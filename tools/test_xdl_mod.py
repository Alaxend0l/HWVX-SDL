#!/usr/bin/env python3
"""tools/xdl_mod.py: the sample mod packs, and every way of putting game data in a mod is
refused, naming the asset.

    python3 tools/test_xdl_mod.py [GAME_FILES]     default: orig/gc/files found above the tools

HWVX_BIN=build/hwvx (the default when it exists) also runs the port's installer (hwvx
--install-mod) on the sample and on damaged packages, in a scratch mods folder.

Indexes HW_Cars, HW_IPack and HW_Items of the given release (a partial index: a few seconds) and
writes under build/mods-test (gitignored: the refused mods hold game data, on purpose).
"""

import base64
import contextlib
import hashlib
import os
import subprocess
import io
import json
import shutil
import struct
import sys
import wave
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_mod as M  # noqa: E402

XM, XG = M.XM, M.XG
OUT = M.ROOT / "build" / "mods-test"
SAMPLE = M.ROOT / "tools" / "mod-samples" / "night-shift"
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f": {detail}" if detail and not cond else ""))
    fails += not cond


def run(*argv):
    """(exit code, output) of xdl_mod.py argv."""
    buf = io.StringIO()
    old = sys.argv
    sys.argv = ["xdl_mod.py"] + [str(a) for a in argv]
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            code = M.main()
    finally:
        sys.argv = old
    return code, buf.getvalue()


def mod(name, edit=None, files=None):
    """A copy of the sample with its manifest edited and files added."""
    d = OUT / "mods" / name
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    doc = M.XS.jsonc_loads((SAMPLE / "mod.jsonc").read_text())
    if edit:
        edit(doc)
    (d / "mod.jsonc").write_text(json.dumps(doc, indent=2))
    for n, b in (files or {}).items():
        (d / n).parent.mkdir(parents=True, exist_ok=True)
        (d / n).write_bytes(b)
    return d


def refused(name, d, *words, extra=()):
    code, out = run("pack", d, "-o", OUT / f"{name}.xmod", "--index", INDEX, "--allow-partial-index", *extra)
    ok = code == 1 and not (OUT / f"{name}.xmod").exists() and all(w in out for w in words)
    check(f"refused: {name}", ok, out.strip()[-600:])
    return out


def png_bytes(img):
    p = OUT / "tmp.png"
    XM.write_png(p, img)
    return p.read_bytes()


def wav_bytes(pcm, rate=22050, channels=1):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.asarray(pcm, "<i2").tobytes())
    return b.getvalue()


def gltf(pos, name="mesh"):
    pos = np.asarray(pos, "<f4")
    return {f"{name}.gltf": json.dumps({
        "asset": {"version": "2.0"}, "buffers": [{"uri": f"{name}.bin", "byteLength": pos.nbytes}],
        "bufferViews": [{"buffer": 0, "byteLength": pos.nbytes}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "type": "VEC3", "count": len(pos)}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 0}]}]}).encode(),
            f"{name}.bin": pos.tobytes()}


