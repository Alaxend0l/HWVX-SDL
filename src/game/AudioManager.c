#include "common.h"
#include "World.h"

/* A 3D sound source (SoundManager_CreateSource): plays its sample on a voice
 * while a listener is within farDist, at full volume within nearDist. */
typedef struct SoundSource {
    Vector4 pos;                /* 0x00 */
    Vector4 vel;                /* 0x10 */
    f32 pitch;                  /* 0x20 SoundSource_Set 1 */
    f32 volume;                 /* 0x24 SoundSource_Set 0, 0: off */
    s32 sample;                 /* 0x28 sound effect id (SoundSource_Set 8) */
    s32 voice;                  /* 0x2C -1: none */
    u32 looping;                /* 0x30 kept by its owner (SoundManager_CreateSource) */
    f32 nearDist;               /* 0x34 SoundSource_Set 2 */
    f32 farDist;                /* 0x38 SoundSource_Set 3 */
    f32 curPitch;               /* 0x3C set on the voice */
    f32 curVol;                 /* 0x40 */
    u32 curPan;                 /* 0x44 */
    s8 paused;                  /* 0x48 silenced while the game is paused */
    s8 playing;                 /* 0x49 SoundSource_Play: start when in range */
    u8 pad4A[6];                /* 0x4A */
} SoundSource;

typedef struct SoundListener {
    Vector4 pos;                /* 0x00 */
    Vector4 vel;                /* 0x10 */
    Vector4 fwd;                /* 0x20 */
    Vector4 up;                 /* 0x30 */
    Vector4 right;              /* 0x40 */
    Matrix4 mtx;                /* 0x50 world to listener */
} SoundListener;

/* A music setting fading to a target: volume, pitch and tempo. */
typedef struct MusicFade {
    f32 cur;                    /* 0x00 */
    f32 target;                 /* 0x04 */
    f32 time;                   /* 0x08 ms left */
} MusicFade;

typedef struct MusicTrack {
    u32 active;                 /* 0x00 */
    f32 vol;                    /* 0x04 */
    f32 target;                 /* 0x08 */
    f32 time;                   /* 0x0C */
    u8 pad10[0x18];             /* 0x10 */
} MusicTrack;

_Static_assert(offsetof(SoundSource, voice) == 0x2C, "SoundSource.voice");
_Static_assert(offsetof(SoundSource, playing) == 0x49, "SoundSource.playing");
_Static_assert(sizeof(SoundSource) == 0x50, "SoundSource size");
_Static_assert(sizeof(MusicTrack) == 0x28, "MusicTrack size");

#define NUM_SOURCES 100

extern SoundSource *g_sources;  /* NUM_SOURCES */
extern MusicTrack *g_tracks;    /* 0x20 */
extern s32 g_nextSource;
extern SoundListener g_listener[];
extern MusicFade g_globalMusic[3];  /* volume, pitch, tempo */

void SoundManager_Create(void) {
    extern s8 g_songLoaded;

    Dprintf("SoundManager_Create()");
    if (!Audio_Create()) {
        printf("AudioManager :: Audio_Create Failed\n");
    }
    Dprintf("SoundManager_Create() 1");
    g_songLoaded = 0;
    g_sources = Allocate(NUM_SOURCES * sizeof(SoundSource), 0, "");
    g_tracks = Allocate(0x20 * sizeof(MusicTrack), 0, "");
}


void SoundManager_Init(void) {
    extern s8 g_paused;
    extern s32 g_numListeners;
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    Vector4 fwd __attribute__((aligned(16))) = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector4 up __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 0.0f};
    Vector4 right __attribute__((aligned(16))) = {0.0f, 0.0f, 1.0f, 0.0f};
    u32 i;

    Audio_Init();
    memset(g_globalMusic, 0, 3 * sizeof(MusicFade));  /* port: 0x24 bytes (asm); g_globalMusic is a pointer view here */
    g_globalMusic[0].cur = 1.0f;
    g_listener[0].pos = g_listener[0].vel = g_zeroVector;
    g_listener[0].fwd = fwd;
    g_listener[0].up = up;
    g_listener[0].right = right;
    g_listener[0].mtx = g_identity;
    g_numListeners = 0;
    memset(g_sources, 0, 100 * sizeof(SoundSource));
    for (i = 0; i < 100; i++) {
        g_sources[i].voice = -1;
        g_sources[i].volume = 0.0f;
    }
    g_nextSource = 0;
    memset(g_tracks, 0, 32 * sizeof(MusicTrack));
    for (i = 0; i < 32; i++) {
        g_tracks[i].vol = 1.0f;
        g_tracks[i].active = 1;
    }
    g_paused = 0;
}


