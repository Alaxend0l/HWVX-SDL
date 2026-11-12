#!/bin/bash
# tools/proto_worlds_check.sh BUILD DATA OUT [SECONDS] [WORLD:MODE ...]
# Each PS2-prototype world (or the ones given) started as a registered track
# under lldb (tools/crash_bt.sh): one line per world, "ok" or where it
# crashed. Default: one level of every kind.
B="$1"; D="$2"; O="$3"; T="${4:-25}"; shift 4 2>/dev/null || shift $#
[ $# -eq 0 ] && set -- 'worlds_\sp_\joyride_\world1:joyride' 'worlds_\sp_\joyride_\world2:joyride' \
    'worlds_\sp_\joyride_\world3:joyride' 'worlds_\sp_\joyride_\world4:joyride' 'worlds_\sp_\joyride_\world5:joyride' \
    'worlds_\sp_\chal1_\ch1_l2:challenge' 'worlds_\sp_\miss_\world1l1:mission' 'worlds_\sp_\drag_\race1l2:drag' \
    'worlds_\mp_\battle_\bat1_l1:battle'
for wm in "$@"; do
    W="${wm%%:*}"; M="${wm##*:}"
    case "$M" in joyride) A=0 L=5;; challenge) A=39 L=18;; mission|adventure) A=5 L=14;; drag) A=29 L=6;; battle) A=19 L=8;; esac
    "$(dirname "$0")/crash_bt.sh" "$B" "$D" "$O" "$T" HWVX_PROTO=added "HWVX_TEST_EXTRA_TRACKS=alias=$A" \
        "HWVX_TEST_EXTRA_TRACK_WORLD=~$W" "HWVX_START=race:mode=$M,world=$L" \
        "HWVX_AUTOINPUT=400:a,500:a,600:a*2400" HWVX_FIXEDSTEP=1 > /dev/null
    f=$(grep -E 'frame #[4-9]: .*hwvx`' "$O/bt.log" | grep -v 'Port_Fatal\|HandleToPtr' | head -1 | sed 's/.*hwvx`//')
    printf '%-36s %-10s %s\n' "$W" "$M" "${f:-ok}"
done
