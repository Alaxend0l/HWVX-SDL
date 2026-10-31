#include "common.h"
#include "Voice.h"


extern void (*DestroySongVoice)(Voice *voice);
extern Voice *g_pFree;

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
extern u32 m_InitOnce;
extern s32 g_active;




void SetVoiceState_(Voice *voice, s32 state);


s32 AudioVoice_Create_(void *voices) {
    u32 i;

    for (i = 0; i < 0x20; i++) {
        SETP(g_voice[i].axv, NULL);
        g_voice[i].id = BE32(i);  /* port: the index is the id's low byte, last in memory (Voice.c VOICE_ID) */
        g_voice[i].owner = VOICE_OWNER_NONE;
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

u32 CreateSFX_(SFX *sfx) {
    u32 size;
    u32 data;

    size = (sfx->length + 0x1F) & ~0x1F;
    data = MAllocA_(size);
    if (data == 0) {
        return 0;
    }
    DCFlushRange((void *)PTR(sfx->data), size);
    MemCpyA_(data, sfx->data, size);
    /* AX addresses 16-bit samples in samples, not bytes */
    if (sfx->bits == 16) {
        sfx->length >>= 1;
        data >>= 1;
        sfx->loop = (s32)sfx->loop >> 1;
    }
    sfx->data = data;
    return data;
}

void AudioVoice_Destroy_(void) {
    AXRegisterAuxBCallback(NULL, NULL);
    AXRegisterAuxACallback(NULL, NULL);
    AXFXDelayShutdown(&g_reverb[1]);
    AXFXDelayShutdown(&g_reverb[0]);
}

void DestroyVoice_(AXVPB *axv);

Voice *GetVoice_(u8 kind, u32 priority) {
    Voice *voice;
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
    voice->state = VOICE_STATE_STOPPED;
    voice->priority = priority;
    voice->fresh = 1;
    voice->curEnvVol = -1;
    voice->curVolL = -1;
    voice->curAuxL = -1;
    voice->unk55 = 0;
    voice->volRamp = 0;
    voice->auxRamp = 0;
    voice->auxVol = 0x7FFF;
    OSRestoreInterrupts(level);
    return voice;
}


void StartVoice_(Voice *voice) {
    extern u32 g_silence;
    AXVPB *axv;
    s32 level;

    level = OSDisableInterrupts();
    axv = GETP(voice->axv);
    switch (voice->bits) {
    case 8:
        axv->gain = 0x100;
        axv->format = AX_FORMAT_PCM8;
        break;
    case 16:
        axv->gain = 0x800;
        axv->format = AX_FORMAT_PCM16;
        break;
    default:
        axv->gain = 0;
        axv->format = AX_FORMAT_ADPCM;
        break;
    }
    axv->sync |= AX_SYNC_ADPCM;
    axv->currentAddress = voice->addr;
    axv->endAddress = voice->addr + (s32)voice->length - 1;
    if (voice->loop != 0xFFFFFFFF) {
        axv->loopFlag = 1;
        axv->loopAddress = voice->addr + voice->loop;
    } else {
        /* one-shot: the DSP still needs a loop address, so point it at silence */
        axv->loopFlag = 0;
        axv->loopAddress = g_silence;
        if (voice->bits == 16) {
            axv->loopAddress >>= 1;
        }
    }
    axv->sync |= AX_SYNC_ADDR;
    axv->srcSelect = 0;
    axv->coefSelect = 2;
    axv->ratio = (1.0f / 48000.0f) * ((f32)voice->rate * (f32)voice->pitch);
    axv->currentAddressFrac = 0;
    if (voice->fresh) {
        axv->lastSamples[0] = 0;
        axv->lastSamples[1] = 0;
        axv->lastSamples[2] = 0;
        axv->lastSamples[3] = 0;
        axv->sync |= AX_SYNC_SRC | AX_SYNC_SELECT;
    } else {
        axv->sync |= AX_SYNC_RATIO | AX_SYNC_SELECT;
    }
    if (voice->kind != VOICE_KIND_SONG) {
        axv->mixerCtrl = 0x203;
    } else {
        axv->mixerCtrl = 0x13;
    }
    axv->vAuxAL = 0;
    axv->vAuxAR = 0;
    axv->vAuxBL = 0;
    axv->vAuxBR = 0;
    axv->sync |= AX_SYNC_MIXERCTRL;
    axv->state = 1;
    axv->sync |= AX_SYNC_STATE;
    voice->fresh = 0;
    voice->state = VOICE_STATE_PLAYING;
    OSRestoreInterrupts(level);
}


void SetVoiceState_(Voice *voice, s32 state) {
    s32 level;
    s32 level2;

    switch (state) {
    case 3:
        level = OSDisableInterrupts();
        voice->state = 0;
        voice->owner = VOICE_OWNER_FREED;
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
        GETP(voice->axv)->sync |= AX_SYNC_STATE;
        break;
    case 1:
        voice->state = 1;
        GETP(voice->axv)->state = 1;
        GETP(voice->axv)->sync |= AX_SYNC_STATE;
        break;
    }
}

void SetVoicePriority_(Voice *voice) {
    s32 level;

    level = OSDisableInterrupts();
    AXSetVoicePriority(GETP(voice->axv), voice->priority);
    OSRestoreInterrupts(level);
}

void DestroyVoice_(AXVPB *axv) {
    Voice *voice;
    s32 level;

    level = OSDisableInterrupts();
    voice = (Voice *)PTR(axv->userContext);
    if (voice->kind == VOICE_KIND_SONG) {
        DestroySongVoice(voice);
    }
    voice->state = 0;
    voice->owner = VOICE_OWNER_STOLEN;
    SETP(voice->axv, NULL);
    SETP(voice->next, g_pFree);
    g_pFree = voice;
    OSRestoreInterrupts(level);
}

void ProcessVoice_(void) {
    extern const u16 g_volTab[];
    Voice *voice;
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
            if (voice->kind == VOICE_KIND_SONG) {
                DestroySongVoice(voice);
            }
            level = OSDisableInterrupts();
            voice->state = 0;
            voice->owner = VOICE_OWNER_FREED;
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
        if (voice->curEnvVol < 0) {
            voice->curEnvVol = (voice->vol * g_volTab[g_active]) >> 15;
            axv->ve = voice->curEnvVol;
            axv->veDelta = 0;
        } else {
            steps = (((voice->vol * g_volTab[g_active]) >> 15) - voice->curEnvVol) / 160;
            if (steps != 0) {
                axv->ve = voice->curEnvVol;
                axv->veDelta = (u16)steps;
                voice->curEnvVol += steps * 160;
            } else {
                voice->curEnvVol = (voice->vol * g_volTab[g_active]) >> 15;
                axv->ve = voice->curEnvVol;
                axv->veDelta = 0;
            }
        }

        /* the ramps SetVoiceVol_ started took one frame: land on the targets */
        if (voice->volRamp != 0) {
            voice->volRamp = 0;
            voice->curVolL = voice->volL;
            voice->curVolR = voice->volR;
            axv->vL = voice->volL;
            axv->vR = voice->volR;
            axv->vDeltaL = 0;
            axv->vDeltaR = 0;
            axv->mixerCtrl &= ~0x8;
            axv->sync |= AX_SYNC_MIX | AX_SYNC_MIXERCTRL;
        }

        if (voice->auxRamp != 0) {
            voice->auxRamp = 0;
            voice->curAuxL = voice->auxL;
            voice->curAuxR = voice->auxR;
            if (voice->kind != VOICE_KIND_SONG) {
                axv->vAuxBL = voice->auxL;
                axv->vAuxBR = voice->auxR;
                axv->vDeltaAuxBL = 0;
                axv->vDeltaAuxBR = 0;
                axv->mixerCtrl &= ~0x800;
            } else {
                axv->vAuxAL = voice->auxL;
                axv->vAuxAR = voice->auxR;
                axv->vDeltaAuxAL = 0;
                axv->vDeltaAuxAR = 0;
                axv->mixerCtrl &= ~0x40;
            }
            axv->sync |= AX_SYNC_MIX | AX_SYNC_MIXERCTRL;
        }
    }
}

/* Sets the speaker volumes (and, for songs, the reverb send) from vol, the
 * voice's own volume and its pan. Changes ramp over one AX frame (160
 * samples); ProcessVoice_ lands them on the next frame. */
void SetVoiceVol_(Voice *v, u32 vol) {
    extern s32 g_speakers;
    extern const u16 g_panTab[128];
    AXVPB *axv;
    s32 pan;
    u32 amp;
    s32 l;
    s32 r;
    s16 dl;
    s16 dr;

    axv = GETP(v->axv);
    if (axv == (AXVPB *)0xFFFFFFFF) {
        return;
    }
    pan = 0x40;
    axv->sync &= ~AX_SYNC_MIX;
    v->volRamp = 0;
    v->auxRamp = 0;
    if (g_speakers >= 1) {
        pan = (((u32)v->pan >> 9) & 0x7F) ^ 0x40;
    }
    l = g_panTab[0x7F - pan];
    r = g_panTab[pan];
    vol = v->mixVol * vol;
    amp = vol >> 15;
    l = (s32)(l * amp) >> 17;
    r = (s32)(r * amp) >> 17;
    if (l != v->curVolL || r != v->curVolR) {
        axv->sync |= AX_SYNC_MIX | AX_SYNC_MIXERCTRL;
        if (v->curVolL < 0) {
            dl = 0;
            v->volRamp = 1;
            dr = 0;
        } else {
            dl = (l - v->curVolL) / 160;
            dr = (r - v->curVolR) / 160;
        }
        v->volL = l;
        v->volR = r;
        if (dl | dr) {
            v->volRamp = 1;
            axv->vDeltaL = dl;
            axv->vDeltaR = dr;
            axv->mixerCtrl |= 8;
        } else {
            axv->vL = l;
            axv->vR = r;
            axv->mixerCtrl &= ~8;
        }
    }
    if (v->kind == VOICE_KIND_SFX) {
        return;
    }
    l = (u32)(l * v->auxVol) >> 15;
    r = (u32)(r * v->auxVol) >> 15;
    if (l == v->curAuxL && r == v->curAuxR) {
        return;
    }
    axv->sync |= AX_SYNC_MIX | AX_SYNC_MIXERCTRL;
    if (v->curAuxL < 0) {
        dl = 0;
        v->auxRamp = 1;
        dr = 0;
    } else {
        dl = (l - v->curAuxL) / 160;
        dr = (r - v->curAuxR) / 160;
    }
    v->auxL = l;
    v->auxR = r;
    if (dl | dr) {
        v->auxRamp = 1;
        if (v->kind != VOICE_KIND_SONG) {
            axv->vDeltaAuxBL = dl;
            axv->vDeltaAuxBR = dr;
            axv->mixerCtrl |= 0x800;
        } else {
            axv->vDeltaAuxAL = dl;
            axv->vDeltaAuxAR = dr;
            axv->mixerCtrl |= 0x40;
        }
    } else if (v->kind != VOICE_KIND_SONG) {
        axv->vAuxBL = l;
        axv->vAuxBR = r;
        axv->mixerCtrl &= ~0x800;
    } else {
        axv->vAuxAL = l;
        axv->vAuxAR = r;
        axv->mixerCtrl &= ~0x40;
    }
}

s8 SetVoiceRate_(Voice *voice) {
    u32 rate = (1.0f / 48000.0f) * ((f32)voice->rate * (f32)voice->pitch);

    if (rate <= 0x2EE00 && rate >= 0x60) {
        GETP(voice->axv)->ratio = rate;
        GETP(voice->axv)->sync |= AX_SYNC_RATIO;
        return 1;
    }
    return 0;
}
s32 SetVoiceOffset_(Voice *voice, u32 offset) {
    AXVPB *axv;

    if (offset < voice->length) {
        axv = GETP(voice->axv);
        axv->currentAddress = voice->addr + offset;
        axv->sync |= AX_SYNC_ADDR;
        return 1;
    }
    return 0;
}

void SetReverb_(u32 idx, u32 output, u32 feedback, u32 delay) {
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
}

