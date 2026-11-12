/* Online play transport: one host and its clients, each connected to the host
 * only (a Cloudflare tunnel only takes connections to the
 * host). Port-only; the game talks to the rest of the session through this.
 *
 * Messages travel over WebSocket, which is what a tunnel carries, in two kinds:
 * - reliable: arrive whole, once, in order. For events (lobby, weapon fired,
 *   round over). A peer too slow to take them is dropped, not waited for.
 * - latest: one slot per key per peer. A newer message replaces one not yet
 *   sent, so a stalled connection catches up instead of falling behind. For
 *   state that the next message makes obsolete (car snapshots).
 *
 * A socket thread does all the waiting, outside the giant lock, so connections
 * stay alive while the game loads; the calls here only touch queues. Call them
 * all from one thread.
 *
 * Payloads are the caller's bytes: build them with NetWriter and read them
 * with NetReader, field by field, never as raw structs (hosts differ in padding
 * and in the size of long) and never with handles or pointers in them.
 *
 * HWVX_NET_SIM adds lag and loss for testing, see net_sim.c. */
#ifndef NET_H
#define NET_H

#include <string.h>

#include "port.h"

#define NET_DEFAULT_PORT 47815
#define NET_MAX_PLAYERS 8        /* what the transport and its messages can carry */
#define NET_DEFAULT_PLAYERS 8    /* what the game hosts: 8 players online (Port_Online.c) */
#define NET_PEER_HOST 0         /* the host's peer id; clients are 1 and up */
#define NET_PEER_ALL 0xFF       /* Net_Send*: every peer */
#define NET_MAX_MESSAGE 60000   /* bytes in one message */
#define NET_LATEST_KEYS 8
#define NET_MAX_ROOM_CODE 32

typedef enum NetEventType {
    NET_EV_NONE,
    NET_EV_CONNECTED,           /* client: in the session; peer is our own id */
    NET_EV_PEER_JOINED,
    NET_EV_PEER_LEFT,
    NET_EV_RELIABLE,            /* data, size from peer */
    NET_EV_LATEST,              /* data, size, key from peer */
    NET_EV_DISCONNECTED,        /* client: the session is over, see error */
} NetEventType;

typedef enum NetError {
    NET_OK,
    NET_ERR_ADDRESS,            /* could not be resolved or parsed */
    NET_ERR_UNREACHABLE,        /* no connection before the connect deadline */
    NET_ERR_NOT_HOST,           /* something answered, but not a game host */
    NET_ERR_REJECT_VERSION,     /* the host runs another protocol version */
    NET_ERR_REJECT_ROOM_CODE,
    NET_ERR_REJECT_FULL,
    NET_ERR_HOST_CLOSED,
    NET_ERR_PEER_CLOSED,        /* NET_EV_PEER_LEFT: it left on purpose */
    NET_ERR_LOST,               /* the connection failed */
    NET_ERR_TIMEOUT,            /* the connection went silent */
    NET_ERR_PROTOCOL,           /* the other side sent something invalid */
    NET_ERR_OVERFLOW,           /* a peer could not keep up */
    NET_ERR_BIND,               /* Net_Host: the port could not be opened */
    NET_ERR_TLS,                /* wss://: no secure connection, or not to whom it should be */
} NetError;

typedef struct NetEvent {
    NetEventType type;
    u8 peer;
    u8 key;                     /* NET_EV_LATEST */
    NetError error;             /* NET_EV_DISCONNECTED, NET_EV_PEER_LEFT */
    const u8 *data;             /* valid until the next Net_Poll or Net_Close */
    u32 size;
} NetEvent;

typedef struct NetStats {
    u32 rttMs;                  /* last ping round trip; 0: none yet */
    u32 staleDropped;           /* latest messages replaced before being sent */
    u32 queuedBytes;            /* waiting to be sent */
    u64 bytesIn, bytesOut;
} NetStats;

/* Starts a session; both return a NetError. roomCode may be NULL (no code).
 * loopbackOnly: listen on 127.0.0.1 only, for hosting behind cloudflared.
 * maxPlayers: counting the host, 2 .. NET_MAX_PLAYERS; a client past it is refused. */
int Net_Host(u16 port, const char *roomCode, int loopbackOnly, u32 maxPlayers);
/* address: see Ws_ParseAddress: an address and port, a URL, or a quick tunnel's name,
 * with /CODE after it for roomCode if that's NULL. Connects in the background, retrying
 * until the deadline: expect NET_EV_CONNECTED or NET_EV_DISCONNECTED. */
