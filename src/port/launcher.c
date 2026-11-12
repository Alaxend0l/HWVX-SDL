/* The launcher: where the player imports the releases' game data, picks the
 * one to play, imports saves, sets what graphics.cfg holds beyond the game's
 * own Graphics menu, converts a release to .xdl ahead of time, and (later)
 * manages mods.
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
 *   not playable on its own; its content goes into the game played, by the
 *   import mode picked on its row (proto.c, game/Port_Proto.c: off, its
 *   three cars only, all its cars added, or its cars replacing the
 *   release's). Its disc image is imported like the PS2's; either disc goes
 *   where its version says, whichever row it was picked from.
 * - a mixed game (Port_Mix.h: the "ultimate" recipe, one game from the best
 *   of each release), offered with two or more releases imported: Play
 *   starts it as --mix ultimate.
 * Kept in <pref>/sources.cfg as "gc=PATH", "pc=PATH", "ps2=PATH",
 * "ps2beta=PATH", "proto=off|cars|added|replace", "play=gc|pc|ps2|mix:ultimate", "skip=1",
 * "interface=gc|ps2" (the mixed game's interface art, read by mix.c too) and
 * "tab=PAGE"; the GameCube data the
 * port found before the launcher existed (datadir.txt) is taken over on the
 * first run.
 *
 * It is plain SDL: a window with an SDL_Renderer drawing SDL's built-in 8x8
 * text at a logical 640x420. A tab bar shows one page at a time: Play (which
 * release plays, the skip tick), Games (importing and removing releases),
 * Saves, Convert (below), Mods (a placeholder), Settings (graphics.cfg,
 * through gfx_settings.c). Play, Quit and the status line stay at the bottom. It opens
 * on the page shown last, or Play; on Games while no release is imported.
 * Mouse, keyboard (arrows, Tab, Enter, Escape; Page Up/Down: the page) and
 * gamepads (d-pad or left stick, A, B, Start: Play; LB/RB: the page) move a
 * focus between the page's buttons and the bottom bar's; the tabs take clicks.
 * Folders and images are picked with the system's dialogs, which need a mouse
 * or a keyboard.
 *
 * Testing: HWVX_LAUNCHER_SHOT=file.bmp saves a frame and closes;
 * HWVX_LAUNCHER_TAB=PAGE opens that page; HWVX_LAUNCHER_INPUT queues input
 * before the shot (QueueInput); HWVX_LAUNCHER_DISC=pc=IMAGE|ps2=IMAGE imports
 * a disc image as if picked.
 *
 * "Import saves..." takes progress from any release's saves (Port_SaveImport.c:
 * GameCube .gci and card images, the PC's velocityX, PS2 card images and
 * .psu, the port's own .sav): the files are read, then a preview says which
 * saves go where and what each adds, with the choices (one save only, the
 * PC/PS2 default cars), and Import merges them into the JSON save store. The
 * preview keeps to the Saves page until it is imported or cancelled.
 * HWVX_LAUNCHER_IMPORT="FILE;FILE" opens that preview on start (testing).
 *
 * Convert: a release's packs converted to the .xdl cache (xdl.c) ahead of
 * time, instead of as the game loads them. "Convert to .xdl" starts this
 * program again as the converter (--convert-xdl, game/Port_Convert.c: the
 * game's own loader, in a process of its own, so the window stays live and a
 * pack that kills it doesn't kill the launcher) and follows the progress it
 * prints; Cancel stops it. It fills <pref>converted/<key>.part, which takes
 * <pref>converted/<key>'s place only when complete (as a disc import does),
 * so a cancelled or failed conversion leaves the last good one. Each release
 * shows its state: not converted, converted, or out of date (the converter's
 * manifest names the source files with their sizes and times, and the cache
 * version). "Delete cache" removes it. The game reads a release's converted
 * packs when it has them (Xdl_Use), unless the environment says otherwise
 * (HWVX_XDL, HWVX_XDL_WRITE, HWVX_XDL_DIR); a file that is out of date is
 * loaded from its source as before.
 *
 * The mixed game has a row of its own there: "Build" runs this program as
 * hwvx --mix-build ultimate (mix.c, game/Port_Mix.c), which prints the same
 * progress and fills <xdl>/mix-ultimate.part, put in mix-ultimate's place
 * when complete. Its state: not built (playing it converts its packs as
 * they load), built, or out of date (built.cfg's stamp, Port_MixStamp, is
 * of another recipe or other releases, or another cache version). Its
 * interface art is one set, chosen on the row: "Yellow (GameCube)" or
 * "Silver (PS2)" (sources.cfg "interface="; in the stamp, so a change
 * leaves the build out of date).
 *
 * A hook point for a feature built elsewhere, a button that calls a
 * function with a weak stub here (port_internal.h): the mod loader on Mods
 * (Port_LauncherMods). */
#include "port_internal.h"
#include "Port_Saves.h"
#include "xdl.h"
#include "../game/include/Port_Convert.h"
#include "../game/include/Port_Mods.h"

#define VIEW_W 640
#define VIEW_H 420
#define CHAR 8

/* The sources: the releases, then the prototype. */
enum { SRC_BETA = PORT_DATA_COUNT, SRC_COUNT };

static const char *const s_keys[SRC_COUNT] = { "gc", "pc", "ps2", "ps2beta" };
static const char *const s_names[SRC_COUNT] = { "GameCube", "PC", "PlayStation 2", "PS2 prototype (beta)" };

/* The mixed game: its recipe (built into the program), and its number where
 * a source's goes (the converter's convSource). */
#define MIX_RECIPE "ultimate"
#define SRC_MIX SRC_COUNT

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
    int mix;            /* the mixed game is chosen ("play=mix:ultimate"); play is then the
                         * release to fall back on */
    int skip;           /* start the game without the launcher */
    int tab;            /* TAB_* last shown, or -1 */
    int proto;          /* the prototype's import mode, PORT_PROTO_* (proto.c) */
    int ifacePS2;       /* the mixed game's interface art: the PS2's silver set, else the GameCube's yellow */
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
        if (SDL_strncmp(line, "proto=", 6) == 0 && Port_ProtoModeNamed(line + 6) >= 0) {
            s->proto = Port_ProtoModeNamed(line + 6);
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
        if (SDL_strcmp(line, "play=mix:" MIX_RECIPE) == 0) {
            s->mix = 1;
        }
        if (SDL_strncmp(line, "interface=", 10) == 0) {
            s->ifacePS2 = SDL_strcasecmp(line + 10, "ps2") == 0;
        }
    }
    SDL_free(text);
    for (i = 0; s->mix && s->play < 0 && i < PORT_DATA_COUNT; i++) {
        if (s->path[i] != NULL) {
            s->play = i;    /* when the mixed game can't be played */
        }
    }
    return 1;
}

