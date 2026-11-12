#!/bin/bash
# tools/level_modes_test.sh BUILD DATA OUT [SAMPLE...]
#
# The sample level of each mode (tools/level-samples: gear-grab joyride, two-gates mission,
# flag-run battle, short-sprint drag) built into its own world pack as the test drive builds it
# (tools/xdl_level.py testdrive --data), then played twice by a script of inputs (the intro
# screens' A presses, then the gas held, or not; scripted, the same every run): "win" must end the level won (joyride,
# mission: "progression: the level ends: won"; battle, drag: "rules: the round ends, player 1
# wins it"), "lose" must end it lost (another player, or "lost"). Each game runs alone and is
# force-killed by its own PID (tools/run_test.sh).
#
# A sample whose level.jsonc has "smoke": {"route": "x,z;..."} also gets a "smoke" run: HWVX_AUTOPILOT
# drives it along that route. It is a smoke test only: the level loads, starts and runs without a
# crash, and what the autopilot reached is printed. It says nothing about whether the level can be
# won or how hard it is (the game's AI isn't good enough for that; the win run above, and a person's
# test drive, are the judges). OUT/<sample>-<run>.log, frames in OUT/dump-*.
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
    route=$(python3 -c "import sys; sys.path.insert(0, '$here'); import xdl_level as L; s = L.read_jsonc('$P/level.jsonc').get('smoke'); print('-' if s is None else s.get('route', ''))")
    rm -rf "$O/$s"
    r=$(python3 "$here/xdl_level.py" testdrive "$P" "$O/$s" --data "$D" --cache "$O/base-cache")
    start=$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('start',''))" "$r")
    [ -z "$start" ] && { echo "FAIL $s: not built: $r"; fail=1; continue; }
    runs=(win lose); [ "$route" != - ] && runs+=(smoke)
    for run in "${runs[@]}"; do
        # the mission's steps (a briefing, the item select) take A presses up to frame 1200 (the joyride
        # sample's gears lie on the road that input drives); the battle holds the gas after the intro's
        # presses; a drag shows "GO" at about frame 240, the gas from 250 (none in its countdown)
        case "$mode" in mission|joyride) pre="$PRESS";; drag) pre="250:a";; *) pre="308:a,377:a,446:a,515:a";; esac
        if [ $run = win ]; then input="$pre*3000"; else input="$pre"; fi
        extra=()
        if [ $run = smoke ]; then
            # the autopilot drives (src/game/Port_Autopilot.c); A only for the screens before the start
            input="+60:a,+60:a,+60:a,+60:a,+60:a,+60:a,$(printf '+120:a,%.0s' $(seq 30))"
            extra=(HWVX_AUTOPILOT=1); [ -n "$route" ] && extra+=("HWVX_AUTOPILOT_ROUTE=$route")
        fi
        out=$(FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "$s-$run" ${SECONDS_RUN:-70} HWVX_RENDERER=${HWVX_RENDERER:-opengl} \
            "HWVX_MODS_DIR=$O/$s/mods" "HWVX_START=$start" "HWVX_AUTOINPUT=$input" ${extra[@]+"${extra[@]}"} 2>&1)
        log="$O/$s-$run.log"
        if [ $run = smoke ]; then
            # smoke: the level applied and the game ran to the end of the time (or to the level's end)
            # without exiting on its own; the autopilot's last word is information, not a verdict
            last=$(grep -o "autopilot: .*" "$log" | tail -1)
            # (the autopilot quits the game itself once it has its result: that is not a crash)
            if grep -q "level: " "$log" && { ! grep -q "exited early" <<<"$out" || grep -q "autopilot: PASSED\|autopilot: FAILED" "$log"; }; then
                echo "ok   $s ($mode) smoke: loaded, started, ran without a crash (not a beatability check; the autopilot last logged: ${last#autopilot: })"
            else echo "FAIL $s ($mode) smoke: $(tail -1 <<<"$out")"; fail=1; fi
            continue
        fi
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
