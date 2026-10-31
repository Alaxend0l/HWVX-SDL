/* SDL3 entry point. Replaces the GameCube main() and Main_NGC.c: sets up the
 * window, renderer and game arena, then runs the game's Main_Init/Main_Loop. */
#include "gx_render.h"

#include <SDL3/SDL_main.h>

/* Globals that main()/Main_NGC.c owned on the GameCube. */
s64 g_resetTicks;
s32 shithack;
s32 m_ilpreset_803C7620;
s32 m_reset;
s32 m_cd_drive_status;
s8 g_bBlockReset;
s32 g_viewID;
s8 NGC_Thread_On;
u8 Thread[0x310];
u8 ThreadStack[0x5000];
void *g_frameBuffer1;
void *g_frameBuffer2;
void *g_currentBuffer;

/* GXNtsc480IntDf as the original uses it: InitRenderMode (Main_NGC.c) passes
 * it through GXAdjustForOverscan(rmode, rmodeobj, 0, 16), which trims 16
 * lines top and bottom: a 448-line EFB and XFB, shown 16 lines down. The game
 * draws its windows into those 448 lines (Render_.c: 0.93333334 = 448 / 480);
 * GX_DrawCopyDisp stretches them to the window, as the TV's overscan hid the
 * border. Layout of GXRenderModeObj, 0x3C bytes. */
static struct {
    u32 viTVmode;
    u16 fbWidth, efbHeight, xfbHeight;
    u16 viXOrigin, viYOrigin, viWidth, viHeight;
    u32 xFBmode;
    u8 field_rendering, aa;
    u8 sample_pattern[12][2];
    u8 vfilter[7];
} s_renderMode = {
    0, 640, 448, 448, 40, 16, 640, 448, 1, 0, 0,
    { {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6},
      {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6} },
    { 0, 0, 21, 22, 21, 0, 0 },
};
void *rmode = &s_renderMode;

PortState g_port;

/* --renderer NAME: the backend, and for SDL_GPU the driver it asks for. */
static const struct {
    const char *name;
    const GXRenderer *renderer;
    const char *gpuDriver;
} s_renderers[] = {
    { "opengl", &g_gxGL, NULL },
    { "vulkan", &g_gxSDLGPU, "vulkan" },
    { "metal", &g_gxSDLGPU, "metal" },
    { "d3d12", &g_gxSDLGPU, "direct3d12" },
};

static int SetRenderer(const char *name) {
    size_t i;

    for (i = 0; i < sizeof(s_renderers) / sizeof(s_renderers[0]); i++) {
        if (SDL_strcasecmp(name, s_renderers[i].name) == 0) {
            g_gxr = s_renderers[i].renderer;
            g_port.gpuDriver = s_renderers[i].gpuDriver;
            return 1;
        }
    }
    return 0;
}

static void ParseArgs(int argc, char **argv) {
    int i, haveSize = 0;

    g_port.dataDir = "orig/files";
    g_port.scale = 2;
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            g_port.dataDir = argv[++i];
        } else if (SDL_strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            g_port.scale = SDL_atoi(argv[++i]);
            haveSize = 1;
        } else if (SDL_strcmp(argv[i], "--window") == 0 && i + 1 < argc &&
                   SDL_sscanf(argv[i + 1], "%dx%d", &g_port.winW, &g_port.winH) == 2 &&
                   g_port.winW > 0 && g_port.winH > 0) {
            i++;
            haveSize = 1;
        } else if (SDL_strcmp(argv[i], "--fullscreen") == 0) {
            g_port.fullscreen = 1;
        } else if (SDL_strcmp(argv[i], "--renderer") == 0 && i + 1 < argc && SetRenderer(argv[i + 1])) {
            i++;
        } else {
            SDL_Log("usage: %s [--data DIR] [--scale N] [--window WxH] [--fullscreen] [--renderer NAME]", argv[0]);
            SDL_Log("  --data DIR       extracted disc files (default: orig/files)");
            SDL_Log("  --window WxH     window size, any shape (default: 640x480 times --scale)");
            SDL_Log("  --renderer NAME  opengl (default), vulkan, metal or d3d12");
            exit(1);
        }
    }
    if (g_port.scale < 1) {
        g_port.scale = 1;
    }
    Port_GfxLoadConfig(haveSize, g_port.fullscreen);
}

