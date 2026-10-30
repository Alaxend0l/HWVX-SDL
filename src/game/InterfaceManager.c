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
    u8 pad29[1];
    s8 accept;
    u8 pad2B[0x30 - 0x2B];
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

/* Interface definition data and live interface, as InterfaceManager_MoveInterface sees them. */
typedef struct IMCondValue {
    s8 isVar;
    u8 pad1;
    u16 value;
} IMCondValue;

typedef struct IMCodeVar {
    u32 id;
    PTR32(IMCondValue) val;
} IMCodeVar;

typedef struct IMVariable {
    u8 type;
    u8 update;
    u8 pad2[2];
    s32 min;
    s32 max;
    s32 offset;
    PTR32(IMCodeVar) data;
    u8 pad14[0x34 - 0x14];
} IMVariable;

typedef struct IMVariableList {
    u32 count;
    PTR32(IMVariable) vars;
} IMVariableList;

typedef struct IMEventList {
    u32 count;
    PTR32(u8) list;
} IMEventList;

typedef struct IMMusicInfo {
    PTR32(char) name;
    u32 id;
} IMMusicInfo;

typedef struct IMHeader {
    u16 type;
    u8 pad2[0x14 - 0x2];
    IMEventList events;
    PTR32(IMVariableList) vars;
    u8 pad20[0x2C - 0x20];
    PTR32(s8) sound;
    PTR32(IMMusicInfo) music;
} IMHeader;

typedef struct IMStep {
    u32 var;
    s32 val[4];
    u8 set[4];
    u8 pad18[0x28 - 0x18];
} IMStep;

typedef struct IMAnimEvent {
    u8 pad0[4];
    u32 delay;
    u8 pad8[0xC - 0x8];
} IMAnimEvent;

typedef struct IMObjectData {
    u8 id;
    u8 next[4];
    u8 pad5[0x8 - 0x5];
    u32 numSteps;
    PTR32(IMStep) steps;
    u32 numEvents;
    PTR32(IMAnimEvent) events;
} IMObjectData;

typedef struct IMObject {
    u8 pad0[0x8];
    u8 active;
    u8 pad9[0xC - 0x9];
    s16 unkC;
    u8 padE[0x30 - 0xE];
    f32 anchorX;
    f32 anchorY;
    u8 pad38[0x44 - 0x38];
    PTR32(IMObjectData) data;
} IMObject;

typedef struct IMSlot {
    u8 pad0[0x2C];
    u8 dirty2C;
    u8 pad2D[0x30 - 0x2D];
    Vector4 scale;
    u8 pad40[0x54 - 0x40];
    u8 dirty54;
    u8 pad55[0x60 - 0x55];
    Vector4 pos;
    u8 pad70[0x84 - 0x70];
    u8 dirty84;
    u8 pad85[0x90 - 0x85];
    Vector4 rot;
    u8 padA0[0xB4 - 0xA0];
    u8 dirtyB4;
    u8 padB5[0x118 - 0xB5];
    u8 dirty118;
    u8 pad119[0x124 - 0x119];
    u8 visible;
    u8 unk125;
    u8 pad126[0x130 - 0x126];
    u8 dirty130;
    u8 pad131[0x240 - 0x131];
    u8 hidden;
    u8 pad241[0x243 - 0x241];
    u8 kind;
    u8 unk244;
    u8 renderType;
    u8 pad246[0x247 - 0x246];
    u8 animDone;
    u8 pad248[0x254 - 0x248];
    PTR32(u8) unk254;
    PTR32(u8) unk258;
    PTR32(IMObject) object;
    PTR32(struct IMSlot) parent;
    PTR32(struct IMSlot) offset;
    u8 pad268[0x26C - 0x268];
    PTR32(void) thing;
    s32 child;
    u32 timer;
    u8 pad278[0x288 - 0x278];
    PTR32(u8) car;
    u8 pad28C[0x300 - 0x28C];
} IMSlot;

typedef struct IMInterface {
    u32 count;
    u32 state;
    PTR32(u8) owner;
    PTR32(IMSlot) selected;
    u32 timer;
    s32 seq;
    f32 delay;
    u32 event;
    PTR32(IMHeader) header;
    PTR32(IMHeader) next;
    PTR32(IMHeader) history[3];
    u8 pad34[0x70 - 0x34];
    u8 historyIds[0xC];
    u8 flags;
    u8 index;
    u8 pad7E;
    u8 isChild;
    u8 historyCount;
    u8 goBack;
    u8 pad82[0x84 - 0x82];
    u8 hasInput;
    u8 unk85;
    u8 pad86[0x88 - 0x86];
    PTR32(IMSlot) slots;
    s32 input;
    FPTR32(void, (InterfaceEventMsg *msg)) callback;
    FPTR32(void, (u8 *info)) infoCallback;
    s32 vars[30];
    u32 numVars;
    u8 player;
    u8 pad115[0x116 - 0x115];
    InterfaceDirState dir[4][4];
    u8 pad156[0x160 - 0x156];
    u8 unk160;
    s8 unk161;
    u8 pad162[0x164 - 0x162];
    f32 unk164;
    s8 locked;
    u8 pad169[0x170 - 0x169];
} IMInterface;

typedef struct IMWorldVar {
    u8 pad0[2];
    u8 id;
    u8 pad3;
    f32 value;
} IMWorldVar;

typedef struct IMWorld {
    u8 pad0[0x48];
    u32 numVars;
    PTR32(PTR32(IMWorldVar)) vars;
} IMWorld;

extern u8 g_objectAllocations[];  /* typed per function below */
extern u8 g_numInterfaceObjectsAllocated[];  /* typed per function below */
extern u8 g_numInterfaceObjectAllocations[];  /* typed per function below */

void *InterfaceManager_GetInterfaceObjects(u32 num) {
    typedef struct ObjectAllocation {
        u32 start;
        u32 count;
        s8 free;
    } ObjectAllocation;
    typedef struct InterfaceObject {
        u8 pad[0x2FC];
        u32 index;
    } InterfaceObject;
    #define g_objectAllocations ((ObjectAllocation *)(void *)&g_objectAllocations)
    #define g_numInterfaceObjectAllocations (*(u32 *)(void *)&g_numInterfaceObjectAllocations)
    extern InterfaceObject g_MasterInterfaceObjectList[500];
    #define g_numInterfaceObjectsAllocated (*(u32 *)(void *)&g_numInterfaceObjectsAllocated)
    InterfaceObject *obj;
    ObjectAllocation *alloc;
    u32 j;
    u32 total;
    u32 i;
    u32 idx;
    u32 n;

    for (i = 0; g_numInterfaceObjectAllocations != 0 && i < g_numInterfaceObjectAllocations - 1; i++) {
        alloc = &g_objectAllocations[i];
        if (alloc->free) {
            total = 0;
            for (j = i + 1; j < g_numInterfaceObjectAllocations &&
                            (g_objectAllocations[j].free || g_objectAllocations[j].count == 0);
                 j++) {
                total += g_objectAllocations[j].count;
                memset(&g_objectAllocations[j], 0, sizeof(ObjectAllocation));
            }
            alloc->count += total;
            i = j - 1;
        }
    }
    for (i = 0; i < g_numInterfaceObjectAllocations; i++) {
        if (g_objectAllocations[i].free && g_objectAllocations[i].count >= num) {
            if (i + 1 < g_numInterfaceObjectAllocations &&
                (g_objectAllocations[i + 1].free || g_objectAllocations[i + 1].count == 0)) {
                g_objectAllocations[i].free = 0;
                idx = g_objectAllocations[i].start;
                g_objectAllocations[i + 1].start = num + idx;
                g_MasterInterfaceObjectList[idx].index = idx;
                g_objectAllocations[i + 1].count = g_objectAllocations[i].count - num;
                g_objectAllocations[i + 1].free = 1;
                g_objectAllocations[i].count = num;
                return &g_MasterInterfaceObjectList[idx];
            }
            g_objectAllocations[i].free = 0;
            idx = g_objectAllocations[i].start;
            g_MasterInterfaceObjectList[idx].index = idx;
            return &g_MasterInterfaceObjectList[idx];
        }
    }
    if (g_numInterfaceObjectAllocations + num > 500) {
        return NULL;
    }
    if (g_numInterfaceObjectAllocations >= 28) {
        return NULL;
    }
    obj = &g_MasterInterfaceObjectList[g_numInterfaceObjectsAllocated];
    obj->index = g_numInterfaceObjectsAllocated;
    g_numInterfaceObjectsAllocated += num;
    n = g_numInterfaceObjectAllocations++;
    g_objectAllocations[n].start = obj->index;
    g_objectAllocations[n].count = num;
    g_objectAllocations[n].free = 0;
    return obj;
#undef g_numInterfaceObjectAllocations
#undef g_numInterfaceObjectsAllocated
#undef g_objectAllocations
}
extern u8 g_playerMessage3[];  /* typed per function below */
extern u8 g_playerMessage2[];  /* typed per function below */
extern u8 g_playerMessage1[];  /* typed per function below */
extern u8 g_pInterfaceToEnd[];  /* typed per function below */
extern u8 g_numInterfacesToRender[];  /* typed per function below */
extern u8 g_interface[];  /* typed per function below */

void InterfaceManager_Init(void) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    extern s32 g_TargetArrow[7];
    #define g_pInterfaceToEnd (*(void **)(void *)&g_pInterfaceToEnd)
    #define g_numInterfaceObjectsAllocated (*(s32 *)(void *)&g_numInterfaceObjectsAllocated)
    #define g_numInterfaceObjectAllocations (*(s32 *)(void *)&g_numInterfaceObjectAllocations)
    #define g_numInterfacesToRender (*(s32 *)(void *)&g_numInterfacesToRender)
    #define g_playerMessage1 (*(void **)(void *)&g_playerMessage1)
    #define g_playerMessage2 (*(void **)(void *)&g_playerMessage2)
    #define g_playerMessage3 (*(void **)(void *)&g_playerMessage3)
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
#undef g_interface
#undef g_numInterfaceObjectAllocations
#undef g_numInterfaceObjectsAllocated
#undef g_numInterfacesToRender
#undef g_pInterfaceToEnd
#undef g_playerMessage1
#undef g_playerMessage2
#undef g_playerMessage3
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
    #define g_interface ((Interface *)(void *)&g_interface)
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
#undef g_interface
}

static inline void InterfaceManager_ReleaseInterfaceObjects(u8 *objs) {
    typedef struct ObjectAllocation {
        u32 start;
        u32 count;
        s8 free;
    } ObjectAllocation;
    #define g_objectAllocations ((ObjectAllocation *)(void *)&g_objectAllocations)
    #define g_numInterfaceObjectAllocations (*(u32 *)(void *)&g_numInterfaceObjectAllocations)
    #define g_numInterfaceObjectsAllocated (*(u32 *)(void *)&g_numInterfaceObjectsAllocated)
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
#undef g_numInterfaceObjectAllocations
#undef g_numInterfaceObjectsAllocated
#undef g_objectAllocations
}
extern u8 g_curMusicPackID[];  /* typed per function below */
extern u8 g_bUnloadMusicPack[];  /* typed per function below */

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
    #define g_interface ((Interface *)(void *)&g_interface)
    #define g_bUnloadMusicPack (*(s8 *)(void *)&g_bUnloadMusicPack)
    extern char *g_curMusicPackName;
    #define g_curMusicPackID (*(s32 *)(void *)&g_curMusicPackID)
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
#undef g_bUnloadMusicPack
#undef g_curMusicPackID
#undef g_interface
}

void InterfaceManager_ReleaseAllInterfaces(void) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    extern s32 g_TargetArrow[7];
    #define g_pInterfaceToEnd (*(void **)(void *)&g_pInterfaceToEnd)
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (*(u32 *)g_interface[i] != 0 /* interface count */) {
            InterfaceManager_ReleaseInterface(i);
        }
    }
    g_pInterfaceToEnd = NULL;
    g_TargetArrow[0] = 0;
    g_TargetArrow[1] = 0;
    g_TargetArrow[3] = 0;
    g_TargetArrow[4] = -1;
    g_TargetArrow[5] = 0;
#undef g_interface
#undef g_pInterfaceToEnd
}

void InterfaceManager_ReleaseAllUnpausedInterfaces(void) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (*(u32 *)g_interface[i] != 0 /* interface count */ && g_interface[i][0x83] == 0) {
            InterfaceManager_ReleaseInterface(i);
        }
    }
#undef g_interface
}

void InterfaceManager_SetCallBackRoutine(s32 index, void *callback) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    SLOT_STORE((g_interface[index] + 0x90), callback);
#undef g_interface
}

void InterfaceManager_SetInfoCallBackRoutine(s32 index, void *callback) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    SLOT_STORE((g_interface[index] + 0x94), callback);
#undef g_interface
}


void InterfaceManager_PauseInterface(u32 pause, s32 index) {
    typedef struct InterfaceSlot {
        u8 pad0[0x243];
        u8 type;
        u8 pad244[0x270 - 0x244];
        s32 child;
        u8 pad274[0x300 - 0x274];
    } InterfaceSlot;
    typedef struct Interface {
        u32 count;
        u8 pad4[0x83 - 0x4];
        u8 paused;
        u8 pad84[0x88 - 0x84];
        PTR32(InterfaceSlot) slots;
        u8 pad8C[0x170 - 0x8C];
    } Interface;
    #define g_interface ((Interface *)(void *)&g_interface)
    u32 i;

    g_interface[index].paused = pause;
    for (i = 0; i < g_interface[index].count; i++) {
        if (GETP(g_interface[index].slots)[i].type != 6) {
            continue;
        }
        InterfaceManager_PauseInterface(pause, GETP(g_interface[index].slots)[i].child);
    }
#undef g_interface
}


