/* GX on OpenGL 3.3 — stage 1.
 *
 * Accepts the full API the game uses and keeps the state the game reads back
 * (projection, viewport, texture objects). Frames are cleared and presented;
 * geometry is not drawn yet. Stage 2 adds the vertex decoder (GXBegin +
 * FIFO writes, display lists), GameCube texture decoding and TEV -> GLSL. */
#include "port_internal.h"

#include <math.h>

static struct {
    f32 proj[4][4];
    int projType;
    f32 viewport[6];
    GXColor clearColor;
    u32 clearZ;
    int inBegin;
    u32 vertsLeft;
} gx;

/* ---- frame ------------------------------------------------------------- */

void GX_PortBeginFrame(void) {
    int w, h;
    SDL_GetWindowSizeInPixels(g_port.window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(gx.clearColor.r / 255.0f, gx.clearColor.g / 255.0f, gx.clearColor.b / 255.0f, 1.0f);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GX_PortPresent(void) {
    SDL_GL_SwapWindow(g_port.window);
}

void *GXInit(void *base, u32 size) {
    (void)base;
    (void)size;
    gx.viewport[2] = 640.0f;
    gx.viewport[3] = 480.0f;
    gx.viewport[5] = 1.0f;
    GX_PortBeginFrame();
    return &gx;
}

void GXDrawDone(void) {}
void GXFlush(void) {}
void GXPixModeSync(void) {}
void GXAdjustForOverscan(const struct GXRenderModeObj *rmin, struct GXRenderModeObj *rmout, u16 hor, u16 ver) {
    (void)hor;
    (void)ver;
    if (rmin != NULL && rmout != NULL && rmin != rmout) {
        memcpy(rmout, rmin, 0x3C); /* sizeof(GXRenderModeObj) */
    }
}
void GXSetCopyClear(GXColor clearColor, u32 clearZ) {
    gx.clearColor = clearColor;
    gx.clearZ = clearZ;
}
void GXSetCopyFilter(u8 aa, const u8 samplePattern[12][2], u8 vf, const u8 vfilter[7]) {
    (void)aa; (void)samplePattern; (void)vf; (void)vfilter;
}
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) { (void)left; (void)top; (void)wd; (void)ht; }
void GXSetDispCopyDst(u16 wd, u16 ht) { (void)wd; (void)ht; }
f32 GXSetDispCopyYScale(f32 vscale) { (void)vscale; return 480.0f; }
void GXSetDispCopyGamma(int gamma) { (void)gamma; }
void GXCopyDisp(void *dest, u8 clear) { (void)dest; (void)clear; }
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) { (void)left; (void)top; (void)wd; (void)ht; }
void GXSetTexCopyDst(u16 wd, u16 ht, int fmt, u8 mipmap) { (void)wd; (void)ht; (void)fmt; (void)mipmap; }
void GXCopyTex(void *dest, u8 clear) {
    (void)dest;
    (void)clear;
    PORT_TODO("GXCopyTex (render to texture)");
}
void GXSetPixelFmt(int pixFmt, int zFmt) { (void)pixFmt; (void)zFmt; }

/* ---- vertex format / immediate geometry -------------------------------- */

void GXClearVtxDesc(void) {}
void GXSetVtxDesc(int attr, int type) { (void)attr; (void)type; }
void GXSetVtxAttrFmt(int vtxfmt, int attr, int cnt, int type, u8 frac) {
    (void)vtxfmt; (void)attr; (void)cnt; (void)type; (void)frac;
}
void GXSetArray(int attr, const void *base, u8 stride) { (void)attr; (void)base; (void)stride; }
void GXInvalidateVtxCache(void) {}

void GXBegin(int type, int vtxfmt, u16 nverts) {
    (void)type;
    (void)vtxfmt;
    gx.inBegin = 1;
    gx.vertsLeft = nverts;
    PORT_TODO("GX vertex submission");
}

void GXCallDisplayList(const void *list, u32 nbytes) {
    (void)list;
    (void)nbytes;
    PORT_TODO("GXCallDisplayList");
}

void GXDrawSphere(u8 numMajor, u8 numMinor) { (void)numMajor; (void)numMinor; }

void GXWGFifo_u8(u8 v) { (void)v; }
void GXWGFifo_s8(s8 v) { (void)v; }
void GXWGFifo_u16(u16 v) { (void)v; }
void GXWGFifo_s16(s16 v) { (void)v; }
void GXWGFifo_u32(u32 v) { (void)v; }
void GXWGFifo_s32(s32 v) { (void)v; }
void GXWGFifo_f32(f32 v) { (void)v; }

/* ---- geometry / transform ---------------------------------------------- */

void GXSetCullMode(int mode) { (void)mode; }
void GXSetPointSize(u8 pointSize, int texOffsets) { (void)pointSize; (void)texOffsets; }
void GXEnableTexOffsets(int coord, u8 line_enable, u8 point_enable) {
    (void)coord; (void)line_enable; (void)point_enable;
}

