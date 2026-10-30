#include "common.h"

typedef struct MIDI_VOICE
{
    u8 type;
    u8 channel;
    u8 byte2;
    u8 byte3;
    PTR32(struct MIDI_VOICE) next;
    u8 pad_8_F[8];
    PTR32(struct MIDI_INST) prog;
    u8 priority;
    u8 state;
    u8 state2;
    s8 byte17;
    u32 rate;
    u32 count;
    u32 field_20;
    u32 field_24;
    u32 field_28;
    u8 byte2C;
    u8 pad_2D_30[3];
    u32 noteRate;
    u32 pitch2;
    s32 field_38;
    s32 field_3C;
    s32 field_40;
    s32 field_44;
    s32 field_48;
    u8 pad_4C_50[4];
} MIDI_VOICE;

typedef struct MIDI_CHN
{
    PTR32(struct MIDI_VOICE) voice;
    s8 field_4;
    u8 field_5;
    u8 prog;
    u8 field_7;
    u16 field_8;
    u8 field_A;
    u8 field_B;
    s32 field_C;
    s32 field_10;
    u32 vol;
    u32 pitch;
    u8 count;
    u8 note;
    u8 field_1E;
    s8 field_1F;
} MIDI_CHN;

typedef struct MIDI_SONG
{
    u32 field_0;
    u32 field_4;
    u32 field_8;
    u32 field_C;
    u32 field_10;
    PTR32(u8) field_14;
    u32 field_18;
    u8 field_1C;
    u8 field_1D;
    u8 field_1E;
    s8 field_1F;
    u32 field_20;
} MIDI_SONG;

u8 g_song[0x24];

typedef struct MIDI_INST
{
    u32 field_0;
    u32 field_4;
    u32 field_8;
    u32 field_C;
    u8 field_10;
    u8 field_11;
    u8 field_12;
    u8 field_13;
    u8 prog;
    u8 field_15;
    u8 lo;
    u8 hi;
    u16 field_18;
    u16 field_1A;
    u16 field_1C;
} MIDI_INST;

typedef struct MIDI_SND
{
    u8 pad_0_10[0x10];
    u32 count;
    u32 size;
    u8 pad_18_20[8];
    MIDI_INST inst[1];
} MIDI_SND;

static MIDI_CHN g_chn[0x10];
static MIDI_INST *g_pProg[0x80];
static MIDI_INST *g_pDrum[0x80];
static u8 g_mrk[0x40][8];
u32 gap_08_803771B4[1];
MIDI_SND *g_pSnd;
void *g_pHdr;

void SetVoiceState_(MIDI_VOICE *voice, s32 state);

s32 AudioMIDI_Create(void)
{
    extern u8 g_song[0x24];
    extern void *g_pHdr;

    memset(g_song, 0, 0x24);
    memset(g_chn, 0, 0x200);
    g_pHdr = NULL;
    return 1;
}

s32 AudioMIDI_Init(void)
{
    extern void *g_pHdr;

    if (g_pHdr != NULL)
    {
        DestroyMIDI();
    }
    return 1;
}

void AudioMIDI_Destroy(void)
{
}


void CreateMIDI(u8 *hdr, u8 *snd)
{
    extern MIDI_SND *g_pSnd;
    extern void *g_pHdr;
    extern MIDI_INST *g_pProg[0x80];
    extern MIDI_INST *g_pDrum[0x80];
    extern s32 g_musicState;
    extern void (*DestroySongVoice)();
    extern s32 __OSCurrHeap;
    s32 CreateMIDI_(MIDI_SND *snd);
    void DestroyMIDIVoice();
    u32 i;
    s32 j;

    if (hdr == NULL || snd == NULL)
    {
        return;
    }
    i = *(u32 *)(hdr + 4) + 8;
    g_pHdr = OSAllocFromHeap(__OSCurrHeap, i + *(u32 *)(snd + 0x14));
    g_pSnd = (MIDI_SND *)((u8 *)g_pHdr + i);
    memcpy(g_pHdr, hdr, i);
    memcpy((u8 *)g_pSnd, snd, *(u32 *)(snd + 0x14));
    g_pSnd->size += (u32)HND(snd);
    FindMarkers((u8 *)g_pHdr + 0x22);
    memset(g_pProg, 0, 0x200);
    memset(g_pDrum, 0, 0x200);
    j = 0x80;
    for (i = 0; i < g_pSnd->count; i++)
    {
        if (g_pSnd->inst[i].prog != 0x80)
        {
            if (j != g_pSnd->inst[i].prog)
            {
                j = g_pSnd->inst[i].prog;
                g_pProg[j] = &g_pSnd->inst[i];
            }
        }
        else
        {
            for (j = g_pSnd->inst[i].lo; j <= g_pSnd->inst[i].hi; j++)
            {
                g_pDrum[j] = &g_pSnd->inst[i];
            }
            j = 0x80;
        }
    }
    CreateMIDI_(g_pSnd);
    g_musicState = 0;
    DestroySongVoice = DestroyMIDIVoice;
}

