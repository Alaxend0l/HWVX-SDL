#include "common.h"
#include "Gfx.h"


extern u32 m_VtxDescFlag;


extern void *g_pDL;
extern void *g_currentBuffer;
extern void *g_frameBuffer1;
extern void *g_frameBuffer2;
extern u8 m_firstFrame;
extern s32 m_srcBlend;
extern s32 m_dstBlend;


extern s32 m_zwrite;
extern s32 m_zstate;
extern s32 m_zmode;
extern f32 m_fogRange2;
extern f32 m_fogRange1;
extern u8 m_fogType;
extern GXColor m_fogColor;
extern s32 m_fogMode;
extern s32 m_cullMode;
extern s32 m_blendMode;


typedef struct GXRenderModeObj {
    u32 viTVmode;
    u16 fbWidth;
    u16 efbHeight;
    u16 xfbHeight;
    u16 viXOrigin;
    u16 viYOrigin;
    u16 viWidth;
    u16 viHeight;
    u32 xFBmode;
    u8 field_rendering;
    u8 aa;
    u8 sample_pattern[12][2];
    u8 vfilter[7];
} GXRenderModeObj;

extern GXRenderModeObj *rmode;
extern void *m_defaultFifo;
extern void *m_defaultFifoObj;
extern u8 ShadowTexture[];
extern u8 pShadowImage[];


typedef struct GfxState {
    u8 unk0[0x60];
    f32 lightDir[4];
    GXColor lightColor;
    GXColor unk74;
    GXColor ambientColor;
    u8 unk7C[0x93 - 0x7C];
    u8 unk93;
    u8 unk94[0xA0 - 0x94];
} GfxState;

extern GfxState g_state;
extern f32 g_identity[4][4];
extern GXColor g_textcolor;
extern GXColor m_clearColor;
extern s32 m_fillMode;
extern s32 m_currentView;
extern s32 m_curLightState;
extern s32 m_curTexture;
typedef struct OSFontHeader {
    u16 fontType;
    u16 firstChar;
    u16 lastChar;
    u16 invalChar;
    u16 ascent;
    u16 descent;
    u16 width;
    u16 leading;
    u16 cellWidth;
    u16 cellHeight;
    u32 sheetSize;
    u16 sheetFormat;
    u16 sheetColumn;
    u16 sheetRow;
    u16 sheetWidth;
    u16 sheetHeight;
    u16 widthTable;
    u32 sheetImage;
    u32 sheetFullSize;
    u8 c0;
    u8 c1;
    u8 c2;
    u8 c3;
} OSFontHeader;


OSFontHeader *FontData;  /* .sbss in the original (local to Render_.o) */
extern s32 __OSCurrHeap;



typedef struct GDLDrawEntry {
    PTR32(void) geometry;
    GXColor color;
    u32 matrix;
    s32 lightState;
    u8 unk10;
    u8 unk11;
    u8 envmapState;
    u8 unk13;
} GDLDrawEntry;

typedef struct GDL {
    u32 numAlphaEntries;      /* 0x00 */
    u8 unk4[0xC - 0x4];
    u32 numAlphaGroups;       /* 0x0C */
    u8 unk10[0x14 - 0x10];
    s32 numTris;              /* 0x14 */
    u8 unk18[0x28 - 0x18];
    u8 unk28[0x2C - 0x28];    /* 0x28 */
    u8 unk2C[0x54 - 0x2C];
    u32 numDrawEntries;
    u8 unk58[0xDFCC0 - 0x58];
    GDLDrawEntry drawEntries[0x800];
} GDL;

typedef struct Vector {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
} Vector;


typedef struct RenderStats {
    s32 numTris;
    s32 debugTriCount;
    u8 unk8[0x100 - 0x8];
} RenderStats;

typedef struct SilhouetteEdge {
    f32 normal0[3];
    u16 v0;
    u16 v1;
    f32 normal1[3];
    s32 type;
} SilhouetteEdge;


typedef union {
    u8 u8;
    u16 u16;
    u32 u32;
    s16 s16;
    s32 s32;
    f32 f32;
} PPCWGPipe;


static inline void GXPosition3f32(f32 x, f32 y, f32 z) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
    GXWGFifo_f32(z);
}

static inline void GXColor1u32(u32 clr) {
    GXWGFifo_u32(clr);
}

static inline void GXPosition3s16(s16 x, s16 y, s16 z) {
    GXWGFifo_s16(x);
    GXWGFifo_s16(y);
    GXWGFifo_s16(z);
}

static inline void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
    GXWGFifo_u8(r);
    GXWGFifo_u8(g);
    GXWGFifo_u8(b);
    GXWGFifo_u8(a);
}

static inline void GXTexCoord2f32(f32 s, f32 t) {
    GXWGFifo_f32(s);
    GXWGFifo_f32(t);
}

static inline void GXTexCoord2s16(s16 s, s16 t) {
    GXWGFifo_s16(s);
    GXWGFifo_s16(t);
}


extern GDL *g_pGDL;
extern RenderStats g_renderStats;
extern s32 g_debugTriCount;



