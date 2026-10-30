#include "common.h"

s32 ActionCameraSetTargets(u32 *mode) {
    CameraManager_SetCameraGlobalMode(mode);
    return 1;
}

s32 ActionRoundComplete(s32 *arg0) {
    GameRules_ForceRoundEnd(*arg0);
    return 1;
}

s32 ActionResetGameRound(void *arg0) {
    GameRules_ResetRound((s8) (u8) M2C_FIELD(arg0, s32 *, 0), (s8) (u8) M2C_FIELD(arg0, s32 *, 4), (s8) (u8) M2C_FIELD(arg0, s32 *, 8));
    return 1;
}

s32 ActionSetPlayerOrder(void *action) {
    GameRules_SetPlayerOrder(action, 4);
    return 1;
}

s32 ActionSpeakerChangeMode(void) {
    return 1;
}

s32 ActionVehicleAISetNamedPath(void) {
    return 1;
}

s32 ActionSetPlayerVehicle(void) {
    return 1;
}

s32 ActionVehicleRemoveCreature(void) {
    return 1;
}

s32 ActionScriptLightChangeMode(void) {
    return 1;
}

void Action_Create(void) {
    M2C_UNK fn_800957D4();
    fn_800957D4();
}

void Action_Destroy(void) {

}

void Action_Init(void) {
    extern void *m_Actions_800DBC80[80];
    u32 i;

    for (i = 0; i < 80; i++) {
        m_Actions_800DBC80[i] = NULL;
    }
}

void Action_Heartbeat(void) {
    typedef struct Action {
        s32 type;
        PTR32(void) data;
        s32 unk8;
        s32 unkC;
        s32 unk10;
        s32 delay;
    } Action;
    extern Action *m_Actions_800DBC80[80];
    extern void (*pActionFuncArray_800CF500[])(void *);
    extern s32 g_iDeltaTimeMS;
    u32 i;

    for (i = 0; i < 80; i++) {
        Action *action = m_Actions_800DBC80[i];
        if (action != NULL && action->delay <= 0) {
            pActionFuncArray_800CF500[action->type](GETP(action->data));
            if (m_Actions_800DBC80[i]->type != 0x14 || SLOT_LOAD(void *, (u32 *)(GETP(m_Actions_800DBC80[i]->data)) + (1)) == NULL) {
                m_Actions_800DBC80[i] = NULL;
            }
        } else if (action != NULL) {
            action->delay -= g_iDeltaTimeMS;
        }
    }
}

void Action_ReleaseAllActions(void) {
    extern u8 m_Actions_800DBC80[];
    memset(m_Actions_800DBC80, 0, 0x140);
}

s32 Action_AddAction(void *arg0) {
    typedef struct Action {
        s32 type;
        PTR32(void) data;
        s32 unk8;
        s32 delayInit;
        u32 count;
        s32 delay;
    } Action;
    extern Action *m_Actions_800DBC80[80];
    Action *action = arg0;
    u32 i;

    for (i = 0; i < 80; i++) {
        if (m_Actions_800DBC80[i] == NULL) {
            break;
        }
    }
    if (i >= 80) {
        Dprintf("The max number of actions has been exceeded for this point in the game.  There can only be %d simaltaneous actions\n", 80);
        return 1;
    }
    if (action->count != 0xFFFFFFFF) {
        action->count--;
    }
    m_Actions_800DBC80[i] = action;
    m_Actions_800DBC80[i]->delay = m_Actions_800DBC80[i]->delayInit;
    if (action->count != 0) {
        return 1;
    }
    return 0;
}


s32 ActionPrintDebugString(u32 *action)
{
    u32 i;
    u8 *obj;

    if (*action == 0)
    {
        if (action[1] == 0)
        {
            action[1] = 0x3c;
        }
        action[1] -= 1;
    }
    else
    {
        action[1] = 0;
        for (i = 0; i < M2C_FIELD(GETP(g_worldData.world), u32 *, 8); i++)
        {
            obj = (u8 *)((char *)SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0xC) + i * 0x60);
            if (M2C_FIELD(obj, u8 *, 0x35) == 1)
            {
                switch (*action)
                {
                case 0:
                    break;
                case 1:
                    M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(obj) + 0x28), u32 *, 0x498) = action[2];
                    break;
                case 2:
                    M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(obj) + 0x28), u32 *, 0x494) = action[2];
                    break;
                case 3:
                    M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(obj) + 0x28), u32 *, 0x494) = action[2];
                    break;
                case 4:
                    InterfaceManager_SetPlayerMessage((void *)PTR(action[2]), 0);
                    break;
                case 5:
                    M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(obj) + 0x28), u32 *, 0x49C) = action[2];
                    break;
                case 6:
                    M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(obj) + 0x28), u32 *, 0x4A0) = action[2];
                    break;
                case 7:
                    InterfaceManager_SetPlayerMessage((void *)PTR(action[2]), 1);
                    break;
                case 8:
                    InterfaceManager_SetPlayerMessage((void *)PTR(action[2]), 2);
                    break;
                }
            }
        }
    }
    return 1;
}

