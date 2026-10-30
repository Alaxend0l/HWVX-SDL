#include "common.h"


typedef struct GDLSprite {
    u8 unk0[0x20];
    s32 next;
    u8 unk24[0x30 - 0x24];
} GDLSprite;

typedef struct GDL2DSprite {
    f32 x;
    f32 y;
    f32 z;
    f32 unkC;
    u8 unk10[0x1C - 0x10];
    f32 unk1C;
    u8 unk20[0x24 - 0x20];
    GXColor color;
    u16 tex;
    u8 flag;
    u8 window;
    u8 unk2C[0x30 - 0x2C];
} GDL2DSprite;

typedef struct GDLTriVtx {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
    u8 unk10[0x20 - 0x10];
} GDLTriVtx;

typedef struct GDLTri {
    u8 unk0[0x4];
    GDLTriVtx v[3];
} GDLTri;

typedef struct GDLLine {
    u8 unk0[0x30];
} GDLLine;

typedef struct GDL3DLine {
    Vector4 p0;
    Vector4 p1;
    GXColor color;
    u8 unk24[0x30 - 0x24];
} GDL3DLine;

typedef struct GDLFXLine2D {
    Vector4 p0;
    Vector4 p1;
    GXColor c0;
    GXColor c1;
    u8 flag;
    u8 unk29[0x30 - 0x29];
} GDLFXLine2D;

typedef struct GDLGroupTri {
    u8 type;
    u8 unk1;
    u8 unk2;
    u8 unk3;
    u16 tex;
    u16 unk6;
    s32 unk8;
    u32 count;
    u32 unk10;
    u8 unk14[0x18 - 0x14];
    f32 unk18;
    f32 unk1C;
} GDLGroupTri;

typedef struct GDLAlphaEntry {
    void (*func)(u16);
    u16 arg;
    u16 next;
} GDLAlphaEntry;

typedef struct GfxPointLight {
    Vector4 position;
    f32 radius;
    GXColor color;
    u8 unk18[0x20 - 0x18];
} GfxPointLight;

typedef struct GfxState {
    GfxPointLight pointLights[3];
    Vector4 lightDir;
    GXColor ambientColor;
    u8 unk74[0x78 - 0x74];
    GXColor lightColor;
    u8 unk7C[0x8E - 0x7C];
    s8 lightChanged;
    s8 unk8F;
    u8 numPointLights;
    u8 unk91[0xA0 - 0x91];
} GfxState;

typedef struct GDLLightState {
    Vector4 pointLights[3];
    Vector4 lightDir;
    u32 numPointLights;
    GXColor pointColors[3];
    GXColor ambientColor;
    GXColor lightColor;
    u8 unk58[0x60 - 0x58];
} GDLLightState;

extern GfxState g_state;
extern Matrix4 g_identity;

typedef struct GfxWindow {
    u8 unk0[0x80];
    Matrix4 unk80;
    u8 unkC0[0x140 - 0xC0];
    Matrix4 viewMatrix;
    Vector4 cameraPos;
    Vector4 cameraRight;
    Vector4 cameraUp;
    Vector4 cameraAt;
    u8 unk1C0[0x2C0 - 0x1C0];
    GXColor overlayColor;
    u32 overlayMode;
    u8 unk2C8[0x2CC - 0x2C8];
    f32 unk2CC;
} GfxWindow;

GfxWindow *GetRenderWindow(void);

typedef struct DLSkinGroup {
    void *lod;
    s32 lightState;
    f32 *matrices;
    u32 count;
    u16 matrix;
    u8 group;
    u8 window;
} DLSkinGroup;

typedef struct DLGroup {
    void *geometry;
    s32 lightState;
    u16 matrix;
    u16 unkA;
    u8 group;
    u8 unkD;
    u8 unkE;
    u8 window;
} DLGroup;

typedef struct DisplayList {
    u16 numOpaqueGroups;
    u16 numLitGroups;
    u16 numShadowGroups;
    u16 numAlphaGroups;
    u8 unk8[0x10 - 0x8];
    u16 numSkinGroups;
    u8 unk12[0x14 - 0x12];
    u16 numDecalGroups;
    u16 numOverlayGroups;
    u8 unk18[0x20 - 0x18];
    DLGroup opaqueGroups[0x9C4];
    DLGroup litGroups[0x258];
    DLGroup shadowGroups[0x100];
    DLGroup alphaGroups[0x100];
    DLGroup decalGroups[0x200];
    DLGroup overlayGroups[0x80];
    DLSkinGroup skinGroups[0x100];
    u16 unk11DE0[0x1FF];
    u16 unk121DE[0x1FF];
} DisplayList;

typedef struct GDLRect {
    u8 unk0[0x83];
    u8 window;
} GDLRect;

extern s32 g_currentWindow;
extern u8 g_CurrentViews;


typedef struct GDLEnvmapState {
    f32 unk0;
    f32 unk4;
    u16 unk8;
    u16 unkA;
} GDLEnvmapState;

