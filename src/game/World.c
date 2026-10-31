/* The world: loading state, quadrant visibility and rendering, accessors for
 * its records (World.h), and the script functions for its items, variables
 * and timers. */
#include "common.h"
#include "World.h"
#include "Trigger.h"
#include "ThingManager.h"

/* g_worldData (common.h WorldData): world, then
 *   unk4     sweep stamp for collision queries (World_ClearSweep/NextSweep)
 *   unk8[2]  the world's DataPack (handle), for quad pack pointer fix-ups
 *   unk8[3]  s8: pick the world quadrant again on the next World_Render */
#define WORLD_SWEEP (g_worldData.unk4)
#define WORLD_PACK (g_worldData.unk8[2])
#define WORLD_RESET_QUADS (*(s8 *)&g_worldData.unk8[3])

s32 World_Create(void) {
    extern s32 m_VertIn;
    extern s32 g_drawShadowsFlag;
    m_VertIn = 0;
    g_drawShadowsFlag = 1;
    SETP(g_worldData.world, NULL);
    return 1;
}

void World_Destroy(void) {
    if (WORLD() != NULL) {
        World_Unload();
    }
}

/* The decomp has an m2c placeholder here; the original calls World_ClearSweep (asm). */
void World_Init(void) {
    World_ClearSweep();
}

/* Applies the world's envmap, fog, music and far plane. */
void World_Refresh(void) {
    WorldGameData *gameData = World_GetGameData();
    GXColor color;

    if (gameData != NULL) {
        Gfx_SetEnvmap(gameData->envmapA, gameData->envmapB, 1, gameData->envmapX, gameData->envmapY);
        color = *GETP(gameData->fogColor);
        Gfx_SetFog(&color, 0, gameData->fogNear, gameData->fogFar);
        SongManager_PlaySong();
        Gfx_SetFarClippingPlane(gameData->farPlane);
    } else {
        Gfx_SetFarClippingPlane(0.0f);
    }
}

void World_Unload(void) {
    u32 color[2];

    FileStreaming_ReleaseQuadPacks();
    FileStreaming_ReleaseAllDataPacks(4, 1);
    FileStreaming_ReleaseAllDataPacks(5, 1);
    FileStreaming_ReleaseAllDataPacks(6, 1);
    FileStreaming_ReleaseAllDataPacks(0, 1);
    FileStreaming_ReleaseAllDataPacks(2, 1);
    FileStreaming_ReleaseAllDataPacks(8, 1);
    FileStreaming_ReleaseAllDataPacks(1, 1);
    FileStreaming_ReleaseAllDataPacks(9, 1);
    SETP(g_worldData.world, NULL);
    g_worldData.unk8[0] = 0;
    g_worldData.unk8[1] = 0;
    g_worldData.unk8[2] = 0;
    VolumeManager_Release();
    color[1] = 0;
    color[0] = 0;
    Gfx_SetFog((GXColor *)color, 0, 1.0f, 0.0f);
}

/* Marks the quadrant a camera is in and its neighbours visible. */
static inline void World_ShowQuadrant(World *world, WorldQuad *quad) {
    u32 j;

    quad->visible = 1;
    for (j = 0; j < quad->numNeighbors; j++) {
        GETP(world->quads)[GETP(quad->neighbors)[j]].visible = 1;
    }
}

/* Finds each camera's quadrant (keeping last frame's while the camera stays
 * in it or moves to a neighbour), sets the volume reverb/fog for the
 * cameras, renders the visible loaded quadrants and the world quadrant, and
 * streams in the visible ones not loaded yet (in the loading thread unless a
 * camera had to search all quadrants). */
