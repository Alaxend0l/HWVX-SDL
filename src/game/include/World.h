/* The world: the root object of a world data pack (pack type 4), reached
 * through g_worldData.world (WORLD()), and the records it points to: placed
 * cars and creatures ("tiles"), streaming quadrants with their objects,
 * teleporters, script variables and timers, volumes and the game data block.
 *
 * All of it is pack data, byte-swapped on load by Port_Swap.c (Claim_World),
 * so field widths are fixed. Offsets are the GameCube layout; the asserts
 * keep them. Pointer fields are PTR32 handles (README, "64-bit hosts").
 * Include this from .c files that need it (not from common.h). */
#ifndef WORLD_H
#define WORLD_H

#include <stddef.h>

#define WORLD_ASSERT(type, f, off) _Static_assert(offsetof(type, f) == (off), #type "." #f)

/* ---- placed objects ------------------------------------------------------ */

/* A car placed in the world (World_GetTileCar): spawned at level start or by
 * ActionVehicleChangeMode. Trigger parameter lists refer to cars by their
 * tile, so `car` is what "vehicle" triggers test. */
typedef struct TileCar {
    Vector4 pos;                /* 0x00 */
    f32 rotX;                   /* 0x10 degrees, for SetOrientation */
    f32 rotY;                   /* 0x14 */
    f32 rotZ;                   /* 0x18 */
    u32 unused1C;               /* 0x1C never read; 0xCDCDCDCD in every pack (uninitialised) */
    u32 editorId;               /* 0x20 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    union {
        PTR32(struct CarAInfo) info;          /* 0x24 the vehicle; AI scripts per difficulty at 0x90 */
        PTR32(struct AIScriptSet) aiScripts;  /*      the same, as the AI reads it */
    };
    PTR32(struct CarA) car;     /* 0x28 the spawned car, NULL if none */
    u32 mode;                   /* 0x2C last ActionVehicleChangeMode mode @xdl: enum=TacToken */
    u32 vehicle;                /* 0x30 */
    u8 dynamic;                 /* 0x34 nonzero: not spawned by mode 1/10; AIPlayerParams.dynamic */
    u8 port;                    /* 0x35 1-4: human player slot; AI cars spawn only when > 4 */
    u8 pad36;                   /* 0x36 */
    u8 maxVehicle;              /* 0x37 AI cars pick a vehicle in 0..maxVehicle */
    f32 nearDist;               /* 0x38 AI ranges */
    f32 farDist;                /* 0x3C */
    u32 scripts[4];             /* 0x40 AI script per difficulty, 0: the vehicle's @xdl: link=AICarScript */
    PTR32(ItemRecord) item2;    /* 0x50 a second item it starts with (LoadWorldProcess) */
    PTR32(ItemRecord) item;     /* 0x54 wildcard the car starts with, NULL: none */
    f32 health;                 /* 0x58 fraction of the maximum, kept across DD Rescue missions
                                 * (GameSettings_RestoreGameData) */
    s8 respawn;                 /* 0x5C freed (car = NULL) when its AI car is removed */
    u8 item2ID;                 /* 0x5D item pack ids of item2 and item, 0xFF: none */
    u8 itemID;                  /* 0x5E */
    u8 spawned;                 /* 0x5F nonzero: always spawned at round start; DD Rescue sets it
                                 * for cars that were spawned when the mission was left */
} TileCar;

WORLD_ASSERT(TileCar, info, 0x24);
WORLD_ASSERT(TileCar, car, 0x28);
WORLD_ASSERT(TileCar, dynamic, 0x34);
WORLD_ASSERT(TileCar, nearDist, 0x38);
WORLD_ASSERT(TileCar, scripts, 0x40);
WORLD_ASSERT(TileCar, item, 0x54);
_Static_assert(sizeof(TileCar) == 0x60, "TileCar size");

/* A creature placed in the world (World_GetTileCreature). */
typedef struct TileCreature {
    Vector4 pos;                /* 0x00 */
    u8 pad10[4];                /* 0x10 */
    f32 angle;                  /* 0x14 */
    PTR32(void) info;           /* 0x18 CreatureInfo */
    union {
        u32 thing;              /* 0x1C the spawned creature (handle), 0: none */
        PTR32(void) creature;   /*      the same, as a pointer field */
    };
    union {
        u32 mode;               /* 0x20 1: alive (ActionCreatureChangeMode) */
        u32 spawnable;          /*      as the AI reads it */
    };
    u32 scripts[4];             /* 0x24 per difficulty, 0: CreatureInfo.aiScripts @xdl: link */
    u8 pad34[0xC];              /* 0x34 */
} TileCreature;

