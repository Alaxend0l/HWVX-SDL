/* An online session on the game's side: the lobby, everyone choosing a car, and
 * every machine loading the same level. Port-only file. The transport is
 * src/port/net.h, the screens are in Port_Menu.c.
 *
 * The host goes through the game's own multi-player menus (mode, options, world).
 * Reaching the car select (Port_OnlineHostSetup) sends the choices to the clients,
 * who jump there from their lobby. Each machine's one player picks a car on the
 * game's one-player car select, and when the host knows them all, every machine sets every
 * port's car and starts the level. A player's port is their peer id + 1: it picks
 * the spawn, and the world's other ports stay unused the way they do in a local
 * game with fewer players (code variable 0xCB -1).
 *
 * Saves: all cars, worlds and weapons are open online, and nothing done online
 * reaches a save: GameSettings_PortOnlineBegin/End keep the game data as it was.
 *
 * In the level, each machine simulates its own car and tells the others about it:
 * the other cars run the same car code, driven by their owners' inputs and pulled
 * towards where the owners say they are (Port_OnlineRemoteControl). The world holds
 * after loading and after each round reset until the host says go, so the scripts'
 * LevelStart triggers, and the countdown they start, fire together. The host also
 * decides who finished first: a machine only claims its own car's finish, and only
 * the host's answer runs the script's RoundComplete trigger, on every machine.
 *
 * Weapons: a level's random pickups come from a seed the host shares, so every machine
 * has the same ones. A car takes pickups only on its owner's machine, which tells the
 * others. Other machines fire a remote car's weapon until its ammo is down to what the
 * owner reports, and a car takes damage only on its owner's machine: its health travels
 * with its snapshots, so a hit counts where the victim is.
 *
 * Not yet: AI opponents are off because each machine would run its own; a weapon a car
 * dropped isn't shared if picked up again. */
#include <SDL3/SDL.h>
#include <math.h>

#include "common.h"
#include "CarA.h"
#include "World.h"
#include "Interface.h"
#include "ThingManager.h"
#include "WildCard.h"
#include "Port_Online.h"
#include "net.h"

#define ONLINE_PLAYERS 4        /* the game's ports; see NET_DEFAULT_PLAYERS */
#define WORLD_OF_LEVEL() ((World *)GETP(g_worldData.world))
#define MAX_VEHICLE 32          /* the car select's list */
#define MAX_ITEM 0x60
#define NO_ITEM (-1)

enum {
    MSG_SETUP = 1,              /* host: u8 progression, u16 level, u8 ports in, the settings */
    MSG_ABORT,                  /* host: left the car select */
    MSG_CARS,                   /* client: s32 vehicle, s32 item */
    MSG_START,                  /* host: u8 ports in, u32 seed, then s32 vehicle, s32 item per port */
    MSG_READY,                  /* client: u32 stage: held, waiting for go */
    MSG_GO,                     /* host: u32 stage */
    MSG_FINISH,                 /* client: u32 stage, u32 ms since go: its car finished */
    MSG_WINNER,                 /* host: u32 stage, u8 the winner's script number */
    MSG_RETURN,                 /* host: the level is over, back to the lobby */
    MSG_LEFT_LEVEL,             /* client: in the lobby */
    MSG_CONTINUE,               /* host: it left the round summary */
    MSG_PICKUP,                 /* u8 port, u16 quadrant, u16 item, u16 ammo: its car took it */
    MSG_CAR_STATE,              /* u8 port, u32 stage, u8 alive: its car was destroyed or respawned */
};

#define SNAPSHOT_MS 33          /* how often a car tells the others about itself */
#define SNAPSHOT_SIZE 72
#define HOLD_MAX_MS 30000       /* a machine this late doesn't hold up the others */
#define SUMMARY_MS 8000         /* the round summary moves on without anyone pressing */
#define SUMMARY_PRESS_MS 500    /* it may be still coming in: press until it's gone */
#define FINISH_WINDOW_MS 60     /* plus the slowest half round trip: for close finishes */
#define MAX_LEAD_S 0.3f         /* extrapolation limit */
/* Fractions per second by which a remote car closes in on where it should be. */
#define PULL_POS 6.0f
#define PULL_VEL 8.0f
#define PULL_ROT 10.0f
/* Further off than this, or than half a second's travel, it jumps instead. */
#define SNAP_DISTANCE 60.0f
#define MAX_SHOTS_BEHIND 3      /* further behind the owner, skip them rather than a burst */

/* The latest word on another machine's car. */
typedef struct RemoteCar {
    int have;
    u32 seq;
    u64 receivedAt;
    Vector4 pos, vel, angularMomentum;
    Quaternion quat;
    s8 stickX, stickY;
    u8 throttle, brake;
    u32 buttons;                /* pressed since the last snapshot: applied once */
    s32 health;
    u8 fire;                    /* held since the last snapshot */
    u8 noAmmoCost;              /* its owner has the infinite ammo cheat: ammo says nothing */
    u8 slot;                    /* the weapon selected */
    s16 ammo;                   /* in it */
    u8 dead;                    /* its owner's car is gone: this copy goes too */
    u8 killed;                  /* and it has been told to */
    u8 respawn;                 /* its owner has a new one */
} RemoteCar;

typedef enum OnlineState {
    STATE_NONE,
    STATE_CONNECTING,
    STATE_LOBBY,
    STATE_CHOOSING,
    STATE_WAITING,              /* this machine has chosen */
    STATE_PLAYING,              /* from the level loading on */
} OnlineState;

/* The settings the mode's options screen leaves, in the order they travel. */
static const u32 s_settings[] = {0xC8, 0x12C, 0x12D, 0x12E, 0x12F, 0x130, 0x131, 0x132, 0x134, 0x135, 0x136};

