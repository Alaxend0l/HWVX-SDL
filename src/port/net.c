/* The online transport's socket thread and queues; the API is described in
 * net.h. Inside each WebSocket binary frame, the first byte says what it is:
 *
 *   HELLO     client, first:  u32 magic, u16 protocol, u8 length, room code
 *   WELCOME   host's answer:  u8 your id, u8 peer count, that many peer ids
 *   REJECT    host's answer:  u8 NetError
 *   JOINED    host:           u8 peer id
 *   LEFT      host:           u8 peer id, u8 NetError
 *   RELIABLE  either way:     the message
 *   LATEST    either way:     u8 key, the message
 *
 * Connections and their sockets belong to the socket thread. s_lock guards what
 * the API also reaches: the queues, a connection's state, stats and transmit
 * position, the events. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NetSocket;
#define NET_NO_SOCKET INVALID_SOCKET
#define NetCloseSocket closesocket
#define NetLastError() WSAGetLastError()
#define NET_WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
#define NET_IN_PROGRESS(e) ((e) == WSAEWOULDBLOCK)
#define NET_INTERRUPTED(e) ((e) == WSAEINTR)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetSocket;
#define NET_NO_SOCKET (-1)
#define NetCloseSocket close
#define NetLastError() errno
#define NET_WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define NET_IN_PROGRESS(e) ((e) == EINPROGRESS)
#define NET_INTERRUPTED(e) ((e) == EINTR)
#endif

#include <SDL3/SDL.h>

#include "net_internal.h"

#define NET_MAGIC 0x58565748u   /* "HWVX" */
#define NET_PROTOCOL 1

enum {
    KIND_HELLO = 1,
    KIND_WELCOME,
    KIND_REJECT,
    KIND_JOINED,
    KIND_LEFT,
    KIND_RELIABLE,
    KIND_LATEST,
};

#define MAX_PAYLOAD (NET_MAX_MESSAGE + 2)  /* the kind and a latest's key */
#define RX_CAP (MAX_PAYLOAD + WS_MAX_HEADER + WS_HTTP_MAX)
#define TX_CAP (MAX_PAYLOAD + WS_MAX_HEADER)
/* Joined peers, and as many again still shaking hands: one stranger's idle
 * connections cannot lock the others out for longer than HANDSHAKE_MS. */
#define MAX_CONNS ((NET_MAX_PLAYERS - 1) * 2)
#define MAX_QUEUED (4u << 20)   /* to one peer; more than that and it is dropped */
#define MAX_EVENT_BYTES (16u << 20)
#define HANDSHAKE_MS 5000
/* Cloudflare closes a connection silent for about 125 s (measured); pings at
 * this rate also give the round trip time. */
#define PING_MS 1000
#define SILENT_MS 15000
#define CONNECT_DEADLINE_MS 30000
#define CONNECT_ATTEMPT_MS 5000
#define RETRY_FIRST_MS 250
#define RETRY_MAX_MS 4000
#define CLOSE_DRAIN_MS 300
/* What the kernel may hold unsent: more and latest messages would go stale in
 * its buffer, past where they could be replaced. */
#define SOCKET_SEND_BUFFER (32 * 1024)
#define SOCKET_NOTSENT_LOWAT (16 * 1024)

/* A queued frame's opcode for bytes sent as they are: the HTTP handshake. */
#define OP_RAW 0xFF

typedef struct QueueNode {
    struct QueueNode *next;
    u8 opcode;
    u32 size;
    u8 data[];
} QueueNode;

typedef struct Queue {
    QueueNode *head, *tail;
    u32 bytes;
} Queue;

typedef struct DelayedFrame {
    struct DelayedFrame *next;
    u64 due;
    u8 opcode;
    u32 size;
    u8 data[];
} DelayedFrame;

typedef enum ConnState {
    CONN_FREE,
    CONN_CONNECTING,        /* client: the TCP connect is under way */
    CONN_HTTP,              /* waiting for the HTTP head: request or response */
    CONN_AWAIT_HELLO,       /* host */
    CONN_AWAIT_WELCOME,     /* client */
    CONN_JOINED,
    CONN_CLOSING,           /* host: sending a rejection, then gone */
} ConnState;

typedef struct Latest {
    u8 *data;
    u32 size, cap;
    int pending;
} Latest;

typedef struct Conn {
    ConnState state;
    u8 peer;
    NetSocket sock;
    u64 deadline;           /* of the handshake or the rejection */
    u64 lastRecv, lastPing;
    u8 *rx;
    u32 rxLen;
    u8 *msg;                /* a fragmented message so far */
    u32 msgLen;
    u8 msgOpen;
    u8 *tx;
    u32 txLen, txOff;
    Queue control, reliable;
    Latest latest[NET_LATEST_KEYS];
    u8 preferLatest;        /* neither kind starves the other */
    u8 nextLatest;
    NetError dropReason;    /* socket thread only: set while handling a frame */
    u8 overflowed;          /* s_lock: more was queued than the peer can take */
    NetStats stats;
    DelayedFrame *delayHead, *delayTail;
    u64 lastDue;
    double bwTokens;
    u64 bwTime;
} Conn;

typedef struct EventNode {
    struct EventNode *next;
    NetEvent event;
    u8 data[];
} EventNode;

typedef struct ResolveJob {
    SDL_AtomicInt refs;
    SDL_AtomicInt done;
    char host[256];
    char port[8];
    struct addrinfo *result;
} ResolveJob;

typedef enum ClientPhase {
    CLIENT_NONE,
    CLIENT_RESOLVING,
    CLIENT_ATTEMPTING,      /* the connection in s_conns[0] is in hand */
    CLIENT_WAITING,         /* until retryAt */
    CLIENT_DONE,            /* joined, or over */
} ClientPhase;

static SDL_Mutex *s_lock;
static SDL_Thread *s_thread;
static SDL_AtomicInt s_stop;
static int s_active, s_isHost;
static Conn s_conns[MAX_CONNS];
static EventNode *s_eventHead, *s_eventTail;
static u32 s_eventBytes;
static EventNode *s_polled;
static NetSocket s_listen = NET_NO_SOCKET, s_wakeRead = NET_NO_SOCKET, s_wakeWrite = NET_NO_SOCKET;
static char s_roomCode[NET_MAX_ROOM_CODE + 1];
static NetSim s_sim;
static u32 s_rng;

static struct {
    ClientPhase phase;
    char host[256];
    u16 port;
    char wsKey[WS_KEY_MAX];
    ResolveJob *job;
    struct addrinfo *addrs, *addr;
    u64 deadline, retryAt, attemptEnd;
    u32 retryMs;
    NetError lastError;
} s_client;

const char *Net_ErrorName(NetError error) {
    switch (error) {
    case NET_OK: return "ok";
    case NET_ERR_ADDRESS: return "address not found";
    case NET_ERR_UNREACHABLE: return "could not connect";
    case NET_ERR_NOT_HOST: return "not a game host";
    case NET_ERR_REJECT_VERSION: return "different game version";
    case NET_ERR_REJECT_ROOM_CODE: return "wrong room code";
    case NET_ERR_REJECT_FULL: return "game is full";
    case NET_ERR_HOST_CLOSED: return "host closed the game";
    case NET_ERR_PEER_CLOSED: return "left the game";
    case NET_ERR_LOST: return "connection lost";
    case NET_ERR_TIMEOUT: return "connection timed out";
    case NET_ERR_PROTOCOL: return "invalid data received";
    case NET_ERR_OVERFLOW: return "connection too slow";
    case NET_ERR_BIND: return "could not open the port";
    }
    return "unknown error";
}

