/* Levels of one's own over a base world (Port_Level.h,
 * docs-pending/mission-editor.md). Port-only file.
 *
 * A level file (tools/xdl_level.py compile) is JSON:
 *
 *   {"format": "hwvx-level", "format_version": 1, "id": "uid://...", "name": "...",
 *    "world": "worlds_\\sp_\\chal1_\\ch1_l2",       the MCP's world path it is made for
 *    "baseTriggers": "all" | "none" | [3, 5, ...],  the base world's triggers it keeps
 *    "parts": [{"id": "...", "kind": "trigger" | "timer" | "variable" | "data",
 *               "hex": "...", "links": [[offset, LINK], ...]}, ...],
 *    "patches": [{"target": LINK, "offset": n, "hex": "..."}, ...],
 *    "appends": [{"record": LINK, "pointer": n, "count": n, "elemSize": n,
 *                 "elems": [{"fromPort": p | "fromIndex": i | "from": LINK | "part": ID,
 *                            "set": [[offset, "hex"], ...]}, ...]}, ...]}
 *
 * A part is one record of the level's own, in host byte order (little-endian
 * words) as the game has pack data once loaded, with its pointer slots zero
 * and listed in `links`. LINK is where a slot points:
 *
 *   {"part": ID, "add": n}         a part of the level (n bytes into it)
 *   {"slot": ID}                   a timer's or variable's entry in the world's
 *                                  table (script parameters name those by entry)
 *   {"append": a, "elem": j, "add": n}
 *                                  element j the level appends to appends[a]'s array
 *   {"root": "world" | "trigger", "index": n, "steps": [["p", off] | ["a", n], ...]}
 *                                  a record of the base world: from the World
 *                                  (offset 0 of the pack) or base trigger n,
 *                                  "p": follow the pointer at off, "a": n bytes on.
 *                                  The compiler makes the steps from the schemas'
 *                                  offsets, which every release shares, so one
 *                                  file works on any release's world.
 *
 * Applying: trigger parts become the pack's trigger table (after the base
 * triggers it keeps, copied), so the loader activates them as it would the
 * pack's own; timer and variable parts are appended to the World's tables
 * (new tables; the old ones stay where base triggers point into them); the
 * rest stay where they were allocated. Patches write bytes into base records
 * (old bytes kept).
 *
 * Appends grow an array of the base world: the u32 pointer at `pointer` and
 * the u32 count at `count` of the record LINK names (World: tileCars,
 * objectives; a quadrant's QuadContents: items, fxPoints). The array is copied
 * into one of ours with room for the new elements, and every pointer of the
 * world pack into the old one (its pointer slots, kept as the pack loaded:
 * Port_LevelNotePack) moved to the copy, so base triggers that name tile car 2
 * name it there. An element is a copy of a base one (the first tile car of
 * port p, the array's element i, or the one LINK names), then `set` bytes of
 * the level's own written over it (its position, port, id), or a part of
 * kind "element" (bytes of the level's own, its links resolved in place).
 * This is what tools/xdl_level_pack.py adds to a world pack, done at load:
 * the level ships as this file, made of the level's own data only.
 * Everything is undone when the pack goes, or before a level is applied
 * again. */
#include <SDL3/SDL.h>
#include "common.h"
#include "World.h"
#include "Trigger.h"
#include "Port_Content.h"
#include "Port_Mods.h"
#include "Port_Level.h"
#include "jsonc.h"

s32 Trigger_ReleaseTrigger(s32 *arg);

#define TRIGGER_SIZE 0x2C

typedef struct LevelPart {
    const char *id;
    const char *kind;
    u8 *mem;            /* its bytes (a trigger's: in the table) */
    u32 size;
    int own;            /* mem is an allocation of its own */
    u32 slot;           /* timer/variable: its entry's index in the new table */
} LevelPart;

typedef struct LevelPatch {
    u8 *at;
    u8 *old;                    /* the old bytes (oldWord when size <= 4) */
    u32 size;
    u32 oldWord;
} LevelPatch;

static struct {
    DataPack *pack;
    World *world;
    char *id, *path;
    u8 *origTriggers;
    u32 origNumTriggers;
    u8 *triggers;       /* the table the pack has now */
    u32 numTriggers;
    u32 origTimers, origNumTimers, origVariables, origNumVariables;
    u32 *timers, *variables;
    u32 numTimerSlots, numVariableSlots;    /* the entries of timers/variables */
    void **allocs;
    int numAllocs, capAllocs;
    LevelPatch *patches;
    int numPatches, capPatches;
    struct LevelGrow {
        u8 *old;                /* the base array (in the blob) */
        u32 oldSize;
        u8 *mem;                /* ours: the old elements, then the level's */
        u32 size;
        u32 elemSize;
        int kind;               /* its s_growable entry */
    } *grows;
    int numGrows;
    WorldVolume **volumes;      /* the level's own volumes (parts of kind "volume") */
    char **volumeIds;
    int numVolumes;
    /* while applying (a level is untrusted input: everything is checked before the pack
     * takes it, see Validate) */
    struct LevelPart *parts;
    int numParts;
    u8 **linked;                /* the words a link wrote (pointer slots the level may fill) */
    int numLinked, capLinked;
    u32 *notedBits;             /* the world pack's pointer slots, a bit per word of the blob */
} s_lv;

/* Limits for what a level file may ask for (far above what any level needs). */
#define LEVEL_MAX_HEX 0x100000      /* bytes of a part or a patch */
#define LEVEL_MAX_APPEND 4096       /* elements one append adds */
#define LEVEL_MAX_PARTS 65536
#define LEVEL_MAX_VARIABLES 30      /* the world's variables, all told: an interface keeps
                                     * vars[30] and stores by world variable index */

/* ---- helpers -------------------------------------------------------------- */

