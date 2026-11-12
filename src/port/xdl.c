/* .xdl: a cache of converted packs (xdl.h). docs/xdl-format/README.md
 * (HWVX-X-FILE-EDITOR) §3 has the container, §5 the loading.
 *
 * File: a 0x40-byte header, sections (each 64-byte aligned), then the TOC
 * section and the section table, which a pack written later moves past its
 * own sections. All of it in host order: the cache is made on the player's
 * machine, for it. Per pack a PACK section (XdlPackInfo: the converted pack
 * header, its segments, the song bank), a FIXUPS section and one SEGMENT
 * section per segment.
 *
 * A segment is stored with its pointer slots holding offsets from the start
 * of the segment they point into; a fixup { slot segment, slot offset,
 * target segment } adds the target's handle as the pack loads. Slots that
 * hold 0 have none. */
#include "port_internal.h"
#include "xdl.h"
#include "Port_Mix.h"

#define XDL_VERSION 1
/* Bump when a converter changes what it leaves in memory (Port_Data.c,
 * Port_Swap.c, Port_Geometry.c, Port_PS2.c, the menus' data): every cache
 * file is then rebuilt. */
#define XDL_COMPILER 1
/* XdlHeader.source: a mixed build's pack, its base release in the low bits. */
#define XDL_SOURCE_MIXED 0x100u

enum { XDL_SEC_TOC = 1, XDL_SEC_PACK, XDL_SEC_FIXUPS, XDL_SEC_SEGMENT };

typedef struct XdlHeader {
    char magic[4];          /* "XDL1" */
    u32 version;
    u32 compiler;
    u32 source;             /* PORT_DATA_*: whose data it came from; | XDL_SOURCE_MIXED */
    u64 sourceSize;         /* the source file's size and modification time (ns): */
    s64 sourceTime;         /*   step 1 "hashes" the source by these */
    u32 numSections;
    u32 sectionTableOffset;
    u32 flags;              /* 0: no compression */
    u32 settings;           /* conversion settings it was made with (bit 0: HWVX_PS2CULL) */
    u32 reserved[4];        /* [0]: a mixed build's stamp (Port_MixStamp), else 0 */
} XdlHeader;
_Static_assert(sizeof(XdlHeader) == 0x40, "XdlHeader");

typedef struct XdlSection {
    u32 kind;               /* XDL_SEC_* */
    u32 pack;               /* TOC index of the pack it belongs to */
    u64 offset;
    u32 size;
    u32 rawSize;            /* = size (no compression yet) */
    u16 compression;
    u16 align;
    u32 seg;                /* SEGMENT: XDL_SEG_* */
} XdlSection;
_Static_assert(sizeof(XdlSection) == 0x20, "XdlSection");

typedef struct XdlTocEntry {
    u32 type, id;           /* BrainCore pack type and id */
    u32 srcOffset;          /* where the pack starts in its source file: the key */
    u32 packSection;        /* its PACK section */
} XdlTocEntry;

typedef struct XdlSegInfo {
    u32 size;               /* bytes (0: none); BANK, SFX and blocks: allocated size */
    u32 role;               /* blocks: XDL_ROLE_* */
    u32 section;
    u32 reserved;
} XdlSegInfo;

typedef struct XdlPackInfo {
    u32 type, id, srcOffset;
    u32 headerSize;
    u8 header[0x80];        /* the pack header, converted */
    XdlSegInfo seg[XDL_MAX_SEGS];
    s32 bankSeg;            /* the song bank: segment and offset, or -1 */
    u32 bankOff;
    u32 numFixups;
    u32 fixupSection;
} XdlPackInfo;

typedef struct XdlFixup {
    u32 slot;               /* offset in the slot's segment */
    u16 slotSeg;
    u16 target;             /* the segment whose handle is added */
} XdlFixup;

typedef struct XdlFile {
    char *path;
    char *source;           /* the file it is made from */
    XdlHeader want;         /* what a current cache file's header holds */
    SDL_IOStream *io;       /* reading: open, its index loaded */
    XdlSection *sections;
    u32 numSections;
    XdlTocEntry *toc;
    u32 numToc;
} XdlFile;

struct XdlPack {
    XdlFile *xf;
    XdlPackInfo info;
    XdlFixup *fix;
    u8 *applied;
    u8 *at[XDL_MAX_SEGS];
};

/* ---- settings --------------------------------------------------------------- */

