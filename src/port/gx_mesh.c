/* Indexed meshes: geometry drawn straight from vertex arrays in host order,
 * not through a display list (gx_fifo.c). Port-only API, gx.h.
 *
 * Every release's pack geometry is drawn this way (Render_Geometry_.c): u16
 * triangle lists or strips per group into one vertex array per LOD, as the
 * PC's OpenGL build fed glDrawElements. Each vertex is built the way the
 * display-list decoder builds one (DecodeVertex): white unless a colour is
 * given, the current position matrix unless the mesh is skinned, normals at
 * the hardware's fixed 1/64 scale. Everything else (texture, TEV, lighting,
 * blending) is the GX state the caller set, as for a display list.
 *
 * Mesh cache. A level's arrays never change after its pack loads, so
 * GX_PortDrawGroup builds them once: the first draw of a level turns all its
 * groups into one triangle list (strips too, so a group is one draw), and
 * the first draw with each vertex setup (the use flags) makes a vertex
 * buffer of GXMeshVertex from it, which stays on the GPU. A group draws a
 * range of it. The matrix slot of an unskinned mesh is not in its vertices
 * but passed per draw, so one buffer serves every object drawn with it. A level is dropped
 * when any of the memory it was built from is freed or rewritten
 * (Port_InvalidateDisplayLists -> GX_MeshInvalidate); its GPU buffers go at
 * the next draw. What that leaves uncovered is the same as for the display
 * list cache: arrays rewritten in place without telling the port.
 * HWVX_MESHVERIFY=1 compares every draw with a fresh build to find out;
 * HWVX_NO_MESHCACHE=1 builds every draw's vertices as before. */
#include "gx_render.h"

/* Scratch vertices for one draw; GX_Draw copies or uploads them. */
static GXVertex *s_verts;
static u32 s_cap;

static GXVertex *Scratch(u32 n) {
    if (n > s_cap) {
        GXVertex *grown = SDL_realloc(s_verts, (n + 1024) * sizeof(GXVertex));

        if (grown == NULL) {
            return NULL;    /* the old buffer stays; this draw is skipped */
        }
        s_verts = grown;
        s_cap = n + 1024;
    }
    return s_verts;
}

/* Matrix slot of vertex `i` in a skinned mesh: its bone, by the per-bone
 * vertex counts (vertices are sorted by bone). Bone k's matrix is loaded
 * into position matrix 3k (Gfx_Anim.c matrixlookup), i.e. slot k. */
static f32 BoneSlot(const GXPortMesh *m, u32 i) {
    u32 k, first = 0;

    for (k = 0; k < m->numBones; k++) {
        first += m->boneCounts[k];
        if (i < first) {
            return (f32)k;
        }
    }
    return (f32)(m->numBones ? m->numBones - 1 : 0);
}

static void BuildVertex(const GXPortMesh *m, u32 i, u32 use, f32 curSlot, GXVertex *v) {
    memset(v, 0, sizeof(*v));
    v->mtxIdx = (use & GX_PORT_SKIN) && m->boneCounts != NULL ? BoneSlot(m, i) : curSlot;
    {
        const f32 *p = (const f32 *)(m->pos + i * m->posStride);
        v->pos[0] = p[0];
        v->pos[1] = p[1];
        v->pos[2] = p[2];
    }
    if ((use & GX_PORT_USE_NRM) && m->nrm != NULL) {
        const f32 *n = (const f32 *)(m->nrm + i * m->nrmStride);
        int k;
        for (k = 0; k < 3; k++) {
            f32 c = n[k] * 64.0f;
            int q = (int)(c >= 0 ? c + 0.5f : c - 0.5f);
            v->nrm[k] = (s8)(q > 127 ? 127 : q < -127 ? -127 : q);
        }
    }
    if ((use & GX_PORT_USE_CLR) && m->clr != NULL) {
        const u8 *c = m->clr + i * m->clrStride;
        v->clr[0][0] = c[0];
        v->clr[0][1] = c[1];
        v->clr[0][2] = c[2];
        v->clr[0][3] = (use & GX_PORT_CLR_ALPHA) ? c[3] : 255;
    } else {
        v->clr[0][0] = v->clr[0][1] = v->clr[0][2] = v->clr[0][3] = 255;
    }
    if ((use & GX_PORT_USE_UV) && m->uv != NULL) {
        const f32 *t = (const f32 *)(m->uv + i * m->uvStride);
        v->tex[0][0] = t[0];
        v->tex[0][1] = t[1];
    }
}

