/* Basic types for this decompilation (CodeWarrior conventions). Agents add
 * structs and prototypes here or in new headers next to it. */
#ifndef COMMON_H
#define COMMON_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "port.h"
#include "sdk.h"
typedef struct Vector4 {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
} Vector4;
typedef struct Quaternion {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
} Quaternion;
typedef union Matrix4 {
  Vector4 v[4];
  f64 d[8];                 /* the game copies matrices as doubles */
} Matrix4 __attribute__((aligned(16)));
#include "Thing.h"
#include "Collision.h"
#include "ItemThing.h"
#include "Creature.h"
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif


typedef struct GameInfo {
  PTR32(const char) name;
  PTR32(const char) id;
  PTR32(const char) title;
  PTR32(const char) fileName;
  PTR32(const char) icon;
  s32 unk14;
  s32 unk18;
} GameInfo;

typedef struct WorldData {
  PTR32(void) world;
  s32 unk4;
  s32 unk8[4];
} WorldData;

extern WorldData g_worldData;

typedef struct Camera {
  PTR32(struct Player) player;    /* 0x00 */
  u8 pad4[0xC];
  Matrix4 mtx;              /* 0x10 v[3] = position */
  u8 pad50[0x10];
  Vector4 pos;              /* 0x60 */
  Vector4 vel;              /* 0x70 */
  Vector4 dir;              /* 0x80 */
  Vector4 up;               /* 0x90 */
  Vector4 right;            /* 0xA0 */
  u8 padB0[4];
  PTR32(u32) mode;                /* 0xB4 */
  f32 modeTime;             /* 0xB8 */
  f32 trim;                 /* 0xBC */
  Vector4 unkC0;            /* 0xC0 */
  Vector4 unkD0;            /* 0xD0 */
  Vector4 lookDir;          /* 0xE0 */
  u8 padF0[0x10];
  s32 unk100;               /* 0x100 -1 when created */
  u8 pad104[0xC];
  Vector4 springPos;        /* 0x110 */
  Vector4 springVel;        /* 0x120 */
  Vector4 rollUp;           /* 0x130 */
  Vector4 rollUpVel;        /* 0x140 */
  Vector4 tiltUp;           /* 0x150 */
  Vector4 tiltUpVel;        /* 0x160 */
  u8 pad170[0xC];
  s32 flipTimer;            /* 0x17C */
  PTR32(Thing) cameraFx;    /* 0x180 the world's camera effect (WorldGameData.cameraFx), kept ahead of it */
  u8 pad184[0x190 - 0x184];
} Camera;

_Static_assert(sizeof(Camera) == 0x190, "Camera size");

/* The cameras, one per viewport (g_cameraManager). */
typedef struct CameraManager {
  Camera cameras[4];        /* 0x000 */
  u8 count;                 /* 0x640 */
  u8 pad641[0xF];           /* 0x641 */
} CameraManager;

_Static_assert(offsetof(CameraManager, count) == 0x640, "CameraManager.count");

extern u8 g_cameraManager[];
#define CAMERA_MANAGER ((CameraManager *)(void *)g_cameraManager)



typedef struct GXLightObjInt {
  u32 reserved[3];
  u32 Color;
  f32 a[3];
  f32 k[3];
  f32 lpos[3];
  f32 ldir[3];
} GXLightObjInt;

typedef struct ShaderHolderGX {
  PTR32(void) gx;
  PTR32(void) shader;
} ShaderHolderGX;

typedef struct ShaderDef {
  u8 pad0[4];
  u32 numColorKeys;
  u8 pad8[4];
  u32 numPointKeys;
  u8 pad10[4];
  PTR32(u32) textureKeys;
} ShaderDef;

typedef struct Shader {
  u8 pad0[0x2C];
  u8 unk2C;
  u8 pad2D[0x38 - 0x2D];
  u8 unk38;
  u8 pad39[0x58 - 0x39];
  u8 unk58;
  u8 pad59[0x5C - 0x59];
  PTR32(ShaderDef) def;
  u8 hasColorKey;
  u8 hasTextureKey;
  u8 hasPointKey;
  u8 pad63;
  u32 refCount;
  u8 pad68[4];
} Shader;

typedef struct ShaderHolder {
  PTR32(void) shaders[0x40];
  Shader slots[0x40];
  ShaderHolderGX gx[0x80];
  u32 numShaders;
  u32 numGX;
} ShaderHolder;

typedef struct DividingAxis {
  f32 dir;
  f32 minA;
  f32 maxA;
  f32 minB;
  f32 maxB;
  f32 tEnter;
  f32 tExit;
} DividingAxis;

typedef struct DVDQueue {
  PTR32(struct DVDQueue) next;
  PTR32(struct DVDQueue) prev;
} DVDQueue;

typedef struct TextureKeyAnim {
  u32 numKeys;
  PTR32(u32) keys;
  u32 frameTime;
} TextureKeyAnim;

typedef struct TextureKeyState {
  u16 texture;
  u16 key;
  f32 time;
  u8 reset;
} TextureKeyState;