void SoundManager_Destroy(void) {
    Audio_Destroy();
    Deallocate(g_sources);
    Deallocate(g_tracks);
}

/* port: cases as in the original's jump table (the decomp had them shifted,
 * so a source's sample and volume were never set and cars made no sound) */
void SoundSource_Set(u32 index, u32 type, void *data) {
    SoundSource *src = &g_sources[index];

    switch (type) {
    case 0:  /* volume */
        memcpy(&src->volume, data, 4);
        break;
    case 1:  /* pitch */
        memcpy(&src->pitch, data, 4);
        break;
    case 2:  /* full-volume distance */
        memcpy(&src->nearDist, data, 4);
        break;
    case 3:  /* audible distance */
        memcpy(&src->farDist, data, 4);
        break;
    case 5:  /* position */
        memcpy(&src->pos, data, 0x10);
        break;
    case 6:  /* velocity */
        memcpy(&src->vel, data, 0x10);
        break;
    case 8:  /* sample (sound effect id) */
        memcpy(&src->sample, data, 4);
        break;
    }
}

s32 SoundManager_CreateSource(u32 owner) {
    extern Vector4 g_zeroVector;
    u32 start;

    start = g_nextSource;
    while (g_nextSource >= 100 || g_sources[g_nextSource].looping != 0) {
        if (++g_nextSource >= 100) {
            g_nextSource = 0;
        }
        if (g_nextSource == start) {
            return -1;
        }
    }
    g_sources[g_nextSource].looping = owner;
    g_sources[g_nextSource].pos = g_sources[g_nextSource].vel = g_zeroVector;
    g_sources[g_nextSource].voice = -1;
    g_sources[g_nextSource].sample = -1;
    g_sources[g_nextSource].pitch = 1.0f;
    g_sources[g_nextSource].volume = 0.0f;
    g_sources[g_nextSource].nearDist = 100.0f;
    g_sources[g_nextSource].farDist = 500.0f;
    g_sources[g_nextSource].paused = 0;
    g_sources[g_nextSource].playing = 0;
    return ++g_nextSource - 1;
}

void SoundManager_Heartbeat(void) {
    s32 temp_r31;

    temp_r31 = OSDisableInterrupts();
    SFX_Heartbeat();
    MusicHearbeat();
    OSRestoreInterrupts(temp_r31);
}

