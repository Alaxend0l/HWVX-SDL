/*
 * saveimporttest.c - checks importing the releases' saves into the JSON
 * store (src/game/Port_SaveImport.c). Links only the store's files: no game,
 * no window, no game data.
 *
 * Run (from the build folder; every file it makes is under OUT):
 *   ./saveimporttest [OUT] [--samples DIR] [--orig DIR]
 *     OUT        scratch pref folders (default: saveimporttest-out)
 *     --samples  HWVX-SDL/build-saves: gc/78-GHWE-hwvx.gci, pc/velocityX.sav,
 *                ps2/BASLUS-20412.sav (saves made after the same progress,
 *                Challenge 1; the PC's also has a Challenge 2 save)
 *     --orig     orig/: gc-memcard/78-GHWE-hwvx.gci, ps2-memcard/Mcd001.ps2
 *   A missing sample skips its checks (logged). The samples are only read.
 *
 * What it covers:
 *   - every real save through every container it comes in: the .gci; the
 *     port's memcardA .sav and a raw GameCube card image built around the
 *     .gci's own card file (non-contiguous blocks, a stale directory copy);
 *     the PC's velocityX; the PS2's file, its card image (.ps2, with ECC)
 *     and a .psu made from the card's file. The three releases' Challenge 1
 *     saves agree on tracks, items, extras and script values, and differ
 *     only in the default cars; imported, the JSON keys them by id;
 *   - the union merge: unlocks, the higher value, script values from the
 *     save further on, a new save into a free slot, no room;
 *   - the PC/PS2 default cars offered in the preview, taken only when asked;
 *   - one save only; the preview changes no file; the source files are
 *     never written; memcardA/hwvx.sav is still imported first;
 *   - damaged and wrong files rejected with a reason, nothing written.
 */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"
#include "jsonc.h"

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
static const char *s_out = "saveimporttest-out";

#define CHECK(cond, ...) do { \
        s_checks++; \
        if (!(cond)) { \
            s_failed++; \
            SDL_Log("FAIL %s:%d: %s", __FILE__, __LINE__, #cond); \
            SDL_Log("     " __VA_ARGS__); \
        } \
    } while (0)

#define HW_SIZE ((u32)SAVE_FILE_SIZE(sizeof(HotWheelsGameData)))
static const PortImportOptions s_all = { -1, -1, 0 };

static void Scenario(const char *name) {
    static const char *const sub[] = { "saves/default/deleted/", "saves/default/", "saves/", "memcardA/", "" };
    size_t j;

    SDL_Log("== %s", name);
    SDL_snprintf(s_pref, sizeof(s_pref), "%s/%s/", s_out, name);
    for (j = 0; j < SDL_arraysize(sub); j++) {
        char dir[1200], **names;
        int n = 0, i;

        SDL_snprintf(dir, sizeof(dir), "%s%s", s_pref, sub[j]);
        names = SDL_GlobDirectory(dir, "*", 0, &n);
        for (i = 0; names != NULL && i < n; i++) {
            char path[1400];
            SDL_snprintf(path, sizeof(path), "%s%s", dir, names[i]);
            SDL_RemovePath(path);
        }
        SDL_free(names);
    }
    Port_SavesForget();
    s_log[0] = 0;
}

static void Store(const char *path, const void *data, size_t len) {
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != NULL, "can't write %s", path);
    if (io != NULL) {
        SDL_WriteIO(io, data, len);
        SDL_CloseIO(io);
    }
}

static char *ScratchPath(const char *name) {
    char *p = NULL;
    SDL_asprintf(&p, "%s/%s", s_out, name);
    return p;
}

static const HotWheelsGameData *Slot(const u8 *file, int slot) {
    return (const HotWheelsGameData *)SAVE_FILE_GAME_DATA(file, sizeof(HotWheelsGameData), slot);
}

static int SlotNamed(const u8 *file, const char *name) {
    const SaveFile *f = (const SaveFile *)file;
    int i;
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        if (f->settings.slots[i].valid && SDL_strncmp(f->settings.slots[i].name, name, 11) == 0) {
            return i;
        }
    }
    return -1;
}

static u64 Mask(const s8 *a, int n) {
    u64 m = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (a[i]) m |= 1ull << i;
    }
    return m;
}

#define CARS(g) Mask((g)->cars, sizeof((g)->cars))
#define TRACKS(g) Mask((g)->tracks, sizeof((g)->tracks))
#define ITEMS(g) Mask((g)->items, sizeof((g)->items))

static u64 Bits(const int *list) {
    u64 m = 0;
    for (; *list >= 0; list++) m |= 1ull << *list;
    return m;
}

/* What the example saves hold (docs/ps2-version/README.md §5.5). */
static const int s_ch1Tracks[] = { 0, 5, 19, 20, 21, 22, 29, 30, 31, 32, 39, 40, -1 };
static const int s_ch2Tracks[] = { 0, 5, 19, 20, 21, 22, 29, 30, 31, 32, 33, 39, 40, 41, -1 };
static const int s_items[] = { 0, 1, 2, 3, -1 };
static const int s_gcCars[] = { 3, 4, 5, -1 };
static const int s_pcCars[] = { 0, 1, 2, 3, 4, 5, -1 };

/* ---- the samples ------------------------------------------------------- */

typedef struct Sample {
    char *path;
    u8 *bytes;
    size_t len;
} Sample;

static Sample s_gci, s_gciOrig, s_pc, s_ps2, s_ps2Card;

static void LoadSample(Sample *s, const char *dir, const char *rel) {
    SDL_zerop(s);
    if (dir == NULL) {
        return;
    }
    SDL_asprintf(&s->path, "%s/%s", dir, rel);
    s->bytes = SDL_LoadFile(s->path, &s->len);
    if (s->bytes == NULL) {
        SDL_Log("   no sample %s: its checks are skipped", s->path);
    }
}

