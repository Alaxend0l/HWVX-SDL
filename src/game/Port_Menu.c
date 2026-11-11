/* Port menu entries: "Quit Game" in the main menu, and a "Graphics"
 * entry in the Options screen with the Graphics screen it opens (resolution,
 * windowed/fullscreen, vsync on/off, a max frame rate of 30/60/90/120/144/240,
 * and gamma and FOV bars: src/port/gfx_settings.c). Port-only file.
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
 *   well as A, like the other menus' rows. With graphics.cfg
 *   "mainmenu=original" (HWVX_ORIGINAL_MAINMENU=1, Port_OriginalMainMenu)
 *   the main menu is left as the release has it: no Quit Game, no Online.
 * - Options: "Graphics" (a copy of "Controller") opens the Graphics screen.
 * - Memory card screens (saves are files on PC, always "slot A"): Load Game
 *   and Save Game start on slot A's file list (no Slot A/B picker), without
 *   the card warnings, and B leaves them. Port_MenuEventTarget skips the rest
 *   where an interface switch is decided: "Loading..."/"Saving..." load or
 *   save at once and go to the main menu (their failure screens remain), Load
 *   Game does nothing without a save, and leaving Options, where the "Be sure
 *   to save" reminder came up after a setting changed, autosaves instead
 *   (GameSettings_PortAutoSave).
 * - Graphics: a copy of the Options screen with the same objects (object
 *   indices in actions stay valid). The rows it doesn't use are hidden; the
 *   "Difficulty" row becomes "Resolution" and the "Sound Mode" row "Display",
 *   moved to the top two row positions. The PC release's Options screen has
 *   no Sound Mode row: there Display is a copy of Difficulty, added after the
 *   screen's own objects (FindOptions finds the rows of either by their
 *   texts; OptScreen). Their variables read the port's code
 *   variables (PORT_CODEVAR_*), their left/right actions write them. B goes
 *   back like the Controller screen's. Leaving it applies the settings
 *   (Port_MenuReleased). Gamma and FOV are more copies of the Music row, its
 *   volume bar and all (AddBarRow), added after the 40.
 *
 * - Multi-Player: a new screen in front of the game's, Online or Local (the game's
 *   own). Online has Host with Tunnel, Host via LAN and Connect, then a lobby; see
 *   "Online screens" below and Port_Online.c.
 *
 * - Battle Options online: no Opponents row (there are no AI cars online), the rows
 *   below moving up. Local play keeps it, the same screen: a copy made each time.
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

/* The Options screen ("Game Settings"), found by its rows' texts rather than
 * at fixed indices: the GameCube and PC packs lay it out differently.
 *
 * GameCube (HW_IPack.xgc, 40 objects): 13 title, 16 Controller, 17 Cheats
 * (18 its greyed label), rows of four from 19 - label, value at +1, arrows
 * at +2/+3 - Difficulty 19, Sound Mode 23, Music 27, SFX 31; 35 the hidden
 * debug row (record 7), 37-39 the footers (child interfaces): 38 "Last/Next"
 * and "B Back", shown for the records in its conditions (3, 6, 7), 39
 * "Soft/Loud", for 4 and 5. Rows 26 apart from y 198.
 *
 * PC (HW_IPack.xpc, 33 objects): the same rows less Sound Mode, and three
 * fewer images before them: 10 title, 13 Controller, 14/15 Cheats,
 * Difficulty 16, Music 20, SFX 24, debug 28, footers 30-32 (31's conditions
 * still list Sound Mode's record 6). Rows 31 apart from y 203.
 *
 * Both: records Controller 1, Cheats 2, Difficulty 3, Music 4, SFX 5, Sound
 * Mode 6 (GameCube only), debug 7; variables 0 difficulty (code var 0xC8, the
 * Difficulty row's step var), 1 the selected record (type 3), 2 code var
 * 0x6A, 3 sound mode (code var 0xD6 - the PC keeps it without a row). All
 * read from the data (FindOptions) except the ids the port gives its own
 * rows (REC_GRAPHICS ...), checked to be free. */
#define NONE 0xFFFFFFFFu
#define OPT_MAX_OBJECTS 48
#define CODEVAR_SOUND_MODE 0xD6

typedef struct RowSpacing {
    f32 packFirst, packStep;    /* the pack's first row and the gap to the next */
    f32 first, step;            /* the port's, with one more row */
} RowSpacing;

typedef struct OptScreen {
    u32 numObjects;
    u32 title, controller, cheats, cheatsLabel;
    u32 difficulty, sound, music, sfx;  /* rows of four; sound NONE on PC */
    u32 debug;                  /* NONE if absent */
    u32 footerLeftRight;        /* "Last/Next" and "B Back", for the rows listed in its conditions */
    u32 footerVolume;           /* "Soft/Loud" and "B Back": the Music and SFX rows'; NONE if absent */
    u8 recController, recDifficulty, recSound, recMusic, recSfx;
    u16 varDifficulty, varSelected, varSound;
    u16 varVSync, varFps;       /* the Graphics screen's, past the pack's */
    RowSpacing rows;
} OptScreen;
#define NO_VAR 0xFFFF

/* The port's own record ids, past the pack's 1-7 (the Display row takes the
 * Sound Mode row's 6 on PC, where that row is gone). */
enum { REC_SOUND = 6, REC_GRAPHICS = 8, REC_GAMMA = 9, REC_FOV = 10 };

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
/* The PC's Options screen has five rows, 31 apart from 203: with the port's
 * sixth they go back to the GameCube's gap, around the same centre. */
#define ROW_CENTRE_Y 265.0f

/* The main menu, found by its title and rows' texts (FindMainMenu): the
 * GameCube's and PS2's (26 objects) and the PC's (21) differ. Rows are texts
 * with a record, some with a label (the same text without one) under them. */
enum {
    MAIN_ONE_PLAYER,
    MAIN_MULTI_PLAYER,          /* not on PC */
    MAIN_LOAD,
    MAIN_SAVE,
    MAIN_OPTIONS,
    MAIN_EXTRAS,
    MAIN_EXIT,                  /* PC: "Exit to Desktop"; the port's "Quit Game" elsewhere */
    MAIN_NUM_ROWS,
    MAIN_MAX_OBJECTS = 32,
    MAIN_MAX_MOVED = 16,        /* the rows' texts, labels too */
};
static const char *const s_mainRowText[MAIN_NUM_ROWS] = {
    "One Player", "Multi-Player", "Load Game", "Save Game", "Options", "Extras", "Exit to Desktop",
};

typedef struct MainMenu {
    u32 numObjects;
    u32 row[MAIN_NUM_ROWS];     /* the row objects, NONE if absent */
} MainMenu;

/* IfaceEvent.type: the buttons menu rows answer to */
enum { EVENT_A = 0x4, EVENT_START = 0x15 };
/* The port's main menu rows: Quit Game takes the id after the GameCube's
 * last (Extras 5, Save Game 6), Multi-Player on PC the first one free. */
enum { REC_DESKTOP = 7 };

/* The Load/Save Game screens with a memory card slot picker (GameCube, PS2):
 * var 4 is the phase (0: slot picker, 1: file list), object 11 the card
 * warnings. The PC's are the file list alone. */
enum { CARD_VAR_PHASE = 4, CARD_WARNING = 11 };
enum { CODEVAR_SETTINGS_CHANGED = 0xCA, CODEVAR_DEVICE = 0x1F7, CODEVAR_FILE_INDEX = 0x1F8 };
enum { EVENT_GOTO = 1, EVENT_BACK = 2, EVENT_NOTHING = 0xC, EVENT_GOTO_PARENT = 0x20 };

void GameSettings_PortAutoSave(void);
s32 GameSettings_GetMemoryCardStatus(s32 device, s8 bForce);
u8 GameSettings_LoadGame(void);
u8 GameSettings_SaveGame(void);
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

static IfaceObjectDef s_mainObjs[MAIN_MAX_OBJECTS + 1];
static AnimCopy s_mainRowAnim[MAIN_MAX_MOVED], s_desktopAnim;
static TextCopy s_desktopText, s_multiText;
static RecordCopy s_desktopRec;
static PTR32(char) s_quitWarning;
static InterfaceHeader *s_main, *s_loadScreen, *s_loadProgress, *s_saveScreen, *s_saveProgress, *s_reminder;
static IfaceEvent s_quitEvents[2][2];