void SFX_ProcessSource(u32 index) {
    extern s8 g_paused;
    extern s32 g_numListeners;
    extern s32 g_sscount;
    extern Vector4 g_zeroVector;
    SoundSource *src;
    s32 pan;
    u32 i;
    SoundListener *listener;
    f32 minDist;
    f32 vol;
    f32 pitch;
    f32 dot;
    Vector4 local __attribute__((aligned(16)));
    Vector4 diff __attribute__((aligned(16)));
    f32 dist;
    s32 p;

    src = &g_sources[index];
    if (g_paused && src->paused && src->volume > 0.0f) {
        if (src->voice > 0) {
            SetVoiceVol(src->voice, 0);
        }
        src->curVol = 0.0f;
        return;
    }
    if (src->volume > 0.0f) {
        minDist = 99999.9f;
        for (i = 0; i <= g_numListeners; i++) {
            DistanceXYZ(&g_listener[i], src, &dist);
            if (minDist > dist) {
                minDist = dist;
            }
        }
        if (minDist < src->farDist) {
            if (src->voice == -1) {
                if (src->looping || src->playing) {
                    src->playing = 0;
                    src->voice = GetVoice(src->sample, 5);
                    if (src->voice) {
                        SetVoiceState(src->voice, 1);
                        src->curPan = 0;
                        src->curPitch = 1.0f;
                        src->curVol = 1.0f;
                    } else {
                        src->voice = -1;
                        return;
                    }
                } else {
                    return;
                }
            }
            if (minDist < src->nearDist) {
                vol = 32767.0f * src->volume;
            } else {
                vol = 32767.0f * src->volume * ((src->farDist - src->nearDist) - (minDist - src->nearDist)) / (src->farDist - src->nearDist);
            }
            if (!FloatsEqual(src->curVol, vol, 0.0002f)) {
                if (!(s8)SetVoiceVol(src->voice, (u32)vol)) {
                    goto fail;
                }
                src->curVol = vol;
            }
            if (g_numListeners != 0) {
                g_sscount++;
                if (!(s8)SetVoicePitch(src->voice, (s32)(65536.0f * src->pitch))) {
                    goto fail;
                }
                if (!(s8)SetVoicePos(src->voice, 0, -0x7FFF)) {
                    goto fail;
                }
                return;
            }
            if (src->pitch != 0.0f) {
                PSVECSubtract(&g_listener[0].pos, &src->pos, &diff);
                XformVecOrientation(&g_listener[0].mtx, &diff, &local);
                dot = PSVECDotProduct(&g_zeroVector, &local);
                if (minDist) {
                    pitch = (343.0f * src->pitch) / (0.1f * (dot / minDist) + 343.0f);
                } else {
                    pitch = (343.0f * src->pitch) / (0.1f * dot + 343.0f);
                }
                if (pitch < 0.0f) {
                    pitch = 1e-7f;
                }
                if (!FloatsEqual(pitch, src->curPitch, 0.0001f)) {
                    if (!(s8)SetVoicePitch(src->voice, (u32)(65536.0f * pitch))) {
                        goto fail;
                    }
                    src->curPitch = pitch;
                }
            } else {
                if (!(s8)SetVoicePitch(src->voice, 0x10000)) {
                    goto fail;
                }
                src->curPitch = 1.0f;
            }
            if (local.x && local.y && local.z && PSVECSquareMag(&local)) {
                PSVECNormalize(&local, &local);
            }
            p = 26213.6f * local.x;
            pan = (s16)p;
            if (src->curPan != pan) {
                if (!(s8)SetVoicePos(src->voice, p, -0x7FFF)) {
                    goto fail;
                }
                src->curPan = pan;
            }
            return;
        }
    }
    if (src->voice != -1) {
        ReleaseVoice(src->voice);
    fail:
        src->voice = -1;
        src->volume = 0.0f;
    }
}

void SFX_Heartbeat(void) {
    extern s32 g_sscount;
    u32 var_r31;

    var_r31 = 0U;
    g_sscount = 0;
    do {
        SFX_ProcessSource(var_r31);
        var_r31 += 1;
    } while (var_r31 < 0x64U);
}