/* The samples are never written: the same bytes at the end. */
static void CheckUnchanged(const Sample *s) {
    size_t len;
    u8 *now;

    if (s->bytes == NULL) {
        return;
    }
    now = SDL_LoadFile(s->path, &len);
    CHECK(now != NULL && len == s->len && SDL_memcmp(now, s->bytes, len) == 0, "%s was changed", s->path);
    SDL_free(now);
}

/* ---- containers built around the real saves ---------------------------- */

static void PutBE16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void PutLE16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void PutLE32(u8 *p, u32 v) { PutLE16(p, (u16)v); PutLE16(p + 2, (u16)(v >> 16)); }

static void GcSums(const u8 *p, u32 bytes, u16 *sum, u16 *inv) {
    u32 i;
    u16 a = 0, c = 0;
    for (i = 0; i < bytes; i += 2) {
        u16 w = (u16)(p[i] << 8 | p[i + 1]);
        a += w;
        c += (u16)~w;
    }
    *sum = a == 0xFFFF ? 0 : a;
    *inv = c == 0xFFFF ? 0 : c;
}

static void SealDir(u8 *dir, u16 counter) {
    u16 s, i;
    PutBE16(dir + 0x1FFA, counter);
    GcSums(dir, 0x1FFC, &s, &i);
    PutBE16(dir + 0x1FFC, s);
    PutBE16(dir + 0x1FFE, i);
}

static void SealMap(u8 *map, u16 counter) {
    u16 s, i;
    PutBE16(map + 4, counter);
    GcSums(map + 4, 0x1FFC, &s, &i);
    PutBE16(map, s);
    PutBE16(map + 2, i);
}

/* A 4 Mbit card (64 blocks) as Dolphin keeps it: another game's 2-block
 * file in blocks 5 and 9, the .gci's file in block 7, so the block map is
 * followed; the backup directory copy is stale (an older counter, without
 * the save). */
static u8 *MakeGcCard(const Sample *gci, size_t *len) {
    u8 *card = SDL_malloc(64 * 0x2000), *dir = card + 0x2000, *old = card + 0x4000, *map = card + 0x6000;

    SDL_memset(card, 0, 64 * 0x2000);
    PutBE16(card + 0x22, 4);   /* 4 Mbit */
    SDL_memset(dir, 0xFF, 0x2000);
    SDL_memset(old, 0xFF, 0x2000);
    /* entry 0: another game's file, blocks 5 -> 9 */
    SDL_memcpy(dir, "GXXE01", 6);
    SDL_memset(dir + 8, 0, 32);
    SDL_strlcpy((char *)dir + 8, "other", 32);
    PutBE16(dir + 0x36, 5);
    PutBE16(dir + 0x38, 2);
    SDL_memcpy(old, dir, 0x40);
    /* entry 1: the .gci's entry, moved to block 7 */
    SDL_memcpy(dir + 0x40, gci->bytes, 0x40);
    PutBE16(dir + 0x40 + 0x36, 7);
    SDL_memcpy(card + 7 * 0x2000, gci->bytes + 0x40, 0x2000);
    SDL_memset(card + 5 * 0x2000, 0xAA, 0x2000);
    SDL_memset(card + 9 * 0x2000, 0xBB, 0x2000);
    SealDir(dir, 3);
    SealDir(old, 2);
    PutBE16(map + 0xA + (5 - 5) * 2, 9);
    PutBE16(map + 0xA + (9 - 5) * 2, 0xFFFF);
    PutBE16(map + 0xA + (7 - 5) * 2, 0xFFFF);
    SealMap(map, 3);
    SDL_memcpy(card + 0x8000, map, 0x2000);
    SealMap(card + 0x8000, 2);
    *len = 64 * 0x2000;
    return card;
}

/* A .psu of the PS2 save's folder: the folder's entry, "." and "..", then
 * icon.sys (made up) and the save, each padded to 1 KB. */
static u8 *MakePsu(const Sample *ps2, size_t *len) {
    static const char *const names[] = { "BASLUS-20412", ".", "..", "icon.sys", "BASLUS-20412" };
    size_t size = 512 * 4 + 1024 + 512 + ((ps2->len + 1023) & ~(size_t)1023), off = 0;
    u8 *psu = SDL_calloc(1, size);
    int i;

    for (i = 0; i < 5; i++) {
        u8 *e = psu + off;
        u32 flen = i == 3 ? 964 : i == 4 ? (u32)ps2->len : i == 0 ? 5 : 0;

        PutLE16(e, i < 3 ? 0x8427 : 0x8497);
        PutLE32(e + 4, flen);
        SDL_strlcpy((char *)e + 0x40, names[i], 32);
        off += 512;
        if (i == 3) {
            SDL_memset(psu + off, 0x50, 964);
            off += 1024;
        } else if (i == 4) {
            SDL_memcpy(psu + off, ps2->bytes, ps2->len);
            off += (ps2->len + 1023) & ~(size_t)1023;
        }
    }
    *len = off;
    return psu;
}

/* The port's own card file, as FileManager_NGC.c writes it: host order. */
static u8 *MakePortSav(const u8 *saveFile, size_t *len) {
    u8 *card = SDL_calloc(1, 0x2000);
    SDL_strlcpy((char *)card, "Hot Wheels Velocity X Save", 0x20);
    SDL_memcpy(card + 0x840, saveFile, HW_SIZE);
    *len = 0x2000;
    return card;
}

/* ---- scenarios --------------------------------------------------------- */

static PortImportSource s_src[PORT_RELEASE_COUNT];   /* the three releases' saves, read */

static int ReadOk(const void *bytes, size_t len, const char *name, int container, int release, PortImportSource *src) {
    char err[256];
    int ok = Port_SaveImportRead(bytes, len, name, src, err, sizeof(err));

    CHECK(ok, "%s: %s", name, err);
    if (ok) {
        CHECK(src->container == container, "%s read as %s", name, Port_SaveContainerName(src->container));
        CHECK(src->release == release, "%s read as the %s's", name, Port_SaveReleaseName(src->release));
        SDL_Log("   %s: %s, %s%s%s", name, Port_SaveReleaseName(src->release), Port_SaveContainerName(src->container),
                src->where[0] ? ", " : "", src->where);
    }
    return ok;
}

