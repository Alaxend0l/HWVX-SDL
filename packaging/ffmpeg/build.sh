#!/bin/sh
# FFmpeg's libraries for the movies (the PC release's Bink .bik, the PS2's
# MPEG-2 .PSS), built LGPL-only as shared libraries that the game loads at run
# time (src/port/bink.c, SDL_LoadObject); nothing of FFmpeg is linked into
# hwvx. The release is FFMPEG_VERSION, checked against FFMPEG_SHA256 (and its
# signature, when gpg is there); the source is used unmodified.
#   packaging/ffmpeg/build.sh TARGET WORKDIR PREFIX
#
# TARGET: linux (this machine's architecture), windows (x86_64, MinGW-w64
# cross: x86_64-w64-mingw32-gcc), macos (universal: arm64 and x86_64 built
# apart and lipo'd; Xcode's clang), android-arm64-v8a, android-x86_64
# (ANDROID_NDK_HOME or ANDROID_NDK_ROOT: the NDK).
#
# PREFIX gets:
#   lib/       the libraries, under the names the game looks for:
#              libavformat.so.63 (linux), avformat-63.dll (windows),
#              libavformat.63.dylib (macos), libavformat.so (android); same
#              for avcodec and avutil
#   licenses/FFmpeg/  COPYING.LGPLv2.1, LICENSE.md (FFmpeg's) and NOTICE.txt
#              (version, source, this script, the configure line)
# A PREFIX already built from the same version, target and script is kept
# (CI caches it keyed on this folder's files).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
version=$(cat "$here/FFMPEG_VERSION")
sha256=$(cat "$here/FFMPEG_SHA256")
# FFmpeg's release signing key (ffmpeg-devel.asc here, from
# https://ffmpeg.org/ffmpeg-devel.asc; https://ffmpeg.org/download.html#releases)
keyfpr=FCF986EA15E6E293A5644F10B4322F04D67658D8
url="https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"

