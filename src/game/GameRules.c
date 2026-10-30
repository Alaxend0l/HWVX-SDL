#include "common.h"


extern s8 g_bRoundEnd;
extern u32 g_playerNumThatWon;


void GameRules_Init(void) {
    extern s8 g_bRoundEnd;
    extern u32 g_playerNumThatWon;
    extern s8 g_bWaitingForRoundReset;
    extern s8 g_bInit;
    extern s32 g_savedActions[];
    extern GRTrigger **g_pTimer;
    u32 j;
    GRZone *zone;
    u32 i;
    u8 *mcp;
    GRTrigger **slot;

    g_bRoundEnd = 0;
    g_playerNumThatWon = 1;
    g_bWaitingForRoundReset = 0;
    g_bInit = 1;
    GameSettings_SetCodeVariable(0x197, 0, 0);
    GameSettings_SetCodeVariable(0x190, 0, 1);
    for (i = 1; i < 5; i++) {
        GameSettings_SetCodeVariable(0x191, i, 0);
        GameSettings_SetCodeVariable(0x194, i, 0);
    }
    Trigger_SaveActions(g_savedActions, 100);
    GameSettings_SetCodeVariable(0x190, 0, 1);
    g_pTimer = NULL;
    switch (GameSettings_GetCodeVariable(0x12D, 0)) {
    case 1:
    case 2:
        for (i = 0; i < ((GRWorld *)GETP(g_worldData.world))->numTriggers; i++) {
            slot = &GETP(((GRWorld *)GETP(g_worldData.world))->triggers)[i];
            if ((*slot)->type == 0xC) {
                g_pTimer = slot;
                (*g_pTimer)->time = GameSettings_GetCodeVariable(0x12E, 0);
                if ((*g_pTimer)->time == 0.0f) {
                    g_pTimer = NULL;
                }
                break;
            }
        }
        break;
    case 3:
        mcp = GameSettings_GetMCP();
        if (mcp != NULL && GameSettings_GetCodeVariable(0x198, 0) != 0) {
            for (i = 0; i < ((GRWorld *)GETP(g_worldData.world))->numTriggers; i++) {
                slot = &GETP(((GRWorld *)GETP(g_worldData.world))->triggers)[i];
                if ((*slot)->type == 0xC) {
                    if ((*slot)->time == 0.0f) {
                        (*slot)->time = (SLOT_LOAD(u32 *, (mcp + 0x24)))[1];
                    }
                    break;
                }
            }
        }
        break;
    }
    for (i = 0; i < ((GRWorld *)GETP(g_worldData.world))->numZones; i++) {
        zone = &GETP(((GRWorld *)GETP(g_worldData.world))->zones)[i];
        for (j = 0; j < GETP(zone->sub)->numA; j++) {
            GETP(GETP(zone->sub)->a)[j].unk39 = GETP(GETP(zone->sub)->a)[j].unk34;
        }
        for (j = 0; j < GETP(zone->sub)->numC; j++) {
            GETP(GETP(zone->sub)->c)[j].unk39 = GETP(GETP(zone->sub)->c)[j].unk28;
            GETP(GETP(zone->sub)->c)[j].unk38 = 0;
        }
        for (j = 0; j < GETP(zone->sub)->numB; j++) {
            GETP(GETP(zone->sub)->b)[j].unk36 = GETP(GETP(zone->sub)->b)[j].unk30;
        }
    }
}

