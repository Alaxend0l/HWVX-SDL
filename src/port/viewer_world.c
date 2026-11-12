/* Viewer mode: a whole world (docs/viewer-protocol.md, "Worlds").
 *
 * `world` loads a level as the game does (LoadWorldProcess, the progression
 * manager's loader: world pack, world textures, collision, share pack, items,
 * the tile cars' packs, then World_Init and World_Refresh for the envmap, fog
 * and far plane) and runs the game's own world processes, as StateGamePlay_Init
 * adds them: World (World_Render streams the quadrant packs around the camera
 * and draws them, the hub and the static instances; FileStreaming_HeartBeatItems
 * spawns the pickups), ThingManager (destructibles, emitters, items, movers),
 * particles, coronas, lights. The viewer's camera is the game's camera, so
 * the quadrants load and unload as the camera moves. The tile cars are drawn
 * standing at their tiles (not spawned: no AI, no physics).
 *
 * Overlays are drawn on top of the frame with the GX line primitive: quadrant
 * regions (boxes of the BSPN0 regions, sampled once; coloured by load state),
 * the collision pack's crease edges, volumes, spawns (tiles, items,
 * teleporters, lights), nav paths, and hooks for other layers
 * (ViewerWorld_AddOverlayHook: trigger volumes once their schemas are in). */
#include "port_internal.h"
#include "gx_render.h"
#include "viewer_internal.h"
#include "gx.h"
#include "World.h"
#include "Nav.h"
#include "Collision.h"
#include "CarA.h"
#include "Port_SpawnBox.h"

enum {
    OV_REGIONS = 1 << 0,
    OV_LOADED = 1 << 1,
    OV_COLLISION = 1 << 2,
    OV_VOLUMES = 1 << 3,
    OV_SPAWNS = 1 << 4,
    OV_PATHS = 1 << 5,
    OV_OBJECTS = 1 << 6,
};

static const struct {
    const char *name;
    u32 bit;
} s_overlayNames[] = {
    { "regions", OV_REGIONS },   { "loaded", OV_LOADED }, { "collision", OV_COLLISION }, { "volumes", OV_VOLUMES },
    { "spawns", OV_SPAWNS },     { "paths", OV_PATHS },   { "objects", OV_OBJECTS },
};

/* The progressions' modes, as Port_Start.c names them. */
static const char *const s_modeNames[] = { "adventure", "challenge", "drag", "battle", "joyride", "mpdrag", "mpchallenge",
                                           "mpbattle" };

#define MAX_HOOKS 8

static struct {
    int active;
    char path[128];             /* the level's world, as the MCP names it */
    char name[64];              /* its last part */
    VPack *vp;                  /* the world pack, as a viewer pack (its outline) */
    u32 overlays;
    f32 (*regionBox)[6];        /* per quadrant: min xyz, max xyz (min > max: none) */
    u32 numRegionBoxes;
    f32 loadMs;
    u32 maxLoaded;
    struct {
        char name[32];
        void (*draw)(void);
        int on;
    } hooks[MAX_HOOKS];
    u32 numHooks;
} w;

int ViewerWorld_Active(void) {
    return w.active && WORLD() != NULL;
}

void ViewerWorld_AddOverlayHook(const char *name, void (*draw)(void)) {
    if (w.numHooks < MAX_HOOKS) {
        SDL_strlcpy(w.hooks[w.numHooks].name, name, sizeof(w.hooks[0].name));
        w.hooks[w.numHooks].draw = draw;
        w.numHooks++;
    }
}

/* ---- levels ---------------------------------------------------------------- */

static const char *LastPart(const char *p) {
    const char *c, *name = p;

    for (c = p; p != NULL && *c != 0; c++) {
        if (*c == '/' || *c == '\\') {
            name = c + 1;
        }
    }
    return name;
}

static const char *ModeName(u32 p, char *buf, size_t size) {
    if (p < SDL_arraysize(s_modeNames)) {
        return s_modeNames[p];
    }
    SDL_snprintf(buf, size, "progression%u", p);
    return buf;
}

/* Each level of each progression script: fn(progression, index, world path). */
static void EachLevel(void (*fn)(void *ctx, u32 p, u32 i, const char *path), void *ctx) {
    MCP *mcp = GameSettings_GetMCP();
    u32 p;
    s32 i;

    for (p = 0; mcp != NULL && p < mcp->numScripts; p++) {
        ProgScript *ps = GETP(GETP(mcp->scripts)[p]);

        for (i = 0; ps != NULL && i < ps->count; i++) {
            const char *path = GETP(GETP(ps->levels)[i].world);

            if (path != NULL && *path != 0) {
                fn(ctx, p, (u32)i, path);
            }
        }
    }
}

static void WriteLevel(void *ctx, u32 p, u32 i, const char *path) {
    JsonWriter *jw = ctx;
    char buf[24];

    Jw_BeginObject(jw, NULL, 1);
    Jw_String(jw, "mode", ModeName(p, buf, sizeof(buf)));
    Jw_Int(jw, "progression", p);
    Jw_Int(jw, "index", i);
    Jw_String(jw, "name", LastPart(path));
    Jw_String(jw, "world", path);
    Jw_EndObject(jw);
}

typedef struct {
    const char *want, *mode;
    char found[128];
} FindCtx;

static void FindLevel(void *ctx, u32 p, u32 i, const char *path) {
    FindCtx *f = ctx;
    char buf[24];

    (void)i;
    if (f->found[0] != 0 || (f->mode != NULL && SDL_strcasecmp(f->mode, ModeName(p, buf, sizeof(buf))) != 0)) {
        return;
    }
    if (SDL_strcasecmp(f->want, path) == 0 || SDL_strcasecmp(f->want, LastPart(path)) == 0) {
        SDL_strlcpy(f->found, path, sizeof(f->found));
    }
}

