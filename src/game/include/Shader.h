/* Shaders (ShaderHolder.c): animated surface effects of pack geometry, a
 * colour, a texture scroll and a texture sequence played by key tracks and
 * applied to every geometry using the shader. Offsets are the GameCube
 * layout; the asserts keep them that way. */
#ifndef SHADER_H
#define SHADER_H

#include <stddef.h>
#include "KeyState.h"

/* A shader's description in the data pack. */
typedef struct ShaderDef {
    u8 isMovie;                 /* 0x00 shows the playing movie's frame */
    u8 pad01[3];                /* 0x01 */
    KeyTrack colorKeys;         /* 0x04 ExtractColorKey */
    KeyTrack pointKeys;         /* 0x0C ExtractPointKey: texture scroll */
    PTR32(TextureKeyAnim) textureKeys; /* 0x14 ExtractTextureKey */
} ShaderDef;

/* A shader playing (ShaderHolder.slots). */
typedef struct Shader {
    KeyColorState color;        /* 0x00 */
    TextureKeyState texture;    /* 0x30 texture: the current one, 0: none */
    u8 pad3C[4];                /* 0x3C */
    KeyPointState point;        /* 0x40 x, y: texture scroll */
    PTR32(ShaderDef) def;       /* 0x5C */
    u8 hasColorKey;             /* 0x60 more than one key: animate */
    u8 hasTextureKey;           /* 0x61 */
    u8 hasPointKey;             /* 0x62 */
    u8 pad63;                   /* 0x63 */
    u32 refCount;               /* 0x64 */
    u8 pad68[4];                /* 0x68 */
} Shader;

/* The texture settings of a geometry a shader drives. */
typedef struct ShaderParams {
    f32 scrollX;                /* 0x00 */
    f32 scrollY;                /* 0x04 */
    GXColor color;              /* 0x08 */
    u32 mode;                   /* 0x0C 2: tinted by color */
} ShaderParams;

/* A geometry using a shader (ShaderHolder_AddGX). */
typedef struct ShadedGeom {
    u8 pad00[0xC];              /* 0x00 */
    PTR32(ShaderParams) params; /* 0x0C NULL: texture only */
    u8 pad10[8];                /* 0x10 */
    u16 texture;                /* 0x18 */
} ShadedGeom;

typedef struct ShaderHolderGX {
    PTR32(ShadedGeom) gx;       /* 0x00 */
    PTR32(Shader) shader;       /* 0x04 */
} ShaderHolderGX;

#define SHADER_MAX 0x40

typedef struct ShaderHolder {
    PTR32(Shader) shaders[SHADER_MAX]; /* 0x0000 in use, the first numShaders */
    Shader slots[SHADER_MAX];   /* 0x0100 */
    ShaderHolderGX gx[0x80];    /* 0x1C00 */
    u32 numShaders;             /* 0x2000 */
    u32 numGX;                  /* 0x2004 */
} ShaderHolder;

_Static_assert(offsetof(Shader, texture) == 0x30, "Shader.texture");
_Static_assert(offsetof(Shader, point) == 0x40, "Shader.point");
_Static_assert(offsetof(Shader, def) == 0x5C, "Shader.def");
_Static_assert(sizeof(Shader) == 0x6C, "Shader size");
_Static_assert(offsetof(ShaderParams, color) == 0x08, "ShaderParams.color");
_Static_assert(offsetof(ShadedGeom, texture) == 0x18, "ShadedGeom.texture");
_Static_assert(sizeof(ShaderHolder) == 0x2008, "ShaderHolder size");

#endif
