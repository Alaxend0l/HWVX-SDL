/* A release converted to the .xdl cache ahead of time: every pack of every
 * pack file loaded once, exactly as the game loads it, with the cache
 * (port/xdl.c) recording. docs/xdl-format/README.md §7: "the compiler is the
 * port's loader run headless". Port-only file.
 *
 * It runs in a process of its own (hwvx --convert-xdl OUT --data PATH; the
 * launcher's Convert page starts one and reads its progress), after
 * Main_Init has set the game up but before any state: the heap, the files,
 * sound and the texture manager are there, with a hidden window. Nothing of
 * the game runs beside it.
 *
 * - The pack files: every .xgc, .xpc and .xps under the data folder, by the
 *   name the game would ask for (its path there without the extension; the
 *   PS2's from XPS/ENGLISH), so dvd.c finds each as it does for the game.
 * - A file's packs: its TOC. Each one but a quadrant goes through
 *   DataPack_LoadFile under its own type, and is let go again as a pack
 *   released before its pointers were refreshed. The shared texture packs
 *   (type 8) that packs refer to load into the streaming pool as they do in
 *   the game, and stay.
 * - Quadrants (type 7; the game finds them through the world): each one
 *   through DataPack_LoadQuadPack, as a quadrant of as many geometries as
 *   its id says (WorldQuad.packId).
 *
 * The cache files are the ones the game would have written itself, pack by
 * pack, with HWVX_XDL_WRITE=1 (tools/xdl_cache_diff.py compares the two). */
#include <SDL3/SDL.h>
#include "common.h"
#include "World.h"
#include "Port_Swap.h"
#include "Port_Convert.h"
#include "xdl.h"

extern StreamingPack *g_StreamingDataPacks[11];
extern u32 g_MaxStreamingDataPacks[11];
extern void (*DestroyMusic)(void);

static int s_converting;

void Port_ConvertBegin(void) {
    s_converting = 1;
}

int Port_Converting(void) {
    return s_converting;
}

/* ---- the pack files ------------------------------------------------------- */

typedef struct TocEntry {
    u32 version, type;
    u32 id;         /* a quadrant's: its world */
    u32 quadId;     /* a quadrant's id (low 16 bits) and number of geometries */
    u32 size, offset;
} TocEntry;

typedef struct PackFile {
    char *name;         /* as the game asks for it, without ".xgc" */
    char *path;         /* on disk */
    SDL_PathInfo info;
    TocEntry *toc;
    u32 count;
} PackFile;

typedef struct Found {
    PackFile *files;
    u32 count, cap;
    const char *root;
    char rel[512];
    int depth;
} Found;

static int IsPackName(const char *name) {
    size_t n = SDL_strlen(name);

    return n > 4 && (SDL_strcasecmp(name + n - 4, ".xgc") == 0 || SDL_strcasecmp(name + n - 4, ".xpc") == 0 ||
                     SDL_strcasecmp(name + n - 4, ".xps") == 0);
}

