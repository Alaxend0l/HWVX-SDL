/* Which weapons may spawn when a battle's items are randomized.  Port-only file.
 * docs: none yet; see Port_BattleItems.h.
 *
 * The state is a mask of item pack ids switched off, read at the first use
 * from <pref>/battle.cfg (one id per line, like online.cfg's one-line style)
 * and saved on every change.  The pool the filter menu lists is what the
 * worlds loaded since the run began recorded: every load of a world, in any
 * mode, adds its spawn points' ids and ranges (ScanWorld), and the item pack
 * their ids name; the menu takes the union.  The names come from the ids'
 * ItemRecords, one pack load per frame (ProcessNames) while the screen is up.
 * Online, the host's mask travels with the level setup (Port_Online.c) and
 * clients apply it before their load, so every machine re-rolls the same.
 *
 * A roll avoids switched-off ids by re-salting Port_OnlineItemRandom with the
 * attempt number: deterministic as the plain roll is offline (its own RNG each
 * attempt) and online (the level seed), bounded attempts, and the plain roll
 * stands when the whole range is off (an empty world beats no item rule). */
#include "common.h"
#include "Port_BattleItems.h"
#include "Port_Online.h"
#include "Port_Content.h"
#include "World.h"
#include "ItemThing.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

static u32 s_off[BATTLE_ITEMS_WORDS];
static u32 s_seen[BATTLE_ITEMS_WORDS];
static u32 s_seenCount;
static s32 s_page;
static int s_loaded;

/* Each seen id's item pack, as the world that showed it first named it, and
 * its ItemRecord's name once loaded (NULL: unknown, "": no name to be had). */
static const char *s_nameSource[BATTLE_ITEMS_MAX_ID];
static char *s_names[BATTLE_ITEMS_MAX_ID];
static u32 s_nameId = BATTLE_ITEMS_MAX_ID;    /* id being looked up, MAX: idle */
static s32 s_namePack;                        /* its streaming slot */

static char *StorePath(void) {
    char *pref = Port_PrefPath();
    char *path = NULL;

    if (pref != NULL) {
        SDL_asprintf(&path, "%sbattle.cfg", pref);
        SDL_free(pref);
    }
    return path;
}

static void Load(void) {
    char *path = StorePath();

    s_loaded = 1;
    if (path == NULL) {
        return;
    }
    {
        size_t size = 0;
        char *data = SDL_LoadFile(path, &size);

        if (data != NULL) {
            char *p = data;
            u32 n = 0;

            while (*p != 0) {
                char *end;
                long v = SDL_strtol(p, &end, 10);

                if (end == p) {
                    p++;
                    continue;
                }
                if (v >= 0 && v < BATTLE_ITEMS_MAX_ID && n < BATTLE_ITEMS_MAX_ID) {
                    s_off[v / 32] |= 1u << (v % 32);
                    n++;
                }
                p = end;
            }
            SDL_free(data);
        }
        SDL_free(path);
    }
}

static void Save(void) {
    char *path = StorePath();
    char buf[BATTLE_ITEMS_MAX_ID * 5];
    size_t at = 0;
    u32 id;

    if (path == NULL) {
        return;
    }
    for (id = 0; id < BATTLE_ITEMS_MAX_ID; id++) {
        if (s_off[id / 32] & (1u << (id % 32))) {
            at += (size_t)SDL_snprintf(buf + at, sizeof(buf) - at, "%u\n", id);
        }
    }
    if (!SDL_SaveFile(path, buf, at)) {
        Port_Log("battle.cfg: %s", SDL_GetError());
    }
    SDL_free(path);
}

int Port_BattleItemsIsOff(u32 id) {
    if (!s_loaded) {
        Load();
    }
    return id < BATTLE_ITEMS_MAX_ID && (s_off[id / 32] & (1u << (id % 32))) != 0;
}

void Port_BattleItemsSetOff(u32 id, int off) {
    if (!s_loaded) {
        Load();
    }
    if (id >= BATTLE_ITEMS_MAX_ID) {
        return;
    }
    if (off) {
        s_off[id / 32] |= 1u << (id % 32);
    } else {
        s_off[id / 32] &= ~(1u << (id % 32));
    }
    Save();
}

u32 Port_BattleItemsOffWord(u32 word) {
    if (!s_loaded) {
        Load();
    }
    return word < BATTLE_ITEMS_WORDS ? s_off[word] : 0;
}

void Port_BattleItemsSetOffWords(const u32 *words, u32 count) {
    u32 i;

    s_loaded = 1;      /* the host's is the truth for this session */
    for (i = 0; i < count && i < BATTLE_ITEMS_WORDS; i++) {
        s_off[i] = words[i];
    }
}

s32 Port_BattleItemsGetPage(void) {
    return s_page;
}

void Port_BattleItemsSetPage(s32 page) {
    s_page = page < 0 ? 0 : (page >= BATTLE_ITEMS_MAX_PAGES ? BATTLE_ITEMS_MAX_PAGES - 1 : page);
}

