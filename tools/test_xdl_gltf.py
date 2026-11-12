#!/usr/bin/env python3
"""tools/xdl_gltf.py: glTF output checked by the Khronos validator, the encoder, and an edited
model built back into a pack (tools/xdl_graph.py).

    python3 tools/test_xdl_gltf.py [SURVEY_ROOT]      default: HWVX-SDL/build-survey

Scratch files go to HWVX-SDL/build-gltf/test (game data: never committed). The validator is
`gltf_validator` on PATH or in ~/.local/bin; without it the structure is checked in Python.
"""

import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_gltf as XG  # noqa: E402
import xdl_graph as GR  # noqa: E402
import xdl_struct as XS  # noqa: E402

survey = Path(sys.argv[1]) if len(sys.argv) > 1 else next(
    (d for d in (XS.ROOT / "build-survey", XS.ROOT.parent / "build-survey") if d.is_dir()), XS.ROOT / "build-survey")
scratch = next((d for d in (XS.ROOT / "build-gltf", XS.ROOT.parent / "build-gltf") if d.is_dir()),
               XS.ROOT / "build-gltf") / "test"
CAR = "HW_Cars/30_car_29"       # a skinned geometry, a shadow volume, 20 geometries
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def validator():
    for p in (shutil.which("gltf_validator"), Path.home() / ".local/bin/gltf_validator"):
        if p and Path(p).exists():
            return str(p)
    return None


def validate(path):
    """(errors, warnings, first messages) for one .gltf."""
    exe = validator()
    if exe:
        r = subprocess.run([exe, "-o", str(path)], capture_output=True, text=True)
        out = r.stdout[r.stdout.index("{"):]
        rep = json.loads(out)
        iss = rep["issues"]
        msgs = [f"{m['code']} {m.get('pointer', '')}" for m in iss["messages"] if m["severity"] == 0]
        return iss["numErrors"], iss["numWarnings"], msgs[:3]
    return structural(path)


def structural(path):
    """Without the validator: indices in range, accessor bounds, buffer sizes, unit normals."""
    gl, b = XG.load(path)
    errs = []
    if gl.get("buffers") and len(b) != gl["buffers"][0]["byteLength"]:
        errs.append("buffer length")
    for i, a in enumerate(gl.get("accessors", [])):
        bv = gl["bufferViews"][a["bufferView"]]
        size = XG._COMP[a["componentType"]][1] * XG._NCOMP[a["type"]] * a["count"]
        if bv["byteOffset"] + size > len(b) or size > bv["byteLength"]:
            errs.append(f"accessor {i} past its view")
    for m in gl.get("meshes", []):
        for p in m["primitives"]:
            n = gl["accessors"][p["attributes"]["POSITION"]]["count"] if "POSITION" in p["attributes"] else 0
            if "indices" in p:
                idx, _ = XG._acc_raw(gl, b, p["indices"])
                if idx and max(v[0] for v in idx) >= n:
                    errs.append("index past the vertices")
            if "NORMAL" in p["attributes"]:
                for v in XG._acc_raw(gl, b, p["attributes"]["NORMAL"])[0]:
                    if abs(sum(c * c for c in v) - 1) > 1e-3:
                        errs.append("normal not unit")
                        break
    return len(errs), 0, errs[:3]


def triangles(g):
    out = []
    for lv in g["levels"]:
        if lv.get("undecoded"):          # PS2 shadow volumes: kept as bytes, not in the glTF
            continue
        for gr in lv["groups"]:
            if "tris" in gr:
                out.append([tuple(gr["verts"][i] for i in t) for t in gr["tris"]])
            else:
                out.append([(e[1], e[2]) for e in gr.get("edges", [])])
    return out


def geom(folder, i):
    blob, tex, offs = pack_geoms(folder)
    return XG.decode(blob, offs[i], tex)


def pack_geoms(folder):
    blob, tex = XG._pack_blob(folder)
    return blob, tex, XG.scan(blob)


# ------------------------------------------------------------------------------------------
print(f"validator: {validator() or 'not installed (structural checks in Python)'}")
if scratch.exists():
    shutil.rmtree(scratch)
scratch.mkdir(parents=True)

# 1. every geometry of a car, three releases: valid glTF, and the decoded mesh survives the file
for rel in ("gc", "pc", "ps2"):
    folder = survey / rel / CAR
    blob, tex, offs = pack_geoms(folder)
    nerr, nfile, same, skinned, shadow = 0, 0, 0, 0, 0
    first = []
    for i, off in enumerate(offs):
        g = XG.decode(blob, off, tex)
        gl, b = XG.to_gltf(g, f"geometry_{i}", f"geometry_{i}.bin", lambda v: f"base://HW_Cars/car_29/texture/{v & 0xFFFF}")
        p = scratch / rel / f"geometry_{i}.gltf"
        p.parent.mkdir(parents=True, exist_ok=True)
        XG.write(gl, b, p)
        e, w, msgs = validate(p)
        nerr += e
        first += msgs
        nfile += 1
        gl2, b2 = XG.load(p)
        g2 = XG.from_gltf(gl2, b2)
        same += triangles(g2) == triangles(g)
        skinned += "skins" in gl
        shadow += any(p_.get("mode") == 1 for m in gl.get("meshes", []) for p_ in m["primitives"])
    check(f"{rel} {CAR}: {nfile} glTF files, 0 validator errors", nerr == 0, f"{nerr} errors: {first}")
    check(f"{rel}: the glTF reads back to the decoded triangles", same == nfile, f"{same}/{nfile}")
    check(f"{rel}: a skinned geometry becomes a glTF skin", skinned == 1, str(skinned))
    if rel != "ps2":
        check(f"{rel}: the shadow volume is a LINES primitive", shadow >= 1, str(shadow))

