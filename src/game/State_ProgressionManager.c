#include "common.h"

typedef struct LWItem {
    u8 pad0[0x1C];
    PTR32(void) script;   /* 0x1C */
    u8 pad20[0x10];
    u16 id;         /* 0x30 */
    u8 pad32[4];
    u8 lo;          /* 0x36 */
    u8 hi;          /* 0x37 */
    u8 pad38[8];
} LWItem;

typedef struct LWItemList {
    u8 pad0[0x14];
    u32 count;      /* 0x14 */
    PTR32(LWItem) items;  /* 0x18 */
} LWItemList;

typedef struct LWQuad {
    u8 pad0[0x1C];
    PTR32(void) bsp;           /* 0x1C */
    u32 numNeighbors;    /* 0x20 */
    PTR32(u32) neighbors;      /* 0x24 */
    u8 pad28[8];
    PTR32(LWItemList) items;   /* 0x30 */
    u8 pad34[0x1C];
} LWQuad;

typedef struct LWCar {
    u8 pad0[0x24];
    PTR32(void) script;   /* 0x24 */
    u8 pad28[8];
    u32 id;         /* 0x30 */
    u8 pad34;
    u8 type;        /* 0x35 */
    u8 pad36;
    u8 hi;          /* 0x37 */
    u8 pad38[0x18];
    PTR32(void) script2;  /* 0x50 */
    PTR32(void) script3;  /* 0x54 */
    u8 pad58[5];
    u8 pack2;       /* 0x5D */
    u8 pack3;       /* 0x5E */
    u8 pad5F;
} LWCar;

typedef struct LWPacks {
    u8 pad0[0xA0];
    PTR32(char) aiPack;   /* 0xA0 */
    PTR32(char) itemPack; /* 0xA4 */
} LWPacks;

typedef struct LWWorld {
    PTR32(void) unk0;
    u32 pad4;
    u32 numCars;     /* 0x08 */
    PTR32(LWCar) cars;     /* 0x0C */
    u8 pad10[0x10];
    u32 numQuads;    /* 0x20 */
    PTR32(LWQuad) quads;   /* 0x24 */
    u8 pad28[0x54];
    PTR32(char) pack5Name; /* 0x7C */
    s32 pack5ID;     /* 0x80 */
    PTR32(char) pack6Name; /* 0x84 */
    s32 pack6ID;     /* 0x88 */
    u8 pad8C[0x10];
    PTR32(LWPacks) packs;  /* 0x9C */
} LWWorld;

extern u8 g_GameProgressionState[];  /* typed per function below */

void StateProgressionManager_SetProgressionID(s32 id, s32 subID) {
    extern s32 g_GameProgressionID;
    #define g_GameProgressionState ((s32 *)(void *)&g_GameProgressionState)
    g_GameProgressionID = id;
    g_GameProgressionState[2] = subID;
#undef g_GameProgressionState
}

void StateProgressionManager_GetProgressionID(s32 *id, s32 *subID) {
    extern s32 g_GameProgressionID;
    #define g_GameProgressionState ((s32 *)(void *)&g_GameProgressionState)
    *id = g_GameProgressionID;
    *subID = g_GameProgressionState[2];
#undef g_GameProgressionState
}
extern u8 g_bRestartCurrentLevel[];  /* typed per function below */

void StateProgressionManager_RestartCurrentLevel(void) {
    #define g_bRestartCurrentLevel (*(u8 *)(void *)&g_bRestartCurrentLevel)
    g_bRestartCurrentLevel = 1;
    ExitStateGamePlay(0xB);
#undef g_bRestartCurrentLevel
}
extern u8 g_GameProgressionEndState[];  /* typed per function below */

