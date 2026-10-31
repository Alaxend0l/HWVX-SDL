/* GX on OpenGL 3.3: the API. Records GX state in g_gx; vertices are decoded
 * in gx_fifo.c and drawn by gx_draw.c, textures decoded in gx_tex.c. */
#include "gx_internal.h"

#include <math.h>

GXState g_gx;

/* ---- frame ------------------------------------------------------------- */

void GX_PortBeginFrame(void) { GX_DrawBeginFrame(); }
void GX_PortPresent(void) { GX_DrawPresent(); }

static void SetTevOp(int stage, int mode);

void *GXInit(void *base, u32 size) {
    GXState *g = &g_gx;
    int i;

    (void)base;
    (void)size;
    memset(g, 0, sizeof(*g));
    g->viewport[2] = 640.0f;
    g->viewport[3] = 480.0f;
    g->viewport[5] = 1.0f;
    g->scissor[2] = 640;
    g->scissor[3] = 480;
    for (i = 0; i <= GX_POSMTX_IDENTITY; i++) {
        g->posMtx[i][0][0] = g->posMtx[i][1][1] = g->posMtx[i][2][2] = 1.0f;
    }
    for (i = 0; i < GX_NUM_TEXMTX; i++) {
        g->texMtx[i][0][0] = g->texMtx[i][1][1] = g->texMtx[i][2][2] = 1.0f;
    }
    for (i = 0; i < 8; i++) {
        g->texGen[i].func = 1;          /* GX_TG_MTX2x4 */
        g->texGen[i].src = (u8)(4 + i); /* GX_TG_TEX0 + i */
        g->texGen[i].mtx = GX_IDENTITY;
    }
    g->numTexGens = 1;
    g->numChans = 1;
    for (i = 0; i < 4; i++) {
        g->chan[i].matSrc = 1;          /* vertex color */
        g->chan[i].attnFn = 2;
    }
    for (i = 0; i < 2; i++) {
        g->matColor[i] = (GXColor){ 255, 255, 255, 255 };
    }
    g->numTevStages = 1;
    for (i = 0; i < 16; i++) {
        g->tev[i].texCoord = (u8)(i < 8 ? i : 0xFF);
        g->tev[i].texMap = (u8)(i < 8 ? i : 0xFF);
        g->tev[i].channel = 4;          /* GX_COLOR0A0 */
        SetTevOp(i, 3);                 /* GX_REPLACE */
    }
    g->alphaComp[0] = 7;                /* GX_ALWAYS */
    g->alphaComp[3] = 7;
    g->zEnable = 1;
    g->zFunc = 3;                       /* GX_LEQUAL */
    g->zUpdate = 1;
    g->colorUpdate = 1;
    g->alphaUpdate = 1;
    g->cullMode = 2;                    /* GX_CULL_BACK */
    g->blendSrc = 4;
    g->blendDst = 5;
    GX_DrawInit();
    return g;
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
    g_gx.clearColor = clearColor;
    g_gx.clearZ = clearZ;
}
void GXSetCopyFilter(u8 aa, const u8 samplePattern[12][2], u8 vf, const u8 vfilter[7]) {
    (void)aa; (void)samplePattern; (void)vf; (void)vfilter;
}
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    g_gx.dispCopySrc[0] = left;
    g_gx.dispCopySrc[1] = top;
    g_gx.dispCopySrc[2] = wd;
    g_gx.dispCopySrc[3] = ht;
}
void GXSetDispCopyDst(u16 wd, u16 ht) { (void)wd; (void)ht; }
f32 GXSetDispCopyYScale(f32 vscale) { (void)vscale; return 480.0f; }
void GXSetDispCopyGamma(int gamma) { (void)gamma; }
void GXCopyDisp(void *dest, u8 clear) {
    (void)dest;
    GX_DrawCopyDisp(clear);
}
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) { (void)left; (void)top; (void)wd; (void)ht; }
void GXSetTexCopyDst(u16 wd, u16 ht, int fmt, u8 mipmap) { (void)wd; (void)ht; (void)fmt; (void)mipmap; }
void GXCopyTex(void *dest, u8 clear) {
    (void)dest;
    (void)clear;
    PORT_TODO("GXCopyTex (render to texture)");
}
void GXSetPixelFmt(int pixFmt, int zFmt) { (void)pixFmt; (void)zFmt; }

