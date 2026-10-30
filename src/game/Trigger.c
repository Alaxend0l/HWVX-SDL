#include "common.h"



s8 TriggerSoundSeqQueueEnd(void) {
    return (s8) (SoundSequence_GetTotalElementsInQueue() == 0);
}

s32 TriggerMoveableEnd(void) {
    return 1;
}

s32 TriggerCreaturePickedUp(void) {
    return 1;
}

s32 TriggerTrue(void) {
    return 1;
}

s32 TriggerGameRoundsComplete(void) {
    return 1;
}

s32 TriggerGameRoundXComplete(void) {
    return 1;
}

s32 TriggerItemDropped(void) {
    return 1;
}

void Trigger_Create(void) {
    M2C_UNK fn_800957D4();
    fn_800957D4();
}

void Trigger_Destroy(void) {
    extern s32 m_triggerData_803C7120;
    Deallocate(PTR(m_triggerData_803C7120));
    m_triggerData_803C7120 = 0;
}

void Trigger_Init(void) {
    extern s32 m_triggerData_803C7120;
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern u32 m_interfaces_802C1900[][2];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 offset = 0;
    u32 total = 0;

    if ((u32)m_triggerData_803C7120 != 0) {
        Deallocate(PTR(m_triggerData_803C7120));
        m_triggerData_803C7120 = 0;
    }
    for (i = 0; i < 28; i++) {
        TriggerStats_800D3C58[i][0] = 0;
        total += TriggerStats_800D3C58[i][1];
    }
    m_triggerData_803C7120 = (s32)HND(Allocate(total * 4, 0, ""));
    for (i = 0; i < 28; i++) {
        m_Triggers_802C1890[i] = (u32 **)PTR((m_triggerData_803C7120 + offset * 4));
        offset += TriggerStats_800D3C58[i][1];
    }
    for (i = 0; i < 10; i++) {
        m_interfaces_802C1900[i][0] = 0;
    }
    g_bTACSystemPaused_803C7124 = 0;
}

void Trigger_SaveActions(s32 *saved, u32 count) {
    typedef struct { u32 pad[4]; u32 value; u32 pad2; } TrigAction;
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    u32 type;
    u32 i;
    u32 j;
    u32 n;

    n = 0;
    for (type = 0; type < 28; type++) {
        for (i = 0; i < TriggerStats_800D3C58[type][0]; i++) {
            for (j = 0; j < m_Triggers_802C1890[type][i][7]; j++) {
                if (n >= count) {
                    return;
                }
                if (((TrigAction *)PTR(m_Triggers_802C1890[type][i][5]))[j].value != 0xFFFFFFFF) {
                    saved[n++] = ((TrigAction *)PTR(m_Triggers_802C1890[type][i][5]))[j].value;
                }
            }
        }
    }
}

void Trigger_RestoreActions(s32 *saved, u32 count) {
    typedef struct { u32 pad[4]; u32 value; u32 pad2; } TrigAction;
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    u32 type;
    u32 i;
    u32 j;
    u32 n;

    n = 0;
    for (type = 0; type < 28; type++) {
        for (i = 0; i < TriggerStats_800D3C58[type][0]; i++) {
            for (j = 0; j < m_Triggers_802C1890[type][i][7] && n < count; j++) {
                if (((TrigAction *)PTR(m_Triggers_802C1890[type][i][5]))[j].value != 0xFFFFFFFF) {
                    ((TrigAction *)PTR(m_Triggers_802C1890[type][i][5]))[j].value = saved[n++];
                }
            }
            if (type == 8) {
                m_Triggers_802C1890[type][i][9] = 1;
            } else {
                m_Triggers_802C1890[type][i][9] = 0x10;
            }
        }
    }
}

void Trigger_PauseTACSystem(s8 arg0) {
    extern s8 g_bTACSystemPaused_803C7124;
    g_bTACSystemPaused_803C7124 = arg0;
}

u8 Trigger_IsTACSystemPaused(void) {
    extern u8 g_bTACSystemPaused_803C7124;
    return g_bTACSystemPaused_803C7124;
}

