/*
 * netbot.c - a headless player for testing online play: it joins a game's
 * session (src/game/Port_Online.c) over the transport and plays along without
 * the game, data or a window, so a test of a full session needs only one or
 * two real games on the machine, and bots for the other seats.
 *
 *   netbot ADDRESS [options]
 *     --vehicle N    the car it picks (default: one per port)
 *     --follow       in a level, drive beside the host's car: its snapshots
 *                    are the host's, moved aside, so every car is in view
 *     --points N     with --follow: the stunt points it reports, per second
 *     --seconds N    leave after this long (default 600)
 *
 * It does what a game's client does, from the messages alone: picks a car
 * when the host sets up (MSG_CARS), says it's ready for every stage the host
 * may hold (MSG_READY), goes back to the lobby when the host does
 * (MSG_LEFT_LEVEL). Without --follow it sends no snapshots: its car stays
 * where it spawned, and as damage counts on the owner's machine, it can't be
 * destroyed. Exit status 0: it was in the session until the end.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "net.h"
#include "Port_OnlineProtocol.h"

#define SNAPSHOT_MS 33
#define FOLLOW_RADIUS 22.0f

typedef struct HostCar {
    int have;
    f32 pos[3], vel[3], quat[4];
} HostCar;

static int s_port;              /* ours, 1 .. ONLINE_MAX_PLAYERS */
static int s_inLevel;
static HostCar s_host;

static void Send(const NetWriter *w) {
    if (w->ok) {
        Net_SendReliable(NET_PEER_HOST, w->data, w->size);
    }
}

static void SendReady(u32 stage) {
    u8 buf[8];
    NetWriter w;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_READY);
    NetWriter_U32(&w, stage);
    Send(&w);
}

static void HandleReliable(const NetEvent *e, s32 vehicle) {
    NetReader r;
    u8 kind, buf[16];
    NetWriter w;

    NetReader_Init(&r, e->data, e->size);
    kind = NetReader_U8(&r);
    switch (kind) {
    case MSG_SETUP:
        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_CARS);
        NetWriter_U32(&w, (u32)vehicle);
        NetWriter_U32(&w, (u32)-1);
        Send(&w);
        printf("netbot %d: picked car %d\n", s_port, (int)vehicle);
        break;
    case MSG_START:
        s_inLevel = 1;
        s_host.have = 0;
        SendReady(1);   /* the level's first stage */
        printf("netbot %d: level starting\n", s_port);
        break;
    case MSG_GO: {
        u32 stage = NetReader_U32(&r);

        /* ready for the next one already: a round reset holds until the host says go */
        if (r.ok) {
            SendReady(stage + 1);
            printf("netbot %d: go, stage %u\n", s_port, stage);
        }
        break;
    }
    case MSG_ROUND_END:
        printf("netbot %d: round over\n", s_port);
        break;
    case MSG_RETURN:
        if (s_inLevel) {
            u8 msg = MSG_LEFT_LEVEL;

            s_inLevel = 0;
            Net_SendReliable(NET_PEER_HOST, &msg, 1);
            printf("netbot %d: back in the lobby\n", s_port);
        }
        break;
    default:
        break;
    }
}

static void ReadHostCar(const NetEvent *e) {
    NetReader r;
    int i;

    NetReader_Init(&r, e->data, e->size);
    NetReader_U32(&r);
    for (i = 0; i < 3; i++) s_host.pos[i] = NetReader_F32(&r);
    for (i = 0; i < 3; i++) s_host.vel[i] = NetReader_F32(&r);
    for (i = 0; i < 4; i++) s_host.quat[i] = NetReader_F32(&r);
    s_host.have = r.ok;
}

