/*
 * savetest.c - checks the JSON save store (src/game/Port_Saves.c,
 * Port_SaveFormat.c, Port_SaveIds.c) and the content registry it takes its
 * ids from (Port_Content.c). Links only those and src/port/jsonc.c,
 * fileio.c: no game, no window, no game data.
 *
 * Run (from the build folder; every file it makes is under OUT):
 *   ./savetest [OUT] [--gci FILE]
 *     OUT     folder for the scratch pref folders (default: savetest-out)
 *     --gci   a GameCube save (Dolphin .gci export of GHWE78 "hwvx") for
 *             the round trip of a real save; skipped without it
 *
 * What it covers:
 *   - round trip: the .gci's SaveFile (big-endian, at 0x40 + 0x840) into the
 *     game's structs, to JSON, back, compared field by field; random saves of
 *     both game types (padding, NaNs and names with bytes after the NUL too)
 *     compared byte for byte;
 *   - ids the game has no index for survive a write, marked inactive;
 *   - atomicity: a write stopped after its temporary file leaves the old
 *     save; a torn temporary file is ignored;
 *   - corruption: a changed byte, a truncated file, a file that isn't a
 *     save: the newest good backup is loaded and the log says so; with no
 *     good file the read fails and nothing is changed; the next write moves
 *     the damaged file aside;
 *   - a file edited without its sha256 line is read as it is;
 *   - backups rotate, PORT_SAVES_BACKUPS kept;
 *   - migration: memcardA/hwvx.sav is imported once and kept; a delete
 *     doesn't bring it back;
 *   - the content registry: retail counts, a count from the data, entries
 *     registered by id (numbers, ids, aliases, duplicates), the save struct
 *     as its view of the retail entries, new-game values, the online stash;
 *   - an extra car (past the save struct's 33) persists: its unlock and the
 *     choice of it as the vehicle are written by id in the slot holding the
 *     live game, read back, carried as the game moves its slots, kept
 *     (inactive) when the registry doesn't know it and active again when it
 *     does.
 */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"
#include "Port_Content.h"
#include "jsonc.h"
#include "net.h"

/* ---- the port functions the store uses --------------------------------- */

static char s_pref[1024];
static char s_log[16384];

char *Port_PrefPath(void) {
    SDL_CreateDirectory(s_pref);
    return SDL_strdup(s_pref);
}

void Port_Log(const char *fmt, ...) {
    char line[1024];
    va_list ap;

    va_start(ap, fmt);
    SDL_vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    SDL_Log("  | %s", line);
    SDL_strlcat(s_log, line, sizeof(s_log));
    SDL_strlcat(s_log, "\n", sizeof(s_log));
}

/* ---- checks ------------------------------------------------------------ */

static int s_failed, s_checks;
static const char *s_out = "savetest-out";

#define CHECK(cond, ...) do { \
        s_checks++; \
        if (!(cond)) { \
            s_failed++; \
            SDL_Log("FAIL %s:%d: %s", __FILE__, __LINE__, #cond); \
            SDL_Log("     " __VA_ARGS__); \
        } \
    } while (0)

/* A fresh pref folder for one scenario. */
static void Scenario(const char *name) {
    SDL_Log("== %s", name);
    SDL_snprintf(s_pref, sizeof(s_pref), "%s/%s/", s_out, name);
    /* start empty: remove what an earlier run left */
    {
        char **names;
        int n = 0, i;
        static const char *const sub[] = { "saves/default/deleted/", "saves/default/", "memcardA/", "" };
        size_t j;
        for (j = 0; j < SDL_arraysize(sub); j++) {
            char dir[1200];
            SDL_snprintf(dir, sizeof(dir), "%s%s", s_pref, sub[j]);
            names = SDL_GlobDirectory(dir, "*", 0, &n);
            for (i = 0; names != NULL && i < n; i++) {
                char path[1400];
                SDL_snprintf(path, sizeof(path), "%s%s", dir, names[i]);
                SDL_RemovePath(path);   /* files; folders once empty */
            }
            SDL_free(names);
        }
    }
    Port_SavesForget();
    Port_SavesTestFailAfterTemp(0);
    s_log[0] = 0;
}

static char *SavePath(const char *suffix) {
    char *p = NULL;
    SDL_asprintf(&p, "%ssaves/default/hwvx%s", s_pref, suffix);
    return p;
}

static int FileExists(const char *suffix) {
    char *p = SavePath(suffix);
    int e = SDL_GetPathInfo(p, NULL);
    SDL_free(p);
    return e;
}

static char *LoadSaveFile(const char *suffix, size_t *len) {
    char *p = SavePath(suffix), *t = SDL_LoadFile(p, len);
    SDL_free(p);
    return t;
}

static void StoreSaveFile(const char *suffix, const void *data, size_t len) {
    char *p = SavePath(suffix);
    SDL_IOStream *io = SDL_IOFromFile(p, "wb");
    if (io != NULL) {
        SDL_WriteIO(io, data, len);
        SDL_CloseIO(io);
    }
    SDL_free(p);
}

/* ---- saves to test with ------------------------------------------------ */

#define HW_SIZE ((u32)SAVE_FILE_SIZE(sizeof(HotWheelsGameData)))
#define DDR_SIZE ((u32)SAVE_FILE_SIZE(sizeof(DDRescueGameData)))

static u32 s_rand = 12345;
static u32 Rand(void) {
    s_rand = s_rand * 1103515245u + 12345u;
    return s_rand >> 8;
}

static void Finish(void *file, const PortSaveKind *kind) {
    SaveFile *f = file;
    f->magic = SAVE_FILE_MAGIC;
    f->version = (u32)(sizeof(GeneralGameSettings) + kind->dataSize * SAVE_FILE_SLOTS);
    f->checksum = Port_SaveChecksum(file, kind);
}

/* Every byte random, then the header made valid: padding, NaNs, names with
 * bytes after their NUL, out-of-table vehicles all included. */
static void RandomSave(u8 *file, const PortSaveKind *kind) {
    u32 i, size = Port_SaveFileSize(kind);
    SaveFile *f = (SaveFile *)file;

    for (i = 0; i < size; i++) {
        file[i] = (u8)Rand();
    }
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        /* a name stops at its NUL or after 11 chars; bytes after a NUL are kept */
        u32 n = Rand() % 12, k;
        for (k = 0; k < n && k < 11; k++) {
            f->settings.slots[i].name[k] = (char)('A' + Rand() % 26);
        }
        if (n < 11) {
            f->settings.slots[i].name[n] = 0;
        }
    }
    Finish(file, kind);
}

/* A small Hot Wheels save, made to be told apart by `tag`. */
static void SimpleSave(u8 *file, int tag) {
    const PortSaveKind *kind = Port_SaveKindByName("hwvx");
    SaveFile *f = (SaveFile *)file;
    HotWheelsGameData *g = (HotWheelsGameData *)SAVE_FILE_GAME_DATA(file, sizeof(HotWheelsGameData), 0);

    SDL_memset(file, 0, HW_SIZE);
    f->settings.audio.sfxVolume = 100;
    f->settings.audio.musicVolume = (u8)tag;
    f->settings.slots[0].valid = 1;
    SDL_snprintf(f->settings.slots[0].name, sizeof(f->settings.slots[0].name), "SAVE %d", tag);
    f->settings.slots[0].year = 2026;
    f->settings.slots[0].minute = tag;
    g->cars[3] = g->cars[4] = g->cars[5] = 1;
    g->tracks[tag % 0x39] = 1;
    g->vehicle = 3;
    g->item = 0;
    g->miscValues[0] = (u16)tag;
    Finish(file, kind);
}

/* ---- field by field ---------------------------------------------------- */

static int s_diffs;
#define SAME(what, a, b) do { \
        if ((a) != (b)) { \
            s_diffs++; \
            if (s_diffs <= 20) SDL_Log("     differs: %s: %lld vs %lld", what, (long long)(a), (long long)(b)); \
        } \
    } while (0)

