/* port: a mod's own pictures (Mod.schema.json, an entry's "thumbnail": "images/x.png").
 *
 * The selects show an entry's picture from a list of texture ids (Port_ContentData.c,
 * FillItems). A registered entry whose thumbnail is a PNG of its mod ("@mod\<id>\<path>" in
 * ids.lock, Port_ModsSyncLock) gets a texture of the port's own there: the PNG decoded
 * (stb_image, src/third_party, PNG only), resampled to the size of the base picture it stands
 * in for (the alias's; the world select's are 256 x 256, drawn stretched to about 5:3, as the
 * base pictures are: no letterbox), stored bottom row first as the game's pictures are, as
 * GameCube RGBA8. Its texture object is past the game's and the button prompts'
 * (m_gxTexObjArray[PORT_MAX_TEXTURES + PORT_PROMPT_TEXTURES + n]), as Port_Prompts.c does,
 * so the game's texture slots are as they were; it is made once and kept for the run.
 *
 * Limits (as the packer's, xdlrs modcheck manifest.rs): a file of at most 4 MB, at most
 * 1024 x 1024 pixels; anything else is logged once and the alias's picture shows. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Mods.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 1024
#define STBI_MALLOC(sz) SDL_malloc(sz)
#define STBI_REALLOC(p, sz) SDL_realloc(p, sz)
#define STBI_FREE(p) SDL_free(p)
#define STBI_ASSERT(x) ((void)0)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "../third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define MAX_SIDE 1024
#define MAX_BYTES (4u << 20)

extern GXTexObj m_gxTexObjArray[];

static int ObjW(const GXTexObj *o) { return (int)(o->dummy[1] >> 16); }
static int ObjH(const GXTexObj *o) { return (int)(o->dummy[1] & 0xFFFF); }

typedef struct ModImage {
    char *link;
    int w, h;
    int ok;
    u8 *image;      /* GX RGBA8, w x h */
} ModImage;
static ModImage s_img[PORT_MOD_IMAGE_TEXTURES];
static u32 s_num;

/* RGBA8 rows into the GameCube's RGBA8 tiles: 4 x 4 blocks, 32 bytes of AR then 32 of GB. */
static void ToGX(const u8 *rgba, int w, int h, u8 *out) {
    int bx, by, x, y;

    for (by = 0; by < h; by += 4) {
        for (bx = 0; bx < w; bx += 4) {
            u8 *ar = out, *gb = out + 32;

            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    const u8 *p = rgba + ((by + y) * w + bx + x) * 4;

                    *ar++ = p[3];
                    *ar++ = p[0];
                    *gb++ = p[1];
                    *gb++ = p[2];
                }
            }
            out += 64;
        }
    }
}

/* src (sw x sh, top row first) to w x h by area averaging, bottom row first. */
static void Resample(const u8 *src, int sw, int sh, u8 *dst, int w, int h) {
    int x, y, sx, sy, c;

    for (y = 0; y < h; y++) {
        int y0 = y * sh / h, y1 = SDL_max((y + 1) * sh / h, y0 + 1);

        for (x = 0; x < w; x++) {
            int x0 = x * sw / w, x1 = SDL_max((x + 1) * sw / w, x0 + 1);
            u32 sum[4] = { 0, 0, 0, 0 }, n = 0;
            u8 *d = dst + ((h - 1 - y) * w + x) * 4;

            for (sy = y0; sy < y1; sy++) {
                for (sx = x0; sx < x1; sx++) {
                    const u8 *p = src + (sy * sw + sx) * 4;

                    for (c = 0; c < 4; c++) {
                        sum[c] += p[c];
                    }
                    n++;
                }
            }
            for (c = 0; c < 4; c++) {
                d[c] = (u8)((sum[c] + n / 2) / n);
            }
        }
    }
}

static u8 *Load(const char *link, int w, int h) {
    char *path = Port_ModsFilePath(link);
    size_t size = 0;
    void *data = NULL;
    u8 *px = NULL, *rgba = NULL, *out = NULL;
    int sw = 0, sh = 0, comp = 0;

    if (path == NULL) {
        Port_Log("content: picture %s: no such file in an enabled mod", link);
        return NULL;
    }
    data = SDL_LoadFile(path, &size);
    if (data == NULL) {
        Port_Log("content: picture %s: %s", path, SDL_GetError());
    } else if (size > MAX_BYTES) {
        Port_Log("content: picture %s: %u bytes; at most %u", path, (unsigned)size, MAX_BYTES);
    } else if (!stbi_info_from_memory(data, (int)size, &sw, &sh, &comp) || sw <= 0 || sh <= 0 || sw > MAX_SIDE || sh > MAX_SIDE) {
        Port_Log("content: picture %s: not a PNG of at most %d x %d", path, MAX_SIDE, MAX_SIDE);
    } else if ((px = stbi_load_from_memory(data, (int)size, &sw, &sh, &comp, 4)) == NULL) {
        Port_Log("content: picture %s: not a PNG the decoder reads", path);
    } else if ((rgba = SDL_malloc((size_t)w * h * 4)) != NULL && (out = SDL_malloc((size_t)w * h * 4)) != NULL) {
        Resample(px, sw, sh, rgba, w, h);
        ToGX(rgba, w, h, out);
        Port_Log("content: picture %s: %d x %d, drawn as %d x %d", path, sw, sh, w, h);
    }
    stbi_image_free(px);
    SDL_free(rgba);
    SDL_free(data);
    SDL_free(path);
    return out;
}

s32 Port_ModImageTexture(const char *link, s32 like) {
    int w = 256, h = 256;
    u32 i;
    ModImage *e;

    if (link == NULL || SDL_strncmp(link, "@mod", 4) != 0) {
        return -1;
    }
    if (like >= 0 && like < PORT_MAX_TEXTURES && ObjW(&m_gxTexObjArray[like]) >= 4 && ObjH(&m_gxTexObjArray[like]) >= 4) {
        w = ObjW(&m_gxTexObjArray[like]) & ~3;
        h = ObjH(&m_gxTexObjArray[like]) & ~3;
    }
    for (i = 0; i < s_num; i++) {
        e = &s_img[i];
        if (SDL_strcmp(e->link, link) == 0 && (!e->ok || (e->w == w && e->h == h))) {
            return e->ok ? (s32)(PORT_MAX_TEXTURES + PORT_PROMPT_TEXTURES + i) : -1;
        }
    }
    if (s_num >= PORT_MOD_IMAGE_TEXTURES) {
        static int said;

        if (!said) {
            said = 1;
            Port_Log("content: more than %d mod pictures: the others show their alias's", PORT_MOD_IMAGE_TEXTURES);
        }
        return -1;
    }
    e = &s_img[s_num];
    e->link = SDL_strdup(link);
    if (e->link == NULL) {
        return -1;
    }
    e->w = w;
    e->h = h;
    e->image = Load(link, w, h);
    e->ok = e->image != NULL;
    i = s_num++;
    if (!e->ok) {
        return -1;
    }
    /* as the prompts' (Render_CreateTextureSlot): repeat, no mip levels */
    GXInitTexObj(&m_gxTexObjArray[PORT_MAX_TEXTURES + PORT_PROMPT_TEXTURES + i], e->image, (u16)w, (u16)h, 6, 1, 1, 0);
    return (s32)(PORT_MAX_TEXTURES + PORT_PROMPT_TEXTURES + i);
}
