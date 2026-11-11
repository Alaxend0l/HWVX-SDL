/* GameCube texture formats -> renderer textures (gx_render.h).
 *
 * Texture data is big-endian and tiled (the data packs keep pixel data as it
 * is on the disc). The PC release's textures come as port-only formats
 * (GX_TF_PORT | PORT_TEX_PC_*, port.h): DXT1/DXT5 and raw RGB(A), row-major,
 * little-endian, mip levels back to back, as its OpenGL upload took them.
 *
 * Block-compressed textures go to the GPU as they are stored when it samples
 * BC1/BC3 and draws them as the release's hardware did: the PC's DXT1/DXT5
 * unchanged, the GameCube's CMPR re-tiled to BC1 (CMPR is DXT1 in 8x8 tiles
 * of four 4x4 blocks, big-endian colours, the indices' bit order reversed;
 * nothing is re-encoded), but only where every colour comes out the same,
 * since the GameCube mixes a block's colours differently (BlockPalette).
 * Everything else, and every texture without BC support or with
 * HWVX_TEX_DECODE=1, is decoded to RGBA8 here. The PC's textures drawn with
 * the GameCube's colours (Port_PCTexturesLookGC, the default) are decoded
 * too: squaring a texel's colour can't be done on its block.
 *
 * Two caches: texture objects are looked up per draw by address, format, size
 * and a hash of their first bytes (packs reuse memory for other textures);
 * a miss hashes the texture's every byte and shares the GPU texture with any
 * other with the same content (the cars' and worlds' copies of a texture). */
#include "gx_render.h"
#include "gx_texmod.h"

enum {
    TF_I4 = 0, TF_I8 = 1, TF_IA4 = 2, TF_IA8 = 3, TF_RGB565 = 4, TF_RGB5A3 = 5,
    TF_RGBA8 = 6, TF_C4 = 8, TF_C8 = 9, TF_C14X2 = 10, TF_CMPR = 14,
    TF_PC_RGB8 = GX_TF_PORT | PORT_TEX_PC_RGB8, TF_PC_RGBA8 = GX_TF_PORT | PORT_TEX_PC_RGBA8,
    TF_PC_DXT1 = GX_TF_PORT | PORT_TEX_PC_DXT1, TF_PC_DXT5 = GX_TF_PORT | PORT_TEX_PC_DXT5
};

/* A texture on the GPU, shared by every texture object with its content. */
typedef struct TexData {
    u64 content;    /* every level's bytes, format, size, levels, colour look */
    void *tex;
    u32 refs;       /* TexEntries using it; 0: free */
    u32 bytes;      /* GPU memory */
    u32 rgba;       /* the same as RGBA8 */
    u8 gxr;         /* GXR_TEX_* */
} TexData;

typedef struct TexEntry {
    u32 image, key, hash;
    TexData *data;
    u32 lastUse;
} TexEntry;

#define TEX_CACHE 1024
static TexEntry s_cache[TEX_CACHE];
static TexData s_data[TEX_CACHE];   /* at most one per TexEntry */
static u32 s_useClock;

/* HWVX_STATS counters (GX_TexStats). */
static struct {
    u32 created, shared, decoded, kept;   /* misses: new GPU textures, shared ones; by path */
    u32 inexact;                          /* CMPR decoded since BC1 would draw it differently */
    u64 ticks;                            /* in the miss path: hashing, decoding, uploading */
    u64 bytes, rgbaBytes;                 /* resident GPU bytes; what they'd be as RGBA8, unshared */
    u32 textures;                         /* resident GPU textures */
} s_stats;

static void BlockSize(int fmt, int *bw, int *bh, int *bpp) {
    switch (fmt) {
    case TF_I4: case TF_C4: case TF_CMPR: *bw = 8; *bh = 8; *bpp = 4; break;
    case TF_I8: case TF_IA4: case TF_C8: *bw = 8; *bh = 4; *bpp = 8; break;
    case TF_RGBA8: *bw = 4; *bh = 4; *bpp = 32; break;
    case TF_PC_DXT1: *bw = 4; *bh = 4; *bpp = 4; break;
    case TF_PC_DXT5: *bw = 4; *bh = 4; *bpp = 8; break;
    case TF_PC_RGB8: *bw = 1; *bh = 1; *bpp = 24; break;
    case TF_PC_RGBA8: *bw = 1; *bh = 1; *bpp = 32; break;
    default: *bw = 4; *bh = 4; *bpp = 16; break;
    }
}

