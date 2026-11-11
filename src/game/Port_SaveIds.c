/* The save format's ids (docs/xdl-format/README.md §10.2, §10.5, §11.1):
 * the one place where the game's unlock arrays, indexed by number, meet the
 * stable ids the JSON save is keyed by. Port-only file.
 *
 * Retail content keeps its retail numbers on every release (§8.1), so for
 * now an id is its table's pattern with the index filled in:
 *
 *   cars    base://HW_Cars/car_<n>     the car's pack in HW_Cars (car_0..car_32)
 *   tracks  base://mcp/track/<n>       the MCP's track (event) number
 *   items   base://mcp/item/<n>        the MCP's item number
 *
 * Cars are packs, so their ids are the packs' structural ids. Tracks and
 * items are numbers the MCP script (archive mcp) gives events and items, not
 * packs of their own, so they are named under it.
 *
 * Later, a project's ids.lock (§10.5) adds entries here: a uid:// (mod
 * content) with the number the compiler gave it. Content past the game's
 * arrays then has no index; Port_SaveFormat.c keeps such entries by id
 * (PortSaveExtras) until the game can hold them. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"

typedef struct SaveIdTable {
    const char *section;   /* the member of a save slot in the JSON */
    const char *pattern;   /* base:// id of index n: pattern then n */
    u32 count;             /* the game's array */
} SaveIdTable;

static const SaveIdTable s_tables[PORT_SAVE_IDS_COUNT] = {
    [PORT_SAVE_IDS_CARS] = { "cars", "base://HW_Cars/car_", sizeof(((HotWheelsGameData *)0)->cars) },
    [PORT_SAVE_IDS_TRACKS] = { "tracks", "base://mcp/track/", sizeof(((HotWheelsGameData *)0)->tracks) },
    [PORT_SAVE_IDS_ITEMS] = { "items", "base://mcp/item/", sizeof(((HotWheelsGameData *)0)->items) },
};

/* Ids with numbers of their own (ids.lock): none yet. Consulted first, so an
 * entry here can also give retail content another id. */
typedef struct SaveIdEntry {
    int table;
    u32 index;
    const char *id;
} SaveIdEntry;
static const SaveIdEntry *s_locked;
static u32 s_lockedCount;

const char *Port_SaveIdSection(int table) {
    return table >= 0 && table < PORT_SAVE_IDS_COUNT ? s_tables[table].section : NULL;
}

u32 Port_SaveIdCount(int table) {
    return table >= 0 && table < PORT_SAVE_IDS_COUNT ? s_tables[table].count : 0;
}

int Port_SaveIdOf(int table, u32 index, char *buf, size_t size) {
    u32 i;

    if (table < 0 || table >= PORT_SAVE_IDS_COUNT || index >= s_tables[table].count) {
        return 0;
    }
    for (i = 0; i < s_lockedCount; i++) {
        if (s_locked[i].table == table && s_locked[i].index == index) {
            SDL_strlcpy(buf, s_locked[i].id, size);
            return 1;
        }
    }
    SDL_snprintf(buf, size, "%s%u", s_tables[table].pattern, (unsigned)index);
    return 1;
}

int Port_SaveIndexOf(int table, const char *id, u32 *index) {
    const char *pattern;
    size_t n;
    char *end;
    unsigned long v;
    u32 i;

    if (table < 0 || table >= PORT_SAVE_IDS_COUNT || id == NULL) {
        return 0;
    }
    for (i = 0; i < s_lockedCount; i++) {
        if (s_locked[i].table == table && SDL_strcmp(s_locked[i].id, id) == 0) {
            *index = s_locked[i].index;
            return 1;
        }
    }
    pattern = s_tables[table].pattern;
    n = SDL_strlen(pattern);
    if (SDL_strncmp(id, pattern, n) != 0 || id[n] < '0' || id[n] > '9' || (id[n] == '0' && id[n + 1] != 0)) {
        return 0;
    }
    v = SDL_strtoul(id + n, &end, 10);
    if (*end != 0 || v >= s_tables[table].count) {
        return 0;
    }
    *index = (u32)v;
    return 1;
}
