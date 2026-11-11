#!/bin/bash
# tools/run_test.sh BUILD DATADIR OUTDIR NAME SECONDS [VAR=value ...]
#
# One deterministic, force-killed game run for testing: a fresh preferences
# folder (OUTDIR/pref-NAME, never the player's), every 60th frame in
# OUTDIR/dump-NAME, the log in OUTDIR/NAME.log. FAST=1 runs it faster than
# real time (HWVX_NO_VIWAIT=1 HWVX_NO_VSYNC=1: fixed 60 Hz steps, no waiting).
# FROMPREF=OTHER starts from OUTDIR/pref-OTHER's files. Extra VAR=value
# arguments go to the game (HWVX_START=carselect, HWVX_MENUDUMP=1, ...).
B="$1"; D="$2"; O="$3"; N="$4"; T="$5"; shift 5
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -rf "$O/dump-$N" "$O/pref-$N"; mkdir -p "$O/dump-$N" "$O/pref-$N"
[ -n "$FROMPREF" ] && cp -R "$O/pref-$FROMPREF/." "$O/pref-$N/"
[ -n "$FAST" ] && set -- HWVX_NO_VIWAIT=1 HWVX_NO_VSYNC=1 "$@"
env HWVX_NO_MOVIE=1 HWVX_NO_DIALOG=1 HWVX_DETERMINISTIC=1 HWVX_SEED=${HWVX_SEED:-3} "$@" \
    HWVX_PREF_DIR="$O/pref-$N" HWVX_DUMP="$O/dump-$N" \
    "$B/hwvx" --windowed --window 1280x960 --data "$D" > "$O/$N.log" 2>&1 &
P=$!
for i in $(seq "$T"); do sleep 1; kill -0 $P 2>/dev/null || break; done
kill -9 $P 2>/dev/null && echo "$N: ran $T s" || echo "$N: exited early: $(wait $P; echo $?)"
wait $P 2>/dev/null
echo "$N: $(ls "$O/dump-$N" | wc -l | tr -d ' ') frames"
