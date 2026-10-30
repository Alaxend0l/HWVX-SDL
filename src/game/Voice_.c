#include "common.h"


typedef struct AXVPB {
    PTR32(struct AXVPB) next;
    PTR32(struct AXVPB) prev;
    PTR32(struct AXVPB) next1;
    u32 priority;
    PTR32(void) callback;
    PTR32(void) userContext;
    u32 index;
    u32 sync;
    u8 unk20[0x144 - 0x20];
    u16 mixerCtrl;
    u16 state;
    u8 unk148[2];
    s16 vL;
    s16 vDeltaL;
    s16 vR;
    s16 vDeltaR;
    s16 vAuxAL;
    s16 vAuxADeltaL;
    s16 vAuxAR;
    s16 vAuxADeltaR;
    s16 vAuxBL;
    s16 vAuxBDeltaL;
    s16 vAuxBR;
    s16 vAuxBDeltaR;
    u8 unk162[0x19C - 0x162];
    s16 ve;
    s16 veDelta;
    u8 unk1A0[0x1B2 - 0x1A0];
    u16 currentAddressHi;
    u16 currentAddressLo;
    u8 unk1B6[0x1DE - 0x1B6];
    u16 ratioHi;
    u16 ratioLo;
} AXVPB;

typedef struct VoiceHW {
    u8 kind;
    u8 id;
    u8 unk2[2];
    PTR32(struct VoiceHW) next;
    u8 unk8[0xC];
    u8 priority;
    u8 state;
    u8 unk16[0xA];
    u32 addr;
    u8 unk24[4];
    u32 length;
    u8 unk2C[4];
    u32 rate;
    u32 pitch;
    s32 vol;
    u8 unk3C[0xC];
    s32 unk48;
    u8 unk4C[4];
    PTR32(AXVPB) axv;
    u8 unk54;
    u8 unk55;
    u8 unk56;
    u8 unk57;
    u8 unk58[2];
    s16 unk5A;
    u8 unk5C[4];
    s16 unk60;
    s16 unk62;
    s16 unk64;
    s16 unk66;
    s16 unk68;
    s16 unk6A;
    s16 unk6C;
    s16 unk6E;
} VoiceHW;

extern void (*DestroySongVoice)(VoiceHW *voice);
extern VoiceHW *g_pFree;

typedef struct AXFX_DELAY {
    PTR32(s32) left;
    PTR32(s32) right;
    PTR32(s32) sur;
    u32 currentSize[3];
    u32 currentPos[3];
    u32 currentFeedback[3];
    u32 currentOutput[3];
    u32 delay[3];
    u32 feedback[3];
    u32 output[3];
} AXFX_DELAY;

extern AXFX_DELAY g_reverb[2];
extern VoiceHW g_voice[0x20];
extern u32 m_InitOnce;
extern s32 g_active;




void SetVoiceState_(VoiceHW *voice, s32 state);


s32 AudioVoice_Create_(void *voices) {
    u32 i;

    for (i = 0; i < 0x20; i++) {
        SETP(g_voice[i].axv, NULL);
        *(u32 *)&g_voice[i] = BE32(i);  /* port: the index is the id's low byte, last in memory (Voice.c VOICE_ID) */
        g_voice[i].id = 0xFF;
    }
    m_InitOnce = 1;
    return 1;
}

s32 AudioVoice_Init_(void) {
    u32 i;

    for (i = 0; i < 0x20; i++) {
        SetVoiceState_(&g_voice[i], 3);
        SETP(g_voice[i].next, &g_voice[i + 1]);
    }
    SETP(g_voice[0x1F].next, NULL);
    g_pFree = g_voice;
    g_active = -1;
    if (m_InitOnce == 0) {
        AudioVoice_Destroy_();
    }
    m_InitOnce = 0;
    for (i = 0; i < 2; i++) {
        g_reverb[i].delay[0] = 10;
        g_reverb[i].delay[1] = 10;
        g_reverb[i].delay[2] = 10;
        g_reverb[i].feedback[0] = 0;
        g_reverb[i].feedback[1] = 0;
        g_reverb[i].feedback[2] = 0;
        g_reverb[i].output[0] = 0;
        g_reverb[i].output[1] = 0;
        g_reverb[i].output[2] = 0;
        AXFXDelayInit(&g_reverb[i]);
    }
    AXRegisterAuxACallback(AXFXDelayCallback, &g_reverb[0]);
    AXRegisterAuxBCallback(AXFXDelayCallback, &g_reverb[1]);
    return 1;
}

