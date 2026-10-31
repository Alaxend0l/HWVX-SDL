#include "common.h"
#include "Gfx.h"








s32 Gfx_Create(void) {
    extern GXColor g_textcolor;
    extern s8 g_screenTextY;
    extern s8 g_screenTextX;
    extern u8 g_CurrentViews;
    extern u8 g_screenText[0xA00];

    Init_Math3D();
    RenderState_Init();
    Render_Create();
    TimeHandler_Init();
    FontManager_Create();
    g_textcolor.a = 0xFF;
    g_textcolor.b = 0xFF;
    g_textcolor.g = 0xFF;
    g_textcolor.r = 0xFF;
    g_screenTextY = 0;
    g_screenTextX = 0;
    g_CurrentViews = 1;
    memset(g_screenText, 0, sizeof(g_screenText));
    if (Gfx_TextureHandler_Create() == 0) {
        return 0;
    } else if (Gfx_DisplayList_Create() == 0) {
        return 0;
    }
    return 1;
}

void Gfx_Destroy(void) {
    FontManager_Destroy();
    Gfx_DisplayList_Destroy();
    Gfx_TextureHandler_Destroy();
    Render_Destroy();
}

/* The decomp has an m2c placeholder here; the original passes its arguments
 * straight on to Render_CreateView (asm). */
s32 Gfx_CreateView(s32 view, u32 width, u32 height) {
    return Render_CreateView(view, width, height);
}

void Gfx_BeginScene(void) {
    extern u8 g_renderStats[0x100];

    g_state.flags = 0;
    Render_BeginScene();
    Render_InitDisplayList();
    memset(g_renderStats, 0, sizeof(g_renderStats));
    Gfx_SetProjectionMatrix(GetRenderWindow()->fov);
    Gfx_SetWorldMatrix(NULL);
}

void Gfx_EndScene(void) {
    extern s8 g_screenTextX;
    extern s8 g_screenTextY;

    Render_EndScene();
    g_screenTextY = 0;
    g_screenTextX = 0;
}

void Gfx_FlipBuffers(void) {
    extern u8 g_screenText[0xA00];

    Render_FlipBuffers();
    memset(g_screenText, 0, sizeof(g_screenText));
}

void RenderState_Init(void) {
    g_state.flags = 1;
    g_state.numPointLights = 0;
    g_state.unk80 = 64.0f;
    g_state.unk91 = 0xFF;
    g_state.lightChanged = 1;
    g_state.unk8F = 1;
    g_state.unk92 = 0xFF;
    g_state.enviroMapMode = 0;
    g_state.unk88 = 0;
    g_state.farClippingPlane = 0.0f;
}

void Gfx_EnableState(s32 arg0) {
    g_state.flags |= arg0;
}

void Gfx_DisableState(s32 arg0) {
    g_state.flags &= ~arg0;
}

void Gfx_SetEnviroMapMode(u8 mode) {
    g_state.enviroMapMode = mode;
}

void Gfx_SetFarClippingPlane(f32 farPlane) {
    g_state.farClippingPlane = farPlane;
}

void Gfx_RemovePointLights(void) {
    if (g_state.numPointLights != 0) {
        g_state.numPointLights = 0;
        g_state.lightChanged = 1;
    }
}

void Gfx_AddPointLight(GXColor *color, Vector4 *pos, f32 radius) {
    if (g_state.numPointLights < 3) {
        g_state.pointLights[g_state.numPointLights].color = *color;
        g_state.pointLights[g_state.numPointLights].position = *pos;
        g_state.pointLights[g_state.numPointLights].radius = radius;
        g_state.lightChanged = 1;
        g_state.numPointLights++;
    }
}

void Gfx_SetParallelLight(GXColor *color, Vector4 *dir) {
    Vector4 *lightDir;

    g_state.lightColor = *color;
    if (dir != NULL) {
        lightDir = &g_state.lightDir;
        *lightDir = *dir;
        if (PSVECSquareMag(lightDir)) {
            PSVECNormalize(lightDir, lightDir);
        }
    }
    g_state.lightChanged = 1;
}

void Gfx_SetAmbientColor(GXColor *color) {
    g_state.ambientColor = *color;
}

void Gfx_SetBaseColor(GXColor *color) {
    g_state.baseColor = *color;
}

void Gfx_SetRenderWindow(s32 window) {
    extern s32 g_currentWindow;
    extern GfxWindow *g_pWindow;
    extern GfxWindow g_window[];

    g_currentWindow = window;
    g_pWindow = &g_window[window];
}

s32 Gfx_GetRenderWindowIndex(void) {
    extern s32 g_currentWindow;
    return g_currentWindow;
}

GfxWindow *GetRenderWindow(void) {
    extern s32 g_currentWindow;
    extern GfxWindow g_window[];
    return &g_window[g_currentWindow];
}

void Gfx_SetRenderWindowSize(s32 x0, s32 y0, s32 x1, s32 y1, s32 arg4) {
    extern GfxWindow g_window[];
    extern s32 g_currentWindow;
    GfxWindow *win = &g_window[g_currentWindow];
    u32 width = x1 - x0;
    u32 height = y1 - y0;
    u32 marginX = 0x1000 - width;
    u32 marginY = 0x1000 - height;

    win->unk2C8 = arg4;
    win->left = x0 + 1;
    win->top = y0 + 1;
    win->right = x1 - 1;
    win->bottom = y1 - 1;
    win->width = width;
    win->halfWidth = width >> 1;
    win->height = height;
    win->halfHeight = height >> 1;
    win->scaleX = marginX / 2.0f;
    win->scaleY = marginY / 2.0f;
    win->sizeX = (width * 16) / 640.0f;
    win->sizeY = (height * 16) / 480.0f;
    win->offsetX = ((marginX >> 1) - x0) * 16;
    win->offsetY = ((marginY >> 1) - y0) * 16;
    win->unk2A0 = 16.0f;
    win->unk2A4 = 15.966666f;
    win->centerX = (marginX >> 1) * 16;
    win->centerY = (marginY >> 1) * 16;
    Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
    memcpy(g_window[g_currentWindow].viewMatrix, g_window[g_currentWindow].view, 0x40);
    /* inlined in the original (no such function in the symbol map) */
    win->overlayMode = 0;
    win->fov = g_window[0].fov;
}

s32 Gfx_GetCameraPositions(Vector4 *out) {
    extern u8 g_CurrentViews;
    extern GfxWindow g_window[];
    u32 i;
    s32 count = 0;

    for (i = 0; i < 5; i++) {
        if (g_CurrentViews & (1 << i)) {
            out[count++] = g_window[i].cameraPos;
        }
    }
    return count;
}

f32 Gfx_GetMinCameraDistSquared(Vector4 *pos) {
    extern u8 g_CurrentViews;
    extern GfxWindow g_window[];
    f32 minDist;
    f32 dist;
    u32 i;

    if (g_CurrentViews != 1) {
        minDist = 99999.9f;
        for (i = 1; i < 5; i++) {
            if (g_CurrentViews & (1 << i)) {
                dist = PSVECSquareDistance(pos, &g_window[i].cameraPos);
                if (minDist > dist) {
                    minDist = dist;
                }
            }
        }
        return minDist;
    }
    return PSVECSquareDistance(pos, &g_window[0].cameraPos);
}

void Gfx_ClearScreen(void)
{
    Render_ClearScreen();
}

