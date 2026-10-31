/* The trigger half of the scripting system: the active trigger lists, the
 * polling heartbeat, event triggers and the code variables tests read. See
 * Trigger.h for the data. */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "Trigger.h"


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

/* The decomp has an m2c placeholder here; the original calls Trigger_Init (asm). */
void Trigger_Create(void) {
    Trigger_Init();
}

void Trigger_Destroy(void) {
    extern s32 m_triggerData;
    Deallocate(PTR(m_triggerData));
    m_triggerData = 0;
}

/* m_Triggers[type]: the active triggers of each type, TriggerStats[type].count
 * of them, in one block (m_triggerData) sized by TriggerStats[].max.
 * port: host pointers, so the table and the lists are pointer sized. */
extern u8 m_Triggers[];
extern u8 TriggerStats[];
extern u8 g_bTACSystemPaused[];  /* s8: the whole system is paused */
#define TRIGGER_LISTS ((Trigger ***)(void *)m_Triggers)
#define TRIGGER_STATS ((TriggerStat *)(void *)TriggerStats)
#define TAC_PAUSED (*(s8 *)(void *)g_bTACSystemPaused)

/* A trigger parameter word: the handle of a world record. */
#define TRIGGER_RECORD(type, h) ((type *)PTR(h))

/* Queues `count` actions; returns how many of them can run again. */
static inline u32 Trigger_QueueActions(TriggerAction *actions, u32 count) {
    u32 queued = 0;
    u32 j;

    for (j = 0; j < count; j++) {
        queued += Action_AddAction(&actions[j]);
    }
    return queued;
}

/* An event trigger whose test matched: if its conditions hold, queue its
 * actions; once none of them can run again it is done. */
static inline void Trigger_FireEvent(Trigger *trigger) {
    if (TriggerCheckConditions(trigger->mode, trigger->numConditions, trigger->conditions.h)) {
        if (Trigger_QueueActions(GETP(trigger->actions), trigger->numActions) == 0) {
            trigger->state = TRIGGER_STATE_DONE;
        }
    }
}

void Trigger_Init(void) {
    extern s32 m_triggerData;
    extern u32 m_interfaces[][2];
    u32 i;
    u32 offset = 0;
    u32 total = 0;

    if ((u32)m_triggerData != 0) {
        Deallocate(PTR(m_triggerData));
        m_triggerData = 0;
    }
    for (i = 0; i < TRIGGER_NUM_TYPES; i++) {
        TRIGGER_STATS[i].count = 0;
        total += TRIGGER_STATS[i].max;
    }
    /* port: runtime lists of trigger pointers, so host pointer sized */
    m_triggerData = (s32)HND(Allocate(total * sizeof(void *), 0, ""));
    for (i = 0; i < TRIGGER_NUM_TYPES; i++) {
        TRIGGER_LISTS[i] = (Trigger **)((u8 *)PTR(m_triggerData) + offset * sizeof(void *));
        offset += TRIGGER_STATS[i].max;
    }
    for (i = 0; i < 10; i++) {
        m_interfaces[i][0] = 0;
    }
    TAC_PAUSED = 0;
}

/* Saves the remaining run counts of the active triggers' actions (unlimited
 * ones skipped), at most `count`, for Trigger_RestoreActions. */
void Trigger_SaveActions(s32 *saved, u32 count) {
    u32 type;
    u32 i;
    u32 j;
    u32 n;

    n = 0;
    for (type = 0; type < TRIGGER_NUM_TYPES; type++) {
        for (i = 0; i < TRIGGER_STATS[type].count; i++) {
            for (j = 0; j < TRIGGER_LISTS[type][i]->numActions; j++) {
                if (n >= count) {
                    return;
                }
                if (GETP(TRIGGER_LISTS[type][i]->actions)[j].count != 0xFFFFFFFF) {
                    saved[n++] = GETP(TRIGGER_LISTS[type][i]->actions)[j].count;
                }
            }
        }
    }
}

