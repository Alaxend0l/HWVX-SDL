#include "common.h"
#include "CarA.h"


typedef struct GPAIScripts {
    u8 pad0[0x90];
    u32 scripts[4];         /* 0x90 one per difficulty */
} GPAIScripts;

typedef struct GPItem {
    u8 pad0[0x30];
    struct {
        u8 pad0[0x20];
        s32 count;          /* 0x20 */
    } *info;                /* 0x30 */
} GPItem;

/* A car placed in the world (World_GetTileCar), 0x60 bytes. */
typedef struct GPTileCar {
    Vector4 pos;            /* 0x00 */
    f32 rotX;               /* 0x10 */
    f32 rotY;               /* 0x14 */
    f32 rotZ;               /* 0x18 */
    u8 pad1C[8];
    PTR32(GPAIScripts) aiScripts; /* 0x24 */
    PTR32(CarA) creature;         /* 0x28 */
    u32 mode;               /* 0x2C */
    u32 vehicle;            /* 0x30 */
    u8 unk34;               /* 0x34 */
    u8 port;                /* 0x35 1-4: human player slot */
    u8 pad36[2];
    f32 nearDist;           /* 0x38 */
    f32 farDist;            /* 0x3C */
    u32 scripts[4];         /* 0x40 one per difficulty */
    u8 pad50[4];
    PTR32(GPItem) item;           /* 0x54 */
    u8 pad58[7];
    u8 unk5F;               /* 0x5F */
} GPTileCar;

typedef struct GPWorldInfo {
    u8 pad0[0x7C];
    u32 radarBlip;          /* 0x7C */
    u8 pad80[8];
    u32 lightTexture;       /* 0x88 */
    u8 pad8C[0x14];
    PTR32(void) vehiclePackList;  /* 0xA0 */
    PTR32(void) packList;         /* 0xA4 */
} GPWorldInfo;

typedef struct GPWorld {
    PTR32(u8) grid;               /* 0x00 */
    u8 pad4[4];
    u32 numTileCars;        /* 0x08 */
    PTR32(GPTileCar) tileCars;    /* 0x0C */
    u8 pad10[0x68];
    PTR32(void) nav;              /* 0x78 */
    u8 pad7C[0x20];
    PTR32(GPWorldInfo) info;      /* 0x9C */
} GPWorld;

typedef struct GPCreatureParams {
    Vector4 pos;            /* 0x00 */
    Vector4 quat;           /* 0x10 */
    Vector4 vel;            /* 0x20 */
    PTR32(GPAIScripts) aiScripts; /* 0x30 */
    u8 pad34[8];
    PTR32(void) player;           /* 0x3C */
} GPCreatureParams;

typedef struct GPAIPlayerDesc {
    u32 script;
    f32 nearSq;
    f32 farSq;
    u8 unkC;
    u8 unkD;
} GPAIPlayerDesc;

typedef struct GPSpeakerSound {
    u8 type;                /* 0x00 */
    u8 pad1[7];
    u8 unk8[4];             /* 0x08 */
    u8 unkC[8];             /* 0x0C */
    s32 unk14;              /* 0x14 */
    PTR32(u8) unk18;              /* 0x18 */
} GPSpeakerSound;

typedef struct GPTileSpeaker {
    Vector4 pos;            /* 0x00 */
    u8 pad10[4];
    PTR32(GPSpeakerSound) sound;  /* 0x14 */
    u8 pad18[4];
    u32 mode;               /* 0x1C */
    u8 pad20[0xC];
    u32 emitter;            /* 0x2C */
} GPTileSpeaker;

typedef struct GPTilePath {
    u8 pad0[4];
    u32 unk4;               /* 0x04 */
    f32 unk8;               /* 0x08 */
    u32 unkC;               /* 0x0C */
    PTR32(CarA) thing;            /* 0x10 */
    u32 unk14;              /* 0x14 */
    u8 pad18;
    u8 hasSpeed;            /* 0x19 */
    u8 pad1A[2];
    f32 speed;              /* 0x1C */
} GPTilePath;


void ExitStateGamePlay(s32 arg0) {
    extern s8 g_bExitStateGamePlay;
    extern s32 g_targetGameState;
    g_targetGameState = arg0;
    g_bExitStateGamePlay = 1;
}


extern u8 g_pauseState[];  /* typed per function below */