void Trigger_Heartbeat(void) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    extern s8 (*pTriggerFuncArray_800D3DA8[])(void *param);
    extern s32 m_ReleasedInterface_803C7128;
    u32 type;
    u32 i;
    u32 j;
    u32 actions;
    u32 *trigger;

    if (!g_bTACSystemPaused_803C7124) {
        for (type = 0; type < 28; type++) {
            if (*(s8 *)&TriggerStats_800D3C58[type][2]) {
                continue;
            }
            for (i = 0; i < TriggerStats_800D3C58[type][0]; i++) {
                trigger = m_Triggers_802C1890[type][i];
                if ((trigger[9] & 1) && pTriggerFuncArray_800D3DA8[trigger[0]]((void *)PTR(trigger[1]))) {
                    trigger = m_Triggers_802C1890[type][i];
                    if (TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                        actions = 0;
                        trigger = m_Triggers_802C1890[type][i];
                        if ((s32)trigger[0] == 0x12 && ((u32 *)PTR(trigger[1]))[2] == 0) {
                            for (j = 0; j < m_Triggers_802C1890[type][i][8]; j++) {
                                actions += Action_AddAction(PTR((s32)HND(((u8 *)PTR(m_Triggers_802C1890[type][i][6]) + j * 0x18))));
                            }
                        } else {
                            for (j = 0; j < m_Triggers_802C1890[type][i][7]; j++) {
                                actions += Action_AddAction(PTR((s32)HND(((u8 *)PTR(m_Triggers_802C1890[type][i][5]) + j * 0x18))));
                            }
                        }
                        if (actions == 0) {
                            m_Triggers_802C1890[type][i][9] = 2;
                        } else {
                            m_Triggers_802C1890[type][i][9] = 0x10;
                        }
                    } else {
                        m_Triggers_802C1890[type][i][9] = 0x10;
                    }
                }
                trigger = m_Triggers_802C1890[type][i];
                if ((trigger[9] & 0x10) && !pTriggerFuncArray_800D3DA8[trigger[0]]((void *)PTR(trigger[1]))) {
                    m_Triggers_802C1890[type][i][9] |= 1;
                    m_Triggers_802C1890[type][i][9] &= ~0x10;
                }
            }
        }
        m_ReleasedInterface_803C7128 = -1;
    }
}

s32 Trigger_AddTrigger(s32 *trigger) {
    extern u32 TriggerStats_800D3C58[][3];
    extern char *triggerLabelsArray_800D3BE8[];
    extern s32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    char buf[0x200];
    s32 type = trigger[0];

    if (TriggerStats_800D3C58[type][0] == TriggerStats_800D3C58[type][1]) {
        sprintf(buf, "Could not add the trigger: \"%s\", please notify Peter.", triggerLabelsArray_800D3BE8[type]);
        return 0;
    }
    m_Triggers_802C1890[type][TriggerStats_800D3C58[type][0]++] = trigger;
    if (trigger[0] == 8) {
        trigger[9] = 1;
    } else {
        trigger[9] = 0x10;
    }
    g_bTACSystemPaused_803C7124 = 0;
    return 1;
}

s32 Trigger_ReleaseTrigger(s32 *trigger) {
    extern u32 TriggerStats_800D3C58[][3];
    extern s32 **m_Triggers_802C1890[];
    u32 i;

    for (i = 0; i < TriggerStats_800D3C58[trigger[0]][0]; i++) {
        if (m_Triggers_802C1890[trigger[0]][i] == trigger) {
            TriggerStats_800D3C58[trigger[0]][0]--;
            m_Triggers_802C1890[trigger[0]][i] = m_Triggers_802C1890[trigger[0]][TriggerStats_800D3C58[trigger[0]][0]];
            return 1;
        }
    }
    return 0;
}