/* Puts back the counts saved by Trigger_SaveActions and resets every
 * trigger's state as Trigger_AddTrigger does. */
void Trigger_RestoreActions(s32 *saved, u32 count) {
    u32 type;
    u32 i;
    u32 j;
    u32 n;

    n = 0;
    for (type = 0; type < TRIGGER_NUM_TYPES; type++) {
        for (i = 0; i < TRIGGER_STATS[type].count; i++) {
            for (j = 0; j < TRIGGER_LISTS[type][i]->numActions && n < count; j++) {
                if (GETP(TRIGGER_LISTS[type][i]->actions)[j].count != 0xFFFFFFFF) {
                    GETP(TRIGGER_LISTS[type][i]->actions)[j].count = saved[n++];
                }
            }
            if (type == TRIGGER_LEVEL_START) {
                TRIGGER_LISTS[type][i]->state = TRIGGER_STATE_ARMED;
            } else {
                TRIGGER_LISTS[type][i]->state = TRIGGER_STATE_FIRED;
            }
        }
    }
}

void Trigger_PauseTACSystem(s8 arg0) {
    TAC_PAUSED = arg0;
}

u8 Trigger_IsTACSystemPaused(void) {
    return (u8)TAC_PAUSED;
}

/* Polls the triggers of the polled types. An armed trigger whose test holds
 * fires: if its conditions hold its actions are queued (PlayerVehicleInAir:
 * its else-actions when the car landed). A fired trigger is re-armed once its
 * test is false again; one whose actions are used up is done. */
void Trigger_Heartbeat(void) {
    extern s8 (*pTriggerFuncArray[])(void *param);
    extern s32 m_ReleasedInterface;
    u32 type;
    u32 i;
    u32 actions;
    Trigger *trigger;

    if (!TAC_PAUSED) {
        for (type = 0; type < TRIGGER_NUM_TYPES; type++) {
            if (TRIGGER_STAT_EVENT(&TRIGGER_STATS[type])) {
                continue;
            }
            for (i = 0; i < TRIGGER_STATS[type].count; i++) {
                /* the list entry is re-read after each call, as in the original */
                trigger = TRIGGER_LISTS[type][i];
                if ((trigger->state & TRIGGER_STATE_ARMED) && pTriggerFuncArray[trigger->type](GETP(trigger->param))) {
                    trigger = TRIGGER_LISTS[type][i];
                    if (TriggerCheckConditions(trigger->mode, trigger->numConditions, trigger->conditions.h)) {
                        trigger = TRIGGER_LISTS[type][i];
                        if (trigger->type == TRIGGER_PLAYER_VEHICLE_IN_AIR && GETP(trigger->param)[2] == 0) {
                            actions = Trigger_QueueActions(GETP(trigger->elseActions), trigger->numElseActions);
                        } else {
                            actions = Trigger_QueueActions(GETP(trigger->actions), trigger->numActions);
                        }
                        if (actions == 0) {
                            TRIGGER_LISTS[type][i]->state = TRIGGER_STATE_DONE;
                        } else {
                            TRIGGER_LISTS[type][i]->state = TRIGGER_STATE_FIRED;
                        }
                    } else {
                        TRIGGER_LISTS[type][i]->state = TRIGGER_STATE_FIRED;
                    }
                }
                trigger = TRIGGER_LISTS[type][i];
                if ((trigger->state & TRIGGER_STATE_FIRED) && !pTriggerFuncArray[trigger->type](GETP(trigger->param))) {
                    TRIGGER_LISTS[type][i]->state |= TRIGGER_STATE_ARMED;
                    TRIGGER_LISTS[type][i]->state &= ~TRIGGER_STATE_FIRED;
                }
            }
        }
        m_ReleasedInterface = -1;
    }
}

/* Activates a trigger of a loaded pack (a Trigger record); 0 if its type's
 * list is full. Also unpauses the system. */