u32 CreateSFX_(u32 *sfx) {
    u32 size;
    u32 data;

    size = (sfx[1] + 0x1F) & ~0x1F;
    data = MAllocA_(size);
    if (data == 0) {
        return 0;
    }
    DCFlushRange((void *)PTR(sfx[0]), size);
    MemCpyA_(data, sfx[0], size);
    if (((u8 *)sfx)[0x11] == 0x10) {
        sfx[1] >>= 1;
        *(u32 *)&data = data >> 1;
        ((s32 *)sfx)[2] >>= 1;
    }
    sfx[0] = data;
    return data;
}

void AudioVoice_Destroy_(void) {
    AXRegisterAuxBCallback(NULL, NULL);
    AXRegisterAuxACallback(NULL, NULL);
    AXFXDelayShutdown(&g_reverb[1]);
    AXFXDelayShutdown(&g_reverb[0]);
}

void DestroyVoice_(AXVPB *axv);

VoiceHW *GetVoice_(u8 kind, u32 priority) {
    VoiceHW *voice;
    AXVPB *axv;
    s32 level;

    if (g_pFree == NULL) {
        OSReport("g_pFree NULL\n");
        return NULL;
    }
    level = OSDisableInterrupts();
    axv = AXAcquireVoice(priority, DestroyVoice_, (u32)HND(g_pFree));
    if (axv == NULL) {
        return NULL;
    }
    voice = g_pFree;
    g_pFree = GETP(g_pFree->next);
    SETP(voice->next, NULL);
    SETP(voice->axv, axv);
    voice->kind = kind;
    voice->state = 3;
    voice->priority = priority;
    voice->unk54 = 1;
    voice->unk5A = -1;
    voice->unk64 = -1;
    voice->unk6C = -1;
    voice->unk55 = 0;
    voice->unk56 = 0;
    voice->unk57 = 0;
    voice->unk48 = 0x7FFF;
    OSRestoreInterrupts(level);
    return voice;
}


