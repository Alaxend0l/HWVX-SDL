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
 * Scores: a car's stunt points are its owner's, and travel with its snapshots. A kill
 * happens where the victim is: its machine tells the others whose it was. Only the host
 * checks a round's targets and timer, and sends the winner with the scores, so every
 * machine tallies the same round.
 *
 * Joyride's worlds have one spawn, which their scripts name directly. So every car
 * spawns from a list of our own (Port_OnlineExpandSpawns), at spots around it that the
 * world's collision says are clear (ChooseSpawns), and the world's
 * spawn keeps pointing at this machine's car: the scripts' music, effects and pickups
 * answer the player here. Its key and gears, the items its scripts watch, count for every
 * player: one taken is taken by all (ReceivePickup). What else a world does isn't shared in
 * Joyride, nor needs to be.
 *
 * Eight players: the retail game has 4 player ports, and its worlds 4 spawns for them.
 * Online a port is 1 .. 8 (Port_OnlineHumanPorts, which the game's own checks of "a
 * player's port" ask). Players 5 and up start where a Battle world has its AI cars
 * (three in each, unused online), and where a world has no spawn left, at a spot behind
 * one of the first four, found from the world's collision (Port_OnlineExpandSpawns,
 * ChooseExtraSpawns): those spawns are ours, kept after the world's list
 * (World_GetTileCar). A script naming every one of the world's player spawns means ours
 * too (Port_OnlineExtraSubject); a Drag Race's finish, which its script has once per
 * player, is found for them from the others' (FinishHeartbeat). The round summary's
 * rows are the retail four: Port_OnlineScoreLine has every player's for the lines
 * Port_Menu.c draws over it.
 *
 * Not yet: AI opponents are off because each machine would run its own; a weapon a car
 * dropped isn't shared if picked up again. */
#include "Port_SpawnBox.h"
#include <SDL3/SDL.h>
#include <math.h>

#include "common.h"
#include "Port_Content.h"
#include "Port_Mods.h"
#include "Port_Proto.h"
#include "CarA.h"
#include "World.h"
#include "Interface.h"
#include "ThingManager.h"
#include "WildCard.h"
#include "Port_Online.h"
#include "Port_OnlineProtocol.h"
#include "Trigger.h"
#include "net.h"

#define ONLINE_PLAYERS ONLINE_MAX_PLAYERS
#define WORLD_OF_LEVEL() ((World *)GETP(g_worldData.world))
#define PROGRESSION_JOYRIDE 4
#define PROGRESSION_MP_DRAG 5
#define PROGRESSION_MP_BATTLE 7
/* Joyride: where the others may start around the world's spawn. */
#define MAX_SHARED_ITEMS 64
#define SPAWN_PROBE_ABOVE 15.0f /* the ground is looked for from this far above the spawn */
#define SPAWN_PROBE_DOWN 40.0f
#define SPAWN_MAX_STEP 5.0f     /* difference from the spawn's ground */
#define SPAWN_MIN_FLAT 0.9f     /* the ground's normal, upwards */
/* Every car's collision box at once, the data's cars (Port_SpawnBox.h: the same on every
 * machine whatever its mods): spots are found for it, so every car fits. */
static f32 SpawnCarSize(int axis) {
    f32 lo[3], hi[3];

    Port_SpawnEnvelope(lo, hi, NULL, 0);
    return hi[axis] - lo[axis];
}
#define SPAWN_CAR_WIDTH SpawnCarSize(0)
#define SPAWN_CAR_LENGTH SpawnCarSize(2)
#define SPAWN_CAR_HALF_DIAGONAL (0.5f * sqrtf(SPAWN_CAR_WIDTH * SPAWN_CAR_WIDTH + SPAWN_CAR_LENGTH * SPAWN_CAR_LENGTH))
#define SPAWN_CLEARANCE (SPAWN_CAR_HALF_DIAGONAL + 10.0f)   /* free on every side, room to turn */
#define SPAWN_GAP 8.0f          /* between two cars' boxes */
/* If nowhere has that, a tight world still gets a spot, nearer the others. */
#define SPAWN_TIGHT_CLEARANCE (SPAWN_CAR_HALF_DIAGONAL + 1.0f)
#define SPAWN_TIGHT_GAP 2.0f
#define SPAWN_BODY_HEIGHT 6.0f  /* walls are looked for this far above the ground */
#define NO_ITEM (-1)
#define DRAG_MAX_PLAYERS 4      /* the retail grid; see Port_OnlineModeAllowed */
#define CHOICE_MAX (5 + 255)    /* Port_ContentWriteChoice: number, length, an id of up to 255 */


#define SNAPSHOT_MS 33          /* how often a car tells the others about itself */
#define HOLD_MAX_MS 30000       /* a machine this late doesn't hold up the others */
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
    u32 points;
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
static int s_tunnel;            /* hosting through a tunnel */
static int s_tunnelReady;
static char s_roomCode[8];
static char s_invite[128];      /* name/CODE */
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
static TileCar s_spawns[ONLINE_PLAYERS];
static TileCar *s_scriptSpawn;  /* Joyride: the world's own, when s_spawns is its list */
/* Battle, Drag Race: spawns for players 5-8, after the world's list (World_GetTileCar),
 * each placed near a spawn of the world's. */
static TileCar s_extra[ONLINE_PLAYERS];
static const TileCar *s_extraBase[ONLINE_PLAYERS];
static u32 s_numExtra;
/* The world's AI spawns that players 5-8 start at instead (Battle). */
static struct {
    const TileCar *tile;
    u8 port;
} s_borrowed[ONLINE_PLAYERS];
static u32 s_numBorrowed;
/* Mods: this machine's (MSG_HELLO); the host's word on each port. */
static u32 s_modsCount;
static u64 s_modsHash;
static char s_modsSummary[MODS_SUMMARY_MAX + 1];
static u32 s_verified;          /* host: ports whose mods match its own */
static u32 s_refused;           /* host: ports whose mods don't */
static int s_helloSent;
/* HWVX_ONLINE_TEST: a session run without anyone at the keyboard (TestHeartbeat). */
static struct {
    int host;
    u32 players, progression, level, car, rounds, roundMs, summaryMs, lobbyMs;
    int started, setUp;
    u64 menuSince, lobbySince;
    u64 nextSync, carsSentStage;
} s_test;
static const QuadItem *s_sharedTaken[MAX_SHARED_ITEMS];
static u32 s_numSharedTaken;

extern CodeVariables g_codeVariables;   /* GameSettings.c */
void GameSettings_PortOnlineBegin(void);
static void PatchSummaryLabels(void);
/* The round summary's row labels, given P5-P8 (PatchSummaryLabels). */
static struct {
    IfaceText *text;
    u32 original;               /* the list's handle as the pack has it */
} s_labels[8];
static u32 s_numLabels;
static void UnpatchSummaryLabels(void);
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

/* How many of a world's spawns are players': the retail game's 4, online 8. */
u32 Port_OnlineHumanPorts(void) {
    return s_state != STATE_NONE ? ONLINE_PLAYERS : RETAIL_PLAYERS;
}