s32 StateGamePlay_Init(void) {
    extern s32 g_gamePlayCallback;
    extern u32 g_DemoModeCurrentPlayer;
    extern s8 g_bExitStateGamePlay;
    #define g_pauseState (*(u32 *)(void *)&g_pauseState)
    extern u32 g_DemoModeCounter;
    extern s32 g_DemoModeSwitchCameraTime;
    extern s32 g_DemoModeSwitchVehicleTime;
    extern Vector4 g_zeroVector;
    static s32 countAI;
    struct {
        Vector4 pos;
        Vector4 vel;
        Vector4 angVel;
        f32 scale[3];
        f32 unk3C;
        u32 unk40;
        u8 pad44[4];
        f32 speed;
        u32 unk4C;
        u32 unk50;
        f32 unk54;
        u8 pad58[4];
        u32 unk5C;
    } pathParams;
    GPCreatureParams params;
    Matrix4 mtx;
    struct {
        Vector4 pos;
        u32 emitter;
        u8 unk14;
    } speakerParams __attribute__((aligned(16)));
    u32 action[3];
    GPAIPlayerDesc desc;
    u32 speakerAction[3];
    u32 airAction[3];
    s32 viewOf[4];
    u32 viewIdx[4];
    Player *players[4];
    char name[8];
    s8 humanUsed[4] = {0, 0, 0, 0};
    s32 soundId;
    Player *aiPlayer;
    u32 i;

    g_gamePlayCallback = 0;
    g_DemoModeCurrentPlayer = 0;
    g_bExitStateGamePlay = 0;
    Geometry_Init(1);
    PlayerManager_Create();
    Gfx_SetEnviroMapMode(1);
    CameraManager_Init();
    ViewManager_Init();
    ShowStats_Init();
    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, HND(NULL), 1);
    ProcessManager_Add("ForceFeedback", ForceFeedback_Init, ForceFeedback_Heartbeat, HND(NULL), 1);
    ProcessManager_Add("Input", Input_Init, Input_Heartbeat, HND(NULL), 1);
    ProcessManager_Add("Player", PlayerManager_Init, PlayerManager_Heartbeat, HND(PlayerManager_Render_DEBUG), 0);
    ProcessManager_Add("Interface", InterfaceManager_Init, InterfaceManager_Heartbeat, HND(InterfaceManager_Render), 1);
    ProcessManager_Add("ThingManager", ThingManager_DARYL_Init, ThingManager_DARYL_Heartbeat, HND(ThingManager_DARYL_Render), 1);
    ProcessManager_Add("World", NULL, FileStreaming_HeartBeatItems, HND(World_Render), 0);
    ProcessManager_Add("Particle", ParticleManager_Init, ParticleManager_Heartbeat, HND(ParticleManager_Render), 0);
    ProcessManager_Add("Trail", Trail_Init, Trail_Heartbeat, HND(Trail_Render), 0);
    ProcessManager_Add("Light", LightHolder_Init, LightHolder_Heartbeat, HND(LightHolder_Render), 0);
    ProcessManager_Add("Flash", FlashHolder_Init, FlashHolder_Heartbeat, HND(FlashHolder_Render), 0);
    ProcessManager_Add("Corona", CoronaHolder_Init, CoronaHolder_Heartbeat, HND(CoronaHolder_Render), 0);
    ProcessManager_Add("Shader", NULL, ShaderHolder_Heartbeat, HND(NULL), 0);
    ProcessManager_Add("Line", LineManager_Init, LineManager_Heartbeat, HND(LineManager_Render), 0);
    ProcessManager_Add("SoundManager", NULL, SoundManager_Heartbeat, HND(NULL), 1);
    ProcessManager_Add("LensFlare", LensFlare_Init, NULL, HND(LensFlare_Render), 0);
    ProcessManager_Add("Game Rules", GameRules_Init, GameRules_HeartBeat, HND(NULL), 0);
    ProcessManager_Add("Triggers", NULL, Trigger_Heartbeat, HND(NULL), 1);
    ProcessManager_Add("Actions", NULL, Action_Heartbeat, HND(NULL), 0);
    ProcessManager_Add("Timers", NULL, TimerManager_Heartbeat, HND(NULL), 0);
    ProcessManager_Add("VertexColorSeq", NULL, VertexColorSeqManager_Heartbeat, HND(NULL), 0);
    ProcessManager_Add("BulletHoles", BulletHole_Init, BulletHole_Heartbeat, HND(BulletHole_Render), 0);
    ProcessManager_Add("Timer", NULL, Timer_Heartbeat, HND(NULL), 0);
    ProcessManager_Add("Lid Handler", LidHandler_Init, LidHandler_Heartbeat, HND(NULL), 1);
    Gfx_StateTextureBlendMode(2);
    Gfx_SetProjectionMatrix(90.0f);
    Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
    g_pauseState = 0;
    VertexColorSeqManager_Init();
    VolumeManager_Initialize();
    LightManager_Initialize();
    Grid_Init(GETP(((GPWorld *)GETP(g_worldData.world))->grid), GETP(((GPWorld *)GETP(g_worldData.world))->grid) + 0x10, 0x10, 0x10);
    NavHolder_Init(GETP(((GPWorld *)GETP(g_worldData.world))->nav));
    LightHolder_SetTexture(GETP(((GPWorld *)GETP(g_worldData.world))->info)->lightTexture);
    InterfaceManager_SetRadarBlip(GETP(((GPWorld *)GETP(g_worldData.world))->info)->radarBlip);
    GameSettings_RestoreGameData();
    ThingManager_DARYL_CreateThing(0xB, NULL);
    {
        GPTileCar *car;
        Player *player;

        i = 0;
        while ((car = World_GetTileCar(i)) != NULL) {
            if (GETP(car->aiScripts) != NULL && (car->port <= 4 || car->unk34 == 0 || car->unk5F != 0)) {
                params.pos = car->pos;
                params.pos.y += 10.0f;
                SETP(params.player, NULL);
                SETP(params.aiScripts, GETP(car->aiScripts));
                params.vel = g_zeroVector;
                if (car->port <= 4) {
                    car->unk34 = 0;
                }
                SetOrientation(&mtx, car->rotY, car->rotX, car->rotZ);
                SubMatrixToQuaternion(&mtx, &params.quat);
                if (car->port <= 4 && GameSettings_GetCodeVariable(0x198, 0) == 0) {
                    u32 script;

                    script = GameSettings_GetCodeVariable(0xCC, car->port);
                    if (car->port - 1 < 4) {
                        player = PlayerManager_NewHumanPlayer();
                        if (car->port == 1) {
                            Player_SetName(player, 1, "Player 1");
                        } else if (car->port == 2) {
                            Player_SetName(player, 2, "Player 2");
                        } else if (car->port == 3) {
                            Player_SetName(player, 3, "Player 3");
                        } else if (car->port == 4) {
                            Player_SetName(player, 4, "Player 4");
                        }
                        humanUsed[car->port - 1] = 1;
                        SETP(car->creature, ThingManager_DARYL_CreateThing(9, &params));
                        Player_SetThing(player, GETP(car->creature));
                        SETP(GETP(car->creature)->tileData, car);
                        SETP(player->tileCar, car);
                        if (GameSettings_GetGameType() == 0 && script == 3 && GameSettings_IsCheatEnabled(8)) {
                            script = 0x4D;
                        }
                        if (script != 0xFFFFFFFF) {
                            GPItem *item;

                            item = DataPack_GetScript(
                                FileStreaming_InitializeDataPack(
                                    FileStreaming_LoadDataPack(GETP(GETP(((GPWorld *)GETP(g_worldData.world))->info)->packList), 2, script, 0), 2, 1),
                                script);
                            if (item != NULL) {
                                void *rack = ThingManagerList_GetByType((u8 *)GETP(car->creature) + 0x344, 8);

                                WildCardRack_JettisonAll(rack);
                                WildCardRack_Add(rack, item, item->info->count);
                            }
                        }
                    }
                } else {
                    s32 difficulty;
                    u32 script;

                    difficulty = GameSettings_GetDifficulty();
                    script = 0;
                    if (difficulty == 0) {
                        if (car->scripts[0] != 0) {
                            script = car->scripts[0];
                        } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                            script = GETP(params.aiScripts)->scripts[0];
                        }
                    } else if (difficulty == 1) {
                        if (car->scripts[1] != 0) {
                            script = car->scripts[1];
                        } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                            script = GETP(params.aiScripts)->scripts[1];
                        }
                    } else if (difficulty == 2) {
                        if (car->scripts[2] != 0) {
                            script = car->scripts[2];
                        } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                            script = GETP(params.aiScripts)->scripts[2];
                        }
                    } else if (difficulty == 3) {
                        if (car->scripts[3] != 0) {
                            script = car->scripts[3];
                        } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                            script = GETP(params.aiScripts)->scripts[3];
                        }
                    }
                    if (script == 0) {
                        if (GETP(params.aiScripts)->scripts[0] != 0) {
                            script = GETP(params.aiScripts)->scripts[0];
                        } else if (GETP(params.aiScripts)->scripts[1] != 0) {
                            script = GETP(params.aiScripts)->scripts[1];
                        } else if (GETP(params.aiScripts)->scripts[2] != 0) {
                            script = GETP(params.aiScripts)->scripts[2];
                        } else {
                            script = GETP(params.aiScripts)->scripts[3];
                        }
                    }
                    desc.script = script;
                    desc.unkD = 0;
                    desc.unkC = car->unk34;
                    desc.nearSq = car->nearDist * car->nearDist;
                    desc.farSq = car->farDist * car->farDist;
                    aiPlayer = PlayerManager_NewAIPlayer(&desc);
                    if (aiPlayer == NULL) {
                        i++;
                        continue;
                    }
                    SETP(aiPlayer->aiScripts, GETP(params.aiScripts));
                    sprintf(name, "AI %d", countAI++);
                    Player_SetName(aiPlayer, -1, name);
                    SETP(car->creature, ThingManager_DARYL_CreateThing(9, &params));
                    Player_SetThing(aiPlayer, GETP(car->creature));
                    SETP(GETP(car->creature)->tileData, car);
                    SETP(aiPlayer->tileCar, car);
                    if (GETP(car->item) != NULL) {
                        void *rack = ThingManagerList_GetByType((u8 *)GETP(car->creature) + 0x344, 8);

                        WildCardRack_JettisonAll(rack);
                        WildCardRack_Add(rack, GETP(car->item), GETP(car->item)->info->count);
                    }
                }
                action[0] = car->mode;
                action[1] = 1;
                action[2] = (u32)HND(car);
                ActionVehicleChangeMode(action);
            }
            i++;
        }
    }
    {
        u32 j;
        GPTileSpeaker *speaker;
        s32 source;

        j = 0;
        while ((speaker = World_GetTileSpeaker(j)) != NULL) {
            source = SoundManager_CreateSource(1);
            soundId = GETP(speaker->sound)->unk14;
            SoundSource_Set(source, 8, &soundId);
            SoundSource_Set(source, 5, speaker);
            switch (GETP(speaker->sound)->type) {
            case 0:
                SoundSource_Set(source, 2, GETP(GETP(speaker->sound)->unk18));
                SoundSource_Set(source, 3, GETP(GETP(speaker->sound)->unk18) + 4);
                SoundSource_Set(source, 0, GETP(speaker->sound)->unk8);
                break;
            }
            SoundSource_Set(source, 1, GETP(speaker->sound)->unkC);
            SoundSource_Play(source);
            speakerAction[0] = speaker->mode;
            speakerAction[1] = 1;
            speakerAction[2] = (u32)HND(speaker);
            ActionSpeakerChangeMode(speakerAction);
            if (speaker->emitter != 0) {
                speakerParams.unk14 = 0;
                speakerParams.pos = speaker->pos;
                speakerParams.emitter = speaker->emitter;
                ThingManager_DARYL_CreateThing(0x10, &speakerParams);
            }
            j++;
        }
    }
    {
        u8 *light;

        i = 0;
        while ((light = World_GetTileLight(i)) != NULL) {
            LightHolder_Add(SLOT_LOAD(void *, (light + 0x10)), light, 0);
            i++;
        }
    }
    {
        GPTilePath *path;

        i = 0;
        while ((path = World_GetTilePath(i)) != NULL) {
            if (path->unk14 == 0) {
                i++;
                continue;
            }
            pathParams.unk4C = 0;
            pathParams.unk50 = 1;
            pathParams.pos.x = 0.0f;
            pathParams.pos.y = 0.0f;
            pathParams.pos.z = 0.0f;
            pathParams.unk40 = path->unkC;
            pathParams.speed = path->hasSpeed ? path->speed : 0.0f;
            pathParams.unk3C = 1.0f;
            pathParams.scale[0] = 0.0f;
            pathParams.scale[1] = 0.0f;
            pathParams.scale[2] = 0.0f;
            pathParams.angVel = g_zeroVector;
            pathParams.vel = g_zeroVector;
            pathParams.unk5C = path->unk4;
            pathParams.unk54 = path->unk8;
            SETP(path->thing, ThingManager_DARYL_CreateThing(0xA, &pathParams));
            SETP(GETP(path->thing)->tileData, path);
            i++;
        }
    }
    {
        u8 *volume;

        i = 0;
        while ((volume = World_GetVolume(i)) != NULL) {
            airAction[0] = *(u32 *)(volume + 4);
            airAction[1] = 1;
            airAction[2] = (u32)HND(volume);
            ActionAirboxChangeMode(airAction);
            i++;
        }
    }
    {
        u8 *nav = World_GetNavHolder();

        for (i = 0; i < *(u32 *)(nav + 4); i += 2) {
            airAction[0] = (SLOT_LOAD(u8 *, nav))[i * 0x60 + 0x50];
            airAction[1] = 1;
            airAction[2] = (u32)HND((SLOT_LOAD(u8 *, nav) + i * 0x60));
            ActionNavPointChangeMode(airAction);
        }
    }
    if (GameSettings_GetCodeVariable(0x198, 0) == 0) {
        u32 count;
        u32 k;
        s32 view;
        Player *player;
        void **camera;

        count = PlayerManager_PlayerHumanCount();
        view = 1;
        if (humanUsed[0]) {
            viewOf[0] = view++;
        }
        if (humanUsed[1]) {
            viewOf[1] = view++;
        }
        if (humanUsed[2]) {
            viewOf[2] = view++;
        }
        if (humanUsed[3]) {
            viewOf[3] = view++;
        }
        switch (count) {
        case 1:
            break;
        case 2:
            ViewManager_SetTwoPlayerHorizontal();
            break;
        case 3:
            ViewManager_SetThreePlayer();
            break;
        case 4:
            ViewManager_SetFourPlayer();
            break;
        }
        for (k = 0; k < count; k++) {
            player = PlayerManager_GetHumanPlayerByIndex(k);
            camera = CameraManager_GetNewCamera();
            player->view = viewOf[player->port - 1];
            Camera_SetPlayer(camera, player);
            if (player->view == 1) {
                if (count > 1) {
                    ViewManager_SetCamera(camera, 1);
                    ViewManager_EnableView(1, 1);
                }
            } else if (player->view == 2) {
                ViewManager_SetCamera(camera, 2);
                ViewManager_EnableView(2, 1);
            } else if (player->view == 3) {
                ViewManager_SetCamera(camera, 3);
                ViewManager_EnableView(3, 1);
            } else if (player->view == 4) {
                ViewManager_SetCamera(camera, 4);
                ViewManager_EnableView(4, 1);
            }
        }
    } else {
        u32 n;
        void *mcp;
        Player *player;
        void **camera;

        n = 0;
        mcp = GameSettings_GetMCP();
        for (i = 0; i < ((GPWorld *)GETP(g_worldData.world))->numTileCars; i++) {
            if (GETP(GETP(((GPWorld *)GETP(g_worldData.world))->tileCars)[i].aiScripts) != NULL &&
                GETP(((GPWorld *)GETP(g_worldData.world))->tileCars)[i].port <= 4 &&
                GETP(GETP(((GPWorld *)GETP(g_worldData.world))->tileCars)[i].creature) != NULL) {
                viewIdx[n] = n;
                players[n] = GETP(GETP(GETP(((GPWorld *)GETP(g_worldData.world))->tileCars)[i].creature)->player);
                n++;
            }
        }
        switch (n) {
        case 1:
            break;
        case 2:
            ViewManager_SetTwoPlayerHorizontal();
            break;
        case 3:
            ViewManager_SetThreePlayer();
            break;
        case 4:
            ViewManager_SetFourPlayer();
            break;
        }
        for (i = 0; i < n; i++) {
            player = players[i];
            camera = CameraManager_GetNewCamera();
            player->view = viewIdx[i] + 1;
            Camera_SetPlayer(camera, player);
            if (player->view == 1) {
                if (n > 1) {
                    ViewManager_SetCamera(camera, 1);
                    ViewManager_EnableView(1, 1);
                }
            } else if (player->view == 2) {
                ViewManager_SetCamera(camera, 2);
                ViewManager_EnableView(2, 1);
            } else if (player->view == 3) {
                ViewManager_SetCamera(camera, 3);
                ViewManager_EnableView(3, 1);
            } else if (player->view == 4) {
                ViewManager_SetCamera(camera, 4);
                ViewManager_EnableView(4, 1);
            }
        }
        for (i = 0; i < n; i++) {
            Player_RefreshInterface(players[i]);
        }
        if (mcp != NULL) {
            g_DemoModeCounter = *(u32 *)(SLOT_LOAD(u8 *, ((u8 *)mcp + 0x24)) + 4);
        } else {
            g_DemoModeCounter = 60;
        }
        g_DemoModeSwitchCameraTime = g_DemoModeCounter - 10000;
        g_DemoModeSwitchVehicleTime = g_DemoModeCounter - 8000;
    }
    World_InitTeleporters();
    SetPlayerControls();
    GameSettings_RestorePlayerStats();
    return 1;