/* ---- loading ----------------------------------------------------------------- */

/* Each quadrant region's box: points of a grid over the collision bounds
 * that the region holds (BSPN0_CollideWithVector), grown by half a step. */
static void RegionBoxes(void) {
    World *wd = WORLD();
    WorldCollision *col = GETP(wd->collision);
    const int nx = 64, ny = 24, nz = 64;
    f32 step[3], lo[3], hi[3];
    u32 q;
    int x, y, z, k;

    SDL_free(w.regionBox);
    w.numRegionBoxes = wd->numQuads;
    w.regionBox = SDL_calloc(wd->numQuads ? wd->numQuads : 1, sizeof(*w.regionBox));
    if (w.regionBox == NULL || col == NULL) {
        w.numRegionBoxes = 0;
        return;
    }
    lo[0] = col->min.x - 50.0f, lo[1] = col->min.y - 50.0f, lo[2] = col->min.z - 50.0f;
    hi[0] = col->max.x + 50.0f, hi[1] = col->max.y + 150.0f, hi[2] = col->max.z + 50.0f;
    step[0] = (hi[0] - lo[0]) / nx, step[1] = (hi[1] - lo[1]) / ny, step[2] = (hi[2] - lo[2]) / nz;
    for (q = 0; q < wd->numQuads; q++) {
        WorldQuad *quad = &GETP(wd->quads)[q];
        f32 *b = w.regionBox[q];

        b[0] = b[1] = b[2] = 1e30f;
        b[3] = b[4] = b[5] = -1e30f;
        if (GETP(quad->bsp) == NULL) {
            continue;
        }
        for (y = 0; y <= ny; y++) {
            for (z = 0; z <= nz; z++) {
                for (x = 0; x <= nx; x++) {
                    Vector4 p __attribute__((aligned(16)));

                    p.x = lo[0] + x * step[0];
                    p.y = lo[1] + y * step[1];
                    p.z = lo[2] + z * step[2];
                    p.w = 1.0f;
                    if (BSPN0_CollideWithVector(GETP(quad->bsp), &p)) {
                        for (k = 0; k < 3; k++) {
                            f32 v = (&p.x)[k];

                            if (v - 0.5f * step[k] < b[k]) {
                                b[k] = v - 0.5f * step[k];
                            }
                            if (v + 0.5f * step[k] > b[3 + k]) {
                                b[3 + k] = v + 0.5f * step[k];
                            }
                        }
                    }
                }
            }
        }
    }
}

/* The game's world processes, as StateGamePlay_Init adds them (not the
 * players, input, interface, rules, triggers or sound). */
static void AddProcesses(void) {
    extern s32 (*Heartbeat_Process)(void);
    World *wd = WORLD();
    u32 i;

    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, 0, 0);
    ProcessManager_Add("ThingManager", ThingManager_DARYL_Init, ThingManager_DARYL_Heartbeat,
                       (s32)HND(ThingManager_DARYL_Render), 1);
    ProcessManager_Add("World", NULL, FileStreaming_HeartBeatItems, (s32)HND(World_Render), 0);
    ProcessManager_Add("Particle", ParticleManager_Init, ParticleManager_Heartbeat, (s32)HND(ParticleManager_Render), 0);
    ProcessManager_Add("Trail", Trail_Init, Trail_Heartbeat, (s32)HND(Trail_Render), 0);
    ProcessManager_Add("Light", LightHolder_Init, LightHolder_Heartbeat, (s32)HND(LightHolder_Render), 0);
    ProcessManager_Add("Flash", FlashHolder_Init, FlashHolder_Heartbeat, (s32)HND(FlashHolder_Render), 0);
    ProcessManager_Add("Corona", CoronaHolder_Init, CoronaHolder_Heartbeat, (s32)HND(CoronaHolder_Render), 0);
    ProcessManager_Add("Shader", NULL, ShaderHolder_Heartbeat, 0, 0);
    ProcessManager_Add("VertexColorSeq", NULL, VertexColorSeqManager_Heartbeat, 0, 0);
    ProcessManager_Add("Timers", NULL, TimerManager_Heartbeat, 0, 0);
    VertexColorSeqManager_Init();
    VolumeManager_Initialize();
    LightManager_Initialize();
    Grid_Init(&GETP(wd->collision)->min, &GETP(wd->collision)->max, 0x10, 0x10);
    NavHolder_Init(GETP(wd->nav));
    LightHolder_SetTexture(GETP(wd->gameData)->lightTexture);
    for (i = 0; i < wd->numTileLights; i++) {
        TileLight *light = World_GetTileLight(i);

        LightHolder_Add(GETP(light->desc), &light->pos, 0);
    }
    (void)Heartbeat_Process;
}

void ViewerWorld_Unload(void) {
    if (!w.active) {
        return;
    }
    ViewerLevel_WorldGone();
    while (IsLoadingThreadRunning()) {
        FileStreaming_HeartBeat();
    }
    if (w.vp != NULL) {
        Viewer_DropPack(w.vp);
        w.vp = NULL;
    }
    NavHolder_Release();
    CameraManager_ReleaseAllEmitters();
    ThingManager_DARYL_Release();
    MatrixCache_Init();
    Grid_Release();
    SongManager_StopBKSong();
    World_Unload();
    Viewer_ForgetShares();      /* World_Unload released the share packs too */
    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, 0, 0);
    ParticleManager_Init();
    SDL_free(w.regionBox);
    w.regionBox = NULL;
    w.numRegionBoxes = 0;
    w.active = 0;
    Port_Log("viewer: world %s unloaded", w.name);
}