typedef struct GDLAlphaGroup {
    f32 matrix[4][4];
    s32 unk40;
    s32 unk44;
    u8 unk48[0x4C - 0x48];
    GXColor color;
    u8 unk50;
    u8 window;
    u16 next;
} GDLAlphaGroup;

typedef struct GDLGeomGroup {
    u8 unk0[0x18];
    u16 sortKey;
    u8 unk1A[0x20 - 0x1A];
} GDLGeomGroup;

typedef struct GDLGeom {
    u8 unk0[0x8];
    GDLGeomGroup *groups;
} GDLGeom;

typedef struct GDLDrawEntry {
    void *geometry;
    GXColor color;
    u32 matrix;
    s32 lightState;
    u8 unk10;
    u8 unk11;
    u8 envmapState;
    u8 unk13;
} GDLDrawEntry;

typedef struct GDLLODEntry {
    void *geometry;
    u32 matrix;
} GDLLODEntry;

typedef struct GDL {
    u32 numAlphaEntries;
    u32 num2DSprites;
    u32 numSprites;
    u32 numAlphaGroups;
    u8 unk10[0x14 - 0x10];
    u32 numTris;
    u32 numRects2D;
    u8 unk1C[0x20 - 0x1C];
    u32 numRects;
    u32 num3DLines;
    u8 unk28[0x2C - 0x28];
    u32 numSpheres;
    u32 numBoxes;
    u32 numLines;
    s32 unk38;
    s32 unk3C;
    s32 unk40;
    s32 numGroupTris;
    u8 unk48[0x4C - 0x48];
    u32 numLightStates;
    u32 numCachedMatrices;
    u32 numDrawEntries;
    u32 numLODEntries;
    u8 unk5C[0x6C - 0x5C];
    u32 numEnvmapStates;
    u32 num2DFXLines;
    u32 numMatrices;
    u8 unk78[0x80 - 0x78];
    u16 alphaTable[0x100];
    GDLAlphaEntry alphaEntries[0x1388];
    GDL2DSprite sprites2D[2000];
    GDLSprite sprites[0x7CF];
    u8 unk38C90[0x38CBC - 0x38C90];
    GDLTri tris[2000];
    u8 unk699FC[0x69A00 - 0x699FC];
    GDLAlphaGroup alphaGroups[0x400];
    GDLRect rects2D[0x400];
    GDLRect rects[0x20];
    u8 unkA0A80[0xA2B80 - 0xA0A80];
    GDL3DLine lines3D[2000];
    GDLFXLine2D fxLines2D[200];
    u8 unkBC800[0xC0040 - 0xBC800];
    GDLLine lines[0x100];
    u8 unkC3040[0xC6EC0 - 0xC3040];
    GDLLightState lightStates[0x80];
    f32 cachedMatrices[0x578][4][4];
    GDLDrawEntry drawEntries[0x800];
    GDLLODEntry lodEntries[0x400];
    u8 unkEBCC0[0xEDCC0 - 0xEBCC0];
    u8 unkEDCC0[0xF3A80 - 0xEDCC0];
    GDLEnvmapState envmapStates[0x20];
    void *matrices[0x578];
    u8 unkF51E0[2][0x4E200];
    u8 unk1915E0[2][0x27100];
    u8 unk1DF7E0[2][0x36B00];
    void *unk24CDE0;
    GDLGroupTri *groupTris;
    void *unk24CDE8;
    void *unk24CDEC;
    void *unk24CDF0;
    u16 spriteHeads[0x1FF];
    u8 unk24D1F2;
} GDL;

extern GDL *g_pGDL;
extern DisplayList *g_pDL;


s32 Gfx_DisplayList_Create(void) {
    u32 i;

    g_pGDL = Allocate(0x24D200, 0, "");
    if (g_pGDL == NULL) {
        return 0;
    }
    for (i = 0; i < 2000; i++) {
        g_pGDL->tris[i].v[0].w = 1.0f;
        g_pGDL->tris[i].v[1].w = 1.0f;
        g_pGDL->tris[i].v[2].w = 1.0f;
    }
    g_pGDL->unk24D1F2 = 0;
    return 1;
}

void Gfx_DisplayList_Destroy(void) {
    Deallocate(g_pGDL);
    Deallocate(g_pDL);
}

extern u32 g_DLBufferOn;
void Render_InitDisplayList(void) {
    memset(g_pGDL, 0, 0x80);
    memset(g_pGDL->spriteHeads, 0xFF, 0x3FE);
    memset(g_pDL->unk11DE0, 0xFF, 0x3FE);
    memset(g_pDL->unk121DE, 0xFF, 0x3FE);
    if (g_DLBufferOn) {
        g_pGDL->unk24CDEC = g_pGDL->unk1DF7E0[0];
        g_pGDL->unk24CDE0 = g_pGDL->unkF51E0[0];
        g_pGDL->unk24CDE8 = g_pGDL->unk1915E0[0];
    } else {
        g_pGDL->unk24CDEC = g_pGDL->unk1DF7E0[1];
        g_pGDL->unk24CDE0 = g_pGDL->unkF51E0[1];
        g_pGDL->unk24CDE8 = g_pGDL->unk1915E0[1];
    }
    g_pGDL->groupTris = (GDLGroupTri *)g_pGDL->unkC3040;
    g_pGDL->unk24CDF0 = g_pGDL->unkEDCC0;
    g_DLBufferOn = !g_DLBufferOn;
    Gfx_SetRenderWindow(0);
    GetRenderWindow()->overlayMode = 0;
    g_state.numPointLights = 0;
    g_state.lightChanged = 1;
    g_state.unk8F = 1;
    g_pGDL->numEnvmapStates = 1;
}

