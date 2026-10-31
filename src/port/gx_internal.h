/* State shared by the GX emulation (gx.c API, gx_fifo.c vertex decoding,
 * gx_tex.c textures, gx_draw.c OpenGL). */
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

    /* textures: decoded GL names bound per map */
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

/* One decoded vertex, as uploaded to GL. */
typedef struct GXVertex {
    f32 pos[3];
    f32 nrm[3];
    f32 clr[2][4];
    f32 tex[8][2];
    f32 mtxIdx;          /* position matrix slot */
} GXVertex;

/* gx_fifo.c */
void GX_FifoBegin(int prim, int vtxfmt, u16 nverts);
void GX_FifoWrite(const void *bytes, u32 n);
void GX_RunCommands(const u8 *cmds, u32 size);

/* gx_draw.c */
void GX_DrawInit(void);
void GX_Draw(int prim, const GXVertex *v, u32 n);
void GX_DrawBeginFrame(void);
void GX_DrawCopyDisp(int clear);
void GX_DrawPresent(void);

/* gx_tex.c */
GLuint GX_TexGet(const GXTexObj *obj);

#endif