static OptScreen s_opt;
static IfaceObjectDef s_optObjs[OPT_MAX_OBJECTS + 1];
static AnimCopy s_optRowAnim[24];       /* Controller .. SFX's last arrow */
static AnimCopy s_entryAnim;
static TextCopy s_entryText;
static RecordCopy s_entryRec;

static InterfaceHeader s_gfx;
/* the Options screen's objects, then on PC a copy of the Difficulty row for
 * Display (AddDisplayRow), then Gamma and FOV (AddBarRow) */
static IfaceObjectDef s_gfxObjs[OPT_MAX_OBJECTS + 12];
static u32 s_gfxResValue;               /* the Resolution row's value object */
static IfaceConditionList s_displayArrowList[2];
static IfaceCondition s_displayArrowCond[2];
static IfaceVarList s_gfxVarList;
static IfaceVar s_gfxVars[8];
static IfaceCodeVar s_gfxCode[4];
static IfaceConditionList s_hideAll;   /* type 0 "hide if all", no conditions: always hidden */
static AnimCopy s_rowAnim[16];
static TextCopy s_gfxText[5];
static RecordCopy s_gfxRec[4];
static ActionCopy s_gfxAction[4];
static ListCopy s_resList, s_displayList, s_vsyncList, s_fpsList;

/* AddBarRow's copies: the arrows only show for their row's own record id. */
typedef struct BarRow {
    AnimCopy anim[4];
    TextCopy label;
    RecordCopy rec;
    IfaceConditionList arrowList[2];
    IfaceCondition arrowCond[2];
} BarRow;
static BarRow s_gammaRow, s_fovRow;
static IfaceConditionList s_footerList;
static IfaceCondition s_footerCond[12];

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

static f32 PosY(const IfaceObjectDef *d) {
    return ((f32 *)GETP(GETP(d->anim)->pos.keys))[1];
}

