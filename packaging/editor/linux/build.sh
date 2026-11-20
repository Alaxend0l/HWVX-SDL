#!/bin/sh
# Linux, for the machine's architecture (x86_64 or aarch64): the level editor
# as an AppImage, with the game it drives (hwvx: viewer mode, test drives) and
# hwvx-x beside it in usr/bin, and the runtime files it reads in
# usr/share/hwvx-editor (schemas/, level-samples/; tools/browser/src/resources.rs
# finds them as ../share/hwvx-editor). Build on an old distribution (CI: Ubuntu
# 22.04). Needs what packaging/linux/build.sh needs, plus cargo (the editor's
# window: winit and glow load X11 or Wayland and GL at run time; the desktop
# has them). hwvx-x: see packaging/editor/common.sh.
#   packaging/editor/linux/build.sh   ->  dist/HWVX-Editor-VERSION-ARCH.AppImage
set -eu
target="linux-$(uname -m)" # shares SDL's build with the game's package
. "$(dirname "$0")/../../common.sh"
. "$(dirname "$0")/../common.sh"
arch=$(uname -m)

"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix"
"$root/packaging/ffmpeg/build.sh" linux "$work" "$work/ffmpeg-prefix"
cmake -S "$root" -B "$ed/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" \
    -DHWVX_VERSION="$game_version"
cmake --build "$ed/game" --target hwvx

CARGO_TARGET_DIR="$ed/cargo" cargo build --release --manifest-path "$root/tools/browser/Cargo.toml" --bin hwvx-browser
if [ -n "${HWVX_X:-}" ]; then
    cp "$HWVX_X" "$ed/bin/hwvx-x"
else
    CARGO_TARGET_DIR="$ed/cargo" cargo build --release --manifest-path "$xcrate/Cargo.toml" --bin hwvx-x
    cp "$ed/cargo/release/hwvx-x" "$ed/bin/hwvx-x"
fi

appdir="$ed/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/hwvx-editor" "$appdir/usr/share/applications" \
    "$appdir/usr/share/icons/hicolor/256x256/apps"
strip -o "$appdir/usr/bin/hwvx-browser" "$ed/cargo/release/hwvx-browser"
strip -o "$appdir/usr/bin/hwvx-x" "$ed/bin/hwvx-x"
strip -o "$appdir/usr/bin/hwvx" "$ed/game/hwvx"
# FFmpeg for the movies (the game's and the editor's viewer) in usr/lib, where
# the game looks (its base path usr/bin's ../lib)
mkdir -p "$appdir/usr/lib" "$appdir/usr/share/licenses"
cp "$work/ffmpeg-prefix"/lib/* "$appdir/usr/lib/"
cp -R "$work/ffmpeg-prefix/licenses/FFmpeg" "$appdir/usr/share/licenses/"
"$root/packaging/licenses.sh" "$appdir/usr/share/licenses" "$work/SDL3-$(cat "$root/packaging/SDL_VERSION")" "$ed/game/_deps/mbedtls-src"
cp -R "$root/schemas" "$appdir/usr/share/hwvx-editor/schemas"
cp -R "$root/tools/level-samples" "$appdir/usr/share/hwvx-editor/level-samples"
cp "$(dirname "$0")/hwvx-editor.desktop" "$appdir/usr/share/applications/hwvx-editor.desktop"
png="$appdir/usr/share/icons/hicolor/256x256/apps/hwvx-editor.png"
cp "$root/packaging/icon-256.png" "$png"
cp "$png" "$appdir/hwvx-editor.png"
deploy="$work/linuxdeploy-$arch.AppImage"
if [ ! -x "$deploy" ]; then
    curl -fsSL -o "$deploy" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
    chmod +x "$deploy"
fi
out="$dist/HWVX-Editor-$ed_version-$arch.AppImage"
rm -f "$out"
# extract-and-run: works without FUSE (containers, CI); the AppImage runs hwvx-browser,
# which finds hwvx and hwvx-x beside itself in usr/bin. LD_LIBRARY_PATH: so it
# finds libavcodec's libavutil beside it (FFmpeg's libraries have no run path).
APPIMAGE_EXTRACT_AND_RUN=1 LD_LIBRARY_PATH="$appdir/usr/lib" LINUXDEPLOY_OUTPUT_VERSION="$ed_version" OUTPUT="$out" \
    "$deploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/hwvx-browser" \
    --executable "$appdir/usr/bin/hwvx" \
    --executable "$appdir/usr/bin/hwvx-x" \
    --desktop-file "$appdir/usr/share/applications/hwvx-editor.desktop" \
    --icon-file "$png" \
    --output appimage
echo "$out"
