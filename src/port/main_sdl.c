/* SDL3 entry point. Replaces the GameCube main() and Main_NGC.c: sets up the
 * window, renderer and game arena, then runs the game's Main_Init/Main_Loop. */
#include "gx_render.h"
#include "disc.h"
#include "xdl.h"
#include "../game/include/Port_Convert.h"

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

/* Without --renderer: each platform's native API through SDL_GPU, Direct3D 12
 * on Windows, Metal on macOS, Vulkan elsewhere; one that fails to start falls
 * back to OpenGL (main), except on Android, which has no desktop OpenGL. A
 * build can pick another (CMake HWVX_DEFAULT_RENDERER). */
#if defined(SDL_PLATFORM_ANDROID)
static const char *const s_defaultRenderer = "vulkan";
#elif defined(HWVX_DEFAULT_RENDERER)
static const char *const s_defaultRenderer = HWVX_DEFAULT_RENDERER;
#elif defined(SDL_PLATFORM_WINDOWS)
static const char *const s_defaultRenderer = "d3d12";
#elif defined(SDL_PLATFORM_APPLE)
static const char *const s_defaultRenderer = "metal";
#else
static const char *const s_defaultRenderer = "vulkan";
#endif

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

/* --vsync on|off, also written 1/0. -1: not something the port takes. */
static int ParseVSync(const char *s) {
    if (SDL_strcasecmp(s, "on") == 0 || SDL_strcasecmp(s, "yes") == 0 || SDL_strcmp(s, "1") == 0) {
        return 1;
    }
    if (SDL_strcasecmp(s, "off") == 0 || SDL_strcasecmp(s, "no") == 0 || SDL_strcmp(s, "0") == 0) {
        return 0;
    }
    return -1;
}

static void ParseArgs(int argc, char **argv) {
    int i, v, haveSize = 0, haveVSync = 0, haveFps = 0, haveDisplay = 0;

    g_port.dataDir = NULL;   /* FindDataDir */
    g_port.program = argc > 0 ? argv[0] : NULL;
    /* the hwvx-launcher copy (CMakeLists.txt) opens the launcher, like --launcher */
    if (argc > 0 && argv[0] != NULL) {
        const char *name = argv[0], *sep;

        for (sep = argv[0]; *sep != '\0'; sep++) {
            if (*sep == '/' || *sep == '\\') {
                name = sep + 1;
            }
        }
        g_port.launcher = SDL_strncasecmp(name, "hwvx-launcher", 13) == 0;
    }
    g_port.scale = 2;
    g_port.resetSave = 0;
    g_port.vsync = 1;        /* what the GameCube's own retrace wait does */
    g_port.maxFps = 60;
    g_port.gamma = 50;
    g_port.fov = 50;
    if (!SetRenderer(s_defaultRenderer)) {
        SetRenderer("opengl");
    }
#ifdef SDL_PLATFORM_ANDROID
    g_port.fullscreen = 1;
#endif
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
            haveDisplay = 1;
        } else if (SDL_strcmp(argv[i], "--windowed") == 0) {
            g_port.fullscreen = 0;      /* whatever graphics.cfg says */
            haveDisplay = 1;
        } else if (SDL_strcmp(argv[i], "--vsync") == 0 && i + 1 < argc &&
                   (v = ParseVSync(argv[i + 1])) >= 0) {
            g_port.vsync = v;
            haveVSync = 1;
            i++;
        } else if (SDL_strcmp(argv[i], "--fps") == 0 && i + 1 < argc && (v = SDL_atoi(argv[i + 1])) >= 0) {
            g_port.maxFps = v;
            haveFps = 1;
            i++;
        } else if (SDL_strcmp(argv[i], "--launcher") == 0) {
            g_port.launcher = 1;
        } else if (SDL_strcmp(argv[i], "--convert-xdl") == 0 && i + 1 < argc) {
            g_port.convertDir = argv[++i];
        } else if (SDL_strcmp(argv[i], "--reset-save") == 0) {
            g_port.resetSave = 1;
        } else if (SDL_strcmp(argv[i], "--renderer") == 0 && i + 1 < argc && SetRenderer(argv[i + 1])) {
            i++;
        } else {
            SDL_Log("usage: %s [--data DIR] [--scale N] [--window WxH] [--fullscreen|--windowed]", argv[0]);
            SDL_Log("              [--vsync on|off] [--fps N] [--renderer NAME] [--reset-save] [--launcher]");
            SDL_Log("  --data PATH      a GameCube disc image (.iso) or the extracted disc files");
            SDL_Log("                   (default: the one chosen last time, or an .iso or files/");
            SDL_Log("                   next to the program, or orig/files)");
            SDL_Log("  --window WxH     window size, any shape (default: 640x480 times --scale)");
            SDL_Log("  --vsync on|off   whether a frame waits for the display (default: on)");
            SDL_Log("  --fps N          frame rate: 30, 60 (default), 90, 120, 144, 240, 300,");
            SDL_Log("                   360, 480, or 0 for unlimited (the game's 1 ms time");
            SDL_Log("                   floor bounds it near 1000); a rate the drawing");
            SDL_Log("                   cannot reach lands where it fits");
            SDL_Log("  --renderer NAME  opengl, vulkan, metal or d3d12 (default: %s;", s_defaultRenderer);
            SDL_Log("                   one that fails to start falls back to opengl)");
            SDL_Log("  --reset-save     start from default game data: the newest save is not loaded");
            SDL_Log("  --launcher       open the launcher (game data, releases) instead of booting");
            SDL_Log("  --convert-xdl DIR  convert every pack of --data PATH to the .xdl cache in DIR");
            SDL_Log("                   and exit (what the launcher's Convert page runs)");
            exit(1);
        }
    }
    if (g_port.scale < 1) {
        g_port.scale = 1;
    }
    Port_GfxLoadConfig(haveSize, haveDisplay || g_port.fullscreen, haveVSync, haveFps);
}

