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
 * becomes one hub quadrant (no region, no contents: the base's placed
 * objects, effect points and items are not placed, its sky not drawn), the
 * quadrant archive is the world's, the players' cars (tile cars of ports
 * 1-4) start side by side at the world's start, and everything else the base
 * places, moves, lights or plays in its own coordinates goes, by kind, for
 * any base: the AI's spawns (never spawned), movers on paths (trains,
 * spinning props), volumes (fog, reverb, force, light), speakers, creatures,
 * tile lights, vertex colour lights, the nav graph (the AI's routes), the
 * weather emitter kept ahead of the camera, the radar map, the teleporters'
 * effects (respawn points to the start, the others far below), and from the
 * level's scripts (TAC) the triggers testing the base's places and the
 * actions acting on them (CleanTriggers). The level's rules (timers,
 * variables, objectives, interfaces, music, the end of the level) stay; the
 * log says what went, per kind. The world's collision pack is loaded next
 * to the base's (Port_WorldCollisionLoaded) and World.collision becomes a
 * copy of its grid header with the base pack's materials (Port_WorldBindCollision),
 * so surfaces, sparks, skid marks and ground sounds are the base's. What is
 * allocated goes with the world pack (PortGeometry_Keep). */
#include <SDL3/SDL.h>
#include "common.h"
#include "World.h"
#include "Nav.h"
#include "Trigger.h"
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

/* ---- what of the base level goes ------------------------------------------
 * Everything the base level places, moves, lights or plays is in its own
 * coordinates, over geometry that is not drawn: it goes, for any base, by
 * kind (none of it by name). What stays is what is not in a place: the
 * level's rules (timers, variables, objectives, interfaces, music, the end
 * of the level), the chase cameras, fog, the item pack, the surfaces. */

/* Trigger tests (and conditions) on the base's places and objects: volumes,
 * placed objects, items, movers, creatures. A trigger with one goes whole. */
static int DropsTriggerType(u32 type) {
    switch (type) {
    case TRIGGER_AIRBOX_ENTERED:
    case TRIGGER_AIRBOX_EXITED:
    case TRIGGER_AIRBOX_STOP_IN:
    case TRIGGER_DESTRUCTIBLE_KILLED:
    case TRIGGER_CREATURE_KILLED:
    case TRIGGER_ITEM_PICKUP:
    case TRIGGER_MOVEABLE_END:
    case TRIGGER_P2P_ACTIVATED:
    case TRIGGER_CREATURE_DROPPED_OFF:
    case TRIGGER_ITEM_VOLUME_ENTERED:
    case TRIGGER_CREATURE_PICKED_UP:
    case TRIGGER_ITEM_DROPPED:
    case TRIGGER_MOVABLE_VOLUME_ENTERED:
    case TRIGGER_CREATURE_VOLUME_ENTERED:
        return 1;
    }
    return 0;
}

/* Actions on the base's places and objects: volumes, placed objects and
 * effect points, items, movers, AI routes, speakers, vertex colour and
 * scripted lights, teleports, sound sequences, creatures, the target arrow,
 * cameras sent anywhere but behind the player. */
static int DropsAction(const TriggerAction *a) {
    const u32 *d = GETP(a->data);

    switch (a->type) {
    case ACTION_AIRBOX_CHANGE_MODE:
    case ACTION_DESTRUCTIBLE_CHANGE_MODE:
    case ACTION_FX_POINT_CHANGE_MODE:
    case ACTION_ITEM_CHANGE_MODE:
    case ACTION_MOVEABLE_CHANGE_MODE:
    case ACTION_NAV_POINT_CHANGE_MODE:
    case ACTION_SPEAKER_CHANGE_MODE:
    case ACTION_TARGET_ARROW_SET_TARGET:
    case ACTION_VEHICLE_AI_SET_NAMED_PATH:
    case ACTION_VERTEX_COLOR_SEQ_CHANGE_MODE:
    case ACTION_VEHICLE_TELEPORT:
    case ACTION_SCRIPT_LIGHT_CHANGE_MODE:
    case ACTION_PLAY_SOUND_SEQUENCE:
    case ACTION_CREATURE_CHANGE_MODE:
        return 1;
    case ACTION_CAMERA_SET_TARGETS:
        return d == NULL || d[1] != CAMERA_PLAYER;
    case ACTION_VEHICLE_GIVE_ITEM:
        return d == NULL || d[0] == 0;   /* 0: one of the base's placed items; 1: from the item pack */
    }
    return 0;
}