s8 ActionLoadInterfacePack(s32 *arg0) {
    s32 temp_r3;

    temp_r3 = FileStreaming_LoadDataPack(arg0 + 1, 1, *arg0, 1);
    return (s8) ((u32) ((-1 - temp_r3) | (temp_r3 + 1)) >> 0x1FU);
}

void ActionUnloadInterfacePack(s32 *arg0) {
    FileStreaming_ReleaseDataPack(FileStreaming_GetPackID((char *)(arg0 + 1), 1, *arg0), 1);
}

s32 ActionCodeVariableModifyValue(u32 *data) {
    f32 value;
    f32 operand;
    f32 cur;
    u32 i;

    if (data[1] < 100) {
        switch (data[data[2] + 3]) {
        case 0:
            value = *(f32 *)&data[data[2] + 4];
            break;
        case 1:
            value = *(f32 *)(SLOT_LOAD(u8 *, data[data[2] + 4]) + 4);
            break;
        case 2:
            value = GetCodeVariable(data[data[2] + 4], (u8 *)PTR(data[data[2] + 5]));
            break;
        }
        for (i = 0; i < data[2]; i++) {
            cur = GetCodeVariable(data[1], (u8 *)PTR(data[i + 3]));
            switch (data[0]) {
            case 15:
                cur += value;
                break;
            case 16:
                cur -= value;
                break;
            case 45:
                cur *= value;
                break;
            case 46:
                if (value) {
                    cur /= value;
                }
                break;
            case 17:
                cur = value;
                break;
            }
            SetCodeVariable(data[1], (u8 *)PTR(data[i + 3]), cur);
        }
    } else {
        value = GetCodeVariable(data[1], (u8 *)PTR(data[2]));
        switch (data[3]) {
        case 0:
            operand = *(f32 *)&data[4];
            break;
        case 1:
            operand = *(f32 *)(SLOT_LOAD(u8 *, data[4]) + 4);
            break;
        case 2:
            operand = GetCodeVariable(data[4], (u8 *)PTR(data[5]));
            break;
        }
        switch (data[0]) {
        case 15:
            value += operand;
            break;
        case 16:
            value -= operand;
            break;
        case 45:
            value *= operand;
            break;
        case 46:
            if (operand) {
                value /= operand;
            }
            break;
        case 17:
            value = operand;
            break;
        }
        SetCodeVariable(data[1], (u8 *)PTR(data[2]), value);
    }
    return 1;
}

s32 ActionMissionObjChangeMode(u32 *action) {
    u32 i;

    for (i = 0; i < action[1]; i++) {
        M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(action[i + 2]) + 4), u8 *, 1) = action[0];
    }
    return 1;
}

void ActionVehicleChangeMode()
{
}

s32 ActionNavPointChangeMode(u32 *action) {
    u32 i;

    for (i = 0; i < action[1]; i++) {
        M2C_FIELD(action[i + 2], u8 *, 0x50) = action[0];
    }
    return 1;
}

s32 ActionPauseTACSystem(s32 *action) {
    switch (action[0]) {
    case 7:
        Trigger_PauseTACSystem(1);
        break;
    case 8:
        Trigger_PauseTACSystem(0);
        break;
    case 2:
        Trigger_PauseTACSystem(!Trigger_IsTACSystemPaused());
        break;
    }
    return 1;
}

s32 ActionSwapAIScript(u32 *action) {
    u8 *creature;
    u32 i;
    u8 *vehicle;

    for (i = 0; i < action[4]; i++) {
        vehicle = (u8 *)PTR(action[i + 5]);
        M2C_FIELD(vehicle, u32 *, 0x40) = action[0];
        M2C_FIELD(vehicle, u32 *, 0x44) = action[1];
        M2C_FIELD(vehicle, u32 *, 0x48) = action[2];
        M2C_FIELD(vehicle, u32 *, 0x4C) = action[3];
        creature = SLOT_LOAD(u8 *, (u8 *)(vehicle) + 0x28);
        if (creature != NULL) {
            switch (GameSettings_GetDifficulty()) {
            case 0:
                M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(creature) + 0x94), u32 *, 0xD0) = action[0];
                break;
            case 1:
                M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(creature) + 0x94), u32 *, 0xD0) = action[1];
                break;
            case 2:
                M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(creature) + 0x94), u32 *, 0xD0) = action[2];
                break;
            case 3:
                M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(creature) + 0x94), u32 *, 0xD0) = action[3];
                break;
            }
            AI_Car_Reset(creature + 0x94);
        }
    }
    return 1;
}

