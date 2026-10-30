/* GX graphics API as used by the game, emulated on OpenGL 3.3 (src/port/gx_*.c).
 * Included from sdk.h. */
#ifndef GX_H
#define GX_H

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GXColor {
    u8 r, g, b, a;
} GXColor;

typedef struct GXColorS10 {
    s16 r, g, b, a;
} GXColorS10;

/* Same sizes as the SDK so game structs embedding them keep their layout. */
typedef struct GXTexObj {
    u32 dummy[8];
} GXTexObj;

typedef struct GXLightObj {
    u32 dummy[16];
} GXLightObj;

typedef struct GXFogAdjTable {
    u16 r[10];
} GXFogAdjTable;

/* Primitive types (GXBegin) */
#define GX_QUADS 0x80
#define GX_TRIANGLES 0x90
#define GX_TRIANGLESTRIP 0x98
#define GX_TRIANGLEFAN 0xA0
#define GX_LINES 0xA8
#define GX_LINESTRIP 0xB0
#define GX_POINTS 0xB8

/* Vertex attributes */
#define GX_VA_PNMTXIDX 0
#define GX_VA_TEX0MTXIDX 1
#define GX_VA_POS 9
#define GX_VA_NRM 10
#define GX_VA_CLR0 11
#define GX_VA_CLR1 12
#define GX_VA_TEX0 13
#define GX_VA_MAX_ATTR 26
#define GX_VA_NULL 0xFF

#define GX_NONE 0
#define GX_DIRECT 1
#define GX_INDEX8 2
#define GX_INDEX16 3

#define GX_PNMTX0 0
#define GX_TEXMTX0 30
#define GX_IDENTITY 60

#define GX_PERSPECTIVE 0
#define GX_ORTHOGRAPHIC 1

#define GX_MAX_TEVSTAGE 16
#define GX_MAX_TEXMAP 8
#define GX_MAX_TEXCOORD 8

/* ---- init / frame ------------------------------------------------------ */
void *GXInit(void *base, u32 size);
void GXDrawDone(void);
void GXFlush(void);
void GXPixModeSync(void);
void GXAdjustForOverscan(const struct GXRenderModeObj *rmin, struct GXRenderModeObj *rmout, u16 hor, u16 ver);
void GXSetCopyClear(GXColor clearColor, u32 clearZ);
void GXSetCopyFilter(u8 aa, const u8 samplePattern[12][2], u8 vf, const u8 vfilter[7]);
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht);
void GXSetDispCopyDst(u16 wd, u16 ht);
f32 GXSetDispCopyYScale(f32 vscale);
void GXSetDispCopyGamma(int gamma);
void GXCopyDisp(void *dest, u8 clear);
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht);
void GXSetTexCopyDst(u16 wd, u16 ht, int fmt, u8 mipmap);
void GXCopyTex(void *dest, u8 clear);
void GXSetPixelFmt(int pixFmt, int zFmt);

/* ---- vertex format / immediate geometry -------------------------------- */
void GXClearVtxDesc(void);
void GXSetVtxDesc(int attr, int type);
void GXSetVtxAttrFmt(int vtxfmt, int attr, int cnt, int type, u8 frac);
void GXSetArray(int attr, const void *base, u8 stride);
void GXInvalidateVtxCache(void);
void GXBegin(int type, int vtxfmt, u16 nverts);
static inline void GXEnd(void) {}
void GXCallDisplayList(const void *list, u32 nbytes);
void GXDrawSphere(u8 numMajor, u8 numMinor);

/* Writes to the GX FIFO (the GameCube's write-gather pipe at 0xCC008000). */
void GXWGFifo_u8(u8 v);
void GXWGFifo_s8(s8 v);
void GXWGFifo_u16(u16 v);
void GXWGFifo_s16(s16 v);
void GXWGFifo_u32(u32 v);
void GXWGFifo_s32(s32 v);
void GXWGFifo_f32(f32 v);

