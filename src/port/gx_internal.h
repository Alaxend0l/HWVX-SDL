/* State shared by the GX emulation (gx.c API, gx_fifo.c vertex decoding,
 * gx_tex.c textures, gx_draw.c rendering). */
#ifndef GX_INTERNAL_H
#define GX_INTERNAL_H

#include "port_internal.h"

#define GX_NUM_ATTR 21        /* PNMTXIDX .. TEX7 */
#define GX_NUM_VTXFMT 8
#define GX_NUM_POSMTX 10      /* ids 0, 3, ... 27 */
/* Slot after the loadable ones: always identity. Position matrix id 60
 * (GX_IDENTITY) reads XF rows 60-62, which GXInit fills with identity; the
 * billboard sprites (Render_DrawNewBBSprites) draw with it. */
#define GX_POSMTX_IDENTITY GX_NUM_POSMTX
#define GX_NUM_TEXMTX 10      /* ids 30, 33, ... 57 */
#define GX_NUM_LIGHTS 8

typedef struct GXVtxAttrFmt {
    u8 cnt, type, frac;
} GXVtxAttrFmt;

typedef struct GXLightState {
    f32 pos[4], dir[4], color[4], a[4], k[4];
} GXLightState;

typedef struct GXTevStage {
    u8 colorIn[4], alphaIn[4];
    u8 colorOp, colorBias, colorScale, colorClamp, colorOut;
    u8 alphaOp, alphaBias, alphaScale, alphaClamp, alphaOut;
    u8 texCoord, texMap, channel;
} GXTevStage;

typedef struct GXTexGen {
    u8 func, src;
    u32 mtx;
} GXTexGen;

typedef struct GXChan {
    u8 enable, ambSrc, matSrc, diffFn, attnFn;
    u32 lightMask;
} GXChan;

typedef struct GXState {
    /* vertex format */
    u8 vcd[GX_NUM_ATTR];
    GXVtxAttrFmt vat[GX_NUM_VTXFMT][GX_NUM_ATTR];
    const u8 *array[GX_NUM_ATTR];
    u8 arrayStride[GX_NUM_ATTR];

    /* transform */
    f32 posMtx[GX_NUM_POSMTX + 1][3][4];
    f32 texMtx[GX_NUM_TEXMTX][3][4];
    u32 curMtx;
    f32 proj[4][4];
    int projType;
    f32 viewport[6];
    u32 scissor[4];
    u8 numTexGens;
    GXTexGen texGen[8];

    /* lighting: COLOR0, COLOR1, ALPHA0, ALPHA1 */
    u8 numChans;
    GXChan chan[4];
    GXColor ambColor[2], matColor[2];
    GXLightState light[GX_NUM_LIGHTS];

    /* textures bound per map */
    GXTexObj texObj[8];
    u8 texLoaded[8];

    /* TEV */
    u8 numTevStages;
    GXTevStage tev[16];
    GXColor tevReg[4];   /* PREV, REG0, REG1, REG2 */
    u8 alphaComp[5];     /* comp0, ref0, op, comp1, ref1 */

    /* pixel engine */
    u8 blendType, blendSrc, blendDst, blendOp;
    u8 zEnable, zFunc, zUpdate;
    u8 colorUpdate, alphaUpdate;
    u8 cullMode;
    u8 pointSize;
    u8 fogType;
    f32 fogStart, fogEnd, fogNear, fogFar;
    GXColor fogColor;

    GXColor clearColor;
    u32 clearZ;
    u16 dispCopySrc[4];   /* GXSetDispCopySrc: left, top, width, height (0 width: whole EFB) */
} GXState;

extern GXState g_gx;

/* One decoded vertex, as uploaded to the renderer. Deliberately compact:
 * this record streams through the vertex pool, the upload buffers and the
 * vertex fetch on every draw. Normals keep the hardware's own encoding (s8
 * at fixed scale 1/64, w unused; the vertex shader multiplies by 127/64),
 * and colors are 8-bit unsigned normalized, expanded bit-for-bit the way
 * the GameCube color registers expand them, so no precision is lost that
 * the pixel pipeline ever had. 92 bytes, down from 124. */
typedef struct GXVertex {
    f32 pos[3];
    s8 nrm[4];           /* fixed scale 1/64 */
    u8 clr[2][4];        /* normalized [0,1] on the GPU */
    f32 tex[8][2];
    f32 mtxIdx;          /* position matrix slot */
} GXVertex;

/* gx.c: bumped whenever the vertex format (GXSetVtxDesc/GXSetVtxAttrFmt) or
 * the vertex arrays (GXSetArray) change; lets the display-list cache skip
 * recomputing its state key. */
extern u32 g_gxVtxGen;

/* gx_fifo.c */
void GX_FifoBegin(int prim, int vtxfmt, u16 nverts);
void GX_FifoWrite(const void *bytes, u32 n);
void GX_RunCommands(const u8 *cmds, u32 size);

/* Decoded display lists are kept in one persistent vertex pool: the same list
 * decodes once and later calls replay it, so a renderer can hold the pool in a
 * single GPU buffer and upload only what is new. A batch is that pool. */
typedef struct GXVtxBatch {
    const GXVertex *base;     /* the pool */
    u32 used;                 /* vertices written since the current generation */
    u32 cap;                  /* capacity of base, in vertices */
    u32 gen;                  /* changes when the pool is reused from the start */
} GXVtxBatch;

/* The pool, when the vertices of a draw (v, n) lie inside it; NULL otherwise. */
const GXVtxBatch *GX_FifoBatch(const GXVertex *v, u32 n);
/* 1 if the shared vertex pool can hold `need` vertices (renderers size their
 * upload buffers from this), and how many it currently holds. */
u32 GX_FifoPoolCapacity(u32 need);
u32 GX_FifoPoolUsed(void);

/* Cache counters for the frame dump log: list hits, lists decoded and kept,
 * entries dropped for holding a bad decode, captures that did not fit, and
 * whole-pool resets. */
void GX_FifoStats(u32 *hit, u32 *miss, u32 *drop, u32 *abort, u32 *flush);
/* Miss forensics, same windowed-delta style: list addresses seen for the
 * first time, addresses whose bytes were rewritten, addresses where bytes and
 * state repeated but no entry answered, diffs in vertex fmt / array pointers /
 * matrix, and captures abandoned as volatile. */
void GX_FifoDiag(u32 *first, u32 *rebuild, u32 *stable, u32 *fmt, u32 *arr, u32 *mtx,
                 u32 *skip);

/* gx_draw.c */
void GX_DrawInit(void);
void GX_Draw(int prim, const GXVertex *v, u32 n);
void GX_DrawBeginFrame(void);
void GX_DrawCopyDisp(int clear);
void GX_DrawCopyTex(void *dest, int clear);
void GX_DrawPresent(void);

/* gx_tex.c: GX_TexGet is in gx_render.h */

#endif
