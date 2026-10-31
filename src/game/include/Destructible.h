/* Destructibles, movers and physical objects (Destructible.c,
 * PhysicalObject.c): creation parameters, surface materials, and the rigid
 * body that physical objects add to Destructible (common.h). Offsets are
 * the GameCube layout; the asserts keep them that way. */
#ifndef DESTRUCTIBLE_H
#define DESTRUCTIBLE_H

#include <stddef.h>

/* Destructible.moveType */
enum {
    DESTRUCTIBLE_STATIC = 0,    /* g_destructibleVTable */
    DESTRUCTIBLE_MOVER = 1,     /* follows an animation track (g_moverVTable) */
    DESTRUCTIBLE_PHYSICAL = 2,  /* rigid body (g_physicalObjectVTable) */
    DESTRUCTIBLE_DRONE = 3,     /* g_droneVTable */
};

/* A mover's track (Destructible.anim): an animation read with
 * Gfx_AnimExtractKeyFrame, looping over [start, end]. */
typedef struct MoverAnim {
    u8 pad00[8];                /* 0x00 */
    f32 start;                  /* 0x08 */
    f32 end;                    /* 0x0C */
} MoverAnim;

/* A key frame of a mover's track. */
typedef struct MoverKey {
    Quaternion rot;             /* 0x00 */
    Vector4 pos;                /* 0x10 */
} MoverKey;

/* ThingManager_DARYL_CreateThing(3 or 10, ...) parameters. */
typedef struct DestructibleParams {
    Vector4 pos;                /* 0x00 */
    Vector4 vel;                /* 0x10 */
    Vector4 angVel;             /* 0x20 physical objects: initial spin */
    Quaternion quat;            /* 0x30 */
    PTR32(ThingRecord) record;  /* 0x40 */
    u32 unk44;                  /* 0x44 */
    f32 drawDist;               /* 0x48 0: the record's */
    u32 unk4C;                  /* 0x4C */
    s32 moveType;               /* 0x50 DESTRUCTIBLE_* */
    f32 speed;                  /* 0x54 movers: track speed */
    u32 unk58;                  /* 0x58 */
    PTR32(MoverAnim) anim;      /* 0x5C movers: track */
} DestructibleParams;

/* A surface: DestructibleInfo.material, CollisionInfo.material, and what
 * the vtable's GetSurfaceScript returns. */
typedef struct SurfaceMaterial {
    u8 pad00[0x14];             /* 0x00 */
    f32 friction;               /* 0x14 */
    f32 restitution;            /* 0x18 */
} SurfaceMaterial;

/* A physical object: a destructible moved as a rigid body (also the layout
 * movers use for their angular velocity). */
typedef struct PhysicalObject {
    Destructible d;             /* 0x00 */
    Vector4 force;              /* 0xF0 accumulated this step */
    Vector4 torque;             /* 0x100 */
    Vector4 angMomentum;        /* 0x110 world space */
    Vector4 invInertia;         /* 0x120 local inverse inertia tensor (diagonal) */
    Vector4 angVel;             /* 0x130 world space */
} PhysicalObject;

_Static_assert(offsetof(DestructibleParams, record) == 0x40, "DestructibleParams.record");
_Static_assert(offsetof(DestructibleParams, moveType) == 0x50, "DestructibleParams.moveType");
_Static_assert(offsetof(DestructibleParams, anim) == 0x5C, "DestructibleParams.anim");
_Static_assert(offsetof(SurfaceMaterial, restitution) == 0x18, "SurfaceMaterial.restitution");
_Static_assert(offsetof(PhysicalObject, force) == 0xF0, "PhysicalObject.force");
_Static_assert(offsetof(PhysicalObject, angMomentum) == 0x110, "PhysicalObject.angMomentum");
_Static_assert(offsetof(PhysicalObject, invInertia) == 0x120, "PhysicalObject.invInertia");
_Static_assert(offsetof(PhysicalObject, angVel) == 0x130, "PhysicalObject.angVel");
_Static_assert(sizeof(PhysicalObject) == 0x140, "PhysicalObject size");

#endif
