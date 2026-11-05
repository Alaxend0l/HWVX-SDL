/* Port menu entries: "Quit Game" in the main menu, and a "Graphics"
 * entry in the Options screen with the Graphics screen it opens (resolution,
 * windowed/fullscreen, vsync on/off and a max frame rate of 30/60/90/120/144/
 * 240: src/port/gfx_settings.c). Port-only file.
 *
 * Menus are data (HW_IPack.xgc), so these are built from the screens' own
 * definitions when they are created (Port_MenuPatch, from
 * InterfaceManager_Create); nothing is added to the pack files. A new entry
 * is a copy of an existing row (a text whose A/Start events go to another
 * interface), relabelled and retargeted, added as a seventh row; the rows are
 * respaced (ROW_*) so the seven fit where the six were, and it joins the
 * up/down ring between the last row and the first.
 *
 * - Main menu: "Quit Game" (a copy of "Extras") opens the game's
 *   "Quit Game / Confirm Quit" screen, made for the GameCube's exit to the
 *   system menu; its warning is reworded, its "Yes, Quit" (event 0x19,
 *   ForceIPLReset) closes the game on PC, and its two buttons take Start as
 *   well as A, like the other menus' rows.
 * - Options: "Graphics" (a copy of "Controller") opens the Graphics screen.
 * - Memory card screens (saves are files on PC, always "slot A"): Load Game
 *   and Save Game start on slot A's file list (no Slot A/B picker), without
 *   the card warnings, and B leaves them. Port_MenuEventTarget skips the rest
 *   where an interface switch is decided: "Loading..."/"Saving..." load or
 *   save at once and go to the main menu (their failure screens remain), Load
 *   Game does nothing without a save, and leaving Options, where the "Be sure
 *   to save" reminder came up after a setting changed, autosaves instead
 *   (GameSettings_PortAutoSave).
 * - Graphics: a copy of the Options screen with the same 40 objects (object
 *   indices in actions stay valid). The rows it doesn't use are hidden; the
 *   "Difficulty" row becomes "Resolution" and the "Sound Mode" row "Display",
 *   moved to the top two row positions. Their variables read the port's code
 *   variables (PORT_CODEVAR_*), their left/right actions write them. B goes
 *   back like the Controller screen's. Leaving it applies the settings
 *   (Port_MenuReleased).
 *
 * - Multi-Player: a new screen in front of the game's, Online or Local (the game's
 *   own). Online has Host with Tunnel, Host via LAN and Connect, then a lobby; see
 *   "Online screens" below and Port_Online.c.
 *
 * - Pause menu in the multi-player modes, online or local: no Save row, Quit moving
 *   up into its place. One Player keeps it: the same screen, so this is a copy made
 *   each time, and the original comes back otherwise.
 *
 * Copies live in static storage; the pack data they point to stays where it
 * is. To add a setting: copy another row into the Graphics screen the same
 * way, with a new PORT_CODEVAR_* id. */
#include "common.h"
#include "Interface.h"
#include "Port_Online.h"

/* The Options screen's objects (pack data, see tools: dump with xgc_inspect). */
enum {
    OPT_NUM_OBJECTS = 40,
    OPT_TITLE = 13,             /* "Game Settings" */
    OPT_CONTROLLER = 16,
    OPT_CHEATS = 17,
    OPT_CHEATS_LABEL = 18,
    OPT_DIFFICULTY = 19,        /* label, value (20), arrows (21, 22) */
    OPT_SOUND = 23,             /* label, value (24), arrows (25, 26) */
    OPT_MUSIC = 27,             /* to 30 */
    OPT_SFX = 31,               /* to 34 */
    OPT_DEBUG = 35,
};

enum {
    OPT_VAR_DIFFICULTY = 0,     /* Graphics: the resolution */
    OPT_VAR_SELECTED = 1,
    OPT_VAR_SOUND = 3,          /* Graphics: the display mode */
    OPT_VAR_MUSIC = 4,          /* Graphics: vsync */
    OPT_VAR_SFX = 5,            /* Graphics: the frame rate */
};

enum { REC_CONTROLLER = 1, REC_MUSIC = 4, REC_SFX = 5, REC_GRAPHICS = 8, REC_DIFFICULTY = 3, REC_SOUND = 6 };

/* Menu rows in the pack: the first (Controller, One Player) at 198, 26 apart.
 * With the port's seventh row they sit a little closer, row k at
 * ROW_Y + k * ROW_STEP, centred in the menu panel: the panel's inside spans
 * about 170 to 360 (main menu and Options alike, measured in frame dumps),
 * centre 265; the seven rows' centre is ROW_Y + 3 * ROW_STEP. */
#define PACK_ROW_FIRST_Y 198.0f
#define PACK_ROW_STEP_Y 26.0f
#define ROW_Y 195.0f
#define ROW_STEP 23.0f
#define ROW_NEW 6               /* the port's row, after the pack's six */

/* The main menu's objects, and the quit screen's. */
enum {
    MAIN_NUM_OBJECTS = 26,
    MAIN_TITLE = 4,             /* "Main Menu" */
    MAIN_ROWS = 12,             /* One Player .. Extras: 12 to 21 */
    MAIN_ROWS_END = 22,
    MAIN_EXTRAS = 21,
    QUIT_NUM_OBJECTS = 27,
    QUIT_WARNING = 19,
    QUIT_CONFIRM = 18,
    QUIT_YES = 21,
    QUIT_NO = 22,
};
/* IfaceEvent.type: the buttons menu rows answer to */
enum { EVENT_A = 0x4, EVENT_START = 0x15 };
enum { REC_ONE_PLAYER = 1, REC_EXTRAS = 5, REC_DESKTOP = 7 };
/* Blob offsets of the main menu (the pack's root) and the quit screen in
 * HW_IPack.xgc pack 6, the menu pack (MCP menuFile). */
#define PACK_MAIN_MENU 0x6ECC60u
#define PACK_QUIT_SCREEN 0x83A8A0u
#define PACK_LOAD_SCREEN 0x7FE220u      /* Load Game: slot picker / file list (46 objects) */
#define PACK_LOAD_PROGRESS 0x825C20u    /* "Loading from Memory Card..." (15) */
#define PACK_SAVE_SCREEN 0x835980u      /* Save Game: slot picker / file list (48) */
#define PACK_SAVE_PROGRESS 0x8484A0u    /* "Saving data to the Memory Card..." (14) */
#define PACK_REMINDER 0x85BC00u         /* "Be sure to save your game..." (12) */

