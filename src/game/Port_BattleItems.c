/* Which weapons may spawn where a battle rolls its items.  Port-only file.
 * See Port_BattleItems.h. */
#include "common.h"
#include "Port_BattleItems.h"
#include "Port_Online.h"
#include "Port_Content.h"
#include <SDL3/SDL.h>

static u32 s_off[BATTLE_ITEMS_WORDS];
static int s_loaded;
static int s_fromHost;      /* s_off is an online host's: never saved */
static s32 s_page;

#define BIT(id) (1u << ((id) % 32))

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
    char *data = path != NULL ? SDL_LoadFile(path, NULL) : NULL;
    const char *p = data;

    s_loaded = 1;
    s_fromHost = 0;
    SDL_memset(s_off, 0, sizeof(s_off));
    while (p != NULL && *p != 0) {
        char *end;
        long v = SDL_strtol(p, &end, 10);

        if (end == p) {
            p++;
            continue;
        }
        if (v >= 0 && v < BATTLE_ITEMS_MAX_ID) {
            s_off[v / 32] |= BIT(v);
        }
        p = end;
    }
    SDL_free(data);
    SDL_free(path);
}

static void Save(void) {
    char *path = StorePath();
    char buf[BATTLE_ITEMS_MAX_ID * 4 + 1];
    size_t at = 0;
    u32 id;

    if (path == NULL) {
        return;
    }
    for (id = 0; id < BATTLE_ITEMS_MAX_ID; id++) {
        if (s_off[id / 32] & BIT(id)) {
            at += (size_t)SDL_snprintf(buf + at, sizeof(buf) - at, "%u\n", (unsigned)id);
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
    return id < BATTLE_ITEMS_MAX_ID && (s_off[id / 32] & BIT(id)) != 0;
}

void Port_BattleItemsSetOff(u32 id, int off) {
    if (!s_loaded) {
        Load();
    }
    if (id >= BATTLE_ITEMS_MAX_ID) {
        return;
    }
    if (off) {
        s_off[id / 32] |= BIT(id);
    } else {
        s_off[id / 32] &= ~BIT(id);
    }
    if (!s_fromHost) {
        Save();
    }
}

u32 Port_BattleItemsOffWord(u32 word) {
    if (!s_loaded) {
        Load();
    }
    return word < BATTLE_ITEMS_WORDS ? s_off[word] : 0;
}

void Port_BattleItemsSetOffWords(const u32 *words, u32 count) {
    u32 i;

    s_loaded = 1;
    s_fromHost = 1;
    for (i = 0; i < BATTLE_ITEMS_WORDS; i++) {
        s_off[i] = i < count ? words[i] : 0;
    }
}

void Port_BattleItemsReload(void) {
    if (s_fromHost) {
        s_loaded = 0;
    }
}

u32 Port_BattleItemsCount(void) {
    u32 n = Port_ContentRetailCount(PORT_CONTENT_ITEM);

    return n < BATTLE_ITEMS_PER_PAGE * BATTLE_ITEMS_MAX_PAGES ? n : BATTLE_ITEMS_PER_PAGE * BATTLE_ITEMS_MAX_PAGES;
}

u32 Port_BattleItemsPages(void) {
    u32 n = (Port_BattleItemsCount() + BATTLE_ITEMS_PER_PAGE - 1) / BATTLE_ITEMS_PER_PAGE;

    return n != 0 ? n : 1;
}

s32 Port_BattleItemsPage(void) {
    return s_page < (s32)Port_BattleItemsPages() ? s_page : 0;
}

void Port_BattleItemsSetPage(s32 page) {
    s_page = page >= 0 && page < (s32)Port_BattleItemsPages() ? page : 0;
}

u32 Port_BattleItemsRowId(u32 row) {
    u32 id = (u32)Port_BattleItemsPage() * BATTLE_ITEMS_PER_PAGE + row;

    return row < BATTLE_ITEMS_PER_PAGE && id < Port_BattleItemsCount() ? id : BATTLE_ITEMS_MAX_ID;
}

s32 Port_BattleItemsRoll(s32 lo, s32 hi, u32 quadrant, u32 index) {
    s32 pid, level, id, on = 0, pick;

    StateProgressionManager_GetProgressionID(&pid, &level);
    if (pid == 3 || pid == 7) {             /* battle, multi-player battle */
        for (id = lo; id <= hi; id++) {
            on += !Port_BattleItemsIsOff((u32)id);
        }
    }
    if ((pid != 3 && pid != 7) || hi < lo || on == hi - lo + 1) {
        return Port_OnlineItemRandom(lo, hi + 1, quadrant, index);
    }
    if (on == 0) {
        return -1;
    }
    /* the on ids, numbered in order: one roll picks among them */
    pick = Port_OnlineItemRandom(0, on, quadrant, index);
    for (id = lo; id <= hi; id++) {
        if (!Port_BattleItemsIsOff((u32)id) && pick-- == 0) {
            break;
        }
    }
    return id;
}
