

#include "common.h"

typedef struct IT_VOICE
{
    u8 byte0;
    u8 track;
    u8 pad_2_3[2];
    PTR32(struct IT_VOICE) next;
    u8 pad_8_3B[0x34];
    s32 vol;
    s32 pan;
    u8 pad_44_6F[0x2C];
} IT_VOICE;

typedef struct IT_TRACK
{
    PTR32(struct IT_VOICE) head;   /* 0x00 */
    PTR32(struct IT_VOICE) active; /* 0x04 */
    PTR32(u8) smp;                 /* 0x08 */
    PTR32(u8) ins;                 /* 0x0C */
    u8 index;                /* 0x10 */
    u8 state;                /* 0x11 */
    u8 priority;             /* 0x12 */
    u8 mask;                 /* 0x13 */
    u8 note;                 /* 0x14 */
    u8 insNum;               /* 0x15 */
    u8 volCol;               /* 0x16 */
    u8 cmd;                  /* 0x17 */
    u8 param;                /* 0x18 */
    u8 realNote;             /* 0x19 */
    s16 gvol;                /* 0x1A */
    s32 chVol;               /* 0x1C */
    u8 pad_20_27[8];
    s32 vol;                 /* 0x28 */
    s32 pan;                 /* 0x2C */
    u8 pad_30_37[8];
    s32 panSep;              /* 0x38 */
    u8 pad_3C_43[8];
    s32 curVol;              /* 0x44 */
    s32 volBase;             /* 0x48 */
    s32 volSlide;            /* 0x4C */
    s32 period;              /* 0x50 */
    s32 slideSpeed;          /* 0x54 */
    s32 targetPeriod;        /* 0x58 */
    u8 memVolSlide;          /* 0x5C */
    u8 memChVolSlide;        /* 0x5D */
    u8 memPanSlide;          /* 0x5E */
    u8 memPitchSlide;        /* 0x5F */
    u8 memArp;               /* 0x60 */
    u8 memVibrato;           /* 0x61 */
    u8 pad_62;
    u8 memTremor;            /* 0x63 */
    u8 memOffset;            /* 0x64 */
    u8 memTempo;             /* 0x65 */
    u8 memSpecial;           /* 0x66 */
    u8 pad_67;
    u32 flags;               /* 0x68 */
    u32 prevFlags;           /* 0x6C */
} IT_TRACK;

static s32 g_song[0xC];
static IT_TRACK g_trk[0x20];
static u8 *g_pIns[100];
static u8 *g_pSmp[100];
static u8 *g_pPtrn[200];

void SetVoiceState_(IT_VOICE *voice, s32 state);
void StopActive(IT_TRACK *trk);
void StartVoice_(void *voice);
void *GetVoice_(u8 type, u32 priority);
s32 PlayNote(IT_TRACK *trk);
void SetTrackVol(IT_TRACK *trk);
void SetTrackPan(IT_TRACK *trk);




s32 AudioIT_Create(void)
{
    extern void *g_pHdr;
    u8 i;

    memset(g_song, 0, sizeof(g_song));
    memset(g_trk, 0, sizeof(g_trk));
    for (i = 0; i < 0x20; i++)
    {
        g_trk[i].index = i;
    }
    g_pHdr = NULL;
    return 1;
}


s32 AudioIT_Init(void)
{
    extern void *g_pHdr;
    u32 i;
    IT_TRACK *trk;

    if (g_pHdr != NULL)
    {
        DestroyIT();
    }
    g_song[7] = 0x10000;
    trk = g_trk;
    for (i = 0x20; i != 0; i--)
    {
        SETP(trk->head, NULL);
        SETP(trk->active, NULL);
    }
    return 1;
}


void AudioIT_Destroy(s32 unused) {

}


