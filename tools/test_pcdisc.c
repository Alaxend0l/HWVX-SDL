/*
 * test_pcdisc.c - the .cue's track file found without case (pcdisc.c CueTrack).
 *
 * The retail PC .cue names "IMS-HWVX.BIN"; a copy on a case-sensitive file
 * system may be ims-hwvx.bin (as it is on the disc rip this was found with).
 * Windows and macOS find it either way; the import must too. No game data: the
 * track here is a few zero sectors, so an import that found it stops at "not
 * an ISO 9660 disc image", one that didn't at "could not open the disc image".
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
    return s_fails != 0;
}