# 2. skinning: one bone per vertex, weight 1.0, the GameCube's per-bone counts
blob, tex, offs = pack_geoms(survey / "gc" / CAR)
g = XG.decode(blob, offs[0], tex)
gl, b = XG.to_gltf(g, "g", "g.bin")
pr = gl["meshes"][0]["primitives"][0]
joints = [v[0] for v in XG._acc_raw(gl, b, pr["attributes"]["JOINTS_0"])[0]]
weights = XG._acc_raw(gl, b, pr["attributes"]["WEIGHTS_0"])[0]
check("skinned: weights are (1, 0, 0, 0)", all(w == (1.0, 0.0, 0.0, 0.0) for w in weights))
check("skinned: bones 0, 1, 3 (car_29: 24/12/12 vertices as the docs say)", sorted(set(joints)) == [0, 1, 3],
      str(sorted(set(joints))))
check("skinned: 8 joints, the level's bone counts in extras",
      len(gl["skins"][0]["joints"]) == 8 and gl["nodes"][0]["extras"]["levels"][0]["boneCounts"][:4] == [30, 15, 0, 15],
      str(gl["nodes"][0]["extras"]["levels"][0].get("boneCounts")))

# 3. the encoder: decode -> glTF -> encode -> decode gives the same triangles (GameCube, PC)
for rel in ("gc", "pc"):
    blob, tex, offs = pack_geoms(survey / rel / CAR)
    ok = 0
    for off in offs:
        g = XG.decode(blob, off, tex)
        gl, b = XG.to_gltf(g, "g", "g.bin", lambda v: "base://HW_Cars/car_29/texture/0")
        data, ptrs, ts, names = XG.encode(XG.from_gltf(gl, b), rel)
        g3 = XG.decode(XG.Blob(data, set(ptrs), rel), 0, {s: 1 for s in ts})
        ok += triangles(g3) == triangles(g)
    check(f"{rel}: re-encoded geometry decodes to the same triangles", ok == len(offs), f"{ok}/{len(offs)}")
blob, tex, offs = pack_geoms(survey / "ps2" / CAR)
g = XG.decode(blob, offs[1], tex)
try:
    XG.encode(g, "ps2")
    msg = None
except XG.GeomError as e:
    msg = str(e)
check("ps2: encoding edited geometry is refused with a reason", msg and "PS2" in msg, str(msg))

# 3b. glTF forms other tools write: COLOR_0 as normalised u8/u16 VEC3/VEC4; no primitive extras
def with_color(gl, b, ctype, typ, vals):
    """A copy whose first primitive's COLOR_0 is a new accessor (ctype, typ) of `vals`."""
    gl = json.loads(json.dumps(gl))
    pr = gl["meshes"][0]["primitives"][0]
    n = gl["accessors"][pr["attributes"]["POSITION"]]["count"]
    code = {5121: "B", 5123: "H", 5126: "f"}[ctype]
    data = b"".join(struct.pack("<%d%s" % (len(vals), code), *vals) for _ in range(n))
    b = bytes(b) + bytes(-len(b) % 4)
    gl["bufferViews"].append({"buffer": 0, "byteOffset": len(b), "byteLength": len(data)})
    gl["accessors"].append({"bufferView": len(gl["bufferViews"]) - 1, "componentType": ctype,
                            "normalized": ctype != 5126, "count": n, "type": typ})
    pr["attributes"]["COLOR_0"] = len(gl["accessors"]) - 1
    return gl, b + data


gl, b = XG.to_gltf(geom(survey / "pc" / CAR, 1), "g", "g.bin")
for ctype, typ, vals, want in ((5121, "VEC3", (255, 128, 0), (255, 128, 0, 255)),
                               (5123, "VEC3", (65535, 0, 32896), (255, 0, 128, 255)),
                               (5123, "VEC4", (0, 65535, 0, 32896), (0, 255, 0, 128)),
                               (5126, "VEC3", (1.0, 0.5, 0.0), (255, 128, 0, 255))):
    g2 = XG.from_gltf(*with_color(gl, b, ctype, typ, vals))
    got = {v[2] for v in g2["levels"][0]["groups"][0]["verts"]}
    check(f"COLOR_0 {ctype} {typ} -> {want}", got == {bytes(want)}, str(got))
gl2 = json.loads(json.dumps(gl))
gl2["meshes"][0]["primitives"][0].pop("extras")
try:
    XG.from_gltf(gl2, b)
    msg = None
