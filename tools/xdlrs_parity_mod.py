#!/usr/bin/env python3
"""Parity of tools/xdlrs's mod check (`xdlrs mod`, src/modcheck) with tools/xdl_mod.py.

    python3 tools/xdlrs_parity_mod.py [--only modindex,modfiles,modcheck] [--reuse] [--gc DIR]
    (also run by tools/xdlrs_parity.py)

  modindex  `index` of the whole GameCube install, and HW_Cars/HW_IPack/HW_Items of the PC
            and PS2 ones, by both: every array of the .npz and the metadata identical; timed
  modfiles  what the check reads from a file, by both: PNGs of every colour type, depth and
            tRNS (pixel hashes, gradient hash), sound files (WAV kinds, FLAC, MP3, Ogg, the
            game's own sounds re-encoded: decoded samples and fingerprint), text and JSON
            with bytes in them (the runs found)
  modcheck  `check` and `pack` of every mod in tools/mod-samples, every case of
            tools/test_xdl_mod.py (its own run() calls, mirrored), mod_assets/monument_remix:
            exit code, printed text, the .xmod's bytes; timed

--reuse keeps indexes already made under OUT (the whole-install one takes the Python minutes).
OUT: build/xdlrs-parity/mod. Needs `cargo build --release` in tools/xdlrs, ffmpeg for the
compressed sound files (skipped without).
"""

import argparse
import contextlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import xdl_mod as M  # noqa: E402

XM = M.XM
BIN = HERE / "xdlrs" / "target" / "release" / "xdlrs"
MONUMENT = next((d / "mod_assets" / "monument_remix" for d in ROOT.parents
                 if (d / "mod_assets" / "monument_remix" / "mod.jsonc").is_file()), None)
RESULTS = []
TIMES = {}


def default_report(name, total, bad, notes=()):
    RESULTS.append((name, total, bad))
    print(f"{'ok  ' if not bad else 'FAIL'} {name}: {total - len(bad)}/{total} identical")
    for b in bad[:8]:
        print(f"       {b}")
    for n in notes:
        print(f"       note: {n}")


report = default_report


def tick(k, dt):
    TIMES[k] = TIMES.get(k, 0.0) + dt


def py_run(*argv):
    """xdl_mod.main() in-process: (code, stdout + stderr)."""
    buf = io.StringIO()
    old = sys.argv
    sys.argv = ["xdl_mod.py"] + [str(x) for x in argv]
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            code = M.main()
    except Exception as e:  # noqa: BLE001
        code = f"EXC {type(e).__name__}: {e}"
    finally:
        sys.argv = old
    return code, buf.getvalue()


def rs_run(*argv):
    p = subprocess.run([str(BIN), "mod", *map(str, argv), "--repo", str(ROOT)], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


# ---- the index ------------------------------------------------------------------------------

def compare_index(pa, pb):
    """Differences between two .npz indexes (arrays and metadata)."""
    a, b = np.load(pa), np.load(pb)
    bad = []
    if sorted(a.files) != sorted(b.files):
        bad.append(f"arrays {sorted(a.files)} vs {sorted(b.files)}")
    ma, mb = json.loads(a["meta"].tobytes()), json.loads(b["meta"].tobytes())
    for k in sorted(set(ma) | set(mb)):
        if ma.get(k) != mb.get(k):
            bad.append(f"meta {k}: {str(ma.get(k))[:120]} vs {str(mb.get(k))[:120]}")
    for k in a.files:
        if k == "meta" or k not in b.files:
            continue
        x, y = a[k], b[k]
        if x.shape != y.shape or x.dtype != y.dtype or not (x == y).all():
            n = int((x != y).sum()) if x.shape == y.shape else "-"
            bad.append(f"{k}: {x.shape} {x.dtype} vs {y.shape} {y.dtype}, {n} differ")
    return bad


def release_dir(rel, a):
    if rel == "gc" and a.gc:
        return Path(a.gc)
    d = XM.release_dirs().get(rel)
    if d is None:
        cfg = Path.home() / "Library/Application Support/HWVX-SDL/HotWheelsVelocityX/sources.cfg"
        if cfg.is_file():
            for ln in cfg.read_text().splitlines():
                if ln.startswith(rel + "="):
                    d = Path(ln.split("=", 1)[1])
    return d if d and d.is_dir() else None


def make_index(a, name, data, extra=()):
    """Both indexes of `data` -> (python's, rust's, bad)."""
    pyp, rsp = a.mout / f"py-{name}.npz", a.mout / f"rs-{name}.npz"
    if not (a.reuse and pyp.exists()):
        t = time.time()
        code, out = py_run("index", data, "-o", pyp, *extra)
        tick(f"index {name}: python", time.time() - t)
        print("   ", out.strip().splitlines()[-1] if out.strip() else code)
    if not (a.reuse and rsp.exists()):
        t = time.time()
        code, out = rs_run("index", data, "-o", rsp, *extra)
        tick(f"index {name}: rust", time.time() - t)
        print("   ", out.strip().splitlines()[-1] if out.strip() else code)
    return pyp, rsp, compare_index(pyp, rsp)


def t_modindex(a):
    total, bad = 0, []
    gc = release_dir("gc", a)
    if gc:
        _, _, b = make_index(a, "gc", gc)
        total += 1
        bad += [f"gc: {x}" for x in b]
    for rel in ("gc", "pc", "ps2"):
        d = release_dir(rel, a)
        if d is None:
            continue
        _, _, b = make_index(a, f"{rel}-part", d, ("--archives", "HW_Cars,HW_IPack,HW_Items"))
        total += 1
        bad += [f"{rel} part: {x}" for x in b]
    report("modindex", total, bad)


# ---- what the check reads -------------------------------------------------------------------

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)


