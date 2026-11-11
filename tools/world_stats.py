#!/usr/bin/env python3
"""Measures every world of the releases in sources.cfg: quadrants, regions, geometry, objects,
collision. The numbers behind docs/worlds/README.md ("Chunking"), for the auto-chunker.

    world_stats.py [--rel gc,pc,ps2] [--only world1,ch1_l2] [--out DIR] [--sources FILE] [--no-geometry]

Reads the archives in place (paths from the game's sources.cfg, or --gc/--pc/--ps2 DIR) and
writes DIR/raw.json (every level, quadrant, cell count) and DIR/stats.md (the tables). DIR
defaults to build-worldstats beside the tools (gitignored). Nothing is written elsewhere.

A "set" is what levels share: a quadrant archive (w1_geo), its collision (w1_col) and its world
textures (w1_tex). A "level" is a world pack (ch1_l2): the quadrant table, regions, objects.
The geometry numbers are of the first level of detail unless they say otherwise.
"""

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import world_lib as WL  # noqa: E402

ROOT = HERE.parent
OUT = (ROOT.parent if ROOT.parent.name == "HWVX-SDL" else ROOT) / "build-worldstats"


def mmm(values):
    v = [x for x in values if x is not None]
    if not v:
        return "-"
    f = (lambda x: f"{x:,.0f}") if max(v) >= 100 or all(float(x).is_integer() for x in v) else (lambda x: f"{x:.2f}")
    return f"{f(min(v))} / {f(statistics.median(v))} / {f(max(v))}"


def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------------------------------
# measuring

def pack_stats(pk):
    geoms = WL.quad_geometry(pk)
    tex, tris0, verts0, groups0, tris_all, maxv, lods = set(), 0, 0, 0, 0, 0, 0
    for g in geoms:
        lods = max(lods, len(g["levels"]))
        for i, lv in enumerate(g["levels"]):
            tris_all += lv["tris"]
            maxv = max(maxv, lv["verts"])
            tex.update(lv["textures"])
            if i == 0:
                tris0 += lv["tris"]
                verts0 += lv["verts"]
                groups0 += lv["groups"]
    h = pk.raw
    return {"bytes": pk.size, "data_bytes": len(pk.d), "geoms": len(geoms), "tris": tris0, "verts": verts0,
            "groups": groups0, "tris_all_lods": tris_all, "max_level_verts": maxv, "max_lods": lods,
            "textures": len(tex), "shared_textures": sum(1 for t in tex if t >> 16),
            "tex_relocs": len(h.section("texture_relocs")) // 8, "shader_relocs": len(h.section("shader_relocs")) // 8,
            "pointers": len(h.section("pointers")) // 4,
            "centers": [g["center"] for g in geoms], "ids": [g["id"] for g in geoms],
            "max_dist": max((d for g in geoms for d in g["dist"]), default=0),
            "max_radius": max((g["radius"] for g in geoms), default=0)}


def region_stats(tree):
    planes = []
    for pl, _, _, _ in tree:
        if not any(WL.same_plane(pl, q) for q in planes):
            planes.append(pl)
    horizontal = sum(1 for p in planes if abs(p[1]) > 0.999)
    vertical = sum(1 for p in planes if abs(p[1]) < 0.02)
    return {"nodes": len(tree), "planes": len(planes), "depth": WL.depth(tree), "convex": WL.is_convex(tree),
            "front_children": sum(1 for n in tree if n[1] is not None),
            "horizontal": horizontal, "vertical": vertical, "sloped": len(planes) - horizontal - vertical}


def collision_stats(col):
    nodes, edges, depths, empty = [], [], [], 0
    for i in range(1, col.num_cells):
        c = col.cell(i)
        if not c["nodes_off"]:
            empty += 1
            continue
        nodes.append(c["num_nodes"])
        edges.append(c["num_edges"])
        depths.append(WL.depth(col.cell_tree(i)))
    nx, ny, nz = col.n
    return {"bytes": col.pk.size, "version": col.version, "mode": col.mode, "grid": col.n, "cell_size": col.cell_size,
            "min": col.min, "max": col.max, "squares": nx * ny * nz,
            "squares_empty": sum(1 for x in col.cell_index if x == 0), "cells": col.num_cells - 1,
            "cells_no_bsp": empty, "nodes": nodes, "edges_per_cell": edges, "depths": depths,
            "num_edges": col.num_edges, "num_polys": col.num_polys}


