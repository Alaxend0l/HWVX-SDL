# The Content tab's Soundtrack

The editor's side of a mod's `music` section (`schemas/Mod.schema.json`; the game's side is
`src/game/Port_Mods.c` `Port_ModsMusic` and `src/port/musicstream.c`; the format is
docs/xdl-format/README.md §8.2.2). A mod replaces any of the game's 60 songs with recorded
tracks of its own: one track loops, several make a playlist.

Code: `src/content_music.rs` (the model: the song list, `impl ModProject` for the section, loop
tags as the game reads them, decoding and previews), `src/content_music_ui.rs` (the panel and
inspector). Tests: the model's unit tests, and `tests/content_music.rs` against the game.

## Using it

* **Soundtrack** (left panel, under Cars and Weapons) lists the songs the mod replaces.
  **+ Replace a song** lists every song, grouped: Menus (`HW_music/music_1..3`), Adventure,
  Challenge (in the game's order: race 1 is `ch1_l2`), Joyride, Drag race, Battle, Multi-player
  drag; a filter field narrows it; a tick marks the songs the mod has. **Choose files…** picks
  OGG, MP3, FLAC or WAV files (several at once).
* **Files** are copied into the mod's `music/` folder under a name the schema takes (letters,
  digits, `_ . - /` and spaces, a lower-case extension); the same file again is the copy already
  there; a `<file>.loop.txt` next to it goes with it. Files can also be added from a typed path,
  dropped on the window, or picked from the mod's own files no song uses.
* **The inspector** shows the song's tracks in order (⏶ ⏷ to reorder, ✖ to take one off; the
  file stays), each with its rate, channels and length, and:
  * **▶ Play**: the file decoded (the port's own decoders, through xdlrs) and played with the
    system's player; **▶ Loop seam**: 3 s before the loop's end then 3 s from its start, twice,
    as the game loops it; **■ Stop**.
  * **loops**: off writes `"loop": false` (played once, then silence). In a playlist a track
    plays from its start to its loop end and the next one follows (musicstream.c), so a
    playlist's tracks show only **ends at** (`loopEnd`).
  * **loop start / loop end**: ticked, the mod's own (`loopStart`, `loopEnd`, in the file's
    frames); unticked, the game's default, shown beside: the file's `LOOPSTART` / `LOOPEND` /
    `LOOPLENGTH` tags (read as musicstream.c reads them, in the first 256 KB), else its
    `.loop.txt`, else the whole file. The manifest's loop points work for every format,
    WAV included.
  * **intro**: a file of the mod played once before the track; **volume** 0-4 (1: a track
    mastered at about -17 dB RMS plays at the game's music level).
* **▶ Test in game** saves the mod, copies it to the editor's scratch folder and starts the game
  there with `HWVX_MUSICLOG=1`: a world's song in its race (`HWVX_START=race:mode=M,world=W`,
  then `HWVX_AUTOINPUT=+240:a,+60:a` for the intro box's Continue: a level's song starts after
  it); `music_1` at the title screen; `music_2` at the main menu (the title passed with Start).
  The log is `testdrive.log` in the scratch folder (the message names it).
* **▶ Base song** plays the game's own song through the viewer (its pack loaded, viewer_media.c's
  `play`) to compare.
* **Problems** (a missing file, a bad name, an id no song has, a loop ending before it starts)
  are listed under the tracks. A file longer than ten minutes is flagged: the game streams it,
  but Save as mod's check (src/audiodec's limit) can't decode it.
* **Save as mod** packs `music/` with the rest: modcheck decodes each audio file and refuses only
  the game's own audio.

## The front-end songs

Measured with a mod replacing each with its own tone (`menu_songs`, an ignored test):
`music_1` plays on the title screen and in the attract demo (HW_IPack interface_1); `music_2`
in the main menus and the select screens (interface_6, interface_3). `HWVX_START`'s menu and
select starts load no front-end song, so a test of them goes through the title. `music_3` is
named by screens of interface_6, 7 and 9 that a scripted run didn't reach: its test starts at the
title, and the label says only "Other front-end screens".

## Tested

* `cargo test --lib content_music`: the song list (60, keys the schema's pattern takes), tracks
  to and from JSON, file names, loop tags as the game reads them (Vorbis comment, ID3 TXXX,
  `.loop.txt`), decoding and the seam, and a section written, saved, reopened and valid against
  `schemas/Mod.schema.json` (xdlrs::validate::MiniValidator).
* `HWVX_DATA=... cargo test --release --test content_music`:
  `songs_match_the_viewer` (the list is the MCP's levels, in its order, each with the mode
  `HWVX_START` takes; a world's song and `music_1` play through the viewer) and
  `replacement_plays_in_the_game` (a test tone made in the test replaces ch1_l2's song with loop
  points in the entry; the game's log streams it and loops it at the entry's end; its
  `HWVX_AUDIODUMP` holds the loop's 440 Hz, the start's 300 Hz once and never the 1000 Hz after
  the loop's end; `xdlrs::level_share::check_and_pack` packs the mod).