typedef struct CollisionTri {
  Vector4 pos;    /* 0x00 */
  Vector4 normal; /* 0x10 */
  u32 triId1;     /* 0x20 */
  u32 triId0;     /* 0x24 */
  u32 time;       /* 0x28 */
  u8 pad2C[4];    /* 0x2C */
} CollisionTri;

/* A local axis-aligned box as stored in the data (DestructibleInfo.box):
 * v[0] min corner, v[1] max corner, v[2].x bounding radius
 * (GetBoundingBoxRadius). BuildOOB turns it into a CollisionOBox. */
typedef struct OBox {
  Vector4 v[3];
} OBox;

/* A convex hull in the data (DestructibleInfo.hull). */
typedef struct ConvexHull {
  u8 pad00[8];             /* 0x00 */
  u32 numFaces;            /* 0x08 */
  u32 numVerts;            /* 0x0C */
  u8 pad10[8];             /* 0x10 */
  PTR32(void) faces;       /* 0x18 */
  PTR32(Vector4) verts;    /* 0x1C local space */
} ConvexHull;

/* Physical properties of destructibles, physical objects and drones
 * (ThingRecord.info); cars read the same layout (CarACollider). */
typedef struct DestructibleInfo {
  f32 inertia[3];    /* 0x00 per-axis factor of the inertia tensor */
  u8 pad0C[4];       /* 0x0C */
  u8 type;           /* 0x10 collision: 0 box, 1 hull, 3 box from the geometry
                        bounds (becomes 0), 4 none, 6 hull, 7 spheres */
  u8 takesDamage;    /* 0x11 */
  u8 pad12[2];       /* 0x12 */
  s32 maxHealth;     /* 0x14 */
  s32 armour;        /* 0x18 subtracted from each hit's damage */
  s32 deathTime;     /* 0x1C ms before a destroyed one is released, <0 never */
  f32 mass;          /* 0x20 */
  f32 gravityScale;  /* 0x24 physical objects: force.y += gravityScale * mass */
  u8 pad28[4];       /* 0x28 */
  f32 angDamping;    /* 0x2C physical objects */
  PTR32(struct SurfaceMaterial) material; /* 0x30 (Destructible.h; vtable GetSurfaceScript) */
  u32 numEndDebris;  /* 0x34 effects spawned when destroyed (Thing_ProcessEndEvent) */
  PTR32(u32) endDebris;    /* 0x38 */
  u32 numDebris;     /* 0x3C effects spawned when the death timer runs out */
  PTR32(u32) debris;       /* 0x40 */
  PTR32(OBox) box;         /* 0x44 type 0, 3 */
  PTR32(ConvexHull) hull;  /* 0x48 type 1, 6 */
} DestructibleInfo;

/* A placed object's record in the data pack (destructibles, movers...). */
typedef struct ThingRecord {
  PTR32(void) geometry;   /* 0x00 noDraw 1: a motion model instead */
  u8 noDraw;              /* 0x04 0: draw geometry, 1: run it as a motion slave */
  u8 collides;            /* 0x05 */
  u8 hasDrawDist;         /* 0x06 */
  u8 isStatic;            /* 0x07 */
  f32 drawDist;           /* 0x08 */
  u8 pad0C[4];            /* 0x0C */
  PTR32(DestructibleInfo) info; /* 0x10 */
  u8 pad14[4];            /* 0x14 */
  u8 slaves[8];           /* 0x18 shared slaves (count, triggers) */
} ThingRecord;

/* A convex hull in world space, built from a ConvexHull and a transform
 * (BuildOConvexHullOffset). Pooled in g_aOConvexHull (OConvexHullHolder_*). */
typedef struct OConvexHull {
  Vector4 verts[32];        /* 0x00 */
  Vector4 planes[16];       /* 0x200 one per face */
  PTR32(ConvexHull) hull;   /* 0x300 the source; NULL: pool slot free */
  u8 pad304[0xC0];          /* 0x304 */
  s32 unk3C4;               /* 0x3C4 */
  s32 unk3C8;               /* 0x3C8 */
  u8 unk3CC;                /* 0x3CC Destructible_Init: 0 for type 1, 1 for type 6 */
  u8 pad3CD[3];             /* 0x3CD */
  Vector4 pos;              /* 0x3D0 translation of the transform */
} OConvexHull;

/* Destructibles, movers, physical objects and drones (Destructible.c,
 * PhysicalObject.c; Destructible.h has PhysicalObject). */
typedef struct Destructible {
  Thing base;                /* 0x00 */
  u8 padAC[0x14];            /* 0xAC */
  PTR32(ThingRecord) record; /* 0xC0 */
  f32 drawDist;              /* 0xC4 */
  s32 deathTimer;            /* 0xC8 ms until a dead destructible is released */
  PTR32(OConvexHull) hull;   /* 0xCC type 1, 6 */
  PTR32(CollisionOBox) orientedBox; /* 0xD0 type 0, 3 */
  PTR32(struct MoverAnim) anim; /* 0xD4 movers: track animation (Destructible.h) */
  f32 speed;                 /* 0xD8 movers: track time per second */
  f32 trackPos;              /* 0xDC movers: time on the track */
  Quaternion moverRot;       /* 0xE0 movers: orientation for Mover_DynamicRotation */
} Destructible;

