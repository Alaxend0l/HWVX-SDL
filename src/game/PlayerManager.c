#include "common.h"
extern u8 g_playerManager[0x20E0];

typedef struct PMPlayer {
    u8 pad0[0x20];
    FPTR32(void, (void)) controlReader;
    FPTR32(void, (void)) init;
    FPTR32(void, (void)) heartbeat;
    u8 pad2C[0xB0 - 0x2C];
    PTR32(struct PMPlayer) next;
    PTR32(struct PMPlayer) prev;
} PMPlayer;

typedef struct PlayerManager {
    u8 pad0[0x20D8];
    PTR32(PMPlayer) active;
    PTR32(PMPlayer) free;
} PlayerManager;

static inline PMPlayer *PlayerManager_PopActive(PlayerManager *mgr) {
    PMPlayer *player = GETP(mgr->active);
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

static inline void PlayerManager_UnlinkActive(PlayerManager *mgr, PMPlayer *player) {
    if (GETP(player->prev) == NULL) {
        SETP(mgr->active, GETP(player->next));
    } else {
        SETP(GETP(player->prev)->next, GETP(player->next));
    }
    if (GETP(player->next) != NULL) {
        SETP(GETP(player->next)->prev, GETP(player->prev));
        SETP(player->next, NULL);
    }
    SETP(player->prev, NULL);
}

static inline void PlayerManager_PushFree(PlayerManager *mgr, PMPlayer *player) {
    SETP(player->next, GETP(mgr->free));
    SETP(player->prev, NULL);
    if (GETP(player->next) != NULL) {
        SETP(GETP(player->next)->prev, player);
    }
    SETP(mgr->free, player);
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
    PlayerManager *mgr = (PlayerManager *)g_playerManager;
    PMPlayer *player;

    while ((player = PlayerManager_PopActive(mgr)) != NULL) {
        Player_ReleaseInterface(player);
        PlayerManager_PushFree(mgr, player);
    }
}

void PlayerManager_Init(void) {
    typedef struct {
        PMPlayer base;
        u8 padB8[0x158 - 0xB8];
    } HumanPlayer;
    typedef struct {
        PMPlayer base;
        u8 padB8[0x670 - 0xB8];
    } CarPlayer;
    typedef struct {
        PMPlayer base;
        u8 padB8[0x100 - 0xB8];
    } CreaturePlayer;
    typedef struct {
        HumanPlayer humans[4];
        CarPlayer cars[4];
        CreaturePlayer creatures[1];
        PMPlayer nulls[1];
        PTR32(PMPlayer) active;
        PTR32(PMPlayer) free;
    } PlayerMgr;
    extern s32 g_aiHoldIndex;
    PlayerMgr *pm = (PlayerMgr *)g_playerManager;
    PMPlayer *player;
    s32 i;

    SETP(pm->active, NULL);
    SETP(pm->free, NULL);
    for (i = 0; i < 4; i++) {
        player = &pm->humans[i].base;
        SETP(player->controlReader, Player_DefaultControlReader);
        SETP(player->init, PlayerHuman_Init);
        SETP(player->heartbeat, PlayerHuman_Heartbeat);
        PlayerManager_PushFree((PlayerManager *)pm, player);
    }
    for (i = 0; i < 4; i++) {
        player = &pm->cars[i].base;
        SETP(player->controlReader, AI_Car_Heartbeat);
        SETP(player->init, AI_Car_Init);
        SETP(player->heartbeat, Player_Heartbeat);
        PlayerManager_PushFree((PlayerManager *)pm, player);
    }
    for (i = 0; i < 1; i++) {
        player = &pm->creatures[i].base;
        SETP(player->controlReader, AI_Creature_Heartbeat);
        SETP(player->init, AI_Creature_Init);
        SETP(player->heartbeat, Player_Heartbeat);
        PlayerManager_PushFree((PlayerManager *)pm, player);
    }
    for (i = 0; i < 1; i++) {
        player = &pm->nulls[i];
        SETP(player->controlReader, Player_NULL);
        SETP(player->init, Player_Init);
        SETP(player->heartbeat, Player_NULL);
        PlayerManager_PushFree((PlayerManager *)pm, player);
    }
    g_aiHoldIndex = 0;
    AI_Create();
    ForceFeedback_Init();
    while ((player = PlayerManager_PopActive((PlayerManager *)pm)) != NULL) {
        Player_ReleaseInterface(player);
        PlayerManager_PushFree((PlayerManager *)pm, player);
    }
}

void *PlayerManager_NewHumanPlayer(void) {
    extern u8 g_playerManager[0x20E0];
    u8 *player;

    for (player = SLOT_LOAD(u8 *, (u8 *)(g_playerManager) + 0x20DC); player != NULL; player = SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0)) {
        if (Player_IsHuman(player)) {
            if (SLOT_LOAD(u8 *, (u8 *)(player) + 0xB4) == NULL) {
                SLOT_STORE((u8 *)(g_playerManager) + 0x20DC, SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0));
            } else {
                SLOT_STORE((u8 *)(SLOT_LOAD(u8 *, (u8 *)(player) + 0xB4)) + 0xB0, SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0));
            }
            if (SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0) != NULL) {
                SLOT_STORE((u8 *)(SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0)) + 0xB4, SLOT_LOAD(u8 *, (u8 *)(player) + 0xB4));
                SLOT_STORE((u8 *)(player) + 0xB0, NULL);
            }
            SLOT_STORE((u8 *)(player) + 0xB4, NULL);
            break;
        }
    }
    if (player == NULL) {
        return NULL;
    }
    ((void (*)(void *, s32))SLOT_LOAD(void *, (u8 *)PORT_ADDR(player) + 0x24))(player, 0);
    SLOT_STORE((u8 *)(player) + 0xB0, SLOT_LOAD(u8 *, (u8 *)(g_playerManager) + 0x20D8));
    SLOT_STORE((u8 *)(player) + 0xB4, NULL);
    if (SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0) != NULL) {
        SLOT_STORE((u8 *)(SLOT_LOAD(u8 *, (u8 *)(player) + 0xB0)) + 0xB4, player);
    }
    SLOT_STORE((u8 *)(g_playerManager) + 0x20D8, player);
    return player;
}

