#!/usr/bin/env python3
"""Pack structs <-> .xdl source JSONC, driven by the generated schemas (schemas/*.schema.json).

    xdl_struct.py extract PACK_DIR [--struct CarAInfo] [--root 0] [--path engine] -o car.jsonc
    xdl_struct.py build FILE.jsonc PACK_DIR [-o out.bin]     bytes + relocations of the object
    xdl_struct.py roundtrip SURVEY_ROOT [--rel gc,pc] [--archive HW_Cars] [--parts] [--out DIR]
    xdl_struct.py observe SURVEY_ROOT [--rel gc,pc]          value ranges -> tools/schema_observed.json

PACK_DIR is one pack of an `hwvx-x extract` tree (data.bin, pointers.bin, *_relocs.bin,
pack.json; the archive's manifest.json one level up). SURVEY_ROOT holds such trees per release
(HWVX-SDL/build-survey/{gc,pc,ps2}).

The schema is the whole contract: x-offset and x-c-type place each field, x-link says which
kind of slot a link is (object pointer: pointers.bin; texture: texture_relocs; geometry: a local
ref reloc whose low half is the geometry's index in the header scan), x-hidden lists the bytes
the JSON leaves out. Writing starts from the base object's bytes and relocations (the
`extends` object; the round trip uses the same car's original bytes), so hidden fields, gaps and
fields an `extends` file leaves out come from the base, and texture slots whose link didn't
change keep the base's bytes (the loader overwrites them; the stored value is tool junk).

Links (docs/xdl-format/pairing.md; objects as a field path below their root object):
    base://HW_Cars/car_5/CarAInfo/0                the car pack's root table entry 0
    base://HW_Cars/car_5/CarAInfo/0/engine         the object CarAInfo.engine points to
    base://HW_Cars/car_5/CarAInfo/0/aiScripts/2    element 2 of a pointer array
    base://HW_Cars/share_2/texture/4               a texture (GameCube index, pairing table)
    base://HW_Cars/car_5/geometry/14               a geometry (header scan order)
"""

import argparse
import json
import math
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMAS = ROOT / "schemas"
OBSERVED = ROOT / "tools" / "schema_observed.json"

PRIM = {  # name: (struct code, size, float)
    "u8": ("B", 1, False), "s8": ("b", 1, False), "char": ("b", 1, False),
    "u16": ("H", 2, False), "s16": ("h", 2, False),
    "u32": ("I", 4, False), "s32": ("i", 4, False), "BOOL": ("i", 4, False),
    "int": ("i", 4, False), "f32": ("f", 4, True), "f64": ("d", 8, True),
}
# share packs by the id texture/sound/ref relocations put in their high half
SHARE = {1: ("HW_share", "share_1"), 2: ("HW_Cars", "share_2"), 3: ("HW_Items", "share_3")}
SHARE_ID = {v: k for k, v in SHARE.items()}
# root objects: struct -> how a pack lists them
ROOTS = {"CarAInfo": "car"}
# the parts round-tripped with --parts: CarAInfo field -> struct
PARTS = {"engine": "CarAEngine", "accel": "CarAAccel", "stunt": "CarAStuntInfo",
         "hud": "CarAHud", "collider": "CarACollider"}
BASE_RE = re.compile(r"^base://([A-Za-z0-9_]+)/([A-Za-z0-9_]+)/([A-Za-z0-9_]+)/((?:[a-z0-9]+:)?[0-9]+)((?:/[A-Za-z0-9_]+)*)$")


class XdlError(Exception):
    pass


# ------------------------------------------------------------------------------------------
# schemas

class Schemas:
    def __init__(self, folder=SCHEMAS):
        self.top, self.defs = {}, {}
        for p in sorted(Path(folder).glob("*.schema.json")):
            s = json.loads(p.read_text())
            self.top[s["title"]] = (s, p)
            for k, v in s.get("$defs", {}).items():
                self.defs.setdefault(k, v)

    def struct(self, name):
        if name in self.top:
            return self.top[name][0]
        if name in self.defs:
            return self.defs[name]
        return None

    def path(self, name):
        return self.top[name][1]

    def ref(self, node):
        r = node.get("$ref")
        if r:
            return self.struct(r.rsplit("/", 1)[1])
        return None

    def size_of(self, ctype):
        base, dims = parse_ctype(ctype)
        if base.startswith("PTR32(") or base == "FPTR32":
            n = 4
        elif base in PRIM:
            n = PRIM[base][1]
        else:
            s = self.struct(base)
            if s is None:
                raise XdlError(f"size of {base} unknown (no schema)")
            n = int(s["x-size"], 16)
        for d in dims:
            n *= d
        return n


