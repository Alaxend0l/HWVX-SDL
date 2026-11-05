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

Packages for Windows, macOS, Linux (AppImage, Flatpak, bare program; x86_64
and aarch64) and Android are built by the scripts in `packaging/`, and by CI
on every push (artifacts) and version tag (GitHub Release): see
[packaging/README.md](packaging/README.md).

## Game data

Not included. Use your own copy of the GameCube disc, either as a disc image
(`.iso`; Dolphin can convert a `.gcz`/`.rvz` to one: right-click the game →
Convert File…) or as its extracted files (Dolphin: right-click the game →
Properties → Filesystem → Extract Entire Disc, which makes a `files/`
folder). On start the game looks for it, in order:

1. `--data PATH` (the image, or the folder);
2. what it used last time (`datadir.txt` in the save folder);
3. an `.iso` or a `files/` folder next to the program (on macOS also next to
   the `.app`), in `orig/` in the current folder, or in the save folder; on
   Android in `Android/data/io.github.alaxend0l.hwvx/files/` (or a `files/`
   folder in it) and
   `/sdcard/HWVX/` (the second needs "All files access", which the app asks
   for);
4. on desktop, it asks you to choose the image or the folder.

```sh
./build/hwvx --data "/path/to/Hot Wheels - Velocity X.iso"
./build/hwvx --data /path/to/files
```

Options: `--data PATH`, `--scale N` (window = 640×480×N), `--window WxH`
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

Saves: there's no memory card on PC, so the GameCube's card screens are
skipped: the startup card check, the Slot A/B picker (saves always go to the
save file), the "do not touch" loading/saving notices and the card warnings
(error screens still appear if loading or saving fails). The most recent save
loads at startup, and leaving Options after changing a setting autosaves over
the save in use (or makes one) instead of reminding you to save.

Renderers (`--renderer`): through SDL_GPU `d3d12` (the default on Windows),
`metal` (the default on macOS) and `vulkan` (the default on Linux and
Android), and `opengl` (OpenGL 3.3 core). When the default can't start (no
driver for it, an old GPU), the game falls back to OpenGL, except on
Android. CMake's `HWVX_DEFAULT_RENDERER` builds in another default.
F11 or Alt+Enter toggles fullscreen. Saves go to SDL's per-user preferences
folder (`memcardA/*.sav`).

