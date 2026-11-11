/*
 * test_dlcache.c - checks the display-list cache in src/port/gx_fifo.c.
 *
 * No GPU is needed: the renderer is a stub that records every draw call, and
 * the test then inspects those calls and the vertices behind them - exactly what
 * a real backend sees.
 *
 * Build (from build/, after the normal build): link the test and its stubs
 * against every object except main_sdl.c.o, which owns the real main().
 * Run:
 *   ./test_dlcache                  # normal: each list is decoded once
 *   HWVX_DLVERIFY=1 ./test_dlcache  # hits are also re-decoded and compared
 *
 * What it covers:
 *   - a list decodes once; a later call replays the recorded draws;
 *   - quads are stored already expanded, in the order GX_Draw would draw them;
 *   - adjacent batches of the same kind inside one list merge into one draw,
 *     with every vertex left where the list put it;
 *   - cached draws hand the renderer the vertex pool as a batch;
 *   - rewriting a list's bytes is a miss (its bytes are hashed);
 *   - rewriting a vertex array, reported through Port_InvalidateDisplayLists,
 *     drops the lists that indexed it;
 *   - an array rewritten without being reported: the known gap, and what
 *     HWVX_DLVERIFY=1 does about it;
 *   - immediate mode (GXBegin + GXWGFifo_*) is not cached and not batched;
 *   - every attribute encoding lands in the compact vertex: s8 normals at
 *     scale 1/64, and 8 bit colors bit-replicated exactly as the hardware
 *     color registers expand them;
 *   - more lists than the pool holds: it starts over, and says so;
 *   - a list rebuilt at one address every call is called volatile and left
 *     uncaptured until its bytes settle.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "gx.h"
#include "gx_render.h"
#include "gx_internal.h"
#include "port.h"

/* ---- the stub renderer --------------------------------------------------- */

static GXDrawCall s_draws[256];
static int s_ndraws;

static int Stub_open(void) { return 1; }
static void Stub_close(void) { }
static void Stub_init(int w, int h) { (void)w; (void)h; }
static void Stub_resize(int w, int h) { (void)w; (void)h; }
static int Stub_texFormat(int fmt) { return fmt == GXR_TEX_RGBA8; }
static void *Stub_texCreate(int fmt, int w, int h, int levels)
{
    (void)fmt; (void)w; (void)h; (void)levels;
    return (void *)(uintptr_t)1;
}
static void Stub_texUpload(void *tex, int fmt, int level, int w, int h, const u8 *rgba)
{
    (void)tex; (void)fmt; (void)level; (void)w; (void)h; (void)rgba;
}
static void Stub_texDestroy(void *tex) { (void)tex; }
static void Stub_draw(const GXDrawCall *dc)
{
    if (s_ndraws < (int)(sizeof(s_draws) / sizeof(s_draws[0])))
        s_draws[s_ndraws++] = *dc;
}
static void Stub_clear(const f32 color[4], f32 depth)
{
    (void)color; (void)depth;
}
static void Stub_drawImage(const u8 *rgba, int w, int h, const int dst[4])
{
    (void)rgba; (void)w; (void)h; (void)dst;
}
static void Stub_copyDisp(const int src[4]) { (void)src; }
static int Stub_readEFB(u8 *rgba) { (void)rgba; return 0; }
static void Stub_present(void) { }

/* by name: entries the test doesn't need (copyEFB, the static meshes) stay NULL */
static const GXRenderer s_stub = {
    .name = "test", .open = Stub_open, .close = Stub_close, .init = Stub_init, .resize = Stub_resize,
    .texFormat = Stub_texFormat, .texCreate = Stub_texCreate, .texUpload = Stub_texUpload, .texDestroy = Stub_texDestroy,
    .draw = Stub_draw, .clear = Stub_clear, .drawImage = Stub_drawImage, .copyDisp = Stub_copyDisp,
    .readEFB = Stub_readEFB, .present = Stub_present,
};

/* ---- checks -------------------------------------------------------------- */

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

/* ---- vertex data ---------------------------------------------------------
 * Direct lists (fmt 0): pos XYZ as S16 with frac 4, so writing id*16 into the
 * stream makes pos.x come out as exactly `id`, which names the vertex. */

static u8 s_dl[1024];
static u32 s_dlSize;

