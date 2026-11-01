#!/bin/sh
# Flatpak bundle for the machine's architecture. Needs flatpak and
# flatpak-builder, with Flathub as a remote (for the runtime and SDK).
#   packaging/flatpak/build.sh   ->  dist/HWVX-VERSION-ARCH.flatpak
set -eu
. "$(dirname "$0")/../common.sh" "flatpak-$(uname -m)"
id=io.github.alaxend0l.hwvx
manifest="$root/packaging/flatpak/$id.yml"

flatpak-builder --user --install-deps-from=flathub --force-clean \
    --repo="$work/repo" "$work/build" "$manifest"
out="$dist/HWVX-$version-$(uname -m).flatpak"
flatpak build-bundle "$work/repo" "$out" "$id"
echo "$out"