def parse_ctype(ct):
    """'f32[3]' -> ('f32', [3]); 'PTR32(PTR32(char))' -> ('PTR32(PTR32(char))', [])."""
    dims = []
    m = re.match(r"^(.*?)((?:\[\d+\])*)$", ct)
    base = m.group(1)
    for d in re.findall(r"\[(\d+)\]", m.group(2)):
        dims.append(int(d))
    return base, dims


def fields(sch):
    """[(json key, property schema)] of a struct schema's own fields (not $schema/extends)."""
    return [(k, p) for k, p in sch["properties"].items() if "x-offset" in p]


def leaf(p, depth):
    for _ in range(depth):
        p = p["items"]
    return p


# ------------------------------------------------------------------------------------------
# packs

class Pack:
    def __init__(self, folder):
        self.dir = Path(folder)
        man = json.loads((self.dir.parent / "manifest.json").read_text())
        self.release = man["platform"]
        self.e = ">" if self.release == "gc" else "<"
        ent = [x for x in man["entries"] if x["path"] == self.dir.name]
        if not ent:
            raise XdlError(f"{self.dir.name} not in {self.dir.parent}/manifest.json")
        self.entry = ent[0]
        self.archive = self.dir.parent.name
        self.name = f"{self.entry['type']}_{self.entry['id']}"
        self.blob = (self.dir / "data.bin").read_bytes()
        self.header = json.loads((self.dir / "pack.json").read_text())["header"]
        self.ptrs = set(self._tab("pointers", 4))
        self.tex = dict(self._tab("texture_relocs", 8))
        self.snd = dict(self._tab("sound_relocs", 8))
        self.ref = dict(self._tab("ref_relocs", 8))
        self._geoms = None

    def _tab(self, n, w):
        p = self.dir / f"{n}.bin"
        if not p.exists():
            return []
        b = p.read_bytes()
        if w == 4:
            return list(struct.unpack(self.e + f"{len(b) // 4}I", b))
        return [struct.unpack_from(self.e + "2I", b, i) for i in range(0, len(b), 8)]

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.blob, o)[0]

    def target(self, o):
        if o not in self.ptrs:
            return None
        return self.u32(o)

    def roots(self, kind):
        """Offsets of the pack's root objects (car/item/interface/audio packs: the table at 0)."""
        if kind != self.entry["type"]:
            raise XdlError(f"{self.archive}/{self.name} is a {self.entry['type']} pack, not {kind}")
        n, tab = self.u32(0), self.target(4)
        if tab is None:
            return []
        return [self.target(tab + 8 * i) for i in range(n)]

    def geometries(self):
        """Geometry header offsets in scan order (xdl_gltf.scan; pairing.md §1)."""
        if self._geoms is None:
            here = str(Path(__file__).resolve().parent)
            if here not in sys.path:
                sys.path.insert(0, here)
            import xdl_gltf
            self._geoms = xdl_gltf.scan(xdl_gltf.Blob(self.blob, self.ptrs, self.release))
        return self._geoms

    def relocs_in(self, lo, hi):
        """{kind: {offset - lo: value}} of every slot in [lo, hi)."""
        return {
            "pointers": {o - lo: self.u32(o) for o in self.ptrs if lo <= o < hi},
            "texture": {o - lo: v for o, v in self.tex.items() if lo <= o < hi},
            "sound": {o - lo: v for o, v in self.snd.items() if lo <= o < hi},
            "ref": {o - lo: v for o, v in self.ref.items() if lo <= o < hi},
        }


class Pairing:
    """Release index <-> structural id, from docs/xdl-format/pairing/<archive>.jsonl."""

    def __init__(self, folder):
        self.dir = folder
        self.fwd, self.rev, self.loaded = {}, {}, set()

    def _load(self, archive):
        a = archive.lower()
        if a in self.loaded:
            return
        self.loaded.add(a)
        if self.dir is None:
            return
        for p in self.dir.glob("*.jsonl"):
            if p.stem.lower() != a:
                continue
            for ln in p.read_text().splitlines():
                r = json.loads(ln)
                if "id" not in r:
                    continue
                m = BASE_RE.match(r["id"])
                if not m:
                    continue
                _, pack, kind, _, _ = m.groups()
                for rel in ("gc", "pc", "ps2", "proto"):
                    if rel in r:
                        self.fwd[(rel, a, pack, kind, r[rel]["i"])] = r["id"]
                        self.rev[(rel, r["id"].lower())] = r[rel]["i"]

    def id(self, rel, archive, pack, kind, i):
        self._load(archive)
        x = self.fwd.get((rel, archive.lower(), pack, kind, i))
        if x:
            return x
        if rel == "gc":
            return f"base://{archive}/{pack}/{kind}/{i}"
        raise XdlError(f"no pairing id for {rel} {archive}/{pack} {kind} {i} "
                       f"(docs/xdl-format/pairing/{archive}.jsonl)")

    def index(self, rel, link):
        m = BASE_RE.match(link)
        archive, pack, kind, idx, rest = m.groups()
        self._load(archive)
        i = self.rev.get((rel, link.lower()))
        if i is not None:
            return archive, pack, kind, i
        if rel == "gc" and ":" not in idx:
            return archive, pack, kind, int(idx)
        if idx.startswith(rel + ":"):
            return archive, pack, kind, int(idx.split(":")[1])
        raise XdlError(f"{link} has no {rel} index in the pairing table")


