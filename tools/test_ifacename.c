/*
 * test_ifacename.c - an interface pack name a load/play interface action keeps
 * (Port_LevelPackName; ActionLoadInterfacePack, ActionPlayInterfacePack).
 *
 * A shared level's parameter lists are its own memory, freed when the level is
 * taken out; the streaming slot the load action claims keeps the name's pointer
 * and later loads compare it (FileStreaming_GetPackID, FileStreaming_LoadDataPack).
 * No game data: the slot is claimed by a threaded load whose thread never runs
 * (the stubs' RunThread). Under -fsanitize=address the compare of a kept
 * pointer into the freed list is a heap-use-after-free:
 *   - load from a heap list, free it, then look the pack up by another name
 *     (same id) and by a new copy of its own name, and unload it;
 *   - the same name gives the same copy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "Port_Level.h"
#include "port_internal.h"

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    s_fails += !ok;
}

/* a load/unload interface action's list: { id, name[] } */
static s32 *List(u32 id, const char *name) {
    s32 *w = calloc(1, 4 + strlen(name) + 4);

    w[0] = (s32)id;
    strcpy((char *)(w + 1), name);
    return w;
}

int main(void) {
    s32 *level, *again;
    s32 slot;

    Port_InitHandles(PORT_ARENA_SIZE);
    OSInit();
    {
        void *lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);

        OSSetCurrentHeap(OSCreateHeap(lo, OSGetArenaHi()));
    }
    Check(Port_LevelPackName("lvlpack") == Port_LevelPackName("lvlpack"), "the same name, the same copy");
    Check(Port_LevelPackName("lvlpack") != Port_LevelPackName("other"), "another name, another copy");

    level = List(7, "lvlpack");
    Check(ActionLoadInterfacePack(level) != 0, "the level's load action claims a slot");
    slot = FileStreaming_GetPackID("lvlpack", 1, 7);
    Check(slot >= 0, "the slot is found by its name");
    free(level);   /* the level is taken out */

    /* what a later load of an interface pack does: compare every used slot's name */
    Check(FileStreaming_GetPackID("otherpack", 1, 7) == -1, "another name, the same id: no slot (reads the kept name)");
    again = List(7, "lvlpack");
    Check(FileStreaming_GetPackID((char *)(again + 1), 1, 7) == slot, "a new copy of the name finds the slot");
    ActionUnloadInterfacePack(again);
    free(again);

    printf("%s: %d check(s) failed\n", s_fails ? "FAILED" : "PASSED", s_fails);
    return s_fails ? 1 : 0;
}
