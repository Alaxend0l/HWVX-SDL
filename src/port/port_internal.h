/* Private header for the platform layer (src/port). */
#ifndef PORT_INTERNAL_H
#define PORT_INTERNAL_H

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port.h"
#include "sdk.h"
#include "gl_funcs.h"

/* Game heap. The GameCube had 24 MB of RAM; give the game room to spare since
 * host-side structures (e.g. decoded textures) never live here. */
#define PORT_ARENA_SIZE (128u << 20)

typedef struct PortState {
    const char *dataDir;
    int scale;
    int winW, winH;          /* --window WxH (0: 640x480 times scale) */
    int fullscreen;
    int vsync;               /* wait for the display before a frame is shown:
                              * 1 on, 0 off (main_sdl.c, the Graphics menu) */
    int maxFps;              /* frames a second, 30 .. 480, 0 for unlimited: the
                              * rate the game is paced at (main_sdl.c, the
                              * Graphics menu) */
    int gamma;               /* the Graphics menu's Gamma bar, 0 .. 100, 50: none */
    int fov;                 /* the Graphics menu's FOV bar, 0 .. 100, 50: the game's */
    int pcTexturesPC;        /* 1: the PC release's textures with its own colours, 0: the
                              * GameCube's (graphics.cfg pctextures; Port_PCTexturesLookGC) */
    int originalMainMenu;    /* 1: the main menu as the release has it, without the port's
                              * rows (graphics.cfg mainmenu; Port_OriginalMainMenu) */
    int moviesOff;           /* 1: skip the movies (graphics.cfg movies=off; thp.c) */
    int splitLodGC;          /* 1: split screen draws models a level coarser, as the
                              * GameCube did (graphics.cfg splitlod; Port_SplitScreenCoarseLOD) */
    int mouseSteer;          /* 1: the PC release's mouse steering in races (graphics.cfg
                              * mousesteer=on; Port_MouseSteerOn, pad.c) */
    int drawDist;            /* PORT_DRAWDIST_*: graphics.cfg drawdist (Port_DrawDistSetting) */
    int resScale;            /* g_portResScales index: graphics.cfg resscale (Port_ResScale) */
    int resNearest;          /* 1: graphics.cfg resfilter=nearest (Port_ResScaleNearest) */
    f32 resScaleOverride;    /* --res-scale, HWVX_RESSCALE (main_sdl.c; 0: graphics.cfg decides) */
    int launcher;            /* --launcher (or run as hwvx-launcher): open the launcher */
    volatile int quit;
    SDL_Window *window;
    const char *gpuDriver;   /* SDL_GPU driver for gx_sdlgpu.c: vulkan, metal, direct3d12 */
    int resetSave;           /* --reset-save (main_sdl.c): default game data, no autoload */
    int gcAirCamera;         /* --gc-air-camera or HWVX_GC_AIR_CAMERA=1 (main_sdl.c): a trigger
                              * orbit camera hands back to a tipped car, as on the GameCube
                              * and PS2 (Port_GCAirCamera, game/CameraManager.c) */
    int pcPan;               /* 1: the PC's pan law (graphics.cfg stereo=pc, the launcher's
                              * Settings; --pc-pan and HWVX_PC_PAN override it in Port_PCPan, game/Voice_.c) */
    const char *convertDir;  /* --convert-xdl DIR: convert the data to the .xdl cache there
                              * and exit (game/Port_Convert.c) */
    const char *program;     /* argv[0]: the launcher starts the converter with it */
} PortState;

extern PortState g_port;

void Port_InitHandles(u32 arenaSize);

/* The resolution scale (main_sdl.c): the EFB is the window times this
 * (gx_draw.c). --res-scale, else HWVX_RESSCALE, else graphics.cfg resscale=
 * (the launcher's Settings, Display). Nearest: a scale under 1x is shown
 * with nearest-neighbour instead of bilinear filtering (graphics.cfg
 * resfilter=nearest, HWVX_RESFILTER=nearest). */
