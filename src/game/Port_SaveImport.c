/* Importing the releases' saves into the JSON store (task 4d). Port-only
 * file. docs/xdl-format/README.md §11.3-§11.5.
 *
 * Every release keeps the same SaveFile (SAVE_FILE_MAGIC 4, the size that
 * follows, the checksum: sum of byte + 1, the settings, 8 x 0xF4
 * HotWheelsGameData) in its own byte order, settings layout and container:
 *
 *   GameCube  big-endian; settings 0x150: s32 difficulty, audio, 4 x 0x12
 *             controllers, 8 slot entries. Card file GHWE78 "hwvx": name,
 *             comment, banner and icons, the SaveFile at +0x840. In a
 *             Dolphin .gci (a 0x40-byte directory entry, then the file's
 *             blocks) or a raw card image (the card's file system, below).
 *             The port's own memcardA/hwvx.sav is that card file in the
 *             host's order.
 *   PC        little-endian, one file "velocityX", no container; settings
 *             0x188: each player's controller settings are 0x20 bytes,
 *             the GameCube's 0x12 (vibration, scheme, the 16 binding
 *             bytes) and a 13-byte gamepad button map, 1 byte padding.
 *   PS2       little-endian; settings 0x150 but packed: u8 at +0 (the
 *             GameCube's s32 difficulty; 1 by default), audio at +1,
 *             controllers at +5 (GameSettings_GetControllerSettings), 3
 *             bytes of padding, slots at +0x50 as the GameCube's. File
 *             BASLUS-20412/BASLUS-20412 on the card (its file system,
 *             below), or in a .psu export.
 *
 * Each is checked (magic, size, its own checksum over its own bytes), moved
 * into the GameCube's layout and read by Port_SaveFromBinary; the slots are
 * the same struct on all three.
 *
 * The merge (Port_SaveImportMerge) is a union, slot by slot: a save goes
 * into the store's save of the same name, else into a free slot as a new
 * save. Unlocks: an unlock in either counts; cars, tracks, items, cheats and
 * extras take the higher value. Script values (miscValues, the MCP script's
 * own numbers) are taken as a set from whichever save is further on (more
 * unlocks; a tie keeps the store's), so the set is always one the game
 * wrote. The chosen vehicle and item, name and settings stay the store's;
 * the date becomes the later one. Cars 0-2 of a PC or PS2 save are that
 * release's defaults (the GameCube's come from the GBA link): they are
 * offered, not taken, unless opt->defaultCars. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"

#define HW_DATA ((u32)sizeof(HotWheelsGameData))
#define HW_SIZE ((u32)SAVE_FILE_SIZE(sizeof(HotWheelsGameData)))
#define HW_VERSION ((u32)(sizeof(GeneralGameSettings) + HW_DATA * SAVE_FILE_SLOTS))   /* 0x8F0 */
#define PC_SETTINGS 0x188u
#define PC_VERSION (PC_SETTINGS + HW_DATA * SAVE_FILE_SLOTS)                            /* 0x928 */
#define DDR_VERSION ((u32)(sizeof(GeneralGameSettings) + sizeof(DDRescueGameData) * SAVE_FILE_SLOTS))
#define PC_DDR_VERSION (PC_SETTINGS + (u32)sizeof(DDRescueGameData) * SAVE_FILE_SLOTS)
#define CARD_DATA 0x840u            /* the SaveFile in a GameCube card file */
#define GC_BLOCK 0x2000u
#define MAX_FILE (64u << 20)        /* nothing to import is bigger */

static const char *const s_releases[PORT_RELEASE_COUNT] = { "GameCube", "PC", "PS2" };
static const char *const s_containers[PORT_CONTAINER_COUNT] = {
    "the port's memory card file (.sav)",
    "Dolphin .gci export",
    "GameCube memory card image",
    "PC save file (velocityX)",
    "PS2 save file",
    "PS2 memory card image (.ps2)",
    "PS2 save export (.psu)",
};

const char *Port_SaveReleaseName(int release) {
    return release >= 0 && release < PORT_RELEASE_COUNT ? s_releases[release] : "?";
}

const char *Port_SaveContainerName(int container) {
    return container >= 0 && container < PORT_CONTAINER_COUNT ? s_containers[container] : "?";
}

static u16 GetBE16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 GetBE32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 GetLE16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 GetLE32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void PutLE32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static int Fail(char *err, size_t errSize, const char *fmt, ...) PORT_PRINTF(3, 4);
static int Fail(char *err, size_t errSize, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(err, errSize, fmt, ap);
    va_end(ap);
    return 0;
}

/* ---- the releases' SaveFiles -------------------------------------------- */

static u32 ByteSum(const u8 *p, u32 n) {
    u32 sum = 0;
    while (n-- > 0) {
        sum += *p++ + 1u;
    }
    return sum;
}

/* A little-endian SaveFile's header and checksum, over its own bytes. */
static int CheckLittle(const u8 *b, size_t len, u32 version, char *err, size_t errSize) {
    if (len < 12 || GetLE32(b) != SAVE_FILE_MAGIC) {
        return Fail(err, errSize, "not a save (magic)");
    }
    if (GetLE32(b + 4) != version) {
        if (GetLE32(b + 4) == DDR_VERSION || GetLE32(b + 4) == PC_DDR_VERSION) {
            return Fail(err, errSize, "a Doomsday Rescue save: only Hot Wheels progress is imported");
        }
        return Fail(err, errSize, "size 0x%X, not 0x%X", (unsigned)GetLE32(b + 4), (unsigned)version);
    }
    if (len < 12 + (size_t)version) {
        return Fail(err, errSize, "cut short: %u bytes of %u", (unsigned)len, (unsigned)(12 + version));
    }
    if (ByteSum(b + 12, version) != GetLE32(b + 8)) {
        return Fail(err, errSize, "fails its checksum (damaged)");
    }
    return 1;
}