void MusicHearbeat(void) {
    extern s8 g_paused;
    extern f32 g_fDeltaTimeMS;
    extern void (*SetMusicPitch)(u32);
    extern void (*SetMusicTempo)(u32);
    extern void (*SetMusicTrackVol)(u32, u32);
    u32 i;

    if (g_paused) {
        return;
    }
    if (g_globalMusic[0].time > 0.0f && g_globalMusic[0].cur != g_globalMusic[0].target) {
        g_globalMusic[0].cur = (g_globalMusic[0].target - g_globalMusic[0].cur) * (g_fDeltaTimeMS / g_globalMusic[0].time) + g_globalMusic[0].cur;
        g_globalMusic[0].time -= g_fDeltaTimeMS;
        if (g_globalMusic[0].cur < 0.0f) {
            g_globalMusic[0].cur = 0.0f;
        }
        SetMusicVol(32767.0f * g_globalMusic[0].cur);
    }
    if (g_globalMusic[1].time > 0.0f && g_globalMusic[1].cur != g_globalMusic[1].target) {
        g_globalMusic[1].cur = (g_globalMusic[1].target - g_globalMusic[1].cur) * (g_fDeltaTimeMS / g_globalMusic[1].time) + g_globalMusic[1].cur;
        g_globalMusic[1].time -= g_fDeltaTimeMS;
        if (g_globalMusic[1].cur < 0.0f) {
            g_globalMusic[1].cur = 0.0f;
        }
        SetMusicPitch(65536.0f * g_globalMusic[1].cur);
    }
    if (g_globalMusic[2].time > 0.0f && g_globalMusic[2].cur != g_globalMusic[2].target) {
        g_globalMusic[2].cur = (g_globalMusic[2].target - g_globalMusic[2].cur) * (g_fDeltaTimeMS / g_globalMusic[2].time) + g_globalMusic[2].cur;
        g_globalMusic[2].time -= g_fDeltaTimeMS;
        if (g_globalMusic[2].cur < 0.0f) {
            g_globalMusic[2].cur = 0.0f;
        }
        SetMusicTempo(65536.0f * g_globalMusic[2].cur);
    }
    for (i = 0; i < 32; i++) {
        if (g_tracks[i].active && g_tracks[i].time > 0.0f && g_tracks[i].vol != g_tracks[i].target) {
            g_tracks[i].vol = (g_fDeltaTimeMS / g_tracks[i].time) * (g_tracks[i].target - g_tracks[i].vol) + g_tracks[i].vol;
            g_tracks[i].time -= g_fDeltaTimeMS;
            if (g_tracks[i].vol < 0.0f) {
                g_tracks[i].vol = 0.0f;
            }
            SetMusicTrackVol(i, 32767.0f * g_tracks[i].vol);
        }
    }
}

void SoundListener_Set(s32 listener, s32 param, void *value) {

    switch (param) {
    case 5:
        g_listener[listener].pos = *(Vector4 *)value;
        break;
    case 6:
        g_listener[listener].vel = *(Vector4 *)value;
        break;
    case 7:
        memcpy(&g_listener[listener].mtx, value, 0x40);
        break;
    }
}

void SoundListener_SetTotalListeners(u32 arg0) {
    extern s32 g_numListeners;
    if (arg0 != 0U) {
        g_numListeners = arg0 - 1;
    }
}

void SoundSource_Play(s32 index) {

    g_sources[index].playing = 1;
}


void SoundSource_Stop(s32 index) {

    if (g_sources[index].voice > 0) {
        ReleaseVoice(g_sources[index].voice);
    }
    /* port: always free the source (asm); the decomp only did when a voice
     * was playing, so silent sources (cars in vehicle select) leaked until
     * the 100 slots ran out and CreateSource spun forever */
    g_sources[index].voice = -1;
    g_sources[index].volume = 0.0f;
    g_sources[index].looping = FALSE;
    return;
}

s8 SoundSource_IsActive(s32 index) {

    return g_sources[index].voice != -1;
}

s8 SoundSource_IsLooping(s32 index) {

    return g_sources[index].looping;
}

/* port: `sample` is a sound effect id (CreateSFX), which the decomp typed as
 * a pointer */
void *SoundAudioSample_Play(u32 sample) {
    void *voice;

    if (sample == 0) {
        return NULL;
    }
    voice = (void *)(uintptr_t)GetVoice(sample, (u8)(rand() % 10 + 5));  /* port: an opaque voice id */
    if (voice != NULL) {
        SetVoiceState((u32)(uintptr_t)voice, 1);
        return voice;
    }
    return NULL;
}

void SoundAudioSample_Set(void *voice, s32 type, s32 value) {
    switch (type) {
    case 0:
        SetVoiceVol((u32)(uintptr_t)voice, (s16)value);
        break;
    case 1:
        SetVoicePitch((u32)(uintptr_t)voice, value);
        break;
    case 4:
        SetVoicePos((u32)(uintptr_t)voice, (s16)value, -0x7FFF);
        break;
    case 9:
        break;
    }
}

void SongManager_StopBKSong(void) {

}

void SoundManager_Pause(s32 paused) {
    extern s8 g_paused;
    extern void (*SetMusicState)(s32);
    u32 i;

    g_paused = paused;
    if (paused) {
        SetSFXState(2);
        SetMusicState(2);
    } else {
        SetSFXState(1);
        SetMusicState(1);
    }
    SetSFXState(1);
    for (i = 0; i < 100; i++) {
        g_sources[i].paused = paused;
    }
}

