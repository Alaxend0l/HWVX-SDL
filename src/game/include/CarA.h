/* Cars (CarA, the Thing kind behind every driveable vehicle), their data-pack
 * description (CarAInfo and its parts), the players driving them (Player)
 * and the MotionState animation subobject.
 *
 * Offsets are the GameCube layout; the asserts keep them that way. Pointer
 * fields are PTR32 handles (see README, "64-bit hosts"). */
#ifndef CARA_H
#define CARA_H

#include "common.h"

/* MotionAnim, MotionLayer and MotionState are in Motion.h. */

/* The player object a car is driven by (GETP(CarA.player)): its controller state
 * sits at the start, read by CarA_PreMove_Control_HW. */
typedef struct Player {
  s8 stickX;                /* 0x00 */
  s8 stickY;                /* 0x01 */
  s8 stick2X;               /* 0x02 second stick (Looker: move) */
  s8 stick2Y;               /* 0x03 */
  f32 aiHeading;            /* 0x04 radians; creatures use it when flags & PLAYER_FLAG_AI_HEADING */
  f32 aiSpeed;              /* 0x08 animation speed scale, likewise */
  u32 buttonsHeld;          /* 0x0C pressed this frame: bit per down[] entry (4: turbo) */
  union {
    u8 down[14];            /* 0x10 analog/held value per button (Player_DefaultControlReader) */
    struct {
      u8 button10;          /* 0x10 */
      u8 buttonSwitch;      /* 0x11 weapon switch (buttonsHeld & 2) */
      u8 throttle;          /* 0x12 */
      u8 brake;             /* 0x13 */
      u8 button14;          /* 0x14 */
      u8 button15;          /* 0x15 */
      u8 pad16[4];          /* 0x16 */
      u8 button1A;          /* 0x1A */
      u8 buttonFire;        /* 0x1B fire (buttonsHeld & 0x800); creatures: attack */
      u8 control1C;         /* 0x1C cleared by AI_Heartbeat */
      u8 button1D;          /* 0x1D */
    };
  };
  u8 pad1E[2];              /* 0x1E never read (GC, PS2) */
  FPTR32(void, (void *player)) controlReader;  /* 0x20 fills the input fields (pad, AI) */
  FPTR32(void, (void *player, void *desc)) init; /* 0x24 PlayerManager_New*Player */
  FPTR32(void, (void *player)) heartbeat;      /* 0x28 */
  PTR32(void) vehicle;            /* 0x2C the Thing the player drives */
  s8 thingLost;             /* 0x30 its thing died (Player_DetachThingDeath) */
  u8 cameraIndex;           /* 0x31 passed to the vehicle's getCameraInfo */
  u8 combo;                 /* 0x32 pending combo/cheat action, run by CarA_ExecuteCombo */
  u8 pad33;                 /* 0x33 never read (GC, PS2) */
  u32 deadTime;             /* 0x34 ms since its thing was lost */
  u32 type;                 /* 0x38 PLAYER_TYPE_* */
  PTR32(void) aiScripts;          /* 0x3C its thing's info (CarA.vehicleIface, Creature.info): AI scripts at 0x90 */
  char name[0x20];          /* 0x40 */
  u32 port;                 /* 0x60 human players: controller port, 1-based */
  u32 view;                 /* 0x64 viewport, 1-based */
  u32 interfaceIndex;       /* 0x68 its HUD interface */
  u32 flags;                /* 0x6C & 0x80 cleared every camera update */
  u8 pad70;                 /* 0x70 0x70-0xA7: stats, cleared by Player_Init and copied
                               to and from the save data (DDRescueGameData.playerStats);
                               this byte is otherwise never read (GC, PS2) */
  u8 scoreDirty;            /* 0x71 score changed: refresh the HUD text */
  u8 pad72[6];              /* 0x72 saved and restored with the stats; never read (GC, PS2) */
  s32 kills;                /* 0x78 */
  u32 stuntPoints;          /* 0x7C capped at 99999 */
  u8 pad80[0xC];            /* 0x80 saved and restored with the stats; never read (GC, PS2) */
  s32 damageDealt;          /* 0x8C */
  u8 pad90[4];              /* 0x90 saved and restored with the stats; never read (GC, PS2) */
  u16 counters[8];          /* 0x94 pod: creatures collected, by type */
  u8 countersChanged;       /* 0xA4 */
  u8 padA5[3];              /* 0xA5 saved and restored with the stats; never read (GC, PS2) */
  PTR32(struct TileCar) tileCar;  /* 0xA8 the world tile car it was spawned from */
  s16 deathNavPoint;        /* 0xAC */
  s16 respawnNavPoint;      /* 0xAE */
  PTR32(struct Player) next; /* 0xB0 PlayerManager active / free list */
  PTR32(struct Player) prev; /* 0xB4 */
  /* 0xB8-0x113: human players (the null player ends at 0xB8); AI players
   * keep their AIPlayer fields here instead (AI.h), aiScript among them. */
  u8 keys[14];              /* 0xB8 key binding per action (Input_KeyDown) */
  u8 stickMode;             /* 0xC6 */
  u8 substickMode;          /* 0xC7 */
  s32 device;               /* 0xC8 controller channel: pad input, force feedback */
  u32 comboCount;           /* 0xCC */
  PTR32(struct AICarScript) aiScript; /* 0xD0 AI players: the script driving it (AI.h) */
  u32 comboInputs[16];      /* 0xD4 recent inputs, matched against cheat combos */
  PTR32(char) stuntScoreText;     /* 0x114 human players: -> stuntScoreBuf */
  char stuntScoreBuf[0x40]; /* 0x118 "Points +n" */
} Player;