void World_Render(void) {
    static WorldQuad *pCurrentQuadrant[5];
    static WorldQuad *pWorldQuadrant;
    World *world = WORLD();
    u32 numLoad = 0;
    u32 numActive = 0;
    s8 threaded = 1;
    u32 numCameras;
    u32 i;
    u32 j;
    u32 count;
    WorldQuad *quad;
    WorldQuad *load[20];
    WorldQuad *active[20];
    Vector4 positions[5] __attribute__((aligned(16)));

    if (world == NULL) {
        return;
    }
    if (WORLD_RESET_QUADS) {
        WORLD_RESET_QUADS = 0;
        for (i = 0; i < world->numQuads; i++) {
            if (GETP(GETP(world->quads)[i].bsp) == NULL) {
                pWorldQuadrant = &GETP(world->quads)[i];
                break;
            }
        }
        FileStreaming_LoadQuadPacks((void **)&pWorldQuadrant, 1, 0);
        FileStreaming_SetActiveQuadrants((void **)&pWorldQuadrant, 1);
        pCurrentQuadrant[0] = NULL;
        pCurrentQuadrant[1] = NULL;
        pCurrentQuadrant[2] = NULL;
        pCurrentQuadrant[3] = NULL;
        pCurrentQuadrant[4] = NULL;
    }
    for (i = 0; i < world->numQuads; i++) {
        GETP(world->quads)[i].visible = 0;
    }
    numCameras = Gfx_GetCameraPositions(positions);
    for (i = 0; i < numCameras; i++) {
        VolumeManager_SetReverbLevel(&positions[i]);
        VolumeManager_SetFogLevel(&positions[i]);
        if (pCurrentQuadrant[i] != NULL && BSPN0_CollideWithVector(GETP(pCurrentQuadrant[i]->bsp), &positions[i])) {
            World_ShowQuadrant(world, pCurrentQuadrant[i]);
        } else if (pCurrentQuadrant[i] != NULL) {
            count = pCurrentQuadrant[i]->numNeighbors;
            for (j = 0; j < count; j++) {
                quad = &GETP(world->quads)[GETP(pCurrentQuadrant[i]->neighbors)[j]];
                if (BSPN0_CollideWithVector(GETP(quad->bsp), &positions[i])) {
                    pCurrentQuadrant[i] = quad;
                    break;
                }
            }
            if (j == count) {
                pCurrentQuadrant[i] = NULL;
            } else {
                World_ShowQuadrant(world, pCurrentQuadrant[i]);
            }
        }
        if (pCurrentQuadrant[i] == NULL) {
            threaded = 0;
            for (j = 0; j < world->numQuads; j++) {
                quad = &GETP(world->quads)[j];
                if (quad != pWorldQuadrant && BSPN0_CollideWithVector(GETP(quad->bsp), &positions[i])) {
                    pCurrentQuadrant[i] = quad;
                    break;
                }
            }
            if (pCurrentQuadrant[i] != NULL) {
                World_ShowQuadrant(world, pCurrentQuadrant[i]);
            }
        }
    }
    for (i = 0; i < world->numQuads; i++) {
        quad = &GETP(world->quads)[i];
        if (quad->visible) {
            active[numActive++] = quad;
            if (!quad->loaded) {
                load[numLoad++] = quad;
            } else {
                RenderQuadrant((u8 *)quad);
            }
        }
    }
    FileStreaming_SetActiveQuadrants((void **)active, numActive);
    if (numLoad != 0) {
        FileStreaming_LoadQuadPacks((void **)load, numLoad, threaded);
    }
    if (pWorldQuadrant != NULL && pWorldQuadrant->loaded) {
        RenderQuadrant((u8 *)pWorldQuadrant);
    } else if (pWorldQuadrant != NULL) {
        FileStreaming_LoadQuadPacks((void **)&pWorldQuadrant, 1, 0);
    }
}

u32 World_GetTotalTiles_Car(void) {
    return WORLD()->numTileCars;
}

u32 World_GetTotalTiles_Creature(void) {
    return WORLD()->numTileCreatures;
}

/* The tile accessors return NULL past the end. */
void *World_GetTileCar(u32 index) {
    World *world = WORLD();
    if (index < world->numTileCars) {
        return &GETP(world->tileCars)[index];
    }
    return NULL;
}

void *World_GetTileCreature(u32 index) {
    World *world = WORLD();
    if (index < world->numTileCreatures) {
        return &GETP(world->tileCreatures)[index];
    }
    return NULL;
}

void *World_GetTilePath(u32 index) {
    if (index < WORLD()->numTilePaths) {
        return &GETP(WORLD()->tilePaths)[index];
    }
    return NULL;
}

void *World_GetTileSpeaker(u32 index) {
    if (index < WORLD()->numSpeakers) {
        return &GETP(WORLD()->speakers)[index];
    }
    return NULL;
}

void *World_GetTileLight(u32 index) {
    if (index < WORLD()->numTileLights) {
        return &GETP(WORLD()->tileLights)[index];
    }
    return NULL;
}

void *World_GetVolume(u32 index) {
    World *world = WORLD();
    if (index < world->numVolumes) {
        return &GETP(world->volumes)[index];
    }
    return NULL;
}

