#!/bin/bash
# tools/world_drive.sh BUILD DATADIR OUTDIR NAME WORLD_JSONC [SECONDS] [VAR=value ...]
#
# Drive a custom world (game/Port_World.c; built by the superproject's
# `hwvx-x world demo|build`): HWVX_WORLD=WORLD_JSONC registers it as a track,
# HWVX_START=race:mode=joyride,world=<its id> starts it, tools/drive_test.sh
# holds the gas (DRIVE=... replaces the input; default: straight ahead).
# Prints the car's positions every 10th log line (30 frames each), the
# highest and lowest heights reached before it first left the world (y below
# FLOOR, default -50: off an edge), the log's world lines and any "not found",
# and writes OUTDIR/NAME.png (four frames, if ImageMagick is there); the
# frame dumps are deleted (KEEP=1 keeps them).
B="$1"; D="$2"; O="$3"; N="$4"; W="$5"; T="${6:-30}"; shift 5; shift 2>/dev/null
here=$(cd "$(dirname "$0")"; pwd)
W=$(cd "$(dirname "$W")"; pwd)/$(basename "$W")
id=$(grep -o '"id": *"[^"]*"' "$W" | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
DRIVE="${DRIVE:-308:a,377:a,446:a,515:a*1500}" "$here/drive_test.sh" "$B" "$D" "$O" "$N" "$T" \
    "HWVX_WORLD=$W" "HWVX_START=race:mode=joyride,world=$id" "$@" > "$O/$N.car"
awk 'NR % 10 == 1 { print "pos", $(NF-2), $(NF-1), $NF }' "$O/$N.car" | head -30
awk -v floor="${FLOOR:--50}" '{ y = $(NF-1) + 0; if (y < floor) exit; if (hi == "" || y > hi) hi = y; if (lo == "" || y < lo) lo = y; n++ }
    END { print "on the world:", n, "lines; highest y:", hi, "lowest y:", lo }' "$O/$N.car"
grep "world:" "$O/$N.log" | head -5
grep -m3 "not found\|ERROR" "$O/$N.log"
d="$O/dump-$N"
if [ -d "$d" ] && command -v magick >/dev/null; then
    fs=($(ls "$d" | sort))
    n=${#fs[@]}
    if [ "$n" -gt 0 ]; then
        pick=()
        for k in 1 2 3 4; do pick+=("$d/${fs[$(( (n - 1) * k / 5 ))]}"); done
        magick "${pick[@]}" -resize 640x480 miff:- | magick montage - -tile 2x2 -geometry +0+0 "$O/$N.png" && echo "frames -> $O/$N.png"
    fi
fi
[ -n "$KEEP" ] || rm -rf "$d"
