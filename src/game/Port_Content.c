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

typedef struct Registered {
    char *id;
    s32 alias;          /* the base entry whose data it uses, -1: its own */
    s8 def;             /* in a new game */
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

/* An entry's value in a new game: past the array, locked unless registered
 * otherwise. Retail entries get theirs from the game (GameSetting_Init). */
static s8 DefaultOf(const Kind *k, u32 i) {
    u32 b = BaseOf(k);
    return i >= b && i - b < k->numReg ? k->reg[i - b].def : 0;
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
    if (i >= k->liveSize) {
        return 0;
    }
    return k->live[i];
}

void Port_ContentSetUnlocked(int kind, u32 i, s8 v) {
    Kind *k = KindOf(kind);

    if (k == NULL || i >= CountOf(k)) {
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

    if (k == NULL || i >= CountOf(k)) {
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
        if (SDL_strcmp(k->reg[i].id, id) == 0) {
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
    return i < b ? (s32)i : k->reg[i - b].alias;
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

s32 Port_ContentRegister(int kind, const char *id, s32 alias, s8 def) {
    Kind *k = KindOf(kind);
    Registered *r;
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
    r = SDL_realloc(k->reg, (k->numReg + 1) * sizeof(Registered));
    if (r == NULL) {
        return -1;
    }
    k->reg = r;
    r = &k->reg[k->numReg];
    r->id = SDL_strdup(id);
    r->alias = alias;
    r->def = def;
    k->numReg++;
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