static OnlineState s_state;
static int s_isHost;
static int s_port;              /* this machine's: 1 .. ONLINE_PLAYERS */
static u32 s_present;           /* bit port - 1 */
static u32 s_in;                /* those in the current game */
static u32 s_chosen;
static s32 s_vehicle[ONLINE_PLAYERS], s_item[ONLINE_PLAYERS];
static char s_hostLine[64];
static int s_inLevel;
static Player *s_localPlayer;
static Player *s_players[ONLINE_PLAYERS];
static RemoteCar s_remote[ONLINE_PLAYERS];
static u32 s_lobby;             /* host: those not in a level */
/* A stage is what's between two go's: the level, each round after a reset. */
static u32 s_stage, s_goStage;
static int s_hold;
static u64 s_holdSince, s_goAt, s_goTick;
static u32 s_ready[ONLINE_PLAYERS];
static int s_claimed, s_decided;
static u32 s_bestElapsed, s_bestPort;
static u64 s_claimDeadline;
static u64 s_nextSnapshot;
static u32 s_snapshotSeq, s_buttons;
static int s_menuOpen;
static const void *s_summary;   /* the round summary's interface header */
static u64 s_summarySince, s_summaryPress;
static int s_continue, s_pressA;
static u32 s_levelSeed;
static int s_fire;              /* held since the last snapshot */
static int s_applyingPickup;
static int s_localAlive;

void GameSettings_PortOnlineBegin(void);
void GameSettings_PortOnlineEnd(void);

int Port_OnlineActive(void) {
    return s_state != STATE_NONE;
}

int Port_OnlineIsRemote(const Player *player) {
    if (s_state == STATE_NONE) {
        return 0;
    }
    /* the port only while the level is being set up: scripts renumber them later */
    return s_localPlayer != NULL ? player != s_localPlayer : (s32)player->port != s_port;
}

int Port_OnlinePlayerCount(void) {
    u32 i, n = 0;

    for (i = 0; i < ONLINE_PLAYERS; i++) {
        n += (s_present >> i) & 1;
    }
    return (int)n;
}

static void ShowLobby(const char *status) {
    char text[64];

    SDL_snprintf(text, sizeof(text), "Players: %d/%d", Port_OnlinePlayerCount(), ONLINE_PLAYERS);
    if (s_isHost) {
        /* the screens are built anew with each main menu */
        Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_HOST, 0, s_hostLine);
        Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_HOST, 1, text);
        if (status != NULL) {
            Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_HOST, 2, status);
        }
    } else {
        Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_CLIENT, 1, text);
        if (status != NULL) {
            Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_CLIENT, 0, status);
        }
    }
}

static void BeginSession(int isHost) {
    GameSettings_PortOnlineBegin();
    s_isHost = isHost;
    s_present = isHost ? 1 : 0;
    s_lobby = 0xF;
    s_hold = 0;
    s_localPlayer = NULL;
    s_port = isHost ? 1 : 0;
    s_in = s_chosen = 0;
}

int Port_OnlineStartHost(void) {
    char address[48];
    NetError error = Net_Host(NET_DEFAULT_PORT, NULL, 0, ONLINE_PLAYERS);

    if (error != NET_OK) {
        Port_Log("online: %s", Net_ErrorName(error));
        Port_MenuOnlineSetLine(ONLINE_SCREEN_ONLINE, 0, Net_ErrorName(error));
        return 0;
    }
    BeginSession(1);
    s_state = STATE_LOBBY;
    if (Net_LocalAddress(address, sizeof(address))) {
        SDL_snprintf(s_hostLine, sizeof(s_hostLine), "%s:%u", address, NET_DEFAULT_PORT);
    } else {
        SDL_snprintf(s_hostLine, sizeof(s_hostLine), "Port %u", NET_DEFAULT_PORT);
    }
    Port_Log("online: hosting at %s", s_hostLine);
    ShowLobby("Waiting...");
    return 1;
}

int Port_OnlineStartClient(const char *address) {
    NetError error = Net_Connect(address, NULL);

    if (error != NET_OK) {
        Port_Log("online: %s", Net_ErrorName(error));
        return 0;
    }
    BeginSession(0);
    s_state = STATE_CONNECTING;
    Port_Log("online: connecting to %s", address);
    Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_CLIENT, 1, "");
    Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_CLIENT, 0, "Connecting...");
    return 1;
}

void Port_OnlineEnd(void) {
    if (s_state == STATE_NONE) {
        return;
    }
    Net_Close();
    s_state = STATE_NONE;
    GameSettings_PortOnlineEnd();
    Port_Log("online: session over");
}

/* The session is gone under us: back out to a menu that exists without it. */
static void SessionLost(const char *why) {
    int playing = s_state == STATE_PLAYING;

    Port_Log("online: %s", why);
    Port_OnlineEnd();
    if (playing && StateManager_CurrState() == 5) {
        ExitStateGamePlay(2);
    } else if (StateManager_CurrState() == 2) {
        /* after a level the way here came through the main menu, not this screen */
        Port_MenuOnlineSetLine(ONLINE_SCREEN_ONLINE, 0, why);
        if (!Port_InterfaceGoBack(Port_MenuOnlineScreen(ONLINE_SCREEN_ONLINE))) {
            Port_InterfaceGoBack(Port_MenuOnlineScreen(ONLINE_SCREEN_MAIN));
        }
    }
}

/* ---- choosing -------------------------------------------------------------- */

void Port_OnlineHostSetup(void) {
    u8 buf[64];
    NetWriter w;
    s32 id, level;
    u32 i;

    if (s_state == STATE_NONE || !s_isHost) {
        return;
    }
    /* each machine would run its own AI: none until they are shared */
    GameSettings_SetCodeVariable(0x133, 0, 0);
    StateProgressionManager_GetProgressionID(&id, &level);
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_SETUP);
    NetWriter_U8(&w, (u8)id);
    NetWriter_U16(&w, (u16)level);
    NetWriter_U8(&w, (u8)(s_present & s_lobby));
    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        NetWriter_U32(&w, GameSettings_GetCodeVariable(s_settings[i], 0));
    }
    s_in = s_present & s_lobby;
    s_chosen = 0;
    s_state = STATE_CHOOSING;
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    Port_Log("online: setting up progression %d level %d for ports %x", id, level, s_in);
}

static void ClientSetup(NetReader *r) {
    u32 id = NetReader_U8(r);
    u32 level = NetReader_U16(r);
    u32 in = NetReader_U8(r);
    s32 values[SDL_arraysize(s_settings)];
    u32 i;

    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        values[i] = (s32)NetReader_U32(r);
    }
    /* the multi-player drag race and battle scripts */
    if (!r->ok || (id != 5 && id != 7) || !(in & (1u << (s_port - 1)))) {
        return;
    }
    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        GameSettings_SetCodeVariable(s_settings[i], 0, values[i]);
    }
    GameSettings_SetCodeVariable(0x133, 0, 0);
    StateProgressionManager_SetProgressionID((s32)id, (s32)level);
    s_in = in;
    s_state = STATE_CHOOSING;
    Port_Log("online: setting up progression %u level %u for ports %x", id, level, in);
    Port_InterfaceGoto(Port_MenuOnlineScreen(ONLINE_SCREEN_CAR_SELECT));
}

