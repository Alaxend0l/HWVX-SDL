# As built: the world auto-chunker (`tools/xdl_chunk.py`)

For docs/worlds/README.md, a new section after "Chunker algorithm" (before "Risks"); the
format facts at the end also belong in docs/xdl-format/README.md §10.8 (packs) and the
"Collision packs" section here. Branch `chunker` of HWVX-SDL.

```
xdl_chunk.py extract LEVEL PROJECT          a retail level as a project, from the player's data
xdl_chunk.py arena PROJECT                  a generated test world (ground, walls, ramps, pillars)
xdl_chunk.py plan PROJECT                   quadrants, neighbours, budgets -> PROJECT/plan.json
xdl_chunk.py build PROJECT OUT [--base-collision]   hwvx-x extract folders + ids.lock
xdl_chunk.py stage PROJECT OUT DATA STAGED  `hwvx-x pack` into a data folder linking DATA's files
xdl_chunk.py stats PROJECT [--retail LEVEL] the plan next to a retail level's quadrants
tools/chunk_drive.sh BUILD STAGED OUT NAME LEVEL [SECONDS]   drive it as a registered track
tools/test_xdl_chunk.py [--retail [LEVEL]]  pieces on generated geometry; arena; retail collision
```

**The project** (`world.jsonc`): `set` (the archives `worlds_\geo_\<set>_geo`,
`worlds_\col_\<set>_col`), `level` (the level archive), `base` (the retail level whose world pack
the new one is made from: scripts, volumes, nav, records, and its collision pack's materials),
`textures` (the world texture pack, default the base's: textures are links,
`base://w1_tex/world_texture_0/texture/N`, `base://hw_share/share_1/texture/N`), `render` (a
folder of glTFs: xdl geometry glTFs as `xdl_gltf.py` writes them, or plain glTF, one geometry per
mesh node, a group per primitive, unlit and vertex coloured, `extras.hub` or a `hub*` name for
the hub), `collision` (a glTF: triangles with `extras.material`; LINES with `extras.creases`, else
the creases are computed), `objects` (`base`: the base's objects placed by position; `none`),
`hub` (`base`: the base level's hub geometry, sky and backdrop, read from the player's data at
build time), `start` (start tiles moved: x, y, z, yaw), `track` (an id and alias: `build` writes
an `ids.lock` registering the level as a track, §10.5.1), `chunk` (budgets; `grid: "base"` keeps
the base's collision grid). Nothing in a project is game data except what `extract` writes from
the player's own copy (never committed).

**What `build` does** (the algorithm above, as far as it goes):

* **Quadrants are strips**, not a grid: slabs across the whole world along its long axis, cut
  where the next geometry (by box centre) would exceed the per-quadrant budget (5,000 triangles,
  8,000 vertices, 250 KB, 60 textures); the budget is lowered until every active set (a strip,
  two each side, the hub) is within 30,000 triangles and 1.2 MB. A strip touches only its two
  neighbours, so the neighbour list is the two on each side: at most 4, the 10-slot limit holds,
  and no "split differently" step is needed. A geometry reaching past the strips next to its own
  is cut by triangle centroids (the first piece keeps its id).
* **Regions** are boxes (6-node convex chains): the strip grown by 5% (at least 10) on its inner
  walls for hysteresis, the outer strips and the other two axes reaching 20,000 past the world, from
  1,000 below the lowest point to 5,000 above the highest. So every camera is in a region.
* **Objects** go to the first region holding them (the game's rule); the base's hub objects stay
  in the hub. The world pack keeps all its bytes; a new quadrant table, regions, neighbour ids
  and contents are appended, every pointer (and local ref relocation) into a moved object entry
  follows it, and the names of the quad and collision archives are new strings.
* **Quadrant packs**: header (word 0 the geometry table), the table (id, 0, geometry; the third
  word a local ref relocation), the geometries 32-aligned, encoded by `xdl_gltf.encode`, with
  their texture and shader relocations (a group's shader relocation sits at the group record).
* **Collision**: squares of about 250 x 200 x 250 over the mesh; per square the polygons
  clipped to it, in the source's order (the first is the splitter), built with
  `bsp_build.build` (tolerance 1e-4); the crease edges listed in every cell whose box, grown by
  10, the edge crosses; the base pack's materials and everything they use (effect geometry,
  sounds, textures) moved behind the new grid.

**Round trip of a retail world** (GameCube `world1`, Joyride 1): 652 geometries extracted,
re-chunked into 22 strips + hub (retail: 20 + hub), built, staged, started as a registered
track (`HWVX_TEST_EXTRA_TRACKS=alias=0`, `HWVX_TEST_EXTRA_TRACK_WORLD=worlds_\sp_\joyride_\cworld1`,
`HWVX_START=race:mode=joyride,world=5`):

| | retail world1 | re-chunked |
|---|---|---|
| quadrants | 20 + hub | 22 + hub |
| triangles per quadrant (max) | 5,797 | 4,983 |
| pack bytes (max) | 232 KB | 207 KB |
| active set triangles (max) | 28,064 | 25,129 |
| neighbours | <= 4 | <= 4 (2 for the end strips) |
| collision cells, nodes per cell (median / max) | 472, 40 / 3,019 | base grid: 472, 44 / 1,209; own grid: 1,512, 22 / 980 |
| collision pack | 1.36 MB | base grid 1.6 MB, own grid 3.0 MB |

* With the retail collision pack (`--base-collision`) the car's 25 s drive logs the same 234
  lines as on the retail level and the frames match but for smoke and a moving prop (objects
  spawn in another order).
* With the rebuilt collision, points 2 units above / below the surfaces agree with the retail
  trees 98.1% / 99.2% (the difference is mostly the retail slivers, snapped away); random points
  in the cells 98.5% (own grid), 99.98% (retail grid); downward rays onto floors
  report a floor facing up 97.0% (retail 98.6%: its own slivers). The car drives, jumps, scrapes
  walls and never falls through (drives of 45 and 100 s, lowest point the road); its path differs from
  retail after a few seconds (a different tree reports other planes at the same surface).

**The arena** (`arena`: a 3,000 x 3,000 ground, walls, 6 ramps, 8 pillars, 468 triangles of
our own; textures and sky links into `w1_tex` and world1's hub): 4 strips + hub, 169 collision
cells, packs of 5 - 6 KB, world pack world1's with no objects and the start moved; registered by
its `ids.lock` (`HWVX_IDS_LOCK`) as track 57, level 5 of Joyride. The car drives, takes a ramp
(y 2 -> 65 -> lands) and stops at the walls.

**What reconstructing the retail collision taught** (for anyone feeding a BSP-derived mesh back):

* The open terrain surface alone does not rebuild: the solid of an open surface depends on the
  polygon order, which the trees keep only partly. `extract` writes each retail cell's solid as a
  **closed** surface instead (`boundary_polys` + `box_caps` of the cell's tree), one glTF node
  per cell with the cell's box in `extras.sourceBox`.
* **float32 positions** are not enough at world coordinates (an ulp is 5e-4 at 5,000, the
  builder's tolerance 1e-4): each cell's vertices are relative to its centre (node translation),
  and the exact planes ride along (`_PLANE`, with `_POLY` grouping a polygon's triangles).
* **Slivers** (a face's two triangles a hair out of plane) leave thin solid / empty layers; built
  into another tree their surfaces become the plane a ray reports (a floor facing down pushes the
  car through it). `bsp_build.snap_tree` before extracting removes them.
* Closed solids of two sources in one new cell **cannot share a tree**: a splitting plane of one
  leaves the other's inside unbounded. The cell is cut along the sources' box walls first (a kd
  split, axis-aligned nodes) and each part gets its source's tree; a part with no polygons is all
  solid or all empty as its source's tree says.
* A polygon lying in a side of the cell and facing into it (a neighbour's surface or cap) would
  declare the cell empty: dropped.

**Format facts found on the way** (xdl-format §10.8 / collision packs):

* A **local ref relocation** (`ref_relocs`, pack 0) is a pointer the loader fixes up
  (`*slot += base`): the slot holds a data offset, the record's value is the target geometry's
  index. Moving data means moving the slot's value too (the collision packs' two point at their
  materials' effect geometry; the world packs have about 600).
* A collision pack's cell lists the crease edges whose segment crosses its box grown by 10
  (w1_col: 455 of 472 cells exactly, the rest list a few more); the header's 0x44 is the cell
  count with cell 0, a cell's 0x3C its node count (`OBoxHitsBSP` skips a cell whose 0x3C and
  0x40 are both 0).
* The collision materials are a pointer table into the pack's tail (surfaces with spark and skid
  effects, their geometry, sounds and textures): a new collision pack reuses the base's tail.

**Risks and what is left**:

* **View distance**: a camera draws its strip and two each way; in world1 that is 630 - 1,330
  units along the strips' axis (whole width across). Farther geometry is not drawn (pop-in) unless
  it is in the hub. Retail picks neighbours by sight; the strips trade that for the guaranteed
  neighbour limit. A grid of regions with a visibility pass is the next step for open worlds.
* **Collision is equivalent, not identical**: same solid (to ~1%), different trees, so the car's
  path differs from retail; collision packs are 1.2x (retail grid) to 2.2x (own grid) retail's size. Cells fully inside a solid
  of a *plain* mesh (no source boxes) are left empty (a car tunnelling through a thick slab would
  fall); closed or ground-only meshes are fine.
* **The level is the base's world pack**: its scripts, volumes (fog, reverb), nav, paths,
  teleporters and objectives are the base level's, in the base's coordinates. Fine for a
  re-chunked retail level; a new world needs them authored (a world1-based arena keeps world1's
  teleporters and radar map). Objects come from the base or none; there is no object list in
  the project yet.
* **GameCube only** (big-endian, GC display lists); PC encoding exists in `xdl_gltf`, PS2 does not.
* **Archives only from the data folder**: the built archives are staged next to linked base
  files; a mod cannot bring world archives yet (`dvd.c` opens only the data folder), and the
  built packs hold base data (materials, hub, world pack) so they are built on the player's
  machine, never shipped.
* The project is the chunker's own (`world.jsonc` + glTF), compiled straight to `hwvx-x` packs,
  not an `xdl_graph` project.