void CreateIT(u8 *data, u32 arg1)
{
    extern u8 *g_pHdr;
    extern u8 *g_pOrder;
    extern u8 g_musicTitle[0x20];
    extern void (*DestroySongVoice)(IT_VOICE *voice);
    extern s32 __OSCurrHeap;
    void CreateIT_(u8 *hdr, u8 **smp);
    void DestroyITVoice(IT_VOICE *voice);
    s32 i;
    u32 *insOff;
    u32 *smpOff;
    u32 *patOff;

    if (data == NULL || arg1 == 0)
    {
        return;
    }
    i = *(u32 *)(data + 0x38);
    g_pHdr = OSAllocFromHeap(__OSCurrHeap, i);
    memcpy(g_pHdr, data, i);
    data = g_pHdr;
    *(s32 *)(g_pHdr + 0x3C) = 1;
    g_pOrder = data + 0xC0;
    insOff = (u32 *)(data + *(u16 *)(g_pHdr + 0x20));
    insOff += 0x30;
    smpOff = insOff + *(u16 *)(g_pHdr + 0x22);
    patOff = smpOff + *(u16 *)(g_pHdr + 0x24);
    for (i = 0; i < *(u16 *)(g_pHdr + 0x22); i++)
    {
        g_pIns[i] = data + insOff[i];
    }
    for (i = 0; i < *(u16 *)(g_pHdr + 0x24); i++)
    {
        g_pSmp[i] = data + smpOff[i];
    }
    for (i = 0; i < *(u16 *)(g_pHdr + 0x26); i++)
    {
        g_pPtrn[i] = data + patOff[i];
    }
    *(u32 *)(g_pSmp[0] + 0x48) = arg1;
    CreateIT_(g_pHdr, g_pSmp);
    memcpy(g_musicTitle, g_pHdr + 4, 0x1A);
    g_musicTitle[0x1A] = 0;
    DestroySongVoice = DestroyITVoice;
}



void DestroyIT(void)
{
    extern void *g_pHdr;
    extern u8 g_musicTitle[0x20];

    if (g_pHdr == NULL)
    {
        return;
    }
    SetITState(3);
    DestroyIT_(g_pHdr);
    g_pHdr = NULL;
    memset(g_musicTitle, 0, 0x20);
}



s32 SetITState(s32 state)
{
    extern u8 *g_pHdr;
    extern s32 g_musicState;
    extern void (*SongUpdate)(u32 ticks);
    extern u32 g_trkOn;
    extern u32 g_trkOff;
    IT_VOICE *voice;
    IT_VOICE *next;
    u32 i;
    IT_TRACK *trk;
    s32 old;

    if (g_pHdr == NULL)
    {
        return -1;
    }
    if (state == 0 || g_pHdr == NULL)
    {
        return g_musicState;
    }
    if (state != 1)
    {
        SongUpdate = NULL;
    }
    if (state == 2)
    {
        for (i = 0; i < 0x20; i++)
        {
            for (voice = GETP(g_trk[i].head); voice != NULL; voice = GETP(voice->next))
            {
                SetVoiceState_(voice, 2);
            }
        }
    }
    if (state == 3 || state == 4)
    {
        g_song[0] = 0;
        g_song[1] = 0;
        for (i = 0; i < 0x20; i++)
        {
            voice = GETP(g_trk[i].head);
            SETP(g_trk[i].head, NULL);
            SETP(g_trk[i].active, NULL);
            while (voice != NULL)
            {
                next = GETP(voice->next);
                SetVoiceState_(voice, 3);
                voice = next;
            }
        }
    }
    if (state == 1 || state == 4)
    {
        if (g_musicState == 3 || state == 4 || g_musicState == 0)
        {
            g_song[8] = g_pHdr[0x32];
            g_song[9] = g_pHdr[0x33];
            g_song[0xA] = 0x10000;
            g_song[2] = 0;
            g_song[4] = 1;
            *((u8 *)g_song + 0x14) = 0;
            *((u8 *)g_song + 0x15) = 0xFF;
            g_song[6] = g_pHdr[0x30];
            g_song[0] = ((u32)g_song[0xA] << 0xF) / (0x1C9C380U / ((u32)g_song[9] * 0x18));
            for (trk = g_trk, i = 0; i < 0x20; trk++, i++)
            {
                memset(trk, 0, sizeof(IT_TRACK));
                trk->index = i;
                SETP(trk->ins, g_pIns[0]);
                SETP(trk->smp, g_pSmp[0]);
                trk->note = 0x3C;
                trk->chVol = g_pHdr[0x80 + i];
                trk->vol = 0x8000;
                trk->panSep = -0x8000;
                trk->priority = 0x10;
                if ((s8)g_pHdr[0x40 + i] >= 0)
                {
                    trk->state = 1;
                    if ((s8)g_pHdr[0x40 + i] == 100)
                    {
                        trk->panSep = 0x7FFF;
                    }
                    else
                    {
                        trk->pan = ((s8)g_pHdr[0x40 + i] << 10) - 0x8000;
                        trk->pan -= trk->pan >> 15;
                    }
                }
            }
            g_trkOn = 0;
            g_trkOff = 0;
        }
        if (g_musicState == 2)
        {
            for (i = 0; i < 0x20; i++)
            {
                for (voice = GETP(g_trk[i].head); voice != NULL; voice = GETP(voice->next))
                {
                    SetVoiceState_(voice, 1);
                }
            }
        }
        state = 1;
        SongUpdate = ProcessIT;
    }
    old = g_musicState;
    g_musicState = state;
    return old;
}