static int Load(const char *path, char *err, size_t errSize) {
    static char s_path[128];
    u64 t0 = SDL_GetTicksNS();
    u32 steps = 0, i;
    s32 state = 0;
    World *wd;

    Viewer_UnloadAll();
    ViewerWorld_Unload();
    SDL_strlcpy(s_path, path, sizeof(s_path));   /* the loader keeps the name's pointer */
    /* a human tile car loads the car player 1 picked (code variable 0xCB); none: car 0 */
    for (i = 1; i <= 4; i++) {
        if ((s32)GameSettings_GetCodeVariable(0xCB, i) == -1) {
            GameSettings_SetCodeVariable(0xCB, i, 0);
        }
    }
    do {
        state = LoadWorldProcess(s_path, 0);
        FileStreaming_HeartBeat();
    } while (state != 7 && state != 8 && ++steps < 0x100000);
    if (state != 7 || (wd = WORLD()) == NULL) {
        SDL_snprintf(err, errSize, "the world %s did not load (state %d; see the game's log)", path, (int)state);
        return 0;
    }
    DataPack_CloseFiles();
    w.active = 1;
    SDL_strlcpy(w.path, path, sizeof(w.path));
    SDL_strlcpy(w.name, LastPart(path), sizeof(w.name));
    AddProcesses();
    g_worldData.resetQuads = 1;
    RegionBoxes();
    w.vp = Viewer_AdoptPack(path, 4, 0, (DataPack *)PTR(g_worldData.worldPack));
    w.loadMs = (f32)((SDL_GetTicksNS() - t0) / 1000000.0);
    w.maxLoaded = 0;
    Port_Log("viewer: world %s loaded in %.0f ms (%u quadrants)", path, w.loadMs, (unsigned)wd->numQuads);
    return 1;
}

/* ---- per frame --------------------------------------------------------------- */

/* The tile cars standing at their tiles. */
void ViewerWorld_Draw(void) {
    World *wd = WORLD();
    u32 i;

    if (!ViewerWorld_Active()) {
        return;
    }
    for (i = 0; i < wd->numTileCars; i++) {
        TileCar *car = &GETP(wd->tileCars)[i];
        Matrix4 m __attribute__((aligned(16)));

        if (GETP(car->info) == NULL) {
            continue;
        }
        SetOrientation(&m, car->rotY, car->rotX, car->rotZ);
        m.v[3] = car->pos;
        m.v[3].w = 1.0f;
        Viewer_DrawCar(GETP(car->info), &m);
    }
}

/* ---- overlays ------------------------------------------------------------------ */

static void TileForward(const TileCar *c, f32 *fwd);

typedef struct {
    f32 p[6];
    u8 c[3];
} Line;

static Line *s_lines;
static u32 s_numLines, s_capLines;

static void AddLine(const f32 *a, const f32 *b, u8 r, u8 g, u8 bl) {
    if (s_numLines == s_capLines) {
        u32 cap = s_capLines ? s_capLines * 2 : 4096;
        Line *n = SDL_realloc(s_lines, cap * sizeof(Line));

        if (n == NULL) {
            return;
        }
        s_lines = n;
        s_capLines = cap;
    }
    SDL_memcpy(s_lines[s_numLines].p, a, 3 * sizeof(f32));
    SDL_memcpy(s_lines[s_numLines].p + 3, b, 3 * sizeof(f32));
    s_lines[s_numLines].c[0] = r;
    s_lines[s_numLines].c[1] = g;
    s_lines[s_numLines].c[2] = bl;
    s_numLines++;
}

void ViewerWorld_Line(const f32 *a, const f32 *b, u8 r, u8 g, u8 bl) {
    AddLine(a, b, r, g, bl);
}

