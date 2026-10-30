#include "common.h"



typedef struct GameState {
    FPTR32(void, (void)) interrupt;
    FPTR32(void, (void)) heartbeat;
    FPTR32(s8, (s32 prev)) enter;
    FPTR32(s8, (s32 next)) exit;
} GameState;

typedef struct StateManager {
    GameState states[13];
    s32 current;
    s32 next;
} StateManager;

StateManager *g_pStateManager;

void StateManager_Create(void) {
    extern StateManager *g_pStateManager;
    s32 i;

    g_pStateManager = Allocate(sizeof(StateManager), 0, "");
    for (i = 0; i < 13; i++) {
        SETP(g_pStateManager->states[i].interrupt, NULL);
        SETP(g_pStateManager->states[i].heartbeat, NULL);
        SETP(g_pStateManager->states[i].enter, NULL);
        SETP(g_pStateManager->states[i].exit, NULL);
    }
    SETP(g_pStateManager->states[3].interrupt, NULL);
    SETP(g_pStateManager->states[3].heartbeat, StateMovie_Heartbeat);
    SETP(g_pStateManager->states[3].enter, StateMovie_Init);
    SETP(g_pStateManager->states[3].exit, StateMovie_Shutdown);
    SETP(g_pStateManager->states[5].interrupt, NULL);
    SETP(g_pStateManager->states[5].heartbeat, StateGamePlay_Heartbeat);
    SETP(g_pStateManager->states[5].enter, StateGamePlay_Init);
    SETP(g_pStateManager->states[5].exit, StateGamePlay_Shutdown);
    SETP(g_pStateManager->states[6].interrupt, NULL);
    SETP(g_pStateManager->states[6].heartbeat, StateRenderTest_Heartbeat);
    SETP(g_pStateManager->states[6].enter, StateRenderTest_Init);
    SETP(g_pStateManager->states[6].exit, StateRenderTest_Shutdown);
    SETP(g_pStateManager->states[8].interrupt, NULL);
    SETP(g_pStateManager->states[8].heartbeat, StateShutdown_Heartbeat);
    SETP(g_pStateManager->states[8].enter, StateShutdown_Init);
    SETP(g_pStateManager->states[8].exit, StateShutdown_Shutdown);
    SETP(g_pStateManager->states[2].interrupt, NULL);
    SETP(g_pStateManager->states[2].heartbeat, StateTitleScreen_Heartbeat);
    SETP(g_pStateManager->states[2].enter, StateTitleScreen_Init);
    SETP(g_pStateManager->states[2].exit, StateTitleScreen_Shutdown);
    SETP(g_pStateManager->states[11].interrupt, NULL);
    SETP(g_pStateManager->states[11].heartbeat, StateProgressionManager_Heartbeat);
    SETP(g_pStateManager->states[11].enter, StateProgressionManager_Init);
    SETP(g_pStateManager->states[11].exit, StateProgressionManager_Shutdown);
    SETP(g_pStateManager->states[12].interrupt, NULL);
    SETP(g_pStateManager->states[12].heartbeat, StateGameManager_Heartbeat);
    SETP(g_pStateManager->states[12].enter, StateGameManager_Init);
    SETP(g_pStateManager->states[12].exit, StateGameManager_Shutdown);
    g_pStateManager->current = -1;
    g_pStateManager->next = -1;
}

void StateManager_Destroy(void) {
    extern StateManager *g_pStateManager;
    if (g_pStateManager != NULL) {
        Deallocate(g_pStateManager);
        g_pStateManager = NULL;
    }
}

s32 StateManager_GotoState(s32 state) {
    extern StateManager *g_pStateManager;
    extern void (*Interrupt_Process)(void);
    extern void (*Heartbeat_Process)(void);
    char curStr[64];
    char nextStr[64];

    if (state >= 13 || state < 0) {
        return g_pStateManager->current;
    }
    State_GetGameStateStr(g_pStateManager->current, curStr);
    State_GetGameStateStr(state, nextStr);
    if (g_pStateManager->current != -1 && GETP(g_pStateManager->states[g_pStateManager->current].exit) != NULL &&
        GETP(g_pStateManager->states[g_pStateManager->current].exit)(state) != 1) {
        return g_pStateManager->current;
    }
    if (GETP(g_pStateManager->states[state].enter) != NULL &&
        GETP(g_pStateManager->states[state].enter)(g_pStateManager->current) != 1) {
        return g_pStateManager->current;
    }
    Interrupt_Process = GETP(g_pStateManager->states[state].interrupt) ? GETP(g_pStateManager->states[state].interrupt) : NULL;
    Heartbeat_Process = GETP(g_pStateManager->states[state].heartbeat);
    g_pStateManager->current = state;
    return g_pStateManager->current;
}

s32 StateManager_CurrState(void) {
    extern StateManager *g_pStateManager;
    return g_pStateManager->current;
}

s32 StateManager_QueuedState(void) {
    extern StateManager *g_pStateManager;
    s32 state;

    state = g_pStateManager->next;
    g_pStateManager->next = -1;
    return state;
}

s32 StateManager_QueueChange(s32 state) {
    extern StateManager *g_pStateManager;
    if (g_pStateManager->next == -1) {
        if (state == 8 || FileManager_GetDeviceStatus(0) == 1) {
            g_pStateManager->next = state;
        }
    }
    return g_pStateManager->next;
}

void State_GetGameStateStr(s32 state, char *str) {
    switch (state) {
    case 0:
        strcpy(str, "STATE_NONE");
        break;
    case 4:
        strcpy(str, "STATE_START_MENU");
        break;
    case 5:
        strcpy(str, "STATE_GAME_PLAY");
        break;
    case 8:
        strcpy(str, "STATE_SHUTDOWN");
        break;
    case 11:
        strcpy(str, "STATE_PROGRESSION_MANAGER");
        break;
    case 12:
        strcpy(str, "STATE_GAME_MANAGER");
        break;
    case -1:
    default:
        strcpy(str, "STATE_NONE");
        break;
    }
}

