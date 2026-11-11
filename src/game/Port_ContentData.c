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
 * ids.lock, not built yet) and the test switches HWVX_TEST_EXTRA_CARS and
 * HWVX_TEST_EXTRA_ITEMS (HWVX_TEST_UNLOCK_CARS / _ITEMS unlock some). An
 * extra car or item whose number its archive has no pack for loads the
 * pack of the entry it aliases (DataPack_LoadFile, Port_ContentPackId). */
#include <SDL3/SDL.h>
#include "common.h"
#include "Interface.h"
#include "Port_Content.h"
#include "Port_Swap.h"

/* The archives whose packs are numbered as entries are: a car is the type-0
 * pack of its number in HW_Cars (the MCP's carsPack; the worlds' own
 * vehiclePackList names it too), an item the type-2 pack in HW_Items (the
 * MCP's itemsPack, the worlds' itemPack). Which ids each TOC has: an entry
 * past the array whose number the TOC lacks loads the pack it aliases. */
#define MAX_TOC_ID 1024
typedef struct Archive {
    int kind;
    u32 type;                       /* the packs' TOC type */
    char name[0x40];
    u8 has[MAX_TOC_ID / 8];         /* TOC ids of that type */
    u32 maxId;                      /* the highest + 1, 0: none (or no TOC) */
} Archive;

static Archive s_archives[] = {
    { PORT_CONTENT_CAR, 0, "HW_Cars" },
    { PORT_CONTENT_ITEM, 2, "HW_Items" },
};

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

static int Has(const Archive *a, u32 id) {
    return id < MAX_TOC_ID && (a->has[id / 8] >> (id % 8) & 1);
}

/* An archive's TOC: the ids of its packs of the archive's type. Read
 * beside the game's own file handling (DVDOpen, as Port_Menu.c's
 * ProgressionHasWorlds does), so the game's heap and file state are as
 * they would be without it. 0 if it can't be read. */
static int ReadToc(Archive *a) {
    char path[0x80], *c;
    DVDFileInfo fi;
    u32 buf[0x800 / 4], i, n;
    s32 got;

    SDL_snprintf(path, sizeof(path), "%s.xgc", a->name);   /* as DataPack_LoadFile names it */
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

        if (e->type == a->type && e->id < MAX_TOC_ID) {
            a->has[e->id / 8] |= (u8)(1u << (e->id % 8));
            if (e->id + 1 > a->maxId) {
                a->maxId = e->id + 1;
            }
        }
    }
    return 1;
}

/* The mod hook: a project's ids.lock (§10.5) gives its content numbers past
 * the base entries, Port_ContentRegister(kind, "uid://...", -1, def) each,
 * in the lock file's order so a save's numbers never shuffle. Not built yet:
 * there is no mod loading. */
static void LoadIdsLock(void) {
}

/* HWVX_TEST_EXTRA_CARS=N, HWVX_TEST_EXTRA_ITEMS=N: N extra entries, each
 * loading the pack of a base one under its own number and id (cars: 3, 4,
 * ..., unlocked in a new game; items: 0, 1, ...), locked in a new game. For
 * testing content past the retail arrays end to end. */
static void TestExtra(int kind, const char *env, const char *what, s32 first) {
    const char *e = SDL_getenv(env);
    u32 base = Port_ContentCount(kind);
    int n, k;

    if (e == NULL || (n = SDL_atoi(e)) <= 0) {
        return;
    }
    for (k = 0; k < n && k < 64; k++) {
        char id[64];
        s32 alias = (s32)((first + k) % base), i;

        SDL_snprintf(id, sizeof(id), "uid://hwvx-test-extra-%s-%d", what, k);
        i = Port_ContentRegister(kind, id, alias, 0);
        Port_Log("content: test: %s %d is %s, the pack of %s %d", what, (int)i, id, what, (int)alias);
    }
}