#undef g_pauseState
}

void SetPlayerControls(void) {
    typedef struct ControlBinding {
        u8 unk0[0x8];
        s32 binding;
    } ControlBinding;
    typedef struct MCP {
        u8 unk0[0x3C];
        u32 numBindings;
        PTR32(ControlBinding **) bindings;
    } MCP;
    typedef struct HumanPlayer {
        u8 unk0[0x60];
        s32 controller;
        u8 unk64[0xC8 - 0x64];
        s32 chan;
    } HumanPlayer;
    ControllerSettings *settings;
    MCP *mcp;
    u32 i;
    u32 count;
    HumanPlayer *player;

    mcp = GameSettings_GetMCP();
    count = PlayerManager_PlayerHumanCount();
    for (i = 0; i < count; i++) {
        player = PlayerManager_GetHumanPlayerByIndex(i);
        settings = GameSettings_GetControllerSettings(player->controller);
        if (mcp != NULL && mcp->numBindings != 0) {
            if (settings->data[1] >= mcp->numBindings) {
                settings->data[1] = 0;
            }
            PlayerHuman_SetControlBinding(player, PTR((*GETP(mcp->bindings)[settings->data[1]])->binding));
        } else {
            PlayerHuman_SetDefaultControls(player, settings->data[1]);
        }
        Input_SetForceFeedbackActive(player->chan, settings->data[0]);
    }
}


