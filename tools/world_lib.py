#!/usr/bin/env python3
"""Reading worlds straight from a release's archives: the shared part of world_stats.py and
bsp_build.py (docs/worlds/README.md).

    sources()                  the releases' data folders, from the game's sources.cfg
    Release(rel, folder)       its archives by name; .world(path) / .collision(name) / .quads(name)
    World                      the world pack's root object (World.h): quadrants, regions, volumes
    Collision                  a collision pack (Collision.h): the grid, its cells' BSPs, the edges
    bspn0_* / tbsp_*           the two BSP encodings as neutral trees

A neutral tree is a list of nodes (plane, front, back, extra) in the pack's order, node 0 the
root; front/back are node indices, None where the pack has no child: an empty leaf in front, a
solid leaf behind. Both encodings are the same solid-leaf BSP:

    BSPN0 (regions, volumes; BSPN0.c)   0x20: plane f32[4], u8 flags, u8 pad, u16 next, pad 0xC
        flags bit 0: the front child is the next node; bit 1: the back child is node + next.
        Strictly in front (d > 0) is the front side.
    terrain (collision cells)           0x20: plane f32[4], u32 material, u16 front, u16 back, pad 8
        child node indices, 0xFFFF: none. d >= 0 is the front side.

Nothing here writes into the data folders.
"""

import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_gltf as XG  # noqa: E402
import xdl_media as XM  # noqa: E402

RELEASES = ("gc", "pc", "ps2")
EXT = {".xgc": "gc", ".xpc": "pc", ".xps": "ps2"}
T_WORLD, T_COLLISION, T_WORLDTEX, T_GEOMETRY = 4, 5, 6, 7
LEAF = 0xFFFF


class WorldError(Exception):
    pass


# ------------------------------------------------------------------------------------------
# where the data is

def pref_dir():
    env = os.environ.get("HWVX_PREF_DIR")
    if env:
        return Path(env)
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support/HWVX-SDL/HotWheelsVelocityX"
    if os.name == "nt":
        return Path(os.environ.get("APPDATA", "")) / "HWVX-SDL/HotWheelsVelocityX"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "HWVX-SDL/HotWheelsVelocityX"


def sources(cfg=None):
    """{release: folder} from sources.cfg (the launcher's "gc=PATH" lines). Read only."""
    cfg = Path(cfg) if cfg else pref_dir() / "sources.cfg"
    out = {}
    if not cfg.is_file():
        return out
    for line in cfg.read_text(errors="replace").splitlines():
        key, _, val = line.partition("=")
        if key in RELEASES and val and Path(val).is_dir():
            out[key] = Path(val)
    return out


