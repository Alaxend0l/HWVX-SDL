# Content tab: sounds

A mod's own sound files (WAV, FLAC, Ogg Vorbis, MP3) over the game's sounds, made in HWVX Editor's
Content tab. Model: `src/content_sounds.rs` (lib); UI: `src/content_sounds_ui.rs`.

## In the editor

* **A new car's Sounds** (car inspector, under Paint): the sounds of the car's own pack, by what
  plays them, read from the player's `HW_Cars` archive (CarAInfo, offsets of CarA.h):
  * `wind (loop)`: `gears[1].soundSet` (CarA_Init's soundIds[0]);
  * `engine (loop, pitched by rpm)`: `soundSets[1]` (soundIds[1]); the other sets: `not played`;
  * `rolling (loop, on a surface)`: `rollSound`; `gear shift`: `shiftSounds[]`;
  * `not played (an unused sound set)`: CarAInfo 0x58 / 0x64, never read;
  * `used by the car (0x…)`: a reference elsewhere in the car's data.

  On the GameCube data the car packs have 2 to 5 sounds each, all named but one (car_5 sound/2,
  "used by the car"). Horns, crashes, skids and
  pickups are not in car packs: they are the shared packs' (HW_share), in the general list.
  Each row: ▶ the game's sound (decoded as the game plays it), its length, rate and loop (↻);
  **Replace…** picks a file, copies it into the mod's `sounds/` folder and writes the car entry's
  `"sounds": {"sound/1": "sounds/x.wav"}`. The row then shows ▶ of the file as the game's decoder
  reads it (src/audiodec, linked into xdlrs), its length and rate, and ✕ to drop it.
* **Sounds** (left panel, under Weapons): the mod's replacements of any other base sound, the
  mod's `"sounds": {"base://HW_share/share_1/sound/3": ...}`. **+ Replace a sound** picks an
  archive of the data folder, a pack with sounds and one of its sounds (▶ to hear it), which opens
  in the inspector with Replace… and **▶ Drive with it** (a Challenge race in that car for a car
  pack's sound, else in the mod's first new car, else car 0). Files of `sounds/` nothing refers to
  any more are listed with a **remove** button (else they go into the package).
* **Drive it** (the car's) and **Save as mod…** work as before: the folder, `sounds/` included,
  is run from a copy, or checked (no game data) and packed.
* `HWVX_CONTENT_SOUND=base://…/sound/N` opens that sound in the inspector at start (with
  `--content DIR --app-shot FILE` for pictures).

The previews play with the system's player (aplay, afplay, PowerShell), as the media view's
"Play here" does, the file at its own rate (the game resamples it to the base sound's rate).

## A new car's own sounds (game side)

A new car loads its base car's pack under its own number, and base:// ids name the base pack, so
`base://HW_Cars/car_7/sound/1` reaches car 7 and every car made from it. The entry's `"sounds"`
(added to schemas/Mod.schema.json's entry, `$defs/sound` shared with the mod's `"sounds"`) is for
the entry's own pack only: `Port_ModsBeginPack` takes the number asked for besides the TOC id
(DataGamePackLoad.c), and Port_Mods.c's `EntrySounds` finds the car (or item) entry whose number
that is; its sounds apply after the base:// ones and the pack skips the .xdl cache. The Rust
packer (xdlrs modcheck manifest) checks them as it checks `"sounds"`. Keys are `sound/N` (every
release) or `sound/gc:N` (one release's); the editor writes `sound/N` with the indices of the data
it reads.

## Testing

* `cargo test` (tools/browser): the model (import, set, drop, save, reopen), schema-valid through
  `xdlrs::validate::MiniValidator`, the packer's manifest reading entry sounds; tones made in the
  tests, never game audio. With `HWVX_TEST_DATA=<GC files>`: every car pack's roles; with
  `HWVX_TEST_OUT` too: Save as mod's `check_and_pack` on a mod with both kinds (12 s for the
  index). `xdlrs` has `modcheck::manifest::tests::entry_sounds`.
* A game run: `HWVX_SOUND_TEST_MODS=OUT/mods cargo test --release --lib game_run_mod` writes
  `OUT/mods/soundtest` (a new car from car 7 whose engine is a loud 2 kHz tone; its uid in
  `OUT/mods/soundtest.uid`), then `SDL_AUDIO_DRIVER=dummy FAST=1 tools/run_test.sh build DATA OUT
  NAME 40 HWVX_MODS_LOG=1 HWVX_MODS_DIR=OUT/mods HWVX_START=race:mode=challenge,car=UID
  HWVX_AUDIODUMP=OUT/NAME.f32` and `xdlrs audio level OUT/NAME.f32`. Measured (GC): the new car
  logs `mods: Sound test: uid://… sound/1: sounds/tone.wav, 11025 samples at 11025 Hz, loop 0`
  and its 250 Hz-4 kHz band is -20.1 dBFS (low band -29.0); car 7 in the same run with the mod on
  logs no replacement and stays the game's (-31.4 dBFS mid, -22.1 low).
