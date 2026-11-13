# Resolution scale (0.4.1 E1-E2) -> docs/plans/v0.4.1.md part E (done notes), README's graphics settings

- `graphics.cfg resscale=0.5|0.75|1|1.5|2` (launcher Settings > Display, "Resolution scale"),
  `HWVX_RESSCALE` / `--res-scale N` override (any value 0.5..2, not saved). `resfilter=linear|nearest`
  (`HWVX_RESFILTER`), no launcher row.
- The EFB is the window's pixel size times the scale (gx_draw.c WindowSize), capped to the renderer's
  largest render target keeping the shape (OpenGL: min of max texture/renderbuffer/viewport; SDL_GPU:
  16384), logged once per window size when it caps.
- Present: at 1x unchanged (letterbox, blit/one sample). Scaled: fills the window; >1x an area-weighted
  box filter (present.frag / gx_gl.c gamma shader), <1x bilinear or nearest.
- Everything that reads the EFB follows it unchanged: HUD 4:3 safe area and widening, split-screen
  viewports (since E3: over the whole EFB, each view widened, below), HWVX_DUMP frames and screenshots at
  render size.
- The data browser's embedded view (Port_ViewerEfbSize) is NOT scaled: it asks for the exact pixel size
  it shows, so its shared frames and pick coordinates need no mapping.
- gx_shaders.h: only present_frag_* regenerated; the local dxc differs from the one that built the rest,
  so present_frag_dxil is untested on D3D12.

## E3 checks (2026-11-13, branch resscale-e3; also -> docs/menus/README.md "Widescreen HUD")

M2 Max, macOS, GC data. Window 1280x960 points (2560x1920 pixels at 1x) unless said. Dumps
compared at 1x size (area downscale/upscale) with ffmpeg SSIM; window captures (screencapture -l,
the game's window only) for the present path. Pass = looks right next to 1x, no crash, HUD and
views in place, dumps at render size.

| | Metal | OpenGL | Vulkan (MoltenVK) | D3D12 (CrossOver) |
|---|---|---|---|---|
| attract demo 0.5 / 0.75 / 1.5 / 2 | pass (SSIM 0.97-0.98 / 0.99 / 0.995 / 0.995) | pass, same numbers | pass, same numbers | pass (1280x960 px window; 0.96-0.98 / 0.98-0.99 / 0.99 / 0.99) |
| race with shadows (challenge ch1_l2), 0.5 / 2 | pass (0.992 / 0.999) | pass | pass | pass at 2 (0.998) |
| stencil vs HWVX_SHADOW_COPY=1, 2x | identical | identical | identical | - |
| battle race 0.5 / 2 (no shadows: split-view mode) | pass | pass | pass | - |
| split screen 2/3/4 players, 0.5 / 2, 4:3 and 16:9 | pass | pass (4P) | pass (4P) | pass (4P 16:9 2x) |
| 4K window (clamped to 3600x2196 px), 2x = 7200x4392 | pass | pass | pass | - |
| cap (forced 4096 in a test build): 2x 5120x3840 -> 4096x3072 | pass, logged once | pass, logged once | - | - |
| window present (0.5 linear/nearest, 1, 2 box) | pass | not capturable (screencapture can't read GL windows) | pass | pass (0.75, 1.5, 2) |
| FPS strip (HWVX_FPSHUD) | fixed: same size at any scale | - | pass | pass |

- 1x byte-identical: tools/demo_compare.sh against pc-data (attract demo), and a 16:9 one-player
  race and 16:9 attract demo against pc-data.
- Renderers agree with each other at every scale (SSIM 0.9999+ between Metal, OpenGL, Vulkan; D3D12
  vs Metal 0.9999 where the sim agrees). The Windows build's attract demo diverges from the macOS one
  later in the demo (frame 3000: another moment of the race, not a rendering difference).
- Shadows: the challenge race draws them (HWVX_GPUTIME: shadow volumes, mask draw; the copy path's
  one EFB copy a frame at 5120x3840); the attract demo and battles draw none (the game skips them
  with more than one view bit).
- Editor: the browser's embedded view (tools/browser_embed_check.sh with HWVX_RESSCALE=1/2/0.5) is
  unscaled as designed, its frames 1652x1342 whatever the setting; the viewer in its own window
  (HWVX_VIEWER_WINDOW=1) at 2x reports 3200x2400, takes picks in those pixels (the centre picks the
  same node as at 1x) and saves screenshots at that size.

Found and fixed (resscale-e3):
- Split screen on a wide window was pillarboxed (the views mapped into the centred 4:3 area). Now
  the views fill the window, each projection widened like one view's (gx_draw.c MapViewport,
  Port_ViewWiden in split screen too), and each player's HUD moves with its view: centre with the
  view's centre, sides to the view's edges (InterfaceManager.c Port_HudPane/Port_HudShift; split
  thresholds 256/384 of 640 so the battle's speed stays by its radar).
- The game's 2- and 4-player HUDs sat 10 units toward the screen's middle lines (offsets 10/310,
  10/230 for views at 0/320, 0/240): now at their views' corners, 4:3 included (split screen only).
- The FPS strip shrank at 2x and grew at 0.5x (sized in EFB pixels): now sized for the window.
- HWVX_START=race:...,players=P (1-4): P local players, as the split car select leaves them (for
  split-screen tests; mpbattle/mpdrag).

Not tested here: a real GPU cap (16384 on every renderer here; only a forced 4096), a physical 4K or
external display, OpenGL's presented window (dumps only), D3D12 on real Windows hardware, the
editor's Test drive (it starts the game normally, so it follows graphics.cfg like any run).
