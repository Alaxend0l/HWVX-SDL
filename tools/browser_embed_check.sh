#!/bin/bash
# tools/browser_embed_check.sh BUILD DATA OUT
#
# The data browser shows the game's picture inside its own window (the embedded frames):
# hwvx-browser --load HW_Cars:car:5 --app-shot OUT/embed.bmp, then the middle of its 3D
# view must hold the car (many colours), not the empty panel. Run it before a commit that
# touches the viewer. Only the browser this starts (and its game) is stopped, by PID.
B="$1"; D="$2"; O="$3"
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -f "$O/embed.bmp"
"$here/browser/target/release/hwvx-browser" --game "$B/hwvx" --data "$D" --pref "$O/pref" \
    --load HW_Cars:car:5 --app-shot "$O/embed.bmp" > "$O/embed.log" 2>&1 &
P=$!
for i in $(seq 120); do sleep 1; kill -0 $P 2>/dev/null || break; done
if kill -0 $P 2>/dev/null; then
    for c in $(pgrep -P $P); do kill -9 "$c"; done
    kill -9 $P
    echo "FAIL: no screenshot in 120 s (a covered window stops the browser's repaints on macOS)"; exit 1
fi
python3 - "$O/embed.bmp" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
mid = im.crop((int(w * 0.35), int(h * 0.3), int(w * 0.75), int(h * 0.8))).resize((40, 40))
n = len(set(mid.getdata()))
print(("ok  " if n > 100 else "FAIL") + f" the embedded picture: {n} colours in the middle of the 3D view ({sys.argv[1]})")
sys.exit(0 if n > 100 else 1)
PY