s32 StateGamePlay_Heartbeat(void) {
    extern Vector4 g_lastCamPos;
    #define g_pauseState (*(u32 *)(void *)&g_pauseState)
    extern u32 g_StopInput;
    extern u8 g_bFreezeWorld;
    extern u32 g_DemoModeCounter;
    extern u32 g_iDeltaTimeMS;
    extern s32 g_DemoModeSwitchVehicleTime;
    extern u32 g_DemoModeCurrentPlayer;
    extern s32 g_DemoModeSwitchCameraTime;
    extern s8 g_bExitStateGamePlay;
    extern s32 g_targetGameState;
    extern s32 g_gamePlayCallback;
    extern s8 g_bRenderWorld;
    static s32 cameraIndex;
    static u32 pauseInterfaceID = 0xFFFFFFFF;
    static s8 bControllerRemoved;
    s8 removed;
    s32 input;
    u32 key;
    s8 pressed;
    u32 colour[2];

    pressed = 0;
    removed = 0;
    ShowStats_StartFrame();
    Gfx_SetCameraMatrix(&g_lastCamPos, NULL, NULL, NULL);
    ProcessManager_Heartbeat(g_pauseState);
    ViewManager_PrepForRender();
    Gfx_GetCameraPosition(&g_lastCamPos);
    if (GameSettings_GetCodeVariable(0x198, 0) == 0) {
        u32 code;
        {
            u32 i;
            u32 count;
            void *player;

            count = PlayerManager_PlayerHumanCount();
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetHumanPlayerByIndex(i);
                if (Input_KeyPress(M2C_FIELD(player, u32 *, 0xC8), 0)) {
                    input = M2C_FIELD(player, u32 *, 0xC8);
                    pressed = 1;
                    break;
                }
            }
        }
        if (GameSettings_GetCodeVariable(0x259, 0) == 0) {
            input = 0;
            pressed = 1;
        }
        if (g_StopInput != 0) {
            input = 0;
            pressed = 1;
        }
        if ((code = GameSettings_GetCodeVariable(0xD4, 0)) != 0) {
            input = code - 1;
            pressed = 1;
            removed = 1;
        }
        if (pressed && GameSettings_GetCodeVariable(0x19B, 0)) {
            if (g_pauseState == 0 && SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x9C)) + 0xCC) != NULL) {
                g_pauseState = 1;
                StateGamePlay_Pause(1);
                g_bFreezeWorld = 1;
                bControllerRemoved = removed;
                pauseInterfaceID = InterfaceManager_Create(SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x9C)) + 0xCC), 0, GamePlay_Interface);
                InterfaceManager_SetInput(pauseInterfaceID, input);
                GameSettings_SetCodeVariable(0xD5, 0, input + 1);
            } else if (SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x9C)) + 0xCC) != NULL && removed && !bControllerRemoved) {
                s32 oldInput = GameSettings_GetCodeVariable(0xD5, 0) - 1;
                bControllerRemoved = removed;
                InterfaceManager_ReleaseAllUnpausedInterfaces();
                pauseInterfaceID = InterfaceManager_Create(SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(GETP(g_worldData.world)) + 0x9C)) + 0xCC), 0, GamePlay_Interface);
                InterfaceManager_SetInput(pauseInterfaceID, oldInput);
            }
            Wheel_PlayAllAirborne();
        } else if (!removed) {
            bControllerRemoved = 0;
        }
        if (g_pauseState != 0) {
            if (removed) {
                InterfaceManager_SetInput(pauseInterfaceID, -2);
            } else {
                s32 oldInput = GameSettings_GetCodeVariable(0xD5, 0) - 1;
                InterfaceManager_SetInput(pauseInterfaceID, oldInput);
            }
        }
    } else {
        if (g_DemoModeCounter > g_iDeltaTimeMS) {
            g_DemoModeCounter -= g_iDeltaTimeMS;
        } else {
            g_DemoModeCounter = 0;
        }
        if (g_DemoModeCounter < g_DemoModeSwitchVehicleTime && CameraManager_GetActiveCameras() == 1) {
            void *player;
            void **camera;

            g_DemoModeSwitchVehicleTime = g_DemoModeCounter - 8000;
            if (g_DemoModeSwitchVehicleTime < 0) {
                g_DemoModeSwitchVehicleTime = 0;
            }
            g_DemoModeCurrentPlayer++;
            if (g_DemoModeCurrentPlayer >= PlayerManager_PlayerCount()) {
                g_DemoModeCurrentPlayer = 0;
            }
            player = PlayerManager_GetPlayerByIndex(g_DemoModeCurrentPlayer);
            camera = CameraManager_GetCameraByIndex(0);
            if (*camera != NULL && M2C_FIELD(*camera, u32 *, 0x68) < 0xFFFFFFFF) {
                InterfaceManager_ReleaseInterface(M2C_FIELD(*camera, u32 *, 0x68));
                M2C_FIELD(*camera, u32 *, 0x68) = 0xFFFFFFFF;
            }
            Camera_SetPlayer(camera, player);
        }
        if (g_DemoModeCounter < g_DemoModeSwitchCameraTime) {
            u32 i;
            u32 count;
            u8 *player;

            g_DemoModeSwitchCameraTime = g_DemoModeCounter - 10000;
            if (g_DemoModeSwitchCameraTime < 0) {
                g_DemoModeSwitchCameraTime = 0;
            }
            count = PlayerManager_PlayerCount();
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                player[0x31] = cameraIndex++;
            }
        }
        if (g_DemoModeCounter == 0) {
            g_targetGameState = 12;
            g_bExitStateGamePlay = 1;
        } else {
            for (key = 0; key < 20; key++) {
                if (Input_KeyPress(0xFFFFFFFF, key)) {
                    g_targetGameState = 12;
                    g_bExitStateGamePlay = 1;
                }
            }
        }
    }
    ShowStats_BeginScene();
    ProcessManager_Render(g_bRenderWorld);
    if (g_pauseState != 0) {
        colour[1] = 0;
        colour[0] = 0;
        Gfx_ScreenOverlay(colour, 0);
    }
    ShowStats_EndScene();
    if (g_bExitStateGamePlay && !IsLoadingThreadRunning()) {
        StateManager_QueueChange(g_targetGameState);
    }
    switch (g_gamePlayCallback) {
    case 0:
        break;
    case 1:
        g_targetGameState = 2;
        g_bExitStateGamePlay = 1;
        break;
    case 2:
        g_targetGameState = 5;
        g_bExitStateGamePlay = 1;
        break;
    case 3:
        g_gamePlayCallback = 0;
        g_pauseState = 0;
        StateGamePlay_Pause(0);
        if (pauseInterfaceID < 0xFFFFFFFF) {
            InterfaceManager_ReleaseInterface(pauseInterfaceID);
        }
        GameSettings_SetCodeVariable(0xD5, 0, 0);
        g_bFreezeWorld = 0;
        break;
    }
    return 1;