/* --reset-save: the game side reads the flag through this accessor rather
 * than our private PortState (port_internal.h is not in the game code). */
int Port_ResetSave(void) {
    return g_port.resetSave;
}

/* ---- the game data ----------------------------------------------------- */

#define DATA_MARKER "HW_share.xgc"   /* in the root of the extracted disc */

static int HasData(const char *dir) {
    /* the GameCube's files, or the PC release's install folder (HW_share.xpc),
     * the PS2 disc's files (XPS/ENGLISH/HW_share.xps) or that folder itself
     * (dvd.c DVDOpen); names matched without case, as dvd.c reads them */
    static const char *const markers[] = { DATA_MARKER, "HW_share.xpc", "XPS/ENGLISH/HW_share.xps", "HW_share.xps" };
    size_t i;

    if (dir == NULL || *dir == '\0') {
        return 0;
    }
    for (i = 0; i < SDL_arraysize(markers); i++) {
        char *path = Port_FindNoCase(dir, markers[i]);
        SDL_PathInfo info;
        int is = path != NULL && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;

        SDL_free(path);
        if (is) {
            return 1;
        }
    }
    return 0;
}

/* Why the last disc image DataIn tried was refused, for the messages. */
static char s_imageError[256];

/* A GameCube disc image with the game's files: opened (disc.c), so the game
 * reads from it. */
static int UseImage(const char *path) {
    SDL_IOStream *io;

    if (!Disc_Open(path)) {
        SDL_snprintf(s_imageError, sizeof(s_imageError), "%s: %s", path, SDL_GetError());
        return 0;
    }
    io = Disc_OpenFile(DATA_MARKER);
    if (io == NULL) {
        SDL_snprintf(s_imageError, sizeof(s_imageError), "%s is a GameCube disc (%s) but not "
                     "Hot Wheels: Velocity X", path, Disc_GameID());
        Disc_Close();
        return 0;
    }
    SDL_CloseIO(io);
    return 1;
}

typedef struct ImageScan {
    const char *dir;
    char *found;
} ImageScan;

static SDL_EnumerationResult SDLCALL ScanImage(void *userdata, const char *dir, const char *name) {
    ImageScan *scan = userdata;
    char *path = NULL;

    if (Disc_IsImageName(name) && SDL_asprintf(&path, "%s%s", dir, name) > 0) {
        if (UseImage(path)) {
            scan->found = path;
            return SDL_ENUM_SUCCESS;
        }
        SDL_free(path);
    }
    return SDL_ENUM_CONTINUE;
}

/* The game data at path, as a new string, or NULL: path itself when it is a
 * disc image (then opened) or a folder of extracted files, a files/ folder in
 * it (what Dolphin's "Extract Entire Disc" makes), or a disc image in it. */
static char *DataIn(const char *dir) {
    char path[1024];
    size_t n;
    SDL_PathInfo info;
    ImageScan scan = { NULL, NULL };

    Disc_Close();
    if (dir == NULL || *dir == '\0') {
        return NULL;
    }
    SDL_strlcpy(path, dir, sizeof(path));
    n = SDL_strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        path[--n] = '\0';
    }
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
        return UseImage(path) ? SDL_strdup(path) : NULL;
    }
    if (HasData(path)) {
        return SDL_strdup(path);
    }
    SDL_strlcat(path, "/files", sizeof(path));
    if (HasData(path)) {
        return SDL_strdup(path);
    }
    path[n] = '\0';
    SDL_strlcat(path, "/", sizeof(path));
    SDL_EnumerateDirectory(path, ScanImage, &scan);
    return scan.found;
}