static void StartLevel(void) {
    u32 port;

    for (port = 1; port <= ONLINE_PLAYERS; port++) {
        int in = (s_in >> (port - 1)) & 1;

        GameSettings_SetCodeVariable(0xCB, port, in ? s_vehicle[port - 1] : -1);
        GameSettings_SetCodeVariable(0xCC, port, in ? s_item[port - 1] : -1);
    }
    s_state = STATE_PLAYING;
    s_inLevel = 0;
    s_localPlayer = NULL;
    SDL_zeroa(s_players);
    SDL_zeroa(s_ready);
    s_stage = s_goStage = 0;
    s_hold = 0;
    s_menuOpen = 0;
    s_lobby = 0;
    Port_Log("online: starting, this is port %d", s_port);
    StateManager_QueueChange(0xB);
}

/* The host: when every car is known, start everyone. */
static void HostCheckChosen(void) {
    u8 buf[64];
    NetWriter w;
    u32 i;

    if (!s_isHost || s_state != STATE_WAITING || (s_chosen & s_in) != s_in) {
        return;
    }
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_START);
    NetWriter_U8(&w, (u8)s_in);
    s_levelSeed = (u32)SDL_GetPerformanceCounter();
    NetWriter_U32(&w, s_levelSeed);
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        NetWriter_U32(&w, (u32)s_vehicle[i]);
        NetWriter_U32(&w, (u32)s_item[i]);
    }
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    StartLevel();
}

int Port_OnlineCarsChosen(Interface *iface) {
    /* other screens end the same way: the race's own HUD does */
    if (s_state != STATE_CHOOSING || GETP(iface->header) != Port_MenuOnlineScreen(ONLINE_SCREEN_CAR_SELECT)) {
        return 0;
    }
    /* the one-player screen's choice is player 1's, whatever this machine's port */
    s_vehicle[s_port - 1] = GameSettings_GetCodeVariable(0xCB, 1);
    s_item[s_port - 1] = GameSettings_GetCodeVariable(0xCC, 1);
    s_chosen |= 1u << (s_port - 1);
    s_state = STATE_WAITING;
    SETP(iface->next, Port_MenuOnlineScreen(ONLINE_SCREEN_WAITING));
    InterfaceManager_SetLoadState(iface, 1);
    if (s_isHost) {
        HostCheckChosen();
    } else {
        u8 buf[16];
        NetWriter w;

        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_CARS);
        NetWriter_U32(&w, (u32)s_vehicle[s_port - 1]);
        NetWriter_U32(&w, (u32)s_item[s_port - 1]);
        Net_SendReliable(NET_PEER_HOST, buf, w.size);
    }
    return 1;
}

int Port_OnlineBack(const InterfaceHeader *header) {
    if (s_state == STATE_NONE) {
        return 0;
    }
    if (header == Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_HOST) || header == Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_CLIENT)) {
        Port_OnlineEnd();
        return 0;
    }
    if (header == Port_MenuOnlineScreen(ONLINE_SCREEN_WAITING)) {
        return 1;
    }
    if (header == Port_MenuOnlineScreen(ONLINE_SCREEN_CAR_SELECT)) {
        u8 msg = MSG_ABORT;

        if (!s_isHost) {
            return 1;           /* the host decides when this ends */
        }
        s_state = STATE_LOBBY;
        Net_SendReliable(NET_PEER_ALL, &msg, 1);
        return 0;
    }
    return 0;
}

/* ---- the level --------------------------------------------------------------- */

/* Whose controller settings a player uses: each machine has one player, on the
 * first pad, whatever port it spawned on. */
u32 Port_OnlineControllerPort(const Player *player) {
    return s_state != STATE_NONE ? 1 : player->port;
}

void Port_OnlineLevelLoading(void) {
    /* a restart makes new players: the last ones say nothing about these */
    s_localPlayer = NULL;
    SDL_zeroa(s_players);
}

void Port_OnlineSetupPlayer(Player *player) {
    if (s_state == STATE_NONE || player->port < 1 || player->port > ONLINE_PLAYERS) {
        return;
    }
    /* by pointer from here on: scripts renumber ports (ActionSetPlayerOrder) */
    s_players[player->port - 1] = player;
    if ((s32)player->port == s_port) {
        s_localPlayer = player;
        player->device = 0;     /* port 2 here is still this machine's first pad */
    }
}

/* ---- hold and go: machines start each stage together ---------------------- */

int Port_OnlineHolding(void) {
    return s_state != STATE_NONE && s_hold;
}

static void Release(void) {
    extern u8 g_bFreezeWorld;

    s_hold = 0;
    s_goAt = 0;
    s_goTick = SDL_GetTicks();
    g_bFreezeWorld = 0;
    Trigger_PauseTACSystem(0);
    Port_Log("online: go, stage %u", s_stage);
}

/* The slowest round trip to anyone, in ms. */
static u32 MaxRtt(void) {
    u32 i, rtt = 0;

    for (i = 1; i < ONLINE_PLAYERS; i++) {
        NetStats stats;

        if (Net_GetStats((u8)i, &stats) && stats.rttMs > rtt) {
            rtt = stats.rttMs;
        }
    }
    return rtt;
}

/* Clients act on go half a round trip after it leaves, so the host waits as long. */
static void HostCheckGo(void) {
    u64 now = SDL_GetTicks();
    u32 i;
    int late = now - s_holdSince >= HOLD_MAX_MS;
    u8 buf[8];
    NetWriter w;

    if (!s_isHost || !s_hold || s_goAt != 0) {
        return;
    }
    for (i = 1; i < ONLINE_PLAYERS && !late; i++) {
        if (((s_in >> i) & 1) && s_ready[i] < s_stage) {
            return;
        }
    }
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_GO);
    NetWriter_U32(&w, s_stage);
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    s_goAt = now + MaxRtt() / 2 + 1;
}

