#!/usr/bin/env python3
"""Turn pointer fields in game structs into 4-byte handles (src/port/port.h).

Pass 1 - declarations, inside every struct/union body:
    T *name;              -> PTR32(T) name;
    T *name[N];           -> PTR32(T) name[N];
    R (*name)(A);         -> FPTR32(R, (A)) name;
  `T **name` becomes PTR32(T *) and is listed for review: what it points at is
  usually an array of 4-byte slots, which needs a human decision.

Pass 2 - accesses `x->name` / `x.name` of converted fields:
    x->name = v           -> SETP(x->name, v)
    x->name               -> GETP(x->name)
  Address-of, ++/--/+=, and designated initializers are left alone and listed.
  Field names that are pointers in one struct and not in another are rewritten
  anyway; the compiler rejects GETP on a non-PTR32 member, and
  `fieldify.py --revert build.log` undoes exactly those sites.

  tools/fieldify.py src/game/*.c src/game/include/*.h

After a decomp merge, rewrite only the merged files (names still come from all):

  tools/fieldify.py --only src/game/A.c,src/game/B.c src/game/*.c src/game/include/*.h

Idempotent: existing PTR32 fields are picked up and already wrapped accesses
are left alone, so it can be rerun after merging new decomp code.
"""
import bisect
import pathlib
import re
import sys

TYPE = r"(?:(?:const|volatile|unsigned|signed|struct|union|enum)\s+)*\w+"
FIELD = re.compile(r"^(?P<ind>[ \t]*)(?P<base>" + TYPE + r")\s*(?P<decls>\*[^;]*?);(?P<rest>.*)$")
FNPTR = re.compile(
    r"^(?P<ind>[ \t]*)(?P<ret>" + TYPE + r"\s*\**)\s*\(\s*\*\s*(?P<name>\w+)\s*(?P<arr>(?:\[[^\]]*\])*)\s*\)\s*"
    r"\((?P<args>[^;]*)\)\s*;(?P<rest>.*)$")
DECL = re.compile(r"^\s*(?P<stars>\*+)\s*(?:const\s+)?(?P<name>\w+)\s*(?P<arr>(?:\[[^\]]*\])*)\s*$")
OPEN = re.compile(r"\b(struct|union)\b(\s+\w+)?\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?\{")
SKIP_BASES = {"return", "typedef", "else", "case", "goto", "PTR32", "FPTR32"}


def match_close(s, i):
    pairs = {"(": ")", "[": "]", "{": "}"}
    depth = 0
    j = i
    while j < len(s):
        c = s[j]
        if s.startswith("/*", j):
            j = s.index("*/", j + 2) + 2
            continue
        if s.startswith("//", j):
            j = s.find("\n", j)
            continue
        if c in pairs:
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


def match_open(s, j):
    """s[j] is a closing bracket; return index of its opener."""
    pairs = {")": "(", "]": "[", "}": "{"}
    depth = 0
    i = j
    while i >= 0:
        c = s[i]
        if c in pairs:
            depth += 1
        elif c in "([{":
            depth -= 1
            if depth == 0:
                return i
        i -= 1
    raise ValueError("unbalanced")


# ---- pass 1 ---------------------------------------------------------------

def note_field(fields, name, is_array, where):
    f = fields.setdefault(name, {"array": set(), "files": {}})
    f["array"].add(is_array)
    f["files"].setdefault(where.split(":")[0], set()).add(is_array)


