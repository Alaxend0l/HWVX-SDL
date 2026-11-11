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
 *           families (the world selects' shared 57-name list: Joyride 0-4,
 *           missions 5-18, battles 19-26, drag races 29-34, challenges
 *           39-56); the MCP's progression scripts pick the worlds but
 *           number nothing. No count to read: the array's. A new track is
 *           a progression level and a world archive first (data, §10.5.1).
 *   items   HW_Items packs 0-11 (the wildcards the item select offers;
 *           16-79 are the AI's and the worlds' own), numbered by the item
 *           select's list. The array's count too.
 *
 * Past those, entries are registered by id: a project's ids.lock
 * (Port_ContentLoadLock) and the test switches HWVX_TEST_EXTRA_CARS and
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
    u32 head[4], *buf, i, n, size;
    s32 got;

    SDL_snprintf(path, sizeof(path), "%s.xgc", a->name);   /* as DataPack_LoadFile names it */
    for (c = path; *c != 0; c++) {
        *c = *c == '\\' ? '/' : (char)tolower((u8)*c);
    }
    if (!DVDOpen(path, &fi)) {
        return 0;
    }
    got = DVDReadPrio(&fi, head, sizeof(head), 0, 2);
    PortData_TOC((s32)fi.format, head, 4);
    n = head[3];
    if (got < 0x10 || head[0] != 0x020100A0 || head[1] != 0x040100AE || n > 0x10000) {
        DVDClose(&fi);
        return 0;
    }
    /* the whole TOC, however many packs (DataPack_LoadFile reads it whole too) */
    size = 0x10 + n * (u32)sizeof(FileTOCEntry);
    buf = SDL_calloc(1, (size + 31) & ~31u);
    got = buf != NULL ? DVDReadPrio(&fi, buf, (s32)((size + 31) & ~31u), 0, 2) : -1;
    DVDClose(&fi);
    if (got < (s32)size) {
        SDL_free(buf);
        return 0;
    }
    PortData_TOC((s32)fi.format, buf, size / 4);
    for (i = 0; i < n; i++) {
        const FileTOCEntry *e = (const FileTOCEntry *)&buf[4 + i * (sizeof(FileTOCEntry) / 4)];

        if (e->type == a->type && e->id < MAX_TOC_ID) {
            a->has[e->id / 8] |= (u8)(1u << (e->id % 8));
            if (e->id + 1 > a->maxId) {
                a->maxId = e->id + 1;
            }
        }
    }
    SDL_free(buf);
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

/* HWVX_TEST_EXTRA_TRACKS=N: N extra tracks, each a level of its own after
 * its mode's, loading the world of a base track: the first of each family in
 * turn (Challenge 1, Joyride's first world, the first battle, drag race and
 * mission: tracks 39, 0, 19, 29, 5), or the ones listed
 * (HWVX_TEST_EXTRA_TRACKS=alias=40,41,6). Locked in a new game
 * (HWVX_TEST_UNLOCK_TRACKS=57,58), named after the alias with " (test)";
 * HWVX_TEST_EXTRA_TRACK_THUMB a texture link for their pictures,
 * HWVX_TEST_EXTRA_TRACK_WORLD another world archive for them to load. */
