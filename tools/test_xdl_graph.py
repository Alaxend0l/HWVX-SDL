#!/usr/bin/env python3
"""tools/xdl_graph.py: round trips of each pack kind, edits, ids, errors.

    python3 tools/test_xdl_graph.py [SURVEY_ROOT]      default: HWVX-SDL/build-survey

Scratch files go to HWVX-SDL/build-gltf/test-graph (game data: never committed). The whole
release is checked by `xdl_graph.py roundtrip`; this is the quick set and the edit cases.
"""

import json
import re
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_graph as GR  # noqa: E402
import xdl_struct as XS  # noqa: E402

survey = Path(sys.argv[1]) if len(sys.argv) > 1 else next(
    (d for d in (XS.ROOT / "build-survey", XS.ROOT.parent / "build-survey") if d.is_dir()), XS.ROOT / "build-survey")
scratch = next((d for d in (XS.ROOT / "build-gltf", XS.ROOT.parent / "build-gltf") if d.is_dir()),
               XS.ROOT / "build-gltf") / "test-graph"
S = XS.Schemas()
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def extract(rel, pack, name):
    src = survey / rel / pack
    proj = scratch / "proj" / rel / name
    s = GR.Extractor(GR.Source(src), proj, S).run()
    return src, proj, s


def build(proj, name):
    pj = GR.read_jsonc(proj / "pack.jsonc")
    out = scratch / "built" / name / pj["archive"] / proj.name
    if out.parent.exists():
        shutil.rmtree(out.parent)
    out.parent.mkdir(parents=True)
    # a manifest beside it, so the built pack can be extracted again
    man = {"platform": pj["release"], "entries": [dict(pj["entry"], path=proj.name)]}
    (out.parent / "manifest.json").write_text(json.dumps(man))
    bd = GR.Builder(proj, S)
    bd.build(out)
    return out, bd


def data_diff(a, b):
    x, y = (Path(a) / "data.bin").read_bytes(), (Path(b) / "data.bin").read_bytes()
    return [i for i in range(min(len(x), len(y))) if x[i] != y[i]] + ([-1] if len(x) != len(y) else [])


def body(p):
    """A file's text without its $schema line (a relative path that depends on the folder)."""
    return re.sub(r'^\s*"\$schema".*\n', "", Path(p).read_text(), flags=re.M)


def edit(path, f):
    p = Path(path)
    p.write_text(f(p.read_text()))


if scratch.exists():
    shutil.rmtree(scratch)

# 1. one pack of each kind, every release: byte-identical
CASES = [
    ("gc", "HW_Cars/06_car_5"), ("pc", "HW_Cars/06_car_5"), ("ps2", "HW_Cars/06_car_5"),
    ("gc", "HW_Cars/00_share_2"), ("gc", "HW_Items/20_item_24"), ("pc", "HW_Items/20_item_24"),
    ("gc", "HW_IPack/04_interface_5"), ("gc", "audio_/HWB_M12/00_audio_1"),
    ("gc", "worlds_/mp_/battle_/bat6_l6/03_world_0"), ("gc", "worlds_/mp_/battle_/bat6_l6/00_collision_0"),
    ("gc", "worlds_/mp_/battle_/bat8_l1/02_world_texture_0"),
    ("gc", "worlds_/geo_/w5_geo/01_geometry_0_q2_g1"),
]
for rel, pack in CASES:
    p = survey / rel / pack
    if not p.exists():
        cands = sorted((survey / rel).rglob(Path(pack).name))
        if not cands:
            check(f"{rel} {pack}: present", False, "not in the survey")
            continue
        p = cands[0]
        pack = str(p.relative_to(survey / rel))
    name = f"{rel}_{Path(pack).name}"
    src, proj, s = extract(rel, pack, name)
    out, bd = build(proj, name)
    d = GR.compare(src, out)
    check(f"{rel} {pack}: {s['nodes']} nodes, {s['files']} files, byte-identical", not d, str(d[:2]))

# 2. the car project: ids
src, proj, s = extract("gc", "HW_Cars/06_car_5", "car5")
car = XS.jsonc_loads((proj / "CarAInfo" / "0.jsonc").read_text())
check("ids: CarAInfo.engine is the CarAEngine file", car["engine"] == "base://HW_Cars/car_5/CarAInfo/0/engine"
      and (proj / "CarAInfo/0/engine.jsonc").exists() and (proj / "CarAInfo/0/engine.base.jsonc").exists())
