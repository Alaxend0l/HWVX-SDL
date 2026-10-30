#include "common.h"

typedef struct Vector {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
} Vector;

typedef struct GfxPointLight {
    Vector position;
    f32 radius;
    GXColor color;
    u8 unk18[0x20 - 0x18];
} GfxPointLight;

typedef struct GfxState {
    GfxPointLight pointLights[3];
    Vector lightDir;
    GXColor lightColor;
    GXColor unk74;
    GXColor ambientColor;
    u32 unk7C;
    f32 unk80;
    s32 unk84;
    s32 unk88;
    u16 debugFont;
    s8 lightChanged;
    s8 unk8F;
    u8 numPointLights;
    u8 unk91;
    u8 unk92;
    u8 unk93;
    u8 enviroMapMode;
    u8 unk95[0x98 - 0x95];
    f32 farClippingPlane;
    u8 unk9C[0xA0 - 0x9C];
} GfxState;

typedef struct GDLDrawEntry {
    s32 geometry;
    GXColor unk4;
    s32 matrix;
    s32 lightState;
    u8 unk10;
    u8 unk11;
    u8 envmapState;
    u8 pad13;
} GDLDrawEntry;

typedef struct GDL {
    u8 unk0[0x54];
    s32 numDrawEntries;
    u8 unk58[0xDFCC0 - 0x58];
    GDLDrawEntry drawEntries[(0xE9CC0 - 0xDFCC0) / 0x14];
} GDL;

typedef struct GeometryGroup {
    u16 unk0;
    u16 flags;
    u8 unk4[0x10 - 0x4];
    PTR32(void) data;
    u16 unk14;
    u8 unk16[0x18 - 0x16];
    u16 unk18;
    u8 unk1A[0x20 - 0x1A];
} GeometryGroup;

typedef struct Geometry {
    u16 unk0;
    u16 numGroups;
    u16 unk4;
    u16 unk6;
    PTR32(GeometryGroup) groups;
    PTR32(f32) lodDistances;
    f32 bboxMin[4];
    f32 bboxMax[4];
    f32 center[4];
} Geometry;

extern GDL *g_pGDL;
extern GfxState g_state;
extern u8 g_CurrentViews;

void *GetRenderWindow();

s32 Gfx_FrustrumCullSphereTestAllWindows(Vector *center, f32 radius) {
    s32 Gfx_FrustrumCullSphereTest(Vector *center, f32 radius);
    u32 i;
    s32 ret;

    if (g_CurrentViews != 1) {
        for (i = 1; i < 5; i++) {
            if (g_CurrentViews & (1 << i)) {
                Gfx_SetRenderWindow(i);
                ret = Gfx_FrustrumCullSphereTest(center, radius);
                if (ret) {
                    return ret;
                }
            }
        }
        return ret;
    }
    return Gfx_FrustrumCullSphereTest(center, radius);
}

s32 Gfx_FrustrumCullSphereTest(Vector *center, f32 radius) {
    Vector normal __attribute__((aligned(16)));
    s32 result;
    u32 i;
    f32 dist;

    result = 2;
    for (i = 0; i < 5; i++) {
        normal.x = ((f32 *)GetRenderWindow())[i * 4 + 0x70];
        normal.y = ((f32 *)GetRenderWindow())[i * 4 + 0x71];
        normal.z = ((f32 *)GetRenderWindow())[i * 4 + 0x72];
        dist = PSVECDotProduct(center, &normal) + ((f32 *)GetRenderWindow())[i * 4 + 0x73];
        if (dist > radius) {
            return 0;
        }
        if (dist > -radius) {
            result = 1;
        }
    }
    return result;
}

