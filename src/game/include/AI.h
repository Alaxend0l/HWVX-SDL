/* AI players: the computer drivers of cars (AI_CarA.c) and the creature
 * spawner/walker (AI_Creature.c), with the shared part in AI.c.
 *
 * An AI player is a player (PlayerManager.c) like a human one: it starts with
 * the Player header (CarA.h: controls at 0x00, vehicle at 0x2C...), which its
 * heartbeat fills in each frame as if read from a pad, and the vehicle then
 * reads those controls. The AI state follows from 0xB8. Offsets are the
 * GameCube layout; the asserts below keep them that way.
 *
 * Include it after common.h (Creature.h forward-declares the types used in
 * function signatures, for game_protos.h). */
#ifndef AI_H
#define AI_H

#include <stddef.h>
#include "Nav.h"
#include "World.h"

/* The Player header of an AI player (struct Player is in CarA.h). */
#define AI_PLAYER(ai) ((struct Player *)(void *)(ai))

/* Control bytes of the Player header that CarA.h's Player doesn't name yet. */
/* AI creatures steer by heading instead of stickX when Player.flags & 0x100: */

/* g_cameraManager (CameraManager.c): Camera cameras[4], then the u8 count. */
extern u8 g_cameraManager[];
#define AI_CAMERA(i)    (&((Camera *)(void *)g_cameraManager)[i])
#define AI_NUM_CAMERAS  (g_cameraManager[4 * sizeof(Camera)])

/* A queued call (AI_AddEvent, run one per frame or so by AI_ProcessEvent). */
typedef struct AIEvent {
    PTR32(void) data;                  /* 0x00 argument; NULL: func takes none */
    FPTR32(void, ()) func;             /* 0x04 */
    PTR32(struct AIEvent) next;        /* 0x08 queue link */
    s8 queued;                         /* 0x0C */
    u8 pad0D[3];                       /* 0x0D */
} AIEvent;

/* PlayerManager_NewAIPlayer's description, passed on to the AI's Init. */
typedef struct AIPlayerParams {
    u32 script;                        /* 0x00 handle: AICarScript / CreatureAIScript */
    f32 nearSq;                        /* 0x04 AI car: AICar.nearSq */
    f32 farSq;                         /* 0x08 AI car: removed beyond this from every camera */
    u8 dynamic;                        /* 0x0C AI car: spawned on demand (AI_STATE_SPAWNED) */
    u8 kind;                           /* 0x0D AI_KIND_* */
    u8 pad0E[2];                       /* 0x0E */
} AIPlayerParams;

enum {
    AI_KIND_CAR = 0,
    AI_KIND_CREATURE = 1,
};

/* AIPlayer.state */
enum {
    AI_STATE_OFF = 0,                  /* creature AI: free, nothing to drive */
    AI_STATE_SPAWNED = 1,              /* AI car: dynamic, not yet turned to its target */
    AI_STATE_ACTIVE = 2,
    AI_STATE_STATIC = 3,               /* AI car: placed (not dynamic), never removed */
};

/* The part shared by the car and creature AI (AI.c). */
typedef struct AIPlayer {
    u8 header[0xB8];                   /* 0x00 Player header, see AI_PLAYER */
    AIEvent heartbeat;                 /* 0xB8 the kind's AsyncHeartbeat, queued when the timer runs out */
    u32 heartbeatTimer;                /* 0xC8 ms */
    s8 reset;                          /* 0xCC re-initialise on the next heartbeat */
    u8 state;                          /* 0xCD AI_STATE_* */
    u8 padCE[2];                       /* 0xCE */
    PTR32(void) script;                /* 0xD0 AICarScript / CreatureAIScript */
} AIPlayer;

/* ---- Car AI ---- */

/* AIStateInfo.type: what the state targets. */
enum {
    AI_TARGET_HUMAN_CAR = 1,
    AI_TARGET_2 = 2,                   /* never finds anything */
    AI_TARGET_CAR = 3,
    AI_TARGET_ITEM = 6,
    AI_TARGET_ITEM_1_6 = 7,            /* items of ItemRecord.type 1 or 6 */
    AI_TARGET_ITEM_2 = 8,
    AI_TARGET_WILDCARD = 9,            /* ITEM_TYPE_WILDCARD */
    AI_TARGET_NAV_POINT = 10,          /* a random enabled nav point (NavPoint.chance) */
    AI_TARGET_CARRIED_ITEM = 11,       /* ITEM_TYPE_CARRIED, aims at the carrier's car */
    AI_TARGET_ITEM_6 = 13,
    AI_TARGET_WAY_POINT = 14,          /* the way point chain (races) */
};

