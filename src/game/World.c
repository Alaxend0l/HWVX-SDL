#include "common.h"



s32 World_Create(void) {
    extern s32 m_VertIn;
    extern s32 g_drawShadowsFlag;
    m_VertIn = 0;
    g_drawShadowsFlag = 1;
    SETP(g_worldData.world, NULL);
    return 1;
}

void World_Destroy(void) {
    if (GETP(g_worldData.world) != NULL) {
        World_Unload();
    }
}

/* The decomp has an m2c placeholder here; the original calls World_ClearSweep (asm). */
void World_Init(void) {
    World_ClearSweep();
}

void World_Refresh(void) {
    typedef struct WorldGameData {
        u8 unk0[0xC];
        f32 envmapX;
        f32 envmapY;
        f32 fogStart;
        f32 fogEnd;
        u8 unk1C[0x4];
        f32 farPlane;
        u8 unk24[0x8C - 0x24];
        u32 envmapA;
        u32 envmapB;
        u8 unk94[0xC0 - 0x94];
        PTR32(GXColor) fogColor;
    } WorldGameData;
    WorldGameData *gameData = World_GetGameData();
    GXColor color;

    if (gameData != NULL) {
        Gfx_SetEnvmap(gameData->envmapA, gameData->envmapB, 1, gameData->envmapX, gameData->envmapY);
        color = *GETP(gameData->fogColor);
        Gfx_SetFog(&color, 0, gameData->fogStart, gameData->fogEnd);
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

void World_Render(void) {
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
    static Quad *pCurrentQuadrant[5];
    static Quad *pWorldQuadrant;
    u32 numLoad = 0;
    u32 numActive = 0;
    s8 threaded = 1;
    u32 numCameras;
    u32 i;
    u32 j;
    u32 count;
    Quad *quad;
    Quad *load[20];
    Quad *active[20];
    Vector4 positions[5] __attribute__((aligned(16)));

    if (GETP(g_worldData.world) == NULL) {
        return;
    }
    if (*(s8 *)&g_worldData.unk8[3]) {
        *(s8 *)&g_worldData.unk8[3] = 0;
        for (i = 0; i < ((QuadWorld *)GETP(g_worldData.world))->numQuads; i++) {
            if (GETP(GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i].bsp) == NULL) {
                pWorldQuadrant = &GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i];
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
    for (i = 0; i < ((QuadWorld *)GETP(g_worldData.world))->numQuads; i++) {
        GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i].visible = 0;
    }
    numCameras = Gfx_GetCameraPositions(positions);
    for (i = 0; i < numCameras; i++) {
        VolumeManager_SetReverbLevel(&positions[i]);
        VolumeManager_SetFogLevel(&positions[i]);
        if (pCurrentQuadrant[i] != NULL && BSPN0_CollideWithVector(GETP(pCurrentQuadrant[i]->bsp), &positions[i])) {
            pCurrentQuadrant[i]->visible = 1;
            for (j = 0; j < pCurrentQuadrant[i]->numNeighbors; j++) {
                GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[GETP(pCurrentQuadrant[i]->neighbors)[j]].visible = 1;
            }
        } else if (pCurrentQuadrant[i] != NULL) {
            count = pCurrentQuadrant[i]->numNeighbors;
            for (j = 0; j < count; j++) {
                quad = &GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[GETP(pCurrentQuadrant[i]->neighbors)[j]];
                if (BSPN0_CollideWithVector(GETP(quad->bsp), &positions[i])) {
                    pCurrentQuadrant[i] = quad;
                    break;
                }
            }
            if (j == count) {
                pCurrentQuadrant[i] = NULL;
            } else {
                pCurrentQuadrant[i]->visible = 1;
                for (j = 0; j < pCurrentQuadrant[i]->numNeighbors; j++) {
                    GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[GETP(pCurrentQuadrant[i]->neighbors)[j]].visible = 1;
                }
            }
        }
        if (pCurrentQuadrant[i] == NULL) {
            threaded = 0;
            for (j = 0; j < ((QuadWorld *)GETP(g_worldData.world))->numQuads; j++) {
                quad = &GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[j];
                if (quad != pWorldQuadrant && BSPN0_CollideWithVector(GETP(quad->bsp), &positions[i])) {
                    pCurrentQuadrant[i] = quad;
                    break;
                }
            }
            if (pCurrentQuadrant[i] != NULL) {
                pCurrentQuadrant[i]->visible = 1;
                for (j = 0; j < pCurrentQuadrant[i]->numNeighbors; j++) {
                    GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[GETP(pCurrentQuadrant[i]->neighbors)[j]].visible = 1;
                }
            }
        }
    }
    for (i = 0; i < ((QuadWorld *)GETP(g_worldData.world))->numQuads; i++) {
        if (GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i].visible) {
            active[numActive++] = &GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i];
            if (!GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i].loaded) {
                load[numLoad++] = &GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i];
            } else {
                RenderQuadrant(&GETP(((QuadWorld *)GETP(g_worldData.world))->quads)[i]);
            }
        }
    }
    FileStreaming_SetActiveQuadrants((void **)active, numActive);
    if (numLoad != 0) {
        FileStreaming_LoadQuadPacks((void **)load, numLoad, threaded);
    }
    if (pWorldQuadrant != NULL && pWorldQuadrant->loaded) {
        RenderQuadrant(pWorldQuadrant);
    } else if (pWorldQuadrant != NULL) {
        FileStreaming_LoadQuadPacks((void **)&pWorldQuadrant, 1, 0);
    }
}

