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
 * Worlds, tracks and the prototype's audio are not imported yet: its world
 * packs have another layout (0x40-byte quadrant records against the
 * release's 0x50; its quadrants live in each level's archive, the release's
 * in w*_geo), and its music is in another format (IMPM modules). See
 * docs-pending/prototype-import.md. */
#include <SDL3/SDL.h>
#include "common.h"
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

    SDL_snprintf(buf, sizeof(buf), "\"%s\"", s);
    return Json_Parse(buf, SDL_strlen(buf), NULL, 0);
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
