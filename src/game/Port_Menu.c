/* Port menu entries: "Quit Game" in the main menu, and a "Graphics"
 * entry in the Options screen with the Graphics screen it opens (resolution,
 * windowed/fullscreen: src/port/gfx_settings.c). Port-only file.
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
 * Copies live in static storage; the pack data they point to stays where it
 * is. To add a setting: copy another row into the Graphics screen the same
 * way, with a new PORT_CODEVAR_* id. */
#include "common.h"
#include "Interface.h"

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
};

enum { REC_CONTROLLER = 1, REC_SFX = 5, REC_GRAPHICS = 8, REC_DIFFICULTY = 3, REC_SOUND = 6 };

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
static IfaceCodeVar s_gfxCode[2];
static IfaceConditionList s_hideAll;   /* type 0 "hide if all", no conditions: always hidden */
static AnimCopy s_rowAnim[8];
static TextCopy s_gfxText[3];
static RecordCopy s_gfxRec[2];
static ActionCopy s_gfxAction[2];
static ListCopy s_resList, s_displayList;

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

/* d's default animation, at height y. */
static void MoveTo(AnimCopy *c, IfaceObjectDef *d, f32 y) {
    IfaceObjAnim *a = GETP(d->anim);
    f32 *key;

    c->anim = *a;
    memcpy(c->pos, GETP(a->pos.keys), sizeof(c->pos));
    key = (f32 *)c->pos;
    key[1] = y;
    SETP(c->anim.pos.keys, c->pos);
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

/* Left/right on a row: write variable `var` to code variable `id`. */
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
        if (rec->defs[i].type == 0xB) {
            SETP(rec->defs[i].target, &c->list);
        }
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
    static const u8 hide[] = {
        OPT_CONTROLLER, OPT_CHEATS, OPT_CHEATS_LABEL, OPT_MUSIC, OPT_MUSIC + 1, OPT_MUSIC + 2, OPT_MUSIC + 3,
        OPT_SFX, OPT_SFX + 1, OPT_SFX + 2, OPT_SFX + 3, OPT_DEBUG,
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

    /* the two rows, moved to the top */
    for (i = 0; i < 4; i++) {
        MoveTo(&s_rowAnim[i], &s_gfxObjs[OPT_DIFFICULTY + i], ROW_Y);
        MoveTo(&s_rowAnim[4 + i], &s_gfxObjs[OPT_SOUND + i], ROW_Y + ROW_STEP);
    }
    SetText(&s_gfxText[1], &s_gfxObjs[OPT_DIFFICULTY], "Resolution");
    SetActions(CopyRecord(&s_gfxRec[0], &s_gfxObjs[OPT_DIFFICULTY], REC_DIFFICULTY, REC_SOUND, REC_SOUND),
               &s_gfxAction[0], PORT_CODEVAR_RESOLUTION, OPT_VAR_DIFFICULTY);
    SetText(&s_gfxText[2], &s_gfxObjs[OPT_SOUND], "Display");
    SetActions(CopyRecord(&s_gfxRec[1], &s_gfxObjs[OPT_SOUND], REC_SOUND, REC_DIFFICULTY, REC_DIFFICULTY),
               &s_gfxAction[1], PORT_CODEVAR_DISPLAY, OPT_VAR_SOUND);
    SetList(&s_displayList, &s_gfxObjs[OPT_SOUND + 1], OPT_VAR_SOUND, displayNames, 2);
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
InterfaceHeader *Port_MenuEventTarget(InterfaceHeader *target) {
    extern s8 g_bMemoryCardHWHack;

    if (target == NULL || s_main == NULL) {
        return target;
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
}

void Port_MenuPatch(InterfaceHeader *h) {
    IfaceObjectDef *objs;
    IfaceObjectDef *entry;
    u32 i;

    if (h == NULL) {
        return;
    }
    Port_CarouselPatch(h);   /* world, car and weapon select: more windows */
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
}
