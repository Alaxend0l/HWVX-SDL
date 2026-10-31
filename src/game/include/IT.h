/* The Impulse Tracker module player (IT.c, IT_.c). Modules are kept in the
 * IT file layout ("IMPM"), with byte order fixed at load; the player plays
 * one at a time (g_pHdr). Offsets are the GameCube layout; the asserts keep
 * them that way. */
#ifndef IT_H
#define IT_H

#include <stddef.h>

#define IT_NUM_TRACKS 0x20
#define IT_ORDER_END 0xFF       /* wraps to the first order */

/* ITHeader.flags */
#define IT_FLAG_INSTRUMENTS 0x4 /* notes go through instruments (else straight to samples) */

/* ITSample.flags */
#define IT_SAMPLE_16BIT 0x2
#define IT_SAMPLE_LOOP 0x10

/* The module header. The message fields of the IT format are reused. */
typedef struct ITHeader {
    char magic[4];              /* 0x00 "IMPM" */
    char title[26];             /* 0x04 */
    u16 highlight;              /* 0x1E */
    u16 numOrders;              /* 0x20 */
    u16 numInstruments;         /* 0x22 */
    u16 numSamples;             /* 0x24 */
    u16 numPatterns;            /* 0x26 */
    u16 cwt;                    /* 0x28 */
    u16 cmwt;                   /* 0x2A */
    u16 flags;                  /* 0x2C IT_FLAG_* */
    u16 special;                /* 0x2E */
    u8 globalVol;               /* 0x30 0-0x80 */
    u8 mixVol;                  /* 0x31 */
    u8 speed;                   /* 0x32 initial ticks per row */
    u8 tempo;                   /* 0x33 initial BPM */
    u8 sep;                     /* 0x34 */
    u8 pwd;                     /* 0x35 */
    u16 msgLength;              /* 0x36 */
    u32 size;                   /* 0x38 the whole module (IT: message offset) */
    u32 loaded;                 /* 0x3C set by CreateIT */
    s8 chnPan[64];              /* 0x40 0-64, 100: surround; bit 7: channel off */
    u8 chnVol[64];              /* 0x80 0-64 */
    u8 orders[];                /* 0xC0 then the instrument, sample and pattern offsets */
} ITHeader;

/* A sample header ("IMPS"). */
typedef struct ITSample {
    char magic[4];              /* 0x00 "IMPS" */
    char fileName[12];          /* 0x04 */
    u8 pad10;                   /* 0x10 */
    u8 globalVol;               /* 0x11 0-64 */
    u8 flags;                   /* 0x12 IT_SAMPLE_* */
    u8 vol;                     /* 0x13 default volume, 0-64 */
    char name[26];              /* 0x14 */
    u8 cvt;                     /* 0x2E */
    u8 defaultPan;              /* 0x2F */
    u32 length;                 /* 0x30 samples */
    u32 loopBegin;              /* 0x34 */
    u32 loopEnd;                /* 0x38 */
    u32 c5Speed;                /* 0x3C Hz at middle C */
    u32 susLoopBegin;           /* 0x40 */
    u32 susLoopEnd;             /* 0x44 */
    u32 data;                   /* 0x48 file offset; ARAM address once loaded (CreateIT_) */
    u8 vibrato[4];              /* 0x4C */
} ITSample;

/* An instrument header ("IMPI"); only its note map is used. */
typedef struct ITInstrument {
    u8 pad00[0x40];             /* 0x00 */
    u8 keyboard[120][2];        /* 0x40 per note: the note to play, the sample (1-based) */
} ITInstrument;

/* The playing module (g_song). */
typedef struct ITSong {
    u32 tickStep;               /* 0x00 16.16 ticks per AX frame */
    u32 tickFrac;               /* 0x04 */
    u32 tick;                   /* 0x08 in the current row */
    u8 pad0C;                   /* 0x0C */
    u8 breakRow;                /* 0x0D effect C */
    u8 panSlide;                /* 0x0E effect X */
    u8 flags;                   /* 0x0F 8: pattern break */
    s32 rowsLeft;               /* 0x10 in the current pattern */
    u8 order;                   /* 0x14 next index into ITHeader.orders */
    s8 nextOrder;               /* 0x15 effect B / SetITOrder, -1: none */
    u8 pad16[2];                /* 0x16 */
    s32 globalVol;              /* 0x18 0-0x80 */
    s32 pitch;                  /* 0x1C 16.16, applied to every voice */
    u32 speed;                  /* 0x20 ticks per row */
    u32 tempo;                  /* 0x24 BPM */
    u32 tempoScale;             /* 0x28 16.16 (SetITTempo) */
    s32 tempoSlide;             /* 0x2C effect T */
} ITSong;