static int Bits(u32 mask) {
    int n = 0;

    for (; mask != 0; mask &= mask - 1) {
        n++;
    }
    return n;
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

    if (s_isHost && (s_refused & s_present) != 0) {
        /* in the session, but with other mods: they won't be in a game */
        SDL_snprintf(text, sizeof(text), "Players: %d/%d, %d other mods", Port_OnlinePlayerCount(), ONLINE_PLAYERS,
                     Bits(s_refused & s_present));
    } else {
        SDL_snprintf(text, sizeof(text), "Players: %d/%d", Port_OnlinePlayerCount(), ONLINE_PLAYERS);
    }
    if (s_tunnel) {
        char name[128], code[32];
        char *slash;

        /* the name over as many lines as it takes, the code clear of it: someone may be
         * reading this out, and a name of four long words fills all it gets */
        SDL_strlcpy(name, s_tunnelReady ? s_invite : "Starting tunnel...", sizeof(name));
        if ((slash = SDL_strchr(name, '/')) != NULL) {
            *slash = 0;
        }
        if (s_tunnelReady) {
            SDL_snprintf(code, sizeof(code), "Code %s %d/%d", s_roomCode, Port_OnlinePlayerCount(), ONLINE_PLAYERS);
        } else {
            SDL_strlcpy(code, text, sizeof(code));
        }
        Port_MenuOnlineSetLines(ONLINE_SCREEN_LOBBY_TUNNEL, 0, 4, name);
        Port_MenuOnlineSetLine(ONLINE_SCREEN_LOBBY_TUNNEL, 4, code);
    } else if (s_isHost) {
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
    s_tunnel = 0;
    s_tunnelReady = 0;
    s_present = isHost ? 1 : 0;
    s_lobby = (1u << ONLINE_PLAYERS) - 1;
    s_hold = 0;
    s_localPlayer = NULL;
    s_port = isHost ? 1 : 0;
    s_in = s_chosen = 0;
    s_verified = isHost ? 1 : 0;
    s_refused = 0;
    s_helloSent = 0;
    s_numExtra = 0;
    s_numBorrowed = 0;
}

int Port_OnlineStartHost(void) {
    char address[48];
    NetError error = Net_Host(NET_DEFAULT_PORT, NULL, 0, ONLINE_PLAYERS);

    if (error != NET_OK) {
        Port_Log("online: %s", Net_ErrorName(error));
        Port_MenuOnlineSetLines(ONLINE_SCREEN_ONLINE, 0, 2, Net_ErrorName(error));
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

/* On loopback only: nobody reaches this but through the tunnel, whose address is public,
 * so a room code is asked for as well. */
int Port_OnlineStartTunnelHost(void) {
    NetError error;

    if (!Port_TunnelAvailable() || !Net_RandomCode(s_roomCode, 6)) {
        return 0;
    }
    error = Net_Host(NET_DEFAULT_PORT, s_roomCode, 1, ONLINE_PLAYERS);
    if (error != NET_OK) {
        Port_Log("online: %s", Net_ErrorName(error));
        return 0;
    }
    if (!Port_TunnelStart(NET_DEFAULT_PORT)) {
        Net_Close();
        return 0;
    }
    BeginSession(1);
    s_tunnel = 1;
    s_state = STATE_LOBBY;
    Port_Log("online: starting a tunnel");
    ShowLobby(NULL);
    return 1;
}

int Port_OnlineInvite(char *out, size_t size) {
    if (s_state == STATE_NONE || !s_tunnel || !s_tunnelReady) {
        return 0;
    }
    SDL_strlcpy(out, s_invite, size);
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
    if (StateManager_CurrState() == 5) {
        UnpatchSummaryLabels();
    }
    s_numLabels = 0;
    Net_Close();
    Port_TunnelStop();
    s_tunnel = 0;
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
        Port_MenuOnlineSetLines(ONLINE_SCREEN_ONLINE, 0, 2, why);
        if (!Port_InterfaceGoBack(Port_MenuOnlineScreen(ONLINE_SCREEN_ONLINE))) {
            Port_InterfaceGoBack(Port_MenuOnlineScreen(ONLINE_SCREEN_MAIN));
        }
    }
}

/* ---- mods: players with other mods don't play together ------------------------ */

/* The enabled mods, ids and versions in load order (Port_Mods.h); none if none change the game.
 * Then the PS2 prototype's import mode and which prototype (Port_ProtoIdentity), when it is
 * in the game: "replace" drives its cars' physics under the release's numbers, the others
 * add cars and tracks the other machine must have the same. */
static void ModsFingerprint(void) {
    int i;

    s_modsCount = 0;
    s_modsHash = 0;
    SDL_strlcpy(s_modsSummary, "no mods", sizeof(s_modsSummary));
    s_modsHash = ONLINE_FNV_BASIS;
    s_modsSummary[0] = 0;
    for (i = 0; Port_ModsActive() && i < Port_ModsCount(); i++) {
        const PortModInfo *m = Port_ModsInfo(i);
        char one[96];

        if (m == NULL || !m->enabled) {
            continue;
        }
        s_modsHash = Online_Fnv1a(s_modsHash, m->id);
        s_modsHash = Online_Fnv1a(s_modsHash, "@");
        s_modsHash = Online_Fnv1a(s_modsHash, m->version != NULL ? m->version : "");
        s_modsHash = Online_Fnv1a(s_modsHash, "\n");
        SDL_snprintf(one, sizeof(one), "%s%s %s", s_modsCount > 0 ? ", " : "", m->name != NULL && *m->name ? m->name : m->id,
                     m->version != NULL ? m->version : "");
        SDL_strlcat(s_modsSummary, one, sizeof(s_modsSummary));
        s_modsCount++;
    }
    if (s_modsCount == 0) {
        s_modsHash = 0;
        SDL_strlcpy(s_modsSummary, "no mods", sizeof(s_modsSummary));
    }
    if (Port_ProtoMode() != PORT_PROTO_OFF) {
        char one[96];

        s_modsHash = Online_Fnv1a(s_modsHash != 0 ? s_modsHash : ONLINE_FNV_BASIS, "proto:");
        s_modsHash = Online_Fnv1a(s_modsHash, Port_ProtoModeKey(Port_ProtoMode()));
        s_modsHash = Online_Fnv1a(s_modsHash, ":");
        s_modsHash = Online_Fnv1a(s_modsHash, Port_ProtoIdentity());
        SDL_snprintf(one, sizeof(one), ", PS2 prototype (%s, %.6s)", Port_ProtoModeKey(Port_ProtoMode()), Port_ProtoIdentity());
        SDL_strlcat(s_modsSummary, one, sizeof(s_modsSummary));
    }
}

static void SendHello(u8 peer) {
    u8 buf[16 + MODS_SUMMARY_MAX];
    NetWriter w;
    u32 len;

    ModsFingerprint();
    len = (u32)SDL_strlen(s_modsSummary);
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_HELLO);
    NetWriter_U8(&w, (u8)SDL_min(s_modsCount, 255u));
    NetWriter_U32(&w, (u32)s_modsHash);
    NetWriter_U32(&w, (u32)(s_modsHash >> 32));
    NetWriter_U8(&w, (u8)len);
    NetWriter_Bytes(&w, s_modsSummary, len);
    Net_SendReliable(peer, buf, w.size);
}

/* Another machine's mods. The host leaves a port with others out of every game; a client
 * with the host's mods differing from its own leaves, saying so. */
static void ReceiveHello(u32 port, NetReader *r) {
    u32 count = NetReader_U8(r);
    u64 hash = NetReader_U32(r);
    u32 len;
    const u8 *text;
    char theirs[MODS_SUMMARY_MAX + 1];

    hash |= (u64)NetReader_U32(r) << 32;
    len = NetReader_U8(r);
    text = NetReader_Take(r, len);
    if (!r->ok || len > MODS_SUMMARY_MAX) {
        return;
    }
    SDL_memcpy(theirs, text, len);
    theirs[len] = 0;
    ModsFingerprint();
    if (count == s_modsCount && hash == s_modsHash) {
        if (s_isHost) {
            s_verified |= 1u << (port - 1);
            s_refused &= ~(1u << (port - 1));
            Port_Log("online: port %u: the same mods (%s)", port, s_modsSummary);
        }
        return;
    }
    Port_Log("online: %s has other mods: %s; here: %s", s_isHost ? "a player" : "the host", theirs, s_modsSummary);
    if (s_isHost) {
        char line[64];

        s_verified &= ~(1u << (port - 1));
        s_refused |= 1u << (port - 1);
        SDL_snprintf(line, sizeof(line), "Player %u has other mods or prototype", port);
        ShowLobby(line);
    } else {
        char why[2 * MODS_SUMMARY_MAX + 64];

        SDL_snprintf(why, sizeof(why), "Mods or prototype differ. Host: %s. Here: %s", theirs, s_modsSummary);
        SessionLost(why);
    }
}

/* ---- choosing -------------------------------------------------------------- */

/* Drag Race is for 4 at most: its worlds have the retail grid of 4 and its scripts a
 * finish for each (Battle and Joyride take 8). */
int Port_OnlineModeAllowed(s32 progression) {
    if (s_state == STATE_NONE || progression != PROGRESSION_MP_DRAG ||
        Bits(s_present & s_lobby & s_verified) <= DRAG_MAX_PLAYERS) {
        return 1;
    }
    Port_Log("online: Drag Race: %d players in, %d at most", Bits(s_present & s_lobby & s_verified), DRAG_MAX_PLAYERS);
    Port_MenuOnlineSetLine(ONLINE_SCREEN_MODE, 0, "Drag Race: 4 players at most");
    return 0;
}

int Port_OnlineHostSetup(void) {
    u8 buf[64 + 256];
    NetWriter w;
    s32 id, level, track, sent;
    char trackId[256];
    u32 i;

    if (s_state == STATE_NONE || !s_isHost) {
        return 1;
    }
    StateProgressionManager_GetProgressionID(&id, &level);
    if (!Port_OnlineModeAllowed(id)) {
        return 0;
    }
    /* each machine would run its own AI: none until they are shared */
    GameSettings_SetCodeVariable(0x133, 0, 0);
    StateProgressionManager_GetProgressionID(&id, &level);
    /* a registered track's level is numbered by each machine's ids.lock: it goes by id,
     * with the level of the track it is made from for a machine without it */
    trackId[0] = 0;
    sent = level;
    if ((track = Port_ContentTrackOfLevel((u32)id, (u32)level)) >= 0 &&
        Port_ContentIdOf(PORT_CONTENT_TRACK, (u32)track, trackId, sizeof(trackId))) {
        s32 base = Port_ContentDataOf(PORT_CONTENT_TRACK, (u32)track);

        sent = base >= 0 ? Port_ContentLevelOfTrack((u32)id, (u32)base) : -1;
        if (sent < 0) {
            sent = 0;
        }
    } else {
        trackId[0] = 0;
    }
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_SETUP);
    NetWriter_U8(&w, (u8)id);
    NetWriter_U16(&w, (u16)sent);
    /* a player whose mods aren't the host's isn't in it (MSG_HELLO) */
    NetWriter_U8(&w, (u8)(s_present & s_lobby & s_verified));
    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        NetWriter_U32(&w, GameSettings_GetCodeVariable(s_settings[i], 0));
    }
    NetWriter_U8(&w, (u8)SDL_strlen(trackId));
    NetWriter_Bytes(&w, trackId, (u32)SDL_strlen(trackId));
    s_in = s_present & s_lobby & s_verified;
    s_chosen = 0;
    s_state = STATE_CHOOSING;
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    Port_Log("online: setting up progression %d level %d (%s) for ports %x", id, level, trackId[0] ? trackId : "its own", s_in);
    return 1;
}

