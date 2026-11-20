#!/bin/sh
# FFmpeg's source for a release, to go beside the packages (the LGPL's "the
# source from the same place as the binaries"): the tarball the libraries were
# built from (FFMPEG_VERSION, checked against FFMPEG_SHA256) and its signature
# into DIR, and the paragraph for the release notes into NOTES.
#   packaging/ffmpeg/source.sh DIR NOTES
set -eu
here=$(cd "$(dirname "$0")" && pwd)
version=$(cat "$here/FFMPEG_VERSION")
sha256=$(cat "$here/FFMPEG_SHA256")
url="https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
dir=$1
notes=$2

mkdir -p "$dir"
curl -fsSL -o "$dir/ffmpeg-$version.tar.xz" "$url"
curl -fsSL -o "$dir/ffmpeg-$version.tar.xz.asc" "$url.asc"
if command -v sha256sum >/dev/null; then
    echo "$sha256  $dir/ffmpeg-$version.tar.xz" | sha256sum -c - >&2
else
    echo "$sha256  $dir/ffmpeg-$version.tar.xz" | shasum -a 256 -c - >&2
fi

cat > "$notes" <<EOF
### FFmpeg

This software uses code of [FFmpeg](https://ffmpeg.org) licensed under the
[LGPLv2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html), and its
source can be downloaded here: \`ffmpeg-$version.tar.xz\` below (unmodified
FFmpeg $version, sha256 \`$sha256\`, with FFmpeg's signature
\`ffmpeg-$version.tar.xz.asc\`). Every package carries FFmpeg's libraries,
built LGPL-only by \`packaging/ffmpeg/build.sh\` in HWVX-SDL's source
(https://github.com/Alaxend0l/HWVX-SDL); each has a \`licenses/FFmpeg\` folder
with the licence and a NOTICE giving the configure line. The game loads them
at run time to play the PC and PS2 movies; they are not linked into it.
EOF
