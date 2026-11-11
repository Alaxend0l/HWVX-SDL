/* Graphics settings: the Options screen's "Graphics" menu (Port_Menu.c in
 * src/game) edits these through two code variables, and they are kept in
 * <pref>/graphics.cfg next to the memory card files.
 *
 * - Resolution: one of the display's fullscreen modes (SDL), or the
 *   current window size if it isn't one of them. Windowed it sets the window
 *   size, fullscreen the display mode. Any shape works: the renderer follows
 *   the window (gx_draw.c).
 * - Display: windowed or fullscreen.
 * - VSync: whether showing a frame waits for the display to have finished
 *   with the one before it. Off costs no game speed: the retrace pacing below
 *   still sets the rate, only the wait for the display goes.
 * - Max FPS: how many frames a second reach the display. The game asks for one
 *   video retrace per frame of its own work (VIWaitForRetrace), so this is also
 *   the rate the game runs at. Any choice keeps the game's real-world speed:
 *   30 doubles each frame's delta and 120 halves it, and most of the game
 *   integrates its motion from that delta (g_fDeltaTimeMS/g_fDeltaTimeSec, the
 *   ms key tracks). A rate the drawing cannot fit in the shorter frame budget
 *   lands below the choice rather than stuttering (vi.c). "Unlimited" drops
 *   the preset pacing: frames cost only what drawing costs, up to the delta's
 *   own 1 ms floor in TimeHandler, which bounds the rate near 1000 FPS; below
 *   that floor the game's clock would run fast and menu and brief animation
 *   with it, so the pacing holds there.
 * - Gamma: a bar like the volumes', from darker to brighter. The display copy
 *   raises each colour to a power (Port_GfxGammaPower), so it covers everything
 *   on screen, menus and movies too, but not the frames HWVX_DUMP saves.
 * - FOV: a bar like the volumes', -25% to +25% of the view of the camera the
 *   player is driving with (Port_GfxFovScale). Scripted shots keep their framing.
 * - PC textures (graphics.cfg only, no menu row yet): the colours the PC
 *   release's textures are drawn with, the GameCube's (default) or the PC's
 *   own, brighter ones (Port_PCTexturesLookGC).
 * - Movies (graphics.cfg "movies=on|off", no menu row; HWVX_NO_MOVIE
 *   overrides): off skips the intro and ending movies, like the PC release's
 *   Disable Movies option (thp.c MoviesOff).
 * - Split-screen LOD (graphics.cfg "splitlod=full|gc", no menu row): full
 *   detail in split screen (default, like the PC release), or one LOD level
 *   coarser as the GameCube drew it (Port_SplitScreenCoarseLOD).
 * - Main menu (graphics.cfg "mainmenu=original|port", no menu row: the
 *   Graphics screen stays as it is; HWVX_ORIGINAL_MAINMENU=1 or 0 overrides
 *   it): "port" (default) adds the port's rows to the GameCube main menu -
 *   Quit Game, and Multi-Player leading to Online / Local; "original" leaves
 *   the main menu exactly as the release has it, rows, spacing and up/down
 *   ring (Port_OriginalMainMenu, Port_Menu.c). The port's other menu changes
 *   (Graphics in Options, the memory card screens) stay either way. The
 *   GameCube's own main menu has no way to quit: close the window (or Alt+F4,
 *   Cmd+Q) instead. The PC release's main menu has its own Exit to Desktop
 *   and gets no port rows, so this changes nothing there.
 *
 * The menu changes pending values; Port_GfxApply applies them when the menu
 * closes, so stepping through modes doesn't switch the display each time. The
 * two bars take effect at once, to judge them by, and are saved with the rest. */
#include "port_internal.h"

#define MAX_MODES 48

typedef struct Res {
    int w, h;
} Res;

static Res s_modes[MAX_MODES];
static char s_names[MAX_MODES][16];
static u32 s_numModes;
static u32 s_pendingRes, s_pendingFull, s_pendingVSync, s_pendingFps;