void StartVoice_(VoiceHW *voice) {
    extern u32 g_silence;
    u8 *axv;
    s32 level;

    level = OSDisableInterrupts();
    axv = (u8 *)GETP(voice->axv);
    switch (voice->unk2C[0]) {
    case 8:
        *(u16 *)(axv + 0x1D6) = 0x100;
        *(u16 *)(axv + 0x1A8) = 0x19;
        break;
    case 16:
        *(u16 *)(axv + 0x1D6) = 0x800;
        *(u16 *)(axv + 0x1A8) = 0xA;
        break;
    default:
        *(u16 *)(axv + 0x1D6) = 0;
        *(u16 *)(axv + 0x1A8) = 0;
        break;
    }
    *(u32 *)(axv + 0x1C) |= 0x20000;
    *(u32 *)(axv + 0x1B2) = voice->addr;
    *(u32 *)(axv + 0x1AE) = voice->addr + (s32)voice->length - 1;
    if (*(u32 *)voice->unk24 != 0xFFFFFFFF) {
        *(u16 *)(axv + 0x1A6) = 1;
        *(u32 *)(axv + 0x1AA) = voice->addr + *(u32 *)voice->unk24;
    } else {
        *(u16 *)(axv + 0x1A6) = 0;
        *(u32 *)(axv + 0x1AA) = g_silence;
        if (voice->unk2C[0] == 16) {
            *(u32 *)(axv + 0x1AA) >>= 1;
        }
    }
    *(u32 *)(axv + 0x1C) |= 0x1000;
    *(u16 *)(axv + 0x140) = 0;
    *(u16 *)(axv + 0x142) = 2;
    *(u32 *)(axv + 0x1DE) = (1.0f / 48000.0f) * ((f32)voice->rate * (f32)voice->pitch);
    *(u16 *)(axv + 0x1E2) = 0;
    if (voice->unk54) {
        *(u16 *)(axv + 0x1E4) = 0;
        *(u16 *)(axv + 0x1E6) = 0;
        *(u16 *)(axv + 0x1E8) = 0;
        *(u16 *)(axv + 0x1EA) = 0;
        *(u32 *)(axv + 0x1C) |= 0x40000 | 1;
    } else {
        *(u32 *)(axv + 0x1C) |= 0x80000 | 1;
    }
    if (voice->kind) {
        *(u16 *)(axv + 0x144) = 0x203;
    } else {
        *(u16 *)(axv + 0x144) = 0x13;
    }
    *(u16 *)(axv + 0x152) = 0;
    *(u16 *)(axv + 0x156) = 0;
    *(u16 *)(axv + 0x15A) = 0;
    *(u16 *)(axv + 0x15E) = 0;
    *(u32 *)(axv + 0x1C) |= 2;
    *(u16 *)(axv + 0x146) = 1;
    *(u32 *)(axv + 0x1C) |= 4;
    voice->unk54 = 0;
    voice->state = 1;
    OSRestoreInterrupts(level);
}


void SetVoiceState_(VoiceHW *voice, s32 state) {
    s32 level;
    s32 level2;

    switch (state) {
    case 3:
        level = OSDisableInterrupts();
        voice->state = 0;
        voice->id = 0xFE;
        if (GETP(voice->axv) != NULL) {
            level2 = OSDisableInterrupts();
            if (GETP(voice->axv)->priority != 0) {
                AXFreeVoice(GETP(voice->axv));
            }
            SETP(voice->axv, NULL);
            OSRestoreInterrupts(level2);
        }
        SETP(voice->next, g_pFree);
        g_pFree = voice;
        OSRestoreInterrupts(level);
        break;
    case 2:
        voice->state = 2;
        GETP(voice->axv)->state = 0;
        GETP(voice->axv)->sync |= 4;
        break;
    case 1:
        voice->state = 1;
        GETP(voice->axv)->state = 1;
        GETP(voice->axv)->sync |= 4;
        break;
    }
}

void SetVoicePriority_(VoiceHW *voice) {
    s32 level;

    level = OSDisableInterrupts();
    AXSetVoicePriority(GETP(voice->axv), voice->priority);
    OSRestoreInterrupts(level);
}

void DestroyVoice_(AXVPB *axv) {
    VoiceHW *voice;
    s32 level;

    level = OSDisableInterrupts();
    voice = GETP(axv->userContext);
    if (voice->kind == 0) {
        DestroySongVoice(voice);
    }
    voice->state = 0;
    voice->id = 0xFD;
    SETP(voice->axv, NULL);
    SETP(voice->next, g_pFree);
    g_pFree = voice;
    OSRestoreInterrupts(level);
}

