#!/bin/bash
# tools/wine_run.sh EXE DATA OUTDIR NAME SECONDS [VAR=value ...]
#
# The Windows build (a console one: -DHWVX_CONSOLE=ON, so its log reaches
# us) under CrossOver's Wine on macOS, like tools/run_test.sh: the attract
# demo (deterministic, no input), a fresh preferences folder
# (OUTDIR/pref-NAME), the log in OUTDIR/NAME.log, force-killed after
# SECONDS. Every 10 s the Wine process's resident size as macOS sees it goes
# to OUTDIR/NAME.mem ("seconds rss_MB"); HWVX_STATS=1 adds the game's own
# numbers (on Windows, Task Manager's: private bytes and working set).
# BOTTLE (default hwvx-win64) must be a 64-bit bottle:
#   cxbottle --bottle hwvx-win64 --create --template win10_64
# Paths go to Wine as Z:\... ; keep them free of spaces.
E="$1"; D="$2"; O="$3"; N="$4"; T="$5"; shift 5
W=${WINE:-/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine}
B=${BOTTLE:-hwvx-win64}
mkdir -p "$O"; O=$(cd "$O"; pwd)
E=$(cd "$(dirname "$E")"; pwd)/$(basename "$E")
D=$(cd "$D"; pwd)
winpath() { echo "Z:$1" | tr / '\\'; }
rm -rf "$O/pref-$N"; mkdir -p "$O/pref-$N"
env WINEDEBUG=-all HWVX_NO_MOVIE=1 HWVX_NO_DIALOG=1 HWVX_DETERMINISTIC=1 HWVX_SEED=${HWVX_SEED:-3} "$@" \
    HWVX_PREF_DIR="$(winpath "$O/pref-$N")" \
    "$W" --bottle "$B" "$E" --windowed --window ${WINDOW:-1280x960} --data "$(winpath "$D")" \
    > "$O/$N.log" 2>&1 &
exe=$(basename "$E")
pid=
for i in $(seq 30); do
    sleep 1
    pid=$(pgrep -f "^[A-Z]:.*$exe" | head -1)
    [ -n "$pid" ] && break
done
[ -z "$pid" ] && { echo "$N: did not start (see $O/$N.log)"; exit 1; }
: > "$O/$N.mem"
for i in $(seq "$T"); do
    sleep 1
    kill -0 "$pid" 2>/dev/null || { echo "$N: exited after $i s"; break; }
    [ $((i % 10)) = 0 ] && echo "$i $(ps -o rss= -p "$pid" | awk '{printf "%.1f", $1/1024}')" >> "$O/$N.mem"
done
kill -9 "$pid" 2>/dev/null
pkill -9 -f "winewrapper.exe --run -- $E" 2>/dev/null
echo "$N: $(tail -1 "$O/$N.mem") MB resident at the end"
