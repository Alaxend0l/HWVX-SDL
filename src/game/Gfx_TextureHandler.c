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
extern u32 m_totalTextures;
extern s32 m_currentView;


typedef struct BTFHeader {
    u8 version;
    u8 unk1;
    s16 width;
    s16 height;
    s16 bpp;
    s16 numColors;
    s16 numMips;
    s32 unkC;
} BTFHeader;

s32 Gfx_TextureHandler_Create(void) {
    m_totalTextures = 0x1FF;
    m_texture = Allocate(0x7FC0, 0, "");
    memset(m_texture, 0, m_totalTextures * sizeof(Texture));
    return 1;
}

void Gfx_TextureHandler_Destroy(void) {
    u32 i;

    for (i = 1; i < m_totalTextures; i++) {
        Render_DestroyTextureSlot(i);
        if (GETP(m_texture[i].data) != NULL && m_texture[i].unk3A == 0) {
            Deallocate(GETP(m_texture[i].data));
        }
    }
    Deallocate(m_texture);
    m_texture = NULL;
}

void Gfx_TextureManager_Init(void) {
    u32 i;

    for (i = 1; i < m_totalTextures; i++) {
        Render_DestroyTextureSlot(i);
        if (GETP(m_texture[i].data) != NULL && m_texture[i].unk3A == 0) {
            Deallocate(GETP(m_texture[i].data));
        }
        memset(&m_texture[i], 0, 0x20);
        m_texture[i].refCount = 0;
        m_texture[i].unk30 = 0;
        m_texture[i].width = 0;
        SETP(m_texture[i].data, NULL);
    }
    Gfx_FontManager_Init();
}






u16 Gfx_LoadTexture(char *name) {
    u32 id;
    char buf[260];
    char *ext;
    u16 i;
    u16 j;
    s32 ok;

    if (strcmp(name, "_NONE_") == 0) {
        return 0xFFFF;
    }
    strcpy(buf, name);
    if ((ext = strstr(buf, ".tga")) != NULL) {
        *ext = 0;
    } else if ((ext = strstr(buf, ".TGA")) != NULL) {
        *ext = 0;
    } else if ((ext = strstr(buf, ".btf")) != NULL) {
        *ext = 0;
    }
    if (strlen(buf) > 34) {
        buf[34] = 0;
    }

    for (j = 1; j < m_totalTextures; j++) {
        if (stricmp(buf, (char *)m_texture[j].unk0) == 0 && m_currentView == m_texture[j].view) {
            m_texture[j].refCount++;
            return j;
        }
    }

    for (i = 1; i < m_totalTextures; i++) {
        if (GETP(m_texture[i].data) == NULL) {
            break;
        }
    }
    if (i >= m_totalTextures) {
        i = m_totalTextures - 1;
    }

    id = i;
    m_texture[i].wrapMode = 0;
    ok = LoadBTF(buf, id);
    if (!ok) {
        ok = LoadTGA(buf, id);
    }
    if (!ok) {
        return 0xFFFF;
    }
    strncpy((char *)m_texture[i].unk0, buf, 0x24);
    m_texture[i].refCount++;
    m_texture[i].view = (s8)m_currentView;
    m_texture[i].unk2C = 0.25f;
    m_texture[i].unk3A = 0;
    return i;
}