static int EnvOn(const char *name) {
    const char *v = SDL_getenv(name);

    return v != NULL && *v && SDL_atoi(v) != 0;
}

/* -1: as the environment says (HWVX_XDL, HWVX_XDL_WRITE, HWVX_XDL_DIR) */
static int s_reading = -1, s_writing = -1;
static char *s_dir;

int Xdl_Reading(void) {
    if (s_reading < 0) {
        /* a mixed build (mix.c) is played from its cache, and fills it */
        s_reading = EnvOn("HWVX_XDL") || Port_MixActive();
    }
    return s_reading;
}

int Xdl_Writing(void) {
    if (s_writing < 0) {
        s_writing = EnvOn("HWVX_XDL_WRITE") || Port_MixActive();
    }
    return s_writing;
}

static const char *CacheDir(void) {
    if (s_dir == NULL) {
        const char *env = SDL_getenv("HWVX_XDL_DIR");

        if (env != NULL && *env) {
            s_dir = SDL_strdup(env);
        } else {
            char *pref = Port_PrefPath();

            SDL_asprintf(&s_dir, "%sxdl", pref != NULL ? pref : "");
            SDL_free(pref);
        }
    }
    return s_dir;
}

void Xdl_Use(const char *dir, int reading, int writing) {
    SDL_free(s_dir);
    s_dir = SDL_strdup(dir);
    s_reading = reading;
    s_writing = writing;
}

int Xdl_EnvSet(void) {
    return SDL_getenv("HWVX_XDL") != NULL || SDL_getenv("HWVX_XDL_WRITE") != NULL || SDL_getenv("HWVX_XDL_DIR") != NULL;
}

static u32 Settings(void) {
    return EnvOn("HWVX_PS2CULL") ? 1u : 0u;
}

void Xdl_Stamp(u32 *version, u32 *compiler, u32 *settings) {
    *version = XDL_VERSION;
    *compiler = XDL_COMPILER;
    *settings = Settings();
}

/* ---- the index (header, section table, TOC) --------------------------------- */

static void FreeIndex(XdlFile *xf) {
    SDL_free(xf->sections);
    SDL_free(xf->toc);
    xf->sections = NULL;
    xf->toc = NULL;
    xf->numSections = xf->numToc = 0;
}

static int ReadAt(SDL_IOStream *io, u64 off, void *dst, size_t n) {
    return SDL_SeekIO(io, (Sint64)off, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, dst, n) == n;
}

/* Load the index of an open cache file; 0 if it isn't one made from this
 * source by this version. */
static int LoadIndex(XdlFile *xf, SDL_IOStream *io) {
    XdlHeader h;
    XdlSection *toc;
    u32 i;

    FreeIndex(xf);
    if (!ReadAt(io, 0, &h, sizeof(h)) || SDL_memcmp(&h, &xf->want, offsetof(XdlHeader, numSections)) != 0 ||
        h.settings != xf->want.settings || h.reserved[0] != xf->want.reserved[0] || h.numSections == 0 || h.numSections > 0x100000) {
        return 0;
    }
    xf->sections = SDL_malloc(h.numSections * sizeof(XdlSection));
    if (xf->sections == NULL || !ReadAt(io, h.sectionTableOffset, xf->sections, h.numSections * sizeof(XdlSection))) {
        FreeIndex(xf);
        return 0;
    }
    xf->numSections = h.numSections;
    toc = &xf->sections[h.numSections - 1];   /* the TOC is the last section */
    if (toc->kind != XDL_SEC_TOC || toc->size % sizeof(XdlTocEntry) != 0) {
        FreeIndex(xf);
        return 0;
    }
    xf->numToc = toc->size / sizeof(XdlTocEntry);
    xf->toc = SDL_malloc(toc->size + 1);
    if (xf->toc == NULL || !ReadAt(io, toc->offset, xf->toc, toc->size)) {
        FreeIndex(xf);
        return 0;
    }
    for (i = 0; i < xf->numToc; i++) {
        if (xf->toc[i].packSection >= xf->numSections) {
            FreeIndex(xf);
            return 0;
        }
    }
    return 1;
}

static const XdlTocEntry *FindToc(const XdlFile *xf, u32 srcOffset) {
    u32 i;

    for (i = 0; i < xf->numToc; i++) {
        if (xf->toc[i].srcOffset == srcOffset) {
            return &xf->toc[i];
        }
    }
    return NULL;
}

