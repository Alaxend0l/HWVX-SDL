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

static inline u32 TimeHandler_GetTimeUs(void) {
    return (OSGetTick() * 8) / ((*(u32 *)0x800000F8 / 4) / 125000);
}

void TimeHandler_Init(void) {
    u32 now = (OSGetTick() * 8) / ((*(u32 *)0x800000F8 / 4) / 125000);

    m_iDeltaTimeUs = 1;
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
    if (m_iDeltaTimeUs >= 34000) {
        m_iDeltaTimeUs = 34000;
    }
    if (m_iDeltaTimeUs < 1000) {
        m_iDeltaTimeUs = 1000;
    }
    m_iLastTimeUs = now;
    g_iLastDeltaTimeMS = g_iDeltaTimeMS;
    g_iDeltaTimeMS = m_iDeltaTimeUs / 1000;
    g_fDeltaTimeMS = (f32)m_iDeltaTimeUs / 1000.0f;
    g_iLastTimeMS = m_iLastTimeUs / 1000;
    g_fDeltaTime60th = g_fDeltaTimeMS / (1000.0f / 60.0f);
    g_fDeltaTimeSec = g_fDeltaTimeMS / 1000.0f;
    g_iDeltaTime60th = g_fDeltaTime60th;
    g_fDeltaTimeSec_inv = 1.0f / g_fDeltaTimeSec;
}

void TimeHandler_SetTimeElapsedSec_f32(f32 sec) {
    extern f32 g_fDeltaTime60th;
    extern f32 g_fDeltaTimeSec;
    extern f32 g_fDeltaTimeSec_inv;
    extern u32 g_iDeltaTimeMS;
    u32 ms;

    g_iDeltaTimeMS = ms = 1000.0f * sec;
    g_fDeltaTimeSec = sec;
    g_fDeltaTimeSec_inv = 1.0f / sec;
    g_fDeltaTime60th = ms / (1000.0f / 60.0f);
}

void Performance_Start(void) {

}

void Performance_End(void) {

}

void Release_Performance_Get(u32 *arg0) {
    *arg0 = (OSGetTick() * 8) / ((*(u32 *)0x800000F8 / 4) / 125000);
}

void Release_Performance_Diff(u32 *arg0) {
    extern u32 max_time;
    u32 now;

    now = (OSGetTick() * 8) / ((*(u32 *)0x800000F8 / 4) / 125000);
    if (now < *arg0) {
        now += max_time;
    }
    *arg0 = now - *arg0;
}