/* pos, col0, tex0 all direct */
static void UseDirectFmt(void)
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, GX_DIRECT);              /* GX_VA_POS */
    GXSetVtxDesc(11, GX_DIRECT);             /* GX_VA_CLR0 */
    GXSetVtxDesc(13, GX_DIRECT);             /* GX_VA_TEX0 */
    GXSetVtxAttrFmt(0, 9, 1, 3, 4);          /* pos XYZ, S16, frac 4 */
    GXSetVtxAttrFmt(0, 11, 1, 2, 0);         /* col0 RGBX8 */
    GXSetVtxAttrFmt(0, 13, 1, 3, 5);         /* tex0 ST, S16, frac 5 */
}

static u8 *WriteBE16(u8 *p, u16 v)
{
    *p++ = (u8)(v >> 8);
    *p++ = (u8)v;
    return p;
}

static u8 *PutVertex(u8 *p, int id)
{
    p = WriteBE16(p, (u16)(id * 16));        /* pos x */
    p = WriteBE16(p, (u16)(id * 32));        /* pos y */
    p = WriteBE16(p, 0);                     /* pos z */
    *p++ = (u8)(id * 8);                     /* col r */
    *p++ = 40;
    *p++ = 60;
    *p++ = 255;
    p = WriteBE16(p, (u16)(id * 64));        /* tex s */
    p = WriteBE16(p, (u16)(id * 96));        /* tex t */
    return p;
}

/* One triangle list (vertices 0,1,2) plus one quad (3,4,5,6). */
static void BuildDl(int first)
{
    u8 *p = s_dl;

    *p++ = 0x90;                             /* GX_TRIANGLES, format 0 */
    *p++ = 0; *p++ = 3;
    p = PutVertex(p, first + 0);
    p = PutVertex(p, first + 1);
    p = PutVertex(p, first + 2);
    *p++ = 0x80;                             /* GX_QUADS, format 0 */
    *p++ = 0; *p++ = 4;
    p = PutVertex(p, first + 3);
    p = PutVertex(p, first + 4);
    p = PutVertex(p, first + 5);
    p = PutVertex(p, first + 6);
    *p++ = 0x00;                             /* NOP */
    s_dlSize = (u32)(p - s_dl);
}

/* A list shaped like the game's geometry: a run of triangle and quad batches
 * (vertices 0..10), then a strip (11,12,13), then more triangles (14,15,16). */
static void BuildRunDl(void)
{
    u8 *p = s_dl;
    int id = 0;

    *p++ = 0x90; *p++ = 0; *p++ = 3;             /* triangles 0,1,2 */
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    *p++ = 0x80; *p++ = 0; *p++ = 4;             /* quad 3,4,5,6 */
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    *p++ = 0x80; *p++ = 0; *p++ = 4;             /* quad 7,8,9,10 */
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    *p++ = 0x98; *p++ = 0; *p++ = 3;             /* strip 11,12,13 */
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    *p++ = 0x90; *p++ = 0; *p++ = 3;             /* triangles 14,15,16 */
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    p = PutVertex(p, id++);
    s_dlSize = (u32)(p - s_dl);
}

/* Indexed lists (fmt 1): the array contents are in host order, so a rewrite is
 * easy to make visible. */
static s16 s_posArr[16][3];
static u8 s_colArr[16][4];

static void FillArrays(int shift)
{
    int i;

    for (i = 0; i < 16; i++) {
        s_posArr[i][0] = (s16)(shift + i * 10);
        s_posArr[i][1] = (s16)(shift + i);
        s_posArr[i][2] = 0;
        s_colArr[i][0] = (u8)(i * 8);
        s_colArr[i][1] = 40;
        s_colArr[i][2] = 60;
        s_colArr[i][3] = 255;
    }
}

static void UseIndexedFmt(void)
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, GX_INDEX8);
    GXSetVtxDesc(11, GX_INDEX8);
    GXSetVtxAttrFmt(1, 9, 1, 3, 0);          /* fmt 1: pos XYZ, S16 */
    GXSetVtxAttrFmt(1, 11, 1, 2, 0);         /* fmt 1: col0 RGBX8 */
    GXSetArray(9, s_posArr, sizeof(s_posArr[0]));
    GXSetArray(11, s_colArr, sizeof(s_colArr[0]));
}

/* Two triangles, all attributes indexed; 2 index bytes per vertex. */
static void BuildIndexedDlAt(u8 *dst)
{
    u8 *p = dst;

    *p++ = 0x90 | 1;                         /* GX_TRIANGLES, format 1 */
    *p++ = 0; *p++ = 6;
    *p++ = 0; *p++ = 0;
    *p++ = 1; *p++ = 1;
    *p++ = 2; *p++ = 2;
    *p++ = 2; *p++ = 2;
    *p++ = 3; *p++ = 3;
    *p++ = 4; *p++ = 4;
    s_dlSize = (u32)(p - dst);
}

