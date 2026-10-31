#include "common.h"
#include "Voice.h"

/* A MIDI song (RIFX 'MIDI': a standard MIDI file with one track) and its
 * sound bank (RIFX 'sndb', MIDI_SND), played by ProcessMIDI. */
typedef struct __attribute__((packed)) MIDIHeader {
    char riff[4];               /* 0x00 "RIFX" */
    u32 size;                   /* 0x04 after this */
    char form[4];               /* 0x08 "MIDI" */
    char mthd[4];               /* 0x0C "MThd" */
    u32 mthdSize;               /* 0x10 */
    u16 format;                 /* 0x14 */
    u16 numTracks;              /* 0x16 */
    u16 division;               /* 0x18 ticks per quarter note */
    char mtrk[4];               /* 0x1A "MTrk" */
    u32 trackSize;              /* 0x1E */
    u8 track[];                 /* 0x22 the events */
} MIDIHeader;
_Static_assert(offsetof(MIDIHeader, division) == 0x18, "MIDIHeader.division");
_Static_assert(offsetof(MIDIHeader, track) == 0x22, "MIDIHeader.track");

/* A MIDI channel. */
typedef struct MIDI_CHN
{
    PTR32(struct Voice) voice;  /* 0x00 its voices */
    s8 on;                      /* 0x04 track not muted (SetMIDITrackOn/Off) */
    u8 pad5;                    /* 0x05 */
    u8 prog;                    /* 0x06 program */
    u8 pad7;                    /* 0x07 */
    u16 rpn;                    /* 0x08 registered parameter (CC 0x64/0x65) */
    u8 expression;              /* 0x0A CC 0x0B */
    u8 volume;                  /* 0x0B CC 0x07 */
    s32 pan;                    /* 0x0C CC 0x0A */
    s32 reverb;                 /* 0x10 CC 0x5B: Voice.auxVol */
    u32 vol;                    /* 0x14 volume x expression x music volume (SetGVol) */
    u32 pitch;                  /* 0x18 16.16 */
    u8 bendRange;               /* 0x1C semitones (RPN 0) */
    u8 wheel;                   /* 0x1D pitch wheel, 0x40: centre */
    u8 modulation;              /* 0x1E CC 0x01 */
    s8 sustain;                 /* 0x1F CC 0x40 pedal down */
} MIDI_CHN;
_Static_assert(sizeof(MIDI_CHN) == 0x20, "MIDI_CHN size");

/* The playing song (g_song). */
typedef struct MIDISong
{
    u32 tick;                   /* 0x00 ticks played */
    u32 tickTime;               /* 0x04 frame time per tick */
    u32 time;                   /* 0x08 frame time not yet a tick */
    u32 tempo;                  /* 0x0C us per quarter note (meta event 0x51) */
    u32 tempoScale;             /* 0x10 SetMIDITempo */
    PTR32(u8) pos;              /* 0x14 next event */
    u32 nextTick;               /* 0x18 of the next event, -1: the end */
    u8 status;                  /* 0x1C running status: event type */
    u8 channel;                 /* 0x1D and channel */
    u8 marker;                  /* 0x1E jump to this marker (SetMIDIMarker) */
    s8 jumpNow;                 /* 0x1F at once, else at the next marker event */
    u32 pitch;                  /* 0x20 16.16 (SetMIDIPitch) */
} MIDISong;
_Static_assert(sizeof(MIDISong) == 0x24, "MIDISong size");

/* A loop marker: CC 0x6E in the track (FindMarkers). */
typedef struct MIDIMarker
{
    PTR32(u8) pos;              /* 0x00 NULL: none */
    u8 status;                  /* 0x04 running status there */
    u8 channel;                 /* 0x05 */
    u8 pad6[2];                 /* 0x06 */
} MIDIMarker;

MIDISong g_song;

