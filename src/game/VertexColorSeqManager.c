#include "common.h"

/* A world light (in the world's lights array): its colour track. */
typedef struct VertexColorLight {
    u8 pad0[0x4];               /* 0x00 */
    PTR32(void) keys;           /* 0x04 colour KeyTrack (ExtractColorKey) */
    u8 pad8[0x8];               /* 0x08 */
} VertexColorLight;

/* A world light's playback slot (g_VertexColorTiles). */
typedef struct VertexColorTile {
    PTR32(VertexColorLight) light;  /* 0x00 the world light it plays */
    KeyColorState color;        /* 0x04 ExtractColorKey playback state */
} VertexColorTile;
_Static_assert(offsetof(VertexColorTile, color) == 0x04, "VertexColorTile.color");
_Static_assert(sizeof(VertexColorTile) == 0x34, "VertexColorTile size");

VertexColorTile g_VertexColorTiles[64];
void *g_pVertexColorSeqList[64];
s32 g_iTotalVertexColorTiles;
s32 g_iTotalVertexColorSeq;


/* A vertex colour sequence of the world's geometry (in a quadrant pack). */
typedef struct VertexColorSeq {
    u8 pad00[8];                /* 0x00 */
    u32 enabled;                /* 0x08 */
} VertexColorSeq;

/* data: { mode (0 off, 1 on, 2 toggle), count, sequence handles... } */
s32 ActionVertexColorSeqChangeMode(s32 *data) {
    u32 count = data[1];
    u32 i;

    for (i = 0; i < count; i++) {
        VertexColorSeq *seq = (VertexColorSeq *)PTR(data[i + 2]);
        switch (data[0]) {
        case 0:
            seq->enabled = 0;
            break;
        case 1:
            seq->enabled = 1;
            break;
        case 2:
            if (seq->enabled == 0) {
                seq->enabled = 1;
            } else {
                seq->enabled = 0;
            }
            break;
        }
    }
    return 1;
}

void VertexColorSeqManager_Create(void) {

}

void VertexColorSeqManager_Destroy(void) {
    extern s32 g_iTotalVertexColorSeq;
    extern s32 g_iTotalVertexColorTiles;
    g_iTotalVertexColorSeq = 0;
    g_iTotalVertexColorTiles = 0;
}

void VertexColorSeqManager_Init(void) {
    typedef struct VCSZone {
        u8 pad0[0x14];          /* 0x00 */
        PTR32(void) vertices;   /* 0x14 vertex colour data */
        u8 pad18[0x4];          /* 0x18 */
        u32 flag1C;             /* 0x1C nonzero: not the default zone */
        u8 pad20[0x30];         /* 0x20 */
    } VCSZone;
    typedef struct VCSWorld {
        u8 pad0[0x20];          /* 0x00 */
        u32 numZones;           /* 0x20 */
        PTR32(VCSZone) zones;   /* 0x24 */
        u8 pad28[0x38];         /* 0x28 */
        u32 numLights;          /* 0x60 */
        PTR32(VertexColorLight) lights;  /* 0x64 */
    } VCSWorld;
    extern VertexColorTile g_VertexColorTiles[64];
    extern s32 g_iTotalVertexColorSeq;
    extern s32 g_iTotalVertexColorTiles;
    VCSWorld *world;
    u32 i;

    g_iTotalVertexColorSeq = 0;
    g_iTotalVertexColorTiles = 0;
    world = GETP(g_worldData.world);
    if (world != NULL) {
        for (i = 0; i < world->numLights; i++) {
            SETP(g_VertexColorTiles[i].light, &GETP(world->lights)[i]);
            g_VertexColorTiles[i].color.reset = 1;
            g_iTotalVertexColorTiles++;
        }
        for (i = 0; i < ((VCSWorld *)GETP(g_worldData.world))->numZones; i++) {
            if (GETP(((VCSWorld *)GETP(g_worldData.world))->zones)[i].flag1C == 0) {
                if (GETP(GETP(((VCSWorld *)GETP(g_worldData.world))->zones)[i].vertices) != NULL) {
                    VertexColorSeqManager_AddVertices(GETP(GETP(((VCSWorld *)GETP(g_worldData.world))->zones)[i].vertices));
                }
                break;
            }
        }
    }
}

void VertexColorSeqManager_Heartbeat(void) {
    extern VertexColorTile g_VertexColorTiles[64];
    extern s32 g_iTotalVertexColorTiles;
    extern f32 g_fDeltaTimeMS;
    f32 dt;
    u32 i;

    dt = g_fDeltaTimeMS;
    for (i = 0; i < g_iTotalVertexColorTiles; i++) {
        ExtractColorKey(GETP(GETP(g_VertexColorTiles[i].light)->keys), &g_VertexColorTiles[i].color, dt);
    }
}

void VertexColorSeqManager_AddVertices(void *vertices) {
    extern s32 g_iTotalVertexColorSeq;
    extern void *g_pVertexColorSeqList[64];
    if (vertices != NULL) {
        g_pVertexColorSeqList[g_iTotalVertexColorSeq++] = vertices;
    }
}

void VertexColorSeqManager_RemoveVertices(void *vertices) {
    extern s32 g_iTotalVertexColorSeq;
    extern void *g_pVertexColorSeqList[64];
    u32 i;

    for (i = 0; i < g_iTotalVertexColorSeq; i++) {
        if (g_pVertexColorSeqList[i] == vertices) {
            g_iTotalVertexColorSeq--;
            g_pVertexColorSeqList[i] = g_pVertexColorSeqList[g_iTotalVertexColorSeq];
            return;
        }
    }
}

s32 VertexColorSeqManager_GetLightIndex(void *light) {
    extern VertexColorTile g_VertexColorTiles[64];
    extern s32 g_iTotalVertexColorTiles;
    u32 i;

    for (i = 0; i < g_iTotalVertexColorTiles; i++) {
        if (light == GETP(g_VertexColorTiles[i].light)) {
            return i;
        }
    }
    return -1;
}

void VertexColorSeqManager_GetLightColor(u32 index, u32 *color) {
    extern VertexColorTile g_VertexColorTiles[64];
    extern s32 g_iTotalVertexColorTiles;

    if (index < g_iTotalVertexColorTiles) {
        *color = *(u32 *)&g_VertexColorTiles[index].color.color;
    }
}