void ProcessVoice_(void) {
    extern const u16 g_volTab[];
    VoiceHW *voice;
    AXVPB *axv;
    s32 i;
    s32 level2;
    s32 level;
    s32 steps;

    g_active = -1;
    for (voice = g_voice, i = 0x20; i != 0; i--) {
        u8 state = voice->state;
        voice++;
        if (state == 1) {
            g_active++;
        }
    }

    for (voice = g_voice, i = 0x20; i != 0; voice++, i--) {
        if (voice->state != 1) {
            continue;
        }
        axv = GETP(voice->axv);
        if (axv->state == 0) {
            if (voice->kind == 0) {
                DestroySongVoice(voice);
            }
            level = OSDisableInterrupts();
            voice->state = 0;
            voice->id = 0xFE;
            if (GETP(voice->axv) != NULL) {
                level2 = OSDisableInterrupts();
                if (GETP(voice->axv)->priority != 0) {
                    AXFreeVoice(GETP(voice->axv));
                }
                SETP(voice->axv, NULL);
                OSRestoreInterrupts(level2);
            }
            SETP(voice->next, g_pFree);
            g_pFree = voice;
            OSRestoreInterrupts(level);
            continue;
        }

        axv->sync |= 0x200;
        if (voice->unk5A < 0) {
            voice->unk5A = (voice->vol * g_volTab[g_active]) >> 15;
            axv->ve = voice->unk5A;
            axv->veDelta = 0;
        } else {
            steps = (((voice->vol * g_volTab[g_active]) >> 15) - voice->unk5A) / 160;
            if (steps != 0) {
                axv->ve = voice->unk5A;
                axv->veDelta = (u16)steps;
                voice->unk5A += steps * 160;
            } else {
                voice->unk5A = (voice->vol * g_volTab[g_active]) >> 15;
                axv->ve = voice->unk5A;
                axv->veDelta = 0;
            }
        }

        if (voice->unk56 != 0) {
            voice->unk56 = 0;
            voice->unk64 = voice->unk60;
            voice->unk66 = voice->unk62;
            axv->vL = voice->unk60;
            axv->vR = voice->unk62;
            axv->vDeltaL = 0;
            axv->vDeltaR = 0;
            axv->mixerCtrl &= ~0x8;
            axv->sync |= 0x12;
        }

        if (voice->unk57 != 0) {
            voice->unk57 = 0;
            voice->unk6C = voice->unk68;
            voice->unk6E = voice->unk6A;
            if (voice->kind != 0) {
                axv->vAuxBL = voice->unk68;
                axv->vAuxBR = voice->unk6A;
                axv->vAuxBDeltaL = 0;
                axv->vAuxBDeltaR = 0;
                axv->mixerCtrl &= ~0x800;
            } else {
                axv->vAuxAL = voice->unk68;
                axv->vAuxAR = voice->unk6A;
                axv->vAuxADeltaL = 0;
                axv->vAuxADeltaR = 0;
                axv->mixerCtrl &= ~0x40;
            }
            axv->sync |= 0x12;
        }
    }
}