#undef g_pauseState
}

void StateGamePlay_Pause(s32 pause) {
    extern u8 g_bFreezeWorld;
    u8 bPause;

    CameraManager_Pause(pause);
    bPause = pause;
    SoundManager_Pause(bPause);
    Input_ForceFeedbackPause(bPause);
    InterfaceManager_PauseAllInterface(bPause);
    g_bFreezeWorld = 1;
}

s32 StateGamePlay_Shutdown(void) {
    extern s8 g_iTimeShift;
    extern u8 g_bFreezeWorld;
    extern void (*DestroyMusic)(void);

    Trigger_PauseTACSystem(1);
    while ((s8)IsLoadingThreadRunning()) {
    }
    g_iTimeShift = 0;
    Gfx_BeginScene();
    Gfx_EndScene();
    Gfx_FlipBuffers();
    Gfx_BeginScene();
    Gfx_EndScene();
    Gfx_FlipBuffers();
    NavHolder_Release();
    CameraManager_ReleaseAllEmitters();
    ThingManager_DARYL_Release();
    MatrixCache_Init();
    Grid_Release();
    DestroyMusic();
    SongManager_StopBKSong();
    MovieManager_Destroy();
    ShowStats_Release();
    PlayerManager_Release();
    Trigger_PauseTACSystem(0);
    World_Unload();
    ViewManager_SetSinglePlayer();
    GameSettings_SetCodeVariable(0x19B, 0, 1);
    g_bFreezeWorld = 0;
    Gfx_ClearScreen();
    return 1;
}

void GamePlay_Interface(void *msg) {
    extern struct {
        FPTR32(void, (void *msg)) handleInput;
        FPTR32(void, (void *msg)) update;
    } g_handleInterfaceGP;

    switch (M2C_FIELD(msg, u16 *, 4)) {
    case 3:
        GamePlay_Startup();
        if (GETP(g_handleInterfaceGP.handleInput) != NULL) {
            GETP(g_handleInterfaceGP.handleInput)(msg);
        }
        break;
    case 1:
        if (GETP(g_handleInterfaceGP.update) != NULL) {
            GETP(g_handleInterfaceGP.update)(msg);
        }
        break;
    case 2:
    default:
        if (GETP(g_handleInterfaceGP.handleInput) != NULL) {
            GETP(g_handleInterfaceGP.handleInput)(msg);
        }
        break;
    }
}

void GamePlay_Startup(void) {
    extern struct {
        FPTR32(void, (void *msg)) handleInput;
        FPTR32(void, (void)) update;
    } g_handleInterfaceGP;

    SETP(g_handleInterfaceGP.handleInput, GamePlay_PauseOptions_HandleInput);
    SETP(g_handleInterfaceGP.update, NULL);
}