static void CheckChallenge1(const PortImportSource *src, const int *cars) {
    int k = SlotNamed(src->saveFile, "HW VEL X 1");
    const HotWheelsGameData *g;

    CHECK(k >= 0, "no \"HW VEL X 1\" in the %s's save", Port_SaveReleaseName(src->release));
    if (k < 0) {
        return;
    }
    g = Slot(src->saveFile, k);
    CHECK(CARS(g) == Bits(cars), "%s cars 0x%llx", Port_SaveReleaseName(src->release), (unsigned long long)CARS(g));
    CHECK(TRACKS(g) == Bits(s_ch1Tracks), "%s tracks 0x%llx", Port_SaveReleaseName(src->release), (unsigned long long)TRACKS(g));
    CHECK(ITEMS(g) == Bits(s_items), "%s items 0x%llx", Port_SaveReleaseName(src->release), (unsigned long long)ITEMS(g));
    CHECK(g->vehicle == 3 && g->item == 0, "%s vehicle %d item %d", Port_SaveReleaseName(src->release), (int)g->vehicle, (int)g->item);
}

static void TestContainers(void) {
    char err[256];
    PortImportSource x;
    size_t len;
    u8 *b;

    Scenario("containers");
    if (s_gci.bytes != NULL && ReadOk(s_gci.bytes, s_gci.len, s_gci.path, PORT_CONTAINER_GCI, PORT_RELEASE_GC, &s_src[PORT_RELEASE_GC])) {
        CheckChallenge1(&s_src[PORT_RELEASE_GC], s_gcCars);
        /* the same card file in the other containers reads the same */
        b = MakePortSav(s_src[PORT_RELEASE_GC].saveFile, &len);
        if (ReadOk(b, len, "memcardA/hwvx.sav", PORT_CONTAINER_PORT_SAV, PORT_RELEASE_GC, &x)) {
            CHECK(SDL_memcmp(x.saveFile, s_src[PORT_RELEASE_GC].saveFile, HW_SIZE) == 0, "the .sav reads otherwise");
            Port_SaveImportClose(&x);
        }
        SDL_free(b);
        b = MakeGcCard(&s_gci, &len);
        if (ReadOk(b, len, "MemoryCardA.USA.raw", PORT_CONTAINER_GC_CARD, PORT_RELEASE_GC, &x)) {
            CHECK(SDL_memcmp(x.saveFile, s_src[PORT_RELEASE_GC].saveFile, HW_SIZE) == 0, "the card reads otherwise");
            Port_SaveImportClose(&x);
        }
        {
            /* the main directory damaged: the backup (stale, without the
             * save) is the card's; the map damaged on both copies: refused */
            u8 *c = SDL_malloc(len);
            SDL_memcpy(c, b, len);
            c[0x2000 + 0x100] ^= 1;
            CHECK(!Port_SaveImportRead(c, len, "card", &x, err, sizeof(err)) && SDL_strstr(err, "no Hot Wheels") != NULL,
                  "a card read through a damaged directory (%s)", err);
            SDL_memcpy(c, b, len);
            c[0x6000 + 0x20] ^= 1;
            c[0x8000 + 0x20] ^= 1;
            CHECK(!Port_SaveImportRead(c, len, "card", &x, err, sizeof(err)) && SDL_strstr(err, "block map") != NULL,
                  "a damaged block map read (%s)", err);
            SDL_memcpy(c, b, len);
            c[0x2000 + 0x40 + 0x37] = 2;   /* first block 2: the directory */
            SealDir(c + 0x2000, 3);
            CHECK(!Port_SaveImportRead(c, len, "card", &x, err, sizeof(err)) && SDL_strstr(err, "chain") != NULL,
                  "a chain into the system area read (%s)", err);
            SDL_memcpy(c, b, len);
            c[7 * 0x2000 + 0x840 + 0x100] ^= 1;
            CHECK(!Port_SaveImportRead(c, len, "card", &x, err, sizeof(err)) && SDL_strstr(err, "checksum") != NULL,
                  "a damaged save on the card read (%s)", err);
            SDL_memset(c + 0x2000, 0, 0x4000);
            CHECK(!Port_SaveImportRead(c, len, "card", &x, err, sizeof(err)) && SDL_strstr(err, "not a GameCube") != NULL,
                  "a file of 64 blocks with no directory read as a card (%s)", err);
            SDL_free(c);
        }
        SDL_free(b);
    }
    if (s_gciOrig.bytes != NULL && ReadOk(s_gciOrig.bytes, s_gciOrig.len, s_gciOrig.path, PORT_CONTAINER_GCI, PORT_RELEASE_GC, &x)) {
        CheckChallenge1(&x, s_gcCars);
        Port_SaveImportClose(&x);
    }
    if (s_pc.bytes != NULL && ReadOk(s_pc.bytes, s_pc.len, s_pc.path, PORT_CONTAINER_PC_FILE, PORT_RELEASE_PC, &s_src[PORT_RELEASE_PC])) {
        const PortImportSource *s = &s_src[PORT_RELEASE_PC];
        int k = SlotNamed(s->saveFile, "HW VEL X 2");
        const SaveFile *f = (const SaveFile *)s->saveFile;

        CheckChallenge1(s, s_pcCars);
        CHECK(k >= 0 && TRACKS(Slot(s->saveFile, k)) == Bits(s_ch2Tracks), "the PC's Challenge 2 save");
        /* settings moved to the GameCube's layout; the PC's own bytes kept */
        CHECK(f->settings.audio.sfxVolume == 100 && f->settings.audio.musicVolume == 100, "PC volumes");
        SDL_Log("   PC controller 0: %02x %02x %02x %02x %02x %02x %02x, buttons %d..%d", f->settings.controllers[0].data[0],
                f->settings.controllers[0].data[1], f->settings.controllers[0].data[2], f->settings.controllers[0].data[3],
                f->settings.controllers[0].data[4], f->settings.controllers[0].data[5], f->settings.controllers[0].data[6],
                s->pc[0].buttons[0], s->pc[0].buttons[12]);
        CHECK(f->settings.controllers[0].data[0] == 1 && f->settings.controllers[0].data[6] == 0x26,
              "PC controller settings not in the GameCube's layout");
        CHECK(s->pc[0].buttons[0] == 10 && s->pc[0].buttons[11] == 13 && s->pc[0].buttons[12] == 1, "PC button map");
    }
    if (s_ps2.bytes != NULL && ReadOk(s_ps2.bytes, s_ps2.len, s_ps2.path, PORT_CONTAINER_PS2_FILE, PORT_RELEASE_PS2, &s_src[PORT_RELEASE_PS2])) {
        const SaveFile *f = (const SaveFile *)s_src[PORT_RELEASE_PS2].saveFile;

        CheckChallenge1(&s_src[PORT_RELEASE_PS2], s_pcCars);
        CHECK(f->settings.audio.sfxVolume == 100 && f->settings.audio.speakerMode == 1 && s_src[PORT_RELEASE_PS2].ps2Settings0 == 1,
              "PS2 settings (packed) misread");
        CHECK(f->settings.controllers[3].data[0] == 1 && f->settings.controllers[3].data[6] == 0x26, "PS2 controllers misread");
        b = MakePsu(&s_ps2, &len);
        if (ReadOk(b, len, "BASLUS-20412.psu", PORT_CONTAINER_PSU, PORT_RELEASE_PS2, &x)) {
            CHECK(SDL_memcmp(x.saveFile, f, HW_SIZE) == 0, "the .psu reads otherwise");
            Port_SaveImportClose(&x);
        }
        b[512 * 5 + 1024 + 0x100] ^= 1;
        CHECK(!Port_SaveImportRead(b, len, "x.psu", &x, err, sizeof(err)) && SDL_strstr(err, "checksum") != NULL,
              "a damaged .psu read (%s)", err);
        CHECK(!Port_SaveImportRead(b, len - 1500, "x.psu", &x, err, sizeof(err)) && SDL_strstr(err, "cut short") != NULL,
              "a cut .psu read (%s)", err);
        SDL_free(b);
    }
    if (s_ps2Card.bytes != NULL && ReadOk(s_ps2Card.bytes, s_ps2Card.len, s_ps2Card.path, PORT_CONTAINER_PS2_CARD, PORT_RELEASE_PS2, &x)) {
        CHECK(s_src[PORT_RELEASE_PS2].saveFile == NULL || SDL_memcmp(x.saveFile, s_src[PORT_RELEASE_PS2].saveFile, HW_SIZE) == 0,
              "the card's save isn't the extracted file's");
        CHECK(SDL_strcmp(x.where, "/BASLUS-20412/BASLUS-20412") == 0, "found at %s", x.where);
        Port_SaveImportClose(&x);
        {
            u8 *c = SDL_malloc(s_ps2Card.len);
            SDL_memcpy(c, s_ps2Card.bytes, s_ps2Card.len);
            c[0x28] = 3;   /* page size */
            CHECK(!Port_SaveImportRead(c, s_ps2Card.len, "card.ps2", &x, err, sizeof(err)) && SDL_strstr(err, "superblock") != NULL,
                  "a damaged superblock read (%s)", err);
            SDL_memcpy(c, s_ps2Card.bytes, s_ps2Card.len);
            SDL_memset(c + 0x50, 0, 0x80);   /* no FAT */
            CHECK(!Port_SaveImportRead(c, s_ps2Card.len, "card.ps2", &x, err, sizeof(err)) && SDL_strstr(err, "FAT") != NULL,
                  "a card without its FAT read (%s)", err);
            CHECK(!Port_SaveImportRead(c, 0x2000, "card.ps2", &x, err, sizeof(err)), "a card cut to 8 KB read");
            SDL_free(c);
        }
    }

    /* the three releases' Challenge 1 saves: the same progress */
    if (s_src[0].saveFile && s_src[1].saveFile && s_src[2].saveFile) {
        const HotWheelsGameData *g[3];
        int r;
        for (r = 0; r < 3; r++) {
            g[r] = Slot(s_src[r].saveFile, SlotNamed(s_src[r].saveFile, "HW VEL X 1"));
        }
        for (r = 1; r < 3; r++) {
            CHECK(TRACKS(g[r]) == TRACKS(g[0]) && ITEMS(g[r]) == ITEMS(g[0]), "%s's tracks/items differ from the GameCube's",
                  Port_SaveReleaseName(r));
            CHECK(SDL_memcmp(g[r]->extras, g[0]->extras, sizeof(g[0]->extras)) == 0 &&
                  SDL_memcmp(g[r]->cheats, g[0]->cheats, sizeof(g[0]->cheats)) == 0 &&
                  SDL_memcmp(g[r]->miscValues, g[0]->miscValues, sizeof(g[0]->miscValues)) == 0,
                  "%s's extras/cheats/script values differ", Port_SaveReleaseName(r));
            CHECK((CARS(g[r]) & ~7ull) == CARS(g[0]), "%s's cars past the defaults differ", Port_SaveReleaseName(r));
        }
        SDL_Log("   the three Challenge 1 saves agree: tracks 0x%llx, items 0x%llx",
                (unsigned long long)TRACKS(g[0]), (unsigned long long)ITEMS(g[0]));
    }
}