static int CompareHotWheels(const u8 *a, const u8 *b) {
    const SaveFile *fa = (const SaveFile *)a, *fb = (const SaveFile *)b;
    char what[64];
    int i, j, fields = 0;

    s_diffs = 0;
    SAME("magic", fa->magic, fb->magic);
    SAME("version", fa->version, fb->version);
    SAME("checksum", fa->checksum, fb->checksum);
    SAME("difficulty", fa->settings.difficulty, fb->settings.difficulty);
    SAME("sfxVolume", fa->settings.audio.sfxVolume, fb->settings.audio.sfxVolume);
    SAME("musicVolume", fa->settings.audio.musicVolume, fb->settings.audio.musicVolume);
    SAME("speakerMode", fa->settings.audio.speakerMode, fb->settings.audio.speakerMode);
    SAME("audio.pad3", fa->settings.audio.pad3, fb->settings.audio.pad3);
    fields += 8;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < (int)sizeof(fa->settings.controllers[i].data); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "controllers[%d].data[%d]", i, j);
            SAME(what, fa->settings.controllers[i].data[j], fb->settings.controllers[i].data[j]);
        }
    }
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        const SaveSlotInfo *x = &fa->settings.slots[i], *y = &fb->settings.slots[i];
        const HotWheelsGameData *g = (const HotWheelsGameData *)SAVE_FILE_GAME_DATA(a, sizeof(HotWheelsGameData), i);
        const HotWheelsGameData *h = (const HotWheelsGameData *)SAVE_FILE_GAME_DATA(b, sizeof(HotWheelsGameData), i);

        SDL_snprintf(what, sizeof(what), "slots[%d].valid", i); SAME(what, x->valid, y->valid);
        SDL_snprintf(what, sizeof(what), "slots[%d].name", i); SAME(what, SDL_memcmp(x->name, y->name, sizeof(x->name)), 0);
        SDL_snprintf(what, sizeof(what), "slots[%d].year", i); SAME(what, x->year, y->year);
        SDL_snprintf(what, sizeof(what), "slots[%d].month", i); SAME(what, x->month, y->month);
        SDL_snprintf(what, sizeof(what), "slots[%d].day", i); SAME(what, x->day, y->day);
        SDL_snprintf(what, sizeof(what), "slots[%d].hour", i); SAME(what, x->hour, y->hour);
        SDL_snprintf(what, sizeof(what), "slots[%d].minute", i); SAME(what, x->minute, y->minute);
        fields += 7;
        for (j = 0; j < (int)sizeof(g->cars); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d cars[%d]", i, j); SAME(what, g->cars[j], h->cars[j]);
        }
        for (j = 0; j < (int)sizeof(g->tracks); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d tracks[%d]", i, j); SAME(what, g->tracks[j], h->tracks[j]);
        }
        for (j = 0; j < (int)sizeof(g->pad5A); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d pad5A[%d]", i, j); SAME(what, g->pad5A[j], h->pad5A[j]);
        }
        for (j = 0; j < (int)sizeof(g->items); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d items[%d]", i, j); SAME(what, g->items[j], h->items[j]);
        }
        for (j = 0; j < (int)sizeof(g->cheats); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d cheats[%d]", i, j); SAME(what, g->cheats[j], h->cheats[j]);
        }
        for (j = 0; j < (int)sizeof(g->extras); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d extras[%d]", i, j); SAME(what, g->extras[j], h->extras[j]);
        }
        SDL_snprintf(what, sizeof(what), "slot %d padAF", i); SAME(what, g->padAF, h->padAF);
        for (j = 0; j < (int)SDL_arraysize(g->miscValues); j++, fields++) {
            SDL_snprintf(what, sizeof(what), "slot %d miscValues[%d]", i, j); SAME(what, g->miscValues[j], h->miscValues[j]);
        }
        SDL_snprintf(what, sizeof(what), "slot %d vehicle", i); SAME(what, g->vehicle, h->vehicle);
        SDL_snprintf(what, sizeof(what), "slot %d item", i); SAME(what, g->item, h->item);
        fields += 3;
    }
    SDL_Log("     %d fields compared, %d differ", fields, s_diffs);
    return s_diffs == 0;
}

/* ---- scenarios --------------------------------------------------------- */

static void TestGameCube(const char *gci) {
    const PortSaveKind *kind = Port_SaveKindByName("hwvx");
    u8 *a = SDL_calloc(1, HW_SIZE), *b = SDL_calloc(1, HW_SIZE);
    size_t len = 0;
    u8 *bytes;
    char err[200];
    const SaveFile *f = (const SaveFile *)a;
    const HotWheelsGameData *g = (const HotWheelsGameData *)SAVE_FILE_GAME_DATA(a, sizeof(HotWheelsGameData), 0);

    Scenario("gamecube");
    if (gci == NULL) {
        SDL_Log("   skipped: no --gci");
        return;
    }
    bytes = SDL_LoadFile(gci, &len);
    CHECK(bytes != NULL, "can't read %s", gci);
    if (bytes == NULL) {
        return;
    }
    /* .gci: a 0x40-byte header, then the card file: the SaveFile at 0x840 */
    CHECK(Port_SaveFromBinary(bytes + 0x40 + 0x840, len - 0x40 - 0x840, PORT_SAVE_ORDER_BIG, kind, a, err, sizeof(err)),
          "%s: %s", gci, err);
    SDL_Log("   %s: slot 0 \"%.11s\" %04d-%02d-%02d, cars 3-5 %d%d%d, vehicle %d",
            gci, f->settings.slots[0].name, (int)f->settings.slots[0].year, (int)f->settings.slots[0].month,
            (int)f->settings.slots[0].day, g->cars[3], g->cars[4], g->cars[5], (int)g->vehicle);
    CHECK(f->settings.slots[0].valid == 1 && f->settings.slots[0].year > 2000, "the slot doesn't look like a save");
    CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write failed");
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", b, HW_SIZE), "read failed");
    CHECK(CompareHotWheels(a, b), "the round trip changed fields");
    CHECK(SDL_memcmp(a, b, HW_SIZE) == 0, "the round trip changed bytes");
    {
        size_t n;
        char *text = LoadSaveFile(".json", &n);
        CHECK(text != NULL && SDL_strstr(text, "\"base://HW_Cars/car_3\": {\"unlocked\": true}") != NULL,
              "the JSON doesn't key cars by id");
        CHECK(text != NULL && SDL_strstr(text, "\"vehicle\": \"base://HW_Cars/car_") != NULL, "vehicle isn't an id");
        SDL_free(text);
    }
    SDL_free(bytes);
    SDL_free(a);
    SDL_free(b);
}

static void TestRandom(void) {
    static const char *const names[] = { "hwvx", "doomsdayrescue" };
    int k, n;

    Scenario("random");
    for (k = 0; k < 2; k++) {
        const PortSaveKind *kind = Port_SaveKindByName(names[k]);
        u32 size = Port_SaveFileSize(kind);
        u8 *a = SDL_malloc(size), *b = SDL_malloc(size);

        for (n = 0; n < 20; n++) {
            RandomSave(a, kind);
            if (k == 0 && n % 2 == 1) {
                /* in-table ids now and then, so both paths are taken */
                HotWheelsGameData *g = (HotWheelsGameData *)SAVE_FILE_GAME_DATA(a, sizeof(HotWheelsGameData), n % 8);
                g->vehicle = n % 0x21;
                g->item = -1;
                Finish(a, kind);
            }
            CHECK(Port_SavesWrite(names[k], a, size), "%s write %d", names[k], n);
            Port_SavesForget();
            CHECK(Port_SavesRead(names[k], b, size), "%s read %d", names[k], n);
            if (SDL_memcmp(a, b, size) != 0) {
                u32 i;
                for (i = sizeof(SaveFile); i < size && a[i] == b[i]; i++) {
                }
                CHECK(0, "%s save %d: differs from byte 0x%X (slot %d +0x%X): %02X vs %02X", names[k], n, (unsigned)i,
                      (int)((i - sizeof(SaveFile)) / kind->dataSize), (unsigned)((i - sizeof(SaveFile)) % kind->dataSize), a[i], b[i]);
                if (k == 0) {
                    CompareHotWheels(a, b);
                }
                break;
            }
        }
        SDL_Log("   %s: 20 random saves through JSON and back", names[k]);
        SDL_free(a);
        SDL_free(b);
    }
}

