/* Wider selection strips: the world, car and weapon select screens show
 * NEW_SIDE windows on each side of the big one instead of 2, for wide
 * screens. Port-only file.
 *
 * The screens are data (HW_IPack.xgc) and share one layout. Variable D
 * (const, -1..1) is the direction the strip is sliding, 0 at rest. Objects
 * shown while D == 0 are the windows at rest: at five x positions, a picture
 * (an image whose texture key is a variable: the entry at that slot), a frame
 * and a lock (shown while a code variable of the slot's entry is 0). Objects
 * shown while D is 1 or -1 slide one slot right or left in their state
 * animation (key 0 at the slot they leave, key 1 at the one they reach); the
 * pieces entering at either end come from off screen.
 *
 * The slot variables are all one variable with offsets: a code variable
 * (the selection) + offset, or a constant the d-pad steps move together,
 * wrapped to min..max. So a slot k further out is a copy of the middle
 * slot's variable with offset + k, stepped like it.
 *
 * Port_CarouselPatch (from Port_MenuPatch) recognises such a header and
 * gives it copies of its objects, variables and records with the windows
 * added: at rest, slots -NEW_SIDE..NEW_SIDE; sliding, the pieces for slots
 * one further out (the old off-screen entry points become slots -3 and 3). New objects go at
 * the end, so object indices in actions stay valid. The extra slots continue
 * the outer spacing and lie beyond the 4:3 screen: at 16:9 the third on each
 * side shows, at 21:9 the fourth. Copies live in static arenas, one per
 * widened screen; the pack data they point to stays where it is. */
#include "common.h"
#include "Interface.h"

#define NEW_SIDE 5              /* windows on each side at rest */
#define MAX_VARS 30             /* Interface.vars */
#define MAX_OBJS 200
#define MAX_PATCHED 10
#define ARENA_SIZE (96 * 1024)

/* A widened screen: its header and the copies, in an arena of its own. The
 * copies are dead once the header no longer points at them (the pack was
 * released, or loaded again: after a race the menus come from a fresh load),
 * and the arena is reused. */
typedef struct Patched {
    InterfaceHeader *header;    /* NULL: arena free */
    IfaceObjectDef *newObjects;
} Patched;

static Patched s_patched[MAX_PATCHED];
static u8 s_arenas[MAX_PATCHED][ARENA_SIZE] __attribute__((aligned(16)));
static u8 *s_arena;             /* the one being filled */
static u32 s_arenaUsed;

static void *Alloc(u32 size) {
    void *p;

    size = (size + 15) & ~15u;
    if (s_arenaUsed + size > ARENA_SIZE) {
        return NULL;
    }
    p = &s_arena[s_arenaUsed];
    s_arenaUsed += size;
    memset(p, 0, size);
    return p;
}

static void *Copy(const void *src, u32 size) {
    void *p = Alloc(size);

    if (p != NULL && src != NULL) {
        memcpy(p, src, size);
    }
    return p;
}

/* Position / scale keys (ExtractScaleKey): 0x20 bytes, x first. */
typedef struct VecKey {
    f32 x, y, z, w;
    u8 active, interp, pad[2];
    f32 duration;
    u8 pad2[8];
} VecKey;
_Static_assert(sizeof(VecKey) == 0x20, "VecKey size");

/* ---- the layout ---------------------------------------------------------- */

enum { PART_PICTURE, PART_FRAME, PART_LOCK };

typedef struct Strip {
    InterfaceHeader *h;
    IfaceObjectDef *objs;       /* the copy being built */
    u32 numObjs;
    u32 maxObjs;
    IfaceVar *vars;             /* the copy being built */
    u32 numVars;
    u32 dirVar;                 /* D */
    f32 slotX[5];               /* at rest, slots -2..2 */
    f32 y;
    s32 pictureVar;             /* the middle picture's variable */
    s32 lockVar;                /* the middle lock's code variable argument */
    s8 ok;
} Strip;

/* -1, 0, 1 when the object is shown while D has that value, else 2. */
static s32 DirOf(Strip *s, const IfaceObjectDef *d) {
    IfaceConditionList *cl = GETP(d->conditions);
    u32 i;

    if (d->type != IFACE_DEF_IMAGE || cl == NULL || cl->type != 1) {
        return 2;
    }
    for (i = 0; i < cl->count; i++) {
        IfaceCondition *c = &GETP(cl->conds)[i];
        IfaceVarValue *v = GETP(c->data);

        if (c->type == 1 && v != NULL && !v->isVar && v->value == s->dirVar && c->value >= -1 && c->value <= 1) {
            return c->value;
        }
    }
    return 2;
}

