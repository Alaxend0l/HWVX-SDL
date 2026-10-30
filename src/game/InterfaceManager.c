#include "common.h"


/* The target arrow points at the override target, else the main one. */
static inline Vector4 TargetArrow_GetTargetPos(void) {
    extern s32 g_TargetArrow[7];

    if ((Vector4 *)PTR(g_TargetArrow[1]) != NULL) {
        return *(Vector4 *)PTR(g_TargetArrow[1]);
    }
    return *(Vector4 *)PTR(g_TargetArrow[0]);
}

/* Analog stick "virtual key" state, one per pad and direction. */
typedef struct InterfaceDirState {
    s8 active;
    s8 held;
    s16 timer;
} InterfaceDirState;

typedef struct InterfaceKeyState {
    u8 pad0[0x116];
    InterfaceDirState dir[4][4];
    u8 pad156[0x160 - 0x156];
    s8 disabled;
    u8 pad161[0x168 - 0x161];
    s8 locked;
} InterfaceKeyState;


/* Message passed to an interface's callback (GETP(InterfaceState.callback)). */
typedef struct InterfaceEventMsg {
    u32 index;
    u16 type;
    u16 id;
    u16 arg;
    u16 objState;
    u8 padC[0x20 - 0xC];
    u32 result;
    u32 value;
    u8 handled;
    u8 pad29[0x30 - 0x29];
    u32 data[3];
    PTR32(u32) values;
    u16 slotId;
} InterfaceEventMsg;

/* One g_interface[] entry (0x170 bytes). */
typedef struct InterfaceState {
    u8 pad0[4];
    u32 state;
    u8 pad8[4];
    PTR32(u8) parentObj;
    u32 unk10;
    u8 pad14[0x20 - 0x14];
    PTR32(u16) header;
    PTR32(u16) targets[0x16];
    u8 flags;
    u8 id;
    u8 pad7E;
    u8 isChild;
    u8 curTarget;
    u8 unk81;
    u8 pad82[0x90 - 0x82];
    FPTR32(void, (InterfaceEventMsg *msg)) callback;
    u8 pad94[4];
    s32 values[0x1E];
    u32 numValues;
    u8 player;
    u8 pad115[0x15C - 0x115];
    u32 parent;
    u8 unk160;
    u8 unk161;
    u8 pad162[0x170 - 0x162];
} InterfaceState;


void InterfaceManager_Init(void) {
    extern u8 g_interface[][0x170];
    extern s32 g_TargetArrow[7];
    extern void *g_pInterfaceToEnd;
    extern s32 g_numInterfaceObjectsAllocated;
    extern s32 g_numInterfaceObjectAllocations;
    extern s32 g_numInterfacesToRender;
    extern void *g_playerMessage1;
    extern void *g_playerMessage2;
    extern void *g_playerMessage3;
    u32 i;

    g_numInterfaceObjectsAllocated = 0;
    g_numInterfaceObjectAllocations = 0;
    g_numInterfacesToRender = 0;
    g_pInterfaceToEnd = NULL;
    for (i = 0; i < 0x12; i++) {
        SLOT_STORE(g_interface[i], NULL);
        g_interface[i][0x7D] = i;
    }
    g_TargetArrow[0] = 0;
    g_playerMessage1 = NULL;
    g_TargetArrow[1] = 0;
    g_TargetArrow[3] = 0;
    g_TargetArrow[4] = -1;
    g_TargetArrow[5] = 0;
    g_playerMessage2 = NULL;
    g_playerMessage3 = NULL;
}

void InterfaceManager_SetRadarBlip(u16 arg0) {
    extern s32 g_radarTextureIndex;
    g_radarTextureIndex = (s32) arg0;
}

void InterfaceManager_SetInput(s32 index, s32 input) {
    typedef struct InterfaceSlot {
        u8 pad0[0x25C];
        PTR32(u8) object;
        u8 pad260[0x270 - 0x260];
        s32 child;
        u8 pad274[0x300 - 0x274];
    } InterfaceSlot;
    typedef struct Interface {
        u32 count;
        u8 pad4[0x84 - 0x4];
        u8 hasInput;
        u8 pad85[0x88 - 0x85];
        PTR32(InterfaceSlot) slots;
        s32 input;
        u8 pad90[0x170 - 0x90];
    } Interface;
    extern Interface g_interface[];
    u32 i;

    g_interface[index].input = input;
    g_interface[index].hasInput = 1;
    for (i = 0; i < g_interface[index].count; i++) {
        switch (GETP(GETP(g_interface[index].slots)[i].object)[1]) {
        case 6:
        case 7:
            if (GETP(GETP(g_interface[index].slots)[i].object)[0] != 3) {
                InterfaceManager_SetInput(GETP(g_interface[index].slots)[i].child, input);
            }
            break;
        }
    }
}

static inline void InterfaceManager_ReleaseInterfaceObjects(u8 *objs) {
    typedef struct ObjectAllocation {
        u32 start;
        u32 count;
        s8 free;
    } ObjectAllocation;
    extern ObjectAllocation g_objectAllocations[28];
    extern u32 g_numInterfaceObjectAllocations;
    extern u32 g_numInterfaceObjectsAllocated;
    s32 i;

    if (objs == NULL) {
        return;
    }
    for (i = g_numInterfaceObjectAllocations - 1; i >= 0; i--) {
        if (*(u32 *)(objs + 0x2FC) == g_objectAllocations[i].start) {
            break;
        }
    }
    if (i == -1) {
        return;
    }
    if (g_numInterfaceObjectsAllocated == g_objectAllocations[i].start + g_objectAllocations[i].count) {
        g_numInterfaceObjectsAllocated -= g_objectAllocations[i].count;
        g_numInterfaceObjectAllocations--;
        while (g_numInterfaceObjectAllocations != 0 &&
               (g_objectAllocations[g_numInterfaceObjectAllocations - 1].free ||
                g_objectAllocations[g_numInterfaceObjectAllocations - 1].count == 0)) {
            g_numInterfaceObjectsAllocated -= g_objectAllocations[g_numInterfaceObjectAllocations - 1].count;
            memset(&g_objectAllocations[g_numInterfaceObjectAllocations - 1], 0, sizeof(ObjectAllocation));
            g_numInterfaceObjectAllocations--;
        }
    } else {
        g_objectAllocations[i].free = 1;
    }
    *(u32 *)(objs + 0x2FC) = -1;
}

void InterfaceManager_ReleaseInterface(u32 index) {
    typedef struct {
        u32 index;
        u16 type;
        u16 id;
        s16 unk10;
        s16 unk12;
        u8 pad14[0x18];
        s32 unk2C;
        s8 unk30;
        u8 pad31[0x1F];
    } InterfaceMessage;
    typedef struct Interface {
        u32 count;
        u8 pad4[0x20 - 0x4];
        PTR32(u8) header;
        u8 pad24[0x7D - 0x24];
        u8 id;
        u8 pad7E[0x88 - 0x7E];
        PTR32(u8) slots;
        u8 pad8C[0x90 - 0x8C];
        FPTR32(void, (InterfaceMessage *)) callback;
        u8 pad94[0x98 - 0x94];
        u32 values[(0x170 - 0x98) / 4];
    } Interface;
    extern Interface g_interface[];
    extern s8 g_bUnloadMusicPack;
    extern char *g_curMusicPackName;
    extern s32 g_curMusicPackID;
    u32 i;
    u8 *entry;
    u8 *slot;
    Interface *iface;

    if (index >= 0x12 || (iface = &g_interface[index])->count == 0) {
        return;
    }
    InterfaceManager_RemoveInterfaceFromRender(index);
    if (GETP(iface->callback) != NULL) {
        InterfaceMessage msg;

        msg.index = iface->id;
        msg.type = 5;
        msg.id = 0;
        msg.unk10 = 0;
        msg.unk2C = 0;
        msg.unk12 = 0;
        msg.unk30 = 0;
        GETP(iface->callback)(&msg);
    }
    for (i = 0; i < iface->count; i++) {
        slot = GETP(iface->slots) + i * 0x300;
        if (*(u32 *)(slot + 0x26C) != 0 && (slot[0x243] == 4 || slot[0x243] == 3)) {
            *(u32 *)(slot + 0x26C) = 0;
            slot[0x244] = 0;
        } else if (slot[0x243] == 6) {
            if (&g_interface[*(u32 *)(slot + 0x270)] != iface) {
                InterfaceManager_ReleaseInterface(*(u32 *)(slot + 0x270));
            }
        } else if (slot[0x243] == 5) {
            Gfx_FontManager_ReleaseFont(slot[0x1B0]);
        } else if (slot[0x243] == 7) {
            if (*(u32 *)(slot + 0x2D0) != 0) {
                SoundSequence_Stop(*(s32 *)(slot + 0x2D4));
            }
            FileStreaming_ReleaseAllDataPacks(9, 1);
        }
        if (slot[0x245] == 0x34) {
            InterfaceManager_RestoreMissionObjString(slot);
        }
    }
    for (i = 0; i < *SLOT_LOAD(u32 *, (GETP(iface->header) + 0x1C)); i++) {
        entry = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->header) + 0x1C)) + 4)) + i * 0x34;
        if (entry[0] == 0 && entry[2] != 0) {
            *(u32 *)(entry + 0xC) = iface->values[i];
        }
    }
    if (g_bUnloadMusicPack && g_curMusicPackName != NULL) {
        SongManager_StopBKSong();
        FileStreaming_GetPackID(g_curMusicPackName, 10, g_curMusicPackID);
        FileStreaming_ReleaseAllDataPacks(10, 1);
        g_curMusicPackName = NULL;
    }
    Trigger_InterfaceEnd(HND(GETP(iface->header)));
    InterfaceHasEnded(index);
    InterfaceManager_ReleaseInterfaceObjects(GETP(g_interface[index].slots));
    iface->count = 0;
}

void InterfaceManager_ReleaseAllInterfaces(void) {
    extern u8 g_interface[][0x170];
    extern s32 g_TargetArrow[7];
    extern void *g_pInterfaceToEnd;
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (SLOT_LOAD(void *, g_interface[i]) != NULL) {
            InterfaceManager_ReleaseInterface(i);
        }
    }
    g_pInterfaceToEnd = NULL;
    g_TargetArrow[0] = 0;
    g_TargetArrow[1] = 0;
    g_TargetArrow[3] = 0;
    g_TargetArrow[4] = -1;
    g_TargetArrow[5] = 0;
}

void InterfaceManager_ReleaseAllUnpausedInterfaces(void) {
    extern u8 g_interface[][0x170];
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (SLOT_LOAD(void *, g_interface[i]) != NULL && g_interface[i][0x83] == 0) {
            InterfaceManager_ReleaseInterface(i);
        }
    }
}

void InterfaceManager_SetCallBackRoutine(s32 index, void *callback) {
    extern u8 g_interface[][0x170];
    SLOT_STORE((g_interface[index] + 0x90), callback);
}

void InterfaceManager_SetInfoCallBackRoutine(s32 index, void *callback) {
    extern u8 g_interface[][0x170];
    SLOT_STORE((g_interface[index] + 0x94), callback);
}

s32 InterfaceManager_GetTotalActiveInterfaces(void) {
    extern u8 g_interface[][0x170];
    s32 count = 0;
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (SLOT_LOAD(void *, g_interface[i]) != NULL) {
            count++;
        }
    }
    return count;
}

void InterfaceManager_PauseAllInterface(u8 pause) {
    extern u8 g_interface[][0x170];
    u32 i;

    for (i = 0; i < 0x12; i++) {
        g_interface[i][0x83] = pause;
    }
}

void InterfaceManager_SetOffset(s32 index, f32 x, f32 y) {
    extern u8 g_interface[][0x170];
    u8 *iface = g_interface[index];
    iface[0x82] = 1;
    *(f32 *)(iface + 0x60) = x;
    *(f32 *)(iface + 0x64) = y;
}