/* ---- vertex format / immediate geometry -------------------------------- */

void GXClearVtxDesc(void) {
    memset(g_gx.vcd, 0, sizeof(g_gx.vcd));
}

void GXSetVtxDesc(int attr, int type) {
    if (attr >= 0 && attr < GX_NUM_ATTR) {
        g_gx.vcd[attr] = (u8)type;
    }
}

void GXSetVtxAttrFmt(int vtxfmt, int attr, int cnt, int type, u8 frac) {
    if (vtxfmt >= 0 && vtxfmt < GX_NUM_VTXFMT && attr >= 0 && attr < GX_NUM_ATTR) {
        g_gx.vat[vtxfmt][attr].cnt = (u8)cnt;
        g_gx.vat[vtxfmt][attr].type = (u8)type;
        g_gx.vat[vtxfmt][attr].frac = frac;
    }
}

void GXSetArray(int attr, const void *base, u8 stride) {
    if (attr >= 0 && attr < GX_NUM_ATTR) {
        g_gx.array[attr] = base;
        g_gx.arrayStride[attr] = stride;
    }
}

void GXInvalidateVtxCache(void) {}

void GXBegin(int type, int vtxfmt, u16 nverts) {
    GX_FifoBegin(type, vtxfmt, nverts);
}

void GXCallDisplayList(const void *list, u32 nbytes) {
    GX_RunCommands(list, nbytes);
}

void GXDrawSphere(u8 numMajor, u8 numMinor) { (void)numMajor; (void)numMinor; }

/* FIFO writes are kept big-endian, as on the hardware. */
void GXWGFifo_u8(u8 v) { GX_FifoWrite(&v, 1); }
void GXWGFifo_s8(s8 v) { GX_FifoWrite(&v, 1); }
void GXWGFifo_u16(u16 v) { v = BE16(v); GX_FifoWrite(&v, 2); }
void GXWGFifo_s16(s16 v) { u16 u = BE16((u16)v); GX_FifoWrite(&u, 2); }
void GXWGFifo_u32(u32 v) { v = BE32(v); GX_FifoWrite(&v, 4); }
void GXWGFifo_s32(s32 v) { u32 u = BE32((u32)v); GX_FifoWrite(&u, 4); }
void GXWGFifo_f32(f32 v) {
    u32 u;
    memcpy(&u, &v, 4);
    u = BE32(u);
    GX_FifoWrite(&u, 4);
}

/* ---- geometry / transform ---------------------------------------------- */

void GXSetCullMode(int mode) { g_gx.cullMode = (u8)mode; }
void GXSetPointSize(u8 pointSize, int texOffsets) {
    (void)texOffsets;
    g_gx.pointSize = pointSize;
}
void GXEnableTexOffsets(int coord, u8 line_enable, u8 point_enable) {
    (void)coord; (void)line_enable; (void)point_enable;
}

void GXSetProjection(const f32 mtx[4][4], int type) {
    memcpy(g_gx.proj, mtx, sizeof(g_gx.proj));
    g_gx.projType = type;
}

/* p[0] = type, then the six values GX keeps for its projection. */
void GXGetProjectionv(f32 *p) {
    p[0] = (f32)g_gx.projType;
    p[1] = g_gx.proj[0][0];
    p[3] = g_gx.proj[1][1];
    p[5] = g_gx.proj[2][2];
    p[6] = g_gx.proj[2][3];
    if (g_gx.projType == GX_ORTHOGRAPHIC) {
        p[2] = g_gx.proj[0][3];
        p[4] = g_gx.proj[1][3];
    } else {
        p[2] = g_gx.proj[0][2];
        p[4] = g_gx.proj[1][2];
    }
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    g_gx.viewport[0] = left;
    g_gx.viewport[1] = top;
    g_gx.viewport[2] = wd;
    g_gx.viewport[3] = ht;
    g_gx.viewport[4] = nearz;
    g_gx.viewport[5] = farz;
}

void GXGetViewportv(f32 *viewport) {
    memcpy(viewport, g_gx.viewport, sizeof(g_gx.viewport));
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht) {
    g_gx.scissor[0] = left;
    g_gx.scissor[1] = top;
    g_gx.scissor[2] = wd;
    g_gx.scissor[3] = ht;
}

