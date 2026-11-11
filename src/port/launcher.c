/* The launcher: where the player imports the releases' game data, picks the
 * one to play, and (later) converts data to .xdl and manages mods.
 *
 * The launcher opens on every start, unless the player ticked "Start the
 * game without this window" (skip=1): then the game boots straight into the
 * chosen release while it is still there. It opens anyway on the first run
 * (nothing found), when the chosen data has gone, and when asked: --launcher,
 * or the executable run under a name with "launcher" in it (the build makes a
 * hwvx-launcher copy). Without a window (HWVX_NO_DIALOG, scripted runs) the
 * chosen release boots whatever the tick.
 *
 * Sources:
 * - the releases (PORT_DATA_*), one of which is played: the GameCube's (a
 *   disc image, or the extracted disc files), the PC's (its install folder,
 *   HW_share.xpc, or its disc image: an .iso or .cue/.bin, imported into a
 *   folder of the player's own by pcdisc.c, movies included, which the PC
 *   game kept on the disc) and the PS2's (its disc files,
 *   XPS/ENGLISH/HW_share.xps, or its disc image, an .iso whose XPS/ENGLISH
 *   pcdisc.c copies into a folder of the player's own);
 * - the PS2 prototype (archive version 177/179; the retail PS2's are 183):
 *   not playable, kept for its beta cars, worlds and audio (later). Its disc
 *   image is imported like the PS2's; either disc goes where its version
 *   says, whichever row it was picked from.
 * Kept in <pref>/sources.cfg as "gc=PATH", "pc=PATH", "ps2=PATH",
 * "ps2beta=PATH", "play=gc|pc|ps2" and "skip=1"; the GameCube data the port found
 * before the launcher existed (datadir.txt) is taken over on the first run.
 *
 * It is plain SDL: a window with an SDL_Renderer drawing SDL's built-in 8x8
 * text at a logical 640x420. Mouse, keyboard (arrows, Tab, Enter, Escape) and
 * gamepads (d-pad or left stick, A, B, Start: Play) move a focus between the
 * buttons. Folders and images are picked with the system's dialogs, which
 * need a mouse or a keyboard.
 *
 * "Import saves..." takes progress from any release's saves (Port_SaveImport.c:
 * GameCube .gci and card images, the PC's velocityX, PS2 card images and
 * .psu, the port's own .sav): the files are read, then a preview says which
 * saves go where and what each adds, with the choices (one save only, the
 * PC/PS2 default cars), and Import merges them into the JSON save store.
 * HWVX_LAUNCHER_IMPORT="FILE;FILE" opens that preview on start (testing). */
#include "port_internal.h"
#include "Port_Saves.h"

#define VIEW_W 640
#define VIEW_H 420
#define CHAR 8

/* The sources: the releases, then the prototype. */
enum { SRC_BETA = PORT_DATA_COUNT, SRC_COUNT };

static const char *const s_keys[SRC_COUNT] = { "gc", "pc", "ps2", "ps2beta" };
static const char *const s_names[SRC_COUNT] = { "GameCube", "PC", "PlayStation 2", "PS2 prototype (beta)" };

/* The pages, in tab order; the names are sources.cfg's "tab=" and
 * HWVX_LAUNCHER_TAB's. */
enum { TAB_PLAY, TAB_GAMES, TAB_SAVES, TAB_CONVERT, TAB_MODS, TAB_SETTINGS, TAB_COUNT };

static const char *const s_tabKeys[TAB_COUNT] = { "play", "games", "saves", "convert", "mods", "settings" };
static const char *const s_tabNames[TAB_COUNT] = { "Play", "Games", "Saves", "Convert", "Mods", "Settings" };

static int TabNamed(const char *name) {
    int i;

    for (i = 0; name != NULL && i < TAB_COUNT; i++) {
        if (SDL_strcasecmp(name, s_tabKeys[i]) == 0) {
            return i;
        }
    }
    return -1;
}

typedef struct Sources {
    char *path[SRC_COUNT];
    int play;           /* PORT_DATA_*, or -1 */
    int skip;           /* start the game without the launcher */
    int tab;            /* TAB_* last shown, or -1 */
} Sources;

/* ---- sources.cfg ----------------------------------------------------- */

static char *PrefFile(const char *name) {
    char *pref = Port_PrefPath(), *path = NULL;

    if (pref != NULL) {
        SDL_asprintf(&path, "%s%s", pref, name);
        SDL_free(pref);
    }
    return path;
}

/* Returns 0 when there is no sources.cfg yet: the first run. */
static int LoadSources(Sources *s) {
    char *path = PrefFile("sources.cfg"), *text, *line, *save = NULL;
    int i;

    SDL_zerop(s);
    s->play = -1;
    s->tab = -1;
    text = path != NULL ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (text == NULL) {
        return 0;
    }
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strcmp(line, "skip=1") == 0) {
            s->skip = 1;
        }
        if (SDL_strncmp(line, "tab=", 4) == 0) {
            s->tab = TabNamed(line + 4);
        }
        for (i = 0; i < SRC_COUNT; i++) {
            size_t n = SDL_strlen(s_keys[i]);

            if (SDL_strncmp(line, s_keys[i], n) == 0 && line[n] == '=' && line[n + 1] != '\0') {
                SDL_free(s->path[i]);
                s->path[i] = SDL_strdup(line + n + 1);
            }
            if (i < PORT_DATA_COUNT && SDL_strncmp(line, "play=", 5) == 0 && SDL_strcmp(line + 5, s_keys[i]) == 0) {
                s->play = i;
            }
        }
    }
    SDL_free(text);
    return 1;
}

static void SaveSources(const Sources *s) {
    char *path = PrefFile("sources.cfg");
    SDL_IOStream *io = path != NULL ? SDL_IOFromFile(path, "w") : NULL;
    int i, ok;

    if (io == NULL) {
        Port_Log("sources.cfg: %s", SDL_GetError());
        SDL_free(path);
        return;
    }
    ok = 1;
    for (i = 0; i < SRC_COUNT; i++) {
        if (s->path[i] != NULL) {
            ok &= SDL_IOprintf(io, "%s=%s\n", s_keys[i], s->path[i]) > 0;
        }
    }
    if (s->play >= 0) {
        ok &= SDL_IOprintf(io, "play=%s\n", s_keys[s->play]) > 0;
    }
    if (s->skip) {
        ok &= SDL_IOprintf(io, "skip=1\n") > 0;
    }
    if (s->tab >= 0) {
        ok &= SDL_IOprintf(io, "tab=%s\n", s_tabKeys[s->tab]) > 0;
    }
    if (!SDL_CloseIO(io) || !ok) {
        Port_Log("sources.cfg: %s", SDL_GetError());
    }
    SDL_free(path);
}

static void FreeSources(Sources *s) {
    int i;

    for (i = 0; i < SRC_COUNT; i++) {
        SDL_free(s->path[i]);
        s->path[i] = NULL;
    }
}

/* ---- which source a place holds ---------------------------------------- */

/* The file at `rel` (components separated by '/') under `dir`, each
 * component matched without case: disc files keep the disc's spelling, which
 * a case-sensitive file system doesn't forgive (HW_SHARE.XPS, a mounted
 * PS2 disc's hw_share.xps). Returns the real path, or NULL. */
typedef struct NoCase {
    const char *want;
    size_t len;
    char *found;
} NoCase;

