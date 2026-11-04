/* GX command stream decoding: display lists and immediate-mode FIFO writes.
 *
 * Both arrive as big-endian GX commands. Immediate mode is GXBegin followed by
 * GXWGFifo_* writes of the vertex data; the vertices are decoded as soon as
 * all of them have arrived (the game never calls GXEnd). Indexed attributes
 * read from the arrays set with GXSetArray, whose components are in host
 * order (data packs are swapped on load, runtime arrays are written by C).
 *
 * The game re-runs the same display list every frame: walk it, decode the same
 * indices into the same vertices, hand the same vertices to the renderer. That
 * decode is most of what the core spends on graphics, so a list is walked once
 * and later calls replay the decoded draws. See "display-list cache" below. */
#include "gx_internal.h"

/* ===========================================================================
 * Display-list cache
 *
 * Reusing memory is what makes keeping decoded lists risky, and the game does
 * it constantly: packs are freed and new ones land on the same addresses.
 * Three things keep it honest:
 *
 *   - the list bytes are hashed on every call, so a list rewritten in place is
 *     a miss (a hash is far cheaper than the decode it replaces);
 *   - the whole vertex-format state is part of the key, so the same list drawn
 *     under a different vertex setup is a different entry;
 *   - each entry records the bytes it read - the list itself plus every vertex
 *     array it indexed - and Port_InvalidateDisplayLists() drops the entries
 *     that overlap a range being rewritten: the pack swapper calls it after
 *     swapping a pack (same addresses, new bytes) and OSFreeToHeap() calls it
 *     when a block is handed back to the heap.
 *
 * What is left uncovered: a list whose vertex arrays stay at one address while
 * the game rewrites their contents, which the port cannot see. HWVX_DLVERIFY=1
 * re-decodes every cached list and compares it with the cached result, which is
 * how to find out on real content. HWVX_NO_DLCACHE=1 turns the cache off.
 * ========================================================================= */

#define CACHE_BUCKETS 16384u  /* power of two */
#define CACHE_ENTRIES 8192u
#define CACHE_MAXSIZE (1024u * 1024u) /* longer lists decode inline */
#define POOL_VERTS 262144u           /* one pool for every cached vertex */
#define ARENA_DRAWS 262144u          /* recorded draws across all entries */

/* One recorded draw: pool vertices [vStart, vStart+nVerts). Quads are stored
 * already expanded to triangles, in the order GX_Draw would draw them. */
typedef struct {
    u32 prim;
    u32 vStart;
    u32 nVerts;
} DlDraw;

/* Everything the decoder reads besides the list bytes. */
typedef struct {
    u8 vcd[GX_NUM_ATTR];
    GXVtxAttrFmt vat[GX_NUM_VTXFMT][GX_NUM_ATTR];
    u64 array[GX_NUM_ATTR];    /* array pointers as bits: the key must see all of them */
    u8 arrayStride[GX_NUM_ATTR];
    u32 mtx;                   /* default matrix index for vertices */
} DlState;

typedef struct {
    u32 hash;
    DlState state;
    u64 dl;     /* the list itself, so a hit is the same list at the same address */
    u64 lo, hi; /* bytes read: the list plus the vertex arrays it indexed */
    u32 size;
    u32 vtxBase;  /* first pool vertex owned, pool vertices from here are live */
    u32 nLive;
    u32 drawBase; /* first recorded draw */
    u32 nDraws;
    s16 loAttr, hiAttr; /* texcoord indices indexed, -1 = none */
    u8 used;
    u8 want;      /* set when seen in a sweep: eviction skips these */
    u32 seq;
    u32 maxIdx[GX_NUM_ATTR]; /* capture: highest index value per attribute */
} DlEntry;

static void CacheUnlink(DlEntry *e);
static void CacheReset(void);

static u32 s_head[CACHE_BUCKETS];  /* entry index + 1, 0 = empty */
static u32 s_next[CACHE_ENTRIES];  /* entry index + 1, 0 = end of chain */
static DlEntry s_entry[CACHE_ENTRIES];
static DlDraw *s_arena;            /* recorded draws, bump allocated */
static GXVertex *s_pool;           /* decoded vertices, bump allocated */
static u32 s_arenaUsed, s_poolUsed;
static u32 s_poolGen = 1;          /* bumps when the pool starts over */
static u32 s_freeHead;             /* entry index + 1, 0 = none free */
static u32 s_usedCnt;
static u32 s_seq;
static u64 s_spanLo, s_spanHi;     /* bytes the live entries read, empty when lo >= hi */
static int s_spanDirty;
static GXVtxBatch s_batch;         /* the pool, as handed to the renderer */
static u32 *s_maxIdx;              /* capture: index high marks, NULL when off */
static int s_cacheOff = -1;
static int s_verify;
static u32 s_statHit, s_statMiss, s_statFlush, s_statAbort, s_statDrop, s_statHash, s_statSkip;

