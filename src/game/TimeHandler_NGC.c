#include "common.h"

typedef struct OSCalendarTime {
    s32 sec;
    s32 min;
    s32 hour;
    s32 mday;
    s32 mon;
    s32 year;
    s32 wday;
    s32 yday;
    s32 msec;
    s32 usec;
} OSCalendarTime;


OSCalendarTime g_todaysDate;
u32 max_time;
u32 m_iDeltaTimeUs;
u32 m_iLastTimeUs;
/* Port fix: g_iDeltaTimeMS is whole ms and the division below used to drop the
 * rest, so ms timers ran up to 2x slow at high frame rates (4% at 60 FPS). Carry
 * it into the next frame so they add up to real time at any frame rate. */
static u32 s_deltaRemainderUs;
/* This frame's values, so that TimeHandler_SetTimeElapsedSec_f32 restoring the
 * frame's time doesn't truncate them again. */
static u32 s_frameDeltaMS;
static f32 s_frameDeltaSec;
/* The previous frame's time in whole ms, truncated: the suspension dampers divide
 * by it and were tuned on that value. */
static u32 s_truncatedDeltaMS;

static inline u32 TimeHandler_GetTimeUs(void) {
    return (OSGetTick() * 8) / (OS_TIMER_CLOCK / 125000);
}

void TimeHandler_Init(void) {
    u32 now = (OSGetTick() * 8) / (OS_TIMER_CLOCK / 125000);

    m_iDeltaTimeUs = 1;
    s_deltaRemainderUs = 0;
    m_iLastTimeUs = now;
    OSTicksToCalendarTime(OSGetTime(), &g_todaysDate);
}

void TimeHandler_Heartbeat(void) {
    extern u32 g_iDeltaTimeMS;
    extern u32 g_iLastDeltaTimeMS;
    extern u32 g_iLastTimeMS;
    extern u32 g_iDeltaTime60th;
    extern f32 g_fDeltaTimeMS;
    extern f32 g_fDeltaTime60th;
    extern f32 g_fDeltaTimeSec;
    extern f32 g_fDeltaTimeSec_inv;
    u32 now = TimeHandler_GetTimeUs();

    if (max_time < now) {
        max_time = now;
    }
    if (m_iLastTimeUs > now) {
        m_iDeltaTimeUs = now + max_time - m_iLastTimeUs;
    } else {
        m_iDeltaTimeUs = now - m_iLastTimeUs;
    }
    if (Port_FixedStepUs() != 0) {
        m_iDeltaTimeUs = Port_FixedStepUs();   /* port: a repeatable run (port.c) */
    }
    if (m_iDeltaTimeUs >= 34000) {
        m_iDeltaTimeUs = 34000;
    }
    if (m_iDeltaTimeUs < 1000) {
        m_iDeltaTimeUs = 1000;
    }
    m_iLastTimeUs = now;
    g_iLastDeltaTimeMS = s_truncatedDeltaMS;
    s_truncatedDeltaMS = m_iDeltaTimeUs / 1000;
    s_deltaRemainderUs += m_iDeltaTimeUs;
    g_iDeltaTimeMS = s_deltaRemainderUs / 1000;
    s_deltaRemainderUs %= 1000;
    g_fDeltaTimeMS = (f32)m_iDeltaTimeUs / 1000.0f;
    g_iLastTimeMS = m_iLastTimeUs / 1000;
    g_fDeltaTime60th = g_fDeltaTimeMS / (1000.0f / 60.0f);
    g_fDeltaTimeSec = g_fDeltaTimeMS / 1000.0f;
    g_iDeltaTime60th = g_fDeltaTime60th;
    g_fDeltaTimeSec_inv = 1.0f / g_fDeltaTimeSec;
    s_frameDeltaMS = g_iDeltaTimeMS;
    s_frameDeltaSec = g_fDeltaTimeSec;
}

void TimeHandler_SetTimeElapsedSec_f32(f32 sec) {
    extern f32 g_fDeltaTime60th;
    extern f32 g_fDeltaTimeSec;
    extern f32 g_fDeltaTimeSec_inv;
    extern u32 g_iDeltaTimeMS;
    u32 ms;

    g_iDeltaTimeMS = ms = sec == s_frameDeltaSec ? s_frameDeltaMS : (u32)(1000.0f * sec);
    g_fDeltaTimeSec = sec;
    g_fDeltaTimeSec_inv = 1.0f / sec;
    g_fDeltaTime60th = ms / (1000.0f / 60.0f);
}

void Performance_Start(void) {

}

void Performance_End(void) {

}

void Release_Performance_Get(u32 *arg0) {
    *arg0 = (OSGetTick() * 8) / (OS_TIMER_CLOCK / 125000);
}

void Release_Performance_Diff(u32 *arg0) {
    extern u32 max_time;
    u32 now;

    now = (OSGetTick() * 8) / (OS_TIMER_CLOCK / 125000);
    if (now < *arg0) {
        now += max_time;
    }
    *arg0 = now - *arg0;
}

