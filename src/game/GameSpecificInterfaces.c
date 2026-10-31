#include "common.h"
#include "Interface.h"
#include "CarA.h"
#include "WildCard.h"

extern VehicleInterfaceData g_vehicleInterfaceData[4];


void GSI_Init(void)
{
    GSI_VehicleItemInit();
}

void GSI_VehicleItemInit(void)
{
    u32 i;

    for (i = 0; i < 4; i++) {
        memset(&g_vehicleInterfaceData[i], 0, sizeof(VehicleInterfaceData));
        g_vehicleInterfaceData[i].iface = -1;
        g_vehicleInterfaceData[i].packIndex = -1;
        g_vehicleInterfaceData[i].packID = -1;
        g_vehicleInterfaceData[i].itemID = -1;
        g_vehicleInterfaceData[i].selection = -1;
        memset(g_vehicleInterfaceData[i].objects, 0xFF, 8);
    }
    PlayerManager_Init();
}

/* Interface info callback of the vehicle select: the shown car's stat bars. */
void GSI_VehicleSelectionInfo(InterfaceHudInfo *info)
{
    u32 i;
    CarAInfo *vehicle;

    for (i = 0; i < 4; i++) {
        if (info->index == g_vehicleInterfaceData[i].iface && GETP(g_vehicleInterfaceData[i].cars[0]) != NULL) {
            vehicle = GETP(GETP(g_vehicleInterfaceData[i].cars[0])->vehicleIface);
            info->carStats[0] = GETP(vehicle->hud)->stats[0];
            info->carStats[1] = GETP(vehicle->hud)->stats[1];
            info->carStats[2] = GETP(vehicle->hud)->stats[2];
            info->carStats[3] = GETP(vehicle->hud)->stats[3];
            return;
        }
    }
}

