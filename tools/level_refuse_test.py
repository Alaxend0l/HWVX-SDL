#!/usr/bin/env python3
"""tools/level_refuse_test.py BUILD DATA OUT [CASE ...]

Crafted level files the game must refuse (src/game/Port_Level.c checks everything a shared
level file asks for: a level comes from other players' .xmod mods, so it is untrusted input).
Each case is a sample level (tools/level-samples) compiled, then changed the way a malicious
or broken file would be; the game loads its world with HWVX_LEVEL pointing at it and must log
"level: not applied: ..." and keep running (no crash, no Port_Fatal). The unchanged samples
("ok-*") must apply. One game at a time (tools/run_test.sh: force-killed by its own PID).

CASE names a few of them (default: all); --list lists them.
"""
import copy
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from xdl_level import read_jsonc  # noqa: E402
SECONDS = int(os.environ.get("SECONDS_RUN", "14"))


def part(d, pid):
    return next(p for p in d["parts"] if p["id"] == pid)


def first(d, kind):
    return next(p for p in d["parts"] if p["kind"] == kind)


def word(p, off, v):
    b = bytearray.fromhex(p["hex"])
    b[off:off + 4] = struct.pack("<I", v)
    p["hex"] = b.hex()


def linked_part(d, p, off):
    """The part the link at `off` of part p names."""
    for o, ln in p["links"]:
        if o == off and "part" in ln:
            return part(d, ln["part"])
    raise KeyError(off)


def trig_with(d, off):
    return next(p for p in d["parts"] if p["kind"] == "trigger" and any(o == off for o, _ in p["links"]))


def bad_trigger_type(d):
    word(first(d, "trigger"), 0, 200)


def bad_condition_type(d):
    t = trig_with(d, 0x0C)
    word(linked_part(d, t, 0x0C), 0, 99)


def bad_action_type(d):
    t = trig_with(d, 0x14)
    word(linked_part(d, t, 0x14), 0, 1000)


def unlinked_param(d):
    t = trig_with(d, 0x04) if any(o == 4 for p in d["parts"] if p["kind"] == "trigger" for o, _ in p["links"]) \
        else first(d, "trigger")
    t["links"] = [l for l in t["links"] if l[0] != 4]
    word(t, 4, 0x80001000)          # an arena handle: an arbitrary address


def unlinked_actions(d):
    t = trig_with(d, 0x14)
    t["links"] = [l for l in t["links"] if l[0] != 0x14]
    word(t, 0x14, 0x01000000)       # a static handle (Port_Fatal if followed)


def too_many_actions(d):
    t = trig_with(d, 0x14)
    word(t, 0x1C, 5000)


def patch_grown(d):
    # an element the level appends is its own (set), not a base record a patch may change
    d["patches"].append({"target": {"append": 0, "elem": 0, "add": 0}, "offset": 0x24, "hex": "00100080"})


def patch_part(d):
    p = first(d, "data")
    d["patches"].append({"target": {"part": p["id"]}, "offset": 0x400, "hex": "41" * 64})


def patch_pointer(d):
    # World.tileCars (0x0C) is a pointer: patches set numbers
    d["patches"].append({"target": {"root": "world", "index": 0, "steps": []}, "offset": 0x0C, "hex": "00100080"})


def patch_long(d):
    d["patches"].append({"target": {"root": "world", "index": 0, "steps": []}, "offset": 0xA0, "hex": "00" * 0x20000})


def append_size(d):
    d["appends"][0]["elemSize"] = 0x1000


def append_table(d):
    a = d["appends"][0]
    a.update(pointer=0x6C, count=0x68, elemSize=48)     # World volumes: not an array a level grows


def append_many(d):
    a = d["appends"][0]
    a["elems"] = [copy.deepcopy(a["elems"][0]) for _ in range(5000)]


def append_set_pointer(d):
    a = next(a for a in d["appends"] if a["pointer"] == 0x18)   # items of a quadrant
    a["elems"][0].setdefault("set", []).append([0x24, "00100080"])     # QuadItem.thing (null in the pack)


def many_variables(d):
    v = first(d, "variable")
    for i in range(40):
        n = copy.deepcopy(v)
        n["id"] += f"-x{i}"
        b = bytearray.fromhex(n["hex"])
        b[2] = 200 - i
        n["hex"] = b.hex()
        d["parts"].append(n)


