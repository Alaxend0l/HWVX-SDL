/* Interfaces: menus, HUDs, briefings (InterfaceManager.c).
 *
 * An interface is described by an InterfaceHeader in a data pack: a list of
 * object definitions (IfaceObjectDef, 0x6C each), input events, variables
 * and sounds. InterfaceManager_Create instantiates one into a free
 * g_interfaces[] entry (Interface, 0x12 of them) and gives it a block of
 * live objects (InterfaceObject, 0x300 each) from g_MasterInterfaceObjectList.
 *
 * Objects animate through key tracks (ExtractKeys.c) and per-state
 * animations: every object is in one of the IFACE_STATE_* states, each
 * state of its definition has an IfaceStateAnim. Selectable objects have a
 * record (IfaceRecord) with an id, neighbours for the d-pad and input events.
 * Interfaces talk to game code through a callback taking an IfaceMsg (the
 * same message as common.h's InterfaceMsg, which names only the fields the
 * vehicle select uses).
 *
 * The data structures are the big-endian pack layout, byte-swapped on load
 * by Port_Swap.c (Claim_IMHeader and friends): keep the field widths.
 * Offsets are the GameCube layout; pointer fields are PTR32 handles. */
#ifndef INTERFACE_H
#define INTERFACE_H

#include <stddef.h>
#include "World.h"

typedef struct Interface Interface;
typedef struct InterfaceObject InterfaceObject;
typedef struct InterfaceHeader InterfaceHeader;
typedef struct IfaceMsg IfaceMsg;
typedef struct InterfaceHudInfo InterfaceHudInfo;
struct Player;

/* ---- key animation (ExtractKeys.c) ------------------------------------- */

/* A key track { count; keys }; the key layout depends on the Extract*Key
 * function that plays it. */
typedef struct IfaceKeyTrack {
    u32 count;
    PTR32(void) keys;
} IfaceKeyTrack;

/* Playback state of a colour track (ExtractColorKey). */
typedef struct IfaceKeyColor {
    f32 slope[4];               /* 0x00 */
    f32 value[4];               /* 0x10 */
    f32 time;                   /* 0x20 */
    u8 rgba[4];                 /* 0x24 current colour, RGBA8 */
    u16 index;                  /* 0x28 */
    u16 current;                /* 0x2A */
    u8 reset;                   /* 0x2C 1: restart from the first key */
    u8 pad2D[3];
} IfaceKeyColor;

/* Playback state of a Vector4 track (ExtractScaleKey). 0x28 bytes; the
 * objects keep them 0x30 apart. */
typedef struct IfaceKeyVec4 {
    Vector4 v;                  /* 0x00 current value */
    f32 slope[3];               /* 0x10 */
    f32 time;                   /* 0x1C */
    u16 index;                  /* 0x20 */
    u16 current;                /* 0x22 */
    u8 reset;                   /* 0x24 */
    u8 pad25[3];
} IfaceKeyVec4;

/* Playback state of a UV track (ExtractUVKey): 4 corners. */
typedef struct IfaceKeyUV {
    f32 uv[8];                  /* 0x00 u0 v0 u1 v1 u2 v2 u3 v3 */
    u8 keyBytes[4];             /* 0x20 */
    f32 duration;               /* 0x24 */
    f32 slope[8];               /* 0x28 */
    u8 pad48[8];                /* 0x48 */
    f32 time;                   /* 0x50 */
    u16 index;                  /* 0x54 */
    u16 current;                /* 0x56 */
    u8 reset;                   /* 0x58 */
    u8 pad59[3];
} IfaceKeyUV;

/* Playback state of an on/off track (ExtractActiveKey). */
typedef struct IfaceKeyActive {
    f32 time;                   /* 0x00 */
    u16 index;                  /* 0x04 */
    u16 current;                /* 0x06 */
    u8 value;                   /* 0x08 1: the object is shown */
    u8 reset;                   /* 0x09 */
} IfaceKeyActive;

_Static_assert(sizeof(IfaceKeyColor) == 0x30, "IfaceKeyColor size");
_Static_assert(sizeof(IfaceKeyVec4) == 0x28, "IfaceKeyVec4 size");
_Static_assert(offsetof(IfaceKeyUV, reset) == 0x58, "IfaceKeyUV.reset");
_Static_assert(sizeof(IfaceKeyActive) == 0xC, "IfaceKeyActive size");

/* ---- interface definition (data pack) ---------------------------------- */

/* A value that is either a constant or one of the interface's variables
 * (conditions, actions, event arguments). */
typedef struct IfaceVarValue {
    s8 isVar;                   /* 0x0 value is a variable index */
    u8 var;                     /* 0x1 action type 3: the variable assigned */
    u16 value;                  /* 0x2 */
} IfaceVarValue;

/* A game setting (GameSettings_GetCodeVariable id) and its argument. */
typedef struct IfaceCodeVar {
    u32 id;                     /* 0x0 */
    PTR32(IfaceVarValue) arg;   /* 0x4 */
} IfaceCodeVar;

/* IfaceVar.type: where an interface variable's value comes from. */
enum {
    IFACE_VAR_CONST = 0,        /* `offset` */
    IFACE_VAR_CODE = 1,         /* a code variable, + offset, wrapped to min..max */
    IFACE_VAR_PLAYER = 2,       /* the owner's controller port, or g_playerIndex */
    IFACE_VAR_SELECTED = 3,     /* the selected object's record id */
    IFACE_VAR_WORLD = 4,        /* the world counter whose id is `offset` */
};

