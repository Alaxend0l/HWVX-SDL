/* VI (video interface): frame presentation, pacing and the SDL event pump. */
#include "port_internal.h"

void Port_GiantLock(void);
void Port_GiantUnlock(void);

static void (*s_postRetrace)(u32);
static u32 s_retraceCount;
static u64 s_nextFrameNS;
static BOOL s_black;

#define FRAME_NS (1000000000ull / 60)

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
    s_nextFrameNS = SDL_GetTicksNS() + FRAME_NS;
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

/* Called by the game after every GXCopyDisp (and in loading loops). One call
 * == one 60 Hz field. Other game threads run while we wait, exactly as they
 * would have while the GameCube CPU idled in VIWaitForRetrace. */
void VIWaitForRetrace(void) {
    u64 now;

    GX_PortPresent();
    Port_PumpEvents();

    Port_GiantUnlock();
    now = SDL_GetTicksNS();
    if (now < s_nextFrameNS) {
        SDL_DelayPrecise(s_nextFrameNS - now);
        s_nextFrameNS += FRAME_NS;
    } else {
        /* Running behind; don't try to catch up with a burst of frames. */
        s_nextFrameNS = now + FRAME_NS;
    }
    Port_GiantLock();

    s_retraceCount++;
    if (s_postRetrace != NULL) {
        s_postRetrace(s_retraceCount);
    }
    GX_PortBeginFrame();
}
