#!/bin/bash
# tools/level_modes_test.sh BUILD DATA OUT [SAMPLE...]
#
# The sample level of each mode (tools/level-samples: gear-grab joyride, two-gates mission,
# flag-run battle, short-sprint drag; and the levels on Joyride worlds: joyride-dash a challenge
# on world 4, which retail has none on, joyride-gates a mission on world 2, joyride-flag a battle
# on world 1, joyride-drag a drag race on world 1) built into its own world pack as the test
# drive builds it (xdlrs level testdrive --data), then played twice by a script of inputs (the
# intro screens' A presses, then the gas held, or not; scripted, the same every run): "win" must
# end the level won (joyride, mission, challenge: "progression: the level ends: won"; battle, drag:
# "rules: the round ends, player 1 wins it"), "lose" must end it lost (another player, or "lost").
# Each game runs alone and is force-killed by its own PID (tools/run_test.sh). A SAMPLE is a
# folder of tools/level-samples or a level project's path. MEMORY=1 builds the level file only
# (applied at run time, as a shared mod's level is) instead of its own world pack.
#
# A level whose level.jsonc has "smoke": {"route": "x,z;..."} also gets a "smoke" run: HWVX_AUTOPILOT
# drives it along that route. It is a smoke test only: the level loads, starts and runs without a
# crash, and what the autopilot reached is printed. It says nothing about whether the level can be
# won or how hard it is (the game's AI isn't good enough for that; the win run above, and a person's
# test drive, are the judges). A level with "test": {"win": ["log line", ...], "lose": [...],
# "memory": [...]} must log each of those lines in that run ("memory": the win run with MEMORY=1:
# the level file applied at run time, which logs what it removes). OUT/<sample>-<run>.log, frames
# in OUT/dump-* (deleted unless KEEP_FRAMES=1).
#
# SAMPLE "templates": every mode on every Joyride world (20 levels: challenge, mission, battle, drag
# on worlds 1-5), each as New level makes it (xdlrs level new: the gears and key removed, the
# clock, rules and round flow added), in OUT/templates, played once without the gas ("start"): a
# challenge or a mission must end lost (the template's clock runs out), a battle or a drag race
# must reach play and run to the end of the time without a crash.
set -u
B="$1"; D="$2"; O="$3"; shift 3
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
X="${XDLRS:-$here/xdlrs/target/release/xdlrs}"
[ -x "$X" ] || { echo "no $X: cargo build --release in tools/xdlrs"; exit 2; }
samples=("$@"); [ ${#samples[@]} = 0 ] && samples=(gear-grab two-gates flag-run short-sprint joyride-dash joyride-gates joyride-flag joyride-drag)
PRESS="308:a,377:a,446:a,515:a,600:a,700:a,800:a,900:a,1000:a,1100:a,1200:a"

# a key of level.jsonc as JSON (xdlrs reads JSONC: comments allowed)
key() { "$X" probe jsonc "$1" | sed -n 's/^OK //p' | jq -c "$2"; }

fail=0
# "templates": the 20 levels made the way New level makes them
expanded=()
for s in "${samples[@]}"; do
    if [ "$s" != templates ]; then expanded+=("$s"); continue; fi
    for m in challenge mission battle drag; do
        for w in 1 2 3 4 5; do
            P="$O/templates/$m-world$w"
            rm -rf "$P"
            if "$X" level new "$P" --mode $m --world "worlds_\\sp_\\joyride_\\world$w" --name "$m world $w" \
                --data "$D" --cache "$O/base-cache" --repo "$here/.." >/dev/null; then expanded+=("$P"); else echo "FAIL $m-world$w: not made"; fail=1; fi
        done
    done
done
samples=("${expanded[@]}")
for s in "${samples[@]}"; do
    if [ -d "$s" ]; then P=$(cd "$s"; pwd); else P="$here/level-samples/$s"; fi
    s=$(basename "$P")
    mode=$(key "$P/level.jsonc" '.mode' | tr -d '"')
    route=$(key "$P/level.jsonc" 'if .smoke == null then "-" else (.smoke.route // "") end' | tr -d '"')
    rm -rf "$O/$s"
    mem=(); [ -n "${MEMORY:-}" ] && mem=(--memory)
    r=$("$X" level testdrive "$P" "$O/$s" --data "$D" --cache "$O/base-cache" ${mem[@]+"${mem[@]}"} --repo "$here/..")
    start=$(jq -r '.start // ""' <<<"$r" 2>/dev/null)
    [ -z "$start" ] && { echo "FAIL $s: not built: $r"; fail=1; continue; }
    runs=(win lose); [ "$route" != - ] && runs+=(smoke)
    case "$P" in "$O/templates/"*) runs=(start);; esac
    for run in "${runs[@]}"; do
        # the mission's steps (a briefing, the item select) take A presses up to frame 1200 (the joyride
        # sample's gears lie on the road that input drives); the battle holds the gas after the intro's
        # presses; a drag shows "GO" at about frame 240, the gas from 250 (none in its countdown); a level
        # on a Joyride world opens with the Joyride intro (A presses, as a mission's)
        case "$mode" in mission|joyride) pre="$PRESS";; drag) pre="250:a";; *) pre="308:a,377:a,446:a,515:a";; esac
        case "$(key "$P/level.jsonc" '.world' | tr 'A-Z' 'a-z')" in *joyride*) [ "$mode" != joyride ] && pre="308:a,377:a";; esac
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
        [ -z "${KEEP_FRAMES:-}" ] && rm -rf "$O/dump-$s-$run"
        if [ $run = start ]; then
            # a template as New level makes it: a challenge or a mission lost when its clock runs out;
            # a battle or a drag race played to the end of the time
            case "$mode" in
                battle|drag)
                    if grep -q "level: " "$log" && grep -q "STATE_GAME_PLAY" "$log" && ! grep -q "exited early" <<<"$out"; then
                        echo "ok   $s ($mode) start: applied, played ${SECONDS_RUN:-70} s"
                    else echo "FAIL $s ($mode) start: $(tail -1 <<<"$out")"; fail=1; fi ;;
                *)
                    got=$(grep -o "progression: the level ends: [a-z]*" "$log" | head -1 | awk '{print $5}')
                    if [ "$got" = lost ]; then echo "ok   $s ($mode) start: lost when the clock ran out"
                    else echo "FAIL $s ($mode) start: ${got:-the level never ended}"; fail=1; fi ;;
            esac
            continue
        fi
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
        sel=".test.$run"; [ $run = win ] && [ -n "${MEMORY:-}" ] && sel="$sel, .test.memory"
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            if grep -qF -- "$line" "$log"; then echo "ok   $s ($mode) $run logs: $line"; else echo "FAIL $s ($mode) $run: no \"$line\" in $log"; fail=1; fi
        done < <(key "$P/level.jsonc" "[($sel) // [] | .[]] | .[]" | jq -r .)
    done
done
exit $fail
