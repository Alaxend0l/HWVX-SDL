# The PS2 prototype's worlds, music, replace mode and cache (HWVX-SDL)

Branch `protoworlds` of HWVX-SDL. Belongs in docs/xdl-format/README.md §9.1.1 ("As built in
HWVX-SDL: prototype import"), replacing its "Not done" and "Doubtful" lists; the tools paragraph
also fits §10.7.

## Reading the prototype's code

The prototype's executable (`orig/ps2-beta/SLUS_001.00`) is not stripped: its `.symtab` names
2,081 functions. `tools/mipsdis.py ELF FUNC...` disassembles functions by name (calls, globals
and strings named from the symbol table, `lui`/`addiu` pairs folded; `--find`, `--addr`), with no
toolchain: macOS's objdump has no MIPS target. Everything below was read from it, not inferred
from the data alone. No prototype function was decompiled into C.

## Cars in races

* **Dark cars** had two causes:
  * **Texture slots.** A race with a prototype car loads the prototype's own `HW_share` (82
    textures) and `HW_Cars` shared pack (20) on top of the release's. That ran past the game's
    0x1FF texture slots: `Gfx_LoadTexture_RAM` gives every texture after that the last slot, so
    the car's body got some other texture. With the prototype's content on, the table has
    `PORT_MAX_TEXTURES` (0x7FF) slots, and the tables indexed by a texture id are sized for it
    (`Gfx_DisplayList.c`, `Render_Geometry_.c`, `Render_QuadTri_.c`, `Render_Texture_.c`). The
    release keeps 0x1FF, so the attract demo is unchanged.
  * **The shadow hull.** `CarAInfo` 0x54 is the release's shadow volume (groups flagged 0x4000,
    which `Render_DrawShadows` draws as stencil volumes and `RenderGroup` skips). The prototype's
    is a closed hull stored as a plain lit geometry (`docs/beta-cars/research/geometry.md` §1.7).
    `CarA_Draw` draws it in a race, so it showed as a black shell over the car. It is left out:
    prototype cars have no shadow volume.
* **The x/y/z readout** is the prototype's own: a role-19 text object in each car's HUD reading
  `X: 00.0 |Y: 00.0 |Z: 00.0`, which the prototype fills with the car's position (it shows in the
  prototype running on PCSX2). The release never sets role 19's text. It is blank by default;
  `HWVX_PROTO_DEBUGHUD=1` (or `proto_debughud=1` in sources.cfg) keeps it and fills it in, as the
  prototype does (`Port_ProtoHudInfo`).

## Worlds as tracks (mode "added")

All 48 single-player and battle levels of the prototype become extra tracks: ids.lock
`uid://ps2proto-track-<level>`, `"source": "prototype"`, `"alias"` the release track of the same
level, `"world"` the prototype's archive (`~worlds_\sp_\joyride_\world1`), `"suffix": " (prototype)"`.
Each is a level of its twin's progression (missions, challenges, battles, drag races,
Joyride), so the world selects list "Monument City (prototype)" and so on next to the release's.
The level keeps the twin's progression entry (briefing, win and lose sequences, timer); the
world itself, with its scripts and objectives, is the prototype's. The prototype's 5
multi-player drag worlds (`race*l1`) have no track number in the release and are left out. In
modes other than "added" the entries stay in ids.lock as `"active": false`.

A prototype world pack has the release's `World` (the same offsets up to `gameData`, 0x9C), but
three of its record types differ (`Port_ProtoWorldLoaded` copies them into the release's layout
as the world pack loads, before anything reads it):

| record | prototype | release | how found |
|---|---|---|---|
| quadrant (`WorldQuad`, world 0x24) | 0x40 bytes | 0x50 | `DataPack_LoadQuadPack`, `LoadQuadrants` |
| nav point (`NavData.nodes`, world 0x78) | 0x70 bytes | 0x60 | `NavHolder_GetNavPoint` (7 × 16), `NavHolder_IsNavLinkNearer`, `_CreatePath`, `_ClearTempValues` |
| moving path (`TilePath`, world 0x14) | 0x14 bytes | 0x20 | `World_GetTilePath`, `StateGamePlay_Init` |

The other world arrays have the release's strides (`World_GetVolume` 0x30, `_GetTileLight` 0x20,
`_GetTileSpeaker` 0x40, `_GetTileCar` 0x60).

* **Quadrants.** The first 0x38 bytes are the release's (id, geometry count, geometries, region,
  neighbours, pack header and data, contents, loaded). The release adds `packId`, `fileOffset`
  and `size`, filled from the TOC. The prototype finds a quadrant's pack each time it loads it,
  by the TOC entry whose id is the record's id, with no geometry count in its high half, in the
  level's own archive (`LoadQuadrants`: `"XPS\<language>\" + world name + ".xps"`). So `packId` =
  the record's id, and `LoadQuadrants` takes a prototype archive: version 177/179, offsets from
  the end of the entry, ids without the count. The quadrant packs themselves are the release's
  layout and load as PS2 packs.
* **Pack names.** The world names its quadrant, texture and collision packs (0x7C, 0x84, 0x8C)
  with the release's folders; they all live in the level's own archive. They get the `~` prefix,
  so they're opened from the prototype.