/* The Max FPS row's choices, in menu order. The row's code variable is the
 * index; the rate behind it is the port's business, not the menu's.
 * 0 means unlimited: no retrace pacing, frames go as fast as they draw. */
static const int s_fpsValues[] = { 30, 60, 90, 120, 144, 240, 300, 360, 480, 0 };
#define NUM_FPS ((int)(sizeof(s_fpsValues) / sizeof(s_fpsValues[0])))
static char s_fpsNames[NUM_FPS][16];

/* What VIWaitForRetrace should pace a frame at, from the Max FPS choice. The
 * game runs one logic frame per video retrace and integrates its motion from
 * the frame delta, so any rate keeps the game's speed; the floor is just the
 * slowest choice the menu has. The ceiling is the game's own delta floor:
 * TimeHandler cannot report under 1 ms, so a shorter retrace than that makes
 * the delta a lie and everything it drives runs fast. The unlimited choice
 * comes here as 1000 rather than pacing not at all, and an off-menu rate
 * beyond 1000 is held to it for the same reason. */
int Port_GfxFrameRate(void) {
    if (g_port.maxFps == 0 || g_port.maxFps > 1000) {
        return 1000;
    }
    return g_port.maxFps < 30 ? 30 : g_port.maxFps;
}

#define BAR_MIDDLE 50

/* Half a bar is one doubling of the power: 2 at the dark end, 1/2 at the bright. */
f32 Port_GfxGammaPower(void) {
    return g_port.gamma == BAR_MIDDLE ? 1.0f : SDL_powf(2.0f, (f32)(BAR_MIDDLE - g_port.gamma) / BAR_MIDDLE);
}

/* The PC release's textures (port.h): GameCube colours unless graphics.cfg
 * or HWVX_PC_TEXTURES=pc says the PC's own. Read as each texture decodes. */
int Port_PCTexturesLookGC(void) {
    const char *env = SDL_getenv("HWVX_PC_TEXTURES");

    if (env != NULL) {
        return SDL_strcmp(env, "pc") != 0;
    }
    return !g_port.pcTexturesPC;
}

/* The main menu without the port's rows: graphics.cfg, or HWVX_ORIGINAL_MAINMENU
 * (1: original, 0: the port's). Read each time the main menu is created. */
int Port_OriginalMainMenu(void) {
    const char *env = SDL_getenv("HWVX_ORIGINAL_MAINMENU");

    if (env != NULL) {
        return SDL_atoi(env) != 0;
    }
    return g_port.originalMainMenu;
}

/* Split-screen LOD (graphics.cfg "splitlod=full|gc", no menu row;
 * HWVX_SPLITSCREEN_LOD overrides): the GameCube drew every model one level
 * coarser whenever the screen was split; the PC release does that only at
 * low Geometry Quality. Default: full detail. */
int Port_SplitScreenCoarseLOD(void) {
    const char *env = SDL_getenv("HWVX_SPLITSCREEN_LOD");

    if (env != NULL) {
        return SDL_strcmp(env, "gc") == 0;
    }
    return g_port.splitLodGC;
}

f32 Port_GfxFovScale(void) {
    return 1.0f + 0.25f * (f32)(g_port.fov - BAR_MIDDLE) / BAR_MIDDLE;
}

/* Whether showing a frame should wait for the display to finish with the one
 * before it. The measurement knob wins over the setting. */
int Port_VSyncWanted(void) {
    static int noVSync = -1;

    if (noVSync < 0) {
        const char *e = SDL_getenv("HWVX_NO_VSYNC");

        noVSync = e != NULL && *e == '1';
    }
    return !noVSync && g_port.vsync;
}

