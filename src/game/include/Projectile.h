/* Projectiles: bullets, rockets, mines... fired by weapons (Projectile.c),
 * and the homing "smart" ones (SmartProjectiles.c). Offsets are the
 * GameCube layout; the asserts keep them that way. */
#ifndef PROJECTILE_H
#define PROJECTILE_H

#include <stddef.h>
#include "KeyState.h"

/* Homing parameters (ProjectileInfo.smart), SmartProjectiles.c. */
typedef struct SmartProjParams {
    u8 unk00;                   /* 0x00 */
    u8 invertZ;                 /* 0x01 look for targets behind */
    s16 delay;                  /* 0x02 ms before steering starts (Projectile.timer) */
    f32 turnMax;                /* 0x04 rad/s */
    f32 turnOut;                /* 0x08 steering gain turning towards the target */
    f32 turnIn;                 /* 0x0C steering gain when overshooting */
    f32 dropRange;              /* 0x10 beyond it steer gradually, within it aim directly */
    f32 gain;                   /* 0x14 direct-aim gain */
    u8 pad18[4];                /* 0x18 */
    f32 fov;                    /* 0x1C targeting cone, degrees */
    f32 minRange;               /* 0x20 ignore targets closer than this */
} SmartProjParams;

/* A projectile's description in the data pack. */
typedef struct ProjectileInfo {
    u8 draw;                    /* 0x00 1: drawn */
    u8 drawMode;                /* 0x01 0: geometry, 1: billboard sprite */
    u8 pad02;                   /* 0x02 */
    u8 collideMode;             /* 0x03 on a hit (Projectile_DynamicCollisionCorrection):
                                 * 0, 3 impact effect + release (3 ignores things),
                                 * 1, 4 pushed off the surface, 6 sticks to it,
                                 * 2, 5 nothing */
    u8 trailMode;               /* 0x04 trail effect spacing: 0 none, 1 time, 2 distance */
    u8 guidance;                /* 0x05 1, 2: smart (homing), 3: aimed at a car ahead */
    u8 pad06;                   /* 0x06 */
    u8 proximityAttach;         /* 0x07 proximity effect sticks to the car hit */
    u8 impactOrient;            /* 0x08 impact effect orientation: 0 projectile's, 1 surface, 2 reflected */
    u8 pad09[3];                /* 0x09 */
    PTR32(void) trailSpacing;   /* 0x0C [base, range]: u32 ms (trailMode 1) or f32 distance (2) */
    PTR32(void) geometry;       /* 0x10 drawMode 0 */
    s32 lifeBase;               /* 0x14 ms */
    s32 lifeRange;              /* 0x18 */
    s32 damageBase;             /* 0x1C */
    s32 damageRange;            /* 0x20 */
    u8 pad24[8];                /* 0x24 */
    f32 mass;                   /* 0x2C */
    f32 recoil;                 /* 0x30 impulse scale applied to the firing car */
    f32 proximityRadius;        /* 0x34 0: no proximity fuse */
    s32 proximityInterval;      /* 0x38 ms between proximity checks */
    u8 pad3C[4];                /* 0x3C */
    f32 thrust;                 /* 0x40 acceleration along the nose */
    f32 maxSpeed;               /* 0x44 */
    u8 pad48[4];                /* 0x48 */
    f32 holeSizeBase;           /* 0x4C bullet hole on terrain */
    f32 holeSizeRange;          /* 0x50 */
    s32 holeLifeBase;           /* 0x54 */
    s32 holeLifeRange;          /* 0x58 */
    u32 holeTexture;            /* 0x5C 0: no hole */
    PTR32(u8) sprite;           /* 0x60 drawMode 1: first byte is the blend mode */
    PTR32(void) lensFlare;      /* 0x64 LensFlare_Add */
    PTR32(void) light;          /* 0x68 LightHolder_Add */
    u8 pad6C[4];                /* 0x6C */
    u32 trailFx;                /* 0x70 DebrisParams.debris, 0: none */
    u32 impactFx;               /* 0x74 */
    u32 expireFx;               /* 0x78 */
    u8 pad7C[0x10];             /* 0x7C */
    KeyTrack scaleKeys;         /* 0x8C */
    KeyTrack colorKeys;         /* 0x94 */
    u8 slaves[8];               /* 0x9C shared slaves (count, triggers) */
    PTR32(f32) spread;          /* 0xA4 [0..5] direction jitter, [6..11] spin range (degrees) */
    PTR32(SmartProjParams) smart; /* 0xA8 guidance 1, 2 */
    PTR32(GXColor) holeColor;   /* 0xAC */
} ProjectileInfo;

