/* Colour recipes on textures (mods, docs/xdl-format §8): a texture whose
 * pixels start at a registered address is decoded and drawn through a 3x4
 * colour matrix, the source pixels untouched. game/Port_Mods.c registers
 * them as a pack's textures are set up; gx_tex.c applies them. */
#ifndef GX_TEXMOD_H
#define GX_TEXMOD_H

#include "port.h"

typedef struct GXTexMod {
    u32 image;      /* handle of the texture's first level */
    u32 key;        /* of the matrix: part of the texture caches' keys */
    f32 m[12];      /* rows r, g, b; columns r, g, b and an offset (1.0 = full scale) */
    void *owner;
} GXTexMod;

extern u32 g_gxTexMods;     /* how many are registered: 0 costs a draw nothing */

/* The texture at `image` is drawn through m from now on (replaces one set before). */
void GX_TexModSet(void *image, const f32 m[12], void *owner);
/* Every recipe of an owner (its pack is released). */
void GX_TexModClear(void *owner);
const GXTexMod *GX_TexModFind(u32 image);
/* RGBA8 pixels through the matrix, alpha kept. */
void GX_TexModApply(const GXTexMod *mod, u8 *rgba, int w, int h);

static inline const GXTexMod *GX_TexModFor(u32 image) {
    return g_gxTexMods != 0 ? GX_TexModFind(image) : NULL;
}

#endif
