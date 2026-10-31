/* The player manager: fixed pools of human, AI-car, creature-AI and null
 * players, kept in an active and a free list (Player.next/prev). */
#include "common.h"
#include "CarA.h"

extern u8 g_playerManager[0x20E0];
extern u8 *g_pPlayerManager;     /* = g_playerManager */

/* The pools: players are Player-headed, but only as big as their kind needs
 * (a creature AI is 0x100 bytes, a null player 0xB8). */
typedef struct PlayerPools {
    u8 humans[4][0x158];        /* 0x0000 */
    u8 cars[4][0x670];          /* 0x0560 AICar */
    u8 creatures[1][0x100];     /* 0x1F20 AICreature */
    u8 nulls[1][0xB8];          /* 0x2020 */
    PTR32(Player) active;       /* 0x20D8 */
    PTR32(Player) free;         /* 0x20DC */
} PlayerPools;

_Static_assert(offsetof(PlayerPools, active) == 0x20D8, "PlayerPools.active");

#define PLAYER_POOLS ((PlayerPools *)g_playerManager)

static inline Player *PlayerManager_PopActive(PlayerPools *mgr) {
    Player *player = GETP(mgr->active);
    if (player == NULL) {
        return NULL;
    }
    SETP(mgr->active, GETP(player->next));
    if (GETP(player->next) != NULL) {
        SETP(GETP(player->next)->prev, NULL);
    }
    SETP(player->next, NULL);
    return player;
}

/* Unlink from a list whose head is *head. */
static inline void PlayerManager_Unlink(PTR32(Player) *head, Player *player) {
    if (GETP(player->prev) == NULL) {
        SETP(*head, GETP(player->next));
    } else {
        SETP(GETP(player->prev)->next, GETP(player->next));
    }
    if (GETP(player->next) != NULL) {
        SETP(GETP(player->next)->prev, GETP(player->prev));
        SETP(player->next, NULL);
    }
    SETP(player->prev, NULL);
}

/* Push onto the front of a list whose head is *head. */
static inline void PlayerManager_Push(PTR32(Player) *head, Player *player) {
    SETP(player->next, GETP(*head));
    SETP(player->prev, NULL);
    if (GETP(player->next) != NULL) {
        SETP(GETP(player->next)->prev, player);
    }
    SETP(*head, player);
}

static inline void *PlayerManager_GetAIInit(void *desc) {
    switch (((u8 *)desc)[0xD]) {
    case 0:
        return AI_Car_Init;
    case 1:
        return AI_Creature_Init;
    }
    return NULL;
}

void PlayerManager_Create(void) {

}

void PlayerManager_Destroy(void) {

}

void PlayerManager_Release(void) {
    PlayerPools *mgr = PLAYER_POOLS;
    Player *player;

    while ((player = PlayerManager_PopActive(mgr)) != NULL) {
        Player_ReleaseInterface(player);
        PlayerManager_Push(&mgr->free, player);
    }
}

/* Fill the free list (human, AI car, creature, null players, each pushed in
 * turn, so the null player ends up first) and set each one's functions. */
