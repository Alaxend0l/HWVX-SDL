#include "common.h"


typedef struct LineDef {
    u8 blendMode;
    u8 followVelocity;
    u8 normalizeDir;
    u8 pad3;
    f32 base;
    f32 range;
    u8 keys[8];
    u8 colourKeys[4];
} LineDef;

typedef struct LineInfo {
    u8 pad0[4];
    PTR32(LineDef) data;
    s32 life;
    s32 lifeRange;
    u8 pad10[4];
    u8 colourKeys[8];
    u32 numFx;
    PTR32(u32) fx;
} LineInfo;

typedef struct Line {
    Vector4 start;          /* 0x00 */
    Vector4 end;            /* 0x10 */
    Vector4 vel;            /* 0x20 */
    Vector4 dir;            /* 0x30 */
    Vector4 accel;          /* 0x40 */
    PTR32(LineInfo) info;         /* 0x50 NULL: free slot */
    u8 colourKey[0x24];     /* 0x54 ExtractColorKey state */
    u32 colour;             /* 0x78 */
    u8 pad7C[4];            /* 0x7C */
    u8 unused80;            /* 0x80 */
    u8 pad81[3];            /* 0x81 */
    u8 colour2Key[0x24];    /* 0x84 */
    u32 colour2;            /* 0xA8 */
    u8 padAC[4];            /* 0xAC */
    u8 unusedB0;            /* 0xB0 */
    u8 padB1[3];            /* 0xB1 */
    f32 length;             /* 0xB4 ExtractF32Key output */
    u8 padB8[0xC];          /* 0xB8 */
    u8 unusedC4;            /* 0xC4 */
    u8 padC5[0xF];          /* 0xC5 */
    f32 life;               /* 0xD4 ms left */
    f32 scale;              /* 0xD8 */
    f32 rate;               /* 0xDC */
    u8 blendMode;           /* 0xE0 GX blend mode for the FX line */
    u8 lengthKeyed;         /* 0xE1 length is keyed over time */
    u8 recomputeEnd;        /* 0xE2 end is recomputed every frame */
    u8 padE3[0xD];          /* 0xE3 */
} Line;

Line g_lineManager[128];

void LineManager_Init(void) {
    extern Line g_lineManager[128];
    u32 i;

    for (i = 0; i < 128; i++) {
        SETP(g_lineManager[i].info, NULL);
    }
}

void LineManager_Add(void *arg, Vector4 *start, Vector4 *dir, Vector4 *accel) {
    extern Line g_lineManager[128];
    LineInfo *info = arg;
    u32 i;
    LineDef *data;

    for (i = 0; i < 128; i++) {
        if (GETP(g_lineManager[i].info) == NULL) {
            data = GETP(info->data);
            g_lineManager[i].life = RandomAdd_s32(info->life, info->lifeRange);
            g_lineManager[i].rate = 1.0f;
            if (info->life && info->lifeRange) {
                g_lineManager[i].rate = 1.0f / (g_lineManager[i].life / info->life);
            }
            g_lineManager[i].start = *start;
            g_lineManager[i].vel = *dir;
            g_lineManager[i].dir = *dir;
            if (PSVECSquareMag(&g_lineManager[i].dir)) {
                PSVECNormalize(&g_lineManager[i].dir, &g_lineManager[i].dir);
            }
            g_lineManager[i].accel = *accel;
            SETP(g_lineManager[i].info, info);
            g_lineManager[i].blendMode = data->blendMode;
            g_lineManager[i].unused80 = 1;
            g_lineManager[i].unusedB0 = 1;
            g_lineManager[i].unusedC4 = 1;
            g_lineManager[i].recomputeEnd = 0;
            g_lineManager[i].lengthKeyed = 0;
            g_lineManager[i].scale = RandomAdd(data->base, data->range);
            if (*(u32 *)data->keys > 1) {
                g_lineManager[i].lengthKeyed = 1;
                g_lineManager[i].recomputeEnd = 1;
            } else {
                ExtractF32Key(data->keys, &g_lineManager[i].length, 0.0f);
                g_lineManager[i].length *= g_lineManager[i].scale;
                PSVECScale(&g_lineManager[i].dir, &g_lineManager[i].end, g_lineManager[i].length);
                PSVECAdd(&g_lineManager[i].start, &g_lineManager[i].end, &g_lineManager[i].end);
            }
            if (data->normalizeDir != 0 || data->followVelocity != 0) {
                g_lineManager[i].recomputeEnd = 1;
            }
            return;
        }
    }
}