s32 InterfaceManager_Create(u8 *header, u8 *owner, void (*callback)(void *msg)) {
    typedef struct {
        s8 isVar;
        u8 pad1;
        u16 value;
    } CondValue;
    typedef struct {
        s32 id;
        PTR32(CondValue) val;
    } CodeVar;
    typedef struct {
        u8 type;
        u8 pad1[3];
        s32 min;
        s32 max;
        s32 offset;
        PTR32(CodeVar) data;
        u8 pad14[0x34 - 0x14];
    } Variable;
    typedef struct {
        u32 count;
        PTR32(Variable) vars;
    } VariableList;
    typedef struct {
        u8 type;
        u8 pad1[3];
        u32 value;
        u8 pad8[0xC - 0x8];
    } Property;
    typedef struct {
        u16 id;
        u8 pad2[0xC - 0x2];
        u32 numObjects;
        PTR32(u8) objects;
        u32 numProps;
        PTR32(Property) props;
        PTR32(VariableList) vars;
        u8 pad20[0x34 - 0x20];
        u32 layer;
        u8 pad38[0x3C - 0x38];
        u8 mode;
    } Header;
    typedef struct {
        u8 id;
        u8 pad1[0x10 - 0x1];
        u32 unk10;
        PTR32(u32) unk14;
    } ObjectData;
    typedef struct {
        u8 pad0[0x8];
        u8 active;
        u8 pad9[0x44 - 0x9];
        PTR32(ObjectData) data;
    } Object;
    typedef struct {
        u8 pad0[0x25C];
        PTR32(Object) object;
        u8 pad260[0x300 - 0x260];
    } Slot;
    typedef struct {
        u32 count;
        u32 state;
        PTR32(u8) owner;
        PTR32(Slot) selected;
        u8 pad10[0x14 - 0x10];
        s32 unk14;
        f32 unk18;
        PTR32(Property) unk1C;
        PTR32(Header) header;
        u32 unk24;
        u8 pad28[0x7D - 0x28];
        u8 index;
        u8 unk7E;
        u8 unk7F;
        u8 unk80;
        u8 unk81;
        u8 unk82;
        u8 paused;
        u8 unk84;
        u8 unk85;
        u8 pad86[0x88 - 0x86];
        PTR32(Slot) slots;
        s32 mode;
        FPTR32(void, (void *msg)) callback;
        u32 unk94;
        s32 vars[30];
        u32 numVars;
        u8 player;
        u8 unk115;
        u8 unk116[0x40];
        u8 pad156[0x158 - 0x156];
        u32 unk158;
        u8 pad15C[0x160 - 0x15C];
        u8 unk160;
        u8 unk161;
        u8 pad162[0x164 - 0x162];
        f32 unk164;
        u8 unk168;
        u8 pad169[0x170 - 0x169];
    } Interface;
    typedef struct {
        u8 pad0[2];
        u8 id;
        u8 pad3;
        f32 value;
    } WorldVar;
    typedef struct {
        u8 pad0[0x48];
        u32 numVars;
        PTR32(WorldVar *) vars;
    } World;
    typedef struct {
        u32 index;
        u16 type;
        u16 id;
        u8 pad8[0xA - 0x8];
        u16 result;
        u8 padC[0x28 - 0xC];
        u8 unk28;
        u8 pad29[0x3C - 0x29];
        PTR32(s32) vars;
        u8 pad40[0x48 - 0x40];
    } InterfaceMessage;
    extern Interface g_interface[];
    extern Header *g_pInterfaceToEnd;
    extern s32 g_playerIndex;
    extern s8 g_bMemoryCardHWHack;
    void *InterfaceManager_GetInterfaceObjects(u32 num);
    void GSI_VehicleItemSelection(void *msg);
    InterfaceMessage msg;
    Header *hdr = (Header *)header;
    s32 index;
    u32 minId;
    Interface *iface;
    u32 i;
    u32 j;
    Slot *slot;
    CodeVar *cv;

    index = -1;
    minId = -1;
    iface = NULL;
    if (hdr == NULL) {
        return -1;
    }
    if (hdr == g_pInterfaceToEnd) {
        g_pInterfaceToEnd = NULL;
        Trigger_InterfaceEnd(HND(hdr));
        return -1;
    }
    for (i = 0; i < 0x12; i++) {
        if (g_interface[i].count == 0) {
            index = i;
            iface = &g_interface[i];
            break;
        }
    }
    if (iface == NULL) {
        return -1;
    }
    SETP(iface->slots, InterfaceManager_GetInterfaceObjects(hdr->numObjects));
    if (GETP(iface->slots) == NULL) {
        return -1;
    }
    iface->paused = 0;
    iface->count = 0;
    SETP(iface->owner, owner);
    SETP(iface->selected, NULL);
    SETP(iface->header, hdr);
    iface->unk24 = 0;
    SETP(iface->callback, callback);
    iface->unk14 = -1;
    iface->unk7E = 0;
    iface->unk7F = 0;
    iface->unk115 = 0;
    iface->unk84 = 0;
    iface->unk80 = 0;
    iface->unk81 = 0;
    iface->unk82 = 0;
    SETP(iface->unk1C, NULL);
    iface->unk18 = 0.0f;
    iface->unk94 = 0;
    iface->unk85 = 0;
    iface->unk158 = 0;
    iface->player = g_playerIndex;
    iface->index = index;
    iface->unk160 = 0;
    iface->unk161 = 0;
    iface->unk164 = 500.0f;
    iface->unk168 = 0;
    switch (hdr->mode) {
    case 1:
        iface->mode = 0;
        break;
    case 2:
        iface->mode = 1;
        break;
    case 3:
        iface->mode = 2;
        break;
    case 4:
        iface->mode = 3;
        break;
    case 0:
    default:
        iface->mode = -1;
        break;
    }
    memset(iface->unk116, 0, 0x40);
    if (hdr->layer == 0) {
        hdr->layer = 5;
    }
    InterfaceManager_AddInterfaceToRender(index, hdr->layer);
    switch (hdr->id) {
    case 2:
        if (GameSettings_GetGameType() == 0) {
            SETP(iface->callback, GSI_VehicleItemSelection);
        } else if (GameSettings_GetGameType() == 1) {
            if (StateManager_CurrState() == 2) {
                SETP(iface->callback, GSI_VehicleItemSelection);
            } else {
                SETP(iface->callback, ArmageddonDropShip);
            }
        }
        GameSettings_SetDefaultControllerState();
        break;
    case 3:
        if (GameSettings_GetGameType() == 0) {
            SETP(iface->callback, GSI_VehicleItemSelection);
        } else if (GameSettings_GetGameType() == 1) {
            SETP(iface->callback, ArmageddonDropShip);
        }
        GameSettings_SetDefaultControllerState();
        break;
    case 8:
        SETP(iface->callback, TitleScreen_Interface);
        break;
    }
    for (i = 0; i < hdr->numObjects; i++) {
        InterfaceManager_AddObject(iface, GETP(hdr->objects) + i * 0x6C);
    }
    InterfaceManager_SetLoadState(iface, 0);
    for (i = 0; i < GETP(iface->header)->numProps; i++) {
        if (GETP(GETP(iface->header)->props)[i].type == 0x1E) {
            SETP(iface->unk1C, &GETP(GETP(iface->header)->props)[i]);
            iface->unk18 = GETP(GETP(iface->header)->props)[i].value;
            break;
        }
    }
    for (i = 0; i < iface->count; i++) {
        slot = &GETP(iface->slots)[i];
        if (GETP(slot->object)->active && GETP(GETP(slot->object)->data)->id != 0 && GETP(GETP(slot->object)->data)->id < minId) {
            minId = GETP(GETP(slot->object)->data)->id;
            SETP(iface->selected, slot);
        }
    }
    for (i = 0, iface->numVars = 0; i < GETP(hdr->vars)->count && i < 30; i++, iface->numVars++) {
        switch (GETP(GETP(hdr->vars)->vars)[i].type) {
        case 0:
            iface->vars[i] = GETP(GETP(hdr->vars)->vars)[i].offset;
            break;
        case 1:
            cv = GETP(GETP(GETP(hdr->vars)->vars)[i].data);
            if (GETP(cv->val)->isVar) {
                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, iface->vars[GETP(cv->val)->value]);
            } else {
                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->val)->value);
            }
            iface->vars[i] += GETP(GETP(hdr->vars)->vars)[i].offset;
            if (iface->vars[i] > GETP(GETP(hdr->vars)->vars)[i].max) {
                iface->vars[i] = iface->vars[i] - GETP(GETP(hdr->vars)->vars)[i].max - 1 + GETP(GETP(hdr->vars)->vars)[i].min;
            } else if (iface->vars[i] < GETP(GETP(hdr->vars)->vars)[i].min) {
                iface->vars[i] = GETP(GETP(hdr->vars)->vars)[i].max - (GETP(GETP(hdr->vars)->vars)[i].min - iface->vars[i] - 1);
            }
            break;
        case 2:
            if (owner != NULL) {
                iface->vars[i] = *(s32 *)(owner + 0x60);
            } else {
                iface->vars[i] = g_playerIndex;
            }
            break;
        case 3:
            if (GETP(iface->selected) != NULL) {
                iface->vars[i] = GETP(GETP(GETP(iface->selected)->object)->data)->id;
            } else {
                iface->vars[i] = 0;
            }
            break;
        case 4:
            if ((World *)GETP(g_worldData.world) != NULL) {
                iface->vars[i] = 0;
                for (j = 0; j < ((World *)GETP(g_worldData.world))->numVars; j++) {
                    if (GETP(((World *)GETP(g_worldData.world))->vars)[j]->id == GETP(GETP(hdr->vars)->vars)[i].offset) {
                        iface->vars[i] = GETP(((World *)GETP(g_worldData.world))->vars)[j]->value;
                    }
                }
            }
            break;
        }
    }
    if (hdr->id == 7) {
        GameSettings_GetSaveGameName(iface->vars);
    }
    if (GETP(iface->callback) != NULL) {
        msg.index = iface->index;
        msg.type = 3;
        msg.id = hdr->id;
        msg.result = 0;
        msg.unk28 = 0;
        SETP(msg.vars, iface->vars);
        GETP(iface->callback)(&msg);
        if (msg.result != 0) {
            for (i = 0; i < iface->count; i++) {
                slot = &GETP(iface->slots)[i];
                if (GETP(slot->object)->active && GETP(GETP(slot->object)->data)->id == msg.result) {
                    SETP(iface->selected, slot);
                }
            }
        }
    }
    InterfaceManager_PreprocessInterface(iface);
    if (g_bMemoryCardHWHack && iface->count == 14 && GETP(GETP(iface->slots)[9].object)->active &&
        GETP(GETP(GETP(iface->slots)[9].object)->data)->id == 6 && GETP(GETP(GETP(iface->slots)[9].object)->data)->unk10 == 1) {
        g_bMemoryCardHWHack = 0;
        GETP(GETP(GETP(GETP(iface->slots)[9].object)->data)->unk14)[1] = 100;
    }
    return index;
}

void InterfaceManager_AddObject()
{
}

void InterfaceManager_PreprocessInterface(u8 *iface) {
    typedef struct MusicInfo {
        PTR32(char) name;
        u32 id;
    } MusicInfo;
    typedef struct InterfaceHeader {
        u8 pad0[0x30];
        PTR32(MusicInfo) music;
        u8 pad34[0x38 - 0x34];
        f32 volume;
    } InterfaceHeader;
    typedef struct FontInfo {
        u8 pad0[0x8];
        PTR32(u8) name;
        u8 padC[0x28 - 0xC];
        s32 id;
    } FontInfo;
    typedef struct ResourceEntry {
        u8 pad0[0x8];
        u32 soundID;
        PTR32(char) soundName;
        u8 pad10[0x14 - 0x10];
        PTR32(FontInfo) font;
        u8 pad18[0x20 - 0x18];
    } ResourceEntry;
    typedef struct ResourceList {
        u32 count;
        PTR32(ResourceEntry) entries;
    } ResourceList;
    typedef struct SoundPack {
        s32 id;
        PTR32(u8) data;
    } SoundPack;
    typedef struct InterfaceSlot {
        u8 pad0[0x245];
        u8 type;
        u8 pad246[0x25C - 0x246];
        PTR32(u8) object;
        u8 pad260[0x300 - 0x260];
    } InterfaceSlot;
    typedef struct WorldObjective {
        u32 unk0;
        PTR32(u8) info;
    } WorldObjective;
    typedef struct World {
        u8 pad0[0x58];
        u32 numObjectives;
        PTR32(WorldObjective) objectives;
    } World;
    extern char *g_curMusicPackName;
    extern u32 g_curMusicPackID;
    extern u32 g_numInterfaceDialogSoundPacks;
    extern u32 g_curInterfaceDialogSoundPack;
    extern u32 g_curInterfaceDialogSoundIndex;
    extern SoundPack g_interfaceDialogSoundPacks[15];
    u32 i;
    u32 total;
    ResourceList *list;
    InterfaceSlot *slot;
    s32 pack;
    u8 *data;
    SoundPack *sp;
    World *world;
    u32 k;
    u8 *info;
    WorldObjective *objective;
    u32 id;

    if (GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music) != NULL && GETP(GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->name) != NULL) {
        if (g_curMusicPackName == NULL || strcmp(g_curMusicPackName, GETP(GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->name)) != 0
            || g_curMusicPackID != GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->id) {
            SongManager_StopBKSong();
            if (g_curMusicPackName != NULL) {
                FileStreaming_ReleaseDataPack(FileStreaming_GetPackID(g_curMusicPackName, 10, g_curMusicPackID), 10);
            }
            FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(GETP(GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->name), 10, GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->id, 0), 10, 1);
            SongManager_PlaySong();
            SetMusicVol(32767.0f * (SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->volume);
            g_curMusicPackName = GETP(GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->name);
            g_curMusicPackID = GETP((SLOT_LOAD(InterfaceHeader *, (iface + 0x20)))->music)->id;
        }
    }
    g_numInterfaceDialogSoundPacks = 0;
    g_curInterfaceDialogSoundPack = 0;
    g_curInterfaceDialogSoundIndex = 0;
    if (SLOT_LOAD(u8 *, (iface + 0x158)) != NULL) {
        list = SLOT_LOAD(ResourceList *, (SLOT_LOAD(u8 *, (iface + 0x158)) + 0x258));
        total = 0;
        for (i = 0; i < list->count; i++) {
            if (GETP(GETP(list->entries)[i].font) != NULL) {
                if (GETP(GETP(GETP(list->entries)[i].font)->name) == NULL) {
                    continue;
                }
                GETP(GETP(list->entries)[i].font)->id = InterfaceManager_AddFont(GETP(GETP(GETP(list->entries)[i].font)->name));
            }
            if (GETP(GETP(list->entries)[i].soundName) != NULL && g_numInterfaceDialogSoundPacks < 15 && total < 1000000) {
                pack = FileStreaming_LoadDataPack(GETP(GETP(list->entries)[i].soundName), 9, GETP(list->entries)[i].soundID, 0);
                data = FileStreaming_InitializeDataPack(pack, 9, 1);
                sp = &g_interfaceDialogSoundPacks[g_numInterfaceDialogSoundPacks];
                g_numInterfaceDialogSoundPacks++;
                sp->id = pack;
                SETP(sp->data, data);
                total += *(u32 *)(data + 0x90);
                g_curInterfaceDialogSoundIndex = i + 1;
            }
        }
    }
    g_curInterfaceDialogSoundPack = 0;
    for (i = 0; i < *(u32 *)iface; i++) {
        slot = &(SLOT_LOAD(InterfaceSlot *, (iface + 0x88)))[i];
        switch (slot->type) {
        case 0x34:
            id = *(u16 *)(GETP(slot->object) + 0xA);
            world = GETP(g_worldData.world);
            if (world == NULL) {
                break;
            }
            for (k = 0; k < world->numObjectives; k++) {
                objective = &GETP(world->objectives)[k];
                info = GETP(objective->info);
                if (info[2] == id) {
                    if (info[1] == 0x17) {
                        InterfaceManager_SetMissionObjectiveText((u8 *)slot, objective);
                    } else {
                        InterfaceManager_SetObjectText((u8 *)slot, NULL);
                    }
                    break;
                }
            }
            break;
        }
    }
}

void InterfaceManager_SetObjectString(u8 *obj, char *str) {
    u8 *info = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 4));

    obj[0x248] = 1;
    obj[0x84] = 1;
    obj[0x2C] = 1;
    obj[0x164] = 1;
    obj[0x194] = 1;
    *(u16 *)(obj + 0x1AA) = 0;
    *(u32 *)(obj + 0x1AC) = *(u32 *)(info + 0x14);
    *(u16 *)(obj + 0x1A8) = 0;
    obj[0x1C0] = 0;
    SLOT_STORE((obj + 0x1A0), NULL);
    SLOT_STORE((obj + 0x1A4), NULL);
    obj[0x1B1] = 0;
    if (str != NULL) {
        if (strlen(str) > 0x80) {
            strncpy((char *)(obj + 0x1C0), str, 0x80);
        } else {
            strcpy((char *)(obj + 0x1C0), str);
        }
        *(u16 *)(obj + 0x1A8) = strlen(str);
        if (*(s32 *)(obj + 0x1AC) == 0) {
            *(u16 *)(obj + 0x1AA) = *(u16 *)(obj + 0x1A8);
        }
        if (*(u8 *)(SLOT_LOAD(u8 *, (obj + 0x258)) + 0x12) != 0) {
            obj[0x1B1] = 1;
        }
    }
}

void InterfaceManager_SetObjectText(u8 *obj, char **text) {
    char *str;

    if (text != NULL) {
        u8 *info = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 4));
        obj[0x248] = 1;
        obj[0x2C] = 1;
        obj[0x54] = 1;
        obj[0x84] = 1;
        obj[0x164] = 1;
        obj[0x194] = 1;
        *(u16 *)(obj + 0x1AA) = 0;
        *(u32 *)(obj + 0x1AC) = *(u32 *)(info + 0x14);
        *(u16 *)(obj + 0x1A8) = 0;
        obj[0x1C0] = 0;
        *(char ***)(obj + 0x1A0) = text;
        SLOT_STORE((obj + 0x1A4), NULL);
        obj[0x1B1] = 0;
        str = *text;
        if (str != NULL) {
            SLOT_STORE((obj + 0x1A4), str);
            *(u16 *)(obj + 0x1A8) = strlen(str);
            if (*(s32 *)(obj + 0x1AC) == 0) {
                *(u16 *)(obj + 0x1AA) = *(u16 *)(obj + 0x1A8);
            }
            if (*(u8 *)(SLOT_LOAD(u8 *, (obj + 0x258)) + 0x12) != 0) {
                obj[0x1B1] = 1;
            }
        }
    }
}

void InterfaceManager_EndInterface(s32 index) {
    extern u8 g_interface[][0x170];
    u8 *iface = g_interface[index];

    if (iface != NULL) {
        InterfaceManager_SetLoadState(iface, 1);
    }
}

void InterfaceManager_SetLoadState(u8 *iface, s32 state) {
    extern u8 g_interface[][0x170];
    void InterfaceManager_SetObjectState(u8 *iface, u8 *obj, s32 state, s32 arg3);
    u32 i;
    u8 *obj;

    if (iface[0x7F] != 0 && (s8)iface[0x115] != 0) {
        if (*(u32 *)(iface + 4) != *(u32 *)(g_interface[*(u32 *)(iface + 0x15C)] + 4)) {
            state = *(u32 *)(g_interface[*(u32 *)(iface + 0x15C)] + 4);
        } else {
            return;
        }
    }
    *(u32 *)(iface + 0x10) = 0;
    if (*(u32 *)(iface + 0x14) < 0xFFFFFFFF) {
        SoundSequence_Stop(*(s32 *)(iface + 0x14));
    }
    *(s32 *)(iface + 0x14) = -1;
    switch (state) {
    case 0:
        *(s32 *)(iface + 4) = state;
        for (i = 0; i < *(u32 *)iface; i++) {
            InterfaceManager_SetObjectState(iface, SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300, 4, 0);
            obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
            if (obj[0x243] == 6) {
                if ((s8)g_interface[*(u32 *)(obj + 0x270)][0x115] != 0) {
                    InterfaceManager_SetLoadState(g_interface[*(u32 *)(obj + 0x270)], state);
                }
                obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
                g_interface[*(u32 *)(obj + 0x270)][0x85] = obj[0x240];
            }
        }
        *(u32 *)(iface + 0x10) = *(u32 *)(SLOT_LOAD(u8 *, (iface + 0x20)) + 4);
        if (SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x20)) != NULL) {
            *(s32 *)(iface + 0x14) = SoundSequence_Play(SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x20)), NULL);
        }
        break;
    case 1:
        *(s32 *)(iface + 4) = state;
        for (i = 0; i < *(u32 *)iface; i++) {
            InterfaceManager_SetObjectState(iface, SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300, 5, 0);
            obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
            if (obj[0x243] == 6) {
                if ((s8)g_interface[*(u32 *)(obj + 0x270)][0x115] != 0) {
                    InterfaceManager_SetLoadState(g_interface[*(u32 *)(obj + 0x270)], state);
                }
                obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
                g_interface[*(u32 *)(obj + 0x270)][0x85] = obj[0x240];
            }
        }
        *(u32 *)(iface + 0x10) = *(u32 *)(SLOT_LOAD(u8 *, (iface + 0x20)) + 8);
        if (SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x28)) != NULL) {
            *(s32 *)(iface + 0x14) = SoundSequence_Play(SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x28)), NULL);
        }
        break;
    case 2:
        *(s32 *)(iface + 4) = state;
        for (i = 0; i < *(u32 *)iface; i++) {
            InterfaceManager_SetObjectState(iface, SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300, 1, 0);
            obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
            if (obj[0x243] == 6) {
                if ((s8)g_interface[*(u32 *)(obj + 0x270)][0x115] != 0) {
                    InterfaceManager_SetLoadState(g_interface[*(u32 *)(obj + 0x270)], state);
                }
                obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
                g_interface[*(u32 *)(obj + 0x270)][0x85] = obj[0x240];
            }
        }
        if (SLOT_LOAD(u8 *, (iface + 0xC)) != NULL) {
            InterfaceManager_SetObjectReactive(iface, 0, *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 0xC)) + 0x25C)) + 0x44)));
        }
        if (SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x24)) != NULL) {
            *(s32 *)(iface + 0x14) = SoundSequence_Play(SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x24)), NULL);
        }
        break;
    }
}

