#!/usr/bin/env python3
"""Edits and errors of tools/xdl_media.py on one car and one song per release (the round trip
command covers every pack unedited).

    python3 tools/test_xdl_media.py          archives from xdl_media.release_dirs()

Writes under HWVX-SDL/build-media/test (gitignored: extracted files are game data).
"""

import shutil
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_media as M  # noqa: E402

OUT = M.OUT / "test"
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def raises(f):
    try:
        f()
    except M.MediaError as e:
        return str(e)
    return None


def fresh_copy(arc, sel, name):
    d = OUT / name
    if d.exists():
        shutil.rmtree(d)
    M.extract_pack(arc, arc.find(sel), d)
    return d


def jsonc(p):
    return M.XS.jsonc_loads(Path(p).read_text())


def rewrite(p, name, **changes):
    doc = jsonc(p)
    doc.update(changes)
    body = {k: v for k, v in doc.items() if k not in ("$schema", "format_version")}
    M.write_jsonc(p, name, body, ["edited by test_xdl_media.py"])


def build(d, **kw):
    st = M.Stats()
    return M.build_pack(d, stats=st, log=lambda m: None, **kw), st


# ---- codecs ------------------------------------------------------------------------------
rng = np.random.default_rng(7)
img = rng.integers(0, 256, (16, 24, 4), dtype=np.uint8)
for rel, fmt in M.TEX_FORMATS:
    if fmt == 2 and rel == "ps2":
        continue
    name = M.TEX_FORMATS[(rel, fmt)][0]
    b = M.encode_level(rel, fmt, img)
    d = M.decode_level(rel, fmt, b, 24, 16)
    check(f"codec {name}: size", len(b) == M.level_size(rel, fmt, 24, 16))
    if M.TEX_FORMATS[(rel, fmt)][3]:
        check(f"codec {name}: lossless", np.array_equal(d, img))
    else:
        check(f"codec {name}: encoding its own decode gives the same bytes", M.encode_level(rel, fmt, d) == b)
pal = M.quantize256([img])
idx = M.encode_level("ps2", 2, img[..., :4], pal)
check("codec PSMT8: nearest palette entry", np.array_equal(M.decode_level("ps2", 2, idx, 24, 16, M.clut_bytes(pal)),
                                                          pal[np.frombuffer(idx, np.uint8).reshape(16, 24)]))
check("CSM1 is its own inverse", all(M.csm1(M.csm1(i)) == i for i in range(256)))
pcm = (np.sin(np.arange(5000) / 9) * 9000).astype(np.int16)
v = M.adpcm_encode(pcm, loop_block=3)
dec = M.adpcm_decode(v)[28:28 + len(pcm)].astype(float)
snr = 10 * np.log10((pcm.astype(float) ** 2).sum() / ((pcm - dec) ** 2).sum())
check("ADPCM: SNR > 25 dB", snr > 25, f"{snr:.1f}")
check("ADPCM: framing (silent block 0, loop flag 6, last 3)", v[0] == 0x0C and v[1] == 2 and v[16 * 3 + 1] == 6
      and v[-15] == 3 and not any(v[2:16]))
offs, total = M.relayout([("a", 0, 10, 10), ("b", 32, 10, 10)], 64)
check("relayout: equal lengths keep offsets", offs == {"a": 0, "b": 32} and total == 64)
offs, total = M.relayout([("a", 0, 10, 40), ("b", 32, 10, 10), ("c", 48, 4, 4)], 64)
check("relayout: growth moves what follows, gaps kept", offs == {"a": 0, "b": 64, "c": 80} and total == 96, offs)

# ---- schemas -----------------------------------------------------------------------------


class A:
    check = True


check("schemas/Media*.schema.json are current", M.cmd_schemas(A()) == 0)