/* Written to sources.cfg.tmp, then renamed over it: never left half written. */
static void SaveSources(const Sources *s) {
    char *path = PrefFile("sources.cfg"), *tmp = NULL;
    SDL_IOStream *io = path != NULL && SDL_asprintf(&tmp, "%s.tmp", path) >= 0 ? SDL_IOFromFile(tmp, "w") : NULL;
    int i, ok;

    if (io == NULL) {
        Port_Log("sources.cfg: %s", SDL_GetError());
        SDL_free(path);
        SDL_free(tmp);
        return;
    }
    ok = 1;
    for (i = 0; i < SRC_COUNT; i++) {
        if (s->path[i] != NULL) {
            ok &= SDL_IOprintf(io, "%s=%s\n", s_keys[i], s->path[i]) > 0;
        }
    }
    if (s->mix) {
        ok &= SDL_IOprintf(io, "play=mix:%s\n", MIX_RECIPE) > 0;
    } else if (s->play >= 0) {
        ok &= SDL_IOprintf(io, "play=%s\n", s_keys[s->play]) > 0;
    }
    if (s->proto != PORT_PROTO_OFF) {
        ok &= SDL_IOprintf(io, "proto=%s\n", Port_ProtoModeKey(s->proto)) > 0;
    }
    if (s->skip) {
        ok &= SDL_IOprintf(io, "skip=1\n") > 0;
    }
    if (s->ifacePS2) {
        ok &= SDL_IOprintf(io, "interface=ps2\n") > 0;
    }
    if (s->tab >= 0) {
        ok &= SDL_IOprintf(io, "tab=%s\n", s_tabKeys[s->tab]) > 0;
    }
    if (!SDL_CloseIO(io) || !ok || !Port_ReplaceFile(tmp, path)) {
        Port_Log("sources.cfg: %s", SDL_GetError());
        SDL_RemovePath(tmp);
    }
    SDL_free(path);
    SDL_free(tmp);
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
    ACT_SET,            /* toggle setting `source` */
    ACT_CONV_DELETE,    /* delete release `source`'s converted packs (ACT_CONVERT converts them) */
    ACT_CONV_CANCEL,
    ACT_HOOK_MODS,      /* the hook point: the mod loader */
    ACT_PLAY_MIX,       /* play the mixed game */
    ACT_MIX_BUILD,      /* build it (ACT_CONV_CANCEL stops it) */
    ACT_MIX_DELETE,     /* delete its cache */
    ACT_MIX_IFACE,      /* its interface art: yellow (GameCube) or silver (PS2) */
    ACT_MOD_TOGGLE,     /* mods (Port_Mods.h): turn mod `source` on or off */
    ACT_MOD_UP,         /* load it one earlier */
    ACT_MOD_REMOVE,     /* delete its folder (pressed twice) */
    ACT_MOD_INSTALL,    /* pick a .xmod to install */
    ACT_PROTO_MODE      /* the prototype's import mode `source` (PORT_PROTO_*) */
};

/* A release's converted packs. */
enum { CONV_NONE, CONV_OK, CONV_STALE, CONV_IMAGE /* a disc image: nothing to convert from */ };

/* The Settings page: graphics.cfg's choices that have no row in the game's
 * own Graphics menu, and the display (gfx_settings.c reads and writes the
 * file; these are its g_port fields, 1 being `on`). */
enum { SET_DISPLAY, SET_PCTEX, SET_MAINMENU, SET_SPLITLOD, SET_MOVIES, SET_MOUSESTEER, SET_DRAWDIST, SET_COUNT };

/* `third`: a third choice (value 2) the row cycles through, NULL for on/off. */
static const struct {
    const char *name, *off, *on, *about, *env, *third;
} s_settings[SET_COUNT] = {
    { "Display", "Windowed", "Fullscreen", "How the game's window opens.", NULL },
    { "PC textures", "GameCube colours", "PC colours",
      "The PC release's textures: in the GameCube's colours, or the PC's own.", "HWVX_PC_TEXTURES" },
    { "Main menu", "With the port's rows", "Original",
      "The port adds Quit Game and Multi-Player; original is the release's.", "HWVX_ORIGINAL_MAINMENU" },
    { "Split screen", "Full detail", "GameCube's detail",
      "Full detail like the PC, or a level coarser as the GameCube drew it.", "HWVX_SPLITSCREEN_LOD" },
    { "Movies", "On", "Off", "The intro and ending movies; off skips them, like the PC's option.", "HWVX_NO_MOVIE" },
    { "Mouse steering", "Off", "On",
      "In races, like the PC: moving steers; left button A (gas), right X (brake).", "HWVX_MOUSESTEER" },
    { "Draw distance", "Normal", "Far",
      "More of the world drawn, the fog further out; the game plays the same.", "HWVX_DRAWDIST", "Everything" },
};

/* A row's button label: its choice. */
static const char *SettingLabel(int i, int v) {
    return v == 2 && s_settings[i].third != NULL ? s_settings[i].third : v ? s_settings[i].on : s_settings[i].off;
}