void DestroyMIDI(void)
{
    extern void *g_pHdr;
    extern u8 g_musicTitle[0x20];

    if (g_pHdr == NULL)
    {
        return;
    }
    SetMIDIState(3);
    DestroyMIDI_(g_pHdr);
    g_pHdr = NULL;
    memset(g_musicTitle, 0, 0x20);
}


s32 SetMIDIState(s32 state) {
 typedef struct MIDI_VOICE *MV; typedef struct MIDI_CHN MC;
 extern void *g_pHdr; extern s32 g_musicState; extern u32 SongUpdate, g_trkOn, g_trkOff;
 extern u8 g_song[0x24];
     void ProcessMIDI(s32); void SetVoiceState_(MV, s32); u32 ReadVar_(u8 **pp);
 MC *chn; MV voice, next; u32 i; s32 old;
 if (g_pHdr == NULL) return -1;
 if (state == 0 || g_pHdr == NULL) return g_musicState;
 if (state != 1) SongUpdate = 0;
 switch (state) {
 case 2:
  for (chn = g_chn, i = 0x10; i != 0; i--)
   for (voice = GETP((chn++)->voice); voice != NULL; voice = GETP(voice->next)) SetVoiceState_(voice, 2);
  break;
 case 3: case 4:
  for (chn = g_chn, i = 0x10; i != 0; i--) {
   voice = GETP(chn->voice); SETP((chn++)->voice, NULL);
   while (voice != NULL) { next = GETP(voice->next); SetVoiceState_(voice, 3); voice = next; }
  }
  if (state == 3) break;
 case 1:
  switch (g_musicState) {
  case 0: case 3: case 4:
   *(u32 *)(g_song + 0x14) = (u32)HND(g_pHdr) + 0x22;
   *(u32 *)(g_song + 0x18) = ReadVar_((u8 **)(g_song + 0x14));
   g_song[0x1C] = 8; g_song[0x1D] = 0;
   *(u32 *)(g_song + 4) = 500000 / *(u16 *)((u8 *)g_pHdr + 0x18);
   *(u32 *)(g_song + 0x10) = 0x10000; *(u32 *)(g_song + 0x20) = 0x10000;
   g_song[0x1E] = 0; g_song[0x1F] = 0;
   for (chn = g_chn, i = 0x10; i != 0; i--) {
    *(u8 *)((u8 *)chn + 4) = 1; SETP(chn->voice, NULL);
    *(u8 *)((u8 *)chn + 6) = 0; *(u8 *)((u8 *)chn + 7) = 0; *(u16 *)((u8 *)chn + 8) = 0;
    *(u8 *)((u8 *)chn + 0xA) = 0x7F; *(u8 *)((u8 *)chn + 0xB) = 0x64;
    *(u32 *)((u8 *)chn + 0xC) = 0; chn->vol = 0x4F5C; chn->note = 0x40; chn->count = 2;
    chn->pitch = 0x10000;
    *(u8 *)((u8 *)chn + 0x1E) = 0; *(u8 *)((u8 *)chn + 0x1F) = 0; chn++;
   }
   g_trkOn = 0; g_trkOff = 0;
   break;
  case 2:
   for (chn = g_chn, i = 0x10; i != 0; i--)
    for (voice = GETP((chn++)->voice); voice != NULL; voice = GETP(voice->next)) SetVoiceState_(voice, 1);
   break;
  }
  state = 1; SongUpdate = (u32)HND(ProcessMIDI);
  break;
 }
 old = g_musicState; g_musicState = state; return old;
}

