# Packages

Release builds for every platform come from one script each, which CI
(`.github/workflows/build.yml`) runs as is and you can run locally. Outputs
go to `dist/`; scratch (SDL builds, AppDirs...) to `build-pkg/`.

| Package | Script | Runs on | Output |
|---------|--------|---------|--------|
| Windows x86_64 `.exe` | `packaging/windows/build.sh` | Linux or macOS with MinGW-w64 GCC | `hwvx-VER-windows-x86_64.exe` |
| macOS `.dmg` (Apple silicon + Intel) | `packaging/macos/build.sh` | macOS | `HWVX-VER-macos.dmg` |
| Linux bare program | `packaging/linux/build.sh` | Linux x86_64 / aarch64 | `hwvx-VER.x86_64` (`.aarch64`) |
| Linux AppImage | (same script) | Linux x86_64 / aarch64 | `HWVX-VER-ARCH.AppImage` |
| Linux Flatpak | `packaging/flatpak/build.sh [ARCH]` | Linux x86_64 / aarch64 with flatpak-builder (another ARCH: qemu-user-static) | `HWVX-VER-ARCH.flatpak` |
| Android `.apk` | `cd android && ./gradlew assembleRelease` | Linux, macOS (JDK 17, Android SDK) | `android/app/build/outputs/apk/release/app-release.apk` |

Every package contains only the program; `cloudflared`, for hosting online through a
tunnel, is the player's to install (the Flatpak and Android can join such games, not host them). The game data is the player's own:
on first start the game looks in the usual places and, on desktop, asks for
the folder (see "Game data" in the top-level README).

## How they are built

- **SDL3** is the release in `SDL_VERSION` (source checked against
  `SDL_SHA256`; the Flatpak manifest repeats both). `build-sdl3.sh` builds
  it as a static library, so the Windows, macOS and Linux programs carry SDL
  inside them: the `.exe` and the bare Linux program are single files. The
  Flatpak builds SDL3 as a module of its own; Android builds it from source
  with the game (`HWVX_SDL_SOURCE_DIR` in `CMakeLists.txt`).
- **Windows** is cross-compiled with GCC (MinGW-w64, `cmake/mingw-w64-x86_64.cmake`)
  as a GUI program (no console; `-DHWVX_CONSOLE=ON` for one). It needs no
  DLLs beyond Windows' own.
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

## The editor (separate release)

The level editor (tools/browser, `hwvx-browser`, with the tools/xdlrs crate) is released on
its own as **HWVX Editor**, versioned from `editor-v*` tags (`editor-v0.1.0`); the game keeps
its `v*` tags (common.sh's `git describe` leaves `editor-v*` out). The editor package contains
the game, which it drives (viewer mode, test drives); the game package does not contain the editor.

| Package | Script | Runs on | Output |
|---------|--------|---------|--------|
| macOS `.dmg` (Apple silicon + Intel) | `packaging/editor/macos/build.sh` | macOS, cargo with both Apple targets | `HWVX-Editor-VER-macos.dmg` |

Version: `HWVX_EDITOR_VERSION`, else `git describe --match 'editor-v*'`, else `git describe`.
`hwvx-x` comes from the superproject's `hwvx-xfile` crate (above the checkout), or `HWVX_X`.

## Layout (macOS)

    HWVX Editor.app/Contents/
      Info.plist                 CFBundleExecutable hwvx-browser
      MacOS/hwvx-browser         the editor (universal)
      MacOS/hwvx                 the game, the plain program (no bundle of its own), SDL3 linked in
      MacOS/hwvx-x               archive extract/pack (world packs, base levels)
      Resources/schemas/         every *.schema.json: the level tools' root, the viewer's schemas
      Resources/level-samples/   the sample level projects (tools/level-samples)
      Resources/hwvx-editor.icns

Linux / Windows (stage 2) keep the same split: the programs together in one folder (`bin/`),
the resources in `../share/hwvx-editor/` or beside the programs.

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