/* The current position matrix's slot, as DecodeVertex gives a vertex. */
static f32 CurSlot(void) {
    return (f32)(g_gx.curMtx / 3 < GX_NUM_POSMTX ? g_gx.curMtx / 3 : GX_POSMTX_IDENTITY);
}

/* Draws with the cull mode GX_PORT_NOCULL asks for. */
static void DrawCulled(u32 use, int prim, const GXVertex *v, u32 n, const GXMeshDraw *mesh) {
    u8 cull = g_gx.cullMode;

    if (use & GX_PORT_NOCULL) {
        g_gx.cullMode = 0;      /* GX_CULL_NONE */
    }
    if (mesh != NULL) {
        GX_DrawMesh(mesh);
    } else {
        GX_Draw(prim, v, n);
    }
    g_gx.cullMode = cull;
}

void GX_PortDrawIndexed(const GXPortMesh *m, int prim, const u16 *idx, u32 count, u32 use) {
    f32 curSlot = CurSlot();
    GXVertex *v;
    u32 i;

    if (m == NULL || m->pos == NULL || idx == NULL || count == 0) {
        return;
    }
    v = Scratch(count);
    if (v == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        BuildVertex(m, idx[i], use, curSlot, &v[i]);
    }
    DrawCulled(use, prim, v, count, NULL);
}

/* A group without the cache: a draw per triangle list or strip. */
static void DrawGroupUncached(const GXPortMesh *m, u32 group, u32 use) {
    u32 count = 0;
    const u16 *idx = m->groupIdx(m->level, group, &count);

    if (idx == NULL) {
        return;
    }
    if (!(idx[0] & 0x8000)) {
        GX_PortDrawIndexed(m, GX_TRIANGLES, idx, count, use);
        return;
    }
    for (;;) {
        u32 n = idx[0] & 0x7FFF;

        if (idx[0] == 0xFFFF) {
            break;
        }
        GX_PortDrawIndexed(m, GX_TRIANGLESTRIP, idx + 1, n, use);
        idx += 1 + n;
    }
}

/* ===========================================================================
 * Mesh cache
 * ========================================================================= */

#define MESH_BUCKETS 4096u            /* power of two */
#define MESH_VARIANTS 8               /* 4 vertex setups, skinned or not */
#define MESH_USE (GX_PORT_USE_NRM | GX_PORT_USE_CLR | GX_PORT_CLR_ALPHA | GX_PORT_USE_UV | GX_PORT_SKIN)

typedef struct MeshGroup {
    const u16 *src;           /* the group's index list as drawn from */
    u32 srcCount;             /* its count (triangle lists) */
    u32 first, count;         /* its triangles in the level's index buffer */
} MeshGroup;

typedef struct MeshVariant {
    u32 use;                  /* MESH_USE bits it was built for */
    void *vtx;                /* meshVertices */
    GXMeshVertex *copy;       /* HWVX_MESHVERIFY: what was uploaded */
} MeshVariant;

/* Memory a level was built from: [lo, hi) each. */
enum { RANGE_LEVEL, RANGE_POS, RANGE_NRM, RANGE_CLR, RANGE_UV, RANGE_BONES, RANGE_IDX, NUM_RANGES };

typedef struct MeshLevel {
    struct MeshLevel *next;   /* hash chain, or the free list once dead */
    const void *level;
    GXPortMesh mesh;          /* the arrays as built (callbacks and level too) */
    u32 numGroups, numVerts, numIdx;
    u32 bufVerts;             /* vertices per vertex buffer: numIdx, or numVerts shared */
    MeshGroup *groups;
    u16 *order;               /* the groups' triangles, numIdx indices */
    void *idx;                /* meshIndices of order (HWVX_MESHSHARED), else NULL */
    MeshVariant var[MESH_VARIANTS];
    u32 numVar;
    u64 lo[NUM_RANGES], hi[NUM_RANGES];
} MeshLevel;

static MeshLevel *s_bucket[MESH_BUCKETS];
static MeshLevel *s_dead;             /* invalidated, GPU buffers not yet freed */
static u32 s_numLevels, s_numVariants, s_built;
static u64 s_vtxBytes, s_idxBytes;
static u64 s_spanLo = ~(u64)0, s_spanHi;  /* covers every live level's ranges */
static int s_spanDirty;
static u64 s_drawTicks;              /* in GX_PortDrawGroup, building and submitting */

