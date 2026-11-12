#!/usr/bin/env python3
"""Screenshots of whole worlds in the viewer (src/port/viewer_world.c), for checking them.

    viewer_world_shots.py --build build --data DIR --out OUT [--release gc] WORLD...

For each WORLD (a level's world name: ch1_l2, bat1_l1, world1): it loads it (`world`), saves
OUT/<world>-start.png (the start, as loaded), -overlays.png (regions, loaded quadrants, spawns,
paths), -collision.png (the collision pack's edges, from above) and -fly.png (flown along the
AI's route, nav point by nav point, the quadrants streaming; the shot halfway), and prints the
load time, the quadrants loaded before and after the flight, the frame rate with the frames shared, and the game's resident memory.
OUT/<release>-worlds.png puts each world's four shots in a row.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer_client import Viewer  # noqa: E402


def rss_mb(pid):
    try:
        return int(subprocess.check_output(["ps", "-o", "rss=", "-p", str(pid)]).strip()) / 1024.0
    except (subprocess.CalledProcessError, ValueError):
        return 0.0


def to_png(bmp, png, size=(640, 480)):
    from PIL import Image
    Image.open(bmp).resize(size).save(png)
    os.remove(bmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", default="build/world-shots")
    ap.add_argument("--release", default="gc", help="a name for the contact sheet")
    ap.add_argument("worlds", nargs="+")
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    v = Viewer.start(a.build, a.data, out / "viewer", window="800x600")
    rows = []
    try:
        for name in a.worlds:
            r = v.call("world", name=name, timeout=120)["world"]
            shots = []

            def shot(tag):
                p = out / f"{name}-{tag}.bmp"
                v.call("screenshot", path=str(p))
                to_png(p, out / f"{name}-{tag}.png")
                shots.append(out / f"{name}-{tag}.png")

            shot("start")
            st0 = v.call("status")["world"]
            v.call("overlays", regions=True, loaded=True, spawns=True, paths=True)
            shot("overlays")
            v.call("overlays", regions=False, loaded=False, spawns=False, paths=False, collision=True)
            v.call("camera", mode="orbit", pitch=80, distance=600)
            shot("collision")
            v.call("overlays", collision=False, loaded=True)
            # fly the AI's route (nav points), a frame or two at each: the quadrants stream
            n = st0["navPoints"]
            seen = set()
            v.call("camera", mode="fly")
            for k in range(0, n, max(1, n // 40)):
                v.call("goto", nav=k)
                v.call("camera", pitch=12)
                v.call("status")
                seen.update(v.call("status")["world"]["loadedQuads"])
            if n:
                v.call("goto", nav=n // 2)
                v.call("camera", pitch=12)
            else:
                v.call("goto")
            v.call("status")
            shot("fly")
            st1 = v.call("status")["world"]
            v.call("frames", width=800, height=600)
            f0 = v.call("status")["frame"]
            t0 = time.time()
            time.sleep(2.0)
            st = v.call("status")
            fps = (st["frame"] - f0) / (time.time() - t0)
            v.call("frames", on=False)
            v.call("overlays", loaded=False)
            v.call("camera", mode="orbit")
            mem = rss_mb(v.proc.pid)
            print(f"{name}: {r['world']}  load {r['loadMs']:.0f} ms, {r['quads']} quadrants; loaded at the start "
                  f"{st0['loadedQuads']} (camera in {st0['cameraQuad']}), after flying {st1['loadedQuads']} "
                  f"(camera in {st1['cameraQuad']}, most at once {st1['maxLoaded']}, {len(seen)} different along the route); {fps:.0f} fps with frames "
                  f"shared at 800x600; {mem:.0f} MB resident")
            rows.append(shots)
    finally:
        v.close()
    from PIL import Image
    sheet = Image.new("RGB", (4 * 320, 240 * len(rows)))
    for i, row in enumerate(rows):
        for j, p in enumerate(row):
            sheet.paste(Image.open(p).resize((320, 240)), (320 * j, 240 * i))
    sheet.save(out / f"{a.release}-worlds.png")
    print(out / f"{a.release}-worlds.png")


if __name__ == "__main__":
    main()
