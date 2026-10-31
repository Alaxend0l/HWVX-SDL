/* Creatures: the animated walkers of the world (thing type 7, Creature.c),
 * driven by the controls of their player (Thing.player), usually the
 * creature AI (AI_Creature.c). Offsets are the GameCube layout.
 *
 * common.h includes this early, so it also forward-declares the AI and nav
 * types (AI.h, Nav.h) that function signatures in game_protos.h use. */
#ifndef CREATURE_H
#define CREATURE_H

#include <stddef.h>

typedef struct NavPoint NavPoint;
typedef struct NavData NavData;
typedef struct AIEvent AIEvent;
typedef struct AIPlayer AIPlayer;
typedef struct AIPlayerParams AIPlayerParams;
typedef struct AIStateInfo AIStateInfo;
typedef struct AIStateNode AIStateNode;
typedef struct AIStateSlot AIStateSlot;
typedef struct AICar AICar;
typedef struct AICreature AICreature;

/* Creature_MotionStateInit states (Creature_InterpretControls picks them) */
enum {
    CREATURE_ANIM_IDLE = 0,
    CREATURE_ANIM_WALK = 1,            /* stickY forward */
    CREATURE_ANIM_RUN = 2,             /* stickY forward + button14 */
    CREATURE_ANIM_AIR = 4,             /* off the ground */
    CREATURE_ANIM_BACK = 5,            /* stickY back */
    CREATURE_ANIM_TURN = 6,            /* stickX right */
    CREATURE_ANIM_BUTTON1B = 7,        /* busy */
    CREATURE_ANIM_BUTTON1A = 9,
    CREATURE_ANIM_COMBO17 = 14,        /* Player.combo 17; busy */
    CREATURE_ANIM_COMBO18 = 15,        /* Player.combo 18; busy */
    /* 8, 10, 11: busy, not picked by the controls */
};

/* Creature flags (Thing.flags) */
enum {
    CREATURE_FLAG_NO_PHYSICS = 0x4,    /* MacroStep doesn't move it */
    CREATURE_FLAG_BUSY = 0x10,         /* animation can't be interrupted */
    CREATURE_FLAG_DYING = 0x20,        /* death animation playing */
    CREATURE_FLAG_DRAWN = 0x4000,      /* set by Draw, cleared by MicroStep */
};

/* One animation of a creature (0x60 bytes). */
typedef struct CreatureAnimEntry {
    u32 model;                         /* 0x00 Model handle the MotionState plays, 0: none */
    f32 forward;                       /* 0x04 root motion per animation frame */
    f32 side;                          /* 0x08 */
    f32 lift;                          /* 0x0C impulse animations: vertical velocity */
    f32 start;                         /* 0x10 animation time window the motion applies in */
    f32 end;                           /* 0x14 */
    u8 unk18;                          /* 0x18 */
    u8 impulse;                        /* 0x19 motion applied once, at start (jumps) */
    u8 pad1A[0x46];                    /* 0x1A */
} CreatureAnimEntry;

/* Pieces a creature loses when gibbed (Creature_PartialGibb). */
typedef struct DamagePart {
    u8 spawned;                        /* 0x00 */
    u8 useOffset;                      /* 0x01 */
    u16 unk2;                          /* 0x02 */
    u16 bone;                          /* 0x04 */
    u16 unk6;                          /* 0x06 */
    PTR32(void) geom;                  /* 0x08 */
    PTR32(Vector4) offset;             /* 0x0C in bone space, if useOffset */
    PTR32(Vector4) slavePos;           /* 0x10 NULL: origin */
    u32 debris;                        /* 0x14 effect spawned with it, 0: none */
    PTR32(void) slaveInfo;             /* 0x18 slave attached to the piece, or NULL */
} DamagePart;

typedef struct DamagePartList {
    u32 count;                         /* 0x00 */
    PTR32(DamagePart) parts;           /* 0x04 */
} DamagePartList;

