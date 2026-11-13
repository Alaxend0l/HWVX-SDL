/* Mixed builds (Port_Mix.h): the recipe, the releases' folders and their
 * files. The packs themselves are mixed in game/Port_Mix.c. */
#include "port_internal.h"
#include "disc.h"
#include "jsonc.h"
#include "Port_Mix.h"
#include "xdl.h"
#include <stdio.h>
#include "mix_recipe_ultimate.h"    /* generated from recipes/ultimate.jsonc (CMakeLists.txt) */

static const char *const s_relKeys[PORT_DATA_COUNT] = { "gc", "pc", "ps2" };

typedef struct MixRule {
    char *match;
    int kind;                       /* > 0: releases in from[]; PORT_MIX_FROM_BASE, PORT_MIX_FROM_BEST */
    int from[PORT_DATA_COUNT];
} MixRule;

typedef struct MixRules {
    MixRule *rule;
    int count;
} MixRules;

static struct {
    int on, building, lookGC;
    int iface;                      /* the interface art's set: PORT_DATA_GC (yellow) or PORT_DATA_PS2 (silver) */
    char name[64];
    char cacheName[80];
    u32 stamp;
    char *root[PORT_DATA_COUNT];    /* NULL: not imported, or not in the recipe */
    int order[PORT_DATA_COUNT];     /* the recipe's "releases" that are there, best first */
    int numOrder;
    MixRules base, assets, files;
    struct {
        SDL_IOStream *io;           /* the file Port_MixRead read last, kept open */
        char *name;
    } donor[PORT_DATA_COUNT];
    SDL_IOStream *manifest;
} s_mix;

int Port_MixActive(void) {
    return s_mix.on;
}

const char *Port_MixName(void) {
    return s_mix.name;
}

int Port_MixBuilding(void) {
    return s_mix.on && s_mix.building;
}

int Port_MixLookGC(void) {
    return s_mix.lookGC;
}

int Port_MixInterface(void) {
    return s_mix.iface;
}

int Port_MixProtoReplace(void) {
    return !s_mix.on && Port_ProtoMode() == PORT_PROTO_REPLACE;
}

const char *Port_MixRoot(int rel) {
    if (rel == PORT_MIX_PROTO) {
        return Port_MixProtoReplace() ? Port_ProtoRoot() : NULL;
    }
    return s_mix.on && rel >= 0 && rel < PORT_DATA_COUNT ? s_mix.root[rel] : NULL;
}

const char *Port_MixCacheName(void) {
    return s_mix.cacheName;
}

u32 Port_MixStamp(void) {
    return s_mix.stamp;
}

/* ---- the recipe --------------------------------------------------------------- */