void *Xdl_OpenFile(const char *name, int fmt, const char *sourcePath) {
    static const char *fmtDir[PORT_DATA_COUNT] = { "gc", "pc", "ps2" };
    SDL_PathInfo info;
    XdlFile *xf;
    const char *base, *c;
    char *lower;
    size_t n;

    if ((!Xdl_Reading() && !Xdl_Writing()) || sourcePath == NULL || fmt < 0 || fmt >= PORT_DATA_COUNT ||
        !SDL_GetPathInfo(sourcePath, &info)) {
        return NULL;
    }
    while (*name == '/') {
        name++;
    }
    n = SDL_strlen(name);
    if (n < 4 || SDL_strcasecmp(name + n - 4, ".xgc") != 0) {
        return NULL;
    }
    xf = SDL_calloc(1, sizeof(XdlFile));
    if (xf == NULL) {
        return NULL;
    }
    /* by the file's own name, in lower case: the game asks for a pack with
     * its folders and in more than one spelling, a flat folder (the PC's)
     * has none, and every pack file's name is its own (dvd.c) */
    for (base = name, c = name; *c; c++) {
        if (*c == '/' || *c == '\\') {
            base = c + 1;
        }
    }
    n -= (size_t)(base - name);
    SDL_asprintf(&xf->path, "%s/%s/%.*s.xdl", CacheDir(), Port_MixActive() ? Port_MixCacheName() : fmtDir[fmt],
                 (int)(n - 4), base);
    for (lower = xf->path != NULL ? xf->path + SDL_strlen(xf->path) - n : NULL; lower != NULL && *lower; lower++) {
        *lower = (char)SDL_tolower((unsigned char)*lower);
    }
    xf->source = SDL_strdup(sourcePath);
    SDL_memcpy(xf->want.magic, "XDL1", 4);
    xf->want.version = XDL_VERSION;
    xf->want.compiler = XDL_COMPILER;
    xf->want.source = (u32)fmt;
    if (Port_MixActive() && xf->path != NULL) {
        /* a mixed build's own folder; its packs hold other releases' assets
         * (MIXED), by the recipe and releases the stamp stands for */
        char *p;

        for (p = xf->path + SDL_strlen(CacheDir()); *p; p++) {
            *p = (char)SDL_tolower((unsigned char)*p);
        }
        xf->want.source |= XDL_SOURCE_MIXED;
        xf->want.reserved[0] = Port_MixStamp();
    }
    xf->want.sourceSize = info.size;
    xf->want.sourceTime = info.modify_time;
    xf->want.settings = Settings();
    if (Xdl_Reading()) {
        xf->io = SDL_IOFromFile(xf->path, "rb");
        if (xf->io != NULL && !LoadIndex(xf, xf->io)) {
            Port_Log("xdl: %s is out of date, loading the source", xf->path);
            SDL_CloseIO(xf->io);
            xf->io = NULL;
        } else if (xf->io != NULL) {
            Port_Log("xdl: %s", xf->path);
        }
    }
    if (xf->io == NULL && !Xdl_Writing()) {
        Xdl_CloseFile(xf);
        return NULL;
    }
    return xf;
}

void Xdl_CloseFile(void *p) {
    XdlFile *xf = p;

    if (xf == NULL) {
        return;
    }
    if (xf->io != NULL) {
        SDL_CloseIO(xf->io);
    }
    FreeIndex(xf);
    SDL_free(xf->path);
    SDL_free(xf->source);
    SDL_free(xf);
}

const char *Xdl_SourcePath(void *xf) {
    return xf != NULL ? ((XdlFile *)xf)->source : NULL;
}

/* ---- recording ---------------------------------------------------------------- */

typedef struct RecSeg {
    u8 *at;
    u32 size;
    u8 *copy;               /* the bytes, slots as offsets */
    u8 live;                /* still allocated: pointers may point into it */
} RecSeg;

/* Slots to resolve, by where they came from. */
enum { SLOT_LIST = 1, SLOT_NOTE = 2, SLOT_CROSS = 4 };

typedef struct RecSlot {
    u8 *at;
    u32 from;
} RecSlot;

typedef struct Rec {
    struct Rec *prev;
    int active;
    XdlFile *xf;
    u32 srcOffset, type, id;
    u8 header[0x80];
    u32 headerSize;
    RecSeg seg[XDL_MAX_SEGS];
    u32 numBlocks;
    RecSlot *slots;
    u32 numSlots, capSlots;
    XdlFixup *fix;
    u32 numFix, capFix;
    s32 bankSeg;
    u32 bankOff;
    u32 fallback;           /* listed slots pointing outside every segment */
    void *soundData;        /* the block DataPack.soundData holds */
    struct {
        u8 *at;
        u32 value;
    } kept[4];              /* Xdl_KeepValue */
    u32 numKept;
    const char *failed;
} Rec;

