# Content tab: Tracks

(For docs/level-editor.md §9b, the Content tab.) A mod can bring new tracks: entries of a
mode's world select, each a race of its own. One mod can hold several, a small championship.

**+ New track**: the mode (Adventure, Challenge, Joyride, Battle, Drag race), the retail track
it is made from, picked by its world select name and its world ("40  Challenge 2 (ch1_l3)";
the names are the select's own list, GameCube, `content_tracks::RETAIL`; the worlds come from
the viewer's `worlds`), a name, locked or not. It is written to mod.jsonc's `tracks` as
`{"extends": "base://mcp/track/N", "name", "unlocked", "thumbnail"}`, and the game puts it after
the mode's own levels in its world select (a new track number past 56, ids.lock).

**The inspector**:

* name, made from (the base track: its loading screen, steps, win and lose sequences, and its
  world unless a level says otherwise), unlocked.
* **Level**: none (the base track's level as it is), or a level project (the Level tab's
  folders, with level.jsonc): *Link a level project…*. The track then takes the level's base
  track (`extends` from its level.jsonc; a level whose mode and base disagree is refused).
  *Compile* (and ▶ Drive it, and Save as mod, which compile first) compiles it into the mod
  folder as `level/<id>.json`, sets the track's `level` and `world`, and copies the project's
  sources beside it as `level/<id>-src/` (as the Level tab's Save as mod does). Only what changed
  is compiled again. *Open in Level tab* edits the project; *Unlink* goes back to the base
  level (the compiled file goes). Which project a track is linked to is kept in the mod folder's
  hidden `.hwvx-editor-tracks.json` (relative when the project is inside the mod folder), which
  packages leave out: a shared mod never carries the maker's folders.
* **Picture in the world select**: the select's own pictures, shown from the player's data
  (HW_IPack's interface pack 6, decoded in Rust), each named after the tracks that use it; a
  click sets `thumbnail` to `base://HW_IPack/interface_6/texture/N`. "the base track's own"
  drops it. A new track starts with its base track's picture.
* **▶ Drive it**: saves, compiles the track's level if it has one, copies the mod into the
  editor's scratch mods folder and starts the game with
  `HWVX_START=race:mode=M,world=uid://TRACK`.

**Save as mod…** compiles the tracks' levels into the folder first, then checks for game data
and packs as before.

Tests: `cargo test content_tracks` (the tables, mod.jsonc as written, schema-valid with
xdlrs::validate::MiniValidator, links). With `HWVX_TEST_DATA=<game data>` (and hwvx-x on the
PATH) also: the pictures decoded, tools/level-samples/checkpoint-run compiled into a mod as a
track; `HWVX_TEST_KEEP_MOD=DIR` keeps that mod (three tracks) and packs it.