def find_up(rel):
    for d in Path(__file__).resolve().parents:
        if (d / rel).exists():
            return d / rel
    return None


# ------------------------------------------------------------------------------------------
# f32 text

def f32_text(v, code="f"):
    if math.isnan(v) or math.isinf(v):
        raise XdlError(f"{v} has no JSON form")
    if v == 0:
        return "-0.0" if math.copysign(1, v) < 0 else "0"
    if code == "d":
        return repr(v)
    bits = struct.pack("<f", v)
    for p in range(1, 10):
        s = f"{v:.{p}g}"
        if struct.pack("<f", float(s)) == bits:
            break
    s = repr(float(s))            # plain decimal unless very large or small: 7400, 0.3
    if s.endswith(".0"):
        s = s[:-2]
    return s


class Num(float):
    """A float that json.dumps writes with the shortest text that round-trips at f32."""

    def __new__(cls, v, text):
        o = float.__new__(cls, v)
        o.text = text
        return o


# ------------------------------------------------------------------------------------------
# decoding

class Decoder:
    def __init__(self, schemas, pack, pairing):
        self.S, self.pk, self.pair = schemas, pack, pairing
        b = pack
        self.slots = {}                     # offset -> kind of every 4-byte reloc slot
        for o in b.ptrs:
            self.slots[o] = "pointer"
        for o in b.tex:
            self.slots.setdefault(o, "texture")
        for o, v in b.ref.items():
            self.slots.setdefault(o, "ref")
        for o in b.snd:
            self.slots.setdefault(o, "sound")

    def struct(self, sch, base, oid):
        out = {}
        for key, p in fields(sch):
            out[key] = self.value(p, base + int(p["x-offset"], 16), f"{oid}/{key}", p["x-c-type"])
        return out

    def value(self, p, off, path, ctype):
        base, dims = parse_ctype(ctype)
        if dims:
            sub = base + "".join(f"[{d}]" for d in dims[1:])
            esz = self.S.size_of(sub)
            return [self.value(p["items"], off + i * esz, f"{path}/{i}", sub) for i in range(dims[0])]
        if "x-link" in p:
            return self.link(p["x-link"], off, path)
        if "$ref" in p:
            return self.struct(self.S.ref(p), off, path)
        if base not in PRIM:
            raise XdlError(f"{path}: no decoder for {ctype}")
        code, size, flt = PRIM[base]
        for o in range(off - off % 4, off + size, 4):
            if o in self.slots and o + 4 > off:
                raise XdlError(f"{path} (0x{off:X}, {ctype}) is a {self.slots[o]} slot in "
                               f"{self.pk.archive}/{self.pk.name}: the schema should make it a link "
                               "(@xdl marker)")
        v = struct.unpack_from(self.pk.e + code, self.pk.blob, off)[0]
        if flt:
            return Num(v, f32_text(v, code))
        return v

    def link(self, x, off, path):
        pk = self.pk
        kind = self.slots.get(off)
        raw = pk.u32(off)
        want = {"object": "pointer", "texture": "texture", "geometry": "ref"}[x["kind"]]
        if kind is None:
            if raw == 0:
                return None
            raise XdlError(f"{path}: a {x['kind']} link, but 0x{off:X} holds 0x{raw:X} and no relocation")
        if kind != want:
            raise XdlError(f"{path}: a {x['kind']} link, but 0x{off:X} is a {kind} slot")
        if kind == "pointer":
            return path
        if kind == "texture":
            v = pk.tex[off]
            sid, idx = v >> 16, v & 0xFFFF
            arch, pname = (pk.archive, pk.name) if sid == 0 else SHARE[sid]
            return self.pair.id(pk.release, arch, pname, "texture", idx)
        v = pk.ref[off]
        if v >> 16:
            raise XdlError(f"{path}: geometry from another pack (ref 0x{v:X}) not supported yet")
        g = pk.geometries()
        idx = v & 0xFFFF
        if idx >= len(g) or g[idx] != raw:
            raise XdlError(f"{path}: ref reloc {idx} does not match the geometry scan")
        return self.pair.id(pk.release, pk.archive, pk.name, "geometry", idx)


# ------------------------------------------------------------------------------------------
# link resolution (object links into the base pack)

