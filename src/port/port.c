/* Core runtime: game arena, 32-bit handle registry, logging. */
#include "port.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __has_include(<execinfo.h>) && !defined(__ANDROID__)   /* glibc, macOS (Android: API 33+ only) */
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#endif

u8 *g_portArena;
u32 g_portArenaSize;

/* ---- handle registry for pointers outside the arena -------------------- */

/* Open-addressing map pointer -> index plus a dense index -> pointer table.
 * Entries are never removed: statics, functions and long-lived host objects
 * are what end up here, so the set stays small. */
static const void **s_handlePtrs;
static u32 s_handleCount;
static u32 s_handleCap;
static u32 *s_hashSlots; /* index + 1, 0 = empty */
static u32 s_hashCap;
static SDL_Mutex *s_handleLock;

static u32 HashPtr(const void *p, u32 cap) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    return (u32)v & (cap - 1);
}

static void HashInsert(u32 index) {
    u32 i = HashPtr(s_handlePtrs[index], s_hashCap);
    while (s_hashSlots[i] != 0) {
        i = (i + 1) & (s_hashCap - 1);
    }
    s_hashSlots[i] = index + 1;
}

u32 Port_PtrToHandleSlow(const void *p) {
    u32 i;
    u32 h;

    SDL_LockMutex(s_handleLock);
    if (s_hashCap != 0) {
        for (i = HashPtr(p, s_hashCap); s_hashSlots[i] != 0; i = (i + 1) & (s_hashCap - 1)) {
            if (s_handlePtrs[s_hashSlots[i] - 1] == p) {
                h = PORT_STATIC_HANDLE_BASE + (s_hashSlots[i] - 1) * PORT_STATIC_HANDLE_STRIDE;
                SDL_UnlockMutex(s_handleLock);
                return h;
            }
        }
    }
    if (s_handleCount >= (PORT_ARENA_HANDLE_BASE - PORT_STATIC_HANDLE_BASE) / PORT_STATIC_HANDLE_STRIDE) {
        Port_Fatal("handle registry full (%u entries); a stack or temporary pointer is "
                   "probably being stored in a 32-bit slot", s_handleCount);
    }
    if (s_handleCount == s_handleCap) {
        s_handleCap = s_handleCap ? s_handleCap * 2 : 1024;
        s_handlePtrs = realloc(s_handlePtrs, s_handleCap * sizeof(*s_handlePtrs));
    }
    s_handlePtrs[s_handleCount] = p;
    if ((s_handleCount + 1) * 2 > s_hashCap) {
        s_hashCap = s_hashCap ? s_hashCap * 2 : 4096;
        free(s_hashSlots);
        s_hashSlots = calloc(s_hashCap, sizeof(*s_hashSlots));
        for (i = 0; i < s_handleCount; i++) {
            HashInsert(i);
        }
    }
    HashInsert(s_handleCount);
    h = PORT_STATIC_HANDLE_BASE + s_handleCount * PORT_STATIC_HANDLE_STRIDE;
    s_handleCount++;
    SDL_UnlockMutex(s_handleLock);
    return h;
}

void *Port_HandleToPtrSlow(u32 h) {
    u32 index;
    const void *p;

    index = (h - PORT_STATIC_HANDLE_BASE) / PORT_STATIC_HANDLE_STRIDE;
    if (h < PORT_STATIC_HANDLE_BASE || index >= s_handleCount) {
        Port_Fatal("bad handle 0x%08X", h);
    }
    SDL_LockMutex(s_handleLock);
    p = s_handlePtrs[index];
    SDL_UnlockMutex(s_handleLock);
    return (u8 *)p + ((h - PORT_STATIC_HANDLE_BASE) % PORT_STATIC_HANDLE_STRIDE);
}

void Port_InitHandles(u32 arenaSize) {
    s_handleLock = SDL_CreateMutex();
    /* 64-byte alignment: the game aligns to 32 and uses the handle value for
     * the arithmetic, so the base must be at least as aligned. */
    g_portArena = SDL_aligned_alloc(64, arenaSize);
    if (g_portArena == NULL) {
        Port_Fatal("could not allocate %u MB game arena", arenaSize >> 20);
    }
    memset(g_portArena, 0, arenaSize);
    g_portArenaSize = arenaSize;
}

/* ---- byte swapping ----------------------------------------------------- */

void Port_SwapArray16(void *p, size_t count) {
    u16 *w = (u16 *)p;
    size_t i;
    for (i = 0; i < count; i++) {
        w[i] = BE16(w[i]);
    }
}

void Port_SwapArray32(void *p, size_t count) {
    u32 *w = (u32 *)p;
    size_t i;
    for (i = 0; i < count; i++) {
        w[i] = BE32(w[i]);
    }
}

/* ---- logging ----------------------------------------------------------- */

void Port_Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

int Port_AllCheats(void) {
    /* HWVX_CHEATS=1: every cheat reads as on, without touching save data. */
    static int all = -1;
    if (all < 0) {
        all = SDL_getenv("HWVX_CHEATS") != NULL;
    }
    return all;
}

void Port_Fatal(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", buf);
#ifdef HAVE_BACKTRACE
    {
        /* resolve with: addr2line -f -s -i -e build/hwvx <addresses> */
        void *frames[24];
        int n = backtrace(frames, 24), k;
        for (k = 1; k < n; k++) {
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "  at %p", frames[k]);
        }
    }
#endif
    if (SDL_getenv("HWVX_NO_DIALOG") == NULL) {  /* scripted/headless runs: log only */
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Hot Wheels: Velocity X", buf, NULL);
    }
    abort();
}

void Port_Todo(const char *func, const char *what) {
    /* Log each unimplemented feature once. */
    static const char *seen[256];
    static int nseen;
    int i;
    for (i = 0; i < nseen; i++) {
        if (seen[i] == what) {
            return;
        }
    }
    if (nseen < 256) {
        seen[nseen++] = what;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TODO %s: %s", func, what);
}