typedef struct IfaceVar {
    u8 type;                    /* 0x00 IFACE_VAR_* */
    u8 update;                  /* 0x01 re-read every frame */
    u8 save;                    /* 0x02 const: the final value is written back to `offset` */
    u8 pad3;
    s32 min;                    /* 0x04 */
    s32 max;                    /* 0x08 */
    s32 offset;                 /* 0x0C */
    PTR32(IfaceCodeVar) code;   /* 0x10 IFACE_VAR_CODE */
    u8 pad14[0x34 - 0x14];
} IfaceVar;

typedef struct IfaceVarList {
    u32 count;
    PTR32(IfaceVar) vars;
} IfaceVarList;

/* What an input event does when it fires (InterfaceManager_PlayEvent). */
typedef struct IfaceEventDef {
    PTR32(u16) target;          /* 0x00 InterfaceHeader to go to (its u16 id), or an action list (type 0xB) */
    u8 flags;                   /* 0x04 & 1: end the interface at once; & 2: next one starts active */
    u8 type;                    /* 0x05 */
    u16 id;                     /* 0x06 passed to the callback; type 2 (back): levels to go back */
    PTR32(void) args;           /* 0x08 table of IfaceVarValue slots (type 0x11: one IfaceVarValue) */
    u32 movie;                  /* 0x0C type 0x10 */
    u8 pad10[4];                /* 0x10 */
    PTR32(s8) sound;            /* 0x14 sound sequence */
    PTR32(u8) bytes;            /* 0x18 arguments for types 0xD, 0xE */
    u32 numMods;                /* 0x1C */
    PTR32(struct IfaceEventMod) mods; /* 0x20 */
    s32 delta;                  /* 0x24 types 0x16, 0x17: volume change */
} IfaceEventDef;

/* Variable change applied after an event. */
typedef struct IfaceEventMod {
    u8 var;                     /* 0x0 */
    u8 pad1[3];
    s32 delta;                  /* 0x4 */
} IfaceEventMod;

/* An input event: a button (or timer) and what it does. */
typedef struct IfaceEvent {
    u8 type;                    /* 0x0 trigger: button (ProcessInput), 0x1E: header timeout */
    u8 pad1[3];
    u32 delay;                  /* 0x4 ms: record events: animation delay; type 0x1E: timeout */
    PTR32(IfaceEventDef) def;   /* 0x8 */
} IfaceEvent;

typedef struct IfaceEventList {
    u32 count;
    PTR32(IfaceEvent) events;
} IfaceEventList;

/* Action list (InterfaceManager_ExecuteActions), 8 bytes per action. */
typedef struct IfaceAction {
    u8 type;                    /* 0x0 1: object state, 2: set code variable, 3: assign variable */
    u8 pad1[3];
    PTR32(void) data;           /* 0x4 IfaceStateAction / IfaceCodeAction / IfaceVarValue */
} IfaceAction;

typedef struct IfaceActionList {
    u32 count;
    PTR32(IfaceAction) actions;
} IfaceActionList;

/* Action type 1: put object `object` in state `state` (4: show, 5: hide). */
typedef struct IfaceStateAction {
    u8 state;                   /* 0x0 */
    u8 isConst;                 /* 0x1 10: `value` is a constant, else a variable index */
    u16 value;                  /* 0x2 */
    u32 object;                 /* 0x4 index in the interface */
} IfaceStateAction;

/* Action type 2: GameSettings_SetCodeVariable(code->id, code->arg, value). */
typedef struct IfaceCodeAction {
    PTR32(IfaceCodeVar) code;   /* 0x0 */
    PTR32(IfaceVarValue) value; /* 0x4 */
} IfaceCodeAction;

/* Object state animation: tracks played while in the state. */
typedef struct IfaceStateAnim {
    IfaceKeyTrack active;       /* 0x00 ExtractActiveKey */
    IfaceKeyTrack pos;          /* 0x08 */
    IfaceKeyTrack color;        /* 0x10 */
    IfaceKeyTrack scale;        /* 0x18 */
    IfaceKeyTrack uv;           /* 0x20 */
    IfaceKeyTrack rot;          /* 0x28 */
    PTR32(s8) sound;            /* 0x30 played on entering the state */
    IfaceActionList actions;    /* 0x34 run on entering the state */
} IfaceStateAnim;

/* IfaceStateDef.type / InterfaceObject.state */
enum {
    IFACE_STATE_OFF = 0,
    IFACE_STATE_ON = 1,         /* selectable, not selected */
    IFACE_STATE_ACTIVE = 2,     /* selected */
    IFACE_STATE_PRESSED = 3,    /* chosen: the interface waits `value` ms */
    IFACE_STATE_LOAD_IN = 4,
    IFACE_STATE_LOAD_OUT = 5,
    IFACE_STATE_HIDDEN = 100,   /* SetObjectState only: hide, no animation */
};

typedef struct IfaceStateDef {
    u8 type;                    /* 0x0 IFACE_STATE_* */
    u8 pad1[3];
    u32 value;                  /* 0x4 IFACE_STATE_PRESSED: ms; DropShip: camera distance */
    PTR32(IfaceStateAnim) anim; /* 0x8 */
} IfaceStateDef;

/* An object's default animation (AddObject). */
typedef struct IfaceObjAnim {
    IfaceKeyTrack pos;          /* 0x00 */
    IfaceKeyTrack rot;          /* 0x08 */
    IfaceKeyTrack scale;        /* 0x10 */
    IfaceKeyTrack color;        /* 0x18 */
    IfaceKeyTrack uv;           /* 0x20 */
} IfaceObjAnim;

/* d-pad step: how a selectable object changes variables. */
typedef struct IfaceStep {
    u32 var;                    /* 0x00 */
    s32 val[4];                 /* 0x04 up, down, left, right */
    u8 set[4];                  /* 0x14 1: assign val, 0: add it */
    u8 pad18[0x28 - 0x18];
} IfaceStep;

