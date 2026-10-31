/* Thing: the header every game object starts with.
 *
 * Cars, creatures, destructibles, projectiles, items, debris... are all
 * "things": pooled by ThingManager (ThingManager.c), kept in doubly linked
 * lists (prev/next), and driven through a 17-entry vtable (ThingVSlot).
 * Each kind embeds this header at offset 0 (as `Thing base`) and adds its
 * own fields from 0xAC on, so a `Thing *` can be taken from any of them.
 *
 * The offsets are the GameCube layout; the asserts below keep them that way.
 * Pointer fields are PTR32 handles (see README, "64-bit hosts"). */
#ifndef THING_H
#define THING_H

#include <stddef.h>

/* Thing.flags bits whose meaning is known. Several others are used with a
 * kind-specific meaning (0x4000 for instance); those stay numeric. */
enum {
    THING_FLAG_DEAD           = 0x2,        /* ignored by collision and rays */
    THING_FLAG_DYNAMIC        = 0x4,        /* moved and collided by the subtick loop */
    THING_FLAG_AT_REST        = 0x8,        /* not simulated; matrix/bounds are current */
    THING_FLAG_MOVED          = 0x80,       /* position changed this frame */
    THING_FLAG_HIDDEN         = 0x200,      /* not drawn (or collided) */
    THING_FLAG_IN_FORCE_BOX   = 0x400,      /* Thing_CollideWithForceBoxes */
    THING_FLAG_IN_TELEPORTER  = 0x800,      /* Thing_CollideWithTeleporters */
    THING_FLAG_FREE           = 0x1000,     /* in a free list */
    THING_FLAG_IN_COLLISION_PASS = 0x8000,
    THING_FLAG_PUSHED         = 0x10000,    /* keep pushVel this frame */
    THING_FLAG_NO_OBJECT_HITS = 0x20000,    /* skip things in the zero-time tests */
    THING_FLAG_NO_TERRAIN_HITS = 0x40000,   /* skip the terrain in the zero-time tests */
    THING_FLAG_PUSHABLE       = 0x100000,   /* physical objects: can be pushed */
    THING_FLAG_RELEASED       = 0x200000,   /* freed at the end of the frame */
    THING_FLAG_NO_DEATH_TIMER = 0x10000000, /* destructibles: never released when dead */
    THING_FLAG_SLAVE          = 0x20000000, /* created by ThingManager_DARYL_CreateSlave */
};

/* Thing.type: the pool / free list a thing belongs to, and the type passed to
 * ThingManager_DARYL_CreateThing. */
enum {
    THING_TYPE_PROJECTILE = 0,
    THING_TYPE_EVENT_SEQ = 1,       /* params: DebrisParams */
    THING_TYPE_EMITTER = 2,         /* params: EmitterParams */
    THING_TYPE_DESTRUCTIBLE = 3,    /* params: DestructibleParams */
    THING_TYPE_ITEM = 4,            /* params: ItemParams */
    THING_TYPE_ATTACH = 5,
    THING_TYPE_DEBRIS = 6,
    THING_TYPE_CREATURE = 7,
    THING_TYPE_WILDCARD_RACK = 8,
    THING_TYPE_CAR = 9,             /* params: CarParams */
    THING_TYPE_PHYSICAL_OBJECT = 10, /* params: DestructibleParams */
    THING_TYPE_LOOKER = 11,
    THING_TYPE_TRACER = 12,
    THING_TYPE_POD = 13,
    THING_TYPE_DRONE = 14,
    THING_TYPE_MOTION = 15,
    THING_TYPE_SOUND_SEQ = 16,
    THING_TYPE_RADAR_BLIP = 17,
    THING_NUM_TYPES = 18,
};

/* Thing.collisionType */
enum {
    THING_COLLIDE_NONE = 0,
    THING_COLLIDE_OBOX = 1,     /* oriented box (vtable GetOrientedCollision) */
    THING_COLLIDE_RAY = 3,      /* others are tested against its CollideWithRay */
    THING_COLLIDE_SPHERES = 6,  /* sphere list from Thing_GetLocalCollision */
};

/* A thing list: the 4-byte slot holding its first thing; the things are
 * linked through Thing.prev/next. ThingManagerList_* take its address. */
typedef PTR32(struct Thing) ThingList;

typedef struct Thing {
    Vector4 vel;                       /* 0x00 linear velocity */
    union {
        Vector4 mtxRows[4];            /* 0x10 world transform, see THING_MTX */
        struct {
            Vector4 axis[3];           /* 0x10 rotation rows */
            Vector4 pos;               /* 0x40 translation = position */
        };
    };
    Quaternion quat;                   /* 0x50 orientation (mirrors mtx) */
    Vector4 pushVel;                   /* 0x60 */
    f32 radius;                        /* 0x70 bounding sphere around pos */
    u32 flags;                         /* 0x74 THING_FLAG_* */
    s32 health;                        /* 0x78 */
    s32 damage;                        /* 0x7C */
    s16 collisionType;                 /* 0x80 THING_COLLIDE_* */
    u8 slaveBone;                      /* 0x82 slave: bone of the master it follows */
    u8 slaveTag;                       /* 0x83 slave: group (MotionState_ReleaseSlavesByType) */
    PTR32(void) gridObject;            /* 0x84 Grid_* handle, or 0 */
    ThingList slaves;                  /* 0x88 attached slave things */
    PTR32(u32) vtable;                 /* 0x8C ThingVSlot-indexed handles */
    PTR32(void) trigger;               /* 0x90 kind-specific: trigger / spawn slot */
    PTR32(void) player;                /* 0x94 owning Player, if any */
    u32 lightIndex;                    /* 0x98 into g_ThingLightingData, -1: none */
    u8 pad9C[4];                       /* 0x9C */
    PTR32(struct Thing) prev;          /* 0xA0 list links (ThingManagerList_*) */
    PTR32(struct Thing) next;          /* 0xA4 */
    u32 type;                          /* 0xA8 index into the free lists */
} Thing;