s32 ActionVehicleRemoveItem(u32 *action) {
    typedef struct ItemThing {
        u8 unk0[0x74];
        u32 flags;
        u8 unk78[0x8C - 0x78];
        void (**vtable)(struct ItemThing *);
    } ItemThing;
    typedef struct Vehicle {
        u8 unk0[0x28];
        PTR32(u8) creature;
    } Vehicle;
    void *list;
    u32 i;
    ItemThing *rack;
    ItemThing *item;
    u8 *creature;

    for (i = 0; i < action[1]; i++) {
        creature = GETP(((Vehicle *)PTR(action[i + 2]))->creature);
        if (creature != NULL) {
            list = creature + 0x344;
            rack = ThingManagerList_GetByType(list, 8);
            switch (action[0]) {
            case 0:
                WildCardRack_JettisonAll(rack);
                break;
            case 1:
                item = ThingManagerList_GetByType(list, 4);
                if (item != NULL) {
                    item->vtable[1](item);
                }
                break;
            }
            rack->flags &= ~0x400200;
        }
    }
    return 1;
}


s32 ActionVehicleGiveItem(void *arg) {
    typedef struct ItemInfo {
        u8 type;
        u8 pad1[0x10 - 0x1];
        f32 height;
        u8 pad14[0x28 - 0x14];
        u32 effect;
    } ItemInfo;
    typedef struct Item {
        Vector4 pos;
        u8 pad10[0x4];
        s32 respawn;
        s32 timer;
        PTR32(ItemInfo) info;
        f32 value;
        PTR32(u8) thing;
        u32 type;
        u8 pad2C[0x34 - 0x2C];
        u8 hasValue;
        u8 subtype;
        u8 pad36[0x38 - 0x36];
        s8 spawned;
        u8 pad39[0x40 - 0x39];
    } Item;
    typedef struct ItemParams {
        Vector4 quat;
        Vector4 pos;
        f32 unk30;
        f32 unk34;
        f32 unk38;
        u32 unk3C;
        PTR32(ItemInfo) info;
        f32 value;
        u8 subtype;
    } ItemParams;
    typedef struct Action {
        s32 type;
        PTR32(void) item;
        u32 count;
        PTR32(u8) cars[1];
    } Action;
    extern Vector4 g_zeroVector;
    Item *item;
    Action *action = arg;
    u32 i;
    u8 *thing = NULL;
    u32 j;
    u8 *car;
    u8 numPlayers;
    struct { Vector4 a; Vector4 b; Vector4 c; Vector4 zero; u32 id; s32 player; f32 f0; f32 f1; } p;
    ItemParams params __attribute__((aligned(16)));
    ItemParams params2;
    ItemInfo *info;
    u8 *t;
    u32 packID;
    void *pack;

    switch (action->type) {
    case 0:
        item = GETP(action->item);
        if (item == NULL) {
            return;
        }
        if (GETP(item->info)->type == 7) {
            numPlayers = PlayerManager_PlayerCount();
            for (j = 0; j < numPlayers; j++) {
                car = SLOT_LOAD(u8 *, ((u8 *)PlayerManager_GetPlayerByIndex(j) + 0x2C));
                if (car != NULL && SLOT_LOAD(u8 *, (car + 0x8C)) == g_carAVTable) {
                    t = ThingManagerList_GetByType(car + 0x344, 4);
                    if (t != NULL && SLOT_LOAD(Item *, (t + 0x90)) == item) {
                        ThingManagerList_Remove((void **)(car + 0x344), t);
                    }
                }
            }
        }
        if (GETP(item->thing) == NULL) {
            item->timer = 0;
            params.pos = item->pos;
            params.pos.y += GETP(item->info)->height;
            params.unk30 = 0.0f;
            params.unk34 = 0.0f;
            params.unk38 = 0.0f;
            SETP(params.info, GETP(item->info));
            params.quat.w = 1.0f;
            params.quat.x = 0.0f;
            params.quat.y = 0.0f;
            params.quat.z = 0.0f;
            params.value = item->hasValue ? item->value : 0.0f;
            params.subtype = item->subtype;
            t = ThingManager_DARYL_CreateThing(4, &params);
            if (t != NULL) {
                item->spawned = 1;
                SETP(item->thing, t);
                SLOT_STORE((GETP(item->thing) + 0x90), item);
                *(u32 *)(t + 0x74) |= 1;
                if (item->respawn != 0) {
                    SLOT_STORE((t + 0xD0), item);
                }
                if (GETP(item->info)->effect != 0) {
                    p.id = GETP(item->info)->effect;
                    p.a = item->pos;
                    p.b = params.quat;
                    p.c.z = 0.0f;
                    p.c.y = 0.0f;
                    p.c.x = 0.0f;
                    p.player = 0;
                    p.f0 = 0.0f;
                    p.f1 = 0.0f;
                    p.zero = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
        }
        thing = GETP(item->thing);
        break;
    case 1:
        info = NULL;
        if ((u32)HND(GETP(action->item)) < 1000) {
            packID = FileStreaming_GetPackID(SLOT_LOAD(char *, (SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0x9C)) + 0xA4)), 2, (u32)HND(GETP(action->item)));
            if (packID < 0xFFFFFFFF) {
                pack = FileStreaming_InitializeDataPack(packID, 2, 1);
                if (pack != NULL) {
                    info = (ItemInfo *)DataPack_GetScript(pack, (s32)HND(GETP(action->item)));
                }
            }
        } else {
            info = GETP(action->item);
        }
        if (info != NULL) {
            params2.pos = g_zeroVector;
            params2.pos.y += info->height;
            params2.unk30 = 0.0f;
            params2.unk34 = 0.0f;
            params2.unk38 = 0.0f;
            SETP(params2.info, info);
            params2.quat.w = 1.0f;
            params2.quat.x = 0.0f;
            params2.quat.y = 0.0f;
            params2.quat.z = 0.0f;
            params2.value = 0.0f;
            thing = ThingManager_DARYL_CreateThing(4, &params2);
            if (thing != NULL) {
                *(u32 *)(thing + 0x74) |= 1;
            }
        }
        break;
    }
    if (thing == NULL) {
        return 0;
    }
    for (i = 0; i < action->count; i++) {
        if (SLOT_LOAD(u8 *, (GETP(action->cars[i]) + 0x28)) != NULL) {
            CarA_ProcessItemPickup(SLOT_LOAD(u8 *, (GETP(action->cars[i]) + 0x28)), thing);
        }
    }
    return 1;
}


