#include "common.h"
#include "Gfx.h"

typedef union {
    u8 u8;
    u16 u16;
    u32 u32;
    s32 s32;
    f32 f32;
} PPCWGPipe;


typedef struct GDLPointSprite {
    f32 x, y, z; /* 0x00 */
    f32 size;    /* 0x0C */
    u32 color;   /* 0x10 */
    u16 tex;     /* 0x14 */
    u8 blendMode; /* 0x16 */
    u8 unk17;
} GDLPointSprite;

typedef GDL2DVtx GDLTriVtx;

typedef struct GDLTri {
    GDLTriVtx v[3]; /* 0x00 */
    u16 tex;        /* 0x60 */
    s16 blendMode;  /* 0x62 */
} GDLTri;

typedef struct GDLSprite {
    Vector4 pos;
    u8 unk10[0x1C - 0x10];
    f32 rot;
    u32 next;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    u8 unk28[2];
    u8 blendMode;
    u8 unk2B[0x30 - 0x2B];
} GDLSprite;

typedef struct GDL2DSprite {
    f32 x;
    f32 y;
    f32 z;
    f32 unkC;
    u8 unk10[0x1C - 0x10];
    f32 unk1C;
    u8 unk20[0x24 - 0x20];
    GXColor color;
    u16 tex;
    u8 flag;
    u8 window;
    u8 unk2C[0x30 - 0x2C];
} GDL2DSprite;

typedef struct GDLGroupTri {
    u8 type;   /* 0x00 0: tris, 1: lit tris, 4: quads */
    u8 unk1[3];
    u16 tex;   /* 0x04 */
    u16 blendMode; /* 0x06 */
    u32 start; /* 0x08 */
    u32 count; /* 0x0C */
    u8 unk10[0x20 - 0x10];
} GDLGroupTri;

typedef struct GDLGroupTriVtx {
    u32 r, g, b, a;
    f32 x0, y0, z0, s0;
    f32 x1, y1, z1, t0;
    f32 x2, y2, z2, unk3C;
    f32 s1, t1, s2, t2;
} GDLGroupTriVtx;

typedef struct GDLGroupQuadVtx {
    u32 r, g, b, a;
    f32 x0, y0, z0, s;
    f32 x1, y1, z1, t;
    f32 x2, y2, z2, unk3C;
    f32 x3, y3, z3, unk4C;
} GDLGroupQuadVtx;

typedef struct GDLGroupLitTriVtx {
    u32 c[3][4];
    f32 x0, y0, z0, s0;
    f32 x1, y1, z1, t0;
    f32 x2, y2, z2, unk5C;
    f32 s1, t1, s2, t2;
} GDLGroupLitTriVtx;

typedef struct GDL {
    u8 unk0[0x4];
    u32 num2DSprites;
    u32 numSprites;
    u8 unkC[0x18 - 0xC];
    u32 numRects2D;
    u8 unk1C[0x20 - 0x1C];
    u32 numRects;
    u8 unk24[0x44 - 0x24];
    u32 numGroupTris;
    u8 unk48[0x68 - 0x48];
    u32 numPointSprites;
    u8 unk6C[0x9EC0 - 0x6C];
    GDL2DSprite sprites2D[2000];
    GDLSprite sprites[0x7CF];
    u8 unk38C90[0x38CC0 - 0x38C90];
    GDLTri tris[2000];
    u8 unk69A00[0x7EA00 - 0x69A00];
    GDLRect rects2D[1000];
    u8 unk9EDA0[0x9FA00 - 0x9EDA0];
    GDLRect rects[1000];
    u8 unkBFDA0[0xEDCC0 - 0xBFDA0];
    GDLPointSprite pointSprites[1000];
    u8 unkF3A80[0x24CDE0 - 0xF3A80];
    PTR32(GDLGroupTriVtx) groupTriVtx;      /* 0x24CDE0 */
    PTR32(GDLGroupTri) groupTris;           /* 0x24CDE4 */
    PTR32(GDLGroupQuadVtx) groupQuadVtx;    /* 0x24CDE8 */
    PTR32(GDLGroupLitTriVtx) groupLitTriVtx; /* 0x24CDEC */
    u8 unk24CDF0[0x24CDF4 - 0x24CDF0];
    u16 spriteHeads[0x1FF];
} GDL;

