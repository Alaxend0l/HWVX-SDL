#!/bin/bash
# tools/level_modes_test.sh BUILD DATA OUT [SAMPLE...]
#
# The sample level of each mode (tools/level-samples: gear-grab joyride, two-gates mission,
# flag-run battle, short-sprint drag) built into its own world pack as the test drive builds it
# (tools/xdl_level.py testdrive --data), then played twice by a script of inputs (the intro
# screens' A presses, then the gas held, or not): "win" must end the level won (joyride,
# mission: "progression: the level ends: won"; battle, drag: "rules: the round ends, player 1
# wins it"), "lose" must end it lost (another player, or "lost"). Each game runs alone and is
# force-killed by its own PID (tools/run_test.sh). OUT/<sample>-<run>.log, frames in OUT/dump-*.
set -u
B="$1"; D="$2"; O="$3"; shift 3
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
samples=("$@"); [ ${#samples[@]} = 0 ] && samples=(gear-grab two-gates flag-run short-sprint)
PRESS="308:a,377:a,446:a,515:a,600:a,700:a,800:a,900:a,1000:a,1100:a,1200:a"
fail=0
for s in "${samples[@]}"; do
    P="$here/level-samples/$s"
    mode=$(python3 -c "import sys; sys.path.insert(0, '$here'); import xdl_level as L; print(L.read_jsonc('$P/level.jsonc')['mode'])")
    rm -rf "$O/$s"
    r=$(python3 "$here/xdl_level.py" testdrive "$P" "$O/$s" --data "$D" --cache "$O/base-cache")
    start=$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('start',''))" "$r")
    [ -z "$start" ] && { echo "FAIL $s: not built: $r"; fail=1; continue; }
    for run in win lose; do
        if [ $run = win ]; then input="$PRESS*3000"; else input="$PRESS"; fi
        FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "$s-$run" ${SECONDS_RUN:-70} HWVX_RENDERER=${HWVX_RENDERER:-opengl} \
            "HWVX_MODS_DIR=$O/$s/mods" "HWVX_START=$start" "HWVX_AUTOINPUT=$input" >/dev/null 2>&1
        log="$O/$s-$run.log"
        case "$mode" in
            battle|drag) got=$(grep -o "rules: the round ends, player [0-9]* wins it" "$log" | head -1 | awk '{print $6}')
                         [ "$got" = 1 ] && res=win || { [ -n "$got" ] && res=lose || res=none; } ;;
            *) got=$(grep -o "progression: the level ends: [a-z]*" "$log" | head -1 | awk '{print $5}')
               [ "$got" = won ] && res=win || { [ "$got" = lost ] && res=lose || res=none; } ;;
        esac
        if [ "$res" = $run ]; then echo "ok   $s ($mode) $run"; else echo "FAIL $s ($mode) $run: $res (${got:-nothing})"; fail=1; fi
    done
done
exit $fail