static const char *const s_triggerNames[TRIGGER_NUM_TYPES] = {
    "true", "airbox entered", "airbox exited", "airbox stop in", "destructible killed", "creature killed",
    "item pickup", "interface end", "level start", "timer", "moveable end", "variable", "vehicle killed",
    "p2p activated", "creature dropped off", "interface pack end", "vehicle damage", "gear", "in air",
    "item volume entered", "code variable", "creature picked up", "rounds complete", "round x complete",
    "item dropped", "movable volume entered", "sound seq end", "creature volume entered",
};
static const char *const s_actionNames[ACTION_NUM_TYPES] = {
    "airbox", "camera", "destructible", "fx point", "interface", "item", "end level", "timer value",
    "timer mode", "mover", "music", "song", "nav point", "pause", "speaker", "target arrow", "variable",
    "vehicle mode", "ai path", "vertex colour", "debug", "teleport", "vehicle control", "player vehicle",
    "remove item", "remove creature", "variable mode", "load interface", "play interface",
    "unload interface", "end interface pack", "end interface", "hud", "script light", "code variable",
    "objective", "pause tac", "ai script", "reset round", "round complete", "give item", "player order",
    "sound sequence", "creature",
};

typedef struct Dropped {
    u32 triggers, actions, cars;
    u32 byTrigger[TRIGGER_NUM_TYPES];
    u32 byAction[ACTION_NUM_TYPES];
} Dropped;

/* Where an action's list of tile cars is (Trigger.h TAC_PARAMS_ACTION_*), -1: none. */
static int CarList(u32 type) {
    switch (type) {
    case ACTION_VEHICLE_CHANGE_MODE:
    case ACTION_VEHICLE_REMOVE_ITEM:
        return 1;
    case ACTION_VEHICLE_CHANGE_CONTROL:
    case ACTION_VEHICLE_GIVE_ITEM:
        return 2;
    case ACTION_SWAP_AI_SCRIPT:
        return 4;
    }
    return -1;
}

/* The tile cars a car list (count, handles) names that are dropped come out
 * of it; 1: none left. */
static int FilterCars(u32 *list, const World *w, const u8 *dropCar) {
    u32 n = list[0], i, k, out = 0;

    for (i = 0; i < n; i++) {
        int drop = 0;

        for (k = 0; k < w->numTileCars; k++) {
            if (dropCar[k] && list[1 + i] == (u32)HND(&GETP(w->tileCars)[k])) {
                drop = 1;
            }
        }
        if (!drop) {
            list[1 + out++] = list[1 + i];
        }
    }
    list[0] = out;
    return n != 0 && out == 0;
}

/* The actions of a list that are kept, moved to its front; their count. */
static u32 CleanActions(TriggerAction *a, u32 n, const World *w, const u8 *dropCar, Dropped *dr) {
    u32 i, out = 0;

    for (i = 0; a != NULL && i < n; i++) {
        u32 *d = GETP(a[i].data);
        int drop = DropsAction(&a[i]), list;

        if (!drop && d != NULL && (list = CarList(a[i].type)) >= 0) {
            drop = FilterCars(&d[list], w, dropCar);
        }
        if (drop) {
            dr->actions++;
            dr->byAction[a[i].type < ACTION_NUM_TYPES ? a[i].type : 0]++;
            continue;
        }
        if (out != i) {
            a[out] = a[i];
        }
        out++;
    }
    return out;
}

/* The pack's triggers that test the base's places go; the rest keep their
 * actions that don't act on them. Before the refresh activates them. */
static void CleanTriggers(DataPack *pack, const World *w, const u8 *dropCar, Dropped *dr) {
    Trigger *t = (Trigger *)GETP(pack->triggers);
    u32 n = pack->numTriggers, i, j, out = 0;

    for (i = 0; t != NULL && i < n; i++) {
        Trigger *c = GETP(t[i].conditions);
        int drop = t[i].type >= TRIGGER_NUM_TYPES || DropsTriggerType(t[i].type);

        for (j = 0; !drop && c != NULL && j < t[i].numConditions; j++) {
            drop = c[j].type >= TRIGGER_NUM_TYPES || DropsTriggerType(c[j].type);
        }
        if (drop) {
            dr->triggers++;
            dr->byTrigger[t[i].type < TRIGGER_NUM_TYPES ? t[i].type : 0]++;
            continue;
        }
        t[i].numActions = CleanActions(GETP(t[i].actions), t[i].numActions, w, dropCar, dr);
        t[i].numElseActions = CleanActions(GETP(t[i].elseActions), t[i].numElseActions, w, dropCar, dr);
        if (out != i) {
            t[out] = t[i];
        }
        out++;
    }
    pack->numTriggers = out;
}

