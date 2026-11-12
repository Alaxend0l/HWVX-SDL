#!/bin/bash
# tools/crash_bt.sh BUILD DATA OUT SECONDS [VAR=value ...]
# One game run under lldb (batch): on a crash or Port_Fatal (abort), the
# backtrace with file:line in OUT/bt.log. The game and lldb are force-killed
# after SECONDS. Fresh preferences in OUT/pref-bt. The prototype's folder
# from sources.cfg's ps2beta= unless HWVX_PROTO_DIR is set.
B="$1"; D="$2"; O="$3"; T="$4"; shift 4
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -rf "$O/pref-bt"; mkdir -p "$O/pref-bt"
if [ -z "$HWVX_PROTO_DIR" ]; then
    HWVX_PROTO_DIR=$(grep '^ps2beta=' "$HOME/Library/Application Support/HWVX-SDL/HotWheelsVelocityX/sources.cfg" 2>/dev/null | cut -d= -f2-)
fi
printf 'run\nbt 16\nframe variable\nkill\nquit\n' > "$O/bt.cmds"
env HWVX_NO_MOVIE=1 HWVX_NO_DIALOG=1 HWVX_DETERMINISTIC=1 HWVX_NO_VIWAIT=1 HWVX_NO_VSYNC=1 HWVX_SEED=3 \
    HWVX_PROTO_DIR="$HWVX_PROTO_DIR" "$@" HWVX_PREF_DIR="$O/pref-bt" \
    lldb -b -s "$O/bt.cmds" -- "$B/hwvx" --windowed --window 640x480 --data "$D" > "$O/bt.log" 2>&1 &
P=$!
# only this run's processes: lldb ($P) and what it started (debugserver, the
# game), by PID; never by name (other games may be running)
tree() { local c; for c in $(pgrep -P "$1"); do tree "$c"; done; echo "$1"; }
for i in $(seq "$T"); do sleep 1; kill -0 $P 2>/dev/null || break; done
PIDS=$(tree $P)
kill -9 $PIDS 2>/dev/null
grep -E 'frame #|stop reason' "$O/bt.log" | head -20
