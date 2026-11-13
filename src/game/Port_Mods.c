/* Mods over the player's own game data (Port_Mods.h; docs/xdl-format
 * README §8, §10.3). Port-only file.
 *
 * - The list: every folder of the mods folder with a mod.jsonc, in the
 *   order mods.jsonc gives ("order", "disabled"), then the others by id,
 *   then each moved behind the mods its "after" names.
 * - Entries (cars, items, tracks): an enabled mod's entries go into ids.lock
 *   with their alias, name (a track's world) and "mod" (Port_ModsSyncLock), and the registry
 *   numbers and loads them as it does any lock entry. A mod that's off or
 *   gone leaves its entries "active": false, their numbers kept.
 * - Patches: RFC 7396 merge patches on objects of a loaded pack, by object
 *   id (base://HW_Cars/car_7/CarAInfo/0/accel; an entry's own as
 *   CarAInfo/0/accel), applied as the pack's pointers are fixed up, with the
 *   fields the schemas name (Port_ModFields.inc, tools/gen_mod_fields.py).
 * - Recipes: colour matrices on a pack's textures (gx_texmod.h), composed
 *   as tools/xdl_mod.py's recipe_matrix does.
 * - Sounds: a mod's own sound files (WAV, FLAC, Ogg Vorbis, MP3) over a
 *   pack's sounds, decoded here (src/audiodec) as the pack loads.
 * - Packages: a .xmod is a zip of stored entries the packer made
 *   (check.json in it); Port_ModsInstall unpacks one. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Content.h"
#include "Port_Mods.h"
#include "Port_Level.h"
#include "jsonc.h"
#include "gx_texmod.h"
#include "Port_Mix.h"
#include "audiodec.h"
#include "mbedtls/sha256.h"

enum { MF_U8, MF_S8, MF_U16, MF_S16, MF_U32, MF_S32, MF_F32, MF_LINK, MF_OTHER };

typedef struct ModField {
    const char *name;
    u32 offset;
    int type;
    int count;          /* 0: one value; else an array of count. MF_LINK: 1 + the
                           offset of the u32 counting the objects it points to
                           (0: one object) */
    int target;         /* MF_LINK: the struct it points to (s_structs) */
} ModField;

typedef struct ModStruct {
    const char *name;
    u32 size;
    const ModField *fields;
    u32 count;
} ModStruct;

#include "Port_ModFields.inc"

void *Gfx_PortTextureImage(u16 id);   /* Gfx_TextureHandler.c */
void Port_RemoveTree(const char *path); /* fileio.c */

typedef struct Mod {
    PortModInfo info;
    char *dir;          /* its folder, with a trailing separator */
    JsonNode *doc;      /* mod.jsonc; info's strings point into it */
    int cycle;          /* its "after" mods come after it in turn */
} Mod;

static Mod *s_mods;
static int s_numMods;
static char **s_report;
static int s_numReport, s_capReport;
static int s_inited;
static int FolderChecked(const char *dir, const char *modId);
static int s_logValues;     /* HWVX_MODS_LOG=1: each value a patch sets, and what it was */
static double s_old;

static const char *const s_typeNames[] = { "car", "interface", "item", "link", "world", "collision",
                                           "world_texture", "geometry", "share", "audio", "music" };

/* ---- small helpers ------------------------------------------------------- */

/* A line of the report (and the log), once: a pack loads every race. */
static void Report(const char *fmt, ...) PORT_PRINTF(1, 2);
static void Report(const char *fmt, ...) {
    va_list ap;
    char *line = NULL;
    int i;

    va_start(ap, fmt);
    SDL_vasprintf(&line, fmt, ap);
    va_end(ap);
    if (line == NULL) {
        return;
    }
    for (i = 0; i < s_numReport; i++) {
        if (SDL_strcmp(s_report[i], line) == 0) {
            SDL_free(line);
            return;
        }
    }
    Port_Log("mods: %s", line);
    if (s_numReport == s_capReport) {
        int cap = s_capReport ? s_capReport * 2 : 16;
        char **r = SDL_realloc(s_report, (size_t)cap * sizeof(*r));

        if (r == NULL) {
            SDL_free(line);
            return;
        }
        s_report = r;
        s_capReport = cap;
    }
    s_report[s_numReport++] = line;
}

static void ClearReport(void) {
    int i;

    for (i = 0; i < s_numReport; i++) {
        SDL_free(s_report[i]);
    }
    s_numReport = 0;
}

static const char *Str(const JsonNode *o, const char *key) {
    const JsonNode *n = Json_Get(o, key);

    return n != NULL && n->type == JSON_STRING ? n->string : NULL;
}

static int Same(const JsonNode *a, const char *b) {
    return a != NULL && a->type == JSON_STRING && b != NULL && SDL_strcmp(a->string, b) == 0;
}

static int Enabled(const Mod *m) {
    const char *e = SDL_getenv("HWVX_MODS");

    return m->info.enabled && !(e != NULL && SDL_strcmp(e, "0") == 0);
}

/* A JSON string node (there's no constructor for one: parsed from text). */
static JsonNode *NewString(const char *s) {
    size_t n = SDL_strlen(s), i, o = 0;
    char *t = SDL_malloc(n * 6 + 3), err[64];
    JsonNode *node;

    if (t == NULL) {
        return NULL;
    }
    t[o++] = '"';
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c == '"' || c == '\\') {
            t[o++] = '\\';
            t[o++] = (char)c;
        } else if (c < 0x20) {
            o += (size_t)SDL_snprintf(t + o, 7, "\\u%04x", c);
        } else {
            t[o++] = (char)c;
        }
    }
    t[o++] = '"';
    node = Json_Parse(t, o, err, sizeof(err));
    SDL_free(t);
    return node;
}

static int WriteJson(const char *path, const JsonNode *root, const char *comment) {
    JsonWriter w;
    char *tmp = NULL;
    int ok = 0;

    Jw_Init(&w);
    if (comment != NULL) {
        Jw_Comment(&w, comment);
    }
    Jw_Node(&w, NULL, root);
    Jw_Raw(&w, "\n");
    if (!w.failed && SDL_asprintf(&tmp, "%s.tmp", path) > 0 && SDL_SaveFile(tmp, w.buf, w.len)) {
        ok = SDL_RenamePath(tmp, path);
    }
    if (!ok) {
        Port_Log("mods: can't write %s: %s", path, SDL_GetError());
    }
    SDL_free(tmp);
    Jw_Free(&w);
    return ok;
}

static const ModStruct *StructNamed(const char *name, size_t len) {
    u32 i;

    for (i = 0; i < SDL_arraysize(s_structs); i++) {
        if (SDL_strlen(s_structs[i].name) == len && SDL_strncmp(s_structs[i].name, name, len) == 0) {
            return &s_structs[i];
        }
    }
    return NULL;
}

static const ModField *FieldNamed(const ModStruct *st, const char *name, size_t len) {
    u32 i;

    for (i = 0; i < st->count; i++) {
        if (SDL_strlen(st->fields[i].name) == len && SDL_strncmp(st->fields[i].name, name, len) == 0) {
            return &st->fields[i];
        }
    }
    return NULL;
}

static int ReleaseOf(const DataPack *pack) {
    return pack->portFormat == PORT_DATA_PC ? 1 : pack->portFormat == PORT_DATA_PS2 ? 2 : 0;
}

