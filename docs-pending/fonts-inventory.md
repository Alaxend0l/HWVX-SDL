# Font pages: inventory (v0.4.1 B1)

Belongs in: docs/plans/v0.4.1.md part B (findings), and a "Fonts" section of docs/xdl-format/README.md
(the IfaceFont record and the page layout).

Tool: `xdlrs fonts list [--gc DIR] [--pc DIR] [--ps2 DIR] [--dump DIR] [--report FILE.json] [--threshold 12]`
(tools/xdlrs/src/fonts.rs). It finds every interface in every pack (Port_IfaceScan.c's header test), follows
the fonts of texts (types 1, 12), boxes (10) and briefing characters (2), resolves each font's texture
relocation (share packs by id), decodes the page, and prints per glyph group: the distinct pixel sets, their
copies, font definitions, the packs and interface ids that draw them, sample texts. `--dump` writes one PNG
per pixel set (game data: never commit them). Not covered: `WorldGameData.debugFont` (ShowStats) and
State_RenderTest's `font36`, both debug only.

## Layout facts

* IfaceFont (0x2C): texture slot 0x00, cell w/h 0x09/0x0A, case mode 0x0B, type delay 0x14, size keys 0x18,
  spacing table 0x20/0x24 (0xC: char, x, y), spacing 0x28. Gfx_FontManager_AddFont lays the case mode's
  letters on a cell grid from t = 1.0; **pages are stored bottom row first** (decoded upright, the grid
  starts at the top-left). Case 0: 80 letters (digits, A-Z, a-z, symbols), case 1: 54 (no lower case).
  Language F/G strings: case 0 adds 9 accents after 6 blanks (cell 86+), case 1/3 swap them in place of
  English symbols.
* No English case-0 page has room or ink for the F/G accent cells (24x32 pages are full at 80); accents are
  a risk only for localised data.

## Results (retail GC + PC + PS2)

489 page locations, 36 distinct pixel sets, 10 glyph groups (pages paired across releases by the mix's
GlyphDistance < 12). Every group exists in all three releases at the same size; GC keeps two encodings
(RGBA8 in HW_IPack, CMPR in share/world packs), PC DXT5, PS2 PSMT8.

| G | size | cell, case, spacing | GC page(s) | drawn for | typeface | class |
|---|---|---|---|---|---|---|
| 0 | 64 | 10x8, 1, 10 | IPack 5:4, 6/7:120; every world_0 (14/15) | debug grid boxes (type 10) | pixel debug | flat white |
| 1 | 128 | 18x18, 1, 18 | HW_Cars share_2:9 | car packs' debug "X: Y: Z:" | rounded italic techno, unknown | outlined |
| 2 | 128 | 12x12, 0, 12 | share_1:10; IPack 3:20, 6/7:37, 8:24 | debug texts, boxes, "Rev. 9/14" | pixel debug; 20 spare cells hold button symbols | outlined |
| 3 | 128 | 16x16, 1, 16 (delay 0/50) | share_1:9; IPack 5:5, 6/7:119, 8:23 | memory card, results, pause rows, "00:00" | **Samba is Dead** | outlined |
| 4 | 256 | 32x30, 1, 32 (3 spacing tables) | share_2:11; IPack 1:6 .. ; worlds | titles ("VELOCITY", "Vehicle Select", ARMOR...), "Press START", pause | **Samba is Dead** | outlined |
| 5 | 256 | 32x30, 1, 27 | share_2:13; race*l1/l2 world_0 | car HUD "/", drag races | stencil, unknown | outlined |
| 6 | 256 | 24x32, 0, 24 | share_1:8; IPack 4:4 | legal screen, "HOT WHEELS", "MPH", "Demo Mode" | bold italic grotesque, unknown | outlined |
| 7 | 256 | 24x32, 0, 24 | share_1:6; IPack 6/7:123 | timers "00:00", "+0:05" | segmented (LCD-like), unknown | outlined |
| 8 | 256 | 24x40, 1, 15 | HW_brief 11/9; IPack 6/7:128, 9:25 | briefing dialogue, "NOTE TO PARENTS" | condensed gothic, unknown | outlined |
| 9 | 256 | 24x32, 0, 23 | share_1:7 (PC IPack 5:0, PS2 IPack 6/7:119) | "HOT WHEELS", "Wins", scores | **Serpentine Sans** | outlined |

"outlined": white fill with a baked dark outline/drop shadow (22-64 % of opaque pixels dark); the text
colour tints the fill and the outline stays dark, so a sharp page must bake the same outline. Only G0 is flat.
G1's GameCube page is the furthest from PC/PS2 (10.7/11.2, near the threshold): check by eye before pairing.
Full listing: run the tool (spacing tables, every copy and user).

## The two named typefaces (identified by eye against specimens; B2 confirms by rendering)

* **Samba is Dead** (G3, G4): Utopia (Dale Thorpe), 1998/99, `SAMBD___.TTF`. Source: dafont,
  https://dl.dafont.com/dl/?f=samba_is_dead (zip sha256 c3cb5cb6...7c8b, TTF sha256 b5a33fc9...aa86). Licence,
  its readme.txt verbatim: "this font is provided free for personal or commercial use, it can be
  redistributed however it may not be sold." So: pages built from it can ship, the TTF can be
  redistributed with a free port (not sold on its own). Matching glyphs: "01" digit forms, "<" as C, wide
  squared letters.
* **Serpentine Sans** (G9): Dick Jensen, Image Club (1995), now Monotype (trademark of Monotype Imaging);
  sold on MyFonts (6 styles). Commercial: no redistribution or embedding without a Monotype licence. Needs an
  approved open lookalike or a player-supplied font (plan question 7). Not downloaded; compared against the
  fontsinuse specimen (https://www.fontsinuse.com/typefaces/4476/serpentine-sans).

Side finding for part A: G2 (the 12x12 debug font, all releases) has 20 inked spare cells after its letters,
including PlayStation-style button symbols; no interface prints them.
