/* GX rendering, independent of the graphics API (see gx_render.h).
 *
 * The embedded frame buffer (EFB) is an off-screen render target of 640x480
 * times the window scale; GXCopyDisp copies it to the window (letterboxed to
 * 4:3) and optionally clears it, like the copy to the external frame buffer.
 *
 * One "uber" shader (shaders/gx.vert, gx.frag) implements the parts of the GX
 * pipeline the game uses, driven by two uniform blocks this file fills in from
 * g_gx: position/texture matrices, texture coordinate generation, the two
 * lighting channels, up to 16 TEV stages, alpha compare and fog. */
#include "gx_render.h"

#define MAX_STAGES 16

const GXRenderer *g_gxr = &g_gxGL;

static u32 s_statDraws, s_statVerts;
static int s_efbW, s_efbH;

void GX_DrawInit(void) {
    int scale = g_port.scale > 0 ? g_port.scale : 1;

    s_efbW = 640 * scale;
    s_efbH = 480 * scale;
    g_gxr->init(s_efbW, s_efbH);
}

void GX_Letterbox(int w, int h, int out[4]) {
    int dw = w, dh = w * 3 / 4;

    if (dh > h) {
        dh = h;
        dw = h * 4 / 3;
    }
    out[0] = (w - dw) / 2;
    out[1] = (h - dh) / 2;
    out[2] = dw;
    out[3] = dh;
}

/* ---- per-draw state ------------------------------------------------------ */

static void Color4(const GXColor *c, f32 *o) {
    o[0] = c->r / 255.0f;
    o[1] = c->g / 255.0f;
    o[2] = c->b / 255.0f;
    o[3] = c->a / 255.0f;
}

/* GX blend factors 2/3 are the other side's color: source color for the
 * destination factor, destination color for the source factor. */
static u8 BlendFactor(int f, int isSrc) {
    switch (f) {
    case 0: return GXR_BF_ZERO;
    case 1: return GXR_BF_ONE;
    case 2: return isSrc ? GXR_BF_DST_COLOR : GXR_BF_SRC_COLOR;
    case 3: return isSrc ? GXR_BF_INV_DST_COLOR : GXR_BF_INV_SRC_COLOR;
    case 4: return GXR_BF_SRC_ALPHA;
    case 5: return GXR_BF_INV_SRC_ALPHA;
    case 6: return GXR_BF_DST_ALPHA;
    default: return GXR_BF_INV_DST_ALPHA;
    }
}

static void PackVS(GXVSUniforms *u) {
    const GXState *g = &g_gx;
    int i;

    memcpy(u->posMtx, g->posMtx, sizeof(u->posMtx));
    memcpy(u->texMtx, g->texMtx, sizeof(u->texMtx));
    memcpy(u->proj, g->proj, sizeof(u->proj));
    for (i = 0; i < 8; i++) {
        u32 m = g->texGen[i].mtx;
        u->texGen[i][0] = g->texGen[i].func;
        u->texGen[i][1] = g->texGen[i].src;
        u->texGen[i][2] = (m >= 30 && m < 60) ? (int)((m - 30) / 3) : -1;
        u->texGen[i][3] = 0;
    }
    for (i = 0; i < 4; i++) {
        u->chan[i][0] = g->chan[i].enable;
        u->chan[i][1] = g->chan[i].ambSrc;
        u->chan[i][2] = g->chan[i].matSrc;
        u->chan[i][3] = (s32)g->chan[i].lightMask;
        u->chanFn[i][0] = g->chan[i].diffFn;
        u->chanFn[i][1] = g->chan[i].attnFn;
        u->chanFn[i][2] = u->chanFn[i][3] = 0;
    }
    Color4(&g->ambColor[0], u->amb[0]);
    Color4(&g->ambColor[1], u->amb[1]);
    Color4(&g->matColor[0], u->mat[0]);
    Color4(&g->matColor[1], u->mat[1]);
    for (i = 0; i < 8; i++) {
        memcpy(u->lightPos[i], g->light[i].pos, 16);
        memcpy(u->lightDir[i], g->light[i].dir, 16);
        memcpy(u->lightColor[i], g->light[i].color, 16);
        memcpy(u->lightA[i], g->light[i].a, 16);
        memcpy(u->lightK[i], g->light[i].k, 16);
    }
    u->counts[0] = g->numTexGens;
    u->counts[1] = g->numChans;
    u->counts[2] = u->counts[3] = 0;
}

