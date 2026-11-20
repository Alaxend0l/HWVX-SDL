/* Which weapons may spawn when a battle's items are randomized.  Port-only file.
 *
 * A player can switch individual items off; where a level rolls a random item
 * (the Gadgets option's Random, and any spawn point without a set item under
 * Original), the roll is re-rolled until it lands on an item that isn't off,
 * bounded, falling back to the plain roll.  Set item ids the level's authors
 * chose are never filtered.  The pool the menu lists is what the world loads
 * recorded (Port_BattleItemsScanWorld), kept for the run; an online client
 * gets the host's mask with the level setup (Port_Online.c) before loading.
 * The mask lives in <pref>/battle.cfg. */
#ifndef PORT_BATTLEITEMS_H
#define PORT_BATTLEITEMS_H

#include "common.h"

#define BATTLE_ITEMS_MAX_ID 256
#define BATTLE_ITEMS_WORDS (BATTLE_ITEMS_MAX_ID / 32)
#define BATTLE_ITEMS_MAX_PAGES 8        /* the Weapon Filter screen's pages (Port_Menu.c) */

int Port_BattleItemsIsOff(u32 id);
void Port_BattleItemsSetOff(u32 id, int off);      /* and save */
u32 Port_BattleItemsOffWord(u32 word);             /* for the online setup */
void Port_BattleItemsSetOffWords(const u32 *words, u32 count);   /* from the host; no save */

/* What the battle worlds seen so far can spawn: ids of their spawn points, in
 * any lo..hi range, and their own ids. 0 if nothing has been seen. */
int Port_BattleItemsSeen(u32 *ids, u32 cap);

/* The page of the Weapon Filter screen shown (Port_CODEVAR_BATFIL_PAGE): kept
 * between openings, wrapped into the pages this pool has. */
s32 Port_BattleItemsGetPage(void);
void Port_BattleItemsSetPage(s32 page);

/* The name an id spawns under (its ItemRecord's), NULL until it is known.
 * Port_BattleItemsProcessNames loads the item packs one per frame while any
 * seen id's name is unknown; call it every frame while the screen is up. */
const char *Port_BattleItemsName(u32 id);
void Port_BattleItemsProcessNames(void);

/* Random item id for a spawn point, avoiding what is off: the same roll on
 * every machine (Port_OnlineItemRandom's), re-salted by the attempt. */
s32 Port_BattleItemsRoll(s32 lo, s32 hi, u32 quadrant, u32 index);

/* Records what this world's spawn points can spawn, and the pack their ids
 * name (every load, once per world). */
void Port_BattleItemsScanWorld(void *world);

#endif