enum {
  PLAYER_TYPE_CAR = 9,      /* Player.type: drives a car */
};
#define PLAYER_FLAG_AI_HEADING 0x100  /* Player.flags: creatures steer by aiHeading/aiSpeed */

_Static_assert(offsetof(Player, aiHeading) == 0x04, "Player.aiHeading");
_Static_assert(offsetof(Player, buttonFire) == 0x1B, "Player.buttonFire");
_Static_assert(offsetof(Player, heartbeat) == 0x28, "Player.heartbeat");
_Static_assert(offsetof(Player, vehicle) == 0x2C, "Player.vehicle");
_Static_assert(offsetof(Player, next) == 0xB0, "Player.next");
_Static_assert(offsetof(Player, type) == 0x38, "Player.type");
_Static_assert(offsetof(Player, interfaceIndex) == 0x68, "Player.interfaceIndex");
_Static_assert(offsetof(Player, damageDealt) == 0x8C, "Player.damageDealt");
_Static_assert(offsetof(Player, counters) == 0x94, "Player.counters");
_Static_assert(offsetof(Player, tileCar) == 0xA8, "Player.tileCar");
_Static_assert(offsetof(Player, device) == 0xC8, "Player.device");
_Static_assert(offsetof(Player, aiScript) == 0xD0, "Player.aiScript");
_Static_assert(offsetof(Player, stuntScoreText) == 0x114, "Player.stuntScoreText");
_Static_assert(offsetof(Player, name) == 0x40, "Player.name");
_Static_assert(sizeof(Player) == 0x158, "Player size (human players)");

/* A camera mode a vehicle hands out (GETP(CameraInfo.subMode)); the cars'
 * CarACameras entries (12 bytes) start like this. */
typedef struct CameraSubMode {
  u8 type;                  /* 0x00 0: default, 2: zoom, 3: orbit, 4: bone (in-car) */
  u8 pad1[3];               /* 0x01 */
  PTR32(void) params;       /* 0x04 */
} CameraSubMode;

/* Filled by the vehicle's vtable GetCameraOrientation for CameraManager. */
typedef struct CameraInfo {
  u8 pad0[0x8];             /* 0x00 */
  f32 unused08;             /* 0x08 */
  f32 unused0C;             /* 0x0C */
  f32 unused10;             /* 0x10 */
  f32 fov;                  /* 0x14 */
  u8 pad18[8];              /* 0x18 */
  Matrix4 mtx;              /* 0x20 v[1]: the up vector */
  Vector4 target;           /* 0x60 looked at */
  Vector4 from;             /* 0x70 trims the camera position */
  s32 unused80;             /* 0x80 */
  PTR32(CameraSubMode) subMode; /* 0x84 NULL: mtx is the camera */
  u32 cameraCount;          /* 0x88 modes the player can cycle through */
  f32 lookAhead;            /* 0x8C */
} CameraInfo;

_Static_assert(offsetof(CameraInfo, mtx) == 0x20, "CameraInfo.mtx");
_Static_assert(offsetof(CameraInfo, subMode) == 0x84, "CameraInfo.subMode");
_Static_assert(offsetof(CameraInfo, lookAhead) == 0x8C, "CameraInfo.lookAhead");

/* A car's chase cameras: count entries of 12 bytes; byte 0 of an entry is
 * its type (4: in-car view, the car itself is not drawn). */
typedef struct CarACameras {
  u32 count;                /* 0x00 */
  PTR32(u8) list;           /* 0x04 */
} CarACameras;

typedef struct CarAEngine {
  f32 bodyOffset[3];        /* 0x00 rest value of CarA.bodyOffset */
  f32 unused0C;             /* 0x0C CarA_Init sets 1, never read */
  s32 selfRightDelay;       /* 0x10 ms airborne before the self-righting torque @xdl: units=ms */
  f32 steerRate;            /* 0x14 rad/s the steering angle ramps to the input @xdl: units=rad/s */
  u32 reverseDelay;         /* 0x18 ms reverse before the car answers the throttle @xdl: units=ms */
  u8 pad1C[4];              /* 0x1C never read (GC, PC, PS2) */
  f32 selfRightArm;         /* 0x20 */
  f32 torqueCurve[4];       /* 0x24 torque at 0 and max rpm, slopes there (CarA_FitCubic) */
  f32 animSpeed;            /* 0x34 scales the steering animation time */
  f32 maxImpactVel;         /* 0x38 car-car impacts cap the closing speed at it, scaled by
                             *      CarA_GetImpactMultiplier (ThingManager_MoveThings); 50
                             *      in most cars */
  f32 maxAngularVel;        /* 0x3C */
  f32 itemDropImpact;       /* 0x40 */
  f32 maxGrade;             /* 0x44 grade the car can hold, times turboRpmScale in turbo */
  u8 pad48[0x10];           /* 0x48 never read (GC, PC, PS2) */
  f32 tiltForceScale;       /* 0x58 force point scale by up.y */
  f32 damageScale;          /* 0x5C car-car bumper damage per unit of closing speed
                             *      (0.04-0.15 by car) */
  u8 pad60[4];              /* 0x60 pointer to the record CarAInfo 0x4C points to; never
                             *      read (GC, PC, PS2) */
  f32 minEnginePitch;       /* 0x64 semitones added at 0 rpm @xdl: units=semitones */
  f32 soundRpmRange;        /* 0x68 rpm per pitch step, CarA_UpdateSound gate @xdl: units=rpm */
  f32 offsetContactFlag;    /* 0x6C 1: the spring scale keeps wheel->offset */
  f32 itemHeight;           /* 0x70 picked-up items ride this high above the rack */
  u8 pad74[0xC];            /* 0x74 never read (GC, PC, PS2) */
  CarACameras cameras;      /* 0x80 */
  CarACameras splitCameras; /* 0x88 used when the screen is split */
} CarAEngine;