GDLLine *Gfx_Add3DLineFX(void) {
    if (g_pGDL->numLines >= 0x100) {
        g_pGDL->numLines = 0;
    }
    return &g_pGDL->lines[g_pGDL->numLines++];
}

void Gfx_Add2DFXLine(Vector4 *p0, Vector4 *p1, GXColor *c0, GXColor *c1, u8 flag) {
    if (g_pGDL->num2DFXLines == 200) {
        return;
    }
    g_pGDL->fxLines2D[g_pGDL->num2DFXLines].p0 = *p0;
    g_pGDL->fxLines2D[g_pGDL->num2DFXLines].p1 = *p1;
    g_pGDL->fxLines2D[g_pGDL->num2DFXLines].c0 = *c0;
    g_pGDL->fxLines2D[g_pGDL->num2DFXLines].c1 = *c1;
    g_pGDL->fxLines2D[g_pGDL->num2DFXLines].flag = flag;
    g_pGDL->num2DFXLines++;
}

GDLSprite *Gfx_AddBillBoardSprite(u16 tex) {
    GDLSprite *sprite;

    if (g_pGDL->numSprites == 0x7CF) {
        g_pGDL->numSprites = 0;
    }
    sprite = &g_pGDL->sprites[g_pGDL->numSprites];
    sprite->next = g_pGDL->spriteHeads[tex];
    g_pGDL->spriteHeads[tex] = g_pGDL->numSprites++;
    return sprite;
}

void *Gfx_Begin_GroupTriFlat(u16 tex, u16 arg1, u8 arg2) {
    u32 n;

    n = g_pGDL->numGroupTris;
    if (n >= 500) {
        n = 0;
        g_pGDL->numGroupTris = 0;
    }
    if (n != 0 && g_pGDL->groupTris[n - 1].type == 0 && g_pGDL->groupTris[n - 1].unk10 >= 0xFFFE &&
        tex == g_pGDL->groupTris[n - 1].tex && arg1 == g_pGDL->groupTris[n - 1].unk6) {
        g_pGDL->numGroupTris--;
    } else {
        g_pGDL->groupTris[n].type = 0;
        g_pGDL->groupTris[n].unk1 = 0;
        g_pGDL->groupTris[n].unk2 = arg2;
        g_pGDL->groupTris[n].unk8 = g_pGDL->unk38;
        g_pGDL->groupTris[n].count = 0;
        g_pGDL->groupTris[n].tex = tex;
        g_pGDL->groupTris[n].unk6 = arg1;
        g_pGDL->groupTris[n].unk10 = 0xFFFE;
        g_pGDL->groupTris[n].unk18 = 0.0f;
        g_pGDL->groupTris[n].unk1C = 0.0f;
    }
    return (u8 *)g_pGDL->unk24CDE0 + g_pGDL->unk38 * 0x50;
}

void *Gfx_Begin_GroupQuadFlat(u16 tex, u16 arg1, u8 arg2) {
    extern GDL *g_pGDL;
    u32 n;

    n = g_pGDL->numGroupTris;
    if (n >= 500) {
        n = 0;
        g_pGDL->numGroupTris = 0;
    }
    if (n != 0 && (&g_pGDL->groupTris[n])[-1].type == 4 && (&g_pGDL->groupTris[n])[-1].unk10 >= 0xFFFE &&
        tex == (&g_pGDL->groupTris[n])[-1].tex && arg1 == (&g_pGDL->groupTris[n])[-1].unk6) {
        g_pGDL->numGroupTris--;
    } else {
        g_pGDL->groupTris[n].type = 4;
        g_pGDL->groupTris[n].unk1 = 0;
        g_pGDL->groupTris[n].unk2 = arg2;
        g_pGDL->groupTris[n].unk8 = g_pGDL->unk40;
        g_pGDL->groupTris[n].count = 0;
        g_pGDL->groupTris[n].tex = tex;
        g_pGDL->groupTris[n].unk6 = arg1;
        g_pGDL->groupTris[n].unk10 = 0xFFFE;
        g_pGDL->groupTris[n].unk18 = 0.0f;
        g_pGDL->groupTris[n].unk1C = 0.0f;
    }
    return (u8 *)g_pGDL->unk24CDE8 + g_pGDL->unk40 * 0x50;
}