/* The Load/Save Game screens: var 4 is the phase (0: slot picker, 1: file
 * list), object 11 the card warnings. */
enum { CARD_VAR_PHASE = 4, CARD_WARNING = 11 };
enum { CODEVAR_SETTINGS_CHANGED = 0xCA, CODEVAR_DEVICE = 0x1F7, CODEVAR_FILE_INDEX = 0x1F8 };
enum { EVENT_GOTO = 1, EVENT_BACK = 2, EVENT_NOTHING = 0xC };
enum { MAIN_MULTI_PLAYER = 14, REC_MULTI_PLAYER = 2, REC_LOAD_GAME = 3 };

void GameSettings_PortAutoSave(void);
s32 GameSettings_GetMemoryCardStatus(s32 device, s8 bForce);
u8 GameSettings_LoadGame(void);
u8 GameSettings_SaveGame(void);
#define OPT_ROWS 16             /* Controller .. SFX: 16 to 34 */
#define OPT_ROWS_END 35
#define MAX_EVENTS 4
#define MAX_MODES 48

typedef struct TextCopy {
    IfaceText text;
    PTR32(char) slot;
    char str[64];
} TextCopy;

typedef struct AnimCopy {
    IfaceObjAnim anim;
    u8 pos[0x20];               /* the one position key: x, y, ... */
} AnimCopy;

typedef struct RecordCopy {
    IfaceRecord rec;
    IfaceEvent events[MAX_EVENTS];
    IfaceEventDef defs[MAX_EVENTS];
    IfaceStep step;             /* SetActions: the row's own step, for records
                                 * the pack gave no step to (its left/right
                                 * events moved something else instead) */
} RecordCopy;

/* SetCodeVariable(id, arg, vars[var]) */
typedef struct ActionCopy {
    IfaceActionList list;
    IfaceAction action;
    IfaceCodeAction code;
    IfaceCodeVar codeVar;
    IfaceVarValue value;
} ActionCopy;

typedef struct ListCopy {
    IfaceText text;
    IfaceStringList list;
    u32 texts[MAX_MODES];
    PTR32(char) slots[MAX_MODES];
} ListCopy;

static IfaceObjectDef s_mainObjs[MAIN_NUM_OBJECTS + 1];
static AnimCopy s_mainRowAnim[MAIN_ROWS_END - MAIN_ROWS], s_desktopAnim;
static TextCopy s_desktopText;
static RecordCopy s_desktopRec;
static PTR32(char) s_quitWarning;
static InterfaceHeader *s_main, *s_loadScreen, *s_loadProgress, *s_saveScreen, *s_saveProgress, *s_reminder;
static IfaceEvent s_quitEvents[2][2];

static IfaceObjectDef s_optObjs[OPT_NUM_OBJECTS + 1];
static AnimCopy s_optRowAnim[OPT_ROWS_END - OPT_ROWS];
static AnimCopy s_entryAnim;
static TextCopy s_entryText;
static RecordCopy s_entryRec;

static InterfaceHeader s_gfx;
static IfaceObjectDef s_gfxObjs[OPT_NUM_OBJECTS];
static IfaceVarList s_gfxVarList;
static IfaceVar s_gfxVars[8];
static IfaceCodeVar s_gfxCode[4];
static IfaceConditionList s_hideAll;   /* type 0 "hide if all", no conditions: always hidden */
static AnimCopy s_rowAnim[16];
static TextCopy s_gfxText[5];
static RecordCopy s_gfxRec[4];
static ActionCopy s_gfxAction[4];
static ListCopy s_resList, s_displayList, s_vsyncList, s_fpsList;

static const char *DefText(const IfaceObjectDef *d) {
    IfaceText *t;

    if (d->type != IFACE_DEF_TEXT || (t = GETP(d->sub)) == NULL || GETP(t->strings) == NULL) {
        return NULL;
    }
    return GETP(*GETP(t->strings));
}

static int TextIs(const IfaceObjectDef *d, const char *s) {
    const char *t = DefText(d);
    return t != NULL && strcmp(t, s) == 0;
}

static void SetText(TextCopy *c, IfaceObjectDef *d, const char *s) {
    c->text = *(IfaceText *)GETP(d->sub);
    strncpy(c->str, s, sizeof(c->str) - 1);
    SETP(c->slot, c->str);
    SETP(c->text.strings, &c->slot);
    SETP(d->sub, &c->text);
}

/* c = d's animation with its one position key raised to height y (the port's
 * copy, `d`'s own anim data is left alone). */
static void CopyAnim(AnimCopy *c, const IfaceObjectDef *d, f32 y) {
    IfaceObjAnim *a = GETP(d->anim);
    f32 *key;

    c->anim = *a;
    memcpy(c->pos, GETP(a->pos.keys), sizeof(c->pos));
    key = (f32 *)c->pos;
    key[1] = y;
    SETP(c->anim.pos.keys, c->pos);
}

/* d's default animation, at height y. */
static void MoveTo(AnimCopy *c, IfaceObjectDef *d, f32 y) {
    CopyAnim(c, d, y);
    SETP(d->anim, &c->anim);
}

/* d, an object of the pack's row k, to row k of the respaced menu. */
static void MoveRow(AnimCopy *c, IfaceObjectDef *d) {
    f32 y = ((f32 *)GETP(GETP(d->anim)->pos.keys))[1];
    s32 k = (s32)((y - PACK_ROW_FIRST_Y) / PACK_ROW_STEP_Y + 0.5f);

    MoveTo(c, d, ROW_Y + k * ROW_STEP);
}

/* A copy of d's record (and its events) with a new id and up/down links. */
static RecordCopy *CopyRecord(RecordCopy *c, IfaceObjectDef *d, u8 id, u8 up, u8 down) {
    IfaceRecord *r = GETP(d->record);
    u32 i;

    c->rec = *r;
    c->rec.id = id;
    c->rec.next[0] = up;
    c->rec.next[1] = down;
    if (c->rec.events.count > MAX_EVENTS) {
        c->rec.events.count = MAX_EVENTS;
    }
    for (i = 0; i < c->rec.events.count; i++) {
        c->events[i] = GETP(r->events.events)[i];
        c->defs[i] = *GETP(c->events[i].def);
        SETP(c->events[i].def, &c->defs[i]);
    }
    SETP(c->rec.events.events, c->events);
    SETP(d->record, &c->rec);
    return c;
}