/* `text` with the first `from` replaced by `to` (a new string; text is freed). */
static char *Replace(char *text, const char *from, const char *to) {
    char *p = text != NULL ? SDL_strstr(text, from) : NULL, *out = NULL;

    CHECK(p != NULL, "no \"%s\" to replace", from);
    if (p == NULL) {
        return text;
    }
    SDL_asprintf(&out, "%.*s%s%s", (int)(p - text), text, to, p + SDL_strlen(from));
    SDL_free(text);
    return out;
}

/* \u escapes: a pair is one code point; a high surrogate without its low
 * one is kept, and the escape after it read on its own. */
static void TestJsonStrings(void) {
    static const struct { const char *json, *utf8; } cases[] = {
        { "\"\\uD83D\\uDE00\"", "\xF0\x9F\x98\x80" },
        { "\"\\uD800\\u0041\"", "\xED\xA0\x80" "A" },
        { "\"\\uD800\\uD83D\\uDE00x\"", "\xED\xA0\x80\xF0\x9F\x98\x80x" },
        { "\"\\uD800\"", "\xED\xA0\x80" },
    };
    size_t i;
    char err[200];

    Scenario("json-strings");
    for (i = 0; i < SDL_arraysize(cases); i++) {
        JsonNode *n = Json_Parse(cases[i].json, SDL_strlen(cases[i].json), err, sizeof(err));
        CHECK(n != NULL && n->type == JSON_STRING && SDL_strcmp(n->string, cases[i].utf8) == 0, "%s read wrong (%s)",
              cases[i].json, n == NULL ? err : "");
        Json_Free(n);
    }
}

static void TestUnknownIds(void) {
    u8 *a = SDL_malloc(HW_SIZE), *b = SDL_malloc(HW_SIZE);
    size_t len;
    char *text, *hash;

    Scenario("unknown-ids");
    SimpleSave(a, 1);
    CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write");
    /* a player (or a removed mod) leaves ids the game hasn't: a car past the
     * array, and a mod's car, chosen as the vehicle */
    text = LoadSaveFile(".json", &len);
    text = Replace(text, "\"cars\": {", "\"cars\": {\n        \"base://HW_Cars/car_40\": {\"unlocked\": true},"
                   "\n        \"uid://a1b2c3d4e5f60718\": {\"unlocked\": true, \"note\": \"mod car\"},");
    text = Replace(text, "\"vehicle\": \"base://HW_Cars/car_3\"", "\"vehicle\": \"uid://a1b2c3d4e5f60718\"");
    /* edited by hand: the sha256 line deleted */
    if (text != NULL && (hash = SDL_strstr(text, "// sha256: ")) != NULL) {
        *hash = 0;
    }
    StoreSaveFile(".json", text, text != NULL ? SDL_strlen(text) : 0);
    SDL_free(text);
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", b, HW_SIZE), "read of the edited save");
    CHECK(SDL_strstr(s_log, "no sha256 line") != NULL, "an edited save isn't said to be one");
    {
        const HotWheelsGameData *g = (const HotWheelsGameData *)SAVE_FILE_GAME_DATA(b, sizeof(HotWheelsGameData), 0);
        CHECK(g->vehicle == -1, "an uninstalled vehicle reads as %d, not -1 (none)", (int)g->vehicle);
        CHECK(g->cars[3] == 1, "retail unlocks lost");
    }
    /* the game writes it back: the unknown entries stay */
    CHECK(Port_SavesWrite("hwvx", b, HW_SIZE), "write back");
    text = LoadSaveFile(".json", &len);
    CHECK(text != NULL && SDL_strstr(text, "\"uid://a1b2c3d4e5f60718\": {\"unlocked\": true, \"note\": \"mod car\", \"inactive\": true}") != NULL,
          "the mod's car is gone");
    CHECK(text != NULL && SDL_strstr(text, "\"base://HW_Cars/car_40\": {\"unlocked\": true, \"inactive\": true}") != NULL,
          "the 41st car is gone");
    CHECK(text != NULL && SDL_strstr(text, "\"vehicle\": \"uid://a1b2c3d4e5f60718\"") != NULL, "the chosen mod car is gone");

    /* an entry nested as deep as the reader goes (root, saves, slot, cars,
     * the entry: 5, then arrays to JSON_MAX_DEPTH) is written back too */
    {
        char deep[JSON_MAX_DEPTH * 2 + 128];
        int d, n = JSON_MAX_DEPTH - 5;

        SDL_strlcpy(deep, "\"cars\": {\n        \"uid://deep\": {\"unlocked\": true, \"note\": ", sizeof(deep));
        for (d = 0; d < n; d++) SDL_strlcat(deep, "[", sizeof(deep));
        for (d = 0; d < n; d++) SDL_strlcat(deep, "]", sizeof(deep));
        SDL_strlcat(deep, "},", sizeof(deep));
        text = Replace(text, "\"cars\": {", deep);
        if (text != NULL && (hash = SDL_strstr(text, "// sha256: ")) != NULL) {
            *hash = 0;
        }
        StoreSaveFile(".json", text, text != NULL ? SDL_strlen(text) : 0);
        SDL_free(text);
        Port_SavesForget();
        CHECK(Port_SavesRead("hwvx", b, HW_SIZE), "read of a save nested %d deep", JSON_MAX_DEPTH);
        CHECK(Port_SavesWrite("hwvx", b, HW_SIZE), "a save nested %d deep isn't written back", JSON_MAX_DEPTH);
        text = LoadSaveFile(".json", &len);
        CHECK(text != NULL && SDL_strstr(text, "\"uid://deep\"") != NULL, "the deep entry is gone");
    }
    SDL_free(text);
    SDL_free(a);
    SDL_free(b);
}

static void TestAtomic(void) {
    u8 *a = SDL_malloc(HW_SIZE), *b = SDL_malloc(HW_SIZE), *r = SDL_malloc(HW_SIZE);
    size_t lenBefore, lenAfter;
    char *before, *after;

    Scenario("atomic");
    SimpleSave(a, 1);
    SimpleSave(b, 2);
    CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write A");
    before = LoadSaveFile(".json", &lenBefore);

    /* the write dies after the temporary file is complete */
    Port_SavesTestFailAfterTemp(1);
    CHECK(!Port_SavesWrite("hwvx", b, HW_SIZE), "the stopped write says it worked");
    Port_SavesTestFailAfterTemp(0);
    CHECK(FileExists(".json.tmp"), "no temporary file left, as a crash would");
    after = LoadSaveFile(".json", &lenAfter);
    CHECK(before != NULL && after != NULL && lenBefore == lenAfter && SDL_memcmp(before, after, lenBefore) == 0,
          "the save changed");
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "the old save doesn't read back");
    CHECK(!FileExists(".1.json"), "a backup was made by a write that didn't happen");
    SDL_free(after);

    /* a temporary file torn halfway (a crash during the write itself) */
    StoreSaveFile(".json.tmp", before, lenBefore / 2);
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "a torn .tmp disturbed the read");

    /* the next write goes through */
    CHECK(Port_SavesWrite("hwvx", b, HW_SIZE), "write B");
    CHECK(!FileExists(".json.tmp"), "the .tmp wasn't renamed over the save");
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, b, HW_SIZE) == 0, "B doesn't read back");
    CHECK(FileExists(".1.json"), "no backup of A");
    SDL_free(before);
    SDL_free(a);
    SDL_free(b);
    SDL_free(r);
}

