#!/bin/bash
# tools/mix_pair_shots.sh BUILD MIXPREF REFDATA GCDATA OUTDIR NAME SECONDS FRAMES [VAR=value ...]
#
# The same scene twice, one after the other: the mixed build (--mix ultimate,
# its sources.cfg and xdl cache from the scratch folder MIXPREF) and a plain
# release (REFDATA, e.g. the PS2's folder), at 1280x720 with OpenGL. Saves
# OUTDIR/NAME_<frame>.png per frame in FRAMES ("1800 2400"): mixed on the
# left, the plain release on the right. Extra VAR=value go to both runs
# (HWVX_START=..., HWVX_AUTOINPUT=...). Uses tools/run_test.sh (force-killed,
# fresh preferences per run).
B="$1"; MP="$2"; REF="$3"; GC="$4"; O="$5"; N="$6"; T="$7"; FR="$8"; shift 8
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O/run/pref-src$N"
cp "$MP/sources.cfg" "$O/run/pref-src$N/"
FAST=1 WINDOW=1280x720 FROMPREF="src$N" "$here/run_test.sh" "$B" "$GC" "$O/run" "mix$N" "$T" HWVX_MIX=ultimate \
    HWVX_XDL_DIR="$MP/xdl" HWVX_RENDERER=opengl SDL_AUDIO_DRIVER=dummy "$@"
FAST=1 WINDOW=1280x720 "$here/run_test.sh" "$B" "$REF" "$O/run" "ref$N" "$T" HWVX_RENDERER=opengl \
    SDL_AUDIO_DRIVER=dummy "$@"
for f in $FR; do
    /usr/bin/python3 - "$O/run/dump-mix$N" "$O/run/dump-ref$N" "$f" "$O/${N}_$f.png" <<'EOF'
import sys
from PIL import Image
a, b, f, out = sys.argv[1:5]
name = "frame%05d.bmp" % int(f)
try:
    l = Image.open(f"{a}/{name}").convert("RGB")
    r = Image.open(f"{b}/{name}").convert("RGB")
except FileNotFoundError as e:
    print("missing", e)
    sys.exit(0)
w, h = l.size
s = Image.new("RGB", (w * 2, h))
s.paste(l, (0, 0))
s.paste(r, (w, 0))
s.resize((w, h // 2)).save(out)
print(out)
EOF
done
