/*
 * test_content.c - checks the content registry (src/game/Port_Content*.c)
 * against the GameCube's own data, with the whole game and no window.
 *
 * Each scenario is a fresh game in a child process (the test runs itself
 * with the scenario's name): the game boots with a stub renderer and no
 * pacing (HWVX_DETERMINISTIC, HWVX_NO_VIWAIT), HWVX_START opens a screen,
 * HWVX_MENUDUMP logs what it offers, and the checks read the log, as a
 * person would grep a run's. A few seconds a scenario.
 *
 *   carselect    retail: 33 cars, the lists' own 36 entries, nothing widened
 *   extracars    HWVX_TEST_EXTRA_CARS=2: cars 33, 34 named after their alias
 *                with " (test)", its picture, a blank paragraph
 *   itemselect   HWVX_TEST_EXTRA_ITEMS=2: items 12, 13 the same way
 *   challenge, drag, battle, joyride
 *                each mode's world select: its track list's range covers its
 *                progression script's levels (the MCP's), by name
 *   adventure    the Adventure screen opens
 *   item16       HWVX_TEST_EXTRA_ITEMS=6: a race with item 16 (aliasing
 *                item 4) loads HW_Items pack 4, not the AI's pack 16
 *
 * Data: the GameCube's extracted files, HWVX_TEST_GC_DATA or orig/gc/files
 * beside the source tree; without them the test is skipped (77).
 *   ./test_content [scenario]     all of them, or one
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_internal.h"
#include "gx_render.h"

#ifndef HWVX_SOURCE_DIR
#define HWVX_SOURCE_DIR "."
#endif

void InitRenderMode(void *mode);
void InitMem(void);

/* ---- the stub renderer --------------------------------------------------- */

static int Stub_open(void) { return 1; }
static void Stub_close(void) { }
static void Stub_init(int w, int h) { (void)w; (void)h; }
static void Stub_resize(int w, int h) { (void)w; (void)h; }
static int Stub_texFormat(int fmt) { return fmt == GXR_TEX_RGBA8; }
static void *Stub_texCreate(int fmt, int w, int h, int levels)
{
    (void)fmt; (void)w; (void)h; (void)levels;
    return (void *)(uintptr_t)1;
}
static void Stub_texUpload(void *tex, int fmt, int level, int w, int h, const u8 *rgba)
{
    (void)tex; (void)fmt; (void)level; (void)w; (void)h; (void)rgba;
}
static void Stub_texDestroy(void *tex) { (void)tex; }
static void Stub_draw(const GXDrawCall *dc) { (void)dc; }
static void Stub_clear(const f32 color[4], f32 depth) { (void)color; (void)depth; }
static void Stub_drawImage(const u8 *rgba, int w, int h, const int dst[4])
{
    (void)rgba; (void)w; (void)h; (void)dst;
}
static void Stub_copyDisp(const int src[4]) { (void)src; }
static int Stub_readEFB(u8 *rgba) { (void)rgba; return 0; }
static void Stub_present(void) { }
static void Stub_copyEFB(void *tex, const int src[4]) { (void)tex; (void)src; }
static void *Stub_meshIndices(const u16 *idx, u32 n) { (void)idx; (void)n; return (void *)(uintptr_t)1; }
static void *Stub_meshVertices(const GXMeshVertex *v, u32 n, int skinned, void *indices)
{
    (void)v; (void)n; (void)skinned; (void)indices;
    return (void *)(uintptr_t)1;
}
static void Stub_meshDestroy(void *buf) { (void)buf; }

static const GXRenderer s_stub = {
    .name = "test", .open = Stub_open, .close = Stub_close, .init = Stub_init, .resize = Stub_resize,
    .texFormat = Stub_texFormat, .texCreate = Stub_texCreate, .texUpload = Stub_texUpload, .texDestroy = Stub_texDestroy,
    .draw = Stub_draw, .clear = Stub_clear, .drawImage = Stub_drawImage, .copyDisp = Stub_copyDisp,
    .readEFB = Stub_readEFB, .present = Stub_present, .copyEFB = Stub_copyEFB, .meshIndices = Stub_meshIndices,
    .meshVertices = Stub_meshVertices, .meshDestroy = Stub_meshDestroy,
};

/* ---- the log --------------------------------------------------------------- */

static char *s_log;
static size_t s_logLen, s_logCap;
static int s_verbose;

static void SDLCALL Capture(void *user, int category, SDL_LogPriority priority, const char *message)
{
    size_t n = strlen(message);

    (void)user; (void)category; (void)priority;
    if (s_logLen + n + 2 > s_logCap) {
        s_logCap = (s_logLen + n + 2) * 2;
        s_log = realloc(s_log, s_logCap);
    }
    memcpy(s_log + s_logLen, message, n);
    s_logLen += n;
    s_log[s_logLen++] = '\n';
    s_log[s_logLen] = 0;
    if (s_verbose) {
        fprintf(stderr, "%s\n", message);
    }
}