static u32 LevelSize(int fmt, int w, int h) {
    int bw, bh, bpp;
    BlockSize(fmt, &bw, &bh, &bpp);
    return (u32)((w + bw - 1) / bw * bw) * (u32)((h + bh - 1) / bh * bh) * (u32)bpp / 8;
}

static void Put(u8 *out, int w, int h, int x, int y, u8 r, u8 g, u8 b, u8 a) {
    if (x < w && y < h) {
        u8 *p = out + (y * w + x) * 4;
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    }
}

static void Rgb565(u16 c, u8 *o) {
    o[0] = (u8)(((c >> 11) & 0x1F) * 255 / 31);
    o[1] = (u8)(((c >> 5) & 0x3F) * 255 / 63);
    o[2] = (u8)((c & 0x1F) * 255 / 31);
    o[3] = 255;
}

static void Rgb5a3(u16 c, u8 *o) {
    if (c & 0x8000) {
        o[0] = (u8)(((c >> 10) & 0x1F) * 255 / 31);
        o[1] = (u8)(((c >> 5) & 0x1F) * 255 / 31);
        o[2] = (u8)((c & 0x1F) * 255 / 31);
        o[3] = 255;
    } else {
        o[0] = (u8)(((c >> 8) & 0xF) * 17);
        o[1] = (u8)(((c >> 4) & 0xF) * 17);
        o[2] = (u8)((c & 0xF) * 17);
        o[3] = (u8)(((c >> 12) & 0x7) * 255 / 7);
    }
}

/* 5- and 6-bit colour channels to 8 bits as the hardware does, by repeating
 * the high bits. */
static u8 Expand5(u32 v) {
    return (u8)(v << 3 | v >> 2);
}

static u8 Expand6(u32 v) {
    return (u8)(v << 2 | v >> 4);
}

/* A 4x4 colour block's palette from its two RGB565 endpoints, c0 > c1 picking
 * four colours, else three and a transparent one.
 *
 * gc: CMPR as the GameCube's texture unit decodes it (Dolphin's decoder,
 * checked against hardware): the middle colours 5/8 and 3/8 of the way, and
 * the transparent one the average colour (rounded towards c0) with alpha 0.
 *
 * Otherwise DXT as Apple GPUs decode BC1/BC3 (measured, every endpoint pair):
 * the middle colours (43a + 21b + 32) / 64, the average rounded up, the
 * transparent one black, or opaque black with opaque3 (the PC's DXT1, drawn
 * as RGB). GPUs of other makers round differently, by 1 or 2. */
static void BlockPalette(u16 c0, u16 c1, int gc, int four, int opaque3, u8 pal[4][4]) {
    int i;

    pal[0][0] = Expand5(c0 >> 11);
    pal[0][1] = Expand6((c0 >> 5) & 0x3F);
    pal[0][2] = Expand5(c0 & 0x1F);
    pal[1][0] = Expand5(c1 >> 11);
    pal[1][1] = Expand6((c1 >> 5) & 0x3F);
    pal[1][2] = Expand5(c1 & 0x1F);
    pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
    for (i = 0; i < 3; i++) {
        u32 a = pal[0][i], b = pal[1][i];

        if (four) {
            pal[2][i] = (u8)(gc ? (5 * a + 3 * b) >> 3 : (43 * a + 21 * b + 32) >> 6);
            pal[3][i] = (u8)(gc ? (3 * a + 5 * b) >> 3 : (21 * a + 43 * b + 32) >> 6);
        } else {
            /* the GameCube's midpoint rounds towards c0 (Dolphin's std::midpoint) */
            pal[2][i] = (u8)(gc ? (a + b + (a > b)) >> 1 : (a + b + 1) >> 1);
            pal[3][i] = gc ? pal[2][i] : 0;
        }
    }
    if (!four && !opaque3) {
        pal[3][3] = 0;
    }
}

static void DecodeCMPRBlock(const u8 *src, u8 *out, int w, int h, int x0, int y0) {
    u16 c0 = Port_ReadBE16(src), c1 = Port_ReadBE16(src + 2);
    u8 pal[4][4];
    int x, y;

    BlockPalette(c0, c1, 1, c0 > c1, 0, pal);
    for (y = 0; y < 4; y++) {
        u8 bits = src[4 + y];
        for (x = 0; x < 4; x++) {
            u8 *c = pal[(bits >> (6 - x * 2)) & 3];
            Put(out, w, h, x0 + x, y0 + y, c[0], c[1], c[2], c[3]);
        }
    }
}

