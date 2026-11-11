/* main_sdl.c's side of the game for tools/test_content.c: the globals and
 * the small platform hooks, without a window (the test brings its own main()
 * and a stub renderer). The loader thread and the heap are main_sdl.c's. */
#include <SDL3/SDL.h>

#include "port_internal.h"
#include "gx_render.h"

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
/* main_sdl.c's render mode: 640x448, as the game sets it up */
static struct {
    u32 viTVmode;
    u16 fbWidth, efbHeight, xfbHeight;
    u16 viXOrigin, viYOrigin, viWidth, viHeight;
    u32 xFBmode;
    u8 field_rendering, aa;
    u8 sample_pattern[12][2];
    u8 vfilter[7];
} s_renderMode = {
    0, 640, 448, 448, 40, 16, 640, 448, 1, 0, 0,
    { {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6},
      {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6} },
    { 0, 0, 21, 22, 21, 0, 0 },
};
void *rmode = &s_renderMode;

PortState g_port;

s32 Gfx_CreateView(s32 view, u32 width, u32 height);

void ForceIPLReset(void) {
    m_ilpreset_803C7620 = 1;
    m_reset = 1;
    g_port.quit = 1;
}

s32 Init_Display(void) {
    g_viewID = Gfx_CreateView(0, 640, 480);
    return g_viewID >= 0;
}

void Main_Shutdown_NGC(void) {
}

s32 RunThread(void *(*func)(void *)) {
    BOOL enabled;

    OSCreateThread(Thread, func, NULL, ThreadStack + sizeof(ThreadStack), sizeof(ThreadStack), 0x10, 1);
    enabled = OSDisableInterrupts();
    OSResumeThread(Thread);
    OSRestoreInterrupts(enabled);
    NGC_Thread_On = 1;
    return 1;
}

s8 isInThread(void) {
    return (s8)(OSGetCurrentThread() == (void *)Thread);
}

void EndThread(void) {
    s32 val;

    NGC_Thread_On = 0;
    OSExitThread(&val);
}

void TextureManager_ReleaseCache(void) {
}

void InitMem(void) {
    void *lo = OSGetArenaLo();
    void *hi = OSGetArenaHi();

    lo = OSInitAlloc(lo, hi, 1);
    lo = (void *)(((uintptr_t)lo + 0x1F) & ~(uintptr_t)0x1F);
    hi = (void *)((uintptr_t)hi & ~(uintptr_t)0x1F);
    OSSetCurrentHeap(OSCreateHeap(lo, hi));
    OSSetArenaLo(hi);
}

void InitRenderMode(void *mode) {
    (void)mode;
}

void InitVI(void) {
}

int Port_ResetSave(void) {
    return 0;
}

void GetSystemDate(s32 *year, s32 *month, s32 *day, s32 *hour, s32 *minute) {
    *year = 2026;
    *month = 1;
    *day = 1;
    *hour = 12;
    *minute = 0;
}

void Port_CreateWindow(SDL_WindowFlags flags) {
    (void)flags;
}

int Port_TryCreateWindow(SDL_WindowFlags flags) {
    (void)flags;
    return 1;
}