class Resolver:
    def __init__(self, schemas, pack, root_struct):
        self.S, self.pk, self.root_struct = schemas, pack, root_struct

    def resolve(self, link):
        """(target offset in the pack, C type name of what it points to) for an object link."""
        m = BASE_RE.match(link)
        if not m:
            raise XdlError(f"{link}: only base:// object links resolve in this step")
        arch, pname, kind, idx, rest = m.groups()
        if arch.lower() != self.pk.archive.lower() or pname != self.pk.name:
            raise XdlError(f"{link}: points into {arch}/{pname}; objects can only link inside "
                           f"their own pack ({self.pk.archive}/{self.pk.name}) so far")
        sch = self.S.struct(kind)
        if sch is None or kind not in ROOTS:
            raise XdlError(f"{link}: {kind} is not a root object kind")
        roots = self.pk.roots(ROOTS[kind])
        off = roots[int(idx)]
        node = ("struct", kind, None)        # (what, type name / ctype, property schema)
        for seg in [s for s in rest.split("/") if s]:
            off, node = self.step(off, node, seg, link)
        if node[0] == "link":
            t = self.pk.target(off)
            if t is None:
                raise XdlError(f"{link}: the base's slot is not a pointer")
            return t, node[2]["x-link"].get("target")
        return off, node[1]

    def step(self, off, node, seg, link):
        what, ctype, p = node
        if what == "link":
            t = self.pk.target(off)
            if t is None:
                raise XdlError(f"{link}: NULL pointer before /{seg}")
            target = p["x-link"].get("target", "")
            if self.S.struct(target):
                return self.step(t, ("struct", target, None), seg, link)
            # a pointer to an array of T (PTR32(T) or a scalar)
            if not seg.isdigit():
                raise XdlError(f"{link}: /{seg} into {target}")
            if target.startswith("PTR32("):
                inner = target[6:-1]
                return t + 4 * int(seg), ("link", inner, {"x-link": {"kind": "object", "target": inner}})
            return t + self.S.size_of(target) * int(seg), ("struct", target, None)
        if what == "struct":
            sch = self.S.struct(ctype)
            if sch is None:
                raise XdlError(f"{link}: no schema for {ctype}")
            props = dict(fields(sch))
            if seg not in props:
                raise XdlError(f"{link}: {ctype} has no field {seg}")
            q = props[seg]
            return self.place(off + int(q["x-offset"], 16), q, q["x-c-type"])
        if what == "array":
            base, dims = parse_ctype(ctype)
            sub = base + "".join(f"[{d}]" for d in dims[1:])
            i = int(seg)
            if not 0 <= i < dims[0]:
                raise XdlError(f"{link}: index {i} out of [0, {dims[0]})")
            return self.place(off + i * self.S.size_of(sub), p["items"], sub)
        raise XdlError(f"{link}: can't go into /{seg}")

    def place(self, off, p, ctype):
        base, dims = parse_ctype(ctype)
        if dims:
            return off, ("array", ctype, p)
        if "x-link" in p:
            return off, ("link", ctype, p)
        if "$ref" in p:
            return off, ("struct", p["$ref"].rsplit("/", 1)[1], None)
        return off, ("scalar", ctype, p)


# ------------------------------------------------------------------------------------------
# encoding

