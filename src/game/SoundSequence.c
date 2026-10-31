#include "common.h"
#include "ThingManager.h"
#include "SoundSequence.h"

/* THING_TYPE_SOUND_SEQ */
s32 SoundSeq_Init(SoundSequence *seq, SoundSeqParams *data) {
    extern Matrix4 g_identity;
    SoundSequence *list[0x80];
    SoundSeqInfo *info;
    s16 count;
    u32 n;

    Thing_Init(&seq->base, 0);
    *THING_MTX(&seq->base) = g_identity;
    seq->base.pos = data->pos;
    SETP(seq->info, GETP(data->seq));
    SETP(seq->queued, NULL);
    info = GETP(seq->info);
    if (info->maxInstances != 0) {
        count = 0;
        n = ThingManager_GetActiveSoundSequences((u8 **)list, 0x80);
        while (n != 0) {
            n--;
            if (list[n] != seq && GETP(list[n]->info) == GETP(seq->info)) {
                count++;
            }
        }
        info = GETP(seq->info);
        if (count >= info->maxInstances) {
            seq->timer = -1;
        } else {
            seq->timer = RandomAdd_s32(info->duration, info->durationRange);
        }
    } else {
        seq->timer = RandomAdd_s32(info->duration, info->durationRange);
    }
    if (GETP(seq->info)->duration == 0) {
        seq->base.flags |= SOUNDSEQ_FLAG_ENDLESS;
    }
    /* a randomised duration stretches the track to fit */
    seq->rate = 1.0f;
    info = GETP(seq->info);
    if (info->duration != 0 && info->durationRange != 0) {
        seq->rate = 1.0f / ((f32)seq->timer / (f32)info->duration);
    }
    seq->state.reset = 1;
    seq->base.flags |= 0xE0202;
    if (data->flag != 0) {
        seq->base.flags |= SOUNDSEQ_FLAG_FOLLOW;
    }
    seq->sources[0] = -1;
    seq->sources[1] = -1;
    seq->sources[2] = -1;
    seq->sources[3] = -1;
    return 1;
}

static void SoundSeq_RemoveFromQueue(SoundSequence *seq) {
    SoundSequence *next;

    next = GETP(seq->queued);
    if (next != NULL) {
        next->base.flags &= ~SOUNDSEQ_FLAG_WAITING;
        SETP(seq->queued, NULL);
    } else {
        Trigger_SoundSequenceQueueEmptied();
    }
}

void SoundSeq_Release(void *thing) {
    SoundSequence *seq = thing;
    u32 i;

    if (((SoundSequence *)thing)->base.flags & SOUNDSEQ_FLAG_QUEUED) {
        SoundSeq_RemoveFromQueue(seq);
    }
    for (i = 0; i < SOUNDSEQ_MAX_SOURCES; i++) {
        if (seq->sources[i] >= 0 && SoundSource_IsLooping(seq->sources[i])) {
            SoundSource_Stop(seq->sources[i]);
        }
        seq->sources[i] = -1;
    }
    Thing_Release(thing);
}


s32 SoundSeq_Move(void *thing) {
    extern s32 g_iDeltaTimeMS;
    extern f32 g_fDeltaTimeMS;
    SoundSequence *seq = thing;
    s8 playing;
    s32 key;
    s8 found;
    u32 i;

    playing = 0;
    if (seq->base.flags & SOUNDSEQ_FLAG_WAITING) {
        return 1;
    }
    if (!(seq->base.flags & SOUNDSEQ_FLAG_ENDLESS)) {
        if (seq->timer < 0) {
            SoundSeq_Release(thing);
            return 0;
        }
        seq->timer -= g_iDeltaTimeMS;
    }
    key = ExtractSoundKey(&GETP(seq->info)->track, &seq->state, &seq->base.pos, g_fDeltaTimeMS * seq->rate);
    if (key >= 0) {
        found = 0;
        for (i = 0; i < SOUNDSEQ_MAX_SOURCES; i++) {
            if (seq->sources[i] == -1) {
                found = 1;
                seq->sources[i] = key;
                if (SoundSource_IsLooping(key)) {
                    seq->base.flags |= SOUNDSEQ_FLAG_LOOPING;
                }
                break;
            }
        }
        if (!found) {
            SoundSource_Stop(key);
        }
    } else if (key == -2) {
        if (!(seq->base.flags & SOUNDSEQ_FLAG_QUEUED)) {
            seq->base.flags |= SOUNDSEQ_FLAG_DONE;
        }
    }
    if (seq->base.flags & SOUNDSEQ_FLAG_FOLLOW) {
        for (i = 0; i < SOUNDSEQ_MAX_SOURCES; i++) {
            if (seq->sources[i] != -1) {
                if (SoundSource_IsLooping(seq->sources[i]) || SoundSource_IsActive(seq->sources[i])) {
                    SoundSource_Set(seq->sources[i], 5, &seq->base.pos);
                    playing = 1;
                } else {
                    SoundSource_Stop(seq->sources[i]);
                    seq->sources[i] = -1;
                }
            }
        }
    }
    if (!playing && (seq->base.flags & SOUNDSEQ_FLAG_DONE) && !(seq->base.flags & SOUNDSEQ_FLAG_QUEUED)) {
        Thing_Release(thing);
    }
    return 1;
}