void PlayerManager_Init(void) {
    extern s32 g_aiHoldIndex;
    PlayerPools *pm = PLAYER_POOLS;
    Player *player;
    s32 i;

    SETP(pm->active, NULL);
    SETP(pm->free, NULL);
    for (i = 0; i < 4; i++) {
        player = (Player *)pm->humans[i];
        SETP(player->controlReader, Player_DefaultControlReader);
        SETP(player->init, PlayerHuman_Init);
        SETP(player->heartbeat, PlayerHuman_Heartbeat);
        PlayerManager_Push(&pm->free, player);
    }
    for (i = 0; i < 4; i++) {
        player = (Player *)pm->cars[i];
        SETP(player->controlReader, AI_Car_Heartbeat);
        SETP(player->init, AI_Car_Init);
        SETP(player->heartbeat, Player_Heartbeat);
        PlayerManager_Push(&pm->free, player);
    }
    for (i = 0; i < 1; i++) {
        player = (Player *)pm->creatures[i];
        SETP(player->controlReader, AI_Creature_Heartbeat);
        SETP(player->init, AI_Creature_Init);
        SETP(player->heartbeat, Player_Heartbeat);
        PlayerManager_Push(&pm->free, player);
    }
    /* The null player is only 0xB8 bytes (the end of the pool); just its
     * functions and list links (below 0xB8) are used. GCC sees a 0x158-byte
     * Player there. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    for (i = 0; i < 1; i++) {
        player = (Player *)pm->nulls[i];
        SETP(player->controlReader, Player_NULL);
        SETP(player->init, Player_Init);
        SETP(player->heartbeat, Player_NULL);
        PlayerManager_Push(&pm->free, player);
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    g_aiHoldIndex = 0;
    AI_Create();
    ForceFeedback_Init();
    while ((player = PlayerManager_PopActive(pm)) != NULL) {
        Player_ReleaseInterface(player);
        PlayerManager_Push(&pm->free, player);
    }
}

/* Take the first free human player, init it and make it active. */
void *PlayerManager_NewHumanPlayer(void) {
    PlayerPools *pm = PLAYER_POOLS;
    Player *player;

    for (player = GETP(pm->free); player != NULL; player = GETP(player->next)) {
        if (Player_IsHuman(player)) {
            PlayerManager_Unlink(&pm->free, player);
            break;
        }
    }
    if (player == NULL) {
        return NULL;
    }
    ((void (*)(void *, s32))GETP(player->init))(player, 0);
    PlayerManager_Push(&pm->active, player);
    return player;
}

/* Take the first free AI player of the kind desc asks for (byte 0x0D: 0 car,
 * 1 creature), init it from desc and make it active. */
void *PlayerManager_NewAIPlayer(u8 *desc) {
    PlayerPools *pm = PLAYER_POOLS;
    void (*init)(void *);
    Player *player;

    player = GETP(pm->free);
    switch (desc[0xD]) {
    case 0:
        init = AI_Car_Init;
        break;
    case 1:
        init = AI_Creature_Init;
        break;
    default:
        init = NULL;
        break;
    }
    for (; player != NULL; player = GETP(player->next)) {
        if ((void (*)(void *))GETP(player->init) == init) {
            PlayerManager_Unlink(&pm->free, player);
            break;
        }
    }
    if (player == NULL) {
        return NULL;
    }
    /* the original leaves desc in r4: AI init functions take (player, desc) */
    GETP(player->init)(player, desc);
    PlayerManager_Push(&pm->active, player);
    return player;
}

u8 PlayerManager_PlayerHumanCount(void) {
    u8 count = 0;
    Player *player;

    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = GETP(player->next)) {
        if (Player_IsHuman(player)) {
            count++;
        }
    }
    return count;
}

u8 PlayerManager_PlayerCount(void) {
    u8 count = 0;
    Player *player;

    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = GETP(player->next)) {
        count++;
    }
    return count;
}

void *PlayerManager_GetPlayerByIndex(u8 index) {
    u8 i = 0;
    Player *player;

    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = GETP(player->next)) {
        if (i == index) {
            return player;
        }
        i++;
    }
    return NULL;
}

void *PlayerManager_GetHumanPlayerByIndex(u8 index) {
    Player *player;
    u8 i = 0;

    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = GETP(player->next)) {
        if (Player_IsHuman(player)) {
            if (i == index) {
                return player;
            }
            i++;
        }
    }
    return NULL;
}

/* Read every player's input, run the AI events, then each player's heartbeat. */
void PlayerManager_Heartbeat(void) {
    extern u32 g_attackDelay;
    extern u32 g_iDeltaTimeMS;
    extern s16 g_AI_FramesSinceAsyncEventProcessed;
    extern u8 g_AI_AsyncProcess[16];
    Player *player;
    Player *next;

    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = GETP(player->next)) {
        Player_InputHeartbeat(player);
    }
    if (g_attackDelay > g_iDeltaTimeMS) {
        g_attackDelay -= g_iDeltaTimeMS;
    } else {
        g_attackDelay = 0;
    }
    g_AI_FramesSinceAsyncEventProcessed++;
    if (GETP(g_worldData.world) != NULL) {
        if ((s8)g_AI_AsyncProcess[0xC] == 0) {
            AI_AddEvent(g_AI_AsyncProcess);
        }
        while ((s8)AI_ProcessEvent()) {
        }
    }
    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = next) {
        next = GETP(player->next);
        GETP(player->heartbeat)(player);
    }
}

void PlayerManager_Render_DEBUG(void) {

}

void PlayerManager_ClearAllCameraFlags_PRIVATE(void) {
    Player *player;

    for (player = GETP(PLAYER_POOLS->active); player != NULL; player = GETP(player->next)) {
        player->flags &= ~0x10;
    }
}

/* Move an active player back to the free list. */
s32 PlayerManager_RemovePlayer(void *player) {
    PlayerPools *mgr = PLAYER_POOLS;
    Player *p;

    for (p = GETP(mgr->active); p != NULL; p = GETP(p->next)) {
        if (p == player) {
            break;
        }
    }
    if (p == NULL) {
        return 0;
    }
    PlayerManager_Unlink(&mgr->active, p);
    PlayerManager_Push(&mgr->free, p);
    return 1;
}