/* The world stops here until go; what was known about other cars is out of date. */
static void BeginStage(void) {
    extern u8 g_bFreezeWorld;

    s_stage++;
    s_hold = 1;
    s_holdSince = SDL_GetTicks();
    s_goAt = 0;
    s_claimed = s_decided = 0;
    s_claimDeadline = 0;
    s_bestElapsed = 0xFFFFFFFF;
    s_buttons = 0;
    SDL_zeroa(s_remote);
    s_localAlive = 1;           /* a reset respawns, on every machine */
    /* the summary went with the level, and it wasn't the Main Menu: a restart */
    if (s_isHost && s_summarySince != 0) {
        u8 msg = MSG_CONTINUE;

        Net_SendReliable(NET_PEER_ALL, &msg, 1);
    }
    s_summary = Port_TriggerRoundSummary();
    s_summarySince = 0;
    s_continue = 0;
    g_bFreezeWorld = 1;
    Trigger_PauseTACSystem(1);
    if (s_isHost) {
        HostCheckGo();
    } else if (s_goStage >= s_stage) {
        Release();              /* the host gave up waiting for this machine */
    } else {
        u8 buf[8];
        NetWriter w;

        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_READY);
        NetWriter_U32(&w, s_stage);
        Net_SendReliable(NET_PEER_HOST, buf, w.size);
    }
}

void Port_OnlineLevelStarted(void) {
    extern s32 g_GameProgressionID;

    if (s_state == STATE_NONE) {
        return;
    }
    Port_Log("online: level ready: progression %d, port %d, %d human cars", g_GameProgressionID, s_port,
             PlayerManager_PlayerHumanCount());
    BeginStage();
}

void Port_OnlineRoundReset(void) {
    if (s_state != STATE_NONE) {
        BeginStage();
    }
}

/* ---- who finished first ---------------------------------------------------- */

static void AnnounceWinner(u32 number) {
    s_decided = 1;
    s_claimDeadline = 0;
    Port_Log("online: round won by %u", number);
    Port_TriggerFireRoundComplete(number);
}

static void HostClaim(u32 number, u32 elapsed) {
    if (s_decided) {
        return;
    }
    if (s_claimDeadline == 0) {
        s_claimDeadline = SDL_GetTicks() + MaxRtt() / 2 + FINISH_WINDOW_MS;
    }
    if (elapsed < s_bestElapsed) {
        s_bestElapsed = elapsed;
        s_bestPort = number;
    }
}

static void HostDecide(void) {
    u8 buf[8];
    NetWriter w;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_WINNER);
    NetWriter_U32(&w, s_stage);
    NetWriter_U8(&w, (u8)s_bestPort);
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    AnnounceWinner(s_bestPort);
}

/* A script's trigger is about to run RoundComplete(number). 1: not on its own say-so.
 * The trigger stays usable: Port_TriggerFireRoundComplete runs it when it's decided. */
int Port_OnlineHoldRoundComplete(u32 number) {
    u32 elapsed;

    if (s_state == STATE_NONE) {
        return 0;
    }
    if (s_localPlayer == NULL || s_localPlayer->port != number || s_claimed) {
        return 1;               /* another machine's car: its owner says when */
    }
    s_claimed = 1;
    elapsed = (u32)(SDL_GetTicks() - s_goTick);
    if (s_isHost) {
        HostClaim(number, elapsed);
    } else {
        u8 buf[12];
        NetWriter w;

        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_FINISH);
        NetWriter_U32(&w, s_stage);
        NetWriter_U32(&w, elapsed);
        Net_SendReliable(NET_PEER_HOST, buf, w.size);
    }
    return 1;
}

/* ---- the round summary: a menu that must not wait for anyone ----------------- */

int Port_OnlinePressA(void) {
    int press = s_state != STATE_NONE && s_pressA;

    s_pressA = 0;
    return press;
}

/* The match is over: the summary offers Restart and Main Menu, and only the host
 * chooses. Everyone else waits, or one machine could restart alone. */
static int MatchComplete(void) {
    return s_summary != NULL && GameSettings_GetCodeVariable(0x197, 0) != 0 && Port_InterfaceIsUp(s_summary);
}

int Port_OnlineMenuBlocked(void) {
    return s_state == STATE_PLAYING && !s_isHost && StateManager_CurrState() == 5 && MatchComplete();
}

/* The host leaving it takes everyone along, and between rounds it leaves by itself
 * after a while. By pressing A on it, as a player would: its first row is what goes
 * on, Continue between rounds and Restart at the end. A restart or the host's Main
 * Menu takes the level and this summary with it: BeginStage and MSG_RETURN tell the
 * others about those. */
static void SummaryHeartbeat(u64 now) {
    if (s_summary == NULL) {
        return;
    }
    if (Port_InterfaceIsUp(s_summary)) {
        int timedOut = !MatchComplete() && s_summarySince != 0 && now - s_summarySince >= SUMMARY_MS;

        if (s_summarySince == 0) {
            s_summarySince = now;
            s_summaryPress = 0;
        }
        if ((s_continue || timedOut) && now >= s_summaryPress) {
            s_summaryPress = now + SUMMARY_PRESS_MS;
            s_pressA = 1;
        }
    } else if (s_summarySince != 0) {
        s_summarySince = 0;
        s_continue = 0;
        if (s_isHost) {
            u8 msg = MSG_CONTINUE;

            Net_SendReliable(NET_PEER_ALL, &msg, 1);
        }
    }
}

/* ---- the pause menu: the race goes on behind it ------------------------------ */

int Port_OnlineMenuOpen(void) {
    return s_state != STATE_NONE && s_menuOpen;
}

void Port_OnlineSetMenuOpen(int open) {
    s_menuOpen = open;
}

/* ---- pickups ----------------------------------------------------------------- */

/* A level's random pickup: the same on every machine. */
s32 Port_OnlineItemRandom(s32 min, s32 max, u32 quadrant, u32 index) {
    u32 x;

    if (s_state == STATE_NONE) {
        return RandomNumberGenerator_GetRandomInteger(min, max);
    }
    x = s_levelSeed ^ (quadrant * 2654435761u) ^ (index * 40503u);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return max > min ? min + (s32)(x % (u32)(max - min)) : min;
}

