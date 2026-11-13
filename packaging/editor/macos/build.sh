#!/bin/sh
# macOS: the level editor (hwvx-browser) as its own universal (Apple silicon +
# Intel) app on a .dmg, with the game it drives (hwvx, viewer mode and test
# drives) and hwvx-x bundled beside it, and the runtime files it reads in
# Resources (see docs-pending/editor-release.md for the layout and lookups).
# Needs what packaging/macos/build.sh needs, plus cargo with the
# aarch64-apple-darwin and x86_64-apple-darwin targets. hwvx-x is built from
# the superproject (the crate hwvx-xfile above this checkout), or taken as is
# from $HWVX_X. Ad-hoc signed only (right-click > Open on the first start).
#   packaging/editor/macos/build.sh   ->  dist/HWVX-Editor-VERSION-macos.dmg
# The version: $HWVX_EDITOR_VERSION, else the last editor-v* tag (git describe),
# else git describe; the game inside keeps its own (v* tags).
set -eu
target=macos # shares SDL's build with the game's package
. "$(dirname "$0")/../../common.sh"
game_version=$version
if [ -n "${HWVX_EDITOR_VERSION:-}" ]; then
    ed_version=$HWVX_EDITOR_VERSION
else
    ed_version=$(git -C "$root" describe --tags --match 'editor-v*' 2>/dev/null ||
        git -C "$root" describe --tags --always --dirty 2>/dev/null || echo dev)
fi
ed_version=$(echo "$ed_version" | sed 's/^editor-v//; s/^v//')
ed="$work/editor"
mkdir -p "$ed"
export MACOSX_DEPLOYMENT_TARGET=11.0
arch_args='-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0'
rust_targets='aarch64-apple-darwin x86_64-apple-darwin'

# the game: the plain program (no bundle of its own), universal, SDL3 linked in
# shellcheck disable=SC2086
"$root/packaging/build-sdl3.sh" "$work" "$work/sdl-prefix" $arch_args
# shellcheck disable=SC2086
cmake -S "$root" -B "$ed/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$work/sdl-prefix" \
    -DHWVX_MACOS_BUNDLE=OFF -DHWVX_VERSION="$game_version" $arch_args
cmake --build "$ed/game" --target hwvx

# universal Rust programs: cargo_lipo MANIFEST BIN OUT
cargo_lipo() {
    parts=
    for t in $rust_targets; do
        CARGO_TARGET_DIR="$ed/cargo" cargo build --release --manifest-path "$1" --bin "$2" --target "$t"
        parts="$parts $ed/cargo/$t/release/$2"
    done
    # shellcheck disable=SC2086
    lipo -create $parts -output "$3"
}
mkdir -p "$ed/bin"
cargo_lipo "$root/tools/browser/Cargo.toml" hwvx-browser "$ed/bin/hwvx-browser"
if [ -n "${HWVX_X:-}" ]; then
    cp "$HWVX_X" "$ed/bin/hwvx-x"
elif grep -q '^name = "hwvx-xfile"' "$root/../Cargo.toml" 2>/dev/null; then
    cargo_lipo "$root/../Cargo.toml" hwvx-x "$ed/bin/hwvx-x"
else
    echo "no hwvx-x: set HWVX_X, or build inside the superproject" >&2
    exit 1
fi

app="$ed/HWVX Editor.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$ed/bin/hwvx-browser" "$ed/bin/hwvx-x" "$app/Contents/MacOS/"
cp "$ed/game/hwvx" "$app/Contents/MacOS/hwvx"
strip -x "$app/Contents/MacOS/hwvx-browser" "$app/Contents/MacOS/hwvx-x" "$app/Contents/MacOS/hwvx"
# the runtime files (src/resources.rs): the schemas, the sample level projects
cp -R "$root/schemas" "$app/Contents/Resources/schemas"
cp -R "$root/tools/level-samples" "$app/Contents/Resources/level-samples"
icon=
if TMPDIR="$ed" "$root/packaging/make-icons.sh" "$app/Contents/Resources/hwvx-editor.icns"; then
    icon=hwvx-editor
else
    echo "no rsvg-convert: building without an icon" >&2
fi
sed -e "s/@VERSION@/$ed_version/g" -e "s/@ICON@/$icon/g" "$(dirname "$0")/Info.plist.in" > "$app/Contents/Info.plist"
codesign --force --deep --sign - "$app"

stage="$ed/dmg"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"
out="$dist/HWVX-Editor-$ed_version-macos.dmg"
rm -f "$out"
hdiutil create -volname "HWVX Editor" -srcfolder "$stage" -ov -format UDZO "$out"
echo "$out"