static int Finish(PortImportSource *src, const u8 *gcLayout, int order, char *err, size_t errSize) {
    const PortSaveKind *kind = Port_SaveKindByName("hwvx");

    /* (callers have HW_SIZE bytes at gcLayout) */
    src->saveFile = SDL_malloc(HW_SIZE);
    if (src->saveFile == NULL) {
        return Fail(err, errSize, "out of memory");
    }
    if (!Port_SaveFromBinary(gcLayout, HW_SIZE, order, kind, src->saveFile, err, errSize)) {
        SDL_free(src->saveFile);
        src->saveFile = NULL;
        return 0;
    }
    return 1;
}

/* The GameCube's SaveFile in `order` (the card file's +0x840). */
static int ReadGameCube(const u8 *b, size_t len, int order, PortImportSource *src, char *err, size_t errSize) {
    u32 magic, version;

    if (len < 12) {
        return Fail(err, errSize, "no save in it");
    }
    magic = order == PORT_SAVE_ORDER_BIG ? GetBE32(b) : GetLE32(b);
    version = order == PORT_SAVE_ORDER_BIG ? GetBE32(b + 4) : GetLE32(b + 4);
    if (magic == SAVE_FILE_MAGIC && version == DDR_VERSION) {
        return Fail(err, errSize, "a Doomsday Rescue save: only Hot Wheels progress is imported");
    }
    if (len < HW_SIZE) {
        return Fail(err, errSize, "cut short: %u bytes of %u", (unsigned)len, (unsigned)HW_SIZE);
    }
    src->release = PORT_RELEASE_GC;
    return Finish(src, b, order, err, errSize);
}

/* The PC's: settings 0x38 bytes longer, moved into the GameCube's layout. */
static int ReadPC(const u8 *b, size_t len, PortImportSource *src, char *err, size_t errSize) {
    u8 *gc;
    int p, ok;

    if (!CheckLittle(b, len, PC_VERSION, err, errSize)) {
        return 0;
    }
    gc = SDL_calloc(1, HW_SIZE);
    if (gc == NULL) {
        return Fail(err, errSize, "out of memory");
    }
    SDL_memcpy(gc, b, 12);
    PutLE32(gc + 4, HW_VERSION);
    SDL_memcpy(gc + 12, b + 12, 8);    /* difficulty, audio */
    for (p = 0; p < 4; p++) {
        const u8 *pc = b + 12 + 8 + p * 0x20;
        u8 *to = gc + 12 + 8 + p * 0x12;

        /* vibration (code variable 0xD1), scheme (0xCE), the MCP binding's
         * 16 bytes: the GameCube's 0x12; then the PC's own */
        SDL_memcpy(to, pc, 0x12);
        SDL_memcpy(src->pc[p].buttons, pc + 0x12, sizeof(src->pc[p].buttons));
        src->pc[p].pad = pc[0x1F];
    }
    SDL_memcpy(gc + 12 + 0x50, b + 12 + 0x88, 0x100);                  /* slot entries */
    SDL_memcpy(gc + sizeof(SaveFile), b + 12 + PC_SETTINGS, HW_DATA * SAVE_FILE_SLOTS);
    PutLE32(gc + 8, ByteSum(gc + 12, HW_VERSION));
    src->release = PORT_RELEASE_PC;
    ok = Finish(src, gc, PORT_SAVE_ORDER_LITTLE, err, errSize);
    SDL_free(gc);
    return ok;
}

/* The PS2's: settings packed (a u8 first), moved into the GameCube's layout. */
static int ReadPS2(const u8 *b, size_t len, PortImportSource *src, char *err, size_t errSize) {
    u8 *gc;
    int ok;

    if (!CheckLittle(b, len, HW_VERSION, err, errSize)) {
        return 0;
    }
    gc = SDL_calloc(1, HW_SIZE);
    if (gc == NULL) {
        return Fail(err, errSize, "out of memory");
    }
    SDL_memcpy(gc, b, 12);
    src->ps2Settings0 = b[12];                           /* not the GameCube's difficulty: left 0 */
    SDL_memcpy(gc + 12 + 4, b + 12 + 1, 4);              /* audio */
    SDL_memcpy(gc + 12 + 8, b + 12 + 5, 4 * 0x12);       /* controllers */
    SDL_memcpy(gc + 12 + 0x50, b + 12 + 0x50, 0x100);    /* slot entries */
    SDL_memcpy(gc + sizeof(SaveFile), b + sizeof(SaveFile), HW_DATA * SAVE_FILE_SLOTS);
    PutLE32(gc + 8, ByteSum(gc + 12, HW_VERSION));
    src->release = PORT_RELEASE_PS2;
    ok = Finish(src, gc, PORT_SAVE_ORDER_LITTLE, err, errSize);
    SDL_free(gc);
    return ok;
}

/* ---- GameCube memory cards --------------------------------------------- */

/* A Dolphin .gci's directory entry, or a card directory's: GHW? (any
 * region) by maker 78, file "hwvx". */
static int IsHotWheelsEntry(const u8 *e) {
    return SDL_memcmp(e, "GHW", 3) == 0 && SDL_memcmp(e + 4, "78", 2) == 0 && SDL_strncmp((const char *)e + 8, "hwvx", 32) == 0;
}

