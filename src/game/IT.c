

#include "common.h"
#include "Voice.h"
#include "IT.h"

static ITSong g_song;
static ITTrack g_trk[0x20];
static ITInstrument *g_pIns[100];
static ITSample *g_pSmp[100];
static u8 *g_pPtrn[200];

void StopActive(ITTrack *trk);
void SetTrackPan(ITTrack *trk);
void SetTrackVol(ITTrack *trk);
s32 PlayNote(ITTrack *trk);



extern ITHeader *g_pHdr;  /* the playing module, NULL: none */

s32 AudioIT_Create(void)
{
    u8 i;

    memset(&g_song, 0, sizeof(g_song));
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
    u32 i;
    ITTrack *trk;

    if (g_pHdr != NULL)
    {
        DestroyIT();
    }
    g_song.pitch = 0x10000;
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


void CreateIT(u8 *data, u8 *arg1)
{
    extern u8 *g_pOrder;
    extern u8 g_musicTitle[0x20];
    extern void (*DestroySongVoice)(Voice *voice);
    extern s32 __OSCurrHeap;
    void DestroyITVoice(Voice *voice);
    s32 i;
    u32 *insOff;
    u32 *smpOff;
    u32 *patOff;

    if (data == NULL || arg1 == 0)
    {
        return;
    }
    i = ((ITHeader *)data)->size;
    g_pHdr = OSAllocFromHeap(__OSCurrHeap, i);
    memcpy(g_pHdr, data, i);
    data = (u8 *)g_pHdr;
    g_pHdr->loaded = 1;
    g_pOrder = g_pHdr->orders;
    /* the order list is followed by the instrument, sample and pattern offsets */
    insOff = (u32 *)(g_pHdr->orders + g_pHdr->numOrders);
    smpOff = insOff + g_pHdr->numInstruments;
    patOff = smpOff + g_pHdr->numSamples;
    for (i = 0; i < g_pHdr->numInstruments; i++)
    {
        g_pIns[i] = (ITInstrument *)(data + insOff[i]);
    }
    for (i = 0; i < g_pHdr->numSamples; i++)
    {
        g_pSmp[i] = (ITSample *)(data + smpOff[i]);
    }
    for (i = 0; i < g_pHdr->numPatterns; i++)
    {
        g_pPtrn[i] = data + patOff[i];
    }
    SLOT_STORE(&g_pSmp[0]->data, arg1);  /* the sample data, kept apart from the module */
    CreateIT_(g_pHdr, g_pSmp);
    memcpy(g_musicTitle, g_pHdr->title, sizeof(g_pHdr->title));
    g_musicTitle[0x1A] = 0;
    DestroySongVoice = DestroyITVoice;
}



void DestroyIT(void)
{
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
    extern s32 g_musicState;
    extern void (*SongUpdate)(u32 ticks);
    extern u32 g_trkOn;
    extern u32 g_trkOff;
    Voice *voice;
    Voice *next;
    u32 i;
    ITTrack *trk;
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
        g_song.tickStep = 0;
        g_song.tickFrac = 0;
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
            g_song.speed = g_pHdr->speed;
            g_song.tempo = g_pHdr->tempo;
            g_song.tempoScale = 0x10000;
            g_song.tick = 0;
            g_song.rowsLeft = 1;
            g_song.order = 0;
            g_song.nextOrder = -1;
            g_song.globalVol = g_pHdr->globalVol;
            g_song.tickStep = (g_song.tempoScale << 0xF) / (0x1C9C380U / (g_song.tempo * 0x18));
            for (trk = g_trk, i = 0; i < 0x20; trk++, i++)
            {
                memset(trk, 0, sizeof(ITTrack));
                trk->index = i;
                SETP(trk->ins, g_pIns[0]);
                SETP(trk->smp, g_pSmp[0]);
                trk->note = 0x3C;
                trk->chVol = g_pHdr->chnVol[i];
                trk->vol = 0x8000;
                trk->panSep = -0x8000;
                trk->priority = 0x10;
                if (g_pHdr->chnPan[i] >= 0)
                {
                    trk->state = 1;
                    if (g_pHdr->chnPan[i] == 100)
                    {
                        trk->panSep = 0x7FFF;
                    }
                    else
                    {
                        trk->pan = (g_pHdr->chnPan[i] << 10) - 0x8000;
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
    ITTrack *trk;
    Voice *voice;
    s32 i;
    s32 gvol;

    for (trk = g_trk, i = 0x20; i != 0; trk++, i--)
    {
        voice = GETP(trk->head);
        gvol = GetITGVol_(&g_song, trk);
        trk->gvol = gvol;
        for (; voice != NULL; voice = GETP(voice->next))
        {
            SetVoiceVol_(voice, gvol);
        }
    }
}




void SetITPitch(s32 pitch)
{
    extern Voice g_voice[0x20];
    Voice *voice;
    s32 i;

    g_song.pitch = pitch;
    for (voice = g_voice, i = 0x20; i != 0; voice++, i--)
    {
        if (voice->state == VOICE_STATE_FREE)
        {
            continue;
        }
        if (voice->kind != VOICE_KIND_SONG)
        {
            continue;
        }
        voice->pitch = pitch;
        SetVoiceRate_(voice);
    }
}


void SetITTempo(u32 tempo)
{
    if (tempo > 0x1FFFF)
    {
        tempo = 0x1FFFF;
    }
    g_song.tempoScale = tempo;
    g_song.tickStep = (g_song.tempoScale << 0xF) / (0x1C9C380U / (g_song.tempo * 0x18));
}




void SetITOrder(u8 order, s8 deferred)
{

    if (g_pHdr == NULL)
    {
        return;
    }
    if (order < g_pHdr->numOrders)
    {
        g_song.nextOrder = order;
        if (deferred == 0)
        {
            g_song.rowsLeft = 1;
        }
    }
}


void SetITTrackPriority(s32 track, s32 priority)
{
    Voice *voice;
    Voice *next;

    priority *= 2;
    g_trk[track].priority = priority;
    voice = GETP(g_trk[track].active);
    if (voice != NULL)
    {
        voice->priority = priority;
        SetVoicePriority_(voice);
        /* sic: 0x08 is never set, so only the active voice gets the new
         * priority; it heads the list, and `next` (0x04) looks meant */
        voice = GETP(voice->unk08);
    }
    else
    {
        voice = GETP(g_trk[track].head);
    }
    priority--;
    while (voice != NULL)
    {
        next = GETP(voice->next);
        voice->priority = priority;
        SetVoicePriority_(voice);
        voice = next;
    }
}



void SetITTrackOn(u32 mask, s8 deferred)
{
    extern u32 g_trkOn;
    ITTrack *trk;

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
    ITTrack *trk;
    Voice *voice;
    Voice *next;

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
    Voice *voice;
    s32 gvol;

    voice = GETP(g_trk[track].head);
    g_trk[track].vol = vol;
    gvol = GetITGVol_(&g_song, &g_trk[track]);
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
    extern const s32 g_amigaNote[120];
    static u8 *pPtrn;
    s32 i;
    ITTrack *trk;
    s32 pos;
    s32 c;
    u8 mask;

    g_song.tickFrac += (u32)(g_song.tickStep * ticks) >> 16;
    if (g_song.tickFrac >> 16)
    {
        g_song.tickFrac &= 0xFFFF;
        if (++g_song.tick < g_song.speed)
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
                    GETP(trk->active)->rate = (u32)((s32)GETP(trk->smp)->c5Speed * 0x6B0) / trk->period;
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
                    GETP(trk->active)->rate = (u32)((s32)GETP(trk->smp)->c5Speed * 0x6B0) / trk->period;
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
                    GETP(trk->active)->vol = trk->curVol;
                }
                trk->prevFlags = trk->flags;
                trk->flags = 0;
            }
            g_song.tick = 0;
            if (--g_song.rowsLeft == 0)
            {
                if (g_song.nextOrder != -1)
                {
                    g_song.order = g_song.nextOrder;
                    g_song.nextOrder = -1;
                }
                do
                {
                    c = g_pOrder[g_song.order++];
                    if (c == 0xFF)
                    {
                        g_song.order = 0;
                    }
                } while (c > 199);
                pPtrn = g_pPtrn[c];
                g_song.rowsLeft = *(u16 *)(pPtrn + 2);
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
                    if (g_pHdr->flags & IT_FLAG_INSTRUMENTS)
                    {
                        SETP(trk->ins, g_pIns[trk->insNum]);
                        SETP(trk->smp, g_pSmp[GETP(trk->ins)->keyboard[trk->note][1]]);
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
                            if (g_pHdr->flags & IT_FLAG_INSTRUMENTS)
                            {
                                trk->realNote = GETP(trk->ins)->keyboard[trk->note][0];
                            }
                            else
                            {
                                trk->realNote = trk->note;
                            }
                            if (!(mask & 0x88) || trk->cmd != 7)
                            {
                                trk->curVol = ((mask & 0x44) ? trk->volCol : GETP(trk->smp)->vol) << 9;
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
                            GETP(trk->active)->vol = trk->curVol;
                            SetVoiceVol_(GETP(trk->active), trk->gvol);
                        }
                    }
                    else if (mask & 0x20)
                    {
                        trk->curVol = GETP(trk->smp)->vol << 9;
                        if (GETP(trk->active) != NULL)
                        {
                            GETP(trk->active)->vol = trk->curVol;
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
                        trk->volSlide = (trk->memVolSlide << 5) * (g_song.speed - 1);
                        if (trk->memVolSlide == 0xF0)
                        {
                            trk->volSlide += trk->memVolSlide << 5;
                        }
                    }
                    else if ((trk->memVolSlide & 0xF0) == 0)
                    {
                        trk->volSlide = -((trk->memVolSlide << 9) * (g_song.speed - 1));
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
                        GETP(trk->active)->rate = (u32)((s32)GETP(trk->smp)->c5Speed * 0x6B0) / trk->period;
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
                        GETP(trk->active)->rate = (u32)((s32)GETP(trk->smp)->c5Speed * 0x6B0) / trk->period;
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
                    trk->gvol = GetITGVol_(&g_song, trk);
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
                    g_song.speed = trk->param;
                    break;
                case 20:
                    if (trk->param)
                    {
                        trk->memTempo = trk->param;
                    }
                    if (trk->memTempo > 0x1F)
                    {
                        g_song.tempo = trk->memTempo;
                        g_song.tickStep = (g_song.tempoScale << 0xF) / (0x1C9C380U / (g_song.tempo * 0x18));
                    }
                    else if (trk->memTempo & 0xF0)
                    {
                        g_song.tempoSlide = trk->memTempo & 0xF;
                    }
                    else
                    {
                        g_song.tempoSlide = -(s8)trk->memTempo;
                    }
                    break;
                case 2:
                    if (trk->param < g_pHdr->numOrders)
                    {
                        g_song.order = trk->param;
                        g_song.rowsLeft = 1;
                    }
                    break;
                case 3:
                    g_song.rowsLeft = 1;
                    g_song.breakRow = trk->param;
                    g_song.flags |= 8;
                    break;
                case 22:
                    if (trk->param <= 0x80)
                    {
                        g_song.globalVol = trk->param;
                    }
                    break;
                case 23:
                    if (trk->param)
                    {
                        trk->memPanSlide = trk->param;
                    }
                    g_song.panSlide = trk->memPanSlide;
                    break;
                case 26:
                    break;
                }
            }
            return;
        }
    }
    pos = (s32)(((g_song.tick << 16) + g_song.tickFrac) / g_song.speed) >> 1;
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
            GETP(trk->active)->vol = trk->curVol;
            SetVoiceVol_(GETP(trk->active), trk->gvol);
        }
    }
}