/* per thread: the loading thread records its own packs */
static _Thread_local Rec *s_rec;

static void AddSlot(Rec *r, void *at, u32 from) {
    if (r->numSlots == r->capSlots) {
        u32 cap = r->capSlots ? r->capSlots * 2 : 1024;
        RecSlot *s = SDL_realloc(r->slots, cap * sizeof(RecSlot));

        if (s == NULL) {
            r->failed = "out of memory";
            return;
        }
        r->slots = s;
        r->capSlots = cap;
    }
    r->slots[r->numSlots].at = at;
    r->slots[r->numSlots].from = from;
    r->numSlots++;
}

static void AddFixup(Rec *r, u32 slot, int slotSeg, int target) {
    if (r->numFix == r->capFix) {
        u32 cap = r->capFix ? r->capFix * 2 : 1024;
        XdlFixup *f = SDL_realloc(r->fix, cap * sizeof(XdlFixup));

        if (f == NULL) {
            r->failed = "out of memory";
            return;
        }
        r->fix = f;
        r->capFix = cap;
    }
    r->fix[r->numFix].slot = slot;
    r->fix[r->numFix].slotSeg = (u16)slotSeg;
    r->fix[r->numFix].target = (u16)target;
    r->numFix++;
}

void Xdl_BeginPack(void *xf, u32 srcOffset, u32 type, u32 id) {
    Rec *r = SDL_calloc(1, sizeof(Rec));

    if (r == NULL) {
        Port_Fatal("xdl: out of memory");
    }
    r->prev = s_rec;
    s_rec = r;
    r->xf = xf;
    r->srcOffset = srcOffset;
    r->type = type;
    r->id = id;
    r->bankSeg = -1;
    r->active = Xdl_Writing() && r->xf != NULL && FindToc(r->xf, srcOffset) == NULL;
}

static void PopRec(void) {
    Rec *r = s_rec;
    int i;

    if (r == NULL) {
        return;
    }
    s_rec = r->prev;
    for (i = 0; i < XDL_MAX_SEGS; i++) {
        SDL_free(r->seg[i].copy);
    }
    SDL_free(r->slots);
    SDL_free(r->fix);
    SDL_free(r);
}

void Xdl_AbortPack(void) {
    PopRec();
}

void Xdl_PackHeader(const void *header, u32 size) {
    Rec *r = s_rec;

    if (r == NULL || !r->active) {
        return;
    }
    if (size > sizeof(r->header)) {
        r->failed = "pack header too big";
        return;
    }
    SDL_memcpy(r->header, header, size);
    r->headerSize = size;
}

void Xdl_RawRead(int seg, const void *bytes, u32 size) {
    Rec *r = s_rec;
    RecSeg *s;

    if (r == NULL || !r->active) {
        return;
    }
    s = &r->seg[seg];
    SDL_free(s->copy);
    s->size = size;
    s->copy = SDL_malloc(size ? size : 1);
    if (s->copy == NULL) {
        r->failed = "out of memory";
        return;
    }
    SDL_memcpy(s->copy, bytes, size);
}

void Xdl_KeepValue(void *slot) {
    Rec *r = s_rec;

    if (r == NULL || !r->active) {
        return;
    }
    if (r->numKept == sizeof(r->kept) / sizeof(r->kept[0])) {
        r->failed = "too many kept values";
        return;
    }
    r->kept[r->numKept].at = slot;
    r->kept[r->numKept].value = *(u32 *)slot;
    r->numKept++;
}

void Xdl_NoteSlot(void *slot) {
    if (s_rec != NULL && s_rec->active) {
        AddSlot(s_rec, slot, SLOT_NOTE);
    }
}

void Xdl_NoteBlock(int seg, void *block, u32 size) {
    Rec *r = s_rec;

    if (r == NULL || !r->active || block == NULL) {
        return;
    }
    if (seg == XDL_SEG_BLOCK) {
        if (r->numBlocks == XDL_MAX_SEGS - XDL_SEG_BLOCK) {
            r->failed = "too many blocks";
            return;
        }
        seg += r->numBlocks++;
    }
    if (r->seg[seg].at != NULL) {
        r->failed = "a segment allocated twice";
        return;
    }
    if (HND(block) & 0x1F) {
        /* converters align inside their blocks: stored as is, they assume
         * the heap's 32-byte alignment */
        r->failed = "a block not 32-byte aligned";
        return;
    }
    r->seg[seg].at = block;
    r->seg[seg].size = size;
    r->seg[seg].live = 1;
}

