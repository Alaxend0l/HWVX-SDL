#!/usr/bin/env python3
"""The values each field of the world's records takes, in every retail world pack.

    python3 tools/world_fields.py [SURVEY_ROOT] [--rel gc,pc,ps2] [--struct TileCar,...] [--words]

Walks each world pack's World (schemas/World.schema.json) down its counted tables (x-link
count: tileCars, quads -> contents -> instances...) and reports per struct how many records
there are and, per field, how many are zero, the distinct values (a few, with counts) and
whether it's a pointer slot. --words splits the unnamed byte arrays (pads) into u32 words, to
see what the exporter left in them. Reads `hwvx-x extract` trees (HWVX-SDL/build-survey).
For docs-pending/objects.md and the field names of World.h.
"""

import argparse
import collections
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_graph as G  # noqa: E402
import xdl_struct as XS  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("survey", nargs="?", default=str(HERE.parents[1] / "build-survey"))
    ap.add_argument("--rel", default="gc,pc,ps2")
    ap.add_argument("--struct", help="only these structs")
    ap.add_argument("--words", action="store_true")
    ap.add_argument("--top", type=int, default=6)
    a = ap.parse_args()
    S = XS.Schemas()
    only = set(a.struct.split(",")) if a.struct else None
    count = collections.Counter()
    vals = collections.defaultdict(collections.Counter)    # (struct, field) -> values
    slots = collections.Counter()
    seen = set()

    def fields(sname):
        out = []
        for key, p in S.struct(sname)["properties"].items():
            if "x-offset" not in p:
                continue
            base, dims = XS.parse_ctype(p["x-c-type"])
            off = int(p["x-offset"], 16)
            if "x-link" in p or base.startswith("PTR32") or base not in XS.PRIM:
                n = S.size_of(p["x-c-type"])
                out.append((key, off, "slot" if n == 4 else "blob", n, p))
                continue
            code, size, flt = XS.PRIM[base]
            n = 1
            for d in dims:
                n *= d
            if a.words and p.get("x-c-name", "").startswith("pad") and size * n % 4 == 0 and off % 4 == 0:
                for i in range(size * n // 4):
                    out.append((f"{key}+{4 * i}", off + 4 * i, "I", 4, p))
                continue
            for i in range(n):
                out.append((key if n == 1 else f"{key}[{i}]", off + i * size, code, size, p))
        return out

    def visit(src, sname, off, rel):
        if (id(src), off, sname) in seen or off + int(S.struct(sname)["x-size"], 16) > len(src.blob):
            return
        seen.add((id(src), off, sname))
        if only is None or sname in only:
            count[(rel, sname)] += 1
            for key, o, code, size, p in fields(sname):
                at = off + o
                if code == "slot":
                    v = "->" if at in src.ptr else src.u32(at)
                    if at in src.ptr:
                        slots[(sname, key)] += 1
                elif code == "blob":
                    v = src.blob[at:at + size].hex()
                else:
                    v = struct.unpack_from(src.e + code, src.blob, at)[0]
                    if isinstance(v, float):
                        v = float(XS.f32_text(v)) if v == v else "nan"
                vals[(sname, key)][v] += 1
        for key, p in S.struct(sname)["properties"].items():
            x = p.get("x-link")
            if not x or "x-offset" not in p:
                continue
            at = off + int(p["x-offset"], 16)
            t = src.ptr.get(at)
            if t is None:
                continue
            item = x.get("item") or G.strip_struct(x.get("target") or "")
            if x.get("count"):
                cp = S.struct(sname)["properties"][x["count"]]
                n = src.u32(off + int(cp["x-offset"], 16))
                if n > 0x10000:
                    continue
            else:
                n = 1
            if item.startswith("PTR32("):
                inner = G.strip_struct(item[6:-1])
                for i in range(n):
                    tt = src.ptr.get(t + 4 * i)
                    if tt is not None and S.struct(inner) is not None:
                        visit(src, inner, tt, rel)
            elif S.struct(item) is not None:
                size = int(S.struct(item)["x-size"], 16)
                for i in range(n):
                    visit(src, item, t + i * size, rel)

    rels = a.rel.split(",")
    for rel in rels:
        for arch in G.survey_archives(a.survey, rel):
            man = json.loads((arch / "manifest.json").read_text())
            for ent in man["entries"]:
                if ent["type"] != "world":
                    continue
                src = G.Source(arch / ent["path"])
                visit(src, "World", 0, rel)
                seen.clear()
    structs = sorted({s for _, s in count}, key=lambda s: -sum(count[(r, s)] for r in rels))
    for s in structs:
        n = sum(count[(r, s)] for r in rels)
        print(f"\n{s}: {n} records (" + ", ".join(f"{r} {count[(r, s)]}" for r in rels) + ")")
        for key, o, code, size, p in fields(s):
            c = vals[(s, key)]
            z = c.get(0, 0)
            top = ", ".join(f"{v}x{k}" if not isinstance(k, float) else f"{v}x{k:g}" for k, v in c.most_common(a.top))
            more = f" (+{len(c) - a.top} more)" if len(c) > a.top else ""
            print(f"  {key:18} {o:#05x} zero {z:6}  distinct {len(c):6}  {top}{more}")


if __name__ == "__main__":
    sys.exit(main())
