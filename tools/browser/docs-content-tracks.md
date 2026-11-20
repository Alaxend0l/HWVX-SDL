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
* **Picture in the world select**: *the game's* or *your own*. The game's: the select's own
  pictures, shown from the player's data (HW_IPack's interface pack 6, decoded in Rust), each
  named after the tracks that use it; a click sets `thumbnail` to
  `base://HW_IPack/interface_6/texture/N`, N the GameCube's number of the picture whatever the
  data (the PS2 numbers them the same; the PC's pack has them in another order, so the editor
  decodes the PC's own texture for the preview and the game finds the same picture there:
  Port_ContentData.c WorldPictureOn). "the base track's own" drops it. A new track starts with its base track's picture.
  Your own: *Choose image…* takes a PNG (at most 1024 x 1024 and 4 MB), copies it into the mod
  folder's `images/` (`images/<track id>-<file name>.png`; the copy it replaces, and the one of a
  deleted track, go) and sets `thumbnail` to that path; the panel previews it. The game draws
  it where the base picture would be, at that picture's size (256 x 256 in the world select,
  which shows its pictures stretched to about 5:3: a 5:3 PNG looks as drawn).
* **▶ Drive it**: saves, compiles the track's level if it has one, copies the mod into the
  editor's scratch mods folder and starts the game with
  `HWVX_START=race:mode=M,world=uid://TRACK`.

**Save as mod…** compiles the tracks' levels into the folder first, then checks for game data
and packs as before.

Tests: `cargo test content_tracks` (the tables, mod.jsonc as written, schema-valid with
xdlrs::validate::MiniValidator, links). With `HWVX_TEST_DATA=<game data>` (and hwvx-x on the
PATH) also: the pictures decoded, tools/level-samples/checkpoint-run compiled into a mod as a
track; `HWVX_TEST_KEEP_MOD=DIR` keeps that mod (three tracks) and packs it.

## A picture of the mod's own (for docs/xdl-format §8 / §10.5.1)

* **Schema.** An entry's `thumbnail` (tracks, cars, items: one code path in the game) is a
  `base://` texture link as before, or a PNG of the mod: `"images/my-track.png"`.
* **Game.** Port_Mods writes it to ids.lock as `@mod\<mod id>\images/my-track.png`. When a
  select's picture list is made for a registered entry with one (Port_ContentData.c FillItems),
  `Port_ModImageTexture` (src/game/Port_ModImages.c) finds the file in the enabled mod
  (`Port_ModsFilePath`), decodes it with stb_image 2.34 (src/third_party, MIT / public domain
  like stb_vorbis; PNG only), resamples it (area average) to the size of the base picture it
  stands in for (the alias's: 256 x 256 in the world select), stores it bottom row first as the
  game's pictures are, as GameCube RGBA8, in a texture object of the port's own past the game's
  and the button prompts' (`PORT_MOD_IMAGE_TEXTURES`, 32), made once per run. The game's texture
  slots are untouched. A file over 4 MB or 1024 x 1024, not a PNG, or missing: logged once, the
  alias's picture shows. Decoded in the game rather than at install: a mod folder put in by hand
  runs as it is, and the packer's check stays on the PNG the modder made.
* **Packer** (xdlrs modcheck manifest.rs): such a thumbnail must be a .png in the mod (no `..`,
  no hidden folders), at most 1024 x 1024 and 4 MB; the PNG goes through the no-game-data check
  as every PNG does (an artist's own image passes; one that is or looks like a game texture is
  refused).
* **Tests.** `cargo test` in tools/xdlrs: `modcheck_crafted::a_track_picture_of_the_mods_own`
  (packs; too big, missing, outside, not a PNG refused). tools/browser:
  `content_tracks::own_picture_tests` and, with the game data,
  `tests/track_picture_game.rs`: a gradient PNG made in the test as a Challenge track's picture,
  `HWVX_START=worldselect:mode=challenge,world=18`, the frame's big picture sampled: the middle
  is the PNG's yellow square, red grows across and blue down (the right way up), and the
  select's list holds the port's texture for level 18.
