/* Portability layer shared by the game code and the SDL3 platform backend.
 *
 * - Fixed-width integer types. CodeWarrior's `long` was 32 bits; on LP64 hosts
 *   (Linux/macOS x86_64 and arm64) it is 64, so the decomp's s32/u32 typedefs
 *   are redefined on <stdint.h>.
 * - Endian helpers. GameCube data is big-endian.
 * - 32-bit handles. Game data stores pointers in 4-byte slots (files are
 *   relocated in place, structs are walked with raw offsets). On 64-bit hosts a
 *   pointer does not fit, so such slots hold a handle instead: an offset into
 *   the game heap arena, or an index into a table of registered static objects
 *   and functions. Use PTR()/HND() wherever the game turns a slot into a pointer
 *   or a pointer into a slot.
 */
#ifndef PORT_H
#define PORT_H

#include <stddef.h>
#include <stdint.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile s32 vs32;
typedef volatile f32 vf32;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- endianness ------------------------------------------------------- */

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PORT_BIG_ENDIAN 1
#else
#define PORT_BIG_ENDIAN 0
#endif

static inline u16 bswap16(u16 v) { return __builtin_bswap16(v); }
static inline u32 bswap32(u32 v) { return __builtin_bswap32(v); }

/* Convert between big-endian (disc/GameCube) and host order. */
#if PORT_BIG_ENDIAN
#define BE16(v) ((u16)(v))
#define BE32(v) ((u32)(v))
#else
#define BE16(v) bswap16((u16)(v))
#define BE32(v) bswap32((u32)(v))
#endif

/* Load big-endian values from unaligned memory. */
static inline u16 Port_ReadBE16(const void *p) {
    const u8 *b = (const u8 *)p;
    return (u16)((b[0] << 8) | b[1]);
}
static inline u32 Port_ReadBE32(const void *p) {
    const u8 *b = (const u8 *)p;
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}
static inline f32 Port_ReadBEF32(const void *p) {
    union { u32 u; f32 f; } c;
    c.u = Port_ReadBE32(p);
    return c.f;
}

/* In-place swaps for whole arrays of 16/32-bit big-endian words. */
void Port_SwapArray16(void *p, size_t count);
void Port_SwapArray32(void *p, size_t count);

/* ---- 32-bit handles --------------------------------------------------- */

/* Arena handles start here so their numeric values (and alignment math done on
 * them) look like the GameCube's cached MEM1 addresses. */
#define PORT_ARENA_HANDLE_BASE 0x80000000u
/* Registered statics/functions get handles from here up to the arena base,
 * spaced so that `handle + fieldOffset` still resolves inside the object. */
#define PORT_STATIC_HANDLE_BASE 0x01000000u
#define PORT_STATIC_HANDLE_STRIDE 0x1000u

extern u8 *g_portArena;
extern u32 g_portArenaSize;

void *Port_HandleToPtrSlow(u32 h);
u32 Port_PtrToHandleSlow(const void *p);

static inline void *Port_HandleToPtr(u32 h) {
    if (h - PORT_ARENA_HANDLE_BASE < g_portArenaSize) {
        return g_portArena + (h - PORT_ARENA_HANDLE_BASE);
    }
    return h == 0 ? NULL : Port_HandleToPtrSlow(h);
}

static inline u32 Port_PtrToHandle(const void *p) {
    size_t off = (size_t)((const u8 *)p - g_portArena);
    if (off < g_portArenaSize) {
        return PORT_ARENA_HANDLE_BASE + (u32)off;
    }
    return p == NULL ? 0 : Port_PtrToHandleSlow(p);
}

/* PTR(h): 4-byte slot value -> pointer. HND(p): pointer -> 4-byte slot value. */
#define PTR Port_HandleToPtr
#define HND Port_PtrToHandle
/* Typed pointer from a slot value, e.g. P(Thing, car->target). */
#define P(type, h) ((type *)PTR(h))
/* An address given either as a pointer or as a 32-bit handle (decomp code
 * keeps object addresses in integers just as often as in pointers). */
