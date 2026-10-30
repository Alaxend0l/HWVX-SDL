#include "common.h"


extern s32 g_sources_803C6F38;

void SoundManager_Create(void) {
    extern s8 g_songLoaded_803C6F2C;
    extern s32 g_tracks_803C6F30;

    Dprintf("SoundManager_Create()");
    if (!Audio_Create()) {
        printf("AudioManager :: Audio_Create Failed\n");
    }
    Dprintf("SoundManager_Create() 1");
    g_songLoaded_803C6F2C = 0;
    g_sources_803C6F38 = HND(Allocate(0x1F40, 0, ""));
    g_tracks_803C6F30 = HND(Allocate(0x500, 0, ""));
}


void SoundManager_Init(void) {
    typedef struct SoundSource {
        Vector4 pos;
        Vector4 vel;
        f32 unk20;
        f32 unk24;
        s32 unk28;
        s32 voice;
        u32 looping;
        f32 unk34;
        f32 unk38;
        f32 unk3C;
        f32 unk40;
        u32 unk44;
        s8 paused;
        s8 playing;
        u8 unk4A[0x50 - 0x4A];
    } SoundSource;
    typedef struct SoundListener {
        Vector4 pos;
        Vector4 vel;
        Vector4 fwd;
        Vector4 up;
        Vector4 right;
        Matrix4 mtx;
    } SoundListener;
    typedef struct MusicFade {
        f32 cur;
        f32 target;
        f32 time;
    } MusicFade;
    typedef struct MusicTrack {
        u32 active;
        f32 vol;
        f32 target;
        f32 time;
        u8 pad10[0x28 - 0x10];
    } MusicTrack;
    extern s8 g_paused;
    extern MusicFade g_globalMusic[3];
    extern MusicTrack *g_tracks;
    extern SoundSource *g_sources;
    extern s32 g_nextSource;
    extern s32 g_numListeners;
    extern SoundListener g_listener[4];
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    Vector4 fwd __attribute__((aligned(16))) = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector4 up __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 0.0f};
    Vector4 right __attribute__((aligned(16))) = {0.0f, 0.0f, 1.0f, 0.0f};
    u32 i;

    Audio_Init();
    memset(g_globalMusic, 0, sizeof(g_globalMusic));
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
        g_sources[i].unk24 = 0.0f;
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
    extern s32 g_tracks_803C6F30;
    Audio_Destroy();
    Deallocate(PTR(g_sources_803C6F38));
    Deallocate(PTR(g_tracks_803C6F30));
}

void SoundSource_Set(u32 index, u32 type, void *data) {
    extern s32 g_sources;

    switch (type) {
    case 0:
        break;
    case 1:
        memcpy((u8 *)g_sources + index * 0x50, data, 0x10);
        break;
    case 2:
        memcpy((u8 *)g_sources + index * 0x50 + 0x10, data, 0x10);
        break;
    case 3:
        memcpy((u8 *)g_sources + index * 0x50 + 0x24, data, 4);
        break;
    case 4:
        memcpy((u8 *)g_sources + index * 0x50 + 0x20, data, 4);
        break;
    case 5:
        memcpy((u8 *)g_sources + index * 0x50 + 0x28, data, 4);
        break;
    case 6:
        memcpy((u8 *)g_sources + index * 0x50 + 0x34, data, 4);
        break;
    case 7:
        memcpy((u8 *)g_sources + index * 0x50 + 0x38, data, 4);
        break;
    case 8:
        break;
    }
}

