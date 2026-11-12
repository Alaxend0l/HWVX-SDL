/*
 * netbot.c - a headless player for testing online play: it joins a game's
 * session (src/game/Port_Online.c) over the transport and plays along without
 * the game, data or a window, so a test of a full session needs one real game
 * on the machine, and bots for the other seats (tools/online8_test.sh).
 *
 *   netbot ADDRESS [options]
 *     --vehicle N    the car it picks (default: one per port)
 *     --drive        report its car moving in a small circle where it stands,
 *                    from where a test host (HWVX_ONLINE_TEST) says it is
 *                    (MSG_TEST_CARS); without it, it sends no snapshots and
 *                    its car stays where it spawned
 *     --points N     with --drive: the stunt points it reports, per second
 *     --finish S     claim its car's finish S seconds after each go
 *     --mods TEXT    say it has these mods (MSG_HELLO), to test the refusal
 *     --seconds N    leave after this long (default 600)
 *
 * It does what a game's client does, from the messages alone: says which mods
 * it has (MSG_HELLO), picks a car when the host sets up (MSG_CARS), says it's
 * ready for every stage the host may hold (MSG_READY), goes back to the lobby
 * when the host does (MSG_LEFT_LEVEL).
 *
 * Checks, each a "netbot N: ERROR" line and exit status 1: the host's scores
 * for its port in each MSG_ROUND_END are the points it reported (--points);
 * with --drive, every other car's snapshots reach it within a level. The last
 * line is a summary: stages, gos, round ends, the ports whose cars it heard of.
 * Exit status 0: in the session until the end, no errors; 3: refused for mods.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "net.h"
#include "Port_OnlineProtocol.h"

#define SNAPSHOT_MS 33
#define DRIVE_RADIUS 4.0f
#define DRIVE_PERIOD_S 6.0f

typedef struct Car {
    int have;
    f32 pos[3], quat[4];
} Car;

static int s_port;              /* ours, 1 .. ONLINE_MAX_PLAYERS */
static int s_inLevel;
static Car s_own;               /* where the host says our car is */
static u32 s_stage, s_goStage;
static u64 s_goAt;              /* ticks of the last go, 0: held */
static int s_finishSent;
static u32 s_pointsPerSecond, s_finishAfter;
static u32 s_stages, s_gos, s_roundEnds, s_errors;
static u32 s_seen, s_seenLevel; /* ports whose snapshots came: ever, this level */
static int s_drive;
static const char *s_mods;

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

static void SendHello(void) {
    u8 buf[16 + MODS_SUMMARY_MAX];
    const char *text = s_mods != NULL ? s_mods : "no mods";
    unsigned long long hash = s_mods != NULL ? Online_Fnv1a(ONLINE_FNV_BASIS, s_mods) : 0;
    u32 len = (u32)SDL_min(SDL_strlen(text), (size_t)MODS_SUMMARY_MAX);
    NetWriter w;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_HELLO);
    NetWriter_U8(&w, s_mods != NULL ? 1 : 0);
    NetWriter_U32(&w, (u32)hash);
    NetWriter_U32(&w, (u32)(hash >> 32));
    NetWriter_U8(&w, (u8)len);
    NetWriter_Bytes(&w, text, len);
    Send(&w);
}

/* The points it has reported in the stage so far. */
static u32 Points(u64 now) {
    return s_goAt != 0 ? (u32)((now - s_goAt) / 1000 * s_pointsPerSecond) : 0;
}

