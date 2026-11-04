#!/bin/sh
# Flatpak bundle, for the machine's architecture or another one. Needs flatpak
# and flatpak-builder, with Flathub as a remote (for the runtime and SDK).
#   packaging/flatpak/build.sh [ARCH]  ->  dist/HWVX-VERSION-ARCH.flatpak
#
# ARCH (x86_64, aarch64) defaults to the machine's. Building for another one
# runs the SDK's compilers under emulation, so the kernel has to run its
# binaries: on Fedora `dnf install qemu-user-static-aarch64` registers that
# (Debian/Ubuntu: qemu-user-static binfmt-support). Expect it to be several
# times slower than a native build.
set -eu
arch=${1:-$(uname -m)}
target="flatpak-$arch"
. "$(dirname "$0")/../common.sh"
id=io.github.alaxend0l.hwvx
manifest="$root/packaging/flatpak/$id.yml"

if [ "$arch" != "$(uname -m)" ] && [ ! -e "/proc/sys/fs/binfmt_misc/qemu-$arch" ]; then
    echo "build.sh: can't run $arch binaries here: no qemu-$arch in /proc/sys/fs/binfmt_misc" >&2
    echo "  (Fedora: sudo dnf install qemu-user-static-$arch)" >&2
    exit 1
fi

flatpak-builder --user --arch="$arch" --install-deps-from=flathub --force-clean \
    --repo="$work/repo" "$work/build" "$manifest"
out="$dist/HWVX-$version-$arch.flatpak"
flatpak build-bundle --arch="$arch" "$work/repo" "$out" "$id"
echo "$out"