WORLD_ASSERT(TileCreature, thing, 0x1C);
_Static_assert(sizeof(TileCreature) == 0x40, "TileCreature size");

/* A teleporter (world + 0x30/0x34); also the data of ActionVehicleTeleport.
 * Thing.c has its own copy of the first part (Thing_CollideWithTeleporters). */
typedef struct Teleporter {
    Matrix4 mtx;                /* 0x00 v[3] is the position */
    u32 editorId;               /* 0x40 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    u32 debris;                 /* 0x44 effect spawned on use, 0: none */
    u32 emitter;                /* 0x48 emitter shown at it (World_InitTeleporters), 0: none */
    f32 radius;                 /* 0x4C */
    u32 numDest;                /* 0x50 */
    u32 flags;                  /* 0x54 */
    PTR32(u32) dest;            /* 0x58 teleporter indices */
    s32 damage;                 /* 0x5C */
    PTR32(Thing) thing;         /* 0x60 the emitter thing */
    u8 pad64[0xC];              /* 0x64 */
} Teleporter;

WORLD_ASSERT(Teleporter, emitter, 0x48);
WORLD_ASSERT(Teleporter, thing, 0x60);
_Static_assert(sizeof(Teleporter) == 0x70, "Teleporter size");

/* A script variable (world + 0x48/0x4C), referenced by triggers/actions. */
typedef struct WorldVariable {
    u8 pad0;                    /* 0x00 */
    u8 active;                  /* 0x01 shown by HUD role 51 */
    u8 id;                      /* 0x02 interfaces bind to it by id */
    u8 pad3;                    /* 0x03 */
    f32 value;                  /* 0x04 */
    u8 pad8[4];                 /* 0x08 */
    PTR32(u8) mode;             /* 0x0C u8 mode (ActionVariableChangeMode) */
} WorldVariable;

WORLD_ASSERT(WorldVariable, value, 0x04);
WORLD_ASSERT(WorldVariable, mode, 0x0C);

/* A timer's mode, set by ActionTimerChangeMode. */
typedef struct WorldTimerMode {
    u8 mode;                    /* 0x00 */
    u8 type;                    /* 0x01 7: stopped (TimerManager_Heartbeat) */
} WorldTimerMode;

/* A script timer (world + 0x50/0x54), counted in ms by TimerManager_Heartbeat. */
typedef struct WorldTimer {
    u8 pad0;                    /* 0x00 */
    u8 active;                  /* 0x01 shown by HUD role 50 */
    u8 type;                    /* 0x02 id interfaces bind to; 0xC: the round timer (GameRules) */
    u8 countUp;                 /* 0x03 0: counts down to 0 */
    u8 pad4[4];                 /* 0x04 */
    f32 time;                   /* 0x08 ms */
    PTR32(WorldTimerMode) mode; /* 0x0C */
} WorldTimer;

WORLD_ASSERT(WorldTimer, countUp, 0x03);
WORLD_ASSERT(WorldTimer, time, 0x08);
WORLD_ASSERT(WorldTimer, mode, 0x0C);

/* Entries of the world's variable and timer tables. Script parameters refer
 * to a variable or timer by the handle of its table entry. */
typedef PTR32(WorldVariable) WorldVariableSlot;
typedef PTR32(WorldTimer) WorldTimerSlot;
#define WORLD_VARIABLE(h) GETP(*(WorldVariableSlot *)PTR(h))
#define WORLD_TIMER(h) GETP(*(WorldTimerSlot *)PTR(h))

/* What a mission objective shows (InterfaceManager_SetMissionObjectiveText).
 * Port_Swap leaves the first four bytes in pack order. */
typedef struct WorldObjectiveInfo {
    u8 type;                    /* 0x00 the value put in the text's '#'s: 0 none, 1 counter,
                                 *      2 timer (mm:ss), 3 code variable */
    u8 mode;                    /* 0x01 0x17: shown (ActionMissionObjChangeMode) */
    u8 id;                      /* 0x02 IfaceObjectDef.id of the HUD object showing it */
    u8 pad3[9];
    PTR32(WorldVariable) done;  /* 0x0C value != 0: `doneText`, NULL: `text` if any */
    PTR32(void) value;          /* 0x10 type 1: WorldVariable, 2: WorldTimer,
                                 *      3: { code variable id, argument } */
    PTR32(PTR32(char)) text;    /* 0x14 string table */
    PTR32(PTR32(char)) doneText; /* 0x18 */
} WorldObjectiveInfo;

