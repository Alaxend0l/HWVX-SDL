#include "common.h"
#include "Voice.h"
#include "Port_Swap.h"

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
        PortSwap_TOC(g_buffer, 0x800 / 4);  /* port: the file is big-endian */
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
    g_portSwapPackType = type;  /* port: picks the swap walker */
    {
        static char label[0x60];  /* "name#id" for the swap checker's reports */
        snprintf(label, sizeof(label), "%s#%u", name, id);
        g_portSwapPackName = label;
    }
    Port_Log("data pack %s: type %u id %u", name, type, id);
    DataPack_LoadDataPack(g_fileID, PTR(arg1));
    g_portSwapPackType = -1;
    return 1;
}

typedef struct DataPackFileHeader {
    u32 dataSize;
    u32 soundDataSize;
    u32 numSoundPointers;
    u32 numPointers;
    u32 numRelocs;
    u32 numTextures;
    u32 textureDataOffset;
    u32 numSoundRelocs;
    u32 numSounds;
    u32 sfxOffset;
    u32 palettes;
    u32 paletteOffset;
    u32 numShaders;
    u32 numGX;
    u32 numTexRelocs;
    u32 shaderOffset;
    u32 numTriggers;
    u32 triggerOffset;
    u32 songOffset;
    u32 songBankOffset;
    DataPackRef refs[2];
} DataPackFileHeader;

