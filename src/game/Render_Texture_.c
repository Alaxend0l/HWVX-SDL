#include "common.h"

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

GXTexObj m_gxTexObjArray[511];
u32 m_curTexture;


void Render_CreateTextureSlot(u32 id) {
    GXTexObj *obj;
    s32 wrap;
    s32 format;
    void *data;
    Texture *texs;
    Texture *tex;

    tex = &m_texture[id];
    wrap = tex->wrapMode == 0;
    obj = &m_gxTexObjArray[id];
    if (GXGetTexObjData(obj) != NULL) {
        texs = m_texture;
        switch (texs[id].unk38) {
        case 0:
        case 2:
            break;
        case 1:
            format = 6;
            break;
        case 4:
            format = 0xE;
            break;
        }
        GXInitTexObj(obj, GXGetTexObjData(obj), texs[id].unk30, texs[id].width, format, wrap, wrap, texs[id].height > 1 ? 1 : 0);
        return;
    }
    tex = &m_texture[id];
    switch (tex->unk38) {
    case 0:
    case 2:
        break;
    case 1:
        DCFlushRange(GETP(tex->data), tex->unk30 * (tex->width * 4));
        format = 6;
        tex = &m_texture[id];
        data = GETP(tex->data);
        tex->height = 0;
        break;
    case 4:
        DCFlushRange(GETP(tex->data), tex->unk30 * tex->width);
        texs = m_texture;
        format = 0xE;
        data = GETP(texs[id].data);
        break;
    }
    tex = &m_texture[id];
    if (tex->unk30 > 0x200 || tex->width > 0x200) {
        return;
    }
    GXInitTexObj(obj, data, tex->unk30, tex->width, format, wrap, wrap, 1 - tex->height < 0);
    texs = m_texture;
    if (texs[id].height > 1) {
        GXInitTexObjLOD(obj, 5, 1, 0.0f, (s32)(texs[id].height - 1) / 1.15f, -1.0f, 1, 1, 0);
    }
}

void Render_DestroyTextureSlot(u32 id) {
    GXInitTexObj(&m_gxTexObjArray[id], NULL, 0x20, 0x20, 6, 0, 0, 0);
}

void Render_SetTextureHandle(u16 id) {
    if (id == 0xFFFF) {
        GXSetNumTexGens(0);
        GXSetTevOp(0, 4);
        GXSetTevOrder(0, 0xFF, 0xFF, 4);
        m_curTexture = 0xFFFF;
    } else if (m_curTexture != id) {
        GXSetNumTexGens(1);
        GXSetTevOp(0, 0);
        GXSetTevOrder(0, 0, 0, 4);
        GXLoadTexObj(&m_gxTexObjArray[id], 0);
        m_curTexture = id;
    }
}

