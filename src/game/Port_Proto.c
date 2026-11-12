/* The PS2 prototype's content in the game being played (Port_Proto.h;
 * docs/xdl-format/README.md §9.1, HWVX-X-FILE-EDITOR). Port-only file.
 *
 * The prototype (archive version 177/179) is an earlier build of the same
 * game: the same pack format as the PS2 release with its own layout (no
 * sector padding, the pointer tables before the data, other TOC offsets and
 * texture level counts: DataGamePackLoad.c, Port_PS2.c). Its packs load into
 * the release being played under its archives' names with a prefix
 * ("~HW_Cars", proto.c): dvd.c opens them from the prototype's folder, and a
 * pack loaded so refers to the prototype's shared packs (Port_ProtoRefs:
 * "~HW_share", "~HW_Cars"#2), so its texture, sound and geometry imports
 * resolve against the packs it was built with, not the release's. Nothing
 * of it is copied anywhere: the player's own copy is read as the game runs.
 *
 * Cars. Its HW_Cars holds 36: the 33 the releases have, under other numbers
 * (paired by their skins' texture names, s_cars), and three only it has:
 * Fandango (1), Sweet 16 II (12), Jester (17). Its car select's thumbnails
 * are HW_IPack interface_3's textures 19 + n. The modes (proto.c):
 *
 *   cars     the three prototype-only cars, as extra cars (ids.lock)
 *   added    all 36: the three, and the 33 others as "<name> (prototype)",
 *            beside the release's own
 *   replace  the release's 33 cars load the prototype's version under their
 *            own numbers (saves keep meaning), and the three are added
 *
 * Extra cars are ids.lock entries (Port_ProtoSyncLock), "source":
 * "prototype", whose "data" names the prototype's pack (proto://HW_Cars/
 * car_N) and whose "alias" the release's car that stands in where the
 * prototype's can't be had (online, a game without the prototype): its twin,
 * or Twin Mill for the three. A mode that leaves an entry out keeps it with
 * "active": false, its number reserved, as a mod's.
 *
 * What a prototype car needs to drive in the release (as hwvx-x port-car
 * does for its converted packs, docs/beta-cars/research/objects.md §3):
 * camera mode 1, which the release dropped, becomes the default chase camera
 * (0) with a release car's values; damage as every release car has it (20
 * hits, 75%); the turntable height (viewOffset y 2); sound records of type 2
 * become 1. Its HUD template, stat bars and AI scripts are its own.
 *
 * Worlds (mode "added"): its 48 single-player and battle levels are
 * extra tracks (ids.lock "uid://ps2proto-track-<level>", s_tracks), each
 * a level of its release twin's progression whose "world" is the
 * prototype's archive ("~worlds_\\sp_\\joyride_\\world1"). Its world packs
 * are put in the release's layout as they load (Port_ProtoWorldLoaded:
 * quadrant records, nav points, moving paths; below), and its quadrants are
 * found in the level's own archive (FileStreaming.c LoadQuadrants).
 * docs-pending/prototype-worlds.md. */
#include <SDL3/SDL.h>
#include "common.h"
#include "CarA.h"
#include "Interface.h"
#include "World.h"
#include "Nav.h"
#include "Port_Swap.h"
#include "Port_Proto.h"
#include "Port_Content.h"
#include "jsonc.h"

#define NUM_CARS 36
#define THUMB_PACK 3        /* HW_IPack interface_3: the car select */
#define THUMB_FIRST 19      /* its texture of car 0 */

/* The prototype's car n is the release's car s_cars[n] (-1: only the
 * prototype has it). Matched by the skins' texture names in both builds'
 * car packs (twinmill256v7, sooofast, at_skin, ...); every release car has
 * exactly one. */
static const s8 s_cars[NUM_CARS] = {
    0, -1, 5, 3, 4, 17, 9, 1, 2, 6, 7, 8, -1, 10, 14, 15, 13, -1,
    12, 16, 11, 18, 19, 20, 21, 22, 23, 24, 25, 29, 27, 28, 26, 30, 31, 32,
};
static const char *ProtoOnlyName(u32 n) {
    return n == 1 ? "Fandango*" : n == 12 ? "Sweet 16 II*" : n == 17 ? "Jester*" : NULL;
}

/* ---- names --------------------------------------------------------------- */

/* "~" + archive, kept for the run (DataPack_LoadFile keeps names it is given). */
static const char *Prefixed(const char *archive, size_t len) {
    static char *names[16];
    static int count;
    int i;

    for (i = 0; i < count; i++) {
        if (SDL_strlen(names[i]) == len + 1 && SDL_strncasecmp(names[i] + 1, archive, len) == 0) {
            return names[i];
        }
    }
    if (count == (int)SDL_arraysize(names) || (names[count] = SDL_malloc(len + 2)) == NULL) {
        return NULL;
    }
    names[count][0] = PORT_PROTO_PREFIX;
    SDL_memcpy(names[count] + 1, archive, len);
    names[count][len + 1] = 0;
    return names[count++];
}