typedef struct MIDI_INST
{
    u32 addr;                   /* 0x00 sample, as SFX */
    u32 length;                 /* 0x04 */
    u32 loop;                   /* 0x08 */
    u32 rate;                   /* 0x0C at the base note (GetNoteRate_) */
    u8 field_10;                /* 0x10 */
    u8 bits;                    /* 0x11 */
    u8 field_12;                /* 0x12 */
    u8 field_13;                /* 0x13 */
    u8 prog;                    /* 0x14 */
    u8 field_15;                /* 0x15 */
    u8 lo;                      /* 0x16 note range */
    u8 hi;                      /* 0x17 */
    u16 attack;                 /* 0x18 envelope rates (GetEnvRate), and the sustain level */
    u16 decay;                  /* 0x1A */
    u16 sustain;                /* 0x1C */
    u16 release;                /* 0x1E */
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
static MIDIMarker g_mrk[0x40];
u32 gap_08_803771B4[1];
MIDI_SND *g_pSnd;
MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */


s32 AudioMIDI_Create(void)
{
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */

    memset(&g_song, 0, sizeof(g_song));
    memset(g_chn, 0, 0x200);
    g_pHdr = NULL;
    return 1;
}

s32 AudioMIDI_Init(void)
{
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */

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
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */
    extern MIDI_INST *g_pProg[0x80];
    extern MIDI_INST *g_pDrum[0x80];
    extern s32 g_musicState;
    extern void (*DestroySongVoice)();
    extern s32 __OSCurrHeap;
    u32 i;
    s32 j;

    if (hdr == NULL || snd == NULL)
    {
        return;
    }
    /* port: the song (RIFX 'MIDI') and bank (RIFX 'sndb') are big-endian in
     * the pack; only the copies below are swapped (PortSwap_MIDI) */
    i = Port_ReadBE32(hdr + 4) + 8;
    g_pHdr = OSAllocFromHeap(__OSCurrHeap, i + Port_ReadBE32(snd + 0x14));
    g_pSnd = (MIDI_SND *)((u8 *)g_pHdr + i);
    memcpy(g_pHdr, hdr, i);
    memcpy((u8 *)g_pSnd, snd, Port_ReadBE32(snd + 0x14));
    PortSwap_MIDI(g_pHdr, (u8 *)g_pSnd);
    g_pSnd->size += (u32)HND(snd);
    FindMarkers(g_pHdr->track);
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
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */
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
 typedef struct Voice *MV; typedef struct MIDI_CHN MC;
 extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */ extern s32 g_musicState; extern u32 g_trkOn, g_trkOff;
 extern void (*volatile SongUpdate)(void);  /* port: a host function pointer (Audio.c) */
     void ProcessMIDI(s32); u32 ReadVar_(u8 **pp);
 MC *chn; MV voice, next; u32 i; s32 old;
 if (g_pHdr == NULL) return -1;
 if (state == 0 || g_pHdr == NULL) return g_musicState;
 if (state != 1) SongUpdate = NULL;
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
   SETP(g_song.pos, g_pHdr->track);
   {
    u8 *pos = GETP(g_song.pos);
    g_song.nextTick = ReadVar_(&pos);
    SETP(g_song.pos, pos);
   }
   g_song.status = 8; g_song.channel = 0;
   g_song.tickTime = 500000 / g_pHdr->division;
   g_song.tempoScale = 0x10000; g_song.pitch = 0x10000;
   g_song.marker = 0; g_song.jumpNow = 0;
   for (chn = g_chn, i = 0x10; i != 0; i--) {
    chn->on = 1; SETP(chn->voice, NULL);
    chn->prog = 0; chn->pad7 = 0; chn->rpn = 0;
    chn->expression = 0x7F; chn->volume = 0x64;
    chn->pan = 0; chn->vol = 0x4F5C; chn->wheel = 0x40; chn->bendRange = 2;
    chn->pitch = 0x10000;
    chn->modulation = 0; chn->sustain = 0; chn++;
   }
   g_trkOn = 0; g_trkOff = 0;
   break;
  case 2:
   for (chn = g_chn, i = 0x10; i != 0; i--)
    for (voice = GETP((chn++)->voice); voice != NULL; voice = GETP(voice->next)) SetVoiceState_(voice, 1);
   break;
  }
  state = 1; SongUpdate = (void (*)(void))ProcessMIDI;  /* called with the frame time (Audio_Update) */
  break;
 }
 old = g_musicState; g_musicState = state; return old;
}
void SetGVol(MIDI_CHN *chn);