/* A mission objective (world + 0x58/0x5C). */
typedef struct WorldObjective {
    u32 arg;                    /* 0x00 argument of a type 3 code variable below 10 */
    PTR32(WorldObjectiveInfo) info; /* 0x04 */
} WorldObjective;

/* ---- volumes (VolumeManager.c) ------------------------------------------- */

/* VolumeCell.lists[] index, = WorldVolume.type */
enum VolumeType {
    VOLUME_AIRBOX = 0,          /* no data: the volumes the airbox triggers and actions name */
    VOLUME_FORCE_BOX = 1,       /* data: Thing.c ForceBox */
    VOLUME_REVERB = 2,          /* data: { u32 type, u16 volume, u16 feedback } */
    VOLUME_CAMERA = 3,          /* the driving camera keeps the car's up inside (Camera_UpdatePosition_Default) */
    VOLUME_FOG = 4,             /* data: { u8 type, f32 near, f32 far, GXColor } */
    VOLUME_CREATURE = 5,        /* creature spawn volumes (AI_Creature); PC: rescue zones (data byte 0) */
    VOLUME_LIGHT = 6,           /* data: a Light (LightManager) */
    VOLUME_NUM_TYPES = 7,
};

/* WorldVolume.shape */
enum VolumeShape {
    VOLUME_SHAPE_BSP = 0,
    VOLUME_SHAPE_SPHERE = 1,
};

/* A volume (world + 0x68/0x6C): a sphere or a BSP. Triggers ("airboxes") and
 * the volume grid refer to them; `data` depends on the type. */
typedef struct WorldVolume {
    u32 editorId;               /* 0x00 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    u32 enabled;                /* 0x04 airboxes: 1 when on (ActionAirboxChangeMode) */
    s32 shape;                  /* 0x08 0: BSP, 1: sphere @xdl: enum=VolumeShape */
    u32 type;                   /* 0x0C VOLUME_* @xdl: enum=VolumeType */
    Vector4 center;             /* 0x10 sphere */
    f32 radius;                 /* 0x20 */
    f32 radiusSq;               /* 0x24 */
    PTR32(void) bsp;            /* 0x28 BSP shape */
    PTR32(void) data;           /* 0x2C settings, by type */
} WorldVolume;

WORLD_ASSERT(WorldVolume, shape, 0x08);
WORLD_ASSERT(WorldVolume, type, 0x0C);
WORLD_ASSERT(WorldVolume, bsp, 0x28);
WORLD_ASSERT(WorldVolume, data, 0x2C);
_Static_assert(sizeof(WorldVolume) == 0x30, "WorldVolume size");

typedef struct VolumeList {
    u32 count;
    PTR32(PTR32(WorldVolume)) volumes;
} VolumeList;

/* One cell of the volume grid: the volumes of each type touching it. */
typedef struct VolumeCell {
    VolumeList lists[VOLUME_NUM_TYPES];
} VolumeCell;

_Static_assert(sizeof(VolumeCell) == 0x38, "VolumeCell size");

/* The volume grid (world + 0x4). */
typedef struct VolumeHolder {
    f32 origin[3];              /* 0x00 */
    u8 padC[4];                 /* 0x0C */
    PTR32(VolumeCell) cells;    /* 0x10 */
    PTR32(s32) cellIndex;       /* 0x14 cell of each grid square, x fastest, then z, then y */
    u8 pad18[8];                /* 0x18 */
    f32 cellSize[3];            /* 0x20 */
    s32 numX;                   /* 0x2C */
    s32 numY;                   /* 0x30 */
    s32 numZ;                   /* 0x34 */
} VolumeHolder;

WORLD_ASSERT(VolumeHolder, cells, 0x10);
WORLD_ASSERT(VolumeHolder, cellSize, 0x20);
WORLD_ASSERT(VolumeHolder, numZ, 0x34);

/* A placed light (World_GetTileLight), 0x20 bytes. */
typedef struct TileLight {
    Vector4 pos;                /* 0x00 */
    PTR32(void) desc;           /* 0x10 LightHolder_Add */
    u8 pad14[0xC];              /* 0x14 */
} TileLight;

_Static_assert(sizeof(TileLight) == 0x20, "TileLight size");

/* A placed sound source (World_GetTileSpeaker), 0x40 bytes. */
typedef struct TileSpeakerSound {
    u8 type;                    /* 0x00 0: positional (range below) */
    u8 pad1[7];                 /* 0x01 */
    f32 volume;                 /* 0x08 SoundSource_Set 0 */
    f32 pitch;                  /* 0x0C SoundSource_Set 1 */
    u8 pad10[4];                /* 0x10 */
    s32 sound;                  /* 0x14 */
    PTR32(f32) range;           /* 0x18 two floats: SoundSource_Set 2 and 3 */
} TileSpeakerSound;

