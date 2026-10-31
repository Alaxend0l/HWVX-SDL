/* What the car and creature AI share (AI.h): the AIPlayer header, the event
 * queue that spreads the expensive "async" work over frames, and steering
 * helpers. */
#include "common.h"
#include "CarA.h"
#include "AI.h"

typedef struct AIStats {
    u32 unk0[7];
    u32 unk1C;
    u32 unk20[19];
} AIStats;

AIStats g_AI_Stats;
s16 g_AI_FramesSinceAsyncEventProcessed;

/* .sbss in the original (local to AI.o) */
AIEvent *gs_pAsyncEventTail;
AIEvent *gs_pAsyncEventHead;

/* Level start: empties the event queue; the creature AI sets itself up again
 * (g_volumeTotal = -1). */
void AI_Create(void)
{
    extern s32 g_volumeTotal;
    extern u32 g_attackDelay;
    u32 var;

    g_volumeTotal = -1;
    gs_pAsyncEventHead = NULL;
    gs_pAsyncEventTail = NULL;
    var = GameSettings_GetCodeVariable(0x12d, 0);
    if (var >= 3)
    {
        g_attackDelay = 8000;
    }
}

void AI_Reset(AIPlayer *ai) {
    ai->heartbeatTimer = 1;
    ai->reset = 0;
}

void AI_Init(void *player, s32 arg1) {
    /* passes its arguments straight on (asm); the decomp has (void) */
    Player_Init(player, arg1);
}

void AI_AsyncProcess_80006C80(void) {

    AI_Car_AsyncProcess();
    AI_Creature_AsyncProcess();
}

/* Counts a ms timer down by the frame time, to 0; returns what's left. */
u32 AI_UpdateTimer(u32 *timer) {
    extern u32 g_iDeltaTimeMS;

    if (*timer > g_iDeltaTimeMS) {
        *timer -= g_iDeltaTimeMS;
    } else {
        *timer = 0;
    }
    return *timer;
}

/* Signed angle (radians) from the thing's forward axis to target, in its
 * horizontal plane; negative to the left of it (by the up axis). */
f32 AI_AngleToTarget(void *thing, Vector4 *target) {
    Thing *t = thing;
    Vector4 dir __attribute__((aligned(16)));
    Vector4 cross __attribute__((aligned(16)));
    f32 dot;
    f32 angle;

    PSVECSubtract(target, &t->pos, &dir);
    ProjectVectorTangential(&dir, &t->axis[1], &dir);
    if (PSVECSquareMag(&dir)) {
        PSVECNormalize(&dir, &dir);
    }
    dot = PSVECDotProduct(&t->axis[2], &dir);
    if (dot > 1.0f) {
        dot = 1.0f;
    } else if (dot < -1.0f) {
        dot = -1.0f;
    }
    angle = acos(dot);
    PSVECCrossProduct(&t->axis[2], &dir, &cross);
    if (Distance_Plane(&t->axis[1], &cross) < 0.0f) {
        angle = -angle;
    }
    return angle;
}

/* Whether angle is within center +- range (degrees); 0/0 or a range of 180
 * means any angle. */
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

/* Runs the first queued event; returns whether it did. Skipped for up to 5
 * frames while g_AI_Stats.unk1C >= 250. */
s32 AI_ProcessEvent(void) {
    extern s16 g_AI_FramesSinceAsyncEventProcessed;
    extern AIStats g_AI_Stats;
    AIEvent *event;

    if (g_AI_FramesSinceAsyncEventProcessed <= 5 && g_AI_Stats.unk1C >= 250) {
        return 0;
    }
    event = gs_pAsyncEventHead;
    g_AI_FramesSinceAsyncEventProcessed = 0;
    if (event != NULL) {
        gs_pAsyncEventHead = GETP(event->next);
        if (gs_pAsyncEventHead == NULL) {
            gs_pAsyncEventTail = NULL;
        }
        event->queued = 0;
        if (GETP(event->data) != NULL) {
            GETP(event->func)(GETP(event->data));
        } else {
            GETP(event->func)();
        }
        return 1;
    }
    return 0;
}

/* Queues an AIEvent unless it is queued already. */
void AI_AddEvent(void *arg) {
    AIEvent *event = arg;

    if (event->queued == 0) {
        SETP(event->next, NULL);
        if (gs_pAsyncEventTail != NULL) {
            SETP(gs_pAsyncEventTail->next, event);
        } else {
            gs_pAsyncEventHead = event;
        }
        gs_pAsyncEventTail = event;
        event->queued = 1;
    }
}

/* The per-frame part every AI kind runs: queues the AsyncHeartbeat when its
 * timer runs out; an active AI that lost its vehicle is reset. Returns 0 if
 * the player's controls aren't the AI's to set. */
s32 AI_Heartbeat(AIPlayer *ai) {

    if (!Player_AIControlReader((s8 *)ai)) {
        return 0;
    }
    PLAYER_BUTTON1C(ai) = 0;
    if (ai->state != AI_STATE_OFF && GETP(AI_PLAYER(ai)->vehicle) == NULL) {
        ai->reset = 1;
    }
    if (ai->heartbeatTimer != 0) {
        AI_UpdateTimer(&ai->heartbeatTimer);
        if (ai->heartbeatTimer == 0) {
            AI_AddEvent(&ai->heartbeat);
        }
    }
    if (ai->reset || !ai->state) {
        return 1;
    }
    return 1;
}

/* Every AsyncHeartbeat starts with this: next one in 500 ms by default. */
void AI_AsyncHeartbeat(AIPlayer *ai) {
    ai->heartbeatTimer = 500;
}