s32 ActionPlaySoundSequence(void *arg0) {
    extern u8 g_soundSeqVTable[];
    u32 id;
    void *thing;

    id = M2C_FIELD(arg0, u32 *, 8);
    if (M2C_FIELD(arg0, u32 *, 0) != 0) {
        if (id != 0) {
            if (M2C_FIELD(arg0, u32 *, 4) != 0) {
                SoundSequence_PlayQueued(PTR(id), 0);
            } else {
                SoundSequence_Play(PTR(id), 0);
            }
        }
    } else if (M2C_FIELD(arg0, u32 *, 4) != 0) {
        SoundSequence_ClearQueue();
    } else {
        for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) {
            if (SLOT_LOAD(u8 *, (u8 *)(thing) + 0x8C) == g_soundSeqVTable && M2C_FIELD(thing, u32 *, 0xB0) == id) {
                ((void (*)(void *))(M2C_FIELD(thing, void ***, 0x8C))[1])(thing);
            }
        }
    }
}

s32 ActionCreatureChangeMode(u32 *action) {
    u32 handle;
    u32 i;
    u8 *creature;

    for (i = 0; i < action[1]; i++) {
        creature = (u8 *)PTR(action[i + 2]);
        handle = M2C_FIELD(creature, u32 *, 0x1C);
        switch (action[0]) {
        case 0:
            M2C_FIELD(creature, u32 *, 0x20) = 0;
            if (handle != 0) {
                M2C_FIELD(creature, u32 *, 0x1C) = 0;
                Creature_Release(PTR(handle));
            }
            break;
        case 1:
            M2C_FIELD(creature, u32 *, 0x20) = 1;
            break;
        case 3:
            M2C_FIELD(creature, u32 *, 0x20) = 0;
            if (handle != 0) {
                M2C_FIELD(creature, u32 *, 0x1C) = 0;
                Creature_MotionStateInitDeath((u8 *)PTR(handle));
            }
            break;
        case 25:
            M2C_FIELD(creature, u32 *, 0x20) = 0;
            if (handle != 0) {
                M2C_FIELD(creature, u32 *, 0x1C) = 0;
                Creature_Release(PTR(handle));
            }
            break;
        case 26:
            M2C_FIELD(creature, u32 *, 0x20) = 1;
            if (handle != 0) {
                M2C_FIELD(creature, u32 *, 0x1C) = 0;
                Creature_MotionStateInitDeath((u8 *)PTR(handle));
            }
            break;
        }
    }
    return 1;
}
