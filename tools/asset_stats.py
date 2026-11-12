#!/usr/bin/env python3
"""What the retail data uses, against the limits in docs-pending/asset-limits.md: texture sizes,
formats, mip counts and non-power-of-two sizes; sound rates, lengths, loops; banks and songs.

    python3 tools/asset_stats.py [--release gc,pc,ps2] [--json OUT]

Reads every archive of each release (xdl_media.release_dirs(), sources.cfg). Prints only counts
and extremes, no game data.
"""

import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_media as M  # noqa: E402

EXT = {"gc": ".xgc", "pc": ".xpc", "ps2": ".xps"}


def pow2(n):
    return n > 0 and n & (n - 1) == 0


def stats(rel, folder):
    st = {"textures": 0, "tex_formats": Counter(), "tex_max": (0, 0, ""), "tex_over_512": 0, "tex_npot": 0,
          "mips": Counter(), "tex_bytes_max_pack": (0, ""), "sounds": 0, "rates": Counter(),
          "snd_longest": (0, ""), "snd_loops": 0, "snd_bits": Counter(), "snd_per_pack_max": (0, ""),
          "snd_bytes_max_pack": (0, ""), "banks": 0, "bank_instr_max": (0, ""), "bank_bytes_max": (0, ""),
          "bank_rates": Counter(), "songs": 0, "song_tracks": Counter(), "song_bytes_max": (0, ""),
          "pack_bytes_max": (0, ""), "errors": 0}
    for p in sorted(Path(folder).rglob("*" + EXT[rel])):
        try:
            arc = M.Archive(p)
        except (M.MediaError, struct.error, OSError):
            continue
        for ent in arc.entries:
            raw = arc.pack_bytes(ent)
            where = f"{arc.name}/{ent['name']}"
            if len(raw) > st["pack_bytes_max"][0]:
                st["pack_bytes_max"] = (len(raw), where)
            try:
                pk = M.RawPack(rel, ent["type"], raw, ent["name"])
                tb = 0
                for t in M.scan_textures(pk):
                    st["textures"] += 1
                    st["tex_formats"][t["fmt"]] += 1
                    st["mips"][t["mips"]] += 1
                    if t["w"] * t["h"] > st["tex_max"][0] * st["tex_max"][1]:
                        st["tex_max"] = (t["w"], t["h"], where)
                    st["tex_over_512"] += t["w"] > 512 or t["h"] > 512
                    st["tex_npot"] += not (pow2(t["w"]) and pow2(t["h"]))
                    tb += sum(n for _, n, _, _ in t["levels"])
                if tb > st["tex_bytes_max_pack"][0]:
                    st["tex_bytes_max_pack"] = (tb, where)
                ss = M.scan_sounds(pk)
                sb = 0
                for s in ss:
                    st["sounds"] += 1
                    st["rates"][s["rate"]] += 1
                    st["snd_bits"][s["bits"]] += 1
                    n = (s["len"] // 16 - 1) * 28 if rel == "ps2" else s["len"] // 2
                    sb += n * 2
                    if s["rate"] and n / s["rate"] > st["snd_longest"][0]:
                        st["snd_longest"] = (round(n / s["rate"], 2), where)
                    st["snd_loops"] += s["loop"] != 0xFFFFFFFF
                if len(ss) > st["snd_per_pack_max"][0]:
                    st["snd_per_pack_max"] = (len(ss), where)
                if sb > st["snd_bytes_max_pack"][0]:
                    st["snd_bytes_max_pack"] = (sb, where)
                song, bank = M.scan_song(pk)
                if song:
                    st["songs"] += 1
                    o, n = song["smf"]
                    st["song_tracks"][struct.unpack_from(">H", raw, o + 10)[0]] += 1
                    if n > st["song_bytes_max"][0]:
                        st["song_bytes_max"] = (n, where)
                if bank:
                    st["banks"] += 1
                    if bank["count"] > st["bank_instr_max"][0]:
                        st["bank_instr_max"] = (bank["count"], where)
                    if bank["size"] > st["bank_bytes_max"][0]:
                        st["bank_bytes_max"] = (bank["size"], where)
                    for en in bank["entries"]:
                        st["bank_rates"][en["rate"]] += 1
            except (M.MediaError, struct.error, ValueError, IndexError, KeyError):
                st["errors"] += 1
    for k, v in st.items():
        if isinstance(v, Counter):
            st[k] = dict(sorted(v.items()))
    return st


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--release", default="gc,pc,ps2")
    ap.add_argument("--json")
    a = ap.parse_args()
    dirs = M.release_dirs()
    out = {}
    for rel in a.release.split(","):
        if rel in dirs:
            out[rel] = stats(rel, dirs[rel])
            print(rel, json.dumps(out[rel], default=str))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
