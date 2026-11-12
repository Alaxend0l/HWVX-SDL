/* Mods over the player's own game data (Port_Mods.h; docs/xdl-format
 * README §8, §10.3). Port-only file.
 *
 * - The list: every folder of the mods folder with a mod.jsonc, in the
 *   order mods.jsonc gives ("order", "disabled"), then the others by id,
 *   then each moved behind the mods its "after" names.
 * - Entries (cars, items): an enabled mod's entries go into ids.lock with
 *   their alias, name and "mod" (Port_ModsSyncLock), and the registry
 *   numbers and loads them as it does any lock entry. A mod that's off or
 *   gone leaves its entries "active": false, their numbers kept.
 * - Patches: RFC 7396 merge patches on objects of a loaded pack, by object
 *   id (base://HW_Cars/car_7/CarAInfo/0/accel; an entry's own as
 *   CarAInfo/0/accel), applied as the pack's pointers are fixed up, with the
 *   fields the schemas name (Port_ModFields.inc, tools/gen_mod_fields.py).
 * - Recipes: colour matrices on a pack's textures (gx_texmod.h), composed
 *   as tools/xdl_mod.py's recipe_matrix does.
 * - Packages: a .hwmod is a zip of stored entries the packer made
 *   (check.json in it); Port_ModsInstall unpacks one. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Content.h"
#include "Port_Mods.h"
#include "jsonc.h"
#include "gx_texmod.h"

enum { MF_U8, MF_S8, MF_U16, MF_S16, MF_U32, MF_S32, MF_F32, MF_LINK, MF_OTHER };

typedef struct ModField {
    const char *name;
    u32 offset;
    int type;
    int count;          /* 0: one value; else an array of count */
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

typedef struct Mod {
    PortModInfo info;
    char *dir;          /* its folder, with a trailing separator */
    JsonNode *doc;      /* mod.jsonc; info's strings point into it */
} Mod;

static Mod *s_mods;
static int s_numMods;
static char **s_report;
static int s_numReport, s_capReport;
static int s_inited;
static int s_logValues;     /* HWVX_MODS_LOG=1: each value a patch sets, and what it was */
static double s_old;

static const char *const s_typeNames[] = { "car", "interface", "item", "link", "world", "collision",
                                           "world_texture", "geometry", "share", "audio", "music" };

/* ---- small helpers ------------------------------------------------------- */

