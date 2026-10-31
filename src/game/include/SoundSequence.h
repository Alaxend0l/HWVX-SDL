/* Sound sequences (SoundSequence.c): things (THING_TYPE_SOUND_SEQ) that play a
 * timed track of sounds at a position, read by ExtractSoundKey. Offsets are
 * the GameCube layout; the asserts keep them that way. */
#ifndef SOUNDSEQUENCE_H
#define SOUNDSEQUENCE_H

#include <stddef.h>
#include "Thing.h"
#include "KeyState.h"

/* SoundSeqSound.type */
enum {
    SOUND_TYPE_3D = 0,          /* a sound source with a falloff range */
    SOUND_TYPE_2D = 1,          /* a plain sample (SoundAudioSample_Play) */
};

/* A sound a key can play. */
typedef struct SoundSeqSound {
    u8 type;                    /* 0x00 SOUND_TYPE_*, else a sound source without range */
    u8 pad01[2];                /* 0x01 */
    u8 owner;                   /* 0x03 SoundManager_CreateSource; nonzero: the sequence
                                 * keeps the source (ExtractSoundKey returns it) */
    u8 pad04[4];                /* 0x04 */
    f32 volume;                 /* 0x08 0-1 */
    f32 pitch;                  /* 0x0C */
    f32 pitchRange;             /* 0x10 RandomAdd */
    u32 sample;                 /* 0x14 sound effect id */
    PTR32(f32) range;           /* 0x18 near, far (SOUND_TYPE_3D) */
} SoundSeqSound;

/* A key: after `duration` it plays one of its sounds at random. */
typedef struct SoundSeqKey {
    f32 duration;               /* 0x00 ms */
    u8 loopStart;               /* 0x04 the track loops back to the first such key */
    u8 pad05[3];                /* 0x05 */
    u32 numSounds;              /* 0x08 */
    PTR32(PTR32(SoundSeqSound)) sounds; /* 0x0C */
} SoundSeqKey;

/* ExtractSoundKey state. */
typedef struct SoundSeqKeyState {
    s32 unk0;                   /* 0x00 cleared every call */
    u16 index;                  /* 0x04 next key */
    u16 current;                /* 0x06 last key played */
    f32 time;                   /* 0x08 ms into the key */
    u8 reset;                   /* 0x0C restart the track */
    u8 pad0D[3];                /* 0x0D */
} SoundSeqKeyState;

/* A sequence's description in the data pack (SoundSequence_Play). */
typedef struct SoundSeqInfo {
    u32 duration;               /* 0x00 ms the thing lives, 0: until the track ends */
    u8 allPlayers;              /* 0x04 also played in split screen */
    u8 maxInstances;            /* 0x05 more at once are made with no time, 0: no limit */
    u16 durationRange;          /* 0x06 RandomAdd on duration */
    KeyTrack track;             /* 0x08 SoundSeqKey */
} SoundSeqInfo;

/* SoundSequence flags (Thing.flags), besides the THING_FLAG_* ones. */
enum {
    SOUNDSEQ_FLAG_FOLLOW = 0x10,        /* keep moving its sources to pos */
    SOUNDSEQ_FLAG_DONE = 0x20,          /* the track ended: released once silent */
    SOUNDSEQ_FLAG_LOOPING = 0x4000,     /* holds a looping source */
    SOUNDSEQ_FLAG_RUNS_FROZEN = 0x100000, /* made while the world was frozen: still runs
                                         * (ThingManager_MoveLookerOnly) */
    SOUNDSEQ_FLAG_ENDLESS = 0x10000000, /* duration 0: no timer */
    SOUNDSEQ_FLAG_QUEUED = 0x40000000,  /* SoundSequence_PlayQueued */
    SOUNDSEQ_FLAG_WAITING = 0x80000000, /* queued behind another: not running yet */
};

#define SOUNDSEQ_MAX_SOURCES 4

typedef struct SoundSequence {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    PTR32(SoundSeqInfo) info;   /* 0xB0 */
    SoundSeqKeyState state;     /* 0xB4 */
    s32 timer;                  /* 0xC4 ms left, < 0: release */
    PTR32(struct SoundSequence) queued; /* 0xC8 the next in the queue */
    f32 rate;                   /* 0xCC track playback speed */
    s32 sources[SOUNDSEQ_MAX_SOURCES]; /* 0xD0 kept sound sources, -1: none */
} SoundSequence;

_Static_assert(offsetof(SoundSeqSound, range) == 0x18, "SoundSeqSound.range");
_Static_assert(sizeof(SoundSeqKey) == 0x10, "SoundSeqKey size");
_Static_assert(sizeof(SoundSeqKeyState) == 0x10, "SoundSeqKeyState size");
_Static_assert(offsetof(SoundSeqInfo, track) == 0x08, "SoundSeqInfo.track");
_Static_assert(offsetof(SoundSequence, info) == 0xB0, "SoundSequence.info");
_Static_assert(offsetof(SoundSequence, state) == 0xB4, "SoundSequence.state");
_Static_assert(offsetof(SoundSequence, queued) == 0xC8, "SoundSequence.queued");
_Static_assert(offsetof(SoundSequence, sources) == 0xD0, "SoundSequence.sources");
_Static_assert(sizeof(SoundSequence) == 0xE0, "SoundSequence size");

#endif
