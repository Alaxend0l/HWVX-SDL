# Sharing a level as a mod (editor stage 6): reference

Where it belongs: docs/xdl-format/README.md §10.6 (a new §10.6.6 "Sharing a level", after
§10.6.5) and §8.2 (a track's `"level"`; `"worldPack"` is for the editor's test drive only). The user
guide's "Sharing your level" section is written separately and uses the steps at the end.

## What ships, and why

A shared level is its **level file**: what `tools/xdl_level.py compile` makes, holding only the
level's own records and paths into the base world. The port applies it to the player's own world
pack as that pack loads (src/game/Port_Level.c). No world pack is built on the player's machine and
none is shipped, because a built world pack is the base world's data.

The alternatives were weighed and not taken:

- **A world-pack builder in C.** It would mean porting xdl_graph's Builder and the packer: a large
  second implementation of the pack layout, and it would write derived game data to disk.
- **Building the pack in the launcher.** The game can't run Python, and the launcher would need
  that same builder.

What the in-memory path lacked before was a level's added records. The level file's `appends`
section now adds them:

| level.jsonc | appended at load to | element |
|---|---|---|
| `starts` | World/0/tileCars (numTileCars) | a copy of the first tile car of port 1, its pos, rotY, port (and other fields given) set |
| `items` | World/0/quads/Q/contents/items (numItems) | a copy of the quadrant's first pickup, its pos, mode "on", id and respawn set |
| `fxPoints` | World/0/quads/Q/contents/fxPoints (numFxPoints) | a copy of the `template` effect point, its pos (and mode) set |
| `objectives` | World/0/objectives (numObjectives) | an entry of the level's own (arg, a link to its info record) |

Volumes, timers, variables, strings, triggers and patches work as before (§10.6.1).

## The level file's `appends`

```
"appends": [{"record": LINK, "pointer": n, "count": n, "elemSize": n, "what": "World/0/...",
             "elems": [{"fromPort": 1 | "fromIndex": i | "from": LINK | "part": ID,
                        "set": [[offset, "hex"], ...]}, ...]}, ...]
```

- `record` is the base record that holds the array (as a pointer walk). `pointer` and `count` are
  the offsets of its u32 pointer and u32 count fields, from the schemas.
- An element is a copy of a base element, with `set` bytes written over it, or a part of kind
  `"element"` whose bytes and links are the level's own.
- A link to an appended element is `{"append": a, "elem": j, "add": n}`. The compiler makes these
  for `level://starts/NAME`, `level://items/NAME`, `level://fxPoints/NAME` and
  `level://objectives/NAME`.
- The pack build (xdl_level_pack.py, `in_memory = False`) adds the same records itself and emits no
  appends.

Port_Level grows each array as follows:

1. Allocate an array of our own with room for the new elements.
2. Move every pointer of the world pack that points into the old array to the same place in ours.
   Base triggers that name tile car 2 then name it there. The pointer slots are the world pack's
   relocation list, kept as it loads: `Port_LevelNotePack`, called by DataPack_LoadDataPack for
   world packs (type 4).
3. Copy the old elements, then fill the new ones.
4. Point the record at our array and set its count.

Everything goes through the patch list, so it is undone when the pack is released or another
level is applied.

**The hook moved.** `Port_LevelWorldLoaded` now runs right after the world pack loads (LoadWorldProcess
state 0, after Port_ProtoWorldLoaded). Before, it ran at state 5. The cars' packs (state 4) and the
pickups (state 5) load from the arrays a level appends to, and they must see the new elements.
Triggers are still activated by the refresh at state 5. The demo is frame-identical.

## Packaging

```
python3 tools/xdl_level.py mod PROJECT OUT_MOD_DIR --hwmod OUT.hwmod (--data DATA | --index INDEX.npz)
```

- **The mod folder** holds:
  - `mod.jsonc`, with the track `{"extends", "name", "unlocked", "world", "level": "level/<id>.json"}`;
  - the level file;
  - the project's sources in `level/<id>-src/`.
- **The package** is made by `tools/xdl_mod.py pack`, with the no-game-data check against an
  index of the modder's own data. The index is made once from `--data` into the cache (about 25 s
  for the GameCube release); it is local and never shipped.
- **xdl_mod.py and a track's members:** it accepts `"level"` (a .json path inside the mod) and
  refuses `"worldPack"`, because a world pack is game data.
- **The sample.** Checkpoint Run packs to 10 files, 38,588 bytes, with "no game data found".

**In the editor.** The Level tab's **Save as mod…** asks where to save the .hwmod, then runs the same
command in the background (the scratch's cache holds the index). It reports the packer's line, or
lists why the level wasn't packed. The level must be saved first.

**"As shared".** This Level-tab checkbox runs the test drive the way an installed mod plays: the
level file applied at load (`testdrive` without `--data`). The default test drive still builds the
level's own world pack (§10.6.4).

## Tests

- **`tools/level_share_test.sh BUILD DATA OUT [PROJECT] [INDEX]`** runs four steps:
  1. Packs the level.
  2. Installs it with `hwvx --install-mod`, which is Port_ModsInstall, the code the launcher's Mods
     page runs, into a fresh preferences folder.
  3. Plays it once by a script of inputs.
  4. Checks the log ("level: uid://... applied", from the installed mod's folder) and the win: the
     log's end, or a challenge's green "Objective complete" screen.

  Checkpoint Run passes. The launcher's Mods page lists the installed mod as "Checkpoint Run 1.0.0",
  checked by the packer, with no problems.
- **In memory, appended records:** gear-grab (two pickups) and two-gates (two objectives) were each
  won once by their scripted inputs.
- **test_xdl_level.py** checks the appends (items, objectives, their links). **tests/testdrive.rs
  `save_as_mod`** checks the browser's packaging.

## Steps for the user guide

1. In the data browser's Level tab, open the level and save it (⌘S).
2. Click **Save as mod…** and choose where the `.hwmod` goes. The first time takes about 30 s
   while the check for game data indexes your copy of the game.
3. Give the `.hwmod` file to the other player.
4. The player opens the launcher's **Mods** page, clicks **Install a mod...** and picks the file.
   The mod is listed with its name and version.
5. The level is in its mode's world select, after the retail levels. Pick it and play.
   Optionally, drive it first with **As shared** ticked, to see it exactly as the player will.
