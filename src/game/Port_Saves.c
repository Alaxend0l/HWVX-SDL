/* The save store (HWVX_SAVES=json): the game's saves as JSONC files, one
 * folder per profile. Port-only file. docs/xdl-format/README.md §11.
 *
 *   <pref>/saves/<profile>/<name>.json        the save (name: File_Open's, "hwvx")
 *   <pref>/saves/<profile>/<name>.<k>.json    backups, k = 1 (newest) .. PORT_SAVES_BACKUPS
 *   <pref>/saves/<profile>/<name>.json.tmp    a write in progress
 *   <pref>/saves/<profile>/<name>.damaged.json  a damaged save, moved aside by the next write
 *   <pref>/saves/<profile>/<name>.imported    the memory card file it was imported from
 *   <pref>/saves/<profile>/deleted/           what the game's Delete removed
 *
 * Every file ends in a line "// sha256: <hex>" over everything before it. A
 * write goes to the .tmp file, which is synced, then the backups move up one
 * (the current file copied to .1), then the .tmp is renamed over the save:
 * at any moment the save is wholly old or wholly new. A read takes the save,
 * or, if it is missing, won't parse or fails its hash, the newest good backup,
 * and says so in the log. Nothing is ever reset for being unreadable: if no
 * file is good the game sees a damaged save (as it would a bad card) and the
 * files stay as they are.
 *
 * A player may edit a save: deleting the sha256 line makes the file count as
 * edited, read as it is, instead of damaged.
 *
 * Profiles: one, "default", for now. A profile per mod set (§11.2) becomes
 * another folder under saves/; Port_SavesProfile is where the choice goes. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"
#include "Port_Content.h"
#include "jsonc.h"
#include "mbedtls/sha256.h"

#define HASH_PREFIX "// sha256: "

typedef struct SaveCache {
    char name[64];          /* "" when empty */
    char stamp[1024];       /* sizes and times of the files the result came from */
    int ok;
    u8 *file;               /* the SaveFile read (when ok) */
    u32 size;
    PortSaveExtras extras;
} SaveCache;

static SaveCache s_cache;
static int s_failAfterTemp;
static char s_migrateTried[2][64];   /* Migrate: once a run for each kind */

int Port_SavesJson(void) {
    static int on = -1;
    if (on < 0) {
        const char *s = SDL_getenv("HWVX_SAVES");
        on = s != NULL && SDL_strcasecmp(s, "json") == 0;
        if (on) {
            Port_Log("saves: JSON store, profile \"%s\" (HWVX_SAVES=json)", Port_SavesProfile());
        }
    }
    return on;
}

const char *Port_SavesProfile(void) {
    return "default";
}

char *Port_SavesDir(void) {
    char *pref = Port_PrefPath(), *dir = NULL;

    SDL_asprintf(&dir, "%ssaves/%s/", pref != NULL ? pref : "./", Port_SavesProfile());
    SDL_free(pref);
    return dir;
}

void Port_SavesTestFailAfterTemp(int on) {
    s_failAfterTemp = on;
}

static void ForgetCache(void) {
    SDL_free(s_cache.file);
    Port_SaveExtrasFree(&s_cache.extras);
    SDL_zero(s_cache);
}

void Port_SavesForget(void) {
    ForgetCache();
    SDL_zero(s_migrateTried);
}

/* ---- files ------------------------------------------------------------- */

/* <dir><name><suffix>; name is the game's file name, kept to [a-z0-9_-]. */
static char *PathOf(const char *name, const char *suffix) {
    char *dir = Port_SavesDir(), *path = NULL, safe[64];
    size_t i;

    for (i = 0; name[i] != 0 && i < sizeof(safe) - 1; i++) {
        char c = (char)SDL_tolower((unsigned char)name[i]);
        safe[i] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';
    }
    safe[i] = 0;
    SDL_asprintf(&path, "%s%s%s", dir != NULL ? dir : "", safe, suffix);
    SDL_free(dir);
    return path;
}

/* Candidate k: 0 the save, 1.. the backups. */
static char *CandidatePath(const char *name, int k) {
    char suffix[32];

    if (k == 0) {
        return PathOf(name, ".json");
    }
    SDL_snprintf(suffix, sizeof(suffix), ".%d.json", k);
    return PathOf(name, suffix);
}