/* AIStateInfo.subtype */
enum {
    AI_SUBTYPE_LOCAL_STATS = 1,        /* entered only if AIStateInfo.stats hold */
    AI_SUBTYPE_NO_TARGET = 2,          /* AI_Car_CanTarget fails */
    AI_SUBTYPE_TARGET_LIMITS = 3,      /* cars filtered by AIStateInfo.limits */
};

/* AIStateInfo.sortMode: which of the candidates becomes the target. */
enum {
    AI_SORT_NEAREST = 0,
    AI_SORT_FARTHEST = 1,              /* way points: the last one */
    AI_SORT_RANDOM = 2,
    AI_SORT_SPECIAL = 3,               /* items: preferred weapon; way points: first on the path */
};

/* AI_SUBTYPE_TARGET_LIMITS: per-target stat limits (AITargetRecord.stats). */
typedef struct AITargetLimits {
    s32 limit[4];                      /* 0x00 0: unchecked */
    u8 mode;                           /* 0x10 1: targets over the limits are wanted, else excluded */
    u8 compare;                        /* 0x11 1: any stat over its limit; else all of them */
} AITargetLimits;

/* AI_SUBTYPE_LOCAL_STATS: conditions on the own car (1: below, 2: above). */
typedef struct AILocalStats {
    u8 health;                         /* 0x00 health percent vs healthValue */
    u8 grip;                           /* 0x01 CarA.gripScale vs gripValue */
    u8 weapons;                        /* 0x02 loaded weapons vs weaponCount */
    u8 pad03[3];                       /* 0x03 */
    u16 weaponCount;                   /* 0x06 */
    s32 healthValue;                   /* 0x08 */
    f32 gripValue;                     /* 0x0C */
} AILocalStats;

/* One state of an AI script (pack data). */
typedef struct AIStateInfo {
    u8 type;                           /* 0x00 AI_TARGET_* */
    u8 subtype;                        /* 0x01 AI_SUBTYPE_* */
    u8 sortMode;                       /* 0x02 AI_SORT_* */
    u8 pad03[2];                       /* 0x03 */
    u8 unk5;                           /* 0x05 set: the target holds only while retargetTimer runs;
                                          nav points stay valid when reached */
    u8 uses;                           /* 0x06 times it can be entered, 0: unlimited */
    u8 pad07[0xD];                     /* 0x07 */
    f32 minDist;                       /* 0x14 target distance range, 0/0: any */
    f32 maxDist;                       /* 0x18 */
    f32 center;                        /* 0x1C target angle range, degrees (AI_CheckAngleInRange) */
    f32 range;                         /* 0x20 */
    f32 approachNear;                  /* 0x24 AI_Car_DriveToTarget: brakes nearer than the */
    f32 approachFar;                   /* 0x28 middle of these, eases off nearer than far */
    u8 pad2C[8];                       /* 0x2C */
    u32 retargetTime;                 /* 0x34 ms, AIStateSlot.retargetTimer */
    s32 duration[2];                   /* 0x38 ms, RandomAdd_s32(base, range) */
    s32 heartbeat[2];                  /* 0x40 ms between AsyncHeartbeats while active */
    s32 cooldown[2];                   /* 0x48 ms before it can be entered again */
    u8 pad50[8];                       /* 0x50 */
    union {
        PTR32(AILocalStats) stats;     /* 0x58 AI_SUBTYPE_LOCAL_STATS */
        PTR32(AITargetLimits) limits;  /* 0x58 AI_SUBTYPE_TARGET_LIMITS */
    };
    u32 debris;                        /* 0x5C effect spawned on entering, 0: none */
} AIStateInfo;

/* The state tree of an AI script. */
typedef struct AIStateNode {
    PTR32(AIStateInfo) info;           /* 0x00 NULL: grouping node */
    u32 numChildren;                   /* 0x04 */
    PTR32(struct AIStateNode) children; /* 0x08 */
} AIStateNode;