void DataPack_LoadDataPack(s32 file, DataPack *pack) {
    extern s8 g_bSoundLoadingEnabled;
    static DataPackFileHeader header;
    char msg[0x68];
    u32 relocSize;
    s8 freeData;
    u32 soundIDsSize;
    u8 *alloc;
    u32 i;
    u32 readSize;
    u32 size;
    u32 pad;
    u8 *data;
    u8 *sfx;
    u32 offset;
    u32 *reloc;
    u32 *pointers;
    u32 packID;
    u32 idx;

    memset(pack, 0, sizeof(DataPack));
    File_Read(file, sizeof(DataPackFileHeader), &header);
    PortSwap_PackHeader((u32 *)&header, 20, 2, sizeof(DataPackRef));  /* port */
    pack->numRelocs = header.numRelocs;
    pack->numSoundRelocs = header.numSoundRelocs;
    pack->numTextures = header.numTextures;
    pack->numSounds = header.numSounds;
    SETP(pack->palettes, (void *)PTR(header.palettes));
    pack->numShaders = header.numShaders;
    pack->numGX = header.numGX;
    pack->numTriggers = header.numTriggers;
    pack->songLoaded = 0;
    pack->dataSize = header.dataSize;
    memcpy(pack->refs, header.refs, sizeof(header.refs));
    size = header.numRelocs * 8 + (pack->numSounds + pack->numTextures) * 2 + pack->numShaders * 2 + header.numGX * 8 + header.numSoundRelocs * 8 + header.dataSize + 0x60;
    relocSize = header.numTexRelocs * 8 + header.numPointers * 4;
    size += relocSize;
    alloc = Allocate(size, 0, "");
    if (alloc == NULL) {
        return;
    }
    SETP(pack->soundIDs, (u16 *)alloc);
    soundIDsSize = pack->numSounds * 2;
    if (header.soundDataSize != 0) {
        freeData = 0;
        pad = (header.numSoundPointers * 4) & 0x1F;
        readSize = header.numSoundPointers * 4;
        readSize += pad == 0 ? 0 : 0x20 - pad;
        readSize += header.soundDataSize;
        if (readSize + 0x60 <= size - soundIDsSize) {
            SETP(pack->soundData, alloc + soundIDsSize);
        } else {
            SETP(pack->soundData, Allocate(readSize + 0x60, 0, ""));
            freeData = 1;
        }
        sfx = (u8 *)PTR((((u32)HND(GETP(pack->soundData)) + 0x3F) & ~0x3F));
        File_Read(file, readSize, sfx);
        data = sfx;
        sfx += header.numSoundPointers * 4;
        SETP(pack->soundBase, (u8 *)PTR((((u32)HND(sfx) + 0x1F) & ~0x1F)));
        /* port: the pointer table and the slots it lists are big-endian; the
         * rest of the sound data stays so (the mixer reads it that way) */
        Port_SwapArray32(data, header.numSoundPointers);
        for (i = 0; i < header.numSoundPointers; i++) {
            u32 *slot = (u32 *)(GETP(pack->soundBase) + ((u32 *)data)[i]);
            *slot = BE32(*slot);
        }
        UpdatePointers((u32 *)data, header.numSoundPointers, GETP(pack->soundBase));
        SETP(pack->sfxData, (u8 *)((u8 *)GETP(pack->soundBase) + header.sfxOffset));
        if (header.songOffset != 0 || (header.soundDataSize != 0 && header.numSoundRelocs == 0)) {
            SETP(pack->song, (void *)((u8 *)GETP(pack->soundBase) + header.songOffset));
        }
        if (header.songBankOffset != 0) {
            SETP(pack->songBank, (void *)((u8 *)GETP(pack->soundBase) + header.songBankOffset));
        }
        pack->soundDataSize = header.soundDataSize;
        if (GETP(pack->song) != NULL && GETP(pack->songBank) != NULL && g_bSoundLoadingEnabled) {
            SongManager_LoadSong(GETP(pack->song), GETP(pack->songBank));
            pack->songLoaded = 1;
        }
        sfx = GETP(pack->sfxData);
        PortSwap_SFXDescriptors(sfx, pack->numSounds);  /* port */
        for (i = 0; i < pack->numSounds; i++) {
            if (g_bSoundLoadingEnabled) {
                GETP(pack->soundIDs)[i] = CreateSFX(sfx);
            } else {
                GETP(pack->soundIDs)[i] = 0;
            }
            if (GETP(pack->soundIDs)[i] == 0 && g_bSoundLoadingEnabled) {
                sprintf(msg, "Could not load the SFX, out of sound space.  SFX size = %d\n", ((SFX *)sfx)->length);
                Dprintf(msg);
            }
            sfx += sizeof(SFX);
        }
        if (freeData) {
            Deallocate(GETP(pack->soundData));
            SETP(pack->soundData, NULL);
            SETP(pack->soundBase, NULL);
            SETP(pack->sfxData, NULL);
            SETP(pack->song, NULL);
            SETP(pack->songBank, NULL);
        }
        SETP(pack->soundData, NULL);
        SETP(pack->soundBase, NULL);
        SETP(pack->sfxData, NULL);
        SETP(pack->song, NULL);
        SETP(pack->songBank, NULL);
    }
    SETP(pack->textureIDs, (u16 *)(alloc + soundIDsSize));
    offset = soundIDsSize + pack->numTextures * 2;
    SETP(pack->shaderIDs, (u16 *)(alloc + offset));
    offset += pack->numShaders * 2;
    readSize = header.numRelocs * 8 + header.numGX * 8 + header.numSoundRelocs * 8;
    pad = readSize & 0x1F;
    pad = pad == 0 ? 0 : 0x20 - pad;
    data = alloc + offset;
    data = (u8 *)PTR((((u32)HND(data) + 0x3F) & ~0x3F));
    readSize += pad;
    readSize += header.dataSize;
    readSize += relocSize;
    File_Read(file, readSize, data);
    {
        /* port: swap tables and data blob before the relocations below */
        u32 rs = header.numRelocs * 8 + header.numGX * 8 + header.numSoundRelocs * 8;
        u8 *blob = data + rs + pad;
        u32 *texRelocs = (u32 *)(blob + header.dataSize);

        PortSwap_PackData(data, rs / 4, blob, header.dataSize, texRelocs, header.numTexRelocs,
                          texRelocs + header.numTexRelocs * 2, header.numPointers,
                          header.textureDataOffset, header.numTextures,
                          header.shaderOffset, header.numShaders,
                          header.triggerOffset, header.numTriggers);
    }
    SETP(pack->relocs, (s32 *)data);
    offset = header.numRelocs * 8;
    SETP(pack->gxList, (u32 *)(data + offset));
    offset += header.numGX * 8;
    SETP(pack->soundRelocs, (u32 *)(data + offset));
    offset += header.numSoundRelocs * 8;
    SETP(pack->base, data + offset);
    offset = header.dataSize + offset;
    SETP(pack->base, PTR((HND(GETP(pack->base)) + 0x1F) & ~0x1F));
    offset = pad + offset;
    reloc = (u32 *)(data + offset);
    pointers = reloc + header.numTexRelocs * 2;
    for (i = 0; i < header.numTexRelocs; i++) {
        packID = reloc[1] & 0xFFFF0000;
        if (packID != 0) {
            packID >>= 16;
            idx = pack->refs[0].id != packID;
            if (GETP(pack->refs[idx].pack) == NULL) {
                SETP(pack->refs[idx].pack, FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&pack->refs[idx], 8, packID, 0), 8, 1));
            }
            *(u32 *)(GETP(pack->base) + reloc[0]) = ((u32 *)GETP(GETP(pack->refs[idx].pack)->base))[reloc[1] & 0xFFFF];
        } else {
            *(u32 *)(GETP(pack->base) + reloc[0]) += (u32)HND(GETP(pack->base));
        }
        reloc += 2;
    }
    UpdatePointers(pointers, header.numPointers, GETP(pack->base));
    SETP(pack->paletteData, (void *)((u8 *)GETP(pack->base) + header.paletteOffset));
    SETP(pack->shaders, (DataPackShader *)((u8 *)GETP(pack->base) + header.shaderOffset));
    pack->textureData = header.textureDataOffset + (u32)HND(GETP(pack->base));
    SETP(pack->triggers, (u8 *)((u8 *)GETP(pack->base) + header.triggerOffset));
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
        SETP(quad->alloc, alloc);
        if (alloc != NULL) {
            data = (u8 *)PTR((((u32)HND(alloc) + 0x3F) & ~0x3F));
            File_Read(file, quad->size, data);
            PortSwap_QuadPack(data, quad->size, quad->enabled);  /* port: big-endian file */
            SETP(quad->header, (QuadPackHeader *)data);
            offset = 0x54;
            header = GETP(quad->header);
            if (header->valid == 0) {
                SETP(quad->data, NULL);
                return;
            }
            n = header->numRelocs;
            if (n != 0) {
                SETP(header->relocs, (s32 *)(data + offset));
                offset += n * 8;
            }
            header = GETP(quad->header);
            n = header->numShaders;
            if (n != 0) {
                SETP(header->shaderList, (s32 *)(data + offset));
                offset += n * 8;
            }
            header = GETP(quad->header);
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
            for (i = 0; i < GETP(quad->header)->numTexRelocs; i++) {
                base = (u8 *)PTR(quad->gxBase);
                packID = reloc[1] & 0xFFFF0000;
                if (packID != 0) {
                    packID >>= 16;
                    header = GETP(quad->header);
                    idx = header->refs[0].id != packID;
                    if (GETP(header->refs[idx].pack) == NULL) {
                        SETP(header->refs[idx].pack, FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&header->refs[idx], 8, packID, 0), 8, 1));
                    }
                    *(u32 *)(base + reloc[0]) = ((u32 *)GETP(GETP(header->refs[idx].pack)->base))[reloc[1] & 0xFFFF];
                } else {
                    *(u32 *)(base + reloc[0]) += (u32)HND(base);
                }
                reloc += 2;
            }
            UpdatePointers(pointers, GETP(quad->header)->numPointers, (u8 *)PTR(quad->gxBase));
            SETP(quad->data, SLOT_LOAD(void *, (u32 *)PTR(quad->gxBase) + (0)));
            SETP(quad->vertices, SLOT_LOAD(void *, (u32 *)PTR(quad->gxBase) + (1)));
        }
    }
}

