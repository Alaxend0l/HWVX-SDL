#include "common.h"
#include "ThingManager.h"

/* A tracer's description in the data pack. */
typedef struct TracerDef {
    u8 pad00[0x20];             /* 0x00 */
    u8 shadeArg;                /* 0x20 to Gfx_Begin_GroupTriShaded arg1 */
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
    u8 pad460[0x48];
    PTR32(TracerPoint) firstPoint;
    u8 pad4AC[4];
    PTR32(TracerPoint) head;
    PTR32(TracerPoint) tail;
    s8 colorDelta[0x4];
    f32 texFade;
    f32 texScroll;
    u32 colorTime;
    u32 frameTime;
    u16 numPoints;
    u16 active;
    u16 texFrame;
    u16 shadeArg;
    u16 frameIndex;
    u8 pad4D6[2];
    f32 tickTime;               /* port: time since the ribbon last took a point */
    PTR32(TracerPoint) newest;  /* port: the point it took last */
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

s32 Tracer_Init(TracerThing *thing, TracerParams *data) {
    TracerDef *def;
    Tracer *tr;

    Thing_Init(thing, 0);
    SETP(thing->def, GETP(data->def));
    thing->base.pos = data->pos;
    thing->index = Tracer_FindFree();
    Tracer_ClearTracer(thing->index);
    tr = &tracers[thing->index];
    tr->active = 1;
    def = GETP(thing->def);
    if (def->numFrames != 0) {
        tr->texFrame = *GETP(def->frames);
    } else {
        tr->texFrame = 0xFFFF;
    }
    tr->shadeArg = def->shadeArg;
    tr->texFade = -1.0f * def->fade / 14.0f;
    tr->colorTime = 0;
    tr->frameTime = 0;
    tr->texScroll = 0.0f;
    return 1;
}

/* Port fix: the ribbon takes one point per step and its length, taper and fade are
 * counted in points, so stepping every frame made tracers shorter as the frame rate
 * rose (1/4 the length at 240 FPS). Above 60 FPS step at about 60 Hz, and in between
 * just keep the newest point on the thing. Just under 1/60 s, so that a 60 FPS frame
 * always takes a step. */
#define TRACER_TICK 0.016f

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
    TracerPoint *pt;
    s8 tick;

    tr->tickTime += g_fDeltaTimeSec;
    tick = tr->tickTime >= TRACER_TICK || GETP(tr->newest) == NULL;
    if (tick) {
        tr->tickTime = 0.0f;
    }
    /* the point to write: a new one on a step, else the newest again */
    pt = tick ? GETP(tr->tail) : GETP(tr->newest);
    p = GETP(tr->head);
    for (i = 0; tick && i < tr->numPoints; i++) {
        s32 c;
        p->pos[0] += p->posDelta[0];
        p->pos[1] += p->posDelta[1];
        p->pos[2] += p->posDelta[2];
        p->vel[0] += p->velDelta[0];
        p->vel[1] += p->velDelta[1];
        p->vel[2] += p->velDelta[2];
        p->texCoord += tr->texFade;
        c = p->color[0] + tr->colorDelta[0];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[0] = c;
        c = p->color[1] + tr->colorDelta[1];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[1] = c;
        c = p->color[2] + tr->colorDelta[2];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[2] = c;
        c = p->color[3] + tr->colorDelta[3];
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
        AccumulateVector(&thing->base.axis[0], &thing->base.pos, pt->pos, def->length);
        AccumulateVector(&thing->base.axis[0], &thing->base.pos, pt->vel, -def->length);
        PSVECScale(&thing->base.axis[0], pt->posDelta, -(def->length - def->endLength) / 14.0f);
        PSVECScale(&thing->base.axis[0], pt->velDelta, (def->length - def->endLength) / 14.0f);
        break;
    case 1:
        AccumulateVector(&thing->base.axis[1], &thing->base.pos, pt->pos, def->length);
        AccumulateVector(&thing->base.axis[1], &thing->base.pos, pt->vel, -def->length);
        PSVECScale(&thing->base.axis[1], pt->posDelta, -(def->length - def->endLength) / 14.0f);
        PSVECScale(&thing->base.axis[1], pt->velDelta, (def->length - def->endLength) / 14.0f);
        break;
    case 2:
        AccumulateVector(&thing->base.axis[2], &thing->base.pos, pt->pos, def->length);
        AccumulateVector(&thing->base.axis[2], &thing->base.pos, pt->vel, -def->length);
        PSVECScale(&thing->base.axis[2], pt->posDelta, -(def->length - def->endLength) / 14.0f);
        PSVECScale(&thing->base.axis[2], pt->velDelta, (def->length - def->endLength) / 14.0f);
        break;
    }
    pt->texCoord = tr->texScroll;
    tr->texScroll += g_fDeltaTimeSec * def->scrollSpeed;
    colour.reset = 1;
    colour2.reset = 1;
    ExtractColorKey(def->headColour, &colour, tr->colorTime);
    ExtractColorKey(def->tailColour, &colour2, tr->colorTime);
    tr->colorTime += g_iDeltaTimeMS;
    if (tr->texFrame != 0xFFFF) {
        tr->frameTime += g_iDeltaTimeMS;
        tr->frameIndex = tr->frameTime >> 4;
        if (tr->frameIndex >= def->numFrames) {
            tr->frameTime = 0;
            tr->frameIndex = 0;
        }
        tr->texFrame = GETP(def->frames)[tr->frameIndex];
    }
    pt->color[0] = colour.c[0];
    pt->color[1] = colour.c[1];
    pt->color[2] = colour.c[2];
    pt->color[3] = colour.c[3];
    tr->colorDelta[0] = -(colour.c[0] - colour2.c[0]) / 15;
    tr->colorDelta[1] = -(colour.c[1] - colour2.c[1]) / 15;
    tr->colorDelta[2] = -(colour.c[2] - colour2.c[2]) / 15;
    tr->colorDelta[3] = -(colour.c[3] - colour2.c[3]) / 15;
    if (tick) {
        SETP(tr->newest, pt);
        SETP(tr->tail, GETP(GETP(tr->tail)->next));
        if (GETP(tr->tail) == GETP(tr->head)) {
            SETP(tr->head, GETP(GETP(tr->head)->next));
        }
        if (tr->numPoints < 15) {
            tr->numPoints++;
        }
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
    tracer->numPoints = 0;
    tracer->active = 0;
    tracer->texFrame = 0xFFFF;
    tracer->tickTime = TRACER_TICK;
    SETP(tracer->newest, NULL);
    for (i = 0; i < 14; i++) {
        SETP(tracer->points[i].next, &tracer->points[i + 1]);
        tracer->points[i].pos[3] = 1.0f;
        tracer->points[i].vel[3] = 1.0f;
        tracer->points[i].texCoord = 1.0f;
    }
    SETP(tracers[index].firstPoint, tracer->points);
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
    if (tracers[idx].active != 0 && tracers[idx].numPoints > 1) {
        p = GETP(tracers[idx].head);
        n = 0;
        tri = Gfx_Begin_GroupTriShaded(tracers[idx].texFrame, tracers[idx].shadeArg, 0);
        for (i = 0; i < (tracers[idx].numPoints == 15 ? tracers[idx].numPoints - 2 : tracers[idx].numPoints - 1); i++) {
            tri[0].color[0][0] = p->color[0];
            tri[0].color[0][1] = p->color[1];
            tri[0].color[0][2] = p->color[2];
            tri[0].color[0][3] = p->color[3];
            tri[0].pos[0][0] = p->pos[0];
            tri[0].pos[0][1] = p->pos[1];
            tri[0].pos[0][2] = p->pos[2];
            tri[0].pos[0][3] = 0.0f;
            tri[0].pos[1][3] = p->texCoord;
            tri[0].color[1][0] = p->color[0];
            tri[0].color[1][1] = p->color[1];
            tri[0].color[1][2] = p->color[2];
            tri[0].color[1][3] = p->color[3];
            tri[0].pos[1][0] = p->vel[0];
            tri[0].pos[1][1] = p->vel[1];
            tri[0].pos[1][2] = p->vel[2];
            tri[0].uv[0] = def->uvScale;
            tri[0].uv[1] = p->texCoord;
            tri[0].color[2][0] = GETP(p->next)->color[0];
            tri[0].color[2][1] = GETP(p->next)->color[1];
            tri[0].color[2][2] = GETP(p->next)->color[2];
            tri[0].color[2][3] = GETP(p->next)->color[3];
            tri[0].pos[2][0] = GETP(p->next)->vel[0];
            tri[0].pos[2][1] = GETP(p->next)->vel[1];
            tri[0].pos[2][2] = GETP(p->next)->vel[2];
            tri[0].uv[2] = def->uvScale;
            tri[0].uv[3] = GETP(p->next)->texCoord;

            tri[1].color[0][0] = GETP(p->next)->color[0];
            tri[1].color[0][1] = GETP(p->next)->color[1];
            tri[1].color[0][2] = GETP(p->next)->color[2];
            tri[1].color[0][3] = GETP(p->next)->color[3];
            tri[1].pos[0][0] = GETP(p->next)->pos[0];
            tri[1].pos[0][1] = GETP(p->next)->pos[1];
            tri[1].pos[0][2] = GETP(p->next)->pos[2];
            tri[1].pos[0][3] = 0.0f;
            tri[1].pos[1][3] = GETP(p->next)->texCoord;
            tri[1].color[2][0] = GETP(p->next)->color[0];
            tri[1].color[2][1] = GETP(p->next)->color[1];
            tri[1].color[2][2] = GETP(p->next)->color[2];
            tri[1].color[2][3] = GETP(p->next)->color[3];
            tri[1].pos[2][0] = GETP(p->next)->vel[0];
            tri[1].pos[2][1] = GETP(p->next)->vel[1];
            tri[1].pos[2][2] = GETP(p->next)->vel[2];
            tri[1].uv[2] = def->uvScale;
            tri[1].uv[3] = GETP(p->next)->texCoord;
            tri[1].color[1][0] = p->color[0];
            tri[1].color[1][1] = p->color[1];
            tri[1].color[1][2] = p->color[2];
            tri[1].color[1][3] = p->color[3];
            tri[1].pos[1][0] = p->pos[0];
            tri[1].pos[1][1] = p->pos[1];
            tri[1].pos[1][2] = p->pos[2];
            tri[1].uv[0] = 0.0f;
            tri[1].uv[1] = p->texCoord;
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