s32 DrawChar(s32 x, s32 y, u16 code) {
    typedef struct {
        u32 dummy[8];
    } GXTexObj;
    typedef struct {
        u8 unk0[0x10];
        u16 cellWidth;
        u16 cellHeight;
        u8 unk14[4];
        u16 sheetFormat;
        u8 unk1A[4];
        u16 sheetWidth;
        u16 sheetHeight;
    } FontHeader;
    #define FontData (*(FontHeader **)(void *)&FontData)
    f32 mtx[3][4];
    GXTexObj tobj;
    char str[4];
    s32 width;
    void *image;
    s32 s;
    s32 t;
    s16 y1;
    s16 y0;
    s16 x1;
    s16 x0;
    s32 s0;
    s32 t0;
    s32 cw;
    s32 ch;

    if (code == 0) {
        return 0;
    }
    if (code < 0x100) {
        str[0] = code;
        str[1] = 0;
    } else {
        str[0] = code >> 8;
        str[1] = code;
        str[2] = 0;
    }
    OSGetFontTexture(str, &image, &s, &t, &width);
    GXInitTexObj(&tobj, image, FontData->sheetWidth, FontData->sheetHeight, FontData->sheetFormat, 0, 0, 0);
    GXInitTexObjLOD(&tobj, 1, 1, 0.0f, 0.0f, 0.0f, 0, 0, 0);
    GXLoadTexObj(&tobj, 0);
    PSMTXScale(mtx, 1.0f / FontData->sheetWidth, 1.0f / FontData->sheetHeight, 1.0f);
    GXLoadTexMtxImm(mtx, 30, 1);
    GXSetTexCoordGen2(0, 1, 4, 30, 0, 125);
    x0 = x;
    y0 = y;
    t0 = t;
    cw = FontData->cellWidth;
    ch = FontData->cellHeight;
    s0 = s;
    x1 = x0 + cw;
    y1 = y0 + ch;
    GXBegin(0x80, 0, 4);
    GXWGFifo_s16(x0);
    GXWGFifo_s16(y0);
    GXWGFifo_s16(0);
    GXWGFifo_s16(s0);
    GXWGFifo_s16(t0);
    GXWGFifo_s16(x1);
    GXWGFifo_s16(y0);
    GXWGFifo_s16(0);
    GXWGFifo_s16(s0 + cw);
    GXWGFifo_s16(t0);
    GXWGFifo_s16(x1);
    GXWGFifo_s16(y1);
    GXWGFifo_s16(0);
    GXWGFifo_s16(s0 + cw);
    GXWGFifo_s16(t0 + ch);
    GXWGFifo_s16(x0);
    GXWGFifo_s16(y1);
    GXWGFifo_s16(0);
    GXWGFifo_s16(s0);
    GXWGFifo_s16(t0 + ch);
    return width;
#undef FontData
}


s32 DrawString(s32 x, s32 y, char *string) {
    typedef struct {
        u32 dummy[8];
    } GXTexObj;
    typedef struct {
        u8 unk0[0x10];
        u16 cellWidth;
        u16 cellHeight;
        u8 unk14[4];
        u16 sheetFormat;
        u8 unk1A[4];
        u16 sheetWidth;
        u16 sheetHeight;
    } FontHeader;
    #define FontData (*(FontHeader **)(void *)&FontData)
    f32 mtx[3][4];
    GXTexObj tobj;
    void *image;
    s32 s;
    s32 t;
    s32 width;
    s16 y1;
    s16 y0;
    s16 x1;
    s16 x0;
    s32 s0;
    s32 t0;
    s32 cw;
    s32 ch;
    s32 total = 0;

    while (*string != 0) {
        string = OSGetFontTexture(string, &image, &s, &t, &width);
        GXInitTexObj(&tobj, image, FontData->sheetWidth, FontData->sheetHeight, FontData->sheetFormat, 0, 0, 0);
        GXInitTexObjLOD(&tobj, 1, 1, 0.0f, 0.0f, 0.0f, 0, 0, 0);
        GXLoadTexObj(&tobj, 0);
        PSMTXScale(mtx, 1.0f / FontData->sheetWidth, 1.0f / FontData->sheetHeight, 1.0f);
        GXLoadTexMtxImm(mtx, 30, 1);
        GXSetTexCoordGen2(0, 1, 4, 30, 0, 125);
        x0 = x + total;
        y0 = y;
        t0 = t;
        cw = FontData->cellWidth;
        ch = FontData->cellHeight;
        s0 = s;
        x1 = x0 + cw;
        y1 = y0 + ch;
        GXBegin(0x80, 0, 4);
        GXPosition3s16(x0, y0, 0);
        GXTexCoord2s16(s0, t0);
        GXPosition3s16(x1, y0, 0);
        GXTexCoord2s16(s0 + cw, t0);
        GXPosition3s16(x1, y1, 0);
        GXTexCoord2s16(s0 + cw, t0 + ch);
        GXPosition3s16(x0, y1, 0);
        GXTexCoord2s16(s0, t0 + ch);
        total += width;
    }
    return total;
#undef FontData
}


