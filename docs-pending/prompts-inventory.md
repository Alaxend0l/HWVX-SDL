# Button prompts: inventory (0.4.1 plan A1)

For docs/plans/v0.4.1.md §A ("Where the glyphs are", "Meaning") and, once A4 lands, the xdl-format
README's interface section (§6 Menus). Made with `xdlrs prompts inventory` (tools/xdlrs/src/prompts.rs):

    xdlrs prompts inventory GC_DIR PC_DIR PS2_DIR [--png DIR] [--all] [--seed KIND:REL:FILE/PACK:INDEX]...

It reads the archives as they are on the disc, finds every interface header as Port_IfaceScan.c does
(every pointer-slot target that fits an InterfaceHeader), names textures through the packs' texture
relocations (`{ slot, packId << 16 | index }`; packId != 0 is the share pack the header's refs name),
and classes a texture as atlas/pad when its decoded level 0 is within a mean distance of 24/255 of a
seed of the same release (the seeds are HW_IPack textures, below). `--all` lists every texture an
interface shows (texture keys spelled out), `--png` writes each class's texture and every cell used.
About 4 s for all three releases.

## Hash table

Hash: h64 of (u32 w, u32 h, RGBA of level 0 as decoded by modcheck::media). Seeds marked *.

| class | release | format | size | hash | copies |
|---|---|---|---|---|---|
| atlas | GC | RGBA8 | 128×128 | `cfda21a8cfb3884f` | 17: HW_IPack interface_3:24*, _5:3, _6:41, _7:41, _8:25; worlds ch5_*, bat5, bat8, race5*, world5* (world_0:11) |
| atlas | GC | CMPR | 128×128 | `5270ed11d5a166f3` | 45: every other world (world_0:11, or :10 in bat3/4/6, race6*, world4*) |
| atlas (keycaps) | PC | DXT5 | 128×128 | `f18ffbb9c378c455` | 55: HW_IPack interface_3:24* and the others, every world (world_0:12 / :11) |
| atlas (glyphs) | PS2 | PSMT8 | 128×128 | `e8d1e50a19f84842` | 62: HW_IPack interface_3:24* and the others, every world (world_0:11 / :10) |
| pad | GC | RGBA8 | 128×128 | `f6dc16ac7761f52d` | 14: HW_IPack interface_6:124*, _7:124; the RGBA8 worlds (world_0:12) |
| pad | GC | CMPR | 128×128 | `d04588dfd5f19ec2` | 45: the other worlds (world_0:12 / :11) |
| pad | PS2 | PSMT8 | 128×128 | `387b94aa6da2f240` | 59: HW_IPack interface_6:125*, _7:125, every world (world_0:12) |
| pad overlay | PS2 | PSMT8 | 128×128 | `49ecf7c612d1caa0` | 59: HW_IPack interface_6:126*, _7:126, every world (world_0:13) |

One design per release; the copies differ only by format (GC: RGBA8 in HW_IPack and some worlds,
CMPR in the rest, distance 2.8). The PC has no pad picture: its controller page is the keyboard screen
(keycap images from HW_IPack interface_6:3, 256×256, and a 16×16 cursor, :124).

## Cells

All atlases are 4×4 cells of 32 px. Textures are stored bottom-up (the draw flips V: rect top uses
`uv[7]`), so a cell's pixels read upside down in a straight dump; cell positions below are in
texture pixels as the UV keys give them (u0,v0)-(u3,v3).

| cell | GC | PC | PS2 | meaning (pad.c / Port_Keyboard.c) |
|---|---|---|---|---|
| (32,64) | A (green) | Enter | ✕ cross | confirm |
| (32,96) | B (red) | Esc | △ triangle | back |
| (0,64) | X (kidney) | C | ○ circle | details |
| (0,96) | B (red, unused) | Z | □ square | unused |

So the PS2 itself shows triangle for back and circle for X-details (not circle for back), and the
PC shows keycaps in the same cells (Enter / Esc / C); C, not X, is the PC's details key in its art.
Every interface of every release uses only these three cells (counts of objects: GC 92/45/10,
PC 67/42/9, PS2 88/45/10). The other cells (L, R, Z, Start, d-pad, sticks; PC PgUp/PgDn/arrows;
PS2 L1/R1, select, start, d-pad, stick) are never shown by an interface.

Pad pictures: GC obj 14 of the controller page, UV (1,0)-(127,128), 130×180 at 320,212. PS2:
obj 14 the pad (0,0)-(128,128), 150×190, obj 15 the overlay on it, and texture key animations
obj 16 (9 keys) and 17 (8 keys) picked by variable 0 (the configuration), all keys naming the same
two textures per the relocations.

## Where they are shown (all three releases alike)

102 interface kinds (2031 interfaces counting every world's copy). Owners (header id 0 unless said):

- HW_IPack: memory card screens (interface_5 root 0, "Select"/"Continue"/"Retry"), the controller
  page (GC/PS2 pad), main-menu children (obj 77 of id 3 / id 0, obj 5 of id 8, obj 70 of id 0),
  quit and restart confirmations.
- Every world pack: the pause menu (world game data 0xCC) and its children (obj 36/37), restart and
  quit confirmations, the controller page copy, and the in-level briefings (below), opened by
  trigger actions (type 4 InterfacePlay) or ended by InterfaceEnd triggers.
- No car pack (HUD) and no HW_brief interface shows a prompt; the car packs' end screens say
  "Press 'Fire' button to continue" in text only.

Most prompts are a pair: an image (A at 290,450 or 270,440; B at 235,440 / 165,440; X at 290–370,440)
and an object with a two-key texture animation of the same atlas cell picked by an interface
variable (`texanim[0/2]`, `[1/2]`, "by variable N"), anchored at the image. Both keys name the same
atlas, so a swap at draw time (A4) covers them. Header mode 0 (all pads) or 1 (pad 0): the player
to use for a per-player style is the interface's owner/mode, as the plan says.

## "(A) CONTINUE"

The in-level briefing / goal screens of the world packs (challenge, battle, race intro: "BEGIN!",
goal texts): an image of the A cell (obj 13 at 270,440, or obj 37 / obj 63 at 320,450, 32×32) and a
text object "Continue" anchored at it (obj 14 / 38 / 64; the capitals presumably from the font's case mode, not checked).
39 + 24 + 14 copies per release, identical on GC, PC and PS2 (PC: Enter keycap, PS2: cross). Opened
by trigger actions (InterfacePlay) or InterfaceEnd triggers of each world. It is not in HW_brief.
