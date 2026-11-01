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
| Linux Flatpak | `packaging/flatpak/build.sh` | Linux x86_64 / aarch64 with flatpak-builder | `HWVX-VER-ARCH.flatpak` |
| Android `.apk` | `cd android && ./gradlew assembleRelease` | Linux, macOS (JDK 17, Android SDK) | `android/app/build/outputs/apk/release/app-release.apk` |

Every package contains only the program. The game data is the player's own:
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
- The **icon** is `icon.svg` (a placeholder design); `make-icons.sh` renders
  the macOS `.icns` from it, and `android/app/src/main/res` has it as an
  adaptive icon.

## Versions and releases

The scripts take the version from `HWVX_VERSION`, else `git describe`. CI
builds every push and pull request and keeps the packages as workflow
artifacts (Actions tab > the run > Artifacts). Pushing a tag `v*` also
publishes them as a GitHub Release:

```sh
git tag v0.1.0
git push origin v0.1.0
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
