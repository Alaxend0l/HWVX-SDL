#include "common.h"

/* A tracer's description in the data pack. */
typedef struct TracerDef {
    u8 pad00[0x20];             /* 0x00 */
    u8 unk20;                   /* 0x20 */
    u8 axis;                    /* 0x21 trail along the thing's axis 0/1/2 */
    u8 pad22[0xE];              /* 0x22 */
    f32 length;                 /* 0x30 */
    f32 endLength;              /* 0x34 */
    f32 scrollSpeed;            /* 0x38 */
    f32 uvScale;                /* 0x3C */
    f32 fade;                   /* 0x40 */
    u8 headColour[8];           /* 0x44 colour keys (ExtractColorKey) */
    u8 tailColour[8];           /* 0x4C */
    u32 numFrames;              /* 0x54 */
    PTR32(u32) frames;          /* 0x58 */
} TracerDef;

_Static_assert(offsetof(TracerDef, length) == 0x30, "TracerDef.length");
_Static_assert(offsetof(TracerDef, frames) == 0x58, "TracerDef.frames");

/* A tracer thing: a Thing driving one of the trails in tracers[]. */
typedef struct TracerThing {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    PTR32(TracerDef) def;       /* 0xB0 */
    u32 index;                  /* 0xB4 into tracers[] */
} TracerThing;

_Static_assert(offsetof(TracerThing, def) == 0xB0, "TracerThing.def");

typedef struct Tracer {
    TracerPoint points[14];
    u8 unk460[0x48];
    PTR32(TracerPoint) unk4A8;
    u8 unk4AC[4];
    PTR32(TracerPoint) head;
    PTR32(TracerPoint) tail;
    s8 unk4B8[0x4];
    f32 unk4BC;
    f32 unk4C0;
    u32 unk4C4;
    u32 unk4C8;
    u16 unk4CC;
    u16 active;
    u16 unk4D0;
    u16 unk4D2;
    u16 unk4D4;
    u8 unk4D6[0xA];
} Tracer;

Tracer tracers[32];

static inline u32 Tracer_FindFree(void) {
    u32 i;

    for (i = 0; i < 32; i++) {
        if (tracers[i].active == 0) {
            break;
        }
    }
    return i;
}

s32 Tracer_Init(TracerThing *thing, u8 *data) {
    TracerDef *def;
    Tracer *tr;

    Thing_Init(thing, 0);
    SETP(thing->def, SLOT_LOAD(TracerDef *, (data + 0x30)));
    thing->base.pos = *(Vector4 *)data;
    thing->index = Tracer_FindFree();
    Tracer_ClearTracer(thing->index);
    tr = &tracers[thing->index];
    tr->active = 1;
    def = GETP(thing->def);
    if (def->numFrames != 0) {
        tr->unk4D0 = *GETP(def->frames);
    } else {
        tr->unk4D0 = 0xFFFF;
    }
    tr->unk4D2 = def->unk20;
    tr->unk4BC = -1.0f * def->fade / 14.0f;
    tr->unk4C4 = 0;
    tr->unk4C8 = 0;
    tr->unk4C0 = 0.0f;
    return 1;
}

