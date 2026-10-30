#include "common.h"

s32 g_coronaRayTest;
u32 g_totalCoronas;          /* compared unsigned: cmplwi r0, 0x40 */

/* A corona's description (from the level data). */
typedef struct CoronaData {
    u8 type;            /* 0x00 */
    u8 rotate;          /* 0x01 */
    u16 fadeTime;       /* 0x02 */
    u8 distCheck;       /* 0x04 */
    u8 pad05[0x03];     /* 0x05 */
    f32 maxDist;        /* 0x08 */
    f32 size;           /* 0x0C */
    s32 spin;           /* 0x10 */
    u32 texture;        /* 0x14 */
    u8 colorTrack[8];   /* 0x18 */
    PTR32(void) lens;         /* 0x20 */
} CoronaData;

/* An emitter's corona: its colour track and the interpolation state. */
typedef struct Corona {
    PTR32(CoronaData) track;  /* 0x00 */
    u8 pad04[0x0C];     /* 0x04 */
    Vector4 pos;        /* 0x10 */
    f32 color[4];       /* 0x20 */
    u8 pad30[0x14];     /* 0x30 */
    GXColor rgba;       /* 0x44 */
    u8 pad48[0x04];     /* 0x48 */
    u8 active;          /* 0x4C */
    u8 pad4D[0x03];     /* 0x4D */
    s32 flare;          /* 0x50 */
    u32 time;           /* 0x54 */
    u8 playing;         /* 0x58 */
    u8 pad59[7];        /* 0x59 */
} Corona;

Corona g_aCorona[64];
extern u32 g_iDeltaTimeMS;

void CoronaHolder_Init(void) {
    g_coronaRayTest = 0;
    g_totalCoronas = 0;
}

s32 CoronaHolder_Add(CoronaData *corona, Vector4 *pos)
{
    if (g_totalCoronas >= 64) {
        return -1;
    }
    if (corona == NULL) {
        return -1;
    }
    SETP(g_aCorona[g_totalCoronas].track, corona);
    {
        Corona *c = &g_aCorona[g_totalCoronas];
        c->pos = *pos;
        c->flare = -1;
        c->playing = 0;
        c->time = 0;
        c->active = 1;
        if (GETP(corona->lens) != NULL) {
            c->flare = LensFlare_Add(GETP(corona->lens), 0, pos);
        }
    }
    return g_totalCoronas++;
}

void CoronaHolder_Render(void) {
    Vector4 camera __attribute__((aligned(16)));
    Vector4 screen __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 len;
    f32 dist;
    GXColor color;
    Corona *c;
    u32 i;
    f32 rot;

    Gfx_GetCameraPosition(&camera);
    if (++g_coronaRayTest > g_totalCoronas) {
        g_coronaRayTest = 0;
    }
    for (i = 0; i < g_totalCoronas; i++) {
        c = &g_aCorona[i];
        Gfx_TransformVector(&c->pos, &screen);
        if (screen.z <= 0.0f) {
            c->time = 0;
            continue;
        }
        if (screen.x < -50.0f || screen.y < -50.0f || screen.x > 690.0f || screen.y > 530.0f) {
            c->time = 0;
            continue;
        }
        if (GETP(c->track)->distCheck && GameSettings_GetNumOfPlayers() == 1) {
            DistanceXYZ(&c->pos, &camera, &dist);
            if (dist > GETP(c->track)->maxDist) {
                continue;
            }
        }
        if (i == g_coronaRayTest) {
            PSVECSubtract(&c->pos, &camera, &dir);
            len = 1.0f;
            if (RayHitsTerrainAndObjects(&camera, &dir, &len, NULL)) {
                c->playing = 0;
            } else {
                c->playing = 1;
            }
        }
        if (c->time && c->playing) {
            rot = 0.0f;
            if (GETP(c->track)->rotate) {
                rot = (screen.x - 320.0f) * (0.017453292f * (0.25f * -(f32)GETP(c->track)->spin));
            }
            color = c->rgba;
            Gfx_Add2DSprite(&screen, GETP(c->track)->texture, color, GETP(c->track)->size, GETP(c->track)->type, rot);
        }
    }
}

void CoronaHolder_Heartbeat(void) {
    Corona *corona;
    u32 i;
    u32 delta;

    delta = g_iDeltaTimeMS;
    for (i = 0; i < g_totalCoronas; i++) {
        corona = &g_aCorona[i];
        if (corona->playing) {
            if (corona->time < GETP(corona->track)->fadeTime) {
                corona->active = 1;
                corona->time += delta;
                ExtractColorKey(GETP(corona->track)->colorTrack, corona->color, corona->time);
            }
        } else if (corona->time != 0) {
            corona->active = 1;
            if (delta < corona->time) {
                corona->time -= delta;
                ExtractColorKey(GETP(corona->track)->colorTrack, corona->color, corona->time);
            } else {
                corona->time = 0;
            }
        }
    }
}

