# The editor's package (for packaging/README.md, a new section "The editor")

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