void GamePlay_PauseOptions_HandleInput(void *msg) {
    typedef struct {
        u8 unk0[4];
        u16 command;
        u16 value;
        u8 unk8[0x18];
        s32 result;
    } InterfaceMsg;
    extern s8 g_bExitStateGamePlay;
    extern s32 g_targetGameState;
    extern s32 g_gamePlayCallback;
    static u16 endCommand;
    static s32 params[2];
    InterfaceMsg *m = (InterfaceMsg *)msg;

    switch (m->command) {
    case 3:
        endCommand = 0xFFFF;
        break;
    case 7:
        params[0] = 4;
        params[1] = m->value;
        m->result = 4;
        endCommand = 7;
        break;
    case 8:
        params[0] = 5;
        params[1] = m->value;
        m->result = 4;
        endCommand = 8;
        break;
    case 9:
        m->result = 4;
        endCommand = 9;
        break;
    case 10:
        m->result = 4;
        endCommand = 10;
        break;
    case 16:
        m->result = 4;
        endCommand = 16;
        break;
    case 5:
        switch (endCommand) {
        case 7:
        case 8:
            ActionEndLevel(params);
            break;
        case 9:
            g_targetGameState = 2;
            g_bExitStateGamePlay = 1;
            break;
        case 10:
            StateProgressionManager_RestartCurrentLevel();
            break;
        case 16:
            g_gamePlayCallback = 3;
            break;
        }
        break;
    }
}

s32 ActionPauseGame(u32 *action) {
    #define g_pauseState (*(s32 *)(void *)&g_pauseState)
    extern s32 g_gamePlayCallback;
    extern u8 g_bFreezeWorld;
    ControllerSettings *settings;
    u8 *mcp;
    u8 *player;
    u32 count;
    u32 i;

    if (*action == 7) {
        g_pauseState = 1;
        CameraManager_Pause(1);
        SoundManager_Pause(1);
        Input_ForceFeedbackPause(1);
        InterfaceManager_PauseAllInterface(1);
        g_bFreezeWorld = 1;
    } else if (*action == 8) {
        g_gamePlayCallback = 0;
        g_pauseState = 0;
        CameraManager_Pause(0);
        SoundManager_Pause(0);
        Input_ForceFeedbackPause(0);
        InterfaceManager_PauseAllInterface(0);
        g_bFreezeWorld = 1;
        mcp = GameSettings_GetMCP();
        count = PlayerManager_PlayerHumanCount();
        for (i = 0; i < count; i++) {
            player = PlayerManager_GetHumanPlayerByIndex(i);
            settings = GameSettings_GetControllerSettings(*(s32 *)(player + 0x60));
            if (mcp != NULL && *(u32 *)(mcp + 0x3C) != 0) {
                if (settings->data[1] >= *(u32 *)(mcp + 0x3C)) {
                    settings->data[1] = 0;
                }
                PlayerHuman_SetControlBinding(player, PTR(*(s32 *)PTR((SLOT_LOAD(u8 *, (*(u8 ***)(mcp + 0x40)))[settings->data[1]] + 0x8))));
            } else {
                PlayerHuman_SetDefaultControls(player, settings->data[1]);
            }
            Input_SetForceFeedbackActive(*(s32 *)(player + 0xC8), settings->data[0]);
        }
    }
    return 1;
#undef g_pauseState
}

s32 TriggerInterfacePackEnd(u32 *pack) {
    u32 i;
    u32 id;
    InterfacePackEntry *entry;

    i = 0;
    id = pack[0];
    for (entry = g_InterfacePackData; i < g_NumInterfacesPlaying; entry++, i++) {
        if (id == entry->id && strcmp(GETP(entry->name), (char *)(pack + 1)) == 0) {
            return 0;
        }
    }
    return 1;
}

s32 ActionPlayInterfacePack(u32 *pack) {
    void *data;
    u8 *script;
    u32 id;
    s32 result;

    id = pack[0];
    data = FileStreaming_InitializeDataPack(HND((void *)PTR(FileStreaming_GetPackID((char *)(pack + 1), 1, id))), 1, 1);
    if (data == NULL) {
        result = 0;
    } else {
        script = DataPack_GetScript(data, id);
        if (script == NULL) {
            result = 0;
        } else {
            SLOT_STORE(((u8 *)g_InterfacePackData + g_NumInterfacesPlaying * sizeof(InterfacePackEntry)), (char *)(pack + 1));
            g_InterfacePackData[g_NumInterfacesPlaying].id = id;
            g_InterfacePackData[g_NumInterfacesPlaying].unk8 =
                InterfaceManager_Create(script, 0, InterfaceDataPackRouter, g_InterfacePackData + g_NumInterfacesPlaying, pack + 1);
            g_NumInterfacesPlaying++;
            result = 1;
        }
    }
    return result;
}

s32 ActionEndInterfacePack(u32 *pack) {
    u32 i;

    for (i = 0; i < g_NumInterfacesPlaying; i++) {
        if (pack[0] == g_InterfacePackData[i].id && strcmp(GETP(g_InterfacePackData[i].name), (char *)(pack + 1)) == 0) {
            InterfaceManager_EndInterface(g_InterfacePackData[i].unk8);
            for (; i < g_NumInterfacesPlaying - 1; i++) {
                g_InterfacePackData[i] = g_InterfacePackData[i + 1];
            }
            g_NumInterfacesPlaying--;
            return 1;
        }
    }
    return 0;
}

void InterfaceHasEnded(u32 handle) {
    u32 i;

    for (i = 0; i < g_NumInterfacesPlaying; i++) {
        if (handle == g_InterfacePackData[i].unk8) {
            for (; i < g_NumInterfacesPlaying - 1; i++) {
                g_InterfacePackData[i] = g_InterfacePackData[i + 1];
            }
            g_NumInterfacesPlaying--;
        }
    }
}

void InterfaceDataPackRouter(void *arg0) {

    switch (M2C_FIELD(arg0, u16 *, 6)) {
    case 0:
        InterfaceManager_SetCallBackRoutine(M2C_FIELD(arg0, s32 *, 0), NULL);
        break;
    case 1:
        break;
    case 2:
        if (GameSettings_GetGameType() == 0) {
            GSI_VehicleItemSelection(arg0);
            InterfaceManager_SetCallBackRoutine(M2C_FIELD(arg0, s32 *, 0), GSI_VehicleItemSelection);
        } else if (GameSettings_GetGameType() == 1) {
            ArmageddonDropShip(arg0);
            InterfaceManager_SetCallBackRoutine(M2C_FIELD(arg0, s32 *, 0), ArmageddonDropShip);
        } else {
            InterfaceManager_SetCallBackRoutine(M2C_FIELD(arg0, s32 *, 0), NULL);
        }
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        break;
    default:
        InterfaceManager_SetCallBackRoutine(M2C_FIELD(arg0, s32 *, 0), NULL);
        break;
    }
}