/* A quadrant pack (WorldQuad, as QuadPack) is back: patch its texture ids
 * and shaders. */
void DataPack_RefreshQuadPackPtrs(DataPack *pack, QuadPack *quad)
{
    s32 *reloc;
    s32 *shader;
    u32 id;
    QuadPackHeader *header;
    u8 *base;
    u32 i;
    u32 idx;
    u32 texID;

    header = GETP(quad->header);
    base = (u8 *)PTR(quad->gxBase);
    reloc = header != NULL ? GETP(header->relocs) : NULL;
    shader = header != NULL ? GETP(header->shaderList) : NULL;
    if (header != NULL)
    {
        for (i = 0; i < header->numRelocs; i++)
        {
            id = reloc[1];
            if (id & 0xFFFF0000)
            {
                u32 packID = id >> 16;
                idx = header->refs[0].id != packID;
                if (GETP(header->refs[idx].pack) == NULL)
                {
                    SETP(header->refs[idx].pack, FileStreaming_InitializeDataPack(
                            FileStreaming_LoadDataPack(&header->refs[idx], 8, packID, 0), 8, 1));
                }
                texID = GETP(GETP(header->refs[idx].pack)->textureIDs)[id & 0xFFFF];
            }
            else
            {
                texID = GETP(pack->textureIDs)[id];
            }
            *(u32 *)(base + reloc[0]) = texID;
            reloc += 2;
        }
        for (i = 0; i < header->numShaders; i++)
        {
            ShaderHolder_AddGX(base + shader[0], GETP(pack->shaderIDs)[shader[1]]);
            shader += 2;
        }
        if (GETP(quad->vertices) != NULL)
        {
            VertexColorSeqManager_AddVertices(GETP(quad->vertices));
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

    base = GETP(pack->base);
    Gfx_LoadPalettes_RAM(GETP(pack->paletteData), HND(GETP(pack->palettes)));
    {
        /* port: textureData is a handle; Gfx_LoadTexture_RAM advances a pointer */
        u8 *tex = PTR(pack->textureData);

        for (i = 0; i < pack->numTextures; i++) {
            GETP(pack->textureIDs)[i] = Gfx_LoadTexture_RAM(&tex);
            texData = HND(tex);
            if (texData & 0x1F) {
                tex += 0x20 - (texData & 0x1F);
            }
        }
    }
    for (i = 0; i < pack->numRelocs; i++) {
        id = GETP(pack->relocs)[i * 2 + 1];
        if (id & 0xFFFF0000) {
            u32 packID = id >> 16;
            idx = pack->refs[0].id != packID;
            if (GETP(pack->refs[idx].pack) == NULL) {
                SETP(pack->refs[idx].pack, FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&pack->refs[idx], 8, packID, 0), 8, 1));
            }
            texID = GETP(GETP(pack->refs[idx].pack)->textureIDs)[id & 0xFFFF];
        } else {
            texID = GETP(pack->textureIDs)[id];
        }
        *(u32 *)(base + GETP(pack->relocs)[i * 2]) = texID;
    }
}