/* The row applies vars[var] to its code variable when its left/right events
 * run. `rec`'s pack events either already do that (type 0xB, the Difficulty
 * and Sound rows) or change one of the game's volume variables instead (types
 * 0x16/0x17, the Music and SFX rows this port clones as VSync and Max FPS):
 * retarget both kinds at the port's own action list, so neither moves volume.
 * A row whose pack record has no step - again the Music and SFX rows, whose
 * left/right only moved volume - gets one here, matching the pack's other
 * rows: left/right add -1/+1 to vars[var], wrapped into the variable's
 * min/max by the interface (InterfaceManager_Update). */
static void SetActions(RecordCopy *rec, ActionCopy *c, u32 id, u16 var) {
    u32 i;

    memset(c, 0, sizeof(*c));
    c->codeVar.id = id;
    SETP(c->codeVar.arg, &c->value);    /* arg: constant 0 */
    c->value.isVar = 1;
    c->value.value = var;
    SETP(c->code.code, &c->codeVar);
    SETP(c->code.value, &c->value);
    c->action.type = 2;
    SETP(c->action.data, &c->code);
    c->list.count = 1;
    SETP(c->list.actions, &c->action);
    for (i = 0; i < rec->rec.events.count; i++) {
        if (rec->defs[i].type == 0xB || rec->defs[i].type == 0x16 || rec->defs[i].type == 0x17) {
            rec->defs[i].type = 0xB;
            rec->defs[i].delta = 0;
            SETP(rec->defs[i].target, &c->list);
        }
    }
    if (rec->rec.numSteps == 0) {
        memset(&rec->step, 0, sizeof(rec->step));
        rec->step.var = var;
        rec->step.val[2] = -1;
        rec->step.val[3] = 1;
        SETP(rec->rec.steps, &rec->step);
        rec->rec.numSteps = 1;
    }
}

/* d shows names[vars[var]]. */
static void SetList(ListCopy *c, IfaceObjectDef *d, u16 var, const char *const *names, u32 n) {
    u32 i;

    if (n > MAX_MODES) {
        n = MAX_MODES;
    }
    c->text = *(IfaceText *)GETP(d->sub);
    for (i = 0; i < n; i++) {
        SETP(c->slots[i], names[i]);
        c->texts[i] = HND(&c->slots[i]);
    }
    memset(&c->list, 0, sizeof(c->list));
    c->list.count = n;
    SETP(c->list.texts, c->texts);
    c->list.fromVar = 1;
    c->list.var = var;
    c->text.listMode = 1;
    SETP(c->text.list, &c->list);
    SETP(c->text.strings, &c->slots[0]);
    SETP(d->sub, &c->text);
}

/* d shows names[vars[var]], as SetList does. But the pack's Music and SFX
 * rows show their volume in a bar, a role 102/103 image that the interface
 * scales by the volume code variable (0xCF/0xD0): no text to list into, and
 * the bar's anim, states and alignment would misplace and mis-tint any text
 * put in its slot. So d becomes a full visual clone of `model`, the Display
 * row's value text: anim in `ac` with its position raised to height y, plus
 * states and anchors copied. Both live in the port's copy of the object
 * table; the bar's image sub is left unused. */
static void SetListAsText(ListCopy *c, IfaceObjectDef *d, u16 var, const char *const *names, u32 n,
                          const IfaceObjectDef *model, AnimCopy *ac, f32 y) {
    u32 i;

    CopyAnim(ac, model, y);
    SETP(d->anim, &ac->anim);
    for (i = 0; i < 6; i++) {
        d->stateIndex[i] = model->stateIndex[i];
    }
    d->numStates = model->numStates;
    SETP(d->states, GETP(model->states));
    d->anchor = model->anchor;
    d->anchorX = model->anchorX;
    d->anchorY = model->anchorY;

    d->type = IFACE_DEF_TEXT;
    d->role = 9;
    SETP(d->sub, GETP(model->sub));
    SetList(c, d, var, names, n);
}

static void SetCodeVar(IfaceVar *v, IfaceCodeVar *c, u32 id, s32 max) {
    *c = *GETP(v->code);
    c->id = id;
    SETP(v->code, c);
    v->type = IFACE_VAR_CODE;
    v->update = 1;
    v->save = 0;
    v->min = 0;
    v->max = max;
    v->offset = 0;
}

/* The display modes as the Resolution row's list (on every opening: the
 * window may be on another display now). */
static void RefreshModes(void) {
    static const char *names[MAX_MODES];
    u32 n, i;

    Port_GfxMenuOpened();
    n = Port_GfxModeCount();
    if (n > MAX_MODES) n = MAX_MODES;
    if (n == 0) {
        names[0] = "-";
        n = 1;
    } else {
        for (i = 0; i < n; i++) {
            names[i] = Port_GfxModeName(i);
        }
    }
    SetList(&s_resList, &s_gfxObjs[OPT_DIFFICULTY + 1], OPT_VAR_DIFFICULTY, names, n);
    s_gfxVars[OPT_VAR_DIFFICULTY].max = (s32)n - 1;
}