void LineManager_Heartbeat(void) {
    extern Line g_lineManager[128];
    extern s8 g_bFreezeWorld;
    extern f32 g_fDeltaTimeMS;
    extern Vector4 g_zeroVector;
    DebrisParams params __attribute__((aligned(16)));
    Vector4 delta __attribute__((aligned(16)));
    u32 i;
    f32 dt;
    LineDef *data;

    if (g_bFreezeWorld) {
        return;
    }
    for (i = 0; i < 128; i++) {
        if (GETP(g_lineManager[i].info) == NULL) {
            continue;
        }
        dt = g_fDeltaTimeMS;
        data = GETP(GETP(g_lineManager[i].info)->data);
        if (g_lineManager[i].life < dt) {
            if (GETP(g_lineManager[i].info)->numFx != 0) {
                s32 idx = RandomAdd_s32(0, GETP(g_lineManager[i].info)->numFx);
                data = (LineDef *)PTR(GETP(GETP(g_lineManager[i].info)->fx)[idx]);
                if (data != NULL) {
                    params.debris = (u32)HND(data);
                    params.pos = g_lineManager[i].start;
                    params.vel = g_lineManager[i].vel;
                    params.quat.w = 1.0f;
                    params.quat.x = 0.0f;
                    params.quat.y = 0.0f;
                    params.quat.z = 0.0f;
                    params.owner = 0;
                    params.speedScale = 0.0f;
                    params.unusedParam = 0.0f;
                    params.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &params);
                }
            }
            SETP(g_lineManager[i].info, NULL);
        } else {
            g_lineManager[i].life -= dt;
            PSVECScale(&g_lineManager[i].vel, &delta, dt);
            AccumulateVector(&g_lineManager[i].accel, &g_lineManager[i].vel, &g_lineManager[i].vel, dt);
            PSVECAdd(&delta, &g_lineManager[i].start, &g_lineManager[i].start);
            ExtractColorKey(GETP(g_lineManager[i].info)->colourKeys, g_lineManager[i].colourKey, dt * g_lineManager[i].rate);
            ExtractColorKey(data->colourKeys, g_lineManager[i].colour2Key, dt * g_lineManager[i].rate);
            if (g_lineManager[i].recomputeEnd) {
                if (data->followVelocity) {
                    g_lineManager[i].dir = g_lineManager[i].vel;
                    if (data->normalizeDir && PSVECSquareMag(&g_lineManager[i].dir)) {
                        PSVECNormalize(&g_lineManager[i].dir, &g_lineManager[i].dir);
                    }
                }
                if (g_lineManager[i].lengthKeyed) {
                    ExtractF32Key(data->keys, &g_lineManager[i].length, dt * g_lineManager[i].rate);
                    g_lineManager[i].length *= g_lineManager[i].scale;
                }
                PSVECScale(&g_lineManager[i].dir, &g_lineManager[i].end, g_lineManager[i].length);
                PSVECAdd(&g_lineManager[i].start, &g_lineManager[i].end, &g_lineManager[i].end);
            }
            PSVECAdd(&delta, &g_lineManager[i].end, &g_lineManager[i].end);
        }
    }
}

void LineManager_Render(void) {
    typedef struct GDLLine {
        Vector4 p0;             /* 0x00 */
        Vector4 p1;             /* 0x10 */
        u32 c0;                 /* 0x20 colour of p0, RGBA8888 */
        u32 c1;                 /* 0x24 colour of p1, RGBA8888 */
        u8 blendMode;           /* 0x28 GX blend mode */
    } GDLLine;
    extern Line g_lineManager[128];
    GDLLine *Gfx_Add3DLineFX(void);
    u32 i;
    GDLLine *fx;

    for (i = 0; i < 128; i++) {
        if (GETP(g_lineManager[i].info) != NULL) {
            fx = Gfx_Add3DLineFX();
            fx->p0 = g_lineManager[i].start;
            fx->p1 = g_lineManager[i].end;
            fx->c0 = g_lineManager[i].colour2;
            fx->c1 = g_lineManager[i].colour;
            fx->blendMode = g_lineManager[i].blendMode;
        }
    }
}