/* ---- miss forensics -------------------------------------------------------
 * A replay needs the list bytes, the list address and the decode state to all
 * come back unchanged. When the game never lets that happen, these counters
 * say which one moves. Direct mapped by (address, size): one probe per miss,
 * nothing on the hit path. A slot whose bytes are rewritten over and over is
 * marked volatile, and capturing it is abandoned: those lists decode inline. */
#define DIAG_SLOTS 8192u
typedef struct {
    u64 addr;
    u32 size;
    u32 hBytes; /* hash of the list bytes alone */
    u32 hFmt;   /* hash of vcd + vat */
    u32 hArr;   /* hash of array pointers + strides */
    u32 mtx;
    u8 rebuilds; /* consecutive times the bytes came back different */
    u8 aborts;   /* consecutive captures that did not fit the pool */
    u8 volatile_;
} DiagSlot;

static DiagSlot s_diag[DIAG_SLOTS];
static u32 s_dFirst, s_dRebuild, s_dStable, s_dFmt, s_dArr, s_dMtx;

static int CacheEnabled(void) {
    if (s_cacheOff < 0) {
        const char *e = SDL_getenv("HWVX_NO_DLCACHE");
        s_verify = SDL_getenv("HWVX_DLVERIFY") != NULL;
        s_cacheOff = (e && e[0] != '0' && e[0] != '\0');
        printf("GX: display-list cache %s%s\n", s_cacheOff ? "off" : "on",
               s_verify && !s_cacheOff ? " (verify)" : "");
    }
    return !s_cacheOff;
}