#ifdef SDL_PLATFORM_ANDROID
/* The image or folder found or chosen last time: datadir.txt in the save
 * folder. Android only: on desktop the launcher keeps the choice, and takes
 * over an old datadir.txt on its first run. */
static char *SavedDataPath(void) {
    char *pref = Port_PrefPath();
    char *path = NULL;

    if (pref != NULL) {
        SDL_asprintf(&path, "%sdatadir.txt", pref);
        SDL_free(pref);
    }
    return path;
}

static char *LoadDataDir(void) {
    char *path = SavedDataPath();
    char *dir = path != NULL ? SDL_LoadFile(path, NULL) : NULL;

    SDL_free(path);
    return dir;
}

static void SaveDataDir(const char *dir) {
    char *path = SavedDataPath();

    if (path != NULL && !SDL_SaveFile(path, dir, SDL_strlen(dir))) {
        Port_Log("could not remember the game data folder: %s", SDL_GetError());
    }
    SDL_free(path);
}
#endif

/* Places a packaged game looks without being told. */
static char *SearchDataDir(char *searched, size_t size) {
    const char *base = SDL_GetBasePath();   /* SDL owns it; ends in a separator */
    char *pref = Port_PrefPath();
    char *candidates[12] = { NULL };
    char *found = NULL;
    int i, n = 0;

    /* (DataIn also takes a files/ folder or an .iso in each) */
    if (base != NULL) {
        candidates[n++] = SDL_strdup(base);                      /* next to the program */
        SDL_asprintf(&candidates[n++], "%sorig", base);
#ifdef SDL_PLATFORM_MACOS
        if (SDL_strstr(base, ".app/Contents/") != NULL) {         /* next to the .app (base: Contents/Resources/) */
            SDL_asprintf(&candidates[n++], "%s../../..", base);
        }
#endif
    }
    candidates[n++] = SDL_strdup("orig");                       /* the source tree */
    if (pref != NULL) {
        candidates[n++] = SDL_strdup(pref);
    }
#ifdef SDL_PLATFORM_ANDROID
    if (SDL_GetAndroidExternalStoragePath() != NULL) {
        candidates[n++] = SDL_strdup(SDL_GetAndroidExternalStoragePath());
        SDL_asprintf(&candidates[n++], "%s/files", SDL_GetAndroidExternalStoragePath());
    }
    candidates[n++] = SDL_strdup("/storage/emulated/0/HWVX");    /* needs "All files access" */
#endif
    for (i = 0; i < n; i++) {
        if (found == NULL && candidates[i] != NULL && (found = DataIn(candidates[i])) == NULL) {
            SDL_strlcat(searched, "\n", size);
            SDL_strlcat(searched, candidates[i], size);
        }
        SDL_free(candidates[i]);
    }
    SDL_free(pref);
    return found;
}

/* For the launcher: the usual places, the ones searched noted for errors. */
static char s_searched[2048];

static char *SearchForLauncher(void) {
    return SearchDataDir(s_searched, sizeof(s_searched));
}

/* Sets g_port.dataDir: --data, else the folder used last time, else the
 * usual places, else (desktop) the player picks it. */