void Port_BattleItemsScanWorld(void *worldv) {
    static const void *s_scanned[32];
    static u32 s_numScanned;
    const World *world = worldv;
    const WorldGameData *gd = GETP(world->gameData);
    const char *source = gd != NULL ? GETP(gd->itemPack) : NULL;
    u32 q, i, k;

    if (!s_loaded) {
        Load();
    }
    if (world == NULL) {
        return;
    }
    /* every world once, at its first load: a later load of the same pack
     * would see the ids an earlier load's rolls wrote into it (the menu's
     * `save` bit writes values back into pack RAM the same way) and take
     * them for the authors' own */
    for (k = 0; k < s_numScanned && k < SDL_arraysize(s_scanned); k++) {
        if (s_scanned[k] == world) {
            return;
        }
    }
    if (s_numScanned < SDL_arraysize(s_scanned)) {
        s_scanned[s_numScanned++] = world;
    }
    for (q = 0; q < world->numQuads; q++) {
        const QuadContents *c = GETP(GETP(world->quads)[q].contents);

        if (c == NULL) {
            continue;
        }
        for (i = 0; i < c->numItems; i++) {
            const QuadItem *sp = &GETP(c->items)[i];
            u32 lo = sp->lo, hi = sp->hi, id;

            if (sp->id != 0xFFFF && sp->id < BATTLE_ITEMS_MAX_ID &&
                !(s_seen[sp->id / 32] & (1u << (sp->id % 32)))) {
                s_seen[sp->id / 32] |= 1u << (sp->id % 32);
                s_seenCount++;
                s_nameSource[sp->id] = source;
            }
            for (id = lo; id <= hi && id < BATTLE_ITEMS_MAX_ID; id++) {
                if (!(s_seen[id / 32] & (1u << (id % 32)))) {
                    s_seen[id / 32] |= 1u << (id % 32);
                    s_seenCount++;
                    s_nameSource[id] = source;
                }
                if (id == 0xFF) {
                    break;
                }
            }
        }
    }
}

int Port_BattleItemsSeen(u32 *ids, u32 cap) {
    u32 id, n = 0;

    if (!s_loaded) {
        Load();
    }
    for (id = 0; id < BATTLE_ITEMS_MAX_ID && n < cap; id++) {
        if (s_seen[id / 32] & (1u << (id % 32))) {
            ids[n++] = id;
        }
    }
    return (int)n;
}

const char *Port_BattleItemsName(u32 id) {
    return id < BATTLE_ITEMS_MAX_ID ? s_names[id] : NULL;
}

void Port_BattleItemsProcessNames(void) {
    MCP *mcp = GameSettings_GetMCP();
    const char *src;
    void *pack;
    ItemRecord *rec;
    char **names;
    u32 id;

    if (mcp == NULL || GETP(mcp->itemsPack) == NULL || IsLoadingThreadRunning()) {
        return;
    }
    if (s_nameId >= BATTLE_ITEMS_MAX_ID) {
        for (id = 0; id < BATTLE_ITEMS_MAX_ID; id++) {
            if ((s_seen[id / 32] & (1u << (id % 32))) && s_names[id] == NULL) {
                s_nameId = id;
                break;
            }
        }
        if (s_nameId >= BATTLE_ITEMS_MAX_ID) {
            return;                                  /* every name known */
        }
        s_namePack = -1;
    }
    if (s_namePack == -1) {
        char *name;

        src = s_nameSource[s_nameId] != NULL ? s_nameSource[s_nameId] : GETP(mcp->itemsPack);
        name = SDL_strdup(src);                      /* the loader keeps the pointer */
        if (name == NULL) {
            s_names[s_nameId] = "";
            s_nameId = BATTLE_ITEMS_MAX_ID;
            return;
        }
        s_namePack = FileStreaming_LoadDataPack(name, 2, s_nameId, 0);
        if (s_namePack == -1) {
            SDL_free(name);
            s_names[s_nameId] = "";
            s_nameId = BATTLE_ITEMS_MAX_ID;
        }
        return;
    }
    pack = FileStreaming_InitializeDataPack(s_namePack, 2, 1);
    if (pack == NULL) {
        return;                                      /* still loading: try again next frame */
    }
    rec = DataPack_GetScript(pack, (s32)s_nameId);
    if (rec != NULL && GETP(rec->names) != NULL) {
        names = GETP(rec->names);
        s_names[s_nameId] = names[0] != NULL ? names[0] : "";
    } else {
        s_names[s_nameId] = "";                      /* this pack's entry has no name */
    }
    FileStreaming_ReleaseDataPack(s_namePack, 1);
    s_nameId = BATTLE_ITEMS_MAX_ID;
}

s32 Port_BattleItemsRoll(s32 lo, s32 hi, u32 quadrant, u32 index) {
    u32 attempt;
    s32 id = -1;

    for (attempt = 0; attempt <= 16; attempt++) {
        /* Port_OnlineItemRandom's own roll, its quadrant salted by the attempt:
         * attempt 0 is exactly the plain roll. */
        id = Port_OnlineItemRandom(lo, hi + 1, quadrant ^ (attempt * 2654435761u), index);
        if (!Port_BattleItemsIsOff((u32)id)) {
            if (attempt && getenv("HWVX_ITEMFILTER") != NULL) {
                Port_Log("itemfilter: q%u i%u %d..%d: %d off, re-rolled to %d", quadrant, index, lo, hi,
                         (int)Port_OnlineItemRandom(lo, hi + 1, quadrant, index), id);
            }
            return id;
        }
    }
    if (getenv("HWVX_ITEMFILTER") != NULL) {
        Port_Log("itemfilter: q%u i%u %d..%d: all off, keeping %d", quadrant, index, lo, hi, id);
    }
    return id;     /* the whole range is off: the last roll stands */
}