def region_checks(world, packs, col):
    """How the regions relate: who holds each geometry's centre and each object, how the
    regions cover the collision bounds, and whether the neighbour lists are the touching ones."""
    import numpy as np
    quads = [q for q in world.quads if q.region]
    if not quads:
        return None
    out = {}
    own = other = none = 0
    for q in quads:
        st = packs.get(q.index)
        if not st or not st["centers"]:
            continue
        pts = np.array(st["centers"], float)
        mine = WL.inside_many(q.region, pts)
        anyone = np.zeros(len(pts), bool)
        for r in quads:
            if r is not q:
                anyone |= WL.inside_many(r.region, pts)
        own += int(mine.sum())
        other += int((~mine & anyone).sum())
        none += int((~mine & ~anyone).sum())
    out["geom_centers"] = {"own": own, "other": other, "none": none}
    for kind in ("instances", "fx_points", "items"):
        own = other = none = 0
        for q in quads:
            pts = [(p[0] if kind == "instances" else p) for p in getattr(q, kind)]
            if not pts:
                continue
            pts = np.array(pts, float)
            mine = WL.inside_many(q.region, pts)
            anyone = np.zeros(len(pts), bool)
            for r in quads:
                if r is not q:
                    anyone |= WL.inside_many(r.region, pts)
            own += int(mine.sum())
            other += int((~mine & anyone).sum())
            none += int((~mine & ~anyone).sum())
        out[kind] = {"own": own, "other": other, "none": none}
    # coverage and adjacency on a grid over the collision bounds
    n = 192
    xs = np.linspace(col.min[0], col.max[0], n)
    zs = np.linspace(col.min[2], col.max[2], n)
    ys = np.linspace(col.min[1], col.max[1], 5)[1:-1]
    gx, gy, gz = np.meshgrid(xs, ys, zs, indexing="ij")
    pts = np.stack([gx.ravel(), gy.ravel(), gz.ravel()], 1)
    masks = [WL.inside_many(q.region, pts).reshape(gx.shape) for q in quads]
    count = sum(m.astype(int) for m in masks)
    out["coverage"] = {"none": float((count == 0).mean()), "one": float((count == 1).mean()),
                       "several": float((count > 1).mean()), "max_overlap": int(count.max())}

    def grow(m):
        g = m.copy()
        g[1:] |= m[:-1]
        g[:-1] |= m[1:]
        g[:, :, 1:] |= m[:, :, :-1]
        g[:, :, :-1] |= m[:, :, 1:]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        return g
    grown = [grow(m) for m in masks]
    touch = {}
    for i, a in enumerate(quads):
        for j, b in enumerate(quads):
            if i < j:
                shared = int((grown[i] & masks[j]).sum() + (grown[j] & masks[i]).sum())
                if shared:
                    touch[(a.index, b.index)] = shared
    listed = {(q.index, nb) for q in quads for nb in q.neighbors if nb is not None}
    sym = sum(1 for a, b in listed if (b, a) in listed)
    und = {tuple(sorted(x)) for x in listed}
    out["adjacency"] = {"listed_directed": len(listed), "listed_symmetric": sym, "listed_pairs": len(und),
                        "touching_pairs": len(touch), "listed_and_touching": len(und & set(touch)),
                        "listed_not_touching": len(und - set(touch)), "touching_not_listed": len(set(touch) - und)}
    # per quadrant: are the listed neighbours the ones sharing the most boundary?
    top = total = 0
    for q in quads:
        cand = sorted(((v, (b if a == q.index else a)) for (a, b), v in touch.items() if q.index in (a, b)),
                      reverse=True)
        want = {x for _, x in cand[:len(q.neighbors)]}
        top += len(want & set(q.neighbors))
        total += len(q.neighbors)
    out["adjacency"]["neighbours_among_most_shared"] = [top, total]
    return out


