
#include "common.h"

typedef struct Process {
    FPTR32(void, (void)) init;  /* 0x00 */
    FPTR32(void, (void)) update;  /* 0x04 */
    s32 arg;                    /* 0x08 render fn, see Render */
    u32 flags;                  /* 0x0C bit 0: active */
    s32 unused10;               /* 0x10 */
    s32 unused14;               /* 0x14 */
    char name[16];              /* 0x18 */
} Process;

typedef struct ProcessManager {
    u32 count;                  /* 0x00 */
    s32 unused4;                /* 0x04 */
    s32 unused8;                /* 0x08 */
    Process processes[27];      /* 0x0C */
} ProcessManager;

ProcessManager g_processManager;






void ProcessManager_Create(void) {
}

void ProcessManager_Init(void) {
    g_processManager.count = 0;
}

void ProcessManager_Add(char *name, void (*init)(void), void (*update)(void), s32 arg, u32 flags) {
    u32 index = g_processManager.count++;

    if (name != NULL) {
        strcpy(g_processManager.processes[index].name, name);
    }
    else {
        strcpy(g_processManager.processes[index].name, "...");
    }
    if (init != NULL) {
        init();
    }
    SETP(g_processManager.processes[index].init, init);
    SETP(g_processManager.processes[index].update, update);
    g_processManager.processes[index].arg = arg;
    g_processManager.processes[index].flags = flags;
    g_processManager.processes[index].unused10 = 0;
    g_processManager.processes[index].unused14 = 0;
}

void ProcessManager_Release(char *name) {
    u32 i;

    for (i = 0; i < g_processManager.count; i++) {
        if (strcmp(name, g_processManager.processes[i].name) == 0) {
            for (; i < g_processManager.count - 1; i++) {
                g_processManager.processes[i] = g_processManager.processes[i + 1];
            }
            g_processManager.count--;
            return;
        }
    }
}

void ProcessManager_Heartbeat(u32 activeOnly) {
    extern u32 g_StopInput;
    u32 i;

    if (g_StopInput != 0 && StateManager_CurrState() != 5) {
        return;
    }
    FileStreaming_HeartBeat();
    for (i = 0; i < g_processManager.count; i++) {
        if (activeOnly == 0 || (g_processManager.processes[i].flags & 1) != 0) {
            if (GETP(g_processManager.processes[i].update) != NULL) {
                GETP(g_processManager.processes[i].update)();
            }
        }
    }
}

void ProcessManager_Render(u32 all) {
    u32 i;

    for (i = 0; i < g_processManager.count; i++) {
        if ((void (*)(void))PTR(g_processManager.processes[i].arg) != NULL) {
            if (all != 0 || (g_processManager.processes[i].flags & 1) != 0) {
                ((void (*)(void))PTR(g_processManager.processes[i].arg))();
            }
        }
    }
}

