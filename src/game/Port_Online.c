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
 * Not yet: cars don't move on other machines (remote cars just sit at their spawn);
 * AI opponents are off because each machine would run its own. */
#include <SDL3/SDL.h>

#include "common.h"
#include "CarA.h"
#include "World.h"
#include "Interface.h"
#include "Port_Online.h"
#include "net.h"

#define ONLINE_PLAYERS 4        /* the game's ports; see NET_DEFAULT_PLAYERS */
#define MAX_VEHICLE 32          /* the car select's list */
#define MAX_ITEM 0x60
#define NO_ITEM (-1)

enum {
    MSG_SETUP = 1,              /* host: u8 progression, u16 level, u8 ports in, the settings */
    MSG_ABORT,                  /* host: left the car select */
    MSG_CARS,                   /* client: s32 vehicle, s32 item */
    MSG_START,                  /* host: u8 ports in, then s32 vehicle, s32 item per port */
    MSG_LOADED,                 /* client: its level is ready */
};

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
static u32 s_loaded;
static s32 s_vehicle[ONLINE_PLAYERS], s_item[ONLINE_PLAYERS];
static char s_hostLine[64];
static int s_inLevel;

void GameSettings_PortOnlineBegin(void);
void GameSettings_PortOnlineEnd(void);

int Port_OnlineActive(void) {
    return s_state != STATE_NONE;
}

int Port_OnlineIsRemote(const Player *player) {
    return s_state != STATE_NONE && player->port != s_port;
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

    SDL_snprintf(text, sizeof(text), "Players: %d of %d", Port_OnlinePlayerCount(), ONLINE_PLAYERS);
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
    s_port = isHost ? 1 : 0;
    s_in = s_chosen = s_loaded = 0;
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
    NetWriter_U8(&w, (u8)s_present);
    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        NetWriter_U32(&w, GameSettings_GetCodeVariable(s_settings[i], 0));
    }
    s_in = s_present;
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
    s_loaded = 0;
    s_inLevel = 0;
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

void Port_OnlineSetupPlayer(Player *player) {
    if (s_state != STATE_NONE && player->port == s_port) {
        player->device = 0;     /* port 2 here is still this machine's first pad */
    }
}

static void HostCheckLoaded(void) {
    if (s_isHost && (s_loaded & s_in) == s_in) {
        Port_Log("online: every player is in the level");
    }
}

void Port_OnlineLevelStarted(void) {
    extern s32 g_GameProgressionID;

    if (s_state == STATE_NONE) {
        return;
    }
    Port_Log("online: level ready: progression %d, port %d, %d human cars", g_GameProgressionID, s_port,
             PlayerManager_PlayerHumanCount());
    s_loaded |= 1u << (s_port - 1);
    if (s_isHost) {
        HostCheckLoaded();
    } else {
        u8 msg = MSG_LOADED;

        Net_SendReliable(NET_PEER_HOST, &msg, 1);
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
        } else if (kind == MSG_LOADED && s_state == STATE_PLAYING) {
            s_loaded |= 1u << (port - 1);
            HostCheckLoaded();
        }
    } else if (e->peer == NET_PEER_HOST) {
        if (kind == MSG_SETUP && s_state == STATE_LOBBY) {
            ClientSetup(&r);
        } else if (kind == MSG_ABORT && (s_state == STATE_CHOOSING || s_state == STATE_WAITING)) {
            s_state = STATE_LOBBY;
            ShowLobby("Waiting for host");
            Port_InterfaceGoto(Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_CLIENT));
        } else if (kind == MSG_START && (s_state == STATE_CHOOSING || s_state == STATE_WAITING)) {
            u32 in = NetReader_U8(&r);
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
            ShowLobby("Waiting for host");
            break;
        case NET_EV_PEER_JOINED:
            if (e.peer < ONLINE_PLAYERS) {
                s_present |= 1u << e.peer;
                Port_Log("online: port %d joined", e.peer + 1);
                ShowLobby(s_isHost ? "Ready to start" : NULL);
            }
            break;
        case NET_EV_PEER_LEFT:
            if (e.peer < ONLINE_PLAYERS) {
                s_present &= ~(1u << e.peer);
                s_in &= ~(1u << e.peer);
                Port_Log("online: port %d left: %s", e.peer + 1, Net_ErrorName(e.error));
                ShowLobby(s_isHost && Port_OnlinePlayerCount() < 2 ? "Waiting..." : NULL);
                HostCheckChosen();
            }
            break;
        case NET_EV_RELIABLE:
            HandleMessage(&e);
            break;
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
        s_inLevel = 1;
    } else if (s_state == STATE_PLAYING && s_inLevel) {
        s_inLevel = 0;
        s_state = STATE_LOBBY;
        ShowLobby(s_isHost ? "Ready to start" : "Waiting for host");
        Port_InterfaceGoto(Port_MenuOnlineScreen(s_isHost ? ONLINE_SCREEN_LOBBY_HOST : ONLINE_SCREEN_LOBBY_CLIENT));
    }
}
