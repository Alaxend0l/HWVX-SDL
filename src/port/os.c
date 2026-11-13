/* Dolphin OS replacement: arena + heap, time, interrupts, threads, messages.
 *
 * Threading model: GameCube threads are cooperative-by-priority on one CPU, and
 * the game relies on that (no locking around shared state). Here each OS
 * thread is an SDL thread, but all game code runs under one "giant lock"; a
 * thread only gives it up at the points where a GameCube thread would block
 * (waiting for vsync, DVD, a message, or exiting). */
#include "port_internal.h"
#include "swapcheck.h"
#include <setjmp.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <stdio.h>
#include <unistd.h>
#endif

/* ---- arena ------------------------------------------------------------- */

static u8 *s_arenaLo;
static u8 *s_arenaHi;

void *OSGetArenaLo(void) { return s_arenaLo; }
void *OSGetArenaHi(void) { return s_arenaHi; }
void OSSetArenaLo(void *addr) { s_arenaLo = addr; }
void OSSetArenaHi(void *addr) { s_arenaHi = addr; }

/* ---- heap ---------------------------------------------------------------
 * First-fit free list with 32-byte granularity, like OSAlloc. Block headers
 * are host structures but live inside the arena. */

typedef struct HeapCell {
    struct HeapCell *prev;
    struct HeapCell *next;
    size_t size; /* including header */
    size_t used;
} HeapCell;

#define HEAP_ALIGN 32
#define HEAP_HDR ((sizeof(HeapCell) + HEAP_ALIGN - 1) & ~(size_t)(HEAP_ALIGN - 1))

typedef struct Heap {
    HeapCell *free;
    HeapCell *allocated;
    size_t size;
    size_t inUse;
    size_t peak;            /* the most inUse has been (HWVX_HEAPLOG, Port_HeapPeak) */
} Heap;

#define MAX_HEAPS 4
static Heap s_heaps[MAX_HEAPS];
static int s_numHeaps;
int __OSCurrHeap = -1;
static SDL_Mutex *s_heapLock;

void *OSInitAlloc(void *arenaStart, void *arenaEnd, int maxHeaps) {
    (void)arenaEnd;
    (void)maxHeaps;
    if (s_heapLock == NULL) {
        s_heapLock = SDL_CreateMutex();
    }
    s_numHeaps = 0;
    return arenaStart;
}

int OSCreateHeap(void *start, void *end) {
    Heap *heap;
    HeapCell *cell;
    uintptr_t lo = ((uintptr_t)start + HEAP_ALIGN - 1) & ~(uintptr_t)(HEAP_ALIGN - 1);
    uintptr_t hi = (uintptr_t)end & ~(uintptr_t)(HEAP_ALIGN - 1);

    if (s_numHeaps >= MAX_HEAPS || hi <= lo + HEAP_HDR) {
        return -1;
    }
    heap = &s_heaps[s_numHeaps];
    cell = (HeapCell *)lo;
    cell->prev = cell->next = NULL;
    cell->size = hi - lo;
    cell->used = 0;
    heap->free = cell;
    heap->allocated = NULL;
    heap->size = hi - lo;
    heap->inUse = 0;
    heap->peak = 0;
    return s_numHeaps++;
}

int OSSetCurrentHeap(int heap) {
    int old = __OSCurrHeap;
    __OSCurrHeap = heap;
    return old;
}

static void CellUnlink(HeapCell **list, HeapCell *c) {
    if (c->prev) {
        c->prev->next = c->next;
    } else {
        *list = c->next;
    }
    if (c->next) {
        c->next->prev = c->prev;
    }
}

static void CellPush(HeapCell **list, HeapCell *c) {
    c->prev = NULL;
    c->next = *list;
    if (*list) {
        (*list)->prev = c;
    }
    *list = c;
}