u32 World_GetTotalTiles_Car(void) {
    return M2C_FIELD(GETP(g_worldData.world), u32 *, 0x8);
}

u32 World_GetTotalTiles_Creature(void) {
    return M2C_FIELD(GETP(g_worldData.world), u32 *, 0x40);
}

void *World_GetTileCar(u32 index) {
    void *world = GETP(g_worldData.world);
    if (index < M2C_FIELD(world, u32 *, 0x8)) {
        return SLOT_LOAD(u8 *, (u8 *)(world) + 0xC) + index * 0x60;
    }
    return NULL;
}

void *World_GetTileCreature(u32 index) {
    void *world = GETP(g_worldData.world);
    if (index < M2C_FIELD(world, u32 *, 0x40)) {
        return SLOT_LOAD(u8 *, (u8 *)(world) + 0x44) + index * 0x40;
    }
    return NULL;
}

void *World_GetTilePath(u32 index) {
    if (index < M2C_FIELD(GETP(g_worldData.world), u32 *, 0x10)) {
        return SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x14) + index * 0x20;
    }
    return NULL;
}

void *World_GetTileSpeaker(u32 index) {
    if (index < M2C_FIELD(GETP(g_worldData.world), u32 *, 0x28)) {
        return SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x2C) + index * 0x40;
    }
    return NULL;
}

void *World_GetTileLight(u32 index) {
    if (index < M2C_FIELD(GETP(g_worldData.world), u32 *, 0x38)) {
        return SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x3C) + index * 0x20;
    }
    return NULL;
}

void *World_GetVolume(u32 index) {
    void *world = GETP(g_worldData.world);
    if (index < M2C_FIELD(world, u32 *, 0x68)) {
        return SLOT_LOAD(u8 *, (u8 *)(world) + 0x6C) + index * 0x30;
    }
    return NULL;
}

void *World_GetNavHolder(void) {
    return SLOT_LOAD(void *, (u8 *)(GETP(g_worldData.world)) + 0x78);
}

void *World_GetGameData(void) {
    void *world = GETP(g_worldData.world);
    if (world != NULL) {
        return SLOT_LOAD(void *, (u8 *)(world) + 0x9C);
    }
    return NULL;
}

void World_ClearSweep(void) {
    u32 i;
    void *data;

    data = SLOT_LOAD(void *, (u8 *)(GETP(g_worldData.world)) + 0x0);
    if (M2C_FIELD(data, u32 *, 0x58) == 0) {
        for (i = 0; i < M2C_FIELD(SLOT_LOAD(void *, (u8 *)(GETP(g_worldData.world)) + 0x0), u32 *, 0x48); i++) {
            M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(void *, (u8 *)(GETP(g_worldData.world)) + 0x0)) + 0x4C) + i * 0x20, s32 *, 0x18) = 0;
        }
    } else if (M2C_FIELD(data, u32 *, 0x58) == 1) {
        for (i = 0; i < M2C_FIELD(SLOT_LOAD(void *, (u8 *)(GETP(g_worldData.world)) + 0x0), u32 *, 0x50); i++) {
            M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(void *, (u8 *)(GETP(g_worldData.world)) + 0x0)) + 0x54) + i * 0x30, s32 *, 0x20) = 0;
        }
    }
    g_worldData.unk4 = 1;
}