s8 TriggerCheckConditions(u8 mode, s32 count, s32 conds) {
    typedef struct TriggerCondition {
        u32 type;
        PTR32(void) param;
        u8 unk8[0x2C - 0x8];
    } TriggerCondition;
    extern s8 (*pTriggerFuncArray_800D3DA8[])(void *param);
    TriggerCondition *cond = (TriggerCondition *)PTR(conds);
    u32 i;

    if (count == 0) {
        return 1;
    }
    switch (mode) {
    case 0:
        for (i = 0; i < count; i++) {
            if (!pTriggerFuncArray_800D3DA8[cond[i].type](GETP(cond[i].param))) {
                return 0;
            }
        }
        return 1;
    case 1:
        for (i = 0; i < count; i++) {
            if (pTriggerFuncArray_800D3DA8[cond[i].type](GETP(cond[i].param))) {
                return 1;
            }
        }
        return 0;
    case 2:
        for (i = 0; i < count; i++) {
            if (!pTriggerFuncArray_800D3DA8[cond[i].type](GETP(cond[i].param))) {
                return 1;
            }
        }
        return 0;
    case 3:
        for (i = 0; i < count; i++) {
            if (pTriggerFuncArray_800D3DA8[cond[i].type](GETP(cond[i].param))) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

/* Returns 1 from the calling trigger check when `a op b` holds
   (op: 21 >, 20 <, 22 ==, 47 >=, 48 <=). */
#define TRIGGER_COMPARE_RETURN(op, a, b) \
    switch (op) {                        \
    case 21:                             \
        if ((a) > (b)) {                 \
            return 1;                    \
        }                                \
        break;                           \
    case 20:                             \
        if ((a) < (b)) {                 \
            return 1;                    \
        }                                \
        break;                           \
    case 22:                             \
        if ((a) == (b)) {                \
            return 1;                    \
        }                                \
        break;                           \
    case 47:                             \
        if ((a) >= (b)) {                \
            return 1;                    \
        }                                \
        break;                           \
    case 48:                             \
        if ((a) <= (b)) {                \
            return 1;                    \
        }                                \
        break;                           \
    }

s32 TriggerLevelStart(void) {
    return 1;
}

f32 GetCodeVariable(s32 var, u8 *thing) {
    u8 *vehicle;
    u8 *player;
    u16 index;

    switch (var) {
    case 1:
        vehicle = SLOT_LOAD(u8 *, (thing + 0x28));
        if (vehicle != NULL) {
            return *(s32 *)(vehicle + 0x78);
        }
        break;
    case 2:
        vehicle = SLOT_LOAD(u8 *, (thing + 0x28));
        if (vehicle != NULL) {
            return *(u32 *)(SLOT_LOAD(u8 *, (vehicle + 0x94)) + 0x7C);
        }
        break;
    case 3:
    case 4:
        break;
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        vehicle = SLOT_LOAD(u8 *, (thing + 0x28));
        index = (u16)var - 20;
        if (vehicle != NULL) {
            player = SLOT_LOAD(u8 *, (vehicle + 0x94));
            if (player != NULL) {
                return ((u16 *)(player + 0x94))[index];
            }
        }
        break;
    default:
        return GameSettings_GetCodeVariable(var, (s32)HND(thing));
    }
    return 0.0f;
}

void SetCodeVariable(s32 var, u8 *obj, f32 value) {
    u8 *thing;
    u8 *data;
    u8 idx;

    switch (var) {
    case 1:
        thing = SLOT_LOAD(u8 *, (obj + 0x28));
        if (thing != NULL) {
            *(s32 *)(thing + 0x78) = (s32)value;
        }
        break;
    case 2:
        thing = SLOT_LOAD(u8 *, (obj + 0x28));
        if (thing != NULL) {
            *(u32 *)(SLOT_LOAD(u8 *, (thing + 0x94)) + 0x7C) = (u32)value;
            *(SLOT_LOAD(u8 *, (thing + 0x94)) + 0x71) = 1;
        }
        break;
    case 3:
        thing = SLOT_LOAD(u8 *, (obj + 0x28));
        if (thing != NULL) {
            s32 max;
            *(s32 *)(thing + 0x400) = (s32)value;
            max = *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (thing + 0x3BC)) + 0xA0)) + 0x14);
            if (*(s32 *)(thing + 0x78) > max) {
                *(s32 *)(thing + 0x78) = max;
            }
        }
        break;
    case 4:
        break;
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        thing = SLOT_LOAD(u8 *, (obj + 0x28));
        idx = (u8)var - 20;
        if (thing != NULL) {
            data = SLOT_LOAD(u8 *, (thing + 0x94));
            if (data != NULL) {
                ((s16 *)(data + 0x94))[idx] = (s16)value;
                *(SLOT_LOAD(u8 *, (thing + 0x94)) + 0xA4) = 1;
            }
        }
        break;
    default:
        GameSettings_SetCodeVariable(var, (s32)HND(obj), (u32)value);
        break;
    }
}

