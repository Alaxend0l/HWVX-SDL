#!/usr/bin/env python3
"""tools/xdl_components.py: factoring, instances, overrides, extends, errors.

    python3 tools/test_xdl_components.py [SURVEY_ROOT] [--no-data]

Part 1 builds two small made-up packs and needs no game data. Part 2 factors real packs of each
release and is skipped when there is no survey (default HWVX-SDL/build-survey). Scratch files
go to <checkout>/build/test-components (never committed).
"""

import json
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_components as XC  # noqa: E402
import xdl_graph as GR  # noqa: E402
import xdl_struct as XS  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith("--")]
survey = Path(args[0]) if args else next(
    (d for d in (XS.ROOT / "build-survey", XS.ROOT.parent / "build-survey") if d.is_dir()), XS.ROOT / "build-survey")
scratch = XS.ROOT / "build" / "test-components"
S = XS.Schemas()
fails = 0
quiet = lambda *a: None  # noqa: E731


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def build(proj):
    out = scratch / "built" / proj.name
    if out.exists():
        shutil.rmtree(out)
    GR.Builder(proj, S).build(out)
    return {p.name: p.read_bytes() for p in out.iterdir()}


def raises(f):
    try:
        f()
    except GR.GraphError as e:
        return str(e)
    return None


def edit_json(p, f):
    d = XC.load(p)
    f(d)
    XC.dump(p, d)


if scratch.exists():
    shutil.rmtree(scratch)

# ------------------------------------------------------------------------------------------
# 1. made-up packs

def words(*w):
    return GR.hex_lines(struct.pack(f">{len(w)}I", *w))


def effect(pid, at, colour):
    """An 'EventSeq' at header/0/<at>: 16 bytes -> a 32-byte table (in its file) and a
    'Particle' (a file of its own) that points back to the effect and out to the pack's header."""
    r = f"{pid}/header/0/{at}"
    seq = {"format_version": 1, "id": r, "type": "EventSeq", "size": 16,
           "links": {"0x8": f"{r}/p8", "0xC": f"{r}/pc"}, "bytes": words(7, 1, 0, 0),
           "children": {f"{r}/p8": {"id": f"{r}/p8", "size": 32, "links": {"0x10": f"{r}/pc/o8"},
                                    "bytes": words(1, 2, 3, 4, 0, 6, 7, 8)}}}
    part = {"format_version": 1, "id": f"{r}/pc", "type": "Particle", "size": 16,
            "links": {"0x0": r, "0x4": f"{pid}/header/0"}, "bytes": words(0, 0, colour, 0x3F800000)}
    return [(f"header/0/{at}", seq, [r, f"{r}/p8"], "EventSeq"), (f"header/0/{at}/pc", part, [f"{r}/pc"], "Particle")]


def make_pack(root, num, effects):
    pid = f"base://Test/item_{num}"
    d = root / "Test" / f"item_{num}"
    head = {"format_version": 1, "id": f"{pid}/header/0", "size": 24,
            "links": {GR.hx(int(at[1:], 16)): f"{pid}/header/0/{at}" for at, c in effects},
            "bytes": words(len(effects), 0, 0, 0, 0, 0)}
    files = [("header/0", head, [f"{pid}/header/0"], None)]
    for at, c in effects:
        files += effect(pid, at, c)
    layout = []
    for stem, doc, ids, typ in files:
        XC.dump(d / (stem + ".jsonc"), doc)
        for i, nid in enumerate(ids):
            layout.append({"id": nid, "file": stem, "align": 4, **({"type": typ} if typ and i == 0 else {})})
    XC.dump(d / "pack.jsonc", {
        "format_version": 1, "id": pid, "release": "gc", "archive": "Test",
        "entry": {"type": "item", "id": num, "sub_id": 0, "path": f"item_{num}"},
        "pack": {"header": {}, "sections": [{"file": "pointers.bin"}, {"file": "data.bin"}]},
        "record_sizes": {"pointers.bin": 4, "data.bin": 1}, "files": ["data.bin", "pack.json", "pointers.bin"],
        "sounds": {}, "nodes": len(layout), "layout": layout})
    return d


def make(name, near=0):
    root = scratch / name
    a = make_pack(root, 1, [("p4", 0xFF0000FF), ("p14", 0xFF0000FF)])
    b = make_pack(root, 2, [("p4", 0xFF0000FF), ("p14", 0x00FF00FF)])       # p8: another colour
    plain = [build(a), build(b)]
    tot = XC.factor(root, near=near, log=quiet)
    return root, a, b, plain, tot