void SetITVol(void)
{
    u16 GetITGVol_(s32 *song, IT_TRACK *trk);
    IT_TRACK *trk;
    IT_VOICE *voice;
    s32 i;
    s32 gvol;

    for (trk = g_trk, i = 0x20; i != 0; trk++, i--)
    {
        voice = GETP(trk->head);
        gvol = GetITGVol_(g_song, trk);
        trk->gvol = gvol;
        for (; voice != NULL; voice = GETP(voice->next))
        {
            SetVoiceVol_(voice, gvol);
        }
    }
}




void SetITPitch(s32 pitch)
{
    extern IT_VOICE g_voice[0x20];
    void SetVoiceRate_(IT_VOICE *voice);
    IT_VOICE *voice;
    s32 i;

    g_song[7] = pitch;
    for (voice = g_voice, i = 0x20; i != 0; voice++, i--)
    {
        if (voice->pad_8_3B[0x0D] == 0)
        {
            continue;
        }
        if (voice->byte0 != 0)
        {
            continue;
        }
        *(s32 *)((u8 *)voice + 0x34) = pitch;
        SetVoiceRate_(voice);
    }
}


void SetITTempo(u32 tempo)
{
    if (tempo > 0x1FFFF)
    {
        tempo = 0x1FFFF;
    }
    g_song[0xA] = tempo;
    g_song[0] = ((u32)g_song[0xA] << 0xF) / (0x1C9C380U / ((u32)g_song[9] * 0x18));
}




void SetITOrder(u8 order, s8 deferred)
{
    extern void *g_pHdr;

    if (g_pHdr == NULL)
    {
        return;
    }
    if (order < *(u16 *)((u8 *)g_pHdr + 0x20))
    {
        *((u8 *)g_song + 0x15) = order;
        if (deferred == 0)
        {
            g_song[4] = 1;
        }
    }
}


void SetITTrackPriority(s32 track, s32 priority)
{
    void SetVoicePriority_(void *voice);
    void *voice;
    void *next;

    priority *= 2;
    g_trk[track].priority = priority;
    voice = GETP(g_trk[track].active);
    if (voice != NULL)
    {
        *(u8 *)((u8 *)voice + 0x14) = priority;
        SetVoicePriority_(voice);
        voice = SLOT_LOAD(void *, ((u8 *)voice + 0x08));
    }
    else
    {
        voice = GETP(g_trk[track].head);
    }
    priority--;
    while (voice != NULL)
    {
        next = SLOT_LOAD(void *, ((u8 *)voice + 0x04));
        *(u8 *)((u8 *)voice + 0x14) = priority;
        SetVoicePriority_(voice);
        voice = next;
    }
}