static int *SettingVar(int i) {
    switch (i) {
    case SET_DISPLAY: return &g_port.fullscreen;
    case SET_PCTEX: return &g_port.pcTexturesPC;
    case SET_MAINMENU: return &g_port.originalMainMenu;
    case SET_SPLITLOD: return &g_port.splitLodGC;
    case SET_MOUSESTEER: return &g_port.mouseSteer;
    case SET_DRAWDIST: return &g_port.drawDist;
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
    int scripted;               /* testing: HWVX_LAUNCHER_INPUT's events still to come */
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
    int pickedCount, pickSaves, pickMod;
    int modsScanned, modRemove;     /* the Mods page: the folder read; mod armed to remove (-1) */
    int savesView;              /* the preview is showing */
    PortImportSource saves[PORT_IMPORT_MAX_SOURCES];
    int numSaves, only;         /* only: 0 every save, k the k-th (file, slot) */
    PortImportOptions opt;
    PortImportPlan plan;
    int planOk;
    char planError[256], onlyLabel[96], carsLabel[64];
    /* Convert: each release's converted packs, and the converter while it runs */
    int convState[PORT_DATA_COUNT], convPacks[PORT_DATA_COUNT];
    Uint64 convBytes[PORT_DATA_COUNT];
    SDL_Process *converter;
    int convSource, convDone, convTotal;
    SDL_Time convStart;         /* when it started (the mixed game's: was its build made since?) */
    char convName[64], convLine[600], convError[200];
    size_t convLineLen;
    /* the mixed game: its recipe looked at (Port_MixProbe), its cache's state */
    PortMixInfo mix;
    int mixOk, mixState, mixPacks, mixHave;
    Uint64 mixBytes;
    char mixError[160];
} Launcher;

/* The mixed game's interface set, as its row's button says it. */
static const char *MixIfaceLabel(const Launcher *l) {
    return l->src.ifacePS2 ? "Menus: Silver (PS2)" : "Menus: Yellow (GameCube)";
}

/* The mixed game can be played: its recipe has two or more releases imported. */
static int MixPlayable(const Launcher *l) {
    return l->mixOk && l->mix.releases >= 2;
}

/* What Play starts: the mixed game, else the chosen release (-1: nothing). */
static int PlayingMix(const Launcher *l) {
    return l->src.mix && MixPlayable(l);
}

static int CanPlay(const Launcher *l) {
    return PlayingMix(l) || (l->src.play >= 0 && l->src.path[l->src.play] != NULL);
}

/* The title and the tabs share the top line; the pages start under it. */
#define TAB_X 216.0f
#define TAB_Y 8.0f
#define PAGE_Y 42.0f
#define ROW_Y(i) (PAGE_Y + (i) * 58.0f)                 /* Games: a release */
#define PLAY_ROW_Y(k) (PAGE_Y + 24.0f + (k) * 34.0f)    /* Play: an imported release */
#define SKIP_Y(rows) (PLAY_ROW_Y((rows) > 2 ? (rows) : 2) + 12.0f)  /* under the releases */
#define SET_ROW_Y(i) (PAGE_Y + 22.0f + (i) * 38.0f)     /* Settings: a setting */
#define CONV_ROW_Y(k) (PAGE_Y + 30.0f + (k) * 52.0f)    /* Convert: an imported release */
#define PROTO_Y (ROW_Y(SRC_COUNT) - 2.0f)                /* Games: the prototype's import mode */
#define MODS_SHOWN 6
#define MOD_ROW_Y(k) (PAGE_Y + 40.0f + (k) * 34.0f)     /* Mods: a mod, in load order */
#define CONV_BAR_Y CONV_ROW_Y(3)                        /* the progress, under the releases */
#define CONV_HOOK_Y (CONV_BAR_Y + 46.0f)                /* the mixed game's row */
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
    Add(l, 24 + 17 * CHAR + 48, VIEW_H - 30, "Cancel", ACT_IMP_CANCEL, -1, !busy);  /* busy: Add file's answer comes */
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
                Add(l, 24, PLAY_ROW_Y(k++), !PlayingMix(l) && l->src.play == i ? "Playing this" : "Play this",
                    ACT_PLAY_THIS, i, !busy);
            }
        }
        if (MixPlayable(l)) {
            Add(l, 24, PLAY_ROW_Y(k++), PlayingMix(l) ? "Playing this" : "Play this", ACT_PLAY_MIX, -1, !busy);
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
        if (l->src.path[SRC_BETA] != NULL) {
            /* the import modes (labels kept: buttons keep the pointer) */
            static const char *const labels[2][PORT_PROTO_MODES] = {
                { "( ) Off", "( ) Beta cars", "( ) Beta added", "( ) Beta replaces" },
                { "(*) Off", "(*) Beta cars", "(*) Beta added", "(*) Beta replaces" },
            };
            float x = 24;

            for (i = 0; i < PORT_PROTO_MODES; i++) {
                x += Add(l, x, PROTO_Y, labels[l->src.proto == i][i], ACT_PROTO_MODE, i, !busy)->r.w + 6;
            }
        }
        break;
    case TAB_SAVES:
        Add(l, 24, PAGE_Y + 24, "Import saves...", ACT_SAVES, -1, !busy);
        break;
    case TAB_CONVERT:
        for (i = 0, k = 0; i < PORT_DATA_COUNT; i++) {
            if (l->src.path[i] != NULL) {
                float x = 24, y = CONV_ROW_Y(k++) + 26;

                x += Add(l, x, y, l->convState[i] == CONV_NONE || l->convState[i] == CONV_IMAGE ? "Convert to .xdl"
                                                                                               : "Convert again",
                         ACT_CONVERT, i, !busy && l->convState[i] != CONV_IMAGE)->r.w + 8;
                if (l->convState[i] == CONV_OK || l->convState[i] == CONV_STALE) {
                    Add(l, x, y, "Delete cache", ACT_CONV_DELETE, i, !busy);
                }
            }
        }
        if (k == 0) {
            Add(l, 24, CONV_ROW_Y(0) + 26, "Import a copy on the Games page", ACT_TAB, TAB_GAMES, 1);
        }
        if (l->converter != NULL) {
            Add(l, VIEW_W - 24 - (6 * CHAR + 16), CONV_BAR_Y + 26, "Cancel", ACT_CONV_CANCEL, -1, 1);
        }
        /* the mixed game */
        {
            float x = 24;

            x += Add(l, x, CONV_HOOK_Y + 26, l->mixState == CONV_NONE ? "Build" : "Build again", ACT_MIX_BUILD, -1,
                     !busy && MixPlayable(l))->r.w + 8;
            if (l->mixHave) {
                Add(l, x, CONV_HOOK_Y + 26, "Delete", ACT_MIX_DELETE, -1, !busy);
            }
            Add(l, VIEW_W - 24 - (float)(SDL_strlen(MixIfaceLabel(l)) * CHAR + 16), CONV_HOOK_Y + 26,
                MixIfaceLabel(l), ACT_MIX_IFACE, -1, !busy);
        }
        break;
    case TAB_MODS:
        if (!l->modsScanned) {
            l->modsScanned = 1;
            l->modRemove = -1;
            Port_ModsScan();
        }
        for (i = 0; i < Port_ModsCount() && i < MODS_SHOWN; i++) {
            const PortModInfo *m = Port_ModsInfo(i);
            float x = VIEW_W - 24 - (8 * CHAR + 16) - 8 - (2 * CHAR + 16) - 8 - (3 * CHAR + 16);

            x += Add(l, x, MOD_ROW_Y(i), m->enabled ? "[x]" : "[ ]", ACT_MOD_TOGGLE, i, !busy)->r.w + 8;
            x += Add(l, x, MOD_ROW_Y(i), "Up", ACT_MOD_UP, i, !busy && i > 0)->r.w + 8;
            Add(l, x, MOD_ROW_Y(i), l->modRemove == i ? "Sure?" : "Remove", ACT_MOD_REMOVE, i, !busy);
        }
        Add(l, 24, MOD_ROW_Y(MODS_SHOWN) + 4, "Install a mod...", ACT_MOD_INSTALL, -1, !busy);
        break;
    case TAB_SETTINGS:
        for (i = 0; i < SET_COUNT; i++) {
            Add(l, SET_X, SET_ROW_Y(i), SettingLabel(i, *SettingVar(i)), ACT_SET, i, 1);
        }
        break;
    default:
        break;
    }
}

