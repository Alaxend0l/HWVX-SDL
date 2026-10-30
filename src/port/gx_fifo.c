/* GX command stream decoding: display lists and immediate-mode FIFO writes.
 *
 * Both arrive as big-endian GX commands. Immediate mode is GXBegin followed by
 * GXWGFifo_* writes of the vertex data; the vertices are decoded as soon as
 * all of them have arrived (the game never calls GXEnd). Indexed attributes
 * read from the arrays set with GXSetArray, whose components are in host
 * order (data packs are swapped on load, runtime arrays are written by C). */
#include "gx_internal.h"

/* ---- component readers -------------------------------------------------- */

typedef struct Reader {
    const u8 *p, *end;
    int bigEndian;
} Reader;

static u32 RdU8(Reader *r) { return r->p < r->end ? *r->p++ : 0; }

static u32 RdU16(Reader *r) {
    u32 v;
    if (r->p + 2 > r->end) {
        r->p = r->end;
        return 0;
    }
    v = r->bigEndian ? Port_ReadBE16(r->p) : *(const u16 *)(const void *)r->p;
    r->p += 2;
    return v;
}

static u32 RdU32(Reader *r) {
    u32 v;
    if (r->p + 4 > r->end) {
        r->p = r->end;
        return 0;
    }
    v = r->bigEndian ? Port_ReadBE32(r->p) : *(const u32 *)(const void *)r->p;
    r->p += 4;
    return v;
}

/* GX component types: U8, S8, U16, S16, F32 */
static f32 RdComp(Reader *r, int type, int frac) {
    f32 scale = 1.0f / (f32)(1 << frac);
    union { u32 u; f32 f; } c;
    switch (type) {
    case 0: return (f32)RdU8(r) * scale;
    case 1: return (f32)(s8)RdU8(r) * scale;
    case 2: return (f32)RdU16(r) * scale;
    case 3: return (f32)(s16)RdU16(r) * scale;
    default:
        c.u = RdU32(r);
        return c.f;
    }
}

static const int s_compSize[5] = { 1, 1, 2, 2, 4 };

/* GX color types: RGB565, RGB8, RGBX8, RGBA4, RGBA6, RGBA8 */
static void RdColor(Reader *r, int type, f32 *out) {
    u32 v;
    switch (type) {
    case 0:
        v = RdU16(r);
        out[0] = ((v >> 11) & 0x1F) / 31.0f;
        out[1] = ((v >> 5) & 0x3F) / 63.0f;
        out[2] = (v & 0x1F) / 31.0f;
        out[3] = 1.0f;
        break;
    case 1:
        out[0] = RdU8(r) / 255.0f;
        out[1] = RdU8(r) / 255.0f;
        out[2] = RdU8(r) / 255.0f;
        out[3] = 1.0f;
        break;
    case 2:
        out[0] = RdU8(r) / 255.0f;
        out[1] = RdU8(r) / 255.0f;
        out[2] = RdU8(r) / 255.0f;
        RdU8(r);
        out[3] = 1.0f;
        break;
    case 3:
        v = RdU16(r);
        out[0] = ((v >> 12) & 0xF) / 15.0f;
        out[1] = ((v >> 8) & 0xF) / 15.0f;
        out[2] = ((v >> 4) & 0xF) / 15.0f;
        out[3] = (v & 0xF) / 15.0f;
        break;
    case 4:
        v = (RdU8(r) << 16);
        v |= RdU8(r) << 8;
        v |= RdU8(r);
        out[0] = ((v >> 18) & 0x3F) / 63.0f;
        out[1] = ((v >> 12) & 0x3F) / 63.0f;
        out[2] = ((v >> 6) & 0x3F) / 63.0f;
        out[3] = (v & 0x3F) / 63.0f;
        break;
    default:
        out[0] = RdU8(r) / 255.0f;
        out[1] = RdU8(r) / 255.0f;
        out[2] = RdU8(r) / 255.0f;
        out[3] = RdU8(r) / 255.0f;
        break;
    }
}

static const int s_colorSize[6] = { 2, 3, 4, 2, 3, 4 };

/* Size of one attribute's data as it appears directly in the stream. */
static int DirectSize(int attr, const GXVtxAttrFmt *f) {
    if (attr <= 8) {
        return 1;                                   /* matrix indices */
    }
    switch (attr) {
    case GX_VA_POS: return (f->cnt ? 3 : 2) * s_compSize[f->type > 4 ? 4 : f->type];
    case GX_VA_NRM: return (f->cnt ? 9 : 3) * s_compSize[f->type > 4 ? 4 : f->type];
    case GX_VA_CLR0:
    case GX_VA_CLR1: return s_colorSize[f->type > 5 ? 5 : f->type];
    default: return (f->cnt ? 2 : 1) * s_compSize[f->type > 4 ? 4 : f->type];
    }
}

static int VertexSize(int fmt) {
    int size = 0, a;
    for (a = 0; a < GX_NUM_ATTR; a++) {
        switch (g_gx.vcd[a]) {
        case GX_DIRECT: size += DirectSize(a, &g_gx.vat[fmt][a]); break;
        case GX_INDEX8: size += 1; break;
        case GX_INDEX16: size += 2; break;
        }
    }
    return size;
}