void GXSetProjection(const f32 mtx[4][4], int type) {
    memcpy(gx.proj, mtx, sizeof(gx.proj));
    gx.projType = type;
}

/* p[0] = type, then the six values GX keeps for its projection. */
void GXGetProjectionv(f32 *p) {
    p[0] = (f32)gx.projType;
    p[1] = gx.proj[0][0];
    p[3] = gx.proj[1][1];
    p[5] = gx.proj[2][2];
    p[6] = gx.proj[2][3];
    if (gx.projType == GX_ORTHOGRAPHIC) {
        p[2] = gx.proj[0][3];
        p[4] = gx.proj[1][3];
    } else {
        p[2] = gx.proj[0][2];
        p[4] = gx.proj[1][2];
    }
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    gx.viewport[0] = left;
    gx.viewport[1] = top;
    gx.viewport[2] = wd;
    gx.viewport[3] = ht;
    gx.viewport[4] = nearz;
    gx.viewport[5] = farz;
}

void GXGetViewportv(f32 *viewport) {
    memcpy(viewport, gx.viewport, sizeof(gx.viewport));
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht) { (void)left; (void)top; (void)wd; (void)ht; }
void GXLoadPosMtxImm(const f32 mtx[3][4], u32 id) { (void)mtx; (void)id; }
void GXLoadTexMtxImm(const f32 mtx[][4], u32 id, int type) { (void)mtx; (void)id; (void)type; }
void GXSetCurrentMtx(u32 id) { (void)id; }

/* Same math as the SDK: model-view, then projection, then viewport. */
void GXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32 *pm, const f32 *vp,
               f32 *sx, f32 *sy, f32 *sz) {
    f32 px = mtx[0][0] * x + mtx[0][1] * y + mtx[0][2] * z + mtx[0][3];
    f32 py = mtx[1][0] * x + mtx[1][1] * y + mtx[1][2] * z + mtx[1][3];
    f32 pz = mtx[2][0] * x + mtx[2][1] * y + mtx[2][2] * z + mtx[2][3];
    f32 xc, yc, zc, wc;

    if (pm[0] == GX_PERSPECTIVE) {
        xc = px * pm[1] + pz * pm[2];
        yc = py * pm[3] + pz * pm[4];
        zc = pm[6] + pz * pm[5];
        wc = 1.0f / -pz;
    } else {
        xc = px * pm[1] + pm[2];
        yc = py * pm[3] + pm[4];
        zc = pm[6] + pz * pm[5];
        wc = 1.0f;
    }
    *sx = vp[2] / 2.0f + vp[0] + wc * xc * vp[2] / 2.0f;
    *sy = vp[3] / 2.0f + vp[1] - wc * yc * vp[3] / 2.0f;
    *sz = vp[5] + wc * zc * (vp[5] - vp[4]);
}

void GXSetNumTexGens(u8 nTexGens) { (void)nTexGens; }
void GXSetTexCoordGen2(int dst_coord, int func, int src_param, u32 mtx, u8 normalize, u32 postmtx) {
    (void)dst_coord; (void)func; (void)src_param; (void)mtx; (void)normalize; (void)postmtx;
}

/* ---- lighting ------------------------------------------------------------ */

void GXSetNumChans(u8 nChans) { (void)nChans; }
void GXSetChanCtrl(int chan, u8 enable, int amb_src, int mat_src, u32 light_mask, int diff_fn, int attn_fn) {
    (void)chan; (void)enable; (void)amb_src; (void)mat_src; (void)light_mask; (void)diff_fn; (void)attn_fn;
}
void GXSetChanAmbColor(int chan, GXColor amb_color) { (void)chan; (void)amb_color; }
void GXSetChanMatColor(int chan, GXColor mat_color) { (void)chan; (void)mat_color; }

/* GXLightObj is the game's 64-byte buffer; keep the SDK's internal layout. */
typedef struct LightObj {
    u32 reserved[3];
    u32 color;
    f32 a[3];
    f32 k[3];
    f32 lpos[3];
    f32 ldir[3];
} LightObj;

void GXInitLightAttnK(GXLightObj *lt_obj, f32 k0, f32 k1, f32 k2) {
    LightObj *l = (LightObj *)lt_obj;
    l->k[0] = k0;
    l->k[1] = k1;
    l->k[2] = k2;
}

void GXInitLightDistAttn(GXLightObj *lt_obj, f32 ref_distance, f32 ref_brightness, int dist_func) {
    LightObj *l = (LightObj *)lt_obj;
    f32 k0 = 1.0f, k1 = 0.0f, k2 = 0.0f;
    if (ref_distance > 0.0f && ref_brightness > 0.0f && ref_brightness < 1.0f) {
        switch (dist_func) {
        case 1: /* GX_DA_GENTLE */
            k1 = (1.0f - ref_brightness) / (ref_brightness * ref_distance);
            break;
        case 2: /* GX_DA_MEDIUM */
            k1 = 0.5f * (1.0f - ref_brightness) / (ref_brightness * ref_distance);
            k2 = 0.5f * (1.0f - ref_brightness) / (ref_brightness * ref_distance * ref_distance);
            break;
        case 3: /* GX_DA_STEEP */
            k2 = (1.0f - ref_brightness) / (ref_brightness * ref_distance * ref_distance);
            break;
        }
    }
    l->k[0] = k0;
    l->k[1] = k1;
    l->k[2] = k2;
}

