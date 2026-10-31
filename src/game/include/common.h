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

typedef struct CameraListener {
  u8 pad0[0x40];
  Vector4 pos;              /* 0x40 */
} CameraListener;

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
  u8 padC0[0x20];
  Vector4 lookDir;          /* 0xE0 */
  u8 padF0[0x20];
  Vector4 springPos;        /* 0x110 */
  Vector4 springVel;        /* 0x120 */
  Vector4 rollUp;           /* 0x130 */
  Vector4 rollUpVel;        /* 0x140 */
  Vector4 tiltUp;           /* 0x150 */
  Vector4 tiltUpVel;        /* 0x160 */
  u8 pad170[0xC];
  s32 flipTimer;            /* 0x17C */
  PTR32(CameraListener) listener; /* 0x180 */
  u8 pad184[0x190 - 0x184];
} Camera;



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

typedef struct OBox {
  Vector4 v[3];
} OBox;

typedef struct ConvexHull {
  u8 pad0[0x3D0];
  Vector4 point; /* 0x3D0 */
} ConvexHull;

typedef struct DestructibleInfo {
  f32 inertia[3];    /* 0x00 */
  u8 pad0C[4];       /* 0x0C */
  u8 type;           /* 0x10 */
  u8 takesDamage;    /* 0x11 */
  u8 pad12[2];       /* 0x12 */
  s32 maxHealth;     /* 0x14 */
  s32 armour;        /* 0x18 */
  s32 deathTime;     /* 0x1C ms before a destroyed one is released, <0 never */
  f32 mass;          /* 0x20 */
  u8 pad24[0x0C];    /* 0x24 */
  s32 surfaceScript; /* 0x30 */
  u8 pad34[8];       /* 0x34 */
  u32 numDebris;     /* 0x3C */
  PTR32(u32) debris;       /* 0x40 thing ids spawned when destroyed */
  PTR32(OBox) box;         /* 0x44 */
  PTR32(ConvexHull) hull;  /* 0x48 */
} DestructibleInfo;

typedef struct ThingRecord {
  PTR32(void) geometry;         /* 0x00 */
  u8 noDraw;              /* 0x04 */
  u8 collides;            /* 0x05 */
  u8 hasDrawDist;         /* 0x06 */
  u8 pad07;               /* 0x07 */
  f32 drawDist;           /* 0x08 */
  u8 pad0C[4];            /* 0x0C */
  PTR32(DestructibleInfo) info; /* 0x10 */
  u8 pad14[4];            /* 0x14 */
  u8 slaves[4];           /* 0x18 shared slaves */
} ThingRecord;

/* Destructibles, movers, physical objects and drones (Destructible.c). */
typedef struct Destructible {
  Thing base;                /* 0x00 */
  u8 padAC[0x14];            /* 0xAC */
  PTR32(ThingRecord) record; /* 0xC0 */
  f32 drawDist;              /* 0xC4 */
  s32 deathTimer;            /* 0xC8 ms until a dead destructible is released */
  PTR32(ConvexHull) hull;    /* 0xCC */
  PTR32(CollisionOBox) orientedBox; /* 0xD0 */
  PTR32(void) anim;          /* 0xD4 movers: track animation */
  f32 speed;                 /* 0xD8 */
  f32 trackPos;              /* 0xDC */
} Destructible;

_Static_assert(offsetof(Destructible, record) == 0xC0, "Destructible.record");
_Static_assert(offsetof(Destructible, trackPos) == 0xDC, "Destructible.trackPos");

typedef struct OConvexHull {
  Vector4 verts[32];        /* 0x00 */
  Vector4 planes[16];       /* 0x200 */
  PTR32(void) hull;               /* 0x300 */
  u8 pad304[0x3E0 - 0x304]; /* 0x304 */
} OConvexHull;

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

typedef struct GeneralGameSettings {
  s32 difficulty;
  AudioSettings audio;
  ControllerSettings controllers[18];
  u8 unk14C[4];
} GeneralGameSettings;

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

typedef struct HotWheelsGameData {
  s8 cars[0x21];      /* 0x00 code variable 0x64 */
  s8 tracks[0x39];    /* 0x21 code variable 0x65 */
  u8 pad5A[0x39];     /* 0x5A */
  s8 unk93[0xC];      /* 0x93 code variable 0x66 */
  s8 unk9F[0xA];      /* 0x9F code variable 0x6A */
  s8 unkA9;           /* 0xA9 */
  s8 unkAA[5];        /* 0xAA code variable 0x68 */
  u8 padAF;           /* 0xAF */
  u16 unkB0[0x1E];    /* 0xB0 code variable 0xCA */
  s32 unkEC;          /* 0xEC */
  u8 padF0[4];        /* 0xF0 */
} HotWheelsGameData;

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
typedef struct GRTrigger {
  u8 unk0;
  u8 unk1;
  u8 type;
  u8 unk3;
  s32 unk4;
  f32 time;
} GRTrigger;

typedef struct GRElem {
  u8 unk0[0x28];
  u32 unk28;
  u32 unk2C;
  u32 unk30;
  u32 unk34;
  u8 unk38;
  u8 unk39;
  u8 unk3A[6];
} GRElem;

typedef struct GRElemB {
  u8 unk0[0x30];
  u32 unk30;
  u8 unk34[2];
  u8 unk36;
  u8 unk37[9];
} GRElemB;