static int IsDDRescueEntry(const u8 *e) {
    return SDL_memcmp(e, "GHW", 3) == 0 && SDL_memcmp(e + 4, "78", 2) == 0 && SDL_strncmp((const char *)e + 8, "hwvx", 4) != 0;
}

/* A card file's blocks: the SaveFile at +0x840, big-endian. */
static int ReadCardFileData(const u8 *data, size_t len, PortImportSource *src, char *err, size_t errSize) {
    if (len < CARD_DATA + HW_SIZE) {
        return Fail(err, errSize, "the card file is too short for a save");
    }
    return ReadGameCube(data + CARD_DATA, len - CARD_DATA, PORT_SAVE_ORDER_BIG, src, err, errSize);
}

static int ReadGci(const u8 *b, size_t len, PortImportSource *src, char *err, size_t errSize) {
    u32 blocks = GetBE16(b + 0x38);

    if (!IsHotWheelsEntry(b)) {
        return Fail(err, errSize, "a GameCube save of another kind (%.4s%.2s \"%.32s\")", (const char *)b,
                    (const char *)b + 4, (const char *)b + 8);
    }
    if (blocks == 0 || 0x40 + (size_t)blocks * GC_BLOCK != len) {
        return Fail(err, errSize, "a .gci of %u blocks, but %u bytes", (unsigned)blocks, (unsigned)len);
    }
    SDL_snprintf(src->where, sizeof(src->where), "%.4s%.2s %s", (const char *)b, (const char *)b + 4, "hwvx");
    return ReadCardFileData(b + 0x40, len - 0x40, src, err, errSize);
}

/* The GameCube's block checksums: over big-endian u16s, the sum and the sum
 * of their complements, 0xFFFF read as 0 (as Dolphin and the SDK do). */
static void GcChecksums(const u8 *p, u32 bytes, u16 *sum, u16 *inv) {
    u32 i;
    u16 a = 0, c = 0;

    for (i = 0; i + 1 < bytes; i += 2) {
        u16 w = GetBE16(p + i);
        a = (u16)(a + w);
        c = (u16)(c + (u16)~w);
    }
    *sum = a == 0xFFFF ? 0 : a;
    *inv = c == 0xFFFF ? 0 : c;
}

/* The good one of a block and its backup, the newer if both are: a
 * directory (counter 0x1FFA, checksums 0x1FFC over 0..0x1FFC) or a block map
 * (checksums 0 over 4..0x2000, counter 4). */
static const u8 *GcPick(const u8 *a, const u8 *b, int map) {
    const u8 *pick = NULL;
    int k;

    for (k = 0; k < 2; k++) {
        const u8 *blk = k == 0 ? a : b;
        u16 s, i;
        int ok;

        if (map) {
            GcChecksums(blk + 4, GC_BLOCK - 4, &s, &i);
            ok = s == GetBE16(blk) && i == GetBE16(blk + 2);
        } else {
            GcChecksums(blk, 0x1FFC, &s, &i);
            ok = s == GetBE16(blk + 0x1FFC) && i == GetBE16(blk + 0x1FFE);
        }
        if (ok && (pick == NULL || (s16)(GetBE16(blk + (map ? 4 : 0x1FFA)) - GetBE16(pick + (map ? 4 : 0x1FFA))) > 0)) {
            pick = blk;
        }
    }
    return pick;
}

/* A raw card image: block 0 the header, 1-2 the directory and its backup
 * (127 entries of 0x40, as a .gci's), 3-4 the block map and its backup (u16
 * next block for blocks 5.., 0xFFFF the last). */
static int ReadGcCard(const u8 *b, size_t len, PortImportSource *src, char *err, size_t errSize) {
    u32 nblocks = (u32)(len / GC_BLOCK), first, count, k, blk;
    const u8 *dir = GcPick(b + GC_BLOCK, b + 2 * GC_BLOCK, 0), *map = GcPick(b + 3 * GC_BLOCK, b + 4 * GC_BLOCK, 1);
    const u8 *e = NULL;
    int i, ddr = 0, ok;
    u8 *data;

    if (dir == NULL) {
        return Fail(err, errSize, "not a GameCube memory card image (no directory passes its checksum)");
    }
    if (map == NULL) {
        return Fail(err, errSize, "the card's block map is damaged (neither copy passes its checksum)");
    }
    for (i = 0; i < 127; i++) {
        const u8 *d = dir + i * 0x40;
        if (IsHotWheelsEntry(d)) {
            e = d;
            break;
        }
        ddr |= IsDDRescueEntry(d);
    }
    if (e == NULL) {
        return Fail(err, errSize, ddr ? "only a Doomsday Rescue save on this card: only Hot Wheels progress is imported"
                                      : "no Hot Wheels: Velocity X save on this card");
    }
    first = GetBE16(e + 0x36);
    count = GetBE16(e + 0x38);
    if (count == 0 || count > nblocks - 5) {
        return Fail(err, errSize, "the save's directory entry is damaged (%u blocks)", (unsigned)count);
    }
    data = SDL_malloc((size_t)count * GC_BLOCK);
    if (data == NULL) {
        return Fail(err, errSize, "out of memory");
    }
    for (k = 0, blk = first; k < count; k++) {
        if (blk < 5 || blk >= nblocks || blk - 5 >= 0xFFB) {
            SDL_free(data);
            return Fail(err, errSize, "the save's block chain is damaged (block %u)", (unsigned)blk);
        }
        SDL_memcpy(data + (size_t)k * GC_BLOCK, b + (size_t)blk * GC_BLOCK, GC_BLOCK);
        blk = GetBE16(map + 0xA + (blk - 5) * 2);
    }
    SDL_snprintf(src->where, sizeof(src->where), "%.4s%.2s hwvx", (const char *)e, (const char *)e + 4);
    ok = ReadCardFileData(data, (size_t)count * GC_BLOCK, src, err, errSize);
    SDL_free(data);
    return ok;
}

