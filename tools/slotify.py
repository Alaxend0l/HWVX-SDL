#!/usr/bin/env python3
"""Rewrite raw pointer-slot accesses in game code to SLOT_LOAD / SLOT_STORE.

The decomp reads pointers out of structs by byte offset:

    M2C_FIELD(e, T **, off)      *(T **)(addr)      ((T **)base)[i]

On 64-bit hosts those slots still hold 4 bytes (a handle, see src/port/port.h),
so reads become SLOT_LOAD(T *, addr) and simple assignments become
SLOT_STORE(addr, value). Anything else (address-of, ++, +=, T ***) is left
unchanged and listed, for the compiler and a human to resolve.

  tools/slotify.py src/game/*.c
"""
import re
import sys

TYPE = r"(?:const\s+)?(?:struct\s+|union\s+|unsigned\s+)?\w+"
M2C = re.compile(r"M2C_FIELD\s*\(")
DEREF_CAST = re.compile(r"\*\s*\(\s*(" + TYPE + r")\s*\*\s*\*\s*\)\s*")
PTRARITH = re.compile(r"\*\s*\(\s*\(\s*(" + TYPE + r")\s*\*\s*\*\s*\)\s*")
INDEX_CAST = re.compile(r"\(\s*\(\s*(" + TYPE + r")\s*\*\s*\*\s*\)\s*")


def match_paren(s, i):
    """s[i] == '(' -> index of the matching ')'."""
    depth = 0
    j = i
    while j < len(s):
        c = s[j]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
            if depth == 0:
                return j
        elif c in "\"'":
            q = c
            j += 1
            while s[j] != q:
                j += 2 if s[j] == "\\" else 1
        j += 1
    raise ValueError("unbalanced")


def split_args(s):
    out, depth, cur = [], 0, ""
    for c in s:
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        if c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    out.append(cur.strip())
    return out


def operand_end(s, i):
    """End of a unary-cast operand starting at s[i]: (...) or identifier chain."""
    if s[i] == "(":
        return match_paren(s, i) + 1
    m = re.match(r"[A-Za-z_]\w*", s[i:])
    if not m:
        return None
    j = i + m.end()
    # allow member access / indexing chains like a->b.c[2]
    while True:
        m2 = re.match(r"(?:->|\.)[A-Za-z_]\w*", s[j:])
        if m2:
            j += m2.end()
            continue
        if j < len(s) and s[j] in "[(":
            j = match_paren(s, j) + 1
            continue
        return j


def assignment_rhs(s, j):
    """If s[j:] starts with `= rhs` (not ==), return (rhs_start, rhs_end)."""
    m = re.match(r"\s*=(?!=)", s[j:])
    if not m:
        return None
    k = j + m.end()
    depth = 0
    e = k
    while e < len(s):
        c = s[e]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif c in ";," and depth == 0:
            break
        elif c in "\"'":
            q = c
            e += 1
            while s[e] != q:
                e += 2 if s[e] == "\\" else 1
        e += 1
    return k, e


def preceded_by_addr_or_incdec(s, i):
    k = i - 1
    while k >= 0 and s[k] in " \t":
        k -= 1
    if k >= 0 and s[k] == "&" and (k == 0 or s[k - 1] != "&"):
        return True
    return s[max(0, k - 1):k + 1] in ("++", "--")


def followed_by_bad_op(s, j):
    return re.match(r"\s*(\+\+|--|[-+*/|&^]=|<<=|>>=)", s[j:]) is not None


def rewrite(src, fname, report):
    out = src
    changed = True
    # Iterate to a fixed point: inner occurrences get rewritten first because
    # we always rewrite the *last* match in the text, which is innermost-or-later.
    while changed:
        changed = False
        cands = []
        for m in M2C.finditer(out):
            open_i = m.end() - 1
            close_i = match_paren(out, open_i)
            args = split_args(out[open_i + 1:close_i])
            if len(args) == 3 and re.fullmatch(TYPE + r"\s*\*\s*\*", args[1]):
                t = re.sub(r"\s*\*\s*\*$", "", args[1])
                addr = f"(u8 *)({args[0]}) + {args[2]}"
                cands.append((m.start(), close_i + 1, t, addr))
        for m in DEREF_CAST.finditer(out):
            j = operand_end(out, m.end())
            if j is None:
                continue
            # `*(T **)x` is always a dereference: a T ** can't be multiplied.
            cands.append((m.start(), j, m.group(1), out[m.end():j]))
        for m in INDEX_CAST.finditer(out):
            base_end = operand_end(out, m.end())
            if base_end is None or base_end >= len(out) or out[base_end] != ")":
                continue
            j = base_end + 1
            if j >= len(out) or out[j] != "[":
                continue
            idx_end = match_paren(out, j)
            base = out[m.end():base_end]
            idx = out[j + 1:idx_end]
            cands.append((m.start(), idx_end + 1, m.group(1), f"(u32 *)({base}) + ({idx})"))
        for m in PTRARITH.finditer(out):
            # *((T **)base + n)
            base_end = operand_end(out, m.end())
            if base_end is None:
                continue
            pm = re.match(r"\s*\+\s*", out[base_end:])
            if not pm:
                continue
            # find the ')' closing the paren opened right after the '*'
            open_i = out.index("(", m.start() + 1)
            close = match_paren(out, open_i)
            n = out[base_end + pm.end():close]
            base = out[m.end():base_end]
            cands.append((m.start(), close + 1, m.group(1), f"(u32 *)({base}) + ({n})"))
        cands.sort(key=lambda c: -c[0])
        for start, end, t, addr in cands:
            line = out.count("\n", 0, start) + 1
            if preceded_by_addr_or_incdec(out, start) or followed_by_bad_op(out, end):
                key = (fname, line)
                if key not in report:
                    report[key] = out[start:end]
                continue
            rhs = assignment_rhs(out, end)
            # `**(T **)a = v` assigns through the loaded pointer, not to the slot.
            k = start - 1
            while k >= 0 and out[k] in " \t":
                k -= 1
            if rhs and not (k >= 0 and out[k] == "*" and (k == 0 or out[k - 1] in " \t\n(=!&|,;")):
                new = f"SLOT_STORE({addr}, {out[rhs[0]:rhs[1]].strip()})"
                out = out[:start] + new + out[rhs[1]:]
            else:
                out = out[:start] + f"SLOT_LOAD({t} *, {addr})" + out[end:]
            changed = True
            break  # positions shifted; rescan
    return out


def main():
    report = {}
    for path in sys.argv[1:]:
        src = open(path).read()
        out = rewrite(src, path.split("/")[-1], report)
        if out != src:
            open(path, "w").write(out)
            print(f"{path}: {out.count('SLOT_') - src.count('SLOT_')} rewrites", file=sys.stderr)
    for (f, line), text in sorted(report.items()):
        print(f"{f}:{line}: manual: {text}")


if __name__ == "__main__":
    main()