/* The unlocked ids of save `name` in the store's JSON, as a list. */
static void JsonUnlocked(const char *name, const char *section, char *out, size_t size) {
    char path[1200];
    size_t len;
    char *text, err[200];
    JsonNode *root;
    const JsonNode *s, *m;

    out[0] = 0;
    SDL_snprintf(path, sizeof(path), "%ssaves/default/hwvx.json", s_pref);
    text = SDL_LoadFile(path, &len);
    root = text != NULL ? Json_Parse(text, len, err, sizeof(err)) : NULL;
    SDL_free(text);
    for (s = root != NULL ? Json_Get(root, "saves")->child : NULL; s != NULL; s = s->next) {
        if (SDL_strcmp(Json_GetString(s, "name", ""), name) != 0) {
            continue;
        }
        for (m = Json_Get(s, section)->child; m != NULL; m = m->next) {
            if (Json_GetBool(m, "unlocked", 0)) {
                SDL_strlcat(out, m->key, size);
                SDL_strlcat(out, " ", size);
            }
        }
    }
    Json_Free(root);
}

/* Each release's save imported into an empty store: the JSON keys the same
 * progress by the same ids. */
static void TestJson(void) {
    char tracks[3][2048], items[3][512], cars[3][512];
    PortImportPlan plan;
    char err[256];
    int r;

    for (r = 0; r < PORT_RELEASE_COUNT; r++) {
        char name[64];
        SDL_snprintf(name, sizeof(name), "json-%s", Port_SaveReleaseName(r));
        Scenario(name);
        tracks[r][0] = items[r][0] = cars[r][0] = 0;
        if (s_src[r].saveFile == NULL) {
            continue;
        }
        CHECK(Port_SaveImportApply(&s_src[r], 1, &s_all, &plan, err, sizeof(err)), "%s", err);
        JsonUnlocked("HW VEL X 1", "tracks", tracks[r], sizeof(tracks[r]));
        JsonUnlocked("HW VEL X 1", "items", items[r], sizeof(items[r]));
        JsonUnlocked("HW VEL X 1", "cars", cars[r], sizeof(cars[r]));
        SDL_Log("   %s: cars %s", Port_SaveReleaseName(r), cars[r]);
        CHECK(SDL_strstr(tracks[r], "base://mcp/track/19 ") != NULL && SDL_strstr(items[r], "base://mcp/item/3 ") != NULL,
              "%s: ids missing from the JSON: %s", Port_SaveReleaseName(r), tracks[r]);
        /* without the choice, a PC or PS2 save gives a new save cars 3-5 only */
        CHECK(SDL_strcmp(cars[r], "base://HW_Cars/car_3 base://HW_Cars/car_4 base://HW_Cars/car_5 ") == 0,
              "%s: cars %s", Port_SaveReleaseName(r), cars[r]);
    }
    if (tracks[0][0] && tracks[1][0] && tracks[2][0]) {
        CHECK(SDL_strcmp(tracks[0], tracks[1]) == 0 && SDL_strcmp(tracks[0], tracks[2]) == 0, "the JSON tracks differ");
        CHECK(SDL_strcmp(items[0], items[1]) == 0 && SDL_strcmp(items[0], items[2]) == 0, "the JSON items differ");
        SDL_Log("   JSON tracks of all three: %s", tracks[0]);
    }
}

