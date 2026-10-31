/* Particles (ParticleManager.c): pooled geometry particles and billboard
 * sprites, outside the Thing system. Offsets are the GameCube layout; the
 * asserts keep them that way. */
#ifndef PARTICLE_H
#define PARTICLE_H

#include <stddef.h>
#include "KeyState.h"

/* ParticleInfo.type */
enum {
    PARTICLE_GX = 0,            /* geometry (ParticleGX) */
    PARTICLE_SPRITE = 1,        /* billboard (ParticleSprite) */
    PARTICLE_LINE = 2,          /* LineManager_Add */
    PARTICLE_NONE = 3,
};

/* ParticleGX.flags / ParticleSprite.flags */
enum {
    PARTICLE_FLAG_COLLIDES = 0x1,   /* rays against the terrain */
    PARTICLE_FLAG_BOUNCES = 0x2,    /* keeps bouncing until slow (else stops at the first hit) */
    PARTICLE_FLAG_AT_REST = 0x4,
    PARTICLE_FLAG_FREE = 0x8,       /* slot unused */
};

/* A particle effect's description in the data pack (CreateParticle). */
typedef struct ParticleInfo {
    u8 type;                    /* 0x00 PARTICLE_* */
    u8 pad01[2];                /* 0x01 */
    u8 priority;                /* 0x03 dropped sooner the more were made this frame
                                 * (ParticleManager_CanCreatureParticle) */
    PTR32(void) anim;           /* 0x04 ParticleGXAnim / ParticleSpriteAnim */
    s32 lifeBase;               /* 0x08 ms */
    s32 lifeRange;              /* 0x0C */
    u8 pad10[4];                /* 0x10 */
    KeyTrack colorKeys;         /* 0x14 */
    u32 numDeathFx;             /* 0x1C effects (DebrisParams.debris) spawned when it dies */
    PTR32(u32) deathFx;         /* 0x20 */
} ParticleInfo;

/* ParticleInfo.anim of a geometry particle. */
typedef struct ParticleGXAnim {
    u8 orient;                  /* 0x00 0: given quat, 1: random, else identity */
    u8 collide;                 /* 0x01 0: none, 1: dies on hitting the terrain, 2: bounces,
                                 * 3: stops; & 4: doesn't move */
    u8 spinAxis;                /* 0x02 0: random, 1: x, 2: y, 3: z */
    u8 animated;                /* 0x03 cycle the frames (else a random one) */
    f32 spinBase;               /* 0x04 degrees/s (x0.1) */
    f32 spinRange;              /* 0x08 */
    f32 sizeBase;               /* 0x0C */
    f32 sizeRange;              /* 0x10 */
    s32 frameTime;              /* 0x14 ms per frame */
    u32 bounceSound;            /* 0x18 SoundSequence handle, 0: none */
    u8 pad1C[4];                /* 0x1C */
    u32 numFrames;              /* 0x20 */
    PTR32(u32) frames;          /* 0x24 geometry handles */
    KeyTrack scaleKeys;         /* 0x28 */
} ParticleGXAnim;

/* ParticleInfo.anim of a sprite particle. */
typedef struct ParticleSpriteAnim {
    u8 blend;                   /* 0x00 FxBillboard.blend */
    u8 animated;                /* 0x01 */
    u8 pad02[2];                /* 0x02 */
    f32 rotBase;                /* 0x04 degrees, RandomDirVar */
    f32 rotRange;               /* 0x08 */
    f32 rotSpeedBase;           /* 0x0C degrees/s */
    f32 rotSpeedRange;          /* 0x10 */
    f32 sizeBase;               /* 0x14 */
    f32 sizeRange;              /* 0x18 */
    s32 frameTime;              /* 0x1C ms per frame */
    u32 numFrames;              /* 0x20 */
    PTR32(u32) frames;          /* 0x24 texture ids */
    KeyTrack sizeKeys;          /* 0x28 */
} ParticleSpriteAnim;

/* A geometry particle. Moves in "macro steps": every `timer` ms (longer the
 * farther from the camera) it simulates up to macroPos, then glides there. */
typedef struct ParticleGX {
    Vector4 pos;                /* 0x00 */
    Vector4 vel;                /* 0x10 per ms */
    Vector4 accel;              /* 0x20 */
    Quaternion quat;            /* 0x30 */
    Vector4 axis;               /* 0x40 spin axis */
    KeyColorState color;        /* 0x50 */
    KeyScaleState scale;        /* 0x80 */
    f32 bounce;                 /* 0xB0 restitution */
    u8 flags;                   /* 0xB4 PARTICLE_FLAG_* */
    u8 padB5;                   /* 0xB5 */
    u8 tinted;                  /* 0xB6 draw with color.color */
    u8 frame;                   /* 0xB7 */
    f32 speed;                  /* 0xB8 spin rate factor */
    PTR32(ParticleInfo) info;   /* 0xBC */
    PTR32(void) geometry;       /* 0xC0 current frame */
    f32 scaleMul;               /* 0xC4 */
    f32 spin;                   /* 0xC8 */
    s32 life;                   /* 0xCC ms */
    f32 rate;                   /* 0xD0 key playback speed */
    s32 frameTimer;             /* 0xD4 */
    f32 size;                   /* 0xD8 */
    s32 timer;                  /* 0xDC ms to the next macro step */
    Vector4 macroPos;           /* 0xE0 where this macro step ends */
    Vector4 macroVel;           /* 0xF0 per ms, towards macroPos */
} ParticleGX;

