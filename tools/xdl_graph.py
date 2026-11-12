#!/usr/bin/env python3
"""Whole packs <-> .xdl source projects: the object graph as JSONC, geometry as glTF (step 3c).

    xdl_graph.py extract PACK_DIR PROJECT_PACK_DIR      one pack of an `hwvx-x extract` tree
    xdl_graph.py build PROJECT_PACK_DIR OUT_PACK_DIR [--check PACK_DIR]
    xdl_graph.py roundtrip SURVEY_ROOT [--rel gc,pc] [--archive HW_Cars] [--out build-gltf] [--keep]
    xdl_graph.py archive-build PROJECT_ARCHIVE_DIR OUT_ARCHIVE_DIR      every pack + manifest.json

A pack is a blob of objects (data.bin) with relocation tables. Extraction cuts the blob into
nodes, gives every node a structural id, writes each node as a file (or inside its parent's
file), and replaces every offset by a link:

* **Nodes.** The blob is cut at every pointer and local-ref target, at the header's tables
  (textures, shaders, triggers, palettes) and at every geometry. A geometry (xdl_gltf.py) and
  an object of a schematised struct (schemas/, xdl_struct.py) claim their whole extent, so a
  pointer into them is an interior link, not a cut. A node runs to the next cut.
* **Ids** (README §10.2, schemas.md §5): `base://<archive>/<pack>/<path>`. Fixed ids first:
  `geometry/N`, `texture/N` (pairing.md: the GameCube's index), `shader/N`, `trigger/N`,
  `palette/0`, the root table's entries (`CarAInfo/0`, `ItemRecord/0`, `IMHeader/3`,
  `SoundSeq/2`), `World/0` / `header/0` for the node at offset 0. Every other node is named by
  its **canonical path**: breadth-first from those roots, in that order, slots in offset order;
  the first path that reaches a node names it. A step is the field path where a schema covers
  the slot (`CarAInfo/0/engine`, `.../aiScripts/2`), else `p<hex offset>` (`.../engine/p48`);
  repeats of one step are counted (`p0_r12` = twelve times `p0`, linked lists). A node no root
  reaches is `orphan/K` (K in blob order). A link into a node is `<id>/o<hex offset>`, or a
  structural name inside a geometry (`geometry/3/level/0/group/2`).
* **Files.** Schematised objects: the xdl_struct JSONC (`$schema`, `extends` its own base id),
  with the bytes the JSON doesn't carry (hidden fields, padding, links in them) in
  `<name>.base.jsonc`. Geometry: `geometry/N.gltf` + `.bin`, with `geometry/N.xgeo.jsonc` (the
  original encoding: bytes in `.orig.bin`, links, the glTF's content hash). Everything else is a
  raw record (`"bytes"` hex or a `.bin` beside it, pointer slots zeroed, `links`, `textures`,
  `sounds`, `refs`, `shaders` by slot offset), typed by the Port_Swap walkers' label where one
  is known. A node gets a file of its own when it is schematised, a geometry, a root, a
  texture/shader/trigger, labelled with a component kind (comp_survey.KINDS), an orphan, or
  large; any other node is written inside its canonical parent's file (`children`).
* **pack.jsonc** holds pack.json (header offsets as links), the manifest entry, the layout (node
  order and alignment) and the sound sections (kept as files for the media step).
  **pack.order.json** keeps the relocation tables' original order (the exporter's traversal),
  as (layout index, offset) pairs; slots it doesn't list go after it, sorted.

Building reads the files back, encodes every node, lays the nodes out in layout order (each at
its alignment; equal sizes give the original offsets) and resolves the links. An unchanged
project builds the original pack files byte for byte.
"""

import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_gltf as XG  # noqa: E402
import xdl_struct as XS  # noqa: E402

FORMAT = 1
ROOT_KIND = {"car": "CarAInfo", "item": "ItemRecord", "interface": "IMHeader", "audio": "SoundSeq"}
# walker labels (docs/beta-cars/research/scripts/walk.py) that are a schematised struct
LABEL_SCHEMA = {
    "CarAInfo": "CarAInfo", "CarAEngine": "CarAEngine", "CarAHud": "CarAHud",
    "CarAStuntInfo": "CarAStuntInfo", "CarAAccel": "CarAAccel", "CarACollider": "CarACollider",
    "CreatureInfo": "CreatureInfo", "DestructibleInfo": "DestructibleInfo",
    "WorldGameData": "WorldGameData", "WeaponInfo": "WeaponInfo", "ItemInfo": "ItemRecord",
    "Projectile": "ProjectileInfo",
}
INLINE_MAX = 256           # raw nodes up to this size keep their bytes in the JSONC
OWN_FILE_MIN = 4096        # raw nodes this large get a file of their own
SECTIONS = ("pointers", "texture", "sound", "ref", "shader")
SECTION_FILE = {"pointers": "pointers.bin", "texture": "texture_relocs.bin", "sound": "sound_relocs.bin",
                "ref": "ref_relocs.bin", "shader": "shader_relocs.bin"}
SECTION_ABOUT = {      # pack.json's informational section notes (src/pack.rs)
    "pointers.bin": "offsets of pointer slots in data",
    "texture_relocs.bin": "data offset + texture index (shared pack << 16)",
    "sound_relocs.bin": "data offset + sound index (shared pack << 16)",
    "ref_relocs.bin": "pointer slot + target (shared pack << 16 | index)",
    "shader_relocs.bin": "data offset + shader index",
}
SECTION_ORDER = ("sound_pointers.bin", "sound_data.bin", "texture_relocs.bin", "shader_relocs.bin",
                 "sound_relocs.bin", "data.bin", "ref_relocs.bin", "pointers.bin")
HEADER_OFFSETS = (("texture_data_offset", "num_textures"), ("palette_offset", "num_palettes"),
                  ("shader_offset", "num_shaders"), ("trigger_offset", "num_triggers"))


class GraphError(Exception):
    pass


# ------------------------------------------------------------------------------------------
# small helpers

def hx(n):
    return f"0x{n:X}"


def hex_lines(b):
    """Bytes as lines of 16, words separated: readable and diffable."""
    out = []
    for i in range(0, len(b), 16):
        c = b[i:i + 16].hex()
        out.append(" ".join(c[j:j + 8] for j in range(0, len(c), 8)))
    return out


def unhex(v):
    if isinstance(v, list):
        v = "".join(v)
    return bytes.fromhex(v.replace(" ", ""))


def align_of(off):
    if off == 0:
        return 32
    a = 1
    while a < 32 and off % (a * 2) == 0:
        a *= 2
    return a


def id_path(nid):
    """base://A/P/x/y -> 'x/y' (the part below the pack)."""
    m = re.match(r"^base://[^/]+/[^/]+/(.*)$", nid)
    return m.group(1)


def file_stem(rest):
    s = rest.replace(":", "_")
    if len(s) > 160:
        s = "deep/" + hashlib.blake2b(rest.encode(), digest_size=8).hexdigest()
    return s


def add_step(path, seg):
    """Append a step, counting repeats of the same p-step (linked lists)."""
    if seg.startswith("p") and path:
        last = path[-1]
        m = re.match(r"^(p[0-9a-f]+)(?:_r(\d+))?$", last)
        if m and m.group(1) == seg:
            n = int(m.group(2) or 1) + 1
            return path[:-1] + [f"{seg}_r{n}"]
    return path + [seg]


def find_up(rel):
    return XS.find_up(rel)


_PAIRING = None
_SPELLING = None


def pairing():
    global _PAIRING
    if _PAIRING is None:
        _PAIRING = XS.Pairing(find_up("docs/xdl-format/pairing"))
    return _PAIRING


