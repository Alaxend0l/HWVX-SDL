/* Item things: pickups lying in the world (weapons, wildcards...), see
 * ItemThing.c. Offsets are the GameCube layout. */
#ifndef ITEMTHING_H
#define ITEMTHING_H

#include <stddef.h>

/* ItemRecord.type */
enum {
    ITEM_TYPE_WILDCARD = 3,     /* goes into the collector's wildcard rack */
    ITEM_TYPE_CARRIED = 7,      /* follows its carrier's nav point */
};

/* The item's description in the data pack. */
typedef struct ItemRecord {
    u8 type;                    /* 0x00 ITEM_TYPE_* */
    u8 pad01[0xB];              /* 0x01 */
    f32 radius;                 /* 0x0C pickup radius */
    u8 pad10[8];                /* 0x10 */
    f32 spinMin;                /* 0x18 random spin speed range, degrees */
    f32 spinMax;                /* 0x1C */
    u8 pad20[4];                /* 0x20 */
    u32 pickupDebris;           /* 0x24 effect spawned when picked up, 0: none */
    u8 pad28[8];                /* 0x28 */
    PTR32(u8) weapon;           /* 0x30 weapon info: s32 ammo at 0x20 */
    u8 pad34[4];                /* 0x34 */
    u8 sharedSlaves[4];         /* 0x38 Thing_DrawSharedSlaves triggers */
} ItemRecord;

/* ThingManager_DARYL_CreateThing parameters for an item. */
typedef struct ItemParams {
    Vector4 quat;               /* 0x00 */
    Vector4 pos;                /* 0x10 */
    Vector4 vel;                /* 0x20 */
    PTR32(ItemRecord) record;   /* 0x30 */
    f32 drawDist;               /* 0x34 0: 500 */
    u8 unk38;                   /* 0x38 */
} ItemParams;

typedef struct ItemThing {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    Vector4 spinAxis;           /* 0xB0 */
    PTR32(ItemRecord) record;   /* 0xC0 */
    f32 spinSpeed;              /* 0xC4 */
    f32 drawDist;               /* 0xC8 */
    s16 pickupDelay;            /* 0xCC ms before it can be picked up (again) */
    s16 ammo;                   /* 0xCE */
    PTR32(u8) spawner;          /* 0xD0 FileStreaming QuadItem (respawn time 0x14 -> timer 0x18), or NULL */
    s32 navPoint;               /* 0xD4 nearest nav point, -1: none */
    u32 timerActive;            /* 0xD8 */
    s32 timer;                  /* 0xDC ms */
    u8 unkE0;                   /* 0xE0 ItemParams.unk38 */
    u8 padE1[7];                /* 0xE1 */
    f32 pickupRadiusSq;         /* 0xE8 */
    s32 unkEC;                  /* 0xEC */
} ItemThing;

_Static_assert(offsetof(ItemThing, spinAxis) == 0xB0, "ItemThing.spinAxis");
_Static_assert(offsetof(ItemThing, record) == 0xC0, "ItemThing.record");
_Static_assert(offsetof(ItemThing, pickupDelay) == 0xCC, "ItemThing.pickupDelay");
_Static_assert(offsetof(ItemThing, navPoint) == 0xD4, "ItemThing.navPoint");
_Static_assert(offsetof(ItemThing, unkE0) == 0xE0, "ItemThing.unkE0");
_Static_assert(offsetof(ItemThing, pickupRadiusSq) == 0xE8, "ItemThing.pickupRadiusSq");
_Static_assert(sizeof(ItemThing) == 0xF0, "ItemThing size");
_Static_assert(offsetof(ItemRecord, pickupDebris) == 0x24, "ItemRecord.pickupDebris");
_Static_assert(offsetof(ItemRecord, sharedSlaves) == 0x38, "ItemRecord.sharedSlaves");
_Static_assert(offsetof(ItemParams, drawDist) == 0x34, "ItemParams.drawDist");

#endif
