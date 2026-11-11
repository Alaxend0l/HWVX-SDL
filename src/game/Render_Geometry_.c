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


typedef struct UVScroll {
    f32 u;
    f32 v;
    GXColor color;
    u32 flags;
} UVScroll;


typedef struct Texture {
    u8 name[0x24];              /* 0x00 texture file name, set on load */
    PTR32(void) data;
    u32 unused28;               /* 0x28 copied from pack header, never read */
    f32 unused2C;               /* 0x2C set to 0.25f, never read */
    u16 texWidth;               /* 0x30 GXInitTexObj width (BTF stores height) */
    u16 width;
    u16 height;
    s16 refCount;
    u16 format;                 /* 0x38 4: CMPR, 1: RGBA8, 0/2: RGB565 */
    u16 fromPack;               /* 0x3A 1: pack texture, data not freed */
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
    u8 pad48[0x4C - 0x48];
    GXColor color;
    u8 useBaseColor;
    u8 window;
    u16 next;
} GDLAlphaGroup;

extern s32 g_debugTriCount;


typedef struct GDLSphere {
    f32 x;
    f32 y;
    f32 z;
    u8 padC[0x14 - 0xC];
    f32 radius;
    u8 pad18[0x20 - 0x18];
} GDLSphere;

typedef struct GDLDrawEntry {
    PTR32(void) geometry;
    GXColor color;
    u32 matrix;
    s32 lightState;
    u8 useBaseColor;
    u8 unused11;
    u8 envmapState;
    u8 pad13;
} GDLDrawEntry;

typedef struct GDLLODEntry {
    PTR32(void) geometry;
    u32 matrix;
} GDLLODEntry;

typedef struct GDLEnvmapState {
    f32 x;                    /* 0x00 Gfx_SetEnvmap position, dedup compare only */
    f32 y;
    u16 texA;                 /* 0x08 texture pair, Render_SetTextureHandle */
    u16 texB;
} GDLEnvmapState;

typedef struct GDLBox {
    Vector4 min;
    Vector4 max;
    u8 pad20[0x30 - 0x20];
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    u8 pad34[0x50 - 0x34];
} GDLBox;

typedef struct GDLLightState {
    Vector4 pointLights[3];
    Vector4 lightDir;
    u32 numPointLights;
    GXColor pointColors[3];
    GXColor lightColor;
    GXColor ambientColor;
    u8 pad58[0x60 - 0x58];
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
    u8 pad0[0x2C];
    u32 numSpheres;
    u32 numBoxes;
    u8 pad34[0x54 - 0x34];
    u32 numDrawEntries;
    u32 numLODEntries;
    u8 pad5C[0x69A00 - 0x5C];
    GDLAlphaGroup alphaGroups[0x400];
    u8 pad7EA00[0xBC800 - 0x7EA00];
    GDLSphere spheres[200];
    u8 padBE100[0xBE110 - 0xBE100];
    GDLBox boxes[100];
    u8 padC0050[0xC6EC0 - 0xC0050];
    GDLLightState lightStates[0x80];
    u8 padC9EC0[0xDFCC0 - 0xC9EC0];
    GDLDrawEntry drawEntries[0x800];
    GDLLODEntry lodEntries[0x400];
    u8 padEBCC0[0xF3A80 - 0xEBCC0];
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
    u16 next;                 /* 0x0A next group in the texture bucket */
    u8 group;
    u8 lod;                   /* 0x0D LOD picked by Gfx_GetCullLODGX */
    u8 blendMode;             /* 0x0E see Render_StateBlendMode */
    u8 window;
} DLGroup;

