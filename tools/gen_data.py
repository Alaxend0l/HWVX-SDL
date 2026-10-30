#!/usr/bin/env python3
"""Generate C definitions for game globals that only exist in the original
binary's data sections.

The decomp keeps much of .data/.bss in split assembly rather than C. This reads
the objects from HWVX-GC-Decomp/build/HOTWHE/asm/*.s (produced by the decomp's
build) and writes src/game/data_gen.c:

- zero-initialised objects (.bss/.sbss) become sized, aligned byte arrays;
- initialised objects become byte arrays in *host* byte order (W16/W32/W64
  macros pick the order at compile time, so the file is endian-portable);
- every `.4byte symbol+addend` becomes a 4-byte slot filled at startup with
  HND(&symbol + addend), so pointer tables (vtables, string tables) hold
  32-bit handles like every other pointer slot in the game.

Local objects those tables point at (string literals, nested tables) are
pulled in automatically.

  tools/gen_data.py NAME...        # or: tools/gen_data.py @names.txt
"""
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASM = ROOT / "HWVX-GC-Decomp/build/HOTWHE/asm"
SYMBOLS = ROOT / "HWVX-GC-Decomp/config/HOTWHE/symbols.txt"
OUT = ROOT / "src/game/data_gen.c"

BSS = (".bss", ".sbss", ".sbss2")


def load_objects():
    """name -> dict(section, align, lines, file)"""
    objs = {}
    for f in sorted(ASM.glob("*.s")):
        section = None
        align = 4
        cur = None
        for raw in f.read_text(errors="replace").splitlines():
            line = raw.strip()
            m = re.match(r"\.section\s+(\S+?),?(\s|$)", line)
            if m:
                section = m.group(1).rstrip(",")
                continue
            if line in (".data", ".text", ".rodata", ".bss"):
                section = line
                continue
            m = re.match(r"\.balign\s+(\d+)", line)
            if m and cur is None:
                align = int(m.group(1))
                continue
            m = re.match(r"\.obj\s+([^,\s]+),\s*(\w+)", line)
            if m:
                cur = {"name": m.group(1), "scope": m.group(2), "section": section,
                       "align": align, "lines": [], "file": f.name}
                continue
            if line.startswith(".endobj"):
                if cur is not None:
                    key = cur["name"]
                    if cur["scope"] == "local":
                        key = f"{f.name}:{key}"
                    objs[key] = cur
                cur = None
                align = 4
                continue
            if cur is not None and line.startswith("."):
                cur["lines"].append(line)
    return objs


def load_functions():
    """Names the original binary has as functions."""
    fns = set()
    for line in SYMBOLS.read_text().splitlines():
        m = re.match(r"(\S+) = \.\w+:0x\w+; // type:function", line)
        if m:
            fns.add(m.group(1))
    return fns


def cname(name):
    """Make a C identifier out of an assembler symbol."""
    return re.sub(r"[^A-Za-z0-9_]", "_", name.lstrip(".")) if not re.fullmatch(r"[A-Za-z_]\w*", name) else name


def parse_string(arg):
    # dtk emits C-style escapes inside "..."
    body = arg.strip()
    assert body.startswith('"') and body.endswith('"'), arg
    out = bytearray()
    s = body[1:-1]
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\":
            n = s[i + 1]
            if n in "01234567":
                j = i + 1
                while j < len(s) and j < i + 4 and s[j] in "01234567":
                    j += 1
                out.append(int(s[i + 1:j], 8) & 0xFF)
                i = j
                continue
            if n == "x":
                j = i + 2
                while j < len(s) and s[j] in "0123456789abcdefABCDEF":
                    j += 1
                out.append(int(s[i + 2:j], 16) & 0xFF)
                i = j
                continue
            out.append({"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, '"': 34, "'": 39,
                        "a": 7, "b": 8, "f": 12, "v": 11}[n])
            i += 2
            continue
        out.extend(c.encode("latin-1"))
        i += 1
    return bytes(out)