static const char *BaseName(const char *path) {
    const char *s = SDL_strrchr(path, '/');
    return s != NULL ? s + 1 : path;
}

static int Exists(const char *path) {
    return SDL_GetPathInfo(path, NULL);
}

static void Sha256Hex(const void *data, size_t len, char out[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned char h[32];
    int i;

    mbedtls_sha256(data, len, h, 0);
    for (i = 0; i < 32; i++) {
        out[2 * i] = digits[h[i] >> 4];
        out[2 * i + 1] = digits[h[i] & 15];
    }
    out[64] = 0;
}

/* The hash line, if the text ends in one: 1 if it matches (or there's none,
 * an edited file), 0 if it doesn't. */
static int CheckHash(const char *text, size_t len, int *hadHash) {
    size_t end = len, start;
    char want[65];

    *hadHash = 0;
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == ' ')) {
        end--;
    }
    for (start = end; start > 0 && text[start - 1] != '\n'; start--) {
    }
    if (end - start != SDL_strlen(HASH_PREFIX) + 64 || SDL_strncmp(text + start, HASH_PREFIX, SDL_strlen(HASH_PREFIX)) != 0) {
        return 1;
    }
    *hadHash = 1;
    Sha256Hex(text, start, want);
    return SDL_strncasecmp(text + start + SDL_strlen(HASH_PREFIX), want, 64) == 0;
}

/* Reads one candidate. 1 good, 0 damaged (why says how), -1 missing. */
static int LoadCandidate(const char *path, const PortSaveKind *kind, u8 *file, PortSaveExtras *extras,
                         char *why, size_t whySize) {
    size_t len = 0;
    char *text = SDL_LoadFile(path, &len);
    JsonNode *root;
    int hadHash, ok;
    char err[200];

    why[0] = 0;
    if (text == NULL) {
        if (!Exists(path)) {
            SDL_strlcpy(why, "missing", whySize);
            return -1;
        }
        SDL_snprintf(why, whySize, "unreadable (%s)", SDL_GetError());
        return 0;
    }
    if (!CheckHash(text, len, &hadHash)) {
        SDL_strlcpy(why, "fails its sha256 (damaged, or edited without deleting the sha256 line)", whySize);
        SDL_free(text);
        return 0;
    }
    root = Json_Parse(text, len, err, sizeof(err));
    SDL_free(text);
    if (root == NULL) {
        SDL_snprintf(why, whySize, "doesn't parse: %s", err);
        return 0;
    }
    ok = Port_SaveFromJson(root, kind, file, extras, err, sizeof(err));
    Json_Free(root);
    if (!ok) {
        SDL_snprintf(why, whySize, "isn't a save: %s", err);
        return 0;
    }
    if (!hadHash) {
        Port_Log("saves: %s has no sha256 line: read as edited by hand", BaseName(path));
    }
    return 1;
}

static void Stamp(const char *name, char *out, size_t size) {
    int k;

    out[0] = 0;
    for (k = 0; k <= PORT_SAVES_BACKUPS; k++) {
        char *path = CandidatePath(name, k), part[64];
        SDL_PathInfo info;

        if (path != NULL && SDL_GetPathInfo(path, &info)) {
            SDL_snprintf(part, sizeof(part), "%llu:%lld;", (unsigned long long)info.size, (long long)info.modify_time);
        } else {
            SDL_strlcpy(part, "-;", sizeof(part));
        }
        SDL_strlcat(out, part, size);
        SDL_free(path);
    }
}

/* ---- reading ----------------------------------------------------------- */

static void Migrate(const char *name, const PortSaveKind *kind);

/* Fills the cache for `name` (if the files changed since): the newest good
 * candidate. */
