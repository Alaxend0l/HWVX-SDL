# Viewer: whole worlds (for docs/viewer-protocol.md)

New section "Worlds", after "Media: textures, sounds, instruments, songs". Also two notes for
"Commands" (the car fix) and "Tests and tools".

## Worlds (new section)

`world` loads a level the way the progression manager does: `LoadWorldProcess` (world pack,
world textures, collision, the MCP's share pack, the level's items, the tile cars' packs, then
`World_Init` / `World_Refresh` for the envmap, fog and far plane), run to the end in one request
(GC ch1_l2: about 40 ms). The game's own world processes then run, as `StateGamePlay_Init` adds
them: World (`World_Render` streams the quadrant packs around the camera and draws them, the hub
and the static instances; `FileStreaming_HeartBeatItems` spawns the pickups), ThingManager
(destructibles, emitters, items, movers), particles, trails, lights (the world's tile lights),
flashes, coronas, shaders, vertex colour sequences, timers. Not the players, input, interface,
rules, triggers or sound. The viewer's camera is the game's camera (window 0), so quadrants load
and unload as it moves, by the game's region test. The tile cars are drawn standing at their
tiles (the game's orientation, lowered onto their wheels), not spawned: no AI, no physics.

The world pack becomes the shown pack (`ARCHIVE/world_0`): `outline`, `get`, `set` work on it
as on any pack. Loading another pack (`load`) unloads the world; `world_unload` does it alone. A
world and a pack are not shown together.

Nothing is drawn from the viewer's MCP envmap or lights while a world is shown: the world's
(`World_Refresh`, `LightManager` for things). The clear colour is the game's (black; the hub's
sky covers it).

### Camera

Besides orbit, a free fly camera. In fly mode the camera is at `pos`, looking along `yaw` /
`pitch` (the same angles as orbit: switching keeps the view). A left drag looks (the view turns
where the pointer goes), a right / middle / shift drag moves sideways and up, the wheel sets the
speed (x1.25 a notch), `move` events move it.

### Commands (add to the table)

| cmd | args | reply |
|---|---|---|
| `worlds` | | `worlds`: every level of every progression script of the MCP: `{mode, progression, index, name, world}`. `mode`: `adventure challenge drag battle joyride mpdrag mpchallenge mpbattle` (progressions 0-7); `name` the world's last part (`ch1_l2`), `world` the MCP's path (`worlds_\sp_\chal1_\ch1_l2`) |
| `world` | `name` (a level's `name` or `world`), `mode?` (only that mode's levels) | `world`: as in `status`. Loads it (see above). Without `name`: the loaded world's status |
| `world_unload` | | |
| `overlays` | any of `regions`, `loaded`, `collision`, `volumes`, `spawns`, `paths`, `objects` (and hook layers): `true` / `false` | `overlays`: each layer and whether it is on |
| `goto` | `port?` (default 1), or `nav` (a nav point's index) | `camera`. The camera behind player `port`'s start (its tile car), looking along it; or at nav point `nav` looking on to its next point (tests fly the AI's route this way) |
| `camera` (add) | `mode`: `orbit` / `fly`, `pos` [x,y,z] (fly; sets fly), `speed` (fly, units a second) | `camera` adds `mode`, `pos` (the camera's position, either mode), `speed` |
| `input` (add) | event `{type: "move", forward, right, up (-1..1), dt (seconds), fast?}` | fly (x4 when fast). Keys add `w a s d` (forward, left, back, right), `q e` (down, up): a tenth of a second each, and `g` (the start) |
| `focus` (add) | a world pack's node | the camera on the world object the node holds (tile car, creature, instance, pickup, effect point, quadrant: its region's middle) |
| `pick` (add) | | in a world: the ray against the visible quadrants' instances (their radius), pickups, effect points, tile cars and creatures; the smallest sphere hit wins. Adds `kind` (`instance`, `item`, `fx_point`, `tile_car`, `tile_creature`) and `at` (`@hex`, the object's offset in the world pack); `node` is the world pack's node holding it (raw until its struct has a schema) |
| `status` (add) | | `world`: null, or `{name, world, pack, quads, loadedQuads, loaded, visible, maxLoaded, cameraQuad, tileCars, tileCreatures, volumes, navPoints, loadMs, overlays}`. `cameraQuad`: the first region in table order holding the camera (-1: none, the hub only) |

### Overlays

Drawn after the frame's scene (after `Gfx_EndScene`, before the readback), as thin quads facing
the camera (about 1.5 points wide at any EFB size; GX lines are one pixel): once dimmed with no
depth test (through what hides them), then bright where in front.

* `regions`: each quadrant's region (BSPN0) as its box: the points of a 64 x 24 x 64 grid over
  the collision bounds the region holds, sampled at load. Grey.
* `loaded`: the regions of loaded quadrants, green when in view, blue when loaded and not; the
  camera's quadrant yellow (also with `regions`).
* `collision`: the collision pack's crease edges (`TerrainEdge`, mode 1 packs; bat2_l9's poly
  lists are not drawn).
* `volumes`: sphere volumes as three circles, BSP volumes as a cross at their centre (fog light
  blue, reverb cyan, others blue).
* `spawns`: tile cars as arrows along their facing (green: player ports 1-4, orange: AI),
  creatures, pickups (crosses), teleporters (circles), lights.
* `paths`: the nav graph's links (yellow from way points).
* `objects`: every quadrant's instances (crosses sized by radius) and effect points.
* Hooks: `ViewerWorld_AddOverlayHook(name, draw)` (viewer_internal.h) adds a layer; its `draw`
  adds lines with `ViewerWorld_Line`, `overlays {name: true}` turns it on, `status` and
  `overlays` list it. For trigger volumes and paths once their schemas are in.

## Commands (note for the car preview)

A car is lit as the car select lights it (`LightManager_LightThing` outside a world: white
ambient and parallel light) and reflects the MCP's environment maps. Those are the MCP's own
textures: the viewer reloads them at start (`StateGameManager_RefreshMCPTextures`, as each
state's Init does), because Main_Init's `Gfx_TextureManager_Init` had dropped them after the
content registry loaded the MCP, and the texture slots the MCP's envmap ids name had gone to the
first pack loaded next (a car's share pack): rims, chrome and windows sampled a wheel texture
and came out black.

## Tests and tools (add)

* `cargo test --manifest-path tools/browser/Cargo.toml --test world` (`HWVX_DATA`): `worlds`
  lists ch1_l2 / bat1_l1 / world1; ch1_l2 loads, its frames are not blank, the start's quadrant
  and the hub are loaded and not all of them; overlays change the picture; a pick at the start
  is the tile car, a node of the world pack's outline; flying the AI's route (`goto nav`) loads
  more than three other quadrants and releases ones left behind; the fps; bat1_l1 and world1 load
  and draw; `load` of a car pack unloads the world.
* `tools/viewer_world_shots.py --build build --data DIR --out OUT WORLD...`: per world the start,
  the overlays, the collision edges from above and a shot along the route, a contact sheet, and
  the load time, quadrants loaded, fps and resident memory.
* `tools/viewer_car_compare.sh BUILD DATA OUT CAR...`: the car select's frame of each car next to
  the viewer's side view of it.
* `hwvx-browser --world NAME [--overlays regions,loaded,...] --app-shot FILE`: the browser with
  a world loaded (the Worlds tab: levels by mode, overlay toggles, Orbit / Fly, Start, Focus,
  Unload; WASD / Q / E fly while over the picture, shift faster, G the start).