s32 InterfaceManager_GetTotalActiveInterfaces(void) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    s32 count = 0;
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (*(u32 *)g_interface[i] != 0 /* interface count */) {
            count++;
        }
    }
    return count;
#undef g_interface
}

void InterfaceManager_PauseAllInterface(u8 pause) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    u32 i;

    for (i = 0; i < 0x12; i++) {
        g_interface[i][0x83] = pause;
    }
#undef g_interface
}

void InterfaceManager_SetOffset(s32 index, f32 x, f32 y) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    u8 *iface = g_interface[index];
    iface[0x82] = 1;
    *(f32 *)(iface + 0x60) = x;
    *(f32 *)(iface + 0x64) = y;
#undef g_interface
}

s32 InterfaceManager_Create(u8 *header, u8 *owner, void (*callback)(void *msg)) {
    if (header != NULL) {  /* port: trace interface creation */
        Port_Log("interface create: header id %u, %u objects%s", *(u16 *)header, *(u32 *)(header + 0xC),
                 owner != NULL ? " (player)" : "");
    }
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
        PTR32(PTR32(WorldVar)) vars;
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
    #define g_interface ((Interface *)(void *)&g_interface)
    #define g_pInterfaceToEnd (*(Header **)(void *)&g_pInterfaceToEnd)
    extern s32 g_playerIndex;
    extern s8 g_bMemoryCardHWHack;
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
                    if (GETP(GETP(((World *)GETP(g_worldData.world))->vars)[j])->id == GETP(GETP(hdr->vars)->vars)[i].offset) {
                        iface->vars[i] = GETP(GETP(((World *)GETP(g_worldData.world))->vars)[j])->value;
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
#undef g_interface
#undef g_pInterfaceToEnd
}

void InterfaceManager_AddObject(void *interface, void *object) {
    typedef struct KeyTrack {
        u32 count;
        PTR32(void) keys;
    } KeyTrack;
    typedef struct ObjAnim {
        KeyTrack pos;
        KeyTrack rot;
        KeyTrack scale;
        KeyTrack color;
        KeyTrack uv;
    } ObjAnim;
    typedef struct ObjSub {
        u8 type;
        u8 unk1;
        u8 pad2[2];
        u32 value;
        u8 pad8[0x10 - 0x8];
        PTR32(void) data;
    } ObjSub;
    typedef struct ObjText {
        PTR32(PTR32(char)) text;
        PTR32(u8) font;
        u8 pad8[0x10 - 0x8];
        PTR32(u8) font2;
    } ObjText;
    typedef struct ObjDef {
        u8 kind;
        u8 type;
        u8 pad2[0x9 - 0x2];
        u8 input;
        u8 padA[0xC - 0xA];
        s16 offset;
        u8 padE[0x14 - 0xE];
        PTR32(void) sub;
        u8 pad18[0x3C - 0x18];
        PTR32(PTR32(void)) header;
        u8 pad40[0x60 - 0x40];
        PTR32(ObjAnim) anim;
        u8 pad64[0x6C - 0x64];
    } ObjDef;
    typedef struct AddSlot {
        u8 pad0[0x2C];
        u8 dirty2C;
        u8 pad2D[0x30 - 0x2D];
        Vector4 scale;
        u8 pad40[0x54 - 0x40];
        u8 dirty54;
        u8 pad55[0x60 - 0x55];
        Vector4 pos;
        u8 pad70[0x84 - 0x70];
        u8 dirty84;
        u8 pad85[0x90 - 0x85];
        Vector4 rot;
        u8 padA0[0xB4 - 0xA0];
        u8 dirtyB4;
        u8 padB5[0xC0 - 0xB5];
        u8 uv[0x118 - 0xC0];
        u8 dirty118;
        u8 pad119[0x125 - 0x119];
        u8 unk125;
        u8 pad126[0x130 - 0x126];
        u8 dirty130;
        u8 pad131[0x164 - 0x131];
        u8 dirty164;
        u8 pad165[0x194 - 0x165];
        u8 dirty194;
        u8 pad195[0x1A4 - 0x195];
        s32 unk1A4;
        u8 pad1A8[0x1B0 - 0x1A8];
        u8 fontId;
        u8 pad1B1[0x1C0 - 0x1B1];
        char text[0x241 - 0x1C0];
        u8 unk241;
        u8 pad242;
        u8 kind;
        u8 unk244;
        u8 renderType;
        u8 pad246;
        u8 animDone;
        u8 unk248;
        u8 unk249;
        s16 unk24A;
        u8 pad24C[0x250 - 0x24C];
        s32 unk250;
        u8 pad254[0x258 - 0x254];
        PTR32(void) data;
        PTR32(ObjDef) object;
        PTR32(struct AddSlot) offset;
        PTR32(struct AddSlot) parent;
        u32 unk268;
        PTR32(void) thing;
        s32 child;
        u32 timer;
        f32 unk278;
        s32 unk27C;
        f32 unk280;
        u8 pad284[0x288 - 0x284];
        PTR32(void) car;
        s32 unk28C;
        u8 unk290;
        u8 pad291[0x2D0 - 0x291];
        u32 unk2D0;
        u8 pad2D4[0x2DC - 0x2D4];
        u32 unk2DC;
        u8 pad2E0[0x2F8 - 0x2E0];
        u32 unk2F8;
        u8 pad2FC[0x300 - 0x2FC];
    } AddSlot;
    typedef struct AddIface {
        u32 count;
        u32 state;
        PTR32(u8) owner;
        u8 padC[0x7D - 0xC];
        u8 index;
        u8 unk7E;
        u8 isChild;
        u8 pad80[0x88 - 0x80];
        PTR32(AddSlot) slots;
        s32 input;
        PTR32(void) callback;
        u8 pad94[0x115 - 0x94];
        u8 unk115;
        u8 pad116[0x158 - 0x116];
        PTR32(AddSlot) unk158;
        u32 parent;
        u8 pad160[0x170 - 0x160];
    } AddIface;
    #define g_interface ((AddIface *)(void *)&g_interface)
    extern s32 g_playerIndex;
    AddIface *iface = interface;
    ObjDef *def = object;
    s32 index;
    AddSlot *slot;
    ObjSub *sub;
    s32 offset;
    s32 player;
    void **header;
    u32 i;

    index = iface->count;
    iface->count = index + 1;
    slot = &GETP(iface->slots)[index];
    SETP(slot->object, def);
    slot->unk250 = -1;
    SETP(slot->thing, NULL);
    slot->unk244 = 0;
    slot->renderType = def->kind;
    slot->animDone = 1;
    slot->unk248 = 1;
    slot->timer = 0;
    SETP(slot->car, NULL);
    slot->unk2DC = 0;
    slot->unk2F8 = 0;
    SETP(slot->parent, NULL);
    slot->unk290 = 0;
    slot->unk27C = -1;
    slot->unk249 = 1;
    slot->unk278 = 0.0f;
    slot->unk280 = 10.0f;
    slot->unk28C = -1;
    slot->unk2D0 = 0;
    slot->dirty2C = 1;
    slot->dirty54 = 1;
    slot->dirty118 = 1;
    slot->dirty84 = 1;
    slot->dirtyB4 = 1;
    slot->unk125 = 1;
    slot->dirty164 = 1;
    slot->dirty194 = 1;
    ExtractColorKey(&GETP(GETP(slot->object)->anim)->color, slot, 0.0f);
    ExtractScaleKey(&GETP(GETP(slot->object)->anim)->scale, &slot->scale, 0.0f);
    ExtractScaleKey(&GETP(GETP(slot->object)->anim)->pos, &slot->pos, 0.0f);
    ExtractScaleKey(&GETP(GETP(slot->object)->anim)->rot, &slot->rot, 0.0f);
    ExtractUVKey(&GETP(GETP(slot->object)->anim)->uv, slot->uv, 0.0f);
    if (slot->renderType == 1) {
        slot->renderType = 0;
    }
    offset = def->offset;
    if (offset != 0) {
        offset += index;
        if (offset > 0) {
            SETP(slot->offset, &GETP(iface->slots)[offset]);
        }
    } else {
        SETP(slot->offset, NULL);
    }
    switch (def->type) {
    case 0:
        sub = GETP(def->sub);
        slot->unk241 = sub->unk1;
        switch (sub->type) {
        case 0:
            slot->unk24A = sub->value;
            slot->kind = 0;
            break;
        case 1:
            slot->unk268 = sub->value;
            slot->kind = 2;
            iface->unk7E = 1;
            break;
        case 2:
            break;
        case 3:
            slot->kind = 1;
            slot->dirty130 = 1;
            SETP(slot->data, GETP(sub->data));
            break;
        }
        break;
    case 1:
        SETP(slot->data, GETP(GETP(slot->object)->sub));
        slot->kind = 5;
        slot->fontId = InterfaceManager_AddFont(GETP(((ObjText *)GETP(slot->data))->font));
        InterfaceManager_SetObjectText(slot, GETP(((ObjText *)GETP(slot->data))->text));
        if (slot->renderType == 0x40) {
            slot->unk1A4 = 0;
            strcpy(slot->text, "Oct  1 2002");
        }
        break;
    case 3:
        slot->kind = 0xB;
        break;
    case 4:
        slot->kind = 0xC;
        break;
    case 6:
    case 7:
        player = g_playerIndex;
        header = GETP(def->header);
        slot->kind = 6;
        if (GETP(slot->object)->kind == 3) {
            g_playerIndex = GETP(slot->object)->input;
        }
        if (slot->renderType == 0x20) {
            slot->child = InterfaceManager_Create(SLOT_LOAD(void *, header), GETP(iface->owner), NULL);
        } else {
            slot->child = InterfaceManager_Create(SLOT_LOAD(void *, header), GETP(iface->owner), GETP(iface->callback));
        }
        if (GETP(slot->object)->kind == 3) {
            g_playerIndex = player;
        }
        g_interface[slot->child].isChild = 1;
        if (def->type == 7) {
            g_interface[slot->child].unk115 = 1;
        }
        g_interface[slot->child].parent = iface->index;
        for (i = 0; i < g_interface[slot->child].count; i++) {
            SETP(GETP(g_interface[slot->child].slots)[i].parent, slot);
        }
        if (GETP(slot->object)->kind == 3) {
            switch (GETP(slot->object)->input) {
            case 1:
                InterfaceManager_SetInput(slot->child, 0);
                break;
            case 2:
                InterfaceManager_SetInput(slot->child, 1);
                break;
            case 3:
                InterfaceManager_SetInput(slot->child, 2);
                break;
            case 4:
                InterfaceManager_SetInput(slot->child, 3);
                break;
            }
        } else {
            InterfaceManager_SetInput(slot->child, iface->input);
        }
        break;
    case 2:
        SETP(iface->unk158, slot);
        slot->kind = 7;
        SETP(slot->data, GETP(GETP(slot->object)->sub));
        break;
    case 10:
        slot->kind = 0xA;
        SETP(slot->data, GETP(GETP(slot->object)->sub));
        slot->fontId = InterfaceManager_AddFont(GETP(((ObjText *)GETP(slot->data))->font2));
        break;
    case 11:
        SETP(slot->data, GETP(GETP(slot->object)->sub));
        slot->kind = 9;
        break;
    case 12:
        SETP(slot->data, GETP(GETP(slot->object)->sub));
        slot->kind = 8;
        slot->fontId = InterfaceManager_AddFont(GETP(((ObjText *)GETP(slot->data))->font));
        break;
    }
#undef g_interface
}
extern u8 g_interfaceDialogSoundPacks[];  /* typed per function below */

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
    #define g_curMusicPackID (*(u32 *)(void *)&g_curMusicPackID)
    extern u32 g_numInterfaceDialogSoundPacks;
    extern u32 g_curInterfaceDialogSoundPack;
    extern u32 g_curInterfaceDialogSoundIndex;
    #define g_interfaceDialogSoundPacks ((SoundPack *)(void *)&g_interfaceDialogSoundPacks)
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
#undef g_curMusicPackID
#undef g_interfaceDialogSoundPacks
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
        SLOT_STORE((obj + 0x1A0), text);  /* port: text is a table of string slots */
        SLOT_STORE((obj + 0x1A4), NULL);
        obj[0x1B1] = 0;
        str = SLOT_LOAD(char *, text);
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
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    u8 *iface = g_interface[index];

    if (iface != NULL) {
        InterfaceManager_SetLoadState(iface, 1);
    }
#undef g_interface
}

void InterfaceManager_SetLoadState(u8 *iface, s32 state) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
#undef g_interface
}