check("ids: the same object by two paths has one id (aiScripts 0..2)", len(set(car["aiScripts"][:3])) == 1, str(car["aiScripts"][:3]))
check("ids: geometry and textures keep the pairing ids", car["reflectionGeom"] == "base://HW_Cars/car_5/geometry/14"
      and car["skidMarkTexture"] == "base://HW_Cars/share_2/texture/4")
pj = GR.read_jsonc(proj / "pack.jsonc")
ids = [e["id"] for e in pj["layout"]]
check("ids: unique, every one matches the link grammar",
      len(ids) == len(set(ids)) and all(XS.BASE_RE.match(i) for i in ids))
lens = max(len(i) for i in ids)
check("ids: repeats counted, not spelled out (longest id < 200 chars)", lens < 200, str(lens))
v, _ = XS.validator(S.top["CarAInfo"][0])
check("typed files validate against their schema", not list(v.iter_errors(car)))

# 3. a scalar edit changes 4 bytes
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('"maxRpm": 7400', '"maxRpm": 7500'))
out, _ = build(proj, "car5_rpm")
d = data_diff(src, out)
car_off = GR.Source(src).ptr[GR.Source(src).ptr[4]]
check("edit: maxRpm changes CarAInfo+0x1C only", d and min(d) >= car_off + 0x1C and max(d) < car_off + 0x20,
      str([hex(x) for x in d[:6]]))
check("edit: the relocation tables are unchanged", not [f for f, w in GR.compare(src, out) if f != "data.bin"])
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('"maxRpm": 7500', '"maxRpm": 7400'))

# 4. a link set to null drops its pointer slot
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('"rollSound": "base://HW_Cars/car_5/CarAInfo/0/rollSound"', '"rollSound": null'))
out, _ = build(proj, "car5_null")
np = lambda f: (Path(f) / "pointers.bin").stat().st_size // 4  # noqa: E731
check("edit: rollSound null -> one pointer slot fewer", np(out) == np(src) - 1, f"{np(src)} -> {np(out)}")
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('"rollSound": null', '"rollSound": "base://HW_Cars/car_5/CarAInfo/0/rollSound"'))

# 5. a geometry link to another geometry changes its ref value
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('"reflectionGeom": "base://HW_Cars/car_5/geometry/14"',
                                                     '"reflectionGeom": "base://HW_Cars/car_5/geometry/13"'))
out, _ = build(proj, "car5_geom")
a = dict(GR.Source(src).tables["ref"])
b = dict(GR.Source(out).tables["ref"])
ch = {o: (a[o], b[o]) for o in a if a[o] != b.get(o)}
check("edit: reflectionGeom 14 -> 13 changes one ref (index 14 -> 13)", list(ch.values()) == [(14, 13)], str(ch))
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('geometry/13"', 'geometry/14"'))

# 6. a raw record grows: everything after it moves and still links up
f = proj / "CarAInfo/0/rollSound.jsonc"
doc = GR.read_jsonc(f)
doc["size"] += 16
doc["bytes"] = doc["bytes"] + ["00000000 00000000 00000000 00000000"]
f.write_text(XS.dump_value(doc, 0) + "\n")
out, _ = build(proj, "car5_grow")
check("grow: data.bin 16 bytes longer (or more with alignment)",
      (out / "data.bin").stat().st_size >= (src / "data.bin").stat().st_size + 16)
proj2 = scratch / "proj" / "regrown"
GR.Extractor(GR.Source(out), proj2, S).run()
diffs = [str(p.relative_to(proj)) for p in proj.rglob("*.jsonc")
         if p.name != "pack.jsonc" and "rollSound" not in p.name and (proj2 / p.relative_to(proj)).exists()
         and body(proj2 / p.relative_to(proj)) != body(p)]
check("grow: the built pack extracts to the same objects (links resolve after the move)", not diffs, str(diffs[:3]))
check("grow: same pointer count", np(out) == np(src))
doc["size"] -= 16
doc["bytes"] = doc["bytes"][:-1]
f.write_text(XS.dump_value(doc, 0) + "\n")

# 7. errors name the file and the problem
edit(proj / "CarAInfo/0/engine.jsonc", lambda t: re.sub(r'("maxSteer": )[^,\n]*', r'\1"fast"', t, count=1)
     if '"maxSteer"' in t else t.replace('"format_version": 1', '"format_version": 1, "bogus": 1'))
try:
    build(proj, "car5_bad")
    msg = None
except GR.GraphError as e:
    msg = str(e)
