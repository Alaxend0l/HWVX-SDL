#include "common.h"

typedef struct ForceFeedback {
    void *effect;                 /* 0x00 */
    f32 point[0x1C / 4 - 1];      /* 0x04 */
    u8 active;                    /* 0x1C */
    u8 pad1D[0x20 - 0x1D];        /* 0x1D */
    f32 value;                    /* 0x20 */
    u8 pad24[0x30 - 0x24];        /* 0x24 */
    u8 playing;                   /* 0x30 */
    u8 pad31[0x40 - 0x31];        /* 0x31 */
    Vector4 dir;                  /* 0x40 */
    s32 magnitude;                /* 0x50 */
    s32 unk54;                    /* 0x54 */
    s32 unk58;                    /* 0x58 */
    u8 pad5C[0x60 - 0x5C];        /* 0x5C */
} ForceFeedback;

ForceFeedback g_forcefeedback[24];

extern f32 g_fDeltaTimeMS;
extern s32 g_iDeltaTimeMS;
void DistanceXYZ(void *a, void *b, f32 *dist);

void ForceFeedback_Init(void)
{
    s32 i;

    for (i = 0; i < 24; i++)
    {
        g_forcefeedback[i].effect = 0;
    }
}

void ForceFeedback_Play(void *effect, Vector4 *dir) {
    u32 i;

    for (i = 0; i < 24; i++) {
        if (g_forcefeedback[i].effect == 0) {
            g_forcefeedback[i].effect = effect;
            g_forcefeedback[i].active = 1;
            g_forcefeedback[i].playing = 1;
            g_forcefeedback[i].dir = *dir;
            g_forcefeedback[i].unk54 = M2C_FIELD(effect, s32 *, 0x14);
            g_forcefeedback[i].unk58 = M2C_FIELD(effect, s32 *, 0x18);
            g_forcefeedback[i].magnitude = M2C_FIELD(effect, s32 *, 0x10);
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
        if (ff->effect != NULL)
        {
            ExtractPointKey((char *)ff->effect + 0x2C, ff->point, g_fDeltaTimeMS);
            if (ff->unk54 > 0)
            {
                ff->unk54 -= g_iDeltaTimeMS;
            }
            if (ff->unk58 > 0)
            {
                ff->unk58 -= g_iDeltaTimeMS;
                if (ff->unk58 >= 0)
                {
                    ExtractF32Key(SLOT_LOAD(u8 *, ((char *)ff->effect + 0x28)), &ff->value, g_fDeltaTimeMS);
                }
            }
            if (ff->magnitude > 0)
            {
                ff->magnitude -= g_iDeltaTimeMS;
                if (ff->magnitude >= 0)
                {
                    ExtractF32Key(SLOT_LOAD(u8 *, ((char *)ff->effect + 0x24)), &ff->value, g_fDeltaTimeMS);
                }
            }
        }
        if (ff->unk54 <= 0 && ff->unk58 <= 0 && ff->magnitude <= 0)
        {
            ff->effect = NULL;
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
            if (fe->effect != NULL)
            {
                DistanceXYZ(ff, &fe->dir, &dist);
                if (fe->unk54 > 0 && dist < fe->point[1])
                {
                    active = 1;
                }
                if (fe->unk58 > 0 && dist < fe->point[1])
                {
                    scale = (fe->point[1] - dist) / (fe->point[1] - fe->point[0]);
                    scale *= fe->value;
                    scale *= 255.0f;
                    if (scale > rumble)
                    {
                        rumble = scale;
                    }
                }
                if (fe->magnitude > 0 && dist < fe->point[1])
                {
                    scale = (fe->point[1] - dist) / (fe->point[1] - fe->point[0]);
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

