/* The content registry (Port_Content.h): counts, unlock state and ids of
 * the cars, tracks and items. Port-only file. No game code here: the save
 * tests link it without the game.
 *
 * Retail content keeps its retail numbers on every release, so a base
 * entry's id is its kind's pattern with the number filled in (§8.1):
 *
 *   cars    base://HW_Cars/car_<n>     the car's pack in HW_Cars
 *   tracks  base://mcp/track/<n>       the MCP's track (event) number
 *   items   base://mcp/item/<n>        the MCP's item number
 *
 * Registered entries (mods, by ids.lock; the test switch) bring their own. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Content.h"
#include "Port_Saves.h"
#include "jsonc.h"
#include "net.h"

typedef struct Registered {
    char *id;
    s32 alias;          /* the base entry whose data it uses, -1: its own */
    s8 def;             /* in a new game */
    char *name;         /* what the selects show (NULL: the alias's name, then suffix) */
    char *suffix;
    char *thumbnail;    /* a texture link (NULL: the alias's picture) */
    char *world;        /* a track's own world archive (NULL: the alias's level's) */
    char *data;         /* the pack its data is, when not the alias's (proto://, Port_Proto.c) */
    s8 reserved;        /* a number kept for content that isn't there (ids.lock):
                         * no id to save by, locked, base entry 0's data */
} Registered;

typedef struct Kind {
    const char *pattern;    /* base:// id of number n: pattern then n */
    const char *section;    /* the save's member for it (Port_SaveIdSection) */
    u32 retail;             /* the save struct's array */
    u32 base;               /* numbered by the data, >= retail */
    u32 numReg;
    Registered *reg;
    s8 *live;               /* base + numReg, the unlock state */
    u32 liveSize;
    s8 *view;               /* the save struct's array (retail entries), or NULL */
    s8 *stash;              /* online: the live entries past the array */
} Kind;

static Kind s_kinds[PORT_CONTENT_KINDS] = {
    [PORT_CONTENT_CAR] = { "base://HW_Cars/car_", "cars", sizeof(((HotWheelsGameData *)0)->cars) },
    [PORT_CONTENT_TRACK] = { "base://mcp/track/", "tracks", sizeof(((HotWheelsGameData *)0)->tracks) },
    [PORT_CONTENT_ITEM] = { "base://mcp/item/", "items", sizeof(((HotWheelsGameData *)0)->items) },
};
static int s_liveSlot = -1;

_Static_assert(PORT_CONTENT_CAR == PORT_SAVE_IDS_CARS && PORT_CONTENT_TRACK == PORT_SAVE_IDS_TRACKS &&
               PORT_CONTENT_ITEM == PORT_SAVE_IDS_ITEMS, "content kinds are the save's id tables");

static Kind *KindOf(int kind) {
    return kind >= 0 && kind < PORT_CONTENT_KINDS ? &s_kinds[kind] : NULL;
}

static u32 BaseOf(const Kind *k) {
    return k->base > k->retail ? k->base : k->retail;
}

static u32 CountOf(const Kind *k) {
    return BaseOf(k) + k->numReg;
}

static int IsReserved(const Kind *k, u32 i) {
    u32 b = BaseOf(k);

    return i >= b && i - b < k->numReg && k->reg[i - b].reserved;
}

/* An entry's value in a new game: past the array, locked unless registered
 * otherwise. Retail entries get theirs from the game (GameSetting_Init). */
static s8 DefaultOf(const Kind *k, u32 i) {
    u32 b = BaseOf(k);
    return i >= b && i - b < k->numReg && !k->reg[i - b].reserved ? k->reg[i - b].def : 0;
}

/* live sized to the count; new entries their new-game value. */
static void Grow(Kind *k) {
    u32 n = CountOf(k), i;
    s8 *p;

    if (k->liveSize >= n) {
        return;
    }
    p = SDL_realloc(k->live, n);
    if (p == NULL) {
        Port_Log("content: out of memory");
        return;
    }
    for (i = k->liveSize; i < n; i++) {
        p[i] = i < k->retail && k->view != NULL ? k->view[i] : DefaultOf(k, i);
    }
    k->live = p;
    k->liveSize = n;
}

u32 Port_ContentCount(int kind) {
    Kind *k = KindOf(kind);
    return k != NULL ? CountOf(k) : 0;
}

u32 Port_ContentBaseCount(int kind) {
    Kind *k = KindOf(kind);
    return k != NULL ? BaseOf(k) : 0;
}