* **Nav points.** Position, link costs (0x10) and links (0x20), four each and inline; the links
  again with the way forward first (0x30); radius (0x40); index (0x44); two floats (0x48, 0x4C,
  not identified); the chance byte (0x51); A* state (0x54 parent index, 0x58 cost, 0x5C parent);
  0x60 a blocked flag; 0x62 type; 0x64 next way point. The links run both ways. The release keeps
  the links A* follows (`prev`, with costs) apart from the ways on (`next`). Converted: `prev` and
  costs from 0x10/0x20; `next` from 0x30, without its last link when it has more than one (the way
  back); speed 0 (no limit); enabled.
* **Moving paths.** Anim, speed, record and thing, as the release's, without `enabled` (the
  prototype spawns every path) and draw distance.

**Tested:** `tools/proto_worlds_check.sh` starts each of the 48 as a race under lldb: all load and
run with no crash. Races were looked at on Joyride `world1` (it matches the prototype on PCSX2:
the same billboards, chevron sign, palm trees and night sky), `ch1_l2` (checkpoints and the
objective work) and drag `race1l2` (the opponents drive).

**Doubtful:** the item packs a prototype world spawns (pickups, by pack id) come from the
release's `HW_Items`, whose ids may mean other items. The prototype's pause screen (its mission
info toggle) is not used: the release's interface runs. The nav direction rule is a guess: AI
cars may turn back at junctions.

## Music

The prototype's songs are Impulse Tracker modules (`IMPM`). Its IOP module (`AUDIO.IRX`) plays
them; `CreateMusic` hands the song and bank over by RPC. The release's game code has an IT player
too (`IT.c`, matched from the GameCube decomp, ARAM samples), and `CreateMusic` picks it for
`IMPM`. The modules are standard IT headers (`HW E3 1`: 96 orders, 17 instruments and samples, 79
patterns). Only the samples differ: each sample header says 16-bit and compressed (flags 0x0B),
its data is SPU-ADPCM in the song bank at the header's offset, and loops are in the ADPCM blocks'
flags (bit 1 on every block of a looping sample, bit 2 at the loop start), not in the header.
`PortPS2_ProtoIT` decodes them into a bank of its own and rewrites the headers (PCM byte offset,
16-bit, uncompressed, loop). The prototype's worlds and `HW_MUSAK` then play their music with
the release's player.

## Mode "replace" beyond cars

The prototype is a fourth source in `Port_Mix.c` (`PORT_MIX_PROTO`), used by mode "replace" when no
mixed-build recipe is loaded (`Port_MixProtoReplace`). Each pack loads as its release has it.
Every texture and sound the pairing table pairs with the prototype comes from the prototype's
copy, read from its file in its own layout:
* TOC offsets from the end of each entry;
* no sector padding;
* sound pointers and data, the relocation tables and the pointer tables, then the blob;
* texture level count one too many, RGB24 textures.

The copy is checked as any other: thumbnail distance under 24, sound duration within 15%.
Prototype sounds are SPU-ADPCM and are decoded to PCM16 (`PortPS2_DecodeSound`). This is also the
dialogue swap: `HWB_M01`–`14` sounds that pair are replaced (58 of 233; the rest are different
recordings and stay the release's). Not heard yet: the release loads those packs only from its
briefing screens.
`tools/gen_mix_pairs.py` writes the prototype's column (`g_portMixPairs[][PORT_MIX_RELS]`).
Prototype packs (`~` names) are never mixed. The cars are still replaced whole (their pack ids
mean other cars in the prototype).

## The .xdl cache

Prototype packs are cached like the release's (`HWVX_XDL`, `HWVX_XDL_WRITE`, the launcher's
converted cache), in the cache folder `ps2proto` (`Xdl_OpenFileIn`). The release's packs mixed
by mode "replace" are cached in `gc+proto`, `pc+proto` and `ps2+proto`, so they never mix with
the plain release's. The fix-ups made after a load (cars, world records, quadrants) run on cached
loads too. They work from the pack as loaded and are idempotent.

## The Ripper Wheels

In the prototype a new game unlocks items 0–5 only (`GameSetting_Init`, 0x260AD4: Jet Boosters,
Energy Shield, Armor Repair, Magnet Mine, Super Zapper, Doom Disks). Ripper Wheels is item 6, so
it starts locked, as do the five after it, until a level unlocks it. Its pack (`HW_ITEMS` item 7,
whose ids start at 1) and its name and picture in `HW_IPACK` are there. This is the prototype's
data, not a menu bug, so the port leaves it as is. The port's item select is the release's in
every mode.

## Tools

* `tools/mipsdis.py`: disassembles the prototype's executable.
* `tools/proto_pack.py`: one of its archives, TOC and pack header; `--song` writes a pack's song
  and bank into a build folder.
* `tools/crash_bt.sh`: one run under lldb, the backtrace. It kills only the processes it started
  (lldb, debugserver, the game) by PID.
* `tools/proto_world.sh`: a race on a prototype world.
* `tools/proto_worlds_check.sh`: every prototype world, crash check.

No PCSX2 captures were made: getting the prototype into a given race takes menu input that
PCSX2 can't script. The comparisons above are against the user's PCSX2 screenshots: Joyride
`world1` with a green car and with Twin Mill II, and the pause menu.