typedef struct TileSpeaker {
    Vector4 pos;                /* 0x00 */
    u8 pad10[4];                /* 0x10 */
    PTR32(TileSpeakerSound) sound; /* 0x14 */
    u8 pad18[4];                /* 0x18 */
    u32 mode;                   /* 0x1C ActionSpeakerChangeMode */
    u8 pad20[0xC];              /* 0x20 */
    u32 soundSeq;               /* 0x2C sound sequence started there, 0: none */
    u8 pad30[0x10];             /* 0x30 */
} TileSpeaker;

/* A mover placed on a track (World_GetTilePath), 0x20 bytes: spawned as a
 * physical object (DESTRUCTIBLE_MOVER) at round start. */
typedef struct TilePath {
    u32 editorId;               /* 0x00 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    u32 anim;                   /* 0x04 the track (DestructibleParams.anim) @xdl: link */
    f32 speed;                  /* 0x08 */
    u32 record;                 /* 0x0C ThingRecord handle @xdl: link=ThingRecord */
    PTR32(Thing) thing;         /* 0x10 the spawned mover */
    u32 enabled;                /* 0x14 0: not spawned */
    u8 pad18;                   /* 0x18 */
    u8 hasDrawDist;             /* 0x19 */
    u8 pad1A[2];                /* 0x1A */
    f32 drawDist;               /* 0x1C */
} TilePath;

WORLD_ASSERT(TileSpeaker, soundSeq, 0x2C);
_Static_assert(sizeof(TileSpeaker) == 0x40, "TileSpeaker size");
_Static_assert(sizeof(TilePath) == 0x20, "TilePath size");

/* ---- streaming quadrants (FileStreaming.c, World_Render) ------------------ */

/* Mode of a quadrant object (QuadInstance.mode, QuadFxPoint.mode), as set by
 * ActionDestructibleChangeMode / ActionFXPointChangeMode. */
enum {
    QUAD_OBJECT_DESTROYED = 3,  /* instances: not respawned unless `respawn` */
};

/* ThingRecord byte 0x07 (common.h pad07): the object is static scenery, drawn
 * by RenderQuadrant instead of being spawned as a thing. */
#define THINGRECORD_IS_STATIC(rec) ((rec)->isStatic)

/* A placed object of a quadrant: drawn directly when its record is static,
 * otherwise spawned as a destructible (type 3) while the quadrant is active.
 * Trigger lists refer to these for "destructible" triggers. */
typedef struct QuadInstance {
    Vector4 pos;                /* 0x00 */
    Quaternion quat;            /* 0x10 */
    u32 editorId;               /* 0x20 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    PTR32(ThingRecord) record;  /* 0x24 see THINGRECORD_IS_STATIC */
    f32 drawDist;               /* 0x28 used if hasDrawDist */
    PTR32(Thing) thing;         /* 0x2C the spawned destructible */
    u32 mode;                   /* 0x30 TacToken: on, off, kill (QUAD_OBJECT_DESTROYED) @xdl: enum=TacToken */
    u8 hasDrawDist;             /* 0x34 */
    s8 respawn;                 /* 0x35 */
    u8 defMode;                 /* 0x36 mode at round start (GameRules_Init) */
    u8 pad37[5];                /* 0x37 */
    f32 radius;                 /* 0x3C static ones: bounding radius, for culling */
} QuadInstance;

WORLD_ASSERT(QuadInstance, record, 0x24);
WORLD_ASSERT(QuadInstance, thing, 0x2C);
WORLD_ASSERT(QuadInstance, mode, 0x30);
WORLD_ASSERT(QuadInstance, defMode, 0x36);
WORLD_ASSERT(QuadInstance, radius, 0x3C);
_Static_assert(sizeof(QuadInstance) == 0x40, "QuadInstance size");

/* An effect point of a quadrant: an emitter (type 2) and/or a corona. */
typedef struct QuadFxPoint {
    Vector4 pos;                /* 0x00 */
    Quaternion quat;            /* 0x10 */
    u32 editorId;               /* 0x20 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    PTR32(void) emitter;        /* 0x24 emitter description, or NULL */
    PTR32(void) corona;         /* 0x28 or NULL */
    f32 drawDist;               /* 0x2C used if hasDrawDist */
    PTR32(Thing) thing;         /* 0x30 the spawned emitter */
    u32 mode;                   /* 0x34 TacToken: on, off (ActionFXPointChangeMode) @xdl: enum=TacToken */
    u8 hasDrawDist;             /* 0x38 */
    u8 defMode;                 /* 0x39 mode at round start */
    u8 pad3A[6];                /* 0x3A */
} QuadFxPoint;

