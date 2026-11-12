#!/usr/bin/env python3
"""tools/xdl_level.py: compiling and checking level projects.

    python3 tools/test_xdl_level.py [BASE_PROJECT]

BASE_PROJECT: ch1_l2's world pack extracted by xdl_graph.py (xdl_level.py base
'worlds_\\sp_\\chal1_\\ch1_l2' --data GC_DATA prints it); without one the checks against the
base world are skipped. Scratch files go to build-level-test/ (never committed).
"""

import json
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import xdl_level as LV  # noqa: E402

SAMPLE = HERE / "level-samples" / "checkpoint-run"
scratch = HERE.parent / "build-level-test"
base = sys.argv[1] if len(sys.argv) > 1 else None
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def variant(name, edit_level=None, edit_trigger=None):
    """A copy of the sample with an edit; its compile errors."""
    d = scratch / name
    if d.exists():
        shutil.rmtree(d)
    shutil.copytree(SAMPLE, d)
    if edit_level:
        lv = LV.read_jsonc(d / "level.jsonc")
        edit_level(lv)
        (d / "level.jsonc").write_text(json.dumps(lv))
    if edit_trigger:
        tname, fn = edit_trigger
        t = LV.read_jsonc(d / "triggers" / f"{tname}.jsonc")
        fn(t)
        (d / "triggers" / f"{tname}.jsonc").write_text(json.dumps(t))
    return LV.compile_project(d, base)


scratch.mkdir(parents=True, exist_ok=True)

# the sample compiles, the same twice
doc, errs = LV.compile_project(SAMPLE, base)
check("sample compiles", doc is not None and not errs, "; ".join(errs))
doc2, _ = LV.compile_project(SAMPLE, base)
check("deterministic", json.dumps(doc) == json.dumps(doc2))
parts = {p["id"]: p for p in doc["parts"]}
check("7 triggers", sum(p["kind"] == "trigger" for p in doc["parts"]) == 7)
check("2 variables", sum(p["kind"] == "variable" for p in doc["parts"]) == 2)
check("world path", doc["world"] == "worlds_\\sp_\\chal1_\\ch1_l2")

# a volume's bytes: sphere airbox, centre, radius squared
v = bytes.fromhex(parts["level://volumes/cp1"]["hex"])
check("volume enabled/shape/type", struct.unpack_from("<IIiI", v, 0) == (0, 1, 1, 0))
check("volume centre", struct.unpack_from("<fff", v, 0x10) == (82.0, 4.0, -780.0))
check("volume radiusSq", struct.unpack_from("<ff", v, 0x20) == (90.0, 8100.0))

# a trigger record: airbox_entered, its param and conditions linked as parts
t = parts["level://triggers/cp1"]
links = dict((o, l) for o, l in t["links"])
check("trigger param link", links.get(4) == {"part": "level://triggers/cp1/param"})
check("trigger conditions link", links.get(0xC) == {"part": "level://triggers/cp1/conditions"})
pm = parts["level://triggers/cp1/param"]
pl = dict((o, l) for o, l in pm["links"])
check("param: own volume", pl.get(4) == {"part": "level://volumes/cp1"})
check("param: base car as a walk", pl.get(12) == {"root": "world", "index": 0, "steps": [["p", 0xC]]})
cond = parts["level://triggers/cp1/conditions/0/param"]
check("condition: variable by its table entry",
      dict((o, l) for o, l in cond["links"]).get(4) == {"slot": "level://variables/progress/to"})
act = parts["level://triggers/cp1/actions"]
check("action count defaults to 1", struct.unpack_from("<I", bytes.fromhex(act["hex"]), 0x10)[0] == 1)

# base paths as pointer walks
check("walk: timers/0/to", LV.base_steps("World/0/timers/0/to", "t") ==
      {"root": "world", "index": 0, "steps": [["p", 0x54], ["p", 0]]})
check("walk: quads/16/contents/fxPoints/7", LV.base_steps("World/0/quads/16/contents/fxPoints/7", "t") ==
      {"root": "world", "index": 0, "steps": [["p", 0x24], ["a", 16 * 0x50], ["p", 0x30], ["p", 0x10],
                                              ["a", 7 * 0x40]]})
check("walk: trigger/15/actions/4/data/p0", LV.base_steps("trigger/15/actions/4/data/p0", "t") ==
      {"root": "trigger", "index": 15, "steps": [["p", 0x14], ["a", 4 * 0x18], ["p", 4], ["p", 0]]})

# the patch
check("patch: timer time", doc["patches"][0]["hex"] == struct.pack("<f", 40000).hex() and
      doc["patches"][0]["offset"] == 8)


# errors name the place
def has(errs, *words):
    return any(all(w in e for w in words) for e in errs)


_, e = variant("badlink", edit_trigger=("cp1", lambda t: t["param"].__setitem__("volumes", ["level://volumes/nope"])))
check("unknown own record", has(e, "triggers/cp1", "level://volumes/nope"), e)
_, e = variant("badworld", edit_trigger=("cp2", lambda t: t["param"].__setitem__(
    "cars", ["base://ch1_l3/world_0/World/0/tileCars/0"])))