/* A small save in memory, as a source: slot 0 named `name`. */
static void MakeSource(PortImportSource *s, int release, const char *name, const int *cars, const int *tracks, u16 misc0) {
    SaveFile *f;
    HotWheelsGameData *g;

    SDL_zerop(s);
    s->release = release;
    SDL_strlcpy(s->path, name, sizeof(s->path));
    s->saveFile = SDL_calloc(1, HW_SIZE);
    f = (SaveFile *)s->saveFile;
    g = (HotWheelsGameData *)SAVE_FILE_GAME_DATA(s->saveFile, sizeof(HotWheelsGameData), 0);
    f->magic = SAVE_FILE_MAGIC;
    f->settings.audio.sfxVolume = 77;
    f->settings.slots[0].valid = 1;
    SDL_strlcpy(f->settings.slots[0].name, name, 11);
    f->settings.slots[0].year = 2026;
    for (; *cars >= 0; cars++) g->cars[*cars] = 1;
    for (; *tracks >= 0; tracks++) g->tracks[*tracks] = 1;
    g->miscValues[0] = misc0;
    g->vehicle = 3;
}

static char *LoadFileAt(const char *dir, const char *rel, size_t *len) {
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", dir, rel);
    return SDL_LoadFile(path, len);
}

static int FileSame(const char *path, const char *text, size_t len) {
    size_t n;
    char *now = SDL_LoadFile(path, &n);
    int same = now != NULL && n == len && SDL_memcmp(now, text, len) == 0;
    SDL_free(now);
    return same;
}

