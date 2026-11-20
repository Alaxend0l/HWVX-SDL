/*
 * test_battleitems.c - the battle weapon filter's store and rolls
 * (Port_BattleItems.c).
 *
 * battle.cfg in HWVX_PREF_DIR reads as the switched-off ids and is written
 * back on a change; in a battle progression a roll over a range never lands
 * on what is off and still reaches every id that is on; a range all off
 * rolls nothing (-1); outside a battle, or with nothing off, the roll is the
 * plain one; the host's mask stands in for the store's until the session
 * ends, and is never saved. No game data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "port_internal.h"
#include "Port_BattleItems.h"
#include "Port_Online.h"

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    s_fails += !ok;
}

static int FileHas(const char *path, const char *want) {
    char buf[256] = {0};
    FILE *f = fopen(path, "r");
    size_t n;

    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strcmp(buf, want) == 0;
}

int main(void) {
    char dir[] = "/tmp/hwvx-battleitems-XXXXXX";
    char path[64];
    FILE *f;
    u32 i;

    if (mkdtemp(dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(path, sizeof(path), "%s/battle.cfg", dir);
    f = fopen(path, "w");
    fprintf(f, "3\n6\n255\nnot a number\n300\n");
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

    StateProgressionManager_SetProgressionID(3, 0);     /* battle */
    {
        int bad = 0, seen[12] = {0}, all = 1;

        for (i = 0; i < 4000; i++) {
            s32 id = Port_BattleItemsRoll(0, 11, i % 7, i);

            if (id < 0 || id > 11 || id == 3 || id == 6) {
                bad++;
            } else {
                seen[id] = 1;
            }
        }
        for (i = 0; i < 12; i++) {
            all &= i == 3 || i == 6 || seen[i];
        }
        Check(!bad, "a battle's roll over 0..11 never lands on what is off");
        Check(all, "and reaches every weapon that is on");
    }
    Check(Port_BattleItemsRoll(6, 6, 1, 2) == -1, "a range all off rolls nothing");
    Check(Port_BattleItemsRoll(62, 62, 1, 2) == 62, "a range with nothing off: the plain roll");

    StateProgressionManager_SetProgressionID(0, 0);     /* adventure */
    {
        int off = 0;

        for (i = 0; i < 2000; i++) {
            s32 id = Port_BattleItemsRoll(0, 11, i % 7, i);

            off |= id == 3 || id == 6;
        }
        Check(off, "outside a battle nothing is filtered");
    }
    StateProgressionManager_SetProgressionID(3, 0);

    Port_BattleItemsSetOff(4, 1);
    Check(Port_BattleItemsIsOff(4) && FileHas(path, "3\n4\n6\n255\n"), "SetOff switches an id off and saves");
    Port_BattleItemsSetOff(4, 0);
    Check(!Port_BattleItemsIsOff(4) && FileHas(path, "3\n6\n255\n"), "and back on");

    {
        u32 words[BATTLE_ITEMS_WORDS] = {0}, k;

        words[0] = 0xDEADBEEF;
        words[BATTLE_ITEMS_WORDS - 1] = 1;
        Port_BattleItemsSetOffWords(words, BATTLE_ITEMS_WORDS);
        for (k = 0; k < BATTLE_ITEMS_WORDS && Port_BattleItemsOffWord(k) == words[k]; k++) {
        }
        Check(k == BATTLE_ITEMS_WORDS, "the host's mask stands in for the store's");
        Port_BattleItemsSetOff(1, 0);
        Check(FileHas(path, "3\n6\n255\n"), "and is never saved");
        Port_BattleItemsReload();
        Check(Port_BattleItemsIsOff(3) && !Port_BattleItemsIsOff(0) && Port_BattleItemsOffWord(0) == 0x48,
              "the session over, battle.cfg's again");
    }

    Port_BattleItemsSetPage(1);
    Check(Port_BattleItemsPage() == 0 || Port_BattleItemsPages() > 1, "the page stays within the list");

    unlink(path);
    rmdir(dir);
    printf("%s: battle items\n", s_fails ? "FAIL" : "ok");
    return s_fails != 0;
}