extern GDL *g_pGDL;
extern s32 g_debugTriCount;

static inline void GXPosition3f32(f32 x, f32 y, f32 z) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
    GXWGFifo_f32(z);
}

static inline void GXColor4u8(const u8 r, const u8 g, const u8 b, const u8 a) {
    GXWGFifo_u8(r);
    GXWGFifo_u8(g);
    GXWGFifo_u8(b);
    GXWGFifo_u8(a);
}

static inline void GXTexCoord2f32(f32 s, f32 t) {
    GXWGFifo_f32(s);
    GXWGFifo_f32(t);
}



extern u8 g_screenText[0xA00];

extern GXColor g_textcolor;

typedef struct FontChar {
    f32 unk0;
    f32 unk4;
    f32 unk8;
    f32 unkC;
    s8 spacingX;
    s8 spacingY;
    u8 unk12[0x14 - 0x12];
} FontChar;

typedef struct Font {
    f32 height;
    f32 width;
    PTR32(char) letters;
    u16 texture;
    u8 caseMode;
    u8 unkF;
    s8 inUse;
    u8 unk11;
    u16 refCount;
    FontChar chars[128];
} Font;

typedef struct FontManager {
    u32 numFonts;
    Font fonts[16];
} FontManager;

extern FontManager g_fontManager;


extern u8 g_CurrentViews;

static inline void GXPosition2f32(f32 x, f32 y) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
}

static inline void FontManager_Vertex2(f32 x, f32 y, f32 s, f32 t) {
    GXWGFifo_f32(x);
    GXWGFifo_f32(y);
    GXWGFifo_u8(g_textcolor.r);
    GXWGFifo_u8(g_textcolor.g);
    GXWGFifo_u8(g_textcolor.b);
    GXWGFifo_u8(g_textcolor.a);
    GXTexCoord2f32(s, t);
}


void Render_Print(void) {
    f32 y;
    s32 i;
    s32 j;
    char buf[88];

    y = 0.0f;
    for (i = 0; i < 32; i++) {
        for (j = 0; j < 80; j++) {
            buf[j] = g_screenText[i * 80 + j];
        }
        buf[j] = 0;
        y += FontManager_Print(buf, 0.0f, y, g_state.debugFont);
    }
}


u32 FontManager_Print(char *str, f32 x, f32 y, s32 id) {
    Font *font = &g_fontManager.fonts[id];
    u32 i;
    u32 count = 0;
    u8 c;

    Render_SetTextureHandle(font->texture);
    Set2DRectVtxDesc();
    for (i = 0; i < strlen(str); i++) {
        if ((u8)str[i] != ' ') {
            count++;
        }
    }
    GXBegin(0x80, 0, count * 4);
    g_debugTriCount += count * 2;
    for (i = 0; i < strlen(str); i++) {
        c = str[i];
        if (c >= 'a' && c <= 'z') {
            c -= 0x20;
        }
        if (c != ' ') {
            f32 y1 = y + font->height;
            f32 x1 = 0.6f * (u8)font->chars[c].spacingX + x;

            FontManager_Vertex2(x, y, font->chars[c].unk0, font->chars[c].unk4);
            FontManager_Vertex2(x, y1, font->chars[c].unk0, font->chars[c].unkC);
            FontManager_Vertex2(x1, y1, font->chars[c].unk8, font->chars[c].unkC);
            FontManager_Vertex2(x1, y, font->chars[c].unk8, font->chars[c].unk4);
        }
        x += 0.6f * (u8)font->chars[' '].spacingX - 2.0f;
    }
    return font->height;
}



void Render_DrawPointSprites(void) {
    u32 i;
    u32 color;

    SetTriVtxDesc();
    Render_StateAlphaBlendEnable(1);
    for (i = 0; i < g_pGDL->numPointSprites; i++) {
        Render_SetTextureHandle(g_pGDL->pointSprites[i].tex);
        Render_StateBlendMode(g_pGDL->pointSprites[i].blendMode);
        GXSetPointSize(255.0f * g_pGDL->pointSprites[i].size, 5);
        GXBegin(0xB8, 0, 1);
        GXPosition3f32(g_pGDL->pointSprites[i].x, g_pGDL->pointSprites[i].y, g_pGDL->pointSprites[i].z);
        color = g_pGDL->pointSprites[i].color;
        GXColor4u8(color >> 24, color >> 16, color >> 8, color);
        GXTexCoord2f32(0.0f, 0.0f);
    }
    Render_StateAlphaBlendEnable(0);
}