/* -1 unchecked; then 0 off (HWVX_NO_MESHCACHE), 1 on, 2 on and verifying. */
static int s_mode = -1;
/* HWVX_MESHSHARED=1: a level's vertex buffers hold each vertex once and
 * its triangles index them. By default they hold the triangles' vertices
 * in order, three per triangle, drawn without indices: the same vertices
 * the per-draw build sent, and on the GPUs measured (Apple M-series, both
 * renderers) only that gives the same pixels. With shared vertices the GPU
 * shades a vertex once for all its triangles, and edge pixels where passes
 * meet at equal depth come out differently (docs: HWVX-SDL README). */
static int s_shared;

static int Mode(void) {
    if (s_mode < 0) {
        const char *off = SDL_getenv("HWVX_NO_MESHCACHE"), *verify = SDL_getenv("HWVX_MESHVERIFY");
        const char *shared = SDL_getenv("HWVX_MESHSHARED");

        s_mode = (off != NULL && *off == '1') ? 0 : (verify != NULL && *verify == '1') ? 2 : 1;
        if (g_gxr->meshIndices == NULL) {
            s_mode = 0;
        }
        s_shared = shared != NULL && *shared == '1';
        Port_Log("mesh cache: %s%s", s_mode == 0 ? "off" : s_mode == 2 ? "on, verifying every draw" : "on",
                 s_mode != 0 && s_shared ? ", shared vertices" : "");
    }
    return s_mode;
}

static u32 Hash(const void *p) {
    return (u32)(((uintptr_t)p >> 4) * 2654435761u) & (MESH_BUCKETS - 1);
}

static void FreeLevel(MeshLevel *l) {
    u32 i;

    for (i = 0; i < l->numVar; i++) {
        if (l->var[i].vtx != NULL) {
            g_gxr->meshDestroy(l->var[i].vtx);
            s_vtxBytes -= (u64)l->bufVerts * sizeof(GXMeshVertex);
        }
        SDL_free(l->var[i].copy);
    }
    s_numVariants -= l->numVar;
    if (l->idx != NULL) {
        g_gxr->meshDestroy(l->idx);
        s_idxBytes -= (u64)l->numIdx * sizeof(u16);
    }
    SDL_free(l->order);
    SDL_free(l->groups);
    SDL_free(l);
}

/* Unlinks a live level; its buffers are freed by GX_MeshCollect. */
static void Kill(MeshLevel *l) {
    MeshLevel **pp = &s_bucket[Hash(l->level)];

    while (*pp != NULL && *pp != l) {
        pp = &(*pp)->next;
    }
    if (*pp == l) {
        *pp = l->next;
    }
    l->next = s_dead;
    s_dead = l;
    s_numLevels--;
    s_spanDirty = 1;
}

void GX_MeshCollect(void) {
    while (s_dead != NULL) {
        MeshLevel *l = s_dead;

        s_dead = l->next;
        FreeLevel(l);
    }
}

static void SpanRefresh(void) {
    u32 b, r;
    MeshLevel *l;

    if (!s_spanDirty) {
        return;
    }
    s_spanLo = ~(u64)0;
    s_spanHi = 0;
    for (b = 0; b < MESH_BUCKETS; b++) {
        for (l = s_bucket[b]; l != NULL; l = l->next) {
            for (r = 0; r < NUM_RANGES; r++) {
                if (l->hi[r] > l->lo[r]) {
                    s_spanLo = SDL_min(s_spanLo, l->lo[r]);
                    s_spanHi = SDL_max(s_spanHi, l->hi[r]);
                }
            }
        }
    }
    s_spanDirty = 0;
}

static void MeshInvalidate(const void *p, u32 size);

/* [p, p+size) is being freed or rewritten: drop every level built from it.
 * HWVX_LOADTIME=1 logs the time it takes, as the levels go. */