/* The car is about to take the item. 1: not here. */
int Port_OnlinePickup(CarA *car, ItemThing *item) {
    Player *player = GETP(car->base.player);
    QuadItem *spawner;
    u32 q, i;

    if (s_state == STATE_NONE || s_applyingPickup || player == NULL || !Player_IsHuman(player)) {
        return 0;
    }
    if (Port_OnlineIsRemote(player)) {
        return 1;               /* its owner says when */
    }
    spawner = GETP(item->spawner);
    if (spawner == NULL) {
        return 0;
    }
    for (q = 0; q < WORLD_OF_LEVEL()->numQuads; q++) {
        QuadContents *contents = GETP(GETP(WORLD_OF_LEVEL()->quads)[q].contents);

        for (i = 0; i < contents->numItems; i++) {
            if (&GETP(contents->items)[i] == spawner) {
                u8 buf[10];
                NetWriter w;

                NetWriter_Init(&w, buf, sizeof(buf));
                NetWriter_U8(&w, MSG_PICKUP);
                NetWriter_U8(&w, (u8)s_port);
                NetWriter_U16(&w, (u16)q);
                NetWriter_U16(&w, (u16)i);
                NetWriter_U16(&w, (u16)item->ammo);
                Net_SendReliable(s_isHost ? NET_PEER_ALL : NET_PEER_HOST, buf, w.size);
                return 0;
            }
        }
    }
    return 0;
}

/* Another machine's car was destroyed, or has a new one. A stage's reset leaves every
 * car alive, so word from an earlier stage is out of date. */
static void ReceiveCarState(u32 port, u32 stage, u32 alive) {
    RemoteCar *r;

    if (port < 1 || port > ONLINE_PLAYERS || (s32)port == s_port || stage != s_stage) {
        return;
    }
    r = &s_remote[port - 1];
    r->dead = !alive;
    r->killed = 0;
    r->respawn = alive != 0;
    if (alive) {
        r->have = 0;            /* what it said about the last car */
    }
}

/* Another machine's car took this. Everything from the network is checked: the
 * numbers index the world. */
static void ReceivePickup(u32 port, u32 q, u32 i, s16 ammo) {
    QuadContents *contents;
    QuadItem *spawner;
    Player *player;
    CarA *car;

    if (StateManager_CurrState() != 5 || port < 1 || port > ONLINE_PLAYERS || (s32)port == s_port || q >= WORLD_OF_LEVEL()->numQuads) {
        return;
    }
    contents = GETP(GETP(WORLD_OF_LEVEL()->quads)[q].contents);
    player = s_players[port - 1];
    if (contents == NULL || i >= contents->numItems || player == NULL || (car = GETP(player->vehicle)) == NULL) {
        return;
    }
    spawner = &GETP(contents->items)[i];
    if (GETP(spawner->thing) != NULL) {
        s_applyingPickup = 1;
        CarA_ProcessItemPickup(car, GETP(spawner->thing));
        s_applyingPickup = 0;
    } else if (GETP(spawner->record) != NULL && GETP(GETP(spawner->record)->weapon) != NULL) {
        /* this machine's own car got there first: both have it, as they do there */
        WildCardRack *rack = ThingManagerList_GetByType(&car->motionState.slaves, THING_TYPE_WILDCARD_RACK);

        if (rack != NULL && ammo > 0) {
            WildCardRack_Add(rack, GETP(spawner->record), ammo);
        }
    }
}

/* ---- cars -------------------------------------------------------------------- */

static void SendSnapshot(u64 now) {
    u8 buf[SNAPSHOT_SIZE];
    NetWriter w;
    CarA *car;
    Thing *t;

    if (s_localPlayer == NULL) {
        return;
    }
    car = GETP(s_localPlayer->vehicle);
    if ((car != NULL) != s_localAlive) {
        /* no snapshots while it's gone: others hear of this directly */
        u8 buf[8];
        NetWriter w;

        s_localAlive = car != NULL;
        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_CAR_STATE);
        NetWriter_U8(&w, (u8)s_port);
        NetWriter_U32(&w, s_stage);
        NetWriter_U8(&w, (u8)s_localAlive);
        Net_SendReliable(s_isHost ? NET_PEER_ALL : NET_PEER_HOST, buf, w.size);
    }
    if (car == NULL) {
        return;
    }
    s_buttons |= s_localPlayer->buttonsHeld;
    s_fire |= s_localPlayer->buttonFire != 0;
    if (now < s_nextSnapshot || s_hold) {
        return;
    }
    s_nextSnapshot = now + SNAPSHOT_MS;
    t = &car->base;
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U32(&w, ++s_snapshotSeq);
    NetWriter_F32(&w, t->pos.x);
    NetWriter_F32(&w, t->pos.y);
    NetWriter_F32(&w, t->pos.z);
    NetWriter_F32(&w, t->vel.x);
    NetWriter_F32(&w, t->vel.y);
    NetWriter_F32(&w, t->vel.z);
    NetWriter_F32(&w, t->quat.w);
    NetWriter_F32(&w, t->quat.x);
    NetWriter_F32(&w, t->quat.y);
    NetWriter_F32(&w, t->quat.z);
    NetWriter_F32(&w, car->angularMomentum.x);
    NetWriter_F32(&w, car->angularMomentum.y);
    NetWriter_F32(&w, car->angularMomentum.z);
    NetWriter_U32(&w, (u32)t->health);
    NetWriter_U32(&w, s_buttons);
    NetWriter_U8(&w, (u8)s_localPlayer->stickX);
    NetWriter_U8(&w, (u8)s_localPlayer->stickY);
    NetWriter_U8(&w, s_localPlayer->throttle);
    NetWriter_U8(&w, s_localPlayer->brake);
    NetWriter_U8(&w, (u8)(s_fire | (GameSettings_IsCheatEnabled(0) ? 2 : 0)));
    {
        WildCardRack *rack = ThingManagerList_GetByType(&car->motionState.slaves, THING_TYPE_WILDCARD_RACK);

        NetWriter_U8(&w, rack != NULL ? rack->cur : 0);
        NetWriter_U16(&w, rack != NULL ? (u16)rack->slots[rack->cur].ammo : 0);
    }
    s_buttons = 0;
    s_fire = 0;
    Net_SendLatest(NET_PEER_ALL, (u8)(s_port - 1), buf, w.size);
}

static int Sane(f32 v, f32 limit) {
    return isfinite(v) && fabsf(v) < limit;
}

