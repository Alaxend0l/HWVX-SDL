#!/usr/bin/env python3
"""tools/xdl_level_pack.py: a level built into its own world pack.

    python3 tools/test_xdl_level_pack.py GC_DATA

The sample (tools/level-samples/checkpoint-run) with a box volume, a start, an item, an effect
point, an objective and one more trigger added, built (hwvx-x needed) and extracted again: the
records are in the pack's arrays and counts, the trigger table holds the level's triggers, the
build is cached, and errors name the place. Scratch: build-level-test/pack (never committed: it
holds the player's game data).
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import xdl_graph as GR  # noqa: E402
import xdl_level as LV  # noqa: E402
import xdl_level_pack as LP  # noqa: E402

if len(sys.argv) < 2:
    print("skip: no GC data given")
    sys.exit(0)
data = sys.argv[1]
scratch = HERE.parent / "build-level-test" / "pack"
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


if scratch.exists():
    shutil.rmtree(scratch)
proj = scratch / "project"
shutil.copytree(HERE / "level-samples" / "checkpoint-run", proj)
lv = LV.read_jsonc(proj / "level.jsonc")
pre = "base://ch1_l2/world_0/"
lv["volumes"]["gate"] = {"box": {"min": [150, 0, -830], "max": [170, 30, -730]}}
lv["starts"] = {"p2": {"pos": [-330, 3.61, -740], "rotY": 1.5708, "port": 2}}
lv["items"] = {"bonus": {"pos": [-2600, 28, -330], "quad": 20, "id": 4}}
lv["fxPoints"] = {"flame": {"pos": [0, 4, -760], "quad": 16, "template": pre + "World/0/quads/16/contents/fxPoints/7"}}
lv["strings"]["obj"] = "Reach the 3 checkpoints"
lv["objectives"] = {"main": {"text": "level://strings/obj", "kind": "counter", "value": "level://variables/progress/to"}}
lv["triggers"].append("gate")
(proj / "level.jsonc").write_text(json.dumps(lv, indent=2))
(proj / "triggers" / "gate.jsonc").write_text(json.dumps({
    "type": "airbox_entered", "param": {"volumes": ["level://volumes/gate"], "cars": [pre + "World/0/tileCars/0"]},
    "actions": [{"type": "timer_change_value", "data": {"op": "add", "timers": [pre + "World/0/timers/0"], "value": {"const": 10000}}}]}))

cache = scratch / "cache"
out, cached, name = LP.build_cached(proj, data, cache, scratch / "worlds")
check("built", out.exists() and not cached and name == "ch1_l2", out)
out2, cached2, _ = LP.build_cached(proj, data, cache, scratch / "worlds")
check("unchanged: the last build", cached2)

# extracted again
hx = LV.find_hwvx_x()
ext = scratch / "extract" / "ch1_l2"
subprocess.run([str(hx), "extract", str(out), str(ext)], check=True, capture_output=True)
pj = scratch / "proj-out"
GR.Extractor(GR.Source(ext / "00_world_0"), pj, __import__("xdl_struct").Schemas()).run()
w = GR.read_jsonc(pj / "World" / "0.jsonc")
check("volumes: 793 + 4", w["numVolumes"] == 797, w["numVolumes"])
check("variables: 3 + 2", w["numVariables"] == 5)
check("starts: 1 + 1", w["numTileCars"] == 2)
check("objectives: 1 + 1", w["numObjectives"] == 2)
check("an item in quadrant 20", GR.read_jsonc(pj / "World/0/quads/20/contents.jsonc")["numItems"] == 2)
check("an effect point in quadrant 16", GR.read_jsonc(pj / "World/0/quads/16/contents.jsonc")["numFxPoints"] == 28)
hdr = GR.read_jsonc(pj / "pack.jsonc")["pack"]["header"]
check("the trigger table: the level's 8", hdr["num_triggers"] == 8, hdr["num_triggers"])
vols = GR.read_jsonc(pj / "World/0/volumes.jsonc")["items"]
check("the box volume: a BSP", vols[796]["shape"] == "bsp" and vols[796]["bsp"], vols[796])
cars = GR.read_jsonc(pj / "World/0/tileCars.jsonc")["items"]
check("the start: port 2 where asked", cars[1]["port"] == 2 and abs(cars[1]["pos"]["z"] + 740) < 0.01, cars[1])
t0 = GR.read_jsonc(pj / "trigger" / "3.jsonc")
check("cp1's volume is the level's first", t0["param"]["volumes"] == [pre + "World/0/volumes/793"], t0.get("param"))
check("the timer patched to 40 s", GR.read_jsonc(pj / "World/0/timers/0/to.jsonc")["time"] == 40000)

# errors
lv["items"]["nowhere"] = {"pos": [0, 0, 0], "quad": 16, "id": 1}
(proj / "level.jsonc").write_text(json.dumps(lv, indent=2))
try:
    LP.build_cached(proj, data, cache, scratch / "worlds")
    check("an item in a quadrant without items: an error", False)
except LV.LevelError as ex:
    check("an item in a quadrant without items: an error", "items/nowhere" in str(ex), ex)

print("FAILED" if fails else "all passed", f"({fails} failures)" if fails else "")
sys.exit(1 if fails else 0)
