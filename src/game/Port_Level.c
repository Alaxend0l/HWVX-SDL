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
 *    "patches": [{"target": LINK, "offset": n, "hex": "..."}, ...]}
 *
 * A part is one record of the level's own, in host byte order (little-endian
 * words) as the game has pack data once loaded, with its pointer slots zero
 * and listed in `links`. LINK is where a slot points:
 *
 *   {"part": ID, "add": n}         a part of the level (n bytes into it)
 *   {"slot": ID}                   a timer's or variable's entry in the world's
 *                                  table (script parameters name those by entry)
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
 * (old bytes kept). Everything is undone when the pack goes, or before a level
 * is applied again. */
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
    u8 *old;
    u32 size;
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
    void **allocs;
    int numAllocs, capAllocs;
    LevelPatch *patches;
    int numPatches;
    WorldVolume **volumes;      /* the level's own volumes (parts of kind "volume") */
    char **volumeIds;
    int numVolumes;
} s_lv;

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
        SDL_memcpy(s_lv.patches[i].at, s_lv.patches[i].old, s_lv.patches[i].size);
        SDL_free(s_lv.patches[i].old);
    }
    SDL_free(s_lv.patches);
    for (i = 0; i < s_lv.numAllocs; i++) {
        SDL_aligned_free(s_lv.allocs[i]);
    }
    SDL_free(s_lv.allocs);
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
        return p->mem + add;
    }
    if (Json_Get(link, "slot") != NULL) {
        p = PartNamed(parts, n, Json_GetString(link, "slot", NULL));
        if (p == NULL || (SDL_strcmp(p->kind, "timer") != 0 && SDL_strcmp(p->kind, "variable") != 0)) {
            Fail(err, errSize, "no timer or variable %s", Json_GetString(link, "slot", "?"));
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
            if (!InBlob(s_lv.pack, cur + v, 4)) {
                Fail(err, errSize, "a pointer step leaves the world pack");
                return NULL;
            }
            cur = Port_HandleToPtr(*(u32 *)(cur + v));
            if (cur == NULL) {
                Fail(err, errSize, "a pointer on the way is null in this release's world");
                return NULL;
            }
        } else {
            cur += v;
        }
        if (!InBlob(s_lv.pack, cur, 1)) {
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
    LevelPatch *grown = SDL_realloc(s_lv.patches, (size_t)(s_lv.numPatches + 1) * sizeof(LevelPatch));

    if (grown == NULL) {
        return 0;
    }
    s_lv.patches = grown;
    pt = &s_lv.patches[s_lv.numPatches];
    if ((pt->old = SDL_malloc(size)) == NULL) {
        return 0;
    }
    pt->at = at;
    pt->size = size;
    SDL_memcpy(pt->old, at, size);
    SDL_memcpy(at, data, size);
    s_lv.numPatches++;
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
        if (cars[i].port == 1 && InBlob(pack, (u8 *)&cars[i], sizeof(TileCar))) {
            if (!PatchBytes((u8 *)&cars[i].pos, v, 12) || !PatchBytes((u8 *)&cars[i].rotY, &v[3], 4)) {
                return Fail(err, errSize, "out of memory");
            }
            Port_Log("level: player 1 starts at %.1f %.1f %.1f, rotY %.3f", v[0], v[1], v[2], v[3]);
            return 1;
        }
    }
    return Fail(err, errSize, "HWVX_LEVEL_START: the world has no player 1 start");
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
    parts = SDL_calloc((size_t)n + 1, sizeof(LevelPart));
    if (parts == NULL) {
        Fail(err, errSize, "out of memory");
        goto done;
    }
    for (i = 0, x = n ? jparts->child : NULL; x != NULL; x = x->next, i++) {
        const char *hex = Json_GetString(x, "hex", "");

        parts[i].id = Json_GetString(x, "id", NULL);
        parts[i].kind = Json_GetString(x, "kind", "data");
        parts[i].size = (u32)HexLen(hex);
        if (parts[i].id == NULL || PartNamed(parts, i, parts[i].id) != NULL) {
            Fail(err, errSize, "part %d: no id, or one used twice", i);
            goto done;
        }
        if (SDL_strcmp(parts[i].kind, "trigger") == 0) {
            if (parts[i].size != TRIGGER_SIZE) {
                Fail(err, errSize, "%s: a trigger is %d bytes", parts[i].id, TRIGGER_SIZE);
                goto done;
            }
            numTrig++;
        } else if (SDL_strcmp(parts[i].kind, "timer") == 0) {
            parts[i].slot = s_lv.origNumTimers + numTimers++;
        } else if (SDL_strcmp(parts[i].kind, "variable") == 0) {
            parts[i].slot = s_lv.origNumVariables + numVars++;
        } else if (SDL_strcmp(parts[i].kind, "data") != 0 && SDL_strcmp(parts[i].kind, "volume") != 0) {
            Fail(err, errSize, "%s: no kind %s", parts[i].id, parts[i].kind);
            goto done;
        }
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
        }
    }

    /* the patches */
    if (jpatches != NULL && jpatches->type == JSON_ARRAY && jpatches->count > 0) {
        s_lv.patches = SDL_calloc((size_t)jpatches->count + 1, sizeof(LevelPatch));
        if (s_lv.patches == NULL) {
            Fail(err, errSize, "out of memory");
            goto done;
        }
        for (x = jpatches->child; x != NULL; x = x->next) {
            const char *hex = Json_GetString(x, "hex", "");
            long long off = Json_GetInt(x, "offset", 0);
            u32 size = (u32)HexLen(hex);
            char why[160];
            u8 *to = Resolve(Json_Get(x, "target"), parts, n, why, sizeof(why));
            LevelPatch *pt = &s_lv.patches[s_lv.numPatches];

            if (to == NULL) {
                Fail(err, errSize, "patch %d: %s", s_lv.numPatches, why);
                goto done;
            }
            if (off < 0 || off > 0x10000 || size == 0 ||
                (InBlob(pack, to, 1) && !InBlob(pack, to + off, size))) {
                Fail(err, errSize, "patch %d: outside the record", s_lv.numPatches);
                goto done;
            }
            pt->at = to + off;
            pt->size = size;
            if ((pt->old = SDL_malloc(size)) == NULL) {
                Fail(err, errSize, "out of memory");
                goto done;
            }
            SDL_memcpy(pt->old, pt->at, size);
            s_lv.numPatches++;
            Json_HexDecode(hex, pt->at, size);
        }
    }

    /* HWVX_LEVEL_START=x,y,z,rotY: player 1 starts there (the editor's "test from here") */
    if (!StartOverride(pack, world, err, errSize)) {
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
    Port_Log("level: %s (%s): %u triggers (%u kept), %u timers and %u variables added, %d parts, %d patches",
             Json_GetString(doc, "id", "?"), Json_GetString(doc, "name", "?"), (unsigned)s_lv.numTriggers,
             (unsigned)numKeep, (unsigned)numTimers, (unsigned)numVars, n, s_lv.numPatches);
done:
    SDL_free(keep);
    SDL_free(parts);
    if (!ok) {
        /* nothing of it stays: the pack's own fields were not touched yet */
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