void InterfaceManager_SetObjectState(u8 *iface, u8 *obj, s32 state, s32 arg3) {
    typedef struct IMState {
        u8 type;
        u8 pad1[3];
        u32 value;
        PTR32(u8) data;
    } IMState;
    u32 i;
    u8 *numOff = NULL;
    u8 *numOn = NULL;
    u8 *numActive = NULL;
    u8 *numSelected = NULL;
    u8 *numDisabled = NULL;
    u32 j;

    if (obj == NULL) {
        return;
    }
    obj[0x240] = 0;
    obj[0x246] = state;
    *(s32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x18) = -1;
    *(s32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x1C) = -1;
    *(s32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x20) = -1;
    *(s32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x24) = -1;
    *(s32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x28) = -1;
    *(s32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x2C) = -1;
    for (i = 0; i < *(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x64); i++) {
        u8 *info = SLOT_LOAD(u8 *, (obj + 0x25C));
        switch ((SLOT_LOAD(IMState *, (info + 0x68)))[i].type) {
        case 0:
            if (numOff <= NULL) {
                *(u32 *)(info + 0x18) = i;
            }
            numOff++;
            break;
        case 1:
            if (numOn <= NULL) {
                *(u32 *)(info + 0x1C) = i;
            }
            numOn++;
            break;
        case 2:
            if (numActive <= NULL) {
                *(u32 *)(info + 0x20) = i;
            }
            numActive++;
            break;
        case 3:
            if (numSelected <= NULL) {
                *(u32 *)(info + 0x24) = i;
            }
            numSelected++;
            break;
        case 4:
            if (numOn <= NULL) {
                *(u32 *)(info + 0x28) = i;
            }
            break;
        case 5:
            if (numDisabled <= NULL) {
                *(u32 *)(info + 0x2C) = i;
            }
            numDisabled++;
            break;
        }
    }
    if (*(u32 *)(obj + 0x250) != 0xFFFFFFFF) {
        SoundSequence_Stop(*(s32 *)(obj + 0x250));
        *(s32 *)(obj + 0x250) = -1;
    }
    if (*(u32 *)(obj + 0x26C) != 0) {
        Thing_Release(SLOT_LOAD(void *, (obj + 0x26C)));
        *(u32 *)(obj + 0x26C) = 0;
        obj[0x244] = 0;
    }
    switch (state) {
    case 100:
        obj[0x240] = 1;
        obj[0x248] = 0;
        *(u32 *)(obj + 0x274) = 0;
        obj[0x247] = 1;
        SLOT_STORE((obj + 0x254), NULL);
        return;
    case 4:
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x28) == 0xFFFFFFFF) {
            obj[0x240] = 1;
            obj[0x248] = 0;
            SLOT_STORE((obj + 0x254), NULL);
            return;
        }
        SLOT_STORE((obj + 0x254), GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x28)].data));
        *(u32 *)(obj + 0x274) = 0;
        obj[0x247] = 1;
        if (SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x28)].data) + 0x30)) != NULL) {
            *(s32 *)(obj + 0x250) = SoundSequence_Play(SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x28)].data) + 0x30)), NULL);
        }
        InterfaceManager_ExecuteActions(iface, SLOT_LOAD(u8 *, (obj + 0x254)) + 0x34);
        break;
    case 5:
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x2C) == 0xFFFFFFFF) {
            obj[0x240] = 1;
            obj[0x248] = 0;
            SLOT_STORE((obj + 0x254), NULL);
            return;
        }
        SLOT_STORE((obj + 0x254), GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x2C)].data));
        *(u32 *)(obj + 0x274) = 0;
        obj[0x247] = 1;
        if (SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x2C)].data) + 0x30)) != NULL) {
            *(s32 *)(obj + 0x250) = SoundSequence_Play(SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x2C)].data) + 0x30)), NULL);
        }
        InterfaceManager_ExecuteActions(iface, SLOT_LOAD(u8 *, (obj + 0x254)) + 0x34);
        break;
    case 0:
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x18) == 0xFFFFFFFF) {
            obj[0x240] = 1;
            obj[0x248] = 0;
            SLOT_STORE((obj + 0x254), NULL);
            return;
        }
        SLOT_STORE((obj + 0x254), GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x18)].data));
        *(u32 *)(obj + 0x274) = 0;
        obj[0x247] = 1;
        if (SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x18)].data) + 0x30)) != NULL) {
            *(s32 *)(obj + 0x250) = SoundSequence_Play(SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x18)].data) + 0x30)), NULL);
        }
        InterfaceManager_ExecuteActions(iface, SLOT_LOAD(u8 *, (obj + 0x254)) + 0x34);
        break;
    case 1:
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x1C) == 0xFFFFFFFF) {
            InterfaceManager_SetObjectState(iface, obj, 0, 0);
            obj[0x246] = 1;
            return;
        }
        SLOT_STORE((obj + 0x254), GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x1C)].data));
        *(u32 *)(obj + 0x274) = 0;
        obj[0x247] = 1;
        if (SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x1C)].data) + 0x30)) != NULL) {
            *(s32 *)(obj + 0x250) = SoundSequence_Play(SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x1C)].data) + 0x30)), NULL);
        }
        InterfaceManager_ExecuteActions(iface, SLOT_LOAD(u8 *, (obj + 0x254)) + 0x34);
        break;
    case 2:
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x20) == 0xFFFFFFFF) {
            InterfaceManager_SetObjectState(iface, obj, 1, 0);
            if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) != NULL) {
                obj[0x247] = 0;
                for (j = 0; j < *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) + 0x10); j++) {
                    *(u32 *)(obj + 0x274) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) + 0x14)) + j * 0xC + 4);
                    if (*(u32 *)(obj + 0x274) != 0) {
                        break;
                    }
                }
            } else {
                *(u32 *)(obj + 0x274) = 0;
                obj[0x247] = 1;
            }
            obj[0x248] = 0;
            return;
        }
        SLOT_STORE((obj + 0x254), GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x20)].data));
        if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) != NULL) {
            obj[0x247] = 0;
            for (j = 0; j < *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) + 0x10); j++) {
                *(u32 *)(obj + 0x274) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) + 0x14)) + j * 0xC + 4);
                if (*(u32 *)(obj + 0x274) != 0) {
                    break;
                }
            }
        } else {
            *(u32 *)(obj + 0x274) = 0;
            obj[0x247] = 1;
        }
        if (SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x20)].data) + 0x30)) != NULL) {
            *(s32 *)(obj + 0x250) = SoundSequence_Play(SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x20)].data) + 0x30)), NULL);
        }
        InterfaceManager_ExecuteActions(iface, SLOT_LOAD(u8 *, (obj + 0x254)) + 0x34);
        break;
    case 3:
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x24) == 0xFFFFFFFF) {
            InterfaceManager_SetObjectState(iface, obj, 2, 0);
            obj[0x248] = 0;
            return;
        }
        SLOT_STORE((obj + 0x254), GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x24)].data));
        *(u32 *)(iface + 4) = 3;
        *(u32 *)(iface + 0x10) = (SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x24)].value;
        if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) != NULL) {
            obj[0x247] = 0;
            *(u32 *)(obj + 0x274) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x44)) + 0x14)) + 4);
        } else {
            *(u32 *)(obj + 0x274) = 0;
            obj[0x247] = 1;
        }
        if (*(u32 *)(iface + 0x10) == 0) {
            obj[0x240] = 1;
        }
        if (SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x24)].data) + 0x30)) != NULL) {
            *(s32 *)(obj + 0x250) = SoundSequence_Play(SLOT_LOAD(s8 *, (GETP((SLOT_LOAD(IMState *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x68)))[*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x24)].data) + 0x30)), NULL);
        }
        InterfaceManager_ExecuteActions(iface, SLOT_LOAD(u8 *, (obj + 0x254)) + 0x34);
        break;
    }
    obj[0x2C] = 1;
    obj[0x54] = 1;
    obj[0x118] = 1;
    obj[0x84] = 1;
    obj[0xB4] = 1;
    obj[0x125] = 1;
    obj[0x164] = 1;
    obj[0x194] = 1;
    obj[0x248] = 1;
    InterfaceManager_CheckConditions(iface, obj);
    InterfaceManager_MoveObject(iface, obj, 0, 0.0f);
}

void InterfaceManager_LinkThingToObject(s32 index, u32 id, void *thing) {
    typedef struct InterfaceSlot {
        u8 pad0[0x25C];
        PTR32(u8) object;
        u8 pad260[0x288 - 0x260];
        PTR32(void) thing;
        u8 pad28C[0x300 - 0x28C];
    } InterfaceSlot;
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    u8 *iface = g_interface[index];
    u32 i;

    if (iface != NULL) {
        for (i = 0; i < *(u32 *)iface; i++) {
            if (id == *(u32 *)(GETP((SLOT_LOAD(InterfaceSlot *, (iface + 0x88)))[i].object) + 0x10)) {
                SETP((SLOT_LOAD(InterfaceSlot *, (iface + 0x88)))[i].thing, thing);
            }
        }
    }
#undef g_interface
}
void InterfaceManager_MoveInterface(IMInterface *iface, f32 dt);
s32 InterfaceManager_ProcessInput(IMInterface *iface, IMSlot *obj, u32 *events);

void InterfaceManager_Heartbeat(void) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
        if (*(u32 *)g_interface[i] != 0 /* interface count */ && g_interface[i][0x83] == 0 && g_interface[i][0x85] == 0) {
            void (*callback)(InterfaceMessage *) = ((void (*)(InterfaceMessage *))SLOT_LOAD(void *, (g_interface[i] + 0x90)));

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
#undef g_interface
}
s32 InterfaceManager_PlayEvent(InterfaceState *iface, u8 *obj, u8 *event, u16 arg);
u32 InterfaceManager_KeyPress(InterfaceKeyState *im, s32 pad, u8 key);

