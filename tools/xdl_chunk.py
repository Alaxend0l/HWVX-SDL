#!/usr/bin/env python3
"""World auto-chunker: a glTF world into the game's quadrant packs (docs/worlds/README.md,
"Chunker algorithm"). GameCube layout.

    xdl_chunk.py extract LEVEL PROJECT [--set NAME] [--level-out PATH]
        A retail level's world as a chunker project, from the player's own data (sources.cfg):
        every quadrant geometry as an xdl glTF (render/), the collision surface recovered from
        the collision pack's BSPs (collision.gltf, with the crease edges), world.jsonc.
    xdl_chunk.py arena PROJECT [--base LEVEL]
        A generated test arena (flat ground, ramps, walls; our own geometry, base textures).
    xdl_chunk.py plan PROJECT
        Quadrants (strips along the world's long axis), neighbours, budgets; PROJECT/plan.json.
    xdl_chunk.py build PROJECT OUT
        hwvx-x extract folders in OUT (OUT/<set>_geo, OUT/<set>_col, OUT/<level>), each with its
        manifest.json: the quadrant packs, the collision pack, the level's world pack.
    xdl_chunk.py stage PROJECT OUT DATA STAGED
        `hwvx-x pack` OUT's folders into STAGED, a data folder that links everything else to
        DATA (the player's data is never written to).
    xdl_chunk.py stats PROJECT [--retail LEVEL]
        The plan's budgets next to a retail level's.

A project (world.jsonc, JSONC):

    {"format_version": 1, "release": "gc",
     "set": "cw1",                                   -> worlds_\\geo_\\cw1_geo, worlds_\\col_\\cw1_col
     "level": "worlds_\\sp_\\joyride_\\cworld1",     the level archive (its world pack)
     "base": "worlds_\\sp_\\joyride_\\world1",       the retail level whose world pack (scripts,
                                                     volumes, nav, records) and collision materials
                                                     the new level is made from
     "textures": "worlds_\\tex_\\w1_tex",            world texture pack (default: the base's)
     "render": "render",                             folder of glTFs (xdl geometry glTFs, or plain)
     "collision": "collision.gltf",                  triangles (extras.material), LINES with
                                                     extras.creases (else computed)
     "objects": "base" | "none",                     the base's objects, placed by position
     "start": [[x, y, z, yaw_degrees], ...],         optional: the start tiles moved here
     "chunk": {"max_tris": 5000, ...}}               budgets (CHUNK below)

Strips, not a grid: each quadrant is a slab across the whole world, so it touches at most two
others and its neighbour list is the two on each side (<= 4, the code's limit; two cameras then
stream at most 10 quadrants). Everything a camera draws is its strip, two strips each way and the
hub, so a strip's width is how far the world is drawn along the long axis; `plan` reports it.
"""

import argparse
import bisect
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import bsp_build as BB  # noqa: E402
import world_lib as WL  # noqa: E402
import xdl_gltf as XG  # noqa: E402

E = ">"                       # GameCube
QUAD_LEN = 0x50
HUB_PACK = 0xFFFF
VERSION = 183
FAR = 20000.0                 # how far the outer regions reach past the world's bounds
EDGE_REACH = 10.0             # a cell lists the crease edges within this of its box (the retail
                              # packs: segment against the box grown by 10 gives w1_col's lists
                              # for 455 of 472 cells, the rest miss a few)

CHUNK = {
    "max_tris": 5000,         # per quadrant (retail median 4,489, max 8,599)
    "max_verts": 8000,
    "max_bytes": 250000,      # GameCube quad pack (retail median 165 KB, max 377 KB)
    "max_textures": 60,
    "active_tris": 30000,     # a quadrant + its neighbours + the hub (retail max 33,582)
    "active_bytes": 1200000,  # (retail max 1.29 MB)
    "min_quads": 1,
    "overlap": 0.05,          # region overlap (hysteresis), share of the strip's width, >= 10
    "below": 1000.0,          # regions reach this far below the lowest collision point
    "above": 5000.0,          # and above the highest (cameras)
    "cell_xz": 250.0,         # collision squares
    "cell_y": 200.0,
    "crease_deg": 0.5,
}


class ChunkError(Exception):
    pass


def jsonc(path):
    text = Path(path).read_text()
    text = re.sub(r'("(?:\\.|[^"\\])*")|//[^\n]*|/\*.*?\*/', lambda m: m.group(1) or "", text, flags=re.S)
    return json.loads(text)


def a32(n, a=32):
    return (n + a - 1) // a * a


def archive_file(name):
    """worlds_\\geo_\\w1_geo -> worlds_/geo_/w1_geo.xgc"""
    return name.replace("\\", "/") + ".xgc"


def hwvx_x():
    env = os.environ.get("HWVX_X")
    if env:
        return env
    for d in HERE.parents:
        for sub in ("target/release/hwvx-x", "target/debug/hwvx-x"):
            if (d / sub).is_file():
                return str(d / sub)
    raise ChunkError("hwvx-x not found (set HWVX_X)")


def release_data(rel="gc"):
    src = WL.sources()
    if rel not in src:
        raise ChunkError(f"no {rel} data in sources.cfg")
    return WL.Release(rel, src[rel]), src[rel]


# ------------------------------------------------------------------------------------------
# texture links

TEX_RE = re.compile(r"base://([^/]+)/(world_texture|share)_(\d+)/texture/(\d+)$")


def tex_link(texarc, v):
    p, n = v >> 16, v & 0xFFFF
    if p == 0:
        return f"base://{texarc}/world_texture_0/texture/{n}"
    return f"base://hw_share/share_{p}/texture/{n}"


def tex_value(link):
    m = TEX_RE.match(link or "")
    if not m:
        raise ChunkError(f"texture link {link!r}: want base://<world texture archive>/world_texture_0/"
                         "texture/N or base://hw_share/share_1/texture/N")
    return int(m.group(4)) if m.group(2) == "world_texture" else (int(m.group(3)) << 16) | int(m.group(4))


# ------------------------------------------------------------------------------------------
# glTF helpers (collision and plain meshes)

def gl_write(path, gl, binb):
    path = Path(path)
    gl["buffers"] = [{"uri": path.with_suffix(".bin").name, "byteLength": len(binb)}]
    path.write_text(json.dumps(gl, indent=1) + "\n")
    path.with_suffix(".bin").write_bytes(binb)


class GBuf:
    def __init__(self):
        self.b, self.views, self.acc = bytearray(), [], []

    def add(self, arr, comp, typ, target=None, minmax=False):
        arr = np.ascontiguousarray(arr)
        while len(self.b) % 4:
            self.b.append(0)
        v = {"buffer": 0, "byteOffset": len(self.b), "byteLength": arr.nbytes}
        if target:
            v["target"] = target
        self.views.append(v)
        self.b += arr.astype(arr.dtype.newbyteorder("<")).tobytes()
        a = {"bufferView": len(self.views) - 1, "componentType": comp, "count": len(arr), "type": typ}
        if minmax:
            a["min"] = [float(x) for x in arr.min(0)]
            a["max"] = [float(x) for x in arr.max(0)]
        self.acc.append(a)
        return len(self.acc) - 1