static int RelOf(const char *s) {
    int i;

    for (i = 0; s != NULL && i < PORT_DATA_COUNT; i++) {
        if (SDL_strcasecmp(s, s_relKeys[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* `s` against `pat` (* any run, ? one character), without case. */
static int Glob(const char *pat, const char *s) {
    for (; *pat; pat++, s++) {
        if (*pat == '*') {
            while (pat[1] == '*') {
                pat++;
            }
            if (pat[1] == 0) {
                return 1;
            }
            for (; *s; s++) {
                if (Glob(pat + 1, s)) {
                    return 1;
                }
            }
            return Glob(pat + 1, s);
        }
        if (*s == 0 || (*pat != '?' && SDL_tolower((unsigned char)*pat) != SDL_tolower((unsigned char)*s))) {
            return 0;
        }
    }
    return *s == 0;
}

/* A rule list: [{ "match": glob, "from": release | [releases] | "base" | "best" | "interface" }]. */
static int ReadRules(const JsonNode *arr, MixRules *out, const char *what, char *err, u32 errSize) {
    const JsonNode *n;
    int i = 0;

    if (arr == NULL) {
        return 1;
    }
    if (arr->type != JSON_ARRAY) {
        SDL_snprintf(err, errSize, "\"%s\" is not a list", what);
        return 0;
    }
    out->rule = SDL_calloc((size_t)arr->count + 1, sizeof(MixRule));
    if (out->rule == NULL) {
        SDL_snprintf(err, errSize, "out of memory");
        return 0;
    }
    for (n = arr->child; n != NULL; n = n->next, i++) {
        MixRule *r = &out->rule[i];
        const JsonNode *from = Json_Get(n, "from"), *e;
        const char *match = Json_GetString(n, "match", NULL);

        if (match == NULL || from == NULL) {
            SDL_snprintf(err, errSize, "\"%s\" rule %d needs \"match\" and \"from\"", what, i + 1);
            return 0;
        }
        r->match = SDL_strdup(match);
        if (from->type == JSON_STRING && SDL_strcasecmp(from->string, "base") == 0) {
            r->kind = PORT_MIX_FROM_BASE;
        } else if (from->type == JSON_STRING && SDL_strcasecmp(from->string, "best") == 0) {
            r->kind = PORT_MIX_FROM_BEST;
        } else if (from->type == JSON_STRING && SDL_strcasecmp(from->string, "interface") == 0) {
            r->kind = PORT_MIX_FROM_IFACE;
        } else {
            for (e = from->type == JSON_ARRAY ? from->child : from; e != NULL;
                 e = from->type == JSON_ARRAY ? e->next : NULL) {
                int rel = e->type == JSON_STRING ? RelOf(e->string) : -1;

                if (rel < 0 || r->kind >= PORT_DATA_COUNT) {
                    SDL_snprintf(err, errSize, "\"%s\" rule %d: \"from\" is a release (gc, pc, ps2), a list of "
                                 "them, \"base\", \"best\" or \"interface\"", what, i + 1);
                    return 0;
                }
                r->from[r->kind++] = rel;
            }
        }
    }
    out->count = i;
    return 1;
}

static const MixRule *FindRule(const MixRules *rules, const char *s) {
    int i;

    for (i = 0; i < rules->count; i++) {
        if (Glob(rules->rule[i].match, s)) {
            return &rules->rule[i];
        }
    }
    return NULL;
}

/* The releases of `rule` that are there, into order[]; all of them, best
 * first, without a rule. */
static int Available(const MixRule *rule, int order[PORT_DATA_COUNT]) {
    int i, n = 0;

    if (rule == NULL || rule->kind <= 0) {
        for (i = 0; i < s_mix.numOrder; i++) {
            order[n++] = s_mix.order[i];
        }
        return n;
    }
    for (i = 0; i < rule->kind; i++) {
        if (s_mix.root[rule->from[i]] != NULL) {
            order[n++] = rule->from[i];
        }
    }
    return n;
}

static const char *Stem(const char *name, int *len) {
    const char *base = name, *p, *dot = NULL;

    for (p = name; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
            dot = NULL;
        } else if (*p == '.') {
            dot = p;
        }
    }
    *len = (int)((dot != NULL ? dot : p) - base);
    return base;
}

int Port_MixCandidate(const char *name, int k) {
    int order[PORT_DATA_COUNT], n, len, i, j;
    size_t l = SDL_strlen(name);
    const MixRule *rule;

    if (!s_mix.on || k < 0) {
        return -1;
    }
    if (l >= 4 && SDL_strcasecmp(name + l - 4, ".xgc") == 0) {
        char stem[96];
        const char *s = Stem(name, &len);

        SDL_snprintf(stem, sizeof(stem), "%.*s", len, s);
        rule = FindRule(&s_mix.base, stem);
    } else {
        while (*name == '/' || *name == '\\') {
            name++;
        }
        rule = FindRule(&s_mix.files, name);
    }
    n = Available(rule, order);
    /* then the others: a file only they have comes from them */
    for (i = 0; i < s_mix.numOrder; i++) {
        for (j = 0; j < n && order[j] != s_mix.order[i]; j++) {
        }
        if (j == n) {
            order[n++] = s_mix.order[i];
        }
    }
    return k < n ? order[k] : -1;
}

int Port_MixAssetRule(const char *id, int order[PORT_MIX_RELS]) {
    const MixRule *rule = s_mix.on ? FindRule(&s_mix.assets, id) : NULL;

    if (Port_MixProtoReplace()) {
        order[0] = PORT_MIX_PROTO;      /* the prototype's copy wherever it pairs */
        return 1;
    }
    if (rule == NULL) {
        return PORT_MIX_FROM_BASE;
    }
    return rule->kind <= 0 ? rule->kind : Available(rule, order);
}

/* ---- the releases ---------------------------------------------------------------- */

/* "key=value" of sources.cfg in the save folder (the launcher's file), or NULL. */
static char *SourceLine(const char *key) {
    char *pref = Port_PrefPath(), *path = NULL, *text, *line, *save = NULL, *found = NULL;
    size_t kl = SDL_strlen(key);

    if (pref == NULL || SDL_asprintf(&path, "%ssources.cfg", pref) < 0) {
        SDL_free(pref);
        return NULL;
    }
    SDL_free(pref);
    text = SDL_LoadFile(path, NULL);
    SDL_free(path);
    if (text == NULL) {
        return NULL;
    }
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strncmp(line, key, kl) == 0 && line[kl] == '=' && line[kl + 1] != 0) {
            found = SDL_strdup(line + kl + 1);
            break;
        }
    }
    SDL_free(text);
    return found;
}

int Port_MixInterfaceSetting(const char *value, int log) {
    size_t n;

    while (value != NULL && SDL_isspace((unsigned char)*value)) {
        value++;
    }
    n = value != NULL ? SDL_strlen(value) : 0;
    while (n > 0 && SDL_isspace((unsigned char)value[n - 1])) {
        n--;
    }
    if (n == 3 && SDL_strncasecmp(value, "ps2", 3) == 0) {
        return PORT_DATA_PS2;
    }
    if (log && !(n == 2 && SDL_strncasecmp(value, "gc", 2) == 0)) {
        Port_Log("mix: sources.cfg interface=%.*s is neither gc nor ps2: the yellow (GameCube) set", (int)n,
                 value != NULL ? value : "");
    }
    return PORT_DATA_GC;
}

static char *CacheFolder(const char *cacheName);

static u32 Fnv(u32 h, const void *p, size_t n) {
    const u8 *b = p;

    while (n--) {
        h = (h ^ *b++) * 16777619u;
    }
    return h;
}

/* The stamp of a release's archives: each one's path, size and time, in any
 * order (a sum of their hashes); a disc image's own size and time. */
typedef struct StampScan {
    const char *ext;
    size_t rootLen;
    u32 sum, count;
} StampScan;

static SDL_EnumerationResult SDLCALL StampArchives(void *user, const char *dir, const char *name) {
    StampScan *st = user;
    char *path = NULL;
    SDL_PathInfo info;
    size_t n = SDL_strlen(name);

    if (name[0] == '.' || SDL_asprintf(&path, "%s%s", dir, name) < 0) {
        return SDL_ENUM_CONTINUE;
    }
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        char *sub = NULL;

        if (SDL_asprintf(&sub, "%s/", path) > 0) {
            SDL_EnumerateDirectory(sub, StampArchives, st);
            SDL_free(sub);
        }
    } else if (info.type == SDL_PATHTYPE_FILE && n > 4 && SDL_strcasecmp(name + n - 4, st->ext) == 0) {
        u32 h = Fnv(2166136261u, path + st->rootLen, SDL_strlen(path + st->rootLen));

        h = Fnv(h, &info.size, sizeof(info.size));
        st->sum += Fnv(h, &info.modify_time, sizeof(info.modify_time));
        st->count++;
    }
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static u32 StampRelease(u32 stamp, int rel) {
    static const char *const exts[PORT_DATA_COUNT] = { ".xgc", ".xpc", ".xps" };
    StampScan st;
    SDL_PathInfo info;
    char *dir = NULL;

    SDL_zero(st);
    if (s_mix.root[rel] == NULL || !SDL_GetPathInfo(s_mix.root[rel], &info)) {
        return stamp;
    }
    if (info.type != SDL_PATHTYPE_DIRECTORY) {     /* a disc image */
        stamp = Fnv(stamp, &info.size, sizeof(info.size));
        return Fnv(stamp, &info.modify_time, sizeof(info.modify_time));
    }
    if (SDL_asprintf(&dir, "%s/", s_mix.root[rel]) > 0) {
        st.ext = exts[rel];
        st.rootLen = SDL_strlen(dir);
        SDL_EnumerateDirectory(dir, StampArchives, &st);
        SDL_free(dir);
    }
    stamp = Fnv(stamp, &st.count, sizeof(st.count));
    return Fnv(stamp, &st.sum, sizeof(st.sum));
}

int Port_MixSetup(const char *nameOrPath, int building, Port_DataCheck check, char *err, u32 errSize) {
    static const char *const env[PORT_DATA_COUNT] = { "HWVX_MIX_GC", "HWVX_MIX_PC", "HWVX_MIX_PS2" };
    char perr[160] = "";
    char *text = NULL;
    size_t len = 0;
    JsonNode *root;
    const JsonNode *rels, *e;
    int inRecipe[PORT_DATA_COUNT] = { 0 }, i;
    u32 stamp = 2166136261u;
    int builtIn = SDL_strcasecmp(nameOrPath, "ultimate") == 0;

    if (builtIn) {
        text = SDL_malloc(sizeof(g_mixRecipeUltimate));
        if (text != NULL) {
            len = sizeof(g_mixRecipeUltimate);
            SDL_memcpy(text, g_mixRecipeUltimate, len);
        }
    } else {
        text = SDL_LoadFile(nameOrPath, &len);
    }
    if (text == NULL) {
        SDL_snprintf(err, errSize, "the recipe %s can't be read", nameOrPath);
        return 0;
    }
    root = Json_Parse(text, len, perr, sizeof(perr));
    stamp = Fnv(stamp, text, len);
    SDL_free(text);
    if (root == NULL || root->type != JSON_OBJECT) {
        SDL_snprintf(err, errSize, "the recipe %s: %s", nameOrPath, perr[0] ? perr : "not an object");
        Json_Free(root);
        return 0;
    }
    SDL_zero(s_mix);
    SDL_strlcpy(s_mix.name, Json_GetString(root, "name", "mix"), sizeof(s_mix.name));
    for (i = 0; s_mix.name[i]; i++) {   /* it names a folder */
        if (!SDL_isalnum((unsigned char)s_mix.name[i]) && s_mix.name[i] != '-' && s_mix.name[i] != '_') {
            s_mix.name[i] = '_';
        }
    }
    s_mix.lookGC = SDL_strcasecmp(Json_GetString(root, "look", "gc"), "pc") != 0;
    {
        /* the interface art's set; the built-in recipe's is the launcher's choice (sources.cfg "interface=") */
        char *chosen = builtIn ? SourceLine("interface") : NULL;
        const char *set = Json_GetString(root, "interface", "gc");

        if (chosen != NULL) {   /* the launcher's choice: read as the launcher reads it */
            s_mix.iface = Port_MixInterfaceSetting(chosen, 1);
        } else {
            s_mix.iface = SDL_strcasecmp(set, "ps2") == 0 ? PORT_DATA_PS2 : SDL_strcasecmp(set, "gc") == 0 ? PORT_DATA_GC : -1;
        }
        SDL_free(chosen);
        if (s_mix.iface < 0) {
            SDL_snprintf(err, errSize, "the recipe %s: \"interface\" is \"gc\" (yellow) or \"ps2\" (silver)", nameOrPath);
            Json_Free(root);
            return 0;
        }
        stamp = Fnv(stamp, &s_mix.iface, sizeof(s_mix.iface));
    }
    rels = Json_Get(root, "releases");
    if (rels != NULL && rels->type == JSON_ARRAY) {
        for (e = rels->child; e != NULL; e = e->next) {
            int rel = e->type == JSON_STRING ? RelOf(e->string) : -1;

            if (rel < 0) {
                SDL_snprintf(err, errSize, "the recipe %s: \"releases\" lists gc, pc and ps2", nameOrPath);
                Json_Free(root);
                return 0;
            }
            if (!inRecipe[rel]) {
                inRecipe[rel] = 1;
                s_mix.order[s_mix.numOrder++] = rel;
            }
        }
    } else {
        for (i = 0; i < PORT_DATA_COUNT; i++) {
            inRecipe[i] = 1;
            s_mix.order[s_mix.numOrder++] = i;
        }
    }
    if (!ReadRules(Json_Get(root, "base"), &s_mix.base, "base", err, errSize) ||
        !ReadRules(Json_Get(root, "assets"), &s_mix.assets, "assets", err, errSize) ||
        !ReadRules(Json_Get(root, "files"), &s_mix.files, "files", err, errSize)) {
        Json_Free(root);
        return 0;
    }
    Json_Free(root);
    /* the folders: the GameCube's last, since a disc image stays open (disc.c has one) */
    for (i = PORT_DATA_COUNT - 1; i >= 0; i--) {
        const char *v = SDL_getenv(env[i]);
        char *path = v != NULL && *v ? SDL_strdup(v) : SourceLine(s_relKeys[i]);

        if (inRecipe[i] && path != NULL) {
            s_mix.root[i] = check(path);
            if (s_mix.root[i] == NULL) {
                Port_Log("mix: the %s release is not at %s, left out", s_relKeys[i], path);
            } else if (i != PORT_DATA_GC && Disc_IsOpen()) {
                Port_Log("mix: the %s release is a disc image, left out (import it as a folder)", s_relKeys[i]);
                SDL_free(s_mix.root[i]);
                s_mix.root[i] = NULL;
                Disc_Close();
            }
        }
        SDL_free(path);
    }
    {
        int n = 0;

        for (i = 0; i < s_mix.numOrder; i++) {
            if (s_mix.root[s_mix.order[i]] != NULL) {
                s_mix.order[n++] = s_mix.order[i];
            }
        }
        s_mix.numOrder = n;
    }
    if (s_mix.numOrder == 0) {
        SDL_snprintf(err, errSize, "none of the recipe's releases is imported (sources.cfg in the save folder "
                     "lists gc=, pc=, ps2=; or set HWVX_MIX_GC, HWVX_MIX_PC, HWVX_MIX_PS2)");
        return 0;
    }
    for (i = 0; i < PORT_DATA_COUNT; i++) {     /* every archive of every release: size and time */
        stamp = Fnv(stamp, &i, sizeof(i));
        if (s_mix.root[i] != NULL) {
            stamp = Fnv(stamp, s_mix.root[i], SDL_strlen(s_mix.root[i]));
        }
        stamp = StampRelease(stamp, i);
    }
    s_mix.stamp = stamp ? stamp : 1;
    /* --mix-build fills "mix-NAME.part", from empty; Port_MixBuildFinish
     * puts it in the place of the last complete build */
    SDL_snprintf(s_mix.cacheName, sizeof(s_mix.cacheName), building ? "mix-%s.part" : "mix-%s", s_mix.name);
    if (building) {
        char *part = CacheFolder(s_mix.cacheName);

        if (part != NULL) {
            Port_RemoveTree(part);
            SDL_free(part);
        }
    }
    s_mix.building = building;
    s_mix.on = 1;
    Port_Log("mix: recipe %s, %s look, %s interface; gc %s, pc %s, ps2 %s", s_mix.name, s_mix.lookGC ? "GameCube" : "PC",
             s_mix.iface == PORT_DATA_PS2 ? "silver (PS2)" : "yellow (GameCube)", s_mix.root[0] ? s_mix.root[0] : "-", s_mix.root[1] ? s_mix.root[1] : "-",
             s_mix.root[2] ? s_mix.root[2] : "-");
    return 1;
}

/* ---- files ------------------------------------------------------------------------- */

SDL_IOStream *Port_OpenReleaseFile(int rel, const char *name, char **found);   /* dvd.c */

SDL_IOStream *Port_OpenProtoFile(const char *name);   /* dvd.c */

/* The prototype's copy of a pack file (mode "replace"): the file read last
 * kept open, per thread (the loading thread mixes its own packs). */
static u32 ProtoRead(const char *name, u64 offset, void *dst, u32 size) {
    static _Thread_local SDL_IOStream *io;
    static _Thread_local char *last;

    if (last == NULL || SDL_strcasecmp(last, name) != 0) {
        if (io != NULL) {
            SDL_CloseIO(io);
        }
        SDL_free(last);
        last = SDL_strdup(name);
        io = Port_OpenProtoFile(name);
    }
    if (io == NULL || SDL_SeekIO(io, (Sint64)offset, SDL_IO_SEEK_SET) < 0) {
        return 0;
    }
    return (u32)SDL_ReadIO(io, dst, size);
}

u32 Port_MixRead(int rel, const char *name, u64 offset, void *dst, u32 size) {
    if (rel == PORT_MIX_PROTO) {
        return Port_MixProtoReplace() ? ProtoRead(name, offset, dst, size) : 0;
    }
    if (rel < 0 || rel >= PORT_DATA_COUNT || s_mix.root[rel] == NULL) {
        return 0;
    }
    if (s_mix.donor[rel].name == NULL || SDL_strcasecmp(s_mix.donor[rel].name, name) != 0) {
        if (s_mix.donor[rel].io != NULL) {
            SDL_CloseIO(s_mix.donor[rel].io);
        }
        SDL_free(s_mix.donor[rel].name);
        s_mix.donor[rel].name = SDL_strdup(name);
        s_mix.donor[rel].io = Port_OpenReleaseFile(rel, name, NULL);
    }
    if (s_mix.donor[rel].io == NULL || SDL_SeekIO(s_mix.donor[rel].io, (Sint64)offset, SDL_IO_SEEK_SET) < 0) {
        return 0;
    }
    return (u32)SDL_ReadIO(s_mix.donor[rel].io, dst, size);
}

/* The cache folder `cacheName` (xdl.c's: HWVX_XDL_DIR, else <pref>xdl). */
static char *CacheFolder(const char *cacheName) {
    const char *env = SDL_getenv("HWVX_XDL_DIR");
    char *path = NULL;

    if (env != NULL && *env) {
        SDL_asprintf(&path, "%s/%s", env, cacheName);
    } else {
        char *pref = Port_PrefPath();

        SDL_asprintf(&path, "%sxdl/%s", pref != NULL ? pref : "", cacheName);
        SDL_free(pref);
    }
    return path;
}

static char *CachePath(const char *file) {
    char *dir = CacheFolder(s_mix.cacheName), *path = NULL;

    if (dir != NULL) {
        SDL_asprintf(&path, "%s/%s", dir, file);
        SDL_free(dir);
    }
    return path;
}

void Port_MixManifest(const char *id, int rel, u32 index, u64 hash) {
    Port_MixManifestUI(id, rel, index, hash, NULL);
}

/* HWVX_MIX_MANIFEST=1: a game played unbuilt writes the manifest of what it
 * mixes too, <cache>/manifest-play.jsonl (appended), to check against a build's. */
void Port_MixManifestUI(const char *id, int rel, u32 index, u64 hash, const char *ui) {
    const char *play = SDL_getenv("HWVX_MIX_MANIFEST");

    if ((!s_mix.building && (play == NULL || SDL_atoi(play) == 0)) || rel < 0 || rel >= PORT_DATA_COUNT) {
        return;
    }
    if (s_mix.manifest == NULL) {
        char *path = CachePath(s_mix.building ? "manifest.jsonl" : "manifest-play.jsonl"), *slash;

        if (path == NULL) {
            return;
        }
        slash = SDL_strrchr(path, '/');
        *slash = 0;
        {
            char *up = SDL_strdup(path), *s2 = SDL_strrchr(up, '/');

            if (s2 != NULL) {
                *s2 = 0;
                SDL_CreateDirectory(up);
            }
            SDL_free(up);
        }
        SDL_CreateDirectory(path);
        *slash = '/';
        s_mix.manifest = SDL_IOFromFile(path, s_mix.building ? "w" : "a");
        SDL_free(path);
        if (s_mix.manifest == NULL) {
            return;
        }
    }
    SDL_IOprintf(s_mix.manifest, "{\"id\":\"%s\",\"from\":\"%s\",\"i\":%u,\"x\":\"%016llx\"%s%s%s}\n", id,
                 s_relKeys[rel], (unsigned)index, (unsigned long long)hash, ui != NULL ? ",\"ui\":\"" : "",
                 ui != NULL ? ui : "", ui != NULL ? "\"" : "");
    if (!s_mix.building) {
        SDL_FlushIO(s_mix.manifest);    /* a played game is stopped, not closed */
    }
}

void Port_MixManifestClose(void) {
    if (s_mix.manifest != NULL) {
        SDL_CloseIO(s_mix.manifest);
        s_mix.manifest = NULL;
    }
}

/* ---- the share packs' textures other packs' interfaces show (uirefs.txt) ------------ */

#define UIREFS_FILE "uirefs.txt"

/* A file of the cache folder that starts "stamp=" and "xdl=" lines (built.cfg,
 * uirefs.txt) is of this build: the recipe and releases, the cache version. */
static int OfThisBuild(char *text) {
    u32 version, compiler, settings, v, c, st;
    char *line, *save = NULL;
    int stamped = 0, xdl = 0;

    Xdl_Stamp(&version, &compiler, &settings);
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strncmp(line, "xdl=", 4) == 0) {
            xdl = SDL_sscanf(line + 4, "%u.%u.%u", &v, &c, &st) == 3 && v == version && c == compiler && st == settings;
        } else if (SDL_strncmp(line, "stamp=", 6) == 0) {
            stamped = (u32)SDL_strtoul(line + 6, NULL, 16) == s_mix.stamp;
        }
    }
    return stamped && xdl;
}

