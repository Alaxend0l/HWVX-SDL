#!/usr/bin/env python3
"""The viewer's protocol against bad or awkward requests (src/port/viewer*.c): one game run.

    python3 tools/test_viewer_safety.py --build build --data GC_DATA [--out build/viewer-safety]

Checks: rays with far-out or non-finite points answer (RayHitsTerrain hung on them) and a
clipped ray hits where the unclipped one does; startcheck/lanecheck/quadat with far points;
spawnbox's path policy; a level with appends (tile cars grown): record, pick and focus of
the moved array; count fields can't be raised; the authed client survives a silent or
wrong-token newcomer and is replaced by one with the token. Scratch files under --out.
"""
import argparse
import json
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer_client import Viewer, ViewerError  # noqa: E402

WORLD = "ch1_l2"
WORLD_PATH = "worlds_\\sp_\\chal1_\\ch1_l2"
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""), flush=True)
    fails += not cond


def err_of(v, cmd, **args):
    try:
        v.call(cmd, timeout=15, **args)
        return None
    except ViewerError as e:
        return str(e)


def raw_line(v, line, timeout=15):
    """A request as raw text (for numbers JSON can't write from Python: 1e400)."""
    v.sock.sendall(line.encode() + b"\n")
    v.sock.settimeout(timeout)
    while b"\n" not in v.buf:
        v.buf += v.sock.recv(1 << 16)
    out, v.buf = v.buf.split(b"\n", 1)
    return json.loads(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", default="build/viewer-safety")
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    v = Viewer.start(a.build, a.data, out / "viewer")
    try:
        v.call("world", name=WORLD, timeout=120)
        st = v.call("status")["world"]
        print("world", st.get("name"), flush=True)

        # 1. rays: far out, non-finite, clipped == unclipped
        t0 = time.time()
        r = v.call("raycast", timeout=15, **{"from": [-1e9, 0, 0], "to": [1e9, 0, 0]})
        check("raycast -1e9..1e9 answers", "hit" in r and time.time() - t0 < 10)
        r = v.call("raycast", timeout=15, **{"from": [3e7, 5e8, -2e9], "to": [-1e9, -4e8, 7e8]})
        check("raycast far diagonal answers", "hit" in r)
        r = raw_line(v, '{"seq":9001,"cmd":"raycast","from":[1e400,0,0],"to":[0,0,0]}')
        check("raycast with inf refused", r.get("ok") is False, json.dumps(r))
        start = v.call("records", table="tileCars")["records"][0]["pos"]
        down = v.call("raycast", down=[start[0], start[2]])
        check("raycast down at the start hits", down.get("hit") is True, json.dumps(down))
        if down.get("hit"):
            p = down["point"]
            near = v.call("raycast", **{"from": [p[0], p[1] + 50, p[2]], "to": [p[0], p[1] - 50, p[2]]})
            far = v.call("raycast", **{"from": [p[0], p[1] + 1e5, p[2]], "to": [p[0], p[1] - 1e5, p[2]]})
            check("a clipped ray hits where an inside one does",
                  near.get("hit") and far.get("hit") and abs(near["point"][1] - far["point"][1]) < 0.05,
                  f"{near.get('point')} vs {far.get('point')}")
        r = v.call("startcheck", timeout=15, starts=[{"pos": [1e9, 0, 1e9], "rotY": 0}, {"pos": start, "rotY": 0}])
        check("startcheck far point answers", len(r["starts"]) == 2 and r["starts"][0]["clear"] is False)
        r = v.call("lanecheck", timeout=15, starts=[{"pos": start, "rotY": 0}], length=1e12)
        check("lanecheck 1e12 long answers", len(r["lanes"]) == 1)
        r = v.call("quadat", timeout=15, pos=[1e30, -1e30, 1e30])
        check("quadat far point answers", "quad" in r)

        # 3. spawnbox's path
        txt = out / "notes.txt"
        txt.write_text("keep me")
        check("spawnbox: not a .json refused", err_of(v, "spawnbox", path=str(txt)) is not None)
        notjson = out / "notjson.json"
        notjson.write_text("keep me too")
        check("spawnbox: a non-JSON .json refused", err_of(v, "spawnbox", path=str(notjson)) is not None
              and notjson.read_text() == "keep me too")
        new = out / "box.json"
        new.unlink(missing_ok=True)
        check("spawnbox: a new .json written", err_of(v, "spawnbox", path=str(new)) is None and
              "box" in json.loads(new.read_text()))
        check("spawnbox: over its own JSON", err_of(v, "spawnbox", path=str(new)) is None)

        # 2. a level whose appends grow the tile cars
        n0 = len(v.call("records", table="tileCars")["records"])
        lvl = out / "appends.json"
        lvl.write_text(json.dumps({
            "format": "hwvx-level", "format_version": 1, "id": "uid://viewersafety", "name": "t",
            "world": WORLD_PATH, "baseTriggers": "all", "parts": [], "patches": [],
            "appends": [{"record": {"root": "world", "index": 0, "steps": []}, "pointer": 12, "count": 8,
                         "elemSize": 96, "elems": [{"fromIndex": 0, "set": []}]}]}))
        v.call("level", path=str(lvl))
        recs = v.call("records", table="tileCars")["records"]
        check("level: tile cars grown", len(recs) == n0 + 1, f"{n0} -> {len(recs)}")
        r0 = None
        try:
            r0 = v.call("record", path="World/0/tileCars/0")
        except ViewerError as e:
            check("record World/0/tileCars/0 after appends", False, str(e))
        if r0:
            check("record World/0/tileCars/0 after appends", r0.get("fields") is not None)
        e = err_of(v, "record", path=f"World/0/tileCars/{n0}")
        check("record of the appended car: says so", e is not None and "appends" in e, str(e))
        # pick the first tile car from just above it, looking down at it
        c = recs[0]["pos"]
        v.call("camera", mode="orbit", target=c, distance=30, pitch=89, yaw=0)
        size = v.call("status")
        pk = v.call("pick", x=size["width"] / 2, y=size["height"] / 2)
        check("pick a moved tile car: its path", pk.get("path", "").startswith("World/0/tileCars/"), json.dumps(pk))
        if "at" in pk:
            off = int(pk["at"][1:], 16)
            check("pick's offset is in the pack", off < 0x10000000)
        v.call("level", path=None)

        # count fields (the world's nodes are raw: World/0's numTileCars is @8:u32)
        e = err_of(v, "set", node="World/0", field="@8:u32", value=n0 + 5)
        check("set numTileCars above the data refused", e is not None and "count" in e, str(e))
        check("set numTileCars lower ok", err_of(v, "set", node="World/0", field="@8:u32", value=max(n0 - 1, 0)) is None)
        check("set numTileCars back ok", err_of(v, "set", node="World/0", field="@8:u32", value=n0) is None)
        check("set numTileCars above, still refused", err_of(v, "set", node="World/0", field="@8:u32", value=n0 + 1) is not None)

        # 4. connections: the authed client keeps its place
        silent = socket.create_connection(("127.0.0.1", v.port))
        time.sleep(0.3)
        check("a silent newcomer: the client still served", "frame" in v.call("status"))
        time.sleep(3.6)
        v.call("status")    # a poll or two
        silent.settimeout(2)
        try:
            got = silent.recv(4096)
        except OSError:
            got = b"(open)"
        check("the silent newcomer dropped after ~3 s", got != b"(open)", repr(got))
        silent.close()
        bad = socket.create_connection(("127.0.0.1", v.port))
        bad.sendall(b'{"cmd":"hello","token":"nope"}\n')
        time.sleep(0.3)
        check("a wrong token: the client still served", "frame" in v.call("status"))
        bad.close()
        v2 = Viewer.connect(v.port, v.token)
        check("a newcomer with the token is served", "frame" in v2.call("status"))
        try:
            v.call("status", timeout=5)
            check("... and the old client dropped", False)
        except (ViewerError, OSError):
            check("... and the old client dropped", True)
        v2.proc, v2.log, v.proc = v.proc, v.log, None     # v2 force-kills the game at the end
        v.sock.close()
        v = v2
    finally:
        log = getattr(v, "log", None)
        v.close()
        if log is not None:
            text = Path(log).read_text(errors="replace")
            for line in text.splitlines():
                if "viewer: a " in line or "Fatal" in line:
                    print("  log:", line)
    print("FAILED" if fails else "all ok", f"({fails} failed)" if fails else "")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