void InterfaceManager_MoveInterface(IMInterface *iface, f32 dt) {
    extern char *g_curMusicPackName;
    #define g_curMusicPackID (*(u32 *)(void *)&g_curMusicPackID)
    #define g_bUnloadMusicPack (*(u8 *)(void *)&g_bUnloadMusicPack)
    #define g_interface ((IMInterface *)(void *)&g_interface)
    InterfaceEventMsg msg;
    InterfaceEventMsg dirMsg;
    u8 info[0xD0];
    u32 ms;
    u32 i;
    u32 j;
    u32 m;
    IMSlot *newSel;
    IMSlot *sel;
    IMSlot *slot;
    IMVariable *v;
    IMCodeVar *cv;
    IMStep *step;
    u32 id;
    u16 target;
    u16 oldId;
    u16 dir;
    s8 changed;
    s8 up;
    s8 down;
    s8 left;
    s8 right;
    s8 set;
    s8 wasHidden;
    s32 val;
    u32 var;
    s32 k;
    u16 origTarget;

    if (iface->count == 0) {
        return;
    }
    if (iface->unk161) {
        iface->unk160 = 0;
        iface->unk161 = 0;
    }
    if (iface->delay) {
        if (dt < iface->delay) {
            iface->delay -= dt;
        } else {
            iface->delay = 0.0f;
            InterfaceManager_PlayEvent(iface, NULL, (void *)PTR(iface->event), 0xFFFF);
        }
    }
    switch (iface->state) {
    case 0:
        if (iface->timer > dt) {
            iface->timer -= (u32)dt;
        } else {
            iface->timer = 0;
            InterfaceManager_SetLoadState(iface, 2);
            if (GETP(iface->callback) != NULL) {
                msg.index = iface->index;
                msg.type = 4;
                msg.id = 0;
                msg.arg = 0;
                msg.value = 0;
                msg.objState = 0;
                msg.handled = 0;
                GETP(iface->callback)(&msg);
            }
        }
        break;
    case 1:
        if (iface->timer > dt) {
            iface->timer -= (u32)dt;
        } else {
            iface->timer = 0;
            if ((u32)iface->seq < 0xFFFFFFFF) {
                SoundSequence_Stop(iface->seq);
            }
            if (GETP(iface->next) != NULL) {
                u8 count = iface->historyCount;
                u8 goBack = iface->goBack;
                u8 selId = 0;
                u8 flags = iface->flags;
                s32 input = iface->input;
                IMInterface *newI;
                IMHeader *hdr = GETP(iface->header);
                u8 histId;

                if (GETP(iface->selected) != NULL) {
                    selId = GETP(GETP(GETP(iface->selected)->object)->data)->id;
                }
                if (g_curMusicPackName != NULL) {
                    if (GETP(GETP(iface->next)->music) != NULL && GETP(GETP(GETP(iface->next)->music)->name) != NULL) {
                        if (strcmp(g_curMusicPackName, GETP(GETP(GETP(iface->next)->music)->name)) == 0 &&
                            g_curMusicPackID == GETP(GETP(iface->next)->music)->id) {
                            g_bUnloadMusicPack = 0;
                        }
                    } else {
                        g_bUnloadMusicPack = 0;
                    }
                }
                InterfaceManager_ReleaseInterface(iface->index);
                iface->count = 1;
                newI = &g_interface[InterfaceManager_Create(GETP(iface->next), GETP(iface->owner), GETP(iface->callback))];
                memcpy(newI->history, iface->history, 0xC);
                memcpy(newI->historyIds, iface->historyIds, 0xC);
                iface->count = 0;
                g_bUnloadMusicPack = 1;
                if (newI->hasInput == 0) {
                    InterfaceManager_SetInput(newI->index, input);
                }
                if (goBack == 0) {
                    for (j = 0; j < count; j++) {
                        if (GETP(newI->history[j]) == GETP(iface->header)) {
                            count = j;
                            break;
                        }
                    }
                    SETP(newI->history[count], hdr);
                    newI->historyIds[count] = selId;
                    newI->historyCount = count + 1;
                } else {
                    histId = newI->historyIds[count];
                    for (j = 0; j < newI->count; j++) {
                        slot = &GETP(newI->slots)[j];
                        if (GETP(slot->object)->active && GETP(GETP(slot->object)->data)->id == histId) {
                            SETP(newI->selected, slot);
                            for (m = 0; m < GETP(GETP(newI->header)->vars)->count && m < 30; m++) {
                                switch (GETP(GETP(GETP(newI->header)->vars)->vars)[m].type) {
                                case 3:
                                    if (GETP(newI->selected) != NULL) {
                                        newI->vars[m] = GETP(GETP(GETP(newI->selected)->object)->data)->id;
                                    } else {
                                        newI->vars[m] = 0;
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    newI->historyCount = count;
                }
                if (flags & 2) {
                    InterfaceManager_SetLoadState(newI, 2);
                }
                return;
            }
            if (iface->isChild == 0) {
                InterfaceManager_ReleaseInterface(iface->index);
                return;
            }
        }
        break;
    case 3:
        if (iface->timer > dt) {
            iface->timer -= (u32)dt;
        } else {
            InterfaceManager_SetObjectState(iface, GETP(iface->selected), 2, 0);
            if (GETP(iface->next) != NULL) {
                InterfaceManager_SetLoadState(iface, 1);
                if (iface->flags & 1) {
                    iface->timer = 0;
                    return;
                }
            } else {
                InterfaceManager_SetLoadState(iface, 2);
            }
        }
        break;
    }
    memset(info, 0, sizeof(info));
    *(u32 *)info = iface->index;
    if (GETP(iface->owner) != NULL) {
        Player_GetInterfaceInfo(GETP(iface->owner), info);
    } else if (GETP(iface->infoCallback) != NULL) {
        GETP(iface->infoCallback)(info);
    }
    for (i = 0, ms = dt; i < iface->count; i++) {
        if (GETP(iface->slots)[i].hidden == 0) {
            if (GETP(GETP(GETP(iface->slots)[i].object)->data) != NULL && GETP(iface->slots)[i].timer > dt) {
                GETP(iface->slots)[i].timer -= ms;
            } else if (GETP(GETP(GETP(iface->slots)[i].object)->data) != NULL && GETP(iface->slots)[i].animDone == 0) {
                GETP(iface->slots)[i].timer = 0;
                GETP(iface->slots)[i].animDone = 1;
                for (j = 0; j < GETP(GETP(GETP(iface->slots)[i].object)->data)->numEvents; j++) {
                    if (GETP(iface->slots)[i].hidden == 0 && GETP(GETP(GETP(GETP(iface->slots)[i].object)->data)->events)[j].delay != 0) {
                        InterfaceManager_PlayEvent(iface, &GETP(iface->slots)[i], GETP(GETP(GETP(GETP(iface->slots)[i].object)->data)->events), 0xFFFF);
                        GETP(iface->slots)[i].timer = GETP(GETP(GETP(GETP(iface->slots)[i].object)->data)->events)[j].delay;
                        GETP(iface->slots)[i].animDone = 0;
                        break;
                    }
                }
            }
        }
        InterfaceManager_MoveObject(iface, &GETP(iface->slots)[i], info, dt);
    }
    if (iface->state == 2 || iface->state == 0) {
        if (iface->unk164 > 0.0f) {
            if (iface->unk164 > dt) {
                iface->unk164 -= ms;
                for (i = 0; i < iface->count; i++) {
                    InterfaceManager_CheckConditions(iface, &GETP(iface->slots)[i]);
                }
                return;
            }
            iface->unk164 = 0.0f;
        }
    }
    if (iface->state == 2 && 0.0f == iface->unk164) {
        sel = GETP(iface->selected);
        oldId = 0;
        dir = 0;
        changed = 0;
        if (sel != NULL && sel->hidden) {
            u32 bestId = 0xFFFFFFFF;
            IMSlot *best = NULL;

            id = GETP(GETP(sel->object)->data)->id;
            for (i = 0; i < iface->count; i++) {
                IMSlot *s = &GETP(iface->slots)[i];
                if (s->hidden == 0 && GETP(s->object)->active && GETP(GETP(s->object)->data)->id != 0 &&
                    GETP(GETP(s->object)->data)->id < bestId) {
                    bestId = GETP(GETP(s->object)->data)->id;
                    best = s;
                }
            }
            if (bestId != 0xFFFFFFFF) {
                SETP(iface->selected, best);
                InterfaceManager_SetObjectReactive(iface, id, bestId);
            }
        }
        for (i = 0; i < iface->numVars; i++) {
            v = &GETP(GETP(GETP(iface->header)->vars)->vars)[i];
            if (v->update) {
                switch (v->type) {
                case 0:
                    break;
                case 1:
                    cv = GETP(v->data);
                    if (GETP(cv->val)->isVar) {
                        iface->vars[i] = GameSettings_GetCodeVariable(cv->id, iface->vars[GETP(cv->val)->value]);
                    } else {
                        iface->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->val)->value);
                    }
                    iface->vars[i] += GETP(GETP(GETP(iface->header)->vars)->vars)[i].offset;
                    if (iface->vars[i] > GETP(GETP(GETP(iface->header)->vars)->vars)[i].max) {
                        iface->vars[i] = iface->vars[i] - GETP(GETP(GETP(iface->header)->vars)->vars)[i].max - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[i].min;
                    } else if (iface->vars[i] < GETP(GETP(GETP(iface->header)->vars)->vars)[i].min) {
                        iface->vars[i] = GETP(GETP(GETP(iface->header)->vars)->vars)[i].max - (GETP(GETP(GETP(iface->header)->vars)->vars)[i].min - iface->vars[i] - 1);
                    }
                    break;
                case 2:
                    break;
                case 3:
                    if (GETP(iface->selected) != NULL) {
                        iface->vars[i] = GETP(GETP(GETP(iface->selected)->object)->data)->id;
                    } else {
                        iface->vars[i] = 0;
                    }
                    break;
                case 4:
                    if ((IMWorld *)GETP(g_worldData.world) != NULL) {
                        for (k = 0; k < ((IMWorld *)GETP(g_worldData.world))->numVars; k++) {
                            if (GETP(GETP(((IMWorld *)GETP(g_worldData.world))->vars)[k])->id == GETP(GETP(GETP(iface->header)->vars)->vars)[i].offset) {
                                iface->vars[i] = GETP(GETP(((IMWorld *)GETP(g_worldData.world))->vars)[k])->value;
                            }
                        }
                    }
                    break;
                }
            }
        }
        up = InterfaceManager_KeyPress((InterfaceKeyState *)iface, iface->input, 4);
        down = InterfaceManager_KeyPress((InterfaceKeyState *)iface, iface->input, 5);
        left = InterfaceManager_KeyPress((InterfaceKeyState *)iface, iface->input, 2);
        right = InterfaceManager_KeyPress((InterfaceKeyState *)iface, iface->input, 3);
        iface->locked = 1;
        for (j = 0; GETP(iface->selected) != NULL && j < GETP(GETP(sel->object)->data)->numSteps; j++) {
            step = &GETP(GETP(GETP(sel->object)->data)->steps)[j];
            var = step->var;
            if (var < iface->numVars) {
                if (up) {
                    set = step->set[0];
                    val = step->val[0];
                } else if (down) {
                    set = step->set[1];
                    val = step->val[1];
                } else if (left) {
                    set = step->set[2];
                    val = step->val[2];
                } else if (right) {
                    set = step->set[3];
                    val = step->val[3];
                } else {
                    break;
                }
                if (!set) {
                    iface->vars[var] += val;
                } else {
                    iface->vars[var] = val;
                }
                if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                    iface->vars[var] = iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                    iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (GETP(GETP(GETP(iface->header)->vars)->vars)[var].min - iface->vars[var] - 1);
                }
                changed = 1;
            }
        }
        if (changed) {
            for (i = 0; i < GETP(GETP(iface->header)->vars)->count; i++) {
                v = &GETP(GETP(GETP(iface->header)->vars)->vars)[i];
                if (v->update) {
                    switch (v->type) {
                    case 1:
                        cv = GETP(v->data);
                        if (cv->id == 0x1F6) {
                            if (GETP(cv->val)->isVar) {
                                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, iface->vars[GETP(cv->val)->value]);
                            } else {
                                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->val)->value);
                            }
                            iface->vars[i] += GETP(GETP(GETP(iface->header)->vars)->vars)[i].offset;
                            if (iface->vars[i] > GETP(GETP(GETP(iface->header)->vars)->vars)[i].max) {
                                iface->vars[i] = iface->vars[i] - GETP(GETP(GETP(iface->header)->vars)->vars)[i].max - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[i].min;
                            } else if (iface->vars[i] < GETP(GETP(GETP(iface->header)->vars)->vars)[i].min) {
                                iface->vars[i] = GETP(GETP(GETP(iface->header)->vars)->vars)[i].max - (GETP(GETP(GETP(iface->header)->vars)->vars)[i].min - iface->vars[i] - 1);
                            }
                        }
                        break;
                    }
                }
            }
        }
        for (i = 0; i < iface->count; i++) {
            wasHidden = GETP(iface->slots)[i].hidden;
            InterfaceManager_CheckConditions(iface, &GETP(iface->slots)[i]);
            if (GETP(iface->slots)[i].hidden == 0 && wasHidden) {
                InterfaceManager_MoveObject(iface, &GETP(iface->slots)[i], info, dt);
            }
        }
        if (InterfaceManager_ProcessInput(iface, NULL, &GETP(iface->header)->events) == 0) {
            return;
        }
        if (iface->state != 2 || GETP(iface->selected) == NULL) {
            return;
        }
        if (InterfaceManager_ProcessInput(iface, GETP(iface->selected), &GETP(GETP(GETP(iface->selected)->object)->data)->numEvents) == 0) {
            for (k = 0; k < 4; k++) {
                iface->dir[k][0].held = 0;
                iface->dir[k][1].held = 0;
                iface->dir[k][3].held = 0;
                iface->dir[k][2].held = 0;
            }
            iface->locked = 0;
            return;
        }
        iface->locked = 0;
        for (k = 0; k < 4; k++) {
            iface->dir[k][0].held = 0;
            iface->dir[k][1].held = 0;
            iface->dir[k][3].held = 0;
            iface->dir[k][2].held = 0;
        }
        if ((target = GETP(GETP(GETP(iface->selected)->object)->data)->next[0]) != 0 && up) {
            dir = 0;
        } else if ((target = GETP(GETP(GETP(iface->selected)->object)->data)->next[1]) != 0 && down) {
            dir = 1;
        } else if ((target = GETP(GETP(GETP(iface->selected)->object)->data)->next[2]) != 0 && left) {
            dir = 2;
        } else if ((target = GETP(GETP(GETP(iface->selected)->object)->data)->next[3]) != 0 && right) {
            dir = 3;
        } else {
            target = 0;
        }
        if (target == 0) {
            return;
        }
        for (origTarget = target, k = 0; k < iface->count; k++) {
            slot = &GETP(iface->slots)[k];
            if (GETP(slot->object)->active && GETP(GETP(slot->object)->data)->id == target) {
                if (slot->hidden) {
                    switch (dir) {
                    case 0:
                        target = GETP(GETP(slot->object)->data)->next[0];
                        for (j = 0; j < GETP(GETP(sel->object)->data)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->object)->data)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->object)->data)->steps)[j].val[0];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    case 1:
                        target = GETP(GETP(slot->object)->data)->next[1];
                        for (j = 0; j < GETP(GETP(sel->object)->data)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->object)->data)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->object)->data)->steps)[j].val[1];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    case 2:
                        target = GETP(GETP(slot->object)->data)->next[2];
                        for (j = 0; j < GETP(GETP(sel->object)->data)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->object)->data)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->object)->data)->steps)[j].val[2];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    case 3:
                        target = GETP(GETP(slot->object)->data)->next[3];
                        for (j = 0; j < GETP(GETP(sel->object)->data)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->object)->data)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->object)->data)->steps)[j].val[3];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    }
                    if (target == origTarget) {
                        break;
                    }
                    k = -1;
                } else {
                    newSel = slot;
                    oldId = GETP(GETP(GETP(iface->selected)->object)->data)->id;
                    break;
                }
            }
        }
        if (oldId == 0) {
            return;
        }
        if (GETP(GETP(iface->header)->sound) != NULL) {
            SoundSequence_Play(GETP(GETP(iface->header)->sound), NULL);
        }
        if (GETP(iface->callback) != NULL) {
            dirMsg.index = iface->index;
            dirMsg.type = 0;
            dirMsg.id = dir + 10001;
            dirMsg.arg = 0;
            dirMsg.value = 0;
            dirMsg.objState = target;
            dirMsg.handled = 0;
            dirMsg.accept = 1;
            GETP(iface->callback)(&dirMsg);
            if (!dirMsg.accept) {
                return;
            }
        }
        SETP(iface->selected, newSel);
        InterfaceManager_SetObjectReactive(iface, oldId, target);
    }
#undef g_bUnloadMusicPack
#undef g_curMusicPackID
#undef g_interface
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

