

#include "common.h"

typedef struct SFX {
    void *data;
    u32 unk4;
    u32 unk8;
    u32 unkC;
    u8 unk10;
    u8 unk11;
    u16 unk12;
    u8 unk14[0xC];
} SFX;

typedef struct Voice {
    union {
        u32 id;
        struct {
            u8 kind;
            u8 sfxIndex;
            u8 serial;
        };
    };
    u8 unk4[0xC];
    SFX *sfx;
    u8 unk14;
    u8 state;
    u8 unk16[0xA];
    u32 unk20;
    u32 unk24;
    u32 unk28;
    u8 unk2C;
    u32 unk30;
    s32 pitch;
    s32 vol;
    u32 unk3C;
    s32 posX;
    s32 posY;
    s32 unk48;
    u8 unk4C[0x24];
} Voice;

extern Voice g_voice[0x20];
extern SFX g_sound[0xFE];
extern u8 g_soundLst[0xFE];
extern u32 g_freeSound;
extern s32 g_sfxvol;
extern s8 g_speakers;
extern void (*VoiceUpdate)(void);
extern s32 g_sfxState;

void SetVoiceState_(Voice *voice, s32 state);
void StartVoice_(Voice *voice);
s8 SetVoiceRate_(Voice *voice);

void AudioVoice_Create(void) {
    memset(g_voice, 0, sizeof(g_voice));
    g_sfxvol = 0x7FFF;
    g_speakers = 1;
    AudioVoice_Create_(g_voice);
}



s32 AudioVoice_Init(void) {
    u32 i;

    for (i = 0; i < 0xFE; i++) {
        g_soundLst[i] = i + 1;
        g_sound[i].data = 0;
    }
    g_soundLst[0xFD] = 0xFF;
    g_freeSound = 0;
    AudioVoice_Init_();
    return StartVoiceUpdate();
}


void AudioVoice_Destroy(void) {
    VoiceUpdate = 0;
    AudioVoice_Destroy_();
}

void SetSpeakerMode(s8 mode) {
    g_speakers = mode;
}

void SetReverb(s32 a, s32 b) {
    SetReverb_(a, b, 0x666, 0x28);
}

void ProcessVoice(void)
{
    ProcessVoice_();
}

u32 CreateSFX(void *desc) {
    u32 idx;
    SFX *sfx;

    if (g_freeSound == 0xFFFFFFFF) {
        return 0;
    }
    sfx = &g_sound[g_freeSound];
    memcpy(sfx, desc, sizeof(SFX));
    if (CreateSFX_(sfx) == 0) {
        return 0;
    }
    idx = g_freeSound;
    g_freeSound = g_soundLst[g_freeSound];
    g_soundLst[idx++] = 0xFF;
    return idx;
}

s8 DestroySFX(u32 handle) {
    u32 data;

    handle--;
    if (handle < 0xFE && g_sound[handle].data != NULL) {
        SFX *sfx = &g_sound[handle];
        g_soundLst[handle] = g_freeSound;
        g_freeSound = handle;
        data = (u32)HND(sfx->data);
        sfx->data = NULL;
        return FreeA_(data);
    }
    return 0;
}

void SetSFXVol(s32 vol) {
    Voice *voice;
    u32 i;

    g_sfxvol = vol;
    for (voice = g_voice, i = 0x20; i != 0; voice++, i--) {
        if (voice->kind == 1 && voice->state == 1) {
            SetVoiceVol_(voice, g_sfxvol);
        }
    }
}

s32 SetSFXState(s32 state) {
    u32 i;
    Voice *voice;
    s32 old;

    if (state == 0 || state == g_sfxState) {
        return g_sfxState;
    }
    if (state != 1) {
        VoiceUpdate = NULL;
    }
    voice = g_voice;
    switch (state) {
    case 2:
        for (i = 0x20; i != 0; voice++, i--) {
            if (voice->kind == 1 && voice->state == 1) {
                SetVoiceState_(voice, 2);
            }
        }
        break;
    case 3:
        for (i = 0x20; i != 0; voice++, i--) {
            if (voice->kind == 1 && voice->state != 0) {
                SetVoiceState_(voice, 2);
                voice->state = 3;
            }
        }
        break;
    case 4:
        for (i = 0x20; i != 0; voice++, i--) {
            if (voice->kind == 1) {
                voice->state = 3;
            }
        }
        voice = g_voice;
    case 1:
        for (i = 0x20; i != 0; voice++, i--) {
            if (voice->kind == 1) {
                if (voice->state == 2) {
                    SetVoiceState_(voice, 1);
                } else if (voice->state == 3) {
                    StartVoice_(voice);
                    SetVoiceVol_(voice, g_sfxvol);
                }
            }
        }
        state = 1;
        VoiceUpdate = ProcessVoice;
        break;
    }
    old = g_sfxState;
    g_sfxState = state;
    return old;
}

Voice *GetVoice_(s32 type, s32 priority);
u32 GetVoice(u32 handle, u8 priority) {
    Voice *voice;
    SFX *sfx;

    handle--;
    if (handle < 0xFE && g_sound[handle].data != NULL) {
        sfx = &g_sound[handle];
        voice = GetVoice_(1, priority * 2);
        if (voice != NULL) {
            voice->sfx = sfx;
            voice->unk20 = (u32)HND(sfx->data);
            voice->unk28 = sfx->unk4;
            voice->unk24 = sfx->unk8;
            voice->unk30 = sfx->unkC;
            voice->pitch = 0x10000;
            voice->unk2C = sfx->unk11;
            voice->vol = 0x7FFF;
            voice->unk3C = sfx->unk12;
            voice->unk48 = 0x7FFF;
            voice->posX = 0;
            voice->posY = -0x7FFF;
            voice->sfxIndex = handle;
            voice->serial++;
            return voice->id;
        }
    }
    return 0;
}

void ReleaseVoice(u32 id) {
    if (id == g_voice[(u8)id].id) {
        SetVoiceState_(&g_voice[(u8)id], 3);
    }
}

extern s32 g_sfxState;
void StartVoice_(Voice *voice);
s32 SetVoiceState(u32 id, s32 state) {
    Voice *voice = &g_voice[(u8)id];

    if (voice->id == id) {
        if (voice->state == state) {
            return state;
        }
        switch (state) {
        case 1:
            if (voice->state == 2) {
                SetVoiceState_(voice, 1);
                g_sfxState = 1;
                break;
            }
        case 4:
            StartVoice_(voice);
            SetVoiceVol_(voice, g_sfxvol);
            g_sfxState = 1;
            break;
        case 2:
        case 3:
            SetVoiceState_(voice, 2);
            break;
        }
        return voice->state;
    }
    return 0;
}

s32 SetVoiceVol(u32 id, s32 vol) {
    Voice *voice = &g_voice[(u8)id];

    if (voice->id == id) {
        voice->vol = vol;
        if (voice->state == 1) {
            SetVoiceVol_(voice, g_sfxvol);
        }
        return 1;
    }
    return 0;
}

s32 SetVoicePos(u32 id, s16 x, s16 y) {
    Voice *voice = &g_voice[(u8)id];

    if (voice->id == id) {
        voice->posX = x;
        voice->posY = y;
        if (voice->state == 1) {
            SetVoiceVol_(voice, g_sfxvol);
        }
        return 1;
    }
    return 0;
}

s32 SetVoicePitch(u32 id, s32 pitch) {
    Voice *voice = &g_voice[(u8)id];

    if (voice->id == id) {
        voice->pitch = pitch;
        if (SetVoiceRate_(voice)) {
            return 1;
        }
        SetVoiceState_(voice, 3);
    }
    return 0;
}

