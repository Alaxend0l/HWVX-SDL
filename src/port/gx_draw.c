/* GX rendering, independent of the graphics API (see gx_render.h).
 *
 * The embedded frame buffer (EFB) is an off-screen render target with the
 * window's size and shape; GXCopyDisp copies it to the window and optionally
 * clears it, like the copy to the external frame buffer.
 *
 * Screen mapping. The game draws into a 640x448 screen (GXSetDispCopySrc),
 * shown at 4:3. Its 4:3 "safe area" is the largest 4:3 rectangle centred in
 * the EFB. In single-view play the game's viewport and scissor are ignored:
 * every draw covers the whole EFB, with the projection narrowed (or, on a
 * screen taller than 4:3, flattened) so that 4:3 content lands in the safe
 * area. 3D views therefore widen to the screen's shape, while the HUD and
 * menus (orthographic, laid out for 640 units) stay 4:3 and centred, and
 * points the game projects itself (Gfx_TransformVector) still line up. In
 * split screen the game's viewports and scissors are used, mapped into the
 * safe area. Port_ViewWiden tells the game's culling how much wider the view
 * is.
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

extern u8 g_CurrentViews[8];   /* game: bit n set = view n drawn this frame */

static int SplitScreen(void) {
    u8 v = g_CurrentViews[0];
    return (v & (v - 1)) != 0;
}