s32 Trigger_AddTrigger(s32 *arg) {
    extern char *triggerLabelsArray[];
    Trigger *trigger = (Trigger *)arg;
    char buf[0x200];
    s32 type = trigger->type;

    if (TRIGGER_STATS[type].count == TRIGGER_STATS[type].max) {
        sprintf(buf, "Could not add the trigger: \"%s\", please notify Peter.", triggerLabelsArray[type]);
        return 0;
    }
    TRIGGER_LISTS[type][TRIGGER_STATS[type].count++] = trigger;
    if (trigger->type == TRIGGER_LEVEL_START) {
        trigger->state = TRIGGER_STATE_ARMED;
    } else {
        trigger->state = TRIGGER_STATE_FIRED;
    }
    TAC_PAUSED = 0;
    return 1;
}

/* Deactivates a trigger: the last one of its list takes its place. */
s32 Trigger_ReleaseTrigger(s32 *arg) {
    Trigger *trigger = (Trigger *)arg;
    u32 i;

    for (i = 0; i < TRIGGER_STATS[trigger->type].count; i++) {
        if (TRIGGER_LISTS[trigger->type][i] == trigger) {
            TRIGGER_STATS[trigger->type].count--;
            TRIGGER_LISTS[trigger->type][i] = TRIGGER_LISTS[trigger->type][TRIGGER_STATS[trigger->type].count];
            return 1;
        }
    }
    return 0;
}

/* Tests `count` conditions (Trigger records at handle `conds`), combined by
 * `mode` (TRIGGER_COND_*). No conditions: true. */
