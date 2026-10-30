#!/usr/bin/env python3
"""Generate C definitions for game globals that only exist in the original
binary's data sections.

The decomp keeps much of .data/.bss in split assembly rather than C. This reads
the objects from HWVX-GC-Decomp/build/HOTWHE/asm/*.s (produced by the decomp's
build) and writes src/game/data_gen.c:

- zero-initialised objects (.bss/.sbss) become aligned byte arrays twice the
  original size (room for host pointers where the original had 4 bytes);
- initialised objects become byte arrays in *host* byte order (W16/W32/W64
  macros pick the order at compile time, so the file is endian-portable);
- every `.4byte symbol+addend` becomes a 4-byte slot filled at startup with
  HND(&symbol + addend), so pointer tables (vtables, string tables) hold
  32-bit handles like every other pointer slot in the game.

Local objects those tables point at (string literals, nested tables) are
pulled in automatically.

  tools/gen_data.py NAME...        # or: tools/gen_data.py @names.txt
  tools/gen_data.py --raw rawData NAME...  # rawData's "pointers" are image data
  tools/gen_data.py --hostptr g_pX NAME... # g_pX is `T *g_pX` in the C code
  tools/gen_data.py --bytes g_tbl NAME...  # g_tbl holds u8/u16 fields: keep the
                                           # original byte order (as pack data)
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


def load_addresses():
    out = {}
    for line in SYMBOLS.read_text().splitlines():
        m = re.match(r"(\S+) = \.\w+:0x(\w+);", line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


ADDRESSES = load_addresses()


def address_of(sym):
    if sym in ADDRESSES:
        return ADDRESSES[sym]
    m = re.search(r"_([0-9A-F]{8})$", sym)  # asm built before a rename: Name_ADDR
    if m:
        return int(m.group(1), 16)
    raise KeyError(sym)


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
        self.exported = set()  # local objects the game's C refers to by name
        self.raw = set()       # objects whose "pointers" are really plain data
        self.hostptr = set()   # objects the C code declares as host pointers
        self.bytes = set()     # byte/halfword tables: emitted in the original byte order
        self.late = []

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
        if obj["scope"] == "local" and key not in self.exported:
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
            if d in (".4byte", ".2byte", ".byte", ".float", ".double"):
                arg = arg.split("#")[0].strip()  # e.g. `.4byte 0x7FFFFFFF # NaN`
            if d == ".4byte":
                for a in [x.strip() for x in arg.split(",")]:
                    if re.fullmatch(r"-?(0x[0-9A-Fa-f]+|\d+)", a):
                        v = int(a, 0) & 0xFFFFFFFF
                        if obj["name"] in self.bytes:
                            items += [("W8", (v >> sh) & 0xFF) for sh in (24, 16, 8, 0)]
                        else:
                            items.append(("W32", v))
                    else:
                        m = re.fullmatch(r"([^+\-]+)([+\-]0x[0-9A-Fa-f]+|[+\-]\d+)?", a)
                        sym, add = m.group(1), int(m.group(2), 0) if m.group(2) else 0
                        if obj["name"] in self.raw:
                            # not a pointer: data that happens to look like an address
                            items.append(("W32", (address_of(sym) + add) & 0xFFFFFFFF))
                        else:
                            fixups.append((size, sym, add))
                            items.append(("W32", 0))
                    size += 4
            elif d == ".2byte":
                for a in arg.split(","):
                    v = int(a.strip(), 0) & 0xFFFF
                    if obj["name"] in self.bytes:
                        items += [("W8", v >> 8), ("W8", v & 0xFF)]
                    else:
                        items.append(("W16", v))
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

        storage = "static " if obj["scope"] == "local" and key not in self.exported else ""
        if obj["name"] in self.hostptr and resolved and len(resolved) * 4 == size:
            # The game's C declares this as host pointer(s) (`T *name`), so
            # emit real pointers with constant initialisers; placed after the
            # other objects so the targets are declared (see main()).
            self.late.append((key, ident, storage, resolved))
            self.done[key] = (ident, [], obj)
            return ""
        is_bss = obj["section"] in BSS or all(i[0] == "ZERO" for i in items)
        decl = f"{storage}u8 {ident}[{max(size, 1)}] __attribute__((aligned({max(obj['align'], 4)})))"
        out = [f"/* {obj['name']} ({obj['section']}, {obj['file']}) */"]
        if is_bss and not resolved:
            # Zero-initialised globals are often host pointers (or arrays of
            # them) in the game code, which take 8 bytes where the original
            # had 4; twice the size leaves room for that.
            decl = f"{storage}u8 {ident}[{max(size * 2, 8)}] __attribute__((aligned(16)))"
            out.append(decl + ";  /* original size 0x%X */" % size)
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
    raw = set()
    hostptr = set()
    nbytes = set()
    while args and args[0] in ("--raw", "--hostptr", "--bytes"):
        {"--raw": raw, "--hostptr": hostptr, "--bytes": nbytes}[args[0]].add(args[1])
        args = args[2:]
    for a in args:
        if a.startswith("@"):
            names += pathlib.Path(a[1:]).read_text().split()
        else:
            names.append(a)
    objs = load_objects()
    em = Emitter(objs)
    em.raw = raw
    em.hostptr = hostptr
    em.bytes = nbytes
    functions = load_functions()
    not_found = []
    for n in names:
        if n in objs:
            em.want(n)
            continue
        # file-local in the original, but the port's C names it from elsewhere
        local = [k for k in objs if k.endswith(":" + n)]
        if len(local) == 1:
            em.exported.add(local[0])
            em.want(local[0])
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
    for key, ident, storage, resolved in em.late:
        vals = []
        for off, kind, target, add in sorted(resolved):
            if kind == "obj":
                t = em.done[target][0] if target in em.done else em.c_ident(target)
                vals.append(f"(u8 *){t} + {add}")
            else:
                externs.add(target)
                vals.append(f"(u8 *)&{target} + {add}")
        chunks.append(f"/* {em.objs[key]['name']}: host pointer(s) */\n"
                      f"{storage}void *{ident}[{len(vals)}] = {{ {', '.join(vals)} }};")
    # after the late (host pointer) objects, which can add externs too
    hdr += [f"extern void {s}(void);" if s in functions else f"extern char {s}[];"
            for s in sorted(externs)] + [""]
    chunks = [c for c in chunks if c]
    body = "\n\n".join(chunks)
    tail = ["", "/* Fill pointer slots with handles once, before main(). */",
            "__attribute__((constructor)) static void DataGen_Fixups(void) {"] + fix_lines + ["}", ""]
    OUT.write_text("\n".join(hdr) + body + "\n" + "\n".join(tail))
    print(f"{len(em.done)} objects, {len(fix_lines)} pointer fixups -> {OUT.relative_to(ROOT)}")
    if not_found:
        print("not in asm:", " ".join(not_found))


if __name__ == "__main__":
    main()