/* A billboard particle. */
typedef struct ParticleSprite {
    Vector4 pos;                /* 0x00 */
    Vector4 vel;                /* 0x10 per ms */
    Vector4 accel;              /* 0x20 */
    KeyColorState color;        /* 0x30 */
    KeyF32State size;           /* 0x60 */
    f32 rot;                    /* 0x80 degrees */
    f32 rotSpeed;               /* 0x84 */
    s32 life;                   /* 0x88 ms */
    f32 sizeMul;                /* 0x8C */
    PTR32(ParticleInfo) info;   /* 0x90 */
    u8 flags;                   /* 0x94 PARTICLE_FLAG_* */
    u8 blend;                   /* 0x95 */
    u8 tinted;                  /* 0x96 */
    u8 frame;                   /* 0x97 */
    f32 speed;                  /* 0x98 time scale */
    f32 rate;                   /* 0x9C key playback speed */
    s32 frameTimer;             /* 0xA0 */
    u8 padA4[0xC];              /* 0xA4 */
} ParticleSprite;

#define PARTICLE_GX_COUNT 0x280
#define PARTICLE_SPRITE_COUNT 0x200

typedef struct ParticleGroup {
    ParticleGX particles[PARTICLE_GX_COUNT]; /* 0x00000 */
    u32 next;                   /* 0x28000 ring index of the next slot to use */
    u32 unk28004;               /* 0x28004 */
    u8 pad28008[8];             /* 0x28008 */
} ParticleGroup;

/* g_particleManager: group 0 holds the colliding geometry particles, the
 * others alternate between groups 1 and 2. */
typedef struct ParticleManager {
    ParticleGroup groups[3];    /* 0x00000 */
    ParticleSprite sprites[PARTICLE_SPRITE_COUNT]; /* 0x78030 */
    u32 spriteNext;             /* 0x8E030 */
    u8 pad8E034[0xC];           /* 0x8E034 */
} ParticleManager;

/* The billboard Gfx_AddBillBoardSprite hands out (GDLSprite in the Gfx files). */
typedef struct FxBillboard {
    f32 x, y, z;                /* 0x00 */
    f32 size;                   /* 0x0C */
    u8 pad10[0xC];              /* 0x10 */
    f32 rot;                    /* 0x1C degrees */
    s32 next;                   /* 0x20 Gfx list link */
    GXColor color;              /* 0x24 */
    u8 pad28[2];                /* 0x28 */
    u8 blend;                   /* 0x2A */
    u8 pad2B[5];                /* 0x2B */
} FxBillboard;

_Static_assert(offsetof(ParticleInfo, colorKeys) == 0x14, "ParticleInfo.colorKeys");
_Static_assert(offsetof(ParticleInfo, deathFx) == 0x20, "ParticleInfo.deathFx");
_Static_assert(offsetof(ParticleGXAnim, frames) == 0x24, "ParticleGXAnim.frames");
_Static_assert(offsetof(ParticleGXAnim, scaleKeys) == 0x28, "ParticleGXAnim.scaleKeys");
_Static_assert(offsetof(ParticleSpriteAnim, frames) == 0x24, "ParticleSpriteAnim.frames");
_Static_assert(offsetof(ParticleSpriteAnim, sizeKeys) == 0x28, "ParticleSpriteAnim.sizeKeys");
_Static_assert(offsetof(ParticleGX, color) == 0x50, "ParticleGX.color");
_Static_assert(offsetof(ParticleGX, scale) == 0x80, "ParticleGX.scale");
_Static_assert(offsetof(ParticleGX, flags) == 0xB4, "ParticleGX.flags");
_Static_assert(offsetof(ParticleGX, info) == 0xBC, "ParticleGX.info");
_Static_assert(offsetof(ParticleGX, timer) == 0xDC, "ParticleGX.timer");
_Static_assert(offsetof(ParticleGX, macroVel) == 0xF0, "ParticleGX.macroVel");
_Static_assert(sizeof(ParticleGX) == 0x100, "ParticleGX size");
_Static_assert(offsetof(ParticleSprite, size) == 0x60, "ParticleSprite.size");
_Static_assert(offsetof(ParticleSprite, rot) == 0x80, "ParticleSprite.rot");
_Static_assert(offsetof(ParticleSprite, info) == 0x90, "ParticleSprite.info");
_Static_assert(offsetof(ParticleSprite, frameTimer) == 0xA0, "ParticleSprite.frameTimer");
_Static_assert(sizeof(ParticleSprite) == 0xB0, "ParticleSprite size");
_Static_assert(offsetof(ParticleManager, sprites) == 0x78030, "ParticleManager.sprites");
_Static_assert(offsetof(ParticleManager, spriteNext) == 0x8E030, "ParticleManager.spriteNext");
_Static_assert(sizeof(ParticleManager) == 0x8E040, "ParticleManager size");
_Static_assert(offsetof(FxBillboard, color) == 0x24, "FxBillboard.color");
_Static_assert(offsetof(FxBillboard, blend) == 0x2A, "FxBillboard.blend");
_Static_assert(sizeof(FxBillboard) == 0x30, "FxBillboard size");

#endif
