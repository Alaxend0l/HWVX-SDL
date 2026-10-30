#include "common.h"

extern FileTOCCacheEntry g_fileTOCCache[11];
extern s8 g_bSoundLoadingEnabled;
extern s32 g_fileID;
extern s32 g_openFileDataPackType;







void DataPack_EnableLoadingSounds(s8 enabled)
{
    g_bSoundLoadingEnabled = enabled;
}

void DataPack_Initialize(void)
{
    u32 i;

    for (i = 0; i < 11; i++) {
        g_fileTOCCache[i].name[0] = 0;
    }
}

void DataPack_CloseFiles(void)
{
    if (g_fileID > 0) {
        File_Close(g_fileID);
        g_fileID = -1;
        g_openFileDataPackType = 0xB;
    }
}



s32 DataPack_LoadFile(char *name, s32 arg1, u32 type, u32 id) {
    extern u32 g_buffer[];
    void DataPack_LoadDataPack(s32 file, s32 arg1);
    FileTOCCacheEntry *toc;
    char *tocName;
    u32 i;
    s8 found;
    char path[0x80];

    found = 0;
    strcpy(path, name);
    strcat(path, ".xgc");
    if ((g_fileID != -1 && g_fileTOCCache[type].name == NULL) || strcmp(g_fileTOCCache[type].name, name) != 0) {
        for (i = 0; i < 11; i++) {
            if (i != type && g_fileTOCCache[i].name != NULL && strcmp(g_fileTOCCache[i].name, name) == 0) {
                memcpy(&g_fileTOCCache[type], &g_fileTOCCache[i], sizeof(FileTOCCacheEntry));
                if (g_fileID && g_openFileDataPackType == i) {
                    g_openFileDataPackType = type;
                } else if (g_openFileDataPackType == type) {
                    File_Close(g_fileID);
                    g_fileID = -1;
                    g_openFileDataPackType = 0xB;
                }
                break;
            }
        }
    }
    if (g_fileID == -1 || (tocName = g_fileTOCCache[type].name) == NULL || strcmp(tocName, name) != 0 ||
        (g_openFileDataPackType != type && strcmp(tocName, g_fileTOCCache[g_openFileDataPackType].name) != 0)) {
        if (g_fileID != -1) {
            File_Close(g_fileID);
            g_openFileDataPackType = 0xB;
        }
        g_fileID = File_Open(0, path, 0, 0);
        if (g_fileID == -1) {
            if (strcmp(name, "mcp") != 0) {
                char msg[0x80];
                sprintf(msg, "Could not open the data pack: %s\n", path);
                Dprintf(msg);
            }
            return 0;
        }
        g_openFileDataPackType = type;
    }
    toc = &g_fileTOCCache[type];
    tocName = toc->name;
    if (tocName == NULL || strcmp(tocName, name) != 0) {
        File_Read(g_fileID, 0x800, g_buffer);
        if (g_buffer[0] != 0x020100A0 || g_buffer[1] != 0x040100AE) {
            File_Close(g_fileID);
            g_fileID = -1;
            g_openFileDataPackType = 0xB;
            return 0;
        }
        if (g_buffer[2] != 0xB7) {
            char msg[0x80];
            File_Close(g_fileID);
            g_fileID = -1;
            g_openFileDataPackType = 0xB;
            sprintf(msg, "The data pack is out of date: %s. Packfile:%d,  Code:%d\nYou need to repack the data using the latest BrainCore\n", path, g_buffer[2], 0xB7);
            Dprintf(msg);
            return 0;
        }
        g_fileTOCCache[type].count = g_buffer[3];
        memcpy(toc, &g_buffer[4], g_fileTOCCache[type].count * sizeof(FileTOCEntry));
        if (strlen(name) >= 0x4C) {
            strncpy(tocName, name, 0x4C);
            g_fileTOCCache[type].name[0x4B] = 0;
        } else {
            strcpy(tocName, name);
        }
    }
    for (i = 0; i < toc->count; i++) {
        if (type == toc->entries[i].type && id == toc->entries[i].id) {
            if (toc->entries[i].version != 0xB7) {
                char msg[0x80];
                sprintf(msg, "The data pack is out of date: %s. Packfile:%d,  Code:%d\nYou need to repack the data using the latest BrainCore\n", path, toc->entries[i].version, 0xB7);
                Dprintf(msg);
                File_Close(g_fileID);
                g_fileID = -1;
                g_openFileDataPackType = 0xB;
                return 0;
            }
            File_Seek(g_fileID, g_fileTOCCache[type].entries[i].offset + 0x10 + g_fileTOCCache[type].count * sizeof(FileTOCEntry), 0);
            found = 1;
            break;
        }
    }
    if (!found) {
        char msg[0x80];
        sprintf(msg, "Could not load the data pack with Pack ID: %d. Within the file: %s\n.Check the PackID and repack using the latest BrainCore.\n", id, path);
        Dprintf(msg);
        File_Close(g_fileID);
        g_fileID = -1;
        g_openFileDataPackType = 0xB;
        return 0;
    }
    DataPack_LoadDataPack(g_fileID, arg1);
    return 1;
}