static void ReceiveSnapshot(u32 port, const NetEvent *e) {
    RemoteCar *r = &s_remote[port - 1];
    RemoteCar in;
    NetReader rd;
    f32 mag;

    SDL_zero(in);
    NetReader_Init(&rd, e->data, e->size);
    in.seq = NetReader_U32(&rd);
    in.pos.x = NetReader_F32(&rd);
    in.pos.y = NetReader_F32(&rd);
    in.pos.z = NetReader_F32(&rd);
    in.vel.x = NetReader_F32(&rd);
    in.vel.y = NetReader_F32(&rd);
    in.vel.z = NetReader_F32(&rd);
    in.quat.w = NetReader_F32(&rd);
    in.quat.x = NetReader_F32(&rd);
    in.quat.y = NetReader_F32(&rd);
    in.quat.z = NetReader_F32(&rd);
    in.angularMomentum.x = NetReader_F32(&rd);
    in.angularMomentum.y = NetReader_F32(&rd);
    in.angularMomentum.z = NetReader_F32(&rd);
    in.health = (s32)NetReader_U32(&rd);
    in.buttons = NetReader_U32(&rd);
    in.stickX = (s8)NetReader_U8(&rd);
    in.stickY = (s8)NetReader_U8(&rd);
    in.throttle = NetReader_U8(&rd);
    in.brake = NetReader_U8(&rd);
    {
        u8 fire = NetReader_U8(&rd);

        in.fire = fire & 1;
        in.noAmmoCost = (fire & 2) != 0;
    }
    in.slot = NetReader_U8(&rd);
    in.ammo = (s16)NetReader_U16(&rd);
    /* from the network: nothing unusable reaches the physics */
    if (!rd.ok || (r->have && in.seq <= r->seq) ||
        !Sane(in.pos.x, 1e6f) || !Sane(in.pos.y, 1e6f) || !Sane(in.pos.z, 1e6f) ||
        !Sane(in.vel.x, 1e5f) || !Sane(in.vel.y, 1e5f) || !Sane(in.vel.z, 1e5f) ||
        !Sane(in.angularMomentum.x, 1e7f) || !Sane(in.angularMomentum.y, 1e7f) || !Sane(in.angularMomentum.z, 1e7f) ||
        !Sane(in.quat.w, 2.0f) || !Sane(in.quat.x, 2.0f) || !Sane(in.quat.y, 2.0f) || !Sane(in.quat.z, 2.0f)) {
        return;
    }
    mag = sqrtf(in.quat.w * in.quat.w + in.quat.x * in.quat.x + in.quat.y * in.quat.y + in.quat.z * in.quat.z);
    if (mag < 0.5f) {
        return;
    }
    in.quat.w /= mag;
    in.quat.x /= mag;
    in.quat.y /= mag;
    in.quat.z /= mag;
    in.have = 1;
    in.receivedAt = SDL_GetTicks();
    in.buttons |= r->have ? r->buttons : 0;
    in.dead = r->dead;
    in.killed = r->killed;
    in.respawn = r->respawn;
    *r = in;
}

/* Its owner's machine destroyed it. Dying is part of taking an impact: a health at
 * zero alone does nothing until the next one, so this is one, of no force. */
static void KillCar(CarA *car) {
    CollisionInfo hit;

    SDL_zero(hit);
    hit.pos = car->base.pos;
    hit.normal = car->base.axis[1];
    SETP(*(PTR32(void) *)&hit.thingA, car);
    car->base.health = 0;
    THING_VFN(&car->base, THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(void *, void *, s32, f32))(car, &hit, 0, 0.0f);
}

/* From the control reader, each frame, for a car driven on another machine: its
 * owner's inputs, and a pull towards where the owner's last word puts it by now. */
void Port_OnlineRemoteControl(Player *player) {
    extern f32 g_fDeltaTimeSec;
    u32 port;
    RemoteCar *r = NULL;
    CarA *car = GETP(player->vehicle);
    Thing *t;
    NetStats stats;
    f32 lead, dt = g_fDeltaTimeSec, ex, ey, ez, dist, speed, k, sign, mag;

    for (port = 0; port < ONLINE_PLAYERS; port++) {
        if (s_players[port] == player) {
            r = &s_remote[port];
        }
    }
    if (r == NULL) {
        return;
    }
    if (car == NULL) {
        /* fire is what respawns a car, once it has been gone long enough here too */
        player->buttonFire = r->respawn ? 0xFF : 0;
        return;
    }
    r->respawn = 0;
    if (r->dead) {
        if (!r->killed) {
            r->killed = 1;
            KillCar(car);
        }
        return;
    }
    if (!r->have) {
        return;
    }
    player->buttonFire = r->fire ? 0xFF : 0;
    player->stickX = r->stickX;
    player->stickY = r->stickY;
    player->throttle = r->throttle;
    player->brake = r->brake;
    player->buttonsHeld = r->buttons & ~2u;     /* the weapon selected comes as such */
    r->buttons = 0;
    {
        /* shots by ammo: a tap can't fall between two snapshots, and input arriving
         * while the weapon isn't ready doesn't lose one */
        WildCardRack *rack = ThingManagerList_GetByType(&car->motionState.slaves, THING_TYPE_WILDCARD_RACK);

        if (rack != NULL && r->slot < WILDCARD_SLOTS && GETP(rack->slots[r->slot].item) != NULL) {
            WildCardSlot *slot = &rack->slots[r->slot];
            s32 perShot = GETP(GETP(slot->item)->weapon)->ammoPerShot;

            if (rack->cur != r->slot) {
                rack->cur = r->slot;
                slot->scale = 0.0f;
            }
            if (slot->ammo < r->ammo || slot->ammo - r->ammo > MAX_SHOTS_BEHIND * perShot) {
                slot->ammo = r->ammo;
            }
            if (!r->noAmmoCost && !GameSettings_IsCheatEnabled(0)) {
                player->buttonFire = perShot > 0 && slot->ammo - r->ammo >= perShot ? 0xFF : 0;
            }
        }
    }

    t = &car->base;
    t->health = r->health;
    /* the snapshot's age: since it came, and about a round trip of travel, there
     * being two legs through the host between clients */
    lead = (f32)(SDL_GetTicks() - r->receivedAt) / 1000.0f;
    if (Net_GetStats(s_isHost ? (u8)port : NET_PEER_HOST, &stats)) {
        lead += (f32)stats.rttMs / (s_isHost ? 2000.0f : 1000.0f);
    }
    if (lead > MAX_LEAD_S) {
        lead = MAX_LEAD_S;
    }
    ex = r->pos.x + r->vel.x * lead - t->pos.x;
    ey = r->pos.y + r->vel.y * lead - t->pos.y;
    ez = r->pos.z + r->vel.z * lead - t->pos.z;
    dist = sqrtf(ex * ex + ey * ey + ez * ez);
    speed = sqrtf(r->vel.x * r->vel.x + r->vel.y * r->vel.y + r->vel.z * r->vel.z);
    k = dist > SNAP_DISTANCE && dist > speed * 0.5f ? 1.0f : SDL_min(1.0f, PULL_POS * dt);
    t->pos.x += ex * k;
    t->pos.y += ey * k;
    t->pos.z += ez * k;
    k = k == 1.0f ? 1.0f : SDL_min(1.0f, PULL_VEL * dt);
    t->vel.x += (r->vel.x - t->vel.x) * k;
    t->vel.y += (r->vel.y - t->vel.y) * k;
    t->vel.z += (r->vel.z - t->vel.z) * k;
    car->angularMomentum.x = r->angularMomentum.x;
    car->angularMomentum.y = r->angularMomentum.y;
    car->angularMomentum.z = r->angularMomentum.z;
    /* the short way round: q and -q are the same turn */
    sign = t->quat.w * r->quat.w + t->quat.x * r->quat.x + t->quat.y * r->quat.y + t->quat.z * r->quat.z < 0.0f ? -1.0f : 1.0f;
    k = SDL_min(1.0f, PULL_ROT * dt);
    t->quat.w += (sign * r->quat.w - t->quat.w) * k;
    t->quat.x += (sign * r->quat.x - t->quat.x) * k;
    t->quat.y += (sign * r->quat.y - t->quat.y) * k;
    t->quat.z += (sign * r->quat.z - t->quat.z) * k;
    mag = sqrtf(t->quat.w * t->quat.w + t->quat.x * t->quat.x + t->quat.y * t->quat.y + t->quat.z * t->quat.z);
    if (mag > 0.001f) {
        t->quat.w /= mag;
        t->quat.x /= mag;
        t->quat.y /= mag;
        t->quat.z /= mag;
        QuaternionToSubMatrix(&t->quat, THING_MTX(t));
    }
}

