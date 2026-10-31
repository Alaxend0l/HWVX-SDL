#include "common.h"
#include "IT.h"

extern s32 __OSCurrHeap;
extern u32 g_aheap;
extern u32 g_musicVol;


s32 CreateIT_(ITHeader *hdr, ITSample **smp) {
    u32 i;
    u32 size;

    /* the samples are stored in order: copy from the first to the end of the last */
    i = hdr->numSamples - 1;
    size = smp[i]->data + (smp[i]->length << ((smp[i]->flags & IT_SAMPLE_16BIT) != 0));
    g_aheap = HAllocA_(size);
    if (g_aheap == 0) {
        return 0;
    }
    DCFlushRange((void *)PTR(smp[0]->data), size);
    MemCpyA_(g_aheap, smp[0]->data, size);
    smp[0]->data = 0;
    for (i = 0; i < hdr->numSamples; i++) {
        smp[i]->data += g_aheap;
        if (smp[i]->flags & IT_SAMPLE_16BIT) {
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

/* A track's volume: song global volume x music volume x channel volume x
 * track volume. */
u32 GetITGVol_(ITSong *song, ITTrack *trk) {
    return (((u32)song->globalVol * g_musicVol >> 7) * ((u32)trk->chVol * trk->vol >> 6)) >> 15;
}