static void CheckDataDir(void) {
    char path[1024];
    SDL_PathInfo info;

    SDL_snprintf(path, sizeof(path), "%s/HW_share.xgc", g_port.dataDir);
    if (!SDL_GetPathInfo(path, &info)) {
        Port_Fatal("Game data not found.\n\nExpected the extracted GameCube disc files "
                   "(HW_share.xgc, worlds_/, audio_/ ...) in:\n%s\n\n"
                   "Pass another location with --data DIR.", g_port.dataDir);
    }
}

void Port_CreateWindow(SDL_WindowFlags flags) {
    flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_port.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    g_port.window = SDL_CreateWindow("Hot Wheels: Velocity X", g_port.winW ? g_port.winW : 640 * g_port.scale,
                                     g_port.winH ? g_port.winH : 480 * g_port.scale, flags);
    if (g_port.window == NULL) {
        Port_Fatal("SDL_CreateWindow failed: %s", SDL_GetError());
    }
    /* a saved resolution is the display mode when fullscreen */
    if (g_port.fullscreen && g_port.winW) {
        Port_GfxSetFullscreenMode(g_port.winW, g_port.winH);
        SDL_SetWindowFullscreen(g_port.window, true);
    }
}

/* ---- Main_NGC.c replacements ------------------------------------------- */

void ForceIPLReset(void) {
    m_ilpreset_803C7620 = 1;
    m_reset = 1;
}

s32 Init_Display(void) {
    g_viewID = Gfx_CreateView(0, 640, 480);
    return g_viewID >= 0;
}

void Main_Shutdown_NGC(void) {
}

/* The game runs one background loader thread (see os_thread.c). */
s32 RunThread(void *(*func)(void *)) {
    BOOL enabled;

    OSCreateThread(Thread, func, NULL, ThreadStack + sizeof(ThreadStack), sizeof(ThreadStack), 0x10, 1);
    enabled = OSDisableInterrupts();
    OSResumeThread(Thread);
    OSRestoreInterrupts(enabled);
    NGC_Thread_On = 1;
    return 1;
}

s8 isInThread(void) {
    return (s8)(OSGetCurrentThread() == (void *)Thread);
}

void EndThread(void) {
    s32 val;
    NGC_Thread_On = 0;
    OSExitThread(&val);
}

void TextureManager_ReleaseCache(void) {
}

void InitMem(void) {
    /* The whole game heap is the port arena (see os.c); no XFBs needed. */
    void *lo = OSGetArenaLo();
    void *hi = OSGetArenaHi();

    lo = OSInitAlloc(lo, hi, 1);
    lo = (void *)(((uintptr_t)lo + 0x1F) & ~(uintptr_t)0x1F);
    hi = (void *)((uintptr_t)hi & ~(uintptr_t)0x1F);
    OSSetCurrentHeap(OSCreateHeap(lo, hi));
    OSSetArenaLo(hi);
}

void InitRenderMode(void *mode) {
    (void)mode;
}

void InitVI(void) {
}

void GetSystemDate(s32 *year, s32 *month, s32 *day, s32 *hour, s32 *minute) {
    SDL_DateTime dt;
    SDL_Time now;

    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &dt, true);
    *year = dt.year;
    *month = dt.month;
    *day = dt.day;
    *hour = dt.hour;
    *minute = dt.minute;
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv) {
    ParseArgs(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        Port_Fatal("SDL_Init failed: %s", SDL_GetError());
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {  /* no sound is not fatal (ax.c runs silently) */
        Port_Log("audio: %s", SDL_GetError());
    }
    CheckDataDir();
    Port_InitHandles(PORT_ARENA_SIZE);
    g_gxr->open();

    OSInit();
    DVDInit();
    VIInit();
    PADInit();
    CARDInit();
    InitRenderMode(NULL);
    InitMem();
    srand((unsigned)time(NULL));

    if (!Main_Init()) {
        Port_Fatal("Main_Init failed");
    }
    m_ilpreset_803C7620 = 0;
    m_reset = 0;
    g_resetTicks = OSGetTime();
    while (!g_port.quit) {
        m_cd_drive_status = DVDGetDriveStatus();
        if (!Main_Loop()) {
            break;
        }
        shithack = 1;
    }
    Main_Shutdown();
    Main_Shutdown_NGC();

    Audio_PortShutdown();
    g_gxr->close();
    SDL_DestroyWindow(g_port.window);
    SDL_Quit();
    return 0;
}