static void PackFS(GXFSUniforms *u, int debug) {
    const GXState *g = &g_gx;
    int i;

    for (i = 0; i < MAX_STAGES; i++) {
        const GXTevStage *t = &g->tev[i];
        int c = t->channel;
        u->order[i][0] = t->texCoord;
        u->order[i][1] = t->texMap == 0xFF ? 255 : t->texMap;
        /* COLOR0/ALPHA0/COLOR0A0 -> 0, COLOR1/ALPHA1/COLOR1A1 -> 1 */
        u->order[i][2] = c == 0xFF ? 255 : (c == 1 || c == 3 || c == 5) ? 1 : (c == 0 || c == 2 || c == 4) ? 0 : 255;
        u->order[i][3] = 0;
        u->cin[i][0] = t->colorIn[0]; u->cin[i][1] = t->colorIn[1];
        u->cin[i][2] = t->colorIn[2]; u->cin[i][3] = t->colorIn[3];
        u->ain[i][0] = t->alphaIn[0]; u->ain[i][1] = t->alphaIn[1];
        u->ain[i][2] = t->alphaIn[2]; u->ain[i][3] = t->alphaIn[3];
        u->cop[i][0] = t->colorOp; u->cop[i][1] = t->colorBias;
        u->cop[i][2] = t->colorScale; u->cop[i][3] = t->colorClamp;
        u->aop[i][0] = t->alphaOp; u->aop[i][1] = t->alphaBias;
        u->aop[i][2] = t->alphaScale; u->aop[i][3] = t->alphaClamp;
        u->out[i][0] = t->colorOut; u->out[i][1] = t->alphaOut;
        u->out[i][2] = u->out[i][3] = 0;
    }
    for (i = 0; i < 4; i++) {
        Color4(&g->tevReg[i], u->reg[i]);
    }
    u->alphaComp[0] = g->alphaComp[0];
    u->alphaComp[1] = g->alphaComp[1];
    u->alphaComp[2] = g->alphaComp[2];
    u->alphaComp[3] = g->alphaComp[3];
    u->misc[0] = g->numTevStages;
    u->misc[1] = g->alphaComp[4];
    /* GXSetFog: with startz == endz (or nearz == farz) the SDK programs A = C = 0,
     * so the fog value is 0 everywhere: no fog. Gfx_SetFog does that for a fog
     * colour with alpha 0 (ranges 0, 0), e.g. after leaving gameplay. */
    u->misc[2] = (g->fogEnd == g->fogStart || g->fogFar == g->fogNear) ? 0 : g->fogType;
    u->misc[3] = debug;
    u->fogParam[0] = g->fogStart;
    u->fogParam[1] = g->fogEnd;
    u->fogParam[2] = g->fogNear;
    u->fogParam[3] = g->fogFar;
    Color4(&g->fogColor, u->fogColor);
}

/* HWVX_GXDEBUG=1: everything magenta, no depth/blend/cull. */
static int DebugDraw(void) {
    static int dbg = -1;
    if (dbg < 0) {
        const char *e = SDL_getenv("HWVX_GXDEBUG");
        dbg = e != NULL && *e == '1';
    }
    return dbg;
}

static void BuildDrawCall(GXDrawCall *dc, GXVSUniforms *vs, GXFSUniforms *fs) {
    const GXState *g = &g_gx;
    int dbg = DebugDraw(), i;

    PackVS(vs);
    PackFS(fs, dbg);
    dc->vs = vs;
    dc->fs = fs;
    memcpy(dc->viewport, g->viewport, sizeof(dc->viewport));
    memcpy(dc->scissor, g->scissor, sizeof(dc->scissor));
    for (i = 0; i < 8; i++) {
        dc->tex[i] = g->texLoaded[i] ? GX_TexGet(&g->texObj[i], &dc->samp[i]) : NULL;
        if (dc->tex[i] == NULL) {
            memset(&dc->samp[i], 0, sizeof(dc->samp[i]));
        }
    }
    dc->blend = g->blendType == 1 || g->blendType == 3;
    dc->blendRevSub = g->blendType == 3;
    dc->blendSrc = dc->blendRevSub ? GXR_BF_ONE : BlendFactor(g->blendSrc, 1);
    dc->blendDst = dc->blendRevSub ? GXR_BF_ONE : BlendFactor(g->blendDst, 0);
    dc->depthTest = g->zEnable;
    dc->depthFunc = g->zFunc & 7;
    dc->depthWrite = g->zUpdate;
    dc->colorWrite = g->colorUpdate;
    dc->alphaWrite = g->alphaUpdate;
    dc->cull = g->cullMode & 3;
    if (dbg) {
        dc->blend = dc->depthTest = 0;
        dc->cull = 0;
        dc->colorWrite = dc->alphaWrite = 1;
    }
}

/* ---- primitives ---------------------------------------------------------- */

static GXVertex *s_tri;
static u32 s_triCap;

static GXVertex *Reserve(u32 n) {
    if (n > s_triCap) {
        s_triCap = n + 1024;
        s_tri = SDL_realloc(s_tri, s_triCap * sizeof(GXVertex));
    }
    return s_tri;
}