void InterfaceManager_LinkThingToObject(s32 index, u32 id, void *thing) {
    typedef struct InterfaceSlot {
        u8 pad0[0x25C];
        PTR32(u8) object;
        u8 pad260[0x288 - 0x260];
        PTR32(void) thing;
        u8 pad28C[0x300 - 0x28C];
    } InterfaceSlot;
    extern u8 g_interface[][0x170];
    u8 *iface = g_interface[index];
    u32 i;

    if (iface != NULL) {
        for (i = 0; i < *(u32 *)iface; i++) {
            if (id == *(u32 *)(GETP((SLOT_LOAD(InterfaceSlot *, (iface + 0x88)))[i].object) + 0x10)) {
                SETP((SLOT_LOAD(InterfaceSlot *, (iface + 0x88)))[i].thing, thing);
            }
        }
    }
}

void InterfaceManager_Heartbeat(void) {
    extern u8 g_interface[][0x170];
    extern s32 g_TargetArrow[7];
    extern f32 g_fDeltaTimeMS;
    extern s32 g_iDeltaTimeMS;
    typedef struct {
        u32 index;
        u16 type;
        u16 id;
        u8 unk8[0x48];
    } InterfaceMessage;
    f32 dt = g_fDeltaTimeMS;
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (SLOT_LOAD(void *, g_interface[i]) != NULL && g_interface[i][0x83] == 0 && g_interface[i][0x85] == 0) {
            void (*callback)(InterfaceMessage *) = *(void (**)(InterfaceMessage *))(g_interface[i] + 0x90);

            if (callback != NULL) {
                InterfaceMessage msg;

                msg.index = g_interface[i][0x7D];
                msg.type = 0xC;
                msg.id = *SLOT_LOAD(u16 *, (g_interface[i] + 0x20));
                callback(&msg);
            }
            InterfaceManager_MoveInterface(g_interface[i], dt);
        }
    }
    if (g_TargetArrow[5] > 0) {
        g_TargetArrow[5] -= g_iDeltaTimeMS;
    }
    if ((u8 *)PTR(g_TargetArrow[3]) != NULL) {
        u8 *info = SLOT_LOAD(u8 *, (g_TargetArrow[3] + 0x94));

        if (info != NULL && SLOT_LOAD(u8 *, (info + 0x2C)) != NULL) {
            g_TargetArrow[0] = (s32)HND((SLOT_LOAD(u8 *, (info + 0x2C)) + 0x40));
        } else {
            g_TargetArrow[0] = g_TargetArrow[3] + 0x40;
        }
    }
}

void InterfaceManager_MoveInterface(void *iface, f32 dt) {
}

void InterfaceManager_SetObjectReactive(u8 *iface, u32 onId, u32 offId) {
    u32 i;
    u32 j;
    u8 *obj;
    u8 *info;

    for (i = 0; i < *(u32 *)iface; i++) {
        obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
        if (*(u8 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 8) != 0 && *(u32 *)(obj + 0x254) != 0) {
            if (*SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) == onId) {
                InterfaceManager_SetObjectState(iface, obj, 1, 0);
            }
            if (*SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) == offId) {
                InterfaceManager_SetObjectState(iface, obj, 2, 0);
                info = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44));
                for (j = 0; j < *(u32 *)(info + 0x10); j++) {
                    if (*(u32 *)(SLOT_LOAD(u8 *, (info + 0x14)) + 4) != 0) {
                        obj[0x247] = 0;
                        *(u32 *)(obj + 0x274) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) + 0x14)) + 4);
                        break;
                    }
                }
            }
        }
    }
}

u32 InterfaceManager_KeyPress(InterfaceKeyState *im, s32 pad, u8 key) {
    extern s32 g_iDeltaTimeMS;
    u32 result = 0;

    if (pad == -2 || im->disabled) {
        return 0;
    }
    switch (pad) {
    case -1: {
        s16 x, y, cx, cy;
        u32 i;
        for (i = 0; i < 4; i++) {
            if (Input_KeyPress(i, key)) {
                result |= 1 << i;
            }
        }
        if (result == 0) {
            if (key == 3) {
                for (i = 0; i < 4; i++) {
                    if (Input_GetAnalog(i, &x, &y, &cx, &cy)) {
                        if (x > 100 && !im->dir[i][2].active && !im->locked) {
                            im->dir[i][2].active = 1;
                            result |= 1 << i;
                            im->dir[i][2].held = 1;
                            im->dir[i][2].timer = 250;
                        } else if (im->dir[i][2].held) {
                            result |= 1 << i;
                        } else {
                            im->dir[i][2].timer -= (s16)g_iDeltaTimeMS;
                            if (im->dir[i][2].timer <= 0) {
                                im->dir[i][2].timer = 0;
                                im->dir[i][2].active = 0;
                            }
                        }
                    }
                }
            } else if (key == 2) {
                for (i = 0; i < 4; i++) {
                    if (Input_GetAnalog(i, &x, &y, &cx, &cy)) {
                        if (x < -100 && !im->dir[i][3].active && !im->locked) {
                            im->dir[i][3].active = 1;
                            result |= 1 << i;
                            im->dir[i][3].held = 1;
                            im->dir[i][3].timer = 250;
                        } else if (im->dir[i][3].held) {
                            result |= 1 << i;
                        } else {
                            im->dir[i][3].timer -= (s16)g_iDeltaTimeMS;
                            if (im->dir[i][3].timer <= 0) {
                                im->dir[i][3].timer = 0;
                                im->dir[i][3].active = 0;
                            }
                        }
                    }
                }
            } else if (key == 4) {
                for (i = 0; i < 4; i++) {
                    if (Input_GetAnalog(i, &x, &y, &cx, &cy)) {
                        if (y > 100 && !im->dir[i][0].active && !im->locked) {
                            im->dir[i][0].active = 1;
                            result |= 1 << i;
                            im->dir[i][0].held = 1;
                            im->dir[i][0].timer = 250;
                        } else if (im->dir[i][0].held) {
                            result |= 1 << i;
                        } else {
                            im->dir[i][0].timer -= (s16)g_iDeltaTimeMS;
                            if (im->dir[i][0].timer <= 0) {
                                im->dir[i][0].timer = 0;
                                im->dir[i][0].active = 0;
                            }
                        }
                    }
                }
            } else if (key == 5) {
                for (i = 0; i < 4; i++) {
                    if (Input_GetAnalog(i, &x, &y, &cx, &cy)) {
                        if (y < -100 && !im->dir[i][1].active && !im->locked) {
                            im->dir[i][1].active = 1;
                            result |= 1 << i;
                            im->dir[i][1].held = 1;
                            im->dir[i][1].timer = 250;
                        } else if (im->dir[i][1].held) {
                            result |= 1 << i;
                        } else {
                            im->dir[i][1].timer -= (s16)g_iDeltaTimeMS;
                            if (im->dir[i][1].timer <= 0) {
                                im->dir[i][1].timer = 0;
                                im->dir[i][1].active = 0;
                            }
                        }
                    }
                }
            }
        }
        break;
    }
    default: {
        s16 x, y, cx, cy;
        if (Input_KeyPress(pad, key)) {
            result |= 1 << pad;
        }
        if (result == 0) {
            if (key == 3) {
                if (Input_GetAnalog(pad, &x, &y, &cx, &cy)) {
                    if (x > 100 && !im->dir[pad][2].active && !im->locked) {
                        im->dir[pad][2].active = 1;
                        result |= 1 << pad;
                        im->dir[pad][2].held = 1;
                        im->dir[pad][2].timer = 250;
                    } else if (im->dir[pad][2].held) {
                        result |= 1 << pad;
                    } else {
                        im->dir[pad][2].timer -= (s16)g_iDeltaTimeMS;
                        if (im->dir[pad][2].timer <= 0) {
                            im->dir[pad][2].timer = 0;
                            im->dir[pad][2].active = 0;
                        }
                    }
                }
            } else if (key == 2) {
                if (Input_GetAnalog(pad, &x, &y, &cx, &cy)) {
                    if (x < -100 && !im->dir[pad][3].active && !im->locked) {
                        im->dir[pad][3].active = 1;
                        result |= 1 << pad;
                        im->dir[pad][3].held = 1;
                        im->dir[pad][3].timer = 250;
                    } else if (im->dir[pad][3].held) {
                        result |= 1 << pad;
                    } else {
                        im->dir[pad][3].timer -= (s16)g_iDeltaTimeMS;
                        if (im->dir[pad][3].timer <= 0) {
                            im->dir[pad][3].timer = 0;
                            im->dir[pad][3].active = 0;
                        }
                    }
                }
            } else if (key == 4) {
                if (Input_GetAnalog(pad, &x, &y, &cx, &cy)) {
                    if (y > 100 && !im->dir[pad][0].active && !im->locked) {
                        im->dir[pad][0].active = 1;
                        result |= 1 << pad;
                        im->dir[pad][0].held = 1;
                        im->dir[pad][0].timer = 250;
                    } else if (im->dir[pad][0].held) {
                        result |= 1 << pad;
                    } else {
                        im->dir[pad][0].timer -= (s16)g_iDeltaTimeMS;
                        if (im->dir[pad][0].timer <= 0) {
                            im->dir[pad][0].timer = 0;
                            im->dir[pad][0].active = 0;
                        }
                    }
                }
            } else if (key == 5) {
                if (Input_GetAnalog(pad, &x, &y, &cx, &cy)) {
                    if (y < -100 && !im->dir[pad][1].active && !im->locked) {
                        im->dir[pad][1].active = 1;
                        result |= 1 << pad;
                        im->dir[pad][1].held = 1;
                        im->dir[pad][1].timer = 250;
                    } else if (im->dir[pad][1].held) {
                        result |= 1 << pad;
                    } else {
                        im->dir[pad][1].timer -= (s16)g_iDeltaTimeMS;
                        if (im->dir[pad][1].timer <= 0) {
                            im->dir[pad][1].timer = 0;
                            im->dir[pad][1].active = 0;
                        }
                    }
                }
            }
        }
        break;
    }
    }
    return result;
}

