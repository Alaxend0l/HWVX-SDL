/* Render windows (Gfx_Main.c g_window[5], GetRenderWindow): one per
 * viewport, with its matrices, camera, clip planes and screen rectangle.
 * Offsets are the GameCube layout; the asserts keep them that way. */
#ifndef GFX_H
#define GFX_H

#include <stddef.h>

typedef struct GfxWindow {
    f32 world[4][4];            /* 0x000 Gfx_SetWorldMatrix */
    f32 view[4][4];             /* 0x040 Gfx_SetCameraMatrix */
    f32 worldView[4][4];        /* 0x080 world x view, loaded as the GX position matrix */
    f32 projection[4][4];       /* 0x0C0 Gfx_SetProjectionMatrix */
    f32 worldViewProj[4][4];    /* 0x100 */
    f32 viewMatrix[4][4];       /* 0x140 view, kept by Gfx_SetRenderWindowSize (index 0xFFFF) */
    Vector4 cameraPos;          /* 0x180 */
    Vector4 cameraRight;        /* 0x190 */
    Vector4 cameraUp;           /* 0x1A0 */
    Vector4 cameraAt;           /* 0x1B0 */
    Vector4 clipPlanes[6];      /* 0x1C0 Render_SetClipPlanes */
    Vector4 shadowClipPlanes[5]; /* 0x220 */
    f32 fov;                    /* 0x270 degrees */
    f32 sinHalfFov;             /* 0x274 sin(0.45 fov) */
    f32 scaleX;                 /* 0x278 */
    f32 scaleY;                 /* 0x27C */
    u32 width;                  /* 0x280 pixels */
    u32 height;                 /* 0x284 */
    u32 halfWidth;              /* 0x288 */
    u32 halfHeight;             /* 0x28C */
    f32 offsetX;                /* 0x290 */
    f32 offsetY;                /* 0x294 */
    f32 sizeX;                  /* 0x298 */
    f32 sizeY;                  /* 0x29C */
    f32 unk2A0;                 /* 0x2A0 */
    f32 unk2A4;                 /* 0x2A4 */
    f32 centerX;                /* 0x2A8 */
    f32 centerY;                /* 0x2AC */
    s32 left;                   /* 0x2B0 */
    s32 right;                  /* 0x2B4 */
    s32 top;                    /* 0x2B8 */
    s32 bottom;                 /* 0x2BC */
    GXColor overlayColor;       /* 0x2C0 full-window tint (Gfx_SetOverlay) */
    u32 overlayMode;            /* 0x2C4 blend mode, 0: none */
    s32 unk2C8;                 /* 0x2C8 */
    f32 invSinHalfFov;          /* 0x2CC 1 / sin(fov / 2): sprite size scale */
} GfxWindow;

_Static_assert(offsetof(GfxWindow, cameraPos) == 0x180, "GfxWindow.cameraPos");
_Static_assert(offsetof(GfxWindow, clipPlanes) == 0x1C0, "GfxWindow.clipPlanes");
_Static_assert(offsetof(GfxWindow, fov) == 0x270, "GfxWindow.fov");
_Static_assert(offsetof(GfxWindow, width) == 0x280, "GfxWindow.width");
_Static_assert(offsetof(GfxWindow, left) == 0x2B0, "GfxWindow.left");
_Static_assert(offsetof(GfxWindow, overlayColor) == 0x2C0, "GfxWindow.overlayColor");
_Static_assert(sizeof(GfxWindow) == 0x2D0, "GfxWindow size");

/* A point light (Gfx_AddPointLight). */
typedef struct GfxPointLight {
    Vector4 position;           /* 0x00 */
    f32 radius;                 /* 0x10 */
    GXColor color;              /* 0x14 */
    u8 pad18[8];                /* 0x18 */
} GfxPointLight;

/* GfxState.flags (Gfx_EnableState / Gfx_DisableState) */