void World_NextSweep(void) {
    if (++g_worldData.unk4 == 0) {
        World_ClearSweep();
    }
}

void World_InitTeleporters(void) {
    typedef struct Teleporter {
        Matrix4 mtx;
        u8 unk40[0x8];
        u32 model;
        u8 unk4C[0x14];
        PTR32(void) thing;
        u8 unk64[0xC];
    } Teleporter;
    typedef struct TeleporterWorld {
        u8 unk0[0x30];
        u32 numTeleporters;
        PTR32(Teleporter) teleporters;
    } TeleporterWorld;
    struct {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        u32 model;
        s32 unk34;
        f32 unk38;
    } params __attribute__((aligned(16)));
    u32 i;
    Teleporter *teleporter;

    for (i = 0; i < ((TeleporterWorld *)GETP(g_worldData.world))->numTeleporters; i++) {
        teleporter = &GETP(((TeleporterWorld *)GETP(g_worldData.world))->teleporters)[i];
        if (teleporter->model != 0) {
            params.model = teleporter->model;
            params.unk38 = 0.0f;
            params.pos = teleporter->mtx.v[3];
            params.vel.x = params.vel.y = params.vel.z = 0.0f;
            SubMatrixToQuaternion(teleporter, &params.quat);
            params.unk34 = 0;
            SETP(teleporter->thing, ThingManager_DARYL_CreateThing(2, &params));
        }
    }
}

s32 ActionItemChangeMode(u32 *action) {
    u32 i;
    u8 *item;
    u8 *obj;

    for (i = 0; i < action[1]; i++) {
        item = (u8 *)PTR(action[i + 2]);
        if (action[0] == 2) {
            if (*(u32 *)(item + 0x28) == 1) {
                *(u32 *)(item + 0x28) = 0;
                obj = SLOT_LOAD(u8 *, (item + 0x24));
                if (obj != NULL) {
                    (((void (*)(void *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (obj + 0x8C)) + 4))))(obj);
                    *(u32 *)(item + 0x18) = *(u32 *)(item + 0x14);
                    SLOT_STORE((item + 0x24), NULL);
                }
            } else {
                *(u32 *)(item + 0x28) = 1;
                *(u32 *)(item + 0x18) = 1;
            }
        } else {
            *(u32 *)(item + 0x28) = action[0];
            if (action[0] == 1) {
                *(u32 *)(item + 0x18) = 1;
            }
            if (action[0] == 0) {
                obj = SLOT_LOAD(u8 *, (item + 0x24));
                if (obj != NULL) {
                    (((void (*)(void *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (obj + 0x8C)) + 4))))(obj);
                    *(u32 *)(item + 0x18) = *(u32 *)(item + 0x14);
                    SLOT_STORE((item + 0x24), NULL);
                }
            }
        }
    }
    return 1;
}