void DataPack_RefreshPtrs(DataPack *pack) {
    u8 *base;
    u32 i;
    u8 *trigger;
    u32 packID;
    u32 idx;
    u32 id;

    base = GETP(pack->base);
    if (base != NULL) {
        DataPack_RefreshTextures(pack);
        for (i = 0; i < pack->numSoundRelocs; i++) {
            id = GETP(pack->soundRelocs)[i * 2 + 1];
            if (id & 0xFFFF0000) {
                packID = id >> 16;
                idx = pack->refs[0].id != packID;
                if (GETP(pack->refs[idx].pack) == NULL) {
                    SETP(pack->refs[idx].pack, FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&pack->refs[idx], 8, packID, 0), 8, 1));
                }
                id = GETP(GETP(pack->refs[idx].pack)->soundIDs)[id & 0xFFFF];
            } else {
                id = GETP(pack->soundIDs)[id];
            }
            *(u32 *)(base + GETP(pack->soundRelocs)[i * 2]) = id;
        }
        for (i = 0; i < pack->numShaders; i++) {
            GETP(pack->shaderIDs)[i] = ShaderHolder_AddShader(PTR(GETP(pack->shaders)[i].shader));
        }
        for (i = 0; i < pack->numGX; i++) {
            ShaderHolder_AddGX(base + GETP(pack->gxList)[i * 2], GETP(pack->shaderIDs)[GETP(pack->gxList)[i * 2 + 1]]);
        }
        trigger = GETP(pack->triggers);
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

    if (GETP(pack->soundIDs) != NULL) {
        for (i = 0; i < pack->numSounds; i++) {
            if (GETP(pack->soundIDs)[i] != 0) {
                DestroySFX(GETP(pack->soundIDs)[i]);
            }
        }
        gx = GETP(pack->gxList);
        for (i = 0; i < pack->numGX; i++) {
            ShaderHolder_RemoveGX((s32)HND(GETP(pack->base)) + *gx);
            gx += 2;
        }
        for (i = 0; i < pack->numShaders; i++) {
            ShaderHolder_RemoveShader(PTR(GETP(pack->shaders)[i].shader));
        }
        for (i = 0; i < pack->numTextures; i++) {
            Gfx_UnLoadTexture(GETP(pack->textureIDs)[i]);
        }
        trigger = GETP(pack->triggers);
        for (i = 0; i < pack->numTriggers; i++, trigger += 0x2C) {
            Trigger_ReleaseTrigger((s32 *)trigger);
            releasedTriggers = 1;
        }
        if (releasedTriggers) {
            Action_ReleaseAllActions();
        }
        Deallocate(GETP(pack->soundIDs));
        Deallocate(GETP(pack->soundData));
        SETP(pack->soundIDs, NULL);
        SETP(pack->soundData, NULL);
        memset(pack, 0, 0x94);
    }
}


