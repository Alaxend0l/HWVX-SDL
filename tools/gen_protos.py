#!/usr/bin/env python3
"""Generate src/game/include/game_protos.h from the game's function definitions.

The decomp has no shared headers: every file re-declares what it calls, and the
copies disagree. This collects the prototype of every non-static function
*definition* and puts them in one header (included by common.h); with --strip
it also deletes the local re-declarations so the definition is the single
source of truth.

Functions whose signature uses a type only visible inside one .c file are left
out (callers keep their local declaration); they are listed in the header.

  tools/gen_protos.py [--strip]
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME = ROOT / "src/game"
OUT = GAME / "include/game_protos.h"
PORT_HEADERS = [ROOT / "src/port/port.h", ROOT / "src/port/sdk.h", ROOT / "src/port/gx.h"]

BUILTIN = set("""void char short int long float double signed unsigned const volatile struct union
enum s8 u8 s16 u16 s32 u32 s64 u64 f32 f64 BOOL vu8 vu16 vu32 vs32 vf32 size_t
va_list FILE""".split())


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


def header_types():
    types = set()
    for h in list((GAME / "include").glob("*.h")) + PORT_HEADERS:
        if h == OUT:
            continue
        t = strip_comments(h.read_text())
        types |= set(re.findall(r"\}\s*(\w+)\s*(?:__attribute__\(\(.*?\)\))?\s*;", t))
        types |= set(re.findall(r"typedef\s+[^;{}]*?\b(\w+)\s*;", t))
        types |= set(re.findall(r"typedef\s+[^;{}]*\(\s*\*\s*(\w+)\s*\)", t))
    return types


DEF = re.compile(
    r"^(?P<ret>(?:(?:const|unsigned|signed|struct|union|enum|inline|extern)\s+)*\w+[\s\*]*?)\s*\b"
    r"(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*\{", re.M)


def definitions(text):
    """Yield (ret, name, params) for top-level function definitions."""
    clean = strip_comments(text)
    depth = 0
    starts = []
    for i, c in enumerate(clean):
        if c == "{":
            if depth == 0:
                starts.append(i)
            depth += 1
        elif c == "}":
            depth -= 1
    tops = set(starts)
    for m in DEF.finditer(clean):
        brace = m.end() - 1
        if brace not in tops:
            continue
        ret = " ".join(m.group("ret").split())
        if ret.split()[0] in ("static", "return", "else", "if", "while", "for", "switch") or "static" in ret.split():
            continue
        if m.group("name") in ("if", "while", "for", "switch", "return"):
            continue
        yield ret, m.group("name"), " ".join(m.group("params").split())


def idents(s):
    return set(re.findall(r"[A-Za-z_]\w*", s))


def decl_re(names):
    return re.compile(
        r"^[ \t]*(?:extern[ \t]+)?(?!(?:return|else|case|goto|do|typedef)\b)(?:(?:const|struct|unsigned|signed)[ \t]+)*\w+[\w \t\*]*?\b(" +
        "|".join(map(re.escape, names)) + r")[ \t]*\([^;{}=()]*(?:\([^;{}=()]*\)[^;{}=()]*)*\)[ \t]*;[ \t]*(?://[^\n]*)?\n", re.M)


def guard(fname):
    return re.sub(r"\W", "_", fname.rsplit(".", 1)[0])


def split_params(params):
    out, depth, cur = [], 0, ""
    for c in params:
        if c == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        depth += c in "(["
        depth -= c in ")]"
        cur += c
    return out + [cur]


def erase(ret, name, params, unknown):
    """Signature with pointers to unknown types turned into void *, or None
    if an unknown type is used by value or inside a function pointer."""
    def fix(decl):
        if not idents(decl) & unknown:
            return decl.strip()
        if "(" in decl or "*" not in decl:
            return None
        m = re.match(r"^(.*?)(\**)\s*(\w*)\s*(\[.*\])?\s*$", decl.strip())
        if not m or not m.group(2):
            return None
        return f"void {m.group(2)}{m.group(3)}{m.group(4) or ''}"
    r = ret
    if idents(ret) & unknown:
        if "*" not in ret:
            return None
        r = "void " + "*" * ret.count("*")
    ps = [fix(p) for p in split_params(params)] if params.strip() not in ("", "void") else ["void"]
    if None in ps:
        return None
    sep = "" if r.endswith("*") else " "
    return f"{r}{sep}{name}({', '.join(ps)});"


def main():
    strip = "--strip" in sys.argv
    known = header_types() | BUILTIN
    protos = {}
    skipped = []
    erased = {}
    plain_ret = set()  # erased ones whose return type is unchanged
    for f in sorted(GAME.glob("*.c")):
        for ret, name, params in definitions(f.read_text()):
            # Decomp naming: types are CapitalCase, parameter names lowercase.
            tagged = set(re.findall(r"(?:struct|union|enum)\s+(\w+)", ret + " " + params))
            unknown = {w for w in idents(ret + " " + params)
                       if w[0].isupper() and w not in known and w not in tagged and w != name}
            sig = f"{ret} {name}({params or 'void'});"
            if unknown:
                esig = erase(ret, name, params, unknown)
                if esig and name not in protos and name not in erased:
                    erased[name] = (esig, f.name)
                    if not idents(ret) & unknown:
                        plain_ret.add(name)
                else:
                    skipped.append(f"{name} ({f.name}: {' '.join(sorted(unknown))})")
            elif name in protos and protos[name][0] != sig:
                skipped.append(f"{name} (defined twice: {protos[name][1]}, {f.name})")
                del protos[name]
            else:
                protos[name] = (sig, f.name)

    lines = ["/* Generated by tools/gen_protos.py from the game's function definitions.",
             " * Do not edit by hand; fix the definition and regenerate. */",
             "#ifndef GAME_PROTOS_H", "#define GAME_PROTOS_H", ""]
    tags = set()
    for sig, _ in list(protos.values()) + list(erased.values()):
        tags |= set(re.findall(r"\b((?:struct|union)\s+\w+)", sig))
    lines += [f"{t};" for t in sorted(tags)] + [""]
    cur = None
    for name, (sig, fname) in sorted(protos.items(), key=lambda kv: (kv[1][1], kv[0])):
        if fname != cur:
            lines.append(f"\n/* {fname} */")
            cur = fname
        lines.append(sig)
    lines += ["", "/* Signatures using a type local to their file, with pointers to it as void *.",
              " * Hidden from that file and from files declaring them with their own copy of",
              " * the type (HWVX_SRC_<file> is defined per source by CMake). */"]
    # Files that declare an erased function themselves (with their own copy
    # of the type) keep that declaration and don't see the erased one.
    declared = {n: set() for n in erased}
    if erased:
        dre = decl_re(sorted(erased))
        for f in sorted(GAME.glob("*.c")):
            for m in dre.finditer(f.read_text()):
                if not (strip and m.group(1) in plain_ret):  # stripped below
                    declared[m.group(1)].add(f.name)
    cur = None
    for name, (sig, fname) in sorted(erased.items(), key=lambda kv: (kv[1][1], kv[0])):
        files = sorted({fname} | declared[name])
        cond = " && ".join(f"!defined(HWVX_SRC_{guard(x)})" for x in files)
        if cond != cur:
            if cur:
                lines.append("#endif")
            lines.append(f"#if {cond}")
            cur = cond
        lines.append(sig)
    if cur:
        lines.append("#endif")
    lines += ["", "/* Not declared here (signature uses a file-local type):"]
    lines += [" *   " + s for s in sorted(skipped)]
    lines += [" */", "", "#endif", ""]
    OUT.write_text("\n".join(lines))
    print(f"{len(protos)} prototypes, {len(erased)} with void * params, {len(skipped)} skipped",
          file=sys.stderr)

    if strip:
        # Local declarations of erased functions only add value where the
        # return type was erased (callers use the struct it points to); the
        # void * parameters accept whatever the call sites pass. The defining
        # file doesn't see the erased prototype, so it keeps its own.
        decl = decl_re(sorted(protos))
        for f in sorted(GAME.glob("*.c")) + sorted((GAME / "include").glob("*.h")):
            if f == OUT or f.name == "data_gen.c":  # generated, doesn't include protos
                continue
            src = f.read_text()
            out = decl.sub("", src)
            others = [n for n in plain_ret if erased[n][1] != f.name]
            if others:
                out = decl_re(sorted(others)).sub("", out)
            if out != src:
                f.write_text(out)


if __name__ == "__main__":
    main()