static SDL_EnumerationResult SDLCALL MatchNoCase(void *userdata, const char *dir, const char *name) {
    NoCase *m = userdata;

    (void)dir;
    if (SDL_strlen(name) == m->len && SDL_strncasecmp(name, m->want, m->len) == 0) {
        m->found = SDL_strdup(name);
        return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

char *Port_FindNoCase(const char *dir, const char *rel) {
    char *path = SDL_strdup(dir);

    while (path != NULL && *rel != '\0') {
        const char *slash = SDL_strchr(rel, '/');
        NoCase m = { rel, slash ? (size_t)(slash - rel) : SDL_strlen(rel), NULL };
        char *next = NULL;
        SDL_PathInfo info;

        SDL_asprintf(&next, "%s/%.*s", path, (int)m.len, rel);
        if (next == NULL || !SDL_GetPathInfo(next, &info)) {
            SDL_free(next);
            next = NULL;
            SDL_EnumerateDirectory(path, MatchNoCase, &m);
            if (m.found != NULL) {
                SDL_asprintf(&next, "%s/%s", path, m.found);
                SDL_free(m.found);
            }
        }
        SDL_free(path);
        path = next;
        rel += m.len + (slash != NULL);
    }
    return path;
}

static int IsFile(const char *dir, const char *name) {
    char *path = Port_FindNoCase(dir, name);
    SDL_PathInfo info;
    int is = path != NULL && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;

    SDL_free(path);
    return is;
}

/* The archive version of a PS2 disc's HW_share (183 retail, 177/179 the
 * prototype), or 0 if it has none. */
static u32 PS2Version(const char *dir) {
    static const char *const names[] = { "XPS/ENGLISH/HW_share.xps", "HW_share.xps" };
    u8 head[12];
    size_t i;

    for (i = 0; i < SDL_arraysize(names); i++) {
        char *path = Port_FindNoCase(dir, names[i]);
        SDL_IOStream *io = path != NULL ? SDL_IOFromFile(path, "rb") : NULL;

        SDL_free(path);
        if (io != NULL) {
            size_t got = SDL_ReadIO(io, head, sizeof(head));

            SDL_CloseIO(io);
            if (got == sizeof(head)) {
                return (u32)head[8] | (u32)head[9] << 8 | (u32)head[10] << 16 | (u32)head[11] << 24;
            }
        }
    }
    return 0;
}

/* The source the place `path` holds, and its resolved location in *found,
 * or -1. A folder of GameCube files with other releases' packs mixed in
 * counts as the GameCube's. */
static int SourceOf(Port_DataCheck check, const char *path, char **found) {
    SDL_PathInfo info;
    u32 v;

    *found = NULL;
    if (path == NULL) {
        return -1;
    }
    v = PS2Version(path);
    if (v == 177 || v == 179) {
        *found = SDL_strdup(path);      /* the prototype: not for the port's loader */
        return SRC_BETA;
    }
    *found = check(path);
    if (*found == NULL) {
        return v == 183 ? -2 : -1;     /* -2: the retail PS2's, but missing files the port needs */
    }
    if (SDL_GetPathInfo(*found, &info) && info.type == SDL_PATHTYPE_FILE) {
        return PORT_DATA_GC;     /* the port opens GameCube disc images only */
    }
    if (IsFile(*found, "HW_share.xgc")) {
        return PORT_DATA_GC;
    }
    if (IsFile(*found, "HW_share.xpc")) {
        return PORT_DATA_PC;
    }
    if (PS2Version(*found) == 183) {
        return PORT_DATA_PS2;
    }
    SDL_free(*found);
    *found = NULL;
    return -1;
}

static char *Absolute(const char *path) {
    char *cwd, *abs = NULL;

    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':')) {
        return SDL_strdup(path);
    }
    cwd = SDL_GetCurrentDirectory();   /* ends in a separator */
    if (cwd == NULL || SDL_asprintf(&abs, "%s%s", cwd, path) < 0) {
        abs = SDL_strdup(path);
    }
    SDL_free(cwd);
    return abs;
}

/* ---- the window -------------------------------------------------------- */

enum {
    ACT_NONE, ACT_PLAY_THIS, ACT_FOLDER, ACT_IMAGE, ACT_REMOVE, ACT_PLAY, ACT_QUIT, ACT_CONVERT, ACT_SKIP,
    ACT_SAVES,          /* "Import saves...": pick files */
    ACT_IMP_ONLY,       /* the preview: which saves (all, or one) */
    ACT_IMP_CARS,       /* the PC/PS2 default cars */
    ACT_IMP_GO, ACT_IMP_ADD, ACT_IMP_CANCEL,
    ACT_TAB,            /* show page `source` */
    ACT_SET             /* toggle setting `source` */
};

/* The Settings page: graphics.cfg's choices that have no row in the game's
 * own Graphics menu, and the display (gfx_settings.c reads and writes the
 * file; these are its g_port fields, 1 being `on`). */
enum { SET_DISPLAY, SET_PCTEX, SET_MAINMENU, SET_SPLITLOD, SET_MOVIES, SET_COUNT };

static const struct {
    const char *name, *off, *on, *about, *env;
} s_settings[SET_COUNT] = {
    { "Display", "Windowed", "Fullscreen", "How the game's window opens.", NULL },
    { "PC textures", "GameCube colours", "PC colours",
      "The PC release's textures: in the GameCube's colours, or the PC's own.", "HWVX_PC_TEXTURES" },
    { "Main menu", "With the port's rows", "Original",
      "The port adds Quit Game and Multi-Player; original is the release's.", "HWVX_ORIGINAL_MAINMENU" },
    { "Split screen", "Full detail", "GameCube's detail",
      "Full detail like the PC, or a level coarser as the GameCube drew it.", "HWVX_SPLITSCREEN_LOD" },
    { "Movies", "On", "Off", "The intro and ending movies; off skips them, like the PC's option.", "HWVX_NO_MOVIE" },
};

static int *SettingVar(int i) {
    switch (i) {
    case SET_DISPLAY: return &g_port.fullscreen;
    case SET_PCTEX: return &g_port.pcTexturesPC;
    case SET_MAINMENU: return &g_port.originalMainMenu;
    case SET_SPLITLOD: return &g_port.splitLodGC;
    default: return &g_port.moviesOff;
    }
}

typedef struct Button {
    SDL_FRect r;
    const char *label;
    int act, source, enabled;
} Button;

typedef struct Launcher {
    SDL_Window *window;
    SDL_Renderer *renderer;
    Sources src;
    Port_DataCheck check;
    Button buttons[40];
    int numButtons, focus;
    int numFocus;               /* the page's and the bottom bar's buttons come first; the tabs
                                 * after them take clicks but not the focus */
    int tab;                    /* TAB_*: the page shown */
    int hover, mouse;           /* the enabled button under the pointer (-1 none); 1 while the mouse leads */
    SDL_FPoint pointer;         /* where the pointer is, in view coordinates (-1,-1 outside) */
    char status[256];
    /* a dialog's answer, from SDL's callback thread */
    SDL_AtomicInt picked;       /* 0 waiting, 1 answered, 2 no dialog */
    char *pickedPath;
    int pickSource, pickImage, dialogOpen;
    /* a PC or PS2 disc being imported (pcdisc.c), on a thread */
    SDL_Thread *importer;
    SDL_AtomicInt importDone, importFiles, importCancel;
    int importOk, importSource;
    char importImage[1200], importDest[1200], importError[256];
    /* saves picked to import, and the preview of the merge */
    char *pickedMore[PORT_IMPORT_MAX_SOURCES];
    int pickedCount, pickSaves;
    int savesView;              /* the preview is showing */
    PortImportSource saves[PORT_IMPORT_MAX_SOURCES];
    int numSaves, only;         /* only: 0 every save, k the k-th (file, slot) */
    PortImportOptions opt;
    PortImportPlan plan;
    int planOk;
    char planError[256], onlyLabel[96], carsLabel[64];
} Launcher;