void SetITTrackOn(u32 mask, s8 deferred)
{
    extern u32 g_trkOn;
    IT_TRACK *trk;

    if (deferred)
    {
        g_trkOn |= mask;
        return;
    }
    for (trk = g_trk; mask != 0; trk++)
    {
        trk->state |= mask & 1;
        mask >>= 1;
    }
}



void SetITTrackOff(u32 mask, s8 deferred)
{
    extern u32 g_trkOff;
    IT_TRACK *trk;
    IT_VOICE *voice;
    IT_VOICE *next;

    if (deferred)
    {
        g_trkOff |= mask;
        return;
    }
    for (trk = g_trk; mask != 0; trk++)
    {
        if (mask & 1)
        {
            trk->state = 0;
            voice = GETP(trk->head);
            SETP(trk->head, NULL);
            SETP(trk->active, NULL);
            while (voice)
            {
                next = GETP(voice->next);
                SetVoiceState_(voice, 3);
                voice = next;
            }
        }
        mask >>= 1;
    }
}



void SetITTrackVol(u32 track, s32 vol)
{
    u16 GetITGVol_(s32 *song, IT_TRACK *trk);
    IT_VOICE *voice;
    s32 gvol;

    voice = GETP(g_trk[track].head);
    g_trk[track].vol = vol;
    gvol = GetITGVol_(g_song, &g_trk[track]);
    g_trk[track].gvol = gvol;
    for (; voice != NULL; voice = GETP(voice->next))
    {
        SetVoiceVol_(voice, gvol);
    }
}



