/* Testing: HWVX_START boots straight into a mode's menu, a select screen or
 * a race, skipping the menus before it. Port-only file.
 *
 *   HWVX_START=menu:mode=M          the screen M's One Player row opens: the
 *                                   mission (adventure), challenge or joyride
 *                                   select, the drag race or battle options
 *   HWVX_START=worldselect:mode=M[,world=W]   M's world (track) select, past its
 *                                   options, opened at world W
 *   HWVX_START=carselect[:mode=M][,car=N][,world=W]   M's car select
 *   HWVX_START=itemselect[:mode=M][,car=N][,item=I]   the item select
 *   HWVX_START=race[:mode=M][,world=W][,car=N][,item=I]  a race
 *
 * M: adventure (or mission), challenge, drag, battle, joyride (default).
 * W: a level of M's progression script, by world name as the MCP names it
 * without folders (bat2_l9, ch1_l2, world1) or its index there (default 0).
 * N, I: car and item numbers (the content registry's; default 0).
 *
 * It does what the menus would, as the title screen starts
 * (StateTitleScreen_Init, Port_StartTitle): the screens are found by walking
 * the menu pack's go-to events from the title's own screen, as Port_MenuDump
 * does, and created as the menus create them. A mode's world select is the
 * screen whose row picks its progression (event 0xF,
 * StateProgressionManager_SetProgressionID) and goes on to the car select;
 * the event that leads to it runs its actions first (the drag race and
 * battle options, at the options screen's starting values). The item select
 * is header id 3. A race is what the selects leave behind (code variables
 * 0xCB and 0xCC of player 1, the progression) and the title screen's change
 * to the progression manager (state 0xB), as its demo loop starts a demo. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Interface.h"
#include "Port_Content.h"

#define WALK_MAX 512

static const InterfaceHeader *s_all[WALK_MAX];
static u32 s_numAll;

/* The modes and their progression scripts (the MCP's: the menus' 0xF events
 * pick them; adventure's is started by its own screens' event, not 0xF). */
static const struct {
    const char *name;
    s32 progression;
    const char *row;        /* the One Player menu's row */
} s_modes[] = {
    { "adventure", 0, "Adventure" }, { "mission", 0, "Adventure" }, { "challenge", 1, "Challenge" },
    { "drag", 2, "Drag Race" },      { "battle", 3, "Battle" },     { "joyride", 4, "Joyride" },
};

/* The go-to target of the first row (in walking order: One Player's before
 * Multi-Player's) whose text is `row`. */
static const InterfaceHeader *RowTarget(const char *row) {
    u32 k, i, j;

    for (k = 0; k < s_numAll; k++) {
        const IfaceObjectDef *objs = GETP(s_all[k]->objects);

        for (i = 0; objs != NULL && i < s_all[k]->numObjects; i++) {
            const IfaceRecord *r = GETP(objs[i].record);
            const IfaceText *t = objs[i].type == IFACE_DEF_TEXT ? GETP(objs[i].sub) : NULL;
            const char *text = t != NULL && GETP(t->strings) != NULL ? GETP(*GETP(t->strings)) : NULL;

            if (r == NULL || text == NULL || SDL_strcmp(text, row) != 0) {
                continue;
            }
            for (j = 0; j < r->events.count && GETP(r->events.events) != NULL; j++) {
                const IfaceEventDef *d = GETP(GETP(r->events.events)[j].def);

                if (d != NULL && d->type == 1) {
                    return (const InterfaceHeader *)GETP(d->target);
                }
            }
        }
    }
    return NULL;
}

typedef struct Found {
    const InterfaceHeader *worldSelect;    /* has a record whose 0xF event picks the progression */
    const InterfaceHeader *carSelect;      /* that record's go-to target */
    const IfaceActionList *options;        /* the action list of the event going to the world select
                                            * (drag and battle options: game type, wins, items) */
    const InterfaceHeader *optionsScreen;
    const InterfaceHeader *itemSelect;     /* header id 3 */
} Found;

static void Add(const InterfaceHeader *h) {
    u32 i;

    if (h == NULL || s_numAll >= WALK_MAX) {
        return;
    }
    for (i = 0; i < s_numAll; i++) {
        if (s_all[i] == h) {
            return;
        }
    }
    s_all[s_numAll++] = h;
}