_Static_assert(offsetof(Destructible, record) == 0xC0, "Destructible.record");
_Static_assert(offsetof(Destructible, trackPos) == 0xDC, "Destructible.trackPos");
_Static_assert(offsetof(Destructible, moverRot) == 0xE0, "Destructible.moverRot");
_Static_assert(sizeof(Destructible) == 0xF0, "Destructible size");
_Static_assert(offsetof(DestructibleInfo, material) == 0x30, "DestructibleInfo.material");
_Static_assert(offsetof(DestructibleInfo, hull) == 0x48, "DestructibleInfo.hull");
_Static_assert(offsetof(ThingRecord, slaves) == 0x18, "ThingRecord.slaves");
_Static_assert(offsetof(ConvexHull, verts) == 0x1C, "ConvexHull.verts");
_Static_assert(offsetof(OConvexHull, hull) == 0x300, "OConvexHull.hull");
_Static_assert(offsetof(OConvexHull, pos) == 0x3D0, "OConvexHull.pos");
_Static_assert(sizeof(OConvexHull) == 0x3E0, "OConvexHull size");

typedef struct CollisionPair {
  Vector4 pos;    /* 0x00 */
  Vector4 normal; /* 0x10 */
  u32 objectA;    /* 0x20 */
  u32 objectB;    /* 0x24 */
  s32 time;       /* 0x28 */
  u8 pad2C[4];    /* 0x2C */
} CollisionPair;

typedef struct InterfaceCallBackData {
  u32 data[17];
} InterfaceCallBackData;

typedef struct InterfacePackEntry {
  PTR32(char) name;
  u32 id;
  u32 unk8;
} InterfacePackEntry;

typedef struct PlayerLink {
  u8 unk0[0x20];
  FPTR32(void, (void)) controlReader;
  FPTR32(void, (void)) init;
  FPTR32(void, (void)) heartbeat;
  u8 unk2C[0xB0 - 0x2C];
  PTR32(struct PlayerLink) next;
  PTR32(struct PlayerLink) prev;
} PlayerLink;

typedef struct GXDataTev {
  u8 unk0[0x130];
  u32 tevc[16];
  u32 teva[16];
  u32 tevKsel[8];
} GXDataTev;

typedef struct GXTexRegionInt {
  u32 image1;
  u32 image2;
  u32 pad;
  u8 is32bMipmap;
  u8 isCached;
} GXTexRegionInt;

typedef struct FileTOCEntry {
  u32 version;
  u32 type;
  u32 id;
  u8 unkC[0x14 - 0xC];
  u32 offset;
} FileTOCEntry;

typedef struct FileTOCCacheEntry {
  FileTOCEntry entries[50];
  u32 count;
  char name[0x4C];
} FileTOCCacheEntry;

extern FileTOCCacheEntry g_fileTOCCache[11];

typedef struct MatrixCacheEntry {
  PTR32(void) mtx;
  u32 key;
} MatrixCacheEntry;

typedef struct MatrixCacheData {
  MatrixCacheEntry small[0x60];
  MatrixCacheEntry medium[0x80];
  MatrixCacheEntry large[4];
} MatrixCacheData;

typedef struct SharedSlaveHolder {
  PTR32(void) key;
  PTR32(void) slaves;
} SharedSlaveHolder;

typedef struct ThingManagerDARYL {
  u8 unk0[0x6AD10];
  u8 looker[0xFE60];
  PTR32(void) activeList;
  PTR32(void) freeList[19];
} ThingManagerDARYL;

extern ThingManagerDARYL g_thingManager_DARYL;

typedef struct CarPart {
  u8 flags;
  u8 xform;
  u8 pad2[2];
  u16 mtxIndex;
  u8 pad6[2];
  PTR32(void) geom;
  PTR32(Vector4) offset;
  PTR32(Vector4) slavePos;        /* 0x10 NULL: g_zeroVector */
  u32 debris;               /* 0x14 thing spawned when the part breaks off */
  PTR32(void) slaveInfo;          /* 0x18 */
} CarPart;
typedef struct CarPartList {
  u32 count;
  PTR32(CarPart) parts;
} CarPartList;
typedef struct CarTrailBone {
  f32 offset;
  f32 width0;
  f32 width1;
  f32 restLength;           /* 0x0C */
  f32 springRate;           /* 0x10 */
  f32 unk14;
  f32 friction;             /* 0x18 */
  f32 damping;              /* 0x1C */
  f32 maxFriction;          /* 0x20 */
  f32 handbrakeGrip;        /* 0x24 */
  u8 pad28[0x38 - 0x28];
  f32 maxDamping;           /* 0x38 */
  u8 driven;                /* 0x3C */
  u8 pad3D;
  u8 skids;                 /* 0x3E */
  u8 parent;
  u16 bone;
  u8 dusts;                 /* 0x42 */
  u8 pad43[0x50 - 0x43];
} CarTrailBone;

typedef struct CarNavPath {
  u8 pad[0x568];
  PTR32(u8) path[7];
} CarNavPath;

