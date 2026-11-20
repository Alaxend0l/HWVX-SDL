#!/bin/bash
# tools/level_movebox_test.sh BUILD DATA OUT
#
# A base box (BSP) volume moved: the test level (tools/level-tests/move-box) patches the center
# of Challenge 1's airbox volume 2 (a BSP box far off the road) to put it across the road ahead
# of the start; driving into it wins. A BSP volume's center is never read by the game: the port
# (in memory) and the pack builder (its own world pack) move the shape's planes by the change.
# Built as the editor's test drive builds it (`xdlrs level testdrive`), both ways: in memory
# (--memory: the level file applied as the world loads, as a shared level is) and into its own
# world pack. Each run: the level applied, and with the gas held the last frames are the green
# "objective complete" screen (`xdlrs frame mean`); the box left where it was, it isn't.
# XDLRS=path (default tools/xdlrs/target/release/xdlrs), HWVX_X=path (hwvx-x, else found as
# xdlrs finds it). Frames are deleted after each run (KEEP=1 keeps them).
set -u
B="$1"; D="$2"; O="$3"
here=$(cd "$(dirname "$0")"; pwd)
X="${XDLRS:-$here/xdlrs/target/release/xdlrs}"
P="$here/level-tests/move-box"
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
    FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "box-$way" ${SECONDS_RUN:-45} HWVX_RENDERER=${HWVX_RENDERER:-opengl} \
        "HWVX_MODS_DIR=$O/$way/mods" "HWVX_START=$start" "HWVX_AUTOINPUT=308:a,377:a,446:a,515:a*3000" >/dev/null 2>&1
    log="$O/box-$way.log"
    if grep -q "level: .* applied to" "$log"; then echo "ok   $way: applied"
    else echo "FAIL $way: $(grep -o 'level: .*' "$log" | head -2 | tr '\n' ' ')"; fail=1; fi
    # the last frame may be cut short by the kill: the one before it
    f=$(ls "$O/dump-box-$way" | tail -2 | head -1)
    rgb=$([ -n "$f" ] && "$X" frame mean "$O/dump-box-$way/$f")
    read -r r g b <<<"${rgb:-0 0 0}"
    if [ $((100 * g)) -gt $((115 * r)) ] && [ $((10 * g)) -gt $((15 * b)) ]; then echo "ok   $way: won ($f is the win screen: $rgb)"
    else echo "FAIL $way: not won ($f: ${rgb:-no frame})"; fail=1; fi
    [ "${KEEP:-0}" = 1 ] || rm -rf "$O/dump-box-$way"
done
exit $fail