void Port_ContentInit(void) {
    static s8 done;
    MCP *mcp;
    size_t i;
    u32 j;

    if (done) {
        return;
    }
    done = 1;
    mcp = GameSettings_GetMCP();
    if (mcp != NULL && GETP(mcp->carsPack) != NULL) {
        SDL_strlcpy(s_archives[0].name, GETP(mcp->carsPack), sizeof(s_archives[0].name));
    }
    if (mcp != NULL && GETP(mcp->itemsPack) != NULL) {
        SDL_strlcpy(s_archives[1].name, GETP(mcp->itemsPack), sizeof(s_archives[1].name));
    }
    for (i = 0; i < SDL_arraysize(s_archives); i++) {
        if (!ReadToc(&s_archives[i])) {
            Port_Log("content: %s's TOC can't be read", s_archives[i].name);
        }
    }
    /* the cars are numbered by their packs; the items' packs 16-79 are the
     * AI's and the worlds', so the item count stays the array's */
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, s_archives[0].maxId);
    Port_ContentSetBaseCount(PORT_CONTENT_TRACK, 0);
    Port_ContentSetBaseCount(PORT_CONTENT_ITEM, 0);
    LoadIdsLock();
    TestExtra(PORT_CONTENT_CAR, "HWVX_TEST_EXTRA_CARS", "car", 3);
    TestExtra(PORT_CONTENT_ITEM, "HWVX_TEST_EXTRA_ITEMS", "item", 0);
    for (i = 0; i < SDL_arraysize(s_archives); i++) {
        const Archive *a = &s_archives[i];

        for (j = Port_ContentRetailCount(a->kind); j < Port_ContentCount(a->kind); j++) {
            if (Has(a, j) && Port_ContentDataOf(a->kind, j) != (s32)j) {
                /* the item numbers 16-79: what the game loads for them is
                 * the archive's own pack (Port_ContentPackId) */
                Port_Log("content: %s has a pack %u of its own; entry %u loads that, not its own data", a->name,
                         (unsigned)j, (unsigned)j);
            }
        }
    }
    Port_Log("content: %u cars (%u in %s), %u tracks, %u items", (unsigned)Port_ContentCount(PORT_CONTENT_CAR),
             (unsigned)s_archives[0].maxId, s_archives[0].name, (unsigned)Port_ContentCount(PORT_CONTENT_TRACK),
             (unsigned)Port_ContentCount(PORT_CONTENT_ITEM));
}

/* ---- loading an entry's data --------------------------------------------- */

u32 Port_ContentPackId(const char *name, u32 type, u32 id) {
    size_t i;
    s32 data;

    for (i = 0; i < SDL_arraysize(s_archives); i++) {
        const Archive *a = &s_archives[i];

        if (a->type != type || id < Port_ContentRetailCount(a->kind) || Has(a, id) || !SameArchive(name, a->name)) {
            continue;
        }
        data = Port_ContentDataOf(a->kind, id);
        return data >= 0 ? (u32)data : id;
    }
    return id;
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

/* The unlocks the test switches ask for (HWVX_TEST_UNLOCK_CARS=33,34,
 * HWVX_TEST_UNLOCK_ITEMS=12), once, as the first select screen of the kind
 * opens: through the game's own code variable, as a race result would
 * unlock a car (and so autosaved). */
static void TestUnlock(int kind) {
    static s8 done[PORT_CONTENT_KINDS];
    const char *e = SDL_getenv(kind == PORT_CONTENT_CAR ? "HWVX_TEST_UNLOCK_CARS" :
                               kind == PORT_CONTENT_ITEM ? "HWVX_TEST_UNLOCK_ITEMS" : "HWVX_TEST_UNLOCK_TRACKS");

    if (done[kind] || e == NULL) {
        return;
    }
    done[kind] = 1;
    while (*e != 0) {
        char *end;
        long n = SDL_strtol(e, &end, 10);

        if (end == e) {
            break;
        }
        Port_Log("content: test: unlocking %s %ld", kind == PORT_CONTENT_CAR ? "car" : kind == PORT_CONTENT_ITEM ? "item" : "track", n);
        GameSettings_SetCodeVariable(s_unlockVar[kind], (u32)n, 1);
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

        if (indexed) {
            TestUnlock(kind);
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