/* The buttons for the current state, in focus order, then the tabs. */
static void Layout(Launcher *l) {
    int i, busy = l->importer != NULL || l->converter != NULL || l->dialogOpen;
    float x = TAB_X;

    l->numButtons = 0;
    if (l->savesView) {
        LayoutSaves(l);
    } else {
        LayoutPage(l, busy);
        Add(l, 24, VIEW_H - 30, "Play", ACT_PLAY, -1, !busy && CanPlay(l));
        Add(l, 24 + 4 * CHAR + 24, VIEW_H - 30, "Quit", ACT_QUIT, -1, !l->dialogOpen);
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
    char line[160], fit[160];
    int i, k;
    float y;

    switch (l->tab) {
    case TAB_PLAY:
        Text(l, 24, PAGE_Y, GREY, "Your copies of the game. Pick the one to play.");
        for (i = 0, k = 0; i < PORT_DATA_COUNT; i++) {
            if (l->src.path[i] != NULL) {
                y = PLAY_ROW_Y(k++);
                Text(l, 148, y + 1, WHITE, s_names[i]);
                if (l->convState[i] == CONV_OK || l->convState[i] == CONV_STALE) {
                    Text(l, 148 + (float)(SDL_strlen(s_names[i]) + 2) * CHAR, y + 1, DIM, "- loads from its .xdl packs");
                }
                Fit(line, sizeof(line), l->src.path[i], 58);
                Text(l, 148, y + 12, GREEN, line);
            }
        }
        if (MixPlayable(l)) {
            y = PLAY_ROW_Y(k++);
            Text(l, 148, y + 1, WHITE, "Mixed game (" MIX_RECIPE ")");
            Text(l, 148, y + 12, l->mixState == CONV_OK ? GREEN : DIM,
                 l->mixState == CONV_OK      ? "The best of each copy; built (Convert page)."
                 : l->mixState == CONV_STALE ? "The best of each copy; its build is out of date."
                                             : "The best of each copy; packs mix as they first load.");
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
                Text(l, 24 + 21 * CHAR, y, DIM, "- its content in the game you play");
            }
            if (l->src.path[i] != NULL) {
                Fit(line, sizeof(line), l->src.path[i], 74);
                Text(l, 24, y + 13, GREEN, line);
            } else {
                Text(l, 24, y + 13, DIM, "not imported");
            }
        }
        y = ROW_Y(SRC_COUNT) + 4;
        if (l->src.path[SRC_BETA] != NULL) {
            /* what the mode does: replaced, added, left as retail (Port_Proto.c) */
            static const char *const what[PORT_PROTO_MODES][2] = {
                { "Nothing of the prototype is used.", "Its cars keep their numbers in ids.lock for saves." },
                { "Adds its 3 cars of its own: Fandango, Sweet 16 II, Jester.",
                  "The release's 33 cars, worlds and audio stay as they are." },
                { "Adds its 36 cars: the 3, and the 33 others as \"... (prototype)\".",
                  "The release's own stay. Its worlds and audio: not yet." },
                { "The release's 33 cars become the prototype's versions; its 3 are added.",
                  "Saves keep their cars. Worlds and audio stay the release's (not yet)." },
            };
            int m = l->src.proto >= 0 && l->src.proto < PORT_PROTO_MODES ? l->src.proto : 0;

            Text(l, 24, y + 22, GREY, what[m][0]);
            Text(l, 24, y + 33, DIM, what[m][1]);
            y += 46;
        }
        if (l->importer != NULL) {
            SDL_snprintf(line, sizeof(line), "Importing the %s disc image into the save folder.",
                         l->importSource == PORT_DATA_PC ? "PC" : "PS2");
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
        Text(l, 24, PAGE_Y, GREY, "A copy's packs converted to .xdl once, here, instead of every time");
        Text(l, 24, PAGE_Y + 11, GREY, "the game loads them. The game then loads the converted packs.");
        for (i = 0, k = 0; i < PORT_DATA_COUNT; i++) {
            if (l->src.path[i] == NULL) {
                continue;
            }
            y = CONV_ROW_Y(k++);
            Text(l, 24, y, WHITE, s_names[i]);
            switch (l->converter != NULL && l->convSource == i ? -1 : l->convState[i]) {
            case -1:
                Text(l, 24, y + 12, 255, 210, 80, "Converting...");
                break;
            case CONV_OK:
                SDL_snprintf(line, sizeof(line), "Converted: %d packs, %d MB", l->convPacks[i],
                             (int)((l->convBytes[i] + 500000) / 1000000));
                Text(l, 24, y + 12, GREEN, line);
                break;
            case CONV_STALE:
                SDL_snprintf(line, sizeof(line), "Out of date (%d MB): the copy or the port changed. Convert again.",
                             (int)((l->convBytes[i] + 500000) / 1000000));
                Text(l, 24, y + 12, 255, 190, 120, line);
                break;
            case CONV_IMAGE:
                Text(l, 24, y + 12, DIM, "A disc image: only a folder of the disc's files is converted.");
                break;
            default:
                Text(l, 24, y + 12, DIM, "Not converted");
                break;
            }
        }
        if (k == 0) {
            Text(l, 24, CONV_ROW_Y(0) + 4, DIM, "No release imported yet.");
        }
        y = CONV_BAR_Y;
        if (l->converter != NULL) {
            SDL_FRect bar = { 24, y + 14, VIEW_W - 48, 8 };
            int pct = l->convTotal > 0 ? l->convDone * 100 / l->convTotal : 0;

            SDL_snprintf(line, sizeof(line), "%s%s%s: %d%%  %d of %d packs  %s",
                         l->convSource == SRC_MIX ? "Building the mixed game" : "Converting the ",
                         l->convSource == SRC_MIX ? "" : s_names[l->convSource],
                         l->convSource == SRC_MIX ? "" : " copy", pct, l->convDone, l->convTotal, l->convName);
            Fit(fit, sizeof(fit), line, 74);
            Text(l, 24, y, 255, 210, 80, fit);
            SDL_SetRenderDrawColor(l->renderer, 38, 44, 64, 255);
            SDL_RenderFillRect(l->renderer, &bar);
            bar.w = bar.w * (float)pct / 100.0f;
            SDL_SetRenderDrawColor(l->renderer, 255, 210, 80, 255);
            SDL_RenderFillRect(l->renderer, &bar);
            Text(l, 24, y + 31, DIM, "Cancel or Quit stops it and keeps the last complete one.");
        }
        y = CONV_HOOK_Y;
        Text(l, 24, y, WHITE, "Mixed game (" MIX_RECIPE ")");
        Text(l, 24 + 23 * CHAR, y, DIM, "- one game from the best of your copies");
        if (l->converter != NULL && l->convSource == SRC_MIX) {
            Text(l, 24, y + 12, 255, 210, 80, "Building...");
        } else if (!l->mixOk) {
            Fit(fit, sizeof(fit), l->mixError, 74);
            Text(l, 24, y + 12, DIM, fit);
        } else if (!MixPlayable(l)) {
            Text(l, 24, y + 12, DIM, "Needs two or more of the GameCube, PC and PS2 copies (as folders).");
        } else if (l->mixState == CONV_OK) {
            SDL_snprintf(line, sizeof(line), "Built: %d packs, %d MB", l->mixPacks, (int)((l->mixBytes + 500000) / 1000000));
            Text(l, 24, y + 12, GREEN, line);
        } else if (l->mixState == CONV_STALE) {
            SDL_snprintf(line, sizeof(line), "Out of date (%d MB): the copies, the recipe or the port changed.",
                         (int)((l->mixBytes + 500000) / 1000000));
            Text(l, 24, y + 12, 255, 190, 120, line);
        } else {
            Text(l, 24, y + 12, DIM, "Not built: playing it mixes each pack as it first loads.");
        }
        break;
    case TAB_MODS:
        Wrap(l, 24, PAGE_Y, PAGE_Y + 30, 74, 0, GREY,
             "Mods change and add to the game. They carry no game data: each one applies to "
             "your own copy as the game loads it. Top loads first; changes apply next start.");
        if (Port_ModsCount() == 0) {
            Text(l, 24, MOD_ROW_Y(0) + 5, DIM, "No mods yet: install a .xmod package.");
        }
        for (i = 0; i < Port_ModsCount() && i < MODS_SHOWN; i++) {
            const PortModInfo *m = Port_ModsInfo(i);

            SDL_snprintf(line, sizeof(line), "%.36s %.10s", m->name, m->version);
            Text(l, 24, MOD_ROW_Y(i) + 1, m->enabled ? WHITE : DIM, line);
            SDL_snprintf(line, sizeof(line), "%.24s%s%s", *m->author ? m->author : m->id,
                         m->checked ? "" : "  - not checked by the packer", m->problems ? "  - see below" : "");
            Text(l, 24, MOD_ROW_Y(i) + 13, DIM, line);
        }
        if (Port_ModsCount() > MODS_SHOWN) {
            SDL_snprintf(line, sizeof(line), "and %d more (mods.jsonc in the mods folder)", Port_ModsCount() - MODS_SHOWN);
            Text(l, 24 + 16 * CHAR + 16 + 12, MOD_ROW_Y(MODS_SHOWN) + 9, DIM, line);
        }
        for (i = 0; i < Port_ModsReportCount() && i < 3; i++) {
            SDL_snprintf(line, sizeof(line), "%.76s", Port_ModsReportLine(i));
            Text(l, 24, MOD_ROW_Y(MODS_SHOWN) + 32 + i * 12.0f, 255, 190, 120, line);
        }
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
        if ((b->act == ACT_PLAY_THIS && !PlayingMix(l) && l->src.play == b->source) ||
            (b->act == ACT_PLAY_MIX && PlayingMix(l))) {
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
    if (SDL_getenv("HWVX_LAUNCHER_SHOT") != NULL && !l->scripted) {
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

static void RefreshConverted(Launcher *l);

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
    RefreshConverted(l);
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

/* ---- converting to .xdl --------------------------------------------------- */

/* The hook point (port_internal.h): the stub of a feature built elsewhere. */
__attribute__((weak)) int Port_LauncherMods(int run, const char *const *paths, const char *prefDir, char *status,
                                            size_t statusSize) {
    (void)run, (void)paths, (void)prefDir, (void)status, (void)statusSize;
    return 0;   /* not built in */
}

static void RunHook(Launcher *l, int (*hook)(int, const char *const *, const char *, char *, size_t)) {
    char *pref = Port_PrefPath();

    hook(1, (const char *const *)l->src.path, pref != NULL ? pref : "", l->status, sizeof(l->status));
    SDL_free(pref);
}

char *Port_ConvertedDir(const char *key) {
    char *pref = Port_PrefPath(), *dir = NULL;

    if (pref != NULL) {
        SDL_asprintf(&dir, "%sconverted/%s", pref, key);
        SDL_free(pref);
    }
    return dir;
}

static SDL_EnumerationResult SDLCALL SumEntry(void *userdata, const char *dir, const char *name) {
    char *path = NULL;
    SDL_PathInfo info;

    if (SDL_asprintf(&path, "%s%s", dir, name) >= 0) {
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            SDL_EnumerateDirectory(path, SumEntry, userdata);
        } else {
            *(Uint64 *)userdata += info.size;
        }
        SDL_free(path);
    }
    return SDL_ENUM_CONTINUE;
}

/* The state of the converted packs in `dir`, made from `source`: CONV_NONE
 * without a finished conversion's manifest; CONV_STALE when the manifest is
 * of another cache version or settings, another place, or names a source
 * file that has changed or gone. */
static int ConvertedState(const char *dir, const char *source, int *packs) {
    char *path = NULL, *text, *line, *save = NULL;
    u32 version, compiler, settings, v, c, s;
    int state = CONV_OK, stamped = 0;

    *packs = 0;
    SDL_asprintf(&path, "%s/%s", dir, PORT_CONVERT_MANIFEST);
    text = path != NULL ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (text == NULL) {
        return CONV_NONE;
    }
    Xdl_Stamp(&version, &compiler, &settings);
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strncmp(line, "xdl=", 4) == 0) {
            stamped = SDL_sscanf(line + 4, "%u.%u.%u", &v, &c, &s) == 3 && v == version && c == compiler && s == settings;
        } else if (SDL_strncmp(line, "source=", 7) == 0) {
            if (SDL_strcmp(line + 7, source) != 0) {
                state = CONV_STALE;
            }
        } else if (SDL_strncmp(line, "packs=", 6) == 0) {
            *packs = SDL_atoi(line + 6);
        } else if (SDL_strncmp(line, "file=", 5) == 0) {
            char *end, *file;
            Uint64 size = SDL_strtoull(line + 5, &end, 10);
            Sint64 time = *end == '\t' ? SDL_strtoll(end + 1, &end, 10) : 0;
            SDL_PathInfo info;

            file = *end == '\t' ? end + 1 : end;
            if (!SDL_GetPathInfo(file, &info) || info.size != size || (Sint64)info.modify_time != time) {
                state = CONV_STALE;
            }
        }
    }
    SDL_free(text);
    return stamped ? state : CONV_STALE;
}

/* The mixed game's recipe looked at again (which releases it has), and the
 * state of its cache: built.cfg (PORT_MIX_BUILT) names the stamp it was
 * built for and the cache version. */
static void RefreshMix(Launcher *l) {
    char *path = NULL, *text, *line, *save = NULL;
    u32 version, compiler, settings, v, c, st, stamp = 0;
    int stamped = 0;
    SDL_PathInfo info;

    l->mixState = CONV_NONE;
    l->mixPacks = l->mixHave = 0;
    l->mixBytes = 0;
    l->mixOk = Port_MixProbe(MIX_RECIPE, l->check, &l->mix, l->mixError, sizeof(l->mixError));
    if (!l->mixOk || l->mix.dir[0] == '\0' || !SDL_GetPathInfo(l->mix.dir, &info) ||
        info.type != SDL_PATHTYPE_DIRECTORY) {
        return;
    }
    l->mixHave = 1;
    SDL_EnumerateDirectory(l->mix.dir, SumEntry, &l->mixBytes);
    SDL_asprintf(&path, "%s/%s", l->mix.dir, PORT_MIX_BUILT);
    text = path != NULL ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (text == NULL) {
        return;     /* filled as it was played, never built */
    }
    Xdl_Stamp(&version, &compiler, &settings);
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strncmp(line, "xdl=", 4) == 0) {
            stamped = SDL_sscanf(line + 4, "%u.%u.%u", &v, &c, &st) == 3 && v == version && c == compiler &&
                      st == settings;
        } else if (SDL_strncmp(line, "stamp=", 6) == 0) {
            stamp = (u32)SDL_strtoul(line + 6, NULL, 16);
        } else if (SDL_strncmp(line, "packs=", 6) == 0) {
            l->mixPacks = SDL_atoi(line + 6);
        }
    }
    SDL_free(text);
    l->mixState = stamped && stamp == l->mix.stamp ? CONV_OK : CONV_STALE;
}

