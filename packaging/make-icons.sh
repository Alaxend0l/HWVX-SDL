#!/bin/sh
# Renders packaging/icon.svg to a macOS .icns (needs rsvg-convert from
# librsvg, and iconutil). Prints nothing and fails if the tools are missing.
#   packaging/make-icons.sh OUT.icns
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=$1
command -v rsvg-convert >/dev/null && command -v iconutil >/dev/null || exit 1
set_dir=$(mktemp -d)/hwvx.iconset
mkdir -p "$set_dir"
for size in 16 32 128 256 512; do
    rsvg-convert -w $size -h $size "$here/icon.svg" -o "$set_dir/icon_${size}x${size}.png"
    rsvg-convert -w $((size * 2)) -h $((size * 2)) "$here/icon.svg" -o "$set_dir/icon_${size}x${size}@2x.png"
done
iconutil -c icns "$set_dir" -o "$out"
