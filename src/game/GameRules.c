/* Game rules of the multi-round modes: round timer, winners and round
 * resets. The mode and its limits are game settings code variables:
 *   0x12C rules on, 0x12D mode (1: points, 2: kills, 3+: no target),
 *   0x12E round time, 0x12F kills / 0x130 points to win, 0x131 rounds,
 *   0x132 count wins, 0x135 jettison wildcards on reset, 0x198 (MCP game);
 *   per player 0x191 wins, 0x192 points, 0x193 kills, 0x194 won the round;
 *   0x190 current round, 0x197 all rounds complete. */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "ThingManager.h"

extern s8 g_bRoundEnd;
extern u32 g_playerNumThatWon;

/* The world timer used as the round timer (type 0xC), or NULL. */
extern WorldTimerSlot *g_pTimer;

/* Starts a series of rounds: resets the round state, saves the trigger
 * action counts for round resets, finds the round timer and records each
 * quadrant object's mode as its round-start mode. */
void GameRules_Init(void) {
    extern s8 g_bWaitingForRoundReset;
    extern s8 g_bInit;
    extern s32 g_savedActions[];
    u32 j;
    QuadContents *contents;
    u32 i;
    MCP *mcp;
    WorldTimerSlot *slot;

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
        for (i = 0; i < WORLD()->numTimers; i++) {
            slot = &GETP(WORLD()->timers)[i];
            if (GETP(*slot)->type == 0xC) {
                g_pTimer = slot;
                GETP(*g_pTimer)->time = GameSettings_GetCodeVariable(0x12E, 0);
                if (GETP(*g_pTimer)->time == 0.0f) {
                    g_pTimer = NULL;
                }
                break;
            }
        }
        break;
    case 3:
        mcp = GameSettings_GetMCP();
        if (mcp != NULL && GameSettings_GetCodeVariable(0x198, 0) != 0) {
            for (i = 0; i < WORLD()->numTimers; i++) {
                slot = &GETP(WORLD()->timers)[i];
                if (GETP(*slot)->type == 0xC) {
                    if (GETP(*slot)->time == 0.0f) {
                        GETP(*slot)->time = GETP(mcp->demo)->time;
                    }
                    break;
                }
            }
        }
        break;
    }
    for (i = 0; i < WORLD()->numQuads; i++) {
        contents = GETP(GETP(WORLD()->quads)[i].contents);
        for (j = 0; j < contents->numFxPoints; j++) {
            GETP(contents->fxPoints)[j].defMode = GETP(contents->fxPoints)[j].mode;
        }
        for (j = 0; j < contents->numItems; j++) {
            GETP(contents->items)[j].defMode = GETP(contents->items)[j].mode;
            GETP(contents->items)[j].spawned = 0;
        }
        for (j = 0; j < contents->numInstances; j++) {
            GETP(contents->instances)[j].defMode = GETP(contents->instances)[j].mode;
        }
    }
}

/* Ends the round when a player reached the target, the timer ran out or a
 * script forced it; scores it (wins, per-player results) and fires the
 * GameRoundXComplete / GameRoundsComplete triggers. Waits for
 * GameRules_ResetRound before checking again. */
