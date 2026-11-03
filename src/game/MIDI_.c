#include "common.h"

typedef struct MIDISample_ {
    u32 addr;                   /* 0x00 start, fixed up by CreateMIDI_ */
    u32 loop;                   /* 0x04 loop start */
    s32 end;                    /* 0x08 end */
    u8 padC[5];                 /* 0x0C */
    u8 bits;                    /* 0x11 0x10: 16-bit, halves the offsets */
    u8 pad12[0xE];              /* 0x12 */
} MIDISample_;

typedef struct MIDIHeader_ {
    u32 unused0;                /* 0x00 */
    u32 size;                   /* 0x04 sample data block size */
    u32 unused8;                /* 0x08 */
    u32 unusedC;                /* 0x0C */
    u32 numSamples;             /* 0x10 */
    u32 sampleData;             /* 0x14 block offset */
    u32 unused18;               /* 0x18 */
    u32 unused1C;               /* 0x1C */
    MIDISample_ samples[1];     /* 0x20 */
} MIDIHeader_;

extern s32 __OSCurrHeap;
extern u32 g_aheap;


s32 CreateMIDI_(MIDIHeader_ *hdr) {
    s32 size;
    u32 i;
    MIDISample_ *smp;

    size = hdr->size - ((u8 *)&hdr->samples[hdr->numSamples] - (u8 *)hdr - 8);
    g_aheap = HAllocA_(size);
    if (g_aheap == 0) {
        return 0;
    }
    DCFlushRange(&hdr->samples[hdr->numSamples], size);
    MemCpyA_(g_aheap, hdr->sampleData, size);
    smp = hdr->samples;
    for (i = 0; i < hdr->numSamples; i++) {
        smp->addr += g_aheap;
        if (smp->bits == 0x10) {
            smp->addr >>= 1;
            smp->loop >>= 1;
            smp->end >>= 1;
        }
        smp++;
    }
    return 1;
}


void DestroyMIDI_(void *data) {
    OSFreeToHeap(__OSCurrHeap, data);
    FreeA_(g_aheap);
}

u32 ReadVar_(u8 **pp) {
    u8 *p = *pp;
    u32 value = 0;
    u32 c;
    u32 b;

    do {
        c = *p++;
        b = c & 0x7F;
        value = (value << 7) | b;
    } while (b != c);
    *pp = p;
    return value;
}

u32 GetPitchWheel_(u8 note, u8 count) {
    extern const u32 g_pitchTab[];
    u8 *pn = &note;
    u8 *pc = &count;
    u32 pitch = g_pitchTab[*pn];
    u32 base;
    s32 i = *pc - 1;

    if (i != 0) {
        base = pitch;
        do {
            pitch = (u32)(((u64)pitch * base) >> 30);
        } while (--i > 0);
    }
    return pitch >> 14;
}

