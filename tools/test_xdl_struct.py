#!/usr/bin/env python3
"""Edits and errors of tools/xdl_struct.py on one car (the round trip covers the no-edit case).

    python3 tools/test_xdl_struct.py [SURVEY_ROOT]      default: HWVX-SDL/build-survey
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_struct as X  # noqa: E402

# default: HWVX-SDL/build-survey (in the main checkout; a worktree inside it
# sits one level deeper, so its parent's is tried too)
survey = Path(sys.argv[1]) if len(sys.argv) > 1 else next(
    (d for d in (X.ROOT / "build-survey", X.ROOT.parent / "build-survey") if d.is_dir()), X.ROOT / "build-survey")
S = X.Schemas()
P = X.Pairing(X.find_up("docs/xdl-format/pairing"))
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def errors(doc):
    v, _ = X.validator(S.top["CarAInfo"][0])
    return [getattr(e, "message", str(e)) for e in v.iter_errors(doc)]


def raises(f):
    try:
        f()
    except X.XdlError as e:
        return str(e)
    return None


for rel in ("gc", "pc"):
    pack = X.Pack(survey / rel / "HW_Cars" / "06_car_5")
    doc, sch, off, sname, oid = X.extract(S, pack, P, "CarAInfo", 0, ())
    doc = X.jsonc_loads(X.to_jsonc(doc, sch, []))
    orig = pack.blob[off:off + 0x120]
    e = pack.e

    # one changed field: one changed f32
    d = dict(doc, maxRpm=8000)
    data, rel_ = X.build(S, pack, P, d, sname, off)
    diff = [i for i in range(0x120) if data[i] != orig[i]]
    check(f"{rel}: maxRpm edit touches 0x1C..0x1F only", diff and set(diff) <= set(range(0x1C, 0x20)), diff)
    check(f"{rel}: maxRpm written", struct.unpack_from(e + "f", data, 0x1C)[0] == 8000)

    # extends-only file: everything from the base
    data, rel_ = X.build(S, pack, P, {"format_version": 1, "extends": oid, "damagePercent": 50}, sname, off)
    diff = [i for i in range(0x120) if data[i] != orig[i]]
    check(f"{rel}: sparse extends file changes damagePercent only", diff == [0x10], diff)
    check(f"{rel}: sparse extends file validates", not errors({"format_version": 1, "extends": oid,
                                                               "damagePercent": 50}))

    # texture link to another share texture: reloc changes, slot bytes 0
    d = dict(doc, skidMarkTexture="base://HW_Cars/share_2/texture/3")
    data, rel_ = X.build(S, pack, P, d, sname, off)
    check(f"{rel}: texture relink -> reloc 0x20003", rel_["texture"].get(0x50) == 0x20003, rel_["texture"])
    # a texture of the car's own pack
    d = dict(doc, skidMarkTexture="base://HW_Cars/car_5/texture/1")
    data, rel_ = X.build(S, pack, P, d, sname, off)
    check(f"{rel}: own-pack texture -> reloc 1", rel_["texture"].get(0x50) == 1, rel_["texture"])

    # pointer relinked to another object of the right type: the field now holds its offset
    # (soundSets[1], the one element CarA.c reads; [0] is 0 in the data, see schemas.md)
    d = dict(doc, rollSound=f"{oid}/soundSets/1")
    data, rel_ = X.build(S, pack, P, d, sname, off)
    ss = pack.target(pack.target(off + 0x11C) + 4)
    check(f"{rel}: rollSound -> soundSets[1]", rel_["pointers"].get(0x60) == ss and
          struct.unpack_from(e + "I", data, 0x60)[0] == ss)
    # null pointer
    d = dict(doc, forcePoints=None)
    data, rel_ = X.build(S, pack, P, d, sname, off)
    check(f"{rel}: null link clears the slot", 0xA4 not in rel_["pointers"] and data[0xA4:0xA8] == bytes(4))

    # errors
    m = raises(lambda: X.build(S, pack, P, dict(doc, engine=f"{oid}/accel"), sname, off))
    check(f"{rel}: engine -> a CarAAccel is refused", m and "CarAAccel" in m, m)
    m = raises(lambda: X.build(S, pack, P, dict(doc, engine="base://HW_Cars/car_3/CarAInfo/0/engine"), sname, off))
    check(f"{rel}: link into another pack is refused", m and "own pack" in m, m)
    m = raises(lambda: X.build(S, pack, P, dict(doc, skidMarkTexture="base://HW_Cars/car_5/geometry/1"), sname, off))
    check(f"{rel}: geometry id in a texture field is refused", m is not None, m)

    # schema
    check(f"{rel}: extracted file validates", not errors(doc))
    check(f"{rel}: hidden field rejected", any("pad0C" in x for x in errors(dict(doc, pad0C=[0, 0, 0, 0]))))
    check(f"{rel}: u8 out of range rejected", errors(dict(doc, damagePercent=300)))
    check(f"{rel}: number as a string rejected", errors(dict(doc, maxRpm="7400")))
    check(f"{rel}: bad link rejected", errors(dict(doc, engine="car_5/engine")))
    check(f"{rel}: texture field needs a texture id",
          errors(dict(doc, skidMarkTexture="base://HW_Cars/car_5/CarAInfo/0/engine")))
    nd = {k: v for k, v in doc.items() if k not in ("extends", "maxRpm")}
    check(f"{rel}: without extends every field is required", any("maxRpm" in x for x in errors(nd)))
    check(f"{rel}: array length is fixed", errors(dict(doc, viewOffset=[0, 1])))

# jsonc_loads: commas and comment marks inside strings are text; errors at EOF
check("jsonc: trailing commas dropped, not inside strings",
      X.jsonc_loads('{"a": "x, }", "b": [1, 2, // z,]\n], "c": /* , */ "//",}') ==
      {"a": "x, }", "b": [1, 2], "c": "//"})
check("jsonc: escaped quote", X.jsonc_loads('// h\n{"a": "q\\"x,]"}') == {"a": 'q"x,]'})
for bad in ('{"a": "abc', '{"a": 1 /* x', '{"a": "x\\"}'):
    try:
        X.jsonc_loads(bad)
        m = None
    except X.XdlError as e:
        m = str(e)
    except Exception as e:  # noqa: BLE001
        m = None
        print("  ", type(e).__name__, e)
    check(f"jsonc: {bad!r} -> XdlError", m is not None)

# PS2 geometries (header 0x1005): the reflection geometry's ref link resolves
pk = X.Pack(survey / "ps2" / "HW_Cars" / "30_car_29")
check("ps2: geometry scan finds the 0x1005 headers", len(pk.geometries()) > 0)
for f in X.PARTS:
    try:
        ok, msg = X.roundtrip_one(S, pk, P, "CarAInfo", [f], None, "ps2")
    except X.XdlError as e:
        ok, msg = False, str(e)
    check(f"ps2: car_29 {f} round trip", ok, msg)

print("all passed" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