static void Box(const f32 *b, u8 r, u8 g, u8 bl) {
    static const int e[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 },
                                  { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    f32 c[8][3];
    int i;

    for (i = 0; i < 8; i++) {
        c[i][0] = b[(i & 1) ? 3 : 0];
        c[i][1] = b[(i & 4) ? 4 : 1];
        c[i][2] = b[(i & 2) ? 5 : 2];
    }
    for (i = 0; i < 12; i++) {
        AddLine(c[e[i][0]], c[e[i][1]], r, g, bl);
    }
}

static void Cross(const Vector4 *p, f32 s, u8 r, u8 g, u8 bl) {
    f32 a[3], b[3];
    int k;

    for (k = 0; k < 3; k++) {
        a[0] = p->x, a[1] = p->y, a[2] = p->z;
        b[0] = p->x, b[1] = p->y, b[2] = p->z;
        a[k] -= s;
        b[k] += s;
        AddLine(a, b, r, g, bl);
    }
}

static void Circle(const Vector4 *c, f32 rad, int axis, u8 r, u8 g, u8 bl) {
    f32 a[3], b[3];
    int i;

    for (i = 0; i < 24; i++) {
        f32 t0 = i * 6.2831853f / 24, t1 = (i + 1) * 6.2831853f / 24;
        int u = axis == 0 ? 1 : 0, v = axis == 2 ? 1 : 2;

        a[0] = c->x, a[1] = c->y, a[2] = c->z;
        b[0] = c->x, b[1] = c->y, b[2] = c->z;
        a[u] += rad * (f32)cos(t0), a[v] += rad * (f32)sin(t0);
        b[u] += rad * (f32)cos(t1), b[v] += rad * (f32)sin(t1);
        AddLine(a, b, r, g, bl);
    }
}

/* An arrow at p pointing along the horizontal direction (s, c), for spawns. */
static void Arrow(const Vector4 *p, f32 s, f32 c, f32 len, u8 r, u8 g, u8 bl) {
    f32 base[3] = { p->x, p->y + 1.0f, p->z }, tip[3], l[3], rr[3];

    tip[0] = base[0] + s * len, tip[1] = base[1], tip[2] = base[2] + c * len;
    l[0] = tip[0] - s * len * 0.3f + c * len * 0.2f, l[1] = tip[1], l[2] = tip[2] - c * len * 0.3f - s * len * 0.2f;
    rr[0] = tip[0] - s * len * 0.3f - c * len * 0.2f, rr[1] = tip[1], rr[2] = tip[2] - c * len * 0.3f + s * len * 0.2f;
    AddLine(base, tip, r, g, bl);
    AddLine(tip, l, r, g, bl);
    AddLine(tip, rr, r, g, bl);
    Circle(p, len * 0.25f, 1, r, g, bl);
}

/* The quadrant the camera is in (as World_Render would find it first), or -1. */
static s32 CameraQuad(void) {
    World *wd = WORLD();
    Vector4 pos __attribute__((aligned(16)));
    u32 q;

    Gfx_GetCameraPosition(&pos);
    pos.w = 1.0f;
    for (q = 0; q < wd->numQuads; q++) {
        WorldQuad *quad = &GETP(wd->quads)[q];

        if (GETP(quad->bsp) != NULL && BSPN0_CollideWithVector(GETP(quad->bsp), &pos)) {
            return (s32)q;
        }
    }
    return -1;
}

static void Collect(void) {
    World *wd = WORLD();
    u32 i, q;

    s_numLines = 0;
    if (w.overlays & (OV_REGIONS | OV_LOADED)) {
        s32 cur = CameraQuad();

        for (q = 0; q < w.numRegionBoxes; q++) {
            WorldQuad *quad = &GETP(wd->quads)[q];
            const f32 *b = w.regionBox[q];

            if (b[0] > b[3]) {
                continue;
            }
            if ((s32)q == cur) {
                Box(b, 255, 230, 40);
            } else if ((w.overlays & OV_LOADED) && quad->loaded) {
                Box(b, quad->visible ? 60 : 40, quad->visible ? 230 : 120, quad->visible ? 90 : 230);
            } else if (w.overlays & OV_REGIONS) {
                Box(b, 150, 150, 160);
            }
        }
    }
    if ((w.overlays & OV_COLLISION) && GETP(wd->collision) != NULL) {
        WorldCollision *col = GETP(wd->collision);

        for (i = 0; col->mode == 1 && i < col->numEdges; i++) {
            TerrainEdge *e = &GETP(col->edges)[i];

            AddLine(&e->a.x, &e->b.x, 230, 80, 220);
        }
        for (i = 0; col->mode == 0 && i < col->numPolys; i++) {
            /* bat2_l9: poly lists; their boxes would do, but the edges are what's useful */
        }
    }
    if (w.overlays & OV_VOLUMES) {
        for (i = 0; i < wd->numVolumes; i++) {
            WorldVolume *v = &GETP(wd->volumes)[i];
            u8 r = v->type == VOLUME_FOG ? 120 : 60, g = v->type == VOLUME_REVERB ? 200 : 160, b = 255;

            if (v->shape == 1) {
                Circle(&v->center, v->radius, 0, r, g, b);
                Circle(&v->center, v->radius, 1, r, g, b);
                Circle(&v->center, v->radius, 2, r, g, b);
            } else if (GETP(v->bsp) != NULL) {
                Cross(&v->center, 2.0f, r, g, b);
            }
        }
    }
    if (w.overlays & OV_SPAWNS) {
        for (i = 0; i < wd->numTileCars; i++) {
            TileCar *c = &GETP(wd->tileCars)[i];
            f32 f[3], n;

            TileForward(c, f);
            n = (f32)sqrt(f[0] * f[0] + f[2] * f[2]);
            n = n > 1e-4f ? n : 1.0f;
            Arrow(&c->pos, f[0] / n, f[2] / n, 8.0f, c->port >= 1 && c->port <= 4 ? 60 : 255, c->port >= 1 && c->port <= 4 ? 255 : 120,
                  60);
            {
                /* the start as the largest car's box (Port_SpawnBox.h), facing as it does */
                f32 p[3] = { c->pos.x, c->pos.y, c->pos.z };
                u8 col[3] = { c->port >= 1 && c->port <= 4 ? 60 : 255, c->port >= 1 && c->port <= 4 ? 255 : 120, 60 };

                ViewerLevel_SpawnBox(p, c->rotY, c->rotX, c->rotZ, col);
            }
        }
        for (i = 0; i < wd->numTileCreatures; i++) {
            TileCreature *c = &GETP(wd->tileCreatures)[i];

            Arrow(&c->pos, (f32)sin(c->angle), (f32)cos(c->angle), 5.0f, 255, 160, 40);
        }
        for (i = 0; i < wd->numTeleporters; i++) {
            Teleporter *t = &GETP(wd->teleporters)[i];

            Circle(&t->mtx.v[3], t->radius > 0.0f ? t->radius : 4.0f, 1, 40, 255, 255);
        }
        for (i = 0; i < wd->numTileLights; i++) {
            Cross(&GETP(wd->tileLights)[i].pos, 1.5f, 255, 255, 140);
        }
        for (q = 0; q < wd->numQuads; q++) {
            QuadContents *qc = GETP(GETP(wd->quads)[q].contents);

            for (i = 0; qc != NULL && i < qc->numItems; i++) {
                Cross(&GETP(qc->items)[i].pos, 2.0f, 80, 255, 120);
            }
        }
    }
    if (w.overlays & OV_OBJECTS) {
        for (q = 0; q < wd->numQuads; q++) {
            QuadContents *qc = GETP(GETP(wd->quads)[q].contents);

            for (i = 0; qc != NULL && i < qc->numInstances; i++) {
                QuadInstance *in = &GETP(qc->instances)[i];

                Cross(&in->pos, in->radius > 0.5f ? in->radius * 0.5f : 1.0f, 200, 200, 255);
            }
            for (i = 0; qc != NULL && i < qc->numFxPoints; i++) {
                Cross(&GETP(qc->fxPoints)[i].pos, 1.0f, 255, 120, 60);
            }
        }
    }
    if ((w.overlays & OV_PATHS) && GETP(wd->nav) != NULL) {
        NavData *nav = GETP(wd->nav);

        for (i = 0; i < nav->numNodes; i++) {
            NavPoint *n = &GETP(nav->nodes)[i];
            u32 k;

            for (k = 0; k < n->numNext; k++) {
                s32 j = GETP(n->next)[k];

                if (j >= 0 && (u32)j < nav->numNodes) {
                    f32 a[3] = { n->pos.x, n->pos.y + 1.0f, n->pos.z };
                    f32 b[3] = { GETP(nav->nodes)[j].pos.x, GETP(nav->nodes)[j].pos.y + 1.0f, GETP(nav->nodes)[j].pos.z };

                    AddLine(a, b, n->type == NAV_TYPE_WAYPOINT ? 255 : 90, 200, n->type == NAV_TYPE_WAYPOINT ? 60 : 255);
                }
            }
        }
    }
    for (i = 0; i < w.numHooks; i++) {
        if (w.hooks[i].on && w.hooks[i].draw != NULL) {
            w.hooks[i].draw();
        }
    }
    ViewerLevel_Collect();
}

/* One pass of the overlays' lines as thin quads facing the camera, about
 * `px` pixels wide whatever the EFB's size (GX lines are one pixel). */
static void Quads(const f32 *cam, f32 pxWorld, int dim) {
    u32 i, n;

    for (i = 0; i < s_numLines; i += n) {
        u32 k;

        n = s_numLines - i < 16000 ? s_numLines - i : 16000;
        GXBegin(0x80, 0, (u16)(n * 4));
        for (k = i; k < i + n; k++) {
            const Line *l = &s_lines[k];
            const f32 *a = l->p, *b = l->p + 3;
            f32 d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            f32 t[3] = { 0.5f * (a[0] + b[0]) - cam[0], 0.5f * (a[1] + b[1]) - cam[1], 0.5f * (a[2] + b[2]) - cam[2] };
            f32 p[3] = { d[1] * t[2] - d[2] * t[1], d[2] * t[0] - d[0] * t[2], d[0] * t[1] - d[1] * t[0] };
            f32 len = (f32)sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), ha, hb;
            f32 da = (f32)sqrt((a[0] - cam[0]) * (a[0] - cam[0]) + (a[1] - cam[1]) * (a[1] - cam[1]) +
                               (a[2] - cam[2]) * (a[2] - cam[2]));
            f32 db = (f32)sqrt((b[0] - cam[0]) * (b[0] - cam[0]) + (b[1] - cam[1]) * (b[1] - cam[1]) +
                               (b[2] - cam[2]) * (b[2] - cam[2]));
            u8 r = dim ? l->c[0] / 3 : l->c[0], g = dim ? l->c[1] / 3 : l->c[1], bl = dim ? l->c[2] / 3 : l->c[2];
            int j;

            if (len < 1e-6f) {
                p[0] = 0.0f, p[1] = 1.0f, p[2] = 0.0f, len = 1.0f;
            }
            ha = pxWorld * da / len;
            hb = pxWorld * db / len;
            {
                f32 v[4][3] = { { a[0] + p[0] * ha, a[1] + p[1] * ha, a[2] + p[2] * ha },
                                { b[0] + p[0] * hb, b[1] + p[1] * hb, b[2] + p[2] * hb },
                                { b[0] - p[0] * hb, b[1] - p[1] * hb, b[2] - p[2] * hb },
                                { a[0] - p[0] * ha, a[1] - p[1] * ha, a[2] - p[2] * ha } };

                for (j = 0; j < 4; j++) {
                    GXWGFifo_f32(v[j][0]);
                    GXWGFifo_f32(v[j][1]);
                    GXWGFifo_f32(v[j][2]);
                    GXWGFifo_u8(r);
                    GXWGFifo_u8(g);
                    GXWGFifo_u8(bl);
                }
            }
        }
    }
}