s32 StateProgressionManager_Init(s32 prevState) {
    extern s32 g_GameProgressionID;
    #define g_GameProgressionState ((s32 *)(void *)&g_GameProgressionState)
    #define g_GameProgressionEndState ((s32 *)(void *)&g_GameProgressionEndState)
    #define g_bRestartCurrentLevel (*(s8 *)(void *)&g_bRestartCurrentLevel)
    void *camera;
    s32 *script;
    Vector4 min;
    Vector4 max;

    ShowStats_Init();
    Gfx_TextureManager_Init();
    StateGameManager_RefreshMCPTextures();
    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, 0, 0);
    ProcessManager_Add("Input", Input_Init, Input_Heartbeat, 0, 1);
    ProcessManager_Add("SoundManager", SoundManager_Init, SoundManager_Heartbeat, 0, 1);
    ProcessManager_Add("Interface", InterfaceManager_Init, InterfaceManager_Heartbeat, (s32)HND(InterfaceManager_Render), 1);
    ProcessManager_Add("Particles", ParticleManager_Init, ParticleManager_Heartbeat, (s32)HND(ParticleManager_Render), 0);
    ProcessManager_Add("Shader", NULL, ShaderHolder_Heartbeat, 0, 0);
    ProcessManager_Add("ThingManager", ThingManager_DARYL_Init, ThingManager_DARYL_Heartbeat, (s32)HND(ThingManager_DARYL_Render), 0);
    ProcessManager_Add("LidHandler", LidHandler_Init, LidHandler_Heartbeat, 0, 1);
    Gfx_FontManager_Init();
    Gfx_SetEnviroMapMode(1);
    CameraManager_Init();
    ViewManager_Init();
    PlayerManager_Init();
    PlayerManager_NewHumanPlayer();
    min.x = min.y = min.z = -500.0f;
    max.x = max.y = max.z = 500.0f;
    Grid_Init(&min, &max, 0x10, 0x10);
    if (M2C_FIELD(GetCameraManager(), u8 *, 0x640) == 0) {
        camera = CameraManager_GetNewCamera();
    }
    ViewManager_SetCamera(camera, 0);
    GameSettings_GetMCP();
    if (GameSettings_GetCodeVariable(0x12D, 0) == 4) {
        GameSettings_SetCodeVariable(0x12D, 0, 0);
    }
    if (prevState == 0xC && GameSettings_GetCodeVariable(0x198, 0) != 0) {
        prevState = 2;
    }
    switch (prevState) {
    case 2:
    case 11:
        script = (s32 *)PTR(StateGameManager_GetProgScript(g_GameProgressionID));
        g_GameProgressionState[0] = (s32)HND(script);
        g_GameProgressionState[1] = 0;
        g_GameProgressionState[3] = 0;
        *(u8 *)&g_GameProgressionState[4] = 0;
        g_GameProgressionState[5] = 0;
        if (g_GameProgressionState[2] >= script[0]) {
            g_GameProgressionState[2] = 0;
        }
        g_GameProgressionEndState[0] = 4;
        g_GameProgressionEndState[1] = 0;
        break;
    case 5:
        g_GameProgressionState[5] = 0;
        if ((u32)g_GameProgressionState[0] == 0) {
            return 1;
        }
        if (g_bRestartCurrentLevel) {
            g_GameProgressionState[1] = 0;
            g_GameProgressionState[3] = 0;
        } else {
            g_GameProgressionState[3] = 0;
            g_GameProgressionState[1] = 2;
        }
        break;
    case 3:
        if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
            StateManager_QueueChange(2);
            return 1;
        }
        if ((u32)g_GameProgressionState[0] == 0) {
            return 0;
        }
        switch (g_GameProgressionState[1]) {
        case 0:
        case 2:
            g_GameProgressionState[3]++;
            break;
        case 1:
            break;
        }
        break;
    case 12:
        g_GameProgressionState[1] = 1;
        g_GameProgressionState[5] = 0;
        break;
    default:
        return 0;
    }
    return 1;
#undef g_GameProgressionEndState
#undef g_GameProgressionState
#undef g_bRestartCurrentLevel
}