/* Which choice a rate is; anything unknown starts at 60. */
static u32 FpsIndex(int rate) {
    int i;

    for (i = 0; i < NUM_FPS; i++) {
        if (s_fpsValues[i] == rate) {
            return (u32)i;
        }
    }
    for (i = 0; i < NUM_FPS; i++) {
        if (s_fpsValues[i] == 60) {
            return (u32)i;
        }
    }
    return 0;
}

static const char *const *FpsNames(void) {
    static const char *names[NUM_FPS];
    static int done;
    int i;

    if (!done) {
        for (i = 0; i < NUM_FPS; i++) {
            if (s_fpsValues[i] == 0) {
                SDL_snprintf(s_fpsNames[i], sizeof(s_fpsNames[i]), "Unlimited");
            } else {
                SDL_snprintf(s_fpsNames[i], sizeof(s_fpsNames[i]), "%d FPS", s_fpsValues[i]);
            }
            names[i] = s_fpsNames[i];
        }
        done = 1;
    }
    return names;
}

static char *ConfigPath(void) {
    char *pref = SDL_GetPrefPath("HWVX-SDL", "HotWheelsVelocityX");
    char *path = NULL;

    if (pref != NULL) {
        SDL_asprintf(&path, "%sgraphics.cfg", pref);
        SDL_free(pref);
    }
    return path;
}

/* Fills in what the command line left unset (main_sdl.c, before the window). */
void Port_GfxLoadConfig(int haveSize, int haveFullscreen, int haveVSync, int haveFps) {
    char *path = ConfigPath(), *text, *line, *save;
    int w, h, f, v, fps, bar;

    if (path == NULL) {
        return;
    }
    text = SDL_LoadFile(path, NULL);
    SDL_free(path);
    if (text == NULL) {
        return;
    }
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_sscanf(line, "resolution=%dx%d", &w, &h) == 2 && w > 0 && h > 0 && !haveSize) {
            g_port.winW = w;
            g_port.winH = h;
        } else if (SDL_sscanf(line, "fullscreen=%d", &f) == 1 && !haveFullscreen) {
            g_port.fullscreen = f != 0;
        } else if (SDL_sscanf(line, "vsync=%d", &v) == 1 && (v == 0 || v == 1) && !haveVSync) {
            g_port.vsync = v;
        } else if (SDL_sscanf(line, "fps=%d", &fps) == 1 && (fps >= 30 || fps == 0) && !haveFps) {
            g_port.maxFps = fps;
        } else if (SDL_sscanf(line, "gamma=%d", &bar) == 1 && bar >= 0 && bar <= 100) {
            g_port.gamma = bar;
        } else if (SDL_sscanf(line, "fov=%d", &bar) == 1 && bar >= 0 && bar <= 100) {
            g_port.fov = bar;
        } else if (SDL_strcmp(line, "pctextures=pc") == 0) {
            g_port.pcTexturesPC = 1;
        } else if (SDL_strcmp(line, "pctextures=gc") == 0) {
            g_port.pcTexturesPC = 0;
        } else if (SDL_strcmp(line, "mainmenu=original") == 0) {
            g_port.originalMainMenu = 1;
        } else if (SDL_strcmp(line, "mainmenu=port") == 0) {
            g_port.originalMainMenu = 0;
        } else if (SDL_strcmp(line, "movies=off") == 0) {
            g_port.moviesOff = 1;
        } else if (SDL_strcmp(line, "movies=on") == 0) {
            g_port.moviesOff = 0;
        } else if (SDL_strcmp(line, "splitlod=gc") == 0) {
            g_port.splitLodGC = 1;
        } else if (SDL_strcmp(line, "splitlod=full") == 0) {
            g_port.splitLodGC = 0;
        }
    }
    SDL_free(text);
}

