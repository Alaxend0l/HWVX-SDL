#!/bin/bash
# tools/level_test.sh BUILD DATA OUT [PROJECT]
#
# A level project played in the game (default: the sample, tools/level-samples/checkpoint-run):
# made into a mod (tools/xdl_level.py mod) in OUT/mods, then three runs with that mods folder
# (tools/run_test.sh: a fresh preferences folder each, force-killed by its own PID):
#   select  the mode's world select: the level is an entry of it (HWVX_MENUDUMP names)
#   win     the race, the intro dismissed and the gas held (DRIVE_WIN): the level's file is
#           applied, and the last frame is the green "objective complete" screen
#   lose    the same with no gas: the red "objective failed" screen once the clock runs out
# PACK=1: the level built into its own world pack (the test drive's way) instead of applied in memory.
# Frames: OUT/dump-<run>; OUT/level_test.png is the select, win and lose side by side.
# The OpenGL renderer: SDL_GPU (Metal) stalls while another window covers the game's.
set -u
B="$1"; D="$2"; O="$3"; P="${4:-$(dirname "$0")/level-samples/checkpoint-run}"
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -rf "$O/mods"
if [ "${PACK:-0}" = 1 ]; then
    # the level built into its own world pack (tools/xdl_level_pack.py), as the test drive does
    python3 "$here/xdl_level.py" testdrive "$P" "$O" --data "$D" --cache "$O/base-cache" >/dev/null || exit 1
else
    python3 "$here/xdl_level.py" mod "$P" "$O/mods/level" || exit 1
fi
mode=$(python3 -c "import sys; sys.path.insert(0, '$here'); import xdl_level as L; print(L.read_jsonc('$P/level.jsonc')['mode'])")
name=$(python3 -c "import sys; sys.path.insert(0, '$here'); import xdl_level as L; print(L.read_jsonc('$P/level.jsonc')['name'])")
common=(HWVX_RENDERER=${HWVX_RENDERER:-opengl} "HWVX_MODS_DIR=$O/mods")
fail=0

FAST=1 "$here/run_test.sh" "$B" "$D" "$O" select 15 "${common[@]}" HWVX_START=worldselect:mode=$mode HWVX_MENUDUMP=1 >/dev/null 2>&1
level=$(grep -o "content: progression [0-9]*: level [0-9]* is track [0-9]*" "$O/select.log" | head -1 | awk '{print $5}')
if grep -q "menudump:.*names.*\"$name\"" "$O/select.log" && [ -n "$level" ]; then
    echo "ok   select: \"$name\" is level $level of the $mode select"
else
    echo "FAIL select: \"$name\" not in the $mode select"; exit 1
fi
FAST=1 "$here/run_test.sh" "$B" "$D" "$O" selshot 12 "${common[@]}" HWVX_START=worldselect:mode=$mode,world=$level >/dev/null 2>&1

for run in win lose; do
    if [ $run = win ]; then drive="${DRIVE_WIN:-308:a,377:a,446:a,515:a*3000}"; else drive="${DRIVE_LOSE:-308:a,377:a,446:a}"; fi
    FAST=1 "$here/run_test.sh" "$B" "$D" "$O" $run ${SECONDS_RUN:-60} "${common[@]}" HWVX_START=race:mode=$mode,world=$level \
        "HWVX_AUTOINPUT=$drive" >/dev/null 2>&1
    if grep -q "level: .*applied to" "$O/$run.log"; then echo "ok   $run: the level applied"; else echo "FAIL $run: not applied"; fail=1; fi
    last=$(ls "$O/dump-$run" | tail -1)
    verdict=$(python3 - "$O/dump-$run/$last" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB").resize((64, 48))
r = g = b = 0
for x, y, z in im.getdata():
    r += x; g += y; b += z
print("win" if g > 1.15 * r and g > 1.5 * b else "lose" if r > 1.3 * g else "neither")
EOF
)
    if [ "$verdict" = $run ]; then echo "ok   $run: the last frame ($last) is the $run screen"; else echo "FAIL $run: the last frame ($last) looks like $verdict"; fail=1; fi
done
python3 - "$O" <<'EOF'
import sys, glob
from PIL import Image
o = sys.argv[1]
ims = [Image.open(sorted(glob.glob(f"{o}/dump-{r}/*.bmp"))[-1]).resize((480, 360)) for r in ("selshot", "win", "lose")]
s = Image.new("RGB", (1440, 360))
for i, im in enumerate(ims):
    s.paste(im, (480 * i, 0))
s.save(f"{o}/level_test.png")
EOF
echo "$O/level_test.png"
exit $fail
