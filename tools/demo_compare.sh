#!/bin/bash
# tools/demo_compare.sh REF_BUILD NEW_BUILD DATADIR OUTDIR [SECONDS]
#
# The merge regression check: the attract demo (no input, deterministic) on
# both builds, one after the other, then frame-by-frame byte comparison.
# Exits 0 only when every frame both runs dumped is identical.
R="$1"; N="$2"; D="$3"; O="$4"; T="${5:-40}"
here=$(cd "$(dirname "$0")"; pwd)
FAST=1 "$here/run_test.sh" "$R" "$D" "$O" ref "$T" >/dev/null
FAST=1 "$here/run_test.sh" "$N" "$D" "$O" new "$T" >/dev/null
same=0; diff=0; first=
for f in "$O/dump-ref"/*; do
    g="$O/dump-new/$(basename "$f")"
    [ -f "$g" ] || continue
    if cmp -s "$f" "$g"; then same=$((same + 1)); else diff=$((diff + 1)); first=${first:-$(basename "$f")}; fi
done
echo "ref $(ls "$O/dump-ref" | wc -l | tr -d ' ') frames, new $(ls "$O/dump-new" | wc -l | tr -d ' ') frames: $same identical, $diff different${first:+ (first: $first)}"
[ $diff = 0 ] && [ $same -gt 0 ]
