#include "common.h"

typedef struct DemoSettings {
    u8 bRandom;
    u8 pad1[7];
    u32 *demoList;
} DemoSettings;

typedef struct MCPSettings {
    u8 pad0[0x24];
    DemoSettings *demos;
} MCPSettings;

void GameSettings_SetCodeVariable(s32 code, s32 arg, s32 value);
void *g_mainMenuFile;
void *g_pInterfaceScript;
s8 g_bPlayingFMV;
void *g_pTitleDataPack;
s32 g_gameDifficulty;
s32 g_mainMenuFileID;
s8 g_clearTextureCache;

void StateTitleScreen_SetMenuFile(void *file, s32 fileID) {
    g_mainMenuFile = file;
    g_mainMenuFileID = fileID;
}



s32 StateTitleScreen_Init(s32 prevState) {
    extern void *g_pTitleDataPack;
    extern void *g_pInterfaceScript;
    extern s8 g_bPlayingFMV;
    extern void *g_mainMenuFile;
    extern s32 g_mainMenuFileID;
    extern s8 g_clearTextureCache;
    void ParticleManager_Render(void);
    void *CameraManager_GetNewCamera(void);
    void *camera;
    void *script;
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    Gfx_SetProjectionMatrix(90.0f);
    Gfx_StateTextureBlendMode(2);
    Gfx_BeginScene();
    Gfx_EndScene();
    Gfx_FlipBuffers();
    Gfx_BeginScene();
    Gfx_EndScene();
    Gfx_FlipBuffers();
    GameSettings_ResetGame();
    ShowStats_Init();
    Gfx_TextureManager_Init();
    StateGameManager_RefreshMCPTextures();
    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, 0, 0);
    ProcessManager_Add("Input", Input_Init, Input_Heartbeat, 0, 1);
    if (prevState != 0xC && prevState != 3) {
        ProcessManager_Add("SoundManager", SoundManager_Init, SoundManager_Heartbeat, 0, 1);
    } else {
        ProcessManager_Add("SoundManager", NULL, SoundManager_Heartbeat, 0, 1);
    }
    ProcessManager_Add("Interface", InterfaceManager_Init, InterfaceManager_Heartbeat, (s32)HND(InterfaceManager_Render), 1);
    ProcessManager_Add("Particle", ParticleManager_Init, ParticleManager_Heartbeat, (s32)HND(ParticleManager_Render), 0);
    ProcessManager_Add("Shader", NULL, ShaderHolder_Heartbeat, 0, 0);
    ProcessManager_Add("ThingManager", ThingManager_DARYL_Init, ThingManager_DARYL_Heartbeat, (s32)HND(ThingManager_DARYL_Render), 0);
    ProcessManager_Add("Lid Handler", LidHandler_Init, LidHandler_Heartbeat, 0, 1);
    switch (prevState) {
    case 3:
        DataPack_RefreshTextures(g_pTitleDataPack);
        InterfaceManager_Create(g_pInterfaceScript, 0, TitleScreen_Interface);
        g_bPlayingFMV = 0;
        g_pInterfaceScript = NULL;
        break;
    default:
        if (g_mainMenuFile == NULL) {
            return 0;
        }
        g_pTitleDataPack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(g_mainMenuFile, 1, g_mainMenuFileID, 0), 1, 1);
        if (g_pTitleDataPack == NULL) {
            return 0;
        }
        script = DataPack_GetScript(g_pTitleDataPack, g_mainMenuFileID);
        if (script == NULL) {
            return 0;
        }
        InterfaceManager_Create(script, 0, TitleScreen_Interface);
        g_clearTextureCache = 0;
        if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
            GameSettings_SetCodeVariable(0xC8, 0, g_gameDifficulty);
            GameSettings_SetCodeVariable(0x198, 0, 0);
        }
        break;
    }
    CameraManager_Init();
    CameraManager_Pause(1);
    PlayerManager_Init();
    PlayerManager_NewHumanPlayer();
    min.x = min.y = min.z = -500.0f;
    max.x = max.y = max.z = 500.0f;
    Grid_Init(&min, &max, 0x10, 0x10);
    if (GetCameraManager()[0x640] == 0) {
        camera = CameraManager_GetNewCamera();
    }
    ViewManager_SetCamera(camera, 0);
    GameSettings_ResetPlayerVehicleItem();
    return 1;
}


s32 StateTitleScreen_Shutdown(void) {
    extern s8 g_bPlayingFMV;
    extern void *g_pTitleDataPack;
    Gfx_BeginScene();
    Gfx_EndScene();
    Gfx_FlipBuffers();
    Gfx_BeginScene();
    Gfx_EndScene();
    Gfx_FlipBuffers();
    ShowStats_Release();
    if ((s8) g_bPlayingFMV == 0) {
        InterfaceManager_ReleaseAllInterfaces();
        FileStreaming_ReleaseAllDataPacks(1, 1);
        g_pTitleDataPack = 0;
    }
    return 1;
}

s32 StateTitleScreen_Heartbeat(void) {
    M2C_UNK TextureManager_ReleaseCache();
    extern s8 g_clearTextureCache;
    ShowStats_StartFrame();
    ViewManager_PrepForRender();
    ProcessManager_Heartbeat(0);
    ShowStats_BeginScene();
    ProcessManager_Render(1);
    Wheel_PlayAllAirborne();
    ShowStats_EndScene();
    if ((s8) g_clearTextureCache == 0) {
        TextureManager_ReleaseCache();
        g_clearTextureCache = 1;
    }
    return 1;
}

void TitleScreen_Interface(InterfaceMsg *msg) {
    extern void *g_pInterfaceScript;
    extern s8 g_bPlayingFMV;
    switch (msg->type) {
    case 0:
        break;
    case 6:
        msg->result = 4;
        break;
    case 5:
        if (GameSettings_GetCodeVariable(0x198, 0)) {
            StateManager_QueueChange(0xB);
        }
        break;
    case 15:
        if (IsLoadingThreadRunning()) {
            msg->result = 0xB;
        }
        break;
    case 9:
        StateManager_QueueChange(2);
        break;
    case 17:
        g_pInterfaceScript = (void *)PTR(msg->value);
        g_bPlayingFMV = 1;
        msg->result = 4;
        break;
    }
}

void TitleScreen_RunDemoLoop(void) {
    static u32 progressionIndex;
    MCPSettings *mcp;
    u32 index;

    mcp = GameSettings_GetMCP();
    if (mcp == NULL || mcp->demos->demoList == NULL) {
        return;
    }
    if (mcp->demos->bRandom) {
        index = RandomNumberGenerator_GetRandomInteger(0, *mcp->demos->demoList);
    } else {
        index = progressionIndex++;
    }
    if (index >= *mcp->demos->demoList) {
        index = 0;
        progressionIndex = 1;
    }
    if (index < 6) {
        GameSettings_SetCodeVariable(0x12D, 0, 3);
    } else {
        GameSettings_SetCodeVariable(0x12D, 0, 2);
    }
    g_gameDifficulty = GameSettings_GetDifficulty();
    GameSettings_SetCodeVariable(0x198, 0, 1);
    GameSettings_SetCodeVariable(0x133, 0, 3);
    GameSettings_SetCodeVariable(0x131, 0, 1);
    GameSettings_SetCodeVariable(0xC8, 0, 3);
    StateProgressionManager_SetProgressionID(0, index);
}