_Static_assert(offsetof(CarAEngine, maxImpactVel) == 0x38, "CarAEngine.maxImpactVel");
_Static_assert(offsetof(CarAEngine, damageScale) == 0x5C, "CarAEngine.damageScale");
_Static_assert(offsetof(CarAEngine, cameras) == 0x80, "CarAEngine.cameras");
_Static_assert(sizeof(CarAEngine) == 0x90, "CarAEngine size");

typedef struct CarAMaterial {
  u8 pad00[0x18];           /* 0x00 */
  f32 restitution;          /* 0x18 bounce of impulses (CarA_ProcessImpulse) */
} CarAMaterial;

/* Spheres a collider of type 7 is made of: center, then radius in the
 * next vector's x (0x20 each). */
typedef struct CollisionSphere {
  Vector4 center;           /* 0x00 local */
  Vector4 radius;           /* 0x10 .x */
} CollisionSphere;

typedef struct CollisionSpheres {
  u32 count;                /* 0x00 */
  PTR32(CollisionSphere) list; /* 0x04 */
} CollisionSpheres;

/* The same record as DestructibleInfo (common.h): the car code reads fewer
 * of its fields. */
typedef struct CarACollider {
  f32 inertiaScale[3];      /* 0x00 per-axis factor of the inverse inertia tensor */
  u8 pad0C[4];              /* 0x0C 0 in every car; never read (GC, PC, PS2) */
  u8 type;                  /* 0x10 0: oriented box, 1: convex hull */
  u8 pad11[3];              /* 0x11 u8s (1, 0, 0 in most cars): DestructibleInfo's
                             *      takesDamage; never read (GC, PC, PS2) */
  s32 maxHealth;            /* 0x14 */
  s32 damageThreshold;      /* 0x18 impacts above it hurt */
  u8 pad1C[4];              /* 0x1C s32 375 or -1: DestructibleInfo's deathTime (ms);
                             *      never read (GC, PC, PS2) */
  f32 mass;                 /* 0x20 */
  f32 gravityScale;         /* 0x24 forceAccum.y += gravityScale * mass */
  u8 pad28[4];              /* 0x28 0 in every car; never read (GC, PC, PS2) */
  f32 gravity;              /* 0x2C */
  PTR32(CarAMaterial) material;   /* 0x30 */
  u32 numEndFx;             /* 0x34 debris spawned when destroyed, one at random */
  PTR32(u32) endFx;         /* 0x38 (Thing_ProcessEndEvent) */
  u8 pad3C[8];              /* 0x3C u32 1, pointer: DestructibleInfo's numDebris,
                             *      debris; never read (GC, PC, PS2) */
  union {
    PTR32(Vector4) box;           /* 0x44 type 0: min/max corners */
    PTR32(struct CollisionSpheres) spheres; /* type 7 */
  };
  PTR32(struct CarAHull) hull;    /* 0x48 type 1 */
} CarACollider;

_Static_assert(offsetof(CarACollider, endFx) == 0x38, "CarACollider.endFx");
_Static_assert(sizeof(CarACollider) == 0x4C, "CarACollider size");

typedef struct CarAHull {
  u8 pad00[0xC];            /* 0x00 */
  u32 numVerts;             /* 0x0C */
  u8 pad10[0xC];            /* 0x10 */
  PTR32(Vector4) verts;           /* 0x1C */
} CarAHull;


typedef struct CarAAccel {
  u8 pad00[4];              /* 0x00 0 in every car; never read (GC, PC, PS2) */
  f32 driveRpmScale;        /* 0x04 drive friction to rpm scale */
  f32 coastRpmScale;        /* 0x08 PC only: driveMode 2 off the throttle uses it in place
                             *      of driveRpmScale (ComputeSuspensionForces 0x416DA2);
                             *      1.5 (0 in one car), no shipped car is mode 2 */
  u8 pad0C[8];              /* 0x0C 0 in every car; never read (GC, PC, PS2) */
  f32 tractionLift;         /* 0x14 traction point lift: (this - 1) * up * contact height */
  f32 brakeDecel;           /* 0x18 rpm/s at full brake @xdl: units=rpm/s */
  f32 engineBrake;          /* 0x1C rpm/s off the throttle @xdl: units=rpm/s */
  f32 reverseThrottleDecel; /* 0x20 rpm/s at full reverse throttle @xdl: units=rpm/s */
  f32 airDownforceBase;     /* 0x24 */
  f32 airDownforceRate;     /* 0x28 */
  f32 airDownforceMax;      /* 0x2C */
  s32 airDownforceDelay;    /* 0x30 ms @xdl: units=ms */
  f32 spinImpulse;          /* 0x34 */
  f32 spinDecay;            /* 0x38 */
} CarAAccel;