void GXLoadPosMtxImm(const f32 mtx[3][4], u32 id) {
    if (id / 3 < GX_NUM_POSMTX) {
        memcpy(g_gx.posMtx[id / 3], mtx, sizeof(g_gx.posMtx[0]));
    }
}

/* type: GX_MTX3x4 (0) or GX_MTX2x4 (1) */
void GXLoadTexMtxImm(const f32 mtx[][4], u32 id, int type) {
    u32 slot = (id - 30) / 3;
    if (id < 30 || slot >= GX_NUM_TEXMTX) {
        return;
    }
    memcpy(g_gx.texMtx[slot], mtx, (type == 1 ? 2 : 3) * sizeof(f32[4]));
    if (type == 1) {
        static const f32 row[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        memcpy(g_gx.texMtx[slot][2], row, sizeof(row));
    }
}

void GXSetCurrentMtx(u32 id) { g_gx.curMtx = id; }

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

void GXSetNumTexGens(u8 nTexGens) { g_gx.numTexGens = nTexGens; }
void GXSetTexCoordGen2(int dst_coord, int func, int src_param, u32 mtx, u8 normalize, u32 postmtx) {
    (void)normalize;
    (void)postmtx;
    if (dst_coord >= 0 && dst_coord < 8) {
        g_gx.texGen[dst_coord].func = (u8)func;
        g_gx.texGen[dst_coord].src = (u8)src_param;
        g_gx.texGen[dst_coord].mtx = mtx;
    }
}

/* ---- lighting ------------------------------------------------------------ */

void GXSetNumChans(u8 nChans) { g_gx.numChans = nChans; }

/* chan: COLOR0, COLOR1, ALPHA0, ALPHA1, COLOR0A0, COLOR1A1 */
void GXSetChanCtrl(int chan, u8 enable, int amb_src, int mat_src, u32 light_mask, int diff_fn, int attn_fn) {
    GXChan c;
    c.enable = enable;
    c.ambSrc = (u8)amb_src;
    c.matSrc = (u8)mat_src;
    c.lightMask = light_mask;
    c.diffFn = (u8)diff_fn;
    c.attnFn = (u8)attn_fn;
    if (chan >= 4) {
        g_gx.chan[chan - 4] = c;
        g_gx.chan[chan - 2] = c;
    } else if (chan >= 0) {
        g_gx.chan[chan] = c;
    }
}

static void SetChanColor(GXColor *dst, int chan, GXColor c) {
    int i = chan & 1;
    if (chan == 2 || chan == 3) {
        dst[i].a = c.a;
    } else if (chan == 0 || chan == 1) {
        dst[i].r = c.r;
        dst[i].g = c.g;
        dst[i].b = c.b;
    } else {
        dst[i] = c;
    }
}

void GXSetChanAmbColor(int chan, GXColor amb_color) { SetChanColor(g_gx.ambColor, chan, amb_color); }
void GXSetChanMatColor(int chan, GXColor mat_color) { SetChanColor(g_gx.matColor, chan, mat_color); }

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

void GXLoadLightObjImm(const GXLightObj *lt_obj, int light) {
    const LightObj *l = (const LightObj *)lt_obj;
    GXLightState *d;
    int i = 0;

    while (i < GX_NUM_LIGHTS && !(light & (1 << i))) {
        i++;
    }
    if (i == GX_NUM_LIGHTS) {
        return;
    }
    d = &g_gx.light[i];
    memcpy(d->pos, l->lpos, sizeof(l->lpos));
    memcpy(d->dir, l->ldir, sizeof(l->ldir));
    memcpy(d->a, l->a, sizeof(l->a));
    memcpy(d->k, l->k, sizeof(l->k));
    d->color[0] = (l->color >> 24) / 255.0f;
    d->color[1] = ((l->color >> 16) & 0xFF) / 255.0f;
    d->color[2] = ((l->color >> 8) & 0xFF) / 255.0f;
    d->color[3] = (l->color & 0xFF) / 255.0f;
}

/* ---- textures ------------------------------------------------------------ */

/* The game's GXTexObj is 32 bytes; layout used by gx_tex.c:
 * [0] image handle, [1] width<<16|height, [2] format, [3] wrapS<<8|wrapT,
 * [4] mipmap, [5] minFilt<<8|magFilt, [6] max LOD (f32 bits). */
void GXInitTexObj(GXTexObj *obj, void *image, u16 width, u16 height, int format, int wrap_s,
                  int wrap_t, u8 mipmap) {
    u32 *w = obj->dummy;
    f32 maxLod = 0.0f;
    memset(obj, 0, sizeof(*obj));
    w[0] = HND(image);
    w[1] = ((u32)width << 16) | height;
    w[2] = (u32)format;
    w[3] = ((u32)wrap_s << 8) | (u32)wrap_t;
    w[4] = mipmap;
    if (mipmap) {
        u32 s = width > height ? width : height;
        while (s > 1) {
            s >>= 1;
            maxLod += 1.0f;
        }
    }
    memcpy(&w[6], &maxLod, 4);
}

void GXInitTexObjLOD(GXTexObj *obj, int min_filt, int mag_filt, f32 min_lod, f32 max_lod,
                     f32 lod_bias, u8 bias_clamp, u8 do_edge_lod, int max_aniso) {
    (void)min_lod; (void)lod_bias; (void)bias_clamp; (void)do_edge_lod; (void)max_aniso;
    obj->dummy[5] = ((u32)min_filt << 8) | (u32)mag_filt;
    memcpy(&obj->dummy[6], &max_lod, 4);
}

void *GXGetTexObjData(const GXTexObj *obj) {
    return PTR(obj->dummy[0]);
}

void GXLoadTexObj(const GXTexObj *obj, int id) {
    if (id >= 0 && id < 8) {
        g_gx.texObj[id] = *obj;
        g_gx.texLoaded[id] = 1;
    }
}

void GXInvalidateTexAll(void) {}

/* ---- TEV ----------------------------------------------------------------- */

enum { CC_CPREV = 0, CC_TEXC = 8, CC_TEXA = 9, CC_RASC = 10, CC_RASA = 11, CC_ONE = 12, CC_ZERO = 15 };
enum { CA_APREV = 0, CA_TEXA = 4, CA_RASA = 5, CA_ZERO = 7 };

static void TevIn(GXTevStage *t, int ca, int cb, int cc, int cd, int aa, int ab, int ac, int ad) {
    t->colorIn[0] = (u8)ca; t->colorIn[1] = (u8)cb; t->colorIn[2] = (u8)cc; t->colorIn[3] = (u8)cd;
    t->alphaIn[0] = (u8)aa; t->alphaIn[1] = (u8)ab; t->alphaIn[2] = (u8)ac; t->alphaIn[3] = (u8)ad;
}

/* GXSetTevOp presets, as the SDK expands them. */
static void SetTevOp(int stage, int mode) {
    GXTevStage *t = &g_gx.tev[stage];
    int carg = stage == 0 ? CC_RASC : CC_CPREV;
    int aarg = stage == 0 ? CA_RASA : CA_APREV;

    switch (mode) {
    case 0: /* MODULATE */
        TevIn(t, CC_ZERO, CC_TEXC, carg, CC_ZERO, CA_ZERO, CA_TEXA, aarg, CA_ZERO);
        break;
    case 1: /* DECAL */
        TevIn(t, carg, CC_TEXC, CC_TEXA, CC_ZERO, CA_ZERO, CA_ZERO, CA_ZERO, aarg);
        break;
    case 2: /* BLEND */
        TevIn(t, carg, CC_ONE, CC_TEXC, CC_ZERO, CA_ZERO, CA_TEXA, aarg, CA_ZERO);
        break;
    case 3: /* REPLACE */
        TevIn(t, CC_ZERO, CC_ZERO, CC_ZERO, CC_TEXC, CA_ZERO, CA_ZERO, CA_ZERO, CA_TEXA);
        break;
    default: /* PASSCLR */
        TevIn(t, CC_ZERO, CC_ZERO, CC_ZERO, carg, CA_ZERO, CA_ZERO, CA_ZERO, aarg);
        break;
    }
    t->colorOp = t->alphaOp = 0;
    t->colorBias = t->alphaBias = 0;
    t->colorScale = t->alphaScale = 0;
    t->colorClamp = t->alphaClamp = 1;
    t->colorOut = t->alphaOut = 0;
}

void GXSetNumTevStages(u8 nStages) { g_gx.numTevStages = nStages; }
void GXSetTevOp(int id, int mode) {
    if (id >= 0 && id < 16) {
        SetTevOp(id, mode);
    }
}
void GXSetTevOrder(int stage, int coord, int map, int color) {
    if (stage >= 0 && stage < 16) {
        g_gx.tev[stage].texCoord = (u8)coord;
        g_gx.tev[stage].texMap = (u8)map;
        g_gx.tev[stage].channel = (u8)color;
    }
}
void GXSetTevColorIn(int stage, int a, int b, int c, int d) {
    GXTevStage *t = &g_gx.tev[stage & 15];
    t->colorIn[0] = (u8)a; t->colorIn[1] = (u8)b; t->colorIn[2] = (u8)c; t->colorIn[3] = (u8)d;
}
void GXSetTevAlphaIn(int stage, int a, int b, int c, int d) {
    GXTevStage *t = &g_gx.tev[stage & 15];
    t->alphaIn[0] = (u8)a; t->alphaIn[1] = (u8)b; t->alphaIn[2] = (u8)c; t->alphaIn[3] = (u8)d;
}
void GXSetTevColorOp(int stage, int op, int bias, int scale, u8 clamp, int out_reg) {
    GXTevStage *t = &g_gx.tev[stage & 15];
    t->colorOp = (u8)op; t->colorBias = (u8)bias; t->colorScale = (u8)scale;
    t->colorClamp = clamp; t->colorOut = (u8)out_reg;
}
void GXSetTevAlphaOp(int stage, int op, int bias, int scale, u8 clamp, int out_reg) {
    GXTevStage *t = &g_gx.tev[stage & 15];
    t->alphaOp = (u8)op; t->alphaBias = (u8)bias; t->alphaScale = (u8)scale;
    t->alphaClamp = clamp; t->alphaOut = (u8)out_reg;
}
void GXSetTevColor(int id, GXColor color) { g_gx.tevReg[id & 3] = color; }
void GXSetTevColorS10(int id, GXColorS10 color) {
    GXColor c;
    c.r = (u8)(color.r < 0 ? 0 : color.r > 255 ? 255 : color.r);
    c.g = (u8)(color.g < 0 ? 0 : color.g > 255 ? 255 : color.g);
    c.b = (u8)(color.b < 0 ? 0 : color.b > 255 ? 255 : color.b);
    c.a = (u8)(color.a < 0 ? 0 : color.a > 255 ? 255 : color.a);
    g_gx.tevReg[id & 3] = c;
}
void GXSetTevKColor(int id, GXColor color) { (void)id; (void)color; }
void GXSetTevKColorSel(int stage, int sel) { (void)stage; (void)sel; }
void GXSetTevKAlphaSel(int stage, int sel) { (void)stage; (void)sel; }
void GXSetTevSwapMode(int stage, int ras_sel, int tex_sel) { (void)stage; (void)ras_sel; (void)tex_sel; }
void GXSetTevSwapModeTable(int id, int red, int green, int blue, int alpha) {
    (void)id; (void)red; (void)green; (void)blue; (void)alpha;
}
void GXSetAlphaCompare(int comp0, u8 ref0, int op, int comp1, u8 ref1) {
    g_gx.alphaComp[0] = (u8)comp0;
    g_gx.alphaComp[1] = ref0;
    g_gx.alphaComp[2] = (u8)op;
    g_gx.alphaComp[3] = (u8)comp1;
    g_gx.alphaComp[4] = ref1;
}

/* ---- pixel engine -------------------------------------------------------- */

void GXSetBlendMode(int type, int src_factor, int dst_factor, int op) {
    g_gx.blendType = (u8)type;
    g_gx.blendSrc = (u8)src_factor;
    g_gx.blendDst = (u8)dst_factor;
    g_gx.blendOp = (u8)op;
}
void GXSetColorUpdate(u8 update_enable) { g_gx.colorUpdate = update_enable; }
void GXSetAlphaUpdate(u8 update_enable) { g_gx.alphaUpdate = update_enable; }
void GXSetZMode(u8 compare_enable, int func, u8 update_enable) {
    g_gx.zEnable = compare_enable;
    g_gx.zFunc = (u8)func;
    g_gx.zUpdate = update_enable;
}
void GXSetZCompLoc(u8 before_tex) { (void)before_tex; }
void GXSetFog(int type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {
    g_gx.fogType = (u8)type;
    g_gx.fogStart = startz;
    g_gx.fogEnd = endz;
    g_gx.fogNear = nearz;
    g_gx.fogFar = farz;
    g_gx.fogColor = color;
}
