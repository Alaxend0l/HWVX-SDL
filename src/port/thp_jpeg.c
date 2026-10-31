/* Baseline JPEG decoder for THP movie frames (thp.c).
 *
 * THP video frames are baseline JPEGs (SOI, DQT, DHT, SOF0, SOS) with one
 * difference: the entropy-coded data does not stuff 0x00 after 0xFF bytes,
 * so every byte after SOS is data until the end of the frame. Restart markers
 * are not used. Covers what the files need: 8-bit samples, Huffman coding,
 * any H/V sampling up to 2x2 (the movies use 4:2:0), up to 3 components.
 * Output is RGBA, top row first. */
#include "port_internal.h"
#include <math.h>

typedef struct Huff {
    u8 lookup[1 << 9];     /* first 9 bits -> code length (0: longer) */
    u8 lookupSym[1 << 9];
    s32 maxcode[18];       /* largest code of each length, -1 if none */
    s32 valptr[17];
    s32 mincode[17];
    u8 symbols[256];
} Huff;

typedef struct Comp {
    int id, h, v, tq, td, ta;
    int pred;
    int bw, bh;            /* plane size in 8x8 blocks */
    u8 *plane;             /* decoded samples, bw*8 x bh*8 */
} Comp;

typedef struct Jpeg {
    const u8 *p, *end;
    u32 bits;
    int nbits;
    u16 q[4][64];
    Huff dc[4], ac[4];
    Comp comp[3];
    int ncomp, width, height, hmax, vmax;
} Jpeg;

static const u8 s_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static f32 s_idct[8][8];   /* c(u) * cos((2x + 1) u pi / 16) / 2 */

static void InitIDCT(void) {
    static int done;
    int x, u;

    if (done) {
        return;
    }
    for (x = 0; x < 8; x++) {
        for (u = 0; u < 8; u++) {
            f32 c = u == 0 ? (f32)(1.0 / sqrt(2.0)) : 1.0f;
            s_idct[x][u] = 0.5f * c * (f32)cos((2 * x + 1) * u * M_PI / 16.0);
        }
    }
    done = 1;
}

static void BuildHuff(Huff *h, const u8 *counts, const u8 *syms) {
    int len, i, k = 0, code = 0;

    memset(h->lookup, 0, sizeof(h->lookup));
    for (len = 1; len <= 16; len++) {
        h->valptr[len] = k;
        h->mincode[len] = code;
        for (i = 0; i < counts[len - 1]; i++, k++, code++) {
            h->symbols[k] = syms[k];
            if (len <= 9) {
                int fill = 1 << (9 - len), base = code << (9 - len), j;
                for (j = 0; j < fill; j++) {
                    h->lookup[base + j] = (u8)len;
                    h->lookupSym[base + j] = syms[k];
                }
            }
        }
        h->maxcode[len] = counts[len - 1] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
}

/* THP: no byte stuffing, so the bit reader takes the bytes as they are. */
static void Fill(Jpeg *j) {
    while (j->nbits <= 24) {
        u32 b = j->p < j->end ? *j->p++ : 0;
        j->bits |= b << (24 - j->nbits);
        j->nbits += 8;
    }
}

static int GetBits(Jpeg *j, int n) {
    u32 v;

    if (n == 0) {
        return 0;
    }
    Fill(j);
    v = j->bits >> (32 - n);
    j->bits <<= n;
    j->nbits -= n;
    return (int)v;
}

static int Extend(int v, int n) {
    return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
}

static int DecodeHuff(Jpeg *j, const Huff *h) {
    int len, code;

    Fill(j);
    code = (int)(j->bits >> (32 - 9));
    len = h->lookup[code];
    if (len != 0) {
        j->bits <<= len;
        j->nbits -= len;
        return h->lookupSym[code];
    }
    code = GetBits(j, 9);
    for (len = 10; len <= 16; len++) {
        code = (code << 1) | GetBits(j, 1);
        if (code <= h->maxcode[len]) {
            return h->symbols[h->valptr[len] + code - h->mincode[len]];
        }
    }
    return 0;   /* corrupt data: decode as zero */
}

static void DecodeBlock(Jpeg *j, Comp *c, u8 *out, int stride) {
    f32 coef[64], tmp[64];
    const u16 *q = j->q[c->tq];
    int k, x, y, u, t;

    memset(coef, 0, sizeof(coef));
    t = DecodeHuff(j, &j->dc[c->td]);
    c->pred += t ? Extend(GetBits(j, t), t) : 0;
    coef[0] = (f32)(c->pred * q[0]);
    for (k = 1; k < 64;) {
        int rs = DecodeHuff(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15) {
                break;   /* end of block */
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) {
            break;
        }
        coef[s_zigzag[k]] = (f32)(Extend(GetBits(j, s), s) * q[k]);
        k++;
    }
    /* separable IDCT: rows, then columns */
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            f32 sum = 0.0f;
            for (u = 0; u < 8; u++) {
                sum += s_idct[x][u] * coef[y * 8 + u];
            }
            tmp[y * 8 + x] = sum;
        }
    }
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            f32 sum = 128.0f;
            int v;
            for (u = 0; u < 8; u++) {
                sum += s_idct[y][u] * tmp[u * 8 + x];
            }
            v = (int)(sum + 0.5f);
            out[y * stride + x] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

static u16 Be16(const u8 *p) {
    return (u16)((p[0] << 8) | p[1]);
}

