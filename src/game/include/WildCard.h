/* Wildcard racks (WildCard.c): the slave thing on a car that holds the
 * wildcard weapons it has picked up, one animated model per slot, and
 * aims/fires the selected one. Offsets are the GameCube layout; the
 * asserts keep them that way. */
#ifndef WILDCARD_H
#define WILDCARD_H

#include <stddef.h>
#include "CarA.h"

#define WILDCARD_SLOTS 12

/* One slot: the motion state of its model, plus the weapon in it. The
 * rack plays the weapon's phases through the state's prev* fields. */
typedef struct WildCardSlot {
    MotionState motion;         /* 0x00 model: WeaponInfo.anim */
    u8 pad38[8];                /* 0x38 */
    s16 ammo;                   /* 0x40 */
    u16 unk42;                  /* 0x42 */
    f32 scale;                  /* 0x44 pops up from 0 to 1 when selected */
    f32 angle;                  /* 0x48 aim, radians */
    PTR32(ItemRecord) item;     /* 0x4C the item it came from, NULL: empty */
} WildCardSlot;

typedef struct WildCardRack {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    WildCardSlot slots[WILDCARD_SLOTS]; /* 0xB0 */
    u8 cur;                     /* 0x470 selected slot */
    u8 pad471;                  /* 0x471 */
    u8 noFire;                  /* 0x472 */
    u8 pad473[5];               /* 0x473 */
    PTR32(Thing) target;        /* 0x478 */
    f32 unk47C;                 /* 0x47C -1: no target kept */
} WildCardRack;

_Static_assert(offsetof(WildCardSlot, ammo) == 0x40, "WildCardSlot.ammo");
_Static_assert(offsetof(WildCardSlot, item) == 0x4C, "WildCardSlot.item");
_Static_assert(sizeof(WildCardSlot) == 0x50, "WildCardSlot size");
_Static_assert(offsetof(WildCardRack, slots) == 0xB0, "WildCardRack.slots");
_Static_assert(offsetof(WildCardRack, cur) == 0x470, "WildCardRack.cur");
_Static_assert(offsetof(WildCardRack, target) == 0x478, "WildCardRack.target");
_Static_assert(sizeof(WildCardRack) == 0x480, "WildCardRack size");

#endif