void Render_ScreenFlash(void) {
    f32 w;
    f32 zero = 0.0f;

    if (GetRenderWindow()->overlayMode != 0) {
        Render_StateZWriteEnable(0);
        Render_StateAlphaBlendEnable(1);
        Render_StateBlendMode(GetRenderWindow()->overlayMode);
        Render_SetTextureHandle(0xFFFF);
        Set2DRectFlatVtxDesc();
        GXBegin(0x80, 0, 4);
        GXPosition2f32(zero, zero);
        GXColor4u8(GetRenderWindow()->overlayColor.r, GetRenderWindow()->overlayColor.g,
                   GetRenderWindow()->overlayColor.b, GetRenderWindow()->overlayColor.a);
        GXPosition2f32(zero, GetRenderWindow()->top);
        GXColor4u8(GetRenderWindow()->overlayColor.r, GetRenderWindow()->overlayColor.g,
                   GetRenderWindow()->overlayColor.b, GetRenderWindow()->overlayColor.a);
        w = GetRenderWindow()->left;
        GXPosition2f32(w, GetRenderWindow()->top);
        GXColor4u8(GetRenderWindow()->overlayColor.r, GetRenderWindow()->overlayColor.g,
                   GetRenderWindow()->overlayColor.b, GetRenderWindow()->overlayColor.a);
        GXPosition2f32(GetRenderWindow()->left, zero);
        GXColor4u8(GetRenderWindow()->overlayColor.r, GetRenderWindow()->overlayColor.g,
                   GetRenderWindow()->overlayColor.b, GetRenderWindow()->overlayColor.a);
        Render_StateZWriteEnable(1);
        Render_StateAlphaBlendEnable(0);
    }
}

void Render_DrawSprite(void) {
    f32 s;
    f32 c;
    u32 i;
    u32 window = 0;
    f32 x;
    f32 y;
    f32 zero = 0.0f;
    f32 x2, x3, x0, x1, y3, y1, y0;
    f32 y2;
    f32 size;
    f32 left;
    f32 top;

    Render_StateZWriteEnable(0);
    for (i = 0; i < g_pGDL->num2DSprites; i++) {
        g_debugTriCount += 2;
        Render_StateBlendMode(g_pGDL->sprites2D[i].flag);
        Render_SetTextureHandle(g_pGDL->sprites2D[i].tex);
        if (g_CurrentViews != 1 && window != g_pGDL->sprites2D[i].window) {
            window = g_pGDL->sprites2D[i].window;
            Gfx_SetRenderWindow(window);
            Render_StateSetScissor();
        }
        size = 0.5f * g_pGDL->sprites2D[i].unkC;
        x = g_pGDL->sprites2D[i].x;
        y = g_pGDL->sprites2D[i].y;
        if (zero == g_pGDL->sprites2D[i].unk1C) {
            left = x - size;
            top = y - size;
            x += size;
            y += size;
            x0 = left;
            x2 = x;
            x3 = x;
            x1 = left;
            y0 = top;
            y3 = top;
            y1 = y;
            y2 = y;
        } else {
            SinCos(&s, &c, 45.0f + g_pGDL->sprites2D[i].unk1C);
            size *= 1.4142137f;
            s *= size;
            c *= size;
            y3 = y - s;
            x3 = x + c;
            x1 = x - c;
            y1 = y + s;
            x2 = x + s;
            y2 = y + c;
            x0 = x - s;
            y0 = y - c;
        }
        Set2DRectVtxDesc();
        GXBegin(0x80, 0, 4);
        GXPosition2f32(x0, y0);
        GXColor4u8(g_pGDL->sprites2D[i].color.r, g_pGDL->sprites2D[i].color.g, g_pGDL->sprites2D[i].color.b, g_pGDL->sprites2D[i].color.a);
        GXTexCoord2f32(0.0f, 1.0f);
        GXPosition2f32(x1, y1);
        GXColor4u8(g_pGDL->sprites2D[i].color.r, g_pGDL->sprites2D[i].color.g, g_pGDL->sprites2D[i].color.b, g_pGDL->sprites2D[i].color.a);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition2f32(x2, y2);
        GXColor4u8(g_pGDL->sprites2D[i].color.r, g_pGDL->sprites2D[i].color.g, g_pGDL->sprites2D[i].color.b, g_pGDL->sprites2D[i].color.a);
        GXTexCoord2f32(1.0f, 0.0f);
        GXPosition2f32(x3, y3);
        GXColor4u8(g_pGDL->sprites2D[i].color.r, g_pGDL->sprites2D[i].color.g, g_pGDL->sprites2D[i].color.b, g_pGDL->sprites2D[i].color.a);
        GXTexCoord2f32(1.0f, 1.0f);
    }
    if (window != 0) {
        Gfx_SetRenderWindow(0);
        Render_StateSetScissor();
    }
    Render_StateZWriteEnable(1);
}