/* The first log line containing `what` (from `from`), or NULL. */
static const char *Line(const char *what, const char *from)
{
    const char *p = s_log != NULL ? strstr(from != NULL ? from : s_log, what) : NULL;

    while (p != NULL && p > s_log && p[-1] != '\n') {
        p--;
    }
    return p;
}

/* Entry i of a dumped list ("... 0 \"Twin Mill 2*\" | 1 ..." or "0 63 |
 * 1 64"), which may go on over the next lines with the same head: its text
 * into out. */
static int EntryInLine(const char *line, u32 i, char *out, size_t size)
{
    char key[16];
    const char *p, *end = strchr(line, '\n'), *colon = strstr(line, "): ");
    size_t n;

    if (colon == NULL || end == NULL || colon > end) {
        return 0;
    }
    snprintf(key, sizeof(key), " %u ", i);
    for (p = colon + 1; p < end; p++) {
        if (strncmp(p, key, strlen(key)) == 0 && (p[-1] == ':' || p[-1] == '|')) {
            const char *q;

            p += strlen(key);
            if (*p == '"') {
                q = strstr(p + 1, "\" | ");
                q = q != NULL && q < end ? q : end - 1;
                p++;
            } else {
                q = strchr(p, ' ');
                q = q != NULL && q < end ? q : end;
            }
            n = (size_t)(q - p);
            n = n < size - 1 ? n : size - 1;
            memcpy(out, p, n);
            out[n] = 0;
            return 1;
        }
    }
    return 0;
}

static int Entry(const char *line, u32 i, char *out, size_t size)
{
    const char *head = strstr(line, "menudump:   "), *colon = strstr(line, "): ");
    size_t n = head != NULL && colon != NULL ? (size_t)(colon - head) : 0;

    while (n != 0) {
        const char *nl = strchr(line, '\n'), *h;

        if (EntryInLine(line, i, out, size)) {
            return 1;
        }
        if (nl == NULL || (h = strstr(nl + 1, "menudump:   ")) == NULL || h > strchr(nl + 1, '\n') ||
            strncmp(h, head, n) != 0) {
            return 0;
        }
        line = nl + 1;
        head = h;
    }
    return 0;
}

/* The dump line of a list kind ("names", "pictures", "paragraphs") under
 * the first "lists of interface <id>" header whose variable ranges `range`. */
static const char *List(u32 id, const char *range, const char *kind)
{
    char head[96], want[32];
    const char *p = s_log;

    snprintf(head, sizeof(head), "menudump: lists of interface %u (", id);
    snprintf(want, sizeof(want), "menudump:   %s ", kind);
    while ((p = Line(head, p)) != NULL) {
        const char *nl = strchr(p, '\n'), *next;

        if (nl == NULL) {
            return NULL;
        }
        if (strstr(p, range) != NULL && strstr(p, range) < nl && (next = Line("menudump: ", nl + 1)) != NULL &&
            strstr(next, want) != NULL && strstr(next, want) < strchr(next, '\n')) {
            return next;
        }
        p = nl + 1;
    }
    return NULL;
}

/* ---- checks -------------------------------------------------------------- */

static int s_fails;

static void Check(int ok, const char *what, ...)
{
    va_list ap;

    printf(ok ? "  ok   " : "  FAIL ");
    va_start(ap, what);
    vprintf(what, ap);
    va_end(ap);
    putchar('\n');
    if (!ok) {
        s_fails++;
    }
}

/* A registered entry shows as its alias: name + " (test)", the alias's
 * picture, a blank paragraph. */
static void CheckExtra(u32 id, const char *range, u32 n, u32 alias)
{
    const char *names = List(id, range, "names"), *pics = List(id, range, "pictures"),
               *paras = List(id, range, "paragraphs");
    char a[96], b[96], want[112];

    Check(names != NULL && pics != NULL && paras != NULL, "interface %u: names, pictures and paragraphs over %s", id,
          range);
    if (names == NULL || pics == NULL || paras == NULL) {
        return;
    }
    Check(Entry(names, alias, a, sizeof(a)) && Entry(names, n, b, sizeof(b)) &&
              (snprintf(want, sizeof(want), "%s (test)", a), strcmp(b, want) == 0),
          "entry %u's name is entry %u's with \" (test)\": \"%s\"", n, alias, b);
    Check(Entry(pics, alias, a, sizeof(a)) && Entry(pics, n, b, sizeof(b)) && strcmp(a, b) == 0,
          "entry %u's picture is entry %u's (texture %s)", n, alias, b);
    Check(Entry(paras, n, b, sizeof(b)) && strcmp(b, " ") == 0, "entry %u's paragraph is blank", n);
}