static SDL_EnumerationResult SDLCALL Walk(void *userdata, const char *dir, const char *name) {
    Found *f = userdata;
    size_t was = SDL_strlen(f->rel);
    char *path = NULL;
    SDL_PathInfo info;

    if (name[0] == '.' || SDL_asprintf(&path, "%s%s", dir, name) < 0) {
        return SDL_ENUM_CONTINUE;
    }
    if (!SDL_GetPathInfo(path, &info) || was + SDL_strlen(name) + 2 > sizeof(f->rel)) {
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }
    SDL_strlcat(f->rel, name, sizeof(f->rel));
    if (info.type == SDL_PATHTYPE_DIRECTORY && f->depth < 6) {
        SDL_strlcat(f->rel, "/", sizeof(f->rel));
        f->depth++;
        SDL_EnumerateDirectory(path, Walk, f);
        f->depth--;
    } else if (info.type == SDL_PATHTYPE_FILE && IsPackName(name)) {
        const char *rel = f->rel;
        PackFile *pf;
        u32 i;

        if (SDL_strncasecmp(rel, "XPS/ENGLISH/", 12) == 0) {
            rel += 12;      /* the PS2 disc's packs: dvd.c looks there itself */
        }
        if (f->count == f->cap) {
            f->cap = f->cap ? f->cap * 2 : 128;
            f->files = SDL_realloc(f->files, f->cap * sizeof(PackFile));
        }
        /* one release's copy of a pack file hides another's (dvd.c DVDOpen) */
        for (i = 0; i < f->count && SDL_strncasecmp(f->files[i].name, rel, SDL_strlen(rel) - 4) != 0; i++) {
        }
        if (i == f->count || SDL_strlen(f->files[i].name) != SDL_strlen(rel) - 4) {
            pf = &f->files[f->count++];
            SDL_zerop(pf);
            pf->name = SDL_strndup(rel, SDL_strlen(rel) - 4);
        }
    }
    f->rel[was] = 0;
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static int SDLCALL ByName(const void *a, const void *b) {
    return SDL_strcasecmp(((const PackFile *)a)->name, ((const PackFile *)b)->name);
}

/* The file's TOC, and the file it is on disk; 0 if the game can't open it
 * or it isn't an archive of this version. */
static int ReadToc(PackFile *pf) {
    char path[0x80];
    DVDFileInfo fi;
    u32 head[4], *buf, size;
    s32 got;

    if (SDL_strlen(pf->name) + 5 > sizeof(path)) {
        return 0;
    }
    SDL_snprintf(path, sizeof(path), "%s.xgc", pf->name);
    if (!DVDOpen(path, &fi)) {
        return 0;
    }
    pf->path = Xdl_SourcePath(fi.xdl) != NULL ? SDL_strdup(Xdl_SourcePath(fi.xdl)) : NULL;
    got = DVDReadPrio(&fi, head, sizeof(head), 0, 2);
    PortData_TOC((s32)fi.format, head, 4);
    if (got < 0x10 || head[0] != 0x020100A0 || head[1] != 0x040100AE || head[2] != 0xB7 || head[3] > 0x10000) {
        DVDClose(&fi);
        return 0;
    }
    size = 0x10 + head[3] * (u32)sizeof(TocEntry);
    buf = SDL_calloc(1, (size + 31) & ~31u);
    got = buf != NULL ? DVDReadPrio(&fi, buf, (s32)((size + 31) & ~31u), 0, 2) : -1;
    DVDClose(&fi);
    if (got < (s32)size) {
        SDL_free(buf);
        return 0;
    }
    PortData_TOC((s32)fi.format, buf, size / 4);
    pf->count = head[3];
    pf->toc = SDL_malloc((pf->count + 1) * sizeof(TocEntry));
    SDL_memcpy(pf->toc, buf + 4, pf->count * sizeof(TocEntry));
    SDL_free(buf);
    return pf->path != NULL && SDL_GetPathInfo(pf->path, &pf->info);
}

/* ---- loading ---------------------------------------------------------------- */

/* The shared packs loaded for a pack's references are named by a pointer
 * into the pack that asked first (DataPackRef.name), which the converter
 * lets go: their names are kept here instead. */
#define NAME_SIZE 0x50

static char *s_names;

static void KeepRefNames(void) {
    StreamingPack *p = g_StreamingDataPacks[8];
    u32 i, n = g_MaxStreamingDataPacks[8];

    if (s_names == NULL) {
        s_names = Allocate(n * NAME_SIZE, 0, "");
        memset(s_names, 0, n * NAME_SIZE);
    }
    for (i = 0; p != NULL && i < n; i++) {
        char *name = GETP(p[i].name);

        if (p[i].used && name != NULL && name != s_names + i * NAME_SIZE) {
            SDL_strlcpy(s_names + i * NAME_SIZE, name, NAME_SIZE);
            SETP(p[i].name, s_names + i * NAME_SIZE);
        }
    }
}

/* The shared packs (type 8) a pack refers to, loaded before it, as the game
 * has them by the time it loads a pack (the MCP's share, car and item packs,
 * State_ProgressionManager.c): DataPack_LoadDataPack would load a missing
 * one itself, in the middle of the pack, over the header it keeps (a
 * static). The references are read from the pack's header in the file. */
static PackFile *s_files;
static u32 s_numFiles;

typedef struct PeekHeader {
    u32 words[20];
    DataPackRef refs[2];
} PeekHeader;

static void EnsureShared(const char *name, u32 id, int depth);

static void EnsureRefs(const PackFile *pf, const TocEntry *e, int depth) {
    char path[0x80];
    DVDFileInfo fi;
    PeekHeader h;
    s32 got;
    int i;

    SDL_snprintf(path, sizeof(path), "%s.xgc", pf->name);
    if (!DVDOpen(path, &fi)) {
        return;
    }
    got = DVDReadPrio(&fi, &h, sizeof(h), (s32)(0x10 + pf->count * (u32)sizeof(TocEntry) + e->offset), 2);
    DVDClose(&fi);
    if (got < (s32)sizeof(h)) {
        return;
    }
    PortData_PackHeader((s32)fi.format, (u32 *)&h, 20, 2, sizeof(DataPackRef));
    for (i = 0; i < 2; i++) {
        h.refs[i].name[sizeof(h.refs[i].name) - 1] = 0;
        if (h.refs[i].id != 0 && h.refs[i].name[0] != 0) {
            EnsureShared(h.refs[i].name, h.refs[i].id, depth);
        }
    }
}

static void EnsureShared(const char *name, u32 id, int depth) {
    static char *asked;
    u32 i, j;
    s32 slot;

    if (FileStreaming_GetPackID((char *)name, 8, id) >= 0 || depth > 4) {
        return;
    }
    /* its own references first: its file, by name */
    for (i = 0; i < s_numFiles; i++) {
        const char *base = SDL_strrchr(s_files[i].name, '/');

        if (SDL_strcasecmp(base != NULL ? base + 1 : s_files[i].name, name) != 0) {
            continue;
        }
        for (j = 0; j < s_files[i].count; j++) {
            if (s_files[i].toc[j].type == 8 && s_files[i].toc[j].id == id) {
                EnsureRefs(&s_files[i], &s_files[i].toc[j], depth + 1);
            }
        }
    }
    if (asked == NULL) {
        asked = Allocate(NAME_SIZE, 0, "");
    }
    SDL_strlcpy(asked, name, NAME_SIZE);
    slot = FileStreaming_LoadDataPack(asked, 8, id, 0);
    if (slot < 0 || FileStreaming_InitializeDataPack((u32)slot, 8, 1) == NULL) {
        Port_Fatal("convert: the shared pack %s#%u can't be loaded", name, id);
    }
    KeepRefNames();
}

/* A pack as DataPack_LoadDataPack left it, never refreshed: its sounds and
 * blocks. */
static void LetGo(DataPack *pack) {
    u32 i;

    if (GETP(pack->soundIDs) != NULL) {
        if (pack->songLoaded) {
            DestroyMusic();
        }
        for (i = 0; i < pack->numSounds; i++) {
            if (GETP(pack->soundIDs)[i] != 0) {
                DestroySFX(GETP(pack->soundIDs)[i]);
            }
        }
        Deallocate(GETP(pack->soundIDs));
        Deallocate(GETP(pack->soundData));
        PortGeometry_Release(pack);
    }
    memset(pack, 0, sizeof(DataPack));
}

static const char *ConvertFile(PackFile *pf, u32 *done, u32 total) {
    static DataPack *pack;
    static WorldQuad *quad;
    char path[0x80];
    u32 i, quads = 0;
    s32 file;

    if (pack == NULL) {
        pack = Allocate(sizeof(DataPack), 0, "");
        quad = Allocate(sizeof(WorldQuad), 0, "");
        memset(pack, 0, sizeof(DataPack));
    }
    for (i = 0; i < pf->count; i++) {
        const TocEntry *e = &pf->toc[i];

        if (e->type == 7) {
            quads++;
            continue;
        }
        if (e->type >= 11 || e->version != 0xB7) {
            return "a pack of a type or version the game doesn't load";
        }
        printf("@convert %u %u %s\n", *done, total, pf->name);
        fflush(stdout);
        EnsureRefs(pf, e, 0);
        if (!DataPack_LoadFile(pf->name, HND(pack), e->type, e->id) || GETP(pack->soundIDs) == NULL) {
            return "a pack didn't load";
        }
        KeepRefNames();
        LetGo(pack);
        ++*done;
    }
    DataPack_CloseFiles();
    if (quads == 0) {
        return NULL;
    }
    SDL_snprintf(path, sizeof(path), "%s.xgc", pf->name);
    file = File_Open(0, path, 0, NULL);
    if (file == -1) {
        return "the file didn't open";
    }
    for (i = 0; i < pf->count; i++) {
        const TocEntry *e = &pf->toc[i];
        QuadPack *q = (QuadPack *)quad;

        if (e->type != 7) {
            continue;
        }
        printf("@convert %u %u %s\n", *done, total, pf->name);
        fflush(stdout);
        memset(quad, 0, sizeof(WorldQuad));
        quad->numGeoms = e->quadId >> 16;
        quad->packId = e->quadId;
        quad->size = e->size;
        quad->fileOffset = 0x10 + pf->count * (u32)sizeof(TocEntry) + e->offset;
        DataPack_LoadQuadPack(file, q);
        KeepRefNames();
        Deallocate(GETP(q->alloc));
        PortGeometry_Release(q);
        ++*done;
    }
    File_Close(file);
    return NULL;
}

int Port_ConvertRun(const char *dataDir, const char *source, const char *outDir) {
    Found f;
    char *root = NULL, *path = NULL;
    const char *err = NULL;
    SDL_IOStream *io;
    u32 i, total = 0, done = 0, version, compiler, settings;
    size_t n = SDL_strlen(dataDir);

    SDL_zero(f);
    SDL_asprintf(&root, "%s%s", dataDir, n > 0 && (dataDir[n - 1] == '/' || dataDir[n - 1] == '\\') ? "" : "/");
    SDL_EnumerateDirectory(root, Walk, &f);
    if (f.count == 0) {
        printf("@error no pack files in %s (a disc image is not converted: extract its files)\n", dataDir);
        fflush(stdout);
        return 1;
    }
    SDL_qsort(f.files, f.count, sizeof(PackFile), ByName);
    for (i = 0; i < f.count; i++) {
        if (!ReadToc(&f.files[i])) {
            Port_Log("convert: %s is not a pack file the game reads, left out", f.files[i].name);
            f.files[i].count = 0;
        }
        total += f.files[i].count;
    }
    s_files = f.files;
    s_numFiles = f.count;
    SoundManager_Init();    /* the states' own start: packs load their sounds and songs */
    SDL_CreateDirectory(outDir);
    for (i = 0; i < f.count && err == NULL; i++) {
        err = ConvertFile(&f.files[i], &done, total);
        if (err != NULL) {
            printf("@error %s: %s\n", f.files[i].name, err);
            fflush(stdout);
            return 1;
        }
    }
    /* the manifest, last: a folder without one isn't a finished conversion */
    Xdl_Stamp(&version, &compiler, &settings);
    SDL_asprintf(&path, "%s/%s", outDir, PORT_CONVERT_MANIFEST);
    io = SDL_IOFromFile(path, "w");
    if (io == NULL) {
        printf("@error %s: %s\n", path, SDL_GetError());
        fflush(stdout);
        return 1;
    }
    SDL_IOprintf(io, "xdl=%u.%u.%u\nsource=%s\npacks=%u\n", version, compiler, settings, source, done);
    for (i = 0; i < f.count; i++) {
        if (f.files[i].count != 0) {
            SDL_IOprintf(io, "file=%" SDL_PRIu64 "\t%" SDL_PRIs64 "\t%s\n", (Uint64)f.files[i].info.size,
                         (Sint64)f.files[i].info.modify_time, f.files[i].path);
        }
    }
    if (!SDL_CloseIO(io)) {
        printf("@error %s: %s\n", path, SDL_GetError());
        fflush(stdout);
        return 1;
    }
    printf("@convert %u %u done\n", done, total);
    fflush(stdout);
    return 0;
}