void *Gfx_Begin_GroupTriShaded(u16 tex, u16 arg1, u8 arg2) {
    u32 n;

    n = g_pGDL->numGroupTris;
    if (n >= 500) {
        n = 0;
        g_pGDL->numGroupTris = 0;
    }
    if (n != 0 && g_pGDL->groupTris[n - 1].type == 1 && g_pGDL->groupTris[n - 1].unk10 >= 0xFFFE &&
        tex == g_pGDL->groupTris[n - 1].tex && arg1 == g_pGDL->groupTris[n - 1].unk6) {
        g_pGDL->numGroupTris--;
    } else {
        g_pGDL->groupTris[n].type = 1;
        g_pGDL->groupTris[n].unk1 = 0;
        g_pGDL->groupTris[n].unk2 = arg2;
        g_pGDL->groupTris[n].unk8 = g_pGDL->unk3C;
        g_pGDL->groupTris[n].count = 0;
        g_pGDL->groupTris[n].tex = tex;
        g_pGDL->groupTris[n].unk6 = arg1;
        g_pGDL->groupTris[n].unk10 = 0xFFFE;
        g_pGDL->groupTris[n].unk18 = 0.0f;
        g_pGDL->groupTris[n].unk1C = 0.0f;
    }
    return (u8 *)g_pGDL->unk24CDEC + g_pGDL->unk3C * 0x70;
}

void Gfx_End_GroupTri(s32 n) {
    g_pGDL->groupTris[g_pGDL->numGroupTris].count += n;
    if (g_pGDL->groupTris[g_pGDL->numGroupTris].count != 0) {
        switch (g_pGDL->groupTris[g_pGDL->numGroupTris].type) {
        case 0:
            g_pGDL->unk38 += n;
            break;
        case 1:
            g_pGDL->unk3C += n;
            break;
        case 2:
            break;
        case 4:
            g_pGDL->unk40 += n;
            break;
        }
        g_pGDL->numGroupTris++;
    }
}

void Gfx_Add2DSprite(Vector4 *pos, u16 tex, GXColor *color, u8 flag, f32 a, f32 b) {
    if (g_pGDL->num2DSprites == 2000) {
        return;
    }
    g_pGDL->sprites2D[g_pGDL->num2DSprites].x = pos->x;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].y = pos->y;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].z = pos->z;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].unkC = a;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].unk1C = b;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].color = *color;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].tex = tex;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].flag = flag;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].window = g_currentWindow;
    g_pGDL->num2DSprites++;
}

void Gfx_AddCorona(Vector4 *pos, u16 tex, GXColor *color, u8 flag, f32 a, f32 b) {
    if (g_pGDL->num2DSprites == 2000) {
        return;
    }
    g_pGDL->sprites2D[g_pGDL->num2DSprites].x = pos->x;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].y = pos->y;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].z = pos->z;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].unkC = a * GetRenderWindow()->unk2CC;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].unk1C = b;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].color = *color;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].tex = tex;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].flag = flag;
    g_pGDL->sprites2D[g_pGDL->num2DSprites].window = g_currentWindow;
    g_pGDL->num2DSprites++;
}

GDLRect *Gfx_Add2DRectangleRenderWindow(void) {
    if (g_pGDL->numRects >= 0x20) {
        g_pGDL->numRects = 0;
    }
    g_pGDL->rects[g_pGDL->numRects].window = g_currentWindow;
    return &g_pGDL->rects[g_pGDL->numRects++];
}

GDLRect *Gfx_Add2DRectangle(void) {
    if (g_pGDL->numRects2D >= 0x400) {
        g_pGDL->numRects2D = 0;
    }
    return &g_pGDL->rects2D[g_pGDL->numRects2D++];
}

void Gfx_Add2DRectangleBack(void)
{
    Render_DrawRectangle();
}

extern f32 g_alphaTestMin;
extern f32 g_AlphaTestMax;
extern f32 g_AlphaScale;

static inline void Gfx_LinkAlphaEntry(u32 n, f32 z) {
    u32 idx;

    if (g_alphaTestMin > z) {
        g_alphaTestMin = z;
    }
    if (g_AlphaTestMax < z) {
        g_AlphaTestMax = z;
    }
    g_AlphaScale = 255.0f / (g_AlphaTestMax - g_alphaTestMin);
    idx = g_AlphaScale * (z - g_alphaTestMin);
    if (idx >= 0x100) {
        idx = 0xFF;
    }
    g_pGDL->alphaEntries[n].next = g_pGDL->alphaTable[idx];
    g_pGDL->alphaTable[idx] = n;
}