static void SaveConfig(int w, int h, int full) {
    char *path = ConfigPath(), buf[256];
    int n;

    if (path == NULL) {
        return;
    }
    n = SDL_snprintf(buf, sizeof(buf),
                     "resolution=%dx%d\nfullscreen=%d\nvsync=%d\nfps=%d\ngamma=%d\nfov=%d\npctextures=%s\n"
                     "mainmenu=%s\nsplitlod=%s\nmovies=%s\n",
                     w, h, full, g_port.vsync, g_port.maxFps, g_port.gamma, g_port.fov,
                     g_port.pcTexturesPC ? "pc" : "gc", g_port.originalMainMenu ? "original" : "port",
                     g_port.splitLodGC ? "gc" : "full", g_port.moviesOff ? "off" : "on");
    if (!SDL_SaveFile(path, buf, (size_t)n)) {
        Port_Log("graphics.cfg: %s", SDL_GetError());
    }
    SDL_free(path);
}

static SDL_DisplayID Display(void) {
    SDL_DisplayID d = SDL_GetDisplayForWindow(g_port.window);
    return d != 0 ? d : SDL_GetPrimaryDisplay();
}

/* The resolution in use: the fullscreen mode, or the window size. */
static Res Current(void) {
    Res r = { 640, 480 };

    if (SDL_GetWindowFlags(g_port.window) & SDL_WINDOW_FULLSCREEN) {
        const SDL_DisplayMode *m = SDL_GetWindowFullscreenMode(g_port.window);
        if (m == NULL) {
            m = SDL_GetDesktopDisplayMode(Display());
        }
        if (m != NULL) {
            r.w = m->w;
            r.h = m->h;
        }
    } else {
        SDL_GetWindowSize(g_port.window, &r.w, &r.h);
    }
    return r;
}

static void AddMode(int w, int h) {
    u32 i;

    for (i = 0; i < s_numModes; i++) {
        if (s_modes[i].w == w && s_modes[i].h == h) {
            return;
        }
    }
    if (s_numModes < MAX_MODES) {
        s_modes[s_numModes].w = w;
        s_modes[s_numModes].h = h;
        s_numModes++;
    }
}

static int CompareRes(const void *a, const void *b) {
    const Res *x = a, *y = b;
    if (x->w != y->w) return y->w - x->w;
    return y->h - x->h;
}

/* The menu opens: list the display's modes (largest first) and start from
 * the settings in use. */
void Port_GfxMenuOpened(void) {
    SDL_DisplayMode **modes;
    Res cur = Current();
    int count = 0, i;

    s_numModes = 0;
    modes = SDL_GetFullscreenDisplayModes(Display(), &count);
    for (i = 0; modes != NULL && i < count; i++) {
        AddMode(modes[i]->w, modes[i]->h);
    }
    SDL_free(modes);
    AddMode(cur.w, cur.h);
    SDL_qsort(s_modes, s_numModes, sizeof(Res), CompareRes);
    s_pendingRes = 0;
    for (i = 0; i < (int)s_numModes; i++) {
        SDL_snprintf(s_names[i], sizeof(s_names[i]), "%dx%d", s_modes[i].w, s_modes[i].h);
        if (s_modes[i].w == cur.w && s_modes[i].h == cur.h) {
            s_pendingRes = (u32)i;
        }
    }
    s_pendingFull = (SDL_GetWindowFlags(g_port.window) & SDL_WINDOW_FULLSCREEN) != 0;
    s_pendingVSync = g_port.vsync != 0;
    s_pendingFps = FpsIndex(g_port.maxFps);
}

u32 Port_GfxModeCount(void) {
    return s_numModes;
}

const char *Port_GfxModeName(u32 i) {
    return i < s_numModes ? s_names[i] : "";
}

u32 Port_GfxGet(s32 id) {
    switch (id) {
    case PORT_CODEVAR_RESOLUTION: return s_pendingRes;
    case PORT_CODEVAR_DISPLAY: return s_pendingFull;
    case PORT_CODEVAR_VSYNC: return s_pendingVSync;
    case PORT_CODEVAR_MAXFPS: return s_pendingFps;
    case PORT_CODEVAR_GAMMA: return (u32)g_port.gamma;
    case PORT_CODEVAR_FOV: return (u32)g_port.fov;
    }
    return 0;
}