def convert_line(line, fields, review, where):
    m = FNPTR.match(line)
    if m:
        args = m.group("args").strip() or "void"
        ret = " ".join(m.group("ret").split())
        note_field(fields, m.group("name"), bool(m.group("arr")), where)
        return f"{m.group('ind')}FPTR32({ret}, ({args})) {m.group('name')}{m.group('arr')};{m.group('rest')}"
    m = FIELD.match(line)
    if not m or m.group("base").split()[0] in SKIP_BASES:
        return line
    base = " ".join(m.group("base").split())
    outs = []
    for d in m.group("decls").split(","):
        dm = DECL.match(d)
        if not dm:
            return line
        outs.append((len(dm.group("stars")), dm.group("name"), dm.group("arr")))
    lines = []
    for nstars, name, arr in outs:
        if nstars == 0:
            lines.append(f"{m.group('ind')}{base} {name}{arr};")
            continue
        pointee = base + " " + "*" * (nstars - 1) if nstars > 1 else base
        if nstars > 1:
            review.append(f"{where}: {name}: {base} {'*' * nstars} field -> PTR32({pointee})")
        note_field(fields, name, bool(arr), where)
        lines.append(f"{m.group('ind')}PTR32({pointee.strip()}) {name}{arr};")
    lines[-1] += m.group("rest")
    return "\n".join(lines)


EXISTING = re.compile(r"^[ \t]*F?PTR32\((?:[^()]|\([^()]*\))*\)\s*(\w+)\s*(\[[^;]*\])?\s*;", re.M)


def note_existing(text, fields, fname):
    for m in EXISTING.finditer(text):
        note_field(fields, m.group(1), bool(m.group(2)), fname)


def convert_decls(text, fields, review, fname):
    spans = []
    for m in OPEN.finditer(text):
        try:
            end = match_close(text, m.end() - 1)
        except ValueError:
            continue
        if spans and m.start() < spans[-1][1]:
            continue  # nested: handled with the enclosing body
        spans.append((m.end(), end))
    out, last = [], 0
    for s, e in spans:
        out.append(text[last:s])
        body = text[s:e]
        line0 = text.count("\n", 0, s) + 1
        if "\n" not in body and ";" in body:
            # one-line struct: `struct { u32 a; T *p; }`
            parts = [p for p in body.split(";")]
            conv = [convert_line(p + ";", fields, review, f"{fname}:{line0}")[:-1]
                    if p.strip() else p for p in parts[:-1]]
            out.append(";".join(conv + [parts[-1]]))
            last = e
            continue
        new = []
        for k, l in enumerate(body.split("\n")):
            new.append(convert_line(l, fields, review, f"{fname}:{line0 + k}"))
        out.append("\n".join(new))
        last = e
    out.append(text[last:])
    return "".join(out)


# ---- pass 2 ---------------------------------------------------------------

def postfix_start(s, i):
    """s[i] is the first char of '->' or '.'; find where its operand begins."""
    j = i - 1
    while True:
        while j >= 0 and s[j] in " \t\n":
            j -= 1
        if j < 0:
            return 0
        if s[j] in ")]":
            j = match_open(s, j) - 1
            # a call or subscript continues the operand; a cast does not:
            # treat (expr) as the start unless an identifier precedes it
            k = j
            while k >= 0 and s[k] in " \t":
                k -= 1
            if k >= 0 and s[k] == ")":
                # (T *)(expr): the group before is a cast, not a call
                o = match_open(s, k)
                if re.fullmatch(r"\s*" + TYPE + r"[\s\*]*", s[o + 1:k]) and "*" in s[o + 1:k] \
                        or re.fullmatch(r"\s*(?:const\s+)?(?:s8|u8|s16|u16|s32|u32|f32|f64|int|char|float|double)\s*", s[o + 1:k]):
                    return j + 1
            if k >= 0 and (s[k].isalnum() or s[k] in "_)]"):
                j = k
                continue
            return j + 1
        if s[j].isalnum() or s[j] == "_":
            while j >= 0 and (s[j].isalnum() or s[j] == "_"):
                j -= 1
            k = j
            while k >= 0 and s[k] in " \t":
                k -= 1
            if k >= 1 and s[k - 1:k + 1] == "->":
                j = k - 2
                continue
            if k >= 0 and s[k] == "." and not (k >= 1 and s[k - 1].isdigit()):
                j = k - 1
                continue
            return j + 1
        return j + 1


def assignment_rhs(s, j):
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


NONCODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def noncode_spans(text):
    """Start/end offsets of comments and string/char literals."""
    return [(m.start(), m.end()) for m in NONCODE.finditer(text)]


def in_spans(spans, i):
    k = bisect.bisect_right(spans, (i, float("inf"))) - 1
    return k >= 0 and spans[k][0] <= i < spans[k][1]


def rewrite_accesses(text, fields, review, fname, line_base=0):
    names = "|".join(sorted(map(re.escape, fields), key=len, reverse=True))
    if not names:
        return text
    acc = re.compile(r"(->|(?<![\w.])\.|(?<=[\w\)\]])\.)\s*(" + names + r")\b")
    changed = True
    while changed:
        changed = False
        # Left to right, so a->b->c becomes GETP(GETP(a->b)->c).
        noncode = noncode_spans(text)
        for m in acc.finditer(text):
            name = m.group(2)
            op_i = m.start()
            if in_spans(noncode, op_i):
                continue
            # already wrapped by an earlier rewrite
            if re.search(r"(GETP|SETP)\(\s*$", text[:postfix_start(text, op_i)][-12:]):
                continue
            k = op_i - 1
            while k >= 0 and text[k] in " \t\n":
                k -= 1
            if text[op_i] == "." and k >= 0 and text[k] in "{,":
                line = line_base + text.count("\n", 0, op_i) + 1
                review.append(f"{fname}:{line}: designated initializer .{name}")
                continue
            start = postfix_start(text, op_i)
            end = m.end()
            # Array or not: this file's own declarations win, then headers,
            # then all; if still mixed, a following [ means array indexing.
            f = fields[name]
            kinds = f["files"].get(fname) or set().union(
                *[v for k, v in f["files"].items() if k.endswith(".h")] or [set()]) or f["array"]
            if len(kinds) > 1:
                kinds = {re.match(r"\s*\[", text[end:]) is not None}
            if True in kinds:
                e = end
                while True:
                    e2 = e
                    while e2 < len(text) and text[e2] in " \t":
                        e2 += 1
                    if e2 < len(text) and text[e2] == "[":
                        e = match_close(text, e2) + 1
                    else:
                        break
                end = e
            # address-of / inc-dec / compound assignment: leave for a human
            p = start - 1
            while p >= 0 and text[p] in " \t":
                p -= 1
            line = line_base + text.count("\n", 0, op_i) + 1
            # & and prefix ++ bind to the whole postfix chain; they only
            # concern this field if the chain ends here.
            continues = re.match(r"\s*(->|\.(?!\d)|\[)", text[end:]) is not None
            if not continues and p >= 0 and text[p] == "&" and not (p >= 1 and text[p - 1] == "&"):
                review.append(f"{fname}:{line}: address of {name}")
                continue
            if re.match(r"\s*(\+\+|--|[-+*/|&^]=|<<=|>>=)", text[end:]) or \
                    (not continues and text[max(0, p - 1):p + 1] in ("++", "--")):
                review.append(f"{fname}:{line}: arithmetic update of {name}")
                continue
            expr = text[start:end]
            rhs = assignment_rhs(text, end)
            # `*x->f = v` and `x->f[i] = v` store through the pointer
            deref = p >= 0 and text[p] == "*" and not continues and \
                (p == 0 or text[p - 1] in " \t\n(=!&|,;{}")
            if rhs and not deref:
                new = f"SETP({expr}, {text[rhs[0]:rhs[1]].strip()})"
                text = text[:start] + new + text[rhs[1]:]
            else:
                text = text[:start] + f"GETP({expr})" + text[end:]
            changed = True
            break
    return text


