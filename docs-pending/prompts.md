# Button prompts that match the device (0.4.1 A2-A4)

For HWVX-SDL/README.md (settings and env switches), docs/xdl-format/README.md §6 Menus (a note
that interfaces' button atlas cells are swapped at draw time), and docs/plans/v0.4.1.md §A
(stages A2-A4 done). Builds on docs-pending/prompts-inventory.md (A1).

## Setting

- Launcher, Settings → Controls: **Button prompts**: Auto (default) / Original / Xbox /
  PlayStation / Switch / Keyboard. graphics.cfg `prompts=auto|original|xbox|ps|switch|keyboard`;
  `--prompts NAME` or `HWVX_PROMPTS=NAME` win for one run (`playstation` is accepted for `ps`).
  Tests that compare frames pin `HWVX_PROMPTS=original`.
- Auto: per port, the device last pressed (a button or a trigger past half way; sticks don't
  count). Port 1 shares the keyboard and the first pad; before any press it is the first pad
  if one is connected, else the keyboard. A pad's style comes from its type (PS3/4/5 →
  PlayStation, Switch Pro/Joy-Cons → Switch, GameCube adapter → Original, anything else → Xbox).
- The glyph of each meaning is the device's label (`SDL_GetGamepadButtonLabel`) of the button
  pad.c reads for it: confirm = SOUTH (GC A), back = EAST (B), details = WEST (X). So a Switch
  pad shows B for confirm and A for back, as on its own buttons. A forced style with another
  device uses the style's own layout (Xbox A/B/X, PS cross/circle/square, Switch B/A/Y,
  keyboard Enter/Esc/X).
- Split screen: per player. Each interface uses its own pad (`Interface.input`); interfaces
  read by all pads (the main menus) follow the device last pressed anywhere.

## Code

- `src/port/pad.c`: device tracking, `Port_PromptStyle(port, glyphs[3])`, the names.
- `src/game/Port_Prompts.c`: recognition and swap.
  - At `DataPack_RefreshTextures` every 128×128 texture is decoded (gx_tex.c's decoder) and given
    a 128-bit perceptual hash (8×8 blocks: alpha above the mean, alpha-weighted luma above the
    mean). Within 12 bits of a release's atlas it is noted for its slot (and forgotten when the
    slot is unloaded, `Gfx_UnLoadTexture`). `xdlrs prompts inventory` prints each atlas's
    phash and the margin: copies within 2 bits (GC RGBA8 vs CMPR), every other 128×128 texture
    of the three releases at least 31 bits away. Being perceptual it also takes a mixed
    build's re-encoded copies. Nothing of the game is in the source (three 128-bit hashes).
  - `InterfaceManager_DrawObject_Standard_Image`: `rect->tex = Port_PromptTexture(obj->texture)`.
    The port is set around each `InterfaceManager_RenderInterface` call (top level and child
    interfaces; `input` -2 "none" inherits the parent's).
  - Replacements: 512×512 GC RGBA8 (format 6), built on first draw per (atlas, glyph triple):
    the original decoded and scaled 4× (nearest), the three shown cells replaced by the glyphs
    (flipped: the atlases are stored bottom-up). Their texture objects are 16 of the port's own
    past the game's (`m_gxTexObjArray[PORT_MAX_TEXTURES + n]`, `PORT_PROMPT_TEXTURES`), so the
    game's texture slots and their allocation stay as they were; a ring of 16, the oldest
    rebuilt (GX_TexInvalidate) when full.
- Original (or a GameCube adapter in Auto) draws the release's own atlas: nothing changes.

## Assets (A2)

`assets/prompts/`: 15 PNGs from "The Meritite Union's Input Prompts" 2.0.0 (CC0 1.0,
https://github.com/meritite-union/input-prompts), `png_256`, look dark / colour / solid,
README.md with each file's pack path and sha256. `xdlrs prompts build assets/prompts/src
assets/prompts/prompts.bin` makes the table (box-filtered to 128 px, 983 KB);
`xdlrs prompts check` (also the cargo test `prompts_art::tests::assets_match_bin`) checks both.
CMakeLists.txt embeds prompts.bin as `g_promptsBin` (build/gen/prompts_bin.h), like the
ultimate recipe. `thirdparty/` (the unpacked pack) is git-ignored.
`xdlrs prompts sheet PNG256_DIR OUT.png` draws the contact sheet of the looks;
`xdlrs prompts montage OUT.png COLS PNG...` puts screenshots in a grid.

## Not done / open

- The controller page's GameCube pad picture stays for every style (question 2 of the plan).
- A5 (launcher N-choice row) came for free: the row is in (Controls). A6 not started.
- The GC adapter's B/X mapping (A0) is unchanged; its labels come from SDL as they are.
