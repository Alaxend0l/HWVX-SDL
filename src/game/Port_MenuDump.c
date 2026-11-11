/* Debugging: HWVX_MENUDUMP=1 logs every interface the game creates, and every
 * screen reachable from it through go-to events and child slots, once each:
 * objects with their texts, rows (record id, up/down ring) and what each
 * event does (go-to targets, the code variables an action list sets, the
 * progression a 0xF event picks); =2 adds every object, images too. First the
 * MCP's progression scripts and their worlds. Port-only file; Port_MenuPatch
 * calls it before patching, so the dump shows the release's screens as its
 * pack has them. Screens are named S<n> in the order found, with their byte
 * offset from the first one dumped (docs/menus/README.md is the inventory
 * taken with it). */
#include "common.h"
#include "Interface.h"

#define DUMP_MAX 512

static const InterfaceHeader *s_seen[DUMP_MAX];
static u32 s_numSeen;
static const u8 *s_base;
static int s_level;            /* HWVX_MENUDUMP: 1 rows and texts, 2 every object */

static s32 ScreenNo(const InterfaceHeader *h) {
    u32 i;

    for (i = 0; i < s_numSeen; i++) {
        if (s_seen[i] == h) {
            return (s32)i;
        }
    }
    return -1;
}

static const char *Text(const IfaceObjectDef *d) {
    IfaceText *t;

    if ((d->type != IFACE_DEF_TEXT && d->type != IFACE_DEF_DEBUG_TEXT) || (t = GETP(d->sub)) == NULL ||
        GETP(t->strings) == NULL || GETP(*GETP(t->strings)) == NULL) {
        return NULL;
    }
    return GETP(*GETP(t->strings));
}

static void Queue(const InterfaceHeader *h, const InterfaceHeader **queue, u32 *n) {
    if (h != NULL && ScreenNo(h) < 0 && s_numSeen < DUMP_MAX) {
        s_seen[s_numSeen++] = h;
        queue[(*n)++] = h;
    }
}

/* An action list (event type 0xB): "c<id>=<value>" per code variable set,
 * "v<var>" for a variable's value, "o<object>:<state>" per object state. */
static void Actions(char *out, size_t size, const IfaceActionList *list) {
    u32 i;

    for (i = 0; list != NULL && i < list->count && GETP(list->actions) != NULL; i++) {
        const IfaceAction *a = &GETP(list->actions)[i];
        size_t len = strlen(out);

        if (a->type == 2 && GETP(a->data) != NULL) {
            const IfaceCodeAction *c = GETP(a->data);
            const IfaceCodeVar *cv = GETP(c->code);
            const IfaceVarValue *v = GETP(c->value);

            snprintf(out + len, size - len, "%sc%X=%s%u", i ? "," : "", cv != NULL ? cv->id : 0,
                     v != NULL && v->isVar ? "v" : "", v != NULL ? v->value : 0);
        } else if (a->type == 1 && GETP(a->data) != NULL) {
            const IfaceStateAction *st = GETP(a->data);

            snprintf(out + len, size - len, "%so%u:%u", i ? "," : "", st->object, st->state);
        } else {
            snprintf(out + len, size - len, "%sa%u", i ? "," : "", a->type);
        }
    }
}

static void Events(char *out, size_t size, const IfaceEventList *list, const InterfaceHeader **queue, u32 *n) {
    u32 i;

    for (i = 0; i < list->count && GETP(list->events) != NULL; i++) {
        const IfaceEvent *e = &GETP(list->events)[i];
        const IfaceEventDef *d = GETP(e->def);
        size_t len = strlen(out);

        if (d == NULL) {
            continue;
        }
        if (d->type == 1 || d->type == 0x20 || d->type == 0x1F) {
            Queue((const InterfaceHeader *)GETP(d->target), queue, n);
            snprintf(out + len, size - len, " %X>%s:S%d", e->type, d->type == 1 ? "go" : d->type == 0x20 ? "goP" : "new",
                     ScreenNo((const InterfaceHeader *)GETP(d->target)));
        } else if (d->type == 0xB) {
            snprintf(out + len, size - len, " %X>act[", e->type);
            Actions(out, size, GETP(d->target));
            len = strlen(out);
            snprintf(out + len, size - len, "]");
        } else if (d->type == 0xF && GETP(d->args) != NULL) {
            const PTR32(IfaceVarValue) *slots = GETP(d->args);
            const IfaceVarValue *a = GETP(slots[0]), *b = GETP(slots[1]);

            snprintf(out + len, size - len, " %X>prog(%s%u,%s%u)", e->type, a->isVar ? "v" : "", a->value,
                     b->isVar ? "v" : "", b->value);
        } else if (d->type == 2) {
            snprintf(out + len, size - len, " %X>back%u", e->type, d->id);
        } else {
            snprintf(out + len, size - len, " %X>t%X(%u)", e->type, d->type, d->id);
        }
    }
}

