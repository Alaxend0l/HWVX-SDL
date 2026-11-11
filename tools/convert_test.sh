#!/bin/bash
# tools/convert_test.sh BUILD DATADIR OUTDIR NAME SECONDS [VAR=value ...]
#
# Tests the ahead-of-time .xdl conversion (hwvx --convert-xdl, the launcher's
# Convert page) on one release's data folder:
#   1. converts DATADIR into OUTDIR/conv-NAME (skipped if it is there already
#      with a manifest; RECONVERT=1 converts again), timing it;
#   2. runs the game SECONDS s from the source (HWVX_XDL=0) and from the
#      converted packs (HWVX_XDL=1 HWVX_XDL_DIR=...), both through
#      tools/run_test.sh with FAST=1: deterministic, every 60th frame dumped;
#   3. compares the frames (tools/cmp_frames.sh) and checks the cached run
#      read the cache.
# Extra VAR=value go to both runs (HWVX_START=race:mode=joyride,world=world1).
# One game at a time; nothing outside OUTDIR is written.
T=$(cd "$(dirname "$0")"; pwd)
B="$1"; D="$2"; O="$3"; N="$4"; S="$5"; shift 5
mkdir -p "$O"; O=$(cd "$O"; pwd)
C="$O/conv-$N"
if [ -n "$RECONVERT" ] || [ ! -f "$C/converted.txt" ]; then
    rm -rf "$C" "$O/pref-conv-$N"; mkdir -p "$O/pref-conv-$N"
    s=$(date +%s)
    HWVX_NO_DIALOG=1 HWVX_PREF_DIR="$O/pref-conv-$N" "$B/hwvx" --convert-xdl "$C" --data "$D" > "$O/conv-$N.log" 2>&1
    rc=$?
    echo "$N: converted in $(( $(date +%s) - s )) s, exit $rc: $(grep '^@' "$O/conv-$N.log" | tail -1)"
    [ $rc -eq 0 ] || exit 1
fi
FAST=1 "$T/run_test.sh" "$B" "$D" "$O" "$N-src" "$S" HWVX_XDL=0 "$@"
FAST=1 "$T/run_test.sh" "$B" "$D" "$O" "$N-xdl" "$S" HWVX_XDL=1 HWVX_XDL_DIR="$C" "$@"
echo "$N: $(grep -c 'xdl: /' "$O/$N-xdl.log") cache files opened, $(grep -c 'out of date' "$O/$N-xdl.log") out of date"
grep -q 'xdl: /' "$O/$N-xdl.log" || { echo "$N: the cached run read no cache"; exit 1; }
"$T/cmp_frames.sh" "$O/dump-$N-src" "$O/dump-$N-xdl"