/* Copy a segment's bytes as they are now. */
static void Snapshot(Rec *r, int seg) {
    RecSeg *s = &r->seg[seg];

    SDL_free(s->copy);
    s->copy = SDL_malloc(s->size ? s->size : 1);
    if (s->copy == NULL) {
        r->failed = "out of memory";
        return;
    }
    SDL_memcpy(s->copy, s->at, s->size);
}

void Xdl_BlockDone(void *block) {
    Rec *r = s_rec;
    int i;

    if (r == NULL || !r->active || block == NULL) {
        return;
    }
    for (i = 0; i < XDL_MAX_SEGS; i++) {
        if (r->seg[i].at == block && r->seg[i].live) {
            Snapshot(r, i);
            r->seg[i].live = 0;
        }
    }
}

/* The segment among `mask` holding [p, p + n), or -1; live ones only unless
 * `dead` too. */
static int SegOf(Rec *r, const u8 *p, u32 n, u32 mask, int dead) {
    int i;

    for (i = 0; i < XDL_MAX_SEGS; i++) {
        RecSeg *s = &r->seg[i];

        if ((mask & (1u << i)) && s->at != NULL && (s->live || dead) && p >= s->at &&
            p + n <= s->at + s->size) {
            return i;
        }
    }
    return -1;
}

/* The segment among `mask` handle `v` points into (or at the end of), or -1. */
static int SegOfHandle(Rec *r, u32 v, u32 mask, int dead) {
    int i;

    for (i = 0; i < XDL_MAX_SEGS; i++) {
        RecSeg *s = &r->seg[i];

        if ((mask & (1u << i)) && s->at != NULL && (s->live || dead) && v - HND(s->at) <= s->size) {
            return i;
        }
    }
    return -1;
}

static int CmpSlot(const void *a, const void *b) {
    const RecSlot *x = a, *y = b;

    return x->at < y->at ? -1 : x->at > y->at;
}

/* Resolve the recorded slots: each must lie in a segment of `slotMask`; what
 * it holds becomes an offset into the segment of `targetMask` it points
 * into, and a fixup. Listed slots pointing outside every segment were
 * relocated against `listSeg` by the loader: kept so. */
static void Resolve(Rec *r, u32 slotMask, u32 targetMask, int listSeg) {
    u32 i, j;

    if (r->numSlots == 0) {
        return;
    }
    SDL_qsort(r->slots, r->numSlots, sizeof(RecSlot), CmpSlot);
    for (i = 0, j = 0; i < r->numSlots; i++) {      /* merge duplicates */
        if (j > 0 && r->slots[j - 1].at == r->slots[i].at) {
            r->slots[j - 1].from |= r->slots[i].from;
        } else {
            r->slots[j++] = r->slots[i];
        }
    }
    r->numSlots = j;
    for (i = 0; i < r->numSlots && r->failed == NULL; i++) {
        RecSlot *s = &r->slots[i];
        int ss = SegOf(r, s->at, 4, slotMask, 0), ts;
        u32 v, off;

        if (ss < 0) {
            r->failed = "a pointer slot outside the pack";
            break;
        }
        off = (u32)(s->at - r->seg[ss].at);
        if (s->from & SLOT_CROSS) {
            if (s->from != SLOT_CROSS) {
                r->failed = "a cross-pack relocation is also a pointer slot";
                break;
            }
            SDL_memset(r->seg[ss].copy + off, 0, 4);   /* filled in again as the pack loads */
            continue;
        }
        v = *(u32 *)s->at;
        if (v == 0) {
            continue;
        }
        ts = SegOfHandle(r, v, targetMask, 0);
        if (ts < 0) {
            if (!(s->from & SLOT_LIST) || listSeg < 0) {
                r->failed = "a converter's pointer outside the pack";
                break;
            }
            ts = listSeg;
            r->fallback++;
        }
        *(u32 *)(r->seg[ss].copy + off) = v - HND(r->seg[ts].at);
        AddFixup(r, off, ss, ts);
    }
    r->numSlots = 0;
}