static void Report(const char *fmt, ...) PORT_PRINTF(1, 2);
static void Report(const char *fmt, ...) {
    va_list ap;
    char *line = NULL;

    va_start(ap, fmt);
    SDL_vasprintf(&line, fmt, ap);
    va_end(ap);
    if (line == NULL) {
        return;
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
    SDL_asprintf(&file, "%scheck.json", dir);
    m->info.checked = file != NULL && SDL_GetPathInfo(file, &pi);
    SDL_free(file);
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

/* mods.jsonc's order and disabled mods, then "after". */
static void Order(const char *dir) {
    char *path = NULL, *text, err[160];
    size_t len;
    JsonNode *cfg = NULL;
    const JsonNode *e;
    int placed = 0, i, pass;

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
    /* each behind the mods it names: as many passes as there are mods (a
     * cycle stops moving then) */
    for (pass = 0; pass < s_numMods; pass++) {
        int moved = 0;

        for (i = 0; i < s_numMods; i++) {
            const JsonNode *a = Json_Get(s_mods[i].doc, "after");
            int last = -1, j;

            for (a = a != NULL && a->type == JSON_ARRAY ? a->child : NULL; a != NULL; a = a->next) {
                if (a->type == JSON_STRING && (j = IndexOfId(a->string)) > last) {
                    last = j;
                }
            }
            if (last > i) {
                MoveMod(i, last);
                moved = 1;
                break;
            }
        }
        if (!moved) {
            break;
        }
    }
}

/* Conflicts and notes for the enabled mods. */
static void BuildReport(void) {
    static const char *const sections[] = { "patches", "recipes" };
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
        if (!m->info.checked) {
            Report("%s: put in by hand, not checked by the packer", m->info.name);
            m->info.problems++;
        }
        for (s = 0; s < 2; s++) {
            const JsonNode *a = Json_Get(m->doc, sections[s]), *x, *y;

            for (x = a != NULL && a->type == JSON_OBJECT ? a->child : NULL; x != NULL; x = x->next) {
                for (j = i + 1; j < s_numMods; j++) {
                    const JsonNode *b = Json_Get(s_mods[j].doc, sections[s]);

                    if (!Enabled(&s_mods[j]) || (y = Json_Get(b, x->key)) == NULL) {
                        continue;
                    }
                    if (s == 1) {
                        Report("%s and %s both recolour %s: %s's applies", m->info.name, s_mods[j].info.name, x->key,
                               s_mods[j].info.name);
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
        for (s = 0; s < 2; s++) {
            const char *kind = s == 0 ? "cars" : "items";
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

static SDL_EnumerationResult SDLCALL RemoveTree(void *userdata, const char *dirname, const char *fname) {
    char *path = NULL;
    SDL_PathInfo pi;

    (void)userdata;
    if (SDL_asprintf(&path, "%s%s", dirname, fname) < 0) {
        return SDL_ENUM_FAILURE;
    }
    if (SDL_GetPathInfo(path, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(path, RemoveTree, NULL);
    }
    SDL_RemovePath(path);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static int RemoveFolder(const char *dir) {
    SDL_PathInfo pi;

    if (!SDL_GetPathInfo(dir, &pi)) {
        return 1;
    }
    SDL_EnumerateDirectory(dir, RemoveTree, NULL);
    return SDL_RemovePath(dir);
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

int Port_ModsInstall(const char *package, char *err, size_t errSize) {
    size_t size = 0;
    u8 *zip = SDL_LoadFile(package, &size);
    const u8 *eocd = NULL, *cd;
    u32 count, cdOff, i, k;
    char *dir = Port_ModsDir(), *part = NULL, *final = NULL, *text = NULL;
    JsonNode *doc = NULL;
    const char *id;
    int ok = 0, hasCheck = 0, hasMod = 0, pass;

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
        SDL_snprintf(err, errSize, "not a .hwmod (zip) package");
        goto done;
    }
    count = Le16(eocd + 10);
    cdOff = Le32(eocd + 16);
    /* pass 0 checks every entry and reads mod.jsonc; pass 1 writes them */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0, cd = zip + cdOff; i < count; i++) {
            char name[256];
            u32 nlen, method, csize, lho, data;

            if ((size_t)(cd - zip) + 46 > size || Le32(cd) != 0x02014B50) {
                SDL_snprintf(err, errSize, "the package is damaged");
                goto done;
            }
            method = Le16(cd + 10);
            csize = Le32(cd + 20);
            nlen = Le16(cd + 28);
            lho = Le32(cd + 42);
            if (nlen >= sizeof(name) || (size_t)(cd - zip) + 46 + nlen > size || (size_t)lho + 30 > size) {
                SDL_snprintf(err, errSize, "the package is damaged");
                goto done;
            }
            SDL_memcpy(name, cd + 46, nlen);
            name[nlen] = 0;
            data = lho + 30 + Le16(zip + lho + 26) + Le16(zip + lho + 28);
            cd += 46 + nlen + Le16(cd + 30) + Le16(cd + 32);
            if (!SafeName(name) || method != 0 || (size_t)data + csize > size) {
                SDL_snprintf(err, errSize, "%s: %s", name, method != 0 ? "compressed (the packer stores entries)"
                                                                       : "not a name or place a package may use");
                goto done;
            }
            if (pass == 0) {
                if (SDL_strcmp(name, "check.json") == 0) {
                    hasCheck = 1;
                } else if (SDL_strcmp(name, "mod.jsonc") == 0) {
                    char e2[160];

                    hasMod = 1;
                    doc = Json_Parse((const char *)zip + data, csize, e2, sizeof(e2));
                }
            } else if (name[nlen - 1] != '/') {
                char *path = NULL, *slash;

                SDL_asprintf(&path, "%s%s", part, name);
                if (path == NULL) {
                    goto done;
                }
                if ((slash = SDL_strrchr(path, '/')) != NULL) {
                    *slash = 0;
                    SDL_CreateDirectory(path);
                    *slash = '/';
                }
                if (!SDL_SaveFile(path, zip + data, csize)) {
                    SDL_snprintf(err, errSize, "can't write %s: %s", path, SDL_GetError());
                    SDL_free(path);
                    goto done;
                }
                SDL_free(path);
            }
        }
        if (pass == 0) {
            id = Str(doc, "id");
            if (!hasCheck || !hasMod) {
                SDL_snprintf(err, errSize, "%s", !hasMod ? "no mod.jsonc in it" : "not made by the packer (no check.json)");
                goto done;
            }
            const char *c;

            for (c = id != NULL && SDL_strncmp(id, "uid://", 6) == 0 ? id + 6 : ""; (*c >= '0' && *c <= '9') ||
                 (*c >= 'a' && *c <= 'z'); c++) {
            }
            if (id == NULL || *c != 0 || c == id + 6) {
                SDL_snprintf(err, errSize, "mod.jsonc has no id this version reads");
                goto done;
            }
            SDL_asprintf(&final, "%s%s", dir, id + 6);
            SDL_asprintf(&part, "%s%s.part/", dir, id + 6);
            if (final == NULL || part == NULL) {
                goto done;
            }
            RemoveFolder(part);
            SDL_CreateDirectory(part);
        }
    }
    /* the mod of the same id goes, wherever it was */
    Port_ModsScan();
    if ((i = (u32)IndexOfId(id)) != (u32)-1) {
        RemoveFolder(s_mods[i].dir);
    }
    RemoveFolder(final);
    part[SDL_strlen(part) - 1] = 0;
    if (!SDL_RenamePath(part, final)) {
        SDL_snprintf(err, errSize, "can't put the mod in place: %s", SDL_GetError());
        goto done;
    }
    ok = 1;
    Port_Log("mods: installed %s from %s", id, package);
done:
    if (!ok && part != NULL) {
        part[SDL_strlen(part) - 1] = 0;
        RemoveFolder(part);
    }
    Json_Free(doc);
    SDL_free(text);
    SDL_free(part);
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
        Port_Log("mods: %d in the mods folder, %s", s_numMods, Port_ModsActive() ? "some on" : "all off");
    }
}

static int Same(const JsonNode *a, const char *b) {
    return a != NULL && a->type == JSON_STRING && b != NULL && SDL_strcmp(a->string, b) == 0;
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
    static const char *const kinds[] = { "cars", "items" };
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
    for (k = 0; lock != NULL && k < 2; k++) {
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

                any = 1;
                if (e2 == NULL) {
                    e2 = Json_ObjectMember(sec, x->key);
                    changed = 1;
                }
                changed |= SetStr(e2, "alias", Str(x, "extends"));
                changed |= SetStr(e2, "name", Str(x, "name"));
                changed |= SetStr(e2, "thumbnail", Str(x, "thumbnail"));
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

/* A root object of a car or item pack (the table at 0: a count, then 8-byte
 * entries whose first word points to the root). */
static u8 *Root(const DataPack *pack, u32 i) {
    u8 *base = GETP(pack->base);
    u8 *tab;

    if (base == NULL || i >= *(u32 *)base || (tab = Ptr(*(u32 *)(base + 4))) == NULL) {
        return NULL;
    }
    return Ptr(*(u32 *)(tab + 8 * i));
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

static int SetValue(u8 *p, int type, const JsonNode *v) {
    double d;

    s_old = GetValue(p, type);
    if (v->type == JSON_BOOL) {
        d = v->boolean;
    } else if (v->type == JSON_NUMBER) {
        d = v->isInteger ? (double)v->integer : v->number;
    } else {
        return 0;
    }
    switch (type) {
    case MF_U8: *p = (u8)(s32)d; break;
    case MF_S8: *(s8 *)p = (s8)d; break;
    case MF_U16: *(u16 *)p = (u16)(s32)d; break;
    case MF_S16: *(s16 *)p = (s16)d; break;
    case MF_U32: *(u32 *)p = v->isInteger ? (u32)v->integer : (u32)d; break;
    case MF_S32: *(s32 *)p = (s32)d; break;
    case MF_F32: *(f32 *)p = (f32)d; break;
    default: return 0;
    }
    return 1;
}

static int TypeSize(int type) {
    return type == MF_U8 || type == MF_S8 ? 1 : type == MF_U16 || type == MF_S16 ? 2 : 4;
}

/* A merge patch over an object of struct st: the fields set. */
static int PatchObject(const Mod *m, const char *where, u8 *obj, const ModStruct *st, const JsonNode *patch) {
    const JsonNode *f;
    int n = 0;

    for (f = patch->child; f != NULL; f = f->next) {
        const ModField *fd = FieldNamed(st, f->key, SDL_strlen(f->key));

        if (fd == NULL || fd->type == MF_OTHER) {
            Report("%s: %s: %s.%s isn't a field a patch can set", m->info.name, where, st->name, f->key);
        } else if (fd->type == MF_LINK) {
            u8 *to = Ptr(*(u32 *)(obj + fd->offset));

            if (f->type == JSON_OBJECT && to != NULL) {
                n += PatchObject(m, where, to, &s_structs[fd->target], f);
            } else {
                Report("%s: %s: %s.%s is an object: patch it as one", m->info.name, where, st->name, f->key);
            }
        } else if (fd->count > 0) {
            const JsonNode *e = f->type == JSON_ARRAY ? f->child : NULL;
            int i;

            for (i = 0; e != NULL && i < fd->count; i++, e = e->next) {
                n += e->type != JSON_NULL && SetValue(obj + fd->offset + i * TypeSize(fd->type), fd->type, e);
            }
        } else if (SetValue(obj + fd->offset, fd->type, f)) {
            n++;
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

/* path: Struct/root[/field or index...] in a pack; the object and its struct. */
static u8 *Locate(const DataPack *pack, const char *path, const ModStruct **st) {
    const char *p = SDL_strchr(path, '/'), *seg;
    u8 *obj;

    if (p == NULL || (*st = StructNamed(path, (size_t)(p - path))) == NULL) {
        return NULL;
    }
    obj = Root(pack, (u32)SDL_strtoul(p + 1, NULL, 10));
    for (seg = SDL_strchr(p + 1, '/'); obj != NULL && seg != NULL; seg = SDL_strchr(seg + 1, '/')) {
        const char *end = SDL_strchr(seg + 1, '/');
        size_t len = end != NULL ? (size_t)(end - seg - 1) : SDL_strlen(seg + 1);
        const ModField *fd;

        if (seg[1] >= '0' && seg[1] <= '9') {
            obj += SDL_strtoul(seg + 1, NULL, 10) * (*st)->size;
        } else if ((fd = FieldNamed(*st, seg + 1, len)) != NULL && fd->type == MF_LINK) {
            obj = Ptr(*(u32 *)(obj + fd->offset));
            *st = &s_structs[fd->target];
        } else {
            return NULL;
        }
    }
    return obj;
}

static void ApplyPatch(const Mod *m, const DataPack *pack, const char *id, const char *path, const JsonNode *patch) {
    const ModStruct *st = NULL;
    u8 *obj = Locate(pack, path, &st);

    if (obj == NULL || patch->type != JSON_OBJECT) {
        Report("%s: %s: no such object in this release's pack", m->info.name, id);
        return;
    }
    Port_Log("mods: %s: %d values of %s", m->info.name, PatchObject(m, id, obj, st, patch), id);
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
                ApplyPatch(m, pack, x->key, rest, x);
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
                    ApplyPatch(m, pack, e->key, x->key, x);
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
    if (g_gxTexMods != 0) {
        GX_TexModClear(dataPack);
    }
}