static void ClientSetup(NetReader *r) {
    u32 id = NetReader_U8(r);
    u32 level = NetReader_U16(r);
    u32 in = NetReader_U8(r);
    s32 values[SDL_arraysize(s_settings)];
    u32 i;

    u32 len;
    const u8 *bytes;
    char missing[320];

    missing[0] = 0;
    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        values[i] = (s32)NetReader_U32(r);
    }
    len = NetReader_U8(r);
    bytes = len > 0 ? NetReader_Take(r, len) : NULL;
    if (r->ok && bytes != NULL) {
        /* a registered track: its level here, else the level it is made from */
        char trackId[256];
        u32 track;
        s32 here;

        SDL_memcpy(trackId, bytes, len);
        trackId[len] = 0;
        if (Port_ContentIndexOf(PORT_CONTENT_TRACK, trackId, &track) && (here = Port_ContentLevelOfTrack(id, track)) >= 0) {
            level = (u32)here;
        } else {
            /* not another world in its place: the cars would stand somewhere else here */
            SDL_snprintf(missing, sizeof(missing), "The host's track isn't here: %s", trackId);
        }
    }
    if (!r->ok || (id != PROGRESSION_MP_DRAG && id != PROGRESSION_MP_BATTLE && id != PROGRESSION_JOYRIDE) ||
        !(in & (1u << (s_port - 1)))) {
        return;
    }
    if (missing[0] != 0) {
        SessionLost(missing);
        return;
    }
    for (i = 0; i < SDL_arraysize(s_settings); i++) {
        if (s_settings[i] == 0x12E) {
            /* the round time reads in ms but is set in the menu's 5-minute steps: as read */
            g_codeVariables.versusRoundTime = values[i];
        } else {
            GameSettings_SetCodeVariable(s_settings[i], 0, values[i]);
        }
    }
    GameSettings_SetCodeVariable(0x133, 0, 0);
    StateProgressionManager_SetProgressionID((s32)id, (s32)level);
    s_in = in;
    s_state = STATE_CHOOSING;
    Port_Log("online: setting up progression %u level %u for ports %x", id, level, in);
    Port_InterfaceGoto(Port_MenuOnlineScreen(id == PROGRESSION_JOYRIDE ? ONLINE_SCREEN_CAR_SELECT_JOYRIDE : ONLINE_SCREEN_CAR_SELECT));
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
    u8 buf[16 + ONLINE_PLAYERS * 2 * CHOICE_MAX];
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
        Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, s_vehicle[i]);
        Port_ContentWriteChoice(&w, PORT_CONTENT_ITEM, s_item[i]);
    }
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    StartLevel();
}

int Port_OnlineCarsChosen(Interface *iface) {
    /* other screens end the same way: the race's own HUD does */
    if (s_state != STATE_CHOOSING || (GETP(iface->header) != Port_MenuOnlineScreen(ONLINE_SCREEN_CAR_SELECT) &&
                                      GETP(iface->header) != Port_MenuOnlineScreen(ONLINE_SCREEN_ITEM_SELECT_JOYRIDE))) {
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
        u8 buf[8 + 2 * CHOICE_MAX];
        NetWriter w;

        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_CARS);
        Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, s_vehicle[s_port - 1]);
        Port_ContentWriteChoice(&w, PORT_CONTENT_ITEM, s_item[s_port - 1]);
        Net_SendReliable(NET_PEER_HOST, buf, w.size);
    }
    return 1;
}

int Port_OnlineBack(const InterfaceHeader *header) {
    if (s_state == STATE_NONE) {
        return 0;
    }
    if (header == Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_HOST) || header == Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_CLIENT) ||
        header == Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_TUNNEL)) {
        Port_OnlineEnd();
        return 0;
    }
    if (header == Port_MenuOnlineScreen(ONLINE_SCREEN_WAITING)) {
        return 1;
    }
    if (header == Port_MenuOnlineScreen(ONLINE_SCREEN_CAR_SELECT) || header == Port_MenuOnlineScreen(ONLINE_SCREEN_CAR_SELECT_JOYRIDE)) {
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

/* A borrowed spawn's player port, our spawns' own (s_spawns, s_extra). */
static int IsOurs(const TileCar *t) {
    return (t >= s_spawns && t < s_spawns + ONLINE_PLAYERS) || (t >= s_extra && t < s_extra + ONLINE_PLAYERS);
}

u32 Port_OnlineTilePort(const TileCar *t) {
    u32 i;

    if (s_state == STATE_NONE) {
        return t->port;
    }
    for (i = 0; i < s_numBorrowed; i++) {
        if (s_borrowed[i].tile == t) {
            return s_borrowed[i].port;
        }
    }
    if (t->port <= RETAIL_PLAYERS || IsOurs(t)) {
        return t->port;
    }
    return 0xFF;                /* an AI spawn: no AI online */
}

u32 Port_OnlineExtraTileCount(void) {
    return s_state != STATE_NONE ? s_numExtra : 0;
}

TileCar *Port_OnlineExtraTile(u32 index) {
    return s_state != STATE_NONE && index < s_numExtra ? &s_extra[index] : NULL;
}

void *Port_OnlineSpawnOf(u32 port) {
    u32 i;

    for (i = 0; s_state != STATE_NONE && i < World_GetTotalTiles_Car(); i++) {
        TileCar *t = World_GetTileCar(i);

        if (Port_OnlineTilePort(t) == port) {
            return t;
        }
    }
    return NULL;
}

/* The world's spawn for player `port` (1-4), as the pack has it. */
static TileCar *WorldSpawn(World *world, u32 port) {
    u32 i;

    for (i = 0; i < world->numTileCars; i++) {
        if (GETP(world->tileCars)[i].port == port) {
            return &GETP(world->tileCars)[i];
        }
    }
    return NULL;
}

u32 Port_OnlineListExtras(const u32 *subjects, u32 n, TileCar **out) {
    World *world;
    u32 i, j, humans = 0;

    if (s_state == STATE_NONE || s_numExtra == 0 || subjects == NULL || n == 0) {
        return 0;
    }
    world = WORLD_OF_LEVEL();
    for (i = 0; i < world->numTileCars; i++) {
        TileCar *t = &GETP(world->tileCars)[i];
        int named = 0;

        if (t->port < 1 || t->port > RETAIL_PLAYERS) {
            continue;
        }
        humans++;
        for (j = 0; j < n && !named; j++) {
            named = subjects[j] == (u32)HND(t);
        }
        if (!named) {
            return 0;
        }
    }
    if (humans == 0) {
        return 0;
    }
    for (i = 0; i < s_numExtra; i++) {
        out[i] = &s_extra[i];
    }
    return s_numExtra;
}

/* LoadWorldProcess: a world with a spawn for one gets more; one with the retail 4 gets
 * spawns for players 5-8: its AI spawns first (Battle has three), then ours, placed
 * when the collision is in (ChooseExtraSpawns). Pack data the loading goes on to fill
 * in: the car's and its item's, by port. */
void Port_OnlineExpandSpawns(World *world) {
    extern s32 g_GameProgressionID;
    TileCar *first = GETP(world->tileCars);
    u32 i, port;

    s_numExtra = 0;
    s_numBorrowed = 0;
    if (s_state == STATE_NONE || first == NULL) {
        s_scriptSpawn = NULL;
        return;
    }
    if (g_GameProgressionID != PROGRESSION_JOYRIDE) {
        s_scriptSpawn = NULL;
        for (port = RETAIL_PLAYERS + 1; port <= ONLINE_PLAYERS; port++) {
            TileCar *tile = NULL, *base, *e;

            if (!((s_in >> (port - 1)) & 1)) {
                continue;
            }
            /* a player spawn nobody has (4 players or fewer start where the retail game's
             * do), then an AI spawn */
            for (i = 0; i < world->numTileCars && tile == NULL; i++) {
                if (first[i].port >= 1 && first[i].port <= RETAIL_PLAYERS && !((s_in >> (first[i].port - 1)) & 1) &&
                    Port_OnlineTilePort(&first[i]) == first[i].port) {
                    tile = &first[i];
                }
            }
            for (i = 0; i < world->numTileCars && tile == NULL; i++) {
                if (first[i].port > RETAIL_PLAYERS && Port_OnlineTilePort(&first[i]) == 0xFF) {
                    tile = &first[i];
                }
            }
            if (tile != NULL) {
                s_borrowed[s_numBorrowed].tile = tile;
                s_borrowed[s_numBorrowed].port = (u8)port;
                s_numBorrowed++;
                Port_Log("online: port %u starts at the world's spawn %u (port %u's)", port, (u32)(tile - first), tile->port);
                continue;
            }
            base = WorldSpawn(world, (port - 1) % RETAIL_PLAYERS + 1);
            e = &s_extra[s_numExtra];
            *e = *(base != NULL ? base : first);
            e->port = (u8)port;
            e->info.h = 0;
            e->car.h = 0;
            e->item.h = 0;
            e->item2.h = 0;
            e->itemID = e->item2ID = 0xFF;
            e->dynamic = 0;
            e->spawned = 0;
            s_extraBase[s_numExtra] = base != NULL ? base : first;
            s_numExtra++;
        }
        return;
    }
    if (first == s_spawns) {
        return;                 /* a restart, the world as it was */
    }
    s_scriptSpawn = first;
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        TileCar *t = &s_spawns[i];

        *t = *first;
        t->port = (u8)(i + 1);
        t->info.h = 0;
        t->car.h = 0;
        t->item.h = 0;
        t->item2.h = 0;
        t->itemID = t->item2ID = 0xFF;
    }
    world->numTileCars = ONLINE_PLAYERS;
    SETP(world->tileCars, s_spawns);
}