static void TestExtraTracks(void) {
    static const s32 first[] = { 39, 0, 19, 29, 5 };
    const char *e = SDL_getenv("HWVX_TEST_EXTRA_TRACKS"), *list = NULL;
    u32 base = Port_ContentCount(PORT_CONTENT_TRACK);
    int n, k;

    if (e == NULL) {
        return;
    }
    if (SDL_strncmp(e, "alias=", 6) == 0) {
        list = e + 6;
        n = 64;
    } else if ((n = SDL_atoi(e)) <= 0) {
        return;
    }
    for (k = 0; k < n && k < 64; k++) {
        PortContentEntry entry;
        char id[64];
        s32 alias = first[k % SDL_arraysize(first)] + k / (s32)SDL_arraysize(first), i;

        if (list != NULL) {
            char *end;

            alias = (s32)SDL_strtol(list, &end, 10);
            if (end == list) {
                break;
            }
            list = *end == ',' ? end + 1 : end;
        }
        if (alias < 0 || (u32)alias >= base) {
            Port_Log("content: test: no track %d to alias", (int)alias);
            continue;
        }
        SDL_snprintf(id, sizeof(id), "uid://hwvx-test-extra-track-%d", k);
        SDL_zero(entry);
        entry.number = -1;
        entry.id = id;
        entry.alias = alias;
        entry.suffix = " (test)";
        entry.thumbnail = SDL_getenv("HWVX_TEST_EXTRA_TRACK_THUMB");
        entry.world = SDL_getenv("HWVX_TEST_EXTRA_TRACK_WORLD");
        i = Port_ContentRegisterEntry(PORT_CONTENT_TRACK, &entry);
        Port_Log("content: test: track %d is %s, the level of track %d", (int)i, id, (int)alias);
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
    TestExtraTracks();
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

/* Which registered entries a list gets, and where: `n` of them from list
 * index `first` on, entry k the registry's number[k] (NULL: first + k, the
 * cars and items, whose lists are indexed by number); an alias's own entry
 * in the list is at its number - aliasBase (a world select's pictures are
 * indexed by level: the alias's number less its family's first). */
typedef struct ExtMap {
    int kind;
    u32 first;
    u32 n;
    const u32 *number;
    s32 aliasBase;
} ExtMap;

enum { EXT_PICTURES, EXT_NAMES, EXT_PARAGRAPHS, EXT_PICTURES_ALIAS /* never the thumbnail */ };

typedef struct ExtList {
    int what, kind;
    u32 first, n;
    u32 *number;        /* [n] */
    u32 origCount;
    u32 *orig;          /* the pack's: texture ids, or string table handles */
    u32 count;
    u32 *items;         /* what the pack's list points at instead */
    char *strings;      /* names: the registered entries' strings */
    u32 *slots;         /* their string tables, a slot each and a NULL */
} ExtList;

/* As many as the screens need (a few per select screen and load address of
 * the menu pack), each kept for the run. */
static ExtList **s_ext;
static u32 s_numExt, s_capExt;
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
        if (s_ext[i]->items == items) {
            return 1;
        }
    }
    return 0;
}

static u32 NumberOf(const ExtMap *m, u32 k) {
    return m->number != NULL ? m->number[k] : m->first + k;
}

static void FreeExt(ExtList *x) {
    if (x != NULL) {
        SDL_free(x->number);
        SDL_free(x->orig);
        SDL_free(x->items);
        SDL_free(x->slots);
        SDL_free(x->strings);
        SDL_free(x);
    }
}

/* The list `orig` (origCount long) with the map's entries in it: as long
 * as it has to be. NULL if nothing changes, or no memory. */
