#!/bin/bash
# tools/play_arena.sh [BUILD] [GC_DATA]
#
# Play the generated test arena (tools/xdl_chunk.py arena) yourself, keyboard or pad: generates
# the project, builds and stages it under build-chunk/play (the staged data folder links your
# GameCube data, which is never written to), and starts the game straight into a Joyride race on
# the arena, registered as track 57 by the build's ids.lock. Preferences go to a scratch folder
# (build-chunk/play/pref), never your own. BUILD defaults to ./build, GC_DATA to sources.cfg's gc=.
# Extra environment variables pass through (HWVX_FULLSCREEN etc.).
set -e
here=$(cd "$(dirname "$0")"; pwd); root=$(dirname "$here")
B="${1:-$root/build}"
D="${2:-$(/usr/bin/python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import world_lib as W; print(W.sources().get("gc", ""))' "$here")}"
[ -d "$D" ] || { echo "no GameCube data folder (give it as the second argument)"; exit 1; }
P="$root/build-chunk/play"
rm -rf "$P/project" "$P/out"; mkdir -p "$P/pref"
/usr/bin/python3 "$here/xdl_chunk.py" arena "$P/project" >/dev/null
/usr/bin/python3 "$here/xdl_chunk.py" build "$P/project" "$P/out" >/dev/null
/usr/bin/python3 "$here/xdl_chunk.py" stage "$P/project" "$P/out" "$D" "$P/data" >/dev/null
cp "$P/out/ids.lock" "$P/ids.lock"         # the game writes the numbers back: a copy
echo "arena staged in $P/data; starting (Joyride level 5 = the arena)"
exec env HWVX_PREF_DIR="$P/pref" HWVX_IDS_LOCK="$P/ids.lock" HWVX_START=race:mode=joyride,world=5 \
    "$B/hwvx" --windowed --data "$P/data"