/* After the frame's scene (Gfx_EndScene): the overlays, in the camera's
 * projection: dimmed through what hides them, then bright where in front. */
void ViewerWorld_Overlays(f32 fov) {
    extern u32 m_fogMode;
    static const Matrix4 id = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
    Vector4 cam;
    int ew, eh;
    f32 px;

    if (!ViewerWorld_Active() || (w.overlays == 0 && !ViewerLevel_HasLines())) {
        return;
    }
    Collect();
    if (s_numLines == 0) {
        return;
    }
    Gfx_GetCameraPosition(&cam);
    GX_DrawEfbSize(&ew, &eh);
    /* half of 1.5 points at the EFB's height, in units at distance 1 */
    px = 0.75f * (eh > 600 ? (f32)eh / 600.0f : 1.0f) * 2.0f * (f32)tan(0.5f * fov * 0.75f * 0.017453292f) /
         (f32)(eh > 0 ? eh : 480);
    Gfx_SetProjectionMatrix(fov);
    Gfx_SetWorldMatrix((f32 (*)[4])&id);
    Render_SetTextureHandle(0xFFFF);
    Render_StateAlphaBlendEnable(0);
    Render_StateFogEnable(0);
    Render_StateCullMode(0);
    Set3DLineVtxDesc();
    Render_StateZEnable(0);
    Render_StateZWriteEnable(0);
    Quads(&cam.x, px, 1);
    Render_StateZEnable(1);
    Quads(&cam.x, px, 0);
    Render_StateZWriteEnable(1);
    Render_StateFogEnable(m_fogMode == 1);
    Gfx_SetWorldMatrix(NULL);
}