_Static_assert(offsetof(CarAAccel, coastRpmScale) == 0x08, "CarAAccel.coastRpmScale");
_Static_assert(sizeof(CarAAccel) == 0x3C, "CarAAccel size");

typedef struct CarAStuntInfo {
  f32 pitchRate;            /* 0x00 */
  f32 rollRate;             /* 0x04 */
  f32 spinRate;             /* 0x08 */
  f32 maxTime;              /* 0x0C */
  u8 pad10[4];              /* 0x10 f32 0.5 in every car; never read (GC, PC, PS2) */
  f32 turboPerPoint;        /* 0x14 */
  f32 pitchScore;           /* 0x18 */
  f32 rollScore;            /* 0x1C */
  u8 pad20[8];              /* 0x20 f32 1, f32 5 in every car; never read (GC, PC, PS2) */
  s32 startDelay;           /* 0x28 ms airborne before the stunt can start @xdl: units=ms */
  f32 minFailTime;          /* 0x2C also the stunt-time range landed stunts score in */
  f32 maxFailTime;          /* 0x30 */
  u32 numRewardThings;      /* 0x34 */
  PTR32(u32) rewardThings;        /* 0x38 picked by score */
  u32 numMessages;          /* 0x3C */
  PTR32(PTR32(PTR32(char))) messages; /* 0x40 picked by score: string tables */
  u8 pad44[4];              /* 0x44 0 in every car; never read (GC, PC, PS2) */
  PTR32(PTR32(char)) failMessage;       /* 0x48 NULL: g_stuntLossString */
  u32 numFailThings;        /* 0x4C */
  PTR32(u32) failThings;          /* 0x50 thing ids, picked by fail time */
  f32 spinScore;            /* 0x54 */
} CarAStuntInfo;

_Static_assert(offsetof(CarAStuntInfo, startDelay) == 0x28, "CarAStuntInfo.startDelay");
_Static_assert(sizeof(CarAStuntInfo) == 0x58, "CarAStuntInfo size");

/* CarAInfo.gears: indexed by CarA.curveTableIdx. */
typedef struct CarAGear {
  f32 ratio;                /* 0x00 wheel speed / rpm */
  f32 soundSet;             /* 0x04 gears[1]: the wind sound (CarASoundSet slot, CarA_Init) */
} CarAGear;

/* A looping car sound (CarA_Init starts one SoundSource per sound). */
typedef struct CarASoundSet {
  u8 type;                  /* 0x00 0: positional, with range */
  u8 pad01[0xB];            /* 0x01 */
  f32 pitchScale;           /* 0x0C */
  u8 pad10[4];              /* 0x10 */
  u32 sample;               /* 0x14 */
  PTR32(f32) range;         /* 0x18 SoundSource params 2 and 3 */
} CarASoundSet;

/* The HUD layouts of a car, by number of viewports. */
typedef struct CarAHud {
  u8 pad00[0x10];           /* 0x00 0 (car 16: f32 -0.35, 15 at 0x04); never read (GC, PC, PS2) */
  u32 layout1;              /* 0x10 one viewport */
  u8 pad14[4];              /* 0x14 0 in every car; never read (GC, PC, PS2) */
  u32 layout2;              /* 0x18 two */
  u32 layout4;              /* 0x1C three or four */
  u8 pad20[0x14];           /* 0x20 0, a pointer at 0x2C; never read (GC, PC, PS2) */
  u8 stats[4];              /* 0x34 vehicle select bars (InterfaceHudInfo.carStats)
                             *      @xdl: hint=bar lengths on the car select screen */
} CarAHud;

_Static_assert(offsetof(CarAHud, stats) == 0x34, "CarAHud.stats");
_Static_assert(sizeof(CarAHud) == 0x38, "CarAHud size");

/* A slave thing description (0x20 bytes) as MotionState_InitAllSlavesNow and
 * ThingManagerList_TriggerSlaves take them. */
typedef struct CarASlave {
  u8 pad00[0x14];           /* 0x00 */
  f32 threshold;            /* 0x14 damage set: health % below which it shows */
  u8 pad18[8];              /* 0x18 */
} CarASlave;

/* CarAInfo.slaves is an array of these (12 bytes each); MotionState_InitAllSlavesNow
 * takes &set->count. Set 0: always on, 1: turbo, 2: damage, 9: frontal hit. */
typedef struct CarASlaveSet {
  u32 unused0;              /* 0x00 */
  union {
    SlaveList list;         /* 0x04 as MotionState_InitAllSlavesNow takes it */
    struct {
      u32 count;            /* 0x04 */
      PTR32(CarASlave) slaves; /* 0x08 the damage set's view (thresholds) */
    };
  };
} CarASlaveSet;

/* Slave things a car starts with (CarAInfo.startSlaves), 0x50 bytes each. */
typedef struct CarAStartSlave {
  u8 pad00[0x32];           /* 0x00 */
  u16 tag;                  /* 0x32 slave tag (ThingManager_DARYL_CreateSlave) */
  s32 count;                /* 0x34 summed by interface condition 4 (InterfaceManager_CheckConditions) */
  u8 pad38[4];              /* 0x38 */
  u32 enabled;              /* 0x3C 0: skipped */
  u8 pad40[0x10];           /* 0x40 */
} CarAStartSlave;

