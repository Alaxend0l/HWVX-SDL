/* CARD: memory card slots backed by files in the user's preferences folder
 * (SDL_GetPrefPath), one file per save: <pref>/memcard<A|B>/<name>.sav */
#include "port_internal.h"

typedef struct CARDFileInfo {
    s32 chan;
    s32 fileNo;
    s32 offset;
    s32 length;
    u16 iBlock;
    u16 pad;
} CARDFileInfo;

typedef struct CARDStat {
    char fileName[32];
    u32 length;
    u32 time;
    u8 gameName[4];
    u8 company[2];
    u8 bannerFormat;
    u8 __padding;
    u32 iconAddr;
    u16 iconFormat;
    u16 iconSpeed;
    u32 commentAddr;
    u32 offsetBanner;
    u32 offsetBannerTlut;
    u32 offsetIcon[8];
    u32 offsetIconTlut;
    u32 offsetData;
} CARDStat;

#define CARD_SECTOR 0x2000
#define CARD_BLOCKS 251   /* a 251-block (Memory Card 251) card */
#define CARD_MAX_FILES 127

typedef struct CardFile {
    char name[32];
    u32 size;
    CARDStat stat;
    int used;
} CardFile;

static struct {
    char *dir;
    int mounted;
    CardFile files[CARD_MAX_FILES];
} s_slots[2];

static char *SlotPath(s32 chan, const char *name) {
    char *path;
    SDL_asprintf(&path, "%s%s.sav", s_slots[chan].dir, name);
    return path;
}

static void ScanSlot(s32 chan) {
    char **names;
    int count = 0;
    int i;

    memset(s_slots[chan].files, 0, sizeof(s_slots[chan].files));
    names = SDL_GlobDirectory(s_slots[chan].dir, "*.sav", 0, &count);
    for (i = 0; names != NULL && i < count && i < CARD_MAX_FILES; i++) {
        CardFile *f = &s_slots[chan].files[i];
        char *path;
        SDL_PathInfo info;
        size_t len = strlen(names[i]) - 4;
        if (len >= sizeof(f->name)) {
            continue;
        }
        memcpy(f->name, names[i], len);
        f->name[len] = 0;
        path = SlotPath(chan, f->name);
        if (SDL_GetPathInfo(path, &info)) {
            f->size = (u32)info.size;
            f->used = 1;
            SDL_strlcpy(f->stat.fileName, f->name, sizeof(f->stat.fileName));
            f->stat.length = f->size;
        }
        SDL_free(path);
    }
    SDL_free(names);
}

