#!/bin/sh
# The licence texts of what the game is built from, for a package's licenses/
# folder (the launcher's Credits tab points there): HWVX-SDL's own, SDL3's,
# Mbed TLS's (with its bundled 3rdparty code's notes), the single-file
# libraries in src/third_party (their licence sections) and the Input Prompts
# glyphs' CC0 dedication. FFmpeg's are added by packaging/ffmpeg/build.sh.
#   packaging/licenses.sh OUT SDL_SRC MBEDTLS_SRC
# SDL_SRC and MBEDTLS_SRC are where the build's sources have their licence files
# (fetch-sdl3.sh's folder or an installed share/licenses/SDL3; the game build's
# _deps/mbedtls-src); "-" leaves one out.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
out=$1
sdl=$2
mbedtls=$3

mkdir -p "$out/HWVX-SDL"
cp "$root/LICENSE" "$out/HWVX-SDL/LICENSE"
if [ "$sdl" != - ]; then
    mkdir -p "$out/SDL3"
    cp "$sdl/LICENSE.txt" "$out/SDL3/LICENSE.txt"
fi
if [ "$mbedtls" != - ]; then
    mkdir -p "$out/MbedTLS"
    cp "$mbedtls/LICENSE" "$out/MbedTLS/LICENSE"
    [ ! -f "$mbedtls/NOTICE" ] || cp "$mbedtls/NOTICE" "$out/MbedTLS/NOTICE"
    for d in everest p256-m; do
        [ ! -f "$mbedtls/3rdparty/$d/README.md" ] || cp "$mbedtls/3rdparty/$d/README.md" "$out/MbedTLS/3rdparty-$d.md"
    done
fi

# section FILE FIRST_LINE OUT: from the line starting FIRST_LINE to the end of
# its comment
section() {
    mkdir -p "$(dirname "$3")"
    awk -v first="$2" 'index($0, first) == 1 { on = 1 } on && $0 == "*/" { exit } on' "$1" > "$3"
    [ -s "$3" ] || { echo "licenses.sh: no licence section in $1" >&2; exit 1; }
}
section "$root/src/third_party/miniaudio.h" "This software is available as a choice" "$out/miniaudio/LICENSE"
section "$root/src/third_party/stb_vorbis.c" "This software is available under 2 licenses" "$out/stb_vorbis/LICENSE"
section "$root/src/third_party/stb_image.h" "This software is available under 2 licenses" "$out/stb_image/LICENSE"

mkdir -p "$out/InputPrompts"
cat > "$out/InputPrompts/LICENSE" <<'EOF'
The button prompt glyphs are from "The Meritite Union's Input Prompts" 2.0.0
by hergergy, https://github.com/meritite-union/input-prompts, dedicated to the
public domain under CC0 1.0 Universal:
https://creativecommons.org/publicdomain/zero/1.0/legalcode
(HWVX-SDL's assets/prompts/README.md lists the files used.)
EOF