s32 StateProgressionManager_Heartbeat(void) {
    typedef struct {
        PTR32(char) name;
        s32 id;
    } ProgPack;
    typedef struct {
        u8 type;
        u8 pad[3];
        PTR32(void) data;
    } ProgStep;
    typedef struct {
        u32 count;
        PTR32(ProgStep) steps;
        u16 advance;
    } ProgSequence;
    typedef struct {
        u32 count;
        PTR32(ProgSequence) seqs;
    } ProgSeqList;
    typedef struct {
        u32 count;
        PTR32(ProgStep) steps;
        PTR32(ProgPack) pack;
        PTR32(char) world;
        ProgSeqList win;
        ProgSeqList lose;
    } ProgLevel;
    typedef struct {
        s32 count;
        PTR32(ProgLevel) levels;
    } ProgScript;
    typedef struct {
        PTR32(ProgScript) script;
        s32 phase;
        s32 index;
        u32 step;
        s8 loaded;
        u8 pad[3];
        s32 worldState;
    } ProgState;
    #define g_GameProgressionState (*(ProgState *)(void *)&g_GameProgressionState)
    #define g_GameProgressionEndState ((u32 *)(void *)&g_GameProgressionEndState)
    extern s8 g_bLoadingDemoLand;
    static s32 loadingInterfaceID;
    char buf[512];
    char *world;
    ProgLevel *level;
    ProgSequence *seq;
    ProgPack *pack;
    void *dataPack;
    s32 id;
    s32 phase;

    level = NULL;
    if (GETP(g_GameProgressionState.script) != NULL) {
        level = &GETP(GETP(g_GameProgressionState.script)->levels)[g_GameProgressionState.index];
        if (g_GameProgressionState.index >= GETP(g_GameProgressionState.script)->count || g_GameProgressionState.index < 0) {
            if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
                StateManager_GotoState(0xC);
            } else {
                StateManager_GotoState(2);
            }
            return 1;
        }
        world = GETP(level->world);
    } else {
        world = "DemoLand";
        g_bLoadingDemoLand = 1;
    }
    phase = g_GameProgressionState.phase;
    switch (phase) {
    case 0:
    case 2:
        if (phase == 0) {
            seq = (ProgSequence *)level;
        } else if (g_GameProgressionEndState[0] == 4) {
            if (g_GameProgressionEndState[1] > level->win.count) {
                StateManager_GotoState(2);
                return 1;
            }
            seq = &GETP(level->win.seqs)[g_GameProgressionEndState[1]];
        } else {
            if (g_GameProgressionEndState[1] > level->lose.count) {
                StateManager_GotoState(2);
                return 1;
            }
            seq = &GETP(level->lose.seqs)[g_GameProgressionEndState[1]];
        }
        if (g_GameProgressionState.step >= seq->count) {
            if (phase == 0) {
                g_GameProgressionState.phase = 1;
            } else {
                g_GameProgressionState.index += GETP((g_GameProgressionEndState[0] == 4 ? &level->win : &level->lose)->seqs)[g_GameProgressionEndState[1]].advance;
                g_GameProgressionState.phase = 0;
                g_GameProgressionState.worldState = 0;
                g_GameProgressionState.step = 0;
            }
        } else {
            switch (GETP(seq->steps)[g_GameProgressionState.step].type) {
            case 0:
                StateMovie_SetMovieToPlay(HND(GETP(GETP(seq->steps)[g_GameProgressionState.step].data)));
                StateManager_QueueChange(3);
                break;
            case 1:
                if (!g_GameProgressionState.loaded) {
                    pack = GETP(GETP(seq->steps)[g_GameProgressionState.step].data);
                    GameSettings_GetMCP();
                    dataPack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(GETP(pack->name), 1, pack->id, 0), 1, 1);
                    if (dataPack == NULL) {
                        sprintf(buf, "Could not load data pack %s (%d)XXXXXXXXXXXXXXXXX\n", GETP(pack->name), pack->id);
                    }
                    InterfaceManager_Create(DataPack_GetScript(dataPack, pack->id), 0, 0);
                    g_GameProgressionState.loaded = 1;
                } else if (InterfaceManager_GetTotalActiveInterfaces() == 0) {
                    pack = GETP(GETP(seq->steps)[g_GameProgressionState.step].data);
                    g_GameProgressionState.loaded = 0;
                    g_GameProgressionState.step++;
                    FileStreaming_ReleaseDataPack(FileStreaming_GetPackID(GETP(pack->name), 1, pack->id), 1);
                    FileStreaming_HeartBeat();
                    SoundManager_Init();
                } else {
                    ShowStats_StartFrame();
                    ViewManager_PrepForRender();
                    ProcessManager_Heartbeat(0);
                    ShowStats_BeginScene();
                    ProcessManager_Render(1);
                    ShowStats_EndScene();
                }
                break;
            }
        }
        break;
    case 1:
        if (GETP(g_GameProgressionState.script) != NULL && !g_GameProgressionState.loaded && GETP(GETP(level->pack)->name) != NULL) {
            ProcessManager_Release("XXXXXXX");
            DataPack_EnableLoadingSounds(0);
            id = FileStreaming_LoadDataPack(GETP(GETP(level->pack)->name), 1, GETP(level->pack)->id, 0);
            DataPack_EnableLoadingSounds(1);
            loadingInterfaceID = InterfaceManager_Create(DataPack_GetScript(FileStreaming_InitializeDataPack(id, 1, 1), GETP(level->pack)->id), 0, 0);
            g_GameProgressionState.loaded = 1;
        }
        ShowStats_StartFrame();
        ProcessManager_Heartbeat(0);
        ShowStats_BeginScene();
        ProcessManager_Render(1);
        ShowStats_EndScene();
        if (g_GameProgressionState.worldState != 7) {
            g_GameProgressionState.worldState = LoadWorldProcess(world, g_GameProgressionState.loaded);
            switch (g_GameProgressionState.worldState) {
            case 8:
                g_GameProgressionState.worldState = 7;
                break;
            case 7:
                if (g_GameProgressionState.loaded) {
                    InterfaceManager_EndInterface(loadingInterfaceID);
                }
                break;
            }
        } else if (g_GameProgressionState.worldState == 7 && InterfaceManager_GetTotalActiveInterfaces() == 0) {
            if (g_GameProgressionState.loaded) {
                FileStreaming_ReleaseDataPack(FileStreaming_GetPackID(GETP(GETP(level->pack)->name), 1, GETP(level->pack)->id), 1);
                g_GameProgressionState.loaded = 0;
            }
            DataPack_CloseFiles();
            StateManager_QueueChange(5);
        }
        break;
    }
    return 1;
