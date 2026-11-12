# Levels of one's own: format, compiling, registering (task #21, stage 21.1)

Where it belongs: docs/xdl-format/README.md, a new §10.6.1 "As built: levels" under §10.6 "Events
and missions" (and a line in §8.2's "Not done yet": tracks can now bring a level of their own);
README of HWVX-SDL: `HWVX_LEVEL`. The editor's user guide comes with the later stages.

## What a level is

A new level of a mode (Challenge first; the format is mode-neutral) in an existing world: a
**source project** that extends a base level and adds its own records over the base world. It is
registered as a track (content registry, §10.5.1), so it is an entry of the mode's world select,
saved by its id, and played with the base level's loading screen, steps, win and lose sequences
(the progression script's level it aliases) in the world it names.

It never holds game data: its own records, and links into the player's world by structural
path. The port applies it to the world pack as the level loads; nothing of the base is copied.

## The project

```
checkpoint-run/
  level.jsonc            schemas/Level.schema.json
  triggers/intro.jsonc   schemas/Trigger.schema.json (as tools/xdl_graph.py writes a world's)
  triggers/cp1.jsonc ...
```

`level.jsonc` (tools/level-samples/checkpoint-run):

| key | |
|---|---|
| `id` | `uid://...` (1-13 of 0-9 a-z): the track's id (ids.lock, saves) |
| `name` | the world select's name |
| `mode` | challenge, mission (adventure), battle, drag, joyride |
| `extends` | `base://mcp/track/N`: a base level of that mode (Challenge 1 is 39) |
| `world` | the world as the MCP names it, `worlds_\sp_\chal1_\ch1_l2`; may differ from the base level's |
| `baseTriggers` | the base world's triggers that stay: `"all"` (default), `"none"`, or their numbers |
| `triggers` | the order of `triggers/NAME.jsonc` (default: by name) |
| `volumes` | named WorldVolume records: `{"center": [x, y, z], "radius": r}`; defaults: enabled, sphere, airbox; `radiusSq` = r² (the game's test is distance² < radiusSq) |
| `timers` | named WorldTimer records (`time` ms, `countUp`, `type`: the id the HUD shows); mode default shown/stopped |
| `variables` | named WorldVariable records, or a number (its value) |
| `strings` | named texts (or lists of paragraphs): the game's string tables, for print_debug_string (`what` 4: the objective line of the pause, win and lose screens) |
| `patches` | `{base record path: {field: number}}`: number fields of base records (`pos/y` for an embedded member) |

**Links** (in triggers and records):

* `base://<archive>/world_0/<path>`: a record of the base world by the path xdl_graph.py names
  it with: `World/0/tileCars/0`, `World/0/timers/0` (a timer's table entry, which scripts name),
  `World/0/quads/16/contents/fxPoints/7`, `trigger/18/param/p0` (an interface the world's own
  script plays). The archive must be the level's world.
* `level://volumes/NAME`, `level://timers/NAME` (its entry), `level://timers/NAME/to` (the timer),
  `level://variables/NAME`, `level://strings/NAME`, `level://triggers/NAME`: the level's own.

**Triggers** are the decoded form of docs/fields/objects.md (kinds, parameter lists by name).
Left out: `count` is 1 (once; `"always"` is 0xFFFFFFFF), `delay` = `delayInit`, other fields 0.

## Compiling: tools/xdl_level.py

```
xdl_level.py check PROJECT [--base DIR | --data GC_DATA]        errors only
xdl_level.py compile PROJECT [-o LEVEL.json] [--base ...]        the level file
xdl_level.py mod PROJECT OUT_MOD_DIR [--base ...]                a mod folder with it as a track
xdl_level.py base WORLD --data DATA [--cache DIR]                the base world extracted (cached)
```

* Errors name the file and place: `triggers/cp1.jsonc/param/...: level://volumes/nope: the level
  has nothing of that name`, `level.jsonc/volumes/cp1/radious: not a field of WorldVolume`,
  `extends: base://mcp/track/N with N a challenge level's (39..56)`, unknown kinds, links into
  another world, patches on link fields, the game's limits (TriggerStats: active triggers of a
  type at once, base triggers kept counted).
* With a base world (`--base`, or `--data` extracts it once with hwvx-x and xdl_graph.py into a
  cache outside the project: game data, local only) every base link is checked against it
  (`World/0/tileCars has 1 entries`).
* The **level file** (JSON, `"format": "hwvx-level"`): each of the level's records as bytes in
  the port's byte order (little-endian words, as packs are once loaded) with its pointer slots
  listed; triggers through xdl_tac.py's encoder (the same one that rebuilds retail packs
  byte-identically). A link is `{"part": id, "add": n}`, `{"slot": id}` (a timer's or variable's
  table entry), or a pointer walk from the World or a base trigger, `["p", off]` follow and
  `["a", n]` add, made from the schemas' offsets, which the three releases share; so one file
  works on any release's world. Patches: `{target, offset, hex}`.
* Deterministic; about 0.1 s for the sample.

## In the port: src/game/Port_Level.c

* `LoadWorldProcess` (state 5, before the world pack's refresh activates its triggers) calls
  `Port_LevelWorldLoaded(pack, world, track)`: the registered track's level file
  (`Port_ModsTrackLevel`: its mod's `tracks` entry's `"level"`, a file in the mod's folder), or
  `HWVX_LEVEL=<file>` for any level whose world matches (tests, the editor's test drive).
* Applying: the kept base triggers (copies) and the level's become the pack's trigger table, so
  the loader activates them as the pack's own; timers and variables are appended to the World's
  tables (new tables; the old entries stay where base triggers point); volumes, strings and
  parameter lists live in allocations of their own; links are resolved (a walk that leaves the
  world pack, or meets a null pointer in this release's world, is an error naming the record);
  patches keep the old bytes. A file made for another world is refused (logged).
* Undone when the world pack is released (`Port_ModsPackReleased`: its triggers out of the
  trigger lists first) or before a level is applied again: Restart re-applies it to the reloaded
  world.
* Log: `level: uid://checkpointrun (Checkpoint Run): 7 triggers (0 kept), 0 timers and 2
  variables added, 74 parts, 1 patches`, `... applied to worlds_\sp_\chal1_\ch1_l2 (track 57)`.

## Registering: a mod track with a level

`Mod.schema.json` tracks take `"level"`. `xdl_level.py mod` writes:

```jsonc
"tracks": {
  "uid://checkpointrun": {"extends": "base://mcp/track/39", "name": "Checkpoint Run", "unlocked": true,
                          "world": "worlds_\\sp_\\chal1_\\ch1_l2", "level": "level/checkpointrun.json"}
}
```

The registry makes it track 57, level 18 of the Challenge progression, an entry of the Challenge
select; `HWVX_START=race:mode=challenge,world=18` starts it.

## The sample: tools/level-samples/checkpoint-run

Challenge 1's world, its script dropped (`baseTriggers: "none"`) for one of its own: three sphere
checkpoints along the start road (cp1 at 82, 4, -780; cp2 1252, 3, -730; cp3 2410, 4, -1454), a
`progress` variable, the round timer patched to 40 s. `intro`: the objective text, the orbit
camera, the challenge's own intro screen; `go`: its countdown, control and the clock; `cp1`,
`cp2`: each counts only after the one before (a condition on `progress`) and shows "Checkpoint
k of 3"; `cp3`: after the other two, won (the challenge's finish screens and music, without its
unlocks); `timeout`: the clock at 0 first, lost.

## Tested

* `python3 tools/test_xdl_level.py [BASE]`: the sample compiles (deterministic), record bytes,
  links, walks, the patch, eight error cases, the base checks, the mod folder.
* `tools/level_test.sh BUILD GC_DATA OUT`: the sample as a mod: "Checkpoint Run" is level 18 of
  the Challenge select (menudump), applied as the race starts, a held throttle reaches the three
  checkpoints and the green "objective complete" screen, no throttle the red "objective failed"
  one; OUT/level_test.png shows the three.
* By hand: Restart from the pause menu takes the level out with the world and applies it again;
  the objective line shows "Checkpoint 2 of 3" on the failed screen after two checkpoints.
* ctest passes; the attract demo is frame-identical to pc-data.
* Note for tests on macOS: SDL_GPU (Metal) stalls while another window covers the game's (seen
  on pc-data's build too); `HWVX_RENDERER=opengl` doesn't (level_test.sh uses it).

## Test drive (stage 21.2)

For the editor's user guide, and docs/viewer-protocol.md (`raycast`) and HWVX-SDL's README
(`HWVX_TESTDRIVE`, `HWVX_LEVEL_START`, `HWVX_START` with a track id).

**In the data browser** (`hwvx-browser`, the Level tab): type the project's folder (the one with
level.jsonc) and Open; its name, mode, base level and world show. Pick the car, and:

* **Quick start**: the intro screen is left out and the start runs without its delays (the camera,
  the countdown, control and the clock at once). The level_start trigger's `interface_play` of an
  interface some `interface_end` trigger waits for is dropped, that trigger runs at the start
  instead, and the delays of the start's actions are cut. Only the level's own triggers change, and
  only in the test build.
* **From here** (a world loaded in the Worlds tab): player 1 starts on the ground under the camera
  (fly: where it is; orbit: what it looks at; the viewer's `raycast` down), facing the camera's way.

**▶ Test drive** (the top bar's button or the Level tab's, or **F5**): the project is compiled into
the editor's scratch folder (`<browser pref>/testdrive/`: `mods/editor-test/` with the level as
its track, `pref/` the game's preferences, `xdl/` its converted-pack cache, `testdrive.log`), and
the game starts straight into the level in a window of its own, in front. Errors stop it and list
under the button by file and place (`triggers/cp1.jsonc param: level://volumes/cp9: the level has
nothing of that name`, `level.jsonc volumes/cp1/radious: not a field of WorldVolume`). The line
under the button gives the compile time and, from the click: the level applied, in the race,
driving. Quitting the race (pause, Quit; or Main menu / Continue at its end) or closing the window
ends the test drive and brings the editor back; F5 again restarts it; ■ Stop ends it. The project's
files are never written. Only the game the editor started is ever stopped (its PID).

Command line: `hwvx-browser --level DIR [--car N] [--quick-start] [--test-drive]
[--testdrive-renderer opengl] [--tools DIR]`.

**Measured** (M-series Mac, GC data, the sample, quick start; tests/testdrive.rs):

| | |
|---|---|
| compile, changed / unchanged (the Python start included) | 53 ms / 44 ms (the tool itself 8.5 / 1.5 ms) |
| click to the level applied | 0.35-0.47 s |
| click to in the race and driving | 0.35-0.56 s (the first one, filling the .xdl cache: 0.56 s) |

Without the quick start the level's own intro runs (the sample: its intro screen waits for A, then
a 3.5 s countdown).

**How it works:**

* `xdl_level.py testdrive PROJECT SCRATCH [--car N] [--skip-intro]` compiles into
  `SCRATCH/mods/editor-test/level.json` only when something it reads changed: the key hashes the
  project's files, the tools and schemas it uses, the options and the base world's identity (the
  level file keeps it as `sourceHash`). It writes the scratch mod's `mod.jsonc` (a track of the
  project's id with `"level"`) when that changes, and prints one JSON line: `ok`, `errors`
  (`{file, path, message}`), `start` (`race:mode=challenge,world=uid://checkpointrun,car=5`),
  `products` (what the build made: the level file; stage 21.4's rebuilt world and quadrant packs
  go through the same cache and list), `ms`.
* The game runs with `HWVX_PREF_DIR`, `HWVX_MODS_DIR` (the scratch's), `HWVX_START` (a race
  straight into the track: `world=uid://...` names a registered track, `Port_Start.c`),
  `HWVX_XDL=1 HWVX_XDL_WRITE=1 HWVX_XDL_DIR` (the converted packs kept for the next test drive),
  `HWVX_TESTDRIVE=1`, and `HWVX_LEVEL_START=x,y,z,rotY` for "from here".
* `HWVX_TESTDRIVE=1` (src/game/Port_Level.c): the window is raised (a covered window also stalls
  SDL_GPU on Metal); the game logs `testdrive: in the race after N ms` (the change to the game
  play state) and `testdrive: driving after N ms` (the player's control first unlocked,
  ActionVehicleChangeControl); once in a race, a change back to the title screen state quits.
* `HWVX_LEVEL_START=x,y,z,rotY`: player 1's tile car moved there as the level applies (undone with
  the level; rotY in radians, forward sin, 0, cos).
* The viewer's new **`raycast`** (`from`/`to` [x, y, z], `down` [x, z], or `x`, `y` image pixels
  through the camera, `range`): the world's collision (the game's RayHitsTerrain); `hit`, `point`,
  `t`. An error without a world. Stage 21.3's ground drags use it too.

**Tested:** `cargo test --test testdrive -- --test-threads=1` (`HWVX_DATA`): the compile, the
cache, an edit compiling again, errors by file and place, the project left unwritten; two test
drives (the .xdl cache made, then used) applied, in the race and driving, and leaving through the
pause menu's Quit ends the game by itself; from here: the car at the place asked; Stop ends that
PID; `raycast` on ch1_l2 (the start's road at y 2, a miss above the world, an error without a
world). Browser screenshots with `--level ... --test-drive --app-shot`.

## Not yet (later stages)

* The intro screen's own text is the base level's (it lives in another pack); the level's text
  shows as the objective line. Replacing interface texts needs pointer patches across packs.
* New records in quadrants (items, effect points), new tile cars, objectives (WorldObjective) and
  BSP volumes; patches are numbers only.
* The editor UI, test drive, live preview in the viewer, other modes' templates, packaging with
  xdl_mod.py's check.
