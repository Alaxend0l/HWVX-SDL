/*
 * test_texlimits.c - the largest texture the port draws (docs-pending/asset-limits.md).
 *
 * No GPU: a stub renderer records the textures GX_TexGet creates. Generated
 * RGBA8 images (no game data) of 256 to 4096 texels a side go through the
 * game's own Render_CreateTextureSlot (Render_Texture_.c: 512 in the original,
 * PORT_MAX_TEX_SIZE in the port) and GX_TexGet (gx_tex.c):
 *   - up to PORT_MAX_TEX_SIZE (2048) the slot gets its texture object and the
 *     renderer a texture of that size: RGBA8 with one level (the game drops an
 *     RGBA8 texture's mips), CMPR with the levels the game asks for
 *     ((int)((levels - 1) / 1.15) + 1: 10 of 2048's 12);
 *   - past it (4096) neither: the slot stays empty and draws untextured;
 *   - a non-power-of-two size (640 x 480, 4-texel blocks) is taken as it is.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "gx.h"
#include "gx_render.h"
#include "gx_internal.h"
#include "port_internal.h"

/* Render_Texture_.c's record */
typedef struct Texture {
    u8 name[0x24];
    u32 data;
    u32 unused28;
    f32 unused2C;
    u16 texWidth;
    u16 width;
    u16 height;
    s16 refCount;
    u16 format;
    u16 fromPack;
    u16 view;
    u16 wrapMode;
} Texture;

extern Texture *m_texture;
extern GXTexObj m_gxTexObjArray[];
void Render_CreateTextureSlot(u32 id);

static int s_w, s_h, s_levels, s_created;

static int Stub_open(void) { return 1; }
static void Stub_close(void) { }
static void Stub_init(int w, int h) { (void)w; (void)h; }
static void Stub_resize(int w, int h) { (void)w; (void)h; }
static int Stub_texFormat(int fmt) { return fmt == GXR_TEX_RGBA8; }
static void *Stub_texCreate(int fmt, int w, int h, int levels)
{
    (void)fmt;
    s_w = w;
    s_h = h;
    s_levels = levels;
    s_created++;
    return (void *)(uintptr_t)1;
}
static void Stub_texUpload(void *tex, int fmt, int level, int w, int h, const u8 *rgba)
{
    (void)tex; (void)fmt; (void)level; (void)w; (void)h; (void)rgba;
}
static void Stub_texDestroy(void *tex) { (void)tex; }
static void Stub_draw(const GXDrawCall *dc) { (void)dc; }
static void Stub_clear(const f32 color[4], f32 depth) { (void)color; (void)depth; }
static void Stub_drawImage(const u8 *rgba, int w, int h, const int dst[4]) { (void)rgba; (void)w; (void)h; (void)dst; }
static void Stub_copyDisp(const int src[4]) { (void)src; }
static int Stub_readEFB(u8 *rgba) { (void)rgba; return 0; }
static void Stub_present(void) { }

static const GXRenderer s_stub = {
    .name = "test", .open = Stub_open, .close = Stub_close, .init = Stub_init, .resize = Stub_resize,
    .texFormat = Stub_texFormat, .texCreate = Stub_texCreate, .texUpload = Stub_texUpload, .texDestroy = Stub_texDestroy,
    .draw = Stub_draw, .clear = Stub_clear, .drawImage = Stub_drawImage, .copyDisp = Stub_copyDisp,
    .readEFB = Stub_readEFB, .present = Stub_present,
};

static int s_fails;

static void Check(int ok, const char *what, ...)
{
    va_list ap;

    printf(ok ? "  ok   " : "  FAIL ");
    va_start(ap, what);
    vprintf(what, ap);
    va_end(ap);
    putchar('\n');
    if (!ok) {
        s_fails++;
    }
}

/* A w x h RGBA8 texture with its mips, in tile order or not: only sizes matter. */
static void Probe(u32 id, int fmt, int w, int h, int mips, int expect)
{
    static Texture tex[16];
    GXSampler samp;
    size_t bytes = 0;
    int l, levels = 0;
    u8 *img;
    void *got;

    for (l = 0; l < (mips ? 16 : 1); l++) {
        int lw = w >> l, lh = h >> l;

        if (lw == 0 && lh == 0) {
            break;
        }
        lw = lw ? lw : 1;
        lh = lh ? lh : 1;
        bytes += fmt == 4 ? (size_t)((lw + 7) & ~7) * ((lh + 7) & ~7) / 2 : (size_t)lw * lh * 4;
        levels++;
    }
    img = g_portArena + 0x100000;
    memset(img, 0x80, bytes);
    m_texture = tex;
    memset(&tex[id], 0, sizeof(tex[id]));
    tex[id].data = HND(img);
    tex[id].format = (u16)fmt;          /* 1: RGBA8, 4: CMPR */
    tex[id].texWidth = (u16)w;
    tex[id].width = (u16)h;
    tex[id].height = (u16)levels;       /* the level count */
    memset(&m_gxTexObjArray[id], 0, sizeof(GXTexObj));
    Render_CreateTextureSlot(id);
    Check((GXGetTexObjData(&m_gxTexObjArray[id]) != NULL) == expect, "%s %d x %d, %d levels: the game %s the slot",
          fmt == 4 ? "CMPR" : "RGBA8", w, h, levels, expect ? "fills" : "leaves empty");
    s_created = 0;
    got = expect ? GX_TexGet(&m_gxTexObjArray[id], &samp) : NULL;
    if (expect) {
        int want = fmt == 4 && levels > 1 ? (int)((levels - 1) / 1.15f) + 1 : 1;

        Check(got != NULL && s_created == 1 && s_w == w && s_h == h && s_levels == want,
              "%d x %d: the renderer gets %d x %d with %d levels (%d expected)", w, h, s_w, s_h, s_levels, want);
    } else {
        GXTexObj obj;

        GXInitTexObj(&obj, img, (u16)w, (u16)h, fmt == 4 ? 14 : 6, 0, 0, mips);
        Check(GX_TexGet(&obj, &samp) == NULL && s_created == 0, "%d x %d: GX_TexGet refuses it", w, h);
    }
}

int main(void)
{
    g_gxr = &s_stub;
    Port_InitHandles(96u << 20);
    printf("PORT_MAX_TEX_SIZE %d, PORT_MAX_TEX_LEVELS %d\n", PORT_MAX_TEX_SIZE, PORT_MAX_TEX_LEVELS);
    Probe(1, 1, 256, 256, 0, 1);
    Probe(2, 1, 1024, 1024, 0, 1);
    Probe(3, 1, 2048, 2048, 0, 1);
    Probe(4, 4, 256, 256, 1, 1);
    Probe(5, 4, 1024, 1024, 1, 1);
    Probe(6, 4, 2048, 2048, 1, 1);
    Probe(7, 4, 2048, 512, 1, 1);
    Probe(8, 1, 640, 480, 0, 1);
    Probe(9, 1, 4096, 4096, 0, 0);
    Probe(10, 4, 4096, 256, 1, 0);
    printf("%s: %d check(s) failed\n", s_fails ? "FAILED" : "PASSED", s_fails);
    return s_fails ? 1 : 0;
}