Controls: any SDL-supported gamepad (A/B/X/Y by position, triggers = L/R,
right shoulder = Z), or the keyboard as player 1, with the PC release's
controls (recovered from its executable, see `src/game/Port_Keyboard.c`):
Up accelerate, Down brake/reverse, Left/Right steer, W/S stunt flip
forward/backward, A/D strafe/stunt roll, Shift power slide, Space fire,
Ctrl change weapon, ` (grave) change camera, Escape pause. In menus: arrows,
Enter to accept, Escape to go back, X for the X button (stats).

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
| `src/port/net.h`, `net*.c` | Online play transport (see "Online play"); nothing in the game calls it yet |
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

### Menu rows, options and code variables

Menus are data: an `InterfaceHeader` with its objects, variables and events
lives in a pack, and `InterfaceManager` runs it (`src/game/include/Interface.h`
documents the structs, `InterfaceManager_Update` is the frame loop). A port
screen is a *copy* of that table made when the interface is created
(`Port_MenuPatch` ← `InterfaceManager_Create`, `src/game/Port_Menu.c`): the
pack is mapped read-only, nothing may be edited there.

A selectable row (Difficulty, Sound Mode, Music, SFX, ...):

- objects `label`, `value`, `arrow left`, `arrow right`; in the Options screen
  (40 objects) they start at 19, 23, 27 and 31, so row *n* is at
  `19 + 4*n` and its value at `20 + 4*n`. Difficulty/Sound are label text +
  value text; Music/SFX are label image + value image.
- `record` (`IfaceRecord`) drives selection: `id`, `next[4]` =
  up/down/left/right (0: stop there, the port re-rings these when it respaces
  rows), `steps`/`numSteps`, `events`.
- d-pad in `InterfaceManager_Update`: presses at 1532-1535, `steps` applied at
  1537-1568 (`vars[step->var]` += `val[dir]`, or = when `set[dir]`; wrapped
  into the variable's min/max at 1561-1564), then variables whose code variable
  id is `0x1F6` are re-read from the game settings (1569-1590), then the
  record's events run (1607), then `next[]` moves the selection (1624+). Left
  and right never move the selection on this screen: they change the row's
  value, either through a step (Difficulty: `var 0`, Sound: `var 3`, both
  `val = {0,0,-1,1}`, `set = {0,0,0,0}`) or through the event itself
  (Music/SFX: no steps, four events - 13 and 14 each have a def of type `0xB`
  plus a def of type `0x16`/`0x17` whose `delta` (-10/+10) changes the volume
  code variables `0xCF`/`0xD0`).
- events: event types 13/14 are the left/right presses and 15 is A; each
  event's def says what happens. Def type `0xB` runs the action list its
  `target` points at, and `IfaceActionList` type 2 with an `IfaceCodeVar`
  writes the row's variable into a game code variable - which is how the
  Graphics rows apply their setting.
- variables (`IfaceVarList`): Options has 4 - 0 difficulty (code var 200/0xC8,
  min 0 max 3), 1 the selected row (type 3, min 1 max 6), 2 type 1 / update 0
  (code var 106/0x6A), 3 sound mode (code var 214/0xD6, min 0 max 1). The port
  appends 4 = vsync and 5 = max fps by cloning 3, and `SetCodeVar` retargets
  the code var id and min/max - every row whose value is a variable needs
  that, not only the row whose events are rewritten, because both the creation
  sync (1491-1531) and the step wrap read it.
- the value shown: `IfaceText` with `listMode` 1 shows
  `GETP(list->texts)[vars[list->var]]` (clamped, 3349+), so a row can display
  names the port made up - `Port_GfxOptionName` - as long as the object is
  `type = IFACE_DEF_TEXT`, `role = 9`. The gate at 3349 wants the *live*
  `kind == 5` (`IFACE_KIND_TEXT`, which `InterfaceManager_AddObject` gives a
  def of type 1) and `listMode == 1`; the string is only re-set when
  `idx != obj->listIndex`, which starts at -1, so the first update always
  shows the list entry.
- how an object comes to life (`InterfaceManager_AddObject`, 591-752, run for
  *every* def of the header including hidden ones): def type 0 -> `kind`
  0/2/1 by `sub->type` (plain image, geometry, texture anim); def type 1 ->
  `kind = 5`, `data = sub`, `fontId = InterfaceManager_AddFont(sub->font)`
  and the initial text from `sub->strings`. `AddObject` dereferences
  `def->anim` unconditionally (633) - a def with a null anim kills the
  interface. `IFACE_DEF_*` and `IFACE_KIND_*` are different enums; text is
  def 1 but kind 5.
- the Music/SFX value objects (28/32) are `type = 0` images with role
  103/102, and for those roles the engine ignores text entirely: it fetches
  code var `0xD0`/`0xCF` (3269-3275) and uses it as the animation delta that
  stretches the bar. To make such an object show text, copy the Display row's
  value object (24: `role = 9`, `type = 1`) - anim (in the port's `AnimCopy`,
  with the position key raised to the new row height), `stateIndex`,
  `states`, `anchor`/`anchorX`/`anchorY` - or the text lands in the wrong
  place and colour. The pack's own shapes (dumped with `tools/xgc_inspect.py`
  over the blob): 24 role 9 text, active 0, 2 states (type 0 and 1),
  `anchorX = 1`; 28 role 103 image, 1 state, a 2-key scale track whose first
  key is `(0.0, 16.0)` - the bar starts at width 0 and the engine stretches
  it by the volume - `anchorX = -1`; 32 role 102 image, 2 states (type 0 and
  **2**); all three `stateIndex = {-1 x6}` and condition lists
  `type = 1, count = 0` (no conditions: always visible), while the arrow
  defs (25/26/29/30/33/34) each carry one `type = 1` condition whose `value`
  is the row's record id - arrows that only show for the selected row.

Pack offsets, if the tables need re-deriving: menu pack `HW_IPack.xgc` entry 5,
main menu header at blob `0x6ECC60` (26 objects), Options header at blob
`0x85BB40` (40 objects, vars at `0x85DE20`); find them again with
`tools/xgc_inspect.py` by searching for a header with 40/26 objects whose
object 16/10's text is "Controller"/"Main Menu".

The volume fix for the two cloned rows lives in `SetActions`: every event def
of type `0xB`, `0x16` or `0x17` is retyped to `0xB` with `delta = 0` and
`target` the port's own action list, a row whose copied record had no step gets
a port-owned `IfaceStep` (`var = 4`/`5`, `val = {0,0,-1,1}`, `set` all 0), and
`SetCodeVar` covers vars 4/5 (`PORT_CODEVAR_VSYNC`/`PORT_CODEVAR_MAXFPS`,
max = option count - 1), so left/right there can no longer move the volume
code vars `0xCF`/`0xD0`. Verified in the menu: the rows cycle, up/down rings
the four rows, and leaving the screen applies and saves - correctly even after
the screen was closed and reopened, because the live vars and code vars stay in
step across the reload, and `vsync=`/`fps=` in `graphics.cfg` survived the next
launch.

The blank Max FPS value that preceded that is kept as a caution. The row
rendered empty although its def matched the working VSync row's shape exactly;
dumping the *live* `InterfaceObject`s - at creation and every 120 frames,
instead of re-reading pack data - showed the object perfectly healthy (kind 5,
role 9, visible, right position, colour and list index, the list gate firing)
but every string it picked was NULL. `SetObjectText` treats a NULL string as
empty text, `len 0`, without complaint: a row that lays out right yet shows
nothing is a resolved-string bug, not a def bug. The bug was one line in
`FpsNames()` - it formatted the names into static buffers but never pointed the
returned array at them.

Feature B - frame rates past 60 - needs no motion compensation after all.
The cap on the Max FPS row was the port being cautious: before the pacing was
measured it was not known how much of the game assumes one frame is a 60th of a
second. A survey of every delta-time consumer settled it: the car physics,
steering and stunts integrate through `g_fDeltaTimeSec` (CarA_HW.c), particles,
keyframe animation (`ExtractKeys.c`: every track advances `time += dt` in ms),
audio fades (AudioManager.c), the interface's timers and input repeat
(InterfaceManager.c) and the THP movie player (wall-clock: `thp.c` derives the
due frame from `SDL_GetTicksNS()`) all follow whatever delta the frame loop
gives them. `g_iDeltaTime60th` is fed but has no consumers. So `vi.c` just
paces at the chosen rate now, and the choices above 60 work: what the drawing
cannot fit in the shorter budget is dropped, not speeded - a 120 FPS choice on
a frame that costs 8.3-9.9 ms of work lands near 100, `graphics.cfg` still
remembers the choice. Measured: `--fps 120` gives 110 FPS while the work fits
8.33 ms and then drifts to ~98 with the game delta at ~8.8 ms; 30 and 60 are
unchanged. Watch list for anything a survey cannot see: gameplay feel at 144
and 240 (the integer-ms timers truncate harder as the delta nears their 1 ms
floor) and any logic that counts frames rather than time.

### Online play

Work in progress: Drag Race plays over a network, weapons and pickups included; Battle
hasn't been tried and Joyride isn't in. One host and its clients, each connected to the host only,
up to 8 players in the transport (the game itself stops at 4 for now: player pools,
score tables, spawns), because a Cloudflare tunnel only takes connections to the host.

Multi-Player now opens Online or Local; Local is the game's own screen. Hosting opens a
lobby, and the host's choices in the game's own menus bring everyone to the car select
(`src/game/Port_Online.c`, screens in `Port_Menu.c`). Online, every car, world and weapon
is open and nothing reaches a save. In a level each machine simulates its own car, and
the others follow its inputs and are pulled towards where it says it is. The world holds
after loading and each round reset until the host says go, and the host decides who
finished first. Tunnel hosting isn't in yet, and neither are AI opponents or typing an
address with only a controller. `HWVX_ONLINE_ADDRESS` fills in
the Connect screen, for testing with `HWVX_AUTOINPUT`.

The transport's API is `src/port/net.h`, the place to start reading: reliable messages, and "latest" ones where a newer message replaces
one not yet sent. All of it travels over WebSocket, the one thing a quick tunnel is
known to carry for a client without `cloudflared` of its own. A socket thread does
the waiting outside the giant lock, so connections live through level loads.

`wss://` (TLS, what a client needs to reach a tunnel) isn't in yet: only `ws://`,
which is enough to host behind `cloudflared`. Measured limits of a quick tunnel, which
the constants in `net.c` follow: a connection silent for about 125 s is closed, and
a new tunnel's name can fail to resolve for over 80 s if looked up in its first few
seconds, so hosts should wait about 15 s before showing it and clients keep retrying.