static void Refresh(const char *name, const PortSaveKind *kind) {
    char stamp[sizeof(s_cache.stamp)], why[PORT_SAVES_BACKUPS + 1][256];
    u32 size = Port_SaveFileSize(kind);
    int k, r, any = 0;

    Stamp(name, stamp, sizeof(stamp));
    if (SDL_strcmp(s_cache.name, name) == 0 && SDL_strcmp(s_cache.stamp, stamp) == 0) {
        return;
    }
    ForgetCache();
    SDL_strlcpy(s_cache.name, name, sizeof(s_cache.name));
    SDL_strlcpy(s_cache.stamp, stamp, sizeof(s_cache.stamp));
    s_cache.size = size;
    s_cache.file = SDL_calloc(1, size);
    if (s_cache.file == NULL) {
        return;
    }
    for (k = 0; k <= PORT_SAVES_BACKUPS; k++) {
        char *path = CandidatePath(name, k);

        r = path != NULL ? LoadCandidate(path, kind, s_cache.file, &s_cache.extras, why[k], sizeof(why[k])) : -1;
        SDL_free(path);
        any |= r >= 0;
        if (r == 1) {
            s_cache.ok = 1;
            if (k > 0) {
                Port_Log("saves: %s.json %s; loaded the backup %s.%d.json", name, why[0], name, k);
            }
            return;
        }
    }
    SDL_memset(s_cache.file, 0, size);
    if (any) {
        Port_Log("saves: no good %s save in %s (the save %s); the files are left as they are", name,
                 Port_SavesProfile(), why[0]);
        for (k = 1; k <= PORT_SAVES_BACKUPS; k++) {
            if (SDL_strcmp(why[k], "missing") != 0) {
                Port_Log("saves:   backup %d %s", k, why[k]);
            }
        }
    }
}

int Port_SavesExists(const char *name) {
    const PortSaveKind *kind = Port_SaveKindByName(name);
    int k;

    if (kind == NULL) {
        return 0;
    }
    Migrate(name, kind);
    for (k = 0; k <= PORT_SAVES_BACKUPS; k++) {
        char *path = CandidatePath(name, k);
        int e = path != NULL && Exists(path);
        SDL_free(path);
        if (e) {
            return 1;
        }
    }
    return 0;
}

int Port_SavesRead(const char *name, void *saveFile, u32 size) {
    const PortSaveKind *kind = Port_SaveKindByName(name);

    SDL_memset(saveFile, 0, size);
    if (kind == NULL || size != Port_SaveFileSize(kind)) {
        Port_Log("saves: %s: no such save (%u bytes)", name, (unsigned)size);
        return 0;
    }
    Migrate(name, kind);
    Refresh(name, kind);
    if (!s_cache.ok) {
        return 0;
    }
    SDL_memcpy(saveFile, s_cache.file, size);
    return 1;
}

/* ---- writing ----------------------------------------------------------- */

/* The entries the game has no index for, carried from the save as last read
 * to the slots of the one being written: a slot whose name and date are the
 * same is the same save (the game moves saves down a slot as it adds one);
 * failing that, the save of the same name it replaces. */
static void CarryExtras(const PortSaveKind *kind, const SaveFile *now, PortSaveExtras *out) {
    const SaveFile *was = (const SaveFile *)s_cache.file;
    unsigned used = 0;
    int i, j;

    SDL_zerop(out);
    if (!s_cache.ok || was == NULL) {
        return;
    }
    SDL_strlcpy(out->importedFrom, s_cache.extras.importedFrom, sizeof(out->importedFrom));
    for (j = 0; j < SAVE_FILE_SLOTS; j++) {
        for (i = 0; i < SAVE_FILE_SLOTS; i++) {
            if (!(used & (1u << i)) && s_cache.extras.kept[i] != NULL &&
                SDL_memcmp(&was->settings.slots[i], &now->settings.slots[j], sizeof(SaveSlotInfo)) == 0) {
                out->kept[j] = Json_Clone(s_cache.extras.kept[i]);
                used |= 1u << i;
                break;
            }
        }
    }
    for (j = 0; j < SAVE_FILE_SLOTS; j++) {
        if (out->kept[j] != NULL || !now->settings.slots[j].valid) {
            continue;
        }
        for (i = 0; i < SAVE_FILE_SLOTS; i++) {
            if (!(used & (1u << i)) && s_cache.extras.kept[i] != NULL && was->settings.slots[i].valid &&
                SDL_strncmp(was->settings.slots[i].name, now->settings.slots[j].name, sizeof(now->settings.slots[j].name)) == 0) {
                out->kept[j] = Json_Clone(s_cache.extras.kept[i]);
                used |= 1u << i;
                break;
            }
        }
    }
    (void)kind;
}