s32 InterfaceManager_PlayEvent(InterfaceState *iface, u8 *obj, u8 *event, u16 arg) {
    typedef struct {
        s8 isVar;
        u8 pad1;
        u16 value;
    } EventArg;
    typedef struct {
        u8 slot;
        u8 pad1[3];
        s32 delta;
    } EventMod;
    typedef struct {
        PTR32(u16) target;
        u8 flags;
        u8 type;
        u16 id;
        PTR32(void) args;
        u32 movie;
        u8 pad10[4];
        u32 sound;
        PTR32(u8) bytes;
        u32 numMods;
        PTR32(EventMod) mods;
        s32 delta;
    } EventDef;
    typedef struct {
        u8 pad0[8];
        PTR32(EventDef) def;
    } Event;
    typedef struct {
        u8 pad0[4];
        s32 min;
        s32 max;
        u8 padC[0x34 - 0xC];
    } ValueDef;
    typedef struct {
        u32 type;
        u32 id;
    } EndLevelAction;
    extern InterfaceState g_interface[];
    extern s8 g_bMemoryCardHWHack;
    void InterfaceManager_SetObjectState(InterfaceState *iface, u8 *obj, s32 state, s32 arg);
    void GameSettings_SetCodeVariable(s32 var, s32 value, s32 arg2);
    void ForceIPLReset(void);
    u8 stateSet;
    u32 i;
    u32 a;
    u32 b;
    s32 v;
    EventMod *mod;
    ValueDef *vdefs;
    u8 slot;

    stateSet = 0;
    if (obj != NULL && (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)))[5] == 0 && (SLOT_LOAD(u8 *, (obj + 0x25C)))[6] != 0) {
        InterfaceManager_SetObjectState(iface, obj, 3, 0);
        stateSet = 1;
    }
    if (GETP(((Event *)event)->def)->sound != 0) {
        SoundSequence_Play(PTR(GETP(((Event *)event)->def)->sound), 0);
    }
    iface->unk81 = 0;
    switch (GETP(((Event *)event)->def)->type) {
    case 1:
    case 0x20:
        if (iface->isChild != 0) {
            if (GETP(((Event *)event)->def)->type == 0x20) {
                iface = &g_interface[iface->parent];
            } else {
                return;
            }
        }
        {
            InterfaceEventMsg msg;

            msg.index = iface->id;
            msg.type = 0xF;
            msg.id = GETP(((Event *)event)->def)->id;
            msg.arg = arg;
            msg.value = 0;
            msg.objState = 0;
            msg.handled = 0;
            msg.result = 0;
            if (GETP(GETP(((Event *)event)->def)->target) != NULL) {
                msg.slotId = *GETP(GETP(((Event *)event)->def)->target);
            }
            if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
                msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
            }
            SETP(msg.values, (u32 *)iface->values);
            if (GETP(iface->callback) != NULL) {
                GETP(iface->callback)(&msg);
            }
            if (msg.result == 0xB) {
                break;
            }
        }
        if (GETP(((Event *)event)->def)->sound != 0) {
            SoundSequence_Play(PTR(GETP(((Event *)event)->def)->sound), 0);
        }
        iface->flags = GETP(((Event *)event)->def)->flags;
        SETP(iface->targets[0], GETP(GETP(((Event *)event)->def)->target));
        if (!stateSet && GETP(iface->targets[0]) != NULL) {
            InterfaceManager_SetLoadState(iface, 1);
            if (iface->flags & 1) {
                iface->unk10 = 0;
                return 0;
            }
        }
        if (GETP(iface->targets[0]) != NULL) {
            return 0;
        }
        break;
    case 3:
        if (*GETP(iface->header) == 2) {
            if (GameSettings_GetCodeVariable(0xC9, iface->player) != 3) {
                return 0;
            }
        } else if (*GETP(iface->header) == 3) {
            if (GameSettings_GetCodeVariable(0xC9, iface->player) != 3) {
                return 0;
            }
        }
        if (iface->isChild != 0) {
            return 0;
        }
        {
            InterfaceEventMsg msg;

            msg.index = iface->id;
            msg.type = 0x10;
            msg.id = GETP(((Event *)event)->def)->id;
            msg.arg = arg;
            msg.value = 0;
            msg.objState = 0;
            msg.handled = 0;
            msg.slotId = 0xFFFF;
            msg.result = 0;
            if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
                msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
            }
            SETP(msg.values, (u32 *)iface->values);
            if (GETP(iface->callback) != NULL) {
                GETP(iface->callback)(&msg);
            }
            if (msg.result == 0xB) {
                break;
            }
        }
        if (StateManager_CurrState() == 2) {
            StateManager_QueueChange(0xB);
        }
        InterfaceManager_SetLoadState(iface, 1);
        if (iface->flags & 1) {
            iface->unk10 = 0;
            return 0;
        }
    case 4: {
        InterfaceEventMsg msg;

        msg.index = iface->id;
        msg.type = 0;
        msg.id = GETP(((Event *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.objState = 0;
        msg.handled = 0;
        msg.result = 0;
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.value != 0xFFFFFFFF && GETP(((Event *)event)->def)->sound != 0 && msg.handled == 0) {
            SoundSequence_Play(PTR(GETP(((Event *)event)->def)->sound), 0);
        }
        if (msg.result == 4) {
            InterfaceManager_SetLoadState(iface, 1);
            InterfaceManager_MoveInterface(iface, 0.0f);
            return 1;
        }
        if (msg.value == 0) {
            return 1;
        }
        if (msg.value != 0xFFFFFFFF) {
            iface->flags = GETP(((Event *)event)->def)->flags;
            SETP(iface->targets[0], (u16 *)PTR(msg.value));
            if (!stateSet && GETP(iface->targets[0]) != NULL) {
                InterfaceManager_SetLoadState(iface, 1);
                if (iface->flags & 1) {
                    iface->unk10 = 0;
                    return 0;
                }
            }
            return 0;
        }
    }
    case 2: {
        InterfaceEventMsg msg;
        u32 cur = iface->curTarget;

        msg.index = iface->id;
        msg.type = 0xF;
        msg.id = GETP(((Event *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.objState = 0;
        msg.handled = 0;
        msg.result = 0;
        if (cur != 0) {
            msg.slotId = *GETP(iface->targets[cur]);
        } else {
            msg.slotId = 0xFFFF;
        }
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        SETP(msg.values, (u32 *)iface->values);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.result == 0xB) {
            return 1;
        }
        InterfaceManager_Backup_Key(iface, event, GETP(((Event *)event)->def)->flags);
        return 0;
    }
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 0x1E: {
        InterfaceEventMsg msg;

        msg.index = iface->id;
        if (GETP(((Event *)event)->def)->type == 7) {
            EndLevelAction action;

            action.type = 4;
            action.id = GETP(((Event *)event)->def)->id;
            ActionEndLevel(&action);
            return 1;
        }
        if (GETP(((Event *)event)->def)->type == 8) {
            EndLevelAction action;

            action.type = 5;
            action.id = GETP(((Event *)event)->def)->id;
            ActionEndLevel(&action);
            return 1;
        }
        if (GETP(((Event *)event)->def)->type == 6 || GETP(((Event *)event)->def)->type == 0x1E) {
            u32 progA;
            u32 progB;

            if (GETP(((Event *)event)->def)->type == 6) {
                EventArg **args = GETP(GETP(((Event *)event)->def)->args);

                if (args[0]->isVar) {
                    progA = iface->values[args[0]->value];
                } else {
                    progA = args[0]->value;
                }
                if (args[1]->isVar) {
                    progB = iface->values[args[1]->value];
                } else {
                    progB = args[1]->value;
                }
            } else {
                GameSettings_GetLoadedProgression(&progA, &progB);
            }
            StateProgressionManager_SetProgressionID(progA, progB);
            msg.type = 6;
            StateManager_QueueChange(0xB);
            msg.result = 4;
        } else if (GETP(((Event *)event)->def)->type == 9) {
            msg.type = 9;
            msg.id = GETP(((Event *)event)->def)->id;
            InterfaceManager_ReleaseInterface(iface->id);
            StateManager_QueueChange(2);
            return;
        } else if (GETP(((Event *)event)->def)->type == 10) {
            msg.type = 10;
            msg.id = GETP(((Event *)event)->def)->id;
            StateProgressionManager_RestartCurrentLevel();
        }
        msg.arg = arg;
        msg.value = 0;
        msg.objState = 0;
        msg.handled = 0;
        msg.result = 0;
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.value != 0xFFFFFFFF && GETP(((Event *)event)->def)->sound != 0 && msg.handled == 0) {
            SoundSequence_Play(PTR(GETP(((Event *)event)->def)->sound), 0);
        }
        if (msg.result == 4) {
            InterfaceManager_SetLoadState(iface, 1);
            InterfaceManager_MoveInterface(iface, 0.0f);
            return 1;
        }
        if (msg.value == 0) {
            return 1;
        }
        if (msg.value == 0xFFFFFFFF) {
            break;
        }
        iface->flags = GETP(((Event *)event)->def)->flags;
        SETP(iface->targets[0], (u16 *)PTR(msg.value));
        if (!stateSet && GETP(iface->targets[0]) != NULL) {
            InterfaceManager_SetLoadState(iface, 1);
            if (iface->flags & 1) {
                iface->unk10 = 0;
                return 0;
            }
        }
        return 0;
    }
    case 0xD: {
        InterfaceEventMsg msg;

        msg.index = iface->id;
        msg.type = 0xD;
        msg.id = GETP(((Event *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.objState = 0;
        msg.handled = 0;
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        msg.data[0] = GETP(GETP(((Event *)event)->def)->bytes)[0];
        msg.data[1] = GETP(GETP(((Event *)event)->def)->bytes)[1];
        msg.data[2] = GETP(GETP(((Event *)event)->def)->bytes)[2];
        SETP(msg.values, (u32 *)iface->values);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        break;
    }
    case 0xE: {
        InterfaceEventMsg msg;

        msg.index = iface->id;
        msg.type = 0xE;
        msg.id = GETP(((Event *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.objState = 0;
        msg.handled = 0;
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        msg.data[0] = GETP(GETP(((Event *)event)->def)->bytes)[0];
        SETP(msg.values, (u32 *)iface->values);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        break;
    }
    case 0xB:
        InterfaceManager_ExecuteActions(iface, GETP(GETP(((Event *)event)->def)->target));
        break;
    case 0xF:
        if (((EventArg **)GETP(GETP(((Event *)event)->def)->args))[0]->isVar) {
            a = iface->values[((EventArg **)GETP(GETP(((Event *)event)->def)->args))[0]->value];
        } else {
            a = ((EventArg **)GETP(GETP(((Event *)event)->def)->args))[0]->value;
        }
        if (((EventArg **)GETP(GETP(((Event *)event)->def)->args))[1]->isVar) {
            b = iface->values[((EventArg **)GETP(GETP(((Event *)event)->def)->args))[1]->value];
        } else {
            b = ((EventArg **)GETP(GETP(((Event *)event)->def)->args))[1]->value;
        }
        StateProgressionManager_SetProgressionID(a, b);
        break;
    case 0x10: {
        InterfaceEventMsg msg;

        msg.index = iface->id;
        msg.type = 0x11;
        msg.id = GETP(((Event *)event)->def)->id;
        msg.arg = arg;
        msg.value = (u32)HND(GETP(iface->header));
        msg.objState = 0;
        msg.handled = 0;
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.result == 4) {
            InterfaceManager_ReleaseInterface(iface->id);
            StateMovie_SetMovieToPlay(GETP(((Event *)event)->def)->movie);
            StateManager_QueueChange(3);
        }
        break;
    }
    case 0x11: {
        InterfaceEventMsg msg;

        if (((EventArg *)GETP(GETP(((Event *)event)->def)->args))->isVar) {
            a = iface->values[((EventArg *)GETP(GETP(((Event *)event)->def)->args))->value];
        } else {
            a = ((EventArg *)GETP(GETP(((Event *)event)->def)->args))->value;
        }
        msg.index = iface->id;
        msg.type = 0xF;
        msg.id = GETP(((Event *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.objState = 0;
        msg.handled = 0;
        msg.slotId = 0xFFFF;
        msg.result = 0;
        if (GETP(iface->parentObj) != NULL && SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) != NULL) {
            msg.objState = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(iface->parentObj) + 0x25C)) + 0x44));
        }
        SETP(msg.values, (u32 *)iface->values);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.result == 0xB) {
            break;
        }
        switch (*GETP(iface->header)) {
        case 2:
            if (GameSettings_GetCodeVariable(100, a)) {
                GameSettings_SetCodeVariable(0xCB, iface->player, a);
                GameSettings_SetCodeVariable(0xC9, iface->player, 3);
            }
            break;
        case 3:
            if (GameSettings_GetCodeVariable(0x66, a)) {
                GameSettings_SetCodeVariable(0xCC, iface->player, a);
                GameSettings_SetCodeVariable(0xC9, iface->player, 3);
            }
            break;
        }
        break;
    }
    case 0x12:
        GameSettings_SetCodeVariable(0x1F7, 0, GETP(((Event *)event)->def)->id);
        break;
    case 0x13:
        GameSettings_SaveGame();
        break;
    case 0x14:
        GameSettings_LoadGame();
        break;
    case 0x15:
        GameSettings_SetSaveGameName(&iface->values[GETP(((Event *)event)->def)->id]);
        g_bMemoryCardHWHack = 1;
        break;
    case 0x16:
        v = GameSettings_GetCodeVariable(0xCF, 0);
        v += GETP(((Event *)event)->def)->delta;
        if (v > 100) {
            v = 100;
        } else if (v < 0) {
            v = 0;
        }
        GameSettings_SetCodeVariable(0xCF, 0, v);
        break;
    case 0x17:
        v = GameSettings_GetCodeVariable(0xD0, 0);
        v += GETP(((Event *)event)->def)->delta;
        if (v > 100) {
            v = 100;
        } else if (v < 0) {
            v = 0;
        }
        GameSettings_SetCodeVariable(0xD0, 0, v);
        break;
    case 0x18:
        GameSettings_SaveGeneralGameData();
        break;
    case 0x19:
        ForceIPLReset();
        break;
    case 0x1A:
        GameSettings_ResetControllerConfig(1);
        break;
    case 0x1B:
        iface->unk160 = 1;
        iface->unk161 = 0;
        break;
    case 0x1C:
        GameSettings_FormatMemoryCard();
        break;
    case 0x1D:
        if (iface->state == 2) {
            TitleScreen_RunDemoLoop();
            InterfaceManager_SetLoadState(iface, 1);
        }
        break;
    case 0x1F:
        InterfaceManager_Create(GETP(GETP(((Event *)event)->def)->target), 0, 0);
        break;
    case 0x21:
        GameSettings_DeleteGameFile();
        break;
    }

    for (i = 0; i < GETP(((Event *)event)->def)->numMods; i++) {
        mod = GETP(GETP(((Event *)event)->def)->mods);
        slot = mod->slot;
        if (slot < iface->numValues) {
            iface->values[slot] += mod->delta;
            vdefs = SLOT_LOAD(ValueDef *, (SLOT_LOAD(u8 *, ((u8 *)GETP(iface->header) + 0x1C)) + 4));
            if (iface->values[slot] > vdefs[slot].max) {
                iface->values[slot] = __abs(iface->values[slot] - vdefs[slot].max) + vdefs[slot].min - 1;
            } else if (iface->values[slot] < vdefs[slot].min) {
                iface->values[slot] = vdefs[slot].max - (__abs(iface->values[slot] - vdefs[slot].min) - 1);
            }
            InterfaceManager_CheckConditions(iface, obj);
        }
    }
    return 1;
}
void InterfaceManager_Backup_Key(u8 *iface, u8 *key, u8 flags) {
    extern u8 g_interface[][0x170];
    s32 count;

    count = iface[0x80];
    if (iface[0x7F] != 0) {
        iface = g_interface[*(s32 *)(iface + 0x15C)];
        count = iface[0x80];
    }
    if (key != NULL) {
        u16 n = *(u16 *)(SLOT_LOAD(u8 *, (key + 8)) + 6);
        if (n != 0) {
            count -= n - 1;
            if (count < 0) {
                count = 0;
            }
        }
    }
    if (count == 0) {
        InterfaceManager_ReleaseAllInterfaces();
        StateManager_QueueChange(2);
        return;
    }
    iface[0x7C] = 0;
    if (key != NULL && SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (key + 8)) + 0x14)) != NULL) {
        SoundSequence_Play(SLOT_LOAD(s8 *, (SLOT_LOAD(u8 *, (key + 8)) + 0x14)), NULL);
    }
    count--;
    iface[0x81] = 1;
    *(u32 *)(iface + 0x24) = ((u32 *)(iface + 0x28))[count];
    iface[0x80] = count;
    iface[0x7C] = flags;
    InterfaceManager_SetLoadState(iface, 1);
    if (iface[0x7C] & 1) {
        *(u32 *)(iface + 0x10) = 0;
    }
}