void *OSAllocFromHeap(int heapId, u32 size) {
    Heap *heap;
    HeapCell *c;
    size_t need;

    if (heapId < 0 || heapId >= s_numHeaps) {
        Port_Fatal("OSAllocFromHeap: bad heap %d", heapId);
    }
    heap = &s_heaps[heapId];
    need = (HEAP_HDR + size + HEAP_ALIGN - 1) & ~(size_t)(HEAP_ALIGN - 1);

    SDL_LockMutex(s_heapLock);
    for (c = heap->free; c != NULL; c = c->next) {
        if (c->size >= need) {
            break;
        }
    }
    if (c == NULL) {
        size_t largest = 0;

        for (c = heap->free; c != NULL; c = c->next) {
            largest = c->size > largest ? c->size : largest;
        }
        SDL_UnlockMutex(s_heapLock);
        Port_Log("OSAllocFromHeap: heap %d out of memory: %u bytes asked, %.1f of %.1f MB in use, the largest free "
                 "block %.1f MB (peak %.1f MB; HWVX_ARENA_MB sets the game heap's size)", heapId, size,
                 heap->inUse / 1048576.0, heap->size / 1048576.0, largest / 1048576.0, heap->peak / 1048576.0);
        return NULL;
    }
    CellUnlink(&heap->free, c);
    if (c->size - need >= HEAP_HDR + HEAP_ALIGN) {
        HeapCell *rest = (HeapCell *)((u8 *)c + need);
        rest->size = c->size - need;
        rest->used = 0;
        CellPush(&heap->free, rest);
        c->size = need;
    }
    c->used = size;
    CellPush(&heap->allocated, c);
    heap->inUse += c->size;
    if (heap->inUse > heap->peak) {
        static int logPeak = -1;
        size_t step = heap->peak >> 20;

        heap->peak = heap->inUse;
        if (logPeak < 0) {
            logPeak = SDL_getenv("HWVX_HEAPLOG") != NULL;
        }
        if (logPeak && (heap->peak >> 20) != step) {   /* each MB the peak passes */
            Port_Log("heap %d: peak %.1f of %.1f MB", heapId, heap->peak / 1048576.0, heap->size / 1048576.0);
        }
    }
    SDL_UnlockMutex(s_heapLock);
    return (u8 *)c + HEAP_HDR;
}

void OSFreeToHeap(int heapId, void *ptr) {
    Heap *heap = &s_heaps[heapId];
    HeapCell *c = (HeapCell *)((u8 *)ptr - HEAP_HDR);
    HeapCell *f;

    PortSwapCheck_Forget(ptr, c->size - HEAP_HDR);
    /* The GX display list cache may hold a decode of these bytes; the block can
     * now be handed out with different contents. */
    Port_InvalidateDisplayLists(ptr, c->size - HEAP_HDR);
    SDL_LockMutex(s_heapLock);
    CellUnlink(&heap->allocated, c);
    heap->inUse -= c->size;
    /* Coalesce with physically adjacent free cells. */
    for (f = heap->free; f != NULL;) {
        HeapCell *next = f->next;
        if ((u8 *)f + f->size == (u8 *)c) {
            CellUnlink(&heap->free, f);
            f->size += c->size;
            c = f;
            next = heap->free;
        } else if ((u8 *)c + c->size == (u8 *)f) {
            CellUnlink(&heap->free, f);
            c->size += f->size;
            next = heap->free;
        }
        f = next;
    }
    c->used = 0;
    CellPush(&heap->free, c);
    SDL_UnlockMutex(s_heapLock);
}

/* The draw distance's heap (src/game/Port_DrawDist.c): the arena's last
 * PORT_EXTRA_HEAP bytes, past the game heap, made the first time it is asked
 * for. The game heap is the same size and its blocks the same either way. */
int Port_ExtraHeap(void) {
    static int id = -1;

    if (id < 0) {
        id = OSCreateHeap(g_portArena + g_portArenaSize - PORT_EXTRA_HEAP, g_portArena + g_portArenaSize);
    }
    return id;
}

u32 Port_GameHeapMB(void) {
    return __OSCurrHeap >= 0 && __OSCurrHeap < s_numHeaps ? (u32)((s_heaps[__OSCurrHeap].size + (1 << 19)) >> 20) : 0;
}

size_t Port_HeapPeak(int heapId) {
    size_t peak = 0;

    if (heapId >= 0 && heapId < s_numHeaps) {
        SDL_LockMutex(s_heapLock);
        peak = s_heaps[heapId].peak;
        SDL_UnlockMutex(s_heapLock);
    }
    return peak;
}

void Port_HeapUsage(int heapId, u32 *inUse, u32 *size) {
    *inUse = 0;
    *size = 0;
    if (heapId >= 0 && heapId < s_numHeaps) {
        SDL_LockMutex(s_heapLock);
        *inUse = (u32)s_heaps[heapId].inUse;
        *size = (u32)s_heaps[heapId].size;
        SDL_UnlockMutex(s_heapLock);
    }
}

