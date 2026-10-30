/* Dolphin OS replacement: arena + heap, time, interrupts, threads, messages.
 *
 * Threading model: GameCube threads are cooperative-by-priority on one CPU, and
 * the game relies on that (no locking around shared state). Here each OS
 * thread is an SDL thread, but all game code runs under one "giant lock"; a
 * thread only gives it up at the points where a GameCube thread would block
 * (waiting for vsync, DVD, a message, or exiting). */
#include "port_internal.h"

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
} Heap;

#define MAX_HEAPS 4
static Heap s_heaps[MAX_HEAPS];
static int s_numHeaps;
volatile int __OSCurrHeap = -1;
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
        SDL_UnlockMutex(s_heapLock);
        Port_Log("OSAllocFromHeap: out of memory (%u bytes, %zu/%zu in use)", size, heap->inUse,
                 heap->size);
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
    SDL_UnlockMutex(s_heapLock);
    return (u8 *)c + HEAP_HDR;
}

void OSFreeToHeap(int heapId, void *ptr) {
    Heap *heap = &s_heaps[heapId];
    HeapCell *c = (HeapCell *)((u8 *)ptr - HEAP_HDR);
    HeapCell *f;

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

/* ---- init / report ----------------------------------------------------- */

static SDL_Mutex *s_giant;
static SDL_ThreadID s_mainThreadId;

void OSInit(void) {
    /* Leave the first 64 KB of the arena unused so no allocation gets a handle
     * that looks like a small integer. */
    s_arenaLo = g_portArena + 0x10000;
    s_arenaHi = g_portArena + g_portArenaSize;
    s_giant = SDL_CreateMutex();
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

/* ---- threads ----------------------------------------------------------- */

typedef struct PortThread {
    SDL_Thread *thread;
    SDL_ThreadID id;
    OSThreadFunc func;
    void *param;
    SDL_Semaphore *resume;
    volatile int exited;
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
void Port_GiantLock(void) { SDL_LockMutex(s_giant); }

/* Give other game threads a chance to run (a GameCube thread would block). */
void Port_Yield(void) {
    SDL_UnlockMutex(s_giant);
    SDL_DelayNS(0);
    SDL_LockMutex(s_giant);
}

static int SDLCALL ThreadEntry(void *data) {
    PortThread *t = data;
    SDL_WaitSemaphore(t->resume);
    SDL_LockMutex(s_giant);
    t->id = SDL_GetCurrentThreadID();
    t->func(t->param);
    t->exited = 1;
    SDL_UnlockMutex(s_giant);
    return 0;
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
    t->resume = SDL_CreateSemaphore(0);
    /* Game code may recurse deeply while loading; don't use its 20 KB stack. */
    {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)ThreadEntry);
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "game");
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, t);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 1 << 20);
        t->thread = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (t->thread == NULL) {
        Port_Fatal("SDL_CreateThread: %s", SDL_GetError());
    }
    SDL_DetachThread(t->thread);
    return TRUE;
}

s32 OSResumeThread(void *thread) {
    PortThread *t = FindThread(thread, 0);
    if (t != NULL && t->resume != NULL) {
        SDL_SignalSemaphore(t->resume);
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
        if (s_threads[i].key != NULL && s_threads[i].t.id == SDL_GetCurrentThreadID()) {
            s_threads[i].t.exited = 1;
            s_threads[i].key = NULL;
        }
    }
    SDL_UnlockMutex(s_giant);
    /* SDL threads can't be killed from inside; park this one forever. A game
     * thread never returns from OSExitThread on the GameCube either. */
    for (;;) {
        SDL_Delay(1000000);
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
