# Button prompt glyphs

The glyphs the port draws in place of a release's button atlas cells (confirm, back, details;
src/game/Port_Prompts.c), one look per device style. From **"The Meritite Union's Input Prompts"
2.0.0** by hergergy, https://github.com/meritite-union/input-prompts, licensed **CC0 1.0 Universal**
(public domain dedication: no attribution needed; credited here anyway). The files in `src/` are
the pack's `png_256` files, copied unchanged and renamed; nothing of the game is in here.

Look: theme `dark` (for dark backgrounds), `color` where the pack has it (Xbox, PlayStation),
fill `solid` (a filled shape with the label cut out). Other looks (`border`, `flat`, monochrome)
are a different set of the same paths: `xdlrs prompts sheet PNG256_DIR OUT.png` shows them.

`prompts.bin` is generated: `xdlrs prompts build assets/prompts/src assets/prompts/prompts.bin`
(each glyph box-filtered to 128 px, the cell size of the port's 512 x 512 replacement atlases);
`xdlrs prompts check assets/prompts/src assets/prompts/prompts.bin` (also a cargo test) checks
it and the sha256 column. CMakeLists.txt embeds it in the game.

| file | pack path (`input-prompts-2.0.0/png_256/`) | sha256 |
|---|---|---|
| `xbox_a.png` | `xbox/dark/button/primary/color/solid/a.png` | `ee4184cd13148f075d59cafea99b7946c8a49b16f5c0b20bdee2fdb1a9b57877` |
| `xbox_b.png` | `xbox/dark/button/primary/color/solid/b.png` | `3e00a670e08eb1233165059df0f60e5a7b31ae74cc6d62afd2842b4012c18c13` |
| `xbox_x.png` | `xbox/dark/button/primary/color/solid/x.png` | `63f5df6e93dca716e5ac0030a87004fb85e963d6a3f97765c71ac833ba59b504` |
| `xbox_y.png` | `xbox/dark/button/primary/color/solid/y.png` | `74cd39fff3e0eba51f6b2f0d5444f58544de0d1f13ea9c56b8dff7ae33e3ba4e` |
| `ps_cross.png` | `ps/dark/button/primary/color/solid/cross.png` | `2f8b5a729b40cc78d93d0aa59caebf704facb1c4e066a6c1f7923e2e3aec19b4` |
| `ps_circle.png` | `ps/dark/button/primary/color/solid/circle.png` | `b5aa393f1a41448521b1804e910b83bb483e889b77c64cecf1bfc493e1818db1` |
| `ps_square.png` | `ps/dark/button/primary/color/solid/square.png` | `5b76916bdd2022b26f159d424f5cbd28e6692b85996be6f1ef63367f4d192e66` |
| `ps_triangle.png` | `ps/dark/button/primary/color/solid/triangle.png` | `c0c3c0d3f7eb6bee8dbd2893b31dbbba49aa7afb9e9208ea09e17bfeb57e2011` |
| `switch_a.png` | `ns/dark/button/primary/_/solid/a.png` | `87660af631b54b0e43ff0fb69191b223418909383fbda5517b6ff7de920c3dc6` |
| `switch_b.png` | `ns/dark/button/primary/_/solid/b.png` | `a56fc084df4482079f90d952df2826536bfa1475163c39b7a4c748edf8176f7c` |
| `switch_x.png` | `ns/dark/button/primary/_/solid/x.png` | `370fb2c26152ee5cad3a9e7792a521886f17a5a7a1137db3b2dc686ad352cd4e` |
| `switch_y.png` | `ns/dark/button/primary/_/solid/y.png` | `67f7a030173cf09c6b4a890981011d28a0070d32eaf15b497c8a728dbd85f0d2` |
| `key_enter.png` | `pc/dark/key/special/_/solid/enter.png` | `ea3fcbc5a2750aefbd67342d15cfa4ed9aede13a75384786d7b2b2cbd161defd` |
| `key_escape.png` | `pc/dark/key/special/_/solid/escape.png` | `2d91d0381e66ed32a2da1f7cee43a3fdfd0fe46de9a48610c9fa6ed0ecd03610` |
| `key_x.png` | `pc/dark/key/alphanum/letter/solid/x.png` | `d8c86d5533f0c282f676124c899399f75a39de383b04d52a2a33f68ea4286929` |