/* The car's model is a MotionAnim (CarAModelRef.data, CarA.motionState.model). */

typedef struct CarAModelRef {
  u8 pad00[0x10];           /* 0x00 */
  PTR32(MotionAnim) data;         /* 0x10 the model (CarA.motionState.model) */
  PTR32(void) anims[3];           /* 0x14 additive animations, timed by CarA.animTimes */
} CarAModelRef;

/* Extra points CarA_ComputeForces pushes the car down at (GETP(CarAInfo.forcePoints)). */
typedef struct CarAForcePoint {
  Vector4 pos;              /* 0x00 body space */
  u8 ray;                   /* 0x10 only when a ray down hits something */
  u8 pad11[3];              /* 0x11 */
  f32 weight;               /* 0x14 */
  u8 pad18[0x18];           /* 0x18 */
} CarAForcePoint;

typedef struct CarAForcePoints {
  u8 pad00[4];              /* 0x00 */
  u32 count;                /* 0x04 */
  PTR32(CarAForcePoint) points;   /* 0x08 */
} CarAForcePoints;

typedef struct CarAInfo {
  f32 viewOffset[3];        /* 0x00 shifts the car when an interface shows it
                             *      (InterfaceManager_RenderObject) */
  u8 pad0C[4];              /* 0x0C 0 in every car; never read (GC, PC, PS2) */
  u8 damagePercent;         /* 0x10 health % below which parts break off @xdl: units=%; hint=0..100 */
  u8 driveMode;             /* 0x11 1: human-controlled */
  u8 pad12[2];              /* 0x12 two u8; never read (GC, PC, PS2) */
  u16 skidLife;             /* 0x14 skid mark life in 16 ms units (Trail_AddSegment
                             *      life << 4): 600 (9.6 s) in every car @xdl: units=16 ms */
  u8 pad16[2];              /* 0x16 u16, 0 in every car; never read (GC, PC, PS2) */
  s32 damageLevel;          /* 0x18 hits above it break parts off */
  f32 maxRpm;               /* 0x1C max rpm (torque curve, engine pitch) @xdl: units=rpm */
  f32 upshiftRpm;           /* 0x20 shift up above it; also caps reverse rpm @xdl: units=rpm */
  f32 downshiftRpm;         /* 0x24 shift down below it @xdl: units=rpm */
  u8 pad28[8];              /* 0x28 f32 1000, u32 18 (21 in car 23); never read (GC, PC, PS2) */
  f32 turboRpmScale;        /* 0x30 4WD (turbo): rpm divisor */
  f32 turboTorqueScale;     /* 0x34 4WD (turbo): torque factor */
  u8 pad38[0x10];           /* 0x38 u32 1, u32 16 (1 in cars 0, 26), u32 0, texture slot
                             *      0x44 (relocated by the loader); never read (GC, PC, PS2) */
  PTR32(void) steerAnim;          /* 0x48 */
  u8 pad4C[4];              /* 0x4C pointer to a 0x40-byte record; never read (GC, PC, PS2) */
  u32 skidMarkTexture;      /* 0x50 texture slot of the skid marks (Trail_AddSegment), 0: none
                             *      @xdl: texture */
  PTR32(void) reflectionGeom;     /* 0x54 drawn under human cars in the garage (state 5)
                                   *      @xdl: geometry */
  u8 pad58[8];              /* 0x58 0, CarASoundSet pointer; never read (GC, PC, PS2) */
  PTR32(CarASoundSet) rollSound;  /* 0x60 CarA.soundIds[2] */
  u8 pad64[8];              /* 0x64 CarASoundSet pointer (car 16 only), 0; never read (GC, PC, PS2) */
  PTR32(GXColor) skidColor;       /* 0x6C */
  u8 pad70[8];              /* 0x70 0 in every car; never read (GC, PC, PS2) */
  PTR32(CarAModelRef) modelRef;   /* 0x78 */
  PTR32(f32) gripScale;           /* 0x7C initial CarA.gripScale */
  PTR32(CarAEngine) engine;       /* 0x80 */
  PTR32(CarAHud) hud;             /* 0x84 */
  PTR32(CarAAccel) accel;         /* 0x88 */
  PTR32(CarAStuntInfo) stunt;     /* 0x8C */
  u32 aiScripts[4];         /* 0x90 AICarScript handles, per difficulty @xdl: link=AICarScript */
  PTR32(CarACollider) collider;   /* 0xA0 */
  PTR32(struct CarAForcePoints) forcePoints; /* 0xA4 extra force points (CarA_ComputeForces) */
  u8 padA8[8];              /* 0xA8 0, pointer to {f32 12100 (110^2), f32 900 (30^2), 0...};
                             *      never read (GC, PC, PS2) */
  u32 explosionThing;       /* 0xB0 effect spawned when an AI car dies (DebrisParams.debris)
                             *      @xdl: link=EventSeq */
  u8 padB4[0x1C];           /* 0xB4 0 in every car; never read (GC, PC, PS2) */
  u32 numWheels;            /* 0xD0 */
  PTR32(CarTrailBone) wheels;     /* 0xD4 */
  u32 numGears;             /* 0xD8 */
  PTR32(CarAGear) gears;          /* 0xDC torque curve table */
  u32 numSlaves;            /* 0xE0 */
  PTR32(CarASlaveSet) slaves;     /* 0xE4 [numSlaves] */
  u32 numSlaveWeapons;      /* 0xE8 */
  PTR32(CarASlaveSet) slaveWeapons; /* 0xEC */
  u32 numPickupSlaves;      /* 0xF0 */
  PTR32(CarASlaveSet) pickupSlaves; /* 0xF4 indexed by item value */
  u8 padF8[8];              /* 0xF8 u32 1, pointer to 12-byte {0, pointer, 0}; never read
                             *      (GC, PC, PS2) */
  u32 numStartSlaves;       /* 0x100 */
  PTR32(CarAStartSlave) startSlaves; /* 0x104 */
  u32 numStartItems;        /* 0x108 */
  PTR32(PTR32(ItemRecord)) startItems; /* 0x10C put into the wildcard rack */
  u32 numShiftSounds;       /* 0x110 */
  PTR32(u32) shiftSounds;         /* 0x114 */
  u32 numSoundSets;         /* 0x118 */
  PTR32(PTR32(CarASoundSet)) soundSets; /* 0x11C */
} CarAInfo;