void PrintDVDMessage(void) {
    extern s64 g_resetTicks;
    extern s32 m_reset;
    extern s32 m_cd_drive_status;
    extern u32 g_StopInput;

    if (OSGetResetButtonState()) {
        m_reset = 1;
    }
    switch (m_cd_drive_status) {
    case -1:
    case 4:
    case 5:
    case 6:
    case 11:
        break;
    default:
        g_StopInput = 0;
        return;
    }
    g_StopInput = 1;
    g_resetTicks = OSGetTime();
    Gfx_SetOrthoMatrix();
    Render_StateCullMode(0);
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxDesc(11, 1);
    GXSetVtxAttrFmt(0, 9, 1, 3, 0);
    GXSetVtxAttrFmt(0, 11, 1, 5, 0);
    GXSetTevOp(0, 4);
    if (m_firstFrame) {
        GXBegin(0x80, 0, 4);
        GXPosition3s16(0, 0, 0);
        GXColor4u8(0, 0, 0, 0xFF);
        GXPosition3s16(640, 0, 0);
        GXColor4u8(0, 0, 0, 0xFF);
        GXPosition3s16(640, 480, 0);
        GXColor4u8(0, 0, 0, 0xFF);
        GXPosition3s16(0, 480, 0);
        GXColor4u8(0, 0, 0, 0xFF);
    } else {
        GXBegin(0x80, 0, 4);
        GXPosition3s16(120, 140, 0);
        GXColor4u8(0, 0, 0, 0xFF);
        GXPosition3s16(520, 140, 0);
        GXColor4u8(0, 0, 0, 0xFF);
        GXPosition3s16(520, 340, 0);
        GXColor4u8(0, 0, 0, 0xFF);
        GXPosition3s16(120, 340, 0);
        GXColor4u8(0, 0, 0, 0xFF);
    }
    GXFlush();
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxDesc(13, 1);
    GXSetVtxAttrFmt(0, 9, 1, 3, 0);
    GXSetVtxAttrFmt(0, 13, 1, 3, 0);
    Render_StateBlendMode(2);
    GXSetNumTexGens(1);
    GXSetTevOp(0, 3);
    GXSetTevOrder(0, 0, 0, 4);
    switch (m_cd_drive_status) {
    case -1:
        DrawString(130, 190, "  An error has occured. Turn ");
        DrawString(130, 210, "  the power off and refer to ");
        DrawString(130, 230, " the Nintendo GameCube    ");
        DrawString(130, 250, "      Instruction Booklet");
        DrawString(130, 270, "    for further instructions.");
        DrawChar(445, 230, 0x99);
        break;
    case 11:
        DrawString(130, 190, " The Game Disc could not be");
        DrawString(130, 210, "    read. Please read the");
        DrawString(130, 230, "   Nintendo GameCube   ");
        DrawString(130, 250, "     Instruction Booklet");
        DrawString(130, 270, "    for more information.");
        DrawChar(420, 230, 0x99);
        break;
    case 6:
        DrawString(130, 180, "      This is not the");
        DrawString(130, 200, "  Hot Wheels    Velocity X");
        DrawString(130, 220, "         Game Disc.");
        DrawString(130, 240, "     Please insert the");
        DrawString(130, 260, "  Hot Wheels    Velocity X");
        DrawString(130, 280, "         Game Disc.");
        DrawChar(300, 200, 0x99);
        DrawChar(300, 260, 0x99);
        break;
    case 5:
        DrawString(130, 200, "   The Disc Cover is open.");
        DrawString(130, 220, "   If you want to continue");
        DrawString(130, 240, "      the game, please");
        DrawString(130, 260, "    close the Disc Cover.");
        break;
    case 4:
        DrawString(130, 210, "      Please Insert the");
        DrawString(130, 230, "  Hot Wheels    Velocity X");
        DrawString(130, 250, "          Game Disc.");
        DrawChar(300, 230, 0x99);
        break;
    }
    GXSetNumChans(1);
    GXSetTexCoordGen2(0, 1, 12, 60, 0, 125);
    if (m_reset && OSGetResetButtonState() == 0) {
        VISetBlack(1);
        VIWaitForRetrace();
        VIWaitForRetrace();
        OSResetSystem(1, 0, 0);
    }
}




void Render_Create(void) {
    m_fogMode = 0;
    m_fillMode = 0;
    m_currentView = 0;
    GetRenderWindow()->fov = 90.0f;
    memset(GetRenderWindow()->projection, 0, 0x40);
    g_state.unk93 = 0;
    memset(g_identity, 0, 0x40);
    g_identity[3][3] = 1.0f;
    g_identity[2][2] = 1.0f;
    g_identity[1][1] = 1.0f;
    g_identity[0][0] = 1.0f;
    memcpy(GetRenderWindow()->world, g_identity, 0x40);
    memcpy(GetRenderWindow()->view, g_identity, 0x40);
    memcpy(GetRenderWindow()->worldView, g_identity, 0x40);
    memcpy(GetRenderWindow()->worldViewProj, g_identity, 0x40);
    m_blendMode = 1;
    g_textcolor.a = 0xFF;
    g_textcolor.b = 0xFF;
    g_textcolor.g = 0xFF;
    g_textcolor.r = 0xFF;
    m_clearColor.b = 0;
    m_clearColor.g = 0;
    m_clearColor.r = 0;
    m_clearColor.a = 0;
    g_state.ambientColor.r = 0x30;
    g_state.ambientColor.g = 0x30;
    g_state.ambientColor.b = 0x30;
    g_state.ambientColor.a = 0xFF;
    g_state.lightColor.r = 0xC8;
    g_state.lightColor.g = 0xC8;
    g_state.lightColor.b = 0xC8;
    g_state.lightColor.a = 0xFF;
    g_state.lightDir[0] = 0.5f;
    g_state.lightDir[1] = 0.5f;
    g_state.lightDir[2] = 0.0f;
    m_curLightState = -1;
    m_curTexture = 0;
    m_zmode = 3;
    m_zstate = 1;
    m_zwrite = 1;
    FontData = OSAllocFromHeap(__OSCurrHeap, 0x20120);
    OSInitFont(FontData);
}