void SongManager_LoadSong(u8 *data, u8 *bank) {
    extern s8 g_songLoaded;
    CreateMusic(data, bank);
    g_songLoaded = 1;
}

void SongManager_PlaySong(void) {
    extern s8 g_songLoaded;
    extern void (*SetMusicState)(s32);

    if (g_songLoaded) {
        SetMusicState(1);
        SetMusicVol(32767.0 * g_globalMusic[0].cur);
    }
}

s32 ActionMusicChange(void *data) {
    typedef struct MusicChangeTrackOld {
        u32 unk0;
        u32 track;
        u32 active;
        u8 padC[0x24 - 0xC];
    } MusicChangeTrackOld;
    typedef struct MusicChangeTrack {
        u32 unk0;
        u32 track;
        u32 active;
        f32 vol;
        u8 pad10[0x28 - 0x10];
    } MusicChangeTrack;
    typedef struct MusicChange {
        f32 vol;
        u32 volTime;
        f32 pitch;
        u32 pitchTime;
        f32 tempo;
        u32 tempoTime;
        u32 orderMode;
        u32 order;
        u32 orderType;
        u32 numTracks;
        union {
            MusicChangeTrackOld old[1];
            MusicChangeTrack tracks[1];
        } u;
    } MusicChange;
    extern s8 g_paused;
    extern s8 g_songLoaded;
    extern void (*SetMusicPitch)(u32);
    extern void (*SetMusicTempo)(u32);
    extern void (*SetMusicOrder)(u8, s8);
    MusicChange *action = data;
    u32 i;
    f32 vol;

    if (g_paused) {
        return 0;
    }
    if (!g_songLoaded) {
        return 0;
    }
    if (action->volTime == 0) {
        g_globalMusic[0].cur = action->vol;
        if (GETP(g_worldData.world) != NULL) {
            g_globalMusic[0].cur = action->vol * GETP(((World *)GETP(g_worldData.world))->gameData)->musicVol;
        }
        SetMusicVol(32767.0f * g_globalMusic[0].cur);
    } else {
        g_globalMusic[0].target = action->vol;
    }
    g_globalMusic[0].time = *(f32 *)&action->volTime;
    if (action->pitchTime == 0) {
        g_globalMusic[1].cur = action->pitch;
        if (GETP(g_worldData.world) != NULL) {
            g_globalMusic[1].cur = action->pitch * GETP(((World *)GETP(g_worldData.world))->gameData)->musicPitch;
        }
        SetMusicPitch(65536.0f * g_globalMusic[1].cur);
    } else {
        g_globalMusic[1].target = action->pitch;
    }
    g_globalMusic[1].time = *(f32 *)&action->pitchTime;
    if (action->tempoTime == 0) {
        g_globalMusic[2].cur = action->tempo;
        if (GETP(g_worldData.world) != NULL) {
            g_globalMusic[2].cur = action->tempo * GETP(((World *)GETP(g_worldData.world))->gameData)->musicTempo;
        }
        SetMusicTempo(65536.0f * g_globalMusic[2].cur);
    } else {
        g_globalMusic[2].target = action->tempo;
    }
    g_globalMusic[2].time = *(f32 *)&action->tempoTime;
    if (action->orderMode == 0x29) {
        SetMusicOrder(action->order, action->orderType == 0x27);
        for (i = 0; i < 15; i++) {
            g_tracks[i].active = 0;
        }
    }
    for (i = 0; i < action->numTracks; i++) {
        g_tracks[action->u.old[i].track].active = action->u.old[i].active;
        g_tracks[action->u.tracks[i].track].active = action->u.tracks[i].active;
        vol = *(f32 *)&((u32 *)data)[i * 10 + 13];
        if (vol > 0.0f) {
            g_tracks[action->u.tracks[i].track].target = vol;
            g_tracks[action->u.tracks[i].track].time = vol;
        }
    }
    return 1;
}

s32 ActionMusicSetSong(void) {
    return 1;
}