check("link into another world", has(e, "not the level's world"), e)
_, e = variant("badkind", edit_trigger=("cp2", lambda t: t.__setitem__("type", "airbox_enterd")))
check("unknown trigger kind", has(e, "triggers/cp2", "airbox_enterd"), e)
_, e = variant("badfield", edit_level=lambda lv: lv["volumes"]["cp1"].__setitem__("radious", 3))
check("unknown field", has(e, "volumes/cp1", "radious"), e)
_, e = variant("badmode", edit_level=lambda lv: lv.__setitem__("extends", "base://mcp/track/3"))
check("extends a level of another mode", has(e, "extends", "challenge"), e)
_, e = variant("badpatch", edit_level=lambda lv: lv["patches"].__setitem__(
    "base://ch1_l2/world_0/World/0/timers/0/to", {"mode": 3}))
check("patch on a link field", has(e, "patches", "a link"), e)


def many(lv):
    lv["triggers"] = lv["triggers"] + [f"x{i}" for i in range(12)]


d = scratch / "toomany"
_, e = variant("toomany", edit_level=many)
for i in range(12):
    shutil.copy2(SAMPLE / "triggers" / "intro.jsonc", scratch / "toomany" / "triggers" / f"x{i}.jsonc")
_, e = LV.compile_project(scratch / "toomany", base)
check("trigger limit (level_start: 10 at once)", has(e, "level_start", "keeps 10"), e)

if base:
    _, e = variant("nobase", edit_trigger=("cp1", lambda t: t["param"].__setitem__(
        "cars", ["base://ch1_l2/world_0/World/0/tileCars/7"])))
    check("base record past its table", has(e, "tileCars has 1 entries"), e)
    _, e = variant("keepall", edit_level=lambda lv: lv.__setitem__("baseTriggers", "all"))
    check("base triggers kept count toward the limits", not e or has(e, "keeps"), e)
else:
    print("skip base checks (no base project given)")

# errors as records: file, place, message (the editor's list)
recs = LV.error_records([
    "level.jsonc/patches/base://ch1_l2/world_0/World/0/timers/0/to/time: a number",
    "level://triggers/cp1/param +0x4: level://volumes/nope: the level has nothing of that name",
    "level.jsonc/volumes/cp1/radious: not a field of WorldVolume",
])
check("error records: a patch's field", recs[0] == {"file": "level.jsonc", "path": "patches/base://ch1_l2/world_0/World/0/timers/0/to/time", "message": "a number"}, recs[0])
check("error records: a trigger's part", recs[1]["file"] == "triggers/cp1.jsonc" and recs[1]["path"] == "param", recs[1])
check("error records: a field", recs[2]["path"] == "volumes/cp1/radious", recs[2])

# starts: the cars' box (retail's envelope: x -5..5, y -1.4..5.25, z -14..13)
import math
BOX = ([-5.0, -1.4, -14.0], [5.0, 5.25, 13.0])
a = LV.start_corners([0, 0, 0], 0.0, BOX)
check("start box: 10 wide, 27 long", sorted({round(x, 3) for x, _ in a[0]}) == [-5.0, 5.0] and
      sorted({round(z, 3) for _, z in a[0]}) == [-14.0, 13.0], a)
check("start boxes side by side, 9 apart: overlap", LV.boxes_overlap(a, LV.start_corners([9, 0, 0], 0.0, BOX)))
check("start boxes side by side, 11 apart: clear", not LV.boxes_overlap(a, LV.start_corners([11, 0, 0], 0.0, BOX)))
check("start boxes nose to tail, 28 apart: clear", not LV.boxes_overlap(a, LV.start_corners([0, 0, 28], 0.0, BOX)))
check("a turned box: 11 apart now overlaps", LV.boxes_overlap(a, LV.start_corners([11, 0, 0], math.pi / 2, BOX)))
check("one above the other: clear", not LV.boxes_overlap(a, LV.start_corners([0, 8, 0], 0.0, BOX)))
envf = scratch / "spawnbox.json"
envf.write_text(json.dumps({"box": {"cars": 33, "min": BOX[0], "max": BOX[1]}}))
check("spawn box file", LV.read_spawn_box(envf) == BOX)

# a mod folder
out = scratch / "mod"
if out.exists():
    shutil.rmtree(out)
LV.make_mod(SAMPLE, out, doc)
mod = LV.read_jsonc(out / "mod.jsonc")
tr = mod["tracks"]["uid://checkpointrun"]
check("mod: track entry", tr["extends"] == "base://mcp/track/39" and tr["level"] == "level/checkpointrun.json")
check("mod: level file", (out / tr["level"]).exists())

print("FAILED" if fails else "all passed", f"({fails} failures)" if fails else "")
sys.exit(1 if fails else 0)
