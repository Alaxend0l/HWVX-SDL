# Media tab: movies and the Audio browser

The left panel's **Media** tab has two parts: **Movies** and **Audio**.

## Movies

The list shows the loaded data's movies, from any release:

| Release | Files | Decoded by |
|---|---|---|
| GameCube | `fmv_/bglogo`, `hw_intro`, ... (THP: JPEG pictures, DSP ADPCM sound) | the editor itself (Rust: zune-jpeg, plus the game's own ADPCM decoder, as in `src/port/thp.c`) |
| PC | `FMV_/*.bik` (Bink 1, Bink Audio) | FFmpeg 6-9, loaded at run time |
| PS2, retail and prototype | `*.PSS` (MPEG-2 video, Sony SShd/SSbd PCM sound) | FFmpeg for the picture; the sound track is read by the editor, as in `src/port/pss.c` |

Each row shows the format, picture size, length and sound. Click a movie to open it where the 3D view is. The tab above the picture says **Movie: NAME**, and **3D view** switches back.

- **▶ Play / ⏸ Pause**, **■ Stop** (back to the start), **⏮ Restart**, and the **scrubber**. You can seek to any frame. The time shows as `0:43.03 / 2:17.14`.
- The sound plays through the system's player, as **Play here** does for a sound. It starts from wherever the picture is when you play, resume or seek.
- **Export audio WAV** saves the soundtrack (stereo PCM16 at the movie's rate).
- **Open in your video player** hands the file to the system's default app (`xdg-open`, macOS `open`, Windows `explorer`). This is the way to play:
  - a Bink or PSS movie when FFmpeg isn't found (the list then says "FFmpeg not found");
  - any other video, such as a modder's mp4, webm or mkv.
- **Open a video…** picks any file. THP, Bink and PSS files play in the editor. Any other video goes to your video player.
- **Rescan** reads the list again. It covers the data folder's movies plus any video files in an open mod's folder (listed as "mod: ...").

FFmpeg is looked for the same way the game looks for it:

1. `HWVX_FFMPEG_DIR`, if set (the only place looked in);
2. beside the program;
3. the package's `../lib` or `../Frameworks`;
4. the system's libraries.

## Audio

The Audio side lists counts of everything playable in the data. **Open the Audio browser** shows the list where the 3D view is (the **Audio browser** tab). The list covers:

- **sound**: every pack's sound effects, in every archive (HW_Cars, HW_Items, HW_share, HW_IPack, the worlds', the briefings' `audio_` packs...);
- **instrument**: every song's instrument bank samples;
- **song**: every pack's MIDI song (HW_music's and the worlds');
- **movie**: every movie's soundtrack;
- **mod file**: an open mod's own WAV, FLAC, Ogg and MP3 files (its sounds and Soundtrack).

You can search by archive, pack and name (for example `hw_cars car_5`, `sound/3`, `music`), tick the kinds to show, and pick one archive.

Each row has:

- ▶ / ■ to play and stop it. A sample's decoded audio plays through the system's player. A bank sample stored at a rate outside 4-96 kHz is resampled to 48 kHz for playback only.
- Its length, rate and loop start.
- **Open in pack**, which loads the pack in the viewer and opens the item in its own view: the waveform, and **▶ Play** through the game's mixer or sequencer.

A song's ▶ does the same and plays the song in the game. A movie's ▶ opens the movie.

Select a row to see its waveform with the loop marker. **Export WAV** saves a sample. For a song, **Export MIDI** saves its standard MIDI file.

## Scripts

- `--movie NAME[@SECONDS][!]` opens the first movie whose name contains NAME, paused at SECONDS. With `!` it plays from there.
- `--audio SEARCH` opens the Audio browser with that search, the first matching sound selected.

Both work with `--app-shot FILE.bmp`.