static u32 NextRandom(void) {
    u32 x = s_rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

/* ---- sockets ------------------------------------------------------------- */

static int NetStartup(void) {
#ifdef _WIN32
    static int started;
    WSADATA data;

    if (!started) {
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return 0;
        }
        started = 1;
    }
#endif
    return 1;
}

static int SetNonBlocking(NetSocket s) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags != -1 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void SetOption(NetSocket s, int level, int name, int value) {
    setsockopt(s, level, name, (const char *)&value, sizeof(value));
}

static void TuneSocket(NetSocket s) {
    SetOption(s, IPPROTO_TCP, TCP_NODELAY, 1);
    SetOption(s, SOL_SOCKET, SO_SNDBUF, SOCKET_SEND_BUFFER);
#ifdef TCP_NOTSENT_LOWAT
    SetOption(s, IPPROTO_TCP, TCP_NOTSENT_LOWAT, SOCKET_NOTSENT_LOWAT);
#endif
#ifdef SO_NOSIGPIPE
    SetOption(s, SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
}

/* A closed peer must give an error here, not end the game with SIGPIPE. */
static s32 NetSend(NetSocket s, const void *data, u32 size) {
#ifdef MSG_NOSIGNAL
    return (s32)send(s, (const char *)data, (int)size, MSG_NOSIGNAL);
#else
    return (s32)send(s, (const char *)data, (int)size, 0);   /* SO_NOSIGPIPE, or Windows */
#endif
}

static int FitsSelect(NetSocket s) {
#ifdef _WIN32
    (void)s;
    return 1;                   /* Windows's fd_set is a list, not a bit field */
#else
    return s >= 0 && s < FD_SETSIZE;
#endif
}

/* A datagram to itself wakes the thread's select when the API queues something.
 * Without it (it never should fail) the thread still polls, just less promptly. */
static void OpenWakeSocket(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    NetSocket s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s == NET_NO_SOCKET) {
        return;
    }
    SDL_zero(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        getsockname(s, (struct sockaddr *)&addr, &len) != 0 ||
        connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        !SetNonBlocking(s) || !FitsSelect(s)) {
        NetCloseSocket(s);
        return;
    }
    s_wakeRead = s;
    s_wakeWrite = s;
}

static void Wake(void) {
    if (s_wakeWrite != NET_NO_SOCKET) {
        send(s_wakeWrite, "w", 1, 0);
    }
}

static void DrainWake(void) {
    char buf[64];

    while (recv(s_wakeRead, buf, sizeof(buf), 0) > 0) {
    }
}

/* ---- events and queues: with s_lock held --------------------------------- */

static void PushEvent(NetEventType type, u8 peer, u8 key, NetError error, const void *data, u32 size) {
    EventNode *node = SDL_malloc(sizeof(EventNode) + size);

    if (node == NULL) {
        return;
    }
    node->next = NULL;
    node->event.type = type;
    node->event.peer = peer;
    node->event.key = key;
    node->event.error = error;
    node->event.data = node->data;
    node->event.size = size;
    if (size != 0) {
        SDL_memcpy(node->data, data, size);
    }
    if (s_eventTail != NULL) {
        s_eventTail->next = node;
    } else {
        s_eventHead = node;
    }
    s_eventTail = node;
    s_eventBytes += size;
}

static int QueuePush(Queue *q, u8 opcode, const void *a, u32 aSize, const void *b, u32 bSize) {
    QueueNode *node = SDL_malloc(sizeof(QueueNode) + aSize + bSize);

    if (node == NULL) {
        return 0;
    }
    node->next = NULL;
    node->opcode = opcode;
    node->size = aSize + bSize;
    if (aSize != 0) {
        SDL_memcpy(node->data, a, aSize);
    }
    if (bSize != 0) {
        SDL_memcpy(node->data + aSize, b, bSize);
    }
    if (q->tail != NULL) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->bytes += node->size;
    return 1;
}

static QueueNode *QueuePop(Queue *q) {
    QueueNode *node = q->head;

    if (node != NULL) {
        q->head = node->next;
        if (q->head == NULL) {
            q->tail = NULL;
        }
        q->bytes -= node->size;
    }
    return node;
}

static void QueueClear(Queue *q) {
    QueueNode *node;

    while ((node = QueuePop(q)) != NULL) {
        SDL_free(node);
    }
}

static int ConnHasOutput(const Conn *c) {
    u32 i;

    if (c->txOff < c->txLen || c->control.head != NULL || c->reliable.head != NULL) {
        return 1;
    }
    for (i = 0; i < NET_LATEST_KEYS; i++) {
        if (c->latest[i].pending) {
            return 1;
        }
    }
    return 0;
}

/* ---- connections ---------------------------------------------------------
 * Called on the socket thread, s_lock not held, unless they say otherwise. */

static Conn *ConnOpen(NetSocket sock, ConnState state, u64 now) {
    u32 i;

    for (i = 0; i < MAX_CONNS; i++) {
        Conn *c = &s_conns[i];

        if (c->state == CONN_FREE) {
            u8 *rx = SDL_malloc(RX_CAP);
            u8 *tx = SDL_malloc(TX_CAP);
            u8 *msg = SDL_malloc(MAX_PAYLOAD);

            if (rx == NULL || tx == NULL || msg == NULL) {
                SDL_free(rx);
                SDL_free(tx);
                SDL_free(msg);
                return NULL;
            }
            SDL_LockMutex(s_lock);
            SDL_zerop(c);
            c->sock = sock;
            c->rx = rx;
            c->tx = tx;
            c->msg = msg;
            c->state = state;
            c->deadline = now + HANDSHAKE_MS;
            c->lastRecv = now;
            c->lastPing = now;
            c->bwTime = now;
            SDL_UnlockMutex(s_lock);
            return c;
        }
    }
    return NULL;
}

/* Closes the socket and frees everything; announces nothing. */
static void ConnFree(Conn *c) {
    u32 i;
    DelayedFrame *d = c->delayHead;

    if (c->state == CONN_FREE) {
        return;
    }
    if (c->sock != NET_NO_SOCKET) {
        NetCloseSocket(c->sock);
    }
    while (d != NULL) {
        DelayedFrame *next = d->next;

        SDL_free(d);
        d = next;
    }
    SDL_LockMutex(s_lock);
    QueueClear(&c->control);
    QueueClear(&c->reliable);
    for (i = 0; i < NET_LATEST_KEYS; i++) {
        SDL_free(c->latest[i].data);
    }
    SDL_free(c->rx);
    SDL_free(c->tx);
    SDL_free(c->msg);
    SDL_zerop(c);
    c->sock = NET_NO_SOCKET;
    SDL_UnlockMutex(s_lock);
}

static void SendRaw(Conn *c, const void *data, u32 size) {
    SDL_LockMutex(s_lock);
    QueuePush(&c->control, OP_RAW, data, size, NULL, 0);
    SDL_UnlockMutex(s_lock);
}

/* Ahead of the messages: the handshake and keepalives. */
static void SendFrame(Conn *c, u8 opcode, const void *data, u32 size) {
    SDL_LockMutex(s_lock);
    /* pongs answer what the peer sends: one that doesn't read can't grow this */
    if (c->control.bytes + size > MAX_QUEUED) {
        c->overflowed = 1;
    } else {
        QueuePush(&c->control, opcode, data, size, NULL, 0);
    }
    SDL_UnlockMutex(s_lock);
}

/* In line with the messages, so a notice about a peer can't pass what was sent
 * before it. */
