# Viewer: the picture embedded in the data browser (task #30)

For the superproject's docs/viewer-protocol.md; each item says where.

## What it is (replace the "Mouse in the game window" bullet)

- **The window is hidden** in viewer mode: the game renders offscreen at the size the client asks
  for (`frames`) and shares each frame through shared memory; the browser draws it in its central
  panel. `HWVX_VIEWER_WINDOW=1` shows the game's own window as before (debugging; the browser's
  `--viewer-window`); there the mouse still orbits (left drag), pans (right / shift drag) and zooms
  (wheel). `HWVX_VIEWER_STATS=1` logs the readback's cost every second.
- Without `frames` the EFB is the (hidden) window's size, as before; `screenshot` saves the EFB at
  whatever size it has.

## Commands table (add)

| cmd | arguments | reply |
|---|---|---|
| `frames` | `width`, `height` (16x16 .. 3840x2160, physical pixels), or `on: false` | `on`, `name` (the shared memory), `key` (16 hex digits), `size` (its bytes), `slots` (3), `capacity` (bytes a slot), `headerSize` (4096), `width`, `height`. From the next frame on the EFB has that size (any shape: the 3D view widens or heightens as in "screen mapping", the camera's vertical field of view follows the panel). A size that fits the shared memory keeps it (same `name`); a bigger one gets a new one (new `name`, reopen it), with a quarter to spare. `on: false` stops and goes back to the window's size. |
| `input` | `events`: `[{type: "drag", button: "left"\|"right"\|"middle", dx, dy, pan?}, {type: "wheel", dy}, {type: "key", key}]`, `height?` | `camera`. In image pixels: a left drag orbits (the image's height is about 170 degrees), a right or middle drag (or `pan: true`) pans so the point under the pointer follows it; `wheel` `dy` notches (positive zooms in, x0.9 each). Keys: `f` frames what is drawn, `home` / `r` resets the view, `space` toggles spin, `left right up down` orbit 15/10 degrees, `plus minus` zoom. An unknown key or event type is an error (the others in the list still apply). |
| `pick` | `x`, `y` (image pixels), `select?` | `node` (the geometry node drawn there, or null), `pack`, `distance`. Basic: the ray against each drawn geometry's bounding sphere (as the last frame placed it), the smallest hit wins (a part over the body), nearest among equals. `select: true` also selects it. For #21: click-to-pick is `pick` then `select`/`get`; a finer pick (triangles, world objects) can replace it behind the same request. |
| `status` | | adds `width`, `height` (the EFB's), `headless`, `frames: {on, width, height, published, fps, ms, msCopy}` (`ms`: the readback's average cost a frame on the game's thread, `msCopy`: a published frame's copy into the shared memory) |

## Embedded frames (new section, after "Share packs")

The shared memory is POSIX `shm_open` (`name` like `/hwvx-<16 hex>-<n>`, mode 0600) or a Windows
named file mapping (`Local\hwvx-...`), opened read-write by the client (it writes `consumed`). The
client checks `magic` and `key`, and on POSIX unlinks the name right after mapping it (so nothing
else opens it and it can't outlive a crash; the game unlinks it too when replacing or freeing it).

Layout, little-endian:

| offset | field |
|---|---|
| 0 | `magic` u32 0x46565748 ("HWVF"), `version` u32 1 |
| 8 | `key` u64 |
| 16 | `headerSize` u32 (4096: slot 0's pixels), `slots` u32 (3), `capacity` u32 (bytes a slot) |
| 28 | `published` u32: frames published so far |
| 32 | `latest` u32: the newest one's slot |
| 36 | `consumed` u32: **written by the client**: the `published` count of the frame it last took |
| 40 | `format` u32 (1: RGBX8, the 4th byte is not alpha) |
| 64 + 32 i | slot i: `seq` (even: complete, odd: being written), `frame` (the viewer's frame number), `width`, `height`, `stride`, `published` (the count with it) |
| headerSize + i * capacity | slot i's pixels, top row first |

Reading (a seqlock): read `published`; if it is new, read `latest`, its slot's `seq` (odd: try
again), copy the pixels, read `seq` again (changed: try again), then store the slot's `published`
into `consumed`. The game writes a frame into the slot after `latest` and only grabs a new one while
at most one published frame is untaken, so with 3 slots the one a client is copying isn't
overwritten (the `seq` check covers the rest).

The readback doesn't stall the game: each frame's EFB is copied into a GPU readback buffer at its
end (SDL_GPU: a download submitted with a fence; OpenGL: a pixel buffer and a fence) and copied into
the shared memory at the end of the next frame, once the GPU is done (2 buffers; with both busy the
oldest is waited for). A client that stops taking frames stops the readbacks. Measured on an M-series
Mac (Metal), car 5, the browser taking every frame: 800x600 0.13 ms/frame, 1920x1080 0.26 ms,
3840x2160 0.79 ms, all at 60 published frames/s; input shows 1-3 frames later.

## tools/browser (Tests and tools: add)

- The central panel shows the picture as a texture (updated when a new frame arrives; a thread polls
  the shared memory and copies it out, off the UI thread). It asks for the panel's size in
  physical pixels (size x pixels_per_point, capped at 3840x2160 keeping the shape) 150 ms after the
  panel stops changing; meanwhile the last frame is stretched. A status line under it: fps, image
  size (and the size coming), the game's readback ms/frame and published frames/s. Drag orbits,
  right/middle/shift drag pans, the wheel zooms, a click picks (and selects the node in the
  outline), keys over the panel (F, Home, R, Space, arrows, +/-) go to `input`. The properties moved
  to a right-hand panel.
- `src/frames.rs`: `FrameReader` (open by name/key/size, `take` the newest new frame).
- `tests/frames.rs`: hello (with the token), `frames` at 400x300, 5 frames of that size, moving on
  and not blank; a still camera gives identical frames and a drag changes the picture; wheel zooms,
  right drag pans, a key, a bad key refused; `pick` at the centre finds a `geometry/N`, a corner
  nothing; a smaller size keeps the shared memory; the readback cost at 800x600, 1920x1080,
  3840x2160 (printed with `--nocapture`); over 3840 refused; `on: false`. `HWVX_RENDERER=opengl`
  runs it on OpenGL.
- `hwvx-browser --app-shot` now also waits for the picture at the panel's size and its stats.