void SetMIDIVol(void)
{
    MIDI_CHN *chn;
    Voice *voice;
    s32 i;

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
    Voice *voice;

    g_song.pitch = pitch;
    for (chn = g_chn, i = 0x10; i != 0; chn++, i--)
    {
        chn->pitch = Mul16_(GetPitchWheel_(chn->wheel, chn->bendRange), pitch);
        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
        {
            voice->pitch = chn->pitch;
            SetVoiceRate_(voice);
        }
    }
}



void SetMIDITempo(u32 tempo)
{
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */

    g_song.tempoScale = 0xFFFFFFFF / tempo;
    g_song.tickTime = (u32)Mul16_(g_song.tempo, g_song.tempoScale) / g_pHdr->division;
}

void SetMIDIMarker(u8 marker, s8 keep)
{
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */
    s32 flag;

    if (g_pHdr != NULL && marker < 0x40 && GETP(g_mrk[marker].pos) != NULL)
    {
        flag = !keep;
        g_song.marker = marker;
        g_song.jumpNow = flag;
        return;
    }
    g_song.marker = 0;
    g_song.jumpNow = 0;
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
            chn->on = 0;
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
    extern MIDIHeader *g_pHdr;  /* also the IT module's (IT.c), one song at a time */
    Voice *DestroyVoice(Voice *voice);
    MIDI_CHN *chn;
    u32 mask;
    u32 i;
    MIDI_INST *inst;
    Voice *voice;
    u32 rate;
    u8 *p;

    if (g_song.jumpNow != 0)
    {
        JumpToMarker();
        g_song.jumpNow = 0;
    }
    for (chn = g_chn, i = 0x10; i != 0; chn++, i--)
    {
        voice = GETP(chn->voice);
        while (voice != NULL)
        {
            if (voice->envStage == 3)
            {
                voice = GETP(voice->next);
                continue;
            }
            voice->envCount += Mul16_(voice->envRate, time);
            switch (voice->envStage)
            {
            case 1:
                if (voice->envCount < 0x10000)
                {
                    voice->vol = voice->envCount >> 1;
                    break;
                }
                rate = GetEnvRate(GETP(voice->inst)->decay);
                if (rate != -1U)
                {
                    voice->envRate = rate;
                    voice->envCount = 0;
                    voice->vol = 0x7FFF;
                    voice->envStage = 2;
                    break;
                }
                goto sustain;
            case 2:
                if (voice->envCount < 0x10000)
                {
                    voice->vol = g_envelope[voice->envCount >> 8];
                    if (voice->vol > GETP(voice->inst)->sustain)
                    {
                        break;
                    }
                }
            sustain:
                voice->vol = GETP(voice->inst)->sustain;
                if (voice->vol > 0)
                {
                    voice->envStage = 3;
                    break;
                }
                voice = DestroyVoice(voice);
                continue;
            case 4:
                if (voice->envCount < 0x10000)
                {
                    voice->vol = g_envelope[voice->envCount >> 8];
                    if (voice->vol > 0)
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
    g_song.time += time;
    if (g_song.tickTime != 0)
    {
        g_song.tick += g_song.time / g_song.tickTime;
        g_song.time %= g_song.tickTime;
    }
    p = GETP(g_song.pos);
    chn = &g_chn[g_song.channel];
    mask = 0;
    while (g_song.nextTick <= g_song.tick)
    {
    top:
        if (*p < 0xF0)
        {
            if ((s8)*p < 0)
            {
                g_song.status = (*p & 0xF0) >> 4;
                g_song.channel = *p & 0xF;
                p++;
                chn = &g_chn[g_song.channel];
            }
            switch (g_song.status)
            {
            case 8:
            noteoff:
                for (voice = GETP(chn->voice); voice != NULL && voice->note != (s8)*p; voice = GETP(voice->next))
                {
                }
                if (voice != NULL)
                {
                    if (chn->sustain)
                    {
                        voice->released = 1;
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
                if (!chn->on)
                {
                    p += 2;
                    break;
                }
                if (g_song.channel != 9)
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
                    for (voice = GETP(chn->voice); voice != NULL && voice->note != (s8)*p; voice = GETP(voice->next))
                    {
                    }
                    if (voice == NULL)
                    {
                        voice = GetVoice_(0, g_priority[g_song.channel] + 15);
                        if (voice == NULL)
                        {
                            p += 2;
                            break;
                        }
                        SETP(voice->next, GETP(chn->voice));
                        SETP(chn->voice, voice);
                        voice->pitch = chn->pitch;
                    }
                    else
                    {
                        if (voice->envStage == 4)
                        {
                            voice->priority += 15;
                            SetVoicePriority_(voice);
                        }
                        if (GETP(voice->inst) == inst)
                        {
                            goto start;
                        }
                    }
                    voice->owner = g_song.channel;
                    voice->note = *p;
                    voice->bits = inst->bits;
                    voice->addr = inst->addr;
                    voice->loop = inst->loop;
                    voice->length = inst->length;
                    SETP(voice->inst, inst);
                start:
                    voice->rate = GetNoteRate_(inst->rate, *p);
                    rate = GetEnvRate(inst->attack);
                    if (rate != -1U)
                    {
                        voice->envStage = 1;
                        voice->vol = 0;
                    }
                    else
                    {
                        rate = GetEnvRate(inst->decay);
                        if (rate != -1U)
                        {
                            voice->envStage = 2;
                            voice->vol = 0x7FFF;
                        }
                        else
                        {
                            voice->envStage = 3;
                            rate = 0;
                            voice->vol = inst->sustain;
                        }
                    }
                    voice->envCount = 0;
                    voice->envRate = rate;
                    voice->mixVol = (s8)p[1] << 8;
                    voice->pan = chn->pan;
                    voice->span = -0x7FFF;
                    voice->auxVol = chn->reverb;
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
                    chn->modulation = *p;
                    break;
                case 0x06:
                    switch (chn->rpn)
                    {
                    case 1:
                        chn->bendRange = *p;
                        break;
                    }
                    break;
                case 0x07:
                    chn->volume = *p;
                    SetGVol(chn);
                    mask |= 1 << g_song.channel;
                    break;
                case 0x0A:
                    chn->pan = (s16)(((s8)*p ^ 0x40) << 9);
                    mask |= 1 << g_song.channel;
                    break;
                case 0x0B:
                    chn->expression = *p;
                    SetGVol(chn);
                    mask |= 1 << g_song.channel;
                    break;
                case 0x40:
                    if (chn->sustain && (s8)*p < 0x40)
                    {
                        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                        {
                            if (voice->released)
                            {
                                ReleaseMIDIVoice(voice);
                            }
                        }
                    }
                    chn->sustain = (s8)*p >> 6;
                    break;
                case 0x5B:
                    chn->reverb = (s8)*p << 8;
                    for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                    {
                        voice->auxVol = chn->reverb;
                    }
                    mask |= 1 << g_song.channel;
                    break;
                case 0x64:
                    chn->rpn = (chn->rpn & ~0x7F) | (s8)*p;
                    break;
                case 0x65:
                    chn->rpn = (chn->rpn & 0x7F) | ((u16)(s8)*p << 7);
                    break;
                case 0x6E:
                case 0x6F:
                case 0x70:
                    if (g_song.marker != 0)
                    {
                        JumpToMarker();
                        goto restart;
                    }
                    if ((s32)p[-1] == 0x70)
                    {
                        i = *(s8 *)p;
                        if (i < 0x40 && GETP(g_mrk[i].pos) != NULL)
                        {
                            g_song.marker = i;
                            JumpToMarker();
                            goto restart;
                        }
                    }
                    break;
                case 0x79:
                    chn->expression = 0x7F;
                    chn->volume = 0x64;
                    chn->pan = 0;
                    chn->reverb = 0;
                    chn->bendRange = 2;
                    chn->pitch = 0x10000;
                    if (chn->sustain)
                    {
                        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                        {
                            if (voice->released)
                            {
                                ReleaseMIDIVoice(voice);
                            }
                        }
                        chn->sustain = 0;
                    }
                    break;
                case 0x7A:
                    break;
                case 0x7B:
                    voice = GETP(chn->voice);
                    if (chn->sustain)
                    {
                        for (; voice != NULL; voice = GETP(voice->next))
                        {
                            voice->released = 1;
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
                chn->wheel = p[1];
                chn->pitch = GetPitchWheel_(chn->wheel, chn->bendRange);
                p += 2;
                for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
                {
                    voice->pitch = chn->pitch;
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
                g_song.nextTick = -1;
                continue;
            case 0x51:
                p++;
                g_song.tempo = (p[0] << 16) | (p[1] << 8) | p[2];
                p += 3;
                g_song.tickTime = (u32)Mul16_(g_song.tempo, g_song.tempoScale) / g_pHdr->division;
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
                if ((s8)p[-1] != 0xF7)  /* sic: a sign-extended byte is never 0xF7 (extsb; cmpwi 0xf7) */
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
        g_song.nextTick += ReadVar_(&p);
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
    SETP(g_song.pos, p);
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
    u32 vol = chn->volume * chn->expression;
    vol = (vol * vol) / 3970;
    chn->vol = Mul16_(vol, g_musicVol);
}


void ReleaseMIDIVoice(Voice *voice)
{
    u32 rate;

    if (voice->envStage != 4)
    {
        voice->envStage = 4;
        voice->released = 0;
        rate = GetEnvRate(GETP(voice->inst)->release);
        if (rate + 0x10000 == 0xFFFF)
        {
            rate = 0;
            voice->envCount = 0x10000;
        }
        voice->envRate = rate;
        voice->priority -= 15;
        SetVoicePriority_(voice);
    }
}


void ChannelOff(void *chn)
{
    Voice *v;

    for (v = GETP(((MIDI_CHN *)chn)->voice); v != NULL; v = GETP(v->next))
    {
        ReleaseMIDIVoice(v);
    }
}




void DestroyMIDIVoice(Voice *voice)
{
    Voice *v;

    v = GETP(g_chn[voice->owner].voice);
    if (v == voice)
    {
        SETP(g_chn[voice->owner].voice, GETP(voice->next));
    }
    else
    {
        for (; GETP(v->next) != voice; v = GETP(v->next))
        {
        }
        SETP(v->next, GETP(voice->next));
    }
    voice->state = 0;
    voice->owner = 0xFF;
}
Voice *DestroyVoice(Voice *voice)
{
    Voice *next;
    Voice *v;

    next = GETP(voice->next);
    v = GETP(g_chn[voice->owner].voice);
    if (v == voice)
    {
        SETP(g_chn[voice->owner].voice, next);
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
    u32 n;
    u8 status;
    u8 chan;

    n = 1;
    status = 8;
    chan = 0;
    SETP(g_mrk[0].pos, p);
    g_mrk[0].status = status;
    g_mrk[0].channel = chan;
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
                    SETP(g_mrk[n].pos, p);
                    g_mrk[n].status = status;
                    g_mrk[n].channel = chan;
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
                if ((s8)p[-1] != 0xF7)  /* sic: a sign-extended byte is never 0xF7 (extsb; cmpwi 0xf7) */
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
    MIDI_CHN *chn;
    Voice *voice;
    u32 i;
    u32 rate;
    u16 env;
    MIDI_CHN *c;
    u32 mask;

    for (c = g_chn, mask = g_trkOn; mask != 0; c++)
    {
        c->on |= mask & 1;
        mask >>= 1;
    }
    for (chn = g_chn; g_trkOff != 0; chn++)
    {
        chn->on &= ~(g_trkOff & 1);
    }
    g_trkOn = 0;
    g_trkOff = 0;
    SETP(g_song.pos, GETP(g_mrk[g_song.marker].pos));
    g_song.tick = 0;
    g_song.time = 0;
    {
        u8 *pos = GETP(g_song.pos);
        g_song.nextTick = ReadVar_(&pos);
        SETP(g_song.pos, pos);
    }
    g_song.channel = g_mrk[g_song.marker].channel;
    g_song.status = g_mrk[g_song.marker].status;
    for (i = 0x10, chn = g_chn; i != 0; chn++, i--)
    {
        for (voice = GETP(chn->voice); voice != NULL; voice = GETP(voice->next))
        {
            if (voice->envStage != 4)
            {
                voice->envStage = 4;
                voice->released = 0;
                env = GETP(voice->inst)->release;
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
                    voice->envCount = 0x10000;
                    rate = 0;
                }
                voice->envRate = rate;
                voice->priority -= 15;
                SetVoicePriority_(voice);
            }
        }
        chn->sustain = 0;
        chn->wheel = 0x40;
        chn->pitch = 0x10000;
    }
    g_song.marker = 0;
}