static void BuildIndexedDl(void)
{
    BuildIndexedDlAt(s_dl);
}

/* Same again with positions as F32: any id survives, which the stress test
 * below needs. */
static u8 *WriteBEf32(u8 *p, f32 v)
{
    u32 u;

    memcpy(&u, &v, 4);
    p = WriteBE16(p, (u16)(u >> 16));
    return WriteBE16(p, (u16)u);
}

static void UseDirectFmtF32(void)
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, GX_DIRECT);              /* GX_VA_POS */
    GXSetVtxDesc(11, GX_DIRECT);             /* GX_VA_CLR0 */
    GXSetVtxAttrFmt(0, 9, 1, 4, 0);          /* pos XYZ, F32 */
    GXSetVtxAttrFmt(0, 11, 1, 2, 0);         /* col0 RGBX8 */
}

/* One list of 9 vertices, numbered `first` .. `first` + 8, written at `dst`. */
static u32 BuildDlF32At(u8 *dst, int first)
{
    u8 *p = dst;
    int j;

    *p++ = 0x90;                             /* GX_TRIANGLES, format 0 */
    *p++ = 0; *p++ = 9;
    for (j = 0; j < 9; j++) {
        int id = first + j;

        p = WriteBEf32(p, (f32)id);
        p = WriteBEf32(p, 0.0f);
        p = WriteBEf32(p, 0.0f);
        *p++ = (u8)id;
        *p++ = 40;
        *p++ = 60;
        *p++ = 255;
    }
    return (u32)(p - dst);
}

/* One list of 9 vertices, numbered `first` .. `first` + 8. */
static void BuildDlF32(int first)
{
    s_dlSize = BuildDlF32At(s_dl, first);
}

static void ResetDraws(void) { s_ndraws = 0; }

/* The vertex id a recorded vertex stands for (its position x). */
static int Id(const GXVertex *v)
{
    return (int)(v->pos[0] + 0.5f);
}

static void Stats(u32 *hit, u32 *miss, u32 *drop, u32 *abort)
{
    u32 flush;

    GX_FifoStats(hit, miss, drop, abort, &flush);
}

/* ---- the tests ----------------------------------------------------------- */

static void Test_DecodeOnce(void)
{
    u32 hit, miss, drop, ab, used0;

    printf("each list decodes once\n");
    UseDirectFmt();
    BuildDl(0);
    ResetDraws();
    Stats(&hit, &miss, &drop, &ab);
    used0 = GX_FifoPoolUsed();

    GXCallDisplayList(s_dl, s_dlSize);
    Check(s_ndraws == 1, "the triangle and the quad became one draw");
    Check(GX_FifoPoolUsed() == used0 + 9, "9 vertices went to the pool");
    Stats(&hit, &miss, &drop, &ab);
    Check(miss == 1 && hit == 0, "the first call was a miss");

    ResetDraws();
    GXCallDisplayList(s_dl, s_dlSize);
    Check(s_ndraws == 1, "the second call draws the same draw");
    Check(GX_FifoPoolUsed() == used0 + 9, "the second call decodes nothing");
    Stats(&hit, &miss, &drop, &ab);
    Check(hit == 1 && miss == 1, "the second call was a hit");

    ResetDraws();
    GXCallDisplayList(s_dl, s_dlSize);
    Stats(&hit, &miss, &drop, &ab);
    Check(hit == 2, "a third call hits too");
}

static void Test_QuadOrder(void)
{
    static const int want[9] = {0, 1, 2, 3, 4, 5, 3, 5, 6};
    const GXDrawCall *q = &s_draws[0];
    int i, ok = 1;

    printf("quads are stored already expanded\n");
    if (s_ndraws < 1) {
        Check(0, "expected a draw");
        return;
    }
    Check(q->prim == GXR_TRIANGLES && q->n == 9, "3 triangle vertices and a quad of 6");
    for (i = 0; i < 9 && ok; i++) {
        if (Id(&q->v[i]) != want[i])
            ok = 0;
    }
    Check(ok, "the quad as v0,v1,v2,v0,v2,v3, after the triangle");
}

