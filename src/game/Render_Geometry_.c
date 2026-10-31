#include "common.h"
#include "Gfx.h"


extern GXColor white;
extern GXColor black;

typedef union {
    u8 u8;
    u16 u16;
    u32 u32;
    s32 s32;
    f32 f32;
} PPCWGPipe;


static inline void GXPosition3f32(f32 x, f32 y, f32 z) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
    GXWGFifo_f32(z);
}

static inline void GXColor4u8(const u8 r, const u8 g, const u8 b, const u8 a) {
    GXWGFifo_u8(r);
    GXWGFifo_u8(g);
    GXWGFifo_u8(b);
    GXWGFifo_u8(a);
}

extern s32 m_triCount;

typedef struct GeometryGroup {
    u16 unk0;
    u16 flags;
    GXColor color;
    u8 unk8[0xC - 0x8];
    PTR32(struct UVScroll) uvScroll;
    u32 dlSize;
    u16 unk14;
    u8 unk16[0x18 - 0x16];
    u16 unk18;
    u16 unk1A;
    PTR32(void) displayList;
} GeometryGroup;

typedef struct UVScroll {
    f32 u;
    f32 v;
    GXColor color;
    u32 flags;
} UVScroll;

typedef struct GfxState {
    u8 unk0[0x74];
    GXColor constColor;
    u8 unk78[0x7C - 0x78];
    u32 flags;
    u8 unk80[0x93 - 0x80];
    u8 unk93;
    u8 unk94[0xA0 - 0x94];
} GfxState;

typedef struct Texture {
    u8 unk0[0x24];
    PTR32(void) data;
    u32 unk28;
    f32 unk2C;
    u16 unk30;
    u16 width;
    u16 height;
    s16 refCount;
    u16 unk38;
    u16 unk3A;
    u16 view;
    u16 wrapMode;
} Texture;

extern Texture *m_texture;
extern s32 m_curTexture;
extern u32 g_onebitflag;
extern u32 g_currentWindow;

typedef struct GDLAlphaGroup {
    f32 matrix[4][4];
    PTR32(struct Geometry) geometry;
    s32 group;
    u8 unk48[0x4C - 0x48];
    GXColor color;
    u8 unk50;
    u8 window;
    u16 next;
} GDLAlphaGroup;

extern GfxState g_state;
extern s32 g_debugTriCount;

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

typedef struct GDLSphere {
    f32 x;
    f32 y;
    f32 z;
    u8 unkC[0x14 - 0xC];
    f32 radius;
    u8 unk18[0x20 - 0x18];
} GDLSphere;

typedef struct GDLDrawEntry {
    PTR32(void) geometry;
    GXColor color;
    u32 matrix;
    s32 lightState;
    u8 unk10;
    u8 unk11;
    u8 envmapState;
    u8 unk13;
} GDLDrawEntry;

typedef struct GDLLODEntry {
    PTR32(void) geometry;
    u32 matrix;
} GDLLODEntry;

typedef struct GDLEnvmapState {
    f32 unk0;
    f32 unk4;
    u16 unk8;
    u16 unkA;
} GDLEnvmapState;

typedef struct GDLBox {
    Vector4 min;
    Vector4 max;
    u8 unk20[0x30 - 0x20];
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    u8 unk34[0x50 - 0x34];
} GDLBox;

typedef struct GDLLightState {
    Vector4 pointLights[3];
    Vector4 lightDir;
    u32 numPointLights;
    GXColor pointColors[3];
    GXColor lightColor;
    GXColor ambientColor;
    u8 unk58[0x60 - 0x58];
} GDLLightState;

static inline GXColor Render_MakeColor(u8 r, u8 g, u8 b) {
    GXColor c;

    c.r = r;
    c.g = g;
    c.b = b;
    c.a = 0xFF;
    return c;
}

extern u8 m_totalLights;

