/*
 * xdltest.c - checks the .xdl pack cache's file handling (src/port/xdl.c).
 * Links only xdl.c and fileio.c: no game, no window, no game data. The
 * packs are made up: a header and one segment whose first word points into
 * itself, recorded as DataGamePackLoad.c records a pack.
 *
 * Run (from the build folder; every file it makes is under OUT):
 *   ./xdltest [OUT]          OUT defaults to xdltest-out
 *
 * What it covers:
 *   - round trip: packs recorded, then read back and placed elsewhere, their
 *     pointer fixed up;
 *   - a pack written this session is found by the open file (reading) and
 *     not recorded again (reading and writing, and writing only);
 *   - appending: nothing before the old end of the file is rewritten but the
 *     header, so a write stopped before the header leaves the old file
 *     readable as it was;
 *   - a file cut short, or whose table points past its end: not read;
 *   - a fixup target or song bank reference out of range: that pack is
 *     damaged (loaded from its source), the others still read;
 *   - the cache file closed (dvd.c) while a pack read from it or recorded
 *     for it is still loading.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_internal.h"
#include "xdl.h"
#include "Port_Mix.h"

/* ---- what xdl.c needs from the rest of the port ------------------------- */

static _Alignas(64) u8 s_arena[1 << 20];
u8 *g_portArena = s_arena;
u32 g_portArenaSize = sizeof(s_arena);
static int s_verbose;

void *Port_HandleToPtrSlow(u32 h) { (void)h; return NULL; }
u32 Port_PtrToHandleSlow(const void *p) { (void)p; return 0; }
int Port_MixActive(void) { return 0; }
int Port_MixProtoReplace(void) { return 0; }
const char *Port_MixCacheName(void) { return "mix"; }
u32 Port_MixStamp(void) { return 0; }
char *Port_PrefPath(void) { return SDL_strdup(""); }

void Port_Log(const char *fmt, ...) {
    va_list ap;

    if (s_verbose) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }
}