class Emitter:
    def __init__(self, objs):
        self.objs = objs
        self.done = {}
        self.queue = []
        self.missing = []

    def resolve(self, name, file):
        """Find the object for a reference made from `file`."""
        if f"{file}:{name}" in self.objs:
            return f"{file}:{name}"
        if name in self.objs:
            return name
        return None

    def want(self, key):
        if key not in self.done and key not in self.queue:
            self.queue.append(key)

    def c_ident(self, key):
        obj = self.objs[key]
        if obj["scope"] == "local":
            return "s_" + cname(obj["file"].replace(".s", "")) + "_" + cname(obj["name"])
        return cname(obj["name"])

    def emit(self, key):
        obj = self.objs[key]
        ident = self.c_ident(key)
        items = []  # ("W8"/"W16"/"W32"/"W64", value) or ("ZERO", n)
        fixups = []
        size = 0
        for line in obj["lines"]:
            d, _, arg = line.partition(" ")
            arg = arg.strip()
            if d == ".4byte":
                for a in [x.strip() for x in arg.split(",")]:
                    if re.fullmatch(r"-?(0x[0-9A-Fa-f]+|\d+)", a):
                        items.append(("W32", int(a, 0) & 0xFFFFFFFF))
                    else:
                        m = re.fullmatch(r"([^+\-]+)([+\-]0x[0-9A-Fa-f]+|[+\-]\d+)?", a)
                        sym, add = m.group(1), int(m.group(2), 0) if m.group(2) else 0
                        fixups.append((size, sym, add))
                        items.append(("W32", 0))
                    size += 4
            elif d == ".2byte":
                for a in arg.split(","):
                    items.append(("W16", int(a.strip(), 0) & 0xFFFF))
                    size += 2
            elif d == ".byte":
                for a in arg.split(","):
                    items.append(("W8", int(a.strip(), 0) & 0xFF))
                    size += 1
            elif d == ".float":
                for a in arg.split(","):
                    items.append(("W32", struct.unpack(">I", struct.pack(">f", float(a)))[0]))
                    size += 4
            elif d == ".double":
                for a in arg.split(","):
                    items.append(("W64", struct.unpack(">Q", struct.pack(">d", float(a)))[0]))
                    size += 8
            elif d in (".string", ".asciz"):
                b = parse_string(arg) + b"\0"
                for x in b:
                    items.append(("W8", x))
                size += len(b)
            elif d == ".ascii":
                b = parse_string(arg)
                for x in b:
                    items.append(("W8", x))
                size += len(b)
            elif d == ".skip":
                n = int(arg.split(",")[0], 0)
                items.append(("ZERO", n))
                size += n
            elif d in (".balign", ".hidden", ".sym", ".rel"):
                continue
            else:
                raise SystemExit(f"{obj['file']}: unhandled directive {line!r} in {obj['name']}")

        resolved = []
        for off, sym, add in fixups:
            k = self.resolve(sym, obj["file"])
            if k is not None and self.objs[k]["section"] in (".text", ".init"):
                k = None
            if k is not None:
                self.want(k)
                resolved.append((off, "obj", k, add))
            elif sym.startswith((".L_", "jumptable", "@")):
                # Code labels (switch jump tables): C never reads these.
                continue
            else:
                resolved.append((off, "sym", sym, add))

        storage = "static " if obj["scope"] == "local" else ""
        is_bss = obj["section"] in BSS or all(i[0] == "ZERO" for i in items)
        decl = f"{storage}u8 {ident}[{max(size, 1)}] __attribute__((aligned({max(obj['align'], 4)})))"
        out = [f"/* {obj['name']} ({obj['section']}, {obj['file']}) */"]
        if is_bss and not resolved:
            out.append(decl + ";")
        else:
            # Designated indices skip zero runs (.skip) without spelling them
            # out; `[pos] = W32(x)` initialises bytes pos..pos+3.
            text, pos = [], 0
            for kind, v in items:
                if kind == "ZERO":
                    pos += v
                    continue
                n = {"W8": 1, "W16": 2, "W32": 4, "W64": 8}[kind]
                text.append(f"[{pos}] = {kind}(0x{v:X})")
                pos += n
            out.append(decl + " = {")
            for i in range(0, len(text), 6):
                out.append("    " + ", ".join(text[i:i + 6]) + ",")
            out.append("};")
        self.done[key] = (ident, resolved, obj)
        return "\n".join(out)


def main():
    args = sys.argv[1:]
    names = []
    for a in args:
        if a.startswith("@"):
            names += pathlib.Path(a[1:]).read_text().split()
        else:
            names.append(a)
    objs = load_objects()
    em = Emitter(objs)
    functions = load_functions()
    not_found = []
    for n in names:
        if n in objs:
            em.want(n)
        else:
            not_found.append(n)
    chunks = []
    while em.queue:
        chunks.append(em.emit(em.queue.pop(0)))

    externs = set()
    fix_lines = []
    for ident, resolved, obj in em.done.values():
        for off, kind, target, add in resolved:
            if kind == "obj":
                t = em.done[target][0] if target in em.done else em.c_ident(target)
                expr = f"(u8 *){t} + {add}"
            else:
                externs.add(target)
                expr = f"(u8 *)&{target} + {add}"
            fix_lines.append(f"    SLOT_STORE({ident} + 0x{off:X}, {expr});")

    hdr = ['/* Generated by tools/gen_data.py from the decomp\'s split data sections.',
           ' * Do not edit; regenerate instead. */',
           '#include "port.h"', '',
           '#if PORT_BIG_ENDIAN',
           '#define W16(v) ((v) >> 8) & 0xFF, (v) & 0xFF',
           '#define W32(v) ((v) >> 24) & 0xFF, ((v) >> 16) & 0xFF, ((v) >> 8) & 0xFF, (v) & 0xFF',
           '#define W64(v) W32((v) >> 32), W32((v) & 0xFFFFFFFFu)',
           '#else',
           '#define W16(v) (v) & 0xFF, ((v) >> 8) & 0xFF',
           '#define W32(v) (v) & 0xFF, ((v) >> 8) & 0xFF, ((v) >> 16) & 0xFF, ((v) >> 24) & 0xFF',
           '#define W64(v) W32((v) & 0xFFFFFFFFu), W32((v) >> 32)',
           '#endif',
           '#define W8(v) (v)', '']
    hdr += [f"extern void {s}(void);" if s in functions else f"extern char {s}[];"
            for s in sorted(externs)] + [""]
    body = "\n\n".join(chunks)
    tail = ["", "/* Fill pointer slots with handles once, before main(). */",
            "__attribute__((constructor)) static void DataGen_Fixups(void) {"] + fix_lines + ["}", ""]
    OUT.write_text("\n".join(hdr) + body + "\n" + "\n".join(tail))
    print(f"{len(em.done)} objects, {len(fix_lines)} pointer fixups -> {OUT.relative_to(ROOT)}")
    if not_found:
        print("not in asm:", " ".join(not_found))


if __name__ == "__main__":
    main()
