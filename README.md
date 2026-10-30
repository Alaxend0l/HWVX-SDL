# HWVX-SDL
Turning the GC Decompilation of HWVX into a portable codebase for modding and porting!

The game code comes from the [HWVX-GC-Decomp](HWVX-GC-Decomp) submodule. The
GameCube SDK it was written against (OS, GX, DVD, PAD, CARD, AX...) is replaced
by a platform layer on **SDL3** (window, input, files, threads),
**OpenGL 3.3** (graphics) and **miniaudio** (sound). Everything is C, built
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

Work in progress: it builds and boots into the game's state machine, but
nothing is drawn and asset data is not byte-swapped yet.

- [x] Build system, platform layer: window/GL context, VI frame pacing,
      threads, heap arena + handles, DVD → data dir, CARD → save files,
      PAD → SDL gamepad/keyboard, AX mixer → miniaudio, MTX/VEC
- [x] Pointer slots, pointer↔integer conversions and struct pointer fields
      converted to 32-bit handles (tool-driven, ~9k sites)
- [x] Game sources compile and link on 64-bit (x86-64 Linux, GCC)
- [x] Decomp code merged in up to the latest decomp commit (see
      `src/game/DECOMP_REVISION`)
- [x] Globals from the original data sections (`src/game/data_gen.c`)
- [ ] Byte-swapping of asset formats on load (next: the game boots, loads
      `mcp.xgc` and runs its state machine, but reads big-endian fields as-is)
- [ ] GX → OpenGL renderer (vertex decoding, display lists, textures, TEV)
- [ ] THP movies (skipped for now), aux-bus reverb, ADPCM
- [ ] Logitech wheels / force feedback via SDL haptics (no wheel reported yet)
- [ ] IPL ROM font for the debug text printer (empty font for now)

### Missing game code

The decomp now has C for every game function; a few are still m2c-quality
drafts that need checking against the assembly when they misbehave.
