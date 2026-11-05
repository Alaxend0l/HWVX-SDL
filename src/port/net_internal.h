/* Private parts of the online transport: the WebSocket protocol (net_ws.c) and
 * the lag simulator (net_sim.c). Neither touches a socket, so tools/nettest.c
 * tests them directly. */
#ifndef NET_INTERNAL_H
#define NET_INTERNAL_H

#include "net.h"

/* ---- net_ws.c ------------------------------------------------------------ */

enum {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xA,
};

#define WS_MAX_HEADER 14
#define WS_HTTP_MAX 8192        /* longest handshake head accepted */
#define WS_KEY_MAX 64
#define WS_ACCEPT_LEN 28        /* base64 of a SHA-1, without the NUL */
#define WS_PATH "/hwvx"

typedef struct WsFrame {
    u8 opcode;
    u8 fin;
    u8 *payload;                /* in the parsed buffer, already unmasked */
    u32 size;
} WsFrame;

void Ws_Sha1(const void *data, u32 size, u8 out[20]);
/* out: size * 4 / 3 + 5 bytes at least. */
void Ws_Base64(const void *data, u32 size, char *out);
/* The Sec-WebSocket-Accept value of a Sec-WebSocket-Key. */
void Ws_AcceptKey(const char *key, char out[WS_ACCEPT_LEN + 1]);

/* Parses the first frame in buf, unmasking it in place. Returns the bytes it
 * took, 0 if the frame is not all there yet, or -1 if it is invalid: bad bits,
 * longer than maxPayload, or masked when it should not be (or the reverse: a
 * client must mask and a server must not, RFC 6455 5.1). */
s32 Ws_ParseFrame(u8 *buf, u32 len, int expectMasked, u32 maxPayload, WsFrame *frame);
/* Writes a whole frame to out (size + WS_MAX_HEADER bytes at least); returns
 * its length. mask: 4 bytes to mask with, or NULL. */
u32 Ws_BuildFrame(u8 *out, u8 opcode, const void *data, u32 size, const u8 *mask);

/* The length of the HTTP head in buf, ending with its blank line; 0 if not all
 * there yet. */
u32 Ws_HeadLength(const u8 *buf, u32 len);
/* A NUL-terminated head: is it an upgrade request for WS_PATH? If so, key gets
 * its Sec-WebSocket-Key. */
int Ws_ParseRequest(const char *head, char key[WS_KEY_MAX]);
/* A NUL-terminated head: is it the 101 answer to key? */
int Ws_CheckResponse(const char *head, const char *key);

/* ---- net_sim.c ----------------------------------------------------------- */

typedef struct NetSim {
    int enabled;
    u32 latencyMs;              /* lat */
    u32 jitterMs;               /* jitter */
    u32 lossPer10k;             /* loss, as a percentage in HWVX_NET_SIM */
    u32 stallMs;                /* stall */
    u32 bandwidth;              /* bw, in KB/s there, bytes/s here; 0: no limit */
    u32 rng;
} NetSim;

/* Reads a HWVX_NET_SIM value; NULL or empty leaves it disabled. 0 if the text
 * is invalid, which leaves it disabled too. */
int NetSim_Parse(NetSim *sim, const char *text);
/* When a frame arriving at now should count as received. prevDue is the frame
 * before it's: order holds, as it does on TCP, so one stall holds up what
 * follows. */
u64 NetSim_Due(NetSim *sim, u64 now, u64 prevDue);

#endif /* NET_INTERNAL_H */