/* The ground below a point, if it's ground a car can stand on. */
static int SpawnGround(f32 x, f32 y, f32 z, f32 *ground) {
    Vector4 start __attribute__((aligned(16)));
    Vector4 down __attribute__((aligned(16)));
    CollisionInfo hit;
    f32 t = SPAWN_PROBE_DOWN;

    start.x = x;
    start.y = y;
    start.z = z;
    start.w = 1.0f;
    down.x = 0.0f;
    down.y = -1.0f;
    down.z = 0.0f;
    down.w = 0.0f;
    SDL_zero(hit);
    if (!RayHitsTerrain_Aux3(&start, &down, &t, &hit) || hit.normal.y < SPAWN_MIN_FLAT) {
        return 0;
    }
    *ground = y - t;
    return 1;
}

/* Is there a wall this far along a level direction? */
static int SpawnBlocked(f32 x, f32 y, f32 z, f32 dirX, f32 dirZ, f32 distance) {
    Vector4 start __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    CollisionInfo hit;
    f32 t = distance;

    start.x = x;
    start.y = y;
    start.z = z;
    start.w = 1.0f;
    dir.x = dirX;
    dir.y = 0.0f;
    dir.z = dirZ;
    dir.w = 0.0f;
    return RayHitsTerrain_Aux3(&start, &dir, &t, &hit);
}

/* A car's footprint: its collision box seen from above. */
typedef struct Footprint {
    int have;
    f32 cx, cz, ax, az, bx, bz, hx, hz, y;   /* middle, the two axes, half sizes */
} Footprint;

static void Project(const Footprint *f, f32 ux, f32 uz, f32 *lo, f32 *hi) {
    f32 c = f->cx * ux + f->cz * uz;
    f32 r = f->hx * fabsf(f->ax * ux + f->az * uz) + f->hz * fabsf(f->bx * ux + f->bz * uz);

    *lo = c - r;
    *hi = c + r;
}

static int Overlap(const Footprint *a, const Footprint *b) {
    const f32 axes[4][2] = {{a->ax, a->az}, {a->bx, a->bz}, {b->ax, b->az}, {b->bx, b->bz}};
    u32 k;

    for (k = 0; k < 4; k++) {
        f32 alo, ahi, blo, bhi;

        Project(a, axes[k][0], axes[k][1], &alo, &ahi);
        Project(b, axes[k][0], axes[k][1], &blo, &bhi);
        if (ahi < blo || bhi < alo) {
            return 0;
        }
    }
    return 1;
}

static Footprint SpotFootprint(f32 x, f32 z, const Matrix4 *m, f32 gap) {
    Footprint f;
    f32 len;

    SDL_zero(f);
    f.have = 1;
    f.cx = x;
    f.cz = z;
    len = sqrtf(m->v[0].x * m->v[0].x + m->v[0].z * m->v[0].z);
    f.ax = len > 0.01f ? m->v[0].x / len : 1.0f;
    f.az = len > 0.01f ? m->v[0].z / len : 0.0f;
    len = sqrtf(m->v[2].x * m->v[2].x + m->v[2].z * m->v[2].z);
    f.bx = len > 0.01f ? m->v[2].x / len : 0.0f;
    f.bz = len > 0.01f ? m->v[2].z / len : 1.0f;
    f.hx = (SPAWN_CAR_WIDTH + gap) * 0.5f;
    f.hz = (SPAWN_CAR_LENGTH + gap) * 0.5f;
    return f;
}

/* Ground under every corner of the largest car's box standing there, as high as the
 * spawn's: none of it hangs over a ledge. */
static int FootprintGround(f32 x, f32 probeY, f32 z, const Matrix4 *m, f32 baseGround) {
    u32 k;

    for (k = 0; k < 4; k++) {
        f32 sx = (k & 1) ? SPAWN_CAR_WIDTH * 0.5f : -SPAWN_CAR_WIDTH * 0.5f;
        f32 sz = (k & 2) ? SPAWN_CAR_LENGTH * 0.5f : -SPAWN_CAR_LENGTH * 0.5f;
        f32 ground;

        if (!SpawnGround(x + m->v[0].x * sx + m->v[2].x * sz, probeY, z + m->v[0].z * sx + m->v[2].z * sz, &ground) ||
            fabsf(ground - baseGround) > SPAWN_MAX_STEP) {
            return 0;
        }
    }
    return 1;
}

/* A taken spot: where a car starts, and which way it faces. */
typedef struct Spot {
    f32 x, z;
    Matrix4 mtx;
} Spot;

static void TakeSpot(Spot *spot, f32 x, f32 z, const TileCar *facing) {
    spot->x = x;
    spot->z = z;
    SetOrientation(&spot->mtx, facing->rotY, facing->rotX, facing->rotZ);
}

/* A spot near a spawn, for a car facing as it does: on ground like the spawn's, with `room`
 * free on every side, its footprint `gap` clear of those taken (the largest car's, both),
 * in plain reach of the spawn so none starts behind a wall. Bearings are eighths of a turn
 * from the spawn's matrix's first row (its side: a car's length is along the third), tried
 * in order at each radius in turn. From the world's own collision, so every machine finds
 * the same. */
static int FindSpot(const TileCar *base, const u8 *bearings, u32 numBearings, const f32 *radii, u32 numRadii, f32 room, f32 gap,
                    const Spot *taken, u32 numTaken, Vector4 *out, u32 *usedRadius, u32 *usedBearing) {
    Matrix4 mtx;
    f32 baseGround, probeY;
    u32 r, b, i;

    probeY = base->pos.y + SPAWN_PROBE_ABOVE;
    if (!SpawnGround(base->pos.x, probeY, base->pos.z, &baseGround)) {
        baseGround = base->pos.y;
    }
    SetOrientation(&mtx, base->rotY, base->rotX, base->rotZ);
    for (r = 0; r < numRadii; r++) {
        for (b = 0; b < numBearings; b++) {
            f32 angle = (f32)bearings[b] * (3.1415927f / 4.0f);
            f32 c = cosf(angle), sn = sinf(angle);
            f32 dirX = mtx.v[0].x * c + mtx.v[2].x * sn;
            f32 dirZ = mtx.v[0].z * c + mtx.v[2].z * sn;
            f32 len = sqrtf(dirX * dirX + dirZ * dirZ);
            f32 x, z, ground, y;
            int clear = 1;

            if (len < 0.01f) {
                continue;
            }
            dirX /= len;
            dirZ /= len;
            x = base->pos.x + dirX * radii[r];
            z = base->pos.z + dirZ * radii[r];
            for (i = 0; i < numTaken && clear; i++) {
                Footprint mine = SpotFootprint(x, z, &mtx, gap), theirs = SpotFootprint(taken[i].x, taken[i].z, &taken[i].mtx, gap);

                clear = !Overlap(&mine, &theirs);
            }
            if (!clear || !SpawnGround(x, probeY, z, &ground) || fabsf(ground - baseGround) > SPAWN_MAX_STEP ||
                !FootprintGround(x, probeY, z, &mtx, baseGround)) {
                continue;
            }
            y = ground + SPAWN_BODY_HEIGHT;
            if (SpawnBlocked(base->pos.x, baseGround + SPAWN_BODY_HEIGHT, base->pos.z, dirX, dirZ, radii[r]) ||
                SpawnBlocked(x, y, z, 1.0f, 0.0f, room) || SpawnBlocked(x, y, z, -1.0f, 0.0f, room) ||
                SpawnBlocked(x, y, z, 0.0f, 1.0f, room) || SpawnBlocked(x, y, z, 0.0f, -1.0f, room)) {
                continue;
            }
            *out = base->pos;
            out->x = x;
            out->y = base->pos.y + (ground - baseGround);
            out->z = z;
            *usedRadius = (u32)radii[r];
            *usedBearing = bearings[b];
            return 1;
        }
    }
    return 0;
}

