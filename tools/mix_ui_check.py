#!/usr/bin/env python3
"""A mixed build's interface art, from its manifest (<xdl>/mix-NAME/manifest.jsonl).

    tools/mix_ui_check.py MANIFEST --set gc|ps2 [--list]

Counts the interface textures (manifest lines with "ui": "art" or "font",
Port_Mix.c) by the release they came from, and checks the set rule: art only
from the set's release or, where the set has no copy, the base's own (a
fallback, listed with --list or when the base isn't the set); font pages from
the PC, else the set's, else the base's. Exit 1 when a texture breaks it.
"""
import argparse
import collections
import json
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("--set", required=True, choices=["gc", "ps2"])
    ap.add_argument("--base", default="gc", help="the base release of the packs (the recipe's first)")
    ap.add_argument("--list", action="store_true", help="list every fallback")
    a = ap.parse_args()

    counts = collections.Counter()
    fallbacks = collections.defaultdict(list)
    bad = []
    for line in open(a.manifest, encoding="utf-8"):
        e = json.loads(line)
        ui = e.get("ui")
        if ui is None:
            continue
        frm = e["from"]
        counts[(ui, frm)] += 1
        pack = "/".join(e["id"].split("/")[2:4])
        if ui == "art":
            if frm == a.set:
                continue
            if frm == a.base:
                fallbacks[pack].append(e["id"].rsplit("/", 1)[1])
            else:
                bad.append(e)
        elif frm not in ("pc", a.set, a.base):
            bad.append(e)
        elif frm != "pc":
            fallbacks[pack + " (font)"].append(e["id"].rsplit("/", 1)[1])

    for ui in ("art", "font"):
        row = ", ".join("%s %d" % (r, counts[(ui, r)]) for r in ("gc", "pc", "ps2") if counts[(ui, r)])
        print("%-5s %s" % (ui, row or "none"))
    n = sum(len(v) for v in fallbacks.values())
    print("not from the set (the base's own%s): %d in %d packs" % (", fonts not from the pc" , n, len(fallbacks)))
    if a.list or a.set != a.base:
        for pack, idx in sorted(fallbacks.items()):
            print("  %-40s %d: %s" % (pack, len(idx), " ".join(idx[:24]) + (" ..." if len(idx) > 24 else "")))
    for e in bad[:40]:
        print("BAD", e["id"], e["from"], e["ui"])
    if bad:
        print("%d interface textures break the set rule" % len(bad))
        return 1
    print("ok: no interface art from another release")
    return 0


if __name__ == "__main__":
    sys.exit(main())