/* ---- geometry / transform ---------------------------------------------- */
void GXSetCullMode(int mode);
void GXSetPointSize(u8 pointSize, int texOffsets);
void GXEnableTexOffsets(int coord, u8 line_enable, u8 point_enable);
void GXSetProjection(const f32 mtx[4][4], int type);
void GXGetProjectionv(f32 *p);
void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz);
void GXGetViewportv(f32 *viewport);
void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht);
void GXLoadPosMtxImm(const f32 mtx[3][4], u32 id);
void GXLoadTexMtxImm(const f32 mtx[][4], u32 id, int type);
void GXSetCurrentMtx(u32 id);
void GXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32 *pm, const f32 *vp,
               f32 *sx, f32 *sy, f32 *sz);
void GXSetNumTexGens(u8 nTexGens);
void GXSetTexCoordGen2(int dst_coord, int func, int src_param, u32 mtx, u8 normalize, u32 postmtx);

/* ---- lighting ------------------------------------------------------------ */
void GXSetNumChans(u8 nChans);
void GXSetChanCtrl(int chan, u8 enable, int amb_src, int mat_src, u32 light_mask, int diff_fn, int attn_fn);
void GXSetChanAmbColor(int chan, GXColor amb_color);
void GXSetChanMatColor(int chan, GXColor mat_color);
void GXInitLightAttnK(GXLightObj *lt_obj, f32 k0, f32 k1, f32 k2);
void GXInitLightDistAttn(GXLightObj *lt_obj, f32 ref_distance, f32 ref_brightness, int dist_func);
void GXInitLightPos(GXLightObj *lt_obj, f32 x, f32 y, f32 z);
void GXInitLightColor(GXLightObj *lt_obj, GXColor color);
void GXLoadLightObjImm(const GXLightObj *lt_obj, int light);

/* ---- textures ------------------------------------------------------------ */
void GXInitTexObj(GXTexObj *obj, void *image, u16 width, u16 height, int format, int wrap_s,
                  int wrap_t, u8 mipmap);
void GXInitTexObjLOD(GXTexObj *obj, int min_filt, int mag_filt, f32 min_lod, f32 max_lod,
                     f32 lod_bias, u8 bias_clamp, u8 do_edge_lod, int max_aniso);
void *GXGetTexObjData(const GXTexObj *obj);
void GXLoadTexObj(const GXTexObj *obj, int id);
void GXInvalidateTexAll(void);

/* ---- TEV ----------------------------------------------------------------- */
void GXSetNumTevStages(u8 nStages);
void GXSetTevOp(int id, int mode);
void GXSetTevOrder(int stage, int coord, int map, int color);
void GXSetTevColorIn(int stage, int a, int b, int c, int d);
void GXSetTevAlphaIn(int stage, int a, int b, int c, int d);
void GXSetTevColorOp(int stage, int op, int bias, int scale, u8 clamp, int out_reg);
void GXSetTevAlphaOp(int stage, int op, int bias, int scale, u8 clamp, int out_reg);
void GXSetTevColor(int id, GXColor color);
void GXSetTevColorS10(int id, GXColorS10 color);
void GXSetTevKColor(int id, GXColor color);
void GXSetTevKColorSel(int stage, int sel);
void GXSetTevKAlphaSel(int stage, int sel);
void GXSetTevSwapMode(int stage, int ras_sel, int tex_sel);
void GXSetTevSwapModeTable(int id, int red, int green, int blue, int alpha);
void GXSetAlphaCompare(int comp0, u8 ref0, int op, int comp1, u8 ref1);

/* ---- pixel engine -------------------------------------------------------- */
void GXSetBlendMode(int type, int src_factor, int dst_factor, int op);
void GXSetColorUpdate(u8 update_enable);
void GXSetAlphaUpdate(u8 update_enable);
void GXSetZMode(u8 compare_enable, int func, u8 update_enable);
void GXSetZCompLoc(u8 before_tex);
void GXSetFog(int type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color);

#ifdef __cplusplus
}
#endif

#endif /* GX_H */