/* ---- picking and places ----------------------------------------------------- */

static int RaySphere(const f32 *o, const f32 *d, const Vector4 *c, f32 r, f32 *t) {
    f32 v[3] = { c->x - o[0], c->y - o[1], c->z - o[2] }, tc, d2;

    tc = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
    d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - tc * tc;
    if (tc + r < 0.0f || d2 > r * r) {
        return 0;
    }
    *t = tc;
    return 1;
}

/* The world object under a ray (origin o, unit direction d): its offset in
 * the world pack, *kind ("instance", "item", "tile_car", ...), *t along the ray;
 * VIEWER_NO_OFFSET if none. Instances of the visible quadrants, pickups,
 * effect points, tile cars and creatures; the smallest hit sphere wins. */
u32 ViewerWorld_Pick(const f32 *o, const f32 *d, f32 *tOut, const char **kind) {
    World *wd = WORLD();
    u8 *base;
    f32 bestR = 1e30f, bestT = 1e30f, t;
    u32 best = VIEWER_NO_OFFSET, q, i;

#define TRY(ptr, pos, rad, k)                                                                                           \
    do {                                                                                                               \
        f32 r_ = (rad);                                                                                                \
        if (RaySphere(o, d, (pos), r_, &t) && (r_ < bestR * 0.999f || (r_ <= bestR * 1.001f && t < bestT))) {          \
            bestR = r_;                                                                                                \
            bestT = t;                                                                                                 \
            best = (u32)((u8 *)(ptr) - base);                                                                          \
            *kind = (k);                                                                                               \
        }                                                                                                              \
    } while (0)

    if (!ViewerWorld_Active() || w.vp == NULL) {
        return VIEWER_NO_OFFSET;
    }
    base = GETP(w.vp->pack->base);
    for (q = 0; q < wd->numQuads; q++) {
        WorldQuad *quad = &GETP(wd->quads)[q];
        QuadContents *qc = GETP(quad->contents);

        if (qc == NULL || (!quad->visible && GETP(quad->bsp) != NULL)) {
            continue;
        }
        for (i = 0; i < qc->numInstances; i++) {
            QuadInstance *in = &GETP(qc->instances)[i];

            TRY(in, &in->pos, in->radius > 0.5f ? in->radius : 2.0f, "instance");
        }
        for (i = 0; i < qc->numItems; i++) {
            TRY(&GETP(qc->items)[i], &GETP(qc->items)[i].pos, 2.5f, "item");
        }
        for (i = 0; i < qc->numFxPoints; i++) {
            TRY(&GETP(qc->fxPoints)[i], &GETP(qc->fxPoints)[i].pos, 1.5f, "fx_point");
        }
    }
    for (i = 0; i < wd->numTileCars; i++) {
        TRY(&GETP(wd->tileCars)[i], &GETP(wd->tileCars)[i].pos, 5.0f, "tile_car");
    }
    for (i = 0; i < wd->numTileCreatures; i++) {
        TRY(&GETP(wd->tileCreatures)[i], &GETP(wd->tileCreatures)[i].pos, 3.0f, "tile_creature");
    }
    for (i = 0; i < wd->numVolumes; i++) {
        WorldVolume *v = &GETP(wd->volumes)[i];

        /* the sphere airboxes the scripts test (for links to them), as big as the test */
        if (v->shape == VOLUME_SHAPE_SPHERE && v->type == VOLUME_AIRBOX && v->radiusSq > 0.0f) {
            TRY(v, &v->center, (f32)sqrt(v->radiusSq), "volume");
        }
    }
#undef TRY
    *tOut = bestT;
    return best;
}

/* The structural path of the record that starts at `off` of the world pack:
 * World/0/tileCars/N, World/0/volumes/N, World/0/tileCreatures/N,
 * World/0/quads/Q/contents/<instances|items|fxPoints>/N; "" for others. */
void ViewerWorld_PathOf(u32 off, char *path, size_t size) {
    World *wd = WORLD();
    u8 *base, *p;
    u32 q, i;

    path[0] = 0;
    if (!ViewerWorld_Active() || w.vp == NULL) {
        return;
    }
    base = GETP(w.vp->pack->base);
    p = base + off;
#define IN(arr, n, sz, fmt, ...)                                                                                       \
    if ((u8 *)(arr) != NULL && p >= (u8 *)(arr) && p < (u8 *)(arr) + (n) * (sz) && (p - (u8 *)(arr)) % (sz) == 0) {   \
        i = (u32)((p - (u8 *)(arr)) / (sz));                                                                           \
        SDL_snprintf(path, size, fmt, __VA_ARGS__);                                                                    \
        return;                                                                                                        \
    }
    IN(GETP(wd->tileCars), wd->numTileCars, sizeof(TileCar), "World/0/tileCars/%u", (unsigned)i)
    IN(GETP(wd->volumes), wd->numVolumes, sizeof(WorldVolume), "World/0/volumes/%u", (unsigned)i)
    IN(GETP(wd->tileCreatures), wd->numTileCreatures, 0x40, "World/0/tileCreatures/%u", (unsigned)i)
    for (q = 0; q < wd->numQuads; q++) {
        QuadContents *qc = GETP(GETP(wd->quads)[q].contents);

        if (qc == NULL) {
            continue;
        }
        IN(GETP(qc->instances), qc->numInstances, 0x40, "World/0/quads/%u/contents/instances/%u", (unsigned)q, (unsigned)i)
        IN(GETP(qc->items), qc->numItems, 0x40, "World/0/quads/%u/contents/items/%u", (unsigned)q, (unsigned)i)
        IN(GETP(qc->fxPoints), qc->numFxPoints, 0x40, "World/0/quads/%u/contents/fxPoints/%u", (unsigned)q, (unsigned)i)
    }
#undef IN
}

