/* The level scripting system ("TAC": triggers, actions, conditions;
 * Trigger.c, Action.c and the Trigger* / Action* functions elsewhere).
 *
 * A level pack holds an array of Trigger records (DataPack.triggers). Each
 * frame Trigger_Heartbeat polls the active triggers of the polled types; the
 * event types fire from Trigger_ItemPickedUp, Trigger_VehicleKilled, ...
 * When a trigger's test and its conditions hold, its actions are queued
 * (Action_AddAction) and run after their delay by Action_Heartbeat.
 *
 * Trigger and action parameters are type-specific word lists in the pack;
 * most start with a count followed by handles of world records (TileCar,
 * QuadItem, WorldVariable... see World.h). Offsets are the GameCube layout. */
#ifndef TRIGGER_H
#define TRIGGER_H

#include <stddef.h>

/* Trigger.type: index into pTriggerFuncArray and TriggerStats. */
enum {
    TRIGGER_TRUE = 0,
    TRIGGER_AIRBOX_ENTERED = 1,
    TRIGGER_AIRBOX_EXITED = 2,
    TRIGGER_AIRBOX_STOP_IN = 3,
    TRIGGER_DESTRUCTIBLE_KILLED = 4,
    TRIGGER_CREATURE_KILLED = 5,
    TRIGGER_ITEM_PICKUP = 6,
    TRIGGER_INTERFACE_END = 7,
    TRIGGER_LEVEL_START = 8,
    TRIGGER_TIMER_VALUE_CHECK = 9,
    TRIGGER_MOVEABLE_END = 10,
    TRIGGER_VARIABLE_VALUE_CHECK = 11,
    TRIGGER_VEHICLE_KILLED = 12,
    TRIGGER_P2P_ACTIVATED = 13,
    TRIGGER_CREATURE_DROPPED_OFF = 14,
    TRIGGER_INTERFACE_PACK_END = 15,
    TRIGGER_VEHICLE_DAMAGE = 16,
    TRIGGER_PLAYER_VEHICLE_GEAR = 17,
    TRIGGER_PLAYER_VEHICLE_IN_AIR = 18,
    TRIGGER_ITEM_VOLUME_ENTERED = 19,
    TRIGGER_CODE_VARIABLE_VALUE_CHECK = 20,
    TRIGGER_CREATURE_PICKED_UP = 21,
    TRIGGER_GAME_ROUNDS_COMPLETE = 22,
    TRIGGER_GAME_ROUND_X_COMPLETE = 23,
    TRIGGER_ITEM_DROPPED = 24,
    TRIGGER_MOVABLE_VOLUME_ENTERED = 25,
    TRIGGER_SOUND_SEQ_QUEUE_END = 26,
    TRIGGER_CREATURE_VOLUME_ENTERED = 27,
    TRIGGER_NUM_TYPES = 28,
};

/* Trigger.state bits */
enum {
    TRIGGER_STATE_ARMED = 0x1,  /* fires when its test becomes true */
    TRIGGER_STATE_DONE = 0x2,   /* actions used up: never fires again */
    TRIGGER_STATE_FIRED = 0x10, /* re-armed once its test is false again */
};

/* Trigger.mode: how the conditions combine (TriggerCheckConditions). */
enum {
    TRIGGER_COND_ALL = 0,
    TRIGGER_COND_ANY = 1,
    TRIGGER_COND_NOT_ALL = 2,
    TRIGGER_COND_NONE = 3,
};

/* An action record. The pack's record is queued itself, so `count` and
 * `delay` change as it runs (Trigger_SaveActions/RestoreActions keep the
 * counts across a round reset). */
typedef struct TriggerAction {
    u32 type;                   /* 0x00 index into pActionFuncArray */
    PTR32(void) data;           /* 0x04 parameters */
    u32 unk8;                   /* 0x08 */
    s32 delayInit;              /* 0x0C ms */
    u32 count;                  /* 0x10 times it can still run, 0xFFFFFFFF: always */
    s32 delay;                  /* 0x14 ms left before it runs */
} TriggerAction;

_Static_assert(offsetof(TriggerAction, count) == 0x10, "TriggerAction.count");
_Static_assert(sizeof(TriggerAction) == 0x18, "TriggerAction size");

/* A trigger; also the record of a condition (only type/param are used). */
typedef struct Trigger {
    u32 type;                   /* 0x00 TRIGGER_* */
    PTR32(u32) param;           /* 0x04 parameters, passed to the test */
    u8 pad8[4];                 /* 0x08 */
    PTR32(struct Trigger) conditions; /* 0x0C */
    u32 numConditions;          /* 0x10 */
    PTR32(TriggerAction) actions;     /* 0x14 */
    PTR32(TriggerAction) elseActions; /* 0x18 PlayerVehicleInAir: run on landing */
    u32 numActions;             /* 0x1C */
    u32 numElseActions;         /* 0x20 */
    u32 state;                  /* 0x24 TRIGGER_STATE_* */
    u8 mode;                    /* 0x28 TRIGGER_COND_* */
    u8 pad29[3];                /* 0x29 */
} Trigger;

_Static_assert(offsetof(Trigger, conditions) == 0x0C, "Trigger.conditions");
_Static_assert(offsetof(Trigger, actions) == 0x14, "Trigger.actions");
_Static_assert(offsetof(Trigger, numActions) == 0x1C, "Trigger.numActions");
_Static_assert(offsetof(Trigger, state) == 0x24, "Trigger.state");
_Static_assert(offsetof(Trigger, mode) == 0x28, "Trigger.mode");
_Static_assert(sizeof(Trigger) == 0x2C, "Trigger size");

/* TriggerStats[type]: the active triggers of a type and the room for them. */
typedef struct TriggerStat {
    u32 count;                  /* 0x00 entries used in m_Triggers[type] */
    u32 max;                    /* 0x04 */
    u32 flags;                  /* 0x08 see TRIGGER_STAT_EVENT */
} TriggerStat;

/* Set for the event-driven types (vehicle killed, item picked up...), which
 * fire from their events and are not polled. The GC reads the first byte of
 * the word; data_gen.c stores it as a host word, so that's bits 24-31. */
#define TRIGGER_STAT_EVENT(stat) ((s8)((stat)->flags >> 24))

/* Comparison operators of the value checks (TriggerVariableValueCheck...). */
enum {
    TRIGGER_OP_LESS = 20,
    TRIGGER_OP_GREATER = 21,
    TRIGGER_OP_EQUAL = 22,
    TRIGGER_OP_GREATER_EQUAL = 47,
    TRIGGER_OP_LESS_EQUAL = 48,
};

/* Arithmetic operators of the value-changing actions. */
enum {
    ACTION_OP_ADD = 15,
    ACTION_OP_SUB = 16,
    ACTION_OP_SET = 17,
    ACTION_OP_MUL = 45,
    ACTION_OP_DIV = 46,
};

/* Where an operand comes from: an immediate float, a WorldVariable, or a
 * code variable (GetCodeVariable(var, arg), two more words). */
enum {
    OPERAND_CONSTANT = 0,
    OPERAND_VARIABLE = 1,
    OPERAND_CODE_VARIABLE = 2,
};

/* pActionFuncArray index of ActionPrintDebugString, which stays queued while
 * its countdown (data word 1) runs. */
#define ACTION_PRINT_DEBUG_STRING 0x14

#endif
