#!/bin/bash
# tools/proto_world.sh BUILD DATA OUT NAME WORLD [MODE] [VAR=value ...]
# A race on one of the PS2 prototype's worlds, registered as an extra track
# of its mode (HWVX_TEST_EXTRA_TRACKS / _WORLD): WORLD is the prototype's
# archive without extension (worlds_\sp_\joyride_\world1), MODE the
# progression (joyride, challenge, mission, drag, battle; default joyride),
# aliasing that mode's first track. tools/proto_race.sh does the run.
B="$1"; D="$2"; O="$3"; N="$4"; W="$5"; M="${6:-joyride}"; shift 6 2>/dev/null || shift $#
case "$M" in joyride) A=0 L=5;; challenge) A=39 L=18;; mission|adventure) A=5 L=14;; drag) A=29 L=6;; battle) A=19 L=8;; esac
exec "$(dirname "$0")/proto_race.sh" "$B" "$D" "$O" "$N" "race:mode=$M,world=${LEVEL:-$L}" HWVX_PROTO=${PROTO:-added} \
    "HWVX_TEST_EXTRA_TRACKS=alias=$A" "HWVX_TEST_EXTRA_TRACK_WORLD=~$W" "$@"