s32 SoundSeq_Invalidate(SoundSequence *seq, u32 start, u32 end)
{
    if ((u32)HND(GETP(seq->info)) >= start && (u32)HND(GETP(seq->info)) < end) {
        return 1;
    }
    return 0;
}

s32 SoundSequence_Play(SoundSeqInfo *seq, Vector4 *pos) {
    extern Vector4 g_zeroVector;
    extern s8 g_bFreezeWorld;
    Vector4 p;
    SoundSeqParams params __attribute__((aligned(16)));
    SoundSequence *thing;

    if (!seq->allPlayers && PlayerManager_PlayerHumanCount() > 1) {
        return -1;
    }
    if (pos != NULL) {
        p = *pos;
    } else {
        p = g_zeroVector;
    }
    params.pos = p;
    SETP(params.seq, seq);
    params.flag = 0;
    thing = ThingManager_DARYL_CreateThing(THING_TYPE_SOUND_SEQ, &params);
    if (thing != NULL) {
        if (g_bFreezeWorld) {
            thing->base.flags |= SOUNDSEQ_FLAG_RUNS_FROZEN;
        }
        return (s32)HND(thing);
    }
    return -1;
}

/* Plays seq after the queued sequences: the queue is a chain through
 * SoundSequence.queued, each waiting until the one before it is released. */
s32 SoundSequence_PlayQueued(SoundSeqInfo *seq, Vector4 *pos) {
    extern Vector4 g_zeroVector;
    extern s8 g_bFreezeWorld;
    Vector4 p;
    SoundSequence *list[0x80];
    SoundSeqParams params __attribute__((aligned(16)));
    SoundSequence *created;
    SoundSequence *thing;
    s8 found;
    u32 n;

    if (!seq->allPlayers && PlayerManager_PlayerHumanCount() > 1) {
        thing = (SoundSequence *)-1;
    } else {
        if (pos != NULL) {
            p = *pos;
        } else {
            p = g_zeroVector;
        }
        params.pos = p;
        SETP(params.seq, seq);
        params.flag = 0;
        created = ThingManager_DARYL_CreateThing(THING_TYPE_SOUND_SEQ, &params);
        if (created != NULL) {
            if (g_bFreezeWorld) {
                created->base.flags |= SOUNDSEQ_FLAG_RUNS_FROZEN;
            }
            thing = created;
        } else {
            thing = (SoundSequence *)-1;
        }
    }
    if ((u32)HND(thing) != 0xFFFFFFFF) {
        found = 0;
        /* append to the queue's tail */
        n = ThingManager_GetActiveSoundSequences((u8 **)list, 0x80);
        while (n != 0) {
            n--;
            if ((list[n]->base.flags & SOUNDSEQ_FLAG_QUEUED) && GETP(list[n]->queued) == NULL) {
                found = 1;
                break;
            }
        }
        if (found) {
            SETP(list[n]->queued, thing);
            thing->base.flags |= SOUNDSEQ_FLAG_WAITING;
        }
        thing->base.flags |= SOUNDSEQ_FLAG_QUEUED;
        SETP(thing->queued, NULL);
    }
    return (s32)HND(thing);
}


void SoundSequence_ClearQueue(void) {
    SoundSequence *list[128];
    u32 count;

    count = ThingManager_GetActiveSoundSequences((u8 **)list, 128);
    while (count != 0) {
        count--;
        if (list[count]->base.flags & SOUNDSEQ_FLAG_QUEUED) {
            list[count]->base.flags &= ~SOUNDSEQ_FLAG_QUEUED;
            SETP(list[count]->queued, NULL);
            SoundSeq_Release(list[count]);
        }
    }
}

s32 SoundSequence_GetTotalElementsInQueue(void)
{
    u8 *list[128];
    u32 count;
    s32 total;

    total = 0;
    count = ThingManager_GetActiveSoundSequences(list, 128);
    while (count != 0) {
        count--;
        if (((SoundSequence *)list[count])->base.flags & SOUNDSEQ_FLAG_QUEUED) {
            total++;
        }
    }
    return total;
}


void SoundSequence_Stop(u32 handle) {
    void *thing;

    if (handle != 0xFFFFFFFF) {
        thing = (void *)PTR(handle);
        if (thing != NULL) {
            SoundSeq_Release(thing);
        }
    }
}