/* ---- init / report ----------------------------------------------------- */

static SDL_Mutex *s_giant;
static SDL_Condition *s_interruptDone;
static SDL_AtomicInt s_interruptPending;
static SDL_Condition *s_windowClosed;
static int s_interruptWindow;  /* see Port_InterruptWindowBegin */
static SDL_ThreadID s_mainThreadId;
/* Resumed game threads, counted under s_handoff, woken on s_gameDone. Their own
 * mutex keeps its own; the main thread waits on it without holding the giant
 * lock, which is how it gives that lock up. See Port_GiantLock. */
static SDL_Mutex *s_handoff;
static SDL_Condition *s_gameDone;
static int s_gameRunning;

/* HWVX_THREADTRACE=1: report each loader handoff, so the frame rate a load ran
 * at and the wait it caused can be read off the log. */
static int s_threadTrace = -1;

static int ThreadTraceOn(void) {
    if (s_threadTrace < 0) {
        const char *e = SDL_getenv("HWVX_THREADTRACE");
        s_threadTrace = e != NULL && *e == '1';
    }
    return s_threadTrace;
}

void OSInit(void) {
    /* Leave the first 64 KB of the arena unused so no allocation gets a handle
     * that looks like a small integer. */
    s_arenaLo = g_portArena + 0x10000;
    s_arenaHi = g_portArena + g_portArenaSize - PORT_EXTRA_HEAP;   /* the rest: Port_ExtraHeap */
    s_giant = SDL_CreateMutex();
    s_interruptDone = SDL_CreateCondition();
    s_windowClosed = SDL_CreateCondition();
    s_handoff = SDL_CreateMutex();
    s_gameDone = SDL_CreateCondition();
    SDL_LockMutex(s_giant);
    s_mainThreadId = SDL_GetCurrentThreadID();
}

void OSReport(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

void OSPanic(const char *file, int line, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    Port_Fatal("OSPanic %s:%d: %s", file, line, msg);
}

/* ---- time ---------------------------------------------------------------
 * GameCube timebase: 40.5 MHz (bus clock / 4). */

OSTime OSGetTime(void) {
    return (OSTime)(SDL_GetTicksNS() * (OS_TIMER_CLOCK / 1000) / 1000000);
}

OSTick OSGetTick(void) {
    return (OSTick)OSGetTime();
}

typedef struct OSCalendarTime {
    s32 sec, min, hour, mday, mon, year, wday, yday, msec, usec;
} OSCalendarTime;

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime *td) {
    SDL_Time now;
    SDL_DateTime dt;

    (void)ticks; /* the game only asks for "now" */
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &dt, true);
    td->sec = dt.second;
    td->min = dt.minute;
    td->hour = dt.hour;
    td->mday = dt.day;
    td->mon = dt.month - 1;
    td->year = dt.year;
    td->wday = dt.day_of_week;
    td->yday = SDL_GetDayOfYear(dt.year, dt.month, dt.day);
    td->msec = dt.nanosecond / 1000000;
    td->usec = (dt.nanosecond / 1000) % 1000;
}

/* ---- interrupts: covered by the giant lock ----------------------------- */

static BOOL s_intrEnabled = TRUE;

BOOL OSDisableInterrupts(void) {
    BOOL old = s_intrEnabled;
    s_intrEnabled = FALSE;
    return old;
}

BOOL OSEnableInterrupts(void) {
    BOOL old = s_intrEnabled;
    s_intrEnabled = TRUE;
    return old;
}

BOOL OSRestoreInterrupts(BOOL level) {
    BOOL old = s_intrEnabled;
    s_intrEnabled = level;
    return old;
}

/* ---- threads -----------------------------------------------------------
 * Handing the giant lock over is the port's decision, never the host
 * scheduler's: SDL's mutex isn't fair, so a main thread that runs straight
 * from one frame into the next (what happens once a frame's work fills the
 * budget, as it does at 120 FPS) takes the lock back before the loader it has
 * just resumed can get it. The load then lands a frame late, or not at all,
 * which is why the car appears in a menu only sometimes. Port_GiantLock closes
 * that hole: the main thread waits for game threads it resumed. */

typedef struct PortThread {
    SDL_Thread *thread;
    SDL_ThreadID id;
    OSThreadFunc func;
    void *param;
    SDL_Semaphore *resume;
    volatile int exited;
    volatile int running;   /* resumed, and not finished yet */
    jmp_buf exitJump;       /* OSExitThread unwinds back to the worker loop */
} PortThread;

