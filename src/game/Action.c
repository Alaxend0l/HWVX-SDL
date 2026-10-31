/* The action half of the scripting system: the queue of pending actions and
 * the action functions of pActionFuncArray kept here. See Trigger.h. */
#include "common.h"
#include "AI.h"
#include "SoundSequence.h"
#include "CarA.h"
#include "World.h"
#include "Trigger.h"
#include "Nav.h"

/* The AI scripts of a vehicle record, one per difficulty (CarAInfo 0x90). */

/* An action parameter word: the handle of a world record. */
#define ACTION_RECORD(type, h) ((type *)PTR(h))

static inline Player *Action_GetCarPlayer(CarA *car) {
    Player *player;

    if (car != NULL) {
        player = GETP(((Thing *)car)->player);
    } else {
        player = NULL;
    }
    return player;
}

s32 ActionCameraSetTargets(u32 *mode) {
    CameraManager_SetCameraGlobalMode(mode);
    return 1;
}

s32 ActionRoundComplete(s32 *arg0) {
    GameRules_ForceRoundEnd(*arg0);
    return 1;
}

/* data: { resetMotion, resetPosition, jettison } */
s32 ActionResetGameRound(void *arg0) {
    u32 *data = arg0;

    GameRules_ResetRound((s8)(u8)data[0], (s8)(u8)data[1], (s8)(u8)data[2]);
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

/* The decomp has an m2c placeholder here; the original calls Action_Init (asm). */
void Action_Create(void) {
    Action_Init();
}

void Action_Destroy(void) {

}

/* The queued actions: pack TriggerAction records, NULL in free entries.
 * port: host pointers (0x140 bytes on the GC). */
extern u8 m_Actions[];
#define ACTION_QUEUE ((TriggerAction **)(void *)m_Actions)
#define ACTION_QUEUE_SIZE 80

void Action_Init(void) {
    u32 i;

    for (i = 0; i < ACTION_QUEUE_SIZE; i++) {
        ACTION_QUEUE[i] = NULL;
    }
}

/* Runs the queued actions whose delay has passed (and drops them, except a
 * PrintDebugString still counting down), counts the others down. */
void Action_Heartbeat(void) {
    extern void (*pActionFuncArray[])(void *);
    extern s32 g_iDeltaTimeMS;
    u32 i;

    for (i = 0; i < ACTION_QUEUE_SIZE; i++) {
        TriggerAction *action = ACTION_QUEUE[i];
        if (action != NULL && action->delay <= 0) {
            pActionFuncArray[action->type](GETP(action->data));
            /* port: the countdown word is a plain value; the decomp read it as
             * a pointer slot, which faults on the host for nonzero values */
            if (ACTION_QUEUE[i]->type != ACTION_PRINT_DEBUG_STRING || ((u32 *)GETP(ACTION_QUEUE[i]->data))[1] == 0) {
                ACTION_QUEUE[i] = NULL;
            }
        } else if (action != NULL) {
            action->delay -= g_iDeltaTimeMS;
        }
    }
}

void Action_ReleaseAllActions(void) {
    memset(m_Actions, 0, ACTION_QUEUE_SIZE * sizeof(void *));  /* port: host pointers (0x140 on the GC) */
}

/* Queues an action (a TriggerAction) with its initial delay and uses up one
 * of its runs. Returns 0 once it has none left, else 1 (also when the queue is
 * full and it is dropped). */
s32 Action_AddAction(void *arg0) {
    TriggerAction *action = arg0;
    u32 i;

    for (i = 0; i < ACTION_QUEUE_SIZE; i++) {
        if (ACTION_QUEUE[i] == NULL) {
            break;
        }
    }
    if (i >= ACTION_QUEUE_SIZE) {
        Dprintf("The max number of actions has been exceeded for this point in the game.  There can only be %d simaltaneous actions\n", 80);
        return 1;
    }
    if (action->count != 0xFFFFFFFF) {
        action->count--;
    }
    ACTION_QUEUE[i] = action;
    ACTION_QUEUE[i]->delay = ACTION_QUEUE[i]->delayInit;
    if (action->count != 0) {
        return 1;
    }
    return 0;
}


/* action: { what, countdown, value }. With what 0 the action stays queued
 * while the countdown (started at 60) runs; otherwise `value` goes to every
 * human-port car's stunt messages / pickup id, or to a player message. */
s32 ActionPrintDebugString(u32 *action)
{
    u32 i;
    TileCar *tile;
    CarA *car;

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
        for (i = 0; i < WORLD()->numTileCars; i++)
        {
            tile = &GETP(WORLD()->tileCars)[i];
            car = GETP(tile->car);
            if (tile->port == 1)
            {
                switch (*action)
                {
                case 0:
                    break;
                case 1:
                    car->pickupId = action[2];
                    break;
                case 2:
                    car->stuntPopup.h = action[2];
                    break;
                case 3:
                    car->stuntPopup.h = action[2];
                    break;
                case 4:
                    InterfaceManager_SetPlayerMessage((void *)PTR(action[2]), 0);
                    break;
                case 5:
                    car->stuntScoreMessage.h = action[2];
                    break;
                case 6:
                    car->stuntMessage.h = action[2];
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

/* data: { id, name[] }; true if the pack has a slot. */
s8 ActionLoadInterfacePack(s32 *arg0) {
    s32 slot;

    slot = FileStreaming_LoadDataPack(arg0 + 1, 1, *arg0, 1);
    return slot != -1;
}

void ActionUnloadInterfacePack(s32 *arg0) {
    FileStreaming_ReleaseDataPack(FileStreaming_GetPackID((char *)(arg0 + 1), 1, *arg0), 1);
}

/* data: { op (ACTION_OP_*), var, then for var < 100: count, TileCar handles,
 * operand; for var >= 100: arg, operand }; operand: { OPERAND_*, value words }. */
s32 ActionCodeVariableModifyValue(u32 *data) {
    f32 value;
    f32 operand;
    f32 cur;
    u32 i;

    if (data[1] < 100) {
        switch (data[data[2] + 3]) {
        case OPERAND_CONSTANT:
            value = *(f32 *)&data[data[2] + 4];
            break;
        case OPERAND_VARIABLE:
            value = WORLD_VARIABLE(data[data[2] + 4])->value;
            break;
        case OPERAND_CODE_VARIABLE:
            value = GetCodeVariable(data[data[2] + 4], data[data[2] + 5]);
            break;
        }
        for (i = 0; i < data[2]; i++) {
            cur = GetCodeVariable(data[1], data[i + 3]);
            switch (data[0]) {
            case ACTION_OP_ADD:
                cur += value;
                break;
            case ACTION_OP_SUB:
                cur -= value;
                break;
            case ACTION_OP_MUL:
                cur *= value;
                break;
            case ACTION_OP_DIV:
                if (value) {
                    cur /= value;
                }
                break;
            case ACTION_OP_SET:
                cur = value;
                break;
            }
            SetCodeVariable(data[1], data[i + 3], cur);
        }
    } else {
        value = GetCodeVariable(data[1], data[2]);
        switch (data[3]) {
        case OPERAND_CONSTANT:
            operand = *(f32 *)&data[4];
            break;
        case OPERAND_VARIABLE:
            operand = WORLD_VARIABLE(data[4])->value;
            break;
        case OPERAND_CODE_VARIABLE:
            operand = GetCodeVariable(data[4], data[5]);
            break;
        }
        switch (data[0]) {
        case ACTION_OP_ADD:
            value += operand;
            break;
        case ACTION_OP_SUB:
            value -= operand;
            break;
        case ACTION_OP_MUL:
            value *= operand;
            break;
        case ACTION_OP_DIV:
            if (operand) {
                value /= operand;
            }
            break;
        case ACTION_OP_SET:
            value = operand;
            break;
        }
        SetCodeVariable(data[1], data[2], value);
    }
    return 1;
}

/* action: { mode, count, WorldObjective handles } */
s32 ActionMissionObjChangeMode(u32 *action) {
    u32 i;

    for (i = 0; i < action[1]; i++) {
        GETP(ACTION_RECORD(WorldObjective, action[i + 2])->info)[1] = action[0];
    }
    return 1;
}



/* action: { mode, count, TileCar handles }. Modes: 0 remove the car (its
 * position kept for a respawn), 1/10 spawn it with an AI player if absent,
 * 3 kill it, 9 set the AI player's flag 1 (cleared by 1/10). */
s32 ActionVehicleChangeMode(u32 *action) {
    extern Vector4 g_zeroVector;
    CarAParams params;
    Matrix4 mtx;
    AIPlayerParams desc;
    Player *player;
    u32 mode;
    u32 count;
    u32 i;
    TileCar *spawn;
    CarA *car;
    Thing *thing;
    Player *ai;
    u32 *scripts;
    u32 script;
    s32 difficulty;
    s32 paused;
    void *rack;

    mode = action[0];
    count = action[1];
    for (i = 0; i < count; i++) {
        spawn = ACTION_RECORD(TileCar, action[i + 2]);
        car = GETP(spawn->car);
        player = Action_GetCarPlayer(car);
        switch (mode) {
        case 0:
            if (car != NULL) {
                paused = Trigger_IsTACSystemPaused();
                Trigger_PauseTACSystem(1);
                spawn->pos = ((Thing *)car)->pos;
                CarA_Release(car);
                PlayerManager_RemovePlayer(player);
                SETP(spawn->car, NULL);
                Trigger_PauseTACSystem(paused);
            }
            break;
        case 9:
            player->flags |= 1;
            break;
        case 3:
            if (car != NULL) {
                Thing_ProcessEndEvent(car, GETP(GETP(car->vehicleIface)->collider), 0, 0);
                CarA_Release(car);
            }
            break;
        case 1:
        case 10:
            if (car == NULL && spawn->unk34 == 0) {
                difficulty = GameSettings_GetDifficulty();
                script = 0;
                params.pos = spawn->pos;
                params.pos.y += 10.0f;
                SETP(params.info, GETP(spawn->info));
                params.vel = g_zeroVector;
                SetOrientation(&mtx, spawn->rotY, spawn->rotX, spawn->rotZ);
                SubMatrixToQuaternion(&mtx, &params.quat);
                /* the tile's script for the difficulty, else the vehicle's
                 * (as in the original, tested by its first entry) */
                scripts = GETP(params.info)->aiScripts;
                if (difficulty == 0) {
                    if (spawn->scripts[0] != 0) {
                        script = spawn->scripts[0];
                    } else if (scripts[0] != 0) {
                        script = scripts[0];
                    }
                } else if (difficulty == 1) {
                    if (spawn->scripts[1] != 0) {
                        script = spawn->scripts[1];
                    } else if (scripts[0] != 0) {
                        script = scripts[1];
                    }
                } else if (difficulty == 2) {
                    if (spawn->scripts[2] != 0) {
                        script = spawn->scripts[2];
                    } else if (scripts[0] != 0) {
                        script = scripts[2];
                    }
                } else if (difficulty == 3) {
                    if (spawn->scripts[3] != 0) {
                        script = spawn->scripts[3];
                    } else if (scripts[0] != 0) {
                        script = scripts[3];
                    }
                } else if (scripts[0] != 0) {
                    script = scripts[0];
                } else if (scripts[1] != 0) {
                    script = scripts[1];
                } else {
                    script = scripts[2];
                }
                desc.script = script;
                desc.kind = 0;
                desc.dynamic = spawn->unk34;
                desc.nearSq = spawn->nearDist * spawn->nearDist;
                desc.farSq = spawn->farDist * spawn->farDist;
                ai = PlayerManager_NewAIPlayer(&desc);
                if (ai != NULL) {
                    SETP(params.player, ai);
                    SETP(spawn->car, ThingManager_DARYL_CreateThing(THING_TYPE_CAR, &params));
                    thing = (Thing *)GETP(spawn->car);
                    if (thing != NULL) {
                        SETP(thing->trigger, spawn);    /* the car's tile */
                        Player_SetThing(ai, thing);
                        Player_SetName(ai, -1, "AI");
                        SETP(ai->tileCar, spawn);
                        SETP(ai->aiScripts, GETP(params.info));
                        if (GETP(spawn->item) != NULL) {
                            rack = ThingManagerList_GetByType(&GETP(spawn->car)->motionState.slaves, 8);
                            WildCardRack_JettisonAll(rack);
                            WildCardRack_Add(rack, (u8 *)GETP(spawn->item), GETP(GETP(spawn->item)->weapon)->ammo);
                        }
                    } else {
                        PlayerManager_RemovePlayer(ai);
                    }
                }
            }
            if (player != NULL) {
                player->flags &= ~1;
            }
            break;
        }
        spawn->mode = mode;
    }
    return 1;
}


/* action: { enabled, count, NavPoint handles } */
s32 ActionNavPointChangeMode(u32 *action) {
    u32 i;

    for (i = 0; i < action[1]; i++) {
        ACTION_RECORD(NavPoint, action[i + 2])->enabled = action[0];
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

/* action: { scripts[4] (per difficulty), count, TileCar handles }: sets the
 * tiles' scripts, and a spawned car's AI script for the current difficulty. */
s32 ActionSwapAIScript(u32 *action) {
    CarA *car;
    u32 i;
    TileCar *tile;

    for (i = 0; i < action[4]; i++) {
        tile = ACTION_RECORD(TileCar, action[i + 5]);
        tile->scripts[0] = action[0];
        tile->scripts[1] = action[1];
        tile->scripts[2] = action[2];
        tile->scripts[3] = action[3];
        car = GETP(tile->car);
        if (car != NULL) {
            /* Player.aiScript: the AI's script for each difficulty */
            switch (GameSettings_GetDifficulty()) {
            case 0:
                ((Player *)GETP(((Thing *)car)->player))->aiScript.h = action[0];
                break;
            case 1:
                ((Player *)GETP(((Thing *)car)->player))->aiScript.h = action[1];
                break;
            case 2:
                ((Player *)GETP(((Thing *)car)->player))->aiScript.h = action[2];
                break;
            case 3:
                ((Player *)GETP(((Thing *)car)->player))->aiScript.h = action[3];
                break;
            }
            /* as in the original: the address of the car's player field */
            AI_Car_Reset((u8 *)&((Thing *)car)->player);
        }
    }
    return 1;
}

/* action: { what, count, TileCar handles }: what 0 drops the car's
 * wildcards, 1 releases a carried item. */
s32 ActionVehicleRemoveItem(u32 *action) {
    void *list;
    u32 i;
    Thing *rack;
    Thing *item;
    CarA *car;

    for (i = 0; i < action[1]; i++) {
        car = GETP(ACTION_RECORD(TileCar, action[i + 2])->car);
        if (car != NULL) {
            list = &car->motionState.slaves;
            rack = ThingManagerList_GetByType(list, 8);
            switch (action[0]) {
            case 0:
                WildCardRack_JettisonAll(rack);
                break;
            case 1:
                item = ThingManagerList_GetByType(list, 4);
                if (item != NULL) {
                    THING_VFN(item, THING_V_RELEASE, void (*)(Thing *))(item);
                }
                break;
            }
            rack->flags &= ~0x400200;
        }
    }
    return 1;
}


/* action: { what, item, count, TileCar handles }: gives the cars an item:
 * what 0: the QuadItem `item`'s pickup, spawned there if needed (a carried
 * one is first taken from whoever has it); 1: a new item, `item` being an
 * id in the world's item pack (< 1000) or an ItemRecord. */
s32 ActionVehicleGiveItem(void *arg) {
    typedef struct GiveItemAction {
        s32 type;
        PTR32(void) item;
        u32 count;
        PTR32(TileCar) cars[1];
    } GiveItemAction;
    extern Vector4 g_zeroVector;
    QuadItem *item;
    GiveItemAction *action = arg;
    u32 i;
    Thing *thing = NULL;
    u32 j;
    Thing *car;
    u8 numPlayers;
    DebrisParams p;
    ItemParams params __attribute__((aligned(16)));
    ItemParams params2;
    ItemRecord *info;
    ItemThing *t;
    u32 packID;
    void *pack;

    switch (action->type) {
    case 0:
        item = GETP(action->item);
        if (item == NULL) {
            return 0;
        }
        if (GETP(item->record)->type == ITEM_TYPE_CARRIED) {
            numPlayers = PlayerManager_PlayerCount();
            for (j = 0; j < numPlayers; j++) {
                car = GETP(((Player *)PlayerManager_GetPlayerByIndex(j))->vehicle);
                if (car != NULL && (u8 *)GETP(car->vtable) == g_carAVTable) {
                    t = ThingManagerList_GetByType(&((CarA *)car)->motionState.slaves, 4);
                    if (t != NULL && GETP(t->base.trigger) == item) {
                        ThingManagerList_Remove((void **)&((CarA *)car)->motionState.slaves, t);
                    }
                }
            }
        }
        if (GETP(item->thing) == NULL) {
            item->timer = 0;
            params.pos = item->pos;
            params.pos.y += (GETP(item->record))->height;
            params.vel.x = 0.0f;
            params.vel.y = 0.0f;
            params.vel.z = 0.0f;
            SETP(params.record, GETP(item->record));
            params.quat.w = 1.0f;
            params.quat.x = 0.0f;
            params.quat.y = 0.0f;
            params.quat.z = 0.0f;
            params.drawDist = item->hasDrawDist ? item->drawDist : 0.0f;
            params.unk38 = item->unk35;
            t = ThingManager_DARYL_CreateThing(4, &params);
            if (t != NULL) {
                item->spawned = 1;
                SETP(item->thing, t);
                SETP(GETP(item->thing)->base.trigger, item);
                t->base.flags |= 1;
                if (item->respawn != 0) {
                    SETP(t->spawner, item);
                }
                if ((GETP(item->record))->spawnDebris != 0) {
                    p.debris = (GETP(item->record))->spawnDebris;
                    p.pos = item->pos;
                    p.quat = params.quat;
                    p.vel.z = 0.0f;
                    p.vel.y = 0.0f;
                    p.vel.x = 0.0f;
                    p.owner = 0;
                    p.unk48 = 0.0f;
                    p.unk4C = 0.0f;
                    p.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
        }
        thing = (Thing *)GETP(item->thing);
        break;
    case 1:
        info = NULL;
        if (action->item.h < 1000) {
            packID = FileStreaming_GetPackID(GETP(GETP(WORLD()->gameData)->itemPack), 2, action->item.h);
            if (packID < 0xFFFFFFFF) {
                pack = FileStreaming_InitializeDataPack(packID, 2, 1);
                if (pack != NULL) {
                    info = (ItemRecord *)DataPack_GetScript(pack, (s32)action->item.h);
                }
            }
        } else {
            info = GETP(action->item);
        }
        if (info != NULL) {
            params2.pos = g_zeroVector;
            params2.pos.y += (info)->height;
            params2.vel.x = 0.0f;
            params2.vel.y = 0.0f;
            params2.vel.z = 0.0f;
            SETP(params2.record, info);
            params2.quat.w = 1.0f;
            params2.quat.x = 0.0f;
            params2.quat.y = 0.0f;
            params2.quat.z = 0.0f;
            params2.drawDist = 0.0f;
            thing = ThingManager_DARYL_CreateThing(4, &params2);
            if (thing != NULL) {
                thing->flags |= 1;
            }
        }
        break;
    }
    if (thing == NULL) {
        return 0;
    }
    for (i = 0; i < action->count; i++) {
        if (GETP(GETP(action->cars[i])->car) != NULL) {
            CarA_ProcessItemPickup(GETP(GETP(action->cars[i])->car), thing);
        }
    }
    return 1;
}


/* data: { play, queued, sequence }: play (queued) or, with play 0, clear
 * the queue (queued) or stop the playing instances of the sequence. */
s32 ActionPlaySoundSequence(void *arg0) {
    extern u8 g_soundSeqVTable[];
    u32 *data = arg0;
    u32 id;
    Thing *thing;

    id = data[2];
    if (data[0] != 0) {
        if (id != 0) {
            if (data[1] != 0) {
                SoundSequence_PlayQueued(PTR(id), 0);
            } else {
                SoundSequence_Play(PTR(id), 0);
            }
        }
    } else if (data[1] != 0) {
        SoundSequence_ClearQueue();
    } else {
        for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
            if ((u8 *)GETP(thing->vtable) == g_soundSeqVTable && ((SoundSequence *)thing)->info.h == id) {
                THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
            }
        }
    }
    return 0;  /* the original returns whatever the last call left in r3 */
}

/* action: { mode, count, TileCreature handles }: 0/25 remove the creature,
 * 3 kill it (26 also sets the mode to 1), 1 enable spawning. */
s32 ActionCreatureChangeMode(u32 *action) {
    u32 handle;
    u32 i;
    TileCreature *tile;

    for (i = 0; i < action[1]; i++) {
        tile = ACTION_RECORD(TileCreature, action[i + 2]);
        handle = tile->thing;
        switch (action[0]) {
        case 0:
            tile->mode = 0;
            if (handle != 0) {
                tile->thing = 0;
                Creature_Release(PTR(handle));
            }
            break;
        case 1:
            tile->mode = 1;
            break;
        case 3:
            tile->mode = 0;
            if (handle != 0) {
                tile->thing = 0;
                Creature_MotionStateInitDeath(PTR(handle));
            }
            break;
        case 25:
            tile->mode = 0;
            if (handle != 0) {
                tile->thing = 0;
                Creature_Release(PTR(handle));
            }
            break;
        case 26:
            tile->mode = 1;
            if (handle != 0) {
                tile->thing = 0;
                Creature_MotionStateInitDeath(PTR(handle));
            }
            break;
        }
    }
    return 1;
}