void SetMIDIVol(void)
{
    MIDI_CHN *chn;
    MIDI_VOICE *voice;
    s32 i;
    void SetGVol(void *chn);

    for (chn = g_chn, i = 0x10; i != 0; chn++, i--)
    {
        SetGVol(chn);
        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
        {
            SetVoiceVol_(voice, chn->vol);
        }
    }
}



void SetMIDIPitch(u32 pitch)
{
    u32 i;
    MIDI_CHN *chn;
    MIDI_VOICE *voice;
    extern u8 g_song[0x24];
    s8 SetVoiceRate_(void *voice);

    *(u32 *)(g_song + 0x20) = pitch;
    for (chn = g_chn, i = 0x10; i != 0; chn++, i--)
    {
        chn->pitch = Mul16_(GetPitchWheel_(chn->note, chn->count), pitch);
        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
        {
            *(s32 *)((u8 *)voice + 0x34) = chn->pitch;
            SetVoiceRate_(voice);
        }
    }
}



void SetMIDITempo(u32 tempo)
{
    extern void *g_pHdr;
    extern u8 g_song[0x24];

    *(u32 *)(g_song + 0x10) = 0xFFFFFFFF / tempo;
    *(u32 *)(g_song + 4) = (u32)Mul16_(*(u32 *)(g_song + 0xC), *(u32 *)(g_song + 0x10)) / *(u16 *)((u8 *)g_pHdr + 0x18);
}

void SetMIDIMarker(u8 marker, s8 keep)
{
    extern void *g_pHdr;
    extern u8 g_mrk[0x40][8];
    extern u8 g_song[0x24];
    s32 flag;

    if (g_pHdr != NULL && marker < 0x40 && *(u32 *)g_mrk[marker] != 0)
    {
        flag = !keep;
        g_song[0x1E] = marker;
        g_song[0x1F] = flag;
        return;
    }
    g_song[0x1E] = 0;
    g_song[0x1F] = 0;
}

void SetMIDITrackPriority(void)
{
}

void SetMIDITrackOn(u32 mask, s8 deferred)
{
    extern u32 g_trkOn;
    extern u32 g_trkOff;
    u8 *chn;

    if (deferred)
    {
        g_trkOff &= ~mask;
        g_trkOn |= mask;
        return;
    }
    for (chn = (u8 *)g_chn; mask != 0; chn += 0x20)
    {
        chn[4] |= mask & 1;
        mask >>= 1;
    }
}


void SetMIDITrackOff(u32 mask, s8 deferred)
{
    extern u32 g_trkOn;
    extern u32 g_trkOff;
    MIDI_CHN *chn;

    if (deferred)
    {
        g_trkOn &= ~mask;
        g_trkOff |= mask;
        return;
    }
    for (chn = g_chn; mask != 0; chn++)
    {
        if (mask & 1)
        {
            chn->field_4 = 0;
            ChannelOff(chn);
        }
        mask >>= 1;
    }
}

void SetMIDITrackVol(void)
{
}



