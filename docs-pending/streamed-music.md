# Streamed music

Branch `streammusic` of HWVX-SDL. Belongs in docs/xdl-format/README.md §8.2 (as built: a new
manifest field, after "Tracks"), with the limits table in the asset-limits section (or §8.2,
"Room past retail limits"). HWVX-SDL/README.md gets the `HWVX_MUSICLOG` switch.

## What it does

The retail game's music is sequenced: a MIDI-like song and its instrument bank in a world or
music pack (the prototype's are Impulse Tracker modules), played by the port's MIDI/IT players
on AX voices. A mod can now replace any of those songs with recorded tracks of its own (OGG
Vorbis, MP3, FLAC, WAV). When the game would start that song, the track streams instead. The
mod ships only its own audio files, never game data.

```jsonc
"music": {
  // Challenge 1-2's song: one track, looping (loop points from its tags)
  "base://ch1_l2/world_0/song/0": "music/ch1-theme.ogg",
  // the front end's music: loop points given here, in the file's sample frames
  "base://HW_music/music_1/song/0": { "file": "music/menu.flac", "loopStart": 441000, "loopEnd": 5292000 },
  // a playlist: each track played through in turn, then from the first again
  "base://world2/world_0": [ "music/a.mp3", { "file": "music/b.ogg", "volume": 0.8 } ]
}
```

* **Keys** are song ids: `base://<archive>/<type>_<id>/song/0` (`/song/0` may be left out),
  the ids `pairing.md` gives songs. The archive is the pack file's own name, without its folders
  (`ch1_l2`, not `worlds_\sp_\chal1_\ch1_l2`). Every retail world has one (`world_0`), and so do
  the three front-end music packs (`HW_music/music_1..3`).
* **A track** is a path in the mod's folder, or an object: `file`; `loopStart` and `loopEnd` (in
  the file's sample frames); `loop: false` (played once, then silence); `volume` (a gain, 0..4,
  under the game's music volume).
* **Loop points**, first found wins: the manifest; the file's `LOOPSTART` / `LOOPEND` /
  `LOOPLENGTH` tags (Vorbis comments in OGG and FLAC, ID3 `TXXX` in MP3: the convention RPG Maker
  and many game rips use); a sidecar `<file>.loop.txt` with the same `KEY=value` lines (for WAV);
  else the whole file loops.
* **Several mods**: the last in the load order applies; the report names the conflict (as for
  entries).
* **Releases**: the songs' ids are the same on every release, so the field applies to GC, PC and
  PS2 alike (`releases` doesn't filter it).
* **Packages** may hold `.ogg`, `.mp3` and `.flac` (Port_ModsInstall's list; tools/xdl_mod.py's
  `ALLOWED_EXT` and its audio check are the asset-limits branch's). `schemas/Mod.schema.json`
  has the field (`music`, `$defs/musicTrack`).

## How the game plays it

`src/port/musicstream.c`, with small port hunks:

* `DataPack_LoadFile` (DataGamePackLoad.c) names the pack being loaded
  (`PortStream_SetPack`), and `CreateMusic` (Music.c) asks `PortStream_Create` first. With a mod
  entry, the game's music hooks (`DestroyMusic`, `SetMusicState`, `SetMusicVolume`; pitch,
  tempo, order and track hooks become `AudioVoid`) point at the stream, so everything the game
  does to its song (start at `World_Refresh` / the menus, pause and resume with the game, stop,
  destroy as the pack goes) happens to the stream. The MIDI/IT players are untouched.
* **Volume**: `g_musicVol`, the game's own: its music option (`SetMusicGVol`) times the world's
  and the scripts' fades (`SetMusicVol`, `ActionMusicChange`). Changes ramp over 20 ms.
* **Song changes**: a stream the game destroys, or one still playing when another song is created,
  fades out over 500 ms while the next starts (up to three at once).
* **Decoding**: miniaudio 0.11.25's decoders (dr_wav, dr_flac, dr_mp3) and stb_vorbis, vendored
  in `src/third_party` (both public domain). Each track is read a chunk at a time (2048 frames)
  at its own rate and channels (mono is spread to stereo), and a cubic (Catmull-Rom) resampler
  brings it to the mixer's 48 kHz into a ring of 16384 frames (341 ms, 128 KB). The AX thread
  tops the rings up between AX frames, outside the giant lock, and mixes them after the voices
  and aux buses, before the clamp. Memory per stream: the ring, the chunk and the decoder's own
  state (a few hundred KB at most); the file is never decoded whole.
* **Off without it**: with no enabled mod (or none naming the song) `PortStream_Create` returns
  at once and the mixer adds nothing: the retail game's audio is unchanged.
* `HWVX_MUSICLOG=1` logs each stream's tracks, rates, loop points, loops, states, volumes,
  fades and frees.

**Not done:** a playlist mixing the retail song with recorded ones (a sequenced song never
ends: it loops on its own markers, so there is no point to hand over at); shuffle; pitch and
tempo actions (`ActionMusicChange`) on a stream; the songs' reverb send (the stream is dry).

**Decoders:** byte-for-byte the official files: miniaudio 0.11.26 (mackron/miniaudio tag
`0.11.26`) and stb_vorbis 1.23 (nothings/stb commit 6d17754, 2026-11-10); sources and sha256 in
`src/third_party/README.md`. stb_vorbis keeps its stdio file API: miniaudio opens OGG in pull
mode with it, the only mode with real seeking (through callbacks it would use push mode, where
each loop's seek decodes the file again from the start).

## Tested

GameCube data, a test mod in the build folder (`tools/make_test_music.sh` makes the tracks with
the local ffmpeg: 8 s at 44.1 kHz, 0-2 s 300 Hz, 2-6 s 440 + 554 Hz, 6-8 s 1000 Hz, tags
`LOOPSTART=88200 LOOPEND=264600`), `HWVX_AUDIODUMP`, measured with `tools/stream_check.py`
(each tone's level per window):

* **Replaces a world's song** (OGG, Challenge 1-2, `HWVX_START=race:...`): the 300 Hz intro in
  the first 2 s, then the loop for the rest of a 40 s race; 1000 Hz never (max 0.002):
  it loops at the tagged loop end, 9 times. Levels 0.28 / 0.175: the tones' 0.4 / 0.25 times
  the world's music volume, 0.7.
* **Pause** (MP3, `HWVX_AUTOINPUT` Start, Start): the stream goes silent at the pause menu and
  carries on from where it was after it; its loops keep their spacing.
* **Every song replaced** (60 ids: worlds a FLAC + OGG playlist with the OGG at volume 0.5,
  `HW_music` a WAV with its loop points in `track.wav.loop.txt`), attract demo: the menu's WAV
  loops; each demo race's playlist goes FLAC → OGG → FLAC at levels 0.4·0.8 / 0.5·0.4·0.8; at
  each song change the old one fades out over ~0.5 s as the new one starts (a crossfade), and
  each old stream is freed ("freed" in the log) once faded.
* **Playlist volumes**: FLAC at `volume` 0.5 then a WAV at 1: 0.14 / 0.28 from the first window
  of each (the gain is applied as each track is decoded, so it changes exactly at the seam).
* **Memory**: a 4-minute race with the MP3 looping (59 loops): RSS 302,560 KB from 60 s to
  240 s, not a page more. Over 3 minutes of attract demo with streams starting and stopping,
  every stream is freed.
* **Retail unchanged** (no mods): the attract demo is frame-identical to pc-data
  (`tools/demo_compare.sh`, 20 s: 44/44). The mix (`HWVX_AUDIODUMP`, 25 s) is bit-identical to
  pc-data's for the first 6.1 s; after that two runs of the *same* build diverge too (the AX
  thread's timing against the game), so `tools/audio_compare.py` sets the difference against
  pc-data run twice: RMS 0.0041 vs 0.0092, spectrum 0.050 vs 0.047 (log10) – within run-to-run
  jitter. ctest passes, as does `tools/test_xdl_mod.py` with the port's installer.
* **Damaged files** (`tools/stream_fuzz.sh`: the player itself under AddressSanitizer and
  UBSan, 136 files made from the test tracks: empty, header only, truncated at 5-95%, random
  bytes with and without the format's magic, 1-1000 bit flips, damaged headers; each mixed for
  20 s with a stop and restart): no crash or sanitizer report. 36 can't be decoded and fall back;
  the rest play up to the damage (a read error ends the track or its loop), with NaN or huge
  samples kept out of the mix. It found one bug, fixed: a create that failed left the freed
  stream current. In the game, a mod whose playlist is a garbage OGG and a FLAC with 1000 bit
  flips logs `can't be decoded` / `nothing more can be decoded` / `no track of the mod's
  decodes ...; the game's own song plays`, and the race's mix matches the run without the mod
  (`tools/audio_compare.py`: RMS 0.0008). The port's installer copies a package's files after
  checking their sha256s and never decodes them, so a damaged audio file can't hurt it.
* **Not tested**: the game's music option itself (no way to set it without a save in a scripted
  run); it reaches the stream by the same hook as the world's 0.7 (`SetMusicGVol` → `g_musicVol`
  → `SetMusicVolume`).

## Audio limits

What the mixer and the sound code allow, before and after. Raised only where the retail game
allocates and sounds exactly as before.

| limit | before | after | why it's safe / why not raised |
|---|---|---|---|
| emulated ARAM (`ax.c` `ARAM_SIZE`) | 16 MB (the GameCube's) | 64 MB | `MAllocA_` allocates from the top of the heap, so retail samples move to other addresses with the same contents; calloc'd, so the untouched 48 MB cost no memory. Room for a mod's samples (more cars, items, interface sounds). |
| ARAM blocks (`Audio_.c` `g_memblk`) | 255 | 1024 | Each loaded sample is a block; the free list hands them out in the same order, so retail allocations are identical. Running out failed silently (`MAllocA_` returned 0: the sound was missing). |
| AX voices (`AX_MAX_VOICES`, the game's `NUM_VOICES`) | 64 | 64 | The game's own voice pool (Voice_.c) is 64 and steals by priority when full; more voices would change which retail sounds are cut in a busy race. A mod can't need more voices without that (it would have to be a per-mod switch). |
| voice sample rates | any (16.16 ratio from the sample's own rate, `Voice_.c`) | the same | Already unlimited: a mod's 44.1 or 48 kHz sample plays at its rate. |
| voice channels | mono (one AX voice per sample) | the same | Stereo sound effects would need paired voices in the game's voice code, not a mixer change. Streamed music is stereo (or mono, spread). |
| music | sequenced only (MIDI songs, IT modules) | + streamed tracks: any rate miniaudio and stb_vorbis read (8-384 kHz), mono or stereo (more channels mixed down), OGG/MP3/FLAC/WAV | Port-only path, off without a mod. |
| MIDI programs / drums (`g_pProg`, `g_pDrum`) | 128 + 128 | the same | The MIDI program and note ranges: a format limit, not a slot count. |
| mixer rate | 48 kHz stereo (the DSP's) | the same | The game's sound timing is built on 160-sample frames at 48 kHz (`Audio_Update(0xD05)`). |
