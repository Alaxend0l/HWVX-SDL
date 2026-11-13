#!/bin/sh
# Windows x86_64: the level editor (hwvx-browser.exe) in a .zip, with the game
# it drives (hwvx.exe: viewer mode, test drives) and hwvx-x.exe beside it, and
# the runtime files it reads (schemas/, level-samples/) in the same folder.
# Cross-compiled with MinGW-w64 from Linux or macOS, as packaging/windows/build.sh
# does the game: needs x86_64-w64-mingw32-gcc, cmake, ninja, zip, and cargo with
# the x86_64-pc-windows-gnu target (rustup target add x86_64-pc-windows-gnu).
# hwvx-x: see packaging/editor/common.sh (HWVX_X_CRATE, HWVX_X: a hwvx-x.exe).
#   packaging/editor/windows/build.sh   ->  dist/HWVX-Editor-VERSION-windows-x86_64.zip
set -eu
target=windows # shares SDL's build with the game's package
. "$(dirname "$0")/../../common.sh"
. "$(dirname "$0")/../common.sh"
toolchain="$root/cmake/mingw-w64-x86_64.cmake"
rt=x86_64-pc-windows-gnu
export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="${CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER:-x86_64-w64-mingw32-gcc}"

# the game: windowed (no console); the editor reads its log through the pipes it gives it
"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix" -DCMAKE_TOOLCHAIN_FILE="$toolchain"
cmake -S "$root" -B "$ed/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" -DCMAKE_FIND_ROOT_PATH="$work/sdl-prefix" \
    -DHWVX_VERSION="$game_version"
cmake --build "$ed/game" --target hwvx

CARGO_TARGET_DIR="$ed/cargo" cargo build --release --manifest-path "$root/tools/browser/Cargo.toml" --bin hwvx-browser --target $rt
if [ -n "${HWVX_X:-}" ]; then
    cp "$HWVX_X" "$ed/bin/hwvx-x.exe"
else
    CARGO_TARGET_DIR="$ed/cargo" cargo build --release --manifest-path "$xcrate/Cargo.toml" --bin hwvx-x --target $rt
    cp "$ed/cargo/$rt/release/hwvx-x.exe" "$ed/bin/hwvx-x.exe"
fi

name="HWVX-Editor-$ed_version-windows-x86_64"
stage="$ed/zip/$name"
rm -rf "$ed/zip"
mkdir -p "$stage"
x86_64-w64-mingw32-strip -o "$stage/hwvx-browser.exe" "$ed/cargo/$rt/release/hwvx-browser.exe"
x86_64-w64-mingw32-strip -o "$stage/hwvx-x.exe" "$ed/bin/hwvx-x.exe"
x86_64-w64-mingw32-strip -o "$stage/hwvx.exe" "$ed/game/hwvx.exe"
# the runtime files (tools/browser/src/resources.rs: beside the programs)
cp -R "$root/schemas" "$stage/schemas"
cp -R "$root/tools/level-samples" "$stage/level-samples"
out="$dist/$name.zip"
rm -f "$out"
(cd "$ed/zip" && zip -qr "$out" "$name")
echo "$out"
