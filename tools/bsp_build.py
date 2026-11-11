#!/usr/bin/env python3
"""A solid-leaf BSP builder for the game's two BSP encodings (prototype for the auto-chunker),
checked against the retail trees. docs/worlds/README.md ("Chunking") has the rules it follows.

    bsp_build.py check [--rel gc,pc,ps2] [--only world1,...] [--what regions,volumes,cells]
                       [--samples N] [--max-cells N] [--out DIR]
        Every retail tree: (1) rebuild it from one polygon per node, in node order, and compare
        the bytes ("identical"); (2) rebuild it from the solid's boundary polygons in a shuffled
        order and compare inside/outside answers on sampled points ("equivalent").
    bsp_build.py rules [--rel gc] [--only ...]
        What the retail trees say about the exporter: layout, coplanar faces, splits, balance.
    bsp_build.py demo
        A box and an L-shaped prism built from triangles, printed as BSPN0 nodes.

As a module (the chunker's entry points):
    build(polys, eps)                 polygons (plane, vertices, extra) -> tree, pre-order
    polys_from_triangles(tris)        counter-clockwise triangles seen from outside -> polygons
    convex_region(planes)             outward planes of a convex cell -> the minimal chain
    prism(outline_xz, y0, y1)         a convex or concave outline, extruded -> triangles
    world_lib.bspn0_write / tbsp_write   trees -> pack bytes

The rule (what the retail trees are consistent with, see `rules`): the splitter is the first
polygon of the list; the others go, in order, in front, behind, or are cut in two; a polygon in
the splitter's plane goes in front when it faces the same way and behind when it faces the
other way. Nodes are written in pre-order (node, front subtree, back subtree). No balancing.

Paths come from the game's sources.cfg (world_lib.sources); output goes to --out (default
build-worldstats/bsp_check.md, gitignored).
"""

import argparse
import random
import struct
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import world_lib as WL  # noqa: E402

ROOT = HERE.parent
OUT = (ROOT.parent if ROOT.parent.name == "HWVX-SDL" else ROOT) / "build-worldstats"
EPS = 0.01          # on-plane tolerance for new geometry, in world units (a car is ~10 long)
sys.setrecursionlimit(20000)


# ------------------------------------------------------------------------------------------
# polygons

def plane_of(verts):
    """The plane of a counter-clockwise polygon (Newell), normal towards the viewer."""
    v = np.asarray(verts, float)
    n = np.zeros(3)
    for i in range(len(v)):
        a, b = v[i], v[(i + 1) % len(v)]
        n += np.cross(a, b)
    ln = np.linalg.norm(n)
    if ln < 1e-12:
        return None
    n /= ln
    return (n[0], n[1], n[2], -float(n @ v.mean(0)))


def f32_plane(p):
    return struct.unpack("<4f", struct.pack("<4f", *p))


def split_poly(verts, plane, eps):
    """(front part, back part, side): side is 'front', 'back', 'on' or 'span'."""
    n = np.array(plane[:3])
    d = verts @ n + plane[3]
    pos, neg = bool((d > eps).any()), bool((d < -eps).any())
    if not pos and not neg:
        return None, None, "on"
    if not neg:
        return verts, None, "front"
    if not pos:
        return None, verts, "back"
    fr, bk = [], []
    m = len(verts)
    for i in range(m):
        a, b, da, db = verts[i], verts[(i + 1) % m], d[i], d[(i + 1) % m]
        if da >= -eps:
            fr.append(a)
        if da <= eps:
            bk.append(a)
        if (da > eps and db < -eps) or (da < -eps and db > eps):
            x = a + (b - a) * (da / (da - db))
            fr.append(x)
            bk.append(x)
    return (np.array(fr) if len(fr) >= 3 else None), (np.array(bk) if len(bk) >= 3 else None), "span"