_Static_assert(offsetof(CarAInfo, skidLife) == 0x14, "CarAInfo.skidLife");
_Static_assert(offsetof(CarAInfo, pad28) == 0x28, "CarAInfo.pad28");
_Static_assert(offsetof(CarAInfo, pad38) == 0x38, "CarAInfo.pad38");
_Static_assert(offsetof(CarAInfo, skidMarkTexture) == 0x50, "CarAInfo.skidMarkTexture");
_Static_assert(offsetof(CarAInfo, padA8) == 0xA8, "CarAInfo.padA8");
_Static_assert(offsetof(CarAInfo, padF8) == 0xF8, "CarAInfo.padF8");
_Static_assert(sizeof(CarAInfo) == 0x120, "CarAInfo size");

/* Per-wheel runtime state, 8 slots in CarA from 0x1B0. */
typedef struct CarAWheelState {
  f32 rotation;             /* 0x00 advanced by speed / radius every PreMove */
  s16 dustTimer;            /* 0x04 ms */
  s16 sparkTimer;           /* 0x06 ms */
  s32 skidTrail;            /* 0x08 Trail_AddSegment handle, -1: no trail */
  f32 compression;          /* 0x0C */
  f32 lastCompression;      /* 0x10 */
  GXColor skidColor;        /* 0x14 */
  u32 skidLife;             /* 0x18 CarAInfo.skidLife, or the surface's (type 2) */
  f32 damperRate;           /* 0x1C port: compression rate last sampled for the damper */
  Vector4 slipVel;          /* 0x20 */
} CarAWheelState;

/* ThingManager_DARYL_CreateThing parameters for a car (CarA_Init). */
typedef struct CarAParams {
  Vector4 pos;              /* 0x00 */
  Quaternion quat;          /* 0x10 */
  Vector4 vel;              /* 0x20 */
  PTR32(CarAInfo) info;     /* 0x30 */
  f32 unused34;             /* 0x34 */
  u32 unused38;             /* 0x38 */
  PTR32(Player) player;     /* 0x3C */
} CarAParams;

/* A car: the Thing kind behind every driveable vehicle (vtable g_carAVTable).
 * Pooled in 0x5A0-byte ThingManager slots. */