void DataPack_LoadQuadPack(s32 file, QuadPack *quad) {
    s32 *reloc;
    u8 *data;
    u32 *pointers;
    u32 i;
    s32 *texRelocs = NULL;
    QuadPackHeader *header;
    u32 offset;
    u8 *base;
    u32 packID;
    u32 n;
    void *alloc;
    u32 idx;

    if (quad->enabled != 0) {
        if ((u32)quad->fileOffset == 0) {
            quad->enabled = 0;
            return;
        }
        File_Seek(file, quad->fileOffset, 0);
        alloc = Allocate(quad->size + 0x40, 0, "");
        quad->alloc = alloc;
        if (alloc != NULL) {
            data = (u8 *)PTR((((u32)HND(alloc) + 0x3F) & ~0x3F));
            File_Read(file, quad->size, data);
            quad->header = (QuadPackHeader *)data;
            offset = 0x54;
            header = quad->header;
            if (header->valid == 0) {
                quad->data = NULL;
                return;
            }
            n = header->numRelocs;
            if (n != 0) {
                header->relocs = (s32 *)(data + offset);
                offset += n * 8;
            }
            header = quad->header;
            n = header->numShaders;
            if (n != 0) {
                header->shaderList = (s32 *)(data + offset);
                offset += n * 8;
            }
            header = quad->header;
            n = header->numTexRelocs;
            if (n != 0) {
                texRelocs = (s32 *)(data + offset);
                offset += n * 8;
            }
            pointers = (u32 *)(data + offset);
            offset += header->numPointers * 4;
            offset += header->dataSize;
            quad->gxBase = (s32)HND((data + offset));
            reloc = texRelocs;
            for (i = 0; i < quad->header->numTexRelocs; i++) {
                base = (u8 *)PTR(quad->gxBase);
                packID = reloc[1] & 0xFFFF0000;
                if (packID != 0) {
                    packID >>= 16;
                    header = quad->header;
                    idx = header->refs[0].id != packID;
                    if (header->refs[idx].pack == NULL) {
                        header->refs[idx].pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&header->refs[idx], 8, packID, 0), 8, 1);
                    }
                    *(u32 *)(base + reloc[0]) = ((u32 *)header->refs[idx].pack->base)[reloc[1] & 0xFFFF];
                } else {
                    *(u32 *)(base + reloc[0]) += (u32)HND(base);
                }
                reloc += 2;
            }
            UpdatePointers(pointers, quad->header->numPointers, (u8 *)PTR(quad->gxBase));
            quad->data = SLOT_LOAD(void *, (u32 *)(quad->gxBase) + (0));
            quad->vertices = SLOT_LOAD(void *, (u32 *)(quad->gxBase) + (1));
        }
    }
}

void DataPack_RefreshQuadPackPtrs(DataPack *pack, void *quad)
{
    s32 *reloc;
    s32 *shader;
    u32 id;
    QuadPackHeader *header;
    u8 *base;
    u32 i;
    u32 idx;
    u32 texID;

    header = SLOT_LOAD(QuadPackHeader *, ((u8 *)quad + 0x28));
    base = SLOT_LOAD(u8 *, ((u8 *)quad + 0x2C));
    reloc = header != NULL ? header->relocs : NULL;
    shader = header != NULL ? header->shaderList : NULL;
    if (header != NULL)
    {
        for (i = 0; i < header->numRelocs; i++)
        {
            id = reloc[1];
            if (id & 0xFFFF0000)
            {
                u32 packID = id >> 16;
                idx = header->refs[0].id != packID;
                if (header->refs[idx].pack == NULL)
                {
                    header->refs[idx].pack =
                        FileStreaming_InitializeDataPack(
                            FileStreaming_LoadDataPack(&header->refs[idx], 8, packID, 0), 8, 1);
                }
                texID = header->refs[idx].pack->textureIDs[id & 0xFFFF];
            }
            else
            {
                texID = pack->textureIDs[id];
            }
            *(u32 *)(base + reloc[0]) = texID;
            reloc += 2;
        }
        for (i = 0; i < header->numShaders; i++)
        {
            ShaderHolder_AddGX(base + shader[0], pack->shaderIDs[shader[1]]);
            shader += 2;
        }
        if (SLOT_LOAD(void *, ((u8 *)quad + 0x14)) != NULL)
        {
            VertexColorSeqManager_AddVertices(SLOT_LOAD(void *, ((u8 *)quad + 0x14)));
        }
    }
}


