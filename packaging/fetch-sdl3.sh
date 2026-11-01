#!/bin/sh
# Downloads and unpacks the SDL3 source release the builds use (SDL_VERSION,
# checked against SDL_SHA256), printing the source directory.
#   packaging/fetch-sdl3.sh DIR
set -eu
here=$(cd "$(dirname "$0")" && pwd)
version=$(cat "$here/SDL_VERSION")
sha256=$(cat "$here/SDL_SHA256")
dir=$1
tarball="$dir/SDL3-$version.tar.gz"

mkdir -p "$dir"
if [ ! -d "$dir/SDL3-$version" ]; then
    if [ ! -f "$tarball" ]; then
        curl -fsSL -o "$tarball" \
            "https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL3-$version.tar.gz"
    fi
    if command -v sha256sum >/dev/null; then
        echo "$sha256  $tarball" | sha256sum -c - >&2
    else
        echo "$sha256  $tarball" | shasum -a 256 -c - >&2
    fi
    tar -xzf "$tarball" -C "$dir"
fi
echo "$dir/SDL3-$version"