static void SendNotice(Conn *c, const u8 *msg, u32 size) {
    SDL_LockMutex(s_lock);
    if (c->reliable.bytes + size > MAX_QUEUED) {
        c->overflowed = 1;
    } else {
        QueuePush(&c->reliable, WS_OP_BINARY, msg, size, NULL, 0);
    }
    SDL_UnlockMutex(s_lock);
}

static void BroadcastLeft(u8 peer, NetError reason) {
    u8 msg[3];
    u32 i;

    msg[0] = KIND_LEFT;
    msg[1] = peer;
    msg[2] = (u8)reason;
    for (i = 0; i < MAX_CONNS; i++) {
        if (s_conns[i].state == CONN_JOINED) {
            SendNotice(&s_conns[i], msg, sizeof(msg));
        }
    }
}

static void ClientAttemptFailed(NetError error, u64 now);

/* A connection is over. */
static void ConnDrop(Conn *c, NetError reason, u64 now) {
    ConnState state = c->state;
    u8 peer = c->peer;

    if (state == CONN_FREE) {
        return;
    }
    ConnFree(c);
    if (s_isHost) {
        if (state == CONN_JOINED) {
            SDL_LockMutex(s_lock);
            PushEvent(NET_EV_PEER_LEFT, peer, 0, reason, NULL, 0);
            SDL_UnlockMutex(s_lock);
            BroadcastLeft(peer, reason);
        }
    } else if (state == CONN_JOINED) {
        SDL_LockMutex(s_lock);
        PushEvent(NET_EV_DISCONNECTED, NET_PEER_HOST, 0, reason, NULL, 0);
        SDL_UnlockMutex(s_lock);
        s_client.phase = CLIENT_DONE;
    } else if (s_client.phase == CLIENT_ATTEMPTING) {
        /* a host that said no means it; anything else may be a host, or a
         * tunnel, still coming up */
        if (reason == NET_ERR_REJECT_VERSION || reason == NET_ERR_REJECT_ROOM_CODE || reason == NET_ERR_REJECT_FULL) {
            SDL_LockMutex(s_lock);
            PushEvent(NET_EV_DISCONNECTED, NET_PEER_HOST, 0, reason, NULL, 0);
            SDL_UnlockMutex(s_lock);
            s_client.phase = CLIENT_DONE;
        } else {
            ClientAttemptFailed(reason, now);
        }
    }
}

/* ---- host: taking a client in -------------------------------------------- */

static u8 FreePeerId(void) {
    u8 id;

    for (id = 1; id < NET_MAX_PLAYERS; id++) {
        u32 i;
        int used = 0;

        for (i = 0; i < MAX_CONNS; i++) {
            if (s_conns[i].state == CONN_JOINED && s_conns[i].peer == id) {
                used = 1;
            }
        }
        if (!used) {
            return id;
        }
    }
    return 0;
}

static void HostReject(Conn *c, NetError reason, u64 now) {
    u8 msg[2];

    msg[0] = KIND_REJECT;
    msg[1] = (u8)reason;
    SendFrame(c, WS_OP_BINARY, msg, sizeof(msg));
    c->state = CONN_CLOSING;
    c->deadline = now + 1000;
}

static void HostHello(Conn *c, const u8 *data, u32 size, u64 now) {
    NetReader r;
    const u8 *code;
    u32 magic, protocol, codeLen, expected, i;
    u8 diff = 0;
    u8 msg[3 + NET_MAX_PLAYERS];
    NetWriter w;
    u8 peer;
    u8 joined[2];

    NetReader_Init(&r, data, size);
    NetReader_U8(&r);
    magic = NetReader_U32(&r);
    protocol = NetReader_U16(&r);
    codeLen = NetReader_U8(&r);
    code = NetReader_Take(&r, codeLen);
    if (!r.ok || magic != NET_MAGIC || r.pos != size) {
        c->dropReason = NET_ERR_PROTOCOL;
        return;
    }
    if (protocol != NET_PROTOCOL) {
        HostReject(c, NET_ERR_REJECT_VERSION, now);
        return;
    }
    expected = (u32)SDL_strlen(s_roomCode);
    /* every byte, wherever the first difference is */
    diff = codeLen != expected;
    for (i = 0; i < codeLen && i < expected; i++) {
        diff |= code[i] ^ (u8)s_roomCode[i];
    }
    if (diff) {
        HostReject(c, NET_ERR_REJECT_ROOM_CODE, now);
        return;
    }
    peer = FreePeerId();
    if (peer == 0) {
        HostReject(c, NET_ERR_REJECT_FULL, now);
        return;
    }
    NetWriter_Init(&w, msg, sizeof(msg));
    NetWriter_U8(&w, KIND_WELCOME);
    NetWriter_U8(&w, peer);
    NetWriter_U8(&w, 0);
    for (i = 0; i < MAX_CONNS; i++) {
        if (s_conns[i].state == CONN_JOINED) {
            NetWriter_U8(&w, s_conns[i].peer);
            msg[2]++;
        }
    }
    joined[0] = KIND_JOINED;
    joined[1] = peer;
    for (i = 0; i < MAX_CONNS; i++) {
        if (s_conns[i].state == CONN_JOINED) {
            SendNotice(&s_conns[i], joined, sizeof(joined));
        }
    }
    SendFrame(c, WS_OP_BINARY, msg, w.size);
    SDL_LockMutex(s_lock);
    c->peer = peer;
    c->state = CONN_JOINED;
    PushEvent(NET_EV_PEER_JOINED, peer, 0, NET_OK, NULL, 0);
    SDL_UnlockMutex(s_lock);
}

/* ---- client: being taken in ---------------------------------------------- */

static void ClientSendHello(Conn *c) {
    u8 msg[8 + NET_MAX_ROOM_CODE];
    NetWriter w;
    u32 codeLen = (u32)SDL_strlen(s_roomCode);

    NetWriter_Init(&w, msg, sizeof(msg));
    NetWriter_U8(&w, KIND_HELLO);
    NetWriter_U32(&w, NET_MAGIC);
    NetWriter_U16(&w, NET_PROTOCOL);
    NetWriter_U8(&w, (u8)codeLen);
    NetWriter_Bytes(&w, s_roomCode, codeLen);
    SendFrame(c, WS_OP_BINARY, msg, w.size);
}

static void ClientWelcome(Conn *c, const u8 *data, u32 size) {
    NetReader r;
    u8 self;
    u32 count, i;

    NetReader_Init(&r, data, size);
    NetReader_U8(&r);
    self = NetReader_U8(&r);
    count = NetReader_U8(&r);
    if (!r.ok || self == NET_PEER_HOST || self >= NET_MAX_PLAYERS || count >= NET_MAX_PLAYERS ||
        size != 3 + count) {
        c->dropReason = NET_ERR_NOT_HOST;
        return;
    }
    SDL_LockMutex(s_lock);
    c->peer = NET_PEER_HOST;
    c->state = CONN_JOINED;
    PushEvent(NET_EV_CONNECTED, self, 0, NET_OK, NULL, 0);
    PushEvent(NET_EV_PEER_JOINED, NET_PEER_HOST, 0, NET_OK, NULL, 0);
    for (i = 0; i < count; i++) {
        u8 peer = NetReader_U8(&r);

        if (peer != NET_PEER_HOST && peer < NET_MAX_PLAYERS && peer != self) {
            PushEvent(NET_EV_PEER_JOINED, peer, 0, NET_OK, NULL, 0);
        }
    }
    SDL_UnlockMutex(s_lock);
    s_client.phase = CLIENT_DONE;
}

/* ---- frames arriving ------------------------------------------------------ */