/* The title and the tabs share the top line; the pages start under it. */
#define TAB_X 216.0f
#define TAB_Y 8.0f
#define PAGE_Y 42.0f
#define ROW_Y(i) (PAGE_Y + (i) * 58.0f)                 /* Games: a release */
#define PLAY_ROW_Y(k) (PAGE_Y + 24.0f + (k) * 34.0f)    /* Play: an imported release */
#define SKIP_Y(rows) (PLAY_ROW_Y((rows) > 2 ? (rows) : 2) + 12.0f)  /* under the releases */
#define SET_ROW_Y(i) (PAGE_Y + 22.0f + (i) * 42.0f)     /* Settings: a setting */
#define SET_X 200.0f

static void Text(Launcher *l, float x, float y, Uint8 r, Uint8 g, Uint8 b, const char *s) {
    SDL_SetRenderDrawColor(l->renderer, r, g, b, 255);
    SDL_RenderDebugText(l->renderer, x, y, s);
}

/* `s` cut to `chars` characters, its start replaced by "..." when longer. */
static void Fit(char *out, size_t size, const char *s, size_t chars) {
    size_t n = SDL_strlen(s);

    if (n <= chars) {
        SDL_strlcpy(out, s, size);
    } else {
        SDL_snprintf(out, size, "...%s", s + n - (chars - 3));
    }
}

static Button *Add(Launcher *l, float x, float y, const char *label, int act, int source, int enabled) {
    Button *b = &l->buttons[l->numButtons++];

    b->r.x = x;
    b->r.y = y;
    b->r.w = (float)(SDL_strlen(label) * CHAR + 16);
    b->r.h = 18;
    b->label = label;
    b->act = act;
    b->source = source;
    b->enabled = enabled;
    return b;
}

static int AnyOtherRelease(const Launcher *l) {
    int i;
    for (i = 0; i < l->numSaves; i++) {
        if (l->saves[i].release != PORT_RELEASE_GC) {
            return 1;
        }
    }
    return 0;
}

/* The preview's buttons: which saves, the default cars, Import. */
static void LayoutSaves(Launcher *l) {
    int busy = l->dialogOpen;

    Add(l, 24, VIEW_H - 92, l->onlyLabel, ACT_IMP_ONLY, -1, !busy);
    if (AnyOtherRelease(l)) {
        Add(l, 24, VIEW_H - 70, l->carsLabel, ACT_IMP_CARS, -1, !busy);
    }
    Add(l, 24, VIEW_H - 30, "Import", ACT_IMP_GO, -1, !busy && l->planOk && l->plan.changes > 0);
    Add(l, 24 + 6 * CHAR + 24, VIEW_H - 30, "Add file...", ACT_IMP_ADD, -1,
        !busy && l->numSaves < PORT_IMPORT_MAX_SOURCES);
    Add(l, 24 + 17 * CHAR + 48, VIEW_H - 30, "Cancel", ACT_IMP_CANCEL, -1, 1);
}

/* 1 if any release the game plays is imported. */
static int AnyRelease(const Launcher *l) {
    int i;

    for (i = 0; i < PORT_DATA_COUNT && l->src.path[i] == NULL; i++) {
    }
    return i < PORT_DATA_COUNT;
}

/* The current page's buttons. */
static void LayoutPage(Launcher *l, int busy) {
    int i, k;

    switch (l->tab) {
    case TAB_PLAY:
        for (i = 0, k = 0; i < PORT_DATA_COUNT; i++) {
            if (l->src.path[i] != NULL) {
                Add(l, 24, PLAY_ROW_Y(k++), l->src.play == i ? "Playing this" : "Play this", ACT_PLAY_THIS, i, !busy);
            }
        }
        if (k == 0) {
            Add(l, 24, PLAY_ROW_Y(1), "Import a copy on the Games page", ACT_TAB, TAB_GAMES, 1);
        }
        Add(l, 24, SKIP_Y(k),
            l->src.skip ? "[x] Start the game without this window" : "[ ] Start the game without this window", ACT_SKIP,
            -1, 1);
        break;
    case TAB_GAMES:
        for (i = 0; i < SRC_COUNT; i++) {
            float x = 24, y = ROW_Y(i) + 28;
            int have = l->src.path[i] != NULL;

            x += Add(l, x, y, have ? "Change folder..." : "Import folder...", ACT_FOLDER, i, !busy)->r.w + 8;
            x += Add(l, x, y, "Import disc image...", ACT_IMAGE, i, !busy)->r.w + 8;
            if (have) {
                Add(l, x, y, "Remove", ACT_REMOVE, i, !busy);
            }
        }
        break;
    case TAB_SAVES:
        Add(l, 24, PAGE_Y + 24, "Import saves...", ACT_SAVES, -1, !busy);
        break;
    case TAB_CONVERT:
        Add(l, 24, PAGE_Y + 24, "Convert to .xdl", ACT_CONVERT, -1, 0);
        break;
    case TAB_SETTINGS:
        for (i = 0; i < SET_COUNT; i++) {
            Add(l, SET_X, SET_ROW_Y(i), *SettingVar(i) ? s_settings[i].on : s_settings[i].off, ACT_SET, i, 1);
        }
        break;
    default:
        break;
    }
}

/* The buttons for the current state, in focus order, then the tabs. */
static void Layout(Launcher *l) {
    int i, busy = l->importer != NULL || l->dialogOpen;
    float x = TAB_X;

    l->numButtons = 0;
    if (l->savesView) {
        LayoutSaves(l);
    } else {
        LayoutPage(l, busy);
        Add(l, 24, VIEW_H - 30, "Play", ACT_PLAY, -1, !busy && l->src.play >= 0 && l->src.path[l->src.play] != NULL);
        Add(l, 24 + 4 * CHAR + 24, VIEW_H - 30, "Quit", ACT_QUIT, -1, 1);
    }
    l->numFocus = l->numButtons;
    /* the preview of an import keeps to its page */
    for (i = 0; i < TAB_COUNT; i++) {
        x += Add(l, x, TAB_Y, s_tabNames[i], ACT_TAB, i, !l->savesView)->r.w + 4;
    }
    if (l->focus >= l->numFocus) {
        l->focus = l->numFocus - 1;
    }
    /* the buttons may have moved (one added or removed) under a still pointer */
    l->hover = -1;
    for (i = 0; i < l->numButtons; i++) {
        if (l->buttons[i].enabled && SDL_PointInRectFloat(&l->pointer, &l->buttons[i].r)) {
            l->hover = i;
        }
    }
}

static const char *BaseName(const char *path) {
    const char *a = SDL_strrchr(path, '/'), *b = SDL_strrchr(path, '\\');
    if (b > a) {
        a = b;
    }
    return a != NULL ? a + 1 : path;
}

