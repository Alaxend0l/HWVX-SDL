#!/bin/bash
# tools/run_bt.sh BUILD DATADIR OUTDIR NAME [VAR=value ...]
#
# Where does it crash: one deterministic, fast game run (as run_test.sh FAST=1)
# under lldb, which prints the backtrace of whatever stops it (a signal, a
# trap) and kills the game. Runs until the crash, or SECONDS (default 30)
# without one. The log is in OUTDIR/NAME.log, the preferences in
# OUTDIR/pref-NAME (never the player's).
#   tools/run_bt.sh build ../orig/gc/files build/t crash HWVX_START=race:car=99
B="$1"; D="$2"; O="$3"; N="$4"; shift 4
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -rf "$O/pref-$N"; mkdir -p "$O/pref-$N"
env HWVX_NO_MOVIE=1 HWVX_NO_DIALOG=1 HWVX_DETERMINISTIC=1 HWVX_SEED=${HWVX_SEED:-3} \
    HWVX_NO_VIWAIT=1 HWVX_NO_VSYNC=1 "$@" HWVX_PREF_DIR="$O/pref-$N" \
    lldb -b -o run -k "bt ${FRAMES:-16}" -k kill -k quit -- \
    "$B/hwvx" --windowed --window 640x480 --data "$D" > "$O/$N.log" 2>&1 &
P=$!
for i in $(seq "${SECONDS_MAX:-30}"); do sleep 1; kill -0 $P 2>/dev/null || break; done
if kill -0 $P 2>/dev/null; then
    pkill -9 -P $P 2>/dev/null; kill -9 $P 2>/dev/null
    echo "$N: no crash in ${SECONDS_MAX:-30} s"
else
    grep -E "stop reason|frame #" "$O/$N.log" | cut -c1-220
fi
wait $P 2>/dev/null