/* A creature's description (pack data, Creature.info). */
typedef struct CreatureInfo {
    u8 pad00[4];                       /* 0x00 */
    s32 health;                        /* 0x04 */
    u8 pad08[0xC];                     /* 0x08 */
    u32 numAnims;                      /* 0x14 */
    PTR32(CreatureAnimEntry) anims;    /* 0x18 indexed by Creature_MotionStateInit */
    u8 pad1C[0x10];                    /* 0x1C */
    u32 numDeathAnims;                 /* 0x2C */
    PTR32(CreatureAnimEntry) deathAnims; /* 0x30 indexed by Creature.deathAnim */
    u8 pad34[0x10];                    /* 0x34 */
    u32 aiScripts[4];                  /* 0x44 CreatureAIScript per difficulty */
    PTR32(s32) dropOff;                /* 0x54 dropped at a pod: [0] health bonus, [3] points */
    u8 dropType;                       /* 0x58 Player.counters index */
    u8 pad59[0x17];                    /* 0x59 */
    PTR32(DamagePartList) damageParts; /* 0x70 NULL: can't be gibbed */
} CreatureInfo;

/* Animation data a MotionState plays. */
typedef struct CreatureAnim {
    u8 pad00[0x18];                    /* 0x00 */
    f32 fps;                           /* 0x18 */
    u8 pad1C[0xC];                     /* 0x1C */
    PTR32(f32) unk28;                  /* 0x28 [1]: time hit animations play uninterrupted */
} CreatureAnim;

/* A Model's skeleton (Model.numBones points to it). */
typedef struct AnimNode {
    u8 pad00[0x1F];                    /* 0x00 */
    u8 flags;                          /* 0x1F 1: blown off, 2: just blown (AnimNodes_BlowBranch) */
} AnimNode;

typedef struct CreatureSkeleton {
    u32 numBones;                      /* 0x00 */
    u8 pad04[0xC];                     /* 0x04 */
    PTR32(AnimNode) nodes;             /* 0x10 */
} CreatureSkeleton;

/* Parameters of ThingManager_DARYL_CreateThing(7) / Creature_Init. */
typedef struct CreatureInit {
    Vector4 pos;                       /* 0x00 */
    f32 heading;                       /* 0x10 radians */
    PTR32(CreatureInfo) info;          /* 0x14 */
    PTR32(void) player;                /* 0x18 the AI player driving it */
} CreatureInit;