void SetVoiceVol_(u8 *v, u32 vol) {
    extern s32 g_speakers;
    extern const u16 g_panTab[128];
    u8 *axv;
    s32 pan;
    u32 amp;
    s32 l;
    s32 r;
    s16 dl;
    s16 dr;

    axv = SLOT_LOAD(u8 *, (v + 0x50));
    if (axv == (u8 *)0xFFFFFFFF) {
        return;
    }
    pan = 0x40;
    *(u32 *)(axv + 0x1C) &= ~0x10;
    v[0x56] = 0;
    v[0x57] = 0;
    if (g_speakers >= 1) {
        pan = ((*(u32 *)(v + 0x40) >> 9) & 0x7F) ^ 0x40;
    }
    l = g_panTab[0x7F - pan];
    r = g_panTab[pan];
    vol = *(u32 *)(v + 0x3C) * vol;
    amp = vol >> 15;
    l = (s32)(l * amp) >> 17;
    r = (s32)(r * amp) >> 17;
    if (l != *(s16 *)(v + 0x64) || r != *(s16 *)(v + 0x66)) {
        *(u32 *)(axv + 0x1C) |= 0x12;
        if (*(s16 *)(v + 0x64) < 0) {
            dl = 0;
            v[0x56] = 1;
            dr = 0;
        } else {
            dl = (l - *(s16 *)(v + 0x64)) / 160;
            dr = (r - *(s16 *)(v + 0x66)) / 160;
        }
        *(s16 *)(v + 0x60) = l;
        *(s16 *)(v + 0x62) = r;
        if (dl | dr) {
            v[0x56] = 1;
            *(s16 *)(axv + 0x14C) = dl;
            *(s16 *)(axv + 0x150) = dr;
            *(u16 *)(axv + 0x144) |= 8;
        } else {
            *(s16 *)(axv + 0x14A) = l;
            *(s16 *)(axv + 0x14E) = r;
            *(u16 *)(axv + 0x144) &= ~8;
        }
    }
    if (v[0] == 1) {
        return;
    }
    l = (u32)(l * *(s32 *)(v + 0x48)) >> 15;
    r = (u32)(r * *(s32 *)(v + 0x48)) >> 15;
    if (l == *(s16 *)(v + 0x6C) && r == *(s16 *)(v + 0x6E)) {
        return;
    }
    *(u32 *)(axv + 0x1C) |= 0x12;
    if (*(s16 *)(v + 0x6C) < 0) {
        dl = 0;
        v[0x57] = 1;
        dr = 0;
    } else {
        dl = (l - *(s16 *)(v + 0x6C)) / 160;
        dr = (r - *(s16 *)(v + 0x6E)) / 160;
    }
    *(s16 *)(v + 0x68) = l;
    *(s16 *)(v + 0x6A) = r;
    if (dl | dr) {
        v[0x57] = 1;
        if (v[0]) {
            *(s16 *)(axv + 0x15C) = dl;
            *(s16 *)(axv + 0x160) = dr;
            *(u16 *)(axv + 0x144) |= 0x800;
        } else {
            *(s16 *)(axv + 0x154) = dl;
            *(s16 *)(axv + 0x158) = dr;
            *(u16 *)(axv + 0x144) |= 0x40;
        }
    } else if (v[0]) {
        *(s16 *)(axv + 0x15A) = l;
        *(s16 *)(axv + 0x15E) = r;
        *(u16 *)(axv + 0x144) &= ~0x800;
    } else {
        *(s16 *)(axv + 0x152) = l;
        *(s16 *)(axv + 0x156) = r;
        *(u16 *)(axv + 0x144) &= ~0x40;
    }
}

s8 SetVoiceRate_(VoiceHW *voice) {
    u32 rate = (1.0f / 48000.0f) * ((f32)voice->rate * (f32)voice->pitch);

    if (rate <= 0x2EE00 && rate >= 0x60) {
        *(u32 *)&GETP(voice->axv)->ratioHi = rate;
        GETP(voice->axv)->sync |= 0x80000;
        return 1;
    }
    return 0;
}
s32 SetVoiceOffset_(VoiceHW *voice, u32 offset) {
    AXVPB *axv;

    if (offset < voice->length) {
        axv = GETP(voice->axv);
        *(u32 *)&axv->currentAddressHi = voice->addr + offset;
        axv->sync |= 0x1000;
        return 1;
    }
    return 0;
}

void SetReverb_(u32 idx, u32 output, u32 feedback, u32 delay) {
    typedef struct {
        u8 unk0[0x3C];
        u32 delay[3];
        u32 feedback[3];
        u32 output[3];
    } AXFX_DELAY;
    #define g_reverb ((AXFX_DELAY *)(void *)&g_reverb)

    if (delay < 10) {
        delay = 10;
    }
    g_reverb[idx].delay[0] = delay;
    g_reverb[idx].delay[1] = delay;
    feedback = (feedback * 100) >> 15;
    g_reverb[idx].feedback[0] = feedback;
    g_reverb[idx].feedback[1] = feedback;
    if (idx == 1) {
        output = (output * 100) >> 15;
        g_reverb[0].output[0] = output;
        g_reverb[0].output[1] = output;
        AXFXDelaySettings(&g_reverb[1]);
    } else {
        g_reverb[1].output[0] = 100;
        g_reverb[1].output[1] = 100;
        AXFXDelaySettings(&g_reverb[0]);
    }
#undef g_reverb
}