check("error: a schema error names the file", msg and "engine.jsonc" in msg, str(msg))
edit(proj / "CarAInfo/0/engine.jsonc", lambda t: t.replace(', "bogus": 1', ''))
GR.Extractor(GR.Source(src), proj, S).run()
edit(proj / "CarAInfo/0.jsonc", lambda t: t.replace('"engine": "base://HW_Cars/car_5/CarAInfo/0/engine"',
                                                     '"engine": "base://HW_Cars/car_5/CarAInfo/0/nowhere"'))
try:
    build(proj, "car5_badlink")
    msg = None
except GR.GraphError as e:
    msg = str(e)
check("error: a link to nothing is refused", msg and "nowhere" in msg, str(msg))

GR.Extractor(GR.Source(src), proj, S).run()
edit(proj / "CarAInfo/0.jsonc", lambda t: re.sub(r'\n\s*"maxRpm": [^\n]*', "", t, count=1))
try:
    build(proj, "car5_nofield")
    msg = None
except GR.GraphError as e:
    msg = str(e)
check("error: a field left out of a typed file is refused (not built as 0)", msg and "maxRpm" in msg, str(msg))

# 7b. an edit that adds a relocation table the source lacks (a shader slot in car_5)
GR.Extractor(GR.Source(src), proj, S).run()
check("car_5 has no shader_relocs.bin", not (src / "shader_relocs.bin").exists())
edit(proj / "texture/0.jsonc", lambda t: t.replace('"links": {', '"shaders": {"0x10": 0},\n  "links": {', 1))
out, bd = build(proj, "car5_shader")
pj = json.loads((out / "pack.json").read_text())
sec = [x for x in pj["sections"] if x["file"] == "shader_relocs.bin"]
check("a new table is written, with its pack.json section",
      (out / "shader_relocs.bin").exists() and len((out / "shader_relocs.bin").read_bytes()) == 8
      and sec and sec[0]["records"] == 1, str(sec))

# 7c. links by offset into a geometry: refused once the geometry is re-encoded; bounds-checked
import xdl_gltf as XG  # noqa: E402
wsrc, wproj, _ = extract("gc", "worlds_/geo_/w3_geo/10_geometry_0_q5_g4", "w3q5")
olinks = [(p, m.group(0)) for p in wproj.rglob("*.jsonc") if not p.name.endswith(".xgeo.jsonc")
          for m in re.finditer(r'"base://[^"]*/geometry/1/o[0-9a-f]+"', p.read_text())]
check("w3_geo q5: links into geometry/1 by offset", olinks, "none found")
if olinks:
    p0, l0 = olinks[0]
    edit(p0, lambda t: t.replace(l0, re.sub(r"/o[0-9a-f]+", "/offffff", l0), 1))
    try:
        build(wproj, "w3q5_oob")
        msg = None
    except GR.GraphError as e:
        msg = str(e)
    check("error: an offset past the end of its object is refused", msg and "past the end" in msg, str(msg))
    edit(p0, lambda t: t.replace(re.sub(r"/o[0-9a-f]+", "/offffff", l0), l0, 1))
    gf = wproj / "geometry" / "1.gltf"
    gl, gb = XG.load(gf)
    a = gl["accessors"][gl["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    bv = gl["bufferViews"][a["bufferView"]]
    gb = bytearray(gb)
    o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    struct.pack_into("<f", gb, o, struct.unpack_from("<f", gb, o)[0] + 0.5)
    a["max"][0] = max(a["max"][0], struct.unpack_from("<f", gb, o)[0])
    XG.write(gl, bytes(gb), gf)
    try:
        build(wproj, "w3q5_edit")
        msg = None
    except GR.GraphError as e:
        msg = str(e)
    check("error: offset links into an edited (re-encoded) geometry are listed", msg and "re-encoded" in msg
          and "geometry/1/o" in msg, str(msg)[:300])

# 8. extracting over a folder that isn't a previous extract is refused
other = scratch / "not_a_project"
other.mkdir(parents=True, exist_ok=True)
(other / "notes.txt").write_text("mine")
try:
    GR.Extractor(GR.Source(src), other, S).run()
    msg = None
except GR.GraphError as e:
    msg = str(e)
check("extract: a non-empty folder without pack.jsonc is refused", msg and "--force" in msg
      and (other / "notes.txt").exists(), str(msg))
GR.Extractor(GR.Source(src), other, S, force=True).run()
check("extract: --force replaces it", (other / "pack.jsonc").exists() and not (other / "notes.txt").exists())
GR.Extractor(GR.Source(src), other, S).run()
check("extract: a previous extract is replaced", (other / "pack.jsonc").exists())

print(f"\n{'all passed' if not fails else f'{fails} failed'}")
sys.exit(1 if fails else 0)
