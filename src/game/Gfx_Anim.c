

#include "common.h"

typedef struct GeometryGroup {
    u16 unk0;
    u16 flags;
    u8 unk4[0x10 - 0x4];
    void *data;
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
    GeometryGroup *groups;
    f32 *lodDistances;
    f32 bboxMin[4];
    f32 bboxMax[4];
    f32 center[4];
} Geometry;

typedef struct DLSkinGroup {
    Geometry *lod;
    s32 lightState;
    f32 *matrices;
    u32 count;
    u16 matrix;
    u8 group;
    u8 window;
} DLSkinGroup;

void AnimTraverseHeirarchyToMatrices(void);
void *GetRenderWindow(void);

void Gfx_AnimExtractKeyFrame(void)
{
    AnimExtractKeyFrame();
}

void Gfx_AnimTraverseHeirarchyToMatrices(void)
{
    AnimTraverseHeirarchyToMatrices();
}

void Gfx_DrawAnimGeometryAux_(Geometry *geometry, f32 *matrices, u32 count, f32 *matrix);


void Gfx_DrawAnimGeometryAux(Geometry *geometry, f32 *matrices, u32 count, f32 *matrix) {
    extern f32 g_identity[];
    extern u8 g_CurrentViews;
    extern s32 g_currentWindow;
    s32 Gfx_CalcGeometryLOD(Geometry *geometry, f32 *matrix);
    DLSkinGroup *DisplayList_AddGroup_Skin(void);
    f32 world[16] __attribute__((aligned(16)));
    DLSkinGroup *group;
    u16 matrixState;
    Geometry *lod;
    u32 level;
    u32 j;
    u32 i;

    if (g_CurrentViews != 1) {
        for (i = 1; i < 5; i++) {
            if (g_CurrentViews & (1 << i)) {
                Gfx_SetRenderWindow(i);
            }
            if (matrix == NULL || matrix == g_identity) {
                level = Gfx_CalcGeometryLOD(geometry, matrices);
            } else {
                MultiplyMatrix(matrices, matrix, world);
                level = Gfx_CalcGeometryLOD(geometry, world);
            }
            if (level != 0xFFFFFFFF) {
                matrixState = Gfx_AddWorldMatrixState_CacheMatrix(matrix);
                lod = (Geometry *)((u8 *)geometry->groups + level * 0x18);
                for (j = 0; j < lod->numGroups; j++) {
                    group = DisplayList_AddGroup_Skin();
                    group->lod = lod;
                    group->group = j;
                    group->matrix = matrixState;
                    group->matrices = matrices;
                    group->lightState = Gfx_GetLightState();
                    group->window = g_currentWindow;
                    group->count = count;
                }
            }
        }
        return;
    }
    Gfx_DrawAnimGeometryAux_(geometry, matrices, count, matrix);
}



void Gfx_DrawAnimGeometryAux_(Geometry *geometry, f32 *matrices, u32 count, f32 *matrix) {
    extern f32 g_identity[];
    extern u32 matrixlookup[];
    extern s32 g_IsAnim;
    s32 Gfx_CalcGeometryLOD(Geometry *geometry, f32 *matrix);
    void Gfx_DrawGeometryLOD_Immediate(Geometry *geometry, void *matrix);
    f32 world[16] __attribute__((aligned(16)));
    f32 bone[3][4] __attribute__((aligned(16)));
    f32 result[3][4] __attribute__((aligned(16)));
    f32 view[3][4] __attribute__((aligned(16)));
    u32 i;
    Geometry *lod;
    s32 level;

    if (matrix == NULL || matrix == g_identity) {
        level = Gfx_CalcGeometryLOD(geometry, matrices);
    } else {
        MultiplyMatrix(matrices, matrix, world);
        level = Gfx_CalcGeometryLOD(geometry, world);
    }
    lod = (Geometry *)((u8 *)geometry->groups + level * 0x18);
    MatrixToMtxTranspose(view, (u8 *)GetRenderWindow() + 0x40);
    for (i = 0; i < count; i++) {
        MatrixToMtxTranspose(bone, matrices + i * 16);
        PSMTXConcat(view, bone, result);
        GXLoadPosMtxImm(result, matrixlookup[i]);
    }
    g_IsAnim = 1;
    Gfx_DrawGeometryLOD_Immediate(lod, matrices);
    g_IsAnim = 0;
}


