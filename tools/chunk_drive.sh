#!/bin/bash
# tools/chunk_drive.sh BUILD STAGED OUTDIR NAME LEVEL [SECONDS] [VAR=value ...]
#
# Drive a world built by tools/xdl_chunk.py: STAGED is `xdl_chunk.py stage`'s data folder,
# LEVEL the project's level archive (worlds_\sp_\joyride_\cworld1). The level is started as a
# registered Joyride track (HWVX_TEST_EXTRA_TRACKS, aliasing Joyride's first world, with
# HWVX_TEST_EXTRA_TRACK_WORLD = LEVEL) and driven by tools/drive_test.sh (gas held, a turn;
# DRIVE=... replaces the input). Prints the car's positions every 30th log line, the lowest
# height reached (the car fell through if it keeps going down), and writes OUTDIR/NAME.png:
# four frames of the run.
B="$1"; D="$2"; O="$3"; N="$4"; L="$5"; T="${6:-30}"; shift 5; shift 2>/dev/null
here=$(cd "$(dirname "$0")"; pwd)
"$here/drive_test.sh" "$B" "$D" "$O" "$N" "$T" "HWVX_TEST_EXTRA_TRACKS=alias=0" \
    "HWVX_TEST_EXTRA_TRACK_WORLD=$L" "HWVX_START=race:mode=joyride,world=5" "$@" > "$O/$N.car"
awk 'NR % 30 == 1 { print $(NF-2), $(NF-1), $NF }' "$O/$N.car" | sed 's/^/pos /' | head -20
awk '{ y = $(NF-1); if (min == "" || y < min) min = y } END { print "lowest y:", min, "lines:", NR }' "$O/$N.car"
grep -m3 "ERROR\|not found" "$O/$N.log"
/usr/bin/python3 - "$O" "$N" <<'EOF'
import os, sys
from PIL import Image
o, n = sys.argv[1], sys.argv[2]
d = os.path.join(o, "dump-" + n)
fs = sorted(os.listdir(d)) if os.path.isdir(d) else []
if fs:
    pick = [fs[min(len(fs) - 1, len(fs) * k // 4)] for k in (1, 2, 3, 4)]
    w = Image.new("RGB", (1280, 960))
    for k, f in enumerate(pick):
        w.paste(Image.open(os.path.join(d, f)).resize((640, 480)), ((k % 2) * 640, (k // 2) * 480))
    w.save(os.path.join(o, n + ".png"))
    print("frames:", " ".join(pick), "->", os.path.join(o, n + ".png"))
EOF