void ProcessMIDI(s32 time)
{
    extern u16 g_envelope[0x100];
    extern u8 g_priority[0x10];
    extern u8 g_musicTitle[0x20];
    extern void *g_pHdr;
    MIDI_VOICE *DestroyVoice(MIDI_VOICE *voice);
    void SetVoicePriority_(void *voice);
    MIDI_VOICE *GetVoice_(u8 type, u32 priority);
    void StartVoice_(void *voice);
    void SetGVol(void *chn);
    s8 SetVoiceRate_(void *voice);
    MIDI_CHN *chn;
    u32 mask;
    u32 i;
    MIDI_INST *inst;
    MIDI_VOICE *voice;
    u32 rate;
    u8 *p;

    if ((s8)g_song[0x1F] != 0)
    {
        JumpToMarker();
        g_song[0x1F] = 0;
    }
    for (chn = g_chn, i = 0x10; i != 0; chn++, i--)
    {
        voice = GETP(chn->voice);
        while (voice != NULL)
        {
            if (voice->state2 == 3)
            {
                voice = GETP(voice->next);
                continue;
            }
            voice->count += Mul16_(voice->rate, time);
            switch (voice->state2)
            {
            case 1:
                if (voice->count < 0x10000)
                {
                    voice->field_38 = voice->count >> 1;
                    break;
                }
                rate = GetEnvRate(GETP(voice->prog)->field_1A);
                if (rate != -1U)
                {
                    voice->rate = rate;
                    voice->count = 0;
                    voice->field_38 = 0x7FFF;
                    voice->state2 = 2;
                    break;
                }
                goto sustain;
            case 2:
                if (voice->count < 0x10000)
                {
                    voice->field_38 = g_envelope[voice->count >> 8];
                    if (voice->field_38 > GETP(voice->prog)->field_1C)
                    {
                        break;
                    }
                }
            sustain:
                voice->field_38 = GETP(voice->prog)->field_1C;
                if (voice->field_38 > 0)
                {
                    voice->state2 = 3;
                    break;
                }
                voice = DestroyVoice(voice);
                continue;
            case 4:
                if (voice->count < 0x10000)
                {
                    voice->field_38 = g_envelope[voice->count >> 8];
                    if (voice->field_38 > 0)
                    {
                        break;
                    }
                }
                voice = DestroyVoice(voice);
                continue;
            }
            SetVoiceVol_(voice, chn->vol);
            voice = GETP(voice->next);
        }
    }
restart:
    *(u32 *)(g_song + 8) += time;
    if (*(u32 *)(g_song + 4) != 0)
    {
        *(u32 *)g_song += *(u32 *)(g_song + 8) / *(u32 *)(g_song + 4);
        *(u32 *)(g_song + 8) %= *(u32 *)(g_song + 4);
    }
    p = SLOT_LOAD(u8 *, (g_song + 0x14));
    chn = &g_chn[g_song[0x1D]];
    mask = 0;
    while (*(u32 *)(g_song + 0x18) <= *(u32 *)g_song)
    {
    top:
        if (*p < 0xF0)
        {
            if ((s8)*p < 0)
            {
                g_song[0x1C] = (*p & 0xF0) >> 4;
                g_song[0x1D] = *p & 0xF;
                p++;
                chn = &g_chn[g_song[0x1D]];
            }
            switch (g_song[0x1C])
            {
            case 8:
            noteoff:
                for (voice = GETP(chn->voice); voice != NULL && voice->byte2 != (s8)*p; voice = GETP(voice->next))
                {
                }
                if (voice != NULL)
                {
                    if (chn->field_1F)
                    {
                        voice->byte17 = 1;
                    }
                    else
                    {
                        ReleaseMIDIVoice(voice);
                    }
                }
                p += 2;
                break;
            case 9:
                if ((s8)p[1] == 0)
                {
                    goto noteoff;
                }
                if (!chn->field_4)
                {
                    p += 2;
                    break;
                }
                if (g_song[0x1D] != 9)
                {
                    inst = g_pProg[chn->prog];
                    if (inst != NULL)
                    {
                        while (inst->prog == chn->prog)
                        {
                            if ((s8)*p >= inst->lo && (s8)*p <= inst->hi)
                            {
                                break;
                            }
                            inst++;
                        }
                        if (inst->prog != chn->prog)
                        {
                            inst = NULL;
                        }
                    }
                }
                else
                {
                    inst = g_pDrum[(s8)*p];
                }
                if (inst != NULL)
                {
                    for (voice = GETP(chn->voice); voice != NULL && voice->byte2 != (s8)*p; voice = GETP(voice->next))
                    {
                    }
                    if (voice == NULL)
                    {
                        voice = GetVoice_(0, g_priority[g_song[0x1D]] + 15);
                        if (voice == NULL)
                        {
                            p += 2;
                            break;
                        }
                        SETP(voice->next, GETP(chn->voice));
                        SETP(chn->voice, voice);
                        voice->pitch2 = chn->pitch;
                    }
                    else
                    {
                        if (voice->state2 == 4)
                        {
                            voice->priority += 15;
                            SetVoicePriority_(voice);
                        }
                        if (GETP(voice->prog) == inst)
                        {
                            goto start;
                        }
                    }
                    voice->channel = g_song[0x1D];
                    voice->byte2 = *p;
                    voice->byte2C = inst->field_11;
                    voice->field_20 = inst->field_0;
                    voice->field_24 = inst->field_8;
                    voice->field_28 = inst->field_4;
                    SETP(voice->prog, inst);
                start:
                    voice->noteRate = GetNoteRate_(inst->field_C, *p);
                    rate = GetEnvRate(inst->field_18);
                    if (rate != -1U)
                    {
                        voice->state2 = 1;
                        voice->field_38 = 0;
                    }
                    else
                    {
                        rate = GetEnvRate(inst->field_1A);
                        if (rate != -1U)
                        {
                            voice->state2 = 2;
                            voice->field_38 = 0x7FFF;
                        }
                        else
                        {
                            voice->state2 = 3;
                            rate = 0;
                            voice->field_38 = inst->field_1C;
                        }
                    }
                    voice->count = 0;
                    voice->rate = rate;
                    voice->field_3C = (s8)p[1] << 8;
                    voice->field_40 = chn->field_C;
                    voice->field_44 = -0x7FFF;
                    voice->field_48 = chn->field_10;
                    StartVoice_(voice);
                    SetVoiceVol_(voice, chn->vol);
                }
                p += 2;
                break;
            case 0xA:
                p += 2;
                break;
            case 0xB:
                switch ((s8)*p++)
                {
                case 0x01:
                    chn->field_1E = *p;
                    break;
                case 0x06:
                    switch (chn->field_8)
                    {
                    case 1:
                        chn->count = *p;
                        break;
                    }
                    break;
                case 0x07:
                    chn->field_B = *p;
                    SetGVol(chn);
                    mask |= 1 << g_song[0x1D];
                    break;
                case 0x0A:
                    chn->field_C = (s16)(((s8)*p ^ 0x40) << 9);
                    mask |= 1 << g_song[0x1D];
                    break;
                case 0x0B:
                    chn->field_A = *p;
                    SetGVol(chn);
                    mask |= 1 << g_song[0x1D];
                    break;
                case 0x40:
                    if (chn->field_1F && (s8)*p < 0x40)
                    {
                        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                        {
                            if (voice->byte17)
                            {
                                ReleaseMIDIVoice(voice);
                            }
                        }
                    }
                    chn->field_1F = (s8)*p >> 6;
                    break;
                case 0x5B:
                    chn->field_10 = (s8)*p << 8;
                    for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                    {
                        voice->field_48 = chn->field_10;
                    }
                    mask |= 1 << g_song[0x1D];
                    break;
                case 0x64:
                    chn->field_8 = (chn->field_8 & ~0x7F) | (s8)*p;
                    break;
                case 0x65:
                    chn->field_8 = (chn->field_8 & 0x7F) | ((u16)(s8)*p << 7);
                    break;
                case 0x6E:
                case 0x6F:
                case 0x70:
                    if (g_song[0x1E] != 0)
                    {
                        JumpToMarker();
                        goto restart;
                    }
                    if ((s32)p[-1] == 0x70)
                    {
                        i = *(s8 *)p;
                        if (i < 0x40 && *(u32 *)g_mrk[i] != 0)
                        {
                            g_song[0x1E] = i;
                            JumpToMarker();
                            goto restart;
                        }
                    }
                    break;
                case 0x79:
                    chn->field_A = 0x7F;
                    chn->field_B = 0x64;
                    chn->field_C = 0;
                    chn->field_10 = 0;
                    chn->count = 2;
                    chn->pitch = 0x10000;
                    if (chn->field_1F)
                    {
                        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                        {
                            if (voice->byte17)
                            {
                                ReleaseMIDIVoice(voice);
                            }
                        }
                        chn->field_1F = 0;
                    }
                    break;
                case 0x7A:
                    break;
                case 0x7B:
                    voice = GETP(chn->voice);
                    if (chn->field_1F)
                    {
                        for (; voice != NULL; voice = GETP(voice->next))
                        {
                            voice->byte17 = 1;
                        }
                    }
                    else
                    {
                        ChannelOff(chn);
                    }
                    break;
                }
                p++;
                break;
            case 0xC:
                chn->prog = *p++;
                break;
            case 0xD:
                p++;
                break;
            case 0xE:
                chn->note = p[1];
                chn->pitch = GetPitchWheel_(chn->note, chn->count);
                p += 2;
                for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                {
                    voice->pitch2 = chn->pitch;
                    SetVoiceRate_(voice);
                }
                break;
            }
        }
        else if ((*p & 0xF) == 0xF)
        {
            p++;
            switch ((s8)*p++)
            {
            case 0x00:
                break;
            case 0x03:
                rate = ReadVar_(&p);
                memcpy(g_musicTitle, p, rate < 0x20 ? rate : 0x1F);
                p += rate;
                break;
            case 0x2F:
                p++;
                *(u32 *)(g_song + 0x18) = -1;
                continue;
            case 0x51:
                p++;
                *(u32 *)(g_song + 0xC) = (p[0] << 16) | (p[1] << 8) | p[2];
                p += 3;
                *(u32 *)(g_song + 4) = (u32)Mul16_(*(u32 *)(g_song + 0xC), *(u32 *)(g_song + 0x10)) / *(u16 *)((u8 *)g_pHdr + 0x18);
                break;
            case 0x54:
            case 0x58:
            case 0x59:
                p += 1 + *p;
                break;
            default:
                p += ReadVar_(&p);
                break;
            }
        }
        else
        {
            switch (*p++ & 0xF)
            {
            case 0:
            case 4:
            case 5:
                while ((s8)*p++ >= -8)
                {
                }
                if ((s8)p[-1] != 0xF7)
                {
                    p--;
                    goto top;
                }
                break;
            case 1:
            case 3:
                p++;
                break;
            case 2:
                p += 2;
                break;
            }
        }
        *(u32 *)(g_song + 0x18) += ReadVar_(&p);
    }
    for (chn = g_chn; mask != 0; mask >>= 1, chn++)
    {
        if (mask & 1)
        {
            for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
            {
                SetVoiceVol_(voice, chn->vol);
            }
        }
    }
    SLOT_STORE((g_song + 0x14), p);
}