void DisplayList_AddAlphaGroup(s32 arg0, s32 arg1, u32 matrix, u8 arg3, GXColor *color) {
    if (g_pGDL->numAlphaGroups == 0x400) {
        return;
    }
    g_pGDL->alphaGroups[g_pGDL->numAlphaGroups].unk40 = arg0;
    g_pGDL->alphaGroups[g_pGDL->numAlphaGroups].unk44 = arg1;
    g_pGDL->alphaGroups[g_pGDL->numAlphaGroups].window = g_currentWindow;
    if (g_CurrentViews != 1) {
        Gfx_SetWorldMatrix(Gfx_GetWorldMatrixState(matrix));
    }
    memcpy(g_pGDL->alphaGroups[g_pGDL->numAlphaGroups].matrix, &GetRenderWindow()->unk80, 0x40);
    g_pGDL->alphaGroups[g_pGDL->numAlphaGroups].unk50 = arg3;
    g_pGDL->alphaGroups[g_pGDL->numAlphaGroups].color = *color;
    g_pGDL->numAlphaGroups++;
}

void Gfx_SortAlpha(void) {
    void XformVec4x4(Matrix4 *m, Vector4 *v, Vector4 *out);
    u16 heads[0x1FF];
    GDLAlphaGroup *grp;
    u16 key;
    GDLAlphaEntry *entry;
    u32 count;
    f32 z;
    u16 i;

    MemClr32(g_pGDL->alphaTable, 0x200);
    g_pGDL->numAlphaEntries = 1;
    for (i = 0; i < g_pGDL->numTris; i++) {
        XformVec4x4((Matrix4 *)((u8 *)GetRenderWindow() + 0x40), (Vector4 *)&g_pGDL->tris[i].v[0], (Vector4 *)&g_pGDL->tris[i].v[0]);
        XformVec4x4((Matrix4 *)((u8 *)GetRenderWindow() + 0x40), (Vector4 *)&g_pGDL->tris[i].v[1], (Vector4 *)&g_pGDL->tris[i].v[1]);
        XformVec4x4((Matrix4 *)((u8 *)GetRenderWindow() + 0x40), (Vector4 *)&g_pGDL->tris[i].v[2], (Vector4 *)&g_pGDL->tris[i].v[2]);
        z = 0.333333f * (g_pGDL->tris[i].v[2].z + (g_pGDL->tris[i].v[0].z + g_pGDL->tris[i].v[1].z));
        g_pGDL->alphaEntries[g_pGDL->numAlphaEntries].func = Render_DrawAlphaTriangle;
        g_pGDL->alphaEntries[g_pGDL->numAlphaEntries].arg = i;
        Gfx_LinkAlphaEntry(g_pGDL->numAlphaEntries++, z);
    }

    grp = g_pGDL->alphaGroups;
    memset(heads, -1, sizeof(heads));
    for (i = 0; i < g_pGDL->numAlphaGroups; i++, grp++) {
        key = ((GDLGeom *)PTR(grp->unk40))->groups[grp->unk44].sortKey;
        grp->next = heads[key];
        heads[key] = i;
    }

    count = g_pGDL->numAlphaEntries;
    if (g_pGDL->numAlphaGroups != 0) {
        entry = &g_pGDL->alphaEntries[count];
        for (i = 0; i < 0x1FF; i++) {
            key = heads[i];
            while (key != 0xFFFF) {
                grp = &g_pGDL->alphaGroups[key];
                entry->func = Render_DrawAlphaGroup;
                entry->arg = key;
                Gfx_LinkAlphaEntry(count++, 0.0f);
                entry++;
                key = grp->next;
            }
        }
    }
    g_pGDL->numAlphaEntries = count;
}


void DrawAlphaTable(void) {
    u16 i;
    u16 idx;

    for (i = 0; i < 0x100; i++) {
        for (idx = g_pGDL->alphaTable[i]; idx != 0; idx = g_pGDL->alphaEntries[idx].next) {
            g_pGDL->alphaEntries[idx].func(g_pGDL->alphaEntries[idx].arg);
        }
    }
}

s32 Gfx_GetLightState(void) {
    u32 i;
    GDLLightState *ls;
    u32 idx;

    if (!g_state.lightChanged) {
        return g_pGDL->numLightStates - 1;
    }
    if (g_pGDL->numLightStates >= 0x80) {
        g_pGDL->numLightStates = 0;
    }
    idx = g_pGDL->numLightStates++;
    g_state.lightChanged = 0;
    ls = &g_pGDL->lightStates[idx];
    ls->lightColor = g_state.lightColor;
    ls->ambientColor = g_state.ambientColor;
    ls->lightDir = g_state.lightDir;
    ls->numPointLights = g_state.numPointLights;
    for (i = 0; i < ls->numPointLights; i++) {
        ls->pointLights[i].x = g_state.pointLights[i].position.x;
        ls->pointLights[i].y = g_state.pointLights[i].position.y;
        ls->pointLights[i].z = g_state.pointLights[i].position.z;
        ls->pointLights[i].w = g_state.pointLights[i].radius;
        ls->pointColors[i] = g_state.pointLights[i].color;
    }
    return idx;
}