#undef g_GameProgressionEndState
#undef g_GameProgressionState
}

s32 StateProgressionManager_Shutdown(void) {
    #define g_GameProgressionState (*(M2C_UNK *)(void *)&g_GameProgressionState)
    u32 var_r31;

    PlayerManager_Release();
    if ((s8) M2C_FIELD(&g_GameProgressionState, u8 *, 0x10) != 0) {
        FileStreaming_ReleaseDataPack(FileStreaming_GetPackID(PTR(M2C_FIELD(&g_GameProgressionState, s32 *, 0x1C)), 1, (s8) M2C_FIELD(&g_GameProgressionState, u8 *, 0x10)), 1);
        M2C_FIELD(&g_GameProgressionState, u8 *, 0x10) = 0U;
    }
    FileStreaming_ReleaseAllDataPacks(1, 1);
    if ((s32) M2C_FIELD(&g_GameProgressionState, s32 *, 0x14) != 7) {
        var_r31 = 0U;
        do {
            if (var_r31 != 3U) {
                FileStreaming_ReleaseAllDataPacks(1, 1);
            }
            var_r31 += 1;
        } while (var_r31 < 0xBU);
    }
    return 1;
#undef g_GameProgressionState
}

s32 ActionEndLevel(s32 *arg0)
{
    #define g_bRestartCurrentLevel (*(u8 *)(void *)&g_bRestartCurrentLevel)
    #define g_GameProgressionEndState ((s32 *)(void *)&g_GameProgressionEndState)

    if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
        ExitStateGamePlay(0xC);
    } else {
        g_bRestartCurrentLevel = 0;
        g_GameProgressionEndState[0] = arg0[0];
        g_GameProgressionEndState[1] = arg0[1];
        ExitStateGamePlay(0xB);
    }
    return 1;
