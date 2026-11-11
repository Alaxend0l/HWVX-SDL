/* The content registry: the cars, tracks and items the game addresses by
 * number (code variables 0x64, 0x65, 0x66), sized from the loaded data
 * instead of the save struct's fixed arrays. Port-only.
 * docs/xdl-format/README.md §10.5.
 *
 *   Port_Content.c      the registry: counts, unlock state, ids, the saves'
 *                       entries past the arrays (no game code: the save tests
 *                       link it)
 *   Port_ContentData.c  what the game data says: the counts (HW_Cars' TOC),
 *                       the car packs extra entries load, the menus' ranges
 *
 * Numbering: entries 0..retail-1 are the HotWheelsGameData arrays (cars[0x21],
 * tracks[0x39], items[0xC]); then base content past them that the data has
 * (a 34th car pack in HW_Cars: base://HW_Cars/car_33); then entries
 * registered by id (mods through ids.lock, the HWVX_TEST_EXTRA_CARS switch),
 * each in the order registered.
 *
 * Unlock state: the registry's own array, sized to the count, is the one
 * the game's unlock checks read (GameSettings_Get/SetCodeVariable). The
 * save struct keeps its exact layout (memory card files and imports use
 * it) as the view of entries 0..retail-1: every write goes to both, and
 * where the game replaces the struct whole (a new game, a load, the end of
 * an online session) Port_ContentFromGameData takes the view back. Entries
 * past the arrays live only here and in the JSON save, by id, per save slot
 * (PortSaveExtras.kept), which carries them as the game moves its slots. */
#ifndef PORT_CONTENT_H
#define PORT_CONTENT_H

#include "port.h"

struct JsonNode;

enum {
    PORT_CONTENT_CAR,       /* HotWheelsGameData.cars, code variable 0x64; HW_Cars packs */
    PORT_CONTENT_TRACK,     /* .tracks, 0x65; the MCP's track (event) numbers */
    PORT_CONTENT_ITEM,      /* .items, 0x66; HW_Items packs */
    PORT_CONTENT_KINDS
};

/* How many there are, and how many the save struct's array holds. */
u32 Port_ContentCount(int kind);
u32 Port_ContentRetailCount(int kind);
/* Entry i's unlock value (the save's s8: 0 locked, 1 unlocked); 0 past
 * the count. Setting past the count does nothing. */
s8 Port_ContentUnlocked(int kind, u32 i);
void Port_ContentSetUnlocked(int kind, u32 i, s8 v);

/* Ids, as the save is keyed: base:// for numbered base content, the
 * registered id otherwise. 1 if there is one. */
int Port_ContentIdOf(int kind, u32 i, char *buf, size_t size);
int Port_ContentIndexOf(int kind, const char *id, u32 *index);
/* The base entry whose data entry i uses: i itself, or what a registered
 * entry was aliased to (the test switch; -1: data of its own, from a mod). */
s32 Port_ContentDataOf(int kind, u32 i);

/* ---- setting up (Port_ContentData.c, once the data is there) ---------- */

/* How many numbered entries the data has; never fewer than the array. Before
 * anything is registered. */
void Port_ContentSetBaseCount(int kind, u32 n);
/* The mod hook: an entry the data doesn't number, by id (a uid:// from a
 * project's ids.lock). Its number: the next past the count, -1 if the id
 * is taken or bad. alias: the base entry whose data it loads (-1: its own).
 * def: its unlock value in a new game. */
s32 Port_ContentRegister(int kind, const char *id, s32 alias, s8 def);
/* Back to the retail counts, nothing registered, everything locked (tests). */
void Port_ContentReset(void);

/* ---- the save struct as the registry's view ----------------------------- */

/* The arrays of the live HotWheelsGameData (retail-sized). */
void Port_ContentBindGameData(s8 *cars, s8 *tracks, s8 *items);
/* The game replaced the struct: entries 0..retail-1 from it; past it,
 * each entry's new-game value (a new game; a load follows with
 * Port_ContentLoadKept). */
void Port_ContentFromGameData(void);

/* ---- entries past the arrays in the JSON save --------------------------- */

/* The live entries past the arrays from a save slot's kept entries (NULL:
 * none: each its new-game value). */
void Port_ContentLoadKept(const struct JsonNode *kept);
/* A slot's kept entries get the live values of the entries past the arrays
 * (others it keeps, by id, are left as they are). */
void Port_ContentStoreKept(struct JsonNode **kept);
/* Unlocked entries past the arrays in a slot's kept entries. */
u32 Port_ContentKeptUnlocked(int kind, const struct JsonNode *kept);
/* GameSettings_SaveGame: the save it writes next has the live game in
 * `slot`; Port_SavesWrite takes it (-1: none, the live game isn't in it). */
void Port_ContentNoteLiveSlot(int slot);
int Port_ContentTakeLiveSlot(void);

/* Online: the live entries past the arrays kept aside and put back with
 * the struct (GameSettings_PortOnlineBegin/End). */
void Port_ContentStash(int restore);

/* ---- Port_ContentData.c -------------------------------------------------- */

/* Once, as the game settings start (GameSetting_Init): the counts from the
 * data, the registered entries. */
void Port_ContentInit(void);
/* DataPack_LoadFile: the TOC id to load for pack `id` of type `type` in
 * archive `name` (an extra car: the car whose pack it aliases). */
u32 Port_ContentPackId(const char *name, u32 type, u32 id);
/* After a pack's pointers are fixed up: an aliased car's scripts answer to
 * its own id (DataPack_GetScript). */
void Port_ContentFixScripts(void *dataPack, const char *name, u32 type, u32 id);
/* Port_MenuPatch: the menus' car, track and item ranges to the counts. */
struct InterfaceHeader;
void Port_ContentPatchInterface(struct InterfaceHeader *h);

#endif