void Port_ProtoRefs(DataPackRef *refs) {
    int k;

    for (k = 0; k < 2; k++) {
        char *name = refs[k].name;
        size_t n = SDL_strnlen(name, sizeof(refs[k].name));

        if (n != 0 && name[0] != PORT_PROTO_PREFIX && n + 1 < sizeof(refs[k].name)) {
            SDL_memmove(name + 1, name, n + 1);
            name[0] = PORT_PROTO_PREFIX;
        }
    }
}

static int IsCars(const char *archive) {
    const char *p, *base = archive;

    for (p = archive; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return SDL_strcasecmp(base, "HW_Cars") == 0;
}

u32 Port_ProtoReplacement(const char *archive, u32 type, u32 id, const char **source) {
    u32 n;

    *source = archive;
    if (type != 0 || Port_ProtoMode() != PORT_PROTO_REPLACE || !IsCars(archive)) {
        return id;
    }
    for (n = 0; n < NUM_CARS; n++) {
        if (s_cars[n] >= 0 && (u32)s_cars[n] == id) {
            *source = Prefixed("HW_Cars", 7);
            return *source != NULL ? n : id;
        }
    }
    return id;
}

u32 Port_ProtoLink(const char *link, u32 type, u32 id, const char **source) {
    const char *p, *slash, *us;
    char *end;
    u32 n;

    if (SDL_strncmp(link, "proto://", 8) != 0 || (slash = SDL_strchr(p = link + 8, '/')) == NULL ||
        (us = SDL_strrchr(slash, '_')) == NULL || us[1] < '0' || us[1] > '9') {
        return id;
    }
    n = (u32)SDL_strtoul(us + 1, &end, 10);
    if (*end != 0 || (*source = Prefixed(p, (size_t)(slash - p))) == NULL) {
        return id;
    }
    (void)type;
    return n;
}

/* ---- ids.lock ------------------------------------------------------------ */

static JsonNode *Str(const char *s) {
    char buf[256];
    size_t n = 0;

    buf[n++] = '"';
    for (; *s && n + 4 < sizeof(buf); s++) {
        if (*s == '"' || *s == '\\') {
            buf[n++] = '\\';
        }
        buf[n++] = *s;
    }
    buf[n++] = '"';
    buf[n] = 0;
    return Json_Parse(buf, n, NULL, 0);
}

/* Lock member `key` set to string s (NULL: removed): 1 if it changed. */
static int SetStr(JsonNode *e, const char *key, const char *s) {
    const JsonNode *v = Json_Get(e, key);

    if (s == NULL) {
        return Json_RemoveMember(e, key);
    }
    if (v != NULL && v->type == JSON_STRING && SDL_strcmp(v->string, s) == 0) {
        return 0;
    }
    Json_RemoveMember(e, key);
    Json_Append(e, Str(s), key);
    return 1;
}

static int SetBool(JsonNode *e, const char *key, int b) {
    const JsonNode *v = Json_Get(e, key);

    if (v != NULL && v->type == JSON_BOOL && v->boolean == b) {
        return 0;
    }
    Json_RemoveMember(e, key);
    Json_Append(e, Json_NewBool(b), key);
    return 1;
}

static int WriteJson(const char *path, const JsonNode *root) {
    JsonWriter w;
    char *tmp = NULL;
    int ok = 0;

    Jw_Init(&w);
    Jw_Node(&w, NULL, root);
    Jw_Raw(&w, "\n");
    if (!w.failed && SDL_asprintf(&tmp, "%s.tmp", path) > 0 && Port_WriteFileSynced(tmp, w.buf, w.len)) {
        ok = Port_ReplaceFile(tmp, path);
    }
    SDL_free(tmp);
    Jw_Free(&w);
    return ok;
}

/* Is the prototype's car n in the game, by the mode? */
static int Wanted(int mode, u32 n) {
    switch (mode) {
    case PORT_PROTO_CARS:
    case PORT_PROTO_REPLACE:
        return s_cars[n] < 0;
    case PORT_PROTO_ADDED:
        return 1;
    default:
        return 0;
    }
}

/* The prototype's levels and the release's tracks they are a version of
 * (the MCP's progression scripts: Joyride 0-4, missions 5-18, battles
 * 19-26, drag races 29-34, challenges 39-56, by the level's place in its
 * script). Its multi-player drag worlds (race*l1) have no track of their
 * own in the release; they are left out. */
static const struct {
    const char *world;      /* its archive, without the prefix */
    s16 track;
} s_tracks[] = {
    { "worlds_\\sp_\\joyride_\\world1", 0 }, { "worlds_\\sp_\\joyride_\\world2", 1 },
    { "worlds_\\sp_\\joyride_\\world3", 2 }, { "worlds_\\sp_\\joyride_\\world4", 3 },
    { "worlds_\\sp_\\joyride_\\world5", 4 },
    { "worlds_\\sp_\\miss_\\world1l1", 5 }, { "worlds_\\sp_\\miss_\\world1l2", 6 },
    { "worlds_\\sp_\\miss_\\world1l3", 7 }, { "worlds_\\sp_\\miss_\\world2l1", 8 },
    { "worlds_\\sp_\\miss_\\world2l2", 9 }, { "worlds_\\sp_\\miss_\\world2l3", 10 },
    { "worlds_\\sp_\\miss_\\world3l1", 11 }, { "worlds_\\sp_\\miss_\\world3l2", 12 },
    { "worlds_\\sp_\\miss_\\world3l3", 13 }, { "worlds_\\sp_\\miss_\\world4l1", 14 },
    { "worlds_\\sp_\\miss_\\world4l2", 15 }, { "worlds_\\sp_\\miss_\\world4l3", 16 },
    { "worlds_\\sp_\\miss_\\world5l1", 17 }, { "worlds_\\sp_\\miss_\\world5l2", 18 },
    { "worlds_\\mp_\\battle_\\bat1_l1", 19 }, { "worlds_\\mp_\\battle_\\bat2_l9", 20 },
    { "worlds_\\mp_\\battle_\\bat3_l6", 21 }, { "worlds_\\mp_\\battle_\\bat4_l1", 22 },
    { "worlds_\\mp_\\battle_\\bat5_l1", 23 }, { "worlds_\\mp_\\battle_\\bat6_l6", 24 },
    { "worlds_\\mp_\\battle_\\bat7_l1", 25 }, { "worlds_\\mp_\\battle_\\bat8_l1", 26 },
    { "worlds_\\sp_\\drag_\\race1l2", 29 }, { "worlds_\\sp_\\drag_\\race2l2", 30 },
    { "worlds_\\sp_\\drag_\\race3l2", 31 }, { "worlds_\\sp_\\drag_\\race4l2", 32 },
    { "worlds_\\sp_\\drag_\\race5l2", 33 },
    { "worlds_\\sp_\\chal1_\\ch1_l2", 39 }, { "worlds_\\sp_\\chal1_\\ch1_l3", 40 },
    { "worlds_\\sp_\\chal1_\\ch2_l1", 41 }, { "worlds_\\sp_\\chal1_\\ch1_l8", 42 },
    { "worlds_\\sp_\\chal1_\\ch1_l4", 43 }, { "worlds_\\sp_\\chal1_\\ch1_l5", 44 },
    { "worlds_\\sp_\\chal1_\\ch1_l6", 45 }, { "worlds_\\sp_\\chal1_\\ch2_l2", 46 },
    { "worlds_\\sp_\\chal1_\\ch2_l4", 47 }, { "worlds_\\sp_\\chal1_\\ch5_l1", 48 },
    { "worlds_\\sp_\\chal1_\\ch5_l2", 49 }, { "worlds_\\sp_\\chal1_\\ch1_l7", 50 },
    { "worlds_\\sp_\\chal1_\\ch5_l3", 52 }, { "worlds_\\sp_\\chal1_\\ch5_l5", 53 },
    { "worlds_\\sp_\\chal1_\\ch5_l4", 55 }, { "worlds_\\sp_\\chal1_\\ch2_l3", 56 },
};

/* The tracks of ids.lock: in mode "added" every prototype level as "<the
 * release's track> (prototype)", loading the prototype's world. */
static int SyncTracks(JsonNode *lock, int mode) {
    JsonNode *tracks = Json_ObjectMember(lock, "tracks"), *e;
    s32 next = (s32)Json_GetInt(Json_Get(lock, "next"), "tracks", 0), top;
    int changed = 0, want = mode == PORT_PROTO_ADDED;
    char id[96], buf[96];
    const char *base;
    u32 k;

    top = (s32)Port_ContentBaseCount(PORT_CONTENT_TRACK);
    for (e = tracks != NULL ? tracks->child : NULL; e != NULL; e = e->next) {
        s32 num = (s32)Json_GetInt(e, "number", -1);

        top = num >= top ? num + 1 : top;
    }
    next = next > top ? next : top;
    for (k = 0; tracks != NULL && k < SDL_arraysize(s_tracks); k++) {
        base = SDL_strrchr(s_tracks[k].world, '\\') + 1;
        SDL_snprintf(id, sizeof(id), "uid://ps2proto-track-%s", base);
        e = (JsonNode *)Json_Get(tracks, id);
        if (e == NULL) {
            if (!want) {
                continue;
            }
            e = Json_ObjectMember(tracks, id);
            Json_Append(e, Json_NewInt(next++), "number");
            changed = 1;
        }
        SDL_snprintf(buf, sizeof(buf), "base://mcp/track/%d", s_tracks[k].track);
        changed |= SetStr(e, "alias", buf);
        SDL_snprintf(buf, sizeof(buf), "%c%s", PORT_PROTO_PREFIX, s_tracks[k].world);
        changed |= SetStr(e, "world", buf);
        changed |= SetStr(e, "suffix", " (prototype)");
        changed |= SetStr(e, "source", "prototype");
        changed |= SetBool(e, "unlocked", 1);
        changed |= SetBool(e, "active", want);
    }
    if (changed) {
        JsonNode *nx = Json_ObjectMember(lock, "next");

        if (Json_GetInt(nx, "tracks", -1) < next) {
            Json_RemoveMember(nx, "tracks");
            Json_Append(nx, Json_NewInt(next), "tracks");
        }
    }
    return changed;
}

void Port_ProtoSyncLock(const char *path) {
    int mode = Port_ProtoMode(), changed = 0;
    size_t len;
    char *text = SDL_LoadFile(path, &len), err[160], id[64], buf[96];
    JsonNode *lock = text != NULL ? Json_Parse(text, len, err, sizeof(err)) : NULL, *cars, *e;
    s32 next, top;
    u32 k, n, order[NUM_CARS];

    SDL_free(text);
    if (text != NULL && (lock == NULL || lock->type != JSON_OBJECT)) {
        Port_Log("prototype: %s isn't readable; its entries aren't added to it", path);
        Json_Free(lock);
        return;
    }
    if (lock == NULL) {
        if (mode == PORT_PROTO_OFF || (lock = Json_NewObject()) == NULL) {
            return;
        }
        Json_Append(lock, Json_NewInt(1), "format_version");
    }
    cars = Json_ObjectMember(lock, "cars");
    /* new entries get numbers in the prototype's order here, past every
     * number the file has given (Port_ContentLoadLock would too, in no
     * particular order), so a mode's cars are numbered alike everywhere */
    next = (s32)Json_GetInt(Json_Get(lock, "next"), "cars", 0);
    top = (s32)Port_ContentCount(PORT_CONTENT_CAR);
    for (e = cars != NULL ? cars->child : NULL; e != NULL; e = e->next) {
        s32 num = (s32)Json_GetInt(e, "number", -1);

        top = num >= top ? num + 1 : top;
    }
    next = next > top ? next : top;
    /* the prototype-only cars first: mode "cars" numbers them 33-35 */
    for (k = 0, n = 0; k < NUM_CARS; k++) {
        if (s_cars[k] < 0) {
            order[n++] = k;
        }
    }
    for (k = 0; k < NUM_CARS; k++) {
        if (s_cars[k] >= 0) {
            order[n++] = k;
        }
    }
    for (k = 0; cars != NULL && k < NUM_CARS; k++) {
        n = order[k];
        SDL_snprintf(id, sizeof(id), "uid://ps2proto-car-%u", (unsigned)n);
        e = (JsonNode *)Json_Get(cars, id);
        if (e == NULL) {
            if (!Wanted(mode, n)) {
                continue;
            }
            e = Json_ObjectMember(cars, id);
            Json_Append(e, Json_NewInt(next++), "number");
            changed = 1;
        }
        SDL_snprintf(buf, sizeof(buf), "base://HW_Cars/car_%d", s_cars[n] >= 0 ? s_cars[n] : 0);
        changed |= SetStr(e, "alias", buf);
        SDL_snprintf(buf, sizeof(buf), "proto://HW_Cars/car_%u", (unsigned)n);
        changed |= SetStr(e, "data", buf);
        changed |= SetStr(e, "name", ProtoOnlyName(n));
        changed |= SetStr(e, "suffix", s_cars[n] >= 0 ? " (prototype)" : NULL);
        SDL_snprintf(buf, sizeof(buf), "proto://HW_IPack/interface_%d/texture/%u", THUMB_PACK,
                     (unsigned)(THUMB_FIRST + n));
        changed |= SetStr(e, "thumbnail", buf);
        changed |= SetStr(e, "source", "prototype");
        changed |= SetBool(e, "unlocked", 1);
        changed |= SetBool(e, "active", Wanted(mode, n));
    }
    changed |= SyncTracks(lock, mode);
    if (changed) {
        JsonNode *nx = Json_ObjectMember(lock, "next");

        if (Json_GetInt(nx, "cars", -1) < next) {
            Json_RemoveMember(nx, "cars");
            Json_Append(nx, Json_NewInt(next), "cars");
        }
        if (WriteJson(path, lock)) {
            Port_Log("prototype: %s brought up to date for mode %s", path, Port_ProtoModeKey(mode));
        } else {
            Port_Log("prototype: can't write %s", path);
        }
    }
    Json_Free(lock);
}

/* ---- pictures ------------------------------------------------------------ */

/* The prototype's car select pack, loaded once for its thumbnails: outside
 * the streaming pools, so no release of a pool frees it. */
static DataPack s_thumbs;
static int s_thumbsLoaded, s_thumbsWanted;

void Port_ProtoInit(void) {
    u32 i;
    const char *name, *suffix, *thumb;

    if (s_thumbsLoaded || Port_ProtoMode() == PORT_PROTO_OFF) {
        return;
    }
    for (i = Port_ContentBaseCount(PORT_CONTENT_CAR); i < Port_ContentCount(PORT_CONTENT_CAR); i++) {
        if (Port_ContentLook(PORT_CONTENT_CAR, i, &name, &suffix, &thumb) == 1 && thumb != NULL &&
            SDL_strncmp(thumb, "proto://HW_IPack/", 17) == 0) {
            break;
        }
    }
    s_thumbsWanted = i < Port_ContentCount(PORT_CONTENT_CAR);
}

/* Loaded when a select first asks for a picture, not at Port_ProtoInit: the
 * renderer's textures are set up after the game settings start, and the
 * sound system too. Never while the loading thread has the pack file. */
static void LoadThumbs(void) {
    extern s8 g_bSoundLoadingEnabled;
    extern s8 g_bLoadingThreadRunning;
    s8 sounds;
    s32 ok;

    if (s_thumbsLoaded || !s_thumbsWanted || g_bLoadingThreadRunning) {
        return;
    }
    s_thumbsLoaded = 1;
    /* its pictures only: no sounds (and this runs before the sound system starts) */
    sounds = g_bSoundLoadingEnabled;
    DataPack_EnableLoadingSounds(0);
    ok = DataPack_LoadFile((char *)Prefixed("HW_IPack", 8), HND(&s_thumbs), 1, THUMB_PACK);
    DataPack_EnableLoadingSounds(sounds);
    if (!ok || GETP(s_thumbs.base) == NULL) {
        Port_Log("prototype: its car select's pictures can't be loaded");
        SDL_zero(s_thumbs);
        return;
    }
    DataPack_RefreshPtrs(&s_thumbs);
    Port_Log("prototype: its car select's pictures loaded (%u textures)", (unsigned)s_thumbs.numTextures);
}

s32 Port_ProtoTexture(const char *archive, const char *rest) {
    char want[32];
    u32 tex;
    char *end;

    LoadThumbs();
    SDL_snprintf(want, sizeof(want), "interface_%d/texture/", THUMB_PACK);
    if (GETP(s_thumbs.base) == NULL || SDL_strcasecmp(archive + 1, "HW_IPack") != 0 ||
        SDL_strncmp(rest, want, SDL_strlen(want)) != 0) {
        return -1;
    }
    tex = (u32)SDL_strtoul(rest + SDL_strlen(want), &end, 10);
    if (*end != 0 || tex >= s_thumbs.numTextures || GETP(s_thumbs.textureIDs) == NULL) {
        return -1;
    }
    return GETP(s_thumbs.textureIDs)[tex];
}

/* ---- a prototype car in the release --------------------------------------- */

static void *HP(u32 h) {
    return h != 0 ? Port_HandleToPtr(h) : NULL;
}

/* The release's default chase camera, as its cars have it (CameraManager.c
 * Camera_UpdatePosition_Default; objects.md §3.1). */
static const f32 s_chase[6] = { 75.0f, 25.0f, 3.6f, 40.0f, 20.0f, 0.235f };

/* The prototype's HUD (its own, in each car pack: CarAInfo.hud layouts)
 * has a text object of role 19 reading "X: 00.0 |Y: 00.0 |Z: 00.0": the
 * car's position, which the prototype fills in (it shows live in the
 * prototype as it runs). The release's code never sets role 19's text
 * (InterfaceHudInfo.message19), so it would show the zeros. By default it is
 * blanked; HWVX_PROTO_DEBUGHUD=1 (or sources.cfg's proto_debughud=1) keeps
 * it, filled in as the prototype does (Port_ProtoHudInfo). Such a car's HUD
 * is marked in CarAHud 0x14 (0 in every car, never read). */
#define HUD_MARK 0x50524F54u    /* "PROT" */

static int DebugHud(void) {
    static int on = -1;

    if (on < 0) {
        const char *env = SDL_getenv("HWVX_PROTO_DEBUGHUD");
        char *v = env != NULL ? NULL : Port_ProtoSetting("proto_debughud");

        on = env != NULL ? SDL_atoi(env) != 0 : v != NULL && SDL_atoi(v) != 0;
        SDL_free(v);
    }
    return on;
}

static void FixHud(u8 *car) {
    static const u32 layouts[3] = { 0x10, 0x18, 0x1C };
    u8 *hud = HP(*(u32 *)(car + 0x84));
    u32 l, i, n;

    for (l = 0; hud != NULL && l < 3; l++) {
        u8 *h = HP(*(u32 *)(hud + layouts[l])), *objs;

        if (h == NULL || (objs = HP(*(u32 *)(h + 0x10))) == NULL) {
            continue;
        }
        n = *(u32 *)(h + 0xC);
        for (i = 0; i < n && i < 0x400; i++) {
            IfaceObjectDef *def = (IfaceObjectDef *)(objs + i * sizeof(IfaceObjectDef));
            IfaceText *text = def->type == IFACE_DEF_TEXT ? GETP(def->sub) : NULL;
            u32 *strings = text != NULL ? (u32 *)GETP(text->strings) : NULL;
            char *str = strings != NULL ? HP(strings[0]) : NULL;

            if (def->role == 19 && str != NULL && str[0] == 'X' && str[1] == ':') {
                if (DebugHud()) {
                    *(u32 *)(hud + 0x14) = HUD_MARK;
                } else {
                    str[0] = 0;
                }
            }
        }
    }
}

void Port_ProtoHudInfo(CarA *car, InterfaceHudInfo *info) {
    /* a string table of one: the slot, then the text */
    static struct {
        u32 slot;
        char text[64];
    } *s_readout;
    CarAInfo *ci = GETP(car->vehicleIface);
    CarAHud *hud = ci != NULL ? GETP(ci->hud) : NULL;

    if (hud == NULL || *(u32 *)hud->pad14 != HUD_MARK) {
        return;
    }
    if (s_readout == NULL && (s_readout = Allocate(sizeof(*s_readout), 0, "")) == NULL) {
        return;
    }
    SDL_snprintf(s_readout->text, sizeof(s_readout->text), "X: %.1f |Y: %.1f |Z: %.1f", car->base.pos.x,
                 car->base.pos.y, car->base.pos.z);
    s_readout->slot = HND(s_readout->text);
    info->message19.h = HND(&s_readout->slot);
}

static void FixCar(DataPack *pack, u8 *car) {
    u8 *engine = HP(*(u32 *)(car + 0x80));
    u32 l, i, slot;

    if (engine != NULL) {
        for (l = 0; l < 2; l++) {
            u32 count = *(u32 *)(engine + 0x80 + l * 8);
            u8 *cams = HP(*(u32 *)(engine + 0x84 + l * 8));

            for (i = 0; cams != NULL && i < count && i < 16; i++) {
                u8 *params = HP(*(u32 *)(cams + i * 0xC + 4));

                if (cams[i * 0xC] == 1 && params != NULL) {
                    cams[i * 0xC] = 0;
                    SDL_memcpy(params, s_chase, sizeof(s_chase));
                }
            }
        }
    }
    if (car[0x10] == 99) {
        car[0x10] = 75;         /* damagePercent */
        *(u32 *)(car + 0x18) = 20;
    }
    /* CarAInfo 0x54: the release's shadow volume (edges, Render_DrawShadows:
     * groups flagged 0x4000, which RenderGroup skips), the prototype's a
     * closed hull as a plain lit geometry (docs/beta-cars/research/
     * geometry.md §1.7, HWVX-X-FILE-EDITOR). Drawn in a race (CarA_Draw,
     * state 5) it covered the car in black. Left out: the car has no
     * shadow volume. */
    *(u32 *)(car + 0x54) = 0;
    FixHud(car);
    if (*(f32 *)(car + 4) == 0.0f) {
        *(f32 *)(car + 4) = 2.0f;   /* viewOffset y: the turntable */
    }
    for (i = 0; i < pack->numSoundRelocs && GETP(pack->soundRelocs) != NULL; i++) {
        slot = GETP(pack->soundRelocs)[i * 2];
        if (slot >= 0x14 && slot - 0x14 < pack->dataSize && GETP(pack->base)[slot - 0x14] == 2) {
            GETP(pack->base)[slot - 0x14] = 1;
        }
    }
}

void Port_ProtoPackLoaded(DataPack *pack, const char *name, u32 type, u32 id) {
    u8 *base = GETP(pack->base), *tab;
    u32 i;

    (void)name;
    (void)id;
    if (!pack->portProto || type != 0 || base == NULL || (tab = HP(*(u32 *)(base + 4))) == NULL) {
        return;
    }
    for (i = 0; i < *(u32 *)base && i < 4; i++) {
        u8 *car = HP(*(u32 *)(tab + 8 * i));

        if (car != NULL) {
            FixCar(pack, car);
        }
    }
}

/* ---- worlds -------------------------------------------------------------- */

/* The prototype's world packs (type 4) have the release's World (the same
 * offsets up to gameData, 0x9C), but:
 *
 *  - its quadrant records are 0x40 bytes, the release's 0x50: the same up
 *    to 0x38 (id, numGeoms, geoms, bsp, neighbours, pack header and data,
 *    contents, loaded); the release adds packId (0x38), fileOffset and size,
 *    which its LoadQuadrants fills from the TOC. The prototype finds a
 *    quadrant's pack each time it loads it, by the TOC entry whose id is
 *    the record's id (0x00), with no geometry count in its high half
 *    (SLUS_001.00 DataPack_LoadQuadPack). So the records are copied into
 *    a table of the release's, with packId = id (FileStreaming.c's
 *    LoadQuadrants matches a prototype archive by that alone).
 *  - its quadrant, world texture and collision packs are in the level's own
 *    archive (World 0x7C, 0x84, 0x8C name it, with the release's folders),
 *    so the names get the prefix that makes them the prototype's.
 *  - its nav points (NavData.nodes, world 0x78) are 0x70 bytes: position,
 *    link costs (0x10) and links (0x20) four each, the links again with the
 *    way forward first (0x30), radius (0x40), index (0x44), two floats
 *    (0x48, 0x4C), the chance byte (0x51), A* state (0x54-0x60), 0x62 the
 *    type, 0x64 the next way point (SLUS_001.00 NavHolder_*, and the data).
 *    The links run both ways; the release's points have the A* links
 *    (prev, with costs) and the ways on (next) apart. Converted: prev and
 *    costs from 0x10/0x20, next all of 0x30 but its last when it has more
 *    than one (the way back), speed 0 (no limit), enabled.
 *  - its moving paths (TilePath, world 0x14) are 0x14 bytes: anim, speed,
 *    record and thing as the release's 0x20, without enabled (every path is
 *    spawned, StateGamePlay_Init) and draw distance.
 *
 * Kept with the world pack (PortGeometry_Keep, freed with it). */
static char *PrefixedName(DataPack *pack, const char *name) {
    size_t n = SDL_strlen(name);
    char *p = name[0] == PORT_PROTO_PREFIX ? NULL : Allocate((u32)n + 2, 0, "");

    if (p == NULL) {
        return NULL;
    }
    p[0] = PORT_PROTO_PREFIX;
    SDL_memcpy(p + 1, name, n + 1);
    PortGeometry_Keep(pack, p);
    return p;
}

#define PROTO_NAV_SIZE 0x70
#define PROTO_NAV_NONE 0xFFFF8000u

/* Is [p, p + count * size) inside the pack's data block? The prototype's world
 * arrays are read from there before they are converted. */
static int InPack(const DataPack *pack, const void *p, u32 count, u32 size, const char *what) {
    const u8 *lo = GETP(pack->base), *q = p;
    u64 bytes = (u64)count * size;

    if (lo != NULL && q != NULL && q >= lo && (u64)(q - lo) <= pack->dataSize && bytes <= pack->dataSize - (u64)(q - lo)) {
        return 1;
    }
    Port_Log("prototype: world's %s (%u x 0x%X bytes) not inside its pack, skipped", what, count, size);
    return 0;
}

/* The way point chain (NAV_TYPE_WAYPOINT nodes by nextWayPoint, Player.c, AI_CarA.c)
 * is followed without bounds: a next out of range is none (-1, and then the node is no
 * way point), and a chain that comes back on itself is cut where it does. */
static void FixWayPoints(NavPoint *pts, u32 n) {
    u8 *state = Allocate(n, 0, "");   /* 0 not seen, 1 on this walk, 2 done */
    u32 i, j, cut = 0;

    for (i = 0; i < n; i++) {
        if ((u32)pts[i].nextWayPoint >= n) {
            pts[i].nextWayPoint = -1;
        }
        if (pts[i].type == NAV_TYPE_WAYPOINT && pts[i].nextWayPoint == -1) {
            pts[i].type = 0;
        }
    }
    if (state == NULL) {
        return;
    }
    SDL_memset(state, 0, n);
    for (i = 0; i < n; i++) {
        for (j = i; pts[j].type == NAV_TYPE_WAYPOINT && state[j] == 0;) {
            u32 next = (u32)pts[j].nextWayPoint;

            state[j] = 1;
            if (state[next] == 1) {         /* back on this walk: a cycle */
                pts[j].type = 0;
                pts[j].nextWayPoint = -1;
                cut++;
                break;
            }
            j = next;
        }
        for (j = i; state[j] == 1;) {
            state[j] = 2;
            if (pts[j].type != NAV_TYPE_WAYPOINT) {
                break;
            }
            j = (u32)pts[j].nextWayPoint;
        }
    }
    Deallocate(state);
    if (cut != 0) {
        Port_Log("prototype: world's way point chain cut at %u cycle(s)", cut);
    }
}

static void ConvertNav(DataPack *pack, World *w) {
    NavData *nd = GETP(w->nav);
    const u8 *old;
    NavPoint *pts;
    s32 *links;
    f32 *costs;
    u32 i, k, n, total = 0;

    if (nd == NULL || nd->numNodes == 0 || nd->numNodes > 0x4000 || (old = (const u8 *)GETP(nd->nodes)) == NULL ||
        !InPack(pack, old, nd->numNodes, PROTO_NAV_SIZE, "nav points")) {
        return;
    }
    n = nd->numNodes;
    pts = Allocate(n * sizeof(NavPoint) + n * 12 * sizeof(s32), 0, "");
    if (pts == NULL) {
        return;
    }
    SDL_memset(pts, 0, n * sizeof(NavPoint));
    links = (s32 *)(pts + n);
    for (i = 0; i < n; i++) {
        const u8 *o = old + i * PROTO_NAV_SIZE;
        const u32 *ol = (const u32 *)(o + 0x20), *on = (const u32 *)(o + 0x30);
        const f32 *oc = (const f32 *)(o + 0x10);
        NavPoint *p = &pts[i];
        u32 numNext = 0;

        SDL_memcpy(&p->pos, o, 16);
        costs = (f32 *)&links[total];
        SETP(p->prevCosts, costs);
        SETP(p->prev, &links[total + 4]);
        SETP(p->next, &links[total + 8]);
        for (k = 0; k < 4; k++) {
            if (ol[k] != PROTO_NAV_NONE && ol[k] < n) {
                costs[p->numPrev] = oc[k];
                GETP(p->prev)[p->numPrev++] = (s32)ol[k];
            }
            if (on[k] != PROTO_NAV_NONE && on[k] < n) {
                numNext++;
            }
        }
        for (k = 0; k < 4 && p->numNext < (numNext > 1 ? numNext - 1 : numNext); k++) {
            if (on[k] != PROTO_NAV_NONE && on[k] < n) {
                GETP(p->next)[p->numNext++] = (s32)on[k];
            }
        }
        p->radius = *(const f32 *)(o + 0x40);
        p->index = (s32)i;
        p->chance = o[0x51];
        p->enabled = 1;
        p->type = o[0x62];
        p->nextWayPoint = *(const s32 *)(o + 0x64);
        total += 12;
    }
    FixWayPoints(pts, n);
    if ((u32)nd->firstWayPoint >= n) {
        nd->firstWayPoint = 0;   /* Player.c reads it before it checks */
    }
    SETP(nd->nodes, pts);
    PortGeometry_Keep(pack, pts);
}

void Port_ProtoWorldLoaded(DataPack *pack) {
    World *w;
    WorldQuad *quads;
    const u8 *old;
    char *p;
    u32 i;

    if (pack == NULL || !pack->portProto || (w = SLOT_LOAD(World *, pack)) == NULL) {
        return;
    }
    if ((p = PrefixedName(pack, GETP(w->collisionPackName))) != NULL) {
        SETP(w->collisionPackName, p);
    }
    if ((p = PrefixedName(pack, GETP(w->quadPackName))) != NULL) {
        SETP(w->quadPackName, p);
    }
    if ((p = PrefixedName(pack, GETP(w->name))) != NULL) {
        SETP(w->name, p);
    }
    old = (const u8 *)GETP(w->quads);
    if (old == NULL || w->numQuads == 0 || w->numQuads > 0x400 || !InPack(pack, old, w->numQuads, 0x40, "quadrants") ||
        (quads = Allocate(w->numQuads * sizeof(WorldQuad), 0, "")) == NULL) {
        return;
    }
    SDL_memset(quads, 0, w->numQuads * sizeof(WorldQuad));
    for (i = 0; i < w->numQuads; i++) {
        SDL_memcpy(&quads[i], old + i * 0x40, offsetof(WorldQuad, packId));
        quads[i].packId = quads[i].id;
    }
    SETP(w->quads, quads);
    PortGeometry_Keep(pack, quads);
    ConvertNav(pack, w);
    if (w->numTilePaths != 0 && GETP(w->tilePaths) != NULL && w->numTilePaths < 0x400 &&
        InPack(pack, GETP(w->tilePaths), w->numTilePaths, 0x14, "moving paths")) {
        const u8 *op = (const u8 *)GETP(w->tilePaths);
        TilePath *paths = Allocate(w->numTilePaths * sizeof(TilePath), 0, "");

        if (paths != NULL) {
            SDL_memset(paths, 0, w->numTilePaths * sizeof(TilePath));
            for (i = 0; i < w->numTilePaths; i++) {
                SDL_memcpy(&paths[i], op + i * 0x14, 0x14);
                paths[i].thing.h = 0;
                paths[i].enabled = 1;
            }
            SETP(w->tilePaths, paths);
            PortGeometry_Keep(pack, paths);
        }
    }
    Port_Log("prototype: world %s, %u quadrants", GETP(w->name), (unsigned)w->numQuads);
}