void InterfaceManager_MoveDialogSystem(u8 *iface, u8 *obj, f32 dt) {
    typedef struct DialogQuad {
        u32 v[4];
    } DialogQuad;
    typedef struct DialogAnim {
        TextureKeyAnim anim;
        u32 pad;
    } DialogAnim;
    typedef struct DialogChar {
        s32 valueA;
        s32 valueB;
        PTR32(u8) info;
        PTR32(u32) color;
        u32 numIdle;
        PTR32(DialogAnim) idle;
        u32 numTalk;
        PTR32(DialogAnim) talk;
        u8 state;
        s32 key;
        s32 style;
        u8 pad2C[0x30 - 0x2C];
        PTR32(DialogAnim) special;
        PTR32(DialogQuad) quad;
    } DialogChar;
    typedef struct DialogEntry {
        u8 active;
        u8 id;
        u8 special;
        u8 specialKey;
        s32 time;
        u32 soundID;
        PTR32(char) soundName;
        PTR32(char *) text;
        PTR32(DialogChar) chr;
        u8 actions[8];
    } DialogEntry;
    typedef struct DialogList {
        u32 count;
        PTR32(DialogEntry) entries;
    } DialogList;
    typedef struct SoundPack {
        s32 id;
        PTR32(u8) data;
    } SoundPack;
    extern u32 g_numInterfaceDialogSoundPacks;
    extern u32 g_curInterfaceDialogSoundPack;
    extern u32 g_curInterfaceDialogSoundIndex;
    extern SoundPack g_interfaceDialogSoundPacks[15];
    u32 i;
    DialogList *list;
    u8 *slot;
    u8 *object;
    DialogChar *chr;
    s8 released;
    s32 pack;
    u8 *data;
    u8 state;

    if (obj[0x246] == 4 || obj[0x246] == 5) {
        return;
    }
    if (obj[0x243] == 7 && obj[0x240] == 0) {
        list = SLOT_LOAD(DialogList *, (obj + 0x258));
        if (obj[0x125] != 0 && list->count != 0) {
            *(u32 *)(obj + 0x2C4) = 0;
            *(s8 *)(obj + 0x2D8) = 1;
        }
        if (*(s8 *)(obj + 0x2D8) != 0) {
            *(s8 *)(obj + 0x2D8) = 0;
            *(s32 *)(obj + 0x2C8) = GETP(list->entries)[*(u32 *)(obj + 0x2C4)].time;
            SLOT_STORE((obj + 0x2DC), GETP(GETP(list->entries)[*(u32 *)(obj + 0x2C4)].chr));
            for (i = 0; i < *(u32 *)iface; i++) {
                slot = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
                if (SLOT_LOAD(u8 *, (slot + 0x254)) == NULL) {
                    continue;
                }
                object = SLOT_LOAD(u8 *, (slot + 0x25C));
                switch (object[0]) {
                case 0x21:
                    if (slot[0x243] == 5) {
                        if (GETP(GETP(list->entries)[*(u32 *)(SLOT_LOAD(u8 *, (iface + 0x158)) + 0x2C4)].text) != NULL) {
                            chr = SLOT_LOAD(DialogChar *, (obj + 0x2DC));
                            slot[0x1B0] = chr->style;
                            SLOT_STORE((SLOT_LOAD(u8 *, (slot + 0x258)) + 4), GETP((SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->info));
                            *(u32 *)(SLOT_LOAD(u8 *, (slot + 0x254)) + 0x10) = 0;
                            *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (slot + 0x25C)) + 0x60)) + 0x18) = 1;
                            *SLOT_LOAD(u32 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (slot + 0x25C)) + 0x60)) + 0x1C)) = *GETP((SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->color);
                            if (GETP((SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->quad) != NULL) {
                                *(u32 *)(SLOT_LOAD(u8 *, (slot + 0x254)) + 0x18) = 0;
                                *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (slot + 0x25C)) + 0x60)) + 0x10) = 1;
                                *SLOT_LOAD(DialogQuad *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (slot + 0x25C)) + 0x60)) + 0x14)) = *GETP((SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->quad);
                            }
                            **(char ****)(slot + 0x258) = GETP(GETP(list->entries)[*(u32 *)(SLOT_LOAD(u8 *, (iface + 0x158)) + 0x2C4)].text);
                            InterfaceManager_SetObjectText(slot, **(char ****)(slot + 0x258));
                        } else {
                            **(char ****)(slot + 0x258) = NULL;
                            InterfaceManager_SetObjectText(slot, **(char ****)(slot + 0x258));
                        }
                    }
                    break;
                case 0x22:
                    if (slot[0x243] == 0) {
                        if (GETP(list->entries)[*(u32 *)(obj + 0x2C4)].active && *(u16 *)(object + 0xA) == GETP(list->entries)[*(u32 *)(obj + 0x2C4)].id) {
                            SLOT_STORE((slot + 0x2DC), SLOT_LOAD(DialogChar *, (obj + 0x2DC)));
                        }
                    }
                    break;
                case 0x23:
                    if (slot[0x243] == 5) {
                        if (GETP(list->entries)[*(u32 *)(obj + 0x2C4)].active && *(u16 *)(object + 0xA) == GETP(list->entries)[*(u32 *)(obj + 0x2C4)].id) {
                            *SLOT_LOAD(s32 *, (object + 0x14)) = (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->valueA;
                            InterfaceManager_SetObjectText(slot, **(char ****)(obj + 0x258));
                        }
                    }
                    break;
                case 0x24:
                    if (slot[0x243] == 5) {
                        if (GETP(list->entries)[*(u32 *)(obj + 0x2C4)].active && *(u16 *)(object + 0xA) == GETP(list->entries)[*(u32 *)(obj + 0x2C4)].id) {
                            *SLOT_LOAD(s32 *, (object + 0x14)) = (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->valueB;
                            InterfaceManager_SetObjectText(slot, **(char ****)(obj + 0x258));
                        }
                    }
                    break;
                }
            }
            *(s8 *)(obj + 0x2D9) = 0;
            if (GETP(list->entries)[*(u32 *)(obj + 0x2C4)].special) {
                (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->state = 5;
                (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->key = GETP(list->entries)[*(u32 *)(obj + 0x2C4)].specialKey;
            } else {
                (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->state = 1;
            }
            InterfaceManager_ExecuteActions(iface, GETP(list->entries)[*(u32 *)(obj + 0x2C4)].actions);
        } else {
            *(s32 *)(obj + 0x2C8) -= (s32)dt;
            if (*(s8 *)(obj + 0x2D9) == 0 && (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->state == 2) {
                *(s8 *)(obj + 0x2D9) = 1;
                if (GETP(GETP(list->entries)[*(u32 *)(obj + 0x2C4)].soundName) != NULL) {
                    released = 0;
                    if (SLOT_LOAD(u8 *, (obj + 0x2D0)) != NULL) {
                        SoundSequence_Stop(*(s32 *)(obj + 0x2D4));
                        FileStreaming_ReleaseDataPack(g_interfaceDialogSoundPacks[g_curInterfaceDialogSoundPack - 1].id, 9);
                        released = 1;
                        SLOT_STORE((obj + 0x2D0), NULL);
                    }
                    if (g_curInterfaceDialogSoundPack == 15) {
                        g_curInterfaceDialogSoundPack = 0;
                    }
                    SLOT_STORE((obj + 0x2D0), DataPack_GetScript(GETP(g_interfaceDialogSoundPacks[g_curInterfaceDialogSoundPack].data), GETP(list->entries)[*(u32 *)(obj + 0x2C4)].soundID));
                    if (SLOT_LOAD(u8 *, (obj + 0x2D0)) != NULL) {
                        *(s32 *)(obj + 0x2D4) = SoundSequence_Play(SLOT_LOAD(s8 *, (obj + 0x2D0)), NULL);
                    }
                    g_curInterfaceDialogSoundPack++;
                    if (released) {
                        while (g_curInterfaceDialogSoundIndex < list->count && GETP(GETP(list->entries)[g_curInterfaceDialogSoundIndex].soundName) == NULL) {
                            g_curInterfaceDialogSoundIndex++;
                        }
                        if (g_curInterfaceDialogSoundIndex < list->count) {
                            pack = FileStreaming_LoadDataPack(GETP(GETP(list->entries)[g_curInterfaceDialogSoundIndex].soundName), 9, GETP(list->entries)[g_curInterfaceDialogSoundIndex].soundID, 0);
                            data = FileStreaming_InitializeDataPack(pack, 9, 1);
                            if (g_numInterfaceDialogSoundPacks == 15) {
                                g_numInterfaceDialogSoundPacks = 0;
                            }
                            g_interfaceDialogSoundPacks[g_numInterfaceDialogSoundPacks].id = pack;
                            SETP(g_interfaceDialogSoundPacks[g_numInterfaceDialogSoundPacks].data, data);
                            g_curInterfaceDialogSoundIndex++;
                            g_numInterfaceDialogSoundPacks++;
                        }
                    }
                }
            }
            if (*(s32 *)(obj + 0x2C8) <= 0) {
                chr = SLOT_LOAD(DialogChar *, (obj + 0x2DC));
                if (chr->state == 5 || chr->state == 1 || chr->state == 4) {
                    if (GETP(GETP(list->entries)[*(u32 *)(obj + 0x2C4)].chr) != NULL) {
                        GETP(GETP(list->entries)[*(u32 *)(obj + 0x2C4)].chr)->state = 0;
                    }
                    (*(u32 *)(obj + 0x2C4))++;
                    if (*(u32 *)(obj + 0x2C4) < list->count) {
                        *(s8 *)(obj + 0x2D8) = 1;
                    } else {
                        InterfaceManager_SetLoadState(iface, 1);
                    }
                } else {
                    chr->state = 3;
                }
            }
        }
    }
    if (obj[0x243] == 0) {
        chr = SLOT_LOAD(DialogChar *, (obj + 0x2DC));
        if (chr != NULL && obj[0x240] == 0) {
            *(s32 *)(obj + 0x2E4) -= (s32)dt;
            if (obj[0x125] != 0 || chr->state != obj[0x2E0] || *(s32 *)(obj + 0x2E4) <= 0) {
                if (chr->state == 1) {
                    chr->state = 2;
                } else if (obj[0x2E0] == 5) {
                    chr->state = 2;
                } else if (obj[0x2E0] == 3 || chr->state == 3) {
                    chr->state = 4;
                }
                obj[0x2E0] = chr->state;
                if (obj[0x2E0] != 3 && obj[0x2E0] != 4) {
                    if (obj[0x2E0] == 5) {
                        *(s32 *)(obj + 0x2E4) = GETP(chr->special)[chr->key].anim.frameTime * GETP(chr->special)[chr->key].anim.numKeys;
                    } else if (obj[0x2E0] == 2) {
                        chr->key = RandomNumberGenerator_GetRandomInteger(0, chr->numTalk);
                        *(s32 *)(obj + 0x2E4) = GETP(chr->talk)[chr->key].anim.frameTime * GETP(chr->talk)[chr->key].anim.numKeys;
                    } else if (obj[0x2E0] == 0) {
                        chr->key = RandomNumberGenerator_GetRandomInteger(0, chr->numIdle);
                        *(s32 *)(obj + 0x2E4) = GETP(chr->idle)[chr->key].anim.frameTime * GETP(chr->idle)[chr->key].anim.numKeys;
                    }
                    obj[0x2F0] = 1;
                }
            }
            if (chr->state == 5) {
                ExtractTextureKey(&GETP(chr->special)[chr->key].anim, (TextureKeyState *)(obj + 0x2E8), dt);
            } else if (chr->state == 2 || chr->state == 3 || chr->state == 4) {
                ExtractTextureKey(&GETP(chr->talk)[chr->key].anim, (TextureKeyState *)(obj + 0x2E8), dt);
            } else {
                ExtractTextureKey(&GETP(chr->idle)[chr->key].anim, (TextureKeyState *)(obj + 0x2E8), dt);
            }
            *(u16 *)(obj + 0x24A) = ((TextureKeyState *)(obj + 0x2E8))->texture;
        }
    }
}

void InterfaceManager_MoveObject(void *iface, void *obj, void *data, f32 dt) {

    if (SLOT_LOAD(void *, ((u8 *)iface + 0x158)) != NULL) {
        InterfaceManager_MoveDialogSystem(iface, obj, dt);
    }
}

void InterfaceManager_ExecuteActions(u8 *iface, u8 *list) {
    typedef struct { u8 pad[0x98]; s32 vars[1]; } IfaceVars;
    void GameSettings_SetCodeVariable(s32 var, s32 value, s32 arg2);
    void InterfaceManager_SetObjectState(u8 *iface, u8 *obj, s32 state, s32 arg3);
    u8 *action;
    u8 *data;
    u8 *obj;
    u8 *op;
    u8 *code;
    s32 a;
    s32 b;
    s32 value;
    u32 i;
    s32 state;

    for (i = 0; i < *(u32 *)list; i++) {
        action = SLOT_LOAD(u8 *, (list + 4)) + i * 8;
        switch (action[0]) {
        case 1:
            data = SLOT_LOAD(u8 *, (action + 4));
            if (*(u32 *)(data + 4) >= *(u32 *)iface) {
                break;
            }
            obj = SLOT_LOAD(u8 *, (iface + 0x88)) + *(u32 *)(data + 4) * 0x300;
            switch (data[0]) {
            case 0:
                state = 0;
                break;
            case 1:
                state = 1;
                break;
            case 2:
                state = 2;
                break;
            case 3:
                state = 3;
                break;
            case 4:
                if (obj[0x240] != 0) {
                    obj[0x240] = 0;
                    obj[0x2C] = 1;
                    obj[0x54] = 1;
                    obj[0x118] = 1;
                    obj[0x84] = 1;
                    obj[0xB4] = 1;
                    obj[0x125] = 1;
                    obj[0x164] = 1;
                    obj[0x194] = 1;
                    obj[0x248] = 1;
                }
                continue;
            case 5:
                obj[0x240] = 1;
                continue;
            }
            switch (data[1]) {
            case 10:
                value = *(u16 *)(data + 2);
                break;
            default:
                value = ((IfaceVars *)iface)->vars[*(u16 *)(data + 2)];
                break;
            }
            InterfaceManager_SetObjectState(iface, obj, state, value);
            break;
        case 2:
            data = SLOT_LOAD(u8 *, (action + 4));
            if (*(s8 *)(op = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, data) + 4)))) {
                a = ((IfaceVars *)iface)->vars[*(u16 *)(op + 2)];
            } else {
                a = *(u16 *)(op + 2);
            }
            if (*(s8 *)(op = SLOT_LOAD(u8 *, (data + 4)))) {
                b = ((IfaceVars *)iface)->vars[*(u16 *)(op + 2)];
            } else {
                b = *(u16 *)(op + 2);
            }
            GameSettings_SetCodeVariable(*SLOT_LOAD(s32 *, data), a, b);
            break;
        case 3:
            op = SLOT_LOAD(u8 *, (action + 4));
            if (*(s8 *)op) {
                ((IfaceVars *)iface)->vars[op[1]] = ((IfaceVars *)iface)->vars[*(u16 *)(op + 2)];
            } else {
                ((IfaceVars *)iface)->vars[op[1]] = *(u16 *)(op + 2);
            }
            break;
        }
    }
}

void InterfaceManager_CheckConditions(u8 *iface, u8 *obj) {
    extern u8 g_interface[][0x170];
    typedef struct {
        s8 isVar;
        u8 pad1;
        u16 value;
    } CondValue;
    typedef struct {
        s32 id;
        PTR32(CondValue) val;
    } CodeVar;
    typedef struct {
        u8 type;
        u8 pad1[3];
        s32 value;
        PTR32(void) data;
        u8 padC[0x1C - 0xC];
    } Condition;
    typedef struct {
        u8 type;
        u32 count;
        PTR32(Condition) conds;
    } ConditionList;
    typedef struct {
        u8 type;
        u8 pad1[3];
        s32 min;
        s32 max;
        s32 offset;
        PTR32(CodeVar) data;
        u8 pad14[0x34 - 0x14];
    } Variable;
    typedef struct {
        u32 count;
        u32 state;
        u8 pad8[0x20 - 0x8];
        PTR32(u8) header;
        u8 pad24[0x88 - 0x24];
        PTR32(u8) objs;
        u8 pad8C[0x98 - 0x8C];
        s32 vars[30];
        u32 numVars;
    } Iface;
    typedef struct {
        u8 pad0[2];
        u8 id;
        u8 pad3;
        f32 value;
    } WorldVar;
    typedef struct {
        u8 pad0[0x48];
        u32 numVars;
        PTR32(WorldVar *) vars;
    } World;
    u32 i;
    ConditionList *list;
    s8 result;
    s8 old;
    s8 show;
    s8 isAnd;
    s8 pass;
    s8 changed;
    Iface *ifc;
    u8 *car;
    u8 *info;
    u32 j;
    u32 k;
    CodeVar *cv;
    u32 sum;

    if (obj == NULL || *(u32 *)(obj + 0x254) == 0 ||
        (list = SLOT_LOAD(ConditionList *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x48))) == NULL) {
        return;
    }
    old = obj[0x240];
    show = (list->type == 1 || list->type == 2);
    isAnd = (list->type == 0 || list->type == 1);
    if (isAnd) {
        result = 1;
    } else {
        result = 0;
    }
    for (i = 0; i < list->count; i++) {
        pass = 1;
        switch (GETP(list->conds)[i].type) {
        case 1:
            if (((CondValue *)GETP(GETP(list->conds)[i].data))->isVar) {
                if (((Iface *)iface)->vars[((Iface *)iface)->vars[((CondValue *)GETP(GETP(list->conds)[i].data))->value]] != GETP(list->conds)[i].value) {
                    pass = 0;
                }
            } else if (((Iface *)iface)->vars[((CondValue *)GETP(GETP(list->conds)[i].data))->value] != GETP(list->conds)[i].value) {
                pass = 0;
            }
            break;
        case 2:
            cv = GETP(GETP(list->conds)[i].data);
            if (GameSettings_GetCodeVariable(cv->id, GETP(cv->val)->isVar ? ((Iface *)iface)->vars[GETP(cv->val)->value] : GETP(cv->val)->value) != GETP(list->conds)[i].value) {
                pass = 0;
            }
            break;
        case 3:
            if (SLOT_LOAD(u8 *, (iface + 8)) == NULL || !(*(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C)) + 0x48C) & 0x80)) {
                pass = 0;
            }
            break;
        case 4:
            if (SLOT_LOAD(u8 *, (iface + 8)) != NULL && (car = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C))) != NULL) {
                info = SLOT_LOAD(u8 *, (car + 0x3BC));
                sum = 0;
                for (j = 0; j < *(u32 *)(info + 0x100); j++) {
                    sum += *(s32 *)(SLOT_LOAD(u8 *, (info + 0x104)) + j * 0x50 + 0x34);
                }
                if (sum != GETP(list->conds)[i].value) {
                    pass = 0;
                }
            } else {
                pass = 0;
            }
            break;
        case 5:
            if (SLOT_LOAD(u8 *, (iface + 8)) != NULL && SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C)) != NULL) {
                pass = 0;
            }
            break;
        }
        if (isAnd && !pass) {
            result = 0;
            break;
        }
        if (!isAnd && pass) {
            result = 1;
            break;
        }
    }
    if (obj[0x243] == 6) {
        ifc = (Iface *)g_interface[*(u32 *)(obj + 0x270)];
        changed = 0;
        if (GameSettings_GetGameType() == 1) {
            return;
        }
        if (result) {
            if (show && ifc->state == 1) {
                InterfaceManager_SetLoadState(ifc, 0);
                changed = 1;
            } else if (!show && ifc->state != 1) {
                InterfaceManager_SetLoadState(ifc, 1);
            }
        } else {
            if (show && ifc->state != 1) {
                InterfaceManager_SetLoadState(ifc, 1);
            } else if (!show && ifc->state == 1) {
                InterfaceManager_SetLoadState(ifc, 0);
                changed = 1;
            }
        }
        if (changed) {
            for (i = 0; i < ifc->numVars; i++) {
                switch ((SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].type) {
                case 1:
                    cv = GETP((SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].data);
                    if (GETP(cv->val)->isVar) {
                        ifc->vars[i] = GameSettings_GetCodeVariable(cv->id, ifc->vars[GETP(cv->val)->value]);
                    } else {
                        ifc->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->val)->value);
                    }
                    ifc->vars[i] += (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].offset;
                    if (ifc->vars[i] > (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].max) {
                        ifc->vars[i] = ifc->vars[i] - (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].max - 1 + (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].min;
                    } else if (ifc->vars[i] < (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].min) {
                        ifc->vars[i] = (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].max - ((SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].min - ifc->vars[i] - 1);
                    }
                    break;
                case 4:
                    if ((World *)GETP(g_worldData.world) != NULL) {
                        ifc->vars[0] = 0;
                        for (k = 0; k < ((World *)GETP(g_worldData.world))->numVars; k++) {
                            if (GETP(((World *)GETP(g_worldData.world))->vars)[k]->id == (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].offset) {
                                ifc->vars[k] = GETP(((World *)GETP(g_worldData.world))->vars)[k]->value;
                            }
                        }
                    }
                    break;
                }
            }
            for (i = 0; i < ifc->count; i++) {
                InterfaceManager_CheckConditions((u8 *)ifc, GETP(ifc->objs) + i * 0x300);
            }
        }
    } else {
        if (result) {
            obj[0x240] = !show;
        } else {
            obj[0x240] = show;
        }
        if (old != obj[0x240]) {
            obj[0x2C] = 1;
            obj[0x54] = 1;
            obj[0x118] = 1;
            obj[0x84] = 1;
            obj[0xB4] = 1;
            obj[0x125] = 1;
            obj[0x164] = 1;
            obj[0x194] = 1;
            obj[0x248] = 1;
        }
    }
}

void InterfaceManager_SetControllerConfigInputString(void) {

}

void InterfaceManager_DrawBox(u8 *obj) {
    f32 width;
    f32 height;
    u32 colour = *(u32 *)(obj + 0x24);
    u32 *box = SLOT_LOAD(u32 *, (obj + 0x258));
    Vector4 origin __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 p0 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 p1 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    char buf[16];
    u32 i;
    u32 step;

    width = 0.5f * box[0];
    height = 0.5f * box[1];
    origin.x = *(f32 *)(obj + 0x60) - width - width * *(f32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x30);
    origin.y = *(f32 *)(obj + 0x64) - height - height * *(f32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x34);
    if (box[2] == 0) {
        box[2] = 1;
    }
    step = box[0] / box[2];
    for (i = 0; i <= box[0]; i += step) {
        p0.y = origin.y;
        p0.x = origin.x + i;
        p1.x = p0.x;
        p1.y = origin.y + box[1];
        Gfx_Add2DFXLine(&p0, &p1, &colour, &colour, 1);
        sprintf(buf, "%d", (u32)p0.x);
        Gfx_FontManager_GetPrintRect(buf, obj[0x1B0], &width, &height, *SLOT_LOAD(f32 *, (box[4] + 0x1C)));
        width *= 0.5f;
        height *= 0.5f;
        Gfx_FontManager_Print(buf, obj[0x1B0], 5, p0.x - width, p0.y - 2.0f * height, *SLOT_LOAD(f32 *, (box[4] + 0x1C)));
    }
    if (box[3] == 0) {
        box[3] = 1;
    }
    step = box[1] / box[3];
    for (i = 0; i <= box[1]; i += step) {
        p0.x = origin.x;
        p0.y = origin.y + i;
        p1.x = origin.x + box[0];
        p1.y = p0.y;
        Gfx_Add2DFXLine(&p0, &p1, &colour, &colour, 1);
        sprintf(buf, "%d", (u32)p0.y);
        Gfx_FontManager_GetPrintRect(buf, obj[0x1B0], &width, &height, *SLOT_LOAD(f32 *, (box[4] + 0x1C)));
        width *= 0.5f;
        height *= 0.5f;
        Gfx_FontManager_Print(buf, obj[0x1B0], 5, p0.x - 2.0f * width, p0.y - height, *SLOT_LOAD(f32 *, (box[4] + 0x1C)));
    }
}