s32 Tracer_Move(TracerThing *thing) {
    typedef struct KeyState {
        f32 slope[4];
        f32 value[4];
        f32 time;
        u8 c[4];
        u16 index;
        u16 current;
        u8 reset;
    } KeyState;
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    KeyState colour;
    KeyState colour2;
    s32 i;
    TracerPoint *p;
    Tracer *tr = &tracers[thing->index];
    TracerDef *def = GETP(thing->def);

    p = GETP(tr->head);
    for (i = 0; i < tr->unk4CC; i++) {
        s32 c;
        p->pos[0] += p->unk20[0];
        p->pos[1] += p->unk20[1];
        p->pos[2] += p->unk20[2];
        p->vel[0] += p->unk30[0];
        p->vel[1] += p->unk30[1];
        p->vel[2] += p->unk30[2];
        p->unk40 += tr->unk4BC;
        c = p->color[0] + tr->unk4B8[0];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[0] = c;
        c = p->color[1] + tr->unk4B8[1];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[1] = c;
        c = p->color[2] + tr->unk4B8[2];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[2] = c;
        c = p->color[3] + tr->unk4B8[3];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[3] = c;
        p = GETP(p->next);
    }
    switch (def->axis) {
    case 0:
        AccumulateVector(&thing->base.axis[0], &thing->base.pos, GETP(tr->tail)->pos, def->length);
        AccumulateVector(&thing->base.axis[0], &thing->base.pos, GETP(tr->tail)->vel, -def->length);
        PSVECScale(&thing->base.axis[0], GETP(tr->tail)->unk20, -(def->length - def->endLength) / 14.0f);
        PSVECScale(&thing->base.axis[0], GETP(tr->tail)->unk30, (def->length - def->endLength) / 14.0f);
        break;
    case 1:
        AccumulateVector(&thing->base.axis[1], &thing->base.pos, GETP(tr->tail)->pos, def->length);
        AccumulateVector(&thing->base.axis[1], &thing->base.pos, GETP(tr->tail)->vel, -def->length);
        PSVECScale(&thing->base.axis[1], GETP(tr->tail)->unk20, -(def->length - def->endLength) / 14.0f);
        PSVECScale(&thing->base.axis[1], GETP(tr->tail)->unk30, (def->length - def->endLength) / 14.0f);
        break;
    case 2:
        AccumulateVector(&thing->base.axis[2], &thing->base.pos, GETP(tr->tail)->pos, def->length);
        AccumulateVector(&thing->base.axis[2], &thing->base.pos, GETP(tr->tail)->vel, -def->length);
        PSVECScale(&thing->base.axis[2], GETP(tr->tail)->unk20, -(def->length - def->endLength) / 14.0f);
        PSVECScale(&thing->base.axis[2], GETP(tr->tail)->unk30, (def->length - def->endLength) / 14.0f);
        break;
    }
    GETP(tr->tail)->unk40 = tr->unk4C0;
    tr->unk4C0 += g_fDeltaTimeSec * def->scrollSpeed;
    colour.reset = 1;
    colour2.reset = 1;
    ExtractColorKey(def->headColour, &colour, tr->unk4C4);
    ExtractColorKey(def->tailColour, &colour2, tr->unk4C4);
    tr->unk4C4 += g_iDeltaTimeMS;
    if (tr->unk4D0 != 0xFFFF) {
        tr->unk4C8 += g_iDeltaTimeMS;
        tr->unk4D4 = tr->unk4C8 >> 4;
        if (tr->unk4D4 >= def->numFrames) {
            tr->unk4C8 = 0;
            tr->unk4D4 = 0;
        }
        tr->unk4D0 = GETP(def->frames)[tr->unk4D4];
    }
    GETP(tr->tail)->color[0] = colour.c[0];
    GETP(tr->tail)->color[1] = colour.c[1];
    GETP(tr->tail)->color[2] = colour.c[2];
    GETP(tr->tail)->color[3] = colour.c[3];
    tr->unk4B8[0] = -(colour.c[0] - colour2.c[0]) / 15;
    tr->unk4B8[1] = -(colour.c[1] - colour2.c[1]) / 15;
    tr->unk4B8[2] = -(colour.c[2] - colour2.c[2]) / 15;
    tr->unk4B8[3] = -(colour.c[3] - colour2.c[3]) / 15;
    SETP(tr->tail, GETP(GETP(tr->tail)->next));
    if (GETP(tr->tail) == GETP(tr->head)) {
        SETP(tr->head, GETP(GETP(tr->head)->next));
    }
    if (tr->unk4CC < 15) {
        tr->unk4CC++;
    }
    return 1;
}

void Tracer_Release(TracerThing *thing) {
    tracers[thing->index].active = 0;
    SETP(thing->def, NULL);
    Thing_Release(thing);
}

void Tracer_ClearTracer(s32 index) {
    Tracer *tracer = &tracers[index];
    u32 i;

    SETP(tracer->head, tracer->points);
    SETP(tracer->tail, GETP(tracer->head));
    tracer->unk4CC = 0;
    tracer->active = 0;
    tracer->unk4D0 = 0xFFFF;
    for (i = 0; i < 14; i++) {
        SETP(tracer->points[i].next, &tracer->points[i + 1]);
        tracer->points[i].pos[3] = 1.0f;
        tracer->points[i].vel[3] = 1.0f;
        tracer->points[i].unk40 = 1.0f;
    }
    SETP(tracers[index].unk4A8, tracer->points);
}