static void Test_PoolBatch(void)
{
    const GXVtxBatch *b;
    const GXVertex *prev;
    int i, ok = 1;

    printf("cached draws hand over the vertex pool\n");
    for (i = 0; i < s_ndraws; i++) {
        if (s_draws[i].batch == NULL)
            ok = 0;
    }
    Check(ok, "every cached draw carries a batch");
    if (!ok || s_ndraws == 0)
        return;
    b = s_draws[0].batch;
    Check(b->used == GX_FifoPoolUsed(), "the batch covers the whole pool");
    Check(GX_FifoPoolCapacity(b->used) == 1, "the pool has room for it");
    Check(s_draws[0].v >= b->base && s_draws[0].v + s_draws[0].n <= b->base + b->used,
          "the draw's vertices are inside it");
    prev = s_draws[0].v;

    BuildDl(70);                           /* a second list, same address */
    ResetDraws();
    GXCallDisplayList(s_dl, s_dlSize);
    Check(s_ndraws == 1 && s_draws[0].batch == b, "the next list uses the same pool");
    Check(s_draws[0].v > prev, "and a range of its own");
    Check(s_draws[0].batch->used == GX_FifoPoolUsed(), "the batch grew with the pool");
}

static void Test_RewrittenList(void)
{
    u32 h0, m0, h1, m1, drop, ab;
    int before;

    printf("rewriting a list's bytes is a miss\n");
    ResetDraws();
    BuildDl(100);                     /* same size, same address, new bytes */
    Stats(&h0, &m0, &drop, &ab);
    before = s_ndraws;
    GXCallDisplayList(s_dl, s_dlSize);
    Stats(&h1, &m1, &drop, &ab);
    Check(s_ndraws == before + 1, "the rewritten list draws again");
    Check(m1 == m0 + 1 && h1 == h0, "and it counted as a miss");
    if (s_ndraws >= before + 1)
        Check(Id(&s_draws[before].v[0]) == 100, "with the new vertex data");
}

static void Test_ArrayInvalidate(void)
{
    u32 hit, miss, drop, drop2, ab;

    printf("rewriting a vertex array drops the lists that read it\n");
    UseIndexedFmt();
    FillArrays(0);
    BuildIndexedDl();
    ResetDraws();
    GXCallDisplayList(s_dl, s_dlSize);
    Check(s_ndraws == 1, "the indexed list draws one 6 vertex triangle list");
    if (s_ndraws == 0)
        return;
    Check(Id(&s_draws[0].v[0]) == 0 && Id(&s_draws[0].v[5]) == 40,
          "the vertices come out of the array");

    Stats(&hit, &miss, &drop, &ab);
    FillArrays(1000);                 /* the game wrote new data over the array */
    Port_InvalidateDisplayLists(s_posArr, sizeof(s_posArr));
    Stats(&hit, &miss, &drop2, &ab);
    Check(drop2 == drop + 1, "the list that indexed it was dropped");

    ResetDraws();
    GXCallDisplayList(s_dl, s_dlSize);
    Check(s_ndraws == 1, "the list draws again");
    if (s_ndraws)
        Check(Id(&s_draws[0].v[0]) == 1000, "from the new array contents");
}

static void Test_Immediate(void)
{
    int i;

    printf("immediate mode is not cached\n");
    UseDirectFmt();
    ResetDraws();
    GXBegin(GX_TRIANGLES, 0, 3);
    for (i = 0; i < 3; i++) {
        GXWGFifo_s16((s16)(i * 16));
        GXWGFifo_s16((s16)(i * 32));
        GXWGFifo_s16(0);
        GXWGFifo_u8((u8)(i * 8));
        GXWGFifo_u8(40);
        GXWGFifo_u8(60);
        GXWGFifo_u8(255);
        GXWGFifo_s16((s16)(i * 64));
        GXWGFifo_s16((s16)(i * 96));
    }
    Check(s_ndraws == 1, "GXBegin plus the FIFO draws once");
    if (s_ndraws)
        Check(s_draws[0].batch == NULL, "and is not batched");
}

/* The compact vertex: normals are stored as s8 at fixed scale 1/64, colors
 * as 8 bit with the same bit replication the hardware color registers use.
 * Three direct formats, one per normal type, so a single vertex description
 * covers S8, S16 and F32 normals and RGB565, RGBX8, RGBA4 and RGBA6 colors. */