static void TestMerge(void) {
    PortImportSource a, b, c;
    PortImportPlan plan;
    PortImportOptions opt = s_all;
    u8 *t = SDL_calloc(1, HW_SIZE), *r = SDL_malloc(HW_SIZE);
    static const int none[] = { -1 }, c35[] = { 3, 4, 5, -1 }, c05[] = { 0, 1, 2, 3, 4, 5, 7, -1 };
    static const int t1[] = { 0, 5, -1 }, t2[] = { 5, 33, -1 };
    char line[512];
    int i;

    Scenario("merge");
    /* the store: a GameCube save; then a PC one of the same name */
    MakeSource(&a, PORT_RELEASE_GC, "HW VEL X 1", c35, t1, 3);
    MakeSource(&b, PORT_RELEASE_PC, "HW VEL X 1", c05, t2, 9);
    Port_SaveImportMerge(t, &a, 1, &opt, &plan);
    CHECK(plan.numSlots == 1 && plan.slots[0].isNew && plan.slots[0].into == 0, "into an empty save: a new one");
    CHECK(((SaveFile *)t)->settings.audio.sfxVolume == 77, "a new save's settings aren't the file's");
    Port_SaveImportMerge(t, &b, 1, &opt, &plan);
    Port_SaveImportDescribe(&plan, 0, &opt, line, sizeof(line));
    SDL_Log("   %s", line);
    CHECK(plan.numSlots == 1 && !plan.slots[0].isNew && plan.slots[0].into == 0, "not merged with the save of its name");
    CHECK(plan.slots[0].added[PORT_SAVE_IDS_TRACKS] == 1ull << 33, "tracks added 0x%llx", (unsigned long long)plan.slots[0].added[1]);
    CHECK(plan.slots[0].added[PORT_SAVE_IDS_CARS] == 1ull << 7, "cars added 0x%llx (the defaults taken?)",
          (unsigned long long)plan.slots[0].added[0]);
    CHECK(plan.defaultCarsOffered == 7 && plan.slots[0].defaultCars == 7, "the default cars aren't offered");
    CHECK(TRACKS(Slot(t, 0)) == ((1ull << 0) | (1ull << 5) | (1ull << 33)), "tracks aren't the union");
    CHECK(CARS(Slot(t, 0)) == 0xB8, "cars 0x%llx", (unsigned long long)CARS(Slot(t, 0)));
    CHECK(plan.slots[0].scriptValues && Slot(t, 0)->miscValues[0] == 9, "script values not taken from the save further on");
    CHECK(SDL_strstr(line, "default cars (0 1 2) left out") != NULL, "the preview doesn't say so: %s", line);

    /* asked: the default cars too */
    opt.defaultCars = 1;
    Port_SaveImportMerge(t, &b, 1, &opt, &plan);
    CHECK(plan.slots[0].added[PORT_SAVE_IDS_CARS] == 7 && CARS(Slot(t, 0)) == 0xBF, "the default cars not taken when asked");
    opt.defaultCars = 0;

    /* again: nothing new; a lesser save keeps the script values */
    Port_SaveImportMerge(t, &b, 1, &opt, &plan);
    CHECK(plan.changes == 0, "a second import changes %d things", plan.changes);
    Port_SaveImportMerge(t, &a, 1, &opt, &plan);
    CHECK(plan.changes == 0 && Slot(t, 0)->miscValues[0] == 9, "a save behind took over the script values");

    /* the higher value; a later date */
    MakeSource(&c, PORT_RELEASE_GC, "HW VEL X 1", none, none, 0);
    ((HotWheelsGameData *)SAVE_FILE_GAME_DATA(c.saveFile, sizeof(HotWheelsGameData), 0))->tracks[5] = 2;
    ((SaveFile *)c.saveFile)->settings.slots[0].year = 2030;
    Port_SaveImportMerge(t, &c, 1, &opt, &plan);
    CHECK(plan.slots[0].raised == 1 && Slot(t, 0)->tracks[5] == 2, "the higher value didn't win");
    CHECK(((SaveFile *)t)->settings.slots[0].year == 2030, "the later date not taken");
    CHECK(Port_SaveChecksum(t, Port_SaveKindByName("hwvx")) == ((SaveFile *)t)->checksum, "the checksum isn't redone");
    Port_SaveImportClose(&c);

    /* another name: a new save in a free slot; one slot only */
    MakeSource(&c, PORT_RELEASE_PS2, "OTHER", c05, t2, 1);
    {
        SaveFile *cf = (SaveFile *)c.saveFile;
        cf->settings.slots[3] = cf->settings.slots[0];
        SDL_memcpy(SAVE_FILE_GAME_DATA(c.saveFile, sizeof(HotWheelsGameData), 3),
                   SAVE_FILE_GAME_DATA(c.saveFile, sizeof(HotWheelsGameData), 0), sizeof(HotWheelsGameData));
        SDL_strlcpy(cf->settings.slots[3].name, "THIRD", 11);
    }
    opt.onlySource = 0;
    opt.onlySlot = 3;
    Port_SaveImportMerge(t, &c, 1, &opt, &plan);
    CHECK(plan.numSlots == 1 && plan.slots[0].from == 3 && plan.slots[0].isNew && plan.slots[0].into == 1,
          "one save only: %d saves, from %d into %d", plan.numSlots, plan.slots[0].from, plan.slots[0].into);
    CHECK(SlotNamed(t, "OTHER") < 0 && SlotNamed(t, "THIRD") == 1, "slot 0 imported though only 3 was asked");
    CHECK(CARS(Slot(t, 1)) == ((1ull << 3) | (1ull << 4) | (1ull << 5) | (1ull << 7)), "a new save's cars 0x%llx",
          (unsigned long long)CARS(Slot(t, 1)));
    opt = s_all;

    /* no room: eight saves of other names */
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        SaveFile *tf = (SaveFile *)t;
        tf->settings.slots[i].valid = 1;
        SDL_snprintf(tf->settings.slots[i].name, 11, "FULL %d", i);
    }
    Port_SaveImportMerge(t, &b, 1, &opt, &plan);
    CHECK(plan.numSlots == 1 && plan.slots[0].into < 0 && plan.changes == 0, "a ninth save went somewhere");
    Port_SaveImportDescribe(&plan, 0, &opt, line, sizeof(line));
    CHECK(SDL_strstr(line, "no room") != NULL, "%s", line);

    Port_SaveImportClose(&a);
    Port_SaveImportClose(&b);
    Port_SaveImportClose(&c);
    SDL_free(t);
    SDL_free(r);
}

/* Preview and apply on the store: the preview writes nothing, the apply
 * merges into the existing save, the memory card file is imported first. */