static void TestCorrupt(void) {
    u8 *a = SDL_malloc(HW_SIZE), *b = SDL_malloc(HW_SIZE), *c = SDL_malloc(HW_SIZE), *r = SDL_malloc(HW_SIZE);
    size_t len, len1;
    char *text, *text1, *t2;
    char *hash;

    Scenario("corrupt");
    SimpleSave(a, 1);
    SimpleSave(b, 2);
    SimpleSave(c, 3);
    CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write A");
    CHECK(Port_SavesWrite("hwvx", b, HW_SIZE), "write B");   /* .1 = A */

    /* one changed byte, the hash line kept */
    text = LoadSaveFile(".json", &len);
    hash = SDL_strstr(text, "\"SAVE 2\"");
    CHECK(hash != NULL, "no name in the save");
    hash[6] = '9';   /* "SAVE 9" */
    StoreSaveFile(".json", text, len);
    Port_SavesForget();
    s_log[0] = 0;
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE), "no fallback");
    CHECK(SDL_memcmp(r, a, HW_SIZE) == 0, "the fallback isn't the backup (A)");
    CHECK(SDL_strstr(s_log, "fails its sha256") != NULL && SDL_strstr(s_log, "loaded the backup hwvx.1.json") != NULL,
          "the fallback wasn't logged");

    /* truncated (a write without the .tmp would leave this) */
    StoreSaveFile(".json", text, len / 3);
    Port_SavesForget();
    s_log[0] = 0;
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "no fallback from a truncated save");
    CHECK(SDL_strstr(s_log, "loaded the backup") != NULL, "not logged");

    /* parses, but isn't a save; no hash line */
    StoreSaveFile(".json", "{\"format_version\": 1, \"game\": \"ddrescue\"}\n", 42);
    Port_SavesForget();
    s_log[0] = 0;
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "no fallback from a wrong save");
    CHECK(SDL_strstr(s_log, "isn't a save") != NULL, "not logged");

    /* a newer format: never read as this one */
    StoreSaveFile(".json", "{\"format_version\": 99, \"game\": \"hwvx\"}\n", 40);
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "a newer format was read");

    /* every file bad: the read fails, nothing is touched */
    text1 = LoadSaveFile(".1.json", &len1);
    t2 = SDL_strdup(text1);
    t2[len1 / 2] ^= 0x20;
    StoreSaveFile(".1.json", t2, len1);
    StoreSaveFile(".json", text, len);
    Port_SavesForget();
    s_log[0] = 0;
    CHECK(!Port_SavesRead("hwvx", r, HW_SIZE), "a read with no good file worked");
    {
        const SaveFile *f = (const SaveFile *)r;
        CHECK(f->magic == 0, "a failed read isn't zeroes (the game's check must fail)");
    }
    CHECK(SDL_strstr(s_log, "left as they are") != NULL, "not logged");
    CHECK(Port_SavesExists("hwvx"), "a damaged save reads as missing (the game would make a new one)");
    {
        size_t n1, n2;
        char *x = LoadSaveFile(".json", &n1), *y = LoadSaveFile(".1.json", &n2);
        CHECK(x != NULL && n1 == len && SDL_memcmp(x, text, len) == 0, "the damaged save was changed by a read");
        CHECK(y != NULL && n2 == len1 && SDL_memcmp(y, t2, len1) == 0, "the damaged backup was changed by a read");
        SDL_free(x);
        SDL_free(y);
    }

    /* the next write: the damaged save is moved aside, not into the backups */
    CHECK(Port_SavesWrite("hwvx", c, HW_SIZE), "write C");
    CHECK(FileExists(".damaged.json"), "the damaged save wasn't kept");
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, c, HW_SIZE) == 0, "C doesn't read back");
    SDL_free(text);
    SDL_free(text1);
    SDL_free(t2);
    SDL_free(a);
    SDL_free(b);
    SDL_free(c);
    SDL_free(r);
}

static void TestRotation(void) {
    u8 *a = SDL_malloc(HW_SIZE), *r = SDL_malloc(HW_SIZE);
    int n, k;

    Scenario("rotation");
    for (n = 1; n <= PORT_SAVES_BACKUPS + 3; n++) {
        SimpleSave(a, n);
        CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write %d", n);
    }
    /* the save is the last; .k is the one k writes before */
    for (k = 1; k <= PORT_SAVES_BACKUPS; k++) {
        char suffix[16];
        size_t len;
        char *text, want[32];

        SDL_snprintf(suffix, sizeof(suffix), ".%d.json", k);
        text = LoadSaveFile(suffix, &len);
        SDL_snprintf(want, sizeof(want), "\"SAVE %d\"", PORT_SAVES_BACKUPS + 3 - k);
        CHECK(text != NULL && SDL_strstr(text, want) != NULL, "backup %d isn't save %d", k, PORT_SAVES_BACKUPS + 3 - k);
        SDL_free(text);
    }
    {
        char suffix[16];
        SDL_snprintf(suffix, sizeof(suffix), ".%d.json", PORT_SAVES_BACKUPS + 1);
        CHECK(!FileExists(suffix), "more than %d backups", PORT_SAVES_BACKUPS);
    }
    /* the same save written again (an autosave with nothing new): the
     * backups stay as they are */
    for (n = 0; n < PORT_SAVES_BACKUPS + 1; n++) {
        CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "rewrite %d", n);
    }
    {
        size_t len;
        char *text = LoadSaveFile(".1.json", &len), want[32];
        SDL_snprintf(want, sizeof(want), "\"SAVE %d\"", PORT_SAVES_BACKUPS + 2);
        CHECK(text != NULL && SDL_strstr(text, want) != NULL, "an unchanged save rotated the backups");
        SDL_free(text);
        CHECK(!FileExists(".json.tmp"), "the unchanged write left its .tmp");
    }
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "the last save doesn't read back");

    /* an import keeps the save before it, whatever is written after */
    SimpleSave(a, 50);
    CHECK(Port_SavesWriteImported("hwvx", a, HW_SIZE, "test"), "imported write");
    for (n = 60; n < 60 + PORT_SAVES_BACKUPS + 2; n++) {
        SimpleSave(a, n);
        CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write %d", n);
    }
    {
        size_t len;
        char *text = LoadSaveFile(".before-import.json", &len), want[32];
        SDL_snprintf(want, sizeof(want), "\"SAVE %d\"", PORT_SAVES_BACKUPS + 3);
        CHECK(text != NULL && SDL_strstr(text, want) != NULL, "the save before the import wasn't kept");
        SDL_free(text);
    }
    SDL_free(a);
    SDL_free(r);
}

