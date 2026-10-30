#include "common.h"


s32 g_iDeltaTimeMS;
s32 g_iLastDeltaTimeMS;
s32 g_iDeltaTime60th;
f32 g_fDeltaTimeMS;
f32 g_fDeltaTime60th;
s32 g_iLastTimeMS;
f32 g_fDeltaTimeSec;
f32 g_fDeltaTimeSec_inv;
u8 g_iTimeShift;

s32 g_timer300 = 300;
s32 g_timer163 = 163;
s32 g_timer78 = 78;
void TimeHandler_SetSlowMotion(void) {
    if (g_iTimeShift) {
        g_iTimeShift = 0;
    } else {
        g_iTimeShift = 1;
    }
}

void Timer_Heartbeat(void) {
    if (g_timer300 < 0) {
        g_timer300 += 300;
    }
    g_timer300 -= g_iDeltaTimeMS;
    if (g_timer163 < 0) {
        g_timer163 += 163;
    }
    g_timer163 -= g_iDeltaTimeMS;
    if (g_timer78 < 0) {
        g_timer78 += 78;
    }
    g_timer78 -= g_iDeltaTimeMS;
}

s8 Timer_Interrupt300(void) {
    return (u32)g_timer300 >> 31;
}
