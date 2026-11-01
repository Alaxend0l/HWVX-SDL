#!/bin/sh
# macOS: a universal (Apple silicon + Intel) app with SDL3 linked in, on a
# .dmg. Needs Xcode's command line tools, cmake, ninja; for the icon,
# rsvg-convert (brew install librsvg). The app is ad-hoc signed only, so
# Gatekeeper asks before the first start (right-click > Open).
#   packaging/macos/build.sh   ->  dist/HWVX-VERSION-macos.dmg
set -eu
. "$(dirname "$0")/../common.sh" macos
arch_args='-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0'

# shellcheck disable=SC2086
"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix" $arch_args
mkdir -p "$work/game"
"$root/packaging/make-icons.sh" "$work/game/hwvx.icns" || echo "no rsvg-convert: building without an icon" >&2
# shellcheck disable=SC2086
cmake -S "$root" -B "$work/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" \
    -DHWVX_MACOS_BUNDLE=ON -DHWVX_VERSION="$version" $arch_args
cmake --build "$work/game"

app="$work/game/Hot Wheels Velocity X.app"
strip -x "$app/Contents/MacOS/Hot Wheels Velocity X"
codesign --force --deep --sign - "$app"

stage="$work/dmg"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"
out="$dist/HWVX-$version-macos.dmg"
rm -f "$out"
hdiutil create -volname "Hot Wheels Velocity X" -srcfolder "$stage" -ov -format UDZO "$out"
echo "$out"