typedef struct Creature {
    Thing base;                        /* 0x00 health < 0: dead; trigger: its TileCreature;
                                          player: whose controls it follows */
    u8 padAC[4];                       /* 0xAC */
    f32 heading;                       /* 0xB0 radians */
    f32 turnRate;                      /* 0xB4 radians/s, from stickX */
    PTR32(CreatureInfo) info;          /* 0xB8 */
    u8 padBC[3];                       /* 0xBC */
    s8 onGround;                       /* 0xBF */
    f32 unkC0;                         /* 0xC0 */
    s32 animState;                     /* 0xC4 Creature_MotionStateInit state */
    f32 blendRate;                     /* 0xC8 per second, blends in a new animation */
    /* 0xCC: its MotionState (CarA.h): model, animMatrices, slaves, then the
     * blend layers (Motion_BlendHeartbeat_FPS(&anim, 2, ...)). */
    PTR32(void) model;                 /* 0xCC Model */
    u8 padD0[0xC];                     /* 0xD0 */
    PTR32(Matrix4) bones;              /* 0xDC world matrix per bone */
    PTR32(Thing) slaves;               /* 0xE0 attached slave things */
    PTR32(CreatureAnim) anim;          /* 0xE4 the animation playing */
    u8 animStatus;                     /* 0xE8 2: finished */
    u8 unkE9;                          /* 0xE9 */
    u8 padEA[2];                       /* 0xEA */
    f32 animTime;                      /* 0xEC */
    f32 animWeight;                    /* 0xF0 blend weight of anim */
    f32 animPrevTime;                  /* 0xF4 this frame's time window: entry->start */
    f32 animCurTime;                   /* 0xF8 crossing it fires an impulse */
    u32 animLooped;                    /* 0xFC set: ends CREATURE_FLAG_BUSY */
    u8 pad100[4];                      /* 0x100 */
    PTR32(CreatureAnim) prevAnim;      /* 0x104 the animation blended out */
    u8 pad108[8];                      /* 0x108 */
    f32 prevAnimWeight;                /* 0x110 */
    s32 animIndex;                     /* 0x114 into CreatureInfo.anims / deathAnims */
    PTR32(CreatureAnimEntry) animEntry; /* 0x118 */
    u8 pad11C[4];                      /* 0x11C */
    Vector4 groundNormal;              /* 0x120 */
    f32 skin;                          /* 0x130 pushed off what it hits by this */
    u8 pad134[0x5C];                   /* 0x134 */
    /* Physics run in macro steps (Creature_MacroStep, rarer the further it is
     * from the cameras) and is interpolated in between (Creature_MicroStep). */
    Vector4 stepPos;                   /* 0x190 where the last macro step ends */
    Vector4 stepVel;                   /* 0x1A0 velocity reaching it in stepTimer */
    s32 stepTimer;                     /* 0x1B0 ms to the next macro step */
    s32 deadTime;                      /* 0x1B4 ms since death; released after 20 s */
    u32 unk1B8;                        /* 0x1B8 & 1: the creature AI runs about */
    f32 mass;                          /* 0x1BC */
    f32 unk1C0;                        /* 0x1C0 1: normal; < 1: frozen, <= 0: not drawn */
    u8 pad1C4[4];                      /* 0x1C4 */
    u32 gibbedBones;                   /* 0x1C8 bit per bone blown off */
    u32 deathAnim;                     /* 0x1CC index into CreatureInfo.deathAnims */
} Creature;

#define CREATURE_ASSERT(f, off) _Static_assert(offsetof(Creature, f) == (off), "Creature." #f)
CREATURE_ASSERT(heading, 0xB0);
CREATURE_ASSERT(info, 0xB8);
CREATURE_ASSERT(onGround, 0xBF);
CREATURE_ASSERT(animState, 0xC4);
CREATURE_ASSERT(model, 0xCC);
CREATURE_ASSERT(bones, 0xDC);
CREATURE_ASSERT(slaves, 0xE0);
CREATURE_ASSERT(anim, 0xE4);
CREATURE_ASSERT(animTime, 0xEC);
CREATURE_ASSERT(animLooped, 0xFC);
CREATURE_ASSERT(prevAnim, 0x104);
CREATURE_ASSERT(prevAnimWeight, 0x110);
CREATURE_ASSERT(animIndex, 0x114);
CREATURE_ASSERT(groundNormal, 0x120);
CREATURE_ASSERT(skin, 0x130);
CREATURE_ASSERT(stepPos, 0x190);
CREATURE_ASSERT(stepTimer, 0x1B0);
CREATURE_ASSERT(mass, 0x1BC);
CREATURE_ASSERT(gibbedBones, 0x1C8);
CREATURE_ASSERT(deathAnim, 0x1CC);
_Static_assert(sizeof(Creature) == 0x1D0, "Creature size");
#undef CREATURE_ASSERT
_Static_assert(offsetof(CreatureInfo, deathAnims) == 0x30, "CreatureInfo.deathAnims");
_Static_assert(offsetof(CreatureInfo, aiScripts) == 0x44, "CreatureInfo.aiScripts");
_Static_assert(offsetof(CreatureInfo, damageParts) == 0x70, "CreatureInfo.damageParts");
_Static_assert(sizeof(CreatureAnimEntry) == 0x60, "CreatureAnimEntry size");
_Static_assert(sizeof(DamagePart) == 0x1C, "DamagePart size");
_Static_assert(offsetof(CreatureInit, player) == 0x18, "CreatureInit.player");

#endif
