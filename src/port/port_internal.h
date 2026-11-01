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
    volatile int quit;
    SDL_Window *window;
    const char *gpuDriver;   /* SDL_GPU driver for gx_sdlgpu.c: vulkan, metal, direct3d12 */
} PortState;

extern PortState g_port;

void Port_InitHandles(u32 arenaSize);

/* Creates g_port.window with the renderer's flags (gx_render.h open). */
void Port_CreateWindow(SDL_WindowFlags flags);

/* gfx_settings.c: graphics.cfg fills in what the command line left unset. */
void Port_GfxLoadConfig(int haveSize, int haveFullscreen);
void Port_GfxSetFullscreenMode(int w, int h);

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

/* Subsystems */
void GX_PortBeginFrame(void);
void GX_PortPresent(void);
void Pad_PortHandleEvent(const SDL_Event *ev);
void Pad_PortSample(void);
extern volatile u32 g_portDiscReads;  /* dvd.c */
void Audio_PortShutdown(void);

/* Game entry points (src/game) */
s32 Main_Init(void);
s32 Main_Loop(void);
s32 Main_Shutdown(void);
s32 Gfx_CreateView(s32 view, u32 width, u32 height);

#endif
