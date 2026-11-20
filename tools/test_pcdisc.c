/*
 * test_pcdisc.c - the .cue's track file found without case (pcdisc.c CueTrack).
 *
 * The retail PC .cue names "IMS-HWVX.BIN"; a copy on a case-sensitive file
 * system may be ims-hwvx.bin (as it is on the disc rip this was found with).
 * Windows and macOS find it either way; the import must too. No game data: the
 * track here is a few zero sectors, so an import that found it stops at "not
 * an ISO 9660 disc image", one that didn't at "could not open the disc image".
 * With the real discs (HWVX_TEST_PC_DISC/_DATA, HWVX_TEST_PS2_DISC/_DATA) the
 * launcher's import is compared with the ripper's folder, file by file.
 */
#include <stdio.h>
#include <string.h>

#include "port_internal.h"

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    s_fails += !ok;
}

static void Put(const char *path, const void *data, size_t n) {
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");

    if (io != NULL) {
        SDL_WriteIO(io, data, n);
        SDL_CloseIO(io);
    }
}

static const char *Import(const char *cue) {
    static char err[256];
    SDL_AtomicInt progress, cancel;

    SDL_SetAtomicInt(&progress, 0);
    SDL_SetAtomicInt(&cancel, 0);
    err[0] = '\0';
    if (Port_ImportPCDisc(cue, "test_pcdisc-out/dest", &progress, &cancel, err, sizeof(err))) {
        return "imported";
    }
    return err;
}


/* ---- with the real discs (skipped without) ------------------------------
 * HWVX_TEST_PC_DISC (.cue/.iso) and HWVX_TEST_PC_DATA (the ripper's folder,
 * `xdlrs disc extract`), HWVX_TEST_PS2_DISC and HWVX_TEST_PS2_DATA: the
 * launcher's import of the disc must hold the same files, byte for byte, as
 * the ripper's folder (the editor's ripper and the game agree on the layout). */

typedef struct Tree {
    const char *a, *b;     /* roots */
    int files, diffs;
} Tree;

static int SameFile(const char *a, const char *b) {
    size_t na, nb;
    void *da = SDL_LoadFile(a, &na), *db = SDL_LoadFile(b, &nb);
    int same = da != NULL && db != NULL && na == nb && SDL_memcmp(da, db, na) == 0;

    SDL_free(da);
    SDL_free(db);
    return same;
}

static void CompareDir(Tree *t, const char *rel);

static SDL_EnumerationResult SDLCALL CompareEntry(void *userdata, const char *dir, const char *name) {
    Tree *t = (Tree *)userdata;
    char rel[1200], a[1400], b[1400];
    const char *sub = dir + SDL_strlen(t->a);
    SDL_PathInfo ia, ib;

    while (*sub == '/') {
        sub++;
    }
    SDL_snprintf(rel, sizeof(rel), "%s%s%s", sub, *sub && sub[SDL_strlen(sub) - 1] != '/' ? "/" : "", name);
    SDL_snprintf(a, sizeof(a), "%s/%s", t->a, rel);
    SDL_snprintf(b, sizeof(b), "%s/%s", t->b, rel);
    if (!SDL_GetPathInfo(a, &ia) || !SDL_GetPathInfo(b, &ib) || ia.type != ib.type) {
        printf("  only in %s: %s\n", t->a, rel);
        t->diffs++;
    } else if (ia.type == SDL_PATHTYPE_DIRECTORY) {
        CompareDir(t, rel);
    } else {
        t->files++;
        if (!SameFile(a, b)) {
            printf("  differs: %s\n", rel);
            t->diffs++;
        }
    }
    return SDL_ENUM_CONTINUE;
}

static void CompareDir(Tree *t, const char *rel) {
    char path[1400];

    SDL_snprintf(path, sizeof(path), "%s/%s", t->a, rel);
    SDL_EnumerateDirectory(path, CompareEntry, t);
}

/* `a` and `b` hold the same files; the count of files in a. */
static int SameTree(const char *a, const char *b, int *files) {
    Tree ab = { a, b, 0, 0 }, ba = { b, a, 0, 0 };

    CompareDir(&ab, "");
    CompareDir(&ba, "");
    *files = ab.files;
    return ab.diffs == 0 && ba.diffs == 0 && ab.files > 0;
}

static void RealDisc(const char *what, const char *discVar, const char *dataVar, int ps2) {
    const char *disc = SDL_getenv(discVar), *data = SDL_getenv(dataVar);
    char err[256] = "", dest[1200], msg[300];
    SDL_AtomicInt progress, cancel;
    int ok, files = 0;
    Uint64 t0;

    if (disc == NULL || data == NULL) {
        printf("skip: %s (set %s and %s)\n", what, discVar, dataVar);
        return;
    }
    SDL_SetAtomicInt(&progress, 0);
    SDL_SetAtomicInt(&cancel, 0);
    t0 = SDL_GetTicks();
    if (ps2) {
        ok = Port_ImportPS2Disc(disc, "test_pcdisc-out/real-", dest, sizeof(dest), &progress, &cancel, err, sizeof(err));
    } else {
        SDL_strlcpy(dest, "test_pcdisc-out/real-pc", sizeof(dest));
        ok = Port_ImportPCDisc(disc, dest, &progress, &cancel, err, sizeof(err));
    }
    SDL_snprintf(msg, sizeof(msg), "%s: the launcher imports it (%s; %u ms)", what, ok ? "ok" : err,
                 (unsigned)(SDL_GetTicks() - t0));
    Check(ok, msg);
    if (ok) {
        ok = SameTree(dest, data, &files);
        SDL_snprintf(msg, sizeof(msg), "%s: the import and %s hold the same %d files", what, data, files);
        Check(ok, msg);
    }
    Port_RemoveTree(dest);
}

int main(void) {
    static char zeros[2048 * 20];
    const char *cue = "FILE \"IMS-HWVX.BIN\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n";
    const char *sub = "FILE \"Sub\\TRACK.BIN\" BINARY\r\n";
    const char *missing = "FILE \"MISSING.BIN\" BINARY\r\n";

    SDL_CreateDirectory("test_pcdisc-out/sub");
    Put("test_pcdisc-out/ims-hwvx.bin", zeros, sizeof(zeros));
    Put("test_pcdisc-out/sub/track.bin", zeros, sizeof(zeros));
    Put("test_pcdisc-out/ims-hwvx.cue", cue, strlen(cue));
    Put("test_pcdisc-out/sub.cue", sub, strlen(sub));
    Put("test_pcdisc-out/missing.cue", missing, strlen(missing));

    Check(strcmp(Import("test_pcdisc-out/ims-hwvx.cue"), "not an ISO 9660 disc image") == 0,
          "IMS-HWVX.BIN in the .cue opens ims-hwvx.bin");
    Check(strcmp(Import("test_pcdisc-out/sub.cue"), "not an ISO 9660 disc image") == 0,
          "Sub\\TRACK.BIN opens sub/track.bin");
    Check(strcmp(Import("test_pcdisc-out/missing.cue"), "could not open the disc image") == 0,
          "a track that isn't there in any case still fails to open");
    RealDisc("PC disc", "HWVX_TEST_PC_DISC", "HWVX_TEST_PC_DATA", 0);
    RealDisc("PS2 disc", "HWVX_TEST_PS2_DISC", "HWVX_TEST_PS2_DATA", 1);
    return s_fails != 0;
}
