/*
 * nettest.c - tests the online transport (src/port/net*.c) without the game:
 * it needs only SDL and sockets, and neither game data nor a window.
 *
 *   nettest selftest                     protocol code, no sockets
 *   nettest e2e                          whole sessions: runs copies of itself
 *                                        as a host and clients on this machine
 *   nettest host PORT ROOM PEERS SEC [MAX]  one echoing host; MAX players (default 4)
 *   nettest client ADDR ROOM COUNT SEC   one client, checking everything it gets
 *   nettest expect-error ADDR ROOM ERR   one client that must be refused with ERR
 *
 * Run it optimized too: masking once passed at -O0 and failed at -O2.
 *
 * host and client also run by hand across two machines; HWVX_NET_SIM (see
 * src/port/net_sim.c) makes the connection bad. Exit status 0 means passed.
 *
 * Client messages: reliable ones are u32 sequence + a size that varies + bytes
 * derived from both, so a lost, repeated, reordered or damaged one is caught.
 * Latest ones carry a rising counter, which may skip but never go back.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

#include "net_internal.h"

static int s_failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

/* ---- selftest ------------------------------------------------------------- */

static void TestHandshake(void) {
    char accept[WS_ACCEPT_LEN + 1];
    char out[64];
    char key[WS_KEY_MAX];
    u8 digest[20];
    /* cloudflared's way of writing them: another case, another order, a list */
    static const char tunnelRequest[] =
        "GET /hwvx HTTP/1.1\r\nHost: a-b-c-d.trycloudflare.com\r\ncf-ray: 1\r\nconnection: keep-alive, Upgrade\r\n"
        "UPGRADE: WebSocket\r\nsec-websocket-version:13\r\nSEC-WEBSOCKET-KEY:   dGhlIHNhbXBsZSBub25jZQ==  \r\n\r\n";

    /* FIPS 180 and RFC 4648 and RFC 6455 section 1.3's vectors */
    Ws_Sha1("abc", 3, digest);
    CHECK(digest[0] == 0xA9 && digest[1] == 0x99 && digest[19] == 0x9D);
    Ws_Sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, digest);
    CHECK(digest[0] == 0x84 && digest[1] == 0x98 && digest[19] == 0xF1);
    Ws_Base64("foob", 4, out);
    CHECK(SDL_strcmp(out, "Zm9vYg==") == 0);
    Ws_Base64("fooba", 5, out);
    CHECK(SDL_strcmp(out, "Zm9vYmE=") == 0);
    Ws_Base64("foobar", 6, out);
    CHECK(SDL_strcmp(out, "Zm9vYmFy") == 0);
    Ws_AcceptKey("dGhlIHNhbXBsZSBub25jZQ==", accept);
    CHECK(SDL_strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

    CHECK(Ws_ParseRequest(tunnelRequest, key));
    CHECK(SDL_strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);
    CHECK(!Ws_ParseRequest("GET /hwvx HTTP/1.1\r\nHost: x\r\n\r\n", key));
    CHECK(!Ws_ParseRequest("GET /other HTTP/1.1\r\nUpgrade: websocket\r\nConnection: upgrade\r\n"
                           "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: a\r\n\r\n", key));
    CHECK(!Ws_ParseRequest("GET /hwvxtra HTTP/1.1\r\nUpgrade: websocket\r\nConnection: upgrade\r\n"
                           "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: a\r\n\r\n", key));
    CHECK(!Ws_ParseRequest("GET /hwvx HTTP/1.1\r\nUpgrade: websocket\r\nConnection: upgrade\r\n"
                           "Sec-WebSocket-Version: 8\r\nSec-WebSocket-Key: a\r\n\r\n", key));
    CHECK(Ws_CheckResponse("HTTP/1.1 101 Switching Protocols\r\nsec-websocket-accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
                           "dGhlIHNhbXBsZSBub25jZQ=="));
    CHECK(!Ws_CheckResponse("HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: AAAA\r\n\r\n", "dGhlIHNhbXBsZSBub25jZQ=="));
    CHECK(!Ws_CheckResponse("HTTP/1.1 530 Origin Unreachable\r\nContent-Length: 0\r\n\r\n", "dGhlIHNhbXBsZSBub25jZQ=="));
    CHECK(Ws_HeadLength((const u8 *)"a\r\n\r\nrest", 9) == 5);
    CHECK(Ws_HeadLength((const u8 *)"a\r\n\r", 4) == 0);
}