Tests need no game data or window, and link only the transport:

```sh
cmake -S . -B build-tests -DHWVX_TESTS=ON && cmake --build build-tests --target nettest
build-tests/nettest selftest     # protocol code, no sockets
build-tests/nettest e2e          # a host and clients as separate processes
```

`HWVX_NET_SIM` makes any session's connection bad, for example
`HWVX_NET_SIM=lat=40,jitter=20,loss=1,stall=200,bw=64` (see `src/port/net_sim.c`). For two
machines, run `nettest host PORT ROOM PEERS SECONDS` and `nettest client ADDRESS ROOM
COUNT SECONDS` (see `tools/nettest.c`).

`tools/net_hostile.py` attacks a host with protocol violations, and
`tools/net_tunnel_check.py` talks to one through a real tunnel; run the first
against a sanitizer build. Test optimized builds as well: a masking loop once passed
at `-O0` and failed at `-O2`.

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
- `HWVX_FPSLOG=1` logs the frame rate and the game's average frame delta every 5 s.
- `HWVX_FPSHUD=1` draws an on-screen overlay, top left: current FPS, the average
  since boot, and the 1% low; frame gaps over 250 ms (movies, loads) don't count.
- `HWVX_THREADTRACE=1` logs every background world/car load: how long it held
  the giant lock, and how long the frame that started it waited for it. A load
  that never runs, or a wait of many milliseconds, means the loader thread is
  being starved; see `Port_GiantLock` in `src/port/os.c`.
- `HWVX_NO_DIALOG=1` skips the error dialog on a fatal error (scripted runs).
- `HWVX_NO_TEVGEN=1` draws everything with the general pixel shader
  (`gx.frag`) instead of the ones specialised to each TEV setup
  (`src/port/gx_tevgen.h`), to compare. `HWVX_STATS=1` logs how many draws
  used which.
- `HWVX_TEVKEYS=tools/tevkeys.txt` appends each TEV setup the game uses that
  the file doesn't have yet. SDL_GPU only has specialised shaders for the
  setups listed there (rerun `tools/gen_shaders.py`); OpenGL builds its own.
  `tools/tevcheck.sh tools/tevkeys.txt --random 2000 --exact` checks the
  specialised shaders against `gx.frag` (needs a display).

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