void Xdl_Sound(u8 *start, u32 size, const u32 *pointers, u32 numPointers, u8 *soundBase, void *songBank) {
    Rec *r = s_rec;
    u32 i;
    int bs;

    if (r == NULL || !r->active || r->failed != NULL) {
        return;
    }
    r->seg[XDL_SEG_SOUND].at = start;
    r->seg[XDL_SEG_SOUND].size = size;
    r->seg[XDL_SEG_SOUND].live = 1;
    Snapshot(r, XDL_SEG_SOUND);
    for (i = 0; i < numPointers; i++) {
        AddSlot(r, soundBase + pointers[i], SLOT_LIST);
    }
    /* so far, only the sound's converters have noted slots */
    Resolve(r, 1u << XDL_SEG_SOUND, 1u << XDL_SEG_SOUND | 1u << XDL_SEG_SFX, XDL_SEG_SOUND);
    if (songBank != NULL) {
        bs = SegOfHandle(r, HND(songBank), 1u << XDL_SEG_SOUND | 1u << XDL_SEG_BANK, 1);
        if (bs < 0) {
            r->failed = "the song bank is outside the pack";
        } else {
            r->bankSeg = bs;
            r->bankOff = (u32)((u8 *)songBank - r->seg[bs].at);
        }
    }
    r->seg[XDL_SEG_SOUND].live = 0;
}

static void WritePack(Rec *r);

void Xdl_EndPack(void *soundData, u8 *main, u32 size, u8 *base, const u32 *pointers, u32 numPointers,
                 const u32 *ptrRelocs, u32 numPtrRelocs) {
    Rec *r = s_rec;
    u32 i, mask = 1u << XDL_SEG_MAIN;

    if (r == NULL) {
        return;
    }
    if (r->active && r->failed == NULL) {
        r->seg[XDL_SEG_MAIN].at = main;
        r->seg[XDL_SEG_MAIN].size = size;
        r->seg[XDL_SEG_MAIN].live = 1;
        Snapshot(r, XDL_SEG_MAIN);
        for (i = 0; i < r->numBlocks; i++) {
            if (r->seg[XDL_SEG_BLOCK + i].live) {
                Snapshot(r, XDL_SEG_BLOCK + i);
                mask |= 1u << (XDL_SEG_BLOCK + i);
            }
        }
        for (i = 0; i < numPointers; i++) {
            AddSlot(r, base + pointers[i], SLOT_LIST);
        }
        for (i = 0; i < numPtrRelocs; i++) {
            /* { slot, pack id << 16 | index }: another pack's pointer, or
             * relocated against the blob like a pointer */
            AddSlot(r, base + ptrRelocs[i * 2], (ptrRelocs[i * 2 + 1] & 0xFFFF0000) ? SLOT_CROSS : SLOT_LIST);
        }
        Resolve(r, mask, mask, XDL_SEG_MAIN);
        for (i = 0; i < r->numKept && r->failed == NULL; i++) {
            int ss = SegOf(r, r->kept[i].at, 4, mask, 0);

            if (ss < 0) {
                r->failed = "a kept value outside the pack";
            } else {
                SDL_memcpy(r->seg[ss].copy + (r->kept[i].at - r->seg[ss].at), &r->kept[i].value, 4);
            }
        }
        r->soundData = soundData;
        if (r->failed == NULL) {
            WritePack(r);
        }
    }
    if (r->active && r->failed != NULL) {
        Port_Log("xdl: pack at 0x%x of %s not cached: %s", r->srcOffset, r->xf->path, r->failed);
    }
    PopRec();
}

/* ---- writing the file ----------------------------------------------------------- */

static int WriteAt(SDL_IOStream *io, u64 off, const void *p, size_t n) {
    return SDL_SeekIO(io, (Sint64)off, SDL_IO_SEEK_SET) >= 0 && SDL_WriteIO(io, p, n) == n;
}

static void MakeDirs(const char *path) {
    char *dir = SDL_strdup(path), *slash = SDL_strrchr(dir, '/');

    if (slash != NULL) {
        *slash = 0;
        SDL_CreateDirectory(dir);
    }
    SDL_free(dir);
}

typedef struct Writer {
    SDL_IOStream *io;
    u64 pos;
    XdlSection *sections;
    u32 numSections;
    u32 pack;
    int ok;
} Writer;