typedef struct ControllerSettings {
  u8 data[0x12];
} ControllerSettings;

typedef struct AudioSettings {
  u8 sfxVolume;
  u8 musicVolume;
  u8 speakerMode;
  u8 unk3;
} AudioSettings;

/* A save in the memory card file's list (GameSettings_SaveGame). */
typedef struct SaveSlotInfo {
  u8 valid;           /* 0x00 */
  char name[11];      /* 0x01 g_saveFileName */
  s32 year;           /* 0x0C GetSystemDate */
  s32 month;          /* 0x10 */
  s32 day;            /* 0x14 */
  u32 hour;           /* 0x18 */
  s32 minute;         /* 0x1C */
} SaveSlotInfo;

#define SAVE_FILE_SLOTS 8

/* Saved in every memory card file, ahead of the game data. */
typedef struct GeneralGameSettings {
  s32 difficulty;     /* 0x00 */
  AudioSettings audio;  /* 0x04 */
  ControllerSettings controllers[4];  /* 0x08 per player */
  SaveSlotInfo slots[SAVE_FILE_SLOTS]; /* 0x50 the file's list of saves, kept as on the card */
} GeneralGameSettings;

/* The memory card file (g_pSavedGameData), followed by SAVE_FILE_SLOTS blocks
 * of game data (HotWheelsGameData / DDRescueGameData) of
 * g_gameInfo[gameType].unk18 bytes each. */
#define SAVE_FILE_MAGIC 4
typedef struct SaveFile {
  u32 magic;          /* 0x00 SAVE_FILE_MAGIC */
  u32 version;        /* 0x04 g_gameInfo[gameType].unk14 */
  u32 checksum;       /* 0x08 FileManager_GetChecksum of what follows */
  GeneralGameSettings settings; /* 0x0C */
} SaveFile;
#define SAVE_FILE_SIZE(dataSize) (sizeof(SaveFile) + (dataSize) * SAVE_FILE_SLOTS)
#define SAVE_FILE_GAME_DATA(file, dataSize, slot) ((u8 *)(file) + sizeof(SaveFile) + (slot) * (dataSize))
_Static_assert(sizeof(SaveSlotInfo) == 0x20, "SaveSlotInfo size");
_Static_assert(offsetof(GeneralGameSettings, slots) == 0x50, "GeneralGameSettings.slots");
_Static_assert(sizeof(GeneralGameSettings) == 0x150, "GeneralGameSettings size");
_Static_assert(sizeof(SaveFile) == 0x15C, "SaveFile size");

typedef struct CodeVariables {
  s32 playerVehicle[4];
  s32 playerItem[4];
  s32 controllerState[4];
  s32 unk30;
  s32 unk34[4];
  s32 unk44[4];
  s32 unk54[4];
  s8 unk64[4];
  f32 unk68[30];
  s32 unkE0;
  s8 unkE4;
  s32 unkE8;
  s32 unkEC;
  s32 unkF0;
  s32 unkF4;
  s32 unkF8;
  s8 unkFC;
  s32 unk100;
  s32 unk104;
  s8 unk108;
  s8 unk109[0x111 - 0x109];
  s8 unk111;
  u8 unk112[0x114 - 0x112];
  u32 device;             /* memory card slot (1, 2); the decomp typed it char * */
  s32 fileIndex;
  s8 unk11C[3];
  s8 bOverwritingFile;
  s8 unk120[0x124 - 0x120];
  s32 unk124;
  u8 unk128[0x134 - 0x128];
} CodeVariables;

typedef struct SIControl {
  s32 chan;
  u32 poll;
  u32 inputBytes;
  PTR32(void) input;
  PTR32(void) callback;
} SIControl;

typedef struct SIPacket {
  s32 chan;
  PTR32(void) output;
  u32 outputBytes;
  PTR32(void) input;
  u32 inputBytes;
  PTR32(void) callback;
  s64 fire;
} SIPacket;

typedef struct SIPacketBss {
  SIPacket packet[4];
  u8 alarm[4][0x28];
  u8 unk120[0x140 - 0x120];
  s64 xferTime[4];
  u8 unk160[0x1B0 - 0x160];
  BOOL inputBufferValid[4];
  u32 inputBuffer[4][2];
  u32 inputBufferVcount[4];
} SIPacketBss;

/* The save data of each game type (MCP.gameType), reached through the code
 * variables (GameSettings_Set/GetCodeVariable) and written to the memory card
 * whole (g_pGameData). */

/* Game type 0: Hot Wheels Velocity X. */
typedef struct HotWheelsGameData {
  s8 cars[0x21];      /* 0x00 code variable 0x64: unlocked */
  s8 tracks[0x39];    /* 0x21 code variable 0x65: unlocked */
  u8 pad5A[0x39];     /* 0x5A */
  s8 items[0xC];      /* 0x93 code variable 0x66: unlocked */
  s8 cheats[0xB];     /* 0x9F code variable 0x6A; [9] also 0x69, [10] turns them all on */
  s8 unkAA[5];        /* 0xAA code variable 0x68 */
  u8 padAF;           /* 0xAF */
  u16 unkB0[0x1E];    /* 0xB0 code variable 0xCA */
  s32 vehicle;        /* 0xEC code variable 0xCB, player 1 */
  s32 item;           /* 0xF0 code variable 0xCC, player 1 */
} HotWheelsGameData;

