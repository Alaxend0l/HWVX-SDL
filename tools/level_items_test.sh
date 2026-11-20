#!/bin/bash
# tools/level_items_test.sh BUILD DATA OUT
#
# Items and effect points of a level's own in quadrants that have none of their own: the test
# level (tools/level-tests/item-new-quad) puts three pickups across the road ahead of the start
# in the world quadrant (no items, no effect points) and an item and an effect point in another
# quadrant without either; picking one of the three up wins. Built as the editor's test drive
# builds it (`xdlrs level testdrive`), both ways: in memory (--memory: the level file applied as
# the world loads, as a shared level is; the port gives the quadrant an array of its own) and into
# its own world pack (the builder adds the quadrant's array). Each run: the level applied, and
# with the gas held the last frames are the green "objective complete" screen (`xdlrs frame mean`).
# XDLRS=path (default tools/xdlrs/target/release/xdlrs), HWVX_X=path (hwvx-x, else found as
# xdlrs finds it). Frames are deleted after each run (KEEP=1 keeps them).
set -u
B="$1"; D="$2"; O="$3"
here=$(cd "$(dirname "$0")"; pwd)
X="${XDLRS:-$here/xdlrs/target/release/xdlrs}"
P="$here/level-tests/item-new-quad"
mkdir -p "$O"; O=$(cd "$O"; pwd)
hx=(); [ -n "${HWVX_X:-}" ] && hx=(--hwvx-x "$HWVX_X")
fail=0
for way in memory pack; do
    flags=(); [ $way = memory ] && flags+=(--memory)
    rm -rf "$O/$way"
    r=$("$X" level testdrive "$P" "$O/$way" --data "$D" --cache "$O/base-cache" ${flags[@]+"${flags[@]}"} ${hx[@]+"${hx[@]}"})
    start=$(grep -o '"start": "[^"]*"' <<<"$r" | cut -d'"' -f4)
    [ -z "$start" ] && { echo "FAIL $way: not built: $r"; fail=1; continue; }
    echo "ok   $way: built"
    FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "items-$way" ${SECONDS_RUN:-45} HWVX_RENDERER=${HWVX_RENDERER:-opengl} \
        "HWVX_MODS_DIR=$O/$way/mods" "HWVX_START=$start" "HWVX_AUTOINPUT=308:a,377:a,446:a,515:a*3000" >/dev/null 2>&1
    log="$O/items-$way.log"
    if grep -q "level: .* applied to" "$log"; then echo "ok   $way: applied"
    else echo "FAIL $way: $(grep -o 'level: .*' "$log" | head -2 | tr '\n' ' ')"; fail=1; fi
    # the last frame may be cut short by the kill: the one before it
    f=$(ls "$O/dump-items-$way" | tail -2 | head -1)
    rgb=$([ -n "$f" ] && "$X" frame mean "$O/dump-items-$way/$f")
    read -r r g b <<<"${rgb:-0 0 0}"
    if [ $((100 * g)) -gt $((115 * r)) ] && [ $((10 * g)) -gt $((15 * b)) ]; then echo "ok   $way: won ($f is the win screen: $rgb)"
    else echo "FAIL $way: not won ($f: ${rgb:-no frame})"; fail=1; fi
    [ "${KEEP:-0}" = 1 ] || rm -rf "$O/dump-items-$way"
done
exit $fail