s32 TriggerCodeVariableValueCheck(u32 *data) {
    f32 value;
    f32 operand;
    s32 i;

    if (data[1] < 100) {
        for (i = 0; i < data[2]; i++) {
            value = GetCodeVariable(data[1], (u8 *)PTR(data[i + 3]));
            switch (data[data[2] + 3]) {
            case 0:
                operand = *(f32 *)&data[data[2] + 4];
                TRIGGER_COMPARE_RETURN(data[0], value, operand);
                break;
            case 1:
                operand = *(f32 *)(SLOT_LOAD(u8 *, data[data[2] + 4]) + 4);
                TRIGGER_COMPARE_RETURN(data[0], value, operand);
                break;
            case 2:
                operand = GetCodeVariable(data[data[2] + 4], (u8 *)PTR(data[data[2] + 5]));
                TRIGGER_COMPARE_RETURN(data[0], value, operand);
                break;
            }
        }
    } else {
        value = GetCodeVariable(data[1], (u8 *)PTR(data[2]));
        switch (data[3]) {
        case 0:
            operand = *(f32 *)&data[4];
            TRIGGER_COMPARE_RETURN(data[0], value, operand);
            break;
        case 1:
            operand = *(f32 *)(SLOT_LOAD(u8 *, data[4]) + 4);
            TRIGGER_COMPARE_RETURN(data[0], value, operand);
            break;
        case 2:
            operand = GetCodeVariable(data[4], (u8 *)PTR(data[5]));
            TRIGGER_COMPARE_RETURN(data[0], value, operand);
            break;
        }
    }
    return 0;
}

void Trigger_ItemPickedUp(void *vehicle, void *item) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 actions;
    u32 *trigger;
    u32 j;
    u32 *list;
    s8 foundItem;
    s8 foundVehicle;
    u32 count;
    u32 count2;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[6][0]; i++) {
            trigger = m_Triggers_802C1890[6][i];
            list = (u32 *)PTR(trigger[1]);
            count = list[0];
            count2 = list[count + 1];
            if (trigger[9] != 2) {
                foundItem = 0;
                foundVehicle = 0;
                for (j = 0; j < count && !foundItem; j++) {
                    if (((void **)PTR(((u32 *)PTR(m_Triggers_802C1890[6][i][1]) + 1)[j]))[9] == item) {
                        foundItem = 1;
                    }
                }
                if (foundItem) {
                    for (j = 0; j < count2 && !foundVehicle; j++) {
                        if (((void **)PTR(*(u32 *)PTR((m_Triggers_802C1890[6][i][1] + (2 + count + j) * 4))))[10] == vehicle) {
                            foundVehicle = 1;
                        }
                    }
                    if (foundVehicle && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                        actions = 0;
                        for (j = 0; j < m_Triggers_802C1890[6][i][7]; j++) {
                            actions += Action_AddAction(PTR(m_Triggers_802C1890[6][i][5] + j * 0x18));
                        }
                        if (actions == 0) {
                            m_Triggers_802C1890[6][i][9] = 2;
                        }
                    }
                }
            }
        }
    }
}

