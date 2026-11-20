#!/bin/sh
# Windows x86_64: hwvx.exe with SDL3 linked in and FFmpeg's DLLs beside it,
# in a .zip (one folder), cross-compiled with MinGW-w64 GCC from Linux or
# macOS (needs x86_64-w64-mingw32-gcc, a host cc, cmake, ninja, make, zip).
#   packaging/windows/build.sh   ->  dist/hwvx-VERSION-windows-x86_64.zip
set -eu
target=windows
. "$(dirname "$0")/../common.sh"
toolchain="$root/cmake/mingw-w64-x86_64.cmake"

"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix" -DCMAKE_TOOLCHAIN_FILE="$toolchain"
"$root/packaging/ffmpeg/build.sh" windows "$work" "$work/ffmpeg-prefix"
cmake -S "$root" -B "$work/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" -DCMAKE_FIND_ROOT_PATH="$work/sdl-prefix" \
    -DHWVX_VERSION="$version"
cmake --build "$work/game"

# The game looks for FFmpeg's DLLs in its own folder first.
name="hwvx-$version-windows-x86_64"
stage="$work/zip"
rm -rf "$stage"
mkdir -p "$stage/$name/licenses"
x86_64-w64-mingw32-strip -o "$stage/$name/hwvx.exe" "$work/game/hwvx.exe"
cp "$work/ffmpeg-prefix"/lib/*.dll "$stage/$name/"
cp -R "$work/ffmpeg-prefix/licenses/FFmpeg" "$stage/$name/licenses/"
out="$dist/$name.zip"
rm -f "$out"
(cd "$stage" && zip -qr9 "$out" "$name")
echo "$out"