/* `s` over lines of at most `chars`, broken at spaces, the later ones
 * indented by `hang`; returns the next y, or stops at `bottom`. */
static float Wrap(Launcher *l, float x, float y, float bottom, size_t chars, size_t hang, Uint8 r, Uint8 g, Uint8 b,
                  const char *s) {
    char line[128];
    size_t indent = 0;

    while (*s != '\0' && y + 8 <= bottom) {
        size_t n = SDL_strlen(s), cut = n;

        if (n + indent > chars) {
            for (cut = chars - indent; cut > 0 && s[cut] != ' '; cut--) {
            }
            if (cut == 0) {
                cut = chars - indent;
            }
        }
        SDL_snprintf(line, sizeof(line), "%*s%.*s", (int)indent, "", (int)cut, s);
        Text(l, x, y, r, g, b, line);
        y += 11;
        s += cut;
        while (*s == ' ') {
            s++;
        }
        indent = hang;
    }
    return y;
}

/* The preview: the files, where they go, what each save adds. */
static void DrawSaves(Launcher *l) {
    char line[256], fit[128];
    float y = PAGE_Y + 30, bottom = VIEW_H - 98;
    int i, shown = 0;

    Text(l, 24, PAGE_Y, 235, 235, 240, "Import saves: the preview");
    Text(l, 24, PAGE_Y + 14, 150, 160, 180, "Progress from any release, merged into your save: nothing is taken away.");
    for (i = 0; i < l->numSaves; i++, y += 11) {
        const PortImportSource *s = &l->saves[i];

        SDL_snprintf(line, sizeof(line), "%s - %s, %s", BaseName(s->path), Port_SaveReleaseName(s->release),
                     Port_SaveContainerName(s->container));
        Fit(fit, sizeof(fit), line, 74);
        Text(l, 24, y, 140, 200, 140, fit);
    }
    y += 4;
    SDL_snprintf(line, sizeof(line), "Into: %s", l->plan.target);
    Fit(fit, sizeof(fit), line, 74);
    Text(l, 24, y, 235, 235, 240, fit);
    y += 16;
    if (!l->planOk) {
        return;
    }
    for (i = 0; i < l->plan.numSlots && y + 8 <= bottom; i++, shown++) {
        const PortImportSlot *s = &l->plan.slots[i];

        Port_SaveImportDescribe(&l->plan, i, &l->opt, line, sizeof(line));
        if (l->numSaves > 1) {
            char from[300];
            SDL_snprintf(from, sizeof(from), "%s: %s", BaseName(l->saves[s->source].path), line);
            SDL_strlcpy(line, from, sizeof(line));
        }
        y = Wrap(l, 24, y, bottom, 74, 4, s->changes > 0 ? 235 : 150, s->changes > 0 ? 235 : 160,
                 s->changes > 0 ? 240 : 180, line) + 3;
    }
    if (shown < l->plan.numSlots) {
        SDL_snprintf(line, sizeof(line), "... and %d more", l->plan.numSlots - shown);
        Text(l, 24, bottom, 150, 160, 180, line);
    }
}

#define GREY 150, 160, 180
#define DIM 130, 130, 140
#define WHITE 235, 235, 240
#define GREEN 140, 200, 140

/* The current page's text (its buttons are drawn with the rest). */
static void DrawPage(Launcher *l) {
    char line[160];
    int i, k;
    float y;

    switch (l->tab) {
    case TAB_PLAY:
        Text(l, 24, PAGE_Y, GREY, "Your copies of the game. Pick the one to play.");
        for (i = 0, k = 0; i < PORT_DATA_COUNT; i++) {
            if (l->src.path[i] != NULL) {
                y = PLAY_ROW_Y(k++);
                Text(l, 148, y + 1, WHITE, s_names[i]);
                Fit(line, sizeof(line), l->src.path[i], 58);
                Text(l, 148, y + 12, GREEN, line);
            }
        }
        if (k == 0) {
            Text(l, 24, PLAY_ROW_Y(0) + 4, DIM, "No release imported yet.");
        }
        Text(l, 24, SKIP_Y(k) + 26, DIM, "Ticked, the game boots the chosen copy straight away; --launcher,");
        Text(l, 24, SKIP_Y(k) + 37, DIM, "or the hwvx-launcher program, opens this window again.");
        break;
    case TAB_GAMES:
        for (i = 0; i < SRC_COUNT; i++) {
            y = ROW_Y(i);
            Text(l, 24, y, WHITE, s_names[i]);
            if (i == SRC_BETA) {
                Text(l, 24 + 21 * CHAR, y, DIM, "- for beta cars, worlds, audio (later)");
            }
            if (l->src.path[i] != NULL) {
                Fit(line, sizeof(line), l->src.path[i], 74);
                Text(l, 24, y + 13, GREEN, line);
            } else {
                Text(l, 24, y + 13, DIM, "not imported");
            }
        }
        y = ROW_Y(SRC_COUNT) + 4;
        if (l->importer != NULL) {
            SDL_snprintf(line, sizeof(line), "Importing the %s disc image: %d files so far.",
                         l->importSource == PORT_DATA_PC ? "PC" : "PS2", SDL_GetAtomicInt(&l->importFiles));
            Text(l, 24, y, 255, 210, 80, line);
            Text(l, 24, y + 11, DIM, "Quit stops it and keeps the last complete copy.");
        } else {
            Text(l, 24, y, DIM, "PC and PS2 disc images are copied into the save folder.");
            Text(l, 24, y + 11, DIM, "Folders and images are picked in the system's file dialog.");
        }
        break;
    case TAB_SAVES:
        Text(l, 24, PAGE_Y, GREY, "Progress from any release, merged into your save: nothing is taken away.");
        y = PAGE_Y + 58;
        Text(l, 24, y, WHITE, "What it reads");
        Text(l, 24, y + 16, GREEN, "GameCube");
        Text(l, 104, y + 16, DIM, "Dolphin .gci exports, raw card images, the port's .sav");
        Text(l, 24, y + 30, GREEN, "PC");
        Text(l, 104, y + 30, DIM, "velocityX, in My Documents\\VelocityX Saved Games");
        Text(l, 24, y + 44, GREEN, "PS2");
        Text(l, 104, y + 44, DIM, "PCSX2 card images (.ps2), BASLUS-20412, .psu exports");
        Text(l, 24, y + 66, DIM, "Pick one file or several. A preview says which saves go where, and");
        Text(l, 24, y + 77, DIM, "what each adds, before anything is written.");
        break;
    case TAB_CONVERT:
        Text(l, 24, PAGE_Y, GREY, "Coming later.");
        Wrap(l, 24, PAGE_Y + 58, VIEW_H - 56, 74, 0, DIM,
             "This will convert your imported releases into .xdl, the packs made for this port: one "
             "format for every release's data, which loads without being converted again, and which "
             "the port's editors and mods work on.");
        break;
    case TAB_MODS:
        Text(l, 24, PAGE_Y, GREY, "Coming later.");
        Wrap(l, 24, PAGE_Y + 24, VIEW_H - 56, 74, 0, DIM,
             "Mods will change and add to the game. They carry no game data of their own: each one "
             "is built from your imported copy, on this machine.");
        break;
    case TAB_SETTINGS:
        Text(l, 24, PAGE_Y, GREY, "Kept in graphics.cfg; the game reads them when it starts.");
        for (i = 0; i < SET_COUNT; i++) {
            const char *env = s_settings[i].env != NULL ? SDL_getenv(s_settings[i].env) : NULL;

            y = SET_ROW_Y(i);
            Text(l, 24, y + 5, WHITE, s_settings[i].name);
            if (env != NULL) {
                SDL_snprintf(line, sizeof(line), "%s=%s is set: it wins over this.", s_settings[i].env, env);
                Text(l, 24, y + 22, 255, 190, 120, line);
            } else {
                Text(l, 24, y + 22, DIM, s_settings[i].about);
            }
        }
        y = SET_ROW_Y(SET_COUNT) + 4;
        Text(l, 24, y, DIM, "Resolution, VSync, Max FPS, gamma and FOV: Options > Graphics in");
        Text(l, 24, y + 11, DIM, "the game. The renderer: --renderer NAME.");
        break;
    default:
        break;
    }
}

