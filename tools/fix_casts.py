#!/usr/bin/env python3
"""Apply PTR()/HND() at pointer<->integer conversion sites reported by GCC.

On 64-bit hosts any 32-bit integer that carries a pointer must hold a handle
(src/port/port.h). GCC reports every place where the decomp converts between
the two; this wraps the integer side in PTR() and the pointer side in HND():

  (T *)intexpr        -> (T *)PTR(intexpr)
  (u32)ptrexpr        -> (u32)HND(ptrexpr)
  f(ptr) to int param -> f(HND(ptr))       and the reverse with PTR()
  x = ptr / return ptr into an integer      -> HND(...), reverse PTR(...)

Uses GCC's SARIF diagnostics for exact source ranges. Configure a build dir
with -fdiagnostics-add-output=sarif (one .sarif per object file in the build
directory), then repeat until it reports 0 edits:

  cmake -S . -B build-fix -G Ninja -DCMAKE_C_FLAGS=-fdiagnostics-add-output=sarif
  ninja -C build-fix -k 0; tools/fix_casts.py build-fix
"""
import collections
import re
import sys



def match_close(s, i):
    depth = 0
    j = i
    while j < len(s):
        c = s[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
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


def skip_ws(s, i):
    while i < len(s) and s[i] in " \t\n":
        i += 1
    return i


BASIC = set("s8 u8 s16 u16 s32 u32 s64 u64 f32 f64 int char short long float double BOOL void".split())


def is_type(text):
    m = re.fullmatch(r"((?:(?:const|volatile|unsigned|signed)\s+)*)((?:struct|union)\s+)?(\w+)((?:\s*\*)*)", text)
    if not m:
        return False
    return bool(m.group(4)) or bool(m.group(2)) or m.group(3) in BASIC or bool(m.group(1))


def unary_end(s, i):
    """End of the operand of a cast starting at s[i]."""
    i = skip_ws(s, i)
    if s[i] in "*-!~&+":
        if s[i:i + 2] in ("++", "--"):
            return unary_end(s, i + 2)
        return unary_end(s, i + 1)
    if s[i] == "(":
        j = match_close(s, i)
        # (type)operand: a cast, keep going; otherwise a parenthesized expr
        if is_type(s[i + 1:j].strip()):
            return unary_end(s, j + 1)
        k = j + 1
    elif s[i] == '"':
        k = s.index('"', i + 1) + 1
    else:
        m = re.match(r"[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+[uUlL]*|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuUlL]*", s[i:])
        if not m:
            raise ValueError("operand?")
        k = i + m.end()
        if s[i:k] == "sizeof":
            k = skip_ws(s, k)
            return match_close(s, k) + 1
    while True:
        k2 = skip_ws(s, k)
        if k2 < len(s) and s[k2] in "([":
            k = match_close(s, k2) + 1
            continue
        m = re.match(r"(?:->|\.)\s*[A-Za-z_]\w*", s[k2:])
        if m:
            k = k2 + m.end()
            continue
        if s[k2:k2 + 2] in ("++", "--"):
            k = k2 + 2
            continue
        return k


def expr_end(s, i):
    """End of an assignment-expression starting at s[i] (stops at , ) ; })."""
    depth = 0
    j = i
    while j < len(s):
        c = s[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return j
            depth -= 1
        elif c in ",;" and depth == 0:
            return j
        elif c in "\"'":
            q = c
            j += 1
            while s[j] != q:
                j += 2 if s[j] == "\\" else 1
        j += 1
    return j


def line_offsets(s):
    offs = [0]
    for i, c in enumerate(s):
        if c == "\n":
            offs.append(i + 1)
    return offs


def plan(src, offs, region, msg):
    """Return (start, end, wrapper) or None."""
    pos = offs[region["startLine"] - 1] + region["startColumn"] - 1
    end_line = region.get("endLine", region["startLine"])
    rend = offs[end_line - 1] + region["endColumn"] - 1 if "endColumn" in region else None
    if "cast to pointer from integer" in msg or "cast from pointer to integer" in msg:
        w = "PTR" if "to pointer" in msg else "HND"
        if src[pos] == "(":
            close = match_close(src, pos)
            start = skip_ws(src, close + 1)
            return start, unary_end(src, start), w
        # Inside macro arguments GCC reports the macro name; which argument
        # is at fault can't be told from here, so leave it for a human.
        return None
    kind = "PTR" if "makes pointer from integer" in msg else "HND" if "makes integer from pointer" in msg else None
    if kind is None:
        return None
    if msg.startswith("passing argument"):
        if rend is None or rend <= pos:
            return None
        return pos, rend, kind
    if msg.startswith(("assignment", "initialization")):
        eq = src.index("=", pos) if src[pos] != "=" else pos
        if "\n" in src[pos:eq] or src[eq + 1] == "=":
            return None
        start = skip_ws(src, eq + 1)
    elif msg.startswith("returning"):
        start = pos
        if src.startswith("return", start):
            start = skip_ws(src, start + 6)
    else:
        return None
    end = expr_end(src, start)
    while end > start and src[end - 1] in " \t\n":
        end -= 1
    return start, end, kind


def main():
    import json
    import pathlib
    edits = collections.defaultdict(list)
    for sarif in pathlib.Path(sys.argv[1]).rglob("*.sarif"):
        try:
            d = json.loads(sarif.read_text())
        except ValueError:
            continue
        for run in d.get("runs", []):
            for r in run.get("results", []):
                if r.get("level") != "error":
                    continue
                loc = r["locations"][0]["physicalLocation"]
                path = loc["artifactLocation"]["uri"].replace("file://", "")
                edits[path].append((loc["region"], r["message"]["text"]))
    total = 0
    for path, errs in edits.items():
        if "/src/game/" not in path:
            continue
        src = open(path).read()
        offs = line_offsets(src)
        planned = set()
        for region, msg in errs:
            try:
                p = plan(src, offs, region, msg)
            except (ValueError, IndexError):
                p = None
            # A span ending inside an identifier means GCC gave a macro token
            # range, not the expression; leave those for a human.
            if p and p[1] < len(src) and re.match(r"\w\w", src[p[1] - 1:p[1] + 1]):
                p = None
            if p and p[1] > p[0] and not re.match(r"(PTR|HND)\(", src[p[0]:p[1]]):
                planned.add(p)
        # Non-overlapping only; the next round picks up the rest.
        chosen = []
        for p in sorted(planned):
            if chosen and p[0] < chosen[-1][1]:
                continue
            chosen.append(p)
        for start, end, w in reversed(chosen):
            src = src[:start] + f"{w}({src[start:end]})" + src[end:]
        if chosen:
            open(path, "w").write(src)
            total += len(chosen)
    print(f"{total} edits")


if __name__ == "__main__":
    main()