/* The place of the world object whose struct holds [off, off + size) of the
 * world pack (or that starts in it), for focus. 1 if there is one. */
int ViewerWorld_PlaceOf(u32 off, u32 size, f32 *pos, f32 *radius) {
    World *wd = WORLD();
    u8 *base;
    u32 q, i;

#define HIT(ptr, sz) ((u32)((u8 *)(ptr) - base) < off + (size ? size : 1) && (u32)((u8 *)(ptr) - base) + (sz) > off)
#define SET(p, r)                                                                                                      \
    do {                                                                                                               \
        pos[0] = (p)->x, pos[1] = (p)->y, pos[2] = (p)->z, *radius = (r);                                              \
        return 1;                                                                                                      \
    } while (0)

    if (!ViewerWorld_Active() || w.vp == NULL) {
        return 0;
    }
    base = GETP(w.vp->pack->base);
    for (i = 0; i < wd->numTileCars; i++) {
        if (HIT(&GETP(wd->tileCars)[i], sizeof(TileCar))) {
            SET(&GETP(wd->tileCars)[i].pos, 6.0f);
        }
    }
    for (i = 0; i < wd->numTileCreatures; i++) {
        if (HIT(&GETP(wd->tileCreatures)[i], sizeof(TileCreature))) {
            SET(&GETP(wd->tileCreatures)[i].pos, 4.0f);
        }
    }
    for (q = 0; q < wd->numQuads; q++) {
        QuadContents *qc = GETP(GETP(wd->quads)[q].contents);

        for (i = 0; qc != NULL && i < qc->numInstances; i++) {
            QuadInstance *in = &GETP(qc->instances)[i];

            if (HIT(in, sizeof(QuadInstance))) {
                SET(&in->pos, in->radius > 1.0f ? in->radius : 3.0f);
            }
        }
        for (i = 0; qc != NULL && i < qc->numItems; i++) {
            if (HIT(&GETP(qc->items)[i], sizeof(QuadItem))) {
                SET(&GETP(qc->items)[i].pos, 3.0f);
            }
        }
        for (i = 0; qc != NULL && i < qc->numFxPoints; i++) {
            if (HIT(&GETP(qc->fxPoints)[i], sizeof(QuadFxPoint))) {
                SET(&GETP(qc->fxPoints)[i].pos, 3.0f);
            }
        }
        if (q < w.numRegionBoxes && HIT(&GETP(wd->quads)[q], sizeof(WorldQuad)) && w.regionBox[q][0] <= w.regionBox[q][3]) {
            const f32 *b = w.regionBox[q];
            Vector4 c;

            c.x = 0.5f * (b[0] + b[3]), c.y = 0.5f * (b[1] + b[4]), c.z = 0.5f * (b[2] + b[5]);
            SET(&c, 0.5f * (f32)sqrt((b[3] - b[0]) * (b[3] - b[0]) + (b[5] - b[2]) * (b[5] - b[2])));
        }
    }
#undef HIT
#undef SET
    return 0;
}

/* A tile car's forward direction (its matrix's z axis), as the game orients it. */
static void TileForward(const TileCar *c, f32 *fwd) {
    Matrix4 m __attribute__((aligned(16)));

    SetOrientation(&m, c->rotY, c->rotX, c->rotZ);   /* as StateGamePlay_Init */
    fwd[0] = m.v[2].x, fwd[1] = m.v[2].y, fwd[2] = m.v[2].z;
}

/* Player `port`'s start (its tile car), else the first tile car, else the
 * first nav point: position and the orbit yaw (radians) that looks along it
 * from behind. 0: none. */
int ViewerWorld_Start(int port, f32 *pos, f32 *yaw) {
    World *wd = WORLD();
    TileCar *c = NULL;
    u32 i;

    if (!ViewerWorld_Active()) {
        return 0;
    }
    for (i = 0; i < wd->numTileCars && c == NULL; i++) {
        if (port <= 0 || GETP(wd->tileCars)[i].port == port) {
            c = &GETP(wd->tileCars)[i];
        }
    }
    if (c == NULL && wd->numTileCars != 0) {
        c = &GETP(wd->tileCars)[0];
    }
    if (c != NULL) {
        f32 f[3];

        TileForward(c, f);
        pos[0] = c->pos.x, pos[1] = c->pos.y, pos[2] = c->pos.z;
        *yaw = (f32)atan2(-f[0], -f[2]);
        return 1;
    }
    if (GETP(wd->nav) != NULL && ((NavData *)GETP(wd->nav))->numNodes != 0) {
        NavPoint *n = &GETP(((NavData *)GETP(wd->nav))->nodes)[0];

        pos[0] = n->pos.x, pos[1] = n->pos.y, pos[2] = n->pos.z, *yaw = 0.0f;
        return 1;
    }
    return 0;
}

/* Nav point i (the AI's route; tests fly along it): position and the orbit
 * yaw looking on to its next point. 0: none. */
int ViewerWorld_NavPoint(u32 i, f32 *pos, f32 *yaw) {
    NavData *nav = ViewerWorld_Active() ? GETP(WORLD()->nav) : NULL;
    NavPoint *n, *m;

    if (nav == NULL || i >= nav->numNodes) {
        return 0;
    }
    n = &GETP(nav->nodes)[i];
    m = n->numNext != 0 && GETP(n->next)[0] >= 0 && (u32)GETP(n->next)[0] < nav->numNodes ? &GETP(nav->nodes)[GETP(n->next)[0]]
                                                                                            : n;
    pos[0] = n->pos.x, pos[1] = n->pos.y, pos[2] = n->pos.z;
    *yaw = m != n ? (f32)atan2(-(m->pos.x - n->pos.x), -(m->pos.z - n->pos.z)) : 0.0f;
    return 1;
}