s32 Render_CreateView(s32 view, u32 width, u32 height) {
    m_defaultFifo = OSAllocFromHeap(__OSCurrHeap, 0x40000);
    m_defaultFifoObj = GXInit(m_defaultFifo, 0x40000);
    GXSetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->xfbHeight, 0.0f, 1.0f);
    GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GXSetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GXSetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
    GXSetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight);
    GXSetCopyFilter(rmode->aa, rmode->sample_pattern, 1, rmode->vfilter);
    GXSetPixelFmt(1, 0);
    GXCopyDisp(g_currentBuffer, 1);
    GXSetDispCopyGamma(1);
    Gfx_SetRenderWindow(0);
    GetRenderWindow()->width = width;
    GetRenderWindow()->halfWidth = width >> 1;
    GetRenderWindow()->height = height;
    GetRenderWindow()->halfHeight = height >> 1;
    GetRenderWindow()->scaleX = 0.0f;
    GetRenderWindow()->scaleY = 0.0f;
    GetRenderWindow()->sizeX = (width * 16) / 640.0f;
    GetRenderWindow()->sizeY = (height * 16) / 480.0f;
    GetRenderWindow()->offsetX = ((0x1000 - width) >> 1) * 16;
    GetRenderWindow()->offsetY = ((0x1000 - height) >> 1) * 16;
    GetRenderWindow()->unk2A0 = GetRenderWindow()->sizeX;
    GetRenderWindow()->unk2A4 = GetRenderWindow()->sizeY;
    GetRenderWindow()->centerX = GetRenderWindow()->offsetX;
    GetRenderWindow()->centerY = GetRenderWindow()->offsetY;
    GetRenderWindow()->left = 0;
    GetRenderWindow()->top = 0;
    GetRenderWindow()->right = width - 1;
    GetRenderWindow()->bottom = height - 1;
    GetRenderWindow()->overlayMode = 0;
    Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
    memcpy(GetRenderWindow()->viewMatrix, GetRenderWindow()->view, 0x40);
    DisplayList_Create();
    GXSetNumChans(1);
    GXSetNumTexGens(1);
    GXSetNumTevStages(1);
    GXSetTevOrder(0, 0, 0, 4);
    GXSetTevOp(0, 0);
    GXEnableTexOffsets(0, 0, 1);
    GXSetPointSize(0xFF, 5);
    GXInitTexObj(ShadowTexture, pShadowImage, 0x140, 0xE0, 1, 0, 0, 0);
    Dprintf("Render Init\n");
    return 1;
}

void Render_StateSetScissor(void) {
    f32 top;
    f32 wd;
    f32 ht;

    Gfx_SetProjectionMatrix(GetRenderWindow()->fov);
    ht = 0.93333334f * GetRenderWindow()->height;
    wd = GetRenderWindow()->width;
    top = 0.93333334f * GetRenderWindow()->top;
    GXSetViewport(GetRenderWindow()->left, top, wd, ht, 0.0f, 1.0f);
    GXSetScissor(GetRenderWindow()->left, 0.93333334f * GetRenderWindow()->top, (f32)GetRenderWindow()->width, 0.93333334f * GetRenderWindow()->height);
}

extern u8 g_CurrentViews;
extern u32 *m_vertexBuffer;
void Render_BeginScene(void) {
    f32 top;
    f32 wd;
    f32 ht;

    Gfx_SetProjectionMatrix(GetRenderWindow()->fov);
    ht = 0.93333334f * GetRenderWindow()->height;
    wd = GetRenderWindow()->width;
    top = 0.93333334f * GetRenderWindow()->top;
    GXSetViewport(GetRenderWindow()->left, top, wd, ht, 0.0f, 1.0f);
    GXSetScissor(GetRenderWindow()->left, 0.93333334f * GetRenderWindow()->top, (f32)GetRenderWindow()->width, 0.93333334f * GetRenderWindow()->height);
    GXInvalidateVtxCache();
    GXInvalidateTexAll();
    GXSetCopyClear(m_clearColor, 0xFFFFFF);
    Render_StateBlendMode(1);
    Gfx_StateTextureBlendMode(2);
    Render_StateAlphaBlendEnable(0);
    Render_StateCullMode(1);
    Render_StateZEnable(1);
    Render_StateZWriteEnable(1);
    if (g_CurrentViews != 1) {
        DisplayList_Init();
    }
    g_debugTriCount = 0;
    Render_StateFogEnable(1);
    m_vertexBuffer = 0;
    m_VtxDescFlag = 0;
    GXSetColorUpdate(1);
    GXSetAlphaUpdate(0);
}

