# HWVX-SDL
Turning the GC Decompilation of HWVX into a portable codebase for modding and porting!

The game code comes from the [HWVX-GC-Decomp](HWVX-GC-Decomp) submodule. The
GameCube SDK it was written against (OS, GX, DVD, PAD, CARD, AX...) is replaced
by a platform layer on **SDL3** (window, input, files, threads) and
**OpenGL 3.3** or **SDL_GPU** (Vulkan, Metal, Direct3D 12) for graphics; sound
goes through SDL3's audio streams. Everything is C, built with GCC.

## Building

Requirements: GCC, CMake ≥ 3.20, Ninja (or Make), SDL3 development files.

```sh
cmake -S . -B build -G Ninja
ninja -C build
```

## Game data

Not included. Extract the `files/` directory from your own copy of the
GameCube disc (Dolphin: right-click the game → Properties → Filesystem →
Extract Entire Disc) and either put it at `orig/files/` or point the game at it:

```sh
./build/hwvx --data /path/to/files
```

Options: `--data DIR`, `--scale N` (window = 640×480×N), `--window WxH`
(any size and shape), `--fullscreen`, `--renderer NAME`.

Resolution: the game renders at the window's size and shape, and the window
can be resized. On a screen wider than 4:3 the 3D view widens to fill it while
the HUD and menus keep their 4:3 layout in the middle (taller screens work the
same way vertically); split screen stays 4:3. Options → Graphics (a menu the
port adds) picks a resolution from the display's modes and windowed or
fullscreen; it is applied when you leave that menu and kept in `graphics.cfg`
in the save folder. Command-line options override it. The main menu also gets
a "Quit Game" entry (with the game's own Confirm Quit screen); both
port entries are built from the menu data at load time (`src/game/Port_Menu.c`).

Renderers (`--renderer`): `opengl` (default, OpenGL 3.3 core), and through
SDL_GPU `vulkan`, `metal` (macOS) and `d3d12` (Windows). The SDL_GPU
renderers are experimental.
F11 or Alt+Enter toggles fullscreen. Saves go to SDL's per-user preferences
folder (`memcardA/*.sav`).

Controls: any SDL-supported gamepad (A/B/X/Y by position, triggers = L/R,
right shoulder = Z), or keyboard on player 1: arrows/WASD steer, Space/Z = A,
X = B, C = X, V = Y, Q/E = L/R, Shift = Z, Enter = Start, I/J/K/L =
D-pad, T/F/G/H or keypad 8/4/2/6 = C-stick.

## Layout

| Path | What |
|------|------|
| `src/game/` | Game sources imported from the decomp, patched for portability |
| `src/game/include/game_protos.h` | Generated: one prototype per game function |
| `src/game/include/Thing.h`, `Collision.h`, ... | Shared game structs (see "Game structs") |
| `src/port/` | Platform layer replacing the Dolphin SDK |
| `src/port/port.h` | Fixed-width types, endian helpers, 32-bit handles |
| `src/port/sdk.h`, `gx.h` | The SDK API surface the game uses |
| `src/port/gx_render.h` | Renderer interface; `gx_gl.c` (OpenGL) and `gx_sdlgpu.c` (SDL_GPU) implement it |
| `src/port/shaders/` | Shader sources (GLSL), shared by every renderer |
| `src/port/gx_shaders.h` | Generated: the shaders as GLSL, SPIR-V, MSL and DXIL |
| `tools/` | Import and rewrite scripts (below) |

## Porting notes

### 64-bit hosts: pointers in 4-byte slots

The game stores pointers in 4-byte fields everywhere: asset files are loaded
as raw blobs with 32-bit pointers patched in place, and code walks structs by
byte offset (`*(u8 **)(car + 0x2C)`). On 64-bit hosts a pointer no longer fits,
so such slots hold a **handle** instead:

- memory from the game heap: `0x80000000 + offset` into the port's arena
  (so alignment arithmetic on the value still works, as on the GameCube);
- anything else (statics, functions, host objects): an entry in a registry,
  allocated on first use.

Conversions are explicit in the code:

| Macro | Meaning |
|-------|---------|
| `PTR(h)` / `HND(p)` | handle → pointer / pointer → handle |
| `P(Type, h)` | typed `PTR` |
| `SLOT_LOAD(T *, addr)` | read a pointer from the 4-byte slot at `addr` (was `*(T **)addr`) |
| `SLOT_STORE(addr, p)` | write a pointer into the slot at `addr` |
| `PTR32(T)`, `FPTR32(R, (A))` | a pointer *field* in a game struct (4 bytes) |
| `GETP(x->f)` / `SETP(x->f, p)` | read / write such a field as a typed pointer |

`PTR32` fields are unions, so any use that bypasses `GETP`/`SETP` (pointer
arithmetic, comparison, assignment) is a compile error rather than silent
integer math on a handle.

The build makes `-Wint-to-pointer-cast`, `-Wpointer-to-int-cast` and
`-Wint-conversion` errors, so every place that squeezes a pointer into an
integer, or the reverse, has to go through these.

### Game structs instead of offsets

The decomp reaches into objects by byte offset (`*(u32 *)(thing + 0x74)`).
Those are being replaced by structs in `src/game/include/`, one shared
definition per object, with `_Static_assert` on every known offset so a
wrong layout fails to compile:

| Header | What |
|--------|------|
| `Thing.h` | `Thing`, the 0xAC-byte header of every game object (velocity, transform, flags, slaves, vtable, list links); `ThingVSlot` + `THING_VFN` for vtable calls; `DebrisParams` |
| `Collision.h` | `CollisionOBox` (the pooled oriented box), `CollisionInfo` (a contact) |
| `ItemThing.h` | pickups: `ItemThing`, `ItemRecord`, `ItemParams` |
| `CarA.h` | cars, players, `MotionState`, cameras |
| `World.h`, `Trigger.h`, `ThingManager.h` | the world (tiles, teleporters, volumes, quads), triggers/actions, thing pools |
| `AI.h`, `Nav.h`, `Creature.h` | AI drivers, nav points, creatures |
| `Projectile.h`, `Debris.h`, `AttachThing.h`, `Destructible.h`, `KeyState.h` | weapons and effects |
| `Interface.h`, `Particle.h`, `WildCard.h` | menus/HUD (interfaces, their objects and definitions), particles, wildcard racks |
| `Gfx.h` | render window and state, geometry, queued 2D rectangles (`GDLRect`) |

Conventions:

- An object kind embeds the header as its first member: `Thing base;`
  (`Destructible`, `ItemThing`, `TracerThing`...); pass `&x->base` (or the
  object itself) where a `Thing *` is wanted.
- The world transform is `Vector4 mtxRows[4]` inside `Thing` (a `Matrix4`
  member would pad the struct to 0xB0); use `THING_MTX(t)` for the matrix and
  `t->pos` / `t->axis[i]` for its rows.
- Vtable calls: `THING_VFN(t, THING_V_MOVE, void (*)(Thing *))(t)`, not
  `SLOT_LOAD(void *, vtable + 0xC)`.
- Name a field only from what the code does with it; keep `unkXX` / `padXX`
  (with the offset in the comment) when that isn't clear. Note quirks of the
  original rather than fixing them, and mark deliberate deviations with
  `port:` (e.g. a value the original read from an uninitialised register).
- After changing signatures, regenerate `game_protos.h`
  (`tools/gen_protos.py`).
- After giving a pointer field a struct type, run `tools/check_ptr_offsets.py`:
  leftover `GETP(x->field) + 0x24` byte offsets would now step whole structs
  (this caused an integer division by zero, SIGFPE on x86 Linux, that ARM64
  silently ignores; `-fsanitize=integer-divide-by-zero` catches those on a Mac).
- `tools/offsets_to_fields.py` rewrites a function's byte offsets into field
  accesses from the layouts clang computes (`-Xclang -fdump-record-layouts`);
  read its diff, since it trusts the variable types it is given.

Status: every game file uses structs. `tools/check_ptr_offsets.py --bytes`
lists the byte offsets still used as struct accesses; it reports only
`data_gen.c` (generated tables) and `Port_Swap.c` (which walks raw big-endian
pack data on purpose). Some globals are still declared as byte arrays in
`data_gen.c` and given their type by a `#define` in the function using them.

Shared layouts live in the headers: `Thing.h`, `CarA.h` (cars, players),
`Motion.h` (animated models and `MotionState`), `World.h`, `Collision.h`,
`AI.h`, `Creature.h`, `Gfx.h` (render window and state, geometry),
`Shader.h`, `Voice.h`, `IT.h`, `SoundSequence.h`, `Interface.h`, and the
rest of `common.h`. Before writing a local `typedef struct` view of an
object, check whether one of them already has it.

Restructured functions no longer match the decomp text, so
`import_decomp.py --merge` will conflict on them; resolve those by hand.

### Big-endian data

GameCube data is big-endian. Loaders byte-swap known formats on load
(`BE16`/`BE32`, `Port_SwapArray*` in `port.h`); ARAM sample data stays
big-endian and the mixer reads it with `Port_ReadBE16`.

### Threads

GameCube threads are cooperative on one CPU and the game relies on that. Each
OS thread is an SDL thread, but game code runs under one "giant lock" that is
released only where a GameCube thread would block (vsync, DVD, messages), so
the streaming loader runs while the main thread waits for retrace, as before.

## Tools

Run in this order on freshly imported sources (each is idempotent):

1. `tools/import_decomp.py`: copy game files from the submodule, apply
   CodeWarrior → GCC syntax fixes. Won't overwrite existing files without
   `--force`.
2. `tools/strip_sdk_decls.py src/game/*.c src/game/include/*.h`: drop the
   game's ad-hoc SDK/libc prototypes in favour of `sdk.h`.
3. `tools/gen_protos.py --strip`: generate `game_protos.h` from function
   definitions and drop local re-declarations.
4. `tools/slotify.py src/game/*.c`: rewrite raw pointer-slot accesses to
   `SLOT_LOAD`/`SLOT_STORE`.
5. `tools/fix_casts.py BUILD_DIR`: wrap pointer↔integer conversions reported
   by GCC in `PTR`/`HND` (see the script for the SARIF build setup).
6. `tools/fieldify.py src/game/*.c src/game/include/*.h`: pointer fields →
   `PTR32`, accesses → `GETP`/`SETP`; then repeat
   `ninja -C build -k 0 > build.log 2>&1; tools/fieldify.py --revert build.log`
   until it reverts nothing (undoes same-named non-pointer fields).
7. `tools/view_externs.py build.log`: where functions in one file declare
   the same global with different types (CodeWarrior allowed it, GCC
   doesn't), keep each function's type as a function-local view macro.
8. `tools/gen_data.py NAME...`: C definitions for globals that exist only in
   the decomp's split data sections (needs the decomp built, for
   `build/HOTWHE/asm`). Pointer entries become handles at startup.

Debugging aids:

- `cmake -DHWVX_SWAPCHECK=ON` (in its own build dir): every game read of pack
  data is checked against how it was swapped; mismatches are logged once per
  code address with the pack offset (`addr2line -e hwvx -f -i -p PC`).
- `HWVX_AUTOINPUT="700:start,+400:start,+150:a"` holds a button on
  controller 1 for 10 frames: `N:btn` at frame N, `+N:btn` once the game has
  gone N frames without reading the disc after the previous press (so
  loading times don't shift the script); `btn*N` holds for N frames
  instead of 10;
- `HWVX_DUMP=dir` saves every 60th frame as a BMP; `HWVX_GXDEBUG=1` draws all
  geometry magenta with depth, blending and culling off.
- `HWVX_GPUDEBUG=1` creates the SDL_GPU device in debug mode (Vulkan
  validation layers, D3D12 debug layer) for the SDL_GPU renderers.
- Run with `stdbuf -oL` when piping the log, or it's lost on a crash.
- `HWVX_CHEATS=1` turns on every cheat (save data is left alone).
- `HWVX_FPSLOG=1` logs the frame rate (capped at 60 Hz) and the game's average frame delta every 5 s.
- `HWVX_NO_DIALOG=1` skips the error dialog on a fatal error (scripted runs).

`tools/gen_shaders.py` regenerates `src/port/gx_shaders.h` after a change in
`src/port/shaders/`: GLSL for OpenGL, and glslang → SPIR-V (Vulkan),
SPIRV-Cross → MSL (Metal) and HLSL → DXC → DXIL (Direct3D 12). The header is
committed, so building the game needs none of these tools. It looks for them
on `PATH` and in `~/.local/opt/shadertools/{bin,dxc/bin}` (`--tools DIR`);
glslang and SPIRV-Cross build from the Khronos sources with CMake, and DXC
has prebuilt Linux releases (keep its `lib/` next to `bin/`: `libdxil.so`
signs the DXIL, which Direct3D 12 requires).

`tools/xgc_inspect.py FILE.xgc [--dump OFFSET N]` shows a data pack's TOC,
header, root objects and which words are pointers; use it when writing swap
walkers in `src/game/Port_Swap.c`.

`tools/asm_args.py NAME...` reports which argument registers a function reads
in the original, and which it passes straight on to a call; use it to settle
call sites that disagree with a definition.

`gen_protos.py` also declares functions whose signatures use a type local to
their own file, with those pointers as `void *`; the declaration is hidden from
that file and from files that declare the function with their own copy of the
type (CMake defines `HWVX_SRC_<file>` for each game source).

### Updating from the decomp

`tools/import_decomp.py --merge` three-way merges the decomp's changes since
`src/game/DECOMP_REVISION` into the patched files (base: old decomp file,
ours: `src/game`, theirs: new decomp file). New code arrives unrewritten;
resolve any conflicts, then rerun steps 2–7 (all idempotent). Run `fieldify.py`
with `--only <merged files>` so files the merge didn't touch keep their hand
fixes (the script prints the command).

## Status

Work in progress: it plays the intro movies, boots to the title screen, the
menus work with gamepad or keyboard, and races (including the attract-mode
demo) run with graphics, sound and music.

- [x] Build system, platform layer: window/GL context, VI frame pacing,
      threads, heap arena + handles, DVD → data dir, CARD → save files,
      PAD → SDL gamepad/keyboard, AX mixer → SDL audio stream, MTX/VEC
- [x] Pointer slots, pointer↔integer conversions and struct pointer fields
      converted to 32-bit handles (tool-driven, ~9k sites)
- [x] Game sources compile and link on 64-bit (x86-64 Linux, GCC)
- [x] Decomp code merged in up to the latest decomp commit (see
      `src/game/DECOMP_REVISION`)
- [x] Globals from the original data sections (`src/game/data_gen.c`)
- [ ] Byte-swapping of asset formats on load (`src/game/Port_Swap.c`): pack
      container and tables, textures, SFX, shaders, the MCP, interfaces,
      worlds (cars, quads, items, lights) and cars are done; triggers/actions,
      animation key data and geometry are next
- [x] GX renderer: FIFO/display-list vertex decoding, all texture
      formats, matrices, texgen, lighting channels, TEV, blending, fog
      (no render-to-texture or indirect texturing yet), on OpenGL
- [ ] SDL_GPU renderers: Vulkan tested on Linux; Metal and Direct3D 12
      untested
- [x] Music
- [x] THP movies
- [ ] Aux-bus reverb, ADPCM
- [ ] Logitech wheels / force feedback via SDL haptics (no wheel reported yet)
- [ ] IPL ROM font for the debug text printer (empty font for now)

### Missing game code

The decomp now has C for every game function; a few are still m2c-quality
drafts that need checking against the assembly when they misbehave.
