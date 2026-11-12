#!/usr/bin/env python3
"""How much of each pack kind's bytes the project files show typed (xdl_graph.py's nodes).

    python3 tools/typed_bytes.py [SURVEY_ROOT] [--rel gc] [--kinds world] [--tools DIR] [--by-schema]

Runs the extractor's cut and naming (no files written) over every pack and sums the node bytes
by kind: typed (a schema's object or array), trigger (a trigger file's parts, xdl_tac.py),
geometry (glTF), texture (images), raw. --tools: another checkout's tools/ (with its schemas/), for a before/after.
"""

import argparse
import collections
import importlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("survey", nargs="?", default=str(HERE.parents[1] / "build-survey"))
    ap.add_argument("--rel", default="gc")
    ap.add_argument("--kinds", default="world")
    ap.add_argument("--tools", default=str(HERE))
    ap.add_argument("--by-schema", action="store_true")
    a = ap.parse_args()
    sys.path.insert(0, a.tools)
    G = importlib.import_module("xdl_graph")
    S = G.XS.Schemas(Path(a.tools).parent / "schemas")
    tot = collections.Counter()
    by = collections.Counter()
    kinds = set(a.kinds.split(","))
    for rel in a.rel.split(","):
        for arch in G.survey_archives(a.survey, rel):
            man = json.loads((arch / "manifest.json").read_text())
            for ent in man["entries"]:
                if ent["type"] not in kinds:
                    continue
                ex = G.Extractor(G.Source(arch / ent["path"]), HERE / "_unused", S)
                ex.cut()
                ex.name()
                if hasattr(ex, "compose_triggers"):
                    ex.kinds = set((G.walkers().KINDS if G.walkers() else {}))
                    for nd in ex.nodes:
                        if nd.fixed or nd.kind != "raw" or nd.parent is None:
                            nd.file = "x"
                    ex.compose_triggers()
                for nd in ex.nodes:
                    n = nd.end - nd.start
                    k = "trigger" if getattr(nd, "tac", None) is not None else nd.kind
                    if k == "raw" and "/texture/" in (nd.id or ""):
                        k = "texture"           # the pack's textures (images, written as PNG)
                    tot[(rel, k)] += n
                    tot[(rel, "all")] += n
                    if a.by_schema:
                        by[(rel, nd.schema if nd.kind == "typed" else k if k != "raw" else (nd.label or "(raw)"))] += n
    for rel in a.rel.split(","):
        allb = tot[(rel, "all")] or 1
        print(f"{rel}: {allb:,} bytes in {a.kinds} packs")
        for k in ("typed", "trigger", "geometry", "texture", "raw"):
            print(f"  {k:9} {tot[(rel, k)]:12,}  {100 * tot[(rel, k)] / allb:5.1f}%")
        rest = allb - tot[(rel, "geometry")] - tot[(rel, "texture")]
        print(f"  without geometry and textures: {rest:,} bytes, typed or trigger "
              f"{100 * (tot[(rel, 'typed')] + tot[(rel, 'trigger')]) / (rest or 1):.1f}%")
        if a.by_schema:
            for (r, k), v in sorted(by.items(), key=lambda x: -x[1]):
                if r == rel:
                    print(f"    {k:28} {v:12,}  {100 * v / allb:5.1f}%")


if __name__ == "__main__":
    sys.exit(main())