f32 Port_ResScale(void);
int Port_ResScaleNearest(void);
/* The launcher's choices (g_port.resScale is the index); "1x" is the default. */
#define PORT_RESSCALE_COUNT 5
#define PORT_RESSCALE_DEFAULT 2
extern const f32 g_portResScales[PORT_RESSCALE_COUNT];
extern const char *const g_portResScaleNames[PORT_RESSCALE_COUNT];

/* Creates g_port.window with the renderer's flags (gx_render.h open). */
void Port_CreateWindow(SDL_WindowFlags flags);
int Port_TryCreateWindow(SDL_WindowFlags flags);   /* 0: failed (SDL_GetError) */

/* launcher.c: the game data to play (see there). The check resolves a place
 * to its game data (a folder, its files/ folder, a disc image in it) or
 * NULL; the search looks in the usual places on a first run. */
/* `rel` under `dir`, each '/'-separated component matched without case; the
 * real path or NULL (launcher.c). */
char *Port_FindNoCase(const char *dir, const char *rel);
typedef char *(*Port_DataCheck)(const char *path);
typedef char *(*Port_DataSearch)(void);
char *Port_Launcher(int open, int window, Port_DataCheck check, Port_DataSearch search);
/* mix.c: start a mixed build (Port_Mix.h) from the recipe `nameOrPath`
 * ("ultimate", or a .jsonc file); `building`: --mix-build. 0 and a message
 * in err when the recipe can't be used. Port_MixManifestClose: the build is done. */
int Port_MixSetup(const char *nameOrPath, int building, Port_DataCheck check, char *err, u32 errSize);
void Port_MixManifestClose(void);
/* --mix-build's end (mix.c): the build's built.cfg (PORT_MIX_BUILT: its
 * stamp, the cache version, the pack counts) written, and its .part folder
 * put in the place of the last complete build; prints "@convert N N done",
 * or "@error TEXT" and removes the .part. 1 when it is in place. */
#define PORT_MIX_BUILT "built.cfg"
int Port_MixBuildFinish(u32 packs, u32 failed);
/* The launcher's look at recipe `nameOrPath` without starting it: its name,
 * cache folder, stamp (Port_MixStamp) and how many of its releases are
 * imported (sources.cfg). 0 and err when it can't be used. */
typedef struct PortMixInfo {
    char name[64];
    char dir[1024];
    u32 stamp;
    int releases;
} PortMixInfo;
int Port_MixProbe(const char *nameOrPath, Port_DataCheck check, PortMixInfo *info, char *err, u32 errSize);
/* sources.cfg "interface=" (the launcher's and mix.c's one reading): "ps2"
 * PORT_DATA_PS2 (silver), anything else PORT_DATA_GC (yellow); spaces
 * around it and case don't matter. `log`: a value that is neither is logged. */
int Port_MixInterfaceSetting(const char *value, int log);
/* launcher.c: the recipe the player chose to play (the Play page's "Mixed
 * game"), or NULL: a release. */
const char *Port_LauncherMix(void);
/* fileio.c: removes a folder and everything in it (or a file); a symlink
 * or junction is removed itself, never followed: its target stays. */
void Port_RemoveTree(const char *path);
/* The launcher's hook point for a feature built elsewhere: the mod loader
 * (the Mods page's "Mods..." row). launcher.c has a weak stub that says the
 * feature isn't there; the feature defines the real function. Called with
 * run 0 to ask (returns 1 if it is built in: the button is enabled), with
 * run 1 when the button is pressed: `paths` are the imported sources
 * ("gc", "pc", "ps2", "ps2beta" order; NULL where none), `prefDir` the
 * preferences folder (ends in a separator); what it has to say goes in
 * `status` (shown on the status line). It runs on the UI thread: start a
 * thread or a process for anything long. */
