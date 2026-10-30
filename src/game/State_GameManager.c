#include "common.h"


void StateGameManager_RefreshMCPTextures(void) {
    void *pack;
    u32 i;

    pack = FileStreaming_InitializeDataPack(HND((void *)PTR(FileStreaming_LoadDataPack("mcp", 3, 0, 0))), 3, 1);
    if (pack != NULL) {
        for (i = 0; i < M2C_FIELD(pack, u32 *, 0x2C); i++) {
            Gfx_UnLoadTexture(SLOT_LOAD(u16 *, (u8 *)(pack) + 0x30)[i]);
        }
        DataPack_RefreshTextures(pack);
    }
}

s32 StateGameManager_Heartbeat(void) {
    typedef struct { s32 name; s32 script; } PackRef;
    typedef struct { u8 type; void *movie; PackRef *pack; } Step;
    typedef struct { u8 pad0[4]; s32 menuName; u8 pad8[0x20]; s32 menuFile; u32 numSteps; Step *steps; } MCP;
    extern struct { MCP *mcp; u32 step; } g_GameManagerState;
    extern s8 g_bInterfacePlaying;
    extern s32 g_interfacePackID;
    s8 loadDefaults = 0;
    Step *step;
    void *pack;

    if (g_GameManagerState.mcp == NULL) {
        StateManager_GotoState(0xB);
        return 1;
    }
    if (g_GameManagerState.step < g_GameManagerState.mcp->numSteps) {
        step = &g_GameManagerState.mcp->steps[g_GameManagerState.step];
        switch (step->type) {
        case 0:
            if (step->movie != NULL) {
                StateMovie_SetMovieToPlay(HND(step->movie));
                StateManager_QueueChange(3);
            }
            break;
        case 1:
            loadDefaults = 1;
        case 4:
            if (!g_bInterfacePlaying) {
                g_interfacePackID = FileStreaming_LoadDataPack(PTR(step->pack->name), 1, step->pack->script, 0);
                if ((pack = FileStreaming_InitializeDataPack(g_interfacePackID, 1, 1)) != NULL) {
                    InterfaceManager_Create(DataPack_GetScript(pack, step->pack->script), 0, StateGameManager_InterfaceHandler);
                    g_bInterfacePlaying = 1;
                }
                if (g_GameManagerState.step == g_GameManagerState.mcp->numSteps - 1) {
                    FileStreaming_LoadDataPack(PTR(g_GameManagerState.mcp->menuName), 1, g_GameManagerState.mcp->menuFile, 0);
                }
            }
            ShowStats_StartFrame();
            ViewManager_PrepForRender();
            ProcessManager_Heartbeat(0);
            ShowStats_BeginScene();
            ProcessManager_Render(1);
            ShowStats_EndScene();
            if (!g_bInterfacePlaying && InterfaceManager_GetTotalActiveInterfaces() == 0) {
                FileStreaming_ReleaseDataPack(g_interfacePackID, 1);
                g_interfacePackID = -1;
                if (loadDefaults) {
                    GameSettings_LoadDefaultMemoryCardData();
                }
            } else {
                g_bInterfacePlaying = 1;
            }
            break;
        }
        if (!g_bInterfacePlaying && GameSettings_GetCodeVariable(0x198, 0) == 0) {
            if (loadDefaults) {
                GameSettings_LoadDefaultMemoryCardData();
            }
            g_GameManagerState.step++;
        }
    } else {
        StateTitleScreen_SetMenuFile(PTR(g_GameManagerState.mcp->menuName), g_GameManagerState.mcp->menuFile);
        StateManager_QueueChange(2);
    }
    return 1;
}

s32 StateGameManager_Shutdown(void) {

    if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
        FileStreaming_ReleaseAllDataPacks(1, 1);
    }
    FileStreaming_HeartBeat();
    ShowStats_Release();
    return 1;
}

void StateGameManager_Release(void) {
    FileStreaming_ReleaseAllDataPacks(3, 1);
}

s32 StateGameManager_GetProgScript(u32 index) {
    extern struct { u8 *mcp; s32 unk4; } g_GameManagerState;

    if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
        if (g_GameManagerState.mcp != NULL) {
            return M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(g_GameManagerState.mcp) + 0x24), s32 *, 8);
        }
        return 0;
    }
    if (g_GameManagerState.mcp == NULL || index >= M2C_FIELD(g_GameManagerState.mcp, u32 *, 0x34)) {
        return 0;
    }
    return SLOT_LOAD(s32 *, (u8 *)(g_GameManagerState.mcp) + 0x38)[index];
}

void StateGameManager_InterfaceHandler(void *event) {
    extern u8 g_bInterfacePlaying;
    switch (M2C_FIELD(event, u16 *, 4)) {
    case 5:
        if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
            StateManager_QueueChange(0xB);
        }
        g_bInterfacePlaying = 0;
        break;
    }
}