/* ThingManager_DARYL_CreateThing(0, ...) parameters. */
typedef struct ProjectileParams {
    Vector4 pos;                /* 0x00 */
    Vector4 vel;                /* 0x10 */
    f32 gravity;                /* 0x20 added to vel.y per second */
    Quaternion quat;            /* 0x24 */
    PTR32(ProjectileInfo) info; /* 0x34 */
    PTR32(Player) player;       /* 0x38 who fired it, or NULL */
} ProjectileParams;

typedef struct Projectile {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    Vector4 spinAxis;           /* 0xB0 */
    KeyScaleState scale;        /* 0xC0 */
    KeyColorState color;        /* 0xF0 */
    PTR32(ProjectileInfo) info; /* 0x120 */
    s32 life;                   /* 0x124 ms left */
    f32 trailDist;              /* 0x128 accumulated since the last trail effect */
    f32 trailStep;              /* 0x12C spacing of the trail effects */
    s32 lensFlare;              /* 0x130 LensFlare handle */
    s32 light;                  /* 0x134 LightHolder index, -1: none */
    s32 unk138;                 /* 0x138 -1 */
    f32 spinSpeed;              /* 0x13C */
    f32 keyRate;                /* 0x140 key playback speed, so the keys span the life */
    s32 proximityTimer;         /* 0x144 ms to the next proximity check */
    f32 gravity;                /* 0x148 */
    PTR32(Thing) target;        /* 0x14C homing / aim target */
    f32 turnX;                  /* 0x150 smart: turn rates, rad/s */
    f32 turnY;                  /* 0x154 */
    f32 turnZ;                  /* 0x158 */
    u16 bounces;                /* 0x15C collision corrections this frame */
    s16 timer;                  /* 0x15E smart: steering delay, ms */
} Projectile;

#define PROJ_ASSERT_OFFSET(f, off) _Static_assert(offsetof(Projectile, f) == (off), "Projectile." #f)
PROJ_ASSERT_OFFSET(spinAxis, 0xB0);
PROJ_ASSERT_OFFSET(scale, 0xC0);
PROJ_ASSERT_OFFSET(color, 0xF0);
PROJ_ASSERT_OFFSET(info, 0x120);
PROJ_ASSERT_OFFSET(lensFlare, 0x130);
PROJ_ASSERT_OFFSET(spinSpeed, 0x13C);
PROJ_ASSERT_OFFSET(gravity, 0x148);
PROJ_ASSERT_OFFSET(target, 0x14C);
PROJ_ASSERT_OFFSET(bounces, 0x15C);
PROJ_ASSERT_OFFSET(timer, 0x15E);
_Static_assert(sizeof(Projectile) == 0x160, "Projectile size");
#undef PROJ_ASSERT_OFFSET

_Static_assert(offsetof(ProjectileInfo, trailSpacing) == 0x0C, "ProjectileInfo.trailSpacing");
_Static_assert(offsetof(ProjectileInfo, mass) == 0x2C, "ProjectileInfo.mass");
_Static_assert(offsetof(ProjectileInfo, thrust) == 0x40, "ProjectileInfo.thrust");
_Static_assert(offsetof(ProjectileInfo, holeTexture) == 0x5C, "ProjectileInfo.holeTexture");
_Static_assert(offsetof(ProjectileInfo, trailFx) == 0x70, "ProjectileInfo.trailFx");
_Static_assert(offsetof(ProjectileInfo, scaleKeys) == 0x8C, "ProjectileInfo.scaleKeys");
_Static_assert(offsetof(ProjectileInfo, slaves) == 0x9C, "ProjectileInfo.slaves");
_Static_assert(offsetof(ProjectileInfo, holeColor) == 0xAC, "ProjectileInfo.holeColor");
_Static_assert(offsetof(ProjectileParams, quat) == 0x24, "ProjectileParams.quat");
_Static_assert(offsetof(ProjectileParams, player) == 0x38, "ProjectileParams.player");
_Static_assert(offsetof(SmartProjParams, minRange) == 0x20, "SmartProjParams.minRange");

#endif