/* An AI car's script (AIPlayer.script). */
typedef struct AICarScript {
    f32 healthScale;                   /* 0x00 scales the car's health */
    f32 gripScale;                     /* 0x04 scales CarA.gripScale */
    u8 pad08[4];                       /* 0x08 */
    f32 speed;                         /* 0x0C cruising speed (AICar.speedLimit) */
    u8 pad10[4];                       /* 0x10 */
    f32 reloadScale[2];                /* 0x14 RandomAdd(base, range) times the weapon reload */
    f32 damageScale;                   /* 0x1C car-car bumper hits */
    u8 pad20[6];                       /* 0x20 */
    u8 preferredWeapon;                /* 0x26 WeaponInfo.priority it prefers */
    u8 pad27;                          /* 0x27 */
    AIStateNode root;                  /* 0x28 */
} AICarScript;

/* Runtime state of one AIStateInfo (AICar.slots, in tree order). */
typedef struct AIStateSlot {
    PTR32(AIStateInfo) info;           /* 0x00 NULL: end of the slots */
    u32 durationTimer;                 /* 0x04 ms */
    u32 cooldownTimer;                 /* 0x08 ms */
    u32 retargetTimer;                 /* 0x0C ms */
    PTR32(void) target;                /* 0x10 Thing or NavPoint */
    u8 usesLeft;                       /* 0x14 */
    u8 pad15[3];                       /* 0x15 */
} AIStateSlot;

/* Per-target stats (AI_SUBTYPE_TARGET_LIMITS). Nothing in the game fills
 * these in; AI_Car_Heartbeat ages them out after 30 s. */
typedef struct AITargetRecord {
    PTR32(void) thing;                 /* 0x00 NULL: end of the records */
    s16 stats[4];                      /* 0x04 vs AITargetLimits.limit */
    f32 age[2];                        /* 0x0C ms */
} AITargetRecord;

/* A tile car's scripts (TileCar.aiScripts, Player.aiScripts). */
typedef struct AIScriptSet {
    u8 pad00[0x90];                    /* 0x00 */
    u32 scripts[4];                    /* 0x90 AICarScript per difficulty */
} AIScriptSet;

/* TileCar (the world tile an AI car spawns from) is in World.h. */

/* WeaponInfo (ItemRecord.weapon) is in ItemThing.h. */

/* A weapon slot of a car's wildcard rack (the slave thing of type 8). */
typedef struct AIWeaponSlot {
    u8 pad00[0x40];                    /* 0x00 */
    s16 ammo;                          /* 0x40 */
    u8 pad42[0xA];                     /* 0x42 */
    PTR32(ItemRecord) record;          /* 0x4C NULL: empty */
} AIWeaponSlot;

/* The wildcard rack as the AI reads it. */
typedef struct AIWeaponRack {
    u8 pad00[0xB0];                    /* 0x00 */
    AIWeaponSlot slots[12];            /* 0xB0 */
    u8 current;                        /* 0x470 selected slot */
} AIWeaponRack;


