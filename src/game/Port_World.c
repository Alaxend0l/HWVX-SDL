/* Custom worlds: a world from a modder's geometry composed over a base level
 * as it loads (Port_World.h; superproject docs/plans/custom-worlds.md).
 * Port-only file.
 *
 * world.jsonc (written by `hwvx-x world`):
 *   id             the track's id (uid://...)
 *   name           its name in the lists
 *   base           the track number whose level it is composed over
 *   archive        <archive>.xgc next to the file: the hub quadrant pack
 *                  (TOC world `worldId`, pack 0xFFFF, `hubGeometries`) and
 *                  the collision pack (id `collisionId`)
 *   start          [x, y, z, heading in degrees] of player 1's car
 *                  (forward is sin h, 0, cos h)
 *
 * As the base's world pack loads (Port_WorldLoaded) its quadrant table
 * becomes one hub quadrant (no region, no contents: the base's objects are
 * not placed), the quadrant archive is the world's, the tile cars start at
 * the world's start, the base's teleporters go out of the way (those only
 * arrived at, the respawn points, to the start), and the volumes (fog,
 * reverb), speakers, creatures and the radar map, which are in the base's
 * coordinates, are dropped. The world's collision pack is loaded next to the
 * base's (Port_WorldCollisionLoaded) and World.collision becomes a copy of
 * its grid header with the base pack's materials (Port_WorldBindCollision),
 * so surfaces, sparks, skid marks and ground sounds are the base's. What is
 * allocated goes with the world pack (PortGeometry_Keep). */
#include <SDL3/SDL.h>
#include "common.h"
#include "World.h"
#include "Port_Content.h"
#include "Port_Swap.h"
#include "Port_World.h"
#include "jsonc.h"

#define PREFIX "@world\\"

static struct {
    int loaded;            /* HWVX_WORLD read */
    char dir[1024];        /* its folder, with the separator */
    char id[128];
    char name[64];
    char archive[96];      /* "@world\<archive>": the name the game opens */
    s32 base;
    u32 geoms;
    u32 worldId;
    u32 collisionId;
    f32 start[4];
    s32 track;             /* the registered track, -1: none */
} s_w = { .track = -1 };

static int s_active;                   /* the world loaded now is the custom one */
static WorldCollision *s_grid;         /* its collision pack's grid */

int Port_WorldActive(void) {
    return s_active;
}

static int ReadWorld(const char *path) {
    char err[160];
    size_t len = 0;
    char *text = SDL_LoadFile(path, &len);
    JsonNode *j;
    const JsonNode *st;
    const char *slash;
    int k;

    if (text == NULL) {
        Port_Log("world: %s: %s", path, SDL_GetError());
        return 0;
    }
    j = Json_Parse(text, len, err, sizeof(err));
    SDL_free(text);
    if (j == NULL) {
        Port_Log("world: %s: %s", path, err);
        return 0;
    }
    SDL_strlcpy(s_w.id, Json_GetString(j, "id", "uid://hwvx-world"), sizeof(s_w.id));
    SDL_strlcpy(s_w.name, Json_GetString(j, "name", "Custom world"), sizeof(s_w.name));
    SDL_snprintf(s_w.archive, sizeof(s_w.archive), PREFIX "%s", Json_GetString(j, "archive", "world"));
    s_w.base = (s32)Json_GetInt(j, "base", 0);
    s_w.geoms = (u32)Json_GetInt(j, "hubGeometries", 0);
    s_w.worldId = (u32)Json_GetInt(j, "worldId", 0);
    s_w.collisionId = (u32)Json_GetInt(j, "collisionId", 0);
    st = Json_Get(j, "start");
    for (k = 0; k < 4; k++) {
        const JsonNode *v = st != NULL ? Json_At(st, k) : NULL;
        s_w.start[k] = v != NULL && v->type == JSON_NUMBER ? (f32)v->number : 0.0f;
    }
    Json_Free(j);
    slash = SDL_strrchr(path, '/');
#ifdef _WIN32
    if (SDL_strrchr(path, '\\') > slash) {
        slash = SDL_strrchr(path, '\\');
    }
#endif
    if (slash == NULL) {
        SDL_strlcpy(s_w.dir, "./", sizeof(s_w.dir));
    } else {
        SDL_strlcpy(s_w.dir, path, SDL_min(sizeof(s_w.dir), (size_t)(slash - path) + 2));
    }
    if (s_w.geoms == 0 || s_w.geoms > 0xFFFF) {
        Port_Log("world: %s: hubGeometries %u", path, (unsigned)s_w.geoms);
        return 0;
    }
    return 1;
}

void Port_WorldRegister(void) {
    const char *path = SDL_getenv("HWVX_WORLD");
    PortContentEntry entry;

    if (path == NULL || *path == 0 || s_w.loaded || !(s_w.loaded = ReadWorld(path))) {
        return;
    }
    if (s_w.base < 0 || (u32)s_w.base >= Port_ContentBaseCount(PORT_CONTENT_TRACK)) {
        Port_Log("world: no base track %d", (int)s_w.base);
        s_w.loaded = 0;
        return;
    }
    SDL_zero(entry);
    entry.number = -1;
    entry.id = s_w.id;
    entry.alias = s_w.base;
    entry.name = s_w.name;
    s_w.track = Port_ContentRegisterEntry(PORT_CONTENT_TRACK, &entry);
    Port_Log("world: %s (%s) is track %d over track %d, %s.xgc, %u hub geometries, start %.1f %.1f %.1f heading %.1f",
             s_w.id, s_w.name, (int)s_w.track, (int)s_w.base, s_w.archive + SDL_strlen(PREFIX), (unsigned)s_w.geoms,
             s_w.start[0], s_w.start[1], s_w.start[2], s_w.start[3]);
}