static inline void Render_InitDirLight(void *lt_obj, Vector4 *dir, Vector4 *lightDir, GXColor *rgb) {
    GXColor color;

    color.r = rgb->r;
    color.g = rgb->g;
    color.b = rgb->b;
    color.a = 0xFF;
    memset(lt_obj, 0, 0x40);
    InverseXformVecOrientation(GetRenderWindow(), lightDir, dir);
    GXInitLightPos(lt_obj, -10000.0f * dir->x, 10000.0f * dir->y, -10000.0f * dir->z);
    GXInitLightColor(lt_obj, color);
    GXLoadLightObjImm(lt_obj, 1);
}

static inline void Render_InitPointLight(void *lt_obj, Vector4 *pos, GXColor *rgb) {
    GXColor color = Render_MakeColor(rgb->r, rgb->g, rgb->b);

    memset(lt_obj, 0, 0x40);
    GXInitLightPos(lt_obj, pos->x, pos->y, pos->z);
    GXInitLightColor(lt_obj, color);
    GXInitLightDistAttn(lt_obj, pos->w, 0.99999f, 1);
    GXInitLightAttnK(lt_obj, 0.0f, 0.0f, 0.01f);
    GXLoadLightObjImm(lt_obj, 1 << m_totalLights);
}

typedef struct GDL {
    u8 unk0[0x2C];
    u32 numSpheres;
    u32 numBoxes;
    u8 unk34[0x54 - 0x34];
    u32 numDrawEntries;
    u32 numLODEntries;
    u8 unk5C[0x69A00 - 0x5C];
    GDLAlphaGroup alphaGroups[0x400];
    u8 unk7EA00[0xBC800 - 0x7EA00];
    GDLSphere spheres[200];
    u8 unkBE100[0xBE110 - 0xBE100];
    GDLBox boxes[100];
    u8 unkC0050[0xC6EC0 - 0xC0050];
    GDLLightState lightStates[0x80];
    u8 unkC9EC0[0xDFCC0 - 0xC9EC0];
    GDLDrawEntry drawEntries[0x800];
    GDLLODEntry lodEntries[0x400];
    u8 unkEBCC0[0xF3A80 - 0xEBCC0];
    GDLEnvmapState envmapStates[0x20];
} GDL;

typedef struct GDLMatrix {
    f32 m[16];
} __attribute__((aligned(16))) GDLMatrix;

typedef struct DLSkinGroup {
    PTR32(void) lod;
    s32 lightState;
    PTR32(f32) matrices;
    u32 count;
    u16 matrix;
    u8 group;
    u8 window;
} DLSkinGroup;