def repack(src, dst, edit, compress=False):
    """A copy of package src with its {name: bytes} edited (as a crafted or damaged package would be)."""
    files = M.read_package(src)
    edit(files)
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED) as z:
        for n in sorted(files):
            zi = zipfile.ZipInfo(n, (1980, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
            z.writestr(zi, files[n])
    return dst


def set_check(pkg, **kv):
    c = json.loads(pkg["check.json"])
    c.update(kv)
    pkg["check.json"] = json.dumps(c).encode()


def relist(files):
    """check.json listing the files as they now are (a package re-signed by hand)."""
    set_check(files, files={n: {"size": len(d), "sha256": hashlib.sha256(d).hexdigest()}
                            for n, d in files.items() if n != "check.json"})


BAD_PACKAGES = {
    # name: (edit, compressed, words the refusal has)
    "no-check": (lambda f: f.pop("check.json"), False, "no check.json"),
    "tampered": (lambda f: f.update({"README.md": f["README.md"] + b"x"}), False, "isn't the file the packer checked"),
    "unlisted": (lambda f: f.update({"extra.txt": b"hello"}), False, "list"),
    "kind": (lambda f: (f.update({"tool.exe": b"MZ"}), relist(f)), False, "kind of file"),
    "hidden": (lambda f: (f.update({".x/a.txt": b"MZ"}), relist(f)), False, ".x/a.txt"),
    "other-mod": (lambda f: set_check(f, mod="uid://someoneelse"), False, "another mod's"),
    "old-tool": (lambda f: set_check(f, tool_version=1), False, "re-pack"),
    "failed": (lambda f: set_check(f, result="2 problems"), False, "passed"),
    "compressed": (lambda f: None, True, "compressed"),
}


def port_install(hwvx, pkg, mods):
    r = subprocess.run([str(hwvx), "--install-mod", str(pkg)], capture_output=True, text=True, timeout=60,
                       env=dict(os.environ, HWVX_MODS_DIR=str(mods), HWVX_PREF_DIR=str(OUT / "pref")))
    return r.returncode, r.stdout + r.stderr


def installers(hwvx):
    """The packer's install and the port's refuse damaged packages alike; links aren't followed."""
    good = OUT / "a.xmod"
    for name, (edit, compress, words) in BAD_PACKAGES.items():
        pkg = repack(good, OUT / f"bad-{name}.xmod", edit, compress)
        mods = OUT / "inst-bad"
        shutil.rmtree(mods, ignore_errors=True)
        if not compress:    # zipfile reads deflated entries: the port's check only
            code, out = run("install", pkg, mods)
            check(f"install refuses: {name}", code == 2 and words in out and not (mods / "nightshift001").exists(),
                  out)
        if hwvx:
            code, out = port_install(hwvx, pkg, mods)
            left = sorted(p.name for p in mods.iterdir()) if mods.exists() else []
            check(f"port install refuses: {name}", code != 0 and words in out and left == [], f"{out} {left}")
    for who in ("packer", "port") if hwvx else ("packer",):
        mods = OUT / f"inst-{who}"
        work = OUT / f"work-{who}"
        shutil.rmtree(mods, ignore_errors=True)
        shutil.rmtree(work, ignore_errors=True)
        shutil.copytree(SAMPLE, work)
        mods.mkdir(parents=True)
        (mods / "mywork").symlink_to(work, target_is_directory=True)       # the same mod, linked in
        (mods / "nightshift001").mkdir()
        (mods / "nightshift001" / "mod.jsonc").write_text('{"id": "uid://someoneelse", "format_version": 1}')
        inst = (lambda p: run("install", p, mods)) if who == "packer" else (lambda p: port_install(hwvx, p, mods))
        code, out = inst(good)
        check(f"{who} install: a folder holding another mod is in the way", code != 0 and "in the way" in out,
              out)
        shutil.rmtree(mods / "nightshift001")
        (mods / "nightshift001").symlink_to(work, target_is_directory=True)  # the same id, linked
        code, out = inst(good)
        names = sorted(p.name for p in mods.iterdir())
        check(f"{who} install: replaces linked copies, never what they link to",
              code == 0 and names == ["nightshift001"] and not (mods / "nightshift001").is_symlink() and
              (mods / "nightshift001" / "check.json").is_file() and (work / "mod.jsonc").is_file() and
              (work / "README.md").is_file(), f"{out} {names}")
        code, out = inst(good)
        names = sorted(p.name for p in mods.iterdir())
        check(f"{who} install: again, over itself", code == 0 and names == ["nightshift001"], f"{out} {names}")


def main():
    global INDEX
    data = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if data is None:
        for d in (XM.release_dirs().get("gc"), M.ROOT / "orig" / "files", M.ROOT.parent / "orig" / "files"):
            if d and (Path(d) / "HW_Cars.xgc").exists():
                data = Path(d)
                break
    if data is None:
        print("no game files (pass their folder): skipped")
        return 77
    OUT.mkdir(parents=True, exist_ok=True)
    INDEX = OUT / "index-test.npz"
    code, out = run("index", data, "-o", INDEX, "--archives", "HW_Cars,HW_IPack,HW_Items")
    check("index of HW_Cars, HW_IPack, HW_Items", code == 0, out)
    for p in OUT.glob("*.xmod"):
        p.unlink()

    # the sample: packs, the same bytes twice, nothing of the game's in it
    code, out = run("pack", SAMPLE, "-o", OUT / "a.xmod", "--index", INDEX)
    check("a partial index refuses to pack", code == 1 and "doesn't cover the whole game" in out, out)
    code, out = run("pack", SAMPLE, "-o", OUT / "a.xmod", "--index", INDEX, "--allow-partial-index")
    check("the sample packs", code == 0 and "no game data found" in out, out)
    run("pack", SAMPLE, "-o", OUT / "b.xmod", "--index", INDEX, "--allow-partial-index")
    a = (OUT / "a.xmod").read_bytes()
    check("packing is deterministic", a == (OUT / "b.xmod").read_bytes())
    files = M.read_package(OUT / "a.xmod")
    chk = json.loads(files["check.json"])
    check("check.json: files hashed, fingerprints of the textures it refers to",
          set(chk["files"]) == {"mod.jsonc", "README.md"} and
          "gc" in chk["fingerprints"].get("base://HW_Cars/car_5/texture/0", {}), chk)
    ix = M.Index(INDEX)
    check("the package itself holds no game data", not M.check_bytes(ix, M.Report(), "pkg", a))
    code, out = run("info", OUT / "a.xmod")
    check("info", code == 0 and "Night Shift 1.0.0" in out and "no game data found" in out, out)
    shutil.rmtree(OUT / "installed", ignore_errors=True)
    code, out = run("install", OUT / "a.xmod", OUT / "installed")
    check("install", code == 0 and (OUT / "installed" / "nightshift001" / "mod.jsonc").is_file(), out)

    # game data, each way it could get into a mod
    arc = XM.Archive(data / "HW_Cars.xgc")
    ent = arc.find("car_5")
    raw = arc.pack_bytes(ent)
    pk = XM.RawPack(arc.release, ent["type"], raw, "car_5")
    d0, dn = pk.sec["data"]
    refused("archive", mod("archive", files={"HW_Items.xgc": (data / "HW_Items.xgc").read_bytes()}),
            "HW_Items.xgc", "game archive")
    refused("archive-renamed", mod("archive-renamed", files={"stuff.bin": (data / "HW_Items.xgc").read_bytes()}),
            "stuff.bin", "the game's own file")
    out = refused("pack-data", mod("pack-data", files={"car/data.bin": raw[d0:d0 + dn]}),
                  "car/data.bin", "game data", "base://HW_Cars/car_5")
    refused("slice", mod("slice", files={"x.bin": b"my own header" + raw[d0 + 0x1235:d0 + 0x1235 + 150] + b"tail"}),
            "x.bin", "base://HW_Cars/car_5")
    rec = {"type": "raw", "bytes": raw[d0 + 0x2000:d0 + 0x2000 + 96].hex()}
    refused("hex-in-json", mod("hex-in-json", files={"objects/thing.jsonc": json.dumps(rec).encode()}),
            "objects/thing.jsonc: bytes", "base://HW_Cars/car_5")
    tex = XM.scan_textures(pk)[0]
    o, n, lw, lh = tex["levels"][0]
    img = XM.decode_level(arc.release, tex["fmt"], raw[o:o + n], lw, lh)
    refused("texture", mod("texture", files={"paint.png": png_bytes(img)}),
            "paint.png", "is the game's texture", 'a recipe on "base://HW_Cars/car_5/texture/0"')
    opaque = img.copy()
    opaque[:, :, 3] = 255
    refused("texture-no-alpha", mod("texture-no-alpha", files={"paint.png": png_bytes(opaque)}),
            "is the game's texture")
    m = M.recipe_matrix([{"op": "hue_shift", "degrees": 140}])
    hue = img.copy()
    hue[:, :, :3] = np.clip(img[:, :, :3].astype(float) @ m[:, :3].T + m[:, 3], 0, 255).astype(np.uint8)
    refused("texture-recoloured", mod("texture-recoloured", files={"paint.png": png_bytes(hue)}),
            "looks like the game's texture", "base://HW_Cars/car_5/texture/0")
    from PIL import Image
    small = np.asarray(Image.fromarray(img).resize((lw // 2 + 13, lh // 2 + 5), Image.LANCZOS))
    refused("texture-resized", mod("texture-resized", files={"paint.png": png_bytes(small)}),
            "looks like the game's texture")
    snd = None
    for e2 in arc.entries:
        p2 = XM.RawPack(arc.release, e2["type"], arc.pack_bytes(e2), e2["name"])
        ss = [s for s in XM.scan_sounds(p2) if s["len"] > 4000]
        if ss:
            snd = (e2["name"], ss[0], XM.decode_sample(arc.release, p2.b[ss[0]["ptr"]:ss[0]["ptr"] + ss[0]["len"]], "s"))
            break
    if snd:
        refused("sound", mod("sound", files={"horn.wav": wav_bytes(snd[2], snd[1]["rate"])}),
                "horn.wav", "game data", f"base://HW_Cars/{snd[0]}/sound/")
        part = np.concatenate([np.zeros(333, np.int16), snd[2][501:3001]])
        refused("sound-part", mod("sound-part", files={"horn.wav": wav_bytes(part)}), "horn.wav", "game data", "sound/")
        # decoded audio: a game sound as FLAC (lossless), MP3 (lossy) and resampled stays the game's
        if shutil.which("ffmpeg") and XM.find_audiodec():
            src = OUT / "game.wav"
            src.write_bytes(wav_bytes(snd[2], snd[1]["rate"]))
            for ext, args, words in ((".flac", [], "game's sound"),
                                     (".mp3", ["-b:a", "96k"], "sounds like the game's sound"),
                                     (".ogg", ["-c:a", "vorbis", "-strict", "-2", "-ac", "2", "-ar", "44100"],
                                      "sounds like the game's sound")):
                f = OUT / f"game{ext}"
                subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", src, *args, f], check=True)
                refused(f"sound-as{ext}", mod(f"sound-as{ext}", files={f"horn{ext}": f.read_bytes()}),
                        f"horn{ext}", words, "sound/")
            longer = OUT / "game-slow.wav"     # resampled to 32 kHz
            longer.write_bytes(wav_bytes(XM.resample(snd[2], snd[1]["rate"], 32000), 32000))
            refused("sound-resampled", mod("sound-resampled", files={"horn.wav": longer.read_bytes()}),
                    "horn.wav", "sounds like the game's sound", "sound/")
            for f in (src, longer, *(OUT / f"game{e}" for e in (".flac", ".mp3", ".ogg"))):
                f.unlink(missing_ok=True)
        else:
            print("skip: compressed sound checks (needs ffmpeg to make the test files, and hwvx-audiodec)")
    else:
        check("a sound to test with", False)
    blob = XG.Blob(pk.section("data"), set(pk.words("pointers")), arc.release)
    pos = max((M._positions(blob, off) for off in XG.scan(blob)), key=lambda p: sum(len(x) for x in p))
    refused("mesh", mod("mesh", files=gltf(np.concatenate(pos))), "mesh.gltf", "vertex positions are the game's",
            "base://HW_Cars/car_5/geometry/")

    def restate(doc):
        doc["cars"]["uid://nightshiftcar"]["patch"]["CarAInfo/0"]["upshiftRpm"] = 6500
    refused("patch-restates", mod("patch-restates", restate), "upshiftRpm", "restates the game's own value")
    code, out = run("pack", mod("patch-restates", restate), "-o", OUT / "stripped.xmod", "--index", INDEX,
                    "--allow-partial-index", "--strip-unchanged")
    doc = json.loads(M.read_package(OUT / "stripped.xmod")["mod.jsonc"]) if code == 0 else {}
    check("--strip-unchanged leaves the restated field out",
          code == 0 and doc["cars"]["uid://nightshiftcar"]["patch"]["CarAInfo/0"] ==
          {"maxRpm": 8200, "turboTorqueScale": 1.3}, out)

    # bytes written as text: hex with separators, base64 lines, arrays of numbers
    chunk = raw[d0 + 0x2000:d0 + 0x2000 + 96]
    refused("hex-in-txt", mod("hex-in-txt", files={"notes.txt": ("my numbers:\n" + ", ".join(
        f"0x{b:02X}" for b in chunk) + "\n").encode()}), "notes.txt: hex", "base://HW_Cars/car_5")
    refused("hex-words-in-md", mod("hex-words-in-md", files={"notes.md": " ".join(
        chunk[i:i + 4].hex() for i in range(0, 96, 4)).encode()}), "notes.md: hex", "base://HW_Cars/car_5")
    b64 = base64.b64encode(raw[d0 + 0x3000:d0 + 0x3000 + 300]).decode()
    refused("base64-in-md", mod("base64-in-md", files={"README.md": ("# data\n\n" + "\n".join(
        b64[i:i + 76] for i in range(0, len(b64), 76)) + "\n").encode()}), "README.md: base64",
            "base://HW_Cars/car_5")
    refused("u8-array", mod("u8-array", files={"t.json": json.dumps({"t": list(chunk)}).encode()}),
            "t.json: t (u8)", "base://HW_Cars/car_5")
    fl = next(o for o in range(d0 + 0x1000, d0 + dn - 128, 4)
              if np.isfinite(np.frombuffer(raw[o:o + 128], ">f4")).all() and
              len(set(raw[o:o + 128])) > 40)
    floats = np.frombuffer(raw[fl:fl + 128], ">f4" if arc.release == "gc" else "<f4").astype(float).tolist()
    refused("f32-array", mod("f32-array", files={"t.json": json.dumps({"v": [floats[i:i + 4] for i in range(
        0, 32, 4)]}).encode()}), "t.json: v (f32", "base://HW_Cars/car_5")

    def rel_ps2(doc):
        doc["releases"] = ["gc", "ps2"]
    code, out = run("pack", mod("releases", rel_ps2), "-o", OUT / "releases.xmod", "--index", INDEX)
    check("refused: releases the index lacks", code == 1 and "ps2, which the index has no data of" in out, out)
    code, out = run("pack", mod("releases", rel_ps2), "-o", OUT / "releases.xmod", "--index", INDEX,
                    "--allow-partial-index")
    check("--allow-partial-index records them", code == 0 and json.loads(M.read_package(
        OUT / "releases.xmod")["check.json"])["unindexed_releases"] == ["ps2"], out)

    # mistakes in the manifest
    def e_field(doc):
        doc["patches"]["base://HW_Cars/car_7/CarAInfo/0/accel"]["warpDrive"] = 9
    refused("unknown-field", mod("unknown-field", e_field), "CarAAccel has no field warpDrive")

    def e_link(doc):
        doc["patches"]["base://HW_Cars/car_7/CarAInfo/0"] = {"engine": 5}
    refused("link-field", mod("link-field", e_link), "is a link")

    def e_count(doc):
        doc["cars"]["uid://nightshiftcar"]["patch"]["CarAInfo/0"]["numWheels"] = 9
    refused("array-count", mod("array-count", e_count), "numWheels counts an array")

    def e_type(doc):
        doc["cars"]["uid://nightshiftcar"]["patch"]["CarAInfo/0"]["skidLife"] = 70000
    refused("value-range", mod("value-range", e_type), "doesn't fit u16")

    def e_tex(doc):
        doc["recipes"]["base://HW_Cars/car_7/texture/99"] = [{"op": "tint", "color": [1, 1, 0]}]
    refused("no-such-texture", mod("no-such-texture", e_tex), "no texture base://HW_Cars/car_7/texture/99")

    def e_obj(doc):
        doc["patches"]["base://HW_Cars/car_70/CarAInfo/0"] = {"maxRpm": 1}
    refused("no-such-object", mod("no-such-object", e_obj), "no object base://HW_Cars/car_70/CarAInfo/0")

    def e_op(doc):
        doc["recipes"]["base://HW_Cars/car_7/texture/1"] = [{"op": "paste", "file": "x.png"}]
    refused("unknown-op", mod("unknown-op", e_op), '"op" is one of')

    def e_uid(doc):
        doc["cars"]["My Car!"] = doc["cars"].pop("uid://nightshiftcar")
    refused("bad-uid", mod("bad-uid", e_uid), "uid://")

    def e_own(doc):
        doc["cars"]["uid://nightshiftcar"].pop("extends")
    refused("no-extends", mod("no-extends", e_own), '"extends" is missing')
    refused("exe", mod("exe", files={"tool.exe": b"MZ" + bytes(100)}), "tool.exe", "can't be checked")

    # a modder's own work goes through
    rng = np.random.default_rng(7)
    yy, xx = np.mgrid[0:128, 0:128]
    art = np.zeros((128, 128, 4), np.uint8)
    art[:, :, 0] = (127 + 120 * np.sin(xx / 9.0 + yy / 23.0)).astype(np.uint8)
    art[:, :, 1] = ((xx * 3 + yy * 5) % 256).astype(np.uint8)
    art[:, :, 2] = rng.integers(0, 256, (128, 128))
    art[:, :, 3] = 255
    t = np.arange(22050)
    tone = (9000 * np.sin(t * 0.07) * np.exp(-t / 9000)).astype(np.int16)
    u, v = np.meshgrid(np.linspace(0, np.pi, 24), np.linspace(0, 2 * np.pi, 24))
    sphere = np.stack([np.sin(u) * np.cos(v) * 1.37, np.cos(u) * 0.91, np.sin(u) * np.sin(v) * 2.2], -1).reshape(-1, 3)
    own = {"art/decal.png": png_bytes(art), "sound/beep.wav": wav_bytes(tone), "notes.txt": b"made by me\n",
           "data/table.json": json.dumps({"curve": list(range(200)), "blob": bytes(rng.integers(0, 256, 200,
                                                                                               dtype=np.uint8)).hex()}).encode()}
    own.update({"model/" + k: v2 for k, v2 in gltf(sphere, "ball").items()})
    code, out = run("pack", mod("own-work", files=own), "-o", OUT / "own.xmod", "--index", INDEX,
                    "--allow-partial-index")
    check("a mod with the modder's own PNG, WAV, glTF and JSON packs", code == 0 and "7 files" in out, out)
    # the modder's own sounds, compressed, replacing a base sound
    if shutil.which("ffmpeg") and XM.find_audiodec():
        chirp = (8000 * np.sin(2 * np.pi * (300 * t / 22050 + 400 * (t / 22050) ** 2))).astype(np.int16)
        (OUT / "own.wav").write_bytes(wav_bytes(np.concatenate([chirp, tone])))
        comp = {}
        for ext in (".flac", ".mp3", ".ogg"):
            f = OUT / f"own{ext}"
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", OUT / "own.wav",
                            *(["-c:a", "vorbis", "-strict", "-2", "-ac", "2"] if ext == ".ogg" else []), f], check=True)
            comp[f"sound/own{ext}"] = f.read_bytes()

        def sounds(doc):
            doc["sounds"] = {"base://HW_Cars/car_7/sound/0": "sound/own.ogg",
                             "base://HW_Cars/car_7/sound/1": {"file": "sound/own.mp3", "loop": 100},
                             "base://HW_Cars/car_7/sound/2": {"file": "sound/own.flac", "loop": None}}
        code, out = run("pack", mod("own-sounds", sounds, files=comp), "-o", OUT / "own-sounds.xmod", "--index",
                        INDEX, "--allow-partial-index")
        check("a mod with its own FLAC, MP3 and Ogg sounds over base sounds packs", code == 0, out)

        def bad(doc):
            doc["sounds"] = {"base://HW_Cars/car_7/sound/0": "sound/missing.ogg",
                             "base://HW_Cars/car_7/texture/0": "sound/own.ogg",
                             "base://HW_Cars/car_7/sound/1": {"file": "../x.mp3"}}
        code, out = run("pack", mod("bad-sounds", bad, files=comp), "-o", OUT / "bad-sounds.xmod", "--index", INDEX,
                        "--allow-partial-index")
        check("sounds: a missing file, a texture id and a path out of the mod are refused",
              code == 1 and "missing.ogg isn't in the mod" in out and "a sound's id" in out and "a sound file of the mod" in out, out)
        for f in OUT.glob("own.*"):
            f.unlink()
    (OUT / "tmp.png").unlink(missing_ok=True)

    hwvx = Path(os.environ.get("HWVX_BIN", M.ROOT / "build" / "hwvx"))
    installers(hwvx if hwvx.is_file() else None)
    print(f"{fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