void Port_GfxSet(s32 id, u32 value) {
    switch (id) {
    case PORT_CODEVAR_RESOLUTION:
        if (value < s_numModes) {
            s_pendingRes = value;
        }
        break;
    case PORT_CODEVAR_DISPLAY:
        s_pendingFull = value != 0;
        break;
    case PORT_CODEVAR_VSYNC:
        s_pendingVSync = value != 0;
        break;
    case PORT_CODEVAR_MAXFPS:
        if (value < NUM_FPS) {
            s_pendingFps = value;
        }
        break;
    case PORT_CODEVAR_GAMMA:
        g_port.gamma = value > 100 ? 100 : (int)value;
        break;
    case PORT_CODEVAR_FOV:
        g_port.fov = value > 100 ? 100 : (int)value;
        break;
    }
}

/* The other two rows' choice lists, for the menu to show. */
u32 Port_GfxOptionCount(s32 id) {
    switch (id) {
    case PORT_CODEVAR_VSYNC: return 2;
    case PORT_CODEVAR_MAXFPS: return NUM_FPS;
    }
    return 0;
}

const char *Port_GfxOptionName(s32 id, u32 i) {
    static const char *const vsyncNames[2] = { "Off", "On" };

    switch (id) {
    case PORT_CODEVAR_VSYNC: return i < 2 ? vsyncNames[i] : "";
    case PORT_CODEVAR_MAXFPS: return i < NUM_FPS ? FpsNames()[i] : "";
    }
    return "";
}

/* Sets the fullscreen mode closest to w x h (used when the window goes or
 * starts fullscreen). */
void Port_GfxSetFullscreenMode(int w, int h) {
    SDL_DisplayMode mode;

    if (SDL_GetClosestFullscreenDisplayMode(Display(), w, h, 0.0f, true, &mode)) {
        SDL_SetWindowFullscreenMode(g_port.window, &mode);
    }
}

/* The menu closes: apply what changed and save it. */
void Port_GfxApply(void) {
    Res cur = Current(), r;
    int full = (SDL_GetWindowFlags(g_port.window) & SDL_WINDOW_FULLSCREEN) != 0;
    int rate;

    if (s_pendingFps >= NUM_FPS) {
        s_pendingFps = FpsIndex(60);
    }
    rate = s_fpsValues[s_pendingFps];

    /* VSync and the frame rate cost no display mode change, so they are dealt
     * with before the mode is compared, and saved even if nothing else moved. */
    if ((int)s_pendingVSync != g_port.vsync || rate != g_port.maxFps) {
        if (rate > 0) {
            Port_Log("graphics: vsync %s, max %d FPS", (int)s_pendingVSync ? "on" : "off", rate);
        } else {
            Port_Log("graphics: vsync %s, max unlimited", (int)s_pendingVSync ? "on" : "off");
        }
        g_port.vsync = (int)s_pendingVSync;
        g_port.maxFps = rate;
    }
    if (s_numModes == 0) {
        return;
    }
    r = s_modes[s_pendingRes];
    if (r.w == cur.w && r.h == cur.h && (int)s_pendingFull == full) {
        SaveConfig(r.w, r.h, (int)s_pendingFull);
        return;
    }
    Port_Log("graphics: %dx%d %s", r.w, r.h, s_pendingFull ? "fullscreen" : "windowed");
    if (s_pendingFull) {
        Port_GfxSetFullscreenMode(r.w, r.h);
        SDL_SetWindowFullscreen(g_port.window, true);
    } else {
        SDL_SetWindowFullscreen(g_port.window, false);
        SDL_SetWindowSize(g_port.window, r.w, r.h);
        SDL_SetWindowPosition(g_port.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_SyncWindow(g_port.window);
    g_port.fullscreen = (int)s_pendingFull;
    SaveConfig(r.w, r.h, (int)s_pendingFull);
}