root, a, b, plain, tot = make("exact")
lib = XC.Library(root / "components")
kinds = sorted(lib.doc(u)["kind"] for u in lib.index)
check("factor: the effect used 3 times is one component; the odd one's particle... stays",
      kinds == ["EventSeq"] and tot["instances"] == 3, f"{kinds} {dict(tot)}")
inst = XC.load(a / "header/0/p4.jsonc")
check("factor: the instance is the uid and the pack's own link",
      inst["component"] in lib.index and inst["overrides"] == {"$/pc": {"links": {"0x4": "base://Test/item_1/header/0"}}},
      json.dumps(inst))
check("factor: the subgraph's files are gone", not (a / "header/0/p4").exists() and (b / "header/0/p14/pc.jsonc").exists())
comp = lib.doc(inst["component"])
check("component: links inside are relative, the link out is '@'",
      comp["nodes"]["$"]["links"] == {"0x8": "$/p8", "0xC": "$/pc"}
      and comp["nodes"]["$/p8"]["links"] == {"0x10": "$/pc/o8"}
      and comp["nodes"]["$/pc"]["links"] == {"0x0": "$", "0x4": "@"}, json.dumps(comp["nodes"]))
check("build: byte-identical after factoring", [build(a), build(b)] == plain)
tot2 = XC.factor(root, log=quiet)
check("factor: a second run changes nothing", tot2["instances"] == 0 and [build(a), build(b)] == plain, str(dict(tot2)))

# the component is defined once: an edit reaches every use
cfile = lib.dir / lib.index[inst["component"]]
edit_json(cfile, lambda d: d["nodes"]["$"].update(bytes=words(9, 1, 0, 0)))
XC.Library._open.clear()
da, db = build(a)["data.bin"], build(b)["data.bin"]
n = sum(x != y for x, y in zip(da + db, plain[0]["data.bin"] + plain[1]["data.bin"]))
check("edit: one changed byte in the component changes all 3 uses", n == 3, str(n))
edit_json(cfile, lambda d: d["nodes"]["$"].update(bytes=words(7, 1, 0, 0)))
XC.Library._open.clear()

# overrides on one instance
edit_json(a / "header/0/p14.jsonc", lambda d: d["overrides"].update({"$/pc": {**d["overrides"]["$/pc"], "patch": {"0x8": "00ff00ff"}}}))
da = build(a)["data.bin"]
diff = [i for i in range(len(da)) if da[i] != plain[0]["data.bin"][i]]
check("override: a patch changes that instance only (the colour word)", len(diff) == 2 and diff[0] % 4 == 0 and diff[1] == diff[0] + 1, str(diff))
check("override: the result is pack 2's odd effect", da[diff[0]:diff[0] + 4] == bytes.fromhex("00ff00ff"))

# a component that extends another
green = "uid://00000000000000aa"
XC.dump(lib.dir / "EventSeq/green.jsonc", {"format_version": 1, "uid": green, "kind": "EventSeq", "release": "gc",
                                           "extends": inst["component"],
                                           "overrides": {"$/pc": {"patch": {"0x8": "00ff00ff"}}, "$/p8": {"patch": {"0x0": "000000aa"}}}})
lib.index[green] = "EventSeq/green.jsonc"
lib.save()
XC.Library._open.clear()
edit_json(a / "header/0/p14.jsonc", lambda d: (d.update(component=green), d["overrides"]["$/pc"].pop("patch")))
da2 = build(a)["data.bin"]
diff2 = [i for i in range(len(da2)) if da2[i] != da[i]]
check("extends: a component over another one adds its own change", len(diff2) == 1 and da2[diff2[0]] == 0xAA, str(diff2))

# a new use, not in the layout
XC.dump(a / "header/0/p10.jsonc", {"format_version": 1, "id": "base://Test/item_1/header/0/p10", "component": green,
                                  "overrides": {"$/pc": {"links": {"0x4": "base://Test/item_1/header/0"}}}})
edit_json(a / "header/0.jsonc", lambda d: d["links"].update({"0x10": "base://Test/item_1/header/0/p10"}))
edit_json(a / "pack.jsonc", lambda d: d.update(append=["header/0/p10"]))
out = build(a)
da3 = out["data.bin"]
tgt = struct.unpack_from(">I", da3, 0x10)[0]
check("append: a new instance goes after the layout and links resolve", len(da3) == len(da2) + 64 and tgt == len(da2)
      and da3[tgt:tgt + 4] == bytes([0, 0, 0, 7]) and len(out["pointers.bin"]) == len(plain[0]["pointers.bin"]) + 4 * 6,
      f"{len(da3)} {tgt}")