static int FindFile(s32 chan, const char *name) {
    int i;
    for (i = 0; i < CARD_MAX_FILES; i++) {
        if (s_slots[chan].files[i].used && SDL_strcmp(s_slots[chan].files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void CARDInit(void) {
    char *pref = SDL_GetPrefPath("HWVX-SDL", "HotWheelsVelocityX");
    int chan;
    for (chan = 0; chan < 2; chan++) {
        SDL_asprintf(&s_slots[chan].dir, "%smemcard%c/", pref ? pref : "./", 'A' + chan);
        SDL_CreateDirectory(s_slots[chan].dir);
    }
    SDL_free(pref);
}

static s32 CheckChan(s32 chan) {
    if (chan != 0) {
        return CARD_RESULT_NOCARD; /* only slot A is populated */
    }
    return s_slots[chan].mounted ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
}

s32 CARDProbeEx(s32 chan, s32 *memSize, s32 *sectorSize) {
    if (chan != 0) {
        return CARD_RESULT_NOCARD;
    }
    if (memSize) *memSize = 16; /* megabits */
    if (sectorSize) *sectorSize = CARD_SECTOR;
    return CARD_RESULT_READY;
}

s32 CARDMount(s32 chan, void *workArea, void *detachCallback) {
    (void)workArea;
    (void)detachCallback;
    if (chan != 0) {
        return CARD_RESULT_NOCARD;
    }
    ScanSlot(chan);
    s_slots[chan].mounted = 1;
    return CARD_RESULT_READY;
}

s32 CARDUnmount(s32 chan) {
    if (chan >= 0 && chan < 2) {
        s_slots[chan].mounted = 0;
    }
    return CARD_RESULT_READY;
}

s32 CARDCheck(s32 chan) { return CheckChan(chan); }
s32 CARDFormat(s32 chan) { return CheckChan(chan); }

s32 CARDFreeBlocks(s32 chan, s32 *byteNotUsed, s32 *filesNotUsed) {
    u32 usedBlocks = 0;
    int usedFiles = 0;
    int i;
    s32 r = CheckChan(chan);
    if (r != CARD_RESULT_READY) {
        return r;
    }
    for (i = 0; i < CARD_MAX_FILES; i++) {
        if (s_slots[chan].files[i].used) {
            usedFiles++;
            usedBlocks += (s_slots[chan].files[i].size + CARD_SECTOR - 1) / CARD_SECTOR;
        }
    }
    if (byteNotUsed) *byteNotUsed = (s32)((CARD_BLOCKS - usedBlocks) * CARD_SECTOR);
    if (filesNotUsed) *filesNotUsed = CARD_MAX_FILES - usedFiles;
    return CARD_RESULT_READY;
}

s32 CARDGetEncoding(s32 chan, u16 *encode) {
    if (encode) *encode = 0; /* ANSI */
    return CheckChan(chan);
}

s32 CARDGetSectorSize(s32 chan, u32 *size) {
    if (size) *size = CARD_SECTOR;
    return CheckChan(chan);
}

s32 CARDCreate(s32 chan, const char *fileName, u32 size, struct CARDFileInfo *fileInfo) {
    CARDFileInfo *fi = (CARDFileInfo *)fileInfo;
    SDL_IOStream *io;
    char *path;
    int i;
    s32 r = CheckChan(chan);

    if (r != CARD_RESULT_READY) return r;
    if (strlen(fileName) >= 32) return CARD_RESULT_NAMETOOLONG;
    if (FindFile(chan, fileName) >= 0) return CARD_RESULT_EXIST;
    for (i = 0; i < CARD_MAX_FILES && s_slots[chan].files[i].used; i++) {
    }
    if (i == CARD_MAX_FILES) return CARD_RESULT_NOENT;

    path = SlotPath(chan, fileName);
    io = SDL_IOFromFile(path, "wb");
    SDL_free(path);
    if (io == NULL) return CARD_RESULT_IOERROR;
    {
        static const u8 zero[CARD_SECTOR];
        u32 left;
        for (left = size; left > 0;) {
            u32 n = left < CARD_SECTOR ? left : CARD_SECTOR;
            SDL_WriteIO(io, zero, n);
            left -= n;
        }
    }
    SDL_CloseIO(io);

    memset(&s_slots[chan].files[i], 0, sizeof(CardFile));
    SDL_strlcpy(s_slots[chan].files[i].name, fileName, 32);
    s_slots[chan].files[i].size = size;
    s_slots[chan].files[i].used = 1;
    SDL_strlcpy(s_slots[chan].files[i].stat.fileName, fileName, 32);
    s_slots[chan].files[i].stat.length = size;
    fi->chan = chan;
    fi->fileNo = i;
    fi->offset = 0;
    fi->length = (s32)size;
    return CARD_RESULT_READY;
}

s32 CARDOpen(s32 chan, const char *fileName, struct CARDFileInfo *fileInfo) {
    CARDFileInfo *fi = (CARDFileInfo *)fileInfo;
    int i;
    s32 r = CheckChan(chan);
    if (r != CARD_RESULT_READY) return r;
    i = FindFile(chan, fileName);
    if (i < 0) return CARD_RESULT_NOFILE;
    fi->chan = chan;
    fi->fileNo = i;
    fi->offset = 0;
    fi->length = (s32)s_slots[chan].files[i].size;
    return CARD_RESULT_READY;
}

s32 CARDClose(struct CARDFileInfo *fileInfo) {
    ((CARDFileInfo *)fileInfo)->chan = -1;
    return CARD_RESULT_READY;
}

s32 CARDDelete(s32 chan, const char *fileName) {
    char *path;
    int i;
    s32 r = CheckChan(chan);
    if (r != CARD_RESULT_READY) return r;
    i = FindFile(chan, fileName);
    if (i < 0) return CARD_RESULT_NOFILE;
    path = SlotPath(chan, fileName);
    SDL_RemovePath(path);
    SDL_free(path);
    s_slots[chan].files[i].used = 0;
    return CARD_RESULT_READY;
}

static s32 Transfer(CARDFileInfo *fi, void *buf, s32 length, s32 offset, int write) {
    CardFile *f;
    SDL_IOStream *io;
    char *path;
    size_t done;

    if (fi->chan < 0 || fi->chan > 1 || fi->fileNo < 0 || fi->fileNo >= CARD_MAX_FILES) {
        return CARD_RESULT_FATAL_ERROR;
    }
    f = &s_slots[fi->chan].files[fi->fileNo];
    if (!f->used) return CARD_RESULT_NOFILE;
    if (offset < 0 || (u32)(offset + length) > f->size) return CARD_RESULT_LIMIT;
    path = SlotPath(fi->chan, f->name);
    io = SDL_IOFromFile(path, write ? "r+b" : "rb");
    SDL_free(path);
    if (io == NULL) return CARD_RESULT_IOERROR;
    SDL_SeekIO(io, offset, SDL_IO_SEEK_SET);
    done = write ? SDL_WriteIO(io, buf, (size_t)length) : SDL_ReadIO(io, buf, (size_t)length);
    SDL_CloseIO(io);
    return done == (size_t)length ? CARD_RESULT_READY : CARD_RESULT_IOERROR;
}

s32 CARDRead(struct CARDFileInfo *fileInfo, void *buf, s32 length, s32 offset) {
    return Transfer((CARDFileInfo *)fileInfo, buf, length, offset, 0);
}

s32 CARDWrite(struct CARDFileInfo *fileInfo, const void *buf, s32 length, s32 offset) {
    return Transfer((CARDFileInfo *)fileInfo, (void *)buf, length, offset, 1);
}

s32 CARDGetStatus(s32 chan, s32 fileNo, struct CARDStat *stat) {
    s32 r = CheckChan(chan);
    if (r != CARD_RESULT_READY) return r;
    if (fileNo < 0 || fileNo >= CARD_MAX_FILES || !s_slots[chan].files[fileNo].used) {
        return CARD_RESULT_NOFILE;
    }
    memcpy(stat, &s_slots[chan].files[fileNo].stat, sizeof(CARDStat));
    return CARD_RESULT_READY;
}

s32 CARDSetStatus(s32 chan, s32 fileNo, struct CARDStat *stat) {
    s32 r = CheckChan(chan);
    if (r != CARD_RESULT_READY) return r;
    if (fileNo < 0 || fileNo >= CARD_MAX_FILES || !s_slots[chan].files[fileNo].used) {
        return CARD_RESULT_NOFILE;
    }
    /* Icon/banner metadata only matters to the GameCube IPL; keep it in memory. */
    memcpy(&s_slots[chan].files[fileNo].stat, stat, sizeof(CARDStat));
    return CARD_RESULT_READY;
}
