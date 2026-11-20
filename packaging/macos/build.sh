#!/bin/sh
# macOS: a universal (Apple silicon + Intel) app with SDL3 linked in and
# FFmpeg's libraries (universal too) in Contents/Frameworks, on a .dmg. Needs
# Xcode's command line tools, cmake, ninja, make; for the icon, rsvg-convert
# (brew install librsvg). The app is ad-hoc signed only, so Gatekeeper asks
# before the first start (right-click > Open).
#   packaging/macos/build.sh   ->  dist/HWVX-VERSION-macos.dmg
set -eu
target=macos
. "$(dirname "$0")/../common.sh"
arch_args='-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0'

# shellcheck disable=SC2086
"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix" $arch_args
"$root/packaging/ffmpeg/build.sh" macos "$work" "$work/ffmpeg-prefix"
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
# FFmpeg: the libraries in Contents/Frameworks, where a bundle's code goes and
# where bink.c looks (the base path's ../Frameworks); their install names are
# @loader_path/..., so they find each other there.
rm -rf "$app/Contents/Frameworks" "$app/Contents/Resources/licenses"
mkdir -p "$app/Contents/Frameworks" "$app/Contents/Resources/licenses"
cp "$work/ffmpeg-prefix"/lib/*.dylib "$app/Contents/Frameworks/"
cp -R "$work/ffmpeg-prefix/licenses/FFmpeg" "$app/Contents/Resources/licenses/"
"$root/packaging/licenses.sh" "$app/Contents/Resources/licenses" "$work/SDL3-$(cat "$root/packaging/SDL_VERSION")" "$work/game/_deps/mbedtls-src"
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