static void TestMigration(void) {
    u8 *a = SDL_malloc(HW_SIZE), *r = SDL_malloc(HW_SIZE), card[0x2000];
    char path[1200];
    size_t len;
    char *text, *back;

    Scenario("migration");
    /* the port's memory card file: header, banner and icons, then the SaveFile */
    SimpleSave(a, 7);
    SDL_memset(card, 0, sizeof(card));
    SDL_strlcpy((char *)card, "Hot Wheels Velocity X Save", 0x20);
    SDL_memcpy(card + 0x840, a, HW_SIZE);
    SDL_snprintf(path, sizeof(path), "%smemcardA/", s_pref);
    SDL_CreateDirectory(path);
    SDL_strlcat(path, "hwvx.sav", sizeof(path));
    {
        SDL_IOStream *io = SDL_IOFromFile(path, "wb");
        SDL_WriteIO(io, card, sizeof(card));
        SDL_CloseIO(io);
    }
    CHECK(Port_SavesExists("hwvx"), "the card file wasn't imported");
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE) && SDL_memcmp(r, a, HW_SIZE) == 0, "the import differs");
    text = LoadSaveFile(".json", &len);
    CHECK(text != NULL && SDL_strstr(text, "\"imported_from\": \"memcardA/hwvx.sav\"") != NULL, "no imported_from");
    SDL_free(text);
    back = SDL_LoadFile(path, &len);
    CHECK(back != NULL && len == sizeof(card) && SDL_memcmp(back, card, sizeof(card)) == 0, "the .sav was changed");
    SDL_free(back);
    CHECK(FileExists(".imported"), "no marker");
    /* a later save keeps where it came from */
    SimpleSave(a, 8);
    CHECK(Port_SavesWrite("hwvx", a, HW_SIZE), "write");
    text = LoadSaveFile(".json", &len);
    CHECK(text != NULL && SDL_strstr(text, "\"imported_from\"") != NULL, "imported_from lost");
    SDL_free(text);
    /* the game's Delete: gone, and not imported again */
    CHECK(Port_SavesDelete("hwvx"), "delete");
    Port_SavesForget();
    CHECK(!Port_SavesExists("hwvx"), "the deleted save came back from the card");
    CHECK(!FileExists(".json") && !FileExists(".1.json"), "files left after delete");
    SDL_snprintf(path, sizeof(path), "%ssaves/default/deleted/hwvx.json", s_pref);
    CHECK(SDL_GetPathInfo(path, NULL), "the deleted save isn't in deleted/");

    /* a bad card file isn't imported, and is left alone */
    Scenario("migration-bad");
    card[0x840 + 0x20] ^= 1;   /* checksum fails */
    SDL_snprintf(path, sizeof(path), "%smemcardA/", s_pref);
    SDL_CreateDirectory(path);
    SDL_strlcat(path, "hwvx.sav", sizeof(path));
    {
        SDL_IOStream *io = SDL_IOFromFile(path, "wb");
        SDL_WriteIO(io, card, sizeof(card));
        SDL_CloseIO(io);
    }
    CHECK(!Port_SavesExists("hwvx"), "a bad card file was imported");
    CHECK(SDL_strstr(s_log, "not imported: checksum") != NULL, "not logged");
    SDL_free(a);
    SDL_free(r);
}


/* ---- the content registry ---------------------------------------------- */

static void TestRegistry(void) {
    HotWheelsGameData g;
    char id[128];
    u32 index;
    s32 a, b;

    Scenario("registry");
    Port_ContentReset();
    CHECK(Port_ContentCount(PORT_CONTENT_CAR) == 33 && Port_ContentCount(PORT_CONTENT_TRACK) == 57 &&
          Port_ContentCount(PORT_CONTENT_ITEM) == 12, "retail counts %u %u %u", (unsigned)Port_ContentCount(0),
          (unsigned)Port_ContentCount(1), (unsigned)Port_ContentCount(2));
    CHECK(Port_ContentRetailCount(PORT_CONTENT_CAR) == sizeof(g.cars), "retail = the array");
    CHECK(Port_ContentIdOf(PORT_CONTENT_CAR, 32, id, sizeof(id)) && SDL_strcmp(id, "base://HW_Cars/car_32") == 0, "car 32: %s", id);
    CHECK(!Port_ContentIdOf(PORT_CONTENT_CAR, 33, id, sizeof(id)), "no car 33 yet");
    CHECK(!Port_ContentIndexOf(PORT_CONTENT_CAR, "base://HW_Cars/car_33", &index), "base car 33 before the data has it");

    /* the view: the struct's arrays */
    SDL_zero(g);
    g.cars[3] = 1;
    g.tracks[5] = 1;
    Port_ContentBindGameData(g.cars, g.tracks, g.items);
    Port_ContentFromGameData();
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 3) == 1 && Port_ContentUnlocked(PORT_CONTENT_TRACK, 5) == 1, "taken from the struct");
    Port_ContentSetUnlocked(PORT_CONTENT_ITEM, 11, 1);
    CHECK(g.items[11] == 1, "a retail entry set through the registry reaches the struct");
    Port_ContentSetUnlocked(PORT_CONTENT_ITEM, 12, 1);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_ITEM, 12) == 0, "past the count: nothing set");

    /* the data has 36 cars: 33-35 are base content past the array */
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, 36);
    CHECK(Port_ContentCount(PORT_CONTENT_CAR) == 36, "count from the data %u", (unsigned)Port_ContentCount(0));
    CHECK(Port_ContentIndexOf(PORT_CONTENT_CAR, "base://HW_Cars/car_35", &index) && index == 35, "base car 35");
    Port_ContentSetBaseCount(PORT_CONTENT_TRACK, 10);
    CHECK(Port_ContentCount(PORT_CONTENT_TRACK) == 57, "never fewer than the array");

    /* registered: numbered after the base content */
    a = Port_ContentRegister(PORT_CONTENT_CAR, "uid://0011223344556677", 4, 0);
    b = Port_ContentRegister(PORT_CONTENT_CAR, "uid://8899aabbccddeeff", -1, 1);
    CHECK(a == 36 && b == 37 && Port_ContentCount(PORT_CONTENT_CAR) == 38, "registered %d %d", (int)a, (int)b);
    CHECK(Port_ContentRegister(PORT_CONTENT_CAR, "uid://0011223344556677", 4, 0) == -1, "an id twice");
    CHECK(Port_ContentRegister(PORT_CONTENT_CAR, "base://HW_Cars/car_7", -1, 0) == -1, "a base id registered");
    CHECK(Port_ContentRegister(PORT_CONTENT_CAR, "uid://x", 99, 0) == -1, "an alias of no base entry");
    CHECK(Port_ContentDataOf(PORT_CONTENT_CAR, 36) == 4 && Port_ContentDataOf(PORT_CONTENT_CAR, 37) == -1 &&
          Port_ContentDataOf(PORT_CONTENT_CAR, 35) == 35, "what data the entries use");
    CHECK(Port_ContentIdOf(PORT_CONTENT_CAR, 37, id, sizeof(id)) && SDL_strcmp(id, "uid://8899aabbccddeeff") == 0, "id of 37: %s", id);
    CHECK(Port_ContentIndexOf(PORT_CONTENT_CAR, "uid://0011223344556677", &index) && index == 36, "index of a uid");
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, 40);
    CHECK(Port_ContentCount(PORT_CONTENT_CAR) == 38, "the base count is not changed under registered entries");

    /* new-game values; past the array nothing reaches the struct */
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 36) == 0 && Port_ContentUnlocked(PORT_CONTENT_CAR, 37) == 1, "new-game values");
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 36, 1);
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 33, 1);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 36) == 1 && g.pad5A[0] == 0 && g.tracks[0] == 0, "an extra car stays out of the struct");

    /* online: put aside and back */
    Port_ContentStash(0);
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 36, 0);
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 34, 1);
    Port_ContentStash(1);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 36) == 1 && Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 0, "online stash");

    /* a new game: the struct's, and each its new-game value past it */
    SDL_zero(g);
    g.cars[0] = 1;
    Port_ContentFromGameData();
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 0) == 1 && Port_ContentUnlocked(PORT_CONTENT_CAR, 3) == 0 &&
          Port_ContentUnlocked(PORT_CONTENT_CAR, 36) == 0 && Port_ContentUnlocked(PORT_CONTENT_CAR, 37) == 1 &&
          Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 0, "a new game");
    Port_ContentReset();
    CHECK(Port_ContentCount(PORT_CONTENT_CAR) == 33 && !Port_ContentIndexOf(PORT_CONTENT_CAR, "uid://0011223344556677", &index), "reset");

    /* online: choices go by id past the base content (Port_ContentWriteChoice) */
    {
        u8 wire[256];
        NetWriter w;
        NetReader r;
        s32 c0, c1, c2, c3, c4;

        /* this machine: 36 and 37 registered, as above */
        Port_ContentSetBaseCount(PORT_CONTENT_CAR, 36);
        Port_ContentRegister(PORT_CONTENT_CAR, "uid://0011223344556677", 4, 0);
        Port_ContentRegister(PORT_CONTENT_CAR, "uid://8899aabbccddeeff", -1, 1);
        NetWriter_Init(&w, wire, sizeof(wire));
        Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, 5);    /* base */
        Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, 36);   /* uid://0011..., data of car 4 */
        Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, 37);   /* uid://8899..., data of its own */
        Port_ContentWriteChoice(&w, PORT_CONTENT_ITEM, -1);  /* no item */
        Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, 99);   /* nothing here */
        CHECK(w.ok, "choices written");
        /* this machine reads its own back */
        NetReader_Init(&r, wire, w.size);
        c0 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        c1 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        c2 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        c3 = Port_ContentReadChoice(&r, PORT_CONTENT_ITEM);
        c4 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        CHECK(r.ok && c0 == 5 && c1 == 36 && c2 == 37 && c3 == -1 && c4 == 0, "own choices %d %d %d %d %d", (int)c0,
              (int)c1, (int)c2, (int)c3, (int)c4);
        /* another machine: the same cars registered the other way round, one missing */
        Port_ContentReset();
        Port_ContentSetBaseCount(PORT_CONTENT_CAR, 36);
        Port_ContentRegister(PORT_CONTENT_CAR, "uid://8899aabbccddeeff", -1, 1);   /* 36 here */
        NetReader_Init(&r, wire, w.size);
        c0 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        c1 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        c2 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
        CHECK(r.ok && c0 == 5 && c1 == 4 && c2 == 36, "another machine: base 5, missing -> its alias 4, by id -> 36: %d %d %d",
              (int)c0, (int)c1, (int)c2);
        Port_ContentReset();
    }
}

