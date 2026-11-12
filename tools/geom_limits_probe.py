#!/usr/bin/env python3
"""How much geometry a model may have: a generated sphere (no game data) of growing vertex
counts put in the place of one group of a base car's geometry, encoded for each release by
tools/xdl_gltf.py (the encoder the .xdl source projects use), decoded back and compared.

    python3 tools/geom_limits_probe.py [--car car_7] [--sizes 1000,30000,40000,70000]

Prints per release and size: encoded or the encoder's refusal, the round trip's largest
position error, and the f32 position precision at a few distances. Scratch only; nothing is
written. docs-pending/asset-limits.md has the results.
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_gltf as XG  # noqa: E402
import xdl_media as M  # noqa: E402

EXT = {"gc": ".xgc", "pc": ".xpc"}


def sphere(n, radius):
    """About n vertices on a UV sphere: (positions f32 bytes, uv bytes, triangles)."""
    rings = max(2, int(np.sqrt(n / 2)))
    segs = max(3, n // rings)
    verts, tris = [], []
    for r in range(rings):
        th = np.pi * (r + 0.5) / rings
        for s in range(segs):
            ph = 2 * np.pi * s / segs
            p = radius * np.array([np.sin(th) * np.cos(ph), np.cos(th), np.sin(th) * np.sin(ph)], np.float32)
            verts.append((p, np.array([s / segs, r / rings], np.float32)))
    for r in range(rings - 1):
        for s in range(segs):
            a, b = r * segs + s, r * segs + (s + 1) % segs
            c, d = a + segs, b + segs
            tris += [(a, c, b), (b, c, d)]
    return verts, tris


def probe(rel, car, n, radius):
    folder = M.release_dirs()[rel]
    arc = M.Archive(str(next(Path(folder).rglob("HW_Cars" + EXT[rel]))))
    ent = next(e for e in arc.entries if e["name"] == car)
    pk = M.RawPack(rel, ent["type"], arc.pack_bytes(ent), car)
    blob = XG.Blob(pk.section("data"), set(pk.words("pointers")), rel)
    off = next(o for o in XG.scan(blob) if not XG.decode(blob, o, {})["skinned"])
    g = XG.decode(blob, off, {})
    gr = g["levels"][0]["groups"][0]
    proto = gr["verts"][0]
    vs, tris = sphere(n, radius)
    gr["verts"] = [(p.astype("<f4").tobytes(), proto[1], proto[2],
                    uv.astype("<f4").tobytes(), 0) for p, uv in vs]
    gr["tris"] = tris
    try:
        data, ptrs, ts, _ = XG.encode(g, rel)
    except (XG.GeomError, struct.error, OverflowError) as e:
        return len(vs), f"refused: {e}", None
    g2 = XG.decode(XG.Blob(data, set(ptrs), rel), 0, {s: 1 for s in ts})
    got = np.array([np.frombuffer(v[0], "<f4") for v in g2["levels"][0]["groups"][0]["verts"]])
    want = np.array([p for p, _ in vs])
    err = float(np.abs(np.sort(got, axis=0) - np.sort(want, axis=0)).max()) if len(got) == len(want) else None
    return len(vs), f"encoded, {len(g2['levels'][0]['groups'][0]['tris'])} triangles back", err


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--car", default="car_7")
    ap.add_argument("--sizes", default="1000,30000,40000,70000")
    ap.add_argument("--radius", type=float, default=2.0)
    a = ap.parse_args()
    for rel in ("gc", "pc"):
        if rel not in M.release_dirs():
            continue
        for n in map(int, a.sizes.split(",")):
            nv, what, err = probe(rel, a.car, n, a.radius)
            print(f"{rel} {nv} vertices: {what}" + (f"; largest position error {err:g}" if err is not None else ""))
    for d in (1.0, 100.0, 1000.0, 10000.0):
        print(f"f32 position step at {d:g} units: {np.spacing(np.float32(d)):.3g}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