void InterfaceManager_SetObjectReference(u8 *iface, u32 id, u32 state) {
    u32 i;

    for (i = 0; i < *(u32 *)iface; i++) {
        u8 *info = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300 + 0x25C)) + 0x44));
        u8 *obj = SLOT_LOAD(u8 *, (iface + 0x88)) + i * 0x300;
        if (info != NULL && *info == id) {
            if (obj[0x246] != state) {
                InterfaceManager_SetObjectState(iface, obj, state, 0);
                if (obj[0x243] == 5) {
                    char **strings = SLOT_LOAD(char **, (obj + 0x1A0));
                    if (strings != NULL) {
                        u8 *font = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 4));
                        char *text;
                        obj[0x248] = 1;
                        obj[0x2C] = 1;
                        obj[0x54] = 1;
                        obj[0x84] = 1;
                        obj[0x164] = 1;
                        obj[0x194] = 1;
                        *(u16 *)(obj + 0x1AA) = 0;
                        *(s32 *)(obj + 0x1AC) = *(s32 *)(font + 0x14);
                        *(u16 *)(obj + 0x1A8) = 0;
                        obj[0x1C0] = 0;
                        SLOT_STORE((obj + 0x1A0), strings);
                        SLOT_STORE((obj + 0x1A4), NULL);
                        obj[0x1B1] = 0;
                        text = SLOT_LOAD(char *, strings);
                        if (text != NULL) {
                            SLOT_STORE((obj + 0x1A4), text);
                            *(u16 *)(obj + 0x1A8) = strlen(text);
                            if (*(s32 *)(obj + 0x1AC) == 0) {
                                *(u16 *)(obj + 0x1AA) = *(u16 *)(obj + 0x1A8);
                            }
                            if (*(u8 *)(SLOT_LOAD(u8 *, (obj + 0x258)) + 0x12) != 0) {
                                obj[0x1B1] = 1;
                            }
                        }
                    }
                }
            }
        } else {
            obj[0x246] = 0;
            obj[0x240] = 1;
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

s32 InterfaceManager_ProcessInput(IMInterface *iface, IMSlot *obj, u32 *events) {
    typedef struct InputEventDef {
        PTR32(u16) target;
        u8 flags;
        u8 type;
        u16 id;
    } InputEventDef;
    typedef struct InputEvent {
        u8 type;
        u8 pad1[7];
        PTR32(InputEventDef) def;
    } InputEvent;
    #define g_interface ((IMInterface *)(void *)&g_interface)
    InterfaceEventMsg msg;
    u32 i;
    InputEvent *event;
    u32 pressed;
    s8 ok;
    u32 j;
    u32 voters;
    u32 votes;
    u8 player;
    s32 vote;

    if (obj != NULL && obj->hidden != 0) {
        return 1;
    }
    for (i = 0; i < events[0]; i++) {
        event = &((InputEvent *)PTR(events[1]))[i];
        pressed = 0;
        switch (event->type) {
        case 1:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 6);
            break;
        case 2:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 7);
            break;
        case 3:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 6);
            break;
        case 4:
            if (StateManager_CurrState() == 5 && GameSettings_GetCodeVariable(0x198, 0) != 0) {
                pressed = 1;
            } else {
                pressed = InterfaceManager_KeyPress(iface, iface->input, 9);
            }
            break;
        case 5:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0xC);
            break;
        case 6:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0xD);
            break;
        case 7:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0xF);
            break;
        case 10:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0xA);
            break;
        case 11:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0xB);
            break;
        case 12:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0xE);
            break;
        case 13:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 2);
            break;
        case 14:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 3);
            break;
        case 15:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 4);
            break;
        case 16:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 5);
            break;
        case 20:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 1);
            break;
        case 21:
            pressed = InterfaceManager_KeyPress(iface, iface->input, 0);
            break;
        case 31:
            if (GETP(iface->header)->type == 8) {
                ok = 1;
                voters = 0;
                votes = 0;
                for (j = 0; j < iface->count && ok; j++) {
                    if (GETP(iface->slots)[j].kind == 6) {
                        if (GETP(g_interface[GETP(iface->slots)[j].child].header)->type == 2 ||
                            GETP(g_interface[GETP(iface->slots)[j].child].header)->type == 3) {
                            player = g_interface[GETP(iface->slots)[j].child].player;
                            vote = GameSettings_GetCodeVariable(0xC9, player);
                            voters++;
                            if (vote == 2) {
                                ok = 0;
                            } else if (vote == 3) {
                                if (GETP(g_interface[GETP(iface->slots)[j].child].callback) != NULL) {
                                    msg.index = g_interface[GETP(iface->slots)[j].child].index;
                                    msg.type = 0xF;
                                    msg.id = GETP(event->def)->id;
                                    msg.arg = pressed;
                                    msg.value = 0;
                                    msg.objState = 0;
                                    msg.handled = 0;
                                    msg.slotId = 0xFFFF;
                                    if (GETP(GETP(event->def)->target) != NULL) {
                                        msg.slotId = *GETP(GETP(event->def)->target);
                                    }
                                    if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->object) != NULL) {
                                        msg.objState = GETP(GETP(GETP(iface->selected)->object)->data)->id;
                                    }
                                    SETP(msg.values, (u32 *)g_interface[GETP(iface->slots)[j].child].vars);
                                    GETP(g_interface[GETP(iface->slots)[j].child].callback)(&msg);
                                    if (msg.result == 11) {
                                        ok = 0;
                                    }
                                }
                                pressed |= 1 << (player - 1);
                                votes++;
                            }
                        }
                    }
                }
                if (!ok || (voters > 1 && votes < 2)) {
                    pressed = 0;
                }
            }
            break;
        }
        if (pressed != 0) {
            if (!InterfaceManager_PlayEvent(iface, obj, event, pressed)) {
                return 0;
            }
        }
    }
    return 1;
#undef g_interface
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
    #define g_interface ((InterfaceState *)(void *)&g_interface)
    extern s8 g_bMemoryCardHWHack;
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
                return 0;  /* bare return in the decomp */
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
                u32 *args = (u32 *)GETP(GETP(((Event *)event)->def)->args);  /* port: table of pointer slots */

                if (((EventArg *)PTR(args[0]))->isVar) {
                    progA = iface->values[((EventArg *)PTR(args[0]))->value];
                } else {
                    progA = ((EventArg *)PTR(args[0]))->value;
                }
                if (((EventArg *)PTR(args[1]))->isVar) {
                    progB = iface->values[((EventArg *)PTR(args[1]))->value];
                } else {
                    progB = ((EventArg *)PTR(args[1]))->value;
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
            return 0;  /* bare return in the decomp */
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
        if (SLOT_LOAD(EventArg *, (u32 *)(GETP(GETP(((Event *)event)->def)->args)) + (0))->isVar) {
            a = iface->values[SLOT_LOAD(EventArg *, (u32 *)(GETP(GETP(((Event *)event)->def)->args)) + (0))->value];
        } else {
            a = SLOT_LOAD(EventArg *, (u32 *)(GETP(GETP(((Event *)event)->def)->args)) + (0))->value;
        }
        if (SLOT_LOAD(EventArg *, (u32 *)(GETP(GETP(((Event *)event)->def)->args)) + (1))->isVar) {
            b = iface->values[SLOT_LOAD(EventArg *, (u32 *)(GETP(GETP(((Event *)event)->def)->args)) + (1))->value];
        } else {
            b = SLOT_LOAD(EventArg *, (u32 *)(GETP(GETP(((Event *)event)->def)->args)) + (1))->value;
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
#undef g_interface
}
void InterfaceManager_Backup_Key(u8 *iface, u8 *key, u8 flags) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
#undef g_interface
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
        PTR32(PTR32(char)) text;
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
    #define g_interfaceDialogSoundPacks ((SoundPack *)(void *)&g_interfaceDialogSoundPacks)
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
                            SLOT_STORE(SLOT_LOAD(u8 *, (slot + 0x258)), GETP(GETP(list->entries)[*(u32 *)(SLOT_LOAD(u8 *, (iface + 0x158)) + 0x2C4)].text));
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, SLOT_LOAD(u8 *, (slot + 0x258))));
                        } else {
                            SLOT_STORE(SLOT_LOAD(u8 *, (slot + 0x258)), NULL);
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, SLOT_LOAD(u8 *, (slot + 0x258))));
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
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, SLOT_LOAD(u8 *, (obj + 0x258))));
                        }
                    }
                    break;
                case 0x24:
                    if (slot[0x243] == 5) {
                        if (GETP(list->entries)[*(u32 *)(obj + 0x2C4)].active && *(u16 *)(object + 0xA) == GETP(list->entries)[*(u32 *)(obj + 0x2C4)].id) {
                            *SLOT_LOAD(s32 *, (object + 0x14)) = (SLOT_LOAD(DialogChar *, (obj + 0x2DC)))->valueB;
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, SLOT_LOAD(u8 *, (obj + 0x258))));
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
#undef g_interfaceDialogSoundPacks
}