/* A car's AI player (0x670 bytes, PlayerMgr cars[]). */
typedef struct AICar {
    AIPlayer base;                     /* 0x00 script: AICarScript */
    AIStateSlot slots[32];             /* 0xD4 */
    union {
        PTR32(AIStateSlot) active[5];  /* 0x3D4 the active branch of the tree, NULL-ended */
        struct {
            u8 pad3D4[0x10];
            PTR32(AIStateSlot) current; /* 0x3E4 = active[4]: the state being run, NULL: none */
        };
    };
    PTR32(Thing) obstacle;             /* 0x3E8 destructible in the way (AI_Car_AsyncScanDestructibles) */
    PTR32(Thing) targetThing;          /* 0x3EC */
    PTR32(NavPoint) targetNav;         /* 0x3F0 */
    u8 pad3F4[0xC];                    /* 0x3F4 */
    Vector4 steerPoint;                /* 0x400 where it drives to this frame */
    f32 targetDistSq;                  /* 0x410 the current target's limits, for the next state */
    f32 minDistSq;                     /* 0x414 */
    f32 center;                        /* 0x418 */
    f32 range;                         /* 0x41C */
    PTR32(NavPoint) path[32];          /* 0x420 NULL-ended (NavHolder_FillPointerArrayPath) */
    s32 wayPoint;                      /* 0x4A0 next way point; -1: none, -2: not started */
    u32 pathIndex;                     /* 0x4A4 next point of path[]; >= 32: none */
    s32 pathDest;                      /* 0x4A8 nav point index path[] leads to, -1: none */
    u32 pathTimer;                     /* 0x4AC ms until the path is recomputed */
    AITargetRecord records[16];        /* 0x4B0 */
    u32 unk5F0;                        /* 0x5F0 zeroed every heartbeat (with unk5F4) */
    u32 unk5F4;                        /* 0x5F4 */
    AIEvent scanEvent;                 /* 0x5F8 AI_Car_AsyncScanDestructibles */
    u32 scanTimer;                     /* 0x608 ms */
    u8 pad60C[4];                      /* 0x60C */
    Vector4 stuckForward;              /* 0x610 forward axis when the unstick turn began */
    Vector4 lastPos;                   /* 0x620 */
    u32 stillFrames;                   /* 0x630 frames it hasn't moved */
    s32 stuckTimer;                    /* 0x634 frames: > 0 unsticking, < 0 holding off */
    u8 pad638[4];                      /* 0x638 */
    f32 speedLimit;                    /* 0x63C 0: none */
    s16 steer;                         /* 0x640 stickX being built up */
    u8 pad642[2];                      /* 0x642 */
    AIEvent weaponEvent;               /* 0x644 AI_Car_WeaponAsyncHeartbeat */
    u32 weaponTimer;                   /* 0x654 ms */
    u32 weaponSwitchTimer;             /* 0x658 ms before switching weapon again */
    f32 nearSq;                        /* 0x65C AIPlayerParams.nearSq */
    f32 farSq;                         /* 0x660 AIPlayerParams.farSq */
    u8 pad664[0xC];                    /* 0x664 */
} AICar;

/* ---- Creature AI ---- */

/* A creature AI's script (AIPlayer.script). */
typedef struct CreatureAIScript {
    u8 pad00[0x18];                    /* 0x00 */
    f32 noticeDistSq;                  /* 0x18 cars nearer than this are noticed */
} CreatureAIScript;

/* A point creatures in a volume walk between. */
typedef struct CreatureNode {
    Vector4 pos;                       /* 0x00 */
    u8 pad10[4];                       /* 0x10 */
    u32 enabled;                       /* 0x14 */
    u32 numLinks;                      /* 0x18 */
    PTR32(PTR32(struct CreatureNode)) links; /* 0x1C */
} CreatureNode;

/* The pack data of a creature volume (volume type 5, CreatureVolume.info). */
typedef struct CreatureVolumeInfo {
    u8 disabled;                       /* 0x00 */
    u8 pad01[3];                       /* 0x01 */
    u32 maxAlive;                      /* 0x04 */
    PTR32(u32) creatures;              /* 0x08 u32 count, then PTR32(CreatureInfo) list */
    u32 numNodes;                      /* 0x0C */
    PTR32(PTR32(CreatureNode)) nodes;  /* 0x10 */
} CreatureVolumeInfo;

/* A volume (VolumeManager) of type 5. */
typedef struct CreatureVolume {
    u8 pad00[4];                       /* 0x00 */
    u32 active;                        /* 0x04 */
    u8 pad08[0x24];                    /* 0x08 */
    PTR32(CreatureVolumeInfo) info;    /* 0x2C */
} CreatureVolume;

/* TileCreature is in World.h. */

/* The creature AI player (0x100 bytes, PlayerMgr.creature): drives one
 * creature at a time, spawned from a tile or a volume. */
typedef struct AICreature {
    AIPlayer base;                     /* 0x00 script: CreatureAIScript */
    PTR32(struct AICreature) next;     /* 0xD4 ring of creature AIs */
    PTR32(SpawnVolume) volume;         /* 0xD8 spawned in this volume, or NULL (from a tile) */
    s32 volumeNode;                    /* 0xDC index into its nodes it spawned at, -1: tile */
    PTR32(CreatureNode) node;          /* 0xE0 node walked to */
    PTR32(NearestCar) nearestCar;      /* 0xE4 car it noticed */
    f32 nearestDistSq;                 /* 0xE8 */
    u8 wanderTime;                     /* 0xEC 0: pick a new wander heading/speed */
    u8 run;                            /* 0xED holds button14 */
    u8 padEE[2];                       /* 0xEE */
    f32 speed;                         /* 0xF0 -1..1, < 0 backwards */
    f32 heading;                       /* 0xF4 radians */
    u32 leaving;                       /* 0xF8 walks to a node and disappears */
    s32 unkFC;                         /* 0xFC */
} AICreature;