/* ---- messages ------------------------------------------------------------------ */

static s32 ClampVehicle(s32 v) {
    return v >= 0 && v <= MAX_VEHICLE ? v : 0;
}

static s32 ClampItem(s32 v) {
    return v >= 0 && v <= MAX_ITEM ? v : NO_ITEM;
}

static void HandleMessage(const NetEvent *e) {
    NetReader r;
    u8 kind;

    NetReader_Init(&r, e->data, e->size);
    kind = NetReader_U8(&r);
    if (s_isHost) {
        /* an id past the game's ports can't get here: the session is capped */
        u32 port = e->peer + 1u;

        if (port > ONLINE_PLAYERS) {
            return;
        }
        if (kind == MSG_CARS) {
            s32 vehicle = (s32)NetReader_U32(&r);
            s32 item = (s32)NetReader_U32(&r);

            if (r.ok && (s_state == STATE_CHOOSING || s_state == STATE_WAITING)) {
                s_vehicle[port - 1] = ClampVehicle(vehicle);
                s_item[port - 1] = ClampItem(item);
                s_chosen |= 1u << (port - 1);
                HostCheckChosen();
            }
        } else if (kind == MSG_READY && s_state == STATE_PLAYING) {
            u32 stage = NetReader_U32(&r);

            if (r.ok && stage > s_ready[port - 1]) {
                s_ready[port - 1] = stage;
                HostCheckGo();
            }
        } else if (kind == MSG_FINISH && s_state == STATE_PLAYING) {
            u32 stage = NetReader_U32(&r);
            u32 elapsed = NetReader_U32(&r);

            /* a player's claim is for its own car, whatever it says */
            if (r.ok && stage == s_stage && !s_hold && s_players[port - 1] != NULL) {
                HostClaim(s_players[port - 1]->port, elapsed);
            }
        } else if (kind == MSG_PICKUP && s_state == STATE_PLAYING) {
            u8 buf[10];
            NetWriter w;
            u32 q, i;
            u16 ammo;

            NetReader_U8(&r);   /* a player's claim is for its own car, whatever it says */
            q = NetReader_U16(&r);
            i = NetReader_U16(&r);
            ammo = NetReader_U16(&r);
            if (r.ok) {
                u32 peer;

                ReceivePickup(port, q, i, (s16)ammo);
                NetWriter_Init(&w, buf, sizeof(buf));
                NetWriter_U8(&w, MSG_PICKUP);
                NetWriter_U8(&w, (u8)port);
                NetWriter_U16(&w, (u16)q);
                NetWriter_U16(&w, (u16)i);
                NetWriter_U16(&w, ammo);
                for (peer = 1; peer < ONLINE_PLAYERS; peer++) {
                    if (peer != e->peer) {
                        Net_SendReliable((u8)peer, buf, w.size);
                    }
                }
            }
        } else if (kind == MSG_CAR_STATE && s_state == STATE_PLAYING) {
            u32 stage;
            u32 alive;

            NetReader_U8(&r);   /* for its own car, whatever it says */
            stage = NetReader_U32(&r);
            alive = NetReader_U8(&r);
            if (r.ok) {
                u8 buf[8];
                NetWriter w;
                u32 peer;

                ReceiveCarState(port, stage, alive);
                NetWriter_Init(&w, buf, sizeof(buf));
                NetWriter_U8(&w, MSG_CAR_STATE);
                NetWriter_U8(&w, (u8)port);
                NetWriter_U32(&w, stage);
                NetWriter_U8(&w, (u8)alive);
                for (peer = 1; peer < ONLINE_PLAYERS; peer++) {
                    if (peer != e->peer) {
                        Net_SendReliable((u8)peer, buf, w.size);
                    }
                }
            }
        } else if (kind == MSG_LEFT_LEVEL) {
            s_lobby |= 1u << (port - 1);
            s_in &= ~(1u << (port - 1));
            s_remote[port - 1].have = 0;
            HostCheckGo();
        }
    } else if (e->peer == NET_PEER_HOST) {
        if (kind == MSG_SETUP && s_state == STATE_LOBBY) {
            ClientSetup(&r);
        } else if (kind == MSG_ABORT && (s_state == STATE_CHOOSING || s_state == STATE_WAITING)) {
            s_state = STATE_LOBBY;
            ShowLobby("Waiting...");
            Port_InterfaceGoto(Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_CLIENT));
        } else if (kind == MSG_GO && s_state == STATE_PLAYING) {
            u32 stage = NetReader_U32(&r);

            if (r.ok && stage > s_goStage) {
                s_goStage = stage;
                if (s_hold && s_goStage >= s_stage) {
                    Release();
                }
            }
        } else if (kind == MSG_WINNER && s_state == STATE_PLAYING) {
            u32 stage = NetReader_U32(&r);
            u32 number = NetReader_U8(&r);

            if (r.ok && stage == s_stage && !s_decided && number >= 1 && number <= ONLINE_PLAYERS) {
                AnnounceWinner(number);
            }
        } else if (kind == MSG_PICKUP && s_state == STATE_PLAYING) {
            u32 port = NetReader_U8(&r);
            u32 q = NetReader_U16(&r);
            u32 i = NetReader_U16(&r);
            u16 ammo = NetReader_U16(&r);

            if (r.ok) {
                ReceivePickup(port, q, i, (s16)ammo);
            }
        } else if (kind == MSG_CAR_STATE && s_state == STATE_PLAYING) {
            u32 port = NetReader_U8(&r);
            u32 stage = NetReader_U32(&r);
            u32 alive = NetReader_U8(&r);

            if (r.ok) {
                ReceiveCarState(port, stage, alive);
            }
        } else if (kind == MSG_CONTINUE && s_state == STATE_PLAYING) {
            s_continue = 1;
        } else if (kind == MSG_RETURN && s_state == STATE_PLAYING) {
            if (StateManager_CurrState() == 5) {
                ExitStateGamePlay(2);
            } else if (StateManager_CurrState() != 2) {
                StateManager_QueueChange(2);
            }
        } else if (kind == MSG_START && (s_state == STATE_CHOOSING || s_state == STATE_WAITING)) {
            u32 in = NetReader_U8(&r);
            u32 seed = NetReader_U32(&r);
            s32 vehicle[ONLINE_PLAYERS], item[ONLINE_PLAYERS];
            u32 i;

            for (i = 0; i < ONLINE_PLAYERS; i++) {
                vehicle[i] = ClampVehicle((s32)NetReader_U32(&r));
                item[i] = ClampItem((s32)NetReader_U32(&r));
            }
            if (r.ok && (in & (1u << (s_port - 1))) && s_state == STATE_WAITING) {
                SDL_memcpy(s_vehicle, vehicle, sizeof(vehicle));
                SDL_memcpy(s_item, item, sizeof(item));
                s_in = in;
                s_levelSeed = seed;
                StartLevel();
            }
        }
    }
}