static inline void *Port_AsPtr(const void *p) { return (void *)p; }
#define PORT_ADDR(a)                                                            \
    _Generic((a), signed char: Port_HandleToPtr, unsigned char: Port_HandleToPtr, \
             short: Port_HandleToPtr, unsigned short: Port_HandleToPtr,          \
             int: Port_HandleToPtr, unsigned int: Port_HandleToPtr,              \
             long: Port_HandleToPtr, unsigned long: Port_HandleToPtr,            \
             long long: Port_HandleToPtr, unsigned long long: Port_HandleToPtr,  \
             default: Port_AsPtr)(a)

/* Load/store a pointer through the address of a 4-byte slot, replacing the
 * decomp's *(T **)addr reads and writes. */
#define SLOT_LOAD(type, addr) ((type)PTR(*(u32 *)PORT_ADDR(addr)))
#define SLOT_STORE(addr, p) (*(u32 *)PORT_ADDR(addr) = HND(p))

/* Pointer fields in game structs. `T *field;` is declared `PTR32(T) field;`:
 * 4 bytes holding a handle, with the pointee type carried by a zero-length
 * array so GETP() can hand back a properly typed pointer. Being a union, any
 * use that skips GETP/SETP (arithmetic, comparison, assignment) fails to
 * compile instead of silently doing integer math on a handle. */
#define PTR32(T) union __attribute__((packed, aligned(4))) { u32 h; T *_t[0]; }
#define FPTR32(R, ARGS) union __attribute__((packed, aligned(4))) { u32 h; R(*_t[0]) ARGS; }
#define GETP(f) ((__typeof__((f)._t[0]))Port_HandleToPtr((f).h))
#define SETP(f, p) ((f).h = Port_PtrToHandle(p))

/* Size checks from the decomp only hold where pointers are 4 bytes. */
#if UINTPTR_MAX == 0xFFFFFFFFu
#define PORT_PTR32 1
#define PORT_ASSERT_SIZE32(cond, name) typedef char name[(cond) ? 1 : -1]
#else
#define PORT_PTR32 0
#define PORT_ASSERT_SIZE32(cond, name) typedef char name[1]
#endif

/* ---- diagnostics ------------------------------------------------------ */

/* These format through SDL (C99 conversions such as %zu everywhere); MinGW's
 * plain `printf` format checks Microsoft's runtime instead. */
#ifdef __MINGW32__
#define PORT_PRINTF(f, a) __attribute__((format(gnu_printf, f, a)))
#else
#define PORT_PRINTF(f, a) __attribute__((format(printf, f, a)))
#endif

void Port_Log(const char *fmt, ...) PORT_PRINTF(1, 2);

/* text_input.c: typing into a menu. */
void Port_TextInputStart(char *buf, size_t cap);
void Port_TextInputStop(void);
int Port_TextInputPaste(void);
void Port_LoadOnlineAddress(char *buf, size_t cap);
void Port_SaveOnlineAddress(const char *address);
void Port_TextCopy(const char *text);

/* tunnel.c: a Cloudflare quick tunnel to a local port. Start: 0 if there's no cloudflared
 * (or none can run here). Poll: 1 and its name when others can reach it, 0 until
 * then, -1 if it ended. */
int Port_TunnelAvailable(void);
int Port_TunnelStart(u16 port);
int Port_TunnelPoll(char *name, size_t size);
void Port_TunnelStop(void);
int Port_AllCheats(void);
/* port.c: the player's folder, SDL's pref path or HWVX_PREF_DIR (SDL_free it). */
char *Port_PrefPath(void);
/* fileio.c: `size` bytes to `path`, on the disk before it returns; `from`
 * renamed over `to`, replacing it (Windows too). 1 on success, else SDL_GetError. */
int Port_WriteFileSynced(const char *path, const void *data, size_t size);
int Port_ReplaceFile(const char *from, const char *to);
/* --reset-save (main_sdl.c): 1 when this boot starts from the default game
 * data (GameSetting_Init) and skips the autoload of the newest save;
 * GameSettings_PortAutoLoad reads it. */
