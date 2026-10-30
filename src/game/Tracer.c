#include "common.h"

struct Thing {
    u8 pad0[0xB0];
    s32 tracer;
    s32 tracerIndex;
};

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

s32 Tracer_Init(u8 *thing, u8 *data) {
    u8 *params;

    Thing_Init(thing, 0);
    SLOT_STORE((thing + 0xB0), SLOT_LOAD(u8 *, (data + 0x30)));
    *(Vector4 *)(thing + 0x40) = *(Vector4 *)data;
    *(u32 *)(thing + 0xB4) = Tracer_FindFree();
    Tracer_ClearTracer(*(u32 *)(thing + 0xB4));
    tracers[*(u32 *)(thing + 0xB4)].active = 1;
    params = SLOT_LOAD(u8 *, (thing + 0xB0));
    if (*(u32 *)(params + 0x54) != 0) {
        tracers[*(u32 *)(thing + 0xB4)].unk4D0 = *SLOT_LOAD(u32 *, (params + 0x58));
    } else {
        tracers[*(u32 *)(thing + 0xB4)].unk4D0 = 0xFFFF;
    }
    tracers[*(u32 *)(thing + 0xB4)].unk4D2 = *(u8 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x20);
    tracers[*(u32 *)(thing + 0xB4)].unk4BC = -1.0f * *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x40) / 14.0f;
    tracers[*(u32 *)(thing + 0xB4)].unk4C4 = 0;
    tracers[*(u32 *)(thing + 0xB4)].unk4C8 = 0;
    tracers[*(u32 *)(thing + 0xB4)].unk4C0 = 0.0f;
    return 1;
}

s32 Tracer_Move(u8 *thing) {
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

    p = GETP(tracers[*(u32 *)(thing + 0xB4)].head);
    for (i = 0; i < tracers[*(u32 *)(thing + 0xB4)].unk4CC; i++) {
        s32 c;
        p->pos[0] += p->unk20[0];
        p->pos[1] += p->unk20[1];
        p->pos[2] += p->unk20[2];
        p->vel[0] += p->unk30[0];
        p->vel[1] += p->unk30[1];
        p->vel[2] += p->unk30[2];
        p->unk40 += tracers[*(u32 *)(thing + 0xB4)].unk4BC;
        c = p->color[0] + tracers[*(u32 *)(thing + 0xB4)].unk4B8[0];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[0] = c;
        c = p->color[1] + tracers[*(u32 *)(thing + 0xB4)].unk4B8[1];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[1] = c;
        c = p->color[2] + tracers[*(u32 *)(thing + 0xB4)].unk4B8[2];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[2] = c;
        c = p->color[3] + tracers[*(u32 *)(thing + 0xB4)].unk4B8[3];
        if (c < 0) {
            c = 0;
        } else if (c > 255) {
            c = 255;
        }
        p->color[3] = c;
        p = GETP(p->next);
    }
    switch (*(u8 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x21)) {
    case 0:
        AccumulateVector(thing + 0x10, thing + 0x40, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->pos, *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30));
        AccumulateVector(thing + 0x10, thing + 0x40, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->vel, -*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30));
        PSVECScale(thing + 0x10, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk20, -(*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30) - *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x34)) / 14.0f);
        PSVECScale(thing + 0x10, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk30, (*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30) - *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x34)) / 14.0f);
        break;
    case 1:
        AccumulateVector(thing + 0x20, thing + 0x40, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->pos, *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30));
        AccumulateVector(thing + 0x20, thing + 0x40, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->vel, -*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30));
        PSVECScale(thing + 0x20, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk20, -(*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30) - *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x34)) / 14.0f);
        PSVECScale(thing + 0x20, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk30, (*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30) - *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x34)) / 14.0f);
        break;
    case 2:
        AccumulateVector(thing + 0x30, thing + 0x40, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->pos, *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30));
        AccumulateVector(thing + 0x30, thing + 0x40, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->vel, -*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30));
        PSVECScale(thing + 0x30, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk20, -(*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30) - *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x34)) / 14.0f);
        PSVECScale(thing + 0x30, GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk30, (*(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x30) - *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x34)) / 14.0f);
        break;
    }
    GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->unk40 = tracers[*(u32 *)(thing + 0xB4)].unk4C0;
    tracers[*(u32 *)(thing + 0xB4)].unk4C0 += g_fDeltaTimeSec * *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x38);
    colour.reset = 1;
    colour2.reset = 1;
    ExtractColorKey(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x44, &colour, tracers[*(u32 *)(thing + 0xB4)].unk4C4);
    ExtractColorKey(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x4C, &colour2, tracers[*(u32 *)(thing + 0xB4)].unk4C4);
    tracers[*(u32 *)(thing + 0xB4)].unk4C4 += g_iDeltaTimeMS;
    if (tracers[*(u32 *)(thing + 0xB4)].unk4D0 != 0xFFFF) {
        tracers[*(u32 *)(thing + 0xB4)].unk4C8 += g_iDeltaTimeMS;
        tracers[*(u32 *)(thing + 0xB4)].unk4D4 = tracers[*(u32 *)(thing + 0xB4)].unk4C8 >> 4;
        if (tracers[*(u32 *)(thing + 0xB4)].unk4D4 >= *(u32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x54)) {
            tracers[*(u32 *)(thing + 0xB4)].unk4C8 = 0;
            tracers[*(u32 *)(thing + 0xB4)].unk4D4 = 0;
        }
        tracers[*(u32 *)(thing + 0xB4)].unk4D0 = (SLOT_LOAD(u32 *, (SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x58)))[tracers[*(u32 *)(thing + 0xB4)].unk4D4];
    }
    GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->color[0] = colour.c[0];
    GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->color[1] = colour.c[1];
    GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->color[2] = colour.c[2];
    GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->color[3] = colour.c[3];
    tracers[*(u32 *)(thing + 0xB4)].unk4B8[0] = -(colour.c[0] - colour2.c[0]) / 15;
    tracers[*(u32 *)(thing + 0xB4)].unk4B8[1] = -(colour.c[1] - colour2.c[1]) / 15;
    tracers[*(u32 *)(thing + 0xB4)].unk4B8[2] = -(colour.c[2] - colour2.c[2]) / 15;
    tracers[*(u32 *)(thing + 0xB4)].unk4B8[3] = -(colour.c[3] - colour2.c[3]) / 15;
    SETP(tracers[*(u32 *)(thing + 0xB4)].tail, GETP(GETP(tracers[*(u32 *)(thing + 0xB4)].tail)->next));
    if (GETP(tracers[*(u32 *)(thing + 0xB4)].tail) == GETP(tracers[*(u32 *)(thing + 0xB4)].head)) {
        SETP(tracers[*(u32 *)(thing + 0xB4)].head, GETP(GETP(tracers[*(u32 *)(thing + 0xB4)].head)->next));
    }
    if (tracers[*(u32 *)(thing + 0xB4)].unk4CC < 15) {
        tracers[*(u32 *)(thing + 0xB4)].unk4CC++;
    }
    return 1;
}

