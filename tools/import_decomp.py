#!/usr/bin/env python3
"""Import game sources from the HWVX-GC-Decomp submodule into src/game.

Applies only mechanical CodeWarrior -> GCC transforms. Portability fixes
(handles, endianness, SDK replacement) are made by hand in src/game afterwards,
so by default this refuses to overwrite files that already exist.

  tools/import_decomp.py            # import files that are missing
  tools/import_decomp.py --force X  # re-import file X (discards local edits)
  tools/import_decomp.py --diff     # print the decomp commit this was taken from
  tools/import_decomp.py --merge    # three-way merge decomp changes since
                                    # DECOMP_REVISION into the patched files
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DECOMP = ROOT / "HWVX-GC-Decomp"
DEST = ROOT / "src" / "game"

# Files from the decomp's "game" library that are really SDK / hardware code and
# are replaced by src/port instead of being imported.
EXCLUDE = {
    "PPCArch.c",          # PowerPC register helpers
    "OS.c",               # Dolphin OS boot
    "DebuggerDriver.c",   # AMC/TRK debugger
    "Main_NGC.c",         # GameCube entry/VI/arena setup -> src/port/main_ngc.c
    "allsrc.c",           # Logitech wheel force-feedback SDK -> SDL haptics
    "THPDec.c",           # THP video decoder (PowerPC paired-single)
    "THPAudioDecode.c",
    "THPVideoDecode.c",
    "THPPlayer.c",
    "THPDraw.c",
    "THPPlay.c",
    "THPRead.c",
    "THPAudio.c",
}

CW_PRAGMA = re.compile(
    r"^\s*#pragma\s+(push|pop|optimization_level|optimize_for_size|use_lmw_stmw|"
    r"readonly_strings|dont_inline|auto_inline|opt_\w+|peephole|peepblock|scheduling|"
    r"fp_contract|inline_depth|force_active|section|sym)\b.*$", re.M)
FIFO_DECL = re.compile(r"^.*PPCWGPipe\s+GXWGFifo\s*:\s*0x[0-9A-Fa-f]+\s*;\s*$", re.M)
FIFO_WRITE = re.compile(r"GXWGFifo\.(u8|s8|u16|s16|u32|s32|f32)\s*=\s*([^;]+);")


def game_objects():
    src = (DECOMP / "configure.py").read_text()
    m = re.search(r'"lib": "game".*?"objects": \[(.*?)\]', src, re.S)
    return re.findall(r'Object\(\w+, "([^"]+)"', m.group(1))


def transform(text):
    text = CW_PRAGMA.sub("", text)
    text = FIFO_DECL.sub("", text)
    text = FIFO_WRITE.sub(r"GXWGFifo_\1(\2);", text)
    return text


def show(rev, rel):
    r = subprocess.run(["git", "-C", str(DECOMP), "show", f"{rev}:{rel}"],
                       capture_output=True)
    return r.stdout.decode(errors="replace") if r.returncode == 0 else None


def merge(files, old, new):
    """Apply decomp changes old..new to the local copies. The local file is
    "ours", the old decomp file the base: local rewrites survive, new decomp
    code arrives unrewritten (rerun the tools afterwards). Conflicts are left
    as markers."""
    conflicts = []
    for rel, dst in files:
        base, theirs = show(old, rel), show(new, rel)
        if theirs is None or base == theirs:
            continue
        theirs = transform(theirs)
        if not dst.exists():
            dst.write_text(theirs)
            print(f"new      {dst.relative_to(ROOT)}")
            continue
        with tempfile.TemporaryDirectory() as tmp:
            b = pathlib.Path(tmp) / "base"
            t = pathlib.Path(tmp) / "theirs"
            b.write_text(transform(base or ""))
            t.write_text(theirs)
            r = subprocess.run(["git", "merge-file", "-L", "port", "-L", old[:10],
                                "-L", new[:10], str(dst), str(b), str(t)])
        status = "merged" if r.returncode == 0 else f"CONFLICT ({r.returncode})"
        if r.returncode:
            conflicts.append(dst)
        print(f"{status:8} {dst.relative_to(ROOT)}")
    return conflicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", nargs="*", default=[])
    ap.add_argument("--diff", action="store_true")
    ap.add_argument("--merge", action="store_true")
    args = ap.parse_args()

    rev = subprocess.run(["git", "-C", str(DECOMP), "rev-parse", "HEAD"],
                         capture_output=True, text=True).stdout.strip()
    if args.diff:
        print(rev)
        return
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "include").mkdir(exist_ok=True)

    files = [("src/" + f, DEST / f) for f in game_objects() if f not in EXCLUDE]
    files += [("include/" + p.name, DEST / "include" / p.name)
              for p in (DECOMP / "include").glob("*.h")]
    if args.merge:
        old = (DEST / "DECOMP_REVISION").read_text().strip()
        conflicts = merge(files, old, rev)
        (DEST / "DECOMP_REVISION").write_text(rev + "\n")
        if conflicts:
            sys.exit(f"{len(conflicts)} file(s) with conflicts")
        return
    n = 0
    for rel, dst in files:
        if dst.exists() and dst.name not in args.force:
            continue
        dst.write_text(transform((DECOMP / rel).read_text(errors="replace")))
        n += 1
    (DEST / "DECOMP_REVISION").write_text(rev + "\n")
    print(f"imported {n} files from decomp {rev[:10]}", file=sys.stderr)


if __name__ == "__main__":
    main()