void RenderQuadrant(u8 *quadrant) {
    typedef struct { PTR32(void) pad0; PTR32(void) pad4; PTR32(void) geom; } StaticGeom;
    typedef struct { PTR32(u8) geom; u8 noDraw; u8 pad5; u8 hasDist; u8 visible; f32 dist; u8 padC[0xC]; u32 slaves[2]; } Model;
    typedef struct { Vector4 pos; Vector4 quat; u8 pad20[4]; PTR32(Model) model; f32 dist; u8 pad2C[8]; u8 useDist; u8 pad35[7]; f32 radius; } Instance;
    typedef struct { u32 pad0; u32 count; PTR32(Instance) instances; } InstanceList;
    typedef struct { u8 pad0[0x10]; f32 mtx[3][4]; Vector4 pos; Vector4 quat; u8 pad60[0x14]; u32 unk74; u8 pad78[0x78]; } DrawThing;
    u32 i;
    u32 j;
    Instance *inst;
    Model *model;
    DrawThing *t;
    f32 distSq;
    DrawThing thing __attribute__((aligned(16)));

    if (quadrant == NULL || *(s8 *)(quadrant + 0x34) == 0 || SLOT_LOAD(StaticGeom *, (quadrant + 0x10)) == NULL) {
        return;
    }
    for (i = 0; i < *(u32 *)(quadrant + 0xC); i++) {
        Gfx_DrawGeometry(HND(GETP((SLOT_LOAD(StaticGeom *, (quadrant + 0x10)))[i].geom)), HND(NULL));
    }
    for (j = 0; j < (SLOT_LOAD(InstanceList *, (quadrant + 0x30)))->count; j++) {
        inst = &GETP((SLOT_LOAD(InstanceList *, (quadrant + 0x30)))->instances)[j];
        model = GETP(inst->model);
        if (model->visible == 0) {
            continue;
        }
        t = &thing;
        distSq = Gfx_GetMinCameraDistSquared(inst);
        if ((u32)ViewManager_GetViewFormat() > 1) {
            distSq *= 4.0f;
        }
        if (inst->useDist) {
            if (distSq > inst->dist * inst->dist) {
                continue;
            }
        } else if (GETP(inst->model)->hasDist && distSq > GETP(inst->model)->dist * GETP(inst->model)->dist) {
            continue;
        }
        if (Gfx_FrustrumCullSphereTestAllWindows(inst, inst->radius)) {
            t->unk74 = 0;
            t->quat = inst->quat;
            QuaternionToSubMatrix(&t->quat, t->mtx);
            inst->pos.w = 1.0f;
            t->pos = inst->pos;
            Thing_DrawSharedSlaves(t, model->slaves);
            if (model->noDraw == 0) {
                Gfx_DrawGeometry(HND(GETP(model->geom)), HND(t->mtx));
            }
        }
    }
}

