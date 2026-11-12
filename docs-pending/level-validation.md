# Level files are checked before they're applied

For docs-pending/mission-editor.md (the level file section) and docs/xdl-format/README.md
§10.6 (levels): a level file comes from shared `.xmod` mods, so the port treats it as untrusted
input. `src/game/Port_Level.c` refuses a whole level, logging `level: not applied: <why>`, when
anything in it is out of bounds; nothing of it stays applied.

What a level may do:
- **parts**: hex exactly 2 digits per byte, at most 1 MB each, at most 65536 parts. Triggers are
  0x2C bytes, timers and variables 0x10, volumes 0x30, elements 8 (objectives only).
- **triggers** (the level's and the base ones it keeps, checked after the patches): trigger and
  condition types below 28, action types below 44; the conditions/actions arrays fit the block
  they're in; every pointer slot (trigger 0x04, 0x0C, 0x14, 0x18; a condition's or action's
  0x04) is zero, one of the level's links, or one of the world's own pointers.
- **timers**: a linked mode. **variables**: an id no world variable or other level variable has;
  at most 30 world variables in all (an interface keeps 30). A variable without a mode gets one.
- **volumes**: type below 7, shape 0 (BSP, with its BSP) or 1 (sphere); fog and light volumes
  have their data.
- **appends**: only World tileCars (96-byte elements) and objectives (8), and a quadrant's
  items and fxPoints (64), once each per array, at most 4096 elements; copies only of an element
  of the same kind; `set` bytes never over the element's pointers.
- **patches**: numbers in the base world's records only (the pack's blob, or a base element an
  append copied), at most 4096 bytes, never over a pointer (the pack's pointer list, or the
  pointer fields of tile cars, items, effect points, objectives).

Not checked yet: the contents of parameter lists (which words are handles depends on the
trigger or action type) and patches over fields of other base record types that the game uses
as indices. The compiler (tools/xdl_level.py) only writes valid ones.

Test: `tools/level_refuse_test.py BUILD DATA OUT` (19 crafted levels refused, the samples
applied; one game at a time).
