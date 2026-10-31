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
    u8 pad1C[8];                /* 0x1C */
    union {
        PTR32(struct CarAInfo) info;          /* 0x24 the vehicle; AI scripts per difficulty at 0x90 */
        PTR32(struct AIScriptSet) aiScripts;  /*      the same, as the AI reads it */
    };
    PTR32(struct CarA) car;     /* 0x28 the spawned car, NULL if none */
    u32 mode;                   /* 0x2C last ActionVehicleChangeMode mode */
    u32 vehicle;                /* 0x30 */
    union {
        u8 unk34;               /* 0x34 nonzero: not spawned by mode 1/10; AIPlayerDesc.unkC */
        u8 dynamic;             /*      AIPlayerParams.dynamic */
    };
    u8 port;                    /* 0x35 1-4: human player slot; AI cars spawn only when > 4 */
    u8 pad36[2];                /* 0x36 */
    f32 nearDist;               /* 0x38 AI ranges */
    f32 farDist;                /* 0x3C */
    u32 scripts[4];             /* 0x40 AI script per difficulty, 0: the vehicle's */
    u8 pad50[4];                /* 0x50 */
    PTR32(ItemRecord) item;     /* 0x54 wildcard the car starts with, NULL: none */
    u8 pad58[4];                /* 0x58 */
    s8 respawn;                 /* 0x5C freed (car = NULL) when its AI car is removed */
    u8 pad5D[3];                /* 0x5D */
} TileCar;

WORLD_ASSERT(TileCar, info, 0x24);
WORLD_ASSERT(TileCar, car, 0x28);
WORLD_ASSERT(TileCar, unk34, 0x34);
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
    u32 scripts[4];             /* 0x24 per difficulty, 0: CreatureInfo.aiScripts */
    u8 pad34[0xC];              /* 0x34 */
} TileCreature;

WORLD_ASSERT(TileCreature, thing, 0x1C);
_Static_assert(sizeof(TileCreature) == 0x40, "TileCreature size");

/* A teleporter (world + 0x30/0x34); also the data of ActionVehicleTeleport.
 * Thing.c has its own copy of the first part (Thing_CollideWithTeleporters). */
