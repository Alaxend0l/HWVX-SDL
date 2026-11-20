#!/bin/sh
# Linux, for the machine's architecture (x86_64 or aarch64): the program with
# SDL3 linked in and FFmpeg's libraries beside it, as a .tar.gz
# (hwvx-VERSION-linux-ARCH/), and an AppImage. Build on an old distribution
# for wide compatibility (CI uses Ubuntu 22.04). Needs gcc, cmake, ninja,
# make, curl and SDL's build dependencies (see the apt line in
# .github/workflows/build.yml).
#   packaging/linux/build.sh   ->  dist/hwvx-VERSION-linux-ARCH.tar.gz, dist/HWVX-VERSION-ARCH.AppImage
set -eu
target="linux-$(uname -m)"
. "$(dirname "$0")/../common.sh"
arch=$(uname -m)
id=io.github.alaxend0l.hwvx

"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix"
"$root/packaging/ffmpeg/build.sh" linux "$work" "$work/ffmpeg-prefix"
cmake -S "$root" -B "$work/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DHWVX_VERSION="$version"
cmake --build "$work/game"

# The program and FFmpeg's libraries in one folder (the game looks for them
# in its own folder first), with FFmpeg's licence and notice.
name="hwvx-$version-linux-$arch"
stage="$work/tar"
rm -rf "$stage"
mkdir -p "$stage/$name/licenses"
strip -o "$stage/$name/hwvx" "$work/game/hwvx"
cp "$work/ffmpeg-prefix"/lib/* "$stage/$name/"
cp -R "$work/ffmpeg-prefix/licenses/FFmpeg" "$stage/$name/licenses/"
out="$dist/$name.tar.gz"
tar -czf "$out" -C "$stage" "$name"
echo "$out"

appdir="$work/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$work/game" --strip
# FFmpeg in usr/lib, where bink.c looks (the base path usr/bin's ../lib).
mkdir -p "$appdir/usr/lib" "$appdir/usr/share/licenses"
cp "$work/ffmpeg-prefix"/lib/* "$appdir/usr/lib/"
cp -R "$work/ffmpeg-prefix/licenses/FFmpeg" "$appdir/usr/share/licenses/"
# appimagetool makes the AppImage's icon (.DirIcon) from a PNG named after
# the desktop file's Icon= at the top of the AppDir, and won't take an SVG:
# use the installed PNG, and leave the SVG out so linuxdeploy can't pick it.
png="$appdir/usr/share/icons/hicolor/256x256/apps/$id.png"
rm -f "$appdir/usr/share/icons/hicolor/scalable/apps/$id.svg"
cp "$png" "$appdir/$id.png"
deploy="$work/linuxdeploy-$arch.AppImage"
if [ ! -x "$deploy" ]; then
    curl -fsSL -o "$deploy" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
    chmod +x "$deploy"
fi
out="$dist/HWVX-$version-$arch.AppImage"
rm -f "$out"
# extract-and-run: works without FUSE (containers, CI). LD_LIBRARY_PATH: so it
# finds libavcodec's libavutil beside it (FFmpeg's libraries have no run path).
APPIMAGE_EXTRACT_AND_RUN=1 LD_LIBRARY_PATH="$appdir/usr/lib" LINUXDEPLOY_OUTPUT_VERSION="$version" OUTPUT="$out" \
    "$deploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/hwvx" \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$png" \
    --output appimage
echo "$out"
