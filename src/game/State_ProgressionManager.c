#include "common.h"
#include "World.h"

/* The world being loaded. */
#define WORLD() ((World *)GETP(g_worldData.world))

/* The progression script being played (g_GameProgressionState). */
typedef struct GameProgressionState {
    PTR32(ProgScript) script;   /* 0x00 NULL: the demo (DemoLand) */
    s32 phase;                  /* 0x04 0: the level's intro steps, 1: playing, 2: win/lose steps */
    s32 index;                  /* 0x08 level (StateProgressionManager_SetProgressionID's sub ID) */
    u32 step;                   /* 0x0C in the phase's sequence */
    s8 loaded;                  /* 0x10 the step's interface pack is loaded (its id) */
    u8 pad11[3];                /* 0x11 */
    s32 worldState;             /* 0x14 LoadWorldProcess state, 7: loaded */
    u8 pad18[4];                /* 0x18 */
    PTR32(char) packName;       /* 0x1C */
} GameProgressionState;
_Static_assert(sizeof(GameProgressionState) == 0x20, "GameProgressionState size");

extern GameProgressionState g_GameProgressionState;

void StateProgressionManager_SetProgressionID(s32 id, s32 subID) {
    extern s32 g_GameProgressionID;
    g_GameProgressionID = id;
    g_GameProgressionState.index = subID;
}

void StateProgressionManager_GetProgressionID(s32 *id, s32 *subID) {
    extern s32 g_GameProgressionID;
    *id = g_GameProgressionID;
    *subID = g_GameProgressionState.index;
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
    if (CAMERA_MANAGER->count == 0) {
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
        SETP(g_GameProgressionState.script, (ProgScript *)script);
        g_GameProgressionState.phase = 0;
        g_GameProgressionState.step = 0;
        g_GameProgressionState.loaded = 0;
        g_GameProgressionState.worldState = 0;
        if (g_GameProgressionState.index >= script[0]) {
            g_GameProgressionState.index = 0;
        }
        g_GameProgressionEndState[0] = 4;
        g_GameProgressionEndState[1] = 0;
        break;
    case 5:
        g_GameProgressionState.worldState = 0;
        if (GETP(g_GameProgressionState.script) == NULL) {
            return 1;
        }
        if (g_bRestartCurrentLevel) {
            g_GameProgressionState.phase = 0;
            g_GameProgressionState.step = 0;
        } else {
            g_GameProgressionState.step = 0;
            g_GameProgressionState.phase = 2;
        }
        break;
    case 3:
        if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
            StateManager_QueueChange(2);
            return 1;
        }
        if (GETP(g_GameProgressionState.script) == NULL) {
            return 0;
        }
        switch (g_GameProgressionState.phase) {
        case 0:
        case 2:
            g_GameProgressionState.step++;
            break;
        case 1:
            break;
        }
        break;
    case 12:
        g_GameProgressionState.phase = 1;
        g_GameProgressionState.worldState = 0;
        break;
    default:
        return 0;
    }
    return 1;
#undef g_GameProgressionEndState
#undef g_bRestartCurrentLevel
}

s32 StateProgressionManager_Heartbeat(void) {
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
}