static inline void Render_BeginView(u16 view) {
    extern GfxState g_state;
    f32 ht;
    f32 wd;
    f32 top;
    u32 sht;
    u32 swd;
    u32 stop;

    Gfx_SetRenderWindow(view);
    Gfx_SetProjectionMatrix(GetRenderWindow()->fov);
    ht = 0.93333334f * GetRenderWindow()->height;
    wd = GetRenderWindow()->width;
    top = 0.93333334f * (u32)GetRenderWindow()->top;
    GXSetViewport((u32)GetRenderWindow()->left, top, wd, ht, 0.0f, 1.0f);
    sht = 0.93333334f * GetRenderWindow()->height;
    swd = (f32)GetRenderWindow()->width;
    stop = 0.93333334f * (u32)GetRenderWindow()->top;
    GXSetScissor((u32)GetRenderWindow()->left, stop, swd, sht);
    g_state.unk93 = 0;
    Gfx_SetWorldMatrix(NULL);
}

static inline void Render_SetWindowViewport(void) {
    f32 top;
    f32 wd;
    f32 ht;
    u32 stop;
    u32 swd;
    u32 sht;

    Gfx_SetProjectionMatrix(GetRenderWindow()->fov);
    ht = 0.93333334f * GetRenderWindow()->height;
    wd = GetRenderWindow()->width;
    top = 0.93333334f * (u32)GetRenderWindow()->top;
    GXSetViewport((u32)GetRenderWindow()->left, top, wd, ht, 0.0f, 1.0f);
    sht = 0.93333334f * GetRenderWindow()->height;
    swd = (f32)GetRenderWindow()->width;
    stop = 0.93333334f * (u32)GetRenderWindow()->top;
    GXSetScissor((u32)GetRenderWindow()->left, stop, swd, sht);
}

void Render_EndScene(void) {
    extern GDL *g_pGDL;
    extern u8 g_CurrentViews;
    #define m_fogMode (*(u32 *)(void *)&m_fogMode)
    extern s32 m_cullMode;
    extern s32 g_onebitflag;
    s32 cull;
    u32 view;
    u32 fog;
    u32 i;
    u16 v;
    u16 j;

    fog = m_fogMode;
    if (g_CurrentViews != 1) {
        DisplayList_RenderGXList();
        Render_GeometryListSplitScreen();
    } else {
        Render_GeometryList();
    }
    cull = m_cullMode;
    Render_StateCullMode(0);
    view = 1;
    g_onebitflag = 1;
    if (g_CurrentViews == 1) {
        for (i = 0; i < g_pGDL->numAlphaGroups; i++) {
            Render_DrawAlphaGroup(i);
        }
    } else {
        for (; view < 5; view++) {
            if (g_CurrentViews & (1 << view)) {
                Gfx_SetRenderWindow(view);
                Render_SetWindowViewport();
                for (i = 0; i < g_pGDL->numAlphaGroups; i++) {
                    Render_DrawAlphaGroup(i);
                }
            }
        }
    }
    Render_StateCullMode(cull);
    g_onebitflag = 0;
    Render_StateFogEnable(0);
    if (g_CurrentViews == 1) {
        Render_DrawShadows();
    }
    Gfx_SetWorldMatrix(NULL);
    cull = m_cullMode;
    Render_StateCullMode(0);
    if (g_CurrentViews == 1) {
        Render_DrawTriGroup();
    } else {
        for (v = 1; v < 5; v++) {
            if (g_CurrentViews & (1 << v)) {
                Render_BeginView(v);
                Render_DrawTriGroup();
            }
        }
    }
    Render_StateCullMode(cull);
    if (g_CurrentViews == 1) {
        Render_SetTextureHandle(0xFFFF);
        Render_StateAlphaBlendEnable(1);
        Render_StateBlendMode(6);
        Render_DrawSpheres();
        Render_DrawBoundingBoxes();
        Render_Draw3DFXLines();
        Render_StateAlphaBlendEnable(0);
        Render_Draw3DLines();
        Render_StateZWriteEnable(0);
        Render_DrawPointSprites();
        Render_StateZWriteEnable(1);
    } else {
        for (v = 1; v < 5; v++) {
            if (g_CurrentViews & (1 << v)) {
                Render_BeginView(v);
                Render_SetTextureHandle(0xFFFF);
                Render_StateAlphaBlendEnable(1);
                Render_StateBlendMode(6);
                Render_DrawSpheres();
                Render_DrawBoundingBoxes();
                Render_Draw3DFXLines();
                Render_StateAlphaBlendEnable(0);
                Render_Draw3DLines();
                Render_StateZWriteEnable(0);
                Render_DrawPointSprites();
                Render_StateZWriteEnable(1);
            }
        }
    }
    Gfx_SetWorldMatrix(NULL);
    cull = m_cullMode;
    Render_StateZWriteEnable(0);
    Render_StateFogEnable(fog);
    Render_StateAlphaBlendEnable(1);
    Render_StateCullMode(0);
    if (g_CurrentViews == 1) {
        Gfx_SortAlpha();
        if (g_pGDL->numAlphaEntries != 0) {
            DrawAlphaTable();
        }
        Render_DrawNewBBSprites();
    } else {
        for (v = 1; v < 5; v++) {
            if (g_CurrentViews & (1 << v)) {
                Render_BeginView(v);
                Gfx_SortAlpha();
                if (g_pGDL->numAlphaEntries != 0) {
                    DrawAlphaTable();
                }
                Render_DrawNewBBSprites();
            }
        }
    }
    Render_StateAlphaBlendEnable(0);
    Render_StateZWriteEnable(1);
    if (g_CurrentViews != 1) {
        Gfx_SetRenderWindow(0);
        Render_SetWindowViewport();
    }
    Gfx_SetOrthoMatrix();
    Render_StateZEnable(0);
    if (g_CurrentViews == 1) {
        Render_StateAlphaBlendEnable(1);
        Render_DrawSprite();
    }
    Render_DrawRectanglesWindow();
    Render_DrawRectangles();
    Render_SetTextureHandle(0xFFFF);
    for (j = 0; j < *(u32 *)g_pGDL->unk28; j++) {
        Render_DrawLine2D((u8 *)g_pGDL + 0xAE700 + j * 0x30);
    }
    Render_StateZWriteEnable(1);
    GXSetBlendMode(1, 4, 5, 5);
    Render_Print();
    Render_StateAlphaBlendEnable(0);
    Render_StateCullMode(cull);
    Render_StateZEnable(1);
    if (fog == 1) {
        Render_StateFogEnable(1);
    }
    Render_ScreenFlash();
#undef m_fogMode
}

