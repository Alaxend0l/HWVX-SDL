#include "common.h"

typedef union {
    u8 u8;
    u16 u16;
    u32 u32;
    s32 s32;
    f32 f32;
} PPCWGPipe;


typedef struct Line2D {
    f32 x0, y0, z0, w0; /* 0x00 */
    f32 x1, y1, z1, w1; /* 0x10 */
    u8 r, g, b, a;      /* 0x20 */
} Line2D;

typedef struct GDLVec {
    f32 x, y, z, w;
} GDLVec;

typedef struct GDL3DLine {
    GDLVec p0;      /* 0x00 */
    GDLVec p1;      /* 0x10 */
    u8 r, g, b, a;  /* 0x20 */
    u8 unk24[0x30 - 0x24];
} GDL3DLine;

typedef struct GDLColor {
    u8 r, g, b, a;
} GDLColor;

typedef struct GDLLine {
    GDLVec p0;      /* 0x00 */
    GDLVec p1;      /* 0x10 */
    GDLColor c0;    /* 0x20 */
    GDLColor c1;    /* 0x24 */
    u8 blendMode;   /* 0x28 */
    u8 unk29[0x30 - 0x29];
} GDLLine;

typedef struct GDL {
    u8 unk0[0x24];
    u32 num3DLines;
    u8 unk28[0x34 - 0x28];
    u32 numLines;
    u8 unk38[0xA2B80 - 0x38];
    GDL3DLine lines3D[2000];
    u8 unkBA280[0xC0040 - 0xBA280];
    GDLLine lines[0x100];
} GDL;

extern GDL *g_pGDL;

static inline void GXPosition3f32(f32 x, f32 y, f32 z) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
    GXWGFifo_f32(z);
}

static inline void GXPosition2f32(f32 x, f32 y) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
}

static inline void GXColor3u8(const u8 r, const u8 g, const u8 b) {
    GXWGFifo_u8(r);
    GXWGFifo_u8(g);
    GXWGFifo_u8(b);
}


extern u32 m_VtxDescFlag;


void Render_Draw3DLines(void) {
    u32 i;

    Gfx_SetWorldMatrix(NULL);
    Render_SetTextureHandle(0xFFFF);
    Render_StateAlphaBlendEnable(0);
    Set3DLineVtxDesc();
    GXBegin(0xA8, 0, g_pGDL->num3DLines * 2);
    for (i = 0; i < g_pGDL->num3DLines; i++) {
        GXPosition3f32(g_pGDL->lines3D[i].p0.x, g_pGDL->lines3D[i].p0.y, g_pGDL->lines3D[i].p0.z);
        GXColor3u8(g_pGDL->lines3D[i].r, g_pGDL->lines3D[i].g, g_pGDL->lines3D[i].b);
        GXPosition3f32(g_pGDL->lines3D[i].p1.x, g_pGDL->lines3D[i].p1.y, g_pGDL->lines3D[i].p1.z);
        GXColor3u8(g_pGDL->lines3D[i].r, g_pGDL->lines3D[i].g, g_pGDL->lines3D[i].b);
    }
}

void Render_Draw3DFXLines(void) {
    u32 i;

    Gfx_SetWorldMatrix(NULL);
    Render_SetTextureHandle(0xFFFF);
    Set3DLineVtxDesc();
    Render_StateBlendMode(g_pGDL->lines[0].blendMode);
    for (i = 0; i < g_pGDL->numLines; i++) {
        GXBegin(0xA8, 0, 2);
        GXPosition3f32(g_pGDL->lines[i].p0.x, g_pGDL->lines[i].p0.y, g_pGDL->lines[i].p0.z);
        GXColor3u8(g_pGDL->lines[i].c0.r, g_pGDL->lines[i].c0.g, g_pGDL->lines[i].c0.b);
        GXPosition3f32(g_pGDL->lines[i].p1.x, g_pGDL->lines[i].p1.y, g_pGDL->lines[i].p1.z);
        GXColor3u8(g_pGDL->lines[i].c1.r, g_pGDL->lines[i].c1.g, g_pGDL->lines[i].c1.b);
    }
}

void Render_DrawLine2D(Line2D *line) {
    Set2DLineVtxDesc();
    GXBegin(0xA8, 0, 2);
    GXPosition2f32(line->x0, line->y0);
    GXColor3u8(line->r, line->g, line->b);
    GXPosition2f32(line->x1, line->y1);
    GXColor3u8(line->r, line->g, line->b);
}

void Set2DLineVtxDesc(void) {
    if (m_VtxDescFlag != 1) {
        m_VtxDescFlag = 1;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxAttrFmt(0, 9, 0, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 0, 1, 0);
    }
}

void Set3DLineVtxDesc(void) {
    if (m_VtxDescFlag != 4) {
        m_VtxDescFlag = 4;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 0, 1, 0);
    }
}