static void TestStore(void) {
    PortImportPlan plan;
    PortImportOptions opt = s_all;
    char err[256], dir[1200], line[512];
    u8 *r = SDL_malloc(HW_SIZE);
    size_t len;
    char *before;
    int i, k;

    if (s_src[PORT_RELEASE_GC].saveFile == NULL || s_src[PORT_RELEASE_PC].saveFile == NULL) {
        SDL_Log("   store: skipped (no samples)");
        SDL_free(r);
        return;
    }

    /* a GameCube player: memcardA/hwvx.sav from the port, no JSON save yet */
    Scenario("store");
    {
        u8 *sav = MakePortSav(s_src[PORT_RELEASE_GC].saveFile, &len);
        SDL_snprintf(dir, sizeof(dir), "%smemcardA/", s_pref);
        SDL_CreateDirectory(dir);
        SDL_strlcat(dir, "hwvx.sav", sizeof(dir));
        Store(dir, sav, len);
        SDL_free(sav);
    }
    CHECK(Port_SaveImportPreview(&s_src[PORT_RELEASE_PC], 1, &opt, &plan, err, sizeof(err)), "%s", err);
    SDL_Log("   preview into %s:", plan.target);
    for (i = 0; i < plan.numSlots; i++) {
        Port_SaveImportDescribe(&plan, i, &opt, line, sizeof(line));
        SDL_Log("     %s", line);
    }
    CHECK(SDL_strstr(plan.target, "memcardA/hwvx.sav") != NULL, "the preview doesn't merge into the card file's save");
    SDL_snprintf(dir, sizeof(dir), "%ssaves/", s_pref);
    CHECK(!SDL_GetPathInfo(dir, NULL), "the preview wrote to the store");
    CHECK(plan.numSlots == 2 && plan.defaultCarsOffered == 7, "PC: %d saves, default cars offered 0x%llx", plan.numSlots,
          (unsigned long long)plan.defaultCarsOffered);
    for (i = 0; i < plan.numSlots; i++) {
        const PortImportSlot *s = &plan.slots[i];
        if (SDL_strcmp(s->name, "HW VEL X 1") == 0) {
            CHECK(!s->isNew && s->changes == 0 && s->defaultCars == 7, "HW VEL X 1: merged, nothing but the default cars");
        } else {
            CHECK(s->isNew && s->added[PORT_SAVE_IDS_TRACKS] == Bits(s_ch2Tracks), "HW VEL X 2: a new save with its tracks");
        }
    }

    /* the player ticks the default cars and imports */
    opt.defaultCars = 1;
    CHECK(Port_SaveImportApply(&s_src[PORT_RELEASE_PC], 1, &opt, &plan, err, sizeof(err)), "%s", err);
    SDL_snprintf(dir, sizeof(dir), "%ssaves/default/hwvx.imported", s_pref);
    CHECK(SDL_GetPathInfo(dir, NULL), "the card file wasn't imported first (no marker)");
    Port_SavesForget();
    CHECK(Port_SavesRead("hwvx", r, HW_SIZE), "the store doesn't read");
    k = SlotNamed(r, "HW VEL X 1");
    CHECK(k >= 0 && CARS(Slot(r, k)) == Bits(s_pcCars), "GameCube save + PC defaults: cars 0x%llx",
          (unsigned long long)(k >= 0 ? CARS(Slot(r, k)) : 0));
    k = SlotNamed(r, "HW VEL X 2");
    CHECK(k >= 0 && TRACKS(Slot(r, k)) == Bits(s_ch2Tracks), "the PC's second save missing");
    {
        char *text = LoadFileAt(s_pref, "saves/default/hwvx.json", &len);
        CHECK(text != NULL && SDL_strstr(text, "\"imported_from\": \"memcardA/hwvx.sav\"") != NULL,
              "imported_from isn't where the save began");
        SDL_free(text);
    }
    CHECK(Port_SavesExists("hwvx"), "no save");
    {
        char *bak = NULL;
        SDL_asprintf(&bak, "%ssaves/default/hwvx.1.json", s_pref);
        CHECK(SDL_GetPathInfo(bak, NULL), "the save before the import isn't kept as a backup");
        SDL_free(bak);
    }

    /* the same again: a preview of nothing, and nothing is written */
    before = LoadFileAt(s_pref, "saves/default/hwvx.json", &len);
    CHECK(Port_SaveImportApply(&s_src[PORT_RELEASE_PC], 1, &opt, &plan, err, sizeof(err)) && plan.changes == 0,
          "a second import changed %d things", plan.changes);
    SDL_snprintf(dir, sizeof(dir), "%ssaves/default/hwvx.json", s_pref);
    CHECK(before != NULL && FileSame(dir, before, len), "an import of nothing new rewrote the save");

    /* all three at once, one of them only partly */
    if (s_src[PORT_RELEASE_PS2].saveFile != NULL) {
        opt.defaultCars = 0;
        CHECK(Port_SaveImportPreview(s_src, 3, &opt, &plan, err, sizeof(err)), "%s", err);
        CHECK(plan.numSlots == 4 && plan.changes == 0, "three files: %d saves, %d changes", plan.numSlots, plan.changes);
        CHECK(before != NULL && FileSame(dir, before, len), "a preview changed the save");
    }
    SDL_free(before);

    /* a damaged save with no good backup: nothing is merged into it */
    Scenario("store-damaged");
    SDL_snprintf(dir, sizeof(dir), "%ssaves/default/", s_pref);
    SDL_CreateDirectory(dir);
    SDL_strlcat(dir, "hwvx.json", sizeof(dir));
    Store(dir, "{\"format_version\": 1, \"game\": \"hwvx\", \"saves\": 7}\n// sha256: 00\n", 60);
    CHECK(!Port_SaveImportApply(&s_src[PORT_RELEASE_PC], 1, &opt, &plan, err, sizeof(err)) && SDL_strstr(err, "damaged") != NULL,
          "an import into a damaged save (%s)", err);
    CHECK(FileSame(dir, "{\"format_version\": 1, \"game\": \"hwvx\", \"saves\": 7}\n// sha256: 00\n", 60),
          "the damaged save was changed");
    SDL_free(r);
}