WORLD_ASSERT(QuadFxPoint, thing, 0x30);
WORLD_ASSERT(QuadFxPoint, defMode, 0x39);
_Static_assert(sizeof(QuadFxPoint) == 0x40, "QuadFxPoint size");

/* QuadItem.mode */
enum {
    QUAD_ITEM_OFF = 0,
    QUAD_ITEM_ON = 1,           /* (re)spawned by FileStreaming_HeartBeatItems */
};

/* A pickup spawn point of a quadrant (ItemThing.spawner). */
typedef struct QuadItem {
    Vector4 pos;                /* 0x00 */
    u32 editorId;               /* 0x10 never read; a number of its own per object of the world, from the exporter (guessed: its object id) */
    s32 respawn;                /* 0x14 ms before it respawns once taken, 0: never */
    s32 timer;                  /* 0x18 ms until it spawns */
    PTR32(ItemRecord) record;   /* 0x1C */
    f32 drawDist;               /* 0x20 used if hasDrawDist */
    PTR32(ItemThing) thing;     /* 0x24 the spawned item */
    u32 mode;                   /* 0x28 QUAD_ITEM_* (TacToken on, off) @xdl: enum=TacToken */
    u8 pad2C[4];                /* 0x2C */
    u16 id;                     /* 0x30 in the world's item pack, 0xFFFF: random in lo..hi */
    u8 pad32[2];                /* 0x32 */
    u8 hasDrawDist;             /* 0x34 */
    u8 aiPickup;                /* 0x35 ItemParams.aiPickup for items spawned here */
    u8 lo;                      /* 0x36 */
    u8 hi;                      /* 0x37 */
    s8 spawned;                 /* 0x38 */
    u8 defMode;                 /* 0x39 mode at round start */
    u8 pad3A[6];                /* 0x3A */
} QuadItem;

WORLD_ASSERT(QuadItem, respawn, 0x14);
WORLD_ASSERT(QuadItem, record, 0x1C);
WORLD_ASSERT(QuadItem, thing, 0x24);
WORLD_ASSERT(QuadItem, mode, 0x28);
WORLD_ASSERT(QuadItem, spawned, 0x38);
_Static_assert(sizeof(QuadItem) == 0x40, "QuadItem size");

/* ItemRecord fields ItemThing.h doesn't name yet. */

/* The objects of a quadrant. */
typedef struct QuadContents {
    s8 dirty;                   /* 0x00 things spawned (FileStreaming_SetActiveQuadrants) */
    u32 numInstances;           /* 0x04 */
    PTR32(QuadInstance) instances; /* 0x08 @xdl: count=numInstances */
    u32 numFxPoints;            /* 0x0C */
    PTR32(QuadFxPoint) fxPoints;   /* 0x10 @xdl: count=numFxPoints */
    u32 numItems;               /* 0x14 */
    PTR32(QuadItem) items;      /* 0x18 @xdl: count=numItems */
} QuadContents;

WORLD_ASSERT(QuadContents, items, 0x18);

/* A geometry piece of a quadrant (RenderQuadrant). */
typedef struct QuadGeom {
    u8 pad0[8];                 /* 0x00 */
    PTR32(void) geom;           /* 0x08 */
} QuadGeom;

/* A streaming quadrant (world + 0x20/0x24). Its geometry is a quad pack in
 * the world's .xgc file, loaded while a camera is in it or a neighbour. The
 * one without a visibility BSP is the "world quadrant", always loaded. */
typedef struct WorldQuad {
    u32 id;                     /* 0x00 neighbors[] hold ids until FileStreaming_InitializeAllQuads */
    u8 pad4[8];                 /* 0x04 */
    u32 numGeoms;               /* 0x0C also the quad pack's "Num GX" */
    PTR32(QuadGeom) geoms;      /* 0x10 set while the pack is loaded */
    u8 pad14[8];                /* 0x14 */
    PTR32(void) bsp;            /* 0x1C visibility (BSPN0_CollideWithVector), NULL: world quadrant */
    u32 numNeighbors;           /* 0x20 */
    PTR32(u32) neighbors;       /* 0x24 quadrant indices */
    u8 pad28[8];                /* 0x28 quad pack header (common.h QuadPack) */
    PTR32(QuadContents) contents; /* 0x30 */
    s8 loaded;                  /* 0x34 pack loaded and pointers refreshed */
    s8 visible;                 /* 0x35 this frame (World_Render) */
    u8 pad36[2];                /* 0x36 */
    u32 packId;                 /* 0x38 low 16 bits: pack id, high: numGeoms */
    u32 fileOffset;             /* 0x3C */
    u32 size;                   /* 0x40 */
    u8 pad44[0xC];              /* 0x44 */
} WorldQuad;