except XG.GeomError as e:
    msg = str(e)
check("a primitive without extras.group is a GeomError naming it", msg and "extras.group" in msg, str(msg))

# 3c. PC: an edit that needs an array the original level lacks (colours on geometry 12 level 1)
blob, tex, offs = pack_geoms(survey / "pc" / CAR)
g = XG.decode(blob, offs[12], tex)
lv = g["levels"][1]
check("pc: geometry 12 level 1 has no colour array", lv["vb"]["has"][1] is False)
gr = lv["groups"][0]
gr["rec"]["flags"] &= ~XG.F_LITENV
gr["verts"] = [(p, None, b"\x10\x20\x30\xff", u, bn) for p, _, _, u, bn in gr["verts"]]
data, ptrs, ts, names = XG.encode(g, "pc")
g3 = XG.decode(XG.Blob(data, set(ptrs), "pc"), 0, {s: 1 for s in ts})
check("pc: unlit group with colours gets a colour array", g3["levels"][1]["vb"]["has"][1] is True
      and {v[2] for v in g3["levels"][1]["groups"][0]["verts"]} == {b"\x10\x20\x30\xff"})

# 4. content hash: unchanged file, same hash; a moved vertex, another hash
gl, b = XG.to_gltf(geom(survey / "gc" / CAR, 1), "g", "g.bin")
h = XG.content_hash(gl, b)
p = scratch / "hash.gltf"
XG.write(gl, b, p)
gl2, b2 = XG.load(p)
check("content hash: unchanged after write and load", XG.content_hash(gl2, b2) == h)
b3 = bytearray(b2)
struct.pack_into("<f", b3, 0, 1.0 + struct.unpack_from("<f", b3, 0)[0])
check("content hash: a moved vertex changes it", XG.content_hash(gl2, bytes(b3)) != h)

# 5. an edited model in a whole pack: extract, move vertices, build, check the built pack
src_dir = survey / "gc" / CAR
proj = scratch / "proj" / "car_29"
GR.Extractor(GR.Source(src_dir), proj).run()
gfile = proj / "geometry" / "1.gltf"
gl, b = XG.load(gfile)
pos_acc = gl["meshes"][0]["primitives"][0]["attributes"]["POSITION"]
a = gl["accessors"][pos_acc]
bv = gl["bufferViews"][a["bufferView"]]
b = bytearray(b)
for k in range(a["count"]):                       # lift LOD 0's first group by 0.25
    o = bv["byteOffset"] + 12 * k + 4
    struct.pack_into("<f", b, o, struct.unpack_from("<f", b, o)[0] + 0.25)
a["min"][1] += 0.25
a["max"][1] += 0.25
XG.write(gl, bytes(b), gfile)
out = scratch / "built" / "HW_Cars" / src_dir.name
out.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(src_dir.parent / "manifest.json", out.parent / "manifest.json")
bd = GR.Builder(proj)
bd.build(out)
check("edited glTF: the geometry is re-encoded", bd.edited == ["base://HW_Cars/car_29/geometry/1"], str(bd.edited))
blob2, tex2, offs2 = pack_geoms(out)
check("edited glTF: the built pack has the same geometries", len(offs2) == len(pack_geoms(src_dir)[2]))
g_old = geom(src_dir, 1)
g_new = XG.decode(blob2, offs2[1], tex2)
t_old, t_new = triangles(g_old)[0], triangles(g_new)[0]
lift = all(abs(XG._f32s(n[0])[1] - XG._f32s(o[0])[1] - 0.25) < 1e-5 for to, tn in zip(t_old, t_new) for o, n in zip(to, tn))
check("edited glTF: LOD 0 group 0 moved up by 0.25, same triangle count", len(t_old) == len(t_new) and lift)
check("edited glTF: the other groups are unchanged", triangles(g_old)[1:] == triangles(g_new)[1:])
check("edited glTF: group textures kept", [gr["texture"] for lv in g_old["levels"] for gr in lv["groups"]]
      == [gr["texture"] for lv in g_new["levels"] for gr in lv["groups"]])
# the rest of the pack moved and still links up: extracting the built pack gives the same objects
proj2 = scratch / "proj2" / "car_29"
GR.Extractor(GR.Source(out), proj2).run()
same = [p.relative_to(proj) for p in proj.rglob("*.jsonc")
        if "geometry" not in p.parts and p.name != "pack.jsonc"
        and (proj2 / p.relative_to(proj)).read_text() != p.read_text()]
check("edited glTF: every other object extracts the same from the built pack", not same, str(same[:3]))
ref_ok = GR.Source(out).rel["ref"]
gi = {o: i for i, o in enumerate(offs2)}
check("edited glTF: geometry refs point to their geometry",
      all(gi.get(struct.unpack_from(">I", blob2.d, o)[0]) == v & 0xFFFF for o, v in ref_ok.items() if v >> 16 == 0))
e, w, msgs = validate(gfile)
check("edited glTF: still valid", e == 0, str(msgs))

print(f"\n{'all passed' if not fails else f'{fails} failed'}")
sys.exit(1 if fails else 0)