/* The game's flow around a save, as GameSettings_SaveGame / LoadGame do it:
 * the live game in slot 0, older saves moved down. */
static void GameSave(u8 *file, HotWheelsGameData *live, const char *name, int minute) {
    SaveFile *f = (SaveFile *)file;
    int slot;

    for (slot = SAVE_FILE_SLOTS - 1; slot > 0; slot--) {
        SDL_memcpy(SAVE_FILE_GAME_DATA(file, sizeof(HotWheelsGameData), slot),
                   SAVE_FILE_GAME_DATA(file, sizeof(HotWheelsGameData), slot - 1), sizeof(HotWheelsGameData));
        f->settings.slots[slot] = f->settings.slots[slot - 1];
    }
    SDL_zero(f->settings.slots[0]);
    f->settings.slots[0].valid = 1;
    SDL_strlcpy(f->settings.slots[0].name, name, sizeof(f->settings.slots[0].name));
    f->settings.slots[0].year = 2026;
    f->settings.slots[0].minute = minute;
    SDL_memcpy(SAVE_FILE_GAME_DATA(file, sizeof(HotWheelsGameData), 0), live, sizeof(HotWheelsGameData));
    Finish(file, Port_SaveKindByName("hwvx"));
    Port_ContentNoteLiveSlot(0);
}

static void GameLoad(u8 *file, HotWheelsGameData *live, int slot) {
    CHECK(Port_SavesRead("hwvx", file, HW_SIZE), "read");
    SDL_memcpy(live, SAVE_FILE_GAME_DATA(file, sizeof(HotWheelsGameData), slot), sizeof(HotWheelsGameData));
    Port_ContentFromGameData();
    Port_ContentLoadKept(Port_SavesKept("hwvx", slot));
}

static void TestExtraCar(void) {
    u8 *file = SDL_calloc(1, HW_SIZE);
    HotWheelsGameData live;
    size_t len;
    char *text;

    Scenario("extra-car");
    Port_ContentReset();
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, 33);
    Port_ContentRegister(PORT_CONTENT_CAR, "uid://hwvx-test-extra-car-0", 3, 0);
    Port_ContentRegister(PORT_CONTENT_CAR, "uid://hwvx-test-extra-car-1", 4, 0);
    SDL_zero(live);
    live.vehicle = -1;
    live.item = -1;
    Port_ContentBindGameData(live.cars, live.tracks, live.items);
    Port_ContentFromGameData();

    /* unlocked and chosen, then saved */
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 3, 1);
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 34, 1);
    live.vehicle = 34;
    GameSave(file, &live, "FIRST", 1);
    CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write");
    CHECK(Port_ContentTakeLiveSlot() == -1, "the write took the live slot");
    text = LoadSaveFile(".json", &len);
    CHECK(text != NULL && SDL_strstr(text, "\"uid://hwvx-test-extra-car-1\": {\"unlocked\": true}") != NULL, "car 34's unlock written");
    CHECK(text != NULL && SDL_strstr(text, "\"uid://hwvx-test-extra-car-0\": {\"unlocked\": false}") != NULL, "car 33's written");
    CHECK(text != NULL && SDL_strstr(text, "\"vehicle\": \"uid://hwvx-test-extra-car-1\"") != NULL, "car 34 as the vehicle");
    CHECK(text != NULL && SDL_strstr(text, "inactive") == NULL, "nothing inactive");
    SDL_free(text);

    /* a new run: the save is read back */
    Port_SavesForget();
    SDL_zero(live);
    Port_ContentFromGameData();
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 0, "a new game");
    GameLoad(file, &live, 0);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 1 && Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 0 &&
          Port_ContentUnlocked(PORT_CONTENT_CAR, 3) == 1, "the unlocks read back");
    CHECK(live.vehicle == 34, "the vehicle reads back as %d", (int)live.vehicle);

    /* another save under a new name: the first moves to slot 1 with its cars */
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 34, 0);
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 33, 1);
    live.vehicle = 33;
    GameSave(file, &live, "SECOND", 2);
    CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write");
    Port_SavesForget();
    GameLoad(file, &live, 1);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 1 && Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 0 && live.vehicle == 34,
          "slot 1 (the first save) kept its cars");
    GameLoad(file, &live, 0);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 1 && Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 0 && live.vehicle == 33,
          "slot 0 has the second's");
    CHECK(Port_ContentKeptUnlocked(PORT_CONTENT_CAR, Port_SavesKept("hwvx", 1)) == 1, "the level details count slot 1's");

    /* a save not made by the game (an import): no live slot, kept as it was */
    CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write without the game");
    Port_SavesForget();
    GameLoad(file, &live, 0);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 1, "kept through a write the game didn't make");

    /* without the extra cars (a build or mod set without them): kept, inactive */
    Port_ContentReset();
    SDL_zero(live);
    Port_ContentBindGameData(live.cars, live.tracks, live.items);
    Port_SavesForget();
    GameLoad(file, &live, 1);
    CHECK(live.vehicle == -1, "an unknown vehicle reads as none");
    GameSave(file, &live, "THIRD", 3);
    CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write");
    text = LoadSaveFile(".json", &len);
    CHECK(text != NULL && SDL_strstr(text, "\"uid://hwvx-test-extra-car-1\": {\"unlocked\": true, \"inactive\": true}") != NULL,
          "car 34 kept, inactive");
    SDL_free(text);

    /* and back */
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, 33);
    Port_ContentRegister(PORT_CONTENT_CAR, "uid://hwvx-test-extra-car-0", 3, 0);
    Port_ContentRegister(PORT_CONTENT_CAR, "uid://hwvx-test-extra-car-1", 4, 0);
    Port_SavesForget();
    GameLoad(file, &live, 2);   /* FIRST, two saves down */
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 1 && live.vehicle == 34, "the first save's car 34 is back");
    Port_ContentReset();
    SDL_free(file);
}

/* A project's ids.lock (Port_ContentLoadLock): numbers assigned once and
 * written back, kept across runs and across entries removed and added
 * again; saves by id stay right throughout. Each "run" resets the registry
 * and reads the lock again, as the game does at start. */
static void WriteText(const char *path, const char *text) {
    CHECK(SDL_SaveFile(path, text, SDL_strlen(text)), "write %s", path);
}

static void LockRun(const char *lock) {
    Port_ContentReset();
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, 33);
    Port_ContentLoadLock(lock);
}

static s32 NumberOf(const char *id) {
    u32 index;

    return Port_ContentIndexOf(PORT_CONTENT_CAR, id, &index) ? (s32)index : -1;
}

