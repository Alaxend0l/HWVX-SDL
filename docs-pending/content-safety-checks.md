# Content safety: the two "not done" items (for the superproject's content-safety doc)

Replaces the "Not done (reported)" paragraph of the content-safety section (docs-pending/content-safety.md
as folded in by 214abdb):

- **Interface pack names.** A load/play interface action's pack name is kept as a game-owned copy
  (`Port_LevelPackName`, Port_Level.c; ActionLoadInterfacePack, ActionPlayInterfacePack), not a pointer
  into the trigger's parameters. The streaming slot and `g_InterfacePackData` keep that pointer, and a
  shared level's parameters are freed when it is taken out. In the normal flow the level goes in
  World_Unload together with every interface pack slot, so no reader was found; the window is an
  interface a level started that runs on past its world (nothing releases interfaces on leaving play),
  whose entry a later world's interface_pack_end / end_interface_pack compares. Same name, same copy:
  FileStreaming_GetPackID's pointer test holds; retail behaviour unchanged.
- **Code variable tables.** GameSettings_Get/SetCodeVariable bound every table they index: 0x6A (cheats
  0-10), 0x6B / 0x6C and GameSettings_IsCheatEnabled (0-11), 0xC9 Get (1-10, as its Set), 0xCB / 0xCC
  (1-4; online ports 5-8 as before), and arg 0 of 0xCE, 0xD1, 0xD2, 0x191-0x194 (indexed [-1]). Out of
  range: the Get returns 0, the Set does nothing, logged once per variable (`codevar: ...`). Every index
  inside a table as retail. The retail attract demo asks `get 0x193` with port -1 (retail's own check
  already gave 0): logged, unchanged.

Tests: `test_ifacename` (load from a freed parameter list, then lookups; a heap-use-after-free under
-fsanitize=address before the fix), `test_codevars` (in-range values read back, out-of-range ones write
nothing and read 0). Both in ctest.