dirs = M.release_dirs()
ids = M.Ids()
for rel in ("gc", "pc", "ps2"):
    if rel not in dirs:
        print(f"skip {rel}: no archives")
        continue
    ext = {"gc": ".xgc", "pc": ".xpc", "ps2": ".xps"}[rel]
    cars = M.Archive(dirs[rel] / f"HW_Cars{ext}")
    orig = cars.pack_bytes(cars.find("car_5"))
    d = fresh_copy(cars, "car_5", f"{rel}/car_5")
    media = jsonc(d / "media.jsonc")

    # unedited: the original bytes
    b, st = build(d)
    check(f"{rel}: car_5 builds back byte-identical", b == orig)
    for f in ["media.jsonc"] + media["textures"] + media["sounds"]:
        kind = "MediaPack" if f == "media.jsonc" else "MediaTexture" if "texture" in f else "MediaSound"
        e = M.validate(jsonc(d / f), kind)
        if e:
            check(f"{rel}: {f} validates", False, e)
    check(f"{rel}: every sidecar validates", True)

    # ids and hashes agree with the pairing table
    pairing = {}
    for ln in (ids.dir / "HW_Cars.jsonl").read_text().splitlines():
        r = M.json.loads(ln)
        if r.get("id", "").startswith("base://HW_Cars/car_5/"):
            pairing[r["id"]] = r
    t0 = jsonc(d / media["textures"][0])
    check(f"{rel}: texture fingerprint = pairing x", pairing[t0["id"]][rel]["x"] == t0["fingerprint"])
    check(f"{rel}: texture index = pairing i", pairing[t0["id"]][rel]["i"] == t0["index"])
    if rel != "ps2":
        s0 = jsonc(d / media["sounds"][0])
        check(f"{rel}: sound samples hash = pairing n", pairing[s0["id"]][rel]["n"] == s0["samples"])

    pk = M.RawPack(rel, cars.find("car_5")["type"], orig, "car_5")
    texs = M.scan_textures(pk)

    # ---- texture edit: a square painted on level 0 of a lossy (or palette) texture
    tp = d / media["textures"][0]
    tdoc = jsonc(tp)
    t = next(x for x in texs if x["i"] == tdoc["index"])
    png = tp.parent / "level0.png"
    rgba, _ = M.read_png(png)
    edit = rgba.copy()
    edit[:16, :16] = (255, 0, 0, 255)
    M.write_png(png, edit)                         # an RGBA PNG (PS2: no longer a palette PNG)
    b, st = build(d)
    o, n, lw, lh = t["levels"][0]
    changed = [i for i in range(len(orig)) if b[i] != orig[i]] if len(b) == len(orig) else None
    clut = t.get("clut")
    allowed = set(range(o, o + n)) | (set(range(clut, clut + 0x400)) if clut else set())
    check(f"{rel}: texture edit changes that level (and its CLUT) only", changed and set(changed) <= allowed,
          f"{len(changed or [])} bytes")
    got = M.decode_level(rel, t["fmt"], b[o:o + n], lw, lh, b[clut:clut + 0x400] if clut else None)
    err = np.sqrt(((got[:16, :16, :3].astype(float) - edit[:16, :16, :3]) ** 2).mean())
    check(f"{rel}: edited texels encoded (RGB RMSE {err:.1f})", err < 24, f"{err:.1f}")
    M.write_png(png, rgba)                         # back: equal pixels reuse the base's bytes
    b, st = build(d)
    check(f"{rel}: unedited pixels in an RGBA PNG reuse the base's encoding", b == orig)

    # a missing mip level is made from the one above
    mt = next((jsonc(d / f) for f in media["textures"] if len(jsonc(d / f)["levels"]) > 1), None)
    if mt:
        mp = d / "texture" / M.id_index(mt["id"])
        (mp / "level1.png").unlink()
        b, st = build(d)
        check(f"{rel}: missing level1.png made from level0", st.c.get(("texture level", "made from the level above")) == 1
              and len(b) == len(orig))
        d = fresh_copy(cars, "car_5", f"{rel}/car_5")

    if rel == "ps2":
        # a palette edit in a palette PNG is exact
        tp = d / media["textures"][0]
        t = next(x for x in texs if x["i"] == jsonc(tp)["index"])
        from PIL import Image
        im = Image.open(tp.parent / "level0.png")
        p = im.getpalette()
        p[0:3] = [1, 2, 3]
        im.putpalette(p)
        im.save(tp.parent / "level0.png", transparency=im.info.get("transparency"))
        users = [x for x in texs if x.get("clut") == t["clut"]]
        if len(users) > 1:
            m = raises(lambda: build(d))
            check(f"{rel}: palettes of a shared CLUT that differ are refused", m and "CLUT" in m, m)
        else:
            b, st = build(d)
            e = M.clut_rgba(b[t["clut"]:t["clut"] + 0x400])
            check(f"{rel}: palette PNG edit -> CLUT entry 0", tuple(e[0][:3]) == (1, 2, 3))
        d = fresh_copy(cars, "car_5", f"{rel}/car_5")

    # ---- sound edits
    sp = d / media["sounds"][0]
    sdoc = jsonc(sp)
    s = next(x for x in M.scan_sounds(pk) if x["i"] == sdoc["index"])
    wav, rate = M.read_wav(sp.parent / sdoc["file"])
    quiet = (wav // 2).astype(np.int16)
    M.write_wav(sp.parent / sdoc["file"], quiet, rate)
    b, st = build(d)
    check(f"{rel}: same-length sound edit keeps the pack's size", len(b) == len(orig))
    nb = M.RawPack(rel, pk.type, b, "car_5")
    ns = next(x for x in M.scan_sounds(nb) if x["i"] == s["i"])
    got = M.decode_sample(rel, b[ns["ptr"]:ns["ptr"] + ns["len"]], "built")[:len(quiet)]
    if rel == "ps2":
        e = 10 * np.log10((quiet.astype(float) ** 2).sum() / max(((quiet - got) ** 2).sum(), 1))
        check(f"{rel}: edited sound ADPCM-encoded (SNR {e:.0f} dB)", e > 20)
    else:
        check(f"{rel}: edited sound written exactly", np.array_equal(got, quiet))

    longer = np.concatenate([wav, wav]).astype(np.int16)
    M.write_wav(sp.parent / sdoc["file"], longer, rate)
    rewrite(sp, "MediaSound", loop=None)
    b, st = build(d)
    nb = M.RawPack(rel, pk.type, b, "car_5")
    sounds = M.scan_sounds(nb)
    ns = next(x for x in sounds if x["i"] == s["i"])
    check(f"{rel}: a longer sound grows sound_data", nb.h["sound_data_size"] > pk.h["sound_data_size"]
          and st.c.get(("sound_data", "laid out again")) == 1)
    got = M.decode_sample(rel, b[ns["ptr"]:ns["ptr"] + ns["len"]], "built")
    check(f"{rel}: the longer sound decodes to its length", abs(len(got) - len(longer)) < 28 and ns["loop"] == 0xFFFFFFFF)
    same = all(M.decode_sample(rel, b[x["ptr"]:x["ptr"] + x["len"]], "x").tobytes() ==
               M.decode_sample(rel, orig[y["ptr"]:y["ptr"] + y["len"]], "y").tobytes()
               for x, y in zip(sounds, M.scan_sounds(pk)) if x["i"] != s["i"])
    check(f"{rel}: other sounds intact after the move", same)
    check(f"{rel}: data and tables after sound_data unchanged",
          nb.section("data") == pk.section("data") and nb.section("pointers") == pk.section("pointers"))

    # ---- errors
    M.write_wav(sp.parent / sdoc["file"], wav, rate + 1)
    m = raises(lambda: build(d))
    check(f"{rel}: WAV rate != sidecar rate is refused", m and "Hz" in m, m)
    import wave
    with wave.open(str(sp.parent / sdoc["file"]), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(8))
    m = raises(lambda: build(d))
    check(f"{rel}: stereo WAV is refused", m and "mono" in m, m)
    d = fresh_copy(cars, "car_5", f"{rel}/car_5")
    tp = d / media["textures"][0]
    rewrite(tp, "MediaTexture", width=jsonc(tp)["width"] * 2)
    m = raises(lambda: build(d))
    check(f"{rel}: another texture size is refused", m and "compiler" in m, m)
    rewrite(tp, "MediaTexture", format="BOGUS")
    m = raises(lambda: build(d))
    check(f"{rel}: unknown format fails the schema", m and "format" in m, m)
    doc = jsonc(tp)
    check(f"{rel}: extra field fails the schema", M.validate(dict(doc, wrap=1), "MediaTexture"))
    check(f"{rel}: bad link fails the schema", M.validate(dict(doc, id="car_5/texture/0"), "MediaTexture"))
    other = M.Archive(dirs[rel] / f"HW_Items{ext}")
    m = raises(lambda: M.build_pack(d, base_arg=other.path, log=lambda m: None))
    check(f"{rel}: a base that isn't the extracted copy is refused", m is not None, m)

    # ---- song and bank
    mus = M.Archive(dirs[rel] / f"HW_music{ext}")
    ent = mus.entries[0]
    orig = mus.pack_bytes(ent)
    d = fresh_copy(mus, ent["name"], f"{rel}/{ent['name']}")
    b, st = build(d)
    check(f"{rel}: {ent['name']} (song + bank) builds back byte-identical", b == orig)
    check(f"{rel}: bank.jsonc validates", not M.validate(jsonc(d / "song/bank.jsonc"), "MediaBank"))
    mid = (d / "song/song.mid").read_bytes()
    check(f"{rel}: song.mid is a standard MIDI file", mid[:4] == b"MThd" and b"MTrk" in mid)
    (d / "song/song.mid").write_bytes(mid + b"MTrk" + struct.pack(">I", 4) + b"\x00\xff\x2f\x00")
    bp = d / "song/bank.jsonc"
    bdoc = jsonc(bp)
    bdoc["instruments"][0]["key_high"] = 99
    M.write_jsonc(bp, "MediaBank", {k: v for k, v in bdoc.items() if k not in ("$schema", "format_version")}, [])
    b, st = build(d)
    nb = M.RawPack(rel, ent["type"], b, ent["name"])
    song, bank = M.scan_song(nb)
    check(f"{rel}: a longer song: RIFX size and bank offset follow",
          song["size"] == 4 + len(mid) + 12 and b[song["smf"][0]:song["smf"][0] + song["smf"][1]].endswith(b"\xff\x2f\x00"))
    check(f"{rel}: instrument key range written", bank["entries"][0]["hi"] == 99)
    wf = d / "song" / bdoc["instruments"][0]["sample"]
    w, r = M.read_wav(wf)
    M.write_wav(wf, np.concatenate([w, w]).astype(np.int16), r)
    b, st = build(d)
    nb = M.RawPack(rel, ent["type"], b, ent["name"])
    _, bank2 = M.scan_song(nb)
    e0 = bank2["entries"][0]
    got = M.decode_sample(rel, b[bank2["data"] + e0["off"]:bank2["data"] + e0["off"] + e0["len"]], "x")
    check(f"{rel}: a longer bank sample grows the bank", bank2["size"] > bank["size"] and abs(len(got) - 2 * len(w)) < 28)
    sharers = [k for k, i in enumerate(bdoc["instruments"]) if i["sample"] == bdoc["instruments"][0]["sample"]]
    check(f"{rel}: instruments sharing the sample still share it",
          len({(bank2["entries"][k]["off"], bank2["entries"][k]["len"]) for k in sharers}) == 1)

print("all passed" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