static void UseEncodeFmt(void)
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, GX_DIRECT);              /* GX_VA_POS  */
    GXSetVtxDesc(10, GX_DIRECT);             /* GX_VA_NRM  */
    GXSetVtxDesc(11, GX_DIRECT);             /* GX_VA_CLR0 */
    GXSetVtxDesc(12, GX_DIRECT);             /* GX_VA_CLR1 */
    GXSetVtxDesc(13, GX_DIRECT);             /* GX_VA_TEX0 */

    GXSetVtxAttrFmt(0, 9, 1, 3, 4);          /* fmt 0: pos S16, frac 4 */
    GXSetVtxAttrFmt(0, 10, 0, 1, 0);         /* nrm XYZ, S8 */
    GXSetVtxAttrFmt(0, 11, 0, 0, 0);         /* clr0 RGB565 */
    GXSetVtxAttrFmt(0, 12, 1, 3, 0);         /* clr1 RGBA4, alpha from data */
    GXSetVtxAttrFmt(0, 13, 1, 3, 5);         /* tex0 ST, S16, frac 5 */

    GXSetVtxAttrFmt(1, 9, 1, 3, 4);          /* fmt 1: pos S16, frac 4 */
    GXSetVtxAttrFmt(1, 10, 0, 3, 0);         /* nrm XYZ, S16 at exponent 14 */
    GXSetVtxAttrFmt(1, 11, 0, 2, 0);         /* clr0 RGBX8, alpha ignored */
    GXSetVtxAttrFmt(1, 12, 0, 3, 0);         /* clr1 RGBA4, alpha ignored */
    GXSetVtxAttrFmt(1, 13, 1, 3, 5);         /* tex0 ST, S16, frac 5 */

    GXSetVtxAttrFmt(2, 9, 1, 4, 0);          /* fmt 2: pos F32 */
    GXSetVtxAttrFmt(2, 10, 0, 4, 0);         /* nrm XYZ, F32 */
    GXSetVtxAttrFmt(2, 11, 0, 0, 0);         /* clr0 RGB565 */
    GXSetVtxAttrFmt(2, 12, 1, 4, 0);         /* clr1 RGBA6, alpha from data */
    GXSetVtxAttrFmt(2, 13, 1, 4, 0);         /* tex0 ST, F32 */
}

static void BuildEncodeDl(int fmt)
{
    static const s8 s8nrm[3][3] = { {64, -64, -128}, {127, -127, 0}, {-1, 0, 1} };
    static const s16 s16nrm[3][3] = { {8192, -16384, 4224}, {-4224, 4096, 0}, {16383, -1, 255} };
    static const f32 f32nrm[3][3] = { {1.0f, -0.25f, 0.999f}, {0.0f, 0.5f, -0.999f}, {2.0f, -1.99f, 0.01f} };
    static const u16 c565[3] = {0x7BEF, 0x0000, 0x0841};
    static const u16 c4[3] = {0xFEDC, 0x0F0F, 0x1234};
    static const u16 c4no[3] = {0x0123, 0xF0F0, 0xFFFF};
    static const u8 rgbx8[3][4] = { {17, 34, 51, 0xA5}, {200, 201, 202, 0}, {0, 0, 0, 0} };
    static const u8 a6[3][3] = { {0xFE, 0x04, 0x01}, {0, 0, 0}, {0xFF, 0xFF, 0xFF} };
    u8 *p = s_dl;
    int j;

    *p++ = (u8)(0x90 | fmt);                 /* GX_TRIANGLES */
    *p++ = 0; *p++ = 3;
    for (j = 0; j < 3; j++) {
        if (fmt == 2) {
            p = WriteBEf32(p, (f32)j);      /* pos x: F32, so comes out as `j` */
            p = WriteBEf32(p, 0.0f);
            p = WriteBEf32(p, 0.0f);
        } else {
            p = WriteBE16(p, (u16)(j * 16)); /* pos x: S16 frac 4, so as `j` too */
            p = WriteBE16(p, (u16)(j * 32));
            p = WriteBE16(p, 0);
        }
        if (fmt == 0) {
            *p++ = (u8)s8nrm[j][0];
            *p++ = (u8)s8nrm[j][1];
            *p++ = (u8)s8nrm[j][2];
            p = WriteBE16(p, c565[j]);
            p = WriteBE16(p, c4[j]);
            p = WriteBE16(p, (u16)(j * 32)); /* tex0 s: frac 5 -> s = j */
            p = WriteBE16(p, (u16)(j * 64)); /* t = 2j */
        } else if (fmt == 1) {
            p = WriteBE16(p, (u16)s16nrm[j][0]);
            p = WriteBE16(p, (u16)s16nrm[j][1]);
            p = WriteBE16(p, (u16)s16nrm[j][2]);
            *p++ = rgbx8[j][0];
            *p++ = rgbx8[j][1];
            *p++ = rgbx8[j][2];
            *p++ = rgbx8[j][3];
            p = WriteBE16(p, c4no[j]);
            p = WriteBE16(p, (u16)(j * 32));
            p = WriteBE16(p, (u16)(j * 64));
        } else {
            p = WriteBEf32(p, f32nrm[j][0]);
            p = WriteBEf32(p, f32nrm[j][1]);
            p = WriteBEf32(p, f32nrm[j][2]);
            p = WriteBE16(p, c565[j]);
            *p++ = a6[j][0];
            *p++ = a6[j][1];
            *p++ = a6[j][2];
            p = WriteBEf32(p, (f32)j * 0.5f);
            p = WriteBEf32(p, (f32)j * -0.25f);
        }
    }
    s_dlSize = (u32)(p - s_dl);
}