void ProcessIT(u32 ticks)
{
    extern u8 *g_pOrder;
    extern u32 g_trkOff;
    extern u32 g_trkOn;
    extern u8 *g_pHdr;
    extern const s32 g_amigaNote[120];
    s8 SetVoiceRate_(IT_VOICE *voice);
    void SetVoiceOffset_(IT_VOICE *voice, u32 offset);
    s16 GetITGVol_(s32 *song, IT_TRACK *trk);
    static u8 *pPtrn;
    s32 i;
    IT_TRACK *trk;
    s32 pos;
    s32 c;
    u8 mask;

    g_song[1] += (u32)(g_song[0] * ticks) >> 16;
    if ((u32)g_song[1] >> 16)
    {
        g_song[1] &= 0xFFFF;
        if ((u32)++g_song[2] < (u32)g_song[8])
        {
            for (i = 0; i < 0x20; i++)
            {
                if (GETP(g_trk[i].active) == NULL)
                {
                    continue;
                }
                trk = &g_trk[i];
                if (trk->flags & 0x40)
                {
                    trk->period -= trk->slideSpeed;
                    if (trk->flags & 0x80)
                    {
                        if (trk->period < trk->targetPeriod)
                        {
                            trk->period = trk->targetPeriod;
                            trk->flags &= ~0xC0;
                        }
                    }
                    else if (trk->period < 1)
                    {
                        StopActive(trk);
                        continue;
                    }
                    *(u32 *)((u8 *)GETP(trk->active) + 0x30) = (u32)(*(s32 *)(GETP(trk->smp) + 0x3C) * 0x6B0) / trk->period;
                    if (!SetVoiceRate_(GETP(trk->active)))
                    {
                        SETP(trk->active, NULL);
                    }
                }
                else if (trk->flags & 0x20)
                {
                    trk->period += trk->slideSpeed;
                    if ((trk->flags & 0x80) && trk->period > trk->targetPeriod)
                    {
                        trk->period = trk->targetPeriod;
                        trk->flags &= ~0xA0;
                    }
                    *(u32 *)((u8 *)GETP(trk->active) + 0x30) = (u32)(*(s32 *)(GETP(trk->smp) + 0x3C) * 0x6B0) / trk->period;
                    if (!SetVoiceRate_(GETP(trk->active)))
                    {
                        SETP(trk->active, NULL);
                    }
                }
            }
        }
        else
        {
            for (trk = g_trk, i = 0; i < 0x20; i++, trk++)
            {
                if (GETP(trk->active) != NULL && (trk->flags & 0x10))
                {
                    trk->curVol = trk->volBase + trk->volSlide;
                    if (trk->curVol < 0)
                    {
                        trk->curVol = 0;
                    }
                    else if (trk->curVol > 0x7FFF)
                    {
                        trk->curVol = 0x7FFF;
                    }
                    *(s32 *)((u8 *)GETP(trk->active) + 0x38) = trk->curVol;
                }
                trk->prevFlags = trk->flags;
                trk->flags = 0;
            }
            g_song[2] = 0;
            if (--g_song[4] == 0)
            {
                if ((s8)*((u8 *)g_song + 0x15) != -1)
                {
                    *((u8 *)g_song + 0x14) = *((u8 *)g_song + 0x15);
                    *((u8 *)g_song + 0x15) = 0xFF;
                }
                do
                {
                    c = g_pOrder[(*((u8 *)g_song + 0x14))++];
                    if (c == 0xFF)
                    {
                        *((u8 *)g_song + 0x14) = 0;
                    }
                } while (c > 199);
                pPtrn = g_pPtrn[c];
                g_song[4] = *(u16 *)(pPtrn + 2);
                pPtrn += 8;
                g_trkOff &= ~g_trkOn;
                if (g_trkOff != 0)
                {
                    SetITTrackOff(g_trkOff, 0);
                }
                if (g_trkOn != 0)
                {
                    SetITTrackOn(g_trkOn, 0);
                }
                g_trkOff = 0;
                g_trkOn = 0;
            }
            while ((c = *pPtrn++) != 0)
            {
                i = (c - 1) & 0x3F;
                trk = &g_trk[i];
                if (c & 0x80)
                {
                    trk->mask = *pPtrn++;
                }
                mask = trk->mask;
                if (mask & 1)
                {
                    trk->note = *pPtrn++;
                }
                if (mask & 2)
                {
                    trk->insNum = *pPtrn++ - 1;
                    if (*(u16 *)(g_pHdr + 0x2C) & 4)
                    {
                        SETP(trk->ins, g_pIns[trk->insNum]);
                        SETP(trk->smp, g_pSmp[GETP(trk->ins)[trk->note * 2 + 0x41]]);
                    }
                    else
                    {
                        SETP(trk->smp, g_pSmp[trk->insNum]);
                    }
                }
                if (mask & 4)
                {
                    trk->volCol = *pPtrn++;
                }
                if (mask & 8)
                {
                    trk->cmd = *pPtrn++;
                    trk->param = *pPtrn++;
                }
                if (trk->state)
                {
                    if (mask & 0x13)
                    {
                        if (trk->note < 0x78)
                        {
                            if (*(u16 *)(g_pHdr + 0x2C) & 4)
                            {
                                trk->realNote = GETP(trk->ins)[trk->note * 2 + 0x40];
                            }
                            else
                            {
                                trk->realNote = trk->note;
                            }
                            if (!(mask & 0x88) || trk->cmd != 7)
                            {
                                trk->curVol = ((mask & 0x44) ? trk->volCol : GETP(trk->smp)[0x13]) << 9;
                                if (trk->curVol >= 0x8000)
                                {
                                    trk->curVol = 0x7FFF;
                                }
                                trk->period = g_amigaNote[trk->realNote];
                                PlayNote(trk);
                                goto effects;
                            }
                        }
                        else
                        {
                            if (GETP(trk->active) != NULL)
                            {
                                StopActive(trk);
                            }
                            goto effects;
                        }
                    }
                    if (mask & 0x44)
                    {
                        trk->curVol = trk->volCol << 9;
                        if (trk->curVol > 0x8000)
                        {
                            trk->curVol = 0x7FFF;
                        }
                        if (GETP(trk->active) != NULL)
                        {
                            *(s32 *)((u8 *)GETP(trk->active) + 0x38) = trk->curVol;
                            SetVoiceVol_(GETP(trk->active), trk->gvol);
                        }
                    }
                    else if (mask & 0x20)
                    {
                        trk->curVol = GETP(trk->smp)[0x13] << 9;
                        if (GETP(trk->active) != NULL)
                        {
                            *(s32 *)((u8 *)GETP(trk->active) + 0x38) = trk->curVol;
                            SetVoiceVol_(GETP(trk->active), trk->gvol);
                        }
                    }
                }
            effects:
                if (!(mask & 0x88))
                {
                    continue;
                }
                switch (trk->cmd)
                {
                case 11:
                case 12:
                    if (trk->cmd == 12)
                    {
                        trk->flags |= ((trk->targetPeriod > trk->period) ? 0x20 : 0x40) | 0x80;
                    }
                    else
                    {
                        trk->flags |= 0x100;
                    }
                case 4:
                    if (trk->param)
                    {
                        trk->memVolSlide = trk->param;
                    }
                    trk->flags |= 0x10;
                    trk->volBase = trk->curVol;
                    if ((trk->memVolSlide & 0xF) == 0)
                    {
                        trk->volSlide = (trk->memVolSlide << 5) * (g_song[8] - 1);
                        if (trk->memVolSlide == 0xF0)
                        {
                            trk->volSlide += trk->memVolSlide << 5;
                        }
                    }
                    else if ((trk->memVolSlide & 0xF0) == 0)
                    {
                        trk->volSlide = -((trk->memVolSlide << 9) * (g_song[8] - 1));
                        if (trk->memVolSlide == 0xF)
                        {
                            trk->volSlide -= trk->memVolSlide << 9;
                        }
                    }
                    else if ((trk->memVolSlide & 0xF) == 0xF)
                    {
                        trk->volSlide = (trk->memVolSlide & 0xF0) << 5;
                    }
                    else if ((trk->memVolSlide & 0xF0) == 0xF0)
                    {
                        trk->volSlide = -(trk->memVolSlide & 0xF) << 9;
                    }
                    else
                    {
                        trk->flags &= ~0x10;
                    }
                    break;
                case 5:
                    if (trk->param)
                    {
                        trk->memPitchSlide = trk->param;
                    }
                    if (trk->memPitchSlide < 0xE0)
                    {
                        trk->flags |= 0x40;
                        trk->slideSpeed = trk->memPitchSlide << 2;
                    }
                    else
                    {
                        if (trk->memPitchSlide > 0xF0)
                        {
                            trk->period += (trk->memPitchSlide & 0xF) * 4;
                        }
                        else
                        {
                            trk->period += trk->memPitchSlide & 0xF;
                        }
                        *(u32 *)((u8 *)GETP(trk->active) + 0x30) = (u32)(*(s32 *)(GETP(trk->smp) + 0x3C) * 0x6B0) / trk->period;
                        if (GETP(trk->active) != NULL && !SetVoiceRate_(GETP(trk->active)))
                        {
                            SETP(trk->active, NULL);
                        }
                    }
                    break;
                case 6:
                    if (trk->param)
                    {
                        trk->memPitchSlide = trk->param;
                    }
                    if (trk->memPitchSlide < 0xE0)
                    {
                        trk->flags |= 0x20;
                        trk->slideSpeed = trk->memPitchSlide << 2;
                    }
                    else if (trk->memPitchSlide & 0xF)
                    {
                        if (trk->memPitchSlide > 0xF0)
                        {
                            trk->period += (trk->memPitchSlide & 0xF) * 4;
                        }
                        else
                        {
                            trk->period += trk->memPitchSlide & 0xF;
                        }
                        *(u32 *)((u8 *)GETP(trk->active) + 0x30) = (u32)(*(s32 *)(GETP(trk->smp) + 0x3C) * 0x6B0) / trk->period;
                        if (GETP(trk->active) != NULL && !SetVoiceRate_(GETP(trk->active)))
                        {
                            SETP(trk->active, NULL);
                        }
                    }
                    break;
                case 7:
                    if (trk->param)
                    {
                        trk->memPitchSlide = trk->param;
                    }
                    trk->slideSpeed = trk->memPitchSlide << 2;
                    if (mask & 0x11)
                    {
                        trk->targetPeriod = g_amigaNote[trk->realNote];
                    }
                    trk->flags |= ((trk->targetPeriod > trk->period) ? 0x20 : 0x40) | 0x80;
                    break;
                case 10:
                    if (trk->param)
                    {
                        trk->memArp = trk->param;
                    }
                    break;
                case 8:
                    if (trk->param)
                    {
                        trk->memVibrato = trk->param;
                    }
                    break;
                case 21:
                    if (trk->param)
                    {
                        trk->memVibrato = trk->param;
                    }
                    break;
                case 15:
                    if (trk->param)
                    {
                        trk->memOffset = trk->param;
                    }
                    if (GETP(trk->active) != NULL)
                    {
                        SetVoiceOffset_(GETP(trk->active), trk->memOffset << 8);
                    }
                    break;
                case 9:
                    if (trk->param)
                    {
                        trk->memTremor = trk->param;
                    }
                    break;
                case 13:
                    if (trk->param)
                    {
                        trk->memChVolSlide = trk->param;
                    }
                    if (trk->param <= 0x40)
                    {
                        trk->chVol = trk->param;
                    }
                    trk->gvol = GetITGVol_(g_song, trk);
                    SetTrackVol(trk);
                    break;
                case 24:
                    trk->pan = trk->param * 0x100 - 0x8000;
                    trk->pan -= trk->pan >> 15;
                    SetTrackPan(trk);
                    break;
                case 19:
                    if (trk->param)
                    {
                        trk->memSpecial = trk->param;
                    }
                    switch (trk->memSpecial & 0xF0)
                    {
                    case 0x10:
                    case 0x30:
                    case 0x40:
                    case 0x50:
                    case 0x60:
                    case 0x70:
                    case 0x80:
                        break;
                    case 0x90:
                        switch (trk->memSpecial)
                        {
                        case 0x90:
                            trk->panSep = -0x8000;
                            break;
                        case 0x91:
                            trk->panSep = 0x8000;
                            break;
                        case 0x92:
                        case 0x93:
                        case 0x94:
                        case 0x95:
                        case 0x96:
                        case 0x97:
                        case 0x98:
                        case 0x99:
                            break;
                        }
                        break;
                    case 0xA0:
                    case 0xB0:
                    case 0xC0:
                    case 0xD0:
                    case 0xE0:
                    case 0xF0:
                        break;
                    }
                    break;
                case 1:
                    g_song[8] = trk->param;
                    break;
                case 20:
                    if (trk->param)
                    {
                        trk->memTempo = trk->param;
                    }
                    if (trk->memTempo > 0x1F)
                    {
                        g_song[9] = trk->memTempo;
                        g_song[0] = ((u32)g_song[0xA] << 0xF) / (0x1C9C380U / ((u32)g_song[9] * 0x18));
                    }
                    else if (trk->memTempo & 0xF0)
                    {
                        g_song[0xB] = trk->memTempo & 0xF;
                    }
                    else
                    {
                        g_song[0xB] = -(s8)trk->memTempo;
                    }
                    break;
                case 2:
                    if (trk->param < *(u16 *)(g_pHdr + 0x20))
                    {
                        *((u8 *)g_song + 0x14) = trk->param;
                        g_song[4] = 1;
                    }
                    break;
                case 3:
                    g_song[4] = 1;
                    *((u8 *)g_song + 0xD) = trk->param;
                    *((u8 *)g_song + 0xF) |= 8;
                    break;
                case 22:
                    if (trk->param <= 0x80)
                    {
                        g_song[6] = trk->param;
                    }
                    break;
                case 23:
                    if (trk->param)
                    {
                        trk->memPanSlide = trk->param;
                    }
                    *((u8 *)g_song + 0xE) = trk->memPanSlide;
                    break;
                case 26:
                    break;
                }
            }
            return;
        }
    }
    pos = (s32)((((u32)g_song[2] << 16) + (u32)g_song[1]) / (u32)g_song[8]) >> 1;
    for (trk = g_trk, i = 0; i < 0x20; trk++, i++)
    {
        if (GETP(trk->active) != NULL && (trk->flags & 0x10))
        {
            trk->curVol = trk->volBase + ((trk->volSlide * pos) >> 15);
            if (trk->curVol < 0)
            {
                trk->curVol = 0;
                trk->flags &= ~0x10;
            }
            else if (trk->curVol > 0x7FFF)
            {
                trk->curVol = 0x7FFF;
                trk->flags &= ~0x10;
            }
            *(s32 *)((u8 *)GETP(trk->active) + 0x38) = trk->curVol;
            SetVoiceVol_(GETP(trk->active), trk->gvol);
        }
    }
}


