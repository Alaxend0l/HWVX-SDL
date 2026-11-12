# The level editor: editing (stage 21.3)

Where it belongs: docs/xdl-format/README.md §10.6 (after §10.6.2 "Test drive": a new §10.6.3
"Editing"), the viewer commands in docs/viewer-protocol.md (a new section "Levels" after
"Worlds"), and a line in tools/browser's help.

## Using it (the data browser's Level tab)

1. **Open** a level project: its folder (type it, or **…** for the system's folder chooser:
   osascript on macOS, PowerShell on Windows, zenity on Linux). The tab lists, under the test
   drive's controls, **Level settings**, the level's **Triggers**, its own **Volumes**, **Timers**,
   **Variables** and **Texts**, and, once the level's world is loaded (**Load its world**, or the
   Worlds tab), **the base world**: starts (tile cars), sphere airbox volumes, items, timers,
   variables, objectives. A filter narrows every list. A record with a problem is marked ⚠.
2. **Select** from a list, or click in the 3D view: the base world's objects (the viewer's
   `pick`: tile cars, airboxes, items, effect points, instances) and the level's own volumes (the
   same ray against their spheres; the nearer wins). The selection is drawn yellow in the world
   (an arrow for a start, a sphere for a volume, a cross otherwise) with an x/y/z gizmo (red,
   green, blue); the level's own volumes are drawn magenta.
3. **The inspector** (the right-hand panel) edits the selection by its schema:
   * numbers (whole numbers within the field's range), enums as menus of their names (VolumeType,
     TacToken modes...), flags as boxes, vectors as x/y/z;
   * links as a menu of what can be named (the level's own records and triggers, the base world's
     listed records) and **🎯**: click the target in the view (Esc cancels);
   * a base record's fields are edited as **patches**: only what differs from the base is kept in
     level.jsonc's `patches` (`pos/x`, `rotY`...); **⟲** takes a field's patch out; "(patched)"
     marks them in the lists;
   * triggers: their kind, how conditions combine, the parameters by the kind's layout
     (keywords as menus, links as above, lists with + add and −), each condition, and each
     action's kind, delay ("after ... ms"), "every time" (count) and parameters;
   * texts: paragraphs (at most 255 characters each);
   * settings: name, description, author, version, unlocked, which base triggers stay.
   Fields left out of the file show greyed with the value the compiler gives them; "all fields"
   also shows the ones the game never reads (editor ids, unknown bytes).
4. **Move and turn**: Ctrl/Cmd + drag in the view moves the selection over the ground (the
   viewer's `raycast` through the pointer; a volume keeps its height above the ground, a start
   sits 2 units up); Ctrl/Cmd + wheel turns a start 15° a notch; **To the ground** drops it
   straight down onto the collision; or type the numbers.
5. **Undo / Redo**: Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, or the buttons; every edit, a burst of changes
   (a drag, typing) being one step.
6. **Checks, live**: a quarter of a second after an edit, the project as it is (a copy in the
   editor's scratch folder: the files aren't written) goes through `xdl_level.py compile --json`
   with the base world (extracted once into the scratch cache, about 4 s; then about 70 ms a
   check): the same checks as `xdl_level.py check` (unknown fields and kinds, links to nothing or
   past a base table's end, enum names, the game's trigger limits). Problems list under each
   record in the inspector and mark it in the lists; the count shows by **Live preview**.
7. **Live preview**: when it compiles, the level is applied to the world on show (the viewer's
   `level`, Port_Level as the game applies it): patched base records move at once (a start, an
   item, the timer's time). Triggers don't run in the viewer: their effect is seen in a test
   drive (F5), which compiles the project as edited, saved or not.
8. **Save** (Ctrl/Cmd+S, or the button; "Save*" when there are changes): the changed files are
   written, edited in place (src/jsonc_edit.rs): comments, blank lines, key order and formatting
   stay; a changed value is replaced where it was, a new member goes after the last one with its
   siblings' indent, a removed one goes with its comma; an array whose length changed is written
   again.

## Viewer protocol: "Levels" (new section)

src/port/viewer_level.c. All need a world on show (`world`), else an error.

| cmd | args | reply |
|---|---|---|
| `level` | `path` (a level file, xdl_level.py compile) or null, `overlay?` (bool: the level's volumes drawn, default true) | `level` (its id, or null), `path`, `overlay`. Applies the file to the world pack shown as the game does when the level loads (Port_LevelApply: what an earlier one put in goes first), or takes it out. Errors: the file's (`... the world has no trigger 40`) |
| `records` | `table`: tileCars, volumes, timers, variables, objectives, teleporters, tilePaths, tileCreatures, quads, items, fxPoints, instances | `records`: `{path, ...}` in table order: tile cars `pos, rotY, port`; volumes `shape, type, enabled` (spheres: `pos, radius`); items `pos, id`; fx points, instances, creatures `pos`; teleporters `pos`; timers `time, type, countUp`; variables `value, id`. `path` is the structural path (World/0/tileCars/0, World/0/quads/16/contents/items/2; timers and variables name the table entry, `/to` the record) |
| `record` | `path` (as above, also World/0, World/0/gameData, .../quads/N/contents, timers/N/to) | `path`, `struct`, `at` (@hex in the world pack), `fields` (decoded by the schema, as `get` decodes a typed node). Live: after `level` the patched values |
| `marks` | `marks`: `[{kind: sphere\|arrow\|cross\|line, pos, to (line), radius, rotY (arrow; forward sin, 0, cos), color [r, g, b]}]` (at most 256) | `marks`: how many. Drawn over the world every frame (dimmed through what hides them), until replaced; `[]` clears |
| `pick` (add) | | in a world also `path` (the record's structural path), `t`, and the ray: `origin`, `dir`; sphere airbox volumes are picked too (`kind: volume`) |

The level's own volumes are drawn magenta while a level is applied and `overlay` is on.

## Tests

* `cargo test --test level_edit -- --test-threads=1` (`HWVX_DATA`): the project model (patches,
  undo and redo over three edits, the copy passing `xdl_level.py check`, saving the two changed
  files with their comments and reading back equal, a broken link reported by file); the viewer:
  `records` and `record` on ch1_l2 (its start, the timer), errors without a world and past a
  table, `level` applied (the timer at 40 s, the start moved and turned, `records` seeing it,
  applied again, taken out: the base's values back), `marks`, `pick` at the start returning its
  path and the ray, `raycast` through the picture's middle meeting the road at the start.
* `cargo test --lib` (src/jsonc_edit.rs): comments and order kept, members added and removed
  (also the last, with trailing commas, into an empty object), arrays and type changes.
* `python3 tools/test_xdl_level.py`: error records for a patch's field, a trigger's part, a field.
* Browser screenshots: `hwvx-browser --level DIR --world ch1_l2 --level-select
  trigger:cp3|own:volumes/cp2|base:World/0/tileCars/0 --app-shot FILE`.

## Not yet

* Car names in the test drive's car picker (the names live in the interface pack's lists; the
  picker is still a number).
* Adding and removing records, new trigger kinds with their parameter forms: stage 21.4.
* A gizmo you drag by its axes (moving is over the ground, or by numbers).
