/*
 * test_battleitems.c - the battle weapon filter's store and rolls
 * (Port_BattleItems.c).
 *
 * battle.cfg in HWVX_PREF_DIR reads as the switched-off ids; a roll over a
 * range avoids them (every attempt, across many spawn points); a range that
 * is entirely off falls back to a roll of it (an item the world can spawn
 * beats the filter emptying the world); the mask's words round-trip for the
 * online setup. No game data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "port_internal.h"
#include "Port_BattleItems.h"

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    s_fails += !ok;
}

int main(void) {
    char dir[] = "/tmp/hwvx-battleitems-XXXXXX";
    char path[64];
    FILE *f;
    s32 seen[64];
    u32 i;

    if (mkdtemp(dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(path, sizeof(path), "%s/battle.cfg", dir);
    f = fopen(path, "w");
    fprintf(f, "3\n6\n255\n");
    fclose(f);
    setenv("HWVX_PREF_DIR", dir, 1);

    Port_InitHandles(PORT_ARENA_SIZE);
    OSInit();
    DVDInit();
    RandomNumberGenerator_Init();
    g_port.dataDir = ".";   /* no game data: no MCP */
    {
        void *lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);

        OSSetCurrentHeap(OSCreateHeap(lo, OSGetArenaHi()));
    }

    Check(Port_BattleItemsIsOff(3) && Port_BattleItemsIsOff(6) && Port_BattleItemsIsOff(255),
          "battle.cfg's ids read as off");
    Check(!Port_BattleItemsIsOff(0) && !Port_BattleItemsIsOff(11) && !Port_BattleItemsIsOff(256),
          "the others, and ids past the mask, as on");
    Check(Port_BattleItemsSeen(seen, 64) == 0, "nothing seen before a world was loaded");

    /* offline (no online session): the game's own RNG each attempt */
    {
        int bad = 0, changed = 0;

        for (i = 0; i < 2000; i++) {
            s32 id = Port_BattleItemsRoll(0, 11, i % 7, i);

            if (id == 3 || id == 6) {
                bad++;
            }
            if (id != 0 && id != 1) {
                changed = 1;
            }
        }
        Check(!bad, "a roll over 0..11 never lands on what is off");
        Check(changed, "the rolls vary");
    }
    Check(Port_BattleItemsRoll(6, 6, 1, 2) == 6, "a range entirely off still rolls (the last roll)");

    Port_BattleItemsSetOff(4, 1);
    Check(Port_BattleItemsIsOff(4), "SetOff switches an id off");
    Port_BattleItemsSetOff(4, 0);
    Check(!Port_BattleItemsIsOff(4), "and back on");

    {
        u32 words[BATTLE_ITEMS_WORDS] = {0};
        u32 k;

        words[0] = 0xDEADBEEF;
        words[BATTLE_ITEMS_WORDS - 1] = 1;
        Port_BattleItemsSetOffWords(words, BATTLE_ITEMS_WORDS);
        for (k = 0; k < BATTLE_ITEMS_WORDS; k++) {
            if (Port_BattleItemsOffWord(k) != words[k]) {
                break;
            }
        }
        Check(k == BATTLE_ITEMS_WORDS, "the mask's words round-trip for the online setup");
        {
            int bad = 0;

            for (k = 0; k < 256; k++) {
                int want = (int)((words[k / 32] >> (k % 32)) & 1), got = Port_BattleItemsIsOff((u32)k);

                if (want != got) {
                    printf("   id %u: want %d got %d\n", k, want, got);
                    bad = 1;
                    break;
                }
            }
            Check(!bad, "the host's mask applies id by id");
        }
    }

    unlink(path);
    rmdir(dir);
    printf("%s: battle items\n", s_fails ? "FAIL" : "ok");
    return s_fails != 0;
}
