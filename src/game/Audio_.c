#include "common.h"



typedef struct MemBlk {
    u32 addr;
    s32 size;
    u32 prev;
    u32 next;
} MemBlk;

u32 g_arqWait;
s32 g_last;
u32 g_free;
u32 g_silence;
s32 g_size;
u32 g_heap;
MemBlk g_memblk[255];


s32 Audio_Create_(void) {
    ARInit(0, 0);
    ARQInit();
    AIInit(0);
    AXInit();
    AISetDSPSampleRate(1);
    AXRegisterCallback(Audio_Update_);
    return 1;
}



s32 Audio_Init_(void) {
    extern s32 g_last;
    extern u32 g_heap;
    extern s32 g_size;
    extern u32 g_silence;
    extern u32 g_free;
    extern s32 __OSCurrHeap;
    u32 i;
    s32 level;
    void *buf;

    ARQFlushQueue();
    g_silence = ARGetBaseAddress();
    g_heap = g_silence + 0x800;
    g_size = ARGetSize() - g_heap;
    g_memblk[0].addr = g_heap;
    g_memblk[0].size = g_size;
    g_memblk[0].prev = -1;
    g_memblk[0].next = -1;
    g_last = 0;
    g_memblk[1].size = 0;
    g_memblk[1].prev = -1;
    g_memblk[1].next = 2;
    g_free = 1;
    for (i = 2; i < 0xFF; i++) {
        g_memblk[i].size = 0;
        g_memblk[i].prev = i - 1;
        g_memblk[i].next = i + 1;
    }
    g_memblk[0xFE].next = -1;
    level = OSDisableInterrupts();
    buf = OSAllocFromHeap(__OSCurrHeap, 0x800);
    OSRestoreInterrupts(level);
    memset(buf, 0, 0x800);
    MemCpyA_(g_silence, (u32)HND(buf), 0x800);
    level = OSDisableInterrupts();
    OSFreeToHeap(__OSCurrHeap, buf);
    OSRestoreInterrupts(level);
    printf("Audio :: Initialized (%ik available RAM)\n", g_size >> 10);
    return 1;
}


void Audio_Destroy_(void) {
    AXQuit();
    AIReset();
    ARQReset();
    ARReset();
}

void Audio_Update_(void) {
    Audio_Update(0xD05);
}


u32 HAllocA_(s32 size) {
    extern s32 g_last;
    extern u32 g_heap;

    size = (size + 0x1F) & ~0x1F;
    if (g_memblk[g_last].size < 0 || g_heap != g_memblk[g_last].addr || g_memblk[g_last].size < size) {
        return 0;
    }
    g_memblk[g_last].addr += size;
    g_memblk[g_last].size -= size;
    return g_heap;
}



u32 MAllocA_(s32 size) {
    extern s32 g_last;
    extern u32 g_free;
    u32 i;
    u32 n;

    i = 0;
    size = (size + 0x1F) & ~0x1F;
    while (i != 0xFFFFFFFF) {
        if (g_memblk[i].size >= size) {
            if (g_memblk[i].size > size) {
                if (g_free == 0xFFFFFFFF) {
                    return 0;
                }
                n = g_free;
                g_free = g_memblk[n].next;
                g_memblk[n].addr = g_memblk[i].addr;
                g_memblk[n].size = g_memblk[i].size - size;
                g_memblk[n].prev = i;
                g_memblk[n].next = g_memblk[i].next;
                if (g_memblk[n].next == 0xFFFFFFFF) {
                    g_last = n;
                }
                g_memblk[i].next = n;
            }
            g_memblk[i].addr += g_memblk[i].size - size;
            g_memblk[i].size = -size;
            return g_memblk[i].addr;
        }
        i = g_memblk[i].next;
    }
    printf("Audio :: MAllocA_ - Insufficient ARAM  size: %i\n", size);
    return 0;
}



s32 FreeA_(u32 addr) {
    extern s32 g_last;
    extern u32 g_heap;
    extern u32 g_free;
    u32 i;
    s32 size;
    u32 next;
    u32 prev;

    i = 0;
    while (i != 0xFFFFFFFF) {
        if (addr == g_memblk[i].addr) {
            size = -g_memblk[i].size;
            g_memblk[i].size = size;
            next = g_memblk[i].next;
            prev = g_memblk[i].prev;
            if (next != 0xFFFFFFFF && g_memblk[next].size > 0) {
                g_memblk[i].addr = g_memblk[next].addr;
                g_memblk[i].size += g_memblk[next].size;
                g_memblk[i].next = g_memblk[next].next;
                g_memblk[g_memblk[i].next].prev = i;
                g_memblk[next].next = g_free;
                g_memblk[next].size = 0;
                g_free = next;
            }
            if (prev != 0xFFFFFFFF && g_memblk[prev].size > 0) {
                g_memblk[prev].addr = g_memblk[i].addr;
                g_memblk[prev].size += g_memblk[i].size;
                g_memblk[prev].next = g_memblk[i].next;
                g_memblk[g_memblk[prev].next].prev = prev;
                g_memblk[i].next = g_free;
                g_memblk[i].size = 0;
                g_free = i;
                i = prev;
            }
            if (g_memblk[i].next == 0xFFFFFFFF) {
                g_last = i;
            }
            return size;
        }
        i = g_memblk[i].next;
    }
    if (g_heap == addr) {
        size = g_memblk[g_last].addr - g_heap;
        g_memblk[g_last].size += size;
        g_memblk[g_last].addr = g_heap;
        return size;
    }
    printf("Audio :: FreeA_ - Invalid pointer: %06X\n", addr);
    return 0;
}


void MemCpyDone(void) {
    extern u32 g_arqWait;
    g_arqWait = 0;
}

u32 MemCpyA_(u32 dst, u32 src, u32 len) {
    extern u32 g_arqWait;
    void PPCSync(void);
    u8 req[0x20];

    g_arqWait = 1;
    if (src & 0x80000000) {
        ARQPostRequest(req, 0, 0, 1, src, dst, len, MemCpyDone);
    } else {
        ARQPostRequest(req, 0, 1, 1, src, dst, len, MemCpyDone);
    }
    while (g_arqWait != 0) {
        PPCSync();
    }
    return dst + len;
}

s32 Mul16_(u32 a, u32 b) {
    u32 hi = __mulhwu(a, b);
    u32 lo = a * b;
    return (lo >> 16) | (hi << 16);
}

