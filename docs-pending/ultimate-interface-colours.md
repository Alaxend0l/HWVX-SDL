# Silver set: the PS2's interface colours (for docs/xdl-format/ultimate-interface.md)

Replaces the section "Open: interface colours" of docs/xdl-format/ultimate-interface.md.

## Interface colours (silver set only)

The packs stay the GameCube's (gameplay data, scripts, the menus' button prompts). For the silver
set, as each pack is mixed (Port_Mix.c, MixIfaceColours), its interfaces are paired with the PS2
copy of the same pack and the PS2's *look* is written into the base's interface data:

* Pairing: every interface header of both packs (Port_IfaceScan's header test; the PS2 blob read
  unrelocated with its own pointer list) by a longest common subsequence of their shapes (object
  count and the object types in order). Headers left in a gap with as many on both sides pair
  object by object (a longest common subsequence of (type, role, id)) when all but three of the
  smaller's objects align: the car HUD (GameCube 44 objects, PS2 45: one image more) and its
  second panel (38 / 37).
* Taken per paired object: the colour key tracks of the default animation and of each state
  (states paired by place, else by state type), RGBA only; key durations and counts stay the
  base's. A track whose key count differs takes, for each base key, the PS2 key whose time span
  holds that key's start (the GameCube's 2-3 key pulses become the PS2's one colour). Also an
  image's blend mode (IfaceImageDef +1: the PS2 draws its empty meters with mode 3 where the
  GameCube uses 2), a text's blend (+0xE), briefing characters' text colours, polygon points.
* Geometry an interface draws (image type 1: the HUD's 3D target arrow, HW_Cars share_2 geometry
  1, referenced from every car pack) has its groups' textures counted as interface art
  (PORT_IFACE_GEOMTEX; the builder's first pass records the cross-pack geometry slot). The arrow's
  flat colour textures (share_2 21 red, 22 yellow; the PS2's 21, 22 are blues) pair with nothing
  in the pairing table: an opaque single texture takes the set's single texture in the same place
  and of the same size.
* The yellow set is untouched: none of this runs, geometry textures stay with the "best" rule
  (checked: a yellow build's manifest and packs are byte-identical to the build before; one byte
  of hwb_m10.xdl, an audio pack, differs between any two builds, also with the old program).

Counts (retail GC+PC+PS2, base GameCube): 113 packs, 2,114 interfaces paired (128 object by
object), 2 not (one each in HW_IPack interface_6/_7; interface_4's single header has another
shape: PS2 pause/controller screens); 60,919 colour tracks, 4,299 keys changed, 423 tracks with
other key counts taken by time, 360 skipped (no PS2 state of that type); 200 blend modes; 687
briefing colours; 0 polygon points (none in the paired interfaces).

Checked side by side with the plain PS2 release at 1280x720 (tools/mix_pair_shots.sh): main menu,
challenge HUD (ch1_l3), battle HUD (bat1_l1), mission HUD (world1l1). The launcher's
MIX_SILVER_READY is 1: the Silver button shows.

## Tools (replace the Python ones in ultimate-interface.md)

* `xdlrs mix ui-check MANIFEST --set gc|ps2 [--base gc] [--list]` (was tools/mix_ui_check.py,
  removed): the interface textures of a build's manifest by release, the fallbacks, exit 1 when
  one breaks the set rule.
* `tools/mix_pair_shots.sh BUILD MIXPREF REFDATA GCDATA OUTDIR NAME SECONDS FRAMES [VAR=value...]`:
  the same scene in the mixed build and a plain release, joined side by side with
  `xdlrs mix pair LEFT.bmp RIGHT.bmp OUT.png [--half]`.