/* The transform as a Matrix4. Not a Matrix4 member: that type is 16-byte
 * aligned, which would pad Thing to 0xB0, and the kinds start at 0xAC. */
#define THING_MTX(thing) ((Matrix4 *)(thing)->mtxRows)

#define THING_ASSERT_OFFSET(f, off) _Static_assert(offsetof(Thing, f) == (off), "Thing." #f)
THING_ASSERT_OFFSET(vel, 0x00);
THING_ASSERT_OFFSET(mtxRows, 0x10);
THING_ASSERT_OFFSET(pos, 0x40);
THING_ASSERT_OFFSET(quat, 0x50);
THING_ASSERT_OFFSET(pushVel, 0x60);
THING_ASSERT_OFFSET(radius, 0x70);
THING_ASSERT_OFFSET(flags, 0x74);
THING_ASSERT_OFFSET(health, 0x78);
THING_ASSERT_OFFSET(damage, 0x7C);
THING_ASSERT_OFFSET(collisionType, 0x80);
THING_ASSERT_OFFSET(slaveBone, 0x82);
THING_ASSERT_OFFSET(slaveTag, 0x83);
THING_ASSERT_OFFSET(gridObject, 0x84);
THING_ASSERT_OFFSET(slaves, 0x88);
THING_ASSERT_OFFSET(vtable, 0x8C);
THING_ASSERT_OFFSET(trigger, 0x90);
THING_ASSERT_OFFSET(player, 0x94);
THING_ASSERT_OFFSET(lightIndex, 0x98);
THING_ASSERT_OFFSET(prev, 0xA0);
THING_ASSERT_OFFSET(next, 0xA4);
THING_ASSERT_OFFSET(type, 0xA8);
_Static_assert(sizeof(Thing) == 0xAC, "Thing size");
#undef THING_ASSERT_OFFSET

/* ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, ...) parameters: an
 * event sequence (sparks, explosions, pickup flashes...) played at a spot.
 * `debris` is the sequence description. */
typedef struct DebrisParams {
    Vector4 pos;                       /* 0x00 */
    Vector4 quat;                      /* 0x10 */
    Vector4 vel;                       /* 0x20 */
    Vector4 angVel;                    /* 0x30 */
    u32 debris;                        /* 0x40 debris description (handle) */
    s32 owner;                         /* 0x44 Player handle credited with it */
    f32 unk48;                         /* 0x48 */
    f32 unk4C;                         /* 0x4C */
} DebrisParams;

_Static_assert(offsetof(DebrisParams, debris) == 0x40, "DebrisParams.debris");

/* Vtable slots, in the order of the g_*VTable tables (e.g. Destructible.c). */
typedef enum ThingVSlot {
    THING_V_INIT,                     /* 0x00 */
    THING_V_RELEASE,                  /* 0x04 */
    THING_V_PRE_MOVE,                 /* 0x08 */
    THING_V_MOVE,                     /* 0x0C */
    THING_V_DRAW,                     /* 0x10 */
    THING_V_GET_MASS,                 /* 0x14 */
    THING_V_DYNAMIC_COLLIDE_IMPACT,   /* 0x18 */
    THING_V_GET_CAMERA_ORIENTATION,   /* 0x1C */
    THING_V_GET_INTERFACE_INFO,       /* 0x20 */
    THING_V_SLAVE_TRANSFORM,          /* 0x24 */
    THING_V_GET_SURFACE_SCRIPT,       /* 0x28 */
    THING_V_GET_NEAREST_NAV_POINT,    /* 0x2C */
    THING_V_GET_INV_INERTIA_TENSOR,   /* 0x30 */
    THING_V_DYNAMIC_ROTATION,         /* 0x34 */
    THING_V_COLLIDE_WITH_RAY,         /* 0x38 */
    THING_V_GET_TARGET_POS,           /* 0x3C */
    THING_V_GET_ORIENTED_COLLISION,   /* 0x40 */
    THING_V_COUNT
} ThingVSlot;

/* The vtable entry `slot` of `thing`, as a function pointer of type FnType:
 *   THING_VFN(t, THING_V_MOVE, void (*)(Thing *))(t); */
#define THING_VFN(thing, slot, FnType) ((FnType)PTR(GETP(((Thing *)(thing))->vtable)[slot]))

#endif
