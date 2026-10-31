#include "common.h"
#include "Gfx.h"




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



extern GDL *g_pGDL;
extern u8 g_CurrentViews;


s32 Gfx_FrustrumCullSphereTestAllWindows(Vector4 *center, f32 radius) {
    s32 Gfx_FrustrumCullSphereTest(Vector4 *center, f32 radius);
    u32 i;
    s32 ret = 0;  /* port: the original returns a stale r4 if no window is tested */

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

s32 Gfx_FrustrumCullSphereTest(Vector4 *center, f32 radius) {
    Vector4 normal __attribute__((aligned(16)));
    s32 result;
    u32 i;
    f32 dist;

    result = 2;
    for (i = 0; i < 5; i++) {
        normal.x = GetRenderWindow()->clipPlanes[i].x;
        normal.y = GetRenderWindow()->clipPlanes[i].y;
        normal.z = GetRenderWindow()->clipPlanes[i].z;
        dist = PSVECDotProduct(center, &normal) + GetRenderWindow()->clipPlanes[i].w;
        if (dist > radius) {
            return 0;
        }
        if (dist > -radius) {
            result = 1;
        }
    }
    return result;
}

u32 Gfx_GetCullLODGX(Geometry *geometry, Vector4 *matrix, u8 *lod, u8 flag) {
    Vector4 diff __attribute__((aligned(16)));
    Vector4 center __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    f32 dist;
    f32 radius;
    f32 a;
    f32 b;
    GfxWindow *window;
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
    PSVECSubtract(&center, (Vector4 *)&window->cameraPos, &diff);
    dist = PSVECDotProduct(&diff, (Vector4 *)&window->cameraRight);
    dist *= window->sinHalfFov;
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
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk10 = (g_state.flags >> 4) & 1;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk4 = g_state.baseColor;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].lightState = Gfx_GetLightState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].envmapState = Gfx_GetEnvmapState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk11 = 0;
    g_pGDL->numDrawEntries++;
}

void Gfx_DrawGeometry_MatrixNotCached(s32 geometry, s32 matrix) {
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].geometry = geometry;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].matrix = Gfx_AddWorldMatrixState_NonCached(PTR(matrix));
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk10 = (g_state.flags >> 4) & 1;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk4 = g_state.baseColor;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].lightState = Gfx_GetLightState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].envmapState = Gfx_GetEnvmapState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk11 = 0;
    g_pGDL->numDrawEntries++;
}

void Gfx_DrawGeometry_QuaternionScalePos(s32 geometry, s32 quat, s32 scale, s32 pos) {
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].geometry = geometry;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].matrix = Gfx_AddWorldMatrixState_CacheMatrixQuaternionScalePos(PTR(quat), PTR(scale), PTR(pos));
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk10 = (g_state.flags >> 4) & 1;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk4 = g_state.baseColor;
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].lightState = Gfx_GetLightState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].envmapState = Gfx_GetEnvmapState();
    g_pGDL->drawEntries[g_pGDL->numDrawEntries].unk11 = 0;
    g_pGDL->numDrawEntries++;
}

s32 Gfx_CalcGeometryLOD(Geometry *geometry, f32 *matrix) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 center __attribute__((aligned(16)));
    f32 dist;
    u16 i;

    if (geometry->numGroups == 1) {
        return 0;
    }
    PSVECAdd((Vector4 *)&matrix[12], (Vector4 *)geometry->bboxMin, &min);
    PSVECAdd((Vector4 *)&matrix[12], (Vector4 *)geometry->bboxMax, &max);
    PSVECAdd(&min, &max, &center);
    PSVECScale(&center, &center, 0.5f);
    PSVECSubtract(&center, (Vector4 *)&GetRenderWindow()->cameraPos, &min);
    dist = PSVECDotProduct(&min, (Vector4 *)&GetRenderWindow()->cameraRight);
    dist *= GetRenderWindow()->sinHalfFov;
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

