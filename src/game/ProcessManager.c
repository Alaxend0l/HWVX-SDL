
#include "common.h"

typedef struct Process {
    void (*init)(void);
    void (*update)(void);
    s32 arg;
    u32 flags;
    s32 unk10;
    s32 unk14;
    char name[16];
} Process;

typedef struct ProcessManager {
    u32 count;
    s32 unk4;
    s32 unk8;
    Process processes[27];
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
    g_processManager.processes[index].init = init;
    g_processManager.processes[index].update = update;
    g_processManager.processes[index].arg = arg;
    g_processManager.processes[index].flags = flags;
    g_processManager.processes[index].unk10 = 0;
    g_processManager.processes[index].unk14 = 0;
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
            if (g_processManager.processes[i].update != NULL) {
                g_processManager.processes[i].update();
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

