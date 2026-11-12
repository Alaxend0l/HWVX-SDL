/* Viewer mode: the preview's frames, shared with the data browser through
 * shared memory, so the browser shows them in its own window (the game's
 * window is hidden; viewer.h). The protocol's `frames` command turns it on
 * at a size; the layout is docs/viewer-protocol.md's "Embedded frames":
 *
 *   header (4096 bytes): magic "HWVF", version, key, sizes, and per slot a
 *   sequence number (odd while it is written), frame, width, height, stride;
 *   then 3 slots of `capacity` bytes: RGBX pixels, top row first.
 *
 * Each frame's EFB is read back asynchronously (the renderer's grabStart /
 * grabFetch: the copy of frame N is fetched while frame N+1 is drawn, so the
 * game never waits for the GPU) and copied into the slot after the newest.
 * A frame is only grabbed while the browser keeps up: at most one published
 * frame it hasn't taken (header `consumed`), which also keeps the slot it is
 * reading from being overwritten.
 *
 * The shared memory is POSIX shm_open (name "/hwvx-...", mode 0600) or a
 * Windows named mapping ("Local\hwvx-..."), behind ViewerShm below. The
 * browser opens it by the name the `frames` reply gives, checks the key, and
 * unlinks the name (so it doesn't outlive a crash); the game unlinks it too
 * when it replaces or frees it. It is reallocated only when a size needs more
 * than its capacity. */
#include "port_internal.h"
#include "gx_render.h"
#include "viewer_internal.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

#define FRAMES_MAGIC 0x46565748u    /* "HWVF" */
#define FRAMES_VERSION 1
#define FRAMES_HEADER 4096u
#define FRAMES_SLOTS 3
#define FRAMES_MAX_W 3840
#define FRAMES_MAX_H 2160

/* The header, little-endian as the hosts are. Written by the game except
 * `consumed`, which the browser writes. */
typedef struct FrameSlot {
    u32 seq;            /* even: complete; odd: being written */
    u32 frame;          /* the viewer's frame number */
    u32 width, height, stride;
    u32 published;      /* the header's `published` when it was published */
    u32 reserved[2];
} FrameSlot;

typedef struct FrameHeader {
    u32 magic, version;
    u64 key;            /* the `frames` reply's: the browser checks it */
    u32 headerSize;     /* where slot 0's pixels start */
    u32 slots;
    u32 capacity;       /* bytes a slot */
    u32 published;      /* frames published so far */
    u32 latest;         /* the newest one's slot */
    u32 consumed;       /* the browser: the `published` count it last took */
    u32 format;         /* 1: RGBX8 (the 4th byte is not alpha) */
    u32 reserved[5];
    FrameSlot slot[FRAMES_SLOTS];
} FrameHeader;

_Static_assert(sizeof(FrameHeader) == 64 + FRAMES_SLOTS * 32, "FrameHeader layout");

/* ---- a tiny portable shared memory wrapper --------------------------------- */

/* (ViewerShm is in viewer_internal.h: the media blobs use it too, viewer_media.c) */
int ViewerShm_Create(ViewerShm *m, size_t size, const char *tag) {
    SDL_zerop(m);
#ifdef _WIN32
    {
        wchar_t wname[64];
        int i;

        SDL_snprintf(m->name, sizeof(m->name), "Local\\hwvx-%s", tag);
        for (i = 0; m->name[i] != '\0' && i < 63; i++) {
            wname[i] = (wchar_t)m->name[i];
        }
        wname[i] = 0;
        m->h = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (DWORD)((u64)size >> 32),
                                  (DWORD)(size & 0xFFFFFFFFu), wname);
        if (m->h == NULL) {
            return 0;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(m->h);
            m->h = NULL;
            return 0;
        }
        m->base = MapViewOfFile(m->h, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (m->base == NULL) {
            CloseHandle(m->h);
            m->h = NULL;
            return 0;
        }
    }
#else
    {
        int fd;
        void *p;

        SDL_snprintf(m->name, sizeof(m->name), "/hwvx-%s", tag);   /* macOS: at most 31 characters */
        fd = shm_open(m->name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            return 0;
        }
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            shm_unlink(m->name);
            return 0;
        }
        p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) {
            shm_unlink(m->name);
            return 0;
        }
        m->base = p;
    }