class Encoder:
    """Writes a JSON object over its base: (bytes, relocations) of the object."""

    def __init__(self, schemas, pack, pairing, struct_name, base_off, base=None):
        """base_off: the base object in the pack (None: no base, zeros); base: (bytes,
        relocations) to start from in place of the pack's (the round trip's scrubbed base)."""
        self.S, self.pk, self.pair = schemas, pack, pairing
        self.sch = schemas.struct(struct_name)
        self.size = int(self.sch["x-size"], 16)
        self.base_off = base_off
        self.dec = Decoder(schemas, pack, pairing)
        self.res = Resolver(schemas, pack, struct_name)
        if base is not None:
            self.out, self.rel = bytearray(base[0]), base[1]
        elif base_off is None:
            self.out = bytearray(self.size)
            self.rel = {"pointers": {}, "texture": {}, "sound": {}, "ref": {}}
        else:
            self.out = bytearray(pack.blob[base_off:base_off + self.size])
            self.rel = pack.relocs_in(base_off, base_off + self.size)
        self.rel = {k: dict(v) for k, v in self.rel.items()}
        self.base_id = None

    def clear(self, lo, hi):
        for k in self.rel:
            for o in [o for o in self.rel[k] if lo <= o + 3 and o < hi]:
                del self.rel[k][o]

    def encode(self, obj, base_id):
        self.base_id = base_id
        self.struct(self.sch, 0, obj, "")
        return bytes(self.out), self.rel

    def struct(self, sch, off, obj, path):
        for key, p in fields(sch):
            if key not in obj:
                continue                     # from the base
            self.value(p, off + int(p["x-offset"], 16), obj[key], f"{path}/{key}", p["x-c-type"])

    def value(self, p, off, v, path, ctype):
        base, dims = parse_ctype(ctype)
        if dims:
            sub = base + "".join(f"[{d}]" for d in dims[1:])
            esz = self.S.size_of(sub)
            if not isinstance(v, list) or len(v) != dims[0]:
                raise XdlError(f"{path}: expected {dims[0]} values")
            for i, x in enumerate(v):
                self.value(p["items"], off + i * esz, x, f"{path}/{i}", sub)
            return
        if "x-link" in p:
            self.link(p["x-link"], off, v, path)
            return
        if "$ref" in p:
            if not isinstance(v, dict):
                raise XdlError(f"{path}: expected an object")
            self.struct(self.S.ref(p), off, v, path)
            return
        code, size, flt = PRIM[base]
        self.clear(off, off + size)
        if flt:
            v = float(v)
        else:
            if isinstance(v, float):
                if v != int(v):
                    raise XdlError(f"{path}: {v} is not an integer")
                v = int(v)
        try:
            struct.pack_into(self.pk.e + code, self.out, off, v)
        except struct.error as e:
            raise XdlError(f"{path}: {v}: {e}")

    def link(self, x, off, v, path):
        pk = self.pk
        abs_base = self.base_off
        # a texture slot whose link didn't change keeps the base's (loader-overwritten) bytes
        keep = False
        if x["kind"] == "texture" and abs_base is not None:
            try:
                keep = self.dec.link(x, abs_base + off, path) == v
            except XdlError:
                keep = False
        self.clear(off, off + 4)
        if v is None:
            struct.pack_into(pk.e + "I", self.out, off, 0)
            return
        if not isinstance(v, str):
            raise XdlError(f"{path}: a link is a string or null")
        if v.startswith("uid://"):
            raise XdlError(f"{path}: uid:// links need the project's id table (not in this step)")
        if x["kind"] == "object":
            t, ttype = self.res.resolve(v)
            want = x.get("target")
            if want and ttype and want not in ("void",) and ttype not in ("void",) and want != ttype:
                raise XdlError(f"{path}: {v} is a {ttype}, the field wants a {want}")
            struct.pack_into(pk.e + "I", self.out, off, t)
            self.rel["pointers"][off] = t
            return
        arch, pname, kind, idx = self.pair.index(pk.release, v)
        if kind != x["kind"]:
            raise XdlError(f"{path}: {v} is not a {x['kind']}")
        if x["kind"] == "texture":
            if arch.lower() == pk.archive.lower() and pname == pk.name:
                sid = 0
            elif (arch, pname) in SHARE_ID:
                sid = SHARE_ID[(arch, pname)]
            else:
                raise XdlError(f"{path}: {v}: textures come from the own pack or a share pack")
            if not keep:
                struct.pack_into(pk.e + "I", self.out, off, 0)
            self.rel["texture"][off] = (sid << 16) | idx
            return
        # geometry: own pack only
        if arch.lower() != pk.archive.lower() or pname != pk.name:
            raise XdlError(f"{path}: {v}: geometry from another pack not supported yet")
        g = pk.geometries()
        struct.pack_into(pk.e + "I", self.out, off, g[idx])
        self.rel["ref"][off] = idx


# ------------------------------------------------------------------------------------------
# JSONC

_JSONC_COMMENT = re.compile(r'"(?:[^"\\]|\\.)*"|//[^\n]*|/\*.*?\*/|/\*|"', re.S)
_JSONC_COMMA = re.compile(r'"(?:[^"\\]|\\.)*"|,(?=\s*[\]}])')


def _jsonc_strip(m):
    t = m.group()
    if t[0] == '"':
        if len(t) == 1:
            raise XdlError(f"unterminated string at offset {m.start()}")
        return t
    if t == "/*":
        raise XdlError(f"unterminated comment at offset {m.start()}")
    return ""


def jsonc_loads(text):
    """JSON with // and /* */ comments and trailing commas (a comma before } or ], outside
    strings). Raises XdlError for an unterminated string or comment, ValueError for bad JSON."""
    # fast path: the files the tools write are JSON after leading // comment lines
    i = 0
    while True:
        j = len(text) - len(text[i:].lstrip())
        if not text.startswith("//", j):
            break
        k = text.find("\n", j)
        i = len(text) if k < 0 else k + 1
    try:
        return json.loads(text[i:])
    except ValueError:
        pass
    s = _JSONC_COMMENT.sub(_jsonc_strip, text)
    s = _JSONC_COMMA.sub(lambda m: m.group() if m.group()[0] == '"' else "", s)
    return json.loads(s)