u32 GetEnvRate(u16 arg0)
{
    if (arg0 != 0)
    {
        return -1U / (u32)(arg0 * 0x3E8);
    }
    return -1U;
}

extern s32 g_musicVol;
void SetGVol(MIDI_CHN *chn)
{
    u32 vol = chn->field_B * chn->field_A;
    vol = (vol * vol) / 3970;
    chn->vol = Mul16_(vol, g_musicVol);
}


void ReleaseMIDIVoice(void *voice)
{
    void SetVoicePriority_(void *voice);
    u32 rate;

    if (*(u8 *)((u8 *)voice + 0x16) != 4)
    {
        *(u8 *)((u8 *)voice + 0x16) = 4;
        *(u8 *)((u8 *)voice + 0x17) = 0;
        rate = GetEnvRate(*(u16 *)((u8 *)PTR((*(u32 *)((u8 *)voice + 0x10))) + 0x1E));
        if (rate + 0x10000 == 0xFFFF)
        {
            rate = 0;
            *(s32 *)((u8 *)voice + 0x1C) = 0x10000;
        }
        *(u32 *)((u8 *)voice + 0x18) = rate;
        *(u8 *)((u8 *)voice + 0x14) -= 15;
        SetVoicePriority_(voice);
    }
}


void ChannelOff(void *chn)
{
    void *v;

    for (v = SLOT_LOAD(void *, chn); v != NULL; v = SLOT_LOAD(void *, ((u8 *)v + 4)))
    {
        ReleaseMIDIVoice(v);
    }
}