static u32 HashBytes(const void *p, u32 n, u32 h) {
    const u8 *b = (const u8 *)p;
    u32 v;

    while (n >= 4) {
        memcpy(&v, b, 4);
        h = (h ^ v) * 16777619u;
        b += 4;
        n -= 4;
    }
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

/* ---- component readers -------------------------------------------------- */

typedef struct Reader {
    const u8 *p, *end;
    int bigEndian;
} Reader;

static u32 RdU8(Reader *r) { return r->p < r->end ? *r->p++ : 0; }

static u32 RdU16(Reader *r) {
    u32 v;
    if (r->p + 2 > r->end) {
        r->p = r->end;
        return 0;
    }
    v = r->bigEndian ? Port_ReadBE16(r->p) : *(const u16 *)(const void *)r->p;
    r->p += 2;
    return v;
}

static u32 RdU32(Reader *r) {
    u32 v;
    if (r->p + 4 > r->end) {
        r->p = r->end;
        return 0;
    }
    v = r->bigEndian ? Port_ReadBE32(r->p) : *(const u32 *)(const void *)r->p;
    r->p += 4;
    return v;
}

/* GX component types: U8, S8, U16, S16, F32 */
static f32 RdComp(Reader *r, int type, int frac) {
    f32 scale = 1.0f / (f32)(1 << frac);
    union { u32 u; f32 f; } c;
    switch (type) {
    case 0: return (f32)RdU8(r) * scale;
    case 1: return (f32)(s8)RdU8(r) * scale;
    case 2: return (f32)RdU16(r) * scale;
    case 3: return (f32)(s16)RdU16(r) * scale;
    default:
        c.u = RdU32(r);
        return c.f;
    }
}

static const int s_compSize[5] = { 1, 1, 2, 2, 4 };

/* GX color types: RGB565, RGB8, RGBX8, RGBA4, RGBA6, RGBA8 */
/* colors: expanded to 8 bits exactly as the hardware color registers do
 * (bit replication), so the GPU's val/255 matches what TEV saw. */
static void RdColor(Reader *r, int type, u8 *out) {
    u32 v;
    u32 c;
    switch (type) {
    case 0:
        v = RdU16(r);
        c = (v >> 11) & 0x1F; out[0] = (u8)((c << 3) | (c >> 2));
        c = (v >> 5) & 0x3F;  out[1] = (u8)((c << 2) | (c >> 4));
        c = v & 0x1F;         out[2] = (u8)((c << 3) | (c >> 2));
        out[3] = 255;
        break;
    case 1:
        out[0] = RdU8(r);
        out[1] = RdU8(r);
        out[2] = RdU8(r);
        out[3] = 255;
        break;
    case 2:
        out[0] = RdU8(r);
        out[1] = RdU8(r);
        out[2] = RdU8(r);
        RdU8(r);
        out[3] = 255;
        break;
    case 3:
        v = RdU16(r);
        c = (v >> 12) & 0xF; out[0] = (u8)((c << 4) | c);
        c = (v >> 8) & 0xF;  out[1] = (u8)((c << 4) | c);
        c = (v >> 4) & 0xF;  out[2] = (u8)((c << 4) | c);
        c = v & 0xF;         out[3] = (u8)((c << 4) | c);
        break;
    case 4:
        v = (RdU8(r) << 16);
        v |= RdU8(r) << 8;
        v |= RdU8(r);
        c = (v >> 18) & 0x3F; out[0] = (u8)((c << 2) | (c >> 4));
        c = (v >> 12) & 0x3F; out[1] = (u8)((c << 2) | (c >> 4));
        c = (v >> 6) & 0x3F;  out[2] = (u8)((c << 2) | (c >> 4));
        c = v & 0x3F;         out[3] = (u8)((c << 2) | (c >> 4));
        break;
    default:
        out[0] = RdU8(r);
        out[1] = RdU8(r);
        out[2] = RdU8(r);
        out[3] = RdU8(r);
        break;
    }
}

static const int s_colorSize[6] = { 2, 3, 4, 2, 3, 4 };

/* Size of one attribute's data as it appears directly in the stream. */
static int DirectSize(int attr, const GXVtxAttrFmt *f) {
    if (attr <= 8) {
        return 1;                                   /* matrix indices */
    }
    switch (attr) {
    case GX_VA_POS: return (f->cnt ? 3 : 2) * s_compSize[f->type > 4 ? 4 : f->type];
    case GX_VA_NRM: return (f->cnt ? 9 : 3) * s_compSize[f->type > 4 ? 4 : f->type];
    case GX_VA_CLR0:
    case GX_VA_CLR1: return s_colorSize[f->type > 5 ? 5 : f->type];
    default: return (f->cnt ? 2 : 1) * s_compSize[f->type > 4 ? 4 : f->type];
    }
}

static int VertexSize(int fmt) {
    int size = 0, a;
    for (a = 0; a < GX_NUM_ATTR; a++) {
        switch (g_gx.vcd[a]) {
        case GX_DIRECT: size += DirectSize(a, &g_gx.vat[fmt][a]); break;
        case GX_INDEX8: size += 1; break;
        case GX_INDEX16: size += 2; break;
        }
    }
    return size;
}

/* Decode one attribute from `r` (direct data) into `v`. */
/* Shader slot for a position matrix id (row address / 3). Ids 0-27 are the
 * loadable matrices; anything else (in practice 60, GX_IDENTITY) is identity. */
static f32 PosMtxSlot(u32 id) {
    return (f32)(id / 3 < GX_NUM_POSMTX ? id / 3 : GX_POSMTX_IDENTITY);
}

static void DecodeAttr(Reader *r, int attr, const GXVtxAttrFmt *f, GXVertex *v) {
    int i, n;

    if (attr == GX_VA_PNMTXIDX) {
        v->mtxIdx = PosMtxSlot(RdU8(r));
        return;
    }
    if (attr <= 8) {
        RdU8(r);                                    /* texture matrix index: unused */
        return;
    }
    switch (attr) {
    case GX_VA_POS:
        v->pos[0] = RdComp(r, f->type, f->frac);
        v->pos[1] = RdComp(r, f->type, f->frac);
        v->pos[2] = f->cnt ? RdComp(r, f->type, f->frac) : 0.0f;
        break;
    case GX_VA_NRM: {
        /* normals: fixed exponent 6 (S8) or 14 (S16); stored at scale 1/64,
         * which is exact for S8 data and hardware-comparable otherwise */
        int frac = f->type == 1 ? 6 : f->type == 3 ? 14 : 0;
        n = f->cnt ? 9 : 3;
        for (i = 0; i < n; i++) {
            f32 c = RdComp(r, f->type, frac);
            if (i < 3) {
                c *= 64.0f;
                int q = (int)(c >= 0 ? c + 0.5f : c - 0.5f);
                v->nrm[i] = (s8)(q > 127 ? 127 : q < -127 ? -127 : q);
            }
        }
        break;
    }
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        RdColor(r, f->type, v->clr[attr - GX_VA_CLR0]);
        if (f->cnt == 0) {
            v->clr[attr - GX_VA_CLR0][3] = 255;
        }
        break;
    default:
        n = attr - GX_VA_TEX0;
        v->tex[n][0] = RdComp(r, f->type, f->frac);
        v->tex[n][1] = f->cnt ? RdComp(r, f->type, f->frac) : 0.0f;
        break;
    }
}

static void DecodeVertex(Reader *r, int fmt, GXVertex *v) {
    int a;

    memset(v, 0, sizeof(*v));
    v->mtxIdx = PosMtxSlot(g_gx.curMtx);
    v->clr[0][0] = v->clr[0][1] = v->clr[0][2] = v->clr[0][3] = 255;
    for (a = 0; a < GX_NUM_ATTR; a++) {
        const GXVtxAttrFmt *f = &g_gx.vat[fmt][a];
        u32 idx;

        switch (g_gx.vcd[a]) {
        case GX_DIRECT:
            DecodeAttr(r, a, f, v);
            break;
        case GX_INDEX8:
        case GX_INDEX16:
            idx = g_gx.vcd[a] == GX_INDEX8 ? RdU8(r) : RdU16(r);
            if (s_maxIdx != NULL && idx > s_maxIdx[a]) {
                s_maxIdx[a] = idx;
            }
            if (g_gx.array[a] != NULL) {
                Reader ar;
                ar.p = g_gx.array[a] + idx * g_gx.arrayStride[a];
                ar.end = ar.p + 64;
                ar.bigEndian = 0;
                DecodeAttr(&ar, a, f, v);
            }
            break;
        }
    }
}

/* ---- draws ------------------------------------------------------------- */

static GXVertex *s_verts;
static u32 s_vertsCap;

static void DrawPrims(Reader *r, int prim, int fmt, u32 count) {
    u32 i;

    if (count > s_vertsCap) {
        s_vertsCap = count + 1024;
        s_verts = SDL_realloc(s_verts, s_vertsCap * sizeof(GXVertex));
    }
    for (i = 0; i < count; i++) {
        DecodeVertex(r, fmt, &s_verts[i]);
    }
    GX_Draw(prim, s_verts, count);
}

/* ---- cache storage ------------------------------------------------------ */

/* Vertices and recorded draws are bump allocated: an entry owns the range it
 * took, and a range is only reclaimed by starting the pool over. */
static int CacheAlloc(void) {
    if (s_pool != NULL) {
        return 1;
    }
    s_pool = SDL_malloc(POOL_VERTS * sizeof(GXVertex));
    s_arena = SDL_malloc(ARENA_DRAWS * sizeof(DlDraw));
    if (s_pool == NULL || s_arena == NULL) {
        s_cacheOff = 1;
        return 0;
    }
    CacheReset();
    return 1;
}

static void CacheReset(void) {
    u32 i;

    memset(s_head, 0, sizeof(s_head));
    for (i = 0; i < CACHE_ENTRIES; i++) {
        s_entry[i].used = 0;
        s_next[i] = i + 1 < CACHE_ENTRIES ? i + 2 : 0;
    }
    s_freeHead = 1;
    s_usedCnt = 0;
    s_arenaUsed = 0;
    s_poolUsed = 0;
    s_spanLo = ~(u64)0;
    s_spanHi = 0;
    s_spanDirty = 0;
    s_poolGen++;
    s_seq++;
}

static void StateSnapshot(DlState *st) {
    int a, f;

    memset(st, 0, sizeof(*st));
    for (a = 0; a < GX_NUM_ATTR; a++) {
        st->vcd[a] = g_gx.vcd[a];
        st->array[a] = (u64)(size_t)g_gx.array[a];
        st->arrayStride[a] = g_gx.arrayStride[a];
        for (f = 0; f < GX_NUM_VTXFMT; f++)
            st->vat[f][a] = g_gx.vat[f][a];
    }
    st->mtx = g_gx.curMtx;
}

static DlEntry *CacheFind(u32 h, const DlState *st, const void *p, u32 size) {
    u32 slot = h & (CACHE_BUCKETS - 1);
    u32 *link = &s_head[slot];

    while (*link != 0) {
        DlEntry *e = &s_entry[*link - 1];
        if (e->hash == h && e->size == size && e->dl == (u64)(size_t)p &&
            memcmp(&e->state, st, sizeof(*st)) == 0) {
            return e;
        }
        link = &s_next[*link - 1];
    }
    return NULL;
}

/* Free the least recently used entry. Its pool space is only reclaimed by a
 * reset, but the slot and the key go back. */
static DlEntry *CacheEvict(void) {
    DlEntry *pick = NULL;
    u32 i;

    for (i = 0; i < CACHE_ENTRIES; i++) {
        DlEntry *e = &s_entry[i];
        if (!e->used)
            continue;
        if (pick == NULL || e->want < pick->want ||
            (e->want == pick->want && e->seq < pick->seq))
            pick = e;
    }
    if (pick == NULL)
        return NULL;
    CacheUnlink(pick);
    return pick;
}

static void CacheUnlink(DlEntry *e) {
    u32 i = (u32)(e - s_entry);
    u32 slot = e->hash & (CACHE_BUCKETS - 1);
    u32 *link = &s_head[slot];

    while (*link != 0 && *link - 1 != i)
        link = &s_next[*link - 1];
    if (*link == 0)
        return;
    *link = s_next[i];
    e->used = 0;
    e->want = 0;
    s_usedCnt--;
    s_next[i] = s_freeHead;
    s_freeHead = i + 1;
    s_spanDirty = 1;              /* the span may shrink: work it out when asked */
}

static DlEntry *CacheReserve(u32 h, const DlState *st) {
    DlEntry *e;
    u32 slot = h & (CACHE_BUCKETS - 1);

    if (s_freeHead == 0 && CacheEvict() == NULL)
        return NULL;
    if (s_freeHead == 0)
        return NULL;
    e = &s_entry[s_freeHead - 1];
    s_freeHead = s_next[s_freeHead - 1];
    memset(e, 0, sizeof(*e));
    e->used = 1;
    e->want = 1;
    e->seq = s_seq;
    e->hash = h;
    e->size = 0;
    e->lo = e->hi = 0;
    e->loAttr = e->hiAttr = -1;
    memcpy(&e->state, st, sizeof(*st));
    s_next[(u32)(e - s_entry)] = s_head[slot];
    s_head[slot] = (u32)(e - s_entry) + 1;
    s_usedCnt++;
    return e;
}

/* The entry's range of pool vertices and recorded draws is handed back. */
static void CacheAbort(DlEntry *e) {
    CacheUnlink(e);
    s_poolUsed = e->vtxBase;
    s_arenaUsed = e->drawBase;
    s_statAbort++;
}

/* ---- miss forensics -------------------------------------------------------- */

static void StateSigs(const DlState *st, u32 *hFmt, u32 *hArr, u32 *mtx) {
    u32 a = 2166136261u, b = 2166136261u;

    a = HashBytes(st->vcd, sizeof(st->vcd), a);
    a = HashBytes(st->vat, sizeof(st->vat), a);
    b = HashBytes(st->array, sizeof(st->array), b);
    b = HashBytes(st->arrayStride, sizeof(st->arrayStride), b);
    *hFmt = a;
    *hArr = b;
    *mtx = st->mtx;
}

static DiagSlot *DiagSlotFor(const void *p, u32 size) {
    return &s_diag[(((u32)((size_t)p >> 4) * 2654435761u) ^ (size * 40503u)) &
                   (DIAG_SLOTS - 1)];
}

/* Called on every cache miss. Answers why this address did not replay - which
 * of the bytes, the arrays or the matrix moved - and whether capturing it is
 * worth trying at all. Returns 1 when the list is volatile: the capture is
 * skipped and the list decodes inline. */
static int DiagMiss(const void *p, u32 size, u32 hBytes, const DlState *st) {
    DiagSlot *d = DiagSlotFor(p, size);
    u32 hf, ha, mx;

    if (d->addr != (u64)(size_t)p || d->size != size) {
        /* first time through this slot, or another list took its place */
        d->addr = (u64)(size_t)p;
        d->size = size;
        d->hBytes = hBytes;
        StateSigs(st, &d->hFmt, &d->hArr, &d->mtx);
        d->rebuilds = 0;
        d->aborts = 0;
        d->volatile_ = 0;
        s_dFirst++;
        return 0;
    }
    if (d->hBytes != hBytes) {
        /* the game rebuilt the list in place: the old bytes are gone */
        s_dRebuild++;
        d->hBytes = hBytes;
        StateSigs(st, &d->hFmt, &d->hArr, &d->mtx);
        d->aborts = 0; /* new bytes, new size: the next capture may well fit */
        if (d->volatile_) {
            return 1;
        }
        if (++d->rebuilds >= 3) {
            d->volatile_ = 1;
            return 1;
        }
        return 0;
    }
    StateSigs(st, &hf, &ha, &mx);
    if (hf != d->hFmt || ha != d->hArr || mx != d->mtx) {
        /* same bytes, different decode inputs: name the one that moved */
        if (hf != d->hFmt)
            s_dFmt++;
        if (ha != d->hArr)
            s_dArr++;
        if (mx != d->mtx)
            s_dMtx++;
        d->hFmt = hf;
        d->hArr = ha;
        d->mtx = mx;
        return 0;
    }
    /* bytes and state both match a call we already saw: the entry that should
     * have answered was removed under it - a pool reset or an eviction */
    s_dStable++;
    if (d->volatile_)
        d->volatile_ = 0; /* the bytes settled again: give caching another go */
    d->rebuilds = 0;
    return 0;
}

/* ---- capture and replay ------------------------------------------------- */

/* Quads expand the way GX_Draw draws them: v0,v1,v2 and v0,v2,v3. */

/* One recorded draw. Consecutive draws of the same kind whose vertices land next
 * to each other in the pool are recorded as a single draw: they come from the
 * same list, whose state cannot change inside itself (only GXSet* calls outside
 * it do that), and the vertex order is kept, so one draw is the same pixels.
 * Runs of quads and of small triangle batches - what the game's geometry lists
 * are mostly made of - become one draw call each. Draws from different lists are
 * never merged: those can set different state between themselves. */
static void RecordDraw(DlEntry *e, u32 prim, u32 base, u32 count) {
    DlDraw *last;

    if (s_arenaUsed > e->drawBase) {
        last = &s_arena[s_arenaUsed - 1];
        if (last->prim == prim && last->vStart + last->nVerts == base) {
            last->nVerts += count;
            return;
        }
    }
    s_arena[s_arenaUsed++] = (DlDraw){ prim, base, count };
}

static int EmitPrim(Reader *r, int prim, int fmt, u32 count, DlEntry *e) {
    u32 i;

    if (e == NULL) {
        DrawPrims(r, prim, fmt, count);
        return 1;
    }
    if (prim == GX_QUADS) {
        u32 nq = count / 4;
        GXVertex scratch;

        if (nq == 0) {
            for (i = 0; i < count; i++)
                DecodeVertex(r, fmt, &scratch);
            return 1;
        }
        if (s_poolUsed + nq * 6 > POOL_VERTS || s_arenaUsed + 1 > ARENA_DRAWS)
            return 0;
        {
            u32 base = s_poolUsed;
            for (i = 0; i < nq; i++) {
                GXVertex *v = &s_pool[base + i * 6];
                DecodeVertex(r, fmt, &v[0]);
                DecodeVertex(r, fmt, &v[1]);
                DecodeVertex(r, fmt, &v[2]);
                DecodeVertex(r, fmt, &v[5]);
                v[3] = v[0];
                v[4] = v[2];
            }
            for (i = nq * 4; i < count; i++)
                DecodeVertex(r, fmt, &scratch);
            s_poolUsed = base + nq * 6;
            RecordDraw(e, GX_TRIANGLES, base, nq * 6);
        }
        return 1;
    }
    if (s_poolUsed + count > POOL_VERTS || s_arenaUsed + 1 > ARENA_DRAWS)
        return 0;
    {
        u32 base = s_poolUsed;
        for (i = 0; i < count; i++)
            DecodeVertex(r, fmt, &s_pool[base + i]);
        s_poolUsed = base + count;
        RecordDraw(e, (u32)prim, base, count);
    }
    return 1;
}

/* Walk one list. `e` records the draws when non-NULL, and the index values read
 * from arrays are tracked to learn how much vertex data the list touches.
 * Returns 1 when the walk finished, 0 when a draw did not fit and the capture
 * must be thrown away, -1 when an unknown opcode stopped the walk. */
static int RunList(Reader *r, DlEntry *e) {
    int ok = 1;

    s_maxIdx = e != NULL ? e->maxIdx : NULL;
    while (r->p < r->end && ok) {
        u32 op = RdU8(r);
        u32 n;

        switch (op) {
        case 0x00: /* NOP */
        case 0x48: /* invalidate vertex cache */
            break;
        case 0x08: /* load CP register */
            r->p += 5;
            break;
        case 0x10: /* load XF registers */
            n = (RdU32(r) >> 16) + 1;
            r->p += n * 4;
            break;
        case 0x20: case 0x28: case 0x30: case 0x38: /* indexed XF loads */
            r->p += 4;
            break;
        case 0x40: /* call display list */
            r->p += 8;
            break;
        case 0x61: /* load BP register */
            r->p += 4;
            break;
        default:
            if (op >= 0x80 && op < 0xC0) {
                n = RdU16(r);
                ok = EmitPrim(r, op & 0xF8, op & 7, n, e);
            } else {
                Port_Log("GX: unknown display list opcode 0x%02X", op);
                s_maxIdx = NULL;
                return -1;
            }
            break;
        }
    }
    s_maxIdx = NULL;
    if (e != NULL && ok) {
        u32 a;
        u64 lo = (u64)(size_t)r->p;
        u64 hi = (u64)(size_t)(r->end - 1);

        for (a = 0; a < GX_NUM_ATTR; a++) {
            if (g_gx.vcd[a] != GX_INDEX8 && g_gx.vcd[a] != GX_INDEX16)
                continue;
            if (e->maxIdx[a] == 0 && g_gx.arrayStride[a] == 0)
                continue;
            if (e->loAttr < 0 || (int)a < e->loAttr)
                e->loAttr = (s16)a;
            if ((int)a > e->hiAttr)
                e->hiAttr = (s16)a;
            if (g_gx.array[a] == NULL)
                continue;
            {
                u64 alo = (u64)(size_t)g_gx.array[a];
                u64 ahi = alo + (u64)(e->maxIdx[a] + 1) * g_gx.arrayStride[a];
                if (alo < lo)
                    lo = alo;
                if (ahi > hi)
                    hi = ahi;
            }
        }
        e->lo = lo;
        e->hi = hi;
    }
    return ok;
}

static void Replay(DlEntry *e) {
    u32 i;

    for (i = 0; i < e->nDraws; i++) {
        const DlDraw *d = &s_arena[e->drawBase + i];
        GX_Draw((int)d->prim, &s_pool[d->vStart], d->nVerts);
    }
}

/* HWVX_DLVERIFY=1: decode the list again and compare it with what is cached.
 * That is slow; it exists to prove the cache is faithful on real content. On a
 * mismatch the entry is dropped and the list decodes inline, so a list whose
 * vertex arrays are rewritten under it still draws correctly while the log
 * shows which lists do that. */
static int VerifyEntry(DlEntry *e, const u8 *cmds, u32 size) {
    u32 vbase = s_poolUsed, abase = s_arenaUsed;
    DlEntry fresh;
    Reader r;
    u32 i;
    int bad = 0;

    memset(&fresh, 0, sizeof(fresh));
    fresh.vtxBase = vbase;
    fresh.drawBase = abase;
    r.p = cmds;
    r.end = cmds + size;
    r.bigEndian = 1;
    if (RunList(&r, &fresh) != 1) {
        s_poolUsed = vbase;
        s_arenaUsed = abase;
        return 0;
    }
    fresh.nDraws = s_arenaUsed - abase;
    if (fresh.nDraws != e->nDraws) {
        bad = -1;
    } else {
        for (i = 0; i < e->nDraws; i++) {
            const DlDraw *a = &s_arena[e->drawBase + i];
            const DlDraw *b = &s_arena[abase + i];
            if (a->prim != b->prim || a->nVerts != b->nVerts ||
                memcmp(&s_pool[a->vStart], &s_pool[b->vStart], a->nVerts * sizeof(GXVertex)) != 0) {
                bad = (int)i + 1;
                break;
            }
        }
    }
    s_poolUsed = vbase;
    s_arenaUsed = abase;
    if (bad == 0)
        return 1;
    Port_Log("GX: DL cache mismatch at %p (draw %d, cached %u draws, fresh %u): dropping",
             (const void *)(size_t)cmds, bad, e->nDraws, fresh.nDraws);
    CacheUnlink(e);
    return 0;
}

/* ---- command streams ---------------------------------------------------- */

void GX_RunCommands(const u8 *cmds, u32 size) {
    Reader r;
    DlState st;
    DlEntry *e;
    u32 h, hBytes;

    if (size == 0 || !CacheEnabled() || size > CACHE_MAXSIZE || !CacheAlloc()) {
        r.p = cmds;
        r.end = cmds + size;
        r.bigEndian = 1;
        RunList(&r, NULL);
        return;
    }

    h = 2166136261u;
    h = HashBytes(cmds, size, h);
    hBytes = h;
    StateSnapshot(&st);
    h = HashBytes(&st, sizeof(st), h);
    s_statHash++;

    e = CacheFind(h, &st, cmds, size);
    if (e != NULL) {
        s_seq++;
        e->want = 1;
        e->seq = s_seq;
        if (s_verify && !VerifyEntry(e, cmds, size)) {
            r.p = cmds;
            r.end = cmds + size;
            r.bigEndian = 1;
            RunList(&r, NULL);
            return;
        }
        s_statHit++;
        Replay(e);
        return;
    }
    s_statMiss++;

    if (DiagMiss(cmds, size, hBytes, &st)) {
        s_statSkip++; /* this address is rebuilt every time: don't bother */
    } else {
        e = CacheReserve(h, &st);
        if (e != NULL) {
            int fit;

            e->vtxBase = s_poolUsed;
            e->drawBase = s_arenaUsed;
            e->size = size;
            e->dl = (u64)(size_t)cmds;
            r.p = cmds;
            r.end = cmds + size;
            r.bigEndian = 1;
            fit = RunList(&r, e);
            if (fit == 1) {
                e->nLive = s_poolUsed - e->vtxBase;
                e->nDraws = s_arenaUsed - e->drawBase;
                if (e->nDraws == 0) {
                    CacheAbort(e);      /* nothing came of it: give the space back */
                } else {
                    DiagSlotFor(cmds, size)->aborts = 0;
                    if (e->lo < s_spanLo)
                        s_spanLo = e->lo;
                    if (e->hi > s_spanHi)
                        s_spanHi = e->hi;
                    Replay(e);
                    return;
                }
            } else {
                DiagSlot *d = DiagSlotFor(cmds, size);

                CacheAbort(e);
                /* A capture that did not fit, or that hit an opcode this
                 * decoder stops at, repeats the same waste every call. Three
                 * in a row and this address is left alone: it decodes inline. */
                if (d->addr == (u64)(size_t)cmds && d->size == size && d->aborts < 250 &&
                    ++d->aborts >= 3)
                    d->volatile_ = 1;
                if (fit == 0 && (s_poolUsed > POOL_VERTS / 4 || s_arenaUsed > ARENA_DRAWS / 4)) {
                    CacheReset();       /* pool full: start over and decode inline */
                    s_statFlush++;
                }
            }
        }
    }
    r.p = cmds;
    r.end = cmds + size;
    r.bigEndian = 1;
    RunList(&r, NULL);
}

/* The pool, when these vertices are part of it; NULL when they are the scratch
 * buffer of a decode, which the renderer uploads the usual way. */
const GXVtxBatch *GX_FifoBatch(const GXVertex *v, u32 n) {
    if (s_pool == NULL || s_cacheOff > 0)
        return NULL;
    if (v < s_pool || v + n > s_pool + s_poolUsed)
        return NULL;
    s_batch.base = s_pool;
    s_batch.used = s_poolUsed;
    s_batch.cap = POOL_VERTS;
    s_batch.gen = s_poolGen;
    return &s_batch;
}

/* 1 if the pool can hold `need` vertices; renderers size their upload buffers
 * from this instead of guessing. */
u32 GX_FifoPoolCapacity(u32 need) {
    return need <= POOL_VERTS;
}

/* Vertices currently held by the pool (all cached draws plus nothing else). */
u32 GX_FifoPoolUsed(void) {
    return s_poolUsed;
}

/* Forget everything that read from [p, p+size). Called when game memory is
 * freed or written over, so a cached list cannot keep serving stale vertices. */
static void SpanRefresh(void) {
    u32 i;

    if (!s_spanDirty) {
        return;
    }
    s_spanLo = ~(u64)0;
    s_spanHi = 0;
    for (i = 0; i < CACHE_ENTRIES; i++) {
        DlEntry *e = &s_entry[i];
        if (!e->used)
            continue;
        if (e->lo < s_spanLo)
            s_spanLo = e->lo;
        if (e->hi > s_spanHi)
            s_spanHi = e->hi;
    }
    s_spanDirty = 0;
}

void Port_InvalidateDisplayLists(const void *p, u32 size) {
    u32 i;
    u64 lo = (u64)(size_t)p;
    u64 hi = lo + (u64)size;

    if (s_usedCnt == 0 || size == 0)
        return;
    /* Most frees are nowhere near what is cached; the span says so in O(1). */
    SpanRefresh();
    if (s_spanHi <= s_spanLo || hi <= s_spanLo || lo >= s_spanHi)
        return;
    for (i = 0; i < CACHE_ENTRIES; i++) {
        DlEntry *e = &s_entry[i];
        if (!e->used || e->lo >= hi || e->hi < lo)
            continue;
        CacheUnlink(e);
        s_statDrop++;
    }
}

void GX_FifoStats(u32 *hit, u32 *miss, u32 *drop, u32 *abort, u32 *flush) {
    *hit = s_statHit;
    *miss = s_statMiss;
    *drop = s_statDrop;
    *abort = s_statAbort;
    *flush = s_statFlush;
}

/* Miss forensics: first = addresses seen for the first time, rebuild = the
 * bytes were rewritten under a known address, stable = bytes and state both
 * repeated but no entry answered (pool reset or eviction ate it), fmt/arr/mtx
 * = same bytes yet a changed decode input, skip = captures given up on. */
void GX_FifoDiag(u32 *first, u32 *rebuild, u32 *stable, u32 *fmt, u32 *arr, u32 *mtx,
                 u32 *skip) {
    *first = s_dFirst;
    *rebuild = s_dRebuild;
    *stable = s_dStable;
    *fmt = s_dFmt;
    *arr = s_dArr;
    *mtx = s_dMtx;
    *skip = s_statSkip;
}

/* ---- immediate mode ----------------------------------------------------- */

static struct {
    int active, prim, fmt;
    u32 nverts, need, have;
    u8 *buf;
    u32 cap;
} s_imm;

void GX_FifoBegin(int prim, int vtxfmt, u16 nverts) {
    s_imm.active = 1;
    s_imm.prim = prim;
    s_imm.fmt = vtxfmt & 7;
    s_imm.nverts = nverts;
    s_imm.need = (u32)VertexSize(s_imm.fmt) * nverts;
    s_imm.have = 0;
    if (s_imm.need > s_imm.cap) {
        s_imm.cap = s_imm.need + 4096;
        s_imm.buf = SDL_realloc(s_imm.buf, s_imm.cap);
    }
    if (s_imm.need == 0) {
        s_imm.active = 0;
    }
}

/* `bytes` are already big-endian, like the hardware FIFO. */
void GX_FifoWrite(const void *bytes, u32 n) {
    if (!s_imm.active) {
        return;
    }
    if (s_imm.have + n > s_imm.need) {
        n = s_imm.need - s_imm.have;
    }
    memcpy(s_imm.buf + s_imm.have, bytes, n);
    s_imm.have += n;
    if (s_imm.have == s_imm.need) {
        Reader r;
        r.p = s_imm.buf;
        r.end = s_imm.buf + s_imm.need;
        r.bigEndian = 1;
        s_imm.active = 0;
        DrawPrims(&r, s_imm.prim, s_imm.fmt, s_imm.nverts);
    }
}