def dup_variable_id(d):
    vs = [p for p in d["parts"] if p["kind"] == "variable"]
    b = bytearray.fromhex(vs[1]["hex"])
    b[2] = bytearray.fromhex(vs[0]["hex"])[2]
    vs[1]["hex"] = b.hex()


def timer_no_mode(d):
    t = first(d, "timer")
    t["links"] = [l for l in t["links"] if l[0] != 0x0C]
    word(t, 0x0C, 0)


def volume_bsp_null(d):
    v = first(d, "volume")
    word(v, 0x08, 0)                # shape BSP, no BSP


def bad_hex(d):
    first(d, "data")["hex"] += "zz"


CASES = {
    # name: (sample, change or None for "must apply")
    "ok-checkpoint-run": ("checkpoint-run", None),
    "ok-two-gates": ("two-gates", None),
    "ok-gear-grab": ("gear-grab", None),
    "trigger-type": ("checkpoint-run", bad_trigger_type),
    "condition-type": ("checkpoint-run", bad_condition_type),
    "action-type": ("checkpoint-run", bad_action_type),
    "unlinked-param": ("checkpoint-run", unlinked_param),
    "unlinked-actions": ("checkpoint-run", unlinked_actions),
    "too-many-actions": ("checkpoint-run", too_many_actions),
    "patch-grown": ("gear-grab", patch_grown),
    "patch-part": ("checkpoint-run", patch_part),
    "patch-pointer": ("checkpoint-run", patch_pointer),
    "patch-long": ("checkpoint-run", patch_long),
    "append-elemsize": ("two-gates", append_size),
    "append-table": ("two-gates", append_table),
    "append-many": ("gear-grab", append_many),
    "append-set-pointer": ("gear-grab", append_set_pointer),
    "variables-30": ("checkpoint-run", many_variables),
    "variable-id-taken": ("checkpoint-run", dup_variable_id),
    "timer-no-mode": ("two-gates", timer_no_mode),
    "volume-bsp-null": ("checkpoint-run", volume_bsp_null),
    "bad-hex": ("checkpoint-run", bad_hex),
}


def compiled(sample, out):
    """The sample made into a mod (its compiled level file, the mod's folder, its id and mode)."""
    mod = out / f"mod-{sample}"
    if not mod.exists():
        subprocess.run([sys.executable, str(HERE / "xdl_level.py"), "mod", str(HERE / "level-samples" / sample),
                        str(mod / "level")], check=True, stdout=subprocess.DEVNULL)
    f = next((mod / "level" / "level").glob("*.json"))
    doc = json.loads(f.read_text())
    lv = read_jsonc(HERE / "level-samples" / sample / "level.jsonc")
    return mod, doc, lv["mode"]


def main():
    args = sys.argv[1:]
    if "--list" in args:
        print("\n".join(CASES))
        return 0
    if len(args) < 3:
        print(__doc__)
        return 2
    build, data, out = args[0], args[1], Path(args[2]).resolve()
    names = args[3:] or list(CASES)
    out.mkdir(parents=True, exist_ok=True)
    fails = 0
    for name in names:
        sample, change = CASES[name]
        mod, doc, mode = compiled(sample, out)
        doc = copy.deepcopy(doc)
        if change is not None:
            change(doc)
        f = out / f"{name}.json"
        f.write_text(json.dumps(doc))
        env = {**os.environ, "FAST": "1"}
        r = subprocess.run([str(HERE / "run_test.sh"), build, data, str(out), name, str(SECONDS),
                            f"HWVX_MODS_DIR={mod}", f"HWVX_LEVEL={f}",
                            f"HWVX_START=race:mode={mode},world={doc['id']}",
                            "HWVX_AUTOINPUT=200:a,300:a,400:a"],  # (a mission waits for its briefing)
                           env=env, capture_output=True, text=True)
        log = (out / f"{name}.log").read_text(errors="replace")
        alive = "ran " in r.stdout
        refused = [l.split("level: not applied: ", 1)[1] for l in log.splitlines() if "level: not applied: " in l]
        applied = "level: " in log and " applied to " in log
        if change is None:
            ok = alive and applied and not refused
            why = "applied" if ok else f"alive={alive} applied={applied} refused={refused[:1]}"
        else:
            ok = alive and bool(refused) and not applied
            why = refused[0] if refused else f"alive={alive} applied={applied}"
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {why}")
        fails += not ok
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