static void FindDataDir(void) {
    char searched[2048] = "";
    char *dir;

    if (g_port.dataDir != NULL) {   /* --data */
        s_imageError[0] = 0;
        if ((dir = DataIn(g_port.dataDir)) == NULL) {
            if (s_imageError[0] != 0) {
                Port_Fatal("Game data not found.\n\nThe disc image can't be used:\n%s", s_imageError);
            }
            Port_Fatal("Game data not found.\n\nExpected a GameCube disc image (.iso) or the extracted "
                       "disc files (" DATA_MARKER ", worlds_/, audio_/ ...) in:\n%s", g_port.dataDir);
        }
        g_port.dataDir = dir;
        return;
    }
#ifndef SDL_PLATFORM_ANDROID
    /* the launcher (launcher.c): the chosen release, the launcher window when
     * there is none or it was asked for (--launcher), or quit */
    {
        int window = SDL_getenv("HWVX_NO_DIALOG") == NULL;
        char *chosen = Port_Launcher(g_port.launcher, window, DataIn, SearchForLauncher);

        if (chosen == NULL && window) {
            exit(0);
        }
        dir = chosen != NULL ? DataIn(chosen) : NULL;
        SDL_free(chosen);
        SDL_strlcpy(searched, s_searched, sizeof(searched));
    }
#else
    dir = LoadDataDir();
    if (dir != NULL) {
        char *found = DataIn(dir);

        SDL_free(dir);
        dir = found;
    }
    if (dir == NULL) {
        dir = SearchDataDir(searched, sizeof(searched));
    }
#endif
    if (dir == NULL) {
        Port_Fatal("Game data not found.\n\nThe game needs your own GameCube disc: an image of it "
                   "(.iso), or its extracted files (" DATA_MARKER ", worlds_/, audio_/ ... in a files/ "
                   "folder). Put either in one of:%s"
#ifndef SDL_PLATFORM_ANDROID
                   "\n\nor pass the location with --data PATH."
#endif
                   , searched);
    }
    if (dir[0] != '/' && dir[0] != '\\' && !(dir[0] && dir[1] == ':')) {   /* relative: to the current folder */
        char *cwd = SDL_GetCurrentDirectory();   /* ends in a separator */
        char *abs = NULL;

        if (cwd != NULL && SDL_asprintf(&abs, "%s%s", cwd, dir) > 0) {
            SDL_free(dir);
            dir = abs;
        }
        SDL_free(cwd);
    }
#ifdef SDL_PLATFORM_ANDROID
    SaveDataDir(dir);
#endif
    g_port.dataDir = dir;
}

int Port_TryCreateWindow(SDL_WindowFlags flags) {
    flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_port.convertDir != NULL) {
        flags |= SDL_WINDOW_HIDDEN;     /* converting: nothing is shown */
        g_port.fullscreen = 0;
    }
    if (g_port.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    g_port.window = SDL_CreateWindow("Hot Wheels: Velocity X", g_port.winW ? g_port.winW : 640 * g_port.scale,
                                     g_port.winH ? g_port.winH : 480 * g_port.scale, flags);
    if (g_port.window == NULL) {
        return 0;
    }
    /* a saved resolution is the display mode when fullscreen */
    if (g_port.fullscreen && g_port.winW) {
        Port_GfxSetFullscreenMode(g_port.winW, g_port.winH);
        SDL_SetWindowFullscreen(g_port.window, true);
    }
    return 1;
}

void Port_CreateWindow(SDL_WindowFlags flags) {
    if (!Port_TryCreateWindow(flags)) {
        Port_Fatal("SDL_CreateWindow failed: %s", SDL_GetError());
    }
}

/* ---- Main_NGC.c replacements ------------------------------------------- */

/* The GameCube's reset to the system menu ("Yes, Quit" on the quit screen,
 * interface event 0x19): on PC, back to the desktop. */
void ForceIPLReset(void) {
    m_ilpreset_803C7620 = 1;
    m_reset = 1;
    g_port.quit = 1;
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

static const char *s_convertSource;

int main(int argc, char **argv) {
    ParseArgs(argc, argv);
    if (g_port.convertDir != NULL) {
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");  /* the converter shows nothing: no Dock icon either */
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        Port_Fatal("SDL_Init failed: %s", SDL_GetError());
    }
    if (g_port.convertDir != NULL) {
        /* the converter (game/Port_Convert.c): the data given, the cache written there */
        if (g_port.dataDir == NULL) {
            Port_Fatal("--convert-xdl needs --data PATH");
        }
        s_convertSource = g_port.dataDir;
        Xdl_Use(g_port.convertDir, 0, 1);
        Port_ConvertBegin();
    } else if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {  /* no sound is not fatal (ax.c runs silently) */
        Port_Log("audio: %s", SDL_GetError());
    }
    FindDataDir();
    Port_InitHandles(PORT_ARENA_SIZE);
    if (!g_gxr->open()) {
#ifdef SDL_PLATFORM_ANDROID
        Port_Fatal("The %s renderer is unavailable: %s", g_port.gpuDriver, SDL_GetError());
#else
        Port_Log("falling back to the OpenGL renderer");
        SetRenderer("opengl");
        g_gxr->open();
#endif
    }

    OSInit();
    DVDInit();
    VIInit();
    PADInit();
    CARDInit();
    InitRenderMode(NULL);
    InitMem();
    srand(Port_Seed((unsigned)time(NULL)));

    if (!Main_Init()) {
        Port_Fatal("Main_Init failed");
    }
    if (g_port.convertDir != NULL) {
        int failed = Disc_IsOpen() ? 1 : Port_ConvertRun(g_port.dataDir, s_convertSource, g_port.convertDir);

        if (Disc_IsOpen()) {
            printf("@error a disc image is not converted: extract its files\n");
        }
        fflush(stdout);
        _Exit(failed);      /* nothing of the game to shut down */
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
