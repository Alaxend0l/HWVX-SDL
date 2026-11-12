# The PS2 prototype's content in the game (for docs/xdl-format/README.md §9.1, "As built")

Branch `protoimport` of HWVX-SDL. Goes under §9.1 as "#### 9.1.1 As built in HWVX-SDL"; the
ids.lock part also belongs in §10.5.1 (the `data` member) and §8.2 (entries with `"source":
"prototype"` sit beside the mods' entries).

**Modes.** The launcher's Games page has the modes on the prototype's row, once it is imported:
Off, Beta cars, Beta added, Beta replaces. The choice is saved in sources.cfg as
`proto=off|cars|added|replace`, and a line under the buttons says what the mode replaces, adds and
leaves as retail. `HWVX_PROTO` overrides the choice, and `HWVX_PROTO_DIR` overrides the
prototype's folder (by default it comes from `ps2beta=`).

| mode | cars | numbers |
|---|---|---|
| **cars** | Fandango, Sweet 16 II and Jester are added | 33, 34, 35 in a fresh ids.lock |
| **added** | all 36 are added: those 3, and the 33 others as "<name> (prototype)" | 33–68 |
| **replace** | the release's 33 cars load the prototype's versions under their own numbers, so saves keep their meaning; the 3 are added | 33, 34, 35 |

**Loading the prototype's packs** (`src/port/proto.c`, `dvd.c`, `DataGamePackLoad.c`, `Port_PS2.c`):

* A pack file whose name starts with `~` belongs to the prototype. `dvd.c` opens it from the
  prototype's folder under the PS2's file names, as `PORT_DATA_PS2`, with `DVDFileInfo.proto` set.
  Its packs are not cached in `.xdl`.
* The prototype's archives are version 177/179, and each TOC entry's offset counts from the end of
  that entry. Its packs have no sector padding and no 32-byte padding. Their `ref_relocs` and
  `pointers` come before the data, so they are read in file order into the places the loader keeps
  them.
* Texture headers: a level value m means m − 1 stored levels, and there is a format 0 (RGB24).
  Everything else is the PS2 release's (`PortPS2_Textures(..., proto)`).
* A prototype pack's references (`HW_share`, `HW_Cars`#2) get the prefix too. Its imports then
  resolve against the prototype's own shared packs, not the release's, so no index remapping is
  needed. When the mode is not off, the share pool grows from 3 to 5 slots.
* Fixed for every release: a pack that loads the packs it refers to while it is loading overwrote
  the static pack header. That cut the loop over the pack's relocations short and left the rest
  unrelocated. The release never got there, because its shared packs are loaded ahead.

**The cars** (`src/game/Port_Proto.c`):

* Of the prototype's 36 car packs, 33 are the release's cars under other numbers. They are paired
  by their skins' texture names, and every release car has exactly one: prototype car 7 is car 1
  (At-A-Tude), 28 is 25 (the bus), and so on (`s_cars`). Cars 1, 12 and 17 exist only in the
  prototype.
* Entries go into ids.lock as `uid://ps2proto-car-N`, with these members:
  * `"source": "prototype"`;
  * `"data": "proto://HW_Cars/car_N"`, the pack the car is loaded from;
  * `"alias"`: its twin, or car 0 for the three. The alias stands in online and in a game without
    the prototype;
  * `name` / `suffix`;
  * `thumbnail`: `proto://HW_IPack/interface_3/texture/<19+N>`, the prototype's own car-select
    pictures;
  * `"unlocked": true`.
* A mode that leaves an entry out sets it `"active": false`, so its number stays reserved for its
  saves, as with mods. New entries are numbered in a fixed order: the three first.
* `Port_ContentPackSource` (registry) gives the archive name and TOC id for a car: the
  `proto://` data link, or the twin in mode replace. `Port_ContentFixScripts` renumbers the
  scripts as it does for aliases.
* Once loaded, a prototype car gets the same changes `hwvx-x port-car` makes:
  * camera mode 1, which the release doesn't have, becomes the default chase camera (0) with a
    release car's values;
  * damage is set to 20 hits / 75%;
  * the turntable height (viewOffset y) is set to 2;
  * sound records of type 2 become type 1.
* The car keeps its own HUD template, which includes the prototype's x/y/z debug readout. It keeps
  its own stat bars and AI scripts.
* The pictures come from the prototype's `HW_IPack` interface_3, without its sounds. That pack is
  loaded outside the streaming pools the first time a select asks for one, and stays loaded for the
  run.

**Tested** on GameCube data with the prototype from sources.cfg:

* Car select in each mode: names, pictures and the turntable model. In mode added, "At-A-Tude
  (prototype)™" shows next to the release's.
* Joyride and the ch1_l2 challenge raced with Fandango, Sweet 16 II and Jester, and with car 1 in
  mode replace (`tools/proto_race.sh`).
* ids.lock through cars → added → off → cars: every car kept its number.
* The attract demo is frame-identical to pc-data with no mode set.
* ctest passes. `test_content` now expects a suffix before a trailing `*`.

**Not done (large):**

* **Worlds and tracks.** The registry side is ready: a registered track with its own `world`
  (§10.5.1). The prototype's level archives can't be loaded as they are:
  * its world packs have another layout (quadrant records are 0x40 bytes against the release's
    0x50, `docs/beta-worlds`; the rest of the world graph has not been compared);
  * its quadrants are packs inside each level's archive, where the release keeps them in
    `w*_geo`, `w*_tex` and `w*_col`; the prototype has no such archives.
  * Two ways forward:
    * (a) a v179→v183 world-pack converter, plus quadrant lookup in the level's own archive in
      `FileStreaming_LoadQuadPacks`;
    * (b) the `hwvx-x port-world` approach: the release's world pack as the host, with the
      prototype's quadrants, world textures and collision. This needs a release level with the
      same quadrant slots (only ch1_l2 has been done).
  * Either way it then becomes a registered track whose `world` is `~worlds_\sp_\...`.
* **Audio.** The briefing dialogue (`AUDIO_/HWB_M01–14`: 174 sounds against the release's 233) can
  only replace sounds one by one, through the pairing table. The music (`HW_MUSAK`, `HW_IT_PC`)
  is IMPM (Impulse Tracker) modules, and the port has no player for them.
* **Mode replace beyond cars** (textures and sounds of worlds and menus through the pairing table,
  as a mixed build does). Port_Mix's per-release arrays are `PORT_DATA_COUNT` (3) long, so the
  prototype has to become a fourth source there:
  * `Port_MixRoot`;
  * `RelPack` reading the prototype's layout (TOC offsets, section order, its texture headers);
  * a `proto` column from `tools/gen_mix_pairs.py`;
  * a recipe rule.
  * Cars are better done whole, as here. The pairing table pairs packs by id, and in `HW_Cars` the
    same id is a different car in the prototype.

**Doubtful:**

* Prototype cars look dark in races (they are lit correctly on the turntable). Not investigated:
  it may be its geometry's vertex colours or env map.
* Mode replace keeps the release's car-select pictures for the replaced cars.