void Render_FlipBuffers(void) {
    PrintDVDMessage();
    GXSetColorUpdate(1);
    GXCopyDisp(g_currentBuffer, 1);
    GXDrawDone();
    VISetNextFrameBuffer(g_currentBuffer);
    if (m_firstFrame) {
        VISetBlack(0);
        m_firstFrame = 0;
    }
    VIFlush();
    VIWaitForRetrace();
    if (g_currentBuffer == g_frameBuffer1) {
        g_currentBuffer = g_frameBuffer2;
    } else {
        g_currentBuffer = g_frameBuffer1;
    }
}

s32 DisplayList_Create(void) {
    g_pDL = Allocate(0x125DC, 0, "");
    if (g_pDL != NULL) {
        DisplayList_Init();
        return 1;
    }
    return 0;
}

void DisplayList_Init(void) {
    memset(g_pDL, 0, 0x18);
}

void Render_StateBlendMode(u32 mode) {
    switch (mode) {
    case 0:
    case 1:
        m_srcBlend = 1;
        m_dstBlend = 0;
        break;
    case 2:
        m_srcBlend = 1;
        m_dstBlend = 1;
        break;
    case 3:
    case 8:
    case 10:
        m_srcBlend = 0;
        m_dstBlend = 3;
        break;
    case 5:
    case 6:
    case 7:
        m_srcBlend = 4;
        m_dstBlend = 5;
        break;
    default:
        m_srcBlend = 1;
        m_dstBlend = 0;
        break;
    }
    GXSetBlendMode(1, m_srcBlend, m_dstBlend, 5);
}

void Gfx_StateTextureBlendMode(s32 mode) {
    m_blendMode = mode;
    switch (mode) {
    case 1:
        GXSetTevOp(0, 1);
        break;
    case 2:
        GXSetTevOp(0, 0);
        break;
    case 3:
        GXSetTevOp(0, 3);
        break;
    case 4:
        GXSetTevOp(0, 2);
        break;
    }
}

void Render_StateAlphaBlendEnable(s32 enable) {
    s32 fogMode = m_fogMode;

    if (enable == 1) {
        GXSetBlendMode(1, m_srcBlend, m_srcBlend, 5);
        GXSetFog(0, 1.0f, 10.0f, 1.0f, 10.0f, m_fogColor);
    } else {
        GXSetBlendMode(0, 4, 5, 5);
        m_fogMode = !m_fogMode;
        Render_StateFogEnable(fogMode);
    }
}

void Render_StateCullMode(s32 mode) {
    switch (mode) {
    case 0:
        GXSetCullMode(0);
        break;
    case 1:
        GXSetCullMode(1);
        break;
    case 2:
        GXSetCullMode(2);
        break;
    }
    m_cullMode = mode;
}

void Render_StateZEnable(s32 enable) {
    GXSetZMode(enable, m_zmode, m_zwrite);
    m_zstate = enable;
}

void Render_StateZWriteEnable(s32 enable) {
    GXSetZMode(m_zstate, m_zmode, enable);
    m_zwrite = enable;
}

