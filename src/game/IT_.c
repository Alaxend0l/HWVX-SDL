#include "common.h"

extern s32 __OSCurrHeap;
extern u32 g_aheap;
extern u32 g_musicVol;

typedef struct ITTrack {
    u8 pad0[0x18];
    u32 vol;
} ITTrack;

typedef struct ITChannel {
    u8 pad0[0x1C];
    u32 vol;
    u8 pad20[0x28 - 0x20];
    u32 gvol;
} ITChannel;

typedef struct ITHeader {
    u8 pad0[0x24];
    u16 smpNum;
} ITHeader;

typedef struct ITSample {
    u8 pad0[0x12];
    u8 flags;
    u8 pad13[0x30 - 0x13];
    u32 length;
    u8 pad34[0x48 - 0x34];
    u32 data;
} ITSample;


s32 CreateIT_(ITHeader *hdr, ITSample **smp) {
    u32 i;
    u32 size;

    i = hdr->smpNum - 1;
    size = smp[i]->data + (smp[i]->length << ((smp[i]->flags & 2) != 0));
    g_aheap = HAllocA_(size);
    if (g_aheap == 0) {
        return 0;
    }
    DCFlushRange((void *)PTR(smp[0]->data), size);
    MemCpyA_(g_aheap, smp[0]->data, size);
    smp[0]->data = 0;
    for (i = 0; i < hdr->smpNum; i++) {
        smp[i]->data += g_aheap;
        if (smp[i]->flags & 2) {
            smp[i]->data >>= 1;
        }
    }
    return 1;
}


void DestroyIT_(void *data) {
    OSFreeToHeap(__OSCurrHeap, data);
    FreeA_(g_aheap);
    g_aheap = 0;
}

u32 GetITGVol_(ITTrack *trk, ITChannel *ch) {
    return ((trk->vol * g_musicVol >> 7) * (ch->vol * ch->gvol >> 6)) >> 15;
}