typedef struct GRSub {
  u32 unk0;
  u32 numB;
  PTR32(GRElemB) b;
  u32 numA;
  PTR32(GRElem) a;
  u32 numC;
  PTR32(GRElem) c;
} GRSub;
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
typedef struct {
  u8 pad0[4];
  s32 menuName;
  u8 pad8[0x20];
  s32 menuFile;
  u32 numSteps;
  PTR32(Step) steps;
} MCP;
typedef struct PodPart {
  u8 flags;
  u8 xform;
  u8 pad2[2];
  u16 mtxIndex;
  u8 pad6[2];
  PTR32(void) geom;
  PTR32(Vector4) offset;
  u8 pad10[0xC];
} PodPart;
typedef struct PodPartList {
  u32 count;
  PTR32(PodPart) parts;
} PodPartList;
typedef struct GRZone {
  u8 unk0[0x30];
  PTR32(GRSub) sub;
  u8 unk34[0x1C];
} GRZone;
typedef struct StuntParams {
  f32 pitchRate;
  f32 rollRate;
  f32 spinRate;
  f32 maxTime;
  u8 pad10[8];
  f32 pitchScore;
  f32 rollScore;
  u8 pad20[8];
  s32 minTime;
  f32 scoreMax;
  u8 pad30[0x24];
  f32 spinScore;
} StuntParams;
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
typedef struct GRWorld {
  u8 unk0[0x20];
  u32 numZones;
  PTR32(GRZone) zones;
  u8 unk28[0x28];
  u32 numTriggers;
  PTR32(PTR32(GRTrigger)) triggers;
} GRWorld;
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
typedef struct SpawnVolume {
  PTR32(u8) volume;
  u32 unk4;
  u32 index;
  u32 counts[32];
} SpawnVolume;
typedef struct Vec {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
} Vec;
typedef struct NearestCar {
  PTR32(u8) car;
  u8 pad4[0x10 - 0x4];
  Vec pos;
  f32 speed;
  f32 range;
  u8 pad28[0x30 - 0x28];
} NearestCar;
typedef struct {
  s32 unk0;
  u8 pad4[0xD - 0x4];
  u8 unkD;
  u8 padE[0x10 - 0xE];
} AIPlayerDesc;
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
typedef struct {
  u8 unk0[0x50];
  u32 numTimers;
  PTR32(PTR32(Timer)) timers;
} TimerWorld;
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
typedef struct WildCard {
  u8 pad0[0x1E];
  u8 unk1E;
  u8 pad1F[0x24 - 0x1F];
  PTR32(f32) unk24;
  PTR32(f32) unk28;
} WildCard;
typedef struct WildCardData {
  PTR32(WildCard) card;
  u8 pad4[0x28 - 0x4];
  s32 max;
} WildCardData;
typedef struct WildcardData {
  u8 unk0[0x24];
  u32 numFrames;
  s32 unk28;
  u8 spinType;
  u8 spinLimit;
  u16 spinBone;
} WildcardData;
typedef struct WildCardItem {
  u8 pad0[0x30];
  PTR32(WildCardData) data;
} WildCardItem;
typedef struct WildcardInfo {
  u8 unk0[0x18];
  f32 unk18;
} WildcardInfo;
typedef struct WildcardAnim {
  u8 unk0[0x1E];
  u8 loop;
  u8 unk1F[0x24 - 0x1F];
  PTR32(f32) times;
} WildcardAnim;
typedef struct WildcardThing {
  u8 unk0[0x30];
  PTR32(WildcardData) data;
} WildcardThing;
typedef struct WildcardSlot {
  PTR32(WildcardInfo) info;
  u8 unk4[0x10 - 0x4];
  PTR32(Matrix4) matrices;
  u8 unk14[0x18 - 0x14];
  PTR32(WildcardAnim) anim;
  u8 mode;
  u8 state;
  u8 unk1E[0x20 - 0x1E];
  f32 time;
  u8 unk24[0x40 - 0x24];
  s16 count;
  u8 unk42[0x44 - 0x42];
  f32 scale;
  f32 angle;
  PTR32(WildcardThing) thing;
} WildcardSlot;
typedef struct WildCardSlot {
  PTR32(void) card;
  u8 pad4[0x44 - 0x4];
  f32 unk44;
  u8 pad48[0x4C - 0x48];
  PTR32(void) unk4C;
} WildCardSlot;
typedef struct WildCardRack {
  u8 pad0[0x74];
  u32 flags;
  u8 pad78[0xB0 - 0x78];
  WildCardSlot slots[12];
  u8 unk470;
} WildCardRack;
typedef struct {
  PTR32(void) motion;
  u8 pad4[0x40];
  f32 scale;
  u8 pad48[0x8];
} RackState;
typedef struct {
  u8 pad0[0xB0];
  RackState states[1];
} Rack;
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
  Vector4 pos;
  Vector4 quat;
  u8 pad20[4];
  PTR32(Model) model;
  f32 dist;
  u8 pad2C[8];
  u8 useDist;
  u8 pad35[7];
  f32 radius;
} Instance;
typedef struct {
  u32 pad0;
  u32 count;
  PTR32(Instance) instances;
} InstanceList;
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
typedef struct GridNode GridNode;

#include "game_protos.h"

#endif