static int Fail(char *err, size_t size, const char *fmt, ...) PORT_PRINTF(3, 4);
static int Fail(char *err, size_t size, const char *fmt, ...) {
    va_list ap;

    if (err != NULL && size > 0) {
        va_start(ap, fmt);
        SDL_vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static void *Keep(void *p) {
    if (p == NULL) {
        return NULL;
    }
    if (s_lv.numAllocs == s_lv.capAllocs) {
        int cap = s_lv.capAllocs ? s_lv.capAllocs * 2 : 32;
        void **a = SDL_realloc(s_lv.allocs, (size_t)cap * sizeof(void *));

        if (a == NULL) {
            SDL_aligned_free(p);
            return NULL;
        }
        s_lv.allocs = a;
        s_lv.capAllocs = cap;
    }
    s_lv.allocs[s_lv.numAllocs++] = p;
    return p;
}

static void *Alloc(size_t size) {
    void *p = SDL_aligned_alloc(32, (size + 31) & ~(size_t)31);

    if (p != NULL) {
        SDL_memset(p, 0, (size + 31) & ~(size_t)31);
    }
    return Keep(p);
}

static int InBlob(const DataPack *pack, const u8 *p, u32 size) {
    const u8 *lo = GETP(pack->base);

    return lo != NULL && p != NULL && p >= lo && (size_t)(p - lo) <= pack->dataSize &&
           size <= pack->dataSize - (u32)(p - lo);
}

/* In the world pack's blob, or an array a level grew. */
static int InWorld(const DataPack *pack, const u8 *p, u32 size) {
    int i;

    if (InBlob(pack, p, size)) {
        return 1;
    }
    for (i = 0; p != NULL && i < s_lv.numGrows; i++) {
        const struct LevelGrow *g = &s_lv.grows[i];

        if (p >= g->mem && (size_t)(p - g->mem) <= g->size && size <= g->size - (u32)(p - g->mem)) {
            return 1;
        }
    }
    return 0;
}

/* The world pack loaded last and its pointer slots (Port_LevelNotePack). */
static struct {
    void *pack;
    u32 *pointers;
    u32 count;
} s_noted;

void Port_LevelNotePack(void *dataPack, const unsigned int *pointers, unsigned int numPointers) {
    SDL_free(s_noted.pointers);
    s_noted.pack = dataPack;
    s_noted.count = 0;
    s_noted.pointers = pointers != NULL && numPointers > 0 ? SDL_malloc((size_t)numPointers * 4) : NULL;
    if (s_noted.pointers != NULL) {
        SDL_memcpy(s_noted.pointers, pointers, (size_t)numPointers * 4);
        s_noted.count = numPointers;
    }
}

/* ---- checks (a level file is untrusted: shared mods) ------------------------ */

/* [p, p + size) inside one block of ours or of the world: a part, a grown array, the
 * trigger, timer and variable tables, the pack's blob. The block's start and size in
 * lo and len when asked. */
static int Container(const u8 *p, u64 size, const u8 **lo, u64 *len) {
    const u8 *b[8];
    u64 n[8];
    int i, nb = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < s_lv.numParts; i++) {
        const u8 *m = s_lv.parts[i].mem;

        if (m != NULL && p >= m && (u64)(p - m) <= s_lv.parts[i].size) {
            if (lo != NULL) {
                *lo = m;
                *len = s_lv.parts[i].size;
            }
            return size <= s_lv.parts[i].size - (u64)(p - m);
        }
    }
    for (i = 0; i < s_lv.numGrows; i++) {
        const u8 *m = s_lv.grows[i].mem;

        if (m != NULL && p >= m && (u64)(p - m) <= s_lv.grows[i].size) {
            if (lo != NULL) {
                *lo = m;
                *len = s_lv.grows[i].size;
            }
            return size <= s_lv.grows[i].size - (u64)(p - m);
        }
    }
    b[nb] = s_lv.triggers, n[nb++] = (u64)s_lv.numTriggers * TRIGGER_SIZE;
    b[nb] = (const u8 *)s_lv.timers, n[nb++] = (u64)s_lv.numTimerSlots * 4;
    b[nb] = (const u8 *)s_lv.variables, n[nb++] = (u64)s_lv.numVariableSlots * 4;
    b[nb] = GETP(s_lv.pack->base), n[nb++] = s_lv.pack->dataSize;
    for (i = 0; i < nb; i++) {
        if (b[i] != NULL && p >= b[i] && (u64)(p - b[i]) <= n[i]) {
            if (lo != NULL) {
                *lo = b[i];
                *len = n[i];
            }
            return size <= n[i] - (u64)(p - b[i]);
        }
    }
    return 0;
}

/* Where the bytes at p came from in the world pack's blob: p itself, or the base
 * element an array the level grew holds a copy of; NULL for the level's own. */
static const u8 *BlobOrigin(const u8 *p) {
    const u8 *base = GETP(s_lv.pack->base);
    int i;

    if (base != NULL && p >= base && (u64)(p - base) < s_lv.pack->dataSize) {
        return p;
    }
    for (i = 0; i < s_lv.numGrows; i++) {
        const struct LevelGrow *g = &s_lv.grows[i];

        if (g->old != NULL && p >= g->mem && (u64)(p - g->mem) < g->oldSize) {
            return g->old + (p - g->mem);
        }
    }
    return NULL;
}

/* The word at blob address p is one of the pack's pointer slots. */
static int NotedSlot(const u8 *p) {
    const u8 *base = GETP(s_lv.pack->base);
    u64 w;

    if (s_lv.notedBits == NULL || p < base || ((p - base) & 3) || (u64)(p - base) + 4 > s_lv.pack->dataSize) {
        return 0;
    }
    w = (u64)(p - base) / 4;
    return (s_lv.notedBits[w / 32] >> (w % 32)) & 1;
}

/* [p, p + size) touches a pointer slot of the world (blob or a base copy). */
static int TouchesNoted(const u8 *p, u32 size) {
    const u8 *o = BlobOrigin(p);
    u32 k;

    if (o == NULL) {
        return 0;
    }
    for (k = 0; k < size + ((uintptr_t)o & 3); k += 4) {
        if (NotedSlot((const u8 *)(((uintptr_t)o & ~(uintptr_t)3) + k))) {
            return 1;
        }
    }
    return 0;
}

static int NoteLinked(u8 *at) {
    if (s_lv.numLinked == s_lv.capLinked) {
        int cap = s_lv.capLinked ? s_lv.capLinked * 2 : 64;
        u8 **a = SDL_realloc(s_lv.linked, (size_t)cap * sizeof(*a));

        if (a == NULL) {
            return 0;
        }
        s_lv.linked = a;
        s_lv.capLinked = cap;
    }
    s_lv.linked[s_lv.numLinked++] = at;
    return 1;
}

/* A link of the level wrote the word at `at`. */
static int Linked(const u8 *at) {
    int i;

    for (i = s_lv.numLinked - 1; i >= 0; i--) {
        if (s_lv.linked[i] == at) {
            return 1;
        }
    }
    return 0;
}

/* [p, p + size) touches a word a link wrote. */
static int TouchesLinked(const u8 *p, u32 size) {
    int i;

    for (i = 0; i < s_lv.numLinked; i++) {
        if (s_lv.linked[i] + 4 > p && s_lv.linked[i] < p + size) {
            return 1;
        }
    }
    return 0;
}

/* The pointer slot at `at` of a record the game will use: zero (if allowed), filled by
 * a link of the level, or one of the world's own slots (`orig`: where a copy of a base
 * trigger came from), pointing at `size` bytes in one block. Only then is its handle
 * turned into a pointer (an arbitrary word would be Port_Fatal's or an arbitrary address). */
static int SlotOk(const u8 *at, const u8 *orig, u64 size, int nullOk, u8 **to) {
    u32 h = *(const u32 *)at;
    const u8 *o;
    u8 *p;

    *to = NULL;
    if (h == 0) {
        return nullOk;
    }
    if (orig != NULL) {
        if (*(const u32 *)orig != h || !NotedSlot(orig)) {
            return 0;
        }
    } else if (!Linked(at) && ((o = BlobOrigin(at)) == NULL || !NotedSlot(o))) {
        return 0;
    }
    p = Port_HandleToPtr(h);
    if (!Container(p, size, NULL, NULL)) {
        return 0;
    }
    *to = p;
    return 1;
}

/* A condition or action array the game will run: types in range, parameters set. */
static int RecordsOk(const u8 *at, const u8 *orig, u32 count, u32 recSize, u32 numTypes, const char *what, char *err,
                     size_t errSize) {
    u8 *recs, *param;
    u32 i;

    if (count == 0) {
        return 1;
    }
    if (count > 0x10000 || !SlotOk(at, orig, (u64)count * recSize, 0, &recs)) {
        return Fail(err, errSize, "%s: %u of them, not where the level linked them", what, (unsigned)count);
    }
    for (i = 0; i < count; i++) {
        const u8 *r = recs + (u64)i * recSize;

        if (*(const u32 *)r >= numTypes) {
            return Fail(err, errSize, "%s %u: no type %u", what, (unsigned)i, (unsigned)*(const u32 *)r);
        }
        if (!SlotOk(r + 4, NULL, 4, 1, &param)) {
            return Fail(err, errSize, "%s %u: its parameters aren't a link of the level", what, (unsigned)i);
        }
    }
    return 1;
}

/* A trigger the pack will activate (`orig`: the base trigger it is a copy of): its type,
 * its conditions and actions. */
static int TriggerOk(const u8 *t, const u8 *orig, char *err, size_t errSize) {
    const Trigger *tr = (const Trigger *)t;
    u8 *param;

    if (tr->type >= TRIGGER_NUM_TYPES) {
        return Fail(err, errSize, "no trigger type %u", (unsigned)tr->type);
    }
    if (!SlotOk(t + 0x04, orig != NULL ? orig + 0x04 : NULL, 4, 1, &param)) {
        return Fail(err, errSize, "its parameters aren't a link of the level");
    }
    return RecordsOk(t + 0x0C, orig != NULL ? orig + 0x0C : NULL, tr->numConditions, TRIGGER_SIZE, TRIGGER_NUM_TYPES,
                     "condition", err, errSize) &&
           RecordsOk(t + 0x14, orig != NULL ? orig + 0x14 : NULL, tr->numActions, sizeof(TriggerAction),
                     ACTION_NUM_TYPES, "action", err, errSize) &&
           RecordsOk(t + 0x18, orig != NULL ? orig + 0x18 : NULL, tr->numElseActions, sizeof(TriggerAction),
                     ACTION_NUM_TYPES, "else action", err, errSize);
}

static int SameWorld(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = (char)SDL_tolower((unsigned char)*a), y = (char)SDL_tolower((unsigned char)*b);

        if (x == '/') {
            x = '\\';
        }
        if (y == '/') {
            y = '\\';
        }
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* ---- undoing -------------------------------------------------------------- */

static void Unapply(void) {
    int i;
    u32 k;

    if (s_lv.pack == NULL) {
        return;
    }
    /* active triggers of ours out of the trigger lists first (no-op if they
     * weren't activated) */
    for (k = 0; s_lv.triggers != NULL && k < s_lv.numTriggers; k++) {
        Trigger_ReleaseTrigger((s32 *)(s_lv.triggers + k * TRIGGER_SIZE));
    }
    if (GETP(s_lv.pack->triggers) == s_lv.triggers) {
        SETP(s_lv.pack->triggers, s_lv.origTriggers);
        s_lv.pack->numTriggers = s_lv.origNumTriggers;
    }
    if (s_lv.timers != NULL && GETP(s_lv.world->timers) == (void *)s_lv.timers) {
        s_lv.world->timers.h = s_lv.origTimers;
        s_lv.world->numTimers = s_lv.origNumTimers;
    }
    if (s_lv.variables != NULL && GETP(s_lv.world->variables) == (void *)s_lv.variables) {
        s_lv.world->variables.h = s_lv.origVariables;
        s_lv.world->numVariables = s_lv.origNumVariables;
    }
    for (i = s_lv.numPatches - 1; i >= 0; i--) {
        LevelPatch *pt = &s_lv.patches[i];

        SDL_memcpy(pt->at, pt->size <= 4 ? (u8 *)&pt->oldWord : pt->old, pt->size);
        if (pt->size > 4) {
            SDL_free(pt->old);
        }
    }
    SDL_free(s_lv.patches);
    SDL_free(s_lv.linked);
    SDL_free(s_lv.notedBits);
    for (i = 0; i < s_lv.numAllocs; i++) {
        SDL_aligned_free(s_lv.allocs[i]);
    }
    SDL_free(s_lv.allocs);
    SDL_free(s_lv.grows);
    for (i = 0; i < s_lv.numVolumes; i++) {
        SDL_free(s_lv.volumeIds[i]);
    }
    SDL_free(s_lv.volumes);
    SDL_free(s_lv.volumeIds);
    SDL_free(s_lv.id);
    SDL_free(s_lv.path);
    SDL_zero(s_lv);
}

void Port_LevelPackReleased(void *dataPack) {
    if (dataPack != NULL && dataPack == s_noted.pack) {
        SDL_free(s_noted.pointers);
        SDL_zero(s_noted);
    }
    if (dataPack != NULL && dataPack == s_lv.pack) {
        Port_Log("level: %s taken out with its world", s_lv.id != NULL ? s_lv.id : "?");
        Unapply();
    }
}

const char *Port_LevelCurrent(const char **path) {
    if (path != NULL) {
        *path = s_lv.path;
    }
    return s_lv.pack != NULL ? s_lv.id : NULL;
}

/* ---- links ---------------------------------------------------------------- */

static LevelPart *PartNamed(LevelPart *parts, int n, const char *id) {
    int i;

    for (i = 0; id != NULL && i < n; i++) {
        if (SDL_strcmp(parts[i].id, id) == 0) {
            return &parts[i];
        }
    }
    return NULL;
}

/* Where LINK points; NULL and why in err. */
static u8 *Resolve(const JsonNode *link, LevelPart *parts, int n, char *err, size_t errSize) {
    const char *root = Json_GetString(link, "root", NULL);
    const JsonNode *steps, *s;
    LevelPart *p;
    u8 *cur;

    if (link == NULL || link->type != JSON_OBJECT) {
        Fail(err, errSize, "a link is an object");
        return NULL;
    }
    if (Json_Get(link, "part") != NULL) {
        long long add = Json_GetInt(link, "add", 0);

        if ((p = PartNamed(parts, n, Json_GetString(link, "part", NULL))) == NULL) {
            Fail(err, errSize, "no part %s", Json_GetString(link, "part", "?"));
            return NULL;
        }
        if (add < 0 || add > p->size) {
            Fail(err, errSize, "%s + %lld is outside it", p->id, add);
            return NULL;
        }
        if (p->mem == NULL) {
            Fail(err, errSize, "%s: not placed yet", p->id);   /* (an append's record or element) */
            return NULL;
        }
        return p->mem + add;
    }
    if (Json_Get(link, "append") != NULL) {
        long long a = Json_GetInt(link, "append", -1), j = Json_GetInt(link, "elem", -1);
        long long add = Json_GetInt(link, "add", 0);
        const struct LevelGrow *g;
        u32 es;

        if (a < 0 || a >= s_lv.numGrows || j < 0) {
            Fail(err, errSize, "no appended element %lld of %lld", j, a);
            return NULL;
        }
        g = &s_lv.grows[a];
        es = g->elemSize;
        if (es == 0 || g->oldSize + (j + 1) * (long long)es > g->size || add < 0 || add >= es) {
            Fail(err, errSize, "appended element %lld of %lld: outside it", j, a);
            return NULL;
        }
        return g->mem + g->oldSize + j * es + add;
    }
    if (Json_Get(link, "slot") != NULL) {
        p = PartNamed(parts, n, Json_GetString(link, "slot", NULL));
        if (p == NULL || (SDL_strcmp(p->kind, "timer") != 0 && SDL_strcmp(p->kind, "variable") != 0)) {
            Fail(err, errSize, "no timer or variable %s", Json_GetString(link, "slot", "?"));
            return NULL;
        }
        if ((SDL_strcmp(p->kind, "timer") == 0 ? s_lv.timers : s_lv.variables) == NULL) {
            Fail(err, errSize, "%s: no table yet", p->id);   /* (an append's record) */
            return NULL;
        }
        return (u8 *)(SDL_strcmp(p->kind, "timer") == 0 ? s_lv.timers : s_lv.variables) + 4 * p->slot;
    }
    if (root == NULL) {
        Fail(err, errSize, "a link names a part, a slot or a root");
        return NULL;
    }
    if (SDL_strcmp(root, "world") == 0) {
        cur = GETP(s_lv.pack->base);
    } else if (SDL_strcmp(root, "trigger") == 0) {
        long long i = Json_GetInt(link, "index", -1);

        if (i < 0 || i >= s_lv.origNumTriggers) {
            Fail(err, errSize, "the world has no trigger %lld (%u)", i, (unsigned)s_lv.origNumTriggers);
            return NULL;
        }
        cur = s_lv.origTriggers + i * TRIGGER_SIZE;
    } else {
        Fail(err, errSize, "no root %s", root);
        return NULL;
    }
    steps = Json_Get(link, "steps");
    for (s = steps != NULL && steps->type == JSON_ARRAY ? steps->child : NULL; s != NULL; s = s->next) {
        const JsonNode *a = Json_At(s, 0), *b = Json_At(s, 1);
        long long v = b != NULL && b->type == JSON_NUMBER ? b->integer : -1;

        if (a == NULL || a->type != JSON_STRING || v < 0 || v > 0x7FFFFFFF) {
            Fail(err, errSize, "a step is [\"p\" or \"a\", n]");
            return NULL;
        }
        if (a->string[0] == 'p') {
            if (!InWorld(s_lv.pack, cur + v, 4)) {
                Fail(err, errSize, "a pointer step leaves the world pack");
                return NULL;
            }
            {
                /* bounds-checked: only a handle inside the world pack's blob is followed (a
                 * level made for another world, or a stale one, never reaches the handle table) */
                u32 h = *(u32 *)(cur + v), lo = Port_PtrToHandle(GETP(s_lv.pack->base));

                if (h == 0) {
                    Fail(err, errSize, "a pointer on the way is null in this release's world");
                    return NULL;
                }
                if (h - lo < s_lv.pack->dataSize) {
                    cur = GETP(s_lv.pack->base) + (h - lo);
                } else {
                    /* or into an array the level grew (its appends) */
                    int g;

                    for (g = 0, cur = NULL; g < s_lv.numGrows && cur == NULL; g++) {
                        if (h == Port_PtrToHandle(s_lv.grows[g].mem)) {
                            cur = s_lv.grows[g].mem;
                        }
                    }
                    if (cur == NULL) {
                        Fail(err, errSize, "a pointer on the way leads out of the world pack (made for another world?)");
                        return NULL;
                    }
                }
            }
        } else {
            cur += v;
        }
        if (!InWorld(s_lv.pack, cur, 1)) {
            Fail(err, errSize, "the path leaves the world pack");
            return NULL;
        }
    }
    return cur;
}

static int HexLen(const char *s) {
    size_t n = 0;

    for (; *s; s++) {
        n += SDL_isxdigit((unsigned char)*s) != 0;
    }
    return (int)(n / 2);
}

/* ---- applying ------------------------------------------------------------- */

/* A patch of our own: `size` bytes at `at` set to `data`, the old ones kept. */
static int PatchBytes(u8 *at, const void *data, u32 size) {
    LevelPatch *pt;

    if (s_lv.numPatches == s_lv.capPatches) {
        /* grown geometrically: an append moves a pointer per patch, thousands of them */
        int cap = s_lv.capPatches ? s_lv.capPatches * 2 : 64;
        LevelPatch *grown = SDL_realloc(s_lv.patches, (size_t)cap * sizeof(LevelPatch));

        if (grown == NULL) {
            return 0;
        }
        s_lv.patches = grown;
        s_lv.capPatches = cap;
    }
    pt = &s_lv.patches[s_lv.numPatches];
    pt->at = at;
    pt->size = size;
    pt->old = NULL;
    if (size <= 4) {
        SDL_memcpy(&pt->oldWord, at, size);     /* a word: kept inline */
    } else if ((pt->old = SDL_malloc(size)) == NULL) {
        return 0;
    } else {
        SDL_memcpy(pt->old, at, size);
    }
    if (data != NULL) {
        SDL_memcpy(at, data, size);
    }
    s_lv.numPatches++;
    return 1;
}

/* A word of ours at `at` (a pointer slot or a count), the old one kept. */
static int PatchWord(u8 *at, u32 v) {
    return PatchBytes(at, &v, 4);
}

/* The arrays a level may grow, by the record that holds them (World: tileCars,
 * objectives; a quadrant's QuadContents: items, fxPoints), with their elements' size. */
static const struct {
    int quad;                   /* 0: the World, 1: a QuadContents */
    u32 pointer, count, elemSize;
    u8 ptrs[4];                 /* the element's pointer slots (0: no more): also the ones
                                 * null in the pack (set at run time), so not in its list */
} s_growable[] = {
    {0, 0x0C, 0x08, sizeof(TileCar), {0x24, 0x28, 0x50, 0x54}},
    {0, 0x5C, 0x58, sizeof(WorldObjective), {0x04}},
    {1, 0x18, 0x14, 64, {0x1C, 0x24}},     /* QuadItem: record, thing */
    {1, 0x10, 0x0C, sizeof(QuadFxPoint), {0x24, 0x28, 0x30}},
};

_Static_assert(offsetof(TileCar, car) == 0x28 && offsetof(TileCar, item) == 0x54, "TileCar pointers");
_Static_assert(offsetof(QuadFxPoint, thing) == 0x30, "QuadFxPoint.thing");

_Static_assert(sizeof(TileCar) == 96, "TileCar size");
_Static_assert(sizeof(WorldObjective) == 8, "WorldObjective size");
_Static_assert(sizeof(QuadFxPoint) == 64, "QuadFxPoint size");
_Static_assert(offsetof(World, tileCars) == 0x0C && offsetof(World, objectives) == 0x5C, "World arrays");
_Static_assert(offsetof(QuadContents, items) == 0x18 && offsetof(QuadContents, fxPoints) == 0x10, "QuadContents");

/* rec is the QuadContents of one of the world's quadrants. */
static int IsQuadContents(DataPack *pack, World *world, const u8 *rec) {
    const u8 *quads = GETP(world->quads);
    u32 i;

    if (world->numQuads > 0x10000 || !InBlob(pack, quads, world->numQuads * (u32)sizeof(WorldQuad))) {
        return 0;
    }
    for (i = 0; i < world->numQuads; i++) {
        u32 h = ((const WorldQuad *)quads)[i].contents.h;

        if (h != 0 && NotedSlot(quads + i * sizeof(WorldQuad) + offsetof(WorldQuad, contents)) &&
            Port_HandleToPtr(h) == rec) {
            return InBlob(pack, rec, sizeof(QuadContents));
        }
    }
    return 0;
}

/* o (in the blob) is in an element of an array of kind t (the World's, or a quadrant's):
 * 1 + its offset in the element, else 0. */
static u32 InElementOfKind(DataPack *pack, const u8 *o, int t) {
    World *world = (World *)GETP(pack->base);
    const u8 *quads = GETP(world->quads);
    u32 i, n = s_growable[t].quad ? world->numQuads : 1;

    if (o == NULL || (s_growable[t].quad && (n > 0x10000 || !InBlob(pack, quads, n * (u32)sizeof(WorldQuad))))) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        const u8 *rec = (const u8 *)world, *arr;
        u32 count;

        if (s_growable[t].quad) {
            const u8 *slot = quads + i * sizeof(WorldQuad) + offsetof(WorldQuad, contents);

            if (*(const u32 *)slot == 0 || !NotedSlot(slot)) {
                continue;
            }
            rec = Port_HandleToPtr(*(const u32 *)slot);
            if (!InBlob(pack, rec, sizeof(QuadContents))) {
                continue;
            }
        }
        count = *(const u32 *)(rec + s_growable[t].count);
        if (count == 0 || count > 0x10000 || !NotedSlot(rec + s_growable[t].pointer)) {
            continue;
        }
        arr = BlobOrigin(Port_HandleToPtr(*(const u32 *)(rec + s_growable[t].pointer)));  /* (moved to a copy) */
        if (InBlob(pack, arr, count * s_growable[t].elemSize) && o >= arr &&
            o < arr + count * s_growable[t].elemSize) {
            return 1 + (u32)(o - arr) % s_growable[t].elemSize;
        }
    }
    return 0;
}

static int ElementOfKind(DataPack *pack, const u8 *o, int t) {
    return InElementOfKind(pack, o, t) == 1;
}

/* The word at blob address o is a pointer slot of an element of the arrays above
 * (also the ones null in the pack). */
static int TypedPointer(DataPack *pack, const u8 *o) {
    int t, q;

    for (t = 0; t < (int)SDL_arraysize(s_growable); t++) {
        u32 in = InElementOfKind(pack, o, t);

        for (q = 0; in != 0 && q < 4 && s_growable[t].ptrs[q] != 0; q++) {
            if (in - 1 == s_growable[t].ptrs[q]) {
                return 1;
            }
        }
    }
    return 0;
}

/* The level's appends (see the top): the arrays grown, the world's pointers
 * into them moved, the elements filled. Element parts get their place in the
 * array as mem (their bytes and links are written there later). */
static int ApplyAppends(DataPack *pack, const JsonNode *japp, LevelPart *parts, int n, char *err, size_t errSize) {
    const JsonNode *ja, *je;
    u8 *base = GETP(pack->base);
    u32 lo = Port_PtrToHandle(base), i;
    u8 **recs;
    int a, na = japp != NULL && japp->type == JSON_ARRAY ? japp->count : 0;
    char why[160];

    if (na == 0) {
        return 1;
    }
    if (s_noted.pack != pack || s_noted.pointers == NULL) {
        return Fail(err, errSize, "appends: the world pack's pointer list wasn't kept (Port_LevelNotePack)");
    }
    s_lv.grows = SDL_calloc((size_t)na, sizeof(*s_lv.grows));
    recs = SDL_calloc((size_t)na, sizeof(*recs));
    if (s_lv.grows == NULL || recs == NULL) {
        SDL_free(recs);
        return Fail(err, errSize, "out of memory");
    }
    /* the arrays: where, how big, ours allocated */
    for (a = 0, ja = japp->child; ja != NULL; ja = ja->next, a++) {
        struct LevelGrow *g = &s_lv.grows[a];
        long long po = Json_GetInt(ja, "pointer", -1), co = Json_GetInt(ja, "count", -1);
        long long es = Json_GetInt(ja, "elemSize", 0);
        const JsonNode *elems = Json_Get(ja, "elems");
        u32 count, k = elems != NULL && elems->type == JSON_ARRAY ? (u32)elems->count : 0;
        u8 *rec = Resolve(Json_Get(ja, "record"), parts, n, why, sizeof(why));

        if (rec == NULL) {
            SDL_free(recs);
            return Fail(err, errSize, "append %d: %s", a, why);
        }
        {
            /* only the arrays above, by their record, at their sizes */
            int t, quad = rec == base ? 0 : IsQuadContents(pack, (World *)base, rec) ? 1 : -1, known = 0, d;

            for (t = 0; t < (int)SDL_arraysize(s_growable); t++) {
                if (s_growable[t].quad == quad && s_growable[t].pointer == po && s_growable[t].count == co &&
                    s_growable[t].elemSize == es) {
                    known = 1;
                    g->kind = t;
                }
            }
            for (d = 0; d < a; d++) {
                known &= !(recs[d] == rec && Json_GetInt(Json_At(japp, d), "pointer", -1) == po);
            }
            if (!known || k > LEVEL_MAX_APPEND) {
                SDL_free(recs);
                return Fail(err, errSize, "append %d: not an array a level grows (World tileCars/objectives, a "
                            "quadrant's items/fxPoints, once each, at most %d elements)", a, LEVEL_MAX_APPEND);
            }
        }
        if (!NotedSlot(rec + po)) {
            SDL_free(recs);
            return Fail(err, errSize, "append %d: its pointer isn't one of the world's", a);
        }
        recs[a] = rec;
        count = *(u32 *)(rec + co);
        if (count > 0x10000) {
            SDL_free(recs);
            return Fail(err, errSize, "append %d: the base array isn't in the world pack", a);
        }
        g->old = count > 0 ? Port_HandleToPtr(*(u32 *)(rec + po)) : NULL;
        g->oldSize = count * (u32)es;
        g->elemSize = (u32)es;
        g->size = g->oldSize + k * (u32)es;
        if (count > 0 && !InBlob(pack, g->old, g->oldSize)) {
            SDL_free(recs);
            return Fail(err, errSize, "append %d: the base array isn't in the world pack", a);
        }
        if ((g->mem = Alloc(g->size + 4)) == NULL) {
            SDL_free(recs);
            return Fail(err, errSize, "out of memory");
        }
        s_lv.numGrows = a + 1;
    }
    /* every pointer of the world pack into an old array: to our copy */
    for (i = 0; i < s_noted.count; i++) {
        u32 off = s_noted.pointers[i], h;
        u8 *q;

        if (off > pack->dataSize - 4 || (off & 3)) {
            continue;
        }
        h = *(u32 *)(base + off);
        if (h - lo >= pack->dataSize) {
            continue;
        }
        q = base + (h - lo);
        for (a = 0; a < s_lv.numGrows; a++) {
            struct LevelGrow *g = &s_lv.grows[a];

            if (g->old != NULL && q >= g->old && q < g->old + g->oldSize) {
                if (!PatchWord(base + off, Port_PtrToHandle(g->mem + (q - g->old)))) {
                    SDL_free(recs);
                    return Fail(err, errSize, "out of memory");
                }
                break;
            }
        }
    }
    /* the old elements (their pointers moved too), then the new ones */
    for (a = 0; a < s_lv.numGrows; a++) {
        if (s_lv.grows[a].oldSize > 0) {
            SDL_memcpy(s_lv.grows[a].mem, s_lv.grows[a].old, s_lv.grows[a].oldSize);
        }
    }
    for (a = 0, ja = japp->child; ja != NULL; ja = ja->next, a++) {
        struct LevelGrow *g = &s_lv.grows[a];
        const JsonNode *elems = Json_Get(ja, "elems");
        u32 es = g->elemSize, count = g->oldSize / es, j;

        for (j = 0, je = elems != NULL && elems->type == JSON_ARRAY ? elems->child : NULL; je != NULL; je = je->next, j++) {
            u8 *dst = g->mem + g->oldSize + j * es;
            const u8 *src = NULL;
            const JsonNode *js, *x;

            if (Json_Get(je, "fromPort") != NULL) {
                long long port = Json_GetInt(je, "fromPort", 1);
                u32 c;

                if (es != sizeof(TileCar)) {
                    SDL_free(recs);
                    return Fail(err, errSize, "append %d: fromPort is for tile cars", a);
                }
                for (c = 0; c < count && src == NULL; c++) {
                    if (((TileCar *)g->mem)[c].port == port) {
                        src = g->mem + c * es;
                    }
                }
                if (src == NULL && count > 0) {
                    src = g->mem;
                }
            } else if (Json_Get(je, "fromIndex") != NULL) {
                long long k = Json_GetInt(je, "fromIndex", 0);

                if (k >= 0 && k < count) {
                    src = g->mem + k * es;
                }
            } else if (Json_Get(je, "from") != NULL) {
                src = Resolve(Json_Get(je, "from"), parts, n, why, sizeof(why));
                if (src == NULL) {
                    SDL_free(recs);
                    return Fail(err, errSize, "append %d element %u: %s", a, (unsigned)j, why);
                }
                if (!InWorld(pack, src, es) || !ElementOfKind(pack, BlobOrigin(src), g->kind)) {
                    src = NULL;     /* only an element of the same kind (no other record's pointers) */
                }
            } else if (Json_Get(je, "part") != NULL) {
                LevelPart *pt = PartNamed(parts, n, Json_GetString(je, "part", NULL));

                if (pt == NULL || SDL_strcmp(pt->kind, "element") != 0 || pt->size != es) {
                    SDL_free(recs);
                    return Fail(err, errSize, "append %d element %u: no element part %s of %u bytes", a, (unsigned)j,
                                Json_GetString(je, "part", "?"), (unsigned)es);
                }
                pt->mem = dst;
                continue;
            } else {
                SDL_free(recs);
                return Fail(err, errSize, "append %d element %u: fromPort, fromIndex, from or part", a, (unsigned)j);
            }
            if (src == NULL) {
                SDL_free(recs);
                return Fail(err, errSize, "append %d element %u: the world has no element to copy there "
                            "(%s has %u)", a, (unsigned)j, Json_GetString(ja, "what", "the array"), (unsigned)count);
            }
            SDL_memcpy(dst, src, es);
            js = Json_Get(je, "set");
            for (x = js != NULL && js->type == JSON_ARRAY ? js->child : NULL; x != NULL; x = x->next) {
                const JsonNode *o = Json_At(x, 0), *hx = Json_At(x, 1);
                int len = hx != NULL && hx->type == JSON_STRING ? HexLen(hx->string) : 0;

                if (o == NULL || o->type != JSON_NUMBER || o->integer < 0 || len == 0 || o->integer + len > es ||
                    !Json_HexDecode(hx->string, dst + o->integer, (size_t)len)) {
                    SDL_free(recs);
                    return Fail(err, errSize, "append %d element %u: a set is [offset, hex] inside it", a, (unsigned)j);
                }
                {
                    /* never over a pointer of the element it copies */
                    const u8 *o2 = BlobOrigin(src);
                    long long w;

                    for (w = o->integer & ~3LL; o2 != NULL && w < o->integer + len; w += 4) {
                        int q, typed = 0;

                        for (q = 0; q < 4 && s_growable[g->kind].ptrs[q] != 0; q++) {
                            typed |= w == s_growable[g->kind].ptrs[q];
                        }
                        if (typed || NotedSlot(o2 + w)) {
                            SDL_free(recs);
                            return Fail(err, errSize, "append %d element %u: a set writes a pointer", a, (unsigned)j);
                        }
                    }
                    if (o2 == NULL) {
                        SDL_free(recs);
                        return Fail(err, errSize, "append %d element %u: a copy of a world element", a, (unsigned)j);
                    }
                }
            }
        }
    }
    /* the records: our arrays, their counts */
    for (a = 0, ja = japp->child; ja != NULL; ja = ja->next, a++) {
        struct LevelGrow *g = &s_lv.grows[a];

        if (!PatchWord(recs[a] + Json_GetInt(ja, "pointer", 0), Port_PtrToHandle(g->mem)) ||
            !PatchWord(recs[a] + Json_GetInt(ja, "count", 0), g->size / g->elemSize)) {
            SDL_free(recs);
            return Fail(err, errSize, "out of memory");
        }
    }
    SDL_free(recs);
    return 1;
}

static int StartOverride(DataPack *pack, World *world, char *err, size_t errSize) {
    const char *e = SDL_getenv("HWVX_LEVEL_START");
    f32 v[4];
    TileCar *cars;
    u32 i;

    if (e == NULL || *e == 0) {
        return 1;
    }
    if (SDL_sscanf(e, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) != 4) {
        return Fail(err, errSize, "HWVX_LEVEL_START: x,y,z,rotY");
    }
    cars = GETP(world->tileCars);
    for (i = 0; cars != NULL && i < world->numTileCars; i++) {
        if (cars[i].port == 1 && InWorld(pack, (u8 *)&cars[i], sizeof(TileCar))) {
            if (!PatchBytes((u8 *)&cars[i].pos, v, 12) || !PatchBytes((u8 *)&cars[i].rotY, &v[3], 4)) {
                return Fail(err, errSize, "out of memory");
            }
            Port_Log("level: player 1 starts at %.1f %.1f %.1f, rotY %.3f", v[0], v[1], v[2], v[3]);
            return 1;
        }
    }
    return Fail(err, errSize, "HWVX_LEVEL_START: the world has no player 1 start");
}

/* The level's records the game will follow: the trigger table (the base triggers kept,
 * as copies of keep[]'s, then the level's), its timers and variables, its volumes and
 * objectives. A level that fails is refused whole. */
static int Validate(const u8 *keep, char *err, size_t errSize) {
    char why[160];
    u32 k, t;
    int i;
    u8 *to;

    for (k = 0, t = 0; k < s_lv.origNumTriggers; k++) {
        if (keep[k] && !TriggerOk(s_lv.triggers + t++ * TRIGGER_SIZE, s_lv.origTriggers + k * TRIGGER_SIZE, why,
                                  sizeof(why))) {
            return Fail(err, errSize, "base trigger %u: %s", (unsigned)k, why);
        }
    }
    for (i = 0; i < s_lv.numParts; i++) {
        LevelPart *p = &s_lv.parts[i];

        if (SDL_strcmp(p->kind, "trigger") == 0) {
            if (!TriggerOk(p->mem, NULL, why, sizeof(why))) {
                return Fail(err, errSize, "%s: %s", p->id, why);
            }
        } else if (SDL_strcmp(p->kind, "timer") == 0) {
            /* its mode is read every frame (World.c) */
            if (!SlotOk(p->mem + offsetof(WorldTimer, mode), NULL, sizeof(WorldTimerMode), 0, &to)) {
                return Fail(err, errSize, "%s: a timer's mode is a link of the level", p->id);
            }
        } else if (SDL_strcmp(p->kind, "variable") == 0) {
            if (!SlotOk(p->mem + offsetof(WorldVariable, mode), NULL, 1, 1, &to)) {
                return Fail(err, errSize, "%s: a variable's mode is a link of the level", p->id);
            }
            if (to == NULL) {
                /* none: one of ours (the HUD reads it when the variable is shown) */
                u8 *mode = Alloc(4);

                if (mode == NULL) {
                    return Fail(err, errSize, "out of memory");
                }
                ((WorldVariable *)p->mem)->mode.h = Port_PtrToHandle(mode);
            }
        } else if (SDL_strcmp(p->kind, "volume") == 0) {
            const WorldVolume *v = (const WorldVolume *)p->mem;

            if (v->type >= VOLUME_NUM_TYPES || (v->shape != VOLUME_SHAPE_BSP && v->shape != VOLUME_SHAPE_SPHERE) ||
                !SlotOk(p->mem + offsetof(WorldVolume, bsp), NULL, 4, v->shape != VOLUME_SHAPE_BSP, &to) ||
                !SlotOk(p->mem + offsetof(WorldVolume, data), NULL, 4,
                        v->type != VOLUME_FOG && v->type != VOLUME_LIGHT, &to)) {
                return Fail(err, errSize, "%s: a volume's type, shape, BSP or data", p->id);
            }
        } else if (SDL_strcmp(p->kind, "element") == 0) {
            /* only objectives are elements of the level's own (other elements are copies of
             * the world's, whose pointers are known) */
            u8 *info, *any;
            u32 o;

            if (p->size != sizeof(WorldObjective) ||
                !SlotOk(p->mem + offsetof(WorldObjective, info), NULL, sizeof(WorldObjectiveInfo), 0, &info)) {
                return Fail(err, errSize, "%s: an objective (its info a link of the level)", p->id);
            }
            for (o = offsetof(WorldObjectiveInfo, done); o + 4 <= sizeof(WorldObjectiveInfo); o += 4) {
                if (!SlotOk(info + o, NULL, 4, 1, &any)) {
                    return Fail(err, errSize, "%s: an objective's info: +0x%x isn't a link", p->id, (unsigned)o);
                }
            }
        }
    }
    return 1;
}

static int ApplyDoc(DataPack *pack, const JsonNode *doc, char *err, size_t errSize) {
    World *world = GETP(pack->base) != NULL ? (World *)GETP(pack->base) : NULL;
    const JsonNode *jparts = Json_Get(doc, "parts"), *jkeep = Json_Get(doc, "baseTriggers"), *x;
    const JsonNode *jpatches = Json_Get(doc, "patches");
    LevelPart *parts = NULL;
    u8 *keep = NULL;
    int n = 0, i, ok = 0;
    u32 numKeep = 0, numTrig = 0, numTimers = 0, numVars = 0, k;

    if (world == NULL || pack->dataSize < sizeof(World)) {
        return Fail(err, errSize, "not a world pack");
    }
    if (SDL_strcmp(Json_GetString(doc, "format", ""), "hwvx-level") != 0 ||
        Json_GetInt(doc, "format_version", 0) != 1) {
        return Fail(err, errSize, "not a level file (format hwvx-level, format_version 1)");
    }
    s_lv.pack = pack;
    s_lv.world = world;
    s_lv.origTriggers = GETP(pack->triggers);
    s_lv.origNumTriggers = pack->numTriggers;
    s_lv.origTimers = world->timers.h;
    s_lv.origNumTimers = world->numTimers;
    s_lv.origVariables = world->variables.h;
    s_lv.origNumVariables = world->numVariables;
    s_lv.numTimerSlots = s_lv.origNumTimers;
    s_lv.numVariableSlots = s_lv.origNumVariables;

    /* the world pack's pointer slots (every check below leans on them) */
    if (s_noted.pack != pack || s_noted.pointers == NULL) {
        Fail(err, errSize, "the world pack's pointer list wasn't kept (Port_LevelNotePack)");
        goto done;
    }
    s_lv.notedBits = SDL_calloc((size_t)pack->dataSize / 128 + 1, 4);
    if (s_lv.notedBits == NULL) {
        Fail(err, errSize, "out of memory");
        goto done;
    }
    for (k = 0; k < s_noted.count; k++) {
        u32 off = s_noted.pointers[k];

        if (!(off & 3) && off <= pack->dataSize - 4) {
            s_lv.notedBits[off / 128] |= 1u << ((off / 4) % 32);
        }
    }

    /* the base triggers kept */
    keep = SDL_calloc(pack->numTriggers + 1, 1);
    if (keep == NULL) {
        return Fail(err, errSize, "out of memory");
    }
    if (jkeep == NULL || (jkeep->type == JSON_STRING && SDL_strcmp(jkeep->string, "all") == 0)) {
        SDL_memset(keep, 1, pack->numTriggers);
    } else if (jkeep->type == JSON_ARRAY) {
        for (x = jkeep->child; x != NULL; x = x->next) {
            if (x->type != JSON_NUMBER || x->integer < 0 || x->integer >= pack->numTriggers) {
                Fail(err, errSize, "baseTriggers: the world has no trigger %lld (%u)",
                     x->type == JSON_NUMBER ? x->integer : -1, (unsigned)pack->numTriggers);
                goto done;
            }
            keep[x->integer] = 1;
        }
    } else if (!(jkeep->type == JSON_STRING && SDL_strcmp(jkeep->string, "none") == 0)) {
        Fail(err, errSize, "baseTriggers: \"all\", \"none\" or a list of numbers");
        goto done;
    }
    for (k = 0; k < pack->numTriggers; k++) {
        numKeep += keep[k];
    }

    /* the parts */
    n = jparts != NULL && jparts->type == JSON_ARRAY ? jparts->count : 0;
    if (n > LEVEL_MAX_PARTS) {
        Fail(err, errSize, "%d parts (at most %d)", n, LEVEL_MAX_PARTS);
        goto done;
    }
    parts = SDL_calloc((size_t)n + 1, sizeof(LevelPart));
    if (parts == NULL) {
        Fail(err, errSize, "out of memory");
        goto done;
    }
    s_lv.parts = parts;
    s_lv.numParts = n;
    for (i = 0, x = n ? jparts->child : NULL; x != NULL; x = x->next, i++) {
        const char *hex = Json_GetString(x, "hex", "");

        parts[i].id = Json_GetString(x, "id", NULL);
        parts[i].kind = Json_GetString(x, "kind", "data");
        parts[i].size = (u32)HexLen(hex);
        if (parts[i].id == NULL || PartNamed(parts, i, parts[i].id) != NULL) {
            Fail(err, errSize, "part %d: no id, or one used twice", i);
            goto done;
        }
        if (parts[i].size > LEVEL_MAX_HEX || SDL_strlen(hex) != (size_t)parts[i].size * 2) {
            Fail(err, errSize, "%s: its hex isn't bytes (at most %d)", parts[i].id, LEVEL_MAX_HEX);
            goto done;
        }
        if (SDL_strcmp(parts[i].kind, "trigger") == 0) {
            if (parts[i].size != TRIGGER_SIZE) {
                Fail(err, errSize, "%s: a trigger is %d bytes", parts[i].id, TRIGGER_SIZE);
                goto done;
            }
            numTrig++;
        } else if (SDL_strcmp(parts[i].kind, "timer") == 0 || SDL_strcmp(parts[i].kind, "variable") == 0) {
            if (parts[i].size != 0x10) {
                Fail(err, errSize, "%s: a %s is 16 bytes", parts[i].id, parts[i].kind);
                goto done;
            }
            if (parts[i].kind[0] == 't') {
                parts[i].slot = s_lv.origNumTimers + numTimers++;
            } else {
                parts[i].slot = s_lv.origNumVariables + numVars++;
            }
            if (s_lv.origNumTimers + numTimers > DDR_MAX_TIMERS ||
                s_lv.origNumVariables + numVars > LEVEL_MAX_VARIABLES) {
                Fail(err, errSize, "%s: the world would have more than %d timers or %d variables", parts[i].id,
                     DDR_MAX_TIMERS, LEVEL_MAX_VARIABLES);
                goto done;
            }
        } else if (SDL_strcmp(parts[i].kind, "volume") == 0 && parts[i].size != sizeof(WorldVolume)) {
            Fail(err, errSize, "%s: a volume is %d bytes", parts[i].id, (int)sizeof(WorldVolume));
            goto done;
        } else if (SDL_strcmp(parts[i].kind, "data") != 0 && SDL_strcmp(parts[i].kind, "volume") != 0 &&
                   SDL_strcmp(parts[i].kind, "element") != 0) {
            Fail(err, errSize, "%s: no kind %s", parts[i].id, parts[i].kind);
            goto done;
        }
    }

    /* the arrays it grows (before the triggers are copied: their pointers move with them) */
    if (!ApplyAppends(pack, Json_Get(doc, "appends"), parts, n, err, errSize)) {
        goto done;
    }

    /* the trigger table: the kept base triggers (copies), then the level's */
    s_lv.numTriggers = numKeep + numTrig;
    s_lv.triggers = Alloc((size_t)s_lv.numTriggers * TRIGGER_SIZE + 4);
    if (s_lv.triggers == NULL) {
        Fail(err, errSize, "out of memory");
        goto done;
    }
    for (k = 0, numKeep = 0; k < s_lv.origNumTriggers; k++) {
        if (keep[k]) {
            SDL_memcpy(s_lv.triggers + numKeep++ * TRIGGER_SIZE, s_lv.origTriggers + k * TRIGGER_SIZE, TRIGGER_SIZE);
        }
    }
    for (i = 0, numTrig = 0; i < n; i++) {
        if (SDL_strcmp(parts[i].kind, "trigger") == 0) {
            parts[i].mem = s_lv.triggers + (numKeep + numTrig++) * TRIGGER_SIZE;
        } else if (SDL_strcmp(parts[i].kind, "element") == 0) {
            if (parts[i].mem == NULL) {
                Fail(err, errSize, "%s: an element no append places", parts[i].id);
                goto done;
            }
        } else if ((parts[i].mem = Alloc(parts[i].size + 4)) == NULL) {
            Fail(err, errSize, "out of memory");
            goto done;
        } else {
            parts[i].own = 1;
        }
    }
    for (i = 0, x = n ? jparts->child : NULL; x != NULL; x = x->next, i++) {
        Json_HexDecode(Json_GetString(x, "hex", ""), parts[i].mem, parts[i].size);
    }

    /* the level's variables: ids of their own (an interface binds a variable by id) */
    for (i = 0; i < n; i++) {
        if (SDL_strcmp(parts[i].kind, "variable") == 0) {
            u8 id = ((const WorldVariable *)parts[i].mem)->id;
            const u32 *slots = GETP(world->variables);
            int j, taken = 0;

            for (k = 0; slots != NULL && k < s_lv.origNumVariables; k++) {
                const WorldVariable *v = Port_HandleToPtr(slots[k]);

                taken |= v != NULL && v->id == id;
            }
            for (j = 0; j < i; j++) {
                taken |= SDL_strcmp(parts[j].kind, "variable") == 0 && ((const WorldVariable *)parts[j].mem)->id == id;
            }
            if (taken) {
                Fail(err, errSize, "%s: variable id %u is taken", parts[i].id, (unsigned)id);
                goto done;
            }
        }
    }

    /* the level's volumes, for the viewer */
    for (i = 0; i < n; i++) {
        if (SDL_strcmp(parts[i].kind, "volume") == 0 && parts[i].size >= sizeof(WorldVolume)) {
            WorldVolume **v = SDL_realloc(s_lv.volumes, (size_t)(s_lv.numVolumes + 1) * sizeof(*v));
            char **ids = SDL_realloc(s_lv.volumeIds, (size_t)(s_lv.numVolumes + 1) * sizeof(*ids));

            if (v != NULL) {
                s_lv.volumes = v;
            }
            if (ids != NULL) {
                s_lv.volumeIds = ids;
            }
            if (v == NULL || ids == NULL) {
                Fail(err, errSize, "out of memory");
                goto done;
            }
            s_lv.volumes[s_lv.numVolumes] = (WorldVolume *)parts[i].mem;
            s_lv.volumeIds[s_lv.numVolumes++] = SDL_strdup(parts[i].id);
        }
    }

    /* the timer and variable tables, grown */
    if (numTimers > 0) {
        s_lv.timers = Alloc((size_t)(s_lv.origNumTimers + numTimers) * 4);
        s_lv.numTimerSlots = s_lv.origNumTimers + numTimers;
        if (s_lv.timers == NULL) {
            Fail(err, errSize, "out of memory");
            goto done;
        }
        if (s_lv.origNumTimers > 0) {
            SDL_memcpy(s_lv.timers, GETP(world->timers), s_lv.origNumTimers * 4);
        }
    }
    if (numVars > 0) {
        s_lv.variables = Alloc((size_t)(s_lv.origNumVariables + numVars) * 4);
        s_lv.numVariableSlots = s_lv.origNumVariables + numVars;
        if (s_lv.variables == NULL) {
            Fail(err, errSize, "out of memory");
            goto done;
        }
        if (s_lv.origNumVariables > 0) {
            SDL_memcpy(s_lv.variables, GETP(world->variables), s_lv.origNumVariables * 4);
        }
    }
    for (i = 0; i < n; i++) {
        if (SDL_strcmp(parts[i].kind, "timer") == 0) {
            s_lv.timers[parts[i].slot] = Port_PtrToHandle(parts[i].mem);
        } else if (SDL_strcmp(parts[i].kind, "variable") == 0) {
            s_lv.variables[parts[i].slot] = Port_PtrToHandle(parts[i].mem);
        }
    }

    /* the links */
    for (i = 0, x = n ? jparts->child : NULL; x != NULL; x = x->next, i++) {
        const JsonNode *jl = Json_Get(x, "links"), *l;

        for (l = jl != NULL && jl->type == JSON_ARRAY ? jl->child : NULL; l != NULL; l = l->next) {
            const JsonNode *off = Json_At(l, 0);
            char why[160];
            u8 *to;

            if (off == NULL || off->type != JSON_NUMBER || off->integer < 0 || off->integer + 4 > parts[i].size ||
                (off->integer & 3)) {
                Fail(err, errSize, "%s: a link's offset isn't a word of it", parts[i].id);
                goto done;
            }
            if ((to = Resolve(Json_At(l, 1), parts, n, why, sizeof(why))) == NULL) {
                Fail(err, errSize, "%s +0x%llx: %s", parts[i].id, off->integer, why);
                goto done;
            }
            *(u32 *)(parts[i].mem + off->integer) = Port_PtrToHandle(to);
            if (!NoteLinked(parts[i].mem + off->integer)) {
                Fail(err, errSize, "out of memory");
                goto done;
            }
        }
    }

    /* the patches: numbers in the base world's records (tools/xdl_level.py writes them from
     * the schemas' number fields): inside the world (or a base element an append copied),
     * never over one of its pointers */
    for (x = jpatches != NULL && jpatches->type == JSON_ARRAY ? jpatches->child : NULL, i = 0; x != NULL;
         x = x->next, i++) {
        const char *hex = Json_GetString(x, "hex", "");
        long long off = Json_GetInt(x, "offset", 0);
        u32 size = (u32)HexLen(hex);
        char why[160];
        u8 *to = Resolve(Json_Get(x, "target"), parts, n, why, sizeof(why)), *at;
        const u8 *o;

        if (to == NULL) {
            Fail(err, errSize, "patch %d: %s", i, why);
            goto done;
        }
        if (off < 0 || off > 0x10000 || size == 0 || size > 0x1000 || SDL_strlen(hex) != (size_t)size * 2) {
            Fail(err, errSize, "patch %d: outside the record", i);
            goto done;
        }
        at = to + off;
        o = BlobOrigin(at);
        if (o == NULL || BlobOrigin(at + size - 1) != o + size - 1 || !InBlob(pack, o, size)) {
            Fail(err, errSize, "patch %d: not in the base world's records", i);
            goto done;
        }
        if (TouchesNoted(at, size)) {
            Fail(err, errSize, "patch %d: over a pointer of the world (patches set numbers)", i);
            goto done;
        }
        {
            u32 w;

            for (w = 0; w < size + ((uintptr_t)o & 3); w += 4) {
                if (TypedPointer(pack, (const u8 *)(((uintptr_t)o & ~(uintptr_t)3) + w))) {
                    Fail(err, errSize, "patch %d: over a pointer of the world (patches set numbers)", i);
                    goto done;
                }
            }
        }
        if (!PatchBytes(at, NULL, size)) {
            Fail(err, errSize, "out of memory");
            goto done;
        }
        Json_HexDecode(hex, at, size);
    }

    /* HWVX_LEVEL_START=x,y,z,rotY: player 1 starts there (the editor's "test from here") */
    if (!StartOverride(pack, world, err, errSize)) {
        goto done;
    }

    /* everything the game will follow, checked (patches included) before the pack has it */
    if (!Validate(keep, err, errSize)) {
        goto done;
    }

    /* in place */
    SETP(pack->triggers, s_lv.triggers);
    pack->numTriggers = s_lv.numTriggers;
    if (s_lv.timers != NULL) {
        SETP(world->timers, (WorldTimerSlot *)s_lv.timers);
        world->numTimers = s_lv.origNumTimers + numTimers;
    }
    if (s_lv.variables != NULL) {
        SETP(world->variables, (WorldVariableSlot *)s_lv.variables);
        world->numVariables = s_lv.origNumVariables + numVars;
    }
    ok = 1;
    Port_Log("level: %s (%s): %u triggers (%u kept), %u timers and %u variables added, %d parts, %d patches, "
             "%d arrays grown", Json_GetString(doc, "id", "?"), Json_GetString(doc, "name", "?"),
             (unsigned)s_lv.numTriggers, (unsigned)numKeep, (unsigned)numTimers, (unsigned)numVars, n,
             s_lv.numPatches, s_lv.numGrows);
done:
    SDL_free(keep);
    SDL_free(parts);
    s_lv.parts = NULL;
    s_lv.numParts = 0;
    if (!ok) {
        /* nothing of it stays: the pack's tables weren't swapped yet, the appends' and the
         * patches' bytes go back */
        s_lv.triggers = NULL;
        s_lv.timers = s_lv.variables = NULL;
        Unapply();
    }
    return ok;
}

static int ApplyFile(DataPack *pack, const char *path, const char *world, char *err, size_t errSize) {
    size_t len = 0;
    char *text, perr[160];
    JsonNode *doc;
    int ok;

    Unapply();
    if (pack == NULL) {
        return Fail(err, errSize, "no world pack");
    }
    if ((text = SDL_LoadFile(path, &len)) == NULL) {
        return Fail(err, errSize, "%s: %s", path, SDL_GetError());
    }
    doc = Json_Parse(text, len, perr, sizeof(perr));
    SDL_free(text);
    if (doc == NULL) {
        return Fail(err, errSize, "%s: %s", path, perr);
    }
    if (world != NULL && !SameWorld(Json_GetString(doc, "world", ""), world)) {
        Fail(err, errSize, "%s is made for %s, not %s", path, Json_GetString(doc, "world", "?"), world);
        Json_Free(doc);
        return 0;
    }
    ok = ApplyDoc(pack, doc, err, errSize);
    if (ok) {
        s_lv.id = SDL_strdup(Json_GetString(doc, "id", "?"));
        s_lv.path = SDL_strdup(path);
    }
    Json_Free(doc);
    return ok;
}

int Port_LevelApply(void *worldPack, const char *world, const char *path, char *err, size_t errSize) {
    /* world: the world pack's archive; a level made for another world is
     * refused (its links would walk that world's records as its own) */
    return ApplyFile(worldPack, path, world, err, errSize);
}

void Port_LevelWorldLoaded(void *worldPack, const char *world, int track) {
    const char *env = SDL_getenv("HWVX_LEVEL");
    char *path = NULL, id[256], err[256];

    Unapply();
    if (world != NULL && SDL_strncmp(world, "@mod", 4) == 0) {
        /* the level built into its own world pack (tools/xdl_level_pack.py): nothing to add */
        DataPack *pack = worldPack;
        char why[160];

        Port_Log("level: its own world pack %s applied to track %d", world, track);
        if (pack != NULL && GETP(pack->base) != NULL && SDL_getenv("HWVX_LEVEL_START") != NULL) {
            /* the editor's "test from here": player 1's start moved (undone with the pack) */
            s_lv.pack = pack;
            s_lv.world = (World *)GETP(pack->base);
            s_lv.id = SDL_strdup(world);
            if (!StartOverride(pack, s_lv.world, why, sizeof(why))) {
                Port_Log("level: %s", why);
                Unapply();
            }
        }
        return;
    }
    if (env != NULL && *env) {
        path = SDL_strdup(env);
    } else if (track >= 0 && Port_ContentIdOf(PORT_CONTENT_TRACK, (u32)track, id, sizeof(id))) {
        path = Port_ModsTrackLevel(id);
    }
    if (path == NULL) {
        return;
    }
    if (ApplyFile(worldPack, path, world, err, sizeof(err))) {
        Port_Log("level: %s applied to %s (track %d)", path, world != NULL ? world : "?", track);
    } else {
        Port_Log("level: not applied: %s", err);
    }
    SDL_free(path);
}

/* ---- test drives (HWVX_TESTDRIVE=1: the editor's) ---------------------------- */

static int TestDrive(void) {
    static int on = -1;

    if (on < 0) {
        const char *e = SDL_getenv("HWVX_TESTDRIVE");

        on = e != NULL && *e != 0 && *e != '0';
    }
    return on;
}

void Port_LevelStateChanged(int from, int to) {
    static int raced;

    (void)from;
    if (!TestDrive()) {
        return;
    }
    if (to == 5 && !raced) {
        raced = 1;
        Port_Log("testdrive: in the race after %llu ms", (unsigned long long)SDL_GetTicks());
    } else if (raced && to == 2) {
        /* the race was left for the menus: the test drive is over, back to the editor */
        SDL_Event ev;

        Port_Log("testdrive: the race was left: quitting");
        SDL_zero(ev);
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
    }
}

void Port_LevelControl(int locked, int human) {
    static int first = 1;

    if (TestDrive() && human && !locked && first) {
        first = 0;
        Port_Log("testdrive: driving after %llu ms", (unsigned long long)SDL_GetTicks());
    }
}

int Port_LevelVolume(int i, const void **volume, const char **id) {
    if (s_lv.pack == NULL || i < 0 || i >= s_lv.numVolumes) {
        return 0;
    }
    *volume = s_lv.volumes[i];
    *id = s_lv.volumeIds[i];
    return 1;
}
