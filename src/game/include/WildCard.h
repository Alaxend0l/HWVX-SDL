/* Wildcard racks (WildCard.c): the slave thing on a car that holds the
 * wildcard weapons it has picked up, one animated model per slot, and
 * aims/fires the selected one. Offsets are the GameCube layout; the
 * asserts keep them that way. */
#ifndef WILDCARD_H
#define WILDCARD_H

#include <stddef.h>

#define WILDCARD_SLOTS 12

/* A motion animation as the rack reads it (AnimHandler.c has the rest). */
typedef struct WildCardAnim {
    u8 pad00[0x18];             /* 0x00 */
    f32 fps;                    /* 0x18 */
    u8 loop[3];                 /* 0x1C */
    u8 pad1F;                   /* 0x1F */
    PTR32(f32) times[3];        /* 0x20 */
} WildCardAnim;

/* ItemRecord.weapon of a wildcard item. */
typedef struct WildCardWeapon {
    PTR32(WildCardAnim) anim;   /* 0x00 the slot's model */
    u8 pad04[0x20];             /* 0x04 */
    u32 ammoPerShot;            /* 0x24 */
    s32 maxAmmo;                /* 0x28 */
    u8 aimMode;                 /* 0x2C 1: steered by the stick, 2: auto-aims at targets,
                                 * 3: faces the target */
    u8 aimLimit;                /* 0x2D degrees */
    u16 aimBone;                /* 0x2E */
} WildCardWeapon;

/* One slot: a motion state (MotionState_Init/_Draw/_Release and
 * WildcardState_Heartbeat work on its first 0x40 bytes) plus rack data. */
typedef struct WildCardSlot {
    PTR32(WildCardAnim) model;  /* 0x00 motion: current animation */
    u8 pad04[0xC];              /* 0x04 */
    PTR32(Matrix4) matrices;    /* 0x10 */
    PTR32(Thing) slaves;        /* 0x14 */
    PTR32(WildCardAnim) anim;   /* 0x18 motion history[0]: animation */
    u8 mode;                    /* 0x1C */
    u8 state;                   /* 0x1D */
    u8 pad1E[2];                /* 0x1E */
    f32 time;                   /* 0x20 */
    f32 blend;                  /* 0x24 */
    u8 pad28[0x18];             /* 0x28 */
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

_Static_assert(offsetof(WildCardAnim, times) == 0x20, "WildCardAnim.times");
_Static_assert(offsetof(WildCardWeapon, ammoPerShot) == 0x24, "WildCardWeapon.ammoPerShot");
_Static_assert(offsetof(WildCardWeapon, aimBone) == 0x2E, "WildCardWeapon.aimBone");
_Static_assert(offsetof(WildCardSlot, anim) == 0x18, "WildCardSlot.anim");
_Static_assert(offsetof(WildCardSlot, ammo) == 0x40, "WildCardSlot.ammo");
_Static_assert(offsetof(WildCardSlot, item) == 0x4C, "WildCardSlot.item");
_Static_assert(sizeof(WildCardSlot) == 0x50, "WildCardSlot size");
_Static_assert(offsetof(WildCardRack, slots) == 0xB0, "WildCardRack.slots");
_Static_assert(offsetof(WildCardRack, cur) == 0x470, "WildCardRack.cur");
_Static_assert(offsetof(WildCardRack, target) == 0x478, "WildCardRack.target");
_Static_assert(sizeof(WildCardRack) == 0x480, "WildCardRack size");

#endif