void Gfx_SetEnvmap(u16 a, u16 b, s8 flag, f32 x, f32 y) {
    u32 i;

    if (flag == 0) {
        for (i = 1; i < g_pGDL->numEnvmapStates; i++) {
            if (g_pGDL->envmapStates[i].unk0 == x && g_pGDL->envmapStates[i].unk4 == y &&
                g_pGDL->envmapStates[i].unkA == b && g_pGDL->envmapStates[i].unk8 == a) {
                return;
            }
        }
        g_pGDL->envmapStates[g_pGDL->numEnvmapStates].unk0 = x;
        g_pGDL->envmapStates[g_pGDL->numEnvmapStates].unk4 = y;
        g_pGDL->envmapStates[g_pGDL->numEnvmapStates].unkA = b;
        g_pGDL->envmapStates[g_pGDL->numEnvmapStates].unk8 = a;
        g_pGDL->numEnvmapStates++;
    } else {
        g_pGDL->envmapStates[0].unk0 = x;
        g_pGDL->envmapStates[0].unk4 = y;
        g_pGDL->envmapStates[0].unkA = b;
        g_pGDL->envmapStates[0].unk8 = a;
    }
    g_state.unk8F = flag;
}

s32 Gfx_GetEnvmapState(void) {
    if (g_state.unk8F) {
        return 0;
    }
    return g_pGDL->numEnvmapStates - 1;
}

u32 Gfx_AddWorldMatrixState_NonCached(void *matrix) {
    if (matrix == NULL) {
        return 0xFFFE;
    }
    if (g_pGDL->numMatrices >= 0x578) {
        g_pGDL->numMatrices = 0;
    }
    g_pGDL->matrices[g_pGDL->numMatrices] = matrix;
    return g_pGDL->numMatrices++;
}

u32 Gfx_AddWorldMatrixState_CacheMatrix(f32 (*matrix)[4]) {
    if (matrix == NULL) {
        return 0xFFFE;
    }
    if (g_pGDL->numMatrices >= 0x578) {
        g_pGDL->numMatrices = 0;
    }
    if (g_pGDL->numCachedMatrices >= 0x578) {
        g_pGDL->numCachedMatrices = 0;
    }
    CopyMatrix(g_pGDL->cachedMatrices[g_pGDL->numCachedMatrices], matrix);
    g_pGDL->matrices[g_pGDL->numMatrices] = g_pGDL->cachedMatrices[g_pGDL->numCachedMatrices];
    g_pGDL->numCachedMatrices++;
    return g_pGDL->numMatrices++;
}

u32 Gfx_AddWorldMatrixState_CacheMatrixQuaternionScalePos(void *quat, void *scale, Vector4 *pos) {
    pos->w = 1.0f;
    if (g_pGDL->numMatrices >= 0x578) {
        g_pGDL->numMatrices = 0;
    }
    if (g_pGDL->numCachedMatrices >= 0x578) {
        g_pGDL->numCachedMatrices = 0;
    }
    QuaternionToSubMatrix(quat, g_pGDL->cachedMatrices[g_pGDL->numCachedMatrices]);
    if (scale != NULL) {
        ScaleMatrix(g_pGDL->cachedMatrices[g_pGDL->numCachedMatrices], scale);
    }
    CopyVector((Vector4 *)g_pGDL->cachedMatrices[g_pGDL->numCachedMatrices][3], pos);
    g_pGDL->matrices[g_pGDL->numMatrices] = g_pGDL->cachedMatrices[g_pGDL->numCachedMatrices];
    g_pGDL->numCachedMatrices++;
    return g_pGDL->numMatrices++;
}

typedef struct GeometryGroup {
    u16 unk0;
    u16 flags;
    u8 unk4[0x10 - 0x4];
    void *data;
    u16 unk14;
    u8 unk16[0x18 - 0x16];
    u16 unk18;
    u8 unk1A[0x1C - 0x1A];
    u16 (*tris)[3];
} GeometryGroup;

typedef struct GeometryVertex {
    Vector4 pos;
    u8 unk10[0x1C - 0x10];
    GXColor color;
    u8 unk20[0x30 - 0x20];
} GeometryVertex;

typedef struct Geometry {
    u16 unk0;
    u16 numGroups;
    u16 unk4;
    u16 unk6;
    GeometryGroup *groups;
    GeometryVertex *verts;
} Geometry;

static inline void Gfx_Add3DLine(Vector4 *p0, Vector4 *p1, GXColor *color) {
    g_pGDL->lines3D[g_pGDL->num3DLines].p0 = *p0;
    g_pGDL->lines3D[g_pGDL->num3DLines].p1 = *p1;
    g_pGDL->lines3D[g_pGDL->num3DLines].color = *color;
    g_pGDL->num3DLines++;
}

void *Gfx_GetWorldMatrixState(u32 index) {
    if (index == 0xFFFE) {
        return &g_identity;
    }
    if (index == 0xFFFF) {
        return &GetRenderWindow()->viewMatrix;
    }
    return g_pGDL->matrices[index];
}

void XformVec(void *m, Vector4 *v, Vector4 *out);