/* The game passes a 0x310-byte OSThread buffer; we keep our state beside it. */
#define MAX_THREADS 8
static struct {
    void *key;
    PortThread t;
} s_threads[MAX_THREADS];

static PortThread *FindThread(void *key, int create) {
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (s_threads[i].key == key) {
            return &s_threads[i].t;
        }
    }
    if (!create) {
        return NULL;
    }
    for (i = 0; i < MAX_THREADS; i++) {
        if (s_threads[i].key == NULL) {
            s_threads[i].key = key;
            SDL_zero(s_threads[i].t);
            return &s_threads[i].t;
        }
    }
    Port_Fatal("too many OS threads");
}

void Port_GiantUnlock(void) { SDL_UnlockMutex(s_giant); }

static void GameThreadStopped(PortThread *t) {
    SDL_LockMutex(s_handoff);
    if (t->running) {
        t->running = 0;
        s_gameRunning--;
        SDL_BroadcastCondition(s_gameDone);
    }
    SDL_UnlockMutex(s_handoff);
}

/* Game threads take the lock here: never while another game thread is inside
 * an interrupt window. */
void Port_GiantLock(void) {
    if (SDL_GetCurrentThreadID() == s_mainThreadId && s_gameRunning) {
        /* The main thread never takes the lock back ahead of a game thread it
         * resumed, so a load always runs, in the frame that started it, at any
         * frame rate. It usually costs nothing: while the frame has idle time
         * left the loader finishes inside it and the count is already zero.
         * Reading the count without the lock is safe: only the main thread
         * raises it, so it can only be read too high, and the loop below
         * re-checks it under s_handoff. */
        int blocked = 0;
        u64 t0 = SDL_GetTicksNS();
        u64 warned = t0;

        SDL_LockMutex(s_handoff);
        while (s_gameRunning > 0) {
            blocked = 1;
            SDL_WaitConditionTimeout(s_gameDone, s_handoff, 100);
            if (s_gameRunning > 0 && SDL_GetTicksNS() - warned > SDL_MS_TO_NS(3000)) {
                warned = SDL_GetTicksNS();
                Port_Log("%d game thread(s) still loading after 3 s", s_gameRunning);
            }
        }
        SDL_UnlockMutex(s_handoff);
        if (blocked && ThreadTraceOn()) {
            u64 waited = SDL_GetTicksNS() - t0;
            Port_Log("threadtrace: frame held for %d.%03d ms waiting on a load",
                     (int)(waited / 1000000), (int)(waited % 1000000) / 1000);
        }
    }
    SDL_LockMutex(s_giant);
    while (s_interruptWindow) {
        SDL_WaitCondition(s_windowClosed, s_giant);
    }
}

/* Interrupt handlers (the AX thread) take the giant lock through these. On the
 * GameCube they preempt the game; here a game thread hands the lock over at
 * Port_InterruptPoint. A plain unlock/lock there isn't enough: the mutex isn't
 * fair, so a game thread that is running behind would take it straight back
 * and starve the handler. */
void Port_InterruptLock(void) {
    SDL_AddAtomicInt(&s_interruptPending, 1);
    SDL_LockMutex(s_giant);
    SDL_AddAtomicInt(&s_interruptPending, -1);
}

void Port_InterruptUnlock(void) {
    SDL_BroadcastCondition(s_interruptDone);
    SDL_UnlockMutex(s_giant);
}

/* Called with the giant lock held: let a waiting interrupt handler run first. */
void Port_InterruptPoint(void) {
    while (SDL_GetAtomicInt(&s_interruptPending) > 0) {
        SDL_WaitConditionTimeout(s_interruptDone, s_giant, 10);
    }
}

/* Around a blocking host call that touches no game state (waiting for vsync):
 * interrupt handlers may take the giant lock meanwhile, other game threads
 * may not, so to them the call still looks atomic. */
void Port_InterruptWindowBegin(void) {
    s_interruptWindow = 1;
    SDL_UnlockMutex(s_giant);
}

void Port_InterruptWindowEnd(void) {
    SDL_LockMutex(s_giant);
    s_interruptWindow = 0;
    SDL_BroadcastCondition(s_windowClosed);
}

/* Give other game threads a chance to run (a GameCube thread would block). */
void Port_Yield(void) {
    SDL_UnlockMutex(s_giant);
    SDL_DelayNS(0);
    Port_GiantLock();
    Port_InterruptPoint();
}