char *Port_WorldResolveFile(const char *name, int *format) {
    size_t n = SDL_strlen(PREFIX);
    char *path = NULL, *p;
    const char *rest;
    size_t len;

    while (*name == '/' || *name == '\\') {
        name++;
    }
    /* "@world\\<file>", its separator turned to "/" on the way in (dvd.c) */
    if (!s_w.loaded || SDL_strncasecmp(name, PREFIX, n - 1) != 0 || (name[n - 1] != '\\' && name[n - 1] != '/')) {
        return NULL;
    }
    rest = name + n;
    len = SDL_strlen(rest);
    if (len > 4 && SDL_strcasecmp(rest + len - 4, ".xgc") == 0) {
        len -= 4;
    }
    if (len == 0 || SDL_strstr(rest, "..") != NULL || SDL_strchr(rest, '/') != NULL || SDL_strchr(rest, '\\') != NULL) {
        return NULL;
    }
    SDL_asprintf(&path, "%s%.*s.xgc", s_w.dir, (int)len, rest);
    for (p = path; p != NULL && *p; p++) {
#ifndef _WIN32
        if (*p == '\\') {
            *p = '/';
        }
#endif
    }
    *format = PORT_DATA_GC;
    return path;
}

static void *Keep(void *pack, u32 size) {
    void *p = Allocate(size, 0, "");

    if (p != NULL) {
        SDL_memset(p, 0, size);
        PortGeometry_Keep(pack, p);
    }
    return p;
}

void Port_WorldLoaded(void *worldPack, int track) {
    World *w;
    WorldQuad *quad;
    QuadContents *contents;
    u32 i;
    f32 yaw;

    s_active = 0;
    s_grid = NULL;
    if (!s_w.loaded || track < 0 || track != s_w.track || worldPack == NULL || (w = SLOT_LOAD(World *, worldPack)) == NULL) {
        return;
    }
    quad = Keep(worldPack, sizeof(WorldQuad));
    contents = Keep(worldPack, sizeof(QuadContents));
    if (quad == NULL || contents == NULL) {
        Port_Log("world: out of memory");
        return;
    }
    /* the hub: no region, no neighbours, no contents */
    quad->numGeoms = s_w.geoms;
    quad->packId = (s_w.geoms << 16) | 0xFFFF;
    SETP(quad->contents, contents);
    w->numQuads = 1;
    SETP(w->quads, quad);
    w->id = s_w.worldId;
    SETP(w->name, s_w.archive);
    /* the start */
    yaw = s_w.start[3] * (f32)(SDL_PI_D / 180.0);
    for (i = 0; i < w->numTileCars; i++) {
        TileCar *c = &GETP(w->tileCars)[i];

        c->pos.x = s_w.start[0];
        c->pos.y = s_w.start[1];
        c->pos.z = s_w.start[2];
        c->rotX = 0.0f;
        c->rotY = yaw;
        c->rotZ = 0.0f;
    }
    /* teleporters: the respawn points (only arrived at) to the start, the
     * others far below everything */
    for (i = 0; i < w->numTeleporters; i++) {
        Teleporter *t = &GETP(w->teleporters)[i];
        f32 *m = (f32 *)&t->mtx;

        if (t->numDest == 0) {
            m[12] = s_w.start[0];
            m[13] = s_w.start[1] + 10.0f;
            m[14] = s_w.start[2];
        } else {
            m[13] = -100000.0f;
        }
    }
    SETP(w->volumeHolder, NULL);
    w->numSpeakers = 0;
    w->numTileCreatures = 0;
    s_active = 1;
    Port_Log("world: %s composed over the level (%u tile cars, %u teleporters); quadrants from %s.xgc",
             s_w.id, (unsigned)w->numTileCars, (unsigned)w->numTeleporters, s_w.archive);
}

void Port_WorldCollisionLoaded(void) {
    s32 slot;
    void *dp;
    WorldCollision *g;

    if (!s_active) {
        return;
    }
    slot = FileStreaming_LoadDataPack(s_w.archive, 5, s_w.collisionId, 0);
    dp = slot >= 0 ? FileStreaming_InitializeDataPack((u32)slot, 5, 1) : NULL;
    g = dp != NULL ? SLOT_LOAD(WorldCollision *, dp) : NULL;
    if (g == NULL || g->version != 0xD || g->mode != 1) {
        Port_Log("world: %s.xgc: no collision pack %u (version 0xD, mode 1): the base's is used", s_w.archive,
                 (unsigned)s_w.collisionId);
        return;
    }
    s_grid = g;
    Port_Log("world: collision grid %d x %d x %d, %u edges", (int)g->numX, (int)g->numY, (int)g->numZ, (unsigned)g->numEdges);
}

void Port_WorldBindCollision(void) {
    WorldCollision *base, *mine;
    WorldGameData *gd;

    /* the radar map is the base's: off (here, after the refresh wrote its texture slot) */
    if (s_active && WORLD() != NULL && (gd = GETP(WORLD()->gameData)) != NULL) {
        gd->mapTexture = 0;
    }
    if (!s_active || s_grid == NULL || WORLD() == NULL || (base = GETP(WORLD()->collision)) == NULL) {
        return;
    }
    mine = Keep(PTR(g_worldData.worldPack), sizeof(WorldCollision));
    if (mine == NULL) {
        return;
    }
    *mine = *s_grid;
    mine->materials = base->materials;
    SETP(WORLD()->collision, mine);
}
