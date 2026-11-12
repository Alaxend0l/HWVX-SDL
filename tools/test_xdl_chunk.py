#!/usr/bin/env python3
"""tools/xdl_chunk.py: the chunker's pieces on generated geometry, then (with the GameCube data
in sources.cfg) a generated arena built, staged and read back, and with --retail a retail
level extracted, re-chunked and its collision compared with the original.

    python3 tools/test_xdl_chunk.py [--retail [LEVEL]]

Scratch files go to HWVX-SDL/build-chunk/test (built packs hold the player's data: never
committed).
"""

import json
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
import xdl_chunk as XC  # noqa: E402
import xdl_gltf as XG  # noqa: E402

scratch = HERE.parent / "build-chunk" / "test"
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def box_mesh(lo, hi):
    c = (np.array(lo) + np.array(hi)) / 2
    h = (np.array(hi) - np.array(lo)) / 2
    return [np.array(t[0]) for t in XC.box_tris(tuple(c), tuple(h), "x")]


def pure():
    # coplanar triangles merge into convex polygons
    sq = [np.array([[0, 0, 0], [0, 0, 1], [1, 0, 1]], float), np.array([[0, 0, 0], [1, 0, 1], [1, 0, 0]], float)]
    m = XC.merge_coplanar(sq, [0, 0])
    check("two triangles of a square merge into one quad", len(m) == 1 and len(m[0][2]) == 4, str(m))
    cube = box_mesh((0, 0, 0), (10, 10, 10))
    m = XC.merge_coplanar(cube, [0] * len(cube))
    check("a box's 12 triangles merge into 6 faces", len(m) == 6 and all(len(p[2]) == 4 for p in m))
    e = XC.crease_edges(cube, [0] * len(cube), 0.5)
    check("a box has 12 crease edges (no diagonals)", len(e) == 12, str(len(e)))
    # clipping and the wall rule
    v = XC.clip_to_box(np.array([[-5, 0, -5], [-5, 0, 5], [5, 0, 5], [5, 0, -5]], float), (0, -1, 0), (2, 1, 2))
    check("a polygon clipped to a box", v is not None and np.allclose(v.min(0), [0, 0, 0]) and np.allclose(v.max(0), [2, 0, 2]))
    wall = np.array([[0, 0, 0], [0, 1, 0], [0, 1, 1], [0, 0, 1]], float)
    check("a polygon in a side facing in is dropped",
          not XC._keep_on_wall(wall, (1, 0, 0, 0), np.zeros(3), np.ones(3), None))
    check("a polygon in a side facing out is kept",
          XC._keep_on_wall(wall, (-1, 0, 0, 0), np.zeros(3), np.ones(3), None))
    # a closed box builds to its solid
    polys = [(pl, v, m_) for pl, v, m_ in ((p[1], p[2], p[3]) for p in XC.merge_coplanar(cube, [0] * len(cube)))]
    t = XC.cell_tree([(pl, v, m_, None) for pl, v, m_ in polys], (-5, -5, -5), (15, 15, 15))
    rng = np.random.default_rng(1)
    pts = rng.uniform(-5, 15, (2000, 3))
    want = np.all((pts > 0) & (pts < 10), 1)
    got = WL.inside_many(t, pts, False)
    check("a box's tree: inside == the box", (got == want).mean() > 0.999, f"{(got == want).mean():.4f}")
    # two sources side by side (a re-gridded retail world): each closed in its own box only
    A = ((0.0, 0.0, 0.0), (10.0, 10.0, 10.0))
    B = ((10.0, 0.0, 0.0), (20.0, 10.0, 10.0))

    def ground(box, h):                 # solid below y = h inside the box: the floor and the caps
        lo, hi = np.array(box[0]), np.array(box[1])
        floor = np.array([[lo[0], h, lo[2]], [lo[0], h, hi[2]], [hi[0], h, hi[2]], [hi[0], h, lo[2]]])
        tr = BB.build([((0.0, 1.0, 0.0, -h), floor, 0)])
        return [((0.0, 1.0, 0.0, -h), floor, 0)] + [(pl, v, 0) for pl, v, _ in BB.box_caps(tr, lo, hi)]
    src = [(pl, v, m_, A) for pl, v, m_ in ground(A, 3.0)] + [(pl, v, m_, B) for pl, v, m_ in ground(B, 6.0)]
    col = XC.ColMesh([p[:3] for p in src], [], [p[3] for p in src])
    lo, hi = np.array([5.0, 0.0, 0.0]), np.array([15.0, 10.0, 10.0])
    pieces = []
    for pl, v, m_, own in src:
        c = XC.clip_to_box(v, lo, hi)
        if c is not None and len(c) >= 3 and XC._thick(c) > 1e-2 and XC._keep_on_wall(c, pl, lo, hi, own):
            pieces.append((pl, c, m_, own))
    t = XC.cell_tree(pieces, lo, hi, XC.Sources(col))
    pts = rng.uniform(lo, hi, (3000, 3))
    want = np.where(pts[:, 0] < 10, pts[:, 1] < 3.0, pts[:, 1] < 6.0)
    got = WL.inside_many(t, pts, False)
    check("a cell over two sources: each side its own ground", (got == want).mean() > 0.995, f"{(got == want).mean():.4f}")
    # a quadrant pack from a generated geometry reads back
    g = generated_geo()
    folder = scratch / "quad"
    if folder.exists():
        shutil.rmtree(folder)
    XC.write_quad_folder(folder, [g, generated_geo(1)])
    d = (folder / "data.bin").read_bytes()
    ptrs = struct.unpack(">%dI" % ((folder / "pointers.bin").stat().st_size // 4), (folder / "pointers.bin").read_bytes())
    tex = dict(XC.Split(folder).pairs("texture_relocs.bin"))
    blob = XG.Blob(d, set(ptrs), "gc")
    offs = XG.scan(blob)
    check("the quadrant pack's geometries scan", len(offs) == 2, str(offs))
    back = XG.decode(blob, offs[0], tex)
    check("a geometry decodes back with its triangles", sum(len(x["tris"]) for x in back["levels"][0]["groups"]) == g.tris)
    check("its texture relocations hold the links' indices", sorted(set(tex.values())) == [4, 9])
    check("every pointer slot in the data", all(0 <= s < len(d) - 3 for s in ptrs))
    table = struct.unpack_from(">I", d, 0)[0]
    check("the geometry table: ids and geometry offsets",
          struct.unpack_from(">III", d, table) == (g.id, 0, offs[0]))


def generated_geo(k=0):
    tris = XC.box_tris((k * 50.0, 10.0, 0.0), (10.0, 10.0, 10.0), "w")
    gb = XC.GBuf()
    pos = np.array([p for t, _, _ in tris for p in t], np.float32)
    uv = np.array([u for _, uvs, _ in tris for u in uvs], np.float32)
    half = len(pos) // 2
    prims = [{"attributes": {"POSITION": gb.add(pos[a:b], 5126, "VEC3", 34962, True),
                             "TEXCOORD_0": gb.add(uv[a:b], 5126, "VEC2", 34962)},
              "indices": gb.add(np.arange(b - a, dtype=np.uint32), 5125, "SCALAR", 34963), "material": i, "mode": 4}
             for i, (a, b) in enumerate(((0, half), (half, len(pos))))]
    gl = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
          "nodes": [{"name": f"box{k}", "mesh": 0, "extras": {"geometryId": 0x5000 + k}}],
          "meshes": [{"primitives": prims}],
          "materials": [{"name": "a", "extras": {"texture": "base://w1_tex/world_texture_0/texture/4"}},
                        {"name": "b", "extras": {"texture": "base://w1_tex/world_texture_0/texture/9"}}],
          "bufferViews": gb.views, "accessors": gb.acc}
    scratch.mkdir(parents=True, exist_ok=True)
    XC.gl_write(scratch / f"box{k}.gltf", gl, bytes(gb.b))
    return XC.geos_from_plain_gltf(scratch / f"box{k}.gltf")[0]


def run(*args):
    return subprocess.run([sys.executable, str(HERE / "xdl_chunk.py"), *map(str, args)], capture_output=True, text=True)


def with_data(retail):
    src = WL.sources()
    if "gc" not in src:
        print("skip: no GameCube data in sources.cfg")
        return
    proj, out, staged = scratch / "arena", scratch / "arena-out", scratch / "arena-data"
    for d in (proj, out):
        if d.exists():
            shutil.rmtree(d)
    r = run("arena", proj)
    check("arena project generated", r.returncode == 0, r.stderr)
    r = run("build", proj, out)
    check("arena built", r.returncode == 0, r.stderr[-400:])
    r = run("stage", proj, out, src["gc"], staged)
    check("arena staged (hwvx-x pack)", r.returncode == 0, r.stderr[-400:])
    if r.returncode:
        return
    rel = WL.Release("gc", staged)
    w = rel.world("arena1")
    check("the world pack names the new archives", w.name == "worlds_\\geo_\\arena1_geo" and
          w.collision_name == "worlds_\\col_\\arena1_col", f"{w.name} {w.collision_name}")
    check("quadrants: a hub and 4 strips", len(w.quads) == 5 and w.quads[0].region is None, str(len(w.quads)))
    check("at most 4 neighbours each", all(len(q.neighbors) <= 4 and None not in q.neighbors for q in w.quads))
    check("every region a convex prism", all(WL.is_convex(q.region) for q in w.quads[1:]))
    check("every quadrant's pack found as the game looks it up",
          all(WL.quad_pack(rel, w, q) is not None for q in w.quads if q.num_geoms))
    start = w.tile_cars()[0]["pos"]
    want = XC.jsonc(proj / "world.jsonc")["start"][0]
    base = WL.Release("gc", src["gc"]).world(XC.jsonc(proj / "world.jsonc")["base"]).tile_cars()[0]["pos"]
    check("the start tile moved to the project's start",
          np.allclose(start, want[:3], atol=0.01) and not np.allclose(start, base, atol=1), f"{start} want {want[:3]}")
    held = [q.index for q in w.quads[1:] if WL.inside(q.region, start)]
    check("the start is in a region", bool(held))
    col = rel.collision("arena1_col")
    check("collision grid over the arena", col.version == 0xD and col.mode == 1 and col.num_cells > 1)
    pts = np.array([[0, 5, 0], [0, -5, 0], [700, 5, 300], [700, -5, 300]], float)
    lo, sz, (nx, ny, nz) = np.array(col.min), np.array(col.cell_size), col.n
    got = []
    for p in pts:
        x, y, z = np.floor((p - lo) / sz).astype(int)
        ci = col.cell_index[x + nx * (z + nz * y)]
        got.append(bool(ci) and WL.inside(col.cell_tree(ci), p, False))
    check("above the ground empty, below it solid", got == [False, True, False, True], str(got))
    if retail:
        retail_check(retail, src["gc"])


def retail_check(level, data):
    proj, out, staged = scratch / "retail", scratch / "retail-out", scratch / "retail-data"
    r = run("extract", level, proj)
    check(f"{level} extracted", r.returncode == 0, r.stderr[-400:])
    r = run("build", proj, out)
    check(f"{level} re-chunked and built", r.returncode == 0, r.stderr[-400:])
    r = run("stage", proj, out, data, staged)
    check(f"{level} staged", r.returncode == 0, r.stderr[-400:])
    if r.returncode:
        return
    cfg = XC.jsonc(proj / "world.jsonc")
    base = WL.Release("gc", data)
    A = base.collision(base.world(level).collision_name)
    B = WL.Release("gc", staged).collision(cfg["set"] + "_col")
    rng = np.random.default_rng(1)
    pts = []
    for i in range(1, A.num_cells, 4):
        c, t = A.cell(i), A.cell_tree(i)
        if t:
            for plane, v, _ in BB.boundary_polys(t, c["min"], c["max"])[0][:20]:
                w = rng.random(len(v))
                p = (w / w.sum()) @ v
                pts += [p + np.array(plane[:3]) * 2, p - np.array(plane[:3]) * 2]

    def solid(C, p):
        lo, sz, (nx, ny, nz) = np.array(C.min), np.array(C.cell_size), C.n
        x, y, z = np.floor((p - lo) / sz).astype(int)
        if not (0 <= x < nx and 0 <= y < ny and 0 <= z < nz):
            return False
        ci = C.cell_index[x + nx * (z + nz * y)]
        return bool(ci) and WL.inside(C.cell_tree(ci), p, False)
    agree = np.mean([solid(A, p) == solid(B, p) for p in pts])
    check(f"{level}: the collision 2 units off its surfaces agrees with retail (> 97%)", agree > 0.97, f"{agree:.4f}")


if __name__ == "__main__":
    pure()
    retail = None
    if "--retail" in sys.argv:
        i = sys.argv.index("--retail")
        retail = sys.argv[i + 1] if i + 1 < len(sys.argv) else "world1"
    with_data(retail)
    print("FAILED" if fails else "all passed")
    sys.exit(1 if fails else 0)