static void Draw(Launcher *l) {
    int i;

    SDL_SetRenderDrawColor(l->renderer, 18, 22, 34, 255);
    SDL_RenderClear(l->renderer);
    Text(l, 24, TAB_Y + 5, 255, 210, 80, "Hot Wheels: Velocity X");
    SDL_SetRenderDrawColor(l->renderer, 60, 74, 120, 255);
    SDL_RenderLine(l->renderer, 16, TAB_Y + 18, VIEW_W - 16, TAB_Y + 18);
    if (l->savesView) {
        DrawSaves(l);
    } else {
        DrawPage(l);
    }
    Text(l, 24, VIEW_H - 48, 255, 190, 120, l->status);
    for (i = 0; i < l->numButtons; i++) {
        const Button *b = &l->buttons[i];
        int on = l->mouse ? i == l->hover : i == l->focus;

        if (i >= l->numFocus) {
            /* a tab: the page shown joins the line under the tabs */
            int shown = b->source == l->tab;

            SDL_SetRenderDrawColor(l->renderer, shown ? 60 : 30, shown ? 74 : 35, shown ? 120 : 52, 255);
            SDL_RenderFillRect(l->renderer, &b->r);
            if (on) {
                SDL_SetRenderDrawColor(l->renderer, 255, 210, 80, 255);
                SDL_RenderRect(l->renderer, &b->r);
            }
            if (shown) {
                Text(l, b->r.x + 8, b->r.y + 5, 255, 210, 80, b->label);
            } else if (b->enabled) {
                Text(l, b->r.x + 8, b->r.y + 5, 200, 200, 210, b->label);
            } else {
                Text(l, b->r.x + 8, b->r.y + 5, 90, 90, 100, b->label);
            }
            continue;
        }
        if (b->act == ACT_PLAY_THIS && l->src.play == b->source) {
            SDL_SetRenderDrawColor(l->renderer, 40, 110, 60, 255);
        } else {
            SDL_SetRenderDrawColor(l->renderer, on ? 70 : 38, on ? 90 : 44, on ? 150 : 64, 255);
        }
        SDL_RenderFillRect(l->renderer, &b->r);
        if (on) {
            SDL_SetRenderDrawColor(l->renderer, 255, 210, 80, 255);
            SDL_RenderRect(l->renderer, &b->r);
        }
        if (b->enabled) {
            Text(l, b->r.x + 8, b->r.y + 5, 240, 240, 245, b->label);
        } else {
            Text(l, b->r.x + 8, b->r.y + 5, 110, 110, 120, b->label);
        }
    }
    if (SDL_getenv("HWVX_LAUNCHER_SHOT") != NULL) {
        /* testing: the frame as a BMP, read before it is presented */
        SDL_Surface *shot = SDL_RenderReadPixels(l->renderer, NULL);

        if (shot != NULL) {
            SDL_SaveBMP(shot, SDL_getenv("HWVX_LAUNCHER_SHOT"));
            SDL_DestroySurface(shot);
        }
    }
    SDL_RenderPresent(l->renderer);
}

static void SDLCALL Picked(void *userdata, const char *const *filelist, int filter) {
    Launcher *l = userdata;

    (void)filter;
    if (filelist != NULL && filelist[0] != NULL) {
        l->pickedPath = SDL_strdup(filelist[0]);
    }
    l->pickedCount = 0;
    while (filelist != NULL && filelist[l->pickedCount] != NULL && l->pickedCount < PORT_IMPORT_MAX_SOURCES) {
        l->pickedMore[l->pickedCount] = SDL_strdup(filelist[l->pickedCount]);
        l->pickedCount++;
    }
    SDL_SetAtomicInt(&l->picked, filelist == NULL ? 2 : 1);
}

/* A picked folder or image: checked, filed under the source it holds. */
static void Import(Launcher *l, const char *path, int asked) {
    char *found;
    int src = SourceOf(l->check, path, &found);

    if (src == -2) {
        SDL_snprintf(l->status, sizeof(l->status), "That PlayStation 2 copy is missing files the game needs.");
        return;
    }
    if (src < 0) {
        SDL_snprintf(l->status, sizeof(l->status), "No game data there (nor in a files/ folder or disc image in it).");
        return;
    }
    SDL_free(l->src.path[src]);
    l->src.path[src] = Absolute(found);
    SDL_free(found);
    if (src < PORT_DATA_COUNT && (l->src.play < 0 || l->src.path[l->src.play] == NULL || src == asked)) {
        l->src.play = src;
    }
    SaveSources(&l->src);
    if (src != asked) {
        SDL_snprintf(l->status, sizeof(l->status), "That is the %s: imported as such.", s_names[src]);
    } else {
        SDL_snprintf(l->status, sizeof(l->status), "%s imported.", s_names[src]);
    }
}

static int SDLCALL ImportDisc(void *userdata) {
    Launcher *l = userdata;

    if (l->importSource == PORT_DATA_PC) {
        l->importOk = Port_ImportPCDisc(l->importImage, l->importDest, &l->importFiles, &l->importCancel,
                                        l->importError, sizeof(l->importError));
    } else {
        char pref[1200];

        SDL_strlcpy(pref, l->importDest, sizeof(pref));
        l->importOk = Port_ImportPS2Disc(l->importImage, pref, l->importDest, sizeof(l->importDest),
                                         &l->importFiles, &l->importCancel, l->importError, sizeof(l->importError));
    }
    SDL_SetAtomicInt(&l->importDone, 1);
    return 0;
}

/* A picked PC or PS2 disc image: its files copied into <pref>/pc, <pref>/ps2
 * or <pref>/ps2beta on a thread. */
static void StartDisc(Launcher *l, const char *image, int source) {
    char *pref = Port_PrefPath();

    if (pref == NULL) {
        SDL_snprintf(l->status, sizeof(l->status), "No folder to import into: %s", SDL_GetError());
        return;
    }
    SDL_strlcpy(l->importImage, image, sizeof(l->importImage));
    /* PC: the folder; PS2: the folder the PS2 import picks "ps2" or "ps2beta" in */
    SDL_snprintf(l->importDest, sizeof(l->importDest), "%s%s", pref, source == PORT_DATA_PC ? "pc" : "");
    SDL_free(pref);
    l->importSource = source;
    SDL_SetAtomicInt(&l->importDone, 0);
    SDL_SetAtomicInt(&l->importFiles, 0);
    SDL_SetAtomicInt(&l->importCancel, 0);
    l->importError[0] = '\0';
    l->importer = SDL_CreateThread(ImportDisc, "discimport", l);
    if (l->importer == NULL) {
        SDL_snprintf(l->status, sizeof(l->status), "Could not start the import: %s", SDL_GetError());
    }
}