s32 LoadBTF(const char *name, u32 id) {
    BTFHeader header;
    char path[64];
    u8 palette[0x400];
    s32 size;
    s32 handle;
    s32 n;
    s32 i;
    s16 width;
    s16 levels;
    s16 height;
    s16 mips;
    s16 idx;
    u8 *dst;
    u8 *src;
    u8 *buf;
    u8 *p;

    sprintf(path, "%s.btf", name);
    handle = File_Open(0, path, 0, 0);
    if (handle < 0) {
        return 0;
    }
    File_Read(handle, 1, &header);
    File_Seek(handle, -1, 1);
    if (header.version == 1) {
        File_Read(handle, 12, &header);
        header.unk1 = 0;
        header.unkC = 0;
    } else if (header.version == 2) {
        File_Read(handle, 12, &header);
        header.unkC = 0;
    } else if (header.version == 3) {
        File_Read(handle, 16, &header);
    }
    width = header.width;
    mips = header.numMips;
    if (width > 0x400 || header.height > 0x400) {
        return 0;
    }
    if (width == 0 || header.height == 0) {
        return 0;
    }
    size = 0;
    levels = 1;
    m_texture[id].width = width;
    height = header.height;
    m_texture[id].unk30 = height;
    while (height != 1 && width != 1) {
        if (mips-- == 0) {
            break;
        }
        size += width * height;
        height >>= 1;
        if (height == 0) {
            height = 1;
        }
        width >>= 1;
        if (width == 0) {
            width = 1;
        }
        levels++;
    }
    if (levels == 1) {
        size = width * height;
    }
    buf = NULL;
    m_texture[id].height = levels;
    switch (header.bpp) {
    case 12:
        size /= 2;
        p = Allocate(size, 0, "");
        SETP(m_texture[id].data, p);
        buf = Allocate(size, 0, "");
        m_texture[id].unk38 = 4;
        File_Read(handle, size, p);
        break;
    case 32:
        n = size * 4;
        dst = Allocate(n, 0, "");
        SETP(m_texture[id].data, dst);
        src = buf = Allocate(n, 0, "");
        m_texture[id].unk38 = 1;
        File_Read(handle, n, src);
        for (i = 0; i < size; i++) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            src += 4;
            dst += 4;
        }
        break;
    case 24:
        n = size * 3;
        dst = Allocate(n, 0, "");
        SETP(m_texture[id].data, dst);
        src = buf = Allocate(n, 0, "");
        m_texture[id].unk38 = 0;
        File_Read(handle, n, src);
        for (i = 0; i < size; i++) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            src += 3;
            dst += 3;
        }
        break;
    case 9:
        dst = Allocate(size * 4, 0, "");
        SETP(m_texture[id].data, dst);
        src = buf = Allocate(size, 0, "");
        m_texture[id].unk38 = 1;
        memset(palette, 0, 0x400);
        if (header.numColors > 0) {
            File_Read(handle, header.numColors * 4, palette);
        } else {
            for (n = 0; n < 0xFF; n++) {
                p = palette;
                p += n;
                p[2] = n;
                p[1] = n;
                p[0] = n;
            }
        }
        File_Read(handle, size, src);
        for (i = 0; i < size; i++) {
            idx = (u8)*src << 2;
            dst[0] = palette[idx + 2];
            dst[1] = palette[idx + 1];
            dst[2] = palette[idx];
            dst[3] = palette[idx + 3];
            src++;
            dst += 4;
        }
        break;
    case 8:
        dst = Allocate(size * 3, 0, "");
        SETP(m_texture[id].data, dst);
        src = buf = Allocate(size, 0, "");
        m_texture[id].unk38 = 0;
        memset(palette, 0, 0x300);
        if (header.numColors > 0) {
            File_Read(handle, header.numColors * 3, palette);
        } else {
            for (n = 0; n < 0xFF; n++) {
                p = palette;
                p += n;
                p[2] = n;
                p[1] = n;
                p[0] = n;
            }
        }
        File_Read(handle, size, src);
        for (i = 0; i < size; i++) {
            idx = *src * 3;
            dst[0] = palette[idx + 2];
            dst[1] = palette[idx + 1];
            dst[2] = palette[idx];
            src++;
            dst += 3;
        }
        break;
    }
    Deallocate(buf);
    File_Close(handle);
    Render_CreateTextureSlot(id);
    return 1;
}