void DestroyMIDIVoice(MIDI_VOICE *voice)
{
    MIDI_VOICE *v;

    v = GETP(g_chn[voice->channel].voice);
    if (v == voice)
    {
        SETP(g_chn[voice->channel].voice, GETP(voice->next));
    }
    else
    {
        for (; GETP(v->next) != voice; v = GETP(v->next))
        {
        }
        SETP(v->next, GETP(voice->next));
    }
    voice->state = 0;
    voice->channel = 0xFF;
}
MIDI_VOICE *DestroyVoice(MIDI_VOICE *voice)
{
    MIDI_VOICE *next;
    MIDI_VOICE *v;

    next = GETP(voice->next);
    v = GETP(g_chn[voice->channel].voice);
    if (v == voice)
    {
        SETP(g_chn[voice->channel].voice, next);
    }
    else
    {
        for (; GETP(v->next) != voice; v = GETP(v->next))
        {
        }
        SETP(v->next, next);
    }
    SetVoiceState_(voice, 3);
    return next;
}



void FindMarkers(u8 *p)
{
    extern u8 g_mrk[0x40][8];
    u32 n;
    u8 status;
    u8 chan;

    n = 1;
    status = 8;
    chan = 0;
    SLOT_STORE(g_mrk[0], p);
    g_mrk[0][4] = status;
    g_mrk[0][5] = chan;
    for (;;)
    {
        if (*p < 0xF0)
        {
            if ((s8)*p < 0)
            {
                status = (*p & 0xF0) >> 4;
                chan = *p & 0xF;
                p++;
            }
            if (status == 0xB)
            {
                p += 2;
                if ((s32)p[-2] == 0x6E)
                {
                    SLOT_STORE(g_mrk[n], p);
                    g_mrk[n][4] = status;
                    g_mrk[n][5] = chan;
                    if (++n >= 0x40)
                    {
                        return;
                    }
                }
            }
            else
            {
                p++;
                if (status < 0xB || status == 0xE)
                {
                    p++;
                }
            }
        }
        else if ((*p & 0xF) == 0xF)
        {
            p++;
            switch ((s8)*p++)
            {
            case 0x2F:
                return;
            case 0x51:
                p += 4;
                break;
            case 0x54:
            case 0x58:
            case 0x59:
                p += *p;
                p++;
                break;
            default:
                p += ReadVar_(&p);
                break;
            }
        }
        else
        {
            switch (*p++ & 0xF)
            {
            case 0:
            case 4:
            case 5:
                while ((s8)*p++ >= -8)
                {
                }
                if ((s8)p[-1] != 0xF7)
                {
                    p--;
                    continue;
                }
                break;
            case 2:
                p += 2;
                break;
            case 1:
            case 3:
                p++;
                break;
            }
        }
        ReadVar_(&p);
    }
}

