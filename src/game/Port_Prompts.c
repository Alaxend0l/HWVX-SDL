/* port: button prompts that match the device (docs/plans/v0.4.1.md A; docs-pending/prompts.md).
 *
 * Every release keeps its button glyphs in one 128 x 128 atlas of 32 px cells, and its
 * interfaces show three of them: confirm (32,64), back (32,96), details (0,64), in texture
 * pixels; the atlas is stored bottom-up, so a cell's rows are upside down. An image object
 * and a texture animation of the same cell make each prompt, so swapping the texture where
 * an image is drawn (InterfaceManager_DrawObject_Standard_Image) covers them all.
 *
 * Recognition: each texture a pack loads (DataPack_RefreshTextures) that is 128 x 128 is
 * decoded and given a perceptual hash; within a few bits of a release's atlas (any format:
 * the GameCube's RGBA8 and CMPR copies, the PC's DXT5 keycaps, the PS2's PSMT8, a mixed
 * build's re-encoding) it is noted, until the slot is unloaded.
 *
 * Replacement: per atlas and glyph triple, a 512 x 512 GameCube RGBA8 texture (the cells 4x),
 * the original's cells copied (nearest) and the three shown cells holding prompts.bin's
 * glyphs (assets/prompts), built the first time it is drawn and kept. Its texture object is
 * one of the port's own past the game's (m_gxTexObjArray[PORT_MAX_TEXTURES + n]), so the
 * game's texture slots are as they were.
 *
 * The device: the interface's pad (Interface.input; all pads: the device last pressed
 * anywhere), pad.c Port_PromptStyle. Original draws the release's own. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Mix.h"
#include "prompts_bin.h"

int GX_PortDecodeTexObj(const GXTexObj *obj, int level, u8 *rgba);   /* gx_tex.c */
void GX_TexInvalidate(const void *p, u32 size);

extern GXTexObj m_gxTexObjArray[];
extern u32 m_totalTextures;

/* GX texture object fields (gx_tex.c): image handle, width << 16 | height, format. */
static void *ObjImage(const GXTexObj *o) { return PTR(o->dummy[0]); }
static int ObjW(const GXTexObj *o) { return (int)(o->dummy[1] >> 16); }
static int ObjH(const GXTexObj *o) { return (int)(o->dummy[1] & 0xFFFF); }
static int ObjFmt(const GXTexObj *o) { return (int)o->dummy[2]; }

/* The atlases' perceptual hashes (alpha bits, luma bits; tools/xdlrs prompts.rs Pix::phash,
 * printed by `xdlrs prompts inventory`): copies are within 2 bits, every other 128 x 128
 * texture of the three releases at least 31 bits off. */
static const struct { u64 a, l; } s_atlases[] = {
    { 0x00606f0f0604f6f2ull, 0x006063020604f6f2ull },   /* GameCube (RGBA8; CMPR 1 bit off) */
    { 0x6ffff363c6c60606ull, 0x6fdff363e6c60606ull },   /* PC (keycaps) */
    { 0x0f6f6f0f0f0f0f0full, 0x0f656f057f360f06ull },   /* PS2 */
};
#define PHASH_MAX 12

static void Phash(const u8 *px, int w, int h, u64 *pa, u64 *pl) {
    u64 a[64], l[64], sa = 0, sl = 0;
    int bw = w / 8, bh = h / 8, x, y, k;

    memset(a, 0, sizeof(a));
    memset(l, 0, sizeof(l));
    for (y = 0; y < bh * 8; y++) {
        for (x = 0; x < bw * 8; x++) {
            const u8 *p = px + (y * w + x) * 4;

            k = (y / bh) * 8 + x / bw;
            a[k] += p[3];
            l[k] += (299u * p[0] + 587u * p[1] + 114u * p[2]) * (u64)p[3] / 255000u;
        }
    }
    *pa = *pl = 0;
    for (k = 0; k < 64; k++) {
        sa += a[k];
        sl += l[k];
    }
    for (k = 0; k < 64; k++) {
        if (a[k] * 64 > sa) *pa |= 1ull << k;
        if (l[k] * 64 > sl) *pl |= 1ull << k;
    }
}

static int Bits(u64 v) {
    int n = 0;

    for (; v; v &= v - 1) {
        n++;
    }
    return n;
}

/* Per game texture slot: the atlas it holds (1-based, 0 none) and its image when noted. */
static u8 s_class[PORT_MAX_TEXTURES];
static void *s_image[PORT_MAX_TEXTURES];

void Port_PromptsNote(u16 id) {
    static u8 px[128 * 128 * 4];
    const GXTexObj *o;
    u64 pa, pl;
    u32 i;

    if (id >= PORT_MAX_TEXTURES) {
        return;
    }
    s_class[id] = 0;
    o = &m_gxTexObjArray[id];
    if (ObjW(o) != 128 || ObjH(o) != 128 || !GX_PortDecodeTexObj(o, 0, px)) {
        return;
    }
    Phash(px, 128, 128, &pa, &pl);
    for (i = 0; i < SDL_arraysize(s_atlases); i++) {
        if (Bits(pa ^ s_atlases[i].a) + Bits(pl ^ s_atlases[i].l) <= PHASH_MAX) {
            s_class[id] = (u8)(i + 1);
            s_image[id] = ObjImage(o);
            if (SDL_getenv("HWVX_PROMPTS_LOG") != NULL) {
                Port_Log("prompts: texture %u is the button atlas (%u), format %d", id, i + 1, ObjFmt(o));
            }
            return;
        }
    }
}