static u32 AddSection(Writer *w, u32 kind, u32 seg, const void *p, u32 size) {
    XdlSection *s = SDL_realloc(w->sections, (w->numSections + 1) * sizeof(XdlSection));

    if (s == NULL) {
        w->ok = 0;
        return 0;
    }
    w->sections = s;
    s += w->numSections;
    SDL_zerop(s);
    w->pos = (w->pos + 63) & ~(u64)63;
    s->kind = kind;
    s->pack = w->pack;
    s->offset = w->pos;
    s->size = s->rawSize = size;
    s->align = 64;
    s->seg = seg;
    if (size != 0 && !WriteAt(w->io, w->pos, p, size)) {
        w->ok = 0;
    }
    w->pos += size;
    return w->numSections++;
}

static void WritePack(Rec *r) {
    XdlFile *xf = r->xf;
    XdlFile index;
    Writer w;
    XdlPackInfo info;
    XdlTocEntry *toc;
    XdlHeader h;
    u32 tocSection;
    int i, fresh = 0;

    SDL_zero(index);
    SDL_zero(w);
    index.want = xf->want;
    MakeDirs(xf->path);
    w.io = SDL_IOFromFile(xf->path, "r+b");
    if (w.io == NULL || !LoadIndex(&index, w.io)) {
        if (w.io != NULL) {
            SDL_CloseIO(w.io);
        }
        w.io = SDL_IOFromFile(xf->path, "w+b");
        if (w.io == NULL) {
            Port_Log("xdl: can't write %s: %s", xf->path, SDL_GetError());
            return;
        }
        fresh = 1;
    }
    if (FindToc(&index, r->srcOffset) != NULL) {
        SDL_CloseIO(w.io);
        FreeIndex(&index);
        return;     /* written by an earlier load */
    }
    w.ok = 1;
    w.pack = index.numToc;
    if (fresh) {
        w.pos = sizeof(XdlHeader);
    } else {
        /* the old TOC and table are replaced: new sections go where the TOC was */
        w.pos = index.sections[index.numSections - 1].offset;
        w.numSections = index.numSections - 1;
        w.sections = index.sections;
        index.sections = NULL;
    }
    SDL_zero(info);
    info.type = r->type;
    info.id = r->id;
    info.srcOffset = r->srcOffset;
    info.headerSize = r->headerSize;
    SDL_memcpy(info.header, r->header, r->headerSize);
    for (i = 0; i < XDL_MAX_SEGS; i++) {
        RecSeg *s = &r->seg[i];

        if (s->copy == NULL) {
            continue;
        }
        info.seg[i].size = s->size;
        info.seg[i].role = s->at == r->soundData ? XDL_ROLE_SOUNDDATA : XDL_ROLE_KEEP;
        info.seg[i].section = AddSection(&w, XDL_SEC_SEGMENT, (u32)i, s->copy, s->size);
    }
    info.bankSeg = r->bankSeg;
    info.bankOff = r->bankOff;
    info.numFixups = r->numFix;
    info.fixupSection = AddSection(&w, XDL_SEC_FIXUPS, 0, r->fix, r->numFix * (u32)sizeof(XdlFixup));
    i = (int)AddSection(&w, XDL_SEC_PACK, 0, &info, sizeof(info));
    toc = SDL_malloc((index.numToc + 1) * sizeof(XdlTocEntry));
    if (toc == NULL) {
        w.ok = 0;
    } else {
        if (index.numToc != 0) {
            SDL_memcpy(toc, index.toc, index.numToc * sizeof(XdlTocEntry));
        }
        toc[index.numToc].type = r->type;
        toc[index.numToc].id = r->id;
        toc[index.numToc].srcOffset = r->srcOffset;
        toc[index.numToc].packSection = (u32)i;
        w.pack = ~0u;
        tocSection = AddSection(&w, XDL_SEC_TOC, 0, toc, (index.numToc + 1) * (u32)sizeof(XdlTocEntry));
        (void)tocSection;
        SDL_free(toc);
    }
    h = xf->want;
    w.pos = (w.pos + 63) & ~(u64)63;
    h.numSections = w.numSections;
    h.sectionTableOffset = (u32)w.pos;
    if (w.ok && w.pos < 0xFFFFFFFFu && WriteAt(w.io, w.pos, w.sections, w.numSections * sizeof(XdlSection))) {
        /* the header last: until it is written, the file still reads as before */
        w.ok = WriteAt(w.io, 0, &h, sizeof(h)) && SDL_FlushIO(w.io);
    } else {
        w.ok = 0;
    }
    if (!w.ok) {
        Port_Log("xdl: writing %s failed", xf->path);
    } else if (r->fallback) {
        Port_Log("xdl: pack at 0x%x of %s: %u listed pointers outside the pack, relocated as listed", r->srcOffset,
                 xf->path, r->fallback);
    }
    SDL_free(w.sections);
    FreeIndex(&index);
    SDL_CloseIO(w.io);
}