void InterfaceManager_MoveObject(u8 *iface, u8 *obj, u8 *data, f32 dt) {
    typedef struct { char str[0x200]; } MoveObjStrBuf;
    typedef struct {
        u32 index;      /* 0x00 */
        u16 unk4;       /* 0x04 */
        u16 item;       /* 0x06 */
        u16 unk8;       /* 0x08 */
        u16 player;     /* 0x0A */
        char text[0x10]; /* 0x0C */
        f32 value;      /* 0x1C */
        u32 state;      /* 0x20 */
        u8 pad24[0x20];
    } MoveObjQuery;
    extern char rndNumberBuffer[16];
    extern char *g_rndNumber[1];
    extern s32 g_iDeltaTimeMS;
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    #define g_playerMessage1 (*(char ***)(void *)&g_playerMessage1)
    #define g_playerMessage2 (*(char ***)(void *)&g_playerMessage2)
    #define g_playerMessage3 (*(char ***)(void *)&g_playerMessage3)
    static u32 s_rndNumberSlot[1];
    char tmp[0x80];
    f32 dtCopy;
    f32 value;
    u8 *snd;
    void *sample;
    f32 pitch;
    u32 i;
    u8 type;
    u32 *ref;
    u32 doText;
    s8 isTime;
    s8 isCount;
    s8 valid;
    s8 setDirty;

    dtCopy = dt;
    ref = NULL;
    if (obj[0x124] != 0) {
        switch (obj[0x243]) {
        case 5:
            if ((s8)obj[0x248] != 0) {
                *(s32 *)(obj + 0x1AC) -= (u32)dt;
                ExtractScaleKey(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 4)) + 0x18, obj + 0x140, dt);
                while (*(u16 *)(obj + 0x1AA) < *(u16 *)(obj + 0x1A8) && *(s32 *)(obj + 0x1AC) < 0) {
                    snd = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 4)) + 4));
                    if (snd != NULL) {
                        pitch = RandomAdd(*(f32 *)(snd + 0xC), *(f32 *)(snd + 0x10));
                        sample = SoundAudioSample_Play(*(u32 *)(snd + 0x14));
                        if (sample != NULL) {
                            SoundAudioSample_Set(sample, 0, 32767.0f * *(f32 *)(snd + 8));
                            SoundAudioSample_Set(sample, 1, 65536.0f * pitch);
                        }
                    }
                    *(s32 *)(obj + 0x1AC) += *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 4)) + 0x14);
                    (*(u16 *)(obj + 0x1AA))++;
                }
            }
            break;
        case 8: {
            MoveObjStrBuf buf = { "" };

            for (i = 0; i < *SLOT_LOAD(u32 *, (SLOT_LOAD(u8 *, (iface + 0x20)) + 0x1C)); i++) {
                sprintf(tmp, "v%d = %d|", i, *(s32 *)(iface + i * 4 + 0x98));
                strcat(buf.str, tmp);
            }
            InterfaceManager_SetObjectString(obj, buf.str);
            break;
        }
        }
    }
    if (SLOT_LOAD(void *, (iface + 0x158)) != NULL) {
        InterfaceManager_MoveDialogSystem(iface, obj, dt);
    }
    type = obj[0x245];
    if (type != 0 && type != 3) {
        value = 99999.99f;
        doText = 1;
        isTime = 0;
        isCount = 0;
        valid = 1;
        setDirty = 1;
        if (data != NULL) {
            switch (type) {
            case 2:
                if (SLOT_LOAD(void *, (iface + 0x90)) != NULL) {
                    MoveObjQuery q = {0, 1};

                    data = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x40));
                    q.index = iface[0x7D];
                    q.item = *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA);
                    if (SLOT_LOAD(u8 *, (iface + 0xC)) != NULL && SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 0xC)) + 0x25C)) != NULL) {
                        q.player = *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (iface + 0xC)) + 0x25C)) + 0x44));
                    }
                    (((void (*)(MoveObjQuery *))SLOT_LOAD(void *, (iface + 0x90))))(&q);
                    if (data[0] != 0) {
                        value = q.value;
                    }
                    if (q.state == 0 && obj[0x246] != 0) {
                        InterfaceManager_SetObjectState(iface, obj, 0, 0);
                    }
                    if (q.state == 1 && obj[0x246] == 0 && *(u32 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x1C) != 0xFFFFFFFF) {
                        InterfaceManager_SetObjectState(iface, obj, 1, 0);
                    }
                    if (q.state == 2) {
                        InterfaceManager_SetObjectState(iface, obj, 1, 0);
                    }
                    if (q.state == 10) {
                        InterfaceManager_SetObjectState(iface, obj, 100, 0);
                    }
                    if (obj[0x243] == 5) {
                        u8 *anim = SLOT_LOAD(u8 *, (obj + 0x258));

                        if (anim[0x10] == 1) {
                            if (q.item < 0xFFFF) {
                                u32 *list = SLOT_LOAD(u32 *, (anim + 8));

                                if (q.item >= list[0]) {
                                    q.item = (u16)list[0] - 1;
                                }
                                if (SLOT_LOAD(char **, (obj + 0x1A0)) != SLOT_LOAD(char **, (u32 *)PTR(list[1]) + q.item)) {
                                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (u32 *)PTR(list[1]) + q.item));
                                }
                            }
                        } else if ((s8)q.text[0] != 0) {
                            *(u32 *)(obj + 0x1A4) = 0;
                            strcpy((char *)(obj + 0x1C0), q.text);
                            *(u16 *)(obj + 0x1A8) = strlen(q.text);
                        } else if (*(u32 *)(obj + 0x1A4) == 0) {
                            InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (obj + 0x1A0)));
                        }
                        doText = 0;
                    } else if (obj[0x243] == 6 && obj[0x246] == 1) {
                        InterfaceManager_SetObjectReference(g_interface[*(u32 *)(obj + 0x270)], q.item, q.state != 0);
                    }
                }
                break;
            case 4:
                Gfx_SetProjectionMatrix(*(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA));
                break;
            case 5:
                value = *(f32 *)(data + 0x24);
                break;
            case 6:
                value = *(f32 *)(data + 0x28);
                break;
            case 8:
                if (SLOT_LOAD(u8 *, (data + 0x7C)) != NULL) {
                    value = 15.915494f * (3.1415927f + ArcTan(*(f32 *)(SLOT_LOAD(u8 *, (data + 0x7C)) + 0x38), *(f32 *)(SLOT_LOAD(u8 *, (data + 0x7C)) + 0x30)));
                }
                break;
            case 10:
                value = *(u32 *)(data + 0x10);
                break;
            case 11:
                value = *(f32 *)(data + 0x20);
                break;
            case 12:
                value = *(u32 *)(data + 0x14);
                break;
            case 13:
                value = *(f32 *)(data + 0x2C);
                break;
            case 14:
                value = *(u32 *)(data + 0x1C);
                break;
            case 15:
                value = *(u32 *)(data + 0x30);
                break;
            case 16:
                if ((s8)data[0x74] != 0) {
                    if (obj[0x243] == 5) {
                        InterfaceManager_SetObjectString(obj, (char *)(data + 0x74));
                    }
                    obj[0x84] = 1;
                    obj[0x2C] = 1;
                    obj[0x54] = 1;
                }
                break;
            case 17:
                if (SLOT_LOAD(char **, (data + 0x48)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x48)));
                }
                break;
            case 18:
                if (SLOT_LOAD(char **, (data + 0x4C)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x4C)));
                }
                break;
            case 19:
                if (SLOT_LOAD(char **, (data + 0x60)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x60)));
                }
                break;
            case 20:
                if (data[0x82] != 0) {
                    value = 0.0f;
                    obj[0x54] = 1;
                    obj[0x2C] = 1;
                }
                break;
            case 21:
                if (data[0x81] != 0) {
                    value = 0.0f;
                    obj[0x54] = 1;
                    obj[0x2C] = 1;
                }
                break;
            case 70:
                value = data[0xC9];
                break;
            case 71:
                value = data[0xCA];
                break;
            case 72:
                value = data[0xCB];
                break;
            case 73:
                value = data[0xCC];
                break;
            case 91:
                if (*(s32 *)(obj + 0x27C) < 0) {
                    *(s32 *)(obj + 0x27C) = 500;
                    obj[0x84] = 1;
                    obj[0x54] = 1;
                    obj[0x2C] = 1;
                    *(f32 *)(obj + 0x278) = RandomAdd(0.0f, *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA));
                    sprintf(rndNumberBuffer, "%1.0f", *(f32 *)(obj + 0x278));
                    s_rndNumberSlot[0] = HND(rndNumberBuffer);  /* port: a table of string slots */
                    SLOT_STORE((data + 0x5C), s_rndNumberSlot);
                    InterfaceManager_SetObjectText(obj, (char **)s_rndNumberSlot);
                } else {
                    *(s32 *)(obj + 0x27C) -= g_iDeltaTimeMS;
                }
                break;
            case 92:
                if (*(s32 *)(obj + 0x27C) < 0) {
                    *(f32 *)(obj + 0x278) += *(f32 *)(obj + 0x284);
                    *(s32 *)(obj + 0x27C) = 50;
                    if ((obj[0x249] != 0 && *(f32 *)(obj + 0x278) < *(f32 *)(obj + 0x280)) ||
                        (obj[0x249] == 0 && *(f32 *)(obj + 0x278) > *(f32 *)(obj + 0x280))) {
                        obj[0x84] = 1;
                        obj[0x54] = 1;
                        obj[0x2C] = 1;
                        *(f32 *)(obj + 0x280) = RandomAdd(0.0f, *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA));
                        *(f32 *)(obj + 0x284) = 0.1f * (*(f32 *)(obj + 0x280) - *(f32 *)(obj + 0x278));
                        if (*(f32 *)(obj + 0x278) < *(f32 *)(obj + 0x280)) {
                            obj[0x249] = 0;
                        } else {
                            obj[0x249] = 1;
                        }
                    }
                } else {
                    *(s32 *)(obj + 0x27C) -= g_iDeltaTimeMS;
                    sprintf(rndNumberBuffer, "%1.0f", *(f32 *)(obj + 0x278));
                    s_rndNumberSlot[0] = HND(rndNumberBuffer);  /* port: a table of string slots */
                    SLOT_STORE((data + 0x5C), s_rndNumberSlot);
                    InterfaceManager_SetObjectText(obj, (char **)s_rndNumberSlot);
                }
                break;
            case 40: {
                u32 **slot = (u32 **)(data + *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA) * 4);

                if (slot[0x80 / 4] != NULL) {
                    if (*slot[0x80 / 4] != *(u32 *)(obj + 0x2F8)) {
                        obj[0x84] = 1;
                        obj[0x54] = 1;
                        obj[0x2C] = 1;
                    }
                    *(u32 *)(obj + 0x2F8) = *slot[0x80 / 4];
                    ref = slot[0x80 / 4];
                }
                break;
            }
            case 41:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
            case 48:
                if (SLOT_LOAD(u16 *, (data + 0xC4)) != NULL) {
                    setDirty = 0;
                    value = (SLOT_LOAD(u16 *, (data + 0xC4)))[(u8)(type - 41)];
                    if ((s32)data[0xC8] == 1) {
                        obj[0x2C] = 1;
                        obj[0x54] = 1;
                        obj[0x84] = 1;
                    }
                }
                break;
            case 49:
                if (SLOT_LOAD(char **, (data + 0x64)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x64)));
                }
                break;
            case 60:
                if (SLOT_LOAD(char **, (data + 0x40)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x40)));
                }
                break;
            case 61:
                if (SLOT_LOAD(char **, (data + 0x44)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x44)));
                }
                break;
            case 62:
                if (data[0x80] == 1) {
                    obj[0x240] = 0;
                } else if (data[0x80] == 0) {
                    obj[0x240] = 1;
                }
                break;
            case 65:
                if (g_playerMessage1 != NULL) {
                    InterfaceManager_SetObjectText(obj, g_playerMessage1);
                    g_playerMessage1 = NULL;
                }
                break;
            case 66:
                if (g_playerMessage2 != NULL) {
                    InterfaceManager_SetObjectText(obj, g_playerMessage2);
                    g_playerMessage2 = NULL;
                }
                break;
            case 67:
                if (g_playerMessage3 != NULL) {
                    InterfaceManager_SetObjectText(obj, g_playerMessage3);
                    g_playerMessage3 = NULL;
                }
                break;
            case 80:
                *(u16 *)(obj + 0x24A) = 0xFFFF;
                if (data[0x34] != 0) {
                    *(u16 *)(obj + 0x24A) = *(u16 *)(data + 0x6C);
                }
                break;
            case 81:
                if (SLOT_LOAD(char **, (data + 0x68)) != NULL) {
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, (data + 0x68)));
                }
                break;
            case 82:
                value = *(u32 *)(data + 0x38);
                break;
            case 83:
                *(u16 *)(obj + 0x24A) = 0xFFFF;
                if (data[0x35] != 0) {
                    *(u16 *)(obj + 0x24A) = *(u16 *)(data + 0x6E);
                }
                break;
            case 84:
                value = *(u32 *)(data + 0x3C);
                break;
            case 86:
                *(u16 *)(obj + 0x24A) = 0xFFFF;
                if (data[0x36] != 0) {
                    *(u16 *)(obj + 0x24A) = *(u16 *)(data + 0x70);
                }
                break;
            case 50: {
                u8 *world;
                u8 *e;
                u32 j;
                u32 id;

                id = *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA);
                world = GETP(g_worldData.world);
                if (world != NULL) {
                    valid = 0;
                    for (j = 0; j < *(u32 *)(world + 0x50); j++) {
                        e = SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x54)) + j);
                        if (e[1] != 0 && id == e[2]) {
                            if (*SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x54)) + j) + 0xC)) == 0x17) {
                                valid = 1;
                            }
                            obj[0x84] = 1;
                            obj[0x130] = 1;
                            obj[0x54] = 1;
                            obj[0x2C] = 1;
                            value = *(f32 *)(SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x54)) + j) + 0x8);
                            break;
                        }
                    }
                }
                isTime = 1;
                break;
            }
            case 51: {
                u8 *world;
                u8 *e;
                u32 j;
                u32 id;

                id = *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA);
                world = GETP(g_worldData.world);
                if (world != NULL) {
                    valid = 0;
                    for (j = 0; j < *(u32 *)(world + 0x48); j++) {
                        e = SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + j);
                        if (e[1] != 0 && id == e[2]) {
                            if (*SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + j) + 0xC)) == 0x17) {
                                valid = 1;
                            }
                            obj[0x84] = 1;
                            obj[0x130] = 1;
                            obj[0x54] = 1;
                            obj[0x2C] = 1;
                            value = *(f32 *)(SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + j) + 0x4);
                            break;
                        }
                    }
                }
                isCount = 1;
                break;
            }
            case 100:
                if (*(u32 *)(iface + 0x4) == 2) {
                    char *name = GameSettings_GetActiveMemoryCardFileName(*SLOT_LOAD(u16 *, (iface + 0x20)) == 5,
                                                                         *(u16 *)(SLOT_LOAD(u8 *, (obj + 0x25C)) + 0xA),
                                                                         *SLOT_LOAD(char **, SLOT_LOAD(u8 *, (obj + 0x258))));

                    if (name != NULL) {
                        InterfaceManager_SetObjectText(obj, &name);
                    }
                    if (name != NULL && SLOT_LOAD(void *, (obj + 0x254)) != NULL) {
                        obj[0x240] = 0;
                    } else {
                        obj[0x240] = 1;
                    }
                    if (obj[0x240] == 0) {
                        InterfaceManager_CheckConditions(iface, obj);
                    }
                }
                break;
            case 101:
                if ((SLOT_LOAD(u8 *, (iface + 0xC)))[0x245] != 100) {
                    obj[0x240] = 1;
                } else if (SLOT_LOAD(void *, (obj + 0x254)) != NULL) {
                    obj[0x240] = 0;
                    InterfaceManager_SetObjectString(obj, GameSettings_GetActiveMemoryCardLevelDetails());
                }
                break;
            case 102:
                value = GameSettings_GetCodeVariable(0xD0, 0);
                break;
            case 103:
                value = GameSettings_GetCodeVariable(0xCF, 0);
                break;
            case 104:
                InterfaceManager_SetControllerConfigInputString();
                break;
            }
        }
        if (value != 99999.99f) {
            if (setDirty) {
                obj[0xB4] = 1;
                obj[0x84] = 1;
                obj[0x118] = 1;
                obj[0x54] = 1;
                obj[0x2C] = 1;
            }
            dt = value;
            if (doText && obj[0x243] == 5) {
                if (isCount) {
                    if (valid) {
                        sprintf((char *)(obj + 0x1C0), "%d", (u32)value);
                        *(u32 *)(obj + 0x1A4) = 0;
                        *(u16 *)(obj + 0x1A8) = strlen((char *)(obj + 0x1C0));
                    } else {
                        sprintf((char *)(obj + 0x1C0), " ");
                        *(u32 *)(obj + 0x1A4) = 0;
                        *(u16 *)(obj + 0x1A8) = strlen((char *)(obj + 0x1C0));
                    }
                } else if (isTime) {
                    if (valid) {
                        u32 secs = (u32)value / 1000;
                        u32 mins = secs / 60;

                        sprintf((char *)(obj + 0x1C0), "%.2d:%.2d", mins, secs - mins * 60);
                        *(u32 *)(obj + 0x1A4) = 0;
                        *(u16 *)(obj + 0x1A8) = strlen((char *)(obj + 0x1C0));
                    } else {
                        sprintf((char *)(obj + 0x1C0), " ");
                        *(u32 *)(obj + 0x1A4) = 0;
                        *(u16 *)(obj + 0x1A8) = strlen((char *)(obj + 0x1C0));
                    }
                } else {
                    *(u32 *)(obj + 0x1A4) = 0;
                    sprintf((char *)(obj + 0x1C0), "%d", (u32)value);
                    *(u16 *)(obj + 0x1A8) = strlen((char *)(obj + 0x1C0));
                }
            }
        }
    }
    if (obj[0x240] == 0) {
        if (obj[0x243] == 1) {
            u8 *anim = SLOT_LOAD(u8 *, (obj + 0x258));

            if (anim[0xC] != 0) {
                u32 idx = ((u32 *)(iface + 0x98))[*(u16 *)(anim + 0xE)];

                if (idx >= *(u32 *)(anim + 0)) {
                    idx = *(u32 *)(anim + 0) - 1;
                }
                *(u16 *)(obj + 0x24A) = (SLOT_LOAD(u32 *, (anim + 4)))[idx];
                if (*(u32 *)(obj + 0x28C) != idx) {
                    *(u32 *)(obj + 0x28C) = idx;
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
            } else {
                ExtractTextureKey(anim, obj + 0x128, dt);
                *(u16 *)(obj + 0x24A) = *(u16 *)(obj + 0x128);
            }
        }
        if (obj[0x243] == 5 && (SLOT_LOAD(u8 *, (obj + 0x258)))[0x10] == 1) {
            u8 *list = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x258)) + 8));

            if (list[8] != 0) {
                u32 idx = ((u32 *)(iface + 0x98))[*(u16 *)(list + 0xA)];

                if (idx >= *(u32 *)(list + 0)) {
                    idx = *(u32 *)(list + 0) - 1;
                }
                if (idx != *(u32 *)(obj + 0x28C)) {
                    *(u32 *)(obj + 0x28C) = idx;
                    InterfaceManager_SetObjectText(obj, SLOT_LOAD(char **, SLOT_LOAD(u32 *, (list + 4)) + idx));
                }
            }
        }
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x10) != 0 /* track key count */) {
            ExtractColorKey(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x10, obj, dt);
        } else {
            ExtractColorKey(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x60)) + 0x18, obj, dt);
        }
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x18) != 0 /* track key count */) {
            ExtractScaleKey(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x18, obj + 0x30, dt);
        } else {
            ExtractScaleKey(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x60)) + 0x10, obj + 0x30, dt);
        }
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x8) != 0 /* track key count */) {
            ExtractScaleKey(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x8, obj + 0x60, dt);
        } else {
            ExtractScaleKey(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x60)), obj + 0x60, dt);
        }
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x28) != 0 /* track key count */) {
            ExtractScaleKey(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x28, obj + 0x90, dt);
        } else {
            ExtractScaleKey(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x60)) + 0x8, obj + 0x90, dt);
        }
        if (*(u32 *)(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x20) != 0 /* track key count */) {
            ExtractUVKey(SLOT_LOAD(u8 *, (obj + 0x254)) + 0x20, obj + 0xC0, dt);
        } else {
            ExtractUVKey(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0x25C)) + 0x60)) + 0x20, obj + 0xC0, dt);
        }
        ExtractActiveKey(SLOT_LOAD(u8 *, (obj + 0x254)), obj + 0x11C, dtCopy);
        if (ref != NULL) {
            *(u32 *)(obj + 0x24) = *ref;
        }
    }