s32 SoundManager_CreateSource(u32 owner) {
    typedef struct SoundSource {
        Vector4 pos;
        Vector4 vel;
        f32 unk20;
        f32 unk24;
        s32 unk28;
        s32 voice;
        u32 looping;
        f32 unk34;
        f32 unk38;
        f32 unk3C;
        f32 unk40;
        u32 unk44;
        s8 paused;
        s8 playing;
        u8 unk4A[0x50 - 0x4A];
    } SoundSource;
    extern SoundSource *g_sources;
    extern u32 g_nextSource;
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
    g_sources[g_nextSource].unk28 = -1;
    g_sources[g_nextSource].unk20 = 1.0f;
    g_sources[g_nextSource].unk24 = 0.0f;
    g_sources[g_nextSource].unk34 = 100.0f;
    g_sources[g_nextSource].unk38 = 500.0f;
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
    typedef struct SoundSource {
        Vector4 pos;
        Vector4 vel;
        f32 unk20;
        f32 unk24;
        s32 unk28;
        s32 voice;
        u32 looping;
        f32 unk34;
        f32 unk38;
        f32 unk3C;
        f32 unk40;
        u32 unk44;
        s8 paused;
        s8 playing;
        u8 unk4A[0x50 - 0x4A];
    } SoundSource;
    typedef struct SoundListener {
        Vector4 pos;
        Vector4 vel;
        u8 unk20[0x30];
        u8 mtx[0x40];
    } SoundListener;
    extern s8 g_paused;
    extern SoundSource *g_sources;
    extern SoundListener g_listener[4];
    extern s32 g_numListeners;
    extern s32 g_sscount;
    extern Vector4 g_zeroVector;
    void DistanceXYZ(void *a, void *b, f32 *dist);
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
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
    if (g_paused && src->paused && src->unk24 > 0.0f) {
        if (src->voice > 0) {
            SetVoiceVol(src->voice, 0);
        }
        src->unk40 = 0.0f;
        return;
    }
    if (src->unk24 > 0.0f) {
        minDist = 99999.9f;
        for (i = 0; i <= g_numListeners; i++) {
            DistanceXYZ(&g_listener[i], src, &dist);
            if (minDist > dist) {
                minDist = dist;
            }
        }
        if (minDist < src->unk38) {
            if (src->voice == -1) {
                if (src->looping || src->playing) {
                    src->playing = 0;
                    src->voice = GetVoice(src->unk28, 5);
                    if (src->voice) {
                        SetVoiceState(src->voice, 1);
                        src->unk44 = 0;
                        src->unk3C = 1.0f;
                        src->unk40 = 1.0f;
                    } else {
                        src->voice = -1;
                        return;
                    }
                } else {
                    return;
                }
            }
            if (minDist < src->unk34) {
                vol = 32767.0f * src->unk24;
            } else {
                vol = 32767.0f * src->unk24 * ((src->unk38 - src->unk34) - (minDist - src->unk34)) / (src->unk38 - src->unk34);
            }
            if (!FloatsEqual(src->unk40, vol, 0.0002f)) {
                if (!(s8)SetVoiceVol(src->voice, (u32)vol)) {
                    goto fail;
                }
                src->unk40 = vol;
            }
            if (g_numListeners != 0) {
                g_sscount++;
                if (!(s8)SetVoicePitch(src->voice, (s32)(65536.0f * src->unk20))) {
                    goto fail;
                }
                if (!(s8)SetVoicePos(src->voice, 0, -0x7FFF)) {
                    goto fail;
                }
                return;
            }
            if (src->unk20 != 0.0f) {
                PSVECSubtract(&g_listener[0].pos, &src->pos, &diff);
                XformVecOrientation(g_listener[0].mtx, &diff, &local);
                dot = PSVECDotProduct(&g_zeroVector, &local);
                if (minDist) {
                    pitch = (343.0f * src->unk20) / (0.1f * (dot / minDist) + 343.0f);
                } else {
                    pitch = (343.0f * src->unk20) / (0.1f * dot + 343.0f);
                }
                if (pitch < 0.0f) {
                    pitch = 1e-7f;
                }
                if (!FloatsEqual(pitch, src->unk3C, 0.0001f)) {
                    if (!(s8)SetVoicePitch(src->voice, (u32)(65536.0f * pitch))) {
                        goto fail;
                    }
                    src->unk3C = pitch;
                }
            } else {
                if (!(s8)SetVoicePitch(src->voice, 0x10000)) {
                    goto fail;
                }
                src->unk3C = 1.0f;
            }
            if (local.x && local.y && local.z && PSVECSquareMag(&local)) {
                PSVECNormalize(&local, &local);
            }
            p = 26213.6f * local.x;
            pan = (s16)p;
            if (src->unk44 != pan) {
                if (!(s8)SetVoicePos(src->voice, p, -0x7FFF)) {
                    goto fail;
                }
                src->unk44 = pan;
            }
            return;
        }
    }
    if (src->voice != -1) {
        ReleaseVoice(src->voice);
    fail:
        src->voice = -1;
        src->unk24 = 0.0f;
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
    typedef struct MusicFade {
        f32 cur;
        f32 target;
        f32 time;
    } MusicFade;
    typedef struct MusicTrack {
        u32 active;
        f32 vol;
        f32 target;
        f32 time;
        u8 pad10[0x28 - 0x10];
    } MusicTrack;
    extern s8 g_paused_803C6F2D;
    extern MusicFade g_globalMusic_800DBE30[3];
    extern MusicTrack *g_tracks_803C6F30;
    extern f32 g_fDeltaTimeMS;
    extern void (*SetMusicPitch)(u32);
    extern void (*SetMusicTempo)(u32);
    extern void (*SetMusicTrackVol)(u32, u32);
    u32 i;

    if (g_paused_803C6F2D) {
        return;
    }
    if (g_globalMusic_800DBE30[0].time > 0.0f && g_globalMusic_800DBE30[0].cur != g_globalMusic_800DBE30[0].target) {
        g_globalMusic_800DBE30[0].cur = (g_globalMusic_800DBE30[0].target - g_globalMusic_800DBE30[0].cur) * (g_fDeltaTimeMS / g_globalMusic_800DBE30[0].time) + g_globalMusic_800DBE30[0].cur;
        g_globalMusic_800DBE30[0].time -= g_fDeltaTimeMS;
        if (g_globalMusic_800DBE30[0].cur < 0.0f) {
            g_globalMusic_800DBE30[0].cur = 0.0f;
        }
        SetMusicVol(32767.0f * g_globalMusic_800DBE30[0].cur);
    }
    if (g_globalMusic_800DBE30[1].time > 0.0f && g_globalMusic_800DBE30[1].cur != g_globalMusic_800DBE30[1].target) {
        g_globalMusic_800DBE30[1].cur = (g_globalMusic_800DBE30[1].target - g_globalMusic_800DBE30[1].cur) * (g_fDeltaTimeMS / g_globalMusic_800DBE30[1].time) + g_globalMusic_800DBE30[1].cur;
        g_globalMusic_800DBE30[1].time -= g_fDeltaTimeMS;
        if (g_globalMusic_800DBE30[1].cur < 0.0f) {
            g_globalMusic_800DBE30[1].cur = 0.0f;
        }
        SetMusicPitch(65536.0f * g_globalMusic_800DBE30[1].cur);
    }
    if (g_globalMusic_800DBE30[2].time > 0.0f && g_globalMusic_800DBE30[2].cur != g_globalMusic_800DBE30[2].target) {
        g_globalMusic_800DBE30[2].cur = (g_globalMusic_800DBE30[2].target - g_globalMusic_800DBE30[2].cur) * (g_fDeltaTimeMS / g_globalMusic_800DBE30[2].time) + g_globalMusic_800DBE30[2].cur;
        g_globalMusic_800DBE30[2].time -= g_fDeltaTimeMS;
        if (g_globalMusic_800DBE30[2].cur < 0.0f) {
            g_globalMusic_800DBE30[2].cur = 0.0f;
        }
        SetMusicTempo(65536.0f * g_globalMusic_800DBE30[2].cur);
    }
    for (i = 0; i < 32; i++) {
        if (GETP(g_tracks_803C6F30[i].active) && GETP(g_tracks_803C6F30[i].time) > 0.0f && g_tracks_803C6F30[i].vol != GETP(g_tracks_803C6F30[i].target)) {
            g_tracks_803C6F30[i].vol = (g_fDeltaTimeMS / GETP(g_tracks_803C6F30[i].time)) * (GETP(g_tracks_803C6F30[i].target) - g_tracks_803C6F30[i].vol) + g_tracks_803C6F30[i].vol;
            g_tracks_803C6F30[i].time -= g_fDeltaTimeMS;
            if (g_tracks_803C6F30[i].vol < 0.0f) {
                g_tracks_803C6F30[i].vol = 0.0f;
            }
            SetMusicTrackVol(i, 32767.0f * g_tracks_803C6F30[i].vol);
        }
    }
}

void SoundListener_Set(s32 listener, s32 param, void *value) {
    typedef struct SoundListener {
        Vector4 pos;
        Vector4 vel;
        u8 unk20[0x30];
        u8 mtx[0x40];
    } SoundListener;
    extern SoundListener g_listener[4];

    switch (param) {
    case 5:
        g_listener[listener].pos = *(Vector4 *)value;
        break;
    case 6:
        g_listener[listener].vel = *(Vector4 *)value;
        break;
    case 7:
        memcpy(g_listener[listener].mtx, value, 0x40);
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
    typedef struct SoundSource {
        u8 pad0[0x49];
        s8 playing;
        u8 pad4A[0x50 - 0x4A];
    } SoundSource;
    extern SoundSource *g_sources;

    g_sources[index].playing = 1;
}


void SoundSource_Stop(s32 index) {
    typedef struct SoundSource {
        u8 pad0[0x24];
        f32 unk24;
        u8 pad28[0x2C - 0x28];
        s32 voice;
        u32 looping;
        u8 pad34[0x50 - 0x34];
    } SoundSource;
    extern SoundSource *g_sources;

    if (GETP(g_sources[index].voice) > 0) {
        ReleaseVoice(GETP(g_sources[index].voice));
        g_sources[index].voice = -1;
        g_sources[index].unk24 = 0.0f;
        g_sources[index].looping = FALSE;
    }
    return;
}

s8 SoundSource_IsActive(s32 index) {
    typedef struct SoundSource {
        u8 pad0[0x2C];
        s32 voice;
        u8 pad30[0x50 - 0x30];
    } SoundSource;
    extern SoundSource *g_sources;

    return GETP(g_sources[index].voice) != -1;
}

s8 SoundSource_IsLooping(s32 index) {
    typedef struct SoundSource {
        u8 pad0[0x30];
        s32 looping;
        u8 pad34[0x50 - 0x34];
    } SoundSource;
    extern SoundSource *g_sources;

    return g_sources[index].looping;
}

void *SoundAudioSample_Play(void *sample) {
    void *voice;

    if (sample == NULL) {
        return NULL;
    }
    voice = (void *)HND(PTR(GetVoice(sample, (u8)(rand() % 10 + 5))));
    if (voice != NULL) {
        SetVoiceState(HND(voice), 1);
        return voice;
    }
    return NULL;
}

void SoundAudioSample_Set(void *voice, s32 type, s32 value) {
    switch (type) {
    case 0:
        SetVoiceVol(HND(voice), (s16)value);
        break;
    case 1:
        SetVoicePitch(HND(voice), value);
        break;
    case 4:
        SetVoicePos(HND(voice), (s16)value, -0x7FFF);
        break;
    case 9:
        break;
    }
}

void SongManager_StopBKSong(void) {

}

void SoundManager_Pause(s32 paused) {
    typedef struct SoundSource {
        Vector4 pos;
        Vector4 vel;
        f32 unk20;
        f32 unk24;
        s32 unk28;
        s32 voice;
        u32 looping;
        f32 unk34;
        f32 unk38;
        f32 unk3C;
        f32 unk40;
        u32 unk44;
        s8 paused;
        s8 playing;
        u8 unk4A[0x50 - 0x4A];
    } SoundSource;
    extern s8 g_paused;
    extern SoundSource *g_sources;
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

void SongManager_LoadSong(u8 *data, u32 size) {
    extern s8 g_songLoaded_803C6F2C;
    CreateMusic(data, size);
    g_songLoaded_803C6F2C = 1;
}

void SongManager_PlaySong(void) {
    extern s8 g_songLoaded_803C6F2C;
    extern void (*SetMusicState)(s32);
    extern f32 g_globalMusic_800DBE30[];

    if (g_songLoaded_803C6F2C) {
        SetMusicState(1);
        SetMusicVol(32767.0 * g_globalMusic_800DBE30[0]);
    }
}

s32 ActionMusicChange(void *data) {
    typedef struct MusicFade {
        f32 cur;
        f32 target;
        f32 time;
    } MusicFade;
    typedef struct MusicTrack {
        u32 active;
        f32 vol;
        f32 target;
        f32 time;
        u8 pad10[0x28 - 0x10];
    } MusicTrack;
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
    typedef struct MusicScale {
        u8 pad0[0x30];
        f32 vol;
        f32 pitch;
        f32 tempo;
    } MusicScale;
    extern s8 g_paused_803C6F2D;
    extern s8 g_songLoaded_803C6F2C;
    extern MusicFade g_globalMusic_800DBE30[3];
    extern MusicTrack *g_tracks_803C6F30;
    extern void (*SetMusicPitch)(u32);
    extern void (*SetMusicTempo)(u32);
    extern void (*SetMusicOrder)(u8, s8);
    MusicChange *action = data;
    u32 i;
    f32 vol;

    if (g_paused_803C6F2D) {
        return 0;
    }
    if (!g_songLoaded_803C6F2C) {
        return 0;
    }
    if (action->volTime == 0) {
        g_globalMusic_800DBE30[0].cur = action->vol;
        if (GETP(g_worldData.world) != NULL) {
            g_globalMusic_800DBE30[0].cur = action->vol * (SLOT_LOAD(MusicScale *, ((u8 *)GETP(g_worldData.world) + 0x9C)))->vol;
        }
        SetMusicVol(32767.0f * g_globalMusic_800DBE30[0].cur);
    } else {
        g_globalMusic_800DBE30[0].target = action->vol;
    }
    g_globalMusic_800DBE30[0].time = *(f32 *)&action->volTime;
    if (action->pitchTime == 0) {
        g_globalMusic_800DBE30[1].cur = action->pitch;
        if (GETP(g_worldData.world) != NULL) {
            g_globalMusic_800DBE30[1].cur = action->pitch * (SLOT_LOAD(MusicScale *, ((u8 *)GETP(g_worldData.world) + 0x9C)))->pitch;
        }
        SetMusicPitch(65536.0f * g_globalMusic_800DBE30[1].cur);
    } else {
        g_globalMusic_800DBE30[1].target = action->pitch;
    }
    g_globalMusic_800DBE30[1].time = *(f32 *)&action->pitchTime;
    if (action->tempoTime == 0) {
        g_globalMusic_800DBE30[2].cur = action->tempo;
        if (GETP(g_worldData.world) != NULL) {
            g_globalMusic_800DBE30[2].cur = action->tempo * (SLOT_LOAD(MusicScale *, ((u8 *)GETP(g_worldData.world) + 0x9C)))->tempo;
        }
        SetMusicTempo(65536.0f * g_globalMusic_800DBE30[2].cur);
    } else {
        g_globalMusic_800DBE30[2].target = action->tempo;
    }
    g_globalMusic_800DBE30[2].time = *(f32 *)&action->tempoTime;
    if (action->orderMode == 0x29) {
        SetMusicOrder(action->order, action->orderType == 0x27);
        for (i = 0; i < 15; i++) {
            SETP(g_tracks_803C6F30[i].active, 0);
        }
    }
    for (i = 0; i < action->numTracks; i++) {
        g_tracks_803C6F30[action->u.old[i].track].active = action->u.old[i].active;
        g_tracks_803C6F30[action->u.tracks[i].track].active = action->u.tracks[i].active;
        vol = *(f32 *)&((u32 *)data)[i * 10 + 13];
        if (vol > 0.0f) {
            g_tracks_803C6F30[action->u.tracks[i].track].target = vol;
            g_tracks_803C6F30[action->u.tracks[i].track].time = vol;
        }
    }
    return 1;
}

s32 ActionMusicSetSong(void) {
    return 1;
}