void GX_Draw(int prim, const GXVertex *v, u32 n) {
    static GXVSUniforms vs;
    static GXFSUniforms fs;
    GXDrawCall dc;
    u32 i;

    if (n == 0) {
        return;
    }
    s_statDraws++;
    s_statVerts += n;
    dc.v = v;
    dc.n = n;
    switch (prim) {
    case GX_QUADS: {
        GXVertex *t = Reserve(n / 4 * 6);
        u32 count = 0;
        for (i = 0; i + 3 < n; i += 4) {
            t[count++] = v[i]; t[count++] = v[i + 1]; t[count++] = v[i + 2];
            t[count++] = v[i]; t[count++] = v[i + 2]; t[count++] = v[i + 3];
        }
        dc.v = t;
        dc.n = count;
        dc.prim = GXR_TRIANGLES;
        break;
    }
    case GX_TRIANGLES: dc.prim = GXR_TRIANGLES; break;
    case GX_TRIANGLESTRIP: dc.prim = GXR_TRIANGLESTRIP; break;
    case GX_TRIANGLEFAN: dc.prim = GXR_TRIANGLEFAN; break;
    case GX_LINES: dc.prim = GXR_LINES; break;
    case GX_LINESTRIP: dc.prim = GXR_LINESTRIP; break;
    case GX_POINTS: dc.prim = GXR_POINTS; break;
    default: return;
    }
    if (dc.n == 0) {
        return;
    }
    BuildDrawCall(&dc, &vs, &fs);
    g_gxr->draw(&dc);
}

/* ---- frame --------------------------------------------------------------- */

static void ClearEFB(void) {
    f32 c[4];

    Color4(&g_gx.clearColor, c);
    g_gxr->clear(c, (f32)((g_gx.clearZ & 0xFFFFFF) / (f64)0xFFFFFF));
}

void GX_DrawBeginFrame(void) {
    static int first = 1;
    if (first) {
        first = 0;
        ClearEFB();
    }
}

/* The display copy source (GXSetDispCopySrc, 448 lines after the original's
 * overscan adjustment) in EFB pixels: x0, y0, x1, y1. */
static void CopySrcRect(int r[4]) {
    const u16 *s = g_gx.dispCopySrc;
    f32 sx = s_efbW / 640.0f, sy = s_efbH / 480.0f;

    if (s[2] == 0 || s[3] == 0) {
        r[0] = 0; r[1] = 0; r[2] = s_efbW; r[3] = s_efbH;
        return;
    }
    r[0] = (int)(s[0] * sx);
    r[1] = (int)(s[1] * sy);
    r[2] = (int)((s[0] + s[2]) * sx);
    r[3] = (int)((s[1] + s[3]) * sy);
}

/* Movie frames (thp.c): an RGBA image, top row first, stretched over the
 * display copy source, then shown, since movies never call GXCopyDisp. */
void GX_PortDrawImage(const u8 *rgba, int width, int height) {
    int r[4];

    CopySrcRect(r);
    g_gxr->drawImage(rgba, width, height, r);
    GX_DrawCopyDisp(0);
}

/* Debugging: HWVX_DUMP=dir saves every 60th copied frame as dir/frameNNNNN.bmp. */
static void DumpEFB(void) {
    static const char *dir;
    static int checked, count;
    SDL_Surface *surf;
    char path[512];

    if (!checked) {
        checked = 1;
        dir = SDL_getenv("HWVX_DUMP");
    }
    if (dir == NULL || (count++ % 60) != 0) {
        return;
    }
    Port_Log("frame %d: %u draws, %u vertices since last dump (%s)", count - 1, s_statDraws, s_statVerts,
             g_gxr->name);
    s_statDraws = s_statVerts = 0;
    /* XBGR: the EFB alpha is game scratch (shadow volumes), not opacity */
    surf = SDL_CreateSurface(s_efbW, s_efbH, SDL_PIXELFORMAT_XBGR8888);
    if (surf == NULL) {
        return;
    }
    if (surf->pitch == s_efbW * 4 && g_gxr->readEFB(surf->pixels)) {
        SDL_snprintf(path, sizeof(path), "%s/frame%05d.bmp", dir, count - 1);
        SDL_SaveBMP(surf, path);
    }
    SDL_DestroySurface(surf);
}

/* EFB -> window, letterboxed to 4:3. */
void GX_DrawCopyDisp(int clear) {
    int r[4];

    DumpEFB();
    CopySrcRect(r);
    g_gxr->copyDisp(r);
    if (clear) {
        ClearEFB();
    }
}

void GX_DrawPresent(void) {
    g_gxr->present();
}
