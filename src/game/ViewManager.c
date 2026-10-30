#include "common.h"


void ViewManager_Init(void) {
    extern void *m_Views_802CD5E0[5];
    extern u8 g_cameraManager[];
    extern s32 g_viewFormat;
    m_Views_802CD5E0[0] = g_cameraManager;
    m_Views_802CD5E0[1] = g_cameraManager;
    m_Views_802CD5E0[2] = g_cameraManager;
    m_Views_802CD5E0[3] = g_cameraManager;
    m_Views_802CD5E0[4] = g_cameraManager;
    g_viewFormat = 0;
}

void ViewManager_SetCamera(void *camera, s32 view) {
    extern void *m_Views_802CD5E0[5];
    m_Views_802CD5E0[view] = camera;
}

void ViewManager_PrepForRender(void) {
    extern void *m_Views_802CD5E0[5];
    extern u8 g_CurrentViews;
    u32 i;

    for (i = 0; i < 5; i++) {
        if (g_CurrentViews & (1 << i)) {
            Gfx_SetRenderWindow(i);
            Camera_PrepareForRender(m_Views_802CD5E0[i]);
        }
    }
}

void ViewManager_EnableView(s32 arg0, s8 arg1) {
    extern u8 g_CurrentViews;
    s32 temp_r3;
    u8 var_r0;

    temp_r3 = 1 << arg0;
    var_r0 = g_CurrentViews & ~temp_r3;
    if (arg1 != 0) {
        var_r0 = g_CurrentViews | temp_r3;
    }
    g_CurrentViews = var_r0;
}

s32 ViewManager_GetViewFormat(void) {
    extern s32 g_viewFormat;
    return g_viewFormat;
}

void ViewManager_SetSinglePlayer(void) {
    extern s32 g_viewFormat;
    extern u8 g_CurrentViews;
    Gfx_SetRenderWindow(0);
    g_CurrentViews |= 1 << 0;
    g_viewFormat = 1;
    g_CurrentViews &= ~(1 << 1);
    g_CurrentViews &= ~(1 << 2);
    g_CurrentViews &= ~(1 << 3);
    g_CurrentViews &= ~(1 << 4);
}

void ViewManager_SetTwoPlayerHorizontal(void) {
    extern s32 g_viewFormat;
    extern u8 g_CurrentViews;
    g_CurrentViews &= ~(1 << 0);
    Gfx_SetRenderWindow(1);
    Gfx_SetRenderWindowSize(0, 0, 0x27F, 0xEF, 0);
    Gfx_SetRenderWindow(2);
    Gfx_SetRenderWindowSize(0, 0xF0, 0x27F, 0x1DF, 0);
    g_CurrentViews &= ~(1 << 3);
    g_viewFormat = 2;
    g_CurrentViews &= ~(1 << 4);
}

void ViewManager_SetThreePlayer(void) {
    extern s32 g_viewFormat;
    extern u8 g_CurrentViews;
    g_CurrentViews &= ~(1 << 0);
    Gfx_SetRenderWindow(1);
    Gfx_SetRenderWindowSize(0, 0, 0x13F, 0xEF, 0);
    Gfx_SetRenderWindow(2);
    Gfx_SetRenderWindowSize(0x140, 0, 0x27F, 0xEF, 0);
    Gfx_SetRenderWindow(3);
    Gfx_SetRenderWindowSize(0, 0xF0, 0x13F, 0x1DF, 0);
    g_CurrentViews &= ~(1 << 4);
    g_viewFormat = 4;
}

void ViewManager_SetFourPlayer(void) {
    extern s32 g_viewFormat;
    extern u8 g_CurrentViews;
    g_CurrentViews &= ~(1 << 0);
    Gfx_SetRenderWindow(1);
    Gfx_SetRenderWindowSize(0, 0, 0x13F, 0xEF, 0);
    Gfx_SetRenderWindow(2);
    Gfx_SetRenderWindowSize(0x140, 0, 0x27F, 0xEF, 0);
    Gfx_SetRenderWindow(3);
    Gfx_SetRenderWindowSize(0, 0xF0, 0x13F, 0x1DF, 0);
    Gfx_SetRenderWindow(4);
    Gfx_SetRenderWindowSize(0x140, 0xF0, 0x27F, 0x1DF, 0);
    g_viewFormat = 5;
}