typedef struct CarA {
  Thing base;               /* 0x00 vel, transform (pos = position), quat,
                               radius, flags, health, vtable, trigger (the world
                               tile it was created from), player (the driver) */
  u8 padAC[0x10];           /* 0xAC never read (GC, PS2) */
  PTR32(struct CarA) lookTarget;  /* 0xBC a looker's target */
  CollisionOBox orientedBox; /* 0xC0 type 0 collider in world space, rebuilt by
                               GetOrientedCollision when flags & 0x180 */
  CarAWheelState wheelStates[8]; /* 0x1B0 one per CarAInfo.wheels entry */
  MotionState motionState;   /* 0x330 the car's model, animation and slaves */
  u8 pad368[8];             /* 0x368 never read (GC, PS2) */
  PTR32(void) hull;               /* 0x370 type 1 collider: OConvexHullHolder slot */
  u32 partVisMask;          /* 0x374 bit set per car part broken off */
  f32 steerAngle;           /* 0x378 steering angle, radians */
  u8 pad37C[4];             /* 0x37C never read (GC, PS2) */
  Vector4 angularMomentum;  /* 0x380 += torque * dt in PreMove */
  Vector4 forceAccum;       /* 0x390 += force in CarA_ProcessForce */
  Vector4 torqueAccum;      /* 0x3A0 += torque in CarA_ProcessForce */
  s32 navPointIndex;        /* 0x3B0 nearest nav point, -1: not searched yet */
  s32 navUpdateTimer;       /* 0x3B4 ms until the nearest nav point is searched again */
  u32 numWheelsGrounded;    /* 0x3B8 */
  PTR32(CarAInfo) vehicleIface;   /* 0x3BC the car's description (CarAInfo) */
  Matrix4 inverseBodyInertia; /* 0x3C0 diagonal: 0x3C0, 0x3D4, 0x3E8 */
  s32 turboTime;            /* 0x400 ms of turbo left */
  s32 lowSpeedFlag;         /* 0x404 ms since the last collision; compared < 500 by AI cars */
  u8 pad408[8];             /* 0x408 never read (GC, PS2) */
  Vector4 localAxis;        /* 0x410 angular velocity (CarA_GetAngularVelocity) */
  f32 animTimes[3];         /* 0x420 times of vehicleIface->modelRef->anims */
  u8 pad42C[0x14];          /* 0x42C never read (GC, PS2) */
  Vector4 prevVelocity;     /* 0x440 velocity at the start of PreMove */
  u32 reverseTimer;         /* 0x450 ms the brake is held at standstill */
  PTR32(void) rollSurface;        /* 0x454 surface whose rolling sound plays */
  f32 gradeOrReverse;       /* 0x458 wheel speed; < 0: reversing (speedometer, AI braking) */
  s32 stuckTimer;           /* 0x45C ms set on a reset, counts down */
  s32 airTime;              /* 0x460 ms since the wheels left the ground */
  s32 stuckCounter;         /* 0x464 > 600 marks the AI car stuck */
  f32 gripScale;            /* 0x468 *GETP(CarAInfo.gripScale), *= AI gripScale */
  union {
    s32 slaveWeaponRpm[8];  /* 0x46C ms since slave weapon i fired (CarA_SlaveWeaponHeartbeat);
                               >= 10000: idle, Init and SlaveWeaponStop write 10001 */
    struct {
      s32 gripLossTime;     /* 0x46C weapon 0 active (< 10000): no wheel grip and no camera
                               look-ahead (ComputeSuspensionForces, GetCameraOrientationInfo);
                               Blast effect 2 sets 9000 and knocks the car down */
      s32 engineRpmState;   /* 0x470 weapon 1 active (< 10000): torque and steering halved */
      s32 pad474;           /* 0x474 weapon 2 (Blast effect 4 sets 0): only the
                               SlaveWeaponHeartbeat loop reads it */
      s32 damageRpm;        /* 0x478 weapon 3 active (< 10000): no impact damage */
    };
  };
  u32 transmissionFlags;    /* 0x48C state bits, e.g. 0x1000: turbo on (see CarA.c) */
  u8 tiltState;             /* 0x490 0: rest, 1/2: tilted by the stick, 3 */
  u8 stuntAxis;             /* 0x491 0: none, 1: pitch (stickX), 2: roll (stickY) */
  u8 pad492[2];             /* 0x492 never read (GC, PS2) */
  /* 0x494-0x4A0: passed to the HUD by GetInterfaceInfo, which clears them */
  PTR32(PTR32(char)) stuntPopup;        /* 0x494 */
  u32 pickupId;             /* 0x498 last picked-up item's id */
  PTR32(PTR32(char)) stuntScoreMessage; /* 0x49C */
  PTR32(PTR32(char)) stuntMessage;      /* 0x4A0 */
  s32 pickupCount;          /* 0x4A4 type 9 items picked up */
  u8 pad4A8[8];             /* 0x4A8 never read (GC, PS2) */
  Vector4 stuntSpin;        /* 0x4B0 zeroed when a stunt fails */
  f32 torquePolyCoeffs[4];  /* 0x4C0 cubic torque curve in rpm (CarA_FitCubic) */
  u8 pad4D0[0x10];          /* 0x4D0 never read (GC, PS2) */
  f32 soundRpmGate;         /* 0x4E0 engine sound gate, left at 0 here */
  f32 rpm;                  /* 0x4E4 */
  u8 curveTableIdx;         /* 0x4E8 current gear, index into CarAInfo.gears */
  u8 pad4E9[7];             /* 0x4E9 never read (GC, PS2) */
  s32 soundIds[3];          /* 0x4F0 SoundSource ids, -1: none: wind, engine, rolling */
  f32 gripBlend;            /* 0x4FC 0 while slipping, recovers to 1 at 1/s */
  s32 damageAccum;          /* 0x500 damage not yet taken off health (in steps of 30) */
  s32 groundedTime;         /* 0x504 ms */
  f32 groundedScale;        /* 0x508 */
  f32 forwardAccel;         /* 0x50C velocity += axis[0] * this * (1/60); strafe impulse,
                               scaled by car speed, cancelled later in the same frame */
  Vector4 stuntAxisVec;     /* 0x510 world axis the stunt spins around */
  Vector4 bodyOffset;       /* 0x520 centre of mass in body space: the model and
                               colliders are drawn/built at XformVec(mtx, this) */
  Vector4 groundNormal;     /* 0x530 */
  union {
    Vector4 colliderMin;    /* 0x540 collider's minimum corner (body space, CarA_Init) */
    struct {
      f32 tiltX;            /* 0x540 the pivot when the car is tilted upright */
      f32 tiltY;            /* 0x544 */
      f32 unused548;        /* 0x548 */
      f32 pad54C;           /* 0x54C never read (GC, PS2) */
    };
  };
  s32 turboCooldown;        /* 0x550 ms */
  f32 stuntTime;            /* 0x554 */
  f32 damperTickTime;       /* 0x558 port: time since the damper's last sample */
  s32 navTimer;             /* 0x55C ms until the path to the target arrow is searched again */
  f32 pathLength;           /* 0x560 of the path found */
  f32 navDist;              /* 0x564 left along it (Player_Heartbeat) */
  PTR32(struct NavPoint) path[7]; /* 0x568 nav points (s16 index at 0x28) of the path the
                               car follows, NULL-terminated (CarA_Release) */
  /* 0x584-0x58C: speedometer smoothing (GetInterfaceInfo): the shown speed
     follows a change of direction only after more than 5 frames */
  s8 speedoRising;          /* 0x584 */
  u8 pad585[3];             /* 0x585 never read (GC, PS2) */
  u32 speedoCount;          /* 0x588 frames the speed went against speedoRising */
  u32 speedoShown;          /* 0x58C */
  u8 pad590[0x10];          /* 0x590 never read (GC, PS2) */
} CarA;