static void BuildGraphicsScreen(InterfaceHeader *opt, const IfaceObjectDef *objs) {
    static const char *const displayNames[2] = { "Windowed", "Fullscreen" };
    const char *optNames[PORT_MAX_CHOICES];
    static const u8 hide[] = {
        OPT_CONTROLLER, OPT_CHEATS, OPT_CHEATS_LABEL, OPT_DEBUG,
    };
    IfaceVarList *vars = GETP(opt->vars);
    InterfaceHeader *ctrl;
    u32 i;

    s_gfx = *opt;
    memcpy(s_gfxObjs, objs, sizeof(s_gfxObjs));
    SETP(s_gfx.objects, s_gfxObjs);
    s_gfx.numObjects = OPT_NUM_OBJECTS;
    /* B: back, as on the Controller screen (the one its row goes to) */
    ctrl = (InterfaceHeader *)GETP(GETP(GETP(GETP(objs[OPT_CONTROLLER].record)->events.events)[0].def)->target);
    s_gfx.events = ctrl->events;

    s_gfxVarList.count = vars->count < 8 ? vars->count : 8;
    memcpy(s_gfxVars, GETP(vars->vars), s_gfxVarList.count * sizeof(IfaceVar));
    /* The pack's list ends at the Sound row's var. Extend the copy with
     * clones of it (a code var of the right shape, which SetCodeVar below
     * retargets) so the VSync and Max FPS rows have vars 4 and 5. */
    while (s_gfxVarList.count < 6 && s_gfxVarList.count < sizeof(s_gfxVars) / sizeof(s_gfxVars[0])) {
        s_gfxVars[s_gfxVarList.count] = s_gfxVars[OPT_VAR_SOUND];
        s_gfxVarList.count++;
    }
    SETP(s_gfxVarList.vars, s_gfxVars);
    SETP(s_gfx.vars, &s_gfxVarList);
    SetCodeVar(&s_gfxVars[OPT_VAR_DIFFICULTY], &s_gfxCode[0], PORT_CODEVAR_RESOLUTION, 0);
    SetCodeVar(&s_gfxVars[OPT_VAR_SOUND], &s_gfxCode[1], PORT_CODEVAR_DISPLAY, 1);

    s_hideAll.type = 0;
    s_hideAll.count = 0;
    for (i = 0; i < sizeof(hide); i++) {
        s_gfxObjs[hide[i]].active = 0;
        SETP(s_gfxObjs[hide[i]].conditions, &s_hideAll);
    }
    SetText(&s_gfxText[0], &s_gfxObjs[OPT_TITLE], "Graphics");

    /* the four rows, moved to the top */
    for (i = 0; i < 4; i++) {
        MoveTo(&s_rowAnim[i], &s_gfxObjs[OPT_DIFFICULTY + i], ROW_Y);
        MoveTo(&s_rowAnim[4 + i], &s_gfxObjs[OPT_SOUND + i], ROW_Y + ROW_STEP);
        MoveTo(&s_rowAnim[8 + i], &s_gfxObjs[OPT_MUSIC + i], ROW_Y + 2 * ROW_STEP);
        MoveTo(&s_rowAnim[12 + i], &s_gfxObjs[OPT_SFX + i], ROW_Y + 3 * ROW_STEP);
    }
    SetText(&s_gfxText[1], &s_gfxObjs[OPT_DIFFICULTY], "Resolution");
    SetActions(CopyRecord(&s_gfxRec[0], &s_gfxObjs[OPT_DIFFICULTY], REC_DIFFICULTY, REC_SFX, REC_SOUND),
               &s_gfxAction[0], PORT_CODEVAR_RESOLUTION, OPT_VAR_DIFFICULTY);
    SetText(&s_gfxText[2], &s_gfxObjs[OPT_SOUND], "Display");
    SetActions(CopyRecord(&s_gfxRec[1], &s_gfxObjs[OPT_SOUND], REC_SOUND, REC_DIFFICULTY, REC_MUSIC),
               &s_gfxAction[1], PORT_CODEVAR_DISPLAY, OPT_VAR_SOUND);
    SetList(&s_displayList, &s_gfxObjs[OPT_SOUND + 1], OPT_VAR_SOUND, displayNames, 2);
    if (s_gfxVarList.count > OPT_VAR_SFX) {
        /* VSync and Max FPS: the Music and SFX rows (their packs' own rows,
         * hidden before), relabelled and wired to the port's code vars. The
         * ring runs Resolution - Display - VSync - Max FPS - Resolution. */
        u32 n;

        SetCodeVar(&s_gfxVars[OPT_VAR_MUSIC], &s_gfxCode[2], PORT_CODEVAR_VSYNC, 1);
        SetCodeVar(&s_gfxVars[OPT_VAR_SFX], &s_gfxCode[3], PORT_CODEVAR_MAXFPS,
                   (s32)Port_GfxOptionCount(PORT_CODEVAR_MAXFPS) - 1);
        SetText(&s_gfxText[3], &s_gfxObjs[OPT_MUSIC], "VSync");
        SetActions(CopyRecord(&s_gfxRec[2], &s_gfxObjs[OPT_MUSIC], REC_MUSIC, REC_SOUND, REC_SFX),
                   &s_gfxAction[2], PORT_CODEVAR_VSYNC, OPT_VAR_MUSIC);
        for (n = 0; n < Port_GfxOptionCount(PORT_CODEVAR_VSYNC) && n < PORT_MAX_CHOICES; n++) {
            optNames[n] = Port_GfxOptionName(PORT_CODEVAR_VSYNC, n);
        }
        SetListAsText(&s_vsyncList, &s_gfxObjs[OPT_MUSIC + 1], OPT_VAR_MUSIC, optNames, n,
                      &s_gfxObjs[OPT_SOUND + 1], &s_rowAnim[9], ROW_Y + 2 * ROW_STEP);
        SetText(&s_gfxText[4], &s_gfxObjs[OPT_SFX], "Max FPS");
        SetActions(CopyRecord(&s_gfxRec[3], &s_gfxObjs[OPT_SFX], REC_SFX, REC_MUSIC, REC_DIFFICULTY),
                   &s_gfxAction[3], PORT_CODEVAR_MAXFPS, OPT_VAR_SFX);
        for (n = 0; n < Port_GfxOptionCount(PORT_CODEVAR_MAXFPS) && n < PORT_MAX_CHOICES; n++) {
            optNames[n] = Port_GfxOptionName(PORT_CODEVAR_MAXFPS, n);
        }
        SetListAsText(&s_fpsList, &s_gfxObjs[OPT_SFX + 1], OPT_VAR_SFX, optNames, n,
                      &s_gfxObjs[OPT_SOUND + 1], &s_rowAnim[13], ROW_Y + 3 * ROW_STEP);
        Port_Log("menu: graphics screen built (resolution, display, vsync, max fps)");
    } else {
        for (i = OPT_MUSIC; i < OPT_SFX + 4; i++) {
            s_gfxObjs[i].active = 0;
            SETP(s_gfxObjs[i].conditions, &s_hideAll);
        }
    }
    RefreshModes();
}

/* The Options screen, still as in the pack? */
static int IsOptions(InterfaceHeader *h) {
    IfaceObjectDef *objs = GETP(h->objects);

    return h->numObjects == OPT_NUM_OBJECTS && objs != NULL && GETP(h->vars) != NULL &&
           GETP(h->vars)->count > OPT_VAR_SOUND && TextIs(&objs[OPT_CONTROLLER], "Controller") &&
           TextIs(&objs[OPT_SOUND], "Sound Mode") && TextIs(&objs[OPT_SFX], "SFX") &&
           GETP(objs[OPT_CONTROLLER].record) != NULL && GETP(objs[OPT_SFX].record) != NULL;
}

