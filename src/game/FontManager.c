#include "common.h"


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

typedef struct GDLRectVtx {
    f32 x;
    f32 y;
    f32 z;
    u8 unkC[0x10 - 0xC];
    f32 s;
    f32 t;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    u8 unk1C[0x20 - 0x1C];
} GDLRectVtx;

typedef struct GDLRect {
    GDLRectVtx v[4];
    u16 tex;
    u8 blendMode;
    u8 window;
} GDLRect;

typedef struct GfxState {
    u8 unk0[0x8C];
    u16 debugFont;
    u8 unk8E[0xA0 - 0x8E];
} GfxState;

/* The letters a font texture holds, in order, per language (E/F/G) and case mode. */
char g_lettersE[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'";
char g_lettersNoLowerCaseE[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?;:+-*/=%\"()&#'";
char g_lettersNoNumE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'";
char g_lettersNoLowerCaseNoNumsE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?:-'/%\";+=()&#";
char g_lettersF[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'      \xC9\xE9\xC8\xE8\xCA\xEA\xD4\xF4\xB0";
char g_lettersNoLowerCaseF[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.'!?;:+-*/=%\xC9\xC8,\"()&#'";
char g_lettersNoNumF[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'";
char g_lettersNoLowerCaseNoNumsF[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?:-'/%\"\xC9\xCA\xC7\xC8\xD4\xC2\xC0;+=()&#";
char g_lettersG[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'      \xC4\xE4\xD6\xF6\xDC\xFC\xDF";
char g_lettersNoLowerCaseG[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.'!?;:+-*/=%\xC4\xD6\xDC\xDF,\"()&#'";
char g_lettersNoNumG[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'";
char g_lettersNoLowerCaseNoNumsG[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?:-'/%\"\xC4\xD6\xDC\xDF;+=()&#";
u8 g_letterWidth[100] = {
    0x12, 0x0D, 0x12, 0x11, 0x12, 0x12, 0x11, 0x11, 0x11, 0x11, 0x13, 0x12, 0x14, 0x13, 0x10, 0x11,
    0x13, 0x14, 0x08, 0x10, 0x13, 0x0F, 0x18, 0x14, 0x13, 0x13, 0x13, 0x13, 0x11, 0x12, 0x13, 0x12,
    0x17, 0x13, 0x13, 0x13, 0x06, 0x08, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0C, 0x0F, 0x10,
    0x08, 0x0A, 0x0F, 0x08, 0x16, 0x0F, 0x10, 0x10, 0x0F, 0x0D, 0x0E, 0x0D, 0x0F, 0x0F, 0x14, 0x0E,
    0x0F, 0x0F, 0x08, 0x08, 0x10, 0x0F, 0x0F, 0x14, 0x12, 0x18, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10,
};

FontManager g_fontManager;
extern GfxState g_state;

typedef struct Texture {
    u8 unk0[0x24];
    PTR32(void) data;
    u32 unk28;
    f32 unk2C;
    u16 unk30;
    u16 width;
    u16 height;
    s16 refCount;
    u16 unk38;
    u16 unk3A;
    u16 view;
    u16 wrapMode;
} Texture;

extern Texture *m_texture;


void FontManager_Create(void) {
    M2C_UNK fn_800957D4();
    fn_800957D4();
}

void FontManager_Destroy(void) {
}

void Gfx_FontManager_Init(void) {
    u32 i;

    g_state.debugFont = 0;
    g_fontManager.numFonts = 0;
    for (i = 0; i < 16; i++) {
        g_fontManager.fonts[i].inUse = 0;
    }
}

void Gfx_SetDebugFont(s16 font) {
    g_state.debugFont = font;
}

s32 Gfx_FontManager_ReleaseFont(s32 id) {
    Font *font = &g_fontManager.fonts[id];
    s32 ret = 0;

    if (!font->inUse) {
        return 1;
    }
    if (--font->refCount == 0) {
        font->inUse = 0;
        g_fontManager.numFonts--;
        ret = 1;
    }
    return ret;
}

u32 Gfx_FontManager_AddFont(u16 texture, u32 caseMode, f32 width, f32 height, f32 spacing) {
    extern Texture *m_texture;
    extern s32 g_gameLanguage;
    u32 slot = -1;
    u32 isNew = 1;
    Font *font;
    u32 i;
    f32 texSize;
    f32 dv;
    f32 u;
    f32 v;
    f32 du;

    for (i = 0; i < 16; i++) {
        if (g_fontManager.fonts[i].inUse && g_fontManager.fonts[i].texture == texture) {
            if (g_fontManager.fonts[i].height == height && g_fontManager.fonts[i].width == width &&
                g_fontManager.fonts[i].caseMode == caseMode) {
                g_fontManager.fonts[i].refCount++;
                return i;
            }
            slot = i;
            isNew = 0;
            g_fontManager.fonts[i].refCount++;
        } else if (isNew && !g_fontManager.fonts[i].inUse && slot == -1) {
            slot = i;
        }
    }
    texSize = m_texture[texture].width;
    font = &g_fontManager.fonts[slot];
    font->texture = texture;
    font->height = height;
    font->width = width;
    font->caseMode = caseMode;
    font->inUse = 1;
    if (isNew) {
        font->refCount = 1;
    }
    switch (font->caseMode) {
    case 1:
        switch (g_gameLanguage) {
        default:
        case 0:
        case 2:
            SETP(font->letters, g_lettersNoLowerCaseE);
            break;
        case 1:
            SETP(font->letters, g_lettersNoLowerCaseF);
            break;
        case 3:
            SETP(font->letters, g_lettersNoLowerCaseG);
            break;
        }
        break;
    case 2:
        switch (g_gameLanguage) {
        default:
        case 0:
        case 2:
            SETP(font->letters, g_lettersNoNumE);
            break;
        case 1:
            SETP(font->letters, g_lettersNoNumF);
            break;
        case 3:
            SETP(font->letters, g_lettersNoNumG);
            break;
        }
        break;
    case 3:
        switch (g_gameLanguage) {
        default:
        case 0:
        case 2:
            SETP(font->letters, g_lettersNoLowerCaseNoNumsE);
            break;
        case 1:
            SETP(font->letters, g_lettersNoLowerCaseNoNumsF);
            break;
        case 3:
            SETP(font->letters, g_lettersNoLowerCaseNoNumsG);
            break;
        }
        break;
    default:
        switch (g_gameLanguage) {
        default:
        case 0:
        case 2:
            SETP(font->letters, g_lettersE);
            break;
        case 1:
            SETP(font->letters, g_lettersF);
            break;
        case 3:
            SETP(font->letters, g_lettersG);
            break;
        }
        break;
    }
    font->unkF = strlen(GETP(font->letters));
    for (i = 0; i < 128; i++) {
        font->chars[i].spacingX = 0;
    }
    du = width / texSize;
    v = 1.0f;
    u = 0.0f;
    dv = height / texSize;
    font->chars[' '].spacingX = spacing;
    for (i = 0; i < font->unkF; i++) {
        font->chars[(u8)GETP(font->letters)[i]].spacingX = spacing;
        font->chars[(u8)GETP(font->letters)[i]].unk0 = u;
        font->chars[(u8)GETP(font->letters)[i]].unk4 = v;
        font->chars[(u8)GETP(font->letters)[i]].unk8 = u + spacing / texSize;
        font->chars[(u8)GETP(font->letters)[i]].unkC = v - dv;
        u += du;
        if (u + du > 1.0f) {
            u = 0.0f;
            v -= dv;
        }
    }
    if (isNew) {
        g_fontManager.numFonts++;
    }
    return slot;
}

void Gfx_FontManager_SetCharacterSpacing(s32 id, u8 c, f32 x, f32 y) {
    Font *font = &g_fontManager.fonts[id];
    f32 width = m_texture[font->texture].width;

    font->chars[c].spacingX = x;
    font->chars[c].spacingY = y;
    x /= width;
    font->chars[c].unk8 = font->chars[c].unk0 + x;
}

u32 Gfx_FontManager_Print(char *str, u32 id, u8 blendMode, f32 x, f32 y, f32 spacing) {
    extern GXColor g_textcolor;
    GDLRect *Gfx_Add2DRectangle(void);
    u32 i;
    Font *font = &g_fontManager.fonts[id];
    GXColor color = g_textcolor;
    u8 c;
    GDLRect *rect;

    if (id == 0 && g_fontManager.numFonts == 0) {
        return 0;
    }
    for (i = 0; i < strlen(str); i++) {
        c = str[i];
        if (font->caseMode == 1 || font->caseMode == 3) {
            if (c >= 'a' && c <= 'z') {
                c -= 0x20;
            }
        }
        if (c != ' ') {
            f32 x1 = x + (u8)font->chars[c].spacingX;
            f32 y1 = y + font->height;
            rect = Gfx_Add2DRectangle();
            rect->blendMode = blendMode;
            rect->tex = font->texture;
            *(GXColor *)&rect->v[0].r = color;
            *(GXColor *)&rect->v[1].r = color;
            *(GXColor *)&rect->v[2].r = color;
            *(GXColor *)&rect->v[3].r = color;
            rect->v[0].s = font->chars[c].unk0;
            rect->v[0].t = font->chars[c].unk4;
            rect->v[1].s = font->chars[c].unk0;
            rect->v[1].t = font->chars[c].unkC;
            rect->v[2].s = font->chars[c].unk8;
            rect->v[2].t = font->chars[c].unkC;
            rect->v[3].s = font->chars[c].unk8;
            rect->v[3].t = font->chars[c].unk4;
            rect->v[0].x = x;
            rect->v[0].y = y;
            rect->v[1].x = x;
            rect->v[1].y = y1;
            rect->v[2].x = x1;
            rect->v[2].y = y1;
            rect->v[3].x = x1;
            rect->v[3].y = y;
        }
        x += (u8)font->chars[c].spacingX + spacing;
    }
    return g_fontManager.fonts[id].height;
}

void Gfx_FontManager_PrintParagraph(char *str, u32 maxChars, s32 id, u8 blendMode, s32 align, f32 *scale, f32 x, f32 y, f32 spacing, f32 width, f32 height, f32 lineSpacing)
{
  extern FontManager g_fontManager;
  Font *font = &g_fontManager.fonts[id];
  f32 center;
  f32 right;
  f32 lineY = 0.0f;
  u32 pos = 0;
  s8 more;
  u32 len = strlen(str);
  f32 sx = (scale) ? (scale[0]) : (1.0f);
  f32 sy = (scale) ? (scale[1]) : (1.0f);
  char buf[256];
  if (maxChars)
  {
    right = x + width;
    center = x + (0.5f * width);
    while (more)
    {
      f32 lastW = 0.0f;
      f32 lineW = lastW;
      u32 lastSpace = -1;
      u32 n = 0;
      s8 c;
      while ((str[pos] == ' ') && (pos < len))
      {
        pos++;
      }

      for (;;)
      {
        c = str[pos + n];
        if ((font->caseMode == 1) || (font->caseMode == 3))
        {
          if ((c >= 'a') && (c <= 'z'))
          {
            c -= 0x20;
          }
        }
        if (c == '|')
        {
          pos++;
          break;
        }
        if (c == ' ')
        {
          lastSpace = n;
          lastW = lineW;
        }
        if (lineW > width)
        {
          if (lastSpace < ((u32) (-1)))
          {
            lineW = lastW;
            n = lastSpace;
          }
          break;
        }
        if ((pos + n) >= len)
        {
          break;
        }
        buf[n++] = c;
        lineW += sx * (((u8) font->chars[c].spacingX) + spacing);
      }

      while ((n != 0) && (buf[n - 1] == ' '))
      {
        n--;
        lineW -= sx * (((u8) font->chars[buf[n]].spacingX) + spacing);
      }

      if ((pos + n) > maxChars)
      {
        more = 0;
        while (((pos + n) > maxChars) && (n != 0))
        {
          n--;
          c = buf[n];
          buf[n] = 0;
          lineW -= sx * (((u8) font->chars[c].spacingX) + spacing);
        }

      }
      lineW -= (-spacing) * sx;
      more = 1;
      buf[n] = 0;
      pos += n;
      switch (align)
      {
        case 1:
          x = center - (0.5f * lineW);
          break;

        case 2:
          x = right - lineW;
          break;

      }

      if (scale)
      {
        Gfx_FontManager_PrintScale(buf, id, scale, blendMode, x, y, spacing);
      }
      else
      {
        Gfx_FontManager_Print(buf, id, blendMode, x, y, spacing);
      }
      lineY += sy * (font->height + lineSpacing);
      y += sy * (font->height + lineSpacing);
      if (((lineY + font->height) + lineSpacing) > height)
      {
        more = 0;
      }
    }

  }
}

void Gfx_FontManager_PrintScale(char *str, s32 id, f32 *scale, u8 blendMode, f32 x, f32 y, f32 spacing) {
    extern GXColor g_textcolor;
    GDLRect *Gfx_Add2DRectangle(void);
    u32 i;
    Font *font = &g_fontManager.fonts[id];
    GXColor color = g_textcolor;
    u8 c;
    GDLRect *rect;

    for (i = 0; i < strlen(str); i++) {
        c = str[i];
        if (font->caseMode == 1 || font->caseMode == 3) {
            if (c >= 'a' && c <= 'z') {
                c -= 0x20;
            }
        }
        if (c != ' ') {
            f32 x1 = (u8)font->chars[c].spacingX * scale[0] + x;
            f32 y1 = font->height * scale[1] + y;
            rect = Gfx_Add2DRectangle();
            rect->blendMode = blendMode;
            rect->tex = font->texture;
            *(GXColor *)&rect->v[0].r = color;
            *(GXColor *)&rect->v[1].r = color;
            *(GXColor *)&rect->v[2].r = color;
            *(GXColor *)&rect->v[3].r = color;
            rect->v[0].s = font->chars[c].unk0;
            rect->v[0].t = font->chars[c].unk4;
            rect->v[1].s = font->chars[c].unk0;
            rect->v[1].t = font->chars[c].unkC;
            rect->v[2].s = font->chars[c].unk8;
            rect->v[2].t = font->chars[c].unkC;
            rect->v[3].s = font->chars[c].unk8;
            rect->v[3].t = font->chars[c].unk4;
            rect->v[0].x = x;
            rect->v[0].y = y;
            rect->v[1].x = x;
            rect->v[1].y = y1;
            rect->v[2].x = x1;
            rect->v[2].y = y1;
            rect->v[3].x = x1;
            rect->v[3].y = y;
            rect->v[0].z = 0.0f;
            rect->v[1].z = 0.0f;
            rect->v[2].z = 0.0f;
            rect->v[3].z = 0.0f;
        }
        x += scale[0] * ((u8)font->chars[c].spacingX + spacing);
    }
}

void Gfx_FontManager_GetPrintRect(char *str, s32 id, f32 *width, f32 *height, f32 spacing) {
    Font *font = &g_fontManager.fonts[id];
    u32 w = 0;
    u32 i = 0;
    u32 len = strlen(str);
    u8 c;

    while (i < len) {
        c = str[i++];
        if (font->caseMode == 1 || font->caseMode == 3) {
            if (c >= 'a' && c <= 'z') {
                c -= 0x20;
            }
        }
        w += (u8)font->chars[c].spacingX + spacing;
    }
    *width = (u32)(w - spacing);
    *height = font->height;
}

