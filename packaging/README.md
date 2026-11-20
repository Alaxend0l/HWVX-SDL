| Windows x86_64 `.zip` | `packaging/editor/windows/build.sh` | Linux or macOS: MinGW-w64, cargo with `x86_64-pc-windows-gnu`, zip | `HWVX-Editor-VER-windows-x86_64.zip` |
| Linux AppImage (x86_64, aarch64) | `packaging/editor/linux/build.sh` | Linux (CI: Ubuntu 22.04), cargo | `HWVX-Editor-VER-ARCH.AppImage` |
# Packages

Release builds for every platform come from one script each, which CI
(`.github/workflows/build.yml`) runs as is and you can run locally. Outputs
go to `dist/`; scratch (SDL builds, AppDirs...) to `build-pkg/`.

| Package | Script | Runs on | Output |
|---------|--------|---------|--------|
| Windows x86_64 `.zip` | `packaging/windows/build.sh` | Linux or macOS with MinGW-w64 GCC | `hwvx-VER-windows-x86_64.zip` |
| macOS `.dmg` (Apple silicon + Intel) | `packaging/macos/build.sh` | macOS | `HWVX-VER-macos.dmg` |
| Linux program `.tar.gz` | `packaging/linux/build.sh` | Linux x86_64 / aarch64 | `hwvx-VER-linux-ARCH.tar.gz` |
| Linux AppImage | (same script) | Linux x86_64 / aarch64 | `HWVX-VER-ARCH.AppImage` |
| Linux Flatpak | `packaging/flatpak/build.sh [ARCH]` | Linux x86_64 / aarch64 with flatpak-builder (another ARCH: qemu-user-static) | `HWVX-VER-ARCH.flatpak` |
| Android `.apk` | `packaging/ffmpeg/android.sh`, then `cd android && ./gradlew assembleRelease` | Linux, macOS (JDK 17, Android SDK) | `android/app/build/outputs/apk/release/app-release.apk` |

Every package contains the program and FFmpeg's libraries
for the movies (see "FFmpeg" below); `cloudflared`, for hosting online through a
tunnel, is the player's to install (the Flatpak and Android can join such games, not host them). The game data is the player's own:
on first start the game looks in the usual places and, on desktop, asks for
the folder (see "Game data" in the top-level README).

Every package (the editor's too) has a `licenses` folder with the licence texts of
what the game is built from, as the launcher's Credits tab says: `packaging/licenses.sh`
(HWVX-SDL's LICENSE, SDL3's and Mbed TLS's from the sources the build used, the licence
sections of `src/third_party`'s single-file libraries, the Input Prompts glyphs' CC0) and
FFmpeg's (`packaging/ffmpeg/build.sh`). It is `licenses/` beside the program (Windows,
Linux `.tar.gz`), `usr/share/licenses/` (AppImage), `Contents/Resources/licenses/`
(macOS), `assets/licenses/` (Android, from `packaging/ffmpeg/android.sh`) and
`/app/share/licenses/io.github.alaxend0l.hwvx/` (Flatpak).

## How they are built

- **SDL3** is the release in `SDL_VERSION` (source checked against
  `SDL_SHA256`; the Flatpak manifest repeats both). `build-sdl3.sh` builds
  it as a static library, so the Windows, macOS and Linux programs carry SDL
  inside them. The
  Flatpak builds SDL3 as a module of its own; Android builds it from source
  with the game (`HWVX_SDL_SOURCE_DIR` in `CMakeLists.txt`).
- **Windows** is cross-compiled with GCC (MinGW-w64, `cmake/mingw-w64-x86_64.cmake`)
  as a GUI program (no console; `-DHWVX_CONSOLE=ON` for one). It needs no
  DLLs beyond Windows' own and FFmpeg's, which are beside it in the `.zip`.
- **macOS** is one universal app (`-DHWVX_MACOS_BUNDLE=ON`, Info.plist from
  `macos/Info.plist.in`), macOS 11 or later, signed ad hoc: not notarized,
  so the first start needs right-click > Open.
- **Linux** builds on Ubuntu 22.04 in CI so the programs run on distributions
  with glibc 2.35 or newer. The AppImage is made by linuxdeploy from
  `cmake --install` (desktop file, AppStream data and icon in `linux/`).