/* Every screen reachable from root through go-to events and child slots. */
static void WalkAll(const InterfaceHeader *root) {
    u32 k, i, j;

    s_numAll = 0;
    Add(root);
    for (k = 0; k < s_numAll; k++) {
        const InterfaceHeader *h = s_all[k];
        const IfaceObjectDef *objs = GETP(h->objects);

        for (i = 0; i <= h->numObjects; i++) {
            const IfaceEventList *list = i == h->numObjects ? &h->events :
                                         GETP(objs[i].record) != NULL ? &GETP(objs[i].record)->events : NULL;

            if (i < h->numObjects && (objs[i].type == IFACE_DEF_CHILD || objs[i].type == IFACE_DEF_CHILD_SYNC) &&
                GETP(objs[i].child) != NULL) {
                Add((const InterfaceHeader *)GETP(*GETP(objs[i].child)));
            }
            for (j = 0; list != NULL && j < list->count && GETP(list->events) != NULL; j++) {
                const IfaceEventDef *d = GETP(GETP(list->events)[j].def);

                if (d != NULL && (d->type == 1 || d->type == 0x20 || d->type == 0x1F)) {
                    Add((const InterfaceHeader *)GETP(d->target));
                }
            }
        }
    }
}

/* The screens of progression p's mode. */
static void Find(s32 p, Found *f) {
    u32 k, i, j;

    SDL_zerop(f);
    for (k = 0; k < s_numAll; k++) {
        const InterfaceHeader *h = s_all[k];
        const IfaceObjectDef *objs = GETP(h->objects);

        if (h->id == 3 && f->itemSelect == NULL) {
            f->itemSelect = h;
        }
        for (i = 0; objs != NULL && i < h->numObjects && f->worldSelect == NULL; i++) {
            const IfaceRecord *r = GETP(objs[i].record);
            const InterfaceHeader *go = NULL;
            int picks = 0, starts = 0;

            for (j = 0; r != NULL && j < r->events.count && GETP(r->events.events) != NULL; j++) {
                const IfaceEventDef *d = GETP(GETP(r->events.events)[j].def);

                if (d != NULL && d->type == 1) {
                    go = (const InterfaceHeader *)GETP(d->target);
                } else if (d != NULL && (d->type == 0xF || d->type == 6) && GETP(d->args) != NULL) {
                    /* (6: the mission select's, which starts the level at once) */
                    const IfaceVarValue *a = GETP(((const PTR32(IfaceVarValue) *)GETP(d->args))[0]);
                    const IfaceVarValue *b = GETP(((const PTR32(IfaceVarValue) *)GETP(d->args))[1]);

                    picks |= a != NULL && !a->isVar && a->value == p && b != NULL && b->isVar;
                    starts |= picks && d->type == 6;
                }
            }
            if (picks && (go != NULL || starts)) {
                f->worldSelect = h;
                f->carSelect = go;
            }
        }
    }
    /* the event that goes to the world select, with its actions */
    for (k = 0; k < s_numAll && f->worldSelect != NULL && f->options == NULL; k++) {
        const InterfaceHeader *h = s_all[k];
        const IfaceObjectDef *objs = GETP(h->objects);

        for (i = 0; i <= h->numObjects && f->options == NULL; i++) {
            const IfaceEventList *list = i == h->numObjects ? &h->events :
                                         GETP(objs[i].record) != NULL ? &GETP(objs[i].record)->events : NULL;
            const IfaceActionList *acts = NULL;
            int goes = 0;

            for (j = 0; list != NULL && j < list->count && GETP(list->events) != NULL; j++) {
                const IfaceEventDef *d = GETP(GETP(list->events)[j].def);

                if (d != NULL && d->type == 1 && (const InterfaceHeader *)GETP(d->target) == f->worldSelect) {
                    goes = 1;
                } else if (d != NULL && d->type == 0xB) {
                    acts = (const IfaceActionList *)GETP(d->target);
                }
            }
            if (goes && acts != NULL) {
                f->options = acts;
                f->optionsScreen = h;
            }
        }
    }
}

/* The options screen's actions, with its variables at their starting values
 * (a constant's offset; a code variable's current value): what pressing
 * through it unchanged sets. */