if [ $# -ne 3 ]; then
    echo "usage: $0 linux|windows|macos|android-arm64-v8a|android-x86_64 WORKDIR PREFIX" >&2
    exit 2
fi
target=$1
mkdir -p "$2" "$3"
work=$(cd "$2" && pwd)
prefix=$(cd "$3" && pwd)

sha() {
    if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}
script_sum=$(cat "$here/build.sh" "$here/FFMPEG_VERSION" "$here/FFMPEG_SHA256" | sha | cut -c1-16)
stamp="ffmpeg $version $target $script_sum"
if [ -f "$prefix/.ffmpeg-built" ] && [ "$(cat "$prefix/.ffmpeg-built")" = "$stamp" ]; then
    echo "FFmpeg $version ($target) already in $prefix" >&2
    exit 0
fi

# The source: download, check, unpack (once per WORKDIR).
tarball="$work/ffmpeg-$version.tar.xz"
src="$work/ffmpeg-$version"
if [ ! -f "$src/.unpacked" ]; then
    [ -f "$tarball" ] || curl -fsSL -o "$tarball" "$url"
    echo "$sha256  $tarball" | sha -c - >&2
    if command -v gpg >/dev/null; then
        [ -f "$tarball.asc" ] || curl -fsSL -o "$tarball.asc" "$url.asc"
        gnupg=$(mktemp -d)
        GNUPGHOME=$gnupg gpg -q --batch --import "$here/ffmpeg-devel.asc" 2>/dev/null
        if ! GNUPGHOME=$gnupg gpg --batch --status-fd 1 --verify "$tarball.asc" "$tarball" 2>/dev/null \
                | grep -q "^\[GNUPG:\] VALIDSIG $keyfpr "; then
            rm -rf "$gnupg"
            echo "build.sh: $tarball: no good signature by FFmpeg's release key $keyfpr" >&2
            exit 1
        fi
        rm -rf "$gnupg"
        echo "ffmpeg-$version.tar.xz: good signature by $keyfpr" >&2
    else
        echo "build.sh: no gpg, the signature is not checked (the sha256 is)" >&2
    fi
    rm -rf "$src"
    tar --no-same-owner -xJf "$tarball" -C "$work"   # (as root in a sandbox, keeping the archive's owners fails)
    touch "$src/.unpacked"
fi

major() {   # major LIB: the library's major version in this release
    grep -h "define LIB$(echo "$1" | tr a-z A-Z)_VERSION_MAJOR " \
        "$src/lib$1/version_major.h" "$src/lib$1/version.h" 2>/dev/null | awk '{print $3; exit}'
}
libs="avutil avcodec avformat"

# LGPL v2.1+ only: no --enable-gpl, --enable-version3 or --enable-nonfree, and
# no external libraries (--disable-autodetect). Only what the game's movies
# use (bink.c, pss.c), each checked against the PC's seven .bik and the PS2's
# .PSS (retail and prototype): the bink demuxer, the Bink video decoder
# ("bink") and Bink Audio DCT (all seven .bik; none uses RDFT); the MPEG
# program stream demuxer, MPEG-2 video (every .PSS; none is MPEG-1) with its
# parser, and the raw mpegvideo demuxer, which is how mpegps probes a video
# stream's codec. No protocols: the game reads the files itself (an AVIO
# context over SDL's), and pss.c reads a PSS's SShd/SSbd audio, which FFmpeg's
# demuxer passes over. No swscale or swresample: bink.c converts YUV to RGBA
# itself and SDL resamples.
# x86 assembly (nasm) is left out: one less build tool everywhere, and the C
# decoders play 640x480 at 30 fps with room to spare.
common="--disable-everything --disable-autodetect --disable-programs --disable-doc
 --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample
 --disable-debug --enable-shared --disable-static --disable-x86asm
 --enable-avcodec --enable-avformat --enable-avutil
 --enable-demuxer=bink,mpegps,mpegvideo
 --enable-decoder=bink,binkaudio_dct,mpeg2video
 --enable-parser=mpegvideo"

configure_lines=""
# build NAME [configure args...]: configures and builds into $work/build-NAME,
# installed to $work/build-NAME/install; the line goes into NOTICE.txt.
build() {
    name=$1
    shift
    b="$work/build-$name"
    rm -rf "$b"
    mkdir -p "$b"
    line="./configure"
    for a in $common "$@"; do
        case $a in *" "*) line="$line '$a'" ;; *) line="$line $a" ;; esac
    done
    configure_lines="$configure_lines
  $name:
    $line"
    # shellcheck disable=SC2086
    (cd "$b" && "$src/configure" --prefix="$b/install" $common "$@")
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make -C "$b" -j"$jobs"
    make -C "$b" install
}

rm -rf "$prefix/lib" "$prefix/licenses" "$prefix/.ffmpeg-built"
mkdir -p "$prefix/lib" "$prefix/licenses/FFmpeg"
case $target in
linux)
    build linux --cc="${CC:-cc}"
    for l in $libs; do
        f="lib$l.so.$(major $l)"
        cp -L "$work/build-linux/install/lib/$f" "$prefix/lib/$f"
        strip --strip-unneeded "$prefix/lib/$f"
    done
    ;;
windows)
    # -static-libgcc: no DLL of libgcc's (libgcc_s_seh-1.dll) needed beside them.
    build windows --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
        --cross-prefix=x86_64-w64-mingw32- --extra-ldflags=-static-libgcc
    for l in $libs; do
        f="$l-$(major $l).dll"
        cp "$work/build-windows/install/bin/$f" "$prefix/lib/$f"
        x86_64-w64-mingw32-strip --strip-unneeded "$prefix/lib/$f"
    done
    ;;