/* ---- importing saves ---------------------------------------------------- */

static int ButtonFor(const Launcher *l, int act);

static void CloseSaves(Launcher *l) {
    int i;

    for (i = 0; i < l->numSaves; i++) {
        Port_SaveImportClose(&l->saves[i]);
    }
    l->numSaves = 0;
    l->savesView = 0;
    l->only = 0;
    l->focus = 0;
}

/* The k-th (k from 1) save of the files read: its file and slot. 0 if there
 * are fewer. */
static int OnlyPair(const Launcher *l, int k, int *source, int *slot, char *name, size_t size) {
    int s, i;

    for (s = 0; s < l->numSaves; s++) {
        for (i = 0; i < 8; i++) {
            if (Port_SaveImportSlotName(&l->saves[s], i, name, size) && --k == 0) {
                *source = s;
                *slot = i;
                return 1;
            }
        }
    }
    return 0;
}

/* The preview again, after the files or the choices changed. */
static void Replan(Launcher *l) {
    char name[16];
    int src = -1, slot = -1;

    if (l->only > 0 && !OnlyPair(l, l->only, &src, &slot, name, sizeof(name))) {
        l->only = 0;
    }
    l->opt.onlySource = src;
    l->opt.onlySlot = slot;
    if (l->only == 0) {
        SDL_strlcpy(l->onlyLabel, "Saves: every save in these files", sizeof(l->onlyLabel));
    } else if (l->numSaves > 1) {
        SDL_snprintf(l->onlyLabel, sizeof(l->onlyLabel), "Saves: only \"%s\" from %.24s", name,
                     BaseName(l->saves[src].path));
    } else {
        SDL_snprintf(l->onlyLabel, sizeof(l->onlyLabel), "Saves: only \"%s\"", name);
    }
    SDL_snprintf(l->carsLabel, sizeof(l->carsLabel), "[%c] Add cars 0-2 too: PC/PS2 saves start with them",
                 l->opt.defaultCars ? 'x' : ' ');
    l->planOk = Port_SaveImportPreview(l->saves, l->numSaves, &l->opt, &l->plan, l->planError, sizeof(l->planError));
    if (!l->planOk) {
        SDL_snprintf(l->status, sizeof(l->status), "Can't import: %s", l->planError);
    } else if (l->plan.changes == 0) {
        SDL_snprintf(l->status, sizeof(l->status), "Nothing new in these saves.");
    } else if (!Port_SavesJson()) {
        SDL_snprintf(l->status, sizeof(l->status), "The game plays this save when started with HWVX_SAVES=json.");
    } else {
        SDL_snprintf(l->status, sizeof(l->status), "Import keeps your save as it was as a backup.");
    }
}

/* Picked files: read, and the preview shown. */
static void AddSaves(Launcher *l, char *const *paths, int count) {
    char err[256], bad[300] = { 0 };
    int i, j, added = 0, failed = 0;

    for (i = 0; i < count; i++) {
        for (j = 0; j < l->numSaves && SDL_strcmp(l->saves[j].path, paths[i]) != 0; j++) {
        }
        if (j < l->numSaves) {
            continue;   /* already there */
        }
        if (l->numSaves == PORT_IMPORT_MAX_SOURCES) {
            SDL_snprintf(bad, sizeof(bad), "%d files at most.", PORT_IMPORT_MAX_SOURCES);
            failed++;
            break;
        }
        if (Port_SaveImportOpen(paths[i], &l->saves[l->numSaves], err, sizeof(err))) {
            l->numSaves++;
            added++;
        } else {
            SDL_snprintf(bad, sizeof(bad), "%s: %s", BaseName(paths[i]), err);
            failed++;
            Port_Log("launcher: %s not imported: %s", paths[i], err);
        }
    }
    if (l->numSaves > 0) {
        l->savesView = 1;
        Replan(l);
        Layout(l);
        l->focus = ButtonFor(l, l->plan.changes > 0 ? ACT_IMP_GO : ACT_IMP_CANCEL);
    }
    if (failed > 0) {
        if (failed > 1) {
            char more[32];
            SDL_snprintf(more, sizeof(more), " (and %d more)", failed - 1);
            SDL_strlcat(bad, more, sizeof(bad));
        }
        SDL_strlcpy(l->status, bad, sizeof(l->status));
    }
    (void)added;
}

static void ApplySaves(Launcher *l) {
    char err[256];
    int changes;

    if (!Port_SaveImportApply(l->saves, l->numSaves, &l->opt, &l->plan, err, sizeof(err))) {
        SDL_snprintf(l->status, sizeof(l->status), "Not imported: %s", err);
        return;
    }
    changes = l->plan.changes;
    CloseSaves(l);
    SDL_snprintf(l->status, sizeof(l->status), "Imported: %d change%s, into %s.", changes, changes == 1 ? "" : "s",
                 l->plan.target);
}

static void PickSaves(Launcher *l) {
    static const SDL_DialogFileFilter filters[] = {
        { "Saves and memory cards (.gci .raw .sav .ps2 .psu)", "gci;raw;sav;ps2;psu;max;mcd;bin" },
        { "All files (the PC's save, velocityX, has no extension)", "*" },
    };

    SDL_SetAtomicInt(&l->picked, 0);
    l->pickSaves = 1;
    l->dialogOpen = 1;
    SDL_ShowOpenFileDialog(Picked, l, l->window, filters, SDL_arraysize(filters), NULL, true);
}

/* The index of the button that does `act`. */
static int ButtonFor(const Launcher *l, int act) {
    int i;

    for (i = 0; i < l->numButtons && l->buttons[i].act != act; i++) {
    }
    return i < l->numButtons ? i : 0;
}

/* Where the focus starts on a page: its first button that works, else Play,
 * else Quit. */
static int FirstFocus(const Launcher *l) {
    int i, play = ButtonFor(l, ACT_PLAY);

    for (i = 0; i < l->numFocus; i++) {
        if (l->buttons[i].enabled && l->buttons[i].act != ACT_PLAY && l->buttons[i].act != ACT_QUIT) {
            return i;
        }
    }
    return l->buttons[play].enabled ? play : ButtonFor(l, ACT_QUIT);
}

/* Shows page `tab` (wrapping round), remembered in sources.cfg. Not while
 * the saves preview is open. */
static void SwitchTab(Launcher *l, int tab) {
    if (l->savesView) {
        return;
    }
    l->tab = (tab + TAB_COUNT) % TAB_COUNT;
    if (l->src.tab != l->tab) {
        l->src.tab = l->tab;
        SaveSources(&l->src);
    }
    Layout(l);
    l->focus = FirstFocus(l);
}