static void LogCounts(const char *what, const u32 *counts, const char *const *names, u32 n) {
    char line[512];
    size_t at = 0;
    u32 k;

    line[0] = 0;
    for (k = 0; k < n && at < sizeof(line); k++) {
        if (counts[k] != 0) {
            at += (size_t)SDL_snprintf(line + at, sizeof(line) - at, "%s%s %u", at ? ", " : "", names[k],
                                       (unsigned)counts[k]);
        }
    }
    if (at != 0) {
        Port_Log("world:   %s: %s", what, line);
    }
}

void Port_WorldLoaded(void *worldPack, int track) {
    World *w;
    WorldQuad *quad;
    QuadContents *contents;
    WorldGameData *gd;
    NavData *nav;
    Dropped dr;
    u8 *dropCar;
    u32 i, port, humans = 0, instances = 0, fxPoints = 0, items = 0, quads, teleporters = 0, emitters = 0;
    f32 yaw, side;

    s_active = 0;
    s_grid = NULL;
    if (!s_w.loaded || track < 0 || track != s_w.track || worldPack == NULL || (w = SLOT_LOAD(World *, worldPack)) == NULL) {
        return;
    }
    quad = Keep(worldPack, sizeof(WorldQuad));
    contents = Keep(worldPack, sizeof(QuadContents));
    dropCar = SDL_calloc(w->numTileCars + 1, 1);
    if (quad == NULL || contents == NULL || dropCar == NULL) {
        SDL_free(dropCar);
        Port_Log("world: out of memory");
        return;
    }
    /* the base's quadrants and what they place (instances, effect points,
     * items: spawned only from a quadrant's contents) */
    quads = w->numQuads;
    for (i = 0; i < quads; i++) {
        const QuadContents *qc = GETP(GETP(w->quads)[i].contents);

        if (qc != NULL) {
            instances += qc->numInstances;
            fxPoints += qc->numFxPoints;
            items += qc->numItems;
        }
    }
    /* the hub: no region, no neighbours, no contents */
    quad->numGeoms = s_w.geoms;
    quad->packId = (s_w.geoms << 16) | 0xFFFF;
    SETP(quad->contents, contents);
    w->numQuads = 1;
    SETP(w->quads, quad);
    w->id = s_w.worldId;
    SETP(w->name, s_w.archive);
    /* the start: the players' cars (ports 1-4) side by side across it, the
     * first on it; the AI's spawns (other ports) never spawned */
    yaw = s_w.start[3] * (f32)(SDL_PI_D / 180.0);
    for (i = 0; i < w->numTileCars; i++) {
        TileCar *c = &GETP(w->tileCars)[i];

        port = c->port;
        side = port >= 1 && port <= 4 ? (f32)((port & 1) ? -(s32)(port / 2) : (s32)(port / 2)) * 16.0f : 0.0f;
        c->pos.x = s_w.start[0] - SDL_cosf(yaw) * side;
        c->pos.y = s_w.start[1];
        c->pos.z = s_w.start[2] + SDL_sinf(yaw) * side;
        c->rotX = 0.0f;
        c->rotY = yaw;
        c->rotZ = 0.0f;
        if (port >= 1 && port <= 4) {
            humans++;
        } else {
            dropCar[i] = 1;
            c->dynamic = 1;   /* not spawned at the start (State_GamePlay) nor by an action */
            c->mode = 0;      /* nor near a camera (AI_CarA, dynamic spawns: mode on) */
            c->spawned = 0;
        }
    }
    /* teleporters: the respawn points (only arrived at) to the start, the
     * others far below everything; none shows its effect */
    for (i = 0; i < w->numTeleporters; i++) {
        Teleporter *t = &GETP(w->teleporters)[i];
        f32 *m = (f32 *)&t->mtx;

        if (t->numDest == 0) {
            m[12] = s_w.start[0];
            m[13] = s_w.start[1] + 10.0f;
            m[14] = s_w.start[2];
        } else {
            m[13] = -100000.0f;
            teleporters++;
        }
        emitters += t->emitter != 0;
        t->emitter = 0;
        t->debris = 0;
    }
    SDL_zero(dr);
    CleanTriggers((DataPack *)worldPack, w, dropCar, &dr);
    nav = GETP(w->nav);
    gd = GETP(w->gameData);
    Port_Log("world: from the base level: %u quadrants (%u placed objects, %u effect points, %u items), %u movers, "
             "%u volumes, %u speakers, %u creatures, %u tile lights, %u vertex colour lights, %u nav points, "
             "%u AI spawns of %u tile cars, %u teleporters out of the way (%u effects), weather %s, %u triggers "
             "of %u and %u actions dropped",
             (unsigned)quads, (unsigned)instances, (unsigned)fxPoints, (unsigned)items, (unsigned)w->numTilePaths,
             (unsigned)w->numVolumes, (unsigned)w->numSpeakers, (unsigned)w->numTileCreatures, (unsigned)w->numTileLights,
             (unsigned)w->numLights, nav != NULL ? (unsigned)nav->numNodes : 0u, (unsigned)(w->numTileCars - humans),
             (unsigned)w->numTileCars, (unsigned)teleporters, (unsigned)emitters,
             gd != NULL && GETP(gd->cameraFx) != NULL ? "dropped" : "none", (unsigned)dr.triggers,
             (unsigned)(dr.triggers + ((DataPack *)worldPack)->numTriggers), (unsigned)dr.actions);
    LogCounts("triggers dropped", dr.byTrigger, s_triggerNames, TRIGGER_NUM_TYPES);
    LogCounts("actions dropped", dr.byAction, s_actionNames, ACTION_NUM_TYPES);
    SDL_free(dropCar);
    w->numTilePaths = 0;      /* movers (trains, spinning props): never spawned */
    w->numTileLights = 0;     /* LightHolder lights */
    w->numLights = 0;         /* vertex colour sequences */
    w->numVolumes = 0;        /* fog, reverb, force, creature, light volumes */
    SETP(w->volumeHolder, NULL);
    w->numSpeakers = 0;
    w->numTileCreatures = 0;
    if (nav != NULL) {
        nav->numNodes = 0;    /* the AI's routes (a NULL graph is not allowed) */
        nav->firstWayPoint = 0;
    }
    if (gd != NULL) {
        SETP(gd->cameraFx, NULL);   /* the weather kept ahead of the camera */
    }
    s_active = 1;
    Port_Log("world: %s composed over the level (%u players' starts); quadrants from %s.xgc", s_w.id, (unsigned)humans,
             s_w.archive);
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

/* HWVX_THINGLOG=N (testing, any level): every N frames of play, the things
 * alive by type and, for the movers, destructibles, motions and emitters,
 * where they are. */
void Port_WorldThingLog(void) {
    static int every = -1;
    static u32 frame;
    static const char *const names[] = { "projectile", "eventseq", "emitter", "destructible", "item", "attach",
                                         "debris", "creature", "wildcardrack", "car", "physical", "looker", "tracer",
                                         "pod", "drone", "motion", "soundseq", "radarblip" };
    u32 count[SDL_arraysize(names) + 1];
    char line[512];
    size_t at = 0;
    Thing *t;
    u32 k;

    if (every < 0) {
        const char *e = SDL_getenv("HWVX_THINGLOG");

        every = e != NULL ? SDL_atoi(e) : 0;
    }
    if (every <= 0 || ++frame % (u32)every != 0) {
        return;
    }
    SDL_zeroa(count);
    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = GETP(t->next)) {
        if (t->flags & 0x200000) {
            continue;
        }
        count[t->type < SDL_arraysize(names) ? t->type : SDL_arraysize(names)]++;
        if (t->type == THING_TYPE_PHYSICAL_OBJECT || t->type == THING_TYPE_DESTRUCTIBLE || t->type == THING_TYPE_MOTION ||
            t->type == THING_TYPE_EMITTER || t->type == THING_TYPE_SOUND_SEQ || t->type == THING_TYPE_CREATURE) {
            Port_Log("thinglog: %u %s at %.0f %.0f %.0f", (unsigned)frame, names[t->type], t->pos.x, t->pos.y, t->pos.z);
        }
    }
    for (k = 0; k <= SDL_arraysize(names) && at < sizeof(line); k++) {
        if (count[k] != 0) {
            at += (size_t)SDL_snprintf(line + at, sizeof(line) - at, " %s %u", k < SDL_arraysize(names) ? names[k] : "other",
                                       (unsigned)count[k]);
        }
    }
    Port_Log("thinglog: %u things:%s", (unsigned)frame, at ? line : " none");
}