#define AI_ASSERT(T, f, off) _Static_assert(offsetof(T, f) == (off), #T "." #f)
AI_ASSERT(AIEvent, queued, 0x0C);
AI_ASSERT(AIPlayerParams, kind, 0x0D);
AI_ASSERT(AIPlayer, heartbeat, 0xB8);
AI_ASSERT(AIPlayer, heartbeatTimer, 0xC8);
AI_ASSERT(AIPlayer, reset, 0xCC);
AI_ASSERT(AIPlayer, state, 0xCD);
AI_ASSERT(AIPlayer, script, 0xD0);
AI_ASSERT(AITargetLimits, compare, 0x11);
AI_ASSERT(AILocalStats, gripValue, 0x0C);
AI_ASSERT(AIStateInfo, minDist, 0x14);
AI_ASSERT(AIStateInfo, retargetTime, 0x34);
AI_ASSERT(AIStateInfo, cooldown, 0x48);
AI_ASSERT(AIStateInfo, stats, 0x58);
AI_ASSERT(AIStateInfo, debris, 0x5C);
AI_ASSERT(AICarScript, reloadScale, 0x14);
AI_ASSERT(AICarScript, preferredWeapon, 0x26);
AI_ASSERT(AICarScript, root, 0x28);
AI_ASSERT(AIStateSlot, target, 0x10);
AI_ASSERT(AIStateSlot, usesLeft, 0x14);
AI_ASSERT(AITargetRecord, age, 0x0C);
AI_ASSERT(TileCar, aiScripts, 0x24);
AI_ASSERT(TileCar, dynamic, 0x34);
AI_ASSERT(TileCar, nearDist, 0x38);
AI_ASSERT(TileCar, item, 0x54);
AI_ASSERT(TileCar, respawn, 0x5C);
AI_ASSERT(AIWeaponSlot, record, 0x4C);
AI_ASSERT(AIWeaponRack, current, 0x470);
AI_ASSERT(AICar, slots, 0xD4);
AI_ASSERT(AICar, active, 0x3D4);
AI_ASSERT(AICar, current, 0x3E4);
AI_ASSERT(AICar, obstacle, 0x3E8);
AI_ASSERT(AICar, steerPoint, 0x400);
AI_ASSERT(AICar, path, 0x420);
AI_ASSERT(AICar, wayPoint, 0x4A0);
AI_ASSERT(AICar, records, 0x4B0);
AI_ASSERT(AICar, scanEvent, 0x5F8);
AI_ASSERT(AICar, stuckForward, 0x610);
AI_ASSERT(AICar, stuckTimer, 0x634);
AI_ASSERT(AICar, speedLimit, 0x63C);
AI_ASSERT(AICar, weaponEvent, 0x644);
AI_ASSERT(AICar, farSq, 0x660);
AI_ASSERT(CreatureAIScript, noticeDistSq, 0x18);
AI_ASSERT(CreatureNode, links, 0x1C);
AI_ASSERT(CreatureVolumeInfo, nodes, 0x10);
AI_ASSERT(CreatureVolume, info, 0x2C);
AI_ASSERT(TileCreature, spawnable, 0x20);
AI_ASSERT(AICreature, next, 0xD4);
AI_ASSERT(AICreature, nearestCar, 0xE4);
AI_ASSERT(AICreature, speed, 0xF0);
AI_ASSERT(AICreature, unkFC, 0xFC);
_Static_assert(sizeof(AIEvent) == 0x10, "AIEvent size");
_Static_assert(sizeof(AIStateSlot) == 0x18, "AIStateSlot size");
_Static_assert(sizeof(AITargetRecord) == 0x14, "AITargetRecord size");
_Static_assert(sizeof(TileCar) == 0x60, "TileCar size");
_Static_assert(sizeof(AIWeaponSlot) == 0x50, "AIWeaponSlot size");
_Static_assert(sizeof(TileCreature) == 0x40, "TileCreature size");
_Static_assert(sizeof(AICar) == 0x670, "AICar size");
_Static_assert(sizeof(AICreature) == 0x100, "AICreature size");
#undef AI_ASSERT

#endif