/* 1: leave, refused. */
static int HandleReliable(const NetEvent *e, s32 vehicle) {
    NetReader r;
    u8 kind, buf[32];
    NetWriter w;
    u64 now = SDL_GetTicks();

    NetReader_Init(&r, e->data, e->size);
    kind = NetReader_U8(&r);
    switch (kind) {
    case MSG_HELLO: {
        u32 count = NetReader_U8(&r);
        u32 lo = NetReader_U32(&r), hi = NetReader_U32(&r);
        unsigned long long mine = s_mods != NULL ? Online_Fnv1a(ONLINE_FNV_BASIS, s_mods) : 0;

        if (r.ok && (count != (s_mods != NULL ? 1u : 0u) || ((unsigned long long)hi << 32 | lo) != mine)) {
            printf("netbot %d: the host's mods differ: leaving\n", s_port);
            return 1;
        }
        break;
    }
    case MSG_SETUP:
        NetWriter_Init(&w, buf, sizeof(buf));
        NetWriter_U8(&w, MSG_CARS);
        NetWriter_U32(&w, (u32)vehicle);    /* Port_ContentWriteChoice: a base car, no id */
        NetWriter_U8(&w, 0);
        NetWriter_U32(&w, (u32)-1);         /* no item */
        NetWriter_U8(&w, 0);
        Send(&w);
        printf("netbot %d: picked car %d\n", s_port, (int)vehicle);
        break;
    case MSG_START:
        s_inLevel = 1;
        s_own.have = 0;
        s_stage = 1;
        s_goAt = 0;
        s_seenLevel = 0;
        SendReady(1);   /* the level's first stage */
        printf("netbot %d: level starting\n", s_port);
        break;
    case MSG_GO: {
        u32 stage = NetReader_U32(&r);

        if (r.ok && stage > s_goStage) {
            s_goStage = stage;
            s_stage = stage;
            s_goAt = now;
            s_finishSent = 0;
            s_gos++;
            /* ready for the next one already: a round reset holds until the host says go */
            SendReady(stage + 1);
            printf("netbot %d: go, stage %u\n", s_port, stage);
        }
        break;
    }
    case MSG_TEST_CARS: {
        u32 stage = NetReader_U32(&r);
        u32 n = NetReader_U8(&r), i;

        for (i = 0; i < n && r.ok; i++) {
            u32 port = NetReader_U8(&r);
            f32 v[7];
            int k;

            for (k = 0; k < 7; k++) {
                v[k] = NetReader_F32(&r);
            }
            if (r.ok && (int)port == s_port) {
                s_own.have = 1;
                s_own.pos[0] = v[0];
                s_own.pos[1] = v[1];
                s_own.pos[2] = v[2];
                for (k = 0; k < 4; k++) {
                    s_own.quat[k] = v[3 + k];
                }
                printf("netbot %d: stage %u, my car is at %.0f %.0f %.0f (%u cars)\n", s_port, stage, v[0], v[1], v[2], n);
            }
        }
        break;
    }
    case MSG_ROUND_END: {
        u32 stage = NetReader_U32(&r);
        u32 winner = NetReader_U8(&r);
        s32 kills[ONLINE_MAX_PLAYERS];
        u32 points[ONLINE_MAX_PLAYERS], i, mine = Points(now);
        char line[256];

        line[0] = 0;
        for (i = 0; i < ONLINE_MAX_PLAYERS; i++) {
            char one[32];

            kills[i] = (s32)NetReader_U32(&r);
            points[i] = NetReader_U32(&r);
            SDL_snprintf(one, sizeof(one), " %u:%d/%u", i + 1, (int)kills[i], points[i]);
            SDL_strlcat(line, one, sizeof(line));
        }
        if (!r.ok) {
            printf("netbot %d: ERROR: a short round end\n", s_port);
            s_errors++;
            break;
        }
        s_roundEnds++;
        printf("netbot %d: round over, stage %u, winner %u, kills/points%s\n", s_port, stage, winner, line);
        /* the host has what this machine said: its last snapshot's, a moment old */
        if (s_drive && s_pointsPerSecond != 0 && s_own.have &&
            (points[s_port - 1] > mine || mine - points[s_port - 1] > 2 * s_pointsPerSecond)) {
            printf("netbot %d: ERROR: the host has %u points for this car, it has %u\n", s_port, points[s_port - 1], mine);
            s_errors++;
        }
        break;
    }
    case MSG_WINNER: {
        /* a race's end: the host decided who finished first */
        u32 stage = NetReader_U32(&r);
        u32 number = NetReader_U8(&r);

        if (r.ok) {
            s_roundEnds++;
            printf("netbot %d: stage %u won by %u\n", s_port, stage, number);
        }
        break;
    }
    case MSG_RETURN:
        if (s_inLevel) {
            u8 msg = MSG_LEFT_LEVEL;

            s_inLevel = 0;
            s_stages += s_stage;
            Net_SendReliable(NET_PEER_HOST, &msg, 1);
            printf("netbot %d: back in the lobby\n", s_port);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void SendSnapshot(u32 seq, u64 now) {
    u8 buf[SNAPSHOT_SIZE];
    f32 t = (f32)(now - s_goAt) / 1000.0f;
    f32 w = 6.2831853f / DRIVE_PERIOD_S, phase = (f32)s_port;
    NetWriter writer;
    int i;

    NetWriter_Init(&writer, buf, sizeof(buf));
    NetWriter_U32(&writer, seq);
    NetWriter_F32(&writer, s_own.pos[0] + DRIVE_RADIUS * (cosf(w * t + phase) - cosf(phase)));
    NetWriter_F32(&writer, s_own.pos[1]);
    NetWriter_F32(&writer, s_own.pos[2] + DRIVE_RADIUS * (sinf(w * t + phase) - sinf(phase)));
    NetWriter_F32(&writer, -DRIVE_RADIUS * w * sinf(w * t + phase));
    NetWriter_F32(&writer, 0.0f);
    NetWriter_F32(&writer, DRIVE_RADIUS * w * cosf(w * t + phase));
    for (i = 0; i < 4; i++) NetWriter_F32(&writer, s_own.quat[i]);
    for (i = 0; i < 3; i++) NetWriter_F32(&writer, 0.0f);   /* angular momentum */
    NetWriter_U32(&writer, 1000);  /* health: what's left is the game's to say */
    NetWriter_U32(&writer, 0);     /* buttons */
    NetWriter_U8(&writer, 0);
    NetWriter_U8(&writer, 0);
    NetWriter_U8(&writer, 0);
    NetWriter_U8(&writer, 0);
    NetWriter_U8(&writer, 0);      /* fire */
    NetWriter_U8(&writer, 0);      /* slot */
    NetWriter_U16(&writer, 0);     /* ammo */
    NetWriter_U32(&writer, Points(now));
    if (writer.ok) {
        Net_SendLatest(NET_PEER_HOST, (u8)(s_port - 1), buf, writer.size);
    }
}

static void SendFinish(u64 now) {
    u8 buf[12];
    NetWriter w;

    NetWriter_Init(&w, buf, sizeof(buf));
    NetWriter_U8(&w, MSG_FINISH);
    NetWriter_U32(&w, s_stage);
    NetWriter_U32(&w, (u32)(now - s_goAt));
    Send(&w);
    printf("netbot %d: claims the finish, stage %u\n", s_port, s_stage);
}

int main(int argc, char **argv) {
    const char *address = NULL;
    s32 vehicle = -1;
    int connected = 0, refused = 0, i;
    u32 seconds = 600, seq = 0;
    u64 end, nextSnapshot = 0;
    NetError err;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--vehicle") == 0 && i + 1 < argc) {
            vehicle = atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--drive") == 0 || SDL_strcmp(argv[i], "--follow") == 0) {
            s_drive = 1;
        } else if (SDL_strcmp(argv[i], "--points") == 0 && i + 1 < argc) {
            s_pointsPerSecond = (u32)atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--finish") == 0 && i + 1 < argc) {
            s_finishAfter = (u32)atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--mods") == 0 && i + 1 < argc) {
            s_mods = argv[++i];
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
    while (SDL_GetTicks() < end && !refused) {
        NetEvent e;
        u64 now;

        while (!refused && Net_Poll(&e)) {
            switch (e.type) {
            case NET_EV_CONNECTED:
                connected = 1;
                s_port = e.peer + 1;
                if (vehicle < 0) {
                    vehicle = (s_port * 5) % 20;
                }
                printf("netbot: connected as port %d\n", s_port);
                SendHello();
                break;
            case NET_EV_RELIABLE:
                if (e.peer == NET_PEER_HOST) {
                    refused = HandleReliable(&e, vehicle);
                }
                break;
            case NET_EV_LATEST:
                if (e.key < ONLINE_MAX_PLAYERS) {
                    s_seen |= 1u << e.key;
                    s_seenLevel |= 1u << e.key;
                }
                break;
            case NET_EV_DISCONNECTED:
                printf("netbot %d: ERROR: %s\n", s_port, Net_ErrorName(e.error));
                Net_Close();
                SDL_Quit();
                return 1;
            default:
                break;
            }
        }
        now = SDL_GetTicks();
        if (s_inLevel && s_goAt != 0 && s_drive && s_own.have && now >= nextSnapshot) {
            nextSnapshot = now + SNAPSHOT_MS;
            SendSnapshot(++seq, now);
        }
        if (s_inLevel && s_goAt != 0 && s_finishAfter != 0 && !s_finishSent && now - s_goAt >= s_finishAfter * 1000ull) {
            s_finishSent = 1;
            SendFinish(now);
        }
        SDL_Delay(5);
    }
    printf("netbot %d: summary: stages %u, gos %u, round ends %u, cars heard of %02X, errors %u%s\n", s_port,
           s_stages + (s_inLevel ? s_stage : 0), s_gos, s_roundEnds, s_seen | (1u << (s_port - 1)), s_errors, refused ? ", refused" : "");
    Net_Close();
    SDL_Quit();
    return refused ? 3 : !connected || s_errors != 0 ? 1 : 0;
}