WORLD_ASSERT(WorldQuad, numGeoms, 0x0C);
WORLD_ASSERT(WorldQuad, bsp, 0x1C);
WORLD_ASSERT(WorldQuad, contents, 0x30);
WORLD_ASSERT(WorldQuad, loaded, 0x34);
WORLD_ASSERT(WorldQuad, packId, 0x38);
WORLD_ASSERT(WorldQuad, size, 0x40);
_Static_assert(sizeof(WorldQuad) == 0x50, "WorldQuad size");

/* ---- the world ----------------------------------------------------------- */

/* World-wide settings (world + 0x9C, World_GetGameData). */
typedef struct WorldGameData {
    u8 fogType;                 /* 0x00 */
    u8 pad1[3];                 /* 0x01 never read (GC, PC, PS2) */
    f32 mapScaleX;              /* 0x04 radar map (InterfaceManager) */
    f32 mapScaleZ;              /* 0x08 */
    f32 envmapX;                /* 0x0C */
    f32 envmapY;                /* 0x10 */
    f32 fogNear;                /* 0x14 */
    f32 fogFar;                 /* 0x18 */
    u8 pad1C[4];                /* 0x1C 0 in every world; never read (GC, PC, PS2) */
    f32 farPlane;               /* 0x20 */
    f32 reverbLevel;            /* 0x24 outside reverb volumes */
    f32 reverbDelay;            /* 0x28 */
    f32 reverbFeedback;         /* 0x2C */
    f32 musicVol;               /* 0x30 scales of the music actions (Music_Change) */
    f32 musicPitch;             /* 0x34 */
    f32 musicTempo;             /* 0x38 */
    f32 cameraFxLead;           /* 0x3C cameraFx is kept this many seconds of car travel ahead */
    u8 pad40[0x18];             /* 0x40 f32 0.3, 80, 0.075 in every world, then 0;
                                 *      never read (GC, PC, PS2) */
    f32 mapOffsetX;             /* 0x58 radar map: texture coordinates of the origin */
    f32 mapOffsetZ;             /* 0x5C */
    f32 mapHalfW;               /* 0x60 half the area shown, in texture coordinates */
    f32 mapHalfH;               /* 0x64 */
    f32 impactFxMinVel;         /* 0x68 car-car impact effects, by relative speed */
    f32 impactFxMaxVel;         /* 0x6C */
    f32 creatureFxMinVel;       /* 0x70 PC only: effects of a car running a creature over */
    f32 creatureFxMaxVel;       /* 0x74 (0x413060), picked by speed like impactFx; 5, 100 */
    u32 mapTexture;             /* 0x78 radar map, 0: none (InterfaceManager_DrawMap) @xdl: texture */
    u32 radarBlip;              /* 0x7C InterfaceManager_SetRadarBlip @xdl: texture */
    u8 pad80[8];                /* 0x80 two texture slots; never read (GC, PC, PS2) */
    u32 lightTexture;           /* 0x88 LightHolder_SetTexture @xdl: texture */
    u32 envmapA;                /* 0x8C @xdl: texture */
    u32 envmapB;                /* 0x90 @xdl: texture */
    u8 pad94[0xC];              /* 0x94 two texture slots, a pointer; never read (GC, PC, PS2) */
    PTR32(void) vehiclePackList; /* 0xA0 */
    PTR32(char) itemPack;       /* 0xA4 name of the world's item pack (type 2) */
    u8 padA8[0x18];             /* 0xA8 0 in every world; never read (GC, PC, PS2) */
    PTR32(GXColor) fogColor;    /* 0xC0 */
    PTR32(void) cameraFx;       /* 0xC4 emitter description kept ahead of each camera (rain...) */
    PTR32(void) debugFont;      /* 0xC8 debug font (ShowStats_Init) */
    PTR32(void) pauseInterface; /* 0xCC interface script of the pause menu */
    u8 padD0[0x10];             /* 0xD0 0 in every world; never read (GC, PC, PS2) */
    u8 padE0[8];                /* 0xE0 two pointers; never read (GC, PC, PS2) */
    u32 numCreatureHitFx;       /* 0xE8 PC only (creature hit handler 0x41F620): */
    PTR32(u32) creatureHitFx;   /* 0xEC [2] under 60 damage, [3] from 60, at a random
                                 *      bone; 0 entries in every world */
    u32 numImpactFx;            /* 0xF0 */
    PTR32(u32) impactFx;        /* 0xF4 debris ids, by speed */
    u32 numCreatureFx;          /* 0xF8 PC only, with creatureFxMinVel: debris ids, */
    PTR32(u32) creatureFx;      /* 0xFC by speed */
    u32 numCameras;             /* 0x100 chase cameras of things without their own */
    PTR32(u8) cameras;          /* 0x104 12 bytes each (CarACameras) */
    u32 numCamerasWide;         /* 0x108 the same for view format 2 */
    PTR32(u8) camerasWide;      /* 0x10C */
} WorldGameData;