#undef g_interface
#undef g_playerMessage1
#undef g_playerMessage2
#undef g_playerMessage3
}

void InterfaceManager_ExecuteActions(u8 *iface, u8 *list) {
    typedef struct { u8 pad[0x98]; s32 vars[1]; } IfaceVars;
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
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
        PTR32(PTR32(WorldVar)) vars;
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
                            if (GETP(GETP(((World *)GETP(g_worldData.world))->vars)[k])->id == (SLOT_LOAD(Variable *, (SLOT_LOAD(u8 *, (GETP(ifc->header) + 0x1C)) + 4)))[i].offset) {
                                ifc->vars[k] = GETP(GETP(((World *)GETP(g_worldData.world))->vars)[k])->value;
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
#undef g_interface
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
void InterfaceManager_RenderInterface(RenderInterface *iface);

void InterfaceManager_Render(void) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    extern u32 g_interfaceRenderOrder[0x12];
    #define g_numInterfacesToRender (*(s32 *)(void *)&g_numInterfacesToRender)
    u32 i;
    u8 *iface;

    for (i = 0; i < g_numInterfacesToRender; i++) {
        iface = g_interface[g_interfaceRenderOrder[i]];
        if (*(u32 *)iface != 0 /* interface count */ && (*(u32 *)(iface + 4) != 1 || *(u32 *)(iface + 0x10) != 0) && iface[0x7F] == 0) {
            InterfaceManager_RenderInterface((RenderInterface *)iface);
        }
    }
#undef g_interface
#undef g_numInterfacesToRender
}
void InterfaceManager_RenderObject(IMInterface *iface, s32 index);

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

static inline void InterfaceManager_DrawDebugRect(Vector4 *corner, f32 w, f32 h) {
    u8 red[4] = {0xFF, 0x80, 0x80, 0xFF};
    Vector4 q0 = *corner;
    Vector4 q1 = *corner;
    Vector4 q2 = *corner;

    q1.x += w;
    q2.y += h;
    Gfx_Add2DFXLine(&q0, &q1, red, red, 1);
    Gfx_Add2DFXLine(&q0, &q2, red, red, 1);
    q0.x += w;
    q0.y += h;
    Gfx_Add2DFXLine(&q0, &q1, red, red, 1);
    Gfx_Add2DFXLine(&q0, &q2, red, red, 1);
}

static inline void InterfaceManager_DrawDebugCross(Vector4 *pos) {
    u8 white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    Vector4 c0 = *pos;
    Vector4 c1 = *pos;

    c0.x -= 10.0f;
    c1.x += 10.0f;
    Gfx_Add2DFXLine(&c0, &c1, white, white, 1);
    c0.x += 10.0f;
    c1.x -= 10.0f;
    c0.y -= 10.0f;
    c1.y += 10.0f;
    Gfx_Add2DFXLine(&c0, &c1, white, white, 1);
}

void InterfaceManager_RenderObject(IMInterface *iface, s32 index) {
    typedef struct { Vector4 pos; Vector4 quat; Vector4 vel; PTR32(u8) p; u32 id; f32 unk38; } RenderObjParams2;
    typedef struct { f32 x; f32 y; u8 pad8[8]; u8 rgba[4]; u8 pad14[0xC]; } RenderObjPoint;
    typedef struct { u32 count; PTR32(RenderObjPoint) points; u8 type; } RenderObjPoly;
    typedef struct { u8 padE[0xE]; u8 blend; u8 scaled; u8 pad10; u8 align; u8 pad12[2]; u16 width; u16 height; f32 lineSpacing; } RenderObjTextInfo;
    extern s8 g_bRenderCenterPoints;
    extern s8 g_bRenderInterfaceHelpers;
    extern u8 g_carAVTable[];
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    #define g_interface ((IMInterface *)(void *)&g_interface)
    char text[0x200];
    FxParams fx;
    Matrix4 scaleMtx;
    Matrix4 mtx;
    RenderObjParams2 prm;
    Matrix4 orient;
    Vector4 cam;
    f32 width;
    f32 height;
    u8 col0[4];
    u8 col1[4];
    f32 sinA;
    f32 cosA;
    IMSlot *carSlot = NULL;
    IMSlot *slot = &GETP(iface->slots)[index];
    u32 i;
    Vector4 *ofs;

    if (slot->hidden || iface->unk85) {
        return;
    }
    if (g_bRenderCenterPoints && GETP(slot->object)->unkC == 0) {
        if (slot->kind == 5 && GETP(slot->unk258)[0x12]) {
            f32 w = *(u16 *)(GETP(slot->unk258) + 0x14);
            f32 h = *(u16 *)(GETP(slot->unk258) + 0x16);
            Vector4 corner = {0.0f, 0.0f, 0.0f, 1.0f};

            corner.x = slot->pos.x - 0.5f * w - 0.5f * w * GETP(slot->object)->anchorX;
            corner.y = slot->pos.y - 0.5f * h - 0.5f * h * GETP(slot->object)->anchorY;
            InterfaceManager_DrawDebugRect(&corner, w, h);
        }
        InterfaceManager_DrawDebugCross(&slot->pos);
    }
    if (slot->unk125) {
        return;
    }
    if (!slot->visible) {
        switch (slot->kind) {
        case 4:
            slot->unk244 = 0;
            if (GETP(slot->thing) != NULL) {
                Thing_Release(GETP(slot->thing));
                SETP(slot->thing, NULL);
            }
            break;
        case 3:
            slot->unk244 = 0;
            SETP(slot->thing, NULL);
            break;
        }
        slot->dirty84 = 1;
        slot->dirtyB4 = 1;
        slot->dirty54 = 1;
        slot->dirty2C = 1;
        slot->dirty118 = 1;
        if (slot->kind == 1) {
            slot->dirty130 = 1;
        }
        if (slot->kind == 6) {
            InterfaceManager_SetLoadState(&g_interface[slot->child], 0);
        }
        return;
    }
    {
        s8 restore;
        Vector4 savedPos;
        Vector4 savedRot;
        Vector4 savedScale;
        Vector4 thingPos;

        restore = 0;
        if (GETP(slot->parent) != NULL) {
            if (*(u32 *)(GETP(slot->unk254) + 8) == 0) {
                slot->pos = GETP(slot->parent)->pos;
            }
            if (*(u32 *)(GETP(slot->unk254) + 0x28) == 0) {
                slot->rot = GETP(slot->parent)->rot;
            }
        }
        if (GETP(slot->offset) != NULL) {
            restore = 1;
            savedPos = slot->pos;
            slot->pos.x += GETP(slot->offset)->pos.x;
            slot->pos.y += GETP(slot->offset)->pos.y;
            slot->pos.z += GETP(slot->offset)->pos.z;
            savedRot = slot->rot;
            slot->rot.x += GETP(slot->offset)->rot.x;
            slot->rot.y += GETP(slot->offset)->rot.y;
            slot->rot.z += GETP(slot->offset)->rot.z;
            savedScale = slot->scale;
            slot->scale.x += GETP(slot->offset)->scale.x;
            slot->scale.y += GETP(slot->offset)->scale.y;
            slot->scale.z += GETP(slot->offset)->scale.z;
        }
        if (GETP(slot->car) != NULL) {
            for (i = 0; i < iface->count; i++) {
                if (GETP(iface->slots)[i].renderType == 4) {
                    carSlot = &GETP(iface->slots)[i];
                    break;
                }
            }
            Gfx_GetCameraPosition(&cam);
            if (carSlot != NULL) {
                cam.x -= carSlot->pos.x;
                cam.y -= carSlot->pos.y;
                cam.z -= carSlot->pos.z;
                SetOrientation(&mtx, 0.017453292f * (slot->rot.y + carSlot->rot.y), 0.017453292f * (slot->rot.x + carSlot->rot.x),
                               0.017453292f * (slot->rot.z + carSlot->rot.z));
            } else {
                SetOrientation(&mtx, 0.017453292f * slot->rot.y, 0.017453292f * slot->rot.x, 0.017453292f * slot->rot.z);
            }
            if (SLOT_LOAD(u8 *, (GETP(slot->car) + 0x8C)) == g_carAVTable) {
                ofs = SLOT_LOAD(Vector4 *, (GETP(slot->car) + 0x3BC));
                if (180.0f == slot->rot.z || -180.0f == slot->rot.z) {
                    cam.x -= ofs->x;
                    cam.y -= ofs->y;
                    cam.z -= ofs->z;
                } else {
                    cam.x += ofs->x;
                    cam.y += ofs->y;
                    cam.z += ofs->z;
                }
            }
            if (GETP(slot->offset) != NULL) {
                mtx.v[3].x = GETP(GETP(slot->offset)->object)->anchorX + (cam.x + savedPos.x);
                mtx.v[3].y = GETP(GETP(slot->offset)->object)->anchorY + (cam.y + savedPos.y);
                mtx.v[3].z = cam.z + savedPos.z;
            } else {
                mtx.v[3].x = cam.x + slot->pos.x;
                mtx.v[3].y = cam.y + slot->pos.y;
                mtx.v[3].z = cam.z + slot->pos.z;
            }
            memcpy(&scaleMtx, &g_identity, sizeof(Matrix4));
            scaleMtx.v[0].x = slot->scale.x;
            scaleMtx.v[1].y = slot->scale.y;
            scaleMtx.v[2].z = slot->scale.z;
            MultiplyMatrix(&scaleMtx, &mtx, (Matrix4 *)(GETP(slot->car) + 0x10));
            SubMatrixToQuaternion((Matrix4 *)(GETP(slot->car) + 0x10), (Quaternion *)(GETP(slot->car) + 0x50));
            *(u32 *)(GETP(slot->car) + 0x74) |= 0x80;
            if (SLOT_LOAD(u8 *, (GETP(slot->car) + 0x8C)) == g_carAVTable) {
                CarA_DrawReflection(GETP(slot->car));
            } else {
                (((void (*)(u8 *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (GETP(slot->car) + 0x8C)) + 0x10))))(GETP(slot->car));
            }
        }
        if (slot->renderType == 7) {
            if (GETP(iface->owner) != NULL && *(u32 *)(GETP(iface->owner) + 0x2C) != 0) {
                InterfaceManager_DrawRadarBlips(&slot->pos.x, (u8 *)iface);
            }
        } else if (slot->renderType == 30) {
            InterfaceManager_DrawMap(slot, iface);
        } else if (slot->renderType != 4) {
            switch (slot->kind) {
            case 6:
                if ((u32)slot->child != iface->index) {
                    InterfaceManager_RenderInterface(&g_interface[slot->child]);
                }
                break;
            case 0:
            case 1:
                InterfaceManager_DrawObject_Standard_Image(slot);
                break;
            case 2:
                InterfaceManager_DrawObject_Standard_Geometry(slot, iface);
                break;
            case 8:
                if (!g_bRenderInterfaceHelpers) {
                    break;
                }
            case 5: {
                u32 n = M2C_FIELD(slot, u16 *, 0x1AA);

                if (!M2C_FIELD(slot, u8 *, 0x1B1) && n < M2C_FIELD(slot, u16 *, 0x1A8)) {
                    if (SLOT_LOAD(char *, (u8 *)(slot) + 0x1A4) != NULL) {
                        strncpy(text, SLOT_LOAD(char *, (u8 *)(slot) + 0x1A4), n);
                    } else {
                        strncpy(text, (char *)slot + 0x1C0, n);
                    }
                    text[n] = 0;
                } else {
                    if (SLOT_LOAD(char *, (u8 *)(slot) + 0x1A4) != NULL) {
                        strcpy(text, SLOT_LOAD(char *, (u8 *)(slot) + 0x1A4));
                    } else {
                        strcpy(text, (char *)slot + 0x1C0);
                    }
                    n = 1;
                }
                if (n != 0) {
                    RenderObjTextInfo *info = SLOT_LOAD(RenderObjTextInfo *, ((u8 *)GETP(slot->object) + 0x14));

                    Gfx_SetTextColor(M2C_FIELD(slot, u8 *, 0x24), M2C_FIELD(slot, u8 *, 0x25), M2C_FIELD(slot, u8 *, 0x26),
                                     M2C_FIELD(slot, u8 *, 0x27));
                    Gfx_FontManager_GetPrintRect(text, M2C_FIELD(slot, u8 *, 0x1B0), &width, &height, M2C_FIELD(slot, f32 *, 0x140));
                    if (info->scaled) {
                        if (M2C_FIELD(slot, u8 *, 0x1B1)) {
                            width = info->width;
                            width *= 0.5f;
                            height = info->height;
                            height *= 0.5f;
                            Gfx_FontManager_PrintParagraph(text, M2C_FIELD(slot, u16 *, 0x1AA), M2C_FIELD(slot, u8 *, 0x1B0), info->blend,
                                                           info->align, &slot->scale.x,
                                                           slot->pos.x - width - width * GETP(slot->object)->anchorX,
                                                           slot->pos.y - height - height * GETP(slot->object)->anchorY,
                                                           M2C_FIELD(slot, f32 *, 0x140), info->width, info->height, info->lineSpacing);
                        } else {
                            width *= 0.5f * slot->scale.x;
                            height *= 0.5f * slot->scale.y;
                            Gfx_FontManager_PrintScale(text, M2C_FIELD(slot, u8 *, 0x1B0), &slot->scale.x, info->blend,
                                                       slot->pos.x - width - width * GETP(slot->object)->anchorX,
                                                       slot->pos.y - height - height * GETP(slot->object)->anchorY,
                                                       M2C_FIELD(slot, f32 *, 0x140));
                        }
                    } else {
                        if (M2C_FIELD(slot, u8 *, 0x1B1)) {
                            width = info->width;
                            width *= 0.5f;
                            height = info->height;
                            height *= 0.5f;
                            Gfx_FontManager_PrintParagraph(text, M2C_FIELD(slot, u16 *, 0x1AA), M2C_FIELD(slot, u8 *, 0x1B0), info->blend,
                                                           info->align, NULL,
                                                           slot->pos.x - width - width * GETP(slot->object)->anchorX,
                                                           slot->pos.y - height - height * GETP(slot->object)->anchorY,
                                                           M2C_FIELD(slot, f32 *, 0x140), info->width, info->height, info->lineSpacing);
                        } else {
                            width *= 0.5f;
                            height *= 0.5f;
                            Gfx_FontManager_Print(text, M2C_FIELD(slot, u8 *, 0x1B0), info->blend,
                                                  slot->pos.x - width - width * GETP(slot->object)->anchorX,
                                                  slot->pos.y - height - height * GETP(slot->object)->anchorY,
                                                  M2C_FIELD(slot, f32 *, 0x140));
                        }
                    }
                }
                break;
            }
            case 4:
                if (!slot->unk244) {
                    SETP(prm.p, GETP(slot->unk258));
                    prm.unk38 = 0.0f;
                    prm.pos.x = prm.pos.y = prm.pos.z = 0.0f;
                    prm.vel.x = prm.vel.y = prm.vel.z = 0.0f;
                    prm.quat.x = prm.quat.y = prm.quat.z = 0.0f;
                    prm.quat.w = 1.0f;
                    prm.id = 0;
                    SETP(slot->thing, ThingManager_DARYL_CreateThing(2, &prm));
                    slot->unk244 = 1;
                }
                if (GETP(slot->thing) != NULL) {
                    carSlot = NULL;
                    for (i = 0; i < iface->count; i++) {
                        if (GETP(iface->slots)[i].renderType == 4) {
                            carSlot = &GETP(iface->slots)[i];
                            break;
                        }
                    }
                    Gfx_GetCameraPosition(&thingPos);
                    if (carSlot != NULL) {
                        PSVECSubtract(&thingPos, &carSlot->pos, &thingPos);
                        SetOrientation(&orient, 0.017453292f * (slot->rot.y + carSlot->rot.y), 0.017453292f * (slot->rot.x + carSlot->rot.x),
                                       0.017453292f * (slot->rot.z + carSlot->rot.z));
                    } else {
                        SetOrientation(&orient, 0.017453292f * slot->rot.y, 0.017453292f * slot->rot.x, 0.017453292f * slot->rot.z);
                    }
                    orient.v[3].x = thingPos.x;
                    orient.v[3].y = thingPos.y;
                    orient.v[3].z = thingPos.z;
                    XformVec(&orient, &slot->pos, &thingPos);
                    *(Vector4 *)((u8 *)GETP(slot->thing) + 0x40) = thingPos;
                }
                break;
            case 3:
                if (!slot->unk244) {
                    fx.id = (u32)HND(GETP(slot->unk258));
                    fx.pos.x = fx.pos.y = fx.pos.z = 0.0f;
                    fx.vel.x = fx.vel.y = fx.vel.z = 0.0f;
                    fx.quat.x = fx.quat.y = fx.quat.z = 0.0f;
                    fx.quat.w = 1.0f;
                    fx.unk44 = 0;
                    fx.unk48 = 0.0f;
                    fx.unk4C = 0.0f;
                    fx.angVel = g_zeroVector;
                    SETP(slot->thing, ThingManager_DARYL_CreateThing(1, &fx));
                    slot->unk244 = 1;
                }
                if (GETP(slot->thing) != NULL) {
                    *(Vector4 *)((u8 *)GETP(slot->thing) + 0x40) = slot->pos;
                }
                break;
            case 9:
                if (g_bRenderInterfaceHelpers) {
                    InterfaceManager_DrawLine(slot);
                }
                break;
            case 10:
                if (g_bRenderInterfaceHelpers) {
                    InterfaceManager_DrawBox(slot);
                }
                break;
            case 11: {
                Vector4 from __attribute__((aligned(16)));
                Vector4 to __attribute__((aligned(16)));
                Vector4 uvA = {0.0f, 0.0f, 0.0f, 1.0f};
                Vector4 uvB = {0.0f, 0.0f, 0.0f, 1.0f};

                uvA.x = M2C_FIELD(slot, f32 *, 0xC0);
                uvA.y = M2C_FIELD(slot, f32 *, 0xC4);
                uvB.x = M2C_FIELD(slot, f32 *, 0xD8);
                uvB.y = M2C_FIELD(slot, f32 *, 0xDC);
                from = slot->pos;
                PSVECAdd(&from, &slot->rot, &to);
                Gfx_Add2DFXLine(&from, &to, (u8 *)slot + 0x24, (u8 *)slot + 0x24, 5);
                if (*(u32 *)((u8 *)GETP(slot->object) + 0x10) != 0) {
                    from = to;
                }
                PSVECAdd(&from, &slot->scale, &to);
                Gfx_Add2DFXLine(&from, &to, (u8 *)slot + 0x24, (u8 *)slot + 0x24, 5);
                if (*(u32 *)((u8 *)GETP(slot->object) + 0x10) != 0) {
                    from = to;
                }
                PSVECAdd(&from, &uvA, &to);
                Gfx_Add2DFXLine(&from, &to, (u8 *)slot + 0x24, (u8 *)slot + 0x24, 5);
                if (*(u32 *)((u8 *)GETP(slot->object) + 0x10) != 0) {
                    from = to;
                }
                PSVECAdd(&from, &uvB, &to);
                Gfx_Add2DFXLine(&from, &to, (u8 *)slot + 0x24, (u8 *)slot + 0x24, 5);
                break;
            }
            case 12: {
                RenderObjPoly *poly = SLOT_LOAD(RenderObjPoly *, ((u8 *)GETP(slot->object) + 0x14));
                Vector4 polyFrom __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 1.0f};
                Vector4 polyTo __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 1.0f};

                if (poly->count != 0) {
                    col0[0] = (GETP(poly->points)[0].rgba[0] + M2C_FIELD(slot, u8 *, 0x24)) / 2;
                    col0[1] = (GETP(poly->points)[0].rgba[1] + M2C_FIELD(slot, u8 *, 0x25)) / 2;
                    col0[2] = (GETP(poly->points)[0].rgba[2] + M2C_FIELD(slot, u8 *, 0x26)) / 2;
                    col0[3] = (GETP(poly->points)[0].rgba[3] + M2C_FIELD(slot, u8 *, 0x27)) / 2;
                    polyFrom.x = GETP(poly->points)[0].x * slot->scale.x;
                    polyFrom.y = GETP(poly->points)[0].y * slot->scale.y;
                    PSVECAdd(&slot->pos, &polyFrom, &polyFrom);
                    for (i = 1; i < poly->count; i++) {
                        f32 rot;

                        col1[0] = (M2C_FIELD(slot, u8 *, 0x24) + GETP(poly->points)[i].rgba[0]) / 2;
                        col1[1] = (M2C_FIELD(slot, u8 *, 0x25) + GETP(poly->points)[i].rgba[1]) / 2;
                        col1[2] = (M2C_FIELD(slot, u8 *, 0x26) + GETP(poly->points)[i].rgba[2]) / 2;
                        col1[3] = (M2C_FIELD(slot, u8 *, 0x27) + GETP(poly->points)[i].rgba[3]) / 2;
                        polyTo.x = slot->scale.x * GETP(poly->points)[i].x;
                        polyTo.y = slot->scale.y * GETP(poly->points)[i].y;
                        rot = slot->rot.x;
                        if (rot != 0.0f) {
                            f32 ang = 57.29578f * ArcTan(polyTo.x, polyTo.y) + rot;
                            f32 len = Sqrt(polyTo.x * polyTo.x + polyTo.y * polyTo.y);

                            SinCos(&sinA, &cosA, ang);
                            polyTo.x = len * sinA;
                            polyTo.y = len * cosA;
                        }
                        PSVECAdd(&slot->pos, &polyTo, &polyTo);
                        Gfx_Add2DFXLine(&polyFrom, &polyTo, col0, col1, poly->type);
                        polyFrom = polyTo;
                        *(u32 *)col0 = *(u32 *)col1;
                    }
                }
                break;
            }
            }
        }
        if (restore) {
            slot->pos = savedPos;
            slot->rot = savedRot;
            slot->scale = savedScale;
        }
    }