/* The render state (g_state). */
typedef struct GfxState {
    GfxPointLight pointLights[3]; /* 0x00 */
    Vector4 lightDir;           /* 0x60 parallel light (Gfx_SetParallelLight) */
    GXColor lightColor;         /* 0x70 */
    GXColor baseColor;          /* 0x74 Gfx_SetBaseColor: geometry constant colour */
    GXColor ambientColor;       /* 0x78 */
    u32 flags;                  /* 0x7C Gfx_EnableState bits */
    f32 unk80;                  /* 0x80 */
    s32 unk84;                  /* 0x84 */
    s32 unk88;                  /* 0x88 */
    u16 debugFont;              /* 0x8C */
    s8 lightChanged;            /* 0x8E a new GDL light state is needed */
    s8 unk8F;                   /* 0x8F */
    u8 numPointLights;          /* 0x90 */
    u8 unk91;                   /* 0x91 */
    u8 unk92;                   /* 0x92 */
    u8 worldIsIdentity;         /* 0x93 the world matrix is the identity (Gfx_SetWorldMatrix) */
    u8 enviroMapMode;           /* 0x94 */
    u8 pad95[3];                /* 0x95 */
    f32 farClippingPlane;       /* 0x98 */
    u8 pad9C[4];                /* 0x9C */
} GfxState;

_Static_assert(sizeof(GfxPointLight) == 0x20, "GfxPointLight size");
_Static_assert(offsetof(GfxState, lightColor) == 0x70, "GfxState.lightColor");
_Static_assert(offsetof(GfxState, flags) == 0x7C, "GfxState.flags");
_Static_assert(offsetof(GfxState, debugFont) == 0x8C, "GfxState.debugFont");
_Static_assert(offsetof(GfxState, worldIsIdentity) == 0x93, "GfxState.worldIsIdentity");
_Static_assert(sizeof(GfxState) == 0xA0, "GfxState size");

extern GfxState g_state;

/* A geometry (pack data): groups of display lists with bounds and LODs. */
typedef struct GeometryGroup {
    u16 unk0;                   /* 0x00 */
    u16 flags;                  /* 0x02 */
    GXColor color;              /* 0x04 */
    u8 pad08[4];                /* 0x08 */
    PTR32(struct UVScroll) uvScroll; /* 0x0C */
    union {
        u32 dlSize;             /* 0x10 display list bytes */
        PTR32(void) data;       /*      the same, as some views read it */
    };
    u16 unk14;                  /* 0x14 */
    u8 pad16[2];                /* 0x16 */
    /* The texture relocation stores the id zero-extended as a big-endian
     * u32 over 0x18-0x1B, i.e. into `texture` with the override 0 (the port
     * keeps that layout: PortSwap_StoreTextureID). ShaderHolder_Heartbeat
     * writes the override (animated and movie textures); Render_Draw*Group
     * uses it when set, and it is the opaque/lit/alpha sort bucket key. */
    u16 textureOverride;        /* 0x18 0: use `texture` */
    u16 texture;                /* 0x1A */
    union {
        PTR32(void) displayList; /* 0x1C */
        PTR32(__typeof__(u16[3])) tris; /* triangles (Gfx_DisplayList.c) */
    };
} GeometryGroup;

typedef struct Geometry {
    u16 unk0;                   /* 0x00 */
    u16 numGroups;              /* 0x02 */
    u16 unk4;                   /* 0x04 */
    u16 unk6;                   /* 0x06 */
    PTR32(GeometryGroup) groups; /* 0x08 */
    union {
        PTR32(f32) lodDistances; /* 0x0C */
        PTR32(struct GeometryVertex) verts;
    };
    f32 bboxMin[4];             /* 0x10 w: bounding radius */
    f32 bboxMax[4];             /* 0x20 */
    f32 center[4];              /* 0x30 */
} Geometry;

_Static_assert(sizeof(GeometryGroup) == 0x20, "GeometryGroup size");
_Static_assert(offsetof(Geometry, bboxMin) == 0x10, "Geometry.bboxMin");
_Static_assert(offsetof(Geometry, center) == 0x30, "Geometry.center");

/* A vertex of a queued 2D primitive. */
typedef struct GDL2DVtx {
    f32 x, y, z;                /* 0x00 screen position, z: depth */
    f32 w;                      /* 0x0C */
    f32 s, t;                   /* 0x10 texture coordinates */
    GXColor color;              /* 0x18 */
    u8 pad1C[4];
} GDL2DVtx;

/* A 2D rectangle queued by Gfx_Add2DRectangle* (drawn by Render_QuadTri_.c). */
typedef struct GDLRect {
    GDL2DVtx v[4];              /* 0x00 */
    u16 tex;                    /* 0x80 texture, 0xFFFF: none */
    u8 blendMode;               /* 0x82 */
    u8 window;                  /* 0x83 render window */
} GDLRect;

_Static_assert(sizeof(GDL2DVtx) == 0x20, "GDL2DVtx size");
_Static_assert(offsetof(GDLRect, window) == 0x83, "GDLRect.window");

#endif