/* A pattern channel. */
typedef struct ITTrack {
    PTR32(struct Voice) head;   /* 0x00 its voices, the active one first */
    PTR32(struct Voice) active; /* 0x04 the voice of the current note */
    PTR32(ITSample) smp;        /* 0x08 */
    PTR32(ITInstrument) ins;    /* 0x0C */
    u8 index;                   /* 0x10 */
    u8 state;                   /* 0x11 */
    u8 priority;                /* 0x12 */
    u8 mask;                    /* 0x13 */
    u8 note;                    /* 0x14 */
    u8 insNum;                  /* 0x15 */
    u8 volCol;                  /* 0x16 */
    u8 cmd;                     /* 0x17 */
    u8 param;                   /* 0x18 */
    u8 realNote;                /* 0x19 */
    s16 gvol;                   /* 0x1A GetITGVol_ */
    s32 chVol;                  /* 0x1C */
    u8 pad20[8];                /* 0x20 */
    s32 vol;                    /* 0x28 */
    s32 pan;                    /* 0x2C */
    u8 pad30[8];                /* 0x30 */
    s32 panSep;                 /* 0x38 */
    u8 pad3C[8];                /* 0x3C */
    s32 curVol;                 /* 0x44 */
    s32 volBase;                /* 0x48 */
    s32 volSlide;               /* 0x4C */
    s32 period;                 /* 0x50 */
    s32 slideSpeed;             /* 0x54 */
    s32 targetPeriod;           /* 0x58 */
    u8 memVolSlide;             /* 0x5C effect memories */
    u8 memChVolSlide;           /* 0x5D */
    u8 memPanSlide;             /* 0x5E */
    u8 memPitchSlide;           /* 0x5F */
    u8 memArp;                  /* 0x60 */
    u8 memVibrato;              /* 0x61 */
    u8 pad62;                   /* 0x62 */
    u8 memTremor;               /* 0x63 */
    u8 memOffset;               /* 0x64 */
    u8 memTempo;                /* 0x65 */
    u8 memSpecial;              /* 0x66 */
    u8 pad67;                   /* 0x67 */
    u32 flags;                  /* 0x68 */
    u32 prevFlags;              /* 0x6C */
} ITTrack;

_Static_assert(offsetof(ITHeader, numOrders) == 0x20, "ITHeader.numOrders");
_Static_assert(offsetof(ITHeader, flags) == 0x2C, "ITHeader.flags");
_Static_assert(offsetof(ITHeader, size) == 0x38, "ITHeader.size");
_Static_assert(offsetof(ITHeader, chnPan) == 0x40, "ITHeader.chnPan");
_Static_assert(offsetof(ITHeader, orders) == 0xC0, "ITHeader.orders");
_Static_assert(offsetof(ITSample, vol) == 0x13, "ITSample.vol");
_Static_assert(offsetof(ITSample, length) == 0x30, "ITSample.length");
_Static_assert(offsetof(ITSample, c5Speed) == 0x3C, "ITSample.c5Speed");
_Static_assert(offsetof(ITSample, data) == 0x48, "ITSample.data");
_Static_assert(sizeof(ITSample) == 0x50, "ITSample size");
_Static_assert(offsetof(ITInstrument, keyboard) == 0x40, "ITInstrument.keyboard");
_Static_assert(offsetof(ITSong, rowsLeft) == 0x10, "ITSong.rowsLeft");
_Static_assert(offsetof(ITSong, globalVol) == 0x18, "ITSong.globalVol");
_Static_assert(offsetof(ITSong, tempoSlide) == 0x2C, "ITSong.tempoSlide");
_Static_assert(sizeof(ITSong) == 0x30, "ITSong size");
_Static_assert(offsetof(ITTrack, chVol) == 0x1C, "ITTrack.chVol");
_Static_assert(offsetof(ITTrack, vol) == 0x28, "ITTrack.vol");
_Static_assert(offsetof(ITTrack, curVol) == 0x44, "ITTrack.curVol");
_Static_assert(offsetof(ITTrack, flags) == 0x68, "ITTrack.flags");
_Static_assert(sizeof(ITTrack) == 0x70, "ITTrack size");

#endif