/* Game type 1: DD Rescue. Besides the unlocks it keeps the state of the
 * current world between missions (GameSettings_GatherGameData /
 * GameSettings_RestoreGameData). Arrays are capped at the counts below. */
#define DDR_MAX_TIMERS 50
#define DDR_MAX_VARIABLES 50
#define DDR_MAX_CREATURES 200
#define DDR_MAX_ITEMS 1000
#define DDR_MAX_INSTANCES 2000
#define DDR_MAX_PATHS 500
#define DDR_MAX_CARS 50
typedef struct DDRescueGameData {
  s8 cars[0x14];      /* 0x0000 code variable 0x64 */
  s8 missions[0xA];   /* 0x0014 code variable 0x65 */
  s8 cheats[0xB];     /* 0x001E code variable 0x6A; [9] also 0x69, [10] turns them all on */
  s8 unk29[5];        /* 0x0029 code variable 0x68 */
  u16 unk2E[0x1E];    /* 0x002E code variable 0xCA */
  u8 pad6A[2];        /* 0x006A */
  s32 progressionID;  /* 0x006C StateProgressionManager_GetProgressionID */
  s32 progression;    /* 0x0070 */
  s8 bValid;          /* 0x0074 the world state below was saved */
  u8 pad75[3];        /* 0x0075 */
  u8 playerStats[0x38]; /* 0x0078 Player 0x70-0xA7 of the only human */
  u8 actions[500 * 4];  /* 0x00B0 Trigger_SaveActions */
  f32 timerTime[DDR_MAX_TIMERS];          /* 0x0880 WorldTimer.time */
  u8 timerMode[DDR_MAX_TIMERS][2];        /* 0x0948 WorldTimerMode */
  f32 variableValue[DDR_MAX_VARIABLES];   /* 0x09AC WorldVariable.value */
  u8 variableMode[DDR_MAX_VARIABLES][2];  /* 0x0A74 WorldVariable.mode[0..1] */
  u8 creatureMode[DDR_MAX_CREATURES];     /* 0x0AD8 TileCreature.mode */
  u8 itemMode[DDR_MAX_ITEMS];             /* 0x0BA0 QuadItem.mode, all quadrants in turn */
  u8 instanceMode[DDR_MAX_INSTANCES];     /* 0x0F88 QuadInstance.mode */
  u8 pathEnabled[DDR_MAX_PATHS];          /* 0x1758 TilePath.enabled */
  u8 carMode[DDR_MAX_CARS];               /* 0x194C TileCar.mode */
  f32 carPos[DDR_MAX_CARS][3];            /* 0x1980 */
  f32 carHealth[DDR_MAX_CARS];            /* 0x1BD8 fraction of the maximum */
  u8 carSpawned[DDR_MAX_CARS];            /* 0x1CA0 -> TileCar.unk5F */
  u8 pad1CD2[2];      /* 0x1CD2 */
  s32 vehicle;        /* 0x1CD4 code variable 0xCB, player 1 */
  s32 item;           /* 0x1CD8 code variable 0xCC, player 1 */
} DDRescueGameData;
_Static_assert(offsetof(HotWheelsGameData, item) == 0xF0, "HotWheelsGameData.item");
_Static_assert(sizeof(HotWheelsGameData) == 0xF4, "HotWheelsGameData size");
_Static_assert(offsetof(DDRescueGameData, unk2E) == 0x2E, "DDRescueGameData.unk2E");
_Static_assert(offsetof(DDRescueGameData, progressionID) == 0x6C, "DDRescueGameData.progressionID");
_Static_assert(offsetof(DDRescueGameData, playerStats) == 0x78, "DDRescueGameData.playerStats");
_Static_assert(offsetof(DDRescueGameData, timerTime) == 0x880, "DDRescueGameData.timerTime");
_Static_assert(offsetof(DDRescueGameData, itemMode) == 0xBA0, "DDRescueGameData.itemMode");
_Static_assert(offsetof(DDRescueGameData, carPos) == 0x1980, "DDRescueGameData.carPos");
_Static_assert(offsetof(DDRescueGameData, carSpawned) == 0x1CA0, "DDRescueGameData.carSpawned");
_Static_assert(offsetof(DDRescueGameData, vehicle) == 0x1CD4, "DDRescueGameData.vehicle");
_Static_assert(sizeof(DDRescueGameData) == 0x1CDC, "DDRescueGameData size");

typedef struct TRKEvent {
  u8 eventType;
  s32 eventID;
  s32 msgBufID;
} TRKEvent;

typedef struct TRKEventQueue {
  s32 mutex;
  s32 count;
  s32 next;
  TRKEvent events[2];
  u32 eventID;
} TRKEventQueue;

typedef struct GXObject {
  u8 pad0[0x1C];
  u32 count;       /* 0x1C */
  PTR32(s32) shaderList; /* 0x20 */
} GXObject;