class Release:
    """A release's archives, found by walking its folder; names are lower-case stems."""

    def __init__(self, rel, folder):
        self.rel, self.folder = rel, Path(folder)
        self.files = {}
        for p in sorted(self.folder.rglob("*")):
            if p.suffix.lower() in EXT and EXT[p.suffix.lower()] == rel and p.is_file():
                self.files.setdefault(p.stem.lower(), p)
        self._arc = {}

    def archive(self, name):
        name = name.replace("\\", "/").split("/")[-1].lower()
        if name not in self._arc:
            if name not in self.files:
                raise WorldError(f"{self.rel}: no archive {name}")
            self._arc[name] = XM.Archive(self.files[name])
        return self._arc[name]

    def drop(self, name):
        self._arc.pop(name.replace("\\", "/").split("/")[-1].lower(), None)

    def world_names(self):
        """Archives holding a world pack (type 4), by reading each TOC's first entries."""
        out = []
        for name, p in self.files.items():
            e = ">" if self.rel == "gc" else "<"
            with open(p, "rb") as f:
                head = f.read(16)
                if len(head) < 16 or struct.unpack_from(e + "I", head, 0)[0] != XM.MAGIC0:
                    continue
                n = struct.unpack_from(e + "I", head, 12)[0]
                toc = f.read(24 * n)
            if any(struct.unpack_from(e + "I", toc, 24 * i + 4)[0] == T_WORLD for i in range(len(toc) // 24)):
                out.append(name)
        return sorted(out)

    def pack(self, name, typ, index=0):
        arc = self.archive(name)
        ents = [x for x in arc.entries if x["type"] == typ]
        if index >= len(ents):
            raise WorldError(f"{self.rel}/{name}: no pack of type {typ}")
        return Pack(arc, ents[index])

    def world(self, name):
        return World(self.pack(name, T_WORLD), name)

    def collision(self, name):
        return Collision(self.pack(name, T_COLLISION))


class Pack:
    """One pack's data blob with typed readers, and its tables."""

    def __init__(self, arc, ent):
        self.rel, self.ent = arc.release, ent
        self.raw = XM.RawPack(arc.release, ent["type"], arc.pack_bytes(ent), f"{arc.name}/{ent['name']}")
        self.e = self.raw.e
        self.d = self.raw.section("data")
        self.size = ent["size"]
        self._ptrs = None

    @property
    def ptrs(self):
        if self._ptrs is None:
            self._ptrs = frozenset(self.raw.words("pointers"))
        return self._ptrs

    def u8(self, o):
        return self.d[o]

    def u16(self, o):
        return struct.unpack_from(self.e + "H", self.d, o)[0]

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.d, o)[0]

    def s32(self, o):
        return struct.unpack_from(self.e + "i", self.d, o)[0]

    def f32(self, o, n=1):
        v = struct.unpack_from(self.e + "%df" % n, self.d, o)
        return v[0] if n == 1 else v

    def u32s(self, o, n):
        return struct.unpack_from(self.e + "%dI" % n, self.d, o)

    def cstr(self, o):
        if not o or o >= len(self.d):
            return None
        return self.d[o:self.d.index(b"\0", o)].decode("latin-1")

    def pairs(self, section):
        w = self.raw.words(section)
        return list(zip(w[::2], w[1::2]))


# ------------------------------------------------------------------------------------------
# BSPN0: regions and volumes

def bspn0_read(pk, off, limit=4096):
    """The tree at `off` (node 0), following the child flags; the nodes are contiguous."""
    nodes, todo, seen = {}, [0], set()
    while todo:
        k = todo.pop()
        if k in seen:
            continue
        seen.add(k)
        if len(seen) > limit:
            raise WorldError(f"BSPN0 at 0x{off:X}: more than {limit} nodes")
        o = off + k * 0x20
        flags, nxt = pk.u8(o + 0x10), pk.u16(o + 0x12)
        front = k + 1 if flags & 1 else None
        back = k + nxt if flags & 2 else None
        nodes[k] = (pk.f32(o, 4), front, back, None)
        todo += [c for c in (front, back) if c is not None]
    if sorted(nodes) != list(range(len(nodes))):
        raise WorldError(f"BSPN0 at 0x{off:X}: nodes not contiguous")
    return [nodes[k] for k in range(len(nodes))]


def bspn0_read_bytes(b, e="<"):
    """BSPN0 bytes (a whole tree, node 0 first) -> [(plane, front, back)]."""
    out = []
    for k in range(len(b) // 0x20):
        o = k * 0x20
        flags, nxt = b[o + 0x10], struct.unpack_from(e + "H", b, o + 0x12)[0]
        out.append((struct.unpack_from(e + "4f", b, o), k + 1 if flags & 1 else None, k + nxt if flags & 2 else None))
    return out


def bspn0_write(tree, e="<", pad=b"\xCD"):
    """A tree (in pre-order: front child at +1) as BSPN0 bytes. Unused bytes are the
    exporter's uninitialised 0xCD."""
    out = bytearray()
    for k, (plane, front, back, _) in enumerate(tree):
        if front is not None and front != k + 1:
            raise WorldError("BSPN0 needs the front child right after its node")
        flags = (1 if front is not None else 0) | (2 if back is not None else 0)
        out += struct.pack(e + "4f", *plane) + bytes([flags]) + pad
        out += struct.pack(e + "H", back - k) if back is not None else pad * 2
        out += pad * 0xC
    return bytes(out)


def tbsp_read(pk, off, count):
    out = []
    for k in range(count):
        o = off + k * 0x20
        f, b = pk.u16(o + 0x14), pk.u16(o + 0x16)
        out.append((pk.f32(o, 4), None if f == LEAF else f, None if b == LEAF else b, pk.u32(o + 0x10)))
    return out


def tbsp_write(tree, e="<", pad=b"\xCD" * 8):
    """A tree as collision-cell nodes. The 8 unused bytes are the exporter's 0xCD fill."""
    out = bytearray()
    for plane, front, back, material in tree:
        out += struct.pack(e + "4fI2H", *plane, material or 0, LEAF if front is None else front,
                           LEAF if back is None else back) + pad
    return bytes(out)


def inside(tree, p, strict=True):
    """Point in solid. strict: BSPN0's rule (d > 0 is in front); else the terrain's (d >= 0)."""
    k = 0
    while True:
        (a, b, c, d), front, back, _ = tree[k]
        dist = a * p[0] + b * p[1] + c * p[2] + d
        if (dist > 0.0) if strict else (dist >= 0.0):
            if front is None:
                return False
            k = front
        else:
            if back is None:
                return True
            k = back


def depth(tree):
    best, todo = 0, [(0, 1)]
    while todo:
        k, dep = todo.pop()
        best = max(best, dep)
        for c in tree[k][1:3]:
            if c is not None:
                todo.append((c, dep + 1))
    return best


def same_plane(p, q, eps=1e-3):
    return all(abs(x - y) <= eps * max(1.0, abs(y)) for x, y in zip(p, q))


def is_convex(tree):
    """True when the solid is an intersection of half-spaces: every front subtree only repeats
    its node's plane (the second triangle of a face)."""
    def only(k, plane):
        if k is None:
            return True
        pl, f, b, _ = tree[k]
        return same_plane(pl, plane) and only(f, plane) and only(b, plane)
    return all(only(f, pl) for pl, f, _, _ in tree)


# ------------------------------------------------------------------------------------------
# the world pack

class World:
    """World.h's World at the base of a world pack."""

    def __init__(self, pk, name):
        self.pk, self.archive = pk, name
        u = pk.u32
        self.name = pk.cstr(u(0x8C))                # the quad packs' archive
        self.id = u(0x90)
        self.collision_name, self.collision_id = pk.cstr(u(0x7C)), pk.s32(0x80)
        self.tex_name, self.tex_id = pk.cstr(u(0x84)), pk.s32(0x88)
        self.counts = {k: u(o) for k, o in (
            ("tileCars", 0x08), ("tilePaths", 0x10), ("quads", 0x20), ("speakers", 0x28),
            ("teleporters", 0x30), ("tileLights", 0x38), ("tileCreatures", 0x40), ("variables", 0x48),
            ("timers", 0x50), ("objectives", 0x58), ("volumes", 0x68))}
        self.quads = [Quad(pk, u(0x24) + i * 0x50, i) for i in range(u(0x20))]
        by_id = {q.id: q.index for q in self.quads}
        for q in self.quads:
            q.neighbors = [by_id.get(n) for n in q.neighbor_ids]

    def volumes(self):
        pk, out = self.pk, []
        base = pk.u32(0x6C)
        for i in range(self.counts["volumes"]):
            o = base + i * 0x30
            out.append({"shape": pk.s32(o + 8), "type": pk.u32(o + 0xC), "center": pk.f32(o + 0x10, 3),
                        "radius": pk.f32(o + 0x20), "bsp": pk.u32(o + 0x28)})
        return out

    def volume_grid(self):
        pk = self.pk
        o = pk.u32(0x04)
        if not o:
            return None
        return {"origin": pk.f32(o, 3), "cellSize": pk.f32(o + 0x20, 3),
                "n": (pk.s32(o + 0x2C), pk.s32(o + 0x30), pk.s32(o + 0x34))}

    def nav_points(self):
        pk = self.pk
        o = pk.u32(0x78)
        if not o:
            return []
        nodes, n = pk.u32(o), pk.u32(o + 4)         # NavData
        if not nodes:
            return []
        return [pk.f32(nodes + i * 0x60, 3) for i in range(n)]

    def tile_cars(self):
        pk = self.pk
        base = pk.u32(0x0C)
        return [{"pos": pk.f32(base + i * 0x60, 3), "port": pk.u8(base + i * 0x60 + 0x35)}
                for i in range(self.counts["tileCars"])]


class Quad:
    """A WorldQuad (0x50) and its QuadContents."""

    def __init__(self, pk, o, index):
        u = pk.u32
        self.index, self.off = index, o
        self.id = u(o)
        self.num_geoms = u(o + 0x0C)
        self.bsp_bytes = u(o + 0x18)
        self.bsp_off = u(o + 0x1C)
        n = u(o + 0x20)
        self.neighbor_ids = list(pk.u32s(u(o + 0x24), n)) if n else []
        self.neighbors = []
        self.pack_id = u(o + 0x38)
        c = u(o + 0x30)
        self.instances = [(pk.f32(u(c + 8) + i * 0x40, 3), u(u(c + 8) + i * 0x40 + 0x24))
                          for i in range(u(c + 4))] if c else []
        self.fx_points = [pk.f32(u(c + 0x10) + i * 0x40, 3) for i in range(u(c + 0xC))] if c else []
        self.items = [pk.f32(u(c + 0x18) + i * 0x40, 3) for i in range(u(c + 0x14))] if c else []
        self.region = bspn0_read(pk, self.bsp_off) if self.bsp_off else None


def quad_pack(release, world, quad):
    """The quadrant's geometry pack, as LoadQuadrants finds it: type 7, the world's id, the
    quadrant's packId (numGeoms << 16 | id)."""
    arc = release.archive(world.name)
    for ent in arc.entries:
        if ent["type"] == T_GEOMETRY and ent["id"] == world.id and ent["sub_id"] == quad.pack_id \
                and ent["sub_id"] >> 16 == quad.num_geoms:
            return Pack(arc, ent)
    return None


def quad_geometry(pk):
    """Per geometry of a quad pack: id, bounds, and what each level of detail draws."""
    blob = XG.Blob(pk.d, pk.ptrs, pk.rel)
    tex = dict(pk.pairs("texture_relocs"))
    table = pk.u32(0)
    ids = {}
    k = 0
    while table + k * 12 + 12 <= len(pk.d) and (table + k * 12 + 8) in pk.ptrs:
        ids[pk.u32(table + k * 12 + 8)] = pk.u32(table + k * 12)
        k += 1
    out = []
    for off in XG.scan(blob):
        g = XG.decode(blob, off, tex)
        box = struct.unpack("<12f", bytes.fromhex(g["box"]))
        if pk.rel == "gc":
            box = struct.unpack(">12f", bytes.fromhex(g["box"]))
        levels = []
        for lv in g["levels"]:
            if lv["kind"] != "drawn":
                continue
            groups = lv["groups"]
            levels.append({"groups": len(groups), "tris": sum(len(x["tris"]) for x in groups),
                           "verts": sum(len(x["verts"]) for x in groups),
                           "max_group_verts": max((len(x["verts"]) for x in groups), default=0),
                           "textures": sorted({x["texture"] for x in groups if x["texture"] is not None}),
                           "flags": sorted({x["rec"]["flags"] for x in groups})})
        out.append({"id": ids.get(off), "offset": off, "min": box[0:3], "max": box[4:7], "center": box[8:11],
                    "radius": box[7], "levels": levels,
                    "dist": [struct.unpack("<f", bytes.fromhex(x))[0] for x in g["dist"]]})
    return out


# ------------------------------------------------------------------------------------------
# the collision pack

class Collision:
    """Collision.h's WorldCollision at the base of a collision pack."""

    def __init__(self, pk):
        self.pk = pk
        u = pk.u32
        self.min, self.max = pk.f32(0, 3), pk.f32(0x10, 3)
        self.version = u(0x24)
        self.cell_size = pk.f32(0x28, 3)
        self.n = (pk.s32(0x34), pk.s32(0x38), pk.s32(0x3C))
        self.num_polys, self.num_edges, self.mode = u(0x48), u(0x50), u(0x58)
        self.edges_off, self.cells_off, self.materials_off = u(0x54), u(0x20), u(0x5C)
        nx, ny, nz = self.n
        self.cell_index = struct.unpack_from(pk.e + "%di" % (nx * ny * nz), pk.d, u(0x40))
        self.num_cells = max(self.cell_index) + 1

    def cell(self, i):
        pk, o = self.pk, self.cells_off + i * 0x50
        return {"min": pk.f32(o, 3), "max": pk.f32(o + 0x10, 3), "nodes_off": pk.u32(o + 0x30),
                "num_nodes": pk.u32(o + 0x3C), "num_edges": pk.u32(o + 0x40), "edges_off": pk.u32(o + 0x44)}

    def cell_tree(self, i):
        c = self.cell(i)
        if not c["nodes_off"] or not c["num_nodes"]:
            return None
        return tbsp_read(self.pk, c["nodes_off"], c["num_nodes"])

    def edge(self, i):
        o = self.edges_off + i * 0x30
        return self.pk.f32(o, 3), self.pk.f32(o + 0x10, 3), self.pk.u32(o + 0x24)


def inside_many(tree, pts, strict=True):
    """inside() for an (N, 3) numpy array; a bool array."""
    import numpy as np
    out = np.zeros(len(pts), bool)
    todo = [(0, np.arange(len(pts)))]
    while todo:
        k, idx = todo.pop()
        if not len(idx):
            continue
        (a, b, c, d), front, back, _ = tree[k]
        p = pts[idx]
        dist = p[:, 0] * a + p[:, 1] * b + p[:, 2] * c + d
        f = dist > 0.0 if strict else dist >= 0.0
        if front is not None:
            todo.append((front, idx[f]))
        if back is not None:
            todo.append((back, idx[~f]))
        else:
            out[idx[~f]] = True
    return out