u32 Port_ContentRetailCount(int kind) {
    Kind *k = KindOf(kind);
    return k != NULL ? k->retail : 0;
}

s8 Port_ContentUnlocked(int kind, u32 i) {
    Kind *k = KindOf(kind);

    if (k == NULL || i >= CountOf(k)) {
        return 0;
    }
    Grow(k);
    if (i >= k->liveSize || IsReserved(k, i)) {
        return 0;
    }
    return k->live[i];
}

void Port_ContentSetUnlocked(int kind, u32 i, s8 v) {
    Kind *k = KindOf(kind);

    if (k == NULL || i >= CountOf(k) || IsReserved(k, i)) {
        return;
    }
    Grow(k);
    if (i < k->liveSize) {
        k->live[i] = v;
    }
    if (i < k->retail && k->view != NULL) {
        k->view[i] = v;
    }
}

/* ---- ids --------------------------------------------------------------- */

int Port_ContentIdOf(int kind, u32 i, char *buf, size_t size) {
    Kind *k = KindOf(kind);
    u32 b;

    if (k == NULL || i >= CountOf(k) || IsReserved(k, i)) {
        return 0;
    }
    b = BaseOf(k);
    if (i >= b) {
        SDL_strlcpy(buf, k->reg[i - b].id, size);
    } else {
        SDL_snprintf(buf, size, "%s%u", k->pattern, (unsigned)i);
    }
    return 1;
}

int Port_ContentIndexOf(int kind, const char *id, u32 *index) {
    Kind *k = KindOf(kind);
    size_t n;
    char *end;
    unsigned long v;
    u32 i;

    if (k == NULL || id == NULL) {
        return 0;
    }
    for (i = 0; i < k->numReg; i++) {
        if (!k->reg[i].reserved && SDL_strcmp(k->reg[i].id, id) == 0) {
            *index = BaseOf(k) + i;
            return 1;
        }
    }
    n = SDL_strlen(k->pattern);
    if (SDL_strncmp(id, k->pattern, n) != 0 || id[n] < '0' || id[n] > '9' || (id[n] == '0' && id[n + 1] != 0)) {
        return 0;
    }
    v = SDL_strtoul(id + n, &end, 10);
    if (*end != 0 || v >= BaseOf(k)) {
        return 0;
    }
    *index = (u32)v;
    return 1;
}

s32 Port_ContentDataOf(int kind, u32 i) {
    Kind *k = KindOf(kind);
    u32 b;

    if (k == NULL || i >= CountOf(k)) {
        return -1;
    }
    b = BaseOf(k);
    return i < b ? (s32)i : k->reg[i - b].reserved ? 0 : k->reg[i - b].alias;
}

int Port_ContentLook(int kind, u32 i, const char **name, const char **suffix, const char **thumbnail) {
    Kind *k = KindOf(kind);
    const Registered *r;
    u32 b;

    if (k == NULL || i >= CountOf(k) || i < (b = BaseOf(k))) {
        return 0;
    }
    r = &k->reg[i - b];
    if (r->reserved) {
        *name = *suffix = *thumbnail = NULL;
        return 2;
    }
    *name = r->name;
    *suffix = r->suffix;
    *thumbnail = r->thumbnail;
    return 1;
}

const char *Port_ContentDataLink(int kind, u32 i) {
    Kind *k = KindOf(kind);
    u32 b;

    if (k == NULL || i >= CountOf(k) || i < (b = BaseOf(k)) || k->reg[i - b].reserved) {
        return NULL;
    }
    return k->reg[i - b].data;
}

const char *Port_ContentWorld(int kind, u32 i) {
    Kind *k = KindOf(kind);
    u32 b;

    if (k == NULL || i >= CountOf(k) || i < (b = BaseOf(k)) || k->reg[i - b].reserved) {
        return NULL;
    }
    return k->reg[i - b].world;
}

/* ---- setting up -------------------------------------------------------- */

void Port_ContentSetBaseCount(int kind, u32 n) {
    Kind *k = KindOf(kind);

    if (k == NULL) {
        return;
    }
    if (k->numReg != 0) {
        Port_Log("content: %s: the count is set before anything is registered", k->section);
        return;
    }
    k->base = n > k->retail ? n : k->retail;
    Grow(k);
}

static char *Dup(const char *s) {
    return s != NULL ? SDL_strdup(s) : NULL;
}