/* The EFB size for the window: its size in pixels (at least 1x1). */
static void WindowSize(int *w, int *h) {
    SDL_GetWindowSizeInPixels(g_port.window, w, h);
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

void GX_DrawInit(void) {
    WindowSize(&s_efbW, &s_efbH);
    g_gxr->init(s_efbW, s_efbH);
}

void GX_Letterbox(int w, int h, int aw, int ah, int out[4]) {
    int dw = w, dh = (int)((s64)w * ah / aw);

    if (dh > h) {
        dh = h;
        dw = (int)((s64)h * aw / ah);
    }
    out[0] = (w - dw) / 2;
    out[1] = (h - dh) / 2;
    out[2] = dw;
    out[3] = dh;
}

/* The game's screen in GX units: GXSetDispCopySrc (640x448), or 640x480. */
static void DisplayArea(f32 d[4]) {
    const u16 *s = g_gx.dispCopySrc;

    if (s[2] == 0 || s[3] == 0) {
        d[0] = 0.0f; d[1] = 0.0f; d[2] = 640.0f; d[3] = 480.0f;
    } else {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
    }
}

/* The 4:3 safe area in EFB pixels: x, y, width, height. */
static void SafeArea(f32 a[4]) {
    int r[4];

    GX_Letterbox(s_efbW, s_efbH, 4, 3, r);
    a[0] = (f32)r[0]; a[1] = (f32)r[1]; a[2] = (f32)r[2]; a[3] = (f32)r[3];
}

/* How much wider (wx) and taller (wy) than 4:3 the EFB is; 1 = not. */
static void Widen(f32 *wx, f32 *wy) {
    f32 a[4];

    SafeArea(a);
    *wx = s_efbW / a[2];
    *wy = s_efbH / a[3];
}

void Port_ViewWiden(f32 *wx, f32 *wy) {
    if (SplitScreen() || s_efbW == 0) {
        *wx = *wy = 1.0f;
    } else {
        Widen(wx, wy);
    }
}

/* A GX rectangle (x, y, width, height) to EFB pixels through the safe area. */
static void MapRect(const f32 in[4], f32 out[4]) {
    f32 d[4], a[4];

    DisplayArea(d);
    SafeArea(a);
    out[0] = a[0] + (in[0] - d[0]) * a[2] / d[2];
    out[1] = a[1] + (in[1] - d[1]) * a[3] / d[3];
    out[2] = in[2] * a[2] / d[2];
    out[3] = in[3] * a[3] / d[3];
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

/* Viewport and scissor in EFB pixels (see "screen mapping"); 0 when the
 * scissor leaves nothing to draw. */
static int MapViewport(GXDrawCall *dc, GXVSUniforms *vs) {
    const GXState *g = &g_gx;
    f32 r[4], wx, wy;
    int x0, y0, x1, y1, i;

    dc->viewport[4] = g->viewport[4];
    dc->viewport[5] = g->viewport[5];
    if (!SplitScreen()) {
        dc->viewport[0] = dc->viewport[1] = 0.0f;
        dc->viewport[2] = (f32)s_efbW;
        dc->viewport[3] = (f32)s_efbH;
        dc->scissor[0] = dc->scissor[1] = 0;
        dc->scissor[2] = s_efbW;
        dc->scissor[3] = s_efbH;
        Widen(&wx, &wy);
        for (i = 0; i < 4; i++) {
            vs->proj[0][i] /= wx;
            vs->proj[1][i] /= wy;
        }
        return 1;
    }
    MapRect(g->viewport, dc->viewport);
    r[0] = (f32)g->scissor[0]; r[1] = (f32)g->scissor[1];
    r[2] = (f32)g->scissor[2]; r[3] = (f32)g->scissor[3];
    MapRect(r, r);
    x0 = (int)(r[0] + 0.5f);
    y0 = (int)(r[1] + 0.5f);
    x1 = (int)(r[0] + r[2] + 0.5f);
    y1 = (int)(r[1] + r[3] + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_efbW) x1 = s_efbW;
    if (y1 > s_efbH) y1 = s_efbH;
    if (x1 <= x0 || y1 <= y0 || dc->viewport[2] < 1.0f || dc->viewport[3] < 1.0f) {
        return 0;
    }
    dc->scissor[0] = x0;
    dc->scissor[1] = y0;
    dc->scissor[2] = x1 - x0;
    dc->scissor[3] = y1 - y0;
    return 1;
}

static int BuildDrawCall(GXDrawCall *dc, GXVSUniforms *vs, GXFSUniforms *fs) {
    const GXState *g = &g_gx;
    int dbg = DebugDraw(), i;

    PackVS(vs);
    PackFS(fs, dbg);
    dc->vs = vs;
    dc->fs = fs;
    if (!MapViewport(dc, vs)) {
        return 0;
    }
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
    return 1;
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
    if (BuildDrawCall(&dc, &vs, &fs)) {
        g_gxr->draw(&dc);
    }
}

/* ---- frame --------------------------------------------------------------- */

static void ClearEFB(void) {
    f32 c[4];

    Color4(&g_gx.clearColor, c);
    g_gxr->clear(c, (f32)((g_gx.clearZ & 0xFFFFFF) / (f64)0xFFFFFF));
}

/* Follows the window: a new size resizes the EFB (and clears it). */
void GX_DrawBeginFrame(void) {
    static int first = 1;
    int w, h;

    WindowSize(&w, &h);
    if (w != s_efbW || h != s_efbH) {
        s_efbW = w;
        s_efbH = h;
        g_gxr->resize(w, h);
        first = 1;
    }
    if (first) {
        first = 0;
        ClearEFB();
    }
}

/* Movie frames (thp.c): an RGBA image, top row first, stretched over the
 * 4:3 safe area (black around it), then shown, since movies never call
 * GXCopyDisp. */
void GX_PortDrawImage(const u8 *rgba, int width, int height) {
    static const f32 black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    f32 a[4];
    int r[4];

    SafeArea(a);
    r[0] = (int)a[0];
    r[1] = (int)a[1];
    r[2] = (int)(a[0] + a[2]);
    r[3] = (int)(a[1] + a[3]);
    g_gxr->clear(black, 1.0f);
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

/* EFB -> window. The game copies its whole screen, which is the whole EFB. */
void GX_DrawCopyDisp(int clear) {
    int r[4] = { 0, 0, s_efbW, s_efbH };

    DumpEFB();
    g_gxr->copyDisp(r);
    if (clear) {
        ClearEFB();
    }
}

void GX_DrawPresent(void) {
    g_gxr->present();
}