/* The world's collision bounds (for framing it all). */
int ViewerWorld_Bounds(f32 *min, f32 *max) {
    WorldCollision *col;

    if (!ViewerWorld_Active() || (col = GETP(WORLD()->collision)) == NULL) {
        return 0;
    }
    min[0] = col->min.x, min[1] = col->min.y, min[2] = col->min.z;
    max[0] = col->max.x, max[1] = col->max.y, max[2] = col->max.z;
    return 1;
}

/* ---- the protocol ---------------------------------------------------------------- */

static void WriteOverlays(JsonWriter *jw) {
    u32 i;

    Jw_BeginObject(jw, "overlays", 1);
    for (i = 0; i < SDL_arraysize(s_overlayNames); i++) {
        Jw_Bool(jw, s_overlayNames[i].name, (w.overlays & s_overlayNames[i].bit) != 0);
    }
    for (i = 0; i < w.numHooks; i++) {
        Jw_Bool(jw, w.hooks[i].name, w.hooks[i].on);
    }
    Jw_EndObject(jw);
}

void ViewerWorld_WriteStatus(JsonWriter *jw) {
    World *wd = WORLD();
    u32 q, loaded = 0, visible = 0;
    s32 cur;

    if (!ViewerWorld_Active()) {
        Viewer_WriteNull(jw, "world");
        return;
    }
    cur = CameraQuad();
    Jw_BeginObject(jw, "world", 0);
    Jw_String(jw, "name", w.name);
    Jw_String(jw, "world", w.path);
    if (w.vp != NULL) {
        char full[200];

        SDL_snprintf(full, sizeof(full), "%s/%s", w.vp->archiveName, w.vp->name);
        Jw_String(jw, "pack", full);   /* the world pack's outline */
    }
    Jw_Int(jw, "quads", wd->numQuads);
    Jw_BeginArray(jw, "loadedQuads", 1);
    for (q = 0; q < wd->numQuads; q++) {
        if (GETP(wd->quads)[q].loaded) {
            Jw_Int(jw, NULL, q);
            loaded++;
        }
        visible += GETP(wd->quads)[q].visible != 0;
    }
    Jw_EndArray(jw);
    if (loaded > w.maxLoaded) {
        w.maxLoaded = loaded;
    }
    Jw_Int(jw, "loaded", loaded);
    Jw_Int(jw, "visible", visible);
    Jw_Int(jw, "maxLoaded", w.maxLoaded);
    Jw_Int(jw, "cameraQuad", cur);
    Jw_Int(jw, "tileCars", wd->numTileCars);
    Jw_Int(jw, "tileCreatures", wd->numTileCreatures);
    Jw_Int(jw, "volumes", wd->numVolumes);
    Jw_Int(jw, "navPoints", GETP(wd->nav) != NULL ? ((NavData *)GETP(wd->nav))->numNodes : 0);
    Jw_Float(jw, "loadMs", w.loadMs);
    WriteOverlays(jw);
    Jw_EndObject(jw);
}

int ViewerWorld_Dispatch(const char *cmd, const JsonNode *req, JsonWriter *jw, int *ok, char *err, size_t errSize) {
    if (SDL_strcmp(cmd, "worlds") == 0) {
        /* every level of every progression script, as the MCP lists them */
        Jw_BeginArray(jw, "worlds", 0);
        EachLevel(WriteLevel, jw);
        Jw_EndArray(jw);
    } else if (SDL_strcmp(cmd, "world") == 0) {
        const char *name = Json_GetString(req, "name", Json_GetString(req, "world", NULL));
        FindCtx f = { name, Json_GetString(req, "mode", NULL), "" };

        if (name == NULL) {
            if (!ViewerWorld_Active()) {
                SDL_snprintf(err, errSize, "world takes name (a level's world: ch1_l2, bat1_l1, world1), mode?");
                *ok = 0;
            } else {
                ViewerWorld_WriteStatus(jw);
            }
            return 1;
        }
        EachLevel(FindLevel, &f);
        if (f.found[0] == 0) {
            SDL_snprintf(err, errSize, "no level's world is %.60s%s%.20s (see worlds)", name, f.mode ? " in " : "",
                         f.mode ? f.mode : "");
            *ok = 0;
        } else if ((*ok = Load(f.found, err, errSize))) {
            f32 lo[3], hi[3];

            Port_SpawnEnvelope(lo, hi, NULL, 1);    /* the starts' box, now (never while drawing) */
            Viewer_WorldLoaded();
            ViewerWorld_WriteStatus(jw);
        }
    } else if (SDL_strcmp(cmd, "world_unload") == 0) {
        ViewerWorld_Unload();
        Viewer_WorldLoaded();
    } else if (SDL_strcmp(cmd, "overlays") == 0) {
        /* any of the overlay names: true / false */
        u32 i;

        for (i = 0; i < SDL_arraysize(s_overlayNames); i++) {
            const JsonNode *v = Json_Get(req, s_overlayNames[i].name);

            if (v != NULL && v->type == JSON_BOOL) {
                w.overlays = v->boolean ? w.overlays | s_overlayNames[i].bit : w.overlays & ~s_overlayNames[i].bit;
            }
        }
        for (i = 0; i < w.numHooks; i++) {
            const JsonNode *v = Json_Get(req, w.hooks[i].name);

            if (v != NULL && v->type == JSON_BOOL) {
                w.hooks[i].on = v->boolean;
            }
        }
        WriteOverlays(jw);
    } else {
        return 0;
    }
    return 1;
}