void GX_MeshInvalidate(const void *p, u32 size) {
    static int timing = -1;
    static u64 total, calls, lastLog;
    u32 before;
    u64 start, now;

    if (timing < 0) {
        timing = SDL_getenv("HWVX_LOADTIME") != NULL;
    }
    if (!timing) {
        MeshInvalidate(p, size);
        return;
    }
    before = s_numLevels;
    start = SDL_GetTicksNS();
    MeshInvalidate(p, size);
    now = SDL_GetTicksNS();
    total += now - start;
    calls++;
    if (s_numLevels != before && now - lastLog > 100000000u) {
        lastLog = now;
        Port_Log("loadtime: mesh invalidate %llu calls %.3f ms, %u levels left", (unsigned long long)calls,
                 total / 1e6, s_numLevels);
    }
}

static void MeshInvalidate(const void *p, u32 size) {
    u64 lo = (u64)(uintptr_t)p, hi = lo + size;
    u32 b, r;

    if (s_numLevels == 0 || size == 0) {
        return;
    }
    SpanRefresh();
    if (hi <= s_spanLo || lo >= s_spanHi) {
        return;
    }
    for (b = 0; b < MESH_BUCKETS; b++) {
        MeshLevel *l = s_bucket[b], *next;

        for (; l != NULL; l = next) {
            next = l->next;
            for (r = 0; r < NUM_RANGES; r++) {
                if (l->hi[r] > l->lo[r] && l->lo[r] < hi && lo < l->hi[r]) {
                    Kill(l);
                    break;
                }
            }
        }
    }
}

void GX_MeshStats(u32 *levels, u32 *variants, u32 *built, u64 *vtxBytes, u64 *idxBytes, u64 *ticks) {
    *ticks = s_drawTicks;
    s_drawTicks = 0;
    *levels = s_numLevels;
    *variants = s_numVariants;
    *built = s_built;
    *vtxBytes = s_vtxBytes;
    *idxBytes = s_idxBytes;
}

/* A group's index list as a triangle list: appended to out (when not NULL),
 * returns the index count. Strips become one triangle each per step, the odd
 * ones in GL's order (i+1, i, i+2), so the faces keep their winding; a
 * triangle that repeats an index covers no pixels and is left out. */
static u32 GroupTriangles(const u16 *idx, u32 count, u16 *out) {
    u32 n = 0, k;

    if (idx == NULL) {
        return 0;
    }
    if (!(idx[0] & 0x8000)) {
        count -= count % 3;
        if (out != NULL) {
            memcpy(out, idx, count * sizeof(u16));
        }
        return count;
    }
    for (;;) {
        u32 len = idx[0] & 0x7FFF;

        if (idx[0] == 0xFFFF) {
            break;
        }
        for (k = 0; k + 2 < len; k++) {
            u16 a = idx[1 + k], b = idx[2 + k], c = idx[3 + k];

            if (a == b || b == c || a == c) {
                continue;
            }
            if (out != NULL) {
                out[n] = (k & 1) ? b : a;
                out[n + 1] = (k & 1) ? a : b;
                out[n + 2] = c;
            }
            n += 3;
        }
        idx += 1 + len;
    }
    return n;
}

/* The bytes of a strip-record list, terminator included. */
static u32 StripBytes(const u16 *idx) {
    const u16 *p = idx;

    while (p[0] != 0xFFFF) {
        p += 1 + (p[0] & 0x7FFF);
    }
    return (u32)((p + 1 - idx) * sizeof(u16));
}

static void SetRange(MeshLevel *l, int r, const void *p, u64 size) {
    if (p != NULL && size != 0) {
        l->lo[r] = (u64)(uintptr_t)p;
        l->hi[r] = l->lo[r] + size;
    }
}