void ArmageddonDropShip(void *arg0) {
    typedef struct DropShipData {
        s32 index;
        u16 type;
        u16 button;
    } DropShipData;
    typedef struct PackData {
        u32 start;
        u8 pad4[0x24];
        u32 size;
    } PackData;
    #define g_pauseState (*(u32 *)(void *)&g_pauseState)
    extern s8 g_bRenderWorld;
    extern u8 g_bFreezeWorld;
    static u32 vehicleSelectIndex;
    static u32 MaxVehicleIndex = 6;
    static f32 MAXTIMEINDEX = 7000.0f;
    static f32 minCount = -1.0f;
    static f32 maxCount = -1.0f;
    static s8 bReady;
    static s8 bWaitingForCamera;
    static u16 WaitingButton;
    static s8 bLeftMovement;
    static s8 bRightMovement;
    static Player *pPlayer;
    GPWorld *world;
    GPTileCar *entry;
    void *list;
    DropShipData *data = arg0;
    u32 i;
    f32 lr;

    world = (GPWorld *)GETP(g_worldData.world);
    entry = NULL;
    for (i = 0; i < world->numTileCars; i++) {
        if (GETP(world->tileCars)[i].port == 1) {
            entry = &GETP(world->tileCars)[i];
            break;
        }
    }
    if (data->type == 5) {
        g_pauseState = 0;
        g_bRenderWorld = 1;
        StateGamePlay_Pause(0);
    } else if (data->type == 4) {
        bReady = 1;
        DropShip_PullCameraForward(data->index);
        bWaitingForCamera = 1;
    } else if (data->type == 3) {
        Vector4 forward __attribute__((aligned(16)));
        Vector4 up __attribute__((aligned(16)));
        Vector4 right __attribute__((aligned(16)));
        u8 *camera;
        f32 count;
        u32 id;
        PackData *pack;

        list = GETP(GETP(world->info)->vehiclePackList);
        g_pauseState = 1;
        g_bRenderWorld = 0;
        StateGamePlay_Pause(1);
        SoundManager_Pause(0);
        InterfaceManager_PauseInterface(0, data->index);
        InterfaceManager_SetInfoCallBackRoutine(data->index, ArmageddonDropShipInfo);
        InterfaceInputAnimator_Initialize();
        pPlayer = GETP(entry->creature->player);
        forward.x = 0.0f;
        forward.y = 0.0f;
        forward.z = 1.0f;
        up.x = 0.0f;
        up.y = 1.0f;
        up.z = 0.0f;
        right.x = 1.0f;
        right.y = 0.0f;
        right.z = 0.0f;
        camera = CameraManager_GetCameraFromPlayer(pPlayer);
        *(Vector4 *)(camera + 0x80) = forward;
        *(Vector4 *)(camera + 0x90) = up;
        *(Vector4 *)(camera + 0xA0) = right;
        maxCount = -1.0f;
        minCount = -1.0f;
        bReady = 0;
        bWaitingForCamera = 0;
        bLeftMovement = 0;
        bRightMovement = 0;
        MAXTIMEINDEX = DropShip_GetLRTime(data->index);
        DropShip_StartChildInterface(data->index, -1);
        vehicleSelectIndex = entry->vehicle;
        if (vehicleSelectIndex > MaxVehicleIndex) {
            vehicleSelectIndex = 0;
        }
        count = (f32)vehicleSelectIndex / (f32)(MaxVehicleIndex + 1);
        count *= MAXTIMEINDEX;
        InterfaceInputAnimator_Count(0, 0, count);
        InterfaceInputAnimator_SetCounter(0, count);
        id = FileStreaming_GetPackID(list, 0, entry->vehicle);
        if (id != 0xFFFFFFFF) {
            pack = FileStreaming_InitializeDataPack(HND((void *)PTR(id)), 0, 1);
            if (pack != NULL) {
                ThingManager_Invalidate(pack->start, pack->start + pack->size);
                FileStreaming_ReleaseDataPack(id, 0);
            }
        }
    } else if (data->type == 12) {
    } else {
        switch (data->button) {
        case 8:
            g_bRenderWorld = 0;
            break;
        case 9:
            bWaitingForCamera = 0;
            data->type = 0x65;
            data->button = WaitingButton;
            ArmageddonDropShip(data);
            break;
        case 10:
            DropShip_StartChildInterface(data->index, vehicleSelectIndex + 1);
            bWaitingForCamera = 0;
            break;
        case 1:
            lr = InterfaceInputAnimator_GetLRCounter();
            if (bReady && !bWaitingForCamera && !bRightMovement) {
                maxCount = -1.0f;
                if (minCount == -1.0f || lr == minCount) {
                    if (DropShip_IsChildInterfaceReady(data->index)) {
                        if ((s32)data->type == 100) {
                            DropShip_PullCameraForward(data->index);
                            bWaitingForCamera = 1;
                            bLeftMovement = 0;
                            InterfaceInputAnimator_SetCallBack(NULL, NULL);
                        } else {
                            DropShip_EndChildInterface(data->index, vehicleSelectIndex + 1);
                            if (!DropShip_IsChildInterfaceReady(data->index)) {
                                InterfaceInputAnimator_SetCallBack(ArmageddonDropShip, (InterfaceCallBackData *)data);
                            } else {
                                InterfaceInputAnimator_SetCallBack(NULL, NULL);
                                if ((s32)data->type != 0x65) {
                                    DropShip_PullCameraBack(data->index);
                                    WaitingButton = 1;
                                    bWaitingForCamera = 1;
                                } else {
                                    if (vehicleSelectIndex == 0) {
                                        vehicleSelectIndex = MaxVehicleIndex;
                                        InterfaceInputAnimator_SetCounter(0, MAXTIMEINDEX);
                                        minCount = (f32)vehicleSelectIndex / (f32)(MaxVehicleIndex + 1);
                                    } else {
                                        vehicleSelectIndex--;
                                        minCount = (f32)vehicleSelectIndex / (f32)(MaxVehicleIndex + 1);
                                    }
                                    minCount *= MAXTIMEINDEX;
                                    InterfaceInputAnimator_Count(0, 1, minCount);
                                    bLeftMovement = 1;
                                    data->type = 100;
                                    InterfaceInputAnimator_SetCallBack(ArmageddonDropShip, (InterfaceCallBackData *)data);
                                }
                            }
                        }
                    }
                }
            }
            break;
        case 2:
            lr = InterfaceInputAnimator_GetLRCounter();
            if (bReady && !bWaitingForCamera && !bLeftMovement) {
                minCount = -1.0f;
                if (maxCount == -1.0f || lr == maxCount) {
                    if (DropShip_IsChildInterfaceReady(data->index)) {
                        if ((s32)data->type == 100) {
                            DropShip_PullCameraForward(data->index);
                            bWaitingForCamera = 1;
                            bRightMovement = 0;
                            InterfaceInputAnimator_SetCallBack(NULL, NULL);
                        } else {
                            DropShip_EndChildInterface(data->index, vehicleSelectIndex + 1);
                            if (!DropShip_IsChildInterfaceReady(data->index)) {
                                InterfaceInputAnimator_SetCallBack(ArmageddonDropShip, (InterfaceCallBackData *)data);
                            } else {
                                InterfaceInputAnimator_SetCallBack(NULL, NULL);
                                if ((s32)data->type != 0x65) {
                                    DropShip_PullCameraBack(data->index);
                                    WaitingButton = 2;
                                    bWaitingForCamera = 1;
                                } else {
                                    if (vehicleSelectIndex == MaxVehicleIndex) {
                                        vehicleSelectIndex = 0;
                                        maxCount = 1.0f;
                                    } else {
                                        if (vehicleSelectIndex == 0) {
                                            InterfaceInputAnimator_SetCounter(0, 0.0f);
                                        }
                                        vehicleSelectIndex++;
                                        maxCount = (f32)vehicleSelectIndex / (f32)(MaxVehicleIndex + 1);
                                    }
                                    maxCount *= MAXTIMEINDEX;
                                    InterfaceInputAnimator_Count(0, 0, maxCount);
                                    bRightMovement = 1;
                                    data->type = 100;
                                    InterfaceInputAnimator_SetCallBack(ArmageddonDropShip, (InterfaceCallBackData *)data);
                                }
                            }
                        }
                    }
                }
            }
            break;
        case 5:
            list = GETP(GETP(world->info)->vehiclePackList);
            if (bReady && !bWaitingForCamera) {
                lr = InterfaceInputAnimator_GetLRCounter();
                if ((maxCount == -1.0f || lr == maxCount) && (minCount == -1.0f || lr == minCount)) {
                    if (DropShip_IsChildInterfaceReady(data->index)) {
                        DropShip_EndChildInterface(data->index, vehicleSelectIndex + 1);
                        if (!DropShip_IsChildInterfaceReady(data->index)) {
                            InterfaceInputAnimator_SetCallBack(ArmageddonDropShip, (InterfaceCallBackData *)data);
                        } else {
                            InterfaceInputAnimator_SetCallBack(NULL, NULL);
                            if ((s32)data->type != 0x65) {
                                DropShip_PullCameraBack(data->index);
                                WaitingButton = 5;
                                bWaitingForCamera = 1;
                            } else {
                                GPCreatureParams params;
                                u32 hud[3];
                                void *dp;

                                InterfaceInputAnimator_Release();
                                dp = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack(list, 0, vehicleSelectIndex, 0), 0, 1);
                                params.pos = entry->pos;
                                params.pos.y += 10.0f;
                                SETP(params.player, pPlayer);
                                params.quat.w = 1.0f;
                                params.quat.z = 0.0f;
                                params.quat.y = 0.0f;
                                params.quat.x = 0.0f;
                                SETP(params.aiScripts, (GPAIScripts *)DataPack_GetScript(dp, vehicleSelectIndex));
                                params.vel.x = 0.0f;
                                params.vel.y = 0.0f;
                                params.vel.z = 0.0f;
                                SETP(entry->creature, ThingManager_DARYL_CreateThing(9, &params));
                                Player_SetThing(pPlayer, GETP(entry->creature));
                                hud[0] = 1;
                                hud[1] = 0;
                                hud[2] = 0;
                                ActionHUDChangeMode(hud);
                                entry->vehicle = vehicleSelectIndex;
                                DropShip_EndInterface(data->index);
                                ParticleManager_Init();
                            }
                        }
                    }
                }
            }
            break;
        }
    }