void InterfaceManager_DrawLine(u8 *obj) {
    u32 *line = SLOT_LOAD(u32 *, (obj + 0x258));
    Vector4 p0 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 p1 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 dir __attribute__((aligned(16)));
    f32 pos;
    f32 x;
    f32 y;

    switch (*(u8 *)line) {
    case 0:
        p0.x = 0.0f;
        p0.y = *(f32 *)(obj + 0x64);
        p1.x = 640.0f;
        p1.y = *(f32 *)(obj + 0x64);
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
        if (line[1] != 0) {
            p0.x = *(f32 *)(obj + 0x60) - 10.0f;
            p0.y = *(f32 *)(obj + 0x64) - 10.0f;
            p1.x = *(f32 *)(obj + 0x60) + 10.0f;
            p1.y = *(f32 *)(obj + 0x64) + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            p0.x = *(f32 *)(obj + 0x60) + 10.0f;
            p0.y = *(f32 *)(obj + 0x64) - 10.0f;
            p1.x = *(f32 *)(obj + 0x60) - 10.0f;
            p1.y = *(f32 *)(obj + 0x64) + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            p0.x = *(f32 *)(obj + 0x60);
            p0.y = *(f32 *)(obj + 0x64) - 10.0f;
            p1.x = *(f32 *)(obj + 0x60);
            p1.y = *(f32 *)(obj + 0x64) + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            for (pos = *(f32 *)(obj + 0x60) + line[1]; pos < 640.0f; pos += line[1]) {
                p0.x = pos;
                p0.y = *(f32 *)(obj + 0x64) - 10.0f;
                p1.x = pos;
                p1.y = *(f32 *)(obj + 0x64) + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            }
            for (pos = *(f32 *)(obj + 0x60) - line[1]; pos > 0.0f; pos -= line[1]) {
                p0.x = pos;
                p0.y = *(f32 *)(obj + 0x64) - 10.0f;
                p1.x = pos;
                p1.y = *(f32 *)(obj + 0x64) + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            }
        }
        break;
    case 1:
        p0.x = *(f32 *)(obj + 0x60);
        p0.y = 0.0f;
        p1.x = *(f32 *)(obj + 0x60);
        p1.y = 480.0f;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
        if (line[1] != 0) {
            p0.x = *(f32 *)(obj + 0x60) - 10.0f;
            p0.y = *(f32 *)(obj + 0x64) - 10.0f;
            p1.x = *(f32 *)(obj + 0x60) + 10.0f;
            p1.y = *(f32 *)(obj + 0x64) + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            p0.x = *(f32 *)(obj + 0x60) + 10.0f;
            p0.y = *(f32 *)(obj + 0x64) - 10.0f;
            p1.x = *(f32 *)(obj + 0x60) - 10.0f;
            p1.y = *(f32 *)(obj + 0x64) + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            p0.x = *(f32 *)(obj + 0x60) - 10.0f;
            p0.y = *(f32 *)(obj + 0x64);
            p1.x = *(f32 *)(obj + 0x60) + 10.0f;
            p1.y = *(f32 *)(obj + 0x64);
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            for (pos = *(f32 *)(obj + 0x64) + line[1]; pos < 480.0f; pos += line[1]) {
                p0.x = *(f32 *)(obj + 0x60) - 10.0f;
                p0.y = pos;
                p1.x = *(f32 *)(obj + 0x60) + 10.0f;
                p1.y = pos;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            }
            for (pos = *(f32 *)(obj + 0x64) - line[1]; pos > 0.0f; pos -= line[1]) {
                p0.x = *(f32 *)(obj + 0x60) - 10.0f;
                p0.y = pos;
                p1.x = *(f32 *)(obj + 0x60) + 10.0f;
                p1.y = pos;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
            }
        }
        break;
    case 2:
        p0.x = 0.0f;
        p0.y = 480.0f;
        p1.x = 640.0f;
        p1.y = 0.0f;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
        dir.x = p1.x - p0.x;
        dir.y = p1.y - p0.y;
        dir.z = 0.0f;
        if (PSVECSquareMag(&dir)) {
            PSVECNormalize(&dir, &dir);
        }
        if (line[1] != 0) {
            x = *(f32 *)(obj + 0x60);
            y = *(f32 *)(obj + 0x64);
            while (x < 640.0f || y < 480.0f) {
                p0.x = x - 10.0f;
                p0.y = y;
                p1.x = x + 10.0f;
                p1.y = y;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
                p0.x = x;
                p0.y = y - 10.0f;
                p1.x = x;
                p1.y = y + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
                break;
            }
        }
        break;
    case 3:
        p0.x = 0.0f;
        p0.y = 0.0f;
        p1.x = 640.0f;
        p1.y = 480.0f;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
        if (line[1] != 0) {
            x = *(f32 *)(obj + 0x60);
            y = *(f32 *)(obj + 0x64);
            while (x < 640.0f || y < 480.0f) {
                p0.x = x - 10.0f;
                p0.y = y;
                p1.x = x + 10.0f;
                p1.y = y;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
                p0.x = x;
                p0.y = y - 10.0f;
                p1.x = x;
                p1.y = y + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(obj + 0x24), (u32 *)(obj + 0x24), 1);
                break;
            }
        }
        break;
    }
}

typedef struct RenderFontData {
    u8 pad0[0x9];
    u8 spacing;
    u8 padA[0x1C - 0xA];
    PTR32(f32) width;
} RenderFontData;

typedef struct RenderFontInfo {
    u8 pad0[0x4];
    PTR32(RenderFontData) data;
    u8 pad8[0xF - 0x8];
    u8 useScale;
    u8 hidden;
} RenderFontInfo;

typedef struct RenderObject {
    u8 unk0;
    u8 type;
    u8 pad2[0x14 - 0x2];
    PTR32(RenderFontInfo) font;
    u8 pad18[0x30 - 0x18];
    f32 align;
} RenderObject;

typedef struct RenderSlot {
    u8 pad0[0x60];
    Vector4 pos;
    u8 pad70[0x180 - 0x70];
    f32 scale;
    u8 pad184[0x1A4 - 0x184];
    PTR32(char) textPtr;
    u8 pad1A8[0x1C0 - 0x1A8];
    char text[0x80];
    u8 hidden;
    u8 pad241[0x243 - 0x241];
    u8 type;
    u8 pad244;
    u8 kind;
    u8 pad246[0x25C - 0x246];
    PTR32(RenderObject) object;
    u8 pad260[0x300 - 0x260];
} RenderSlot;

typedef struct RenderInterface {
    u32 count;
    u8 pad4[0xC - 0x4];
    PTR32(RenderSlot) parent;
    u8 pad10[0x60 - 0x10];
    Vector4 offset;
    u8 pad70[0x7F - 0x70];
    u8 active;
    u8 pad80[0x82 - 0x80];
    u8 hasOffset;
    u8 paused;
    u8 pad84[0x88 - 0x84];
    PTR32(RenderSlot) slots;
} RenderInterface;

void InterfaceManager_Render(void) {
    extern u8 g_interface[][0x170];
    extern u32 g_interfaceRenderOrder[0x12];
    extern s32 g_numInterfacesToRender;
    void InterfaceManager_RenderInterface(RenderInterface *iface);
    u32 i;
    u8 *iface;

    for (i = 0; i < g_numInterfacesToRender; i++) {
        iface = g_interface[g_interfaceRenderOrder[i]];
        if (SLOT_LOAD(void *, iface) != NULL && (*(u32 *)(iface + 4) != 1 || *(u32 *)(iface + 0x10) != 0) && iface[0x7F] == 0) {
            InterfaceManager_RenderInterface((RenderInterface *)iface);
        }
    }
}

void InterfaceManager_RenderInterface(RenderInterface *iface) {
    u32 i;
    Vector4 saved __attribute__((aligned(16)));
    Vector4 parentSaved __attribute__((aligned(16)));
    RenderFontInfo *font;
    RenderFontData *fontData;
    u32 len;
    s32 width;
    f32 scale;
    RenderSlot *slot;

    if (iface->active && iface->paused) {
        return;
    }
    for (i = 0; i < iface->count; i++) {
        slot = &GETP(iface->slots)[i];
        if (iface->hasOffset) {
            saved = slot->pos;
            slot->pos.x += iface->offset.x;
            slot->pos.y += iface->offset.y;
            slot->pos.z += iface->offset.z;
        }
        if (slot->kind == 3) {
            if (GETP(iface->parent) != NULL && GETP(iface->parent)->hidden) {
                continue;
            }
            if (slot->type == 6) {
                InterfaceManager_RenderObject(iface, i);
            } else if (GETP(iface->parent) != NULL) {
                parentSaved = slot->pos;
                slot->pos.x += GETP(iface->parent)->pos.x;
                slot->pos.y += GETP(iface->parent)->pos.y;
                slot->pos.z += GETP(iface->parent)->pos.z;
                if (GETP(GETP(iface->parent)->object)->type == 1 && GETP(slot->object)->align) {
                    font = GETP(GETP(GETP(iface->parent)->object)->font);
                    if (font != NULL && !font->hidden) {
                        scale = 1.0f;
                        if (GETP(GETP(iface->parent)->textPtr) != NULL) {
                            len = strlen(GETP(GETP(iface->parent)->textPtr));
                        } else {
                            len = strlen(GETP(iface->parent)->text);
                        }
                        fontData = GETP(font->data);
                        width = *GETP(fontData->width);
                        if (font->useScale) {
                            scale = GETP(iface->parent)->scale;
                        }
                        slot->pos.x = scale * (f32)(len * (fontData->spacing + width)) *
                                          (GETP(slot->object)->align <= 0.0f ? -(0.5f * GETP(GETP(iface->parent)->object)->align + 0.5f)
                                                                       : -(0.5f * GETP(GETP(iface->parent)->object)->align - 0.5f)) +
                                      slot->pos.x;
                    }
                }
                InterfaceManager_RenderObject(iface, i);
                slot->pos = parentSaved;
            }
        } else {
            InterfaceManager_RenderObject(iface, i);
        }
        if (iface->hasOffset) {
            slot->pos = saved;
        }
    }
}

void InterfaceManager_RenderObject(u8 *iface, s32 index) {
}

void InterfaceManager_DrawObject_Standard_Geometry(u8 *object, u8 *iface) {
    typedef struct InterfaceObject {
        u8 pad0[0x30];
        Vector4 scale;          /* 0x30 */
        u8 pad40[0x20];
        Vector4 pos;            /* 0x60 */
        u8 pad70[0x20];
        Vector4 rot;            /* 0x90 degrees */
        u8 padA0[0x245 - 0xA0];
        u8 type;                /* 0x245 */
        u8 pad246[0x268 - 0x246];
        PTR32(void) geom;             /* 0x268 */
        u8 pad26C[0x300 - 0x26C];
    } InterfaceObject;
    extern s32 g_TargetArrow[7];
    extern Matrix4 g_identity;
    InterfaceObject *obj = (InterfaceObject *)object;
    Matrix4 mtx;
    Matrix4 rot;
    Matrix4 out;
    Matrix4 view;
    Matrix4 tmp;
    Vector4 camPos;
    Vector4 at;
    Vector4 up;
    Vector4 right;
    Vector4 toTarget;
    Vector4 target;
    InterfaceObject *parent;
    u32 i;
    f32 a;
    f32 b;
    f32 c;
    f32 t;
    f32 scale;
    f32 cosA;
    f32 sinA;
    f32 s;

    if (GETP(obj->geom) == NULL) {
        return;
    }
    if (obj->type == 0x1F) {
        s = (f32)sin(0.5f * (0.017453292f * Gfx_GetProjectionMatrix()));
        if ((Vector4 *)PTR(g_TargetArrow[0]) == NULL || GameSettings_GetCodeVariable(0x198, 0)) {
            return;
        }
        scale = 0.1f;
        Gfx_GetCameraPosition(&camPos);
        Gfx_GetCameraVector(&at, &up, &right);
        a = -obj->pos.x * scale;
        b = scale * (1.1f * -s * obj->pos.z);
        c = scale * Sqrt(225.0f - s * (s * (obj->pos.z * obj->pos.z)));
        mtx.v[0].x = -right.x;
        mtx.v[0].y = -right.y;
        mtx.v[0].z = -right.z;
        mtx.v[1].x = up.x;
        mtx.v[1].y = up.y;
        mtx.v[1].z = up.z;
        mtx.v[2].x = -at.x;
        mtx.v[2].y = -at.y;
        mtx.v[2].z = -at.z;
        mtx.v[3].x = camPos.x - (mtx.v[0].x * a + mtx.v[1].x * b + mtx.v[2].x * c);
        mtx.v[3].y = camPos.y - (mtx.v[0].y * a + mtx.v[1].y * b + mtx.v[2].y * c);
        mtx.v[3].z = camPos.z - (mtx.v[0].z * a + mtx.v[1].z * b + mtx.v[2].z * c);
        mtx.v[0].w = mtx.v[1].w = mtx.v[2].w = 0.0f;
        mtx.v[3].w = 1.0f;
        target = TargetArrow_GetTargetPos();
        if ((Vector4 *)PTR(g_TargetArrow[2]) != NULL && (u32)g_TargetArrow[6] < 19) {
            t = (u32)++g_TargetArrow[6] / 20.0f;
            target.x = t * (target.x - ((Vector4 *)PTR(g_TargetArrow[2]))->x) + ((Vector4 *)PTR(g_TargetArrow[2]))->x;
            target.y = t * (target.y - ((Vector4 *)PTR(g_TargetArrow[2]))->y) + ((Vector4 *)PTR(g_TargetArrow[2]))->y;
            target.z = t * (target.z - ((Vector4 *)PTR(g_TargetArrow[2]))->z) + ((Vector4 *)PTR(g_TargetArrow[2]))->z;
        }
        toTarget.x = target.x - mtx.v[3].x;
        toTarget.y = target.y - mtx.v[3].y;
        toTarget.z = target.z - mtx.v[3].z;
        toTarget.y = at.y = right.y = 0.0f;
        if (PSVECSquareMag(&toTarget)) {
            PSVECNormalize(&toTarget, &toTarget);
        }
        if (PSVECSquareMag(&at)) {
            PSVECNormalize(&at, &at);
        }
        if (PSVECSquareMag(&right)) {
            PSVECNormalize(&right, &right);
        }
        cosA = -PSVECDotProduct(&at, &toTarget);
        sinA = -PSVECDotProduct(&right, &toTarget);
        memcpy(&rot, &g_identity, sizeof(Matrix4));
        rot.v[0].x = cosA * scale;
        rot.v[0].z = -sinA * scale;
        rot.v[1].x = 0.0f;
        rot.v[1].y = scale;
        rot.v[1].z = 0.0f;
        rot.v[2].x = sinA * scale;
        rot.v[2].z = cosA * scale;
        MultiplyMatrix(&rot, &mtx, &out);
        Gfx_DrawGeometry(HND(GETP(obj->geom)), HND(&out));
        return;
    }
    parent = NULL;
    for (i = 0; i < *(u32 *)iface; i++) {
        if ((SLOT_LOAD(InterfaceObject *, (iface + 0x88)))[i].type == 4) {
            parent = &(SLOT_LOAD(InterfaceObject *, (iface + 0x88)))[i];
            break;
        }
    }
    Gfx_GetCameraPosition(&camPos);
    Gfx_GetCameraVector(&at, &up, &right);
    if (right.x == 0.0f && right.y == 0.0f && right.z == 0.0f) {
        PSVECCrossProduct(&up, &at, &right);
    }
    view.v[0].x = right.x;
    view.v[0].y = right.y;
    view.v[0].z = right.z;
    view.v[1].x = up.x;
    view.v[1].y = up.y;
    view.v[1].z = up.z;
    view.v[2].x = at.x;
    view.v[2].y = at.y;
    view.v[2].z = at.z;
    view.v[0].w = view.v[1].w = view.v[2].w = 0.0f;
    view.v[3].w = 1.0f;
    if (parent != NULL) {
        view.v[3].x = view.v[0].x * (camPos.x - parent->pos.x) + view.v[1].x * (camPos.y - parent->pos.y) + view.v[2].x * (camPos.z - parent->pos.z);
        view.v[3].y = view.v[0].y * (camPos.x - parent->pos.x) + view.v[1].y * (camPos.y - parent->pos.y) + view.v[2].y * (camPos.z - parent->pos.z);
        view.v[3].z = view.v[0].z * (camPos.x - parent->pos.x) + view.v[1].z * (camPos.y - parent->pos.y) + view.v[2].z * (camPos.z - parent->pos.z);
        SetOrientation(&rot, 0.017453292f * (obj->rot.y + parent->rot.y),
                       0.017453292f * (obj->rot.x + parent->rot.x),
                       0.017453292f * (obj->rot.z + parent->rot.z));
    } else {
        view.v[3].x = view.v[0].x * camPos.x + view.v[1].x * camPos.y + view.v[2].x * camPos.z;
        view.v[3].y = view.v[0].y * camPos.x + view.v[1].y * camPos.y + view.v[2].y * camPos.z;
        view.v[3].z = view.v[0].z * camPos.x + view.v[1].z * camPos.y + view.v[2].z * camPos.z;
        SetOrientation(&rot, 0.017453292f * obj->rot.y, 0.017453292f * obj->rot.x, 0.017453292f * obj->rot.z);
    }
    rot.v[3].x = obj->pos.x;
    rot.v[3].y = obj->pos.y;
    rot.v[3].z = obj->pos.z;
    memcpy(&mtx, &g_identity, sizeof(Matrix4));
    mtx.v[0].x = obj->scale.x;
    mtx.v[1].y = obj->scale.y;
    mtx.v[2].z = obj->scale.z;
    MultiplyMatrix(&mtx, &rot, &tmp);
    MultiplyMatrix(&tmp, &view, &out);
    Gfx_DrawGeometry(HND(GETP(obj->geom)), HND(&out));
}

