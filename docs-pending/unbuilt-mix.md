# A mixed game played unbuilt: the same interface textures as a build

Belongs in: docs/xdl-format/ultimate-interface.md, "What counts as interface art" (replaces the
sentence "Playing a mixed game that was never built ... those 23 follow the "best" rule there"),
and HWVX-SDL README (env switches: HWVX_MIX_MANIFEST).

* The builder's first pass (the share packs' textures other packs' interfaces show: HW_Cars
  share_2's gauges, HW_share share_1's fonts and frames, the HUD arrow's geometry slot; 24 entries
  on retail) is kept in the cache folder as `uirefs.txt` ("stamp=", "xdl=" as built.cfg, "count=",
  then `ref ARCHIVE PACKID INDEX WHAT` lines). `--mix-build` writes it with the build.
* `--mix` (and the launcher's Play of the mixed game) reads it before the first pack loads. When
  it is missing or of another stamp / cache version, the game runs `hwvx --mix-scan RECIPE`, the
  first pass alone, in a process of its own (the pass loads every pack and leaves share packs
  loaded, which the game must not keep), waits for it (~0.7-0.8 s once) and reads the list. If that
  fails the game plays on with the old "best" rule for those textures (logged).
* Why not require a build: the scan costs under a second, once per recipe/release/cache change,
  and runs the builder's own code, so the list is the build's exactly; requiring a build would drop
  the "playing it mixes each pack as it first loads" option for a check this cheap.
* Caches from before: when the list had to be made and the folder has no built.cfg of this build,
  the share archives the list names (hw_cars.xdl, hw_share.xdl) are deleted and mix again as they
  load (an unbuilt game's cache had them mixed without the list, under the same stamp).
* `HWVX_MIX_MANIFEST=1`: a game played unbuilt appends what it mixes to
  `<cache>/manifest-play.jsonl` (the build's manifest format), for `xdlrs mix ui-check` and for
  comparing with a build's manifest.jsonl.
* sources.cfg `interface=`: one reading for the launcher and mix.c (Port_MixInterfaceSetting):
  spaces and case ignored, "ps2" silver, anything else yellow (mix.c logs a value that is neither).

Checked (retail GC+PC+PS2, base GameCube, ch1_l3 challenge race): played unbuilt, every line of
manifest-play.jsonl is in the build's manifest.jsonl, both sets (silver 243 lines, yellow too);
ui-check ok. With an empty list (the old behaviour) 14 share textures differ from the build
(HW_Cars share_2 5,6,8,9,10,11,13, HW_share share_1 6..10,49,67 from the PC/PS2 by "best"): the
HUD's gauge frames gold and the target arrow yellow in the silver set; with the list all silver.
An old unbuilt cache is repaired on the next start (hw_cars.xdl, hw_share.xdl mixed again; then
identical to the build).