#undef g_pauseState
}

void ArmageddonDropShipInfo(void *info) {
    typedef struct DropShipInfo {
        u8 pad0[0x24];
        f32 lr;
        f32 ud;
    } DropShipInfo;
    DropShipInfo *ship = info;

    ship->lr = InterfaceInputAnimator_GetLRCounter();
    ship->ud = InterfaceInputAnimator_GetUDCounter();
}

void InterfaceInputAnimator_Initialize(void) {
    extern f32 g_LR_Counter;
    extern f32 g_UD_Counter;
    extern f32 g_LR_Max_Min;
    extern f32 g_UD_Max_Min;
    g_LR_Counter = 0.0f;
    g_UD_Counter = 0.0f;
    g_LR_Max_Min = 0.0f;
    g_UD_Max_Min = 0.0f;
    ProcessManager_Add("InterfaceInput", NULL, InterfaceInputAnimator_HeartBeat, HND(NULL), 1);
}

void InterfaceInputAnimator_Release(void) {
    static const char name[] = "InterfaceInput";
    ProcessManager_Release((char *)name);
}

void InterfaceInputAnimator_SetCounter(s32 which, f32 value) {
    extern f32 g_LR_Counter;
    extern f32 g_UD_Counter;
    switch (which) {
    case 0:
        g_LR_Counter = value;
        break;
    case 1:
        g_UD_Counter = value;
        break;
    }
}

void InterfaceInputAnimator_Count(s32 arg0, s32 arg1, f32 farg0) {
    extern s32 g_LR_Direction;
    extern f32 g_LR_Max_Min;
    extern s32 g_UD_Direction;
    extern f32 g_UD_Max_Min;
    switch (arg0) {                                 /* irregular */
    case 0:
        g_LR_Max_Min = farg0;
        g_LR_Direction = arg1;
        return;
    case 1:
        g_UD_Max_Min = farg0;
        g_UD_Direction = arg1;
        return;
    }
}

f32 InterfaceInputAnimator_GetLRCounter(void) {
    extern f32 g_LR_Counter;
    return g_LR_Counter;
}

f32 InterfaceInputAnimator_GetUDCounter(void) {
    extern f32 g_LR_Counter;
    return g_LR_Counter;
}

void InterfaceInputAnimator_SetCallBack(void *callBack, InterfaceCallBackData *data) {
    extern void *g_pCallBack;

    g_pCallBack = callBack;
    if (callBack != NULL) {
        g_CallBackData = *data;
    }
}

void InterfaceInputAnimator_HeartBeat(void) {
    extern f32 g_fDeltaTimeMS;
    extern s32 g_LR_Direction;
    extern s32 g_UD_Direction;
    extern f32 g_LR_Counter;
    extern f32 g_UD_Counter;
    extern f32 g_LR_Max_Min;
    extern f32 g_UD_Max_Min;
    extern void *g_pCallBack;
    f32 dt = g_fDeltaTimeMS;

    if (g_LR_Direction == 0) {
        g_LR_Counter += dt;
        if (g_LR_Counter > g_LR_Max_Min) {
            g_LR_Counter = g_LR_Max_Min;
        }
    } else {
        g_LR_Counter -= dt;
        if (g_LR_Counter < g_LR_Max_Min) {
            g_LR_Counter = g_LR_Max_Min;
        }
    }
    if (g_UD_Direction == 0) {
        g_UD_Counter += dt;
        if (g_UD_Counter > g_UD_Max_Min) {
            g_UD_Counter = g_UD_Max_Min;
        }
    } else {
        g_UD_Counter -= dt;
        if (g_UD_Counter < g_UD_Max_Min) {
            g_UD_Counter = g_UD_Max_Min;
        }
    }
    if (g_pCallBack != NULL) {
        ((void (*)(InterfaceCallBackData *))g_pCallBack)(&g_CallBackData);
    }
}

