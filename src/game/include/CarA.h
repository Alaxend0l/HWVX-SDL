/* Layout of the CarA vehicle object (the Thing-derived class behind every
 * driveable car), reconstructed from the raw-offset accesses in src/CarA.c,
 * src/CarA_Suspension.c and src/AI_CarA.c (vehicle side, reached through
 * *(u8 **)(aiCar + 0x2C)).  sizeof(CarA) is exactly 0x560; the CarNavPath
 * hack in CarA_Release shows path[] lives at 0x568, i.e. outside this block. */
#ifndef CARA_H
#define CARA_H

#include "common.h"

/* The MotionState base subobject, embedded in CarA at 0x330: CarA_SlaveWeaponStop
 * passes both the subobject itself and its slaves list head, so the two really
 * are one and the same object. */
typedef struct MotionState {
  PTR32(Model) model;            /* 0x00 */
  u8 pad[0xC];             /* 0x04 */
  PTR32(Matrix4) animMatrices;   /* 0x10 */
  PTR32(Thing) slaves;           /* 0x14 attached slave things (Thing.next list) */
} MotionState;

/* The player object a car is driven by (GETP(CarA.player)): its controller state
 * sits at the start, read by CarA_PreMove_Control_HW. */
typedef struct Player {
  s8 stickX;                /* 0x00 */
  s8 stickY;                /* 0x01 */
  u8 pad02[0xA];            /* 0x02 */
  u32 buttonsHeld;          /* 0x0C & 4: turbo button */
  u8 button10;              /* 0x10 */
  u8 pad11;                 /* 0x11 */
  u8 throttle;              /* 0x12 */
  u8 brake;                 /* 0x13 */
  u8 button14;              /* 0x14 */
  u8 button15;              /* 0x15 */
  u8 pad16[4];              /* 0x16 */
  u8 button1A;              /* 0x1A */
  u8 pad1B[0x11];           /* 0x1B */
  PTR32(void) vehicle;            /* 0x2C the Thing the player drives */
  u8 pad30;                 /* 0x30 */
  u8 cameraIndex;           /* 0x31 passed to the vehicle's getCameraInfo */
  u8 combo;                 /* 0x32 pending combo/cheat action, run by CarA_ExecuteCombo */
  u8 pad33[9];              /* 0x33 */
  PTR32(void) aiScripts;          /* 0x3C AI players: the tile car's script table */
  u8 pad40[0x20];           /* 0x40 */
  u32 port;                 /* 0x60 human players: controller port, 1-based */
  u32 view;                 /* 0x64 viewport, 1-based */
  u8 pad68[4];              /* 0x68 */
  u32 flags;                /* 0x6C & 0x80 cleared every camera update */
  u8 pad70;                 /* 0x70 */
  u8 stuntScored;           /* 0x71 set when a landed stunt scores */
  u8 pad72[6];              /* 0x72 */
  s32 kills;                /* 0x78 */
  u32 stuntPoints;          /* 0x7C capped at 99999 */
  u8 pad80[0x28];           /* 0x80 */
  PTR32(void) tileCar;            /* 0xA8 the world tile car it was spawned from */
  s16 deathNavPoint;        /* 0xAC */
  s16 respawnNavPoint;      /* 0xAE */
  u8 padB0[0x18];           /* 0xB0 */
  s32 wheelChannel;         /* 0xC8 force-feedback wheel channel */
  u8 padCC[4];              /* 0xCC */
  PTR32(f32) healthScale;         /* 0xD0 AI players: divides the health-bar fraction */
  u8 padD4[0x40];           /* 0xD4 */
  PTR32(char) stuntScoreText;     /* 0x114 */
} Player;

struct CarA;
struct CameraInfo;

/* The Thing virtual table (Thing + 0x8C). */
typedef struct ThingVTable {
  u8 pad00[0x1C];
  FPTR32(void, (struct CarA *thing, u8 index, struct CameraInfo *info)) getCameraInfo; /* 0x1C */
  u8 pad20[8];
  FPTR32(void *, (void *thing)) getSurface; /* 0x28 */
  u8 pad2C[0x10];
  FPTR32(void, (struct CarA *thing, Vector4 *out)) getTargetPos; /* 0x3C */
} ThingVTable;

