/*
 * test_sfxsize.c - the samples block a pack's replaced sounds go in
 * (PortMix_SFX, PortPS2_SFX; docs-pending/content-safety.md).
 *
 * A mod's sounds are untrusted: each may decode to up to 16M samples, and a
 * pack's were summed in 32 bits, so 32 sounds of 128 MB wrapped the sum to a
 * small block that PortMix_SFXFill then overran. Crafted lengths only (the
 * PCM buffers are a few bytes: a refused block never copies them), no game
 * data, no 4 GB:
 *   - PORT_MIX_SFX_SPACE never wraps;
 *   - 32 sounds of 128 MB (a u32 sum of 0x400) are refused: no block;
 *   - a block just past 256 MB is refused, small ones are allocated whole;
 *   - the PS2's: a sound of a damaged length silences the pack's, no block.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "Voice.h"
#include "Port_Mix.h"
#include "Port_Swap.h"
#include "port_internal.h"

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    s_fails += !ok;
}

/* `n` mod sounds of `len` bytes each over a pack of `n` sounds, then the block. */
static u32 Block(s32 fmt, u32 n, u32 len) {
    static SFX desc[64];
    u32 i, size;

    memset(desc, 0, sizeof(desc));
    for (i = 0; i < n; i++) {
        PortMix_SFXSet(i, n, malloc(64), len, 0xFFFFFFFFu, 22050);
    }
    size = PortMix_SFX(fmt, (u8 *)desc, n);
    PortMix_SFXDone();
    return size;
}

int main(void) {
    Port_InitHandles(PORT_ARENA_SIZE);
    OSInit();
    {
        void *lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);

        OSSetCurrentHeap(OSCreateHeap(lo, OSGetArenaHi()));
    }
    Check(PORT_MIX_SFX_SPACE(0xFFFFFFFFu) == 0x100000020ull, "PORT_MIX_SFX_SPACE(0xFFFFFFFF) doesn't wrap");
    Check(PORT_MIX_SFX_SPACE(1) == 0x40 && PORT_MIX_SFX_SPACE(0x20) == 0x40 && PORT_MIX_SFX_SPACE(0) == 0x20,
          "PORT_MIX_SFX_SPACE rounds to 32 and adds 32");
    /* 32 * (0x8000000 + 0x20) = 0x100000400: 0x400 in 32 bits */
    Check(Block(PORT_DATA_GC, 32, 0x8000000) == 0, "32 sounds of 128 MB: refused (the u32 sum wrapped to 0x400)");
    Check(Block(PORT_DATA_PC, 2, PORT_MIX_SFX_BLOCK_MAX / 2) == 0, "two sounds of 128 MB: refused (past 256 MB)");
    Check(Block(PORT_DATA_GC, 1, PORT_MIX_SFX_BLOCK_MAX) == 0, "one sound of 256 MB: refused");
    Check(Block(PORT_DATA_GC, 3, 64) == 3 * 0x60, "three sounds of 64 bytes: a block of 3 * 0x60");
    Check(Block(PORT_DATA_PS2, 3, 64) == 0, "a PS2 pack's go to PortPS2_SFX instead");
    {
        /* the PS2's: ADPCM lengths whose PCM passes the limit (3.5 times the ADPCM) */
        static SFX desc[4];
        u32 i;

        memset(desc, 0, sizeof(desc));
        for (i = 0; i < 4; i++) {
            PortMix_SFXSet(i, 4, malloc(64), 0x8000000, 0xFFFFFFFFu, 22050);
        }
        PortPS2_SFX((u8 *)desc, 4);
        for (i = 0; i < 4 && desc[i].length == 0; i++) {
        }
        Check(i == 4, "PS2: replaced sounds past 256 MB: the pack's silenced, no block");
        PortPS2_SFXDone();
        PortMix_SFXDone();
    }
    printf("%s: %d check(s) failed\n", s_fails ? "FAILED" : "PASSED", s_fails);
    return s_fails ? 1 : 0;
}