/* Selection record of an object (IfaceObjectDef.record). */
typedef struct IfaceRecord {
    u8 id;                      /* 0x00 selection id, 0: not selectable */
    u8 next[4];                 /* 0x01 id selected by up, down, left, right */
    u8 noPress;                 /* 0x05 0: an event on it shows IFACE_STATE_PRESSED (with def->pressable) */
    u8 pad6[2];
    u32 numSteps;               /* 0x08 */
    PTR32(IfaceStep) steps;     /* 0x0C */
    IfaceEventList events;      /* 0x10 */
} IfaceRecord;

/* Condition (InterfaceManager_CheckConditions). */
typedef struct IfaceCondition {
    u8 type;                    /* 0x00 1: variable == value, 2: code variable == value,
                                 *      3: owner's car flag 0x80, 4: wildcard count == value,
                                 *      5: no owner car */
    u8 pad1[3];
    s32 value;                  /* 0x04 */
    PTR32(void) data;           /* 0x08 IfaceVarValue (1), IfaceCodeVar (2) */
    u8 padC[0x1C - 0xC];
} IfaceCondition;

typedef struct IfaceConditionList {
    u8 type;                    /* 0x0 0: hide if all, 1: show if all, 2: show if any, 3: hide if any */
    u8 pad1[3];
    u32 count;                  /* 0x4 */
    PTR32(IfaceCondition) conds; /* 0x8 */
} IfaceConditionList;

/* IfaceObjectDef.type: what the definition creates (AddObject). */
enum {
    IFACE_DEF_IMAGE = 0,        /* IfaceImageDef sub */
    IFACE_DEF_TEXT = 1,
    IFACE_DEF_DIALOG = 2,       /* briefing dialogue */
    IFACE_DEF_DEBUG_VECTORS = 3,
    IFACE_DEF_POLY = 4,
    IFACE_DEF_CHILD = 6,        /* child interface */
    IFACE_DEF_CHILD_SYNC = 7,   /* child following the parent's load state */
    IFACE_DEF_BOX = 10,
    IFACE_DEF_LINE = 11,
    IFACE_DEF_DEBUG_TEXT = 12,
};

/* An object definition in an InterfaceHeader. */
typedef struct IfaceObjectDef {
    u8 role;                    /* 0x00 InterfaceObject.role */
    u8 type;                    /* 0x01 IFACE_DEF_* */
    u8 pad2[4];                 /* 0x02 */
    u8 pressable;               /* 0x06 see IfaceRecord.noPress */
    u8 pad7;
    u8 active;                  /* 0x08 can be selected */
    u8 input;                   /* 0x09 role 3 child: controller 1-4 */
    u16 id;                     /* 0x0A role-specific parameter (HUD slot, goal id...) */
    s16 anchor;                 /* 0x0C relative index of the object it is positioned at, 0: none */
    u16 unkE;                   /* 0x0E */
    u32 linkId;                 /* 0x10 InterfaceManager_LinkThingToObject; debug vectors: chain */
    PTR32(void) sub;            /* 0x14 type-specific data, see InterfaceObject.data */
    s32 stateIndex[6];          /* 0x18 first IfaceStateDef per IFACE_STATE_*, -1: none
                                 *      (written by SetObjectState) */
    f32 anchorX;                /* 0x30 -1..1: alignment of the object on its position */
    f32 anchorY;                /* 0x34 */
    u8 pad38[4];
    PTR32(PTR32(InterfaceHeader)) child; /* 0x3C slot holding the child interface */
    PTR32(u8) query;            /* 0x40 role 2: [0] != 0: use the callback's value */
    PTR32(IfaceRecord) record;  /* 0x44 */
    PTR32(IfaceConditionList) conditions; /* 0x48 */
    u8 pad4C[0x60 - 0x4C];
    PTR32(IfaceObjAnim) anim;   /* 0x60 */
    u32 numStates;              /* 0x64 */
    PTR32(IfaceStateDef) states; /* 0x68 */
} IfaceObjectDef;

/* Texture key animation (ExtractTextureKey), optionally indexed by a
 * variable instead (image animations). */
typedef struct IfaceTexAnim {
    TextureKeyAnim anim;        /* 0x0 */
    u8 fromVar;                 /* 0xC show key vars[var] instead of playing */
    u8 padD;
    u16 var;                    /* 0xE */
} IfaceTexAnim;

/* IFACE_DEF_IMAGE sub. */
typedef struct IfaceImageDef {
    u8 type;                    /* 0x0 0: texture `value`, 1: geometry `value`, 3: texture animation */
    u8 blendMode;               /* 0x1 */
    u8 pad2[2];
    u32 value;                  /* 0x4 */
    u8 pad8[8];
    PTR32(IfaceTexAnim) anim;   /* 0x10 type 3 */
} IfaceImageDef;

/* Typing click of a font (InterfaceManager_MoveObject). */
typedef struct IfaceTypeSound {
    u8 pad0[8];
    f32 volume;                 /* 0x08 */
    f32 pitchMin;               /* 0x0C */
    f32 pitchMax;               /* 0x10 */
    u32 sample;                 /* 0x14 */
} IfaceTypeSound;

/* A font (InterfaceManager_AddFont). */
typedef struct IfaceFont {
    u32 texture;                /* 0x00 */
    PTR32(IfaceTypeSound) typeSound; /* 0x04 */
    u8 pad8;
    u8 charWidth;               /* 0x09 */
    u8 charHeight;              /* 0x0A */
    u8 caseMode;                /* 0x0B */
    u8 padC[8];
    s32 typeDelay;              /* 0x14 ms per typed character, 0: show at once */
    IfaceKeyTrack size;         /* 0x18 text size (ExtractScaleKey; .x) */
    u32 numChars;               /* 0x20 */
    PTR32(u8) chars;            /* 0x24 0xC each: u8 char, f32 x, f32 y spacing */
    f32 spacing;                /* 0x28 */
} IfaceFont;