def dump_value(v, ind, comments=None):
    pad = "  " * ind
    if isinstance(v, Num):
        return v.text
    if isinstance(v, dict):
        if not v:
            return "{}"
        items = list(v.items())
        lines = []
        for k, (key, x) in enumerate(items):
            com = (comments or {}).get(key)
            sep = "," if k < len(items) - 1 else ""
            lines.append(f"{pad}  {json.dumps(key)}: {dump_value(x, ind + 1)}{sep}"
                         + (f"  // {com}" if com else ""))
        return "{\n" + "\n".join(lines) + f"\n{pad}}}"
    if isinstance(v, list):
        if all(not isinstance(x, (dict, list)) for x in v):
            one = "[" + ", ".join(dump_value(x, ind + 1) for x in v) + "]"
            if len(one) <= 100:
                return one
        return "[\n" + ",\n".join(f"{pad}  {dump_value(x, ind + 1)}" for x in v) + f"\n{pad}]"
    return json.dumps(v)


def to_jsonc(doc, sch, header):
    comments = {}
    for key, p in fields(sch):
        if "x-units" in p:
            comments[key] = p["x-units"]
    return "".join(f"// {h}\n" for h in header) + dump_value(doc, 0, comments) + "\n"


# ------------------------------------------------------------------------------------------
# validation

def validator(sch):
    try:
        import jsonschema
    except ImportError:
        return MiniValidator(sch), "builtin"
    return jsonschema.Draft202012Validator(sch), "jsonschema"


class MiniValidator:
    """The keywords the generated schemas use, for hosts without the jsonschema package."""

    def __init__(self, sch):
        self.root = sch

    def iter_errors(self, inst):
        errs = []
        self.check(self.root, inst, "", errs)
        return errs

    def check(self, s, v, path, errs):
        if s is True:
            return
        if "$ref" in s:
            s = self.root["$defs"][s["$ref"].rsplit("/", 1)[1]]
        t = s.get("type")
        if t:
            ts = t if isinstance(t, list) else [t]
            ok = any(_is(v, x) for x in ts)
            if not ok:
                errs.append(f"{path}: not {t}")
                return
        if "const" in s and v != s["const"]:
            errs.append(f"{path}: must be {s['const']}")
        if isinstance(v, str) and "pattern" in s and not re.search(s["pattern"], v):
            errs.append(f"{path}: {v!r} doesn't match the link grammar")
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if "minimum" in s and v < s["minimum"] or "maximum" in s and v > s["maximum"]:
                errs.append(f"{path}: {v} out of range")
        if isinstance(v, list):
            if len(v) < s.get("minItems", 0) or len(v) > s.get("maxItems", 1 << 30):
                errs.append(f"{path}: {len(v)} items")
            for i, x in enumerate(v):
                self.check(s.get("items", True), x, f"{path}/{i}", errs)
        if isinstance(v, dict):
            props = s.get("properties", {})
            for k, x in v.items():
                if k in props:
                    self.check(props[k], x, f"{path}/{k}", errs)
                elif s.get("additionalProperties") is False:
                    errs.append(f"{path}: unknown field {k}")
            req = list(s.get("required", []))
            if "if" in s:
                cond = all(k in v for k in s["if"].get("required", []))
                branch = s.get("then") if cond else s.get("else")
                if isinstance(branch, dict):
                    req += branch.get("required", [])
            for k in req:
                if k not in v:
                    errs.append(f"{path}: missing {k}")


def _is(v, t):
    return {"string": isinstance(v, str), "null": v is None,
            "integer": isinstance(v, int) and not isinstance(v, bool),
            "number": isinstance(v, (int, float)) and not isinstance(v, bool),
            "array": isinstance(v, list), "object": isinstance(v, dict),
            "boolean": isinstance(v, bool)}[t]


# ------------------------------------------------------------------------------------------
# commands

def object_id(pack, struct_name, root, path):
    return f"base://{pack.archive}/{pack.name}/{struct_name}/{root}" + "".join(f"/{p}" for p in path)


def locate(S, pack, root_struct, root, path):
    """(offset, struct name) of the root object or of the object a field path points to."""
    oid = object_id(pack, root_struct, root, path)
    if not path:
        return pack.roots(ROOTS[root_struct])[root], root_struct, oid
    off, t = Resolver(S, pack, root_struct).resolve(oid)
    return off, t, oid


def extract(S, pack, pairing, root_struct, root=0, path=(), schema_ref=None):
    off, sname, oid = locate(S, pack, root_struct, root, list(path))
    sch = S.struct(sname)
    if sch is None:
        raise XdlError(f"no schema for {sname}")
    doc = {}
    if schema_ref:
        doc["$schema"] = schema_ref
    doc["format_version"] = 1
    doc["extends"] = oid
    doc.update(Decoder(S, pack, pairing).struct(sch, off, oid))
    return doc, sch, off, sname, oid