def measure_level(rel, release, name, geometry, cache):
    world = release.world(name)
    lv = {"level": name, "set": (world.name or "").replace("\\", "/").split("/")[-1].lower(),
          "collision": (world.collision_name or "").replace("\\", "/").split("/")[-1].lower(),
          "textures": (world.tex_name or "").replace("\\", "/").split("/")[-1].lower(),
          "world_id": world.id, "world_bytes": world.pk.size, "world_data_bytes": len(world.pk.d),
          "counts": world.counts, "nav_points": len(world.nav_points()), "quads": []}
    vols = world.volumes()
    vb = []
    for v in vols:
        if v["shape"] == 0 and v["bsp"]:
            try:
                vb.append(len(WL.bspn0_read(world.pk, v["bsp"])))
            except WL.WorldError:
                vb.append(None)
    lv["volumes"] = {"sphere": sum(1 for v in vols if v["shape"] == 1), "bsp": len(vb),
                     "bsp_nodes": [x for x in vb if x], "types": {}}
    for v in vols:
        lv["volumes"]["types"][str(v["type"])] = lv["volumes"]["types"].get(str(v["type"]), 0) + 1
    lv["volume_grid"] = world.volume_grid()
    packs = {}
    for q in world.quads:
        rec = {"index": q.index, "id": q.id, "pack_id": q.pack_id & 0xFFFF, "num_geoms": q.num_geoms,
               "hub": q.region is None, "neighbors": q.neighbors, "instances": len(q.instances),
               "instance_records": len({r for _, r in q.instances}), "fx_points": len(q.fx_points),
               "items": len(q.items), "region": region_stats(q.region) if q.region else None}
        key = (lv["set"], q.pack_id)
        if key not in cache:
            pk = WL.quad_pack(release, world, q)
            if pk is None:
                cache[key] = None
            elif geometry:
                cache[key] = pack_stats(pk)
            else:
                cache[key] = {"bytes": pk.size, "data_bytes": len(pk.d)}
        rec["pack"] = {k: v for k, v in (cache[key] or {}).items() if k not in ("centers", "ids")} or None
        packs[q.index] = cache[key]
        lv["quads"].append(rec)
    ckey = ("col", lv["collision"])
    if ckey not in cache:
        col = release.collision(lv["collision"])
        cache[ckey] = (col, collision_stats(col))
    col, lv["collision_stats"] = cache[ckey]
    tkey = ("tex", lv["textures"])
    if tkey not in cache:
        pk = release.pack(lv["textures"], WL.T_WORLDTEX)
        cache[tkey] = {"bytes": pk.size, "textures": pk.raw.h["num_textures"], "shaders": pk.raw.h["num_shaders"]}
    lv["texture_pack"] = cache[tkey]
    if geometry:
        lv["checks"] = region_checks(world, packs, col)
        ids = [i for st in packs.values() if st for i in st.get("ids", []) if i is not None]
        lv["geometry_ids"] = {"count": len(ids), "distinct": len(set(ids))}
    return lv


# ------------------------------------------------------------------------------------------
# tables

def active_sets(lv, key):
    """Per streamed quadrant: itself + its neighbours + the hub, summed over `key` of the packs."""
    q = {x["index"]: x for x in lv["quads"]}
    hub = sum((x["pack"] or {}).get(key, 0) for x in lv["quads"] if x["hub"])
    out = []
    for x in lv["quads"]:
        if x["hub"]:
            continue
        members = {x["index"]} | {n for n in x["neighbors"] if n is not None}
        out.append(hub + sum((q[m]["pack"] or {}).get(key, 0) for m in members))
    return out