int Port_ResetSave(void);
/* Data formats (dvd.c, game/Port_Data.c). The releases share the BrainCore
 * pack format and the file names; a data folder can mix them file by file.
 * dvd.c finds each file under its GameCube name first, then the others'. */
enum {
    PORT_DATA_GC,   /* .xgc: big-endian, GX geometry and textures */
    PORT_DATA_PC,   /* .xpc: little-endian, OpenGL triangle lists, DXT textures */
    PORT_DATA_PS2,  /* .xps: little-endian, VIF strips, paletted textures, SPU-ADPCM (Port_PS2.c) */
    PORT_DATA_COUNT
};
/* dvd.c: the release (PORT_DATA_*) whose packs the game runs on, from the
 * last pack opened; for the few places the releases' code differs. */
int Port_DataRelease(void);
/* The PC release's texture formats (pack header u16 0) as Texture.format
 * (Render_Texture_.c) once Port_Data.c has read them: PORT_TEX_PC | format,
 * clear of the GameCube's 0-4. gx_tex.c decodes them as port-only GX formats,
 * GX_TF_PORT | format. */
enum {
    PORT_TEX_PC = 0x100,
    PORT_TEX_PC_RGB8 = 0,       /* RGB888, row-major */
    PORT_TEX_PC_RGBA8 = 1,      /* RGBA8888, row-major */
    PORT_TEX_PC_DXT1 = 4,       /* drawn as RGB: the PC uploads it without alpha */
    PORT_TEX_PC_DXT5 = 5,
    GX_TF_PORT = 0x40,
};
static inline int Port_PCTextureGXFormat(u32 texFormat) {
    return GX_TF_PORT | (int)(texFormat & 0xF);
}
/* gfx_settings.c: draw the PC release's textures with the GameCube's colours
 * (its texels are the PC's squared, gc = pc * pc / 255), or as the PC drew
 * them. graphics.cfg "pctextures=gc|pc", HWVX_PC_TEXTURES overrides. */
int Port_PCTexturesLookGC(void);
/* gfx_settings.c: leave the main menu as the release has it (no Quit Game or
 * Online rows, src/game/Port_Menu.c). graphics.cfg "mainmenu=original|port",
 * HWVX_ORIGINAL_MAINMENU=1|0 overrides. */
int Port_OriginalMainMenu(void);
/* gfx_settings.c: split screen draws every model one LOD level coarser, as
 * the GameCube did (Gfx_GeometryHandler.c). Default off: full detail, as the
 * PC release at high Geometry Quality. graphics.cfg "splitlod=full|gc",
 * HWVX_SPLITSCREEN_LOD=gc|full overrides. */
int Port_SplitScreenCoarseLOD(void);
/* Keyboard (pad.c): the keys the game reads, as bits (1 << PORT_KEY_*).
 * src/game/Port_Keyboard.c turns them into the PC release's controls. */
enum {
    PORT_KEY_UP, PORT_KEY_DOWN, PORT_KEY_LEFT, PORT_KEY_RIGHT, PORT_KEY_ENTER, PORT_KEY_ESCAPE,
    PORT_KEY_SPACE, PORT_KEY_CTRL, PORT_KEY_SHIFT, PORT_KEY_A, PORT_KEY_D, PORT_KEY_W, PORT_KEY_S,
    PORT_KEY_GRAVE, PORT_KEY_X,
};
u32 Port_KeyboardKeys(void);
/* port.c: a repeatable run (HWVX_DETERMINISTIC, HWVX_SEED, HWVX_FIXEDSTEP):
 * the random seed to use (fallback when none is set), and the fixed frame
 * step in microseconds (0: measured time). */
u32 Port_Seed(u32 fallback);
u32 Port_FixedStepUs(void);
/* The keys the operating system repeated (key-down events with the repeat
 * flag) since the last call, as the same bits; reading clears them. The PC
 * release's menus step once per such repeat (its WM_KEYDOWN path). */