void Tracer_Render(TracerThing *thing) {
    typedef struct TriShaded {
        s32 color[3][4];
        f32 pos[3][4];
        f32 uv[4];
    } TriShaded;
    s32 i;
    TracerPoint *p;
    u32 idx;
    TriShaded *tri;
    s32 n;
    TracerDef *def = GETP(thing->def);

    idx = thing->index;
    if (tracers[idx].active != 0 && tracers[idx].unk4CC > 1) {
        p = GETP(tracers[idx].head);
        n = 0;
        tri = Gfx_Begin_GroupTriShaded(tracers[idx].unk4D0, tracers[idx].unk4D2, 0);
        for (i = 0; i < (tracers[idx].unk4CC == 15 ? tracers[idx].unk4CC - 2 : tracers[idx].unk4CC - 1); i++) {
            tri[0].color[0][0] = p->color[0];
            tri[0].color[0][1] = p->color[1];
            tri[0].color[0][2] = p->color[2];
            tri[0].color[0][3] = p->color[3];
            tri[0].pos[0][0] = p->pos[0];
            tri[0].pos[0][1] = p->pos[1];
            tri[0].pos[0][2] = p->pos[2];
            tri[0].pos[0][3] = 0.0f;
            tri[0].pos[1][3] = p->unk40;
            tri[0].color[1][0] = p->color[0];
            tri[0].color[1][1] = p->color[1];
            tri[0].color[1][2] = p->color[2];
            tri[0].color[1][3] = p->color[3];
            tri[0].pos[1][0] = p->vel[0];
            tri[0].pos[1][1] = p->vel[1];
            tri[0].pos[1][2] = p->vel[2];
            tri[0].uv[0] = def->uvScale;
            tri[0].uv[1] = p->unk40;
            tri[0].color[2][0] = GETP(p->next)->color[0];
            tri[0].color[2][1] = GETP(p->next)->color[1];
            tri[0].color[2][2] = GETP(p->next)->color[2];
            tri[0].color[2][3] = GETP(p->next)->color[3];
            tri[0].pos[2][0] = GETP(p->next)->vel[0];
            tri[0].pos[2][1] = GETP(p->next)->vel[1];
            tri[0].pos[2][2] = GETP(p->next)->vel[2];
            tri[0].uv[2] = def->uvScale;
            tri[0].uv[3] = GETP(p->next)->unk40;

            tri[1].color[0][0] = GETP(p->next)->color[0];
            tri[1].color[0][1] = GETP(p->next)->color[1];
            tri[1].color[0][2] = GETP(p->next)->color[2];
            tri[1].color[0][3] = GETP(p->next)->color[3];
            tri[1].pos[0][0] = GETP(p->next)->pos[0];
            tri[1].pos[0][1] = GETP(p->next)->pos[1];
            tri[1].pos[0][2] = GETP(p->next)->pos[2];
            tri[1].pos[0][3] = 0.0f;
            tri[1].pos[1][3] = GETP(p->next)->unk40;
            tri[1].color[2][0] = GETP(p->next)->color[0];
            tri[1].color[2][1] = GETP(p->next)->color[1];
            tri[1].color[2][2] = GETP(p->next)->color[2];
            tri[1].color[2][3] = GETP(p->next)->color[3];
            tri[1].pos[2][0] = GETP(p->next)->vel[0];
            tri[1].pos[2][1] = GETP(p->next)->vel[1];
            tri[1].pos[2][2] = GETP(p->next)->vel[2];
            tri[1].uv[2] = def->uvScale;
            tri[1].uv[3] = GETP(p->next)->unk40;
            tri[1].color[1][0] = p->color[0];
            tri[1].color[1][1] = p->color[1];
            tri[1].color[1][2] = p->color[2];
            tri[1].color[1][3] = p->color[3];
            tri[1].pos[1][0] = p->pos[0];
            tri[1].pos[1][1] = p->pos[1];
            tri[1].pos[1][2] = p->pos[2];
            tri[1].uv[0] = 0.0f;
            tri[1].uv[1] = p->unk40;
            tri += 2;
            n += 2;
            p = GETP(p->next);
        }
        Gfx_End_GroupTri(n);
    }
}

/* Whether the tracer's description lies in [start, end) (a data pack being
 * unloaded). */
s32 Tracer_Invalidate(TracerThing *thing, u32 start, u32 end) {
    u32 def = thing->def.h;

    if ((def >= start) && (def < end)) {
        return 1;
    }
    return 0;
}


/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_tracerVTable[17];

__attribute__((constructor)) static void PortInit_g_tracerVTable(void) {
    static void *const fns[] = {
        (void *)Tracer_Init,
        (void *)Tracer_Release,
        (void *)Thing_PreMove,
        (void *)Tracer_Move,
        (void *)Tracer_Render,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransform,
        (void *)Thing_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Thing_DynamicRotation,
        (void *)Thing_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Thing_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_tracerVTable[i] = HND(fns[i]);
    }
}