s32 Port_ContentRegister(int kind, const char *id, s32 alias, s8 def) {
    PortContentEntry e;

    SDL_zero(e);
    e.number = -1;
    e.id = id;
    e.alias = alias;
    e.def = def;
    return Port_ContentRegisterEntry(kind, &e);
}

/* A new entry at the end (no checks); NULL if out of memory. */
static Registered *Append(Kind *k) {
    Registered *r = SDL_realloc(k->reg, (k->numReg + 1) * sizeof(Registered));

    if (r == NULL) {
        return NULL;
    }
    k->reg = r;
    r = &k->reg[k->numReg++];
    SDL_zerop(r);
    return r;
}

/* Up to number n (exclusive), reserved entries without an id. 0: no memory. */
static int FillTo(Kind *k, u32 n) {
    Registered *r;

    while (CountOf(k) < n) {
        if ((r = Append(k)) == NULL) {
            return 0;
        }
        r->reserved = 1;
        r->id = SDL_strdup("");
    }
    return 1;
}

s32 Port_ContentRegisterEntry(int kind, const PortContentEntry *e) {
    Kind *k = KindOf(kind);
    Registered *r;
    const char *id = e != NULL ? e->id : NULL;
    s32 alias = e != NULL ? e->alias : -1;
    u32 index;

    if (k == NULL || id == NULL || id[0] == 0) {
        return -1;
    }
    if (Port_ContentIndexOf(kind, id, &index)) {
        Port_Log("content: %s: %s is already number %u", k->section, id, (unsigned)index);
        return -1;
    }
    if (alias >= (s32)BaseOf(k)) {
        Port_Log("content: %s: %s: no base entry %d to use the data of", k->section, id, (int)alias);
        return -1;
    }
    if (e->number >= 0 && (u32)e->number < CountOf(k)) {
        Port_Log("content: %s: %s: number %d is taken", k->section, id, (int)e->number);
        return -1;
    }
    if (e->number >= 0 && !FillTo(k, (u32)e->number)) {
        return -1;
    }
    if ((r = Append(k)) == NULL) {
        return -1;
    }
    r->id = SDL_strdup(id);
    r->alias = alias;
    r->def = e->def;
    r->name = Dup(e->name);
    r->suffix = Dup(e->suffix);
    r->thumbnail = Dup(e->thumbnail);
    r->world = Dup(e->world);
    r->data = Dup(e->data);
    r->reserved = e->reserved;
    Grow(k);
    return (s32)(CountOf(k) - 1);
}

void Port_ContentReset(void) {
    int i;
    u32 j;

    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Kind *k = &s_kinds[i];

        for (j = 0; j < k->numReg; j++) {
            SDL_free(k->reg[j].id);
            SDL_free(k->reg[j].name);
            SDL_free(k->reg[j].suffix);
            SDL_free(k->reg[j].thumbnail);
            SDL_free(k->reg[j].world);
            SDL_free(k->reg[j].data);
        }
        SDL_free(k->reg);
        SDL_free(k->live);
        SDL_free(k->stash);
        k->reg = NULL;
        k->numReg = 0;
        k->live = NULL;
        k->liveSize = 0;
        k->stash = NULL;
        k->base = k->retail;
        k->view = NULL;
    }
    s_liveSlot = -1;
}

/* ---- the save struct as the view ---------------------------------------- */

void Port_ContentBindGameData(s8 *cars, s8 *tracks, s8 *items) {
    s_kinds[PORT_CONTENT_CAR].view = cars;
    s_kinds[PORT_CONTENT_TRACK].view = tracks;
    s_kinds[PORT_CONTENT_ITEM].view = items;
}

void Port_ContentFromGameData(void) {
    int i;
    u32 j;

    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Kind *k = &s_kinds[i];

        Grow(k);
        for (j = 0; j < k->liveSize; j++) {
            k->live[j] = j < k->retail ? (k->view != NULL ? k->view[j] : 0) : DefaultOf(k, j);
        }
    }
}

/* ---- the JSON save's entries past the arrays ---------------------------- */

/* A kept entry's value, as Port_SaveFormat.c reads one. */
static s8 KeptValue(const JsonNode *m) {
    if (m->type == JSON_OBJECT) {
        const JsonNode *v = Json_Get(m, "value");
        int unlocked = Json_GetBool(m, "unlocked", 0);

        return v != NULL && v->type == JSON_NUMBER && v->isInteger && v->integer >= -128 && v->integer <= 127
                   ? (s8)v->integer : (s8)unlocked;
    }
    if (m->type == JSON_BOOL) {
        return (s8)m->boolean;
    }
    if (m->type == JSON_NUMBER && m->isInteger && m->integer >= -128 && m->integer <= 127) {
        return (s8)m->integer;
    }
    return 0;
}