static IfaceTexAnim *PictureAnim(const IfaceObjectDef *d) {
    IfaceImageDef *im = GETP(d->sub);

    return im != NULL && im->type == 3 ? GETP(im->anim) : NULL;
}

/* The lock's code variable condition (argument is a variable), or NULL. */
static IfaceCondition *LockCond(const IfaceObjectDef *d) {
    IfaceConditionList *cl = GETP(d->conditions);
    u32 i;

    for (i = 0; cl != NULL && i < cl->count; i++) {
        IfaceCondition *c = &GETP(cl->conds)[i];
        IfaceCodeVar *cv = c->type == 2 ? GETP(c->data) : NULL;

        if (cv != NULL && GETP(cv->arg) != NULL && GETP(cv->arg)->isVar) {
            return c;
        }
    }
    return NULL;
}

static s32 PartOf(const IfaceObjectDef *d) {
    if (PictureAnim(d) != NULL) {
        return PART_PICTURE;
    }
    return LockCond(d) != NULL ? PART_LOCK : PART_FRAME;
}

static VecKey *PosKeys(IfaceKeyTrack *t) {
    return t->count != 0 ? (VecKey *)GETP(t->keys) : NULL;
}

static f32 RestX(const IfaceObjectDef *d) {
    IfaceObjAnim *a = GETP(d->anim);
    VecKey *k = a != NULL ? PosKeys(&a->pos) : NULL;

    return k != NULL ? k->x : 0.0f;
}

/* The slide (state 0 animation) of an object shown while D != 0. */
static VecKey *SlideKeys(const IfaceObjectDef *d) {
    IfaceStateDef *st = d->numStates != 0 ? GETP(d->states) : NULL;
    IfaceStateAnim *sa = st != NULL ? GETP(st[0].anim) : NULL;

    return sa != NULL && sa->pos.count == 2 ? PosKeys(&sa->pos) : NULL;
}

static f32 Abs(f32 x) {
    return x < 0.0f ? -x : x;
}

static f32 SlotX(Strip *s, s32 k) {
    f32 dx = s->slotX[1] - s->slotX[0];

    if (k < -2) {
        return s->slotX[0] + (k + 2) * dx;
    }
    if (k > 2) {
        return s->slotX[4] + (k - 2) * dx;
    }
    return s->slotX[k + 2];
}

/* The slot at x; x beyond the outer windows: the old off-screen points. */
static s32 SlotAt(Strip *s, f32 x) {
    s32 k;

    if (x < s->slotX[0] - 1.0f) {
        return -3;
    }
    if (x > s->slotX[4] + 1.0f) {
        return 3;
    }
    for (k = -2; k <= 2; k++) {
        if (Abs(x - s->slotX[k + 2]) < 1.0f) {
            return k;
        }
    }
    return 99;
}

/* ---- variables ----------------------------------------------------------- */

static int SameCode(const IfaceVar *a, const IfaceVar *b) {
    IfaceCodeVar *ca = GETP(a->code), *cb = GETP(b->code);
    IfaceVarValue *aa, *ab;

    if (ca == NULL || cb == NULL) {
        return ca == cb;
    }
    aa = GETP(ca->arg);
    ab = GETP(cb->arg);
    return ca->id == cb->id && (aa == ab || (aa != NULL && ab != NULL && aa->isVar == ab->isVar && aa->value == ab->value));
}

/* The variable for slot k: the middle variable `base` with offset + k. An
 * existing one with that offset is reused, else one is added; records that
 * step `base` step it the same way. -1: no room. */
static s32 SlotVar(Strip *s, s32 base, s32 k) {
    IfaceVar *b = &s->vars[base];
    s32 n = b->max - b->min + 1;
    s32 off = b->min + (((b->offset - b->min + k) % n) + n) % n;
    u32 i, j;

    if (k == 0) {
        return base;
    }
    for (i = 0; i < s->numVars; i++) {
        IfaceVar *v = &s->vars[i];

        if (v->type == b->type && v->min == b->min && v->max == b->max && v->offset == off &&
            SameCode(v, b) && v->update == b->update && v->save == b->save) {
            return (s32)i;
        }
    }
    if (s->numVars >= MAX_VARS) {
        return -1;
    }
    i = s->numVars++;
    s->vars[i] = *b;
    s->vars[i].offset = off;
    /* step it along with base */
    for (j = 0; j < s->numObjs; j++) {
        IfaceRecord *r = GETP(s->objs[j].record);
        IfaceStep *steps;
        u32 m;

        if (r == NULL || r->numSteps == 0) {
            continue;
        }
        steps = GETP(r->steps);
        for (m = 0; m < r->numSteps && steps[m].var != (u32)base; m++) {
        }
        if (m == r->numSteps) {
            continue;
        }
        r = Copy(r, sizeof(IfaceRecord));
        steps = Copy(steps, (r->numSteps + 1) * sizeof(IfaceStep));
        if (r == NULL || steps == NULL) {
            return -1;
        }
        steps[r->numSteps] = steps[m];
        steps[r->numSteps].var = i;
        r->numSteps++;
        SETP(r->steps, steps);
        SETP(s->objs[j].record, r);
    }
    return (s32)i;
}

