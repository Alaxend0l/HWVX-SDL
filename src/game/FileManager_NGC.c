#include "common.h"



u8 CardWorkArea[0xA000] __attribute__((aligned(32)));


s32 stricmp(const char *s1, const char *s2) {
    char c1, c2;

    while (*s1) {
        c2 = tolower(*(u8 *)s2);
        c1 = tolower(*(u8 *)s1);
        if (c1 != c2) {
            c1 = tolower(*(u8 *)s1);
            c2 = tolower(*(u8 *)s2);
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return *s2 ? -1 : 0;
}

s32 FileManager_GetDeviceLastError(u32 device) {
    extern s32 m_cd_drive_status;
    s32 chan;

    if (device == 11 || device == 12) {
        chan = device - 11;
        if (chan < 0) {
            return 0;
        }
        switch (m_memcardstatus[chan]) {
        case -6:
            return -3;
        case -128:
        case -5:
            return -4;
        case -2:
            return -6;
        case -9:
            return -1;
        case -100:
            return -2;
        case -13:
            return -5;
        }
    }
    if (device == 1) {
        switch (m_cd_drive_status) {
        case -1:
            return -11;
        case 11:
            return -10;
        case 6:
            return -9;
        case 5:
            return -7;
        case 4:
            return -8;
        }
    }
    return 0;
}

s32 FileManager_GetDeviceFreeSpace(s32 device, u32 *freeBytes, u32 *totalBytes) {
    freeBytes[1] = 0x32000;
    freeBytes[0] = 0;
    totalBytes[1] = 0x32000;
    totalBytes[0] = 0;
    return 1;
}

s8 FileManager_IsDeviceFormatted(s32 device) {
    return m_memcardstatus[device - 11] == 0;
}

s8 FileManager_FormatDevice(s32 device) {
    m_memcardstatus[device - 11] = CARDFormat(device - 11);
    return m_memcardstatus[device - 11] == 0 ? 1 : 0;
}
typedef struct { s32 fileInfo; char name[0x100]; } FileDataPC;
extern FileDataPC g_filedata_PC[4];


s32 File_Open(u32 dev, const char *name, u32 flags, s32 *size) {
    char *p;
    s32 i;
    s8 ok;
    char c;
    char copy[0x100];

    /* port: the original lowercases `name` in place; callers pass literals */
    snprintf(copy, sizeof(copy), "%s", name);
    name = copy;
    p = copy;
    strrchr(name, '.');
    while ((c = *p) != 0) {
        *p = tolower((u8)c);
        if (*p == '\\') {
            *p = '/';
        }
        p++;
    }
    for (i = 0; i < 4; i++) {
        if (g_files[i].slot == -1) {
            break;
        }
    }
    if (i == 4) {
        return 0;
    }
    if (dev >= 11) {
        ok = MemCard_Open__FUlPCScUlPl(dev, name, flags, size);
    } else {
        g_filedata_PC[i].fileInfo = (s32)HND(Allocate(0x3C, 0, ""));
        ok = DVDOpen(name, (void *)PTR(g_filedata_PC[i].fileInfo));
    }
    if (!ok) {
        return -1;
    }
    g_files[i].slot = 1;
    g_files[i].position = 0;
    if (dev < 11) {
        g_files[i].size = *(s32 *)PTR((g_filedata_PC[i].fileInfo + 0x34));
    }
    strcpy(g_filedata_PC[i].name, name);
    if (size != NULL) {
        *size = g_files[i].size;
    }
    if (dev >= 11) {
        return (dev << 16) | (g_files[i].slot << 8) | i;
    }
    return (g_files[i].slot << 8) | i;
}

void File_Close(s32 handle) {
    s32 slot = (s8)(u8)((u32)handle >> 8);
    s32 idx = (s8)(u8)handle;
    s32 device = (s8)(u8)((u32)handle >> 16);

    if (device >= 11) {
        MemCard_Close__Fl(device);
        g_files[idx].slot = -1;
        return;
    }
    if (!(idx >= 0 && idx < 4 && slot == g_files[idx].slot)) {
        return;
    }
    DVDClose((void *)PTR(g_filedata_PC[idx].fileInfo));
    Deallocate(PTR(g_filedata_PC[idx].fileInfo));
    g_filedata_PC[idx].fileInfo = 0;
    g_files[idx].slot = -1;
}

s32 File_Seek(s32 handle, s32 offset, s8 mode) {
    s8 idx = (u8)handle;

    if (mode == 0) {
        g_files[idx].position = offset;
    } else {
        g_files[idx].position += offset;
    }
    return 0;
}

s32 File_Read(u32 id, s32 size, void *data) {
    extern s64 g_resetTicks;
    extern s32 m_reset;
    extern s32 m_cd_drive_status;
    extern u32 shithack;
    s8 isInThread(void);
    char msg[0x100];
    s8 device = (u8)(id >> 16);
    s8 idx;
    s32 result;
    s32 length;
    s32 buffer;
    u8 *aligned;

    if (device >= 11) {
        return MemCard_Read__FUllPv(device, size, data);
    }
    if (size == 0) {
        return 1;
    }
    idx = (u8)id;
    if (g_files[idx].position + size <= g_files[idx].size) {
        if (size < 0xC800) {
            length = (size + 0x1F) & ~0x1F;
            buffer = (s32)HND(Allocate(length + 0x40, 0, ""));
            aligned = (u8 *)PTR(((buffer + 0x1F) & ~0x1F));
            if (isInThread()) {
                result = DVDReadPrio((void *)PTR(g_filedata_PC[idx].fileInfo), aligned, length, g_files[idx].position, 2);
            } else {
                result = DVDReadAsyncPrio((void *)PTR(g_filedata_PC[idx].fileInfo), aligned, length, g_files[idx].position, NULL, 2);
                do {
                    m_cd_drive_status = DVDGetDriveStatus();
                    switch (m_cd_drive_status) {
                    case -1:
                    case 4:
                    case 5:
                    case 6:
                    case 11:
                        shithack = 1;
                        Render_BeginScene();
                        Render_EndScene();
                        Render_FlipBuffers();
                        break;
                    }
                    if (OSGetResetButtonState()) {
                        m_reset = 1;
                    }
                    if (shithack && m_reset && OSGetResetButtonState() == 0) {
                        VISetBlack(1);
                        VIWaitForRetrace();
                        VIFlush();
                        VIWaitForRetrace();
                        VIFlush();
                        VIWaitForRetrace();
                        if ((OSGetTime() - g_resetTicks) / OS_TIMER_CLOCK < 8) {
                            OSResetSystem(1, 0, 0);
                        } else {
                            OSResetSystem(0, 0, 0);
                        }
                    }
                } while (m_cd_drive_status != 0);
            }
            memcpy(data, aligned, size);
            Deallocate(PTR(buffer));
            g_files[idx].position += size;
        } else {
            if (isInThread()) {
                result = DVDReadPrio((void *)PTR(g_filedata_PC[idx].fileInfo), data, (size + 0x1F) & ~0x1F, g_files[idx].position, 2);
            } else {
                result = DVDReadAsyncPrio((void *)PTR(g_filedata_PC[idx].fileInfo), data, (size + 0x1F) & ~0x1F, g_files[idx].position, NULL, 2);
                do {
                    m_cd_drive_status = DVDGetDriveStatus();
                    switch (m_cd_drive_status) {
                    case -1:
                    case 4:
                    case 5:
                    case 6:
                    case 11:
                        shithack = 1;
                        Render_BeginScene();
                        Render_EndScene();
                        Render_FlipBuffers();
                        break;
                    }
                    if (OSGetResetButtonState()) {
                        m_reset = 1;
                    }
                    if (shithack && m_reset && OSGetResetButtonState() == 0) {
                        VISetBlack(1);
                        VIWaitForRetrace();
                        VIFlush();
                        VIWaitForRetrace();
                        VIFlush();
                        VIWaitForRetrace();
                        if ((OSGetTime() - g_resetTicks) / OS_TIMER_CLOCK < 8) {
                            OSResetSystem(1, 0, 0);
                        } else {
                            OSResetSystem(0, 0, 0);
                        }
                    }
                } while (m_cd_drive_status != 0);
            }
            g_files[idx].position += size;
        }
    } else {
        sprintf(msg, "File error: Attempting to read past end of file in:\n %s", g_filedata_PC[idx].name);
        return 0;
    }
    return result;
}

s32 File_Write(u32 id, s32 size, void *data) {
    s8 device = (u8)(id >> 16);

    if (device >= 11) {
        return MemCard_Write__FUllPv(device, size, data);
    }
    return 0;
}

s32 File_Delete(s32 device, const char *fileName) {
    s32 chan = device - 11;

    if (CARDMount(chan, CardWorkArea, 0) != 0) {
        return 0;
    }
    if (CARDDelete(chan, fileName) == 0) {
        CARDUnmount(chan);
        return 1;
    }
    CARDUnmount(chan);
    return 0;
}

u32 FileManager_GetDeviceStatus(u32 device) {
    u16 encoding;
    s32 memSize;
    s32 sectorSize;
    s32 bytesNotUsed;
    s32 filesNotUsed;
    CARDFileInfo fileInfo;
    s32 result;

    if (device == 11 || device == 12) {
        result = CARDProbeEx(device - 11, &memSize, &sectorSize);
        if (result != 0) {
            m_memcardstatus[device - 11] = result;
            return 3;
        }
        if (sectorSize != 0x2000) {
            m_memcardstatus[device - 11] = -100;
            return 3;
        }
        result = CARDMount(device - 11, CardWorkArea, 0);
        if (result == -6) {
            if (m_memcardstatus[device - 11] != -5) {
                result = CARDCheck(device - 11);
            } else {
                result = m_memcardstatus[device - 11];
            }
        }
        if ((m_memcardstatus[device - 11] = result) != 0) {
            return 3;
        }
        CARDGetEncoding(device - 11, &encoding);
        if (encoding != 0) {
            m_memcardstatus[device - 11] = -13;
            CARDUnmount(device - 11);
            return 3;
        }
        CARDFreeBlocks(device - 11, &bytesNotUsed, &filesNotUsed);
        result = CARDOpen(device - 11, g_appInfo[3], &fileInfo);
        if (result == 0) {
            CARDClose(&fileInfo);
        }
        CARDUnmount(device - 11);
        if (result == -4 && (bytesNotUsed == 0 || filesNotUsed == 0)) {
            m_memcardstatus[device - 11] = -9;
            return 3;
        }
        return 1;
    }
    if (device == 0) {
        return 1;
    }
    return device == 1;
}

s32 MemCard_Open__FUlPCScUlPl(u32 device, const char *fileName, u32 flags, s32 *unused) {
    u32 chan;
    CARDFileInfo *fileInfo;
    s32 *blockSize;

    if (FileManager_GetDeviceStatus(device) == 3) {
        return 0;
    }
    chan = device - 11;
    m_memcardstatus[chan] = CARDMount(chan, CardWorkArea, 0);
    OSReport("CARDMount\n");
    if (m_memcardstatus[chan] < 0) {
        OSReport("failed. (%d)\n", m_memcardstatus[chan]);
        return 0;
    }
    blockSize = &memCardBlockSize[chan];
    CARDGetSectorSize(chan, blockSize);
    fileInfo = &memCardfileInfo[chan];
    m_memcardstatus[chan] = CARDOpen(chan, fileName, fileInfo);
    if (m_memcardstatus[chan] == 0) {
        return 1;
    }
    if (m_memcardstatus[chan] == -4 && (flags & 1)) {
        m_memcardstatus[chan] = CARDCreate(chan, fileName, *blockSize, fileInfo);
        if (m_memcardstatus[chan] < 0) {
            m_memcardstatus[chan] = CARDUnmount(chan);
            OSReport("failed. (%d)\n", m_memcardstatus[chan]);
            return 0;
        }
        OSReport("CARDCreate\n");
    } else {
        m_memcardstatus[chan] = CARDUnmount(chan);
        return 0;
    }
    return 1;
}

void MemCard_Close__Fl(s32 device) {
    s32 chan = device - 11;

    m_memcardstatus[chan] = CARDClose(&memCardfileInfo[chan]);
    m_memcardstatus[chan] = CARDUnmount(chan);
    OSReport("CARDClose\n");
}

s32 MemCard_Read__FUllPv(u32 device, s32 size, void *data) {
    u8 buffer[0x2000] __attribute__((aligned(32)));
    s32 chan = device - 11;

    m_memcardstatus[chan] = CARDRead(&memCardfileInfo[chan], buffer, memCardBlockSize[chan], 0);
    memcpy(data, buffer + 0x840, size);
    return size;
}

s32 MemCard_Write__FUllPv(u32 id, s32 size, void *data) {
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
    extern u8 rawData[0x800];
    CARDStat stat;
    u8 buffer[0x2000] __attribute__((aligned(32)));
    s32 chan = id - 11;

    memset(buffer, 0, 0x40);
    sprintf((char *)buffer, "Hot Wheels Velocity X Save");
    sprintf((char *)buffer + 0x20, "Game Data");
    memcpy(buffer + 0x40, rawData, 0x800);
    memcpy(buffer + 0x840, data, size);
    m_memcardstatus[chan] = CARDWrite(&memCardfileInfo[chan], buffer, memCardBlockSize[chan], 0);
    if (m_memcardstatus[chan] < 0) {
        if (m_memcardstatus[chan] == -5) {
            CARDDelete(chan, g_appInfo[3]);
        }
        m_memcardstatus[chan] = CARDUnmount(chan);
        OSReport("failed. (%d)\n", m_memcardstatus[chan]);
        return 0;
    }
    OSReport("CARDWrite\n");
    m_memcardstatus[chan] = CARDGetStatus(chan, memCardfileInfo[chan].fileNo, &stat);
    stat.commentAddr = 0;
    stat.iconAddr = 0x40;
    stat.bannerFormat = (u8)((stat.bannerFormat & ~3) | 0);
    stat.bannerFormat = (u8)((stat.bannerFormat & ~4) | 0);
    stat.iconFormat = (u16)((stat.iconFormat & ~3) | 2);
    stat.iconSpeed = (u16)((stat.iconSpeed & ~3) | 3);
    stat.iconSpeed = (u16)((stat.iconSpeed & ~(3 << 2)) | (0 << 2));
    m_memcardstatus[chan] = CARDSetStatus(chan, memCardfileInfo[chan].fileNo, &stat);
    return 1;
}

void LidHandler_Init(void) {
    extern u32 m_isopen;
    extern u32 m_curtime;
    extern u8 m_fileInfo[0x3C];

    if (!m_isopen) {
        DVDOpen("bsfile", m_fileInfo);
        m_isopen = 1;
        m_curtime = 0;
    }
}

void LidHandler_Heartbeat(void) {
    extern u8 cd_test_block[0x20];
    extern s32 g_iDeltaTimeMS;
    extern u32 m_curtime;
    extern u8 m_fileInfo[0x3C];

    m_curtime += g_iDeltaTimeMS;
    if (m_curtime > 500) {
        if (DVDGetCommandBlockStatus(m_fileInfo) == 0) {
            DVDReadAsyncPrio(m_fileInfo, cd_test_block, 0x20, 0, NULL, 2);
            m_curtime = 0;
        }
    }
}