void Render_DrawNewBBSprites(void) {
    Vector4 out __attribute__((aligned(16)));
    f32 s;
    f32 c;
    u32 tex;
    u32 blend;
    s8 first;
    u16 idx;
    u32 cur;
    f32 z;
    f32 x;
    f32 y;
    f32 x2, x3, x1, y0, y3, y1, y2;
    f32 x0;
    f32 size;
    f32 left;
    f32 top;

    blend = -1;
    if (g_pGDL->numSprites == 0) {
        return;
    }
    GXSetCurrentMtx(0x3C);
    for (tex = 0; tex < 0x1FF; tex++) {
        first = 1;
        idx = g_pGDL->spriteHeads[tex];
        if (idx == 0xFFFF) {
            continue;
        }
        while (idx < 0xFFFF) {
            cur = idx;
            idx = g_pGDL->sprites[cur].next;
            XformVec4x4((Matrix4 *)GetRenderWindow()->view, &g_pGDL->sprites[cur].pos, &out);
            if (out.z > -2.0f) {
                continue;
            }
            g_debugTriCount += 2;
            if (blend != g_pGDL->sprites[cur].blendMode) {
                blend = g_pGDL->sprites[cur].blendMode;
                Render_StateBlendMode(blend);
            }
            if (first) {
                Render_SetTextureHandle(tex);
                first = 0;
            }
            size = 0.5f * g_pGDL->sprites[cur].pos.w;
            z = out.z;
            x = out.x;
            y = out.y;
            if (0.0f == g_pGDL->sprites[cur].rot) {
                left = x - size;
                top = y - size;
                x += size;
                y += size;
                x2 = left;
                x3 = left;
                x1 = x;
                y0 = top;
                y3 = top;
                y1 = y;
                y2 = y;
                x0 = x;
            } else {
                SinCos(&s, &c, 45.0f + g_pGDL->sprites[cur].rot);
                size *= 1.4142137f;
                s *= size;
                c *= size;
                y3 = y + s;
                x3 = x + c;
                x1 = x - c;
                y1 = y - s;
                x2 = x + s;
                y2 = y - c;
                x0 = x - s;
                y0 = y + c;
            }
            Set3DRectVtxDesc();
            GXBegin(0x80, 0, 4);
            GXPosition3f32(x0, y0, z);
            GXColor4u8(g_pGDL->sprites[cur].r, g_pGDL->sprites[cur].g, g_pGDL->sprites[cur].b, g_pGDL->sprites[cur].a);
            GXTexCoord2f32(1.0f, 0.0f);
            GXPosition3f32(x1, y1, z);
            GXColor4u8(g_pGDL->sprites[cur].r, g_pGDL->sprites[cur].g, g_pGDL->sprites[cur].b, g_pGDL->sprites[cur].a);
            GXTexCoord2f32(1.0f, 1.0f);
            GXPosition3f32(x2, y2, z);
            GXColor4u8(g_pGDL->sprites[cur].r, g_pGDL->sprites[cur].g, g_pGDL->sprites[cur].b, g_pGDL->sprites[cur].a);
            GXTexCoord2f32(0.0f, 1.0f);
            GXPosition3f32(x3, y3, z);
            GXColor4u8(g_pGDL->sprites[cur].r, g_pGDL->sprites[cur].g, g_pGDL->sprites[cur].b, g_pGDL->sprites[cur].a);
            GXTexCoord2f32(0.0f, 0.0f);
        }
    }
}