static void ApplyOptions(const Found *f) {
    const IfaceVarList *vl = GETP(f->optionsScreen->vars);
    u32 i;

    for (i = 0; i < f->options->count && GETP(f->options->actions) != NULL; i++) {
        const IfaceAction *a = &GETP(f->options->actions)[i];
        const IfaceCodeAction *c = a->type == 2 ? GETP(a->data) : NULL;
        const IfaceCodeVar *cv = c != NULL ? GETP(c->code) : NULL;
        const IfaceVarValue *v = c != NULL ? GETP(c->value) : NULL;
        const IfaceVarValue *arg = cv != NULL ? GETP(cv->arg) : NULL;
        s32 value;

        if (cv == NULL || v == NULL) {
            continue;
        }
        value = v->value;
        if (v->isVar && vl != NULL && v->value < vl->count) {
            const IfaceVar *var = &GETP(vl->vars)[v->value];
            const IfaceCodeVar *vc = var->type == IFACE_VAR_CODE ? GETP(var->code) : NULL;

            value = vc != NULL ? (s32)GameSettings_GetCodeVariable(vc->id, GETP(vc->arg) != NULL ? GETP(vc->arg)->value : 0) +
                                     var->offset
                               : var->offset;
        }
        GameSettings_SetCodeVariable(cv->id, arg != NULL && !arg->isVar ? arg->value : 0, value);
        Port_Log("start: options: code variable 0x%X = %d", (unsigned)cv->id, (int)value);
    }
}

/* A select screen (or the wrapper holding it: its child of header id `id`)
 * starts at entry n of `count`: its constant variables numbering the entries
 * (min 0, the retail or the registry's count - 1) keep their starting value in
 * their offset (the variable's `save`: the game writes the last choice back
 * there), each an offset of the middle one, the variable of its name list;
 * after the registry has widened them to its count. */
static void StartAt(const InterfaceHeader *h, u16 id, s32 n, u32 count, u32 retail) {
    const IfaceObjectDef *objs = GETP(h->objects);
    IfaceVarList *vl;
    s32 mid = -1, delta;
    u32 i;

    for (i = 0; objs != NULL && i < h->numObjects && h->id != id; i++) {
        if ((objs[i].type == IFACE_DEF_CHILD || objs[i].type == IFACE_DEF_CHILD_SYNC) && GETP(objs[i].child) != NULL &&
            ((const InterfaceHeader *)GETP(*GETP(objs[i].child)))->id == id) {
            h = (const InterfaceHeader *)GETP(*GETP(objs[i].child));
            objs = GETP(h->objects);
            break;
        }
    }
    if (h->id != id || (vl = GETP(h->vars)) == NULL || n < 0 || (u32)n >= count) {
        return;
    }
    Port_ContentPatchInterface((InterfaceHeader *)h);   /* widened and rebased first (it does that once) */
    for (i = 0; objs != NULL && i < h->numObjects && mid < 0; i++) {
        const IfaceText *t = objs[i].type == IFACE_DEF_TEXT ? GETP(objs[i].sub) : NULL;
        const IfaceStringList *l = t != NULL && t->listMode == 1 && !t->paragraph ? GETP(t->list) : NULL;

        if (l != NULL && l->fromVar && l->var < vl->count) {
            mid = l->var;
        }
    }
    if (mid < 0) {
        return;
    }
    delta = n - GETP(vl->vars)[mid].offset;
    for (i = 0; i < vl->count; i++) {
        IfaceVar *v = &GETP(vl->vars)[i];

        if (v->type == IFACE_VAR_CONST && v->min == 0 && (v->max == (s32)retail - 1 || v->max == (s32)count - 1)) {
            v->offset = (s32)(((v->offset + delta) % (s32)count + (s32)count) % (s32)count);
        }
    }
}

/* A world of progression p by name or index; -1 if there's none. */
static s32 WorldIndex(s32 p, const char *w) {
    MCP *mcp = GameSettings_GetMCP();
    const ProgScript *ps;
    char *end;
    s32 i;

    if (mcp == NULL || p < 0 || (u32)p >= mcp->numScripts || (ps = GETP(GETP(mcp->scripts)[p])) == NULL) {
        return -1;
    }
    i = (s32)SDL_strtol(w, &end, 10);
    if (*w != 0 && *end == 0) {
        return i >= 0 && i < ps->count ? i : -1;
    }
    for (i = 0; i < ps->count; i++) {
        const char *name = GETP(GETP(ps->levels)[i].world), *c;

        for (c = name; name != NULL && *c != 0; c++) {
            if (*c == '/' || *c == '\\') {
                name = c + 1;
            }
        }
        if (name != NULL && SDL_strcasecmp(name, w) == 0) {
            return i;
        }
    }
    return -1;
}

