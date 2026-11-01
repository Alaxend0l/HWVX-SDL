#include "common.h"
#include "CarA.h"
#include "World.h"
#include "Interface.h"
#include "Gfx.h"
#include "ThingManager.h"
#include "AttachThing.h"

/* The target arrow points at the override target, else the main one. */
static inline Vector4 TargetArrow_GetTargetPos(void) {
    extern s32 g_TargetArrow[7];

    if ((Vector4 *)PTR(g_TargetArrow[1]) != NULL) {
        return *(Vector4 *)PTR(g_TargetArrow[1]);
    }
    return *(Vector4 *)PTR(g_TargetArrow[0]);
}


/* A block of g_MasterInterfaceObjectList handed to an interface
 * (g_objectAllocations, g_numInterfaceObjectAllocations of them). */
typedef struct IfaceObjectAlloc {
    u32 start;                  /* first object */
    u32 count;
    s8 free;
} IfaceObjectAlloc;

extern u8 g_objectAllocations[];  /* typed per function below */
extern u8 g_numInterfaceObjectsAllocated[];  /* typed per function below */
extern u8 g_numInterfaceObjectAllocations[];  /* typed per function below */

void *InterfaceManager_GetInterfaceObjects(u32 num) {
    #define g_objectAllocations ((IfaceObjectAlloc *)(void *)&g_objectAllocations)
    #define g_numInterfaceObjectAllocations (*(u32 *)(void *)&g_numInterfaceObjectAllocations)
    extern InterfaceObject g_MasterInterfaceObjectList[500];
    #define g_numInterfaceObjectsAllocated (*(u32 *)(void *)&g_numInterfaceObjectsAllocated)
    InterfaceObject *obj;
    IfaceObjectAlloc *alloc;
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
                memset(&g_objectAllocations[j], 0, sizeof(IfaceObjectAlloc));
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
                g_MasterInterfaceObjectList[idx].allocIndex = idx;
                g_objectAllocations[i + 1].count = g_objectAllocations[i].count - num;
                g_objectAllocations[i + 1].free = 1;
                g_objectAllocations[i].count = num;
                return &g_MasterInterfaceObjectList[idx];
            }
            g_objectAllocations[i].free = 0;
            idx = g_objectAllocations[i].start;
            g_MasterInterfaceObjectList[idx].allocIndex = idx;
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
    obj->allocIndex = g_numInterfaceObjectsAllocated;
    g_numInterfaceObjectsAllocated += num;
    n = g_numInterfaceObjectAllocations++;
    g_objectAllocations[n].start = obj->allocIndex;
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

void InterfaceManager_Init(void) {
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
        g_interfaces[i].count = 0;
        g_interfaces[i].index = i;
    }
    g_TargetArrow[0] = 0;
    g_playerMessage1 = NULL;
    g_TargetArrow[1] = 0;
    g_TargetArrow[3] = 0;
    g_TargetArrow[4] = -1;
    g_TargetArrow[5] = 0;
    g_playerMessage2 = NULL;
    g_playerMessage3 = NULL;
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
    u32 i;

    g_interfaces[index].input = input;
    g_interfaces[index].hasInput = 1;
    for (i = 0; i < g_interfaces[index].count; i++) {
        switch (GETP(GETP(g_interfaces[index].objects)[i].def)->type) {
        case 6:
        case 7:
            if (GETP(GETP(g_interfaces[index].objects)[i].def)->role != 3) {
                InterfaceManager_SetInput(GETP(g_interfaces[index].objects)[i].child, input);
            }
            break;
        }
    }
}

static inline void InterfaceManager_ReleaseInterfaceObjects(InterfaceObject *objs) {
    #define g_objectAllocations ((IfaceObjectAlloc *)(void *)&g_objectAllocations)
    #define g_numInterfaceObjectAllocations (*(u32 *)(void *)&g_numInterfaceObjectAllocations)
    #define g_numInterfaceObjectsAllocated (*(u32 *)(void *)&g_numInterfaceObjectsAllocated)
    s32 i;

    if (objs == NULL) {
        return;
    }
    for (i = g_numInterfaceObjectAllocations - 1; i >= 0; i--) {
        if (objs->allocIndex == g_objectAllocations[i].start) {
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
            memset(&g_objectAllocations[g_numInterfaceObjectAllocations - 1], 0, sizeof(IfaceObjectAlloc));
            g_numInterfaceObjectAllocations--;
        }
    } else {
        g_objectAllocations[i].free = 1;
    }
    objs->allocIndex = -1;
#undef g_numInterfaceObjectAllocations
#undef g_numInterfaceObjectsAllocated
#undef g_objectAllocations
}
extern u8 g_curMusicPackID[];  /* typed per function below */
extern u8 g_bUnloadMusicPack[];  /* typed per function below */

void InterfaceManager_ReleaseInterface(u32 index) {
    #define g_bUnloadMusicPack (*(s8 *)(void *)&g_bUnloadMusicPack)
    extern char *g_curMusicPackName;
    #define g_curMusicPackID (*(s32 *)(void *)&g_curMusicPackID)
    u32 i;
    IfaceVar *entry;
    InterfaceObject *slot;
    Interface *iface;

    if (index >= 0x12 || (iface = &g_interfaces[index])->count == 0) {
        return;
    }
    InterfaceManager_RemoveInterfaceFromRender(index);
    if (GETP(iface->callback) != NULL) {
        IfaceMsg msg;

        msg.index = iface->index;
        msg.type = 5;
        msg.id = 0;
        msg.arg = 0;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        GETP(iface->callback)(&msg);
    }
    for (i = 0; i < iface->count; i++) {
        slot = (&GETP(iface->objects)[i]);
        if (slot->thing.h != 0 && (slot->kind == 4 || slot->kind == 3)) {
            slot->thing.h = 0;
            slot->thingCreated = 0;
        } else if (slot->kind == 6) {
            if (&g_interfaces[((u32)slot->child)] != iface) {
                InterfaceManager_ReleaseInterface(((u32)slot->child));
            }
        } else if (slot->kind == 5) {
            Gfx_FontManager_ReleaseFont(slot->fontId);
        } else if (slot->kind == 7) {
            if (slot->dialogSound.h != 0) {
                SoundSequence_Stop(slot->dialogSeq);
            }
            FileStreaming_ReleaseAllDataPacks(9, 1);
        }
        if (slot->role == 0x34) {
            InterfaceManager_RestoreMissionObjString(slot);
        }
    }
    for (i = 0; i < GETP(GETP(iface->header)->vars)->count; i++) {
        entry = (&GETP(GETP(GETP(iface->header)->vars)->vars)[i]);
        if (entry->type == IFACE_VAR_CONST && entry->save != 0) {
            entry->offset = iface->vars[i];
        }
    }
    if (g_bUnloadMusicPack && g_curMusicPackName != NULL) {
        SongManager_StopBKSong();
        FileStreaming_GetPackID(g_curMusicPackName, 10, g_curMusicPackID);
        FileStreaming_ReleaseAllDataPacks(10, 1);
        g_curMusicPackName = NULL;
    }
    Port_MenuReleased(GETP(iface->header));   /* port: apply graphics settings */
    Trigger_InterfaceEnd(HND(GETP(iface->header)));
    InterfaceHasEnded(index);
    InterfaceManager_ReleaseInterfaceObjects(GETP(g_interfaces[index].objects));
    iface->count = 0;
#undef g_bUnloadMusicPack
#undef g_curMusicPackID
}

void InterfaceManager_ReleaseAllInterfaces(void) {
    extern s32 g_TargetArrow[7];
    #define g_pInterfaceToEnd (*(void **)(void *)&g_pInterfaceToEnd)
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (g_interfaces[i].count != 0) {
            InterfaceManager_ReleaseInterface(i);
        }
    }
    g_pInterfaceToEnd = NULL;
    g_TargetArrow[0] = 0;
    g_TargetArrow[1] = 0;
    g_TargetArrow[3] = 0;
    g_TargetArrow[4] = -1;
    g_TargetArrow[5] = 0;
#undef g_pInterfaceToEnd
}

void InterfaceManager_ReleaseAllUnpausedInterfaces(void) {
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (g_interfaces[i].count != 0 && g_interfaces[i].paused == 0) {
            InterfaceManager_ReleaseInterface(i);
        }
    }
}

void InterfaceManager_SetCallBackRoutine(s32 index, void *callback) {
    SETP(g_interfaces[index].callback, callback);
}

void InterfaceManager_SetInfoCallBackRoutine(s32 index, void *callback) {
    SETP(g_interfaces[index].infoCallback, callback);
}


void InterfaceManager_PauseInterface(u32 pause, s32 index) {
    u32 i;

    g_interfaces[index].paused = pause;
    for (i = 0; i < g_interfaces[index].count; i++) {
        if (GETP(g_interfaces[index].objects)[i].kind != 6) {
            continue;
        }
        InterfaceManager_PauseInterface(pause, GETP(g_interfaces[index].objects)[i].child);
    }
}


s32 InterfaceManager_GetTotalActiveInterfaces(void) {
    s32 count = 0;
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (g_interfaces[i].count != 0) {
            count++;
        }
    }
    return count;
}

void InterfaceManager_PauseAllInterface(u8 pause) {
    u32 i;

    for (i = 0; i < 0x12; i++) {
        g_interfaces[i].paused = pause;
    }
}

void InterfaceManager_SetOffset(s32 index, f32 x, f32 y) {
    Interface *iface = &g_interfaces[index];
    iface->hasOffset = 1;
    iface->offset.x = x;
    iface->offset.y = y;
}

/* port: does an event list react to a button (not just a timer, 0x1E)? */
static s32 Port_HasButtonEvents(const IfaceEventList *list) {
    u32 i;

    for (i = 0; i < list->count; i++) {
        u8 t = GETP(list->events)[i].type;
        if (t != 0 && t != 0x1E) {
            return 1;
        }
    }
    return 0;
}

/* port: is a screen up that waits for buttons, not a player's HUD (the
 * mission information window at a race's start, briefings)? Messages such
 * as "Checkpoint" have selectable rows too, but only timers on them, and
 * don't count. Port_Keyboard.c gives the keyboard to such a screen during
 * gameplay. */
s32 Port_InterfaceTakingInput(void) {
    u32 i;

    for (i = 0; i < 0x12; i++) {
        Interface *f = &g_interfaces[i];
        InterfaceObject *sel = GETP(f->selected);
        if (f->count == 0 || f->state != 2 || f->paused || f->hidden || f->isChild || f->input == -2 ||
            GETP(f->owner) != NULL) {
            continue;
        }
        if (Port_HasButtonEvents(&GETP(f->header)->events) ||
            (sel != NULL && !sel->hidden && GETP(GETP(sel->def)->record) != NULL &&
             Port_HasButtonEvents(&GETP(GETP(sel->def)->record)->events))) {
            return 1;
        }
    }
    return 0;
}

