# For docs/xdl-format/README.md §8.2 (a new #### 8.2.4 after 8.2.3), and a line in §10.8.x (xdlrs)

#### 8.2.4 The mod check in Rust (tools/xdlrs, `modcheck`)

The editor packs mods without Python: `tools/xdlrs/src/modcheck` is `tools/xdl_mod.py`'s
`index`, `check` and `pack`, with the parts of `xdl_media.py` (archives, pack sections, texture
levels, sounds, songs and banks, structural ids) and `xdl_gltf.py` (vertex positions, the PS2's
through `ps2_geom.py`'s packet reader) they use. The Python stays the reference.

* Same decisions: the same refusals in the same order (place, reason, the base:// id or recipe
  to use instead), notes, printed text and exit codes (0 passed, 1 refused, 2 error); `pack`
  writes the same `.xmod` bytes (Python's zipfile layout: stored, sorted, fixed dates).
* Sounds decode through the port's own decoders: `build.rs` compiles `src/audiodec` (dr_wav,
  dr_flac, dr_mp3, stb_vorbis) into the crate, so a mod's sound is checked as the samples the
  game plays; a PCM16 WAV is read as Python's `wave` reads it. The quiet-run rule (QUIET = 256,
  `_loud_windows`) and the fingerprint (`sound_fp`, rustfft) are kept.
* PNGs read as Pillow reads them (every colour type and bit depth, tRNS compared on its low byte
  as Pillow does, palettes shorter than their indices); the gradient hash resizes as Pillow's BOX
  filter does on a float image, and numpy's float32 std decides "flat".
* The index is the same file: numpy's `.npz` with the same arrays (`b32`, `b256`, `whole`, `px`,
  `rgb`, `snd`, `vtx` with their `_src`, `dh`, `dh_src`, `obj`, `sfp`, `sfp_frame`, `sfp_src`,
  `meta`), written without zip64 and read with or without it: an index made by either is used by
  the other, and the parity test compares them array for array.
* `mod.jsonc` is checked as `xdl_mod.Manifest` checks it, member by member. (The Python doesn't
  run `jsonschema` over Mod.schema.json; nothing to match there.)

```
xdlrs mod index DATA... [-o OUT.npz] [--archives A,B] [--no-geometry] [-j N]
xdlrs mod check MOD --index INDEX [--strip-unchanged]
xdlrs mod pack MOD -o OUT.xmod --index INDEX [--strip-unchanged] [--allow-partial-index]
```

Library: `xdlrs::modcheck::check_and_pack(mod_dir, index, Some(out) | None, &Options)` ->
`Outcome { code, stdout, stderr, refused: [Refusal { place, why, use_instead }], notes, packed }`
(None: `check`), and `build_index(&[data dirs], out, &IndexOptions, repo_root)` -> the printed
summary.

Parity (`python3 tools/xdlrs_parity.py --only modindex,modfiles,modcheck`, or
`tools/xdlrs_parity_mod.py`): the whole GameCube index and the partial (HW_Cars, HW_IPack,
HW_Items) GC, PC and PS2 ones identical array for array, sound fingerprints included; PNGs of
every kind, WAV kinds, FLAC/MP3/Ogg (the game's sounds re-encoded, a remix) decoded to the same
samples and fingerprints; every case of `test_xdl_mod.py`, the samples and
mod_assets/monument_remix checked and packed to the same text, codes and bytes. Index of the
whole GameCube install: 26 s Python, 10 s Rust (4 threads each).

Where the Rust is stricter than the Python (inputs the Python would crash on or misread; the
Rust refuses the file as unreadable):

* a `.png` that is another format (Pillow opens a JPEG named .png and checks its pixels; the
  port doesn't read PNGs, the check only reads PNG);
* JSON with NaN / Infinity (Python's json accepts them);
* malformed tRNS chunks (Pillow ignores them, the png crate stops);
* a WAV whose chunk runs past the RIFF size, odd glTF values (a string as an index): the Python
  stops with a traceback.
