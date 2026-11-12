#!/bin/bash
# tools/level_share_test.sh BUILD DATA OUT [PROJECT] [INDEX]
#
# A level shared as a mod, as a player gets it (default: the sample Checkpoint Run):
#   1. packed: tools/xdl_level.py mod PROJECT --hwmod (the compiled level file and the project's
#      sources; tools/xdl_mod.py's no-game-data check against INDEX, else one made from DATA)
#   2. installed: hwvx --install-mod (Port_ModsInstall, what the launcher's Mods page runs) into a
#      fresh preferences folder, OUT/pref-share: its mods/ is the only place the level is
#   3. played once by a script of inputs (the intro's presses, then the gas held): the game must
#      apply the level to the player's own base world as it loads and end it won.
# No world pack is built anywhere: the port applies the level file in memory (Port_Level.c).
# Each game runs alone and is force-killed by its own PID (tools/run_test.sh).
set -u
B="$1"; D="$2"; O="$3"; P="${4:-$(dirname "$0")/level-samples/checkpoint-run}"; I="${5:-}"
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -rf "$O/share" "$O/share.hwmod" "$O/pref-share"
idx=(--data "$D"); [ -n "$I" ] && idx=(--index "$I")
python3 "$here/xdl_level.py" mod "$P" "$O/share" --hwmod "$O/share.hwmod" "${idx[@]}" || { echo "FAIL pack"; exit 1; }
echo "ok   packed: $(python3 "$here/xdl_mod.py" info "$O/share.hwmod" | head -1)"

mkdir -p "$O/pref-share"
HWVX_PREF_DIR="$O/pref-share" "$B/hwvx" --install-mod "$O/share.hwmod" > "$O/install.log" 2>&1 &
pid=$!
for i in $(seq 20); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 $pid 2>/dev/null
wait $pid; code=$?
[ $code = 0 ] || { echo "FAIL install ($code): $(tail -1 "$O/install.log")"; exit 1; }
echo "ok   installed: $(ls "$O/pref-share/mods")"

id=$(python3 -c "import sys; sys.path.insert(0, '$here'); import xdl_level as L; print(L.read_jsonc('$P/level.jsonc')['id'])")
mode=$(python3 -c "import sys; sys.path.insert(0, '$here'); import xdl_level as L; print(L.read_jsonc('$P/level.jsonc')['mode'])")
drive="${DRIVE_WIN:-308:a,377:a,446:a,515:a*3000}"
FROMPREF=share FAST=1 "$here/run_test.sh" "$B" "$D" "$O" played ${SECONDS_RUN:-60} HWVX_RENDERER=${HWVX_RENDERER:-opengl} \
    "HWVX_START=race:mode=$mode,world=$id" "HWVX_AUTOINPUT=$drive" >/dev/null 2>&1
log="$O/played.log"
fail=0
if grep -q "level: .*applied to" "$log"; then echo "ok   played: $(grep -o "level: uid://[^ ]* ([^)]*)" "$log" | head -1), from the installed mod"
else echo "FAIL played: the level wasn't applied ($(grep -o "level: not applied.*" "$log" | head -1))"; fail=1; fi
# won: the log's end (end_level, a round), or a challenge's "objective complete" screen (its end
# interface: the last frame green, as tools/level_test.sh judges it)
last="$O/dump-played/$(ls "$O/dump-played" | tail -1)"
green=$(python3 - "$last" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB").resize((64, 48))
r = g = b = 0
for x, y, z in im.getdata():
    r += x; g += y; b += z
print(1 if g > 1.15 * r and g > 1.5 * b else 0)
PY
)
case "$mode" in
    battle|drag) grep -q "rules: the round ends, player 1 wins it" "$log" ;;
    *) grep -q "progression: the level ends: won" "$log" || [ "$green" = 1 ] ;;
esac && echo "ok   played: won" || { echo "FAIL played: not won"; fail=1; }
exit $fail
