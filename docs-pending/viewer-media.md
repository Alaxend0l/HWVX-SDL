# Viewer media: textures, sounds, songs (task #33)

For the superproject's docs/viewer-protocol.md (a new section "Media" after "Embedded frames", the
commands into the commands table, the tests under "Tests and tools"); a one-line pointer from
README §12.1 (tools/xdl_media.py: "the data browser shows the same media as the game decodes them,
docs/viewer-protocol.md Media").

## What it is (add)

- **Packs load with their sounds** in viewer mode: the viewer starts the sound manager
  (SoundManager_Init) as a state's start does. `HWVX_VIEWER_AUDIO=0` loads them without, as before.
- Nothing is decoded by the viewer itself (src/port/viewer_media.c): a texture is the texture
  object the game draws it with (m_gxTexObjArray, Render_CreateTextureSlot), decoded by the
  renderer's decoder (gx_tex.c `GX_PortDecodeTexObj`: the GameCube's tiled formats and CMPR as its
  texture unit mixes them, the PC's DXT/RGB(A), the PS2's palettised textures as PortPS2_Textures
  expanded them; a mod's colour recipe; the PC's colours squared, `Port_PCTexturesLookGC`); the
  levels are the ones the game samples. A sound is the sample in ARAM the mixer plays (after the
  loader's conversions: PCM16 to host order, the PS2's SPU-ADPCM decoded); a song is the MIDI
  player's copy (CreateMIDI) and its bank's samples.

## The media blob (new section)

Pixels and samples don't go over the socket: a second shared memory block (POSIX
`/hwvx-<key>-m<n>`, Windows `Local\hwvx-...`; opened and unlinked by the client as the frames' is).
A `texture` or `sound` reply has `blob: {name, key, size, serial, offset, length}`; reopen it when
`name` changes (it grows: a new one). Layout, little-endian:

| offset | field |
|---|---|
| 0 | `magic` u32 0x4D565748 ("HWVM"), `version` u32 1 |
| 8 | `key` u64 |
| 16 | `serial` u32: the reply's (0 while being written) |
| 20 | `length` u32, `dataOffset` u32 (64), `capacity` u32 |
| 64 | the payload |

The payload stays until the next `texture`/`sound` request rewrites it: check `serial` before and
after copying (src/media.rs `MediaReader`).

## Commands table (add)