void *World_GetNavHolder(void) {
    return GETP(WORLD()->nav);
}

void *World_GetGameData(void) {
    World *world = WORLD();
    if (world != NULL) {
        return GETP(world->gameData);
    }
    return NULL;
}

/* Clears the sweep stamps of the collision polys (mode 0) or edges (mode 1)
 * and restarts the stamp at 1. */
void World_ClearSweep(void) {
    u32 i;
    WorldCollision *collision;

    collision = GETP(WORLD()->collision);
    if (collision->mode == 0) {
        for (i = 0; i < GETP(WORLD()->collision)->numPolys; i++) {
            *(s32 *)(GETP(GETP(WORLD()->collision)->polys) + i * 0x20 + 0x18) = 0;
        }
    } else if (collision->mode == 1) {
        for (i = 0; i < GETP(WORLD()->collision)->numEdges; i++) {
            *(s32 *)(GETP(GETP(WORLD()->collision)->edges) + i * 0x30 + 0x20) = 0;
        }
    }
    WORLD_SWEEP = 1;
}

/* Next sweep stamp; on wrap-around the stamps are cleared. */
void World_NextSweep(void) {
    if (++WORLD_SWEEP == 0) {
        World_ClearSweep();
    }
}

/* Spawns the emitter shown at each teleporter that has one. */
void World_InitTeleporters(void) {
    EmitterParams params __attribute__((aligned(16)));
    u32 i;
    Teleporter *teleporter;

    for (i = 0; i < WORLD()->numTeleporters; i++) {
        teleporter = &GETP(WORLD()->teleporters)[i];
        if (teleporter->emitter != 0) {
            params.emitter.h = teleporter->emitter;
            params.drawDist = 0.0f;
            params.pos = teleporter->mtx.v[3];
            params.vel.x = params.vel.y = params.vel.z = 0.0f;
            SubMatrixToQuaternion(&teleporter->mtx, (Quaternion *)&params.quat);
            params.owner = 0;
            SETP(teleporter->thing, ThingManager_DARYL_CreateThing(THING_TYPE_EMITTER, &params));
        }
    }
}

/* Takes away a QuadItem's spawned item and restarts its respawn timer. */
static inline void World_RemoveItem(QuadItem *item) {
    ItemThing *obj = GETP(item->thing);

    if (obj != NULL) {
        THING_VFN(obj, THING_V_RELEASE, void (*)(ItemThing *))(obj);
        item->timer = item->respawn;
        SETP(item->thing, NULL);
    }
}

/* action: { mode, count, QuadItem handles }: mode 0 turns the spawn points
 * off (removing their items), 1 on (spawning soon), 2 toggles. */
s32 ActionItemChangeMode(u32 *action) {
    u32 i;
    QuadItem *item;

    for (i = 0; i < action[1]; i++) {
        item = (QuadItem *)PTR(action[i + 2]);
        if (action[0] == 2) {
            if (item->mode == QUAD_ITEM_ON) {
                item->mode = QUAD_ITEM_OFF;
                World_RemoveItem(item);
            } else {
                item->mode = QUAD_ITEM_ON;
                item->timer = 1;
            }
        } else {
            item->mode = action[0];
            if (action[0] == QUAD_ITEM_ON) {
                item->timer = 1;
            }
            if (action[0] == QUAD_ITEM_OFF) {
                World_RemoveItem(item);
            }
        }
    }
    return 1;
}

/* Draws a loaded quadrant: its geometry, then its static instances within
 * their draw distance (doubled in split screen) and in view, with their
 * shared slaves. */