/* Do button `b`; returns 1 to play, -1 to quit, 0 to stay. */
static int Activate(Launcher *l, const Button *b) {
    static const SDL_DialogFileFilter gcImages[] = { { "GameCube disc image (.iso, .gcm)", "iso;gcm;gcz" } };
    static const SDL_DialogFileFilter pcImages[] = { { "PC disc image (.iso, .cue, .bin)", "iso;cue;bin" } };
    static const SDL_DialogFileFilter ps2Images[] = { { "PS2 disc image (.iso)", "iso" } };

    if (!b->enabled) {
        return 0;
    }
    switch (b->act) {
    case ACT_PLAY_THIS:
        l->src.play = b->source;
        SaveSources(&l->src);
        SDL_snprintf(l->status, sizeof(l->status), "%s chosen.", s_names[b->source]);
        return 0;
    case ACT_FOLDER:
    case ACT_IMAGE:
        SDL_SetAtomicInt(&l->picked, 0);
        l->pickSource = b->source;
        l->pickImage = b->act == ACT_IMAGE;
        l->dialogOpen = 1;
        if (b->act == ACT_IMAGE) {
            SDL_ShowOpenFileDialog(Picked, l, l->window,
                                   b->source == PORT_DATA_GC   ? gcImages
                                   : b->source == PORT_DATA_PC ? pcImages
                                                               : ps2Images,
                                   1, NULL, false);
        } else {
            SDL_ShowOpenFolderDialog(Picked, l, l->window, NULL, false);
        }
        return 0;
    case ACT_REMOVE:
        SDL_free(l->src.path[b->source]);
        l->src.path[b->source] = NULL;
        if (l->src.play == b->source) {
            l->src.play = -1;
        }
        SaveSources(&l->src);
        SDL_snprintf(l->status, sizeof(l->status), "%s removed from the list (its files stay).", s_names[b->source]);
        return 0;
    case ACT_SKIP:
        l->src.skip = !l->src.skip;
        SaveSources(&l->src);
        if (l->src.skip) {
            SDL_snprintf(l->status, sizeof(l->status), "Next time the game starts straight away; --launcher shows this.");
        } else {
            SDL_snprintf(l->status, sizeof(l->status), "This window will show on every start.");
        }
        return 0;
    case ACT_SAVES:
    case ACT_IMP_ADD:
        PickSaves(l);
        return 0;
    case ACT_IMP_ONLY:
        l->only++;
        Replan(l);
        return 0;
    case ACT_IMP_CARS:
        l->opt.defaultCars = !l->opt.defaultCars;
        Replan(l);
        return 0;
    case ACT_IMP_GO:
        ApplySaves(l);
        return 0;
    case ACT_IMP_CANCEL:
        CloseSaves(l);
        SDL_snprintf(l->status, sizeof(l->status), "Nothing imported.");
        return 0;
    case ACT_TAB:
        SwitchTab(l, b->source);
        return 0;
    case ACT_SET: {
        int i = b->source, *v = SettingVar(i);

        *v = !*v;
        Port_GfxSaveConfig();
        SDL_snprintf(l->status, sizeof(l->status), "%s: %s. Saved in graphics.cfg.", s_settings[i].name,
                     *v ? s_settings[i].on : s_settings[i].off);
        return 0;
    }
    case ACT_PLAY:
        return 1;
    case ACT_QUIT:
        return -1;
    default:
        return 0;
    }
}

/* The nearest button from the focused one in direction (dx, dy). */
static void Move(Launcher *l, int dx, int dy) {
    const SDL_FRect *f = &l->buttons[l->focus].r;
    float fx = f->x + f->w / 2, fy = f->y + f->h / 2, best = 1e9f;
    int i, pick = l->focus;

    for (i = 0; i < l->numFocus; i++) {
        const SDL_FRect *r = &l->buttons[i].r;
        float x = r->x + r->w / 2 - fx, y = r->y + r->h / 2 - fy, d;

        if (i == l->focus || !l->buttons[i].enabled || (dx && x * dx <= 0) || (dy && y * dy <= 0)) {
            continue;
        }
        d = dx ? SDL_fabsf(x) + 4 * SDL_fabsf(y) : SDL_fabsf(y) + SDL_fabsf(x) / 4;
        if (d < best) {
            best = d;
            pick = i;
        }
    }
    l->focus = pick;
}

/* Answers that come from other threads: a dialog, a disc import. */
static void Poll(Launcher *l) {
    if (l->dialogOpen && SDL_GetAtomicInt(&l->picked) != 0) {
        l->dialogOpen = 0;
        if (SDL_GetAtomicInt(&l->picked) == 2) {
            SDL_snprintf(l->status, sizeof(l->status), "No file dialog on this system: use --data PATH.");
        } else if (l->pickSaves) {
            AddSaves(l, l->pickedMore, l->pickedCount);
        } else if (l->pickedPath != NULL) {
            if (l->pickImage && l->pickSource != PORT_DATA_GC) {
                StartDisc(l, l->pickedPath, l->pickSource);
            } else {
                Import(l, l->pickedPath, l->pickSource);
            }
            SDL_free(l->pickedPath);
            l->pickedPath = NULL;
        }
        SDL_free(l->pickedPath);
        l->pickedPath = NULL;
        while (l->pickedCount > 0) {
            SDL_free(l->pickedMore[--l->pickedCount]);
        }
        l->pickSaves = 0;
    }
    if (l->importer != NULL) {
        if (SDL_GetAtomicInt(&l->importDone)) {
            SDL_WaitThread(l->importer, NULL);
            l->importer = NULL;
            if (l->importOk) {
                Import(l, l->importDest, l->importSource);
            } else {
                SDL_snprintf(l->status, sizeof(l->status), "Disc not imported: %s", l->importError);
            }
        } else {
            SDL_snprintf(l->status, sizeof(l->status), "Importing the %s disc... %d files",
                         l->importSource == PORT_DATA_PC ? "PC" : "PS2", SDL_GetAtomicInt(&l->importFiles));
        }
    }
}

