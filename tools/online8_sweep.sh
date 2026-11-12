#!/bin/bash
# tools/online8_sweep.sh BUILD DATADIR OUTDIR [SECONDS]
# tools/online8_test.sh on every world of Battle (8), Drag Race (6) and Joyride (5), 8
# players each (Drag Race 4), one after the other; a line per world, then how many failed.
B="$1"; D="$2"; O="$3"; T="${4:-60}"
fails=0
for spec in battle:0-7 drag:0-5 joyride:0-4; do
    mode=${spec%%:*}; range=${spec#*:}
    for w in $(seq ${range%-*} ${range#*-}); do
        n=8; [ "$mode" = drag ] && n=4    # Drag Race: 4 at most
        r=$(CAR="${CAR:-}" "$(dirname "$0")/online8_test.sh" "$B" "$D" "$O" "$mode" "$T" $n "$w" 2>&1 | grep -E '^(PASS|FAIL|note)' | tr '\n' ' ')
        echo "$mode $w: $r"
        case "$r" in *FAIL*|"") fails=$((fails + 1));; esac
    done
done
echo "$fails failed"
exit $((fails != 0))