void TimerManager_Heartbeat(void) {
    extern u32 g_iDeltaTimeMS;
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
    TimerWorld *world;
    Timer *timer;
    u32 i;
    u8 speed;
    u32 dt;

    world = GETP(g_worldData.world);
    if (world == NULL) {
        return;
    }
    speed = GameSettings_GetCodeVariable(0x6C, 7);
    for (i = 0; i < world->numTimers; i++) {
        timer = GETP(GETP(world->timers)[i]);  /* port: a table of pointer slots */
        if (GETP(timer->def)->type == 7) {
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

s32 TriggerVariableValueCheck(u32 *data) {
    u32 i;
    f32 value;
    s32 op;
    u8 **var;

    switch (data[data[0] + 1]) {
    case 0:
        op = data[data[0] + 3];
        value = *(f32 *)&data[data[0] + 2];
        break;
    case 1:
        op = data[data[0] + 3];
        value = *(f32 *)(SLOT_LOAD(u8 *, data[data[0] + 2]) + 4);
        break;
    case 2:
        value = GetCodeVariable(data[data[0] + 2], (u8 *)PTR(data[data[0] + 3]));
        if (data[data[0] + 2] < 20) {
            op = data[data[0] + 4];
        } else {
            op = data[data[0] + 3];
        }
        break;
    }
    for (i = 0; i < data[0]; i++) {
        var = (u8 **)PTR(data[i + 1]);
        switch (op) {
        case 20:
            if (*(f32 *)(SLOT_LOAD(u8 *, var) + 4) < value) {
                return 1;
            }
            break;
        case 21:
            if (*(f32 *)(SLOT_LOAD(u8 *, var) + 4) > value) {
                return 1;
            }
            break;
        case 22:
            if (*(f32 *)(SLOT_LOAD(u8 *, var) + 4) == value) {
                return 1;
            }
            break;
        case 47:
            if (*(f32 *)(SLOT_LOAD(u8 *, var) + 4) >= value) {
                return 1;
            }
            break;
        case 48:
            if (*(f32 *)(SLOT_LOAD(u8 *, var) + 4) <= value) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

s32 ActionVariableChangeValue(u32 *action) {
    u32 i;
    f32 value;
    u8 **var;

    switch (action[action[1] + 2]) {
    case 0:
        value = *(f32 *)&action[action[1] + 3];
        break;
    case 1:
        value = *(f32 *)(SLOT_LOAD(u8 *, action[action[1] + 3]) + 4);
        break;
    case 2:
        value = GetCodeVariable(action[action[1] + 3], (u8 *)PTR(action[action[1] + 4]));
        break;
    }
    for (i = 0; i < action[1]; i++) {
        var = (u8 **)PTR(action[i + 2]);
        switch (action[0]) {
        case 0xF:
            *(f32 *)(SLOT_LOAD(u8 *, var) + 4) += value;
            break;
        case 0x10:
            *(f32 *)(SLOT_LOAD(u8 *, var) + 4) -= value;
            break;
        case 0x2D:
            *(f32 *)(SLOT_LOAD(u8 *, var) + 4) *= value;
            break;
        case 0x2E:
            if (value) {
                *(f32 *)(SLOT_LOAD(u8 *, var) + 4) /= value;
            }
            break;
        case 0x11:
            *(f32 *)(SLOT_LOAD(u8 *, var) + 4) = value;
            break;
        }
    }
    return 1;
}

s32 ActionVariableChangeMode(u32 *action) {
    u32 i;
    u8 *var;

    for (i = 0; i < action[1]; i++) {
        var = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(void *, action[i + 2])) + 0xC);
        if (var != NULL) {
            *var = action[0];
        }
    }
    return 1;
}

s32 ActionTimerChangeValue(u32 *action) {
    u32 i;
    f32 value;
    u8 **timer;

    switch (action[action[1] + 2]) {
    case 0:
        value = *(f32 *)&action[action[1] + 3];
        break;
    case 1:
        value = *(f32 *)(SLOT_LOAD(u8 *, action[action[1] + 3]) + 4);
        break;
    case 2:
        value = GetCodeVariable(action[action[1] + 3], (u8 *)PTR(action[action[1] + 4]));
        break;
    }
    for (i = 0; i < action[1]; i++) {
        timer = (u8 **)PTR(action[i + 2]);
        switch (action[0]) {
        case 0xF:
            *(f32 *)(SLOT_LOAD(u8 *, timer) + 8) += value;
            break;
        case 0x10:
            *(f32 *)(SLOT_LOAD(u8 *, timer) + 8) -= value;
            break;
        case 0x2D:
            *(f32 *)(SLOT_LOAD(u8 *, timer) + 8) *= value;
            break;
        case 0x2E:
            if (value) {
                *(f32 *)(SLOT_LOAD(u8 *, timer) + 8) /= value;
            }
            break;
        case 0x11:
            *(f32 *)(SLOT_LOAD(u8 *, timer) + 8) = value;
            break;
        }
    }
    return 1;
}

s32 ActionTimerChangeMode(u32 *action) {
    u8 **item;
    u32 i;

    for (i = 0; i < action[2]; i++) {
        item = (u8 **)PTR(action[i + 3]);
        M2C_FIELD(SLOT_LOAD(u8 *, SLOT_LOAD(u8 *, item) + 0xC), u8 *, 0) = action[0];
        M2C_FIELD(SLOT_LOAD(u8 *, SLOT_LOAD(u8 *, item) + 0xC), u8 *, 1) = action[1];
    }
    return 1;
}

s32 TriggerTimerValueCheck(u32 *data) {
    u32 i;
    f32 value;
    s32 op;
    u8 **timer;

    switch (data[data[0] + 1]) {
    case 0:
        op = data[data[0] + 3];
        value = *(f32 *)&data[data[0] + 2];
        break;
    case 1:
        op = data[data[0] + 3];
        value = *(f32 *)(SLOT_LOAD(u8 *, data[data[0] + 2]) + 4);
        break;
    case 2:
        value = GetCodeVariable(data[data[0] + 2], (u8 *)PTR(data[data[0] + 3]));
        if (data[data[0] + 2] < 20) {
            op = data[data[0] + 4];
        } else {
            op = data[data[0] + 3];
        }
        break;
    }
    for (i = 0; i < data[0]; i++) {
        timer = (u8 **)PTR(data[i + 1]);
        switch (op) {
        case 20:
            if (*(f32 *)(SLOT_LOAD(u8 *, timer) + 8) < value) {
                return 1;
            }
            break;
        case 21:
            if (*(f32 *)(SLOT_LOAD(u8 *, timer) + 8) > value) {
                return 1;
            }
            break;
        case 22:
            if (*(f32 *)(SLOT_LOAD(u8 *, timer) + 8) == value) {
                return 1;
            }
            break;
        case 47:
            if (*(f32 *)(SLOT_LOAD(u8 *, timer) + 8) >= value) {
                return 1;
            }
            break;
        case 48:
            if (*(f32 *)(SLOT_LOAD(u8 *, timer) + 8) <= value) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