/* Files that aren't saves, or are damaged: refused, with a reason. */
static void TestRejected(void) {
    PortImportSource x;
    char err[256];
    u8 junk[0x3000];
    size_t i;

    Scenario("rejected");
    for (i = 0; i < sizeof(junk); i++) {
        junk[i] = (u8)(i * 7 + 3);
    }
    CHECK(!Port_SaveImportRead(junk, sizeof(junk), "junk", &x, err, sizeof(err)) && err[0], "junk read");
    SDL_Log("   junk: %s", err);
    CHECK(!Port_SaveImportRead(junk, 0, "empty", &x, err, sizeof(err)) && err[0], "an empty file read");
    CHECK(!Port_SaveImportRead("Ps2PowerSave\0\0\0\0", 16, "x.max", &x, err, sizeof(err)) && SDL_strstr(err, ".max") != NULL,
          "a .max: %s", err);
    SDL_Log("   .max: %s", err);
    {
        /* the port's card file cut short: the header there, the rest not
         * (a buffer of exactly that size, for ASan) */
        size_t n = 0x840 + 0x40;
        u8 *b = SDL_calloc(1, n);
        PutLE32(b + 0x840, SAVE_FILE_MAGIC);
        PutLE32(b + 0x844, HW_SIZE - 12);
        CHECK(!Port_SaveImportRead(b, n, "hwvx.sav", &x, err, sizeof(err)) && SDL_strstr(err, "cut short") != NULL,
              "a cut .sav (%s)", err);
        SDL_free(b);
    }
    {
        /* a PS2 card image that is only its superblock's first 0x40 bytes */
        size_t n = 0x40;
        u8 *b = SDL_calloc(1, n);
        SDL_memcpy(b, "Sony PS2 Memory Card Format 1.2.0.0", 0x24);
        PutLE16(b + 0x28, 512);
        PutLE16(b + 0x2A, 2);
        PutLE32(b + 0x30, 8);
        CHECK(!Port_SaveImportRead(b, n, "cut.ps2", &x, err, sizeof(err)) && SDL_strstr(err, "cut short") != NULL,
              "a cut .ps2 (%s)", err);
        SDL_free(b);
    }
    if (s_gci.bytes != NULL) {
        u8 *b = SDL_malloc(s_gci.len);
        SDL_memcpy(b, s_gci.bytes, s_gci.len);
        CHECK(!Port_SaveImportRead(b, s_gci.len - 100, "cut.gci", &x, err, sizeof(err)) && SDL_strstr(err, "blocks") != NULL,
              "a cut .gci (%s)", err);
        b[0x40 + 0x840 + 0x200] ^= 0x10;
        CHECK(!Port_SaveImportRead(b, s_gci.len, "bad.gci", &x, err, sizeof(err)) && SDL_strstr(err, "checksum") != NULL,
              "a damaged .gci (%s)", err);
        SDL_memcpy(b, s_gci.bytes, s_gci.len);
        SDL_strlcpy((char *)b + 8, "doomsdayrescue", 32);
        CHECK(!Port_SaveImportRead(b, s_gci.len, "ddr.gci", &x, err, sizeof(err)) && SDL_strstr(err, "another kind") != NULL,
              "another file of the game's (%s)", err);
        SDL_free(b);
    }
    if (s_pc.bytes != NULL) {
        u8 *b = SDL_malloc(s_pc.len);
        SDL_memcpy(b, s_pc.bytes, s_pc.len);
        b[0x300] ^= 4;
        CHECK(!Port_SaveImportRead(b, s_pc.len, "velocityX", &x, err, sizeof(err)) && SDL_strstr(err, "checksum") != NULL,
              "a damaged PC save (%s)", err);
        CHECK(!Port_SaveImportRead(s_pc.bytes, s_pc.len - 1, "velocityX", &x, err, sizeof(err)) && SDL_strstr(err, "cut short") != NULL,
              "a cut PC save (%s)", err);
        SDL_free(b);
    }
    if (s_ps2.bytes != NULL) {
        u8 *b = SDL_malloc(s_ps2.len);
        SDL_memcpy(b, s_ps2.bytes, s_ps2.len);
        b[4] = 0x30;
        b[5] = 0xE8;   /* a Doomsday Rescue save's size */
        CHECK(!Port_SaveImportRead(b, s_ps2.len, "ddr", &x, err, sizeof(err)) && SDL_strstr(err, "Doomsday") != NULL,
              "a DD Rescue save (%s)", err);
        SDL_free(b);
    }
    {
        char *p = ScratchPath("no-such-file");
        CHECK(!Port_SaveImportOpen(p, &x, err, sizeof(err)), "a missing file opened");
        SDL_free(p);
        CHECK(!Port_SaveImportOpen(s_out, &x, err, sizeof(err)) && SDL_strstr(err, "not a file") != NULL, "a folder opened");
    }
    /* through the file path too */
    if (s_ps2Card.bytes != NULL) {
        PortImportSource y;
        CHECK(Port_SaveImportOpen(s_ps2Card.path, &y, err, sizeof(err)), "%s", err);
        Port_SaveImportClose(&y);
    }
}

int main(int argc, char **argv) {
    const char *samples = NULL, *orig = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
            samples = argv[++i];
        } else if (SDL_strcmp(argv[i], "--orig") == 0 && i + 1 < argc) {
            orig = argv[++i];
        } else {
            s_out = argv[i];
        }
    }
    SDL_CreateDirectory(s_out);
    LoadSample(&s_gci, samples, "gc/78-GHWE-hwvx.gci");
    LoadSample(&s_pc, samples, "pc/velocityX.sav");
    LoadSample(&s_ps2, samples, "ps2/BASLUS-20412.sav");
    LoadSample(&s_gciOrig, orig, "gc-memcard/78-GHWE-hwvx.gci");
    LoadSample(&s_ps2Card, orig, "ps2-memcard/Mcd001.ps2");
    if (s_gci.bytes == NULL && s_gciOrig.bytes != NULL) {
        s_gci = s_gciOrig;
    }

    TestContainers();
    TestJson();
    TestMerge();
    TestStore();
    TestRejected();
    Port_SavesForget();

    CheckUnchanged(&s_gci);
    CheckUnchanged(&s_pc);
    CheckUnchanged(&s_ps2);
    CheckUnchanged(&s_gciOrig);
    CheckUnchanged(&s_ps2Card);
    for (i = 0; i < PORT_RELEASE_COUNT; i++) {
        Port_SaveImportClose(&s_src[i]);
    }
    SDL_Log("%d checks, %d failed", s_checks, s_failed);
    return s_failed != 0;
}