/* The backups move up one; the save itself is copied to .1 (it stays where
 * it is until the new one is renamed over it). Without its text (it couldn't
 * be loaded just now) the save is renamed to .1 instead: until the new one
 * is in place a read finds .1. */
static int Rotate(const char *name, const char *current, const char *text, size_t len) {
    char *from, *to, *tmp;
    int k, ok;

    to = CandidatePath(name, PORT_SAVES_BACKUPS);
    if (to != NULL) {
        SDL_RemovePath(to);
    }
    SDL_free(to);
    for (k = PORT_SAVES_BACKUPS - 1; k >= 1; k--) {
        from = CandidatePath(name, k);
        to = CandidatePath(name, k + 1);
        if (from != NULL && to != NULL && Exists(from) && !Port_ReplaceFile(from, to)) {
            Port_Log("saves: %s", SDL_GetError());
        }
        SDL_free(from);
        SDL_free(to);
    }
    to = CandidatePath(name, 1);
    tmp = PathOf(name, ".1.json.tmp");
    if (text == NULL) {
        ok = to != NULL && Port_ReplaceFile(current, to);
    } else {
        ok = to != NULL && tmp != NULL && Port_WriteFileSynced(tmp, text, len) && Port_ReplaceFile(tmp, to);
    }
    if (!ok) {
        Port_Log("saves: no backup of %s: %s", BaseName(current), SDL_GetError());
    }
    SDL_free(to);
    SDL_free(tmp);
    return ok;
}

static int WriteSave(const char *name, const PortSaveKind *kind, const void *saveFile, const PortSaveExtras *extras) {
    JsonWriter w;
    char hash[65], *dir = Port_SavesDir(), *current = CandidatePath(name, 0), *tmp = PathOf(name, ".json.tmp");
    int ok = 0;

    Jw_Init(&w);
    if (dir == NULL || current == NULL || tmp == NULL) {
        goto done;
    }
    if (!SDL_CreateDirectory(dir)) {
        Port_Log("saves: can't make %s: %s", dir, SDL_GetError());
        goto done;
    }
    if (!Port_SaveToJson(saveFile, kind, Port_SavesProfile(), extras, &w)) {
        Port_Log("saves: can't write %s: out of memory", BaseName(current));
        goto done;
    }
    Sha256Hex(w.buf, w.len, hash);
    Jw_Raw(&w, HASH_PREFIX);
    Jw_Raw(&w, hash);
    Jw_Raw(&w, "\n");
    if (w.failed) {
        goto done;
    }
    if (!Port_WriteFileSynced(tmp, w.buf, w.len)) {
        Port_Log("saves: %s", SDL_GetError());
        goto done;
    }
    if (s_failAfterTemp) {
        Port_Log("saves: test: the write stops after %s", BaseName(tmp));
        goto done;
    }
    if (Exists(current)) {
        size_t len = 0;
        char *text = SDL_LoadFile(current, &len), why[256] = "";
        u8 *scratch = SDL_malloc(Port_SaveFileSize(kind));
        PortSaveExtras x;
        int r = -1;   /* not checked (out of memory): kept as a backup */

        if (scratch != NULL) {
            r = LoadCandidate(current, kind, scratch, &x, why, sizeof(why));
        }
        if (r == 1) {
            Port_SaveExtrasFree(&x);
        }
        if (r != 0) {
            /* good, or not known to be bad: a backup (renamed if its text
             * couldn't be loaded just now) */
            Rotate(name, current, text, len);
        } else {
            /* not a backup worth keeping in the rotation, but not thrown away */
            char *aside = PathOf(name, ".damaged.json");
            if (aside != NULL && Port_ReplaceFile(current, aside)) {
                Port_Log("saves: %s %s; moved to %s", BaseName(current), why, BaseName(aside));
            }
            SDL_free(aside);
        }
        SDL_free(text);
        SDL_free(scratch);
    }
    if (!Port_ReplaceFile(tmp, current)) {
        Port_Log("saves: %s", SDL_GetError());
        goto done;
    }
    ok = 1;
done:
    Jw_Free(&w);
    SDL_free(dir);
    SDL_free(current);
    SDL_free(tmp);
    return ok;
}