void GXInitLightPos(GXLightObj *lt_obj, f32 x, f32 y, f32 z) {
    LightObj *l = (LightObj *)lt_obj;
    l->lpos[0] = x;
    l->lpos[1] = y;
    l->lpos[2] = z;
}

void GXInitLightColor(GXLightObj *lt_obj, GXColor color) {
    LightObj *l = (LightObj *)lt_obj;
    l->color = ((u32)color.r << 24) | ((u32)color.g << 16) | ((u32)color.b << 8) | color.a;
}

void GXLoadLightObjImm(const GXLightObj *lt_obj, int light) { (void)lt_obj; (void)light; }

/* ---- textures ------------------------------------------------------------ */

void GXInitTexObj(GXTexObj *obj, void *image, u16 width, u16 height, int format, int wrap_s,
                  int wrap_t, u8 mipmap) {
    /* The game's GXTexObj is 32 bytes; image pointer stored as a handle so
     * the struct stays the same size on 64-bit hosts. */
    u32 *w = obj->dummy;
    memset(obj, 0, sizeof(*obj));
    w[0] = HND(image);
    w[1] = ((u32)width << 16) | height;
    w[2] = (u32)format;
    w[3] = ((u32)wrap_s << 8) | (u32)wrap_t;
    w[4] = mipmap;
}

void GXInitTexObjLOD(GXTexObj *obj, int min_filt, int mag_filt, f32 min_lod, f32 max_lod,
                     f32 lod_bias, u8 bias_clamp, u8 do_edge_lod, int max_aniso) {
    (void)min_lod; (void)max_lod; (void)lod_bias; (void)bias_clamp; (void)do_edge_lod; (void)max_aniso;
    obj->dummy[5] = ((u32)min_filt << 8) | (u32)mag_filt;
}

void *GXGetTexObjData(const GXTexObj *obj) {
    return PTR(obj->dummy[0]);
}

void GXLoadTexObj(const GXTexObj *obj, int id) { (void)obj; (void)id; }
void GXInvalidateTexAll(void) {}

/* ---- TEV ----------------------------------------------------------------- */

void GXSetNumTevStages(u8 nStages) { (void)nStages; }
void GXSetTevOp(int id, int mode) { (void)id; (void)mode; }
void GXSetTevOrder(int stage, int coord, int map, int color) { (void)stage; (void)coord; (void)map; (void)color; }
void GXSetTevColorIn(int stage, int a, int b, int c, int d) { (void)stage; (void)a; (void)b; (void)c; (void)d; }
void GXSetTevAlphaIn(int stage, int a, int b, int c, int d) { (void)stage; (void)a; (void)b; (void)c; (void)d; }
void GXSetTevColorOp(int stage, int op, int bias, int scale, u8 clamp, int out_reg) {
    (void)stage; (void)op; (void)bias; (void)scale; (void)clamp; (void)out_reg;
}
void GXSetTevAlphaOp(int stage, int op, int bias, int scale, u8 clamp, int out_reg) {
    (void)stage; (void)op; (void)bias; (void)scale; (void)clamp; (void)out_reg;
}
void GXSetTevColor(int id, GXColor color) { (void)id; (void)color; }
void GXSetTevColorS10(int id, GXColorS10 color) { (void)id; (void)color; }
void GXSetTevKColor(int id, GXColor color) { (void)id; (void)color; }
void GXSetTevKColorSel(int stage, int sel) { (void)stage; (void)sel; }
void GXSetTevKAlphaSel(int stage, int sel) { (void)stage; (void)sel; }
void GXSetTevSwapMode(int stage, int ras_sel, int tex_sel) { (void)stage; (void)ras_sel; (void)tex_sel; }
void GXSetTevSwapModeTable(int id, int red, int green, int blue, int alpha) {
    (void)id; (void)red; (void)green; (void)blue; (void)alpha;
}
void GXSetAlphaCompare(int comp0, u8 ref0, int op, int comp1, u8 ref1) {
    (void)comp0; (void)ref0; (void)op; (void)comp1; (void)ref1;
}

/* ---- pixel engine -------------------------------------------------------- */

void GXSetBlendMode(int type, int src_factor, int dst_factor, int op) {
    (void)type; (void)src_factor; (void)dst_factor; (void)op;
}
void GXSetColorUpdate(u8 update_enable) { (void)update_enable; }
void GXSetAlphaUpdate(u8 update_enable) { (void)update_enable; }
void GXSetZMode(u8 compare_enable, int func, u8 update_enable) {
    (void)compare_enable; (void)func; (void)update_enable;
}
void GXSetZCompLoc(u8 before_tex) { (void)before_tex; }
void GXSetFog(int type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {
    (void)type; (void)startz; (void)endz; (void)nearz; (void)farz; (void)color;
}
