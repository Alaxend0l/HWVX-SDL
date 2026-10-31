/* Collision shapes and results shared by the Collision*.c files, things and
 * physics. Offsets are the GameCube layout; the asserts keep them that way. */
#ifndef COLLISION_H
#define COLLISION_H

#include <stddef.h>

/* An oriented box in world space, as built by BuildOOB from a thing's local
 * box and transform. Pooled in g_aOBox (OBoxHolder_Get / _Release). */
typedef struct CollisionOBox {
    Vector4 center;             /* 0x00 */
    Vector4 corners[8];         /* 0x10 */
    f32 extent[3];              /* 0x90 half sizes along axis[] */
    u8 pad9C;                   /* 0x9C */
    s8 inUse;                   /* 0x9D pool slot taken */
    u8 pad9E[2];                /* 0x9E */
    PTR32(Vector4) axis[3];     /* 0xA0 unit axes (rows of the owner's transform) */
    u8 padAC[4];                /* 0xAC */
    f32 centerProj[3];          /* 0xB0 center projected onto each axis */
    u8 padBC[4];                /* 0xBC */
    f32 velProj[3];             /* 0xC0 velocity projected onto each axis */
    u32 cachedPoly;             /* 0xCC last terrain poly hit (CollisionTriCache), handle */
    Vector4 min;                /* 0xD0 world-space bounds */
    Vector4 max;                /* 0xE0 */
} CollisionOBox;

_Static_assert(offsetof(CollisionOBox, inUse) == 0x9D, "CollisionOBox.inUse");
_Static_assert(offsetof(CollisionOBox, axis) == 0xA0, "CollisionOBox.axis");
_Static_assert(offsetof(CollisionOBox, centerProj) == 0xB0, "CollisionOBox.centerProj");
_Static_assert(offsetof(CollisionOBox, cachedPoly) == 0xCC, "CollisionOBox.cachedPoly");
_Static_assert(offsetof(CollisionOBox, min) == 0xD0, "CollisionOBox.min");
_Static_assert(sizeof(CollisionOBox) == 0xF0, "CollisionOBox size");

/* A contact, filled in by the collision tests and passed to the vtable's
 * DynamicCollideImpact and the surface scripts. */
typedef struct CollisionInfo {
    Vector4 pos;                /* 0x00 contact point */
    Vector4 normal;             /* 0x10 */
    u32 thingA;                 /* 0x20 Thing handles of the two parties */
    u32 thingB;                 /* 0x24 (CollisionInfo_GetOther) */
    u32 material;               /* 0x28 surface (handle) */
    u8 pad2C[0x14];             /* 0x2C */
    u8 type;                    /* 0x40 0xFF: pushed out, 0xFE: embedded */
    u8 pad41[0xF];              /* 0x41 */
} CollisionInfo;

_Static_assert(offsetof(CollisionInfo, material) == 0x28, "CollisionInfo.material");
_Static_assert(offsetof(CollisionInfo, type) == 0x40, "CollisionInfo.type");
_Static_assert(sizeof(CollisionInfo) == 0x50, "CollisionInfo size");

#endif
