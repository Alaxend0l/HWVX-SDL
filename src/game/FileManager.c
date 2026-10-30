#include "common.h"


s32 FileManager_Create(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        g_files[i].slot = -1;
    }
    for (i = 0; i < 5; i++) {
        g_appInfo[i][0] = 0;
    }
    return 1;
}

void FileManager_Destroy(void) {
    M2C_UNK fn_800957D4();
    fn_800957D4();
}

void FileManager_Release(void) {
    s16 i;

    for (i = 0; i < 4; i++) {
        if (g_files[i].slot != -1) {
            File_Close((g_files[i].id << 16) | (g_files[i].slot << 8) | i);
        }
    }
}

void FileManager_SetAppInfo(const char *a, const char *b, const char *c, const char *d, const char *e) {

    strcpy(g_appInfo[0], a);
    strcpy(g_appInfo[1], b);
    strcpy(g_appInfo[2], c);
    strcpy(g_appInfo[3], d);
    strcpy(g_appInfo[4], e);
}

s32 FileManager_GetChecksum(u32 size, u8 *data) {
    u32 i;
    s32 sum = 0;

    for (i = 0; i < size; i++) {
        sum += data[i] + 1;
    }
    return sum;
}