void Port_OnlineHeartbeat(void) {
    NetEvent e;

    Port_MenuOnlineUpdate();
    if (s_state == STATE_NONE) {
        return;
    }
    while (s_state != STATE_NONE && Net_Poll(&e)) {
        switch (e.type) {
        case NET_EV_CONNECTED:
            s_port = e.peer + 1;
            if (s_port > ONLINE_PLAYERS) {
                SessionLost("Game is full");
                return;
            }
            s_present |= 1u << (s_port - 1);
            s_state = STATE_LOBBY;
            Port_Log("online: connected as port %d", s_port);
            {
                char address[128];

                Port_MenuOnlineAddress(address, sizeof(address));
                Port_SaveOnlineAddress(address);
            }
            ShowLobby("Waiting...");
            break;
        case NET_EV_PEER_JOINED:
            if (e.peer < ONLINE_PLAYERS) {
                s_present |= 1u << e.peer;
                Port_Log("online: port %d joined", e.peer + 1);
                ShowLobby(s_isHost ? "Ready" : NULL);
            }
            break;
        case NET_EV_PEER_LEFT:
            if (e.peer < ONLINE_PLAYERS) {
                s_present &= ~(1u << e.peer);
                s_in &= ~(1u << e.peer);
                s_remote[e.peer].have = 0;
                Port_Log("online: port %d left: %s", e.peer + 1, Net_ErrorName(e.error));
                ShowLobby(s_isHost && Port_OnlinePlayerCount() < 2 ? "Waiting..." : NULL);
                HostCheckChosen();
                if (s_state == STATE_PLAYING) {
                    HostCheckGo();
                }
            }
            break;
        case NET_EV_RELIABLE:
            HandleMessage(&e);
            break;
        case NET_EV_LATEST: {
            /* the key is whose car it is: from a client, only its own */
            u32 port = e.key + 1u;

            if (s_state == STATE_PLAYING && port <= ONLINE_PLAYERS && (s32)port != s_port &&
                (!s_isHost || e.peer + 1u == port)) {
                ReceiveSnapshot(port, &e);
                if (s_isHost) {
                    u32 peer;

                    for (peer = 1; peer < ONLINE_PLAYERS; peer++) {
                        if (peer != e.peer && ((s_in >> peer) & 1)) {
                            Net_SendLatest((u8)peer, e.key, e.data, e.size);
                        }
                    }
                }
            }
            break;
        }
        case NET_EV_DISCONNECTED:
            SessionLost(Net_ErrorName(e.error));
            return;
        default:
            break;
        }
    }
    /* the level ended: everyone's back on the title state's main menu. Starting
     * one only queues the change, so it counts once the title state has been left. */
    if (s_state == STATE_PLAYING && StateManager_CurrState() != 2) {
        u64 now = SDL_GetTicks();

        s_inLevel = 1;
        if (StateManager_CurrState() == 5) {
            SendSnapshot(now);
            SummaryHeartbeat(now);
            if (s_isHost && s_hold) {
                HostCheckGo();  /* for the time limit */
                if (s_goAt != 0 && now >= s_goAt) {
                    Release();
                }
            }
            if (s_isHost && s_claimDeadline != 0 && now >= s_claimDeadline) {
                HostDecide();
            }
        }
    } else if (s_state == STATE_PLAYING && s_inLevel) {
        u8 msg = s_isHost ? MSG_RETURN : MSG_LEFT_LEVEL;

        Net_SendReliable(s_isHost ? NET_PEER_ALL : NET_PEER_HOST, &msg, 1);
        s_lobby = 0xF;
        s_localPlayer = NULL;
        s_hold = 0;
        s_menuOpen = 0;
        s_inLevel = 0;
        s_state = STATE_LOBBY;
        ShowLobby(s_isHost ? "Ready" : "Waiting...");
        Port_InterfaceGoto(Port_MenuOnlineScreen(s_isHost ? ONLINE_SCREEN_LOBBY_HOST : ONLINE_SCREEN_LOBBY_CLIENT));
    }
}
