#!/bin/bash
# tools/gputime.sh BUILD DATADIR OUTDIR RENDERER WxH SECONDS [VAR=value ...]
#
# One timed run for fill-rate work: the game unpaced (no vsync, no VI wait,
# deterministic), HWVX_GPUTIME=1, a fresh preferences folder in OUTDIR, the
# window WxH in the window system's units (on a Retina Mac that is half the
# pixels: 640x480 draws 1280x960). Prints the run's gputime lines after the
# first 3 (start-up and loads) and the frame rate. Extra VAR=value go to the
# game: HWVX_START=race:mode=challenge HWVX_AUTOPILOT=1 for a race; none for
# the title's attract demo. The game is force-killed by its own PID.
B="$1"; D="$2"; O="$3"; R="$4"; S="$5"; T="$6"; shift 6
mkdir -p "$O"; O=$(cd "$O"; pwd)
N="$R-$S"; rm -rf "$O/pref-$N"; mkdir -p "$O/pref-$N"
env HWVX_NO_MOVIE=1 HWVX_NO_DIALOG=1 HWVX_DETERMINISTIC=1 HWVX_NO_VIWAIT=1 HWVX_NO_VSYNC=1 \
    HWVX_GPUTIME=1 HWVX_FPSLOG=1 "$@" HWVX_PREF_DIR="$O/pref-$N" \
    "$B/hwvx" --renderer "$R" --windowed --window "$S" --data "$D" > "$O/$N.log" 2>&1 &
P=$!
for i in $(seq "$T"); do sleep 1; kill -0 $P 2>/dev/null || break; done
kill -9 $P 2>/dev/null
wait $P 2>/dev/null
grep -iE "gputime|fps" "$O/$N.log" | sed 1,3d