/* The main menu, still as in the pack? */
static int IsMainMenu(InterfaceHeader *h) {
    IfaceObjectDef *objs = GETP(h->objects);

    return h->numObjects == MAIN_NUM_OBJECTS && objs != NULL && TextIs(&objs[MAIN_TITLE], "Main Menu") &&
           TextIs(&objs[MAIN_ROWS], "One Player") && TextIs(&objs[MAIN_EXTRAS], "Extras") &&
           GETP(objs[MAIN_ROWS].record) != NULL && GETP(objs[MAIN_EXTRAS].record) != NULL;
}

/* The quit screen, found from the main menu (both in the menu pack). */
static InterfaceHeader *QuitScreen(InterfaceHeader *main) {
    InterfaceHeader *q = (InterfaceHeader *)((u8 *)main - PACK_MAIN_MENU + PACK_QUIT_SCREEN);
    IfaceObjectDef *objs = GETP(q->objects);

    if (q->numObjects != QUIT_NUM_OBJECTS || objs == NULL || !TextIs(&objs[QUIT_CONFIRM], "Confirm Quit") ||
        !TextIs(&objs[QUIT_YES], "Yes, Quit") || DefText(&objs[QUIT_WARNING]) == NULL) {
        return NULL;
    }
    return q;
}

/* A screen of the menu pack, at `offset` there if it has `numObjects`. */
static InterfaceHeader *PackScreen(InterfaceHeader *main, u32 offset, u32 numObjects) {
    InterfaceHeader *h = (InterfaceHeader *)((u8 *)main - PACK_MAIN_MENU + offset);

    if (h->numObjects != numObjects || GETP(h->objects) == NULL) {
        Port_Log("menu: no screen at 0x%X", offset);
        return NULL;
    }
    return h;
}

static void FindCardScreens(InterfaceHeader *main) {
    IfaceObjectDef *objs;
    const char *t;

    s_main = main;
    s_loadScreen = PackScreen(main, PACK_LOAD_SCREEN, 46);
    s_loadProgress = PackScreen(main, PACK_LOAD_PROGRESS, 15);
    s_saveScreen = PackScreen(main, PACK_SAVE_SCREEN, 48);
    s_saveProgress = PackScreen(main, PACK_SAVE_PROGRESS, 14);
    s_reminder = PackScreen(main, PACK_REMINDER, 12);
    if (s_reminder != NULL) {
        objs = GETP(s_reminder->objects);
        t = DefText(&objs[11]);
        if (t == NULL || strncmp(t, "Be sure to save", 15) != 0) {
            s_reminder = NULL;
        }
    }
}

/* Load/Save Game: slot A's file list from the start, no card warnings, and
 * B (back to the slot picker) and the automatic return to it leave instead. */
static void PatchCardScreen(InterfaceHeader *h) {
    IfaceObjectDef *objs = GETP(h->objects);
    IfaceVarList *vars = GETP(h->vars);
    u32 i, j, k;

    if (vars != NULL && vars->count > CARD_VAR_PHASE) {
        GETP(vars->vars)[CARD_VAR_PHASE].offset = 1;
    }
    GameSettings_SetCodeVariable(CODEVAR_DEVICE, 0, 1);
    GameSettings_SetCodeVariable(CODEVAR_FILE_INDEX, 0, 1);
    s_hideAll.type = 0;
    s_hideAll.count = 0;
    SETP(objs[CARD_WARNING].conditions, &s_hideAll);
    for (i = 0; i < h->numObjects; i++) {
        IfaceRecord *rec = GETP(objs[i].record);
        if (rec == NULL) {
            continue;
        }
        for (j = 0; j < rec->events.count; j++) {
            IfaceEventDef *def = GETP(GETP(rec->events.events)[j].def);
            if (def == NULL || def->type != EVENT_NOTHING) {
                continue;
            }
            for (k = 0; k < def->numMods; k++) {
                IfaceEventMod *mod = &GETP(def->mods)[k];
                if (mod->var == CARD_VAR_PHASE && mod->delta < 0) {
                    def->type = EVENT_BACK;
                    def->id = 1;
                    def->numMods = 0;
                    break;
                }
            }
        }
    }
}

/* InterfaceManager_PlayEvent: an event is about to switch to `target` (go-to
 * events, types 1 and 0x20). Returns where to go instead, NULL for nowhere. */
/* ---- Online screens ---------------------------------------------------------
 * All copies of the game's Multi-Player screen: the same frame and title, with
 * lines of text in place of its two rows, some of them selectable. Text lines come
 * first (their numbers are what Port_MenuOnlineSetLine takes), then the rows. */

enum {
    MP_NUM_OBJECTS = 16,
    MP_TITLE = 10,
    MP_ROW_A = 11,
    MP_ROW_B = 12,
    MP_MAX_LINES = 6,
};
#define PACK_MP_SCREEN 0x7B4200u
#define PACK_MP_DRAG 0x7B61C0u      /* Drag Race Options */
#define PACK_MP_BATTLE 0x7E4440u    /* Battle Options */
#define PACK_CAR_SELECT 0x7C6540u   /* First Player .. Fourth Player */
#define CAR_SELECT_NUM_OBJECTS 22
#define PACK_CAR_SELECT_SOLO 0x72CA00u  /* the one-player modes': online too, each
                                         * machine having one player */
#define CAR_SELECT_SOLO_NUM_OBJECTS 6

typedef struct OnlineScreen {
    InterfaceHeader h;
    IfaceObjectDef objs[MP_NUM_OBJECTS + MP_MAX_LINES];
    TextCopy title;
    TextCopy text[MP_MAX_LINES];
    AnimCopy anim[MP_MAX_LINES];
    RecordCopy rec[MP_MAX_LINES];
    u32 lineObject[MP_MAX_LINES];
    u32 numLines;
} OnlineScreen;

typedef struct OnlineRow {
    const char *name;
    InterfaceHeader *target;    /* NULL: back */
} OnlineRow;

static OnlineScreen s_olChoose, s_olOnline, s_olTunnel, s_olConnect, s_olLobbyHost, s_olLobbyClient, s_olMode, s_olWaiting;
static InterfaceHeader s_olPaste;   /* never shown: the Paste row's "target" */
static InterfaceHeader *s_mpScreen, *s_carSelect, *s_carSelectSolo;
static RecordCopy s_mainMultiRec;
static char s_olAddress[64];