s8 TriggerCheckConditions(u8 mode, s32 count, s32 conds) {
    extern s8 (*pTriggerFuncArray[])(void *param);
    Trigger *cond = (Trigger *)PTR(conds);
    u32 i;

    if (count == 0) {
        return 1;
    }
    switch (mode) {
    case TRIGGER_COND_ALL:
        for (i = 0; i < count; i++) {
            if (!pTriggerFuncArray[cond[i].type](GETP(cond[i].param))) {
                return 0;
            }
        }
        return 1;
    case TRIGGER_COND_ANY:
        for (i = 0; i < count; i++) {
            if (pTriggerFuncArray[cond[i].type](GETP(cond[i].param))) {
                return 1;
            }
        }
        return 0;
    case TRIGGER_COND_NOT_ALL:
        for (i = 0; i < count; i++) {
            if (!pTriggerFuncArray[cond[i].type](GETP(cond[i].param))) {
                return 1;
            }
        }
        return 0;
    case TRIGGER_COND_NONE:
        for (i = 0; i < count; i++) {
            if (pTriggerFuncArray[cond[i].type](GETP(cond[i].param))) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

/* Returns 1 from the calling trigger check when `a op b` holds (TRIGGER_OP_*). */
#define TRIGGER_COMPARE_RETURN(op, a, b) \
    switch (op) {                        \
    case TRIGGER_OP_GREATER:             \
        if ((a) > (b)) {                 \
            return 1;                    \
        }                                \
        break;                           \
    case TRIGGER_OP_LESS:                \
        if ((a) < (b)) {                 \
            return 1;                    \
        }                                \
        break;                           \
    case TRIGGER_OP_EQUAL:               \
        if ((a) == (b)) {                \
            return 1;                    \
        }                                \
        break;                           \
    case TRIGGER_OP_GREATER_EQUAL:       \
        if ((a) >= (b)) {                \
            return 1;                    \
        }                                \
        break;                           \
    case TRIGGER_OP_LESS_EQUAL:          \
        if ((a) <= (b)) {                \
            return 1;                    \
        }                                \
        break;                           \
    }

s32 TriggerLevelStart(void) {
    return 1;
}

/* Code variables: 1 health, 2 stunt points, 3 turbo (write only), 20-27 a
 * player's counters, of the car of a TileCar; the rest are game settings.
 * port: `arg` is a 32-bit value: the TileCar handle for 1-27, otherwise a
 * plain argument for the game settings (e.g. a player number), so it only
 * becomes a pointer where one is used. */
f32 GetCodeVariable(s32 var, u32 arg) {
    TileCar *tile = var >= 1 && var <= 27 ? TRIGGER_RECORD(TileCar, arg) : NULL;
    CarA *car;
    Player *player;
    u16 index;

    switch (var) {
    case 1:
        car = GETP(tile->car);
        if (car != NULL) {
            return ((Thing *)car)->health;
        }
        break;
    case 2:
        car = GETP(tile->car);
        if (car != NULL) {
            return (u32)((Player *)GETP(((Thing *)car)->player))->stuntPoints;
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
        car = GETP(tile->car);
        index = (u16)var - 20;
        if (car != NULL) {
            player = GETP(((Thing *)car)->player);
            if (player != NULL) {
                return ((u16 *)((u8 *)player + 0x94))[index];   /* Player 0x94: u16 counters[8] */
            }
        }
        break;
    default:
        return GameSettings_GetCodeVariable(var, arg);
    }
    return 0.0f;
}

void SetCodeVariable(s32 var, u32 arg, f32 value) {
    TileCar *tile = var >= 1 && var <= 27 ? TRIGGER_RECORD(TileCar, arg) : NULL;  /* port: see GetCodeVariable */
    CarA *car;
    Player *player;
    u8 idx;

    switch (var) {
    case 1:
        car = GETP(tile->car);
        if (car != NULL) {
            ((Thing *)car)->health = (s32)value;
        }
        break;
    case 2:
        car = GETP(tile->car);
        if (car != NULL) {
            ((Player *)GETP(((Thing *)car)->player))->stuntPoints = (u32)value;
            ((Player *)GETP(((Thing *)car)->player))->stuntScored = 1;
        }
        break;
    case 3:
        car = GETP(tile->car);
        if (car != NULL) {
            s32 max;
            car->turboTime = (s32)value;
            /* as in the original: clamps the health, not the turbo */
            max = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
            if (((Thing *)car)->health > max) {
                ((Thing *)car)->health = max;
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
        car = GETP(tile->car);
        idx = (u8)var - 20;
        if (car != NULL) {
            player = GETP(((Thing *)car)->player);
            if (player != NULL) {
                ((s16 *)((u8 *)player + 0x94))[idx] = (s16)value;   /* Player 0x94: counters[8] */
                *((u8 *)GETP(((Thing *)car)->player) + 0xA4) = 1;    /* Player 0xA4: counters changed */
            }
        }
        break;
    default:
        GameSettings_SetCodeVariable(var, arg, (u32)value);
        break;
    }
}

/* data: { op, var, then for var < 100: count, TileCar handles[count],
 * operand } or for var >= 100: { arg, operand }; operand: { OPERAND_*, value
 * words }. True if any tested value compares true. */
s32 TriggerCodeVariableValueCheck(u32 *data) {
    f32 value;
    f32 operand;
    s32 i;

    if (data[1] < 100) {
        for (i = 0; i < data[2]; i++) {
            value = GetCodeVariable(data[1], data[i + 3]);
            switch (data[data[2] + 3]) {
            case OPERAND_CONSTANT:
                operand = *(f32 *)&data[data[2] + 4];
                TRIGGER_COMPARE_RETURN(data[0], value, operand);
                break;
            case OPERAND_VARIABLE:
                operand = WORLD_VARIABLE(data[data[2] + 4])->value;
                TRIGGER_COMPARE_RETURN(data[0], value, operand);
                break;
            case OPERAND_CODE_VARIABLE:
                operand = GetCodeVariable(data[data[2] + 4], data[data[2] + 5]);
                TRIGGER_COMPARE_RETURN(data[0], value, operand);
                break;
            }
        }
    } else {
        value = GetCodeVariable(data[1], data[2]);
        switch (data[3]) {
        case OPERAND_CONSTANT:
            operand = *(f32 *)&data[4];
            TRIGGER_COMPARE_RETURN(data[0], value, operand);
            break;
        case OPERAND_VARIABLE:
            operand = WORLD_VARIABLE(data[4])->value;
            TRIGGER_COMPARE_RETURN(data[0], value, operand);
            break;
        case OPERAND_CODE_VARIABLE:
            operand = GetCodeVariable(data[4], data[5]);
            TRIGGER_COMPARE_RETURN(data[0], value, operand);
            break;
        }
    }
    return 0;
}

/* Item picked up / dropped triggers: param { numItems, QuadItem handles,
 * numCars, TileCar handles }; fire when one of the items and one of the cars
 * match. */
static inline void Trigger_ItemEvent(u32 type, void *vehicle, void *item) {
    u32 i;
    Trigger *trigger;
    u32 j;
    u32 *list;
    s8 foundItem;
    s8 foundVehicle;
    u32 count;
    u32 count2;

    if (!TAC_PAUSED) {
        for (i = 0; i < TRIGGER_STATS[type].count; i++) {
            trigger = TRIGGER_LISTS[type][i];
            list = GETP(trigger->param);
            count = list[0];
            count2 = list[count + 1];
            if (trigger->state != TRIGGER_STATE_DONE) {
                foundItem = 0;
                foundVehicle = 0;
                for (j = 0; j < count && !foundItem; j++) {
                    if ((void *)GETP(TRIGGER_RECORD(QuadItem, list[1 + j])->thing) == item) {
                        foundItem = 1;
                    }
                }
                if (foundItem) {
                    for (j = 0; j < count2 && !foundVehicle; j++) {
                        if ((void *)GETP(TRIGGER_RECORD(TileCar, list[2 + count + j])->car) == vehicle) {
                            foundVehicle = 1;
                        }
                    }
                    if (foundVehicle) {
                        Trigger_FireEvent(trigger);
                    }
                }
            }
        }
    }
}

void Trigger_ItemPickedUp(void *vehicle, void *item) {
    Trigger_ItemEvent(TRIGGER_ITEM_PICKUP, vehicle, item);
}

void Trigger_ItemDropped(void *vehicle, void *item) {
    Trigger_ItemEvent(TRIGGER_ITEM_DROPPED, vehicle, item);
}

s32 TriggerInterfaceEnd(void) {
    return 1;
}

/* Interface `id` ended: fires the InterfaceEnd triggers with param { id }. */
void Trigger_InterfaceEnd(u32 id) {
    u32 i;

    if (!TAC_PAUSED) {
        for (i = 0; i < TRIGGER_STATS[TRIGGER_INTERFACE_END].count; i++) {
            Trigger *trigger = TRIGGER_LISTS[TRIGGER_INTERFACE_END][i];
            u32 param = GETP(trigger->param)[0];
            if (trigger->state != TRIGGER_STATE_DONE && param == id) {
                Trigger_FireEvent(trigger);
            }
        }
    }
}

/* data: { count, TileCar handles }: true if one of the cars is dead. */
s32 TriggerVehicleKilled(u32 *data) {
    u32 i;

    for (i = 0; i < data[0]; i++) {
        Thing *vehicle = (Thing *)GETP(TRIGGER_RECORD(TileCar, data[1 + i])->car);
        if (vehicle != NULL && (vehicle->flags & 2)) {   /* Thing flag 2: dead */
            return 1;
        }
    }
    return 0;
}

/* param { count, TileCar handles }. */
void Trigger_VehicleKilled(void *vehicle) {
    u32 i;
    Trigger *trigger;
    u32 j;
    s8 found;
    u32 count;

    if (!TAC_PAUSED) {
        for (i = 0; i < TRIGGER_STATS[TRIGGER_VEHICLE_KILLED].count; i++) {
            trigger = TRIGGER_LISTS[TRIGGER_VEHICLE_KILLED][i];
            count = GETP(trigger->param)[0];
            if (trigger->state != TRIGGER_STATE_DONE) {
                found = 0;
                for (j = 0; j < count && !found; j++) {
                    if ((void *)GETP(TRIGGER_RECORD(TileCar, GETP(trigger->param)[1 + j])->car) == vehicle) {
                        found = 1;
                    }
                }
                if (found) {
                    Trigger_FireEvent(trigger);
                }
            }
        }
    }
}

/* data: { count, QuadInstance handles }: true if one was destroyed. */
s32 TriggerDesructibleKilled(u32 *data) {
    u32 i;

    for (i = 0; i < data[0]; i++) {
        if (TRIGGER_RECORD(QuadInstance, data[1 + i])->mode == QUAD_OBJECT_DESTROYED) {
            return 1;
        }
    }
    return 0;
}

/* param { count, QuadInstance handles, how }: `how` 3 fires on any kill, 2
 * when the thing doesn't have flag 0x20, other values when it does. */
void Trigger_DestructibleKilled(void *arg) {
    Thing *thing = arg;
    u32 i;
    Trigger *trigger;
    u32 j;
    u32 *list;
    s8 found;
    u32 count;
    u32 mode;

    if (!TAC_PAUSED) {
        for (i = 0; i < TRIGGER_STATS[TRIGGER_DESTRUCTIBLE_KILLED].count; i++) {
            trigger = TRIGGER_LISTS[TRIGGER_DESTRUCTIBLE_KILLED][i];
            list = GETP(trigger->param);
            count = list[0];
            if (trigger->state != TRIGGER_STATE_DONE) {
                found = 0;
                for (j = 0; j < count; j++) {
                    if (GETP(TRIGGER_RECORD(QuadInstance, list[1 + j])->thing) == thing) {
                        mode = list[count + 1];
                        if (mode == 3) {
                            found = 1;
                        } else if (mode == 2) {
                            found = !(thing->flags & 0x20);
                        } else {
                            found = thing->flags & 0x20;
                        }
                        break;
                    }
                }
                if (found) {
                    Trigger_FireEvent(trigger);
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

/* param { numCreatures, TileCreature handles, numCars, TileCar handles }. */
void Trigger_CreatureDroppedOff(void *vehicle, void *creature) {
    u32 i;
    Trigger *trigger;
    u32 j;
    u32 *list;
    s8 foundCreature;
    s8 foundVehicle;
    u32 count;
    u32 count2;

    if (!TAC_PAUSED) {
        for (i = 0; i < TRIGGER_STATS[TRIGGER_CREATURE_DROPPED_OFF].count; i++) {
            trigger = TRIGGER_LISTS[TRIGGER_CREATURE_DROPPED_OFF][i];
            list = GETP(trigger->param);
            count = list[0];
            count2 = list[count + 1];
            if (trigger->state != TRIGGER_STATE_DONE) {
                foundCreature = 0;
                foundVehicle = 0;
                for (j = 0; j < count && !foundCreature; j++) {
                    if (PTR(TRIGGER_RECORD(TileCreature, list[1 + j])->thing) == creature) {
                        foundCreature = 1;
                    }
                }
                if (foundCreature) {
                    for (j = 0; j < count2 && !foundVehicle; j++) {
                        if ((void *)GETP(TRIGGER_RECORD(TileCar, list[2 + count + j])->car) == vehicle) {
                            foundVehicle = 1;
                        }
                    }
                    if (foundVehicle) {
                        Trigger_FireEvent(trigger);
                    }
                }
            }
        }
    }
}

/* Fires the active triggers of an event type that has no parameters. */
static inline void Trigger_FireAll(u32 type) {
    u32 i;

    if (!TAC_PAUSED) {
        for (i = 0; i < TRIGGER_STATS[type].count; i++) {
            Trigger *trigger = TRIGGER_LISTS[type][i];
            if (trigger->state != TRIGGER_STATE_DONE) {
                Trigger_FireEvent(trigger);
            }
        }
    }
}

void Trigger_GameRoundsComplete(void) {
    Trigger_FireAll(TRIGGER_GAME_ROUNDS_COMPLETE);
}

void Trigger_GameRoundXComplete(void) {
    Trigger_FireAll(TRIGGER_GAME_ROUND_X_COMPLETE);
}

void Trigger_SoundSequenceQueueEmptied(void) {
    Trigger_FireAll(TRIGGER_SOUND_SEQ_QUEUE_END);
}