#endif
    m->size = size;
    return 1;
}

void ViewerShm_Destroy(ViewerShm *m) {
    if (m->base == NULL) {
        return;
    }
#ifdef _WIN32
    UnmapViewOfFile(m->base);
    CloseHandle(m->h);
#else
    munmap(m->base, m->size);
    shm_unlink(m->name);    /* (the browser may have done it already) */
#endif
    SDL_zerop(m);
}

/* ---- the ring --------------------------------------------------------------- */

static struct {
    int on;
    int w, h;               /* asked for: the EFB's size */
    ViewerShm shm;
    u64 key;
    u32 gen;
    u32 capacity;
    /* the renderer's readbacks in flight, oldest first */
    int pending[GXR_GRAB_SLOTS];
    u32 pendingFrame[GXR_GRAB_SLOTS];
    int pendingW[GXR_GRAB_SLOTS], pendingH[GXR_GRAB_SLOTS];
    u32 pendingFrameOut;
    int numPending;
    /* measuring */
    u64 workNs, workFrames;     /* time in ViewerFrames_EndFrame */
    u64 pubNs, pubCount;        /* of it, copying finished readbacks into the shared memory */
    u64 windowStart;
    u32 windowPublished, lastPublished;
    f32 fps, ms, msCopy;
} f;

static FrameHeader *Header(void) {
    return (FrameHeader *)f.shm.base;
}

u64 Viewer_RandomU64(void) {
    u64 x = 0;
    int got = 0;
#ifndef _WIN32
    SDL_IOStream *io = SDL_IOFromFile("/dev/urandom", "rb");

    if (io != NULL) {
        got = SDL_ReadIO(io, &x, sizeof(x)) == sizeof(x);
        SDL_CloseIO(io);
    }
#endif
    if (!got) {
        static u64 mix = 0x9E3779B97F4A7C15ull;

        x = SDL_GetPerformanceCounter() ^ SDL_GetTicksNS() ^ ((u64)(uintptr_t)&x << 21) ^ (mix += 0x9E3779B97F4A7C15ull);
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDull;
        x ^= x >> 33;
    }
    return x;
}

int ViewerFrames_On(void) {
    return f.on;
}

int Port_ViewerEfbSize(int *w, int *h) {
    if (!f.on) {
        return 0;
    }
    *w = f.w;
    *h = f.h;
    return 1;
}

/* Frees the shared memory and forgets the readbacks in flight (their slots
 * are fetched and dropped so the renderer can reuse them). */
static void Release(void) {
    int i, w, h;

    for (i = 0; i < f.numPending; i++) {
        g_gxr->grabFetch(f.pending[i], NULL, 0, 0, 0, &w, &h, 1);   /* (too big: dropped) */
    }
    f.numPending = 0;
    ViewerShm_Destroy(&f.shm);
}

void ViewerFrames_Stop(void) {
    Release();
    f.on = 0;
}