static int ParseHeaders(Jpeg *j) {
    const u8 *p = j->p;

    if (p + 2 > j->end || p[0] != 0xFF || p[1] != 0xD8) {
        return 0;
    }
    p += 2;
    while (p + 4 <= j->end) {
        u8 m;
        u16 len;
        const u8 *seg;

        if (p[0] != 0xFF) {
            return 0;
        }
        m = p[1];
        len = Be16(p + 2);
        seg = p + 4;
        if (seg + len - 2 > j->end) {
            return 0;
        }
        switch (m) {
        case 0xDB: {   /* DQT */
            const u8 *s = seg;
            while (s < seg + len - 2) {
                int pq = s[0] >> 4, tq = s[0] & 3, k;
                s++;
                for (k = 0; k < 64; k++) {
                    j->q[tq][k] = pq ? Be16(s + k * 2) : s[k];
                }
                s += pq ? 128 : 64;
            }
            break;
        }
        case 0xC4: {   /* DHT */
            const u8 *s = seg;
            while (s < seg + len - 2) {
                int tc = s[0] >> 4, th = s[0] & 3, n = 0, k;
                for (k = 0; k < 16; k++) {
                    n += s[1 + k];
                }
                BuildHuff(tc ? &j->ac[th] : &j->dc[th], s + 1, s + 17);
                s += 17 + n;
            }
            break;
        }
        case 0xC0: {   /* SOF0 */
            int k;
            j->height = Be16(seg + 1);
            j->width = Be16(seg + 3);
            j->ncomp = seg[5];
            if (j->ncomp < 1 || j->ncomp > 3) {
                return 0;
            }
            j->hmax = j->vmax = 1;
            for (k = 0; k < j->ncomp; k++) {
                Comp *c = &j->comp[k];
                c->id = seg[6 + k * 3];
                c->h = seg[7 + k * 3] >> 4;
                c->v = seg[7 + k * 3] & 15;
                c->tq = seg[8 + k * 3] & 3;
                if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2) {
                    return 0;
                }
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
            break;
        }
        case 0xDA: {   /* SOS: entropy data follows */
            int ns = seg[0], k, n;
            for (k = 0; k < ns; k++) {
                for (n = 0; n < j->ncomp; n++) {
                    if (j->comp[n].id == seg[1 + k * 2]) {
                        j->comp[n].td = seg[2 + k * 2] >> 4 & 3;
                        j->comp[n].ta = seg[2 + k * 2] & 3;
                    }
                }
            }
            j->p = seg + len - 2;
            return j->width > 0 && j->height > 0;
        }
        default:
            break;
        }
        p = seg + len - 2;
    }
    return 0;
}

int THPJpeg_Decode(const u8 *data, u32 size, u8 *rgba, int width, int height) {
    static Jpeg j;
    int mcuX, mcuY, mx, my, k, x, y;

    InitIDCT();
    memset(&j, 0, sizeof(j));
    j.p = data;
    j.end = data + size;
    if (!ParseHeaders(&j) || j.width != width || j.height != height) {
        return 0;
    }
    mcuX = (j.width + j.hmax * 8 - 1) / (j.hmax * 8);
    mcuY = (j.height + j.vmax * 8 - 1) / (j.vmax * 8);
    for (k = 0; k < j.ncomp; k++) {
        Comp *c = &j.comp[k];
        c->bw = mcuX * c->h;
        c->bh = mcuY * c->v;
        c->plane = SDL_malloc((size_t)c->bw * 8 * c->bh * 8);
        if (c->plane == NULL) {
            goto fail;
        }
    }
    for (my = 0; my < mcuY; my++) {
        for (mx = 0; mx < mcuX; mx++) {
            for (k = 0; k < j.ncomp; k++) {
                Comp *c = &j.comp[k];
                int stride = c->bw * 8, bx, by;
                for (by = 0; by < c->v; by++) {
                    for (bx = 0; bx < c->h; bx++) {
                        int px = (mx * c->h + bx) * 8, py = (my * c->v + by) * 8;
                        DecodeBlock(&j, c, c->plane + py * stride + px, stride);
                    }
                }
            }
        }
    }
    /* colour conversion, upsampling chroma by nearest neighbour */
    for (y = 0; y < height; y++) {
        u8 *o = rgba + (size_t)y * width * 4;
        for (x = 0; x < width; x++, o += 4) {
            const Comp *c0 = &j.comp[0];
            f32 Y = c0->plane[(y * c0->v / j.vmax) * c0->bw * 8 + x * c0->h / j.hmax];
            if (j.ncomp == 3) {
                const Comp *c1 = &j.comp[1], *c2 = &j.comp[2];
                f32 cb = c1->plane[(y * c1->v / j.vmax) * c1->bw * 8 + x * c1->h / j.hmax] - 128.0f;
                f32 cr = c2->plane[(y * c2->v / j.vmax) * c2->bw * 8 + x * c2->h / j.hmax] - 128.0f;
                int r = (int)(Y + 1.402f * cr + 0.5f);
                int g = (int)(Y - 0.344136f * cb - 0.714136f * cr + 0.5f);
                int b = (int)(Y + 1.772f * cb + 0.5f);
                o[0] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r);
                o[1] = (u8)(g < 0 ? 0 : g > 255 ? 255 : g);
                o[2] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b);
            } else {
                o[0] = o[1] = o[2] = (u8)Y;
            }
            o[3] = 255;
        }
    }
    for (k = 0; k < j.ncomp; k++) {
        SDL_free(j.comp[k].plane);
    }
    return 1;
fail:
    for (k = 0; k < j.ncomp; k++) {
        SDL_free(j.comp[k].plane);
    }
    return 0;
}