/* ---- PS2 memory cards (docs/ps2-version/scripts/ps2_mcd.py) ------------- */

/* The card file system: a superblock in page 0 (page size, pages per
 * cluster, the allocatable area's first cluster, the root directory's
 * cluster, the indirect FAT cluster list), a FAT of u32 entries (bit 31 in
 * use, low 31 bits the next cluster, 0x7FFFFFFF the last) through two levels
 * of indirection, and 512-byte directory entries (u16 mode, u32 length at 4,
 * u32 first cluster at 0x10, name at 0x40). Data clusters count from the
 * allocatable area; FAT clusters are absolute. Raw images keep 16 ECC bytes
 * after each 512-byte page, which are skipped. */
typedef struct Ps2Card {
    const u8 *d;
    size_t len;
    u32 pageLen, ppc, rawPage, clusterLen, clusters, allocOffset, root;
    u32 *fat;
    u32 fatLen;
} Ps2Card;

#define PS2_MAGIC "Sony PS2 Memory Card Format "
#define PS2_END 0x7FFFFFFFu

/* Cluster n (absolute) into out; 0 if it's past the image. */
static int Ps2Cluster(const Ps2Card *c, u32 n, u8 *out) {
    u32 i;

    for (i = 0; i < c->ppc; i++) {
        size_t o = ((size_t)n * c->ppc + i) * c->rawPage;
        if (o + c->pageLen > c->len) {
            return 0;
        }
        SDL_memcpy(out + i * c->pageLen, c->d + o, c->pageLen);
    }
    return 1;
}

static int Ps2Open(Ps2Card *c, const u8 *b, size_t len, char *err, size_t errSize) {
    u32 per, i, j;
    u8 *buf, *buf2;

    SDL_zerop(c);
    if (len < 512) {
        /* the superblock (with the indirect FAT cluster list at 0x50..0xCF) is page 0 */
        return Fail(err, errSize, "the PS2 card is cut short (%u bytes)", (unsigned)len);
    }
    c->d = b;
    c->len = len;
    c->pageLen = GetLE16(b + 0x28);
    c->ppc = GetLE16(b + 0x2A);
    c->clusters = GetLE32(b + 0x30);
    c->allocOffset = GetLE32(b + 0x34);
    c->root = GetLE32(b + 0x3C);
    if ((c->pageLen != 512 && c->pageLen != 1024) || c->ppc == 0 || c->ppc > 16 || c->clusters == 0 ||
        c->clusters > 0x100000) {
        return Fail(err, errSize, "the PS2 card's superblock is damaged");
    }
    c->clusterLen = c->pageLen * c->ppc;
    c->rawPage = c->pageLen;
    if (len % (c->pageLen + 16) == 0 && len / (c->pageLen + 16) * c->pageLen == (size_t)c->clusters * c->ppc * c->pageLen) {
        c->rawPage = c->pageLen + 16;
    }
    per = c->clusterLen / 4;
    buf = SDL_malloc(c->clusterLen);
    buf2 = SDL_malloc(c->clusterLen);
    c->fat = SDL_malloc((size_t)c->clusters * 4 + (size_t)per * 4);
    if (buf == NULL || buf2 == NULL || c->fat == NULL) {
        SDL_free(buf);
        SDL_free(buf2);
        SDL_free(c->fat);
        c->fat = NULL;
        return Fail(err, errSize, "out of memory");
    }
    for (i = 0; i < 32; i++) {
        u32 ifc = GetLE32(b + 0x50 + i * 4);
        if (ifc == 0) {
            continue;
        }
        if (!Ps2Cluster(c, ifc, buf)) {
            break;
        }
        for (j = 0; j < per; j++) {
            u32 fc = GetLE32(buf + j * 4), k;
            if (fc == 0 || fc == 0xFFFFFFFF) {
                continue;
            }
            if (c->fatLen + per > c->clusters + per || !Ps2Cluster(c, fc, buf2)) {
                break;
            }
            for (k = 0; k < per; k++) {
                c->fat[c->fatLen++] = GetLE32(buf2 + k * 4);
            }
        }
    }
    SDL_free(buf);
    SDL_free(buf2);
    if (c->fatLen == 0) {
        SDL_free(c->fat);
        c->fat = NULL;
        return Fail(err, errSize, "the PS2 card's FAT is damaged");
    }
    return 1;
}

/* `length` bytes of the chain from data cluster `first`, or NULL. */
static u8 *Ps2Read(const Ps2Card *c, u32 first, u32 length) {
    u32 n = (length + c->clusterLen - 1) / c->clusterLen, k, cl = first;
    u8 *out;

    if (length == 0 || length > MAX_FILE || n > c->fatLen) {
        return NULL;
    }
    out = SDL_malloc((size_t)n * c->clusterLen);
    if (out == NULL) {
        return NULL;
    }
    for (k = 0; k < n; k++) {
        u32 e;
        if (cl >= c->fatLen || !Ps2Cluster(c, c->allocOffset + cl, out + (size_t)k * c->clusterLen)) {
            SDL_free(out);
            return NULL;
        }
        e = c->fat[cl];
        if (k + 1 < n && (!(e & 0x80000000u) || (e & PS2_END) == PS2_END)) {
            SDL_free(out);
            return NULL;   /* the chain ends before the file does */
        }
        cl = e & PS2_END;
    }
    return out;
}