/* String list of a text (IfaceText.listMode 1). */
typedef struct IfaceStringList {
    u32 count;                  /* 0x0 */
    PTR32(u32) texts;           /* 0x4 slots of string tables */
    u8 fromVar;                 /* 0x8 pick by variable `var` */
    u8 pad9;
    u16 var;                    /* 0xA */
} IfaceStringList;

/* IFACE_DEF_TEXT / DEBUG_TEXT sub. */
typedef struct IfaceText {
    PTR32(PTR32(char)) strings; /* 0x00 table of string slots, [0] shown */
    PTR32(IfaceFont) font;      /* 0x04 */
    PTR32(IfaceStringList) list; /* 0x08 */
    u8 padC[2];
    u8 blend;                   /* 0x0E */
    u8 scaled;                  /* 0x0F scale by the object's scale */
    u8 listMode;                /* 0x10 1: text from `list` */
    u8 align;                   /* 0x11 */
    u8 paragraph;               /* 0x12 print as a wrapped paragraph */
    u8 pad13;
    u16 width;                  /* 0x14 paragraph box */
    u16 height;                 /* 0x16 */
    f32 lineSpacing;            /* 0x18 */
} IfaceText;

/* IFACE_DEF_BOX sub (debug grid). */
typedef struct IfaceBox {
    u32 width;                  /* 0x00 */
    u32 height;                 /* 0x04 */
    u32 xSteps;                 /* 0x08 */
    u32 ySteps;                 /* 0x0C */
    PTR32(IfaceFont) font;      /* 0x10 */
} IfaceBox;

/* IFACE_DEF_LINE sub (debug line). */
typedef struct IfaceLine {
    u8 type;                    /* 0x0 0: horizontal, 1: vertical, 2, 3: diagonals */
    u8 pad1[3];
    u32 step;                   /* 0x4 tick spacing, 0: none */
} IfaceLine;

/* IFACE_DEF_POLY sub. */
typedef struct IfacePolyPoint {
    f32 x;                      /* 0x00 */
    f32 y;                      /* 0x04 */
    u8 pad8[8];
    u8 rgba[4];                 /* 0x10 */
    u8 pad14[0xC];
} IfacePolyPoint;

typedef struct IfacePoly {
    u32 count;                  /* 0x0 */
    PTR32(IfacePolyPoint) points; /* 0x4 */
    u8 type;                    /* 0x8 line type */
} IfacePoly;

/* Briefing dialogue (IFACE_DEF_DIALOG; InterfaceManager_MoveDialogSystem). */

typedef struct IfaceDialogChar {
    s32 valueA;                 /* 0x00 shown by role 0x23 */
    s32 valueB;                 /* 0x04 shown by role 0x24 */
    PTR32(IfaceFont) font;      /* 0x08 */
    PTR32(u32) color;           /* 0x0C RGBA8 text colour */
    u32 numIdle;                /* 0x10 */
    PTR32(IfaceTexAnim) idle; /* 0x14 */
    u32 numTalk;                /* 0x18 */
    PTR32(IfaceTexAnim) talk; /* 0x1C */
    u8 state;                   /* 0x20 0: idle, 1: start, 2: talking, 3: done, 4: finishing, 5: special */
    u8 pad21[3];
    s32 key;                    /* 0x24 animation playing */
    s32 fontId;                 /* 0x28 set by PreprocessInterface */
    u8 pad2C[4];
    PTR32(IfaceTexAnim) special; /* 0x30 indexed by IfaceDialogEntry.specialKey */
    PTR32(u32) quad;            /* 0x34 4 words copied into the text's scale key */
} IfaceDialogChar;

typedef struct IfaceDialogEntry {
    u8 active;                  /* 0x00 */
    u8 id;                      /* 0x01 portrait object id (def->id) */
    u8 special;                 /* 0x02 */
    u8 specialKey;              /* 0x03 */
    s32 time;                   /* 0x04 ms */
    u32 soundID;                /* 0x08 */
    PTR32(char) soundName;      /* 0x0C data pack with the speech */
    PTR32(PTR32(char)) text;    /* 0x10 string table */
    PTR32(IfaceDialogChar) chr; /* 0x14 */
    IfaceActionList actions;    /* 0x18 */
} IfaceDialogEntry;

typedef struct IfaceDialogList {
    u32 count;
    PTR32(IfaceDialogEntry) entries;
} IfaceDialogList;

typedef struct IfaceMusic {
    PTR32(char) name;           /* 0x0 music pack */
    u32 id;                     /* 0x4 */
} IfaceMusic;

/* Interface definition (InterfaceManager_Create). */
struct InterfaceHeader {
    u16 id;                     /* 0x00 which interface (callbacks switch on it) */
    u16 unk2;                   /* 0x02 */
    u32 loadInTime;             /* 0x04 ms */
    u32 loadOutTime;            /* 0x08 ms */
    u32 numObjects;             /* 0x0C */
    PTR32(IfaceObjectDef) objects; /* 0x10 */
    IfaceEventList events;      /* 0x14 interface-wide input events */
    PTR32(IfaceVarList) vars;   /* 0x1C */
    PTR32(s8) loadInSound;      /* 0x20 sound sequences */
    PTR32(s8) activeSound;      /* 0x24 */
    PTR32(s8) loadOutSound;     /* 0x28 */
    PTR32(s8) moveSound;        /* 0x2C selection changed */
    PTR32(IfaceMusic) music;    /* 0x30 */
    u32 layer;                  /* 0x34 render priority, 0: 5 */
    f32 musicVolume;            /* 0x38 */
    u8 mode;                    /* 0x3C input: 0 all pads, 1-4 pad 0-3 */
};