/* d, an object of the pack's row k, to row k of the respaced menu. */
static void MoveRow(AnimCopy *c, IfaceObjectDef *d, const RowSpacing *sp) {
    s32 k = (s32)((PosY(d) - sp->packFirst) / sp->packStep + 0.5f);

    MoveTo(c, d, sp->first + k * sp->step);
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

/* Objects at..at+3 of the Graphics screen: a copy of the pack's Music row (objs)
 * at height y, whose bar shows and whose left/right change a port code variable,
 * by the pack's own delta. The row's other events (they mark the game's settings
 * changed, for the "Be sure to save" reminder) go: graphics.cfg isn't in the save. */
/* The arrows d[2], d[3] of a copied row show for record `id` (the pack's
 * show only for their own row's). */
static void SetArrowConds(IfaceObjectDef *d, IfaceConditionList *lists, IfaceCondition *conds, u8 id) {
    u32 i;

    for (i = 0; i < 2; i++) {
        const IfaceConditionList *list = GETP(d[2 + i].conditions);

        lists[i] = *list;
        conds[i] = *GETP(list->conds);
        conds[i].value = id;
        SETP(lists[i].conds, &conds[i]);
        SETP(d[2 + i].conditions, &lists[i]);
    }
}

static void AddBarRow(BarRow *row, const IfaceObjectDef *objs, u32 at, const char *name, u8 role, u32 codeVar,
                      u8 id, u8 up, u8 down, f32 y) {
    IfaceObjectDef *d = &s_gfxObjs[at];
    u32 i;

    for (i = 0; i < 4; i++) {
        d[i] = objs[s_opt.music + i];
        MoveTo(&row->anim[i], &d[i], y);
    }
    SetText(&row->label, &d[0], name);
    d[1].role = role;
    CopyRecord(&row->rec, &d[0], id, up, down);
    for (i = 0; i < row->rec.rec.events.count; i++) {
        if (row->rec.defs[i].type == 0x16) {
            row->rec.defs[i].type = PORT_EVENT_SLIDER;
            row->rec.defs[i].id = (u16)codeVar;
        } else {
            row->rec.defs[i].type = EVENT_NOTHING;
            row->rec.defs[i].target.h = 0;
        }
    }
    SetArrowConds(d, row->arrowList, row->arrowCond, id);
}

/* The screen's footer for left/right rows also shows for the rows made from the
 * Music and SFX rows, and for the bars; "Soft/Loud" means nothing here. */
static void SetLeftRightFooter(const IfaceObjectDef *objs) {
    const u8 rows[] = { s_opt.recMusic, s_opt.recSfx, REC_GAMMA, REC_FOV, s_opt.recSound };
    const IfaceConditionList *list = GETP(objs[s_opt.footerLeftRight].conditions);
    u32 n = list->count, i, k;

    if (n > 12 - sizeof(rows)) {
        n = 12 - sizeof(rows);
    }
    memcpy(s_footerCond, GETP(list->conds), n * sizeof(IfaceCondition));
    /* the ones it doesn't list yet (all but Display's 6 on both packs) */
    for (i = 0, k = n; i < sizeof(rows); i++) {
        u32 j;

        for (j = 0; j < k && s_footerCond[j].value != rows[i]; j++) {
        }
        if (j == k) {
            s_footerCond[k] = s_footerCond[0];
            s_footerCond[k].value = rows[i];
            k++;
        }
    }
    s_footerList = *list;
    s_footerList.count = k;
    SETP(s_footerList.conds, s_footerCond);
    SETP(s_gfxObjs[s_opt.footerLeftRight].conditions, &s_footerList);
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
    SetList(&s_resList, &s_gfxObjs[s_gfxResValue], s_opt.varDifficulty, names, n);
    s_gfxVars[s_opt.varDifficulty].max = (s32)n - 1;
}

/* PC: the Graphics screen's Display row, where the GameCube's reuses the
 * Sound Mode row the PC's Options screen lacks - a copy of its Difficulty row
 * (also label + value text) as four more objects at `at`, with the record id
 * the Sound Mode row had (REC_SOUND: still in the PC footer's conditions) and
 * arrows showing for it. Its step is the pack's Difficulty step on var 0, so
 * it goes: SetActions gives the row its own, on the display variable. */
static u32 AddDisplayRow(const IfaceObjectDef *objs, u32 at) {
    IfaceObjectDef *d = &s_gfxObjs[at];
    u32 i;

    for (i = 0; i < 4; i++) {
        d[i] = objs[s_opt.difficulty + i];
    }
    CopyRecord(&s_gfxRec[1], &d[0], REC_SOUND, s_opt.recDifficulty, s_opt.recMusic);
    s_gfxRec[1].rec.numSteps = 0;
    s_gfxRec[1].rec.steps.h = 0;
    SetArrowConds(d, s_displayArrowList, s_displayArrowCond, REC_SOUND);
    return at;
}

static void BuildGraphicsScreen(InterfaceHeader *opt, const IfaceObjectDef *objs) {
    static const char *const displayNames[2] = { "Windowed", "Fullscreen" };
    const char *optNames[PORT_MAX_CHOICES];
    const OptScreen *o = &s_opt;
    const u32 hide[] = {
        o->controller, o->cheats, o->cheatsLabel, o->debug, o->footerVolume,
    };
    IfaceVarList *vars = GETP(opt->vars);
    InterfaceHeader *ctrl;
    u32 sound, gamma, i, n;

    s_gfx = *opt;
    memcpy(s_gfxObjs, objs, o->numObjects * sizeof(IfaceObjectDef));
    SETP(s_gfx.objects, s_gfxObjs);
    /* B: back, as on the Controller screen (the one its row goes to) */
    ctrl = (InterfaceHeader *)GETP(GETP(GETP(GETP(objs[o->controller].record)->events.events)[0].def)->target);
    s_gfx.events = ctrl->events;

    s_gfxVarList.count = vars->count;
    memcpy(s_gfxVars, GETP(vars->vars), s_gfxVarList.count * sizeof(IfaceVar));
    /* The pack's list ends at the sound mode var. Extend the copy with
     * clones of it (a code var of the right shape, which SetCodeVar below
     * retargets) so the VSync and Max FPS rows have vars of their own. */
    while (s_gfxVarList.count <= o->varFps) {
        s_gfxVars[s_gfxVarList.count] = s_gfxVars[o->varSound];
        s_gfxVarList.count++;
    }
    SETP(s_gfxVarList.vars, s_gfxVars);
    SETP(s_gfx.vars, &s_gfxVarList);
    SetCodeVar(&s_gfxVars[o->varDifficulty], &s_gfxCode[0], PORT_CODEVAR_RESOLUTION, 0);
    SetCodeVar(&s_gfxVars[o->varSound], &s_gfxCode[1], PORT_CODEVAR_DISPLAY, 1);

    s_hideAll.type = 0;
    s_hideAll.count = 0;
    for (i = 0; i < sizeof(hide) / sizeof(hide[0]); i++) {
        if (hide[i] != NONE) {
            s_gfxObjs[hide[i]].active = 0;
            SETP(s_gfxObjs[hide[i]].conditions, &s_hideAll);
        }
    }
    SetText(&s_gfxText[0], &s_gfxObjs[o->title], "Graphics");

    /* the rows, moved to the top: Difficulty, Sound Mode (on PC a copy of
     * Difficulty, past the screen's own objects), Music, SFX; then Gamma and
     * FOV past those */
    sound = o->sound != NONE ? o->sound : AddDisplayRow(objs, o->numObjects);
    gamma = o->sound != NONE ? o->numObjects : o->numObjects + 4;
    s_gfx.numObjects = gamma + 8;
    for (i = 0; i < 4; i++) {
        MoveTo(&s_rowAnim[i], &s_gfxObjs[o->difficulty + i], ROW_Y);
        MoveTo(&s_rowAnim[4 + i], &s_gfxObjs[sound + i], ROW_Y + ROW_STEP);
        MoveTo(&s_rowAnim[8 + i], &s_gfxObjs[o->music + i], ROW_Y + 2 * ROW_STEP);
        MoveTo(&s_rowAnim[12 + i], &s_gfxObjs[o->sfx + i], ROW_Y + 3 * ROW_STEP);
    }
    s_gfxResValue = o->difficulty + 1;
    SetText(&s_gfxText[1], &s_gfxObjs[o->difficulty], "Resolution");
    SetActions(CopyRecord(&s_gfxRec[0], &s_gfxObjs[o->difficulty], o->recDifficulty, REC_FOV, REC_SOUND),
               &s_gfxAction[0], PORT_CODEVAR_RESOLUTION, o->varDifficulty);
    SetText(&s_gfxText[2], &s_gfxObjs[sound], "Display");
    if (o->sound != NONE) {
        CopyRecord(&s_gfxRec[1], &s_gfxObjs[sound], REC_SOUND, o->recDifficulty, o->recMusic);
    }
    SetActions(&s_gfxRec[1], &s_gfxAction[1], PORT_CODEVAR_DISPLAY, o->varSound);
    SetList(&s_displayList, &s_gfxObjs[sound + 1], o->varSound, displayNames, 2);

    /* VSync and Max FPS: the Music and SFX rows (their packs' own rows,
     * hidden before), relabelled and wired to the port's code vars. The
     * ring runs Resolution - Display - VSync - Max FPS - Gamma - FOV -
     * Resolution. */
    SetCodeVar(&s_gfxVars[o->varVSync], &s_gfxCode[2], PORT_CODEVAR_VSYNC, 1);
    SetCodeVar(&s_gfxVars[o->varFps], &s_gfxCode[3], PORT_CODEVAR_MAXFPS,
               (s32)Port_GfxOptionCount(PORT_CODEVAR_MAXFPS) - 1);
    SetText(&s_gfxText[3], &s_gfxObjs[o->music], "VSync");
    SetActions(CopyRecord(&s_gfxRec[2], &s_gfxObjs[o->music], o->recMusic, REC_SOUND, o->recSfx),
               &s_gfxAction[2], PORT_CODEVAR_VSYNC, o->varVSync);
    for (n = 0; n < Port_GfxOptionCount(PORT_CODEVAR_VSYNC) && n < PORT_MAX_CHOICES; n++) {
        optNames[n] = Port_GfxOptionName(PORT_CODEVAR_VSYNC, n);
    }
    SetListAsText(&s_vsyncList, &s_gfxObjs[o->music + 1], o->varVSync, optNames, n,
                  &s_gfxObjs[sound + 1], &s_rowAnim[9], ROW_Y + 2 * ROW_STEP);
    SetText(&s_gfxText[4], &s_gfxObjs[o->sfx], "Max FPS");
    SetActions(CopyRecord(&s_gfxRec[3], &s_gfxObjs[o->sfx], o->recSfx, o->recMusic, REC_GAMMA),
               &s_gfxAction[3], PORT_CODEVAR_MAXFPS, o->varFps);
    for (n = 0; n < Port_GfxOptionCount(PORT_CODEVAR_MAXFPS) && n < PORT_MAX_CHOICES; n++) {
        optNames[n] = Port_GfxOptionName(PORT_CODEVAR_MAXFPS, n);
    }
    SetListAsText(&s_fpsList, &s_gfxObjs[o->sfx + 1], o->varFps, optNames, n,
                  &s_gfxObjs[sound + 1], &s_rowAnim[13], ROW_Y + 3 * ROW_STEP);
    Port_Log("menu: graphics screen built (resolution, display, vsync, max fps, gamma, fov)");

    AddBarRow(&s_gammaRow, objs, gamma, "Gamma", PORT_ROLE_GAMMA_BAR, PORT_CODEVAR_GAMMA,
              REC_GAMMA, o->recSfx, REC_FOV, ROW_Y + 4 * ROW_STEP);
    AddBarRow(&s_fovRow, objs, gamma + 4, "FOV", PORT_ROLE_FOV_BAR, PORT_CODEVAR_FOV,
              REC_FOV, REC_GAMMA, o->recDifficulty, ROW_Y + 5 * ROW_STEP);
    SetLeftRightFooter(objs);
    s_gfxVars[o->varSelected].max = REC_FOV;
    RefreshModes();
}

/* The first object whose text is s, with a record (a row) or without. */
static u32 FindText(const IfaceObjectDef *objs, u32 n, const char *s, int row) {
    u32 i;

    for (i = 0; i < n; i++) {
        if (TextIs(&objs[i], s) && (GETP(objs[i].record) != NULL) == row) {
            return i;
        }
    }
    return NONE;
}

/* The child interface (a footer) shown when record `id` is selected. */
static u32 FindFooter(const IfaceObjectDef *objs, u32 n, u8 id) {
    u32 i, j;

    for (i = 0; i < n; i++) {
        const IfaceConditionList *list = GETP(objs[i].conditions);

        if (objs[i].type != IFACE_DEF_CHILD || list == NULL || GETP(list->conds) == NULL) {
            continue;
        }
        for (j = 0; j < list->count; j++) {
            if (GETP(list->conds)[j].type == 1 && GETP(list->conds)[j].value == id) {
                return i;
            }
        }
    }
    return NONE;
}

static u8 RecId(const IfaceObjectDef *objs, u32 i) {
    return GETP(objs[i].record)->id;
}

/* The Options screen, still as in the pack? Fills `o` with where its parts
 * are (see OptScreen). The GameCube's must be its 40 objects, as it always
 * had to; the PC's is told by its missing Sound Mode row. */
static int FindOptions(InterfaceHeader *h, OptScreen *o) {
    IfaceObjectDef *objs = GETP(h->objects);
    IfaceVarList *vl = GETP(h->vars);
    IfaceRecord *diff, *snd;
    u32 n = h->numObjects, i, rows;

    if (objs == NULL || objs == s_optObjs || vl == NULL || n > OPT_MAX_OBJECTS) {
        return 0;
    }
    memset(o, 0, sizeof(*o));
    o->numObjects = n;
    o->title = FindText(objs, n, "Game Settings", 0);
    o->controller = FindText(objs, n, "Controller", 1);
    o->cheats = FindText(objs, n, "Cheats", 1);
    o->cheatsLabel = FindText(objs, n, "Cheats", 0);
    o->difficulty = FindText(objs, n, "Difficulty", 1);
    o->sound = FindText(objs, n, "Sound Mode", 1);
    o->music = FindText(objs, n, "Music", 1);
    o->sfx = FindText(objs, n, "SFX", 1);
    if (o->title == NONE || o->controller == NONE || o->difficulty == NONE || o->music == NONE ||
        o->sfx == NONE || o->difficulty + 4 > n || o->music + 4 > n || o->sfx + 4 > n ||
        (o->sound != NONE && (n != 40 || o->sound + 4 > n))) {
        return 0;
    }
    o->recController = RecId(objs, o->controller);
    o->recDifficulty = RecId(objs, o->difficulty);
    o->recMusic = RecId(objs, o->music);
    o->recSfx = RecId(objs, o->sfx);
    o->recSound = REC_SOUND;
    /* the hidden debug row: the first row after SFX's */
    o->debug = NONE;
    for (i = o->sfx + 4; i < n && o->debug == NONE; i++) {
        if (GETP(objs[i].record) != NULL) {
            o->debug = i;
        }
    }
    /* the port's ids must be free: no row but Sound Mode's may have them */
    for (i = 0; i < n; i++) {
        IfaceRecord *r = GETP(objs[i].record);

        if (r != NULL && i != o->sound &&
            (r->id == REC_SOUND || r->id == REC_GRAPHICS || r->id == REC_GAMMA || r->id == REC_FOV)) {
            Port_Log("menu: Options object %u has record id %u, no Graphics entry", i, r->id);
            return 0;
        }
    }
    if (o->sound != NONE && RecId(objs, o->sound) != REC_SOUND) {
        return 0;
    }
    o->footerLeftRight = FindFooter(objs, n, o->recDifficulty);
    o->footerVolume = FindFooter(objs, n, o->recMusic);
    if (o->footerLeftRight == NONE) {
        return 0;
    }

    /* variables: Difficulty's step var, the selected-record var, the sound
     * mode var (Sound Mode's step var, or on PC the var still reading code
     * var 0xD6), then two more for VSync and Max FPS */
    diff = GETP(objs[o->difficulty].record);
    if (diff->numSteps == 0 || GETP(diff->steps)[0].var >= vl->count) {
        return 0;
    }
    o->varDifficulty = (u16)GETP(diff->steps)[0].var;
    o->varSelected = o->varSound = NO_VAR;
    for (i = 0; i < vl->count; i++) {
        IfaceVar *v = &GETP(vl->vars)[i];

        if (v->type == IFACE_VAR_SELECTED && o->varSelected == NO_VAR) {
            o->varSelected = (u16)i;
        }
        if (o->sound == NONE && v->type == IFACE_VAR_CODE && GETP(v->code) != NULL &&
            GETP(v->code)->id == CODEVAR_SOUND_MODE) {
            o->varSound = (u16)i;
        }
    }
    if (o->sound != NONE) {
        snd = GETP(objs[o->sound].record);
        if (snd->numSteps != 0 && GETP(snd->steps)[0].var < vl->count) {
            o->varSound = (u16)GETP(snd->steps)[0].var;
        }
    }
    if (o->varSelected == NO_VAR || o->varSound == NO_VAR || vl->count > 6) {
        return 0;
    }
    o->varVSync = (u16)vl->count;
    o->varFps = (u16)(vl->count + 1);

    /* the rows' spacing: the GameCube's seven rows as they always were, the
     * PC's six at the GameCube's own gap, centred where its five were */
    rows = o->sound != NONE ? 7 : 6;
    o->rows.packFirst = PosY(&objs[o->controller]);
    o->rows.packStep = o->cheats != NONE ? PosY(&objs[o->cheats]) - o->rows.packFirst : PACK_ROW_STEP_Y;
    if (o->rows.packStep <= 0.0f || o->sfx + 4 - o->controller > sizeof(s_optRowAnim) / sizeof(s_optRowAnim[0])) {
        return 0;
    }
    if (rows == 7) {
        o->rows.first = ROW_Y;
        o->rows.step = ROW_STEP;
    } else {
        o->rows.step = PACK_ROW_STEP_Y;
        o->rows.first = ROW_CENTRE_Y - (rows - 1) * PACK_ROW_STEP_Y / 2.0f;
    }
    return 1;
}

/* ---- Finding screens by content ----------------------------------------------
 * The three releases' menu packs hold the same kinds of screens at different
 * offsets, some changed or missing (docs/menus/README.md), so screens are
 * found by what they show and where their rows lead, from the main menu, the
 * root of the menu pack: every menu screen is reached from it through go-to
 * events and child slots. */

/* The first go-to target (event types 1 and 0x20) of an event list. */
static InterfaceHeader *EventTarget(const IfaceEventList *list) {
    u32 i;

    for (i = 0; i < list->count && GETP(list->events) != NULL; i++) {
        const IfaceEventDef *def = GETP(GETP(list->events)[i].def);

        if (def != NULL && (def->type == EVENT_GOTO || def->type == EVENT_GOTO_PARENT) && GETP(def->target) != NULL) {
            return (InterfaceHeader *)GETP(def->target);
        }
    }
    return NULL;
}

/* Where row i leads (NONE: nowhere). */
static InterfaceHeader *RowTarget(const InterfaceHeader *h, u32 i) {
    const IfaceRecord *r;

    if (h == NULL || i == NONE || i >= h->numObjects || (r = GETP(GETP(h->objects)[i].record)) == NULL) {
        return NULL;
    }
    return EventTarget(&r->events);
}

/* Where the row showing `text` leads. */
static InterfaceHeader *RowTargetOf(const InterfaceHeader *h, const char *text) {
    return h != NULL ? RowTarget(h, FindText(GETP(h->objects), h->numObjects, text, 1)) : NULL;
}

/* Where the first row with somewhere to go leads (a world select's entry). */
static InterfaceHeader *FirstRowTarget(const InterfaceHeader *h) {
    u32 i;

    for (i = 0; h != NULL && i < h->numObjects; i++) {
        InterfaceHeader *t = RowTarget(h, i);

        if (t != NULL) {
            return t;
        }
    }
    return NULL;
}

/* Where the screen's own events lead (an options screen's A: its world select). */
static InterfaceHeader *ScreenTarget(const InterfaceHeader *h) {
    return h != NULL ? EventTarget(&h->events) : NULL;
}

static int HasText(const InterfaceHeader *h, const char *s, int row) {
    return GETP(h->objects) != NULL && FindText(GETP(h->objects), h->numObjects, s, row) != NONE;
}

/* The first text starting with s. */
static u32 FindTextPrefix(const InterfaceHeader *h, const char *s) {
    const IfaceObjectDef *objs = GETP(h->objects);
    u32 i;

    for (i = 0; objs != NULL && i < h->numObjects; i++) {
        const char *t = DefText(&objs[i]);

        if (t != NULL && strncmp(t, s, strlen(s)) == 0) {
            return i;
        }
    }
    return NONE;
}

#define WALK_MAX 192

static void WalkAdd(InterfaceHeader **seen, u32 *n, InterfaceHeader *h) {
    u32 i;

    if (h == NULL || GETP(h->objects) == NULL || *n == WALK_MAX) {
        return;
    }
    for (i = 0; i < *n; i++) {
        if (seen[i] == h) {
            return;
        }
    }
    seen[(*n)++] = h;
}

static void WalkEvents(InterfaceHeader **seen, u32 *n, const IfaceEventList *list) {
    u32 i;

    for (i = 0; i < list->count && GETP(list->events) != NULL; i++) {
        const IfaceEventDef *def = GETP(GETP(list->events)[i].def);

        if (def != NULL && (def->type == EVENT_GOTO || def->type == EVENT_GOTO_PARENT || def->type == 0x1F)) {
            WalkAdd(seen, n, (InterfaceHeader *)GETP(def->target));
        }
    }
}

/* The first screen reachable from `root` that `test` takes, NULL if none. */
static InterfaceHeader *FindScreen(InterfaceHeader *root, int (*test)(const InterfaceHeader *h)) {
    static InterfaceHeader *seen[WALK_MAX];
    u32 n = 0, k, i;

    WalkAdd(seen, &n, root);
    for (k = 0; k < n; k++) {
        InterfaceHeader *h = seen[k];
        const IfaceObjectDef *objs = GETP(h->objects);

        if (test(h)) {
            return h;
        }
        WalkEvents(seen, &n, &h->events);
        for (i = 0; i < h->numObjects; i++) {
            const IfaceRecord *r = GETP(objs[i].record);

            if ((objs[i].type == IFACE_DEF_CHILD || objs[i].type == IFACE_DEF_CHILD_SYNC) && GETP(objs[i].child) != NULL) {
                WalkAdd(seen, &n, (InterfaceHeader *)GETP(*GETP(objs[i].child)));
            }
            if (r != NULL) {
                WalkEvents(seen, &n, &r->events);
            }
        }
    }
    return NULL;
}

/* ---- the main menu ------------------------------------------------------------
 * GameCube and PS2: One Player, Multi-Player, Load Game, Save Game, Options,
 * Extras; the port adds Quit Game, a copy of Extras opening the release's
 * "Quit Game / Confirm Quit" screen (made for the GameCube's exit to the
 * system menu, reached in the pack only from Save Game's Manage Memory Card).
 * PC: One Player, Load Game, Save Game, Options, Extras, Exit to Desktop (its
 * own confirm screen, which quits): the port adds Multi-Player, a copy of One
 * Player, as the second row. Either way the seven rows sit at the GameCube's
 * port spacing (ROW_*), in that order, and the up/down ring follows them. */

/* The main menu, still as in the pack? Fills `m` with its rows. */
static int FindMainMenu(InterfaceHeader *h, MainMenu *m) {
    IfaceObjectDef *objs = GETP(h->objects);
    u32 i;

    if (objs == NULL || objs == s_mainObjs || h->numObjects > MAIN_MAX_OBJECTS ||
        FindText(objs, h->numObjects, "Main Menu", 0) == NONE) {
        return 0;
    }
    m->numObjects = h->numObjects;
    for (i = 0; i < MAIN_NUM_ROWS; i++) {
        m->row[i] = FindText(objs, h->numObjects, s_mainRowText[i], 1);
    }
    /* a release's main menu has all of these, and one of Multi-Player and
     * Exit to Desktop at least */
    return m->row[MAIN_ONE_PLAYER] != NONE && m->row[MAIN_LOAD] != NONE && m->row[MAIN_SAVE] != NONE &&
           m->row[MAIN_OPTIONS] != NONE && m->row[MAIN_EXTRAS] != NONE &&
           (m->row[MAIN_MULTI_PLAYER] != NONE || m->row[MAIN_EXIT] != NONE);
}

/* The quit screen: "Confirm Quit", its rows "Yes, Quit" and "No", the warning
 * under the title (blank on PS2). */
static int IsQuitScreen(const InterfaceHeader *h) {
    const IfaceObjectDef *objs = GETP(h->objects);
    u32 confirm = FindText(objs, h->numObjects, "Confirm Quit", 0);

    return confirm != NONE && confirm + 1 < h->numObjects && DefText(&objs[confirm + 1]) != NULL &&
           GETP(objs[confirm + 1].record) == NULL && FindText(objs, h->numObjects, "Yes, Quit", 1) != NONE &&
           FindText(objs, h->numObjects, "No", 1) != NONE;
}

static int IsLoadProgress(const InterfaceHeader *h) {
    return HasText(h, "LOAD GAME", 0) && HasText(h, "Continue without loading", 1);
}

static int IsSaveNaming(const InterfaceHeader *h) {
    return HasText(h, "NAME SAVED GAME", 0);
}

static int IsReminder(const InterfaceHeader *h) {
    return FindTextPrefix(h, "Be sure to save") != NONE;
}

/* The memory card screens. GameCube and PS2: Load Game and Save Game (the
 * latter's screen is the child of a one-object wrapper) start on a slot
 * picker; PC: a file list. Loading and saving each have a progress screen
 * (loading's is "LOAD GAME" with "Continue without loading", saving's the
 * naming screen's next), and leaving Options shows the reminder. */
static void FindCardScreens(InterfaceHeader *main, const MainMenu *m) {
    InterfaceHeader *save = RowTarget(main, m->row[MAIN_SAVE]);
    const IfaceObjectDef *objs;

    s_main = main;
    s_loadScreen = RowTarget(main, m->row[MAIN_LOAD]);
    s_saveScreen = NULL;
    if (save != NULL && save->numObjects == 1 && (objs = GETP(save->objects))[0].type == IFACE_DEF_CHILD_SYNC &&
        GETP(objs[0].child) != NULL) {
        s_saveScreen = (InterfaceHeader *)GETP(*GETP(objs[0].child));
    }
    s_loadProgress = FindScreen(main, IsLoadProgress);
    s_saveProgress = FirstRowTarget(FindScreen(main, IsSaveNaming));
    s_reminder = FindScreen(RowTarget(main, m->row[MAIN_OPTIONS]), IsReminder);
    if (s_loadScreen == NULL || s_saveScreen == NULL || s_loadProgress == NULL || s_saveProgress == NULL ||
        s_reminder == NULL) {
        Port_Log("menu: memory card screens: load %d, save %d, loading %d, saving %d, reminder %d",
                 s_loadScreen != NULL, s_saveScreen != NULL, s_loadProgress != NULL, s_saveProgress != NULL,
                 s_reminder != NULL);
    }
}

/* Load/Save Game: saves are files, always slot A. Where the screen starts on
 * a slot picker, slot A's file list from the start, no card warnings, and B
 * (back to the slot picker) and the automatic return to it leave instead. */
static void PatchCardScreen(InterfaceHeader *h) {
    IfaceObjectDef *objs = GETP(h->objects);
    IfaceVarList *vars = GETP(h->vars);
    u32 i, j, k;

    GameSettings_SetCodeVariable(CODEVAR_DEVICE, 0, 1);
    GameSettings_SetCodeVariable(CODEVAR_FILE_INDEX, 0, 1);
    if (FindTextPrefix(h, "Memory Card Slot") == NONE && FindTextPrefix(h, " Memory Card Slot") == NONE) {
        return;                 /* PC: no slot picker */
    }
    if (vars != NULL && vars->count > CARD_VAR_PHASE) {
        GETP(vars->vars)[CARD_VAR_PHASE].offset = 1;
    }
    s_hideAll.type = 0;
    s_hideAll.count = 0;
    if (h->numObjects > CARD_WARNING && GETP(objs[CARD_WARNING].record) == NULL) {
        SETP(objs[CARD_WARNING].conditions, &s_hideAll);
    }
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
/* ---- Online screens ---------------------------------------------------------
 * All copies of a menu screen of the release's own (OnlineTemplate: the
 * Multi-Player screen, or on PC, which has none, the One Player screen): the
 * same frame and title, with lines of text in place of its rows, some of them
 * selectable. Text lines come first (their numbers are what
 * Port_MenuOnlineSetLine takes), then the rows. Lines take the template's row
 * objects in order, then copies of the first added after its objects; the
 * rows left over are hidden. */

enum {
    TEMPLATE_MAX_OBJECTS = 24,
    TEMPLATE_MAX_TITLES = 2,
    TEMPLATE_MAX_ROWS = 8,
    MP_MAX_LINES = 8,
    PANEL_LINE_CHARS = 15,      /* what fits across the panel */
};

typedef struct OnlineTemplate {
    const InterfaceHeader *h;
    u32 numTitles, title[TEMPLATE_MAX_TITLES];  /* the title's texts (the PC's has two) */
    u32 numRows, row[TEMPLATE_MAX_ROWS];
} OnlineTemplate;

typedef struct OnlineScreen {
    InterfaceHeader h;
    IfaceObjectDef objs[TEMPLATE_MAX_OBJECTS + MP_MAX_LINES];
    TextCopy title[TEMPLATE_MAX_TITLES];
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

static OnlineScreen s_olChoose, s_olOnline, s_olTunnel, s_olConnect, s_olLobbyHost, s_olLobbyClient, s_olLobbyTunnel, s_olMode, s_olWaiting;
static InterfaceHeader s_olPaste, s_olCopy;   /* never shown: those rows' "targets" */
/* the game's screens online play goes through: Multi-Player, its split car
 * select, the one-player modes' car select (online too, each machine having
 * one player), Battle Options, Joyride's car and item selects */
static InterfaceHeader *s_mpScreen, *s_carSelect, *s_carSelectSolo, *s_battleOptions, *s_joyrideCar, *s_joyrideItem;
static RecordCopy s_mainMultiRec;
static char s_olAddress[64];

/* h's title texts (those without a record at the first one's height) and
 * rows, if it can be one. */
static int OnlineTemplateOf(OnlineTemplate *t, const InterfaceHeader *h) {
    const IfaceObjectDef *objs;
    u32 i;

    memset(t, 0, sizeof(*t));
    if (h == NULL || (objs = GETP(h->objects)) == NULL || h->numObjects > TEMPLATE_MAX_OBJECTS) {
        return 0;
    }
    t->h = h;
    for (i = 0; i < h->numObjects; i++) {
        if (DefText(&objs[i]) == NULL) {
            continue;
        }
        if (GETP(objs[i].record) != NULL) {
            if (t->numRows < TEMPLATE_MAX_ROWS) {
                t->row[t->numRows++] = i;
            }
        } else if (t->numTitles < TEMPLATE_MAX_TITLES &&
                   (t->numTitles == 0 || PosY(&objs[i]) == PosY(&objs[t->title[0]]))) {
            t->title[t->numTitles++] = i;
        }
    }
    return t->numTitles > 0 && t->numRows > 0;
}

static void BuildOnlineScreen(OnlineScreen *s, const OnlineTemplate *tpl, const char *title,
                              u32 numText, u32 numRows, const OnlineRow *rows) {
    const IfaceObjectDef *from = GETP(tpl->h->objects);
    u32 numObjects = tpl->h->numObjects;
    u32 numLines = numText + numRows;
    u32 extra = 0;
    u32 i;

    s->h = *tpl->h;
    memcpy(s->objs, from, numObjects * sizeof(IfaceObjectDef));
    for (i = 0; i < tpl->numTitles; i++) {
        SetText(&s->title[i], &s->objs[tpl->title[i]], title);
    }
    s->numLines = numLines;
    for (i = 0; i < numLines; i++) {
        u32 at = i < tpl->numRows ? tpl->row[i] : numObjects + extra++;
        IfaceObjectDef *d = &s->objs[at];

        *d = from[tpl->row[0]];
        s->lineObject[i] = at;
        /* a full panel: closer together, as it only just fits */
        MoveTo(&s->anim[i], d, numLines >= 8 ? ROW_Y - 10.0f + i * 20.0f : ROW_Y + i * ROW_STEP);
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
    /* the template's rows no line took */
    for (i = numLines; i < tpl->numRows; i++) {
        IfaceObjectDef *d = &s->objs[tpl->row[i]];

        SETP(d->conditions, &s_hideAll);
        d->active = 0;
        d->record.h = 0;
    }
    s->h.numObjects = numObjects + extra;
    SETP(s->h.objects, s->objs);
}

/* The screens online play goes through, found from the main menu (Multi-Player:
 * Drag Race and Battle, then their options, world select and split car select;
 * One Player: Drag Race's car select, Joyride's world, car and item selects).
 * Built again with each main menu: pack data may have moved since the last. */
static void BuildOnlineScreens(InterfaceHeader *main, const MainMenu *m) {
    InterfaceHeader *onePlayer = RowTarget(main, m->row[MAIN_ONE_PLAYER]);
    InterfaceHeader *mpDrag, *joyride;
    OnlineTemplate tpl;
    OnlineRow rows[4];

    s_mpScreen = RowTarget(main, m->row[MAIN_MULTI_PLAYER]);
    mpDrag = RowTargetOf(s_mpScreen, "Drag Race");
    s_battleOptions = RowTargetOf(s_mpScreen, "Battle");
    s_carSelect = FirstRowTarget(ScreenTarget(mpDrag));
    s_carSelectSolo = FirstRowTarget(ScreenTarget(RowTargetOf(onePlayer, "Drag Race")));
    joyride = RowTargetOf(onePlayer, "Joyride");
    s_joyrideCar = FirstRowTarget(joyride);
    s_joyrideItem = ScreenTarget(s_joyrideCar);
    s_hideAll.type = 0;
    s_hideAll.count = 0;
    if (s_carSelect == NULL || s_carSelectSolo == NULL || mpDrag == NULL || s_battleOptions == NULL ||
        !OnlineTemplateOf(&tpl, s_mpScreen) || !TextIs(&GETP(s_mpScreen->objects)[tpl.title[0]], "Multi-Player")) {
        s_mpScreen = NULL;
        return;
    }
    rows[0] = (OnlineRow){"Online", &s_olOnline.h};
    rows[1] = (OnlineRow){"Local", s_mpScreen};
    BuildOnlineScreen(&s_olChoose, &tpl, "Multi-Player", 0, 2, rows);

    rows[0] = (OnlineRow){"Tunnel Host", &s_olLobbyTunnel.h};
    rows[1] = (OnlineRow){"LAN Host", &s_olLobbyHost.h};
    rows[2] = (OnlineRow){"Connect", &s_olConnect.h};
    BuildOnlineScreen(&s_olOnline, &tpl, "Online", 2, 3, rows);

    rows[0] = (OnlineRow){"Back", NULL};
    /* what Tunnel Host leads to if it can't start */
    BuildOnlineScreen(&s_olTunnel, &tpl, "Tunnel Host", 3, 1, rows);
    if (Port_TunnelAvailable()) {
        SetText(&s_olTunnel.text[0], &s_olTunnel.objs[s_olTunnel.lineObject[0]], "Install");
        SetText(&s_olTunnel.text[1], &s_olTunnel.objs[s_olTunnel.lineObject[1]], "cloudflared");
        SetText(&s_olTunnel.text[2], &s_olTunnel.objs[s_olTunnel.lineObject[2]], "to host");
    } else {
        SetText(&s_olTunnel.text[0], &s_olTunnel.objs[s_olTunnel.lineObject[0]], "Not available");
        SetText(&s_olTunnel.text[1], &s_olTunnel.objs[s_olTunnel.lineObject[1]], "here: use LAN");
        SetText(&s_olTunnel.text[2], &s_olTunnel.objs[s_olTunnel.lineObject[2]], "or Connect");
    }

    rows[0] = (OnlineRow){"Connect", &s_olLobbyClient.h};
    rows[1] = (OnlineRow){"Paste Address", &s_olPaste};
    rows[2] = (OnlineRow){"Back", NULL};
    BuildOnlineScreen(&s_olConnect, &tpl, "Connect", 4, 3, rows);

    rows[0] = (OnlineRow){"Start Game", &s_olMode.h};
    rows[1] = (OnlineRow){"Leave", NULL};
    BuildOnlineScreen(&s_olLobbyHost, &tpl, "Lobby", 3, 2, rows);

    rows[0] = (OnlineRow){"Leave", NULL};
    BuildOnlineScreen(&s_olLobbyClient, &tpl, "Lobby", 2, 1, rows);

    rows[0] = (OnlineRow){"Copy Invite", &s_olCopy};
    rows[1] = (OnlineRow){"Start Game", &s_olMode.h};
    rows[2] = (OnlineRow){"Leave", NULL};
    BuildOnlineScreen(&s_olLobbyTunnel, &tpl, "Lobby", 5, 3, rows);

    rows[0] = (OnlineRow){"Drag Race", mpDrag};
    rows[1] = (OnlineRow){"Battle", s_battleOptions};
    rows[2] = (OnlineRow){"Joyride", joyride};
    BuildOnlineScreen(&s_olMode, &tpl, "Online Game", 0, joyride != NULL && s_joyrideCar != NULL && s_joyrideItem != NULL ? 3 : 2, rows);

    BuildOnlineScreen(&s_olWaiting, &tpl, "Please Wait", 1, 0, rows);
    SetText(&s_olWaiting.text[0], &s_olWaiting.objs[s_olWaiting.lineObject[0]], "Waiting...");
}

static OnlineScreen *OnlineScreenOf(PortOnlineScreen screen) {
    switch (screen) {
    case ONLINE_SCREEN_ONLINE: return &s_olOnline;
    case ONLINE_SCREEN_LOBBY_HOST: return &s_olLobbyHost;
    case ONLINE_SCREEN_LOBBY_CLIENT: return &s_olLobbyClient;
    case ONLINE_SCREEN_LOBBY_TUNNEL: return &s_olLobbyTunnel;
    case ONLINE_SCREEN_WAITING: return &s_olWaiting;
    default: return NULL;
    }
}

InterfaceHeader *Port_MenuOnlineScreen(PortOnlineScreen screen) {
    OnlineScreen *s = OnlineScreenOf(screen);

    if (s != NULL) {
        return &s->h;
    }
    switch (screen) {
    case ONLINE_SCREEN_MAIN: return s_main;
    case ONLINE_SCREEN_CAR_SELECT: return s_carSelectSolo;
    case ONLINE_SCREEN_CAR_SELECT_JOYRIDE: return s_joyrideCar;
    case ONLINE_SCREEN_ITEM_SELECT_JOYRIDE: return s_joyrideItem;
    default: return NULL;
    }
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

/* Over several lines, parted at the marks addresses have, or wherever it must be. */
static void SetOnlineLines(OnlineScreen *s, u32 first, u32 count, const char *text) {
    u32 i;

    for (i = 0; i < count; i++) {
        char line[PANEL_LINE_CHARS + 1];
        size_t n = strlen(text);

        if (n > PANEL_LINE_CHARS && i + 1 < count) {
            n = PANEL_LINE_CHARS;
            while (n > 1 && !strchr("-/. :", text[n - 1])) {
                n--;
            }
            if (n <= 1) {
                n = PANEL_LINE_CHARS;
            }
        } else if (n > PANEL_LINE_CHARS) {
            n = PANEL_LINE_CHARS;
        }
        memcpy(line, text, n);
        line[n] = 0;
        if (first + i < s->numLines) {
            SetOnlineLine(s, first + i, line);
        }
        text += n;
    }
}

void Port_MenuOnlineSetLines(PortOnlineScreen screen, u32 first, u32 count, const char *text) {
    OnlineScreen *s = OnlineScreenOf(screen);

    if (s != NULL) {
        SetOnlineLines(s, first, count, text);
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
    if (target == &s_olLobbyTunnel.h) {
        return Port_OnlineStartTunnelHost() ? target : &s_olTunnel.h;
    }
    if (target == &s_olCopy) {
        char invite[128];

        if (Port_OnlineInvite(invite, sizeof(invite))) {
            Port_TextCopy(invite);
            Port_Log("online: invite copied");
        }
        return NULL;
    }
    if (target == &s_olLobbyClient.h) {
        if (s_olAddress[0] == 0) {
            return NULL;
        }
        return Port_OnlineStartClient(s_olAddress) ? target : NULL;
    }
    if (target == &s_olPaste) {
        Port_TextInputPaste();  /* the next update shows it */
        return NULL;
    }
    if (target == &s_olMode.h && Port_OnlinePlayerCount() < 2) {
        return NULL;            /* nobody to play with yet */
    }
    if (target == s_carSelect && Port_OnlineActive()) {
        Port_OnlineHostSetup();
        return s_carSelectSolo;
    }
    if (target == s_joyrideCar && Port_OnlineActive()) {
        Port_OnlineHostSetup();
    }
    return target;
}

/* Each frame's typing shows on the Connect screen, from Port_MenuPatch's Start
 * on until it is released. */
void Port_MenuOnlineUpdate(void) {
    u32 i;

    for (i = 0; i < 0x12; i++) {
        if (g_interfaces[i].count != 0 && GETP(g_interfaces[i].header) == &s_olConnect.h) {
            SetOnlineLines(&s_olConnect, 0, 4, s_olAddress[0] != 0 ? s_olAddress : "Type or paste an address");
            return;
        }
    }
}

/* ---- Battle Options without Opponents ---------------------------------------- */

enum {
    BATTLE_NUM_OBJECTS = 34,
    BATTLE_OPPONENTS = 24,      /* label, value, arrow */
    BATTLE_ROW_OBJECTS = 3,
    BATTLE_TIME_LIMIT = 27,
    BATTLE_SCORE = 30,
    BATTLE_END = 33,
    BATTLE_REC_RESPAWN = 6,
    BATTLE_REC_OPPONENTS = 3,
    BATTLE_REC_TIME_LIMIT = 4,
};

static u32 s_battleOriginal;    /* the handle in h->objects */
static InterfaceHeader *s_battlePatched;
static IfaceObjectDef s_battleObjs[BATTLE_NUM_OBJECTS];
static RecordCopy s_battleRecAbove, s_battleRecBelow;
static AnimCopy s_battleAnim[BATTLE_END - BATTLE_TIME_LIMIT];

static void PatchBattleOptions(InterfaceHeader *h) {
    IfaceObjectDef *objs;
    IfaceRecord *opponents;
    u32 above = BATTLE_NUM_OBJECTS, i;
    f32 step;

    if (h == s_battlePatched) {
        h->objects.h = s_battleOriginal;   /* as the pack has it */
        s_battlePatched = NULL;
    }
    if (h != s_battleOptions || !Port_OnlineActive()) {
        return;
    }
    objs = GETP(h->objects);
    opponents = GETP(objs[BATTLE_OPPONENTS].record);
    if (!TextIs(&objs[BATTLE_OPPONENTS], "Opponents") || opponents == NULL || opponents->id != BATTLE_REC_OPPONENTS) {
        return;
    }
    for (i = 0; i < BATTLE_OPPONENTS; i++) {
        IfaceRecord *r = GETP(objs[i].record);

        if (r != NULL && r->id == BATTLE_REC_RESPAWN) {
            above = i;
        }
    }
    if (above == BATTLE_NUM_OBJECTS) {
        return;
    }
    s_battlePatched = h;
    s_battleOriginal = h->objects.h;
    memcpy(s_battleObjs, objs, sizeof(s_battleObjs));
    for (i = BATTLE_OPPONENTS; i < BATTLE_OPPONENTS + BATTLE_ROW_OBJECTS; i++) {
        s_battleObjs[i].active = 0;
        s_battleObjs[i].record.h = 0;
        SETP(s_battleObjs[i].conditions, &s_hideAll);
    }
    CopyRecord(&s_battleRecAbove, &s_battleObjs[above], BATTLE_REC_RESPAWN, GETP(objs[above].record)->next[0], BATTLE_REC_TIME_LIMIT);
    CopyRecord(&s_battleRecBelow, &s_battleObjs[BATTLE_TIME_LIMIT], BATTLE_REC_TIME_LIMIT, BATTLE_REC_RESPAWN,
               GETP(objs[BATTLE_TIME_LIMIT].record)->next[1]);
    step = ((f32 *)GETP(GETP(objs[BATTLE_TIME_LIMIT].anim)->pos.keys))[1] - ((f32 *)GETP(GETP(objs[BATTLE_OPPONENTS].anim)->pos.keys))[1];
    for (i = BATTLE_TIME_LIMIT; i < BATTLE_END; i++) {
        if (GETP(objs[i].anim) != NULL && GETP(GETP(objs[i].anim)->pos.keys) != NULL) {
            MoveTo(&s_battleAnim[i - BATTLE_TIME_LIMIT], &s_battleObjs[i], ((f32 *)GETP(GETP(objs[i].anim)->pos.keys))[1] - step);
        }
    }
    SETP(h->objects, s_battleObjs);
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

/* InterfaceManager_PlayEvent: an event is about to switch to `target` (go-to
 * events, types 1 and 0x20). Returns where to go instead, NULL for nowhere. */
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

/* The quit screen's Yes and No take Start as well as A, like the other menus'
 * rows, and its warning (made for the exit to the GameCube's memory card
 * manager, blank on PS2) is reworded. Pack data, changed in place. */
static void PatchQuitScreen(InterfaceHeader *quit) {
    IfaceObjectDef *objs = GETP(quit->objects);
    u32 rows[2], i;
    IfaceText *warning;

    rows[0] = FindText(objs, quit->numObjects, "Yes, Quit", 1);
    rows[1] = FindText(objs, quit->numObjects, "No", 1);
    for (i = 0; i < 2; i++) {
        IfaceRecord *rec = GETP(objs[rows[i]].record);
        if (rec->events.count == 1 && GETP(rec->events.events)[0].type == EVENT_A) {
            s_quitEvents[i][0] = GETP(rec->events.events)[0];
            s_quitEvents[i][1] = s_quitEvents[i][0];
            s_quitEvents[i][1].type = EVENT_START;
            rec->events.count = 2;
            SETP(rec->events.events, s_quitEvents[i]);
        }
    }
    warning = GETP(objs[FindText(objs, quit->numObjects, "Confirm Quit", 0) + 1].sub);
    SETP(s_quitWarning, "Warning:|Any unsaved progress will be lost.");
    SETP(warning->strings, &s_quitWarning);
}

/* A row's copy at `at` of the main menu's copy: d's object with text `name`
 * and record `id`, going where `target` is. */
static void AddMainRow(u32 at, const IfaceObjectDef *d, const char *name, TextCopy *text, RecordCopy *rec, u8 id,
                       InterfaceHeader *target) {
    IfaceObjectDef *entry = &s_mainObjs[at];
    u32 i;

    *entry = *d;
    SetText(text, entry, name);
    CopyRecord(rec, entry, id, 0, 0);
    for (i = 0; i < rec->rec.events.count; i++) {
        if (rec->defs[i].type == EVENT_GOTO) {
            SETP(rec->defs[i].target, target);
        }
    }
}

static void PatchMainMenu(InterfaceHeader *h, const MainMenu *m) {
    IfaceObjectDef *objs = GETP(h->objects);
    u32 row[MAIN_NUM_ROWS];
    u32 n = m->numObjects, insertAt = MAIN_NUM_ROWS, moved = 0, i;
    u8 freeId = 0;
    InterfaceHeader *quit;
    IfaceVarList *vars = GETP(h->vars);

    FindCardScreens(h, m);
    BuildOnlineScreens(h, m);
    memcpy(row, m->row, sizeof(row));
    memcpy(s_mainObjs, objs, n * sizeof(IfaceObjectDef));
    for (i = 0; i < n; i++) {
        if (GETP(objs[i].record) != NULL && GETP(objs[i].record)->id > freeId) {
            freeId = GETP(objs[i].record)->id;
        }
    }
    freeId++;

    /* Quit Game, a copy of Extras, last */
    if (row[MAIN_EXIT] == NONE) {
        quit = FindScreen(h, IsQuitScreen);
        if (quit != NULL) {
            PatchQuitScreen(quit);
            AddMainRow(n, &objs[row[MAIN_EXTRAS]], "Quit Game", &s_desktopText, &s_desktopRec, REC_DESKTOP, quit);
            row[MAIN_EXIT] = n++;
        } else {
            Port_Log("menu: quit screen not found, no Quit Game entry");
        }
    }
    /* Multi-Player: to the new Online / Local screen; on PC a copy of One
     * Player, second */
    if (s_mpScreen == NULL) {
        Port_Log("menu: multi-player screens not found, no online menus");
    } else if (row[MAIN_MULTI_PLAYER] != NONE) {
        RecordCopy *rec = CopyRecord(&s_mainMultiRec, &s_mainObjs[row[MAIN_MULTI_PLAYER]],
                                     GETP(objs[row[MAIN_MULTI_PLAYER]].record)->id, 0, 0);

        for (i = 0; i < rec->rec.events.count; i++) {
            if (rec->defs[i].type == EVENT_GOTO) {
                SETP(rec->defs[i].target, &s_olChoose.h);
            }
        }
    } else {
        AddMainRow(n, &objs[row[MAIN_ONE_PLAYER]], "Multi-Player", &s_multiText, &s_mainMultiRec, freeId,
                   &s_olChoose.h);
        row[MAIN_MULTI_PLAYER] = n++;
        insertAt = 1;
    }
    /* The pack's rows (their texts, labels too) in the port's spacing, after
     * the new ones made room; the new rows in theirs. */
    for (i = 0; i < m->numObjects; i++) {
        f32 y;
        s32 k;

        if (n == m->numObjects || DefText(&objs[i]) == NULL) {
            continue;           /* (no new row: the pack's spacing) */
        }
        y = PosY(&objs[i]);
        k = (s32)((y - PACK_ROW_FIRST_Y) / PACK_ROW_STEP_Y + 0.5f);
        y -= PACK_ROW_FIRST_Y + k * PACK_ROW_STEP_Y;
        if (k < 0 || k >= MAIN_NUM_ROWS - 1 || y > 3.0f || y < -3.0f || moved == MAIN_MAX_MOVED) {
            continue;
        }
        MoveTo(&s_mainRowAnim[moved++], &s_mainObjs[i], ROW_Y + (k + ((u32)k >= insertAt)) * ROW_STEP);
    }
    if (row[MAIN_EXIT] != NONE && row[MAIN_EXIT] >= m->numObjects) {
        MoveTo(&s_desktopAnim, &s_mainObjs[row[MAIN_EXIT]], ROW_Y + ROW_NEW * ROW_STEP);
    }
    if (row[MAIN_MULTI_PLAYER] != NONE && row[MAIN_MULTI_PLAYER] >= m->numObjects) {
        MoveTo(&s_mainRowAnim[moved++], &s_mainObjs[row[MAIN_MULTI_PLAYER]], ROW_Y + insertAt * ROW_STEP);
    }

    /* the up/down ring, in the rows' order: pack records changed in place */
    for (i = 0; i < MAIN_NUM_ROWS; i++) {
        u32 up = i, down = i;
        IfaceRecord *rec;

        if (row[i] == NONE) {
            continue;
        }
        do {
            up = (up + MAIN_NUM_ROWS - 1) % MAIN_NUM_ROWS;
        } while (row[up] == NONE);
        do {
            down = (down + 1) % MAIN_NUM_ROWS;
        } while (row[down] == NONE);
        rec = GETP(s_mainObjs[row[i]].record);
        rec->next[0] = GETP(s_mainObjs[row[up]].record)->id;
        rec->next[1] = GETP(s_mainObjs[row[down]].record)->id;
    }
    /* a selected-record variable must reach the new ids */
    for (i = 0; vars != NULL && i < vars->count; i++) {
        IfaceVar *v = &GETP(vars->vars)[i];

        if (v->type == IFACE_VAR_SELECTED && v->max < freeId) {
            Port_Log("menu: main menu var %u max %d -> %u", i, v->max, freeId);
            v->max = freeId;
        }
    }
    h->numObjects = n;
    SETP(h->objects, s_mainObjs);
}


void Port_MenuPatch(InterfaceHeader *h) {
    IfaceObjectDef *objs;
    IfaceObjectDef *entry;
    MainMenu main;
    u32 i;

    if (h == NULL) {
        return;
    }
    Port_MenuDump(h);        /* HWVX_MENUDUMP: the screens as the pack has them */
    Port_CarouselPatch(h);   /* world, car and weapon select: more windows */
    PatchPauseMenu(h);
    PatchBattleOptions(h);
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
    if (FindMainMenu(h, &main)) {
        if (Port_OriginalMainMenu()) {
            /* graphics.cfg mainmenu=original: the main menu as the
             * release has it - no Quit Game (the GameCube's has no way to
             * quit: close the window instead), no Online / Local, the
             * pack's rows, spacing and ring. The card screens are still
             * found from it, so their patches stay. */
            FindCardScreens(h, &main);
            return;
        }
        PatchMainMenu(h, &main);
        return;
    }
    if (h == s_loadScreen || h == s_saveScreen) {
        PatchCardScreen(h);
        return;
    }
    if (!FindOptions(h, &s_opt)) {
        return;
    }
    objs = GETP(h->objects);
    BuildGraphicsScreen(h, objs);

    /* Graphics, a copy of Controller, as the last row (the pack's rows close
     * up to make room for it) */
    memcpy(s_optObjs, objs, s_opt.numObjects * sizeof(IfaceObjectDef));
    for (i = s_opt.controller; i < s_opt.sfx + 4; i++) {
        MoveRow(&s_optRowAnim[i - s_opt.controller], &s_optObjs[i], &s_opt.rows);
    }
    entry = &s_optObjs[s_opt.numObjects];
    *entry = objs[s_opt.controller];
    MoveTo(&s_entryAnim, entry, s_opt.rows.first + (s_opt.sound != NONE ? ROW_NEW : ROW_NEW - 1) * s_opt.rows.step);
    SetText(&s_entryText, entry, "Graphics");
    CopyRecord(&s_entryRec, entry, REC_GRAPHICS, s_opt.recSfx, s_opt.recController);
    for (i = 0; i < s_entryRec.rec.events.count; i++) {
        if (s_entryRec.defs[i].type == 1) {
            SETP(s_entryRec.defs[i].target, &s_gfx);
        }
    }
    /* into the ring: SFX -> Graphics -> Controller */
    GETP(objs[s_opt.sfx].record)->next[1] = REC_GRAPHICS;
    GETP(objs[s_opt.controller].record)->next[0] = REC_GRAPHICS;
    if (GETP(GETP(h->vars)->vars)[s_opt.varSelected].max < REC_GRAPHICS) {
        GETP(GETP(h->vars)->vars)[s_opt.varSelected].max = REC_GRAPHICS;
    }
    h->numObjects = s_opt.numObjects + 1;
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
