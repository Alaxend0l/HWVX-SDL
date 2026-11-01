#!/bin/sh
# Windows x86_64: one .exe with SDL3 linked in, cross-compiled with
# MinGW-w64 GCC from Linux or macOS (needs x86_64-w64-mingw32-gcc, cmake,
# ninja).
#   packaging/windows/build.sh   ->  dist/hwvx-VERSION-windows-x86_64.exe
set -eu
. "$(dirname "$0")/../common.sh" windows
toolchain="$root/cmake/mingw-w64-x86_64.cmake"

"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix" -DCMAKE_TOOLCHAIN_FILE="$toolchain"
cmake -S "$root" -B "$work/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" -DCMAKE_FIND_ROOT_PATH="$work/sdl-prefix" \
    -DHWVX_VERSION="$version"
cmake --build "$work/game"
out="$dist/hwvx-$version-windows-x86_64.exe"
x86_64-w64-mingw32-strip -o "$out" "$work/game/hwvx.exe"
echo "$out"
