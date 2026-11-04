#!/bin/sh
# Builds SDL3 as a static library into PREFIX, for release builds that link
# it into the game (one self-contained program per platform). Extra
# arguments go to SDL's CMake (toolchain file, macOS architectures...).
#   packaging/build-sdl3.sh WORKDIR PREFIX [cmake args...]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=$1
prefix=$2
shift 2

src=$("$here/fetch-sdl3.sh" "$work")
if [ -f "$prefix/.sdl-built" ] && [ "$(cat "$prefix/.sdl-built")" = "$(cat "$here/SDL_VERSION") $*" ]; then
    echo "SDL3 already in $prefix" >&2
    exit 0
fi
cmake -S "$src" -B "$work/sdl-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    "$@"
cmake --build "$work/sdl-build"
cmake --install "$work/sdl-build"
echo "$(cat "$here/SDL_VERSION") $*" > "$prefix/.sdl-built"