WORLD_ASSERT(WorldGameData, fogFar, 0x18);
WORLD_ASSERT(WorldGameData, farPlane, 0x20);
WORLD_ASSERT(WorldGameData, reverbLevel, 0x24);
WORLD_ASSERT(WorldGameData, mapOffsetX, 0x58);
WORLD_ASSERT(WorldGameData, mapHalfH, 0x64);
WORLD_ASSERT(WorldGameData, impactFxMinVel, 0x68);
WORLD_ASSERT(WorldGameData, mapTexture, 0x78);
WORLD_ASSERT(WorldGameData, radarBlip, 0x7C);
WORLD_ASSERT(WorldGameData, impactFxMinVel, 0x68);
WORLD_ASSERT(WorldGameData, envmapA, 0x8C);
WORLD_ASSERT(WorldGameData, itemPack, 0xA4);
WORLD_ASSERT(WorldGameData, fogColor, 0xC0);
WORLD_ASSERT(WorldGameData, impactFx, 0xF4);
WORLD_ASSERT(WorldGameData, creatureFxMinVel, 0x70);
WORLD_ASSERT(WorldGameData, padE0, 0xE0);
WORLD_ASSERT(WorldGameData, numCreatureHitFx, 0xE8);
WORLD_ASSERT(WorldGameData, creatureFx, 0xFC);
WORLD_ASSERT(WorldGameData, numCameras, 0x100);
_Static_assert(sizeof(WorldGameData) == 0x110, "WorldGameData size");

/* The world's collision pack (world + 0x0; CollisionTerrain.c TerrainGrid):
 * only the parts World_ClearSweep uses. Polys and edges carry a sweep stamp
 * (g_worldData.sweep) so a query tests each once. */
typedef struct WorldCollision {
    Vector4 min;                /* 0x00 world bounds (Grid_Init) */
    Vector4 max;                /* 0x10 */
    PTR32(TerrainCell) cells;   /* 0x20 cells[0]: outside the grid */
    u32 version;                /* 0x24 format, 0xD (LoadWorldProcess) */
    f32 cellSize[3];            /* 0x28 */
    s32 numX;                   /* 0x34 */
    s32 numY;                   /* 0x38 */
    s32 numZ;                   /* 0x3C */
    PTR32(s32) cellIndex;       /* 0x40 cell of each square, x fastest, then z, then y */
    u8 pad44[4];                /* 0x44 */
    u32 numPolys;               /* 0x48 */
    PTR32(TerrainPoly) polys;   /* 0x4C */
    u32 numEdges;               /* 0x50 */
    PTR32(TerrainEdge) edges;   /* 0x54 */
    u32 mode;                   /* 0x58 1: cells hold BSPs, else poly lists */
    PTR32(u32) materials;       /* 0x5C surface handles */
} WorldCollision;

WORLD_ASSERT(WorldCollision, cells, 0x20);
WORLD_ASSERT(WorldCollision, cellIndex, 0x40);
WORLD_ASSERT(WorldCollision, mode, 0x58);