typedef struct CarAEngine {
  f32 bodyOffset[3];        /* 0x00 rest value of CarA.bodyOffset */
  u8 pad0C[4];              /* 0x0C */
  s32 selfRightDelay;       /* 0x10 ms airborne before the self-righting torque */
  f32 unk14;                /* 0x14 */
  u32 unk18;                /* 0x18 */
  u8 pad1C[4];              /* 0x1C */
  f32 selfRightArm;         /* 0x20 */
  u8 pad24[0x10];           /* 0x24 */
  f32 animSpeed;            /* 0x34 scales the steering animation time */
  u8 pad38[4];              /* 0x38 */
  f32 maxAngularVel;        /* 0x3C */
  f32 itemDropImpact;       /* 0x40 */
  f32 unk44;                /* 0x44 */
  u8 pad48[0x10];           /* 0x48 */
  f32 unk58;                /* 0x58 force point scale by up.y */
  u8 pad5C[8];              /* 0x5C */
  f32 unk64;                /* 0x64 */
  f32 unk68;                /* 0x68 */
  f32 unk6C;                /* 0x6C */
  f32 itemHeight;           /* 0x70 picked-up items ride this high above the rack */
} CarAEngine;

typedef struct CarAMaterial {
  u8 pad00[0x18];           /* 0x00 */
  f32 restitution;          /* 0x18 bounce of impulses (CarA_ProcessImpulse) */
} CarAMaterial;

typedef struct CarACollider {
  f32 inertiaScale[3];      /* 0x00 per-axis factor of the inverse inertia tensor */
  u8 pad0C[4];              /* 0x0C */
  u8 type;                  /* 0x10 0: oriented box, 1: convex hull */
  u8 pad11[3];              /* 0x11 */
  s32 maxHealth;            /* 0x14 */
  s32 damageThreshold;      /* 0x18 impacts above it hurt */
  u8 pad1C[4];              /* 0x1C */
  f32 mass;                 /* 0x20 */
  f32 gravityScale;         /* 0x24 forceAccum.y += gravityScale * mass */
  u8 pad28[4];              /* 0x28 */
  f32 gravity;              /* 0x2C */
  PTR32(CarAMaterial) material;   /* 0x30 */
  u8 pad34[0x10];           /* 0x34 */
  PTR32(Vector4) box;             /* 0x44 type 0: min/max corners */
  PTR32(struct CarAHull) hull;    /* 0x48 type 1 */
} CarACollider;

typedef struct CarAHull {
  u8 pad00[0xC];            /* 0x00 */
  u32 numVerts;             /* 0x0C */
  u8 pad10[0xC];            /* 0x10 */
  PTR32(Vector4) verts;           /* 0x1C */
} CarAHull;


typedef struct CarAAccel {
  u8 pad00[4];              /* 0x00 */
  f32 unk4;                 /* 0x04 */
  u8 pad08[0xC];            /* 0x08 */
  f32 unk14;                /* 0x14 */
  f32 unk18;                /* 0x18 */
  f32 unk1C;                /* 0x1C */
  f32 unk20;                /* 0x20 */
  f32 airDownforceBase;     /* 0x24 */
  f32 airDownforceRate;     /* 0x28 */
  f32 airDownforceMax;      /* 0x2C */
  s32 airDownforceDelay;    /* 0x30 ms */
  f32 spinImpulse;          /* 0x34 */
  f32 spinDecay;            /* 0x38 */
} CarAAccel;

typedef struct CarAStuntInfo {
  f32 pitchRate;            /* 0x00 */
  f32 rollRate;             /* 0x04 */
  f32 spinRate;             /* 0x08 */
  f32 maxTime;              /* 0x0C */
  u8 pad10[4];              /* 0x10 */
  f32 turboPerPoint;        /* 0x14 */
  f32 pitchScore;           /* 0x18 */
  f32 rollScore;            /* 0x1C */
  u8 pad20[8];              /* 0x20 */
  s32 unk28;                /* 0x28 */
  f32 minFailTime;          /* 0x2C also the stunt-time range landed stunts score in */
  f32 maxFailTime;          /* 0x30 */
  u32 numRewardThings;      /* 0x34 */
  PTR32(u32) rewardThings;        /* 0x38 picked by score */
  u32 numMessages;          /* 0x3C */
  PTR32(PTR32(PTR32(char))) messages; /* 0x40 picked by score: string tables */
  u8 pad44[4];              /* 0x44 */
  PTR32(PTR32(char)) failMessage;       /* 0x48 NULL: g_stuntLossString */
  u32 numFailThings;        /* 0x4C */
  PTR32(u32) failThings;          /* 0x50 thing ids, picked by fail time */
  f32 spinScore;            /* 0x54 */
} CarAStuntInfo;