extern s32 g_debugTriCount;
void Render_DrawAlphaTriangle(u16 index) {
    g_debugTriCount++;
    Render_StateBlendMode(g_pGDL->tris[index].blendMode);
    Render_SetTextureHandle(g_pGDL->tris[index].tex);
    Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
    if (g_state.unk84 == 0xFFFF) {
        SetTriFlatVtxDesc();
        GXBegin(0x90, 0, 3);
        GXPosition3f32(g_pGDL->tris[index].v[0].x, g_pGDL->tris[index].v[0].y, g_pGDL->tris[index].v[0].z);
        GXColor4u8(g_pGDL->tris[index].v[0].color.r, g_pGDL->tris[index].v[0].color.g, g_pGDL->tris[index].v[0].color.b, g_pGDL->tris[index].v[0].color.a);
        GXPosition3f32(g_pGDL->tris[index].v[1].x, g_pGDL->tris[index].v[1].y, g_pGDL->tris[index].v[1].z);
        GXColor4u8(g_pGDL->tris[index].v[1].color.r, g_pGDL->tris[index].v[1].color.g, g_pGDL->tris[index].v[1].color.b, g_pGDL->tris[index].v[1].color.a);
        GXPosition3f32(g_pGDL->tris[index].v[2].x, g_pGDL->tris[index].v[2].y, g_pGDL->tris[index].v[2].z);
        GXColor4u8(g_pGDL->tris[index].v[2].color.r, g_pGDL->tris[index].v[2].color.g, g_pGDL->tris[index].v[2].color.b, g_pGDL->tris[index].v[2].color.a);
    } else {
        SetTriVtxDesc();
        GXBegin(0x90, 0, 3);
        GXPosition3f32(g_pGDL->tris[index].v[0].x, g_pGDL->tris[index].v[0].y, g_pGDL->tris[index].v[0].z);
        GXColor4u8(g_pGDL->tris[index].v[0].color.r, g_pGDL->tris[index].v[0].color.g, g_pGDL->tris[index].v[0].color.b, g_pGDL->tris[index].v[0].color.a);
        GXTexCoord2f32(g_pGDL->tris[index].v[0].s, g_pGDL->tris[index].v[0].t);
        GXPosition3f32(g_pGDL->tris[index].v[1].x, g_pGDL->tris[index].v[1].y, g_pGDL->tris[index].v[1].z);
        GXColor4u8(g_pGDL->tris[index].v[1].color.r, g_pGDL->tris[index].v[1].color.g, g_pGDL->tris[index].v[1].color.b, g_pGDL->tris[index].v[1].color.a);
        GXTexCoord2f32(g_pGDL->tris[index].v[1].s, g_pGDL->tris[index].v[1].t);
        GXPosition3f32(g_pGDL->tris[index].v[2].x, g_pGDL->tris[index].v[2].y, g_pGDL->tris[index].v[2].z);
        GXColor4u8(g_pGDL->tris[index].v[2].color.r, g_pGDL->tris[index].v[2].color.g, g_pGDL->tris[index].v[2].color.b, g_pGDL->tris[index].v[2].color.a);
        GXTexCoord2f32(g_pGDL->tris[index].v[2].s, g_pGDL->tris[index].v[2].t);
    }
}