static void BuildOnlineScreen(OnlineScreen *s, const InterfaceHeader *mp, const char *title,
                              u32 numText, u32 numRows, const OnlineRow *rows) {
    const IfaceObjectDef *from = GETP(mp->objects);
    u32 numLines = numText + numRows;
    u32 extra = 0;
    u32 i;

    s->h = *mp;
    memcpy(s->objs, from, MP_NUM_OBJECTS * sizeof(IfaceObjectDef));
    SetText(&s->title, &s->objs[MP_TITLE], title);
    s->numLines = numLines;
    for (i = 0; i < numLines; i++) {
        u32 at = i == 0 ? MP_ROW_A : i == 1 ? MP_ROW_B : MP_NUM_OBJECTS + extra++;
        IfaceObjectDef *d = &s->objs[at];

        *d = from[MP_ROW_A];
        s->lineObject[i] = at;
        MoveTo(&s->anim[i], d, ROW_Y + i * ROW_STEP);
        if (i < numText) {
            SetText(&s->text[i], d, "");
            d->active = 0;
            d->record.h = 0;
        } else {
            u32 row = i - numText;
            RecordCopy *rec = CopyRecord(&s->rec[i], d, (u8)(row + 1),
                                         (u8)(row == 0 ? numRows : row), (u8)(row + 1 == numRows ? 1 : row + 2));
            u32 e;

            SetText(&s->text[i], d, rows[row].name);
            for (e = 0; e < rec->rec.events.count; e++) {
                if (rows[row].target != NULL) {
                    SETP(rec->defs[e].target, rows[row].target);
                } else {
                    rec->defs[e].type = EVENT_BACK;
                    rec->defs[e].id = 1;
                    rec->defs[e].target.h = 0;
                }
            }
        }
    }
    if (numLines < 2) {
        SETP(s->objs[MP_ROW_B].conditions, &s_hideAll);
        s->objs[MP_ROW_B].active = 0;
        s->objs[MP_ROW_B].record.h = 0;
    }
    s->h.numObjects = MP_NUM_OBJECTS + extra;
    SETP(s->h.objects, s->objs);
}

/* Built again with each main menu: pack data may have moved since the last. */
static void BuildOnlineScreens(InterfaceHeader *main) {
    OnlineRow rows[4];

    s_mpScreen = PackScreen(main, PACK_MP_SCREEN, MP_NUM_OBJECTS);
    s_carSelect = PackScreen(main, PACK_CAR_SELECT, CAR_SELECT_NUM_OBJECTS);
    s_carSelectSolo = PackScreen(main, PACK_CAR_SELECT_SOLO, CAR_SELECT_SOLO_NUM_OBJECTS);
    if (s_mpScreen == NULL || s_carSelect == NULL || s_carSelectSolo == NULL || !TextIs(&GETP(s_mpScreen->objects)[MP_TITLE], "Multi-Player")) {
        s_mpScreen = NULL;
        return;
    }
    rows[0] = (OnlineRow){"Online", &s_olOnline.h};
    rows[1] = (OnlineRow){"Local", s_mpScreen};
    BuildOnlineScreen(&s_olChoose, s_mpScreen, "Multi-Player", 0, 2, rows);

    rows[0] = (OnlineRow){"Tunnel Host", &s_olTunnel.h};
    rows[1] = (OnlineRow){"LAN Host", &s_olLobbyHost.h};
    rows[2] = (OnlineRow){"Connect", &s_olConnect.h};
    BuildOnlineScreen(&s_olOnline, s_mpScreen, "Online", 1, 3, rows);

    rows[0] = (OnlineRow){"Back", NULL};
    BuildOnlineScreen(&s_olTunnel, s_mpScreen, "Host with Tunnel", 1, 1, rows);
    SetText(&s_olTunnel.text[0], &s_olTunnel.objs[s_olTunnel.lineObject[0]], "Not available yet");

    rows[0] = (OnlineRow){"Connect", &s_olLobbyClient.h};
    rows[1] = (OnlineRow){"Paste Address", &s_olPaste};
    rows[2] = (OnlineRow){"Back", NULL};
    BuildOnlineScreen(&s_olConnect, s_mpScreen, "Connect", 1, 3, rows);

    rows[0] = (OnlineRow){"Start Game", &s_olMode.h};
    rows[1] = (OnlineRow){"Leave", NULL};
    BuildOnlineScreen(&s_olLobbyHost, s_mpScreen, "Lobby", 3, 2, rows);

    rows[0] = (OnlineRow){"Leave", NULL};
    BuildOnlineScreen(&s_olLobbyClient, s_mpScreen, "Lobby", 2, 1, rows);

    rows[0] = (OnlineRow){"Drag Race", PackScreen(main, PACK_MP_DRAG, 26)};
    rows[1] = (OnlineRow){"Battle", PackScreen(main, PACK_MP_BATTLE, 34)};
    BuildOnlineScreen(&s_olMode, s_mpScreen, "Online Game", 0, 2, rows);

    BuildOnlineScreen(&s_olWaiting, s_mpScreen, "Please Wait", 1, 0, rows);
    SetText(&s_olWaiting.text[0], &s_olWaiting.objs[s_olWaiting.lineObject[0]], "Waiting...");
}

static OnlineScreen *OnlineScreenOf(PortOnlineScreen screen) {
    switch (screen) {
    case ONLINE_SCREEN_ONLINE: return &s_olOnline;
    case ONLINE_SCREEN_LOBBY_HOST: return &s_olLobbyHost;
    case ONLINE_SCREEN_LOBBY_CLIENT: return &s_olLobbyClient;
    case ONLINE_SCREEN_WAITING: return &s_olWaiting;
    default: return NULL;
    }
}

InterfaceHeader *Port_MenuOnlineScreen(PortOnlineScreen screen) {
    OnlineScreen *s = OnlineScreenOf(screen);

    if (s != NULL) {
        return &s->h;
    }
    return screen == ONLINE_SCREEN_MAIN ? s_main : screen == ONLINE_SCREEN_CAR_SELECT ? s_carSelectSolo : NULL;
}

/* A line's text, and the live object's if its screen is up (only when it changed:
 * setting text restarts its animation). */
