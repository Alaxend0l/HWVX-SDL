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
 * extra car whose number HW_Cars has no pack for loads the pack of the car
 * it aliases (DataPack_LoadFile, Port_ContentPackId).
 *
 * Item numbers and HW_Items pack ids: retail items 0-11 are packs 0-11, but
 * the archive's packs 16-79 are the AI's and the worlds' own (the world
 * scripts and spawn points load them by pack id). So an item number past
 * the base ones is never a pack id: the places that turn the player's item
 * (code variable 0xCC, the item select's list) into a pack go through
 * Port_ContentItemPack, the registry's mapping: an aliasing item loads its
 * alias's pack (as the release itself loads pack 0x4D for item 3 with a
 * cheat), an item with data of its own a pack numbered past the archive's
 * last (where a mod's build puts it). Registered items are numbered 12, 13,
 * ... with no gap, whatever HW_Items holds. Cars need no mapping: they're
 * numbered past HW_Cars' highest pack. */
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

/* The mod hook: a project's ids.lock (§10.5.1) gives its content numbers
 * past the base entries (Port_ContentLoadLock). HWVX_IDS_LOCK=<file>, else
 * ids.lock in the preferences folder. */
static void LoadIdsLock(void) {
    const char *env = SDL_getenv("HWVX_IDS_LOCK");
    char *pref = NULL, *path = NULL;
    int n;

    if (env != NULL && *env != 0) {
        path = SDL_strdup(env);
    } else if ((pref = Port_PrefPath()) != NULL) {
        SDL_asprintf(&path, "%sids.lock", pref);
    }
    if (path != NULL && (n = Port_ContentLoadLock(path)) > 0) {
        Port_Log("content: %d entries from %s", n, path);
    }
    SDL_free(path);
    SDL_free(pref);
}

/* HWVX_TEST_EXTRA_CARS=N, HWVX_TEST_EXTRA_ITEMS=N: N extra entries, each
 * loading the pack of a base one under its own number and id (cars: 3, 4,
 * ..., unlocked in a new game; items: 0, 1, ...), locked in a new game,
 * shown as the alias with " (test)" after its name. For testing content past
 * the retail arrays end to end. HWVX_TEST_EXTRA_CAR_THUMB / _ITEM_THUMB: a
 * texture link (base://HW_IPack/interface_0/texture/12) for their pictures. */
static void TestExtra(int kind, const char *env, const char *what, s32 first, const char *thumbEnv) {
    const char *e = SDL_getenv(env);
    u32 base = Port_ContentCount(kind);
    int n, k;
    PortContentEntry entry;

    if (e == NULL || (n = SDL_atoi(e)) <= 0) {
        return;
    }
    for (k = 0; k < n && k < 64; k++) {
        char id[64];
        s32 alias = (s32)((first + k) % base), i;

        SDL_snprintf(id, sizeof(id), "uid://hwvx-test-extra-%s-%d", what, k);
        SDL_zero(entry);
        entry.number = -1;
        entry.id = id;
        entry.alias = alias;
        entry.suffix = " (test)";
        entry.thumbnail = SDL_getenv(thumbEnv);
        i = Port_ContentRegisterEntry(kind, &entry);
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
    TestExtra(PORT_CONTENT_CAR, "HWVX_TEST_EXTRA_CARS", "car", 3, "HWVX_TEST_EXTRA_CAR_THUMB");
    TestExtra(PORT_CONTENT_ITEM, "HWVX_TEST_EXTRA_ITEMS", "item", 0, "HWVX_TEST_EXTRA_ITEM_THUMB");
    for (j = Port_ContentRetailCount(PORT_CONTENT_CAR); j < Port_ContentCount(PORT_CONTENT_CAR); j++) {
        if (Has(&s_archives[0], j) && Port_ContentDataOf(PORT_CONTENT_CAR, j) != (s32)j) {
            Port_Log("content: %s has a pack %u of its own; car %u loads that, not its own data", s_archives[0].name,
                     (unsigned)j, (unsigned)j);
        }
    }
    Port_Log("content: %u cars (%u in %s), %u tracks, %u items", (unsigned)Port_ContentCount(PORT_CONTENT_CAR),
             (unsigned)s_archives[0].maxId, s_archives[0].name, (unsigned)Port_ContentCount(PORT_CONTENT_TRACK),
             (unsigned)Port_ContentCount(PORT_CONTENT_ITEM));
}

/* ---- loading an entry's data --------------------------------------------- */

u32 Port_ContentItemPack(u32 n) {
    u32 base = Port_ContentBaseCount(PORT_CONTENT_ITEM);
    s32 data;

    if (n < base || n >= Port_ContentCount(PORT_CONTENT_ITEM)) {
        return n;
    }
    data = Port_ContentDataOf(PORT_CONTENT_ITEM, n);
    return data >= 0 ? (u32)data : s_archives[1].maxId + (n - base);
}

u32 Port_ContentPackId(const char *name, u32 type, u32 id) {
    size_t i;
    s32 data;

    for (i = 0; i < SDL_arraysize(s_archives); i++) {
        const Archive *a = &s_archives[i];

        /* items: numbered apart from their packs (Port_ContentItemPack) */
        if (a->kind != PORT_CONTENT_CAR || a->type != type || id < Port_ContentRetailCount(a->kind) || Has(a, id) || !SameArchive(name, a->name)) {
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

/* ---- the selects' lists: names and pictures ------------------------------ */

/* The car and item selects show an entry through lists indexed by its
 * number: a picture (an image whose texture key is the variable, 36 keys
 * for the cars on every release, 14 for the items), its name (a text list,
 * 36 / 12) and a paragraph (how to unlock it). Past a list's end the
 * interface shows its last entry, and the keys between the base count and
 * the list's end are the pack's own (a "?"). So for a screen whose variables
 * number a kind, each such list is replaced by a copy as long as the count,
 * the registered entries' own: the picture their thumbnail link names or
 * their alias's, the name theirs or the alias's with their suffix, a blank
 * paragraph. Copies are shared by content and kept for the run (a pack loaded
 * again has the same lists, at most a few distinct ones per release). */

#define MAX_EXT 48
typedef struct ExtList {
    u32 origCount;
    u32 *orig;          /* the pack's: texture ids, or string table handles */
    u32 count;
    u32 *items;         /* what the pack's list points at instead */
    char *strings;      /* names: the registered entries' strings */
    u32 *slots;         /* their string tables, a slot each and a NULL */
} ExtList;

static ExtList s_ext[MAX_EXT];
static u32 s_numExt;
static u32 s_blankSlots[2];
static char s_blank[] = " ";

static const char *const s_typeNames[] = { "car", "interface", "item", "link", "world", "collision",
                                           "world_texture", "geometry", "share", "audio", "music" };

/* A texture link, base://<archive>/<type>_<id>/texture/<n>, to the texture
 * id of a loaded pack; -1 if it names none. */
static s32 TextureOfLink(const char *link) {
    extern StreamingPack *g_StreamingDataPacks[11];
    extern u32 g_MaxStreamingDataPacks[11];
    char archive[0x40];
    const char *p, *slash;
    size_t len, n;
    u32 type, id, tex, i;
    char *end;

    if (link == NULL || SDL_strncmp(link, "base://", 7) != 0 || (slash = SDL_strchr(p = link + 7, '/')) == NULL ||
        (len = (size_t)(slash - p)) >= sizeof(archive)) {
        return -1;
    }
    SDL_memcpy(archive, p, len);
    archive[len] = 0;
    p = slash + 1;
    for (type = 0; type < SDL_arraysize(s_typeNames); type++) {
        n = SDL_strlen(s_typeNames[type]);
        if (SDL_strncmp(p, s_typeNames[type], n) == 0 && p[n] == '_' && p[n + 1] >= '0' && p[n + 1] <= '9') {
            id = (u32)SDL_strtoul(p + n + 1, &end, 10);
            if (SDL_strncmp(end, "/texture/", 9) != 0 || end[9] < '0' || end[9] > '9') {
                return -1;
            }
            tex = (u32)SDL_strtoul(end + 9, &end, 10);
            if (*end != 0 || g_StreamingDataPacks[type] == NULL) {
                return -1;
            }
            for (i = 0; i < g_MaxStreamingDataPacks[type]; i++) {
                const StreamingPack *sp = &g_StreamingDataPacks[type][i];

                if (sp->used && sp->loaded && sp->id == id && SameArchive(GETP(sp->name), archive) &&
                    tex < sp->pack.numTextures && GETP(sp->pack.textureIDs) != NULL) {
                    return GETP(sp->pack.textureIDs)[tex];
                }
            }
            return -1;
        }
    }
    return -1;
}

/* The pack's string table's first string. */
static const char *FirstString(u32 table) {
    char **t = (char **)PTR(table);

    return t != NULL ? SLOT_LOAD(char *, t) : NULL;
}

static int IsExt(const u32 *items) {
    u32 i;

    for (i = 0; i < s_numExt; i++) {
        if (s_ext[i].items == items) {
            return 1;
        }
    }
    return 0;
}

/* The list `orig` (origCount long) of a kind, as long as its count. what: 0
 * pictures, 1 names, 2 paragraphs. NULL if nothing changes, or no room. */
static const ExtList *Extend(int kind, int what, const u32 *orig, u32 origCount) {
    u32 base = Port_ContentBaseCount(kind), count = Port_ContentCount(kind), i, j, k, size;
    ExtList *x;
    char *str;

    if (base >= count || origCount == 0 || orig == NULL || IsExt(orig)) {
        return NULL;
    }
    for (i = 0; i < s_numExt; i++) {
        x = &s_ext[i];
        if (x->origCount == origCount && x->count == (count > origCount ? count : origCount) &&
            SDL_memcmp(x->orig, orig, origCount * sizeof(u32)) == 0 && (x->strings != NULL) == (what == 1)) {
            return x;
        }
    }
    if (s_numExt >= MAX_EXT) {
        Port_Log("content: more than %d lists to extend; the rest show their last entry", MAX_EXT);
        return NULL;
    }
    x = &s_ext[s_numExt];
    SDL_zerop(x);
    x->origCount = origCount;
    x->count = count > origCount ? count : origCount;
    x->orig = SDL_malloc(origCount * sizeof(u32));
    x->items = SDL_malloc(x->count * sizeof(u32));
    if (what == 1) {
        x->slots = SDL_calloc((count - base) * 2, sizeof(u32));
    }
    if (x->orig == NULL || x->items == NULL || (what == 1 && x->slots == NULL)) {
        SDL_free(x->orig);
        SDL_free(x->items);
        SDL_free(x->slots);
        return NULL;
    }
    SDL_memcpy(x->orig, orig, origCount * sizeof(u32));
    SDL_memcpy(x->items, orig, origCount * sizeof(u32));
    for (i = origCount; i < x->count; i++) {
        x->items[i] = orig[origCount - 1];
    }
    /* names: the strings first, so the tables can point into one block */
    for (k = 0; k < 2 && what == 1; k++) {
        size = 0;
        for (i = base; i < count; i++) {
            const char *name, *suffix, *thumb, *aliasName;
            s32 alias = Port_ContentDataOf(kind, i);

            int look = Port_ContentLook(kind, i, &name, &suffix, &thumb);

            if (look == 0) {
                name = suffix = NULL;
            } else if (look == 2) {
                name = " ";     /* reserved */
            }
            aliasName = alias >= 0 && (u32)alias < origCount ? FirstString(orig[alias]) : NULL;
            if (name == NULL) {
                name = aliasName != NULL ? aliasName : "";
            } else {
                suffix = NULL;
            }
            if (k == 1) {
                str = x->strings + size;
                SDL_snprintf(str, SDL_strlen(name) + (suffix != NULL ? SDL_strlen(suffix) : 0) + 1, "%s%s", name,
                             suffix != NULL ? suffix : "");
                j = (i - base) * 2;
                x->slots[j] = HND(str);
                x->slots[j + 1] = 0;
                x->items[i] = HND(&x->slots[j]);
            }
            size += (u32)(SDL_strlen(name) + (suffix != NULL ? SDL_strlen(suffix) : 0) + 1);
        }
        if (k == 0 && (x->strings = SDL_malloc(size)) == NULL) {
            SDL_free(x->orig);
            SDL_free(x->items);
            SDL_free(x->slots);
            return NULL;
        }
    }
    for (i = base; i < count && what != 1; i++) {
        const char *name, *suffix, *thumb = NULL;
        s32 alias = Port_ContentDataOf(kind, i), tex;

        if (what == 2) {
            if (s_blankSlots[0] == 0) {
                s_blankSlots[0] = HND(s_blank);
            }
            x->items[i] = HND(s_blankSlots);
            continue;
        }
        if (Port_ContentLook(kind, i, &name, &suffix, &thumb) == 2) {
            x->items[i] = orig[origCount - 1];   /* reserved: the list's own last ("?") */
        } else if ((tex = TextureOfLink(thumb)) >= 0) {
            x->items[i] = (u32)tex;
        } else {
            if (thumb != NULL) {
                Port_Log("content: %u's thumbnail %s: no such texture loaded; the alias's picture", (unsigned)i, thumb);
            }
            x->items[i] = alias >= 0 && (u32)alias < origCount ? orig[alias] : orig[origCount - 1];
        }
    }
    s_numExt++;
    return x;
}

/* Lists in h numbered by a variable of `vars` (a bit per variable index). */
static void ExtendLists(InterfaceHeader *h, int kind, const u8 *vars, u32 numVars) {
    IfaceObjectDef *objs = GETP(h->objects);
    const ExtList *x;
    u32 i, j, old;
    int changed = 0;

    for (i = 0; objs != NULL && i < h->numObjects; i++) {
        IfaceObjectDef *d = &objs[i];

        if (d->type == IFACE_DEF_IMAGE && GETP(d->sub) != NULL && ((IfaceImageDef *)GETP(d->sub))->type == 3) {
            IfaceTexAnim *ta = GETP(((IfaceImageDef *)GETP(d->sub))->anim);

            if (ta == NULL || !ta->fromVar || ta->var >= numVars || !(vars[ta->var / 8] >> (ta->var % 8) & 1) ||
                (x = Extend(kind, 0, GETP(ta->anim.keys), ta->anim.numKeys)) == NULL) {
                continue;
            }
            /* every image showing this list, the ones playing it too (the
             * car select's sliding picture, which Port_Carousel indexes) */
            old = ta->anim.keys.h;
            for (j = 0; j < h->numObjects; j++) {
                IfaceImageDef *im = objs[j].type == IFACE_DEF_IMAGE ? GETP(objs[j].sub) : NULL;
                IfaceTexAnim *t = im != NULL && im->type == 3 ? GETP(im->anim) : NULL;

                if (t != NULL && t->anim.keys.h == old) {
                    SETP(t->anim.keys, x->items);
                    t->anim.numKeys = x->count;
                }
            }
            changed = 1;
        } else if (d->type == IFACE_DEF_TEXT && GETP(d->sub) != NULL && ((IfaceText *)GETP(d->sub))->listMode == 1) {
            IfaceText *t = GETP(d->sub);
            IfaceStringList *l = GETP(t->list);

            if (l == NULL || !l->fromVar || l->var >= numVars || !(vars[l->var / 8] >> (l->var % 8) & 1) ||
                (x = Extend(kind, t->paragraph ? 2 : 1, GETP(l->texts), l->count)) == NULL) {
                continue;
            }
            SETP(l->texts, x->items);
            l->count = x->count;
            changed = 1;
        }
    }
    if (changed) {
        Port_Log("content: interface %u: the %s lists name and picture %u entries", h->id,
                 kind == PORT_CONTENT_CAR ? "car" : kind == PORT_CONTENT_TRACK ? "track" : "item",
                 (unsigned)Port_ContentCount(kind));
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

/* A carousel's windows are constant variables the d-pad steps together,
 * wrapped to min..max, each starting at its window's entry (its offset): the
 * middle one's, the others the entries before and after it modulo the
 * retail count (the item select's left windows start at 10 and 11 for
 * item 0). Widened to the registry's count, they're rebased so the windows
 * stay neighbours modulo the new count (10, 11 would be 3 and 2 before 0
 * of 14): offset = middle + (offset - middle, as -retail/2..retail/2), the
 * middle being the variable the screen's name list shows. Done once, as
 * the variables are widened (marked by `wide`). */
static void Rebase(InterfaceHeader *h, const u8 *wide, u32 retail, u32 count) {
    const IfaceObjectDef *objs = GETP(h->objects);
    IfaceVarList *vl = GETP(h->vars);
    s32 mid = -1, rel;
    u32 i;

    for (i = 0; objs != NULL && i < h->numObjects && mid < 0; i++) {
        const IfaceText *t = objs[i].type == IFACE_DEF_TEXT ? GETP(objs[i].sub) : NULL;
        const IfaceStringList *l = t != NULL && t->listMode == 1 && !t->paragraph ? GETP(t->list) : NULL;

        if (l != NULL && l->fromVar && l->var < vl->count && l->var < 256 && (wide[l->var / 8] >> (l->var % 8) & 1)) {
            mid = l->var;
        }
    }
    if (mid < 0) {
        return;
    }
    for (i = 0; i < vl->count && i < 256; i++) {
        IfaceVar *v = &GETP(vl->vars)[i];

        if (!(wide[i / 8] >> (i % 8) & 1) || v->type != IFACE_VAR_CONST) {
            continue;
        }
        rel = ((v->offset - GETP(vl->vars)[mid].offset) % (s32)retail + (s32)retail) % (s32)retail;
        if (rel > (s32)retail / 2) {
            rel -= (s32)retail;
        }
        v->offset = ((GETP(vl->vars)[mid].offset + rel) % (s32)count + (s32)count) % (s32)count;
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
    u8 vars[256 / 8], wide[256 / 8];

    if (h == NULL || (vl = GETP(h->vars)) == NULL) {
        return;
    }
    for (kind = 0; kind < PORT_CONTENT_KINDS; kind++) {
        u32 count = Port_ContentCount(kind), retail = Port_ContentRetailCount(kind);
        int indexed = TestsUnlockByVar(h, s_unlockVar[kind]), any = 0;

        if (indexed) {
            TestUnlock(kind);
        }
        if (count == retail) {
            continue;
        }
        SDL_zeroa(vars);
        SDL_zeroa(wide);
        for (i = 0; i < vl->count && i < 256; i++) {
            IfaceVar *v = &GETP(vl->vars)[i];
            IfaceCodeVar *cv = v->type == IFACE_VAR_CODE ? GETP(v->code) : NULL;

            /* (max count - 1: the screen again, its pack still loaded) */
            if (v->min != 0 || (v->max != (s32)retail - 1 && v->max != (s32)count - 1)) {
                continue;
            }
            if (indexed || (cv != NULL && s_choiceVar[kind] >= 0 && (s32)cv->id == s_choiceVar[kind])) {
                vars[i / 8] |= (u8)(1u << (i % 8));
                any = 1;
                if (v->max != (s32)count - 1) {
                    v->max = (s32)count - 1;
                    wide[i / 8] |= (u8)(1u << (i % 8));
                    Port_Log("content: interface %u: variable %u ranges over %u %s", h->id, (unsigned)i,
                             (unsigned)count,
                             kind == PORT_CONTENT_CAR ? "cars" : kind == PORT_CONTENT_TRACK ? "tracks" : "items");
                }
            }
        }
        if (any && indexed) {
            Rebase(h, wide, retail, count);
        }
        if (any && count > Port_ContentBaseCount(kind)) {
            ExtendLists(h, kind, vars, vl->count < 256 ? vl->count : 256);
        }
    }
}
