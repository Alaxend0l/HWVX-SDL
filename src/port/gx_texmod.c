/* Colour recipes on textures (gx_texmod.h). */
#include <SDL3/SDL.h>
#include "gx_texmod.h"

static GXTexMod *s_mods;
static u32 s_cap;
u32 g_gxTexMods;

void GX_TexModSet(void *image, const f32 m[12], void *owner) {
    u32 h = HND(image), i, key = 0x811C9DC5u;
    const u8 *b = (const u8 *)m;
    GXTexMod *e = NULL;

    if (image == NULL) {
        return;
    }
    for (i = 0; i < g_gxTexMods; i++) {
        if (s_mods[i].image == h) {
            e = &s_mods[i];
        }
    }
    if (e == NULL) {
        if (g_gxTexMods == s_cap) {
            GXTexMod *p = SDL_realloc(s_mods, (s_cap + 16) * sizeof(*p));

            if (p == NULL) {
                return;
            }
            s_mods = p;
            s_cap += 16;
        }
        e = &s_mods[g_gxTexMods++];
    }
    for (i = 0; i < 12 * sizeof(f32); i++) {
        key = (key ^ b[i]) * 0x01000193u;
    }
    e->image = h;
    e->key = key | 1;
    e->owner = owner;
    SDL_memcpy(e->m, m, sizeof(e->m));
}

void GX_TexModClear(void *owner) {
    u32 i = 0;

    while (i < g_gxTexMods) {
        if (s_mods[i].owner == owner) {
            s_mods[i] = s_mods[--g_gxTexMods];
        } else {
            i++;
        }
    }
}

const GXTexMod *GX_TexModFind(u32 image) {
    u32 i;

    for (i = 0; i < g_gxTexMods; i++) {
        if (s_mods[i].image == image) {
            return &s_mods[i];
        }
    }
    return NULL;
}

void GX_TexModApply(const GXTexMod *mod, u8 *rgba, int w, int h) {
    const f32 *m = mod->m;
    int n = w * h, c;

    for (; n > 0; n--, rgba += 4) {
        f32 r = rgba[0], g = rgba[1], b = rgba[2];

        for (c = 0; c < 3; c++) {
            f32 v = m[c * 4] * r + m[c * 4 + 1] * g + m[c * 4 + 2] * b + m[c * 4 + 3] * 255.0f + 0.5f;

            rgba[c] = v <= 0.0f ? 0 : v >= 255.0f ? 255 : (u8)v;
        }
    }
}