void Port_ContentLoadKept(const JsonNode *kept) {
    int i;
    u32 j;
    char id[160];

    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Kind *k = &s_kinds[i];
        const JsonNode *section = Json_Get(kept, k->section), *m;

        Grow(k);
        for (j = k->retail; j < k->liveSize; j++) {
            k->live[j] = DefaultOf(k, j);
            if (section != NULL && Port_ContentIdOf(i, j, id, sizeof(id)) && (m = Json_Get(section, id)) != NULL) {
                k->live[j] = KeptValue(m);
            }
        }
    }
}

void Port_ContentStoreKept(JsonNode **kept) {
    int i;
    u32 j;
    char id[160];

    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Kind *k = &s_kinds[i];
        JsonNode *section, *e;

        Grow(k);
        if (k->liveSize <= k->retail) {
            continue;
        }
        if (*kept == NULL && (*kept = Json_NewObject()) == NULL) {
            return;
        }
        section = Json_ObjectMember(*kept, k->section);
        for (j = k->retail; j < k->liveSize; j++) {
            if (!Port_ContentIdOf(i, j, id, sizeof(id))) {
                continue;
            }
            Json_RemoveMember(section, id);
            if ((e = Json_NewObject()) == NULL) {
                continue;
            }
            Json_Append(e, Json_NewBool(k->live[j] != 0), "unlocked");
            if (k->live[j] != 0 && k->live[j] != 1) {
                Json_Append(e, Json_NewInt(k->live[j]), "value");
            }
            Json_Append(section, e, id);
        }
    }
}

u32 Port_ContentKeptUnlocked(int kind, const JsonNode *kept) {
    Kind *k = KindOf(kind);
    const JsonNode *section;
    u32 j, n = 0;
    char id[160];

    if (k == NULL || (section = Json_Get(kept, k->section)) == NULL) {
        return 0;
    }
    for (j = k->retail; j < CountOf(k); j++) {
        const JsonNode *m;

        if (Port_ContentIdOf(kind, j, id, sizeof(id)) && (m = Json_Get(section, id)) != NULL && KeptValue(m) != 0) {
            n++;
        }
    }
    return n;
}

void Port_ContentNoteLiveSlot(int slot) {
    s_liveSlot = slot;
}

int Port_ContentTakeLiveSlot(void) {
    int slot = s_liveSlot;

    s_liveSlot = -1;
    return slot;
}

void Port_ContentStash(int restore) {
    int i;

    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Kind *k = &s_kinds[i];

        Grow(k);
        if (k->liveSize <= k->retail) {
            continue;
        }
        if (!restore) {
            SDL_free(k->stash);
            k->stash = SDL_malloc(k->liveSize - k->retail);
            if (k->stash != NULL) {
                SDL_memcpy(k->stash, k->live + k->retail, k->liveSize - k->retail);
            }
        } else if (k->stash != NULL) {
            SDL_memcpy(k->live + k->retail, k->stash, k->liveSize - k->retail);
        }
    }
}

/* ---- ids.lock ------------------------------------------------------------ */

/* An ids.lock entry as read. */
typedef struct LockEntry {
    const char *id;
    s32 number;             /* -1: none yet */
    const JsonNode *node;
    s32 alias;              /* -1: no base entry (data of its own: not loadable yet) */
    s8 active;
    s8 skip;                /* a duplicate */
} LockEntry;

static int ByNumber(const void *a, const void *b) {
    const LockEntry *x = a, *y = b;

    return x->number != y->number ? (x->number < y->number ? -1 : 1) : 0;
}

static const char *Str(const JsonNode *o, const char *key) {
    const JsonNode *v = Json_Get(o, key);

    return v != NULL && v->type == JSON_STRING ? v->string : NULL;
}