/* Every release's state again: on start, and after anything that changes it. */
static void RefreshConverted(Launcher *l) {
    int i;

    for (i = 0; i < PORT_DATA_COUNT; i++) {
        char *dir = Port_ConvertedDir(s_keys[i]);
        SDL_PathInfo info;

        l->convState[i] = CONV_NONE;
        l->convBytes[i] = 0;
        l->convPacks[i] = 0;
        if (l->src.path[i] != NULL && dir != NULL) {
            l->convState[i] = ConvertedState(dir, l->src.path[i], &l->convPacks[i]);
            if (l->convState[i] != CONV_NONE) {
                SDL_EnumerateDirectory(dir, SumEntry, &l->convBytes[i]);
            } else if (SDL_GetPathInfo(l->src.path[i], &info) && info.type == SDL_PATHTYPE_FILE) {
                l->convState[i] = CONV_IMAGE;
            }
        }
        SDL_free(dir);
    }
    RefreshMix(l);
}

/* The game reads release `src`'s converted packs, if it has any and the
 * environment doesn't set the cache up itself. */
static void UseConverted(const Sources *s) {
    char *dir = s->play >= 0 ? Port_ConvertedDir(s_keys[s->play]) : NULL;
    int packs;

    if (dir != NULL && !Xdl_EnvSet() && ConvertedState(dir, s->path[s->play], &packs) != CONV_NONE) {
        Port_Log("launcher: the %s copy loads from its converted packs, %s", s_names[s->play], dir);
        Xdl_Use(dir, 1, 0);
    }
    SDL_free(dir);
}

/* Starts the converter on release `src`, or the mixed game's builder
 * (SRC_MIX): this program, as a process. The builder clears and fills its
 * own .part folder (mix.c). */