/* ---- objects ------------------------------------------------------------- */

/* Gives d its own copy of what is changed below. */
static int OwnAnims(IfaceObjectDef *d) {
    IfaceObjAnim *a = GETP(d->anim);
    IfaceStateDef *st = GETP(d->states);
    u32 i;

    if (a != NULL) {
        a = Copy(a, sizeof(*a));
        if (a == NULL) {
            return 0;
        }
        if (a->pos.count != 0) {
            SETP(a->pos.keys, Copy(GETP(a->pos.keys), a->pos.count * sizeof(VecKey)));
        }
        SETP(d->anim, a);
    }
    if (st != NULL && d->numStates != 0) {
        st = Copy(st, d->numStates * sizeof(IfaceStateDef));
        if (st == NULL) {
            return 0;
        }
        for (i = 0; i < d->numStates; i++) {
            IfaceStateAnim *sa = GETP(st[i].anim);

            if (sa != NULL) {
                sa = Copy(sa, sizeof(*sa));
                if (sa == NULL) {
                    return 0;
                }
                if (sa->pos.count != 0) {
                    SETP(sa->pos.keys, Copy(GETP(sa->pos.keys), sa->pos.count * sizeof(VecKey)));
                }
                SETP(st[i].anim, sa);
            }
        }
        SETP(d->states, st);
    }
    return 1;
}

/* Every position key at x `from` (default and state animations) to `to`. */
static void MoveX(IfaceObjectDef *d, f32 from, f32 to) {
    IfaceObjAnim *a = GETP(d->anim);
    IfaceStateDef *st = GETP(d->states);
    VecKey *k;
    u32 i, j;

    if (a != NULL && (k = PosKeys(&a->pos)) != NULL) {
        for (j = 0; j < a->pos.count; j++) {
            if (Abs(k[j].x - from) < 1.0f) {
                k[j].x = to;
            }
        }
    }
    for (i = 0; st != NULL && i < d->numStates; i++) {
        IfaceStateAnim *sa = GETP(st[i].anim);

        if (sa != NULL && (k = PosKeys(&sa->pos)) != NULL) {
            for (j = 0; j < sa->pos.count; j++) {
                if (Abs(k[j].x - from) < 1.0f) {
                    k[j].x = to;
                }
            }
        }
    }
}

/* Points d's picture or lock at slot k's entry. */
static int SetSlotVars(Strip *s, IfaceObjectDef *d, s32 k) {
    IfaceTexAnim *ta = PictureAnim(d);
    IfaceCondition *lc = LockCond(d);
    s32 v;

    if (ta != NULL) {
        IfaceImageDef *im = Copy(GETP(d->sub), sizeof(IfaceImageDef));

        v = SlotVar(s, s->pictureVar, k);
        ta = Copy(ta, sizeof(IfaceTexAnim));
        if (v < 0 || im == NULL || ta == NULL) {
            return 0;
        }
        ta->fromVar = 1;   /* one sliding picture in the car select has it off */
        ta->var = (u16)v;
        SETP(im->anim, ta);
        SETP(d->sub, im);
    }
    if (lc != NULL && s->lockVar >= 0) {
        IfaceConditionList *cl = Copy(GETP(d->conditions), sizeof(IfaceConditionList));
        IfaceCondition *conds;
        IfaceCodeVar *cv;
        IfaceVarValue *arg;
        u32 i;

        v = SlotVar(s, s->lockVar, k);
        if (v < 0 || cl == NULL) {
            return 0;
        }
        conds = Copy(GETP(cl->conds), cl->count * sizeof(IfaceCondition));
        if (conds == NULL) {
            return 0;
        }
        for (i = 0; i < cl->count; i++) {
            if (&GETP(GETP(d->conditions)->conds)[i] == lc) {
                cv = Copy(GETP(conds[i].data), sizeof(IfaceCodeVar));
                arg = cv != NULL ? Copy(GETP(cv->arg), sizeof(IfaceVarValue)) : NULL;
                if (arg == NULL) {
                    return 0;
                }
                arg->value = (u16)v;
                SETP(cv->arg, arg);
                SETP(conds[i].data, cv);
            }
        }
        SETP(cl->conds, conds);
        SETP(d->conditions, cl);
    }
    return 1;
}