def plane_quad(plane, lo, hi):
    """The part of a plane inside the box lo..hi: a convex polygon, counter-clockwise seen
    from the plane's front."""
    n = np.array(plane[:3], float)
    n /= np.linalg.norm(n)
    c = (np.array(lo) + np.array(hi)) / 2
    r = float(np.linalg.norm(np.array(hi) - np.array(lo))) * 2 + 1
    o = c - n * (float(n @ c) + plane[3] / np.linalg.norm(plane[:3]))
    a = np.cross(n, [1.0, 0, 0] if abs(n[0]) < 0.9 else [0, 1.0, 0])
    a /= np.linalg.norm(a)
    b = np.cross(n, a)
    v = np.array([o - a * r - b * r, o + a * r - b * r, o + a * r + b * r, o - a * r + b * r])
    for axis in range(3):
        for sign, lim in ((1, lo[axis]), (-1, hi[axis])):
            pl = [0.0, 0.0, 0.0, -sign * lim]
            pl[axis] = float(sign)
            v, _, side = split_poly(v, pl, 1e-9)
            if v is None:
                return None
    return v


def polys_from_triangles(tris, materials=None):
    """Triangles (3 points each, counter-clockwise from outside) -> builder polygons; thin
    ones are dropped."""
    out = []
    for i, t in enumerate(tris):
        v = np.asarray(t, float)
        pl = plane_of(v)
        if pl is not None:
            out.append((f32_plane(pl), v, materials[i] if materials else None))
    return out