/* ---- live interface ---------------------------------------------------- */

/* InterfaceObject.kind: how the object is drawn (RenderObject). */
enum {
    IFACE_KIND_IMAGE = 0,
    IFACE_KIND_ANIM_IMAGE = 1,  /* texture animation */
    IFACE_KIND_GEOMETRY = 2,
    IFACE_KIND_FX = 3,          /* particle effect thing */
    IFACE_KIND_THING = 4,       /* a thing created from `data` */
    IFACE_KIND_TEXT = 5,
    IFACE_KIND_CHILD = 6,       /* child interface `child` */
    IFACE_KIND_DIALOG = 7,
    IFACE_KIND_DEBUG_TEXT = 8,
    IFACE_KIND_LINE = 9,
    IFACE_KIND_BOX = 10,
    IFACE_KIND_DEBUG_VECTORS = 11,
    IFACE_KIND_POLY = 12,
};

/* A live interface object (0x300), in the interface's `objects` block. */
struct InterfaceObject {
    IfaceKeyColor color;        /* 0x000 colour, rgba at 0x24 */
    IfaceKeyVec4 scale;         /* 0x030 images: width, height */
    u8 pad58[8];
    IfaceKeyVec4 pos;           /* 0x060 screen position; images: z != 0 draws behind */
    u8 pad88[8];
    IfaceKeyVec4 rot;           /* 0x090 degrees */
    u8 padB8[8];
    IfaceKeyUV uv;              /* 0x0C0 */
    IfaceKeyActive visible;     /* 0x11C value: shown */
    TextureKeyState texAnim;    /* 0x128 IFACE_KIND_ANIM_IMAGE */
    u8 pad134[0xC];
    IfaceKeyVec4 textSize;      /* 0x140 text: font size in .v.x */
    u8 pad168[8];
    u8 unk170[0x10];            /* 0x170 another Vector4 key state (reset at 0x194) */
    f32 unk180;                 /* 0x180 read as the selected text's scale (RenderInterface) */
    u8 pad184[0x10];
    u8 unk194Reset;             /* 0x194 */
    u8 pad195[0xB];
    PTR32(PTR32(char)) strings; /* 0x1A0 text: table of string slots */
    PTR32(char) string;         /* 0x1A4 text shown, NULL: `text` */
    u16 textLen;                /* 0x1A8 */
    u16 textShown;              /* 0x1AA characters typed so far */
    s32 typeTimer;              /* 0x1AC ms to the next typed character */
    u8 fontId;                  /* 0x1B0 */
    u8 paragraph;               /* 0x1B1 print as a paragraph (IfaceText.paragraph) */
    u8 pad1B2[0xE];
    char text[0x80];            /* 0x1C0 own copy of the text */
    u8 hidden;                  /* 0x240 */
    u8 blendMode;               /* 0x241 images */
    u8 pad242;
    u8 kind;                    /* 0x243 IFACE_KIND_* */
    u8 thingCreated;            /* 0x244 `thing` was created */
    u8 role;                    /* 0x245 def->role: HUD value shown (MoveObject) or
                                 *       special behaviour: 3 follows the selection,
                                 *       4 3D anchor, 7 radar, 30 map, 0x1F target arrow,
                                 *       0x20 child without callback, 0x34 mission goal */
    u8 state;                   /* 0x246 IFACE_STATE_* */
    u8 animDone;                /* 0x247 record event timer expired */
    s8 refresh;                 /* 0x248 set on changes; text objects type while set */
    u8 rndDown;                 /* 0x249 role 92: counting down */
    u16 texture;                /* 0x24A 0xFFFF: none */
    u8 pad24C[4];
    s32 stateSound;             /* 0x250 sound sequence of the state, -1 */
    PTR32(IfaceStateAnim) stateAnim; /* 0x254 NULL: no animation for the state */
    PTR32(void) data;           /* 0x258 def->sub (IfaceText, IfaceDialogList, IfaceBox,
                                 *       IfaceLine, TextureKeyAnim) or created thing params */
    PTR32(IfaceObjectDef) def;  /* 0x25C */
    PTR32(InterfaceObject) anchor; /* 0x260 def->anchor: takes its position and rotation */
    PTR32(InterfaceObject) parent; /* 0x264 child interfaces: the parent's object, added on */
    PTR32(void) geom;           /* 0x268 IFACE_KIND_GEOMETRY */
    PTR32(Thing) thing;         /* 0x26C created FX / thing */
    s32 child;                  /* 0x270 IFACE_KIND_CHILD: interface index */
    u32 timer;                  /* 0x274 ms to the next record event */
    f32 rndValue;               /* 0x278 roles 91, 92: random number shown */
    s32 rndTimer;               /* 0x27C */
    f32 rndTarget;              /* 0x280 */
    f32 rndStep;                /* 0x284 */
    PTR32(Thing) car;           /* 0x288 linked thing drawn in the interface */
    u32 listIndex;              /* 0x28C string/texture picked by variable, -1 */
    char savedText[0x34];       /* 0x290 role 0x34: the objective text before '#' substitution */
    u32 dialogEntry;            /* 0x2C4 dialogue: entry playing */
    s32 dialogTime;             /* 0x2C8 ms left */
    u8 pad2CC[4];
    PTR32(s8) dialogSound;      /* 0x2D0 speech sound sequence */
    s32 dialogSeq;              /* 0x2D4 */
    s8 dialogNext;              /* 0x2D8 start the next entry */
    s8 dialogSpoken;            /* 0x2D9 speech started */
    u8 pad2DA[2];
    PTR32(IfaceDialogChar) dialogChar; /* 0x2DC dialogue / portrait: character */
    u8 charState;               /* 0x2E0 portrait: character state shown */
    u8 pad2E1[3];
    s32 charTimer;              /* 0x2E4 ms */
    TextureKeyState charAnim;   /* 0x2E8 */
    u8 pad2F4[4];
    u32 refValue;               /* 0x2F8 role 40: last value */
    u32 allocIndex;             /* 0x2FC index in g_MasterInterfaceObjectList, -1: free */
};