static void HandleBinary(Conn *c, const u8 *data, u32 size, u64 now) {
    u8 kind;

    if (size == 0) {
        c->dropReason = NET_ERR_PROTOCOL;
        return;
    }
    kind = data[0];
    switch (c->state) {
    case CONN_AWAIT_HELLO:
        if (kind == KIND_HELLO) {
            HostHello(c, data, size, now);
        } else {
            c->dropReason = NET_ERR_PROTOCOL;
        }
        return;
    case CONN_AWAIT_WELCOME:
        if (kind == KIND_WELCOME) {
            ClientWelcome(c, data, size);
        } else if (kind == KIND_REJECT && size == 2 &&
                   (data[1] == NET_ERR_REJECT_VERSION || data[1] == NET_ERR_REJECT_ROOM_CODE || data[1] == NET_ERR_REJECT_FULL)) {
            c->dropReason = (NetError)data[1];
        } else {
            c->dropReason = NET_ERR_NOT_HOST;
        }
        return;
    case CONN_JOINED:
        break;
    default:
        return;                 /* CONN_CLOSING: it is leaving anyway */
    }
    switch (kind) {
    case KIND_RELIABLE:
        SDL_LockMutex(s_lock);
        PushEvent(NET_EV_RELIABLE, c->peer, 0, NET_OK, data + 1, size - 1);
        SDL_UnlockMutex(s_lock);
        break;
    case KIND_LATEST:
        if (size < 2 || data[1] >= NET_LATEST_KEYS) {
            c->dropReason = NET_ERR_PROTOCOL;
            return;
        }
        SDL_LockMutex(s_lock);
        PushEvent(NET_EV_LATEST, c->peer, data[1], NET_OK, data + 2, size - 2);
        SDL_UnlockMutex(s_lock);
        break;
    case KIND_JOINED:
    case KIND_LEFT:
        /* only the host announces peers, and never itself */
        if (s_isHost || size != (kind == KIND_JOINED ? 2u : 3u) || data[1] == NET_PEER_HOST || data[1] >= NET_MAX_PLAYERS) {
            c->dropReason = NET_ERR_PROTOCOL;
            return;
        }
        SDL_LockMutex(s_lock);
        if (kind == KIND_JOINED) {
            PushEvent(NET_EV_PEER_JOINED, data[1], 0, NET_OK, NULL, 0);
        } else {
            NetError why = data[2] <= NET_ERR_BIND ? (NetError)data[2] : NET_ERR_LOST;

            PushEvent(NET_EV_PEER_LEFT, data[1], 0, why, NULL, 0);
        }
        SDL_UnlockMutex(s_lock);
        break;
    default:
        c->dropReason = NET_ERR_PROTOCOL;
        return;
    }
    SDL_LockMutex(s_lock);
    if (s_eventBytes > MAX_EVENT_BYTES) {
        c->dropReason = NET_ERR_OVERFLOW;   /* the game isn't taking them */
    }
    SDL_UnlockMutex(s_lock);
}

/* A whole message or control frame, once any simulated lag has passed. */
static void HandleFrame(Conn *c, u8 opcode, const u8 *data, u32 size, u64 now) {
    switch (opcode) {
    case WS_OP_BINARY:
        HandleBinary(c, data, size, now);
        break;
    case WS_OP_PING:
        SendFrame(c, WS_OP_PONG, data, size);
        break;
    case WS_OP_PONG:
        if (size == 8) {
            u64 sent = 0;
            u32 i;

            for (i = 0; i < 8; i++) {
                sent |= (u64)data[i] << (i * 8);
            }
            if (sent <= now && now - sent < 3600000) {
                SDL_LockMutex(s_lock);
                c->stats.rttMs = (u32)(now - sent);
                SDL_UnlockMutex(s_lock);
            }
        }
        break;
    case WS_OP_CLOSE:
        c->dropReason = s_isHost ? NET_ERR_PEER_CLOSED : NET_ERR_HOST_CLOSED;
        break;
    default:
        c->dropReason = NET_ERR_PROTOCOL;   /* this game only sends binary */
        break;
    }
}

static void DelayOrHandle(Conn *c, u8 opcode, const u8 *data, u32 size, u64 now) {
    DelayedFrame *d;

    if (!s_sim.enabled) {
        HandleFrame(c, opcode, data, size, now);
        return;
    }
    d = SDL_malloc(sizeof(DelayedFrame) + size);
    if (d == NULL) {
        c->dropReason = NET_ERR_LOST;
        return;
    }
    d->next = NULL;
    d->due = c->lastDue = NetSim_Due(&s_sim, now, c->lastDue);
    d->opcode = opcode;
    d->size = size;
    if (size != 0) {
        SDL_memcpy(d->data, data, size);
    }
    if (c->delayTail != NULL) {
        c->delayTail->next = d;
    } else {
        c->delayHead = d;
    }
    c->delayTail = d;
}

/* flush: the connection ended, so what it already received is delivered now. */
static void RunDelayed(Conn *c, u64 now, int flush) {
    NetError ending = c->dropReason;

    c->dropReason = NET_OK;
    while (c->delayHead != NULL && c->dropReason == NET_OK && (flush || c->delayHead->due <= now)) {
        DelayedFrame *d = c->delayHead;

        c->delayHead = d->next;
        if (c->delayHead == NULL) {
            c->delayTail = NULL;
        }
        HandleFrame(c, d->opcode, d->data, d->size, now);
        SDL_free(d);
    }
    /* a close frame among them says more than the end of the stream does */
    if (c->dropReason == NET_OK) {
        c->dropReason = ending;
    }
}

static void HandleWsFrame(Conn *c, const WsFrame *f, u64 now) {
    if (f->opcode >= WS_OP_CLOSE) {
        DelayOrHandle(c, f->opcode, f->payload, f->size, now);
    } else if (f->opcode == WS_OP_TEXT) {
        c->dropReason = NET_ERR_PROTOCOL;
    } else if (f->opcode == WS_OP_BINARY && !c->msgOpen && f->fin) {
        DelayOrHandle(c, WS_OP_BINARY, f->payload, f->size, now);
    } else if ((f->opcode == WS_OP_BINARY) == c->msgOpen || f->size > MAX_PAYLOAD - c->msgLen) {
        c->dropReason = NET_ERR_PROTOCOL;   /* out of order, or too long in all */
    } else {
        /* A proxy may split a message; the WebSocket rules let it. */
        if (f->size != 0) {
            SDL_memcpy(c->msg + c->msgLen, f->payload, f->size);
        }
        c->msgLen += f->size;
        c->msgOpen = 1;
        if (f->fin) {
            c->msgOpen = 0;
            DelayOrHandle(c, WS_OP_BINARY, c->msg, c->msgLen, now);
            c->msgLen = 0;
        }
    }
}

