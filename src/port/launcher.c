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
 * need a mouse or a keyboard. */
#include "port_internal.h"

#define VIEW_W 640
#define VIEW_H 420
#define CHAR 8

/* The sources: the releases, then the prototype. */
enum { SRC_BETA = PORT_DATA_COUNT, SRC_COUNT };

static const char *const s_keys[SRC_COUNT] = { "gc", "pc", "ps2", "ps2beta" };
static const char *const s_names[SRC_COUNT] = { "GameCube", "PC", "PlayStation 2", "PS2 prototype (beta)" };

typedef struct Sources {
    char *path[SRC_COUNT];
    int play;           /* PORT_DATA_*, or -1 */
    int skip;           /* start the game without the launcher */
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
    text = path != NULL ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    if (text == NULL) {
        return 0;
    }
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strcmp(line, "skip=1") == 0) {
            s->skip = 1;
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

enum { ACT_NONE, ACT_PLAY_THIS, ACT_FOLDER, ACT_IMAGE, ACT_REMOVE, ACT_PLAY, ACT_QUIT, ACT_CONVERT, ACT_SKIP };

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
} Launcher;

#define ROW_Y(i) (56.0f + (i) * 62.0f)

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

/* The buttons for the current state, in focus order. */
static void Layout(Launcher *l) {
    int i, busy = l->importer != NULL || l->dialogOpen;

    l->numButtons = 0;
    for (i = 0; i < SRC_COUNT; i++) {
        float x = 24, y = ROW_Y(i) + 28;
        int have = l->src.path[i] != NULL;

        if (i < PORT_DATA_COUNT) {
            x += Add(l, x, y, l->src.play == i ? "Playing this" : "Play this", ACT_PLAY_THIS, i, have && !busy)->r.w + 8;
        }
        x += Add(l, x, y, have ? "Change folder..." : "Import folder...", ACT_FOLDER, i, !busy)->r.w + 8;
        {
            x += Add(l, x, y, "Import disc image...", ACT_IMAGE, i, !busy)->r.w + 8;
        }
        if (have) {
            Add(l, x, y, "Remove", ACT_REMOVE, i, !busy);
        }
    }
    Add(l, 24, 316, "Convert to .xdl", ACT_CONVERT, -1, 0);
    Add(l, 24, VIEW_H - 30, "Play", ACT_PLAY, -1, !busy && l->src.play >= 0 && l->src.path[l->src.play] != NULL);
    Add(l, 24 + 4 * CHAR + 24, VIEW_H - 30, "Quit", ACT_QUIT, -1, 1);
    Add(l, 24 + 8 * CHAR + 64, VIEW_H - 30,
        l->src.skip ? "[x] Start the game without this window" : "[ ] Start the game without this window", ACT_SKIP,
        -1, 1);
    if (l->focus >= l->numButtons) {
        l->focus = l->numButtons - 1;
    }
    /* the buttons may have moved (one added or removed) under a still pointer */
    l->hover = -1;
    for (i = 0; i < l->numButtons; i++) {
        if (l->buttons[i].enabled && SDL_PointInRectFloat(&l->pointer, &l->buttons[i].r)) {
            l->hover = i;
        }
    }
}

static void Draw(Launcher *l) {
    char line[128];
    int i;

    SDL_SetRenderDrawColor(l->renderer, 18, 22, 34, 255);
    SDL_RenderClear(l->renderer);
    Text(l, 24, 14, 255, 210, 80, "Hot Wheels: Velocity X");
    Text(l, 24, 28, 150, 160, 180, "Your copies of the game. Pick the one to play.");
    for (i = 0; i < SRC_COUNT; i++) {
        float y = ROW_Y(i);

        Text(l, 24, y, 235, 235, 240, s_names[i]);
        if (i == SRC_BETA) {
            Text(l, 24 + 21 * CHAR, y, 130, 130, 140, "- for beta cars, worlds, audio (later)");
        }
        if (l->src.path[i] != NULL) {
            Fit(line, sizeof(line), l->src.path[i], 74);
            Text(l, 24, y + 13, 140, 200, 140, line);
        } else {
            Text(l, 24, y + 13, 130, 130, 140, "not imported");
        }
    }
    Text(l, 24, 304, 235, 235, 240, "Convert");
    Text(l, 24 + 16 * CHAR + 24, 321, 130, 130, 140, "coming later: packs made for this port");
    Text(l, 24, 346, 235, 235, 240, "Mods");
    Text(l, 24 + 5 * CHAR, 346, 130, 130, 140, "- coming later");
    Text(l, 24, VIEW_H - 48, 255, 190, 120, l->status);
    for (i = 0; i < l->numButtons; i++) {
        const Button *b = &l->buttons[i];
        int on = l->mouse ? i == l->hover : i == l->focus;

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

/* The index of the button that does `act`. */
static int ButtonFor(const Launcher *l, int act) {
    int i;

    for (i = 0; i < l->numButtons && l->buttons[i].act != act; i++) {
    }
    return i < l->numButtons ? i : 0;
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

    for (i = 0; i < l->numButtons; i++) {
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
        } else if (l->pickedPath != NULL) {
            if (l->pickImage && l->pickSource != PORT_DATA_GC) {
                StartDisc(l, l->pickedPath, l->pickSource);
            } else {
                Import(l, l->pickedPath, l->pickSource);
            }
            SDL_free(l->pickedPath);
            l->pickedPath = NULL;
        }
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
                if (l->hover >= 0) {
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
                case SDLK_TAB: l->focus = (l->focus + 1) % l->numButtons; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: result = Activate(l, focused); break;
                case SDLK_ESCAPE: result = -1; break;
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
                case SDL_GAMEPAD_BUTTON_EAST: result = -1; break;
                case SDL_GAMEPAD_BUTTON_START: result = Activate(l, &l->buttons[ButtonFor(l, ACT_PLAY)]); break;
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
    Layout(&l);
    if (valid) {
        l.focus = ButtonFor(&l, ACT_PLAY);     /* A or Enter starts the game */
    }
    if (Run(&l) > 0 && l.src.play >= 0 && l.src.path[l.src.play] != NULL) {
        chosen = SDL_strdup(l.src.path[l.src.play]);
    }
    SDL_DestroyRenderer(l.renderer);
    SDL_DestroyWindow(l.window);
    FreeSources(&l.src);
    return chosen;
}