static void Test_AttrEncode(void)
{
    /* nrm: S8 arrives untouched except -128 -> -127; S16 lands at raw/256
     * (exponent 14 rescaled to 1/64), half away from zero; F32 is scaled by
     * 64 and clamped to s8. */
    static const s8 wantN8[3][3] = { {64, -64, -127}, {127, -127, 0}, {-1, 0, 1} };
    static const s8 wantN16[3][3] = { {32, -64, 17}, {-17, 16, 0}, {64, 0, 1} };
    static const s8 wantNF[3][3] = { {64, -16, 64}, {0, 32, -64}, {127, -127, 1} };
    /* colors: each channel widened to 8 bits by bit replication, so the
     * GPU's val/255 is exactly the value TEV would have seen. */
    static const u8 want565[3][4] = { {123, 125, 123, 255}, {0, 0, 0, 255}, {8, 8, 8, 255} };
    static const u8 wantX8[3][4] = { {17, 34, 51, 255}, {200, 201, 202, 255}, {0, 0, 0, 255} };
    static const u8 wantA4[3][4] = { {255, 238, 221, 204}, {0, 255, 0, 255}, {17, 34, 51, 68} };
    static const u8 wantA4no[3][4] = { {0, 17, 34, 255}, {255, 0, 255, 255}, {255, 255, 255, 255} };
    static const u8 wantA6[3][4] = { {255, 130, 65, 4}, {0, 0, 0, 0}, {255, 255, 255, 255} };
    int f, j, i, ok;

    printf("attributes decode into the compact vertex\n");
    UseEncodeFmt();
    for (f = 0; f < 3; f++) {
        const GXDrawCall *q;
        const s8 (*wn)[3] = f == 0 ? wantN8 : f == 1 ? wantN16 : wantNF;
        const u8 (*wc0)[4] = f == 1 ? wantX8 : want565;
        const u8 (*wc1)[4] = f == 0 ? wantA4 : f == 1 ? wantA4no : wantA6;

        BuildEncodeDl(f);
        ResetDraws();
        GXCallDisplayList(s_dl, s_dlSize);
        if (s_ndraws != 1) {
            Check(0, "fmt %d drew one triangle", f);
            return;
        }
        q = &s_draws[0];
        ok = 1;
        for (j = 0; j < 3 && ok; j++) {
            if (Id(&q->v[j]) != j)
                ok = 0;
            for (i = 0; i < 3; i++) {
                if (q->v[j].nrm[i] != wn[j][i])
                    ok = 0;
            }
            for (i = 0; i < 4; i++) {
                if (q->v[j].clr[0][i] != wc0[j][i] || q->v[j].clr[1][i] != wc1[j][i])
                    ok = 0;
            }
        }
        Check(ok, "fmt %d: normals, both color channels, positions", f);
        ok = 1;
        for (j = 0; j < 3 && ok; j++) {
            f32 es = f == 2 ? (f32)j * 0.5f : (f32)j;
            f32 et = f == 2 ? (f32)j * -0.25f : (f32)(2 * j);

            if (q->v[j].tex[0][0] != es || q->v[j].tex[0][1] != et)
                ok = 0;
            if (q->v[j].nrm[3] != 0)
                ok = 0;
            for (i = 1; i < 8; i++) {
                if (q->v[j].tex[i][0] != 0.0f || q->v[j].tex[i][1] != 0.0f)
                    ok = 0;
            }
        }
        Check(ok, "fmt %d: texture coords, unused normal w and tex1..7", f);
    }
}