void InterfaceManager_DrawObject_Standard_Image(u8 *obj) {
    typedef struct RectVtx {
        f32 x;
        f32 y;
        f32 z;
        f32 pad;
        f32 s;
        f32 t;
        u32 color;
        u32 pad2;
    } RectVtx;
    typedef struct Rect {
        RectVtx v[4];
        u16 tex;
        u8 blendMode;
        u8 window;
    } Rect;
    Rect *Gfx_Add2DRectangle(void);
    Rect local;
    Rect *rect;
    f32 x0, y0;
    f32 s, c;
    f32 tx, ty;
    u8 *anchor;

    if (*(u16 *)(obj + 0x24A) == 0xFFFF) {
        return;
    }
    if (*(f32 *)(obj + 0x68) != 0.0f) {
        rect = &local;
    } else {
        rect = Gfx_Add2DRectangle();
    }
    rect->blendMode = obj[0x241];
    rect->tex = *(u16 *)(obj + 0x24A);
    anchor = SLOT_LOAD(u8 *, (obj + 0x25C));
    x0 = 0.5f * *(f32 *)(obj + 0x30) + 0.5f * (*(f32 *)(anchor + 0x30) * *(f32 *)(obj + 0x30));
    y0 = 0.5f * *(f32 *)(obj + 0x34) + 0.5f * (*(f32 *)(anchor + 0x34) * *(f32 *)(obj + 0x34));
    x0 = -x0;
    y0 = -y0;
    rect->v[0].x = x0;
    rect->v[0].y = y0;
    rect->v[1].x = x0 + *(f32 *)(obj + 0x30);
    rect->v[1].y = y0;
    rect->v[2].x = x0 + *(f32 *)(obj + 0x30);
    rect->v[2].y = y0 + *(f32 *)(obj + 0x34);
    rect->v[3].x = x0;
    rect->v[3].y = y0 + *(f32 *)(obj + 0x34);
    rect->v[0].s = *(f32 *)(obj + 0xC0);
    rect->v[0].t = *(f32 *)(obj + 0xDC);
    rect->v[1].s = *(f32 *)(obj + 0xD8);
    rect->v[1].t = *(f32 *)(obj + 0xDC);
    rect->v[2].s = *(f32 *)(obj + 0xD8);
    rect->v[2].t = *(f32 *)(obj + 0xC4);
    rect->v[3].s = *(f32 *)(obj + 0xC0);
    rect->v[3].t = *(f32 *)(obj + 0xC4);
    rect->v[0].color = *(u32 *)(obj + 0x24);
    rect->v[1].color = *(u32 *)(obj + 0x24);
    rect->v[2].color = *(u32 *)(obj + 0x24);
    rect->v[3].color = *(u32 *)(obj + 0x24);
    if (*(f32 *)(obj + 0x90)) {
        SinCos(&s, &c, *(f32 *)(obj + 0x90));
        tx = rect->v[0].x;
        ty = rect->v[0].y;
        rect->v[0].x = *(f32 *)(obj + 0x60) + (tx * c - ty * s);
        rect->v[0].y = *(f32 *)(obj + 0x64) + (tx * s + ty * c);
        tx = rect->v[1].x;
        ty = rect->v[1].y;
        rect->v[1].x = *(f32 *)(obj + 0x60) + (tx * c - ty * s);
        rect->v[1].y = *(f32 *)(obj + 0x64) + (tx * s + ty * c);
        tx = rect->v[2].x;
        ty = rect->v[2].y;
        rect->v[2].x = *(f32 *)(obj + 0x60) + (tx * c - ty * s);
        rect->v[2].y = *(f32 *)(obj + 0x64) + (tx * s + ty * c);
        tx = rect->v[3].x;
        ty = rect->v[3].y;
        rect->v[3].x = *(f32 *)(obj + 0x60) + (tx * c - ty * s);
        rect->v[3].y = *(f32 *)(obj + 0x64) + (tx * s + ty * c);
    } else {
        rect->v[0].x += *(f32 *)(obj + 0x60);
        rect->v[0].y += *(f32 *)(obj + 0x64);
        rect->v[1].x += *(f32 *)(obj + 0x60);
        rect->v[1].y += *(f32 *)(obj + 0x64);
        rect->v[2].x += *(f32 *)(obj + 0x60);
        rect->v[2].y += *(f32 *)(obj + 0x64);
        rect->v[3].x += *(f32 *)(obj + 0x60);
        rect->v[3].y += *(f32 *)(obj + 0x64);
    }
    if (*(f32 *)(obj + 0x68) != 0.0f) {
        Gfx_Add2DRectangleBack(rect);
    }
}

void InterfaceManager_DrawRadarBlips(f32 *centre, u8 *obj) {
    typedef struct RectVtx {
        f32 x;
        f32 y;
        f32 z;
        f32 pad;
        f32 s;
        f32 t;
        u32 color;
        u32 pad2;
    } RectVtx;
    typedef struct Rect {
        RectVtx v[4];
        u16 tex;
        u8 blendMode;
        u8 window;
    } Rect;
    typedef struct Colour {
        u8 r, g, b, a;
    } Colour;
    typedef struct BlipThing {
        u8 unk0[0x74];
        u32 flags;
        u8 unk78[0xB0 - 0x78];
        PTR32(u8) desc;
        u8 unkB4[0xC0 - 0xB4];
        f32 x;
        f32 y;
        f32 z;
        u8 unkCC[0xD0 - 0xCC];
    } BlipThing;
    typedef struct BlipManager {
        u8 unk0[0x77770];
        BlipThing things[64];
    } BlipManager;
    extern u32 g_radarBlip;
    Rect *Gfx_Add2DRectangle(void);
    void *targets[32];
    f32 c, s;
    Colour colour;
    u8 *player;
    u32 count;
    u32 i;
    BlipThing *thing;
    Rect *rect;
    f32 u1, u0;
    f32 dx, dz;
    f32 dist;
    f32 scale;
    f32 x, y;

    count = GetAllTargets(targets);
    player = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 8)) + 0x2C));
    if (obj[0x83] == 0) {
        g_radarBlip++;
    }
    if (g_radarBlip > 15) {
        g_radarBlip = 0;
    }
    u0 = 0.25f * (g_radarBlip >> 2);
    if (player == NULL || count == 0) {
        return;
    }
    SinCosRad(&s, &c, ArcTan(*(f32 *)(player + 0x38), *(f32 *)(player + 0x30)));
    s *= -1.0f;
    c *= -1.0f;
    u1 = 0.25f + u0;
    for (i = 0; i < 64; i++) {
        if (((BlipManager *)&g_thingManager_DARYL)->things[i].flags & 0x1000) {
            continue;
        }
        thing = &((BlipManager *)&g_thingManager_DARYL)->things[i];
        dx = 0.05f * (thing->x - *(f32 *)(player + 0x40));
        dz = 0.05f * (thing->z - *(f32 *)(player + 0x48));
        colour = *SLOT_LOAD(Colour *, (GETP(thing->desc) + 0x1C));
        rect = Gfx_Add2DRectangle();
        dist = dx * dx + dz * dz;
        if (dist > 2209.0f) {
            scale = 47.0f / Sqrt(dist);
            colour.g = 0;
            colour.b = 0;
            x = scale * (dx * s - dz * c) + centre[0];
            y = scale * (dx * c + dz * s) + centre[1];
        } else {
            x = centre[0] + (dx * s - dz * c);
            y = centre[1] + (dx * c + dz * s);
        }
        rect->v[0].x = x - 4.0f;
        rect->v[0].y = y - 4.0f;
        rect->v[1].x = 4.0f + x;
        rect->v[1].y = y - 4.0f;
        rect->v[2].x = 4.0f + x;
        rect->v[2].y = 4.0f + y;
        rect->v[3].x = x - 4.0f;
        rect->v[3].y = 4.0f + y;
        rect->v[0].color = *(u32 *)&colour;
        rect->v[1].color = *(u32 *)&colour;
        rect->v[2].color = *(u32 *)&colour;
        rect->v[3].color = *(u32 *)&colour;
        rect->blendMode = 2;
        rect->tex = *(u32 *)(GETP(thing->desc) + 0x14);
        rect->v[0].s = u0;
        rect->v[0].t = 0.75f;
        rect->v[1].s = u1;
        rect->v[1].t = 0.75f;
        rect->v[2].s = u1;
        rect->v[2].t = 0.5f;
        rect->v[3].s = u0;
        rect->v[3].t = 0.5f;
    }
}

s32 ActionInterfacePlay(s32 *arg0) {
    InterfaceManager_Create(PTR(*arg0), 0, 0);
    return 1;
}

s32 ActionHUDChangeMode(u32 *action) {
    extern u8 g_cameraManager[];
    s32 i;

    for (i = 0; i < g_cameraManager[0x640]; i++) {
        if (action[1] == 1) {
            Player_RefreshInterface(SLOT_LOAD(void *, (u8 *)(g_cameraManager + i * 0x190) + 0));
        } else {
            InterfaceManager_ReleaseInterface(M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(g_cameraManager + i * 0x190) + 0), u32 *, 0x68));
            M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(g_cameraManager + i * 0x190) + 0), s32 *, 0x68) = -1;
        }
    }
    return 1;
}

void InterfaceManager_DrawMap(u8 *obj, u8 *iface) {
    typedef struct RectVtx {
        f32 x;
        f32 y;
        f32 z;
        f32 pad;
        f32 s;
        f32 t;
        u32 color;
        u32 pad2;
    } RectVtx;
    typedef struct Rect {
        RectVtx v[4];
        u16 tex;
        u8 blendMode;
        u8 window;
    } Rect;
    typedef struct Colour {
        u8 r, g, b, a;
    } Colour;
    Rect *Gfx_Add2DRectangle(void);
    Colour colour;
    f32 s;
    f32 c;
    Rect *rect;
    f32 x, z;
    f32 scaleX, scaleZ;
    f32 offsetX, offsetZ;
    f32 halfW, halfH;
    u8 *player;

    if (SLOT_LOAD(u8 *, (iface + 8)) != NULL && SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C)) != NULL &&
        *(u32 *)((u8 *)World_GetGameData() + 0x78) != 0) {
        x = *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C)) + 0x40);
        z = *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C)) + 0x48);
    } else {
        return;
    }
    scaleX = *(f32 *)((u8 *)World_GetGameData() + 0x4);
    scaleZ = *(f32 *)((u8 *)World_GetGameData() + 0x8);
    offsetX = *(f32 *)((u8 *)World_GetGameData() + 0x58);
    offsetZ = *(f32 *)((u8 *)World_GetGameData() + 0x5C);
    halfW = *(f32 *)((u8 *)World_GetGameData() + 0x60);
    halfH = *(f32 *)((u8 *)World_GetGameData() + 0x64);
    rect = Gfx_Add2DRectangle();
    rect->blendMode = 2;
    rect->tex = *(u32 *)((u8 *)World_GetGameData() + 0x78);
    rect->v[0].x = *(f32 *)(obj + 0x60);
    rect->v[0].y = *(f32 *)(obj + 0x64);
    rect->v[1].x = *(f32 *)(obj + 0x60) + *(f32 *)(obj + 0x30);
    rect->v[1].y = *(f32 *)(obj + 0x64);
    rect->v[2].x = *(f32 *)(obj + 0x60) + *(f32 *)(obj + 0x30);
    rect->v[2].y = *(f32 *)(obj + 0x64) + *(f32 *)(obj + 0x34);
    rect->v[3].x = *(f32 *)(obj + 0x60);
    rect->v[3].y = *(f32 *)(obj + 0x64) + *(f32 *)(obj + 0x34);
    x = offsetX + x / scaleX;
    z = offsetZ + -z / scaleZ;
    player = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 8)) + 0x2C));
    SinCosRad(&s, &c, 2.3561945f + ArcTan(*(f32 *)(player + 0x38), *(f32 *)(player + 0x30)));
    rect->v[0].s = x - halfW * c;
    rect->v[0].t = halfH * s + z;
    rect->v[1].s = halfW * s + x;
    rect->v[1].t = halfH * c + z;
    rect->v[2].s = halfW * c + x;
    rect->v[2].t = z - halfH * s;
    rect->v[3].s = x - halfW * s;
    rect->v[3].t = z - halfH * c;
    rect->v[0].color = *(u32 *)(obj + 0x24);
    rect->v[1].color = *(u32 *)(obj + 0x24);
    rect->v[2].color = *(u32 *)(obj + 0x24);
    rect->v[3].color = *(u32 *)(obj + 0x24);
    rect = Gfx_Add2DRectangle();
    rect->blendMode = 2;
    rect->tex = 0xFFFF;
    colour.r = colour.g = colour.b = 0xFF;
    colour.a = 0xFF;
    rect->v[0].x = *(f32 *)(obj + 0x30) / 2.0f + *(f32 *)(obj + 0x60) - 1.0f;
    rect->v[0].y = *(f32 *)(obj + 0x34) / 2.0f + *(f32 *)(obj + 0x64) - 1.0f;
    rect->v[1].x = 1.0f + (*(f32 *)(obj + 0x30) / 2.0f + *(f32 *)(obj + 0x60));
    rect->v[1].y = *(f32 *)(obj + 0x34) / 2.0f + *(f32 *)(obj + 0x64) - 1.0f;
    rect->v[2].x = 1.0f + (*(f32 *)(obj + 0x30) / 2.0f + *(f32 *)(obj + 0x60));
    rect->v[2].y = 1.0f + (*(f32 *)(obj + 0x34) / 2.0f + *(f32 *)(obj + 0x64));
    rect->v[3].x = *(f32 *)(obj + 0x30) / 2.0f + *(f32 *)(obj + 0x60) - 1.0f;
    rect->v[3].y = 1.0f + (*(f32 *)(obj + 0x34) / 2.0f + *(f32 *)(obj + 0x64));
    rect->v[0].s = 0.0f;
    rect->v[0].t = 1.0f;
    rect->v[1].s = 1.0f;
    rect->v[1].t = 1.0f;
    rect->v[2].s = 1.0f;
    rect->v[2].t = 0.0f;
    rect->v[3].s = 0.0f;
    rect->v[3].t = 0.0f;
    rect->v[0].color = *(u32 *)&colour;
    rect->v[1].color = *(u32 *)&colour;
    rect->v[2].color = *(u32 *)&colour;
    rect->v[3].color = *(u32 *)&colour;
}

s32 ActionTargetArrowSetTarget(void *params) {
    extern s32 g_TargetArrow[7];
    extern Vector4 g_zeroVector;
    typedef struct {
        u8 unk0[0x10];
        PTR32(u8) unk10;
        u8 unk14[0x10];
        PTR32(u8) unk24;
        PTR32(u8) unk28;
        PTR32(u8) unk2C;
    } TargetObject;
    typedef struct {
        u32 type;
        PTR32(TargetObject) obj;
    } TargetParams;
    TargetParams *p = params;
    void *target;

    g_TargetArrow[3] = 0;
    switch (p->type) {
    case 1:
        if (GETP(GETP(p->obj)->unk28) != NULL) {
            target = GETP(GETP(p->obj)->unk28) + 0x40;
        } else {
            target = GETP(p->obj);
        }
        break;
    case 4:
        if (GETP(GETP(p->obj)->unk10) != NULL) {
            target = GETP(GETP(p->obj)->unk10) + 0x40;
        } else {
            target = &g_zeroVector;
        }
        break;
    case 2:
        if (GETP(GETP(p->obj)->unk2C) != NULL) {
            target = GETP(GETP(p->obj)->unk2C) + 0x40;
        } else {
            target = GETP(p->obj);
        }
        break;
    case 3:
        if (GETP(GETP(p->obj)->unk24) != NULL) {
            g_TargetArrow[3] = (s32)HND(GETP(GETP(p->obj)->unk24));
            target = GETP(GETP(p->obj)->unk24) + 0x40;
        } else {
            target = GETP(p->obj);
        }
        break;
    case 6:
        target = GETP(p->obj);
        break;
    case 0:
    case 5:
    default:
        target = NULL;
        break;
    }
    if ((void *)PTR(g_TargetArrow[0]) != target) {
        g_TargetArrow[0] = (s32)HND(target);
        g_TargetArrow[4] = -1;
        g_TargetArrow[2] = 0;
    }
    return 1;
}


