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
    int launcher;            /* --launcher (or run as hwvx-launcher): open the launcher */
    volatile int quit;
    SDL_Window *window;
    const char *gpuDriver;   /* SDL_GPU driver for gx_sdlgpu.c: vulkan, metal, direct3d12 */
    int resetSave;           /* --reset-save (main_sdl.c): default game data, no autoload */
} PortState;

extern PortState g_port;

void Port_InitHandles(u32 arenaSize);

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

/* Frame rate HUD (hud.c, HWVX_FPSHUD=1): vi.c ticks it once per retrace,
 * gx_draw.c draws its strip into the EFB's top left before each copy. */
void Port_HudTick(void);
void Port_HudDraw(int efbW, int efbH);

/* Subsystems */
void GX_PortBeginFrame(void);
void GX_PortPresent(void);
void Pad_PortHandleEvent(const SDL_Event *ev);
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