/* The lock back to its file: every entry, numbers as assigned. */
static int WriteLock(const char *path, LockEntry *const *entries, const u32 *num, const s32 *next) {
    JsonWriter w;
    char *tmp = NULL;
    int i, ok;
    u32 j;

    Jw_Init(&w);
    Jw_BeginObject(&w, NULL, 0);
    Jw_Comment(&w, "HWVX-SDL content numbers: docs/xdl-format/README.md 10.5. The game assigns a number to each");
    Jw_Comment(&w, "entry without one and writes this file back; an entry keeps its number for good (\"active\":");
    Jw_Comment(&w, "false keeps it for content that's away), and \"next\" never goes down, so numbers aren't reused.");
    Jw_Int(&w, "format_version", 1);
    Jw_BeginObject(&w, "next", 1);
    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Jw_Int(&w, s_kinds[i].section, next[i]);
    }
    Jw_EndObject(&w);
    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Jw_BeginObject(&w, s_kinds[i].section, 0);
        for (j = 0; j < num[i]; j++) {
            const LockEntry *e = &entries[i][j];
            const JsonNode *m;

            if (e->skip) {
                continue;
            }
            Jw_BeginObject(&w, e->id, 0);
            Jw_Int(&w, "number", e->number);
            /* the rest as it was, in its order */
            for (m = e->node != NULL && e->node->type == JSON_OBJECT ? e->node->child : NULL; m != NULL; m = m->next) {
                if (SDL_strcmp(m->key, "number") != 0) {
                    Jw_Node(&w, m->key, m);
                }
            }
            Jw_EndObject(&w);
        }
        Jw_EndObject(&w);
    }
    Jw_EndObject(&w);
    Jw_Raw(&w, "\n");
    ok = !w.failed && SDL_asprintf(&tmp, "%s.tmp", path) > 0 && Port_WriteFileSynced(tmp, w.buf, w.len) &&
         Port_ReplaceFile(tmp, path);
    SDL_free(tmp);
    Jw_Free(&w);
    return ok;
}

int Port_ContentLoadLock(const char *path) {
    size_t len;
    char *text = path != NULL ? SDL_LoadFile(path, &len) : NULL, err[160];
    JsonNode *root;
    LockEntry *entries[PORT_CONTENT_KINDS] = { NULL };
    u32 num[PORT_CONTENT_KINDS] = { 0 }, j, m;
    s32 next[PORT_CONTENT_KINDS];
    int i, changed = 0, registered = 0;

    if (text == NULL) {
        return 0;
    }
    root = Json_Parse(text, len, err, sizeof(err));
    SDL_free(text);
    if (root == NULL || root->type != JSON_OBJECT) {
        Port_Log("content: %s: %s; no entries from it (the file is left as it is)", path,
                 root == NULL ? err : "not an object");
        Json_Free(root);
        return -1;
    }
    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        Kind *k = &s_kinds[i];
        const JsonNode *section = Json_Get(root, k->section), *e;
        s32 top = (s32)CountOf(k) - 1;
        u32 index;

        next[i] = (s32)Json_GetInt(Json_Get(root, "next"), k->section, 0);
        entries[i] = SDL_calloc(section != NULL && section->count > 0 ? (size_t)section->count : 1, sizeof(LockEntry));
        for (e = section != NULL && section->type == JSON_OBJECT ? section->child : NULL; e != NULL && entries[i] != NULL;
             e = e->next) {
            LockEntry *x = &entries[i][num[i]++];
            const JsonNode *n = Json_Get(e, "number");
            const char *alias = Str(e, "alias");

            x->id = e->key;
            x->node = e;
            x->number = n != NULL && n->type == JSON_NUMBER && n->isInteger && n->integer >= 0 && n->integer < 0x7FFFFFFF
                            ? (s32)n->integer : -1;
            x->active = (s8)Json_GetBool(e, "active", 1);
            x->alias = alias != NULL && Port_ContentIndexOf(i, alias, &index) && index < BaseOf(k) ? (s32)index : -1;
            if (alias != NULL && x->alias < 0) {
                Port_Log("content: %s: %s: no base entry %s to use the data of", path, x->id, alias);
            }
            for (m = 0; m + 1 < num[i]; m++) {
                if (SDL_strcmp(entries[i][m].id, x->id) == 0) {
                    x->skip = 1;
                }
            }
            if (!x->skip && SDL_strncmp(x->id, "uid://", 6) != 0) {
                Port_Log("content: %s: %s: not a uid:// id; left out", path, x->id);
                x->skip = 1;
            }
        }
        /* numbers: kept where they're past the base content and not taken
         * twice; the others get the next past everything ever numbered */
        SDL_qsort(entries[i], num[i], sizeof(LockEntry), ByNumber);
        for (j = 0; j < num[i]; j++) {
            LockEntry *x = &entries[i][j];

            if (x->skip || x->number < 0) {
                continue;
            }
            if (x->number < (s32)CountOf(k) || x->number <= top) {
                Port_Log("content: %s: %s: number %d is %s; it gets a new one", path, x->id, (int)x->number,
                         x->number < (s32)BaseOf(k) ? "base content's" : "taken");
                x->number = -1;
                continue;
            }
            top = x->number;
        }
        if (next[i] < top + 1) {
            next[i] = top + 1;
        }
        for (j = 0; j < num[i]; j++) {
            LockEntry *x = &entries[i][j];

            if (!x->skip && x->number < 0) {
                x->number = next[i]++;
                changed = 1;
                Port_Log("content: %s: %s %s is number %d", path, k->section, x->id, (int)x->number);
            }
        }
        if ((s32)Json_GetInt(Json_Get(root, "next"), k->section, -1) != next[i]) {
            changed = 1;
        }
        SDL_qsort(entries[i], num[i], sizeof(LockEntry), ByNumber);
        for (j = 0; j < num[i]; j++) {
            const LockEntry *x = &entries[i][j];
            PortContentEntry pe;

            if (x->skip) {
                continue;
            }
            SDL_zero(pe);
            pe.id = x->id;
            pe.number = x->number;
            pe.alias = x->alias;
            pe.def = (s8)Json_GetBool(x->node, "unlocked", 0);
            pe.name = Str(x->node, "name");
            pe.suffix = Str(x->node, "suffix");
            pe.thumbnail = Str(x->node, "thumbnail");
            pe.world = Str(x->node, "world");
            pe.data = Str(x->node, "data");
            /* away, or data of its own that nothing loads (a mod's: not yet;
             * the prototype's, proto://, loads, Port_Proto.c): its number kept */
            pe.reserved = !x->active || (x->alias < 0 && pe.data == NULL);
            if (Port_ContentRegisterEntry(i, &pe) >= 0) {
                registered += !pe.reserved;
            }
        }
    }
    if (changed && !WriteLock(path, entries, num, next)) {
        Port_Log("content: %s can't be written", path);
    }
    for (i = 0; i < PORT_CONTENT_KINDS; i++) {
        SDL_free(entries[i]);
    }
    Json_Free(root);
    return registered;
}