s32 InterfaceManager_GetTargetArrowNearestNavPoint(void)
{
    extern s32 g_TargetArrow[7];

    if (g_TargetArrow[4] == -1 || g_TargetArrow[5] <= 0)
    {
        g_TargetArrow[4] = NavHolder_FindIndexOfNearestNavPoint((void *)PTR(g_TargetArrow[0]), g_TargetArrow[4]);
        g_TargetArrow[5] = 300;
    }
    return g_TargetArrow[4];
}

s32 DropShip_IsChildInterfaceReady(s32 index) {
    typedef struct DropShipSlot {
        u8 pad0[0x243];
        u8 type;
        u8 pad244[0x270 - 0x244];
        s32 child;
        u8 pad274[0x300 - 0x274];
    } DropShipSlot;
    extern u8 g_interface[][0x170];
    u32 i;
    u8 *iface = g_interface[index];

    for (i = 0; i < *(u32 *)iface; i++) {
        if ((SLOT_LOAD(DropShipSlot *, (iface + 0x88)))[i].type == 6) {
            u8 *child = g_interface[(SLOT_LOAD(DropShipSlot *, (iface + 0x88)))[i].child];

            if (child[0x83] == 0 &&
                (*(u32 *)(child + 4) == 0 ||
                 (*(u32 *)(child + 4) == 1 && *(u32 *)(child + 0x10) != 0))) {
                return 0;
            }
        }
    }
    return 1;
}

void DropShip_StartChildInterface(s32 index, u32 id) {
    typedef struct DropShipSlot {
        u8 pad0[0x243];
        u8 type;
        u8 pad244[0x25C - 0x244];
        PTR32(u8) anim;
        u8 pad260[0x270 - 0x260];
        s32 child;
        u8 pad274[0x300 - 0x274];
    } DropShipSlot;
    typedef struct Interface {
        u32 count;
        u32 loadState;
        u8 pad8[0x83 - 0x8];
        u8 flag83;
        u8 pad84[0x88 - 0x84];
        PTR32(DropShipSlot) slots;
        u8 pad8C[0x170 - 0x8C];
    } Interface;
    extern Interface g_interface[];
    u16 animId;
    u32 i;
    Interface *iface;

    iface = &g_interface[index];
    for (i = 0; i < iface->count; i++) {
        if (GETP(iface->slots)[i].type == 6) {
            animId = *(u16 *)(GETP(GETP(iface->slots)[i].anim) + 0xA);
            if ((animId == id) && (g_interface[GETP(iface->slots)[i].child].loadState != 0)) {
                g_interface[GETP(iface->slots)[i].child].flag83 = 0;
                InterfaceManager_SetLoadState(&g_interface[GETP(iface->slots)[i].child], 0);
            } else {
                g_interface[GETP(iface->slots)[i].child].flag83 = 1;
                g_interface[GETP(iface->slots)[i].child].loadState = 2;
            }
        }
    }
}


void DropShip_EndChildInterface(s32 index, u32 id)
{
    typedef struct DropShipSlot
    {
        u8 pad0[0x243];
        u8 type;
        u8 pad244[0x25C - 0x244];
        PTR32(u8) anim;
        u8 pad260[0x270 - 0x260];
        s32 child;
        u8 pad274[0x300 - 0x274];
    } DropShipSlot;
    extern u8 g_interface[][0x170];
    u32 i;
    u8 *iface = g_interface[index];
    DropShipSlot *slot;

    for (i = 0; i < *(u32 *)iface; i++)
    {
        slot = &(SLOT_LOAD(DropShipSlot *, (iface + 0x88)))[i];
        if (slot->type == 6 && *(u16 *)(GETP(slot->anim) + 0xA) == id &&
            *(u32 *)(g_interface[slot->child] + 4) != 1)
        {
            InterfaceManager_SetLoadState(g_interface[slot->child], 1);
        }
    }
}

void DropShip_EndInterface(s32 index) {
    extern u8 g_interface[][0x170];
    u8 *iface = g_interface[index];

    *(u16 *)PTR((*(u32 *)PTR((*(u32 *)(iface + 0x1C) + 8)) + 6)) = 8;
    if (*(u32 *)(iface + 4) != 1) {
        InterfaceManager_SetLoadState(iface, 1);
        InterfaceManager_MoveInterface(iface, 0.0f);
    }
}

void DropShip_PullCameraBack(s32 index) {
    typedef struct ObjectEntry {
        u8 type;
        u8 pad1[3];
        s32 value;
        u8 pad8[4];
    } ObjectEntry;
    typedef struct ObjectInfo {
        u8 pad0[0x64];
        u32 count;
        u8 pad68[4];
    } ObjectInfo;
    typedef struct InterfaceSlot {
        u8 pad0[0x245];
        u8 type;
        u8 pad246[0x25C - 0x246];
        PTR32(u8) object;
        u8 pad260[0x300 - 0x260];
    } InterfaceSlot;
    typedef struct Interface {
        u32 count;
        u8 pad4[0x18 - 0x4];
        f32 camDistance;
        PTR32(u8) ptr1C;
        u8 pad20[0x88 - 0x20];
        PTR32(InterfaceSlot) slots;
        u8 pad8C[0x170 - 0x8C];
    } Interface;
    extern Interface g_interface[];
    void InterfaceManager_SetObjectState(Interface *iface, InterfaceSlot *slot, s32 state, s32 arg3);
    ObjectInfo *info;
    u32 i;
    u32 j;
    Interface *iface;

    iface = &g_interface[index];
    for (i = 0; i < iface->count; i++) {
        if (GETP(iface->slots)[i].type == 4) {
            InterfaceManager_SetObjectState(iface, &GETP(iface->slots)[i], 2, 0);
            info = (ObjectInfo *)GETP(GETP(iface->slots)[i].object);
            for (j = 0; j < info->count; j++) {
                if ((SLOT_LOAD(ObjectEntry *, (GETP(GETP(iface->slots)[i].object) + 0x68)))[j].type == 2) {
                    iface->camDistance = (f32)(SLOT_LOAD(ObjectEntry *, (GETP(GETP(iface->slots)[i].object) + 0x68)))[j].value;
                    break;
                }
            }
            *(u16 *)(SLOT_LOAD(u8 *, (GETP(iface->ptr1C) + 8)) + 6) = 9;
        }
    }
}

void DropShip_PullCameraForward(s32 index) {
    typedef struct ObjectEntry {
        u8 type;
        u8 pad1[3];
        s32 value;
        u8 pad8[4];
    } ObjectEntry;
    typedef struct ObjectInfo {
        u8 pad0[0x64];
        u32 count;
        u8 pad68[4];
    } ObjectInfo;
    typedef struct InterfaceSlot {
        u8 pad0[0x240];
        u8 flag;
        u8 pad241[0x245 - 0x241];
        u8 type;
        u8 pad246[0x25C - 0x246];
        PTR32(u8) object;
        u8 pad260[0x300 - 0x260];
    } InterfaceSlot;
    typedef struct Interface {
        u32 count;
        u32 field4;
        u8 pad8[0x18 - 0x8];
        f32 camDistance;
        PTR32(u8) ptr1C;
        u8 pad20[0x88 - 0x20];
        PTR32(InterfaceSlot) slots;
        u8 pad8C[0x170 - 0x8C];
    } Interface;
    extern Interface g_interface[];
    void InterfaceManager_SetObjectState(Interface *iface, InterfaceSlot *slot, s32 state, s32 arg3);
    ObjectInfo *info;
    u32 i;
    u32 j;
    Interface *iface;

    iface = &g_interface[index];
    for (i = 0; i < iface->count; i++) {
        if (GETP(iface->slots)[i].type == 4) {
            InterfaceManager_SetObjectState(iface, &GETP(iface->slots)[i], 3, 0);
            GETP(iface->slots)[i].flag = 0;
            info = (ObjectInfo *)GETP(GETP(iface->slots)[i].object);
            for (j = 0; j < info->count; j++) {
                if ((SLOT_LOAD(ObjectEntry *, (GETP(GETP(iface->slots)[i].object) + 0x68)))[j].type == 3) {
                    iface->camDistance = (f32)(SLOT_LOAD(ObjectEntry *, (GETP(GETP(iface->slots)[i].object) + 0x68)))[j].value;
                    break;
                }
            }
            *(u16 *)(SLOT_LOAD(u8 *, (GETP(iface->ptr1C) + 8)) + 6) = 10;
            iface->field4 = 2;
        }
    }
}

f32 DropShip_GetLRTime(s32 index)
{
    typedef struct DropShipSlot
    {
        u8 pad0[0x245];
        u8 state;
        u8 pad246[0x25C - 0x246];
        PTR32(u8) anim;
        u8 pad260[0x300 - 0x260];
    } DropShipSlot;
    extern u8 g_interface[][0x170];
    u32 i;
    u8 *iface = g_interface[index];

    for (i = 0; i < *(u32 *)iface; i++)
    {
        if ((SLOT_LOAD(DropShipSlot *, (iface + 0x88)))[i].state == 5)
        {
            return (f32)*(u16 *)(GETP((SLOT_LOAD(DropShipSlot *, (iface + 0x88)))[i].anim) + 0xA);
        }
    }
    return 0.0f;
}

void InterfaceManager_SetMissionObjectiveText(u8 *obj, u8 **data) {
    char **text;
    char buf[16];
    char *str;
    s32 i;
    s32 j;
    s32 len;

    if (SLOT_LOAD(u8 *, (data[1] + 0xC)) == NULL) {
        if (*(char ***)(data[1] + 0x14) != NULL) {
            text = *(char ***)(data[1] + 0x14);
        } else {
            text = *(char ***)(data[1] + 0x18);
        }
    } else if (*(f32 *)(SLOT_LOAD(u8 *, (data[1] + 0xC)) + 4)) {
        text = *(char ***)(data[1] + 0x18);
    } else {
        text = *(char ***)(data[1] + 0x14);
    }
    if (text == NULL) {
        return;
    }
    if (data[1][0] == 0) {
        InterfaceManager_SetObjectText(obj, text);
        return;
    }
    if (data[1][0] == 1) {
        sprintf(buf, "%d", (u32)*(f32 *)(SLOT_LOAD(u8 *, (data[1] + 0x10)) + 4));
    } else if (data[1][0] == 2) {
        u32 ms = *(f32 *)(SLOT_LOAD(u8 *, (data[1] + 0x10)) + 8);
        u32 secs = ms / 1000;
        u32 mins = secs / 60;
        secs %= 60;
        sprintf(buf, "%02d:%02d", mins, secs);
    } else if (data[1][0] == 3) {
        u32 *var = SLOT_LOAD(u32 *, (data[1] + 0x10));
        u32 value;
        if (var[0] < 10) {
            value = GetCodeVariable(var[0], data[0]);
        } else {
            value = GetCodeVariable(var[0], (u8 *)PTR(var[1]));
        }
        sprintf(buf, "%d", value);
    }
    str = *text;
    len = strlen(str);
    j = strlen(buf);
    if (len != strlen((char *)(obj + 0x290))) {
        strcpy((char *)(obj + 0x290), str);
    } else {
        strcpy(str, (char *)(obj + 0x290));
    }
    j--;
    for (i = len - 1; i >= 0; i--) {
        if (str[i] == '#') {
            if (j >= 0) {
                str[i] = buf[j--];
            } else {
                str[i] = ' ';
            }
        }
    }
    InterfaceManager_SetObjectText(obj, text);
}


void InterfaceManager_RestoreMissionObjString(u8 *mgr)
{
    u32 i;
    u32 id;
    u8 *obj;
    char **str;
    f32 zero;

    if (*(u32 *)((char *)mgr + 0x25c) == 0)
    {
        return;
    }
    id = *(u16 *)((char *)SLOT_LOAD(u8 *, ((char *)mgr + 0x25c)) + 0xa);
    if (strlen((char *)(mgr + 0x290)) == 0)
    {
        return;
    }
    if (GETP(g_worldData.world) == NULL)
    {
        return;
    }
    zero = 0.0f;
    for (i = 0; i < *(u32 *)((char *)GETP(g_worldData.world) + 0x58); i++)
    {
        obj = SLOT_LOAD(u8 *, ((char *)SLOT_LOAD(u8 *, ((char *)GETP(g_worldData.world) + 0x5c)) + i * 8 + 4));
        if (obj[2] == id)
        {
            if (*(f32 *)((char *)SLOT_LOAD(f32 *, ((char *)obj + 0xc)) + 4) != zero)
            {
                str = *(char ***)((char *)obj + 0x18);
            }
            else
            {
                str = *(char ***)((char *)obj + 0x14);
            }
            strcpy(*str, (char *)(mgr + 0x290));
        }
    }
}
void InterfaceManager_SetPlayerMessage(void *message, u32 player) {
    extern void *g_playerMessage1;
    extern void *g_playerMessage2;
    extern void *g_playerMessage3;

    if (player == 0) {
        g_playerMessage1 = message;
    } else if (player == 1) {
        g_playerMessage2 = message;
    } else if (player == 2) {
        g_playerMessage3 = message;
    }
}

s32 ActionEndInterface(void *action) {
    extern u8 g_interface[][0x170];
    extern void *g_pInterfaceToEnd;
    u32 i;
    s8 found;

    if (M2C_FIELD(action, u32 *, 0) == 0xFFFFFFFF) {
        for (i = 0; i < 0x12; i++) {
            if (SLOT_LOAD(void *, g_interface[i]) != NULL && g_interface[i] != NULL) {
                InterfaceManager_SetLoadState(g_interface[i], 1);
            }
        }
    } else {
        found = 0;
        for (i = 0; i < 0x12; i++) {
            if (SLOT_LOAD(void *, g_interface[i]) != NULL && SLOT_LOAD(void *, (u8 *)(g_interface[i]) + 0x20) == SLOT_LOAD(void *, (u8 *)(action) + 4)) {
                if (g_interface[i] != NULL) {
                    InterfaceManager_SetLoadState(g_interface[i], 1);
                }
                found = 1;
            }
        }
        if (found == 0 && SLOT_LOAD(void *, (u8 *)(action) + 8) != NULL) {
            g_pInterfaceToEnd = SLOT_LOAD(void *, (u8 *)(action) + 4);
        }
    }
    return 1;
}

s32 InterfaceManager_AddFont(u8 *font) {
    u32 i;
    s32 id;
    u8 *spacing;

    id = Gfx_FontManager_AddFont(M2C_FIELD(font, u32 *, 0), font[0xB], font[9], font[0xA], M2C_FIELD(font, f32 *, 0x28));
    for (i = 0; i < M2C_FIELD(font, u32 *, 0x20); i++) {
        spacing = SLOT_LOAD(u8 *, (u8 *)(font) + 0x24) + i * 0xC;
        Gfx_FontManager_SetCharacterSpacing(id, spacing[0], M2C_FIELD(spacing, f32 *, 4), M2C_FIELD(spacing, f32 *, 8));
    }
    return id;
}

void InterfaceManager_AddInterfaceToRender(u32 index, u32 priority) {
    extern u8 g_interface[][0x170];
    extern u32 g_interfaceRenderOrder[18];
    extern u32 g_numInterfacesToRender;
    u32 i;
    u32 pos;

    if (g_numInterfacesToRender == 0) {
        g_interfaceRenderOrder[g_numInterfacesToRender++] = index;
        return;
    }
    pos = -1;
    for (i = 0; i < g_numInterfacesToRender; i++) {
        if (priority > *(u32 *)(SLOT_LOAD(u8 *, (g_interface[g_interfaceRenderOrder[i]] + 0x20)) + 0x34)) {
            pos = i;
            break;
        }
    }
    if (pos != -1) {
        for (i = g_numInterfacesToRender; i > pos; i--) {
            g_interfaceRenderOrder[i] = g_interfaceRenderOrder[i - 1];
        }
        g_interfaceRenderOrder[pos] = index;
        g_numInterfacesToRender++;
        return;
    }
    g_interfaceRenderOrder[g_numInterfacesToRender++] = index;
}

void InterfaceManager_RemoveInterfaceFromRender(u32 id) {
    extern u32 g_interfaceRenderOrder[];
    extern u32 g_numInterfacesToRender;
    u32 i;

    for (i = 0; i < g_numInterfacesToRender; i++) {
        if (id == g_interfaceRenderOrder[i]) {
            g_numInterfacesToRender--;
            for (; i < g_numInterfacesToRender; i++) {
                g_interfaceRenderOrder[i] = g_interfaceRenderOrder[i + 1];
            }
            return;
        }
    }
}