macos)
    # @loader_path: each library finds the others beside itself.
    for a in arm64 x86_64; do
        case $a in arm64) fa=aarch64 ;; *) fa=x86_64 ;; esac
        build "macos-$a" --enable-cross-compile --target-os=darwin --arch=$fa \
            --cc="clang -arch $a" --extra-cflags=-mmacosx-version-min=11.0 \
            --extra-ldflags=-mmacosx-version-min=11.0 --install-name-dir=@loader_path
    done
    for l in $libs; do
        f="lib$l.$(major $l).dylib"
        lipo -create -output "$prefix/lib/$f" \
            "$work/build-macos-arm64/install/lib/$f" "$work/build-macos-x86_64/install/lib/$f"
        strip -x "$prefix/lib/$f"
        codesign --force --sign - "$prefix/lib/$f"
    done
    ;;
android-arm64-v8a | android-x86_64)
    ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
    if [ -z "$ndk" ]; then
        echo "build.sh: set ANDROID_NDK_HOME to the NDK (android/app/build.gradle's ndkVersion)" >&2
        exit 1
    fi
    tc=$(echo "$ndk"/toolchains/llvm/prebuilt/*/bin)
    case $target in android-arm64-v8a) fa=aarch64 ;; *) fa=x86_64 ;; esac
    # API 28 as the app's minSdk; 16 KB pages (Android 15 devices).
    build "$target" --enable-cross-compile --target-os=android --arch=$fa \
        --cc="$tc/$fa-linux-android28-clang" --cross-prefix="$tc/llvm-" \
        --extra-ldflags=-Wl,-z,max-page-size=16384
    for l in $libs; do
        cp "$work/build-$target/install/lib/lib$l.so" "$prefix/lib/lib$l.so"
        "$tc/llvm-strip" --strip-unneeded "$prefix/lib/lib$l.so"
    done
    ;;
*)
    echo "build.sh: unknown target $target" >&2
    exit 2
    ;;
esac

# What was built is what was asked for: the libraries say LGPL, and their
# configuration has no gpl, version3 or nonfree (avcodec_license() and
# avcodec_configuration() return these strings).
for f in "$prefix"/lib/*; do
    case $f in *avcodec*) grep -q "LGPL version 2.1 or later" "$f" || {
        echo "build.sh: $f does not say LGPL version 2.1 or later" >&2
        exit 1
    } ;; esac
    if grep -qE -- "--enable-(gpl|version3|nonfree)" "$f"; then
        echo "build.sh: $f was configured with gpl, version3 or nonfree" >&2
        exit 1
    fi
done

cp "$src/COPYING.LGPLv2.1" "$src/LICENSE.md" "$prefix/licenses/FFmpeg/"
{
    echo "FFmpeg in Hot Wheels Velocity X (HWVX)"
    echo
    echo "This software uses libraries from the FFmpeg project (https://ffmpeg.org)"
    echo "under the LGPLv2.1: the GNU Lesser General Public License version 2.1 or"
    echo "later, in COPYING.LGPLv2.1 beside this file. HWVX does not own FFmpeg; its"
    echo "authors are listed in its source (https://ffmpeg.org/about.html)."
    echo
    echo "The game loads these libraries at run time to play movies (a missing or"
    echo "unusable FFmpeg only means no movies); they are not linked into it. You may"
    echo "replace them with your own build of FFmpeg 6.0-9.x (libavformat 60-63)."
    echo
    echo "Version:  FFmpeg $version, unmodified"
    echo "Source:   $url"
    echo "          sha256 $sha256"
    echo "          signature $url.asc (FFmpeg's release key $keyfpr)"
    echo "          The same tarball is attached to every HWVX release (the game) and"
    echo "          HWVX Editor release on GitHub:"
    echo "          https://github.com/Alaxend0l/HWVX-SDL/releases"
    echo "          https://github.com/Alaxend0l/HWVX-X-FILE-EDITOR/releases"
    echo "Built by: packaging/ffmpeg/build.sh $target (HWVX's source, same release)"
    echo "Libraries:"
    for f in "$prefix"/lib/*; do echo "          $(basename "$f")"; done
    echo "Configure line(s), in a build folder beside the unpacked source:$configure_lines"
} > "$prefix/licenses/FFmpeg/NOTICE.txt"
echo "$stamp" > "$prefix/.ffmpeg-built"
ls -l "$prefix/lib" >&2