def archive_spelling(name):
    """The archive's name as the GameCube spells it (pairing.md §1)."""
    global _SPELLING
    if _SPELLING is None:
        _SPELLING = {}
        d = find_up("docs/xdl-format/pairing")
        if d:
            for p in d.glob("*.jsonl"):
                _SPELLING[p.stem.lower()] = p.stem
    return _SPELLING.get(name.lower(), name)


def pack_name(entry):
    n = f"{entry['type']}_{entry['id']}"
    if entry["type"] == "geometry" and entry.get("sub_id"):
        n += f"_q{entry['sub_id'] & 0xFFFF}"
    return n


_WALK = None


def walkers():
    """comp_survey.label_pack (the Port_Swap walkers in Python), or None if the docs are absent."""
    global _WALK
    if _WALK is None:
        d = find_up("docs/xdl-format/scripts")
        if d is None or not (d / "comp_survey.py").exists():
            _WALK = False
        else:
            sys.path.insert(0, str(d))
            import comp_survey  # noqa: E402
            _WALK = comp_survey
    return _WALK or None


# ------------------------------------------------------------------------------------------
# the source pack

class Source:
    """One pack of an `hwvx-x extract` tree, every table kept in its file order."""

    def __init__(self, folder):
        self.dir = Path(folder)
        self.man = json.loads((self.dir.parent / "manifest.json").read_text())
        self.release = self.man["platform"]
        self.e = ">" if self.release == "gc" else "<"
        ent = [x for x in self.man["entries"] if x["path"] == self.dir.name]
        if not ent:
            raise GraphError(f"{self.dir.name} not in {self.dir.parent}/manifest.json")
        self.entry = ent[0]
        self.archive = archive_spelling(self.dir.parent.name)
        self.name = pack_name(self.entry)
        self.packjson = json.loads((self.dir / "pack.json").read_text())
        self.header = self.packjson["header"]
        self.blob = (self.dir / "data.bin").read_bytes()
        self.tables = {"pointers": self._tab("pointers.bin", 4)}
        for k in ("texture", "sound", "ref", "shader"):
            self.tables[k] = self._tab(SECTION_FILE[k], 8)
        self.ptr = {o: struct.unpack_from(self.e + "I", self.blob, o)[0] for o in self.tables["pointers"]}
        self.rel = {k: dict(self.tables[k]) for k in ("texture", "sound", "ref", "shader")}

    def _tab(self, name, w):
        p = self.dir / name
        if not p.exists():
            return []
        b = p.read_bytes()
        if w == 4:
            return list(struct.unpack(self.e + f"{len(b) // 4}I", b))
        return [struct.unpack_from(self.e + "2I", b, i) for i in range(0, len(b), 8)]

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.blob, o)[0]

    def base(self, rest):
        return f"base://{self.archive}/{self.name}/{rest}"


class XsPack:
    """What xdl_struct's Decoder/Encoder need of a pack, over a Source."""

    def __init__(self, src):
        self.release, self.e = src.release, src.e
        self.archive, self.name = src.archive, src.name
        self.blob = src.blob
        self.ptrs = set(src.ptr)
        self.tex = src.rel["texture"]
        self.snd = src.rel["sound"]
        self.ref = src.rel["ref"]

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.blob, o)[0]

    def target(self, o):
        return self.u32(o) if o in self.ptrs else None


# ------------------------------------------------------------------------------------------
# links for relocation values

def shared_pack(src, sid):
    if sid == 0:
        return src.archive, src.name
    if sid in XS.SHARE:
        return XS.SHARE[sid]
    return f"pack{sid}", f"share_{sid}"


def asset_link(src, kind, value):
    """texture / sound reloc value -> base:// id (pairing table, or <release>:<index>)."""
    sid, idx = value >> 16, value & 0xFFFF
    arch, pname = shared_pack(src, sid)
    try:
        return pairing().id(src.release, arch, pname, kind, idx)
    except XS.XdlError:
        return f"base://{arch}/{pname}/{kind}/{src.release}:{idx}"


def asset_value(release, link, kind, own):
    """base:// id -> reloc value (inverse of asset_link). own = (archive, pack) of this pack."""
    m = XS.BASE_RE.match(link)
    if not m:
        raise GraphError(f"{link}: not a base:// {kind} id")
    arch, pname, k, idx, rest = m.groups()
    if k != kind:
        raise GraphError(f"{link} is not a {kind}")
    if arch.lower() == own[0].lower() and pname == own[1]:
        sid = 0
    elif (arch, pname) in XS.SHARE_ID:
        sid = XS.SHARE_ID[(arch, pname)]
    elif arch.startswith("pack") and pname.startswith("share_"):
        sid = int(pname[6:])
    else:
        raise GraphError(f"{link}: {kind}s come from the pack itself or a share pack")
    if idx.startswith(release + ":"):
        i = int(idx.split(":")[1])
    else:
        try:
            _, _, _, i = pairing().index(release, link)
        except XS.XdlError as e:
            raise GraphError(str(e))
    return (sid << 16) | i


def export_link(src, value):
    arch, pname = shared_pack(src, value >> 16)
    return f"base://{arch}/{pname}/export/{value & 0xFFFF}"


def export_value(link):
    m = XS.BASE_RE.match(link)
    arch, pname, k, idx, rest = m.groups()
    if k != "export":
        raise GraphError(f"{link} is not an export")
    if (arch, pname) in XS.SHARE_ID:
        sid = XS.SHARE_ID[(arch, pname)]
    elif pname.startswith("share_"):
        sid = int(pname[6:])
    else:
        raise GraphError(f"{link}: unknown shared pack")
    return (sid << 16) | int(idx)


# ------------------------------------------------------------------------------------------
# extraction

class Node:
    __slots__ = ("start", "end", "kind", "schema", "label", "geom", "gindex", "id", "parent",
                 "file", "anchors", "fixed")

    def __init__(self, start, end):
        self.start, self.end = start, end
        self.kind = "raw"           # raw | typed | geometry
        self.schema = None
        self.label = None
        self.geom = None
        self.gindex = None
        self.id = None
        self.parent = None
        self.file = None
        self.anchors = {}           # interior name -> offset in the node
        self.fixed = False


