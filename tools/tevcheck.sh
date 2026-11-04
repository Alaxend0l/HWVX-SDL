#!/bin/sh
# Build and run tools/tevcheck.c (see there): the specialised pixel shaders
# against gx.frag. Arguments go to tevcheck, e.g.
#   tools/tevcheck.sh tools/tevkeys.txt --random 2000
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/hwvx-tevcheck
cc -std=gnu11 -O1 -g -Wall -Wno-unused-function -I"$root/src/port" -I"$root/src/game/include" \
    "$root/tools/tevcheck.c" "$root/src/port/gx_tevgen.c" "$root/src/port/gl_funcs.c" \
    $(pkg-config --cflags --libs sdl3) -lm -o "$out"
exec "$out" "$@"
