#!/bin/bash
# tools/autopilot_drive.sh BUILD DATADIR OUTDIR MODE WORLD SECONDS [VAR=value ...]
# tools/autopilot_drive.sh compare OUTDIR NAME_A NAME_B
#
# A test drive that crosses the world: HWVX_START=race:mode=MODE,world=WORLD,
# the mission window and the levels' "press A" screens dismissed (A once the
# disc has been quiet 60 frames, six times, then every 120 quiet frames:
# HWVX_AUTOINPUT; AUTOINPUT=... replaces it; the autopilot owns the car's
# controls, so A only reaches the menus),
# then player 1's car driven by HWVX_AUTOPILOT=1 (src/game/Port_Autopilot.c:
# the game's AI driving, along the level's nav paths, or the quadrants'
# centres where there are none (Joyride), or HWVX_AUTOPILOT_ROUTE's points;
# "autopilot: FAILED" in the log ends the run). Deterministic
# (run_test.sh's HWVX_DETERMINISTIC, fixed 60 Hz steps), so two runs with
# one thing changed can be compared. Writes, with NAME = $NAME or MODE-WORLD:
#   OUTDIR/NAME.car     the car log (HWVX_CARLOG=1, every 30 frames)
#   OUTDIR/NAME.quads   camera 1's quadrant whenever it changes (HWVX_QUADLOG=1)
#   OUTDIR/NAME-sheet.png  a contact sheet of the frames dumped (every 60th)
# The run lasts SECONDS of real time, so runs end at different frames:
# `compare` checks the lines both runs have (the shorter run's log must be
# the start of the longer's), and says how many that is.
here=$(cd "$(dirname "$0")"; pwd)
if [ "$1" = compare ]; then
    O="$2"; A="$3"; B="$4"; rc=0
    for ext in car quads; do
        a="$O/$A.$ext"; b="$O/$B.$ext"
        n=$(( $(wc -l < "$a") < $(wc -l < "$b") ? $(wc -l < "$a") : $(wc -l < "$b") ))
        d=$(diff <(head -$n "$a") <(head -$n "$b") | head -3)
        if [ -z "$d" ]; then echo "$ext: $n lines identical"; else echo "$ext: DIFFERENT"; echo "$d"; rc=1; fi
    done
    exit $rc
fi
B="$1"; D="$2"; O="$3"; M="$4"; W="$5"; T="$6"; shift 6
N=${NAME:-$M-$W}
FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "$N" "$T" "HWVX_START=race:mode=$M,world=$W" \
    "HWVX_AUTOPILOT=${AUTOPILOT:-1}" HWVX_CARLOG=1 HWVX_QUADLOG=1 \
    "HWVX_AUTOINPUT=${AUTOINPUT:-+60:a,+60:a,+60:a,+60:a,+60:a,+60:a,$(printf '+120:a,%.0s' $(seq 60))}" "$@" >/dev/null
grep -o "carlog: .*" "$O/$N.log" > "$O/$N.car"
grep -o "quadrant: .*" "$O/$N.log" > "$O/$N.quads"
/usr/bin/python3 - "$O/dump-$N" "$O/$N-sheet.png" <<'EOF'
import os, sys
from PIL import Image
d, out = sys.argv[1], sys.argv[2]
files = sorted(f for f in os.listdir(d) if f.endswith('.bmp'))
if not files:
    sys.exit(0)
step = max(1, len(files) // 16)
pick = files[::step][:16]
tw, th = 320, 240
sheet = Image.new('RGB', (tw * 4, th * ((len(pick) + 3) // 4)))
for i, f in enumerate(pick):
    try:
        im = Image.open(os.path.join(d, f)).convert('RGB').resize((tw, th))
    except Exception:
        continue
    sheet.paste(im, ((i % 4) * tw, (i // 4) * th))
sheet.save(out)
EOF
echo "$N: $(wc -l < "$O/$N.car" | tr -d ' ') car log lines, $(wc -l < "$O/$N.quads" | tr -d ' ') quadrant changes ($(cut -d' ' -f6 "$O/$N.quads" | sort -un | wc -l | tr -d ' ') quadrants), $(grep -c 'autopilot: plan' "$O/$N.log") plans"
grep -o "autopilot: \(objective\|round\|re-plan\|skipped\|PASSED\|FAILED\).*" "$O/$N.log"
true
