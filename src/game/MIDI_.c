#include "common.h"

typedef struct MIDISample_ {
    u32 addr;
    u32 loop;
    s32 end;
    u8 unkC[5];
    u8 bits;
    u8 unk12[0xE];
} MIDISample_;

typedef struct MIDIHeader_ {
    u32 unk0;
    u32 size;
    u32 unk8;
    u32 unkC;
    u32 numSamples;
    u32 sampleData;
    u32 unk18;
    u32 unk1C;
    MIDISample_ samples[1];
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