void Tracer_Release(struct Thing *thing) {

    tracers[thing->tracerIndex].active = 0;
    thing->tracer = 0;
    Thing_Release(HND(thing));
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

void Tracer_Render(u8 *thing) {
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

    idx = *(u32 *)(thing + 0xB4);
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
            tri[0].uv[0] = *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x3C);
            tri[0].uv[1] = p->unk40;
            tri[0].color[2][0] = GETP(p->next)->color[0];
            tri[0].color[2][1] = GETP(p->next)->color[1];
            tri[0].color[2][2] = GETP(p->next)->color[2];
            tri[0].color[2][3] = GETP(p->next)->color[3];
            tri[0].pos[2][0] = GETP(p->next)->vel[0];
            tri[0].pos[2][1] = GETP(p->next)->vel[1];
            tri[0].pos[2][2] = GETP(p->next)->vel[2];
            tri[0].uv[2] = *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x3C);
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
            tri[1].uv[2] = *(f32 *)(SLOT_LOAD(u8 *, (thing + 0xB0)) + 0x3C);
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

s32 Tracer_Invalidate(void *arg0, u32 arg1, u32 arg2) {
    u32 temp_r0;

    temp_r0 = M2C_FIELD(arg0, u32 *, 0xB0);
    if ((temp_r0 >= arg1) && (temp_r0 < arg2)) {
        return 1;
    }
    return 0;
}


void *g_tracerVTable[] = {
    Tracer_Init,
    Tracer_Release,
    Thing_PreMove,
    Tracer_Move,
    Tracer_Render,
    Thing_GetMass,
    Thing_DynamicCollideImpact,
    Thing_GetCameraOrientationInfo,
    Thing_GetInterfaceInfo,
    Thing_SlaveTransform,
    Thing_GetSurfaceScript,
    Thing_GetNearestNavPoint,
    Thing_GetInverseLocalInertiaTensor,
    Thing_DynamicRotation,
    Thing_CollideWithRay,
    Thing_GetTargetPos,
    Thing_GetOrientedCollision,
};