/* A directory's `count` entries (the length in its parent's entry; the
 * root's in its own "." entry), or NULL. */
static u8 *Ps2Dir(const Ps2Card *c, u32 cluster, u32 *count) {
    if (*count == 0) {
        u8 *one = Ps2Read(c, cluster, 512);
        if (one == NULL) {
            return NULL;
        }
        *count = GetLE32(one + 4);
        SDL_free(one);
    }
    if (*count < 2 || *count > 4096) {
        return NULL;
    }
    return Ps2Read(c, cluster, *count * 512);
}

/* The save in a directory entry `e` that names a file of the dir `dirName`. */
static int Ps2IsSaveEntry(const u8 *e, const char *dirName) {
    return (GetLE16(e) & 0x8010) == 0x8010 && SDL_strncmp((const char *)e + 0x40, dirName, 32) == 0;
}

static int ReadPs2Card(const u8 *b, size_t len, PortImportSource *src, char *err, size_t errSize) {
    Ps2Card c;
    u8 *root, *sub = NULL, *file = NULL;
    u32 n, m, i, j, fileLen = 0;
    int pass, ok = 0;
    char found[33] = { 0 }, why[160] = { 0 };

    if (!Ps2Open(&c, b, len, err, errSize)) {
        return 0;
    }
    n = 0;
    root = Ps2Dir(&c, c.root, &n);
    if (root == NULL) {
        SDL_free(c.fat);
        return Fail(err, errSize, "the PS2 card's root directory is damaged");
    }
    /* BASLUS-20412 (the US release) first, then any folder holding a file
     * of its own name that reads as a save (other regions) */
    for (pass = 0; pass < 2 && !ok; pass++) {
        for (i = 0; i < n && !ok; i++) {
            const u8 *e = root + i * 512;
            char name[33];

            SDL_memcpy(name, e + 0x40, 32);
            name[32] = 0;
            if ((GetLE16(e) & 0x8020) != 0x8020 || name[0] == '.' ||
                (pass == 0) != (SDL_strcmp(name, "BASLUS-20412") == 0)) {
                continue;
            }
            m = GetLE32(e + 4);
            sub = Ps2Dir(&c, GetLE32(e + 0x10), &m);
            for (j = 0; sub != NULL && j < m; j++) {
                const u8 *f = sub + j * 512;
                if (Ps2IsSaveEntry(f, name)) {
                    fileLen = GetLE32(f + 4);
                    file = Ps2Read(&c, GetLE32(f + 0x10), fileLen);
                    if (file != NULL) {
                        char e2[160];
                        if (ReadPS2(file, fileLen, src, e2, sizeof(e2))) {
                            ok = 1;
                            SDL_strlcpy(found, name, sizeof(found));
                        } else if (why[0] == 0) {
                            SDL_snprintf(why, sizeof(why), "/%s/%s: %s", name, name, e2);
                        }
                    } else if (why[0] == 0) {
                        SDL_snprintf(why, sizeof(why), "/%s/%s can't be read: the card is damaged", name, name);
                    }
                    SDL_free(file);
                    file = NULL;
                    break;
                }
            }
            SDL_free(sub);
            sub = NULL;
        }
    }
    SDL_free(root);
    SDL_free(c.fat);
    if (!ok) {
        return Fail(err, errSize, "%s", why[0] ? why : "no Hot Wheels: Velocity X save on this card (BASLUS-20412)");
    }
    SDL_snprintf(src->where, sizeof(src->where), "/%s/%s", found, found);
    return 1;
}

/* A .psu (uLaunchELF's export of one save folder): the folder's 512-byte
 * directory entry, "." and "..", then each file's entry followed by its
 * bytes, padded to 1 KB. */
static int ReadPsu(const u8 *b, size_t len, PortImportSource *src, char *err, size_t errSize) {
    char dir[33];
    size_t off = 512;

    SDL_memcpy(dir, b + 0x40, 32);
    dir[32] = 0;
    while (off + 512 <= len) {
        const u8 *e = b + off;
        u16 mode = GetLE16(e);
        u32 flen = GetLE32(e + 4);

        off += 512;
        if ((mode & 0x8020) == 0x8020) {
            continue;   /* "." and ".." */
        }
        if ((mode & 0x8010) != 0x8010) {
            return Fail(err, errSize, "a damaged .psu (an entry of mode 0x%04X)", mode);
        }
        if (flen > len - off) {
            return Fail(err, errSize, "a damaged .psu (\"%.32s\" is cut short)", (const char *)e + 0x40);
        }
        if (Ps2IsSaveEntry(e, dir)) {
            SDL_snprintf(src->where, sizeof(src->where), "/%s/%s", dir, dir);
            return ReadPS2(b + off, flen, src, err, errSize);
        }
        off += ((size_t)flen + 1023) & ~(size_t)1023;
    }
    return Fail(err, errSize, "no save named %s in this .psu", dir);
}

static int LooksLikePsu(const u8 *b, size_t len) {
    return len >= 3 * 512 && (GetLE16(b) & 0x8020) == 0x8020 && b[0x40] != 0 && b[0x40] != '.' &&
           SDL_strncmp((const char *)b + 0x240, ".", 32) == 0;
}

/* ---- which file ---------------------------------------------------------- */