static void StartConvert(Launcher *l, int src) {
    char *dir = src == SRC_MIX ? NULL : Port_ConvertedDir(s_keys[src]), *part = NULL;
    char *parent = src == SRC_MIX ? NULL : Port_ConvertedDir("");
    const char *args[6];
    SDL_Environment *env;
    SDL_PropertiesID props;

    if (src == SRC_MIX) {
        args[0] = g_port.program;
        args[1] = "--mix-build";
        args[2] = MIX_RECIPE;
        args[3] = NULL;
        if (g_port.program == NULL || !MixPlayable(l)) {
            SDL_snprintf(l->status, sizeof(l->status), "Can't build the mixed game.");
            return;
        }
    } else if (dir == NULL || parent == NULL || g_port.program == NULL || SDL_asprintf(&part, "%s.part", dir) < 0) {
        SDL_snprintf(l->status, sizeof(l->status), "Can't convert: no folder to convert into.");
        SDL_free(dir);
        SDL_free(parent);
        return;
    } else {
        Port_RemoveTree(part);
        SDL_CreateDirectory(parent);
        args[0] = g_port.program;
        args[1] = "--convert-xdl";
        args[2] = part;
        args[3] = "--data";
        args[4] = l->src.path[src];
        args[5] = NULL;
    }
    env = SDL_CreateEnvironment(true);
    SDL_SetEnvironmentVariable(env, "HWVX_NO_DIALOG", "1", true);
    SDL_UnsetEnvironmentVariable(env, "HWVX_DUMP");
    SDL_UnsetEnvironmentVariable(env, "HWVX_START");
    SDL_UnsetEnvironmentVariable(env, "HWVX_MIX");          /* only what the arguments say */
    SDL_UnsetEnvironmentVariable(env, "HWVX_MIX_BUILD");
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    l->converter = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    l->convSource = src;
    l->convDone = l->convTotal = 0;
    l->convName[0] = l->convError[0] = '\0';
    l->convLineLen = 0;
    SDL_GetCurrentTime(&l->convStart);
    if (l->converter == NULL) {
        SDL_snprintf(l->status, sizeof(l->status), "Could not start the converter: %s", SDL_GetError());
    } else if (src == SRC_MIX) {
        SDL_snprintf(l->status, sizeof(l->status), "Building the mixed game...");
    } else {
        SDL_snprintf(l->status, sizeof(l->status), "Converting the %s copy...", s_names[src]);
    }
    SDL_free(part);
    SDL_free(dir);
    SDL_free(parent);
}

/* A line the converter printed (Port_Convert.h). */
static void ConvertLine(Launcher *l, const char *line) {
    int done, total, at = 0;

    if (SDL_sscanf(line, "@convert %d %d %n", &done, &total, &at) >= 2 && at > 0) {
        l->convDone = done;
        l->convTotal = total;
        SDL_strlcpy(l->convName, BaseName(line + at), sizeof(l->convName));
    } else if (SDL_strncmp(line, "@error ", 7) == 0) {
        SDL_strlcpy(l->convError, line + 7, sizeof(l->convError));
    }
}

/* The mixed game's builder has ended: it puts a complete build in place
 * itself (mix.c: <dir> to <dir>.old, <dir>.part to <dir>, .old removed), so
 * a stop, a Cancel too, can come at any point of that. What happened is
 * read from the folders: a stop between the renames gets the last build
 * back; a built.cfg written since the start is the new build. */
static void EndMixBuild(Launcher *l, int cancel, int code) {
    char *part = NULL, *old = NULL, *built = NULL;
    SDL_PathInfo info;
    int ok = 0;

    RefreshMix(l);
    if (l->mix.dir[0] != '\0' && SDL_asprintf(&part, "%s.part", l->mix.dir) >= 0 &&
        SDL_asprintf(&old, "%s.old", l->mix.dir) >= 0 && SDL_asprintf(&built, "%s/%s", l->mix.dir, PORT_MIX_BUILT) >= 0) {
        if (!SDL_GetPathInfo(l->mix.dir, NULL)) {
            SDL_RenamePath(old, l->mix.dir);
        }
        Port_RemoveTree(old);
        Port_RemoveTree(part);
        RefreshMix(l);
        ok = SDL_GetPathInfo(built, &info) && info.modify_time >= l->convStart && l->mixState == CONV_OK;
    }
    SDL_free(part);
    SDL_free(old);
    SDL_free(built);
    if (ok) {
        SDL_snprintf(l->status, sizeof(l->status), "The mixed game is built: %d packs.", l->mixPacks);
    } else if (cancel) {
        SDL_snprintf(l->status, sizeof(l->status), "Build cancelled: nothing changed.");
    } else if (l->convError[0] != '\0') {
        SDL_snprintf(l->status, sizeof(l->status), "Not built: %s", l->convError);
    } else {
        SDL_snprintf(l->status, sizeof(l->status), "Not built: the builder stopped (%d) at %s.", code,
                     l->convName[0] ? l->convName : "the start");
    }
}

/* The converter's output so far; when it has ended, its folder swapped in
 * (or removed). `cancel`: stop it now. */
static void PollConvert(Launcher *l, int cancel) {
    SDL_IOStream *out = SDL_GetProcessOutput(l->converter);
    char buf[2048], *dir, *part = NULL, *old = NULL, *manifest = NULL;
    int code = -1, ended, ok = 0;
    size_t n, i;
    SDL_PathInfo info;

    if (cancel) {
        SDL_KillProcess(l->converter, true);
    }
    ended = SDL_WaitProcess(l->converter, cancel, &code);
    while (out != NULL && (n = SDL_ReadIO(out, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                l->convLine[l->convLineLen] = '\0';
                ConvertLine(l, l->convLine);
                l->convLineLen = 0;
            } else if (l->convLineLen + 1 < sizeof(l->convLine)) {
                l->convLine[l->convLineLen++] = buf[i];
            }
        }
    }
    if (!ended) {
        return;
    }
    SDL_DestroyProcess(l->converter);
    l->converter = NULL;
    if (l->convSource == SRC_MIX) {
        EndMixBuild(l, cancel, code);
        return;
    }
    dir = Port_ConvertedDir(s_keys[l->convSource]);
    if (dir != NULL && SDL_asprintf(&part, "%s.part", dir) >= 0 && SDL_asprintf(&old, "%s.old", dir) >= 0 &&
        SDL_asprintf(&manifest, "%s/%s", part, PORT_CONVERT_MANIFEST) >= 0) {
        /* only a finished conversion takes the last one's place */
        if (!cancel && code == 0 && SDL_GetPathInfo(manifest, NULL)) {
            Port_RemoveTree(old);
            if (SDL_GetPathInfo(dir, &info) && !SDL_RenamePath(dir, old)) {
                SDL_snprintf(l->convError, sizeof(l->convError), "could not replace %s: %s", dir, SDL_GetError());
            } else if (!SDL_RenamePath(part, dir)) {
                SDL_snprintf(l->convError, sizeof(l->convError), "could not finish %s: %s", dir, SDL_GetError());
                SDL_RenamePath(old, dir);
            } else {
                ok = 1;
            }
            Port_RemoveTree(old);
        }
        Port_RemoveTree(part);
    }
    RefreshConverted(l);
    if (ok) {
        SDL_snprintf(l->status, sizeof(l->status), "The %s copy is converted: %d packs.", s_names[l->convSource],
                     l->convPacks[l->convSource]);
    } else if (cancel) {
        SDL_snprintf(l->status, sizeof(l->status), "Conversion cancelled: nothing changed.");
    } else if (l->convError[0] != '\0') {
        SDL_snprintf(l->status, sizeof(l->status), "Not converted: %s", l->convError);
    } else {
        SDL_snprintf(l->status, sizeof(l->status), "Not converted: the converter stopped (%d) at %s.", code,
                     l->convName[0] ? l->convName : "the start");
    }
    SDL_free(dir);
    SDL_free(part);
    SDL_free(old);
    SDL_free(manifest);
}

