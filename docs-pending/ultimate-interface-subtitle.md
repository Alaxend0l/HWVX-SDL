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

Checked against the plain PS2 at 1280x720 (title frames 600-1200): identical but for the
press-start text, which on the GameCube sits where the PS2 moved its own line down: from about
frame 780 to 1080 "PRESS START" overlaps the subtitle's lower edge (the PS2 draws "PRESS START
BUTTON" below it). Moving the GameCube's line is a layout question, not done.