static int ForRelease(const Mod *m, const DataPack *pack) {
    static const char *const names[] = { "gc", "pc", "ps2" };
    const JsonNode *rel = Json_Get(m->doc, "releases"), *e;

    if (rel == NULL || rel->type != JSON_ARRAY) {
        return 1;
    }
    for (e = rel->child; e != NULL; e = e->next) {
        if (e->type == JSON_STRING && SDL_strcmp(e->string, names[ReleaseOf(pack)]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- the list ------------------------------------------------------------ */

char *Port_ModsDir(void) {
    const char *env = SDL_getenv("HWVX_MODS_DIR");
    char *pref, *dir = NULL;

    if (env != NULL && *env != 0) {
        size_t n = SDL_strlen(env);

        SDL_asprintf(&dir, "%s%s", env, env[n - 1] == '/' || env[n - 1] == '\\' ? "" : "/");
        return dir;
    }
    if ((pref = Port_PrefPath()) == NULL) {
        return NULL;
    }
    SDL_asprintf(&dir, "%smods/", pref);
    SDL_free(pref);
    return dir;
}

static void FreeMods(void) {
    int i;

    for (i = 0; i < s_numMods; i++) {
        SDL_free(s_mods[i].dir);
        SDL_free((char *)s_mods[i].info.folder);
        Json_Free(s_mods[i].doc);
    }
    SDL_free(s_mods);
    s_mods = NULL;
    s_numMods = 0;
}

static SDL_EnumerationResult SDLCALL AddMod(void *userdata, const char *dirname, const char *fname) {
    char *dir = NULL, *file = NULL, *text, err[160];
    size_t len;
    JsonNode *doc;
    Mod *m;
    const char *id;
    SDL_PathInfo pi;

    (void)userdata;
    if (fname[0] == '.' || SDL_strstr(fname, ".part") != NULL) {
        return SDL_ENUM_CONTINUE;
    }
    SDL_asprintf(&dir, "%s%s/", dirname, fname);
    SDL_asprintf(&file, "%smod.jsonc", dir);
    if (dir == NULL || file == NULL || !SDL_GetPathInfo(file, &pi)) {
        SDL_free(dir);
        SDL_free(file);
        return SDL_ENUM_CONTINUE;
    }
    text = SDL_LoadFile(file, &len);
    doc = text != NULL ? Json_Parse(text, len, err, sizeof(err)) : NULL;
    SDL_free(text);
    id = doc != NULL ? Str(doc, "id") : NULL;
    if (doc == NULL || doc->type != JSON_OBJECT || id == NULL || SDL_strncmp(id, "uid://", 6) != 0 ||
        Json_GetInt(doc, "format_version", 0) != 1) {
        Report("%s: not a mod this version reads (%s)", fname, doc == NULL ? err : "id, format_version");
        Json_Free(doc);
        SDL_free(dir);
        SDL_free(file);
        return SDL_ENUM_CONTINUE;
    }
    m = SDL_realloc(s_mods, (size_t)(s_numMods + 1) * sizeof(*m));
    if (m == NULL) {
        Json_Free(doc);
        SDL_free(dir);
        SDL_free(file);
        return SDL_ENUM_FAILURE;
    }
    s_mods = m;
    m = &s_mods[s_numMods++];
    SDL_zerop(m);
    m->dir = dir;
    m->doc = doc;
    m->info.id = id;
    m->info.name = Str(doc, "name") != NULL ? Str(doc, "name") : id;
    m->info.version = Str(doc, "version") != NULL ? Str(doc, "version") : "";
    m->info.author = Str(doc, "author") != NULL ? Str(doc, "author") : "";
    m->info.description = Str(doc, "description") != NULL ? Str(doc, "description") : "";
    m->info.folder = SDL_strdup(fname);
    m->info.enabled = 1;
    SDL_free(file);
    m->info.checked = FolderChecked(dir, id);
    return SDL_ENUM_CONTINUE;
}

static int IndexOfId(const char *id) {
    int i;

    for (i = 0; i < s_numMods; i++) {
        if (SDL_strcmp(s_mods[i].info.id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static int ById(const void *a, const void *b) {
    return SDL_strcmp(((const Mod *)a)->info.id, ((const Mod *)b)->info.id);
}

static void MoveMod(int from, int to) {
    Mod t = s_mods[from];

    if (from < to) {
        SDL_memmove(&s_mods[from], &s_mods[from + 1], (size_t)(to - from) * sizeof(Mod));
    } else if (from > to) {
        SDL_memmove(&s_mods[to + 1], &s_mods[to], (size_t)(from - to) * sizeof(Mod));
    }
    s_mods[to] = t;
}

/* Each mod behind the mods its "after" names: a stable topological sort (the
 * order so far kept wherever "after" doesn't say otherwise); mods in a cycle
 * keep their order and are reported. */
static void AfterOrder(void) {
    int n = s_numMods, i, j, k, done = 0;
    int *deps = SDL_calloc((size_t)n * n + 1, sizeof(int)), *left = SDL_calloc((size_t)n + 1, sizeof(int));
    Mod *out = SDL_malloc(((size_t)n + 1) * sizeof(Mod));
    char *placed = SDL_calloc((size_t)n + 1, 1);

    if (deps == NULL || left == NULL || out == NULL || placed == NULL) {
        goto done;
    }
    for (i = 0; i < n; i++) {     /* deps[i * n + j]: i comes after j */
        const JsonNode *a = Json_Get(s_mods[i].doc, "after");

        for (a = a != NULL && a->type == JSON_ARRAY ? a->child : NULL; a != NULL; a = a->next) {
            if (a->type == JSON_STRING && (j = IndexOfId(a->string)) >= 0 && j != i && !deps[i * n + j]) {
                deps[i * n + j] = 1;
                left[i]++;
            }
        }
    }
    while (done < n) {
        /* the first (in the order so far) whose mods are all placed; in a cycle, the first left */
        for (i = 0; i < n && (placed[i] || left[i] > 0); i++) {
        }
        if (i == n) {
            for (i = 0; placed[i]; i++) {
            }
            s_mods[i].cycle = 1;
        }
        placed[i] = 1;
        out[done++] = s_mods[i];
        for (k = 0; k < n; k++) {
            if (deps[k * n + i]) {
                deps[k * n + i] = 0;
                left[k]--;
            }
        }
    }
    SDL_memcpy(s_mods, out, (size_t)n * sizeof(Mod));
done:
    SDL_free(deps);
    SDL_free(left);
    SDL_free(out);
    SDL_free(placed);
}

/* mods.jsonc's order and disabled mods, then "after". */
static void Order(const char *dir) {
    char *path = NULL, *text, err[160];
    size_t len;
    JsonNode *cfg = NULL;
    const JsonNode *e;
    int placed = 0, i;

    SDL_qsort(s_mods, (size_t)s_numMods, sizeof(Mod), ById);
    SDL_asprintf(&path, "%smods.jsonc", dir);
    text = path != NULL ? SDL_LoadFile(path, &len) : NULL;
    if (text != NULL && (cfg = Json_Parse(text, len, err, sizeof(err))) == NULL) {
        Report("mods.jsonc: %s; the order is by id", err);
    }
    SDL_free(text);
    SDL_free(path);
    e = Json_Get(cfg, "order");
    for (e = e != NULL && e->type == JSON_ARRAY ? e->child : NULL; e != NULL; e = e->next) {
        if (e->type == JSON_STRING && (i = IndexOfId(e->string)) >= placed) {
            MoveMod(i, placed++);
        }
    }
    e = Json_Get(cfg, "disabled");
    for (e = e != NULL && e->type == JSON_ARRAY ? e->child : NULL; e != NULL; e = e->next) {
        if (e->type == JSON_STRING && (i = IndexOfId(e->string)) >= 0) {
            s_mods[i].info.enabled = 0;
        }
    }
    Json_Free(cfg);
    AfterOrder();
}

/* Conflicts and notes for the enabled mods. */
static void BuildReport(void) {
    static const char *const sections[] = { "patches", "recipes", "sounds" };
    int i, j, s;

    ClearReport();
    for (i = 0; i < s_numMods; i++) {
        s_mods[i].info.problems = 0;
    }
    for (i = 0; i < s_numMods; i++) {
        Mod *m = &s_mods[i];

        if (!Enabled(m)) {
            continue;
        }
        if (m->cycle) {
            Report("%s: the mods its \"after\" names come after it in turn (a cycle); it loads in the list's "
                   "order", m->info.name);
            m->info.problems++;
        }
        if (!m->info.checked) {
            Report("%s: put in by hand, not checked by the packer", m->info.name);
            m->info.problems++;
        }
        for (s = 0; s < 3; s++) {
            const JsonNode *a = Json_Get(m->doc, sections[s]), *x, *y;

            for (x = a != NULL && a->type == JSON_OBJECT ? a->child : NULL; x != NULL; x = x->next) {
                for (j = i + 1; j < s_numMods; j++) {
                    const JsonNode *b = Json_Get(s_mods[j].doc, sections[s]);

                    if (!Enabled(&s_mods[j]) || (y = Json_Get(b, x->key)) == NULL) {
                        continue;
                    }
                    if (s >= 1) {
                        Report("%s and %s both %s %s: %s's applies", m->info.name, s_mods[j].info.name,
                               s == 1 ? "recolour" : "replace", x->key, s_mods[j].info.name);
                        s_mods[j].info.problems++;
                    } else if (x->type == JSON_OBJECT && y->type == JSON_OBJECT) {
                        const JsonNode *f;

                        for (f = x->child; f != NULL; f = f->next) {
                            if (Json_Get(y, f->key) != NULL) {
                                Report("%s and %s both set %s %s: %s's applies", m->info.name, s_mods[j].info.name,
                                       x->key, f->key, s_mods[j].info.name);
                                s_mods[j].info.problems++;
                            }
                        }
                    }
                }
            }
        }
        for (s = 0; s < 4; s++) {
            const char *kind = s == 0 ? "cars" : s == 1 ? "items" : s == 2 ? "tracks" : "music";
            const JsonNode *a = Json_Get(m->doc, kind), *x;

            for (x = a != NULL && a->type == JSON_OBJECT ? a->child : NULL; x != NULL; x = x->next) {
                for (j = i + 1; j < s_numMods; j++) {
                    if (Enabled(&s_mods[j]) && Json_Get(Json_Get(s_mods[j].doc, kind), x->key) != NULL) {
                        Report("%s and %s both bring %s: %s's is used", m->info.name, s_mods[j].info.name, x->key,
                               s_mods[j].info.name);
                        s_mods[j].info.problems++;
                    }
                }
            }
        }
    }
}

int Port_ModsScan(void) {
    char *dir = Port_ModsDir();

    FreeMods();
    ClearReport();
    if (dir != NULL) {
        SDL_EnumerateDirectory(dir, AddMod, NULL);
        Order(dir);
    }
    SDL_free(dir);
    BuildReport();
    return s_numMods;
}

int Port_ModsCount(void) {
    return s_numMods;
}

const PortModInfo *Port_ModsInfo(int i) {
    return i >= 0 && i < s_numMods ? &s_mods[i].info : NULL;
}

void Port_ModsSetEnabled(int i, int enabled) {
    if (i >= 0 && i < s_numMods) {
        s_mods[i].info.enabled = enabled != 0;
        BuildReport();
    }
}

void Port_ModsMove(int i, int to) {
    if (i >= 0 && i < s_numMods && to >= 0 && to < s_numMods) {
        MoveMod(i, to);
        BuildReport();
    }
}

int Port_ModsSave(void) {
    char *dir = Port_ModsDir(), *path = NULL;
    JsonNode *root = Json_NewObject(), *order, *off;
    int i, ok = 0;

    if (dir != NULL && root != NULL && SDL_asprintf(&path, "%smods.jsonc", dir) > 0) {
        order = Json_ObjectMember(root, "order");
        off = Json_ObjectMember(root, "disabled");
        order->type = off->type = JSON_ARRAY;
        for (i = 0; i < s_numMods; i++) {
            Json_Append(order, NewString(s_mods[i].info.id), NULL);
            if (!s_mods[i].info.enabled) {
                Json_Append(off, NewString(s_mods[i].info.id), NULL);
            }
        }
        SDL_CreateDirectory(dir);
        ok = WriteJson(path, root, "HWVX-SDL mods: the load order (first loads first) and the mods turned off.");
    }
    Json_Free(root);
    SDL_free(path);
    SDL_free(dir);
    return ok;
}

int Port_ModsReportCount(void) {
    return s_numReport;
}

const char *Port_ModsReportLine(int i) {
    return i >= 0 && i < s_numReport ? s_report[i] : NULL;
}

int Port_ModsActive(void) {
    int i;

    for (i = 0; i < s_numMods; i++) {
        if (Enabled(&s_mods[i])) {
            return 1;
        }
    }
    return 0;
}

/* ---- packages ------------------------------------------------------------ */

/* A folder and what's in it (Port_RemoveTree: a link, inside or the folder
 * itself, is unlinked, never followed); 1 if it's gone. */
static int RemoveFolder(const char *dir) {
    char *d = SDL_strdup(dir);
    size_t n = d != NULL ? SDL_strlen(d) : 0;
    SDL_PathInfo pi;

    if (d == NULL) {
        return 0;
    }
    while (n > 1 && (d[n - 1] == '/' || d[n - 1] == '\\')) {    /* "link/" would be followed */
        d[--n] = 0;
    }
    Port_RemoveTree(d);
    n = !SDL_GetPathInfo(d, &pi);
    SDL_free(d);
    return (int)n;
}

int Port_ModsRemove(int i) {
    int ok;

    if (i < 0 || i >= s_numMods) {
        return 0;
    }
    ok = RemoveFolder(s_mods[i].dir);
    Port_ModsScan();
    return ok;
}

static u32 Le32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 Le16(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

/* A name a zip entry may have: relative, no "..", no backslashes. */
static int SafeName(const char *s) {
    const char *p;

    if (*s == 0 || *s == '/' || SDL_strchr(s, '\\') != NULL || SDL_strchr(s, ':') != NULL) {
        return 0;
    }
    for (p = s; p != NULL; p = SDL_strchr(p, '/')) {
        if (*p == '/') {
            p++;
        }
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) {
            return 0;
        }
    }
    return 1;
}

/* The kinds of file a package may hold: those tools/xdl_mod.py's check reads
 * (ALLOWED_EXT), and check.json. */
static int AllowedName(const char *name) {
    static const char *const ext[] = { ".json", ".jsonc", ".png", ".wav", ".gltf", ".glb", ".bin", ".mid", ".md",
                                       ".txt", ".ogg", ".mp3", ".flac" };
    const char *dot = SDL_strrchr(name, '.'), *p;
    u32 i;

    for (p = name; p != NULL; p = SDL_strchr(p, '/')) {     /* no hidden files or folders */
        if (*p == '/') {
            p++;
        }
        if (*p == '.') {
            return 0;
        }
    }
    for (i = 0; dot != NULL && SDL_strchr(dot, '/') == NULL && i < SDL_arraysize(ext); i++) {
        if (SDL_strcasecmp(dot, ext[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void Sha256Hex(const void *data, size_t len, char out[65]) {
    u8 h[32];
    int i;

    mbedtls_sha256(data, len, h, 0);
    for (i = 0; i < 32; i++) {
        SDL_snprintf(out + 2 * i, 3, "%02x", h[i]);
    }
}

/* check.json against the mod it came with: written by the packer for this
 * format, of this mod, and each file it lists of the size and sha256 it gives.
 * NULL if so, else what's wrong. */
static const char *CheckManifest(const JsonNode *check, const char *modId) {
    const JsonNode *files = Json_Get(check, "files");

    if (check == NULL || check->type != JSON_OBJECT) {
        return "check.json isn't readable";
    }
    if (!Same(Json_Get(check, "tool"), "tools/xdl_mod.py") || Json_GetInt(check, "format_version", 0) != 1 ||
        Json_GetInt(check, "tool_version", 0) < 2) {
        return "check.json isn't one this version reads (re-pack the mod with tools/xdl_mod.py)";
    }
    if (modId == NULL || !Same(Json_Get(check, "mod"), modId)) {
        return "check.json is another mod's";
    }
    if (!Same(Json_Get(check, "result"), "no game data found")) {
        return "check.json doesn't say the mod passed";
    }
    if (files == NULL || files->type != JSON_OBJECT || Json_Get(files, "mod.jsonc") == NULL) {
        return "check.json lists no files";
    }
    return NULL;
}

/* One file against its check.json entry. */
static int FileMatches(const JsonNode *entry, const void *data, size_t len) {
    char hex[65];

    if (entry == NULL || entry->type != JSON_OBJECT || Json_GetInt(entry, "size", -1) != (long long)len) {
        return 0;
    }
    Sha256Hex(data, len, hex);
    return Same(Json_Get(entry, "sha256"), hex);
}

/* An installed folder's check.json: the packer's, of this mod, and every file
 * it lists there unchanged. */
static int FolderChecked(const char *dir, const char *modId) {
    char *path = NULL, *text;
    size_t len;
    JsonNode *check;
    const JsonNode *f;
    int ok;

    if (SDL_asprintf(&path, "%scheck.json", dir) < 0) {
        return 0;
    }
    text = SDL_LoadFile(path, &len);
    SDL_free(path);
    check = text != NULL ? Json_Parse(text, len, NULL, 0) : NULL;
    SDL_free(text);
    ok = check != NULL && CheckManifest(check, modId) == NULL;
    for (f = ok ? Json_Get(check, "files")->child : NULL; f != NULL && ok; f = f->next) {
        path = NULL;
        text = SafeName(f->key) && AllowedName(f->key) && SDL_asprintf(&path, "%s%s", dir, f->key) > 0
                   ? SDL_LoadFile(path, &len) : NULL;
        ok = text != NULL && FileMatches(f, text, len);
        SDL_free(text);
        SDL_free(path);
    }
    Json_Free(check);
    return ok;
}

typedef struct ZipEntry {
    char name[256];
    u32 data, size;
} ZipEntry;

/* The id in a folder's mod.jsonc (to free), or NULL. */
static char *FolderId(const char *dir) {
    char *path = NULL, *text, *id = NULL;
    size_t len;
    JsonNode *doc;

    if (SDL_asprintf(&path, "%s/mod.jsonc", dir) < 0) {
        return NULL;
    }
    text = SDL_LoadFile(path, &len);
    doc = text != NULL ? Json_Parse(text, len, NULL, 0) : NULL;
    if (Str(doc, "id") != NULL) {
        id = SDL_strdup(Str(doc, "id"));
    }
    Json_Free(doc);
    SDL_free(text);
    SDL_free(path);
    return id;
}

int Port_ModsInstall(const char *package, char *err, size_t errSize) {
    size_t size = 0;
    u8 *zip = SDL_LoadFile(package, &size);
    const u8 *eocd = NULL, *cd;
    u32 count, cdOff, i, k, n = 0;
    char *dir = Port_ModsDir(), *part = NULL, *final = NULL, *old = NULL, *have = NULL;
    JsonNode *doc = NULL, *check = NULL;
    ZipEntry *ent = NULL;
    const char *id = NULL, *why, *c;
    const JsonNode *f;
    int ok = 0, hasCheck = 0, hasMod = 0, listed = 0, moved = 0;
    SDL_PathInfo pi;

    if (zip == NULL || dir == NULL) {
        SDL_snprintf(err, errSize, "can't read %s", package);
        goto done;
    }
    for (k = size >= 22 ? (u32)size - 22 : 0; size >= 22; k--) {
        if (Le32(zip + k) == 0x06054B50) {
            eocd = zip + k;
            break;
        }
        if (k == 0 || size - k > 0x10000 + 22) {
            break;
        }
    }
    if (eocd == NULL) {
        SDL_snprintf(err, errSize, "not a .xmod (zip) package");
        goto done;
    }
    count = Le16(eocd + 10);
    cdOff = Le32(eocd + 16);
    if ((ent = SDL_calloc(count + 1, sizeof(*ent))) == NULL) {
        goto done;
    }
    /* every entry: a name a package may have, stored, inside the file */
    for (i = 0, cd = zip + (cdOff <= size ? cdOff : size); i < count; i++) {
        ZipEntry *e = &ent[n];
        u32 nlen, method, csize, usize, lho, j;

        if ((size_t)(cd - zip) + 46 > size || Le32(cd) != 0x02014B50) {
            SDL_snprintf(err, errSize, "the package is damaged");
            goto done;
        }
        method = Le16(cd + 10);
        csize = Le32(cd + 20);
        usize = Le32(cd + 24);
        nlen = Le16(cd + 28);
        lho = Le32(cd + 42);
        if (nlen == 0 || nlen >= sizeof(e->name) || (size_t)(cd - zip) + 46 + nlen > size ||
            (size_t)lho + 30 > size || Le32(zip + lho) != 0x04034B50) {
            SDL_snprintf(err, errSize, "the package is damaged");
            goto done;
        }
        SDL_memcpy(e->name, cd + 46, nlen);
        e->name[nlen] = 0;
        e->data = lho + 30 + Le16(zip + lho + 26) + Le16(zip + lho + 28);
        e->size = csize;
        cd += 46 + nlen + Le16(cd + 30) + Le16(cd + 32);
        if (!SafeName(e->name) || method != 0 || csize != usize || (size_t)e->data + csize > size) {
            SDL_snprintf(err, errSize, "%s: %s", e->name, method != 0 || csize != usize
                                                               ? "compressed (the packer stores entries)"
                                                               : "not a name or place a package may use");
            goto done;
        }
        if (e->name[nlen - 1] == '/') {
            continue;   /* a folder: made as its files are written */
        }
        if (!AllowedName(e->name)) {
            SDL_snprintf(err, errSize, "%s: not a kind of file a mod holds (the packer's check reads .json "
                                       ".jsonc .png .wav .gltf .glb .bin .mid .md .txt .ogg .mp3 .flac)", e->name);
            goto done;
        }
        for (j = 0; j < n; j++) {
            if (SDL_strcmp(ent[j].name, e->name) == 0) {
                SDL_snprintf(err, errSize, "%s: twice in the package", e->name);
                goto done;
            }
        }
        if (SDL_strcmp(e->name, "check.json") == 0) {
            hasCheck = 1;
            check = Json_Parse((const char *)zip + e->data, csize, NULL, 0);
        } else if (SDL_strcmp(e->name, "mod.jsonc") == 0) {
            hasMod = 1;
            doc = Json_Parse((const char *)zip + e->data, csize, NULL, 0);
        }
        n++;
    }
    if (!hasCheck || !hasMod) {
        SDL_snprintf(err, errSize, "%s", !hasMod ? "no mod.jsonc in it" : "not made by the packer (no check.json)");
        goto done;
    }
    id = Str(doc, "id");
    for (c = id != NULL && SDL_strncmp(id, "uid://", 6) == 0 ? id + 6 : ""; (*c >= '0' && *c <= '9') ||
         (*c >= 'a' && *c <= 'z'); c++) {
    }
    if (id == NULL || *c != 0 || c == id + 6 || c - id > 6 + 13 || Json_GetInt(doc, "format_version", 0) != 1) {
        SDL_snprintf(err, errSize, "mod.jsonc has no id this version reads");
        goto done;
    }
    /* check.json: the packer's, of this mod, listing exactly the package's files */
    if ((why = CheckManifest(check, id)) != NULL) {
        SDL_snprintf(err, errSize, "%s", why);
        goto done;
    }
    for (i = 0; i < n; i++) {
        if (SDL_strcmp(ent[i].name, "check.json") != 0) {
            if (!FileMatches(Json_Get(Json_Get(check, "files"), ent[i].name), zip + ent[i].data, ent[i].size)) {
                SDL_snprintf(err, errSize, "%s isn't the file the packer checked", ent[i].name);
                goto done;
            }
            listed++;
        }
    }
    for (f = Json_Get(check, "files")->child; f != NULL; f = f->next) {
        listed--;
    }
    if (listed != 0) {
        SDL_snprintf(err, errSize, "check.json lists files the package doesn't have");
        goto done;
    }
    if (SDL_asprintf(&final, "%s%s", dir, id + 6) < 0 || SDL_asprintf(&part, "%s%s.part", dir, id + 6) < 0 ||
        SDL_asprintf(&old, "%s%s.part-old", dir, id + 6) < 0) {
        goto done;
    }
    /* a folder of that name holding something else stays */
    if (SDL_GetPathInfo(final, &pi) && ((have = FolderId(final)) == NULL || SDL_strcmp(have, id) != 0)) {
        SDL_snprintf(err, errSize, "%s%s is in the way: it holds %s", id + 6, "/", have != NULL ? have : "no mod");
        goto done;
    }
    /* unpacked beside it, then swapped in */
    SDL_CreateDirectory(dir);
    RemoveFolder(part);
    if (!SDL_CreateDirectory(part)) {
        SDL_snprintf(err, errSize, "can't make %s: %s", part, SDL_GetError());
        goto done;
    }
    for (i = 0; i < n; i++) {
        char *path = NULL, *slash;

        if (SDL_asprintf(&path, "%s/%s", part, ent[i].name) < 0) {
            goto done;
        }
        if ((slash = SDL_strrchr(path, '/')) != NULL) {
            *slash = 0;
            SDL_CreateDirectory(path);
            *slash = '/';
        }
        if (!SDL_SaveFile(path, zip + ent[i].data, ent[i].size)) {
            SDL_snprintf(err, errSize, "can't write %s: %s", path, SDL_GetError());
            SDL_free(path);
            goto done;
        }
        SDL_free(path);
    }
    RemoveFolder(old);
    if (SDL_GetPathInfo(final, &pi)) {
        if (!SDL_RenamePath(final, old)) {
            SDL_snprintf(err, errSize, "can't move the old %s aside: %s", id, SDL_GetError());
            goto done;
        }
        moved = 1;
    }
    if (!SDL_RenamePath(part, final)) {
        SDL_snprintf(err, errSize, "can't put the mod in place: %s", SDL_GetError());
        if (moved && !SDL_RenamePath(old, final)) {
            Port_Log("mods: the old %s is left in %s", id, old);
        }
        goto done;
    }
    ok = 1;
    RemoveFolder(old);
    /* the mod of the same id in another folder goes */
    Port_ModsScan();
    for (k = 0; k < (u32)s_numMods; k++) {
        if (SDL_strcmp(s_mods[k].info.id, id) == 0 && SDL_strcmp(s_mods[k].info.folder, id + 6) != 0) {
            Port_Log("mods: %s replaces the copy in %s", id, s_mods[k].dir);
            RemoveFolder(s_mods[k].dir);
        }
    }
    Port_Log("mods: installed %s from %s", id, package);
done:
    if (!ok && part != NULL) {
        RemoveFolder(part);
    }
    Json_Free(doc);
    Json_Free(check);
    SDL_free(ent);
    SDL_free(have);
    SDL_free(part);
    SDL_free(old);
    SDL_free(final);
    SDL_free(dir);
    SDL_free(zip);
    Port_ModsScan();
    return ok;
}

/* ---- the game's side ------------------------------------------------------ */

void Port_ModsInit(void) {
    if (s_inited) {
        return;
    }
    s_inited = 1;
    s_logValues = SDL_getenv("HWVX_MODS_LOG") != NULL;
    if (Port_ModsScan() > 0) {
        int i;

        Port_Log("mods: %d in the mods folder, %s", s_numMods, Port_ModsActive() ? "some on" : "all off");
        for (i = 0; s_logValues && i < s_numMods; i++) {
            Port_Log("mods: %d. %s%s", i + 1, s_mods[i].info.id, Enabled(&s_mods[i]) ? "" : " (off)");
        }
    }
}


/* Sets lock member `key` to string `s` (or removes it for NULL): 1 if it changed. */
static int SetStr(JsonNode *e, const char *key, const char *s) {
    if (s == NULL) {
        return Json_RemoveMember(e, key);
    }
    if (Same(Json_Get(e, key), s)) {
        return 0;
    }
    Json_RemoveMember(e, key);
    Json_Append(e, NewString(s), key);
    return 1;
}

static int SetBool(JsonNode *e, const char *key, int v) {
    const JsonNode *n = Json_Get(e, key);

    if (n != NULL && n->type == JSON_BOOL && n->boolean == v) {
        return 0;
    }
    Json_RemoveMember(e, key);
    Json_Append(e, Json_NewBool(v), key);
    return 1;
}

void Port_ModsSyncLock(const char *path) {
    static const char *const kinds[] = { "cars", "items", "tracks" };
    size_t len;
    char *text = SDL_LoadFile(path, &len), err[160];
    JsonNode *lock = text != NULL ? Json_Parse(text, len, err, sizeof(err)) : NULL;
    int changed = 0, any = 0, k, i;

    SDL_free(text);
    if (text != NULL && (lock == NULL || lock->type != JSON_OBJECT)) {
        Port_Log("mods: %s isn't readable; the mods' entries aren't added to it", path);
        Json_Free(lock);
        return;
    }
    if (lock == NULL && (lock = Json_NewObject()) != NULL) {
        Json_Append(lock, Json_NewInt(1), "format_version");
    }
    for (k = 0; lock != NULL && k < (int)SDL_arraysize(kinds); k++) {
        JsonNode *sec = Json_ObjectMember(lock, kinds[k]), *e;

        /* entries of mods that are off or gone wait for them */
        for (e = sec->child; e != NULL; e = e->next) {
            const char *mod = Str(e, "mod");

            if (mod != NULL) {
                any = 1;
                if ((i = IndexOfId(mod)) < 0 || !Enabled(&s_mods[i]) ||
                    Json_Get(Json_Get(s_mods[i].doc, kinds[k]), e->key) == NULL) {
                    changed |= SetBool(e, "active", 0);
                }
            }
        }
        for (i = 0; i < s_numMods; i++) {
            const JsonNode *entries = Json_Get(s_mods[i].doc, kinds[k]), *x;

            if (!Enabled(&s_mods[i])) {
                continue;
            }
            for (x = entries != NULL && entries->type == JSON_OBJECT ? entries->child : NULL; x != NULL; x = x->next) {
                JsonNode *e2 = (JsonNode *)Json_Get(sec, x->key);
                int j, later = 0;

                any = 1;
                /* brought by a mod later in the load order too: that one's is used */
                for (j = i + 1; j < s_numMods && !later; j++) {
                    later = Enabled(&s_mods[j]) && Json_Get(Json_Get(s_mods[j].doc, kinds[k]), x->key) != NULL;
                }
                if (later) {
                    continue;
                }
                if (e2 == NULL) {
                    e2 = Json_ObjectMember(sec, x->key);
                    changed = 1;
                }
                changed |= SetStr(e2, "alias", Str(x, "extends"));
                changed |= SetStr(e2, "name", Str(x, "name"));
                changed |= SetStr(e2, "thumbnail", Str(x, "thumbnail"));
                if (Str(x, "worldPack") != NULL && SafeName(Str(x, "worldPack"))) {
                    /* a world pack of the mod's own (built on this machine: tools/xdl_level_pack.py):
                     * "@mod\<id>\<file without its extension>", found by Port_ModsResolveFile */
                    char *w = NULL, *dot, *p;

                    SDL_asprintf(&w, "@mod\\%s\\%s", s_mods[i].info.id + (SDL_strncmp(s_mods[i].info.id, "uid://", 6) == 0 ? 6 : 0),
                                 Str(x, "worldPack"));
                    if (w != NULL) {
                        if ((dot = SDL_strrchr(w, '.')) != NULL && SDL_strchr(dot, '\\') == NULL) {
                            *dot = 0;
                        }
                        for (p = w; *p; p++) {
                            if (*p == '/') {
                                *p = '\\';
                            }
                        }
                        changed |= SetStr(e2, "world", w);
                        SDL_free(w);
                    }
                } else {
                    changed |= SetStr(e2, "world", Str(x, "world"));   /* tracks */
                }
                changed |= SetStr(e2, "mod", s_mods[i].info.id);
                changed |= SetBool(e2, "unlocked", Json_GetBool(x, "unlocked", 0));
                changed |= SetBool(e2, "active", 1);
            }
        }
    }
    if (any && changed) {
        WriteJson(path, lock, NULL);
        Port_Log("mods: %s brought up to date with the mods' entries", path);
    }
    Json_Free(lock);
}

/* ---- patches ------------------------------------------------------------- */

static void *Ptr(u32 h) {
    return h != 0 ? Port_HandleToPtr(h) : NULL;
}

/* Is [p, p + size) inside the pack's data blob? Every object a patch reads or
 * writes is. */
static int InBlob(const DataPack *pack, const void *p, u32 size) {
    const u8 *lo = GETP(pack->base), *q = p;

    return lo != NULL && q != NULL && q >= lo && (size_t)(q - lo) <= pack->dataSize &&
           size <= pack->dataSize - (u32)(q - lo);
}

/* The struct of a pack type's root objects (tools/xdl_mod.py ROOT_KIND): car
 * and item packs only; others have no root table. */
static const ModStruct *RootStruct(unsigned type) {
    const char *n = type == 0 ? "CarAInfo" : type == 2 ? "ItemRecord" : NULL;

    return n != NULL ? StructNamed(n, SDL_strlen(n)) : NULL;
}

/* A root object of a car or item pack (the table at 0: a count, then 8-byte
 * entries whose first word points to the root), all of it in the blob. */
static u8 *Root(const DataPack *pack, const ModStruct *st, u32 i) {
    u8 *base = GETP(pack->base), *tab, *obj;
    u32 n;

    if (base == NULL || pack->dataSize < 8) {
        return NULL;
    }
    n = *(u32 *)base;
    if (i >= n || n > pack->dataSize / 8 || (tab = Ptr(*(u32 *)(base + 4))) == NULL || !InBlob(pack, tab, 8 * n)) {
        return NULL;
    }
    obj = Ptr(*(u32 *)(tab + 8 * i));
    return InBlob(pack, obj, st->size) ? obj : NULL;
}

/* A path's number: digits only, up to the next '/' or the end. */
static int Index(const char *s, u32 *out) {
    char *end;
    unsigned long v;

    if (*s < '0' || *s > '9') {
        return 0;
    }
    v = SDL_strtoul(s, &end, 10);
    if ((*end != '/' && *end != 0) || v > 0xFFFFFFFFul) {
        return 0;
    }
    *out = (u32)v;
    return 1;
}

/* The objects link fd of obj points to: their count (1 unless the schema names
 * a count for it), all of them in the blob; NULL when there are none. */
static u8 *Follow(const DataPack *pack, const u8 *obj, const ModField *fd, u32 *count) {
    const ModStruct *to = &s_structs[fd->target];
    u8 *p = Ptr(*(u32 *)(obj + fd->offset));

    *count = fd->count > 0 ? *(const u32 *)(obj + fd->count - 1) : 1;
    if (*count == 0 || to->size == 0 || *count > pack->dataSize / to->size || !InBlob(pack, p, *count * to->size)) {
        return NULL;
    }
    return p;
}

static double GetValue(const u8 *p, int type) {
    switch (type) {
    case MF_U8: return *p;
    case MF_S8: return *(const s8 *)p;
    case MF_U16: return *(const u16 *)p;
    case MF_S16: return *(const s16 *)p;
    case MF_U32: return *(const u32 *)p;
    case MF_S32: return *(const s32 *)p;
    case MF_F32: return *(const f32 *)p;
    default: return 0;
    }
}

static int s_clamped;   /* SetValue: the value didn't fit the field's type */

/* 1 if set; the value clamped to the type's range (s_clamped) when it doesn't fit. */
static int SetValue(u8 *p, int type, const JsonNode *v) {
    static const double lo[] = { 0, -128, 0, -32768, 0, -2147483648.0, -3.4028234663852886e38 };
    static const double hi[] = { 255, 127, 65535, 32767, 4294967295.0, 2147483647.0, 3.4028234663852886e38 };
    double d;

    s_old = GetValue(p, type);
    s_clamped = 0;
    if (v->type == JSON_BOOL) {
        d = v->boolean;
    } else if (v->type == JSON_NUMBER) {
        d = v->isInteger ? (double)v->integer : v->number;
    } else {
        return 0;
    }
    if (type < MF_U8 || type > MF_F32) {
        return 0;
    }
    if (!(d >= lo[type])) {     /* NaN too */
        d = d != d ? 0 : lo[type];
        s_clamped = 1;
    } else if (d > hi[type]) {
        d = hi[type];
        s_clamped = 1;
    }
    switch (type) {
    case MF_U8: *p = (u8)d; break;
    case MF_S8: *(s8 *)p = (s8)d; break;
    case MF_U16: *(u16 *)p = (u16)d; break;
    case MF_S16: *(s16 *)p = (s16)d; break;
    case MF_U32: *(u32 *)p = (u32)d; break;
    case MF_S32: *(s32 *)p = (s32)d; break;
    case MF_F32: *(f32 *)p = (f32)d; break;
    default: return 0;
    }
    return 1;
}

static int TypeSize(int type) {
    return type == MF_U8 || type == MF_S8 ? 1 : type == MF_U16 || type == MF_S16 ? 2 : 4;
}

/* A merge patch over an object of struct st (in the blob): the fields set. */
static int PatchObject(const Mod *m, const DataPack *pack, const char *where, u8 *obj, const ModStruct *st,
                       const JsonNode *patch) {
    const JsonNode *f;
    int n = 0;

    for (f = patch->child; f != NULL; f = f->next) {
        const ModField *fd = FieldNamed(st, f->key, SDL_strlen(f->key));

        if (fd == NULL || fd->type == MF_OTHER ||
            fd->offset + (u32)(fd->type == MF_LINK ? 4 : TypeSize(fd->type) * (fd->count > 0 ? fd->count : 1)) >
                st->size) {
            Report("%s: %s: %s.%s isn't a field a patch can set", m->info.name, where, st->name, f->key);
        } else if (fd->type == MF_LINK) {
            u32 count;
            u8 *to = Follow(pack, obj, fd, &count);

            if (f->type == JSON_OBJECT && to != NULL) {
                n += PatchObject(m, pack, where, to, &s_structs[fd->target], f);
            } else if (f->type == JSON_OBJECT) {
                Report("%s: %s: %s.%s points to nothing in this release's pack", m->info.name, where, st->name,
                       f->key);
            } else {
                Report("%s: %s: %s.%s is an object: patch it as one", m->info.name, where, st->name, f->key);
            }
        } else if (fd->count > 0) {
            const JsonNode *e = f->type == JSON_ARRAY ? f->child : NULL;
            int i, clamped = 0;

            for (i = 0; e != NULL && i < fd->count; i++, e = e->next) {
                n += e->type != JSON_NULL && SetValue(obj + fd->offset + i * TypeSize(fd->type), fd->type, e);
                clamped |= s_clamped;
            }
            if (clamped) {
                Report("%s: %s: %s.%s: a value out of its type's range, clamped", m->info.name, where, st->name,
                       f->key);
            }
        } else if (SetValue(obj + fd->offset, fd->type, f)) {
            n++;
            if (s_clamped) {
                Report("%s: %s: %s.%s: out of its type's range, clamped to %g", m->info.name, where, st->name,
                       f->key, GetValue(obj + fd->offset, fd->type));
            }
            if (s_logValues) {
                Port_Log("mods: %s %s.%s = %g (was %g)", where, st->name, f->key, GetValue(obj + fd->offset, fd->type),
                         s_old);
            }
        } else if (f->type != JSON_NULL) {
            Report("%s: %s: %s.%s takes a number", m->info.name, where, st->name, f->key);
        }
    }
    return n;
}

/* path: Root/i[/field or index...] in a car or item pack, Root its root struct;
 * an index only into an array the schema gives a count for (else only 0). The
 * object (in the blob) and its struct. */
static u8 *Locate(const DataPack *pack, unsigned type, const char *path, const ModStruct **st) {
    const char *p = SDL_strchr(path, '/'), *seg;
    const ModStruct *root = RootStruct(type);
    u8 *obj, *array;
    u32 count = 1, i;

    if (p == NULL || root == NULL || (*st = StructNamed(path, (size_t)(p - path))) != root || !Index(p + 1, &i)) {
        return NULL;
    }
    obj = array = Root(pack, root, i);
    for (seg = SDL_strchr(p + 1, '/'); obj != NULL && seg != NULL; seg = SDL_strchr(seg + 1, '/')) {
        const char *end = SDL_strchr(seg + 1, '/');
        size_t len = end != NULL ? (size_t)(end - seg - 1) : SDL_strlen(seg + 1);
        const ModField *fd;

        if (seg[1] >= '0' && seg[1] <= '9') {
            if (!Index(seg + 1, &i) || i >= count) {
                return NULL;
            }
            obj = array + i * (*st)->size;
            count = 1;
            array = obj;
        } else if ((fd = FieldNamed(*st, seg + 1, len)) != NULL && fd->type == MF_LINK &&
                   fd->offset + 4 <= (*st)->size) {
            obj = array = Follow(pack, obj, fd, &count);
            *st = &s_structs[fd->target];
        } else {
            return NULL;
        }
    }
    return obj;
}

static void ApplyPatch(const Mod *m, const DataPack *pack, unsigned type, const char *id, const char *path,
                       const JsonNode *patch) {
    const ModStruct *st = NULL;
    u8 *obj = Locate(pack, type, path, &st);

    if (obj == NULL || patch->type != JSON_OBJECT) {
        Report("%s: %s: no such object in this release's pack", m->info.name, id);
        return;
    }
    Port_Log("mods: %s: %d values of %s", m->info.name, PatchObject(m, pack, id, obj, st, patch), id);
}

/* ---- recipes ------------------------------------------------------------- */

static void Mul(f32 a[16], const f32 b[16]) {   /* a = b @ a */
    f32 r[16];
    int i, j, k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            r[i * 4 + j] = 0;
            for (k = 0; k < 4; k++) {
                r[i * 4 + j] += b[i * 4 + k] * a[k * 4 + j];
            }
        }
    }
    SDL_memcpy(a, r, sizeof(r));
}

static int RecipeMatrix(const Mod *m, const char *where, const JsonNode *ops, f32 out[12]) {
    f32 a[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const JsonNode *op;

    for (op = ops != NULL && ops->type == JSON_ARRAY ? ops->child : NULL; op != NULL; op = op->next) {
        const char *k = Str(op, "op");
        f32 n[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        int i, j;

        if (k == NULL) {
            continue;
        } else if (SDL_strcmp(k, "hue_shift") == 0) {
            f32 t = (f32)(Json_GetNumber(op, "degrees", 0) * SDL_PI_D / 180), c = SDL_cosf(t), s = SDL_sinf(t);
            f32 r = SDL_sqrtf(1.0f / 3), sk[9] = { 0, -1, 1, 1, 0, -1, -1, 1, 0 };

            for (i = 0; i < 3; i++) {
                for (j = 0; j < 3; j++) {
                    n[i * 4 + j] = (i == j ? c : 0) + (1 - c) / 3 + s * r * sk[i * 3 + j];
                }
            }
        } else if (SDL_strcmp(k, "tint") == 0) {
            const JsonNode *c = Json_Get(op, "color");

            for (i = 0; i < 3; i++) {
                const JsonNode *v = Json_At(c, i);

                n[i * 5] = v != NULL && v->type == JSON_NUMBER ? (f32)v->number : 1;
            }
        } else if (SDL_strcmp(k, "saturation") == 0 || SDL_strcmp(k, "brightness") == 0) {
            f32 amt = (f32)Json_GetNumber(op, "amount", 1);
            int sat = k[0] == 's';

            for (i = 0; i < 3; i++) {
                for (j = 0; j < 3; j++) {
                    n[i * 4 + j] = (i == j ? amt : 0) + (sat ? (1 - amt) / 3 : 0);
                }
            }
        } else if (SDL_strcmp(k, "matrix") == 0) {
            const JsonNode *rows = Json_Get(op, "rows");

            for (i = 0; i < 3; i++) {
                for (j = 0; j < 4; j++) {
                    const JsonNode *v = Json_At(Json_At(rows, i), j);

                    n[i * 4 + j] = v != NULL && v->type == JSON_NUMBER ? (f32)v->number : n[i * 4 + j];
                }
            }
        } else {
            Report("%s: %s: no recipe op %s", m->info.name, where, k);
            continue;
        }
        Mul(a, n);
    }
    SDL_memcpy(out, a, 12 * sizeof(f32));
    return 1;
}

/* tex: N or release:N (texture N of that release's pack only). */
static void ApplyRecipe(const Mod *m, DataPack *pack, const char *id, const char *tex, const JsonNode *ops) {
    static const char *const names[] = { "gc", "pc", "ps2" };
    const char *colon = SDL_strchr(tex, ':');
    u32 i;
    u16 *ids = GETP(pack->textureIDs);
    void *image;
    f32 mat[12];

    if (colon != NULL) {
        if ((size_t)(colon - tex) != SDL_strlen(names[ReleaseOf(pack)]) ||
            SDL_strncmp(tex, names[ReleaseOf(pack)], (size_t)(colon - tex)) != 0) {
            return;
        }
        tex = colon + 1;
    }
    i = (u32)SDL_strtoul(tex, NULL, 10);
    if (ids == NULL || i >= pack->numTextures || (image = Gfx_PortTextureImage(ids[i])) == NULL) {
        Report("%s: %s: no such texture in this release's pack", m->info.name, id);
        return;
    }
    RecipeMatrix(m, id, ops, mat);
    GX_TexModSet(image, mat, pack);
}

/* ---- packs --------------------------------------------------------------- */

/* base://<archive>/<type>_<id>/<rest>: does it name this pack? *rest after it. */
static int Names(const char *link, const char *name, unsigned type, unsigned id, const char **rest) {
    const char *a, *p, *u;
    size_t tl;
    char *end;

    if (SDL_strncmp(link, "base://", 7) != 0 || type >= SDL_arraysize(s_typeNames)) {
        return 0;
    }
    a = link + 7;
    if ((p = SDL_strchr(a, '/')) == NULL || (size_t)(p - a) != SDL_strlen(name) ||
        SDL_strncasecmp(a, name, (size_t)(p - a)) != 0) {
        return 0;
    }
    tl = SDL_strlen(s_typeNames[type]);
    if (SDL_strncmp(p + 1, s_typeNames[type], tl) != 0 || p[1 + tl] != '_') {
        return 0;
    }
    u = p + 2 + tl;
    if (SDL_strtoul(u, &end, 10) != id || end == u || (*end != '/' && *end != 0)) {
        return 0;
    }
    *rest = *end == '/' ? end + 1 : end;
    return 1;
}

void Port_ModsPackLoaded(void *dataPack, const char *name, unsigned type, unsigned id) {
    DataPack *pack = dataPack;
    int i, k;

    if (pack == NULL || name == NULL || !Port_ModsActive()) {
        return;
    }
    for (i = 0; i < s_numMods; i++) {
        const Mod *m = &s_mods[i];
        const JsonNode *sec, *x;
        const char *rest;

        if (!Enabled(m) || !ForRelease(m, pack)) {
            continue;
        }
        sec = Json_Get(m->doc, "patches");
        for (x = sec != NULL && sec->type == JSON_OBJECT ? sec->child : NULL; x != NULL; x = x->next) {
            if (Names(x->key, name, type, id, &rest) && *rest != 0) {
                ApplyPatch(m, pack, type, x->key, rest, x);
            }
        }
        sec = Json_Get(m->doc, "recipes");
        for (x = sec != NULL && sec->type == JSON_OBJECT ? sec->child : NULL; x != NULL; x = x->next) {
            if (Names(x->key, name, type, id, &rest) && SDL_strncmp(rest, "texture/", 8) == 0) {
                ApplyRecipe(m, pack, x->key, rest + 8, x);
            }
        }
        /* the mod's own entries: their packs load under their numbers */
        for (k = 0; k < 2; k++) {
            int kind = k == 0 ? PORT_CONTENT_CAR : PORT_CONTENT_ITEM;
            const JsonNode *e;
            u32 n;

            if (type != (k == 0 ? 0u : 2u)) {
                continue;
            }
            sec = Json_Get(m->doc, k == 0 ? "cars" : "items");
            for (e = sec != NULL && sec->type == JSON_OBJECT ? sec->child : NULL; e != NULL; e = e->next) {
                const JsonNode *p;

                if (!Port_ContentIndexOf(kind, e->key, &n) || (k == 0 ? n : Port_ContentItemPack(n)) != id) {
                    continue;
                }
                p = Json_Get(e, "patch");
                for (x = p != NULL && p->type == JSON_OBJECT ? p->child : NULL; x != NULL; x = x->next) {
                    ApplyPatch(m, pack, type, e->key, x->key, x);
                }
                p = Json_Get(e, "recipes");
                for (x = p != NULL && p->type == JSON_OBJECT ? p->child : NULL; x != NULL; x = x->next) {
                    if (SDL_strncmp(x->key, "texture/", 8) == 0) {
                        ApplyRecipe(m, pack, e->key, x->key + 8, x);
                    }
                }
            }
        }
    }
}

void Port_ModsPackReleased(void *dataPack) {
    Port_LevelPackReleased(dataPack);   /* a level's own records go with its world */
    if (g_gxTexMods != 0) {
        GX_TexModClear(dataPack);
    }
}

/* ---- levels ---------------------------------------------------------------- */

/* A track entry's level file (its "level", in its mod's folder), for the
 * enabled mod that brings the entry last; NULL if none. */
char *Port_ModsTrackLevel(const char *trackId) {
    char *path = NULL;
    int i;

    if (trackId == NULL) {
        return NULL;
    }
    for (i = s_numMods - 1; i >= 0 && path == NULL; i--) {
        const char *level = Str(Json_Get(Json_Get(s_mods[i].doc, "tracks"), trackId), "level");

        if (!Enabled(&s_mods[i]) || level == NULL) {
            continue;
        }
        if (!SafeName(level)) {
            Report("%s: track %s: level %s isn't a file in the mod", s_mods[i].info.name, trackId, level);
            continue;
        }
        SDL_asprintf(&path, "%s%s", s_mods[i].dir, level);
    }
    return path;
}

/* A file of a mod's own that the game opens by name: "@mod\\<id>\\<path>.xgc" (a track's
 * "worldPack"); the release's extension (.xgc, .xpc, .xps) found beside it. The host path
 * (the caller frees it) and *format (PORT_DATA_*), or NULL. */
char *Port_ModsResolveFile(const char *name, int *format) {
    static const char *const ext[3] = { ".xgc", ".xpc", ".xps" };
    const char *rest, *slash;
    char id[64], *rel, *p, *path = NULL;
    int i, k;
    size_t n;

    while (*name == '/' || *name == '\\') {
        name++;
    }
    if (SDL_strncmp(name, "@mod", 4) != 0 || (name[4] != '\\' && name[4] != '/')) {
        return NULL;
    }
    rest = name + 5;
    slash = rest;
    while (*slash && *slash != '\\' && *slash != '/') {
        slash++;
    }
    if (*slash == 0 || (size_t)(slash - rest) >= sizeof(id)) {
        return NULL;
    }
    SDL_memcpy(id, rest, (size_t)(slash - rest));
    id[slash - rest] = 0;
    rel = SDL_strdup(slash + 1);
    if (rel == NULL) {
        return NULL;
    }
    for (p = rel; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    n = SDL_strlen(rel);
    if (n > 4 && SDL_strcasecmp(rel + n - 4, ".xgc") == 0) {
        rel[n - 4] = 0;
    }
    for (i = 0; i < s_numMods && path == NULL; i++) {
        const char *mid = s_mods[i].info.id;

        if (!Enabled(&s_mods[i]) || mid == NULL) {
            continue;
        }
        if (SDL_strncmp(mid, "uid://", 6) == 0) {
            mid += 6;
        }
        if (SDL_strcmp(mid, id) != 0 || !SafeName(rel)) {
            continue;
        }
        for (k = 0; k < 3 && path == NULL; k++) {
            char *cand = NULL;
            SDL_PathInfo info;

            SDL_asprintf(&cand, "%s%s%s", s_mods[i].dir, rel, ext[k]);
            if (cand != NULL && SDL_GetPathInfo(cand, &info) && info.type == SDL_PATHTYPE_FILE) {
                path = cand;
                *format = k;
            } else {
                SDL_free(cand);
            }
        }
    }
    SDL_free(rel);
    return path;
}

/* ---- music ------------------------------------------------------------------ */

/* One "music" entry: "file" or {"file", "intro", "loopStart", "loopEnd", "loop", "volume"}:
 * its tracks into t (an intro is a track of its own, first), how many (0 when broken). */
static int MusicTrack(const Mod *m, const char *key, const JsonNode *e, PortModMusic *t) {
    const char *file = e->type == JSON_STRING ? e->string : e->type == JSON_OBJECT ? Str(e, "file") : NULL;
    const char *intro = e->type == JSON_OBJECT ? Str(e, "intro") : NULL;
    const JsonNode *n;
    int k = intro != NULL;

    SDL_memset(t, 0, sizeof(*t) * (size_t)(k + 1));
    if (file == NULL || !SafeName(file) || (intro != NULL && !SafeName(intro))) {
        Report("%s: music %s: not a file of the mod", m->info.name, key);
        return 0;
    }
    SDL_asprintf(&t[k].path, "%s%s", m->dir, file);
    t[k].loopStart = t[k].loopEnd = -1;
    t[k].loop = 1;
    t[k].volume = 1.0f;
    if (e->type == JSON_OBJECT) {
        if ((n = Json_Get(e, "loopStart")) != NULL && n->type == JSON_NUMBER && n->number >= 0) {
            t[k].loopStart = (long long)n->number;
        }
        if ((n = Json_Get(e, "loopEnd")) != NULL && n->type == JSON_NUMBER && n->number > 0) {
            t[k].loopEnd = (long long)n->number;
        }
        if ((n = Json_Get(e, "loop")) != NULL && n->type == JSON_BOOL && !n->boolean) {
            t[k].loop = 0;
        }
        if ((n = Json_Get(e, "volume")) != NULL && n->type == JSON_NUMBER) {
            t[k].volume = (float)SDL_clamp(n->number, 0.0, 4.0);
        }
    }
    if (intro != NULL) {        /* played once, at the track's volume, straight into it */
        SDL_asprintf(&t[0].path, "%s%s", m->dir, intro);
        t[0].loopStart = t[0].loopEnd = -1;
        t[0].volume = t[1].volume;
        t[0].intro = 1;
    }
    if (t[0].path == NULL || t[k].path == NULL) {
        SDL_free(t[0].path);
        SDL_free(t[k].path);
        return 0;
    }
    return k + 1;
}

int Port_ModsMusic(const char *name, unsigned type, unsigned id, PortModMusic **out) {
    int i;

    *out = NULL;
    if (name == NULL || !Port_ModsActive()) {
        return 0;
    }
    for (i = s_numMods - 1; i >= 0; i--) {     /* the later mod's applies */
        const Mod *m = &s_mods[i];
        const JsonNode *sec = Json_Get(m->doc, "music"), *x, *e;
        const char *rest;

        if (!Enabled(m)) {
            continue;
        }
        for (x = sec != NULL && sec->type == JSON_OBJECT ? sec->child : NULL; x != NULL; x = x->next) {
            int n = 0, k = 0;
            PortModMusic *t;

            if (!Names(x->key, name, type, id, &rest) || (*rest != 0 && SDL_strcmp(rest, "song/0") != 0)) {
                continue;
            }
            if (x->type == JSON_ARRAY) {      /* a playlist */
                for (e = x->child; e != NULL; e = e->next) {
                    n++;
                }
            } else {
                n = 1;
            }
            t = SDL_calloc((size_t)(n ? n : 1) * 2, sizeof(*t));     /* each may have an intro */
            if (t == NULL) {
                return 0;
            }
            for (e = x->type == JSON_ARRAY ? x->child : x; e != NULL && k < 2 * n; e = x->type == JSON_ARRAY ? e->next : NULL) {
                k += MusicTrack(m, x->key, e, &t[k]);
            }
            if (k == 0) {
                SDL_free(t);
                return 0;
            }
            *out = t;
            return k;
        }
    }
    return 0;
}

void Port_ModsMusicFree(PortModMusic *tracks, int count) {
    int i;

    for (i = 0; tracks != NULL && i < count; i++) {
        SDL_free(tracks[i].path);
    }
    SDL_free(tracks);
}

/* ---- sounds ------------------------------------------------------------------ */

/* A mod's "sounds": { "base://HW_Cars/car_5/sound/3": "sounds/horn.ogg" } or
 * { ...: { "file": "sounds/horn.ogg", "loop": 1200 } } (loop in the file's
 * samples, null: once). The file (WAV, FLAC, Ogg Vorbis, MP3) is decoded here,
 * on the player's machine (src/audiodec), mixed to mono and resampled to the
 * base sound's rate; its loop is the file's tag, else "loop", else (the base
 * sound loops) the whole sound. The pack's own sound is replaced as a mixed
 * build replaces one (PortMix_SFXSet). A pack with mod sounds skips the .xdl
 * cache. */
typedef struct SndPack {
    char archive[0x50];
    u32 type, id;
} SndPack;

static _Thread_local SndPack s_sndPack[8];
static _Thread_local int s_sndDepth;

void Port_ModsBeginPack(const char *name, unsigned type, unsigned id) {
    const char *stem = name, *c;

    if (++s_sndDepth > 8 || name == NULL) {
        return;
    }
    for (c = name; *c; c++) {
        if (*c == '/' || *c == '\\') {
            stem = c + 1;
        }
    }
    SDL_snprintf(s_sndPack[s_sndDepth - 1].archive, sizeof(s_sndPack[0].archive), "%s", stem);
    s_sndPack[s_sndDepth - 1].type = type;
    s_sndPack[s_sndDepth - 1].id = id;
}

void Port_ModsEndPack(void) {
    if (s_sndDepth > 0) {
        s_sndDepth--;
    }
}

static const SndPack *CurPack(void) {
    return s_sndDepth > 0 && s_sndDepth <= 8 ? &s_sndPack[s_sndDepth - 1] : NULL;
}

/* "sound/<i>" or "sound/<release>:<i>" of this release: the index, or -1. */
static long SoundIndex(const char *rest, int fmt) {
    static const char *const names[] = { "gc", "pc", "ps2" };
    const char *colon;
    char *end;
    unsigned long i;

    if (SDL_strncmp(rest, "sound/", 6) != 0) {
        return -1;
    }
    rest += 6;
    colon = SDL_strchr(rest, ':');
    if (colon != NULL) {
        int r = fmt == PORT_DATA_PC ? 1 : fmt == PORT_DATA_PS2 ? 2 : 0;

        if ((size_t)(colon - rest) != SDL_strlen(names[r]) || SDL_strncmp(rest, names[r], (size_t)(colon - rest)) != 0) {
            return -1;
        }
        rest = colon + 1;
    }
    i = SDL_strtoul(rest, &end, 10);
    return end != rest && *end == 0 && i < 0x10000 ? (long)i : -1;
}

int Port_ModsPackSounds(void) {
    const SndPack *p = CurPack();
    int i;

    if (p == NULL || !Port_ModsActive()) {
        return 0;
    }
    for (i = 0; i < s_numMods; i++) {
        const JsonNode *sec = Json_Get(s_mods[i].doc, "sounds"), *x;
        const char *rest;

        if (!Enabled(&s_mods[i])) {
            continue;
        }
        for (x = sec != NULL && sec->type == JSON_OBJECT ? sec->child : NULL; x != NULL; x = x->next) {
            if (Names(x->key, p->archive, p->type, p->id, &rest) && SDL_strncmp(rest, "sound/", 6) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static u32 Rd32Fmt(const u8 *p, int be) {
    return be ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3] : Le32(p);
}

static void ModSound(const Mod *m, const char *id, const JsonNode *v, s32 fmt, const u8 *desc, u32 count, u32 i) {
    const char *file = v->type == JSON_STRING ? v->string : Str(v, "file");
    const JsonNode *loopNode = v->type == JSON_OBJECT ? Json_Get(v, "loop") : NULL;
    const u8 *d = desc + i * 0x20;
    int be = fmt == PORT_DATA_GC;
    u32 baseLen = Rd32Fmt(d + 4, be), baseLoop = Rd32Fmt(d + 8, be), rate = Rd32Fmt(d + 0xC, be), n;
    AudioDecoded a;
    char err[160], *path = NULL;
    int16_t *mono, *pcm;
    int64_t loop, end;

    if (file == NULL || !SafeName(file)) {
        Report("%s: %s: needs a file of the mod (\"file\")", m->info.name, id);
        return;
    }
    if (i >= count) {
        Report("%s: %s: this release's pack has %u sounds", m->info.name, id, (unsigned)count);
        return;
    }
    if (rate < 1000 || rate > 192000 || baseLen == 0) {
        Report("%s: %s: the base sound has no usable rate", m->info.name, id);
        return;
    }
    if (SDL_asprintf(&path, "%s%s", m->dir, file) < 0 || !AudioDec_File(path, &a, err, sizeof(err))) {
        Report("%s: %s: %s %s", m->info.name, id, file, path != NULL ? err : "out of memory");
        SDL_free(path);
        return;
    }
    SDL_free(path);
    loop = a.loopStart;
    end = a.loopEnd;
    if (loop < 0 && loopNode != NULL && loopNode->type == JSON_NUMBER && loopNode->number >= 0 &&
        loopNode->number < a.frames) {
        loop = (int64_t)loopNode->number;
    } else if (loop < 0 && loopNode == NULL && baseLoop != 0xFFFFFFFFu) {
        loop = 0;      /* the base loops and nothing says where: the whole sound */
    }
    mono = AudioDec_Mono(&a);
    pcm = mono != NULL ? AudioDec_Resample(mono, a.frames, a.rate, rate, &n) : NULL;
    if (pcm != NULL && a.rate != rate) {
        loop = AudioDec_ScalePos(loop, a.rate, rate);
        end = AudioDec_ScalePos(end, a.rate, rate);
    }
    free(mono);
    AudioDec_Free(&a);
    /* at most 16M samples (32 MB of PCM16, 6 minutes at 44.1 kHz): half the 64 MB sound
     * memory (Audio_.c) a pack's sounds share, so a longer one could never be created; and a
     * silent file of a few bytes on disk decodes to that much (PortMix_SFX sums them) */
    if (pcm == NULL || n == 0 || n > 0x1000000) {
        free(pcm);
        Report("%s: %s: %s can't be converted (at most 16M samples once resampled)", m->info.name, id, file);
        return;
    }
    if (loop >= n) {
        loop = -1;
    }
    if (loop >= 0 && end > loop && end < n) {
        n = (u32)end;          /* the game loops from the loop start to the sample's end */
    }
    PortMix_SFXSet(i, count, (u8 *)pcm, n * 2, loop >= 0 ? (u32)loop * 2 : 0xFFFFFFFFu, rate);
    if (s_logValues) {
        Port_Log("mods: %s: %s: %s, %u samples at %u Hz, loop %lld", m->info.name, id, file, (unsigned)n,
                 (unsigned)rate, (long long)loop);
    }
}

void Port_ModsSFX(int fmt, const void *desc, unsigned count) {
    const SndPack *p = CurPack();
    int i;

    if (p == NULL || desc == NULL || !Port_ModsActive()) {
        return;
    }
    for (i = 0; i < s_numMods; i++) {     /* in load order: a later mod's sound replaces an earlier one's */
        const Mod *m = &s_mods[i];
        const JsonNode *sec = Json_Get(m->doc, "sounds"), *x;
        const char *rest;
        DataPack fake;

        if (!Enabled(m)) {
            continue;
        }
        fake.portFormat = (u8)fmt;
        if (!ForRelease(m, &fake)) {
            continue;
        }
        for (x = sec != NULL && sec->type == JSON_OBJECT ? sec->child : NULL; x != NULL; x = x->next) {
            long k;

            if (Names(x->key, p->archive, p->type, p->id, &rest) && (k = SoundIndex(rest, fmt)) >= 0) {
                ModSound(m, x->key, x, fmt, desc, count, (u32)k);
            }
        }
    }
}
