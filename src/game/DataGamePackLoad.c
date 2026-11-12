#include "common.h"
#include "Port_Mods.h"
#include "Voice.h"
#include "Port_Swap.h"
#include "xdl.h"
#include "Port_Content.h"
#include "Port_Mix.h"
#include "Port_Proto.h"

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



static u32 s_xdlType, s_xdlId;
u64 SDL_GetTicksNS(void);

/* port: each type's TOC, as long as the archive's. The game kept 50 entries
 * in g_fileTOCCache[type].entries and read the archive's first 0x800 bytes
 * (84 entries at most), and HW_Items already has 49: an archive with more
 * packs (a mod's) overran the cache into the next type's. The TOC is read
 * whole here and kept beside the cache, which keeps its count and name. */
static FileTOCEntry *s_tocEntries[11];
static u32 s_tocCap[11];

static int TocReserve(u32 type, u32 n) {
    FileTOCEntry *p;

    if (n <= s_tocCap[type]) {
        return 1;
    }
    p = realloc(s_tocEntries[type], n * sizeof(FileTOCEntry));
    if (p == NULL) {
        return 0;
    }
    s_tocEntries[type] = p;
    s_tocCap[type] = n;
    return 1;
}

s32 DataPack_LoadFile(char *name, s32 arg1, u32 type, u32 id) {
    extern u32 g_buffer[];
    FileTOCCacheEntry *toc;
    char *tocName;
    u32 i;
    s8 found;
    char path[0x80];
    u32 tocId;
    s32 proto;
    const char *source = name;

    found = 0;
    /* port: the pack an entry's data is (Port_ContentData.c): an extra car
     * loads the pack it aliases, prototype content the prototype's pack
     * (Port_Proto.c), under the prototype's archive name ("~HW_Cars") */
    tocId = Port_ContentPackSource(name, type, id, &source);
    name = (char *)source;
    proto = Port_ProtoName(name) != NULL;
    snprintf(path, sizeof(path), "%s.xgc", name);   /* port: bounded (strcpy + strcat) */
    if ((g_fileID != -1 && g_fileTOCCache[type].name == NULL) || strcmp(g_fileTOCCache[type].name, name) != 0) {
        for (i = 0; i < 11; i++) {
            if (i != type && g_fileTOCCache[i].name != NULL && strcmp(g_fileTOCCache[i].name, name) == 0) {
                memcpy(&g_fileTOCCache[type], &g_fileTOCCache[i], sizeof(FileTOCCacheEntry));
                if (TocReserve(type, g_fileTOCCache[i].count)) {   /* port: the whole TOC */
                    memcpy(s_tocEntries[type], s_tocEntries[i], g_fileTOCCache[i].count * sizeof(FileTOCEntry));
                }
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
                snprintf(msg, sizeof(msg), "Could not open the data pack: %s\n", path);
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
        PortData_TOC(Port_FileFormat(g_fileID), g_buffer, 0x800 / 4);  /* port: GameCube files are big-endian */
        if (g_buffer[0] != 0x020100A0 || g_buffer[1] != 0x040100AE) {
            File_Close(g_fileID);
            g_fileID = -1;
            g_openFileDataPackType = 0xB;
            return 0;
        }
        if (g_buffer[2] != 0xB7 && !(proto && (g_buffer[2] == 177 || g_buffer[2] == 179))) {   /* port: the prototype's */
            char msg[0x80];
            File_Close(g_fileID);
            g_fileID = -1;
            g_openFileDataPackType = 0xB;
            snprintf(msg, sizeof(msg), "The data pack is out of date: %s. Packfile:%d,  Code:%d\nYou need to repack the data using the latest BrainCore\n", path, g_buffer[2], 0xB7);
            Dprintf(msg);
            return 0;
        }
        g_fileTOCCache[type].count = g_buffer[3];
        /* port: the whole TOC, however long (see s_tocEntries) */
        if (!TocReserve(type, g_buffer[3])) {
            File_Close(g_fileID);
            g_fileID = -1;
            g_openFileDataPackType = 0xB;
            Port_Log("data pack %s: out of memory for a TOC of %u packs", path, g_buffer[3]);
            return 0;
        }
        if (0x10 + g_buffer[3] * sizeof(FileTOCEntry) <= 0x800) {
            memcpy(s_tocEntries[type], &g_buffer[4], g_buffer[3] * sizeof(FileTOCEntry));
        } else {
            u32 n = g_buffer[3], size = 0x10 + n * (u32)sizeof(FileTOCEntry);
            u32 *whole = malloc(size);

            if (whole == NULL || File_Seek(g_fileID, 0, 0) < 0 || File_Read(g_fileID, (s32)size, whole) <= 0) {
                free(whole);
                File_Close(g_fileID);
                g_fileID = -1;
                g_openFileDataPackType = 0xB;
                Port_Log("data pack %s: its TOC of %u packs can't be read", path, n);
                return 0;
            }
            PortData_TOC(Port_FileFormat(g_fileID), whole, size / 4);
            memcpy(s_tocEntries[type], &whole[4], n * sizeof(FileTOCEntry));
            free(whole);
        }
        if (strlen(name) >= 0x4C) {
            strncpy(tocName, name, 0x4C);
            g_fileTOCCache[type].name[0x4B] = 0;
        } else {
            strcpy(tocName, name);
        }
    }
    for (i = 0; i < toc->count; i++) {
        if (type == s_tocEntries[type][i].type && tocId == s_tocEntries[type][i].id) {
            if (s_tocEntries[type][i].version != 0xB7 &&
                !(proto && (s_tocEntries[type][i].version == 177 || s_tocEntries[type][i].version == 179))) {
                char msg[0x80];
                snprintf(msg, sizeof(msg), "The data pack is out of date: %s. Packfile:%d,  Code:%d\nYou need to repack the data using the latest BrainCore\n", path, s_tocEntries[type][i].version, 0xB7);
                Dprintf(msg);
                File_Close(g_fileID);
                g_fileID = -1;
                g_openFileDataPackType = 0xB;
                return 0;
            }
            /* port: the prototype's offsets count from the end of their own entry */
            File_Seek(g_fileID, s_tocEntries[type][i].offset + 0x10 + (proto ? i + 1 : g_fileTOCCache[type].count) * sizeof(FileTOCEntry), 0);
            found = 1;
            break;
        }
    }
    if (!found) {
        char msg[0x80];
        /* port: snprintf - the message with a full path is longer than msg (the
         * release's paths were short): a pack that isn't there overran it */
        snprintf(msg, sizeof(msg), "Could not load the data pack with Pack ID: %d. Within the file: %s\n.Check the PackID and repack using the latest BrainCore.\n", id, path);
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
    s_xdlType = type;   /* port: for the pack cache's TOC (xdl.c) */
    s_xdlId = tocId;
    {
        /* port: HWVX_LOADTIME=1 logs the time packs take to load (nested
         * ones count in the pack that asked for them) */
        static int depth, timing = -1;
        static u64 total;
        u64 start = 0, t;

        if (timing < 0) {
            timing = getenv("HWVX_LOADTIME") != NULL;
        }
        if (timing && depth++ == 0) {
            start = SDL_GetTicksNS();
        }
        PortMix_BeginPack(name, type, tocId);   /* port: a mixed build's pack (Port_Mix.c) */
        DataPack_LoadDataPack(g_fileID, PTR(arg1));
        PortMix_EndPack();
        if (timing && --depth == 0) {
            t = SDL_GetTicksNS() - start;
            total += t;
            Port_Log("loadtime: %s#%u %.3f ms, packs %.3f ms", name, id, t / 1e6, total / 1e6);
        }
    }
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

/* port: what File_Read does to the heap for a read of `size` bytes, without
 * reading: a small read borrows a buffer (FileManager_NGC.c) and leaves the
 * file's bytes in it, `raw` from the cache. A pack from the cache (xdl.c)
 * takes the same blocks in the same order as from its source, and leaves
 * the same bytes behind in them, so the heap ends up as it would have. */
static void SkipRead(XdlPack *xp, int raw, u32 size) {
    u8 *buffer;

    if (size != 0 && size < 0xC800) {
        buffer = Allocate(((size + 0x1F) & ~0x1F) + 0x40, 0, "");
        if (buffer != NULL) {
            Xdl_ReadSeg(xp, raw, PTR((HND(buffer) + 0x1F) & ~0x1F), 0, Xdl_SegSize(xp, raw));
        }
        Deallocate(buffer);
    }
}

/* port: recording a pack for the cache, the bytes a small read of `size`
 * just left in the buffer it borrowed (FileManager_NGC.c File_Read). */
static void RecordRead(s32 file, int raw, u32 size) {
    u32 len = (size + 0x1F) & ~0x1F;
    u8 *bytes;

    if (size != 0 && size < 0xC800 && Xdl_Writing() && (bytes = malloc(len)) != NULL) {
        Xdl_RawRead(raw, bytes, Port_FilePeek(file, Port_FileTell(file) - size, bytes, len));
        free(bytes);
    }
}

/* port: a cached pack's block of `seg`, allocated as its converter did. */
static void *XdlBlock(XdlPack *xp, int seg) {
    u32 size = Xdl_SegSize(xp, seg);
    void *block;

    if (size == 0) {
        return NULL;
    }
    block = Allocate(size, 0, "");
    if (block == NULL || (HND(block) & 0x1F)) {
        Port_Fatal("out of memory loading a cached pack, or not 32-byte aligned");
    }
    Xdl_ReadSeg(xp, seg, block, 0, size);
    Xdl_PlaceSeg(xp, seg, block);
    return block;
}

void DataPack_LoadDataPack(s32 file, DataPack *pack) {
    extern s8 g_bSoundLoadingEnabled;
    static DataPackFileHeader header;
    DataPackFileHeader ownHeader;
    char msg[0x68];
    u32 relocSize;
    s8 freeData;
    u32 soundIDsSize;
    u8 *alloc;
    u32 i;
    u32 readSize;
    u32 tablesSize;
    u32 size;
    u32 pad;
    u8 *data;
    u8 *sfx;
    u32 offset;
    u32 *reloc;
    u32 *pointers;
    u32 packID;
    u32 idx;
    s32 fmt;
    XdlPack *xp;
    u8 *xdlBank = NULL;
    void *xdlSfx = NULL;
    s32 proto;

    memset(pack, 0, sizeof(DataPack));
    /* port: the pack as an earlier load left it, cached (xdl.c, HWVX_XDL):
     * the same heap blocks, filled from the cache, its pointers fixed up;
     * no conversion runs. Otherwise its load is recorded for the cache
     * (HWVX_XDL_WRITE). */
    xp = Xdl_FindPack(Port_FileXdl(file), Port_FileTell(file));
    fmt = Port_FileFormat(file);   /* port: whose pack it is (Port_Data.c) */
    pack->portFormat = (u8)fmt;
    /* port: the PS2 prototype's (Port_Proto.c): no sector padding, its
     * pointer tables before its data, other texture headers */
    proto = Port_FileProto(file);
    pack->portProto = (u8)proto;
    if (xp != NULL) {
        SkipRead(xp, XDL_SEG_RAW_HEADER, sizeof(DataPackFileHeader));
        Xdl_Header(xp, &header, sizeof(header));
    } else {
        Xdl_BeginPack(Port_FileXdl(file), Port_FileTell(file), s_xdlType, s_xdlId);
        File_Read(file, sizeof(DataPackFileHeader), &header);
        RecordRead(file, XDL_SEG_RAW_HEADER, sizeof(DataPackFileHeader));
        PortData_PackHeader(fmt, (u32 *)&header, 20, 2, sizeof(DataPackRef));
        Xdl_PackHeader(&header, sizeof(header));
        if (fmt == PORT_DATA_PS2 && !proto) {
            Port_FileAlign(file, PORT_PS2_SECTOR);   /* port: the header is padded to a sector */
        }
    }
    pack->numRelocs = header.numRelocs;
    pack->numSoundRelocs = header.numSoundRelocs;
    pack->numTextures = header.numTextures;
    pack->numSounds = header.numSounds;
    /* port: 0 on GameCube/PC; the PS2's CLUT count, which isn't a handle
     * (its CLUTs are expanded at load, Port_PS2.c) */
    SETP(pack->palettes, fmt == PORT_DATA_PS2 ? NULL : (void *)PTR(header.palettes));
    pack->numShaders = header.numShaders;
    pack->numGX = header.numGX;
    pack->numTriggers = header.numTriggers;
    pack->songLoaded = 0;
    pack->dataSize = header.dataSize;
    memcpy(pack->refs, header.refs, sizeof(header.refs));
    if (proto) {
        Port_ProtoRefs(pack->refs);   /* port: the packs it refers to are the prototype's too */
    }
    size = header.numRelocs * 8 + (pack->numSounds + pack->numTextures) * 2 + pack->numShaders * 2 + header.numGX * 8 + header.numSoundRelocs * 8 + header.dataSize + 0x60;
    relocSize = header.numTexRelocs * 8 + header.numPointers * 4;
    size += relocSize;
    alloc = Allocate(size, 0, "");
    if (alloc == NULL) {
        if (xp != NULL) {
            Xdl_FreePack(xp);
        } else {
            Xdl_AbortPack();
        }
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
        if (xp != NULL) {
            /* port: converted and relocated */
            if (Xdl_SegSize(xp, XDL_SEG_SOUND) != ((readSize + 0x1F) & ~0x1F)) {
                Port_Fatal("a cached pack's sound size doesn't match its header: delete the .xdl cache");
            }
            SkipRead(xp, XDL_SEG_RAW_SOUND, readSize);
            Xdl_ReadSeg(xp, XDL_SEG_SOUND, sfx, 0, Xdl_SegSize(xp, XDL_SEG_SOUND));
            Xdl_PlaceSeg(xp, XDL_SEG_SOUND, sfx);
        } else if (proto) {
            /* port: the prototype's sound data follows its pointers unpadded */
            File_Read(file, header.numSoundPointers * 4, sfx);
            File_Read(file, header.soundDataSize, PTR((HND(sfx) + header.numSoundPointers * 4 + 0x1F) & ~0x1F));
        } else {
            File_Read(file, readSize, sfx);
            RecordRead(file, XDL_SEG_RAW_SOUND, readSize);
            if (fmt == PORT_DATA_PS2) {
                Port_FileAlign(file, PORT_PS2_SECTOR);   /* port: so is the sound data */
            }
        }
        data = sfx;
        sfx += header.numSoundPointers * 4;
        SETP(pack->soundBase, (u8 *)PTR((((u32)HND(sfx) + 0x1F) & ~0x1F)));
        /* port: the pointer table and the slots it lists are big-endian; the
         * rest of the sound data stays so (the mixer reads it that way) */
        if (xp == NULL) {
            PortData_SoundPointers(fmt, (u32 *)data, header.numSoundPointers, GETP(pack->soundBase));
        }
        PortData_SoundData(GETP(pack->soundBase), header.soundDataSize);
        if (xp == NULL) {
            UpdatePointers((u32 *)data, header.numSoundPointers, GETP(pack->soundBase));
        }
        SETP(pack->sfxData, (u8 *)((u8 *)GETP(pack->soundBase) + header.sfxOffset));
        if (header.songOffset != 0 || (header.soundDataSize != 0 && header.numSoundRelocs == 0)) {
            SETP(pack->song, (void *)((u8 *)GETP(pack->soundBase) + header.songOffset));
        }
        if (header.songBankOffset != 0) {
            SETP(pack->songBank, (void *)((u8 *)GETP(pack->soundBase) + header.songBankOffset));
            /* port: the PC's is little-endian, the PS2's rebuilt elsewhere (Port_Data.c) */
            if (xp == NULL) {
                SETP(pack->songBank, PortData_SongBank(fmt, GETP(pack->songBank)));
            } else {
                /* port: in place, or the PS2's in its own block, or none */
                u32 off;
                int seg = Xdl_BankRef(xp, &off);

                if (seg == XDL_SEG_BANK) {
                    xdlBank = XdlBlock(xp, XDL_SEG_BANK);
                    SETP(pack->songBank, xdlBank + off);
                } else if (seg < 0) {
                    SETP(pack->songBank, NULL);
                }
            }
        }
        pack->soundDataSize = header.soundDataSize;
        if (GETP(pack->song) != NULL && GETP(pack->songBank) != NULL && g_bSoundLoadingEnabled) {
            SongManager_LoadSong(GETP(pack->song), GETP(pack->songBank));
            pack->songLoaded = 1;
        }
        if (xp == NULL) {
            PortData_SongBankDone(fmt);   /* port: CreateMIDI has its copy */
        } else if (xdlBank != NULL) {
            Deallocate(xdlBank);
        }
        sfx = GETP(pack->sfxData);
        if (xp == NULL) {
            /* port: a mixed build's samples from another release (Port_Mix.c) */
            PortMix_SFXPrepare(fmt, sfx, pack->numSounds);
            PortData_SFX(fmt, sfx, pack->numSounds);  /* port */
            PortMix_SFX(fmt, sfx, pack->numSounds);
            Xdl_Sound(data, (readSize + 0x1F) & ~0x1F, (u32 *)data, header.numSoundPointers, GETP(pack->soundBase),
                      GETP(pack->songBank));
        } else {
            xdlSfx = XdlBlock(xp, XDL_SEG_SFX);   /* port: the PS2's decoded samples */
        }
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
        if (xp == NULL) {
            PortData_SFXDone(fmt);   /* port: CreateSFX has copied the samples */
            PortMix_SFXDone();
        } else {
            Deallocate(xdlSfx);
            Xdl_PlaceSeg(xp, XDL_SEG_SOUND, NULL);
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
    tablesSize = (header.numRelocs + header.numGX + header.numSoundRelocs) * 8;
    pad = -tablesSize & 0x1F;
    data = alloc + offset;
    data = (u8 *)PTR((((u32)HND(data) + 0x3F) & ~0x3F));
    readSize = tablesSize + pad + header.dataSize + relocSize;
    if (xp != NULL) {
        /* port: the tables, blob and relocations as the source load left them */
        if (Xdl_SegSize(xp, XDL_SEG_MAIN) != ((readSize + 0x1F) & ~0x1F)) {
            Port_Fatal("a cached pack's size doesn't match its header: delete the .xdl cache");
        }
        SkipRead(xp, XDL_SEG_RAW_MAIN, readSize);
        Xdl_ReadSeg(xp, XDL_SEG_MAIN, data, 0, Xdl_SegSize(xp, XDL_SEG_MAIN));
        Xdl_PlaceSeg(xp, XDL_SEG_MAIN, data);
        if (fmt == PORT_DATA_GC) {
            /* as the byte-swapper does (Port_Swap.c Swapper_Finish) */
            Port_InvalidateDisplayLists(data + tablesSize + pad, header.dataSize);
        }
    } else {
        if (proto) {
            /* port: the prototype's pointer tables come before its data;
             * read in the file's order, each to where the loader has it
             * (a read rounds up to 32 bytes: the tables go aside first) */
            u8 *tables = relocSize != 0 ? malloc(relocSize + 0x20) : NULL;

            File_Read(file, tablesSize, data);
            if (tables != NULL) {
                File_Read(file, relocSize, tables);
            }
            File_Read(file, header.dataSize, data + tablesSize + pad);
            if (tables != NULL) {
                memcpy(data + tablesSize + pad + header.dataSize, tables, relocSize);
                free(tables);
            }
        } else {
            File_Read(file, readSize, data);
            RecordRead(file, XDL_SEG_RAW_MAIN, readSize);
        }
        {
            /* port: swap tables and data blob before the relocations below */
            u8 *blob = data + tablesSize + pad;
            u32 *texRelocs = (u32 *)(blob + header.dataSize);

            g_portSwapNumTextureRelocs = header.numRelocs;
            PortData_PackData(fmt, data, tablesSize / 4, blob, header.dataSize, texRelocs, header.numTexRelocs,
                              texRelocs + header.numTexRelocs * 2, header.numPointers,
                              header.textureDataOffset, header.numTextures,
                              header.shaderOffset, header.numShaders,
                              header.triggerOffset, header.numTriggers);
        }
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
    /* port: a pack this one refers to may load just below (the prototype's
     * shared packs, Port_Proto.c, aren't loaded ahead as the release's
     * are), into the same static header: the header is kept aside */
    memcpy(&ownHeader, &header, sizeof(header));
    for (i = 0; i < ownHeader.numTexRelocs; i++) {
        packID = reloc[1] & 0xFFFF0000;
        if (packID != 0) {
            packID >>= 16;
            idx = pack->refs[0].id != packID;
            if (GETP(pack->refs[idx].pack) == NULL) {
                SETP(pack->refs[idx].pack, FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(&pack->refs[idx], 8, packID, 0), 8, 1));
            }
            *(u32 *)(GETP(pack->base) + reloc[0]) = ((u32 *)GETP(GETP(pack->refs[idx].pack)->base))[reloc[1] & 0xFFFF];
        } else if (xp == NULL) {   /* port: a cached pack's are fixups */
            *(u32 *)(GETP(pack->base) + reloc[0]) += (u32)HND(GETP(pack->base));
        }
        reloc += 2;
    }
    memcpy(&header, &ownHeader, sizeof(header));
    if (xp != NULL) {
        /* port: the blocks the converters allocated, in their order */
        for (i = XDL_SEG_BLOCK; i < XDL_MAX_SEGS; i++) {
            void *block = XdlBlock(xp, i);

            if (block != NULL && Xdl_BlockRole(xp, i) == XDL_ROLE_SOUNDDATA) {
                SETP(pack->soundData, block);
            } else if (block != NULL) {
                PortGeometry_Keep(pack, block);
            }
        }
        Xdl_FreePack(xp);
    } else {
        UpdatePointers(pointers, header.numPointers, GETP(pack->base));
        if (fmt == PORT_DATA_GC) {
            /* port: the GameCube's geometry in the indexed layout (Port_Geometry.c);
             * soundData, NULL by now, holds it and is freed with the pack */
            SETP(pack->soundData, PortGeometry_ConvertGC(GETP(pack->base), header.dataSize, pointers,
                                                         header.numPointers, pointers - header.numTexRelocs * 2,
                                                         header.numTexRelocs));
        } else if (fmt == PORT_DATA_PS2) {
            /* port: the PS2's geometry and textures (Port_Geometry.c, Port_PS2.c),
             * converted now that the slots hold pointers */
            SETP(pack->soundData, PortGeometry_ConvertPS2(GETP(pack->base), header.dataSize, pointers,
                                                          header.numPointers, (u32 *)GETP(pack->relocs),
                                                          header.numRelocs));
            PortGeometry_Keep(pack, PortPS2_Textures(GETP(pack->base), header.dataSize, header.textureDataOffset,
                                                     header.numTextures, header.paletteOffset, header.palettes,
                                                     proto));
        }
        /* port: a mixed build's textures from the other releases (Port_Mix.c) */
        PortMix_Textures(pack, fmt, GETP(pack->base), header.dataSize, header.textureDataOffset, header.numTextures);
        Xdl_EndPack(GETP(pack->soundData), data, (tablesSize + pad + header.dataSize + relocSize + 0x1F) & ~0x1F,
                    GETP(pack->base),
                    pointers, header.numPointers,
                    pointers - header.numTexRelocs * 2, header.numTexRelocs);
    }
    SETP(pack->paletteData, (void *)((u8 *)GETP(pack->base) + header.paletteOffset));
    SETP(pack->shaders, (DataPackShader *)((u8 *)GETP(pack->base) + header.shaderOffset));
    pack->textureData = header.textureDataOffset + (u32)HND(GETP(pack->base));
    SETP(pack->triggers, (u8 *)((u8 *)GETP(pack->base) + header.triggerOffset));
}



static void DataPack_LoadQuadPackTimed(s32 file, QuadPack *quad);

void DataPack_LoadQuadPack(s32 file, QuadPack *quad) {

    static int timing = -1;
    static u64 total;
    u64 start = 0;

    if (timing < 0) {
        timing = getenv("HWVX_LOADTIME") != NULL;   /* port: as DataPack_LoadFile */
    }
    if (timing) {
        start = SDL_GetTicksNS();
    }
    DataPack_LoadQuadPackTimed(file, quad);
    if (timing && GETP(quad->alloc) != NULL) {
        u64 t = SDL_GetTicksNS() - start;

        total += t;
        Port_Log("loadtime: quadrant %u %.3f ms, quadrants %.3f ms", quad->enabled, t / 1e6, total / 1e6);
    }
}

static void DataPack_LoadQuadPackTimed(s32 file, QuadPack *quad) {
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
    XdlPack *xp = NULL;

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
            /* port: as a data pack (DataPack_LoadDataPack), from the pack
             * cache or recorded for it (xdl.c) */
            xp = Xdl_FindPack(Port_FileXdl(file), quad->fileOffset);
            if (xp != NULL) {
                if (Xdl_SegSize(xp, XDL_SEG_MAIN) != ((quad->size + 0x1F) & ~0x1F)) {
                    Port_Fatal("a cached quadrant's size doesn't match its world's: delete the .xdl cache");
                }
                SkipRead(xp, XDL_SEG_RAW_MAIN, quad->size);
                Xdl_ReadSeg(xp, XDL_SEG_MAIN, data, 0, Xdl_SegSize(xp, XDL_SEG_MAIN));
                Xdl_PlaceSeg(xp, XDL_SEG_MAIN, data);
            } else {
                Xdl_BeginPack(Port_FileXdl(file), quad->fileOffset, 7, quad->enabled);
                File_Read(file, quad->size, data);
                RecordRead(file, XDL_SEG_RAW_MAIN, quad->size);
                PortData_QuadPack(Port_FileFormat(file), data, quad->size, quad->enabled);  /* port */
            }
            SETP(quad->header, (QuadPackHeader *)data);
            offset = 0x54;
            header = GETP(quad->header);
            if (header->valid == 0) {
                SETP(quad->data, NULL);
                if (xp != NULL) {
                    Xdl_FreePack(xp);
                } else {
                    Xdl_AbortPack();
                }
                return;
            }
            Xdl_KeepValue(&header->refs[0].pack);   /* port: filled in below */
            Xdl_KeepValue(&header->refs[1].pack);
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
            if (xp != NULL && Port_FileFormat(file) == PORT_DATA_GC) {
                /* port: as the byte-swapper does (Port_Swap.c Swapper_Finish) */
                Port_InvalidateDisplayLists(data + offset, quad->size - offset);
            }
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
                } else if (xp == NULL) {   /* port: a cached quadrant's are fixups */
                    *(u32 *)(base + reloc[0]) += (u32)HND(base);
                }
                reloc += 2;
            }
            if (xp != NULL) {
                /* port: the converted geometry's blocks, in their order */
                for (i = XDL_SEG_BLOCK; i < XDL_MAX_SEGS; i++) {
                    PortGeometry_Keep(quad, XdlBlock(xp, i));
                }
                Xdl_FreePack(xp);
            } else {
                UpdatePointers(pointers, GETP(quad->header)->numPointers, (u8 *)PTR(quad->gxBase));
                if (Port_FileFormat(file) == PORT_DATA_GC) {
                    /* port: the GameCube's geometry in the indexed layout (Port_Geometry.c) */
                    PortGeometry_Keep(quad, PortGeometry_ConvertGC((u8 *)PTR(quad->gxBase), quad->size - offset, pointers,
                                                                   GETP(quad->header)->numPointers, (u32 *)texRelocs,
                                                                   GETP(quad->header)->numTexRelocs));
                } else if (Port_FileFormat(file) == PORT_DATA_PS2) {
                    /* port: the PS2's geometry (Port_Geometry.c); its texture-id
                     * relocations at converted groups get flagged */
                    PortGeometry_Keep(quad, PortGeometry_ConvertPS2((u8 *)PTR(quad->gxBase), quad->size - offset, pointers,
                                                                    GETP(quad->header)->numPointers,
                                                                    (u32 *)GETP(GETP(quad->header)->relocs),
                                                                    GETP(quad->header)->numRelocs));
                }
                Xdl_EndPack(NULL, data, (quad->size + 0x1F) & ~0x1F, (u8 *)PTR(quad->gxBase), pointers,
                            GETP(quad->header)->numPointers, (u32 *)texRelocs, GETP(quad->header)->numTexRelocs);
            }
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
            PortSwap_StoreTextureID(base, reloc[0], texID);   /* port: was *(u32 *)(base + reloc[0]) = texID */
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
            /* the PC and PS2 releases pack the 0x10-byte headers (Port_Data.c) */
            if ((texData & 0x1F) && pack->portFormat != PORT_DATA_PC && pack->portFormat != PORT_DATA_PS2) {
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
        PortSwap_StoreTextureID(base, GETP(pack->relocs)[i * 2], texID);   /* port: was a u32 store */
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

    Port_ModsPackReleased(pack);   /* port: its textures' colour recipes go */

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
        PortGeometry_Release(pack);   /* port: a PS2 pack's expanded textures (Port_PS2.c) */
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
        PortGeometry_Release(pack);   /* port: its converted geometry (Port_Geometry.c) */
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