static const ExtList *Extend(const ExtMap *m, int what, const u32 *orig, u32 origCount) {
    u32 count = m->first + m->n > origCount ? m->first + m->n : origCount, i, j, k, pass, size;
    ExtList *x;
    char *str;

    if (m->n == 0 || origCount == 0 || orig == NULL || IsExt(orig)) {
        return NULL;
    }
    for (i = 0; i < s_numExt; i++) {
        x = s_ext[i];
        if (x->what != what || x->kind != m->kind || x->first != m->first || x->n != m->n ||
            x->origCount != origCount || SDL_memcmp(x->orig, orig, origCount * sizeof(u32)) != 0) {
            continue;
        }
        for (k = 0; k < m->n && x->number[k] == NumberOf(m, k); k++) {
        }
        if (k == m->n) {
            return x;
        }
    }
    if (s_numExt == s_capExt) {
        ExtList **p = SDL_realloc(s_ext, (s_capExt + 16) * sizeof(*p));

        if (p == NULL) {
            return NULL;
        }
        s_ext = p;
        s_capExt += 16;
    }
    if ((x = SDL_calloc(1, sizeof(*x))) == NULL) {
        return NULL;
    }
    x->what = what;
    x->kind = m->kind;
    x->first = m->first;
    x->n = m->n;
    x->origCount = origCount;
    x->count = count;
    x->number = SDL_malloc(m->n * sizeof(u32));
    x->orig = SDL_malloc(origCount * sizeof(u32));
    x->items = SDL_malloc(count * sizeof(u32));
    if (what == EXT_NAMES) {
        x->slots = SDL_calloc(m->n * 2, sizeof(u32));
    }
    if (x->number == NULL || x->orig == NULL || x->items == NULL || (what == EXT_NAMES && x->slots == NULL)) {
        FreeExt(x);
        return NULL;
    }
    for (k = 0; k < m->n; k++) {
        x->number[k] = NumberOf(m, k);
    }
    SDL_memcpy(x->orig, orig, origCount * sizeof(u32));
    SDL_memcpy(x->items, orig, origCount * sizeof(u32));
    for (i = origCount; i < count; i++) {
        x->items[i] = orig[origCount - 1];
    }
    /* names: the strings first, so the tables can point into one block */
    for (pass = 0; pass < 2 && what == EXT_NAMES; pass++) {
        size = 0;
        for (k = 0; k < m->n; k++) {
            const char *name, *suffix, *thumb, *aliasName;
            u32 num = x->number[k];
            s32 alias = Port_ContentDataOf(m->kind, num) - m->aliasBase;
            int look = Port_ContentLook(m->kind, num, &name, &suffix, &thumb);

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
            if (pass == 1) {
                str = x->strings + size;
                SDL_snprintf(str, SDL_strlen(name) + (suffix != NULL ? SDL_strlen(suffix) : 0) + 1, "%s%s", name,
                             suffix != NULL ? suffix : "");
                j = k * 2;
                x->slots[j] = HND(str);
                x->slots[j + 1] = 0;
                x->items[m->first + k] = HND(&x->slots[j]);
            }
            size += (u32)(SDL_strlen(name) + (suffix != NULL ? SDL_strlen(suffix) : 0) + 1);
        }
        if (pass == 0 && (x->strings = SDL_malloc(size)) == NULL) {
            FreeExt(x);
            return NULL;
        }
    }
    for (k = 0; k < m->n && what != EXT_NAMES; k++) {
        const char *name, *suffix, *thumb = NULL;
        u32 num = x->number[k];
        s32 alias = Port_ContentDataOf(m->kind, num) - m->aliasBase, tex;

        i = m->first + k;
        if (what == EXT_PARAGRAPHS) {
            if (s_blankSlots[0] == 0) {
                s_blankSlots[0] = HND(s_blank);
            }
            x->items[i] = HND(s_blankSlots);
            continue;
        }
        if (Port_ContentLook(m->kind, num, &name, &suffix, &thumb) == 2) {
            x->items[i] = orig[origCount - 1];   /* reserved: the list's own last ("?") */
        } else if (what == EXT_PICTURES && (tex = TextureOfLink(thumb)) >= 0) {
            x->items[i] = (u32)tex;
        } else {
            if (thumb != NULL && what == EXT_PICTURES) {
                Port_Log("content: %u's thumbnail %s: no such texture loaded; the alias's picture", (unsigned)num, thumb);
            }
            x->items[i] = alias >= 0 && (u32)alias < origCount ? orig[alias] : orig[origCount - 1];
        }
    }
    s_ext[s_numExt++] = x;
    return x;
}

static int InVars(const u8 *vars, u32 numVars, u32 var) {
    return var < numVars && (vars[var / 8] >> (var % 8) & 1);
}

/* The lists of h numbered by a variable: picture lists by one of picVars
 * get `pics`' entries, name and paragraph lists by one of textVars
 * `texts`'. windows: only the pictures of objects shown on a condition (a
 * carousel's windows) take a thumbnail, the others the alias's picture.
 * 1 if any list changed. */
