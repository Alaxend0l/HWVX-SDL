/* Emitters: things at a fixed spot that keep spawning effects (event
 * sequences) from their description, e.g. the effect points of the world's
 * quadrants and teleporters. */
#include "common.h"
#include "ThingManager.h"
#include "World.h"

extern u32 g_emitterVTable[17];  /* port: generated from the original .data (data_gen.c) */

/* Emitter.flags (kind-specific bits) */
enum {
    EMITTER_FLAG_RANGE_DIRTY = 0x10,      /* recompute the camera distance */
    EMITTER_FLAG_DISABLED = 0x02000000,   /* ActionFXPointChangeMode */
};

/* An emitter's description (EMIX data). */
typedef struct EmitterDesc {
    u8 hasRange;                /* 0x00 range is valid */
    u8 pickMode;                /* 0x01 0: fixed, 1: random, 2: in order */
    u8 limited;                 /* 0x02 stops after `count` effects */
    u8 pad3;                    /* 0x03 */
    s32 count;                  /* 0x04 */
    s32 delayBase;              /* 0x08 ms between effects: RandomAdd(base, range) */
    s32 delayRange;             /* 0x0C */
    f32 range;                  /* 0x10 only emits within this of a camera, 0: always */
    f32 speedScale;             /* 0x14 moving emitters emit faster */
    u32 fxCount;                /* 0x18 */
    PTR32(u32) fxIds;           /* 0x1C event sequence descriptions */
    u32 soundSeq;               /* 0x20 sound sequence started with it, 0: none */
} EmitterDesc;

typedef struct Emitter {
    Thing base;                 /* 0x00 trigger (0x90): the spawning entry */
    u8 padAC[4];                /* 0xAC */
    PTR32(EmitterDesc) desc;    /* 0xB0 */
    u32 fxId;                   /* 0xB4 the effect it emits next */
    u32 padB8;                  /* 0xB8 */
    u32 timeToNextFx;           /* 0xBC ms */
    u32 fxIndex;                /* 0xC0 */
    s32 fxCount;                /* 0xC4 effects left (limited), 0: unlimited */
    f32 range;                  /* 0xC8 */
    f32 distSq;                 /* 0xCC to the nearest camera */
} Emitter;

_Static_assert(offsetof(Emitter, desc) == 0xB0, "Emitter.desc");
_Static_assert(sizeof(Emitter) == 0xD0, "Emitter size (ThingPools.emitters)");

typedef struct ActionFXList {
    s32 mode;                   /* 0x00 0: disable, 1: enable, 2: toggle */
    u32 pointCount;             /* 0x04 */
    PTR32(QuadFxPoint) points[1]; /* 0x08 */
} ActionFXList;

u32 Emitter_InCameraRange(Emitter *emitter, f32 range);

extern u8 g_CurrentViews;
s32 Emitter_Init(Emitter *emitter, struct EmitterParams *data) {
    SoundSeqParams params __attribute__((aligned(16)));
    EmitterDesc *desc;

    Thing_Init(&emitter->base, (s32)HND(data));
    emitter->base.flags = 0;
    emitter->base.collisionType = THING_COLLIDE_NONE;
    emitter->base.quat = data->quat;
    emitter->base.pos = data->pos;
    emitter->base.pos.w = 1.0f;
    QuaternionToSubMatrix(&emitter->base.quat, THING_MTX(&emitter->base));
    SETP(emitter->desc, GETP(data->emitter));
    emitter->timeToNextFx = 0;
    emitter->base.player.h = data->owner;
    emitter->range = data->drawDist;
    if (emitter->range == 0.0f) {
        desc = GETP(data->emitter);
        emitter->range = desc->hasRange ? desc->range : 0.0f;
    }
    desc = GETP(emitter->desc);
    if (desc->limited) {
        emitter->fxCount = desc->count;
    } else {
        emitter->fxCount = 0;
    }
    desc = GETP(emitter->desc);
    if (!desc->pickMode) {
        emitter->fxIndex = RandomAdd_s32(0, desc->fxCount);
    }
    if (GETP(emitter->desc)->soundSeq != 0) {
        params.flag = 0;
        params.pos = emitter->base.pos;
        SETP(params.seq, PTR(GETP(emitter->desc)->soundSeq));
        ThingManager_DARYL_CreateThing(THING_TYPE_SOUND_SEQ, &params);
    }
    emitter->base.flags |= 0x80202;
    return 1;
}

