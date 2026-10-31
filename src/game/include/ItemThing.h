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

/* A weapon's description (ItemRecord.weapon): its model, ammo, aiming and
 * how the AI uses it. */
typedef struct WeaponInfo {
    PTR32(struct MotionAnim) anim; /* 0x00 wildcard: the rack slot's model */
    u8 pad04[0xC];              /* 0x04 */
    u32 hudName;                /* 0x10 InterfaceHudInfo.weaponName (handle) */
    u8 pad14[4];                /* 0x14 */
    u32 hudIcon;                /* 0x18 InterfaceHudInfo.weaponIcon[0] */
    u8 pad1C[4];                /* 0x1C */
    s32 ammo;                   /* 0x20 picked up with the item */
    u32 ammoPerShot;            /* 0x24 the AI doesn't use it with less */
    s32 maxAmmo;                /* 0x28 */
    u8 aimMode;                 /* 0x2C 1: by the stick, 2: auto-aims at targets, 3: faces the target */
    u8 aimLimit;                /* 0x2D degrees */
    u16 aimBone;                /* 0x2E */
    u32 reloadTime;             /* 0x30 ms */
    u8 priority;                /* 0x34 */
    u8 pad35[3];                /* 0x35 */
    f32 minRange;               /* 0x38 */
    f32 maxRange;               /* 0x3C */
    f32 aimAngle;               /* 0x40 degrees */
    f32 farAngle;               /* 0x44 aim tolerance at maxRange */
    f32 nearAngle;              /* 0x48 aim tolerance at minRange */
} WeaponInfo;

/* The item's description in the data pack. */
typedef struct ItemRecord {
    u8 type;                    /* 0x00 ITEM_TYPE_* */
    u8 pad01;                   /* 0x01 */
    u16 value;                  /* 0x02 points, or the car's pickup slave set */
    u8 pad04[8];                /* 0x04 */
    f32 radius;                 /* 0x0C pickup radius */
    f32 height;                 /* 0x10 spawned this high above its point */
    u8 pad14[4];                /* 0x14 */
    f32 spinMin;                /* 0x18 random spin speed range, degrees */
    f32 spinMax;                /* 0x1C */
    PTR32(PTR32(char)) names;   /* 0x20 name string table ([0]: the weapon name) */
    u32 pickupDebris;           /* 0x24 effect spawned when picked up, 0: none */
    u32 spawnDebris;            /* 0x28 effect spawned when it appears, 0: none */
    u8 pad2C[4];                /* 0x2C */
    PTR32(WeaponInfo) weapon;   /* 0x30 */
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
_Static_assert(offsetof(ItemRecord, height) == 0x10, "ItemRecord.height");
_Static_assert(offsetof(ItemRecord, names) == 0x20, "ItemRecord.names");
_Static_assert(offsetof(ItemRecord, pickupDebris) == 0x24, "ItemRecord.pickupDebris");
_Static_assert(offsetof(ItemRecord, weapon) == 0x30, "ItemRecord.weapon");
_Static_assert(offsetof(WeaponInfo, hudIcon) == 0x18, "WeaponInfo.hudIcon");
_Static_assert(offsetof(WeaponInfo, ammo) == 0x20, "WeaponInfo.ammo");
_Static_assert(offsetof(WeaponInfo, aimBone) == 0x2E, "WeaponInfo.aimBone");
_Static_assert(offsetof(WeaponInfo, nearAngle) == 0x48, "WeaponInfo.nearAngle");
_Static_assert(offsetof(ItemRecord, sharedSlaves) == 0x38, "ItemRecord.sharedSlaves");
_Static_assert(offsetof(ItemParams, drawDist) == 0x34, "ItemParams.drawDist");

#endif