s32 PlayNote(ITTrack *trk)
{
    u32 rate;
    u8 stolen;
    Voice *voice;
    ITSample *smp;

    stolen = 0;
    rate = GetNoteRate_(GETP(trk->smp)->c5Speed, trk->realNote);
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
        SETP(trk->active, (Voice *)GetVoice_(0, trk->priority));
        if (GETP(trk->active) == NULL)
        {
            return 0;
        }
        GETP(trk->active)->owner = trk->index;
        GETP(trk->active)->note = trk->note;
        SETP(GETP(trk->active)->next, GETP(trk->head));
        SETP(trk->head, GETP(trk->active));
    }
    voice = GETP(trk->active);
    SETP(voice->itSample, GETP(trk->smp));
    smp = GETP(trk->smp);
    voice->addr = smp->data;
    voice->loop = (smp->flags & IT_SAMPLE_LOOP) ? smp->loopBegin : -1;
    voice->length = (smp->flags & IT_SAMPLE_LOOP) ? smp->loopEnd : smp->length;
    voice->bits = (smp->flags & IT_SAMPLE_16BIT) ? 16 : 8;
    voice->rate = rate;
    voice->pitch = g_song.pitch;
    voice->vol = trk->curVol;
    voice->mixVol = smp->globalVol << 9;
    voice->pan = trk->pan;
    voice->span = -0x7FFF;
    StartVoice_(voice);
    SetVoiceVol_(voice, trk->gvol);
    return 1;
}

void SetTrackVol(ITTrack *trk)
{
    Voice *voice;

    for (voice = GETP(trk->head); voice != NULL; voice = GETP(voice->next))
    {
        SetVoiceVol_(voice, trk->gvol);
    }
}

void SetTrackPan(ITTrack *trk)
{
    Voice *voice;

    for (voice = GETP(trk->head); voice != NULL; voice = GETP(voice->next))
    {
        voice->pan = trk->pan;
        SetVoiceVol_(voice, trk->gvol);
    }
}

void StopActive(ITTrack *trk)
{
    SETP(trk->head, GETP(GETP(trk->head)->next));
    SetVoiceState_(GETP(trk->active), 3);
    SETP(trk->active, NULL);
}



void DestroyITVoice(Voice *voice)
{
    ITTrack *trk;
    Voice *v;

    if (voice->owner > 0x20)
    {
        return;
    }
    trk = &g_trk[voice->owner];
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