def build(S, pack, pairing, doc, sname, base_off, base=None):
    ext = doc.get("extends")
    enc = Encoder(S, pack, pairing, sname, base_off if ext else None, base)
    return enc.encode(doc, ext)


def scrubbed_base(S, pack, sname, off):
    """The base object with every field the JSON carries zeroed and its relocations dropped,
    so a round trip proves those bytes come from the JSON. Kept: hidden fields and gaps, and
    the raw bytes of texture slots (loader junk the writer takes from the base by design)."""
    sch = S.struct(sname)
    size = int(sch["x-size"], 16)
    data = bytearray(pack.blob[off:off + size])
    rel = pack.relocs_in(off, off + size)
    for key, p in fields(sch):
        lo = int(p["x-offset"], 16)
        hi = lo + S.size_of(p["x-c-type"])
        tex = "x-link" in leaf(p, len(parse_ctype(p["x-c-type"])[1])) and \
            leaf(p, len(parse_ctype(p["x-c-type"])[1]))["x-link"]["kind"] == "texture"
        if not tex:
            data[lo:hi] = bytes(hi - lo)
        for k in rel:
            for o in [o for o in rel[k] if lo <= o < hi]:
                del rel[k][o]
    return bytes(data), rel


def survey_packs(root, rel, archive):
    d = Path(root) / rel / archive
    if not d.exists():
        cands = [p for p in (Path(root) / rel).rglob("manifest.json") if p.parent.name.lower() == archive.lower()]
        if not cands:
            raise XdlError(f"no {archive} under {root}/{rel}")
        d = cands[0].parent
    man = json.loads((d / "manifest.json").read_text())
    return [d / e["path"] for e in man["entries"]]


def cmd_roundtrip(a):
    S = Schemas()
    pairing = Pairing(find_up("docs/xdl-format/pairing"))
    out = Path(a.out) if a.out else None
    total, fails = 0, 0
    for rel in a.rel.split(","):
        for pdir in survey_packs(a.survey, rel, a.archive):
            pack = Pack(pdir)
            if pack.entry["type"] != ROOTS[a.struct]:
                continue
            targets = [()] + ([(f,) for f in PARTS] if a.parts else [])
            for path in targets:
                total += 1
                label = f"{rel} {pack.name}" + (f" {path[0]}" if path else "")
                try:
                    ok, msg = roundtrip_one(S, pack, pairing, a.struct, list(path), out, rel)
                except XdlError as e:
                    ok, msg = False, str(e)
                if not ok:
                    fails += 1
                    print(f"FAIL {label}: {msg}")
                elif a.verbose:
                    print(f"ok   {label}: {msg}")
    print(f"{total - fails}/{total} byte-identical round trips (bytes and relocations)")
    return 1 if fails else 0


def roundtrip_one(S, pack, pairing, root_struct, path, out, rel):
    sname_guess = PARTS[path[0]] if path else root_struct
    fname = (path[0] if path else "car") + ".jsonc"
    dest = out / rel / pack.name / fname if out else None
    schema_ref = None
    if dest:
        dest.parent.mkdir(parents=True, exist_ok=True)
        schema_ref = os.path.relpath(S.path(sname_guess), dest.parent.resolve())
    doc, sch, off, sname, oid = extract(S, pack, pairing, root_struct, 0, path, schema_ref)
    text = to_jsonc(doc, sch, [f"{sname} {oid} ({pack.release}), from tools/xdl_struct.py"])
    if dest:
        dest.write_text(text)
    # back from the text, as an editor would leave it
    doc2 = jsonc_loads(text)
    v, kind = validator(S.top[sname][0])
    errs = [str(getattr(e, "message", e)) for e in v.iter_errors(doc2)]
    if errs:
        return False, f"schema ({kind}): " + "; ".join(errs[:3])
    base = scrubbed_base(S, pack, sname, off)
    data, rel_out = build(S, pack, pairing, doc2, sname, off, base)
    size = len(data)
    want = pack.blob[off:off + size]
    want_rel = pack.relocs_in(off, off + size)
    if data != want:
        diff = [i for i in range(size) if data[i] != want[i]]
        return False, f"bytes differ at {', '.join(hex(i) for i in diff[:8])}"
    for k in want_rel:
        if want_rel[k] != rel_out[k]:
            return False, f"{k} relocations differ: {sorted(set(want_rel[k].items()) ^ set(rel_out[k].items()))[:4]}"
    nrel = sum(len(x) for x in rel_out.values())
    return True, f"0x{size:X} bytes, {nrel} relocations, {len(fields(sch))} fields ({kind})"


