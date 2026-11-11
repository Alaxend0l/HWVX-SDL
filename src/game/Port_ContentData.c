/* The content registry's side that reads the game data (Port_Content.h).
 * Port-only file.
 *
 * Where the game learns its counts: it doesn't. Every count is the save
 * struct's array length (GameSettings_Get/SetCodeVariable test arg < 0x21,
 * 0x39, 0xC) and the data's own ranges agree with it by construction:
 *
 *   cars    HW_Cars (the MCP's carsPack, also the worlds' vehiclePackList):
 *           one type-0 pack per car, ids 0-32 on every release (the TOC also
 *           holds the type-8 shared pack). The car select's range is its
 *           carousel variables' max (32), the car each window shows the
 *           variable itself, the pack loaded `FileStreaming_LoadDataPack(
 *           carsPack, 0, id)`. So the count here is the TOC's: the highest
 *           car pack id + 1.
 *   tracks  numbers the interface scripts set and test (code variable 0x65
 *           with constant arguments in HW_IPack and the worlds), laid out in
 *           families (missions 5-18, challenges 39-56, races 33-34,
 *           battles 23-26, ...); the MCP's progression scripts pick the
 *           worlds but number nothing. No count to read: the array's.
 *   items   HW_Items packs 0-11 (the wildcards the item select offers;
 *           16-79 are the AI's and the worlds' own), numbered by the item
 *           select's list. The array's count too.
 *
 * Past those, entries are registered by id: the mod hook (a project's
 * ids.lock, not built yet) and HWVX_TEST_EXTRA_CARS. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Interface.h"
#include "Port_Content.h"
#include "Port_Swap.h"

static char s_carsArchive[0x40] = "HW_Cars";
static s8 s_testUnlockDone;

static int SameArchive(const char *a, const char *b) {
    const char *p;

    for (p = a; a != NULL && *p; p++) {   /* the name, without folders */
        if (*p == '/' || *p == '\\') {
            a = p + 1;
        }
    }
    for (p = b; b != NULL && *p; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return a != NULL && b != NULL && SDL_strcasecmp(a, b) == 0;
}

/* The car packs in archive `name`'s TOC: the highest type-0 id + 1, 0 if
 * it can't be read. Read beside the game's own file handling (DVDOpen, as
 * Port_Menu.c's ProgressionHasWorlds does), so the game's heap and file
 * state are as they would be without it. */
static u32 CarsInToc(const char *name) {
    char path[0x80], *c;
    DVDFileInfo fi;
    u32 buf[0x800 / 4], i, n, cars = 0;
    s32 got;

    SDL_snprintf(path, sizeof(path), "%s.xgc", name);   /* as DataPack_LoadFile names it */
    for (c = path; *c != 0; c++) {
        *c = *c == '\\' ? '/' : (char)tolower((u8)*c);
    }
    if (!DVDOpen(path, &fi)) {
        return 0;
    }
    SDL_memset(buf, 0, sizeof(buf));
    got = DVDReadPrio(&fi, buf, sizeof(buf), 0, 2);
    DVDClose(&fi);
    if (got < 0x10) {
        return 0;
    }
    PortData_TOC((s32)fi.format, buf, SDL_arraysize(buf));
    n = buf[3];
    if (buf[0] != 0x020100A0 || buf[1] != 0x040100AE || 4 + n * (sizeof(FileTOCEntry) / 4) > SDL_arraysize(buf)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        const FileTOCEntry *e = (const FileTOCEntry *)&buf[4 + i * (sizeof(FileTOCEntry) / 4)];

        if (e->type == 0 && e->id + 1 > cars) {
            cars = e->id + 1;
        }
    }
    return cars;
}

/* The mod hook: a project's ids.lock (§10.5) gives its content numbers past
 * the base entries, Port_ContentRegister(kind, "uid://...", -1, def) each,
 * in the lock file's order so a save's numbers never shuffle. Not built yet:
 * there is no mod loading. */
static void LoadIdsLock(void) {
}

/* HWVX_TEST_EXTRA_CARS=N: N extra cars, each loading the pack of a base car
 * (3, 4, ...: unlocked in a new game) under its own number and id, locked
 * in a new game. For testing cars past the retail 33 end to end. */
static void TestExtraCars(void) {
    const char *e = SDL_getenv("HWVX_TEST_EXTRA_CARS");
    u32 base = Port_ContentCount(PORT_CONTENT_CAR);
    int n, k;

    if (e == NULL || (n = SDL_atoi(e)) <= 0) {
        return;
    }
    for (k = 0; k < n && k < 64; k++) {
        char id[64];
        s32 alias = (s32)((3 + k) % base), i;

        SDL_snprintf(id, sizeof(id), "uid://hwvx-test-extra-car-%d", k);
        i = Port_ContentRegister(PORT_CONTENT_CAR, id, alias, 0);
        Port_Log("content: test: car %d is %s, the pack of car %d", (int)i, id, (int)alias);
    }
}

void Port_ContentInit(void) {
    static s8 done;
    MCP *mcp;
    u32 cars;

    if (done) {
        return;
    }
    done = 1;
    mcp = GameSettings_GetMCP();
    if (mcp != NULL && GETP(mcp->carsPack) != NULL) {
        SDL_strlcpy(s_carsArchive, GETP(mcp->carsPack), sizeof(s_carsArchive));
    }
    cars = CarsInToc(s_carsArchive);
    if (cars == 0) {
        Port_Log("content: %s's TOC can't be read; %u cars", s_carsArchive,
                 (unsigned)Port_ContentRetailCount(PORT_CONTENT_CAR));
    }
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, cars);
    Port_ContentSetBaseCount(PORT_CONTENT_TRACK, 0);
    Port_ContentSetBaseCount(PORT_CONTENT_ITEM, 0);
    LoadIdsLock();
    TestExtraCars();
    Port_Log("content: %u cars (%u in %s), %u tracks, %u items", (unsigned)Port_ContentCount(PORT_CONTENT_CAR),
             (unsigned)cars, s_carsArchive, (unsigned)Port_ContentCount(PORT_CONTENT_TRACK),
             (unsigned)Port_ContentCount(PORT_CONTENT_ITEM));
}

