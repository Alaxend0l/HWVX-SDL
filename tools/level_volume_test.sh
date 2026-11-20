#!/bin/bash
# tools/level_volume_test.sh BUILD DATA OUT
#
# A level's own volumes of other types than airbox work as the base world's do: the game finds
# them through the world's volume grid (Port_Level.c adds them to it). The test level
# (tools/level-tests/fog-start) puts thick red fog around the player's start; the game finds a
# fog volume only through the grid. Built as the editor's test drive builds it (`xdlrs level
# testdrive`), both ways: in memory (--memory: the level file applied as the world loads, as a
# shared level is) and into its own world pack (the volume in the World's array, marked). Each
# run: the log says the volume went into the grid, and a frame in the race is red (its mean
# colour, `xdlrs frame mean`). Without the grid the frames are the world's own colours.
# XDLRS=path (default tools/xdlrs/target/release/xdlrs), HWVX_X=path (hwvx-x, else found as
# xdlrs finds it). Frames are deleted after each run (KEEP=1 keeps them).
set -u
B="$1"; D="$2"; O="$3"
here=$(cd "$(dirname "$0")"; pwd)
X="${XDLRS:-$here/xdlrs/target/release/xdlrs}"
P="$here/level-tests/fog-start"
mkdir -p "$O"; O=$(cd "$O"; pwd)
hx=(); [ -n "${HWVX_X:-}" ] && hx=(--hwvx-x "$HWVX_X")
fail=0
for way in memory pack; do
    flags=(--skip-intro); [ $way = memory ] && flags+=(--memory)
    rm -rf "$O/$way"
    r=$("$X" level testdrive "$P" "$O/$way" --data "$D" --cache "$O/base-cache" "${flags[@]}" ${hx[@]+"${hx[@]}"})
    start=$(grep -o '"start": "[^"]*"' <<<"$r" | cut -d'"' -f4)
    [ -z "$start" ] && { echo "FAIL $way: not built: $r"; fail=1; continue; }
    FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "fog-$way" ${SECONDS_RUN:-25} HWVX_RENDERER=${HWVX_RENDERER:-opengl} \
        "HWVX_MODS_DIR=$O/$way/mods" "HWVX_START=$start" "HWVX_AUTOINPUT=308:a,377:a,446:a,515:a" >/dev/null 2>&1
    log="$O/fog-$way.log"
    if grep -q "1 volumes added to the volume grid" "$log"; then echo "ok   $way: the fog volume added to the volume grid"
    else echo "FAIL $way: $(grep -o 'level: .*' "$log" | head -2 | tr '\n' ' ')"; fail=1; fi
    # the last frame may be cut short by the kill: the one before it
    f=$(ls "$O/dump-fog-$way" | tail -2 | head -1)
    rgb=$([ -n "$f" ] && "$X" frame mean "$O/dump-fog-$way/$f")
    read -r r g b <<<"${rgb:-0 0 0}"
    if [ "$r" -gt 100 ] && [ "$r" -gt $((4 * g)) ] && [ "$r" -gt $((4 * b)) ]; then echo "ok   $way: $f is red ($rgb)"
    else echo "FAIL $way: $f isn't red (${rgb:-no frame})"; fail=1; fi
    [ "${KEEP:-0}" = 1 ] || rm -rf "$O/dump-fog-$way"
done
exit $fail
