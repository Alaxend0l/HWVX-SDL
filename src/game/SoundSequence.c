#include "common.h"

typedef struct SoundSequence {
    u8 pad0[0x40];
    Vector4 pos;  /* 0x40 */
    u8 pad50[0x74 - 0x50];
    u32 flags;    /* 0x74 */
    u8 pad78[0x38];
    u8 *params;   /* 0xB0 */
    u8 state[0xC4 - 0xB4]; /* 0xB4 */
    s32 timer;    /* 0xC4 */
    struct SoundSequence *queued; /* 0xC8 */
    f32 rate;     /* 0xCC */
    s32 sources[4]; /* 0xD0 */
} SoundSequence;


s32 SoundSeq_Init(u8 *seq, u8 *data) {
    extern Matrix4 g_identity;
    s32 Thing_Init(void *thing, s32 arg1);
    u8 *list[0x80];
    u8 *params;
    s16 count;
    u32 n;

    Thing_Init(seq, 0);
    *(Matrix4 *)(seq + 0x10) = g_identity;
    *(Vector4 *)(seq + 0x40) = *(Vector4 *)data;
    SLOT_STORE((seq + 0xB0), SLOT_LOAD(u8 *, (data + 0x10)));
    *(s32 *)(seq + 0xC8) = 0;
    params = SLOT_LOAD(u8 *, (seq + 0xB0));
    if (*(u8 *)(params + 5) != 0) {
        count = 0;
        n = ThingManager_GetActiveSoundSequences(list, 0x80);
        while (n != 0) {
            n--;
            if (list[n] != seq && SLOT_LOAD(u8 *, (list[n] + 0xB0)) == SLOT_LOAD(u8 *, (seq + 0xB0))) {
                count++;
            }
        }
        params = SLOT_LOAD(u8 *, (seq + 0xB0));
        if (count >= *(u8 *)(params + 5)) {
            *(s32 *)(seq + 0xC4) = -1;
        } else {
            *(s32 *)(seq + 0xC4) = RandomAdd_s32(*(u32 *)params, *(u16 *)(params + 6));
        }
    } else {
        *(s32 *)(seq + 0xC4) = RandomAdd_s32(*(u32 *)params, *(u16 *)(params + 6));
    }
    if (*(u32 *)SLOT_LOAD(u8 *, (seq + 0xB0)) == 0) {
        *(u32 *)(seq + 0x74) |= 0x10000000;
    }
    *(f32 *)(seq + 0xCC) = 1.0f;
    params = SLOT_LOAD(u8 *, (seq + 0xB0));
    if (*(u32 *)params != 0 && *(u16 *)(params + 6) != 0) {
        *(f32 *)(seq + 0xCC) = 1.0f / ((f32)*(s32 *)(seq + 0xC4) / (f32)*(u32 *)params);
    }
    *(u8 *)(seq + 0xC0) = 1;
    *(u32 *)(seq + 0x74) |= 0xE0202;
    if (*(s8 *)(data + 0x14) != 0) {
        *(u32 *)(seq + 0x74) |= 0x10;
    }
    *(s32 *)(seq + 0xD0) = -1;
    *(s32 *)(seq + 0xD4) = -1;
    *(s32 *)(seq + 0xD8) = -1;
    *(s32 *)(seq + 0xDC) = -1;
    return 1;
}

static void SoundSeq_RemoveFromQueue(SoundSequence *seq) {
    SoundSequence *next;

    next = seq->queued;
    if (next != NULL) {
        next->flags &= ~0x80000000;
        seq->queued = NULL;
    } else {
        Trigger_SoundSequenceQueueEmptied();
    }
}

