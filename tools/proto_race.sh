#!/bin/bash
# tools/proto_race.sh BUILD DATA OUT NAME START [VAR=value ...]
# A short race with the PS2 prototype's content (HWVX_PROTO_DIR from
# sources.cfg's ps2beta= unless set): run_test.sh FAST=1 with A pressed to
# get past the briefing and held to drive, then a 3-frame sheet OUT/NAME.png.
#   tools/proto_race.sh build ../orig/files build/t r1 race:car=33 HWVX_PROTO=cars
B="$1"; D="$2"; O="$3"; N="$4"; S="$5"; shift 5
if [ -z "$HWVX_PROTO_DIR" ]; then
    HWVX_PROTO_DIR=$(grep '^ps2beta=' "$HOME/Library/Application Support/HWVX-SDL/HotWheelsVelocityX/sources.cfg" 2>/dev/null | cut -d= -f2-)
fi
FAST=1 "$(dirname "$0")/run_test.sh" "$B" "$D" "$O" "$N" ${SECONDS_MAX:-40} \
    "HWVX_AUTOINPUT=${INPUT:-400:a,500:a,600:a*2400}" "HWVX_START=$S" "HWVX_PROTO_DIR=$HWVX_PROTO_DIR" "$@"
grep -E "bad handle|Fatal|ERROR" "$O/$N.log" | head -3
/usr/bin/python3 - "$O/dump-$N" "$O/$N.png" <<'PY'
import os, sys
from PIL import Image
d, out = sys.argv[1], sys.argv[2]
fs = sorted(os.listdir(d))
if fs:
    fs = fs[:-1] or fs
    pick = [fs[len(fs) // 3], fs[2 * len(fs) // 3], fs[-1]]
    s = Image.new('RGB', (1920, 480))
    for i, f in enumerate(pick):
        s.paste(Image.open(os.path.join(d, f)).resize((640, 480)), (i * 640, 0))
    s.save(out)
PY