int Port_SavesWrite(const char *name, const void *saveFile, u32 size) {
    const PortSaveKind *kind = Port_SaveKindByName(name);
    PortSaveExtras extras;
    int ok, live;

    if (kind == NULL || size != Port_SaveFileSize(kind)) {
        Port_Log("saves: %s: no such save (%u bytes)", name, (unsigned)size);
        return 0;
    }
    Refresh(name, kind);   /* what the save held, for CarryExtras */
    CarryExtras(kind, saveFile, &extras);
    live = Port_ContentTakeLiveSlot();
    if (live >= 0 && live < SAVE_FILE_SLOTS && kind->dataSize == sizeof(HotWheelsGameData)) {
        /* the game's own save (GameSettings_SaveGame): that slot is the live
         * game, so its entries past the arrays are the registry's */
        Port_ContentStoreKept(&extras.kept[live]);
    }
    ok = WriteSave(name, kind, saveFile, &extras);
    Port_SaveExtrasFree(&extras);
    if (ok) {
        Port_Log("saves: wrote %s/%s.json", Port_SavesProfile(), name);
    }
    return ok;
}

const struct JsonNode *Port_SavesKept(const char *name, int slot) {
    const PortSaveKind *kind = Port_SaveKindByName(name);

    if (kind == NULL || slot < 0 || slot >= SAVE_FILE_SLOTS) {
        return NULL;
    }
    Refresh(name, kind);
    return s_cache.ok ? s_cache.extras.kept[slot] : NULL;
}

int Port_SavesDelete(const char *name) {
    char *dir = Port_SavesDir(), *bin = NULL;
    int k, moved = 0;

    if (dir == NULL || Port_SaveKindByName(name) == NULL) {
        SDL_free(dir);
        return 0;
    }
    SDL_asprintf(&bin, "%sdeleted/", dir);
    if (bin != NULL) {
        SDL_CreateDirectory(bin);
    }
    for (k = 0; k <= PORT_SAVES_BACKUPS; k++) {
        char *path = CandidatePath(name, k), *to = NULL;

        if (path != NULL && bin != NULL) {
            SDL_asprintf(&to, "%s%s", bin, BaseName(path));
            if (to != NULL) {
                SDL_RemovePath(to);   /* an earlier delete's */
                if (Exists(path) && Port_ReplaceFile(path, to)) {
                    moved++;
                }
            }
        }
        SDL_free(path);
        SDL_free(to);
    }
    ForgetCache();
    Port_Log("saves: deleted %s (%d files moved to %s/deleted/)", name, moved, Port_SavesProfile());
    SDL_free(bin);
    SDL_free(dir);
    return moved > 0;
}

/* ---- migration --------------------------------------------------------- */

/* The port's own memory card file, <pref>/memcardA/<name>.sav, read into
 * `file` (checked). 1 read, 0 there is none, -1 it is there but bad (err).
 * The card file: name, comment, banner and icons, then at 0x840 the SaveFile
 * (FileManager_NGC.c CardFile), in the host's order. */
static int ReadCardFile(const char *name, const PortSaveKind *kind, u8 *file, char *source, size_t sourceSize,
                        char *err, size_t errSize) {
    char *pref, *sav = NULL;
    void *bytes;
    size_t len = 0;
    int ok;

    SDL_snprintf(source, sourceSize, "memcardA/%s.sav", name);
    pref = Port_PrefPath();
    SDL_asprintf(&sav, "%s%s", pref != NULL ? pref : "./", source);
    SDL_free(pref);
    bytes = sav != NULL ? SDL_LoadFile(sav, &len) : NULL;
    SDL_free(sav);
    if (bytes == NULL) {
        return 0;
    }
    if (len < 0x840) {
        SDL_strlcpy(err, "too short", errSize);
        ok = -1;
    } else {
        ok = Port_SaveFromBinary((u8 *)bytes + 0x840, len - 0x840, PORT_SAVE_ORDER_HOST, kind, file, err, errSize) ? 1 : -1;
    }
    SDL_free(bytes);
    return ok;
}

/* Whether a JSON save (or a backup) is there. */
static int AnyCandidate(const char *name) {
    int k;

    for (k = 0; k <= PORT_SAVES_BACKUPS; k++) {
        char *path = CandidatePath(name, k);
        int e = path != NULL && Exists(path);
        SDL_free(path);
        if (e) {
            return 1;
        }
    }
    return 0;
}