void GameRules_HeartBeat(void) {
    extern s8 g_bRoundEnd;
    extern u32 g_playerNumThatWon;
    extern s8 g_bWaitingForRoundReset;
    extern s8 g_bInit;
    extern GRTrigger **g_pTimer;
    static u32 s_requiredNumWins = -1;
    s8 useWins;
    u32 numPlayers;
    u32 i;
    u32 numRounds;
    s8 roundOver;
    u32 winner;
    u32 count;
    u8 *player;
    u32 wins;

    roundOver = 0;
    if (!g_bWaitingForRoundReset && GameSettings_GetCodeVariable(0x12C, 0) != 0) {
        winner = -1;
        if (g_bInit) {
            s8 used[4] = {0, 0, 0, 0};

            numPlayers = PlayerManager_PlayerHumanCount();
            for (i = 0; i < numPlayers; i++) {
                used[*(s32 *)((u8 *)PlayerManager_GetHumanPlayerByIndex(i) + 0x60) - 1] = 1;
            }
            numPlayers = PlayerManager_PlayerCount();
            for (i = 0; i < numPlayers; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                if (*(u32 *)(player + 0x60) > 4) {
                    s32 j;
                    for (j = 0; j < 4; j++) {
                        if (!used[j]) {
                            used[j] = 1;
                            *(u32 *)(player + 0x60) = j + 1;
                            break;
                        }
                    }
                }
            }
            g_bInit = 0;
        }
        if (g_bRoundEnd) {
            winner = g_playerNumThatWon;
            g_bRoundEnd = 0;
            roundOver = 1;
        } else if (g_pTimer != NULL && (*g_pTimer)->time == 0.0f) {
            roundOver = 1;
        } else {
            switch (GameSettings_GetCodeVariable(0x12D, 0)) {
            case 1:
                numRounds = GameSettings_GetCodeVariable(0x130, 0);
                numPlayers = PlayerManager_PlayerCount();
                for (i = 0; i < numPlayers; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if (*(u32 *)(player + 0x7C) >= numRounds) {
                        winner = *(u32 *)(player + 0x60);
                        roundOver = 1;
                        break;
                    }
                }
                break;
            case 2:
                numRounds = GameSettings_GetCodeVariable(0x12F, 0);
                numPlayers = PlayerManager_PlayerCount();
                for (i = 0; i < numPlayers; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if (*(u32 *)(player + 0x78) >= numRounds) {
                        winner = *(u32 *)(player + 0x60);
                        roundOver = 1;
                        break;
                    }
                    GameSettings_SetCodeVariable(0x193, *(u32 *)(player + 0x60), *(u32 *)(player + 0x78));
                }
                break;
            }
        }
        if (!roundOver) {
            return;
        }
        useWins = GameSettings_GetCodeVariable(0x132, 0) != 0;
        numRounds = GameSettings_GetCodeVariable(0x131, 0);
        roundOver = 0;
        count = PlayerManager_PlayerCount();
        if (winner == -1) {
            u32 max = 0;
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                if (*(u32 *)(player + 0x7C) > max) {
                    max = *(u32 *)(player + 0x7C);
                    winner = *(u32 *)(player + 0x60);
                }
            }
            switch (GameSettings_GetCodeVariable(0x12D, 0)) {
            case 1: {
                u32 max = 0;
                for (i = 0; i < count; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if (*(u32 *)(player + 0x7C) > max) {
                        max = *(u32 *)(player + 0x7C);
                        winner = *(u32 *)(player + 0x60);
                    }
                }
                break;
            }
            case 2: {
                u32 max = 0;
                for (i = 0; i < count; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if (*(u32 *)(player + 0x78) > max) {
                        max = *(u32 *)(player + 0x78);
                        winner = *(u32 *)(player + 0x60);
                    }
                }
                break;
            }
            case 3:
                winner = -1;
                break;
            }
        }
        for (i = 0; i < count; i++) {
            player = PlayerManager_GetPlayerByIndex(i);
            GameSettings_SetCodeVariable(0x192, *(u32 *)(player + 0x60), *(u32 *)(player + 0x7C));
            GameSettings_SetCodeVariable(0x193, *(u32 *)(player + 0x60), *(u32 *)(player + 0x78));
            if (*(u32 *)(player + 0x60) == winner) {
                GameSettings_SetCodeVariable(0x191, *(u32 *)(player + 0x60),
                                             GameSettings_GetCodeVariable(0x191, *(u32 *)(player + 0x60)) + 1);
                GameSettings_SetCodeVariable(0x194, *(u32 *)(player + 0x60), 1);
            } else {
                GameSettings_SetCodeVariable(0x194, *(u32 *)(player + 0x60), 0);
            }
        }
        if (useWins) {
            u32 max = 0;
            if (s_requiredNumWins == -1) {
                s_requiredNumWins = numRounds;
            }
            for (i = 0; i < count; i++) {
                wins = GameSettings_GetCodeVariable(0x191, *(u32 *)((u8 *)PlayerManager_GetPlayerByIndex(i) + 0x60));
                if (wins >= s_requiredNumWins) {
                    roundOver = 1;
                    s_requiredNumWins = -1;
                    break;
                }
                if (wins > max) {
                    max = wins;
                }
            }
        } else if (numRounds == GameSettings_GetCodeVariable(0x190, 0)) {
            roundOver = 1;
        }
        if (roundOver) {
            u32 max = 0;
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                wins = GameSettings_GetCodeVariable(0x191, *(u32 *)(player + 0x60));
                if (wins >= max) {
                    max = wins;
                }
                GameSettings_SetCodeVariable(0x194, *(u32 *)(player + 0x60), 0);
            }
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                wins = GameSettings_GetCodeVariable(0x191, *(u32 *)(player + 0x60));
                if (wins == max) {
                    GameSettings_SetCodeVariable(0x194, *(u32 *)(player + 0x60), 1);
                }
            }
            GameSettings_SetCodeVariable(0x197, 0, 1);
            Trigger_GameRoundsComplete();
            if (GameSettings_GetCodeVariable(0x198, 0) != 0) {
                StateManager_QueueChange(0xC);
            }
        } else {
            Trigger_GameRoundXComplete();
        }
        g_bWaitingForRoundReset = 1;
    }
}

void GameRules_ForceRoundEnd(u32 arg0) {
    g_bRoundEnd = 1;
    g_playerNumThatWon = arg0;
    if ((arg0 < 1U) && (arg0 > 4U)) {
        g_playerNumThatWon = -1U;
    }
}

void GameRules_SetPlayerOrder(u32 *order, u32 count) {
    u32 i;
    u32 numPlayers;
    u32 j;
    void *player;

    numPlayers = PlayerManager_PlayerCount();
    for (i = 0; i < numPlayers; i++) {
        player = PlayerManager_GetPlayerByIndex(i);
        for (j = 0; j < count; j++) {
            if (M2C_FIELD(player, u32 *, 0xA8) == order[j]) {
                M2C_FIELD(player, s32 *, 0x60) = j + 1;
                break;
            }
        }
    }
}