int Port_SaveImportRead(const void *bytes, size_t len, const char *path, PortImportSource *src,
                        char *err, size_t errSize) {
    const u8 *b = bytes;
    int ok;

    SDL_zerop(src);
    SDL_strlcpy(src->path, path != NULL ? path : "", sizeof(src->path));
    err[0] = 0;
    if (len >= 0x40 && SDL_memcmp(b, PS2_MAGIC, SDL_strlen(PS2_MAGIC)) == 0) {
        src->container = PORT_CONTAINER_PS2_CARD;
        ok = ReadPs2Card(b, len, src, err, errSize);
    } else if (len >= 12 && SDL_memcmp(b, "Ps2PowerSave", 12) == 0) {
        return Fail(err, errSize, "an Action Replay MAX save (.max): its compression isn't supported; "
                                  "export the save as .psu, or import the card image");
    } else if (LooksLikePsu(b, len)) {
        src->container = PORT_CONTAINER_PSU;
        ok = ReadPsu(b, len, src, err, errSize);
    } else if (len >= 0x40 && SDL_memcmp(b, "GHW", 3) == 0 && SDL_memcmp(b + 4, "78", 2) == 0) {
        src->container = PORT_CONTAINER_GCI;
        ok = ReadGci(b, len, src, err, errSize);
    } else if (len >= 12 && GetLE32(b) == SAVE_FILE_MAGIC && (GetLE32(b + 4) == PC_VERSION || GetLE32(b + 4) == PC_DDR_VERSION)) {
        src->container = PORT_CONTAINER_PC_FILE;
        SDL_strlcpy(src->where, "velocityX", sizeof(src->where));
        ok = ReadPC(b, len, src, err, errSize);
    } else if (len >= 12 && GetLE32(b) == SAVE_FILE_MAGIC && (GetLE32(b + 4) == HW_VERSION || GetLE32(b + 4) == DDR_VERSION)) {
        src->container = PORT_CONTAINER_PS2_FILE;
        SDL_strlcpy(src->where, "BASLUS-20412", sizeof(src->where));
        ok = ReadPS2(b, len, src, err, errSize);
    } else if (len >= CARD_DATA + 12 && len <= GC_BLOCK * 4 &&
               (GetLE32(b + CARD_DATA) == SAVE_FILE_MAGIC || GetBE32(b + CARD_DATA) == SAVE_FILE_MAGIC)) {
        /* the port's card file, in the order of the machine that wrote it */
        src->container = PORT_CONTAINER_PORT_SAV;
        SDL_strlcpy(src->where, "hwvx", sizeof(src->where));
        ok = ReadGameCube(b + CARD_DATA, len - CARD_DATA,
                          GetLE32(b + CARD_DATA) == SAVE_FILE_MAGIC ? PORT_SAVE_ORDER_LITTLE : PORT_SAVE_ORDER_BIG, src,
                          err, errSize);
    } else if (len % GC_BLOCK == 0 && len >= 5 * GC_BLOCK) {
        src->container = PORT_CONTAINER_GC_CARD;
        ok = ReadGcCard(b, len, src, err, errSize);
    } else {
        return Fail(err, errSize, "not a save or memory card this knows (GameCube .gci or card image, the PC's "
                                  "velocityX, a PS2 card image or .psu)");
    }
    if (!ok) {
        Port_SaveImportClose(src);
    }
    return ok;
}

int Port_SaveImportOpen(const char *path, PortImportSource *src, char *err, size_t errSize) {
    SDL_PathInfo info;
    void *bytes;
    size_t len = 0;
    int ok;

    SDL_zerop(src);
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return Fail(err, errSize, "not a file");
    }
    if (info.size > MAX_FILE) {
        return Fail(err, errSize, "too big for a save or memory card (%llu bytes)", (unsigned long long)info.size);
    }
    bytes = SDL_LoadFile(path, &len);
    if (bytes == NULL) {
        return Fail(err, errSize, "can't read it: %s", SDL_GetError());
    }
    ok = Port_SaveImportRead(bytes, len, path, src, err, errSize);
    SDL_free(bytes);
    return ok;
}

void Port_SaveImportClose(PortImportSource *src) {
    SDL_free(src->saveFile);
    src->saveFile = NULL;
}

int Port_SaveImportSlotName(const PortImportSource *src, int slot, char *name, size_t size) {
    const SaveFile *f = (const SaveFile *)src->saveFile;

    name[0] = 0;
    if (f == NULL || slot < 0 || slot >= SAVE_FILE_SLOTS || !f->settings.slots[slot].valid) {
        return 0;
    }
    SDL_snprintf(name, size, "%.11s", f->settings.slots[slot].name);
    return 1;
}

/* ---- the merge ----------------------------------------------------------- */

static HotWheelsGameData *SlotData(u8 *file, int slot) {
    return (HotWheelsGameData *)SAVE_FILE_GAME_DATA(file, HW_DATA, slot);
}

/* How far on a save is: its unlocks. */
static int Progress(const HotWheelsGameData *g) {
    int i, n = 0;

    for (i = 0; i < (int)sizeof(g->cars); i++) n += g->cars[i] != 0;
    for (i = 0; i < (int)sizeof(g->tracks); i++) n += g->tracks[i] != 0;
    for (i = 0; i < (int)sizeof(g->items); i++) n += g->items[i] != 0;
    for (i = 0; i < (int)sizeof(g->extras); i++) n += g->extras[i] != 0;
    return n;
}