def gl_accessor(gl, binb, i):
    a = gl["accessors"][i]
    dt = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[a["componentType"]]
    nc = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    bv = gl["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    item = np.dtype(dt).itemsize * nc
    stride = bv.get("byteStride", item)
    raw = np.frombuffer(binb, np.uint8, count=stride * (a["count"] - 1) + item if a["count"] else 0, offset=off)
    if stride == item:
        out = raw.view(dt).reshape(a["count"], nc)
    else:
        out = np.stack([raw[k * stride:k * stride + item].view(dt) for k in range(a["count"])])
    if a.get("normalized") and dt != "<f4":
        out = out.astype(float) / {"u1": 255.0, "<u2": 65535.0, "i1": 127.0, "<i2": 32767.0}[dt]
    return out


def node_matrices(gl):
    """World matrix of every node of the default scene."""
    out = {}

    def local(nd):
        if "matrix" in nd:
            return np.array(nd["matrix"], float).reshape(4, 4).T
        m = np.eye(4)
        if "scale" in nd:
            m = np.diag([*nd["scale"], 1.0]) @ m
        if "rotation" in nd:
            x, y, z, w = nd["rotation"]
            r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                          [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                          [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
            rm = np.eye(4)
            rm[:3, :3] = r
            m = rm @ m
        if "translation" in nd:
            t = np.eye(4)
            t[:3, 3] = nd["translation"]
            m = t @ m
        return m

    def walk(i, parent):
        m = parent @ local(gl["nodes"][i])
        out[i] = m
        for c in gl["nodes"][i].get("children", []):
            walk(c, m)
    for r in gl["scenes"][gl.get("scene", 0)]["nodes"]:
        walk(r, np.eye(4))
    return out


def load_gl(path):
    path = Path(path)
    gl = json.loads(path.read_text())
    b = (path.parent / gl["buffers"][0]["uri"]).read_bytes() if gl.get("buffers") else b""
    return gl, b


# ------------------------------------------------------------------------------------------
# geometry: one drawable piece of the world

LEVEL_REC = {"u0": 0, "u4": 0, "u10": 0}
GROUP_REC = {"u0": 0, "flags": 0x1000, "colour": "00000000", "u8": 0, "blend": 0, "sort": 0, "uc": 0}
FAR_LOD = "20bcbe4c"        # every retail world geometry's single LOD distance


class Geo:
    """A geometry in the neutral form (xdl_gltf.decode's), with its texture and shader values per
    group, its id and where the source wants it (hub)."""

    def __init__(self, g, gid, hub=False, name=""):
        self.g, self.id, self.hub, self.name = g, gid, hub, name
        pts = [p for lv in g["levels"][:1] for gr in lv["groups"] for p in gr.get("verts", [])]
        if pts:
            p = np.frombuffer(b"".join(v[0] for v in pts), "<f4").reshape(-1, 3)
            self.lo, self.hi = p.min(0), p.max(0)
        else:
            self.lo = self.hi = np.zeros(3)
        self.centre = (self.lo + self.hi) / 2
        lv0 = [lv for lv in g["levels"] if lv["kind"] == "drawn"][:1]
        self.tris = sum(len(gr["tris"]) for lv in lv0 for gr in lv["groups"])
        self.verts = sum(len(gr["verts"]) for lv in lv0 for gr in lv["groups"])
        self.groups = sum(len(lv["groups"]) for lv in lv0)
        self.textures = {tex_of(gr) for lv in g["levels"] for gr in lv["groups"]} - {None}
        self._enc = None

    def encoded(self):
        if self._enc is None:
            self._enc = XG.encode(self.g, "gc")
        return self._enc

    @property
    def nbytes(self):
        return len(self.encoded()[0])


def tex_of(gr):
    if gr.get("texture_reloc") is not None:
        return gr["texture_reloc"]
    if gr.get("texture_link"):
        return tex_value(gr["texture_link"])
    return gr.get("texture")


def geo_from_xdl_gltf(path):
    gl, b = XG.load(path)
    g = XG.from_gltf(gl, b)
    root = gl["nodes"][gl["scenes"][gl.get("scene", 0)]["nodes"][0]]
    x = root.get("extras", {})
    by_level = {}
    for ci in root.get("children", []):
        nd = gl["nodes"][ci]
        if "mesh" in nd:
            by_level[nd["extras"]["level"]] = gl["meshes"][nd["mesh"]]
    for li, lv in enumerate(g["levels"]):
        prims = by_level.get(li, {}).get("primitives", [])
        for k, gr in enumerate(lv["groups"]):
            gr["shader"] = prims[k].get("extras", {}).get("shader") if k < len(prims) else None
    return Geo(g, x.get("geometryId"), bool(x.get("hub")), Path(path).stem)


def geos_from_plain_gltf(path):
    """A glTF from any tool: one geometry per mesh node (world positions), a group per primitive
    (unlit, vertex coloured, the material's extras.texture or name as the texture link)."""
    gl, b = load_gl(path)
    mats = node_matrices(gl)
    out = []
    for ni, nd in enumerate(gl["nodes"]):
        if "mesh" not in nd or ni not in mats:
            continue
        m = mats[ni]
        groups, names = [], []
        for pr in gl["meshes"][nd["mesh"]]["primitives"]:
            if pr.get("mode", 4) != 4:
                continue
            at = pr["attributes"]
            pos = gl_accessor(gl, b, at["POSITION"]).astype(float)
            pos = pos @ m[:3, :3].T + m[:3, 3]
            n = len(pos)
            uv = gl_accessor(gl, b, at["TEXCOORD_0"]).astype(float) if "TEXCOORD_0" in at else np.zeros((n, 2))
            if "COLOR_0" in at:
                c = gl_accessor(gl, b, at["COLOR_0"])
                c = c.astype(float) if c.dtype.kind == "f" else c / 255.0
                if c.shape[1] == 3:
                    c = np.hstack([c, np.ones((n, 1))])
                col = np.clip(np.round(c * 255), 0, 255).astype(np.uint8)
            else:
                col = np.full((n, 4), 0xFF, np.uint8)
            idx = gl_accessor(gl, b, pr["indices"]).ravel() if "indices" in pr else np.arange(n)
            mat = gl["materials"][pr["material"]] if "material" in pr else {}
            link = mat.get("extras", {}).get("texture") or (mat.get("name") if TEX_RE.match(mat.get("name") or "") else None)
            verts = [(struct.pack("<3f", *pos[i]), None, bytes(col[i]), struct.pack("<2f", *uv[i]), 0) for i in range(n)]
            rec = dict(GROUP_REC)
            rec.update(mat.get("extras", {}).get("group", {}))
            groups.append({"rec": rec, "verts": verts, "tris": [tuple(int(x) for x in idx[i:i + 3]) for i in range(0, len(idx) - 2, 3)],
                           "uvscroll": None, "texture_link": link, "texture_reloc": None,
                           "shader": mat.get("extras", {}).get("shader")})
            names.append((mat.get("name") or "tex")[:31])
        if not groups:
            continue
        g = {"release": "gc", "magic": XG.MAGIC["gc"], "skinned": 0, "u6": 0, "box": "00" * 0x30,
             "dist": [FAR_LOD], "levels": [{"kind": "drawn", "rec": dict(LEVEL_REC), "names": names,
                                            "undecoded": False, "skin": None, "groups": groups}]}
        ex = nd.get("extras", {})
        out.append(Geo(g, ex.get("geometryId"), bool(ex.get("hub")) or (nd.get("name") or "").startswith("hub"),
                       nd.get("name") or f"node{ni}"))
    return out


def quad_geos(rel, world, q, hub=None):
    """A quadrant's geometries, decoded from the player's data (never stored in a project)."""
    pk = WL.quad_pack(rel, world, q)
    if pk is None:
        return []
    blob = XG.Blob(pk.d, pk.ptrs, rel.rel)
    tex, sh = dict(pk.pairs("texture_relocs")), dict(pk.pairs("shader_relocs"))
    table = pk.u32(0)
    ids = {pk.u32(table + 12 * k + 8): pk.u32(table + 12 * k) for k in range(q.num_geoms)}
    out = []
    for off in XG.scan(blob):
        g = XG.decode(blob, off, tex)
        at = {v: k for k, v in g["names"].items()}
        for li, lv in enumerate(g["levels"]):
            for k, gr in enumerate(lv["groups"]):
                gr["shader"] = sh.get(at.get(f"level/{li}/group/{k}"))
                gr["texture_reloc"] = gr.get("texture")
        out.append(Geo(g, ids.get(off), q.bsp_off == 0 if hub is None else hub, f"q{q.index}_{ids.get(off)}"))
    return out


def base_hub(pj):
    """"hub": "base": the base level's hub geometry (sky, backdrop), read from the player's data
    at build time."""
    w = pj.base_world
    return [g for q in w.quads if q.bsp_off == 0 for g in quad_geos(pj.rel, w, q, True)]


def split_geo(geo, keyfn, ids):
    """Cut a geometry's triangles into pieces by keyfn(centroid) (one drawn level only); each piece
    a geometry of its own (the first keeps the id)."""
    g = geo.g
    if len(g["levels"]) != 1 or g["levels"][0]["kind"] != "drawn" or g["skinned"]:
        return [geo]
    lv = g["levels"][0]
    parts = {}
    for k, gr in enumerate(lv["groups"]):
        pos = np.frombuffer(b"".join(v[0] for v in gr["verts"]), "<f4").reshape(-1, 3) if gr["verts"] else None
        for t in gr["tris"]:
            key = keyfn(pos[list(t)].mean(0))
            parts.setdefault(key, {}).setdefault(k, []).append(t)
    if len(parts) <= 1:
        return [geo]
    out = []
    for n, key in enumerate(sorted(parts)):
        groups = []
        for k, tris in sorted(parts[key].items()):
            gr = lv["groups"][k]
            used = sorted({i for t in tris for i in t})
            remap = {o: i for i, o in enumerate(used)}
            ng = dict(gr)
            ng["verts"] = [gr["verts"][i] for i in used]
            ng["tris"] = [tuple(remap[i] for i in t) for t in tris]
            groups.append(ng)
        nl = dict(lv)
        nl["groups"] = groups
        ng_ = dict(g)
        ng_["levels"] = [nl]
        out.append(Geo(ng_, geo.id if n == 0 else next(ids), geo.hub, f"{geo.name}.{n}"))
    return out


# ------------------------------------------------------------------------------------------
# collision mesh

class ColMesh:
    """Collision polygons [(plane, verts (n,3), material)] and crease segments [(a, b, material)]."""

    def __init__(self, polys, creases, owners=None):
        self.polys, self.creases = polys, creases
        self.owners = owners or [None] * len(polys)     # the box a polygon was cut to, if any
        if polys:
            allv = np.concatenate([v for _, v, _ in polys])
            self.lo, self.hi = allv.min(0), allv.max(0)
        else:
            self.lo = self.hi = np.zeros(3)


def load_collision(path, crease_deg):
    """The collision glTF: triangles (extras.material per primitive; LINES with extras.creases are
    the crease edges, else they are computed). Polygons keep the file's order, which picks the
    BSP splitters (the exporter's rule: the first polygon of the list). Triangles carrying a
    _POLY attribute (and _PLANE) are polygons already (an extracted retail world)."""
    gl, b = load_gl(path)
    mats = node_matrices(gl)
    tris, tmat, creases, given = [], [], [], {}
    for ni, nd in enumerate(gl["nodes"]):
        if "mesh" not in nd or ni not in mats:
            continue
        m = mats[ni]
        for pr in gl["meshes"][nd["mesh"]]["primitives"]:
            ex = pr.get("extras", {})
            mat = int(ex.get("material", gl["materials"][pr["material"]].get("extras", {}).get("material", 0)
                             if "material" in pr else 0))
            at = pr["attributes"]
            pos = gl_accessor(gl, b, at["POSITION"]).astype(float) @ m[:3, :3].T + m[:3, 3]
            idx = gl_accessor(gl, b, pr["indices"]).ravel() if "indices" in pr else np.arange(len(pos))
            mode = pr.get("mode", 4)
            if mode == 1:
                for i in range(0, len(idx) - 1, 2):
                    creases.append((pos[idx[i]], pos[idx[i + 1]], mat))
            elif mode == 4 and "_POLY" in at:
                pid = gl_accessor(gl, b, at["_POLY"]).ravel()
                own = nd.get("extras", {}).get("sourceBox")
                plane = gl_accessor(gl, b, at["_PLANE"]) if "_PLANE" in at else None
                for i in range(0, len(idx) - 2, 3):
                    k = int(pid[idx[i]])
                    t = pos[idx[i:i + 3]]
                    if k not in given:
                        given[k] = [tuple(float(x) for x in plane[idx[i]]) if plane is not None else None,
                                    [t[0], t[1], t[2]], mat, own]
                    else:
                        given[k][1].append(t[2])
            elif mode == 4:
                for i in range(0, len(idx) - 2, 3):
                    tris.append(pos[idx[i:i + 3]])
                    tmat.append(mat)
    polys = []
    for k in sorted(given):
        plane, vs, mat, own = given[k]
        v = np.array(vs)
        if plane is None:
            plane = BB.plane_of(v)
        if plane is not None:
            polys.append((k, BB.f32_plane(plane), v, mat, own))
    base = len(polys) and max(given) + 1
    polys += [(base + o, pl, v, mt, None) for o, pl, v, mt in merge_coplanar(tris, tmat)]
    polys.sort(key=lambda x: x[0])
    have_creases = any(pr.get("mode", 4) == 1 for me in gl.get("meshes", []) for pr in me["primitives"])
    if not have_creases:
        creases = crease_edges(tris, tmat, crease_deg)
    return ColMesh([x[1:4] for x in polys], creases, [x[4] for x in polys])


def _weld(tris, eps=1e-3):
    pts = np.concatenate(tris) if tris else np.zeros((0, 3))
    key = np.round(pts / eps).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    return inv.reshape(-1, 3)


def merge_coplanar(tris, tmat):
    """Triangles -> convex polygons: edge-sharing triangles in one plane with one material are
    merged while the union stays convex (the exporter merged coplanar faces before building)."""
    ids = _weld(tris)
    polys = []
    pl = []
    for t in tris:
        p = BB.plane_of(t)
        pl.append(p)
    groups = {}
    for i, p in enumerate(pl):
        if p is None:
            continue
        key = (tmat[i], round(p[0], 4), round(p[1], 4), round(p[2], 4), round(p[3], 2))
        groups.setdefault(key, []).append(i)
    for key, members in groups.items():
        # polygon = list of vertex ids (ccw) + coords; greedy merge across shared edges
        cur = {i: (list(ids[i]), {int(ids[i][k]): tris[i][k] for k in range(3)}) for i in members}
        first = {i: i for i in members}
        n = np.array(pl[members[0]][:3])
        changed = True
        while changed:
            changed = False
            edge_of = {}
            for pid, (vs, _) in cur.items():
                for k in range(len(vs)):
                    edge_of.setdefault((vs[k], vs[(k + 1) % len(vs)]), pid)
            for pid in list(cur):
                if pid not in cur:
                    continue
                vs, co = cur[pid]
                for k in range(len(vs)):
                    a, b = vs[k], vs[(k + 1) % len(vs)]
                    other = edge_of.get((b, a))
                    if other is None or other == pid or other not in cur:
                        continue
                    ws, co2 = cur[other]
                    j = ws.index(b)
                    # merged ring: vs from b .. a (all), then ws after a .. before b
                    ring = vs[k + 1:] + vs[:k + 1]           # starts at b, ends at a
                    wj = ws[j:] + ws[:j]                      # starts at b: b, a, ...
                    ring = ring + wj[2:]
                    allco = dict(co)
                    allco.update(co2)
                    if len(set(ring)) != len(ring) or not _convex([allco[v] for v in ring], n):
                        continue
                    cur[pid] = (_drop_collinear(ring, allco, n), allco)
                    first[pid] = min(first[pid], first[other])
                    del cur[other]
                    changed = True
                    break
                if changed:
                    break
        for pid, (vs, co) in cur.items():
            v = np.array([co[x] for x in vs])
            p = BB.plane_of(v)
            if p is not None:
                polys.append((first[pid], BB.f32_plane(p), v, key[0]))
    return polys


def _convex(pts, n, eps=1e-6):
    m = len(pts)
    for i in range(m):
        a, b, c = pts[i], pts[(i + 1) % m], pts[(i + 2) % m]
        if float(np.dot(np.cross(np.subtract(b, a), np.subtract(c, b)), n)) < -eps:
            return False
    return True


def _drop_collinear(ring, co, n, eps=1e-6):
    out = []
    m = len(ring)
    for i in range(m):
        a, b, c = co[ring[i - 1]], co[ring[i]], co[ring[(i + 1) % m]]
        if np.linalg.norm(np.cross(np.subtract(b, a), np.subtract(c, b))) > eps * max(1.0, np.linalg.norm(np.subtract(c, a))) ** 2:
            out.append(ring[i])
    return out if len(out) >= 3 else ring


def crease_edges(tris, tmat, deg):
    """Edges where two faces meet at more than `deg` degrees (welded mesh)."""
    ids = _weld(tris)
    normals = []
    for t in tris:
        p = BB.plane_of(t)
        normals.append(np.array(p[:3]) if p is not None else None)
    faces = {}
    for i, t in enumerate(ids):
        if normals[i] is None:
            continue
        for k in range(3):
            a, b = int(t[k]), int(t[(k + 1) % 3])
            faces.setdefault((min(a, b), max(a, b)), []).append((i, k))
    cos = math.cos(math.radians(deg))
    out = []
    for (a, b), fs in faces.items():
        if len(fs) != 2:
            continue
        (i, k), (j, _) = fs
        if float(np.dot(normals[i], normals[j])) < cos:
            out.append((tris[i][k], tris[i][(k + 1) % 3], tmat[i]))
    return out


# ------------------------------------------------------------------------------------------
# the project

class Project:
    def __init__(self, folder):
        self.dir = Path(folder)
        self.cfg = jsonc(self.dir / "world.jsonc")
        c = self.cfg
        if c.get("release", "gc") != "gc":
            raise ChunkError("only the GameCube layout is written so far (release gc)")
        self.set, self.level, self.base = c["set"], c["level"], c["base"]
        self.chunk = dict(CHUNK)
        self.chunk.update(c.get("chunk", {}))
        self.rel, self.data = release_data("gc")
        self.base_world = self.rel.world(self.base)
        self.textures = c.get("textures") or self.base_world.tex_name
        self._geos = self._col = None

    @property
    def tex_archive(self):
        return self.textures.replace("\\", "/").split("/")[-1].lower()

    def geos(self):
        if self._geos is None:
            out = []
            for p in sorted((self.dir / self.cfg.get("render", "render")).glob("*.gltf")):
                gl = json.loads(p.read_text())
                root = gl["nodes"][gl["scenes"][gl.get("scene", 0)]["nodes"][0]]
                if root.get("extras", {}).get("xdl") == "geometry":
                    out.append(geo_from_xdl_gltf(p))
                else:
                    out += geos_from_plain_gltf(p)
            if self.cfg.get("hub") == "base":
                out += base_hub(self)
            ids = self.id_source(out)
            for g in out:
                if g.id is None:
                    g.id = next(ids)
            self._ids = ids
            self._geos = out
        return self._geos

    def id_source(self, geos):
        """Fresh object ids: past every id the base level uses (quadrants, geometries)."""
        top = max([q.id for q in self.base_world.quads] + [g.id for g in geos if g.id is not None] + [0x1000])

        def gen():
            k = top + 1
            while True:
                yield k
                k += 1
        return gen()

    def collision(self):
        if self._col is None:
            self._col = load_collision(self.dir / self.cfg.get("collision", "collision.gltf"), self.chunk["crease_deg"])
        return self._col


# ------------------------------------------------------------------------------------------
# planning: strips, neighbours, assignment

class Plan:
    pass


def make_plan(pj, log=print):
    C = pj.chunk
    geos = pj.geos()
    col = pj.collision()
    world = [g for g in geos if not g.hub]
    lo = np.minimum(col.lo, np.min([g.lo for g in world], 0)) if world else col.lo
    hi = np.maximum(col.hi, np.max([g.hi for g in world], 0)) if world else col.hi
    axis = 0 if (hi[0] - lo[0]) >= (hi[2] - lo[2]) else 2
    hub = [g for g in geos if g.hub]
    rest = [g for g in geos if not g.hub]
    hub_tris = sum(g.tris for g in hub)
    hub_bytes = sum(g.nbytes for g in hub)

    def strips_for(items, max_tris):
        items = sorted(items, key=lambda g: g.centre[axis])
        cuts, cur = [], {"tris": 0, "verts": 0, "bytes": 0, "tex": set()}
        for k, g in enumerate(items):
            over = cur["tris"] and (cur["tris"] + g.tris > max_tris or cur["verts"] + g.verts > C["max_verts"]
                                    or cur["bytes"] + g.nbytes > C["max_bytes"]
                                    or len(cur["tex"] | g.textures) > C["max_textures"])
            if over:
                cuts.append((items[k - 1].centre[axis] + g.centre[axis]) / 2)
                cur = {"tris": 0, "verts": 0, "bytes": 0, "tex": set()}
            cur["tris"] += g.tris
            cur["verts"] += g.verts
            cur["bytes"] += g.nbytes
            cur["tex"] |= g.textures
        if len(cuts) + 1 < C["min_quads"]:
            span = (lo[axis], hi[axis])
            cuts = list(np.linspace(*span, C["min_quads"] + 1)[1:-1])
        return cuts

    def strip_of(x, cuts):
        return bisect.bisect_right(cuts, x)

    ids = pj._ids
    max_tris = C["max_tris"]
    for attempt in range(40):
        pieces = list(rest)
        for _ in range(3):
            cuts = strips_for(pieces, max_tris)
            # a geometry reaching past the strips next to its own is cut by triangle centroids
            nxt = []
            for g in pieces:
                s = strip_of(g.centre[axis], cuts)
                a, b = strip_of(g.lo[axis], cuts), strip_of(g.hi[axis], cuts)
                if a < s - 1 or b > s + 1:
                    nxt += split_geo(g, lambda p, cuts=cuts: strip_of(p[axis], cuts), ids)
                else:
                    nxt.append(g)
            if len(nxt) == len(pieces):
                break
            pieces = nxt
        n = len(cuts) + 1
        quads = [[] for _ in range(n)]
        for g in pieces:
            quads[strip_of(g.centre[axis], cuts)].append(g)
        tris = [sum(g.tris for g in q) for q in quads]
        byts = [sum(g.nbytes for g in q) for q in quads]
        act_t = [sum(tris[max(0, i - 2):i + 3]) + hub_tris for i in range(n)]
        act_b = [sum(byts[max(0, i - 2):i + 3]) + hub_bytes for i in range(n)]
        if max(act_t) <= C["active_tris"] and max(act_b) <= C["active_bytes"]:
            break
        max_tris = int(max_tris * 0.85)
        if max_tris < 200:
            break
    p = Plan()
    p.axis, p.lo, p.hi, p.cuts, p.hub, p.quads = axis, lo, hi, cuts, hub, quads
    p.n = n
    # regions: slabs, grown by the overlap margin; the outer ones reach far past the world
    ylo, yhi = lo[1] - C["below"], hi[1] + C["above"]
    edges = [lo[axis] - FAR] + list(cuts) + [hi[axis] + FAR]
    other = 2 if axis == 0 else 0
    p.boxes = []
    for i in range(n):
        a, b = edges[i], edges[i + 1]
        w = (min(b, hi[axis]) - max(a, lo[axis]))
        m = max(10.0, C["overlap"] * max(w, 0.0))
        box_lo, box_hi = np.zeros(3), np.zeros(3)
        box_lo[axis], box_hi[axis] = a - (m if i else 0), b + (m if i < n - 1 else 0)
        box_lo[other], box_hi[other] = lo[other] - FAR, hi[other] + FAR
        box_lo[1], box_hi[1] = ylo, yhi
        p.boxes.append((box_lo, box_hi))
    p.neighbors = [[j for j in (i - 1, i + 1, i - 2, i + 2) if 0 <= j < n] for i in range(n)]
    # how far each quadrant's drawn set reaches along the axis (the strips 2 away)
    p.reach = []
    for i in range(n):
        # from a wall of strip i to the far wall of the strips two away on the other side
        r = []
        if i - 2 >= 0:
            r.append(edges[i + 1] - edges[i - 1])
        if i + 2 < n:
            r.append(edges[i + 2] - edges[i])
        p.reach.append(min(r) if r else math.inf)
    p.stats = [{"geoms": len(q), "tris": sum(g.tris for g in q), "verts": sum(g.verts for g in q),
                "groups": sum(g.groups for g in q), "textures": len(set().union(*[g.textures for g in q])) if q else 0,
                "bytes": pack_size(q)} for q in quads]
    p.hub_stats = {"geoms": len(hub), "tris": hub_tris, "verts": sum(g.verts for g in hub),
                   "bytes": pack_size(hub)}
    p.active = [{"tris": sum(p.stats[j]["tris"] for j in [i] + p.neighbors[i]) + hub_tris,
                 "verts": sum(p.stats[j]["verts"] for j in [i] + p.neighbors[i]) + p.hub_stats["verts"],
                 "bytes": sum(p.stats[j]["bytes"] for j in [i] + p.neighbors[i]) + p.hub_stats["bytes"]}
                for i in range(n)]
    return p


def pack_size(geos):
    """A quadrant pack's bytes as written (header 0x54, tables, data)."""
    if not geos:
        return 0
    data = a32(0x20 + 12 * len(geos))
    ptrs = 1
    tex = sh = 0
    for g in geos:
        b, pt, ts, _ = g.encoded()
        data = a32(data) + len(b)
        ptrs += len(pt)
        tex += len(ts)
        sh += sum(1 for lv in g.g["levels"] for gr in lv["groups"] if gr.get("shader") is not None)
    return a32(0x54 + 8 * (tex + sh + len(geos)) + 4 * ptrs) + data


def region_of(plan, pos):
    """The first region in table order holding pos (the game's rule), else None (the hub)."""
    for i, (lo, hi) in enumerate(plan.boxes):
        if all(lo[k] < pos[k] < hi[k] for k in range(3)):
            return i
    return None


def print_plan(pj, p, out=print):
    ax = "xyz"[p.axis]
    out(f"{pj.set}: {p.n} quadrants + hub, strips along {ax} "
        f"({p.lo[p.axis]:.0f} .. {p.hi[p.axis]:.0f}), bounds {np.round(p.lo).tolist()} .. {np.round(p.hi).tolist()}")
    hs = p.hub_stats
    out(f"  hub: {hs['geoms']} geometries, {hs['tris']} triangles, {hs['bytes']} bytes")
    out("   q  geoms   tris  verts groups tex   bytes | active tris   bytes | neighbours  reach")
    for i, s in enumerate(p.stats):
        a = p.active[i]
        r = "-" if math.isinf(p.reach[i]) else f"{p.reach[i]:.0f}"
        out(f"  {i + 1:2d} {s['geoms']:6d} {s['tris']:6d} {s['verts']:6d} {s['groups']:6d} {s['textures']:3d} "
            f"{s['bytes']:7d} | {a['tris']:11d} {a['bytes']:7d} | {','.join(str(j + 1) for j in p.neighbors[i]):10s} {r}")


def plan_json(pj, p):
    return {"set": pj.set, "axis": "xyz"[p.axis], "cuts": [float(c) for c in p.cuts],
            "hub": p.hub_stats,
            "quadrants": [{"index": i + 1, "box": [p.boxes[i][0].tolist(), p.boxes[i][1].tolist()],
                           "neighbors": [j + 1 for j in p.neighbors[i]], "geometries": [g.id for g in p.quads[i]],
                           **p.stats[i], "active": p.active[i]} for i in range(p.n)]}


# ------------------------------------------------------------------------------------------
# writing: hwvx-x extract folders

def write_section(folder, name, b):
    (folder / name).write_bytes(b)


ABOUT = {"texture_relocs.bin": "data offset + texture index (shared pack << 16)",
         "shader_relocs.bin": "data offset + shader index",
         "ref_relocs.bin": "pointer slot + target (shared pack << 16 | index)",
         "pointers.bin": "offsets of pointer slots in data",
         "data.bin": "the pack's objects (offsets in place of pointers)"}


def write_quad_folder(folder, geos):
    """A quadrant pack: header (word 0: the geometry table), the table (id, 0, geometry; the
    third word a ref relocation), the geometries (32-aligned)."""
    folder.mkdir(parents=True, exist_ok=True)
    n = len(geos)
    d = bytearray(a32(0x20 + 12 * n))
    struct.pack_into(E + "I", d, 0, 0x20)
    ptrs, tex, sh, refs = [0], [], [], []
    for k, g in enumerate(geos):
        b, pt, ts, names = g.encoded()
        while len(d) % 32:
            d.append(0)
        at = len(d)
        d += b
        for slot, target in sorted(pt.items()):
            struct.pack_into(E + "I", d, at + slot, at + target)
            ptrs.append(at + slot)
        for slot, (li, gk) in sorted(ts.items()):
            v = tex_of(g.g["levels"][li]["groups"][gk])
            tex.append((at + slot, v))
        for li, lv in enumerate(g.g["levels"]):
            for gk, gr in enumerate(lv["groups"]):
                if gr.get("shader") is not None:
                    sh.append((at + names[f"level/{li}/group/{gk}"], gr["shader"]))
        e = 0x20 + 12 * k
        struct.pack_into(E + "III", d, e, g.id, 0, at)
        refs.append((e + 8, k))
    secs = [("texture_relocs.bin", b"".join(struct.pack(E + "II", *x) for x in tex), len(tex))]
    if sh:
        secs.append(("shader_relocs.bin", b"".join(struct.pack(E + "II", *x) for x in sh), len(sh)))
    secs += [("ref_relocs.bin", b"".join(struct.pack(E + "II", *x) for x in refs), len(refs)),
             ("pointers.bin", b"".join(struct.pack(E + "I", x) for x in ptrs), len(ptrs)),
             ("data.bin", bytes(d), len(d))]
    for name, b, _ in secs:
        write_section(folder, name, b)
    pj = {"header": {"layout": "quad", "refs": [{"name": "HW_share", "id": 1}, {"name": "", "id": 0}]},
          "sections": [{"file": nm, "bytes": len(b), "records": r, "about": ABOUT[nm]} for nm, b, r in secs]}
    (folder / "pack.json").write_text(json.dumps(pj, indent=2) + "\n")
    return len(d)


def write_manifest(folder, source_file, entries):
    m = {"tool": "hwvx-xfile 0.1.0", "manifest_version": 1, "source_file": source_file, "platform": "gc",
         "archive_version": VERSION, "entries": entries}
    (folder / "manifest.json").write_text(json.dumps(m, indent=2) + "\n")


def extract_base(archive, out):
    """hwvx-x extract of a base archive (the player's data) into out (a build folder)."""
    src = Path(WL.sources()["gc"]) / archive_file(archive)
    if not src.is_file():
        src = pj_find(archive)
    if out.exists():
        shutil.rmtree(out)
    subprocess.run([hwvx_x(), "extract", str(src), str(out)], check=True, stdout=subprocess.DEVNULL)
    return out


def pj_find(archive):
    rel, _ = release_data("gc")
    stem = archive.replace("\\", "/").split("/")[-1].lower()
    if stem not in rel.files:
        raise ChunkError(f"no archive {archive}")
    return rel.files[stem]


class Split:
    """An extracted pack folder's sections, editable."""

    def __init__(self, folder):
        self.dir = Path(folder)
        self.json = json.loads((self.dir / "pack.json").read_text())
        self.sec = {s["file"]: (self.dir / s["file"]).read_bytes() for s in self.json["sections"]}

    def words(self, name):
        b = self.sec.get(name, b"")
        return list(struct.unpack(E + "%dI" % (len(b) // 4), b))

    def pairs(self, name):
        w = self.words(name)
        return list(zip(w[::2], w[1::2]))

    def save(self, folder=None):
        folder = Path(folder or self.dir)
        folder.mkdir(parents=True, exist_ok=True)
        for s in self.json["sections"]:
            b = self.sec[s["file"]]
            (folder / s["file"]).write_bytes(b)
            s["bytes"] = len(b)
            unit = 8 if s["file"].endswith("relocs.bin") else 4 if s["file"].endswith("pointers.bin") else 1
            s["records"] = len(b) // unit
        self.json.pop("padding", None)
        (folder / "pack.json").write_text(json.dumps(self.json, indent=2) + "\n")


class Blob:
    """data.bin with its pointer list, for appending."""

    def __init__(self, data, ptrs):
        self.d = bytearray(data)
        self.ptrs = list(ptrs)
        self.pset = set(ptrs)

    def align(self, a):
        while len(self.d) % a:
            self.d.append(0)

    def alloc(self, n, a=16):
        self.align(a)
        o = len(self.d)
        self.d += bytes(n)
        return o

    def put(self, o, fmt, *v):
        struct.pack_into(E + fmt, self.d, o, *v)

    def u32(self, o):
        return struct.unpack_from(E + "I", self.d, o)[0]

    def setp(self, slot, target):
        self.put(slot, "I", target)
        if slot not in self.pset:
            self.ptrs.append(slot)
            self.pset.add(slot)

    def string(self, s):
        o = self.alloc(len(s) + 1, 4)
        self.d[o:o + len(s)] = s.encode("latin-1")
        return o


# ------------------------------------------------------------------------------------------
# collision pack

def clip_to_box(v, lo, hi):
    for axis in range(3):
        for sign, lim in ((1.0, hi[axis]), (-1.0, lo[axis])):
            pl = [0.0, 0.0, 0.0, -sign * lim]
            pl[axis] = sign
            f, b, side = BB.split_poly(v, pl, 1e-6)
            if side == "front":
                return None
            if side == "span":
                v = b
                if v is None:
                    return None
    return v


def seg_hits_box(a, b, lo, hi):
    t0, t1 = 0.0, 1.0
    for k in range(3):
        d = b[k] - a[k]
        if abs(d) < 1e-12:
            if a[k] < lo[k] or a[k] > hi[k]:
                return False
            continue
        u0, u1 = (lo[k] - a[k]) / d, (hi[k] - a[k]) / d
        if u0 > u1:
            u0, u1 = u1, u0
        t0, t1 = max(t0, u0), min(t1, u1)
        if t0 > t1:
            return False
    return True


def build_collision(col, C, grid=None, log=print):
    """(grid dict, cells [(lo, hi, tree, edge ids)]) for a collision mesh. grid: (lo, n) to use
    instead of squares of about C["cell_xz"] x C["cell_y"] over the mesh's bounds."""
    if grid is not None:
        lo, hi, n = np.array(grid[0], float), np.array(grid[1], float), list(grid[2])
        span = hi - lo
    else:
        lo = col.lo - 1.0
        hi = col.hi + 1.0
        span = hi - lo
        n = [max(1, math.ceil(span[0] / C["cell_xz"])), max(1, math.ceil(span[1] / C["cell_y"])),
             max(1, math.ceil(span[2] / C["cell_xz"]))]
    size = span / np.array(n)
    nx, ny, nz = n
    per = {}
    for pi, ((plane, v, mat), own) in enumerate(zip(col.polys, col.owners)):
        a = np.floor((v.min(0) - lo) / size).astype(int)
        b = np.floor((v.max(0) - lo) / size).astype(int)
        a = np.clip(a, 0, np.array(n) - 1)
        b = np.clip(b, 0, np.array(n) - 1)
        for x in range(a[0], b[0] + 1):
            for y in range(a[1], b[1] + 1):
                for z in range(a[2], b[2] + 1):
                    clo = lo + size * np.array([x, y, z])
                    piece = clip_to_box(v, clo, clo + size)
                    if piece is not None and len(piece) >= 3 and _thick(piece) > 1e-2 \
                            and _keep_on_wall(piece, plane, clo, clo + size, own):
                        pl = BB.plane_of(piece)
                        if pl is not None:
                            per.setdefault((x, y, z), []).append((plane, piece, mat, own))
    cells, index = [], [0] * (nx * ny * nz)
    edges = [(np.asarray(a, float), np.asarray(b, float), m) for a, b, m in col.creases]
    oracle = Sources(col)
    squares = set(per)
    for key in oracle.boxes:
        a = np.clip(np.floor((np.array(key[0]) - lo) / size).astype(int), 0, np.array(n) - 1)
        b = np.clip(np.ceil((np.array(key[1]) - lo) / size).astype(int) - 1, 0, np.array(n) - 1)
        squares |= {(x, y, z) for x in range(a[0], b[0] + 1) for y in range(a[1], b[1] + 1)
                    for z in range(a[2], b[2] + 1)}
    for (x, y, z) in sorted(squares, key=lambda k: (k[1], k[2], k[0])):
        clo = lo + size * np.array([x, y, z])
        chi = clo + size
        tree = cell_tree(per.get((x, y, z), []), clo, chi, oracle)
        if not tree:
            continue
        eids = [i for i, (a, b, m) in enumerate(edges) if seg_hits_box(a, b, clo - EDGE_REACH, chi + EDGE_REACH)]
        cells.append((clo, chi, tree, eids))
        index[x + nx * (z + nz * y)] = len(cells)
    return {"lo": lo, "hi": hi, "n": n, "size": size, "index": index, "edges": edges}, cells


class Sources:
    """The source boxes of a collision mesh cut to boxes (an extracted retail world: one per
    retail cell) and, per box, whether a point is solid (its pieces' own tree)."""

    def __init__(self, col):
        self.pieces = {}
        for (plane, v, mat), own in zip(col.polys, col.owners):
            if own is not None:
                self.pieces.setdefault((tuple(own[0]), tuple(own[1])), []).append((plane, v, mat))
        self.boxes = list(self.pieces)
        self._trees = {}

    def overlapping(self, lo, hi, eps=1e-3):
        return [k for k in self.boxes if np.all(np.minimum(np.array(k[1]), hi) - np.maximum(np.array(k[0]), lo) > eps)]

    def solid(self, key, p):
        if key not in self._trees:
            self._trees[key] = BB.build(self.pieces[key])
        t = self._trees[key]
        return bool(t) and WL.inside(t, p, strict=False)


def cell_tree(pieces, lo, hi, sources=None):
    """A cell's terrain tree. Pieces of one source (a mesh, or one cell of an extracted retail
    world) build one tree, in the source's order (the first polygon is the splitter). Pieces of
    several sources are not mixed: a source's solid is closed only inside its own box, and a
    splitting plane of one leaves the other's inside unbounded (it reads as empty), so the cell is
    first cut along the sources' box walls and each part gets its source's tree (or, with no
    pieces in it, is all solid or all empty as that source says)."""
    if not sources or not sources.boxes or any(own is None for *_, own in pieces):
        # a mesh of its own (not cut to source boxes): faces lying on another's plane facing
        # the other way are inside faces (a wall's foot on the ground)
        return BB.build([(pl, v, m) for pl, v, m, _ in pieces],
                        opposite="drop" if all(own is None for *_, own in pieces) else "back")
    lo, hi = np.array(lo, float), np.array(hi, float)
    groups = {k: [] for k in sources.overlapping(lo, hi)}
    for plane, v, mat, own in pieces:
        groups.setdefault((tuple(own[0]), tuple(own[1])), []).append((plane, v, mat))
    return _kd(groups, lo, hi, sources)


def _kd(groups, lo, hi, sources, eps=1e-3):
    live = {k: ps for k, ps in groups.items()
            if np.all(np.minimum(np.array(k[1]), hi) - np.maximum(np.array(k[0]), lo) > eps)}
    if not live:
        return []
    if len(live) == 1:
        key, ps = next(iter(live.items()))
        if ps:
            return BB.build(ps)
        if sources.solid(key, (lo + hi) / 2):
            return [((1.0, 0.0, 0.0, -float(hi[0]) - 1.0), None, None, 0)]     # all behind: solid
        return []
    # a wall of one source's box strictly inside the part
    for k in live:
        for axis in range(3):
            for w in (k[0][axis], k[1][axis]):
                if lo[axis] + eps < w < hi[axis] - eps:
                    plane = [0.0, 0.0, 0.0, -float(w)]
                    plane[axis] = 1.0
                    sides = []
                    for keep_front in (True, False):
                        sub = {}
                        for key, ps in live.items():
                            mid = (key[0][axis] + key[1][axis]) / 2
                            out = []
                            for pl, v, m in ps:
                                f, b, side = BB.split_poly(v, plane, 1e-6)
                                part = f if keep_front else b
                                if side == "on":    # on the wall: on its own box's side
                                    part = v if (mid > w) == keep_front else None
                                if part is not None and len(part) >= 3 and _thick(part) > 1e-2:
                                    out.append((pl, part, m))
                            sub[key] = out
                        slo, shi = lo.copy(), hi.copy()
                        if keep_front:
                            slo[axis] = w
                        else:
                            shi[axis] = w
                        sides.append(_kd(sub, slo, shi, sources, eps))
                    if not sides[0] and not sides[1]:
                        return []
                    return _join(BB.f32_plane(plane), sides[0], sides[1])
    # no wall inside (sources overlapping): one tree for all
    return BB.build([p for ps in live.values() for p in ps])


def _join(plane, front, back):
    """A node over two pre-order trees. An empty back side needs a node of its own (a missing
    back child is solid): the plane flipped, nothing in front of it."""
    if not back:
        back = [(tuple(-x for x in plane), None, None, 0)]
    out = [(plane, 1 if front else None, 1 + len(front), 0)]
    for t, base in ((front, 1), (back, 1 + len(front))):
        for pl, f, b, m in t:
            out.append((pl, None if f is None else f + base, None if b is None else b + base, m))
    return out


def ray_hits(col, s, e):
    """The game's ray against the collision grid (CollisionTerrain.c RayHitsBSP2 / RayHitsBSP, in
    float32): (t, plane, point) of the first hit over the cells the segment's box overlaps, hits
    outside a cell's box (+-0.1) dropped; None for no hit. col: a world_lib.Collision."""
    s, e = np.asarray(s, float), np.asarray(e, float)
    lo, sz, (nx, ny, nz) = np.array(col.min), np.array(col.cell_size), col.n
    a = np.floor((np.minimum(s, e) - lo) / sz).astype(int)
    b = np.floor((np.maximum(s, e) - lo) / sz).astype(int)
    best = None
    for x in range(max(a[0], 0), min(b[0], nx - 1) + 1):
        for y in range(max(a[1], 0), min(b[1], ny - 1) + 1):
            for z in range(max(a[2], 0), min(b[2], nz - 1) + 1):
                ci = col.cell_index[x + nx * (z + nz * y)]
                tree = col.cell_tree(ci) if ci else None
                if not tree:
                    continue
                c = col.cell(ci)
                r = _ray2(tree, s, e, 0)
                if r and all(c["min"][k] - 0.1 <= r[2][k] <= c["max"][k] + 0.1 for k in range(3)):
                    if best is None or r[0] < best[0]:
                        best = r
    return best


def _ray2(tree, s, e, k):
    f = np.float32
    pl, fr, bk, _ = tree[k]

    def dist(p):
        return f(pl[0]) * f(p[0]) + f(pl[1]) * f(p[1]) + f(pl[2]) * f(p[2]) + f(pl[3])
    d0, d1 = dist(s), dist(e)
    if d0 == 0 and d1 == 0:
        return None if fr is None else _ray2(tree, s, e, fr)
    if (d0 > 0 > d1) or (d0 < 0 < d1):
        q = float(d0 / (d0 - d1))
        mid = s + (e - s) * q
        near, far = (fr, bk) if d0 > 0 else (bk, fr)
        if near is not None:
            r = _ray2(tree, s, mid, near)
            if r:
                return (r[0] * q, r[1], r[2])
        elif d0 < 0:
            return (q, pl, mid)              # starting behind a solid leaf
        if far is not None:
            r = _ray2(tree, mid, e, far)
            if r:
                t = r[0] * (1 - q) + q
                return (q, pl, mid) if t == q else (t, r[1], r[2])
            return None
        return (q, pl, mid) if d0 > 0 else None
    if d0 > 0 or d1 > 0:
        return None if fr is None else _ray2(tree, s, e, fr)
    return _ray2(tree, s, e, bk) if bk is not None else (0.0, pl, s)


def _keep_on_wall(v, plane, lo, hi, own, eps=1e-3):
    """Whether a polygon lying in a side of the cell belongs to it. A polygon cut to a source box
    (an extracted world's cells) belongs to the cells that box overlaps; otherwise one facing into
    the cell is a neighbour's (in a solid-leaf tree it would declare the whole cell empty)."""
    on = any(np.all(np.abs(v[:, axis] - lim) < eps) for axis in range(3) for lim in (lo[axis], hi[axis]))
    if not on:
        return True
    if own is not None:
        olo, ohi = np.array(own[0]), np.array(own[1])
        return bool(np.all(np.minimum(ohi, hi) - np.maximum(olo, lo) > eps))
    return not _faces_in(v, plane, lo, hi, eps)


def _faces_in(v, plane, lo, hi, eps=1e-3):
    """A polygon lying in a side of the box and facing into it (a neighbour's surface or cap on
    the shared wall): in a solid-leaf tree it would declare the whole box empty."""
    for axis in range(3):
        for lim, inward in ((lo[axis], 1.0), (hi[axis], -1.0)):
            if np.all(np.abs(v[:, axis] - lim) < eps) and plane[axis] * inward > 0.5:
                return True
    return False


def _thick(v):
    """A polygon's width: twice its area over its longest edge (a sliver cut off at a cell's wall
    is ~0)."""
    e = max(float(np.linalg.norm(v[(i + 1) % len(v)] - v[i])) for i in range(len(v)))
    return 2 * _area(v) / e if e > 0 else 0.0


def _area(v):
    return float(np.linalg.norm(sum(np.cross(v[i], v[(i + 1) % len(v)]) for i in range(len(v))))) / 2


def write_collision(sp, grid, cells):
    """The collision pack: header, edges, cell index, cells, nodes and edge lists, then the base
    pack's materials (and everything they use: effects, sounds, textures) moved behind them."""
    d0 = sp.sec["data.bin"]
    ptrs0 = sp.words("pointers.bin")
    tail = struct.unpack_from(E + "I", d0, 0x5C)[0]
    for name in ("texture_relocs.bin", "shader_relocs.bin", "sound_relocs.bin", "ref_relocs.bin"):
        for slot, _ in sp.pairs(name):
            if slot < tail:
                raise ChunkError(f"base collision pack: a relocation ({name}) before its materials")
    hdr_slots = {0x20, 0x40, 0x4C, 0x54, 0x5C}
    for s in ptrs0:
        if s < tail and s not in hdr_slots and not (0x60 <= s):
            raise ChunkError("base collision pack: a pointer before its materials")
    o = Blob(b"", [])
    o.alloc(0x60)
    edges = grid["edges"]
    if len(edges) > 0xFFFF:
        raise ChunkError(f"{len(edges)} crease edges: the cells' edge indices are u16")
    eo = o.alloc(0x30 * len(edges), 16) if edges else 0
    for i, (a, b, m) in enumerate(edges):
        o.put(eo + 0x30 * i, "4f4f", *a, 0.0, *b, 0.0)
        o.d[eo + 0x30 * i + 0x20:eo + 0x30 * i + 0x24] = b"\xCD" * 4
        o.put(eo + 0x30 * i + 0x24, "I", m)
        o.d[eo + 0x30 * i + 0x28:eo + 0x30 * i + 0x30] = b"\xCD" * 8
    nx, ny, nz = grid["n"]
    ci = o.alloc(4 * nx * ny * nz, 16)
    o.put(ci, "%di" % (nx * ny * nz), *grid["index"])
    co = o.alloc(0x50 * (len(cells) + 1), 32)
    for k, (clo, chi, tree, eids) in enumerate(cells):
        c = co + 0x50 * (k + 1)
        if len(tree) >= 0xFFFF:
            raise ChunkError(f"a collision cell of {len(tree)} nodes (u16 indices): use smaller squares")
        o.put(c, "4f4f", *clo, 0.0, *chi, 0.0)
        no = o.alloc(0x20 * len(tree), 32)
        o.d[no:no + 0x20 * len(tree)] = WL.tbsp_write(tree, E)
        o.setp(c + 0x30, no)
        o.put(c + 0x3C, "I", len(tree))
        o.put(c + 0x40, "I", len(eids))
        if eids:
            lo_ = o.alloc(a32(2 * len(eids), 16), 16)
            o.put(lo_, "%dH" % len(eids), *eids)
            o.d[lo_ + 2 * len(eids):a32(lo_ + 2 * len(eids), 16)] = b"\xCD" * (a32(lo_ + 2 * len(eids), 16) - lo_ - 2 * len(eids))
            o.setp(c + 0x44, lo_)
    # the base's materials and what they use
    o.align(32)
    delta = len(o.d) - tail
    o.d += d0[tail:]
    for s in ptrs0:
        if s >= tail:
            v = struct.unpack_from(E + "I", d0, s)[0]
            if v < tail:
                raise ChunkError(f"base collision pack: a material pointer at 0x{s:X} into the grid")
            o.setp(s + delta, v + delta)
    # a local ref relocation (pack 0) is a pointer the loader fixes up too: its slot holds an
    # offset in data
    for slot, v in sp.pairs("ref_relocs.bin"):
        if not v >> 16:
            o.put(slot + delta, "I", struct.unpack_from(E + "I", d0, slot)[0] + delta)
    mat_at = struct.unpack_from(E + "I", d0, 0x5C)[0] + delta
    lo, hi = grid["lo"], grid["hi"]
    o.put(0, "4f4f", *lo, 0.0, *hi, 0.0)
    o.setp(0x20, co)
    o.put(0x24, "I", 0xD)
    o.put(0x28, "3f", *grid["size"])
    o.put(0x34, "3i", nx, ny, nz)
    o.setp(0x40, ci)
    o.put(0x44, "I", len(cells) + 1)
    o.put(0x48, "II", 0, 0)
    o.put(0x50, "I", len(edges))
    if edges:
        o.setp(0x54, eo)
    o.put(0x58, "I", 1)
    o.setp(0x5C, mat_at)
    sp.sec["data.bin"] = bytes(o.d)
    sp.sec["pointers.bin"] = b"".join(struct.pack(E + "I", s) for s in o.ptrs)
    for name in ("texture_relocs.bin", "shader_relocs.bin", "sound_relocs.bin", "ref_relocs.bin"):
        if name in sp.sec:
            sp.sec[name] = b"".join(struct.pack(E + "II", s + delta, v) for s, v in sp.pairs(name))
    return len(o.d)


# ------------------------------------------------------------------------------------------
# world pack

def patch_world(sp, pj, plan, quad_ids, pack_ids, names, log=print):
    """The base world pack with a new quadrant table appended: regions, neighbour ids, contents
    (the base's objects moved to the quadrant holding them; every pointer to one follows)."""
    w = pj.base_world
    o = Blob(sp.sec["data.bin"], sp.words("pointers.bin"))
    nq = plan.n + 1
    # relocations by slot: a local ref (pack 0) is a pointer too (its slot holds an offset)
    relocs = {name: dict(sp.pairs(name)) for name in ("texture_relocs.bin", "shader_relocs.bin",
                                                       "sound_relocs.bin", "ref_relocs.bin") if name in sp.sec}
    added = {name: [] for name in relocs}
    # objects: the base's, per kind, as (old entry offset, position, base hub?)
    kinds = ((0x04, 0x08), (0x0C, 0x10), (0x14, 0x18))
    objs = [[[] for _ in kinds] for _ in range(nq)]
    old_entries = []
    keep = pj.cfg.get("objects", "base") == "base"
    for q in w.quads:
        c = o.u32(q.off + 0x30)
        if not c or not keep:
            continue
        for k, (cnt, arr) in enumerate(kinds):
            n_, a_ = o.u32(c + cnt), o.u32(c + arr)
            for i in range(n_):
                e = a_ + 0x40 * i
                pos = struct.unpack_from(E + "3f", o.d, e)
                r = None if q.bsp_off == 0 else region_of(plan, pos)
                objs[0 if r is None else r + 1][k].append(e)
                old_entries.append(e)
    # the quadrant table
    qt = o.alloc(QUAD_LEN * nq, 32)
    moved = {}
    for qi in range(nq):
        r = qt + QUAD_LEN * qi
        o.put(r, "I", quad_ids[qi])
        o.put(r + 0x0C, "I", pack_ids[qi] >> 16)
        o.put(r + 0x38, "I", pack_ids[qi])
        if qi:
            lo, hi = plan.boxes[qi - 1]
            planes = [(1, 0, 0, -hi[0]), (-1, 0, 0, lo[0]), (0, 1, 0, -hi[1]), (0, -1, 0, lo[1]),
                      (0, 0, 1, -hi[2]), (0, 0, -1, lo[2])]
            tree = BB.convex_region(planes)
            b = WL.bspn0_write(tree, E)
            bo = o.alloc(len(b), 32)
            o.d[bo:bo + len(b)] = b
            o.put(r + 0x18, "I", len(b))
            o.setp(r + 0x1C, bo)
            nb = [quad_ids[j + 1] for j in plan.neighbors[qi - 1]]
            if nb:
                no = o.alloc(4 * len(nb), 16)
                o.put(no, "%dI" % len(nb), *nb)
                o.put(r + 0x20, "I", len(nb))
                o.setp(r + 0x24, no)
        c = o.alloc(0x20, 32)
        o.setp(r + 0x30, c)
        for k, (cnt, arr) in enumerate(kinds):
            lst = objs[qi][k]
            o.put(c + cnt, "I", len(lst))
            if not lst:
                continue
            ao = o.alloc(0x40 * len(lst), 16)
            o.setp(c + arr, ao)
            for i, e in enumerate(lst):
                ne = ao + 0x40 * i
                o.d[ne:ne + 0x40] = o.d[e:e + 0x40]
                moved[e] = ne
                for s in range(e, e + 0x40, 4):
                    if s in o.pset:
                        o.setp(ne + (s - e), o.u32(s))
                    for name, rl in relocs.items():
                        if s in rl:
                            added[name].append((ne + (s - e), rl[s]))
    # every pointer into a moved entry follows it
    starts = sorted(moved)
    n_remap = 0
    local = [sl for sl, v in relocs.get("ref_relocs.bin", {}).items() if not v >> 16]
    local += [sl for sl, v in added.get("ref_relocs.bin", []) if not v >> 16]
    for s in list(o.ptrs) + local:
        v = o.u32(s)
        j = bisect.bisect_right(starts, v) - 1
        if j >= 0 and starts[j] <= v < starts[j] + 0x40 and not (starts[j] <= s < starts[j] + 0x40 and s in moved):
            o.put(s, "I", moved[starts[j]] + v - starts[j])
            n_remap += 1
    o.put(0x20, "I", nq)
    o.setp(0x24, qt)
    for off, s in names.items():
        o.setp(off, o.string(s))
    sp.sec["data.bin"] = bytes(o.d)
    sp.sec["pointers.bin"] = b"".join(struct.pack(E + "I", s) for s in o.ptrs)
    for name, extra in added.items():
        if extra:
            sp.sec[name] += b"".join(struct.pack(E + "II", *x) for x in extra)
    counts = [sum(len(x) for x in objs[q]) for q in range(nq)]
    log(f"  world pack: {nq} quadrants, {len(old_entries)} objects placed (hub {counts[0]}), "
        f"{n_remap} pointers to them redirected, {sum(len(x) for x in added.values())} relocations copied")
    return counts


def move_start(sp, starts):
    """Move the start tiles (TileCar: position at 0, rotation x, y, z in degrees at 0x10), and the
    base's destination-only teleporters (Teleporter 0x70: position at 0x30, numDest 0x50 == 0):
    the respawn point a fall-out-of-the-world teleporter sends the car to, in the base's
    coordinates (world1's sent the arena's car outside the arena, to fall forever)."""
    d = bytearray(sp.sec["data.bin"])
    nt, tb = struct.unpack_from(E + "II", d, 0x30)
    for i in range(nt):
        o = tb + 0x70 * i
        if struct.unpack_from(E + "I", d, o + 0x50)[0] == 0:
            x, y, z, _ = starts[0]
            struct.pack_into(E + "3f", d, o + 0x30, x, y + 10.0, z)
    n, base = struct.unpack_from(E + "II", d, 0x08)
    for i in range(n):
        x, y, z, yaw = starts[i % len(starts)]
        o = base + 0x60 * i
        struct.pack_into(E + "3f", d, o, x, y, z)
        struct.pack_into(E + "3f", d, o + 0x10, 0.0, yaw, 0.0)
    sp.sec["data.bin"] = bytes(d)


# ------------------------------------------------------------------------------------------
# commands

def cmd_extract(a):
    rel, _ = release_data(a.rel)
    a.level = full_level_name(rel, a.level)
    w = rel.world(a.level)
    out = Path(a.project)
    (out / "render").mkdir(parents=True, exist_ok=True)
    texarc = w.tex_name.replace("\\", "/").split("/")[-1].lower()
    n = 0
    for q in w.quads:
        pk = WL.quad_pack(rel, w, q)
        if pk is None:
            continue
        blob = XG.Blob(pk.d, pk.ptrs, rel.rel)
        tex, sh = dict(pk.pairs("texture_relocs")), dict(pk.pairs("shader_relocs"))
        table = pk.u32(0)
        ids = {pk.u32(table + 12 * k + 8): pk.u32(table + 12 * k) for k in range(q.num_geoms)}
        for off in XG.scan(blob):
            g = XG.decode(blob, off, tex)
            gid = ids.get(off)
            at = {v: k for k, v in g["names"].items()}
            gl, b = XG.to_gltf(g, f"geometry_{gid}", f"g{gid}.bin", tex_link=lambda v: tex_link(texarc, v),
                               extras={"geometryId": gid, "retailQuad": q.index, "hub": q.bsp_off == 0})
            for nd in gl["nodes"]:
                if "mesh" not in nd:
                    continue
                li = nd["extras"]["level"]
                for k, pr in enumerate(gl["meshes"][nd["mesh"]]["primitives"]):
                    G = at.get(f"level/{li}/group/{k}")
                    if G in sh:
                        pr["extras"]["shader"] = sh[G]
            XG.write(gl, b, out / "render" / f"g{gid}.gltf")
            n += 1
    print(f"{a.level}: {n} geometries")
    col = rel.collision(w.collision_name)
    gb = GBuf()
    nodes, meshes, order, npoly = [], [], 0, 0
    for i in range(1, col.num_cells):
        c, t = col.cell(i), col.cell_tree(i)
        if not t:
            continue
        # the cell's solid as a closed surface: the tree's boundary inside the cell and the caps
        # where the solid meets the cell's sides. A closed surface builds back to the same solid
        # in any polygon order and any grid (bsp_build.py check: "equivalent"); the open
        # terrain surface alone would depend on the exporter's polygon order, which is lost.
        # One node per cell, its vertices relative to the cell's centre (float32 keeps them
        # within ~1e-5 of their planes; world coordinates would be 5e-4 off at 5,000).
        lo_, hi_ = np.array(c["min"]), np.array(c["max"])
        ctr = (lo_ + hi_) / 2
        # nearly coplanar planes snapped together first: the exporter's slivers (a face's two
        # triangles a hair out of plane) leave thin solid and empty layers whose surfaces, built
        # into another tree, become the plane a ray reports (a floor facing down)
        t, _ = BB.snap_tree(t, ctr)
        polys, _ = BB.boundary_polys(t, lo_, hi_)
        by_mat = {}
        for plane, v, mat in polys + [(pl, v, t[0][3]) for pl, v, _ in BB.box_caps(t, lo_, hi_)]:
            by_mat.setdefault(mat or 0, []).append((order, plane, v))
            order += 1
            npoly += 1
        prims = []
        for mat, vs in sorted(by_mat.items()):
            pts, idx, pid, pls = [], [], [], []
            for k, plane, v in vs:
                base = len(pts)
                pts += list(v - ctr)
                pid += [k] * len(v)
                pls += [plane] * len(v)
                for j in range(1, len(v) - 1):
                    idx += [base, base + j, base + j + 1]
            prims.append({"attributes": {"POSITION": gb.add(np.array(pts, np.float32), 5126, "VEC3", 34962, True),
                                         "_POLY": gb.add(np.array(pid, np.float32).reshape(-1, 1), 5126, "SCALAR", 34962),
                                         "_PLANE": gb.add(np.array(pls, np.float32), 5126, "VEC4", 34962)},
                          "indices": gb.add(np.array(idx, np.uint32), 5125, "SCALAR", 34963), "mode": 4,
                          "extras": {"material": mat}})
        meshes.append({"name": f"cell{i}", "primitives": prims})
        nodes.append({"name": f"cell{i}", "mesh": len(meshes) - 1, "translation": [float(x) for x in ctr],
                      "extras": {"sourceBox": [list(map(float, lo_)), list(map(float, hi_))]}})
    em = {}
    for i in range(col.num_edges):
        a_, b_, m = col.edge(i)
        em.setdefault(m, []).append((a_, b_))
    prims = []
    for m, es in sorted(em.items()):
        pts = np.array([p for e in es for p in e], np.float32)
        prims.append({"attributes": {"POSITION": gb.add(pts, 5126, "VEC3", 34962, True)},
                      "indices": gb.add(np.arange(len(pts), dtype=np.uint32), 5125, "SCALAR", 34963), "mode": 1,
                      "extras": {"material": m, "creases": True}})
    meshes.append({"name": "creases", "primitives": prims})
    nodes.append({"name": "creases", "mesh": len(meshes) - 1})
    gl = {"asset": {"version": "2.0", "generator": "HWVX-SDL tools/xdl_chunk.py"}, "scene": 0,
          "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes,
          "bufferViews": gb.views, "accessors": gb.acc}
    gl_write(out / "collision.gltf", gl, bytes(gb.b))
    print(f"collision: {npoly} polygons in {len(nodes) - 1} cells, {col.num_edges} crease edges")
    setname = a.set or ("c" + w.name.replace("\\", "/").split("/")[-1].replace("_geo", ""))
    level = a.level_out or (a.level.rsplit("\\", 1)[0] + "\\c" + a.level.rsplit("\\", 1)[-1]
                            if "\\" in a.level else a.level)
    cfg = {"format_version": 1, "release": "gc", "set": setname, "level": level,
           "base": a.level, "textures": w.tex_name, "render": "render", "collision": "collision.gltf",
           "objects": "base", "chunk": {}}
    (out / "world.jsonc").write_text("// xdl_chunk.py project: " + a.level + " re-chunked\n" + json.dumps(cfg, indent=2) + "\n")
    print(f"project: {out / 'world.jsonc'} (set {setname}, level {level})")


def full_level_name(rel, name):
    """A level's archive path as the progression scripts name it (worlds_\\sp_\\joyride_\\world1)."""
    if "\\" in name:
        return name
    p = rel.files.get(name.lower())
    if p is None:
        raise ChunkError(f"no level {name}")
    r = p.relative_to(rel.folder).with_suffix("")
    return "\\".join(r.parts)


def cmd_plan(a):
    pj = Project(a.project)
    p = make_plan(pj)
    print_plan(pj, p)
    (pj.dir / "plan.json").write_text(json.dumps(plan_json(pj, p), indent=1) + "\n")


def build_col(pj, col_dir):
    extract_base(pj.base_world.collision_name, col_dir)
    g = None
    if pj.chunk.get("grid") == "base":
        bc = pj.rel.collision(pj.base_world.collision_name)
        g = (bc.min, bc.max, bc.n)
    grid, cells = build_collision(pj.collision(), pj.chunk, g)
    m = json.loads((col_dir / "manifest.json").read_text())
    sp = Split(col_dir / m["entries"][0]["path"])
    nbytes = write_collision(sp, grid, cells)
    sp.save()
    m["source_file"] = f"{pj.set}_col.xgc"
    (col_dir / "manifest.json").write_text(json.dumps(m, indent=2) + "\n")
    nodes = [len(c[2]) for c in cells]
    print(f"  collision: grid {grid['n']} squares {np.round(grid['size'], 1).tolist()}, {len(cells)} cells, "
          f"nodes per cell {min(nodes)}/{int(np.median(nodes))}/{max(nodes)}, depth max "
          f"{max(WL.depth(c[2]) for c in cells)}, {len(grid['edges'])} edges, {nbytes} bytes")


def cmd_build(a):
    pj = Project(a.project)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    p = make_plan(pj)
    print_plan(pj, p)
    (pj.dir / "plan.json").write_text(json.dumps(plan_json(pj, p), indent=1) + "\n")
    # ids: quadrants keep fresh ids past the base's; pack ids 1..n, the hub 0xFFFF
    ids = pj._ids
    hub_id = pj.base_world.quads[0].id
    quad_ids = [hub_id] + [next(ids) for _ in range(p.n)]
    quads = [p.hub] + p.quads
    pack_ids = [(len(quads[0]) << 16) | HUB_PACK] + [(len(quads[i]) << 16) | i for i in range(1, p.n + 1)]
    geo_dir = out / f"{pj.set}_geo"
    if geo_dir.exists():
        shutil.rmtree(geo_dir)
    entries = []
    for qi, gs in enumerate(quads):
        if not gs:
            continue
        name = f"{qi:02d}_geometry_0_q{pack_ids[qi] & 0xFFFF}_g{len(gs)}"
        write_quad_folder(geo_dir / name, gs)
        entries.append({"type": "geometry", "id": pj.base_world.id, "sub_id": pack_ids[qi], "version": VERSION,
                        "path": name, "layout": "split"})
    write_manifest(geo_dir, f"{pj.set}_geo.xgc", entries)
    # collision ("collision": "base" keeps the base level's pack)
    col_name = f"worlds_\\col_\\{pj.set}_col"
    col_dir = out / f"{pj.set}_col"
    if col_dir.exists():
        shutil.rmtree(col_dir)
    if pj.cfg.get("collision") == "base" or a.base_collision:
        col_name = pj.base_world.collision_name
    else:
        build_col(pj, col_dir)
    # the level's world pack
    lvl = pj.level.replace("\\", "/").split("/")[-1]
    lvl_dir = out / lvl
    extract_base(full_level_name(pj.rel, pj.base), lvl_dir)
    m = json.loads((lvl_dir / "manifest.json").read_text())
    wi = [i for i, e in enumerate(m["entries"]) if e["type"] == "world"][0]
    sp = Split(lvl_dir / m["entries"][wi]["path"])
    names = {0x8C: f"worlds_\\geo_\\{pj.set}_geo", 0x7C: col_name}
    if pj.textures != pj.base_world.tex_name:
        names[0x84] = pj.textures
    patch_world(sp, pj, p, quad_ids, pack_ids, names)
    if pj.cfg.get("start"):
        move_start(sp, pj.cfg["start"])
    sp.save()
    m["source_file"] = f"{lvl}.xgc"
    (lvl_dir / "manifest.json").write_text(json.dumps(m, indent=2) + "\n")
    (out / "built.json").write_text(json.dumps({
        "archives": {f"worlds_\\geo_\\{pj.set}_geo": geo_dir.name, pj.level: lvl_dir.name,
                     **({col_name: col_dir.name} if col_dir.exists() else {})}},
        indent=1) + "\n")
    track = pj.cfg.get("track")
    if track:
        # an ids.lock registering the level as a track (§10.5.1): the game takes it with
        # HWVX_IDS_LOCK=<a copy> (it writes the numbers back), or a mod brings the same entry
        entry = {"alias": track.get("alias", "base://mcp/track/0"), "world": pj.level,
                 "name": track.get("name", pj.set), "unlocked": True}
        lock = {"format_version": 1, "tracks": {track["id"]: entry}}
        (out / "ids.lock").write_text(json.dumps(lock, indent=2) + "\n")
    print(f"built: {out}")


def link_tree(src, dst, want):
    """dst mirrors src by symlinks; the directories on the way to `want` (relative file paths)
    are real so the new files can be put there."""
    dirs = set()
    for w in want:
        parts = Path(w).parts[:-1]
        for k in range(1, len(parts) + 1):
            dirs.add(Path(*parts[:k]))

    def walk(rel):
        (dst / rel).mkdir(parents=True, exist_ok=True)
        for p in sorted((src / rel).iterdir()):
            r = rel / p.name
            if p.is_dir() and _ci(r, dirs):
                walk(_ci(r, dirs))
            elif not (dst / r).exists():
                os.symlink(p, dst / r)
    walk(Path("."))


def _ci(r, dirs):
    for d in dirs:
        if str(d).lower() == str(r).lower().lstrip("./") or str(d).lower() == str(r).lower():
            return Path(str(r))
    return None


def cmd_stage(a):
    out, data, staged = Path(a.out), Path(a.data), Path(a.staged)
    built = json.loads((out / "built.json").read_text())["archives"]
    if staged.exists():
        shutil.rmtree(staged)
    want = []
    for name in built:
        f = archive_file(name)
        # the data folder's own case for the directories (worlds_/sp_/joyride_)
        want.append(f)
    link_tree(data, staged, want)
    for name, folder in built.items():
        target = staged / archive_file(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_symlink() or target.exists():
            target.unlink()
        subprocess.run([hwvx_x(), "pack", "--force", str(out / folder), str(target)], check=True,
                       stdout=subprocess.DEVNULL)
        print(f"  {target.relative_to(staged)}: {target.stat().st_size} bytes")
    print(f"staged: {staged}")


def cmd_stats(a):
    pj = Project(a.project)
    p = make_plan(pj)
    print_plan(pj, p)
    if a.retail:
        rel = pj.rel
        w = rel.world(a.retail)
        print(f"retail {a.retail}: {len(w.quads)} quadrants (with hub)")
        rows = []
        for q in w.quads:
            pk = WL.quad_pack(rel, w, q)
            if pk is None:
                rows.append((q.index, 0, 0, 0, 0))
                continue
            gs = WL.quad_geometry(pk)
            rows.append((q.index, len(gs), sum(g["levels"][0]["tris"] for g in gs if g["levels"]),
                         sum(g["levels"][0]["verts"] for g in gs if g["levels"]), pk.size))
        for r in rows:
            nb = w.quads[r[0]].neighbors
            act = r[2] + sum(rows[j][2] for j in nb if j is not None) + (rows[0][2] if r[0] else 0)
            print(f"  {r[0]:2d} {r[1]:4d} geoms {r[2]:6d} tris {r[3]:6d} verts {r[4]:7d} bytes | active {act:6d} tris"
                  f" | neighbours {nb}")


# ------------------------------------------------------------------------------------------
# a generated test arena

def cmd_arena(a):
    """A flat arena with ramps and walls, as a plain glTF (render) and a collision glTF. All
    geometry is generated here; the textures are links to the base's world texture pack."""
    out = Path(a.project)
    (out / "render").mkdir(parents=True, exist_ok=True)
    rel, _ = release_data("gc")
    base = full_level_name(rel, a.base)
    w = rel.world(base)
    texarc = w.tex_name.replace("\\", "/").split("/")[-1].lower()
    size, tile = a.size, 250.0
    boxes = []                      # (centre, half extents, yaw) solid blocks: walls
    ramps = []                      # (x, z, width, length, height, dir)
    h = size / 2
    t = 20.0
    # walls that touch but don't overlap: interpenetrating closed meshes don't make a valid
    # solid-leaf tree (inside one, past a face of the other, reads as empty: the car went
    # through the corners)
    for sx, sz, hx, hz in ((0, -h - t, h + 2 * t, t), (0, h + t, h + 2 * t, t), (-h - t, 0, t, h), (h + t, 0, t, h)):
        boxes.append(((sx, 40.0, sz), (hx, 40.0, hz)))
    for k in range(a.ramps):
        ang = 2 * math.pi * k / max(1, a.ramps)
        r = size * 0.28
        ramps.append((r * math.cos(ang), r * math.sin(ang), 120.0, 300.0, 60.0 + 20 * (k % 3), ang + math.pi / 2))
    for k in range(a.pillars):
        ang = 2 * math.pi * (k + 0.5) / max(1, a.pillars)
        r = size * 0.4
        boxes.append(((r * math.cos(ang), 60.0, r * math.sin(ang)), (30.0, 60.0, 30.0)))
    tris = []                       # (3 points, uv 3, material key)
    # ground: tiles, so the quadrants have something to split
    n = int(math.ceil(size / tile))
    for i in range(n):
        for j in range(n):
            x0, z0 = -h + i * tile, -h + j * tile
            x1, z1 = min(h, x0 + tile), min(h, z0 + tile)
            q = [(x0, 0.0, z0), (x0, 0.0, z1), (x1, 0.0, z1), (x1, 0.0, z0)]
            uv = [(0, 0), (0, 4), (4, 4), (4, 0)]
            tris.append(([q[0], q[1], q[2]], [uv[0], uv[1], uv[2]], "ground"))
            tris.append(([q[0], q[2], q[3]], [uv[0], uv[2], uv[3]], "ground"))
    for (cx, cy, cz), (hx, hy, hz) in boxes:
        tris += box_tris((cx, cy, cz), (hx, hy, hz), "wall")
    for x, z, wdt, ln, ht, ang in ramps:
        tris += ramp_tris(x, z, wdt, ln, ht, ang, "ramp")
    tex = {"ground": a.tex_ground, "wall": a.tex_wall, "ramp": a.tex_ramp}
    # render: one node per tile row band and per object, so geometry is spread for the strips
    gb = GBuf()
    meshes, nodes = [], []
    mats = [{"name": k, "extras": {"texture": f"base://{texarc}/world_texture_0/texture/{v}"}} for k, v in tex.items()]
    mat_ix = {k: i for i, k in enumerate(tex)}
    bands = {}
    for p3, uv, mk in tris:
        cx = sum(p[0] for p in p3) / 3
        bands.setdefault((int((cx + h) // tile), mk), []).append((p3, uv))
    for (band, mk), ts in sorted(bands.items()):
        pos = np.array([p for p3, _ in ts for p in p3], np.float32)
        uv = np.array([u for _, uv in ts for u in uv], np.float32)
        # simple baked light: brighter on faces that look up
        col = []
        for p3, _ in ts:
            nrm = np.cross(np.subtract(p3[1], p3[0]), np.subtract(p3[2], p3[0]))
            nrm = nrm / (np.linalg.norm(nrm) or 1)
            s = 0.45 + 0.3 * max(0.0, float(nrm[1])) + 0.1 * float(nrm[0])
            col += [[min(1.0, s)] * 3 + [1.0]] * 3
        col = np.array(col, np.float32)
        prim = {"attributes": {"POSITION": gb.add(pos, 5126, "VEC3", 34962, True),
                               "TEXCOORD_0": gb.add(uv, 5126, "VEC2", 34962),
                               "COLOR_0": gb.add(col, 5126, "VEC4", 34962)},
                "indices": gb.add(np.arange(len(pos), dtype=np.uint32), 5125, "SCALAR", 34963),
                "material": mat_ix[mk], "mode": 4}
        meshes.append({"name": f"{mk}_{band}", "primitives": [prim]})
        nodes.append({"name": f"{mk}_{band}", "mesh": len(meshes) - 1})
    gl = {"asset": {"version": "2.0", "generator": "HWVX-SDL tools/xdl_chunk.py arena"}, "scene": 0,
          "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes, "materials": mats,
          "bufferViews": gb.views, "accessors": gb.acc}
    gl_write(out / "render" / "arena.gltf", gl, bytes(gb.b))
    # collision: the same surfaces, welded (creases computed at build)
    gb = GBuf()
    pos = np.array([p for p3, _, _ in tris for p in p3], np.float32)
    gl = {"asset": {"version": "2.0", "generator": "HWVX-SDL tools/xdl_chunk.py arena"}, "scene": 0,
          "scenes": [{"nodes": [0]}], "nodes": [{"name": "collision", "mesh": 0}],
          "meshes": [{"name": "collision", "primitives": [{
              "attributes": {"POSITION": gb.add(pos, 5126, "VEC3", 34962, True)},
              "indices": gb.add(np.arange(len(pos), dtype=np.uint32), 5125, "SCALAR", 34963), "mode": 4,
              "extras": {"material": 0}}]}],
          "bufferViews": gb.views, "accessors": gb.acc}
    gl_write(out / "collision.gltf", gl, bytes(gb.b))
    cfg = {"format_version": 1, "release": "gc", "set": a.set, "level": a.level, "base": base,
           "textures": w.tex_name, "render": "render", "collision": "collision.gltf", "objects": "none",
           "hub": "base",
           "start": [[size * 0.28, 20.0, -size * 0.25, 0.0]], "chunk": {"min_quads": a.quads},
           "track": {"id": f"uid://xdl-chunk-{a.set}", "alias": "base://mcp/track/0", "name": "Chunk Arena"}}
    (out / "world.jsonc").write_text("// xdl_chunk.py arena: generated test world\n" + json.dumps(cfg, indent=2) + "\n")
    print(f"arena: {len(tris)} triangles, {len(bands)} render pieces, project {out}")


def box_tris(c, he, mk):
    cx, cy, cz = c
    hx, hy, hz = he
    v = [(cx + sx * hx, cy + sy * hy, cz + sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    out = []
    for f in faces:
        q = [v[i] for i in f]
        # outward winding: counter-clockwise seen from outside
        nrm = np.cross(np.subtract(q[1], q[0]), np.subtract(q[2], q[0]))
        if float(np.dot(nrm, np.subtract(np.mean(q, 0), c))) < 0:
            q = q[::-1]
        uv = [(0, 0), (0, 1), (1, 1), (1, 0)]
        out.append(([q[0], q[1], q[2]], [uv[0], uv[1], uv[2]], mk))
        out.append(([q[0], q[2], q[3]], [uv[0], uv[2], uv[3]], mk))
    return out


def ramp_tris(x, z, w, ln, ht, ang, mk):
    """A wedge: rises along its direction from 0 to ht over ln, then a vertical back face."""
    d = np.array([math.cos(ang), 0, math.sin(ang)])
    s = np.array([-d[2], 0, d[0]])
    c = np.array([x, 0, z])
    a0, a1 = c - d * ln / 2 - s * w / 2, c - d * ln / 2 + s * w / 2
    b0, b1 = c + d * ln / 2 - s * w / 2, c + d * ln / 2 + s * w / 2
    t0, t1 = b0 + [0, ht, 0], b1 + [0, ht, 0]
    centre = (a0 + a1 + b0 + b1 + t0 + t1) / 6
    faces = [[a0, a1, t1, t0], [b0, t0, t1, b1], [a0, t0, b0], [a1, b1, t1]]
    out = []
    for f in faces:
        f = [tuple(map(float, p)) for p in f]
        nrm = np.cross(np.subtract(f[1], f[0]), np.subtract(f[2], f[0]))
        if float(np.dot(nrm, np.subtract(np.mean(f, 0), centre))) < 0:
            f = f[::-1]
        uv = [(0, 0), (0, 2), (2, 2), (2, 0)]
        for k in range(1, len(f) - 1):
            out.append(([f[0], f[k], f[k + 1]], [uv[0], uv[k], uv[k + 1]], mk))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("extract")
    s.add_argument("level")
    s.add_argument("project")
    s.add_argument("--rel", default="gc")
    s.add_argument("--set")
    s.add_argument("--level-out")
    s.set_defaults(fn=cmd_extract)
    s = sub.add_parser("arena")
    s.add_argument("project")
    s.add_argument("--base", default="world1")
    s.add_argument("--set", default="arena1")
    s.add_argument("--level", default="worlds_\\sp_\\joyride_\\arena1")
    s.add_argument("--size", type=float, default=3000.0)
    s.add_argument("--ramps", type=int, default=6)
    s.add_argument("--pillars", type=int, default=8)
    s.add_argument("--quads", type=int, default=4)
    s.add_argument("--tex-ground", type=int, default=4)
    s.add_argument("--tex-wall", type=int, default=9)
    s.add_argument("--tex-ramp", type=int, default=36)
    s.set_defaults(fn=cmd_arena)
    s = sub.add_parser("plan")
    s.add_argument("project")
    s.set_defaults(fn=cmd_plan)
    s = sub.add_parser("build")
    s.add_argument("project")
    s.add_argument("out")
    s.add_argument("--base-collision", action="store_true", help="keep the base level's collision pack")
    s.set_defaults(fn=cmd_build)
    s = sub.add_parser("stage")
    s.add_argument("project")
    s.add_argument("out")
    s.add_argument("data")
    s.add_argument("staged")
    s.set_defaults(fn=cmd_stage)
    s = sub.add_parser("stats")
    s.add_argument("project")
    s.add_argument("--retail")
    s.set_defaults(fn=cmd_stats)
    a = ap.parse_args()
    try:
        a.fn(a)
    except (ChunkError, WL.WorldError, XG.GeomError) as e:
        print(f"xdl_chunk: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
