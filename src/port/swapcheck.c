/* Checker for the data pack byte-swapping (src/game/Port_Swap.c).
 *
 * Configure with -DHWVX_SWAPCHECK=ON: the game sources are then compiled with
 * GCC's kernel-address instrumentation in outline mode, which calls the
 * __asan_load*_noabort hooks below for every memory read. A read that falls
 * in a loaded pack blob is compared with how its bytes were swapped: a u16
 * read of bytes that were swapped as a 32-bit word, a byte read inside a
 * swapped word, a word read of raw bytes, ... Each such read is reported once
 * per code address, with the pack, the blob offset and the address to look up:
 *
 *   addr2line -e build/hwvx -f -i 0x...
 *
 * The fix is a claim for that field in the walker for its type. Normal builds
 * only keep no claim maps. */
#include "port_internal.h"
#include "swapcheck.h"

typedef struct Blob {
    u8 *base;
    u32 size;
    u8 *claim;
    char pack[32];
    s32 type;
} Blob;

#define MAX_BLOBS 64
static Blob s_blobs[MAX_BLOBS];
static int s_numBlobs;

#ifdef HWVX_SWAPCHECK
static const int s_enabled = 1;
#else
static const int s_enabled = 0;
#endif

void PortSwapCheck_Register(u8 *base, u32 size, u8 *claim, const char *pack, s32 packType) {
    Blob *b;
    int i;

    if (!s_enabled) {
        free(claim);
        return;
    }
    PortSwapCheck_Forget(base, size);
    if (s_numBlobs == MAX_BLOBS) {
        free(s_blobs[0].claim);
        memmove(&s_blobs[0], &s_blobs[1], sizeof(Blob) * (MAX_BLOBS - 1));
        s_numBlobs--;
    }
    b = &s_blobs[s_numBlobs++];
    b->base = base;
    b->size = size;
    b->claim = claim;
    b->type = packType;
    for (i = 0; pack != NULL && pack[i] && i < 31; i++) {
        b->pack[i] = pack[i];
    }
    b->pack[i] = 0;
}

void PortSwapCheck_Forget(const void *p, u32 size) {
    const u8 *lo = p, *hi = lo + size;
    int i;

    for (i = 0; i < s_numBlobs;) {
        if (s_blobs[i].base < hi && s_blobs[i].base + s_blobs[i].size > lo) {
            free(s_blobs[i].claim);
            s_blobs[i] = s_blobs[--s_numBlobs];
        } else {
            i++;
        }
    }
}

#ifdef HWVX_SWAPCHECK

#define MAX_REPORTS 4096
static uintptr_t s_reported[MAX_REPORTS];
static int s_numReported;

static void Report(const Blob *b, u32 off, int size, u8 claim, uintptr_t pc) {
    static const char *names[] = { "word-swapped", "pointer", "u32", "u16", "raw bytes" };
    int i;

    for (i = 0; i < s_numReported; i++) {
        if (s_reported[i] == (pc ^ (uintptr_t)size)) {
            return;
        }
    }
    if (s_numReported < MAX_REPORTS) {
        s_reported[s_numReported++] = pc ^ (uintptr_t)size;
    }
    Port_Log("swapcheck: %d-byte read of %s data: pack %s (type %d) offset 0x%X, pc %p",
             size, names[claim < 5 ? claim : 0], b->pack, b->type, off, (void *)pc);
}

static void Check(uintptr_t addr, int size, uintptr_t pc) {
    int i;

    if (addr - (uintptr_t)g_portArena >= g_portArenaSize) {
        return;
    }
    for (i = 0; i < s_numBlobs; i++) {
        const Blob *b = &s_blobs[i];
        u32 off = (u32)(addr - (uintptr_t)b->base);
        u8 c;
        int ok;

        if (addr < (uintptr_t)b->base || off >= b->size) {
            continue;
        }
        c = b->claim[off];
        switch (size) {
        case 1: ok = c == CLAIM_BYTES; break;
        case 2: ok = c == CLAIM_U16 && (off & 1) == 0; break;
        case 4: ok = (c == CLAIM_DEFAULT || c == CLAIM_U32 || c == CLAIM_DONE) && (off & 3) == 0; break;
        default: ok = 0; break;
        }
        if (!ok) {
            Report(b, off, size, c, pc);
        }
        return;
    }
}

#define HOOK(n) \
    void __asan_load##n##_noabort(uintptr_t addr); \
    void __asan_load##n##_noabort(uintptr_t addr) { Check(addr, n, (uintptr_t)__builtin_return_address(0)); } \
    void __asan_store##n##_noabort(uintptr_t addr); \
    void __asan_store##n##_noabort(uintptr_t addr) { (void)addr; }
HOOK(1)
HOOK(2)
HOOK(4)
HOOK(8)
HOOK(16)
void __asan_loadN_noabort(uintptr_t addr, size_t size);
void __asan_loadN_noabort(uintptr_t addr, size_t size) { (void)addr; (void)size; }
void __asan_storeN_noabort(uintptr_t addr, size_t size);
void __asan_storeN_noabort(uintptr_t addr, size_t size) { (void)addr; (void)size; }
void __asan_handle_no_return(void);
void __asan_handle_no_return(void) {}

#endif
