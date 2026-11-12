#!/usr/bin/env python3
"""A retail level's script at a glance: the world's records' counts and its triggers, one line
each (kind, parameters, conditions, actions with their delays), links shortened to the world's
paths. For learning how a mode's levels are made (tools/xdl_level.py base extracts the world).

    level_summary.py WORLD --data DATA [--cache DIR] [--width N]

WORLD as the MCP names it (worlds_\\sp_\\chal1_\\ch1_l2) or its last part (ch1_l2, bat1_l1, world1).
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import xdl_graph as GR  # noqa: E402
import xdl_level as LV  # noqa: E402


def short(v, pre, width):
    s = json.dumps(v).replace(pre, "")
    return s if len(s) <= width else s[:width] + "…"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("world")
    ap.add_argument("--data", required=True)
    ap.add_argument("--cache")
    ap.add_argument("--width", type=int, default=160)
    a = ap.parse_args()
    proj = Path(LV.base_project(a.world, a.data, LV.cache_dir(a)))
    pj = GR.read_jsonc(proj / "pack.jsonc")
    pre = f"base://{pj['archive']}/world_0/"
    w = GR.read_jsonc(proj / "World" / "0.jsonc")
    print(f"{pj['archive']}: " + ", ".join(f"{k[3:]} {v}" for k, v in w.items() if k.startswith("num")))
    n = pj["pack"]["header"].get("num_triggers", 0)
    for i in range(n):
        f = proj / "trigger" / f"{i}.jsonc"
        if not f.exists():
            continue
        t = GR.read_jsonc(f)
        print(f"{i:3} {t['type']} {short(t.get('param'), pre, a.width)}" + (f" mode {t['mode']}" if t.get("mode") else ""))
        for c in t.get("conditions") or []:
            print(f"      if {c['type']} {short(c.get('param'), pre, a.width)}")
        for x in t.get("actions") or []:
            d = f" +{x['delayInit']}ms" if x.get("delayInit") else ""
            cnt = "" if x.get("count") == 1 else f" x{x.get('count')}"
            print(f"      do {x['type']}{d}{cnt} {short(x.get('data'), pre, a.width)}")
        for x in t.get("elseActions") or []:
            print(f"      else {x['type']} {short(x.get('data'), pre, a.width)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