static int Run(Launcher *l) {
    SDL_Gamepad *pads[8] = { NULL };
    int result = 0, i;
    static int held[2];

    while (result == 0) {
        SDL_Event ev;

        Poll(l);
        Layout(l);
        Draw(l);
        if (SDL_getenv("HWVX_LAUNCHER_SHOT") != NULL) {
            result = -1;    /* testing: Draw saved the first frame */
            break;
        }
        if (!SDL_WaitEventTimeout(&ev, 50)) {
            continue;
        }
        do {
            Button *focused = &l->buttons[l->focus];

            SDL_ConvertEventToRenderCoordinates(l->renderer, &ev);
            switch (ev.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                result = -1;
                break;
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                /* the mouse highlights what it is over, and nothing when over
                 * nothing; the keyboard and pads take over from where it was */
                SDL_FPoint p = { ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.x : ev.button.x,
                                 ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.y : ev.button.y };

                l->mouse = 1;
                l->pointer = p;
                Layout(l);
                if (l->hover >= 0 && l->hover < l->numFocus) {
                    l->focus = l->hover;
                }
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT && l->hover >= 0) {
                    result = Activate(l, &l->buttons[l->hover]);
                }
                break;
            }
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                l->pointer.x = l->pointer.y = -1;
                break;
            case SDL_EVENT_KEY_DOWN:
                l->mouse = 0;
                switch (ev.key.key) {
                case SDLK_LEFT: Move(l, -1, 0); break;
                case SDLK_RIGHT: Move(l, 1, 0); break;
                case SDLK_UP: Move(l, 0, -1); break;
                case SDLK_DOWN: Move(l, 0, 1); break;
                case SDLK_TAB: l->focus = (l->focus + 1) % l->numFocus; break;
                case SDLK_PAGEUP: SwitchTab(l, l->tab - 1); break;
                case SDLK_PAGEDOWN: SwitchTab(l, l->tab + 1); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: result = Activate(l, focused); break;
                case SDLK_ESCAPE:
                    if (l->savesView) {
                        result = Activate(l, &l->buttons[ButtonFor(l, ACT_IMP_CANCEL)]);
                    } else {
                        result = -1;
                    }
                    break;
                default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                for (i = 0; i < 8; i++) {
                    if (pads[i] == NULL) {
                        pads[i] = SDL_OpenGamepad(ev.gdevice.which);
                        break;
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                l->mouse = 0;
                switch (ev.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: Move(l, -1, 0); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: Move(l, 1, 0); break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP: Move(l, 0, -1); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: Move(l, 0, 1); break;
                case SDL_GAMEPAD_BUTTON_SOUTH: result = Activate(l, focused); break;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: SwitchTab(l, l->tab - 1); break;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: SwitchTab(l, l->tab + 1); break;
                case SDL_GAMEPAD_BUTTON_EAST:
                    result = l->savesView ? Activate(l, &l->buttons[ButtonFor(l, ACT_IMP_CANCEL)]) : -1;
                    break;
                case SDL_GAMEPAD_BUTTON_START:
                    result = Activate(l, &l->buttons[ButtonFor(l, l->savesView ? ACT_IMP_GO : ACT_PLAY)]);
                    break;
                default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                /* the left stick as a d-pad: a step when it crosses half way */
                if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    int a = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
                    int dir = ev.gaxis.value > 16000 ? 1 : ev.gaxis.value < -16000 ? -1 : 0;

                    if (dir != 0 && held[a] != dir) {
                        l->mouse = 0;
                        Move(l, a ? 0 : dir, a ? dir : 0);
                    }
                    held[a] = dir;
                }
                break;
            default:
                break;
            }
            if (result != 0) {
                break;
            }
            Layout(l);
        } while (SDL_PollEvent(&ev));
        if (result < 0 && l->importer != NULL) {
            /* stop the import (it stops within a few sectors and leaves the
             * last good copy as it was) */
            SDL_SetAtomicInt(&l->importCancel, 1);
            SDL_snprintf(l->status, sizeof(l->status), "Stopping the disc import...");
            Draw(l);
            SDL_WaitThread(l->importer, NULL);
            l->importer = NULL;
        }
    }
    for (i = 0; i < 8; i++) {
        if (pads[i] != NULL) {
            SDL_CloseGamepad(pads[i]);
        }
    }
    return result;
}

/* ---- entry -------------------------------------------------------------
 *
 * The data to play: the chosen release's path (the caller's check opens it),
 * or NULL (the player quit, or no window may open). `open`: show the
 * launcher even when the chosen data is there. `search`: where to look on a
 * first run, before asking. */
char *Port_Launcher(int open, int window, Port_DataCheck check, Port_DataSearch search) {
    Launcher l;
    char *chosen = NULL, *legacy, *found = NULL;
    int i, valid, firstRun;

    SDL_zero(l);
    l.pointer.x = l.pointer.y = -1;
    l.check = check;
    firstRun = !LoadSources(&l.src);
    /* on the first run only (no sources.cfg yet), take over what the port
     * used before (datadir.txt), else look around; later, a removed source
     * stays removed */
    legacy = PrefFile("datadir.txt");
    for (i = 0; firstRun && i < 2 && (l.src.play < 0 || l.src.path[l.src.play] == NULL); i++) {
        char *dir = i == 0 ? (legacy != NULL ? SDL_LoadFile(legacy, NULL) : NULL) : (search ? search() : NULL);
        int src = SourceOf(check, dir, &found);

        if (src >= 0 && src < PORT_DATA_COUNT) {
            SDL_free(l.src.path[src]);
            l.src.path[src] = Absolute(found);
            l.src.play = src;
            SaveSources(&l.src);
        }
        SDL_free(found);
        found = NULL;
        SDL_free(dir);
    }
    SDL_free(legacy);

    valid = l.src.play >= 0 && l.src.path[l.src.play] != NULL;
    if (valid) {
        char *ok = check(l.src.path[l.src.play]);

        valid = ok != NULL;
        SDL_free(ok);
    }
    if (valid && !open && (l.src.skip || !window)) {
        chosen = SDL_strdup(l.src.path[l.src.play]);
        FreeSources(&l.src);
        return chosen;
    }
    if (!window) {
        FreeSources(&l.src);
        return NULL;
    }
    if (!valid && l.src.play >= 0) {
        SDL_snprintf(l.status, sizeof(l.status), "The %s data is no longer where it was: import it again.",
                     s_names[l.src.play]);
    } else if (!valid) {
        SDL_snprintf(l.status, sizeof(l.status), "Import your copy of the game to play.");
    }
    if (!SDL_CreateWindowAndRenderer("Hot Wheels: Velocity X", VIEW_W * 3 / 2, VIEW_H * 3 / 2, SDL_WINDOW_RESIZABLE,
                                     &l.window, &l.renderer)) {
        /* no launcher here: play what was chosen, or say why there is nothing */
        Port_Log("launcher: %s", SDL_GetError());
        if (valid) {
            chosen = SDL_strdup(l.src.path[l.src.play]);
        } else {
            Port_Fatal("The launcher could not open (%s), and no game data is chosen: pass it with --data PATH.",
                       SDL_GetError());
        }
        FreeSources(&l.src);
        return chosen;
    }
    SDL_SetRenderLogicalPresentation(l.renderer, VIEW_W, VIEW_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(l.renderer, 1);
    /* the page shown last, or Play; Games while there is nothing to play.
     * HWVX_LAUNCHER_TAB=NAME picks it (testing: screenshots of each page) */
    l.tab = AnyRelease(&l) ? (l.src.tab >= 0 ? l.src.tab : TAB_PLAY) : TAB_GAMES;
    if (TabNamed(SDL_getenv("HWVX_LAUNCHER_TAB")) >= 0) {
        l.tab = TabNamed(SDL_getenv("HWVX_LAUNCHER_TAB"));
    }
    Layout(&l);
    l.focus = valid ? ButtonFor(&l, ACT_PLAY) : FirstFocus(&l);     /* valid: A or Enter starts the game */
    if (SDL_getenv("HWVX_LAUNCHER_IMPORT") != NULL) {
        /* testing: the import preview of these files, as if picked */
        char *list = SDL_strdup(SDL_getenv("HWVX_LAUNCHER_IMPORT")), *paths[PORT_IMPORT_MAX_SOURCES], *save = NULL, *p;
        int n = 0;

        for (p = SDL_strtok_r(list, ";", &save); p != NULL && n < PORT_IMPORT_MAX_SOURCES; p = SDL_strtok_r(NULL, ";", &save)) {
            paths[n++] = p;
        }
        l.tab = TAB_SAVES;
        AddSaves(&l, paths, n);
        SDL_free(list);
    }
    if (Run(&l) > 0 && l.src.play >= 0 && l.src.path[l.src.play] != NULL) {
        chosen = SDL_strdup(l.src.path[l.src.play]);
    }
    CloseSaves(&l);
    SDL_DestroyRenderer(l.renderer);
    SDL_DestroyWindow(l.window);
    FreeSources(&l.src);
    return chosen;
}