u32 Gfx_GetCullLODGX(Geometry *geometry, Vector *matrix, u8 *lod, u8 flag) {
    Vector diff __attribute__((aligned(16)));
    Vector center __attribute__((aligned(16)));
    Vector pos __attribute__((aligned(16)));
    f32 dist;
    f32 radius;
    f32 a;
    f32 b;
    void *window;
    u16 i;

    window = GetRenderWindow();
    pos.x = geometry->center[0];
    pos.y = geometry->center[1];
    pos.z = geometry->center[2];
    pos.w = 1.0f;
    if (matrix == NULL) {
        center = pos;
        radius = geometry->bboxMax[3];
    } else {
        XformVec(matrix, &pos, &center);
        a = PSVECSquareMag(&matrix[0]) >= PSVECSquareMag(&matrix[1]) ? PSVECSquareMag(&matrix[0]) : PSVECSquareMag(&matrix[1]);
        b = a >= PSVECSquareMag(&matrix[2])
                ? (PSVECSquareMag(&matrix[0]) >= PSVECSquareMag(&matrix[1]) ? PSVECSquareMag(&matrix[0]) : PSVECSquareMag(&matrix[1]))
                : PSVECSquareMag(&matrix[2]);
        radius = geometry->bboxMax[3] * Sqrt(b);
    }
    PSVECSubtract(&center, (Vector *)((u8 *)window + 0x180), &diff);
    dist = PSVECDotProduct(&diff, (Vector *)((u8 *)window + 0x190));
    dist *= ((f32 *)window)[0x9D];
    if (!(GETP(((Geometry *)GETP(geometry->groups))->groups)->flags & 0x8000) && g_state.farClippingPlane > 0.0f &&
        dist >= g_state.farClippingPlane) {
        return 0;
    }
    switch (Gfx_FrustrumCullSphereTest(&center, radius)) {
    case 1:
        *lod = 1;
        break;
    case 2:
        *lod = 0;
        break;
    default:
        return 0;
    }
    for (i = 0; i < geometry->numGroups; i++) {
        if (dist < GETP(geometry->lodDistances)[i]) {
            break;
        }
    }
    if (geometry->numGroups == 1 && GETP(geometry->lodDistances)[0] == 0.0f) {
        i = 0;
    }
    if (i < geometry->numGroups) {
        if (g_CurrentViews != 1 && i + 1 < geometry->numGroups) {
            return (u32)HND(GETP(geometry->groups)) + (i + 1) * 0x18;
        }
        return (u32)HND(GETP(geometry->groups)) + i * 0x18;
    }
    Gfx_EnableState(0x20000);
    return 0;
}

void Gfx_DrawGeometry(s32 geometry, s32 matrix) {
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].geometry = geometry;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].matrix = Gfx_AddWorldMatrixState_CacheMatrix(PTR(matrix));
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk10 = (g_state.unk7C >> 4) & 1;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk4 = g_state.unk74;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].lightState = Gfx_GetLightState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].envmapState = Gfx_GetEnvmapState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk11 = 0;
    g_pGDL->numDrawEntries++;
}

void Gfx_DrawGeometry_MatrixNotCached(s32 geometry, s32 matrix) {
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].geometry = geometry;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].matrix = Gfx_AddWorldMatrixState_NonCached(PTR(matrix));
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk10 = (g_state.unk7C >> 4) & 1;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk4 = g_state.unk74;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].lightState = Gfx_GetLightState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].envmapState = Gfx_GetEnvmapState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk11 = 0;
    g_pGDL->numDrawEntries++;
}

void Gfx_DrawGeometry_QuaternionScalePos(s32 geometry, s32 quat, s32 scale, s32 pos) {
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].geometry = geometry;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].matrix = Gfx_AddWorldMatrixState_CacheMatrixQuaternionScalePos(PTR(quat), PTR(scale), PTR(pos));
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk10 = (g_state.unk7C >> 4) & 1;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk4 = g_state.unk74;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].lightState = Gfx_GetLightState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].envmapState = Gfx_GetEnvmapState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk11 = 0;
    g_pGDL->numDrawEntries++;
}

s32 Gfx_CalcGeometryLOD(Geometry *geometry, f32 *matrix) {
    Vector min __attribute__((aligned(16)));
    Vector max __attribute__((aligned(16)));
    Vector center __attribute__((aligned(16)));
    f32 dist;
    u16 i;

    if (geometry->numGroups == 1) {
        return 0;
    }
    PSVECAdd((Vector *)&matrix[12], (Vector *)geometry->bboxMin, &min);
    PSVECAdd((Vector *)&matrix[12], (Vector *)geometry->bboxMax, &max);
    PSVECAdd(&min, &max, &center);
    PSVECScale(&center, &center, 0.5f);
    PSVECSubtract(&center, (Vector *)((u8 *)GetRenderWindow() + 0x180), &min);
    dist = PSVECDotProduct(&min, (Vector *)((u8 *)GetRenderWindow() + 0x190));
    dist *= ((f32 *)GetRenderWindow())[0x9D];
    for (i = 0; i < geometry->numGroups; i++) {
        if (dist < GETP(geometry->lodDistances)[i]) {
            break;
        }
    }
    if (i < geometry->numGroups) {
        if (g_CurrentViews != 1 && i + 1 < geometry->numGroups) {
            return i + 1;
        }
        return i;
    }
    return -1;
}