void GameRules_HeartBeat(void) {
    extern s8 g_bWaitingForRoundReset;
    extern s8 g_bInit;
    static u32 s_requiredNumWins = -1;
    s8 useWins;
    u32 numPlayers;
    u32 i;
    u32 numRounds;
    s8 roundOver;
    u32 winner;
    u32 count;
    Player *player;
    u32 wins;

    roundOver = 0;
    if (!g_bWaitingForRoundReset && GameSettings_GetCodeVariable(0x12C, 0) != 0) {
        winner = -1;
        if (g_bInit) {
            /* give the players without a controller port (AI) a free one */
            s8 used[4] = {0, 0, 0, 0};

            numPlayers = PlayerManager_PlayerHumanCount();
            for (i = 0; i < numPlayers; i++) {
                used[(s32)((Player *)PlayerManager_GetHumanPlayerByIndex(i))->port - 1] = 1;
            }
            numPlayers = PlayerManager_PlayerCount();
            for (i = 0; i < numPlayers; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                if (player->port > 4) {
                    s32 j;
                    for (j = 0; j < 4; j++) {
                        if (!used[j]) {
                            used[j] = 1;
                            player->port = j + 1;
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
        } else if (g_pTimer != NULL && GETP(*g_pTimer)->time == 0.0f) {
            roundOver = 1;
        } else {
            switch (GameSettings_GetCodeVariable(0x12D, 0)) {
            case 1:
                numRounds = GameSettings_GetCodeVariable(0x130, 0);
                numPlayers = PlayerManager_PlayerCount();
                for (i = 0; i < numPlayers; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if (player->stuntPoints >= numRounds) {
                        winner = player->port;
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
                    if ((u32)player->kills >= numRounds) {
                        winner = player->port;
                        roundOver = 1;
                        break;
                    }
                    GameSettings_SetCodeVariable(0x193, player->port, player->kills);
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
                if (player->stuntPoints > max) {
                    max = player->stuntPoints;
                    winner = player->port;
                }
            }
            switch (GameSettings_GetCodeVariable(0x12D, 0)) {
            case 1: {
                u32 max = 0;
                for (i = 0; i < count; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if (player->stuntPoints > max) {
                        max = player->stuntPoints;
                        winner = player->port;
                    }
                }
                break;
            }
            case 2: {
                u32 max = 0;
                for (i = 0; i < count; i++) {
                    player = PlayerManager_GetPlayerByIndex(i);
                    if ((u32)player->kills > max) {
                        max = player->kills;
                        winner = player->port;
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
            GameSettings_SetCodeVariable(0x192, player->port, player->stuntPoints);
            GameSettings_SetCodeVariable(0x193, player->port, player->kills);
            if (player->port == winner) {
                GameSettings_SetCodeVariable(0x191, player->port,
                                             GameSettings_GetCodeVariable(0x191, player->port) + 1);
                GameSettings_SetCodeVariable(0x194, player->port, 1);
            } else {
                GameSettings_SetCodeVariable(0x194, player->port, 0);
            }
        }
        if (useWins) {
            u32 max = 0;
            if (s_requiredNumWins == -1) {
                s_requiredNumWins = numRounds;
            }
            for (i = 0; i < count; i++) {
                wins = GameSettings_GetCodeVariable(0x191, ((Player *)PlayerManager_GetPlayerByIndex(i))->port);
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
            /* the series is over: the players with the most wins won */
            u32 max = 0;
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                wins = GameSettings_GetCodeVariable(0x191, player->port);
                if (wins >= max) {
                    max = wins;
                }
                GameSettings_SetCodeVariable(0x194, player->port, 0);
            }
            for (i = 0; i < count; i++) {
                player = PlayerManager_GetPlayerByIndex(i);
                wins = GameSettings_GetCodeVariable(0x191, player->port);
                if (wins == max) {
                    GameSettings_SetCodeVariable(0x194, player->port, 1);
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

/* Ends the round with player `arg0` (1-4) as the winner. */
void GameRules_ForceRoundEnd(u32 arg0) {
    g_bRoundEnd = 1;
    g_playerNumThatWon = arg0;
    if ((arg0 < 1U) && (arg0 > 4U)) {   /* as in the original: never true */
        g_playerNumThatWon = -1U;
    }
}

/* Renumbers the players' ports by the order of their tile cars in `order`. */
void GameRules_SetPlayerOrder(u32 *order, u32 count) {
    u32 i;
    u32 numPlayers;
    u32 j;
    Player *player;

    numPlayers = PlayerManager_PlayerCount();
    for (i = 0; i < numPlayers; i++) {
        player = PlayerManager_GetPlayerByIndex(i);
        for (j = 0; j < count; j++) {
            if (player->tileCar.h == order[j]) {
                player->port = j + 1;
                break;
            }
        }
    }
}

/* Starts the next round: restores the quadrants and trigger actions, the
 * round timer and the players' scores; respawns missing cars at their tile,
 * and optionally repairs them, puts them back on their tile and jettisons
 * their wildcards. */
void GameRules_ResetRound(s8 resetMotion, s8 resetPosition, s8 jettison) {
    extern s8 g_bWaitingForRoundReset;
    extern s32 g_savedActions[];
    extern u32 g_attackDelay;
    extern Vector4 g_zeroVector;
    u32 numPlayers;
    u32 i;
    TileCar *spawn;
    TileCar *sp;
    Player *player;
    CarA *car;
    void *rack;
    CarAParams params;
    Matrix4 mtx;

    GameSettings_SetCodeVariable(0x190, 0, GameSettings_GetCodeVariable(0x190, 0) + 1);
    g_bWaitingForRoundReset = 0;
    FileStreaming_ResetQuadResources();
    Trigger_RestoreActions(g_savedActions, 100);
    InterfaceManager_ReleaseAllInterfaces();
    if (g_pTimer != NULL) {
        GETP(*g_pTimer)->time = GameSettings_GetCodeVariable(0x12E, 0);
    }
    if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
        g_attackDelay = 8000;
    }
    numPlayers = PlayerManager_PlayerCount();
    for (i = 0; i < numPlayers; i++) {
        player = PlayerManager_GetPlayerByIndex(i);
        GameSettings_SetCodeVariable(0x194, player->port, 0);
        GameSettings_SetCodeVariable(0x193, player->port, 0);
        player->kills = 0;
        player->stuntPoints = 0;
        spawn = GETP(player->tileCar);
        if (spawn != NULL) {
            if (GETP(player->vehicle) == NULL) {
                params.pos = spawn->pos;
                params.pos.y += 10.0f;
                SETP(params.player, player);
                SETP(params.info, GETP(spawn->info));
                params.vel = g_zeroVector;
                SetOrientation(&mtx, spawn->rotY, spawn->rotX, spawn->rotZ);
                SubMatrixToQuaternion(&mtx, &params.quat);
                SETP(spawn->car, ThingManager_DARYL_CreateThing(THING_TYPE_CAR, &params));
                Player_SetThing(player, GETP(spawn->car));
                SETP(((Thing *)GETP(spawn->car))->trigger, GETP(player->tileCar));
            }
            sp = GETP(player->tileCar);
            car = GETP(player->vehicle);
            if (resetMotion) {
                ((Thing *)car)->health = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
                car->partVisMask = 0;
                MotionState_ReleaseSlavesByType(&car->motionState, 2);
            }
            if (resetPosition) {
                Thing *thing;

                car = GETP(player->vehicle);
                thing = (Thing *)car;
                SetOrientation(THING_MTX(thing), sp->rotY, sp->rotX, sp->rotZ);
                SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
                thing->pos.x = sp->pos.x;
                thing->pos.y = 10.0f + sp->pos.y;
                thing->pos.z = sp->pos.z;
                thing->vel.x = 0.0f;
                thing->vel.y = 0.0f;
                thing->vel.z = 0.0f;
                car->forceAccum = g_zeroVector;
                car->angularMomentum = g_zeroVector;
                car->localAxis = g_zeroVector;
                car->rpm = 0.0f;
                car->gradeOrReverse = 0.0f;
                car->curveTableIdx = 1;
                car->navPointIndex = -1;
                car->navUpdateTimer = 1;
                THING_VFN(thing, THING_V_MOVE, void (*)(Thing *))(thing);
                thing->flags |= THING_FLAG_IN_TELEPORTER;   /* don't teleport straight away */
            }
            if (jettison || GameSettings_GetCodeVariable(0x135, 0) != 0) {
                rack = ThingManagerList_GetByType(&((CarA *)GETP(player->vehicle))->motionState.slaves, THING_TYPE_WILDCARD_RACK);
                if (rack != NULL) {
                    WildCardRack_JettisonAll(rack);
                }
            }
            if (!Player_IsHuman(player)) {
                AI_Car_Reset((u8 *)player);
            }
        }
    }
}