/* ---- worker loop --------------------------------------------------------
 * One host thread per slot, live for the whole run: the game calls
 * OSCreateThread and OSExitThread again for every load, and a fresh thread
 * (1 MB of stack) and semaphore each time leaves a parked thread behind per
 * load. OSExitThread can't be answered by returning, because the game never
 * comes back from it, so it unwinds to this loop and the thread waits to be
 * resumed again. */
static int s_loadCount;
static int s_hostThreads;

static int SDLCALL ThreadEntry(void *data) {
    PortThread *t = data;
    volatile int rc;

    for (;;) {
        volatile u64 start;

        SDL_WaitSemaphore(t->resume);   /* idle: holds no locks */
        Port_GiantLock();
        t->id = SDL_GetCurrentThreadID();
        start = SDL_GetTicksNS();
        rc = setjmp(t->exitJump);
        if (rc == 0) {
            t->func(t->param);
        }
        t->exited = 1;
        if (ThreadTraceOn()) {
            u64 run = SDL_GetTicksNS() - start;
            s_loadCount++;
            Port_Log("threadtrace: load %d took %d.%03d ms on the giant lock "
                     "(host threads: %d)", s_loadCount,
                     (int)(run / 1000000), (int)(run % 1000000) / 1000, s_hostThreads);
        }
        GameThreadStopped(t);
        SDL_UnlockMutex(s_giant);
    }
    return 0; /* never reached: the loop above only ends by longjmp back into it */
}

BOOL OSCreateThread(void *thread, OSThreadFunc func, void *param, void *stack, u32 stackSize,
                    s32 priority, u16 attr) {
    PortThread *t = FindThread(thread, 1);
    (void)stack;
    (void)stackSize;
    (void)priority;
    (void)attr;
    t->func = func;
    t->param = param;
    t->exited = 0;
    /* Game code may recurse deeply while loading; don't use its 20 KB stack. */
    if (t->resume == NULL) {
        t->resume = SDL_CreateSemaphore(0);
    }
    if (t->thread == NULL) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)ThreadEntry);
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "game");
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, t);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 1 << 20);
        t->thread = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
        if (t->thread == NULL) {
            Port_Fatal("SDL_CreateThread: %s", SDL_GetError());
        }
        SDL_DetachThread(t->thread);
        s_hostThreads++;
    }
    return TRUE;
}

s32 OSResumeThread(void *thread) {
    PortThread *t = FindThread(thread, 0);
    if (t != NULL && t->resume != NULL) {
        SDL_LockMutex(s_handoff);
        if (!t->running) {   /* resuming a thread that is already running
                              * would queue a second run of the same load */
            t->running = 1;
            s_gameRunning++;
            SDL_SignalSemaphore(t->resume);
        }
        SDL_UnlockMutex(s_handoff);
    }
    return 0;
}

s32 OSSuspendThread(void *thread) {
    (void)thread;
    PORT_TODO("OSSuspendThread");
    return 0;
}

void OSCancelThread(void *thread) {
    (void)thread;
    PORT_TODO("OSCancelThread");
}

void OSExitThread(void *val) {
    int i;
    (void)val;
    for (i = 0; i < MAX_THREADS; i++) {
        PortThread *t = &s_threads[i].t;
        if (s_threads[i].key != NULL && t->id == SDL_GetCurrentThreadID()) {
            /* Never returns, as on the GameCube: unwind to this thread's worker
             * loop, which is what keeps it alive for the next load. The slot
             * keeps its key so the game's next OSCreateThread finds this same
             * worker; id 0 means "not running" to OSGetCurrentThread. */
            t->exited = 1;
            t->id = 0;
            longjmp(t->exitJump, 1);
        }
    }
}

void *OSGetCurrentThread(void) {
    int i;
    SDL_ThreadID id = SDL_GetCurrentThreadID();
    if (id == s_mainThreadId) {
        return NULL;
    }
    for (i = 0; i < MAX_THREADS; i++) {
        if (s_threads[i].key != NULL && s_threads[i].t.id == id) {
            return s_threads[i].key;
        }
    }
    return NULL;
}

/* ---- message queues ---------------------------------------------------- */

typedef struct PortMsgQueue {
    OSMessage *msgs;
    s32 count;
    s32 first;
    s32 used;
} PortMsgQueue;