void *PlayerManager_NewAIPlayer(u8 *desc) {
    extern u8 g_playerManager[0x20E0];
    void (*init)(void *);
    u8 *player;

    player = SLOT_LOAD(u8 *, (g_playerManager + 0x20DC));
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
    for (; player != NULL; player = SLOT_LOAD(u8 *, (player + 0xB0))) {
        if (*(void (**)(void *))(player + 0x24) == init) {
            if (SLOT_LOAD(u8 *, (player + 0xB4)) == NULL) {
                SLOT_STORE((g_playerManager + 0x20DC), SLOT_LOAD(u8 *, (player + 0xB0)));
            } else {
                SLOT_STORE((SLOT_LOAD(u8 *, (player + 0xB4)) + 0xB0), SLOT_LOAD(u8 *, (player + 0xB0)));
            }
            if (SLOT_LOAD(u8 *, (player + 0xB0)) != NULL) {
                SLOT_STORE((SLOT_LOAD(u8 *, (player + 0xB0)) + 0xB4), SLOT_LOAD(u8 *, (player + 0xB4)));
                SLOT_STORE((player + 0xB0), NULL);
            }
            SLOT_STORE((player + 0xB4), NULL);
            break;
        }
    }
    if (player == NULL) {
        return NULL;
    }
    (*(void (**)(void *))(player + 0x24))(player);
    SLOT_STORE((player + 0xB0), SLOT_LOAD(u8 *, (g_playerManager + 0x20D8)));
    SLOT_STORE((player + 0xB4), NULL);
    if (SLOT_LOAD(u8 *, (player + 0xB0)) != NULL) {
        SLOT_STORE((SLOT_LOAD(u8 *, (player + 0xB0)) + 0xB4), player);
    }
    SLOT_STORE((g_playerManager + 0x20D8), player);
    return player;
}

