#!/bin/sh
# Linux, for the machine's architecture (x86_64 or aarch64): the bare
# program with SDL3 linked in (hwvx-VERSION.ARCH) and an AppImage. Build on
# an old distribution for wide compatibility (CI uses Ubuntu 22.04). Needs
# gcc, cmake, ninja, curl and SDL's build dependencies (see the apt line in
# .github/workflows/build.yml).
#   packaging/linux/build.sh   ->  dist/hwvx-VERSION.ARCH, dist/HWVX-VERSION-ARCH.AppImage
set -eu
. "$(dirname "$0")/../common.sh" "linux-$(uname -m)"
arch=$(uname -m)
id=io.github.alaxend0l.hwvx

"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix"
cmake -S "$root" -B "$work/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DHWVX_VERSION="$version"
cmake --build "$work/game"

bin="$dist/hwvx-$version.$arch"
strip -o "$bin" "$work/game/hwvx"
echo "$bin"

appdir="$work/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$work/game" --strip
deploy="$work/linuxdeploy-$arch.AppImage"
if [ ! -x "$deploy" ]; then
    curl -fsSL -o "$deploy" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
    chmod +x "$deploy"
fi
out="$dist/HWVX-$version-$arch.AppImage"
rm -f "$out"
# extract-and-run: works without FUSE (containers, CI)
APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION="$version" OUTPUT="$out" \
    "$deploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/hwvx" \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/$id.svg" \
    --output appimage
echo "$out"