static int Later(const SaveSlotInfo *a, const SaveSlotInfo *b) {
    long long x = (((a->year * 13LL + a->month) * 32 + a->day) * 24 + a->hour) * 60 + a->minute;
    long long y = (((b->year * 13LL + b->month) * 32 + b->day) * 24 + b->hour) * 60 + b->minute;
    return x > y;
}

/* The higher of each pair; bits of the ones that became unlocked, and the
 * count of those only raised. */
static u64 Union(s8 *to, const s8 *from, int n, int skipBelow, int *raised) {
    u64 added = 0;
    int i;

    for (i = skipBelow; i < n; i++) {
        if (from[i] > to[i]) {
            if (to[i] == 0) {
                added |= 1ull << i;
            } else {
                (*raised)++;
            }
            to[i] = from[i];
        }
    }
    return added;
}

static int Bits(u64 v) {
    int n = 0;
    for (; v != 0; v &= v - 1) n++;
    return n;
}

static void MergeSlot(u8 *target, const PortImportSource *src, int si, int from, const PortImportOptions *opt,
                      PortImportSlot *out) {
    const SaveFile *sf = (const SaveFile *)src->saveFile;
    SaveFile *tf = (SaveFile *)target;
    const SaveSlotInfo *info = &sf->settings.slots[from];
    const HotWheelsGameData *g = SlotData(src->saveFile, from);
    HotWheelsGameData *t;
    int into = -1, i, defaults = src->release != PORT_RELEASE_GC;

    SDL_zerop(out);
    out->source = si;
    out->from = from;
    SDL_memcpy(out->name, info->name, sizeof(info->name));
    for (i = 0; i < SAVE_FILE_SLOTS && into < 0; i++) {
        if (tf->settings.slots[i].valid && SDL_strncmp(tf->settings.slots[i].name, info->name, sizeof(info->name)) == 0) {
            into = i;
        }
    }
    if (into < 0) {
        for (i = 0; i < SAVE_FILE_SLOTS && into < 0; i++) {
            if (!tf->settings.slots[i].valid) {
                into = i;
                out->isNew = 1;
            }
        }
    }
    out->into = into;
    if (into < 0) {
        return;   /* eight saves already, none of this name */
    }
    t = SlotData(target, into);
    if (out->isNew) {
        /* a new save: the source's, less what the merge decides below */
        SDL_memset(t, 0, sizeof(*t));
        tf->settings.slots[into] = *info;
        tf->settings.slots[into].valid = 1;
        t->vehicle = g->vehicle;
        t->item = g->item;
    } else if (Later(info, &tf->settings.slots[into])) {
        tf->settings.slots[into].year = info->year;
        tf->settings.slots[into].month = info->month;
        tf->settings.slots[into].day = info->day;
        tf->settings.slots[into].hour = info->hour;
        tf->settings.slots[into].minute = info->minute;
    }
    SDL_memcpy(out->intoName, tf->settings.slots[into].name, sizeof(out->intoName) - 1);
    if (out->isNew) {
        SDL_memcpy(t->miscValues, g->miscValues, sizeof(g->miscValues));
    } else if (Progress(g) > Progress(t) && SDL_memcmp(t->miscValues, g->miscValues, sizeof(g->miscValues)) != 0) {
        SDL_memcpy(t->miscValues, g->miscValues, sizeof(g->miscValues));   /* the set of the save further on */
        out->scriptValues = 1;
    }
    /* cars 0-2 of a PC or PS2 save: that release's defaults, offered */
    if (defaults) {
        for (i = 0; i < 3; i++) {
            if (g->cars[i] != 0 && t->cars[i] == 0) {
                out->defaultCars |= 1ull << i;
            }
        }
    }
    out->added[PORT_SAVE_IDS_CARS] = Union(t->cars, g->cars, sizeof(g->cars), defaults && !opt->defaultCars ? 3 : 0, &out->raised);
    out->added[PORT_SAVE_IDS_TRACKS] = Union(t->tracks, g->tracks, sizeof(g->tracks), 0, &out->raised);
    out->added[PORT_SAVE_IDS_ITEMS] = Union(t->items, g->items, sizeof(g->items), 0, &out->raised);
    out->cheats = (u32)Union(t->cheats, g->cheats, sizeof(g->cheats), 0, &out->raised);
    out->extras = (u32)Union(t->extras, g->extras, sizeof(g->extras), 0, &out->raised);
    out->changes = Bits(out->added[0]) + Bits(out->added[1]) + Bits(out->added[2]) + Bits(out->cheats) +
                   Bits(out->extras) + out->raised + out->scriptValues + out->isNew;
}

void Port_SaveImportMerge(u8 *target, const PortImportSource *srcs, int n, const PortImportOptions *opt,
                          PortImportPlan *plan) {
    SaveFile *tf = (SaveFile *)target;
    const PortSaveKind *kind = Port_SaveKindByName("hwvx");
    int s, k, blank = tf->magic != SAVE_FILE_MAGIC;

    plan->numSlots = 0;
    plan->changes = 0;
    plan->defaultCarsOffered = 0;
    if (blank) {
        /* no save yet: the settings come with the first file's */
        for (s = 0; s < n; s++) {
            if (srcs[s].saveFile != NULL) {
                SDL_memcpy(&tf->settings, &((const SaveFile *)srcs[s].saveFile)->settings, sizeof(tf->settings));
                SDL_memset(tf->settings.slots, 0, sizeof(tf->settings.slots));
                break;
            }
        }
    }
    for (s = 0; s < n && s < PORT_IMPORT_MAX_SOURCES; s++) {
        const SaveFile *sf = (const SaveFile *)srcs[s].saveFile;

        if (sf == NULL || (opt->onlySource >= 0 && opt->onlySource != s)) {
            continue;
        }
        for (k = 0; k < SAVE_FILE_SLOTS; k++) {
            PortImportSlot *out = &plan->slots[plan->numSlots];

            if (!sf->settings.slots[k].valid || (opt->onlySlot >= 0 && opt->onlySlot != k)) {
                continue;
            }
            MergeSlot(target, &srcs[s], s, k, opt, out);
            plan->changes += out->changes;
            plan->defaultCarsOffered |= out->defaultCars;
            plan->numSlots++;
        }
    }
    tf->magic = SAVE_FILE_MAGIC;
    tf->version = HW_VERSION;
    tf->checksum = Port_SaveChecksum(target, kind);
}

