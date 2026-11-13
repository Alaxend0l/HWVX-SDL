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
  viewports (mapped through the safe area, proportional), HWVX_DUMP frames and screenshots at render size.
- The data browser's embedded view (Port_ViewerEfbSize) is NOT scaled: it asks for the exact pixel size
  it shows, so its shared frames and pick coordinates need no mapping.
- gx_shaders.h: only present_frag_* regenerated; the local dxc differs from the one that built the rest,
  so present_frag_dxil is untested on D3D12.
