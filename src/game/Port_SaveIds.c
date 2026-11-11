/* The save format's ids (docs/xdl-format/README.md §10.2, §10.5, §11.1):
 * the one place where the game's unlock arrays, indexed by number, meet the
 * stable ids the JSON save is keyed by. Port-only file.
 *
 * The ids and numbers themselves are the content registry's
 * (Port_Content.c): base://HW_Cars/car_<n>, base://mcp/track/<n>,
 * base://mcp/item/<n> for numbered base content, and the id each registered
 * entry (a mod's, from its ids.lock) was given. Numbers past the save
 * struct's arrays are the registry's alone; Port_SaveFormat.c keeps their
 * entries by id per slot (PortSaveExtras), as it does an id the game has no
 * number for at all (mod content not installed), which it marks inactive. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"
#include "Port_Content.h"

static const char *const s_sections[PORT_SAVE_IDS_COUNT] = {
    [PORT_SAVE_IDS_CARS] = "cars",
    [PORT_SAVE_IDS_TRACKS] = "tracks",
    [PORT_SAVE_IDS_ITEMS] = "items",
};

const char *Port_SaveIdSection(int table) {
    return table >= 0 && table < PORT_SAVE_IDS_COUNT ? s_sections[table] : NULL;
}

u32 Port_SaveIdCount(int table) {
    return Port_ContentRetailCount(table);
}

int Port_SaveIdOf(int table, u32 index, char *buf, size_t size) {
    return Port_ContentIdOf(table, index, buf, size);
}

int Port_SaveIndexOf(int table, const char *id, u32 *index) {
    return Port_ContentIndexOf(table, id, index);
}