/* A mode's world select: the track names' variable ranges over as many as
 * its progression script has levels, and the screen and script agree. */
static void CheckWorlds(u32 progression)
{
    char want[64];
    const char *prog, *lists;
    int levels = -1, lo = -1, hi = -1;

    snprintf(want, sizeof(want), "menudump: progression %u: ", progression);
    prog = Line(want, NULL);
    if (prog != NULL) {
        sscanf(strstr(prog, want) + strlen(want), "%d levels", &levels);
    }
    lists = Line("variable 11: ", NULL);
    if (lists != NULL) {
        sscanf(strstr(lists, "variable 11: ") + 13, "%d..%d", &lo, &hi);
    }
    Check(levels > 0, "progression %u has %d levels", progression, levels);
    Check(lists != NULL && hi - lo + 1 == levels, "its world select's track names range over %d..%d (%d tracks)", lo,
          hi, hi - lo + 1);
    Check(List(0, "variable 11: ", "names") != NULL, "the track names are listed");
}

/* ---- scenarios --------------------------------------------------------------- */

typedef struct Scenario {
    const char *name;
    const char *env[6];         /* NAME=value */
    const char *until;          /* log text that says the screen is there */
} Scenario;

static const Scenario s_scenarios[] = {
    { "carselect", { "HWVX_START=carselect" }, "menudump:   paragraphs 36" },
    { "extracars", { "HWVX_START=carselect", "HWVX_TEST_EXTRA_CARS=2" }, "menudump:   paragraphs 36" },
    { "itemselect", { "HWVX_START=itemselect", "HWVX_TEST_EXTRA_ITEMS=2" }, "menudump:   paragraphs 14" },
    { "challenge", { "HWVX_START=worldselect:mode=challenge" }, "menudump:   paragraphs 57" },
    { "drag", { "HWVX_START=worldselect:mode=drag" }, "menudump:   paragraphs 57" },
    { "battle", { "HWVX_START=worldselect:mode=battle" }, "menudump:   paragraphs 57" },
    { "joyride", { "HWVX_START=worldselect:mode=joyride" }, "menudump:   paragraphs 57" },
    { "adventure", { "HWVX_START=menu:mode=adventure" }, "start: menu, adventure" },
    { "item16", { "HWVX_START=race:item=16", "HWVX_TEST_EXTRA_ITEMS=6" }, "-> STATE_GAME_PLAY" },
};

static void Checks(const char *name)
{
    if (strcmp(name, "carselect") == 0) {
        const char *names = List(2, ": 0..32", "names");

        Check(Line("content: 33 cars (33 in HW_Cars), 57 tracks, 12 items", NULL) != NULL,
              "33 cars from HW_Cars' TOC, 57 tracks, 12 items");
        Check(names != NULL && strstr(names, "names 36 ") != NULL, "the car select's 36 names, over 0..32");
        Check(Line("ranges over", NULL) == NULL && Line("lists name and picture", NULL) == NULL,
              "with the retail counts nothing is widened or extended");
    } else if (strcmp(name, "extracars") == 0) {
        Check(Line("content: 35 cars", NULL) != NULL, "35 cars");
        CheckExtra(2, ": 0..34", 33, 3);
        CheckExtra(2, ": 0..34", 34, 4);
    } else if (strcmp(name, "itemselect") == 0) {
        Check(Line("start: the car select picked car", NULL) != NULL, "the car select went on to the item select");
        CheckExtra(3, ": 0..13", 12, 0);
        CheckExtra(3, ": 0..13", 13, 1);
    } else if (strcmp(name, "challenge") == 0) {
        CheckWorlds(1);
    } else if (strcmp(name, "drag") == 0) {
        CheckWorlds(2);
    } else if (strcmp(name, "battle") == 0) {
        CheckWorlds(3);
    } else if (strcmp(name, "joyride") == 0) {
        CheckWorlds(4);
    } else if (strcmp(name, "item16") == 0) {
        Check(Line("content: test: item 16 is uid://hwvx-test-extra-item-4, the pack of item 4", NULL) != NULL,
              "item 16 is registered, aliasing item 4");
        Check(Line("data pack HW_Items: type 2 id 4\n", Line("start: race", NULL)) != NULL,
              "a race with item 16 loads HW_Items pack 4");
        Check(Line("data pack HW_Items: type 2 id 16\n", NULL) == NULL,
              "and not pack 16 (the AI's): item numbers aren't pack ids");
    } else if (strcmp(name, "adventure") == 0) {
        Check(Line("interface create: header id 0", Line("start: menu, adventure", NULL)) != NULL,
              "the Adventure screen opens");
    }
}

static void SetEnv(const char *kv)
{
    char name[64];
    const char *eq = strchr(kv, '=');
    size_t n = (size_t)(eq - kv);

    memcpy(name, kv, n);
    name[n] = 0;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, eq + 1, true);