/* A DXT colour block (little-endian RGB565 pair, 2-bit indices, pixel 0 in
 * the low bits). The PC draws DXT1 as RGB (GL_COMPRESSED_RGB_S3TC_DXT1), so
 * its c0 <= c1 fourth colour is opaque black; DXT5 always uses 4 colours. */
static void DecodeDXTColor(const u8 *src, u8 *out, int w, int h, int x0, int y0, const u8 *alpha) {
    u16 c0 = (u16)(src[0] | src[1] << 8), c1 = (u16)(src[2] | src[3] << 8);
    u32 bits = (u32)src[4] | (u32)src[5] << 8 | (u32)src[6] << 16 | (u32)src[7] << 24;
    u8 pal[4][4];
    int p;

    BlockPalette(c0, c1, 0, c0 > c1 || alpha != NULL, 1, pal);
    for (p = 0; p < 16; p++) {
        u8 *c = pal[(bits >> (p * 2)) & 3];
        Put(out, w, h, x0 + (p & 3), y0 + (p >> 2), c[0], c[1], c[2], alpha != NULL ? alpha[p] : c[3]);
    }
}

/* A DXT5 alpha block: two endpoints, 3-bit indices (48 bits, little-endian).
 * The steps between the endpoints in 64ths, rounded, as Apple GPUs have them
 * (measured, every endpoint pair). */
static void DecodeDXT5Alpha(const u8 *src, u8 *alpha) {
    static const u8 w7[6] = { 9, 18, 27, 37, 46, 55 }, w5[4] = { 13, 26, 38, 51 };
    u32 a[8];
    u64 bits = 0;
    int i;

    a[0] = src[0];
    a[1] = src[1];
    for (i = 2; i < 8; i++) {
        if (a[0] > a[1]) {
            a[i] = (a[0] * (64u - w7[i - 2]) + a[1] * w7[i - 2] + 32) >> 6;
        } else if (i < 6) {
            a[i] = (a[0] * (64u - w5[i - 2]) + a[1] * w5[i - 2] + 32) >> 6;
        } else {
            a[i] = i == 6 ? 0 : 255;
        }
    }
    for (i = 0; i < 6; i++) {
        bits |= (u64)src[2 + i] << (8 * i);
    }
    for (i = 0; i < 16; i++) {
        alpha[i] = (u8)a[(bits >> (3 * i)) & 7];
    }
}

/* The PC release's texels are the GameCube's square roots: squared, they
 * draw with the GameCube's colours (Port_PCTexturesLookGC). Alpha stays. */
static void SquareColours(u8 *rgba, int w, int h) {
    int i;

    for (i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = (u8)((rgba[i * 4 + 0] * rgba[i * 4 + 0] + 127) / 255);
        rgba[i * 4 + 1] = (u8)((rgba[i * 4 + 1] * rgba[i * 4 + 1] + 127) / 255);
        rgba[i * 4 + 2] = (u8)((rgba[i * 4 + 2] * rgba[i * 4 + 2] + 127) / 255);
    }
}