/* Decode one attribute from `r` (direct data) into `v`. */
static void DecodeAttr(Reader *r, int attr, const GXVtxAttrFmt *f, GXVertex *v) {
    int i, n;

    if (attr == GX_VA_PNMTXIDX) {
        v->mtxIdx = (f32)(RdU8(r) / 3);
        return;
    }
    if (attr <= 8) {
        RdU8(r);                                    /* texture matrix index: unused */
        return;
    }
    switch (attr) {
    case GX_VA_POS:
        v->pos[0] = RdComp(r, f->type, f->frac);
        v->pos[1] = RdComp(r, f->type, f->frac);
        v->pos[2] = f->cnt ? RdComp(r, f->type, f->frac) : 0.0f;
        break;
    case GX_VA_NRM: {
        /* normals: fixed exponent 6 (S8) or 14 (S16) */
        int frac = f->type == 1 ? 6 : f->type == 3 ? 14 : 0;
        n = f->cnt ? 9 : 3;
        for (i = 0; i < n; i++) {
            f32 c = RdComp(r, f->type, frac);
            if (i < 3) {
                v->nrm[i] = c;
            }
        }
        break;
    }
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        RdColor(r, f->type, v->clr[attr - GX_VA_CLR0]);
        if (f->cnt == 0) {
            v->clr[attr - GX_VA_CLR0][3] = 1.0f;
        }
        break;
    default:
        n = attr - GX_VA_TEX0;
        v->tex[n][0] = RdComp(r, f->type, f->frac);
        v->tex[n][1] = f->cnt ? RdComp(r, f->type, f->frac) : 0.0f;
        break;
    }
}

static void DecodeVertex(Reader *r, int fmt, GXVertex *v) {
    int a;

    memset(v, 0, sizeof(*v));
    v->mtxIdx = (f32)(g_gx.curMtx / 3);
    v->clr[0][0] = v->clr[0][1] = v->clr[0][2] = v->clr[0][3] = 1.0f;
    for (a = 0; a < GX_NUM_ATTR; a++) {
        const GXVtxAttrFmt *f = &g_gx.vat[fmt][a];
        u32 idx;

        switch (g_gx.vcd[a]) {
        case GX_DIRECT:
            DecodeAttr(r, a, f, v);
            break;
        case GX_INDEX8:
        case GX_INDEX16:
            idx = g_gx.vcd[a] == GX_INDEX8 ? RdU8(r) : RdU16(r);
            if (g_gx.array[a] != NULL) {
                Reader ar;
                ar.p = g_gx.array[a] + idx * g_gx.arrayStride[a];
                ar.end = ar.p + 64;
                ar.bigEndian = 0;
                DecodeAttr(&ar, a, f, v);
            }
            break;
        }
    }
}

/* ---- draws ------------------------------------------------------------- */

static GXVertex *s_verts;
static u32 s_vertsCap;

static void DrawPrims(Reader *r, int prim, int fmt, u32 count) {
    u32 i;

    if (count > s_vertsCap) {
        s_vertsCap = count + 1024;
        s_verts = SDL_realloc(s_verts, s_vertsCap * sizeof(GXVertex));
    }
    for (i = 0; i < count; i++) {
        DecodeVertex(r, fmt, &s_verts[i]);
    }
    GX_Draw(prim, s_verts, count);
}

/* ---- command streams ---------------------------------------------------- */

void GX_RunCommands(const u8 *cmds, u32 size) {
    Reader r;

    r.p = cmds;
    r.end = cmds + size;
    r.bigEndian = 1;
    while (r.p < r.end) {
        u32 op = RdU8(&r);
        u32 n;

        switch (op) {
        case 0x00: /* NOP */
        case 0x48: /* invalidate vertex cache */
            break;
        case 0x08: /* load CP register */
            r.p += 5;
            break;
        case 0x10: /* load XF registers */
            n = (RdU32(&r) >> 16) + 1;
            r.p += n * 4;
            break;
        case 0x20: case 0x28: case 0x30: case 0x38: /* indexed XF loads */
            r.p += 4;
            break;
        case 0x40: /* call display list */
            r.p += 8;
            break;
        case 0x61: /* load BP register */
            r.p += 4;
            break;
        default:
            if (op >= 0x80 && op < 0xC0) {
                n = RdU16(&r);
                DrawPrims(&r, op & 0xF8, op & 7, n);
            } else {
                Port_Log("GX: unknown display list opcode 0x%02X", op);
                return;
            }
            break;
        }
    }
}

/* ---- immediate mode ----------------------------------------------------- */

static struct {
    int active, prim, fmt;
    u32 nverts, need, have;
    u8 *buf;
    u32 cap;
} s_imm;

void GX_FifoBegin(int prim, int vtxfmt, u16 nverts) {
    s_imm.active = 1;
    s_imm.prim = prim;
    s_imm.fmt = vtxfmt & 7;
    s_imm.nverts = nverts;
    s_imm.need = (u32)VertexSize(s_imm.fmt) * nverts;
    s_imm.have = 0;
    if (s_imm.need > s_imm.cap) {
        s_imm.cap = s_imm.need + 4096;
        s_imm.buf = SDL_realloc(s_imm.buf, s_imm.cap);
    }
    if (s_imm.need == 0) {
        s_imm.active = 0;
    }
}

/* `bytes` are already big-endian, like the hardware FIFO. */
void GX_FifoWrite(const void *bytes, u32 n) {
    if (!s_imm.active) {
        return;
    }
    if (s_imm.have + n > s_imm.need) {
        n = s_imm.need - s_imm.have;
    }
    memcpy(s_imm.buf + s_imm.have, bytes, n);
    s_imm.have += n;
    if (s_imm.have == s_imm.need) {
        Reader r;
        r.p = s_imm.buf;
        r.end = s_imm.buf + s_imm.need;
        r.bigEndian = 1;
        s_imm.active = 0;
        DrawPrims(&r, s_imm.prim, s_imm.fmt, s_imm.nverts);
    }
}