static void DumpScreen(const InterfaceHeader *h, const InterfaceHeader **queue, u32 *n) {
    const IfaceObjectDef *objs = GETP(h->objects);
    char line[2048];
    u32 i;

    line[0] = 0;
    Events(line, sizeof(line), &h->events, queue, n);
    Port_Log("menudump: S%d %+ld id %u, %u objects, events:%s", ScreenNo(h),
             (long)((const u8 *)h - s_base), h->id, h->numObjects, line);
    for (i = 0; i < h->numObjects && objs != NULL; i++) {
        const IfaceObjectDef *d = &objs[i];
        const IfaceRecord *r = GETP(d->record);
        const char *t = Text(d);
        const IfaceObjAnim *a = GETP(d->anim);
        f32 x = a != NULL && GETP(a->pos.keys) != NULL ? ((f32 *)GETP(a->pos.keys))[0] : -1.0f;
        f32 y = a != NULL && GETP(a->pos.keys) != NULL ? ((f32 *)GETP(a->pos.keys))[1] : -1.0f;

        line[0] = 0;
        if ((d->type == IFACE_DEF_CHILD || d->type == IFACE_DEF_CHILD_SYNC) && GETP(d->child) != NULL) {
            const InterfaceHeader *c = (const InterfaceHeader *)GETP(*GETP(d->child));

            Queue(c, queue, n);
            snprintf(line, sizeof(line), " child S%d", ScreenNo(c));
        }
        if (r != NULL) {
            size_t len = strlen(line);

            snprintf(line + len, sizeof(line) - len, " rec %u [%u %u %u %u]", r->id, r->next[0], r->next[1],
                     r->next[2], r->next[3]);
            Events(line, sizeof(line), &r->events, queue, n);
        }
        if (t == NULL && line[0] == 0 && s_level < 2) {
            continue;           /* images and the like */
        }
        if (s_level >= 2) {
            size_t len = strlen(line);
            const IfaceConditionList *cl = GETP(d->conditions);

            u32 c;

            snprintf(line + len, sizeof(line) - len, " x %.0f active %u", x, d->active);
            for (c = 0; cl != NULL && c < cl->count && GETP(cl->conds) != NULL; c++) {
                const IfaceCondition *cd = &GETP(cl->conds)[c];

                len = strlen(line);
                if (cd->type == 2 && GETP(cd->data) != NULL) {
                    snprintf(line + len, sizeof(line) - len, " %s%u:c%X==%d", c ? "" : "if", cl->type,
                             ((const IfaceCodeVar *)GETP(cd->data))->id, cd->value);
                } else {
                    snprintf(line + len, sizeof(line) - len, " %s%u:t%u==%d", c ? "" : "if", cl->type, cd->type,
                             cd->value);
                }
            }
        }
        Port_Log("menudump:   %2u type %u role %u y %.0f%s%s%s%s", i, d->type, d->role, y, t != NULL ? " \"" : "",
                 t != NULL ? t : "", t != NULL ? "\"" : "", line);
    }
}

void Port_MenuDump(const InterfaceHeader *root) {
    static int on = -1;
    const InterfaceHeader *queue[DUMP_MAX];
    u32 n = 0, k;

    if (on < 0) {
        const char *e = getenv("HWVX_MENUDUMP");
        on = e != NULL && *e != '0';
        s_level = on ? atoi(e) : 0;
    }
    if (!on || root == NULL || ScreenNo(root) >= 0) {
        return;
    }
    if (s_base == NULL) {
        MCP *mcp = GameSettings_GetMCP();
        u32 i, j;

        s_base = (const u8 *)root;
        /* the progression scripts the menus' 0xF events pick: levels, worlds */
        for (i = 0; mcp != NULL && i < mcp->numScripts; i++) {
            const ProgScript *ps = GETP(GETP(mcp->scripts)[i]);
            char line[512];

            line[0] = 0;
            for (j = 0; ps != NULL && j < (u32)ps->count && strlen(line) < sizeof(line) - 40; j++) {
                const ProgLevel *lv = &GETP(ps->levels)[j];
                size_t len = strlen(line);

                snprintf(line + len, sizeof(line) - len, " %s", GETP(lv->world) != NULL ? GETP(lv->world) : "-");
            }
            Port_Log("menudump: progression %u: %d levels:%s", i, ps != NULL ? ps->count : -1, line);
        }
    }
    Queue(root, queue, &n);
    for (k = 0; k < n; k++) {
        DumpScreen(queue[k], queue, &n);
    }
}
