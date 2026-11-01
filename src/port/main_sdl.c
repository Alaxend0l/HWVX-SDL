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

/* Without --renderer: Android has no desktop OpenGL; a build can pick
 * another (CMake HWVX_DEFAULT_RENDERER, e.g. the Flatpak's vulkan). */
#if defined(SDL_PLATFORM_ANDROID)
static const char *const s_defaultRenderer = "vulkan";
#elif defined(HWVX_DEFAULT_RENDERER)
static const char *const s_defaultRenderer = HWVX_DEFAULT_RENDERER;
#else
static const char *const s_defaultRenderer = "opengl";
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

static void ParseArgs(int argc, char **argv) {
    int i, haveSize = 0;

    g_port.dataDir = NULL;   /* FindDataDir */
    g_port.scale = 2;
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
        } else if (SDL_strcmp(argv[i], "--renderer") == 0 && i + 1 < argc && SetRenderer(argv[i + 1])) {
            i++;
        } else {
            SDL_Log("usage: %s [--data DIR] [--scale N] [--window WxH] [--fullscreen] [--renderer NAME]", argv[0]);
            SDL_Log("  --data DIR       extracted disc files (default: the folder chosen last time,");
            SDL_Log("                   or files/ next to the program, or orig/files)");
            SDL_Log("  --window WxH     window size, any shape (default: 640x480 times --scale)");
            SDL_Log("  --renderer NAME  opengl, vulkan, metal or d3d12 (default: %s;", s_defaultRenderer);
            SDL_Log("                   one that fails to start falls back to opengl)");
            exit(1);
        }
    }
    if (g_port.scale < 1) {
        g_port.scale = 1;
    }
    Port_GfxLoadConfig(haveSize, g_port.fullscreen);
}

/* ---- the game data ----------------------------------------------------- */

#define DATA_MARKER "HW_share.xgc"   /* in the root of the extracted disc */

