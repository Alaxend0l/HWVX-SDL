#include "common.h"
#include "KeyState.h"

/* A force feedback effect in the data pack (ForceFeedback_Play): within a
 * range of its position it can set the pad's "active" flag, rumble, and push
 * the wheel, each for its own time. */
typedef struct ForceFeedbackEffect {
    u8 pad00[0x10];             /* 0x00 */
    s32 forceTime;              /* 0x10 ms */
    s32 activeTime;             /* 0x14 */
    s32 rumbleTime;             /* 0x18 */
    u8 pad1C[8];                /* 0x1C */
    PTR32(KeyTrack) forceKeys;  /* 0x24 ExtractF32Key: strength */
    PTR32(KeyTrack) rumbleKeys; /* 0x28 */
    KeyTrack rangeKeys;         /* 0x2C ExtractPointKey: near (x) and far (y) range */
} ForceFeedbackEffect;

typedef struct ForceFeedback {
    PTR32(ForceFeedbackEffect) effect; /* 0x00 NULL: slot free */
    KeyPointState range;        /* 0x04 x: full strength within, y: none beyond */
    KeyF32State value;          /* 0x20 strength, 0-1 */
    Vector4 pos;                /* 0x40 */
    s32 forceTime;              /* 0x50 ms left */
    s32 activeTime;             /* 0x54 */
    s32 rumbleTime;             /* 0x58 */
    u8 pad5C[4];                /* 0x5C */
} ForceFeedback;

_Static_assert(offsetof(ForceFeedbackEffect, rangeKeys) == 0x2C, "ForceFeedbackEffect.rangeKeys");
_Static_assert(offsetof(ForceFeedback, value) == 0x20, "ForceFeedback.value");
_Static_assert(offsetof(ForceFeedback, pos) == 0x40, "ForceFeedback.pos");
_Static_assert(sizeof(ForceFeedback) == 0x60, "ForceFeedback size");

ForceFeedback g_forcefeedback[24];

extern f32 g_fDeltaTimeMS;
extern s32 g_iDeltaTimeMS;

void ForceFeedback_Init(void)
{
    s32 i;

    for (i = 0; i < 24; i++)
    {
        SETP(g_forcefeedback[i].effect, 0);
    }
}

void ForceFeedback_Play(ForceFeedbackEffect *effect, Vector4 *dir) {
    u32 i;

    for (i = 0; i < 24; i++) {
        if (GETP(g_forcefeedback[i].effect) == 0) {
            SETP(g_forcefeedback[i].effect, effect);
            g_forcefeedback[i].range.reset = 1;
            g_forcefeedback[i].value.reset = 1;
            g_forcefeedback[i].pos = *dir;
            g_forcefeedback[i].activeTime = effect->activeTime;
            g_forcefeedback[i].rumbleTime = effect->rumbleTime;
            g_forcefeedback[i].forceTime = effect->forceTime;
            return;
        }
    }
}

void ForceFeedback_Heartbeat(void)
{
    ForceFeedback *ff;
    s32 i;

    for (i = 0; i < 24; i++)
    {
        ff = &g_forcefeedback[i];
        if (GETP(ff->effect) != NULL)
        {
            ExtractPointKey((u8 *)&GETP(ff->effect)->rangeKeys, (u8 *)&ff->range, g_fDeltaTimeMS);
            if (ff->activeTime > 0)
            {
                ff->activeTime -= g_iDeltaTimeMS;
            }
            if (ff->rumbleTime > 0)
            {
                ff->rumbleTime -= g_iDeltaTimeMS;
                if (ff->rumbleTime >= 0)
                {
                    ExtractF32Key((u8 *)GETP(GETP(ff->effect)->rumbleKeys), (u8 *)&ff->value, g_fDeltaTimeMS);
                }
            }
            if (ff->forceTime > 0)
            {
                ff->forceTime -= g_iDeltaTimeMS;
                if (ff->forceTime >= 0)
                {
                    ExtractF32Key((u8 *)GETP(GETP(ff->effect)->forceKeys), (u8 *)&ff->value, g_fDeltaTimeMS);
                }
            }
        }
        if (ff->activeTime <= 0 && ff->rumbleTime <= 0 && ff->forceTime <= 0)
        {
            SETP(ff->effect, NULL);
        }
    }
}


void ForceFeedback_GetForce(void *ff, u8 *a, u8 *b, f32 *c)
{
    s32 i;
    u8 active;
    s32 rumble;
    ForceFeedback *fe;
    f32 force;
    f32 dist;
    f32 scale;

    force = 0.0f;
    rumble = 0;
    active = 0;
    if (ff != NULL)
    {
        for (i = 0; i < 24; i++)
        {
            fe = &g_forcefeedback[i];
            if (GETP(fe->effect) != NULL)
            {
                DistanceXYZ(ff, &fe->pos, &dist);
                if (fe->activeTime > 0 && dist < fe->range.y)
                {
                    active = 1;
                }
                if (fe->rumbleTime > 0 && dist < fe->range.y)
                {
                    scale = (fe->range.y - dist) / (fe->range.y - fe->range.x);
                    scale *= fe->value.value;
                    scale *= 255.0f;
                    if (scale > rumble)
                    {
                        rumble = scale;
                    }
                }
                if (fe->forceTime > 0 && dist < fe->range.y)
                {
                    scale = (fe->range.y - dist) / (fe->range.y - fe->range.x);
                    if (scale > force)
                    {
                        force = scale;
                    }
                }
            }
        }
    }
    *a = active;
    *b = rumble;
    *c = force;
}