u32 Port_KeyboardRepeats(void);
/* Widescreen (gx_draw.c): how much wider (wx) and taller (wy) than 4:3 the
 * game's single view is drawn; 1, 1 in split screen. The game's culling and
 * on-screen tests (Render_SetClipPlanes, isInWindow) widen by these. */
void Port_ViewWiden(f32 *wx, f32 *wy);

/* Display-list cache (gx_fifo.c): the GX layer decodes each display list once
 * and keeps the result, including the vertices it read out of game memory. Game
 * memory that is rewritten or reused - a pack swapped into place, a freed heap
 * block - has to be reported here, or a cached decode keeps serving what the
 * old bytes said. Port_Swap.c and os.c do both. */
void Port_InvalidateDisplayLists(const void *p, u32 size);

/* Graphics settings (gfx_settings.c), edited by the Options screen's
 * "Graphics" menu (src/game/Port_Menu.c) through these code variables
 * (GameSettings_Get/SetCodeVariable). */
#define PORT_CODEVAR_RESOLUTION 0x1000   /* index into the mode list */
#define PORT_CODEVAR_DISPLAY 0x1001      /* 0 windowed, 1 fullscreen */
#define PORT_CODEVAR_VSYNC 0x1002        /* 0 off, 1 on: wait for the display */
#define PORT_CODEVAR_MAXFPS 0x1003       /* index into the frame rate choices */
#define PORT_CODEVAR_GAMMA 0x1004       /* 0 .. 100, a volume-style bar: 50 leaves the picture alone */
#define PORT_CODEVAR_FOV 0x1005         /* 0 .. 100, a volume-style bar: 50 is the game's own view */
#define PORT_IS_CODEVAR(id) ((id) >= PORT_CODEVAR_RESOLUTION && (id) <= PORT_CODEVAR_FOV)
/* The content registry's (src/game/Port_Content.h): code variable 0x65 (a
 * track's unlock) for the entry a world select of progression P numbers
 * `arg`: PORT_CODEVAR_TRACK + P. The selects number a mode's tracks in a
 * row (its family's, then the registered ones), the registry anywhere. */
#define PORT_CODEVAR_TRACK 0x1100
#define PORT_IS_TRACK_CODEVAR(id) ((id) >= PORT_CODEVAR_TRACK && (id) < PORT_CODEVAR_TRACK + 0x100)
/* The Gamma and FOV rows reuse the volume rows' bar: an object role that stretches
 * the bar by the code variable (roles 102/103 are the volumes), and an event type
 * that adds the event's delta to the code variable named by its id, clamped to
 * 0 .. 100 (types 0x16/0x17 are the volumes). See InterfaceManager.c. */
#define PORT_ROLE_GAMMA_BAR 105
#define PORT_ROLE_FOV_BAR 106
#define PORT_EVENT_SLIDER 0x40
#define PORT_MAX_CHOICES 10              /* the widest of those choice lists */
u32 Port_GfxGet(s32 id);
void Port_GfxSet(s32 id, u32 value);
void Port_GfxMenuOpened(void);
u32 Port_GfxModeCount(void);
const char *Port_GfxModeName(u32 i);
/* The choice lists of the other rows, VSync and Max FPS: how many options
 * there are, and each one's name. The option's index is what the row's code
 * variable holds, so only this side knows the rates behind the names. */
u32 Port_GfxOptionCount(s32 id);
const char *Port_GfxOptionName(s32 id, u32 i);
void Port_GfxApply(void);
/* What the FOV row multiplies the player's camera by (CameraManager.c). */
f32 Port_GfxFovScale(void);
void Port_Fatal(const char *fmt, ...) PORT_PRINTF(1, 2) __attribute__((noreturn));
#define PORT_TODO(what) Port_Todo(__func__, what)
void Port_Todo(const char *func, const char *what);

#ifdef __cplusplus
}
#endif

#endif /* PORT_H */
