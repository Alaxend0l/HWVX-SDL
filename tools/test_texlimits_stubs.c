/* Stub replacements for src/port/main_sdl.c (as test_dlcache_stubs.c), so the GX tests can supply their
 * own main() and a window-less renderer. Nothing here runs: the tests only
 * call the GX API. The globals mirror the ones main_sdl.c owns. */
#include <stdlib.h>
#include <time.h>

#include "port_internal.h"

s64 g_resetTicks;
s32 shithack;
s32 m_ilpreset_803C7620;
s32 m_reset;
s32 m_cd_drive_status;
s8 g_bBlockReset;
s32 g_viewID;
s8 NGC_Thread_On;
u8 Thread[0x310];
u8 ThreadStack[0x5000];
void *g_frameBuffer1;
void *g_frameBuffer2;
void *g_currentBuffer;
void *rmode;

PortState g_port;

void ForceIPLReset(void) { }
s32 Init_Display(void) { return 0; }
s32 RunThread(void *(*func)(void *)) { (void)func; return 0; }
s8 isInThread(void) { return 0; }
void EndThread(void) { }
void TextureManager_ReleaseCache(void) { }
int Port_ResetSave(void) { return 0; }
int Port_GCAirCamera(void) { return 0; }
int Port_PCPan(void) { return 0; }

void GetSystemDate(s32 *year, s32 *month, s32 *day, s32 *hour, s32 *minute)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);

    if (year) *year = lt->tm_year + 1900;
    if (month) *month = lt->tm_mon + 1;
    if (day) *day = lt->tm_mday;
    if (hour) *hour = lt->tm_hour;
    if (minute) *minute = lt->tm_min;
}

void Port_CreateWindow(SDL_WindowFlags flags) { (void)flags; abort(); }
int Port_TryCreateWindow(SDL_WindowFlags flags) { (void)flags; return 0; }