| cmd | arguments | reply |
|---|---|---|
| `textures` | `pack?`, `shared?` (default true) | `pack`, `textures`: `{id, pack, index, textureId, drawn, format, gxFormat, width, height, levels, bytes, storedLevels, source, palette, flags: {wrapS, wrapT, linear, mipmaps, squared, mod}, usedBy, shared?}`. `id` is `texture/N`, or for the share packs' textures the pack uses (its texture relocations with a pack id) `base://ARCHIVE/share_N/texture/M`. `format`: GX names (`CMPR`, `RGBA8`, ...) or `PC_DXT1/PC_DXT5/PC_RGB8/PC_RGBA8` (the PS2's become PC_RGBA8); `source`: the release's stored format (`PSMT8` with `palette: {clut, colours}` on the PS2); `levels`: the ones the game samples (`storedLevels`: the header's). `usedBy`: the nodes whose texture slots name it (a geometry group's: its geometry). `drawn: false`: no texture object (past 512 texels). |
| `texture` | `node` (`texture/N`, or a `base://` id), `pack?`, `level?` | the listing's keys, `pixels: "rgba8"`, `levelData`: `[{level, width, height, offset, bytes}]` (every level, or the one asked for) in the blob, `alpha`: `opaque` / `cutout` / `blended` (level 0), `blob` |
| `sounds` | `pack?` | `pack`, `release`, `audio`, `sounds`: `{id: "sound/N", index, loaded, soundId, format (stored: pcm16be, pcm16le, pcm8, spu-adpcm), rate, length (samples), seconds, loop (sample, or null), bits, plays, volume}`; `song`: null or `{id: "song", kind: midi/it, division, tempo (us per quarter, the first), bpm, tempoChanges, ticks, events, notes, markers, channels, tracks (channels used: one SMF track), playing, tick, instruments}`; `instruments`: `{id: "instrument/N", drum, program, lo, hi, format, rate, length, loop, envelope: {attack, decay, sustain, release}}`. Only the pack whose song the MIDI player holds has one (one song at a time: loading a pack with a song drops the one held). |
| `sound` | `node` (`sound/N` or `instrument/N`), `pack?` | `rate, length, loop, bits, format, pcm: "s16"` (mono, little-endian) and `blob` |
| `play` | `node` (`sound/N`, `instrument/N`, `song` or `base://.../song`), `pack?`, `note?` (instrument: a MIDI note, GetNoteRate_), `mix?` (default false: what plays stops first) | `playing` (the base:// id), `voice` (sounds), `looping`, `device` (false: no output device, nothing is heard: the browser plays it itself). A sound plays as the game plays a sample (GetVoice, SetVoiceState: its volume, rate, loop); an instrument on a voice set up as a note on does; a song through SetMusicState and the sequencer. |
| `stop` | | `voices` (stopped), `song` (bool) |
| `status` | | adds `audio: {on, device, voices, song, tick?, last?}` |
| `hello` | | adds `media: 1` |

## For the mission editor and later edits (add to "For the mission editor")

- Media ids are node ids: `texture/N`, `sound/N`, `instrument/N`, `song`, with `base://` for
  another pack's; a texture link field's value (`texture/3`, `base://HW_share/share_1/texture/5`)
  is what `texture` takes, and `usedBy` goes the other way.
- Planned, behind the same names: `replace` `{node: "texture/N", level?}` with the RGBA in a blob
  the client writes (a `upload` request returns a writable blob of the size asked; the viewer
  encodes to the pack's format with the game's rules, rebuilds the texture object and the GPU
  copy), `{node: "sound/N"}` with PCM16 the same way (CreateSFX again). Like `set`, in memory only;
  files change through xdl_media.py `build` and a `load`.

## Tests and tools (add)

- `tools/browser/tests/media.rs` (`--test-threads=1`: one game at a time): HW_Cars car 5's
  textures and sounds against `tools/xdl_media.py extract` of the same pack, on the GameCube
  (`HWVX_DATA`) and the PC/PS2 (`HWVX_DATA_PC`, `HWVX_DATA_PS2`). Results: GameCube RGBA8, PS2
  (PSMT8 through the palette) and sounds/bank samples on all three: identical. GameCube CMPR:
  alpha identical, colours within 11 (the GameCube's 5/8-3/8 mixing and bit-repeat widening vs.
  xdl_media's DXT1 1/3-2/3 and x*255/31); transparent texels' hidden colour differs (average vs.
  black). PC DXT1/DXT5: within 4 (Apple BC rounding vs. // 3), after squaring xdl_media's colours
  as the game draws the PC's (also the PS2's) with the GameCube's look. Play/stop acknowledged,
  the voice counted in `status`, and heard: the mixed output (`HWVX_AUDIODUMP`) has the sound;
  the song's tick advances, stop stops it; an instrument at a note.
- `hwvx-browser --load HW_Cars:car:5 --select texture/0 --app-shot t.bmp` (or `sound/0`,
  `instrument/0`, `song`): the browser with that item open.

## tools/browser (add)

- The outline has Textures (the pack's, then each share pack's it uses), Sounds, and Song with its
  Instruments; each row says format, size/levels or rate/length/loop. A texture node, a texture
  link (in fields, slots, or a geometry's "Textures" list in the properties) and a "Used by" link
  jump between them.
- The central panel has tabs: 3D view / the open item. Texture: level, RGBA over a checkerboard /
  RGB / alpha alone, linear or nearest, wheel zoom around the pointer, drag pan, Fit, 1:1, the texel
  under the pointer (position, RGBA, hex), Export PNG (the level as decoded). Sound: waveform (min
  and max per pixel column; wheel zooms, drag pans, double-click shows all), the loop start marked,
  a playhead, Play (the game's mixer), Play here (the system's player: afplay, aplay, PowerShell;
  also when `play` says `device: false`), Stop, Export WAV; instruments can play at a note. Song:
  tempo, ticks, channels, Play/Stop and its position.