typedef struct DLGroup {
    PTR32(void) geometry;
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

extern DisplayList *g_pDL;
extern u8 g_CurrentViews;

extern GDL *g_pGDL;
extern s32 m_cullMode;

extern f32 g_identity[4][4];
extern u32 m_fillMode;
void RenderGroup(Geometry *geometry, u32 group, f32 (*matrix)[4]);

extern u32 m_VtxDescFlag;
extern u32 *m_vertexBuffer;
extern u32 m_pointLightMask;

extern s32 m_curLightState;
extern s32 m_curEnvmapState;
void Render_DrawOpaqueGroup(Geometry *geometry, u32 group, void *matrix);
void EnvMap(Geometry *geometry, u32 group, void *matrix, s32 mode);
void Gfx_DrawGeometryLOD_Immediate(Geometry *geometry, f32 (*matrix)[4]);
void Render_Geometry(Geometry *geom, f32 *m);

static inline void RenderOpaqueGroup(Geometry *geometry, u32 group, void *matrix) {
    Render_DrawOpaqueGroup(geometry, group, matrix);
    if ((GETP(geometry->groups)[group].flags & 0x10) && g_CurrentViews == 1) {
        EnvMap(geometry, group, matrix, 2);
    }
}

void Render_GeometryListSplitScreen(void) {
    u8 view;
    u32 i;

    for (view = 1; view < 5; view++) {
        if (g_CurrentViews & (1 << view)) {
            Gfx_SetRenderWindow(view);
            Render_StateSetScissor();
            for (i = 0; i < g_pDL->numOpaqueGroups; i++) {
                Geometry *geometry = GETP(g_pDL->opaqueGroups[i].geometry);
                void *matrix = Gfx_GetWorldMatrixState(g_pDL->opaqueGroups[i].matrix);
                if (g_pDL->opaqueGroups[i].window == view) {
                    if (matrix == g_identity) {
                        Gfx_SetWorldMatrix(NULL);
                    } else {
                        Gfx_SetWorldMatrix(matrix);
                    }
                    RenderGroup(geometry, g_pDL->opaqueGroups[i].group, matrix);
                }
            }
            for (i = 0; i < g_pDL->numLitGroups; i++) {
                Geometry *geometry = GETP(g_pDL->litGroups[i].geometry);
                void *matrix = Gfx_GetWorldMatrixState(g_pDL->litGroups[i].matrix);
                m_curLightState = g_pDL->litGroups[i].lightState;
                if (g_pDL->litGroups[i].window == view) {
                    if (matrix == g_identity) {
                        Gfx_SetWorldMatrix(NULL);
                    } else {
                        Gfx_SetWorldMatrix(matrix);
                    }
                    RenderGroup(geometry, g_pDL->litGroups[i].group, matrix);
                }
            }
            for (i = 0; i < g_pDL->numAlphaGroups; i++) {
                Geometry *geometry = GETP(g_pDL->alphaGroups[i].geometry);
                void *matrix = Gfx_GetWorldMatrixState(g_pDL->alphaGroups[i].matrix);
                if (g_pDL->alphaGroups[i].window == view) {
                    if (matrix == g_identity) {
                        Gfx_SetWorldMatrix(NULL);
                    } else {
                        Gfx_SetWorldMatrix(matrix);
                    }
                    RenderGroup(geometry, g_pDL->alphaGroups[i].group, matrix);
                }
            }
        }
    }
}

void Render_GeometryList(void) {
    u32 i;
    void *geometry;
    void *matrix;
    GXColor color;

    for (i = 0; i < g_pGDL->numDrawEntries; i++) {
        geometry = GETP(g_pGDL->drawEntries[i].geometry);
        matrix = Gfx_GetWorldMatrixState(g_pGDL->drawEntries[i].matrix);
        if (g_pGDL->drawEntries[i].unk10) {
            Gfx_EnableState(0x10);
            color = g_pGDL->drawEntries[i].color;
            Gfx_SetBaseColor(&color);
        }
        m_curLightState = g_pGDL->drawEntries[i].lightState;
        m_curEnvmapState = g_pGDL->drawEntries[i].envmapState;
        Render_Geometry(geometry, matrix);
        if (g_pGDL->drawEntries[i].unk10) {
            Gfx_DisableState(0x10);
        }
    }
    for (i = 0; i < g_pGDL->numLODEntries; i++) {
        Geometry *lod = GETP(g_pGDL->lodEntries[i].geometry);
        void *lodMatrix = Gfx_GetWorldMatrixState(g_pGDL->lodEntries[i].matrix);
        Gfx_DrawGeometryLOD_Immediate(lod, lodMatrix);
    }
}

void Render_Geometry(Geometry *geom, f32 *m) {
    Vector4 *bmin = (Vector4 *)geom->bboxMin;
    Vector4 *bmax = (Vector4 *)geom->bboxMax;
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 center __attribute__((aligned(16)));
    Vector4 scale __attribute__((aligned(16)));
    Vector4 out __attribute__((aligned(16)));
    f32 radius;
    u16 i;

    if (geom->unk4 == 0) {
        PSVECAdd((Vector4 *)&m[12], bmin, &min);
        PSVECAdd((Vector4 *)&m[12], bmax, &max);
        PSVECAdd(&min, &max, &center);
        PSVECScale(&center, &center, 0.5f);
        scale.x = geom->bboxMin[3];
        scale.y = geom->bboxMin[3];
        scale.z = geom->bboxMin[3];
        scale.w = 0.0f;
        XformVecOrientation(m, &scale, &out);
        radius = _fabs(out.x);
        out.y = _fabs(out.y);
        out.z = _fabs(out.z);
        if (out.y > radius) {
            radius = out.y;
        }
        if (out.z > radius) {
            radius = out.z;
        }
        switch (Gfx_FrustrumCullSphereTest(&center, radius)) {
        case 1:
            Gfx_EnableState(0x20000);
            break;
        case 2:
            Gfx_DisableState(0x20000);
            break;
        default:
            return;
        }
        PSVECSubtract(&center, &GetRenderWindow()->cameraPos, &min);
        radius = PSVECDotProduct(&min, &GetRenderWindow()->cameraRight);
        radius *= GetRenderWindow()->sinHalfFov;
        for (i = 0; i < geom->numGroups; i++) {
            if (radius < GETP(geom->lodDistances)[i]) {
                break;
            }
        }
        if (geom->numGroups == 1 && 0.0f == GETP(geom->lodDistances)[0]) {
            i = 0;
        }
        if (i < geom->numGroups) {
            Gfx_DrawGeometryLOD_Immediate((Geometry *)((u8 *)GETP(geom->groups) + i * 0x18), (f32 (*)[4])m);
        }
        Gfx_EnableState(0x20000);
    }
}

void Gfx_DrawGeometryLOD_Immediate(Geometry *geometry, f32 (*matrix)[4]) {
    u16 i;

    if (geometry != NULL) {
        if (matrix == g_identity) {
            Gfx_SetWorldMatrix(NULL);
        } else {
            Gfx_SetWorldMatrix(matrix);
        }
        for (i = 0; i < geometry->numGroups; i++) {
            GETP(geometry->groups)[i].flags |= m_fillMode;
            RenderGroup(geometry, i, matrix);
            GETP(geometry->groups)[i].flags &= ~m_fillMode;
        }
    }
}

void Render_DrawOpaqueGroup(Geometry *geometry, u32 group, void *matrix);
void EnvMap(Geometry *geometry, u32 group, void *matrix, s32 mode);
void RenderGroup(Geometry *geometry, u32 group, f32 (*matrix)[4]) {
    if (GETP(geometry->groups)[group].flags & 0x4000) {
        return;
    }
    if (GETP(geometry->groups)[group].flags & 0x80) {
        GXColor color = g_state.constColor;
        DisplayList_AddAlphaGroup((s32)HND(geometry), group, 0, (g_state.flags >> 4) & 1, &color);
    } else if (GETP(geometry->groups)[group].flags & 1) {
        GXColor color = g_state.constColor;
        DisplayList_AddAlphaGroup((s32)HND(geometry), group, 0, (g_state.flags >> 4) & 1, &color);
    } else if (GETP(geometry->groups)[group].flags & 2) {
        EnvMap(geometry, group, matrix, 1);
    } else if (GETP(geometry->groups)[group].flags & 0x100) {
        DrawGroupWireframe(geometry, group, matrix);
    } else {
        RenderOpaqueGroup(geometry, group, matrix);
    }
}

extern u32 g_IsAnim;

void EnvMap(Geometry *geometry, u32 group, void *matrix, s32 mode) {
    u32 opaque;
    GeometryGroup *grp;
    void *dl;
    GXColor color;
    f32 mtx[3][4];
    s32 blend;

    opaque = mode & 1;
    grp = &GETP(geometry->groups)[group];
    dl = GETP(grp->displayList);
    color.r = grp->color.r;
    color.g = grp->color.g;
    color.b = grp->color.b;
    color.a = grp->color.a;
    if (!opaque) {
        Render_SetTextureHandle(g_pGDL->envmapStates[m_curEnvmapState].unk8);
        blend = 5;
        if (mode & 2) {
            blend = 2;
        }
        Render_StateAlphaBlendEnable(1);
        Render_StateBlendMode(blend);
        MatrixToMtxTranspose(mtx, GetRenderWindow()->worldView);
        color.r = grp->color.r >> 2;
        color.g = grp->color.g >> 2;
        color.b = grp->color.b >> 2;
    } else {
        Render_SetTextureHandle(g_pGDL->envmapStates[m_curEnvmapState].unkA);
        MatrixToMtxTranspose(mtx, GetRenderWindow()->worldView);
    }
    mtx[0][0] *= 0.5f;
    mtx[0][1] *= 0.5f;
    mtx[0][2] *= 0.5f;
    mtx[0][3] = 0.5f;
    mtx[1][0] *= 0.5f;
    mtx[1][1] *= 0.5f;
    mtx[1][2] *= 0.5f;
    mtx[1][3] = 0.5f;
    GXLoadTexMtxImm(mtx, 30, 1);
    GXSetTexCoordGen2(0, 1, 1, 30, 0, 125);
    Render_SetConstColor(&color);
    SetTriEnvVtxDescIdx();
    if (g_IsAnim) {
        GXSetVtxDesc(0, 1);
        m_VtxDescFlag = 0;
    }
    SetVertexBuffer((u32 *)GETP(geometry->lodDistances), grp->flags, geometry->unk4);
    DCFlushRange(dl, grp->dlSize);
    GXCallDisplayList(dl, grp->dlSize);
    Render_SetConstColor(NULL);
    GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
    if (!opaque) {
        Render_StateAlphaBlendEnable(0);
    }
}

void Render_DrawOpaqueGroup(Geometry *geometry, u32 group, void *matrix) {
    GeometryGroup *grp;
    f32 *vb;
    void *dl;
    UVScroll *scroll;
    f32 mtx[3][4];

    grp = &GETP(geometry->groups)[group];
    vb = GETP(geometry->lodDistances);
    dl = GETP(grp->displayList);
    if (grp->unk18 == 0) {
        Render_SetTextureHandle(grp->unk1A);
    } else {
        Render_SetTextureHandle(grp->unk18);
    }
    g_debugTriCount += grp->dlSize;
    if ((g_state.flags & 0x10) || (g_state.flags & 0x40000)) {
        g_state.constColor.a = 0xFF;
        Render_SetConstColor(&g_state.constColor);
        SetTriVtxDescIdx();
        SetVertexBuffer((u32 *)vb, grp->flags, geometry->unk4);
        DCFlushRange(dl, grp->dlSize);
        GXCallDisplayList(dl, grp->dlSize);
        Render_SetConstColor(NULL);
    } else if (grp->flags & 4) {
        Render_UpdateLightState(m_curLightState);
        Render_UseLighting(1);
        if (GETP(grp->uvScroll) != NULL) {
            mtx[0][0] = 1.0f;
            mtx[0][1] = 0.0f;
            mtx[0][2] = 0.0f;
            mtx[0][3] = GETP(grp->uvScroll)->u;
            mtx[1][0] = 0.0f;
            mtx[1][1] = 1.0f;
            mtx[1][2] = 0.0f;
            mtx[1][3] = GETP(grp->uvScroll)->v;
            GXLoadTexMtxImm(mtx, 30, 1);
            GXSetTexCoordGen2(0, 1, 4, 30, 0, 125);
        }
        SetTriNormVtxDescIdx();
        if (g_IsAnim) {
            GXSetVtxDesc(0, 1);
            m_VtxDescFlag = 0;
        }
        SetVertexBuffer((u32 *)vb, grp->flags, geometry->unk4);
        DCFlushRange(dl, grp->dlSize);
        GXCallDisplayList(dl, grp->dlSize);
        if (GETP(grp->uvScroll) != NULL) {
            GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
        }
        Render_UseLighting(0);
    } else {
        scroll = GETP(grp->uvScroll);
        if (scroll != NULL) {
            f32 mtx2[3][4];
            mtx2[0][0] = 1.0f;
            mtx2[0][1] = 0.0f;
            mtx2[0][2] = 0.0f;
            mtx2[0][3] = scroll->u;
            mtx2[1][0] = 0.0f;
            mtx2[1][1] = 1.0f;
            mtx2[1][2] = 0.0f;
            mtx2[1][3] = scroll->v;
            GXLoadTexMtxImm(mtx2, 30, 1);
            GXSetTexCoordGen2(0, 1, 4, 30, 0, 125);
            if (scroll->flags & 2) {
                Render_SetConstColor(&scroll->color);
            }
            SetTriVtxDescIdx();
            SetVertexBuffer((u32 *)vb, grp->flags, geometry->unk4);
            DCFlushRange(dl, grp->dlSize);
            GXCallDisplayList(dl, grp->dlSize);
            if (scroll->flags & 2) {
                Render_SetConstColor(NULL);
            }
            GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
        } else {
            SetTriVtxDescIdx();
            SetVertexBuffer((u32 *)vb, grp->flags, geometry->unk4);
            DCFlushRange(dl, grp->dlSize);
            GXCallDisplayList(dl, grp->dlSize);
        }
    }
}


void Render_DrawAlphaGroup(u16 index) {
    GeometryGroup *grp;
    f32 *vb;
    GDLAlphaGroup *ag;
    UVScroll *scroll;
    void *dl;
    f32 mtx[3][4];
    f32 tmtx[3][4];

    if (g_pGDL->alphaGroups[index].window != g_currentWindow) {
        return;
    }
    ag = &g_pGDL->alphaGroups[index];
    vb = GETP(GETP(ag->geometry)->lodDistances);
    grp = &GETP(GETP(ag->geometry)->groups)[g_pGDL->alphaGroups[index].group];
    if (grp->unk18 == 0) {
        Render_SetTextureHandle(grp->unk1A);
    } else {
        Render_SetTextureHandle(grp->unk18);
    }
    if (!g_onebitflag && m_texture[m_curTexture].unk38 == 4 && grp->unk14 == 5) {
        Gfx_SetWorldMatrix(NULL);
        return;
    }
    if (g_onebitflag) {
        if (m_texture[m_curTexture].unk38 != 4 || grp->unk14 != 5) {
            Gfx_SetWorldMatrix(NULL);
            return;
        }
    } else if (m_texture[m_curTexture].unk38 == 4 && grp->unk14 == 5) {
        Gfx_SetWorldMatrix(NULL);
        return;
    }
    if (m_texture[m_curTexture].unk38 == 4 && grp->unk14 == 5) {
        GXSetAlphaCompare(6, 0x80, 0, 6, 0);
        GXSetZCompLoc(0);
        Render_StateZWriteEnable(1);
    }
    Render_StateBlendMode(grp->unk14);
    MatrixToMtxTranspose(mtx, ag->matrix);
    GXLoadPosMtxImm(mtx, 0);
    GXSetCurrentMtx(0);
    g_state.unk93 = 0;
    scroll = GETP(grp->uvScroll);
    dl = GETP(grp->displayList);
    if (scroll != NULL) {
        tmtx[0][0] = 1.0f;
        tmtx[0][1] = 0.0f;
        tmtx[0][2] = 0.0f;
        tmtx[0][3] = scroll->u;
        tmtx[1][0] = 0.0f;
        tmtx[1][1] = 1.0f;
        tmtx[1][2] = 0.0f;
        tmtx[1][3] = scroll->v;
        GXLoadTexMtxImm(tmtx, 30, 1);
        GXSetTexCoordGen2(0, 1, 4, 30, 0, 125);
        if (scroll->flags & 2) {
            Render_SetConstColor(&scroll->color);
        } else if (ag->unk50) {
            Render_SetConstColor(&ag->color);
        }
        SetVertexBuffer((u32 *)vb, GETP(GETP(ag->geometry)->groups)->flags, GETP(ag->geometry)->unk4);
        SetTriVtxDescIdxAlpha();
        DCFlushRange(dl, grp->dlSize);
        GXCallDisplayList(dl, grp->dlSize);
        if ((scroll->flags & 2) || ag->unk50) {
            Render_SetConstColor(NULL);
        }
        GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
    } else if (ag->unk50) {
        Render_SetConstColor(&ag->color);
        SetVertexBuffer((u32 *)vb, GETP(GETP(ag->geometry)->groups)->flags, GETP(ag->geometry)->unk4);
        SetTriVtxDescIdxAlpha();
        DCFlushRange(dl, grp->dlSize);
        GXCallDisplayList(dl, grp->dlSize);
        Render_SetConstColor(NULL);
    } else {
        SetVertexBuffer((u32 *)vb, GETP(GETP(ag->geometry)->groups)->flags, GETP(ag->geometry)->unk4);
        SetTriVtxDescIdxAlpha();
        DCFlushRange(dl, grp->dlSize);
        GXCallDisplayList(dl, grp->dlSize);
    }
    if (m_texture[m_curTexture].unk38 == 4 && grp->unk14 == 5) {
        GXSetAlphaCompare(7, 0, 1, 7, 0);
        GXSetZCompLoc(1);
        Render_StateZWriteEnable(0);
    }
    Gfx_SetWorldMatrix(NULL);
}


void Render_UseLighting(u8 enable) {
    if (enable) {
        GXSetChanCtrl(4, 1, 0, 0, m_pointLightMask | 1, 2, 2);
    } else {
        GXSetChanCtrl(4, 0, 1, 1, 0, 0, 2);
    }
}

void Render_UpdateLightState(s32 index) {
    u8 lightObj[0x40];
    u8 pointObj[0x40];
    Vector4 dir;
    GXColor ambient;
    u8 i;
    GDLLightState *ls;

    if (index == 0xFFFFFFFF) {
        index = 0;
    }
    ls = &g_pGDL->lightStates[index];
    Render_InitDirLight(lightObj, &dir, &ls->lightDir, &ls->lightColor);
    ambient.r = ls->ambientColor.r;
    ambient.g = ls->ambientColor.g;
    ambient.b = ls->ambientColor.b;
    ambient.a = 0xFF;
    GXSetChanAmbColor(4, ambient);
    m_pointLightMask = 0;
    m_totalLights = 0;
    for (i = 0; i < ls->numPointLights; i++) {
        if (m_totalLights <= 4) {
            m_totalLights++;
            Render_InitPointLight(pointObj, &ls->pointLights[i], &ls->pointColors[i]);
            m_pointLightMask |= 1 << m_totalLights;
        }
    }
}

void Render_DrawSpheres(void) {
    u32 i;
    s32 cullMode;
    GDLMatrix mtx;

    cullMode = m_cullMode;
    Render_SetTextureHandle(0xFFFF);
    Render_StateCullMode(2);
    mtx = *(GDLMatrix *)g_identity;
    for (i = 0; i < g_pGDL->numSpheres; i++) {
        mtx.m[12] = g_pGDL->spheres[i].x;
        mtx.m[13] = g_pGDL->spheres[i].y;
        mtx.m[14] = g_pGDL->spheres[i].z;
        mtx.m[0] = mtx.m[5] = mtx.m[10] = g_pGDL->spheres[i].radius;
        Gfx_SetWorldMatrix((f32 (*)[4])mtx.m);
        GXDrawSphere(8, 16);
    }
    Render_StateCullMode(cullMode);
}


void Render_DrawBoundingBoxes(void) {
    u32 i;
    s32 cullMode;
    Vector4 v[8] __attribute__((aligned(16)));

    cullMode = m_cullMode;
    Render_StateCullMode(0);
    for (i = 0; i < g_pGDL->numBoxes; i++) {
        v[0] = v[1] = v[2] = v[3] = g_pGDL->boxes[i].min;
        v[1].x = g_pGDL->boxes[i].max.x;
        v[2].x = g_pGDL->boxes[i].max.x;
        v[2].z = g_pGDL->boxes[i].max.z;
        v[3].z = g_pGDL->boxes[i].max.z;
        v[4] = v[5] = v[6] = v[7] = g_pGDL->boxes[i].max;
        v[5].x = g_pGDL->boxes[i].min.x;
        v[6].x = g_pGDL->boxes[i].min.x;
        v[6].z = g_pGDL->boxes[i].min.z;
        v[7].z = g_pGDL->boxes[i].min.z;
        SetTriFlatVtxDesc();
        GXBegin(0x80, 0, 24);
        GXPosition3f32(v[0].x, v[0].y, v[0].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[1].x, v[1].y, v[1].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[2].x, v[2].y, v[2].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[3].x, v[3].y, v[3].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);

        GXPosition3f32(v[4].x, v[4].y, v[4].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[5].x, v[5].y, v[5].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[6].x, v[6].y, v[6].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[7].x, v[7].y, v[7].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);

        GXPosition3f32(v[0].x, v[0].y, v[0].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[6].x, v[6].y, v[6].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[7].x, v[7].y, v[7].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[1].x, v[1].y, v[1].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);

        GXPosition3f32(v[3].x, v[3].y, v[3].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[5].x, v[5].y, v[5].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[4].x, v[4].y, v[4].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[2].x, v[2].y, v[2].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);

        GXPosition3f32(v[0].x, v[0].y, v[0].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[6].x, v[6].y, v[6].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[5].x, v[5].y, v[5].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[3].x, v[3].y, v[3].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);

        GXPosition3f32(v[2].x, v[2].y, v[2].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[4].x, v[4].y, v[4].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[7].x, v[7].y, v[7].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
        GXPosition3f32(v[1].x, v[1].y, v[1].z);
        GXColor4u8(g_pGDL->boxes[i].r, g_pGDL->boxes[i].g, g_pGDL->boxes[i].b, g_pGDL->boxes[i].a);
    }
    Render_StateCullMode(cullMode);
    m_triCount += 12;
}

void SetVertexBuffer(u32 *vb, u16 flags, u16 unk) {
    u8 *data;

    if (vb != m_vertexBuffer) {
        data = (u8 *)(vb + 4);
        m_vertexBuffer = vb;
        GXSetArray(9, data, 0xC);
        GXSetArray(0xD, data + vb[0], 8);
        if (vb[1] != 0) {
            GXSetArray(0xB, data + vb[1], 4);
        }
        if (vb[2] != 0) {
            GXSetArray(0xA, data + vb[2], 0xC);
        }
        DCFlushRange(vb, vb[3]);
    }
}

void SetTriVtxDescIdx(void) {
    if (m_VtxDescFlag != 0x100) {
        m_VtxDescFlag = 0x100;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 3);
        GXSetVtxDesc(0xB, 3);
        GXSetVtxDesc(0xD, 3);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 0, 1, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
    }
}

void SetTriVtxDescIdxAlpha(void) {
    if (m_VtxDescFlag != 0x400) {
        m_VtxDescFlag = 0x400;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 3);
        GXSetVtxDesc(0xB, 3);
        GXSetVtxDesc(0xD, 3);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
    }
}

void SetTriNormVtxDescIdx(void) {
    if (m_VtxDescFlag != 0x200) {
        m_VtxDescFlag = 0x200;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 3);
        GXSetVtxDesc(0xA, 3);
        GXSetVtxDesc(0xD, 3);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xA, 0, 4, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
    }
}

void SetTriEnvVtxDescIdx(void) {
    if (m_VtxDescFlag != 0x800) {
        m_VtxDescFlag = 0x800;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 3);
        GXSetVtxDesc(0xA, 3);
        GXSetVtxDesc(0xB, 3);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xA, 0, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
    }
}

void SetTriVtxDesc(void) {
    if (m_VtxDescFlag != 8) {
        m_VtxDescFlag = 8;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxDesc(0xD, 1);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
    }
}

void Render_SetConstColor(GXColor *color) {
    if (color != NULL) {
        GXSetChanMatColor(4, *color);
        GXSetChanAmbColor(4, white);
        GXSetChanCtrl(4, 1, 0, 0, 1, 2, 2);
    } else {
        GXSetChanCtrl(4, 0, 0, 1, 0, 0, 2);
        GXSetChanAmbColor(4, black);
        GXSetChanMatColor(4, white);
    }
}