typedef struct QuadPack {
  u8 pad00[0xC];
  u32 enabled;    /* 0x0C */
  PTR32(void) data;     /* 0x10 */
  PTR32(void) vertices; /* 0x14 */
  u8 pad18[0x10];
  union {
    PTR32(GXObject) gxObject;            /* 0x28 */
    PTR32(struct QuadPackHeader) header; /* 0x28 */
  };
  s32 gxBase;         /* 0x2C */
  u8 pad30[0x4];
  u8 flag34; /* 0x34 */
  u8 pad35[0x7];
  s32 fileOffset; /* 0x3C */
  u32 size;       /* 0x40 */
  PTR32(void) alloc;    /* 0x44 */
} QuadPack;

typedef struct DataPackShader {
  s32 shader; /* 0x00 */
  u8 pad04[0x8];
} DataPackShader;

typedef struct DataPackRef {
  char name[0xC];        /* 0x00 */
  u32 id;                /* 0x0C */
  PTR32(struct DataPack) pack; /* 0x10 */
} DataPackRef;

typedef struct DataPack {
  PTR32(u8) base;                /* 0x00 */
  u32 numRelocs;           /* 0x04 */
  PTR32(s32) relocs;             /* 0x08 */
  u32 numSoundRelocs;      /* 0x0C */
  PTR32(u32) soundRelocs;        /* 0x10 */
  PTR32(void) palettes;          /* 0x14 */
  u32 numShaders;          /* 0x18 */
  u32 numGX;               /* 0x1C */
  PTR32(u16) shaderIDs;          /* 0x20 */
  PTR32(u32) gxList;             /* 0x24 */
  u32 dataSize;            /* 0x28 */
  u32 numTextures;         /* 0x2C */
  PTR32(u16) textureIDs;         /* 0x30 */
  u32 numSounds;           /* 0x34 */
  PTR32(u16) soundIDs;           /* 0x38 */
  PTR32(void) soundData;         /* 0x3C */
  PTR32(u8) soundBase;           /* 0x40 */
  u32 textureData;         /* 0x44 */
  PTR32(u8) sfxData;             /* 0x48 */
  PTR32(void) paletteData;       /* 0x4C */
  PTR32(DataPackShader) shaders; /* 0x50 */
  u32 numTriggers;         /* 0x54 */
  PTR32(u8) triggers;            /* 0x58 */
  PTR32(void) song;              /* 0x5C */
  PTR32(void) songBank;          /* 0x60 */
  s8 songLoaded;           /* 0x64 */
  u8 pad65[3];             /* 0x65 */
  DataPackRef refs[2];     /* 0x68 */
  u32 soundDataSize;       /* 0x90 */
} DataPack;



typedef struct {
  PTR32(char) name;
  s32 id;
} ProgPack;
typedef struct {
  u8 type;
  u8 pad[3];
  PTR32(void) data;
} ProgStep;
typedef struct {
  u32 count;
  PTR32(ProgStep) steps;
  u16 advance;
} ProgSequence;
typedef struct {
  u32 count;
  PTR32(ProgSequence) seqs;
} ProgSeqList;
typedef struct {
  u32 count;
  PTR32(ProgStep) steps;
  PTR32(ProgPack) pack;
  PTR32(char) world;
  ProgSeqList win;
  ProgSeqList lose;
} ProgLevel;
typedef struct {
  s32 count;
  PTR32(ProgLevel) levels;
} ProgScript;
typedef struct {
  PTR32(ProgScript) script;
  s32 phase;
  s32 index;
  u32 step;
  s8 loaded;
  u8 pad[3];
  s32 worldState;
} ProgState;
typedef struct {
  s32 name;
  s32 script;
} PackRef;
typedef struct {
  u8 type;
  PTR32(void) movie;
  PTR32(PackRef) pack;
} Step;
/* MCP demo settings (MCP.demo). */
typedef struct MCPDemo {
  u8 pad0[4];               /* 0x00 */
  u32 time;                 /* 0x04 ms the attract demo / rounds last */
  PTR32(ProgScript) script; /* 0x08 */
} MCPDemo;

/* A controller layout (MCP.bindings): pad settings and key bindings. */
typedef struct ControlBinding {
  PTR32(char) name;         /* 0x00 */
  PTR32(u8) settings;       /* 0x04 16 bytes */
  PTR32(void) keys;         /* 0x08 KeyBinding, 16 bytes */
} ControlBinding;

/* The master control program (mcp.xgc): which game this is and what it
 * loads (Port_Swap.c Claim_MCP has the swapping). */