/* Decode one mip level into `out` (w*h RGBA8). */
static void DecodeLevel(int fmt, const u8 *src, int w, int h, u8 *out) {
    int bw, bh, bpp, bx, by, x, y;

    BlockSize(fmt, &bw, &bh, &bpp);
    for (by = 0; by < h; by += bh) {
        for (bx = 0; bx < w; bx += bw) {
            switch (fmt) {
            case TF_I4:
            case TF_C4:
                for (y = 0; y < 8; y++) {
                    for (x = 0; x < 8; x++) {
                        u8 v = src[y * 4 + x / 2];
                        v = (x & 1) ? (v & 0xF) : (v >> 4);
                        v = (u8)(v * 17);
                        Put(out, w, h, bx + x, by + y, v, v, v, v);
                    }
                }
                src += 32;
                break;
            case TF_I8:
            case TF_C8:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 8; x++) {
                        u8 v = src[y * 8 + x];
                        Put(out, w, h, bx + x, by + y, v, v, v, v);
                    }
                }
                src += 32;
                break;
            case TF_IA4:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 8; x++) {
                        u8 v = src[y * 8 + x];
                        u8 i = (u8)((v & 0xF) * 17), a = (u8)((v >> 4) * 17);
                        Put(out, w, h, bx + x, by + y, i, i, i, a);
                    }
                }
                src += 32;
                break;
            case TF_IA8:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        const u8 *p = src + (y * 4 + x) * 2;
                        Put(out, w, h, bx + x, by + y, p[1], p[1], p[1], p[0]);
                    }
                }
                src += 32;
                break;
            case TF_RGB565:
            case TF_RGB5A3:
            case TF_C14X2:
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        u8 c[4];
                        u16 v = Port_ReadBE16(src + (y * 4 + x) * 2);
                        if (fmt == TF_RGB565) {
                            Rgb565(v, c);
                        } else {
                            Rgb5a3(v, c);
                        }
                        Put(out, w, h, bx + x, by + y, c[0], c[1], c[2], c[3]);
                    }
                }
                src += 32;
                break;
            case TF_RGBA8:
                /* 32 bytes of AR pairs, then 32 bytes of GB pairs */
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        int i = (y * 4 + x) * 2;
                        Put(out, w, h, bx + x, by + y, src[i + 1], src[32 + i], src[32 + i + 1], src[i]);
                    }
                }
                src += 64;
                break;
            case TF_CMPR:
                DecodeCMPRBlock(src, out, w, h, bx, by);
                DecodeCMPRBlock(src + 8, out, w, h, bx + 4, by);
                DecodeCMPRBlock(src + 16, out, w, h, bx, by + 4);
                DecodeCMPRBlock(src + 24, out, w, h, bx + 4, by + 4);
                src += 32;
                break;
            case TF_PC_DXT1:
                DecodeDXTColor(src, out, w, h, bx, by, NULL);
                src += 8;
                break;
            case TF_PC_DXT5: {
                u8 alpha[16];

                DecodeDXT5Alpha(src, alpha);
                DecodeDXTColor(src + 8, out, w, h, bx, by, alpha);
                src += 16;
                break;
            }
            case TF_PC_RGB8:
                Put(out, w, h, bx, by, src[0], src[1], src[2], 255);
                src += 3;
                break;
            case TF_PC_RGBA8:
                Put(out, w, h, bx, by, src[0], src[1], src[2], src[3]);
                src += 4;
                break;
            default:
                return;
            }
        }
    }
}

/* For the mixed builds' texture rules and checks (game/Port_Mix.c). */
void GX_PortDecodeLevel(int gxFormat, const u8 *src, int w, int h, u8 *rgba) {
    DecodeLevel(gxFormat, src, w, h, rgba);
}

u32 GX_PortLevelSize(int gxFormat, int w, int h) {
    return LevelSize(gxFormat, w, h);
}

