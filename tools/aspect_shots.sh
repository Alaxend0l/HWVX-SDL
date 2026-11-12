#!/bin/bash
# tools/aspect_shots.sh BUILD DATA OUT NAME SECONDS [VAR=value ...]
#
# The same run (tools/run_test.sh, FAST=1) at several window sizes, one after
# the other, for widescreen checks of the HUD and menus: OUT/dump-NAME-WxH per
# size, and OUT/NAME.png, the last frame every size dumped side by side at
# the same height (magenta between). SIZES="640x480 1280x720 2560x1080" by
# default. Extra VAR=value arguments go to the game (HWVX_START=..., input).
# Frames dumped at the same number are the same game moment (deterministic).
set -u
B="$1"; D="$2"; O="$3"; N="$4"; T="$5"; shift 5
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
dirs=()
for s in ${SIZES:-640x480 1280x720 2560x1080}; do
    WINDOW=$s FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "$N-$s" "$T" \
        HWVX_RENDERER=${HWVX_RENDERER:-opengl} SDL_AUDIO_DRIVER=${SDL_AUDIO_DRIVER:-dummy} "$@" >/dev/null
    dirs+=("$O/dump-$N-$s")
done
/usr/bin/python3 - "$O/$N.png" "${FRAME:-}" "${dirs[@]}" <<'EOF'
import sys, os
from PIL import Image
out, frame, dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
common = set.intersection(*(set(os.listdir(d)) for d in dirs))
if not common:
    sys.exit("no frame number every size dumped")
f = frame + ".bmp" if frame else max(common)
H = 480
ims = [Image.open(os.path.join(d, f)).convert("RGB") for d in dirs]
ims = [i.resize((round(i.width * H / i.height), H)) for i in ims]
s = Image.new("RGB", (sum(i.width for i in ims) + 8 * (len(ims) - 1), H), (255, 0, 255))
x = 0
for i in ims:
    s.paste(i, (x, 0)); x += i.width + 8
s.save(out)
print(out, f)
EOF