s32 LoadTGA(const char *name, u32 id) {
    u8 header[18];
    char path[56];
    s32 size;
    s32 handle;
    u8 *dst;
    u8 *src;
    u8 *buf;
    s16 width;
    s16 height;
    s32 bpp;
    s32 i;

    sprintf(path, "%s.tga", name);
    handle = File_Open(0, path, 0, 0);
    if (handle == -1) {
        return 0;
    }
    File_Read(handle, 18, header);
    m_texture[id].width = height = header[14] + header[15] * 256;
    m_texture[id].unk30 = width = header[12] + header[13] * 256;
    if (height > 0x400 || width > 0x400) {
        return 0;
    }
    if (height == 0 || width == 0) {
        return 0;
    }
    i = width * height;
    m_texture[id].height = 0;
    bpp = 3;
    if (header[16] == 32) {
        bpp = 4;
    }
    dst = Allocate(i * bpp, 0, "");
    SETP(m_texture[id].data, dst);
    size = (header[16] >> 3) * (m_texture[id].unk30 * m_texture[id].width);
    src = buf = Allocate(size, 0, "");
    switch (header[16]) {
    case 8:
        break;
    case 32:
        m_texture[id].unk38 = 1;
        File_Read(handle, size, src);
        for (i = 0; i < size; i += 4) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            src += 4;
            dst += 4;
        }
        break;
    case 24:
        m_texture[id].unk38 = 0;
        File_Read(handle, size, src);
        for (i = 0; i < size; i += 3) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            src += 3;
            dst += 3;
        }
        break;
    case 16:
        m_texture[id].unk38 = 0;
        File_Read(handle, size, src);
        for (i = 0; i < size; i += 2) {
            dst[0] = (src[0] & 0x7C00) >> 7;
            dst[1] = (src[0] & 0x3E0) >> 2;
            dst[2] = (src[0] & 0x1F) << 3;
            src += 2;
            dst += 3;
        }
        break;
    }
    File_Close(handle);
    Deallocate(buf);
    Render_CreateTextureSlot(id);
    return 1;
}


u16 Gfx_LoadTexture_RAM(u8 **data) {
    extern s32 m_currentView;
    char buf[32];
    u16 i;

    for (i = 1; i < m_totalTextures; i++) {
        if (GETP(m_texture[i].data) == NULL) {
            break;
        }
    }
    if (i >= m_totalTextures) {
        i = m_totalTextures - 1;
    }
    *data = LoadBTF_RAM(*data, i);
    sprintf(buf, "/pack:%d", i);
    memcpy(&m_texture[i], buf, 0x20);
    m_texture[i].refCount++;
    m_texture[i].view = (s8)m_currentView;
    m_texture[i].unk2C = 0.25f;
    m_texture[i].unk3A = 1;
    m_texture[i].wrapMode = 0;
    return i;
}

/* A texture's header in a data pack (LoadBTF_RAM); its u16s are little-endian. */
typedef struct BTFPackHeader {
    u16 unk0;                   /* 0x00 -> Texture.unk38 */
    u16 unk2;                   /* 0x02 -> Texture.unk30 */
    u16 width;                  /* 0x04 */
    u16 height;                 /* 0x06 */
    PTR32(void) data;           /* 0x08 */
    u32 unkC;                   /* 0x0C -> Texture.unk28 */
} BTFPackHeader;

/* Fill texture slot `id` from the BTF at data; returns the end of its header. */
u8 *LoadBTF_RAM(u8 *data, u32 id) {
    BTFPackHeader *hdr = (BTFPackHeader *)data;
    Texture *tex;

    tex = &((Texture *)m_texture)[id];
    tex->width = ((hdr->width & 0xFF00) / 256) | (hdr->width * 256);
    tex = &((Texture *)m_texture)[id];
    __sthbrx(hdr->unk2, &tex->unk30, 0);
    tex = &((Texture *)m_texture)[id];
    __sthbrx(hdr->height, &tex->height, 0);
    tex = &((Texture *)m_texture)[id];
    SETP(tex->data, GETP(hdr->data));
    tex = &((Texture *)m_texture)[id];
    tex->unk28 = hdr->unkC;
    tex = &((Texture *)m_texture)[id];
    __sthbrx(hdr->unk0, &tex->unk38, 0);
    Render_CreateTextureSlot(id);
    return (u8 *)(hdr + 1);
}


void Gfx_SetTextureWrapMode(u16 id, u16 mode) {
    Texture *tex = &m_texture[id];

    tex->wrapMode = mode;
    Render_CreateTextureSlot(id);
}

void Gfx_LoadPalettes_RAM(void *data, u32 count) {

}

void Gfx_UnLoadTexture(u16 id) {
    if (id < m_totalTextures && m_texture != NULL) {
        if (--m_texture[id].refCount == 0) {
            Render_DestroyTextureSlot(id);
            if (m_texture[id].unk3A == 0) {
                Deallocate(GETP(m_texture[id].data));
            }
            memset(&m_texture[id], 0, 0x20);
            m_texture[id].refCount = 0;
            m_texture[id].unk30 = 0;
            m_texture[id].width = 0;
            SETP(m_texture[id].data, NULL);
        }
    }
}

