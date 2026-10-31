/* Animated models and their playback (AnimHandler.c): MotionAnim is the
 * model and its animation in one record, MotionState plays it on a thing
 * (cars, creatures, wildcard slots, Motion things) and holds the slave things
 * attached to its bones. Offsets are the GameCube layout; the asserts keep
 * them that way. */
#ifndef MOTION_H
#define MOTION_H

#include <stddef.h>
#include "Thing.h"

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

/* A model's skeleton (MotionAnim.skeleton): its bones' base pose. */
typedef struct AnimNode {
    Quaternion rot;                    /* 0x00 base pose */
    f32 pos[3];                        /* 0x10 */
    u8 numChildren;                    /* 0x1C the children follow it */
    u8 pop;                            /* 0x1D */
    u8 next;                           /* 0x1E nodes to its next sibling (its subtree's size) */
    u8 flags;                          /* 0x1F 1: blown off, 2: just blown (AnimNodes_BlowBranch) */
} AnimNode;
_Static_assert(sizeof(AnimNode) == 0x20, "AnimNode size");

typedef struct CreatureSkeleton {
    u32 numBones;                      /* 0x00 */
    u8 pad04[0xC];                     /* 0x04 */
    PTR32(AnimNode) nodes;             /* 0x10 */
} CreatureSkeleton;

/* An animated model (AnimHandler.c): its geometry, skeleton and key frames
 * are one record, so a model is also its animation. Played in three phases,
 * intro, loop and outro, each a time range of the key frames. */
typedef struct MotionRange {
  f32 start;                /* 0x00 */
  f32 end;                  /* 0x04 */
} MotionRange;

/* A geometry drawn on one bone matrix. */
typedef struct MotionPart {
  PTR32(void) geom;         /* 0x00 */
  u16 mtxIndex;             /* 0x04 bone */
  u8 pad06[2];              /* 0x06 */
} MotionPart;

/* MotionAnim.loopMode */
enum {
  MOTION_LOOP_NONE = 0,
  MOTION_LOOP_WRAP = 1,     /* up to maxLoops times, 0: forever */
  MOTION_LOOP_PING_PONG = 2,
};

typedef struct MotionAnim {
  u32 numGeoms;             /* 0x00 skinned geometries (Gfx_DrawAnimGeometryAux) */
  PTR32(PTR32(void)) geoms; /* 0x04 */
  u32 numRigid;             /* 0x08 geometries on one bone */
  PTR32(MotionPart) rigid;  /* 0x0C */
  PTR32(void) keys;         /* 0x10 key frames (Gfx_AnimExtractKeyFrame) */
  PTR32(CreatureSkeleton) skeleton; /* 0x14 numBones first; base pose at 0x10 */
  f32 fps;                  /* 0x18 */
  u8 hasIntro;              /* 0x1C */
  u8 loopMode;              /* 0x1D MOTION_LOOP_* */
  u8 hasOutro;              /* 0x1E */
  u8 maxLoops;              /* 0x1F */
  PTR32(MotionRange) phases[3]; /* 0x20 intro, loop, outro */
  SlaveList events;         /* 0x2C slave triggers fired as their times pass
                             * (MotionState_BlendTriggerEvents) */
  u8 pad34[4];              /* 0x34 */
  PTR32(struct CarPartList) parts; /* 0x38 cars: parts that break off (CarA.partVisMask) */
} MotionAnim;

_Static_assert(offsetof(MotionAnim, skeleton) == 0x14, "MotionAnim.skeleton");
_Static_assert(offsetof(MotionAnim, phases) == 0x20, "MotionAnim.phases");
_Static_assert(offsetof(MotionAnim, parts) == 0x38, "MotionAnim.parts");

/* An animation playing (Motion_BlendHeartbeat_FPS). */
typedef struct MotionLayer {
  PTR32(MotionAnim) anim;   /* 0x00 */
  u8 phase;                 /* 0x04 0 intro, 1 loop, 2 outro */
  u8 loops;                 /* 0x05 loops done */
  u8 reverse;               /* 0x06 playing backwards (ping-pong) */
  u8 pad07;                 /* 0x07 */
  f32 time;                 /* 0x08 */
  f32 blend;                /* 0x0C weight */
  f32 prevTime;             /* 0x10 time before the last step: events between */
  f32 rawTime;              /* 0x14 and this one fire */
  s32 done;                 /* 0x18 the last phase ended */
  u8 pad1C[4];              /* 0x1C */
} MotionLayer;

_Static_assert(sizeof(MotionLayer) == 0x20, "MotionLayer size");

/* A model's animation state and attached slave things (AnimHandler.c),
 * embedded in its owner (CarA at 0x330, Pod at 0xB0). CarA_SlaveWeaponStop
 * passes both the subobject and its slaves list head. */
typedef struct MotionState {
  PTR32(MotionAnim) model;       /* 0x00 the model, which is also its animation */
  u8 mode;                       /* 0x04 phase (MotionState_Init) */
  u8 loops;                      /* 0x05 */
  u8 reverse;                    /* 0x06 */
  u8 numMatrices;                /* 0x07 animMatrices allocated (MatrixCache) */
  u8 pad08[4];                   /* 0x08 */
  f32 time;                      /* 0x0C */
  PTR32(Matrix4) animMatrices;   /* 0x10 per bone */
  ThingList slaves;              /* 0x14 attached slave things */
  MotionLayer layer;             /* 0x18 the animation played */
} MotionState;

_Static_assert(offsetof(MotionState, slaves) == 0x14, "MotionState.slaves");
_Static_assert(offsetof(MotionState, layer) == 0x18, "MotionState.layer");
_Static_assert(sizeof(MotionState) == 0x38, "MotionState size");

#endif
