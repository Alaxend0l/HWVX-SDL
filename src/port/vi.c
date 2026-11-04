/* VI (video interface): frame presentation, pacing and the SDL event pump. */
#include "port_internal.h"

static void (*s_postRetrace)(u32);
static u32 s_retraceCount;
static u64 s_nextFrameNS;
static BOOL s_black;

/* One video retrace's length at the frame rate in use. The game runs one logic
 * frame per retrace, so this is also the game's rate: the Graphics menu's Max
 * FPS choice moves it, and gfx_settings.c keeps it inside the rates the game's
 * own per-frame timers follow. */
static u64 RetraceNS(void) {
    int rate = Port_GfxFrameRate();

    return 1000000000ull / (u64)(rate > 0 ? rate : 60);
}

void Port_PumpEvents(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            g_port.quit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
                g_port.fullscreen = !g_port.fullscreen;
                SDL_SetWindowFullscreen(g_port.window, g_port.fullscreen);
            }
            Pad_PortHandleEvent(&ev);
            break;
        default:
            Pad_PortHandleEvent(&ev);
            break;
        }
    }
    if (g_port.quit) {
        /* The game has no exit path of its own on the GameCube. */
        exit(0);
    }
}

void VIInit(void) {
    s_nextFrameNS = SDL_GetTicksNS() + RetraceNS();
}

void VIConfigure(const struct GXRenderModeObj *rm) { (void)rm; }
void VIFlush(void) {}
void VISetNextFrameBuffer(void *fb) { (void)fb; }
void VISetBlack(BOOL black) { s_black = black; }
u32 VIGetNextField(void) { return s_retraceCount & 1; }
u32 VIGetTvFormat(void) { return 0; /* NTSC */ }

void *VISetPostRetraceCallback(void (*callback)(u32)) {
    void *old = (void *)s_postRetrace;
    s_postRetrace = callback;
    return old;
}

/* Benchmarking: HWVX_NO_VIWAIT=1 removes the 60 Hz wait, so the frame time in
 * the HWVX_DUMP log is what drawing costs instead of the pacing idle. The game
 * runs fast-forward while it is on. */
static int NoRetraceWait(void) {
    static int off = -1;
    const char *e;

    if (off < 0) {
        e = SDL_getenv("HWVX_NO_VIWAIT");
        off = e != NULL && *e == '1';
    }
    return off;
}

/* Frame pacing split: how much of the time between retraces was the 60 Hz wait
 * and how much was work. Both are summed over the same retraces, so the two
 * subtract exactly. Only HWVX_FPSLOG's window reads and clears them: a paced
 * frame always takes 16.7 ms, so just the work part says anything about what
 * drawing costs. */
static u64 s_idleNS, s_spanNS, s_lastRetraceNS;

/* Called by the game after every GXCopyDisp (and in loading loops). One call
 * == one retrace at the rate in use (RetraceNS above). Other game threads run
 * while we wait, exactly as they would have while the GameCube CPU idled in
 * VIWaitForRetrace. Whether the frame then waits for the display to take it is
 * the renderer's business, not this one's: see Port_VSyncWanted. */
void VIWaitForRetrace(void) {
    u64 now, period;

    now = SDL_GetTicksNS();
    if (s_lastRetraceNS != 0)
        s_spanNS += now - s_lastRetraceNS;
    s_lastRetraceNS = now;

    GX_PortPresent();
    Port_PumpEvents();

    Port_GiantUnlock();
    now = SDL_GetTicksNS();
    period = RetraceNS();
    if (now < s_nextFrameNS && !NoRetraceWait()) {
        u64 wait = s_nextFrameNS - now;

        SDL_DelayPrecise(wait);
        s_idleNS += wait;
        s_nextFrameNS += period;
    } else {
        /* Running behind; don't try to catch up with a burst of frames. */
        s_nextFrameNS = now + period;
    }
    Port_GiantLock();
    Port_InterruptPoint();

    s_retraceCount++;
    {   /* HWVX_FPSLOG=1: log the real frame rate and the game's frame delta */
        static int on = -1;
        static u64 t0;
        static u32 frames, msSum;
        extern s32 g_iDeltaTimeMS;
        if (on < 0) {
            on = SDL_getenv("HWVX_FPSLOG") != NULL;
            t0 = SDL_GetTicksNS();
        }
        if (on) {
            u64 t = SDL_GetTicksNS();
            frames++;
            msSum += g_iDeltaTimeMS;
            if (t - t0 >= 5000000000ull) {
                /* The wait is pacing idle; the rest of the span is the port and
                 * the game working, which is what a drawing change can move. */
                Port_Log("FPS %.2f (game delta avg %.2f ms, %.2f ms/frame of work in %d frames)",
                         frames * 1e9 / (double)(t - t0), (double)msSum / frames,
                         frames > 0 ? (double)(s_spanNS - s_idleNS) / (double)frames / 1e6 : 0.0,
                         frames);
                t0 = t;
                frames = msSum = 0;
                s_spanNS = s_idleNS = 0;
            }
        }
    }
    Pad_PortSample();
    if (s_postRetrace != NULL) {
        s_postRetrace(s_retraceCount);
    }
    GX_PortBeginFrame();
}
