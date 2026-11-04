/* GameCube texture formats -> RGBA8 renderer textures (gx_render.h).
 *
 * Texture data is big-endian and tiled (the data packs keep pixel data as it
 * is on the disc). Decoded textures are cached by address, format, size and a
 * hash of their first bytes, since packs reuse memory for other textures. */
#include "gx_render.h"

enum {
    TF_I4 = 0, TF_I8 = 1, TF_IA4 = 2, TF_IA8 = 3, TF_RGB565 = 4, TF_RGB5A3 = 5,
    TF_RGBA8 = 6, TF_C4 = 8, TF_C8 = 9, TF_C14X2 = 10, TF_CMPR = 14
};

typedef struct TexEntry {
    u32 image, key, hash;
    void *tex;
    u32 lastUse;
} TexEntry;

#define TEX_CACHE 1024
static TexEntry s_cache[TEX_CACHE];
static u32 s_useClock;

static void BlockSize(int fmt, int *bw, int *bh, int *bpp) {
    switch (fmt) {
    case TF_I4: case TF_C4: case TF_CMPR: *bw = 8; *bh = 8; *bpp = 4; break;
    case TF_I8: case TF_IA4: case TF_C8: *bw = 8; *bh = 4; *bpp = 8; break;
    case TF_RGBA8: *bw = 4; *bh = 4; *bpp = 32; break;
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

static void DecodeCMPRBlock(const u8 *src, u8 *out, int w, int h, int x0, int y0) {
    u16 c0 = Port_ReadBE16(src), c1 = Port_ReadBE16(src + 2);
    u8 pal[4][4];
    int x, y, i;

    Rgb565(c0, pal[0]);
    Rgb565(c1, pal[1]);
    if (c0 > c1) {
        for (i = 0; i < 4; i++) {
            pal[2][i] = (u8)((2 * pal[0][i] + pal[1][i]) / 3);
            pal[3][i] = (u8)((pal[0][i] + 2 * pal[1][i]) / 3);
        }
    } else {
        for (i = 0; i < 4; i++) {
            pal[2][i] = (u8)((pal[0][i] + pal[1][i]) / 2);
            pal[3][i] = 0;
        }
    }
    for (y = 0; y < 4; y++) {
        u8 bits = src[4 + y];
        for (x = 0; x < 4; x++) {
            u8 *c = pal[(bits >> (6 - x * 2)) & 3];
            Put(out, w, h, x0 + x, y0 + y, c[0], c[1], c[2], c[3]);
        }
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
            default:
                return;
            }
        }
    }
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

void *GX_TexGet(const GXTexObj *obj, GXSampler *samp) {
    const u32 *d = obj->dummy;
    u8 *image = PTR(d[0]);
    int w = (int)(d[1] >> 16), h = (int)(d[1] & 0xFFFF), fmt = (int)d[2];
    int levels = 1, l;
    u32 key = d[1] ^ (u32)fmt << 28, hash, oldest = 0, i;
    u8 *rgba;

    if (image == NULL || w == 0 || h == 0 || w > 1024 || h > 1024) {
        return NULL;
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
    hash = Hash(image, LevelSize(fmt, w, h) < 256 ? LevelSize(fmt, w, h) : 256) ^ (u32)levels;
    s_useClock++;
    i = HashFind(d[0], key, hash);
    if (i != TEX_CACHE) {
        s_cache[i].lastUse = s_useClock;
        return s_cache[i].tex;
    }
    oldest = LruPick();
    {
        TexEntry *e = &s_cache[oldest];
        if (e->tex != NULL) {
            HashRemove(e->image, e->key, e->hash);
            g_gxr->texDestroy(e->tex);
        }
        e->tex = g_gxr->texCreate(w, h, levels);
        if (e->tex == NULL) {
            e->lastUse = 0;
            return NULL;
        }
        e->image = d[0];
        e->key = key;
        e->hash = hash;
        e->lastUse = s_useClock;
        HashInsert(oldest, d[0], key, hash);
        rgba = SDL_malloc((size_t)w * h * 4);
        for (l = 0; l < levels; l++) {
            int lw = w >> l, lh = h >> l;
            if (lw < 1) lw = 1;
            if (lh < 1) lh = 1;
            memset(rgba, 0, (size_t)lw * lh * 4);
            DecodeLevel(fmt, image, lw, lh, rgba);
            g_gxr->texUpload(e->tex, l, lw, lh, rgba);
            image += LevelSize(fmt, lw, lh);
        }
        SDL_free(rgba);
        return e->tex;
    }
}
