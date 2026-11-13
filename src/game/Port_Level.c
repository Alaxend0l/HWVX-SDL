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
    u8 *wordKind;               /* per word of the blob: WK_* (BuildTypedMap) */
    u8 *recStart;               /* per word of the blob: 1 + the s_levelStructs index of a
                                 * record of the world that starts there */
} s_lv;

/* What a word of the world pack's blob is, as far as patches go (BuildTypedMap). */
enum {
    WK_UNKNOWN = 0,             /* not in a record the walk reached */
    WK_NUMBER,                  /* a number field: patches may set it */
    WK_POINTER,                 /* a pointer field (an x-link: object links, texture ids, handle
                                 * arrays), null in the pack or not */
    WK_COUNT,                   /* a count a link names */
    WK_INDEX,                   /* a number the game indexes with (s_indexFields) */
    WK_TRIGGER,                 /* read by a base trigger: its conditions, actions and
                                 * parameters (patches never change what a kept trigger runs) */
};

enum { LF_NONE, LF_PTR, LF_COUNT };

typedef struct LevelField {
    u32 off;
    u32 size;
    u8 kind;                    /* LF_* */
    s8 target;                  /* the s_levelStructs index the pointer leads to, -1 none */
    u32 count;                  /* 1 + the offset of its count (0: one record) */
    u32 item;                   /* the size of a record it points to */
} LevelField;

typedef struct LevelStruct {
    const char *name;
    u32 size;
    const LevelField *fields;
    u32 count;
} LevelStruct;

#include "Port_LevelFields.inc"

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

/* ---- the world's records by type (patches, typed links) ------------------- */

