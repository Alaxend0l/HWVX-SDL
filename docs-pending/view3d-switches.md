# The 3D view's switches (task #43): reference

Where it belongs:
- docs/viewer-protocol.md: the new `view3d` command, in the world commands' table.
- docs/level-editor.md: the Worlds tab's "View" row.
- The draw-distance docs: the viewer branch of src/game/Port_DrawDist.c.

**`view3d`** takes any of `clip`, `allQuads` and `fog` (booleans) and replies with all three as
they are now. The defaults are `clip: true`, `allQuads: false`, `fog: true`, which draw as the game
does. The switches belong to the viewer process (HWVX_VIEWER) and hold for whatever world it
shows. They never apply to the game's own play or to a test drive, which are separate processes
without HWVX_VIEWER. Every branch is gated on Port_ViewerOn().

- **`clip: false`** (Geometry clipping off): nothing disappears with distance.
  - No far clipping plane (Port_DrawDistFarPlane returns 0).
  - The projection's far plane goes out ×4.
  - Static instances ignore their draw distances and the frustum test (World.c RenderQuadrant;
    Port_DrawDist.c InDrawDist).
  - Geometry skips the frustum test, and past its last LOD distance it keeps the last LOD instead
    of vanishing (Gfx_GeometryHandler.c, Gfx_GetCullLODGX).
  - With the fog on, the fog's range is pushed out ×4 with the geometry.
- **`allQuads: true`** (Load all quadrants): the draw-distance code's "All" set, whatever
  graphics.cfg says.
  - The quadrants the game isn't drawing are drawn from its loaded copy, or from shadow copies in
    the draw distance's own heap, loaded a couple a frame.
  - The game's streaming and active quadrants are untouched.
  - With clipping on, the far plane still applies.
- **`fog: false`**: no distance fog. The sky (group flag 0x8000) keeps the world's own haze
  (Port_RenderGroupFog).

**In the browser:** the Worlds tab has a "View" row with three checkboxes: Geometry clipping,
Load all quadrants, Fog. From the command line, `--world NAME --view noclip,allquads,nofog` sets
them at start, for scripted shots.

**Tested:**
- viewer_client on world1, from a camera 600 up:
  - default: the distant skyline is fogged and cut at the far plane;
  - all three switched: the city is drawn to the horizon with no fog;
  - switched back: the same picture as the default.
- The attract demo is frame-identical (20 s), ctest passes, and the embed check passes.
