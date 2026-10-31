/* Hardware voices (g_voice): the 32 slots that play samples through AX, shared
 * by the sound effects (Voice.c), the MIDI player (MIDI.c) and the IT module
 * player (IT.c). Voice_.c turns their fields into AXVPB settings. Offsets are
 * the GameCube layout; the asserts keep them that way. */
#ifndef VOICE_H
#define VOICE_H

#include <stddef.h>

#define NUM_VOICES 0x20

/* Voice.kind */
enum {
    VOICE_KIND_SONG = 0,        /* MIDI / IT: reverb on aux A; DestroySongVoice when it ends */
    VOICE_KIND_SFX = 1,         /* sound effect: dry */
};

/* Voice.state */
enum {
    VOICE_STATE_FREE = 0,
    VOICE_STATE_PLAYING = 1,
    VOICE_STATE_PAUSED = 2,
    VOICE_STATE_STOPPED = 3,    /* SetVoiceState_(3) frees it */
};

/* Voice.owner of a voice not in use */
#define VOICE_OWNER_NONE 0xFF   /* never used */
#define VOICE_OWNER_FREED 0xFE  /* stopped by the game, or by the DSP */
#define VOICE_OWNER_STOLEN 0xFD /* taken back by AX for a higher priority voice */

/* A sample as Voice_.c plays it: a sound effect (g_sound), also the start of
 * a MIDI instrument. CreateSFX_ copies it to ARAM. */
typedef struct SFX {
    u32 data;                   /* 0x00 address, in ARAM once created (CreateSFX_) */
    u32 length;                 /* 0x04 samples */
    u32 loop;                   /* 0x08 loop start, ~0: one-shot */
    u32 rate;                   /* 0x0C Hz */
    u8 unk10;                   /* 0x10 */
    u8 bits;                    /* 0x11 8, 16, else ADPCM */
    u16 vol;                    /* 0x12 */
    u8 pad14[0xC];              /* 0x14 */
} SFX;

typedef struct Voice {
    union {
        u32 id;                 /* 0x00 read big-endian: see VOICE_ID (Voice.c) */
        struct {
            u8 kind;            /* 0x00 VOICE_KIND_* */
            u8 owner;           /* 0x01 sound effect index, MIDI channel or IT track;
                                 *      VOICE_OWNER_* once free */
            union {
                u8 serial;      /* 0x02 sound effects: tells reuses of the slot apart */
                u8 note;        /*      MIDI */
            };
            u8 index;           /* 0x03 slot in g_voice */
        };
    };
    PTR32(struct Voice) next;   /* 0x04 the free list, or a channel's / track's voices */
    PTR32(struct Voice) unk08;  /* 0x08 never set; SetITTrackPriority follows it (sic) */
    u8 pad0C[4];                /* 0x0C */
    union {
        PTR32(SFX) sfx;         /* 0x10 sound effects */
        PTR32(struct MIDI_INST) inst; /* MIDI */
        PTR32(struct ITSample) itSample; /* IT */
    };
    u8 priority;                /* 0x14 AX voice priority */
    u8 state;                   /* 0x15 VOICE_STATE_* */
    u8 envStage;                /* 0x16 MIDI envelope: 1 attack, 2 decay, 3 sustain, 4 release */
    s8 released;                /* 0x17 MIDI: note off seen while held by the sustain pedal */
    u32 envRate;                /* 0x18 MIDI envelope step */
    u32 envCount;               /* 0x1C 16.16 through the stage */
    u32 addr;                   /* 0x20 ARAM sample address */
    u32 loop;                   /* 0x24 loop start from addr, ~0: one-shot */
    u32 length;                 /* 0x28 samples */
    u8 bits;                    /* 0x2C 8, 16, else ADPCM */
    u8 pad2D[3];                /* 0x2D */
    u32 rate;                   /* 0x30 sample rate */
    u32 pitch;                  /* 0x34 16.16 factor */
    s32 vol;                    /* 0x38 0-0x7FFF, applied as the AX envelope (AXVPB.ve) by
                                 * ProcessVoice_; MIDI: the envelope level */
    s32 mixVol;                 /* 0x3C 0-0x7FFF, scales the speaker mix (SetVoiceVol_):
                                 * SFX.vol, the MIDI velocity or the IT volume */
    s32 pan;                    /* 0x40 -0x8000 (left) .. 0x7FFF (right); sound effects: x */
    s32 span;                   /* 0x44 front/back; sound effects: y */
    s32 auxVol;                 /* 0x48 reverb send, 0-0x7FFF */
    u8 pad4C[4];                /* 0x4C */
    PTR32(AXVPB) axv;           /* 0x50 */
    u8 fresh;                   /* 0x54 not started yet: StartVoice_ clears the resampler */
    u8 unk55;                   /* 0x55 */
    u8 volRamp;                 /* 0x56 the speaker volumes are ramping (ProcessVoice_ ends it) */
    u8 auxRamp;                 /* 0x57 the reverb send is ramping */
    u8 pad58[2];                /* 0x58 */
    s16 curEnvVol;              /* 0x5A envelope set on the AXVPB, < 0: none yet */
    u8 pad5C[4];                /* 0x5C */
    s16 volL, volR;             /* 0x60 speaker volumes being ramped to */
    s16 curVolL, curVolR;       /* 0x64 set on the AXVPB, < 0: none yet */
    s16 auxL, auxR;             /* 0x68 reverb sends being ramped to */
    s16 curAuxL, curAuxR;       /* 0x6C */
} Voice;

_Static_assert(offsetof(SFX, vol) == 0x12, "SFX.vol");
_Static_assert(sizeof(SFX) == 0x20, "SFX size");
_Static_assert(offsetof(Voice, sfx) == 0x10, "Voice.sfx");
_Static_assert(offsetof(Voice, state) == 0x15, "Voice.state");
_Static_assert(offsetof(Voice, addr) == 0x20, "Voice.addr");
_Static_assert(offsetof(Voice, rate) == 0x30, "Voice.rate");
_Static_assert(offsetof(Voice, pan) == 0x40, "Voice.pan");
_Static_assert(offsetof(Voice, axv) == 0x50, "Voice.axv");
_Static_assert(offsetof(Voice, curEnvVol) == 0x5A, "Voice.curEnvVol");
_Static_assert(offsetof(Voice, curAuxL) == 0x6C, "Voice.curAuxL");
_Static_assert(sizeof(Voice) == 0x70, "Voice size");

extern Voice g_voice[NUM_VOICES];

#endif