static void TestIdsLock(void) {
    char lock[512];
    u8 *file = SDL_calloc(1, HW_SIZE);
    HotWheelsGameData live;
    char *text;
    size_t len;

    Scenario("ids-lock");
    SDL_snprintf(lock, sizeof(lock), "%s/ids.lock", s_out);
    /* no file: nothing */
    SDL_RemovePath(lock);
    LockRun(lock);
    CHECK(Port_ContentCount(PORT_CONTENT_CAR) == 33, "no lock, no entries");

    /* added: two cars without numbers get 33 and 34, written back */
    WriteText(lock, "// a project's lock\n{\n  \"cars\": {\n"
                    "    \"uid://aaaa\": {\"alias\": \"base://HW_Cars/car_3\", \"name\": \"Alpha\"},\n"
                    "    \"uid://bbbb\": {\"alias\": \"base://HW_Cars/car_4\", \"unlocked\": true},\n  },\n"
                    "  \"items\": {\"uid://cccc\": {\"alias\": \"base://mcp/item/2\"}}\n}\n");
    LockRun(lock);
    CHECK(NumberOf("uid://aaaa") == 33 && NumberOf("uid://bbbb") == 34, "assigned 33, 34: %d %d",
          (int)NumberOf("uid://aaaa"), (int)NumberOf("uid://bbbb"));
    CHECK(Port_ContentDataOf(PORT_CONTENT_CAR, 33) == 3 && Port_ContentDataOf(PORT_CONTENT_CAR, 34) == 4, "aliases");
    {
        u32 index;

        CHECK(Port_ContentIndexOf(PORT_CONTENT_ITEM, "uid://cccc", &index) && index == 12 &&
              Port_ContentDataOf(PORT_CONTENT_ITEM, 12) == 2, "an item: 12, item 2's data");
    }
    {
        const char *name, *suffix, *thumb;

        CHECK(Port_ContentLook(PORT_CONTENT_CAR, 33, &name, &suffix, &thumb) == 1 && name != NULL &&
              SDL_strcmp(name, "Alpha") == 0, "its name");
    }
    text = SDL_LoadFile(lock, &len);
    CHECK(text != NULL && SDL_strstr(text, "\"uid://aaaa\": {\n      \"number\": 33") != NULL &&
          SDL_strstr(text, "\"number\": 34") != NULL && SDL_strstr(text, "\"cars\": 35") != NULL,
          "written back with the numbers and next");
    CHECK(text != NULL && SDL_strstr(text, "\"name\": \"Alpha\"") != NULL, "the rest kept");
    SDL_free(text);

    /* a save: car 34 unlocked and chosen */
    SDL_zero(live);
    live.vehicle = -1;
    live.item = -1;
    Port_ContentBindGameData(live.cars, live.tracks, live.items);
    Port_ContentFromGameData();
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 1 && Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 0,
          "new-game values from the lock");
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 33, 1);
    live.vehicle = 34;
    Port_SavesForget();
    GameSave(file, &live, "LOCK", 1);
    CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write");

    /* the same lock again: the same numbers, the file unchanged */
    text = SDL_LoadFile(lock, &len);
    LockRun(lock);
    {
        char *again = SDL_LoadFile(lock, &len);

        CHECK(NumberOf("uid://aaaa") == 33 && NumberOf("uid://bbbb") == 34, "the same numbers");
        CHECK(text != NULL && again != NULL && SDL_strcmp(text, again) == 0, "nothing written");
        SDL_free(again);
    }
    SDL_free(text);

    /* removed: aaaa away (inactive) - bbbb keeps 34, 33 is reserved */
    WriteText(lock, "{\"format_version\": 1, \"next\": {\"cars\": 35, \"tracks\": 57, \"items\": 13},\n"
                    " \"cars\": {\"uid://aaaa\": {\"number\": 33, \"alias\": \"base://HW_Cars/car_3\", \"active\": false},\n"
                    "          \"uid://bbbb\": {\"number\": 34, \"alias\": \"base://HW_Cars/car_4\"}}}\n");
    LockRun(lock);
    Port_ContentBindGameData(live.cars, live.tracks, live.items);
    CHECK(NumberOf("uid://bbbb") == 34 && NumberOf("uid://aaaa") == -1 && Port_ContentCount(PORT_CONTENT_CAR) == 35,
          "bbbb keeps 34; aaaa is away");
    Port_ContentSetUnlocked(PORT_CONTENT_CAR, 33, 1);
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 0, "a reserved number is never unlocked");
    Port_SavesForget();
    GameLoad(file, &live, 0);
    CHECK(live.vehicle == 34 && Port_ContentUnlocked(PORT_CONTENT_CAR, 34) == 1, "the save's vehicle is still bbbb (34), unlocked");
    GameSave(file, &live, "AWAY", 2);
    CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write");
    text = LoadSaveFile(".json", &len);
    CHECK(text != NULL && SDL_strstr(text, "\"uid://aaaa\": {\"unlocked\": true, \"inactive\": true}") != NULL,
          "aaaa's unlock kept in the save, inactive");
    SDL_free(text);

    /* removed for good: the line deleted - bbbb still 34, and a new car
     * gets 35 (next), never 33 again */
    WriteText(lock, "{\"next\": {\"cars\": 35},\n \"cars\": {\"uid://bbbb\": {\"number\": 34, \"alias\": \"base://HW_Cars/car_4\"},\n"
                    "  \"uid://dddd\": {\"alias\": \"base://HW_Cars/car_5\"}}}\n");
    LockRun(lock);
    CHECK(NumberOf("uid://bbbb") == 34 && NumberOf("uid://dddd") == 35, "bbbb 34, a new car 35: %d",
          (int)NumberOf("uid://dddd"));

    /* re-added: aaaa back with its number - its unlock comes back from the save */
    WriteText(lock, "{\"next\": {\"cars\": 36},\n \"cars\": {\"uid://aaaa\": {\"number\": 33, \"alias\": \"base://HW_Cars/car_3\"},\n"
                    "  \"uid://bbbb\": {\"number\": 34, \"alias\": \"base://HW_Cars/car_4\"},\n"
                    "  \"uid://dddd\": {\"number\": 35, \"alias\": \"base://HW_Cars/car_5\"}}}\n");
    LockRun(lock);
    Port_ContentBindGameData(live.cars, live.tracks, live.items);
    CHECK(NumberOf("uid://aaaa") == 33 && NumberOf("uid://bbbb") == 34 && NumberOf("uid://dddd") == 35, "33, 34, 35 again");
    Port_SavesForget();
    GameLoad(file, &live, 1);   /* LOCK, one save down */
    CHECK(Port_ContentUnlocked(PORT_CONTENT_CAR, 33) == 1 && live.vehicle == 34, "LOCK's aaaa unlocked again, bbbb chosen");

    /* conflicts: a number base content has, and one taken twice, get new ones */
    WriteText(lock, "{\"cars\": {\"uid://eeee\": {\"number\": 5, \"alias\": \"base://HW_Cars/car_1\"},\n"
                    "  \"uid://ffff\": {\"number\": 40, \"alias\": \"base://HW_Cars/car_1\"},\n"
                    "  \"uid://gggg\": {\"number\": 40, \"alias\": \"base://HW_Cars/car_1\"},\n"
                    "  \"uid://hhhh\": {\"number\": 41}}}\n");
    LockRun(lock);
    CHECK(NumberOf("uid://ffff") == 40 && NumberOf("uid://eeee") == 42 && NumberOf("uid://gggg") == 43,
          "40 kept, the others after everything: %d %d", (int)NumberOf("uid://eeee"), (int)NumberOf("uid://gggg"));
    CHECK(NumberOf("uid://hhhh") == -1 && Port_ContentCount(PORT_CONTENT_CAR) == 44 &&
          Port_ContentUnlocked(PORT_CONTENT_CAR, 41) == 0, "no alias (data of its own): its number reserved");
    CHECK(Port_ContentDataOf(PORT_CONTENT_CAR, 35) == 0 && Port_ContentUnlocked(PORT_CONTENT_CAR, 35) == 0,
          "the numbers between are reserved");

    /* a broken file is left alone */
    WriteText(lock, "{\"cars\": {");
    CHECK(Port_ContentLoadLock(lock) == -1, "unreadable: -1");
    text = SDL_LoadFile(lock, &len);
    CHECK(text != NULL && SDL_strcmp(text, "{\"cars\": {") == 0, "and not written");
    SDL_free(text);
    Port_ContentReset();
    SDL_free(file);
}