/* Joyride: spots for the other cars around the world's one spawn. */
static void ChooseSpawns(void) {
    /* nearest and beside it first */
    static const u8 bearings[] = {0, 4, 7, 5, 6, 1, 3, 2};
    static const f32 radii[] = {30.0f, 40.0f, 55.0f, 70.0f, 90.0f, 120.0f, 160.0f};
    TileCar *base = s_scriptSpawn;
    Spot taken[ONLINE_PLAYERS];
    u32 numTaken = 1, port, radius, bearing;

    if (base == NULL) {
        return;
    }
    TakeSpot(&taken[0], base->pos.x, base->pos.z, base);
    s_spawns[0].pos = base->pos;
    for (port = 1; port < ONLINE_PLAYERS; port++) {
        s_spawns[port].pos = base->pos;
        /* if nowhere has the room, a tight world still gets a spot, nearer the others */
        if (FindSpot(base, bearings, SDL_arraysize(bearings), radii, SDL_arraysize(radii), SPAWN_CLEARANCE, SPAWN_GAP, taken,
                     numTaken, &s_spawns[port].pos, &radius, &bearing) ||
            FindSpot(base, bearings, SDL_arraysize(bearings), radii, SDL_arraysize(radii), SPAWN_TIGHT_CLEARANCE, SPAWN_TIGHT_GAP,
                     taken, numTaken, &s_spawns[port].pos, &radius, &bearing)) {
            TakeSpot(&taken[numTaken++], s_spawns[port].pos.x, s_spawns[port].pos.z, base);
            Port_Log("online: spawn for port %u: %u units away, bearing %u", port + 1, radius, bearing);
        } else {
            /* nowhere clear: above the spawn, the car falls and the collision sorts it out */
            s_spawns[port].pos.y += SPAWN_CAR_LENGTH * (f32)port;
            Port_Log("online: no clear spawn for port %u", port + 1);
        }
    }
}

/* Battle, Drag Race: spawns for players 5-8 the world has no room for, each near the
 * spawn of player (port - 1) % 4 + 1, facing as it does: beside it first, two of the
 * world's spacings out (a Drag Race grid gets more columns on its start line), then
 * diagonally, then ahead or behind. */
static void ChooseExtraSpawns(void) {
    static const u8 bearings[] = {4, 0, 3, 5, 1, 7, 2, 6};
    World *world = WORLD_OF_LEVEL();
    Spot taken[24];
    f32 radii[6], spacing = 1e9f;
    u32 numTaken = 0, i, j, k, radius, bearing;

    if (s_numExtra == 0) {
        return;
    }
    /* how close the world's spawns are: cars fit that far apart */
    for (i = 0; i < world->numTileCars && numTaken < SDL_arraysize(taken); i++) {
        const TileCar *a = &GETP(world->tileCars)[i];

        TakeSpot(&taken[numTaken++], a->pos.x, a->pos.z, a);
        for (j = 0; j < i; j++) {
            const TileCar *b = &GETP(world->tileCars)[j];
            f32 dx = a->pos.x - b->pos.x, dz = a->pos.z - b->pos.z, d = sqrtf(dx * dx + dz * dz);

            if (a->port >= 1 && a->port <= RETAIL_PLAYERS && b->port >= 1 && b->port <= RETAIL_PLAYERS && d > 1.0f && d < spacing) {
                spacing = d;
            }
        }
    }
    spacing = SDL_clamp(spacing, 18.0f, 40.0f);
    for (k = 0; k < SDL_arraysize(radii); k++) {
        radii[k] = spacing * (2.0f + (f32)k);
    }
    for (i = 0; i < s_numExtra; i++) {
        TileCar *e = &s_extra[i];
        const TileCar *base = s_extraBase[i];

        e->pos = base->pos;
        if (FindSpot(base, bearings, SDL_arraysize(bearings), radii, SDL_arraysize(radii), SPAWN_TIGHT_CLEARANCE, SPAWN_TIGHT_GAP, taken,
                     numTaken, &e->pos, &radius, &bearing)) {
            Port_Log("online: spawn for port %u: %u units from port %u's, bearing %u", e->port, radius, base->port, bearing);
        } else {
            e->pos.y += spacing * (f32)(i + 1);
            Port_Log("online: no clear spawn for port %u", e->port);
        }
        if (numTaken < SDL_arraysize(taken)) {
            TakeSpot(&taken[numTaken++], e->pos.x, e->pos.z, base);
        }
    }
}

void Port_OnlineLevelLoading(void) {
    /* a restart makes new players: the last ones say nothing about these */
    s_localPlayer = NULL;
    SDL_zeroa(s_players);
    s_numSharedTaken = 0;
    if (s_state != STATE_NONE) {
        ChooseSpawns();
        ChooseExtraSpawns();
    }
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
    } else if (player->device < 0 || player->device > 3) {
        /* players 5-8 got no pad (Input_GetPlayerDevice): one of the pads online leaves
         * idle, or the game would ask for theirs to be plugged in (code variable 0xD4) */
        player->device = 1 + (player->port - 1) % 3;
    }
}