/* HWVX_START is set: the game manager skips its steps' screens (the
 * press-start and memory card screens), loading what they would. */
int Port_StartSkipsIntro(void) {
    const char *e = SDL_getenv("HWVX_START");

    return e != NULL && *e != 0;
}

static void *s_titleScript;
static s32 s_pickCar = -1;     /* itemselect: the car select picks this car, then goes on */
static u32 s_pickFrames;

/* Every frame (InterfaceManager_Heartbeat): for itemselect, the car select
 * picks its car as a press of A on it would (its selected window's A event;
 * the wrapper's start event then goes on to the item select, which takes
 * over the car select's car). Tried every 20 frames until the item select
 * is there: the car select refuses while the car is loading. */
void Port_StartHeartbeat(void) {
    u32 i, j;

    if (s_pickCar < 0 || ++s_pickFrames % 20 != 0) {
        return;
    }
    for (i = 0; i < IFACE_MAX; i++) {
        if (g_interfaces[i].count != 0 && GETP(g_interfaces[i].header)->id == 3) {
            Port_Log("start: the car select picked car %d; the item select is up", (int)s_pickCar);
            s_pickCar = -1;
            return;
        }
    }
    for (i = 0; i < IFACE_MAX; i++) {
        Interface *f = &g_interfaces[i];
        const InterfaceHeader *h = f->count != 0 ? GETP(f->header) : NULL;

        if (h == NULL || h->id != 2 || f->state != IFACE_ACTIVE || GameSettings_GetCodeVariable(0xC9, f->player) == 3) {
            continue;
        }
        for (j = 0; j < h->events.count && GETP(h->events.events) != NULL; j++) {
            if (GETP(h->events.events)[j].type == 4) {   /* A, on the screen (the carousel has no rows) */
                InterfaceManager_PlayEvent(f, NULL, (u8 *)&GETP(h->events.events)[j], 1);
                break;
            }
        }
    }
}

/* HWVX_START, from StateTitleScreen_Init as it would create the title's own
 * screen `script`: 1 if it takes over (once), and Port_StartTitleLate does
 * the rest once the title screen has its player (the selects take human
 * player 0 as they open) and has reset the choices. */
int Port_StartTitle(void *script) {
    static int done;
    const char *e = SDL_getenv("HWVX_START");

    if (done || e == NULL || *e == 0 || script == NULL) {
        return 0;
    }
    done = 1;
    s_titleScript = script;
    return 1;
}

static int StartTitle(void *script);

void Port_StartTitleLate(void) {
    void *script = s_titleScript;

    s_titleScript = NULL;
    if (script != NULL && !StartTitle(script)) {
        InterfaceManager_Create(script, 0, TitleScreen_Interface);
    }
}