typedef struct MCP {
  u8 unk0;                  /* 0x00 0: load the world's item and AI packs */
  u8 gameType;              /* 0x01 index into g_gameInfo (0: Hot Wheels, 1: DD Rescue) */
  u8 pad2[2];               /* 0x02 */
  s32 menuName;             /* 0x04 */
  PTR32(char) carsPack;     /* 0x08 shared pack 2 (HW_Cars) */
  PTR32(char) itemsPack;    /* 0x0C shared pack 3 (HW_Items) */
  PTR32(char) sharePack;    /* 0x10 shared pack 1 (HW_share) */
  u32 envmapX;              /* 0x14 Gfx_SetEnvmap */
  u32 envmapY;              /* 0x18 */
  u8 pad1C[8];              /* 0x1C */
  PTR32(MCPDemo) demo;      /* 0x24 */
  s32 menuFile;             /* 0x28 */
  u32 numSteps;             /* 0x2C */
  PTR32(Step) steps;        /* 0x30 0xC each */
  u32 numScripts;           /* 0x34 progression scripts */
  PTR32(PTR32(ProgScript)) scripts; /* 0x38 */
  u32 numBindings;          /* 0x3C */
  PTR32(PTR32(PTR32(ControlBinding))) bindings; /* 0x40 table -> slot -> binding */
} MCP;

_Static_assert(offsetof(MCP, demo) == 0x24, "MCP.demo");
_Static_assert(offsetof(MCP, bindings) == 0x40, "MCP.bindings");

/* The binding in MCP.bindings[i] (each entry is a slot holding it). */
#define MCP_BINDING(mcp, i) GETP(*GETP(GETP((mcp)->bindings)[i]))
typedef struct {
  Vector4 pos;
  Vector4 quat;
  Vector4 vel;
  Vector4 angVel;
  u32 id;
  s32 unk44;
  f32 unk48;
  f32 unk4C;
} FxParams;
typedef struct SpawnPoint {
  Vector4 pos;
  f32 rotX;
  f32 rotY;
  f32 rotZ;
  u8 pad1C[0x24 - 0x1C];
  PTR32(u8) info;
  PTR32(u8) thing;
} SpawnPoint;
typedef struct CarParams {
  Vector4 pos;
  Vector4 quat;
  Vector4 vel;
  PTR32(u8) info;
  u8 pad34[0x3C - 0x34];
  PTR32(u8) player;
} CarParams;
typedef struct VehicleInterfaceData {
  u32 iface;
  u32 packID;
  u32 itemID;
  u32 packIndex;
  u32 itemPack;
  u32 selection;
  s8 active;
  u8 unk19[0x1C - 0x19];
  PTR32(u8) players[2];
  PTR32(u8) cars[2];
  u32 objects[2];
  s8 locked;
  u8 unk35[0x38 - 0x35];
} VehicleInterfaceData;
typedef struct InterfaceMsg {
  u32 iface;
  u16 type;
  u16 sub;
  u8 unk8[0x20 - 0x8];
  u32 result;
  u32 value;
  u8 unk28[0x30 - 0x28];
  u32 selection;
  u32 objects[2];
  PTR32(u32) list;
  u16 nextState;
} InterfaceMsg;
typedef struct CreateCarParams {
  Vector4 pos;
  Vector4 rot;
  Vector4 vel;
  PTR32(void) script;
  u8 unk44[0x4C - 0x44];
  PTR32(void) player;
} CreateCarParams;
typedef struct FlashDef {
  u8 unk0;
  u8 enabled;
  u8 unk2[0xC - 0x2];
  s32 duration;
  u8 unk10[0x20 - 0x10];
  PTR32(u8) colorKey;
  u8 unk24[0x2C - 0x24];
  u8 pointKey[4];
} FlashDef;

typedef struct Flash {
  Vector4 pos;
  u8 colorOut[0x34 - 0x10];
  GXColor color;
  u8 unk38[0x3C - 0x38];
  u8 active;
  u8 unk3D[0x40 - 0x3D];
  u8 pointOut[0x58 - 0x40];
  u8 visible;
  u8 unk59[0x5C - 0x59];
  PTR32(FlashDef) flash;
  s32 unk60;
  u8 unk64[0x68 - 0x64];
  u32 remaining;
  u8 unk6C[0x74 - 0x6C];
  f32 time;
  u8 unk78[0x80 - 0x78];
} Flash;

typedef struct BulletHole {
  u32 r;
  u32 g;
  u32 b;
  u32 a;
  f32 v[16];
  s32 life;
  u8 unk54[0x5C - 0x54];
  u16 texture;
  u8 unk5E[0x60 - 0x5E];
} BulletHole;

typedef struct QuadPackRef {
  char name[0xC]; /* 0x00 */
  u32 id;         /* 0x0C */
  PTR32(DataPack) pack; /* 0x10 */
} QuadPackRef;

/* The quad pack's header carries its pack references inline, at 0x2C. */
typedef struct QuadPackHeader {
  u32 valid;           /* 0x00 */
  u32 numPointers;     /* 0x04 */
  u32 numRelocs;       /* 0x08 */
  PTR32(s32) relocs;         /* 0x0C */
  u32 pad10[3];        /* 0x10 */
  u32 numShaders;      /* 0x1C */
  PTR32(s32) shaderList;     /* 0x20 */
  u32 dataSize;        /* 0x24 */
  u32 numTexRelocs;    /* 0x28 */
  QuadPackRef refs[2]; /* 0x2C */
} QuadPackHeader;

