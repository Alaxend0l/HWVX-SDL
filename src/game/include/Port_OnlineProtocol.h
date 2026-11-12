/* The game's messages over the online transport (src/port/net.h): what
 * Port_Online.c sends and reads, and tools/netbot.c, a headless player for
 * testing, speaks too. Payloads are built with NetWriter, field by field. */
#ifndef PORT_ONLINE_PROTOCOL_H
#define PORT_ONLINE_PROTOCOL_H

/* Players in a session, the host counted: the game's 8-car budget, which
 * online play fills with people (there are no AI cars online). A port is
 * 1 .. ONLINE_MAX_PLAYERS: the peer id + 1. Bit port - 1 in the u8 masks.
 * The retail game has 4 player ports; what online play does about 5 to 8 is
 * at the top of Port_Online.c. */
#define ONLINE_MAX_PLAYERS 8
#define RETAIL_PLAYERS 4

enum {
    MSG_SETUP = 1,              /* host: u8 progression, u16 level, u8 ports in, the settings */
    MSG_ABORT,                  /* host: left the car select */
    MSG_CARS,                   /* client: the car, then the item (Port_ContentWriteChoice) */
    MSG_START,                  /* host: u8 ports in, u32 seed, then the car and the item per port */
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
    MSG_HELLO,                  /* both, on joining: the enabled mods (ModsFingerprint below) */
    MSG_TEST_CARS,              /* host, HWVX_ONLINE_TEST only: u32 stage, u8 ports; per port in it
                                 * u8 port, f32 pos xyz, quat wxyz: where every car is, for
                                 * tools/netbot.c to report its own from */
};

/* MSG_HELLO: u8 how many mods, u32 low and high words of a 64-bit FNV-1a hash
 * of "id@version\n" per enabled mod in load order; u8 length and the text of a
 * summary to show (names and versions). No mods: 0, 0, 0 and "no mods". Players
 * whose hellos differ don't play together: the client leaves, saying why, and
 * the host leaves it out until it does. */
#define MODS_SUMMARY_MAX 120

static inline unsigned long long Online_Fnv1a(unsigned long long h, const char *s) {
    for (; s != 0 && *s != 0; s++) {
        h ^= (unsigned char)*s;
        h *= 0x100000001B3ull;
    }
    return h;
}
#define ONLINE_FNV_BASIS 0xCBF29CE484222325ull

/* A car's snapshot, a "latest" message keyed by its port - 1: u32 seq; f32 pos xyz,
 * vel xyz, quat wxyz, angular momentum xyz; s32 health; u32 buttons; s8 stick x, y;
 * u8 throttle, brake, fire (bit 1: infinite ammo cheat), weapon slot; u16 its ammo;
 * u32 stunt points. */
#define SNAPSHOT_SIZE 76

#endif