static void Test_Merge(void)
{
    static const int a[] = {0, 1, 2,   3, 4, 5, 3, 5, 6,   7, 8, 9, 7, 9, 10};
    static const int b[] = {11, 12, 13};
    static const int c[] = {14, 15, 16};
    int i, ok = 1;

    printf("adjacent batches of the same kind become one draw\n");
    UseDirectFmt();
    BuildRunDl();
    ResetDraws();
    GXCallDisplayList(s_dl, s_dlSize);
    Check(s_ndraws == 3, "a triangle list, two quad batches, a strip and triangles: 3 draws");
    if (s_ndraws != 3)
        return;
    Check(s_draws[0].prim == GXR_TRIANGLES && s_draws[0].n == 15,
          "the triangles and both quads came to one 15 vertex triangle draw");
    Check(s_draws[1].prim == GXR_TRIANGLESTRIP, "the strip stayed on its own");
    Check(s_draws[2].prim == GXR_TRIANGLES && s_draws[2].n == 3, "so did the triangles after it");
    for (i = 0; i < 15 && ok; i++) {
        if (Id(&s_draws[0].v[i]) != a[i])
            ok = 0;
    }
    for (i = 0; i < 3 && ok; i++) {
        if (Id(&s_draws[1].v[i]) != b[i] || Id(&s_draws[2].v[i]) != c[i])
            ok = 0;
    }
    Check(ok, "with every vertex where it was in the list");
    Check(s_draws[1].v == s_draws[0].v + 15 && s_draws[2].v == s_draws[1].v + 3,
          "the three ranges sit back to back in the pool");
}

/* Is HWVX_DLVERIFY set? That is the cache's safety net for the one case the
 * port cannot see coming: vertex array contents rewritten in place. */
static int VerifyOn(void)
{
    return SDL_getenv("HWVX_DLVERIFY") != NULL;
}

static void Test_StaleArray(void)
{
    static u8 list[64]; /* not s_dl: Test_ArrayInvalidate left its entry there */
    u32 h0, m0, d0, a0, h1, m1, d1, a1, used0;

    printf("an array rewritten without saying so\n");
    UseIndexedFmt();
    FillArrays(0);
    BuildIndexedDlAt(list);
    ResetDraws();
    GXCallDisplayList(list, s_dlSize);       /* miss: cached */
    used0 = GX_FifoPoolUsed();

    Stats(&h0, &m0, &d0, &a0);
    FillArrays(2000);                        /* nothing reported the write */
    ResetDraws();
    GXCallDisplayList(list, s_dlSize);
    Stats(&h1, &m1, &d1, &a1);
    Check(GX_FifoPoolUsed() == used0, "the cache did not decode anything new");
    if (VerifyOn()) {
        Check(s_ndraws == 1 && Id(&s_draws[0].v[0]) == 2000,
              "verify mode caught it and used the fresh vertices");
        Check(h1 == h0 && m1 == m0, "verify mode counted it as neither hit nor miss");
    } else {
        Check(s_ndraws == 1 && Id(&s_draws[0].v[0]) == 0,
              "without verify mode the old vertices are replayed (the known gap)");
        Check(h1 == h0 + 1, "which reads as a hit");
    }
    /* Port_InvalidateDisplayLists is what closes the gap for game memory whose
     * writes the port does see; Test_ArrayInvalidate checks that. */
    Port_InvalidateDisplayLists(s_posArr, sizeof(s_posArr));
    ResetDraws();
    GXCallDisplayList(list, s_dlSize);
    Check(s_ndraws == 1 && Id(&s_draws[0].v[0]) == 2000,
          "invalidating the array makes it right again");
}

/* The game rebuilds one list at one address every frame: capture can never
 * pay off there. After three tries the cache calls the address volatile and
 * decodes inline, so the pool stops growing. When the bytes settle, capture
 * resumes and the call after it replays. */