void DrawGroupWireframe(Geometry *geometry, u32 group, void *matrix) {
    Vector4 v[3] __attribute__((aligned(16)));
    GeometryGroup *grp;
    u32 i;
    u32 count;
    s32 j;

    grp = &geometry->groups[group];
    count = (u32)HND(grp->data);
    for (i = 0; i < count; i++) {
        for (j = 0; j < 3; j++) {
            XformVec(matrix, &geometry->verts[grp->tris[i][j]].pos, &v[j]);
        }
        Gfx_Add3DLine(&v[0], &v[1], &geometry->verts[grp->tris[i][0]].color);
        Gfx_Add3DLine(&v[1], &v[2], &geometry->verts[grp->tris[i][1]].color);
        Gfx_Add3DLine(&v[2], &v[0], &geometry->verts[grp->tris[i][2]].color);
    }
}


void Gfx_GetCameraPosition(Vector4 *pos) {
    *pos = GetRenderWindow()->cameraPos;
}

void Gfx_GetCameraVector(Vector4 *right, Vector4 *up, Vector4 *at) {
    *right = GetRenderWindow()->cameraRight;
    *up = GetRenderWindow()->cameraUp;
    *at = GetRenderWindow()->cameraAt;
}

void Gfx_ScreenOverlay(GXColor *color, u32 mode) {
    if (mode != GetRenderWindow()->overlayMode) {
        GetRenderWindow()->overlayColor = *color;
        GetRenderWindow()->overlayMode = mode;
        return;
    }
    if (color->r > GetRenderWindow()->overlayColor.r) {
        GetRenderWindow()->overlayColor.r = color->r;
    }
    if (color->g > GetRenderWindow()->overlayColor.g) {
        GetRenderWindow()->overlayColor.g = color->g;
    }
    if (color->b > GetRenderWindow()->overlayColor.b) {
        GetRenderWindow()->overlayColor.b = color->b;
    }
    if (color->a > GetRenderWindow()->overlayColor.a) {
        GetRenderWindow()->overlayColor.a = color->a;
    }
}

DLSkinGroup *DisplayList_AddGroup_Skin(void) {
    DisplayList *dl = g_pDL;

    if (dl->numSkinGroups == 0x100) {
        return NULL;
    }
    return &dl->skinGroups[dl->numSkinGroups++];
}

static inline DLGroup *DisplayList_AddGroup_Opaque(u16 key) {
    DLGroup *group;

    if (g_pDL->numOpaqueGroups == 0x9C4) {
        return NULL;
    }
    group = &g_pDL->opaqueGroups[g_pDL->numOpaqueGroups];
    group->unkA = g_pDL->unk11DE0[key];
    g_pDL->unk11DE0[key] = g_pDL->numOpaqueGroups++;
    return group;
}

static inline DLGroup *DisplayList_AddGroup_Lit(u16 key) {
    DLGroup *group;

    if (g_pDL->numLitGroups == 0x258) {
        return NULL;
    }
    group = &g_pDL->litGroups[g_pDL->numLitGroups];
    group->unkA = g_pDL->unk121DE[key];
    g_pDL->unk121DE[key] = g_pDL->numLitGroups++;
    return group;
}

static inline DLGroup *DisplayList_AddGroup_Shadow(void) {
    DisplayList *dl;

    if (g_CurrentViews != 1) {
        return NULL;
    }
    dl = g_pDL;
    if (dl->numShadowGroups == 0x100) {
        return NULL;
    }
    return &dl->shadowGroups[dl->numShadowGroups++];
}

static inline DLGroup *DisplayList_AddGroup_Alpha(void) {
    DisplayList *dl = g_pDL;

    if (dl->numAlphaGroups == 0x100) {
        return NULL;
    }
    return &dl->alphaGroups[dl->numAlphaGroups++];
}

static inline DLGroup *DisplayList_AddGroup_Decal(void) {
    DisplayList *dl = g_pDL;

    if (dl->numDecalGroups == 0x200) {
        return NULL;
    }
    return &dl->decalGroups[dl->numDecalGroups++];
}

static inline DLGroup *DisplayList_AddGroup_Overlay(void) {
    DisplayList *dl = g_pDL;

    if (dl->numOverlayGroups == 0x80) {
        return NULL;
    }
    return &dl->overlayGroups[dl->numOverlayGroups++];
}

static inline void DisplayList_AddGeometryAlphaGroup(Geometry *geom, u16 group, u32 matrix, u8 flag, GXColor color) {
    DisplayList_AddAlphaGroup((s32)HND(geom), group, matrix, flag, &color);
}

u32 Gfx_GetCullLODGX(void *geometry, void *matrix, u8 *lod, u8 flag);