static u32 Hash(const u8 *p, u32 n) {
    u32 h = 2166136261u, i;
    for (i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

/* s_cache is looked up once per bound texture per draw, so the old linear walk
 * over 1024 entries (tens of thousands of compares per frame) was hot. Entries
 * are indexed by open addressing on (image, key, hash); the index holds entry
 * index + 1 so 0 marks an empty slot. */
#define TEX_HASH_BITS 11 /* 2048 slots for 1024 entries: load factor <= 0.5 */
#define TEX_HASH_SIZE (1u << TEX_HASH_BITS)
static u16 s_hashIdx[TEX_HASH_SIZE];

static u32 HashSlot(u32 image, u32 key, u32 hash) {
    u32 h = image * 2654435761u;
    h ^= key * 2246822519u;
    h ^= hash * 374761393u;
    h ^= h >> 15;
    return h & (TEX_HASH_SIZE - 1);
}

/* Returns the entry index matching this key, or TEX_CACHE when absent. */
static u32 HashFind(u32 image, u32 key, u32 hash) {
    u32 s = HashSlot(image, key, hash), guard;
    for (guard = 0; guard < TEX_HASH_SIZE; guard++) {
        u16 v = s_hashIdx[s];
        if (v == 0) {
            return TEX_CACHE; /* empty slot ends the probe chain */
        }
        if (s_cache[v - 1].image == image && s_cache[v - 1].key == key &&
            s_cache[v - 1].hash == hash) {
            return v - 1;
        }
        s = (s + 1) & (TEX_HASH_SIZE - 1);
    }
    return TEX_CACHE;
}

static void HashInsert(u32 idx, u32 image, u32 key, u32 hash) {
    u32 s = HashSlot(image, key, hash);
    u32 guard = 0;
    while (s_hashIdx[s] != 0 && guard++ < TEX_HASH_SIZE) {
        s = (s + 1) & (TEX_HASH_SIZE - 1);
    }
    s_hashIdx[s] = (u16)(idx + 1);
}

/* Clears the slot for this key and re-inserts the entries that had probed past
 * it, which is what keeps a linear-probe chain intact after a removal. Keys are
 * unique among resident entries, so the key alone identifies the slot. */
static void HashRemove(u32 image, u32 key, u32 hash) {
    u32 s = HashSlot(image, key, hash), guard;
    for (guard = 0; guard < TEX_HASH_SIZE; guard++) {
        u16 v = s_hashIdx[s];
        TexEntry *e;
        if (v == 0) {
            return; /* not present */
        }
        e = &s_cache[v - 1];
        if (e->image == image && e->key == key && e->hash == hash) {
            break;
        }
        s = (s + 1) & (TEX_HASH_SIZE - 1);
    }
    s_hashIdx[s] = 0;
    for (guard = 0; guard < TEX_HASH_SIZE; guard++) {
        u32 next = (s + 1) & (TEX_HASH_SIZE - 1);
        u16 v = s_hashIdx[next];
        TexEntry *e;
        if (v == 0) {
            break;
        }
        s_hashIdx[next] = 0;
        s = next;
        e = &s_cache[v - 1];
        HashInsert(v - 1, e->image, e->key, e->hash);
    }
}

/* GXInitTexObj layout (gx.c): [0] image handle, [1] w<<16|h, [2] format,
 * [3] wrapS<<8|wrapT, [4] mipmap, [5] minFilt<<8|magFilt, [6] max LOD. */
static void Sampler(const u32 *d, int levels, GXSampler *samp) {
    u32 minf = (d[5] >> 8) & 0xFF, magf = d[5] & 0xFF;

    if (levels > 1) {
        /* NEAREST/NEAR_MIP_NEAR: nearest texel, nearest level; others linear */
        int nearest = minf == 0 || minf == 2;
        samp->minLinear = !nearest;
        samp->mip = nearest ? 1 : 2;
    } else {
        samp->minLinear = minf != 0;
        samp->mip = 0;
    }
    samp->magLinear = magf != 0;
    if (d[5] == 0) {
        samp->minLinear = samp->magLinear = 1;
        samp->mip = levels > 1 ? 2 : 0;
    }
    samp->wrapS = (d[3] >> 8) & 0xFF;
    samp->wrapT = d[3] & 0xFF;
    if (samp->wrapS > 2) samp->wrapS = 0;
    if (samp->wrapT > 2) samp->wrapT = 0;
}

/* Least-recently-used resident entry; free slots (lastUse == 0) win. Only run
 * when a lookup missed and the cache has no room, so the scan is rare. */
static u32 LruPick(void) {
    u32 oldest = 0, i;
    for (i = 0; i < TEX_CACHE; i++) {
        if (s_cache[i].lastUse < s_cache[oldest].lastUse) {
            oldest = i;
        }
    }
    return oldest;
}

/* The EFB copy (GXCopyTex). It never reaches the game's memory: the texture
 * object's image address just selects it, whatever size and format the object
 * says. Only the alpha is meaningful, which is all the game's GX_CTF_A8 copy is
 * read for (TEXA). One is enough, the game only has the shadow mask. */
static struct {
    u32 image;
    void *tex;
    int w, h;
} s_copy;

void GX_TexCopyEFB(void *dest, const int src[4]) {
    int w = src[2] - src[0], h = src[3] - src[1];

    if (s_copy.tex == NULL || s_copy.w != w || s_copy.h != h) {
        if (s_copy.tex != NULL) {
            g_gxr->texDestroy(s_copy.tex);
        }
        s_copy.tex = g_gxr->texCreate(GXR_TEX_RGBA8, w, h, 1);
        s_copy.w = w;
        s_copy.h = h;
    }
    s_copy.image = HND(dest);
    if (s_copy.tex != NULL) {
        g_gxr->copyEFB(s_copy.tex, src);
    }
}

/* CMPR to BC1, one level: each 4x4 block from its place in the 8x8 tiles,
 * colours to little-endian, each row's 2-bit indices reversed (CMPR has pixel
 * 0 in the high bits, BC1 in the low). A level smaller than a tile is its
 * top-left blocks. */
static void CmprToBC1(const u8 *src, int w, int h, u8 *dst) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4, tw = (w + 7) / 8, bx, by, r;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            const u8 *b = src + ((by / 2) * tw + bx / 2) * 32 + ((by & 1) * 2 + (bx & 1)) * 8;
            u8 *o = dst + (by * bw + bx) * 8;

            o[0] = b[1];
            o[1] = b[0];
            o[2] = b[3];
            o[3] = b[2];
            for (r = 0; r < 4; r++) {
                u8 v = b[4 + r];
                o[4 + r] = (u8)((v >> 6) | ((v >> 2) & 0x0C) | ((v << 2) & 0x30) | (v << 6));
            }
        }
    }
}

