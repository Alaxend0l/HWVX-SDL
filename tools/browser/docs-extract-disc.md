# Extract a disc

The game reads a GameCube disc image (.iso, .gcm) as it is. It can't read the PC CD, the PS2 DVD or a Dolphin .gcz that way: they have to be extracted into a folder first, which then plays as the game data. The game's launcher can import them; the editor does it too, with xdlrs's disc ripper (`xdlrs::disc`).

## Where

- **File > Extract a disc…** asks for the image, then opens the window.
- **The game data screen**: choose a disc image (or type its path). The screen says what it is and offers **Extract it…**. A GameCube .iso/.gcm also gets **Use it directly**.

The image dialog lists .iso, .gcm, .gcz, .cue and .bin. A folder holding the PC CD's files (HWVX.cab at its top) works too.

## The window

1. **The disc**: its release (GameCube, PC, PlayStation 2, or the PS2 prototype), the kind of image (.cue/.bin with raw sectors, NKit…), the game ID or archive version, the file count and the extracted size.
2. **Extract into**: by default `game-data/<release>` beside the editor's pref folder. For the packaged editor on Linux that's `~/.local/share/hwvx-editor/game-data/ps2`. The folder must be one of these:
   - new or empty;
   - an earlier extraction of the same disc, finished or stopped. Every file in it must be one of the disc's files at its size, or a `.part` of one. Extracting again finishes it.

   Any other folder is refused, so the extraction never mixes with other files.
3. **The free space** is checked first: the files plus 64 MB. For an earlier extraction, only what's still missing (plus the largest file) counts.
4. **Extract** runs on a worker thread. The progress bar shows files and bytes, and **Cancel** stops after the current file. Each file is written as `NAME.part` and renamed once it's complete. The image itself is never written to.
5. The result is checked as the game checks its data (`data_dir::check`). **Use this as the game data** remembers it and restarts the game on it, as the game data screen does.

Errors are given in plain words, with the ripper's details below them:

- not a Velocity X disc (or a Wii disc);
- an unknown PS2 version;
- an HWVX.cab compressed other than MSZIP;
- an image that's cut short;
- no space left;
- a folder the editor can't write to;
- a .cue whose .bin is missing.

## Code and tests

- `src/extract.rs`: the logic (`inspect`, `default_dest`, `check_dest`, `space_needed`, `check_space`, `plan`, `run`, `Job`).
- `src/extract_ui.rs`: the window.
- `xdlrs::disc::Disc::extract_until` is the cancellable extraction.
- `xdlrs::disc::test_images` provides the tiny-image builders, enabled by the `test-utils` feature, which the editor's dev-dependency turns on.
- `tests/extract.rs` runs on tiny GameCube, PS2 and PC (.cue/.bin) images: the destination's choice and refusal, the size check, the plain errors, whole extractions, and cancelling. It also extracts the real discs into `test-out/` when they're given, checks them, and removes them:

```
HWVX_TEST_PC_DISC=…/ims-hwvx.bin HWVX_TEST_PS2_DISC="…/SLUS-20412 (1.00).iso" \
  cargo test --release --manifest-path tools/browser/Cargo.toml --test extract -- --nocapture
```

Screenshots for scripts:

```
hwvx-browser --extract-disc IMAGE --app-shot info.bmp
hwvx-browser --extract-disc IMAGE --extract-to DIR --app-shot progress.bmp
```

The second one shoots at 30% of the way and leaves a partial extraction in DIR.