void Render_StateFogEnable(s32 enable) {
    if (m_fogMode != enable) {
        if (enable) {
            GXSetFog(m_fogType, m_fogRange1, m_fogRange2, 1.0f, 15000.0f, m_fogColor);
        } else {
            GXSetFog(0, 1.0f, 10.0f, 1.0f, 10.0f, m_fogColor);
        }
        m_fogMode = enable;
    }
}

void Gfx_GetRenderStats(RenderStats *stats) {
    g_renderStats.numTris = g_pGDL->numTris;
    g_renderStats.debugTriCount = g_debugTriCount;
    *stats = g_renderStats;
}

void Render_ClearScreen(void) {

}

void Set2DRectVtxDesc(void) {
    if (m_VtxDescFlag != 2) {
        m_VtxDescFlag = 2;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxDesc(0xD, 1);
        GXSetVtxAttrFmt(0, 9, 0, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
    }
}

void Set2DRectFlatVtxDesc(void) {
    if (m_VtxDescFlag != 0x80) {
        m_VtxDescFlag = 0x80;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxAttrFmt(0, 9, 0, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
    }
}

void Set3DRectVtxDesc(void) {
    if (m_VtxDescFlag != 0x40) {
        m_VtxDescFlag = 0x40;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxDesc(0xD, 1);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
    }
}

void SetTriFlatVtxDesc(void) {
    if (m_VtxDescFlag != 0x20) {
        m_VtxDescFlag = 0x20;
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xB, 1);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
    }
}

void Gfx_SetFog(GXColor *color, u8 type, f32 start, f32 end) {
    extern GXColor m_fogColor;
    extern u8 m_fogType;
    extern f32 m_fogRange1;
    extern f32 m_fogRange2;
    u8 r, g, b, a;
    f32 scale;
    switch (type) {
    case 0:
        m_fogType = 2;
        break;
    case 1:
        m_fogType = 4;
        break;
    case 2:
        m_fogType = 5;
        break;
    }
    a = color->a;
    b = color->b;
    g = color->g;
    r = color->r;
    m_fogColor.g = g;
    m_fogColor.r = r;
    m_fogColor.b = b;
    m_fogColor.a = 0xFF;
    if (a != 0) {
        scale = a / 255.0f;
        m_fogRange2 = end / scale;
        m_fogRange1 = start / scale;
    } else {
        m_fogRange1 = 0.0f;
        m_fogRange2 = 0.0f;
    }
}

void Render_Destroy(void) {

}

void Gfx_GetViewport(s32 *x, s32 *y, s32 *width, s32 *height) {
    *x = 0;
    *y = 0;
    *width = 640;
    *height = 480;
}

void DrawVolume(f32 (*verts)[3], SilhouetteEdge *edges, u32 numEdges, f32 dx, f32 dy, f32 dz);
void CreateSilhouette(SilhouetteEdge *edges, u32 count, f32 *dir);


void Render_DrawShadows(void) {
    typedef struct ShadowGroup {
        u16 unk0;
        u16 flags;
        u8 unk4[0x10 - 0x4];
        u32 numEdges;
        u8 unk14[0x1C - 0x14];
        PTR32(SilhouetteEdge) edges;
    } ShadowGroup;
    typedef struct ShadowGeometry {
        u16 unk0;
        u16 numGroups;
        u8 unk4[0x8 - 0x4];
        PTR32(ShadowGroup) groups;
        PTR32(__typeof__(f32[3])) verts;
    } ShadowGeometry;
    typedef struct ShadowModel {
        u8 unk0[0x8];
        PTR32(ShadowGeometry) geometry;
    } ShadowModel;
    Vector dir __attribute__((aligned(16)));
    Vector lightDir __attribute__((aligned(16))) = { 0.707f, -0.707f, 0.0f, 0.0f };
    GXColor color;
    u32 i;
    u32 numShadows;
    ShadowModel *model;
    u32 j;
    void *matrix;

    numShadows = 0;
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxDesc(0xB, 1);
    GXSetVtxAttrFmt(0, 9, 1, 4, 0);
    GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);
    m_VtxDescFlag = 0;
    for (i = 0; i < g_pGDL->numDrawEntries; i++) {
        model = (ShadowModel *)GETP(g_pGDL->drawEntries[i].geometry);
        matrix = Gfx_GetWorldMatrixState(g_pGDL->drawEntries[i].matrix);
        if (GETP(GETP(model->geometry)->groups)->flags & 0x4000) {
            numShadows++;
            Gfx_SetWorldMatrix(matrix);
            InverseXformVecOrientation(GetRenderWindow(), &lightDir, &dir);
            for (j = 0; j < GETP(model->geometry)->numGroups; j++) {
                CreateSilhouette(GETP(GETP(GETP(model->geometry)->groups)[j].edges), GETP(GETP(model->geometry)->groups)[j].numEdges, &dir.x);
            }
            GXSetColorUpdate(0);
            GXSetZMode(1, 1, 0);
            GXSetAlphaUpdate(1);
            GXSetTevOp(0, 4);
            GXSetNumTexGens(0);
            GXSetTevOrder(0, 0xFF, 0xFF, 4);
            GXSetBlendMode(1, 1, 1, 5);
            for (j = 0; j < GETP(model->geometry)->numGroups; j++) {
                DrawVolume(GETP(GETP(model->geometry)->verts), GETP(GETP(GETP(model->geometry)->groups)[j].edges), GETP(GETP(model->geometry)->groups)[j].numEdges,
                           300.0f * dir.x, 300.0f * dir.y, 300.0f * dir.z);
            }
            GXSetBlendMode(1, 0, 3, 5);
            GXSetCullMode(2);
            for (j = 0; j < GETP(model->geometry)->numGroups; j++) {
                DrawVolume(GETP(GETP(model->geometry)->verts), GETP(GETP(GETP(model->geometry)->groups)[j].edges), GETP(GETP(model->geometry)->groups)[j].numEdges,
                           300.0f * dir.x, 300.0f * dir.y, 300.0f * dir.z);
            }
            GXSetCullMode(1);
        }
    }
    if (numShadows) {
        color.r = color.g = color.b = 0;
        color.a = 100;
        GXSetTexCopySrc(0, 0, 0x280, 0x1C0);
        GXSetTexCopyDst(0x140, 0xE0, 0x27, 1);
        GXCopyTex(pShadowImage, 0);
        GXPixModeSync();
        GXLoadTexObj((GXTexObj *)ShadowTexture, 0);
        GXSetTevOrder(0, 0, 0, 0xFF);
        GXSetTevColorOp(0, 0, 0, 0, 1, 0);
        GXSetTevColorIn(0, 0xF, 0xF, 0xF, 2);
        GXSetTevAlphaOp(0, 0xE, 0, 0, 1, 0);
        GXSetTevAlphaIn(0, 4, 7, 1, 7);
        GXSetBlendMode(1, 4, 5, 3);
        GXSetAlphaCompare(7, 0, 1, 7, 0);
        GXSetTevColor(1, color);
        GXSetColorUpdate(1);
        GXSetNumTexGens(1);
        GXSetTexCoordGen2(0, 1, 0xC, 0x3C, 0, 0x7D);
        Gfx_SetOrthoMatrix();
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xD, 1);
        GXSetVtxAttrFmt(0, 9, 1, 3, 0);
        GXSetVtxAttrFmt(0, 0xD, 1, 4, 0);
        GXBegin(0x80, 0, 4);
        GXPosition3s16(0, 0, 0);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3s16(0, 0x1E0, 0);
        GXTexCoord2f32(0.0f, 1.0f);
        GXPosition3s16(0x280, 0x1E0, 0);
        GXTexCoord2f32(1.0f, 1.0f);
        GXPosition3s16(0x280, 0, 0);
        GXTexCoord2f32(1.0f, 0.0f);
        GXSetAlphaUpdate(1);
        GXSetZMode(1, 1, 1);
        Gfx_SetProjectionMatrix(GetRenderWindow()->fov);
    }
}


