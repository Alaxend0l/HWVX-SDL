/* Online play, on the game's side: the session (Port_Online.c) and its menus
 * (Port_Menu.c). Everything here does nothing unless a session is running, so
 * offline play takes the original paths. The transport is src/port/net.h. */
#ifndef PORT_ONLINE_H
#define PORT_ONLINE_H

#include "common.h"
#include "Interface.h"

struct TileCar;
struct World;

/* ---- Port_Online.c ------------------------------------------------------- */

int Port_OnlineActive(void);
/* A car driven from another machine: no input, no camera, no view of its own. */
int Port_OnlineIsRemote(const Player *player);
/* Once a frame, from Main_Loop. */
void Port_OnlineHeartbeat(void);
/* Menu actions: 0 if the session could not start. */
int Port_OnlineStartHost(void);
int Port_OnlineStartTunnelHost(void);
/* What to send to others to join; 0 while there's nothing yet. */
int Port_OnlineInvite(char *out, size_t size);
int Port_OnlineStartClient(const char *address);
void Port_OnlineEnd(void);
int Port_OnlinePlayerCount(void);
/* The host reached the car select: everyone goes there. 0: refused (Drag Race with
 * more than 4 players), the host stays where it is. */
int Port_OnlineHostSetup(void);
/* May the host start this progression with the players in? 0 says why on the mode screen. */
int Port_OnlineModeAllowed(s32 progression);
/* The car select is done on this machine: 1 if a session took it over and the
 * waiting screen is showing instead of the level starting. */
int Port_OnlineCarsChosen(Interface *iface);
/* The back button on a screen: 1 refuses it. */
int Port_OnlineBack(const InterfaceHeader *header);
/* LoadWorldProcess: a world with a spawn for one gets more; one with 4 player spawns
 * gets them for players 5 to 8. */
void Port_OnlineExpandSpawns(struct World *world);
/* Player ports a world's spawns are for: the retail game's 4, online up to 8. */
u32 Port_OnlineHumanPorts(void);
/* A spawn's player port, as the loading and the level's setup read it: offline its
 * own; online also 5-8 for the world's AI spawns that players use, and above
 * Port_OnlineHumanPorts for the other AI spawns. */
u32 Port_OnlineTilePort(const struct TileCar *tile);
/* Spawns of our own, after the world's list (World_GetTileCar): none offline. */
u32 Port_OnlineExtraTileCount(void);
struct TileCar *Port_OnlineExtraTile(u32 index);
/* A script's list of `n` spawns (TileCar handles): if it names every player spawn
 * of the world, the spawns of ours go with it: how many, put in out (8 room). */
u32 Port_OnlineListExtras(const u32 *subjects, u32 n, struct TileCar **out);
/* StateGamePlay_Init: before it makes any players. */
void Port_OnlineLevelLoading(void);
/* StateGamePlay_Init: a human player was made. */
void Port_OnlineSetupPlayer(Player *player);
/* StateGamePlay_Init: the level is ready. */
void Port_OnlineLevelStarted(void);
/* The script reset the round. */
void Port_OnlineRoundReset(void);
/* The world waits for the other machines. */
int Port_OnlineHolding(void);
/* A script is about to run RoundComplete(number): 1 if that's not for it to decide. */
int Port_OnlineHoldRoundComplete(u32 number);
/* The pause menu is up: the race goes on, the local car without input. */
int Port_OnlineMenuOpen(void);
void Port_OnlineSetMenuOpen(int open);
/* LoadWorldProcess: a level's random pickup, the same on every machine. */
s32 Port_OnlineItemRandom(s32 min, s32 max, u32 quadrant, u32 index);
/* The car is about to take the item. 1: not here. */
int Port_OnlinePickup(struct CarA *car, struct ItemThing *item);
/* The car was destroyed by this player. 1: a copy's: don't count it. */
int Port_OnlineCarKilled(const Player *victim, const Player *killer);
/* GameRules found the round over; forced: a script or the host said so. 1: not for this
 * machine to decide. */
int Port_OnlineRoundOver(int forced, u32 winner);
/* InterfaceManager: the round summary shows players 5-8 in its four rows in turns: the
 * port a row's code variable reads, and whether its variables are read each frame. */
int Port_OnlineInterfaceRead(const void *header, u32 id, u32 arg, s32 *out);
int Port_OnlineInterfacePaging(const void *header);
/* Whose controller settings a player uses. */
u32 Port_OnlineControllerPort(const Player *player);
/* The control reader's work for a car driven on another machine. */
void Port_OnlineRemoteControl(Player *player);
/* Trigger.c: runs the script's RoundComplete(number) trigger. */
void Port_TriggerFireRoundComplete(u32 number);
/* Trigger.c: does a script watch for this item being picked up? fire: and run those. */
int Port_TriggerItemWatched(const void *spawner, int fire);
/* Trigger.c: online, Drag Race players 5-8: is the car at the finish of the world's 4? */
int Port_TriggerAtFinish(const void *car);
/* The spawn of a player 1-8 in this level (online), or NULL. */
void *Port_OnlineSpawnOf(u32 port);
/* Trigger.c: the interface whose end the script answers with a round reset. */
void *Port_TriggerRoundSummary(void);
/* Testing: the active triggers and their actions in the log. */
void Port_TriggerDump(void);
/* The keyboard's turn in Input_Heartbeat: 1 to press A, as a player would. */
int Port_OnlinePressA(void);
/* This machine's pad does nothing for now: the host is choosing for everyone. */
int Port_OnlineMenuBlocked(void);

/* ---- Port_Menu.c ---------------------------------------------------------- */

typedef enum PortOnlineScreen {
    ONLINE_SCREEN_MAIN,         /* the game's main menu */
    ONLINE_SCREEN_ONLINE,       /* Host with Tunnel / Host via LAN / Connect */
    ONLINE_SCREEN_LOBBY_HOST,
    ONLINE_SCREEN_LOBBY_CLIENT,
    ONLINE_SCREEN_LOBBY_TUNNEL,
    ONLINE_SCREEN_CAR_SELECT,   /* the game's one-player car select: races, battles */
    ONLINE_SCREEN_CAR_SELECT_JOYRIDE,   /* Joyride's, going on to its item select */
    ONLINE_SCREEN_ITEM_SELECT_JOYRIDE,
    ONLINE_SCREEN_WAITING,
    ONLINE_SCREEN_MODE,         /* Drag Race / Battle / Joyride */
} PortOnlineScreen;

InterfaceHeader *Port_MenuOnlineScreen(PortOnlineScreen screen);
/* Shows text on a line of a port screen's text, live if the screen is up. */
void Port_MenuOnlineSetLine(PortOnlineScreen screen, u32 line, const char *text);
/* The same across `count` lines, for what's longer than a line of the panel. */
void Port_MenuOnlineSetLines(PortOnlineScreen screen, u32 first, u32 count, const char *text);
/* The address on the Connect screen. */
void Port_MenuOnlineAddress(char *out, size_t size);
/* Once a frame: typing shows on the Connect screen. */
void Port_MenuOnlineUpdate(void);

/* ---- InterfaceManager.c --------------------------------------------------- */

/* Leaves whatever menu is up for this one, as if a menu event had chosen it. */
void Port_InterfaceGoto(InterfaceHeader *header);
s32 Port_InterfaceIsUp(const void *header);
/* Back to a screen in the menu's history; 0 if it isn't there. */
s32 Port_InterfaceGoBack(InterfaceHeader *header);

#endif