void Trigger_ItemDropped(void *vehicle, void *item) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 actions;
    u32 *trigger;
    u32 j;
    u32 *list;
    s8 foundItem;
    s8 foundVehicle;
    u32 count;
    u32 count2;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[24][0]; i++) {
            trigger = m_Triggers_802C1890[24][i];
            list = (u32 *)PTR(trigger[1]);
            count = list[0];
            count2 = list[count + 1];
            if (trigger[9] != 2) {
                foundItem = 0;
                foundVehicle = 0;
                for (j = 0; j < count && !foundItem; j++) {
                    if (((void **)PTR(((u32 *)PTR(m_Triggers_802C1890[24][i][1]) + 1)[j]))[9] == item) {
                        foundItem = 1;
                    }
                }
                if (foundItem) {
                    for (j = 0; j < count2 && !foundVehicle; j++) {
                        if (((void **)PTR(*(u32 *)PTR((m_Triggers_802C1890[24][i][1] + (2 + count + j) * 4))))[10] == vehicle) {
                            foundVehicle = 1;
                        }
                    }
                    if (foundVehicle && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                        actions = 0;
                        for (j = 0; j < m_Triggers_802C1890[24][i][7]; j++) {
                            actions += Action_AddAction(PTR(m_Triggers_802C1890[24][i][5] + j * 0x18));
                        }
                        if (actions == 0) {
                            m_Triggers_802C1890[24][i][9] = 2;
                        }
                    }
                }
            }
        }
    }
}

s32 TriggerInterfaceEnd(void) {
    return 1;
}

void Trigger_InterfaceEnd(u32 id) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 j;
    u32 actions;
    u32 i;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[7][0]; i++) {
            u32 *trigger = m_Triggers_802C1890[7][i];
            u32 param = *(u32 *)PTR(trigger[1]);
            if (trigger[9] != 2 && param == id && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                actions = 0;
                for (j = 0; j < m_Triggers_802C1890[7][i][7]; j++) {
                    actions += Action_AddAction(PTR(m_Triggers_802C1890[7][i][5] + j * 0x18));
                }
                if (actions == 0) {
                    m_Triggers_802C1890[7][i][9] = 2;
                }
            }
        }
    }
}

s32 TriggerVehicleKilled(u32 *data) {
    u32 i;

    for (i = 0; i < data[0]; i++) {
        u8 *vehicle = SLOT_LOAD(u8 *, (u8 *)((data + 1)[i]) + 0x28);
        if (vehicle != NULL && (M2C_FIELD(vehicle, s32 *, 0x74) & 2)) {
            return 1;
        }
    }
    return 0;
}

void Trigger_VehicleKilled(void *vehicle) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 actions;
    u32 *trigger;
    u32 j;
    s8 found;
    u32 count;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[12][0]; i++) {
            trigger = m_Triggers_802C1890[12][i];
            count = *(u32 *)PTR(trigger[1]);
            if (trigger[9] != 2) {
                found = 0;
                for (j = 0; j < count && !found; j++) {
                    if (((void **)PTR(((u32 *)PTR(m_Triggers_802C1890[12][i][1]) + 1)[j]))[10] == vehicle) {
                        found = 1;
                    }
                }
                if (found && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                    actions = 0;
                    for (j = 0; j < m_Triggers_802C1890[12][i][7]; j++) {
                        actions += Action_AddAction(PTR(m_Triggers_802C1890[12][i][5] + j * 0x18));
                    }
                    if (actions == 0) {
                        m_Triggers_802C1890[12][i][9] = 2;
                    }
                }
            }
        }
    }
}

s32 TriggerDesructibleKilled(u32 *data) {
    u32 i;

    for (i = 0; i < data[0]; i++) {
        if (M2C_FIELD((data + 1)[i], u32 *, 0x30) == 3) {
            return 1;
        }
    }
    return 0;
}

void Trigger_DestructibleKilled(void *thing) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 actions;
    u32 *trigger;
    u32 j;
    u32 *list;
    s8 found;
    u32 count;
    u32 mode;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[4][0]; i++) {
            trigger = m_Triggers_802C1890[4][i];
            list = (u32 *)PTR(trigger[1]);
            count = list[0];
            if (trigger[9] != 2) {
                found = 0;
                for (j = 0; j < count; j++) {
                    if (((void **)PTR(((u32 *)PTR(m_Triggers_802C1890[4][i][1]) + 1)[j]))[11] == thing) {
                        mode = list[count + 1];
                        if (mode == 3) {
                            found = 1;
                        } else if (mode == 2) {
                            found = !(M2C_FIELD(thing, u32 *, 0x74) & 0x20);
                        } else {
                            found = M2C_FIELD(thing, u32 *, 0x74) & 0x20;
                        }
                        break;
                    }
                }
                if (found && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                    actions = 0;
                    for (j = 0; j < m_Triggers_802C1890[4][i][7]; j++) {
                        actions += Action_AddAction(PTR(m_Triggers_802C1890[4][i][5] + j * 0x18));
                    }
                    if (actions == 0) {
                        m_Triggers_802C1890[4][i][9] = 2;
                    }
                }
            }
        }
    }
}