# errors
edit_json(a / "header/0/p10.jsonc", lambda d: d.pop("overrides"))
msg = raises(lambda: build(a))
check("error: an unbound '@' names the file and the node", msg and "p10.jsonc" in msg and "$/pc" in msg and "not bound" in msg, str(msg))
edit_json(a / "header/0/p10.jsonc", lambda d: d.update(component="uid://0000000000000bad"))
msg = raises(lambda: build(a))
check("error: an unknown uid", msg and "uid://0000000000000bad" in msg, str(msg))
edit_json(a / "header/0/p10.jsonc", lambda d: d.update(component=green, overrides={"$/nope": {"patch": {"0x0": "00"}}}))
msg = raises(lambda: build(a))
check("error: an override of a node the component doesn't have", msg and "$/nope" in msg, str(msg))

# inline: back to plain files
n = XC.inline(b, S=S)
left = [p for p in b.rglob("*.jsonc") if "component" in XC.load(p) and p.name != "pack.jsonc"]
check("inline: instances written out, same build", n == 1 and not left and build(b) == plain[1], f"{n} {left}")

# near-duplicates
root, a, b, plain, tot = make("near", near=2)
inst = XC.load(b / "header/0/p14.jsonc")
check("near: the odd effect is an instance with one patched word",
      tot["instances"] == 4 and inst.get("overrides", {}).get("$/pc", {}).get("patch") == {"0x8": "00ff00ff"}, json.dumps(inst))
check("near: byte-identical", [build(a), build(b)] == plain)

# ------------------------------------------------------------------------------------------
# 2. real packs

SETS = [
    ("gc", ["HW_Cars/06_car_5", "HW_Cars/07_car_6", "HW_Cars/08_car_7", "HW_Items/20_item_24", "HW_Items/21_item_25",
            "HW_IPack/04_interface_5", "worlds_/mp_/battle_/bat6_l6/03_world_0", "worlds_/mp_/battle_/bat8_l1/03_world_0"]),
    ("pc", ["HW_Cars/06_car_5", "HW_Cars/07_car_6", "HW_Items/20_item_24"]),
    ("ps2", ["HW_Cars/06_car_5", "HW_Cars/07_car_6"]),
]
if "--no-data" in sys.argv or not survey.is_dir():
    print(f"(the real-pack part is skipped: --no-data, or no survey at {survey})")
else:
    for rel, packs in SETS:
        for near in (0, 100):
            root = scratch / f"real_{rel}_{near}"
            todo = []
            for pk in packs:
                src = survey / rel / pk
                if not src.exists():
                    c = sorted((survey / rel).rglob(Path(pk).name))
                    c = [x for x in c if Path(pk).parent.name in str(x)] or c
                    if not c:
                        continue
                    src = c[0]
                proj = root / src.relative_to(survey / rel)
                GR.Extractor(GR.Source(src), proj, S).run()
                todo.append((src, proj))
            if not todo:
                check(f"{rel}: packs present", False, "not in the survey")
                continue
            tot = XC.factor(root, near=near, log=quiet)
            bad = []
            for src, proj in todo:
                out = scratch / "built" / rel / proj.name
                shutil.rmtree(out, ignore_errors=True)
                GR.Builder(proj, S).build(out)
                bad += GR.compare(src, out)
            check(f"{rel} near={near}: {len(todo)} packs, {tot['components']} components ({tot['extending']} extending), "
                  f"{tot['instances']} instances ({tot['overridden']} with overrides), byte-identical",
                  not bad and tot["instances"] > 0 and tot["refused"] == 0, str(bad[:2]))
            if rel == "gc" and near:
                hud = XC.load(root / "HW_Cars/07_car_6/CarAInfo/0/hud.jsonc")
                hud5 = XC.load(root / "HW_Cars/06_car_5/CarAInfo/0/hud.jsonc")
                ov = hud.get("overrides") or hud5.get("overrides") or {}
                check("gc: a car's HUD is the shared HUD component plus its stat bars (a schematised node's doc override)",
                      "component" in hud and hud.get("component") == hud5.get("component")
                      and "stats" in ov.get("$", {}).get("doc", {}), json.dumps(hud)[:300])

print(f"\n{'all passed' if not fails else f'{fails} failed'}")
sys.exit(1 if fails else 0)