s32 InterfaceManager_Create(InterfaceHeader *header, Player *owner, void (*callback)(void *msg)) {
    Port_MenuPatch(header);   /* port: the Options screen's Graphics menu */
    if (header != NULL) {  /* port: trace interface creation */
        Port_Log("interface create: header id %u, %u objects%s", header->id, header->numObjects,
                 owner != NULL ? " (player)" : "");
    }
    #define g_pInterfaceToEnd (*(InterfaceHeader **)(void *)&g_pInterfaceToEnd)
    extern s32 g_playerIndex;
    extern s8 g_bMemoryCardHWHack;
    IfaceMsg msg;
    InterfaceHeader *hdr = (InterfaceHeader *)header;
    s32 index;
    u32 minId;
    Interface *iface;
    u32 i;
    u32 j;
    InterfaceObject *slot;
    IfaceCodeVar *cv;

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
        if (g_interfaces[i].count == 0) {
            index = i;
            iface = &g_interfaces[i];
            break;
        }
    }
    if (iface == NULL) {
        return -1;
    }
    SETP(iface->objects, InterfaceManager_GetInterfaceObjects(hdr->numObjects));
    if (GETP(iface->objects) == NULL) {
        return -1;
    }
    iface->paused = 0;
    iface->count = 0;
    SETP(iface->owner, owner);
    SETP(iface->selected, NULL);
    SETP(iface->header, hdr);
    iface->next.h = 0;
    SETP(iface->callback, callback);
    iface->stateSeq = -1;
    iface->hasGeometry = 0;
    iface->isChild = 0;
    iface->syncParent = 0;
    iface->hasInput = 0;
    iface->historyCount = 0;
    iface->goBack = 0;
    iface->hasOffset = 0;
    SETP(iface->timeoutEvent, NULL);
    iface->timeout = 0.0f;
    iface->infoCallback.h = 0;
    iface->hidden = 0;
    iface->dialog.h = 0;
    iface->player = g_playerIndex;
    iface->index = index;
    iface->inputDisabled = 0;
    iface->unk161 = 0;
    iface->inputDelay = 500.0f;
    iface->locked = 0;
    switch (hdr->mode) {
    case 1:
        iface->input = 0;
        break;
    case 2:
        iface->input = 1;
        break;
    case 3:
        iface->input = 2;
        break;
    case 4:
        iface->input = 3;
        break;
    case 0:
    default:
        iface->input = -1;
        break;
    }
    memset(iface->dir, 0, 0x40);
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
        InterfaceManager_AddObject(iface, (&GETP(hdr->objects)[i]));
    }
    InterfaceManager_SetLoadState(iface, 0);
    for (i = 0; i < GETP(iface->header)->events.count; i++) {
        if (GETP(GETP(iface->header)->events.events)[i].type == 0x1E) {
            SETP(iface->timeoutEvent, &GETP(GETP(iface->header)->events.events)[i]);
            iface->timeout = GETP(GETP(iface->header)->events.events)[i].delay;
            break;
        }
    }
    for (i = 0; i < iface->count; i++) {
        slot = &GETP(iface->objects)[i];
        if (GETP(slot->def)->active && GETP(GETP(slot->def)->record)->id != 0 && GETP(GETP(slot->def)->record)->id < minId) {
            minId = GETP(GETP(slot->def)->record)->id;
            SETP(iface->selected, slot);
        }
    }
    for (i = 0, iface->numVars = 0; i < GETP(hdr->vars)->count && i < 30; i++, iface->numVars++) {
        switch (GETP(GETP(hdr->vars)->vars)[i].type) {
        case 0:
            iface->vars[i] = GETP(GETP(hdr->vars)->vars)[i].offset;
            break;
        case 1:
            cv = GETP(GETP(GETP(hdr->vars)->vars)[i].code);
            if (GETP(cv->arg)->isVar) {
                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, iface->vars[GETP(cv->arg)->value]);
            } else {
                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->arg)->value);
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
                iface->vars[i] = owner->port;
            } else {
                iface->vars[i] = g_playerIndex;
            }
            break;
        case 3:
            if (GETP(iface->selected) != NULL) {
                iface->vars[i] = GETP(GETP(GETP(iface->selected)->def)->record)->id;
            } else {
                iface->vars[i] = 0;
            }
            break;
        case 4:
            if (GETP(g_worldData.world) != NULL) {
                iface->vars[i] = 0;
                for (j = 0; j < WORLD()->numVariables; j++) {
                    if (GETP(GETP(WORLD()->variables)[j])->id == GETP(GETP(hdr->vars)->vars)[i].offset) {
                        iface->vars[i] = GETP(GETP(WORLD()->variables)[j])->value;
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
        msg.selId = 0;
        msg.handled = 0;
        SETP(msg.values, iface->vars);
        GETP(iface->callback)(&msg);
        if (msg.selId != 0) {
            for (i = 0; i < iface->count; i++) {
                slot = &GETP(iface->objects)[i];
                if (GETP(slot->def)->active && GETP(GETP(slot->def)->record)->id == msg.selId) {
                    SETP(iface->selected, slot);
                }
            }
        }
    }
    InterfaceManager_PreprocessInterface(iface);
    if (g_bMemoryCardHWHack && iface->count == 14 && GETP(GETP(iface->objects)[9].def)->active &&
        GETP(GETP(GETP(iface->objects)[9].def)->record)->id == 6 && GETP(GETP(GETP(iface->objects)[9].def)->record)->events.count == 1) {
        g_bMemoryCardHWHack = 0;
        GETP(GETP(GETP(GETP(iface->objects)[9].def)->record)->events.events)[0].delay = 100;
    }
    return index;
#undef g_pInterfaceToEnd
}

void InterfaceManager_AddObject(void *interface, void *object) {
    extern s32 g_playerIndex;
    Interface *iface = interface;
    IfaceObjectDef *def = object;
    s32 index;
    InterfaceObject *slot;
    IfaceImageDef *sub;
    s32 offset;
    s32 player;
    void **header;
    u32 i;

    index = iface->count;
    iface->count = index + 1;
    slot = &GETP(iface->objects)[index];
    SETP(slot->def, def);
    slot->stateSound = -1;
    SETP(slot->thing, NULL);
    slot->thingCreated = 0;
    slot->role = def->role;
    slot->animDone = 1;
    slot->refresh = 1;
    slot->timer = 0;
    SETP(slot->car, NULL);
    slot->dialogChar.h = 0;
    slot->refValue = 0;
    SETP(slot->parent, NULL);
    slot->savedText[0] = 0;
    slot->rndTimer = -1;
    slot->rndDown = 1;
    slot->rndValue = 0.0f;
    slot->rndTarget = 10.0f;
    slot->listIndex = -1;
    slot->dialogSound.h = 0;
    slot->color.reset = 1;
    slot->scale.reset = 1;
    slot->uv.reset = 1;
    slot->pos.reset = 1;
    slot->rot.reset = 1;
    slot->visible.reset = 1;
    slot->textSize.reset = 1;
    slot->unk194Reset = 1;
    ExtractColorKey(&GETP(GETP(slot->def)->anim)->color, slot, 0.0f);
    ExtractScaleKey(&GETP(GETP(slot->def)->anim)->scale, &slot->scale, 0.0f);
    ExtractScaleKey(&GETP(GETP(slot->def)->anim)->pos, &slot->pos, 0.0f);
    ExtractScaleKey(&GETP(GETP(slot->def)->anim)->rot, &slot->rot, 0.0f);
    ExtractUVKey(&GETP(GETP(slot->def)->anim)->uv, &slot->uv, 0.0f);
    if (slot->role == 1) {
        slot->role = 0;
    }
    offset = def->anchor;
    if (offset != 0) {
        offset += index;
        if (offset > 0) {
            SETP(slot->anchor, &GETP(iface->objects)[offset]);
        }
    } else {
        SETP(slot->anchor, NULL);
    }
    switch (def->type) {
    case 0:
        sub = GETP(def->sub);
        slot->blendMode = sub->blendMode;
        switch (sub->type) {
        case 0:
            slot->texture = sub->value;
            slot->kind = 0;
            break;
        case 1:
            slot->geom.h = sub->value;
            slot->kind = 2;
            iface->hasGeometry = 1;
            break;
        case 2:
            break;
        case 3:
            slot->kind = 1;
            slot->texAnim.reset = 1;
            SETP(slot->data, GETP(sub->anim));
            break;
        }
        break;
    case 1:
        SETP(slot->data, GETP(GETP(slot->def)->sub));
        slot->kind = 5;
        slot->fontId = InterfaceManager_AddFont(GETP(((IfaceText *)GETP(slot->data))->font));
        InterfaceManager_SetObjectText(slot, GETP(((IfaceText *)GETP(slot->data))->strings));
        if (slot->role == 0x40) {
            slot->string.h = 0;
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
        header = GETP(def->child);
        slot->kind = 6;
        if (GETP(slot->def)->role == 3) {
            g_playerIndex = GETP(slot->def)->input;
        }
        if (slot->role == 0x20) {
            slot->child = InterfaceManager_Create(SLOT_LOAD(void *, header), GETP(iface->owner), NULL);
        } else {
            slot->child = InterfaceManager_Create(SLOT_LOAD(void *, header), GETP(iface->owner), GETP(iface->callback));
        }
        if (GETP(slot->def)->role == 3) {
            g_playerIndex = player;
        }
        g_interfaces[slot->child].isChild = 1;
        if (def->type == 7) {
            g_interfaces[slot->child].syncParent = 1;
        }
        g_interfaces[slot->child].parent = iface->index;
        for (i = 0; i < g_interfaces[slot->child].count; i++) {
            SETP(GETP(g_interfaces[slot->child].objects)[i].parent, slot);
        }
        if (GETP(slot->def)->role == 3) {
            switch (GETP(slot->def)->input) {
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
        SETP(iface->dialog, slot);
        slot->kind = 7;
        SETP(slot->data, GETP(GETP(slot->def)->sub));
        break;
    case 10:
        slot->kind = 0xA;
        SETP(slot->data, GETP(GETP(slot->def)->sub));
        slot->fontId = InterfaceManager_AddFont(GETP(((IfaceBox *)GETP(slot->data))->font));
        break;
    case 11:
        SETP(slot->data, GETP(GETP(slot->def)->sub));
        slot->kind = 9;
        break;
    case 12:
        SETP(slot->data, GETP(GETP(slot->def)->sub));
        slot->kind = 8;
        slot->fontId = InterfaceManager_AddFont(GETP(((IfaceText *)GETP(slot->data))->font));
        break;
    }
}
/* A dialogue speech pack loaded for an interface (g_interfaceDialogSoundPacks,
 * up to 15). */
typedef struct IfaceSoundPack {
    s32 id;                     /* FileStreaming pack index */
    PTR32(DataPack) data;
} IfaceSoundPack;

extern u8 g_interfaceDialogSoundPacks[];  /* typed per function below */

void InterfaceManager_PreprocessInterface(Interface *iface) {
    extern char *g_curMusicPackName;
    #define g_curMusicPackID (*(u32 *)(void *)&g_curMusicPackID)
    extern u32 g_numInterfaceDialogSoundPacks;
    extern u32 g_curInterfaceDialogSoundPack;
    extern u32 g_curInterfaceDialogSoundIndex;
    #define g_interfaceDialogSoundPacks ((IfaceSoundPack *)(void *)&g_interfaceDialogSoundPacks)
    u32 i;
    u32 total;
    IfaceDialogList *list;
    InterfaceObject *slot;
    s32 pack;
    DataPack *data;
    IfaceSoundPack *sp;
    World *world;
    u32 k;
    WorldObjectiveInfo *info;
    WorldObjective *objective;
    u32 id;

    if (GETP((GETP(iface->header))->music) != NULL && GETP(GETP((GETP(iface->header))->music)->name) != NULL) {
        if (g_curMusicPackName == NULL || strcmp(g_curMusicPackName, GETP(GETP((GETP(iface->header))->music)->name)) != 0
            || g_curMusicPackID != GETP((GETP(iface->header))->music)->id) {
            SongManager_StopBKSong();
            if (g_curMusicPackName != NULL) {
                FileStreaming_ReleaseDataPack(FileStreaming_GetPackID(g_curMusicPackName, 10, g_curMusicPackID), 10);
            }
            FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(GETP(GETP((GETP(iface->header))->music)->name), 10, GETP((GETP(iface->header))->music)->id, 0), 10, 1);
            SongManager_PlaySong();
            SetMusicVol(32767.0f * GETP(iface->header)->musicVolume);
            g_curMusicPackName = GETP(GETP((GETP(iface->header))->music)->name);
            g_curMusicPackID = GETP((GETP(iface->header))->music)->id;
        }
    }
    g_numInterfaceDialogSoundPacks = 0;
    g_curInterfaceDialogSoundPack = 0;
    g_curInterfaceDialogSoundIndex = 0;
    if (GETP(iface->dialog) != NULL) {
        list = GETP(GETP(iface->dialog)->data);
        total = 0;
        for (i = 0; i < list->count; i++) {
            if (GETP(GETP(list->entries)[i].chr) != NULL) {
                if (GETP(GETP(GETP(list->entries)[i].chr)->font) == NULL) {
                    continue;
                }
                GETP(GETP(list->entries)[i].chr)->fontId = InterfaceManager_AddFont(GETP(GETP(GETP(list->entries)[i].chr)->font));
            }
            if (GETP(GETP(list->entries)[i].soundName) != NULL && g_numInterfaceDialogSoundPacks < 15 && total < 1000000) {
                pack = FileStreaming_LoadDataPack(GETP(GETP(list->entries)[i].soundName), 9, GETP(list->entries)[i].soundID, 0);
                data = FileStreaming_InitializeDataPack(pack, 9, 1);
                sp = &g_interfaceDialogSoundPacks[g_numInterfaceDialogSoundPacks];
                g_numInterfaceDialogSoundPacks++;
                sp->id = pack;
                SETP(sp->data, data);
                total += data->soundDataSize;
                g_curInterfaceDialogSoundIndex = i + 1;
            }
        }
    }
    g_curInterfaceDialogSoundPack = 0;
    for (i = 0; i < *(u32 *)iface; i++) {
        slot = &(GETP(iface->objects))[i];
        switch (slot->role) {
        case 0x34:
            id = GETP(slot->def)->id;
            world = GETP(g_worldData.world);
            if (world == NULL) {
                break;
            }
            for (k = 0; k < world->numObjectives; k++) {
                objective = &GETP(world->objectives)[k];
                info = GETP(objective->info);
                if (info->id == id) {
                    if (info->mode == 0x17) {
                        InterfaceManager_SetMissionObjectiveText(slot, objective);
                    } else {
                        InterfaceManager_SetObjectText(slot, NULL);
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

void InterfaceManager_SetObjectString(InterfaceObject *obj, char *str) {
    IfaceText *data = GETP(obj->data);

    obj->refresh = 1;
    obj->pos.reset = 1;
    obj->color.reset = 1;
    obj->textSize.reset = 1;
    obj->unk194Reset = 1;
    obj->textShown = 0;
    obj->typeTimer = GETP(data->font)->typeDelay;
    obj->textLen = 0;
    obj->text[0] = 0;
    SETP(obj->strings, NULL);
    SETP(obj->string, NULL);
    obj->paragraph = 0;
    if (str != NULL) {
        if (strlen(str) > 0x80) {
            strncpy(obj->text, str, 0x80);
        } else {
            strcpy(obj->text, str);
        }
        obj->textLen = strlen(str);
        if (obj->typeTimer == 0) {
            obj->textShown = obj->textLen;
        }
        if (data->paragraph != 0) {
            obj->paragraph = 1;
        }
    }
}

void InterfaceManager_SetObjectText(InterfaceObject *obj, char **text) {
    char *str;

    if (text != NULL) {
        IfaceText *data = GETP(obj->data);
        obj->refresh = 1;
        obj->color.reset = 1;
        obj->scale.reset = 1;
        obj->pos.reset = 1;
        obj->textSize.reset = 1;
        obj->unk194Reset = 1;
        obj->textShown = 0;
        obj->typeTimer = GETP(data->font)->typeDelay;
        obj->textLen = 0;
        obj->text[0] = 0;
        SETP(obj->strings, text);  /* port: text is a table of string slots */
        SETP(obj->string, NULL);
        obj->paragraph = 0;
        str = SLOT_LOAD(char *, text);
        if (str != NULL) {
            SETP(obj->string, str);
            obj->textLen = strlen(str);
            if (obj->typeTimer == 0) {
                obj->textShown = obj->textLen;
            }
            if (data->paragraph != 0) {
                obj->paragraph = 1;
            }
        }
    }
}

void InterfaceManager_EndInterface(s32 index) {
    Interface *iface = &g_interfaces[index];

    if (iface != NULL) {
        InterfaceManager_SetLoadState(iface, 1);
    }
}

void InterfaceManager_SetLoadState(Interface *iface, s32 state) {
    u32 i;
    InterfaceObject *obj;

    if (iface->isChild != 0 && (s8)iface->syncParent != 0) {
        if (iface->state != g_interfaces[iface->parent].state) {
            state = g_interfaces[iface->parent].state;
        } else {
            return;
        }
    }
    iface->timer = 0;
    if (((u32)iface->stateSeq) < 0xFFFFFFFF) {
        SoundSequence_Stop(iface->stateSeq);
    }
    iface->stateSeq = -1;
    switch (state) {
    case 0:
        iface->state = state;
        for (i = 0; i < *(u32 *)iface; i++) {
            InterfaceManager_SetObjectState(iface, (&GETP(iface->objects)[i]), 4, 0);
            obj = (&GETP(iface->objects)[i]);
            if (obj->kind == 6) {
                if (g_interfaces[((u32)obj->child)].syncParent != 0) {
                    InterfaceManager_SetLoadState(&g_interfaces[((u32)obj->child)], state);
                }
                obj = (&GETP(iface->objects)[i]);
                g_interfaces[((u32)obj->child)].hidden = obj->hidden;
            }
        }
        iface->timer = GETP(iface->header)->loadInTime;
        if (GETP(GETP(iface->header)->loadInSound) != NULL) {
            iface->stateSeq = SoundSequence_Play(GETP(GETP(iface->header)->loadInSound), NULL);
        }
        break;
    case 1:
        iface->state = state;
        for (i = 0; i < *(u32 *)iface; i++) {
            InterfaceManager_SetObjectState(iface, (&GETP(iface->objects)[i]), 5, 0);
            obj = (&GETP(iface->objects)[i]);
            if (obj->kind == 6) {
                if (g_interfaces[((u32)obj->child)].syncParent != 0) {
                    InterfaceManager_SetLoadState(&g_interfaces[((u32)obj->child)], state);
                }
                obj = (&GETP(iface->objects)[i]);
                g_interfaces[((u32)obj->child)].hidden = obj->hidden;
            }
        }
        iface->timer = GETP(iface->header)->loadOutTime;
        if (GETP(GETP(iface->header)->loadOutSound) != NULL) {
            iface->stateSeq = SoundSequence_Play(GETP(GETP(iface->header)->loadOutSound), NULL);
        }
        break;
    case 2:
        iface->state = state;
        for (i = 0; i < *(u32 *)iface; i++) {
            InterfaceManager_SetObjectState(iface, (&GETP(iface->objects)[i]), 1, 0);
            obj = (&GETP(iface->objects)[i]);
            if (obj->kind == 6) {
                if (g_interfaces[((u32)obj->child)].syncParent != 0) {
                    InterfaceManager_SetLoadState(&g_interfaces[((u32)obj->child)], state);
                }
                obj = (&GETP(iface->objects)[i]);
                g_interfaces[((u32)obj->child)].hidden = obj->hidden;
            }
        }
        if (GETP(iface->selected) != NULL) {
            InterfaceManager_SetObjectReactive(iface, 0, GETP(GETP(GETP(iface->selected)->def)->record)->id);
        }
        if (GETP(GETP(iface->header)->activeSound) != NULL) {
            iface->stateSeq = SoundSequence_Play(GETP(GETP(iface->header)->activeSound), NULL);
        }
        break;
    }
}

void InterfaceManager_SetObjectState(Interface *iface, InterfaceObject *obj, s32 state, s32 arg3) {
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
    obj->hidden = 0;
    obj->state = state;
    GETP(obj->def)->stateIndex[0] = -1;
    GETP(obj->def)->stateIndex[1] = -1;
    GETP(obj->def)->stateIndex[2] = -1;
    GETP(obj->def)->stateIndex[3] = -1;
    GETP(obj->def)->stateIndex[4] = -1;
    GETP(obj->def)->stateIndex[5] = -1;
    for (i = 0; i < GETP(obj->def)->numStates; i++) {
        IfaceObjectDef *info = GETP(obj->def);
        switch ((GETP(info->states))[i].type) {
        case 0:
            if (numOff <= NULL) {
                info->stateIndex[0] = i;
            }
            numOff++;
            break;
        case 1:
            if (numOn <= NULL) {
                info->stateIndex[1] = i;
            }
            numOn++;
            break;
        case 2:
            if (numActive <= NULL) {
                info->stateIndex[2] = i;
            }
            numActive++;
            break;
        case 3:
            if (numSelected <= NULL) {
                info->stateIndex[3] = i;
            }
            numSelected++;
            break;
        case 4:
            if (numOn <= NULL) {
                info->stateIndex[4] = i;
            }
            break;
        case 5:
            if (numDisabled <= NULL) {
                info->stateIndex[5] = i;
            }
            numDisabled++;
            break;
        }
    }
    if (((u32)obj->stateSound) != 0xFFFFFFFF) {
        SoundSequence_Stop(obj->stateSound);
        obj->stateSound = -1;
    }
    if (obj->thing.h != 0) {
        Thing_Release(GETP(obj->thing));
        obj->thing.h = 0;
        obj->thingCreated = 0;
    }
    switch (state) {
    case 100:
        obj->hidden = 1;
        obj->refresh = 0;
        obj->timer = 0;
        obj->animDone = 1;
        SETP(obj->stateAnim, NULL);
        return;
    case 4:
        if (((u32)GETP(obj->def)->stateIndex[4]) == 0xFFFFFFFF) {
            obj->hidden = 1;
            obj->refresh = 0;
            SETP(obj->stateAnim, NULL);
            return;
        }
        SETP(obj->stateAnim, GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[4])].anim));
        obj->timer = 0;
        obj->animDone = 1;
        if (GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[4])].anim)->sound) != NULL) {
            obj->stateSound = SoundSequence_Play(GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[4])].anim)->sound), NULL);
        }
        InterfaceManager_ExecuteActions(iface, &GETP(obj->stateAnim)->actions);
        break;
    case 5:
        if (((u32)GETP(obj->def)->stateIndex[5]) == 0xFFFFFFFF) {
            obj->hidden = 1;
            obj->refresh = 0;
            SETP(obj->stateAnim, NULL);
            return;
        }
        SETP(obj->stateAnim, GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[5])].anim));
        obj->timer = 0;
        obj->animDone = 1;
        if (GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[5])].anim)->sound) != NULL) {
            obj->stateSound = SoundSequence_Play(GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[5])].anim)->sound), NULL);
        }
        InterfaceManager_ExecuteActions(iface, &GETP(obj->stateAnim)->actions);
        break;
    case 0:
        if (((u32)GETP(obj->def)->stateIndex[0]) == 0xFFFFFFFF) {
            obj->hidden = 1;
            obj->refresh = 0;
            SETP(obj->stateAnim, NULL);
            return;
        }
        SETP(obj->stateAnim, GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[0])].anim));
        obj->timer = 0;
        obj->animDone = 1;
        if (GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[0])].anim)->sound) != NULL) {
            obj->stateSound = SoundSequence_Play(GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[0])].anim)->sound), NULL);
        }
        InterfaceManager_ExecuteActions(iface, &GETP(obj->stateAnim)->actions);
        break;
    case 1:
        if (((u32)GETP(obj->def)->stateIndex[1]) == 0xFFFFFFFF) {
            InterfaceManager_SetObjectState(iface, obj, 0, 0);
            obj->state = 1;
            return;
        }
        SETP(obj->stateAnim, GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[1])].anim));
        obj->timer = 0;
        obj->animDone = 1;
        if (GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[1])].anim)->sound) != NULL) {
            obj->stateSound = SoundSequence_Play(GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[1])].anim)->sound), NULL);
        }
        InterfaceManager_ExecuteActions(iface, &GETP(obj->stateAnim)->actions);
        break;
    case 2:
        if (((u32)GETP(obj->def)->stateIndex[2]) == 0xFFFFFFFF) {
            InterfaceManager_SetObjectState(iface, obj, 1, 0);
            if (GETP(GETP(obj->def)->record) != NULL) {
                obj->animDone = 0;
                for (j = 0; j < GETP(GETP(obj->def)->record)->events.count; j++) {
                    obj->timer = GETP(GETP(GETP(obj->def)->record)->events.events)[j].delay;
                    if (obj->timer != 0) {
                        break;
                    }
                }
            } else {
                obj->timer = 0;
                obj->animDone = 1;
            }
            obj->refresh = 0;
            return;
        }
        SETP(obj->stateAnim, GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[2])].anim));
        if (GETP(GETP(obj->def)->record) != NULL) {
            obj->animDone = 0;
            for (j = 0; j < GETP(GETP(obj->def)->record)->events.count; j++) {
                obj->timer = GETP(GETP(GETP(obj->def)->record)->events.events)[j].delay;
                if (obj->timer != 0) {
                    break;
                }
            }
        } else {
            obj->timer = 0;
            obj->animDone = 1;
        }
        if (GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[2])].anim)->sound) != NULL) {
            obj->stateSound = SoundSequence_Play(GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[2])].anim)->sound), NULL);
        }
        InterfaceManager_ExecuteActions(iface, &GETP(obj->stateAnim)->actions);
        break;
    case 3:
        if (((u32)GETP(obj->def)->stateIndex[3]) == 0xFFFFFFFF) {
            InterfaceManager_SetObjectState(iface, obj, 2, 0);
            obj->refresh = 0;
            return;
        }
        SETP(obj->stateAnim, GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[3])].anim));
        iface->state = 3;
        iface->timer = (GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[3])].value;
        if (GETP(GETP(obj->def)->record) != NULL) {
            obj->animDone = 0;
            obj->timer = GETP(GETP(GETP(obj->def)->record)->events.events)->delay;
        } else {
            obj->timer = 0;
            obj->animDone = 1;
        }
        if (iface->timer == 0) {
            obj->hidden = 1;
        }
        if (GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[3])].anim)->sound) != NULL) {
            obj->stateSound = SoundSequence_Play(GETP(GETP((GETP(GETP(obj->def)->states))[((u32)GETP(obj->def)->stateIndex[3])].anim)->sound), NULL);
        }
        InterfaceManager_ExecuteActions(iface, &GETP(obj->stateAnim)->actions);
        break;
    }
    obj->color.reset = 1;
    obj->scale.reset = 1;
    obj->uv.reset = 1;
    obj->pos.reset = 1;
    obj->rot.reset = 1;
    obj->visible.reset = 1;
    obj->textSize.reset = 1;
    obj->unk194Reset = 1;
    obj->refresh = 1;
    InterfaceManager_CheckConditions(iface, obj);
    InterfaceManager_MoveObject(iface, obj, 0, 0.0f);
}

