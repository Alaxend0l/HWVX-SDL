/* The game's messages over the online transport (src/port/net.h): what
 * Port_Online.c sends and reads, and tools/netbot.c, a headless player for
 * testing, speaks too. Payloads are built with NetWriter, field by field. */
#ifndef PORT_ONLINE_PROTOCOL_H
#define PORT_ONLINE_PROTOCOL_H

/* Players in a session, the host counted: the game's 8-car budget, which
 * online play fills with people (there are no AI cars online). A port is
 * 1 .. ONLINE_MAX_PLAYERS: the peer id + 1. Bit port - 1 in the u8 masks. */
#define ONLINE_MAX_PLAYERS 4

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
    MSG_KILL,                   /* u32 stage, u8 the killer's port: this car's death was its doing */
    MSG_ROUND_END,              /* host: u32 stage, u8 winner, 0xFF: none; s32 kills, u32 points per port */
};

/* A car's snapshot, a "latest" message keyed by its port - 1: u32 seq; f32 pos xyz,
 * vel xyz, quat wxyz, angular momentum xyz; s32 health; u32 buttons; s8 stick x, y;
 * u8 throttle, brake, fire (bit 1: infinite ammo cheat), weapon slot; u16 its ammo;
 * u32 stunt points. */
#define SNAPSHOT_SIZE 76

#endif