s32 Emitter_Move(Emitter *emitter) {
    extern f32 g_fDeltaTimeMS;
    extern Vector4 g_zeroVector;
    u32 dt;

    if (emitter->base.flags & EMITTER_FLAG_DISABLED) {
        return 1;
    }
    emitter->base.flags |= EMITTER_FLAG_RANGE_DIRTY;
    if (GETP(emitter->desc)->speedScale > 0.0f) {
        dt = g_fDeltaTimeMS * (GETP(emitter->desc)->speedScale * MagnitudeVector(&emitter->base.vel) + 1.0f);
        if (dt > 1000) {
            dt = 1000;
        }
    } else {
        dt = g_fDeltaTimeMS;
    }
    if (GETP(emitter->desc)->fxCount != 0) {
        for (;;) {
            if (emitter->timeToNextFx != 0) {
                if (dt > emitter->timeToNextFx) {
                    dt -= emitter->timeToNextFx;
                } else {
                    emitter->timeToNextFx -= dt;
                    return 1;
                }
            }
            emitter->timeToNextFx = RandomAdd_s32(GETP(emitter->desc)->delayBase, GETP(emitter->desc)->delayRange);
            if (GETP(emitter->desc)->fxCount != 0) {
                if (GETP(emitter->desc)->pickMode == 1) {
                    emitter->fxIndex = RandomAdd_s32(0, GETP(emitter->desc)->fxCount);
                } else if (GETP(emitter->desc)->pickMode == 2) {
                    emitter->fxIndex++;
                    if (emitter->fxIndex >= GETP(emitter->desc)->fxCount) {
                        emitter->fxIndex = 0;
                    }
                }
                emitter->fxId = GETP(GETP(emitter->desc)->fxIds)[emitter->fxIndex];
            }
            if (emitter->fxId != 0 && Emitter_InCameraRange(emitter, emitter->range)) {
                DebrisParams params __attribute__((aligned(16)));

                params.debris = emitter->fxId;
                params.pos = emitter->base.pos;
                params.quat = *(Vector4 *)&emitter->base.quat;
                params.vel = emitter->base.vel;
                params.owner = emitter->base.player.h;
                params.unk48 = GETP(emitter->desc)->speedScale;
                params.unk4C = 0.0f;
                params.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &params);
            }
            if (emitter->fxCount > 0) {
                if (emitter->fxCount == 1) {
                    Thing_Release(emitter);
                    break;
                }
                emitter->fxCount--;
            }
        }
    }
    return 1;
}

void Emitter_Release(Emitter *emitter) {
    QuadFxPoint *slot;

    if (GETP(emitter->base.vtable) == g_emitterVTable) {
        /* detach from its spawn entry */
        slot = GETP(emitter->base.trigger);
        if (slot != NULL) {
            SETP(emitter->base.trigger, NULL);
            SETP(slot->thing, NULL);
        }
    }
    Thing_Release(emitter);
}

u32 Emitter_InCameraRange(Emitter *emitter, f32 range) {
    Vector4 positions[5] __attribute__((aligned(16)));
    Vector4 *pos;
    f32 dist;
    u32 count;
    u32 i;

    if (range == 0.0f) {
        return 1;
    }
    if (emitter->base.flags & EMITTER_FLAG_RANGE_DIRTY) {
        count = Gfx_GetCameraPositions(positions);
        emitter->distSq = PSVECSquareDistance(&emitter->base.pos, (f32 *)&positions[0]);
        if (count > 1) {
            for (i = 0, pos = positions; i < 5; i++, pos++) {
                if (g_CurrentViews & (1 << i)) {
                    dist = PSVECSquareDistance(&emitter->base.pos, pos);
                    if (dist < emitter->distSq) {
                        emitter->distSq = dist;
                    }
                }
            }
        }
        emitter->base.flags &= ~EMITTER_FLAG_RANGE_DIRTY;
    }
    return range * range > emitter->distSq;
}

s32 ActionFXPointChangeMode(ActionFXList *list) {
    s32 mode = list->mode;
    u32 count = list->pointCount;
    u32 i;

    for (i = 0; i < count; i++) {
        QuadFxPoint *point = GETP(list->points[i]);
        Emitter *fx = (Emitter *)GETP(point->thing);

        switch (mode) {
        case 0:
            if (fx != NULL) {
                fx->base.flags |= EMITTER_FLAG_DISABLED;
            }
            break;
        case 1:
            if (fx != NULL) {
                fx->base.flags &= ~EMITTER_FLAG_DISABLED;
            }
            break;
        case 2:
            if (fx->base.flags & EMITTER_FLAG_DISABLED) {
                if (fx != NULL) {
                    fx->base.flags &= ~EMITTER_FLAG_DISABLED;
                }
            } else {
                if (fx != NULL) {
                    fx->base.flags |= EMITTER_FLAG_DISABLED;
                }
            }
            break;
        }
        point->mode = mode;
    }
    return 1;
}

s32 Emitter_Invalidate(Emitter *emitter, u32 start, u32 end) {
    if (emitter->desc.h >= start && emitter->desc.h < end) {
        return 1;
    }
    return 0;
}