/* Analog stick "virtual key" state, per pad and direction (KeyPress). */
typedef struct IfaceDirState {
    s8 active;
    s8 held;
    s16 timer;                  /* ms before the stick repeats */
} IfaceDirState;

/* Interface.state, the interface's load state. */
enum {
    IFACE_LOADING = 0,          /* load-in animation, `timer` ms */
    IFACE_ENDING = 1,           /* load-out, then `next` or release */
    IFACE_ACTIVE = 2,           /* taking input */
    IFACE_PRESSED = 3,          /* an object was chosen, `timer` ms */
};

/* A live interface, one g_interfaces[] entry (0x170). */
struct Interface {
    u32 count;                  /* 0x000 objects; 0: entry free */
    u32 state;                  /* 0x004 IFACE_LOADING... */
    PTR32(struct Player) owner;        /* 0x008 HUD: the player */
    PTR32(InterfaceObject) selected; /* 0x00C */
    u32 timer;                  /* 0x010 ms left in the state */
    s32 stateSeq;               /* 0x014 sound sequence of the state, -1 */
    f32 timeout;                /* 0x018 ms before `timeoutEvent` fires */
    PTR32(IfaceEvent) timeoutEvent; /* 0x01C header event of type 0x1E */
    PTR32(InterfaceHeader) header; /* 0x020 */
    PTR32(InterfaceHeader) next; /* 0x024 interface to go to when this one ends */
    PTR32(InterfaceHeader) history[12]; /* 0x028 interfaces gone through, for "back" */
    u8 pad58[8];
    Vector4 offset;             /* 0x060 added to the objects' positions if hasOffset */
    u8 historyIds[12];          /* 0x070 selection in each history entry */
    u8 flags;                   /* 0x07C IfaceEventDef.flags of the event ending it */
    u8 index;                   /* 0x07D in g_interfaces */
    u8 hasGeometry;             /* 0x07E */
    u8 isChild;                 /* 0x07F */
    u8 historyCount;            /* 0x080 */
    u8 goBack;                  /* 0x081 ending to go back in the history */
    u8 hasOffset;               /* 0x082 */
    u8 paused;                  /* 0x083 */
    u8 hasInput;                /* 0x084 `input` was set */
    u8 hidden;                  /* 0x085 child: its object in the parent is hidden */
    u8 pad86[2];
    PTR32(InterfaceObject) objects; /* 0x088 `count` objects */
    s32 input;                  /* 0x08C pad, -1: all, -2: none */
    FPTR32(void, (IfaceMsg *msg)) callback; /* 0x090 */
    FPTR32(void, (InterfaceHudInfo *info)) infoCallback; /* 0x094 HUD values without an owner */
    s32 vars[30];               /* 0x098 IfaceVar values */
    u32 numVars;                /* 0x110 */
    u8 player;                  /* 0x114 g_playerIndex at creation */
    u8 syncParent;              /* 0x115 child follows the parent's load state */
    IfaceDirState dir[4][4];    /* 0x116 */
    u8 pad156[2];
    PTR32(InterfaceObject) dialog; /* 0x158 the dialogue object */
    u32 parent;                 /* 0x15C child: parent's index */
    u8 inputDisabled;           /* 0x160 */
    s8 unk161;                  /* 0x161 clears inputDisabled next frame */
    u8 pad162[2];
    f32 inputDelay;             /* 0x164 ms after creation without input */
    s8 locked;                  /* 0x168 stick repeat blocked */
    u8 pad169[7];
};

/* Message to an interface callback. `type` says what happened: 0 object
 * or button event, 1 HUD value query (MoveObject, role 2), 3 created,
 * 4 loaded, 5 released, 0xC every frame, 0xD/0xE event with `data`,
 * 0xF interface change, 0x10 quit, 0x11 movie. */
struct IfaceMsg {
    u32 index;                  /* 0x00 interface */
    u16 type;                   /* 0x04 */
    u16 id;                     /* 0x06 event id / header id / queried object's def->id */
    u16 arg;                    /* 0x08 pads that pressed */
    u16 selId;                  /* 0x0A selected record id; type 3: the callback's choice */
    char text[0x10];            /* 0x0C type 1: text to show */
    f32 fvalue;                 /* 0x1C type 1: value to show */
    u32 result;                 /* 0x20 4: end the interface, 0xB: refuse; type 1: state */
    u32 value;                  /* 0x24 header handle to go to, 0, or -1 */
    u8 handled;                 /* 0x28 sound played by the callback */
    u8 pad29;
    s8 accept;                  /* 0x2A d-pad move: 0 refuses it */
    u8 pad2B[5];
    u32 data[3];                /* 0x30 IfaceEventDef.bytes */
    PTR32(s32) values;          /* 0x3C the interface's vars */
    u16 targetId;               /* 0x40 id of the interface the event goes to, 0xFFFF */
    u8 pad42[0x50 - 0x42];
};

