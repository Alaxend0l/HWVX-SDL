# HWVX-SDL
Turning the GC Decompilation of HWVX into a portable codebase for modding and porting!

The game code comes from the [HWVX-GC-Decomp](HWVX-GC-Decomp) submodule. The
GameCube SDK it was written against (OS, GX, DVD, PAD, CARD, AX...) is replaced
by a platform layer on **SDL3** (window, input, files, threads),
**OpenGL 3.3** (graphics); sound goes through SDL3's audio streams. Everything is C, built
with GCC.

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

Options: `--data DIR`, `--scale N` (window = 640×480×N), `--fullscreen`.
F11 or Alt+Enter toggles fullscreen. Saves go to SDL's per-user preferences
folder (`memcardA/*.sav`).

Controls: any SDL-supported gamepad (A/B/X/Y by position, triggers = L/R,
right shoulder = Z), or keyboard on player 1: arrows/WASD steer, Space/Z = A,
X = B, C = X, V = Y, Q/E = L/R, Shift = Z, Enter = Start.

## Layout

| Path | What |
|------|------|
| `src/game/` | Game sources imported from the decomp, patched for portability |
| `src/game/include/game_protos.h` | Generated: one prototype per game function |
| `src/game/include/Thing.h`, `Collision.h`, ... | Shared game structs (see "Game structs") |
| `src/port/` | Platform layer replacing the Dolphin SDK |
| `src/port/port.h` | Fixed-width types, endian helpers, 32-bit handles |
| `src/port/sdk.h`, `gx.h` | The SDK API surface the game uses |
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
| `CarA.h` | cars, players, `MotionState` |

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

Converted so far: `Thing.c`, `ItemThing.c`, `Tracer.c`, the collision files'
box/contact types, the slave code in `AnimHandler.c`, and `Destructible`'s
header. The rest still uses offsets.

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
- Run with `stdbuf -oL` when piping the log, or it's lost on a crash.
- `HWVX_CHEATS=1` turns on every cheat (save data is left alone).
- `HWVX_FPSLOG=1` logs the frame rate (capped at 60 Hz) and the game's average frame delta every 5 s.
- `HWVX_NO_DIALOG=1` skips the error dialog on a fatal error (scripted runs).

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
- [x] GX → OpenGL renderer: FIFO/display-list vertex decoding, all texture
      formats, matrices, texgen, lighting channels, TEV, blending, fog
      (no render-to-texture or indirect texturing yet)
- [x] Music
- [x] THP movies
- [ ] Aux-bus reverb, ADPCM
- [ ] Logitech wheels / force feedback via SDL haptics (no wheel reported yet)
- [ ] IPL ROM font for the debug text printer (empty font for now)

### Missing game code

The decomp now has C for every game function; a few are still m2c-quality
drafts that need checking against the assembly when they misbehave.
