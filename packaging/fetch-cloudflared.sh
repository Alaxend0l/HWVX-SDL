#!/bin/sh
# Downloads the Linux cloudflared program the packages carry (CLOUDFLARED_VERSION,
# checked against CLOUDFLARED_SHA256), printing its path.
#   packaging/fetch-cloudflared.sh ARCH DIR
set -eu
here=$(cd "$(dirname "$0")" && pwd)
version=$(cat "$here/CLOUDFLARED_VERSION")
arch=$1
dir=$2
sha256=$(awk -v arch="$arch" '$1 == arch { print $2 }' "$here/CLOUDFLARED_SHA256")
case $arch in
    x86_64) asset=cloudflared-linux-amd64 ;;
    aarch64) asset=cloudflared-linux-arm64 ;;
    *) echo "fetch-cloudflared.sh: no cloudflared for $arch" >&2; exit 1 ;;
esac
file="$dir/$asset-$version"

mkdir -p "$dir"
if [ ! -f "$file" ]; then
    curl -fsSL -o "$file.part" \
        "https://github.com/cloudflare/cloudflared/releases/download/$version/$asset"
    mv "$file.part" "$file"
fi
if command -v sha256sum >/dev/null; then
    echo "$sha256  $file" | sha256sum -c - >&2
else
    echo "$sha256  $file" | shasum -a 256 -c - >&2
fi
echo "$file"
