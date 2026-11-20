# Joyride worlds as templates: a level of any mode on any world

(For docs/level-editor.md §1 "Starting a new level", §3 "Changing the base world", §6 "Each mode",
and docs/xdl-format/README.md §10.6.1/§10.6.5.) A Joyride level is an open-ended version of its
world: the whole world, its own intro, music and effects, and ten gears and a hidden key to pick
up. So it is the best stage for a level of another mode, and the only one on some worlds: retail
has no challenge on world 4. Every mode can now be made on every Joyride world.

## In the editor

**File > New level…** (or **New level…** in the Level tab): **on** lists the mode's retail
worlds, then, under *a Joyride world as the template*, the five Joyride worlds
(`world1 (Joyride world 1)` … `world5 (Joyride world 5)`) for every mode but Joyride. A Joyride
world picked shows:

* **Template: remove the gears and the key** (on by default): the level takes out the template's
  ten gears and its key (their pickup points), the triggers that count them and unlock cars, and
  the two objectives that show them on the intro box ("Collect 10 GEARS to UNLOCK CAR", "Find
  Hidden KEY to UNLOCK CAR"). The world is a clean stage; its intro, music and effects stay.
* a line on what the template gives the mode and what is left to you (below).

**Make it** extracts the world the first time (a few seconds, a spinner) and writes the project
with `xdlrs level new` (the same code: `xdlrs::level_template`). **Level settings** then says
"11 base pickups and 2 objectives removed (the template's gears and key)", with **keep them**
(the gears, key and objectives back, base triggers back to all).

The **Content tab**'s **+ New track**: *made from* lists the mode's retail tracks, then
"Joyride world 1 … 5 as the template" (not for Joyride). Such a track extends the mode's first
track, names the Joyride world (`world`) and starts with that world's select picture. Link a level
project made on the same world to give it a race.

### What each mode gets on a Joyride world, and what it needs

A Joyride world's script is free roam: an intro box, the player's car held until it is closed,
music; nothing ends the level and nothing counts rounds. A level of another mode keeps that
script (minus the collectibles) and New level adds the rest as files of its own, to change:

| mode | the template adds | you add | the checks warn when |
|---|---|---|---|
| Challenge | a 60 s clock on the HUD (`clock`, timer type 12, the HUD's), started when the intro box closes (`go`), `timeout`: lost at 0; a briefing line in the intro box (`brief`) | what wins it: a trigger with `end_level` result 4 (a volume to reach) | nothing ends it won |
| Mission | the same (no briefing screen: the game leaves out the alias mission's, which tells of another world) | the objectives and what wins it | nothing ends it won |
| Battle | round rules (kills 5, 3 rounds, 2 minutes on the HUD's round clock: `rules.roundTimer`), `next-round` (a round won: the car held, `reset_game_round` after 3 s), `won`/`lost` (the series over: `end_level` by whether player 1 won it) | opponents: a start each (port 0, the AI's) | under 2 starts; no `game_rounds_complete` trigger that ends the level; an AI start (it roams) |
| Drag race | rules (no target, 1 round), the same round flow | 3 more starts, one per lane (**Lanes: Check the lanes**), a finish volume and a `round_complete` trigger per car | not 4 starts; no round flow; an AI start |

* **One start.** A Joyride world has the player's start only (a battle world has 7, a drag
  world 4). Battles and drag races add theirs: + Add > Start, here.
* **AI on an open world.** The world has no AI scripts of the mode (a battle world's cars attack,
  a drag world's keep to their lanes): an AI car of the level's own drives its vehicle's own AI on
  the world's roads, wherever they lead. It roams: it won't chase a flag or keep to a lane. Use AI
  cars as traffic, rivals in a battle, or leave them out and race the clock (the joyride-drag
  sample). An AI start is port 0 in the editor; the game gets port 6 (the AI's) and spawns it
  with the level.
* **No results screens.** A battle or drag world shows its round and final results with
  interfaces of its own pack; a Joyride world has none, so the template's round flow goes on to
  the next round after 3 s and ends the level after the last. The win and lose of a challenge or
  mission go straight back to the menu (a challenge world's "objective complete" screen is in
  its own pack too).
* **The intro box.** The Joyride intro shows the world's objectives by their HUD ids (1, 2). The
  removed objectives are hidden and lose their ids, so the level's own objective with id 1 (the
  template's `brief`) is the intro's text. Keep it short (the box holds about two lines).
* **Lanes and the spawn box.** Open worlds are wide; the start checks (the cars' box against the
  world, starts overlapping) and **Check the lanes** work as on any world.

## In the format

`level.jsonc`:

* `mode` is the mode (and progression) the level plays in; `extends` a level of that mode (its
  progression entry: loading screen, opening, win and lose sequences; New level takes the mode's
  first, `base://mcp/track/39` for a challenge); `world` any world, a Joyride world's
  (`worlds_\sp_\joyride_\world4`) included. That was already allowed: what is new is the rest.
* `remove`: `{"items": [base pickup points], "objectives": [base objectives]}`, as
  `base://world4/world_0/World/0/quads/3/contents/items/0` and
  `base://world4/world_0/World/0/objectives/0`. Checked: the level's world, those kinds of record
  only, present in the base, never twice, never also patched; a kept base trigger that names a
  removed pickup is warned of. The compiled level file has
  `"removes": [{"kind": "item" | "objective", "target": walk, "path": "World/0/..."}]`; a test
  drive's own world pack has them written into the records.
* `starts`: port 0 (the editor's "an AI car") is compiled as port 6 and `dynamic` 0 (spawned with
  the level), as retail's AI starts. Port 0 used to crash the game (a player's start of port 0
  indexed its tables at -1); the game now refuses a level start of port 0.
* `test`: `{"win": [...], "lose": [...], "memory": [...]}` log lines tools/level_modes_test.sh
  expects (the samples').

`xdlrs level collectibles WORLD --data D` lists what a world's script collects; `xdlrs level new
DIR --mode M --world W --name N [--keep-collectibles] --data D` makes a project (as New level).
The collectibles are found from the script, the same way on every world: the `item_pickup`
triggers and the ones that unlock a car (code variable 100), then every trigger that reads or
writes a variable those do, until none is added; their pickup points; the objectives whose
"done" variable is one of those. On each of the five Joyride worlds (GameCube): 11 pickups (ten
gears, `HW_Items` pack 21; the key, pack 53), 2 objectives, 6 triggers (world 1: 18-21, 24, 25).
Their variables stay (inert once their triggers are gone: the world's variable table isn't
shortened, base triggers name variables by entry).

## In the game

* `Port_Level.c` `ApplyRemoves`: a removed pickup point's mode and round-start mode are
  `QUAD_ITEM_OFF` (never spawned; a round reset or a quadrant streamed in again keeps it off); a
  removed objective's info gets mode 0 (not shown, not counted by code variable 0x19A) and HUD
  object id 0xFF (so the level's own objective with that id shows in the intro box: the first id
  match wins). Elements of the world's arrays only; the bytes go back with the level. Log:
  `level: uid://joyridedash: 11 base items and 2 base objectives removed`.
* `Port_ContentData.c`: a registered track on another family's world (a Joyride world) shows
  that world's picture in its select when it names none (the alias's would show the alias's
  world): `content: track 57 is on the world of another mode's level: its picture, ...`. Its
  progression entry is still a copy of its alias's, but on a world none of the script's levels is
  on, the opening keeps only the steps another level of the script has too (a mission: the item
  select, not the alias mission's briefing and movie): `content: progression 0: level 14 is on a
  world of no level of its own: 1 of its alias's 3 opening steps`. Loading screen (HW_IPack 2),
  win and lose sequences are every level's; the music is the world's own script's.
* `HWVX_MENUDUMP` logs each progression level's loading screen, opening steps and win and lose
  sequences (`menudump: progression 1 level 0: worlds_\sp_\chal1_\ch1_l2; loading HW_IPack:2;
  opening; win 1: [+1]; lose 1: [+0]`).

Retail has no challenge on world 4, and no mission, battle or drag race on any Joyride world:
every (mode, Joyride world) pair but Joyride's own is new (challenges exist on worlds 1, 2, 3 and
5 as separate challenge levels; battles and drag races only on their arenas and strips).

## Samples and tests

`tools/level-samples` (each made with `xdlrs level new`, then given its goal):

* `joyride-dash`: a challenge on Joyride world 4: the goal down the road before 30 s.
* `joyride-gates`: a mission on Joyride world 2: the gate before 40 s.
* `joyride-flag`: a battle on Joyride world 1: one round, the flag before 25 s (the HUD's round
  clock), else player 2's; an AI car of its own.
* `joyride-drag`: a drag race on Joyride world 1: four lanes (the player's start patched into
  the first, three AI starts), the finish before 20 s, else player 2's.

`tools/level_modes_test.sh BUILD DATA OUT` now runs on xdlrs (no Python) and takes these four
by default with the four older samples; `MEMORY=1` runs the level files at run time instead of
their own world packs (as a shared mod's level is). Each sample: won with the gas held, lost
without, and its `test` log lines.
