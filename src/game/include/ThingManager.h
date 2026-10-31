/* ThingManager: the fixed pools every Thing is allocated from, the active
 * and free lists, and the per-frame move/collide/render loops
 * (ThingManager.c). The pools live in g_thingManager_DARYL; ThingPools gives
 * them their layout. Slot sizes are the original allocation sizes (each kind's
 * struct fits its slot), so the offsets match the GameCube's.
 * Include this from .c files that need it (not from common.h). */
#ifndef THINGMANAGER_H
#define THINGMANAGER_H

#include <stddef.h>
#include "Destructible.h"

/* THING_TYPE_* and THING_FLAG_* are in Thing.h. */



/* Pool slots: a Thing followed by the kind's fields, at the original size. */
#define THING_SLOT(size) struct { Thing thing; u8 pad[(size) - sizeof(Thing)]; }

typedef struct ThingSlotProjectile {
    Thing thing;
    u8 padAC[0x84];
    s32 unk130;                 /* 0x130 -1 when free */
    u8 pad134[0x2C];
} ThingSlotProjectile;

_Static_assert(offsetof(ThingSlotProjectile, unk130) == 0x130, "ThingSlotProjectile.unk130");
_Static_assert(sizeof(ThingSlotProjectile) == 0x160, "ThingSlotProjectile size");

/* g_thingManager_DARYL: one pool per thing type, then the lists. */
typedef struct ThingPools {
    ThingSlotProjectile projectiles[140]; /* 0x00000 */
    THING_SLOT(0x100) eventSeqs[192];     /* 0x0C080 */
    THING_SLOT(0xD0) emitters[160];       /* 0x18080 */
    THING_SLOT(0xF0) destructibles[500];  /* 0x20280 */
    THING_SLOT(0xF0) items[96];           /* 0x3D740 */
    THING_SLOT(0x160) attaches[192];      /* 0x43140 */
    THING_SLOT(0x100) debris[128];        /* 0x53940 */
    THING_SLOT(0x1D0) creature[1];        /* 0x5B940 */
    THING_SLOT(0x480) wildCardRacks[16];  /* 0x5BB10 */
    THING_SLOT(0x5A0) cars[8];            /* 0x60310 */
    THING_SLOT(0x140) physicalObjects[100]; /* 0x63010 */
    THING_SLOT(0xD0) looker[1];           /* 0x6AD10 */
    THING_SLOT(0x110) tracers[32];        /* 0x6ADE0 */
    THING_SLOT(0x120) pod[1];             /* 0x6CFE0 */
    THING_SLOT(0x1F0) drone[1];           /* 0x6D100 */
    THING_SLOT(0xF0) motions[56];         /* 0x6D2F0 */
    THING_SLOT(0xE0) soundSeqs[128];      /* 0x70770 */
    THING_SLOT(0xD0) radarBlips[64];      /* 0x77770 */
    ThingList activeList;                 /* 0x7AB70 */
    ThingList freeList[19];               /* 0x7AB74 by type */
} ThingPools;

_Static_assert(offsetof(ThingPools, eventSeqs) == 0x0C080, "ThingPools.eventSeqs");
_Static_assert(offsetof(ThingPools, items) == 0x3D740, "ThingPools.items");
_Static_assert(offsetof(ThingPools, cars) == 0x60310, "ThingPools.cars");
_Static_assert(offsetof(ThingPools, looker) == 0x6AD10, "ThingPools.looker");
_Static_assert(offsetof(ThingPools, drone) == 0x6D100, "ThingPools.drone");
_Static_assert(offsetof(ThingPools, soundSeqs) == 0x70770, "ThingPools.soundSeqs");
_Static_assert(offsetof(ThingPools, activeList) == 0x7AB70, "ThingPools.activeList");
_Static_assert(offsetof(ThingPools, activeList) == offsetof(ThingManagerDARYL, activeList), "ThingPools vs ThingManagerDARYL");
_Static_assert(sizeof(ThingPools) == sizeof(ThingManagerDARYL), "ThingPools size");

#define THING_POOLS ((ThingPools *)&g_thingManager_DARYL)

/* ---- slaves -------------------------------------------------------------- */

/* A slave trigger: creates a slave thing (an effect attached to a thing, or
 * to a bone of its animation) of a kind given by `type`
 * (ThingManagerList_TriggerSlaves). */
typedef struct SlaveTrigger {
    Vector4 pos;                /* 0x00 offset */
    u8 type;                    /* 0x10 */
    u8 unk11;                   /* 0x11 */
    u16 id;                     /* 0x12 -> Thing.slaveBone of the slave */
    f32 time;                   /* 0x14 */
    PTR32(void) data;           /* 0x18 by type */
    u8 unk1C;                   /* 0x1C */
    u8 holder;                  /* 0x1D first trigger of shared slaves: g_aSharedSlaveHolder index */
    u8 pad1E[2];                /* 0x1E */
} SlaveTrigger;

_Static_assert(offsetof(SlaveTrigger, data) == 0x18, "SlaveTrigger.data");
_Static_assert(sizeof(SlaveTrigger) == 0x20, "SlaveTrigger size");

typedef struct SlaveList {
    u32 count;
    PTR32(SlaveTrigger) triggers;
} SlaveList;

/* Slaves shared by every instance of a model (trigger type 10): created once
 * in a g_aSharedSlaveHolder entry and drawn at each instance. */
typedef struct SharedSlaves {
    u32 active;                 /* 0x00 holder assigned */
    SlaveList list;             /* 0x04 */
} SharedSlaves;

/* ---- ThingManager_DARYL_CreateThing parameters --------------------------- */

/* THING_TYPE_EMITTER */
typedef struct EmitterParams {
    Vector4 pos;                /* 0x00 */
    Quaternion quat;            /* 0x10 */
    Vector4 vel;                /* 0x20 */
    PTR32(void) emitter;        /* 0x30 emitter description */
    u32 owner;                  /* 0x34 Player handle */
    f32 drawDist;               /* 0x38 0: the description's */
} EmitterParams;

/* DestructibleParams (THING_TYPE_DESTRUCTIBLE / _PHYSICAL_OBJECT) is in Destructible.h. */

_Static_assert(offsetof(EmitterParams, drawDist) == 0x38, "EmitterParams.drawDist");
_Static_assert(offsetof(DestructibleParams, record) == 0x40, "DestructibleParams.record");
_Static_assert(offsetof(DestructibleParams, anim) == 0x5C, "DestructibleParams.anim");

#endif