def prism(outline_xz, y0, y1):
    """Triangles of an extruded outline (points (x, z) in either direction; concave is fine):
    side walls, top, bottom, all facing outwards."""
    pts = [tuple(map(float, p)) for p in outline_xz]
    n = len(pts)
    tris = []

    def add(a, b, c, want):
        """The triangle, wound so that its normal points along `want`."""
        if float(np.dot(np.cross(np.subtract(b, a), np.subtract(c, a)), want)) < 0:
            b, c = c, b
        tris.append((a, b, c))

    def area2(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    turn = 1.0 if sum(area2((0.0, 0.0), pts[i], pts[(i + 1) % n]) for i in range(n)) > 0 else -1.0
    for i in range(n):
        (ax, az), (bx, bz) = pts[i], pts[(i + 1) % n]
        out = ((bz - az) * turn, 0.0, -(bx - ax) * turn)     # away from the outline's inside
        a0, b0, a1, b1 = (ax, y0, az), (bx, y0, bz), (ax, y1, az), (bx, y1, bz)
        add(a0, a1, b1, out)
        add(a0, b1, b0, out)
    idx = list(range(n))

    def cap(a, b, c):
        add((a[0], y1, a[1]), (b[0], y1, b[1]), (c[0], y1, c[1]), (0.0, 1.0, 0.0))
        add((a[0], y0, a[1]), (b[0], y0, b[1]), (c[0], y0, c[1]), (0.0, -1.0, 0.0))
    guard = 0
    while len(idx) > 3 and guard < n * n + 8:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = pts[i0], pts[i1], pts[i2]
            if area2(a, b, c) * turn <= 0:
                continue                                    # a reflex corner
            if any(j not in (i0, i1, i2) and area2(a, b, pts[j]) * turn >= 0 and area2(b, c, pts[j]) * turn >= 0
                   and area2(c, a, pts[j]) * turn >= 0 for j in idx):
                continue                                    # another corner inside this ear
            cap(a, b, c)
            idx.pop(k)
            break
    cap(*(pts[i] for i in idx))
    return tris


def convex_region(planes):
    """The smallest BSPN0 for a convex cell: one node per outward plane, each the back child
    of the one before. (The retail exporter wrote one node per triangle of the cell's mesh.)"""
    planes = [f32_plane(p) for p in planes]
    return [(p, None, k + 1 if k + 1 < len(planes) else None, None) for k, p in enumerate(planes)]


# ------------------------------------------------------------------------------------------
# the builder

def build(polys, eps=EPS, limit=0xFFFF):
    """polys: [(plane, vertices (n, 3) array, extra)], in the order that picks the splitters.
    Returns the tree in pre-order: [(plane, front, back, extra)]."""
    tree = []

    def rec(items):
        k = len(tree)
        if k >= limit:
            raise WL.WorldError(f"more than {limit} nodes")
        plane, verts, extra = items[0]
        tree.append(None)
        n = np.array(plane[:3])
        front, back = [], []
        for pl, v, ex in items[1:]:
            f, b, side = split_poly(v, plane, eps)
            if side == "on":
                (front if float(np.dot(n, pl[:3])) > 0 else back).append((pl, v, ex))
            else:
                if f is not None:
                    front.append((pl, f, ex))
                if b is not None:
                    back.append((pl, b, ex))
        fi = rec(front) if front else None
        bi = rec(back) if back else None
        tree[k] = (plane, fi, bi, extra)
        return k
    if polys:
        rec(list(polys))
    return tree


# ------------------------------------------------------------------------------------------
# reading a retail tree back into polygons

def bounds_of(tree, lo=None, hi=None):
    """A box for the tree's polygons: the given one, grown a little."""
    lo, hi = np.array(lo, float), np.array(hi, float)
    pad = (hi - lo) * 0.25 + 50
    return lo - pad, hi + pad


def node_polys(tree, lo, hi, eps=1e-4, paths=None):
    """One polygon per node: its plane inside the box and inside the node's cell (the half-
    spaces of its ancestors). None where the cell is empty (a node no point reaches)."""
    out = [None] * len(tree)
    todo = [(0, [])]
    while todo:
        k, path = todo.pop()
        plane, front, back, _ = tree[k]
        if paths is not None:
            paths[k] = path
        v = plane_quad(plane, lo, hi)
        for pl, want_front in path:
            if v is None:
                break
            f, b, side = split_poly(v, pl, eps)
            if side == "on":
                continue
            v = f if want_front else b
        out[k] = v
        if front is not None:
            todo.append((front, path + [(plane, True)]))
        if back is not None:
            todo.append((back, path + [(plane, False)]))
    return out


def _route(tree, k, verts, normal, solid_if_none, eps, out):
    """Send a polygon lying on a plane with `normal` down the subtree at k, as the space just
    on the normal's side of it; collect (piece, solid)."""
    if k is None:
        out.append((verts, solid_if_none))
        return
    plane, front, back, _ = tree[k]
    f, b, side = split_poly(verts, plane, eps)
    if side == "on":
        if float(np.dot(normal, plane[:3])) > 0:
            _route(tree, front, verts, normal, False, eps, out)
        else:
            _route(tree, back, verts, normal, True, eps, out)
        return
    if f is not None:
        _route(tree, front, f, normal, False, eps, out)
    if b is not None:
        _route(tree, back, b, normal, True, eps, out)


def boundary_polys(tree, lo, hi, eps=1e-4):
    """The solid's surface inside the box as polygons facing out of the solid: for every node,
    the parts of its polygon with empty space in front and solid behind (kept with the node's
    plane), and the rare parts the other way round (plane flipped)."""
    out, flipped = [], 0
    paths = {}

    def in_cell(k, p):
        for (a, b, c, d), want_front in paths[k]:
            if ((a * p[0] + b * p[1] + c * p[2] + d) > 0.0) != want_front:
                return False
        return True
    for k, v in enumerate(node_polys(tree, lo, hi, eps, paths)):
        if v is None:
            continue
        plane, front, back, extra = tree[k]
        n = np.array(plane[:3])
        fr = []
        _route(tree, front, v, n, False, eps, fr)
        for piece, front_solid in fr:
            bk = []
            _route(tree, back, piece, -n, True, eps, bk)
            for p2, back_solid in bk:
                if front_solid == back_solid:
                    continue
                # a face only where the node's cell has room on both sides of it: the second
                # triangle of a face sits in a cell with no room behind it
                c = p2.mean(0)
                if not in_cell(k, c + n * 1e-3) or not in_cell(k, c - n * 1e-3):
                    continue
                if back_solid:
                    out.append((plane, p2, extra))
                else:
                    flipped += 1
                    out.append((tuple(-x for x in plane), p2[::-1].copy(), extra))
    return out, flipped


# ------------------------------------------------------------------------------------------
# checks

def same_tree(a, b):
    return len(a) == len(b) and all(x[0] == y[0] and x[1] == y[1] and x[2] == y[2] for x, y in zip(a, b))


def preorder(tree):
    """The tree renumbered in pre-order, and whether it already was."""
    order, todo = [], [0]
    while todo:
        k = todo.pop()
        order.append(k)
        _, f, b, _ = tree[k]
        if b is not None:
            todo.append(b)
        if f is not None:
            todo.append(f)
    new = {old: i for i, old in enumerate(order)}
    out = [(tree[o][0], None if tree[o][1] is None else new[tree[o][1]],
            None if tree[o][2] is None else new[tree[o][2]], tree[o][3]) for o in order]
    return out, order == list(range(len(tree))) and len(order) == len(tree)


def sample_points(tree, lo, hi, n, rng, polys):
    """Random points in the box, and points just either side of the surface."""
    pts = [rng.uniform(lo, hi, (n, 3))]
    faces = [v for _, v, _ in polys if v is not None]
    if faces:
        pick = rng.integers(0, len(faces), n)
        near = []
        for i in pick:
            v = faces[i]
            w = rng.dirichlet(np.ones(len(v)))
            near.append(w @ v)
        near = np.array(near)
        near = np.clip(near + rng.normal(0, 1.0, near.shape) * rng.choice([0.5, 5, 50], (n, 1)), lo, hi)
        pts.append(near)
    return np.concatenate(pts)


def near_surface(tree, pts, tol):
    """Points within tol of any of the tree's planes they are tested against: their answer may
    differ between trees that cut the same solid differently."""
    out = np.zeros(len(pts), bool)
    todo = [(0, np.arange(len(pts)))]
    while todo:
        k, idx = todo.pop()
        if not len(idx):
            continue
        (a, b, c, d), front, back, _ = tree[k]
        p = pts[idx]
        dist = p[:, 0] * a + p[:, 1] * b + p[:, 2] * c + d
        out[idx[np.abs(dist) < tol]] = True
        if front is not None:
            todo.append((front, idx[dist > 0]))
        if back is not None:
            todo.append((back, idx[dist <= 0]))
    return out


def check_tree(tree, lo, hi, kind, e, original, samples, rng, slo=None, shi=None, eps=1e-4):
    """One retail tree -> a result record."""
    res = {"nodes": len(tree)}
    strict = kind == "bspn0"
    pre, was_pre = preorder(tree)
    res["preorder"] = was_pre
    polys = node_polys(pre, lo, hi, eps)
    slo, shi = (lo, hi) if slo is None else (slo, shi)
    res["unreached"] = sum(1 for v in polys if v is None)
    # (1) the fixed point: one polygon per node, in node order
    items = [(pre[k][0], v, pre[k][3]) for k, v in enumerate(polys) if v is not None]
    try:
        again = build(items, eps=eps)
    except (RecursionError, WL.WorldError):
        again = []
    res["same_tree"] = same_tree(again, pre)
    if kind == "bspn0":
        res["same_bytes"] = res["same_tree"] and was_pre and WL.bspn0_write(again, e, original[0x11:0x12]) == original
    else:
        res["same_bytes"] = res["same_tree"] and was_pre and WL.tbsp_write(again, e, original[0x18:0x20]) == original
    # (2) from the surface, any order
    if samples:
        surf, flipped = boundary_polys(pre, lo, hi)
        res["surface_polys"], res["flipped"] = len(surf), flipped
        order = list(range(len(surf)))
        random.Random(len(tree)).shuffle(order)
        try:
            other = build([surf[i] for i in order], eps=1e-4, limit=1 << 20)
        except (RecursionError, WL.WorldError):
            other = None
        if other is None:
            res["agree"] = None
        elif not other:
            pts = sample_points(pre, slo, shi, samples, rng, surf)
            res["points"] = len(pts)
            res["agree"] = float((~WL.inside_many(pre, pts, strict)).mean())
            res["shuffled_nodes"] = 0
        else:
            pts = sample_points(pre, slo, shi, samples, rng, surf)
            keep = ~(near_surface(pre, pts, 0.01) | near_surface(other, pts, 0.01))
            pts = pts[keep]
            a = WL.inside_many(pre, pts, strict)
            b = WL.inside_many(other, pts, strict)
            res["points"] = len(pts)
            res["agree"] = float((a == b).mean()) if len(pts) else 1.0
            res["shuffled_nodes"] = len(other)
    return res


def retail_trees(release, name, what, sample_box=False):
    """(label, kind, tree, lo, hi, e, original bytes[, the box to sample: the collision bounds]) for a level's regions and volumes and
    its collision pack's cells."""
    world = release.world(name)
    pk = world.pk
    col = release.collision(world.collision_name)
    lo, hi = np.array(col.min) - 20000.0, np.array(col.max) + 20000.0
    box = (np.array(col.min) - 100.0, np.array(col.max) + 100.0) if sample_box else ()
    if "regions" in what:
        for q in world.quads:
            if q.region:
                yield (f"{name}/region/{q.index}", "bspn0", q.region, lo, hi, pk.e,
                       pk.d[q.bsp_off:q.bsp_off + 0x20 * len(q.region)]) + box
    if "volumes" in what:
        for i, v in enumerate(world.volumes()):
            if v["shape"] == 0 and v["bsp"]:
                t = WL.bspn0_read(pk, v["bsp"])
                yield (f"{name}/volume/{i}", "bspn0", t, lo, hi, pk.e, pk.d[v["bsp"]:v["bsp"] + 0x20 * len(t)]) + box
    if "cells" in what:
        for i in range(1, col.num_cells):
            c = col.cell(i)
            t = col.cell_tree(i)
            if t:
                clo, chi = bounds_of(None, c["min"], c["max"])
                cbox = (np.array(c["min"]), np.array(c["max"])) if sample_box else ()
                yield (f"{world.collision_name.split(chr(92))[-1]}/cell/{i}", "terrain", t, clo, chi, col.pk.e,
                       col.pk.d[c["nodes_off"]:c["nodes_off"] + 0x20 * len(t)]) + cbox


def each_level(a):
    src = WL.sources(a.sources)
    for rel in a.rel.split(","):
        if rel not in src:
            print(f"{rel}: no data folder in sources.cfg", file=sys.stderr)
            continue
        release = WL.Release(rel, src[rel])
        names = release.world_names()
        if a.only:
            names = [n for n in names if n in a.only.lower().split(",")]
        yield rel, release, names


def cmd_check(a):
    what = a.what.split(",")
    rng = np.random.default_rng(1)
    lines = ["# BSP rebuild check\n", "Written by tools/bsp_build.py check.\n",
             "identical: rebuilt from one polygon per node in node order, the same bytes as the pack's. "
             "equivalent: rebuilt from the solid's surface polygons in a shuffled order; the share of sampled "
             "points (not within 0.01 of a plane) with the same inside/outside answer.\n"]
    rows, bad = [], []
    for rel, release, names in each_level(a):
        seen, t0 = set(), time.time()
        tot = {}
        for name in names:
            try:
                trees = list(retail_trees(release, name, what, True))
            except (WL.WorldError, WL.XM.MediaError) as err:
                print(f"{rel}/{name}: {err}", file=sys.stderr)
                continue
            for label, kind, tree, lo, hi, e, orig, slo, shi in trees:
                key = label.split("/")[1]
                if kind == "terrain":
                    if label in seen:
                        continue
                    seen.add(label)
                    if a.max_cells and tot.get("cell", {}).get("trees", 0) >= a.max_cells:
                        continue
                elif orig in seen:
                    continue            # the same region in another level of the set
                else:
                    seen.add(orig)
                r = check_tree(tree, lo, hi, kind, e, orig, a.samples if len(tree) <= a.max_nodes else 0, rng, slo, shi,
                               a.eps)
                t = tot.setdefault(key, {"trees": 0, "nodes": 0, "identical": 0, "same_tree": 0, "preorder": 0,
                                         "unreached": 0, "points": 0, "agree_pts": 0.0, "full": 0, "tested": 0,
                                         "flipped": 0, "shuffled_nodes": 0, "tested_nodes": 0})
                t["trees"] += 1
                t["nodes"] += r["nodes"]
                t["identical"] += r["same_bytes"]
                t["same_tree"] += r["same_tree"]
                t["preorder"] += r["preorder"]
                t["unreached"] += r["unreached"]
                if not r["same_bytes"]:
                    bad.append(f"{rel} {label}: {r['nodes']} nodes, {r['unreached']} unreached, "
                               f"same tree {r['same_tree']}, pre-order {r['preorder']}")
                if r.get("agree") is not None:
                    t["tested"] += 1
                    t["points"] += r["points"]
                    t["agree_pts"] += r["agree"] * r["points"]
                    t["full"] += r["agree"] == 1.0
                    t["flipped"] += r["flipped"]
                    t["shuffled_nodes"] += r["shuffled_nodes"]
                    t["tested_nodes"] += r["nodes"]
            release.drop(name)
        for key, t in tot.items():
            rows.append([rel, key, t["trees"], f"{t['nodes']:,}", f"{t['identical']} ({t['identical'] / t['trees']:.1%})",
                         t["preorder"], t["unreached"], t["tested"],
                         f"{t['agree_pts'] / t['points']:.4%}" if t["points"] else "-",
                         f"{t['full']}/{t['tested']}", f"{t['points']:,}", t["flipped"],
                         f"{t['shuffled_nodes'] / t['tested_nodes']:.2f}x" if t["tested_nodes"] else "-"])
        print(f"{rel}: {time.time() - t0:.1f} s", file=sys.stderr)
    head = ["release", "trees", "count", "nodes", "identical", "in pre-order", "unreached nodes", "shuffled: trees",
            "points agreeing", "trees fully agreeing", "points", "inward faces", "node count vs retail"]
    import world_stats
    lines.append(world_stats.table(head, rows))
    if bad:
        lines.append(f"\nNot identical ({len(bad)}):\n")
        lines += [f"* {b}" for b in bad[:200]]
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "bsp_check.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:6 + len(rows)]))
    print(a.out / "bsp_check.md")
    return 0


def cmd_rules(a):
    """Layout and partition facts of the retail trees."""
    what = a.what.split(",")
    for rel, release, names in each_level(a):
        stats = {}
        seen = set()
        for name in names:
            try:
                trees = list(retail_trees(release, name, what))
            except (WL.WorldError, WL.XM.MediaError) as err:
                print(f"{rel}/{name}: {err}", file=sys.stderr)
                continue
            for label, kind, tree, lo, hi, e, orig in trees:
                if orig in seen:
                    continue
                seen.add(orig)
                s = stats.setdefault(label.split("/")[1], {
                    "trees": 0, "nodes": 0, "preorder": 0, "copl_same_front": 0, "copl_same_back": 0,
                    "copl_opp_front": 0, "copl_opp_back": 0, "bit_equal": 0, "near_equal": 0,
                    "split_planes": 0, "depth": [], "size": [], "unit": 0, "front_children": 0,
                    "back_children": 0, "pad": set(), "max_depth_ratio": 0.0})
                s["trees"] += 1
                s["nodes"] += len(tree)
                s["preorder"] += preorder(tree)[1]
                s["depth"].append(WL.depth(tree))
                s["size"].append(len(tree))
                if kind == "bspn0":
                    s["pad"].add(orig[0x11:0x12].hex() + ".." + orig[0x14:0x20].hex())
                else:
                    s["pad"].add(orig[0x18:0x20].hex())
                for plane, front, back, _ in tree:
                    s["unit"] += abs(sum(x * x for x in plane[:3]) - 1.0) < 1e-4
                    s["front_children"] += front is not None
                    s["back_children"] += back is not None
                # every (ancestor, descendant) pair in one plane; planes found on both sides
                def walk(k, anc):
                    plane, front, back, _ = tree[k]
                    for ap, side in anc:
                        same = WL.same_plane(plane, ap, 1e-5)
                        opp = WL.same_plane(plane, tuple(-x for x in ap), 1e-5)
                        if same or opp:
                            s["copl_%s_%s" % ("same" if same else "opp", side)] += 1
                            if plane == ap or plane == tuple(-x for x in ap):
                                s["bit_equal"] += 1
                            else:
                                s["near_equal"] += 1
                    below = {plane}
                    sides = []
                    for child, side in ((front, "front"), (back, "back")):
                        if child is not None:
                            sub = walk(child, anc + [(plane, side)])
                            sides.append(sub)
                            below |= sub
                    if len(sides) == 2:
                        s["split_planes"] += len(sides[0] & sides[1])
                    return below
                walk(0, [])
        print(f"== {rel}")
        for key, s in stats.items():
            n = s["nodes"]
            import statistics
            ratio = [d / max(1.0, np.log2(z + 1)) for d, z in zip(s["depth"], s["size"])]
            print(f"{key}: {s['trees']} trees, {n} nodes; pre-order {s['preorder']}/{s['trees']}; "
                  f"unit normals {s['unit']}/{n}; front children {s['front_children']}, back {s['back_children']}")
            print(f"   in an ancestor's plane: same way in front {s['copl_same_front']}, same way behind "
                  f"{s['copl_same_back']}, other way in front {s['copl_opp_front']}, other way behind "
                  f"{s['copl_opp_back']} (bit-equal planes {s['bit_equal']}, nearly {s['near_equal']})")
            print(f"   planes found on both sides of a node (a polygon cut in two): {s['split_planes']}; "
                  f"depth / log2(nodes + 1): median {statistics.median(ratio):.2f}, max {max(ratio):.2f}; "
                  f"depth max {max(s['depth'])}; unused bytes {sorted(s['pad'])[:4]}")
    return 0


def cmd_demo(a):
    box = prism([(0, 0), (0, 10), (10, 10), (10, 0)], 0, 5)
    ell = prism([(0, 0), (0, 20), (10, 20), (10, 10), (20, 10), (20, 0)], 0, 5)
    for name, tris, tests in (("box", box, [((5, 2, 5), True), ((15, 2, 5), False), ((5, 6, 5), False)]),
                              ("L", ell, [((5, 2, 15), True), ((15, 2, 5), True), ((15, 2, 15), False),
                                          ((5, 2, 5), True), ((25, 2, 5), False)])):
        tree = build(polys_from_triangles(tris))
        print(f"{name}: {len(tris)} triangles -> {len(tree)} nodes, convex {WL.is_convex(tree)}")
        for k, (pl, f, b, _) in enumerate(tree):
            print("  %2d  %7.3f %7.3f %7.3f %9.3f  front %s back %s" % (k, *pl, f, b))
        for p, want in tests:
            got = WL.inside(tree, p)
            print(f"  {p}: {'inside' if got else 'outside'}{'' if got == want else '  WRONG'}")
            if got != want:
                return 1
        assert WL.bspn0_read_bytes(WL.bspn0_write(tree)) == [(pl, f, b) for pl, f, b, _ in tree]
    chain = convex_region([(1, 0, 0, -10), (-1, 0, 0, 0), (0, 1, 0, -5), (0, -1, 0, 0), (0, 0, 1, -10), (0, 0, -1, 0)])
    print("convex_region: 6 planes ->", len(chain), "nodes;", "(5,2,5)", WL.inside(chain, (5, 2, 5)),
          "(11,2,5)", WL.inside(chain, (11, 2, 5)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("check", cmd_check), ("rules", cmd_rules), ("demo", cmd_demo)):
        p = sub.add_parser(name)
        p.set_defaults(fn=fn)
        p.add_argument("--rel", default="gc,pc,ps2")
        p.add_argument("--only")
        p.add_argument("--what", default="regions,volumes,cells")
        p.add_argument("--sources")
        p.add_argument("--samples", type=int, default=2000, help="points per tree for the shuffled check (0: skip)")
        p.add_argument("--max-nodes", type=int, default=400, help="shuffled check only for trees up to this size")
        p.add_argument("--max-cells", type=int, default=0, help="at most this many collision cells per release")
        p.add_argument("--eps", type=float, default=1e-4, help="on-plane tolerance when reading retail trees back")
        p.add_argument("--out", type=Path, default=OUT)
    a = ap.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
