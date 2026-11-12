# Draw distance option

For the superproject's `docs/worlds/README.md` (new section "Who reads the active
quadrant set" after "Streaming: how quadrants are picked") and HWVX-SDL's README
(env switch `HWVX_DRAWDIST`).

## Who reads the quadrant set (research)

The game's quadrant state: `WorldQuad.visible` (this frame), `WorldQuad.loaded`
and `geoms` (pack in memory), `QuadContents.dirty` (contents spawned),
`g_LoadedQuadrants` / `g_numQuadPacksInMemory` (the active set, without the
hub), `g_pWorldQuadrant` (the hub), the loader (`g_bLoadingThreadRunning`,
`g_QuadrantsToLoad`, `g_pQuadPackToBeReleased`). Every reader, HWVX-SDL `src/game`:

| where | what it does with it | logic / drawing |
|---|---|---|
| `World.c:84` `World_Render` | finds each camera's quadrant (region BSP, `pCurrentQuadrant[5]`), marks it and its neighbours `visible`, draws the visible loaded ones, makes them the active set, queues loads | both: it *drives* the logic below |
| `World.c:334` `RenderQuadrant` | geometry + static instances (draw distance, frustum, `radius`), their shared slaves | drawing |
| `FileStreaming.c:228` `FileStreaming_SetActiveQuadrants` | spawns the new active quadrants' contents: destructibles (non-static `QuadInstance` → Destructible things: collision, physics, triggers on kill), emitters (`QuadFxPoint` → Emitter things + `ActionFXPointChangeMode`), coronas (`CoronaHolder_Add`, add-only, cap 64), shared slave holders (`SharedSlaveHolder_Get`), static `radius`; releases the leaving quadrants' things (items: `spawned` reset) and queues their packs | **logic** |
| `FileStreaming.c:377` `FileStreaming_HeartBeatItems` | every 1.5 s counts down and (re)spawns the pickups of the active set + hub only | **logic** |
| `FileStreaming.c:157` `FileStreaming_ResetQuadResources` | round reset: releases and respawns the active set's things, resets every quadrant's modes | **logic** |
| `FileStreaming.c:88` `FileStreaming_ReleaseQuadPacks` | level unload | logic (lifetime) |
| `FileStreaming.c:447` `FileStreaming_LoadQuadPacks`, `LoadQuadrants`, the loading thread | streams geometry; `g_bLoadingThreadRunning` is read by `State_GamePlay.c:663` (leaving gameplay waits for it), `State_ProgressionManager.c` (loading states), `GameSpecificInterfaces.c` (menus), `Port_Proto.c:430` | drawing, but the thread flag is logic-visible |
| `State_ProgressionManager.c:446` | level start: loads all quadrants (≤ 11) or the hub + player 1's region and neighbours | drawing (loading) |
| `State_ProgressionManager.c:566` | loads every quadrant's item records (all quadrants) | logic, not set-dependent |
| `GameRules.c:77`, `GameSettings.c:1292/1385` (DD rescue save) | modes of every quadrant's items/instances/fx points | logic, not set-dependent |
| `Port_Online.c:1716` | finds an item's spawner in every quadrant | logic, not set-dependent |
| `Destructible.c:510` `ActionDestructibleChangeMode`, `World.c:304` `ActionItemChangeMode`, `Action.c:540`, `Trigger.c:313/735/826`, `WildCard.c:379`, `CameraManager.c:907`, `ItemThing.c`, `Emitter.c`, `CarA.c:2372` (pickup respawn) | act on the spawned thing of a quadrant entry, if any: so they depend on the active set through it | **logic** |
| AI (`AI_CarA.c` targets: items, cars, destructible obstacles via `AI_Car_AsyncScanDestructibles`), physics (destructibles), triggers on destroyed instances | see only spawned things | **logic** |
| `VolumeManager.c` fog/reverb volumes, collision (`CollisionTerrain`), nav (`NavHolder`), paths, tiles, sounds (speaker tiles) | world-wide, not per quadrant | not set-dependent |
| `ShaderHolder_AddGX` (128 entries, no bounds check), `VertexColorSeqManager_AddVertices` (64, no check) | filled by loaded quadrant packs (`DataPack_RefreshQuadPackPtrs`) | drawing, but fixed tables: extra packs could overflow them |
| `Gfx_GeometryHandler.c:103` far clipping plane, `Render_.c` fog, `Render_Matrix_.c` projection far 20000 | | drawing |
| `port/viewer_world.c` | overlays of loaded/visible | drawing (tool) |