void Render_DrawTriGroup(void) {
    u32 i;
    u32 j;
    u32 start;
    s32 blend;
    GDLGroupTriVtx *tri;
    GDLGroupQuadVtx *quad;
    GDLGroupLitTriVtx *lit;

    Render_StateZWriteEnable(0);
    for (i = 0; i < g_pGDL->numGroupTris; i++) {
        blend = GETP(g_pGDL->groupTris)[i].blendMode;
        start = GETP(g_pGDL->groupTris)[i].start;
        g_debugTriCount += GETP(g_pGDL->groupTris)[i].count;
        Render_StateAlphaBlendEnable(blend > 1);
        Render_StateBlendMode(blend);
        Render_SetTextureHandle(GETP(g_pGDL->groupTris)[i].tex);
        switch (GETP(g_pGDL->groupTris)[i].type) {
        case 0:
            SetTriVtxDesc();
            GXBegin(0x90, 0, GETP(g_pGDL->groupTris)[i].count * 3);
            for (j = 0; j < GETP(g_pGDL->groupTris)[i].count; j++) {
                tri = &GETP(g_pGDL->groupTriVtx)[start++];
                GXPosition3f32(tri->x0, tri->y0, tri->z0);
                GXColor4u8(tri->r, tri->g, tri->b, tri->a);
                GXTexCoord2f32(tri->s0, tri->t0);
                GXPosition3f32(tri->x1, tri->y1, tri->z1);
                GXColor4u8(tri->r, tri->g, tri->b, tri->a);
                GXTexCoord2f32(tri->s1, tri->t1);
                GXPosition3f32(tri->x2, tri->y2, tri->z2);
                GXColor4u8(tri->r, tri->g, tri->b, tri->a);
                GXTexCoord2f32(tri->s2, tri->t2);
            }
            break;
        case 4:
            SetTriVtxDesc();
            GXBegin(0x90, 0, GETP(g_pGDL->groupTris)[i].count * 6);
            for (j = 0; j < GETP(g_pGDL->groupTris)[i].count; j++) {
                quad = &GETP(g_pGDL->groupQuadVtx)[start++];
                GXPosition3f32(quad->x0, quad->y0, quad->z0);
                GXColor4u8(quad->r, quad->g, quad->b, quad->a);
                GXTexCoord2f32(0.0f, quad->s);
                GXPosition3f32(quad->x1, quad->y1, quad->z1);
                GXColor4u8(quad->r, quad->g, quad->b, quad->a);
                GXTexCoord2f32(1.0f, quad->s);
                GXPosition3f32(quad->x2, quad->y2, quad->z2);
                GXColor4u8(quad->r, quad->g, quad->b, quad->a);
                GXTexCoord2f32(0.0f, quad->t);
                GXPosition3f32(quad->x1, quad->y1, quad->z1);
                GXColor4u8(quad->r, quad->g, quad->b, quad->a);
                GXTexCoord2f32(1.0f, quad->s);
                GXPosition3f32(quad->x3, quad->y3, quad->z3);
                GXColor4u8(quad->r, quad->g, quad->b, quad->a);
                GXTexCoord2f32(1.0f, quad->t);
                GXPosition3f32(quad->x2, quad->y2, quad->z2);
                GXColor4u8(quad->r, quad->g, quad->b, quad->a);
                GXTexCoord2f32(0.0f, quad->t);
            }
            break;
        case 1:
            SetTriVtxDesc();
            GXBegin(0x90, 0, GETP(g_pGDL->groupTris)[i].count * 3);
            for (j = 0; j < GETP(g_pGDL->groupTris)[i].count; j++) {
                lit = &GETP(g_pGDL->groupLitTriVtx)[start++];
                GXPosition3f32(lit->x0, lit->y0, lit->z0);
                GXColor4u8(lit->c[0][0], lit->c[0][1], lit->c[0][2], lit->c[0][3]);
                GXTexCoord2f32(lit->s0, lit->t0);
                GXPosition3f32(lit->x1, lit->y1, lit->z1);
                GXColor4u8(lit->c[1][0], lit->c[1][1], lit->c[1][2], lit->c[1][3]);
                GXTexCoord2f32(lit->s1, lit->t1);
                GXPosition3f32(lit->x2, lit->y2, lit->z2);
                GXColor4u8(lit->c[2][0], lit->c[2][1], lit->c[2][2], lit->c[2][3]);
                GXTexCoord2f32(lit->s2, lit->t2);
            }
            break;
        }
    }
    Render_StateZWriteEnable(1);
    Render_StateAlphaBlendEnable(1);
}