typedef struct Light {
  u8 unk0[0x10];            /* 0x00 direction (directional lights) */
  u8 r;                     /* 0x10 */
  u8 g;
  u8 b;
  u8 unk13;
  s32 index;                /* 0x14 */
  u8 type;                  /* 0x18 1: directional, 2: ambient, 3: sequenced */
  s8 subtract;              /* 0x19 */
} Light;
typedef struct Volume {
  u8 unk0[0x8];
  u32 falloff;              /* 0x08 */
  u32 type;                 /* 0x0C */
  u8 unp10[0x10];
  f32 innerDist;            /* 0x20 */
  f32 outerDist;            /* 0x24 */
  u8 unk28[4];
  PTR32(Light) light;             /* 0x2C */
} Volume;
typedef struct LightWorld {
  u32 unk0;
  u8 unk4[0x60];
  PTR32(u8) lights;
  u32 numVolumes;
  PTR32(u8) volumes;
} LightWorld;
typedef struct TracerPoint {
  f32 pos[4];
  f32 vel[4];
  f32 unk20[4];
  f32 unk30[4];
  f32 unk40;
  u8 color[4];
  PTR32(struct TracerPoint) next;
  u8 unk4C[4];
} TracerPoint;
typedef struct {
  PlayerLink base;
  u8 unkB8[0x158 - 0xB8];
} HumanPlayer;
typedef struct {
  PlayerLink base;
  u8 unkB8[0x670 - 0xB8];
} CarPlayer;
typedef struct {
  PlayerLink base;
  u8 unkB8[0x100 - 0xB8];
} CreaturePlayer;
typedef struct {
  HumanPlayer humans[4];
  CarPlayer cars[4];
  CreaturePlayer creature;
  PlayerLink null;
  PTR32(PlayerLink) active;
  PTR32(PlayerLink) free;
} PlayerMgr;
typedef struct {
  u8 unk0;
  u8 type;
} TimerDef;
typedef struct {
  u8 unk0[3];
  u8 countUp;
  u8 unk4[4];
  f32 time;
  PTR32(TimerDef) def;
} Timer;
typedef struct Quad {
  u8 pad0[0x1C];
  PTR32(void) bsp;
  u32 numNeighbors;
  PTR32(u32) neighbors;
  u8 pad28[0x34 - 0x28];
  s8 loaded;
  s8 visible;
  u8 pad36[0x50 - 0x36];
} Quad;
typedef struct QuadWorld {
  u8 pad[0x20];
  u32 numQuads;
  PTR32(Quad) quads;
} QuadWorld;
typedef struct {
  PTR32(void) pad0;
  PTR32(void) pad4;
  PTR32(void) geom;
} StaticGeom;
typedef struct {
  PTR32(u8) geom;
  u8 noDraw;
  u8 pad5;
  u8 hasDist;
  u8 visible;
  f32 dist;
  u8 padC[0x8];
  PTR32(u32) numBones;            /* 0x14 bone count, the key frames an animation extracts */
  u32 slaves[2];
} Model;
typedef struct {
  u8 pad0[0x10];
  f32 mtx[3][4];
  Vector4 pos;
  Vector4 quat;
  u8 pad60[0x14];
  u32 unk74;
  u8 pad78[0x78];
} DrawThing;

typedef struct GridResult {
        u32 data;
        u32 id;
    }GridResult;

    typedef struct FileEntry {
    s32 id;
    u8 unk4[0xC - 0x4];
    s32 size;
    u8 unk10[0x14 - 0x10];
    s32 position;
    s8 slot;
    u8 unk19[0x1C - 0x19];
} FileEntry;

typedef struct CARDFileInfo {
    s32 chan;
    s32 fileNo;
    s32 offset;
    s32 length;
    u16 iBlock;
    u16 pad;
} CARDFileInfo;

extern FileEntry g_files[4];
extern char g_appInfo[5][0x40];

extern u8 CardWorkArea[0xA000];
extern CARDFileInfo memCardfileInfo[2];
extern s32 memCardBlockSize[2];
extern s32 m_memcardstatus[2];

extern InterfaceCallBackData g_CallBackData;
extern InterfacePackEntry g_InterfacePackData[10];
extern u32 g_NumInterfacesPlaying;
extern u8 g_carAVTable[];
extern u8 g_creatureVTable[];

/* What machine drafts (m2c) use for what they can't tell yet: autopilot tries
 * them as they are. */
#ifndef M2C_DEFINES
#define M2C_DEFINES
typedef s32 M2C_UNK;
typedef s8 M2C_UNK8;
typedef s16 M2C_UNK16;
typedef s32 M2C_UNK32;
#define M2C_FIELD(expr, type_ptr, offset) (*(type_ptr)((s8 *)PORT_ADDR(expr) + (offset)))
#define M2C_BITWISE(type, expr) ((type)(expr))
#define M2C_ERROR(desc) (0)
#define M2C_CARRY 0
#define M2C_OVERFLOW(a) (0)
#endif

/* Forward typedefs for types whose bodies live in other headers or GETP(in .c)
 * files, so game_protos.h can name them. C11 allows the later full typedef. */
typedef struct CarA CarA;
typedef struct Player Player;
typedef struct CameraInfo CameraInfo;
typedef struct WildCardRack WildCardRack;
typedef struct GridNode GridNode;

#include "game_protos.h"

#endif
