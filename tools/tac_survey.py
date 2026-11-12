#!/usr/bin/env python3
"""The triggers of every retail world pack, decoded by the layouts in tools/tac_layouts.py.

    python3 tools/tac_survey.py [SURVEY_ROOT] [--rel gc,pc,ps2] [--show KIND] [--dump N]

Per trigger kind and action kind: how many there are, how many decode cleanly (scalars off
pointer slots, links on them, the list targets in the world array they should be in, the
decoded length within the node), the values each scalar takes, and the leftover bytes.
A research tool: it reads `hwvx-x extract` trees (HWVX-SDL/build-survey by default).
"""

import argparse
import bisect
import collections
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import tac_layouts as TL  # noqa: E402
import xdl_graph as XGR  # noqa: E402


class Pack:
    def __init__(self, d):
        self.src = XGR.Source(d)
        self.blob = self.src.blob
        self.e = self.src.e
        self.ptr = self.src.ptr
        self.targets = sorted(set(v for v in self.ptr.values()))
        self.regions = TL.world_regions(self)

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.blob, o)[0]

    def extent(self, o):
        """Bytes from o to the next pointer target (a node's extent, roughly)."""
        i = bisect.bisect_right(self.targets, o)
        return (self.targets[i] if i < len(self.targets) else len(self.blob)) - o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("survey", nargs="?", default=str(HERE.parents[1] / "build-survey"))
    ap.add_argument("--rel", default="gc,pc,ps2")
    ap.add_argument("--show", help="print every instance of this kind (trigger:N or action:N)")
    ap.add_argument("--fails", action="store_true", help="print every failure")
    a = ap.parse_args()
    stats = collections.defaultdict(collections.Counter)
    values = collections.defaultdict(collections.Counter)
    fails = collections.defaultdict(list)
    for rel in a.rel.split(","):
        for arch in XGR.survey_archives(a.survey, rel):
            import json
            man = json.loads((arch / "manifest.json").read_text())
            for ent in man["entries"]:
                if ent["type"] != "world":
                    continue
                pk = Pack(arch / ent["path"])
                where = f"{rel} {arch.name}"
                for rec in TL.walk_triggers(pk):
                    key = f"{rec['what']}:{rec['kind']}"
                    stats[key]["n"] += 1
                    stats[key][rel] += 1
                    if rec.get("error"):
                        stats[key]["fail"] += 1
                        fails[key].append(f"{where} @0x{rec['off']:X}: {rec['error']}")
                        continue
                    stats[key]["ok"] += 1
                    left = rec.get("left")
                    if left:
                        stats[key]["left>0"] += 1
                        values[key + " left"][left] += 1
                    for k, v in rec.get("scalars", {}).items():
                        values[f"{key} {k}"][v] += 1
                    if a.show and a.show == key:
                        print(where, hex(rec["off"]), rec["value"], "left", left)
    names = {"trigger": TL.TRIGGER_NAMES, "cond": TL.TRIGGER_NAMES, "action": TL.ACTION_NAMES}
    print(f"{'kind':40} {'n':>5} {'ok':>5} {'fail':>5} {'left':>5}  gc/pc/ps2")
    for key in sorted(stats, key=lambda k: (k.split(':')[0], int(k.split(':')[1]))):
        s = stats[key]
        w, k = key.split(":")
        nm = names[w].get(int(k), "?")
        print(f"{key + ' ' + nm:40} {s['n']:5} {s['ok']:5} {s['fail']:5} {s['left>0']:5}  "
              f"{s['gc']}/{s['pc']}/{s['ps2']}")
    print()
    for key in sorted(values):
        c = values[key]
        print(f"  {key}: {dict(c.most_common(12))}{' ...' if len(c) > 12 else ''}")
    print()
    for key, fl in sorted(fails.items()):
        print(f"FAIL {key} x{len(fl)}")
        for f in fl[: (None if a.fails else 4)]:
            print("   ", f)


if __name__ == "__main__":
    sys.exit(main())