void Port_PromptsForget(u16 id) {
    if (id < PORT_MAX_TEXTURES) {
        s_class[id] = 0;
    }
}

/* ---- replacements ------------------------------------------------------ */

#define REP_SIZE 512
typedef struct Replacement {
    u8 cls;             /* 0: free */
    s8 glyph[3];
    u8 *image;          /* GX RGBA8, REP_SIZE x REP_SIZE */
} Replacement;
static Replacement s_rep[PORT_PROMPT_TEXTURES];
static u32 s_repNext;

/* prompts.bin's glyph g (CELL x CELL RGBA8, top row first), or NULL. */
static const u8 *Glyph(int g, u32 *cell) {
    const u8 *b = g_promptsBin;
    u32 count, size;

    if (memcmp(b, "HWVXPRM1", 8) != 0) {
        return NULL;
    }
    memcpy(&count, b + 8, 4);
    memcpy(&size, b + 12, 4);
    count = SDL_Swap32LE(count);
    size = SDL_Swap32LE(size);
    if (g < 0 || (u32)g >= count || 16 + (u64)count * (16 + size * size * 4) > sizeof(g_promptsBin)) {
        return NULL;
    }
    *cell = size;
    return b + 16 + (size_t)g * (16 + size * size * 4) + 16;
}

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

static u8 *Build(u16 tex, const int glyph[3]) {
    /* the shown cells, in texture pixels of the 128 atlas: confirm, back, details */
    static const int cells[3][2] = { { 32, 64 }, { 32, 96 }, { 0, 64 } };
    const int k = REP_SIZE / 128;
    u8 *src = SDL_malloc(128 * 128 * 4), *rgba = SDL_malloc(REP_SIZE * REP_SIZE * 4), *out = NULL;
    int x, y, c;

    if (src == NULL || rgba == NULL || !GX_PortDecodeTexObj(&m_gxTexObjArray[tex], 0, src)) {
        goto done;
    }
    for (y = 0; y < REP_SIZE; y++) {
        for (x = 0; x < REP_SIZE; x++) {
            memcpy(rgba + (y * REP_SIZE + x) * 4, src + ((y / k) * 128 + x / k) * 4, 4);
        }
    }
    for (c = 0; c < 3; c++) {
        u32 size = 0;
        const u8 *g = Glyph(glyph[c], &size);
        int x0 = cells[c][0] * k, y0 = cells[c][1] * k, n = 32 * k;

        if (g == NULL) {
            continue;
        }
        for (y = 0; y < n; y++) {
            for (x = 0; x < n; x++) {
                /* bottom-up: the glyph's top row is the cell's last */
                const u8 *p = g + (((n - 1 - y) * size / n) * size + x * size / n) * 4;

                memcpy(rgba + ((y0 + y) * REP_SIZE + x0 + x) * 4, p, 4);
            }
        }
    }
    out = SDL_malloc(REP_SIZE * REP_SIZE * 4);
    if (out != NULL) {
        ToGX(rgba, REP_SIZE, REP_SIZE, out);
    }
done:
    SDL_free(src);
    SDL_free(rgba);
    return out;
}

/* The prompt port for the interface being drawn (Interface.input: a pad, -1 all, -2 none). */
static int s_port = -1;

int Port_PromptsInterface(s32 input) {
    int old = s_port;

    if (input >= 0 && input < 4) {
        s_port = input;
    } else if (input == -1) {
        s_port = -1;
    }
    return old;
}

void Port_PromptsRestore(int old) {
    s_port = old;
}

u16 Port_PromptTexture(u16 tex) {
    int glyph[3], style, cls;
    u32 i;
    Replacement *r;

    if (tex >= PORT_MAX_TEXTURES || s_class[tex] == 0 || ObjImage(&m_gxTexObjArray[tex]) != s_image[tex]) {
        return tex;
    }
    style = Port_PromptStyle(s_port, glyph);
    if (style == PORT_PROMPTS_ORIGINAL) {
        return tex;
    }
    cls = s_class[tex];
    for (i = 0; i < PORT_PROMPT_TEXTURES; i++) {
        r = &s_rep[i];
        if (r->cls == cls && r->glyph[0] == glyph[0] && r->glyph[1] == glyph[1] && r->glyph[2] == glyph[2]) {
            return (u16)(PORT_MAX_TEXTURES + i);
        }
    }
    /* a new one: the next slot, round the ring */
    i = s_repNext;
    s_repNext = (s_repNext + 1) % PORT_PROMPT_TEXTURES;
    r = &s_rep[i];
    if (r->image != NULL) {
        GX_TexInvalidate(r->image, REP_SIZE * REP_SIZE * 4);
        SDL_free(r->image);
        r->cls = 0;
    }
    r->image = Build(tex, glyph);
    if (r->image == NULL) {
        return tex;
    }
    r->cls = (u8)cls;
    r->glyph[0] = (s8)glyph[0];
    r->glyph[1] = (s8)glyph[1];
    r->glyph[2] = (s8)glyph[2];
    /* as the original's slot (Render_CreateTextureSlot): repeat, no mip levels */
    GXInitTexObj(&m_gxTexObjArray[PORT_MAX_TEXTURES + i], r->image, REP_SIZE, REP_SIZE, 6, 1, 1, 0);
    if (SDL_getenv("HWVX_PROMPTS_LOG") != NULL) {
        Port_Log("prompts: atlas %d with glyphs %d %d %d built (slot %u)", cls, glyph[0], glyph[1], glyph[2], i);
    }
    return (u16)(PORT_MAX_TEXTURES + i);
}