def report(data):
    out = ["# World statistics\n", "Written by tools/world_stats.py; min / median / max unless a column says otherwise.\n"]
    for rel, levels in data.items():
        if not levels:
            continue
        out.append(f"\n## {rel}\n")
        sets = {}
        for lv in levels:
            sets.setdefault(lv["set"], []).append(lv)
        streamed = {k: v for k, v in sets.items() if len(v[0]["quads"]) > 2}
        single = {k: v for k, v in sets.items() if k not in streamed}

        rows = []
        for name, lvs in sorted(streamed.items()):
            lv = lvs[0]
            qs = [q for q in lv["quads"] if not q["hub"]]
            rows.append([name, len(lvs), len(lv["quads"]), mmm([len(q["neighbors"]) for q in qs]),
                         mmm([q["region"]["nodes"] for q in qs]), mmm([q["region"]["planes"] for q in qs]),
                         f"{sum(1 for q in qs if q['region']['convex'])}/{len(qs)}",
                         f"{lv['texture_pack']['textures']} / {lv['texture_pack']['bytes']:,}",
                         f"{lv['collision']} {lv['collision_stats']['bytes']:,}"])
        out.append("### Streamed sets\n")
        out.append(table(["set", "levels", "quadrants (with hub)", "neighbours", "region nodes", "region planes",
                          "convex regions", "world textures / bytes", "collision pack bytes"], rows))

        def quad_table(title, pick, sets_):
            rows = []
            allq = []
            for name, lvs in sorted(sets_.items()):
                qs = [q for q in lvs[0]["quads"] if pick(q) and q["pack"]]
                allq += qs
                if qs:
                    rows.append(row(name, qs))
            if len(rows) > 1:
                rows.append(row("**all**", allq))
            if rows:
                out.append(f"### {title}\n")
                out.append(table(["set", "packs", "pack bytes", "geometries", "triangles", "vertices", "groups",
                                  "textures used", "most vertices in a level", "triangles, all LODs"], rows))

        def row(name, qs):
            g = lambda k: mmm([q["pack"].get(k) for q in qs])  # noqa: E731
            return [name, len(qs), g("bytes"), g("geoms"), g("tris"), g("verts"), g("groups"), g("textures"),
                    max((q["pack"].get("max_level_verts", 0) for q in qs), default=0), g("tris_all_lods")]

        quad_table("Per quadrant pack (streamed quadrants)", lambda q: not q["hub"], streamed)
        quad_table("The hub (world quadrant) packs", lambda q: q["hub"], streamed)
        quad_table("Single-quadrant worlds (battle arenas): every pack", lambda q: True, single)

        rows = []
        for name, lvs in sorted(streamed.items()):
            lv = lvs[0]
            rows.append([name, mmm([len(x["neighbors"]) + 1 for x in lv["quads"] if not x["hub"]]),
                         mmm(active_sets(lv, "bytes")), mmm(active_sets(lv, "tris")), mmm(active_sets(lv, "verts")),
                         mmm(active_sets(lv, "geoms")),
                         f"{sum((q['pack'] or {}).get('bytes', 0) for q in lv['quads']):,}",
                         f"{sum((q['pack'] or {}).get('tris', 0) for q in lv['quads']):,}"])
        out.append("### Active set: a quadrant, its neighbours and the hub (one camera)\n")
        out.append(table(["set", "streamed packs", "bytes", "triangles", "vertices", "geometries",
                          "whole set bytes", "whole set triangles"], rows))

        rows = []
        for lv in sorted(levels, key=lambda x: (x["set"], x["level"])):
            c = lv["counts"]
            qs = lv["quads"]
            rows.append([lv["level"], lv["set"], f"{lv['world_bytes']:,}", mmm([q["instances"] for q in qs]),
                         sum(q["instances"] for q in qs), mmm([q["fx_points"] for q in qs]),
                         mmm([q["items"] for q in qs]), c["tileCars"], c["tileCreatures"], c["tilePaths"],
                         c["tileLights"], c["speakers"], c["teleporters"], lv["nav_points"],
                         f"{c['volumes']} ({lv['volumes']['bsp']} BSP)", c["variables"], c["timers"]])
        out.append("### Per level: the world pack's objects\n")
        out.append(table(["level", "set", "world pack bytes", "instances per quadrant", "instances", "fx points",
                          "items", "cars", "creatures", "movers", "lights", "speakers", "teleporters", "nav points",
                          "volumes", "variables", "timers"], rows))

        rows, seen = [], set()
        for lv in sorted(levels, key=lambda x: x["collision"]):
            if lv["collision"] in seen:
                continue
            seen.add(lv["collision"])
            c = lv["collision_stats"]
            rows.append([lv["collision"], f"{c['bytes']:,}", "x".join(map(str, c["grid"])),
                         " x ".join(f"{v:.0f}" for v in c["cell_size"]), f"{c['cells']} ({c['squares_empty']} empty squares)",
                         mmm(c["nodes"]), f"{sum(c['nodes']):,}", mmm(c["depths"]), mmm(c["edges_per_cell"]),
                         f"{c['num_edges']:,}", c["mode"]])
        out.append("### Collision packs\n")
        out.append(table(["pack", "bytes", "grid x,y,z", "square size", "cells", "BSP nodes per cell", "nodes",
                          "tree depth", "edges per cell", "edges", "mode"], rows))

        rows = []
        for name, lvs in sorted(streamed.items()):
            ch = lvs[0].get("checks")
            if not ch:
                continue
            f = lambda d: f"{d['own']} / {d['other']} / {d['none']}"  # noqa: E731
            a, cv = ch["adjacency"], ch["coverage"]
            rows.append([name, f(ch["geom_centers"]), f(ch["instances"]), f(ch["fx_points"]), f(ch["items"]),
                         f"{cv['none']:.0%} / {cv['one']:.0%} / {cv['several']:.0%} (max {cv['max_overlap']})",
                         f"{a['listed_pairs']} ({a['listed_symmetric']}/{a['listed_directed']} two-way)",
                         a["touching_pairs"], a["listed_not_touching"],
                         "{}/{}".format(*a["neighbours_among_most_shared"])])
        if rows:
            out.append("### Regions: what they hold and how they meet (first level of each set)\n")
            out.append("own / other / none: in its own quadrant's region, only in another's, in no region. "
                       "Coverage: share of grid points over the collision bounds in no, one, several regions.\n")
            out.append(table(["set", "geometry centres", "instances", "fx points", "items", "coverage",
                              "neighbour pairs listed", "pairs touching", "listed, not touching",
                              "listed among the most-shared borders"], rows))
        nodes = [n for lv in levels for n in lv["volumes"]["bsp_nodes"]]
        out.append(f"Volume BSPs over all levels: {len(nodes)} trees, nodes {mmm(nodes)}.\n")
        ids = [(lv["level"], lv["geometry_ids"]) for lv in levels if lv.get("geometry_ids")]
        dup = [f"{n} ({g['count'] - g['distinct']})" for n, g in ids if g["count"] != g["distinct"]]
        out.append(f"Geometry ids used by two quadrant packs of one level: {', '.join(dup) if dup else 'none'}.\n")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rel", default=",".join(WL.RELEASES))
    ap.add_argument("--only", help="levels (archive names), comma separated")
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--sources", help="a sources.cfg (default: the game's)")
    ap.add_argument("--no-geometry", action="store_true", help="skip decoding geometry (fast)")
    for r in WL.RELEASES:
        ap.add_argument("--" + r, help=f"the {r} release's data folder")
    a = ap.parse_args()
    src = WL.sources(a.sources)
    for r in WL.RELEASES:
        if getattr(a, r):
            src[r] = Path(getattr(a, r))
    data = {}
    for rel in a.rel.split(","):
        if rel not in src:
            print(f"{rel}: no data folder (sources.cfg or --{rel})", file=sys.stderr)
            continue
        release = WL.Release(rel, src[rel])
        names = release.world_names()
        if a.only:
            names = [n for n in names if n in a.only.lower().split(",")]
        cache, levels, t0 = {}, [], time.time()
        for n in names:
            try:
                levels.append(measure_level(rel, release, n, not a.no_geometry, cache))
            except (WL.WorldError, WL.XM.MediaError, WL.XG.GeomError) as err:
                print(f"{rel}/{n}: {err}", file=sys.stderr)
            release.drop(n)
        data[rel] = levels
        print(f"{rel}: {len(levels)} levels in {time.time() - t0:.1f} s", file=sys.stderr)
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "raw.json").write_text(json.dumps(data, indent=1))
    (a.out / "stats.md").write_text(report(data))
    print(a.out / "stats.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