void DataPack_ReleaseQuadPack(QuadPack *pack)
{
    s32 *list;
    s32 base;
    u32 i;

    if (GETP(pack->gxObject) != NULL) {
        base = pack->gxBase;
        list = GETP(GETP(pack->gxObject)->shaderList);
        for (i = 0; i < GETP(pack->gxObject)->count; i++) {
            ShaderHolder_RemoveGX(base + *list);
            list += 2;
        }
        if (GETP(pack->vertices) != NULL) {
            VertexColorSeqManager_RemoveVertices(GETP(pack->vertices));
        }
        Deallocate(GETP(pack->alloc));
        SETP(pack->alloc, NULL);
        SETP(pack->data, NULL);
        SETP(pack->vertices, NULL);
        pack->gxBase = 0;
        SETP(pack->gxObject, NULL);
        pack->flag34 = 0;
    }
}

/* A pack's scripts (DataPack.base): the records the game asks for by id. */
typedef struct DataPackScript {
    PTR32(u8) script;           /* 0x00 */
    u32 id;                     /* 0x04 */
} DataPackScript;

typedef struct DataPackScripts {
    u32 count;                  /* 0x00 */
    PTR32(DataPackScript) entries; /* 0x04 */
} DataPackScripts;

u8 *DataPack_GetScript(DataPack *pack, s32 id)
{
    DataPackScript *entries;
    u32 i;
    DataPackScripts *table = (DataPackScripts *)GETP(pack->base);

    if (table == NULL)
    {
        return NULL;
    }
    for (i = 0; i < table->count; i++)
    {
        entries = GETP(table->entries);
        if (id == entries[i].id)
        {
            return GETP(entries[i].script);
        }
    }
    return NULL;
}