typedef struct Teleporter {
    Matrix4 mtx;                /* 0x00 v[3] is the position */
    u8 pad40[4];                /* 0x40 */
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
    u8 pad0[2];                 /* 0x00 */
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
    u8 pad0[2];                 /* 0x00 */
    u8 type;                    /* 0x02 0xC: the round timer (GameRules) */
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

/* A mission objective (world + 0x58/0x5C). */
typedef struct WorldObjective {
    u32 unk0;                   /* 0x00 */
    PTR32(u8) info;             /* 0x04 u8 mode at 0x1 (ActionMissionObjChangeMode) */
} WorldObjective;

/* ---- volumes (VolumeManager.c) ------------------------------------------- */

/* VolumeCell.lists[] index, = WorldVolume.type */
enum {
    VOLUME_FORCE_BOX = 1,       /* data: Thing.c ForceBox */
    VOLUME_REVERB = 2,          /* data: { u32 type, u16 volume, u16 feedback } */
    VOLUME_FOG = 4,             /* data: { u8 type, f32 near, f32 far, GXColor } */
    VOLUME_NUM_TYPES = 7,
};

/* A volume (world + 0x68/0x6C): a sphere or a BSP. Triggers ("airboxes") and
 * the volume grid refer to them; `data` depends on the type. */
typedef struct WorldVolume {
    u8 pad0[4];                 /* 0x00 */
    u32 enabled;                /* 0x04 airboxes: 1 when on (ActionAirboxChangeMode) */
    s32 shape;                  /* 0x08 0: BSP, 1: sphere */
    u32 type;                   /* 0x0C VOLUME_* */
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
    u8 pad20[4];                /* 0x20 */
    PTR32(ThingRecord) record;  /* 0x24 see THINGRECORD_IS_STATIC */
    f32 drawDist;               /* 0x28 used if hasDrawDist */
    PTR32(Thing) thing;         /* 0x2C the spawned destructible */
    u32 mode;                   /* 0x30 */
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
    u8 pad20[4];                /* 0x20 */
    PTR32(void) emitter;        /* 0x24 emitter description, or NULL */
    PTR32(void) corona;         /* 0x28 or NULL */
    f32 drawDist;               /* 0x2C used if hasDrawDist */
    PTR32(Thing) thing;         /* 0x30 the spawned emitter */
    u32 mode;                   /* 0x34 */
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
    u8 pad10[4];                /* 0x10 */
    s32 respawn;                /* 0x14 ms before it respawns once taken, 0: never */
    s32 timer;                  /* 0x18 ms until it spawns */
    PTR32(ItemRecord) record;   /* 0x1C */
    f32 drawDist;               /* 0x20 used if hasDrawDist */
    PTR32(ItemThing) thing;     /* 0x24 the spawned item */
    u32 mode;                   /* 0x28 QUAD_ITEM_* */
    u8 pad2C[8];                /* 0x2C */
    u8 hasDrawDist;             /* 0x34 */
    u8 unk35;                   /* 0x35 ItemParams.unk38 */
    u8 pad36[2];                /* 0x36 */
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
    PTR32(QuadInstance) instances; /* 0x08 */
    u32 numFxPoints;            /* 0x0C */
    PTR32(QuadFxPoint) fxPoints;   /* 0x10 */
    u32 numItems;               /* 0x14 */
    PTR32(QuadItem) items;      /* 0x18 */
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
    u8 pad1[3];                 /* 0x01 */
    f32 mapScaleX;              /* 0x04 radar map (InterfaceManager) */
    f32 mapScaleZ;              /* 0x08 */
    f32 envmapX;                /* 0x0C */
    f32 envmapY;                /* 0x10 */
    f32 fogNear;                /* 0x14 */
    f32 fogFar;                 /* 0x18 */
    u8 pad1C[4];                /* 0x1C */
    f32 farPlane;               /* 0x20 */
    f32 reverbLevel;            /* 0x24 outside reverb volumes */
    f32 reverbDelay;            /* 0x28 */
    f32 reverbFeedback;         /* 0x2C */
    u8 pad30[0x38];             /* 0x30 music volume/pitch/tempo scales, radar map... */
    f32 impactFxMinVel;         /* 0x68 car-car impact effects, by relative speed */
    f32 impactFxMaxVel;         /* 0x6C */
    u8 pad70[0x1C];             /* 0x70 */
    u32 envmapA;                /* 0x8C */
    u32 envmapB;                /* 0x90 */
    u8 pad94[0x10];             /* 0x94 */
    PTR32(char) itemPack;       /* 0xA4 name of the world's item pack (type 2) */
    u8 padA8[0x18];             /* 0xA8 */
    PTR32(GXColor) fogColor;    /* 0xC0 */
    u8 padC4[0x2C];             /* 0xC4 */
    u32 numImpactFx;            /* 0xF0 */
    PTR32(u32) impactFx;        /* 0xF4 debris ids, by speed */
} WorldGameData;

WORLD_ASSERT(WorldGameData, fogFar, 0x18);
WORLD_ASSERT(WorldGameData, farPlane, 0x20);
WORLD_ASSERT(WorldGameData, reverbLevel, 0x24);
WORLD_ASSERT(WorldGameData, impactFxMinVel, 0x68);
WORLD_ASSERT(WorldGameData, envmapA, 0x8C);
WORLD_ASSERT(WorldGameData, itemPack, 0xA4);
WORLD_ASSERT(WorldGameData, fogColor, 0xC0);
WORLD_ASSERT(WorldGameData, impactFx, 0xF4);

/* The world's collision pack (world + 0x0; CollisionTerrain.c TerrainGrid):
 * only the parts World_ClearSweep uses. Polys and edges carry a sweep stamp
 * (g_worldData.sweep) so a query tests each once. */
typedef struct WorldCollision {
    u8 pad0[0x48];              /* 0x00 grid */
    u32 numPolys;               /* 0x48 */
    PTR32(u8) polys;            /* 0x4C 0x20 each, stamp at 0x18 */
    u32 numEdges;               /* 0x50 */
    PTR32(u8) edges;            /* 0x54 0x30 each, stamp at 0x20 */
    u32 mode;                   /* 0x58 */
    PTR32(u32) materials;       /* 0x5C */
} WorldCollision;

WORLD_ASSERT(WorldCollision, mode, 0x58);

typedef struct World {
    PTR32(WorldCollision) collision; /* 0x00 */
    PTR32(VolumeHolder) volumeHolder; /* 0x04 */
    u32 numTileCars;            /* 0x08 */
    PTR32(TileCar) tileCars;    /* 0x0C */
    u32 numTilePaths;           /* 0x10 */
    PTR32(u8) tilePaths;        /* 0x14 0x20 each */
    u8 pad18[8];                /* 0x18 */
    u32 numQuads;               /* 0x20 */
    PTR32(WorldQuad) quads;     /* 0x24 */
    u32 numSpeakers;            /* 0x28 */
    PTR32(u8) speakers;         /* 0x2C 0x40 each */
    u32 numTeleporters;         /* 0x30 */
    PTR32(Teleporter) teleporters; /* 0x34 */
    u32 numTileLights;          /* 0x38 */
    PTR32(u8) tileLights;       /* 0x3C 0x20 each */
    u32 numTileCreatures;       /* 0x40 */
    PTR32(TileCreature) tileCreatures; /* 0x44 */
    u32 numVariables;           /* 0x48 */
    PTR32(WorldVariableSlot) variables; /* 0x4C */
    u32 numTimers;              /* 0x50 */
    PTR32(WorldTimerSlot) timers; /* 0x54 */
    u32 numObjectives;          /* 0x58 */
    PTR32(WorldObjective) objectives; /* 0x5C */
    u8 pad60[4];                /* 0x60 */
    PTR32(u8) lights;           /* 0x64 0x10 each (LightManager) */
    u32 numVolumes;             /* 0x68 */
    PTR32(WorldVolume) volumes; /* 0x6C */
    u8 pad70[8];                /* 0x70 */
    PTR32(void) nav;            /* 0x78 NavHolder */
    PTR32(char) fileName;       /* 0x7C */
    u8 pad80[0xC];              /* 0x80 */
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
    u8 unk10;                   /* 0x10 */
    u8 pad11[3];                /* 0x11 */
    DataPack pack;              /* 0x14 */
} StreamingPack;

WORLD_ASSERT(StreamingPack, refs, 0x0C);
WORLD_ASSERT(StreamingPack, pack, 0x14);
_Static_assert(sizeof(StreamingPack) == 0xA8, "StreamingPack size");

#undef WORLD_ASSERT

#endif