s32 PlayNote(IT_TRACK *trk)
{
    u32 rate;
    u8 stolen;
    u8 *voice;
    u8 *song;

    stolen = 0;
    rate = GetNoteRate_(*(u32 *)(GETP(trk->smp) + 0x3C), trk->realNote);
    if (rate > 0x2EE00 || rate < 0x60)
    {
        if (!stolen && GETP(trk->active) != NULL)
        {
            StopActive(trk);
        }
        return 0;
    }
    if (GETP(trk->active) == NULL)
    {
        SETP(trk->active, (IT_VOICE *)GetVoice_(0, trk->priority));
        if (GETP(trk->active) == NULL)
        {
            return 0;
        }
        GETP(trk->active)->track = trk->index;
        GETP(trk->active)->pad_2_3[0] = trk->note;
        SETP(GETP(trk->active)->next, GETP(trk->head));
        SETP(trk->head, GETP(trk->active));
    }
    voice = (u8 *)GETP(trk->active);
    *(u32 *)(voice + 0x10) = (u32)HND(GETP(trk->smp));
    song = GETP(trk->smp);
    *(u32 *)(voice + 0x20) = *(u32 *)(song + 0x48);
    *(s32 *)(voice + 0x24) = (song[0x12] & 0x10) ? *(s32 *)(song + 0x34) : -1;
    *(s32 *)(voice + 0x28) = (song[0x12] & 0x10) ? *(s32 *)(song + 0x38) : *(s32 *)(song + 0x30);
    voice[0x2C] = (song[0x12] & 2) ? 0x10 : 8;
    *(u32 *)(voice + 0x30) = rate;
    *(s32 *)(voice + 0x34) = g_song[7];
    *(u32 *)(voice + 0x38) = trk->curVol;
    *(s32 *)(voice + 0x3C) = song[0x11] << 9;
    *(s32 *)(voice + 0x40) = trk->pan;
    *(s32 *)(voice + 0x44) = -0x7FFF;
    StartVoice_((void *)voice);
    SetVoiceVol_((IT_VOICE *)voice, trk->gvol);
    return 1;
}