/* HUD values filled by the owner's GetInterfaceInfo (Player, CarA, Thing)
 * or the interface's infoCallback, read by MoveObject per object role. */
struct InterfaceHudInfo {
    u32 index;                  /* 0x00 interface */
    u32 layout;                 /* 0x04 */
    s32 x;                      /* 0x08 split-screen offset */
    s32 y;                      /* 0x0C */
    u32 health;                 /* 0x10 % (role 10) */
    u32 speed;                  /* 0x14 (role 12) */
    u32 rpm;                    /* 0x18 */
    u32 unk1C;                  /* 0x1C role 14 */
    f32 turbo;                  /* 0x20 role 11: 0-1 */
    f32 unk24;                  /* 0x24 role 5 */
    f32 unk28;                  /* 0x28 role 6 */
    f32 altitude;               /* 0x2C role 13 */
    u32 unk30;                  /* 0x30 role 15 */
    u8 weapon[3];               /* 0x34 roles 80, 83, 86: icons shown */
    u8 pad37;
    u32 ammo;                   /* 0x38 % (role 82) */
    u32 ammoPips;               /* 0x3C role 84 */
    PTR32(PTR32(char)) messages[4]; /* 0x40 roles 60, 61, 17, 18: string tables */
    u8 pad50[0xC];
    PTR32(u32) rndText;         /* 0x5C roles 91, 92 store their text here */
    PTR32(PTR32(char)) message19; /* 0x60 role 19 */
    PTR32(PTR32(char)) message49; /* 0x64 role 49 */
    PTR32(PTR32(char)) weaponName; /* 0x68 role 81 */
    u16 weaponIcon[3];          /* 0x6C */
    u8 pad72[2];
    char text[8];               /* 0x74 role 16 (Player_GetInterfaceInfo: score) */
    PTR32(Thing) unk7C;         /* 0x7C role 8: heading of its matrix */
    union {
        struct {
            u8 state;           /* 0x80 role 62: 1 show, 0 hide */
            u8 unk81;           /* 0x81 role 21 */
            u8 unk82;           /* 0x82 role 20 */
        };
        PTR32(u32) refs[17];    /* 0x80 role 40: value slots, [def->id] */
    };
    PTR32(u16) values;          /* 0xC4 roles 41-48 */
    u8 valuesChanged;           /* 0xC8 */
    u8 carStats[4];             /* 0xC9 roles 70-73 (GSI_VehicleSelectionInfo) */
    u8 padCD[3];
};

/* World goals (counters at world + 0x48, timers at 0x50: lists of slots). */
typedef struct WorldGoal {
    u8 unk0;
    u8 active;                  /* 0x1 */
    u8 id;                      /* 0x2 IfaceObjectDef.id of the HUD object */
    u8 pad3;
    f32 count;                  /* 0x4 counters */
    f32 time;                   /* 0x8 timers, ms */
    PTR32(u8) info;             /* 0xC [0] == 0x17: shown */
} WorldGoal;

/* WorldObjective (world + 0x58) is in World.h. */