def main():
    args = sys.argv[1:]
    if args and args[0] == "--revert":
        revert(args[1])
        return
    only = None
    if args and args[0] == "--only":
        only = {pathlib.Path(p).resolve() for p in args[1].split(",")}
        args = args[2:]
    fields = {}
    review = []
    texts = {p: pathlib.Path(p).read_text() for p in args}
    for p in args:
        # Fields converted by an earlier run still need their new accesses
        # rewritten (e.g. code merged in from the decomp since).
        note_existing(texts[p], fields, pathlib.Path(p).name)
        texts[p] = convert_decls(texts[p], fields, review, pathlib.Path(p).name)
    for p in args:
        out = rewrite_accesses_file(texts[p], fields, review, pathlib.Path(p).name)
        texts[p] = out
    for p, t in texts.items():
        if only is None or pathlib.Path(p).resolve() in only:
            pathlib.Path(p).write_text(t)
    review = list(dict.fromkeys(review))
    pathlib.Path("fieldify_review.txt").write_text("\n".join(review) + "\n")
    print(f"{len(fields)} pointer field names converted; {len(review)} sites to review "
          f"(fieldify_review.txt)", file=sys.stderr)


def rewrite_accesses_file(text, fields, review, fname):
    # Don't rewrite inside struct bodies (declarations).
    spans = []
    for m in OPEN.finditer(text):
        try:
            spans.append((m.end(), match_close(text, m.end() - 1)))
        except ValueError:
            pass
    pieces, last = [], 0
    merged = []
    for s, e in sorted(spans):
        if merged and s < merged[-1][1]:
            continue
        merged.append((s, e))
    for s, e in merged:
        pieces.append(rewrite_accesses(text[last:s], fields, review, fname, text.count("\n", 0, last)))
        pieces.append(text[s:e])
        last = e
    pieces.append(rewrite_accesses(text[last:], fields, review, fname, text.count("\n", 0, last)))
    return "".join(pieces)


def revert(log):
    """Undo GETP/SETP calls that GCC rejected because the member isn't a PTR32
    (a same-named field that is a plain value or native pointer in that struct).

    Reads a text build log; errors inside the macro are attributed to the use
    site through the following "in expansion of macro" note."""
    bad = ("request for member", "has no member named \u2018_t\u2019", "has no member named \u2018h\u2019",
           "is a pointer; did you mean")
    err = re.compile(r"^(/\S+?\.[ch]):(\d+):(\d+): error: (.*)$")
    note = re.compile(r"^(/\S+?\.[ch]):(\d+):(\d+): note: in expansion of macro \u2018(GETP|SETP)\u2019")
    # Notes run from the outermost expansion to the innermost; the innermost
    # GETP/SETP is the one applied to the wrong member.
    sites = set()
    pending = False
    last = None
    for line in open(log, errors="replace"):
        line = line.rstrip("\n")
        m = err.match(line)
        if m:
            if last:
                sites.add(last)
            pending = any(b in m.group(4) for b in bad)
            last = None
            continue
        m = note.match(line)
        if m and pending and "/src/game/" in m.group(1):
            last = (m.group(1), int(m.group(2)), int(m.group(3)), m.group(4))
    if last:
        sites.add(last)
    by_file = {}
    for f, l, c, mac in sites:
        by_file.setdefault(f, []).append((l, c, mac))
    n = 0
    for path, lst in by_file.items():
        src = pathlib.Path(path).read_text()
        offs = [0] + [i + 1 for i, ch in enumerate(src) if ch == "\n"]
        # right to left so earlier offsets stay valid
        for l, c, mac in sorted(lst, key=lambda x: -(offs[x[0] - 1] + x[1])):
            pos = offs[l - 1] + c - 1
            if not src.startswith(mac + "(", pos):
                continue
            o = pos + len(mac)
            close = match_close(src, o)
            inner = src[o + 1:close]
            if mac == "GETP":
                rep = inner
            else:
                depth = 0
                for k, ch in enumerate(inner):
                    if ch in "([{":
                        depth += 1
                    elif ch in ")]}":
                        depth -= 1
                    elif ch == "," and depth == 0:
                        break
                rep = inner[:k] + " = " + inner[k + 1:].strip()
            src = src[:pos] + rep + src[close + 1:]
            n += 1
        pathlib.Path(path).write_text(src)
    print(f"reverted {n} accesses")


if __name__ == "__main__":
    main()