void RenderQuadrant(u8 *quadrant) {
    /* A stand-in thing for the instance's transform (0xF0, as the original). */
    typedef struct DrawThing {
        Thing base;
        u8 padAC[0x44];
    } DrawThing;
    WorldQuad *quad = (WorldQuad *)quadrant;
    u32 i;
    u32 j;
    QuadInstance *inst;
    ThingRecord *model;
    DrawThing *t;
    f32 distSq;
    DrawThing thing __attribute__((aligned(16)));

    if (quad == NULL || quad->loaded == 0 || GETP(quad->geoms) == NULL) {
        return;
    }
    for (i = 0; i < quad->numGeoms; i++) {
        Gfx_DrawGeometry(HND(GETP(GETP(quad->geoms)[i].geom)), HND(NULL));
    }
    for (j = 0; j < GETP(quad->contents)->numInstances; j++) {
        inst = &GETP(GETP(quad->contents)->instances)[j];
        model = GETP(inst->record);
        if (THINGRECORD_IS_STATIC(model) == 0) {
            continue;
        }
        t = &thing;
        distSq = Gfx_GetMinCameraDistSquared(inst);
        if ((u32)ViewManager_GetViewFormat() > 1) {
            distSq *= 4.0f;
        }
        if (inst->hasDrawDist) {
            if (distSq > inst->drawDist * inst->drawDist) {
                continue;
            }
        } else if (GETP(inst->record)->hasDrawDist && distSq > GETP(inst->record)->drawDist * GETP(inst->record)->drawDist) {
            continue;
        }
        if (Gfx_FrustrumCullSphereTestAllWindows(inst, inst->radius)) {
            t->base.flags = 0;
            t->base.quat = inst->quat;
            QuaternionToSubMatrix(&t->base.quat, THING_MTX(&t->base));
            inst->pos.w = 1.0f;
            t->base.pos = inst->pos;
            Thing_DrawSharedSlaves((u8 *)t, (u32 *)model->slaves);
            if (model->noDraw == 0) {
                Gfx_DrawGeometry(HND(GETP(model->geometry)), HND(THING_MTX(&t->base)));
            }
        }
    }
}

/* Counts the script timers by the frame time (halved / a tenth with game
 * speed setting 1 / 2), except stopped ones (mode type 7). */
void TimerManager_Heartbeat(void) {
    extern u32 g_iDeltaTimeMS;
    World *world;
    WorldTimer *timer;
    u32 i;
    u8 speed;
    u32 dt;

    world = WORLD();
    if (world == NULL) {
        return;
    }
    speed = GameSettings_GetCodeVariable(0x6C, 7);
    for (i = 0; i < world->numTimers; i++) {
        timer = GETP(GETP(world->timers)[i]);  /* port: a table of pointer slots */
        if (GETP(timer->mode)->type == 7) {
            continue;
        }
        dt = g_iDeltaTimeMS;
        if (speed == 1) {
            dt >>= 1;
        } else if (speed == 2) {
            dt = 0.1f * dt;
        }
        if (timer->countUp == 0) {
            timer->time -= dt;
            if (timer->time < 0.0f) {
                timer->time = 0.0f;
            }
        } else {
            timer->time += dt;
        }
    }
}

/* data: { count, variable slot handles, operand }: operand { OPERAND_*,
 * value words, op }. The op follows the operand, except for a code variable
 * >= 20 where it is read from the arg's word (as in the original). True if a
 * variable compares true. */