/* ---- online: choices by id --------------------------------------------- */

/* One of this game's cars or items, else the first car or no item (-1). */
static s32 Clamp(int kind, s32 v) {
    if (v >= 0 && (u32)v < Port_ContentCount(kind)) {
        return v;
    }
    return kind == PORT_CONTENT_CAR ? 0 : -1;
}

/* A choice on the wire: a number, then the length of an id and the id.
 * Base content (numbered by its packs) has the same number on every
 * machine: no id. Registered content (ids.lock, mods) is numbered per
 * machine, so it goes by id, with the base entry whose data it uses as the
 * number: a machine without it shows that one instead. */
void Port_ContentWriteChoice(NetWriter *w, int kind, s32 n) {
    char id[64];
    s32 stand;
    size_t len;

    if (n < 0 || (u32)n < Port_ContentBaseCount(kind) || !Port_ContentIdOf(kind, (u32)n, id, sizeof(id))) {
        NetWriter_U32(w, (u32)n);
        NetWriter_U8(w, 0);
        return;
    }
    stand = Port_ContentDataOf(kind, (u32)n);
    len = SDL_strlen(id);
    NetWriter_U32(w, (u32)(stand >= 0 ? stand : 0));
    NetWriter_U8(w, (u8)len);
    NetWriter_Bytes(w, id, (u32)len);
}

s32 Port_ContentReadChoice(NetReader *r, int kind) {
    s32 n = (s32)NetReader_U32(r);
    u32 len = NetReader_U8(r), index;
    const u8 *bytes = len > 0 ? NetReader_Take(r, len) : NULL;
    char id[256];

    if (bytes == NULL) {
        return Clamp(kind, n);
    }
    SDL_memcpy(id, bytes, len);
    id[len] = 0;
    if (Port_ContentIndexOf(kind, id, &index)) {
        return (s32)index;
    }
    Port_Log("content: another player's %s %s isn't here; %d stands in", kind == PORT_CONTENT_CAR ? "car" : "item", id,
             (int)Clamp(kind, n));
    return Clamp(kind, n);
}
