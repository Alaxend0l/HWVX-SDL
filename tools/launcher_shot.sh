#!/bin/bash
# tools/launcher_shot.sh BUILD PREF OUT.png TAB [INPUT] [VAR=value ...]
#
# A screenshot of the launcher: opens page TAB (play, games, saves, convert,
# mods, settings; settings/SECTION: display, audio, controls, game) with
# save folder PREF (HWVX_PREF_DIR: copy a sources.cfg there, never use the
# real one), plays HWVX_LAUNCHER_INPUT INPUT (e.g.
# "down,enter,waitconvert"; "" for none), saves the frame as OUT.png and
# closes. Extra VAR=value are set for the run. The launcher's log goes to
# OUT.log. Kills the program if it hasn't closed within LIMIT s (600).
B="$1"; P="$2"; O="$3"; TAB="$4"; IN="$5"; shift 5
mkdir -p "$P" "$(dirname "$O")"
bmp="${O%.png}.bmp"
rm -f "$bmp"
env HWVX_PREF_DIR="$P" HWVX_LAUNCHER_TAB="$TAB" HWVX_LAUNCHER_INPUT="$IN" HWVX_LAUNCHER_SHOT="$bmp" "$@" \
    "$B/hwvx" --launcher > "${O%.png}.log" 2>&1 &
pid=$!
for _ in $(seq 1 $(( ${LIMIT:-600} * 10 ))); do
    kill -0 $pid 2>/dev/null || break
    sleep 0.1
done
kill -9 $pid 2>/dev/null
wait $pid 2>/dev/null
[ -f "$bmp" ] || { echo "no frame saved (see ${O%.png}.log)"; exit 1; }
sips -s format png "$bmp" --out "$O" > /dev/null && rm -f "$bmp"
exit 0