/* Bad input the registry must survive: huge numbers, long ids, reserved
 * entries on the wire. */
static void TestContentLimits(void) {
    char lock[512], id[300], back[300];
    u8 wire[1024];
    NetWriter w;
    NetReader r;
    u32 index;
    s32 c0, c1, c2, c3, c4;
    PortContentEntry e;
    char *text;
    size_t len;

    Scenario("content-limits");
    SDL_snprintf(lock, sizeof(lock), "%s/ids.lock", s_out);

    /* a huge number or next: logged, not filled up to (no hang, no overflow) */
    WriteText(lock, "{\n  \"next\": {\"cars\": 2147483647, \"items\": 99999999999},\n  \"cars\": {\n"
                    "    \"uid://huge\": {\"number\": 2000000000, \"alias\": \"base://HW_Cars/car_1\"},\n"
                    "    \"uid://ok\": {\"number\": 40, \"alias\": \"base://HW_Cars/car_2\"}\n  }\n}\n");
    LockRun(lock);
    CHECK(NumberOf("uid://ok") == 40 && NumberOf("uid://huge") == 41 && Port_ContentCount(PORT_CONTENT_CAR) == 42,
          "a huge number gets the next one: ok %d huge %d count %u", (int)NumberOf("uid://ok"),
          (int)NumberOf("uid://huge"), (unsigned)Port_ContentCount(PORT_CONTENT_CAR));
    CHECK(SDL_strstr(s_log, "number 2000000000 is past") != NULL && SDL_strstr(s_log, "next cars 2147483647") != NULL &&
          SDL_strstr(s_log, "next items 99999999999") != NULL, "not logged");
    CHECK(Port_ContentReserved(PORT_CONTENT_CAR, 33) && Port_ContentReserved(PORT_CONTENT_CAR, 39) &&
          !Port_ContentReserved(PORT_CONTENT_CAR, 40) && !Port_ContentReserved(PORT_CONTENT_CAR, 3) &&
          !Port_ContentReserved(PORT_CONTENT_CAR, 99), "reserved 33-39");
    text = SDL_LoadFile(lock, &len);
    CHECK(text != NULL && SDL_strstr(text, "\"cars\": 42") != NULL, "next written back sane");
    SDL_free(text);
    /* numbers at the limit: the last one is taken, then none are left */
    SDL_snprintf(id, sizeof(id), "{\"next\": {\"cars\": %d}, \"cars\": {\"uid://last\": {\"alias\": \"base://HW_Cars/car_1\"},"
                 " \"uid://none\": {\"alias\": \"base://HW_Cars/car_1\"}}}", 33 + PORT_CONTENT_MAX_REGISTERED - 1);
    WriteText(lock, id);
    LockRun(lock);
    CHECK(NumberOf("uid://last") == 33 + PORT_CONTENT_MAX_REGISTERED - 1 && NumberOf("uid://none") == -1 &&
          SDL_strstr(s_log, "no number left") != NULL, "the limit: last %d none %d", (int)NumberOf("uid://last"),
          (int)NumberOf("uid://none"));
    Port_ContentReset();
    Port_ContentSetBaseCount(PORT_CONTENT_CAR, 33);
    SDL_zero(e);
    e.id = "uid://far";
    e.alias = 1;
    e.number = 33 + PORT_CONTENT_MAX_REGISTERED;
    CHECK(Port_ContentRegisterEntry(PORT_CONTENT_CAR, &e) == -1 && Port_ContentCount(PORT_CONTENT_CAR) == 33,
          "a number past the limit registered");

    /* ids: 255 characters at most, never cut */
    SDL_memset(id, 'a', sizeof(id));
    SDL_memcpy(id, "uid://", 6);
    id[256] = 0;
    CHECK(Port_ContentRegister(PORT_CONTENT_CAR, id, 1, 0) == -1 && SDL_strstr(s_log, "an id of 256 characters") != NULL,
          "a 256-character id registered");
    id[255] = 0;
    c0 = Port_ContentRegister(PORT_CONTENT_CAR, id, 1, 1);
    CHECK(c0 == 33 && Port_ContentIdOf(PORT_CONTENT_CAR, 33, back, sizeof(back)) && SDL_strcmp(back, id) == 0,
          "a 255-character id");
    {
        JsonNode *kept = NULL;

        Port_ContentStoreKept(&kept);
        CHECK(kept != NULL && Json_Get(Json_Get(kept, "cars"), id) != NULL, "a 255-character id kept whole");
        Json_Free(kept);
    }
    {
        HotWheelsGameData g;
        u8 *file = SDL_calloc(1, HW_SIZE);
        char *saved;
        size_t n;

        SDL_zero(g);
        g.vehicle = 33;
        g.item = -1;
        Port_ContentBindGameData(g.cars, g.tracks, g.items);
        Port_ContentFromGameData();
        Port_SavesForget();
        GameSave(file, &g, "LONG", 1);
        CHECK(Port_SavesWrite("hwvx", file, HW_SIZE), "write");
        SDL_snprintf(back, sizeof(back), "\"vehicle\": \"%s\"", id);
        saved = LoadSaveFile(".json", &n);
        CHECK(saved != NULL && SDL_strstr(saved, back) != NULL, "the chosen 255-character id saved whole");
        SDL_free(saved);
        SDL_free(file);
        Port_ContentBindGameData(NULL, NULL, NULL);
    }

    /* the wire: a long id whole, a reserved entry as car 0, per-machine
     * numbers without an id not taken */
    e.id = "uid://after-gap";
    e.alias = 5;
    e.number = 36;   /* 34, 35 reserved */
    CHECK(Port_ContentRegisterEntry(PORT_CONTENT_CAR, &e) == 36 && Port_ContentReserved(PORT_CONTENT_CAR, 34), "a gap");
    NetWriter_Init(&w, wire, sizeof(wire));
    Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, 33);
    Port_ContentWriteChoice(&w, PORT_CONTENT_CAR, 34);
    NetWriter_U32(&w, 36);   /* a number past the base, no id (an older or bad peer) */
    NetWriter_U8(&w, 0);
    NetWriter_U32(&w, 13);
    NetWriter_U8(&w, 0);
    NetWriter_U32(&w, 7);
    NetWriter_U8(&w, 0);
    CHECK(w.ok, "written");
    NetReader_Init(&r, wire, w.size);
    c0 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
    c1 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
    c2 = Port_ContentReadChoice(&r, PORT_CONTENT_CAR);
    c3 = Port_ContentReadChoice(&r, PORT_CONTENT_ITEM);
    c4 = Port_ContentReadChoice(&r, PORT_CONTENT_ITEM);
    CHECK(r.ok && c0 == 33 && c1 == 0 && c2 == 0 && c3 == -1 && c4 == 7,
          "long id %d, reserved %d, bare 36 %d, bare item 13 %d, item 7 %d", (int)c0, (int)c1, (int)c2, (int)c3, (int)c4);
    CHECK(Port_ContentIndexOf(PORT_CONTENT_CAR, id, &index) && index == 33, "index of the long id");
    Port_ContentReset();
}

int main(int argc, char **argv) {
    const char *gci = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--gci") == 0 && i + 1 < argc) {
            gci = argv[++i];
        } else {
            s_out = argv[i];
        }
    }
    SDL_CreateDirectory(s_out);
    TestGameCube(gci);
    TestRandom();
    TestJsonStrings();
    TestUnknownIds();
    TestAtomic();
    TestCorrupt();
    TestRotation();
    TestMigration();
    TestRegistry();
    TestExtraCar();
    TestIdsLock();
    TestContentLimits();
    Port_SavesForget();
    SDL_Log("%d checks, %d failed", s_checks, s_failed);
    return s_failed != 0;
}