void CreateSilhouette(SilhouetteEdge *edges, u32 count, f32 *dir) {
    SilhouetteEdge *e;
    u32 i;
    f32 d0;
    f32 d1;

    e = edges;
    for (i = 0; i < count; i++) {
        d0 = PSVECDotProduct(e->normal0, dir);
        d1 = PSVECDotProduct(e->normal1, dir);
        if (d0 < 0.0f && d1 > 0.0f) {
            e->type = 2;
        } else if (d0 > 0.0f && d1 < 0.0f) {
            e->type = 1;
        } else {
            e->type = 0;
        }
        e++;
    }
}

void DrawVolume(f32 (*verts)[3], SilhouetteEdge *edges, u32 numEdges, f32 dx, f32 dy, f32 dz) {
    SilhouetteEdge *e;
    u32 i;

    e = edges;
    for (i = 0; i < numEdges; i++) {
        if (e->type & 2) {
            GXBegin(0x80, 0, 4);
            GXPosition3f32(verts[e->v0][0], verts[e->v0][1], verts[e->v0][2]);
            GXColor1u32(0xFF0004);
            GXPosition3f32(verts[e->v1][0], verts[e->v1][1], verts[e->v1][2]);
            GXColor1u32(0xFF0004);
            GXPosition3f32(dx + verts[e->v1][0], dy + verts[e->v1][1], dz + verts[e->v1][2]);
            GXColor1u32(0xFF0004);
            GXPosition3f32(dx + verts[e->v0][0], dy + verts[e->v0][1], dz + verts[e->v0][2]);
            GXColor1u32(0xFF0004);
        }
        if (e->type & 1) {
            GXBegin(0x80, 0, 4);
            GXPosition3f32(verts[e->v1][0], verts[e->v1][1], verts[e->v1][2]);
            GXColor1u32(0xFF0004);
            GXPosition3f32(verts[e->v0][0], verts[e->v0][1], verts[e->v0][2]);
            GXColor1u32(0xFF0004);
            GXPosition3f32(dx + verts[e->v0][0], dy + verts[e->v0][1], dz + verts[e->v0][2]);
            GXColor1u32(0xFF0004);
            GXPosition3f32(dx + verts[e->v1][0], dy + verts[e->v1][1], dz + verts[e->v1][2]);
            GXColor1u32(0xFF0004);
        }
        e++;
    }
}