s32 StateProgressionManager_Shutdown(void) {
    u32 var_r31;

    PlayerManager_Release();
    if (g_GameProgressionState.loaded != 0) {
        FileStreaming_ReleaseDataPack(FileStreaming_GetPackID(GETP(g_GameProgressionState.packName), 1, (s8)g_GameProgressionState.loaded), 1);
        g_GameProgressionState.loaded = 0;
    }
    FileStreaming_ReleaseAllDataPacks(1, 1);
    if (g_GameProgressionState.worldState != 7) {
        var_r31 = 0U;
        do {
            if (var_r31 != 3U) {
                FileStreaming_ReleaseAllDataPacks(1, 1);
            }
            var_r31 += 1;
        } while (var_r31 < 0xBU);
    }
    return 1;
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
    static TileCar *pPlayer1Car;
    static u32 totalQuads;
    static WorldQuad *pQuadsToLoad[50];
    static s8 bLoadingItems;
    static s32 numAIToActivate;
    static u32 randOffset;
    extern s8 g_bLoadingDemoLand;
    QuadItem *item;
    u32 id;
    char *name;
    TileCar *car;
    MCP *mcp;
    u32 cnt;
    u32 i;
    u32 j;
    WorldQuad *quad;
    u32 max;
    void *dp;

    switch (newLoadingState)
    {
    case 0:
        if (newLoadingState != curLoadingState)
        {
            ShaderHolder_Init();
            g_worldData.resetQuads = 1;
            packID = FileStreaming_LoadDataPack(world, 4, 0, loaded);
            curLoadingState = newLoadingState;
        }
        if (!IsLoadingThreadRunning())
        {
            g_worldData.worldPack = (s32)HND((dp = FileStreaming_InitializeDataPack(packID, 4, 0)));
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
            packID = FileStreaming_LoadDataPack(GETP(WORLD()->quadPackName), 6, WORLD()->quadPackID, loaded);
            curLoadingState = newLoadingState;
        }
        if (!IsLoadingThreadRunning())
        {
            g_worldData.quadPack = (s32)HND((dp = FileStreaming_InitializeDataPack(packID, 6, 0)));
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
            packID = FileStreaming_LoadDataPack(GETP(WORLD()->collisionPackName), 5, WORLD()->collisionPackID, loaded);
            curLoadingState = newLoadingState;
        }
        if (!IsLoadingThreadRunning())
        {
            g_worldData.collisionPack = (s32)HND((dp = FileStreaming_InitializeDataPack(packID, 5, 0)));
            if (dp == NULL)
            {
                return 8;
            }
            if (((WorldCollision *)SLOT_LOAD(void *, dp))->version != 0xD)
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
            for (i = 0; i < WORLD()->numTileCars; i++)
            {
                car = &GETP(WORLD()->tileCars)[i];
                if (car->port == 1)
                {
                    pPlayer1Car = car;
                    break;
                }
            }
            if (FileStreaming_GetNumAvailableQuadPacks() >= WORLD()->numQuads)
            {
                for (i = 0; i < WORLD()->numQuads; i++)
                {
                    pQuadsToLoad[totalQuads++] = &GETP(WORLD()->quads)[i];
                }
            }
            else
            {
                for (i = 0; i < WORLD()->numQuads; i++)
                {
                    if (GETP(GETP(WORLD()->quads)[i].bsp) == NULL)
                    {
                        pQuadsToLoad[totalQuads++] = &GETP(WORLD()->quads)[i];
                    }
                    else if (pPlayer1Car != NULL && BSPN0_CollideWithVector(GETP(GETP(WORLD()->quads)[i].bsp), pPlayer1Car))
                    {
                        quad = &GETP(WORLD()->quads)[i];
                        pQuadsToLoad[totalQuads++] = quad;
                        for (j = 0; j < quad->numNeighbors; j++)
                        {
                            pQuadsToLoad[totalQuads++] = &GETP(WORLD()->quads)[GETP(quad->neighbors)[j]];
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
            if (WORLD()->numTileCars == 0 || GETP(GETP(WORLD()->tileCars)->info) != NULL)
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
            DataPack_RefreshPtrs((void *)PTR(g_worldData.worldPack));
            DataPack_RefreshPtrs((void *)PTR(g_worldData.quadPack));
            DataPack_RefreshPtrs((void *)PTR(g_worldData.collisionPack));
            if (mcp != NULL && GETP(GETP(WORLD()->gameData)->itemPack) != NULL && mcp->unk0 == 0)
            {
                packID = FileStreaming_LoadDataPack(GETP(GETP(WORLD()->gameData)->itemPack), 8, 3, 0);
            }
        }
        if (!IsLoadingThreadRunning())
        {
            if (quadrantNum < WORLD()->numQuads)
            {
                if (iterationNum < GETP(GETP(WORLD()->quads)[quadrantNum].contents)->numItems)
                {
                    if (bLoadingItems)
                    {
                        item = &GETP(GETP(GETP(WORLD()->quads)[quadrantNum].contents)->items)[iterationNum++];
                        if (GETP(item->record) == NULL)
                        {
                            name = GETP(GETP(WORLD()->gameData)->itemPack);
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
                        item = &GETP(GETP(GETP(WORLD()->quads)[quadrantNum].contents)->items)[iterationNum++];
                        if (GETP(item->record) == NULL && item->id != 0xFFFF)
                        {
                            id = item->id;
                            if ((dp = FileStreaming_InitializeDataPack(FileStreaming_GetPackID(GETP(GETP(WORLD()->gameData)->itemPack), 2, id), 2, 1)) == NULL)
                            {
                                return 8;
                            }
                            SETP(item->record, DataPack_GetScript(dp, id));
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
                for (i = 0; i < WORLD()->numTileCars; i++)
                {
                    car = &GETP(WORLD()->tileCars)[i];
                    if (car->item2ID != 0xFF && GETP(car->item2) == NULL)
                    {
                        packID = FileStreaming_LoadDataPack(GETP(GETP(WORLD()->gameData)->itemPack), 2, car->item2ID, 0);
                        if ((dp = FileStreaming_InitializeDataPack(packID, 2, 1)) == NULL)
                        {
                            return 8;
                        }
                        SETP(car->item2, DataPack_GetScript(dp, car->item2ID));
                    }
                    if (car->itemID != 0xFF && GETP(car->item) == NULL)
                    {
                        packID = FileStreaming_LoadDataPack(GETP(GETP(WORLD()->gameData)->itemPack), 2, car->itemID, 0);
                        if ((dp = FileStreaming_InitializeDataPack(packID, 2, 1)) == NULL)
                        {
                            return 8;
                        }
                        SETP(car->item, DataPack_GetScript(dp, car->itemID));
                    }
                    if (car->port <= 4)
                    {
                        id = GameSettings_GetCodeVariable(0xCC, car->port);
                        if (id != -1)
                        {
                            if (GameSettings_GetGameType() == 0 && id == 3 && GameSettings_IsCheatEnabled(8))
                            {
                                id = 0x4D;
                            }
                            FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(GETP(GETP(WORLD()->gameData)->itemPack), 2, id, 0), 2, 1);
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
                    for (car = GETP(WORLD()->tileCars), i = 0; i < WORLD()->numTileCars; i++)
                    {
                        if (GETP(WORLD()->tileCars)[i].port <= 4)
                        {
                            cnt++;
                        }
                    }
                    cnt = 1;
                    for (i = 0; i < WORLD()->numTileCars; i++)
                    {
                        if (GETP(WORLD()->tileCars)[i].port <= 4)
                        {
                            if (++j > 1)
                            {
                                GETP(WORLD()->tileCars)[i].port = 6;
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
            if (mcp != NULL && GETP(GETP(WORLD()->gameData)->vehiclePackList) != NULL && mcp->unk0 == 0)
            {
                packID = FileStreaming_LoadDataPack(GETP(GETP(WORLD()->gameData)->vehiclePackList), 8, 2, 0);
            }
        }
        if (!IsLoadingThreadRunning())
        {
            if (iterationNum < WORLD()->numTileCars)
            {
                car = &GETP(WORLD()->tileCars)[iterationNum++];
                if (GETP(car->info) == NULL)
                {
                    name = GETP(GETP(WORLD()->gameData)->vehiclePackList);
                    id = car->vehicle;
                    if (car->port <= 4 && GameSettings_GetCodeVariable(0x198, 0) == 0)
                    {
                        if (!g_bLoadingDemoLand)
                        {
                            id = GameSettings_GetCodeVariable(0xCB, car->port);
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
                        id = (id + iterationNum) % (car->maxVehicle + 1);
                        if (id == cnt)
                        {
                            id = (cnt + iterationNum) % (car->maxVehicle + 1);
                        }
                    }
                    if (car->port > 4)
                    {
                        if (--numAIToActivate < 0)
                        {
                            return curLoadingState;
                        }
                    }
                    dp = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(name, 0, id, 0), 0, 1);
                    car->vehicle = id;
                    if (dp == NULL)
                    {
                        return 8;
                    }
                    SETP(car->info, DataPack_GetScript(dp, id));
                }
            }
            else
            {
                newLoadingState = 7;
            }
        }
        if (newLoadingState == 7)
        {
            SETP(WORLD()->collision, SLOT_LOAD(void *, g_worldData.collisionPack));
            newLoadingState = 0;
            curLoadingState = 7;
            World_Init();
            World_Refresh();
        }
        return curLoadingState;
    }
    return 1;  /* asm */
}

