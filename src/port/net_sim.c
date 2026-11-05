/* Bad connections on demand: HWVX_NET_SIM holds comma-separated settings, for
 * example
 *   HWVX_NET_SIM=lat=40,jitter=15,loss=1,stall=200,bw=64,seed=7
 *
 * lat     ms added to each frame received (so per direction: 40 on both ends
 *         is 80 ms more round trip)
 * jitter  up to this many ms more, random per frame
 * loss    percentage of frames (decimals allowed) that behave like a lost TCP
 *         packet: the retransmit holds it, and everything behind it, for stall
 * stall   how long a loss holds things up, in ms; default 200
 * bw      KB/s this end may send; the queue backs up behind it, which is how
 *         latest messages get to be replaced
 * seed    repeat the same random choices
 *
 * Everything works on whole frames at the socket thread. Frames are never
 * dropped, since TCP doesn't drop them either. */
#include <SDL3/SDL.h>

#include "net_internal.h"

static u32 NextRandom(NetSim *sim) {
    u32 x = sim->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sim->rng = x;
    return x;
}

int NetSim_Parse(NetSim *sim, const char *text) {
    NetSim s;
    const char *p = text;

    SDL_zerop(sim);
    if (text == NULL || *text == 0) {
        return 1;
    }
    SDL_zero(s);
    s.enabled = 1;
    s.stallMs = 200;
    s.rng = (u32)SDL_GetPerformanceCounter() | 1;
    while (*p != 0) {
        char *end;
        const char *eq = SDL_strchr(p, '=');
        size_t name;
        double value;

        if (eq == NULL) {
            return 0;
        }
        name = (size_t)(eq - p);
        value = SDL_strtod(eq + 1, &end);
        if (end == eq + 1 || (*end != 0 && *end != ',') || value < 0.0 || value > 1000000.0) {
            return 0;
        }
        if (name == 3 && SDL_strncmp(p, "lat", 3) == 0) {
            s.latencyMs = (u32)value;
        } else if (name == 6 && SDL_strncmp(p, "jitter", 6) == 0) {
            s.jitterMs = (u32)value;
        } else if (name == 4 && SDL_strncmp(p, "loss", 4) == 0) {
            if (value > 100.0) {
                return 0;
            }
            s.lossPer10k = (u32)(value * 100.0 + 0.5);
        } else if (name == 5 && SDL_strncmp(p, "stall", 5) == 0) {
            s.stallMs = (u32)value;
        } else if (name == 2 && SDL_strncmp(p, "bw", 2) == 0) {
            s.bandwidth = (u32)(value * 1024.0);
        } else if (name == 4 && SDL_strncmp(p, "seed", 4) == 0) {
            s.rng = (u32)value * 2654435761u | 1;
        } else {
            return 0;
        }
        p = *end == ',' ? end + 1 : end;
    }
    *sim = s;
    return 1;
}

u64 NetSim_Due(NetSim *sim, u64 now, u64 prevDue) {
    u64 due = now + sim->latencyMs;

    if (sim->jitterMs != 0) {
        due += NextRandom(sim) % (sim->jitterMs + 1);
    }
    if (due < prevDue) {
        due = prevDue;
    }
    if (sim->lossPer10k != 0 && NextRandom(sim) % 10000 < sim->lossPer10k) {
        due += sim->stallMs;
    }
    return due;
}