/* The HTTP head of the handshake, if it is all here; removes it from rx. */
static void HandleHttp(Conn *c, u64 now) {
    static const char notFound[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    char head[WS_HTTP_MAX + 1];
    u32 len = Ws_HeadLength(c->rx, c->rxLen < WS_HTTP_MAX ? c->rxLen : WS_HTTP_MAX);

    if (len == 0) {
        if (c->rxLen >= WS_HTTP_MAX) {
            c->dropReason = s_isHost ? NET_ERR_PROTOCOL : NET_ERR_NOT_HOST;
        }
        return;
    }
    SDL_memcpy(head, c->rx, len);
    head[len] = 0;
    c->rxLen -= len;
    SDL_memmove(c->rx, c->rx + len, c->rxLen);
    if (s_isHost) {
        char key[WS_KEY_MAX];
        char accept[WS_ACCEPT_LEN + 1];
        char response[256];

        if (!Ws_ParseRequest(head, key)) {
            /* a browser, or a tunnel's health check */
            SendRaw(c, notFound, sizeof(notFound) - 1);
            c->state = CONN_CLOSING;
            c->deadline = now + 1000;
            return;
        }
        Ws_AcceptKey(key, accept);
        SDL_snprintf(response, sizeof(response),
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
        SendRaw(c, response, (u32)SDL_strlen(response));
        c->state = CONN_AWAIT_HELLO;
    } else {
        if (!Ws_CheckResponse(head, s_client.wsKey)) {
            c->dropReason = NET_ERR_NOT_HOST;
            return;
        }
        c->state = CONN_AWAIT_WELCOME;
        ClientSendHello(c);
    }
}

static void ProcessInput(Conn *c, u64 now) {
    if (c->state == CONN_HTTP) {
        HandleHttp(c, now);
    }
    while (c->state != CONN_HTTP && c->dropReason == NET_OK && c->rxLen != 0) {
        WsFrame frame;
        s32 used = Ws_ParseFrame(c->rx, c->rxLen, s_isHost, MAX_PAYLOAD, &frame);

        if (used < 0) {
            c->dropReason = s_isHost ? NET_ERR_PROTOCOL : NET_ERR_NOT_HOST;
            return;
        }
        if (used == 0) {
            return;
        }
        HandleWsFrame(c, &frame, now);
        c->rxLen -= (u32)used;
        SDL_memmove(c->rx, c->rx + used, c->rxLen);
    }
}

static void ConnRead(Conn *c, u64 now) {
    int reads;

    /* a few reads, then let the other connections have a turn */
    for (reads = 0; reads < 8 && c->dropReason == NET_OK; reads++) {
        s32 got = (s32)recv(c->sock, (char *)c->rx + c->rxLen, (int)(RX_CAP - c->rxLen), 0);

        if (got > 0) {
            c->rxLen += (u32)got;
            c->lastRecv = now;
            SDL_LockMutex(s_lock);
            c->stats.bytesIn += (u32)got;
            SDL_UnlockMutex(s_lock);
            ProcessInput(c, now);
        } else if (got == 0) {
            c->dropReason = s_isHost ? NET_ERR_LOST : NET_ERR_HOST_CLOSED;
            return;
        } else {
            int e = NetLastError();

            if (!NET_WOULD_BLOCK(e) && !NET_INTERRUPTED(e)) {
                c->dropReason = NET_ERR_LOST;
            }
            return;
        }
    }
}

/* ---- frames leaving ------------------------------------------------------- */

/* Moves the next frame into tx. Returns 0 if nothing is waiting. */
static int FillTx(Conn *c) {
    QueueNode *node = NULL;
    Latest *latest = NULL;
    u8 prefix[2];
    u32 prefixLen = 0;
    u8 maskBytes[4];
    const u8 *mask = NULL;
    static u8 frameData[MAX_PAYLOAD];  /* this thread's only */
    u32 i;
    int found = 0;

    SDL_LockMutex(s_lock);
    /* the handshake and the keepalives go first, or a backlog could time the
     * connection out; then a latest and a reliable by turns */
    node = QueuePop(&c->control);
    if (node == NULL && c->state == CONN_JOINED) {
        int tryLatest = c->preferLatest || c->reliable.head == NULL;

        if (tryLatest) {
            for (i = 0; i < NET_LATEST_KEYS; i++) {
                u32 k = (c->nextLatest + i) % NET_LATEST_KEYS;

                if (c->latest[k].pending) {
                    latest = &c->latest[k];
                    c->nextLatest = (u8)((k + 1) % NET_LATEST_KEYS);
                    prefix[0] = KIND_LATEST;
                    prefix[1] = (u8)k;
                    prefixLen = 2;
                    break;
                }
            }
        }
        if (latest == NULL) {
            node = QueuePop(&c->reliable);
            c->preferLatest = 1;
        } else {
            c->preferLatest = 0;
        }
    }
    if (node != NULL) {
        found = 1;
        if (node->opcode == OP_RAW) {
            SDL_memcpy(c->tx, node->data, node->size);
            c->txLen = node->size;
        } else {
            if (!s_isHost) {
                u32 m = NextRandom();

                SDL_memcpy(maskBytes, &m, 4);
                mask = maskBytes;
            }
            c->txLen = Ws_BuildFrame(c->tx, node->opcode, node->data, node->size, mask);
        }
        c->txOff = 0;
        SDL_free(node);
    } else if (latest != NULL) {
        found = 1;
        if (!s_isHost) {
            u32 m = NextRandom();

            SDL_memcpy(maskBytes, &m, 4);
            mask = maskBytes;
        }
        SDL_memcpy(frameData, prefix, prefixLen);
        SDL_memcpy(frameData + prefixLen, latest->data, latest->size);
        c->txLen = Ws_BuildFrame(c->tx, WS_OP_BINARY, frameData, prefixLen + latest->size, mask);
        c->txOff = 0;
        latest->pending = 0;
    }
    SDL_UnlockMutex(s_lock);
    return found;
}

static void ConnWrite(Conn *c, u64 now) {
    u32 budget = 0xFFFFFFFF;

    if (s_sim.enabled && s_sim.bandwidth != 0) {
        c->bwTokens += (double)(now - c->bwTime) * s_sim.bandwidth / 1000.0;
        c->bwTime = now;
        /* a frame's worth of burst, at most: otherwise an idle moment would
         * save up enough to hide the limit */
        if (c->bwTokens > 2048.0) {
            c->bwTokens = 2048.0;
        }
        budget = (u32)c->bwTokens;
    }
    while (c->dropReason == NET_OK && budget != 0) {
        u32 n;
        s32 sent;

        if (c->txOff >= c->txLen && !FillTx(c)) {
            return;
        }
        n = c->txLen - c->txOff;
        if (n > budget) {
            n = budget;
        }
        sent = NetSend(c->sock, c->tx + c->txOff, n);
        if (sent < 0) {
            int e = NetLastError();

            if (!NET_WOULD_BLOCK(e) && !NET_INTERRUPTED(e)) {
                c->dropReason = NET_ERR_LOST;
            }
            return;
        }
        if (budget != 0xFFFFFFFF) {
            budget -= (u32)sent;
            c->bwTokens -= sent;
        }
        SDL_LockMutex(s_lock);
        c->txOff += (u32)sent;  /* Net_GetStats counts what's left */
        c->stats.bytesOut += (u32)sent;
        SDL_UnlockMutex(s_lock);
        if ((u32)sent < n) {
            return;             /* the kernel is full; select says when */
        }
    }
}

/* ---- client: reaching the host ------------------------------------------- */

static int ResolveThread(void *arg) {
    ResolveJob *job = arg;
    struct addrinfo hints;

    SDL_zero(hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(job->host, job->port, &hints, &job->result) != 0) {
        job->result = NULL;
    }
    SDL_SetAtomicInt(&job->done, 1);
    if (SDL_AddAtomicInt(&job->refs, -1) == 1) {
        if (job->result != NULL) {
            freeaddrinfo(job->result);
        }
        SDL_free(job);
    }
    return 0;
}

/* getaddrinfo can't be cancelled and may take many seconds, so it gets its own
 * thread, which Net_Close doesn't wait for: whichever finishes last frees. */
static int StartResolve(void) {
    ResolveJob *job = SDL_calloc(1, sizeof(ResolveJob));
    SDL_Thread *thread;

    if (job == NULL) {
        return 0;
    }
    SDL_SetAtomicInt(&job->refs, 2);
    SDL_strlcpy(job->host, s_client.host, sizeof(job->host));
    SDL_snprintf(job->port, sizeof(job->port), "%u", s_client.port);
    thread = SDL_CreateThread(ResolveThread, "net-resolve", job);
    if (thread == NULL) {
        SDL_free(job);
        return 0;
    }
    SDL_DetachThread(thread);
    s_client.job = job;
    return 1;
}

static void ReleaseResolve(void) {
    ResolveJob *job = s_client.job;

    s_client.job = NULL;
    if (job != NULL && SDL_AddAtomicInt(&job->refs, -1) == 1) {
        if (job->result != NULL) {
            freeaddrinfo(job->result);
        }
        SDL_free(job);
    }
}

static void ClientGiveUp(NetError error) {
    SDL_LockMutex(s_lock);
    PushEvent(NET_EV_DISCONNECTED, NET_PEER_HOST, 0, error, NULL, 0);
    SDL_UnlockMutex(s_lock);
    s_client.phase = CLIENT_DONE;
    s_client.addr = NULL;
}

/* Everything failed this time: wait a little and resolve again, until the
 * deadline. A tunnel's name can take a while to appear (measured: over 80 s if
 * looked up too soon), and a host may still be starting. */
static void ClientAttemptFailed(NetError error, u64 now) {
    s_client.lastError = error;
    s_client.addr = NULL;
    if (now >= s_client.deadline) {
        ClientGiveUp(error);
        return;
    }
    s_client.phase = CLIENT_WAITING;
    s_client.retryAt = now + s_client.retryMs;
    s_client.retryMs = s_client.retryMs * 2 < RETRY_MAX_MS ? s_client.retryMs * 2 : RETRY_MAX_MS;
}

/* Tries addresses from s_client.addr until a connect is under way. */
static void ClientTryNextAddress(u64 now) {
    for (; s_client.addr != NULL; s_client.addr = s_client.addr->ai_next) {
        struct addrinfo *a = s_client.addr;
        NetSocket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        Conn *c;

        if (s == NET_NO_SOCKET) {
            continue;
        }
        if (!FitsSelect(s) || !SetNonBlocking(s)) {
            NetCloseSocket(s);
            continue;
        }
        TuneSocket(s);
        if (connect(s, a->ai_addr, (socklen_t)a->ai_addrlen) != 0 && !NET_IN_PROGRESS(NetLastError())) {
            NetCloseSocket(s);
            continue;
        }
        c = ConnOpen(s, CONN_CONNECTING, now);
        if (c == NULL) {
            NetCloseSocket(s);
            ClientAttemptFailed(NET_ERR_LOST, now);
            return;
        }
        s_client.attemptEnd = now + CONNECT_ATTEMPT_MS;
        s_client.phase = CLIENT_ATTEMPTING;
        return;
    }
    ClientAttemptFailed(NET_ERR_UNREACHABLE, now);
}

static void ClientConnectFinished(Conn *c, u64 now) {
    int err = 0;
    socklen_t len = sizeof(err);
    char request[640];
    u8 keyBytes[16];
    char hostHeader[280];
    u32 i;

    if (getsockopt(c->sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err != 0) {
        /* on to the next address, if there is one */
        ConnFree(c);
        s_client.addr = s_client.addr != NULL ? s_client.addr->ai_next : NULL;
        ClientTryNextAddress(now);
        return;
    }
    for (i = 0; i < 16; i++) {
        keyBytes[i] = (u8)NextRandom();
    }
    Ws_Base64(keyBytes, sizeof(keyBytes), s_client.wsKey);
    /* the Host header is what routes a request through a tunnel */
    if (SDL_strchr(s_client.host, ':') != NULL) {
        SDL_snprintf(hostHeader, sizeof(hostHeader), "[%s]:%u", s_client.host, s_client.port);
    } else if (s_client.port == 80) {
        SDL_strlcpy(hostHeader, s_client.host, sizeof(hostHeader));
    } else {
        SDL_snprintf(hostHeader, sizeof(hostHeader), "%s:%u", s_client.host, s_client.port);
    }
    SDL_snprintf(request, sizeof(request),
                 "GET " WS_PATH " HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", hostHeader, s_client.wsKey);
    c->state = CONN_HTTP;
    c->deadline = now + HANDSHAKE_MS;
    SendRaw(c, request, (u32)SDL_strlen(request));
}

static void ClientStep(u64 now) {
    switch (s_client.phase) {
    case CLIENT_RESOLVING:
        if (SDL_GetAtomicInt(&s_client.job->done)) {
            /* the job's addresses stay valid until ReleaseResolve */
            s_client.addrs = s_client.job->result;
            s_client.addr = s_client.addrs;
            if (s_client.addr == NULL) {
                ReleaseResolve();
                ClientAttemptFailed(NET_ERR_ADDRESS, now);
            } else {
                ClientTryNextAddress(now);
            }
        } else if (now >= s_client.deadline) {
            ClientGiveUp(NET_ERR_ADDRESS);
        }
        break;
    case CLIENT_WAITING:
        if (now >= s_client.retryAt) {
            ReleaseResolve();
            if (StartResolve()) {
                s_client.phase = CLIENT_RESOLVING;
            } else {
                ClientGiveUp(NET_ERR_LOST);
            }
        }
        break;
    case CLIENT_ATTEMPTING:
        if (s_conns[0].state == CONN_CONNECTING && now >= s_client.attemptEnd) {
            ConnFree(&s_conns[0]);
            s_client.addr = s_client.addr != NULL ? s_client.addr->ai_next : NULL;
            ClientTryNextAddress(now);
        }
        break;
    default:
        break;
    }
}

/* ---- the thread ----------------------------------------------------------- */

static void AcceptClients(u64 now) {
    int i;

    for (i = 0; i < MAX_CONNS; i++) {
        NetSocket s = accept(s_listen, NULL, NULL);

        if (s == NET_NO_SOCKET) {
            return;
        }
        if (!FitsSelect(s) || !SetNonBlocking(s)) {
            NetCloseSocket(s);
            continue;
        }
        TuneSocket(s);
        if (ConnOpen(s, CONN_HTTP, now) == NULL) {
            NetCloseSocket(s);  /* no room even to say so: strangers are holding the slots */
        }
    }
}

static void ConnTimers(Conn *c, u64 now) {
    SDL_LockMutex(s_lock);
    if (c->overflowed) {
        c->dropReason = NET_ERR_OVERFLOW;
    }
    SDL_UnlockMutex(s_lock);
    switch (c->state) {
    case CONN_HTTP:
    case CONN_AWAIT_HELLO:
    case CONN_AWAIT_WELCOME:
        if (now >= c->deadline) {
            c->dropReason = NET_ERR_TIMEOUT;
        }
        break;
    case CONN_CLOSING:
        SDL_LockMutex(s_lock);
        if (!ConnHasOutput(c) || now >= c->deadline) {
            c->dropReason = NET_ERR_LOST;
        }
        SDL_UnlockMutex(s_lock);
        break;
    case CONN_JOINED:
        if (now - c->lastRecv >= SILENT_MS) {
            c->dropReason = NET_ERR_TIMEOUT;
        } else if (now - c->lastPing >= PING_MS) {
            u8 stamp[8];
            u32 i;

            for (i = 0; i < 8; i++) {
                stamp[i] = (u8)(now >> (i * 8));
            }
            c->lastPing = now;
            SendFrame(c, WS_OP_PING, stamp, sizeof(stamp));
        }
        break;
    default:
        break;
    }
}

static int NetThread(void *unused) {
    (void)unused;

    while (!SDL_GetAtomicInt(&s_stop)) {
        fd_set reads, writes, errors;
        struct timeval timeout;
        NetSocket maxSock = 0;
        u64 now = SDL_GetTicks();
        u32 waitMs = s_wakeRead != NET_NO_SOCKET ? 100 : 5;
        u32 i;

#define WATCH(set, s) do { FD_SET((s), (set)); if ((s) > maxSock) { maxSock = (s); } } while (0)
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_ZERO(&errors);
        if (s_wakeRead != NET_NO_SOCKET) {
            WATCH(&reads, s_wakeRead);
        }
        if (s_listen != NET_NO_SOCKET) {
            WATCH(&reads, s_listen);
        }
        if (s_client.phase == CLIENT_RESOLVING && waitMs > 20) {
            waitMs = 20;
        }
        for (i = 0; i < MAX_CONNS; i++) {
            Conn *c = &s_conns[i];
            int output;

            if (c->state == CONN_FREE) {
                continue;
            }
            if (c->state == CONN_CONNECTING) {
                WATCH(&writes, c->sock);
                WATCH(&errors, c->sock);    /* where Windows reports a failed connect */
                continue;
            }
            WATCH(&reads, c->sock);
            SDL_LockMutex(s_lock);
            output = ConnHasOutput(c);
            SDL_UnlockMutex(s_lock);
            if (output) {
                if (s_sim.enabled && s_sim.bandwidth != 0) {
                    waitMs = 1;     /* the budget refills with time, not with the socket */
                } else {
                    WATCH(&writes, c->sock);
                }
            }
            if (c->delayHead != NULL) {
                u32 until = c->delayHead->due > now ? (u32)SDL_min(c->delayHead->due - now, 100) : 0;

                if (until < waitMs) {
                    waitMs = until;
                }
            }
        }
#undef WATCH
        timeout.tv_sec = 0;
        timeout.tv_usec = (int)waitMs * 1000;
        if (select((int)maxSock + 1, &reads, &writes, &errors, &timeout) < 0) {
            int e = NetLastError();

            if (!NET_INTERRUPTED(e)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: select failed: %d", e);
                SDL_Delay(1);
            }
            continue;
        }
        now = SDL_GetTicks();
        if (s_wakeRead != NET_NO_SOCKET && FD_ISSET(s_wakeRead, &reads)) {
            DrainWake();
        }
        if (s_listen != NET_NO_SOCKET && FD_ISSET(s_listen, &reads)) {
            AcceptClients(now);
        }
        for (i = 0; i < MAX_CONNS; i++) {
            Conn *c = &s_conns[i];

            if (c->state == CONN_FREE) {
                continue;
            }
            if (c->state == CONN_CONNECTING) {
                if (FD_ISSET(c->sock, &writes) || FD_ISSET(c->sock, &errors)) {
                    ClientConnectFinished(c, now);
                }
                continue;
            }
            if (FD_ISSET(c->sock, &reads)) {
                ConnRead(c, now);
            }
            RunDelayed(c, now, c->dropReason != NET_OK);
            if (c->dropReason == NET_OK) {
                ConnTimers(c, now);
            }
            if (c->dropReason == NET_OK) {
                ConnWrite(c, now);
            }
            if (c->dropReason != NET_OK) {
                ConnDrop(c, c->dropReason, now);
            }
        }
        ClientStep(now);
    }

    /* Leaving: send what's queued, briefly, then say so. */
    {
        u64 end = SDL_GetTicks() + CLOSE_DRAIN_MS;
        u32 i;

        for (;;) {
            int waiting = 0;
            u64 now = SDL_GetTicks();

            for (i = 0; i < MAX_CONNS; i++) {
                Conn *c = &s_conns[i];

                if (c->state == CONN_JOINED && c->dropReason == NET_OK) {
                    ConnWrite(c, now);
                    SDL_LockMutex(s_lock);
                    waiting |= c->dropReason == NET_OK && ConnHasOutput(c);
                    SDL_UnlockMutex(s_lock);
                }
            }
            if (!waiting || now >= end) {
                break;
            }
            SDL_Delay(1);
        }
        for (i = 0; i < MAX_CONNS; i++) {
            Conn *c = &s_conns[i];

            if (c->state == CONN_JOINED && c->dropReason == NET_OK && c->txOff >= c->txLen) {
                u8 frame[WS_MAX_HEADER];
                u32 m = NextRandom();
                u32 n = Ws_BuildFrame(frame, WS_OP_CLOSE, NULL, 0, s_isHost ? NULL : (const u8 *)&m);

                NetSend(c->sock, frame, n);
            }
            ConnFree(c);
        }
    }
    return 0;
}

/* ---- the API --------------------------------------------------------------- */

static int StartSession(int isHost, const char *roomCode) {
    u32 i;

    if (s_active) {
        Net_Close();
    }
    if (!NetStartup()) {
        return 0;
    }
    if (s_lock == NULL) {
        s_lock = SDL_CreateMutex();
        if (s_lock == NULL) {
            return 0;
        }
    }
    SDL_SetAtomicInt(&s_stop, 0);
    s_isHost = isHost;
    s_rng = (u32)SDL_GetPerformanceCounter() | 1;
    SDL_strlcpy(s_roomCode, roomCode != NULL ? roomCode : "", sizeof(s_roomCode));
    for (i = 0; i < MAX_CONNS; i++) {
        SDL_zero(s_conns[i]);
        s_conns[i].sock = NET_NO_SOCKET;
    }
    if (!NetSim_Parse(&s_sim, SDL_getenv("HWVX_NET_SIM"))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: HWVX_NET_SIM not understood, ignored");
    } else if (s_sim.enabled) {
        SDL_Log("net: simulating lat=%u jitter=%u loss=%.2f%% stall=%u bw=%u B/s", s_sim.latencyMs, s_sim.jitterMs,
                s_sim.lossPer10k / 100.0, s_sim.stallMs, s_sim.bandwidth);
    }
    OpenWakeSocket();
    return 1;
}

static int StartThread(void) {
    s_thread = SDL_CreateThread(NetThread, "net", NULL);
    if (s_thread == NULL) {
        return 0;
    }
    s_active = 1;
    return 1;
}

static NetSocket OpenListener(u16 port, int loopbackOnly) {
    NetSocket s;

    if (!loopbackOnly) {
        /* both IPv6 and IPv4, where the system allows it */
        struct sockaddr_in6 addr6;

        s = socket(AF_INET6, SOCK_STREAM, 0);
        if (s != NET_NO_SOCKET) {
            SetOption(s, IPPROTO_IPV6, IPV6_V6ONLY, 0);
#ifndef _WIN32
            SetOption(s, SOL_SOCKET, SO_REUSEADDR, 1);  /* on Windows this means port sharing */
#endif
            SDL_zero(addr6);
            addr6.sin6_family = AF_INET6;
            addr6.sin6_port = htons(port);
            addr6.sin6_addr = in6addr_any;
            if (bind(s, (struct sockaddr *)&addr6, sizeof(addr6)) == 0 && listen(s, 8) == 0 &&
                SetNonBlocking(s) && FitsSelect(s)) {
                return s;
            }
            NetCloseSocket(s);
        }
    }
    {
        struct sockaddr_in addr4;

        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == NET_NO_SOCKET) {
            return NET_NO_SOCKET;
        }
#ifndef _WIN32
        SetOption(s, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
        SDL_zero(addr4);
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port);
        addr4.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        if (bind(s, (struct sockaddr *)&addr4, sizeof(addr4)) == 0 && listen(s, 8) == 0 &&
            SetNonBlocking(s) && FitsSelect(s)) {
            return s;
        }
        NetCloseSocket(s);
    }
    return NET_NO_SOCKET;
}

int Net_Host(u16 port, const char *roomCode, int loopbackOnly) {
    if (!StartSession(1, roomCode)) {
        return NET_ERR_BIND;
    }
    s_listen = OpenListener(port, loopbackOnly);
    if (s_listen == NET_NO_SOCKET) {
        Net_Close();
        return NET_ERR_BIND;
    }
    if (!StartThread()) {
        Net_Close();
        return NET_ERR_BIND;
    }
    return NET_OK;
}

/* Splits an address into s_client.host and port; 0 if it can't be understood. */
static int ParseAddress(const char *address) {
    const char *p = address;
    const char *hostEnd;
    const char *portText = NULL;
    size_t n;

    while (*p == ' ') {
        p++;
    }
    if (SDL_strncasecmp(p, "ws://", 5) == 0) {
        p += 5;
    } else if (SDL_strstr(p, "://") != NULL) {
        return 0;               /* wss:// comes with TLS, in a later milestone */
    }
    if (*p == '[') {
        hostEnd = SDL_strchr(p, ']');
        if (hostEnd == NULL) {
            return 0;
        }
        p++;
        if (hostEnd[1] == ':') {
            portText = hostEnd + 2;
        } else if (hostEnd[1] != 0 && hostEnd[1] != '/' && hostEnd[1] != ' ') {
            return 0;
        }
    } else {
        const char *colon = SDL_strchr(p, ':');

        if (colon != NULL && SDL_strchr(colon + 1, ':') != NULL) {
            /* more than one colon: a bare IPv6 address */
            hostEnd = p + SDL_strlen(p);
        } else if (colon != NULL) {
            hostEnd = colon;
            portText = colon + 1;
        } else {
            hostEnd = p;
            while (*hostEnd != 0 && *hostEnd != '/' && *hostEnd != ' ') {
                hostEnd++;
            }
        }
    }
    n = (size_t)(hostEnd - p);
    while (n != 0 && p[n - 1] == ' ') {
        n--;
    }
    if (n == 0 || n >= sizeof(s_client.host)) {
        return 0;
    }
    SDL_memcpy(s_client.host, p, n);
    s_client.host[n] = 0;
    s_client.port = NET_DEFAULT_PORT;
    if (portText != NULL) {
        char *end;
        long port = SDL_strtol(portText, &end, 10);

        if (end == portText || port < 1 || port > 65535 || (*end != 0 && *end != '/' && *end != ' ')) {
            return 0;
        }
        s_client.port = (u16)port;
    }
    return 1;
}

int Net_Connect(const char *address, const char *roomCode) {
    if (address == NULL) {
        return NET_ERR_ADDRESS;
    }
    if (!StartSession(0, roomCode)) {
        return NET_ERR_LOST;
    }
    SDL_zero(s_client);
    if (!ParseAddress(address)) {
        Net_Close();
        return NET_ERR_ADDRESS;
    }
    s_client.deadline = SDL_GetTicks() + CONNECT_DEADLINE_MS;
    s_client.retryMs = RETRY_FIRST_MS;
    s_client.lastError = NET_ERR_UNREACHABLE;
    if (!StartResolve()) {
        Net_Close();
        return NET_ERR_LOST;
    }
    s_client.phase = CLIENT_RESOLVING;
    if (!StartThread()) {
        Net_Close();
        return NET_ERR_LOST;
    }
    return NET_OK;
}

void Net_Close(void) {
    u32 i;
    EventNode *e;

    if (s_thread != NULL) {
        SDL_SetAtomicInt(&s_stop, 1);
        Wake();
        SDL_WaitThread(s_thread, NULL);
        s_thread = NULL;
    }
    for (i = 0; i < MAX_CONNS; i++) {
        ConnFree(&s_conns[i]);
    }
    ReleaseResolve();
    SDL_zero(s_client);
    if (s_listen != NET_NO_SOCKET) {
        NetCloseSocket(s_listen);
        s_listen = NET_NO_SOCKET;
    }
    if (s_wakeRead != NET_NO_SOCKET) {
        NetCloseSocket(s_wakeRead);
        s_wakeRead = s_wakeWrite = NET_NO_SOCKET;
    }
    SDL_free(s_polled);
    s_polled = NULL;
    while ((e = s_eventHead) != NULL) {
        s_eventHead = e->next;
        SDL_free(e);
    }
    s_eventTail = NULL;
    s_eventBytes = 0;
    s_active = 0;
}

int Net_IsActive(void) {
    return s_active;
}

/* With s_lock held. */
static int QueueReliable(Conn *c, const void *data, u32 size) {
    u8 kind = KIND_RELIABLE;

    if (c->reliable.bytes + size > MAX_QUEUED) {
        c->overflowed = 1;      /* it would only fall further behind */
        return 0;
    }
    return QueuePush(&c->reliable, WS_OP_BINARY, &kind, 1, data, size);
}

/* With s_lock held. */
static int QueueLatest(Conn *c, u8 key, const void *data, u32 size) {
    Latest *slot = &c->latest[key];

    if (slot->cap < size) {
        u8 *grown = SDL_realloc(slot->data, size);

        if (grown == NULL) {
            return 0;
        }
        slot->data = grown;
        slot->cap = size;
    }
    if (slot->pending) {
        c->stats.staleDropped++;
    }
    if (size != 0) {
        SDL_memcpy(slot->data, data, size);
    }
    slot->size = size;
    slot->pending = 1;
    return 1;
}

static int SendTo(u8 peer, int latest, u8 key, const void *data, u32 size) {
    int sent = 0;
    u32 i;

    if (!s_active || size > NET_MAX_MESSAGE || (latest && key >= NET_LATEST_KEYS) || (data == NULL && size != 0)) {
        return 0;
    }
    SDL_LockMutex(s_lock);
    for (i = 0; i < MAX_CONNS; i++) {
        Conn *c = &s_conns[i];

        if (c->state == CONN_JOINED && (peer == NET_PEER_ALL || c->peer == peer)) {
            sent |= latest ? QueueLatest(c, key, data, size) : QueueReliable(c, data, size);
        }
    }
    SDL_UnlockMutex(s_lock);
    if (sent) {
        Wake();
    }
    return sent;
}

int Net_SendReliable(u8 peer, const void *data, u32 size) {
    return SendTo(peer, 0, 0, data, size);
}

int Net_SendLatest(u8 peer, u8 key, const void *data, u32 size) {
    return SendTo(peer, 1, key, data, size);
}

int Net_Poll(NetEvent *event) {
    EventNode *node;

    if (s_lock == NULL) {
        return 0;
    }
    SDL_free(s_polled);
    s_polled = NULL;
    SDL_LockMutex(s_lock);
    node = s_eventHead;
    if (node != NULL) {
        s_eventHead = node->next;
        if (s_eventHead == NULL) {
            s_eventTail = NULL;
        }
        s_eventBytes -= node->event.size;
    }
    SDL_UnlockMutex(s_lock);
    if (node == NULL) {
        return 0;
    }
    *event = node->event;
    s_polled = node;
    return 1;
}

int Net_GetStats(u8 peer, NetStats *stats) {
    int found = 0;
    u32 i;

    if (s_lock == NULL) {
        return 0;
    }
    SDL_LockMutex(s_lock);
    for (i = 0; i < MAX_CONNS; i++) {
        Conn *c = &s_conns[i];

        if (c->state == CONN_JOINED && c->peer == peer) {
            u32 k;

            *stats = c->stats;
            stats->queuedBytes = c->control.bytes + c->reliable.bytes + (c->txLen - c->txOff);
            for (k = 0; k < NET_LATEST_KEYS; k++) {
                if (c->latest[k].pending) {
                    stats->queuedBytes += c->latest[k].size;
                }
            }
            found = 1;
            break;
        }
    }
    SDL_UnlockMutex(s_lock);
    return found;
}