class Extractor:
    def __init__(self, src, out, S=None, log=None, force=False):
        self.src, self.out = src, Path(out)
        self.force = force          # replace a folder that isn't a previous extract
        self.S = S or XS.Schemas()
        self.log = log if log is not None else []
        self.blob = src.blob
        self.notes = collections.Counter()

    # -- typing --------------------------------------------------------------------------
    def labels(self):
        W = walkers()
        if W is None:
            return {}
        ptype = self.src.entry["type"]
        try:
            s = W.label_pack(self.src.dir, self.src.e, ptype)
        except Exception as e:  # the walkers are research code: never fatal
            self.log.append(f"walkers failed: {e}")
            return {}
        return dict(s.objs)

    def find_geometry(self):
        blob = XG.Blob(self.blob, set(self.src.ptr), self.src.release)
        self.gblob = blob
        geoms = []
        for i, off in enumerate(XG.scan(blob)):
            try:
                g = XG.decode(blob, off, self.src.rel["texture"])
            except (XG.GeomError, struct.error, IndexError, ValueError) as e:
                self.log.append(f"geometry {i} @0x{off:X} not decoded ({e}): kept as raw bytes")
                g = None
            geoms.append((off, g))
        return geoms

    def type_objects(self, claims, labels):
        """Schematised objects: root entries, walker labels, and links from typed fields."""
        S, src = self.S, self.src
        xp = XsPack(src)
        dec = XS.Decoder(S, xp, None)
        queue = []
        rk = ROOT_KIND.get(src.entry["type"])
        if rk and S.struct(rk) is not None:
            for o in self.root_entries():
                if o is not None:
                    queue.append((o, rk))
        for o, lab in sorted(labels.items()):
            if lab in LABEL_SCHEMA and S.struct(LABEL_SCHEMA[lab]) is not None:
                queue.append((o, LABEL_SCHEMA[lab]))
        typed = {}
        seen = set()
        while queue:
            off, sname = queue.pop(0)
            if (off, sname) in seen or off in typed:
                continue
            seen.add((off, sname))
            sch = S.struct(sname)
            size = int(sch["x-size"], 16)
            if off + size > len(self.blob) or any(a < off + size and off < b for a, b in claims):
                self.notes[f"{sname}: not typed (overlaps another object)"] += 1
                continue
            try:
                self._check_typed(dec, sch, off)
            except (XS.XdlError, struct.error) as e:
                self.notes[f"{sname}: not typed ({str(e).split(':')[-1].strip()[:60]})"] += 1
                continue
            typed[off] = sname
            claims.append((off, off + size))
            for rel_off, (path, target) in self.slot_fields(sname).items():
                t = src.ptr.get(off + rel_off)
                if t is not None and S.struct(target) is not None and target in S.top:
                    queue.append((t, target))
        return typed

    def _check_typed(self, dec, sch, off):
        # decode with the pairing-free check only: scalars must not sit on relocation slots
        for key, p in XS.fields(sch):
            self._check_value(dec, p, off + int(p["x-offset"], 16), p["x-c-type"])

    def _check_value(self, dec, p, off, ctype):
        base, dims = XS.parse_ctype(ctype)
        if dims:
            sub = base + "".join(f"[{d}]" for d in dims[1:])
            esz = self.S.size_of(sub)
            for i in range(dims[0]):
                self._check_value(dec, p["items"], off + i * esz, sub)
            return
        if "x-link" in p:
            kind = dec.slots.get(off)
            want = {"object": "pointer", "texture": "texture", "geometry": "ref"}[p["x-link"]["kind"]]
            if kind is None:
                if struct.unpack_from(self.src.e + "I", self.blob, off)[0] != 0:
                    raise XS.XdlError("link without relocation")
            elif kind != want:
                raise XS.XdlError(f"a {want} link on a {kind} slot")
            elif kind == "ref" and self.src.rel["ref"][off] >> 16:
                # a share pack's export: the geometry link grammar has no form for it yet
                raise XS.XdlError("geometry from a share pack")
            return
        if "$ref" in p:
            for key, q in XS.fields(self.S.ref(p)):
                self._check_value(dec, q, off + int(q["x-offset"], 16), q["x-c-type"])
            return
        size = XS.PRIM[base][1]
        for o in range(off - off % 4, off + size, 4):
            if o in dec.slots and o + 4 > off:
                raise XS.XdlError(f"scalar on a {dec.slots[o]} slot")

    _slot_cache = {}

    def slot_fields(self, sname):
        """{offset: (field path, link target type)} of a struct's link fields."""
        if sname in Extractor._slot_cache:
            return Extractor._slot_cache[sname]
        out = {}

        def walk(p, off, path, ctype):
            base, dims = XS.parse_ctype(ctype)
            if dims:
                sub = base + "".join(f"[{d}]" for d in dims[1:])
                esz = self.S.size_of(sub)
                for i in range(dims[0]):
                    walk(p["items"], off + i * esz, path + [str(i)], sub)
                return
            if "x-link" in p:
                out[off] = ("/".join(path), p["x-link"].get("target") or "")
                return
            if "$ref" in p:
                for key, q in XS.fields(self.S.ref(p)):
                    walk(q, off + int(q["x-offset"], 16), path + [key], q["x-c-type"])
        for key, p in XS.fields(self.S.struct(sname)):
            walk(p, int(p["x-offset"], 16), [key], p["x-c-type"])
        Extractor._slot_cache[sname] = out
        return out

    def root_entries(self):
        src = self.src
        if src.entry["type"] not in ROOT_KIND or len(self.blob) < 8:
            return []
        n = src.u32(0)
        tab = src.ptr.get(4)
        if tab is None:
            return []
        return [src.ptr.get(tab + 8 * i) for i in range(min(n, 0x1000))]

    # -- nodes ---------------------------------------------------------------------------
    def cut(self):
        src, n = self.src, len(self.blob)
        geoms = self.find_geometry()
        labels = self.labels()
        claims = []
        gclaims = {}
        for i, (off, g) in enumerate(geoms):
            hi = off + 0x40
            if g is not None:
                hi = max(b for a, b in g["owned"])
            gclaims[off] = (i, g, hi)
            claims.append((off, hi))
        typed = self.type_objects(claims, labels)
        cuts = {0, n}
        cuts |= {t for t in src.ptr.values() if t <= n}
        cuts |= {src.u32(o) for o, v in src.rel["ref"].items() if v >> 16 == 0}
        cuts |= set(gclaims) | set(typed)
        cuts |= {o for o in labels if o < n}
        self.tables_at = {}
        h = src.header
        for key, cnt in HEADER_OFFSETS:
            if h.get(cnt) and 0 < h.get(key, 0) < n:
                cuts.add(h[key])
        if h.get("num_textures") and 0 < h.get("texture_data_offset", 0) < n:
            stride = 0x20 if src.release == "gc" else 0x10
            for i in range(h["num_textures"]):
                o = h["texture_data_offset"] + stride * i
                if o < n:
                    cuts.add(o)
                    self.tables_at[o] = ("texture", i)
        if h.get("num_shaders") and 0 < h.get("shader_offset", 0) < n:
            for i in range(h["num_shaders"]):
                o = h["shader_offset"] + 0xC * i
                if o < n:
                    cuts.add(o)
                    self.tables_at.setdefault(o, ("shader", i))
        if h.get("num_triggers") and 0 < h.get("trigger_offset", 0) < n:
            for i in range(h["num_triggers"]):
                o = h["trigger_offset"] + 0x2C * i
                if o < n:
                    cuts.add(o)
                    self.tables_at.setdefault(o, ("trigger", i))
        if h.get("num_palettes") and 0 < h.get("palette_offset", 0) < n:
            self.tables_at.setdefault(h["palette_offset"], ("palette", 0))
        inside = []
        for a, b in claims:
            inside.append((a, b))
        inside.sort()
        cl = sorted(cuts)
        keep = []
        j = 0
        for c in cl:
            while j < len(inside) and inside[j][1] <= c:
                j += 1
            # a cut strictly inside a claim is dropped (claims don't overlap)
            if j < len(inside) and inside[j][0] < c < inside[j][1]:
                continue
            keep.append(c)
        nodes = []
        for a, b in zip(keep, keep[1:]):
            nd = Node(a, b)
            if a in gclaims:
                i, g, hi = gclaims[a]
                nd.kind, nd.geom, nd.gindex = "geometry", g, i
                if g is not None:
                    nd.anchors = {nm: o - a for o, nm in g["names"].items() if a <= o < b and nm}
            elif a in typed:
                nd.kind, nd.schema = "typed", typed[a]
            nd.label = labels.get(a)
            nodes.append(nd)
        if not nodes:
            nodes.append(Node(0, 0))
        self.nodes = nodes
        self.starts = [nd.start for nd in nodes]
        self.by_start = {nd.start: nd for nd in nodes}
        self.typed = typed
        self.ngeom = len(geoms)

    def node_at(self, off):
        import bisect
        i = bisect.bisect_right(self.starts, off) - 1
        if i < 0:
            return None
        nd = self.nodes[i]
        if off >= nd.end and not (off == nd.end == len(self.blob)):
            return None
        return nd

    # -- ids -----------------------------------------------------------------------------
    def name(self):
        src = self.src
        pr = pairing()
        fixed = []
        for nd in self.nodes:
            if nd.kind == "geometry":
                try:
                    nd.id = pr.id(src.release, src.archive, src.name, "geometry", nd.gindex)
                except XS.XdlError:
                    nd.id = src.base(f"geometry/{src.release}:{nd.gindex}")
                fixed.append(nd)
            elif nd.start in self.tables_at:
                kind, i = self.tables_at[nd.start]
                if kind == "texture":
                    nd.id = asset_link(src, "texture", i)
                else:
                    nd.id = src.base(f"{kind}/{i}")
                fixed.append(nd)
        roots = []
        rk = ROOT_KIND.get(src.entry["type"])
        if rk:
            for i, o in enumerate(self.root_entries()):
                nd = self.by_start.get(o) if o is not None else None
                if nd is not None and nd.id is None:
                    nd.id = src.base(f"{rk}/{i}")
                    roots.append(nd)
        n0 = self.nodes[0]
        if n0.id is None:
            n0.id = src.base("World/0" if src.entry["type"] == "world" else "header/0")
        for nd in roots + fixed + [n0]:
            nd.fixed = True
        self.children = collections.defaultdict(list)     # node -> [(seg, child)]
        for nd in self.nodes:
            for rel_off, seg, t in self.out_slots(nd):
                c = self.node_at(t)
                if c is not None and c.start == t and c is not nd:
                    self.children[nd.start].append((rel_off, seg, c))
        self.paths = {nd.start: id_path(nd.id).split("/") for nd in self.nodes if nd.id}
        for seed in roots + fixed + [n0]:
            self._bfs(seed)
        k = 0
        for nd in self.nodes:
            if nd.id is None:
                nd.id = src.base(f"orphan/{k}")
                nd.fixed = True
                self.paths[nd.start] = ["orphan", str(k)]
                k += 1
                self._bfs(nd)
        ids = collections.Counter(nd.id for nd in self.nodes)
        dup = [i for i, c in ids.items() if c > 1]
        if dup:
            raise GraphError(f"duplicate ids: {dup[:3]}")

    def _bfs(self, seed):
        q = collections.deque([seed])
        while q:
            nd = q.popleft()
            for rel_off, seg, c in sorted(self.children[nd.start], key=lambda x: x[0]):
                if c.id is not None:
                    continue
                path = add_step(self.paths[nd.start], seg)
                self.paths[c.start] = path
                c.id = self.src.base("/".join(path))
                c.parent = nd
                q.append(c)

    def out_slots(self, nd):
        """[(offset in node, step name, target)] of the node's pointer slots, offset order."""
        src = self.src
        fields = self.slot_fields(nd.schema) if nd.kind == "typed" else {}
        out = []
        for o in range(nd.start - nd.start % 4, nd.end, 4):
            if o < nd.start:
                continue
            t = src.ptr.get(o)
            if t is None:
                continue
            r = o - nd.start
            seg = fields[r][0] if r in fields else f"p{r:x}"
            out.append((r, seg, t))
        return out

    def link_to(self, t):
        nd = self.node_at(t)
        if nd is None:
            raise GraphError(f"pointer to 0x{t:X} outside every node")
        d = t - nd.start
        if d == 0:
            return nd.id
        for nm, o in nd.anchors.items():
            if o == d:
                return f"{nd.id}/{nm}"
        return f"{nd.id}/o{d:x}"

    # -- records -------------------------------------------------------------------------
    def raw_record(self, nd, skip=()):
        """bytes (pointer and local-ref slots zeroed) and links of a node; `skip`: slot offsets
        (node-relative) that the typed JSON carries."""
        src = self.src
        b = bytearray(self.blob[nd.start:nd.end])
        rec = {"size": nd.end - nd.start}
        links, tex, snd, refs, shd = {}, {}, {}, {}, {}
        for o in range(nd.start - nd.start % 4, nd.end, 4):
            if o < nd.start:
                continue
            r = o - nd.start
            if r in skip:
                continue
            if o in src.ptr:
                links[hx(r)] = self.link_to(src.ptr[o])
                b[r:r + 4] = bytes(4)
            if o in src.rel["ref"]:
                v = src.rel["ref"][o]
                if v >> 16 == 0:
                    links[hx(r)] = self.link_to(src.u32(o))
                    refs[hx(r)] = "local"
                    b[r:r + 4] = bytes(4)
                else:
                    refs[hx(r)] = export_link(src, v)
            if o in src.rel["texture"]:
                tex[hx(r)] = asset_link(src, "texture", src.rel["texture"][o])
            if o in src.rel["sound"]:
                snd[hx(r)] = asset_link(src, "sound", src.rel["sound"][o])
            if o in src.rel["shader"]:
                shd[hx(r)] = src.rel["shader"][o]
        for k, v in (("links", links), ("textures", tex), ("sounds", snd), ("refs", refs), ("shaders", shd)):
            if v:
                rec[k] = v
        return rec, bytes(b)

    def run(self):
        out = self.out
        if out.exists() and not out.is_dir():
            raise GraphError(f"{out}: not a folder")
        if (out.exists() and not self.force and not (out / "pack.jsonc").exists()
                and any(out.iterdir())):
            raise GraphError(f"{out}: not empty and not a previous extract (no pack.jsonc); "
                             "extracting replaces the folder (--force to do it anyway)")
        self.cut()
        self.name()
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        kinds = set((walkers().KINDS if walkers() else {}))
        for nd in self.nodes:
            own = (nd.fixed or nd.kind != "raw" or (nd.label in kinds)
                   or nd.end - nd.start >= OWN_FILE_MIN or nd.parent is None)
            if own:
                nd.file = file_stem(id_path(nd.id))
        return self._write()

    def _home(self, nd):
        p = nd
        while p.file is None:
            p = p.parent
        return p

    def _write(self):
        layout = []
        self.kids = collections.defaultdict(dict)        # home start -> {id: record}
        for nd in self.nodes:
            home = self._home(nd)
            layout.append({"id": nd.id, "file": home.file, "align": align_of(nd.start)})
            if home is nd and nd.label:
                layout[-1]["type"] = nd.label         # tools/xdl_components.py finds its candidates by this
            if home is not nd:
                rec, b = self.raw_record(nd)
                rec = {"id": nd.id, **({"type": nd.label} if nd.label else {}), **rec}
                self.put_bytes(rec, b, nd)
                self.kids[home.start][nd.id] = rec
        for nd in self.nodes:
            if nd.file is None:
                continue
            if nd.kind == "geometry":
                self.write_geometry(nd)
            elif nd.kind == "typed":
                self.write_typed(nd)
            else:
                rec, b = self.raw_record(nd)
                doc = {"format_version": FORMAT, "id": nd.id, **({"type": nd.label} if nd.label else {}), **rec}
                self.put_bytes(doc, b, nd)
                if self.kids.get(nd.start):
                    doc["children"] = self.kids[nd.start]
                self.write_doc(nd.file + ".jsonc", doc, ["raw object (no schema yet): bytes as stored, "
                                                         "pointer slots zeroed; links by slot offset"])
        self.write_pack(layout)
        return self.summary()

    def put_bytes(self, rec, b, nd):
        if len(b) <= INLINE_MAX:
            rec["bytes"] = hex_lines(b)
        else:
            stem = file_stem(id_path(nd.id))
            rec["bin"] = stem + ".bin"            # paths are relative to the pack folder
            p = self.out / (stem + ".bin")
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b)

    def write_doc(self, rel, doc, header=(), sch=None):
        p = self.out / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        if sch is not None:
            text = XS.to_jsonc(doc, sch, header)
        else:
            text = "".join(f"// {h}\n" for h in header) + XS.dump_value(doc, 0) + "\n"
        p.write_text(text)

    def write_typed(self, nd):
        src, S = self.src, self.S
        sname = nd.schema
        sch = S.struct(sname)
        xp = XsPack(src)
        dec = GraphDecoder(S, xp, self)
        stem = nd.file if nd.file else file_stem(id_path(nd.id))
        p = self.out / (stem + ".jsonc")
        p.parent.mkdir(parents=True, exist_ok=True)
        doc = {"$schema": os.path.relpath(S.path(sname).resolve(), p.parent.resolve()),
               "format_version": 1, "extends": nd.id}
        doc.update(dec.struct(sch, nd.start, nd.id))
        self.write_doc(stem + ".jsonc", doc, [f"{sname} {nd.id} ({src.release}), from tools/xdl_graph.py"], sch)
        # the base: what the JSON leaves out
        size = int(sch["x-size"], 16)
        rec, b = self.raw_record(nd, skip=self.json_slots(sname))
        b = bytearray(b)
        for key, q in XS.fields(sch):
            lo = int(q["x-offset"], 16)
            hi = lo + S.size_of(q["x-c-type"])
            lf = XS.leaf(q, len(XS.parse_ctype(q["x-c-type"])[1]))
            if not ("x-link" in lf and lf["x-link"]["kind"] == "texture"):
                b[lo:hi] = bytes(hi - lo)
        # texture relocs the JSON carries come from the JSON
        for r in self.json_slots(sname):
            rec.get("textures", {}).pop(hx(r), None)
        if "textures" in rec and not rec["textures"]:
            del rec["textures"]
        base = {"format_version": FORMAT, "id": nd.id, "schema": sname, "json_size": size}
        if nd.label:
            base["type"] = nd.label
        base.update(rec)
        self.put_bytes(base, bytes(b), nd)
        if self.kids.get(nd.start):
            base["children"] = self.kids[nd.start]
        if "bin" in base:
            base["bin"] = stem + ".base.bin"
            (self.out / (file_stem(id_path(nd.id)) + ".bin")).rename(self.out / (stem + ".base.bin"))
        self.write_doc(stem + ".base.jsonc", base, [f"base of {nd.id}: the bytes and links the "
                                                    f"{sname} JSON does not carry (hidden fields, padding, tail)"])

    def json_slots(self, sname):
        return set(self.slot_fields(sname))

    def write_geometry(self, nd):
        src = self.src
        stem = nd.file
        rec, b = self.raw_record(nd)
        side = {"format_version": FORMAT, "id": nd.id, "type": "Geometry", **rec}
        side["anchors"] = {k: hx(v) for k, v in sorted(nd.anchors.items(), key=lambda x: x[1])}
        if nd.geom is not None:
            g = nd.geom
            name = id_path(nd.id).replace("/", "_").replace(":", "_")
            gl, gb = XG.to_gltf(g, name, Path(stem).name + ".bin", lambda v: asset_link(src, "texture", v),
                                {"id": nd.id})
            p = self.out / (stem + ".gltf")
            p.parent.mkdir(parents=True, exist_ok=True)
            XG.write(gl, gb, p)
            side["gltf"] = stem + ".gltf"
            side["hash"] = XG.content_hash(gl, gb)
        side["bin"] = stem + ".orig.bin"
        side.pop("bytes", None)
        if self.kids.get(nd.start):
            side["children"] = self.kids[nd.start]
        (self.out / (stem + ".orig.bin")).write_bytes(b)
        self.write_doc(stem + ".xgeo.jsonc", side, [
            f"geometry {nd.id} ({src.release}): its original encoding. Built from these bytes while "
            "the glTF's content hash is unchanged; an edited glTF is encoded afresh (tools/xdl_gltf.py)"])

    def write_pack(self, layout):
        src = self.src
        pj = json.loads(json.dumps(src.packjson))
        h = pj["header"]
        for key, cnt in HEADER_OFFSETS:
            v = h.get(key)
            if v and h.get(cnt) and v in self.by_start:
                h[key] = self.by_start[v].id
        recsize = {s["file"]: (s["bytes"] // s["records"] if s["records"] else 1) for s in pj["sections"]}
        for s in pj["sections"]:
            s.pop("bytes", None)
            s.pop("records", None)
        idx = {nd.start: i for i, nd in enumerate(self.nodes)}

        def slot(o):
            nd = self.node_at(o)
            return [idx[nd.start], o - nd.start]
        order = {"pointers": [slot(o) for o in src.tables["pointers"]]}
        for k in ("texture", "sound", "ref", "shader"):
            order[k] = [slot(o) for o, v in src.tables[k]]
        sounds = {}
        sd = self.out / "sounds"
        for f in ("sound_data.bin", "sound_pointers.bin"):
            if (src.dir / f).exists():
                sd.mkdir(exist_ok=True)
                shutil.copyfile(src.dir / f, sd / f)
                sounds[f] = "sounds/" + f
        doc = {"format_version": FORMAT, "id": src.base("pack/0")[:-len("/pack/0")],
               "release": src.release, "archive": src.archive, "entry": src.entry,
               "pack": pj, "record_sizes": recsize, "files": sorted(f for f in os.listdir(src.dir)),
               "sounds": sounds, "nodes": len(self.nodes), "layout": layout}
        self.write_doc("pack.jsonc", doc, [f"{src.archive}/{src.name} ({src.release}): pack header, "
                                           "TOC entry and the order of its nodes (tools/xdl_graph.py)"])
        (self.out / "pack.order.json").write_text(json.dumps(order, separators=(",", ":")) + "\n")

    def summary(self):
        c = collections.Counter(nd.kind for nd in self.nodes)
        files = sum(1 for nd in self.nodes if nd.file)
        untyped = collections.Counter(nd.label or "(unlabelled)" for nd in self.nodes if nd.kind == "raw")
        return {"nodes": len(self.nodes), "typed": c["typed"], "geometry": c["geometry"],
                "raw": c["raw"], "files": files, "untyped": untyped, "notes": self.notes, "log": self.log}


class GraphDecoder(XS.Decoder):
    """xdl_struct's decoder with links named by the graph's canonical ids."""

    def __init__(self, S, xp, ex):
        super().__init__(S, xp, pairing())
        self.ex = ex

    def link(self, x, off, path):
        pk = self.pk
        kind = self.slots.get(off)
        raw = pk.u32(off)
        want = {"object": "pointer", "texture": "texture", "geometry": "ref"}[x["kind"]]
        if kind is None:
            if raw == 0:
                return None
            raise XS.XdlError(f"{path}: a {x['kind']} link, but 0x{off:X} holds 0x{raw:X} and no relocation")
        if kind != want:
            raise XS.XdlError(f"{path}: a {x['kind']} link, but 0x{off:X} is a {kind} slot")
        if kind == "pointer":
            return self.ex.link_to(raw)
        if kind == "texture":
            return asset_link(self.ex.src, "texture", pk.tex[off])
        v = pk.ref[off]
        if v >> 16:
            raise XS.XdlError(f"{path}: geometry from another pack (ref 0x{v:X}) not supported yet")
        return self.ex.link_to(raw)




# ------------------------------------------------------------------------------------------
# building

class GraphEncoder(XS.Encoder):
    """xdl_struct's encoder writing links symbolically (resolved when the pack is laid out)."""

    def __init__(self, S, xp, sname, base_bytes):
        super().__init__(S, xp, pairing(), sname, None, (base_bytes, {k: {} for k in ("pointers", "texture", "sound", "ref")}))
        self.links = {}

    def clear(self, lo, hi):
        super().clear(lo, hi)
        for o in [o for o in self.links if lo <= o + 3 and o < hi]:
            del self.links[o]

    def struct(self, sch, off, obj, path):
        # the base (.base.jsonc) has every JSON field zeroed: a field left out would build as 0.
        # Only top-level texture links keep their base bytes (write_typed).
        for key, p in XS.fields(sch):
            if key in obj:
                continue
            lf = XS.leaf(p, len(XS.parse_ctype(p["x-c-type"])[1]))
            if not (path == "" and lf.get("x-link", {}).get("kind") == "texture"):
                raise XS.XdlError(f"{path}/{key}: missing (a project file lists every field; "
                                  "its base has 0 there)")
        super().struct(sch, off, obj, path)

    def link(self, x, off, v, path):
        if v is not None and not isinstance(v, str):
            raise XS.XdlError(f"{path}: a link is a string or null")
        if v is not None and v.startswith("uid://"):
            raise XS.XdlError(f"{path}: uid:// links need the project's id table (not in this step)")
        self.clear(off, off + 4)
        if v is None:
            struct.pack_into(self.pk.e + "I", self.out, off, 0)
            return
        if x["kind"] == "texture":
            self.links[off] = ("texture", v)     # bytes: the base's (loader-overwritten junk)
            return
        struct.pack_into(self.pk.e + "I", self.out, off, 0)
        self.links[off] = (x["kind"], v)


class Built:
    """A node ready to place: bytes, and its slots as (offset, kind, value)."""

    def __init__(self, nid, data, align):
        self.id, self.data, self.align = nid, bytearray(data), align
        self.links = {}       # offset -> link (pointer, or local ref with refs[offset] = local)
        self.refs = {}        # offset -> "local" | export link
        self.tex, self.snd, self.shd = {}, {}, {}
        self.anchors = {}
        self.start = None
        self.geometry = False


def read_jsonc(p):
    return XS.jsonc_loads(Path(p).read_text())


def rec_slots(b, rec):
    def offs(d):
        return {int(k, 16): v for k, v in (d or {}).items()}
    b.links.update(offs(rec.get("links")))
    b.refs.update(offs(rec.get("refs")))
    b.tex.update(offs(rec.get("textures")))
    b.snd.update(offs(rec.get("sounds")))
    b.shd.update(offs(rec.get("shaders")))
    b.anchors.update({k: int(v, 16) for k, v in (rec.get("anchors") or {}).items()})


def rec_bytes(folder, rec):
    if "bin" in rec:
        return (folder / rec["bin"]).read_bytes()      # folder: the pack folder
    return unhex(rec.get("bytes", []))


class Builder:
    def __init__(self, proj, S=None, lib=None):
        self.dir = Path(proj)
        self.S = S or XS.Schemas()
        self.pack = read_jsonc(self.dir / "pack.jsonc")
        self.release = self.pack["release"]
        self.e = ">" if self.release == "gc" else "<"
        ent = self.pack["entry"]
        self.archive = self.pack["archive"]
        self.name = pack_name(ent)
        self.docs = {}
        self.edited = []
        self.lib = lib                # the component library (xdl_components.Library); found when needed
        self.instances = {}

    def doc(self, stem, ext=".jsonc"):
        key = stem + ext
        if key not in self.docs:
            self.docs[key] = read_jsonc(self.dir / key)
        return self.docs[key]

    def node(self, ent):
        nid, stem = ent["id"], ent["file"]
        folder = self.dir
        d = None
        if (self.dir / (stem + ".xgeo.jsonc")).exists():
            d = self.doc(stem, ".xgeo.jsonc")
            if d["id"] == nid:
                return self.geometry(ent, stem, folder)
        elif (self.dir / (stem + ".base.jsonc")).exists():
            d = self.doc(stem, ".base.jsonc")
            if d["id"] == nid:
                return self.typed(ent, stem, folder, d)
        if d is None:
            d = self.doc(stem)
        if "component" in d:                    # a component instance (tools/xdl_components.py)
            rec = self.instance(stem).get(nid)
            if rec is None:
                raise GraphError(f"{nid}: not a node of the component instance {stem}.jsonc")
            if "schema" in rec:
                return self.typed_rec(ent, stem, rec["schema"], rec["doc"], rec["base"])
        else:
            rec = d if d.get("id") == nid else d.get("children", {}).get(nid)
        if rec is None:
            raise GraphError(f"{nid}: not in {stem}.jsonc")
        b = Built(nid, rec_bytes(folder, rec), ent["align"])
        if len(b.data) != rec["size"]:
            raise GraphError(f"{nid}: {len(b.data)} bytes, size says {rec['size']}")
        rec_slots(b, rec)
        return b

    def instance(self, stem):
        """{node id: record} of the component instance in STEM.jsonc, expanded once."""
        if stem not in self.instances:
            import xdl_components as XC
            if self.lib is None:
                self.lib = XC.Library.locate(self.dir, self.pack.get("components"))
            try:
                self.instances[stem] = XC.expand(self.lib, self.doc(stem))
            except GraphError as e:
                raise GraphError(f"{stem}.jsonc: {e}")
        return self.instances[stem]

    def typed(self, ent, stem, folder, base):
        return self.typed_rec(ent, stem, base["schema"], self.doc(stem), base)

    def typed_rec(self, ent, stem, sname, doc, base):
        data = rec_bytes(self.dir, base)
        sch = self.S.struct(sname)
        v, vk = XS.validator(self.S.top[sname][0])
        errs = [str(getattr(e, "message", e)) for e in v.iter_errors(doc)]
        if errs:
            raise GraphError(f"{stem}.jsonc: schema ({vk}): " + "; ".join(errs[:3]))
        xp = type("P", (), {"e": self.e, "release": self.release, "archive": self.archive, "name": self.name,
                            "ptrs": set(), "tex": {}, "snd": {}, "ref": {}, "blob": b""})()
        enc = GraphEncoder(self.S, xp, sname, data)
        try:
            enc.encode(doc, doc.get("extends"))
        except XS.XdlError as e:
            raise GraphError(f"{stem}.jsonc: {e}")
        b = Built(ent["id"], enc.out, ent["align"])
        rec_slots(b, base)
        for off, (kind, v) in enc.links.items():
            if kind == "object":
                b.links[off] = v
            elif kind == "texture":
                b.tex[off] = v
            else:                               # geometry: a local ref
                b.links[off] = v
                b.refs[off] = "local"
        return b

    def geometry(self, ent, stem, folder):
        side = self.doc(stem, ".xgeo.jsonc")
        orig = (folder / side["bin"]).read_bytes()
        if "gltf" in side:
            gl, gb = XG.load(folder / side["gltf"])
            if XG.content_hash(gl, gb) != side["hash"]:
                return self.encode_geometry(ent, side, gl, gb)
        b = Built(ent["id"], orig, ent["align"])
        b.geometry = True
        rec_slots(b, side)
        return b

    def encode_geometry(self, ent, side, gl, gb):
        g = XG.from_gltf(gl, gb)
        data, ptrs, texslots, names = XG.encode(g, self.release)
        b = Built(ent["id"], data, ent["align"])
        b.geometry = True
        nid = ent["id"]
        for slot, t in ptrs.items():
            nm = [k for k, v in names.items() if v == t]
            b.links[slot] = f"{nid}/{nm[0]}" if nm and nm[0] else (nid if t == 0 else f"{nid}/o{t:x}")
        b.anchors = {k: v for k, v in names.items() if k}
        for slot, (li, k) in texslots.items():
            gr = g["levels"][li]["groups"][k]
            if gr.get("texture_link"):
                b.tex[slot] = gr["texture_link"]
            elif gr.get("texture_reloc") is not None:
                b.tex[slot] = gr["texture_reloc"]
        self.edited.append(nid)
        return b

    def build(self, out):
        out = Path(out)
        layout = list(self.pack["layout"])
        have = {ent["id"] for ent in layout}
        for stem in self.pack.get("append", []):      # files whose nodes the layout doesn't list yet
            d = self.doc(stem)
            if "component" in d:
                new = [(i, r.get("align", 4)) for i, r in self.instance(stem).items()]
            else:
                new = [(d["id"], d.get("align", 4))] + [(i, r.get("align", 4)) for i, r in d.get("children", {}).items()]
            for nid, al in new:
                if nid not in have:
                    have.add(nid)
                    layout.append({"id": nid, "file": stem, "align": al})
        built = [self.node(ent) for ent in layout]
        # place
        pos = 0
        index = {}
        for i, b in enumerate(built):
            if pos % b.align:
                pos += b.align - pos % b.align
            b.start = pos
            pos += len(b.data)
            index[b.id] = b
        blob = bytearray(pos)
        for b in built:
            blob[b.start:b.start + len(b.data)] = b.data
        geoms = [b for b in built if b.geometry]
        gidx = {b.id: i for i, b in enumerate(geoms)}
        own = (self.archive, self.name)

        def resolve(link):
            if link in index:
                return index[link].start, link
            parts = link.split("/")
            for k in range(len(parts) - 1, 2, -1):
                head = "/".join(parts[:k])
                if head in index:
                    nb = index[head]
                    rest = "/".join(parts[k:])
                    m = re.match(r"^o([0-9a-f]+)$", rest)
                    if m:
                        d = int(m.group(1), 16)
                        if d >= len(nb.data):
                            raise GraphError(f"{link}: offset 0x{d:X} past the end of {head} "
                                             f"(0x{len(nb.data):X} bytes)")
                        return nb.start + d, head
                    if rest in nb.anchors:
                        return nb.start + nb.anchors[rest], head
                    raise GraphError(f"{link}: no {rest} in {head}")
            raise GraphError(f"{link}: no such object in this pack")
        slots = {k: {} for k in SECTIONS}
        e = self.e
        edited = set(self.edited)
        stale = []                  # offset links into a re-encoded geometry: its layout changed
        for li, b in enumerate(built):
            for off, link in b.links.items():
                t, head = resolve(link)
                if head in edited and head != b.id and re.match(r"^o[0-9a-f]+$", link[len(head) + 1:]):
                    stale.append(f"{b.id}+0x{off:X} -> {link}")
                struct.pack_into(e + "I", blob, b.start + off, t)
                if b.refs.get(off) == "local":
                    if head not in gidx or t != index[head].start:
                        raise GraphError(f"{b.id}+0x{off:X}: a ref slot must point to a geometry")
                    slots["ref"][(li, off)] = gidx[head]
                else:
                    slots["pointers"][(li, off)] = None
            for off, v in b.refs.items():
                if v != "local":
                    slots["ref"][(li, off)] = export_value(v)
            for off, v in b.tex.items():
                slots["texture"][(li, off)] = v if isinstance(v, int) else asset_value(self.release, v, "texture", own)
            for off, v in b.snd.items():
                slots["sound"][(li, off)] = asset_value(self.release, v, "sound", own)
            for off, v in b.shd.items():
                slots["shader"][(li, off)] = v
        if stale:
            raise GraphError(f"{len(stale)} links into re-encoded geometry by offset (the offsets were the "
                             "original encoding's; only its structural names carry over): "
                             + "; ".join(stale[:8]) + (" ..." if len(stale) > 8 else ""))
        orderf = self.dir / "pack.order.json"
        order = json.loads(orderf.read_text()) if orderf.exists() else {}
        out.mkdir(parents=True, exist_ok=True)
        files = {}
        files["data.bin"] = bytes(blob)
        for k in SECTIONS:
            have = slots[k]
            seq = [tuple(x) for x in order.get(k, []) if tuple(x) in have]
            seen = set(seq)
            seq += sorted(x for x in have if x not in seen)
            if k == "pointers":
                raw = b"".join(struct.pack(e + "I", built[li].start + off) for li, off in seq)
            else:
                raw = b"".join(struct.pack(e + "2I", built[li].start + off, have[(li, off)]) for li, off in seq)
            files[SECTION_FILE[k]] = raw
        for f, rel in self.pack.get("sounds", {}).items():
            files[f] = (self.dir / rel).read_bytes()
        # pack.json
        pj = json.loads(json.dumps(self.pack["pack"]))
        h = pj["header"]
        for key, cnt in HEADER_OFFSETS:
            v = h.get(key)
            if isinstance(v, str):
                h[key] = resolve(v)[0]
        present = set(self.pack.get("files") or [])
        # a table the source didn't have (an edit added its first entry): file and section
        for f in SECTION_FILE.values():
            if f in present or not files.get(f):
                continue
            present.add(f)
            if not any(s["file"] == f for s in pj["sections"]):
                rank = SECTION_ORDER.index(f)
                at = sum(1 for s in pj["sections"] if s["file"] in SECTION_ORDER
                         and SECTION_ORDER.index(s["file"]) < rank)
                pj["sections"].insert(at, {"file": f, "about": SECTION_ABOUT[f]})
        for s in pj["sections"]:
            data = files.get(s["file"], b"")
            rs = self.pack["record_sizes"].get(s["file"], 4 if s["file"] == "pointers.bin" else
                                               8 if s["file"] in SECTION_ABOUT else 1)
            byts, recs = len(data), len(data) // rs
            s_new = {"file": s["file"], "bytes": byts, "records": recs}
            s_new.update({k: v for k, v in s.items() if k not in ("file",)})
            s.clear()
            s.update(s_new)
        files["pack.json"] = (json.dumps(pj, indent=2) + "\n").encode()
        for f, data in files.items():
            if f in present or f == "data.bin" or f == "pack.json":
                (out / f).write_bytes(data)
        return files


# ------------------------------------------------------------------------------------------
# commands

def compare(a, b):
    """[(file, what)] of the differences between two pack folders."""
    a, b = Path(a), Path(b)
    out = []
    fa = sorted(p.name for p in a.iterdir() if p.is_file())
    fb = sorted(p.name for p in b.iterdir() if p.is_file())
    if fa != fb:
        out.append(("files", f"{fa} != {fb}"))
    for f in fa:
        if (b / f).exists():
            x, y = (a / f).read_bytes(), (b / f).read_bytes()
            if x != y:
                i = next((k for k in range(min(len(x), len(y))) if x[k] != y[k]), min(len(x), len(y)))
                out.append((f, f"differs at 0x{i:X} ({len(x)} / {len(y)} bytes)"))
    return out


def cmd_extract(a):
    src = Source(a.pack)
    s = Extractor(src, a.out, force=a.force).run()
    print(f"{src.archive}/{src.name}: {s['nodes']} nodes ({s['typed']} typed, {s['geometry']} geometry, "
          f"{s['raw']} raw) in {s['files']} files")
    for k, v in s["notes"].items():
        print(f"  note: {k} x{v}")
    for m in s["log"]:
        print(f"  {m}")
    return 0


def cmd_build(a):
    bd = Builder(a.project)
    bd.build(a.out)
    if bd.edited:
        print(f"re-encoded (glTF edited): {', '.join(bd.edited)}")
    if a.check:
        d = compare(a.check, a.out)
        for f, w in d:
            print(f"DIFF {f}: {w}")
        print("identical" if not d else f"{len(d)} differences")
        return 1 if d else 0
    return 0


def write_archive(arch_dir, proj_dir):
    """archive.jsonc: the manifest's own fields and its packs in TOC order."""
    man = json.loads((Path(arch_dir) / "manifest.json").read_text())
    doc = {k: v for k, v in man.items() if k != "entries"}
    doc = {"format_version": FORMAT, "archive": archive_spelling(Path(arch_dir).name), **doc,
           "packs": [pack_name(e) for e in man["entries"]]}
    Path(proj_dir).mkdir(parents=True, exist_ok=True)
    text = "// archive: manifest fields and pack order (tools/xdl_graph.py)\n" + XS.dump_value(doc, 0) + "\n"
    (Path(proj_dir) / "archive.jsonc").write_text(text)


def build_manifest(proj_dir, out_dir):
    """manifest.json of a built archive, from archive.jsonc and each pack.jsonc's entry."""
    doc = read_jsonc(Path(proj_dir) / "archive.jsonc")
    man = {k: v for k, v in doc.items() if k not in ("format_version", "archive", "packs")}
    man["entries"] = [read_jsonc(Path(proj_dir) / p / "pack.jsonc")["entry"] for p in doc["packs"]]
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    (Path(out_dir) / "manifest.json").write_text(json.dumps(man, indent=2) + "\n")
    return man


def cmd_extract_archive(a):
    arch = Path(a.archive)
    man = json.loads((arch / "manifest.json").read_text())
    write_archive(arch, a.out)
    S = XS.Schemas()
    for e in man["entries"]:
        s = Extractor(Source(arch / e["path"]), Path(a.out) / pack_name(e), S, force=a.force).run()
        print(f"{pack_name(e)}: {s['nodes']} nodes in {s['files']} files")
    return 0


def cmd_build_archive(a):
    S = XS.Schemas()
    man = build_manifest(a.project, a.out)
    doc = read_jsonc(Path(a.project) / "archive.jsonc")
    for p, e in zip(doc["packs"], man["entries"]):
        bd = Builder(Path(a.project) / p, S)
        bd.build(Path(a.out) / e["path"])
        if bd.edited:
            print(f"{p}: re-encoded {', '.join(bd.edited)}")
    print(f"{a.out}: {len(man['entries'])} packs and manifest.json")
    return 0


def hwvx_x():
    p = find_up("target/release/hwvx-x")
    return p


def pack_and_compare(built_arch, orig_root, source_file):
    """`hwvx-x pack` the built archive folder and compare with the original archive file."""
    import subprocess
    exe = hwvx_x()
    if exe is None:
        return "hwvx-x not built (target/release/hwvx-x)"
    cands = [p for root in orig_root for p in sorted(Path(root).rglob("*")) if p.name.lower() == source_file.lower()]
    if not cands:
        return f"no original {source_file} under {', '.join(map(str, orig_root))}"
    out = Path(built_arch).parent / ("_" + source_file)
    r = subprocess.run([str(exe), "pack", str(built_arch), str(out), "-f"], capture_output=True, text=True)
    if r.returncode:
        return "hwvx-x pack failed: " + r.stderr.strip()[-200:]
    same = out.read_bytes() == cands[0].read_bytes()
    out.unlink()
    return None if same else f"{source_file} differs from {cands[0]}"


def orig_dir(spec, rel):
    """Folders to look for a release's original archives in, first match wins: --orig ROOT
    (ROOT/<rel>), or rel=path+path,rel=path (e.g. pc=orig/pc-backup+orig/pc)."""
    if "=" in spec:
        m = dict(x.split("=", 1) for x in spec.split(","))
        return [Path(x) for x in m[rel].split("+")]
    return [Path(spec) / rel]


def survey_archives(root, rel, only=None):
    base = Path(root) / rel
    out = []
    for m in sorted(base.rglob("manifest.json")):
        d = m.parent
        if only and d.name.lower() not in only:
            continue
        out.append(d)
    return out


def cmd_roundtrip(a):
    S = XS.Schemas()
    only = {x.lower() for x in a.archive.split(",")} if a.archive else None
    kinds = set(a.kinds.split(",")) if a.kinds else None
    tot = collections.Counter()
    untyped = collections.Counter()
    notes = collections.Counter()
    fails = []
    outroot = Path(a.out)
    for rel in a.rel.split(","):
        for arch in survey_archives(a.survey, rel, only):
            man = json.loads((arch / "manifest.json").read_text())
            relpath = arch.relative_to(Path(a.survey) / rel)
            write_archive(arch, outroot / rel / relpath)
            arch_ok = True
            for ent in man["entries"]:
                if kinds and ent["type"] not in kinds:
                    continue
                pdir = arch / ent["path"]
                proj = outroot / rel / relpath / pack_name(ent)
                built = outroot / "_built" / rel / relpath / ent["path"]
                label = f"{rel} {relpath}/{ent['path']}"
                key = (rel, ent["type"])
                tot[key + ("packs",)] += 1
                try:
                    src = Source(pdir)
                    s = Extractor(src, proj, S).run()
                    untyped.update(s["untyped"])
                    notes.update(s["notes"])
                    for m in s["log"]:
                        notes[m.split(" @")[0].split(":")[0] if "not decoded" not in m else "geometry not decoded"] += 1
                    Builder(proj, S).build(built)
                    d = compare(pdir, built)
                except (GraphError, XS.XdlError, XG.GeomError) as e:
                    d = [("error", str(e))]
                if d:
                    arch_ok = False
                    fails.append(label)
                    print(f"FAIL {label}: {d[:3]}", flush=True)
                else:
                    tot[key + ("ok",)] += 1
                    tot[key + ("nodes",)] += s["nodes"]
                    tot[key + ("typed",)] += s["typed"]
                    tot[key + ("geometry",)] += s["geometry"]
                    if a.verbose:
                        print(f"ok   {label}: {s['nodes']} nodes, {s['files']} files", flush=True)
                if not a.keep and not a.orig:
                    shutil.rmtree(built, ignore_errors=True)
            if a.orig and arch_ok and not kinds:
                barch = outroot / "_built" / rel / relpath
                build_manifest(outroot / rel / relpath, barch)
                why = pack_and_compare(barch, orig_dir(a.orig, rel), man["source_file"])
                key = (rel, "(archive file)")
                tot[key + ("packs",)] += 1
                if why:
                    fails.append(f"{rel} {relpath}")
                    print(f"FAIL {rel} {relpath} archive: {why}", flush=True)
                else:
                    tot[key + ("ok",)] += 1
                if not a.keep:
                    shutil.rmtree(barch, ignore_errors=True)
    print("\nrelease  kind            ok/packs   nodes  typed  geometry")
    for rel, kind in sorted({k[:2] for k in tot}):
        g = lambda f: tot[(rel, kind, f)]  # noqa: E731
        print(f"{rel:8} {kind:15} {g('ok'):4}/{g('packs'):<5} {g('nodes'):7} {g('typed'):6} {g('geometry'):8}")
    if a.untyped:
        print("\nraw nodes by walker label:")
        for k, v in untyped.most_common():
            print(f"  {v:8}  {k}")
        print("\nnotes:")
        for k, v in notes.most_common(40):
            print(f"  {v:8}  {k}")
    print(f"\n{sum(v for k, v in tot.items() if k[2] == 'ok')}/{sum(v for k, v in tot.items() if k[2] == 'packs')} "
          "packs byte-identical")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract")
    p.add_argument("pack")
    p.add_argument("out")
    p.add_argument("--force", action="store_true", help="replace OUT even if it isn't a previous extract")
    p = sub.add_parser("build")
    p.add_argument("project")
    p.add_argument("out")
    p.add_argument("--check")
    p = sub.add_parser("roundtrip")
    p.add_argument("survey")
    p.add_argument("--rel", default="gc,pc")
    p.add_argument("--archive", help="comma-separated archive names (default: all)")
    p.add_argument("--kinds", help="comma-separated pack types (car, world, ...)")
    p.add_argument("--out", default=str(HERE.parents[1] / "build-gltf"))
    p.add_argument("--keep", action="store_true", help="keep the built pack folders (OUT/_built)")
    p.add_argument("--untyped", action="store_true", help="list raw nodes by walker label")
    p.add_argument("--orig", help="folder of the original archives per release (ORIG/gc ...), or "
                   "rel=folder+folder,rel=folder (first match wins): also `hwvx-x pack` each rebuilt archive and compare the file")
    p.add_argument("-v", "--verbose", action="store_true")
    p = sub.add_parser("extract-archive")
    p.add_argument("archive")
    p.add_argument("out")
    p.add_argument("--force", action="store_true", help="replace pack folders that aren't previous extracts")
    p = sub.add_parser("build-archive")
    p.add_argument("project")
    p.add_argument("out")
    a = ap.parse_args()
    try:
        return {"extract": cmd_extract, "build": cmd_build, "roundtrip": cmd_roundtrip,
                "extract-archive": cmd_extract_archive, "build-archive": cmd_build_archive}[a.cmd](a)
    except (GraphError, XS.XdlError, XG.GeomError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