typedef struct CarAGear {
  f32 ratio;                /* 0x00 */
  f32 unk4;                 /* 0x04 */
} CarAGear;

/* The car's config/vehicle-interface container (GETP(CarA.vehicleIface)). */
typedef struct CarASoundSet {
  u8 pad00[0xC];            /* 0x00 */
  f32 pitchScale;           /* 0x0C */
} CarASoundSet;

/* The car's model data (GETP(GETP(CarAInfo.modelRef)->data)): its breakable parts. */
typedef struct CarAModelData {
  u8 pad00[0x38];           /* 0x00 */
  PTR32(CarPartList) parts;       /* 0x38 */
} CarAModelData;

typedef struct CarAModelRef {
  u8 pad00[0x10];           /* 0x00 */
  PTR32(CarAModelData) data;      /* 0x10 */
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
  u8 pad00[0x10];           /* 0x00 */
  u8 damagePercent;         /* 0x10 health % below which parts break off */
  u8 driveMode;             /* 0x11 1: human-controlled */
  u8 pad12[2];              /* 0x12 */
  u16 skidTexture;          /* 0x14 */
  u8 pad16[2];              /* 0x16 */
  s32 damageLevel;          /* 0x18 hits above it break parts off */
  f32 unk1C;                /* 0x1C */
  f32 unk20;                /* 0x20 */
  f32 unk24;                /* 0x24 */
  u8 pad28[8];              /* 0x28 */
  f32 unk30;                /* 0x30 */
  u8 pad34[0x14];           /* 0x34 */
  PTR32(void) steerAnim;          /* 0x48 */
  u8 pad4C[4];              /* 0x4C */
  u32 skidsEnabled;         /* 0x50 */
  u8 pad54[0x18];           /* 0x54 */
  PTR32(GXColor) skidColor;       /* 0x6C */
  u8 pad70[8];              /* 0x70 */
  PTR32(CarAModelRef) modelRef;   /* 0x78 */
  u8 pad7C[4];              /* 0x7C */
  PTR32(CarAEngine) engine;       /* 0x80 */
  u8 pad84[4];              /* 0x84 */
  PTR32(CarAAccel) accel;         /* 0x88 */
  PTR32(CarAStuntInfo) stunt;     /* 0x8C */
  u8 pad90[0x10];           /* 0x90 */
  PTR32(CarACollider) collider;   /* 0xA0 */
  PTR32(struct CarAForcePoints) forcePoints; /* 0xA4 extra force points (CarA_ComputeForces) */
  u8 padA8[8];              /* 0xA8 */
  u32 explosionThing;       /* 0xB0 */
  u8 padB4[0x1C];           /* 0xB4 */
  u32 numWheels;            /* 0xD0 */
  PTR32(CarTrailBone) wheels;     /* 0xD4 */
  u32 numGears;             /* 0xD8 */
  PTR32(CarAGear) gears;          /* 0xDC torque curve table */
  u32 numSlaves;            /* 0xE0 */
  PTR32(u8) slaves;               /* 0xE4 */
  u32 numSlaveWeapons;      /* 0xE8 */
  PTR32(u8) slaveWeapons;         /* 0xEC 12-byte entries, slave list at +4 */
  u32 numPickupSlaves;      /* 0xF0 */
  PTR32(u8) pickupSlaves;         /* 0xF4 12-byte entries, slave list at +4, indexed by item value */
  u8 padF8[0x18];           /* 0xF8 */
  u32 numShiftSounds;       /* 0x110 */
  PTR32(u32) shiftSounds;         /* 0x114 */
  u32 numSoundSets;         /* 0x118 */
  PTR32(PTR32(CarASoundSet)) soundSets; /* 0x11C */
} CarAInfo;

