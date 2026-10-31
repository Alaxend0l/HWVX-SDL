/* Graphics settings: the Options screen's "Graphics" menu (Port_Menu.c in
 * src/game) edits these through two code variables, and they are kept in
 * <pref>/graphics.cfg next to the memory card files.
 *
 * - Resolution: one of the display's fullscreen modes (SDL), or the
 *   current window size if it isn't one of them. Windowed it sets the window
 *   size, fullscreen the display mode. Any shape works: the renderer follows
 *   the window (gx_draw.c).
 * - Display: windowed or fullscreen.
 *
 * The menu changes pending values; Port_GfxApply applies them when the menu
 * closes, so stepping through modes doesn't switch the display each time. */
#include "port_internal.h"

#define MAX_MODES 48

typedef struct Res {
    int w, h;
} Res;

static Res s_modes[MAX_MODES];
static char s_names[MAX_MODES][16];
static u32 s_numModes;
static u32 s_pendingRes, s_pendingFull;

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
void Port_GfxLoadConfig(int haveSize, int haveFullscreen) {
    char *path = ConfigPath(), *text, *line, *save;
    int w, h, f;

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
        }
    }
    SDL_free(text);
}

static void SaveConfig(int w, int h, int full) {
    char *path = ConfigPath(), buf[64];
    int n;

    if (path == NULL) {
        return;
    }
    n = SDL_snprintf(buf, sizeof(buf), "resolution=%dx%d\nfullscreen=%d\n", w, h, full);
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
    }
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

    if (s_numModes == 0) {
        return;
    }
    r = s_modes[s_pendingRes];
    if (r.w == cur.w && r.h == cur.h && (int)s_pendingFull == full) {
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