- **Android** (`android/`): SDL's Java activity (`HWVXActivity` adds storage
  access) and the game as `libmain.so`, for arm64-v8a and x86_64, Android 9
  or later. It renders with Vulkan (SDL_GPU) and plays with a game
  controller; there are no touch controls.
- The **icon** is `icon.svg` (a placeholder design). `icon-256.png` is it
  rendered for Linux, where AppStream and appimagetool need a PNG (after
  changing the SVG: `rsvg-convert -w 256 -h 256 icon.svg -o icon-256.png`);
  `make-icons.sh` renders the macOS `.icns` from it at build time, and
  `android/app/src/main/res` has it as an adaptive icon.

## Versions and releases

The scripts take the version from `HWVX_VERSION`, else `git describe`. CI
builds every push and pull request and keeps the packages as workflow
artifacts (Actions tab > the run > Artifacts). Pushing a tag `v*` also
publishes them as a GitHub Release:

```sh
git tag v0.1.0
git push origin v0.1.0
```

To release a commit CI has already built, without building it again, use
`packaging/release.sh` (needs the [GitHub CLI](https://cli.github.com)). The
tag must not start with `v`, and the packages keep their `dev-COMMIT` version
inside, under file names with the tag:

```sh
packaging/release.sh 0.2.0 --prerelease
```

## Android signing

Without a key the APK is signed with a debug key: it installs, but a later
APK signed with a different key can't update it. For a lasting key, make
one once:

```sh
keytool -genkeypair -v -keystore hwvx-release.jks -alias hwvx -keyalg RSA -keysize 4096 -validity 10000
```

and add these repository secrets (Settings > Secrets and variables > Actions):
`ANDROID_KEYSTORE_BASE64` (`base64 -i hwvx-release.jks`),
`ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD`.
Locally, the same values go in the environment as `HWVX_KEYSTORE` (the file),
`HWVX_KEYSTORE_PASSWORD`, `HWVX_KEY_ALIAS` and `HWVX_KEY_PASSWORD`.

## Updating SDL

Change `SDL_VERSION` and `SDL_SHA256` (`shasum -a 256` of the
`SDL3-VERSION.tar.gz` release), and the `url`/`sha256` in
`flatpak/io.github.alaxend0l.hwvx.yml`. The Android NDK version in
`android/app/build.gradle` follows SDL's `android-project`.

## FFmpeg (the movies)

The game plays the PC release's Bink movies (`FMV_/*.bik`) and the PS2's MPEG-2
ones (`*.PSS`) with FFmpeg, which it loads at run time (`src/port/bink.c`,
`SDL_LoadObject`): FFmpeg is never linked into `hwvx`, the build needs neither
its libraries nor its headers, and without a usable FFmpeg the movies are only
skipped. Every package carries FFmpeg's libraries, built by
`ffmpeg/build.sh` from the release in `ffmpeg/FFMPEG_VERSION` (9.0.2, the newest
point release of FFmpeg's newest branch with one; 9.1 came out the day this was
set up), checked against `ffmpeg/FFMPEG_SHA256` and, when gpg is there,
FFmpeg's signature (release key `FCF986EA15E6E293A5644F10B4322F04D67658D8`).
The source is used unmodified.

    packaging/ffmpeg/build.sh linux|windows|macos|android-arm64-v8a|android-x86_64 WORKDIR PREFIX

It is configured LGPL-only (v2.1 or later): no `--enable-gpl`,
`--enable-version3` or `--enable-nonfree`, no external libraries
(`--disable-autodetect`), shared libraries only, no programs or docs, and only
what the game's movies use:

    --disable-everything --disable-autodetect --disable-programs --disable-doc
    --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample
    --disable-debug --enable-shared --disable-static --disable-x86asm
    --enable-avcodec --enable-avformat --enable-avutil
    --enable-demuxer=bink,mpegps,mpegvideo
    --enable-decoder=bink,binkaudio_dct,mpeg2video
    --enable-parser=mpegvideo

plus the platform's (cross) compiler options, all written into each package's
`NOTICE.txt`. Each one is there because a release's movies use it: all seven
PC `.bik` are Bink video with Bink Audio DCT (no RDFT); every PS2 `.PSS`
(retail's five, the July 2002 prototype's four) is MPEG-2 (no MPEG-1) in an
MPEG program stream; `mpegvideo` (demuxer) is how `mpegps` recognises that
video stream's codec, and the parser cuts its frames. Not there: protocols
(the game hands FFmpeg its own reader, an AVIO context), swscale and
swresample (bink.c converts YUV to RGBA itself and SDL resamples), the PSS's
audio decoders (pss.c reads its `SShd`/`SSbd` PCM, which FFmpeg's demuxer
passes over), THP (`thp.c`). No x86 assembly (`--disable-x86asm`): nasm is one
build tool less, and the C decoders run all of the PC and PS2 movies at over
1000 frames a second on one core. (Bink Audio then differs from an
assembly-optimised FFmpeg's by up to about 1e-6, under a 16-bit sample's
step; against an ffmpeg run with `-cpuflags 0` it is identical.) After
building, `build.sh` checks that libavcodec says `LGPL version 2.1 or later`
and that no library's configuration has gpl, version3 or nonfree.

| Package | Libraries | Where | Licence, notice | Size added |
|---------|-----------|-------|-----------------|------------|
| Windows `.zip` | `avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll` | beside `hwvx.exe` | `licenses/FFmpeg/` | 1.9 MB unpacked, 0.85 MB in the zip |
| Linux `.tar.gz` | `libavcodec.so.63`, `libavformat.so.63`, `libavutil.so.61` | beside `hwvx` | `licenses/FFmpeg/` | 1.7 MB unpacked, 0.74 MB in the tar.gz |
| AppImage | the same | `usr/lib` (the base path's `../lib`) | `usr/share/licenses/FFmpeg/` | 0.68 MB |
| macOS `.app` | `libavcodec.63.dylib`... (universal, install names `@loader_path/...`, ad-hoc signed) | `Contents/Frameworks` (the base path, `Contents/Resources`, `../Frameworks`) | `Contents/Resources/licenses/FFmpeg/` | not measured (two slices: about 3.5 MB unpacked) |
| Android `.apk` | `libavcodec.so`... (unversioned, as Android packages them; 16 KB pages) | `lib/arm64-v8a`, `lib/x86_64` | `assets/licenses/FFmpeg/` | 3.2 MB (native libraries are stored uncompressed) |
| Flatpak | the same (`libavcodec.so.63`...) | `/app/lib` (`HWVX_FFMPEG_DIR`) | `/app/share/licenses/io.github.alaxend0l.hwvx/FFmpeg/` | about 0.7 MB (not measured against a bundle without it) |

Sizes are the x86_64 builds (Linux built on Ubuntu 22.04, Windows with Ubuntu
24.04's MinGW-w64).

The game's lookup (bink.c): `HWVX_FFMPEG_DIR` if set (the only place), else its
own folder (`SDL_GetBasePath()`) and the packages' (an `.app`'s
`../Frameworks`, an AppImage's `../lib`), then the system's library search
(and Homebrew's folders on macOS), for libavformat majors 63 down to 60
(FFmpeg 9.x to 6.0) by the names above, then unversioned (`libavformat.so`:
Android). libavutil, libavcodec and libavformat are opened in that order, so
each one's dependencies are already loaded. The **Flatpak** (runtime
org.freedesktop.Platform 26.08) builds the same FFmpeg as a module of its
manifest (`build.sh linux`, with the tarball and its signature as the
module's sources) into `/app/lib`, and sets `HWVX_FFMPEG_DIR=/app/lib`, so
that is the only FFmpeg the game looks at: the runtime's
`org.freedesktop.Platform.codecs-extra` extension, on the sandbox's library path when it is installed (FFmpeg 8,
`libavcodec.so.62` in 26.08), is built with `--enable-gpl`.

**Android**: `packaging/ffmpeg/android.sh` (needs `ANDROID_NDK_HOME`, the NDK
of `android/app/build.gradle`) builds both ABIs into `build-pkg/android-ffmpeg`,
which `build.gradle` adds to the APK when it is there; CI runs it before
Gradle. A local `./gradlew` without it makes an APK without movies.

**CI** caches each platform's built FFmpeg (`actions/cache`, keyed on
`ffmpeg/build.sh` and `ffmpeg/FFMPEG_*`), so a release build compiles it only
when those change. A build from scratch takes about 30 s on 4 cores (85 s of
CPU) per architecture here: expect a minute or two per job on GitHub's
runners, twice that on macOS (two architectures) and Android (two ABIs, plus
installing the NDK). The release job attaches FFmpeg's source
(`ffmpeg/source.sh`: `ffmpeg-VERSION.tar.xz` and its `.asc`) to the release
and puts the FFmpeg paragraph at the top of its notes; `release.sh` does the
same.

### LGPL checklist

FFmpeg's own checklist (https://ffmpeg.org/legal.html, "License Compliance
Checklist"; this is not legal advice), item by item:

- No `--enable-gpl` / `--enable-nonfree`: the configure line above; checked by
  `build.sh` after each build (avcodec_license() is `LGPL version 2.1 or later`).
- Dynamic linking (DLLs on Windows): loaded at run time, never linked
  (`src/port/bink.c`); `--enable-shared --disable-static`.
- Distribute FFmpeg's source, corresponding exactly to the binaries: the
  pinned tarball (sha256 and signature checked) is attached to every release
  by the release job / `release.sh`, unmodified (so there is no
  `changes.diff`).
- Explain how it was compiled: `licenses/FFmpeg/NOTICE.txt` in every package
  (version, source URL and sha256, the configure line), `packaging/ffmpeg/build.sh`
  in this repository and in each release's source archive. (The checklist
  suggests a text file inside the source archive; the tarball is FFmpeg's own,
  unchanged, so it can be checked against their signature, and the notice
  goes beside it in the packages and release notes instead.)
- Source as a tarball, on the same server as the binaries: the `.tar.xz` on the
  GitHub Release, beside the packages.
- "This software uses code of FFmpeg licensed under the LGPLv2.1 and its
  source can be downloaded here" where the program is downloaded: the release
  notes' FFmpeg paragraph (`ffmpeg/source.sh`) and the top-level README.
- "This software uses libraries from the FFmpeg project under the LGPLv2.1" in
  the about box: the game has no about box; the sentence is in `NOTICE.txt`,
  the README and the release notes. (Open: a line in the launcher.)
- EULA items (mention FFmpeg, no ownership claim, no reverse-engineering
  prohibition, translations): HWVX has no EULA; its licence is MIT, and
  `NOTICE.txt` says HWVX does not own FFmpeg.
- Don't misspell FFmpeg, don't rename the DLLs: FFmpeg's own names
  (`avcodec-63.dll`...).
- LGPL external libraries compiled in: none (`--disable-autodetect`).
- No GPL libraries (libx264...): none in this FFmpeg; the game (MIT) links SDL3
  (zlib licence), Mbed TLS (Apache-2.0) and public-domain/MIT single-file libraries
  (`src/third_party`).

### Updating FFmpeg

Change `ffmpeg/FFMPEG_VERSION` and `ffmpeg/FFMPEG_SHA256` (`shasum -a 256` of
`ffmpeg-VERSION.tar.xz` from https://ffmpeg.org/releases/, after checking its
`.asc`). The game takes FFmpeg 6.0 to 9.x (bink.c's `FF_MAX_MAJOR`); a release
with a new libavformat major needs bink.c checked against its headers first.
The CI caches rebuild on their own.

## The editor (separate release)

The level editor (tools/browser, `hwvx-browser`, with the tools/xdlrs crate) is released on
its own as **HWVX Editor**, versioned from `editor-v*` tags (`editor-v0.1.0`); the game keeps
its `v*` tags (common.sh's `git describe` leaves `editor-v*` out). The editor package contains
the game, which it drives (viewer mode, test drives); the game package does not contain the editor.
It carries the same FFmpeg as the game's packages (`packaging/ffmpeg/build.sh`, its licence and
NOTICE; "FFmpeg" above), for the game and the editor's movie viewer, and its releases have
FFmpeg's source beside the packages.

| Package | Script | Runs on | Output |
|---------|--------|---------|--------|
| macOS `.dmg` (Apple silicon + Intel) | `packaging/editor/macos/build.sh` | macOS, cargo with both Apple targets | `HWVX-Editor-VER-macos.dmg` |

Version: `HWVX_EDITOR_VERSION`, else `git describe --match 'editor-v*'`, else `git describe`.
`hwvx-x` comes from the superproject's `hwvx-xfile` crate: `HWVX_X_CRATE` (its folder), else the
superproject above the checkout; or `HWVX_X`, the program as is (packaging/editor/common.sh).

CI lives in the superproject (`Alaxend0l/HWVX-X-FILE-EDITOR`, `.github/workflows/editor.yml`): on an
`editor-v*` tag it builds the three from the HWVX-SDL commit the superproject pins, with hwvx-x from
the superproject's own crate, and publishes them as a GitHub Release there; by hand
(workflow_dispatch) as artifacts. This repository's releases are the game's alone.

The game's launcher has an **Editor** button (src/port/launcher.c): it starts `hwvx-browser`
when the editor is installed beside the game (in the game's folder, as in the editor's package;
macOS: `HWVX Editor.app` beside `HWVX.app` or in /Applications; Linux: an
`HWVX-Editor*.AppImage` beside the game's AppImage), else it reads **Get the editor** and opens
the editor's releases page.

## Layout (macOS)

    HWVX Editor.app/Contents/
      Info.plist                 CFBundleExecutable hwvx-browser
      MacOS/hwvx-browser         the editor (universal)
      MacOS/hwvx                 the game, the plain program (no bundle of its own), SDL3 linked in
      MacOS/hwvx-x               archive extract/pack (world packs, base levels)
      Frameworks/                FFmpeg's libraries (libavcodec.63.dylib...), for the movies
      Resources/schemas/         every *.schema.json: the level tools' root, the viewer's schemas
      Resources/level-samples/   the sample level projects (tools/level-samples)
      Resources/hwvx-editor.icns
      Resources/licenses/FFmpeg/ FFmpeg's licence and NOTICE.txt

Windows: one folder, `hwvx-browser.exe` (a windowed program; run from a console it writes to it),
`hwvx.exe` (windowed; with its stderr a pipe or file, as the editor runs it, the log goes there),
`hwvx-x.exe` (run without a console window), FFmpeg's DLLs and `licenses/FFmpeg/`, `schemas/`, `level-samples/`.
Linux (AppImage): `usr/bin/{hwvx-browser,hwvx,hwvx-x}`, FFmpeg in `usr/lib` (licence in
`usr/share/licenses/FFmpeg`), `usr/share/hwvx-editor/{schemas,level-samples}`.

What it reads at run time is only `schemas/` (xdlrs: level::Env, graph::Ctx), the game, and
hwvx-x. Not bundled: the pairing table (superproject docs/xdl-format/pairing, 19 MB; optional,
for decoding base geometry/texture ids; level checks, test drives and Save as mod work without
it), the Python tools, any game data (the player's own, `--data` or the viewer's sources).

## Lookup order (tools/browser/src/resources.rs)

- Resources (the folder holding `schemas/`): `$HWVX_EDITOR_RESOURCES`; beside the program
  (`../Resources`, `../share/hwvx-editor`, the program's own folder); else the checkout above
  the program (dev). `--schemas` / `--tools` still override. The packaged or given resources
  win over a `--game`'s checkout; without them, the game's checkout, then the program's.
- The game: `--game`; `hwvx` beside the program; the checkout's `build/hwvx`.
- hwvx-x: beside the program; the checkout's `target/release/hwvx-x`; the PATH.
- Scratch (viewer pref, test drives, base cache): `--pref`; in a package, the user's
  `~/Library/Application Support/HWVX-Editor/viewer-pref` (`$XDG_DATA_HOME/hwvx-editor`,
  `%APPDATA%\HWVX-Editor`), never inside the app; in a checkout, `viewer-pref/` beside the program.

## Headless (scripts, smoke tests)

    hwvx-browser --level PROJECT [--check] [--drive SECS] [--save-mod OUT.xmod] [--data DIR] [--pref DIR]

The Level tab's own steps without a window: the check (compile), a test drive until it
drives (at most SECS; the game stopped after), Save as mod. Exit 0 when all went well.
A mission's or challenge's information window at the start waits for A, as retail: headless
`--drive` presses A a few times (`HWVX_AUTOINPUT` set: that instead); a joyride, which never
holds the car, counts as driving once in the race. `HWVX_START=race` (which test drives use)
leaves out the level's opening sequence (an adventure mission's briefing, car and item select).