#define IFACE_ASSERT(T, f, off) _Static_assert(offsetof(T, f) == (off), #T "." #f)
IFACE_ASSERT(IfaceVar, code, 0x10);
_Static_assert(sizeof(IfaceVar) == 0x34, "IfaceVar size");
IFACE_ASSERT(IfaceEventDef, sound, 0x14);
IFACE_ASSERT(IfaceEventDef, mods, 0x20);
IFACE_ASSERT(IfaceEventDef, delta, 0x24);
_Static_assert(sizeof(IfaceEvent) == 0xC, "IfaceEvent size");
IFACE_ASSERT(IfaceStateAnim, sound, 0x30);
IFACE_ASSERT(IfaceStateAnim, actions, 0x34);
_Static_assert(sizeof(IfaceStateDef) == 0xC, "IfaceStateDef size");
_Static_assert(sizeof(IfaceStep) == 0x28, "IfaceStep size");
IFACE_ASSERT(IfaceRecord, numSteps, 0x08);
IFACE_ASSERT(IfaceRecord, events, 0x10);
_Static_assert(sizeof(IfaceCondition) == 0x1C, "IfaceCondition size");
IFACE_ASSERT(IfaceConditionList, conds, 0x8);
IFACE_ASSERT(IfaceObjectDef, id, 0x0A);
IFACE_ASSERT(IfaceObjectDef, linkId, 0x10);
IFACE_ASSERT(IfaceObjectDef, stateIndex, 0x18);
IFACE_ASSERT(IfaceObjectDef, anchorX, 0x30);
IFACE_ASSERT(IfaceObjectDef, child, 0x3C);
IFACE_ASSERT(IfaceObjectDef, record, 0x44);
IFACE_ASSERT(IfaceObjectDef, conditions, 0x48);
IFACE_ASSERT(IfaceObjectDef, anim, 0x60);
IFACE_ASSERT(IfaceObjectDef, states, 0x68);
_Static_assert(sizeof(IfaceObjectDef) == 0x6C, "IfaceObjectDef size");
IFACE_ASSERT(IfaceImageDef, anim, 0x10);
IFACE_ASSERT(IfaceFont, typeDelay, 0x14);
IFACE_ASSERT(IfaceFont, numChars, 0x20);
IFACE_ASSERT(IfaceFont, spacing, 0x28);
IFACE_ASSERT(IfaceText, listMode, 0x10);
IFACE_ASSERT(IfaceText, lineSpacing, 0x18);
IFACE_ASSERT(IfacePolyPoint, rgba, 0x10);
_Static_assert(sizeof(IfacePolyPoint) == 0x20, "IfacePolyPoint size");
_Static_assert(sizeof(IfaceTexAnim) == 0x10, "IfaceTexAnim size");
IFACE_ASSERT(IfaceDialogChar, state, 0x20);
IFACE_ASSERT(IfaceDialogChar, special, 0x30);
IFACE_ASSERT(IfaceDialogEntry, actions, 0x18);
_Static_assert(sizeof(IfaceDialogEntry) == 0x20, "IfaceDialogEntry size");
IFACE_ASSERT(InterfaceHeader, events, 0x14);
IFACE_ASSERT(InterfaceHeader, vars, 0x1C);
IFACE_ASSERT(InterfaceHeader, music, 0x30);
IFACE_ASSERT(InterfaceHeader, mode, 0x3C);
IFACE_ASSERT(InterfaceObject, scale, 0x30);
IFACE_ASSERT(InterfaceObject, pos, 0x60);
IFACE_ASSERT(InterfaceObject, rot, 0x90);
IFACE_ASSERT(InterfaceObject, uv, 0xC0);
IFACE_ASSERT(InterfaceObject, visible, 0x11C);
IFACE_ASSERT(InterfaceObject, texAnim, 0x128);
IFACE_ASSERT(InterfaceObject, textSize, 0x140);
IFACE_ASSERT(InterfaceObject, unk180, 0x180);
IFACE_ASSERT(InterfaceObject, unk194Reset, 0x194);
IFACE_ASSERT(InterfaceObject, strings, 0x1A0);
IFACE_ASSERT(InterfaceObject, fontId, 0x1B0);
IFACE_ASSERT(InterfaceObject, text, 0x1C0);
IFACE_ASSERT(InterfaceObject, hidden, 0x240);
IFACE_ASSERT(InterfaceObject, kind, 0x243);
IFACE_ASSERT(InterfaceObject, role, 0x245);
IFACE_ASSERT(InterfaceObject, texture, 0x24A);
IFACE_ASSERT(InterfaceObject, stateSound, 0x250);
IFACE_ASSERT(InterfaceObject, def, 0x25C);
IFACE_ASSERT(InterfaceObject, anchor, 0x260);
IFACE_ASSERT(InterfaceObject, parent, 0x264);
IFACE_ASSERT(InterfaceObject, thing, 0x26C);
IFACE_ASSERT(InterfaceObject, child, 0x270);
IFACE_ASSERT(InterfaceObject, car, 0x288);
IFACE_ASSERT(InterfaceObject, savedText, 0x290);
IFACE_ASSERT(InterfaceObject, dialogEntry, 0x2C4);
IFACE_ASSERT(InterfaceObject, dialogChar, 0x2DC);
IFACE_ASSERT(InterfaceObject, charAnim, 0x2E8);
IFACE_ASSERT(InterfaceObject, allocIndex, 0x2FC);
_Static_assert(sizeof(InterfaceObject) == 0x300, "InterfaceObject size");
IFACE_ASSERT(Interface, header, 0x20);
IFACE_ASSERT(Interface, history, 0x28);
IFACE_ASSERT(Interface, offset, 0x60);
IFACE_ASSERT(Interface, historyIds, 0x70);
IFACE_ASSERT(Interface, flags, 0x7C);
IFACE_ASSERT(Interface, historyCount, 0x80);
IFACE_ASSERT(Interface, paused, 0x83);
IFACE_ASSERT(Interface, hidden, 0x85);
IFACE_ASSERT(Interface, objects, 0x88);
IFACE_ASSERT(Interface, callback, 0x90);
IFACE_ASSERT(Interface, vars, 0x98);
IFACE_ASSERT(Interface, numVars, 0x110);
IFACE_ASSERT(Interface, dir, 0x116);
IFACE_ASSERT(Interface, dialog, 0x158);
IFACE_ASSERT(Interface, parent, 0x15C);
IFACE_ASSERT(Interface, inputDelay, 0x164);
IFACE_ASSERT(Interface, locked, 0x168);
_Static_assert(sizeof(Interface) == 0x170, "Interface size");
IFACE_ASSERT(IfaceMsg, selId, 0x0A);
IFACE_ASSERT(IfaceMsg, fvalue, 0x1C);
IFACE_ASSERT(IfaceMsg, result, 0x20);
IFACE_ASSERT(IfaceMsg, handled, 0x28);
IFACE_ASSERT(IfaceMsg, accept, 0x2A);
IFACE_ASSERT(IfaceMsg, data, 0x30);
IFACE_ASSERT(IfaceMsg, values, 0x3C);
IFACE_ASSERT(IfaceMsg, targetId, 0x40);
IFACE_ASSERT(InterfaceHudInfo, weapon, 0x34);
IFACE_ASSERT(InterfaceHudInfo, rndText, 0x5C);
IFACE_ASSERT(InterfaceHudInfo, weaponIcon, 0x6C);
IFACE_ASSERT(InterfaceHudInfo, text, 0x74);
IFACE_ASSERT(InterfaceHudInfo, state, 0x80);
IFACE_ASSERT(InterfaceHudInfo, refs, 0x80);
IFACE_ASSERT(InterfaceHudInfo, unk82, 0x82);
IFACE_ASSERT(InterfaceHudInfo, values, 0xC4);
IFACE_ASSERT(InterfaceHudInfo, carStats, 0xC9);
_Static_assert(sizeof(InterfaceHudInfo) == 0xD0, "InterfaceHudInfo size");
IFACE_ASSERT(WorldGoal, info, 0xC);
#undef IFACE_ASSERT

/* The 0x12 interfaces (data_gen.c defines the storage as bytes). */
#define IFACE_MAX 0x12
extern u8 g_interface[];
#define g_interfaces ((Interface *)(void *)g_interface)

#endif
