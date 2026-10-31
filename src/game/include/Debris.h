/* Debris (Debris.c): loose pieces (car parts, gibs) that fly, spin and
 * bounce off the terrain until they come to rest or expire. Offsets are
 * the GameCube layout; the asserts keep them that way. */
#ifndef DEBRIS_H
#define DEBRIS_H

#include <stddef.h>

/* ThingManager_DARYL_CreateThing(6, ...) parameters. */
typedef struct DebrisPartParams {
    Vector4 pos;                /* 0x00 */
    Quaternion quat;            /* 0x10 */
    Vector4 spinAxis;           /* 0x20 w: spin speed, rad/s */
    Vector4 vel;                /* 0x30 */
    f32 gravity;                /* 0x40 added to vel.y per second */
    PTR32(void) geometry;       /* 0x44 */
    s32 life;                   /* 0x48 ms */
    f32 mass;                   /* 0x4C */
    u32 moves;                  /* 0x50 0: stays where it is (Thing flag 2) */
} DebrisPartParams;

/* Moves in "macro steps" like the particles: every macroTime ms (longer the
 * farther from the camera) it simulates up to macroPos, then glides there. */
typedef struct Debris {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    Vector4 spinAxis;           /* 0xB0 w: spin speed, rad/s */
    PTR32(void) geometry;       /* 0xC0 */
    f32 gravity;                /* 0xC4 */
    s32 life;                   /* 0xC8 ms */
    f32 mass;                   /* 0xCC */
    Vector4 macroPos;           /* 0xD0 where this macro step ends */
    Vector4 macroVel;           /* 0xE0 per second, towards macroPos */
    s32 macroTime;              /* 0xF0 ms left in this macro step */
    u8 padF4[0xC];              /* 0xF4 */
} Debris;

_Static_assert(offsetof(DebrisPartParams, gravity) == 0x40, "DebrisPartParams.gravity");
_Static_assert(offsetof(DebrisPartParams, moves) == 0x50, "DebrisPartParams.moves");
_Static_assert(offsetof(Debris, spinAxis) == 0xB0, "Debris.spinAxis");
_Static_assert(offsetof(Debris, geometry) == 0xC0, "Debris.geometry");
_Static_assert(offsetof(Debris, macroPos) == 0xD0, "Debris.macroPos");
_Static_assert(offsetof(Debris, macroTime) == 0xF0, "Debris.macroTime");
_Static_assert(sizeof(Debris) == 0x100, "Debris size");

#endif