int ViewerFrames_Configure(int w, int h, char *err, size_t errSize) {
    u32 need;

    if (w < 16 || h < 16 || w > FRAMES_MAX_W || h > FRAMES_MAX_H) {
        SDL_snprintf(err, errSize, "frames takes width and height, 16x16 .. %dx%d", FRAMES_MAX_W, FRAMES_MAX_H);
        return 0;
    }
    if (g_gxr->grabStart == NULL) {
        SDL_snprintf(err, errSize, "the %s renderer can't read frames back", g_gxr->name);
        return 0;
    }
    need = (u32)w * (u32)h * 4;
    if (f.shm.base == NULL || need > f.capacity) {
        /* a new one (a new name): grows by a quarter to spare reallocating on every drag */
        u32 cap = need + need / 4;
        char tag[40];

        if (cap > (u32)FRAMES_MAX_W * FRAMES_MAX_H * 4) {
            cap = (u32)FRAMES_MAX_W * FRAMES_MAX_H * 4;
        }
        Release();
        f.key = Viewer_RandomU64();
        SDL_snprintf(tag, sizeof(tag), "%016llx-%u", (unsigned long long)f.key, f.gen++);
        if (!ViewerShm_Create(&f.shm, FRAMES_HEADER + (size_t)FRAMES_SLOTS * cap, tag)) {
            SDL_snprintf(err, errSize, "shared memory of %u MB could not be made", (FRAMES_HEADER + FRAMES_SLOTS * cap) >> 20);
            f.on = 0;
            return 0;
        }
        f.capacity = cap;
        SDL_memset(f.shm.base, 0, FRAMES_HEADER);
        Header()->magic = FRAMES_MAGIC;
        Header()->version = FRAMES_VERSION;
        Header()->key = f.key;
        Header()->headerSize = FRAMES_HEADER;
        Header()->slots = FRAMES_SLOTS;
        Header()->capacity = cap;
        Header()->format = 1;
        f.lastPublished = f.windowPublished = 0;
    }
    f.w = w;
    f.h = h;
    f.on = 1;
    return 1;
}

void ViewerFrames_WriteInfo(JsonWriter *w) {
    Jw_Bool(w, "on", f.on);
    if (!f.on) {
        return;
    }
    Jw_String(w, "name", f.shm.name);
    {
        char key[24];

        SDL_snprintf(key, sizeof(key), "%016llx", (unsigned long long)f.key);
        Jw_String(w, "key", key);
    }
    Jw_Int(w, "size", (long long)f.shm.size);
    Jw_Int(w, "slots", FRAMES_SLOTS);
    Jw_Int(w, "capacity", f.capacity);
    Jw_Int(w, "headerSize", FRAMES_HEADER);
    Jw_Int(w, "width", f.w);
    Jw_Int(w, "height", f.h);
}

void ViewerFrames_WriteStats(JsonWriter *w) {
    Jw_BeginObject(w, "frames", 1);
    Jw_Bool(w, "on", f.on);
    if (f.on) {
        Jw_Int(w, "width", f.w);
        Jw_Int(w, "height", f.h);
        Jw_Int(w, "published", Header()->published);
        Jw_Float(w, "fps", f.fps);
        Jw_Float(w, "ms", f.ms);
        Jw_Float(w, "msCopy", f.msCopy);
    }
    Jw_EndObject(w);
}

/* The oldest readback in flight into the slot after the newest, if the GPU
 * has finished it (wait: it waits for it). 1: published; 0: not ready. */
static int Publish(int wait) {
    FrameHeader *hd = Header();
    u32 next = hd->published == 0 ? 0 : (hd->latest + 1) % FRAMES_SLOTS;
    FrameSlot *sl = &hd->slot[next];
    u8 *dst = f.shm.base + FRAMES_HEADER + (size_t)next * f.capacity;
    int grab = f.pending[0], w = 0, h = 0, r;
    u32 seq = sl->seq & ~1u;

    if ((u64)f.pendingW[0] * (u64)f.pendingH[0] * 4 > f.capacity) {
        r = g_gxr->grabFetch(grab, NULL, 0, 0, 0, &w, &h, 1);   /* from before a smaller buffer: dropped */
    } else {
        sl->seq = seq + 1;          /* odd: being written */
        SDL_MemoryBarrierRelease();
        r = g_gxr->grabFetch(grab, dst, f.pendingW[0] * 4, f.pendingW[0], f.pendingH[0], &w, &h, wait);
        if (r == 0) {
            sl->seq = seq;          /* not ready: nothing was written */
            return 0;
        }
    }
    f.numPending--;
    SDL_memmove(&f.pending[0], &f.pending[1], (size_t)f.numPending * sizeof(f.pending[0]));
    SDL_memmove(&f.pendingFrame[0], &f.pendingFrame[1], (size_t)f.numPending * sizeof(f.pendingFrame[0]));
    SDL_memmove(&f.pendingW[0], &f.pendingW[1], (size_t)f.numPending * sizeof(f.pendingW[0]));
    SDL_memmove(&f.pendingH[0], &f.pendingH[1], (size_t)f.numPending * sizeof(f.pendingH[0]));
    if (r != 1) {
        sl->seq = seq;
        return 1;                   /* failed: nothing published, the slot is free again */
    }
    sl->frame = f.pendingFrameOut;
    sl->width = (u32)w;
    sl->height = (u32)h;
    sl->stride = (u32)w * 4;
    sl->published = hd->published + 1;
    SDL_MemoryBarrierRelease();
    sl->seq = seq + 2;              /* even: complete */
    hd->latest = next;
    SDL_MemoryBarrierRelease();
    hd->published++;
    return 1;
}