#undef g_interface
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
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
#undef g_interface
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
    #define g_interface ((Interface *)(void *)&g_interface)
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
#undef g_interface
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
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
#undef g_interface
}

void DropShip_EndInterface(s32 index) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    u8 *iface = g_interface[index];

    *(u16 *)PTR((*(u32 *)PTR((*(u32 *)(iface + 0x1C) + 8)) + 6)) = 8;
    if (*(u32 *)(iface + 4) != 1) {
        InterfaceManager_SetLoadState(iface, 1);
        InterfaceManager_MoveInterface(iface, 0.0f);
    }
#undef g_interface
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
    #define g_interface ((Interface *)(void *)&g_interface)
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
#undef g_interface
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
    #define g_interface ((Interface *)(void *)&g_interface)
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
#undef g_interface
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
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
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
#undef g_interface
}

void InterfaceManager_SetMissionObjectiveText(u8 *obj, u8 **data) {
    /* port: data is a world objective { u32 arg, info slot }; info has a
     * big-endian word type 0x0 (the u8s in it are left as bytes), value
     * slots 0xC/0x10 and string-table slots 0x14/0x18 */
    u8 *objective = (u8 *)data;
    u8 *info = SLOT_LOAD(u8 *, objective + 4);
    char **text;
    char buf[16];
    char *str;
    s32 i;
    s32 j;
    s32 len;
    u32 type;

    if (SLOT_LOAD(u8 *, (info + 0xC)) == NULL) {
        if (SLOT_LOAD(char **, (info + 0x14)) != NULL) {
            text = SLOT_LOAD(char **, (info + 0x14));
        } else {
            text = SLOT_LOAD(char **, (info + 0x18));
        }
    } else if (*(f32 *)(SLOT_LOAD(u8 *, (info + 0xC)) + 4)) {
        text = SLOT_LOAD(char **, (info + 0x18));
    } else {
        text = SLOT_LOAD(char **, (info + 0x14));
    }
    if (text == NULL) {
        return;
    }
    type = Port_ReadBE32(info);
    if (type == 0) {
        InterfaceManager_SetObjectText(obj, text);
        return;
    }
    buf[0] = 0;
    if (type == 1) {
        sprintf(buf, "%d", (u32)*(f32 *)(SLOT_LOAD(u8 *, (info + 0x10)) + 4));
    } else if (type == 2) {
        u32 ms = *(f32 *)(SLOT_LOAD(u8 *, (info + 0x10)) + 8);
        u32 secs = ms / 1000;
        u32 mins = secs / 60;
        secs %= 60;
        sprintf(buf, "%02d:%02d", mins, secs);
    } else if (type == 3) {
        u32 *var = SLOT_LOAD(u32 *, (info + 0x10));
        u32 value;
        if (var[0] < 10) {
            value = GetCodeVariable(var[0], *(u32 *)objective);
        } else {
            value = GetCodeVariable(var[0], var[1]);
        }
        sprintf(buf, "%d", value);
    }
    str = SLOT_LOAD(char *, text);
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
                str = SLOT_LOAD(char **, ((char *)obj + 0x18));
            }
            else
            {
                str = SLOT_LOAD(char **, ((char *)obj + 0x14));
            }
            strcpy(SLOT_LOAD(char *, str), (char *)(mgr + 0x290));
        }
    }
}
void InterfaceManager_SetPlayerMessage(void *message, u32 player) {
    #define g_playerMessage1 (*(void **)(void *)&g_playerMessage1)
    #define g_playerMessage2 (*(void **)(void *)&g_playerMessage2)
    #define g_playerMessage3 (*(void **)(void *)&g_playerMessage3)

    if (player == 0) {
        g_playerMessage1 = message;
    } else if (player == 1) {
        g_playerMessage2 = message;
    } else if (player == 2) {
        g_playerMessage3 = message;
    }
#undef g_playerMessage1
#undef g_playerMessage2
#undef g_playerMessage3
}

s32 ActionEndInterface(void *action) {
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    #define g_pInterfaceToEnd (*(void **)(void *)&g_pInterfaceToEnd)
    u32 i;
    s8 found;

    if (M2C_FIELD(action, u32 *, 0) == 0xFFFFFFFF) {
        for (i = 0; i < 0x12; i++) {
            if (*(u32 *)g_interface[i] != 0 /* interface count */ && g_interface[i] != NULL) {
                InterfaceManager_SetLoadState(g_interface[i], 1);
            }
        }
    } else {
        found = 0;
        for (i = 0; i < 0x12; i++) {
            if (*(u32 *)g_interface[i] != 0 /* interface count */ && SLOT_LOAD(void *, (u8 *)(g_interface[i]) + 0x20) == SLOT_LOAD(void *, (u8 *)(action) + 4)) {
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
#undef g_interface
#undef g_pInterfaceToEnd
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
    #define g_interface ((u8 (*)[0x170])(void *)&g_interface)
    extern u32 g_interfaceRenderOrder[18];
    #define g_numInterfacesToRender (*(u32 *)(void *)&g_numInterfacesToRender)
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
#undef g_interface
#undef g_numInterfacesToRender
}

void InterfaceManager_RemoveInterfaceFromRender(u32 id) {
    extern u32 g_interfaceRenderOrder[];
    #define g_numInterfacesToRender (*(u32 *)(void *)&g_numInterfacesToRender)
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
#undef g_numInterfacesToRender
}