/* ---- loading an entry's data --------------------------------------------- */

u32 Port_ContentPackId(const char *name, u32 type, u32 id) {
    s32 data;

    if (type != 0 || id < Port_ContentRetailCount(PORT_CONTENT_CAR) || !SameArchive(name, s_carsArchive)) {
        return id;
    }
    data = Port_ContentDataOf(PORT_CONTENT_CAR, id);
    return data >= 0 ? (u32)data : id;
}

/* A pack's scripts (DataPack.base), as DataPack_GetScript reads them. */
typedef struct Script {
    PTR32(u8) script;
    u32 id;
} Script;
typedef struct Scripts {
    u32 count;
    PTR32(Script) entries;
} Scripts;

void Port_ContentFixScripts(void *dataPack, const char *name, u32 type, u32 id) {
    DataPack *pack = dataPack;
    Scripts *table;
    u32 src = Port_ContentPackId(name, type, id), i;

    if (src == id || pack == NULL || (table = (Scripts *)GETP(pack->base)) == NULL) {
        return;
    }
    for (i = 0; i < table->count; i++) {
        if (GETP(table->entries)[i].id == src) {
            GETP(table->entries)[i].id = id;
        }
    }
}

/* ---- the menus' ranges ------------------------------------------------- */

/* Per kind: the code variable that tests an entry's unlock, and the one that
 * holds a player's choice (-1: none). */
static const s32 s_unlockVar[PORT_CONTENT_KINDS] = { 0x64, 0x65, 0x66 };
static const s32 s_choiceVar[PORT_CONTENT_KINDS] = { 0xCB, -1, 0xCC };

/* Does an object of h test the kind's unlock with a variable argument (the
 * car select's padlocks: the variable is the car's number)? */
static int TestsUnlockByVar(const InterfaceHeader *h, s32 code) {
    const IfaceObjectDef *objs = GETP(h->objects);
    u32 i, j;

    for (i = 0; objs != NULL && i < h->numObjects; i++) {
        const IfaceConditionList *cl = GETP(objs[i].conditions);

        for (j = 0; cl != NULL && j < cl->count; j++) {
            const IfaceCondition *c = &GETP(cl->conds)[j];
            const IfaceCodeVar *cv = c->type == 2 ? GETP(c->data) : NULL;

            if (cv != NULL && (s32)cv->id == code && GETP(cv->arg) != NULL && GETP(cv->arg)->isVar) {
                return 1;
            }
        }
    }
    return 0;
}

/* The unlock the test switch asks for (HWVX_TEST_UNLOCK_CARS=33,34), once,
 * as the first car select opens: through the game's own code variable, as
 * a race result would unlock a car (and so autosaved). */
static void TestUnlock(void) {
    const char *e = SDL_getenv("HWVX_TEST_UNLOCK_CARS");

    if (s_testUnlockDone || e == NULL) {
        return;
    }
    s_testUnlockDone = 1;
    while (*e != 0) {
        char *end;
        long n = SDL_strtol(e, &end, 10);

        if (end == e) {
            break;
        }
        Port_Log("content: test: unlocking car %ld", n);
        GameSettings_SetCodeVariable(0x64, (u32)n, 1);
        e = *end == ',' ? end + 1 : end;
    }
}

/* Variables ranging over the retail entries (min 0, max retail - 1) that
 * number them, widened to the count: in a screen whose padlocks test the
 * kind's unlock by variable (the car select's carousel), and wherever the
 * variable reads a player's choice (the results screens' car names). With
 * the retail counts nothing changes. The pack's own definitions are changed
 * in place: a later load of the pack is a fresh copy. */
void Port_ContentPatchInterface(InterfaceHeader *h) {
    IfaceVarList *vl;
    int kind;
    u32 i;

    if (h == NULL || (vl = GETP(h->vars)) == NULL) {
        return;
    }
    for (kind = 0; kind < PORT_CONTENT_KINDS; kind++) {
        u32 count = Port_ContentCount(kind), retail = Port_ContentRetailCount(kind);
        int indexed = TestsUnlockByVar(h, s_unlockVar[kind]);

        if (kind == PORT_CONTENT_CAR && indexed) {
            TestUnlock();
        }
        if (count == retail) {
            continue;
        }
        for (i = 0; i < vl->count; i++) {
            IfaceVar *v = &GETP(vl->vars)[i];
            IfaceCodeVar *cv = v->type == IFACE_VAR_CODE ? GETP(v->code) : NULL;

            if (v->min != 0 || v->max != (s32)retail - 1) {
                continue;
            }
            if (indexed || (cv != NULL && s_choiceVar[kind] >= 0 && (s32)cv->id == s_choiceVar[kind])) {
                v->max = (s32)count - 1;
                Port_Log("content: interface %u: variable %u ranges over %u %s", h->id, (unsigned)i, (unsigned)count,
                         kind == PORT_CONTENT_CAR ? "cars" : kind == PORT_CONTENT_TRACK ? "tracks" : "items");
            }
        }
    }
}
