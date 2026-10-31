#include "common.h"



typedef struct DebugFontInfo {
    u32 texture;
    u8 unk4[5];
    u8 width;
    u8 height;
    u8 caseMode;
    u8 unkC[0x1C];
    f32 spacing;
} DebugFontInfo;

typedef struct GameDataFonts {
    u8 unk0[0xC8];
    PTR32(DebugFontInfo) debugFont;
} GameDataFonts;

/* Per frame timings (Release_Performance_*), two frames alternating. */
typedef struct TimeBar {
    u32 frame;                  /* 0x00 start of the frame to BeginScene */
    u32 scene;                  /* 0x04 BeginScene to EndScene */
    u32 render;                 /* 0x08 EndScene */
    s32 sleep;                  /* 0x0C g_TripleBufferSleepQTime */
} TimeBar;

TimeBar g_timeBar[2];
TimeBar *g_pActiveTimeBar;
s32 g_timeBarOn;
u32 g_debugDataFlag;
s32 debugFont;
u32 g_bDrawTimeBars;

typedef struct TimeBarVtx {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
    f32 s;
    f32 t;
    GXColor color;
    u8 pad[4];
} TimeBarVtx;

typedef struct TimeBarRect {
    TimeBarVtx v[4];
    u16 tex;
    u8 blendMode;
    u8 window;
} TimeBarRect;


/* The decomp has an m2c placeholder here; the original calls ShowStats_Init (asm). */
void ShowStats_Create(void) {
    ShowStats_Init();
}

void ShowStats_Init(void) {
    DebugFontInfo *font;

    if (World_GetGameData() != NULL) {
        font = GETP(((GameDataFonts *)World_GetGameData())->debugFont);
        g_debugDataFlag = 1;
        memset(g_timeBar, 0, sizeof(g_timeBar));
        g_timeBarOn = 0;
        if (font != NULL) {
            debugFont = Gfx_FontManager_AddFont(font->texture, font->caseMode, font->width, font->height, font->spacing);
            Gfx_SetDebugFont(debugFont);
        }
    }
}

void ShowStats_Release(void) {
    extern s32 debugFont;
    Gfx_FontManager_ReleaseFont(debugFont);
}

void ShowStats_StartFrame(void) {
    extern s32 g_timeBarOn;

    g_timeBarOn = !g_timeBarOn;
    g_pActiveTimeBar = &g_timeBar[g_timeBarOn];
    Performance_Start();
    Release_Performance_Get(&g_pActiveTimeBar->frame);
}

void ShowStats_BeginScene(void) {
    Release_Performance_Diff(&g_pActiveTimeBar->frame);
    Release_Performance_Get(&g_pActiveTimeBar->scene);
    Gfx_BeginScene();
}

void ShowStats_EndScene(void) {
    extern u32 g_debugDataFlag;
    extern s32 g_timeBarOn;
    extern u32 g_bDrawTimeBars;
    extern s32 g_TripleBufferSleepQTime;
    TimeBar *bar = &g_timeBar[!g_timeBarOn];

    if (g_debugDataFlag != 0 && g_bDrawTimeBars != 0) {
        Render_TimeBars(bar);
    }
    Release_Performance_Diff(&g_pActiveTimeBar->scene);
    Release_Performance_Get(&g_pActiveTimeBar->render);
    Gfx_EndScene();
    Release_Performance_Diff(&g_pActiveTimeBar->render);
    g_pActiveTimeBar->sleep = g_TripleBufferSleepQTime;
    Gfx_FlipBuffers();
    ShowStats();
    Performance_End();
}

void ShowStats(void) {
}