int Port_MixUiRefsSave(const PortMixUiRef *refs, u32 count) {
    char *path = CachePath(UIREFS_FILE), *tmp = NULL, *dir = CacheFolder(s_mix.cacheName);
    u32 version, compiler, settings, i;
    SDL_IOStream *io;
    int ok;

    if (path == NULL || dir == NULL || SDL_asprintf(&tmp, "%s.part", path) < 0) {
        SDL_free(path);
        SDL_free(dir);
        return 0;
    }
    SDL_CreateDirectory(dir);   /* makes its parents too */
    Xdl_Stamp(&version, &compiler, &settings);
    ok = (io = SDL_IOFromFile(tmp, "w")) != NULL;
    if (ok) {
        ok &= SDL_IOprintf(io, "stamp=%08x\nxdl=%u.%u.%u\ncount=%u\n", (unsigned)s_mix.stamp, version, compiler, settings,
                           (unsigned)count) > 0;
        for (i = 0; ok && i < count; i++) {     /* archive, pack id, texture or slot index, PORT_IFACE_* */
            ok &= SDL_IOprintf(io, "ref %s %u %u %u\n", refs[i].archive, (unsigned)refs[i].id, (unsigned)refs[i].index,
                               (unsigned)refs[i].what) > 0;
        }
        ok &= SDL_CloseIO(io);
    }
    ok = ok && SDL_RenamePath(tmp, path);
    if (!ok) {
        Port_Log("mix: could not write %s: %s", path, SDL_GetError());
        SDL_RemovePath(tmp);
    }
    SDL_free(tmp);
    SDL_free(path);
    SDL_free(dir);
    return ok;
}

