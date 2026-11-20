#!/bin/sh
# FFmpeg for the APK (and the licence texts of what the APK is built from):
# build.sh for each of the app's ABIs (android/app/build.gradle's
# abiFilters), laid out the way Gradle takes them. Needs ANDROID_NDK_HOME (the
# NDK of build.gradle's ndkVersion). Run it before ./gradlew; build.gradle adds
# these folders when they are there (without them the APK has no movies).
#   packaging/ffmpeg/android.sh  ->  build-pkg/android-ffmpeg/jni/ABI/lib*.so,
#                                    build-pkg/android-ffmpeg/assets/licenses/FFmpeg/
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work="$here/../../build-pkg/android-ffmpeg"
mkdir -p "$work"
work=$(cd "$work" && pwd)

rm -rf "$work/jni" "$work/assets"
mkdir -p "$work/assets/licenses/FFmpeg"
for abi in arm64-v8a x86_64; do
    "$here/build.sh" "android-$abi" "$work" "$work/prefix-$abi"
    mkdir -p "$work/jni/$abi"
    cp "$work/prefix-$abi"/lib/*.so "$work/jni/$abi/"
    cp "$work/prefix-$abi"/licenses/FFmpeg/COPYING.LGPLv2.1 "$work/prefix-$abi"/licenses/FFmpeg/LICENSE.md \
        "$work/assets/licenses/FFmpeg/"
    cp "$work/prefix-$abi/licenses/FFmpeg/NOTICE.txt" "$work/assets/licenses/FFmpeg/NOTICE-$abi.txt"
done

# The other licence texts (packaging/licenses.sh), from the sources the APK's
# build uses: SDL3's release (as build.gradle fetches it) and Mbed TLS's
# (CMakeLists.txt's FetchContent URL and hash).
root=$(cd "$here/../.." && pwd)
sdl=$("$root/packaging/fetch-sdl3.sh" "$root/build-pkg/android")
url=$(sed -n 's/^ *URL \(https:.*mbedtls.*\)$/\1/p' "$root/CMakeLists.txt")
hash=$(sed -n 's/^ *URL_HASH SHA256=\([0-9a-f]*\).*/\1/p' "$root/CMakeLists.txt")
tarball="$work/$(basename "$url")"
[ -f "$tarball" ] || curl -fsSL -o "$tarball" "$url"
if command -v sha256sum >/dev/null; then
    echo "$hash  $tarball" | sha256sum -c - >&2
else
    echo "$hash  $tarball" | shasum -a 256 -c - >&2
fi
rm -rf "$work/mbedtls"
mkdir -p "$work/mbedtls"
tar -xjf "$tarball" -C "$work/mbedtls"
"$root/packaging/licenses.sh" "$work/assets/licenses" "$sdl" "$(echo "$work"/mbedtls/*)"
ls -l "$work"/jni/*
