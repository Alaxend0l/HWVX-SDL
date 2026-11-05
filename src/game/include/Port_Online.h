/* Online play, on the game's side: the session (Port_Online.c) and its menus
 * (Port_Menu.c). Everything here does nothing unless a session is running, so
 * offline play takes the original paths. The transport is src/port/net.h. */
#ifndef PORT_ONLINE_H
#define PORT_ONLINE_H

#include "common.h"
#include "Interface.h"

/* ---- Port_Online.c ------------------------------------------------------- */

int Port_OnlineActive(void);
/* A car driven from another machine: no input, no camera, no view of its own. */
int Port_OnlineIsRemote(const Player *player);
/* Once a frame, from Main_Loop. */
void Port_OnlineHeartbeat(void);
/* Menu actions: 0 if the session could not start. */
int Port_OnlineStartHost(void);
int Port_OnlineStartClient(const char *address);
void Port_OnlineEnd(void);
int Port_OnlinePlayerCount(void);
/* The host reached the car select: everyone goes there. */
void Port_OnlineHostSetup(void);
/* The car select is done on this machine: 1 if a session took it over and the
 * waiting screen is showing instead of the level starting. */
int Port_OnlineCarsChosen(Interface *iface);
/* The back button on a screen: 1 refuses it. */
int Port_OnlineBack(const InterfaceHeader *header);
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
/* Whose controller settings a player uses. */
u32 Port_OnlineControllerPort(const Player *player);
/* The control reader's work for a car driven on another machine. */
void Port_OnlineRemoteControl(Player *player);
/* Trigger.c: runs the script's RoundComplete(number) trigger. */
void Port_TriggerFireRoundComplete(u32 number);
/* Trigger.c: the interface whose end the script answers with a round reset. */
void *Port_TriggerRoundSummary(void);
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
    ONLINE_SCREEN_CAR_SELECT,   /* the game's one-player car select */
    ONLINE_SCREEN_WAITING,
} PortOnlineScreen;

InterfaceHeader *Port_MenuOnlineScreen(PortOnlineScreen screen);
/* Shows text on a line of a port screen's text, live if the screen is up. */
void Port_MenuOnlineSetLine(PortOnlineScreen screen, u32 line, const char *text);
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
