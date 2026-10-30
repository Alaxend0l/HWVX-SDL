#include "common.h"

typedef struct AIStats {
    u32 unk0[7];
    u32 unk1C;
    u32 unk20[19];
} AIStats;

typedef struct AIEvent {
    void *data;
    void (*func)();
    struct AIEvent *next;
    s8 queued;
} AIEvent;

AIStats g_AI_Stats;
s16 g_AI_FramesSinceAsyncEventProcessed;


void AI_Create(void)
{
    extern s32 g_volumeTotal;
    extern void *gs_pAsyncEventHead_803C6F08;
    extern void *gs_pAsyncEventTail_803C6F04;
    extern u32 g_attackDelay;
    u32 var;

    g_volumeTotal = -1;
    gs_pAsyncEventHead_803C6F08 = NULL;
    gs_pAsyncEventTail_803C6F04 = NULL;
    var = GameSettings_GetCodeVariable(0x12d, 0);
    if (var >= 3)
    {
        g_attackDelay = 8000;
    }
}

void AI_Reset(void *arg0) {
    M2C_FIELD(arg0, s32 *, 0xC8) = 1;
    M2C_FIELD(arg0, s8 *, 0xCC) = 0;
}

void AI_Init(void) {
    M2C_UNK fn_800957D4();
    fn_800957D4();
}

void AI_AsyncProcess_80006C80(void) {
    AI_Car_AsyncProcess();
    AI_Creature_AsyncProcess();
}

u32 AI_UpdateTimer(u32 *arg0) {
    extern u32 g_iDeltaTimeMS;

    if (*arg0 > g_iDeltaTimeMS) {
        *arg0 -= g_iDeltaTimeMS;
    } else {
        *arg0 = 0;
    }
    return *arg0;
}

f32 AI_AngleToTarget(u8 *ai, Vector4 *target) {
    void ProjectVectorTangential(Vector4 *v, Vector4 *n, Vector4 *out);
    Vector4 dir __attribute__((aligned(16)));
    Vector4 cross __attribute__((aligned(16)));
    f32 dot;
    f32 angle;

    PSVECSubtract(target, (Vector4 *)(ai + 0x40), &dir);
    ProjectVectorTangential(&dir, (Vector4 *)(ai + 0x20), &dir);
    if (PSVECSquareMag(&dir)) {
        PSVECNormalize(&dir, &dir);
    }
    dot = PSVECDotProduct(ai + 0x30, &dir);
    if (dot > 1.0f) {
        dot = 1.0f;
    } else if (dot < -1.0f) {
        dot = -1.0f;
    }
    angle = acos(dot);
    PSVECCrossProduct((Vector4 *)(ai + 0x30), &dir, &cross);
    if (Distance_Plane(ai + 0x20, &cross) < 0.0f) {
        angle = -angle;
    }
    return angle;
}

s8 AI_CheckAngleInRange(f32 angle, f32 center, f32 range) {
    s8 inRange = 0;
    f32 diff;

    if (center == 0.0f && range == 0.0f) {
        return 1;
    }
    if (range == 180.0f) {
        return 1;
    }
    if (center > 180.0f) {
        center -= 360.0f;
    }
    diff = angle - center;
    if (diff > 180.0f) {
        diff -= 360.0f;
    } else if (diff < -180.0f) {
        diff += 360.0f;
    }
    if (diff <= range && diff >= -range) {
        inRange = 1;
    }
    return inRange;
}

s32 AI_ProcessEvent(void) {
    extern s16 g_AI_FramesSinceAsyncEventProcessed;
    extern AIStats g_AI_Stats;
    extern AIEvent *gs_pAsyncEventHead_803C6F08;
    extern AIEvent *gs_pAsyncEventTail_803C6F04;
    AIEvent *event;

    if (g_AI_FramesSinceAsyncEventProcessed <= 5 && g_AI_Stats.unk1C >= 250) {
        return 0;
    }
    event = gs_pAsyncEventHead_803C6F08;
    g_AI_FramesSinceAsyncEventProcessed = 0;
    if (event != NULL) {
        gs_pAsyncEventHead_803C6F08 = event->next;
        if (gs_pAsyncEventHead_803C6F08 == NULL) {
            gs_pAsyncEventTail_803C6F04 = NULL;
        }
        event->queued = 0;
        if (event->data != NULL) {
            event->func(event->data);
        } else {
            event->func();
        }
        return 1;
    }
    return 0;
}

void AI_AddEvent(void *arg0) {
    extern void *gs_pAsyncEventHead_803C6F08;
    extern void *gs_pAsyncEventTail_803C6F04;
    if ((s8) M2C_FIELD(arg0, u8 *, 0xC) == 0) {
        M2C_FIELD(arg0, s32 *, 8) = 0;
        if ((void *) gs_pAsyncEventTail_803C6F04 != NULL) {
            SLOT_STORE((u8 *)(gs_pAsyncEventTail_803C6F04) + 8, arg0);
        } else {
            gs_pAsyncEventHead_803C6F08 = arg0;
        }
        gs_pAsyncEventTail_803C6F04 = arg0;
        M2C_FIELD(arg0, u8 *, 0xC) = 1U;
    }
}

s32 AI_Heartbeat(u8 *ai) {

    if (!Player_AIControlReader(ai)) {
        return 0;
    }
    M2C_FIELD(ai, u8 *, 0x1C) = 0;
    if (M2C_FIELD(ai, u8 *, 0xCD) != 0 && SLOT_LOAD(void *, (u8 *)(ai) + 0x2C) == NULL) {
        M2C_FIELD(ai, s8 *, 0xCC) = 1;
    }
    if (M2C_FIELD(ai, u32 *, 0xC8) != 0) {
        AI_UpdateTimer((u32 *)(ai + 0xC8));
        if (M2C_FIELD(ai, u32 *, 0xC8) == 0) {
            AI_AddEvent(ai + 0xB8);
        }
    }
    if (M2C_FIELD(ai, s8 *, 0xCC) || !M2C_FIELD(ai, u8 *, 0xCD)) {
        return 1;
    }
    return 1;
}

void AI_AsyncHeartbeat(void *arg0) {
    M2C_FIELD(arg0, s32 *, 0xC8) = 0x1F4;
}

