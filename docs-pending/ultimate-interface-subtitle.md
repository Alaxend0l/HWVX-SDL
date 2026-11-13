# Silver set: the PS2's "MAXIMUM JUSTICE" subtitle (for docs/xdl-format/ultimate-interface.md)

Add to the silver set's section (after "Which PS2 copy").

The GameCube's title interfaces have the PS2's subtitle too: HW_IPack interface_1 (the title,
header 0x2B3440, objects 23-27), interface_2 and interface_8 (5 objects each) hold the two
"MAXIMUM JUSTICE" images (texture 10 on both releases), their two flares (texture 9) and the TM
text, with the same animations on both releases. Each is shown only while the interface's
variable 0, the code variable 0x258 (GameSettings.c: GameCube 2, PS2 1, PC 0), is 1: the PS2.

For the silver set (Port_Mix.c, ShowPS2Art), an image whose only condition is "code variable
0x258 == 1", and an object so conditioned that is anchored to one, gets the base's platform value
in its condition (2 on a GameCube base), so it shows as on the PS2. Text so conditioned is left
alone: the PS2's "PRESS START BUTTON" wording stays the GameCube's "PRESS START". 15 objects in
the retail data, all of them the subtitle's. The yellow set is untouched (manifest and packs
byte-identical).

## The press-start line (both sets, mixed builds only)

Recipe field `"press_start"` (recipes/ultimate.jsonc: `"Press START/ENTER"`; absent or "": the
release's own). Port_Mix.c (MixPressStart) points the shown string (strings[0]) of every text
object of role 9 whose string starts "Press START" at the recipe's text, in a block the pack keeps
and the cache records; font, colour and animation stay. In the retail data that is HW_IPack
interface_1's title (one string table, shared by its two press-start objects); interface_2 and _8
have no press-start line. The title font has '/' (its spacing table lists it) and draws the text
in capitals ("PRESS START/ENTER"), centred like the original (align 0 at x 320).

Silver also takes the PS2's place for the line: of the title's two press-start objects, the PS2's
("show if 0x258 == 1", y 392, below the subtitle) is shown and the GameCube's ("hide if 0x258 ==
1", y 370) hidden (ShowPS2Art). Yellow keeps the GameCube's line at y 370. Plain GameCube, PC and
PS2 games are unchanged (nothing of this runs without a recipe).

Checked at 1280x720 against the plain PS2 (silver) and the plain GameCube (yellow), title frames
600-1200: identical but for the press-start row, which reads PRESS START/ENTER where they read
PRESS START BUTTON and PRESS START, at the same height and centre.
