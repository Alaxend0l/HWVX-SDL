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

/* Trigger.type: index into pTriggerFuncArray and TriggerStats. Named as the
 * game's own triggerLabelsArray ("Trigger Airbox Entered"...) and the test
 * functions' symbols. */
enum TriggerType {
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
enum TriggerState {
    TRIGGER_STATE_ARMED = 0x1,  /* fires when its test becomes true */
    TRIGGER_STATE_DONE = 0x2,   /* actions used up: never fires again */
    TRIGGER_STATE_FIRED = 0x10, /* re-armed once its test is false again */
};

/* Trigger.mode: how the conditions combine (TriggerCheckConditions). */
enum TriggerCond {
    TRIGGER_COND_ALL = 0,
    TRIGGER_COND_ANY = 1,
    TRIGGER_COND_NOT_ALL = 2,
    TRIGGER_COND_NONE = 3,
};

/* TriggerAction.type: index into pActionFuncArray (data_gen.c), named after
 * its functions' symbols (ActionAirboxChangeMode...). */
enum ActionType {
    ACTION_AIRBOX_CHANGE_MODE = 0,
    ACTION_CAMERA_SET_TARGETS = 1,
    ACTION_DESTRUCTIBLE_CHANGE_MODE = 2,
    ACTION_FX_POINT_CHANGE_MODE = 3,
    ACTION_INTERFACE_PLAY = 4,
    ACTION_ITEM_CHANGE_MODE = 5,
    ACTION_END_LEVEL = 6,
    ACTION_TIMER_CHANGE_VALUE = 7,
    ACTION_TIMER_CHANGE_MODE = 8,
    ACTION_MOVEABLE_CHANGE_MODE = 9,
    ACTION_MUSIC_CHANGE = 10,
    ACTION_MUSIC_SET_SONG = 11,
    ACTION_NAV_POINT_CHANGE_MODE = 12,
    ACTION_PAUSE_GAME = 13,
    ACTION_SPEAKER_CHANGE_MODE = 14,
    ACTION_TARGET_ARROW_SET_TARGET = 15,
    ACTION_VARIABLE_CHANGE_VALUE = 16,
    ACTION_VEHICLE_CHANGE_MODE = 17,
    ACTION_VEHICLE_AI_SET_NAMED_PATH = 18,
    ACTION_VERTEX_COLOR_SEQ_CHANGE_MODE = 19,
    ACTION_PRINT_DEBUG_STRING = 20,     /* stays queued while its countdown (data word 1) runs */
    ACTION_VEHICLE_TELEPORT = 21,
    ACTION_VEHICLE_CHANGE_CONTROL = 22,
    ACTION_SET_PLAYER_VEHICLE = 23,
    ACTION_VEHICLE_REMOVE_ITEM = 24,
    ACTION_VEHICLE_REMOVE_CREATURE = 25,
    ACTION_VARIABLE_CHANGE_MODE = 26,
    ACTION_LOAD_INTERFACE_PACK = 27,
    ACTION_PLAY_INTERFACE_PACK = 28,
    ACTION_UNLOAD_INTERFACE_PACK = 29,
    ACTION_END_INTERFACE_PACK = 30,
    ACTION_END_INTERFACE = 31,
    ACTION_HUD_CHANGE_MODE = 32,
    ACTION_SCRIPT_LIGHT_CHANGE_MODE = 33,
    ACTION_CODE_VARIABLE_MODIFY_VALUE = 34,
    ACTION_MISSION_OBJ_CHANGE_MODE = 35,
    ACTION_PAUSE_TAC_SYSTEM = 36,
    ACTION_SWAP_AI_SCRIPT = 37,
    ACTION_RESET_GAME_ROUND = 38,
    ACTION_ROUND_COMPLETE = 39,
    ACTION_VEHICLE_GIVE_ITEM = 40,
    ACTION_SET_PLAYER_ORDER = 41,
    ACTION_PLAY_SOUND_SEQUENCE = 42,
    ACTION_CREATURE_CHANGE_MODE = 43,
    ACTION_NUM_TYPES = 44,
};

/* An action record. The pack's record is queued itself, so `count` and
 * `delay` change as it runs (Trigger_SaveActions/RestoreActions keep the
 * counts across a round reset). */
typedef struct TriggerAction {
    u32 type;                   /* 0x00 index into pActionFuncArray @xdl: enum=ActionType */
    PTR32(void) data;           /* 0x04 parameters (TAC_PARAMS_ACTION_* below) @xdl: params=type */
    u32 unused8;                /* 0x08 never read; 1-135, grows with the parameter list (the
                                 *      exporter's parameter count?) */
    s32 delayInit;              /* 0x0C ms @xdl: units=ms */
    u32 count;                  /* 0x10 times it can still run, 0xFFFFFFFF: always */
    s32 delay;                  /* 0x14 ms left before it runs; = delayInit in every pack @xdl: units=ms */
} TriggerAction;

/* A trigger; also the record of a condition (only type/param are used). */
typedef struct Trigger {
    u32 type;                   /* 0x00 TRIGGER_* @xdl: enum=TriggerType */
    PTR32(u32) param;           /* 0x04 parameters, passed to the test (TAC_PARAMS_TRIGGER_*
                                 *      below) @xdl: params=type */
    u32 unused8;                /* 0x08 never read; 0-452, grows with the parameter list (as
                                 *      TriggerAction.unused8) */
    PTR32(struct Trigger) conditions; /* 0x0C @xdl: count=numConditions; inline */
    u32 numConditions;          /* 0x10 */
    PTR32(TriggerAction) actions;     /* 0x14 @xdl: count=numActions; inline */
    PTR32(TriggerAction) elseActions; /* 0x18 PlayerVehicleInAir: run on landing @xdl: count=numElseActions; inline */
    u32 numActions;             /* 0x1C */
    u32 numElseActions;         /* 0x20 */
    u32 state;                  /* 0x24 TRIGGER_STATE_*, set at run time; 0 in every pack
                                 *      @xdl: flags=TriggerState */
    u8 mode;                    /* 0x28 TRIGGER_COND_* @xdl: enum=TriggerCond */
    u8 pad29[3];                /* 0x29 0 in every pack; never read */
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

/* The keywords of the parameter lists: one numbering for every mode, operator
 * and option word the exporter writes (a comparison is 20-22 and 47-48, an
 * arithmetic operator 15-17 and 45-46, wherever it appears). Named from the
 * functions that test them; the gaps are values no function tests and no
 * retail pack holds. */
enum TacToken {
    TAC_OFF = 0,                /* airboxes, fx points, items, vertex colours, creatures; vehicles:
                                 *      remove the car; destructibles: hide */
    TAC_ON = 1,                 /* the same, back on; vehicles: spawn */
    TAC_TOGGLE = 2,             /* also PauseTACSystem: toggle the pause */
    TAC_KILL = 3,               /* destroy (destructible, vehicle, creature, mover) */
    TAC_SET_SPEED = 6,          /* movers: speed from the list; position: unchanged */
    TAC_PAUSE = 7,              /* PauseGame, PauseTACSystem; timers: stopped */
    TAC_RESUME = 8,             /* PauseGame, PauseTACSystem; timers: running */
    TAC_AI_HOLD = 9,            /* vehicles: the AI player's flag 1 (no driving) */
    TAC_SPAWN = 10,             /* vehicles: as on */
    TAC_START = 11,             /* movers: run at the list's speed */
    TAC_STOP = 12,              /* movers: stop */
    TAC_TO_START = 13,          /* movers: back to the track's start */
    TAC_TO_END = 14,            /* movers: to the track's end */
    TAC_ADD = 15,
    TAC_SUBTRACT = 16,
    TAC_SET = 17,
    TAC_LESS = 20,
    TAC_GREATER = 21,
    TAC_EQUAL = 22,
    TAC_SHOW = 23,              /* timers, variables, objectives: on the HUD (InterfaceManager) */
    TAC_HIDE = 24,
    TAC_REMOVE = 25,            /* destructibles, creatures: released without dying */
    TAC_KILL_HIDDEN = 26,       /* destructibles: destroyed and hidden; creatures: killed, kept spawnable */
    TAC_LOCK_CONTROL = 34,      /* VehicleChangeControl: drivers lose control */
    TAC_UNLOCK_CONTROL = 35,    /* ... and get it back (any other value) */
    TAC_ORDER_LOOP = 39,        /* MusicChange orderType: SetMusicOrder's loop flag */
    TAC_ORDER_JUMP = 41,        /* MusicChange orderMode: jump to `order` */
    TAC_DAMAGE_LOST = 42,       /* VehicleDamage: lost more than `amount` since the last test */
    TAC_DAMAGE_BELOW = 43,      /* VehicleDamage: health below max / amount */
    TAC_MULTIPLY = 45,
    TAC_DIVIDE = 46,
    TAC_GREATER_EQUAL = 47,
    TAC_LESS_EQUAL = 48,
};

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
enum TacOperand {
    OPERAND_CONSTANT = 0,
    OPERAND_VARIABLE = 1,
    OPERAND_CODE_VARIABLE = 2,
};


/* ---- parameter lists ------------------------------------------------------
 * What Trigger.param and TriggerAction.data point to, per kind: word lists
 * the functions index (data[0], data[data[0] + 1]...). Written here for
 * tools/gen_schemas.py, which turns them into the tagged union of
 * schemas/Trigger.schema.json; C never expands these macros. Items, in order:
 *
 *   U32(name) S32(name) F32(name)  one word
 *   TOKEN(name)        a TacToken word
 *   ANY(name)          a word that is a handle (a link) in some packs, a number in others
 *   LINK(name, T)      a handle of a T world record, 0: none (T: void, anything)
 *   LIST(name, T)      a count, then that many handles of T records
 *   OPERAND(name)      a TacOperand word, then an f32, a WorldVariableSlot handle, or a code
 *                      variable's id and argument
 *   CODEVAR(name)      a code variable id; below 100 a LIST of TileCar handles follows, else
 *                      one argument word (GetCodeVariable)
 *   SCRATCH(name, L)   as many run-time words as LIST L has handles (0 in the pack)
 *   STRING(name)       a NUL-terminated name, padded to a word
 *   TABLE(name, R)     a count, then that many TAC_RECORD_R records
 *   WORDS(name)        the rest of the list, not decoded
 *
 * A record's handle is the address of its entry in the world's tables (World.h:
 * TileCar, QuadInstance, WorldVolume...; variables and timers by their table
 * slot). Counts and values: docs/fields (objects.md). */

#define TAC_PARAMS_TRIGGER_TRUE
#define TAC_PARAMS_TRIGGER_AIRBOX_ENTERED           LIST(volumes, WorldVolume) LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_AIRBOX_EXITED            LIST(volumes, WorldVolume) LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_AIRBOX_STOP_IN           LIST(volumes, WorldVolume) LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_DESTRUCTIBLE_KILLED      LIST(instances, QuadInstance) U32(how)
#define TAC_PARAMS_TRIGGER_CREATURE_KILLED          WORDS(words)
#define TAC_PARAMS_TRIGGER_ITEM_PICKUP              LIST(items, QuadItem) LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_INTERFACE_END            LINK(interface, void)
#define TAC_PARAMS_TRIGGER_LEVEL_START
#define TAC_PARAMS_TRIGGER_TIMER_VALUE_CHECK        LIST(timers, WorldTimerSlot) OPERAND(value) TOKEN(op)
#define TAC_PARAMS_TRIGGER_MOVEABLE_END             WORDS(words)
#define TAC_PARAMS_TRIGGER_VARIABLE_VALUE_CHECK     LIST(variables, WorldVariableSlot) OPERAND(value) TOKEN(op)
#define TAC_PARAMS_TRIGGER_VEHICLE_KILLED           LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_P2P_ACTIVATED            WORDS(words)
#define TAC_PARAMS_TRIGGER_CREATURE_DROPPED_OFF     LIST(creatures, TileCreature) LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_INTERFACE_PACK_END       U32(script) STRING(pack)
#define TAC_PARAMS_TRIGGER_VEHICLE_DAMAGE           LIST(cars, TileCar) TOKEN(test) S32(amount) SCRATCH(saved, cars)
#define TAC_PARAMS_TRIGGER_PLAYER_VEHICLE_GEAR      U32(unused) U32(gear)
#define TAC_PARAMS_TRIGGER_PLAYER_VEHICLE_IN_AIR    U32(unused) S32(time) U32(inAir)
#define TAC_PARAMS_TRIGGER_ITEM_VOLUME_ENTERED      LIST(volumes, WorldVolume) LIST(items, QuadItem)
#define TAC_PARAMS_TRIGGER_CODE_VARIABLE_VALUE_CHECK TOKEN(op) CODEVAR(var) OPERAND(value)
#define TAC_PARAMS_TRIGGER_CREATURE_PICKED_UP       WORDS(words)
#define TAC_PARAMS_TRIGGER_GAME_ROUNDS_COMPLETE
#define TAC_PARAMS_TRIGGER_GAME_ROUND_X_COMPLETE
#define TAC_PARAMS_TRIGGER_ITEM_DROPPED             LIST(items, QuadItem) LIST(cars, TileCar)
#define TAC_PARAMS_TRIGGER_MOVABLE_VOLUME_ENTERED   LIST(volumes, WorldVolume) LIST(paths, TilePath)
#define TAC_PARAMS_TRIGGER_SOUND_SEQ_QUEUE_END
#define TAC_PARAMS_TRIGGER_CREATURE_VOLUME_ENTERED  LIST(volumes, WorldVolume) LIST(creatures, TileCreature)

#define TAC_PARAMS_ACTION_AIRBOX_CHANGE_MODE        TOKEN(mode) LIST(volumes, WorldVolume)
#define TAC_PARAMS_ACTION_CAMERA_SET_TARGETS        U32(player) WORDS(camera)
#define TAC_PARAMS_ACTION_DESTRUCTIBLE_CHANGE_MODE  TOKEN(mode) LIST(instances, QuadInstance)
#define TAC_PARAMS_ACTION_FX_POINT_CHANGE_MODE      TOKEN(mode) LIST(points, QuadFxPoint)
#define TAC_PARAMS_ACTION_INTERFACE_PLAY            LINK(interface, void)
#define TAC_PARAMS_ACTION_ITEM_CHANGE_MODE          TOKEN(mode) LIST(items, QuadItem)
#define TAC_PARAMS_ACTION_END_LEVEL                 S32(result) S32(arg)
#define TAC_PARAMS_ACTION_TIMER_CHANGE_VALUE        TOKEN(op) LIST(timers, WorldTimerSlot) OPERAND(value)
#define TAC_PARAMS_ACTION_TIMER_CHANGE_MODE         TOKEN(show) TOKEN(run) LIST(timers, WorldTimerSlot)
#define TAC_PARAMS_ACTION_MOVEABLE_CHANGE_MODE      TOKEN(speedMode) TOKEN(posMode) U32(speed) U32(unused) LIST(paths, TilePath)
#define TAC_PARAMS_ACTION_MUSIC_CHANGE              F32(vol) F32(volTime) F32(pitch) F32(pitchTime) F32(tempo) F32(tempoTime) TOKEN(orderMode) U32(order) TOKEN(orderType) TABLE(tracks, MUSIC_TRACK)
#define TAC_PARAMS_ACTION_MUSIC_SET_SONG            WORDS(words)
#define TAC_PARAMS_ACTION_NAV_POINT_CHANGE_MODE     U32(enabled) LIST(points, NavPoint)
#define TAC_PARAMS_ACTION_PAUSE_GAME                TOKEN(mode)
#define TAC_PARAMS_ACTION_SPEAKER_CHANGE_MODE       WORDS(words)
#define TAC_PARAMS_ACTION_TARGET_ARROW_SET_TARGET   U32(type) LINK(target, void)
#define TAC_PARAMS_ACTION_VARIABLE_CHANGE_VALUE     TOKEN(op) LIST(variables, WorldVariableSlot) OPERAND(value)
#define TAC_PARAMS_ACTION_VEHICLE_CHANGE_MODE       TOKEN(mode) LIST(cars, TileCar)
#define TAC_PARAMS_ACTION_VEHICLE_AI_SET_NAMED_PATH WORDS(words)
#define TAC_PARAMS_ACTION_VERTEX_COLOR_SEQ_CHANGE_MODE TOKEN(mode) LIST(sequences, void)
#define TAC_PARAMS_ACTION_PRINT_DEBUG_STRING        U32(what) U32(countdown) ANY(value)
#define TAC_PARAMS_ACTION_VEHICLE_TELEPORT          U32(unused) LINK(teleporter, Teleporter) LIST(cars, TileCar)
#define TAC_PARAMS_ACTION_VEHICLE_CHANGE_CONTROL    TOKEN(mode) U32(unused) LIST(cars, TileCar)
#define TAC_PARAMS_ACTION_SET_PLAYER_VEHICLE        WORDS(words)
#define TAC_PARAMS_ACTION_VEHICLE_REMOVE_ITEM       U32(what) LIST(cars, TileCar)
#define TAC_PARAMS_ACTION_VEHICLE_REMOVE_CREATURE   WORDS(words)
#define TAC_PARAMS_ACTION_VARIABLE_CHANGE_MODE      TOKEN(mode) LIST(variables, WorldVariableSlot)
#define TAC_PARAMS_ACTION_LOAD_INTERFACE_PACK       U32(script) STRING(pack)
#define TAC_PARAMS_ACTION_PLAY_INTERFACE_PACK       U32(script) STRING(pack)
#define TAC_PARAMS_ACTION_UNLOAD_INTERFACE_PACK     U32(script) STRING(pack)
#define TAC_PARAMS_ACTION_END_INTERFACE_PACK        U32(script) STRING(pack)
#define TAC_PARAMS_ACTION_END_INTERFACE             S32(all) LINK(interface, void) ANY(later)
#define TAC_PARAMS_ACTION_HUD_CHANGE_MODE           U32(unused) U32(on)
#define TAC_PARAMS_ACTION_SCRIPT_LIGHT_CHANGE_MODE  WORDS(words)
#define TAC_PARAMS_ACTION_CODE_VARIABLE_MODIFY_VALUE TOKEN(op) CODEVAR(var) OPERAND(value)
#define TAC_PARAMS_ACTION_MISSION_OBJ_CHANGE_MODE   TOKEN(mode) LIST(objectives, WorldObjective)
#define TAC_PARAMS_ACTION_PAUSE_TAC_SYSTEM          TOKEN(mode)
#define TAC_PARAMS_ACTION_SWAP_AI_SCRIPT            ANY(easy) ANY(medium) ANY(hard) ANY(expert) LIST(cars, TileCar)
#define TAC_PARAMS_ACTION_RESET_GAME_ROUND          U32(resetMotion) U32(resetPosition) U32(jettison)
#define TAC_PARAMS_ACTION_ROUND_COMPLETE            U32(player)
#define TAC_PARAMS_ACTION_VEHICLE_GIVE_ITEM         U32(what) ANY(item) LIST(cars, TileCar)
#define TAC_PARAMS_ACTION_SET_PLAYER_ORDER          LINK(first, TileCar) LINK(second, TileCar) LINK(third, TileCar) LINK(fourth, TileCar)
#define TAC_PARAMS_ACTION_PLAY_SOUND_SEQUENCE       U32(play) U32(queued) ANY(sequence)
#define TAC_PARAMS_ACTION_CREATURE_CHANGE_MODE      TOKEN(mode) LIST(creatures, TileCreature)

/* A MusicChange track (ActionMusicChange reads track, active and, past 0,
 * vol; the rest are never read). */
#define TAC_RECORD_MUSIC_TRACK  U32(unused0) U32(track) U32(active) F32(vol) U32(unused10) U32(unused14) U32(unused18) U32(unused1C) U32(unused20) U32(unused24)

#endif
