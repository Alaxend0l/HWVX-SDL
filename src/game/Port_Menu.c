/* A "Graphics" entry in the Options screen and the Graphics screen it opens
 * (resolution, windowed/fullscreen: src/port/gfx_settings.c). Port-only
 * file.
 *
 * Menus are data (HW_IPack.xgc), so both are built from the Options screen's
 * own definitions when it is created (Port_MenuPatch, from
 * InterfaceManager_Create); nothing is added to the pack files.
 *
 * - Options: a 41st object, a copy of the "Controller" row (a text whose
 *   A/Start events go to another interface) moved one row below "SFX",
 *   labelled "Graphics" and going to the Graphics screen. It joins the
 *   up/down ring between "SFX" and "Controller".
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

#define ROW_FIRST_Y 198.0f      /* Controller */
#define ROW_STEP_Y 26.0f
#define ROW_GRAPHICS_Y 354.0f   /* below SFX (328) */
#define MAX_EVENTS 4
#define MAX_MODES 48

typedef struct TextCopy {
    IfaceText text;
    PTR32(char) slot;
    char str[24];
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

static IfaceObjectDef s_optObjs[OPT_NUM_OBJECTS + 1];
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
        MoveTo(&s_rowAnim[i], &s_gfxObjs[OPT_DIFFICULTY + i], ROW_FIRST_Y);
        MoveTo(&s_rowAnim[4 + i], &s_gfxObjs[OPT_SOUND + i], ROW_FIRST_Y + ROW_STEP_Y);
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

void Port_MenuPatch(InterfaceHeader *h) {
    IfaceObjectDef *objs;
    IfaceObjectDef *entry;
    u32 i;

    if (h == NULL) {
        return;
    }
    if (h == &s_gfx) {
        RefreshModes();
        return;
    }
    if (!IsOptions(h)) {
        return;
    }
    objs = GETP(h->objects);
    BuildGraphicsScreen(h, objs);

    memcpy(s_optObjs, objs, OPT_NUM_OBJECTS * sizeof(IfaceObjectDef));
    entry = &s_optObjs[OPT_NUM_OBJECTS];
    *entry = objs[OPT_CONTROLLER];
    MoveTo(&s_entryAnim, entry, ROW_GRAPHICS_Y);
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
