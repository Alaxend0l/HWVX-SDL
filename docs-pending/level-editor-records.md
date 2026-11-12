# The level editor: adding and taking out records, levels built into their own world pack (stage 21.4)

Where it belongs: docs/xdl-format/README.md §10.6 (a new §10.6.4 "Records of a level's own, and
its world pack", after §10.6.3), §8.2 (a track's `"worldPack"`), the viewer commands in
docs/viewer-protocol.md "Levels", and a line in docs/worlds/README.md (the starts' box). The
spawn envelope (first part) also belongs in docs/online (spawn spots).

## The starts' box: every car's collision box at once

`src/game/Port_SpawnBox.c` (`Port_SpawnEnvelope`): per axis the lowest min and the highest max of
every car's collision box (CarAInfo/0/collider, type 0: its min and max corners), over the cars
the content registry counts (the data's, a mod's, the prototype's), each car's pack loaded,
read and released (35-60 ms for 33 cars). Retail data, all three releases: x -5..5 (cars 5, 12,
17 are 10 wide), y -1.4 (car 7)..5.25, z -14..13 (car 25, Surfin' School Bus: the tallest and
longest), in the car's frame (x its side, y up from where a tile car stands, z forward).

* The viewer draws every start (tile car) as that box facing its way (the `spawns` overlay, the
  editor's marks); it computes it when a world loads, never while drawing.
* `spawnbox` {path?}: `box` {cars, min, max}, and with `path` written there as JSON.
* `startcheck` {starts: [{pos, rotY}]}: per start `clear`, `problems` ("its bottom 1.4 into the
  ground", "its bottom 16.4 above the ground", "a wall crosses the box", "a ceiling or overhang
  inside the box", "over a ledge or slope", "no ground under it") and `seat`: the position that
  puts the box's bottom on the highest ground under it (the editor's "To the ground" for starts).
  The world's collision (RayHitsTerrain): rays down at the bottom corners and the middle, along
  the box's edges and through its diagonals (the bottom raised off the ground).
* `tools/xdl_level.py check --spawn-box FILE` (the file `spawnbox` writes) warns of starts whose
  boxes overlap (the base world's tile cars as the level's patches move them, and the level's own
  starts): oriented footprints (separating axes) and heights.
* Online's spawn spots (Port_Online.c) use the envelope of the data's own cars (the same on every
  machine, whatever mods each has): 10 wide instead of 9 (car 25's alone). Tested:
  `tools/online8_test.sh build GC battle 60 8` passes.
* Retail starts are "1.4 into the ground" by this box (car 7's box reaches below its origin):
  a warning, not an error.

## Levels built into their own world pack (option 1)

`tools/xdl_level_pack.py` (module and tool): the base world's extracted project (`xdl_level.py
base`, cached; game data, local only) with the level's records added to its arrays and tables
and its patches written into the records' JSON, rebuilt by `xdl_graph.py`'s Builder (no copy of
the project: the changed documents are handed to it in memory; arrays, counts, relocation tables
and the trigger table laid out again) and packed with `hwvx-x pack`. About 1.6 s; cached on the
level's files, the tools and the base archive's hash (0.1 s when nothing changed). An unchanged
level builds the retail archive byte for byte.

What goes where:

| level.jsonc | into the world pack |
|---|---|
| `volumes` (sphere: center, radius; `box`: min, max) | appended to World/0/volumes; a box's BSP from `tools/bsp_build.py` (prism, build, bspn0_write) as a node of its own; `data` (the type's settings) a node of its own |
| `timers`, `variables` | records of their own (WorldTimer/WorldVariable and mode), their entries appended to the World's tables |
| `strings` | string tables |
| `starts` {pos, rotY, port, ...} | tile cars appended to World/0/tileCars: copies of the player's start (its vehicle, ranges), moved |
| `items` {pos, quad, id, respawn} | pickups appended to their quadrant's items (QuadContents: count and array) |
| `fxPoints` {pos, quad, template} | copies of a base effect point (its emitter, corona) appended to their quadrant's |
| `objectives` {text, doneText, done, kind: none/counter/timer, value, hud, shown} | WorldObjective entries and their info records |
| triggers | records in the pack's trigger table: the base triggers kept first, then the level's; the dropped ones stay in the pack, after the table (their nodes can still be linked, as the sample links the challenge's screens) |
| `patches` | the base records' fields, written before the build |

Node ids of the level's own: `base://<archive>/world_0/level/<n>[/...]` (the link pattern wants a
number), triggers `trigger/<26+k>`.

**The track loads it.** A mod track's `"worldPack": "worlds/ch1_l2.xgc"` (Mod.schema.json) makes
its world `@mod\<mod id>\worlds\ch1_l2` (Port_ModsSyncLock); DVDOpen asks
`Port_ModsResolveFile` for `@mod\...` names first: the file in that mod's folder (the release's
extension found beside it), so only that world pack comes from the mod: its quadrant geometry,
collision and textures are the base's, by the names the pack holds. Port_Level logs `level: its
own world pack ... applied to track N` (and honours `HWVX_LEVEL_START`); the in-memory path
(Port_LevelApply, the level file) stays for the editor's live preview and HWVX_LEVEL.

**The test drive** builds it: `xdl_level.py testdrive PROJECT SCRATCH --data DATA` (the browser
passes the data folder; `--memory` keeps the old in-memory way). Measured, the sample, quick
start: the first test drive of a world 5.6 s (the base extracted once, 4 s, then built), after an
edit about 2 s (the build 1.6 s, the game 0.4 s), unchanged 0.4-0.5 s to driving.

## The editor: adding and taking out

* **+ Add** (the Level tab's lists): a sphere volume, a box volume (a BSP), a start (port 0: an AI
  car's; 1-4 a player's), an item, an effect point (a copy of the base effect point selected),
  placed on the ground under the camera (fly: below it; orbit: what it looks at; an item's and an
  effect point's quadrant found by the viewer's new `quadat` {pos} -> {quad}); a timer, a
  variable, a text, an objective; a trigger of any kind, its parameters as the kind's layout has
  them (lists empty, numbers 0, keywords "on", links null).
* **Delete** (the inspector) for the level's own records and triggers, saying what uses it (the
  links then show as problems). A deleted trigger's file is deleted when saved.
* Triggers: **+ condition / + action** of a kind from the menu (its parameters' defaults), **−**
  takes one out, and changing a kind gives it that kind's parameters.
* The level's own starts, items and effect points are drawn (starts as the cars' box, cyan),
  picked, moved (Ctrl/Cmd + drag; their quadrant found again), turned (starts, Ctrl/Cmd + wheel)
  and seated ("To the ground": a start's box bottom on the ground); a start is checked against
  the world (`startcheck`) and the other starts (warnings in the inspector, orange boxes).
* Volumes: the box's size; the type's settings (force box: force, oneShot, carOnly; reverb:
  type, volume, feedback; fog: type, near, far, colour; light: direction, colour, type,
  subtract; creature: rescue), encoded by `xdl_level.py volume_data`. The viewer's `record` of a
  base volume decodes its settings (`volumeData`).
* Everything is undoable (adding, deleting, a kind changed).

## Not yet

* New volumes of types other than airbox aren't in the world's volume grid (VolumeHolder): only
  the airbox triggers find a volume without it (a warning says so). Adding them to the grid's
  cells is next.
* An item or effect point in a quadrant that has none yet (a new items table) is refused.
* The editor's live preview shows the level's own records (marks) and what the in-memory path
  applies (patches, volumes, timers, variables, texts, triggers); starts, items, effect points
  and objectives take effect in the built pack (the test drive).
* Packaged mods (21.6) need the world pack built on the player's machine: the game can't run the
  tools, so installing such a mod must run `xdl_level_pack.py` (or the launcher must).

## Tests

* `python3 tools/test_xdl_level_pack.py GC_DATA`: the sample with a box volume, a start, an item,
  an effect point, an objective and a trigger added: built, cached, extracted again: every count
  and array, the trigger table, the box's BSP, the start's port and place, the links, the patch;
  an item in a quadrant without items refused by name.
* `PACK=1 tools/level_test.sh build GC OUT`: the sample as a built world pack: in the Challenge
  select, applied, won and lost (also without PACK: the in-memory way).
* `cargo test --test level_edit` (`HWVX_DATA`): adding and taking out triggers and starts with
  undo/redo, the file deleted on save; the viewer's `spawnbox`, `startcheck` (seated, too low, too
  high, inside the world), `start` marks.
* `cargo test --test testdrive`: test drives through the built pack (the first, and from the
  cache), from here.
* `tools/browser_embed_check.sh build GC OUT`: the browser's picture embedded (car 5).