def observe_values(v, acc):
    if isinstance(v, list):
        for x in v:
            observe_values(x, acc)
    elif isinstance(v, dict):
        return
    elif isinstance(v, (int, float)) and not isinstance(v, bool):
        acc.append(v)


def cmd_observe(a):
    S = Schemas()
    pairing = Pairing(find_up("docs/xdl-format/pairing"))
    vals = {}
    rels = a.rel.split(",")
    for rel in rels:
        for pdir in survey_packs(a.survey, rel, a.archive):
            pack = Pack(pdir)
            if pack.entry["type"] != ROOTS[a.struct]:
                continue
            for path in [()] + [(f,) for f in PARTS]:
                doc, sch, off, sname, oid = extract(S, pack, pairing, a.struct, 0, path)
                for key, p in fields(sch):
                    if "x-link" in p or "$ref" in p or "x-link" in p.get("items", {}):
                        continue
                    acc = vals.setdefault(sname, {}).setdefault(key, {})
                    lst = []
                    observe_values(doc[key], lst)
                    acc.setdefault("v", []).extend(lst)
                    acc.setdefault("objects", set()).add((rel, pack.name))
    out = json.loads(OBSERVED.read_text()) if OBSERVED.exists() else {}
    for sname, ks in vals.items():
        o = out[sname] = {}
        for key, acc in ks.items():
            vs = acc["v"]
            if not vs:
                continue
            fmt = lambda x: x if isinstance(x, int) else float(f32_text(x))  # noqa: E731
            o[key] = {"min": fmt(min(vs)), "max": fmt(max(vs)), "distinct": len(set(vs)),
                      "objects": len({p for _, p in acc["objects"]}),
                      "releases": sorted({r for r, _ in acc["objects"]}),
                      "archive": a.archive}
    OBSERVED.write_text(json.dumps(out, indent=1, sort_keys=True) + "\n")
    print(f"{OBSERVED.relative_to(ROOT)}: {sum(len(v) for v in out.values())} fields")
    return 0


def cmd_extract(a):
    S = Schemas()
    pairing = Pairing(find_up("docs/xdl-format/pairing"))
    pack = Pack(a.pack)
    path = [x for x in (a.path or "").split("/") if x]
    doc, sch, off, sname, oid = extract(S, pack, pairing, a.struct, a.root, path,
                                        str(S.path(PARTS.get(path[0], a.struct) if path else a.struct)))
    text = to_jsonc(doc, sch, [f"{sname} {oid} ({pack.release}), from tools/xdl_struct.py"])
    if a.o:
        Path(a.o).write_text(text)
    else:
        sys.stdout.write(text)
    return 0


def cmd_build(a):
    S = Schemas()
    pairing = Pairing(find_up("docs/xdl-format/pairing"))
    doc = jsonc_loads(Path(a.file).read_text())
    pack = Pack(a.pack)
    ext = doc.get("extends")
    if not ext:
        raise XdlError("build needs an extends object for now (the base bytes)")
    m = BASE_RE.match(ext)
    _, _, kind, idx, rest = m.groups()
    path = [x for x in rest.split("/") if x]
    off, sname, _ = locate(S, pack, kind, int(idx), path)
    v, vk = validator(S.top[sname][0])
    errs = list(v.iter_errors(doc))
    for e in errs:
        print(f"schema: {getattr(e, 'message', e)}", file=sys.stderr)
    if errs:
        return 1
    data, rel = build(S, pack, pairing, doc, sname, off)
    if a.o:
        Path(a.o).write_bytes(data)
        Path(a.o + ".relocs.json").write_text(json.dumps(rel, indent=1) + "\n")
    same = data == pack.blob[off:off + len(data)]
    print(f"{sname}: 0x{len(data):X} bytes, {sum(len(x) for x in rel.values())} relocations; "
          f"{'equal to' if same else 'differs from'} the base")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract")
    p.add_argument("pack")
    p.add_argument("--struct", default="CarAInfo")
    p.add_argument("--root", type=int, default=0)
    p.add_argument("--path", help="field path to a part (engine, hud ...)")
    p.add_argument("-o")
    p = sub.add_parser("build")
    p.add_argument("file")
    p.add_argument("pack")
    p.add_argument("-o")
    for name in ("roundtrip", "observe"):
        p = sub.add_parser(name)
        p.add_argument("survey")
        p.add_argument("--rel", default="gc,pc")
        p.add_argument("--archive", default="HW_Cars")
        p.add_argument("--struct", default="CarAInfo")
        if name == "roundtrip":
            p.add_argument("--parts", action="store_true", help="also CarAInfo's tuning parts")
            p.add_argument("--out", help="write the .jsonc files here")
            p.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    try:
        return {"extract": cmd_extract, "build": cmd_build, "roundtrip": cmd_roundtrip,
                "observe": cmd_observe}[a.cmd](a)
    except XdlError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