s32 TriggerCreatureKilled(void) {
    return 0;
}

s32 TriggerCreatureDroppedOff(void) {
    return 0;
}

void Trigger_CreatureDroppedOff(void *vehicle, void *creature) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 actions;
    u32 *trigger;
    u32 j;
    u32 *list;
    s8 foundCreature;
    s8 foundVehicle;
    u32 count;
    u32 count2;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[14][0]; i++) {
            trigger = m_Triggers_802C1890[14][i];
            list = (u32 *)PTR(trigger[1]);
            count = list[0];
            count2 = list[count + 1];
            if (trigger[9] != 2) {
                foundCreature = 0;
                foundVehicle = 0;
                for (j = 0; j < count && !foundCreature; j++) {
                    if (((void **)PTR(((u32 *)PTR(m_Triggers_802C1890[14][i][1]) + 1)[j]))[7] == creature) {
                        foundCreature = 1;
                    }
                }
                if (foundCreature) {
                    for (j = 0; j < count2 && !foundVehicle; j++) {
                        if (((void **)PTR(*(u32 *)PTR((m_Triggers_802C1890[14][i][1] + (2 + count + j) * 4))))[10] == vehicle) {
                            foundVehicle = 1;
                        }
                    }
                    if (foundVehicle && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                        actions = 0;
                        for (j = 0; j < m_Triggers_802C1890[14][i][7]; j++) {
                            actions += Action_AddAction(PTR(m_Triggers_802C1890[14][i][5] + j * 0x18));
                        }
                        if (actions == 0) {
                            m_Triggers_802C1890[14][i][9] = 2;
                        }
                    }
                }
            }
        }
    }
}


void Trigger_GameRoundsComplete(void) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 j;
    u32 actions;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[22][0]; i++) {
            u32 *trigger = m_Triggers_802C1890[22][i];
            if (trigger[9] != 2 && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                actions = 0;
                for (j = 0; j < m_Triggers_802C1890[22][i][7]; j++) {
                    actions += Action_AddAction(PTR(m_Triggers_802C1890[22][i][5] + j * 0x18));
                }
                if (actions == 0) {
                    m_Triggers_802C1890[22][i][9] = 2;
                }
            }
        }
    }
}

void Trigger_GameRoundXComplete(void) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 j;
    u32 actions;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[23][0]; i++) {
            u32 *trigger = m_Triggers_802C1890[23][i];
            if (trigger[9] != 2 && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                actions = 0;
                for (j = 0; j < m_Triggers_802C1890[23][i][7]; j++) {
                    actions += Action_AddAction(PTR(m_Triggers_802C1890[23][i][5] + j * 0x18));
                }
                if (actions == 0) {
                    m_Triggers_802C1890[23][i][9] = 2;
                }
            }
        }
    }
}

void Trigger_SoundSequenceQueueEmptied(void) {
    extern u32 TriggerStats_800D3C58[][3];
    extern u32 **m_Triggers_802C1890[];
    extern s8 g_bTACSystemPaused_803C7124;
    u32 i;
    u32 j;
    u32 actions;

    if (!g_bTACSystemPaused_803C7124) {
        for (i = 0; i < TriggerStats_800D3C58[26][0]; i++) {
            u32 *trigger = m_Triggers_802C1890[26][i];
            if (trigger[9] != 2 && TriggerCheckConditions(((u8 *)trigger)[0x28], trigger[4], trigger[3])) {
                actions = 0;
                for (j = 0; j < m_Triggers_802C1890[26][i][7]; j++) {
                    actions += Action_AddAction(PTR(m_Triggers_802C1890[26][i][5] + j * 0x18));
                }
                if (actions == 0) {
                    m_Triggers_802C1890[26][i][9] = 2;
                }
            }
        }
    }
}

