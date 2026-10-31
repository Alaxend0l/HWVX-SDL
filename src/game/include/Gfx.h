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

#endif