/* Whether BC1 draws a CMPR level's blocks exactly as the GameCube does:
 * every colour a block uses the same both ways (its endpoints, and middle
 * colours whose channels happen to round alike; a 3-colour block's
 * transparent texel is black in BC1, the average colour on the GameCube).
 * close: only the transparent texels count, the middle colours may differ
 * (5/8 against 21/32 of the way, up to 12 levels apart). */
static int CmprExactAsBC1(const u8 *src, int w, int h, int close) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4, tw = (w + 7) / 8, bx, by, k;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            const u8 *b = src + ((by / 2) * tw + bx / 2) * 32 + ((by & 1) * 2 + (bx & 1)) * 8;
            u16 c0 = Port_ReadBE16(b), c1 = Port_ReadBE16(b + 2);
            u8 gc[4][4], bc[4][4];
            u32 used = 0;

            for (k = 0; k < 4; k++) {
                u8 v = b[4 + k];
                used |= 1u << (v & 3) | 1u << ((v >> 2) & 3) | 1u << ((v >> 4) & 3) | 1u << (v >> 6);
            }
            if ((used & 0xC) == 0 || (close && (c0 > c1 || (used & 0x8) == 0))) {
                continue;
            }
            BlockPalette(c0, c1, 1, c0 > c1, 0, gc);
            BlockPalette(c0, c1, 0, c0 > c1, 0, bc);
            for (k = close ? 3 : 2; k < 4; k++) {
                if ((used & 1u << k) != 0 && memcmp(gc[k], bc[k], 4) != 0) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* A PC DXT1 texture BC1 draws differently from the PC's RGB DXT1: a 3-colour
 * block that uses its fourth colour (transparent black in BC1, opaque in RGB). */
static int DXT1UsesTransparent(const u8 *p, u32 size) {
    u32 i;

    for (i = 0; i + 8 <= size; i += 8) {
        u16 c0 = (u16)(p[i] | p[i + 1] << 8), c1 = (u16)(p[i + 2] | p[i + 3] << 8);
        u32 bits = (u32)p[i + 4] | (u32)p[i + 5] << 8 | (u32)p[i + 6] << 16 | (u32)p[i + 7] << 24;

        /* any index 3: both bits of some 2-bit pair set */
        if (c0 <= c1 && (bits & (bits >> 1) & 0x55555555u) != 0) {
            return 1;
        }
    }
    return 0;
}

/* 64-bit content hash, 8 bytes a step. */
static u64 HashContent(const u8 *p, u32 n, u64 h) {
    u64 v;

    while (n >= 8) {
        memcpy(&v, p, 8);
        h ^= v * 0x9E3779B97F4A7C15ull;
        h = (h << 31 | h >> 33) * 0xC2B2AE3D27D4EB4Full;
        p += 8;
        n -= 8;
    }
    v = 0;
    memcpy(&v, p, n);
    h ^= (v + n) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    return h ^ (h >> 33);
}

static int EnvOn(const char *name, int dflt) {
    const char *e = SDL_getenv(name);

    return e == NULL || *e == 0 ? dflt : *e == '1';
}

/* The GPU format a texture goes up in: its blocks as they are where they can
 * be, else RGBA8. */
static int GpuFormat(int fmt, int w, int h, int levels, int squared, const u8 *image, u32 size) {
    static int decode = -1, cmprClose;
    int l;

    if (decode < 0) {
        const char *e = SDL_getenv("HWVX_TEX_CMPR");

        decode = EnvOn("HWVX_TEX_DECODE", 0);
        cmprClose = e != NULL && SDL_strcmp(e, "close") == 0;
    }
    if (decode || squared || (w & 3) != 0 || (h & 3) != 0) {
        return GXR_TEX_RGBA8;
    }
    switch (fmt) {
    case TF_CMPR:
        if (!g_gxr->texFormat(GXR_TEX_BC1)) {
            return GXR_TEX_RGBA8;
        }
        for (l = 0; l < levels; l++) {
            int lw = (w >> l) > 0 ? w >> l : 1, lh = (h >> l) > 0 ? h >> l : 1;

            if (!CmprExactAsBC1(image, lw, lh, cmprClose)) {
                s_stats.inexact++;
                return GXR_TEX_RGBA8;
            }
            image += LevelSize(fmt, lw, lh);
        }
        return GXR_TEX_BC1;
    case TF_PC_DXT1:
        if (g_gxr->texFormat(GXR_TEX_BC1_RGB)) {
            return GXR_TEX_BC1_RGB;
        }
        return g_gxr->texFormat(GXR_TEX_BC1) && !DXT1UsesTransparent(image, size) ? GXR_TEX_BC1 : GXR_TEX_RGBA8;
    case TF_PC_DXT5:
        return g_gxr->texFormat(GXR_TEX_BC3) ? GXR_TEX_BC3 : GXR_TEX_RGBA8;
    default:
        return GXR_TEX_RGBA8;
    }
}

/* Creates and uploads the GPU texture for image's levels in gxr's format. */
static void *Upload(int fmt, int gxr, const u8 *image, int w, int h, int levels, int squared, u32 *bytes,
                    const GXTexMod *mod) {
    void *tex = g_gxr->texCreate(gxr, w, h, levels);
    u8 *buf;
    int l;

    *bytes = 0;
    if (tex == NULL) {
        return NULL;
    }
    buf = gxr == GXR_TEX_RGBA8 || fmt == TF_CMPR ? SDL_malloc(GXR_TexLevelBytes(gxr, w, h)) : NULL;
    for (l = 0; l < levels; l++) {
        int lw = w >> l, lh = h >> l;
        const u8 *data = image;

        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        if (gxr == GXR_TEX_RGBA8) {
            memset(buf, 0, (size_t)lw * lh * 4);
            DecodeLevel(fmt, image, lw, lh, buf);
            if (mod != NULL) {
                GX_TexModApply(mod, buf, lw, lh);   /* a mod's colour recipe, on the decoded image */
            }
            if (squared) {
                SquareColours(buf, lw, lh);
            }
            data = buf;
        } else if (fmt == TF_CMPR) {
            CmprToBC1(image, lw, lh, buf);
            data = buf;
        }
        g_gxr->texUpload(tex, gxr, l, lw, lh, data);
        *bytes += GXR_TexLevelBytes(gxr, lw, lh);
        image += LevelSize(fmt, lw, lh);
    }
    SDL_free(buf);
    return tex;
}

/* One texture object fewer uses d. */
static void Release(TexData *d) {
    s_stats.rgbaBytes -= d->rgba;
    if (--d->refs == 0) {
        g_gxr->texDestroy(d->tex);
        s_stats.bytes -= d->bytes;
        s_stats.textures--;
        d->tex = NULL;
    }
}

/* The shared GPU texture for this content, made if there is none. */
static TexData *Acquire(int fmt, const u8 *image, int w, int h, int levels, int squared, const GXTexMod *mod) {
    static int share = -1;
    u32 size = 0, i;
    u64 content;
    TexData *d = NULL;
    int l;

    if (share < 0) {
        share = EnvOn("HWVX_TEX_SHARE", 1);
    }
    for (l = 0; l < levels; l++) {
        size += LevelSize(fmt, (w >> l) > 0 ? w >> l : 1, (h >> l) > 0 ? h >> l : 1);
    }
    content = HashContent(image, size, (u64)fmt << 48 ^ (u64)w << 32 ^ (u64)h << 16 ^ (u64)levels << 4 ^ (u64)squared);
    if (mod != NULL) {
        content = HashContent((const u8 *)mod->m, sizeof(mod->m), content);
    }
    for (i = 0; i < TEX_CACHE; i++) {
        if (s_data[i].refs != 0 && share && s_data[i].content == content) {
            s_data[i].refs++;
            s_stats.shared++;
            s_stats.rgbaBytes += s_data[i].rgba;
            return &s_data[i];
        }
        if (s_data[i].refs == 0 && d == NULL) {
            d = &s_data[i];
        }
    }
    if (d == NULL) {
        return NULL;   /* can't happen: one TexData per TexEntry at most */
    }
    d->gxr = mod != NULL ? GXR_TEX_RGBA8 : (u8)GpuFormat(fmt, w, h, levels, squared, image, size);
    d->tex = Upload(fmt, d->gxr, image, w, h, levels, squared, &d->bytes, mod);
    if (d->tex == NULL) {
        return NULL;
    }
    d->content = content;
    d->refs = 1;
    d->rgba = 0;
    for (l = 0; l < levels; l++) {
        d->rgba += GXR_TexLevelBytes(GXR_TEX_RGBA8, (w >> l) > 0 ? w >> l : 1, (h >> l) > 0 ? h >> l : 1);
    }
    s_stats.created++;
    if (d->gxr == GXR_TEX_RGBA8) {
        s_stats.decoded++;
    } else {
        s_stats.kept++;
    }
    s_stats.textures++;
    s_stats.bytes += d->bytes;
    s_stats.rgbaBytes += d->rgba;
    return d;
}

void GX_TexStats(GXTexStats *st) {
    st->created = s_stats.created;
    st->shared = s_stats.shared;
    st->decoded = s_stats.decoded;
    st->kept = s_stats.kept;
    st->inexact = s_stats.inexact;
    st->ticks = s_stats.ticks;
    st->textures = s_stats.textures;
    st->bytes = s_stats.bytes;
    st->rgbaBytes = s_stats.rgbaBytes;
}

void *GX_TexGet(const GXTexObj *obj, GXSampler *samp) {
    const u32 *d = obj->dummy;
    u8 *image = PTR(d[0]);
    int w = (int)(d[1] >> 16), h = (int)(d[1] & 0xFFFF), fmt = (int)d[2];
    int levels = 1, squared;
    u32 key = d[1] ^ (u32)fmt * 0x9E3779B1u, hash, oldest = 0, i;   /* fmt: up to the port's 0x4F */
    u64 t0;
    const GXTexMod *mod;

    if (image == NULL || w == 0 || h == 0 || w > 1024 || h > 1024) {
        return NULL;
    }
    if (d[0] == s_copy.image && s_copy.tex != NULL) {
        /* drawn 1:1 with the EFB; linear could only blur the mask's edge */
        Sampler(d, 1, samp);
        samp->minLinear = samp->magLinear = 0;
        return s_copy.tex;
    }
    if (d[4]) {
        f32 maxLod;
        memcpy(&maxLod, &d[6], 4);
        levels = (int)maxLod + 1;
        if (levels < 1 || levels > 11) {
            levels = 1;
        }
    }
    Sampler(d, levels, samp);
    squared = fmt >= GX_TF_PORT && Port_PCTexturesLookGC();
    key ^= (u32)squared << 31;
    mod = GX_TexModFor(d[0]);   /* a mod's colour recipe on this texture (gx_texmod.h) */
    if (mod != NULL) {
        key ^= mod->key;
    }
    hash = Hash(image, LevelSize(fmt, w, h) < 256 ? LevelSize(fmt, w, h) : 256) ^ (u32)levels;
    s_useClock++;
    i = HashFind(d[0], key, hash);
    if (i != TEX_CACHE) {
        s_cache[i].lastUse = s_useClock;
        return s_cache[i].data->tex;
    }
    t0 = SDL_GetPerformanceCounter();
    oldest = LruPick();
    {
        TexEntry *e = &s_cache[oldest];
        if (e->data != NULL) {
            HashRemove(e->image, e->key, e->hash);
            Release(e->data);
            e->data = NULL;
        }
        e->lastUse = 0;
        e->data = Acquire(fmt, image, w, h, levels, squared, mod);
        if (e->data == NULL) {
            return NULL;
        }
        e->image = d[0];
        e->key = key;
        e->hash = hash;
        e->lastUse = s_useClock;
        HashInsert(oldest, d[0], key, hash);
        s_stats.ticks += SDL_GetPerformanceCounter() - t0;
        return e->data->tex;
    }
}