static void Test_Volatile(void)
{
    static u8 list[192]; /* a private address: no other list shares it */
    u32 size = 0, drop;
    u32 h0, m0, ab0, fl0, h1, m1, ab1, fl1;
    u32 dfirst, dreb, dstab, dfmt, darr, dmtx, sk0, sk1;
    u32 usedx;
    int i, j, ok = 1;

    printf("a list rebuilt at one address stops being captured\n");
    UseDirectFmtF32();
    GX_FifoStats(&h0, &m0, &drop, &ab0, &fl0);
    GX_FifoDiag(&dfirst, &dreb, &dstab, &dfmt, &darr, &dmtx, &sk0);

    for (i = 0; i < 8; i++) {
        size = BuildDlF32At(list, i * 9); /* same address, new bytes */
        ResetDraws();
        GXCallDisplayList(list, size);
        if (s_ndraws != 1) {
            ok = 0;
            break;
        }
        for (j = 0; j < 9; j++) {
            if (Id(&s_draws[0].v[j]) != i * 9 + j) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            break;
        if (i == 3)
            usedx = GX_FifoPoolUsed(); /* volatile has kicked in by now */
    }
    Check(ok, "all 8 rebuilt lists drew their own vertices");
    size = BuildDlF32At(list, 8 * 9);
    ResetDraws();
    GXCallDisplayList(list, size); /* still one more byte change: still skipped */
    Check(s_ndraws == 1 && Id(&s_draws[0].v[8]) == 8 * 9 + 8, "and so is the next rebuild");

    GX_FifoStats(&h1, &m1, &drop, &ab1, &fl1);
    GX_FifoDiag(&dfirst, &dreb, &dstab, &dfmt, &darr, &dmtx, &sk1);
    Check(h1 == h0, "none of it ever replayed");
    Check(m1 == m0 + 9, "each call is a miss");
    Check(sk1 == sk0 + 6, "6 of the 9 calls skipped capture");
    Check(GX_FifoPoolUsed() == usedx, "the pool stopped growing once it went volatile");

    /* The bytes settle: the next call captures again, the one after replays. */
    ResetDraws();
    GXCallDisplayList(list, size); /* bytes match the last call: capture */
    ResetDraws();
    GXCallDisplayList(list, size); /* same as the entry just made: hit */
    GX_FifoStats(&h1, &m1, &drop, &ab1, &fl1);
    GX_FifoDiag(&dfirst, &dreb, &dstab, &dfmt, &darr, &dmtx, &sk1);
    Check(ok && s_ndraws == 1 && Id(&s_draws[0].v[0]) == 8 * 9, "the settled list drew right");
    Check(h1 == h0 + 1, "caching resumed once the bytes stopped changing");
    Check(sk1 == sk0 + 6, "no more captures were skipped");
}

/* Many different lists at different addresses: more than the pool holds, so
 * it has to start over without losing or repeating a vertex. Distinct
 * addresses on purpose: the cache only calls a list volatile when the bytes
 * under one address keep changing, and these lists are all different once. */
#define STRESS_LISTS 40000
#define STRESS_STRIDE 160 /* one 9 vertex F32 list is 147 bytes */
static u8 s_stress[STRESS_LISTS * STRESS_STRIDE];

static void Test_PoolFull(void)
{
    u32 gens[16];
    int ngen = 0, i, ok = 1, resets = 0;

    printf("the pool starts over when it fills up\n");
    UseDirectFmtF32();
    for (i = 0; i < STRESS_LISTS; i++) {
        const GXVtxBatch *b;
        u8 *list = s_stress + (size_t)i * STRESS_STRIDE;
        u32 size = BuildDlF32At(list, i * 9);
        int j;

        ResetDraws();
        GXCallDisplayList(list, size);
        if (s_ndraws != 1) {
            ok = 0;
            break;
        }
        for (j = 0; j < 9; j++) {
            if (Id(&s_draws[0].v[j]) != i * 9 + j) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            break;
        b = s_draws[0].batch;               /* NULL: this one decoded inline */
        if (b == NULL) {
            resets++;
            continue;
        }
        if (b->used > b->cap) {
            ok = 0;
            break;
        }
        for (j = 0; j < ngen; j++) {
            if (gens[j] == b->gen)
                break;
        }
        if (j == ngen && ngen < (int)(sizeof(gens) / sizeof(gens[0])))
            gens[ngen++] = b->gen;
    }
    Check(ok, "all %d lists drew their own vertices", STRESS_LISTS);
    Check(resets > 0, "the pool filled up and began again");
    Check(ngen >= 2, "the renderer was told a new generation (%d)", ngen);
}

int main(void)
{
    g_gxr = &s_stub;
    GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXSetZMode(0, 7, 0);                /* depth off */
    GXSetCullMode(0);

    Test_DecodeOnce();
    Test_QuadOrder();
    Test_Merge();
    Test_PoolBatch();
    Test_RewrittenList();
    Test_ArrayInvalidate();
    Test_StaleArray();
    Test_Immediate();
    Test_AttrEncode();
    Test_Volatile();
    Test_PoolFull();

    printf("%s: %d check(s) failed\n", s_fails ? "FAILED" : "PASSED", s_fails);
    return s_fails ? 1 : 0;
}