static int StructIndex(const char *name) {
    int i;

    for (i = 0; i < (int)SDL_arraysize(s_levelStructs); i++) {
        if (SDL_strcmp(s_levelStructs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Number fields the game indexes or switches with (tables, function tables, ids that pick
 * a record, sizes): patches never set them. By struct, offset and size. */
static const struct {
    const char *st;
    u32 off, size;
} s_indexFields[] = {
    {"WorldVolume", 0x08, 8},   /* shape (a BSP walked, or a sphere), type (creature volumes
                                 * read their data as an info, gs_aSpawnVolumes[256]) */
    {"WorldQuad", 0x00, 4},     /* id: the neighbour lists name quadrants by it */
    {"WorldQuad", 0x0C, 4},     /* numGeoms */
    {"WorldQuad", 0x20, 4},     /* numNeighbors */
    {"WorldQuad", 0x28, 8},     /* QuadPack.header, set at load */
    {"WorldQuad", 0x38, 0x18},  /* packId, fileOffset, size (Allocate(size + 0x40)), alloc */
    {"TileCar", 0x34, 4},       /* port: humanUsed[port - 1], viewOf[], at most 4 players */
    {"TileCreature", 0x1C, 4},  /* thing: a handle set at run time */
    {"Teleporter", 0x44, 8},    /* debris, emitter: handles */
    {"Teleporter", 0x50, 4},    /* numDest */
    {"TileSpeaker", 0x2C, 4},   /* soundSeq: a handle */
    {"World", 0x90, 4},         /* id: the quad packs' TOC key */
    {"WorldGameData", 0x68, 8}, /* impactFxMinVel/MaxVel: they make an index into impactFx */
    {"WorldTimer", 0x00, 4},    /* active, type (the interface binding), countUp */
    {"WorldVariable", 0x00, 4}, /* active (reads *mode, null for some), id (the binding) */
};

static void MarkWords(const u8 *p, u32 size, u8 kind, int force) {
    const u8 *base = GETP(s_lv.pack->base);
    u64 w, end;

    if (p < base || (u64)(p - base) >= s_lv.pack->dataSize) {
        return;
    }
    end = SDL_min((u64)(p - base) + size, (u64)s_lv.pack->dataSize);
    for (w = (u64)(p - base) / 4; w * 4 < end; w++) {
        if (force || s_lv.wordKind[w] == WK_UNKNOWN || s_lv.wordKind[w] == WK_NUMBER) {
            s_lv.wordKind[w] = kind;
        }
    }
}

/* The record of struct `st` at `rec` (a blob address) and every record it leads to. */
static void MarkRecord(const u8 *rec, int st, int depth) {
    const u8 *base = GETP(s_lv.pack->base);
    const LevelStruct *s = &s_levelStructs[st];
    u32 i, w;

    if (depth > 12 || rec == NULL || ((rec - base) & 3) || !InBlob(s_lv.pack, rec, s->size)) {
        return;
    }
    w = (u32)(rec - base) / 4;
    if (s_lv.recStart[w] == st + 1) {
        return;
    }
    if (s_lv.recStart[w] == 0) {
        s_lv.recStart[w] = (u8)(st + 1);
    }
    MarkWords(rec, s->size, WK_NUMBER, 0);
    for (i = 0; i < SDL_arraysize(s_indexFields); i++) {
        if (SDL_strcmp(s_indexFields[i].st, s->name) == 0) {
            MarkWords(rec + s_indexFields[i].off, s_indexFields[i].size, WK_INDEX, 0);
        }
    }
    for (i = 0; i < s->count; i++) {
        const LevelField *f = &s->fields[i];

        if (f->kind == LF_NONE) {
            continue;
        }
        MarkWords(rec + f->off, f->size, f->kind == LF_PTR ? WK_POINTER : WK_COUNT, 1);
        if (f->kind == LF_PTR && f->size == 4 && f->target >= 0 && f->item != 0 && NotedSlot(rec + f->off)) {
            const u8 *to = Port_HandleToPtr(*(const u32 *)(rec + f->off));
            u32 n = f->count != 0 ? *(const u32 *)(rec + f->count - 1) : 1, k;

            for (k = 0; k < n && k < 0x10000; k++) {
                const u8 *o = BlobOrigin(to + (u64)k * f->item);   /* (an array a level grew: its base part) */

                if (o != NULL) {
                    MarkRecord(o, f->target, depth + 1);
                }
            }
        }
    }
}

/* The typed map of the world's blob: from the World, every record the schemas' links reach
 * (and the variables, timers and objective infos the World's tables point to). */
static int BuildTypedMap(void) {
    const u8 *base = GETP(s_lv.pack->base);
    World *world = (World *)base;
    u32 words = s_lv.pack->dataSize / 4 + 1, k;
    int sv = StructIndex("WorldVariable"), stm = StructIndex("WorldTimer"), sw = StructIndex("World");

    s_lv.wordKind = SDL_calloc(words, 1);
    s_lv.recStart = SDL_calloc(words, 1);
    if (s_lv.wordKind == NULL || s_lv.recStart == NULL || sv < 0 || stm < 0 || sw < 0) {
        return 0;
    }
    MarkRecord(base, sw, 0);
    /* the variable and timer tables (handle tables: no schema struct) and what they hold */
    for (k = 0; k < 2; k++) {
        const u8 *slot = base + (k == 0 ? offsetof(World, variables) : offsetof(World, timers));
        u32 n = k == 0 ? s_lv.origNumVariables : s_lv.origNumTimers, i;
        const u32 *tab = NotedSlot(slot) ? Port_HandleToPtr(*(const u32 *)slot) : NULL;

        for (i = 0; tab != NULL && i < n && InBlob(s_lv.pack, (const u8 *)(tab + i), 4); i++) {
            if (NotedSlot((const u8 *)(tab + i))) {
                MarkRecord(BlobOrigin(Port_HandleToPtr(tab[i])), k == 0 ? sv : stm, 1);
            }
        }
    }
    /* objective infos: no schema struct; every byte is a pointer or a number the HUD uses as
     * a kind, a mode or an object id */
    {
        const WorldObjective *obj = GETP(world->objectives);

        for (k = 0; obj != NULL && k < world->numObjectives && k < 0x10000; k++) {
            const u8 *o = BlobOrigin((const u8 *)&obj[k]);

            if (o != NULL && NotedSlot(o + offsetof(WorldObjective, info))) {
                MarkWords(Port_HandleToPtr(*(const u32 *)(o + offsetof(WorldObjective, info))),
                          sizeof(WorldObjectiveInfo), WK_INDEX, 1);
            }
        }
    }
    return 1;
}

static int WordKind(const u8 *o) {
    const u8 *base = GETP(s_lv.pack->base);

    if (s_lv.wordKind == NULL || o < base || (u64)(o - base) >= s_lv.pack->dataSize) {
        return WK_UNKNOWN;
    }
    return s_lv.wordKind[(u64)(o - base) / 4];
}

/* ---- parameter lists ---------------------------------------------------------
 * Trigger.h's TAC_PARAMS_* layouts (the ones the game's functions read) as strings of item
 * codes: w a number, a a handle or a number (ANY), k/l a link/list of a record type (the
 * letter after it), o an operand, c a code variable, s scratch words, z a string, t the
 * music table, * words, x the camera switch. */
#define REC_TileCar "C"
#define REC_TilePath "P"
#define REC_Teleporter "E"
#define REC_TileCreature "K"
#define REC_QuadInstance "I"
#define REC_QuadItem "M"
#define REC_QuadFxPoint "F"
#define REC_WorldObjective "O"
#define REC_WorldVolume "V"
#define REC_WorldTimerSlot "T"
#define REC_WorldVariableSlot "R"
#define REC_NavPoint "N"
#define REC_void "?"
#define U32(n) "w"
#define S32(n) "w"
#define F32(n) "w"
#define TOKEN(n) "w"
#define ANY(n) "a"
#define LINK(n, T) "k" REC_##T
#define LIST(n, T) "l" REC_##T
#define OPERAND(n) "o"
#define CODEVAR(n) "c"
#define SCRATCH(n, L) "s"
#define STRING(n) "z"
#define TABLE(n, R) "t"
#define WORDS(n) "*"
#define VEC3(n) "www"
#define SWITCH(n, E) "x"
#define TP(x) [TRIGGER_##x] = "" TAC_PARAMS_TRIGGER_##x
#define AP(x) [ACTION_##x] = "" TAC_PARAMS_ACTION_##x
static const char *const s_trigParams[TRIGGER_NUM_TYPES] = {
    TP(TRUE), TP(AIRBOX_ENTERED), TP(AIRBOX_EXITED), TP(AIRBOX_STOP_IN), TP(DESTRUCTIBLE_KILLED),
    TP(CREATURE_KILLED), TP(ITEM_PICKUP), TP(INTERFACE_END), TP(LEVEL_START), TP(TIMER_VALUE_CHECK),
    TP(MOVEABLE_END), TP(VARIABLE_VALUE_CHECK), TP(VEHICLE_KILLED), TP(P2P_ACTIVATED), TP(CREATURE_DROPPED_OFF),
    TP(INTERFACE_PACK_END), TP(VEHICLE_DAMAGE), TP(PLAYER_VEHICLE_GEAR), TP(PLAYER_VEHICLE_IN_AIR),
    TP(ITEM_VOLUME_ENTERED), TP(CODE_VARIABLE_VALUE_CHECK), TP(CREATURE_PICKED_UP), TP(GAME_ROUNDS_COMPLETE),
    TP(GAME_ROUND_X_COMPLETE), TP(ITEM_DROPPED), TP(MOVABLE_VOLUME_ENTERED), TP(SOUND_SEQ_QUEUE_END),
    TP(CREATURE_VOLUME_ENTERED),
};
static const char *const s_actParams[ACTION_NUM_TYPES] = {
    AP(AIRBOX_CHANGE_MODE), AP(CAMERA_SET_TARGETS), AP(DESTRUCTIBLE_CHANGE_MODE), AP(FX_POINT_CHANGE_MODE),
    AP(INTERFACE_PLAY), AP(ITEM_CHANGE_MODE), AP(END_LEVEL), AP(TIMER_CHANGE_VALUE), AP(TIMER_CHANGE_MODE),
    AP(MOVEABLE_CHANGE_MODE), AP(MUSIC_CHANGE), AP(MUSIC_SET_SONG), AP(NAV_POINT_CHANGE_MODE), AP(PAUSE_GAME),
    AP(SPEAKER_CHANGE_MODE), AP(TARGET_ARROW_SET_TARGET), AP(VARIABLE_CHANGE_VALUE), AP(VEHICLE_CHANGE_MODE),
    AP(VEHICLE_AI_SET_NAMED_PATH), AP(VERTEX_COLOR_SEQ_CHANGE_MODE), AP(PRINT_DEBUG_STRING), AP(VEHICLE_TELEPORT),
    AP(VEHICLE_CHANGE_CONTROL), AP(SET_PLAYER_VEHICLE), AP(VEHICLE_REMOVE_ITEM), AP(VEHICLE_REMOVE_CREATURE),
    AP(VARIABLE_CHANGE_MODE), AP(LOAD_INTERFACE_PACK), AP(PLAY_INTERFACE_PACK), AP(UNLOAD_INTERFACE_PACK),
    AP(END_INTERFACE_PACK), AP(END_INTERFACE), AP(HUD_CHANGE_MODE), AP(SCRIPT_LIGHT_CHANGE_MODE),
    AP(CODE_VARIABLE_MODIFY_VALUE), AP(MISSION_OBJ_CHANGE_MODE), AP(PAUSE_TAC_SYSTEM), AP(SWAP_AI_SCRIPT),
    AP(RESET_GAME_ROUND), AP(ROUND_COMPLETE), AP(VEHICLE_GIVE_ITEM), AP(SET_PLAYER_ORDER), AP(PLAY_SOUND_SEQUENCE),
    AP(CREATURE_CHANGE_MODE),
};
#undef TP
#undef AP
#undef U32
#undef S32
#undef F32
#undef TOKEN
#undef ANY
#undef LINK
#undef LIST
#undef OPERAND
#undef CODEVAR
#undef SCRATCH
#undef STRING
#undef TABLE
#undef WORDS
#undef VEC3
#undef SWITCH

/* A record of the world (or one a level appended) of type `rec` starts at p. */
static int TypedAt(const u8 *p, char rec) {
    static const struct {
        char rec;
        const char *st;
        int grow;               /* its s_growable kind, -1 none */
    } types[] = {
        {'C', "TileCar", 0}, {'O', "WorldObjective", 1}, {'M', "QuadItem", 2}, {'F', "QuadFxPoint", 3},
        {'P', "TilePath", -1}, {'E', "Teleporter", -1}, {'K', "TileCreature", -1}, {'I', "QuadInstance", -1},
        {'V', "WorldVolume", -1},
    };
    const u8 *o;
    int i, a;

    if (p == NULL) {
        return 0;
    }
    if (rec == 'R' || rec == 'T') {
        /* a slot of the variable or timer table the game will have */
        const u8 *tab = rec == 'R' ? (s_lv.variables != NULL ? (const u8 *)s_lv.variables
                                                             : (const u8 *)GETP(s_lv.world->variables))
                                   : (s_lv.timers != NULL ? (const u8 *)s_lv.timers : (const u8 *)GETP(s_lv.world->timers));
        u32 n = rec == 'R' ? s_lv.numVariableSlots : s_lv.numTimerSlots;

        return tab != NULL && p >= tab && p < tab + (u64)n * 4 && ((p - tab) & 3) == 0;
    }
    if (rec == 'v') {
        return Container(p, sizeof(Vector4), NULL, NULL);   /* a point: read only */
    }
    for (i = 0; i < (int)SDL_arraysize(types); i++) {
        if (types[i].rec != rec) {
            continue;
        }
        if (rec == 'V') {
            for (a = 0; a < s_lv.numParts; a++) {
                if (s_lv.parts[a].mem == p && SDL_strcmp(s_lv.parts[a].kind, "volume") == 0) {
                    return 1;   /* a volume of the level's own (checked in Validate) */
                }
            }
        }
        for (a = 0; types[i].grow >= 0 && a < s_lv.numGrows; a++) {
            const struct LevelGrow *g = &s_lv.grows[a];

            if (g->kind == types[i].grow && p >= g->mem && p < g->mem + g->size && (p - g->mem) % g->elemSize == 0) {
                return 1;
            }
        }
        o = BlobOrigin(p);
        return o != NULL && s_lv.recStart != NULL && s_lv.recStart[(o - GETP(s_lv.pack->base)) / 4] != 0 &&
               SDL_strcmp(s_levelStructs[s_lv.recStart[(o - GETP(s_lv.pack->base)) / 4] - 1].name, types[i].st) == 0 &&
               ((o - GETP(s_lv.pack->base)) & 3) == 0;
    }
    return 0;   /* '?', 'N': records the level can't make (interfaces, sequences, scripts...) */
}

typedef struct ParamWalk {
    const u32 *w, *end;         /* the next word, the end of the block it is in */
    const u32 *start;
    char *err;
    size_t errSize;
    const char *what;
    u32 lastCount;              /* the last list's count (SCRATCH) */
} ParamWalk;

static int PwFail(ParamWalk *pw, const char *why) {
    return Fail(pw->err, pw->errSize, "%s: %s (word %u of its parameters)", pw->what, why,
                (unsigned)(pw->w - pw->start));
}

static int PwNeed(ParamWalk *pw, u64 n) {
    return (u64)(pw->end - pw->w) >= n;
}

/* h is a handle a kept base trigger's parameters hold (an interface, a sequence, a script:
 * records only the world's own triggers name, which a level may name too). */
static int BaseParamHandle(u32 h) {
    const u8 *base = GETP(s_lv.pack->base);
    u32 w, n = s_lv.pack->dataSize / 4;

    for (w = 0; s_lv.wordKind != NULL && w < n; w++) {
        if (s_lv.wordKind[w] == WK_TRIGGER && ((const u32 *)base)[w] == h && NotedSlot(base + w * 4)) {
            return 1;
        }
    }
    return 0;
}

/* The handle word at slot: zero (nullOk), the world's own pointer (unchanged: patches never
 * set one), or a link of the level to a record of type rec (of a kind the level can't make:
 * one a kept base trigger names). */
static int HandleOk(const u32 *slot, char rec, int nullOk) {
    const u8 *o;

    if (*slot == 0) {
        return nullOk;
    }
    o = BlobOrigin((const u8 *)slot);
    if (o != NULL && NotedSlot(o)) {
        return 1;
    }
    if (!Linked((const u8 *)slot)) {
        return 0;
    }
    return rec == '?' || rec == 'N' ? BaseParamHandle(*slot) : TypedAt(Port_HandleToPtr(*slot), rec);
}

static int PwHandle(ParamWalk *pw, char rec, int nullOk) {
    if (!PwNeed(pw, 1)) {
        return PwFail(pw, "runs past its block");
    }
    if (!HandleOk(pw->w, rec, nullOk)) {
        return PwFail(pw, rec == '?' || rec == 'N' ? "a handle that isn't the world's own"
                                                    : "a handle that isn't a record of the right kind");
    }
    pw->w++;
    return 1;
}

static int PwList(ParamWalk *pw, char rec) {
    u32 n, i;

    if (!PwNeed(pw, 1) || (n = *pw->w) > 0x10000 || !PwNeed(pw, (u64)n + 1)) {
        return PwFail(pw, "a list runs past its block");
    }
    pw->w++;
    pw->lastCount = n;
    for (i = 0; i < n; i++) {
        if (!PwHandle(pw, rec, 0)) {
            return 0;
        }
    }
    return 1;
}

/* GetCodeVariable/SetCodeVariable's argument for code variable var (GameSettings.c's tables:
 * the ranges they index without checking). */
static int CodeArgOk(u32 var, u32 arg) {
    if (var == 0xC9 || var == 0xCB || var == 0xCC || var == 0xCE || var == 0xD1 || var == 0xD2 ||
        (var >= 0x191 && var <= 0x194)) {
        return arg >= 1 && arg <= 4;
    }
    if (var >= 0x6A && var <= 0x6C) {
        return arg <= 10;
    }
    return 1;
}

static int PwCodeFail(ParamWalk *pw, u32 var, u32 arg) {
    char why[80];

    SDL_snprintf(why, sizeof(why), "code variable 0x%X's argument %u past its table", (unsigned)var, (unsigned)arg);
    return PwFail(pw, why);
}

static int CarCodeVar(u32 var) {
    return var == 1 || var == 2 || var == 3 || (var >= 20 && var <= 27);
}

static int PwOperand(ParamWalk *pw) {
    if (!PwNeed(pw, 1)) {
        return PwFail(pw, "runs past its block");
    }
    switch (*pw->w++) {
    case OPERAND_CONSTANT:
        if (!PwNeed(pw, 1)) {
            return PwFail(pw, "runs past its block");
        }
        pw->w++;
        return 1;
    case OPERAND_VARIABLE:
        return PwHandle(pw, 'R', 0);
    case OPERAND_CODE_VARIABLE: {
        u32 var;

        if (!PwNeed(pw, 2)) {
            return PwFail(pw, "runs past its block");
        }
        var = *pw->w++;
        if (CarCodeVar(var)) {
            return PwHandle(pw, 'C', 0);
        }
        if (!CodeArgOk(var, *pw->w)) {
            return PwCodeFail(pw, var, *pw->w);
        }
        pw->w++;
        return 1;
    }
    default:
        pw->w--;
        return PwFail(pw, "no such operand type");
    }
}

static int PwCodeVar(ParamWalk *pw) {
    u32 var;

    if (!PwNeed(pw, 1)) {
        return PwFail(pw, "runs past its block");
    }
    var = *pw->w++;
    if (var < 100) {
        return PwList(pw, 'C');
    }
    if (!PwNeed(pw, 1)) {
        return PwFail(pw, "runs past its block");
    }
    if (!CodeArgOk(var, *pw->w)) {
        return PwCodeFail(pw, var, *pw->w);
    }
    pw->w++;
    return 1;
}

/* A camera place (Camera_TriggerActionGetLocation). */
static int PwPlace(ParamWalk *pw) {
    u32 kind;
    int ok;

    if (!PwNeed(pw, 1)) {
        return PwFail(pw, "runs past its block");
    }
    kind = *pw->w++;
    switch (kind) {
    case PLACE_CAR: ok = PwHandle(pw, 'C', 0); break;
    case PLACE_INSTANCE: ok = PwHandle(pw, 'I', 0); break;
    case PLACE_POINT:
    case PLACE_POINT4: ok = PwHandle(pw, 'v', 0); break;
    case PLACE_PATH: ok = PwHandle(pw, 'P', 0); break;
    case PLACE_CAMERA: return 1;
    default: pw->w--; return PwFail(pw, "no such camera place");
    }
    if (!ok || !PwNeed(pw, 3)) {
        return ok ? PwFail(pw, "runs past its block") : 0;
    }
    pw->w += 3;
    return 1;
}

static int PwCamera(ParamWalk *pw) {
    u32 mode, n;

    if (!PwNeed(pw, 1)) {
        return PwFail(pw, "runs past its block");
    }
    mode = *pw->w++;
    n = mode == CAMERA_LOOK_AT ? 1 : mode == CAMERA_ORBIT ? 4 : 0;
    if (mode < CAMERA_LOOK_AT || mode > CAMERA_PLAYER) {
        pw->w--;
        return PwFail(pw, "no such camera mode");
    }
    if (!PwNeed(pw, n)) {
        return PwFail(pw, "runs past its block");
    }
    pw->w += n;
    return PwPlace(pw) && PwPlace(pw);
}

static int PwString(ParamWalk *pw) {
    const char *s = (const char *)pw->w;
    size_t room = (size_t)(pw->end - pw->w) * 4, n;

    for (n = 0; n < room && s[n] != 0; n++) {
    }
    if (n >= room || n >= 0x70) {
        return PwFail(pw, "a name not ended in its block (or 112 characters or more)");
    }
    if (SDL_strstr(s, "..") != NULL || SDL_strchr(s, '/') != NULL || SDL_strchr(s, ':') != NULL) {
        return PwFail(pw, "a pack name with a path in it");
    }
    pw->w += (n + 4) / 4;
    return 1;
}

static int StringTableOk(const u8 *slot, u32 max);
static int s_strictBase;     /* HWVX_LEVEL_SWEEP: the base triggers' lists walked too */

/* The parameter list at p of a trigger/condition (action 0) or action (action 1) of `type`,
 * as the game's function will read it: every handle a record of the right kind (or the
 * world's own), every index inside its table, every list inside the block. A level's own
 * list only: one in the world pack's blob that a base trigger reads is the world's (patches
 * never change it). */
static int ParamsOk(const u8 *p, int action, u32 type, const char *what, char *err, size_t errSize) {
    const char *spec = action ? s_actParams[type] : s_trigParams[type];
    const u8 *lo;
    u64 len;
    ParamWalk pw;
    const char *c;

    if (spec == NULL || spec[0] == 0) {
        return 1;   /* reads none */
    }
    if (p == NULL) {
        return Fail(err, errSize, "%s: no parameters", what);
    }
    if (!s_strictBase && WordKind(BlobOrigin(p)) == WK_TRIGGER) {
        return 1;
    }
    if (!Container(p, 4, &lo, &len) || ((uintptr_t)p & 3)) {
        return Fail(err, errSize, "%s: its parameters aren't in a block", what);
    }
    SDL_zero(pw);
    pw.w = pw.start = (const u32 *)p;
    pw.end = (const u32 *)(lo + (len & ~(u64)3));
    pw.err = err;
    pw.errSize = errSize;
    pw.what = what;
    if (!action && type == TRIGGER_P2P_ACTIVATED && s_strictBase) {
        return 1;
    }
    if (!action && type == TRIGGER_P2P_ACTIVATED) {
        return PwFail(&pw, "a p2p trigger of the level's own (its words are thing handles)");
    }
    for (c = spec; *c; c++) {
        int ok = 1;

        switch (*c) {
        case 'w':
            if (!PwNeed(&pw, 1)) {
                return PwFail(&pw, "runs past its block");
            }
            pw.w++;
            break;
        case 'a':
            /* a handle or a number: the game follows it (scripts, messages, sequences,
             * items) except END_INTERFACE's, which it only compares */
            if (action && type == ACTION_END_INTERFACE) {
                ok = PwNeed(&pw, 1) ? (pw.w++, 1) : PwFail(&pw, "runs past its block");
            } else if (action && type == ACTION_VEHICLE_GIVE_ITEM && pw.w > pw.start && pw.w[-1] == 0) {
                ok = PwHandle(&pw, 'M', 1);   /* what 0: a pickup */
            } else if (action && type == ACTION_VEHICLE_GIVE_ITEM && PwNeed(&pw, 1) && *pw.w < 1000) {
                pw.w++;                       /* what 1: an item id */
            } else if (action && type == ACTION_PRINT_DEBUG_STRING && pw.start[0] >= 2 && pw.start[0] <= 8) {
                /* a message: a string table (the HUD shows it, a car's stunt messages) */
                if (!PwNeed(&pw, 1)) {
                    return PwFail(&pw, "runs past its block");
                }
                if (!HandleOk(pw.w, '?', 1) && !StringTableOk((const u8 *)pw.w, 0x80)) {
                    return PwFail(&pw, "a message that isn't a string table (strings under 128 characters)");
                }
                pw.w++;
            } else if (action && type == ACTION_PRINT_DEBUG_STRING) {
                ok = PwNeed(&pw, 1) ? (pw.w++, 1) : PwFail(&pw, "runs past its block");   /* a number */
            } else {
                ok = PwHandle(&pw, '?', 1);
            }
            break;
        case 'k': {
            char rec = *++c;
            int nullOk = rec == '?';

            if (action && type == ACTION_SET_PLAYER_ORDER) {
                ok = PwNeed(&pw, 1) ? (pw.w++, 1) : PwFail(&pw, "runs past its block");   /* compared only */
                break;
            }
            if (action && type == ACTION_TARGET_ARROW_SET_TARGET && pw.w > pw.start) {
                u32 kind = pw.w[-1];

                rec = kind == 1 ? 'C' : kind == 2 ? 'I' : kind == 3 ? 'M' : kind == 4 ? 'P' : kind == 6 ? 'v' : 0;
                if (rec == 0) {
                    ok = PwNeed(&pw, 1) ? (pw.w++, 1) : PwFail(&pw, "runs past its block");   /* no target */
                    break;
                }
                nullOk = kind == 6;
            }
            ok = PwHandle(&pw, rec, nullOk);
            break;
        }
        case 'l':
            ok = PwList(&pw, *++c);
            break;
        case 'o':
            ok = PwOperand(&pw);
            break;
        case 'c':
            ok = PwCodeVar(&pw);
            break;
        case 's':
            if (!PwNeed(&pw, pw.lastCount)) {
                return PwFail(&pw, "its scratch words run past its block");
            }
            pw.w += pw.lastCount;
            break;
        case 'z':
            ok = PwString(&pw);
            break;
        case 't': {
            /* MusicChange's tracks: `track` indexes g_tracks[32], read at two strides
             * (0x28 and the GameCube's 0x24, AudioManager.c) */
            u32 n, i;

            if (!PwNeed(&pw, 1) || (n = *pw.w) > 64 || !PwNeed(&pw, 1 + (u64)n * 10)) {
                return PwFail(&pw, "the music table runs past its block");
            }
            for (i = 0; i < n; i++) {
                if (pw.w[2 + 10 * i] >= 32 || pw.w[2 + 9 * i] >= 32) {
                    return PwFail(&pw, "a music track past the 32 tracks");
                }
            }
            pw.w += 1 + n * 10;
            break;
        }
        case '*':
            return 1;   /* the rest: not read (P2P, the one that reads them, refused above) */
        case 'x':
            ok = PwCamera(&pw);
            break;
        default:
            return PwFail(&pw, "no such item");
        }
        if (!ok) {
            return 0;
        }
    }
    if (!action && type == TRIGGER_VEHICLE_DAMAGE) {
        /* cars, test, amount: 0x2B divides by amount */
        const u32 *x = pw.start + 1 + pw.start[0];

        if (x[0] == TAC_DAMAGE_BELOW && x[1] == 0) {
            return Fail(err, errSize, "%s: a damage test dividing by 0", what);
        }
    }
    return 1;
}

/* The base triggers' records and parameters the kept triggers read: marked WK_TRIGGER (patches
 * never change them, so their contents stay the world's). The same walk as ParamsOk, only
 * marking. */
static void MarkTriggerData(const u8 *orig) {
    const Trigger *t = (const Trigger *)orig;
    u32 i, k;

    for (k = 0; k < 3; k++) {
        u32 n = k == 0 ? t->numConditions : k == 1 ? t->numActions : t->numElseActions;
        u32 size = k == 0 ? TRIGGER_SIZE : sizeof(TriggerAction);
        const u8 *slot = orig + (k == 0 ? 0x0C : k == 1 ? 0x14 : 0x18);
        const u8 *recs;

        if (n == 0 || n > 0x10000 || !NotedSlot(slot)) {
            continue;
        }
        recs = Port_HandleToPtr(*(const u32 *)slot);
        MarkWords(recs, n * size, WK_TRIGGER, 1);
        for (i = 0; i < n && InBlob(s_lv.pack, recs, n * size); i++) {
            const u8 *r = recs + i * size;
            const u8 *p = NotedSlot(r + 4) ? Port_HandleToPtr(*(const u32 *)(r + 4)) : NULL;
            const u8 *base = GETP(s_lv.pack->base);

            if (p != NULL && InBlob(s_lv.pack, p, 4)) {
                /* to the end of its parameters: up to the next record the walk reached, at
                 * most 1 KB (no list is longer) */
                u32 w = (u32)(p - base) / 4, e;

                for (e = w; e < w + 256 && (u64)e * 4 < s_lv.pack->dataSize; e++) {
                    if (e > w && s_lv.recStart[e] != 0) {
                        break;
                    }
                    if (s_lv.wordKind[e] == WK_UNKNOWN || s_lv.wordKind[e] == WK_TRIGGER) {
                        s_lv.wordKind[e] = WK_TRIGGER;
                    } else if (e > w) {
                        break;
                    }
                }
            }
        }
    }
    {
        const u8 *p = NotedSlot(orig + 4) ? Port_HandleToPtr(*(const u32 *)(orig + 4)) : NULL;

        if (p != NULL && InBlob(s_lv.pack, p, 4)) {
            const u8 *base = GETP(s_lv.pack->base);
            u32 w = (u32)(p - base) / 4, e;

            for (e = w; e < w + 256 && (u64)e * 4 < s_lv.pack->dataSize; e++) {
                if (e > w && s_lv.recStart[e] != 0) {
                    break;
                }
                if (s_lv.wordKind[e] == WK_UNKNOWN || s_lv.wordKind[e] == WK_TRIGGER) {
                    s_lv.wordKind[e] = WK_TRIGGER;
                } else if (e > w) {
                    break;
                }
            }
        }
    }
    MarkWords(orig, TRIGGER_SIZE, WK_TRIGGER, 1);
}

/* ---- volumes and objectives of the level's own ------------------------------- */

/* A BSP the game walks (BSPN0.c): 0x20-byte nodes, a node going on to the next (type bit 0)
 * or `next` nodes on (bit 1). Every node it can reach inside the block it starts in, and no
 * path deeper than the game's recursion should go. */
static int BspOk(const u8 *bsp) {
    const u8 *lo;
    u64 len;
    u32 n, i, *depth;
    int ok = 1;

    if (!Container(bsp, 0x20, &lo, &len) || ((uintptr_t)(bsp - lo) & 3)) {
        return 0;
    }
    n = (u32)SDL_min((len - (u64)(bsp - lo)) / 0x20, 0x10000);
    depth = SDL_calloc((size_t)n + 1, 4);
    if (depth == NULL) {
        return 0;
    }
    depth[0] = 1;   /* reached, at depth 1 (nodes only point forwards) */
    for (i = 0; i < n && ok; i++) {
        const u8 *node = bsp + (u64)i * 0x20;
        u8 type = node[0x10];
        u16 next = *(const u16 *)(node + 0x12);

        if (depth[i] == 0) {
            continue;
        }
        if (depth[i] > 1024) {
            ok = 0;
            break;
        }
        if (type & 1) {
            if (i + 1 >= n) {
                ok = 0;
            } else {
                depth[i + 1] = SDL_max(depth[i + 1], depth[i] + 1);
            }
        }
        if (type & 2) {
            if (next == 0 || (u64)i + next >= n) {
                ok = 0;
            } else {
                depth[i + next] = SDL_max(depth[i + next], depth[i] + 1);
            }
        }
    }
    SDL_free(depth);
    return ok;
}

/* The bytes a volume's data holds, by type (what VolumeManager.c, Thing.c and LightManager.c
 * read; viewer_level.c's table). */
static u32 VolumeDataSize(u32 type) {
    switch (type) {
    case VOLUME_FORCE_BOX: return 0x14;
    case VOLUME_REVERB: return 8;
    case VOLUME_FOG: return 16;
    case VOLUME_LIGHT: return (u32)sizeof(Light);
    case VOLUME_CREATURE: return 1;
    default: return 0;
    }
}

/* A string table (InterfaceManager_SetObjectText): string handles up to a null one, each a
 * string ended in its block, shorter than `max` (the objective text is copied into the
 * object's savedText[0x34]). */
static int StringTableOk(const u8 *slot, u32 max) {
    u8 *tab;
    u32 i;

    if (!SlotOk(slot, NULL, 4, 1, &tab)) {
        return 0;
    }
    for (i = 0; tab != NULL && i < 64; i++) {
        const u8 *lo;
        u64 len, k;
        u8 *str;

        if (!SlotOk(tab + i * 4, NULL, 1, 1, &str)) {
            return 0;
        }
        if (str == NULL) {
            return 1;
        }
        if (!Container(str, 1, &lo, &len)) {
            return 0;
        }
        for (k = 0; (u64)(str - lo) + k < len && str[k] != 0; k++) {
        }
        if ((u64)(str - lo) + k >= len || k >= max) {
            return 0;
        }
        if (!Container(tab + (i + 1) * 4, 4, NULL, NULL)) {
            return 0;
        }
    }
    return tab == NULL;
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
    SDL_free(s_lv.wordKind);
    SDL_free(s_lv.recStart);
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
/* A level trigger's parameter lists: its own, its conditions' and its actions' (TriggerOk
 * has checked the arrays and types). */
static int TriggerParamsOk(const u8 *t, char *err, size_t errSize) {
    const Trigger *tr = (const Trigger *)t;
    char what[48];
    u32 k, i;

    if (!ParamsOk(Port_HandleToPtr(tr->param.h), 0, tr->type, "its test", err, errSize)) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        u32 n = k == 0 ? tr->numConditions : k == 1 ? tr->numActions : tr->numElseActions;
        u32 size = k == 0 ? TRIGGER_SIZE : sizeof(TriggerAction);
        const u8 *recs = n != 0 ? Port_HandleToPtr(*(const u32 *)(t + (k == 0 ? 0x0C : k == 1 ? 0x14 : 0x18))) : NULL;

        for (i = 0; i < n; i++) {
            const u8 *r = recs + (u64)i * size;

            SDL_snprintf(what, sizeof(what), "%s %u", k == 0 ? "condition" : k == 1 ? "action" : "else action",
                         (unsigned)i);
            if (!ParamsOk(Port_HandleToPtr(*(const u32 *)(r + 4)), k != 0, *(const u32 *)r, what, err, errSize)) {
                return 0;
            }
        }
    }
    return 1;
}

static int Validate(const u8 *keep, char *err, size_t errSize) {
    char why[160];
    u32 k, t;
    int i;
    u8 *to;

    for (k = 0, t = 0; k < s_lv.origNumTriggers; k++) {
        if (!keep[k]) {
            continue;
        }
        if (!TriggerOk(s_lv.triggers + t * TRIGGER_SIZE, s_lv.origTriggers + k * TRIGGER_SIZE, why, sizeof(why)) ||
            (s_strictBase && !TriggerParamsOk(s_lv.triggers + t * TRIGGER_SIZE, why, sizeof(why)))) {
            return Fail(err, errSize, "base trigger %u: %s", (unsigned)k, why);
        }
        t++;
    }
    for (i = 0; i < s_lv.numParts; i++) {
        LevelPart *p = &s_lv.parts[i];

        if (SDL_strcmp(p->kind, "trigger") == 0) {
            if (!TriggerOk(p->mem, NULL, why, sizeof(why)) || !TriggerParamsOk(p->mem, why, sizeof(why))) {
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

            u8 *bsp;

            if (v->type >= VOLUME_NUM_TYPES || (v->shape != VOLUME_SHAPE_BSP && v->shape != VOLUME_SHAPE_SPHERE) ||
                !SlotOk(p->mem + offsetof(WorldVolume, bsp), NULL, 0x20, v->shape != VOLUME_SHAPE_BSP, &bsp) ||
                !SlotOk(p->mem + offsetof(WorldVolume, data), NULL, SDL_max(VolumeDataSize(v->type), 4),
                        VolumeDataSize(v->type) == 0, &to)) {
                return Fail(err, errSize, "%s: a volume's type, shape, BSP or data (sized for its type)", p->id);
            }
            /* a BSP of the level's own is walked by the game node to node: every node it can
             * reach inside its block (one of the world's is the world's) */
            if (v->shape == VOLUME_SHAPE_BSP && BlobOrigin(bsp) == NULL && !BspOk(bsp)) {
                return Fail(err, errSize, "%s: its BSP's nodes lead out of its block (or too deep)", p->id);
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
            {
                /* InterfaceManager_SetMissionObjectiveText: done's value; the value its type
                 * shows (1: a variable, 2: a timer, 3: a code variable's id and argument); the
                 * texts, copied into a 0x34-byte buffer when a value goes in them */
                const WorldObjectiveInfo *oi = (const WorldObjectiveInfo *)info;
                u32 vsize = oi->type == 1 || oi->type == 2 ? 0x10 : oi->type == 3 ? 8 : 4;
                u8 *val;

                (void)o;
                if (!SlotOk(info + offsetof(WorldObjectiveInfo, done), NULL, sizeof(WorldVariable), 1, &any) ||
                    !SlotOk(info + offsetof(WorldObjectiveInfo, value), NULL, vsize, oi->type == 0 || oi->type > 3,
                            &val)) {
                    return Fail(err, errSize, "%s: an objective's done or value (sized for its type %u)", p->id,
                                (unsigned)oi->type);
                }
                if (oi->type == 3 && !CarCodeVar(((const u32 *)val)[0]) && ((const u32 *)val)[0] >= 10 &&
                    !CodeArgOk(((const u32 *)val)[0], ((const u32 *)val)[1])) {
                    return Fail(err, errSize, "%s: an objective's code variable argument past its table", p->id);
                }
                if (oi->type == 3 && (CarCodeVar(((const u32 *)val)[0]) || ((const u32 *)val)[0] >= 10)) {
                    /* a car's (by a TileCar handle): only the level's own objectives' code
                     * variables of the game settings */
                    if (CarCodeVar(((const u32 *)val)[0])) {
                        return Fail(err, errSize, "%s: an objective's code variable of a car", p->id);
                    }
                }
                if (!StringTableOk(info + offsetof(WorldObjectiveInfo, text), oi->type != 0 ? 0x34 : 0x80) ||
                    !StringTableOk(info + offsetof(WorldObjectiveInfo, doneText), oi->type != 0 ? 0x34 : 0x80)) {
                    return Fail(err, errSize, "%s: an objective's texts (string handles of the level, ended, "
                                "under 52 characters when a value goes in)", p->id);
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

    /* the world's records by type, and what the kept base triggers read (patches never set
     * a pointer, a count, an index or a trigger's data) */
    if (!BuildTypedMap()) {
        Fail(err, errSize, "out of memory");
        goto done;
    }
    for (k = 0; k < s_lv.origNumTriggers; k++) {
        MarkTriggerData(s_lv.origTriggers + k * TRIGGER_SIZE);   /* (kept or not: what they name is the world's) */
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
        {
            static const char *const kinds[] = {"outside the records the world's links reach", NULL,
                                                "over a pointer", "over a count", "over a number the game indexes with",
                                                "over what a kept base trigger reads"};
            u32 w;

            for (w = 0; w < size + ((uintptr_t)o & 3); w += 4) {
                int kind = WordKind((const u8 *)(((uintptr_t)o & ~(uintptr_t)3) + w));

                if (kind != WK_NUMBER) {
                    Fail(err, errSize, "patch %d: %s", i, kinds[kind]);
                    goto done;
                }
            }
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

static void CheckList(void *worldPack, const char *world);

void Port_LevelWorldLoaded(void *worldPack, const char *world, int track) {
    const char *env = SDL_getenv("HWVX_LEVEL");
    char *path = NULL, id[256], err[256];

    Unapply();
    CheckList(worldPack, world);
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

/* For the viewer (Port_Level.h): the arrays the appends grew, mapped by
 * element index between the base array in the pack and our copy. */
void *Port_LevelLiveOf(const void *p) {
    int a;

    for (a = 0; s_lv.pack != NULL && a < s_lv.numGrows; a++) {
        const struct LevelGrow *g = &s_lv.grows[a];

        if (g->old != NULL && g->mem != NULL && (const u8 *)p >= g->old && (const u8 *)p < g->old + g->oldSize) {
            return g->mem + ((const u8 *)p - g->old);
        }
    }
    return (void *)p;
}

const void *Port_LevelBaseOf(const void *p) {
    int a;

    for (a = 0; s_lv.pack != NULL && a < s_lv.numGrows; a++) {
        const struct LevelGrow *g = &s_lv.grows[a];

        if (g->mem != NULL && (const u8 *)p >= g->mem && (const u8 *)p < g->mem + g->size) {
            u32 at = (u32)((const u8 *)p - g->mem);

            return at < g->oldSize && g->old != NULL ? g->old + at : NULL;
        }
    }
    return p;
}

int Port_LevelVolume(int i, const void **volume, const char **id) {
    if (s_lv.pack == NULL || i < 0 || i >= s_lv.numVolumes) {
        return 0;
    }
    *volume = s_lv.volumes[i];
    *id = s_lv.volumeIds[i];
    return 1;
}

/* ---- tests -------------------------------------------------------------------- */

/* HWVX_LEVEL_SWEEP=1 (hwvx --convert-xdl, which loads every pack): each world pack gets an
 * empty level that keeps every base trigger, its parameter lists walked as a level's are;
 * "levelsweep: NAME: ok" or why not. Shows the checks agree with every retail world. */
void Port_LevelSweep(void *worldPack, const char *name) {
    static const char doc[] = "{\"format\": \"hwvx-level\", \"format_version\": 1, \"id\": \"sweep\"}";
    static int on = -1;
    char err[256], perr[160];
    JsonNode *d;
    int ok;

    if (on < 0) {
        const char *e = SDL_getenv("HWVX_LEVEL_SWEEP");

        on = e != NULL && *e == '1';
    }
    if (!on || worldPack == NULL) {
        return;
    }
    Unapply();
    d = Json_Parse(doc, sizeof(doc) - 1, perr, sizeof(perr));
    s_strictBase = 1;
    ok = d != NULL && ApplyDoc(worldPack, d, err, sizeof(err));
    s_strictBase = 0;
    Port_Log("levelsweep: %s: %s", name, ok ? "ok" : d == NULL ? perr : err);
    Unapply();
    Json_Free(d);
}

/* HWVX_LEVEL_CHECK=FILE (a list of level files, one a line): as the world loads, each is
 * applied and taken out again, "levelcheck: PATH: applied" or "...: not applied: why"; then
 * the world goes on as it would. tools/xdlrs's `level refuse-test`. */
static void CheckList(void *worldPack, const char *world) {
    const char *list = SDL_getenv("HWVX_LEVEL_CHECK");
    char *text, *line, *next, err[256];
    size_t len;

    if (list == NULL || *list == 0 || (text = SDL_LoadFile(list, &len)) == NULL) {
        return;
    }
    for (line = text; line != NULL && *line; line = next) {
        next = SDL_strchr(line, '\n');
        if (next != NULL) {
            *next++ = 0;
        }
        if (*line == 0) {
            continue;
        }
        if (ApplyFile(worldPack, line, world, err, sizeof(err))) {
            Port_Log("levelcheck: %s: applied", line);
        } else {
            Port_Log("levelcheck: %s: not applied: %s", line, err);
        }
        Unapply();
    }
    SDL_free(text);
}