static void SetOnlineLine(OnlineScreen *s, u32 line, const char *text) {
    TextCopy *c = &s->text[line];
    u32 i, k;

    if (strncmp(c->str, text, sizeof(c->str) - 1) == 0) {
        return;
    }
    strncpy(c->str, text, sizeof(c->str) - 1);
    c->str[sizeof(c->str) - 1] = 0;
    for (i = 0; i < 0x12; i++) {
        Interface *iface = &g_interfaces[i];

        if (iface->count == 0 || GETP(iface->header) != &s->h) {
            continue;
        }
        for (k = 0; k < iface->count; k++) {
            InterfaceObject *obj = &GETP(iface->objects)[k];

            if (GETP(obj->def) == &s->objs[s->lineObject[line]]) {
                InterfaceManager_SetObjectText(obj, (char **)(void *)&c->slot);
            }
        }
    }
}

void Port_MenuOnlineSetLine(PortOnlineScreen screen, u32 line, const char *text) {
    OnlineScreen *s = OnlineScreenOf(screen);

    if (s != NULL && line < s->numLines) {
        SetOnlineLine(s, line, text);
    }
}

void Port_MenuOnlineAddress(char *out, size_t size) {
    strncpy(out, s_olAddress, size - 1);
    out[size - 1] = 0;
}

/* What choosing an online screen does, or NULL for staying put. */
static InterfaceHeader *OnlineEventTarget(InterfaceHeader *target) {
    if (target == &s_olLobbyHost.h) {
        return Port_OnlineStartHost() ? target : NULL;
    }
    if (target == &s_olLobbyClient.h) {
        if (s_olAddress[0] == 0) {
            return NULL;
        }
        return Port_OnlineStartClient(s_olAddress) ? target : NULL;
    }
    if (target == &s_olPaste) {
        if (Port_TextInputPaste()) {
            SetOnlineLine(&s_olConnect, 0, s_olAddress);
        }
        return NULL;
    }
    if (target == &s_olMode.h && Port_OnlinePlayerCount() < 2) {
        return NULL;            /* nobody to play with yet */
    }
    if (target == s_carSelect && Port_OnlineActive()) {
        Port_OnlineHostSetup();
        return s_carSelectSolo;
    }
    return target;
}

/* Each frame's typing shows on the Connect screen, from Port_MenuPatch's Start
 * on until it is released. */
void Port_MenuOnlineUpdate(void) {
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (g_interfaces[i].count != 0 && GETP(g_interfaces[i].header) == &s_olConnect.h) {
            SetOnlineLine(&s_olConnect, 0, s_olAddress[0] != 0 ? s_olAddress : "Type or paste an address");
            return;
        }
    }
}

/* ---- the pause menu without Save ------------------------------------------ */

#define PAUSE_MAX_OBJECTS 64
/* The progression scripts of the multi-player drag race and battle. */
enum { PROGRESSION_MP_DRAG = 5, PROGRESSION_MP_BATTLE = 7 };

static InterfaceHeader *s_pauseHeader;
static u32 s_pauseOriginal;      /* the handle in h->objects */
static IfaceObjectDef s_pauseObjs[PAUSE_MAX_OBJECTS];
static RecordCopy s_pauseRecAbove, s_pauseRecBelow;
static AnimCopy s_pauseAnimBelow;

/* A pause menu is any screen with both Resume and Save; -1 if this isn't one. */
static s32 PauseSaveRow(const InterfaceHeader *h) {
    const IfaceObjectDef *objs = GETP(h->objects);
    s32 save = -1;
    int resume = 0;
    u32 i;

    if (objs == NULL) {
        return -1;
    }
    for (i = 0; i < h->numObjects; i++) {
        if (TextIs(&objs[i], "Resume")) {
            resume = 1;
        } else if (TextIs(&objs[i], "Save") && GETP(objs[i].record) != NULL) {
            save = (s32)i;
        }
    }
    return resume ? save : -1;
}

static void PatchPauseMenu(InterfaceHeader *h) {
    IfaceObjectDef *objs;
    IfaceRecord *rec;
    s32 id, level, save, above = -1, below = -1;
    u32 i;

    if (h == s_pauseHeader) {
        h->objects.h = s_pauseOriginal;   /* as the pack has it, whichever mode this is */
        s_pauseHeader = NULL;
    }
    StateProgressionManager_GetProgressionID(&id, &level);
    if ((!Port_OnlineActive() && id != PROGRESSION_MP_DRAG && id != PROGRESSION_MP_BATTLE) ||
        h->numObjects > PAUSE_MAX_OBJECTS || (save = PauseSaveRow(h)) < 0) {
        return;
    }
    objs = GETP(h->objects);
    rec = GETP(objs[save].record);
    for (i = 0; i < h->numObjects; i++) {
        IfaceRecord *r = GETP(objs[i].record);

        if (r != NULL && (s32)i != save && r->id == rec->next[0]) {
            above = (s32)i;
        } else if (r != NULL && (s32)i != save && r->id == rec->next[1]) {
            below = (s32)i;
        }
    }
    if (above < 0 || below < 0) {
        return;
    }
    s_pauseHeader = h;
    s_pauseOriginal = h->objects.h;
    memcpy(s_pauseObjs, objs, h->numObjects * sizeof(IfaceObjectDef));
    s_pauseObjs[save].active = 0;
    s_pauseObjs[save].record.h = 0;
    SETP(s_pauseObjs[save].conditions, &s_hideAll);
    CopyRecord(&s_pauseRecAbove, &s_pauseObjs[above], rec->next[0], GETP(objs[above].record)->next[0], rec->next[1]);
    CopyRecord(&s_pauseRecBelow, &s_pauseObjs[below], rec->next[1], rec->next[0], GETP(objs[below].record)->next[1]);
    MoveTo(&s_pauseAnimBelow, &s_pauseObjs[below], ((f32 *)GETP(GETP(objs[save].anim)->pos.keys))[1]);
    SETP(h->objects, s_pauseObjs);
}

InterfaceHeader *Port_MenuEventTarget(InterfaceHeader *target) {
    extern s8 g_bMemoryCardHWHack;

    if (target == NULL || s_main == NULL) {
        return target;
    }
    target = OnlineEventTarget(target);
    if (target == NULL) {
        return NULL;
    }
    if (target == s_reminder) {
        if (GameSettings_GetCodeVariable(CODEVAR_SETTINGS_CHANGED, 0)) {
            GameSettings_PortAutoSave();
        }
        return s_main;
    }
    if (target == s_loadScreen && GameSettings_GetMemoryCardStatus(1, 1) != 1) {
        return NULL;   /* no save to load */
    }
    if (target == s_loadProgress) {
        return GameSettings_LoadGame() ? s_main : target;
    }
    if (target == s_saveProgress) {
        g_bMemoryCardHWHack = 0;
        return GameSettings_SaveGame() ? s_main : target;
    }
    return target;
}