def png(w, h, ct, depth, rows, plte=None, trns=None):
    raw = b"".join(b"\0" + r for r in rows)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ct, 0, 0, 0))
    if plte:
        out += chunk(b"PLTE", plte)
    if trns is not None:
        out += chunk(b"tRNS", trns)
    return out + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def crafted_pngs(rng):
    """PNGs of every colour type and depth, with and without tRNS (18 x 17: dhash too)."""
    out = {}
    w, h = 18, 17
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    for ct, depths in ((0, (1, 2, 4, 8, 16)), (2, (8, 16)), (3, (1, 2, 4, 8)), (4, (8, 16)), (6, (8, 16))):
        for depth in depths:
            mx = (1 << depth) - 1
            n = chans[ct] * w
            vals = rng.integers(0, mx + 1, (h, n))
            vals[:, :3] = [0, mx, 1]

            def row(r):
                if depth >= 8:
                    return b"".join(int(v).to_bytes(depth // 8, "big") for v in r)
                bits = "".join(format(int(v), f"0{depth}b") for v in r)
                bits += "0" * (-len(bits) % 8)
                return int(bits, 2).to_bytes(len(bits) // 8, "big")
            rows = [row(r) for r in vals]
            plte = bytes(rng.integers(0, 256, 3 * min(256, (1 << depth) - 1)).tolist()) if ct == 3 else None
            trs = [None]
            if ct == 0:
                trs += [struct.pack(">H", 0), struct.pack(">H", mx), struct.pack(">H", 1)]
            elif ct == 2:
                trs += [struct.pack(">HHH", *(int(x) for x in vals[0, :3])), struct.pack(">HHH", 256, 0, 0)]
            elif ct == 3:
                trs += [b"\x00", b"\xff\x00\xff", bytes(range(0, 250, 7))]
            for i, t in enumerate(trs):
                out[f"ct{ct}-d{depth}-t{i}.png"] = png(w, h, ct, depth, rows, plte, t)
    return out


def t_modfiles(a):
    import base64
    from PIL import Image
    rng = np.random.default_rng(11)
    d = a.mout / "files"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    bad, total = [], 0

    def probe(kind, p):
        r = subprocess.run([str(BIN), "mod", "probe", kind, str(p), "--repo", str(ROOT)], capture_output=True, text=True)
        return r.stdout.strip()

    # PNGs: crafted, the game's textures (exact, recoloured, resized, as palette PNGs)
    files = crafted_pngs(rng)
    gc = release_dir("gc", a)
    if gc:
        arc = XM.Archive(gc / "HW_Cars.xgc")
        for pname in ("car_5", "car_7", "car_12"):
            ent = arc.find(pname)
            pk = XM.RawPack(arc.release, ent["type"], arc.pack_bytes(ent), pname)
            for t in XM.scan_textures(pk)[:6]:
                o, n, lw, lh = t["levels"][0]
                img = XM.decode_level(arc.release, t["fmt"], pk.b[o:o + n], lw, lh)
                for k, im in enumerate((img, img[::-1].copy(), np.clip(img.astype(int) * 3 // 2, 0, 255).astype(np.uint8))):
                    b = io.BytesIO()
                    Image.fromarray(im).save(b, "PNG")
                    files[f"{pname}-{t['i']}-{k}.png"] = b.getvalue()
                b = io.BytesIO()
                Image.fromarray(img).resize((lw // 2 + 13, lh // 2 + 5), Image.LANCZOS).save(b, "PNG")
                files[f"{pname}-{t['i']}-small.png"] = b.getvalue()
                b = io.BytesIO()
                Image.fromarray(img).convert("RGB").quantize(64).save(b, "PNG")
                files[f"{pname}-{t['i']}-pal.png"] = b.getvalue()
    files["notpng.png"] = b"\x89PNG\r\n\x1a\nbroken"
    for n, b in files.items():
        p = d / n
        p.write_bytes(b)
        try:
            img, _ = XM.read_png(p)
            ph = M.pixel_hashes(img)
            dh = M.dhash(img)
            want = json.dumps({"w": img.shape[1], "h": img.shape[0], "px": f"{ph[0]:016x}", "rgb": f"{ph[1]:016x}",
                               "dhash": dh.view(np.uint8).tobytes().hex() if dh is not None else None})
        except Exception as e:  # noqa: BLE001
            want = f"ERR {type(e).__name__}"
        got = probe("png", p)
        total += 1
        if got.startswith("ERR") and want.startswith("ERR"):
            continue
        if got != want:
            bad.append(f"png {n}: {want[:150]} vs {got[:150]}")

    # sounds: WAV kinds, compressed files, the game's sounds re-encoded, the user's remix
    snd = []
    t = np.arange(30000)
    tone = (9000 * np.sin(t * 0.07) * np.exp(-t / 9000)).astype(np.int16)
    st = np.stack([tone, (tone[::-1] // 3).astype(np.int16)], 1)
    for name, pcm, rate in (("mono", tone, 22050), ("stereo", st, 44100)):
        p = d / f"{name}.wav"
        import wave
        with wave.open(str(p), "wb") as w:
            w.setnchannels(pcm.ndim and (1 if pcm.ndim == 1 else pcm.shape[1]))
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(np.asarray(pcm, "<i2").tobytes())
        snd.append(p)
    if gc:
        arc = XM.Archive(gc / "HW_Cars.xgc")
        for e2 in arc.entries[:30]:
            p2 = XM.RawPack(arc.release, e2["type"], arc.pack_bytes(e2), e2["name"])
            ss = [s for s in XM.scan_sounds(p2) if s["len"] > 4000][:1]
            for s in ss:
                pcm = XM.decode_sample(arc.release, p2.b[s["ptr"]:s["ptr"] + s["len"]], "s")
                p = d / f"game-{e2['name']}-{s['i']}.wav"
                XM.write_wav(p, pcm, s["rate"])
                snd.append(p)
            if len(snd) > 8:
                break
    if shutil.which("ffmpeg"):
        src = list(snd)
        for p in src[:5]:
            for ext, args in ((".flac", []), (".mp3", ["-b:a", "96k"]), (".ogg", ["-c:a", "vorbis", "-strict", "-2", "-ac", "2"]),
                              ("-s24.wav", ["-c:a", "pcm_s24le"]), ("-u8.wav", ["-c:a", "pcm_u8"]),
                              ("-f32.wav", ["-c:a", "pcm_f32le"]), ("-32k.wav", ["-ar", "32000"])):
                q = d / (p.stem + ext)
                subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(p), *args, str(q)], check=True)
                snd.append(q)
    else:
        print("       note: no ffmpeg: compressed sound files skipped")
    if MONUMENT:
        snd += sorted((MONUMENT / "music").glob("*.ogg"))
    (d / "trunc.wav").write_bytes(snd[0].read_bytes()[:1001])
    (d / "junk.ogg").write_bytes(b"OggS" + bytes(200))
    snd += [d / "trunc.wav", d / "junk.ogg"]
    for p in snd:
        try:
            pcm, rate, _, _ = XM.decode_audio(p)
            chans = [np.ascontiguousarray(pcm[:, c]) for c in range(pcm.shape[1])]
            mono = XM.to_mono(pcm) if len(chans) > 1 else chans[0]
            k, f = M.sound_fp(mono, rate)
            want = json.dumps({"channels": pcm.shape[1], "rate": rate, "frames": pcm.shape[0],
                               "pcm": f"{M.h64(pcm.astype('<i2').tobytes()):016x}", "keys": k.tolist(),
                               "frames_kept": f.tolist()})
        except Exception as e:  # noqa: BLE001
            want = f"ERR {type(e).__name__}: {e}"
        got = probe("fp", p)
        total += 1
        if got.startswith("ERR") and want.startswith("ERR"):
            continue
        if got != want:
            gj = json.loads(got) if got.startswith("{") else got
            wj = json.loads(want) if want.startswith("{") else want
            if isinstance(gj, dict) and isinstance(wj, dict) and {k: v for k, v in gj.items() if k not in ("keys", "frames_kept")} == \
                    {k: v for k, v in wj.items() if k not in ("keys", "frames_kept")}:
                nk = sum(x != y for x, y in zip(gj["keys"], wj["keys"])) + abs(len(gj["keys"]) - len(wj["keys"]))
                bad.append(f"sound {p.name}: same samples, {nk} of {len(wj['keys'])} fingerprint keys differ")
            else:
                bad.append(f"sound {p.name}: {want[:160]} vs {got[:160]}")

    # bytes in text and JSON
    chunk_ = bytes(rng.integers(0, 256, 300, dtype=np.uint8))
    b64 = base64.b64encode(chunk_).decode()
    texts = {
        "hex.txt": ", ".join(f"0x{b:02X}" for b in chunk_[:96]),
        "words.md": " ".join(chunk_[i:i + 4].hex() for i in range(0, 96, 4)),
        "b64.md": "# x\n\n" + "\n".join(b64[i:i + 76] for i in range(0, len(b64), 76)),
        "b64pad.txt": b64[:50] + "=" + b64[50:120] + "==\x1c" + b64[120:200],
        "mixed.txt": "héllo " + chunk_[:40].hex() + " — " + b64[:60] + "　" + chunk_[40:80].hex(" "),
        "j.json": json.dumps({"a": list(chunk_[:40]), "b": [[1.5, -2.25, 3e30, 4], [5, 6, 7, 8]] * 3, "s": [-3] * 40,
                              "u": "data:application/octet-stream;base64," + b64[:100], "h": chunk_[:64].hex(),
                              "n": {"deep": [b64[:88]]}, "e": [], "m": [1, "x"]}),
    }
    for n, s in texts.items():
        p = d / n
        p.write_text(s)
        try:
            tree = M.XS.jsonc_loads(s)
            want = [[w, b.hex()] for w, b in M.json_blobs(tree)]
        except Exception:  # noqa: BLE001
            want = [[w, b.hex()] for w, b in M.text_blobs(s)]
        got = probe("blobs", p)
        total += 1
        if json.dumps(want) != got:
            bad.append(f"blobs {n}: {json.dumps(want)[:150]} vs {got[:150]}")
    report("modfiles", total, bad)


# ---- check and pack -------------------------------------------------------------------------

def compare_run(argv, bad, label):
    """Both on one check / pack / index command line; pack's output path is swapped for the
    Rust (its .xmod compared byte for byte)."""
    argv = [str(x) for x in argv]
    t = time.time()
    pc, pout = py_run(*argv)
    tick("check/pack: python", time.time() - t)
    rargv = list(argv)
    pyx = rsx = None
    if argv[0] in ("pack", "index") and "-o" in argv:
        i = argv.index("-o") + 1
        pyx = Path(argv[i])
        rsx = pyx.with_name("rs-" + pyx.name)
        rargv[i] = str(rsx)
        if rsx.exists():
            rsx.unlink()
    t = time.time()
    rc, rout = rs_run(*rargv)
    tick("check/pack: rust", time.time() - t)
    if rsx:
        rout = rout.replace(str(M.Path(rargv[argv.index("-o") + 1])), str(M.Path(argv[argv.index("-o") + 1])))
    if argv[0] == "index":
        rout = "\n".join(ln for ln in rout.splitlines() if "MB, " not in ln)
        pout = "\n".join(ln for ln in pout.splitlines() if "MB, " not in ln)
    if label.startswith("error") and pc == rc == 2:
        # the reason in brackets is the library's (numpy's, zipfile's) own text
        pout, rout = pout.split(" (")[0], rout.split(" (")[0]
    if pc != rc or pout != rout:
        bad.append(f"{label}: python {pc} {pout.strip()[-300:]!r}\n         rust   {rc} {rout.strip()[-300:]!r}")
    elif argv[0] == "pack" and pc == 0 and pyx.read_bytes() != rsx.read_bytes():
        bad.append(f"{label}: the .xmod differs")
    elif argv[0] == "index" and pc == 0:
        bad += [f"{label}: {x}" for x in compare_index(pyx, rsx)]
    return pc, pout


def t_modcheck(a):
    import test_xdl_mod as T
    bad, n = [], [0]
    out = a.mout / "check"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    T.OUT = out

    def mirrored(*argv):
        n[0] += 1
        if argv[0] in ("index", "check", "pack"):
            return compare_run(argv, bad, f"test_xdl_mod {' '.join(Path(str(x)).name for x in argv[:2])}")
        return py_run(*argv)
    T.run = mirrored
    os.environ["HWVX_BIN"] = str(out / "no-port")       # the port's installer isn't part of this
    gc = release_dir("gc", a)
    old = sys.argv
    sys.argv = ["test_xdl_mod.py", str(gc)]
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            T.main()
    finally:
        sys.argv = old
    # the samples and the user's remix, against the whole index (and the partial one)
    full = a.mout / "py-gc.npz"
    part = out / "index-test.npz"
    mods = sorted(p for p in (HERE / "mod-samples").iterdir() if (p / "mod.jsonc").is_file())
    if MONUMENT:
        mods.append(MONUMENT)
    for m in mods:
        for idx in (full, part):
            if not idx.exists():
                continue
            for argv in (("check", m, "--index", idx), ("pack", m, "-o", out / f"{m.name}.xmod", "--index", idx),
                         ("pack", m, "-o", out / f"{m.name}.xmod", "--index", idx, "--allow-partial-index",
                          "--strip-unchanged")):
                n[0] += 1
                compare_run(argv, bad, f"{m.name} {argv[0]} {idx.name}")
    # errors before the check
    for argv in (("check", out / "nowhere", "--index", part), ("check", HERE / "mod-samples" / "night-shift", "--index",
                                                                 out / "missing.npz"),
                 ("check", HERE / "mod-samples" / "night-shift", "--index", HERE / "xdl_mod.py")):
        n[0] += 1
        compare_run(argv, bad, f"error {Path(str(argv[1])).name} {Path(str(argv[3])).name}")
    report("modcheck", n[0], bad)


def run_all(a, only=None, rep=None):
    global report
    if rep:
        report = rep
    a.mout = Path(a.out) / "mod" if not hasattr(a, "mout") else a.mout
    a.mout.mkdir(parents=True, exist_ok=True)
    for k in ("gc", "reuse"):
        if not hasattr(a, k):
            setattr(a, k, None)
    if not BIN.exists():
        raise SystemExit(f"{BIN}: run `cargo build --release` in tools/xdlrs first")
    for name, fn in (("modindex", t_modindex), ("modfiles", t_modfiles), ("modcheck", t_modcheck)):
        if only is None or name in only:
            fn(a)
    for k, v in sorted(TIMES.items()):
        print(f"       time {k}: {v:.1f} s")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=str(ROOT / "build" / "xdlrs-parity"))
    ap.add_argument("--only", default="")
    ap.add_argument("--gc", default=None)
    ap.add_argument("--reuse", action="store_true")
    a = ap.parse_args()
    run_all(a, set(a.only.split(",")) if a.only else None)
    fails = sum(1 for r in RESULTS if r[2])
    print(f"\n{len(RESULTS) - fails}/{len(RESULTS)} checks identical")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