#ifdef _WIN32
    _putenv_s(name, eq + 1);
#else
    setenv(name, eq + 1, 1);
#endif
}

/* One scenario, in this process: boot, run until its screen is up, check. */
static int RunOne(const Scenario *sc, const char *data, const char *out)
{
    char pref[1024];
    int i, frames;

    snprintf(pref, sizeof(pref), "HWVX_PREF_DIR=%s/pref-%s", out, sc->name);
    SetEnv(pref);
    SetEnv("HWVX_DETERMINISTIC=1");
    SetEnv("HWVX_NO_VIWAIT=1");
    SetEnv("HWVX_NO_MOVIE=1");
    SetEnv("HWVX_NO_DIALOG=1");
    SetEnv("HWVX_MENUDUMP=1");
    for (i = 0; i < 6 && sc->env[i] != NULL; i++) {
        SetEnv(sc->env[i]);
    }
    SDL_SetLogOutputFunction(Capture, NULL);
    if (!SDL_Init(0)) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    g_gxr = &s_stub;
    g_port.dataDir = data;
    Port_InitHandles(PORT_ARENA_SIZE);
    OSInit();
    DVDInit();
    VIInit();
    PADInit();
    CARDInit();
    InitRenderMode(NULL);
    InitMem();
    if (!Main_Init()) {
        printf("Main_Init failed\n");
        return 1;
    }
    for (frames = 0; frames < 3000 && !g_port.quit; frames++) {
        if (!Main_Loop()) {
            break;
        }
        if (Line(sc->until, NULL) != NULL && frames > 0) {
            /* a few frames more: the screen's children are created after it */
            int k;

            for (k = 0; k < 30 && Main_Loop(); k++) {
            }
            break;
        }
    }
    printf("%s: %d frames\n", sc->name, frames);
    Checks(sc->name);
    fflush(stdout);
    return s_fails != 0;
}

int main(int argc, char **argv)
{
    const char *data = getenv("HWVX_TEST_GC_DATA");
    char found[1024], out[1024];
    size_t i;
    int fails = 0, ran = 0;

    s_verbose = getenv("HWVX_TEST_VERBOSE") != NULL;
    if (data == NULL) {
        snprintf(found, sizeof(found), "%s/orig/gc/files", HWVX_SOURCE_DIR);
        for (i = 0; i < 3; i++) {
            FILE *f;
            char probe[1100];

            snprintf(probe, sizeof(probe), "%s/HW_Cars.xgc", found);
            if ((f = fopen(probe, "rb")) != NULL) {
                fclose(f);
                data = found;
                break;
            }
            /* a worktree sits deeper: HWVX-SDL/<wt>, the superproject's orig/ */
            snprintf(probe, sizeof(probe), "%s", found);
            snprintf(found, sizeof(found), "%.*s/../orig/gc/files", (int)(strlen(probe) - strlen("/orig/gc/files")), probe);
        }
    }
    if (data == NULL) {
        printf("no GameCube data (HWVX_TEST_GC_DATA, or orig/gc/files by the sources): skipped\n");
        return 77;
    }
    snprintf(out, sizeof(out), "%s", "content-out");
    SDL_CreateDirectory(out);
    if (SDL_GetCurrentDirectory() != NULL) {
        char *cwd = SDL_GetCurrentDirectory();

        snprintf(out, sizeof(out), "%scontent-out", cwd);
        SDL_free(cwd);
    }
    if (argc > 2 && strcmp(argv[1], "--one") == 0) {
        for (i = 0; i < SDL_arraysize(s_scenarios); i++) {
            if (strcmp(s_scenarios[i].name, argv[2]) == 0) {
                return RunOne(&s_scenarios[i], data, out);
            }
        }
        printf("no scenario %s\n", argv[2]);
        return 1;
    }
    /* each scenario a fresh game: this program again */
    for (i = 0; i < SDL_arraysize(s_scenarios); i++) {
        const char *args[] = { argv[0], "--one", s_scenarios[i].name, NULL };
        SDL_Process *p;
        int code = -1;

        if (argc > 1 && strcmp(argv[1], s_scenarios[i].name) != 0) {
            continue;
        }
        printf("== %s\n", s_scenarios[i].name);
        fflush(stdout);
        p = SDL_CreateProcess(args, false);
        if (p == NULL || !SDL_WaitProcess(p, true, &code)) {
            printf("  FAIL can't run %s: %s\n", s_scenarios[i].name, SDL_GetError());
            fails++;
            continue;
        }
        SDL_DestroyProcess(p);
        if (code != 0) {
            printf("  FAIL scenario %s (exit %d)\n", s_scenarios[i].name, code);
            fails++;
        }
        ran++;
    }
    printf("%d scenarios, %d failed\n", ran, fails);
    return fails != 0;
}