static void PatchMainMenu(InterfaceHeader *h) {
    IfaceObjectDef *objs = GETP(h->objects);
    IfaceObjectDef *entry;
    InterfaceHeader *quit = QuitScreen(h);
    IfaceText *warning;
    u32 i;

    FindCardScreens(h);

    if (quit == NULL) {
        Port_Log("menu: quit screen not found, no Quit Game entry");
        return;
    }
    /* Yes/No: Start does what A does */
    for (i = 0; i < 2; i++) {
        IfaceRecord *rec = GETP(GETP(quit->objects)[i == 0 ? QUIT_YES : QUIT_NO].record);
        if (rec->events.count == 1 && GETP(rec->events.events)[0].type == EVENT_A) {
            s_quitEvents[i][0] = GETP(rec->events.events)[0];
            s_quitEvents[i][1] = s_quitEvents[i][0];
            s_quitEvents[i][1].type = EVENT_START;
            rec->events.count = 2;
            SETP(rec->events.events, s_quitEvents[i]);
        }
    }
    warning = GETP(GETP(quit->objects)[QUIT_WARNING].sub);
    SETP(s_quitWarning, "Warning:|Any unsaved progress will be lost.");
    SETP(warning->strings, &s_quitWarning);

    memcpy(s_mainObjs, objs, MAIN_NUM_OBJECTS * sizeof(IfaceObjectDef));
    for (i = MAIN_ROWS; i < MAIN_ROWS_END; i++) {
        MoveRow(&s_mainRowAnim[i - MAIN_ROWS], &s_mainObjs[i]);
    }
    entry = &s_mainObjs[MAIN_NUM_OBJECTS];
    *entry = objs[MAIN_EXTRAS];
    MoveTo(&s_desktopAnim, entry, ROW_Y + ROW_NEW * ROW_STEP);
    SetText(&s_desktopText, entry, "Quit Game");
    CopyRecord(&s_desktopRec, entry, REC_DESKTOP, REC_EXTRAS, REC_ONE_PLAYER);
    for (i = 0; i < s_desktopRec.rec.events.count; i++) {
        if (s_desktopRec.defs[i].type == 1) {
            SETP(s_desktopRec.defs[i].target, quit);
        }
    }
    /* into the ring: Extras -> Quit Game -> One Player */
    GETP(objs[MAIN_EXTRAS].record)->next[1] = REC_DESKTOP;
    GETP(objs[MAIN_ROWS].record)->next[0] = REC_DESKTOP;
    h->numObjects = MAIN_NUM_OBJECTS + 1;
    SETP(h->objects, s_mainObjs);

    /* Multi-Player: to the new Online / Local screen */
    BuildOnlineScreens(h);
    if (s_mpScreen != NULL) {
        RecordCopy *rec = CopyRecord(&s_mainMultiRec, &s_mainObjs[MAIN_MULTI_PLAYER], REC_MULTI_PLAYER,
                                     REC_ONE_PLAYER, REC_LOAD_GAME);

        for (i = 0; i < rec->rec.events.count; i++) {
            if (rec->defs[i].type == EVENT_GOTO) {
                SETP(rec->defs[i].target, &s_olChoose.h);
            }
        }
    } else {
        Port_Log("menu: multi-player screens not found, no online menus");
    }
}

void Port_MenuPatch(InterfaceHeader *h) {
    IfaceObjectDef *objs;
    IfaceObjectDef *entry;
    u32 i;

    if (h == NULL) {
        return;
    }
    Port_CarouselPatch(h);   /* world, car and weapon select: more windows */
    PatchPauseMenu(h);
    if (h == &s_olConnect.h) {
        if (s_olAddress[0] == 0) {
            Port_LoadOnlineAddress(s_olAddress, sizeof(s_olAddress));
        }
        Port_TextInputStart(s_olAddress, sizeof(s_olAddress));
        return;
    }
    if (h == &s_gfx) {
        RefreshModes();
        return;
    }
    if (IsMainMenu(h)) {
        PatchMainMenu(h);
        return;
    }
    if (h == s_loadScreen || h == s_saveScreen) {
        PatchCardScreen(h);
        return;
    }
    if (!IsOptions(h)) {
        return;
    }
    objs = GETP(h->objects);
    BuildGraphicsScreen(h, objs);

    memcpy(s_optObjs, objs, OPT_NUM_OBJECTS * sizeof(IfaceObjectDef));
    for (i = OPT_ROWS; i < OPT_ROWS_END; i++) {
        MoveRow(&s_optRowAnim[i - OPT_ROWS], &s_optObjs[i]);
    }
    entry = &s_optObjs[OPT_NUM_OBJECTS];
    *entry = objs[OPT_CONTROLLER];
    MoveTo(&s_entryAnim, entry, ROW_Y + ROW_NEW * ROW_STEP);
    SetText(&s_entryText, entry, "Graphics");
    CopyRecord(&s_entryRec, entry, REC_GRAPHICS, REC_SFX, REC_CONTROLLER);
    for (i = 0; i < s_entryRec.rec.events.count; i++) {
        if (s_entryRec.defs[i].type == 1) {
            SETP(s_entryRec.defs[i].target, &s_gfx);
        }
    }
    /* into the ring: SFX -> Graphics -> Controller */
    GETP(objs[OPT_SFX].record)->next[1] = REC_GRAPHICS;
    GETP(objs[OPT_CONTROLLER].record)->next[0] = REC_GRAPHICS;
    if (GETP(GETP(h->vars)->vars)[OPT_VAR_SELECTED].max < REC_GRAPHICS) {
        GETP(GETP(h->vars)->vars)[OPT_VAR_SELECTED].max = REC_GRAPHICS;
    }
    h->numObjects = OPT_NUM_OBJECTS + 1;
    SETP(h->objects, s_optObjs);
}

void Port_MenuReleased(InterfaceHeader *h) {
    if (h == &s_gfx) {
        Port_GfxApply();
    }
    if (h == &s_olConnect.h) {
        Port_TextInputStop();
    }
}