/* The level's groups and index buffer. NULL on failure (drawn uncached). */
static MeshLevel *BuildLevel(const GXPortMesh *m) {
    MeshLevel *l = SDL_calloc(1, sizeof(MeshLevel));
    u16 *all = NULL;
    u32 g, n, maxIdx = 0;
    u64 idxLo = ~(u64)0, idxHi = 0;

    if (l == NULL) {
        return NULL;
    }
    l->level = m->level;
    l->mesh = *m;
    l->numGroups = m->numGroups;
    l->groups = SDL_calloc(SDL_max(m->numGroups, 1), sizeof(MeshGroup));
    if (l->groups == NULL) {
        SDL_free(l);
        return NULL;
    }
    for (g = 0; g < l->numGroups; g++) {
        MeshGroup *mg = &l->groups[g];

        mg->src = m->groupIdx(m->level, g, &mg->srcCount);
        mg->first = l->numIdx;
        mg->count = GroupTriangles(mg->src, mg->srcCount, NULL);
        l->numIdx += mg->count;
        if (mg->src != NULL) {
            u64 a = (u64)(uintptr_t)mg->src;
            u64 b = a + ((mg->src[0] & 0x8000) ? StripBytes(mg->src) : mg->srcCount * sizeof(u16));

            idxLo = SDL_min(idxLo, a);
            idxHi = SDL_max(idxHi, b);
        }
    }
    if (l->numIdx != 0) {
        all = SDL_malloc(l->numIdx * sizeof(u16));
        if (all == NULL) {
            SDL_free(l->groups);
            SDL_free(l);
            return NULL;
        }
        for (g = 0; g < l->numGroups; g++) {
            GroupTriangles(l->groups[g].src, l->groups[g].srcCount, all + l->groups[g].first);
        }
        for (n = 0; n < l->numIdx; n++) {
            maxIdx = SDL_max(maxIdx, all[n]);
        }
        l->numVerts = maxIdx + 1;
        l->bufVerts = l->numIdx;
        if (s_shared) {
            l->bufVerts = l->numVerts;
            l->idx = g_gxr->meshIndices(all, l->numIdx);
            if (l->idx == NULL) {
                SDL_free(all);
                SDL_free(l->groups);
                SDL_free(l);
                return NULL;
            }
            s_idxBytes += (u64)l->numIdx * sizeof(u16);
        }
    }
    l->order = all;
    SetRange(l, RANGE_LEVEL, m->level, 1);
    SetRange(l, RANGE_POS, m->pos, (u64)l->numVerts * m->posStride);
    SetRange(l, RANGE_NRM, m->nrm, (u64)l->numVerts * m->nrmStride);
    SetRange(l, RANGE_CLR, m->clr, (u64)l->numVerts * m->clrStride);
    SetRange(l, RANGE_UV, m->uv, (u64)l->numVerts * m->uvStride);
    SetRange(l, RANGE_BONES, m->boneCounts, (u64)m->numBones * sizeof(u32));
    if (idxHi > idxLo) {
        l->lo[RANGE_IDX] = idxLo;
        l->hi[RANGE_IDX] = idxHi;
    }
    {
        u32 h = Hash(l->level);

        l->next = s_bucket[h];
        s_bucket[h] = l;
    }
    s_numLevels++;
    s_spanDirty = 1;
    return l;
}

/* Vertex i of a mesh for these use flags, as BuildVertex makes it. */
static void MeshVertex(const GXPortMesh *m, u32 i, u32 use, GXMeshVertex *out) {
    GXVertex v;

    BuildVertex(m, i, use, 0.0f, &v);
    memcpy(out->pos, v.pos, sizeof(out->pos));
    memcpy(out->nrm, v.nrm, sizeof(out->nrm));
    memcpy(out->clr, v.clr[0], sizeof(out->clr));
    memcpy(out->tex, v.tex[0], sizeof(out->tex));
    out->mtxIdx = v.mtxIdx;
}

static MeshVariant *Variant(MeshLevel *l, u32 use) {
    MeshVariant *mv;
    GXMeshVertex *v;
    u32 i;

    for (i = 0; i < l->numVar; i++) {
        if (l->var[i].use == use) {
            return &l->var[i];
        }
    }
    if (l->numVar == MESH_VARIANTS) {
        return NULL;
    }
    v = SDL_malloc((size_t)l->bufVerts * sizeof(GXMeshVertex));
    if (v == NULL) {
        return NULL;
    }
    if (l->idx != NULL) {
        for (i = 0; i < l->numVerts; i++) {
            MeshVertex(&l->mesh, i, use, &v[i]);
        }
    } else {
        for (i = 0; i < l->numIdx; i++) {
            MeshVertex(&l->mesh, l->order[i], use, &v[i]);
        }
    }
    mv = &l->var[l->numVar];
    mv->vtx = g_gxr->meshVertices(v, l->bufVerts, (use & GX_PORT_SKIN) != 0, l->idx);
    if (mv->vtx == NULL) {
        SDL_free(v);
        return NULL;
    }
    mv->use = use;
    if (Mode() == 2) {
        mv->copy = v;
    } else {
        SDL_free(v);
    }
    l->numVar++;
    s_numVariants++;
    s_built++;
    s_vtxBytes += (u64)l->bufVerts * sizeof(GXMeshVertex);
    return mv;
}

