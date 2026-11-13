# Shared content is untrusted: packs' sounds, mods' sounds, shared levels (branch `safety`)

Where it belongs: docs/xdl-format/README.md §10.6.7 (replaces its "Not checked yet" paragraph
and its Test line); the sound part goes with the pack loader's notes (§3, packs) and the
mods' sounds (§8 / §21.6). Mods and shared levels come from other people: everything they
ask for is checked before the game follows it.

## Sound descriptors (src/game/DataGamePackLoad.c)

- A pack's `numSounds` sound descriptors (0x20 bytes each, at `sfxOffset` in its sound data)
  are read only as far as they lie inside its `soundDataSize`. The ones past it are not read,
  their sound ids stay 0 (no sound), and the log says `pack (type T, id N): K of its M sound
  descriptors lie outside its ... bytes of sound data, not loaded`. A pack with sounds but no
  sound data gets ids of 0 too (they were uninitialised). `numSounds` itself, and so the
  soundIDs table's size, is unchanged: every reloc and every release of the pack stays as
  it was.
- A header whose sizes (summed in 64 bits) pass 2 GB is refused (`Port_Fatal`, naming the
  pack): the block was allocated with a wrapped-around size.
- Every retail pack fits: `--convert-xdl` loads every pack: GameCube 369 loads with 4788
  sounds, PC 363 / 4412, PS2 369 / 4788, all read (none cut); the PS2 prototype's packs a
  prototype world race loads (world, share, cars) too.

## Replaced samples' blocks (src/game/Port_Mix.c, Port_PS2.c, Port_Mods.c)

- PortMix_SFX (a mixed build's or a mod's sounds) summed the samples' sizes in 32 bits and
  compared after the loop: 32 mod sounds of 128 MB wrapped the sum to 0x400, then
  PortMix_SFXFill copied 128 MB into it. Now the sum is 64-bit (`PORT_MIX_SFX_SPACE`), stops
  at 256 MB (`PORT_MIX_SFX_BLOCK_MAX`) and the pack keeps its own sounds ("mix: a pack's
  replaced sounds need more than 256 MB: kept the base's").
- PortPS2_SFX: the same sum (64-bit already, its terms 32-bit); the PCM size of an ADPCM length
  (3.5 times) is 64-bit now, and a sample that alone passes the limit is dropped.
- A mod's sound is at most 16M samples once resampled (32 MB of PCM16, about 6 minutes at
  44.1 kHz; was 64M): half of the 64 MB sound memory a pack's sounds share, so a longer one
  could never be created; a silent FLAC of a few bytes decodes to that much.
- Test: `build/test_sfxsize` (ctest): crafted lengths, no game data, no 4 GB.

## Shared levels: what is checked now (src/game/Port_Level.c)

Added to the list of §10.6.7:

- **Parameter lists** of the level's own triggers, conditions and actions are walked by the
  layouts the game's functions read: Trigger.h's `TAC_PARAMS_*` macros, expanded in
  Port_Level.c into item strings (the same source tools/gen_schemas.py makes
  schemas/Trigger.schema.json from). Refused:
  - a handle that is not zero (where the game allows it), not one of the world's own pointers,
    and not a link of the level to a record of the right kind: a TileCar handle must be the
    start of a tile car of the world (or one the level appended), a variable/timer handle a
    slot of the table the game will have, a volume the world's or one of the level's, and so
    on (QuadItem, QuadFxPoint, QuadInstance, TilePath, TileCreature, Teleporter, objective);
  - records the level can't make (interfaces, vertex colour sequences, nav points, AI scripts,
    sound sequences, items by pointer): only handles the world's own triggers name;
  - a list, table or string that runs past its block; an operand type other than 0-2; a camera
    mode other than 27-30 or a camera place other than 1-6;
  - a music track past g_tracks[32] (read at both the 0x28 and the GameCube's 0x24 stride);
  - a code variable argument past the GameSettings table it indexes unchecked (0xC9, 0xCB,
    0xCC, 0xCE, 0xD1, 0xD2, 0x191-0x194: 1-4; 0x6A-0x6C: 0-10); a car code variable (1-3,
    20-27) whose argument isn't a tile car;
  - a print_debug_string message (what 2-8) that isn't a string table of strings under 128;
  - an interface pack name with a path in it (`..`, `/`, `:`) or not ended in its block;
  - a vehicle_damage "below" test dividing by 0; a p2p_activated trigger of the level's own
    (its words are thing handles).
- **Patches** go only over number fields of the records the world's links reach. The port
  walks the World by the schemas (src/game/Port_LevelFields.inc, made by `xdlrs level
  gen-fields` from schemas/*.schema.json: every x-link field is a pointer, the counts they
  name are counts) and refuses a patch over a pointer (null in the pack or not: World
  teleporters, speakers, lights...), a count, a word outside the reached records (the volume
  grid, neighbour lists, teleporter destinations), what any base trigger reads (its
  conditions, actions and parameters), or a number the game indexes with (`s_indexFields`:
  WorldVolume shape/type, WorldQuad id/counts/TOC fields, TileCar.port, TileCreature.thing,
  Teleporter debris/emitter/numDest, TileSpeaker.soundSeq, World.id, the impact-fx velocities,
  WorldTimer/WorldVariable active and ids, every byte of an objective's info).
- **Objectives** of the level's own: `value` non-null and sized for the info's type (a
  variable, a timer: 16 bytes; a code variable: 8, its argument in range, no car's); `done` a
  16-byte record; `text`/`doneText` string tables of the level (handles up to a null one, each
  string ended in its block, under 52 characters when a value goes in: it is copied into the
  object's savedText[0x34], else under 128).
- **Volumes** of the level's own: data sized for the type (force box 0x14, reverb 8, fog 16,
  light sizeof(Light), creature 1); a BSP of the level's own walked as BSPN0 walks it: every
  node it can reach inside its block, no path deeper than 1024 nodes.

Port fixes found on the way (retail never reaches them):
- `end_level`'s argument equal to the win/lose sequence count read one past the table
  (State_ProgressionManager.c: `>=`).
- An 11th play_interface_pack wrote past g_InterfacePackData[10] (State_GamePlay.c).

Not done (reported): GameSettings' unchecked code variable indices are bounded by the level
check only, not in GameSettings.c; the interface pack name a load/play action stores stays a
pointer into the level's part after the level is taken out.

## Tests

- `xdlrs level refuse-test BUILD DATA OUT [CASE...]` (`--list`; was tools/level_refuse_test.py,
  removed): 47 cases, one game run per sample world: the game's `HWVX_LEVEL_CHECK=FILE` (a list
  of level files) applies each as the world loads and takes it out again, logging
  `levelcheck: FILE: applied` or `...: not applied: WHY`. Each refusal case names the reason
  it must be refused for; the five samples and four `ok-*` variants (a crafted action of the
  level's own, an objective counter, a BSP volume) must apply.
- `HWVX_LEVEL_SWEEP=1 hwvx --convert-xdl OUT --data DATA`: every world pack gets an empty
  level keeping all its base triggers, whose lists are walked as a level's are
  (`levelsweep: NAME: ok`): GameCube 57/57, PC 51/51, PS2 57/57.