/* What the world's scripts call the player's car is this machine's, also after a respawn. */
static void KeepScriptSpawn(void) {
    if (s_scriptSpawn != NULL && s_port >= 1) {
        s_scriptSpawn->car.h = s_spawns[s_port - 1].car.h;
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
    s_numLabels = 0;            /* the level was made anew: its lists are as the pack has them */
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

/* Testing, HWVX_SCRIPTDUMP=1: what a level gives online play to work with, in the
 * log as it starts (any level, online or not): its spawns and its scripts. */
static void DumpLevel(void) {
    World *world = WORLD_OF_LEVEL();
    u32 i;

    for (i = 0; i < world->numTileCars; i++) {
        const TileCar *t = &GETP(world->tileCars)[i];

        Port_Log("script: tile car T%u port %u dynamic %u spawned %u vehicle %d car %s pos %.0f %.0f %.0f rotY %.0f", i, t->port,
                 t->dynamic, t->spawned, (s32)t->vehicle, GETP(t->car) != NULL ? "yes" : "no", t->pos.x, t->pos.y, t->pos.z,
                 t->rotY);
    }
    Port_TriggerDump();
}

void Port_OnlineLevelStarted(void) {
    extern s32 g_GameProgressionID;
    const char *dump = SDL_getenv("HWVX_SCRIPTDUMP");

    if (dump != NULL && *dump != 0) {
        DumpLevel();
    }
    if (s_state == STATE_NONE) {
        return;
    }
    Port_Log("online: level ready: progression %d, port %d, %d human cars", g_GameProgressionID, s_port,
             PlayerManager_PlayerHumanCount());
    Port_Log("online: rules %u mode %u time %u kills %u points %u rounds %u wins %u", GameSettings_GetCodeVariable(0x12C, 0),
             GameSettings_GetCodeVariable(0x12D, 0), GameSettings_GetCodeVariable(0x12E, 0), GameSettings_GetCodeVariable(0x12F, 0),
             GameSettings_GetCodeVariable(0x130, 0), GameSettings_GetCodeVariable(0x131, 0), GameSettings_GetCodeVariable(0x132, 0));
    KeepScriptSpawn();
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

/* ---- the round summary: the host moves everyone on --------------------------- */

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

/* The host leaving it takes everyone along; it never moves on by itself, so the
 * scores stay up until the host continues. By pressing A on it, as a player would:
 * its first row is what goes on, Continue between rounds and Restart at the end. A
 * player who continues first waits in the next round, which holds until the host
 * says go. A restart or the host's Main Menu takes the level and this summary with
 * it: BeginStage and MSG_RETURN tell the others about those. */
static void SummaryHeartbeat(u64 now) {
    if (s_summary == NULL) {
        return;
    }
    if (Port_InterfaceIsUp(s_summary)) {
        if (s_summarySince == 0) {
            s_summarySince = now;
            s_summaryPress = 0;
        }
        PatchSummaryLabels();
        if (s_continue && now >= s_summaryPress) {
            s_summaryPress = now + SUMMARY_PRESS_MS;
            s_pressA = 1;
        }
    } else if (s_summarySince != 0) {
        s_summarySince = 0;
        s_continue = 0;
        UnpatchSummaryLabels();
        if (s_isHost) {
            u8 msg = MSG_CONTINUE;

            Net_SendReliable(NET_PEER_ALL, &msg, 1);
        }
    }
}

/* ---- the round summary of more than 4 --------------------------------------------
 * Its rows are the retail four, P1-P4, each reading its port's code variables. With
 * players 5-8 in the game it shows them in turns: P1-P4, then P5-P8 in the same rows
 * (Port_OnlineInterfaceArg), the row labels' lists given P5-P8 (PatchSummaryLabels). */
#define SUMMARY_PAGE_MS 3000

static IfaceStringList s_labelLists[8];
static u32 s_labelTexts[8][10];   /* the list's 6, then P5-P8 */
static u32 s_labelPage;
static u32 s_labelBlank;        /* frames the labels stay blank */
static PTR32(char) s_labelTables[4][1];
static char s_labelNames[4][4] = {"P|5", "P|6", "P|7", "P|8"};

static int SummaryPaging(const void *header) {
    return s_state != STATE_NONE && header != NULL && header == s_summary && (s_in & ~0xFu) != 0;
}

static u32 SummaryPage(void) {
    return s_summarySince != 0 ? (u32)((SDL_GetTicks() - s_summarySince) / SUMMARY_PAGE_MS) & 1 : 0;
}

int Port_OnlineInterfacePaging(const void *header) {
    return SummaryPaging(header);
}

/* A row's code variable, read for the player shown in it: 0 to read it as it is. */
int Port_OnlineInterfaceRead(const void *header, u32 id, u32 arg, s32 *out) {
    u32 page;

    if (!SummaryPaging(header) || arg < 1 || arg > RETAIL_PLAYERS) {
        return 0;
    }
    page = SummaryPage();
    if (id == 0x196) {
        /* the label: the list's P1-P4 are P5-P8 on the second page (PatchSummaryLabels);
         * blank a moment as the pages turn, so the text is looked up again */
        *out = s_labelBlank == 0 && GameSettings_GetCodeVariable(id, arg + RETAIL_PLAYERS * page) != 0 ? (s32)arg : 0;
        return 1;
    }
    if (page == 0) {
        return 0;
    }
    switch (id) {
    case 0xCB:
    case 0x191:
    case 0x192:
    case 0x193:
    case 0x194:
        *out = (s32)GameSettings_GetCodeVariable(id, arg + RETAIL_PLAYERS);
        return 1;
    }
    return 0;
}

static void UnpatchSummaryLabels(void) {
    u32 i;

    for (i = 0; i < s_numLabels; i++) {
        s_labels[i].text->list.h = s_labels[i].original;
    }
    s_numLabels = 0;
}

/* The label lists ("     ", P1-P4, AI by code variable 0x196): ours, whose P1-P4 are
 * P5-P8 on the second page. */
static void PatchSummaryLabels(void) {
    const InterfaceHeader *h = s_summary;
    u32 i, k;

    if (s_numLabels != 0) {
        u32 page = SummaryPage();

        if (s_labelBlank != 0) {
            s_labelBlank--;
        }
        if (page != s_labelPage) {
            s_labelPage = page;
            s_labelBlank = 2;
            for (i = 0; i < s_numLabels; i++) {
                for (k = 1; k <= 4; k++) {
                    /* P1-P4 and P5-P8 trade places */
                    u32 t = s_labelTexts[i][k];

                    s_labelTexts[i][k] = s_labelTexts[i][5 + k];
                    s_labelTexts[i][5 + k] = t;
                }
            }
        }
        return;
    }
    if (!SummaryPaging(h)) {
        return;
    }
    s_labelPage = 0;
    for (k = 0; k < 4; k++) {
        SETP(s_labelTables[k][0], s_labelNames[k]);
    }
    for (i = 0; i < h->numObjects && s_numLabels < SDL_arraysize(s_labels); i++) {
        const IfaceObjectDef *d = &GETP(h->objects)[i];
        IfaceText *t;
        IfaceStringList *list;
        PTR32(char) *first;

        if (d->type != IFACE_DEF_TEXT || (t = GETP(d->sub)) == NULL || !t->listMode || (list = GETP(t->list)) == NULL ||
            list->count != 6 || (first = (PTR32(char) *)PTR(GETP(list->texts)[1])) == NULL || GETP(first[0]) == NULL ||
            SDL_strcmp(GETP(first[0]), "P|1") != 0) {
            continue;
        }
        s_labelLists[s_numLabels] = *list;
        for (k = 0; k < 6; k++) {
            s_labelTexts[s_numLabels][k] = GETP(list->texts)[k];
        }
        for (k = 0; k < 4; k++) {
            s_labelTexts[s_numLabels][6 + k] = (u32)HND(s_labelTables[k]);
        }
        SETP(s_labelLists[s_numLabels].texts, s_labelTexts[s_numLabels]);
        s_labels[s_numLabels].text = t;
        s_labels[s_numLabels].original = t->list.h;
        SETP(t->list, &s_labelLists[s_numLabels]);
        s_numLabels++;
    }
}

/* ---- the pause menu: the race goes on behind it ------------------------------ */

int Port_OnlineMenuOpen(void) {
    return s_state != STATE_NONE && s_menuOpen;
}

void Port_OnlineSetMenuOpen(int open) {
    s_menuOpen = open;
}

/* ---- scores -------------------------------------------------------------------- */

/* The car was destroyed by this player. The game has just counted it here, if the
 * car is this machine's. 1: a copy's death, which its owner's machine counts instead. */
int Port_OnlineCarKilled(const Player *victim, const Player *killer) {
    u8 buf[8];
    NetWriter w;
    u32 port;

    if (s_state == STATE_NONE) {
        return 0;
    }
    if (Port_OnlineIsRemote(victim)) {
        return 1;
    }
    for (port = 1; port <= ONLINE_PLAYERS; port++) {
        if (s_players[port - 1] == killer) {
            NetWriter_Init(&w, buf, sizeof(buf));
            NetWriter_U8(&w, MSG_KILL);
            NetWriter_U32(&w, s_stage);
            NetWriter_U8(&w, (u8)port);
            Net_SendReliable(s_isHost ? NET_PEER_ALL : NET_PEER_HOST, buf, w.size);
            break;
        }
    }
    return 0;
}

static void ReceiveKill(u32 stage, u32 killer) {
    if (stage == s_stage && killer >= 1 && killer <= ONLINE_PLAYERS && s_players[killer - 1] != NULL) {
        s_players[killer - 1]->kills++;
        s_players[killer - 1]->scoreDirty = 1;
    }
}

/* GameRules found the round over; forced: a script or the host said so. 1: not for this
 * machine to decide: only the host checks targets and the timer. */
int Port_OnlineRoundOver(int forced, u32 winner) {
    u8 buf[8 + ONLINE_PLAYERS * 8];
    NetWriter w;
    u32 i;

    if (s_state == STATE_NONE || forced) {
        return 0;
    }
    if (!s_isHost) {
        return 1;
    }
    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_ROUND_END);
    NetWriter_U32(&w, s_stage);
    NetWriter_U8(&w, winner >= 1 && winner <= ONLINE_PLAYERS ? (u8)winner : 0xFF);
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        NetWriter_U32(&w, s_players[i] != NULL ? (u32)s_players[i]->kills : 0);
        NetWriter_U32(&w, s_players[i] != NULL ? s_players[i]->stuntPoints : 0);
    }
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    return 0;
}

static void ReceiveRoundEnd(NetReader *r) {
    u32 stage = NetReader_U32(r);
    u32 winner = NetReader_U8(r);
    s32 kills[ONLINE_PLAYERS];
    u32 points[ONLINE_PLAYERS];
    u32 i;

    for (i = 0; i < ONLINE_PLAYERS; i++) {
        kills[i] = (s32)NetReader_U32(r);
        points[i] = NetReader_U32(r);
    }
    if (!r->ok || stage != s_stage || s_hold) {
        return;
    }
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        if (s_players[i] != NULL) {
            s_players[i]->kills = kills[i];
            s_players[i]->stuntPoints = points[i];
            s_players[i]->scoreDirty = 1;
        }
    }
    Port_Log("online: the host ended the round, winner %u", winner);
    GameRules_ForceRoundEnd(winner == 0xFF ? 0xFFFFFFFFu : winner);
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

/* Joyride's key and gears: taken once, and for everyone. 0 if this one already was: two
 * players reaching it together have their one, not two. */
static int SharedItemFirstTaken(const QuadItem *spawner) {
    u32 i;

    for (i = 0; i < s_numSharedTaken; i++) {
        if (s_sharedTaken[i] == spawner) {
            return 0;
        }
    }
    if (s_numSharedTaken < MAX_SHARED_ITEMS) {
        s_sharedTaken[s_numSharedTaken++] = spawner;
    }
    return 1;
}

static int IsSharedItem(const QuadItem *spawner) {
    return s_scriptSpawn != NULL && Port_TriggerItemWatched(spawner, 0);
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
                if (IsSharedItem(spawner)) {
                    SharedItemFirstTaken(spawner);
                }
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
    if (contents == NULL || i >= contents->numItems) {
        return;
    }
    spawner = &GETP(contents->items)[i];
    if (IsSharedItem(spawner)) {
        /* this machine's player gets it too, as if they had driven over it */
        ItemThing *thing = GETP(spawner->thing);
        CarA *own = s_localPlayer != NULL ? GETP(s_localPlayer->vehicle) : NULL;

        if (!SharedItemFirstTaken(spawner)) {
            return;
        }
        spawner->spawned = 1;   /* its area may not be loaded here: it stays away after */
        spawner->timer = 0;
        if (thing != NULL && own != NULL) {
            s_applyingPickup = 1;
            CarA_ProcessItemPickup(own, thing);
            s_applyingPickup = 0;
        } else {
            Port_TriggerItemWatched(spawner, 1);
            if (own != NULL && GETP(spawner->record) != NULL) {
                own->pickupId = GETP(spawner->record)->names.h;
            }
            if (thing != NULL) {
                Item_Release(thing);
            }
        }
        return;
    }
    if (player == NULL || (car = GETP(player->vehicle)) == NULL) {
        return;
    }
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
    NetWriter_U32(&w, s_localPlayer->stuntPoints);
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
    in.points = NetReader_U32(&rd);
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
            break;              /* port: the peer id below (port index) */
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
    if (player->stuntPoints != r->points) {
        player->stuntPoints = r->points;
        player->scoreDirty = 1;
    }
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
        if (kind == MSG_HELLO) {
            ReceiveHello(port, &r);
            ShowLobby(NULL);
        } else if (kind == MSG_CARS) {
            s32 vehicle = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
            s32 item = Port_ContentReadChoice(&r, PORT_CONTENT_ITEM);

            if (r.ok && (s_state == STATE_CHOOSING || s_state == STATE_WAITING)) {
                s_vehicle[port - 1] = vehicle;
                s_item[port - 1] = item;
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
        } else if (kind == MSG_KILL && s_state == STATE_PLAYING) {
            u32 stage = NetReader_U32(&r);
            u32 killer = NetReader_U8(&r);

            if (r.ok) {
                u32 peer;

                ReceiveKill(stage, killer);
                for (peer = 1; peer < ONLINE_PLAYERS; peer++) {
                    if (peer != e->peer) {
                        Net_SendReliable((u8)peer, e->data, e->size);
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
            if (s_state == STATE_PLAYING) {
                /* it went back to the lobby: its car goes too, for everyone */
                u8 buf[8];
                NetWriter w;
                u32 peer;

                ReceiveCarState(port, s_stage, 0);
                NetWriter_Init(&w, buf, sizeof(buf));
                NetWriter_U8(&w, MSG_CAR_STATE);
                NetWriter_U8(&w, (u8)port);
                NetWriter_U32(&w, s_stage);
                NetWriter_U8(&w, 0);
                for (peer = 1; peer < ONLINE_PLAYERS; peer++) {
                    if (peer != e->peer) {
                        Net_SendReliable((u8)peer, buf, w.size);
                    }
                }
            }
        }
    } else if (e->peer == NET_PEER_HOST) {
        if (kind == MSG_HELLO) {
            ReceiveHello(1, &r);
        } else if (kind == MSG_SETUP && s_state == STATE_LOBBY) {
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
        } else if (kind == MSG_KILL && s_state == STATE_PLAYING) {
            u32 stage = NetReader_U32(&r);
            u32 killer = NetReader_U8(&r);

            if (r.ok) {
                ReceiveKill(stage, killer);
            }
        } else if (kind == MSG_ROUND_END && s_state == STATE_PLAYING) {
            ReceiveRoundEnd(&r);
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
                vehicle[i] = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
                item[i] = Port_ContentReadChoice(&r, PORT_CONTENT_ITEM);
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

/* Drag Race, players 5-8: the script has finishes for the world's four spawns only, so
 * this machine's car claims its own at theirs (Port_TriggerAtFinish), and the host's
 * decision runs a stand-in for its RoundComplete (Trigger.c). */
static void FinishHeartbeat(void) {
    extern s32 g_GameProgressionID;
    CarA *car;

    if (g_GameProgressionID != PROGRESSION_MP_DRAG || s_hold || s_claimed || s_decided || s_localPlayer == NULL ||
        s_localPlayer->port <= RETAIL_PLAYERS || (car = GETP(s_localPlayer->vehicle)) == NULL) {
        return;
    }
    if (Port_TriggerAtFinish(car)) {
        Port_Log("online: port %d at the finish", s_port);
        Port_OnlineHoldRoundComplete(s_localPlayer->port);
    }
}

/* ---- HWVX_ONLINE_TEST: a session nobody plays, for tools/online8_test.sh ----------
 *   HWVX_ONLINE_TEST=host,players=8,mode=battle|drag|joyride,world=N,car=N,rounds=N,
 *     time=SECONDS (a Battle round),summary=SECONDS (then the host goes on),
 *     lobby=SECONDS (the least the lobby stays open, default 2)
 * The host opens its lobby from the main menu, waits for the players, sets the game up
 * and picks its car without the menus. It logs how far each other car is from where its
 * owner says (online: sync), and tells the others where every car is (MSG_TEST_CARS) so
 * tools/netbot.c can drive its own. Test only: in play, the summary waits for the host. */
static void TestParse(void) {
    const char *e = SDL_getenv("HWVX_ONLINE_TEST");
    char buf[256], *tok, *save = NULL;

    SDL_zero(s_test);
    if (e == NULL || *e == 0) {
        return;
    }
    s_test.players = 2;
    s_test.progression = PROGRESSION_MP_BATTLE;
    s_test.rounds = 2;
    s_test.roundMs = 30000;
    s_test.summaryMs = 6000;
    s_test.lobbyMs = 2000;
    SDL_strlcpy(buf, e, sizeof(buf));
    for (tok = SDL_strtok_r(buf, ",:", &save); tok != NULL; tok = SDL_strtok_r(NULL, ",:", &save)) {
        char *eq = SDL_strchr(tok, '=');
        const char *v = eq != NULL ? eq + 1 : "";

        if (eq != NULL) {
            *eq = 0;
        }
        if (SDL_strcmp(tok, "host") == 0) {
            s_test.host = 1;
        } else if (SDL_strcmp(tok, "players") == 0) {
            s_test.players = (u32)SDL_clamp(SDL_atoi(v), 1, ONLINE_PLAYERS);
        } else if (SDL_strcmp(tok, "mode") == 0) {
            s_test.progression = SDL_strcmp(v, "drag") == 0 ? PROGRESSION_MP_DRAG :
                                 SDL_strcmp(v, "joyride") == 0 ? PROGRESSION_JOYRIDE : PROGRESSION_MP_BATTLE;
        } else if (SDL_strcmp(tok, "world") == 0) {
            s_test.level = (u32)SDL_atoi(v);
        } else if (SDL_strcmp(tok, "car") == 0) {
            s_test.car = (u32)SDL_atoi(v);
        } else if (SDL_strcmp(tok, "rounds") == 0) {
            s_test.rounds = (u32)SDL_atoi(v);
        } else if (SDL_strcmp(tok, "time") == 0) {
            s_test.roundMs = (u32)SDL_atoi(v) * 1000u;
        } else if (SDL_strcmp(tok, "summary") == 0) {
            s_test.summaryMs = (u32)SDL_atoi(v) * 1000u;
        } else if (SDL_strcmp(tok, "lobby") == 0) {
            s_test.lobbyMs = (u32)SDL_atoi(v) * 1000u;
        }
    }
    Port_Log("online test: %s, %u players, progression %u level %u", s_test.host ? "host" : "?", s_test.players, s_test.progression,
             s_test.level);
}

static void TestSetUp(void) {
    /* what the mode's options screen would leave */
    GameSettings_SetCodeVariable(0x12C, 0, 1);
    GameSettings_SetCodeVariable(0x12D, 0, s_test.progression == PROGRESSION_MP_BATTLE ? 2 : 3);
    g_codeVariables.versusRoundTime = (s32)s_test.roundMs;   /* ms; the menu sets 5-minute steps */
    GameSettings_SetCodeVariable(0x12F, 0, 99);
    GameSettings_SetCodeVariable(0x130, 0, 0);
    GameSettings_SetCodeVariable(0x131, 0, s_test.rounds);
    GameSettings_SetCodeVariable(0x132, 0, 0);
    GameSettings_SetCodeVariable(0x134, 0, 2);
    GameSettings_SetCodeVariable(0x135, 0, 0);
    GameSettings_SetCodeVariable(0x136, 0, 1);
    StateProgressionManager_SetProgressionID((s32)s_test.progression, (s32)s_test.level);
    if (!Port_OnlineHostSetup()) {
        Port_Log("online test: refused to start");
        return;
    }
    /* the host's own choice, as the car select would make it */
    s_vehicle[0] = (s32)s_test.car;
    s_item[0] = NO_ITEM;
    s_chosen |= 1;
    s_state = STATE_WAITING;
    Port_InterfaceGoto(Port_MenuOnlineScreen(ONLINE_SCREEN_WAITING));
    HostCheckChosen();
}

/* Where every car is, once they have settled after go (test only). */
static void TestSendCars(void) {
    u8 buf[8 + ONLINE_PLAYERS * 32];
    NetWriter w;
    u32 i, n = 0;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_TEST_CARS);
    NetWriter_U32(&w, s_stage);
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        n += s_players[i] != NULL && GETP(s_players[i]->vehicle) != NULL;
    }
    NetWriter_U8(&w, (u8)n);
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        Thing *t;

        if (s_players[i] == NULL || (t = (Thing *)GETP(s_players[i]->vehicle)) == NULL) {
            continue;
        }
        NetWriter_U8(&w, (u8)(i + 1));
        NetWriter_F32(&w, t->pos.x);
        NetWriter_F32(&w, t->pos.y);
        NetWriter_F32(&w, t->pos.z);
        NetWriter_F32(&w, t->quat.w);
        NetWriter_F32(&w, t->quat.x);
        NetWriter_F32(&w, t->quat.y);
        NetWriter_F32(&w, t->quat.z);
    }
    Net_SendReliable(NET_PEER_ALL, buf, w.size);
    Port_Log("online test: told everyone where the %u cars are, stage %u", n, s_stage);
}

/* Where the cars settled after go (test only): each car's footprint (its collision box),
 * how far it moved from its spawn (pushed out of a wall or another car, or fell), a wall
 * between its middle and a corner of its footprint, and footprints overlapping. Lines with
 * MOVED, FELL, WALL or OVERLAP fail tools/online8_test.sh. */
static void TestSpawnCheck(void) {
    Footprint f[ONLINE_PLAYERS];
    u32 i, j;

    SDL_zeroa(f);
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        CarA *car;
        const TileCar *tile;
        const CarACollider *coll;
        const Matrix4 *m;
        f32 dx, dz, lx, ly, lz, len;
        int k, wall = 0;

        if (s_players[i] == NULL || (car = GETP(s_players[i]->vehicle)) == NULL || (tile = GETP(s_players[i]->tileCar)) == NULL) {
            continue;
        }
        coll = GETP(GETP(car->vehicleIface)->collider);
        m = THING_MTX(&car->base);
        if (coll->type != 0 || GETP(coll->box) == NULL) {
            Port_Log("online test: car %u: collider type %u, not a box", i + 1, coll->type);
            continue;
        }
        {
            const Vector4 *box = GETP(coll->box);

            f[i].hx = (box[1].x - box[0].x) * 0.5f;
            f[i].hz = (box[1].z - box[0].z) * 0.5f;
            lx = (box[1].x + box[0].x) * 0.5f;
            ly = (box[1].y + box[0].y) * 0.5f;
            lz = (box[1].z + box[0].z) * 0.5f;
        }
        f[i].have = 1;
        len = sqrtf(m->v[0].x * m->v[0].x + m->v[0].z * m->v[0].z);
        f[i].ax = len > 0.01f ? m->v[0].x / len : 1.0f;
        f[i].az = len > 0.01f ? m->v[0].z / len : 0.0f;
        len = sqrtf(m->v[2].x * m->v[2].x + m->v[2].z * m->v[2].z);
        f[i].bx = len > 0.01f ? m->v[2].x / len : 0.0f;
        f[i].bz = len > 0.01f ? m->v[2].z / len : 1.0f;
        f[i].cx = car->base.pos.x + m->v[0].x * lx + m->v[1].x * ly + m->v[2].x * lz;
        f[i].cz = car->base.pos.z + m->v[0].z * lx + m->v[1].z * ly + m->v[2].z * lz;
        f[i].y = car->base.pos.y + m->v[0].y * lx + m->v[1].y * ly + m->v[2].y * lz;
        dx = car->base.pos.x - tile->pos.x;
        dz = car->base.pos.z - tile->pos.z;
        for (k = 0; k < 4; k++) {
            f32 sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
            f32 ex = sx * f[i].hx * f[i].ax + sz * f[i].hz * f[i].bx, ez = sx * f[i].hx * f[i].az + sz * f[i].hz * f[i].bz;
            f32 d = sqrtf(ex * ex + ez * ez);

            if (d > 0.01f && SpawnBlocked(f[i].cx, f[i].y, f[i].cz, ex / d, ez / d, d)) {
                wall = 1;
            }
        }
        Port_Log("online test: car %u, box %.1f x %.1f: moved %.1f from its spawn, %.1f up%s%s%s", i + 1, 2.0f * f[i].hx, 2.0f * f[i].hz,
                 sqrtf(dx * dx + dz * dz), car->base.pos.y - tile->pos.y, sqrtf(dx * dx + dz * dz) > 12.0f ? " MOVED" : "",
                 car->base.pos.y - tile->pos.y < -15.0f ? " FELL" : "", wall ? " WALL" : "");
    }
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        for (j = i + 1; j < ONLINE_PLAYERS; j++) {
            if (f[i].have && f[j].have && Overlap(&f[i], &f[j])) {
                Port_Log("online test: cars %u and %u OVERLAP", i + 1, j + 1);
            }
        }
    }
}

/* How far the other cars here are from where their owners last said. */
static void TestSync(void) {
    char line[256];
    u32 i, cars = 0;

    line[0] = 0;
    for (i = 0; i < ONLINE_PLAYERS; i++) {
        RemoteCar *r = &s_remote[i];
        Thing *t;
        char one[24];

        if (s_players[i] == NULL || (t = (Thing *)GETP(s_players[i]->vehicle)) == NULL) {
            continue;
        }
        cars++;
        if ((s32)i + 1 == s_port || !r->have) {
            continue;
        }
        {
            f32 age = (f32)(SDL_GetTicks() - r->receivedAt) / 1000.0f;
            f32 dx = r->pos.x + r->vel.x * age - t->pos.x, dy = r->pos.y + r->vel.y * age - t->pos.y;
            f32 dz = r->pos.z + r->vel.z * age - t->pos.z;

            SDL_snprintf(one, sizeof(one), " %u:%.1f", i + 1, sqrtf(dx * dx + dy * dy + dz * dz));
            SDL_strlcat(line, one, sizeof(line));
        }
    }
    Port_Log("online: sync stage %u, %u cars, %d human players, off by:%s", s_stage, cars, PlayerManager_PlayerHumanCount(), line);
}

static void TestHeartbeat(u64 now) {
    if (!s_test.host) {
        return;
    }
    if (!s_test.started) {
        /* from the main menu, once it has been up a moment (HWVX_AUTOINPUT gets there) */
        if (StateManager_CurrState() != 2 || Port_MenuOnlineScreen(ONLINE_SCREEN_MAIN) == NULL ||
            !Port_InterfaceIsUp(Port_MenuOnlineScreen(ONLINE_SCREEN_MAIN))) {
            s_test.menuSince = 0;
            return;
        }
        if (s_test.menuSince == 0) {
            s_test.menuSince = now;
        }
        if (now - s_test.menuSince > 3000 && Port_OnlineStartHost()) {
            s_test.started = 1;
            s_test.lobbySince = now;
            Port_InterfaceGoto(Port_MenuOnlineScreen(ONLINE_SCREEN_LOBBY_HOST));
        }
        return;
    }
    if (s_state == STATE_LOBBY && !s_test.setUp && (u32)Bits(s_present & s_verified) >= s_test.players && now - s_test.lobbySince > s_test.lobbyMs) {
        s_test.setUp = 1;
        Port_Log("online test: %d players in, setting up", Bits(s_present & s_verified));
        TestSetUp();
    }
    if (s_state != STATE_PLAYING || StateManager_CurrState() != 5) {
        return;
    }
    if (now >= s_test.nextSync) {
        s_test.nextSync = now + 2000;
        TestSync();
    }
    if (!s_hold && s_goTick != 0 && now - s_goTick > 1500 && s_test.carsSentStage != s_stage) {
        s_test.carsSentStage = s_stage;
        TestSendCars();
        TestSpawnCheck();
    }
    /* the host moves on from the summary on its own, after a while */
    if (s_summary != NULL && Port_InterfaceIsUp(s_summary) && s_summarySince != 0 && now - s_summarySince > s_test.summaryMs &&
        now >= s_summaryPress) {
        s_summaryPress = now + SUMMARY_PRESS_MS;
        s_pressA = 1;
    }
}

void Port_OnlineHeartbeat(void) {
    NetEvent e;
    static int s_testParsed;

    if (!s_testParsed) {
        s_testParsed = 1;
        TestParse();
    }
    TestHeartbeat(SDL_GetTicks());

    Port_MenuOnlineUpdate();
    if (s_state == STATE_NONE) {
        return;
    }
    if (s_tunnel) {
        char name[128];
        int status = Port_TunnelPoll(name, sizeof(name));

        if (status < 0) {
            SessionLost("Tunnel failed");
            return;
        }
        if (status > 0 && !s_tunnelReady) {
            s_tunnelReady = 1;
            SDL_snprintf(s_invite, sizeof(s_invite), "%s/%s", name, s_roomCode);
            Port_Log("online: tunnel ready, invite %s", s_invite);
            ShowLobby(NULL);
        }
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
            SendHello(NET_PEER_HOST);
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
                if (s_isHost) {
                    SendHello(e.peer);
                }
                ShowLobby(s_isHost ? "Ready" : NULL);
            }
            break;
        case NET_EV_PEER_LEFT:
            if (e.peer < ONLINE_PLAYERS) {
                s_present &= ~(1u << e.peer);
                s_in &= ~(1u << e.peer);
                s_verified &= ~(1u << e.peer);
                s_refused &= ~(1u << e.peer);
                s_remote[e.peer].have = 0;
                if (s_state == STATE_PLAYING) {
                    ReceiveCarState(e.peer + 1u, s_stage, 0);   /* gone: its car too */
                }
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
            KeepScriptSpawn();
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
            FinishHeartbeat();
        }
    } else if (s_state == STATE_PLAYING && s_inLevel) {
        u8 msg = s_isHost ? MSG_RETURN : MSG_LEFT_LEVEL;

        Net_SendReliable(s_isHost ? NET_PEER_ALL : NET_PEER_HOST, &msg, 1);
        s_lobby = (1u << ONLINE_PLAYERS) - 1;
        s_localPlayer = NULL;
        s_scriptSpawn = NULL;
        s_numLabels = 0;        /* gone with the level */
        s_hold = 0;
        s_menuOpen = 0;
        s_inLevel = 0;
        s_state = STATE_LOBBY;
        ShowLobby(s_isHost ? "Ready" : "Waiting...");
        Port_InterfaceGoto(Port_MenuOnlineScreen(s_tunnel ? ONLINE_SCREEN_LOBBY_TUNNEL : s_isHost ? ONLINE_SCREEN_LOBBY_HOST : ONLINE_SCREEN_LOBBY_CLIENT));
    }
}