/* The cached level for these arrays, if it was built from the same ones. */
static MeshLevel *Find(const GXPortMesh *m) {
    MeshLevel *l;

    for (l = s_bucket[Hash(m->level)]; l != NULL; l = l->next) {
        if (l->level == m->level) {
            const GXPortMesh *b = &l->mesh;

            if (b->pos != m->pos || b->nrm != m->nrm || b->clr != m->clr || b->uv != m->uv ||
                b->posStride != m->posStride || b->nrmStride != m->nrmStride ||
                b->clrStride != m->clrStride || b->uvStride != m->uvStride ||
                b->boneCounts != m->boneCounts || b->numBones != m->numBones ||
                b->numGroups != m->numGroups || b->groupIdx != m->groupIdx) {
                Kill(l);
                return NULL;
            }
            return l;
        }
    }
    return NULL;
}

/* HWVX_MESHVERIFY: the draw's indices and vertices against a fresh build. */
static void Verify(const MeshLevel *l, const MeshGroup *mg, const MeshVariant *mv, const GXPortMesh *m,
                   u32 use) {
    static u32 reported;
    u16 *idx = SDL_malloc((mg->count + 1) * sizeof(u16));
    u32 i, bad = 0;

    if (idx == NULL) {
        return;
    }
    if (GroupTriangles(mg->src, mg->srcCount, idx) != mg->count ||
        memcmp(idx, l->order + mg->first, mg->count * sizeof(u16)) != 0) {
        bad = 1;
    }
    for (i = 0; i < mg->count && !bad; i++) {
        GXMeshVertex v;

        MeshVertex(m, idx[i], use, &v);
        if (idx[i] >= l->numVerts) {
            bad = 1;
        } else if (memcmp(&v, &mv->copy[l->idx != NULL ? idx[i] : mg->first + i], sizeof(v)) != 0) {
            bad = 2;
        }
    }
    if (bad && reported < 20) {
        reported++;
        Port_Log("HWVX_MESHVERIFY: level %p group %u use %#x: %s differ from the cached build", l->level,
                 (unsigned)(mg - l->groups), use, bad == 1 ? "indices" : "vertices");
    }
    SDL_free(idx);
}

static void DrawGroup(const GXPortMesh *m, u32 group, u32 use);

void GX_PortDrawGroup(const GXPortMesh *m, u32 group, u32 use) {
    u64 t0 = SDL_GetPerformanceCounter();

    DrawGroup(m, group, use);
    s_drawTicks += SDL_GetPerformanceCounter() - t0;
}

static void DrawGroup(const GXPortMesh *m, u32 group, u32 use) {
    MeshLevel *l;
    MeshGroup *mg;
    MeshVariant *mv;
    GXMeshDraw d;
    u32 count = 0, key;
    const u16 *src;

    if (m == NULL || m->pos == NULL || m->groupIdx == NULL || group >= m->numGroups) {
        return;
    }
    if (Mode() == 0 || m->level == NULL) {
        DrawGroupUncached(m, group, use);
        return;
    }
    GX_MeshCollect();
    key = use & MESH_USE;
    if (m->boneCounts == NULL) {
        key &= ~(u32)GX_PORT_SKIN;   /* BuildVertex: no counts, the current matrix */
    }
    l = Find(m);
    src = m->groupIdx(m->level, group, &count);
    if (l != NULL && (l->groups[group].src != src ||
                      (src != NULL && !(src[0] & 0x8000) && l->groups[group].srcCount != count))) {
        Kill(l);         /* the group points elsewhere now: rebuild from what it says */
        l = NULL;
    }
    if (l == NULL) {
        l = BuildLevel(m);
        if (l == NULL) {
            DrawGroupUncached(m, group, use);
            return;
        }
    }
    mg = &l->groups[group];
    if (mg->count == 0) {
        return;
    }
    mv = Variant(l, key);
    if (mv == NULL) {
        DrawGroupUncached(m, group, use);
        return;
    }
    if (Mode() == 2) {
        Verify(l, mg, mv, m, key);
    }
    d.vtx = mv->vtx;
    d.idx = l->idx;
    d.first = mg->first;
    d.count = mg->count;
    d.slot = CurSlot();
    DrawCulled(use, GX_TRIANGLES, NULL, 0, &d);
}