void DisplayList_RenderGXList(void) {
    GXColor color = {0, 0, 0, 0};
    u32 j;
    u32 i;
    u32 view;
    void *matrix;
    u32 geometry;
    u8 lod;
    u8 lod2;

    if (g_CurrentViews == 1) {
        for (j = 0; j < g_pGDL->numDrawEntries; j++) {
            matrix = Gfx_GetWorldMatrixState(g_pGDL->drawEntries[j].matrix);
            geometry = Gfx_GetCullLODGX(g_pGDL->drawEntries[j].geometry, matrix, &lod, g_pGDL->drawEntries[j].unk11);
            if (geometry) {
                DisplayList_AddGeometry(geometry, g_pGDL->drawEntries[j].matrix, g_pGDL->drawEntries[j].lightState,
                                        lod, g_pGDL->drawEntries[j].unk10, g_pGDL->drawEntries[j].color);
            }
        }
        for (j = 0; j < g_pGDL->numLODEntries; j++) {
            DisplayList_AddGeometry((u32)HND(g_pGDL->lodEntries[j].geometry), g_pGDL->lodEntries[j].matrix, -1, 1, 0, color);
        }
    } else {
        for (i = 0; i < g_pGDL->numDrawEntries; i++) {
            matrix = Gfx_GetWorldMatrixState(g_pGDL->drawEntries[i].matrix);
            for (view = 1; view < 5; view++) {
                if (g_CurrentViews & (1 << view)) {
                    Gfx_SetRenderWindow(view);
                    geometry = Gfx_GetCullLODGX(g_pGDL->drawEntries[i].geometry, matrix, &lod2, g_pGDL->drawEntries[i].unk11);
                    if (geometry) {
                        DisplayList_AddGeometry(geometry, g_pGDL->drawEntries[i].matrix,
                                                g_pGDL->drawEntries[i].lightState, lod2, g_pGDL->drawEntries[i].unk10,
                                                g_pGDL->drawEntries[i].color);
                    }
                }
            }
        }
        for (i = 0; i < g_pGDL->numLODEntries; i++) {
            for (view = 1; view < 5; view++) {
                if (g_CurrentViews & (1 << view)) {
                    Gfx_SetRenderWindow(view);
                    DisplayList_AddGeometry((u32)HND(g_pGDL->lodEntries[i].geometry), g_pGDL->lodEntries[i].matrix, -1, 1, 0, color);
                }
            }
        }
    }
}

void DisplayList_AddGeometry(u32 geometry, u16 matrix, s32 lightState, u8 lod, u8 flag, GXColor color) {
    Geometry *geom = (Geometry *)PTR(geometry);
    DLGroup *group;
    u16 i;
    u16 flags;
    u8 unkE;

    if (geom->groups[0].flags & 0x4000) {
        for (i = 0; i < geom->numGroups; i++) {
            if (geom->groups[i].data != NULL) {
                group = DisplayList_AddGroup_Overlay();
                if (group != NULL) {
                    group->geometry = geom;
                    group->group = i;
                    group->matrix = matrix;
                    group->unkE = 0;
                    group->unkD = lod;
                    group->window = g_currentWindow;
                }
            }
        }
        return;
    }
    for (i = 0; i < geom->numGroups; i++) {
        if (geom->groups[i].data == NULL) {
            continue;
        }
        flags = geom->groups[i].flags;
        unkE = geom->groups[i].unk14;
        if (flags & 2) {
            group = DisplayList_AddGroup_Alpha();
            if (group != NULL) {
                group->geometry = geom;
                group->group = i;
                group->matrix = matrix;
                group->unkE = unkE;
                group->unkD = lod;
                group->window = g_currentWindow;
                group->lightState = lightState;
            }
        } else if (flags & 0x80) {
            DisplayList_AddGeometryAlphaGroup(geom, i, matrix, flag, color);
        } else if (flags & 1) {
            DisplayList_AddGeometryAlphaGroup(geom, i, matrix, flag, color);
        } else if (!(flags & 0x40)) {
            if ((flags & 4) || (flags & 0x10)) {
                group = DisplayList_AddGroup_Lit(geom->groups[i].unk18);
                if (group != NULL) {
                    group->geometry = geom;
                    group->group = i;
                    group->matrix = matrix;
                    group->unkE = unkE;
                    group->unkD = lod;
                    group->lightState = lightState;
                    group->window = g_currentWindow;
                }
            } else if (flags & 0x100) {
                group = DisplayList_AddGroup_Decal();
                if (group != NULL) {
                    group->geometry = geom;
                    group->group = i;
                    group->matrix = matrix;
                    group->unkE = unkE;
                    group->unkD = lod;
                    group->window = g_currentWindow;
                }
            } else {
                group = DisplayList_AddGroup_Opaque(geom->groups[i].unk18);
                if (group != NULL) {
                    group->geometry = geom;
                    group->group = i;
                    group->matrix = matrix;
                    group->unkE = unkE;
                    group->unkD = lod;
                    group->window = g_currentWindow;
                }
            }
        }
        if (geom->groups[i].flags & 0x10) {
            group = DisplayList_AddGroup_Shadow();
            if (group != NULL) {
                group->geometry = geom;
                group->group = i;
                group->matrix = matrix;
                group->unkE = unkE;
                group->unkD = lod;
                group->window = g_currentWindow;
            }
        }
    }
}

