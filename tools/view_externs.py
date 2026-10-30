#!/usr/bin/env python3
"""Resolve "conflicting types" between block-scope externs of one global.

Decomp functions each declare the globals they use, inside the function, with
whatever type fits that function (often a struct type local to it).
CodeWarrior accepted different types for the same object in one file; GCC
does not. Each declaration is a valid *view* of the object for its function,
so this keeps them as views: inside the function, the declaration

    extern Interface g_interface[];

becomes a self-referential macro (the inner name is not re-expanded, so it
names the object itself)

    #define g_interface ((Interface *)(void *)&g_interface)

with an #undef before the function's closing brace, and the file gets one
file-scope declaration of the object for the macro to refer to.

Names are taken from GCC's "conflicting types for" errors in a build log:

  ninja -C build -k 0 > build.log 2>&1; tools/view_externs.py build.log

Function pointers and pointer-vs-integer conflicts are left alone: with 32-bit
handles those are real differences (see README) and need a human.
"""
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = ROOT / "src" / "game"
PROTOS = GAME / "include" / "game_protos.h"
INTS = {"s8", "u8", "s16", "u16", "s32", "u32", "int", "char", "short", "long",
        "signed", "unsigned", "BOOL"}


def depth_map(text):
    """depth[i] = brace depth before text[i] (comments/strings skipped)."""
    depth = [0] * (len(text) + 1)
    d = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                depth[k] = d
            i = j
            continue
        if text.startswith("//", i) or c in "\"'":
            if c in "\"'":
                j = i + 1
                while j < n and text[j] != c and text[j] != "\n":
                    j += 2 if text[j] == "\\" else 1
                j += 1
            else:
                j = text.find("\n", i)
                j = n if j < 0 else j
            for k in range(i, min(j, n)):
                depth[k] = d
            i = j
            continue
        depth[i] = d
        if c == "{":
            d += 1
        elif c == "}":
            d -= 1
        i += 1
    depth[n] = d
    return depth


def func_end(text, depth, pos):
    """Offset of the closing brace of the top-level block containing pos."""
    i = pos
    while i < len(text):
        if text[i] == "}" and depth[i] == 1:
            return i
        i += 1
    raise ValueError("no function end")


def func_start(text, depth, pos):
    """Offset of the start of the line where the top-level definition begins."""
    i = pos
    while i > 0 and not (text[i] == "{" and depth[i] == 0):
        i -= 1
    # back to the start of the declarator line(s): previous ; } or blank line
    j = text.rfind("\n\n", 0, i)
    k = max(text.rfind(";", 0, i), text.rfind("}", 0, i))
    j = max(j, k)
    return text.find("\n", j) + 1 if j >= 0 else 0


def view(base, stars, dims):
    t = f"{base} {stars}" if stars else base
    ptr = t + "*" if stars else t + " *"
    if not dims:
        return f"(*({ptr})(void *)&{{n}})"
    rest = "".join(dims[1:])
    if rest:
        return f"(({t} (*){rest})(void *)&{{n}})"
    return f"(({ptr})(void *)&{{n}})"


def header_declares(name):
    for h in (GAME / "include").glob("*.h"):
        if h == PROTOS:
            continue
        if re.search(r"^\s*extern\b[^;]*\b" + name + r"\b", h.read_text(), re.M):
            return True
    return False


def process(path, names, report):
    text = path.read_text()
    edits = []          # (start, end, replacement)
    undefs = collections.defaultdict(set)   # function end offset -> names
    first_func = {}     # name -> start of first function using a view
    depth = depth_map(text)
    for name in sorted(names):
        decl = re.compile(
            r"^([ \t]*)extern\s+((?:(?:const|volatile|struct|union|enum|unsigned|signed)\s+)*\w+)"
            r"\s*(\**)\s*\b" + name + r"\s*((?:\[[^\]]*\]\s*)*);[ \t]*(?:(?://|/\*)[^\n]*)?$", re.M)
        found = [m for m in decl.finditer(text) if depth[m.start()] > 0]
        if not found:
            continue
        kinds = {("ptr" if m.group(3) or m.group(4) else
                  "int" if set(m.group(2).split()) & INTS else "other") for m in found}
        if "int" in kinds and "ptr" in kinds and not all(m.group(4) for m in found):
            report.append(f"{path.name}: {name}: pointer and integer views, fix by hand")
            continue
        for m in found:
            dims = re.findall(r"\[[^\]]*\]", m.group(4))
            v = view(" ".join(m.group(2).split()), m.group(3), dims).format(n=name)
            edits.append((m.start(), m.end(), f"{m.group(1)}#define {name} {v}"))
            end = func_end(text, depth, m.start())
            undefs[end].add(name)
            fs = func_start(text, depth, m.start())
            first_func[name] = min(first_func.get(name, fs), fs)
    if not edits:
        return 0
    for end, ns in undefs.items():
        edits.append((end, end, "".join(f"#undef {n}\n" for n in sorted(ns))))
    # a file-scope declaration for the macros to refer to
    for name, fs in first_func.items():
        before = text[:fs]
        if re.search(r"^[^\s#/*][^;{}()]*\b" + name + r"\b\s*(\[[^\]]*\]\s*)*(=|;)", before, re.M) \
                or header_declares(name):
            continue
        later = re.search(r"^((?:static\s+)?[A-Za-z_][\w \*]*?\b" + name +
                          r"\s*(?:\[[^\]]*\]\s*)*)(?:=[^;]*)?;", text[fs:], re.M)
        if later and depth[fs + later.start()] == 0 and "static" not in later.group(1):
            d = "extern " + re.sub(r"^extern\s+", "", later.group(1).strip()) + ";"
        elif later and depth[fs + later.start()] == 0:
            report.append(f"{path.name}: {name}: static defined after first use, fix by hand")
            continue
        else:
            d = f"extern u8 {name}[];  /* typed per function below */"
        edits.append((fs, fs, d + "\n"))
    for s, e, r in sorted(edits, key=lambda x: (x[0], x[1]), reverse=True):
        text = text[:s] + r + text[e:]
    path.write_text(text)
    return len(edits)


def main():
    log = pathlib.Path(sys.argv[1]).read_text()
    funcs = set(re.findall(r"\b(\w+)\(", PROTOS.read_text()))
    per_file = collections.defaultdict(set)
    for f, n in re.findall(r"src/game/(\w+\.c):\d+:\d+: error: conflicting types for ‘(\w+)’", log):
        if n not in funcs:
            per_file[f].add(n)
    report = []
    total = 0
    for f, ns in sorted(per_file.items()):
        total += process(GAME / f, ns, report)
    for r in report:
        print(r)
    print(f"{total} edits in {len(per_file)} files", file=sys.stderr)


if __name__ == "__main__":
    main()