void InterfaceManager_LinkThingToObject(s32 index, u32 id, void *thing) {
    Interface *iface = &g_interfaces[index];
    u32 i;

    if (iface != NULL) {
        for (i = 0; i < *(u32 *)iface; i++) {
            if (id == GETP((GETP(iface->objects))[i].def)->linkId) {
                SETP((GETP(iface->objects))[i].car, thing);
            }
        }
    }
}
void InterfaceManager_MoveInterface(Interface *iface, f32 dt);
s32 InterfaceManager_ProcessInput(Interface *iface, InterfaceObject *obj, u32 *events);

void InterfaceManager_Heartbeat(void) {
    extern s32 g_TargetArrow[7];
    extern f32 g_fDeltaTimeMS;
    extern s32 g_iDeltaTimeMS;
    f32 dt = g_fDeltaTimeMS;
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (g_interfaces[i].count != 0 && g_interfaces[i].paused == 0 && g_interfaces[i].hidden == 0) {
            void (*callback)(IfaceMsg *) = GETP(g_interfaces[i].callback);

            if (callback != NULL) {
                IfaceMsg msg;

                msg.index = g_interfaces[i].index;
                msg.type = 0xC;
                msg.id = GETP(g_interfaces[i].header)->id;
                callback(&msg);
            }
            InterfaceManager_MoveInterface(&g_interfaces[i], dt);
        }
    }
    if (g_TargetArrow[5] > 0) {
        g_TargetArrow[5] -= g_iDeltaTimeMS;
    }
    if (PTR(g_TargetArrow[3]) != NULL) {
        Thing *target = PTR(g_TargetArrow[3]);
        Player *player = GETP(target->player);

        /* the main target: the position of the target's player's car, else its own */
        if (player != NULL && GETP(player->vehicle) != NULL) {
            g_TargetArrow[0] = (s32)HND(&((Thing *)GETP(player->vehicle))->pos);
        } else {
            g_TargetArrow[0] = (s32)HND(&target->pos);
        }
    }
}
s32 InterfaceManager_PlayEvent(Interface *iface, InterfaceObject *obj, u8 *event, u16 arg);
u32 InterfaceManager_KeyPress(Interface *im, s32 pad, u8 key);

void InterfaceManager_MoveInterface(Interface *iface, f32 dt) {
    extern char *g_curMusicPackName;
    #define g_curMusicPackID (*(u32 *)(void *)&g_curMusicPackID)
    #define g_bUnloadMusicPack (*(u8 *)(void *)&g_bUnloadMusicPack)
    IfaceMsg msg;
    IfaceMsg dirMsg;
    u8 info[0xD0];
    u32 ms;
    u32 i;
    u32 j;
    u32 m;
    InterfaceObject *newSel;
    InterfaceObject *sel;
    InterfaceObject *slot;
    IfaceVar *v;
    IfaceCodeVar *cv;
    IfaceStep *step;
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
        iface->inputDisabled = 0;
        iface->unk161 = 0;
    }
    if (iface->timeout) {
        if (dt < iface->timeout) {
            iface->timeout -= dt;
        } else {
            iface->timeout = 0.0f;
            InterfaceManager_PlayEvent(iface, NULL, GETP(iface->timeoutEvent), 0xFFFF);
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
                msg.selId = 0;
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
            if ((u32)iface->stateSeq < 0xFFFFFFFF) {
                SoundSequence_Stop(iface->stateSeq);
            }
            if (GETP(iface->next) != NULL) {
                u8 count = iface->historyCount;
                u8 goBack = iface->goBack;
                u8 selId = 0;
                u8 flags = iface->flags;
                s32 input = iface->input;
                Interface *newI;
                InterfaceHeader *hdr = GETP(iface->header);
                u8 histId;

                if (GETP(iface->selected) != NULL) {
                    selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
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
                newI = &g_interfaces[InterfaceManager_Create(GETP(iface->next), GETP(iface->owner), GETP(iface->callback))];
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
                        slot = &GETP(newI->objects)[j];
                        if (GETP(slot->def)->active && GETP(GETP(slot->def)->record)->id == histId) {
                            SETP(newI->selected, slot);
                            for (m = 0; m < GETP(GETP(newI->header)->vars)->count && m < 30; m++) {
                                switch (GETP(GETP(GETP(newI->header)->vars)->vars)[m].type) {
                                case 3:
                                    if (GETP(newI->selected) != NULL) {
                                        newI->vars[m] = GETP(GETP(GETP(newI->selected)->def)->record)->id;
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
        if (GETP(iface->objects)[i].hidden == 0) {
            if (GETP(GETP(GETP(iface->objects)[i].def)->record) != NULL && GETP(iface->objects)[i].timer > dt) {
                GETP(iface->objects)[i].timer -= ms;
            } else if (GETP(GETP(GETP(iface->objects)[i].def)->record) != NULL && GETP(iface->objects)[i].animDone == 0) {
                GETP(iface->objects)[i].timer = 0;
                GETP(iface->objects)[i].animDone = 1;
                for (j = 0; j < GETP(GETP(GETP(iface->objects)[i].def)->record)->events.count; j++) {
                    if (GETP(iface->objects)[i].hidden == 0 && GETP(GETP(GETP(GETP(iface->objects)[i].def)->record)->events.events)[j].delay != 0) {
                        InterfaceManager_PlayEvent(iface, &GETP(iface->objects)[i], GETP(GETP(GETP(GETP(iface->objects)[i].def)->record)->events.events), 0xFFFF);
                        GETP(iface->objects)[i].timer = GETP(GETP(GETP(GETP(iface->objects)[i].def)->record)->events.events)[j].delay;
                        GETP(iface->objects)[i].animDone = 0;
                        break;
                    }
                }
            }
        }
        InterfaceManager_MoveObject(iface, &GETP(iface->objects)[i], info, dt);
    }
    if (iface->state == 2 || iface->state == 0) {
        if (iface->inputDelay > 0.0f) {
            if (iface->inputDelay > dt) {
                iface->inputDelay -= ms;
                for (i = 0; i < iface->count; i++) {
                    InterfaceManager_CheckConditions(iface, &GETP(iface->objects)[i]);
                }
                return;
            }
            iface->inputDelay = 0.0f;
        }
    }
    if (iface->state == 2 && 0.0f == iface->inputDelay) {
        sel = GETP(iface->selected);
        oldId = 0;
        dir = 0;
        changed = 0;
        if (sel != NULL && sel->hidden) {
            u32 bestId = 0xFFFFFFFF;
            InterfaceObject *best = NULL;

            id = GETP(GETP(sel->def)->record)->id;
            for (i = 0; i < iface->count; i++) {
                InterfaceObject *s = &GETP(iface->objects)[i];
                if (s->hidden == 0 && GETP(s->def)->active && GETP(GETP(s->def)->record)->id != 0 &&
                    GETP(GETP(s->def)->record)->id < bestId) {
                    bestId = GETP(GETP(s->def)->record)->id;
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
                    cv = GETP(v->code);
                    if (GETP(cv->arg)->isVar) {
                        iface->vars[i] = GameSettings_GetCodeVariable(cv->id, iface->vars[GETP(cv->arg)->value]);
                    } else {
                        iface->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->arg)->value);
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
                        iface->vars[i] = GETP(GETP(GETP(iface->selected)->def)->record)->id;
                    } else {
                        iface->vars[i] = 0;
                    }
                    break;
                case 4:
                    if (GETP(g_worldData.world) != NULL) {
                        for (k = 0; k < WORLD()->numVariables; k++) {
                            if (GETP(GETP(WORLD()->variables)[k])->id == GETP(GETP(GETP(iface->header)->vars)->vars)[i].offset) {
                                iface->vars[i] = GETP(GETP(WORLD()->variables)[k])->value;
                            }
                        }
                    }
                    break;
                }
            }
        }
        up = InterfaceManager_KeyPress((Interface *)iface, iface->input, 4);
        down = InterfaceManager_KeyPress((Interface *)iface, iface->input, 5);
        left = InterfaceManager_KeyPress((Interface *)iface, iface->input, 2);
        right = InterfaceManager_KeyPress((Interface *)iface, iface->input, 3);
        iface->locked = 1;
        for (j = 0; GETP(iface->selected) != NULL && j < GETP(GETP(sel->def)->record)->numSteps; j++) {
            step = &GETP(GETP(GETP(sel->def)->record)->steps)[j];
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
                        cv = GETP(v->code);
                        if (cv->id == 0x1F6) {
                            if (GETP(cv->arg)->isVar) {
                                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, iface->vars[GETP(cv->arg)->value]);
                            } else {
                                iface->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->arg)->value);
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
            wasHidden = GETP(iface->objects)[i].hidden;
            InterfaceManager_CheckConditions(iface, &GETP(iface->objects)[i]);
            if (GETP(iface->objects)[i].hidden == 0 && wasHidden) {
                InterfaceManager_MoveObject(iface, &GETP(iface->objects)[i], info, dt);
            }
        }
        if (InterfaceManager_ProcessInput(iface, NULL, &GETP(iface->header)->events) == 0) {
            return;
        }
        if (iface->state != 2 || GETP(iface->selected) == NULL) {
            return;
        }
        if (InterfaceManager_ProcessInput(iface, GETP(iface->selected), &GETP(GETP(GETP(iface->selected)->def)->record)->events.count) == 0) {
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
        if ((target = GETP(GETP(GETP(iface->selected)->def)->record)->next[0]) != 0 && up) {
            dir = 0;
        } else if ((target = GETP(GETP(GETP(iface->selected)->def)->record)->next[1]) != 0 && down) {
            dir = 1;
        } else if ((target = GETP(GETP(GETP(iface->selected)->def)->record)->next[2]) != 0 && left) {
            dir = 2;
        } else if ((target = GETP(GETP(GETP(iface->selected)->def)->record)->next[3]) != 0 && right) {
            dir = 3;
        } else {
            target = 0;
        }
        if (target == 0) {
            return;
        }
        for (origTarget = target, k = 0; k < iface->count; k++) {
            slot = &GETP(iface->objects)[k];
            if (GETP(slot->def)->active && GETP(GETP(slot->def)->record)->id == target) {
                if (slot->hidden) {
                    switch (dir) {
                    case 0:
                        target = GETP(GETP(slot->def)->record)->next[0];
                        for (j = 0; j < GETP(GETP(sel->def)->record)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->def)->record)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->def)->record)->steps)[j].val[0];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    case 1:
                        target = GETP(GETP(slot->def)->record)->next[1];
                        for (j = 0; j < GETP(GETP(sel->def)->record)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->def)->record)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->def)->record)->steps)[j].val[1];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    case 2:
                        target = GETP(GETP(slot->def)->record)->next[2];
                        for (j = 0; j < GETP(GETP(sel->def)->record)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->def)->record)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->def)->record)->steps)[j].val[2];
                            if (iface->vars[var] > GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) {
                                iface->vars[var] = __abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].max) - 1 + GETP(GETP(GETP(iface->header)->vars)->vars)[var].min;
                            } else if (iface->vars[var] < GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) {
                                iface->vars[var] = GETP(GETP(GETP(iface->header)->vars)->vars)[var].max - (__abs(iface->vars[var] - GETP(GETP(GETP(iface->header)->vars)->vars)[var].min) - 1);
                            }
                        }
                        break;
                    case 3:
                        target = GETP(GETP(slot->def)->record)->next[3];
                        for (j = 0; j < GETP(GETP(sel->def)->record)->numSteps; j++) {
                            var = GETP(GETP(GETP(sel->def)->record)->steps)[j].var;
                            iface->vars[var] += GETP(GETP(GETP(sel->def)->record)->steps)[j].val[3];
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
                    oldId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
                    break;
                }
            }
        }
        if (oldId == 0) {
            return;
        }
        if (GETP(GETP(iface->header)->moveSound) != NULL) {
            SoundSequence_Play(GETP(GETP(iface->header)->moveSound), NULL);
        }
        if (GETP(iface->callback) != NULL) {
            dirMsg.index = iface->index;
            dirMsg.type = 0;
            dirMsg.id = dir + 10001;
            dirMsg.arg = 0;
            dirMsg.value = 0;
            dirMsg.selId = target;
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
}

void InterfaceManager_SetObjectReactive(Interface *iface, u32 onId, u32 offId) {
    u32 i;
    u32 j;
    InterfaceObject *obj;
    IfaceRecord *record;

    for (i = 0; i < iface->count; i++) {
        obj = (&GETP(iface->objects)[i]);
        if (GETP(obj->def)->active != 0 && obj->stateAnim.h != 0) {
            if (GETP(GETP(obj->def)->record)->id == onId) {
                InterfaceManager_SetObjectState(iface, obj, 1, 0);
            }
            if (GETP(GETP(obj->def)->record)->id == offId) {
                InterfaceManager_SetObjectState(iface, obj, 2, 0);
                record = GETP(GETP(obj->def)->record);
                for (j = 0; j < record->events.count; j++) {
                    /* sic: tests the first event each time */
                    if (GETP(record->events.events)[0].delay != 0) {
                        obj->animDone = 0;
                        obj->timer = GETP(GETP(GETP(obj->def)->record)->events.events)->delay;
                        break;
                    }
                }
            }
        }
    }
}

void InterfaceManager_SetObjectReference(Interface *iface, u32 id, u32 state) {
    u32 i;

    for (i = 0; i < iface->count; i++) {
        IfaceRecord *info = GETP(GETP(GETP(iface->objects)[i].def)->record);
        InterfaceObject *obj = (&GETP(iface->objects)[i]);
        if (info != NULL && info->id == id) {
            if (obj->state != state) {
                InterfaceManager_SetObjectState(iface, obj, state, 0);
                if (obj->kind == 5) {
                    char **strings = GETP(obj->strings);
                    if (strings != NULL) {
                        IfaceText *data = GETP(obj->data);
                        char *text;
                        obj->refresh = 1;
                        obj->color.reset = 1;
                        obj->scale.reset = 1;
                        obj->pos.reset = 1;
                        obj->textSize.reset = 1;
                        obj->unk194Reset = 1;
                        obj->textShown = 0;
                        obj->typeTimer = GETP(data->font)->typeDelay;
                        obj->textLen = 0;
                        obj->text[0] = 0;
                        SETP(obj->strings, strings);
                        SETP(obj->string, NULL);
                        obj->paragraph = 0;
                        text = SLOT_LOAD(char *, strings);
                        if (text != NULL) {
                            SETP(obj->string, text);
                            obj->textLen = strlen(text);
                            if (obj->typeTimer == 0) {
                                obj->textShown = obj->textLen;
                            }
                            if (data->paragraph != 0) {
                                obj->paragraph = 1;
                            }
                        }
                    }
                }
            }
        } else {
            obj->state = 0;
            obj->hidden = 1;
        }
    }
}