void GameRules_ResetRound(s8 resetMotion, s8 resetPosition, s8 jettison) {
    typedef struct SpawnPoint {
        Vector4 pos;
        f32 rotX;
        f32 rotY;
        f32 rotZ;
        u8 pad1C[0x24 - 0x1C];
        PTR32(u8) info;
        PTR32(u8) thing;
    } SpawnPoint;
    typedef struct CarParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        PTR32(u8) info;
        u8 pad34[0x3C - 0x34];
        PTR32(u8) player;
    } CarParams;
    extern s8 g_bWaitingForRoundReset;
    extern s32 g_savedActions[];
    extern GRTrigger **g_pTimer;
    extern u32 g_attackDelay;
    extern Vector4 g_zeroVector;
    u32 numPlayers;
    u32 i;
    SpawnPoint *spawn;
    SpawnPoint *sp;
    u8 *player;
    u8 *car;
    void *rack;
    CarParams params;
    Matrix4 mtx;

    GameSettings_SetCodeVariable(0x190, 0, GameSettings_GetCodeVariable(0x190, 0) + 1);
    g_bWaitingForRoundReset = 0;
    FileStreaming_ResetQuadResources();
    Trigger_RestoreActions(g_savedActions, 100);
    InterfaceManager_ReleaseAllInterfaces();
    if (g_pTimer != NULL) {
        (*g_pTimer)->time = GameSettings_GetCodeVariable(0x12E, 0);
    }
    if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
        g_attackDelay = 8000;
    }
    numPlayers = PlayerManager_PlayerCount();
    for (i = 0; i < numPlayers; i++) {
        player = PlayerManager_GetPlayerByIndex(i);
        GameSettings_SetCodeVariable(0x194, *(s32 *)(player + 0x60), 0);
        GameSettings_SetCodeVariable(0x193, *(s32 *)(player + 0x60), 0);
        *(s32 *)(player + 0x78) = 0;
        *(s32 *)(player + 0x7C) = 0;
        spawn = SLOT_LOAD(SpawnPoint *, (player + 0xA8));
        if (spawn != NULL) {
            if (SLOT_LOAD(u8 *, (player + 0x2C)) == NULL) {
                params.pos = spawn->pos;
                params.pos.y += 10.0f;
                SETP(params.player, player);
                SETP(params.info, GETP(spawn->info));
                params.vel = g_zeroVector;
                SetOrientation(&mtx, spawn->rotY, spawn->rotX, spawn->rotZ);
                SubMatrixToQuaternion(&mtx, &params.quat);
                SETP(spawn->thing, ThingManager_DARYL_CreateThing(9, &params));
                Player_SetThing(player, GETP(spawn->thing));
                SLOT_STORE((GETP(spawn->thing) + 0x90), SLOT_LOAD(u8 *, (player + 0xA8)));
            }
            sp = SLOT_LOAD(SpawnPoint *, (player + 0xA8));
            car = SLOT_LOAD(u8 *, (player + 0x2C));
            if (resetMotion) {
                *(u32 *)(car + 0x78) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)) + 0x14);
                *(u32 *)(car + 0x374) = 0;
                MotionState_ReleaseSlavesByType((s32)HND((car + 0x330)), 2);
            }
            if (resetPosition) {
                car = SLOT_LOAD(u8 *, (player + 0x2C));
                SetOrientation(car + 0x10, sp->rotY, sp->rotX, sp->rotZ);
                SubMatrixToQuaternion(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x10, (Vector4 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x50));
                *(f32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x40) = sp->pos.x;
                *(f32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x44) = 10.0f + sp->pos.y;
                *(f32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x48) = sp->pos.z;
                *(f32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x0) = 0.0f;
                *(f32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x4) = 0.0f;
                *(f32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x8) = 0.0f;
                *(Vector4 *)(car + 0x390) = g_zeroVector;
                *(Vector4 *)(car + 0x380) = g_zeroVector;
                *(Vector4 *)(car + 0x410) = g_zeroVector;
                *(f32 *)(car + 0x4E4) = 0.0f;
                *(f32 *)(car + 0x458) = 0.0f;
                *(u8 *)(car + 0x4E8) = 1;
                *(s32 *)(car + 0x3B0) = -1;
                *(s32 *)(car + 0x3B4) = 1;
                (*(void (**)(void *))(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (player + 0x2C)) + 0x8C)) + 0xC))(SLOT_LOAD(u8 *, (player + 0x2C)));
                *(u32 *)(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x74) |= 0x800;
            }
            if (jettison || GameSettings_GetCodeVariable(0x135, 0) != 0) {
                rack = ThingManagerList_GetByType(SLOT_LOAD(u8 *, (player + 0x2C)) + 0x344, 8);
                if (rack != NULL) {
                    WildCardRack_JettisonAll(rack);
                }
            }
            if (!Player_IsHuman(player)) {
                AI_Car_Reset(player);
            }
        }
    }
}

