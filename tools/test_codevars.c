/*
 * test_codevars.c - code variables' arguments past their tables
 * (GameSettings_PortBadIndex, GameSettings.c).
 *
 * Shared levels' checks refuse those (Port_Level.c); the game guards them too.
 * Out of range: the Get returns 0, the Set does nothing (no write outside the
 * table: under -fsanitize=address a global-buffer-overflow otherwise). Every
 * index inside a table reads back what was set. No game data (no MCP: game
 * type 0).
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "port_internal.h"

extern CodeVariables g_codeVariables;

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    s_fails += !ok;
}

int main(void) {
    CodeVariables before;
    u32 i;
    int same;

    Port_InitHandles(PORT_ARENA_SIZE);
    OSInit();
    DVDInit();
    g_port.dataDir = ".";   /* no game data here: the MCP isn't found, game type 0 */
    FileManager_Create();
    {
        void *lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);

        OSSetCurrentHeap(OSCreateHeap(lo, OSGetArenaHi()));
    }
    for (i = 1; i <= 4; i++) {
        GameSettings_SetCodeVariable(0xCB, i, 10 + i);
        GameSettings_SetCodeVariable(0xCC, i, 20 + i);
        GameSettings_SetCodeVariable(0x192, i, 30 + i);
    }
    same = 1;
    for (i = 1; i <= 4; i++) {
        same &= g_codeVariables.playerVehicle[i - 1] == (s32)(10 + i) && GameSettings_GetCodeVariable(0xCC, i) == 20 + i &&
                GameSettings_GetCodeVariable(0x192, i) == 30 + i;
    }
    Check(same, "ports 1-4: what is set reads back");
    for (i = 0; i < 0xC; i++) {
        g_codeVariables.cheatsEnabled[i] = (u8)(i & 1);
    }
    same = 1;
    for (i = 0; i < 0xC; i++) {
        same &= GameSettings_GetCodeVariable(0x6C, i) == (i & 1);
    }
    Check(same, "0x6C: cheats 0-11 read back");

    before = g_codeVariables;
    GameSettings_SetCodeVariable(0xCB, 0, 99);
    GameSettings_SetCodeVariable(0xCC, 0, 99);
    GameSettings_SetCodeVariable(0xCB, 0x40000000, 99);
    GameSettings_SetCodeVariable(0xCC, 77, 99);
    GameSettings_SetCodeVariable(0x191, 0, 99);
    Check(memcmp(&before, &g_codeVariables, sizeof(before)) == 0, "sets past the tables: nothing written");
    Check(GameSettings_GetCodeVariable(0x6A, 0x10000) == 0, "0x6A, cheat 0x10000: 0");
    Check(GameSettings_GetCodeVariable(0x6B, 0x10000) == 0, "0x6B, cheat 0x10000: 0");
    Check(GameSettings_GetCodeVariable(0x6C, 0x10000) == 0, "0x6C, cheat 0x10000: 0");
    Check(GameSettings_GetCodeVariable(0xC9, 0x10000) == 0, "0xC9, port 0x10000: 0");
    Check(GameSettings_GetCodeVariable(0xCC, 0x10000) == 0, "0xCC, port 0x10000: 0");   /* 0xCB asks the state manager first */
    Check(GameSettings_GetCodeVariable(0xCC, 0) == 0, "0xCC, port 0: 0");
    Check(GameSettings_GetCodeVariable(0xD1, 0) == 0 && GameSettings_GetCodeVariable(0x193, 0) == 0, "0xD1, 0x193, port 0: 0");
    Check(memcmp(&before, &g_codeVariables, sizeof(before)) == 0, "gets past the tables: nothing written");

    printf("%s: %d check(s) failed\n", s_fails ? "FAILED" : "PASSED", s_fails);
    return s_fails ? 1 : 0;
}