u32 InterfaceManager_KeyPress(Interface *im, s32 pad, u8 key) {
    extern s32 g_iDeltaTimeMS;
    u32 result = 0;

    if (pad == -2 || im->inputDisabled) {
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

s32 InterfaceManager_ProcessInput(Interface *iface, InterfaceObject *obj, u32 *events) {
    IfaceMsg msg;
    u32 i;
    IfaceEvent *event;
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
        event = &((IfaceEvent *)PTR(events[1]))[i];
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
            if (GETP(iface->header)->id == 8) {
                ok = 1;
                voters = 0;
                votes = 0;
                for (j = 0; j < iface->count && ok; j++) {
                    if (GETP(iface->objects)[j].kind == 6) {
                        if (GETP(g_interfaces[GETP(iface->objects)[j].child].header)->id == 2 ||
                            GETP(g_interfaces[GETP(iface->objects)[j].child].header)->id == 3) {
                            player = g_interfaces[GETP(iface->objects)[j].child].player;
                            vote = GameSettings_GetCodeVariable(0xC9, player);
                            voters++;
                            if (vote == 2) {
                                ok = 0;
                            } else if (vote == 3) {
                                if (GETP(g_interfaces[GETP(iface->objects)[j].child].callback) != NULL) {
                                    msg.index = g_interfaces[GETP(iface->objects)[j].child].index;
                                    msg.type = 0xF;
                                    msg.id = GETP(event->def)->id;
                                    msg.arg = pressed;
                                    msg.value = 0;
                                    msg.selId = 0;
                                    msg.handled = 0;
                                    msg.targetId = 0xFFFF;
                                    if (GETP(GETP(event->def)->target) != NULL) {
                                        msg.targetId = *GETP(GETP(event->def)->target);
                                    }
                                    if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
                                        msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
                                    }
                                    SETP(msg.values, (u32 *)g_interfaces[GETP(iface->objects)[j].child].vars);
                                    GETP(g_interfaces[GETP(iface->objects)[j].child].callback)(&msg);
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
}

s32 InterfaceManager_PlayEvent(Interface *iface, InterfaceObject *obj, u8 *event, u16 arg) {
    typedef struct EndLevelAction {
        u32 type;
        u32 id;
    } EndLevelAction;
    extern s8 g_bMemoryCardHWHack;
    void ForceIPLReset(void);
    u8 stateSet;
    u32 i;
    u32 a;
    u32 b;
    s32 v;
    IfaceEventMod *mod;
    IfaceVar *vdefs;
    u8 slot;

    stateSet = 0;
    if (obj != NULL && GETP(GETP(obj->def)->record)->noPress == 0 && GETP(obj->def)->pressable != 0) {
        InterfaceManager_SetObjectState(iface, obj, 3, 0);
        stateSet = 1;
    }
    if (GETP(GETP(((IfaceEvent *)event)->def)->sound) != NULL) {
        SoundSequence_Play(GETP(GETP(((IfaceEvent *)event)->def)->sound), 0);
    }
    iface->goBack = 0;
    switch (GETP(((IfaceEvent *)event)->def)->type) {
    case 1:
    case 0x20:
        if (iface->isChild != 0) {
            if (GETP(((IfaceEvent *)event)->def)->type == 0x20) {
                iface = &g_interfaces[iface->parent];
            } else {
                return 0;  /* bare return in the decomp */
            }
        }
        {
            IfaceMsg msg;

            msg.index = iface->index;
            msg.type = 0xF;
            msg.id = GETP(((IfaceEvent *)event)->def)->id;
            msg.arg = arg;
            msg.value = 0;
            msg.selId = 0;
            msg.handled = 0;
            msg.result = 0;
            if (GETP(GETP(((IfaceEvent *)event)->def)->target) != NULL) {
                msg.targetId = *GETP(GETP(((IfaceEvent *)event)->def)->target);
            }
            if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
                msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
            }
            SETP(msg.values, (u32 *)iface->vars);
            if (GETP(iface->callback) != NULL) {
                GETP(iface->callback)(&msg);
            }
            if (msg.result == 0xB) {
                break;
            }
        }
        if (GETP(GETP(((IfaceEvent *)event)->def)->sound) != NULL) {
            SoundSequence_Play(GETP(GETP(((IfaceEvent *)event)->def)->sound), 0);
        }
        iface->flags = GETP(((IfaceEvent *)event)->def)->flags;
        /* port: memory card screens skipped, autosave (Port_Menu.c) */
        SETP(iface->next, Port_MenuEventTarget((InterfaceHeader *)GETP(GETP(((IfaceEvent *)event)->def)->target)));
        if (!stateSet && GETP(iface->next) != NULL) {
            InterfaceManager_SetLoadState(iface, 1);
            if (iface->flags & 1) {
                iface->timer = 0;
                return 0;
            }
        }
        if (GETP(iface->next) != NULL) {
            return 0;
        }
        break;
    case 3:
        if (GETP(iface->header)->id == 2) {
            if (GameSettings_GetCodeVariable(0xC9, iface->player) != 3) {
                return 0;
            }
        } else if (GETP(iface->header)->id == 3) {
            if (GameSettings_GetCodeVariable(0xC9, iface->player) != 3) {
                return 0;
            }
        }
        if (iface->isChild != 0) {
            return 0;
        }
        {
            IfaceMsg msg;

            msg.index = iface->index;
            msg.type = 0x10;
            msg.id = GETP(((IfaceEvent *)event)->def)->id;
            msg.arg = arg;
            msg.value = 0;
            msg.selId = 0;
            msg.handled = 0;
            msg.targetId = 0xFFFF;
            msg.result = 0;
            if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
                msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
            }
            SETP(msg.values, (u32 *)iface->vars);
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
            iface->timer = 0;
            return 0;
        }
    case 4: {
        IfaceMsg msg;

        msg.index = iface->index;
        msg.type = 0;
        msg.id = GETP(((IfaceEvent *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        msg.result = 0;
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.value != 0xFFFFFFFF && GETP(GETP(((IfaceEvent *)event)->def)->sound) != NULL && msg.handled == 0) {
            SoundSequence_Play(GETP(GETP(((IfaceEvent *)event)->def)->sound), 0);
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
            iface->flags = GETP(((IfaceEvent *)event)->def)->flags;
            SETP(iface->next, (u16 *)PTR(msg.value));
            if (!stateSet && GETP(iface->next) != NULL) {
                InterfaceManager_SetLoadState(iface, 1);
                if (iface->flags & 1) {
                    iface->timer = 0;
                    return 0;
                }
            }
            return 0;
        }
    }
    case 2: {
        IfaceMsg msg;
        u32 cur = iface->historyCount;

        msg.index = iface->index;
        msg.type = 0xF;
        msg.id = GETP(((IfaceEvent *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        msg.result = 0;
        if (cur != 0) {
            msg.targetId = GETP(iface->history[cur - 1])->id;
        } else {
            msg.targetId = 0xFFFF;
        }
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        SETP(msg.values, (u32 *)iface->vars);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.result == 0xB) {
            return 1;
        }
        InterfaceManager_Backup_Key(iface, event, GETP(((IfaceEvent *)event)->def)->flags);
        return 0;
    }
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 0x1E: {
        IfaceMsg msg;

        msg.index = iface->index;
        if (GETP(((IfaceEvent *)event)->def)->type == 7) {
            EndLevelAction action;

            action.type = 4;
            action.id = GETP(((IfaceEvent *)event)->def)->id;
            ActionEndLevel(&action);
            return 1;
        }
        if (GETP(((IfaceEvent *)event)->def)->type == 8) {
            EndLevelAction action;

            action.type = 5;
            action.id = GETP(((IfaceEvent *)event)->def)->id;
            ActionEndLevel(&action);
            return 1;
        }
        if (GETP(((IfaceEvent *)event)->def)->type == 6 || GETP(((IfaceEvent *)event)->def)->type == 0x1E) {
            u32 progA;
            u32 progB;

            if (GETP(((IfaceEvent *)event)->def)->type == 6) {
                u32 *args = (u32 *)GETP(GETP(((IfaceEvent *)event)->def)->args);  /* port: table of pointer slots */

                if (((IfaceVarValue *)PTR(args[0]))->isVar) {
                    progA = iface->vars[((IfaceVarValue *)PTR(args[0]))->value];
                } else {
                    progA = ((IfaceVarValue *)PTR(args[0]))->value;
                }
                if (((IfaceVarValue *)PTR(args[1]))->isVar) {
                    progB = iface->vars[((IfaceVarValue *)PTR(args[1]))->value];
                } else {
                    progB = ((IfaceVarValue *)PTR(args[1]))->value;
                }
            } else {
                GameSettings_GetLoadedProgression(&progA, &progB);
            }
            StateProgressionManager_SetProgressionID(progA, progB);
            msg.type = 6;
            StateManager_QueueChange(0xB);
            msg.result = 4;
        } else if (GETP(((IfaceEvent *)event)->def)->type == 9) {
            msg.type = 9;
            msg.id = GETP(((IfaceEvent *)event)->def)->id;
            InterfaceManager_ReleaseInterface(iface->index);
            StateManager_QueueChange(2);
            return 0;  /* bare return in the decomp */
        } else if (GETP(((IfaceEvent *)event)->def)->type == 10) {
            msg.type = 10;
            msg.id = GETP(((IfaceEvent *)event)->def)->id;
            StateProgressionManager_RestartCurrentLevel();
        }
        msg.arg = arg;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        msg.result = 0;
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.value != 0xFFFFFFFF && GETP(GETP(((IfaceEvent *)event)->def)->sound) != NULL && msg.handled == 0) {
            SoundSequence_Play(GETP(GETP(((IfaceEvent *)event)->def)->sound), 0);
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
        iface->flags = GETP(((IfaceEvent *)event)->def)->flags;
        SETP(iface->next, (u16 *)PTR(msg.value));
        if (!stateSet && GETP(iface->next) != NULL) {
            InterfaceManager_SetLoadState(iface, 1);
            if (iface->flags & 1) {
                iface->timer = 0;
                return 0;
            }
        }
        return 0;
    }
    case 0xD: {
        IfaceMsg msg;

        msg.index = iface->index;
        msg.type = 0xD;
        msg.id = GETP(((IfaceEvent *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        msg.data[0] = GETP(GETP(((IfaceEvent *)event)->def)->bytes)[0];
        msg.data[1] = GETP(GETP(((IfaceEvent *)event)->def)->bytes)[1];
        msg.data[2] = GETP(GETP(((IfaceEvent *)event)->def)->bytes)[2];
        SETP(msg.values, (u32 *)iface->vars);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        break;
    }
    case 0xE: {
        IfaceMsg msg;

        msg.index = iface->index;
        msg.type = 0xE;
        msg.id = GETP(((IfaceEvent *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        msg.data[0] = GETP(GETP(((IfaceEvent *)event)->def)->bytes)[0];
        SETP(msg.values, (u32 *)iface->vars);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        break;
    }
    case 0xB:
        InterfaceManager_ExecuteActions(iface, (IfaceActionList *)GETP(GETP(((IfaceEvent *)event)->def)->target));
        break;
    case 0xF:
        if (SLOT_LOAD(IfaceVarValue *, (u32 *)(GETP(GETP(((IfaceEvent *)event)->def)->args)) + (0))->isVar) {
            a = iface->vars[SLOT_LOAD(IfaceVarValue *, (u32 *)(GETP(GETP(((IfaceEvent *)event)->def)->args)) + (0))->value];
        } else {
            a = SLOT_LOAD(IfaceVarValue *, (u32 *)(GETP(GETP(((IfaceEvent *)event)->def)->args)) + (0))->value;
        }
        if (SLOT_LOAD(IfaceVarValue *, (u32 *)(GETP(GETP(((IfaceEvent *)event)->def)->args)) + (1))->isVar) {
            b = iface->vars[SLOT_LOAD(IfaceVarValue *, (u32 *)(GETP(GETP(((IfaceEvent *)event)->def)->args)) + (1))->value];
        } else {
            b = SLOT_LOAD(IfaceVarValue *, (u32 *)(GETP(GETP(((IfaceEvent *)event)->def)->args)) + (1))->value;
        }
        StateProgressionManager_SetProgressionID(a, b);
        break;
    case 0x10: {
        IfaceMsg msg;

        msg.index = iface->index;
        msg.type = 0x11;
        msg.id = GETP(((IfaceEvent *)event)->def)->id;
        msg.arg = arg;
        msg.value = (u32)HND(GETP(iface->header));
        msg.selId = 0;
        msg.handled = 0;
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.result == 4) {
            InterfaceManager_ReleaseInterface(iface->index);
            StateMovie_SetMovieToPlay(GETP(((IfaceEvent *)event)->def)->movie);
            StateManager_QueueChange(3);
        }
        break;
    }
    case 0x11: {
        IfaceMsg msg;

        if (((IfaceVarValue *)GETP(GETP(((IfaceEvent *)event)->def)->args))->isVar) {
            a = iface->vars[((IfaceVarValue *)GETP(GETP(((IfaceEvent *)event)->def)->args))->value];
        } else {
            a = ((IfaceVarValue *)GETP(GETP(((IfaceEvent *)event)->def)->args))->value;
        }
        msg.index = iface->index;
        msg.type = 0xF;
        msg.id = GETP(((IfaceEvent *)event)->def)->id;
        msg.arg = arg;
        msg.value = 0;
        msg.selId = 0;
        msg.handled = 0;
        msg.targetId = 0xFFFF;
        msg.result = 0;
        if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
            msg.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
        }
        SETP(msg.values, (u32 *)iface->vars);
        if (GETP(iface->callback) != NULL) {
            GETP(iface->callback)(&msg);
        }
        if (msg.result == 0xB) {
            break;
        }
        switch (GETP(iface->header)->id) {
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
        GameSettings_SetCodeVariable(0x1F7, 0, GETP(((IfaceEvent *)event)->def)->id);
        break;
    case 0x13:
        GameSettings_SaveGame();
        break;
    case 0x14:
        GameSettings_LoadGame();
        break;
    case 0x15:
        GameSettings_SetSaveGameName(&iface->vars[GETP(((IfaceEvent *)event)->def)->id]);
        g_bMemoryCardHWHack = 1;
        break;
    case 0x16:
        v = GameSettings_GetCodeVariable(0xCF, 0);
        v += GETP(((IfaceEvent *)event)->def)->delta;
        if (v > 100) {
            v = 100;
        } else if (v < 0) {
            v = 0;
        }
        GameSettings_SetCodeVariable(0xCF, 0, v);
        break;
    case 0x17:
        v = GameSettings_GetCodeVariable(0xD0, 0);
        v += GETP(((IfaceEvent *)event)->def)->delta;
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
        iface->inputDisabled = 1;
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
        InterfaceManager_Create(GETP(GETP(((IfaceEvent *)event)->def)->target), 0, 0);
        break;
    case 0x21:
        GameSettings_DeleteGameFile();
        break;
    }

    for (i = 0; i < GETP(((IfaceEvent *)event)->def)->numMods; i++) {
        mod = GETP(GETP(((IfaceEvent *)event)->def)->mods);
        slot = mod->var;
        if (slot < iface->numVars) {
            iface->vars[slot] += mod->delta;
            vdefs = GETP(GETP(GETP(iface->header)->vars)->vars);
            if (iface->vars[slot] > vdefs[slot].max) {
                iface->vars[slot] = __abs(iface->vars[slot] - vdefs[slot].max) + vdefs[slot].min - 1;
            } else if (iface->vars[slot] < vdefs[slot].min) {
                iface->vars[slot] = vdefs[slot].max - (__abs(iface->vars[slot] - vdefs[slot].min) - 1);
            }
            InterfaceManager_CheckConditions(iface, obj);
        }
    }
    return 1;
}
void InterfaceManager_Backup_Key(Interface *iface, IfaceEvent *key, u8 flags) {
    s32 count;

    count = iface->historyCount;
    if (iface->isChild != 0) {
        iface = &g_interfaces[((s32)iface->parent)];
        count = iface->historyCount;
    }
    if (key != NULL) {
        u16 n = GETP(key->def)->id;      /* levels to go back */
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
    iface->flags = 0;
    if (key != NULL && GETP(GETP(key->def)->sound) != NULL) {
        SoundSequence_Play(GETP(GETP(key->def)->sound), NULL);
    }
    count--;
    iface->goBack = 1;
    iface->next.h = iface->history[count].h;
    iface->historyCount = count;
    iface->flags = flags;
    InterfaceManager_SetLoadState(iface, 1);
    if (iface->flags & 1) {
        iface->timer = 0;
    }
}

void InterfaceManager_MoveDialogSystem(Interface *iface, InterfaceObject *obj, f32 dt) {
    typedef struct DialogQuad {
        u32 v[4];
    } DialogQuad;
    extern u32 g_numInterfaceDialogSoundPacks;
    extern u32 g_curInterfaceDialogSoundPack;
    extern u32 g_curInterfaceDialogSoundIndex;
    #define g_interfaceDialogSoundPacks ((IfaceSoundPack *)(void *)&g_interfaceDialogSoundPacks)
    u32 i;
    IfaceDialogList *list;
    InterfaceObject *slot;
    IfaceObjectDef *object;
    IfaceDialogChar *chr;
    s8 released;
    s32 pack;
    u8 *data;
    u8 state;

    if (obj->state == 4 || obj->state == 5) {
        return;
    }
    if (obj->kind == 7 && obj->hidden == 0) {
        list = GETP(obj->data);
        if (obj->visible.reset != 0 && list->count != 0) {
            obj->dialogEntry = 0;
            obj->dialogNext = 1;
        }
        if (obj->dialogNext != 0) {
            obj->dialogNext = 0;
            obj->dialogTime = GETP(list->entries)[obj->dialogEntry].time;
            SETP(obj->dialogChar, GETP(GETP(list->entries)[obj->dialogEntry].chr));
            for (i = 0; i < *(u32 *)iface; i++) {
                slot = (&GETP(iface->objects)[i]);
                if (GETP(slot->stateAnim) == NULL) {
                    continue;
                }
                object = GETP(slot->def);
                switch (object->role) {
                case 0x21:
                    if (slot->kind == 5) {
                        if (GETP(GETP(list->entries)[GETP(iface->dialog)->dialogEntry].text) != NULL) {
                            chr = GETP(obj->dialogChar);
                            slot->fontId = chr->fontId;
                            SETP(((IfaceText *)GETP(slot->data))->font, GETP(GETP(obj->dialogChar)->font));
                            GETP(slot->stateAnim)->color.count = 0;
                            GETP(GETP(slot->def)->anim)->color.count = 1;
                            *(u32 *)GETP(GETP(GETP(slot->def)->anim)->color.keys) = *GETP(GETP(obj->dialogChar)->color);
                            if (GETP((GETP(obj->dialogChar))->quad) != NULL) {
                                GETP(slot->stateAnim)->scale.count = 0;
                                GETP(GETP(slot->def)->anim)->scale.count = 1;
                                *(DialogQuad *)GETP(GETP(GETP(slot->def)->anim)->scale.keys) = *(DialogQuad *)GETP(GETP(obj->dialogChar)->quad);
                            }
                            SLOT_STORE(GETP(slot->data), GETP(GETP(list->entries)[GETP(iface->dialog)->dialogEntry].text));
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, GETP(slot->data)));
                        } else {
                            SLOT_STORE(GETP(slot->data), NULL);
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, GETP(slot->data)));
                        }
                    }
                    break;
                case 0x22:
                    if (slot->kind == 0) {
                        if (GETP(list->entries)[obj->dialogEntry].active && object->id == GETP(list->entries)[obj->dialogEntry].id) {
                            SETP(slot->dialogChar, GETP(obj->dialogChar));
                        }
                    }
                    break;
                case 0x23:
                    if (slot->kind == 5) {
                        if (GETP(list->entries)[obj->dialogEntry].active && object->id == GETP(list->entries)[obj->dialogEntry].id) {
                            *(s32 *)GETP(object->sub) = GETP(obj->dialogChar)->valueA;
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, GETP(obj->data)));
                        }
                    }
                    break;
                case 0x24:
                    if (slot->kind == 5) {
                        if (GETP(list->entries)[obj->dialogEntry].active && object->id == GETP(list->entries)[obj->dialogEntry].id) {
                            *(s32 *)GETP(object->sub) = GETP(obj->dialogChar)->valueB;
                            InterfaceManager_SetObjectText(slot, SLOT_LOAD(char **, GETP(obj->data)));
                        }
                    }
                    break;
                }
            }
            obj->dialogSpoken = 0;
            if (GETP(list->entries)[obj->dialogEntry].special) {
                (GETP(obj->dialogChar))->state = 5;
                (GETP(obj->dialogChar))->key = GETP(list->entries)[obj->dialogEntry].specialKey;
            } else {
                (GETP(obj->dialogChar))->state = 1;
            }
            InterfaceManager_ExecuteActions(iface, &GETP(list->entries)[obj->dialogEntry].actions);
        } else {
            obj->dialogTime -= (s32)dt;
            if (obj->dialogSpoken == 0 && (GETP(obj->dialogChar))->state == 2) {
                obj->dialogSpoken = 1;
                if (GETP(GETP(list->entries)[obj->dialogEntry].soundName) != NULL) {
                    released = 0;
                    if (GETP(obj->dialogSound) != NULL) {
                        SoundSequence_Stop(obj->dialogSeq);
                        FileStreaming_ReleaseDataPack(g_interfaceDialogSoundPacks[g_curInterfaceDialogSoundPack - 1].id, 9);
                        released = 1;
                        SETP(obj->dialogSound, NULL);
                    }
                    if (g_curInterfaceDialogSoundPack == 15) {
                        g_curInterfaceDialogSoundPack = 0;
                    }
                    SETP(obj->dialogSound, DataPack_GetScript(GETP(g_interfaceDialogSoundPacks[g_curInterfaceDialogSoundPack].data), GETP(list->entries)[obj->dialogEntry].soundID));
                    if (GETP(obj->dialogSound) != NULL) {
                        obj->dialogSeq = SoundSequence_Play(GETP(obj->dialogSound), NULL);
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
            if (obj->dialogTime <= 0) {
                chr = GETP(obj->dialogChar);
                if (chr->state == 5 || chr->state == 1 || chr->state == 4) {
                    if (GETP(GETP(list->entries)[obj->dialogEntry].chr) != NULL) {
                        GETP(GETP(list->entries)[obj->dialogEntry].chr)->state = 0;
                    }
                    (obj->dialogEntry)++;
                    if (obj->dialogEntry < list->count) {
                        obj->dialogNext = 1;
                    } else {
                        InterfaceManager_SetLoadState(iface, 1);
                    }
                } else {
                    chr->state = 3;
                }
            }
        }
    }
    if (obj->kind == 0) {
        chr = GETP(obj->dialogChar);
        if (chr != NULL && obj->hidden == 0) {
            obj->charTimer -= (s32)dt;
            if (obj->visible.reset != 0 || chr->state != obj->charState || obj->charTimer <= 0) {
                if (chr->state == 1) {
                    chr->state = 2;
                } else if (obj->charState == 5) {
                    chr->state = 2;
                } else if (obj->charState == 3 || chr->state == 3) {
                    chr->state = 4;
                }
                obj->charState = chr->state;
                if (obj->charState != 3 && obj->charState != 4) {
                    if (obj->charState == 5) {
                        obj->charTimer = GETP(chr->special)[chr->key].anim.frameTime * GETP(chr->special)[chr->key].anim.numKeys;
                    } else if (obj->charState == 2) {
                        chr->key = RandomNumberGenerator_GetRandomInteger(0, chr->numTalk);
                        obj->charTimer = GETP(chr->talk)[chr->key].anim.frameTime * GETP(chr->talk)[chr->key].anim.numKeys;
                    } else if (obj->charState == 0) {
                        chr->key = RandomNumberGenerator_GetRandomInteger(0, chr->numIdle);
                        obj->charTimer = GETP(chr->idle)[chr->key].anim.frameTime * GETP(chr->idle)[chr->key].anim.numKeys;
                    }
                    obj->charAnim.reset = 1;
                }
            }
            if (chr->state == 5) {
                ExtractTextureKey(&GETP(chr->special)[chr->key].anim, (TextureKeyState *)(&obj->charAnim), dt);
            } else if (chr->state == 2 || chr->state == 3 || chr->state == 4) {
                ExtractTextureKey(&GETP(chr->talk)[chr->key].anim, (TextureKeyState *)(&obj->charAnim), dt);
            } else {
                ExtractTextureKey(&GETP(chr->idle)[chr->key].anim, (TextureKeyState *)(&obj->charAnim), dt);
            }
            obj->texture = ((TextureKeyState *)(&obj->charAnim))->texture;
        }
    }
#undef g_interfaceDialogSoundPacks
}

void InterfaceManager_MoveObject(Interface *iface, InterfaceObject *obj, InterfaceHudInfo *data, f32 dt) {
    u8 *query;  /* role 2: [0] != 0: use the callback's value (the decomp reused `data`) */
    typedef struct { char str[0x200]; } MoveObjStrBuf;
    extern char rndNumberBuffer[16];
    extern char *g_rndNumber[1];
    extern s32 g_iDeltaTimeMS;
    #define g_playerMessage1 (*(char ***)(void *)&g_playerMessage1)
    #define g_playerMessage2 (*(char ***)(void *)&g_playerMessage2)
    #define g_playerMessage3 (*(char ***)(void *)&g_playerMessage3)
    static u32 s_rndNumberSlot[1];
    char tmp[0x80];
    f32 dtCopy;
    f32 value;
    IfaceTypeSound *snd;
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
    if (obj->visible.value != 0) {
        switch (obj->kind) {
        case 5:
            if ((s8)((u8)obj->refresh) != 0) {
                obj->typeTimer -= (u32)dt;
                ExtractScaleKey(&GETP(((IfaceText *)GETP(obj->data))->font)->size, &obj->textSize, dt);
                /* type the text a character at a time, with the font's click */
                while (obj->textShown < obj->textLen && obj->typeTimer < 0) {
                    snd = GETP(GETP(((IfaceText *)GETP(obj->data))->font)->typeSound);
                    if (snd != NULL) {
                        pitch = RandomAdd(snd->pitchMin, snd->pitchMax);
                        sample = SoundAudioSample_Play(snd->sample);
                        if (sample != NULL) {
                            SoundAudioSample_Set(sample, 0, 32767.0f * snd->volume);
                            SoundAudioSample_Set(sample, 1, 65536.0f * pitch);
                        }
                    }
                    obj->typeTimer += GETP(((IfaceText *)GETP(obj->data))->font)->typeDelay;
                    (obj->textShown)++;
                }
            }
            break;
        case 8: {
            MoveObjStrBuf buf = { "" };

            for (i = 0; i < GETP(GETP(iface->header)->vars)->count; i++) {
                sprintf(tmp, "v%d = %d|", i, iface->vars[i]);
                strcat(buf.str, tmp);
            }
            InterfaceManager_SetObjectString(obj, buf.str);
            break;
        }
        }
    }
    if (GETP(iface->dialog) != NULL) {
        InterfaceManager_MoveDialogSystem(iface, obj, dt);
    }
    type = obj->role;
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
                if (GETP(iface->callback) != NULL) {
                    IfaceMsg q = {0, 1};

                    query = GETP(GETP(obj->def)->query);
                    q.index = iface->index;
                    q.id = GETP(obj->def)->id;
                    if (GETP(iface->selected) != NULL && GETP(GETP(iface->selected)->def) != NULL) {
                        q.selId = GETP(GETP(GETP(iface->selected)->def)->record)->id;
                    }
                    GETP(iface->callback)(&q);
                    if (query[0] != 0) {
                        value = q.fvalue;
                    }
                    if (q.result == 0 && obj->state != 0) {
                        InterfaceManager_SetObjectState(iface, obj, 0, 0);
                    }
                    if (q.result == 1 && obj->state == 0 && GETP(obj->def)->stateIndex[1] != 0xFFFFFFFF) {
                        InterfaceManager_SetObjectState(iface, obj, 1, 0);
                    }
                    if (q.result == 2) {
                        InterfaceManager_SetObjectState(iface, obj, 1, 0);
                    }
                    if (q.result == 10) {
                        InterfaceManager_SetObjectState(iface, obj, 100, 0);
                    }
                    if (obj->kind == 5) {
                        IfaceText *text = GETP(obj->data);

                        if (text->listMode == 1) {
                            if (q.id < 0xFFFF) {
                                IfaceStringList *list = GETP(text->list);

                                if (q.id >= list->count) {
                                    q.id = (u16)list->count - 1;
                                }
                                if (GETP(obj->strings) != (char **)PTR(GETP(list->texts)[q.id])) {
                                    InterfaceManager_SetObjectText(obj, (char **)PTR(GETP(list->texts)[q.id]));
                                }
                            }
                        } else if ((s8)q.text[0] != 0) {
                            obj->string.h = 0;
                            strcpy((char *)(&obj->text), q.text);
                            obj->textLen = strlen(q.text);
                        } else if (obj->string.h == 0) {
                            InterfaceManager_SetObjectText(obj, GETP(obj->strings));
                        }
                        doText = 0;
                    } else if (obj->kind == 6 && obj->state == 1) {
                        InterfaceManager_SetObjectReference(&g_interfaces[((u32)obj->child)], q.id, q.result != 0);
                    }
                }
                break;
            case 4:
                Gfx_SetProjectionMatrix(GETP(obj->def)->id);
                break;
            case 5:
                value = data->unk24;
                break;
            case 6:
                value = data->unk28;
                break;
            case 8:
                if (GETP(data->unk7C) != NULL) {
                    value = 15.915494f * (3.1415927f + ArcTan(GETP(data->unk7C)->mtxRows[2].z, GETP(data->unk7C)->mtxRows[2].x));
                }
                break;
            case 10:
                value = data->health;
                break;
            case 11:
                value = data->turbo;
                break;
            case 12:
                value = data->speed;
                break;
            case 13:
                value = data->altitude;
                break;
            case 14:
                value = data->unk1C;
                break;
            case 15:
                value = data->unk30;
                break;
            case 16:
                if ((s8)((u8)data->text[0]) != 0) {
                    if (obj->kind == 5) {
                        InterfaceManager_SetObjectString(obj, (char *)(&data->text));
                    }
                    obj->pos.reset = 1;
                    obj->color.reset = 1;
                    obj->scale.reset = 1;
                }
                break;
            case 17:
                if (GETP(data->messages[2]) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->messages[2]));
                }
                break;
            case 18:
                if (GETP(data->messages[3]) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->messages[3]));
                }
                break;
            case 19:
                if (GETP(data->message19) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->message19));
                }
                break;
            case 20:
                if (data->unk82 != 0) {
                    value = 0.0f;
                    obj->scale.reset = 1;
                    obj->color.reset = 1;
                }
                break;
            case 21:
                if (data->unk81 != 0) {
                    value = 0.0f;
                    obj->scale.reset = 1;
                    obj->color.reset = 1;
                }
                break;
            case 70:
                value = data->carStats[0];
                break;
            case 71:
                value = data->carStats[1];
                break;
            case 72:
                value = data->carStats[2];
                break;
            case 73:
                value = data->carStats[3];
                break;
            case 91:
                if (obj->rndTimer < 0) {
                    obj->rndTimer = 500;
                    obj->pos.reset = 1;
                    obj->scale.reset = 1;
                    obj->color.reset = 1;
                    obj->rndValue = RandomAdd(0.0f, GETP(obj->def)->id);
                    sprintf(rndNumberBuffer, "%1.0f", obj->rndValue);
                    s_rndNumberSlot[0] = HND(rndNumberBuffer);  /* port: a table of string slots */
                    SETP(data->rndText, s_rndNumberSlot);
                    InterfaceManager_SetObjectText(obj, (char **)s_rndNumberSlot);
                } else {
                    obj->rndTimer -= g_iDeltaTimeMS;
                }
                break;
            case 92:
                if (obj->rndTimer < 0) {
                    obj->rndValue += obj->rndStep;
                    obj->rndTimer = 50;
                    if ((obj->rndDown != 0 && obj->rndValue < obj->rndTarget) ||
                        (obj->rndDown == 0 && obj->rndValue > obj->rndTarget)) {
                        obj->pos.reset = 1;
                        obj->scale.reset = 1;
                        obj->color.reset = 1;
                        obj->rndTarget = RandomAdd(0.0f, GETP(obj->def)->id);
                        obj->rndStep = 0.1f * (obj->rndTarget - obj->rndValue);
                        if (obj->rndValue < obj->rndTarget) {
                            obj->rndDown = 0;
                        } else {
                            obj->rndDown = 1;
                        }
                    }
                } else {
                    obj->rndTimer -= g_iDeltaTimeMS;
                    sprintf(rndNumberBuffer, "%1.0f", obj->rndValue);
                    s_rndNumberSlot[0] = HND(rndNumberBuffer);  /* port: a table of string slots */
                    SETP(data->rndText, s_rndNumberSlot);
                    InterfaceManager_SetObjectText(obj, (char **)s_rndNumberSlot);
                }
                break;
            case 40: {
                u32 *val = GETP(data->refs[GETP(obj->def)->id]);

                if (val != NULL) {
                    if (*val != obj->refValue) {
                        obj->pos.reset = 1;
                        obj->scale.reset = 1;
                        obj->color.reset = 1;
                    }
                    obj->refValue = *val;
                    ref = val;
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
                if (GETP(data->values) != NULL) {
                    setDirty = 0;
                    value = (GETP(data->values))[(u8)(type - 41)];
                    if ((s32)data->valuesChanged == 1) {
                        obj->color.reset = 1;
                        obj->scale.reset = 1;
                        obj->pos.reset = 1;
                    }
                }
                break;
            case 49:
                if (GETP(data->message49) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->message49));
                }
                break;
            case 60:
                if (GETP(data->messages[0]) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->messages[0]));
                }
                break;
            case 61:
                if (GETP(data->messages[1]) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->messages[1]));
                }
                break;
            case 62:
                if (data->state == 1) {
                    obj->hidden = 0;
                } else if (data->state == 0) {
                    obj->hidden = 1;
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
                obj->texture = 0xFFFF;
                if (data->weapon[0] != 0) {
                    obj->texture = data->weaponIcon[0];
                }
                break;
            case 81:
                if (GETP(data->weaponName) != NULL) {
                    InterfaceManager_SetObjectText(obj, GETP(data->weaponName));
                }
                break;
            case 82:
                value = data->ammo;
                break;
            case 83:
                obj->texture = 0xFFFF;
                if (data->weapon[1] != 0) {
                    obj->texture = data->weaponIcon[1];
                }
                break;
            case 84:
                value = data->ammoPips;
                break;
            case 86:
                obj->texture = 0xFFFF;
                if (data->weapon[2] != 0) {
                    obj->texture = data->weaponIcon[2];
                }
                break;
            case 50: {
                World *world;
                WorldTimer *timer;
                u32 j;
                u32 id;

                id = GETP(obj->def)->id;
                world = GETP(g_worldData.world);
                if (world != NULL) {
                    valid = 0;
                    for (j = 0; j < world->numTimers; j++) {
                        timer = GETP(GETP(world->timers)[j]);
                        if (timer->active != 0 && id == timer->type) {
                            if (GETP(timer->mode)->mode == 0x17) {
                                valid = 1;
                            }
                            obj->pos.reset = 1;
                            obj->texAnim.reset = 1;
                            obj->scale.reset = 1;
                            obj->color.reset = 1;
                            value = timer->time;
                            break;
                        }
                    }
                }
                isTime = 1;
                break;
            }
            case 51: {
                World *world;
                WorldVariable *var;
                u32 j;
                u32 id;

                id = GETP(obj->def)->id;
                world = GETP(g_worldData.world);
                if (world != NULL) {
                    valid = 0;
                    for (j = 0; j < world->numVariables; j++) {
                        var = GETP(GETP(world->variables)[j]);
                        if (var->active != 0 && id == var->id) {
                            if (*GETP(var->mode) == 0x17) {
                                valid = 1;
                            }
                            obj->pos.reset = 1;
                            obj->texAnim.reset = 1;
                            obj->scale.reset = 1;
                            obj->color.reset = 1;
                            value = var->value;
                            break;
                        }
                    }
                }
                isCount = 1;
                break;
            }
            case 100:
                if (iface->state == 2) {
                    /* port: the default name is the first string slot of the
                     * object's text table, and SetObjectText takes a table of
                     * string slots (it copies the string pointer at once, so
                     * one static slot does, as the original's stack variable) */
                    static u32 s_nameSlot;
                    char *name = GameSettings_GetActiveMemoryCardFileName(GETP(iface->header)->id == 5,
                                                                         GETP(obj->def)->id,
                                                                         SLOT_LOAD(char *, GETP(obj->data)));

                    if (name != NULL) {
                        s_nameSlot = HND(name);
                        InterfaceManager_SetObjectText(obj, (char **)&s_nameSlot);
                    }
                    if (name != NULL && GETP(obj->stateAnim) != NULL) {
                        obj->hidden = 0;
                    } else {
                        obj->hidden = 1;
                    }
                    if (obj->hidden == 0) {
                        InterfaceManager_CheckConditions(iface, obj);
                    }
                }
                break;
            case 101:
                if (GETP(iface->selected)->role != 100) {
                    obj->hidden = 1;
                } else if (GETP(obj->stateAnim) != NULL) {
                    obj->hidden = 0;
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
                obj->rot.reset = 1;
                obj->pos.reset = 1;
                obj->uv.reset = 1;
                obj->scale.reset = 1;
                obj->color.reset = 1;
            }
            dt = value;
            if (doText && obj->kind == 5) {
                if (isCount) {
                    if (valid) {
                        sprintf((char *)(&obj->text), "%d", (u32)value);
                        obj->string.h = 0;
                        obj->textLen = strlen((char *)(&obj->text));
                    } else {
                        sprintf((char *)(&obj->text), " ");
                        obj->string.h = 0;
                        obj->textLen = strlen((char *)(&obj->text));
                    }
                } else if (isTime) {
                    if (valid) {
                        u32 secs = (u32)value / 1000;
                        u32 mins = secs / 60;

                        sprintf((char *)(&obj->text), "%.2d:%.2d", mins, secs - mins * 60);
                        obj->string.h = 0;
                        obj->textLen = strlen((char *)(&obj->text));
                    } else {
                        sprintf((char *)(&obj->text), " ");
                        obj->string.h = 0;
                        obj->textLen = strlen((char *)(&obj->text));
                    }
                } else {
                    obj->string.h = 0;
                    sprintf((char *)(&obj->text), "%d", (u32)value);
                    obj->textLen = strlen((char *)(&obj->text));
                }
            }
        }
    }
    if (obj->hidden == 0) {
        if (obj->kind == 1) {
            IfaceTexAnim *anim = GETP(obj->data);

            if (anim->fromVar != 0) {
                u32 idx = iface->vars[anim->var];

                if (idx >= anim->anim.numKeys) {
                    idx = anim->anim.numKeys - 1;
                }
                obj->texture = GETP(anim->anim.keys)[(s32)idx]; /* count 0: [-1], as on GC */
                if (obj->listIndex != idx) {
                    obj->listIndex = idx;
                    obj->color.reset = 1;
                    obj->scale.reset = 1;
                    obj->uv.reset = 1;
                    obj->pos.reset = 1;
                    obj->rot.reset = 1;
                    obj->visible.reset = 1;
                    obj->textSize.reset = 1;
                    obj->unk194Reset = 1;
                    obj->refresh = 1;
                }
            } else {
                ExtractTextureKey(anim, &obj->texAnim, dt);
                obj->texture = obj->texAnim.texture;
            }
        }
        if (obj->kind == 5 && ((IfaceText *)GETP(obj->data))->listMode == 1) {
            IfaceStringList *list = GETP(((IfaceText *)GETP(obj->data))->list);

            if (list->fromVar != 0) {
                u32 idx = iface->vars[list->var];

                if (idx >= list->count) {
                    idx = list->count - 1;
                }
                if (idx != obj->listIndex) {
                    obj->listIndex = idx;
                    InterfaceManager_SetObjectText(obj, (char **)PTR(GETP(list->texts)[(s32)idx])); /* count 0: [-1], as on GC */
                }
            }
        }
        if (GETP(obj->stateAnim)->color.count != 0 /* track key count */) {
            ExtractColorKey(&GETP(obj->stateAnim)->color, obj, dt);
        } else {
            ExtractColorKey(&GETP(GETP(obj->def)->anim)->color, obj, dt);
        }
        if (GETP(obj->stateAnim)->scale.count != 0 /* track key count */) {
            ExtractScaleKey(&GETP(obj->stateAnim)->scale, &obj->scale, dt);
        } else {
            ExtractScaleKey(&GETP(GETP(obj->def)->anim)->scale, &obj->scale, dt);
        }
        if (GETP(obj->stateAnim)->pos.count != 0 /* track key count */) {
            ExtractScaleKey(&GETP(obj->stateAnim)->pos, &obj->pos, dt);
        } else {
            ExtractScaleKey(GETP(GETP(obj->def)->anim), &obj->pos, dt);
        }
        if (GETP(obj->stateAnim)->rot.count != 0 /* track key count */) {
            ExtractScaleKey(&GETP(obj->stateAnim)->rot, &obj->rot, dt);
        } else {
            ExtractScaleKey(&GETP(GETP(obj->def)->anim)->rot, &obj->rot, dt);
        }
        if (GETP(obj->stateAnim)->uv.count != 0 /* track key count */) {
            ExtractUVKey(&GETP(obj->stateAnim)->uv, &obj->uv, dt);
        } else {
            ExtractUVKey(&GETP(GETP(obj->def)->anim)->uv, &obj->uv, dt);
        }
        ExtractActiveKey(GETP(obj->stateAnim), &obj->visible, dtCopy);
        if (ref != NULL) {
            *(u32 *)(&obj->color.rgba) = *ref;
        }
    }
#undef g_playerMessage1
#undef g_playerMessage2
#undef g_playerMessage3
}

/* Runs an action list (state animations, dialogue entries). */
void InterfaceManager_ExecuteActions(Interface *iface, IfaceActionList *list) {
    IfaceAction *action;
    IfaceStateAction *set;
    IfaceCodeAction *code;
    IfaceVarValue *op;
    InterfaceObject *obj;
    s32 a;
    s32 b;
    s32 value;
    u32 i;
    s32 state;

    for (i = 0; i < list->count; i++) {
        action = &GETP(list->actions)[i];
        switch (action->type) {
        case 1:
            set = GETP(action->data);
            if (set->object >= iface->count) {
                break;
            }
            obj = &GETP(iface->objects)[set->object];
            switch (set->state) {
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
                if (obj->hidden != 0) {
                    obj->hidden = 0;
                    obj->color.reset = 1;
                    obj->scale.reset = 1;
                    obj->uv.reset = 1;
                    obj->pos.reset = 1;
                    obj->rot.reset = 1;
                    obj->visible.reset = 1;
                    obj->textSize.reset = 1;
                    obj->unk194Reset = 1;
                    obj->refresh = 1;
                }
                continue;
            case 5:
                obj->hidden = 1;
                continue;
            }
            switch (set->isConst) {
            case 10:
                value = set->value;
                break;
            default:
                value = iface->vars[set->value];
                break;
            }
            InterfaceManager_SetObjectState(iface, obj, state, value);
            break;
        case 2:
            code = GETP(action->data);
            if ((op = GETP(GETP(code->code)->arg))->isVar) {
                a = iface->vars[op->value];
            } else {
                a = op->value;
            }
            if ((op = GETP(code->value))->isVar) {
                b = iface->vars[op->value];
            } else {
                b = op->value;
            }
            GameSettings_SetCodeVariable(GETP(code->code)->id, a, b);
            break;
        case 3:
            op = GETP(action->data);
            if (op->isVar) {
                iface->vars[op->var] = iface->vars[op->value];
            } else {
                iface->vars[op->var] = op->value;
            }
            break;
        }
    }
}

void InterfaceManager_CheckConditions(Interface *iface, InterfaceObject *obj) {
    u32 i;
    IfaceConditionList *list;
    s8 result;
    s8 old;
    s8 show;
    s8 isAnd;
    s8 pass;
    s8 changed;
    Interface *ifc;
    CarA *car;
    CarAInfo *info;
    u32 j;
    u32 k;
    IfaceCodeVar *cv;
    u32 sum;

    if (obj == NULL || obj->stateAnim.h == 0 ||
        (list = GETP(GETP(obj->def)->conditions)) == NULL) {
        return;
    }
    old = obj->hidden;
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
            if (((IfaceVarValue *)GETP(GETP(list->conds)[i].data))->isVar) {
                if (((Interface *)iface)->vars[((Interface *)iface)->vars[((IfaceVarValue *)GETP(GETP(list->conds)[i].data))->value]] != GETP(list->conds)[i].value) {
                    pass = 0;
                }
            } else if (((Interface *)iface)->vars[((IfaceVarValue *)GETP(GETP(list->conds)[i].data))->value] != GETP(list->conds)[i].value) {
                pass = 0;
            }
            break;
        case 2:
            cv = GETP(GETP(list->conds)[i].data);
            if (GameSettings_GetCodeVariable(cv->id, GETP(cv->arg)->isVar ? ((Interface *)iface)->vars[GETP(cv->arg)->value] : GETP(cv->arg)->value) != GETP(list->conds)[i].value) {
                pass = 0;
            }
            break;
        case 3:
            if (GETP(iface->owner) == NULL || !(((CarA *)GETP(GETP(iface->owner)->vehicle))->transmissionFlags & 0x80)) {
                pass = 0;
            }
            break;
        case 4:
            if (GETP(iface->owner) != NULL && (car = GETP(GETP(iface->owner)->vehicle)) != NULL) {
                info = GETP(car->vehicleIface);
                sum = 0;
                for (j = 0; j < info->numStartSlaves; j++) {
                    sum += GETP(info->startSlaves)[j].count;
                }
                if (sum != GETP(list->conds)[i].value) {
                    pass = 0;
                }
            } else {
                pass = 0;
            }
            break;
        case 5:
            if (GETP(iface->owner) != NULL && GETP(GETP(iface->owner)->vehicle) != NULL) {
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
    if (obj->kind == 6) {
        ifc = &g_interfaces[((u32)obj->child)];
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
                switch (GETP(GETP(GETP(ifc->header)->vars)->vars)[i].type) {
                case 1:
                    cv = GETP(GETP(GETP(GETP(ifc->header)->vars)->vars)[i].code);
                    if (GETP(cv->arg)->isVar) {
                        ifc->vars[i] = GameSettings_GetCodeVariable(cv->id, ifc->vars[GETP(cv->arg)->value]);
                    } else {
                        ifc->vars[i] = GameSettings_GetCodeVariable(cv->id, GETP(cv->arg)->value);
                    }
                    ifc->vars[i] += GETP(GETP(GETP(ifc->header)->vars)->vars)[i].offset;
                    if (ifc->vars[i] > GETP(GETP(GETP(ifc->header)->vars)->vars)[i].max) {
                        ifc->vars[i] = ifc->vars[i] - GETP(GETP(GETP(ifc->header)->vars)->vars)[i].max - 1 + GETP(GETP(GETP(ifc->header)->vars)->vars)[i].min;
                    } else if (ifc->vars[i] < GETP(GETP(GETP(ifc->header)->vars)->vars)[i].min) {
                        ifc->vars[i] = GETP(GETP(GETP(ifc->header)->vars)->vars)[i].max - (GETP(GETP(GETP(ifc->header)->vars)->vars)[i].min - ifc->vars[i] - 1);
                    }
                    break;
                case 4:
                    if (GETP(g_worldData.world) != NULL) {
                        /* sic: clears vars[0] and stores by world variable index k */
                        ifc->vars[0] = 0;
                        for (k = 0; k < WORLD()->numVariables; k++) {
                            if (GETP(GETP(WORLD()->variables)[k])->id == GETP(GETP(GETP(ifc->header)->vars)->vars)[i].offset) {
                                ifc->vars[k] = GETP(GETP(WORLD()->variables)[k])->value;
                            }
                        }
                    }
                    break;
                }
            }
            for (i = 0; i < ifc->count; i++) {
                InterfaceManager_CheckConditions(ifc, &GETP(ifc->objects)[i]);
            }
        }
    } else {
        if (result) {
            obj->hidden = !show;
        } else {
            obj->hidden = show;
        }
        if (old != obj->hidden) {
            obj->color.reset = 1;
            obj->scale.reset = 1;
            obj->uv.reset = 1;
            obj->pos.reset = 1;
            obj->rot.reset = 1;
            obj->visible.reset = 1;
            obj->textSize.reset = 1;
            obj->unk194Reset = 1;
            obj->refresh = 1;
        }
    }
}

void InterfaceManager_SetControllerConfigInputString(void) {

}

void InterfaceManager_DrawBox(InterfaceObject *obj) {
    f32 width;
    f32 height;
    u32 colour = *(u32 *)obj->color.rgba;
    IfaceBox *box = GETP(obj->data);
    /* labels in the first size key of the box font */
    f32 textSize = *(f32 *)GETP(GETP(box->font)->size.keys);
    Vector4 origin __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 p0 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 p1 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    char buf[16];
    u32 i;
    u32 step;

    width = 0.5f * box->width;
    height = 0.5f * box->height;
    origin.x = obj->pos.v.x - width - width * GETP(obj->def)->anchorX;
    origin.y = obj->pos.v.y - height - height * GETP(obj->def)->anchorY;
    if (box->xSteps == 0) {
        box->xSteps = 1;
    }
    step = box->width / box->xSteps;
    for (i = 0; i <= box->width; i += step) {
        p0.y = origin.y;
        p0.x = origin.x + i;
        p1.x = p0.x;
        p1.y = origin.y + box->height;
        Gfx_Add2DFXLine(&p0, &p1, &colour, &colour, 1);
        sprintf(buf, "%d", (u32)p0.x);
        Gfx_FontManager_GetPrintRect(buf, obj->fontId, &width, &height, textSize);
        width *= 0.5f;
        height *= 0.5f;
        Gfx_FontManager_Print(buf, obj->fontId, 5, p0.x - width, p0.y - 2.0f * height, textSize);
    }
    if (box->ySteps == 0) {
        box->ySteps = 1;
    }
    step = box->height / box->ySteps;
    for (i = 0; i <= box->height; i += step) {
        p0.x = origin.x;
        p0.y = origin.y + i;
        p1.x = origin.x + box->width;
        p1.y = p0.y;
        Gfx_Add2DFXLine(&p0, &p1, &colour, &colour, 1);
        sprintf(buf, "%d", (u32)p0.y);
        Gfx_FontManager_GetPrintRect(buf, obj->fontId, &width, &height, textSize);
        width *= 0.5f;
        height *= 0.5f;
        Gfx_FontManager_Print(buf, obj->fontId, 5, p0.x - 2.0f * width, p0.y - height, textSize);
    }
}

void InterfaceManager_DrawLine(InterfaceObject *obj) {
    IfaceLine *line = GETP(obj->data);
    Vector4 p0 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 p1 __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 dir __attribute__((aligned(16)));
    f32 pos;
    f32 x;
    f32 y;

    switch (line->type) {
    case 0:
        p0.x = 0.0f;
        p0.y = obj->pos.v.y;
        p1.x = 640.0f;
        p1.y = obj->pos.v.y;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
        if (line->step != 0) {
            p0.x = obj->pos.v.x - 10.0f;
            p0.y = obj->pos.v.y - 10.0f;
            p1.x = obj->pos.v.x + 10.0f;
            p1.y = obj->pos.v.y + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            p0.x = obj->pos.v.x + 10.0f;
            p0.y = obj->pos.v.y - 10.0f;
            p1.x = obj->pos.v.x - 10.0f;
            p1.y = obj->pos.v.y + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            p0.x = obj->pos.v.x;
            p0.y = obj->pos.v.y - 10.0f;
            p1.x = obj->pos.v.x;
            p1.y = obj->pos.v.y + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            for (pos = obj->pos.v.x + line->step; pos < 640.0f; pos += line->step) {
                p0.x = pos;
                p0.y = obj->pos.v.y - 10.0f;
                p1.x = pos;
                p1.y = obj->pos.v.y + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            }
            for (pos = obj->pos.v.x - line->step; pos > 0.0f; pos -= line->step) {
                p0.x = pos;
                p0.y = obj->pos.v.y - 10.0f;
                p1.x = pos;
                p1.y = obj->pos.v.y + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            }
        }
        break;
    case 1:
        p0.x = obj->pos.v.x;
        p0.y = 0.0f;
        p1.x = obj->pos.v.x;
        p1.y = 480.0f;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
        if (line->step != 0) {
            p0.x = obj->pos.v.x - 10.0f;
            p0.y = obj->pos.v.y - 10.0f;
            p1.x = obj->pos.v.x + 10.0f;
            p1.y = obj->pos.v.y + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            p0.x = obj->pos.v.x + 10.0f;
            p0.y = obj->pos.v.y - 10.0f;
            p1.x = obj->pos.v.x - 10.0f;
            p1.y = obj->pos.v.y + 10.0f;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            p0.x = obj->pos.v.x - 10.0f;
            p0.y = obj->pos.v.y;
            p1.x = obj->pos.v.x + 10.0f;
            p1.y = obj->pos.v.y;
            Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            for (pos = obj->pos.v.y + line->step; pos < 480.0f; pos += line->step) {
                p0.x = obj->pos.v.x - 10.0f;
                p0.y = pos;
                p1.x = obj->pos.v.x + 10.0f;
                p1.y = pos;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            }
            for (pos = obj->pos.v.y - line->step; pos > 0.0f; pos -= line->step) {
                p0.x = obj->pos.v.x - 10.0f;
                p0.y = pos;
                p1.x = obj->pos.v.x + 10.0f;
                p1.y = pos;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
            }
        }
        break;
    case 2:
        p0.x = 0.0f;
        p0.y = 480.0f;
        p1.x = 640.0f;
        p1.y = 0.0f;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
        dir.x = p1.x - p0.x;
        dir.y = p1.y - p0.y;
        dir.z = 0.0f;
        if (PSVECSquareMag(&dir)) {
            PSVECNormalize(&dir, &dir);
        }
        if (line->step != 0) {
            x = obj->pos.v.x;
            y = obj->pos.v.y;
            while (x < 640.0f || y < 480.0f) {
                p0.x = x - 10.0f;
                p0.y = y;
                p1.x = x + 10.0f;
                p1.y = y;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
                p0.x = x;
                p0.y = y - 10.0f;
                p1.x = x;
                p1.y = y + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
                break;
            }
        }
        break;
    case 3:
        p0.x = 0.0f;
        p0.y = 0.0f;
        p1.x = 640.0f;
        p1.y = 480.0f;
        Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
        if (line->step != 0) {
            x = obj->pos.v.x;
            y = obj->pos.v.y;
            while (x < 640.0f || y < 480.0f) {
                p0.x = x - 10.0f;
                p0.y = y;
                p1.x = x + 10.0f;
                p1.y = y;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
                p0.x = x;
                p0.y = y - 10.0f;
                p1.x = x;
                p1.y = y + 10.0f;
                Gfx_Add2DFXLine(&p0, &p1, (u32 *)(&obj->color.rgba), (u32 *)(&obj->color.rgba), 1);
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
    extern u32 g_interfaceRenderOrder[0x12];
    #define g_numInterfacesToRender (*(s32 *)(void *)&g_numInterfacesToRender)
    u32 i;
    Interface *iface;

    for (i = 0; i < g_numInterfacesToRender; i++) {
        iface = &g_interfaces[g_interfaceRenderOrder[i]];
        if (*(u32 *)iface != 0 /* interface count */ && (iface->state != 1 || iface->timer != 0) && iface->isChild == 0) {
            InterfaceManager_RenderInterface((RenderInterface *)iface);
        }
    }
#undef g_numInterfacesToRender
}
void InterfaceManager_RenderObject(Interface *iface, s32 index);

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

/* port: widescreen HUD. On a screen wider than 4:3 the in-game HUD (an
 * interface owned by a player, in gameplay) moves its left and right
 * elements to the screen's edges: an object laid out left of LEFT_X moves
 * left by the width of the side area (Port_ViewWiden), one right of RIGHT_X
 * right; the middle (the target arrow, messages) stays. The side is decided
 * by the layout position (the default animation's first key, plus the
 * parent's or anchor's), not the animated one, so elements that fly in
 * across the middle move as a whole. 0 in split screen and menus. */
#define HUD_LEFT_X 192.0f
#define HUD_RIGHT_X 448.0f
static f32 Port_HudLayoutX(InterfaceObject *obj, u32 depth) {
    IfaceObjAnim *anim = GETP(GETP(obj->def)->anim);
    f32 x = obj->pos.v.x;

    if (depth > 8) {
        return x;
    }
    if (GETP(obj->anchor) != NULL) {
        return Port_HudLayoutX(GETP(obj->anchor), depth + 1);
    }
    if (anim != NULL && anim->pos.count != 0 && GETP(anim->pos.keys) != NULL) {
        x = ((f32 *)GETP(anim->pos.keys))[0];
    }
    if (GETP(obj->parent) != NULL) {
        x += Port_HudLayoutX(GETP(obj->parent), depth + 1);
    }
    return x;
}

static f32 Port_HudShift(Interface *iface, InterfaceObject *obj) {
    f32 wx, wy, side, x;

    /* a child interface's objects are drawn inside their parent object's
     * draw, offset by its (already shifted) position */
    if (GETP(iface->owner) == NULL || StateManager_CurrState() != 5 || GETP(obj->parent) != NULL) {
        return 0.0f;
    }
    Port_ViewWiden(&wx, &wy);
    if (wx <= 1.0f) {
        return 0.0f;
    }
    side = 320.0f * (wx - 1.0f);
    x = Port_HudLayoutX(obj, 0);
    return x < HUD_LEFT_X ? -side : x > HUD_RIGHT_X ? side : 0.0f;
}

void InterfaceManager_RenderObject(Interface *iface, s32 index) {
    typedef struct { Vector4 pos; Vector4 quat; Vector4 vel; PTR32(u8) p; u32 id; f32 unk38; } RenderObjParams2;
    extern s8 g_bRenderCenterPoints;
    extern s8 g_bRenderInterfaceHelpers;
    extern u8 g_carAVTable[];
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
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
    InterfaceObject *carSlot = NULL;
    InterfaceObject *slot = &GETP(iface->objects)[index];
    u32 i;
    Vector4 *ofs;

    if (slot->hidden || iface->hidden) {
        return;
    }
    if (g_bRenderCenterPoints && GETP(slot->def)->anchor == 0) {
        if (slot->kind == IFACE_KIND_TEXT && ((IfaceText *)GETP(slot->data))->paragraph) {
            f32 w = ((IfaceText *)GETP(slot->data))->width;
            f32 h = ((IfaceText *)GETP(slot->data))->height;
            Vector4 corner = {0.0f, 0.0f, 0.0f, 1.0f};

            corner.x = slot->pos.v.x - 0.5f * w - 0.5f * w * GETP(slot->def)->anchorX;
            corner.y = slot->pos.v.y - 0.5f * h - 0.5f * h * GETP(slot->def)->anchorY;
            InterfaceManager_DrawDebugRect(&corner, w, h);
        }
        InterfaceManager_DrawDebugCross(&slot->pos.v);
    }
    if (slot->visible.reset) {
        return;
    }
    if (!slot->visible.value) {
        switch (slot->kind) {
        case 4:
            slot->thingCreated = 0;
            if (GETP(slot->thing) != NULL) {
                Thing_Release(GETP(slot->thing));
                SETP(slot->thing, NULL);
            }
            break;
        case 3:
            slot->thingCreated = 0;
            SETP(slot->thing, NULL);
            break;
        }
        slot->pos.reset = 1;
        slot->rot.reset = 1;
        slot->scale.reset = 1;
        slot->color.reset = 1;
        slot->uv.reset = 1;
        if (slot->kind == 1) {
            slot->texAnim.reset = 1;
        }
        if (slot->kind == 6) {
            InterfaceManager_SetLoadState(&g_interfaces[slot->child], 0);
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
        if (GETP(slot->anchor) != NULL) {
            if (GETP(slot->stateAnim)->pos.count == 0) {
                slot->pos.v = GETP(slot->anchor)->pos.v;
            }
            if (GETP(slot->stateAnim)->rot.count == 0) {
                slot->rot.v = GETP(slot->anchor)->rot.v;
            }
        }
        if (GETP(slot->parent) != NULL) {
            restore = 1;
            savedPos = slot->pos.v;
            slot->pos.v.x += GETP(slot->parent)->pos.v.x;
            slot->pos.v.y += GETP(slot->parent)->pos.v.y;
            slot->pos.v.z += GETP(slot->parent)->pos.v.z;
            savedRot = slot->rot.v;
            slot->rot.v.x += GETP(slot->parent)->rot.v.x;
            slot->rot.v.y += GETP(slot->parent)->rot.v.y;
            slot->rot.v.z += GETP(slot->parent)->rot.v.z;
            savedScale = slot->scale.v;
            slot->scale.v.x += GETP(slot->parent)->scale.v.x;
            slot->scale.v.y += GETP(slot->parent)->scale.v.y;
            slot->scale.v.z += GETP(slot->parent)->scale.v.z;
        }
        {   /* port: widescreen, for this draw only (restored below). In
             * gameplay, images as wide as the 4:3 screen (the black bars
             * that slide in for briefings, fades) stretch across the whole
             * window, about its centre; the HUD's side elements move to the
             * edges (Port_HudShift). In menus only the letterbox bars do
             * (640x60): backdrops and panels keep their 4:3 shape. */
            f32 wx, wy, shift = 0.0f;
            s8 stretch;

            Port_ViewWiden(&wx, &wy);
            stretch = wx > 1.0f && GETP(slot->parent) == NULL &&
                      (slot->kind == IFACE_KIND_IMAGE || slot->kind == IFACE_KIND_ANIM_IMAGE) &&
                      slot->scale.v.x >= 639.0f &&
                      (StateManager_CurrState() == 5 || slot->scale.v.y <= 120.0f);
            if (!stretch) {
                shift = Port_HudShift(iface, slot);
            }
            if (stretch || shift != 0.0f) {
                if (!restore) {
                    restore = 1;
                    savedPos = slot->pos.v;
                    savedRot = slot->rot.v;
                    savedScale = slot->scale.v;
                }
                if (stretch) {
                    slot->pos.v.x = 320.0f + (slot->pos.v.x - 320.0f) * wx;
                    slot->scale.v.x *= wx;
                } else {
                    slot->pos.v.x += shift;
                }
            }
        }
        if (GETP(slot->car) != NULL) {
            for (i = 0; i < iface->count; i++) {
                if (GETP(iface->objects)[i].role == 4) {
                    carSlot = &GETP(iface->objects)[i];
                    break;
                }
            }
            Gfx_GetCameraPosition(&cam);
            if (carSlot != NULL) {
                cam.x -= carSlot->pos.v.x;
                cam.y -= carSlot->pos.v.y;
                cam.z -= carSlot->pos.v.z;
                SetOrientation(&mtx, 0.017453292f * (slot->rot.v.y + carSlot->rot.v.y), 0.017453292f * (slot->rot.v.x + carSlot->rot.v.x),
                               0.017453292f * (slot->rot.v.z + carSlot->rot.v.z));
            } else {
                SetOrientation(&mtx, 0.017453292f * slot->rot.v.y, 0.017453292f * slot->rot.v.x, 0.017453292f * slot->rot.v.z);
            }
            if (GETP(GETP(slot->car)->vtable) == g_carAVTable) {
                ofs = (Vector4 *)GETP(((CarA *)GETP(slot->car))->vehicleIface)->viewOffset;
                if (180.0f == slot->rot.v.z || -180.0f == slot->rot.v.z) {
                    cam.x -= ofs->x;
                    cam.y -= ofs->y;
                    cam.z -= ofs->z;
                } else {
                    cam.x += ofs->x;
                    cam.y += ofs->y;
                    cam.z += ofs->z;
                }
            }
            if (GETP(slot->parent) != NULL) {
                mtx.v[3].x = GETP(GETP(slot->parent)->def)->anchorX + (cam.x + savedPos.x);
                mtx.v[3].y = GETP(GETP(slot->parent)->def)->anchorY + (cam.y + savedPos.y);
                mtx.v[3].z = cam.z + savedPos.z;
            } else {
                mtx.v[3].x = cam.x + slot->pos.v.x;
                mtx.v[3].y = cam.y + slot->pos.v.y;
                mtx.v[3].z = cam.z + slot->pos.v.z;
            }
            memcpy(&scaleMtx, &g_identity, sizeof(Matrix4));
            scaleMtx.v[0].x = slot->scale.v.x;
            scaleMtx.v[1].y = slot->scale.v.y;
            scaleMtx.v[2].z = slot->scale.v.z;
            MultiplyMatrix(&scaleMtx, &mtx, (Matrix4 *)(&GETP(slot->car)->mtxRows));
            SubMatrixToQuaternion((Matrix4 *)(&GETP(slot->car)->mtxRows), (Quaternion *)(&GETP(slot->car)->quat));
            GETP(slot->car)->flags |= 0x80;
            if (GETP(GETP(slot->car)->vtable) == g_carAVTable) {
                CarA_DrawReflection(GETP(slot->car));
            } else {
                THING_VFN(GETP(slot->car), THING_V_DRAW, void (*)(Thing *))(GETP(slot->car));
            }
        }
        if (slot->role == 7) {
            if (GETP(iface->owner) != NULL && GETP(iface->owner)->vehicle.h != 0) {
                InterfaceManager_DrawRadarBlips(&slot->pos.v.x, iface);
            }
        } else if (slot->role == 30) {
            InterfaceManager_DrawMap(slot, iface);
        } else if (slot->role != 4) {
            switch (slot->kind) {
            case 6:
                if ((u32)slot->child != iface->index) {
                    InterfaceManager_RenderInterface(&g_interfaces[slot->child]);
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
                u32 n = slot->textShown;

                if (!slot->paragraph && n < slot->textLen) {
                    if (GETP(slot->string) != NULL) {
                        strncpy(text, GETP(slot->string), n);
                    } else {
                        strncpy(text, slot->text, n);
                    }
                    text[n] = 0;
                } else {
                    if (GETP(slot->string) != NULL) {
                        strcpy(text, GETP(slot->string));
                    } else {
                        strcpy(text, slot->text);
                    }
                    n = 1;
                }
                if (n != 0) {
                    IfaceText *info = GETP(GETP(slot->def)->sub);

                    Gfx_SetTextColor(slot->color.rgba[0], slot->color.rgba[1], slot->color.rgba[2],
                                     slot->color.rgba[3]);
                    Gfx_FontManager_GetPrintRect(text, slot->fontId, &width, &height, slot->textSize.v.x);
                    if (info->scaled) {
                        if (slot->paragraph) {
                            width = info->width;
                            width *= 0.5f;
                            height = info->height;
                            height *= 0.5f;
                            Gfx_FontManager_PrintParagraph(text, slot->textShown, slot->fontId, info->blend,
                                                           info->align, &slot->scale.v.x,
                                                           slot->pos.v.x - width - width * GETP(slot->def)->anchorX,
                                                           slot->pos.v.y - height - height * GETP(slot->def)->anchorY,
                                                           slot->textSize.v.x, info->width, info->height, info->lineSpacing);
                        } else {
                            width *= 0.5f * slot->scale.v.x;
                            height *= 0.5f * slot->scale.v.y;
                            Gfx_FontManager_PrintScale(text, slot->fontId, &slot->scale.v.x, info->blend,
                                                       slot->pos.v.x - width - width * GETP(slot->def)->anchorX,
                                                       slot->pos.v.y - height - height * GETP(slot->def)->anchorY,
                                                       slot->textSize.v.x);
                        }
                    } else {
                        if (slot->paragraph) {
                            width = info->width;
                            width *= 0.5f;
                            height = info->height;
                            height *= 0.5f;
                            Gfx_FontManager_PrintParagraph(text, slot->textShown, slot->fontId, info->blend,
                                                           info->align, NULL,
                                                           slot->pos.v.x - width - width * GETP(slot->def)->anchorX,
                                                           slot->pos.v.y - height - height * GETP(slot->def)->anchorY,
                                                           slot->textSize.v.x, info->width, info->height, info->lineSpacing);
                        } else {
                            width *= 0.5f;
                            height *= 0.5f;
                            Gfx_FontManager_Print(text, slot->fontId, info->blend,
                                                  slot->pos.v.x - width - width * GETP(slot->def)->anchorX,
                                                  slot->pos.v.y - height - height * GETP(slot->def)->anchorY,
                                                  slot->textSize.v.x);
                        }
                    }
                }
                break;
            }
            case 4:
                if (!slot->thingCreated) {
                    SETP(prm.p, GETP(slot->data));
                    prm.unk38 = 0.0f;
                    prm.pos.x = prm.pos.y = prm.pos.z = 0.0f;
                    prm.vel.x = prm.vel.y = prm.vel.z = 0.0f;
                    prm.quat.x = prm.quat.y = prm.quat.z = 0.0f;
                    prm.quat.w = 1.0f;
                    prm.id = 0;
                    SETP(slot->thing, ThingManager_DARYL_CreateThing(2, &prm));
                    slot->thingCreated = 1;
                }
                if (GETP(slot->thing) != NULL) {
                    carSlot = NULL;
                    for (i = 0; i < iface->count; i++) {
                        if (GETP(iface->objects)[i].role == 4) {
                            carSlot = &GETP(iface->objects)[i];
                            break;
                        }
                    }
                    Gfx_GetCameraPosition(&thingPos);
                    if (carSlot != NULL) {
                        PSVECSubtract(&thingPos, &carSlot->pos.v, &thingPos);
                        SetOrientation(&orient, 0.017453292f * (slot->rot.v.y + carSlot->rot.v.y), 0.017453292f * (slot->rot.v.x + carSlot->rot.v.x),
                                       0.017453292f * (slot->rot.v.z + carSlot->rot.v.z));
                    } else {
                        SetOrientation(&orient, 0.017453292f * slot->rot.v.y, 0.017453292f * slot->rot.v.x, 0.017453292f * slot->rot.v.z);
                    }
                    orient.v[3].x = thingPos.x;
                    orient.v[3].y = thingPos.y;
                    orient.v[3].z = thingPos.z;
                    XformVec(&orient, &slot->pos.v, &thingPos);
                    GETP(slot->thing)->mtxRows[3] = thingPos;
                }
                break;
            case 3:
                if (!slot->thingCreated) {
                    fx.id = (u32)HND(GETP(slot->data));
                    fx.pos.x = fx.pos.y = fx.pos.z = 0.0f;
                    fx.vel.x = fx.vel.y = fx.vel.z = 0.0f;
                    fx.quat.x = fx.quat.y = fx.quat.z = 0.0f;
                    fx.quat.w = 1.0f;
                    fx.unk44 = 0;
                    fx.unk48 = 0.0f;
                    fx.unk4C = 0.0f;
                    fx.angVel = g_zeroVector;
                    SETP(slot->thing, ThingManager_DARYL_CreateThing(1, &fx));
                    slot->thingCreated = 1;
                }
                if (GETP(slot->thing) != NULL) {
                    GETP(slot->thing)->mtxRows[3] = slot->pos.v;
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

                uvA.x = slot->uv.uv[0];
                uvA.y = slot->uv.uv[1];
                uvB.x = slot->uv.uv[6];
                uvB.y = slot->uv.uv[7];
                from = slot->pos.v;
                PSVECAdd(&from, &slot->rot.v, &to);
                Gfx_Add2DFXLine(&from, &to, slot->color.rgba, slot->color.rgba, 5);
                if (GETP(slot->def)->linkId != 0) {
                    from = to;
                }
                PSVECAdd(&from, &slot->scale.v, &to);
                Gfx_Add2DFXLine(&from, &to, slot->color.rgba, slot->color.rgba, 5);
                if (GETP(slot->def)->linkId != 0) {
                    from = to;
                }
                PSVECAdd(&from, &uvA, &to);
                Gfx_Add2DFXLine(&from, &to, slot->color.rgba, slot->color.rgba, 5);
                if (GETP(slot->def)->linkId != 0) {
                    from = to;
                }
                PSVECAdd(&from, &uvB, &to);
                Gfx_Add2DFXLine(&from, &to, slot->color.rgba, slot->color.rgba, 5);
                break;
            }
            case 12: {
                IfacePoly *poly = GETP(GETP(slot->def)->sub);
                Vector4 polyFrom __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 1.0f};
                Vector4 polyTo __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 1.0f};

                if (poly->count != 0) {
                    col0[0] = (GETP(poly->points)[0].rgba[0] + slot->color.rgba[0]) / 2;
                    col0[1] = (GETP(poly->points)[0].rgba[1] + slot->color.rgba[1]) / 2;
                    col0[2] = (GETP(poly->points)[0].rgba[2] + slot->color.rgba[2]) / 2;
                    col0[3] = (GETP(poly->points)[0].rgba[3] + slot->color.rgba[3]) / 2;
                    polyFrom.x = GETP(poly->points)[0].x * slot->scale.v.x;
                    polyFrom.y = GETP(poly->points)[0].y * slot->scale.v.y;
                    PSVECAdd(&slot->pos.v, &polyFrom, &polyFrom);
                    for (i = 1; i < poly->count; i++) {
                        f32 rot;

                        col1[0] = (slot->color.rgba[0] + GETP(poly->points)[i].rgba[0]) / 2;
                        col1[1] = (slot->color.rgba[1] + GETP(poly->points)[i].rgba[1]) / 2;
                        col1[2] = (slot->color.rgba[2] + GETP(poly->points)[i].rgba[2]) / 2;
                        col1[3] = (slot->color.rgba[3] + GETP(poly->points)[i].rgba[3]) / 2;
                        polyTo.x = slot->scale.v.x * GETP(poly->points)[i].x;
                        polyTo.y = slot->scale.v.y * GETP(poly->points)[i].y;
                        rot = slot->rot.v.x;
                        if (rot != 0.0f) {
                            f32 ang = 57.29578f * ArcTan(polyTo.x, polyTo.y) + rot;
                            f32 len = Sqrt(polyTo.x * polyTo.x + polyTo.y * polyTo.y);

                            SinCos(&sinA, &cosA, ang);
                            polyTo.x = len * sinA;
                            polyTo.y = len * cosA;
                        }
                        PSVECAdd(&slot->pos.v, &polyTo, &polyTo);
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
            slot->pos.v = savedPos;
            slot->rot.v = savedRot;
            slot->scale.v = savedScale;
        }
    }
}

void InterfaceManager_DrawObject_Standard_Geometry(InterfaceObject *object, Interface *iface) {
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
    if (obj->role == 0x1F) {
        s = (f32)sin(0.5f * (0.017453292f * Gfx_GetProjectionMatrix()));
        if ((Vector4 *)PTR(g_TargetArrow[0]) == NULL || GameSettings_GetCodeVariable(0x198, 0)) {
            return;
        }
        scale = 0.1f;
        Gfx_GetCameraPosition(&camPos);
        Gfx_GetCameraVector(&at, &up, &right);
        a = -obj->pos.v.x * scale;
        b = scale * (1.1f * -s * obj->pos.v.z);
        c = scale * Sqrt(225.0f - s * (s * (obj->pos.v.z * obj->pos.v.z)));
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
        if ((GETP(iface->objects))[i].role == 4) {
            parent = &(GETP(iface->objects))[i];
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
        view.v[3].x = view.v[0].x * (camPos.x - parent->pos.v.x) + view.v[1].x * (camPos.y - parent->pos.v.y) + view.v[2].x * (camPos.z - parent->pos.v.z);
        view.v[3].y = view.v[0].y * (camPos.x - parent->pos.v.x) + view.v[1].y * (camPos.y - parent->pos.v.y) + view.v[2].y * (camPos.z - parent->pos.v.z);
        view.v[3].z = view.v[0].z * (camPos.x - parent->pos.v.x) + view.v[1].z * (camPos.y - parent->pos.v.y) + view.v[2].z * (camPos.z - parent->pos.v.z);
        SetOrientation(&rot, 0.017453292f * (obj->rot.v.y + parent->rot.v.y),
                       0.017453292f * (obj->rot.v.x + parent->rot.v.x),
                       0.017453292f * (obj->rot.v.z + parent->rot.v.z));
    } else {
        view.v[3].x = view.v[0].x * camPos.x + view.v[1].x * camPos.y + view.v[2].x * camPos.z;
        view.v[3].y = view.v[0].y * camPos.x + view.v[1].y * camPos.y + view.v[2].y * camPos.z;
        view.v[3].z = view.v[0].z * camPos.x + view.v[1].z * camPos.y + view.v[2].z * camPos.z;
        SetOrientation(&rot, 0.017453292f * obj->rot.v.y, 0.017453292f * obj->rot.v.x, 0.017453292f * obj->rot.v.z);
    }
    rot.v[3].x = obj->pos.v.x;
    rot.v[3].y = obj->pos.v.y;
    rot.v[3].z = obj->pos.v.z;
    memcpy(&mtx, &g_identity, sizeof(Matrix4));
    mtx.v[0].x = obj->scale.v.x;
    mtx.v[1].y = obj->scale.v.y;
    mtx.v[2].z = obj->scale.v.z;
    MultiplyMatrix(&mtx, &rot, &tmp);
    MultiplyMatrix(&tmp, &view, &out);
    Gfx_DrawGeometry(HND(GETP(obj->geom)), HND(&out));
}

void InterfaceManager_DrawObject_Standard_Image(InterfaceObject *obj) {
    GDLRect local;
    GDLRect *rect;
    f32 x0, y0;
    f32 s, c;
    f32 tx, ty;
    InterfaceObject *anchor;

    if (obj->texture == 0xFFFF) {
        return;
    }
    if (obj->pos.v.z != 0.0f) {
        rect = &local;
    } else {
        rect = Gfx_Add2DRectangle();
    }
    rect->blendMode = obj->blendMode;
    rect->tex = obj->texture;
    anchor = GETP(obj->def);
    x0 = 0.5f * obj->scale.v.x + 0.5f * (anchor->scale.v.x * obj->scale.v.x);
    y0 = 0.5f * obj->scale.v.y + 0.5f * (anchor->scale.v.y * obj->scale.v.y);
    x0 = -x0;
    y0 = -y0;
    rect->v[0].x = x0;
    rect->v[0].y = y0;
    rect->v[1].x = x0 + obj->scale.v.x;
    rect->v[1].y = y0;
    rect->v[2].x = x0 + obj->scale.v.x;
    rect->v[2].y = y0 + obj->scale.v.y;
    rect->v[3].x = x0;
    rect->v[3].y = y0 + obj->scale.v.y;
    rect->v[0].s = obj->uv.uv[0];
    rect->v[0].t = obj->uv.uv[7];
    rect->v[1].s = obj->uv.uv[6];
    rect->v[1].t = obj->uv.uv[7];
    rect->v[2].s = obj->uv.uv[6];
    rect->v[2].t = obj->uv.uv[1];
    rect->v[3].s = obj->uv.uv[0];
    rect->v[3].t = obj->uv.uv[1];
    rect->v[0].color = *(GXColor *)obj->color.rgba;
    rect->v[1].color = *(GXColor *)obj->color.rgba;
    rect->v[2].color = *(GXColor *)obj->color.rgba;
    rect->v[3].color = *(GXColor *)obj->color.rgba;
    if (obj->rot.v.x) {
        SinCos(&s, &c, obj->rot.v.x);
        tx = rect->v[0].x;
        ty = rect->v[0].y;
        rect->v[0].x = obj->pos.v.x + (tx * c - ty * s);
        rect->v[0].y = obj->pos.v.y + (tx * s + ty * c);
        tx = rect->v[1].x;
        ty = rect->v[1].y;
        rect->v[1].x = obj->pos.v.x + (tx * c - ty * s);
        rect->v[1].y = obj->pos.v.y + (tx * s + ty * c);
        tx = rect->v[2].x;
        ty = rect->v[2].y;
        rect->v[2].x = obj->pos.v.x + (tx * c - ty * s);
        rect->v[2].y = obj->pos.v.y + (tx * s + ty * c);
        tx = rect->v[3].x;
        ty = rect->v[3].y;
        rect->v[3].x = obj->pos.v.x + (tx * c - ty * s);
        rect->v[3].y = obj->pos.v.y + (tx * s + ty * c);
    } else {
        rect->v[0].x += obj->pos.v.x;
        rect->v[0].y += obj->pos.v.y;
        rect->v[1].x += obj->pos.v.x;
        rect->v[1].y += obj->pos.v.y;
        rect->v[2].x += obj->pos.v.x;
        rect->v[2].y += obj->pos.v.y;
        rect->v[3].x += obj->pos.v.x;
        rect->v[3].y += obj->pos.v.y;
    }
    if (obj->pos.v.z != 0.0f) {
        Gfx_Add2DRectangleBack(rect);
    }
}

void InterfaceManager_DrawRadarBlips(f32 *centre, Interface *iface) {
    extern u32 g_radarBlip;
    void *targets[32];
    f32 c, s;
    GXColor colour;
    Thing *car;
    u32 count;
    u32 i;
    RadarBlip *blip;
    GDLRect *rect;
    f32 u1, u0;
    f32 dx, dz;
    f32 dist;
    f32 scale;
    f32 x, y;

    count = GetAllTargets(targets);
    car = GETP(GETP(iface->owner)->vehicle);
    if (iface->paused == 0) {
        g_radarBlip++;
    }
    if (g_radarBlip > 15) {
        g_radarBlip = 0;
    }
    u0 = 0.25f * (g_radarBlip >> 2);
    if (car == NULL || count == 0) {
        return;
    }
    /* the radar turns with the car: its heading from the forward axis */
    SinCosRad(&s, &c, ArcTan(car->axis[2].z, car->axis[2].x));
    s *= -1.0f;
    c *= -1.0f;
    u1 = 0.25f + u0;
    for (i = 0; i < 64; i++) {
        blip = (RadarBlip *)&THING_POOLS->radarBlips[i];
        if (blip->base.flags & THING_FLAG_FREE) {
            continue;
        }
        dx = 0.05f * (blip->lastPos.x - car->pos.x);
        dz = 0.05f * (blip->lastPos.z - car->pos.z);
        colour = *GETP(GETP(blip->data)->radarColor);
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
        rect->v[0].color = colour;
        rect->v[1].color = colour;
        rect->v[2].color = colour;
        rect->v[3].color = colour;
        rect->blendMode = 2;
        rect->tex = GETP(blip->data)->radarTexture;
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
    s32 i;

    /* action[1] 1: the players' HUDs back on, else off */
    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        if (action[1] == 1) {
            Player_RefreshInterface(GETP(CAMERA_MANAGER->cameras[i].player));
        } else {
            InterfaceManager_ReleaseInterface(GETP(CAMERA_MANAGER->cameras[i].player)->interfaceIndex);
            GETP(CAMERA_MANAGER->cameras[i].player)->interfaceIndex = -1;
        }
    }
    return 1;
}

void InterfaceManager_DrawMap(InterfaceObject *obj, Interface *iface) {
    GXColor colour;
    f32 s;
    f32 c;
    GDLRect *rect;
    f32 x, z;
    f32 scaleX, scaleZ;
    f32 offsetX, offsetZ;
    f32 halfW, halfH;
    Thing *car;

    if (GETP(iface->owner) != NULL && GETP(GETP(iface->owner)->vehicle) != NULL &&
        ((WorldGameData *)World_GetGameData())->mapTexture != 0) {
        x = ((Thing *)GETP(GETP(iface->owner)->vehicle))->pos.x;
        z = ((Thing *)GETP(GETP(iface->owner)->vehicle))->pos.z;
    } else {
        return;
    }
    scaleX = ((WorldGameData *)World_GetGameData())->mapScaleX;
    scaleZ = ((WorldGameData *)World_GetGameData())->mapScaleZ;
    offsetX = ((WorldGameData *)World_GetGameData())->mapOffsetX;
    offsetZ = ((WorldGameData *)World_GetGameData())->mapOffsetZ;
    halfW = ((WorldGameData *)World_GetGameData())->mapHalfW;
    halfH = ((WorldGameData *)World_GetGameData())->mapHalfH;
    /* the map, turned with the car around its position */
    rect = Gfx_Add2DRectangle();
    rect->blendMode = 2;
    rect->tex = ((WorldGameData *)World_GetGameData())->mapTexture;
    rect->v[0].x = obj->pos.v.x;
    rect->v[0].y = obj->pos.v.y;
    rect->v[1].x = obj->pos.v.x + obj->scale.v.x;
    rect->v[1].y = obj->pos.v.y;
    rect->v[2].x = obj->pos.v.x + obj->scale.v.x;
    rect->v[2].y = obj->pos.v.y + obj->scale.v.y;
    rect->v[3].x = obj->pos.v.x;
    rect->v[3].y = obj->pos.v.y + obj->scale.v.y;
    x = offsetX + x / scaleX;
    z = offsetZ + -z / scaleZ;
    car = GETP(GETP(iface->owner)->vehicle);
    SinCosRad(&s, &c, 2.3561945f + ArcTan(car->axis[2].z, car->axis[2].x));
    rect->v[0].s = x - halfW * c;
    rect->v[0].t = halfH * s + z;
    rect->v[1].s = halfW * s + x;
    rect->v[1].t = halfH * c + z;
    rect->v[2].s = halfW * c + x;
    rect->v[2].t = z - halfH * s;
    rect->v[3].s = x - halfW * s;
    rect->v[3].t = z - halfH * c;
    rect->v[0].color = *(GXColor *)obj->color.rgba;
    rect->v[1].color = *(GXColor *)obj->color.rgba;
    rect->v[2].color = *(GXColor *)obj->color.rgba;
    rect->v[3].color = *(GXColor *)obj->color.rgba;
    rect = Gfx_Add2DRectangle();
    rect->blendMode = 2;
    rect->tex = 0xFFFF;
    colour.r = colour.g = colour.b = 0xFF;
    colour.a = 0xFF;
    rect->v[0].x = obj->scale.v.x / 2.0f + obj->pos.v.x - 1.0f;
    rect->v[0].y = obj->scale.v.y / 2.0f + obj->pos.v.y - 1.0f;
    rect->v[1].x = 1.0f + (obj->scale.v.x / 2.0f + obj->pos.v.x);
    rect->v[1].y = obj->scale.v.y / 2.0f + obj->pos.v.y - 1.0f;
    rect->v[2].x = 1.0f + (obj->scale.v.x / 2.0f + obj->pos.v.x);
    rect->v[2].y = 1.0f + (obj->scale.v.y / 2.0f + obj->pos.v.y);
    rect->v[3].x = obj->scale.v.x / 2.0f + obj->pos.v.x - 1.0f;
    rect->v[3].y = 1.0f + (obj->scale.v.y / 2.0f + obj->pos.v.y);
    rect->v[0].s = 0.0f;
    rect->v[0].t = 1.0f;
    rect->v[1].s = 1.0f;
    rect->v[1].t = 1.0f;
    rect->v[2].s = 1.0f;
    rect->v[2].t = 0.0f;
    rect->v[3].s = 0.0f;
    rect->v[3].t = 0.0f;
    rect->v[0].color = colour;
    rect->v[1].color = colour;
    rect->v[2].color = colour;
    rect->v[3].color = colour;
}

/* action: { type, world record }; points the target arrow at the record's
 * spawned thing, else at the record's own position. */
s32 ActionTargetArrowSetTarget(void *params) {
    extern s32 g_TargetArrow[7];
    extern Vector4 g_zeroVector;
    typedef struct TargetArrowAction {
        u32 type;               /* 1 TileCar, 2 QuadInstance, 3 QuadItem, 4 TilePath, 6 a position */
        PTR32(void) obj;
    } TargetArrowAction;
    TargetArrowAction *p = params;
    void *target;

    g_TargetArrow[3] = 0;
    switch (p->type) {
    case 1: {
        TileCar *tile = GETP(p->obj);

        if (GETP(tile->car) != NULL) {
            target = &((Thing *)GETP(tile->car))->pos;
        } else {
            target = tile;
        }
        break;
    }
    case 4: {
        TilePath *path = GETP(p->obj);

        if (GETP(path->thing) != NULL) {
            target = &GETP(path->thing)->pos;
        } else {
            target = &g_zeroVector;
        }
        break;
    }
    case 2: {
        QuadInstance *inst = GETP(p->obj);

        if (GETP(inst->thing) != NULL) {
            target = &GETP(inst->thing)->pos;
        } else {
            target = inst;
        }
        break;
    }
    case 3: {
        QuadItem *item = GETP(p->obj);

        if (GETP(item->thing) != NULL) {
            /* followed: Heartbeat retargets it at the item's carrier */
            g_TargetArrow[3] = (s32)HND(GETP(item->thing));
            target = &((Thing *)GETP(item->thing))->pos;
        } else {
            target = item;
        }
        break;
    }
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
    u32 i;
    Interface *iface = &g_interfaces[index];

    for (i = 0; i < *(u32 *)iface; i++) {
        if ((GETP(iface->objects))[i].kind == 6) {
            Interface *child = &g_interfaces[(GETP(iface->objects))[i].child];

            if (child->paused == 0 &&
                (child->state == 0 ||
                 (child->state == 1 && child->timer != 0))) {
                return 0;
            }
        }
    }
    return 1;
}

void DropShip_StartChildInterface(s32 index, u32 id) {
    u16 animId;
    u32 i;
    Interface *iface;

    iface = &g_interfaces[index];
    for (i = 0; i < iface->count; i++) {
        if (GETP(iface->objects)[i].kind == 6) {
            animId = GETP(GETP(iface->objects)[i].def)->id;
            if ((animId == id) && (g_interfaces[GETP(iface->objects)[i].child].state != 0)) {
                g_interfaces[GETP(iface->objects)[i].child].paused = 0;
                InterfaceManager_SetLoadState(&g_interfaces[GETP(iface->objects)[i].child], 0);
            } else {
                g_interfaces[GETP(iface->objects)[i].child].paused = 1;
                g_interfaces[GETP(iface->objects)[i].child].state = 2;
            }
        }
    }
}


void DropShip_EndChildInterface(s32 index, u32 id)
{
    u32 i;
    Interface *iface = &g_interfaces[index];
    InterfaceObject *slot;

    for (i = 0; i < *(u32 *)iface; i++)
    {
        slot = &(GETP(iface->objects))[i];
        if (slot->kind == 6 && GETP(slot->def)->id == id &&
            g_interfaces[slot->child].state != 1)
        {
            InterfaceManager_SetLoadState(&g_interfaces[slot->child], 1);
        }
    }
}

void DropShip_EndInterface(s32 index) {
    Interface *iface = &g_interfaces[index];

    *(u16 *)PTR((*(u32 *)PTR((iface->timeoutEvent.h + 8)) + 6)) = 8;
    if (iface->state != 1) {
        InterfaceManager_SetLoadState(iface, 1);
        InterfaceManager_MoveInterface(iface, 0.0f);
    }
}

void DropShip_PullCameraBack(s32 index) {
    IfaceObjectDef *info;
    u32 i;
    u32 j;
    Interface *iface;

    iface = &g_interfaces[index];
    for (i = 0; i < iface->count; i++) {
        if (GETP(iface->objects)[i].role == 4) {
            InterfaceManager_SetObjectState(iface, &GETP(iface->objects)[i], 2, 0);
            info = (IfaceObjectDef *)GETP(GETP(iface->objects)[i].def);
            for (j = 0; j < info->numStates; j++) {
                if ((GETP(GETP(GETP(iface->objects)[i].def)->states))[j].type == 2) {
                    iface->timeout = (f32)(GETP(GETP(GETP(iface->objects)[i].def)->states))[j].value;
                    break;
                }
            }
            GETP(GETP(iface->timeoutEvent)->def)->id = 9;
        }
    }
}

void DropShip_PullCameraForward(s32 index) {
    IfaceObjectDef *info;
    u32 i;
    u32 j;
    Interface *iface;

    iface = &g_interfaces[index];
    for (i = 0; i < iface->count; i++) {
        if (GETP(iface->objects)[i].role == 4) {
            InterfaceManager_SetObjectState(iface, &GETP(iface->objects)[i], 3, 0);
            GETP(iface->objects)[i].hidden = 0;
            info = (IfaceObjectDef *)GETP(GETP(iface->objects)[i].def);
            for (j = 0; j < info->numStates; j++) {
                if ((GETP(GETP(GETP(iface->objects)[i].def)->states))[j].type == 3) {
                    iface->timeout = (f32)(GETP(GETP(GETP(iface->objects)[i].def)->states))[j].value;
                    break;
                }
            }
            GETP(GETP(iface->timeoutEvent)->def)->id = 10;
            iface->state = 2;
        }
    }
}

f32 DropShip_GetLRTime(s32 index)
{
    u32 i;
    Interface *iface = &g_interfaces[index];

    for (i = 0; i < *(u32 *)iface; i++)
    {
        if ((GETP(iface->objects))[i].role == 5)
        {
            return (f32)GETP((GETP(iface->objects))[i].def)->id;
        }
    }
    return 0.0f;
}

void InterfaceManager_SetMissionObjectiveText(InterfaceObject *obj, WorldObjective *objective) {
    WorldObjectiveInfo *info = GETP(objective->info);
    char **text;
    char buf[16];
    char *str;
    s32 i;
    s32 j;
    s32 len;

    if (GETP(info->done) == NULL) {
        if (GETP(info->text) != NULL) {
            text = GETP(info->text);
        } else {
            text = GETP(info->doneText);
        }
    } else if (GETP(info->done)->value) {
        text = GETP(info->doneText);
    } else {
        text = GETP(info->text);
    }
    if (text == NULL) {
        return;
    }
    /* port: the decomp read the type as a big-endian word, taking in `mode`
     * and `id`; the game tests the byte */
    if (info->type == 0) {
        InterfaceManager_SetObjectText(obj, text);
        return;
    }
    buf[0] = 0;
    if (info->type == 1) {
        sprintf(buf, "%d", (u32)((WorldVariable *)GETP(info->value))->value);
    } else if (info->type == 2) {
        u32 ms = ((WorldTimer *)GETP(info->value))->time;
        u32 secs = ms / 1000;
        u32 mins = secs / 60;
        secs %= 60;
        sprintf(buf, "%02d:%02d", mins, secs);
    } else if (info->type == 3) {
        u32 *var = GETP(info->value);
        u32 value;
        if (var[0] < 10) {
            value = GetCodeVariable(var[0], objective->arg);
        } else {
            value = GetCodeVariable(var[0], var[1]);
        }
        sprintf(buf, "%d", value);
    }
    str = SLOT_LOAD(char *, text);
    len = strlen(str);
    j = strlen(buf);
    if (len != strlen((char *)(&obj->savedText))) {
        strcpy((char *)(&obj->savedText), str);
    } else {
        strcpy(str, (char *)(&obj->savedText));
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


/* Puts back the objective text of a role 0x34 object, whose '#'s
 * SetMissionObjectiveText filled in place. */
void InterfaceManager_RestoreMissionObjString(InterfaceObject *obj) {
    World *world;
    WorldObjectiveInfo *info;
    char **str;
    u32 i;
    u32 id;
    f32 zero;

    if (obj->def.h == 0) {
        return;
    }
    id = GETP(obj->def)->id;
    if (strlen(obj->savedText) == 0) {
        return;
    }
    world = GETP(g_worldData.world);
    if (world == NULL) {
        return;
    }
    zero = 0.0f;
    for (i = 0; i < world->numObjectives; i++) {
        info = GETP(GETP(world->objectives)[i].info);
        if (info->id == id) {
            /* sic: `done` is not checked for NULL here */
            if (GETP(info->done)->value != zero) {
                str = GETP(info->doneText);
            } else {
                str = GETP(info->text);
            }
            strcpy(SLOT_LOAD(char *, str), obj->savedText);
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

/* action: { -1: every interface, header to end, and whether to end it once
 * it is created if it isn't up yet } */
s32 ActionEndInterface(void *action) {
    typedef struct EndInterfaceAction {
        s32 all;
        PTR32(InterfaceHeader) header;
        PTR32(void) later;      /* non-NULL: deferred (g_pInterfaceToEnd) */
    } EndInterfaceAction;
    #define g_pInterfaceToEnd (*(void **)(void *)&g_pInterfaceToEnd)
    EndInterfaceAction *a = action;
    u32 i;
    s8 found;

    if (a->all == -1) {
        for (i = 0; i < 0x12; i++) {
            if (g_interfaces[i].count != 0) {
                InterfaceManager_SetLoadState(&g_interfaces[i], 1);
            }
        }
    } else {
        found = 0;
        for (i = 0; i < 0x12; i++) {
            if (g_interfaces[i].count != 0 && GETP(g_interfaces[i].header) == GETP(a->header)) {
                InterfaceManager_SetLoadState(&g_interfaces[i], 1);
                found = 1;
            }
        }
        if (found == 0 && GETP(a->later) != NULL) {
            g_pInterfaceToEnd = GETP(a->header);
        }
    }
    return 1;
#undef g_pInterfaceToEnd
}

s32 InterfaceManager_AddFont(IfaceFont *font) {
    u32 i;
    s32 id;
    IfaceFontChar *chr;

    id = Gfx_FontManager_AddFont(font->texture, font->caseMode, font->charWidth, font->charHeight, font->spacing);
    for (i = 0; i < font->numChars; i++) {
        chr = &GETP(font->chars)[i];
        Gfx_FontManager_SetCharacterSpacing(id, chr->c, chr->spacingX, chr->spacingY);
    }
    return id;
}

void InterfaceManager_AddInterfaceToRender(u32 index, u32 priority) {
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
        if (priority > GETP(g_interfaces[g_interfaceRenderOrder[i]].header)->layer) {
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