int Port_LauncherMods(int run, const char *const *paths, const char *prefDir, char *status, size_t statusSize);
/* Where the launcher keeps release `key`'s ("gc", "pc", "ps2") converted
 * packs: <pref>converted/<key>; SDL_free it. The game reads them when the
 * folder holds a finished conversion (its manifest, Port_Convert.h). */
char *Port_ConvertedDir(const char *key);
/* pcdisc.c: the PC release's disc image (.iso, .cue/.bin) into folder `dest`:
 * HWVX.cab's files flat, the FMV_ movies; `progress` counts files. */
int Port_ImportPCDisc(const char *image, const char *dest, SDL_AtomicInt *progress, SDL_AtomicInt *cancel, char *err,
                      size_t errLen);
/* A PS2 disc's XPS/ENGLISH copied into prefDir + "ps2" or "ps2beta" (by its
 * version), that folder given back in dest. Both imports build the folder
 * beside it and swap it in only when complete; setting *cancel stops them. */
int Port_ImportPS2Disc(const char *image, const char *prefDir, char *dest, size_t destLen, SDL_AtomicInt *progress,
                       SDL_AtomicInt *cancel, char *err, size_t errLen);

/* gfx_settings.c: graphics.cfg fills in what the command line left unset. */
void Port_GfxLoadConfig(int haveSize, int haveFullscreen, int haveVSync, int haveFps);
/* Saves graphics.cfg from g_port as it stands (the launcher's Settings page). */
void Port_GfxSaveConfig(void);
void Port_GfxSetFullscreenMode(int w, int h);
/* The Max FPS choice, brought into the rate the retrace pacing allows. */
int Port_GfxFrameRate(void);
/* The Gamma row as the power the display copy raises each colour to (gx_gl.c,
 * shaders/present.frag): exactly 1 in the middle of the bar. */
f32 Port_GfxGammaPower(void);
/* Whether showing a frame should wait for the display. HWVX_NO_VSYNC=1 wins:
 * the frame time in the logs is then what drawing costs. */
int Port_VSyncWanted(void);

/* Called once per displayed frame from VIWaitForRetrace: presents, pumps
 * events, paces to 60 Hz. */
void Port_EndFrame(void);
void Port_PumpEvents(void);

/* os.c: the giant lock that serialises game threads (see os.c for the
 * interrupt-handler side, used by the AX thread). */
void Port_GiantLock(void);
void Port_GiantUnlock(void);
void Port_InterruptLock(void);
void Port_InterruptUnlock(void);
void Port_InterruptPoint(void);
void Port_InterruptWindowBegin(void);
void Port_InterruptWindowEnd(void);

/* os.c: the process's memory, for HWVX_STATS: `committed` is what Task
 * Manager calls its memory on Windows (private bytes), the physical
 * footprint on macOS (Activity Monitor's), the resident size elsewhere;
 * `resident` is the working set / resident size. 0 where unknown. */
void Port_ProcessMemory(u64 *committed, u64 *resident);

/* Frame rate HUD (hud.c, HWVX_FPSHUD=1): vi.c ticks it once per retrace,
 * gx_draw.c draws its strip into the EFB's top left before each copy. */
void Port_HudTick(void);
void Port_HudDraw(int efbW, int efbH);

/* Subsystems */
void GX_PortBeginFrame(void);
void GX_PortPresent(void);
void Pad_PortHandleEvent(const SDL_Event *ev);
void Pad_PortMouseUpdate(void);   /* mouse steering: before each event pump */
void Port_TextHandleEvent(const SDL_Event *ev);
void Pad_PortSample(void);
extern volatile u32 g_portDiscReads;  /* dvd.c */
void Audio_PortShutdown(void);

/* Game entry points (src/game) */
s32 Main_Init(void);
s32 Main_Loop(void);
s32 Main_Shutdown(void);
s32 Gfx_CreateView(s32 view, u32 width, u32 height);

#endif