s32 TriggerVariableValueCheck(u32 *data) {
    u32 i;
    f32 value;
    s32 op;
    WorldVariable *var;

    switch (data[data[0] + 1]) {
    case OPERAND_CONSTANT:
        op = data[data[0] + 3];
        value = *(f32 *)&data[data[0] + 2];
        break;
    case OPERAND_VARIABLE:
        op = data[data[0] + 3];
        value = WORLD_VARIABLE(data[data[0] + 2])->value;
        break;
    case OPERAND_CODE_VARIABLE:
        value = GetCodeVariable(data[data[0] + 2], data[data[0] + 3]);
        if (data[data[0] + 2] < 20) {
            op = data[data[0] + 4];
        } else {
            op = data[data[0] + 3];
        }
        break;
    }
    for (i = 0; i < data[0]; i++) {
        var = WORLD_VARIABLE(data[i + 1]);
        switch (op) {
        case TRIGGER_OP_LESS:
            if (var->value < value) {
                return 1;
            }
            break;
        case TRIGGER_OP_GREATER:
            if (var->value > value) {
                return 1;
            }
            break;
        case TRIGGER_OP_EQUAL:
            if (var->value == value) {
                return 1;
            }
            break;
        case TRIGGER_OP_GREATER_EQUAL:
            if (var->value >= value) {
                return 1;
            }
            break;
        case TRIGGER_OP_LESS_EQUAL:
            if (var->value <= value) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

/* action: { op (ACTION_OP_*), count, variable slot handles, operand }. */
s32 ActionVariableChangeValue(u32 *action) {
    u32 i;
    f32 value;
    WorldVariable *var;

    switch (action[action[1] + 2]) {
    case OPERAND_CONSTANT:
        value = *(f32 *)&action[action[1] + 3];
        break;
    case OPERAND_VARIABLE:
        value = WORLD_VARIABLE(action[action[1] + 3])->value;
        break;
    case OPERAND_CODE_VARIABLE:
        value = GetCodeVariable(action[action[1] + 3], action[action[1] + 4]);
        break;
    }
    for (i = 0; i < action[1]; i++) {
        var = WORLD_VARIABLE(action[i + 2]);
        switch (action[0]) {
        case ACTION_OP_ADD:
            var->value += value;
            break;
        case ACTION_OP_SUB:
            var->value -= value;
            break;
        case ACTION_OP_MUL:
            var->value *= value;
            break;
        case ACTION_OP_DIV:
            if (value) {
                var->value /= value;
            }
            break;
        case ACTION_OP_SET:
            var->value = value;
            break;
        }
    }
    return 1;
}

/* action: { mode, count, variable slot handles } */
s32 ActionVariableChangeMode(u32 *action) {
    u32 i;
    u8 *mode;

    for (i = 0; i < action[1]; i++) {
        mode = GETP(WORLD_VARIABLE(action[i + 2])->mode);
        if (mode != NULL) {
            *mode = action[0];
        }
    }
    return 1;
}

/* action: { op (ACTION_OP_*), count, timer slot handles, operand } */
s32 ActionTimerChangeValue(u32 *action) {
    u32 i;
    f32 value;
    WorldTimer *timer;

    switch (action[action[1] + 2]) {
    case OPERAND_CONSTANT:
        value = *(f32 *)&action[action[1] + 3];
        break;
    case OPERAND_VARIABLE:
        value = WORLD_VARIABLE(action[action[1] + 3])->value;
        break;
    case OPERAND_CODE_VARIABLE:
        value = GetCodeVariable(action[action[1] + 3], action[action[1] + 4]);
        break;
    }
    for (i = 0; i < action[1]; i++) {
        timer = WORLD_TIMER(action[i + 2]);
        switch (action[0]) {
        case ACTION_OP_ADD:
            timer->time += value;
            break;
        case ACTION_OP_SUB:
            timer->time -= value;
            break;
        case ACTION_OP_MUL:
            timer->time *= value;
            break;
        case ACTION_OP_DIV:
            if (value) {
                timer->time /= value;
            }
            break;
        case ACTION_OP_SET:
            timer->time = value;
            break;
        }
    }
    return 1;
}

/* action: { mode, type, count, timer slot handles } */
s32 ActionTimerChangeMode(u32 *action) {
    u32 i;

    for (i = 0; i < action[2]; i++) {
        GETP(WORLD_TIMER(action[i + 3])->mode)->mode = action[0];
        GETP(WORLD_TIMER(action[i + 3])->mode)->type = action[1];
    }
    return 1;
}

/* As TriggerVariableValueCheck, for timers' times. */
s32 TriggerTimerValueCheck(u32 *data) {
    u32 i;
    f32 value;
    s32 op;
    WorldTimer *timer;

    switch (data[data[0] + 1]) {
    case OPERAND_CONSTANT:
        op = data[data[0] + 3];
        value = *(f32 *)&data[data[0] + 2];
        break;
    case OPERAND_VARIABLE:
        op = data[data[0] + 3];
        value = WORLD_VARIABLE(data[data[0] + 2])->value;
        break;
    case OPERAND_CODE_VARIABLE:
        value = GetCodeVariable(data[data[0] + 2], data[data[0] + 3]);
        if (data[data[0] + 2] < 20) {
            op = data[data[0] + 4];
        } else {
            op = data[data[0] + 3];
        }
        break;
    }
    for (i = 0; i < data[0]; i++) {
        timer = WORLD_TIMER(data[i + 1]);
        switch (op) {
        case TRIGGER_OP_LESS:
            if (timer->time < value) {
                return 1;
            }
            break;
        case TRIGGER_OP_GREATER:
            if (timer->time > value) {
                return 1;
            }
            break;
        case TRIGGER_OP_EQUAL:
            if (timer->time == value) {
                return 1;
            }
            break;
        case TRIGGER_OP_GREATER_EQUAL:
            if (timer->time >= value) {
                return 1;
            }
            break;
        case TRIGGER_OP_LESS_EQUAL:
            if (timer->time <= value) {
                return 1;
            }
            break;
        }
    }
    return 0;
}