void DataPack_RefreshTextures(DataPack *pack) {
    u8 *base;
    u32 texData;
    u32 id;
    u32 i;
    u32 idx;
    u32 texID;

    base = pack->base;
    Gfx_LoadPalettes_RAM(pack->paletteData, pack->palettes);
    texData = pack->textureData;
    for (i = 0; i < pack->numTextures; i++) {
        pack->textureIDs[i] = Gfx_LoadTexture_RAM(&texData);
        if (texData & 0x1F) {
            texData += 0x20 - (texData & 0x1F);
        }
    }
    for (i = 0; i < pack->numRelocs; i++) {
        id = pack->relocs[i * 2 + 1];
        if (id & 0xFFFF0000) {
            u32 packID = id >> 16;
            idx = pack->refs[0].id != packID;
            if (pack->refs[idx].pack == NULL) {
                pack->refs[idx].pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&pack->refs[idx], 8, packID, 0), 8, 1);
            }
            texID = pack->refs[idx].pack->textureIDs[id & 0xFFFF];
        } else {
            texID = pack->textureIDs[id];
        }
        *(u32 *)(base + pack->relocs[i * 2]) = texID;
    }
}

void DataPack_RefreshPtrs(DataPack *pack) {
    u8 *base;
    u32 i;
    u8 *trigger;
    u32 packID;
    u32 idx;
    u32 id;

    base = pack->base;
    if (base != NULL) {
        DataPack_RefreshTextures(pack);
        for (i = 0; i < pack->numSoundRelocs; i++) {
            id = pack->soundRelocs[i * 2 + 1];
            if (id & 0xFFFF0000) {
                packID = id >> 16;
                idx = pack->refs[0].id != packID;
                if (pack->refs[idx].pack == NULL) {
                    pack->refs[idx].pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&pack->refs[idx], 8, packID, 0), 8, 1);
                }
                id = pack->refs[idx].pack->soundIDs[id & 0xFFFF];
            } else {
                id = pack->soundIDs[id];
            }
            *(u32 *)(base + pack->soundRelocs[i * 2]) = id;
        }
        for (i = 0; i < pack->numShaders; i++) {
            pack->shaderIDs[i] = ShaderHolder_AddShader(PTR(pack->shaders[i].shader));
        }
        for (i = 0; i < pack->numGX; i++) {
            ShaderHolder_AddGX(base + pack->gxList[i * 2], pack->shaderIDs[pack->gxList[i * 2 + 1]]);
        }
        trigger = pack->triggers;
        for (i = 0; i < pack->numTriggers; i++) {
            Trigger_AddTrigger((s32 *)trigger);
            trigger += 0x2C;
        }
    }
}

void DataPack_ReleaseDataPack(DataPack *pack) {
    s8 releasedTriggers = 0;
    u32 *gx;
    u8 *trigger;
    u32 i;

    if (pack->soundIDs != NULL) {
        for (i = 0; i < pack->numSounds; i++) {
            if (pack->soundIDs[i] != 0) {
                DestroySFX(pack->soundIDs[i]);
            }
        }
        gx = pack->gxList;
        for (i = 0; i < pack->numGX; i++) {
            ShaderHolder_RemoveGX((s32)HND(pack->base) + *gx);
            gx += 2;
        }
        for (i = 0; i < pack->numShaders; i++) {
            ShaderHolder_RemoveShader(PTR(pack->shaders[i].shader));
        }
        for (i = 0; i < pack->numTextures; i++) {
            Gfx_UnLoadTexture(pack->textureIDs[i]);
        }
        trigger = pack->triggers;
        for (i = 0; i < pack->numTriggers; i++, trigger += 0x2C) {
            Trigger_ReleaseTrigger((s32 *)trigger);
            releasedTriggers = 1;
        }
        if (releasedTriggers) {
            Action_ReleaseAllActions();
        }
        Deallocate(pack->soundIDs);
        Deallocate(pack->soundData);
        pack->soundIDs = NULL;
        pack->soundData = NULL;
        memset(pack, 0, 0x94);
    }
}


void DataPack_ReleaseQuadPack(QuadPack *pack)
{
    s32 *list;
    s32 base;
    u32 i;

    if (pack->gxObject != NULL) {
        base = pack->gxBase;
        list = pack->gxObject->shaderList;
        for (i = 0; i < pack->gxObject->count; i++) {
            ShaderHolder_RemoveGX(base + *list);
            list += 2;
        }
        if (pack->vertices != NULL) {
            VertexColorSeqManager_RemoveVertices(pack->vertices);
        }
        Deallocate(pack->alloc);
        pack->alloc = NULL;
        pack->data = NULL;
        pack->vertices = NULL;
        pack->gxBase = 0;
        pack->gxObject = NULL;
        pack->flag34 = 0;
    }
}

u8 *DataPack_GetScript(void *pack, s32 id)
{
    u32 *entries;
    u32 i;
    u8 *table = SLOT_LOAD(u8 *, pack);

    if (table == NULL)
    {
        return NULL;
    }
    for (i = 0; i < M2C_FIELD(table, u32 *, 0); i++)
    {
        entries = SLOT_LOAD(u32 *, (u8 *)(table) + 4);
        if (id == entries[i * 2 + 1])
        {
            return (u8 *)PTR(entries[i * 2]);
        }
    }
    return NULL;
}