#define CARA_ASSERT_OFFSET(f, off) _Static_assert(offsetof(CarA, f) == (off), "CarA." #f)
CARA_ASSERT_OFFSET(lookTarget, 0xBC);
CARA_ASSERT_OFFSET(orientedBox, 0xC0);
CARA_ASSERT_OFFSET(wheelStates, 0x1B0);
CARA_ASSERT_OFFSET(motionState, 0x330);
CARA_ASSERT_OFFSET(hull, 0x370);
CARA_ASSERT_OFFSET(partVisMask, 0x374);
CARA_ASSERT_OFFSET(steerAngle, 0x378);
CARA_ASSERT_OFFSET(angularMomentum, 0x380);
CARA_ASSERT_OFFSET(forceAccum, 0x390);
CARA_ASSERT_OFFSET(torqueAccum, 0x3A0);
CARA_ASSERT_OFFSET(navPointIndex, 0x3B0);
CARA_ASSERT_OFFSET(navUpdateTimer, 0x3B4);
CARA_ASSERT_OFFSET(numWheelsGrounded, 0x3B8);
CARA_ASSERT_OFFSET(vehicleIface, 0x3BC);
CARA_ASSERT_OFFSET(inverseBodyInertia, 0x3C0);
CARA_ASSERT_OFFSET(turboTime, 0x400);
CARA_ASSERT_OFFSET(lowSpeedFlag, 0x404);
CARA_ASSERT_OFFSET(localAxis, 0x410);
CARA_ASSERT_OFFSET(animTimes, 0x420);
CARA_ASSERT_OFFSET(prevVelocity, 0x440);
CARA_ASSERT_OFFSET(reverseTimer, 0x450);
CARA_ASSERT_OFFSET(rollSurface, 0x454);
CARA_ASSERT_OFFSET(gradeOrReverse, 0x458);
CARA_ASSERT_OFFSET(stuckTimer, 0x45C);
CARA_ASSERT_OFFSET(airTime, 0x460);
CARA_ASSERT_OFFSET(stuckCounter, 0x464);
CARA_ASSERT_OFFSET(gripScale, 0x468);
CARA_ASSERT_OFFSET(slaveWeaponRpm, 0x46C);
CARA_ASSERT_OFFSET(gripLossTime, 0x46C);
CARA_ASSERT_OFFSET(engineRpmState, 0x470);
CARA_ASSERT_OFFSET(damageRpm, 0x478);
CARA_ASSERT_OFFSET(transmissionFlags, 0x48C);
CARA_ASSERT_OFFSET(tiltState, 0x490);
CARA_ASSERT_OFFSET(stuntAxis, 0x491);
CARA_ASSERT_OFFSET(stuntPopup, 0x494);
CARA_ASSERT_OFFSET(pickupId, 0x498);
CARA_ASSERT_OFFSET(stuntScoreMessage, 0x49C);
CARA_ASSERT_OFFSET(stuntMessage, 0x4A0);
CARA_ASSERT_OFFSET(pickupCount, 0x4A4);
CARA_ASSERT_OFFSET(stuntSpin, 0x4B0);
CARA_ASSERT_OFFSET(torquePolyCoeffs, 0x4C0);
CARA_ASSERT_OFFSET(soundRpmGate, 0x4E0);
CARA_ASSERT_OFFSET(rpm, 0x4E4);
CARA_ASSERT_OFFSET(curveTableIdx, 0x4E8);
CARA_ASSERT_OFFSET(soundIds, 0x4F0);
CARA_ASSERT_OFFSET(gripBlend, 0x4FC);
CARA_ASSERT_OFFSET(damageAccum, 0x500);
CARA_ASSERT_OFFSET(groundedTime, 0x504);
CARA_ASSERT_OFFSET(groundedScale, 0x508);
CARA_ASSERT_OFFSET(forwardAccel, 0x50C);
CARA_ASSERT_OFFSET(stuntAxisVec, 0x510);
CARA_ASSERT_OFFSET(bodyOffset, 0x520);
CARA_ASSERT_OFFSET(groundNormal, 0x530);
CARA_ASSERT_OFFSET(tiltX, 0x540);
CARA_ASSERT_OFFSET(unused548, 0x548);
CARA_ASSERT_OFFSET(turboCooldown, 0x550);
CARA_ASSERT_OFFSET(stuntTime, 0x554);
CARA_ASSERT_OFFSET(navTimer, 0x55C);
CARA_ASSERT_OFFSET(navDist, 0x564);
CARA_ASSERT_OFFSET(path, 0x568);
CARA_ASSERT_OFFSET(speedoRising, 0x584);
CARA_ASSERT_OFFSET(speedoCount, 0x588);
CARA_ASSERT_OFFSET(speedoShown, 0x58C);
_Static_assert(sizeof(CarA) == 0x5A0, "CarA size");
#undef CARA_ASSERT_OFFSET

/* The car's driver: Thing.player as a Player *. */
#define CARA_PLAYER(car) ((Player *)GETP((car)->base.player))

#endif /* CARA_H */