static void SendSnapshot(u32 seq, u32 points) {
    u8 buf[SNAPSHOT_SIZE];
    f32 angle = (f32)(s_port - 1) * (6.2831853f / ONLINE_MAX_PLAYERS);
    NetWriter w;
    int i;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U32(&w, seq);
    NetWriter_F32(&w, s_host.pos[0] + FOLLOW_RADIUS * cosf(angle));
    NetWriter_F32(&w, s_host.pos[1]);
    NetWriter_F32(&w, s_host.pos[2] + FOLLOW_RADIUS * sinf(angle));
    for (i = 0; i < 3; i++) NetWriter_F32(&w, s_host.vel[i]);
    for (i = 0; i < 4; i++) NetWriter_F32(&w, s_host.quat[i]);
    for (i = 0; i < 3; i++) NetWriter_F32(&w, 0.0f);   /* angular momentum */
    NetWriter_U32(&w, 1000);    /* health: what's left is the game's to say */
    NetWriter_U32(&w, 0);       /* buttons */
    NetWriter_U8(&w, 0);
    NetWriter_U8(&w, 0);
    NetWriter_U8(&w, 0);
    NetWriter_U8(&w, 0);
    NetWriter_U8(&w, 0);        /* fire */
    NetWriter_U8(&w, 0);        /* slot */
    NetWriter_U16(&w, 0);       /* ammo */
    NetWriter_U32(&w, points);
    if (w.ok) {
        Net_SendLatest(NET_PEER_HOST, (u8)(s_port - 1), buf, w.size);
    }
}

int main(int argc, char **argv) {
    const char *address = NULL;
    s32 vehicle = -1;
    int follow = 0, connected = 0, i;
    u32 pointsPerSecond = 0, seconds = 600, seq = 0;
    u64 end, nextSnapshot = 0, levelSince = 0;
    NetError err;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--vehicle") == 0 && i + 1 < argc) {
            vehicle = atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--follow") == 0) {
            follow = 1;
        } else if (SDL_strcmp(argv[i], "--points") == 0 && i + 1 < argc) {
            pointsPerSecond = (u32)atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = (u32)atoi(argv[++i]);
        } else if (address == NULL && argv[i][0] != '-') {
            address = argv[i];
        } else {
            address = NULL;
            break;
        }
    }
    if (address == NULL) {
        printf("usage: see the top of tools/netbot.c\n");
        return 2;
    }
    if (!SDL_Init(0)) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    err = Net_Connect(address, NULL);
    if (err != NET_OK) {
        printf("netbot: %s\n", Net_ErrorName(err));
        return 1;
    }
    end = SDL_GetTicks() + seconds * 1000ull;
    while (SDL_GetTicks() < end) {
        NetEvent e;
        u64 now;

        while (Net_Poll(&e)) {
            switch (e.type) {
            case NET_EV_CONNECTED:
                connected = 1;
                s_port = e.peer + 1;
                if (vehicle < 0) {
                    vehicle = (s_port * 5) % 20;
                }
                printf("netbot: connected as port %d\n", s_port);
                break;
            case NET_EV_RELIABLE:
                if (e.peer == NET_PEER_HOST) {
                    HandleReliable(&e, vehicle);
                    if (s_inLevel && levelSince == 0) {
                        levelSince = SDL_GetTicks();
                    } else if (!s_inLevel) {
                        levelSince = 0;
                    }
                }
                break;
            case NET_EV_LATEST:
                if (e.key == 0) {
                    ReadHostCar(&e);
                }
                break;
            case NET_EV_DISCONNECTED:
                printf("netbot %d: %s\n", s_port, Net_ErrorName(e.error));
                Net_Close();
                SDL_Quit();
                return 1;
            default:
                break;
            }
        }
        now = SDL_GetTicks();
        if (s_inLevel && s_host.have && follow && now >= nextSnapshot) {
            nextSnapshot = now + SNAPSHOT_MS;
            SendSnapshot(++seq, (u32)((now - levelSince) / 1000 * pointsPerSecond));
        }
        SDL_Delay(5);
    }
    Net_Close();
    SDL_Quit();
    return connected ? 0 : 1;
}