void OSInitMessageQueue(OSMessageQueue *mq, OSMessage *msgArray, s32 msgCount) {
    PortMsgQueue *q = (PortMsgQueue *)mq;
    SDL_COMPILE_TIME_ASSERT(mq_size, sizeof(PortMsgQueue) <= sizeof(OSMessageQueue));
    q->msgs = msgArray;
    q->count = msgCount;
    q->first = 0;
    q->used = 0;
}

BOOL OSSendMessage(OSMessageQueue *mq, OSMessage msg, s32 flags) {
    PortMsgQueue *q = (PortMsgQueue *)mq;
    while (q->used >= q->count) {
        if (!(flags & 1)) {
            return FALSE;
        }
        Port_Yield();
    }
    q->msgs[(q->first + q->used) % q->count] = msg;
    q->used++;
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue *mq, OSMessage *msg, s32 flags) {
    PortMsgQueue *q = (PortMsgQueue *)mq;
    while (q->used == 0) {
        if (!(flags & 1)) {
            return FALSE;
        }
        Port_Yield();
    }
    if (msg != NULL) {
        *msg = q->msgs[q->first];
    }
    q->first = (q->first + 1) % q->count;
    q->used--;
    return TRUE;
}

/* ---- misc -------------------------------------------------------------- */

void OSInitAlarm(void) {}
u32 OSGetResetCode(void) { return 0; }

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) {
    (void)reset;
    (void)resetCode;
    (void)forceMenu;
    g_port.quit = 1;
}

BOOL OSGetResetButtonState(void) { return FALSE; }

static u32 s_soundMode = 1; /* stereo */
u32 OSGetSoundMode(void) { return s_soundMode; }
void OSSetSoundMode(u32 mode) { s_soundMode = mode; }

void DCFlushRange(void *addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCInvalidateRange(void *addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCStoreRange(void *addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCZeroRange(void *addr, u32 nBytes) { memset(addr, 0, nBytes); }
void LCEnable(void) {}
void LCDisable(void) {}
void LCQueueWait(u32 len) { (void)len; }
void EXIInit(void) {}
void GBAConnectivityInit(void) {}
void GBAConnectivityLoop(void) {}

void Port_ProcessMemory(u64 *committed, u64 *resident) {
    *committed = *resident = 0;
#if defined(_WIN32)
    {
        PROCESS_MEMORY_COUNTERS_EX pmc;

        if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc))) {
            *committed = pmc.PrivateUsage;
            *resident = pmc.WorkingSetSize;
        }
    }
#elif defined(__APPLE__)
    {
        task_vm_info_data_t vm;
        mach_msg_type_number_t n = TASK_VM_INFO_COUNT;

        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &n) == KERN_SUCCESS) {
            *committed = vm.phys_footprint;
            *resident = vm.resident_size;
        }
    }
#elif defined(__linux__)
    {
        FILE *f = fopen("/proc/self/statm", "r");
        unsigned long size, rss;

        if (f != NULL) {
            if (fscanf(f, "%lu %lu", &size, &rss) == 2) {
                *committed = *resident = (u64)rss * (u64)sysconf(_SC_PAGESIZE);
            }
            fclose(f);
        }
    }
#endif
}

/* ---- the log, redirected (Windows) --------------------------------------- */

#if defined(_WIN32)
static HANDLE s_logPipe;
static SDL_LogOutputFunction s_logDefault;
static void *s_logDefaultData;

static void SDLCALL LogToPipe(void *data, int category, SDL_LogPriority priority, const char *message) {
    DWORD n;

    (void)data;
    WriteFile(s_logPipe, message, (DWORD)SDL_strlen(message), &n, NULL);
    WriteFile(s_logPipe, "\r\n", 2, &n, NULL);
    s_logDefault(s_logDefaultData, category, priority, message);
}
#endif

void Port_LogToRedirectedStderr(void) {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD type = h != NULL && h != INVALID_HANDLE_VALUE ? GetFileType(h) : FILE_TYPE_UNKNOWN;

    /* a console build (HWVX_CONSOLE) has its console: SDL writes there */
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
        DWORD mode;

        if (!GetConsoleMode(h, &mode) && GetConsoleWindow() == NULL) {
            s_logPipe = h;
            SDL_GetLogOutputFunction(&s_logDefault, &s_logDefaultData);
            SDL_SetLogOutputFunction(LogToPipe, NULL);
        }
    }
#endif
}