/* Whether the card file would be imported: no JSON save and no marker. */
static int WouldMigrate(const char *name) {
    char *marker;
    int e;

    if (AnyCandidate(name)) {
        return 0;
    }
    marker = PathOf(name, ".imported");
    e = marker == NULL || Exists(marker);
    SDL_free(marker);
    return !e;
}

/* With no JSON save yet, the port's own memory card file
 * (<pref>/memcardA/<name>.sav) is imported once; the .sav stays. The marker
 * file keeps a deleted save from coming back from the card. Other releases'
 * saves come in through Port_SaveImport.c, merged into this one. */
static void Migrate(const char *name, const PortSaveKind *kind) {
    char *marker, source[128], err[200];
    u8 *file;
    PortSaveExtras extras;
    int r, slot = SDL_strcmp(kind->game, "hwvx") == 0 ? 0 : 1;

    if (SDL_strcmp(s_migrateTried[slot], name) == 0) {
        return;   /* once a run */
    }
    SDL_strlcpy(s_migrateTried[slot], name, sizeof(s_migrateTried[slot]));
    if (!WouldMigrate(name)) {
        return;
    }
    file = SDL_malloc(Port_SaveFileSize(kind));
    if (file == NULL) {
        return;
    }
    err[0] = 0;
    r = ReadCardFile(name, kind, file, source, sizeof(source), err, sizeof(err));
    if (r < 0) {
        Port_Log("saves: %s not imported: %s; it stays as it is", source, err);
    } else if (r > 0) {
        SDL_zero(extras);
        SDL_strlcpy(extras.importedFrom, source, sizeof(extras.importedFrom));
        marker = PathOf(name, ".imported");
        if (marker != NULL && WriteSave(name, kind, file, &extras)) {
            char line[160];
            SDL_snprintf(line, sizeof(line), "%s\n", source);
            Port_WriteFileSynced(marker, line, SDL_strlen(line));
            Port_Log("saves: imported %s into saves/%s/%s.json (the original is kept)", source, Port_SavesProfile(), name);
        }
        SDL_free(marker);
    }
    SDL_free(file);
}

/* ---- for importing (Port_SaveImport.c) --------------------------------- */

int Port_SavesPeek(const char *name, void *saveFile, u32 size, char *what, size_t whatSize) {
    const PortSaveKind *kind = Port_SaveKindByName(name);
    char source[128], err[200];
    int r;

    SDL_memset(saveFile, 0, size);
    if (kind == NULL || size != Port_SaveFileSize(kind)) {
        SDL_strlcpy(what, "no such save", whatSize);
        return -1;
    }
    if (WouldMigrate(name)) {
        /* what Port_SavesRead would import first, read without writing */
        r = ReadCardFile(name, kind, saveFile, source, sizeof(source), err, sizeof(err));
        if (r > 0) {
            SDL_snprintf(what, whatSize, "%s (imported first)", source);
            return 1;
        }
        SDL_memset(saveFile, 0, size);
        SDL_strlcpy(what, "no save yet", whatSize);
        return 0;
    }
    Refresh(name, kind);
    if (!s_cache.ok) {
        SDL_snprintf(what, whatSize, "saves/%s/%s.json is damaged and has no good backup", Port_SavesProfile(), name);
        return -1;
    }
    SDL_memcpy(saveFile, s_cache.file, size);
    SDL_snprintf(what, whatSize, "saves/%s/%s.json", Port_SavesProfile(), name);
    return 1;
}

int Port_SavesWriteImported(const char *name, const void *saveFile, u32 size, const char *importedFrom) {
    const PortSaveKind *kind = Port_SaveKindByName(name);
    PortSaveExtras extras;
    int ok;

    if (kind == NULL || size != Port_SaveFileSize(kind)) {
        return 0;
    }
    Refresh(name, kind);
    CarryExtras(kind, saveFile, &extras);
    if (extras.importedFrom[0] == 0 && importedFrom != NULL) {
        SDL_strlcpy(extras.importedFrom, importedFrom, sizeof(extras.importedFrom));
    }
    ok = WriteSave(name, kind, saveFile, &extras);
    Port_SaveExtrasFree(&extras);
    if (ok) {
        Port_Log("saves: wrote %s/%s.json", Port_SavesProfile(), name);
    }
    return ok;
}