static int ExtendLists(InterfaceHeader *h, const ExtMap *pics, const u8 *picVars, const ExtMap *texts,
                       const u8 *textVars, u32 numVars, int windows) {
    IfaceObjectDef *objs = GETP(h->objects);
    const ExtList *x;
    u32 i, j, old;
    int changed = 0;

    for (i = 0; objs != NULL && i < h->numObjects; i++) {
        IfaceObjectDef *d = &objs[i];

        if (d->type == IFACE_DEF_IMAGE && GETP(d->sub) != NULL && ((IfaceImageDef *)GETP(d->sub))->type == 3) {
            IfaceTexAnim *ta = GETP(((IfaceImageDef *)GETP(d->sub))->anim);

            if (ta == NULL || !ta->fromVar || !InVars(picVars, numVars, ta->var) ||
                (x = Extend(pics, windows && GETP(d->conditions) == NULL ? EXT_PICTURES_ALIAS : EXT_PICTURES,
                            GETP(ta->anim.keys), ta->anim.numKeys)) == NULL) {
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

            if (l == NULL || !l->fromVar || !InVars(textVars, numVars, l->var) ||
                (x = Extend(texts, t->paragraph ? EXT_PARAGRAPHS : EXT_NAMES, GETP(l->texts), l->count)) == NULL) {
                continue;
            }
            SETP(l->texts, x->items);
            l->count = x->count;
            changed = 1;
        }
    }
    return changed;
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

            /* (a world select's padlocks, patched: PORT_CODEVAR_TRACK) */
            if (cv != NULL && ((s32)cv->id == code || (code == 0x65 && PORT_IS_TRACK_CODEVAR((s32)cv->id))) &&
                GETP(cv->arg) != NULL && GETP(cv->arg)->isVar) {
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
static void PatchTrackSelect(InterfaceHeader *h);

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
        if (kind == PORT_CONTENT_TRACK) {
            /* tracks aren't numbered in a row: a mode's world select lists its
             * family and the registered tracks of it (below) */
            Port_ContentLearnTracks(h);
            PatchTrackSelect(h);
            if (!indexed && TestsUnlockByVar(h, s_unlockVar[kind])) {
                TestUnlock(kind);   /* (only the patched select tests it by variable) */
            }
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
            ExtMap map = { kind, Port_ContentBaseCount(kind), count - Port_ContentBaseCount(kind), NULL, 0 };

            if (ExtendLists(h, &map, vars, &map, vars, vl->count < 256 ? vl->count : 256, 0)) {
                Port_Log("content: interface %u: the %s lists name and picture %u entries", h->id,
                         kind == PORT_CONTENT_CAR ? "car" : "item", (unsigned)count);
            }
        }
    }
}

/* ---- tracks -------------------------------------------------------------- */

/* A mode's world select is a carousel over two parallel sets of variables:
 * the level of the mode's progression script (0..levels-1: the pictures, and
 * what the A press's event starts: type 0xF, or 6 on the mission select,
 * with the script's number and the level variable) and the track number of
 * that level in the mode's family (first..last: the names and unlock
 * paragraphs, one list for every select, and the padlocks, code variable
 * 0x65 of the track variable). So the screens say which track numbers a
 * script's levels are, and that is read from them, not kept here: the
 * families differ by release.
 *
 * A registered track aliases a base one. It becomes a level of every script
 * whose family its alias is in, after the script's own: a copy of the
 * alias's level (loading screen, steps, win and lose sequences) with its own
 * world archive if it names one. The MCP's script is replaced by the longer
 * copy, so everything that reads a script's levels (the progression manager,
 * HWVX_START, online, the multi-player screens) has them. In the select its
 * level variable ranges over the longer script and its track variable over
 * first..last + extras; the entries past `last` aren't track numbers (a
 * family's next number is another family's), so the padlocks' code variable
 * becomes PORT_CODEVAR_TRACK + script, which maps the select's entry to the
 * track (Port_ContentTrackAt), and the screen gets its own name and
 * paragraph lists with the registered tracks after `last`. */

#define MAX_PROG 64

typedef struct Family {
    s8 known;
    s32 first, last;        /* the track numbers of the script's own levels */
    u32 levels;             /* the script's own */
    u32 numExtra;
    u32 *extra;             /* the registered tracks, in level order */
} Family;

static Family s_family[MAX_PROG];

typedef struct TrackSelect {
    u32 prog;
    u32 lvar, tvar;         /* the middle window's level and track variables */
} TrackSelect;

/* Is h a world select: a row whose A event starts a level of a script by a
 * variable, shown while the track of another variable is unlocked. */
static int FindTrackSelect(const InterfaceHeader *h, TrackSelect *ts) {
    const IfaceObjectDef *objs = GETP(h->objects);
    const IfaceVarList *vl = GETP(h->vars);
    u32 i, j;

    for (i = 0; objs != NULL && vl != NULL && i < h->numObjects; i++) {
        const IfaceRecord *r = GETP(objs[i].record);
        const IfaceConditionList *cl = GETP(objs[i].conditions);
        s32 prog = -1, lvar = -1, tvar = -1;

        for (j = 0; r != NULL && j < r->events.count && GETP(r->events.events) != NULL; j++) {
            const IfaceEventDef *d = GETP(GETP(r->events.events)[j].def);
            const PTR32(IfaceVarValue) *slots = d != NULL && (d->type == 0xF || d->type == 6) ? GETP(d->args) : NULL;
            const IfaceVarValue *a = slots != NULL ? GETP(slots[0]) : NULL, *b = slots != NULL ? GETP(slots[1]) : NULL;

            if (a != NULL && b != NULL && !a->isVar && b->isVar) {
                prog = a->value;
                lvar = b->value;
            }
        }
        for (j = 0; cl != NULL && j < cl->count && GETP(cl->conds) != NULL; j++) {
            const IfaceCondition *c = &GETP(cl->conds)[j];
            const IfaceCodeVar *cv = c->type == 2 ? GETP(c->data) : NULL;

            if (cv != NULL && ((s32)cv->id == 0x65 || PORT_IS_TRACK_CODEVAR((s32)cv->id)) && GETP(cv->arg) != NULL &&
                GETP(cv->arg)->isVar) {
                tvar = GETP(cv->arg)->value;
            }
        }
        if (prog >= 0 && prog < MAX_PROG && lvar >= 0 && tvar >= 0 && (u32)lvar < vl->count && (u32)tvar < vl->count &&
            vl->count <= 256) {
            ts->prog = (u32)prog;
            ts->lvar = (u32)lvar;
            ts->tvar = (u32)tvar;
            return 1;
        }
    }
    return 0;
}

static ProgScript *ScriptOf(u32 prog) {
    MCP *mcp = GameSettings_GetMCP();

    return mcp != NULL && prog < mcp->numScripts && GETP(mcp->scripts) != NULL ? GETP(GETP(mcp->scripts)[prog]) : NULL;
}

/* A select as the pack has it says its script's family; the registered
 * tracks of the family become the script's levels. */
static void Learn(const InterfaceHeader *h, const TrackSelect *ts) {
    Family *f = &s_family[ts->prog];
    const IfaceVar *lv = &GETP(GETP(h->vars)->vars)[ts->lvar], *tv = &GETP(GETP(h->vars)->vars)[ts->tvar];
    MCP *mcp = GameSettings_GetMCP();
    ProgScript *ps = ScriptOf(ts->prog), *ns;
    ProgLevel *levels;
    u32 base = Port_ContentBaseCount(PORT_CONTENT_TRACK), count = Port_ContentCount(PORT_CONTENT_TRACK), i, k;

    if (f->known || ps == NULL || ps->count <= 0 || lv->min != 0 || tv->max - tv->min != lv->max || tv->min < 0) {
        return;
    }
    f->first = tv->min;
    f->last = tv->max;
    f->levels = (u32)ps->count;
    if (lv->max != ps->count - 1) {
        /* not as the pack has it: a copy of another script's select that's
         * widened already (the multi-player selects the port builds from One
         * Player's): that script's family */
        for (i = 0; i < MAX_PROG; i++) {
            const Family *q = &s_family[i];

            if (q->known && q->first == tv->min && q->last + (s32)q->numExtra == tv->max &&
                (s32)(q->levels + q->numExtra) - 1 == lv->max && q->levels == (u32)ps->count) {
                f->last = q->last;
                break;
            }
        }
        if (i == MAX_PROG) {
            return;
        }
    }
    if ((u32)f->last >= base) {
        return;
    }
    f->known = 1;
    for (k = 0; k < 2; k++) {
        f->numExtra = 0;
        for (i = base; i < count; i++) {
            const char *name, *suffix, *thumb;
            s32 alias = Port_ContentDataOf(PORT_CONTENT_TRACK, i);

            if (Port_ContentLook(PORT_CONTENT_TRACK, i, &name, &suffix, &thumb) == 1 && alias >= f->first &&
                alias <= f->last) {
                if (k == 1) {
                    f->extra[f->numExtra] = i;
                }
                f->numExtra++;
            }
        }
        if (f->numExtra == 0 || (k == 0 && (f->extra = SDL_malloc(f->numExtra * sizeof(u32))) == NULL)) {
            f->numExtra = 0;
            return;
        }
    }
    levels = SDL_malloc((f->levels + f->numExtra) * sizeof(ProgLevel));
    ns = SDL_malloc(sizeof(ProgScript));
    if (levels == NULL || ns == NULL) {
        SDL_free(levels);
        SDL_free(ns);
        f->numExtra = 0;
        return;
    }
    SDL_memcpy(levels, GETP(ps->levels), f->levels * sizeof(ProgLevel));
    for (k = 0; k < f->numExtra; k++) {
        ProgLevel *l = &levels[f->levels + k];
        const char *world = Port_ContentWorld(PORT_CONTENT_TRACK, f->extra[k]);

        *l = levels[Port_ContentDataOf(PORT_CONTENT_TRACK, f->extra[k]) - f->first];
        if (world != NULL) {
            SETP(l->world, (char *)world);
        }
        Port_Log("content: progression %u: level %u is track %u (%s)", (unsigned)ts->prog, (unsigned)(f->levels + k),
                 (unsigned)f->extra[k], GETP(l->world) != NULL ? GETP(l->world) : "-");
    }
    ns->count = (s32)(f->levels + f->numExtra);
    SETP(ns->levels, levels);
    SETP(GETP(mcp->scripts)[ts->prog], ns);
    Port_Log("content: progression %u: tracks %d..%d, %u levels and %u registered", (unsigned)ts->prog, (int)f->first,
             (int)f->last, (unsigned)f->levels, (unsigned)f->numExtra);
}

void Port_ContentLearnTracks(const InterfaceHeader *root) {
    static const InterfaceHeader *all[512];
    u32 n = 0, k, i, j;
    TrackSelect ts;

    if (root == NULL || Port_ContentCount(PORT_CONTENT_TRACK) == Port_ContentBaseCount(PORT_CONTENT_TRACK)) {
        return;
    }
    /* every screen reachable through go-to events and child slots */
    all[n++] = root;
    for (k = 0; k < n; k++) {
        const InterfaceHeader *h = all[k], *t;
        const IfaceObjectDef *objs = GETP(h->objects);

        if (FindTrackSelect(h, &ts)) {
            Learn(h, &ts);
        }
        for (i = 0; i <= h->numObjects; i++) {
            const IfaceEventList *list = i == h->numObjects ? &h->events :
                                         objs != NULL && GETP(objs[i].record) != NULL ? &GETP(objs[i].record)->events : NULL;

            for (j = 0; j <= (list != NULL && GETP(list->events) != NULL ? list->count : 0); j++) {
                t = NULL;
                if (list != NULL && GETP(list->events) != NULL && j < list->count) {
                    const IfaceEventDef *d = GETP(GETP(list->events)[j].def);

                    if (d != NULL && (d->type == 1 || d->type == 0x20 || d->type == 0x1F)) {
                        t = (const InterfaceHeader *)GETP(d->target);
                    }
                } else if (i < h->numObjects && objs != NULL &&
                           (objs[i].type == IFACE_DEF_CHILD || objs[i].type == IFACE_DEF_CHILD_SYNC) &&
                           GETP(objs[i].child) != NULL) {
                    t = (const InterfaceHeader *)GETP(*GETP(objs[i].child));
                }
                if (t != NULL && n < SDL_arraysize(all)) {
                    u32 m;

                    for (m = 0; m < n && all[m] != t; m++) {
                    }
                    if (m == n) {
                        all[n++] = t;
                    }
                }
            }
        }
    }
}

u32 Port_ContentTrackAt(u32 prog, u32 local) {
    const Family *f = prog < MAX_PROG ? &s_family[prog] : NULL;

    if (f == NULL || !f->known || (s32)local <= f->last || local - (u32)f->last - 1 >= f->numExtra) {
        return local;
    }
    return f->extra[local - (u32)f->last - 1];
}

s32 Port_ContentTrackOfLevel(u32 prog, u32 level) {
    const Family *f = prog < MAX_PROG ? &s_family[prog] : NULL;

    return f != NULL && f->known && level >= f->levels && level - f->levels < f->numExtra ? (s32)f->extra[level - f->levels] : -1;
}

/* The padlocks' code variable for a select of `prog` whose entry is
 * variable `var`: kept for the run (a condition of any load of the pack
 * points at it). */
static IfaceCodeVar *TrackCodeVar(u32 prog, u16 var) {
    typedef struct Cv {
        IfaceCodeVar cv;
        IfaceVarValue arg;
        struct Cv *next;
    } Cv;
    static Cv *all;
    Cv *c;

    for (c = all; c != NULL; c = c->next) {
        if (c->cv.id == PORT_CODEVAR_TRACK + prog && c->arg.value == var) {
            return &c->cv;
        }
    }
    if ((c = SDL_calloc(1, sizeof(*c))) == NULL) {
        return NULL;
    }
    c->cv.id = PORT_CODEVAR_TRACK + prog;
    c->arg.isVar = 1;
    c->arg.value = var;
    SETP(c->cv.arg, &c->arg);
    c->next = all;
    all = c;
    return &c->cv;
}

/* Is v one of the windows' variables of `mid` (the same source and range;
 * `max` the pack's or the widened one)? */
static int WindowOf(const IfaceVar *v, const IfaceVar *mid, s32 max, s32 wide) {
    const IfaceCodeVar *a = GETP(v->code), *b = GETP(mid->code);

    return v->type == mid->type && (v->max == max || v->max == wide) &&
           (v->type != IFACE_VAR_CODE || (a != NULL && b != NULL && a->id == b->id));
}

/* What each variable numbers, from how the screen uses it: 1 levels (a
 * picture list, the event's level), 2 tracks (a name or paragraph list, a
 * padlock). */
static void TrackVarUse(const InterfaceHeader *h, const TrackSelect *ts, u8 *use) {
    const IfaceObjectDef *objs = GETP(h->objects);
    u32 i, j;

    SDL_memset(use, 0, 256);
    use[ts->lvar] |= 1;
    use[ts->tvar] |= 2;
    for (i = 0; objs != NULL && i < h->numObjects; i++) {
        const IfaceObjectDef *d = &objs[i];
        const IfaceImageDef *im = d->type == IFACE_DEF_IMAGE ? GETP(d->sub) : NULL;
        const IfaceTexAnim *ta = im != NULL && im->type == 3 ? GETP(im->anim) : NULL;
        const IfaceText *t = d->type == IFACE_DEF_TEXT ? GETP(d->sub) : NULL;
        const IfaceStringList *l = t != NULL && t->listMode == 1 ? GETP(t->list) : NULL;
        const IfaceConditionList *cl = GETP(d->conditions);

        if (ta != NULL && ta->fromVar && ta->var < 256) {
            use[ta->var] |= 1;
        }
        if (l != NULL && l->fromVar && l->var < 256) {
            use[l->var] |= 2;
        }
        for (j = 0; cl != NULL && j < cl->count && GETP(cl->conds) != NULL; j++) {
            const IfaceCondition *c = &GETP(cl->conds)[j];
            const IfaceCodeVar *cv = c->type == 2 ? GETP(c->data) : NULL;

            if (cv != NULL && ((s32)cv->id == 0x65 || PORT_IS_TRACK_CODEVAR((s32)cv->id)) && GETP(cv->arg) != NULL &&
                GETP(cv->arg)->isVar && GETP(cv->arg)->value < 256) {
                use[GETP(cv->arg)->value] |= 2;
            }
        }
    }
}

/* A window variable's start, `offset`, as the window's place from the
 * middle one's: the difference modulo the n entries that is nearest the
 * variable's place in the variable list (the windows' variables are in
 * window order; with fewer entries than windows the difference alone
 * doesn't say which side). */
static s32 PlaceOf(s32 offset, s32 midOffset, s32 n, s32 order) {
    s32 d = ((offset - midOffset) % n + n) % n, turns = order - d;

    turns = turns >= 0 ? (turns + n / 2) / n : -((-turns + n / 2) / n);
    return d + turns * n;
}

static void PatchTrackSelect(InterfaceHeader *h) {
    TrackSelect ts;
    const Family *f;
    IfaceVarList *vl;
    IfaceVar *vars, *lv, *tv;
    IfaceObjectDef *objs = GETP(h->objects);
    u8 use[256], lset[256 / 8], tset[256 / 8];
    u32 i, j, e;
    s32 lmid, tmid, wideL, wideT;
    ExtMap pics, texts;
    int widened = 0;

    if (!FindTrackSelect(h, &ts) || !(f = &s_family[ts.prog])->known || f->numExtra == 0) {
        return;
    }
    vl = GETP(h->vars);
    vars = GETP(vl->vars);
    lv = &vars[ts.lvar];
    tv = &vars[ts.tvar];
    e = f->numExtra;
    wideL = (s32)(f->levels + e) - 1;
    wideT = f->last + (s32)e;
    if ((lv->max != (s32)f->levels - 1 && lv->max != wideL) || (tv->max != f->last && tv->max != wideT)) {
        return;
    }
    TrackVarUse(h, &ts, use);
    SDL_zeroa(lset);
    SDL_zeroa(tset);
    lmid = lv->offset;
    tmid = tv->offset;
    for (i = 0; i < vl->count; i++) {
        IfaceVar *v = &vars[i];
        s32 place;

        if ((use[i] & 1) && v->min == lv->min && WindowOf(v, lv, (s32)f->levels - 1, wideL)) {
            lset[i / 8] |= (u8)(1u << (i % 8));
            if (v->max != wideL) {
                place = PlaceOf(v->offset, lmid, (s32)f->levels, (s32)i - (s32)ts.lvar);
                v->max = wideL;
                v->offset = v->min + ((lmid - v->min + place) % (wideL + 1) + (wideL + 1)) % (wideL + 1);
                widened = 1;
            }
        }
        /* (the track variable's range alone: the battle select's leftmost has min 29 for 19) */
        if ((use[i] & 2) && WindowOf(v, tv, f->last, wideT)) {
            tset[i / 8] |= (u8)(1u << (i % 8));
            if (v->max != wideT) {
                s32 n = (s32)(f->levels + e);

                place = PlaceOf(v->offset, tmid, (s32)f->levels, (s32)i - (s32)ts.tvar);
                v->max = wideT;
                v->offset = f->first + ((tmid - f->first + place) % n + n) % n;
                widened = 1;
            }
        }
    }
    /* the padlocks: the select's entry to its track */
    for (i = 0; objs != NULL && i < h->numObjects; i++) {
        IfaceConditionList *cl = GETP(objs[i].conditions);

        for (j = 0; cl != NULL && j < cl->count && GETP(cl->conds) != NULL; j++) {
            IfaceCondition *c = &GETP(cl->conds)[j];
            IfaceCodeVar *cv = c->type == 2 ? GETP(c->data) : NULL, *own;

            if (cv != NULL && (s32)cv->id == 0x65 && GETP(cv->arg) != NULL && GETP(cv->arg)->isVar &&
                InVars(tset, vl->count, GETP(cv->arg)->value) && (own = TrackCodeVar(ts.prog, GETP(cv->arg)->value)) != NULL) {
                SETP(c->data, own);
            }
        }
    }
    pics = (ExtMap){ PORT_CONTENT_TRACK, f->levels, e, f->extra, f->first };
    texts = (ExtMap){ PORT_CONTENT_TRACK, (u32)f->last + 1, e, f->extra, 0 };
    if (ExtendLists(h, &pics, lset, &texts, tset, vl->count, 1) || widened) {
        char line[256];

        line[0] = 0;
        for (i = 0; i < e && SDL_strlen(line) < sizeof(line) - 24; i++) {
            SDL_snprintf(line + SDL_strlen(line), sizeof(line) - SDL_strlen(line), " %u%s", (unsigned)f->extra[i],
                         Port_ContentUnlocked(PORT_CONTENT_TRACK, f->extra[i]) ? "" : " (locked)");
        }
        Port_Log("content: interface %u: progression %u's world select: levels 0..%d, tracks %d..%d and%s", h->id,
                 (unsigned)ts.prog, (int)wideL, (int)f->first, (int)f->last, line);
    }
}

int Port_ContentTrackSelectAt(InterfaceHeader *h, s32 level) {
    TrackSelect ts;
    IfaceVarList *vl;
    IfaceVar *vars;
    u8 use[256];
    u32 i;
    s32 lmax, tmax, tmin, delta, n;

    if (h == NULL || !FindTrackSelect(h, &ts)) {
        return 0;
    }
    Port_ContentLearnTracks(h);
    PatchTrackSelect(h);
    vl = GETP(h->vars);
    vars = GETP(vl->vars);
    lmax = vars[ts.lvar].max;
    tmax = vars[ts.tvar].max;
    tmin = tmax - lmax;
    n = lmax + 1;
    if (level < 0 || level > lmax) {
        return 1;
    }
    TrackVarUse(h, &ts, use);
    /* (a code variable's value is added to the offset: the select then
     * opens that many further on, as it does without this) */
    delta = level - vars[ts.lvar].offset;
    for (i = 0; i < vl->count; i++) {
        IfaceVar *v = &vars[i];

        if ((use[i] & 1) && v->min == 0 && WindowOf(v, &vars[ts.lvar], lmax, lmax)) {
            v->offset = ((v->offset + delta) % n + n) % n;
        } else if ((use[i] & 2) && WindowOf(v, &vars[ts.tvar], tmax, tmax)) {
            v->offset = tmin + ((v->offset - tmin + delta) % n + n) % n;
        }
    }
    return 1;
}