static int HasData(const char *dir) {
    char path[1024];
    SDL_PathInfo info;

    if (dir == NULL || *dir == '\0') {
        return 0;
    }
    SDL_snprintf(path, sizeof(path), "%s/" DATA_MARKER, dir);
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* dir itself, or a files/ folder in it (what Dolphin's "Extract Entire
 * Disc" makes): the one with the data, as a new string, or NULL. */
static char *DataIn(const char *dir) {
    char path[1024];
    size_t n;

    if (dir == NULL || *dir == '\0') {
        return NULL;
    }
    SDL_strlcpy(path, dir, sizeof(path));
    n = SDL_strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        path[--n] = '\0';
    }
    if (HasData(path)) {
        return SDL_strdup(path);
    }
    SDL_strlcat(path, "/files", sizeof(path));
    return HasData(path) ? SDL_strdup(path) : NULL;
}

/* The folder found or chosen last time: datadir.txt in the save folder. */
static char *SavedDataPath(void) {
    char *pref = SDL_GetPrefPath("HWVX-SDL", "HotWheelsVelocityX");
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

/* Places a packaged game looks without being told. */
static char *SearchDataDir(char *searched, size_t size) {
    const char *base = SDL_GetBasePath();   /* SDL owns it; ends in a separator */
    char *pref = SDL_GetPrefPath("HWVX-SDL", "HotWheelsVelocityX");
    char *candidates[8] = { NULL };
    char *found = NULL;
    int i, n = 0;

    if (base != NULL) {
        SDL_asprintf(&candidates[n++], "%sfiles", base);         /* next to the program */
        SDL_asprintf(&candidates[n++], "%sorig/files", base);
#ifdef SDL_PLATFORM_MACOS
        if (SDL_strstr(base, ".app/Contents/") != NULL) {         /* next to the .app (base: Contents/Resources/) */
            SDL_asprintf(&candidates[n++], "%s../../../files", base);
        }
#endif
    }
    candidates[n++] = SDL_strdup("orig/files");                 /* the source tree */
    if (pref != NULL) {
        SDL_asprintf(&candidates[n++], "%sfiles", pref);
    }
#ifdef SDL_PLATFORM_ANDROID
    if (SDL_GetAndroidExternalStoragePath() != NULL) {
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

#ifndef SDL_PLATFORM_ANDROID
typedef struct FolderPick {
    SDL_AtomicInt done;
    char *dir;
} FolderPick;

static void SDLCALL FolderPicked(void *userdata, const char *const *filelist, int filter) {
    FolderPick *pick = userdata;

    (void)filter;
    if (filelist == NULL) {
        Port_Log("folder dialog: %s", SDL_GetError());
    } else if (filelist[0] != NULL) {
        pick->dir = SDL_strdup(filelist[0]);
    }
    SDL_SetAtomicInt(&pick->done, filelist == NULL ? 2 : 1);
}

/* Asks for the folder until it has the data. NULL: the player gave up, or
 * there is no folder dialog (no portal or zenity on Linux). */
static char *PickDataDir(void) {
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder..." },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
    };
    const char *text =
        "Hot Wheels: Velocity X needs the files of your own copy of the GameCube disc.\n\n"
        "Extract them with Dolphin (right-click the game > Properties > Filesystem > "
        "Extract Entire Disc), then choose the folder they went to: the one with "
        DATA_MARKER ", worlds_ and audio_ in it, or the folder above it.";

    for (;;) {
        SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, NULL, "Game data", text, 2, buttons, NULL };
        FolderPick pick;
        char *found;
        int button = 0;

        if (!SDL_ShowMessageBox(&box, &button) || button != 1) {
            return NULL;
        }
        SDL_zero(pick);
        SDL_ShowOpenFolderDialog(FolderPicked, &pick, NULL, NULL, false);
        while (SDL_GetAtomicInt(&pick.done) == 0) {
            SDL_PumpEvents();
            SDL_Delay(20);
        }
        if (SDL_GetAtomicInt(&pick.done) == 2) {
            return NULL;   /* no dialog available */
        }
        if (pick.dir == NULL) {
            continue;      /* cancelled: ask again */
        }
        found = DataIn(pick.dir);
        if (found != NULL) {
            SDL_free(pick.dir);
            return found;
        }
        text = "That folder doesn't have the game data in it (no " DATA_MARKER ").\n\n"
               "Choose the folder Dolphin extracted the disc to: the one with "
               DATA_MARKER ", worlds_ and audio_ in it, or the folder above it.";
        SDL_free(pick.dir);
    }
}
#endif

/* Sets g_port.dataDir: --data, else the folder used last time, else the
 * usual places, else (desktop) the player picks it. */
static void FindDataDir(void) {
    char searched[2048] = "";
    char *dir;

    if (g_port.dataDir != NULL) {   /* --data */
        if ((dir = DataIn(g_port.dataDir)) == NULL) {
            Port_Fatal("Game data not found.\n\nExpected the extracted GameCube disc files "
                       "(" DATA_MARKER ", worlds_/, audio_/ ...) in:\n%s", g_port.dataDir);
        }
        g_port.dataDir = dir;
        return;
    }
    dir = LoadDataDir();
    if (dir != NULL) {
        char *found = DataIn(dir);

        SDL_free(dir);
        if (found != NULL) {
            g_port.dataDir = found;
            return;
        }
    }
    dir = SearchDataDir(searched, sizeof(searched));
#ifndef SDL_PLATFORM_ANDROID
    if (dir == NULL && SDL_getenv("HWVX_NO_DIALOG") == NULL) {
        dir = PickDataDir();
        if (dir == NULL) {
            exit(0);
        }
    }
#endif
    if (dir == NULL) {
        Port_Fatal("Game data not found.\n\nThe game needs the extracted files of your own GameCube "
                   "disc (" DATA_MARKER ", worlds_/, audio_/ ...). Put them in one of:%s"
#ifndef SDL_PLATFORM_ANDROID
                   "\n\nor pass their location with --data DIR."
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
    SaveDataDir(dir);
    g_port.dataDir = dir;
}

int Port_TryCreateWindow(SDL_WindowFlags flags) {
    flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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

int main(int argc, char **argv) {
    ParseArgs(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        Port_Fatal("SDL_Init failed: %s", SDL_GetError());
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {  /* no sound is not fatal (ax.c runs silently) */
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
