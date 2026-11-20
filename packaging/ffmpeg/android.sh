#!/bin/sh
# FFmpeg for the APK: build.sh for each of the app's ABIs (android/app/build.gradle's
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
ls -l "$work"/jni/*
