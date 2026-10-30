#include "common.h"

s32 Main_Init(void) {
    
    s8 Init_Display(void);
    GameInfo info;

    TimeHandler_Init();
    Init_Math3D();
    Gfx_Create();
    if (!Init_Display()) {
        Gfx_Destroy();
        return 0;
    }
    Input_Create();
    Input_Init();
    SoundManager_Create();
    FileManager_Create();
    LogHandler_Create("MM_Log.txt", 1);
    DataPack_Initialize();
    StateManager_Create();
    CameraManager_Create();
    ThingManager_DARYL_Create();
    if (!Geometry_Create()) {
        return 0;
    }
    RandomNumberGenerator_Init();
    ParticleManager_Init();
    Trail_Create();
    World_Create();
    ShaderHolder_Create();
    MatrixCache_Create();
    ProcessManager_Create();
    ShowStats_Create();
    Trigger_Create();
    Action_Create();
    VertexColorSeqManager_Create();
    FileStreaming_Initialize();
    VolumeManager_Create();
    GSI_Init();
    Gfx_TextureManager_Init();
    InterfaceManager_Init();
    GameSettings_GetGameInfo(&info);
    FileManager_SetAppInfo(info.name, info.id, info.title, info.fileName, info.icon);
    return StateManager_GotoState(12) != -1;
}


s32 Main_Loop(void) {
    extern s32 (*Heartbeat_Process)(void);
    s32 queued;

    queued = StateManager_QueuedState();
    if (queued != -1 && queued != StateManager_GotoState(queued)) {
        return 0;
    }
    return Heartbeat_Process();
}

s32 Main_Shutdown(void) {

    PlayerManager_Destroy();
    ThingManager_DARYL_Destroy();
    Input_Destroy();
    ShaderHolder_Destroy();
    VertexColorSeqManager_Destroy();
    MatrixCache_Destroy();
    World_Destroy();
    Geometry_Destroy();
    StateManager_Destroy();
    CameraManager_Destroy();
    Trigger_Destroy();
    Action_Destroy();
    LogHandler_Destroy();
    FileManager_Destroy();
    Gfx_Destroy();
    StateGameManager_Release();
    FileStreaming_Destroy();
    SoundManager_Destroy();
    HeapDump();
    return 1;
}