typedef struct World {
    PTR32(WorldCollision) collision; /* 0x00 */
    PTR32(VolumeHolder) volumeHolder; /* 0x04 */
    u32 numTileCars;            /* 0x08 */
    PTR32(TileCar) tileCars;    /* 0x0C @xdl: count=numTileCars */
    u32 numTilePaths;           /* 0x10 */
    PTR32(struct TilePath) tilePaths; /* 0x14 @xdl: count=numTilePaths */
    u8 pad18[8];                /* 0x18 */
    u32 numQuads;               /* 0x20 */
    PTR32(WorldQuad) quads;     /* 0x24 @xdl: count=numQuads */
    u32 numSpeakers;            /* 0x28 */
    PTR32(struct TileSpeaker) speakers; /* 0x2C @xdl: count=numSpeakers */
    u32 numTeleporters;         /* 0x30 */
    PTR32(Teleporter) teleporters; /* 0x34 @xdl: count=numTeleporters */
    u32 numTileLights;          /* 0x38 */
    PTR32(struct TileLight) tileLights; /* 0x3C @xdl: count=numTileLights */
    u32 numTileCreatures;       /* 0x40 */
    PTR32(TileCreature) tileCreatures; /* 0x44 @xdl: count=numTileCreatures */
    u32 numVariables;           /* 0x48 */
    PTR32(WorldVariableSlot) variables; /* 0x4C @xdl: count=numVariables */
    u32 numTimers;              /* 0x50 */
    PTR32(WorldTimerSlot) timers; /* 0x54 @xdl: count=numTimers */
    u32 numObjectives;          /* 0x58 */
    PTR32(WorldObjective) objectives; /* 0x5C @xdl: count=numObjectives */
    u32 numLights;              /* 0x60 VertexColorSeqManager_Init */
    PTR32(u8) lights;           /* 0x64 0x10 each (LightManager, VertexColorSeqManager) */
    u32 numVolumes;             /* 0x68 */
    PTR32(WorldVolume) volumes; /* 0x6C @xdl: count=numVolumes */
    u8 pad70[8];                /* 0x70 */
    PTR32(void) nav;            /* 0x78 NavHolder */
    PTR32(char) collisionPackName; /* 0x7C the world's collision pack (type 5) */
    s32 collisionPackID;        /* 0x80 */
    PTR32(char) quadPackName;   /* 0x84 its quadrants' geometry pack (type 6) */
    s32 quadPackID;             /* 0x88 */
    PTR32(char) name;           /* 0x8C the quad packs are in <name>.xgc */
    u32 id;                     /* 0x90 */
    u8 pad94[8];                /* 0x94 */
    PTR32(WorldGameData) gameData; /* 0x9C */
} World;

WORLD_ASSERT(World, volumeHolder, 0x04);
WORLD_ASSERT(World, tileCars, 0x0C);
WORLD_ASSERT(World, quads, 0x24);
WORLD_ASSERT(World, teleporters, 0x34);
WORLD_ASSERT(World, tileCreatures, 0x44);
WORLD_ASSERT(World, variables, 0x4C);
WORLD_ASSERT(World, timers, 0x54);
WORLD_ASSERT(World, objectives, 0x5C);
WORLD_ASSERT(World, volumes, 0x6C);
WORLD_ASSERT(World, nav, 0x78);
WORLD_ASSERT(World, name, 0x8C);
WORLD_ASSERT(World, gameData, 0x9C);

/* The current world, or NULL. */
#define WORLD() ((World *)GETP(g_worldData.world))

/* ---- data pack slots (FileStreaming.c) ----------------------------------- */

/* A slot of g_StreamingDataPacks[type]: a data pack loaded by name and id,
 * reference counted. */
typedef struct StreamingPack {
    s8 used;                    /* 0x00 */
    s8 loaded;                  /* 0x01 file read (LoadDataPack) */
    s8 refreshed;               /* 0x02 pointers fixed up (DataPack_RefreshPtrs) */
    u8 pad3;                    /* 0x03 */
    PTR32(char) name;           /* 0x04 */
    u32 id;                     /* 0x08 */
    s32 refs;                   /* 0x0C */
    u8 unused10;                /* 0x10 */
    u8 pad11[3];                /* 0x11 */
    DataPack pack;              /* 0x14 */
} StreamingPack;

WORLD_ASSERT(StreamingPack, refs, 0x0C);
WORLD_ASSERT(StreamingPack, pack, 0x14);
WORLD_ASSERT(WorldObjectiveInfo, done, 0x0C);
WORLD_ASSERT(WorldObjectiveInfo, doneText, 0x18);
_Static_assert(sizeof(StreamingPack) == 0xA8, "StreamingPack size");

#undef WORLD_ASSERT

/* port: the optional longer draw distance (Port_DrawDist.c) */
s32 Port_DrawDistLevel(void);
void Port_DrawDistRender(World *world, WorldQuad **current, u32 numCameras, WorldQuad *hub);
void Port_DrawDistReleaseAll(void);
f32 Port_DrawDistFarPlane(f32 farPlane);
s32 Port_DrawDistFog(f32 *start, f32 *end);
f32 Port_DrawDistProjectionFar(f32 farPlane);
/* port, testing: HWVX_AUTOPILOT=1, player 1's car drives itself (Port_Autopilot.c) */
struct Player;
s32 Port_AutopilotDrive(struct Player *player);
void Port_AutopilotAfterAIEvents(void);
void Port_QuadLog(World *world, WorldQuad **current, u32 numCameras);

#endif