void GSI_VehicleItemSelection(void *arg) {
    typedef struct InterfaceMsg {
        u32 iface;
        u16 type;
        u16 sub;
        u8 unk8[0x20 - 0x8];
        u32 result;
        u8 unk24[0x30 - 0x24];
        u32 selection;
        u32 objects[2];
        PTR32(u32) list;
        u16 nextState;
    } InterfaceMsg;
    extern Vector4 g_zeroVector;
    static s8 bReleaseThings = 1;
    static s8 bReleaseAllPacks = 1;
    static u32 lockedID = 0xFFFFFFFF;
    static WorldCollision *s_pColGrid;  /* the world's collision, off while the cars are shown */
    InterfaceMsg *msg = arg;
    VehicleInterfaceData *data;
    MCP *mcp;
    CarA *car;
    WildCardRack *rack;
    u8 *pack;
    void *script;
    u32 id;
    u32 i;
    u32 j;
    s8 busy;
    CarAParams params;

    data = NULL;
    if (msg->type != 3) {
        for (i = 0; i < 4; i++) {
            if (msg->iface == g_vehicleInterfaceData[i].iface) {
                data = &g_vehicleInterfaceData[i];
            }
        }
        if (data == NULL && msg->sub != 2) {
            return;
        }
        if (data == NULL) {
            return;
        }
    }
    switch (msg->type) {
    case 3:
        mcp = GameSettings_GetMCP();
        if (msg->sub == 2) {
            for (i = 1; i <= 4; i++) {
                GameSettings_SetCodeVariable(0xCB, i, -1);
            }
            for (i = 0; i < 4; i++) {
                if (g_vehicleInterfaceData[i].iface == -1) {
                    data = &g_vehicleInterfaceData[i];
                    Input_Init();
                    for (j = 0; j < 2; j++) {
                        SETP(data->players[j], PlayerManager_GetHumanPlayerByIndex(0));
                    }
                    data->iface = msg->iface;
                    break;
                }
            }
            bReleaseAllPacks = 1;
            if (data == NULL && msg->sub != 2) {
                return;
            }
            if (data == NULL) {
                return;
            }
            data->active = 1;
            if (mcp != NULL) {
                if (GETP(mcp->sharePack) != NULL) {
                    FileStreaming_LoadDataPack(GETP(mcp->sharePack), 8, 1, 0);
                }
                if (GETP(mcp->carsPack) != NULL) {
                    FileStreaming_LoadDataPack(GETP(mcp->carsPack), 8, 2, 0);
                }
            }
            lockedID = -1;
        } else if (msg->sub == 3) {
            data = &g_vehicleInterfaceData[0];
            if (g_vehicleInterfaceData[0].iface != -1 && g_vehicleInterfaceData[1].iface == -1) {
                data->iface = msg->iface;
            } else {
                if (g_vehicleInterfaceData[0].iface != -1) {
                    return;
                }
                data = &g_vehicleInterfaceData[1];
                if (g_vehicleInterfaceData[1].iface == -1) {
                    return;
                }
                data->iface = msg->iface;
            }
            data->active = 0;
            for (i = 0; i < 2; i++) {
                if (data->objects[i] != 0xFF && GETP(data->cars[i]) != NULL) {
                    InterfaceManager_LinkThingToObject(msg->iface, data->objects[i], GETP(data->cars[i]));
                }
            }
            bReleaseAllPacks = 1;
            bReleaseThings = 1;
            if (mcp != NULL && GETP(mcp->itemsPack) != NULL) {
                FileStreaming_LoadDataPack(GETP(mcp->itemsPack), 8, 3, 0);
            }
            lockedID = -1;
        } else {
            return;
        }
        Input_SetForceFeedbackActive(0, GameSettings_GetControllerSettings(1)->data[0]);
        if (GETP(g_worldData.world) != NULL) {
            s_pColGrid = GETP(((World *)GETP(g_worldData.world))->collision);
            SETP(((World *)GETP(g_worldData.world))->collision, NULL);
        } else {
            s_pColGrid = NULL;
        }
        Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
        Gfx_SetProjectionMatrix(90.0f);
        CameraManager_ResetAllCameras();
        CameraManager_Pause(1);
        if (mcp != NULL) {
            Gfx_SetEnvmap(mcp->envmapX, mcp->envmapY, 1, 100.0f, 100.0f);
            Gfx_SetEnviroMapMode(1);
        }
        InterfaceManager_SetInfoCallBackRoutine(msg->iface, GSI_VehicleSelectionInfo);
        break;
    case 5:
        if (!bReleaseThings) {
            break;
        }
        if (bReleaseAllPacks) {
            for (i = 0; i < 2; i++) {
                if (GETP(data->cars[i]) != NULL) {
                    WildCardRack_JettisonAll(ThingManagerList_GetByType(&GETP(data->cars[i])->motionState.slaves, THING_TYPE_WILDCARD_RACK));
                    CarA_Release(GETP(data->cars[i]));
                }
            }
            memset(data, 0, sizeof(VehicleInterfaceData));
            memset(data->objects, 0xFF, 8);
        } else {
            for (i = 0; i < 2; i++) {
                if (GETP(data->cars[i]) != NULL) {
                    WildCardRack_JettisonAll(ThingManagerList_GetByType(&GETP(data->cars[i])->motionState.slaves, THING_TYPE_WILDCARD_RACK));
                    CarA_Release(GETP(data->cars[i]));
                    SETP(data->cars[i], NULL);
                }
            }
        }
        CameraManager_Pause(0);
        data->iface = -1;
        data->selection = -1;
        data->itemID = -1;
        ParticleManager_Init();
        if (bReleaseAllPacks) {
            data->packIndex = -1;
            data->packID = -1;
            FileStreaming_ReleaseAllDataPacks(0, 0);
            FileStreaming_ReleaseAllDataPacks(2, 0);
            FileStreaming_ReleaseAllDataPacks(8, 0);
        }
        if (GETP(g_worldData.world) != NULL) {
            SETP(((World *)GETP(g_worldData.world))->collision, s_pColGrid);
        }
        break;
    case 0:
        switch (msg->sub) {
        case 100:
            for (i = 0; i < 2; i++) {
                if (lockedID != -1) {
                    data->locked = 1;
                    break;
                }
                if (GETP(data->cars[i]) != NULL) {
                    rack = ThingManagerList_GetByType(&GETP(data->cars[i])->motionState.slaves, THING_TYPE_WILDCARD_RACK);
                    if (rack != NULL) {
                        /* the demo fire: full ammo and health, fire held */
                        rack->slots[rack->cur].ammo = 10000;
                        GETP(data->cars[i])->base.health = 10000;
                        ((Player *)GETP(GETP(data->cars[i])->base.player))->buttonFire = 0xFF;
                    }
                }
            }
            break;
        }
        break;
    case 12:
        for (i = 0; i < 2; i++) {
            car = GETP(data->cars[i]);
            if (car != NULL) {
                if (!data->locked) {
                    Control_ClearControl(GETP(car->base.player));
                }
                car->unk460 = 0;
                car->gradeOrReverse = 10.0f;
                GETP(data->cars[i])->base.vel.x = 0.0f;
                GETP(data->cars[i])->base.vel.y = 0.0f;
                GETP(data->cars[i])->base.vel.z = 0.0f;
                car->forceAccum = g_zeroVector;
                car->angularMomentum = g_zeroVector;
                car->localAxis = g_zeroVector;
            }
        }
        data->locked = 0;
        break;
    case 13:
        id = GETP(msg->list)[msg->selection];
        if (IsLoadingThreadRunning()) {
            break;
        }
        data->selection = msg->selection;
        if (id != data->packID || (GETP(data->cars[0]) == NULL && lockedID == -1)) {
            for (i = 0; i < 2; i++) {
                if (GETP(data->cars[i]) != NULL) {
                    rack = ThingManagerList_GetByType(&GETP(data->cars[i])->motionState.slaves, THING_TYPE_WILDCARD_RACK);
                    if (rack != NULL) {
                        WildCardRack_JettisonAll(rack);
                    }
                    CarA_Release(GETP(data->cars[i]));
                    SETP(data->cars[i], NULL);
                    if (msg->objects[i] != 0xFF) {
                        InterfaceManager_LinkThingToObject(msg->iface, msg->objects[i], NULL);
                    }
                }
            }
            if (IsLoadingThreadRunning()) {
                break;
            }
            if (lockedID != -1 && lockedID != data->iface) {
                break;
            }
            lockedID = data->iface;
            ParticleManager_Init();
            if (data->packIndex != -1 && id != data->packID) {
                FileStreaming_ReleaseDataPack(data->packIndex, 0);
                data->packIndex = -1;
                break;
            }
            mcp = GameSettings_GetMCP();
            if (mcp == NULL || id == data->packID) {
                break;
            }
            FileStreaming_InitializeDataPack(FileStreaming_GetPackID(GETP(mcp->carsPack), 8, 2), 8, 1);
            DataPack_EnableLoadingSounds(0);
            data->packIndex = FileStreaming_LoadDataPack(GETP(mcp->carsPack), 0, id, 1);
            if (data->packIndex != -1) {
                data->packID = id;
            }
            break;
        }
        if (IsLoadingThreadRunning() || (lockedID != -1 && lockedID != data->iface)) {
            if (!IsLoadingThreadRunning()) {
                lockedID = -1;
            }
            break;
        }
        j = 0;
        if (msg->objects[0] == 0xFF) {
            break;
        }
        if (GETP(data->cars[0]) == NULL) {
            pack = FileStreaming_InitializeDataPack(data->packIndex, 0, 1);
            if (pack == NULL) {
                lockedID = -1;
                break;
            }
            DataPack_EnableLoadingSounds(1);
            script = DataPack_GetScript(pack, data->packID);
            if (script == NULL) {
                break;
            }
            params.pos.x = 0.0f;
            params.pos.y = -7.0f;
            params.pos.z = 30.0f;
            SETP(params.player, GETP(data->players[0]));
            params.quat.x = 0.0f;
            params.quat.y = 0.0f;
            params.quat.z = 0.0f;
            params.quat.w = 1.0f;
            SETP(params.info, script);
            params.vel.x = 0.0f;
            params.vel.y = 0.0f;
            params.vel.z = 0.0f;
            /* the show cars sit lower on their springs */
            for (i = 0; i < ((CarAInfo *)script)->numWheels; i++) {
                GETP(((CarAInfo *)script)->wheels)[i].restLength = 0.3f;
            }
            SETP(data->cars[0], ThingManager_DARYL_CreateThing(THING_TYPE_CAR, &params));
            GETP(data->cars[0])->base.trigger.h = 0;
            data->objects[0] = msg->objects[0];
            Player_SetThing(GETP(data->players[0]), GETP(data->cars[0]));
            GETP(data->players[0])->tileCar.h = 0;
            GETP(data->cars[0])->base.flags |= THING_FLAG_NO_TERRAIN_HITS | THING_FLAG_NO_OBJECT_HITS | THING_FLAG_HIDDEN;
            if (j == 1) {
                GETP(data->cars[0])->base.flags |= THING_FLAG_DEAD;
            }
        } else {
            InterfaceManager_LinkThingToObject(msg->iface, msg->objects[0], GETP(data->cars[0]));
            lockedID = -1;
        }
        break;
    case 14:
        id = GETP(msg->list)[msg->selection];
        if (IsLoadingThreadRunning()) {
            break;
        }
        if (GameSettings_GetGameType() == 0 && id == 3 && GameSettings_IsCheatEnabled(8)) {
            id = 0x4D;
        }
        if (id != data->itemID) {
            if (lockedID != -1 && lockedID != data->iface) {
                break;
            }
            lockedID = data->iface;
            mcp = GameSettings_GetMCP();
            if (mcp != NULL) {
                data->itemPack = FileStreaming_LoadDataPack(GETP(mcp->itemsPack), 2, id, 0);
                if (data->itemPack != -1) {
                    data->itemID = id;
                }
            }
            Control_ClearControl(GETP(GETP(data->cars[0])->base.player));
            data->locked = 0;
        }
        if (lockedID == -1 || lockedID != data->iface) {
            break;
        }
        lockedID = -1;
        pack = FileStreaming_InitializeDataPack(data->itemPack, 2, 1);
        if (pack == NULL) {
            break;
        }
        script = DataPack_GetScript(pack, data->itemID);
        if (script == NULL || GETP(data->cars[0]) == NULL) {
            break;
        }
        rack = ThingManagerList_GetByType(&GETP(data->cars[0])->motionState.slaves, THING_TYPE_WILDCARD_RACK);
        if (rack == NULL) {
            break;
        }
        WildCardRack_JettisonAll(rack);
        WildCardRack_Add(rack, script, GETP(((ItemRecord *)script)->weapon)->ammo);
        break;
    case 15:
        busy = 0;
        for (i = 0; i < 2 && !busy; i++) {
            if (GETP(data->cars[i]) != NULL || data->packID == -1) {
                busy = 1;
            }
        }
        if (!busy || IsLoadingThreadRunning() ||
            (data->selection != -1 && GETP(msg->list)[data->selection] != data->packID && data->active)) {
            msg->result = 0xB;
        }
        if ((msg->nextState == 3 || msg->nextState == 8) && msg->result != 0xB) {
            bReleaseAllPacks = 0;
        }
        if (msg->nextState == 3 && msg->result != 0xB) {
            bReleaseThings = 0;
        }
        ParticleManager_Init();
        break;
    }
}