u8 PlayerManager_PlayerHumanCount(void) {
    extern u8 g_playerManager[0x20E0];
    u8 count = 0;
    void *player;

    for (player = SLOT_LOAD(void *, (u8 *)(g_playerManager) + 0x20D8); player != NULL; player = SLOT_LOAD(void *, (u8 *)(player) + 0xB0)) {
        if (Player_IsHuman(player)) {
            count++;
        }
    }
    return count;
}

u8 PlayerManager_PlayerCount(void) {
    extern u8 g_playerManager[0x20E0];
    u8 count = 0;
    void *player;

    for (player = SLOT_LOAD(void *, (u8 *)(g_playerManager) + 0x20D8); player != NULL; player = SLOT_LOAD(void *, (u8 *)(player) + 0xB0)) {
        count++;
    }
    return count;
}

void *PlayerManager_GetPlayerByIndex(u8 index) {
    extern u8 *g_pPlayerManager;
    u8 i = 0;
    void *player;

    for (player = SLOT_LOAD(void *, (u8 *)(g_pPlayerManager) + 0x20D8); player != NULL; player = SLOT_LOAD(void *, (u8 *)(player) + 0xB0)) {
        if (i == index) {
            return player;
        }
        i++;
    }
    return NULL;
}

void *PlayerManager_GetHumanPlayerByIndex(u8 index) {
    extern u8 *g_pPlayerManager;
    void *player;
    u8 i = 0;

    for (player = SLOT_LOAD(void *, (u8 *)(g_pPlayerManager) + 0x20D8); player != NULL; player = SLOT_LOAD(void *, (u8 *)(player) + 0xB0)) {
        if (Player_IsHuman(player)) {
            if (i == index) {
                return player;
            }
            i++;
        }
    }
    return NULL;
}

void PlayerManager_Heartbeat(void) {
    extern u8 *g_pPlayerManager;
    extern u32 g_attackDelay;
    extern u32 g_iDeltaTimeMS;
    extern s16 g_AI_FramesSinceAsyncEventProcessed;
    extern u8 g_AI_AsyncProcess[16];
    void *player;
    void *next;

    for (player = SLOT_LOAD(void *, (u8 *)(g_pPlayerManager) + 0x20D8); player != NULL; player = SLOT_LOAD(void *, (u8 *)(player) + 0xB0)) {
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
    for (player = SLOT_LOAD(void *, (u8 *)(g_pPlayerManager) + 0x20D8); player != NULL; player = next) {
        next = SLOT_LOAD(void *, (u8 *)(player) + 0xB0);
        ((void (*)(void *))SLOT_LOAD(void *, (u8 *)PORT_ADDR(player) + 0x28))(player);
    }
}

void PlayerManager_Render_DEBUG(void) {

}

void PlayerManager_ClearAllCameraFlags_PRIVATE(void) {
    extern u8 *g_pPlayerManager;
    void *player;

    for (player = SLOT_LOAD(void *, (u8 *)(g_pPlayerManager) + 0x20D8); player != NULL; player = SLOT_LOAD(void *, (u8 *)(player) + 0xB0)) {
        M2C_FIELD(player, u32 *, 0x6C) &= ~0x10;
    }
}

s32 PlayerManager_RemovePlayer(void *player) {
    extern u8 *g_pPlayerManager;
    PlayerManager *mgr;
    PMPlayer *p;

    for (p = GETP(((PlayerManager *)g_playerManager)->active); p != NULL; p = GETP(p->next)) {
        if (p == player) {
            break;
        }
    }
    if (p == NULL) {
        return 0;
    }
    PlayerManager_UnlinkActive((PlayerManager *)g_pPlayerManager, p);
    mgr = (PlayerManager *)g_pPlayerManager;
    SETP(p->next, GETP(mgr->free));
    SETP(p->prev, NULL);
    if (GETP(p->next) != NULL) {
        SETP(GETP(p->next)->prev, p);
    }
    SETP(mgr->free, p);
    return 1;
}