/* After the frame's draws (Gfx_EndScene), before the display copy: publishes
 * the readbacks the GPU has finished, and starts this frame's, unless the
 * browser is behind. */
void ViewerFrames_EndFrame(u32 frame) {
    FrameHeader *hd;
    u64 t0, now;
    u32 consumed;

    if (!f.on || f.shm.base == NULL) {
        return;
    }
    t0 = SDL_GetTicksNS();
    hd = Header();
    while (f.numPending > 0) {
        u64 p0 = SDL_GetTicksNS();
        u32 before = hd->published;

        f.pendingFrameOut = f.pendingFrame[0];
        /* with every readback slot busy, the oldest is waited for */
        if (!Publish(f.numPending >= GXR_GRAB_SLOTS)) {
            break;
        }
        if (hd->published != before) {
            f.pubNs += SDL_GetTicksNS() - p0;
            f.pubCount++;
        }
    }
    SDL_MemoryBarrierAcquire();
    consumed = hd->consumed;
    /* at most one published frame the browser hasn't taken, counting those in flight */
    if (f.numPending < GXR_GRAB_SLOTS && hd->published + (u32)f.numPending - consumed <= 1) {
        int slot, k, used;

        for (slot = 0; slot < GXR_GRAB_SLOTS; slot++) {
            for (k = 0, used = 0; k < f.numPending; k++) {
                used |= f.pending[k] == slot;
            }
            if (!used) {
                break;
            }
        }
        if (slot < GXR_GRAB_SLOTS && g_gxr->grabStart(slot)) {
            GX_DrawEfbSize(&f.pendingW[f.numPending], &f.pendingH[f.numPending]);
            f.pending[f.numPending] = slot;
            f.pendingFrame[f.numPending] = frame;
            f.numPending++;
        }
    }
    now = SDL_GetTicksNS();
    f.workNs += now - t0;
    f.workFrames++;
    if (f.windowStart == 0) {
        f.windowStart = now;
        f.windowPublished = hd->published;
    } else if (now - f.windowStart >= 1000000000ull) {
        f.fps = (f32)((f64)(hd->published - f.windowPublished) * 1e9 / (f64)(now - f.windowStart));
        f.ms = f.workFrames != 0 ? (f32)((f64)f.workNs / 1e6 / (f64)f.workFrames) : 0.0f;
        f.msCopy = f.pubCount != 0 ? (f32)((f64)f.pubNs / 1e6 / (f64)f.pubCount) : 0.0f;
        if (SDL_getenv("HWVX_VIEWER_STATS") != NULL) {
            Port_Log("viewer: frames %dx%d, %.1f published/s, %.3f ms/frame reading back (%.3f ms a frame copied)", f.w,
                     f.h, f.fps, f.ms, f.msCopy);
        }
        f.pubNs = 0;
        f.pubCount = 0;
        f.windowStart = now;
        f.windowPublished = hd->published;
        f.workNs = 0;
        f.workFrames = 0;
    }
}

void ViewerFrames_Shutdown(void) {
    ViewerFrames_Stop();
}