void SetTrackVol(IT_TRACK *trk)
{
    IT_VOICE *voice;

    for (voice = GETP(trk->head); voice != NULL; voice = GETP(voice->next))
    {
        SetVoiceVol_(voice, trk->gvol);
    }
}

void SetTrackPan(IT_TRACK *trk)
{
    IT_VOICE *voice;

    for (voice = GETP(trk->head); voice != NULL; voice = GETP(voice->next))
    {
        voice->pan = trk->pan;
        SetVoiceVol_(voice, trk->gvol);
    }
}

void StopActive(IT_TRACK *trk)
{
    SETP(trk->head, GETP(GETP(trk->head)->next));
    SetVoiceState_(GETP(trk->active), 3);
    SETP(trk->active, NULL);
}



void DestroyITVoice(IT_VOICE *voice)
{
    IT_TRACK *trk;
    IT_VOICE *v;

    if (voice->track > 0x20)
    {
        return;
    }
    trk = &g_trk[voice->track];
    if (GETP(trk->active) == voice)
    {
        SETP(trk->active, NULL);
        SETP(trk->head, GETP(voice->next));
        return;
    }
    v = GETP(trk->head);
    while (GETP(v->next) != voice)
    {
        v = GETP(v->next);
    }
    SETP(v->next, GETP(voice->next));
}