#undef g_GameProgressionEndState
#undef g_bRestartCurrentLevel
}

s32 LoadWorldProcess(char *world, s8 loaded)
{
    static s32 newLoadingState;
    static s32 curLoadingState = 7;
    static u32 packID;
    static u32 iterationNum;
    static u32 quadrantNum;
    static LWCar *pPlayer1Car;
    static u32 totalQuads;
    static LWQuad *pQuadsToLoad[50];
    static s8 bLoadingItems;
    static s32 numAIToActivate;
    static u32 randOffset;
    extern s8 g_bLoadingDemoLand;
    LWItem *item;
    u32 id;
    char *name;
    LWCar *car;
    MCP *mcp;
    u32 cnt;
    u32 i;
    u32 j;
    LWQuad *quad;
    u32 max;
    void *dp;

    switch (newLoadingState)
    {
    case 0:
        if (newLoadingState != curLoadingState)
        {
            ShaderHolder_Init();
            *(u8 *)&g_worldData.unk8[3] = 1;
            packID = FileStreaming_LoadDataPack(world, 4, 0, loaded);
            curLoadingState = newLoadingState;
        }
        if (!IsLoadingThreadRunning())
        {
            g_worldData.unk8[0] = (s32)HND((dp = FileStreaming_InitializeDataPack(packID, 4, 0)));
            if (dp == NULL)
            {
                return 8;
            }
            if ((SETP(g_worldData.world, SLOT_LOAD(void *, dp))) == 0)
            {
                return 8;
            }
            newLoadingState = 1;
        }
        return newLoadingState;
    case 1:
        if (newLoadingState != curLoadingState)
        {
            packID = FileStreaming_LoadDataPack(GETP(((LWWorld *)GETP(g_worldData.world))->pack6Name), 6, ((LWWorld *)GETP(g_worldData.world))->pack6ID, loaded);
            curLoadingState = newLoadingState;
        }
        if (!IsLoadingThreadRunning())
        {
            g_worldData.unk8[2] = (s32)HND((dp = FileStreaming_InitializeDataPack(packID, 6, 0)));
            if (dp == NULL)
            {
                return 8;
            }
            newLoadingState = 2;
        }
        return newLoadingState;
    case 2:
        if (newLoadingState != curLoadingState)
        {
            packID = FileStreaming_LoadDataPack(GETP(((LWWorld *)GETP(g_worldData.world))->pack5Name), 5, ((LWWorld *)GETP(g_worldData.world))->pack5ID, loaded);
            curLoadingState = newLoadingState;
        }
        if (!IsLoadingThreadRunning())
        {
            g_worldData.unk8[1] = (s32)HND((dp = FileStreaming_InitializeDataPack(packID, 5, 0)));
            if (dp == NULL)
            {
                return 8;
            }
            if ((SLOT_LOAD(LWWorld *, dp))->quads.h != 0xD)  /* port: a format number, not a pointer */
            {
                return 8;
            }
            FileStreaming_InitializeAllQuads();
            newLoadingState = 4;
        }
        return newLoadingState;
    case 3:
        if (newLoadingState != curLoadingState)
        {
            curLoadingState = newLoadingState;
            FileStreaming_InitializeAllQuads();
            iterationNum = 0;
            totalQuads = 0;
            pPlayer1Car = NULL;
            for (i = 0; i < ((LWWorld *)GETP(g_worldData.world))->numCars; i++)
            {
                car = &GETP(((LWWorld *)GETP(g_worldData.world))->cars)[i];
                if (car->type == 1)
                {
                    pPlayer1Car = car;
                    break;
                }
            }
            if (FileStreaming_GetNumAvailableQuadPacks() >= ((LWWorld *)GETP(g_worldData.world))->numQuads)
            {
                for (i = 0; i < ((LWWorld *)GETP(g_worldData.world))->numQuads; i++)
                {
                    pQuadsToLoad[totalQuads++] = &GETP(((LWWorld *)GETP(g_worldData.world))->quads)[i];
                }
            }
            else
            {
                for (i = 0; i < ((LWWorld *)GETP(g_worldData.world))->numQuads; i++)
                {
                    if (GETP(GETP(((LWWorld *)GETP(g_worldData.world))->quads)[i].bsp) == NULL)
                    {
                        pQuadsToLoad[totalQuads++] = &GETP(((LWWorld *)GETP(g_worldData.world))->quads)[i];
                    }
                    else if (pPlayer1Car != NULL && BSPN0_CollideWithVector(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->quads)[i].bsp), pPlayer1Car))
                    {
                        quad = &GETP(((LWWorld *)GETP(g_worldData.world))->quads)[i];
                        pQuadsToLoad[totalQuads++] = quad;
                        for (j = 0; j < quad->numNeighbors; j++)
                        {
                            pQuadsToLoad[totalQuads++] = &GETP(((LWWorld *)GETP(g_worldData.world))->quads)[GETP(quad->neighbors)[j]];
                        }
                        break;
                    }
                }
            }
        }
        if (!IsLoadingThreadRunning())
        {
            if (iterationNum < totalQuads)
            {
                FileStreaming_LoadQuadPacks(&pQuadsToLoad[iterationNum++], 1, 1);
            }
            else
            {
                newLoadingState = 4;
            }
        }
        return newLoadingState;
    case 4:
        if (newLoadingState != curLoadingState)
        {
            curLoadingState = newLoadingState;
            iterationNum = 0;
            totalQuads = 0;
            packID = -1;
            if (((LWWorld *)GETP(g_worldData.world))->numCars == 0 || GETP(GETP(((LWWorld *)GETP(g_worldData.world))->cars)->script) != NULL)
            {
                return newLoadingState = 5;
            }
        }
        if (!IsLoadingThreadRunning())
        {
            name = NULL;
            switch (iterationNum)
            {
            case 0:
                mcp = GameSettings_GetMCP();
                if (mcp != NULL)
                {
                    name = GETP(mcp->sharePack);
                }
                break;
            case 1:
                return newLoadingState = 5;
            }
            if (name == NULL)
            {
                iterationNum++;
            }
            else if (packID == -1)
            {
                packID = FileStreaming_LoadDataPack(name, 8, 1, loaded);
            }
            else
            {
                FileStreaming_InitializeDataPack(packID, 8, 1);
                packID = -1;
                iterationNum++;
            }
        }
        return newLoadingState;
    case 5:
        if (newLoadingState != curLoadingState)
        {
            mcp = GameSettings_GetMCP();
            quadrantNum = 0;
            iterationNum = 0;
            bLoadingItems = 1;
            curLoadingState = newLoadingState;
            DataPack_RefreshPtrs((void *)PTR(g_worldData.unk8[0]));
            DataPack_RefreshPtrs((void *)PTR(g_worldData.unk8[2]));
            DataPack_RefreshPtrs((void *)PTR(g_worldData.unk8[1]));
            if (mcp != NULL && GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack) != NULL && mcp->unk0 == 0)
            {
                packID = FileStreaming_LoadDataPack(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack), 8, 3, 0);
            }
        }
        if (!IsLoadingThreadRunning())
        {
            if (quadrantNum < ((LWWorld *)GETP(g_worldData.world))->numQuads)
            {
                if (iterationNum < GETP(GETP(((LWWorld *)GETP(g_worldData.world))->quads)[quadrantNum].items)->count)
                {
                    if (bLoadingItems)
                    {
                        item = &GETP(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->quads)[quadrantNum].items)->items)[iterationNum++];
                        if (GETP(item->script) == NULL)
                        {
                            name = GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack);
                            id = item->id;
                            switch (GameSettings_GetCodeVariable(0x134, 0))
                            {
                            case 0:
                                item->id = 0xFFFF;
                                break;
                            case 1:
                                if (id == 0xFFFF)
                                {
                                    id = RandomNumberGenerator_GetRandomInteger(item->lo, item->hi + 1);
                                }
                                if (GameSettings_GetGameType() == 0 && id == 3 && GameSettings_IsCheatEnabled(8))
                                {
                                    id = 0x4D;
                                }
                                item->id = id;
                                FileStreaming_LoadDataPack(name, 2, id, loaded);
                                break;
                            case 2:
                                id = RandomNumberGenerator_GetRandomInteger(item->lo, item->hi + 1);
                                if (GameSettings_GetGameType() == 0 && id == 3 && GameSettings_IsCheatEnabled(8))
                                {
                                    id = 0x4D;
                                }
                                item->id = id;
                                FileStreaming_LoadDataPack(name, 2, id, loaded);
                                break;
                            }
                        }
                    }
                    else
                    {
                        item = &GETP(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->quads)[quadrantNum].items)->items)[iterationNum++];
                        if (GETP(item->script) == NULL && item->id != 0xFFFF)
                        {
                            id = item->id;
                            if ((dp = FileStreaming_InitializeDataPack(FileStreaming_GetPackID(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack), 2, id), 2, 1)) == NULL)
                            {
                                return 8;
                            }
                            SETP(item->script, DataPack_GetScript(dp, id));
                        }
                    }
                }
                else
                {
                    quadrantNum++;
                    iterationNum = 0;
                }
            }
            else if (bLoadingItems)
            {
                quadrantNum = 0;
                iterationNum = 0;
                bLoadingItems = 0;
            }
            else
            {
                for (i = 0; i < ((LWWorld *)GETP(g_worldData.world))->numCars; i++)
                {
                    car = &GETP(((LWWorld *)GETP(g_worldData.world))->cars)[i];
                    if (car->pack2 != 0xFF && GETP(car->script2) == NULL)
                    {
                        packID = FileStreaming_LoadDataPack(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack), 2, car->pack2, 0);
                        if ((dp = FileStreaming_InitializeDataPack(packID, 2, 1)) == NULL)
                        {
                            return 8;
                        }
                        SETP(car->script2, DataPack_GetScript(dp, car->pack2));
                    }
                    if (car->pack3 != 0xFF && GETP(car->script3) == NULL)
                    {
                        packID = FileStreaming_LoadDataPack(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack), 2, car->pack3, 0);
                        if ((dp = FileStreaming_InitializeDataPack(packID, 2, 1)) == NULL)
                        {
                            return 8;
                        }
                        SETP(car->script3, DataPack_GetScript(dp, car->pack3));
                    }
                    if (car->type <= 4)
                    {
                        id = GameSettings_GetCodeVariable(0xCC, car->type);
                        if (id != -1)
                        {
                            if (GameSettings_GetGameType() == 0 && id == 3 && GameSettings_IsCheatEnabled(8))
                            {
                                id = 0x4D;
                            }
                            FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->itemPack), 2, id, 0), 2, 1);
                        }
                    }
                }
                newLoadingState = 6;
            }
        }
        return newLoadingState;
    case 6:
        if (newLoadingState != curLoadingState)
        {
            mcp = GameSettings_GetMCP();
            iterationNum = 0;
            curLoadingState = newLoadingState;
            if ((s32)GameSettings_GetCodeVariable(0x12D, 0) != 0)
            {
                cnt = 0;
                if (GameSettings_GetCodeVariable(0x198, 0) != 0)
                {
                    j = 0;
                    for (car = GETP(((LWWorld *)GETP(g_worldData.world))->cars), i = 0; i < ((LWWorld *)GETP(g_worldData.world))->numCars; i++)
                    {
                        if (GETP(((LWWorld *)GETP(g_worldData.world))->cars)[i].type <= 4)
                        {
                            cnt++;
                        }
                    }
                    cnt = 1;
                    for (i = 0; i < ((LWWorld *)GETP(g_worldData.world))->numCars; i++)
                    {
                        if (GETP(((LWWorld *)GETP(g_worldData.world))->cars)[i].type <= 4)
                        {
                            if (++j > 1)
                            {
                                GETP(((LWWorld *)GETP(g_worldData.world))->cars)[i].type = 6;
                            }
                        }
                    }
                }
                else
                {
                    for (i = 1; i <= 4; i++)
                    {
                        if (GameSettings_GetCodeVariable(0xCB, i) != -1)
                        {
                            cnt++;
                        }
                    }
                }
                max = GameSettings_GetCodeVariable(0x133, 0) + 1;
                numAIToActivate = (cnt < max) ? max - cnt : 0;
            }
            else
            {
                numAIToActivate = 100000;
            }
            randOffset = RandomNumberGenerator_GetRandomInteger(0, 0x24);
            if (mcp != NULL && GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->aiPack) != NULL && mcp->unk0 == 0)
            {
                packID = FileStreaming_LoadDataPack(GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->aiPack), 8, 2, 0);
            }
        }
        if (!IsLoadingThreadRunning())
        {
            if (iterationNum < ((LWWorld *)GETP(g_worldData.world))->numCars)
            {
                car = &GETP(((LWWorld *)GETP(g_worldData.world))->cars)[iterationNum++];
                if (GETP(car->script) == NULL)
                {
                    name = GETP(GETP(((LWWorld *)GETP(g_worldData.world))->packs)->aiPack);
                    id = car->id;
                    if (car->type <= 4 && GameSettings_GetCodeVariable(0x198, 0) == 0)
                    {
                        if (!g_bLoadingDemoLand)
                        {
                            id = GameSettings_GetCodeVariable(0xCB, car->type);
                            if (id == -1)
                            {
                                return curLoadingState;
                            }
                        }
                    }
                    else if (id == -1 || GameSettings_GetCodeVariable(0x198, 0) != 0)
                    {
                        cnt = GameSettings_GetCodeVariable(0xCB, 1);
                        id = cnt + randOffset;
                        id = (id + iterationNum) % (car->hi + 1);
                        if (id == cnt)
                        {
                            id = (cnt + iterationNum) % (car->hi + 1);
                        }
                    }
                    if (car->type > 4)
                    {
                        if (--numAIToActivate < 0)
                        {
                            return curLoadingState;
                        }
                    }
                    dp = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(name, 0, id, 0), 0, 1);
                    car->id = id;
                    if (dp == NULL)
                    {
                        return 8;
                    }
                    SETP(car->script, DataPack_GetScript(dp, id));
                }
            }
            else
            {
                newLoadingState = 7;
            }
        }
        if (newLoadingState == 7)
        {
            SETP(((LWWorld *)GETP(g_worldData.world))->unk0, SLOT_LOAD(void *, g_worldData.unk8[1]));
            newLoadingState = 0;
            curLoadingState = 7;
            World_Init();
            World_Refresh();
        }
        return curLoadingState;
    }
    return 1;  /* asm */
}

