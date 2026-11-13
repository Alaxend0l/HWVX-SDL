#!/bin/bash
# tools/mix_pair_shots.sh BUILD MIXPREF REFDATA GCDATA OUTDIR NAME SECONDS FRAMES [VAR=value ...]
#
# The same scene twice, one after the other: the mixed build (--mix ultimate,
# its sources.cfg and xdl cache from the scratch folder MIXPREF) and a plain
# release (REFDATA, e.g. the PS2's folder), at 1280x720 with OpenGL. Saves
# OUTDIR/NAME_<frame>.png per frame in FRAMES ("1800 2400"): mixed on the
# left, the plain release on the right. Extra VAR=value go to both runs
# (HWVX_START=..., HWVX_AUTOINPUT=...). Uses tools/run_test.sh (force-killed,
# fresh preferences per run) and xdlrs mix pair (XDLRS=path, else built in
# tools/xdlrs/target/release). The run folders stay in OUTDIR/run.
B="$1"; MP="$2"; REF="$3"; GC="$4"; O="$5"; N="$6"; T="$7"; FR="$8"; shift 8
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O/run/pref-src$N"
cp "$MP/sources.cfg" "$O/run/pref-src$N/"
FAST=1 WINDOW=1280x720 FROMPREF="src$N" "$here/run_test.sh" "$B" "$GC" "$O/run" "mix$N" "$T" HWVX_MIX=ultimate \
    HWVX_XDL_DIR="$MP/xdl" HWVX_RENDERER=opengl SDL_AUDIO_DRIVER=dummy "$@"
FAST=1 WINDOW=1280x720 "$here/run_test.sh" "$B" "$REF" "$O/run" "ref$N" "$T" HWVX_RENDERER=opengl \
    SDL_AUDIO_DRIVER=dummy "$@"
X="${XDLRS:-$here/xdlrs/target/release/xdlrs}"
[ -x "$X" ] || cargo build --release --quiet --manifest-path "$here/xdlrs/Cargo.toml" || exit 1
for f in $FR; do
    name=$(printf 'frame%05d.bmp' "$f")
    if [ -f "$O/run/dump-mix$N/$name" ] && [ -f "$O/run/dump-ref$N/$name" ]; then
        "$X" mix pair "$O/run/dump-mix$N/$name" "$O/run/dump-ref$N/$name" "$O/${N}_$f.png" --half && echo "$O/${N}_$f.png"
    else
        echo "missing $name"
    fi
done