void Render_DrawRectanglesWindow(void) {
    u32 i;
    u32 window = 0;
    u32 j;

    if (g_pGDL->numRects == 0) {
        return;
    }
    Set2DRectVtxDesc();
    for (i = 0; i < g_pGDL->numRects; i++) {
        if (g_CurrentViews != 1 && window != g_pGDL->rects[i].window) {
            window = g_pGDL->rects[i].window;
            Gfx_SetRenderWindow(window);
            Render_StateSetScissor();
            Gfx_SetOrthoMatrix();
        }
        Render_StateAlphaBlendEnable(g_pGDL->rects[i].blendMode > 1);
        Render_SetTextureHandle(g_pGDL->rects[i].tex);
        Render_StateBlendMode(g_pGDL->rects[i].blendMode);
        GXBegin(0x80, 0, 4);
        for (j = 0; j < 4; j++) {
            GXPosition2f32(g_pGDL->rects[i].v[j].x, g_pGDL->rects[i].v[j].y);
            GXColor4u8(g_pGDL->rects[i].v[j].color.r, g_pGDL->rects[i].v[j].color.g, g_pGDL->rects[i].v[j].color.b, g_pGDL->rects[i].v[j].color.a);
            GXTexCoord2f32(g_pGDL->rects[i].v[j].s, g_pGDL->rects[i].v[j].t);
        }
    }
    if (window != 0) {
        Gfx_SetRenderWindow(0);
        Render_StateSetScissor();
        Gfx_SetOrthoMatrix();
    }
    Render_StateAlphaBlendEnable(1);
}

void Render_DrawRectangles(void) {
    u32 i;
    u32 j;
    s32 blend;

    if (g_pGDL->numRects2D == 0) {
        return;
    }
    g_debugTriCount += g_pGDL->numRects2D * 2;
    Render_StateZWriteEnable(0);
    Set2DRectVtxDesc();
    for (i = 0; i < g_pGDL->numRects2D; i++) {
        blend = g_pGDL->rects2D[i].blendMode;
        Render_StateAlphaBlendEnable(blend > 1);
        Render_StateBlendMode(blend);
        Render_SetTextureHandle(g_pGDL->rects2D[i].tex);
        GXBegin(0x80, 0, 4);
        for (j = 0; j < 4; j++) {
            GXPosition2f32(g_pGDL->rects2D[i].v[j].x, g_pGDL->rects2D[i].v[j].y);
            GXColor4u8(g_pGDL->rects2D[i].v[j].color.r, g_pGDL->rects2D[i].v[j].color.g, g_pGDL->rects2D[i].v[j].color.b, g_pGDL->rects2D[i].v[j].color.a);
            GXTexCoord2f32(g_pGDL->rects2D[i].v[j].s, g_pGDL->rects2D[i].v[j].t);
        }
    }
    Render_StateAlphaBlendEnable(1);
}

extern s32 m_cullMode;
void Render_DrawRectangle(GDLRect *rect) {
    s32 cullMode;
    u32 i;

    cullMode = m_cullMode;
    g_debugTriCount += 2;
    Render_StateCullMode(0);
    Render_StateAlphaBlendEnable(rect->blendMode > 1);
    Render_SaveCamera();
    Gfx_SetOrthoMatrix();
    Render_StateZWriteEnable(0);
    Render_StateBlendMode(rect->blendMode);
    Render_SetTextureHandle(rect->tex);
    Set2DRectVtxDesc();
    GXBegin(0x80, 0, 4);
    for (i = 0; i < 4; i++) {
        GXPosition2f32(rect->v[i].x, rect->v[i].y);
        GXColor4u8(rect->v[i].color.r, rect->v[i].color.g, rect->v[i].color.b, rect->v[i].color.a);
        GXTexCoord2f32(rect->v[i].s, rect->v[i].t);
    }
    if (rect->blendMode != 0) {
        Render_StateAlphaBlendEnable(0);
    }
    Render_StateZWriteEnable(1);
    Render_StateCullMode(cullMode);
    Render_LoadCamera();
}

void Gfx_SetTextColor(u8 r, u8 g, u8 b, u8 a) {
    g_textcolor.r = r;
    g_textcolor.g = g;
    g_textcolor.b = b;
    g_textcolor.a = a;
}
