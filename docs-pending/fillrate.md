# Fill rate: what grows with the window (for docs/plans or README "Debugging")

Measured 2026-11-13, M2 Max, retail GC data, unpaced (tools/gputime.sh, HWVX_GPUTIME=1).
Window sizes are pixels (Retina: --window is half). 4K is 3600x2160 (screen clamp).

Per frame, race (challenge ch1_l2, autopilot, single view): 1 clear, 1 EFB copy (the
shadow mask, the only GXCopyTex in the game, full EFB size RGBA8; the GameCube's was
320x224 A8), 1 display copy, 0 readbacks (only HWVX_DUMP / the viewer read back), no
MSAA. SDL_GPU: 7 passes (2 EFB render passes split by the mask copy, 2 upload copy
passes, the copy itself, present); attract demo 4 (split-screen battle: no shadows).

OpenGL GPU ms (GL_TIME_ELAPSED), race:
| EFB        | scene | volumes | mask copy + mask draw | rest | total |
| 640x480    | 0.68  | 0.24    | 0.58 (with rest)      |      | 1.51  |
| 1280x960   | 0.70  | 0.23    | 0.80                  | 0.17 | 1.90  |
| 2560x1440  | 0.82  | 0.26    | ~2.4 (with rest)      |      | 3.75  |
| 3600x2160  | 0.98  | 0.25-0.57 | 2.12 (3.21 before the fix) | 0.33 | 4.00 (4.66) |
Attract demo (no shadows): 0.35 / 0.40 / 0.48 / 0.74 ms. Metal (fence wait, includes
vsync on macOS so only indicative): race 1.7 / 2.5 / 6.0 / 5.4 ms, demo 0.9-2.9 ms.

The scene barely grows with pixels; the shadow mask (a full-EFB copy plus a full-screen
alpha-compare/blend quad) is most of the growth. Shadows came in 2663b2d (0.2.0), after
0.1 (GXCopyTex was a TODO there). 0.1 vs now FPS is not comparable unpaced: 0.1 steps
the game by the real delta (1.1 ms), now a fixed 16.67 ms step.

Done (branch fillrate): GL's mask copy no longer reallocates its texture each frame
(-1.1 ms GPU at 4K here). Left, by gain:
1. Shadow mask without the EFB copy: count volumes in stencil and test the mask quad on
   it (no copy, no SDL_GPU pass split; identical if the counting matches).
2. Copy and mask only the volumes' screen rectangle (identical only if nothing else
   leaves alpha outside it), or a half-size mask as the GameCube had (changes output).
3. SDL_GPU: depth store DONT_CARE on the frame's last EFB pass (copyDisp then clear).
4. GL display copy: one draw writing alpha 1 instead of clear + blit + masked alpha clear.
5. present.frag: skip pow() at gamma 1 (window only, not in dumps).
6. Retina/HiDPI: 640x480 points draws 1280x960; a render scale (resscale branch) helps.