/* ---- reading -------------------------------------------------------------------- */

XdlPack *Xdl_FindPack(void *p, u32 srcOffset) {
    XdlFile *xf = p;
    const XdlTocEntry *e;
    const XdlSection *s;
    XdlPack *xp;
    u32 i;

    if (xf == NULL || xf->io == NULL || (e = FindToc(xf, srcOffset)) == NULL) {
        return NULL;
    }
    s = &xf->sections[e->packSection];
    xp = SDL_calloc(1, sizeof(XdlPack));
    if (xp == NULL || s->kind != XDL_SEC_PACK || s->size != sizeof(XdlPackInfo) ||
        !ReadAt(xf->io, s->offset, &xp->info, sizeof(XdlPackInfo)) || xp->info.fixupSection >= xf->numSections ||
        xp->info.headerSize > sizeof(xp->info.header)) {
        goto bad;
    }
    for (i = 0; i < XDL_MAX_SEGS; i++) {
        if (xp->info.seg[i].size != 0 && (xp->info.seg[i].section >= xf->numSections ||
                                          xf->sections[xp->info.seg[i].section].size != xp->info.seg[i].size)) {
            goto bad;
        }
    }
    s = &xf->sections[xp->info.fixupSection];
    if (s->size != xp->info.numFixups * sizeof(XdlFixup)) {
        goto bad;
    }
    xp->fix = SDL_malloc(s->size + 1);
    xp->applied = SDL_calloc(xp->info.numFixups + 1, 1);
    if (xp->fix == NULL || xp->applied == NULL || !ReadAt(xf->io, s->offset, xp->fix, s->size)) {
        goto bad;
    }
    for (i = 0; i < xp->info.numFixups; i++) {
        const XdlFixup *f = &xp->fix[i];

        if (f->slotSeg >= XDL_MAX_SEGS || f->target >= XDL_MAX_SEGS ||
            (u64)f->slot + 4 > xp->info.seg[f->slotSeg].size) {
            goto bad;
        }
    }
    xp->xf = xf;
    return xp;
bad:
    Port_Log("xdl: pack at 0x%x of %s is damaged, loading the source", srcOffset, xf->path);
    Xdl_FreePack(xp);
    return NULL;
}

void Xdl_FreePack(XdlPack *xp) {
    if (xp != NULL) {
        SDL_free(xp->fix);
        SDL_free(xp->applied);
        SDL_free(xp);
    }
}

void Xdl_Header(XdlPack *xp, void *header, u32 size) {
    if (size != xp->info.headerSize) {
        Port_Fatal("xdl: %s holds a pack header of %u bytes, not %u", xp->xf->path, xp->info.headerSize, size);
    }
    SDL_memcpy(header, xp->info.header, size);
}

u32 Xdl_SegSize(XdlPack *xp, int seg) {
    return xp->info.seg[seg].size;
}

int Xdl_BlockRole(XdlPack *xp, int seg) {
    return (int)xp->info.seg[seg].role;
}

int Xdl_BankRef(XdlPack *xp, u32 *offset) {
    *offset = xp->info.bankOff;
    return xp->info.bankSeg;
}

void Xdl_ReadSeg(XdlPack *xp, int seg, void *dst, u32 from, u32 size) {
    const XdlSection *s = &xp->xf->sections[xp->info.seg[seg].section];

    if (size == 0) {
        return;
    }
    if ((u64)from + size > xp->info.seg[seg].size || !ReadAt(xp->xf->io, s->offset + from, dst, size)) {
        Port_Fatal("xdl: can't read segment %d of the pack at 0x%x in %s", seg, xp->info.srcOffset, xp->xf->path);
    }
}

void Xdl_PlaceSeg(XdlPack *xp, int seg, void *at) {
    u32 i;

    xp->at[seg] = at;
    if (at == NULL) {
        return;
    }
    for (i = 0; i < xp->info.numFixups; i++) {
        const XdlFixup *f = &xp->fix[i];

        if (!xp->applied[i] && xp->at[f->slotSeg] != NULL && xp->at[f->target] != NULL) {
            *(u32 *)(xp->at[f->slotSeg] + f->slot) += HND(xp->at[f->target]);
            xp->applied[i] = 1;
        }
    }
}