void Render_TimeBars(void *bar) {
    TimeBarRect *Gfx_Add2DRectangle(void);
    u32 *times = bar;
    TimeBarVtx v[4];
    TimeBarRect *rect;
    f32 x;
    u32 i;

    {
        f32 left = 50.0f;
        f32 top;

        v[0].x = v[3].x = left;
        x = left + (f32)(times[0] / 60);
        top = 23.0f;
        v[0].y = v[1].y = top;
        v[2].y = v[3].y = 15.0f + top;
        v[1].x = v[2].x = x;
    }
    v[0].z = v[1].z = v[2].z = v[3].z = 0.0f;
    v[0].w = v[1].w = v[2].w = v[3].w = 1.0f;
    v[0].s = v[0].t = 0.0f;
    v[1].s = v[3].t = 1.0f;
    v[2].s = v[2].t = 1.0f;
    v[3].s = v[1].t = 0.0f;
    v[0].color.r = 0;
    v[0].color.b = 0;
    v[0].color.g = 0xFF;
    v[1].color.r = 0;
    v[1].color.b = 0;
    v[1].color.g = 0xFF;
    v[2].color.r = 0;
    v[2].color.b = 0;
    v[2].color.g = 0;
    v[3].color.r = 0;
    v[3].color.b = 0;
    v[3].color.g = 0;
    v[0].color.a = v[1].color.a = v[2].color.a = v[3].color.a = 0xFF;
    rect = Gfx_Add2DRectangle();
    memcpy(rect, v, sizeof(v));
    rect->blendMode = 0;
    rect->tex = 0;

    v[0].x = v[3].x = x;
    x += (f32)(times[1] / 60);
    v[1].x = v[2].x = x;
    v[0].color.r = 0xFF;
    v[0].color.b = 0;
    v[0].color.g = 0x80;
    v[1].color.r = 0xFF;
    v[1].color.b = 0;
    v[1].color.g = 0x80;
    v[2].color.r = 0;
    v[2].color.b = 0;
    v[2].color.g = 0;
    v[3].color.r = 0;
    v[3].color.b = 0;
    v[3].color.g = 0;
    rect = Gfx_Add2DRectangle();
    memcpy(rect, v, sizeof(v));
    rect->blendMode = 0;
    rect->tex = 0;

    v[0].x = v[3].x = x;
    v[1].x = v[2].x = x + (f32)(times[2] / 60);
    v[0].color.r = 0;
    v[0].color.b = 0xFF;
    v[0].color.g = 0;
    v[1].color.r = 0;
    v[1].color.b = 0xFF;
    v[1].color.g = 0;
    v[2].color.r = 0;
    v[2].color.b = 0;
    v[2].color.g = 0;
    v[3].color.r = 0;
    v[3].color.b = 0;
    v[3].color.g = 0;
    rect = Gfx_Add2DRectangle();
    memcpy(rect, v, sizeof(v));
    rect->blendMode = 0;
    rect->tex = 0;

    {
        f32 top = 40.0f;
        f32 left = 50.0f;

        v[0].x = v[3].x = left;
        v[0].y = v[1].y = top;
        v[2].y = v[3].y = 15.0f + top;
        v[1].x = v[2].x = left + (f32)(times[3] / 60);
    }
    v[0].color.r = 0;
    v[0].color.g = 0xFF;
    v[0].color.b = 0xFF;
    v[1].color.r = 0;
    v[1].color.g = 0xFF;
    v[1].color.b = 0xFF;
    v[2].color.r = 0;
    v[2].color.g = 0;
    v[2].color.b = 0;
    v[3].color.r = 0;
    v[3].color.g = 0;
    v[3].color.b = 0;
    rect = Gfx_Add2DRectangle();
    memcpy(rect, v, sizeof(v));
    rect->blendMode = 0;
    rect->tex = 0;

    {
        f32 left = 50.0f;

        v[0].x = v[3].x = left;
        v[0].y = v[1].y = 23.0f;
        v[2].y = v[3].y = 55.0f;
        v[1].x = v[2].x = 1.0f + left;
    }
    v[0].color.r = 0xFF;
    v[0].color.g = 0xFF;
    v[0].color.b = 0xFF;
    v[1].color.r = 0xFF;
    v[1].color.g = 0xFF;
    v[1].color.b = 0xFF;
    v[2].color.r = 0xFF;
    v[2].color.g = 0xFF;
    v[2].color.b = 0xFF;
    v[3].color.r = 0xFF;
    v[3].color.g = 0xFF;
    v[3].color.b = 0xFF;
    rect = Gfx_Add2DRectangle();
    memcpy(rect, v, sizeof(v));
    rect->blendMode = 0;
    rect->tex = 0;

    for (i = 0; i < 4; i++) {
        x = 50.0f + 16666.666f / (f32)(0xF0 >> i);
        v[0].x = v[3].x = x;
        v[1].x = v[2].x = 2.0f + x;
        rect = Gfx_Add2DRectangle();
        memcpy(rect, v, sizeof(v));
        rect->blendMode = 0;
        rect->tex = 0;
    }
}