/* Per-wheel runtime state, 8 slots in CarA from 0x1B0. */
typedef struct CarAWheelState {
  f32 rotation;             /* 0x00 advanced by speed / radius every PreMove */
  s16 dustTimer;            /* 0x04 ms */
  s16 sparkTimer;           /* 0x06 ms */
  s32 skidTrail;            /* 0x08 Trail_AddSegment handle, -1: no trail */
  f32 compression;          /* 0x0C */
  f32 lastCompression;      /* 0x10 */
  GXColor skidColor;        /* 0x14 */
  u32 skidTexture;          /* 0x18 */
  u8 pad1C[4];              /* 0x1C */
  Vector4 slipVel;          /* 0x20 */
} CarAWheelState;

typedef struct CarA {
  Vector4 velocity;         /* 0x00 carVel = *(Vector4 *)car; read as u32 at 0x8 (scale) */
  Matrix4 worldMatrix;      /* 0x10 mtx = *(Matrix4 *)(car + 0x10);
                               v[1] = up normal  @ 0x20 (impact dot product),
                               v[2] = forward   @ 0x30 (read as Quaternion by AI cars),
                               v[3] = position  @ 0x40 */
  Vector4 animRefPos;       /* 0x50 fed to TEST_AnimTraverseHeirarchyToMatricesWorld */
  u8 pad60[0x10];           /* 0x60 */
  f32 unk70;                /* 0x70 camera zoom */
  u32 collDirtyFlags;       /* 0x74 &= ~0x180 when the oriented box is rebuilt */
  s32 speed;                /* 0x78 scaled by AI speedScale */
  u8 pad7C[0x10];           /* 0x7C */
  PTR32(ThingVTable) vtable;      /* 0x8C */
  PTR32(void) tileData;           /* 0x90 the world tile (car, path) it was created from */
  PTR32(Player) player;           /* 0x94 NULL-checked, copied to slave players */
  u32 lightIndex;           /* 0x98 index into g_ThingLightingData, -1: none */
  u8 pad9C[0x20];           /* 0x9C */
  PTR32(struct CarA) unkBC;       /* 0xBC a looker's target */
  OBox orientedBox;         /* 0xC0 BuildOOBOffset writes it; GetOrientedCollision
                               returns car + 0xC0; GetTargetPos reads its v[0] */
  u8 padF0[0xC0];           /* 0xF0 */
  CarAWheelState wheelStates[8]; /* 0x1B0 one per CarAInfo.wheels entry */
  MotionState motionState;   /* 0x330 embedded base subobject: model @0x330,
                               animMatrices @0x340, slave players list @0x344 */
  u8 pad348[0x28];          /* 0x348 */
  PTR32(void) hull;               /* 0x370 OConvexHullHolder: Released + NULLed in CarA_Release */
  u32 partVisMask;          /* 0x374 bit per car part in the reflection loop */
  f32 unk378;               /* 0x378 */
  u8 pad37C[4];             /* 0x37C */
  Vector4 angularMomentum;  /* 0x380 += torque * dt in PreMove */
  Vector4 forceAccum;       /* 0x390 += force in CarA_ProcessForce */
  Vector4 torqueAccum;      /* 0x3A0 += torque in CarA_ProcessForce */
  s32 navPointIndex;        /* 0x3B0 stored as s16 pairs into the player */
  s32 navUpdateTimer;       /* 0x3B4 ms until the nearest nav point is searched again */
  u32 numWheelsGrounded;    /* 0x3B8 */
  PTR32(CarAInfo) vehicleIface;   /* 0x3BC config/vehicle-interface container: read as u32
                               by CarA_Invalidate, dereferenced for +0x11 (AI drive
                               mode), +0x54 (reflection geom), +0xA0 (collider),
                               +0xD4 (CarTrailBone), +0xDC (torque curve table) */
  Matrix4 inverseBodyInertia; /* 0x3C0 = g_identity, diagonal set at 0x3C0/0x3D4/0x3E8;
                                 read through (f64 *) casts in the decomp only */
  s32 turboTime;            /* 0x400 ms of turbo left */
  s32 lowSpeedFlag;         /* 0x404 compared < 500 by AI cars */
  u8 pad408[8];             /* 0x408 */
  Vector4 localAxis;        /* 0x410 cross-product operand in CarA_ProcessForce */
  f32 animTimes[3];         /* 0x420 times of vehicleIface->modelRef->anims */
  u8 pad42C[0x14];          /* 0x42C */
  Vector4 prevVelocity;     /* 0x440 velocity at the start of PreMove */
  u32 reverseTimer;         /* 0x450 ms the brake is held at standstill */
  PTR32(void) rollSurface;        /* 0x454 surface whose rolling sound plays */
  f32 gradeOrReverse;       /* 0x458 < 0.0f gates AI braking */
  s32 unk45C;               /* 0x45C */
  s32 unk460;               /* 0x460 */
  s32 stuckCounter;         /* 0x464 > 600 marks the AI car stuck */
  f32 gripScale;            /* 0x468 *= AI gripScale */
  s32 slaveWeaponRpm[1];      /* 0x46C indexed by weapon slot (0..2) in CarA_SlaveWeaponStop,
                                which writes 10001, just past the 10000 rpm test on the word
                                that follows; only slot 0 is sized here */
  s32 engineRpmState;       /* 0x470 compared < 10000 in GetTorqueAtWheels */
  u8 pad474[4];             /* 0x474 */
  s32 damageRpm;            /* 0x478 compared < 10000 before damage applies */
  u8 pad47C[0x10];          /* 0x47C */
  u32 transmissionFlags;    /* 0x48C & 0x1000 selects 4WD torque split */
  u8 tiltState;             /* 0x490 0: rest, 1/2: tilted by the stick, 3 */
  u8 stuntAxis;             /* 0x491 0: none, 1: pitch (stickX), 2: roll (stickY) */
  u8 pad492[2];             /* 0x492 */
  PTR32(PTR32(char)) stuntPopup;        /* 0x494 */
  u32 pickupId;             /* 0x498 last picked-up item's id */
  PTR32(PTR32(char)) stuntScoreMessage; /* 0x49C */
  PTR32(PTR32(char)) stuntMessage;      /* 0x4A0 */
  s32 pickupCount;          /* 0x4A4 type 9 items picked up */
  u8 pad4A8[8];             /* 0x4A8 */
  Vector4 stuntSpin;        /* 0x4B0 zeroed when a stunt fails */
  f32 torquePolyCoeffs[4];  /* 0x4C0 Horner polynomial in rpm */
  u8 pad4D0[0x10];          /* 0x4D0 */
  f32 unk4E0;               /* 0x4E0 */
  f32 rpm;                  /* 0x4E4 */
  u8 curveTableIdx;         /* 0x4E8 index into the interface's curve table */
  u8 pad4E9[7];             /* 0x4E9 */
  s32 soundIds[3];          /* 0x4F0 SoundSource ids, -1 == none; stopped + reset in Release */
  f32 unk4FC;               /* 0x4FC */
  s32 damageAccum;          /* 0x500 */
  s32 groundedTime;         /* 0x504 ms */
  f32 groundedScale;        /* 0x508 */
  f32 forwardAccel;         /* 0x50C velocity += forward * this * dt */
  Vector4 stuntAxisVec;     /* 0x510 world axis the stunt spins around */
  Vector4 bodyOffset;       /* 0x520 XformVec(worldMatrix, this) -> camera/reflection pos */
  Vector4 groundNormal;     /* 0x530 */
  f32 tiltX;                /* 0x540 */
  f32 tiltY;                /* 0x544 */
  u8 pad548[8];             /* 0x548 */
  s32 turboCooldown;        /* 0x550 ms */
  f32 stuntTime;            /* 0x554 */
  u8 pad558[8];             /* 0x558 */
} CarA;

/* Compile-time proof that sizeof(CarA) == 0x560 exactly: the first typedef is
   invalid if the struct is too big, the second if it is too small. */
PORT_ASSERT_SIZE32(sizeof(CarA) == 0x560, CarA_size_check);

#endif /* CARA_H */
