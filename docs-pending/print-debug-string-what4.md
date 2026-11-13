# print_debug_string `what` 4: the message line, not the objective line

For docs/xdl-format/README.md §10.6 (the `strings` row of the level.jsonc table) and
docs/level-editor.md "Texts". Both say `what` 4 is "the objective line of the pause, win and lose
screens"; that is wrong.

Replace the `strings` row's note with:

> named texts (or lists of paragraphs): the game's string tables, for print_debug_string. `what`
> 4 (7, 8: players 2, 3) sets player 1's message (InterfaceManager_SetPlayerMessage); the next
> interface up with a text of role 65 (66, 67) takes it: the challenge and joyride briefing's
> bottom line (big timer font, centred at 320,396, under the box), the win/lose screens' lines,
> "NEW GADGET UNLOCKED!!!". Retail levels print "     " (blank) as they start and set short
> end texts later ("CHECKPOINT!!!!", "Out of Time"). The briefing box's text and the pause
> screen's objective line are an objective's (`objectives`, role 0x34), not a message.

And in level-editor.md "Texts": "Triggers show texts with `print_debug_string` (what 4: the
message line under the challenge and joyride briefing and on the win and lose screens). Keep them
short, and print a blank text as the level starts, as retail does; the briefing's own text is an
objective's."

`xdlrs level check/compile` now warns about a level_start trigger whose print_debug_string
(what 4/7/8) shows a non-blank level://strings text (the bug the user saw: a sentence drawn under
the challenge briefing in the timer font, off the screen at any aspect).