typedef struct DisplayList {
    u16 numOpaqueGroups;
    u16 numLitGroups;
    u16 numShadowGroups;
    u16 numAlphaGroups;
    u8 pad8[0x10 - 0x8];
    u16 numSkinGroups;
    u8 pad12[0x14 - 0x12];
    u16 numDecalGroups;
    u16 numOverlayGroups;
    u8 pad18[0x20 - 0x18];
    DLGroup opaqueGroups[0x9C4];
    DLGroup litGroups[0x258];
    DLGroup shadowGroups[0x100];
    DLGroup alphaGroups[0x100];
    DLGroup decalGroups[0x200];
    DLGroup overlayGroups[0x80];
    DLSkinGroup skinGroups[0x100];
    u16 opaqueTexHeads[0x1FF];  /* 0x11DE0 opaqueGroups chain heads */
    u16 litTexHeads[0x1FF];     /* 0x121DE litGroups chain heads */
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
        if (g_pGDL->drawEntries[i].useBaseColor) {
            Gfx_EnableState(0x10);
            color = g_pGDL->drawEntries[i].color;
            Gfx_SetBaseColor(&color);
        }
        m_curLightState = g_pGDL->drawEntries[i].lightState;
        m_curEnvmapState = g_pGDL->drawEntries[i].envmapState;
        Render_Geometry(geometry, matrix);
        if (g_pGDL->drawEntries[i].useBaseColor) {
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

    if (geom->skipDraw == 0) {
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
        GXColor color = g_state.baseColor;
        DisplayList_AddAlphaGroup((s32)HND(geometry), group, 0, (g_state.flags >> 4) & 1, &color);
    } else if (GETP(geometry->groups)[group].flags & 1) {
        GXColor color = g_state.baseColor;
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

/* port: the vertex setups a group is drawn with (SetTri*VtxDesc*). */
enum { GROUP_VTX_TRI, GROUP_VTX_ALPHA, GROUP_VTX_NORM, GROUP_VTX_ENV };

/* port: the vertex buffer of a PORT_LAYOUT_INDEXED level (LOD 0xC), the PC
 * release's: pointers to its arrays (strides 16, 4, 8, 16; positions and
 * normals x,y,z and a pad word), 16 zero bytes, then the arrays. Colours or
 * normals are NULL when the level has none. */
typedef struct PortPCVertexBuffer {
    PTR32(u8) pos;
    PTR32(u8) clr;
    PTR32(u8) uv;
    PTR32(u8) nrm;
} PortPCVertexBuffer;

/* port: the attributes each vertex setup reads (SetTri*VtxDesc*): positions
 * always; RGB colour and UVs; RGBA colour and UVs; normals and UVs; normals
 * and RGBA colour (the texture coordinates are generated from the normal). */
static const u32 s_groupUse[] = {
    [GROUP_VTX_TRI] = GX_PORT_USE_CLR | GX_PORT_USE_UV,
    [GROUP_VTX_ALPHA] = GX_PORT_USE_CLR | GX_PORT_CLR_ALPHA | GX_PORT_USE_UV,
    [GROUP_VTX_NORM] = GX_PORT_USE_NRM | GX_PORT_USE_UV,
    [GROUP_VTX_ENV] = GX_PORT_USE_NRM | GX_PORT_USE_CLR | GX_PORT_CLR_ALPHA,
};

/* port: a PORT_LAYOUT_INDEXED group: u16 indices into the level's arrays,
 * a triangle list of 3 * grp->dlSize, or, when the first index has bit 15
 * set, strip records {u16 count (bit 15 on the first), count indices} up to
 * 0xFFFF (the PC release's two draw modes). A skinned level's vertices are
 * sorted by bone, with the bones' counts at LOD 0x10 (bone k: matrix slot k). */
static void Render_DrawGroupIndexed(Geometry *lod, GeometryGroup *grp, int vtx) {
    PortPCVertexBuffer *vb = (PortPCVertexBuffer *)GETP(lod->lodDistances);
    const u16 *idx = (const u16 *)GETP(grp->displayList);
    GXPortMesh mesh;
    u32 use = s_groupUse[vtx];

    if (vb == NULL || idx == NULL) {
        return;
    }
    mesh.pos = GETP(vb->pos);
    mesh.clr = GETP(vb->clr);
    mesh.uv = GETP(vb->uv);
    mesh.nrm = GETP(vb->nrm);
    mesh.posStride = 16;
    mesh.clrStride = 4;
    mesh.uvStride = 8;
    mesh.nrmStride = 16;
    mesh.boneCounts = NULL;
    mesh.numBones = 0;
    if (g_IsAnim && (vtx == GROUP_VTX_NORM || vtx == GROUP_VTX_ENV)) {
        /* the PC's table: four counts, then zero words; a converted GameCube
         * level's (Port_Geometry.c): eight counts */
        mesh.boneCounts = SLOT_LOAD(const u32 *, &lod->bboxMin[0]);
        mesh.numBones = mesh.boneCounts != NULL ? 8 : 0;
        use |= GX_PORT_SKIN;
    }
    if (!(idx[0] & 0x8000)) {
        GX_PortDrawIndexed(&mesh, GX_TRIANGLES, idx, grp->dlSize * 3, use);
        return;
    }
    for (;;) {
        u32 n = idx[0] & 0x7FFF;

        if (idx[0] == 0xFFFF) {
            break;
        }
        GX_PortDrawIndexed(&mesh, GX_TRIANGLESTRIP, idx + 1, n, use);
        idx += 1 + n;
    }
}

/* port: a group's primitives, the one place pack geometry is drawn. Every
 * level is in the indexed layout by now: the PC release's as it comes, the
 * GameCube's converted as its pack loaded (Port_Geometry.c; every level of
 * the game converts). This replaced the GameCube's GX path here (vertex
 * setup, SetVertexBuffer, GXCallDisplayList of the group's list), which drew
 * the same frames byte for byte. Each caller sets the texture, lighting and
 * blending around it. */
static void Render_DrawGroupPrims(Geometry *lod, GeometryGroup *grp, int vtx) {
    static int warned;

    if (lod->portLayout == PORT_LAYOUT_INDEXED) {
        Render_DrawGroupIndexed(lod, grp, vtx);
    } else if (!warned) {
        warned = 1;
        Port_Log("geometry: a level in no known layout (not converted?) is not drawn");
    }
}

void EnvMap(Geometry *geometry, u32 group, void *matrix, s32 mode) {
    u32 opaque;
    GeometryGroup *grp;
    GXColor color;
    f32 mtx[3][4];
    s32 blend;

    opaque = mode & 1;
    grp = &GETP(geometry->groups)[group];
    color.r = grp->color.r;
    color.g = grp->color.g;
    color.b = grp->color.b;
    color.a = grp->color.a;
    if (!opaque) {
        Render_SetTextureHandle(g_pGDL->envmapStates[m_curEnvmapState].texA);
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
        Render_SetTextureHandle(g_pGDL->envmapStates[m_curEnvmapState].texB);
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
    Render_DrawGroupPrims(geometry, grp, GROUP_VTX_ENV);
    Render_SetConstColor(NULL);
    GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
    if (!opaque) {
        Render_StateAlphaBlendEnable(0);
    }
}

void Render_DrawOpaqueGroup(Geometry *geometry, u32 group, void *matrix) {
    GeometryGroup *grp;
    UVScroll *scroll;
    f32 mtx[3][4];

    grp = &GETP(geometry->groups)[group];
    if (grp->textureOverride == 0) {
        Render_SetTextureHandle(grp->texture);
    } else {
        Render_SetTextureHandle(grp->textureOverride);
    }
    g_debugTriCount += grp->dlSize;
    if ((g_state.flags & 0x10) || (g_state.flags & 0x40000)) {
        g_state.baseColor.a = 0xFF;
        Render_SetConstColor(&g_state.baseColor);
        Render_DrawGroupPrims(geometry, grp, GROUP_VTX_TRI);
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
        Render_DrawGroupPrims(geometry, grp, GROUP_VTX_NORM);
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
            Render_DrawGroupPrims(geometry, grp, GROUP_VTX_TRI);
            if (scroll->flags & 2) {
                Render_SetConstColor(NULL);
            }
            GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
        } else {
            Render_DrawGroupPrims(geometry, grp, GROUP_VTX_TRI);
        }
    }
}


void Render_DrawAlphaGroup(u16 index) {
    GeometryGroup *grp;
    GDLAlphaGroup *ag;
    UVScroll *scroll;
    f32 mtx[3][4];
    f32 tmtx[3][4];

    if (g_pGDL->alphaGroups[index].window != g_currentWindow) {
        return;
    }
    ag = &g_pGDL->alphaGroups[index];
    grp = &GETP(GETP(ag->geometry)->groups)[g_pGDL->alphaGroups[index].group];
    if (grp->textureOverride == 0) {
        Render_SetTextureHandle(grp->texture);
    } else {
        Render_SetTextureHandle(grp->textureOverride);
    }
    if (!g_onebitflag && m_texture[m_curTexture].format == 4 && grp->blendMode == 5) {
        Gfx_SetWorldMatrix(NULL);
        return;
    }
    if (g_onebitflag) {
        if (m_texture[m_curTexture].format != 4 || grp->blendMode != 5) {
            Gfx_SetWorldMatrix(NULL);
            return;
        }
    } else if (m_texture[m_curTexture].format == 4 && grp->blendMode == 5) {
        Gfx_SetWorldMatrix(NULL);
        return;
    }
    if (m_texture[m_curTexture].format == 4 && grp->blendMode == 5) {
        GXSetAlphaCompare(6, 0x80, 0, 6, 0);
        GXSetZCompLoc(0);
        Render_StateZWriteEnable(1);
    }
    Render_StateBlendMode(grp->blendMode);
    MatrixToMtxTranspose(mtx, ag->matrix);
    GXLoadPosMtxImm(mtx, 0);
    GXSetCurrentMtx(0);
    g_state.worldIsIdentity = 0;
    scroll = GETP(grp->uvScroll);
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
        } else if (ag->useBaseColor) {
            Render_SetConstColor(&ag->color);
        }
        Render_DrawGroupPrims(GETP(ag->geometry), grp, GROUP_VTX_ALPHA);
        if ((scroll->flags & 2) || ag->useBaseColor) {
            Render_SetConstColor(NULL);
        }
        GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
    } else if (ag->useBaseColor) {
        Render_SetConstColor(&ag->color);
        Render_DrawGroupPrims(GETP(ag->geometry), grp, GROUP_VTX_ALPHA);
        Render_SetConstColor(NULL);
    } else {
        Render_DrawGroupPrims(GETP(ag->geometry), grp, GROUP_VTX_ALPHA);
    }
    if (m_texture[m_curTexture].format == 4 && grp->blendMode == 5) {
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