/* The list in the cache, given to PortMix_AddUiRef; -1 when there is none of this build. */
static int UiRefsRead(void) {
    char *path = CachePath(UIREFS_FILE), *text, *copy, *line, *save = NULL;
    int n = 0, count = -1;

    text = path != NULL ? SDL_LoadFile(path, NULL) : NULL;
    SDL_free(path);
    copy = text != NULL ? SDL_strdup(text) : NULL;
    if (copy == NULL || !OfThisBuild(copy)) {
        SDL_free(copy);
        SDL_free(text);
        return -1;
    }
    SDL_free(copy);
    for (line = SDL_strtok_r(text, "\r\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char archive[0x10];
        unsigned id, index, what;

        if (SDL_strncmp(line, "count=", 6) == 0) {
            count = SDL_atoi(line + 6);
        } else if (SDL_sscanf(line, "ref %15s %u %u %u", archive, &id, &index, &what) == 4) {
            PortMix_AddUiRef(archive, id, index, (u8)what);
            n++;
        }
    }
    SDL_free(text);
    return n == count ? n : -1;     /* a list cut short: made again */
}

int Port_MixUiRefsLoad(const char *program, const char *recipe) {
    const char *args[4];
    SDL_Environment *env;
    SDL_PropertiesID props;
    SDL_Process *proc;
    Uint64 t0;
    int n, code = -1;

    if (!s_mix.on || s_mix.building) {
        return 0;
    }
    if ((n = UiRefsRead()) >= 0) {
        Port_Log("mix: %d textures of share packs shown by other packs' interfaces (%s)", n, UIREFS_FILE);
        return 1;
    }
    if (program == NULL || recipe == NULL) {
        Port_Log("mix: no %s, and no program to make it: shared interface textures follow the \"best\" rule", UIREFS_FILE);
        return 0;
    }
    /* the builder's first pass, in a process of its own (it loads every pack) */
    t0 = SDL_GetTicks();
    args[0] = program;
    args[1] = "--mix-scan";
    args[2] = recipe;
    args[3] = NULL;
    env = SDL_CreateEnvironment(true);
    SDL_SetEnvironmentVariable(env, "HWVX_NO_DIALOG", "1", true);
    SDL_UnsetEnvironmentVariable(env, "HWVX_DUMP");
    SDL_UnsetEnvironmentVariable(env, "HWVX_START");
    SDL_UnsetEnvironmentVariable(env, "HWVX_MIX");      /* only what the arguments say */
    SDL_UnsetEnvironmentVariable(env, "HWVX_MIX_BUILD");
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (proc == NULL) {
        Port_Log("mix: could not start %s --mix-scan: %s", program, SDL_GetError());
    } else {
        SDL_WaitProcess(proc, true, &code);
        SDL_DestroyProcess(proc);
    }
    n = code == 0 ? UiRefsRead() : -1;
    if (n < 0) {
        Port_Log("mix: --mix-scan failed (%d): shared interface textures follow the \"best\" rule", code);
        return 0;
    }
    Port_Log("mix: %d textures of share packs shown by other packs' interfaces (--mix-scan, %u ms)", n,
             (unsigned)(SDL_GetTicks() - t0));
    {
        /* share archives cached before there was a list (an unbuilt game's,
         * mixed as they loaded) are mixed again; a complete build's are right */
        char *built = CachePath(PORT_MIX_BUILT), *text = built != NULL ? SDL_LoadFile(built, NULL) : NULL;

        if (text == NULL || !OfThisBuild(text)) {
            char *list = CachePath(UIREFS_FILE), *lt = list != NULL ? SDL_LoadFile(list, NULL) : NULL, *line, *save = NULL;

            for (line = lt != NULL ? SDL_strtok_r(lt, "\r\n", &save) : NULL; line != NULL;
                 line = SDL_strtok_r(NULL, "\r\n", &save)) {
                char archive[0x10], *xdl = NULL, *p;
                unsigned id, index, what;

                if (SDL_sscanf(line, "ref %15s %u %u %u", archive, &id, &index, &what) != 4) {
                    continue;
                }
                for (p = archive; *p; p++) {
                    *p = (char)SDL_tolower((unsigned char)*p);
                }
                SDL_asprintf(&xdl, "%s.xdl", archive);
                p = xdl != NULL ? CachePath(xdl) : NULL;
                if (p != NULL && SDL_GetPathInfo(p, NULL) && SDL_RemovePath(p)) {
                    Port_Log("mix: %s was cached without the list: mixed again", p);
                }
                SDL_free(p);
                SDL_free(xdl);
            }
            SDL_free(lt);
            SDL_free(list);
        }
        SDL_free(text);
        SDL_free(built);
    }
    return 1;
}

/* ---- the archives of every release ---------------------------------------------------- */

typedef struct ArchiveScan {
    char **names;
    int count, cap;
    size_t rootLen;
    const char *ext;
} ArchiveScan;

static SDL_EnumerationResult SDLCALL ScanArchives(void *user, const char *dir, const char *name) {
    ArchiveScan *scan = user;
    char *path = NULL;
    SDL_PathInfo info;
    size_t n = SDL_strlen(name);
    int i;

    if (name[0] == '.' || SDL_asprintf(&path, "%s%s", dir, name) < 0) {
        return SDL_ENUM_CONTINUE;
    }
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        char *sub = NULL;

        if (SDL_asprintf(&sub, "%s/", path) > 0) {
            SDL_EnumerateDirectory(sub, ScanArchives, scan);
            SDL_free(sub);
        }
    } else if (n > 4 && SDL_strcasecmp(name + n - 4, scan->ext) == 0 && SDL_strlen(path) > scan->rootLen) {
        char *rel = SDL_strdup(path + scan->rootLen), *p;

        rel[SDL_strlen(rel) - 4] = 0;
        for (p = rel; *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
        /* once, whichever release spells or files it differently (the PC's
         * import is flat): by its stem */
        for (i = 0; i < scan->count; i++) {
            int la, lb;
            const char *a = Stem(scan->names[i], &la), *b = Stem(rel, &lb);

            if (la == lb && SDL_strncasecmp(a, b, (size_t)la) == 0) {
                break;
            }
        }
        if (i == scan->count) {
            if (scan->count == scan->cap) {
                scan->cap = scan->cap ? scan->cap * 2 : 128;
                scan->names = SDL_realloc(scan->names, (size_t)scan->cap * sizeof(char *));
            }
            scan->names[scan->count++] = rel;
        } else {
            SDL_free(rel);
        }
    }
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static int CmpName(const void *a, const void *b) {
    return SDL_strcasecmp(*(char *const *)a, *(char *const *)b);
}

void Port_MixArchives(void (*fn)(const char *name, void *user), void *user) {
    static const char *const exts[PORT_DATA_COUNT] = { ".xgc", ".xpc", ".xps" };
    /* folders first: they give the names the game asks for */
    static const int rels[PORT_DATA_COUNT] = { PORT_DATA_GC, PORT_DATA_PS2, PORT_DATA_PC };
    ArchiveScan scan;
    int i;

    SDL_zero(scan);
    for (i = 0; i < PORT_DATA_COUNT; i++) {
        int rel = rels[i];
        char *dir = NULL;

        if (s_mix.root[rel] == NULL || (rel == PORT_DATA_GC && Disc_IsOpen())) {
            continue;
        }
        if (rel == PORT_DATA_PS2) {
            char *sub = Port_FindNoCase(s_mix.root[rel], "XPS/ENGLISH");

            if (sub != NULL) {
                SDL_asprintf(&dir, "%s/", sub);
                SDL_free(sub);
            }
        }
        if (dir == NULL) {
            SDL_asprintf(&dir, "%s/", s_mix.root[rel]);
        }
        scan.rootLen = SDL_strlen(dir);
        scan.ext = exts[rel];
        SDL_EnumerateDirectory(dir, ScanArchives, &scan);
        SDL_free(dir);
    }
    SDL_qsort(scan.names, (size_t)scan.count, sizeof(char *), CmpName);
    for (i = 0; i < scan.count; i++) {
        fn(scan.names[i], user);
        SDL_free(scan.names[i]);
    }
    SDL_free(scan.names);
}

/* ---- the end of a build, and the launcher's look at one ------------------------------- */

static void MixReset(void) {
    int i, k;
    MixRules *lists[3] = { &s_mix.base, &s_mix.assets, &s_mix.files };

    Port_MixManifestClose();
    for (k = 0; k < 3; k++) {
        for (i = 0; lists[k]->rule != NULL && lists[k]->rule[i].match != NULL; i++) {   /* a list ends in an empty rule */
            SDL_free(lists[k]->rule[i].match);
        }
        SDL_free(lists[k]->rule);
    }
    for (i = 0; i < PORT_DATA_COUNT; i++) {
        SDL_free(s_mix.root[i]);
        if (s_mix.donor[i].io != NULL) {
            SDL_CloseIO(s_mix.donor[i].io);
        }
        SDL_free(s_mix.donor[i].name);
    }
    SDL_zero(s_mix);
}

int Port_MixBuildFinish(u32 packs, u32 failed) {
    char *part = CacheFolder(s_mix.cacheName), *dir = NULL, *old = NULL, *built = NULL;
    u32 version, compiler, settings;
    SDL_IOStream *io;
    int ok = 0;

    Port_MixManifestClose();
    if (part == NULL || SDL_asprintf(&dir, "%.*s", (int)(SDL_strlen(part) - 5), part) < 0 ||
        SDL_asprintf(&old, "%s.old", dir) < 0 || SDL_asprintf(&built, "%s/%s", part, PORT_MIX_BUILT) < 0) {
        printf("@error out of memory\n");
    } else if (packs == 0) {
        printf("@error no pack was built\n");
    } else if ((io = SDL_IOFromFile(built, "w")) == NULL) {
        printf("@error %s: %s\n", built, SDL_GetError());
    } else {
        Xdl_Stamp(&version, &compiler, &settings);
        SDL_IOprintf(io, "stamp=%08x\nxdl=%u.%u.%u\npacks=%u\nfailed=%u\n", (unsigned)s_mix.stamp, version, compiler,
                     settings, (unsigned)packs, (unsigned)failed);
        if (!SDL_CloseIO(io)) {
            printf("@error %s: %s\n", built, SDL_GetError());
        } else {
            /* only now does it take the last build's place */
            Port_RemoveTree(old);
            if (SDL_GetPathInfo(dir, NULL) && !SDL_RenamePath(dir, old)) {
                printf("@error could not replace %s: %s\n", dir, SDL_GetError());
            } else if (!SDL_RenamePath(part, dir)) {
                printf("@error could not finish %s: %s\n", dir, SDL_GetError());
                SDL_RenamePath(old, dir);
            } else {
                ok = 1;
                printf("@convert %u %u done\n", (unsigned)packs, (unsigned)packs);
            }
            Port_RemoveTree(old);
        }
    }
    if (!ok && part != NULL) {
        Port_RemoveTree(part);
    }
    fflush(stdout);
    SDL_free(part);
    SDL_free(dir);
    SDL_free(old);
    SDL_free(built);
    return ok;
}

int Port_MixProbe(const char *nameOrPath, Port_DataCheck check, PortMixInfo *info, char *err, u32 errSize) {
    int ok;

    SDL_zerop(info);
    if (s_mix.on) {
        SDL_snprintf(err, errSize, "a mixed build is running");
        return 0;
    }
    ok = Port_MixSetup(nameOrPath, 0, check, err, errSize);
    if (ok) {
        char *dir = CacheFolder(s_mix.cacheName);

        SDL_strlcpy(info->name, s_mix.name, sizeof(info->name));
        SDL_strlcpy(info->dir, dir != NULL ? dir : "", sizeof(info->dir));
        SDL_free(dir);
        info->stamp = s_mix.stamp;
        info->releases = s_mix.numOrder;
    }
    MixReset();
    Disc_Close();   /* the GameCube's image, if the check opened it */
    return ok;
}