static void DeleteConverted(Launcher *l, int src) {
    char *dir = Port_ConvertedDir(s_keys[src]);

    if (dir != NULL) {
        Port_RemoveTree(dir);
        SDL_free(dir);
    }
    RefreshConverted(l);
    SDL_snprintf(l->status, sizeof(l->status), "The %s copy's converted packs are deleted.", s_names[src]);
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
        SDL_snprintf(l->status, sizeof(l->status), "%s",
                     SDL_strcmp(l->plan.target, "no save yet") == 0
                         ? "Import makes a new save."
                         : "Import keeps your save as it was in hwvx.before-import.json.");
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

/* Shows page `tab` (wrapping round), remembered in sources.cfg (written when
 * the launcher closes). Not while the saves preview is open. */
static void SwitchTab(Launcher *l, int tab) {
    if (l->savesView) {
        return;
    }
    l->tab = (tab + TAB_COUNT) % TAB_COUNT;
    l->src.tab = l->tab;
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
        l->src.mix = 0;
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
            int i;

            l->src.play = -1;
            for (i = 0; l->src.mix && l->src.play < 0 && i < PORT_DATA_COUNT; i++) {
                if (l->src.path[i] != NULL) {
                    l->src.play = i;
                }
            }
        }
        SaveSources(&l->src);
        RefreshConverted(l);
        SDL_snprintf(l->status, sizeof(l->status), "%s removed from the list (its files stay).", s_names[b->source]);
        return 0;
    case ACT_PROTO_MODE:
        l->src.proto = b->source;
        SaveSources(&l->src);
        SDL_snprintf(l->status, sizeof(l->status), "Prototype: %s. It applies when the game starts.",
                     b->source == PORT_PROTO_OFF ? "off" : b->source == PORT_PROTO_CARS ? "its cars only"
                     : b->source == PORT_PROTO_ADDED ? "its content added" : "it replaces the release's cars");
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

        *v = s_settings[i].third != NULL ? (*v + 1) % 3 : !*v;
        Port_GfxSaveConfig();
        SDL_snprintf(l->status, sizeof(l->status), "%s: %s. Saved in graphics.cfg.", s_settings[i].name,
                     SettingLabel(i, *v));
        return 0;
    }
    case ACT_CONVERT:
        StartConvert(l, b->source);
        return 0;
    case ACT_CONV_DELETE:
        DeleteConverted(l, b->source);
        return 0;
    case ACT_CONV_CANCEL:
        if (l->converter != NULL) {
            PollConvert(l, 1);
        }
        return 0;
    case ACT_PLAY_MIX:
        l->src.mix = 1;
        SaveSources(&l->src);
        SDL_snprintf(l->status, sizeof(l->status), "The mixed game chosen: Play starts it as --mix %s.", MIX_RECIPE);
        return 0;
    case ACT_MIX_BUILD:
        StartConvert(l, SRC_MIX);
        return 0;
    case ACT_MIX_IFACE:
        l->src.ifacePS2 = !l->src.ifacePS2;
        SaveSources(&l->src);
        RefreshMix(l);      /* the stamp has the choice: a build of the other set is out of date */
        SDL_snprintf(l->status, sizeof(l->status), "The mixed game's menus and HUD: %s.%s",
                     l->src.ifacePS2 ? "the PS2's silver set" : "the GameCube's yellow set",
                     l->mixState == CONV_STALE ? " Build again to use it." : "");
        return 0;
    case ACT_MIX_DELETE:
        if (l->mix.dir[0] != '\0') {
            Port_RemoveTree(l->mix.dir);
        }
        RefreshMix(l);
        SDL_snprintf(l->status, sizeof(l->status), "The mixed game's packs are deleted.");
        return 0;
    case ACT_HOOK_MODS:
        RunHook(l, Port_LauncherMods);
        return 0;
    case ACT_MOD_TOGGLE:
    case ACT_MOD_UP: {
        const PortModInfo *m = Port_ModsInfo(b->source);

        if (m == NULL) {
            return 0;
        }
        if (b->act == ACT_MOD_TOGGLE) {
            Port_ModsSetEnabled(b->source, !m->enabled);
        } else {
            Port_ModsMove(b->source, b->source - 1);
        }
        l->modRemove = -1;
        SDL_snprintf(l->status, sizeof(l->status), "%s", Port_ModsSave() ? "Saved: the game uses it next time it starts."
                                                                          : "Couldn't save the mods' order.");
        return 0;
    }
    case ACT_MOD_REMOVE: {
        const PortModInfo *m = Port_ModsInfo(b->source);

        if (m == NULL) {
            return 0;
        }
        if (l->modRemove != b->source) {
            l->modRemove = b->source;
            SDL_snprintf(l->status, sizeof(l->status), "Press Sure? to delete %.40s's folder.", m->name);
            return 0;
        }
        SDL_snprintf(l->status, sizeof(l->status), "%.40s %s", m->name, Port_ModsRemove(b->source) ? "removed." : "could not be removed.");
        l->modRemove = -1;
        return 0;
    }
    case ACT_MOD_INSTALL: {
        static const SDL_DialogFileFilter mods[] = { { "Mod packages (.xmod)", "xmod" } };

        SDL_SetAtomicInt(&l->picked, 0);
        l->pickMod = 1;
        l->dialogOpen = 1;
        SDL_ShowOpenFileDialog(Picked, l, l->window, mods, 1, NULL, false);
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
        } else if (l->pickMod) {
            char err[256];

            if (l->pickedPath != NULL && Port_ModsInstall(l->pickedPath, err, sizeof(err))) {
                SDL_snprintf(l->status, sizeof(l->status), "Installed: the game uses it next time it starts.");
                Port_ModsSave();
            } else if (l->pickedPath != NULL) {
                SDL_snprintf(l->status, sizeof(l->status), "Not installed: %.200s", err);
            }
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
        l->pickSaves = l->pickMod = 0;
    }
    if (l->converter != NULL) {
        PollConvert(l, 0);
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

/* Testing: HWVX_LAUNCHER_INPUT="rb,down,a,..." queues keys (pgup pgdn tab
 * up down left right enter esc), gamepad buttons (lb rb a b start dup ddown
 * dleft dright) and clicks (click:X:Y, in the 640x420 view) as if the player
 * had pressed them, with waits between them (wait:MS, or waitconvert: until
 * the converter has ended); HWVX_LAUNCHER_SHOT then saves the frame after
 * them. */
static void QueueInput(Launcher *l) {
    static const struct {
        const char *name;
        int key, pad;
    } names[] = {
        { "pgup", SDLK_PAGEUP, -1 }, { "pgdn", SDLK_PAGEDOWN, -1 }, { "tab", SDLK_TAB, -1 },
        { "up", SDLK_UP, -1 }, { "down", SDLK_DOWN, -1 }, { "left", SDLK_LEFT, -1 },
        { "right", SDLK_RIGHT, -1 }, { "enter", SDLK_RETURN, -1 }, { "esc", SDLK_ESCAPE, -1 },
        { "lb", 0, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "rb", 0, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
        { "a", 0, SDL_GAMEPAD_BUTTON_SOUTH }, { "b", 0, SDL_GAMEPAD_BUTTON_EAST },
        { "start", 0, SDL_GAMEPAD_BUTTON_START }, { "dup", 0, SDL_GAMEPAD_BUTTON_DPAD_UP },
        { "ddown", 0, SDL_GAMEPAD_BUTTON_DPAD_DOWN }, { "dleft", 0, SDL_GAMEPAD_BUTTON_DPAD_LEFT },
        { "dright", 0, SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
    };
    const char *env = SDL_getenv("HWVX_LAUNCHER_INPUT");
    char *list, *p, *save = NULL;
    SDL_Event ev;
    size_t i;

    if (env == NULL) {
        return;
    }
    list = SDL_strdup(env);
    for (p = SDL_strtok_r(list, ", ", &save); p != NULL; p = SDL_strtok_r(NULL, ", ", &save)) {
        float x, y;

        SDL_zero(ev);
        if (SDL_strcmp(p, "waitconvert") == 0 || SDL_sscanf(p, "wait:%f", &x) == 1) {
            ev.type = SDL_EVENT_USER;       /* wait here: for the converter to end, or x ms */
            ev.user.code = SDL_strcmp(p, "waitconvert") == 0 ? -1 : (Sint32)x;
            SDL_PushEvent(&ev);
            continue;
        }
        if (SDL_sscanf(p, "click:%f:%f", &x, &y) == 2) {
            SDL_RenderCoordinatesToWindow(l->renderer, x, y, &x, &y);
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.windowID = SDL_GetWindowID(l->window);
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.down = true;
            ev.button.x = x;
            ev.button.y = y;
            SDL_PushEvent(&ev);
            continue;
        }
        for (i = 0; i < SDL_arraysize(names) && SDL_strcmp(p, names[i].name) != 0; i++) {
        }
        if (i == SDL_arraysize(names)) {
            Port_Log("HWVX_LAUNCHER_INPUT: no such input: %s", p);
            continue;
        }
        if (names[i].pad >= 0) {
            ev.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            ev.gbutton.button = (Uint8)names[i].pad;
            ev.gbutton.down = true;
        } else {
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.key = (SDL_Keycode)names[i].key;
            ev.key.down = true;
        }
        SDL_PushEvent(&ev);
    }
    SDL_free(list);
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;   /* the end of them */
    SDL_PushEvent(&ev);
    l->scripted = 1;
}

static int Run(Launcher *l) {
    SDL_Gamepad *pads[8] = { NULL };
    int result = 0, i;
    static int held[2];

    QueueInput(l);
    while (result == 0) {
        SDL_Event ev;

        Poll(l);
        Layout(l);
        Draw(l);
        if (SDL_getenv("HWVX_LAUNCHER_SHOT") != NULL && !l->scripted) {
            result = -1;    /* testing: Draw saved the frame */
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
            case SDL_EVENT_USER:
                if (ev.user.code != 0) {
                    /* testing: wait, the window kept up, before the inputs that follow */
                    Uint64 until = SDL_GetTicks() + (ev.user.code > 0 ? (Uint64)ev.user.code : 600000);

                    while (SDL_GetTicks() < until && (ev.user.code > 0 || l->converter != NULL)) {
                        Poll(l);
                        Layout(l);
                        Draw(l);
                        SDL_Delay(10);
                    }
                    break;
                }
                l->scripted = 0;
                break;
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
                        result = l->dialogOpen ? 0 : -1;    /* not while a dialog is open */
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
            case SDL_EVENT_GAMEPAD_REMOVED:
                for (i = 0; i < 8; i++) {
                    if (pads[i] != NULL && SDL_GetGamepadID(pads[i]) == ev.gdevice.which) {
                        SDL_CloseGamepad(pads[i]);
                        pads[i] = NULL;
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
                    result = l->savesView ? Activate(l, &l->buttons[ButtonFor(l, ACT_IMP_CANCEL)])
                             : l->dialogOpen ? 0
                                             : -1;
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
    }
    if (l->converter != NULL) {
        PollConvert(l, 1);  /* stopped: the last complete conversion stays */
    }
    if (l->importer != NULL) {
        /* stop the import (it stops within a few sectors and leaves the
         * last good copy as it was) */
        SDL_SetAtomicInt(&l->importCancel, 1);
        SDL_snprintf(l->status, sizeof(l->status), "Stopping the disc import...");
        if (SDL_getenv("HWVX_LAUNCHER_SHOT") == NULL) {
            Draw(l);
        }
        SDL_WaitThread(l->importer, NULL);
        l->importer = NULL;
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
static const char *s_playMix;

const char *Port_LauncherMix(void) {
    return s_playMix;
}

/* What was chosen, to play: the release's path, or NULL with s_playMix set. */
static char *PlayChosen(Launcher *l) {
    if (PlayingMix(l)) {
        s_playMix = MIX_RECIPE;     /* main_sdl.c starts it as --mix */
        return NULL;
    }
    UseConverted(&l->src);
    return SDL_strdup(l->src.path[l->src.play]);
}

char *Port_Launcher(int open, int window, Port_DataCheck check, Port_DataSearch search) {
    /* static: a file dialog's answer (Picked, on SDL's thread) may come after
     * the window closed (the program asked to quit while it was open) */
    static Launcher l;
    char *chosen = NULL, *legacy, *found = NULL;
    int i, valid, firstRun, tabLoaded;

    SDL_zero(l);
    l.pointer.x = l.pointer.y = -1;
    l.check = check;
    firstRun = !LoadSources(&l.src);
    tabLoaded = l.src.tab;
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

    if (l.src.mix) {
        RefreshMix(&l);     /* the mixed game is chosen: can it be played? */
    }
    valid = l.src.play >= 0 && l.src.path[l.src.play] != NULL;
    if (valid && !PlayingMix(&l)) {
        char *ok = check(l.src.path[l.src.play]);

        valid = ok != NULL;
        SDL_free(ok);
    }
    valid |= PlayingMix(&l);
    if (valid && !open && (l.src.skip || !window)) {
        chosen = PlayChosen(&l);
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
            chosen = PlayChosen(&l);
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
    RefreshConverted(&l);
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
    if (SDL_getenv("HWVX_LAUNCHER_DISC") != NULL) {
        /* testing: "pc=IMAGE" or "ps2=IMAGE", imported as if picked on Games */
        const char *disc = SDL_getenv("HWVX_LAUNCHER_DISC");

        l.tab = TAB_GAMES;
        if (SDL_strncmp(disc, "pc=", 3) == 0) {
            StartDisc(&l, disc + 3, PORT_DATA_PC);
        } else if (SDL_strncmp(disc, "ps2=", 4) == 0) {
            StartDisc(&l, disc + 4, PORT_DATA_PS2);
        }
    }
    if (Run(&l) > 0 && CanPlay(&l)) {
        chosen = PlayChosen(&l);
    }
    if (l.src.tab != tabLoaded) {
        SaveSources(&l.src);    /* the page shown last */
    }
    CloseSaves(&l);
    SDL_DestroyRenderer(l.renderer);
    SDL_DestroyWindow(l.window);
    FreeSources(&l.src);
    return chosen;
}