static void TestFrames(void) {
    /* RFC 6455 section 5.7's examples */
    u8 hello[] = {0x81, 0x05, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    u8 masked[] = {0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58};
    u8 first[] = {0x01, 0x03, 0x48, 0x65, 0x6C};
    u8 ping[] = {0x89, 0x05, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    u8 bad[16];
    WsFrame f;
    static u8 big[70000], built[70100], parsed[70100];
    static const u8 mask[4] = {1, 2, 3, 4};
    u32 sizes[] = {0, 1, 125, 126, 127, 65535, 65536, 69999};
    u32 i;

    CHECK(Ws_ParseFrame(hello, sizeof(hello), 0, 100, &f) == 7);
    CHECK(f.fin && f.opcode == WS_OP_TEXT && f.size == 5 && SDL_memcmp(f.payload, "Hello", 5) == 0);
    CHECK(Ws_ParseFrame(masked, sizeof(masked), 1, 100, &f) == 11);
    CHECK(f.size == 5 && SDL_memcmp(f.payload, "Hello", 5) == 0);
    CHECK(Ws_ParseFrame(first, sizeof(first), 0, 100, &f) == 5 && !f.fin);
    CHECK(Ws_ParseFrame(ping, sizeof(ping), 0, 100, &f) == 7 && f.opcode == WS_OP_PING);

    /* a client must mask and a server must not */
    SDL_memcpy(bad, hello, sizeof(hello));
    CHECK(Ws_ParseFrame(bad, sizeof(hello), 1, 100, &f) == -1);
    /* every incomplete prefix asks for more instead of guessing */
    for (i = 0; i < sizeof(masked); i++) {
        u8 again[sizeof(masked)] = {0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58};
        CHECK(Ws_ParseFrame(again, i, 1, 100, &f) == 0);
    }
    /* reserved bits, unknown opcode, long control, split control */
    bad[0] = 0xC2; bad[1] = 0;
    CHECK(Ws_ParseFrame(bad, 2, 0, 100, &f) == -1);
    bad[0] = 0x83; bad[1] = 0;
    CHECK(Ws_ParseFrame(bad, 2, 0, 100, &f) == -1);
    bad[0] = 0x89; bad[1] = 126; bad[2] = 0; bad[3] = 200;
    CHECK(Ws_ParseFrame(bad, 4, 0, 1000, &f) == -1);
    bad[0] = 0x09; bad[1] = 0;
    CHECK(Ws_ParseFrame(bad, 2, 0, 100, &f) == -1);
    /* lengths over the limit, including ones that would wrap 32-bit sums */
    bad[0] = 0x82; bad[1] = 126; bad[2] = 0xFF; bad[3] = 0xFF;
    CHECK(Ws_ParseFrame(bad, 4, 0, 100, &f) == -1);
    SDL_memset(bad, 0xFF, sizeof(bad));
    bad[0] = 0x82; bad[1] = 127;
    CHECK(Ws_ParseFrame(bad, 10, 0, 100, &f) == -1);
    bad[2] = 0; bad[3] = 0; bad[4] = 0; bad[5] = 0;
    CHECK(Ws_ParseFrame(bad, 10, 0, 100, &f) == -1);

    /* what's built parses back, at the sizes where the header changes */
    for (i = 0; i < sizeof(big); i++) {
        big[i] = (u8)(i * 31 + (i >> 8));
    }
    for (i = 0; i < SDL_arraysize(sizes); i++) {
        u32 n = Ws_BuildFrame(built, WS_OP_BINARY, big, sizes[i], NULL);

        CHECK(Ws_ParseFrame(built, n, 0, sizeof(big), &f) == (s32)n);
        CHECK(f.size == sizes[i] && SDL_memcmp(f.payload, big, sizes[i]) == 0);
        n = Ws_BuildFrame(built, WS_OP_BINARY, big, sizes[i], mask);
        CHECK(Ws_ParseFrame(built, n, 1, sizeof(big), &f) == (s32)n);
        CHECK(f.size == sizes[i] && SDL_memcmp(f.payload, big, sizes[i]) == 0);
        CHECK(Ws_ParseFrame(built, n - 1 + (sizes[i] == 0), 1, sizeof(big), &f) == (sizes[i] == 0 ? (s32)n : 0));
    }
    (void)parsed;
}

/* Random input must never read out of bounds (run under HWVX_ASAN) nor claim
 * more bytes than it was given. */
static void TestFuzz(void) {
    u32 x = 0x12345678;
    u32 round;

    for (round = 0; round < 2000000; round++) {
        u8 buf[32];
        u32 len, i;
        WsFrame f;
        s32 used;

        for (i = 0; i < sizeof(buf); i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            buf[i] = (u8)x;
        }
        len = x % (sizeof(buf) + 1);
        used = Ws_ParseFrame(buf, len, (int)(round & 1), 16, &f);
        CHECK(used >= -1 && used <= (s32)len);
        if (used > 0) {
            CHECK(f.size <= 16 && f.payload >= buf && f.payload + f.size <= buf + len);
        }
    }
    for (round = 0; round < 200000; round++) {
        char head[64];
        char key[WS_KEY_MAX];
        u32 i;

        for (i = 0; i + 1 < sizeof(head); i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            /* mostly the characters the parser cares about */
            head[i] = "\r\n: GET/hwvxUpgrade\t"[x % 20];
        }
        head[sizeof(head) - 1] = 0;
        Ws_ParseRequest(head, key);
        Ws_CheckResponse(head, "x");
        Ws_HeadLength((const u8 *)head, sizeof(head) - 1);
    }
}

static void TestMessages(void) {
    u8 buf[16];
    NetWriter w;
    NetReader r;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, 0xAB);
    NetWriter_U16(&w, 0x1234);
    NetWriter_U32(&w, 0xDEADBEEF);
    NetWriter_F32(&w, 1.5f);
    CHECK(w.ok && w.size == 11);
    /* little-endian on every host */
    CHECK(buf[1] == 0x34 && buf[2] == 0x12 && buf[3] == 0xEF && buf[6] == 0xDE);
    NetWriter_Bytes(&w, "12345678", 8);
    CHECK(!w.ok && w.size == 11);
    NetWriter_U8(&w, 1);
    CHECK(w.size == 11);

    NetReader_Init(&r, buf, 11);
    CHECK(NetReader_U8(&r) == 0xAB && NetReader_U16(&r) == 0x1234 && NetReader_U32(&r) == 0xDEADBEEF);
    CHECK(NetReader_F32(&r) == 1.5f && r.ok && r.pos == 11);
    CHECK(NetReader_U8(&r) == 0 && !r.ok);
    NetReader_Init(&r, buf, 3);
    CHECK(NetReader_Take(&r, 0xFFFFFFFFu) == NULL && !r.ok);
    CHECK(NetReader_U32(&r) == 0);
}

static void TestSim(void) {
    NetSim sim;
    u64 prev = 0, now;
    u32 stalls = 0;

    CHECK(NetSim_Parse(&sim, NULL) && !sim.enabled);
    CHECK(NetSim_Parse(&sim, "") && !sim.enabled);
    CHECK(!NetSim_Parse(&sim, "nonsense") && !sim.enabled);
    CHECK(!NetSim_Parse(&sim, "lat=") && !sim.enabled);
    CHECK(!NetSim_Parse(&sim, "lat=10x") && !sim.enabled);
    CHECK(!NetSim_Parse(&sim, "lat=-1") && !sim.enabled);
    CHECK(!NetSim_Parse(&sim, "loss=101") && !sim.enabled);
    CHECK(!NetSim_Parse(&sim, "what=1") && !sim.enabled);
    CHECK(NetSim_Parse(&sim, "lat=40,jitter=15,loss=1.5,stall=300,bw=64,seed=7"));
    CHECK(sim.enabled && sim.latencyMs == 40 && sim.jitterMs == 15 && sim.lossPer10k == 150);
    CHECK(sim.stallMs == 300 && sim.bandwidth == 64 * 1024);
    for (now = 0; now < 100000; now++) {
        u64 due = NetSim_Due(&sim, now, prev);

        CHECK(due >= prev);         /* order holds */
        CHECK(due >= now + 40);
        if (due >= prev + 300) {
            stalls++;
        }
        prev = due;
    }
    /* about 1.5% of them */
    CHECK(stalls > 1000 && stalls < 2000);
}

/* ---- host and client ------------------------------------------------------- */

#define MAX_SIZE 20000
#define MARKER_HELLO 0xFFFFFFFFu

static u32 SizeOf(u32 seq) {
    /* mostly small, some large enough to take several reads */
    u32 mix = seq * 2654435761u;
    return 8 + (seq % 16 == 15 ? mix % MAX_SIZE : mix % 200);
}

static u8 ByteOf(u32 seq, u32 i) {
    return (u8)((seq * 131 + i * 7) ^ (i >> 8));
}

static u32 BuildReliable(u8 *out, u32 seq) {
    u32 size = SizeOf(seq);
    NetWriter w;
    u32 i;

    NetWriter_Init(&w, out, MAX_SIZE + 8);
    NetWriter_U32(&w, seq);
    NetWriter_U32(&w, size);
    for (i = 8; i < size; i++) {
        NetWriter_U8(&w, ByteOf(seq, i));
    }
    return w.size;
}

static int CheckReliable(const NetEvent *e, u32 seq) {
    NetReader r;
    u32 i;

    NetReader_Init(&r, e->data, e->size);
    if (NetReader_U32(&r) != seq || NetReader_U32(&r) != e->size || !r.ok || e->size != SizeOf(seq)) {
        return 0;
    }
    for (i = 8; i < e->size; i++) {
        if (e->data[i] != ByteOf(seq, i)) {
            return 0;
        }
    }
    return 1;
}

static void Pause(void) {
    SDL_Delay(1);
}

static int RunHost(u16 port, const char *room, u32 expected, u32 seconds, u32 maxPlayers) {
    u64 end = SDL_GetTicks() + seconds * 1000ull;
    u32 joined = 0, present = 0, messages = 0, latest = 0;
    NetError err = Net_Host(port, room, 0, maxPlayers);

    if (err != NET_OK) {
        printf("host: %s\n", Net_ErrorName(err));
        return 1;
    }
    while (SDL_GetTicks() < end) {
        NetEvent e;

        while (Net_Poll(&e)) {
            switch (e.type) {
            case NET_EV_PEER_JOINED:
                joined++;
                present++;
                printf("host: peer %u joined\n", e.peer);
                break;
            case NET_EV_PEER_LEFT:
                present--;
                printf("host: peer %u left: %s\n", e.peer, Net_ErrorName(e.error));
                break;
            case NET_EV_RELIABLE:
                messages++;
                if (!Net_SendReliable(e.peer, e.data, e.size)) {
                    printf("host: could not echo to peer %u\n", e.peer);
                }
                break;
            case NET_EV_LATEST:
                latest++;
                Net_SendLatest(e.peer, e.key, e.data, e.size);
                break;
            default:
                break;
            }
        }
        if (joined >= expected && present == 0) {
            printf("host: done, %u joined, %u reliable, %u latest echoed\n", joined, messages, latest);
            Net_Close();
            return 0;
        }
        Pause();
    }
    printf("host: timed out, %u joined, %u still here\n", joined, present);
    Net_Close();
    return 1;
}

static int RunClient(const char *addr, const char *room, u32 count, u32 seconds) {
    u64 start = SDL_GetTicks();
    u64 end = start + seconds * 1000ull;
    u64 nextLatest = start;
    static u8 out[MAX_SIZE + 8];
    u32 sent = 0, received = 0, latestSent = 0, latestSeen = 0, latestGot = 0;
    u32 lastLatest = 0, errors = 0, maxRtt = 0;
    int connected = 0;
    NetError err = Net_Connect(addr, room);
    NetStats stats;
    u8 self = 0;

    SDL_zero(stats);
    if (err != NET_OK) {
        printf("client: %s\n", Net_ErrorName(err));
        return 1;
    }
    while (SDL_GetTicks() < end) {
        NetEvent e;
        u64 now = SDL_GetTicks();

        while (Net_Poll(&e)) {
            switch (e.type) {
            case NET_EV_CONNECTED:
                connected = 1;
                self = e.peer;
                printf("client %u: connected in %u ms\n", self, (u32)(now - start));
                break;
            case NET_EV_RELIABLE:
                if (!CheckReliable(&e, received)) {
                    printf("client %u: reliable %u damaged, or out of order\n", self, received);
                    errors++;
                }
                received++;
                break;
            case NET_EV_LATEST: {
                NetReader r;
                u32 v;

                NetReader_Init(&r, e.data, e.size);
                v = NetReader_U32(&r);
                if (!r.ok || e.key != 3 || (latestGot != 0 && v <= lastLatest)) {
                    printf("client %u: latest went from %u to %u\n", self, lastLatest, v);
                    errors++;
                }
                lastLatest = v;
                latestGot++;
                break;
            }
            case NET_EV_DISCONNECTED:
                printf("client %u: disconnected: %s\n", self, Net_ErrorName(e.error));
                Net_Close();
                return 1;
            default:
                break;
            }
        }
        if (connected) {
            NetStats st;

            if (Net_GetStats(NET_PEER_HOST, &st)) {
                stats = st;
                if (st.rttMs > maxRtt) {
                    maxRtt = st.rttMs;
                }
            }
            /* not faster than the echoes come back, or a slow link overflows
             * the queue, which is a different test */
            if (sent < count && st.queuedBytes < (1u << 20)) {
                if (Net_SendReliable(NET_PEER_HOST, out, BuildReliable(out, sent))) {
                    sent++;
                }
            }
            if (now >= nextLatest && latestSent < count * 4) {
                u8 buf[64];
                NetWriter w;

                SDL_zeroa(buf);
                NetWriter_Init(&w, buf, sizeof(buf));
                NetWriter_U32(&w, ++latestSeen);
                Net_SendLatest(NET_PEER_HOST, 3, buf, sizeof(buf));
                latestSent++;
                nextLatest = now + 33;
            }
            /* one latest round trip at least, then stop */
            if (sent == count && received == count && (latestGot > 0 || now >= end)) {
                break;
            }
        }
        Pause();
    }
    printf("client %u: reliable %u/%u in %u ms, latest %u of %u echoed, rtt last %u max %u ms, %u stale, %u errors\n",
           self, received, count, (u32)(SDL_GetTicks() - start), latestGot, latestSent, stats.rttMs, maxRtt,
           stats.staleDropped, errors);
    Net_Close();
    /* a bandwidth limit backs the queue up: latest messages must have given way */
    {
        const char *sim = SDL_getenv("HWVX_NET_SIM");

        if (sim != NULL && SDL_strstr(sim, "bw=") != NULL && stats.staleDropped == 0) {
            printf("client %u: nothing was replaced under a bandwidth limit\n", self);
            errors++;
        }
    }
    return !(connected && received == count && errors == 0 && latestGot > 0);
}

static int RunExpectError(const char *addr, const char *room, int expected) {
    u64 end = SDL_GetTicks() + 45000;
    NetError err = Net_Connect(addr, room);

    if (err != NET_OK) {
        printf("rejected client: %s (wanted %s)\n", Net_ErrorName(err), Net_ErrorName(expected));
        return (int)err != expected;
    }
    while (SDL_GetTicks() < end) {
        NetEvent e;

        while (Net_Poll(&e)) {
            if (e.type == NET_EV_DISCONNECTED) {
                printf("rejected client: %s (wanted %s)\n", Net_ErrorName(e.error), Net_ErrorName(expected));
                Net_Close();
                return (int)e.error != expected;
            }
            if (e.type == NET_EV_CONNECTED) {
                printf("rejected client: was let in\n");
                Net_Close();
                return 1;
            }
        }
        Pause();
    }
    Net_Close();
    printf("rejected client: nothing happened\n");
    return 1;
}

/* ---- e2e ------------------------------------------------------------------- */

static const char *s_self;

static SDL_Process *Spawn(const char *sim, const char *a, const char *b, const char *c, const char *d, const char *e, const char *f) {
    const char *args[] = {s_self, a, b, c, d, e, f, NULL};
    SDL_Environment *env = SDL_CreateEnvironment(true);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process *p;

    if (sim != NULL) {
        SDL_SetEnvironmentVariable(env, "HWVX_NET_SIM", sim, true);
    } else {
        SDL_UnsetEnvironmentVariable(env, "HWVX_NET_SIM");
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (p == NULL) {
        printf("could not run %s: %s\n", s_self, SDL_GetError());
        s_failures++;
    }
    return p;
}

static void Wait(SDL_Process *p, const char *what) {
    int code = -1;

    if (p != NULL) {
        SDL_WaitProcess(p, true, &code);
        SDL_DestroyProcess(p);
        if (code != 0) {
            printf("FAILED: %s exited with %d\n", what, code);
            s_failures++;
        }
    }
}

/* A host and clients, all with the same simulated connection. */
static void Session(const char *name, u16 port, const char *sim, u32 clients, const char *count, const char *seconds) {
    char portText[8], addr[32], peers[8], maxPlayers[8];
    SDL_Process *host, *c[NET_MAX_PLAYERS];
    u32 i;
    u64 start = SDL_GetTicks();

    printf("--- %s\n", name);
    fflush(stdout);
    SDL_snprintf(portText, sizeof(portText), "%u", port);
    SDL_snprintf(addr, sizeof(addr), "127.0.0.1:%u", port);
    SDL_snprintf(peers, sizeof(peers), "%u", clients);
    SDL_snprintf(maxPlayers, sizeof(maxPlayers), "%u", clients + 1);
    /* clients first, on purpose: they must wait for a host still coming up */
    for (i = 0; i < clients; i++) {
        c[i] = Spawn(sim, "client", addr, "ROOM", count, seconds, NULL);
    }
    SDL_Delay(300);
    host = Spawn(sim, "host", portText, "ROOM", peers, seconds, maxPlayers);
    for (i = 0; i < clients; i++) {
        Wait(c[i], "client");
    }
    Wait(host, "host");
    printf("    %.1f s\n", (SDL_GetTicks() - start) / 1000.0);
}

/* A session of maxPlayers: a wrong code is refused, and so is one player too many. */
static void Rejections(u16 port, u32 maxPlayers) {
    char portText[8], addr[32], err[8], peers[8], maxText[8];
    SDL_Process *host, *in[NET_MAX_PLAYERS - 1];
    u32 i;

    printf("--- wrong room code and a full room of %u\n", maxPlayers);
    fflush(stdout);
    SDL_snprintf(peers, sizeof(peers), "%u", maxPlayers - 1);
    SDL_snprintf(maxText, sizeof(maxText), "%u", maxPlayers);
    SDL_snprintf(portText, sizeof(portText), "%u", port);
    SDL_snprintf(addr, sizeof(addr), "127.0.0.1:%u", port);
    host = Spawn(NULL, "host", portText, "ROOM", peers, "60", maxText);
    SDL_Delay(300);
    SDL_snprintf(err, sizeof(err), "%d", NET_ERR_REJECT_ROOM_CODE);
    Wait(Spawn(NULL, "expect-error", addr, "WRONG", err, "0", NULL), "client with the wrong code");
    /* 4000 reliable messages keep them in the room for a while */
    for (i = 0; i < maxPlayers - 1; i++) {
        in[i] = Spawn(NULL, "client", addr, "ROOM", "4000", "60", NULL);
    }
    SDL_Delay(500);
    SDL_snprintf(err, sizeof(err), "%d", NET_ERR_REJECT_FULL);
    Wait(Spawn(NULL, "expect-error", addr, "ROOM", err, "0", NULL), "one player too many");
    for (i = 0; i < maxPlayers - 1; i++) {
        Wait(in[i], "client in the room");
    }
    Wait(host, "host");
}

static int RunE2E(void) {
    /* nothing listens on this port: a client must give up, not hang */
    char err[8];

    Session("clean connection, 3 clients", 47901, NULL, 3, "2000", "60");
    Session("bad connection: lag, jitter, stalls", 47902, "lat=40,jitter=20,loss=1,stall=200,seed=7", 3, "300", "90");
    Session("slow connection: 64 KB/s, latest must give way", 47903, "bw=64,seed=9", 1, "150", "90");
    Session("full transport capacity: 8 players", 47906, NULL, NET_MAX_PLAYERS - 1, "500", "60");
    Rejections(47904, NET_DEFAULT_PLAYERS);
    Rejections(47907, NET_MAX_PLAYERS);
    printf("--- nothing to connect to\n");
    fflush(stdout);
    SDL_snprintf(err, sizeof(err), "%d", NET_ERR_UNREACHABLE);
    Wait(Spawn(NULL, "expect-error", "127.0.0.1:47905", "ROOM", err, "0", NULL), "client with no host");
    return s_failures != 0;
}

int main(int argc, char **argv) {
    int result = 2;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!SDL_Init(0)) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    s_self = argv[0];
    if (argc == 2 && SDL_strcmp(argv[1], "selftest") == 0) {
        TestHandshake();
        TestFrames();
        TestFuzz();
        TestMessages();
        TestSim();
        printf(s_failures == 0 ? "selftest passed\n" : "selftest: %d failures\n", s_failures);
        result = s_failures != 0;
    } else if (argc == 2 && SDL_strcmp(argv[1], "e2e") == 0) {
        result = RunE2E();
        printf(result == 0 ? "e2e passed\n" : "e2e: %d failures\n", s_failures);
    } else if (argc >= 6 && SDL_strcmp(argv[1], "host") == 0) {
        result = RunHost((u16)atoi(argv[2]), argv[3], (u32)atoi(argv[4]), (u32)atoi(argv[5]),
                         argc >= 7 ? (u32)atoi(argv[6]) : NET_DEFAULT_PLAYERS);
    } else if (argc >= 6 && SDL_strcmp(argv[1], "client") == 0) {
        result = RunClient(argv[2], argv[3], (u32)atoi(argv[4]), (u32)atoi(argv[5]));
    } else if (argc >= 5 && SDL_strcmp(argv[1], "expect-error") == 0) {
        result = RunExpectError(argv[2], argv[3], atoi(argv[4]));
    } else {
        printf("usage: see the top of tools/nettest.c\n");
    }
    SDL_Quit();
    return result;
}
