/* Which weapons may spawn where a battle rolls its items.  Port-only file.
 *
 * The player switches weapons off on the Battle Options' Weapons screen
 * (Port_Menu.c). Where a battle's spawn point rolls a random item in its
 * lo..hi range (the Gadgets option's Random; under Original only the points
 * the authors left at 0xFFFF), the roll picks among the ids in range that
 * are on; with every id in range off, the point stays empty. Ids the
 * authors set are never filtered, and nothing changes outside the battle
 * progressions (3 and 7). The retail battle worlds roll 0..11, the weapons
 * the item select lists, so those are what the screen shows.
 *
 * The mask lives in <pref>/battle.cfg (the ids switched off, one per
 * line). Online, the host's travels in MSG_SETUP (Port_Online.c) and
 * stands in for a client's own until the session ends, so every machine
 * rolls the same items. */
#ifndef PORT_BATTLEITEMS_H
#define PORT_BATTLEITEMS_H

#include "common.h"

#define BATTLE_ITEMS_MAX_ID 256
#define BATTLE_ITEMS_WORDS (BATTLE_ITEMS_MAX_ID / 32)
#define BATTLE_ITEMS_PER_PAGE 6     /* the Weapons screen's rows */
#define BATTLE_ITEMS_MAX_PAGES 8

int Port_BattleItemsIsOff(u32 id);
void Port_BattleItemsSetOff(u32 id, int off);      /* and saves battle.cfg */
u32 Port_BattleItemsOffWord(u32 word);             /* for the online setup */
void Port_BattleItemsSetOffWords(const u32 *words, u32 count);   /* the host's; not saved */
void Port_BattleItemsReload(void);                 /* battle.cfg's again (session over) */

/* How many weapons the Weapons screen lists (ids 0..n-1), and its pages. */
u32 Port_BattleItemsCount(void);
u32 Port_BattleItemsPages(void);
/* The page shown (PORT_CODEVAR_BATFIL_PAGE), and the id its row `row`
 * stands for (PORT_CODEVAR_BATFIL's arg): BATTLE_ITEMS_MAX_ID past the list. */
s32 Port_BattleItemsPage(void);
void Port_BattleItemsSetPage(s32 page);
u32 Port_BattleItemsRowId(u32 row);

/* A random item id in lo..hi for spawn point (quadrant, index), rolled as
 * Port_OnlineItemRandom rolls it (the same on every machine online) among
 * the ids that are on; -1: all of them are off, spawn nothing. Outside a
 * battle, or with nothing in range off, exactly the plain roll. */
s32 Port_BattleItemsRoll(s32 lo, s32 hi, u32 quadrant, u32 index);

#endif