static IfaceObjectDef *AddObject(Strip *s, const IfaceObjectDef *from) {
    IfaceObjectDef *d;

    if (s->numObjs >= s->maxObjs) {
        return NULL;
    }
    d = &s->objs[s->numObjs++];
    *d = *from;
    return OwnAnims(d) ? d : NULL;
}

/* At rest: windows for slots out to +-NEW_SIDE, copies of the outer ones. */
static int AddRest(Strip *s, u32 orig) {
    s32 part, k;
    u32 i;

    for (part = PART_PICTURE; part <= PART_LOCK; part++) {
        for (k = -NEW_SIDE; k <= NEW_SIDE; k++) {
            s32 from = k < 0 ? -2 : 2;

            if (k >= -2 && k <= 2) {
                continue;
            }
            for (i = 0; i < orig; i++) {
                IfaceObjectDef *d;

                if (DirOf(s, &s->objs[i]) != 0 || PartOf(&s->objs[i]) != part ||
                    SlotAt(s, RestX(&s->objs[i])) != from) {
                    continue;
                }
                if ((d = AddObject(s, &s->objs[i])) == NULL) {
                    return 0;
                }
                MoveX(d, SlotX(s, from), SlotX(s, k));
                if (!SetSlotVars(s, d, k)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Sliding by dir: the pieces reaching slots out to +-(NEW_SIDE + 1), copies of
 * the outermost ones, and the old off-screen points moved to slots +-3. */
static int AddSlide(Strip *s, u32 orig, s32 dir) {
    s32 part, k, lo = 99, hi = -99;
    u32 i;

    for (i = 0; i < orig; i++) {
        VecKey *key;
        s32 a, b;

        if (DirOf(s, &s->objs[i]) != dir || (key = SlideKeys(&s->objs[i])) == NULL) {
            continue;
        }
        a = SlotAt(s, key[0].x);
        b = SlotAt(s, key[1].x);
        if (a == 99 || b == 99 || b - a != dir) {
            continue;
        }
        if (!OwnAnims(&s->objs[i])) {
            return 0;
        }
        key = SlideKeys(&s->objs[i]);
        key[0].x = SlotX(s, a);
        key[1].x = SlotX(s, b);
        if (PartOf(&s->objs[i]) != PART_FRAME && !SetSlotVars(s, &s->objs[i], b)) {
            return 0;
        }
        if (b != 0 && a != 0) {
            lo = b < lo ? b : lo;
            hi = b > hi ? b : hi;
        }
    }
    if (lo > hi) {
        return 0;
    }
    for (part = PART_PICTURE; part <= PART_LOCK; part++) {
        for (k = -NEW_SIDE - 1; k <= NEW_SIDE + 1; k++) {
            s32 from = k < 0 ? lo : hi;

            /* slots the slide reaches: entering from k - dir must be a
             * window (or the off-screen point next to the outermost) */
            if ((k >= lo && k <= hi) || k - dir < -NEW_SIDE - 1 || k - dir > NEW_SIDE + 1 ||
                k < -NEW_SIDE - 1 || k > NEW_SIDE + 1 ||
                (dir > 0 && k == -NEW_SIDE - 1) || (dir < 0 && k == NEW_SIDE + 1)) {
                continue;
            }
            for (i = 0; i < orig; i++) {
                IfaceObjectDef *d;
                VecKey *key;

                if (DirOf(s, &s->objs[i]) != dir || PartOf(&s->objs[i]) != part ||
                    (key = SlideKeys(&s->objs[i])) == NULL || SlotAt(s, key[1].x) != from) {
                    continue;
                }
                if ((d = AddObject(s, &s->objs[i])) == NULL) {
                    return 0;
                }
                key = SlideKeys(d);
                key[0].x = SlotX(s, k - dir);
                key[1].x = SlotX(s, k);
                if (!SetSlotVars(s, d, k)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Finds D, the five rest positions and the middle variables. */
static int Recognise(Strip *s) {
    IfaceVarList *vl = GETP(s->h->vars);
    IfaceObjectDef *objs = GETP(s->h->objects);
    f32 xs[8];
    u32 n = 0, i, j;

    if (vl == NULL || objs == NULL || vl->count > MAX_VARS || s->h->numObjects + 16 * (NEW_SIDE + 1) > MAX_OBJS) {
        return 0;
    }
    for (i = 0; i < vl->count; i++) {
        IfaceVar *v = &GETP(vl->vars)[i];

        if (v->type == IFACE_VAR_CONST && v->min == -1 && v->max == 1) {
            break;
        }
    }
    if (i == vl->count) {
        return 0;
    }
    s->dirVar = i;
    s->objs = objs;   /* read-only until the copy is made */
    s->numObjs = s->h->numObjects;
    for (i = 0; i < s->numObjs; i++) {
        f32 x;

        if (DirOf(s, &objs[i]) != 0 || PartOf(&objs[i]) != PART_PICTURE) {
            continue;
        }
        x = RestX(&objs[i]);
        for (j = 0; j < n && Abs(xs[j] - x) >= 1.0f; j++) {
        }
        if (j == n) {
            if (n == 8) {
                return 0;
            }
            xs[n++] = x;
        }
    }
    if (n != 5) {
        return 0;
    }
    for (i = 0; i < n; i++) {   /* sort */
        for (j = i + 1; j < n; j++) {
            if (xs[j] < xs[i]) {
                f32 t = xs[i];
                xs[i] = xs[j];
                xs[j] = t;
            }
        }
    }
    memcpy(s->slotX, xs, sizeof(s->slotX));
    s->pictureVar = -1;
    s->lockVar = -1;
    for (i = 0; i < s->numObjs; i++) {
        if (DirOf(s, &objs[i]) != 0 || SlotAt(s, RestX(&objs[i])) != 0) {
            continue;
        }
        if (PartOf(&objs[i]) == PART_PICTURE && PictureAnim(&objs[i])->fromVar) {
            s->pictureVar = PictureAnim(&objs[i])->var;
            s->y = GETP(objs[i].anim) != NULL ? PosKeys(&GETP(objs[i].anim)->pos)->y : 0.0f;
        } else if (PartOf(&objs[i]) == PART_LOCK) {
            s->lockVar = GETP(((IfaceCodeVar *)GETP(LockCond(&objs[i])->data))->arg)->value;
        }
    }
    return s->pictureVar >= 0 && (u32)s->pictureVar < vl->count &&
           (s->lockVar < 0 || (u32)s->lockVar < vl->count);
}

static int Live(const Patched *p) {
    return p->header != NULL && p->header->objects.h == HND(p->newObjects);
}

void Port_CarouselPatch(InterfaceHeader *h) {
    Strip s;
    Patched *p = NULL;
    IfaceVarList *vl;
    u32 i, orig, room;

    for (i = 0; i < MAX_PATCHED; i++) {
        if (s_patched[i].header == h && Live(&s_patched[i])) {
            return;   /* already ours */
        }
    }
    memset(&s, 0, sizeof(s));
    s.h = h;
    if (!Recognise(&s)) {
        return;
    }
    for (i = 0; i < MAX_PATCHED && p == NULL; i++) {
        if (!Live(&s_patched[i])) {
            p = &s_patched[i];
        }
    }
    if (p == NULL) {
        Port_Log("carousel: header %u not widened (all %d arenas in use)", h->id, MAX_PATCHED);
        return;
    }
    p->header = NULL;
    s_arena = s_arenas[p - s_patched];
    s_arenaUsed = 0;
    orig = h->numObjects;
    room = orig + 16 * (NEW_SIDE + 1) < MAX_OBJS ? orig + 16 * (NEW_SIDE + 1) : MAX_OBJS;
    vl = Copy(GETP(h->vars), sizeof(IfaceVarList));
    s.vars = Alloc(MAX_VARS * sizeof(IfaceVar));
    s.objs = Alloc(room * sizeof(IfaceObjectDef));
    if (vl == NULL || s.vars == NULL || s.objs == NULL) {
        return;
    }
    s.maxObjs = room;
    memcpy(s.objs, GETP(h->objects), orig * sizeof(IfaceObjectDef));
    memcpy(s.vars, GETP(GETP(h->vars)->vars), vl->count * sizeof(IfaceVar));
    s.numVars = vl->count;
    if (!AddRest(&s, orig) || !AddSlide(&s, orig, 1) || !AddSlide(&s, orig, -1)) {
        Port_Log("carousel: header %u not widened (out of room)", h->id);
        return;
    }
    Port_Log("carousel: header %u: %u -> %u objects, %u -> %u variables (%u bytes)", h->id, orig, s.numObjs,
             vl->count, s.numVars, s_arenaUsed);
    vl->count = s.numVars;
    SETP(vl->vars, s.vars);
    p->header = h;
    p->newObjects = s.objs;
    SETP(h->objects, s.objs);
    SETP(h->vars, vl);
    h->numObjects = s.numObjs;
}
