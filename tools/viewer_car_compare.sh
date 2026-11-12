#!/bin/bash
# tools/viewer_car_compare.sh BUILD DATA OUT CAR...
#
# For each car: the car select's frame (tools/run_test.sh, the car unlocked)
# and the viewer's side view of the same car, side by side in OUT/cars.png
# (viewer left, game right). For checking the viewer draws cars as the game.
B="$1"; D="$2"; O="$3"; shift 3
H=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
for C in "$@"; do
  FAST=1 "$H/run_test.sh" "$B" "$D" "$O" cs$C 10 HWVX_START=carselect:mode=joyride,car=$C HWVX_TEST_UNLOCK_CARS=$C >/dev/null
  /usr/bin/python3 "$H/viewer_client.py" --build "$B" --data "$D" --out "$O/viewer" --window 800x600 \
    "load archive=HW_Cars type=car id=$C" 'camera yaw=-90 pitch=8 distance=28 target=[0,0,0]' \
    "screenshot path=$O/v$C.bmp" > "$O/v$C.json" 2>&1
done
/usr/bin/python3 - "$O" "$@" <<'PY'
import sys, glob
from PIL import Image
o, cs = sys.argv[1], sys.argv[2:]
W = Image.new('RGB', (1000, 300 * len(cs)))
for i, c in enumerate(cs):
    g = sorted(glob.glob(f'{o}/dump-cs{c}/*.bmp'))[-2]     # the last may be cut off by the kill
    a = Image.open(g).resize((500, 375)).crop((0, 125, 500, 425))
    b = Image.open(f'{o}/v{c}.bmp').resize((500, 375)).crop((0, 40, 500, 340))
    W.paste(b, (0, 300 * i)); W.paste(a, (500, 300 * i))
W.save(f'{o}/cars.png')
print(f'{o}/cars.png')
PY