void JumpToMarker(void)
{
    extern u32 g_trkOn;
    extern u32 g_trkOff;
    void SetVoicePriority_(void *voice);
    MIDI_CHN *chn;
    MIDI_VOICE *voice;
    u32 i;
    u32 rate;
    u16 env;
    MIDI_CHN *c;
    u32 mask;

    for (c = g_chn, mask = g_trkOn; mask != 0; c++)
    {
        c->field_4 |= mask & 1;
        mask >>= 1;
    }
    for (chn = g_chn; g_trkOff != 0; chn++)
    {
        chn->field_4 &= ~(g_trkOff & 1);
    }
    g_trkOn = 0;
    g_trkOff = 0;
    SETP(((MIDI_SONG *)g_song)->field_14, SLOT_LOAD(u8 *, g_mrk[((MIDI_SONG *)g_song)->field_1E]));
    ((MIDI_SONG *)g_song)->field_0 = 0;
    ((MIDI_SONG *)g_song)->field_8 = 0;
    ((MIDI_SONG *)g_song)->field_18 = ReadVar_(&((MIDI_SONG *)g_song)->field_14);
    ((MIDI_SONG *)g_song)->field_1D = (&g_mrk[((MIDI_SONG *)g_song)->field_1E][4])[1];
    ((MIDI_SONG *)g_song)->field_1C = g_mrk[((MIDI_SONG *)g_song)->field_1E][4];
    for (i = 0x10, chn = g_chn; i != 0; chn++, i--)
    {
        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
        {
            if (voice->state2 != 4)
            {
                voice->state2 = 4;
                voice->byte17 = 0;
                env = *(u16 *)((u8 *)GETP(voice->prog) + 0x1E);
                if (env != 0)
                {
                    rate = -1U / (u32)(env * 0x3E8);
                }
                else
                {
                    rate = -1U;
                }
                if (rate + 0x10000 == 0xFFFF)
                {
                    voice->count = 0x10000;
                    rate = 0;
                }
                voice->rate = rate;
                voice->priority -= 15;
                SetVoicePriority_(voice);
            }
        }
        chn->field_1F = 0;
        chn->note = 0x40;
        chn->pitch = 0x10000;
    }
    ((MIDI_SONG *)g_song)->field_1E = 0;
}