int Net_Connect(const char *address, const char *roomCode);
/* Ends the session; sends what is queued, for at most a moment. */
void Net_Close(void);

int Net_IsActive(void);
/* A room code nobody could guess: `length` characters of capitals and digits, and a NUL. */
int Net_RandomCode(char *out, u32 length);
/* This machine's address on its network, for other players to type; 0 if unknown.
 * Sends nothing. Behind a router it is the inside address: port forwarding has
 * an outside one this can't see. */
int Net_LocalAddress(char *out, u32 size);
const char *Net_ErrorName(NetError error);

/* 1 if queued. Clients can send only to the host (NET_PEER_HOST or
 * NET_PEER_ALL); relaying to other clients is the host's decision to make. */
int Net_SendReliable(u8 peer, const void *data, u32 size);
int Net_SendLatest(u8 peer, u8 key, const void *data, u32 size);

/* The next event, or 0 if there is none. */
int Net_Poll(NetEvent *event);
/* 0 if there is no such peer. Clients ask about NET_PEER_HOST. */
int Net_GetStats(u8 peer, NetStats *stats);

/* ---- message building: little-endian, bounds checked ---------------------
 * Running past the end sets ok to 0 and leaves the buffer alone; reads then
 * return 0. Check ok once at the end instead of after every field. */

typedef struct NetWriter {
    u8 *data;
    u32 size, cap;
    int ok;
} NetWriter;

typedef struct NetReader {
    const u8 *data;
    u32 size, pos;
    int ok;
} NetReader;

static inline void NetWriter_Init(NetWriter *w, void *data, u32 cap) {
    w->data = data;
    w->size = 0;
    w->cap = cap;
    w->ok = 1;
}

static inline u8 *NetWriter_Reserve(NetWriter *w, u32 n) {
    if (!w->ok || n > w->cap - w->size) {
        w->ok = 0;
        return NULL;
    }
    w->size += n;
    return w->data + w->size - n;
}

static inline void NetWriter_U8(NetWriter *w, u8 v) {
    u8 *p = NetWriter_Reserve(w, 1);
    if (p != NULL) {
        p[0] = v;
    }
}

static inline void NetWriter_U16(NetWriter *w, u16 v) {
    u8 *p = NetWriter_Reserve(w, 2);
    if (p != NULL) {
        p[0] = (u8)v;
        p[1] = (u8)(v >> 8);
    }
}

static inline void NetWriter_U32(NetWriter *w, u32 v) {
    u8 *p = NetWriter_Reserve(w, 4);
    if (p != NULL) {
        p[0] = (u8)v;
        p[1] = (u8)(v >> 8);
        p[2] = (u8)(v >> 16);
        p[3] = (u8)(v >> 24);
    }
}

static inline void NetWriter_F32(NetWriter *w, f32 v) {
    u32 bits;
    memcpy(&bits, &v, 4);
    NetWriter_U32(w, bits);
}

static inline void NetWriter_Bytes(NetWriter *w, const void *src, u32 n) {
    u8 *p = NetWriter_Reserve(w, n);
    if (p != NULL && n != 0) {
        memcpy(p, src, n);
    }
}

static inline void NetReader_Init(NetReader *r, const void *data, u32 size) {
    r->data = data;
    r->size = size;
    r->pos = 0;
    r->ok = 1;
}

static inline const u8 *NetReader_Take(NetReader *r, u32 n) {
    if (!r->ok || n > r->size - r->pos) {
        r->ok = 0;
        return NULL;
    }
    r->pos += n;
    return r->data + r->pos - n;
}

static inline u8 NetReader_U8(NetReader *r) {
    const u8 *p = NetReader_Take(r, 1);
    return p != NULL ? p[0] : 0;
}

static inline u16 NetReader_U16(NetReader *r) {
    const u8 *p = NetReader_Take(r, 2);
    return p != NULL ? (u16)(p[0] | p[1] << 8) : 0;
}

static inline u32 NetReader_U32(NetReader *r) {
    const u8 *p = NetReader_Take(r, 4);
    return p != NULL ? (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24 : 0;
}

/* Bits, not a checked number: NaN and infinity come through. A message from
 * the network is untrusted, so check the value before the game uses it. */
static inline f32 NetReader_F32(NetReader *r) {
    u32 bits = NetReader_U32(r);
    f32 v;
    memcpy(&v, &bits, 4);
    return v;
}

#endif /* NET_H */
