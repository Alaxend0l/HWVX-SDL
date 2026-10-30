#include "common.h"




MatrixCacheData *g_pMatrixCacheData;

void MatrixCache_Create(void) {
    u8 *buf;
    u32 i;

    g_pMatrixCacheData = Allocate(0x2A720, 0, "");
    buf = (u8 *)g_pMatrixCacheData + sizeof(MatrixCacheData);
    for (i = 0; i < 0x60; i++) {
        g_pMatrixCacheData->small[i].key = (u32)HND(buf);
        g_pMatrixCacheData->small[i].mtx = NULL;
        buf += 0x100;
    }
    for (i = 0; i < 0x80; i++) {
        g_pMatrixCacheData->medium[i].key = (u32)HND(buf);
        g_pMatrixCacheData->medium[i].mtx = NULL;
        buf += 0x400;
    }
    for (i = 0; i < 4; i++) {
        g_pMatrixCacheData->large[i].key = (u32)HND(buf);
        g_pMatrixCacheData->large[i].mtx = NULL;
        buf += 0x1000;
    }
}

void MatrixCache_Init(void) {

}

void MatrixCache_Destroy(void) {
    Deallocate(g_pMatrixCacheData);
    g_pMatrixCacheData = NULL;
}

u32 MatrixCache_Get(u32 size, void *mtx) {
    u32 i;

    if (size > 0x10) {
        for (i = 0; i < 4; i++) {
            if (g_pMatrixCacheData->large[i].mtx == NULL) {
                g_pMatrixCacheData->large[i].mtx = mtx;
                return g_pMatrixCacheData->large[i].key;
            }
        }
    } else if (size > 4) {
        for (i = 0; i < 0x80; i++) {
            if (g_pMatrixCacheData->medium[i].mtx == NULL) {
                g_pMatrixCacheData->medium[i].mtx = mtx;
                return g_pMatrixCacheData->medium[i].key;
            }
        }
    } else {
        for (i = 0; i < 0x60; i++) {
            if (g_pMatrixCacheData->small[i].mtx == NULL) {
                g_pMatrixCacheData->small[i].mtx = mtx;
                return g_pMatrixCacheData->small[i].key;
            }
        }
    }
    return 0;
}

void MatrixCache_Free(u32 key) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (g_pMatrixCacheData->large[i].key == key) {
            g_pMatrixCacheData->large[i].mtx = NULL;
            return;
        }
    }
    for (i = 0; i < 0x80; i++) {
        if (g_pMatrixCacheData->medium[i].key == key) {
            g_pMatrixCacheData->medium[i].mtx = NULL;
            return;
        }
    }
    for (i = 0; i < 0x60; i++) {
        if (g_pMatrixCacheData->small[i].key == key) {
            g_pMatrixCacheData->small[i].mtx = NULL;
            return;
        }
    }
}