## The option (HWVX-SDL)

`graphics.cfg` `drawdist=normal|far|all` (default normal), `HWVX_DRAWDIST`
overrides; the launcher's Settings page: "Draw distance: Normal / Far /
Everything". `src/game/Port_DrawDist.c`, called from `World_Render` after the
game has marked its visible quadrants and before it draws them:

* Far: the quadrants two neighbour steps from each camera's; Everything: all.
  Those the game isn't drawing this frame are drawn here, before its own.
* Never touched: `visible`, the active set, `dirty`, the loader and its flag,
  the game's packs' `loaded`/`geoms`. A quadrant the game has loaded anyway is
  drawn from its copy; otherwise a **shadow copy** (a `WorldQuad` copy sharing
  the game's `contents`) is loaded on the main thread while the loader is
  idle (≤ 2 a frame, 4 ms), into its **own heap past the game heap**
  (`os.c Port_ExtraHeap`, the arena's last 32 MB, untouched unless used), so
  game allocations are byte-for-byte where they were. Its shaders are
  animated by Port_DrawDist (not added to ShaderHolder), no vertex colour
  lists, no coronas. Its disc reads don't count for `HWVX_AUTOINPUT`'s "+N".
  Shadows are kept 300 frames after they stop being wanted, freed at
  Normal and at world unload.
* Drawn: geometry, static instances (no shared slaves: those are things), and
  for quadrants the game never activated, destructibles in their placed pose
  if shown (mode 1) as stand-ins. Emitters, coronas, pickups of extra
  quadrants are not drawn (things).
* Far: far clipping plane and fog ×2. Everything: no far clipping plane, no
  distance fog, projection far plane 80000 (from 20000).
* The sky keeps its fog: backdrop geometry (group flag 0x8000 on its first
  group, the flag the far plane skips) is drawn with the world's own fog,
  everything else with the pushed-out / no fog (`Render_.c`
  `Port_RenderGroupFog`, from the opaque and alpha group draws in
  `Render_Geometry_.c`; at Normal it returns at once).
* Normal in the attract demo (code variable 0x198), input playback
  (`g_playbackStatus == 2`) and online play.

Measured (GC data, Mission 1 driven by the autopilot test switch, which is
on its own branch; 164 × 60 frames):

| | extra copies | their size | extra heap in use | draws / 60 frames | renderer ms/frame avg (max) |
|---|---|---|---|---|---|
| Normal | 0 | 0 | 0 | 62,773 | 4.61 (8.15) |
| Far | 10 | 1.7 MB | 4.1 MB | 89,663 | 3.94 (6.65) |
| Everything | 20 | 3.3 MB | 8.1 MB | 125,851 | 4.41 (11.08) |

(The machine was shared: frame times are noisy; nothing above 12 ms.) A GC
quadrant copy loads in 0.3 - 0.9 ms. Process memory moves by less than the
noise (430 - 480 MB resident).

Verified: GC, PC, PS2 attract demos frame-identical to pc-data with the
option off; ctest. Logic: a scratch build of the autopilot branch's source
(which carries this same draw-distance code) drove Challenge 1 (4 quadrants)
and Mission 1 (11 quadrants, a fight) to a win at Normal, Far and Everything:
identical car logs and quadrant routes (`tools/autopilot_drive.sh compare`),
the same win times.

Open points:

* Far quadrants show no emitters, coronas or pickups, and destructibles only
  as stand-ins in their placed pose.
* An earlier scripted Joyride drive (car idling at a respawn point) gave car
  logs that differed between two Everything runs as well as against Normal,
  so that was run-to-run timing there (a heavy frame), not the option; the
  autopilot drives above were identical in every run.