void Port_Fatal(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    printf("FATAL: ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    exit(1);
}

/* ---- checks ------------------------------------------------------------------ */

static int s_fails, s_checks;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        s_checks++;                                                        \
        if (!(cond)) {                                                     \
            s_fails++;                                                     \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);         \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

static char s_dir[1024], s_source[1100], s_cache[1100];

#define PACK_SIZE 0x100
#define HEADER_SIZE 0x50

static u8 *Block(u32 at) {
    return s_arena + 0x1000 + at;
}

/* Record the pack at `srcOffset`: its header bytes and one segment
 * (PACK_SIZE bytes in the arena at Block(0)) whose word 0 points at byte
 * 0x20 of itself, word 1 at its end. */
static void Record(void *xf, u32 srcOffset) {
    u8 header[HEADER_SIZE];
    u8 *main = Block(0);
    u32 pointers[2] = { 0, 4 }, i;

    for (i = 0; i < HEADER_SIZE; i++) {
        header[i] = (u8)(srcOffset + i);
    }
    for (i = 0; i < PACK_SIZE; i++) {
        main[i] = (u8)(srcOffset * 3 + i);
    }
    *(u32 *)main = HND(main + 0x20);
    *(u32 *)(main + 4) = HND(main + PACK_SIZE);
    Xdl_BeginPack(xf, srcOffset, 4, srcOffset >> 8);
    Xdl_PackHeader(header, sizeof(header));
    Xdl_EndPack(NULL, main, PACK_SIZE, main, pointers, 2, NULL, 0);
}

/* The pack at `srcOffset` read back and placed at Block(0x400): 1 if it is
 * the one Record made. */
static int ReadBack(void *xf, u32 srcOffset) {
    XdlPack *xp = Xdl_FindPack(xf, srcOffset);
    u8 header[HEADER_SIZE], *at = Block(0x400);
    u32 i;
    int ok = 1;

    if (xp == NULL) {
        return 0;
    }
    Xdl_Header(xp, header, sizeof(header));
    for (i = 0; i < HEADER_SIZE; i++) {
        ok &= header[i] == (u8)(srcOffset + i);
    }
    ok &= Xdl_SegSize(xp, XDL_SEG_MAIN) == PACK_SIZE;
    memset(at, 0, PACK_SIZE);
    Xdl_ReadSeg(xp, XDL_SEG_MAIN, at, 0, PACK_SIZE);
    Xdl_PlaceSeg(xp, XDL_SEG_MAIN, at);
    ok &= *(u32 *)at == HND(at + 0x20) && *(u32 *)(at + 4) == HND(at + PACK_SIZE);
    for (i = 8; i < PACK_SIZE; i++) {
        ok &= at[i] == (u8)(srcOffset * 3 + i);
    }
    Xdl_FreePack(xp);
    return ok;
}

static u8 *Slurp(const char *path, size_t *size) {
    return SDL_LoadFile(path, size);
}

static void Spit(const char *path, const void *data, size_t size) {
    if (!SDL_SaveFile(path, data, size)) {
        Port_Fatal("can't write %s", path);
    }
}

static size_t FileSize(const char *path) {
    SDL_PathInfo info;

    return SDL_GetPathInfo(path, &info) ? (size_t)info.size : 0;
}

/* Fields of the file (xdl.c's structs). */
#define HDR_NUMSECTIONS 0x20
#define HDR_TABLE 0x24
#define SEC_OFFSET 8
#define SEC_SIZE 16
#define PI_SEG 0x90       /* XdlPackInfo.seg[] */
#define PI_BANKSEG (PI_SEG + XDL_MAX_SEGS * 16)

static u32 Rd(const u8 *d, size_t off) {
    u32 v;

    memcpy(&v, d + off, 4);
    return v;
}

static void Wr(u8 *d, size_t off, u32 v) {
    memcpy(d + off, &v, 4);
}

/* The file offset of section `i`'s table entry. */
static size_t SecEntry(const u8 *d, u32 i) {
    return Rd(d, HDR_TABLE) + (size_t)i * 0x20;
}

/* The PACK section's file offset for the pack at `srcOffset`. */
static size_t PackInfo(const u8 *d, u32 srcOffset) {
    u32 n = Rd(d, HDR_NUMSECTIONS), i;
    size_t toc = SecEntry(d, n - 1);
    u32 tocOff = Rd(d, toc + SEC_OFFSET), tocSize = Rd(d, toc + SEC_SIZE);

    for (i = 0; i < tocSize / 16; i++) {
        if (Rd(d, tocOff + i * 16 + 8) == srcOffset) {
            return Rd(d, SecEntry(d, Rd(d, tocOff + i * 16 + 12)) + SEC_OFFSET);
        }
    }
    Port_Fatal("no pack at 0x%x", srcOffset);
}

static void *OpenCache(int reading, int writing) {
    Xdl_Use(s_dir, reading, writing);
    return Xdl_OpenFile("/files/Test.xgc", PORT_DATA_GC, s_source);
}

int main(int argc, char **argv) {
    const char *out = "xdltest-out";
    char gcDir[1100];
    u8 *f1, *f2, *d;
    size_t n1, n2, n;
    void *xf;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            s_verbose = 1;
        } else {
            out = argv[i];
        }
    }
    SDL_CreateDirectory(out);
    snprintf(s_dir, sizeof(s_dir), "%s/cache", out);
    snprintf(gcDir, sizeof(gcDir), "%s/gc", s_dir);
    snprintf(s_source, sizeof(s_source), "%s/test.xgc", out);
    snprintf(s_cache, sizeof(s_cache), "%s/test.xdl", gcDir);
    SDL_CreateDirectory(s_dir);
    SDL_RemovePath(s_cache);
    Spit(s_source, "source", 6);

    /* ---- round trip; a pack written this session is found, not recorded again */
    xf = OpenCache(1, 1);
    CHECK(xf != NULL, "a cache to write");
    Record(xf, 0x100);
    n1 = FileSize(s_cache);
    CHECK(n1 > 0, "the first pack written");
    CHECK(ReadBack(xf, 0x100), "the pack written this session read back from the open file");
    Record(xf, 0x100);
    CHECK(FileSize(s_cache) == n1, "not written again");
    f1 = Slurp(s_cache, &n1);
    Record(xf, 0x200);
    Xdl_CloseFile(xf);
    f2 = Slurp(s_cache, &n2);
    CHECK(f1 != NULL && f2 != NULL && n2 > n1, "the second pack appended");

    /* ---- appending rewrites nothing before the old end but the header */
    CHECK(memcmp(f1 + 0x40, f2 + 0x40, n1 - 0x40) == 0, "the old file's bytes are kept");
    xf = OpenCache(1, 0);
    CHECK(xf != NULL && ReadBack(xf, 0x100) && ReadBack(xf, 0x200), "both packs read");
    CHECK(Xdl_FindPack(xf, 0x300) == NULL, "no pack at 0x300");
    Xdl_CloseFile(xf);

    /* a write stopped before its header: the old header on the new file */
    d = SDL_malloc(n2);
    memcpy(d, f2, n2);
    memcpy(d, f1, 0x40);
    Spit(s_cache, d, n2);
    xf = OpenCache(1, 0);
    CHECK(xf != NULL && ReadBack(xf, 0x100), "the old file read as it was");
    CHECK(xf == NULL || Xdl_FindPack(xf, 0x200) == NULL, "without the pack whose header wasn't written");
    Xdl_CloseFile(xf);

    /* writing only: what the file holds isn't written again */
    Spit(s_cache, f2, n2);
    xf = OpenCache(0, 1);
    Record(xf, 0x100);
    Record(xf, 0x200);
    Xdl_CloseFile(xf);
    SDL_free(d);
    d = Slurp(s_cache, &n);
    CHECK(n == n2 && memcmp(d, f2, n2) == 0, "unchanged");
    SDL_free(d);

    /* ---- cut short, or a table pointing past the end: not read */
    Spit(s_cache, f2, n2 - 100);
    CHECK(OpenCache(1, 0) == NULL, "a file cut short");
    Spit(s_cache, f2, n2 - 0x20);
    CHECK(OpenCache(1, 0) == NULL, "a file cut short, its table");
    d = SDL_malloc(n2);
    memcpy(d, f2, n2);
    Wr(d, SecEntry(d, 0) + SEC_SIZE, Rd(d, SecEntry(d, 0) + SEC_SIZE) + (u32)n2);
    Spit(s_cache, d, n2);
    CHECK(OpenCache(1, 0) == NULL, "a section past the end");
    memcpy(d, f2, n2);
    Wr(d, SecEntry(d, 0) + SEC_OFFSET, (u32)n2 - 8);
    Spit(s_cache, d, n2);
    CHECK(OpenCache(1, 0) == NULL, "a section starting near the end");
    memcpy(d, f2, n2);
    Wr(d, HDR_TABLE, (u32)n2);
    Spit(s_cache, d, n2);
    CHECK(OpenCache(1, 0) == NULL, "the table past the end");

    /* ---- fixups and the song bank checked: that pack damaged, the other read */
    {
        static const struct {
            const char *what;
            u32 field, value;   /* field: ~0u: the segment's first word */
        } bad[] = {
            { "a fixup target past its segment", ~0u, PACK_SIZE + 1 },
            { "a fixup target far out", ~0u, 0x7FFFFFF0u },
            { "a song bank segment out of range", PI_BANKSEG, 40 },
            { "a negative song bank segment", PI_BANKSEG, (u32)-2 },
            { "a song bank past its segment", PI_BANKSEG + 4, PACK_SIZE + 1 },
        };
        u32 k;

        memcpy(d, f2, n2);
        Spit(s_cache, d, n2);
        xf = OpenCache(1, 0);
        CHECK(xf != NULL && ReadBack(xf, 0x100), "unchanged, read");
        Xdl_CloseFile(xf);
        for (k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
            size_t pi;

            memcpy(d, f2, n2);
            pi = PackInfo(d, 0x100);
            if (bad[k].field == ~0u) {
                u32 sec = Rd(d, pi + PI_SEG + XDL_SEG_MAIN * 16 + 8);

                Wr(d, Rd(d, SecEntry(d, sec) + SEC_OFFSET), bad[k].value);
            } else {
                if (bad[k].field == PI_BANKSEG + 4) {
                    Wr(d, pi + PI_BANKSEG, XDL_SEG_MAIN);
                }
                Wr(d, pi + bad[k].field, bad[k].value);
            }
            Spit(s_cache, d, n2);
            xf = OpenCache(1, 0);
            CHECK(xf != NULL && Xdl_FindPack(xf, 0x100) == NULL, "%s: damaged", bad[k].what);
            CHECK(xf != NULL && ReadBack(xf, 0x200), "%s: the other pack read", bad[k].what);
            Xdl_CloseFile(xf);
        }
        /* a song bank in range is fine */
        memcpy(d, f2, n2);
        Wr(d, PackInfo(d, 0x100) + PI_BANKSEG, XDL_SEG_MAIN);
        Wr(d, PackInfo(d, 0x100) + PI_BANKSEG + 4, 0x40);
        Spit(s_cache, d, n2);
        xf = OpenCache(1, 0);
        CHECK(xf != NULL && ReadBack(xf, 0x100), "a song bank in range");
        Xdl_CloseFile(xf);
    }
    SDL_free(d);

    /* ---- the file closed while a pack read from it or recorded for it loads */
    Spit(s_cache, f1, n1);
    xf = OpenCache(1, 1);
    {
        XdlPack *xp = Xdl_FindPack(xf, 0x100);
        u8 *at = Block(0x400);

        CHECK(xp != NULL, "found");
        if (xp != NULL) {
            Xdl_CloseFile(xf);   /* as DVDClose does */
            Xdl_ReadSeg(xp, XDL_SEG_MAIN, at, 0, PACK_SIZE);
            Xdl_PlaceSeg(xp, XDL_SEG_MAIN, at);
            CHECK(*(u32 *)at == HND(at + 0x20), "read after the file was closed");
            Xdl_FreePack(xp);
        }
    }
    xf = OpenCache(1, 1);
    {
        u8 *main = Block(0);
        u32 pointers[1] = { 0 };

        Xdl_BeginPack(xf, 0x200, 4, 2);
        Xdl_CloseFile(xf);
        memset(main, 0x55, PACK_SIZE);
        *(u32 *)main = HND(main + 0x20);
        Xdl_PackHeader(main, HEADER_SIZE);
        Xdl_EndPack(NULL, main, PACK_SIZE, main, pointers, 1, NULL, 0);
    }
    xf = OpenCache(1, 0);
    CHECK(xf != NULL && ReadBack(xf, 0x100) && Xdl_FindPack(xf, 0x200) != NULL,
          "recorded after the file was closed");
    Xdl_CloseFile(xf);

    SDL_free(f1);
    SDL_free(f2);
    printf("xdltest: %d checks, %d failed\n", s_checks, s_fails);
    return s_fails != 0;
}