static int Merge(const PortImportSource *srcs, int n, const PortImportOptions *opt, PortImportPlan *plan, int apply,
                 char *err, size_t errSize) {
    u8 *target = SDL_calloc(1, HW_SIZE);
    char from[1100];
    int r, ok = 1;

    SDL_zerop(plan);
    if (target == NULL) {
        return Fail(err, errSize, "out of memory");
    }
    r = Port_SavesPeek("hwvx", target, HW_SIZE, plan->target, sizeof(plan->target));
    if (r < 0) {
        SDL_free(target);
        return Fail(err, errSize, "%s: nothing is imported into it", plan->target);
    }
    if (apply && r > 0 && !Port_SavesRead("hwvx", target, HW_SIZE)) {
        /* (the read imports the memory card file the peek saw, as on any start) */
        SDL_free(target);
        return Fail(err, errSize, "the save can't be read: nothing is imported into it");
    }
    Port_SaveImportMerge(target, srcs, n, opt, plan);
    if (apply && plan->changes > 0) {
        const char *base = srcs[0].path, *slash = SDL_strrchr(base, '/');
        if (slash == NULL) {
            slash = SDL_strrchr(base, '\\');
        }
        SDL_snprintf(from, sizeof(from), "%s (%s)", slash != NULL ? slash + 1 : base, Port_SaveReleaseName(srcs[0].release));
        ok = Port_SavesWriteImported("hwvx", target, HW_SIZE, from);
        if (!ok) {
            Fail(err, errSize, "the save couldn't be written (the log says why)");
        } else {
            int i;
            for (i = 0; i < n; i++) {
                Port_Log("saves: imported %s, %s%s%s", srcs[i].path, Port_SaveContainerName(srcs[i].container),
                         srcs[i].where[0] ? ": " : "", srcs[i].where);
            }
        }
    }
    SDL_free(target);
    return ok;
}

int Port_SaveImportPreview(const PortImportSource *srcs, int n, const PortImportOptions *opt, PortImportPlan *plan,
                           char *err, size_t errSize) {
    return Merge(srcs, n, opt, plan, 0, err, errSize);
}

int Port_SaveImportApply(const PortImportSource *srcs, int n, const PortImportOptions *opt, PortImportPlan *plan,
                         char *err, size_t errSize) {
    return Merge(srcs, n, opt, plan, 1, err, errSize);
}

/* "3 cars (0, 1, 2)" appended to buf. */
static void List(char *buf, size_t size, const char *what, u64 bits) {
    char part[256];
    int i, first = 1, n = Bits(bits);

    if (n == 0) {
        return;
    }
    SDL_snprintf(part, sizeof(part), "%s%d %s%s (", buf[0] ? ", " : "", n, what, n == 1 ? "" : "s");
    SDL_strlcat(buf, part, size);
    for (i = 0; i < 64; i++) {
        if (bits & (1ull << i)) {
            SDL_snprintf(part, sizeof(part), "%s%d", first ? "" : " ", i);
            SDL_strlcat(buf, part, size);
            first = 0;
        }
    }
    SDL_strlcat(buf, ")", size);
}

void Port_SaveImportDescribe(const PortImportPlan *plan, int i, const PortImportOptions *opt, char *buf, size_t size) {
    const PortImportSlot *s = &plan->slots[i];
    char what[512] = { 0 }, part[192];

    if (s->into < 0) {
        SDL_snprintf(buf, size, "\"%s\": no room (8 saves already)", s->name);
        return;
    }
    List(what, sizeof(what), "car", s->added[PORT_SAVE_IDS_CARS]);
    List(what, sizeof(what), "track", s->added[PORT_SAVE_IDS_TRACKS]);
    List(what, sizeof(what), "item", s->added[PORT_SAVE_IDS_ITEMS]);
    List(what, sizeof(what), "extra", s->extras);
    List(what, sizeof(what), "cheat", s->cheats);
    if (s->raised) {
        SDL_snprintf(part, sizeof(part), "%s%d raised", what[0] ? ", " : "", s->raised);
        SDL_strlcat(what, part, sizeof(what));
    }
    if (s->scriptValues) {
        SDL_strlcat(what, what[0] ? ", script values" : "script values", sizeof(what));
    }
    if (s->defaultCars && !opt->defaultCars) {
        char d[128] = { 0 };
        List(d, sizeof(d), "default car", s->defaultCars);
        SDL_snprintf(part, sizeof(part), "%s%s left out", what[0] ? "; " : "", d);
        SDL_strlcat(what, part, sizeof(what));
    }
    SDL_snprintf(buf, size, "\"%s\" -> %s%s%s: %s", s->name, s->isNew ? "new save" : "\"", s->isNew ? "" : s->intoName,
                 s->isNew ? "" : "\"", what[0] ? what : "nothing new");
}