void SoundSeq_Release(void *thing) {
    SoundSequence *seq = thing;
    u32 i;

    if (((SoundSequence *)thing)->flags & 0x40000000) {
        SoundSeq_RemoveFromQueue(seq);
    }
    for (i = 0; i < 4; i++) {
        if (seq->sources[i] >= 0 && SoundSource_IsLooping(seq->sources[i])) {
            SoundSource_Stop(seq->sources[i]);
        }
        seq->sources[i] = -1;
    }
    Thing_Release(HND(thing));
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
    if (seq->flags & 0x80000000) {
        return 1;
    }
    if (!(seq->flags & 0x10000000)) {
        if (seq->timer < 0) {
            SoundSeq_Release(thing);
            return 0;
        }
        seq->timer -= g_iDeltaTimeMS;
    }
    key = ExtractSoundKey(seq->params + 8, seq->state, &seq->pos, g_fDeltaTimeMS * seq->rate);
    if (key >= 0) {
        found = 0;
        for (i = 0; i < 4; i++) {
            if (seq->sources[i] == -1) {
                found = 1;
                seq->sources[i] = key;
                if (SoundSource_IsLooping(key)) {
                    seq->flags |= 0x4000;
                }
                break;
            }
        }
        if (!found) {
            SoundSource_Stop(key);
        }
    } else if (key == -2) {
        if (!(seq->flags & 0x40000000)) {
            seq->flags |= 0x20;
        }
    }
    if (seq->flags & 0x10) {
        for (i = 0; i < 4; i++) {
            if (seq->sources[i] != -1) {
                if (SoundSource_IsLooping(seq->sources[i]) || SoundSource_IsActive(seq->sources[i])) {
                    SoundSource_Set(seq->sources[i], 5, &seq->pos);
                    playing = 1;
                } else {
                    SoundSource_Stop(seq->sources[i]);
                    seq->sources[i] = -1;
                }
            }
        }
    }
    if (!playing && (seq->flags & 0x20) && !(seq->flags & 0x40000000)) {
        Thing_Release(HND(thing));
    }
    return 1;
}


s32 SoundSeq_Invalidate(SoundSequence *seq, u32 start, u32 end)
{
    if ((u32)HND(seq->params) >= start && (u32)HND(seq->params) < end) {
        return 1;
    }
    return 0;
}

s32 SoundSequence_Play(s8 *seq, Vector4 *pos) {
    extern Vector4 g_zeroVector;
    extern s8 g_bFreezeWorld;
    Vector4 p;
    struct { Vector4 pos; s8 *seq; u8 flag; } params __attribute__((aligned(16)));
    u8 *thing;

    if (!seq[4] && PlayerManager_PlayerHumanCount() > 1) {
        return -1;
    }
    if (pos != NULL) {
        p = *pos;
    } else {
        p = g_zeroVector;
    }
    params.pos = p;
    params.seq = seq;
    params.flag = 0;
    thing = ThingManager_DARYL_CreateThing(0x10, &params);
    if (thing != NULL) {
        if (g_bFreezeWorld) {
            *(u32 *)(thing + 0x74) |= 0x100000;
        }
        return (s32)HND(thing);
    }
    return -1;
}

s32 SoundSequence_PlayQueued(s8 *seq, Vector4 *pos) {
    extern Vector4 g_zeroVector;
    extern s8 g_bFreezeWorld;
    Vector4 p;
    u8 *list[0x80];
    struct { Vector4 pos; s8 *seq; u8 flag; } params __attribute__((aligned(16)));
    u8 *created;
    u8 *thing;
    s8 found;
    u32 n;

    if (!seq[4] && PlayerManager_PlayerHumanCount() > 1) {
        thing = (u8 *)-1;
    } else {
        if (pos != NULL) {
            p = *pos;
        } else {
            p = g_zeroVector;
        }
        params.pos = p;
        params.seq = seq;
        params.flag = 0;
        created = ThingManager_DARYL_CreateThing(0x10, &params);
        if (created != NULL) {
            if (g_bFreezeWorld) {
                *(u32 *)(created + 0x74) |= 0x100000;
            }
            thing = created;
        } else {
            thing = (u8 *)-1;
        }
    }
    if ((u32)HND(thing) != 0xFFFFFFFF) {
        found = 0;
        n = ThingManager_GetActiveSoundSequences(list, 0x80);
        while (n != 0) {
            n--;
            if ((*(u32 *)(list[n] + 0x74) & 0x40000000) && SLOT_LOAD(u8 *, (list[n] + 0xc8)) == NULL) {
                found = 1;
                break;
            }
        }
        if (found) {
            SLOT_STORE((list[n] + 0xc8), thing);
            *(u32 *)(thing + 0x74) |= 0x80000000;
        }
        *(u32 *)(thing + 0x74) |= 0x40000000;
        SLOT_STORE((thing + 0xc8), NULL);
    }
    return (s32)HND(thing);
}


void SoundSequence_ClearQueue(void) {
    SoundSequence *list[128];
    u32 count;

    count = ThingManager_GetActiveSoundSequences((u8 **)list, 128);
    while (count != 0) {
        count--;
        if (list[count]->flags & 0x40000000) {
            list[count]->flags &= ~0x40000000;
            list[count]->queued = NULL;
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
        if (((SoundSequence *)list[count])->flags & 0x40000000) {
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

