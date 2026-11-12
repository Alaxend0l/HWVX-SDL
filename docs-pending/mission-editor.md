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

## Not yet (later stages)

* The intro screen's own text is the base level's (it lives in another pack); the level's text
  shows as the objective line. Replacing interface texts needs pointer patches across packs.
* New records in quadrants (items, effect points), new tile cars, objectives (WorldObjective) and
  BSP volumes; patches are numbers only.
* The editor UI, test drive, live preview in the viewer, other modes' templates, packaging with
  xdl_mod.py's check.