/* 1 if a screen was created, or a race queued. */
static int StartTitle(void *script) {
    const char *e = SDL_getenv("HWVX_START");
    char mode[16], world[64] = "0", modeName[16] = "joyride";
    const char *args;
    s32 car = 0, item = 0, w, p;
    Found f;
    size_t len, m;

    args = SDL_strchr(e, ':');
    len = args != NULL ? (size_t)(args - e) : SDL_strlen(e);
    SDL_strlcpy(mode, e, len + 1 < sizeof(mode) ? len + 1 : sizeof(mode));
    while (args != NULL && *args != 0) {
        char key[16], val[64];
        const char *eq, *end;

        args++;
        end = SDL_strchr(args, ',');
        end = end != NULL ? end : args + SDL_strlen(args);
        eq = SDL_strchr(args, '=');
        if (eq == NULL || eq > end || (size_t)(eq - args) >= sizeof(key) || (size_t)(end - eq - 1) >= sizeof(val)) {
            break;
        }
        SDL_strlcpy(key, args, (size_t)(eq - args) + 1);
        SDL_strlcpy(val, eq + 1, (size_t)(end - eq));
        if (SDL_strcmp(key, "car") == 0) {
            car = SDL_atoi(val);
        } else if (SDL_strcmp(key, "item") == 0) {
            item = SDL_atoi(val);
        } else if (SDL_strcmp(key, "mode") == 0) {
            SDL_strlcpy(modeName, val, sizeof(modeName));
        } else if (SDL_strcmp(key, "world") == 0 || SDL_strcmp(key, "level") == 0) {
            SDL_strlcpy(world, val, sizeof(world));
        }
        args = end;
    }
    for (m = 0; m < SDL_arraysize(s_modes) && SDL_strcmp(s_modes[m].name, modeName) != 0; m++) {
    }
    if (m == SDL_arraysize(s_modes)) {
        Port_Log("start: no mode %s (adventure, mission, challenge, drag, battle, joyride)", modeName);
        return 0;
    }
    p = s_modes[m].progression;
    WalkAll(script);
    Port_ContentLearnTracks(script);   /* the registered tracks' levels, before a world is looked up */
    Find(p, &f);
    /* a car or item there isn't has no pack: the game would go on without a car */
    if (car < 0 || (u32)car >= Port_ContentCount(PORT_CONTENT_CAR)) {
        Port_Log("start: no car %d (0..%u); car 0", (int)car, (unsigned)Port_ContentCount(PORT_CONTENT_CAR) - 1);
        car = 0;
    }
    if (item < 0 || (u32)item >= Port_ContentCount(PORT_CONTENT_ITEM)) {
        Port_Log("start: no item %d (0..%u); item 0", (int)item, (unsigned)Port_ContentCount(PORT_CONTENT_ITEM) - 1);
        item = 0;
    }
    if ((w = WorldIndex(p, world)) < 0) {
        Port_Log("start: progression %d has no world %s; HWVX_START ignored", (int)p, world);
        return 0;
    }
    if (SDL_strcmp(mode, "menu") == 0) {
        const InterfaceHeader *h = RowTarget(s_modes[m].row);

        if (h == NULL) {
            Port_Log("start: no %s row in the menus; HWVX_START ignored", s_modes[m].row);
            return 0;
        }
        Port_Log("start: menu, %s (the %s row's screen)", modeName, s_modes[m].row);
        InterfaceManager_Create((InterfaceHeader *)h, 0, TitleScreen_Interface);
        return 1;
    }
    if (f.options != NULL) {
        ApplyOptions(&f);
    }
    StateProgressionManager_SetProgressionID(p, w);
    GameSettings_SetCodeVariable(0xCB, 1, car);
    GameSettings_SetCodeVariable(0xCC, 1, item);
    if (SDL_strcmp(mode, "worldselect") == 0) {
        if (f.worldSelect == NULL) {
            Port_Log("start: %s has no world select in the menus; HWVX_START ignored", modeName);
            return 0;
        }
        Port_Log("start: worldselect, %s (progression %d) at world %d", modeName, (int)p, (int)w);
        if (SDL_strcmp(world, "0") != 0) {
            Port_ContentTrackSelectAt((InterfaceHeader *)f.worldSelect, w);
        }
        InterfaceManager_Create((InterfaceHeader *)f.worldSelect, 0, TitleScreen_Interface);
        return 1;
    }
    if (SDL_strcmp(mode, "carselect") == 0 || SDL_strcmp(mode, "itemselect") == 0) {
        const InterfaceHeader *h = mode[0] == 'c' ? f.carSelect : f.itemSelect != NULL ? f.carSelect : NULL;

        if (h == NULL) {
            Port_Log("start: %s has no %s in the menus; HWVX_START ignored", modeName, mode);
            return 0;
        }
        Port_Log("start: %s, %s (progression %d) world %d, car %d item %d", mode, modeName, (int)p, (int)w, (int)car,
                 (int)item);
        StartAt(f.carSelect, 2, car, Port_ContentCount(PORT_CONTENT_CAR), Port_ContentRetailCount(PORT_CONTENT_CAR));
        if (f.itemSelect != NULL) {
            StartAt(f.itemSelect, 3, item, Port_ContentCount(PORT_CONTENT_ITEM),
                    Port_ContentRetailCount(PORT_CONTENT_ITEM));
        }
        if (mode[0] == 'i') {
            /* through the car select, which loads the car the item select shows */
            h = f.carSelect;
            s_pickCar = car;
        }
        InterfaceManager_Create((InterfaceHeader *)h, 0, TitleScreen_Interface);
        return 1;
    }
    if (SDL_strcmp(mode, "race") == 0) {
        Port_Log("start: race, %s (progression %d) world %d, car %d item %d", modeName, (int)p, (int)w, (int)car,
                 (int)item);
        StateManager_QueueChange(0xB);
        return 1;
    }
    Port_Log("start: HWVX_START=%s: carselect, itemselect or race", e);
    return 0;
}
