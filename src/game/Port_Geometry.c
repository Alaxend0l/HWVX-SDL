/* The GameCube's geometry turned into the indexed layout the PC release
 * keeps, as a pack loads (the PS2's too: PortGeometry_ConvertPS2, below).
 * Port-only file.
 *
 * The port draws one geometry layout, the PC's (Render_Geometry_.c,
 * PORT_LAYOUT_INDEXED): per group a u16 triangle list, per LOD level one
 * array per attribute (positions and normals x,y,z + pad, stride 16; RGBA8
 * colours; f32 s,t). A GameCube level instead has GX display lists of
 * triangles, strips or fans whose vertices are three u16 indices (A, B, C)
 * into packed arrays (positions and normals stride 12, UVs 8, colours 4)
 * behind a header of offsets {uv, colours or 0, normals or 0, end}.
 *
 * What A, B and C are depends on the group, as the PC exporter shows
 * (docs/beta-cars/research/pc_formats.md, HWVX-X-FILE-EDITOR): a lit or
 * environment-mapped group (flags & 0x16, not alpha 0x81) is position A,
 * normal B, UV C, colour white; any other group is position A, colour B,
 * UV C. Each distinct (A, B, C, kind) of a level becomes one vertex, in
 * first-use order, like the PC's.
 *
 * The converted arrays and lists go in one block from the game heap, which
 * the pack keeps (DataPack.soundData; a quadrant's here, PortGeometry_Keep)
 * and frees with itself; the level's vertex buffer (0xC) and its groups' lists (0x1C,
 * 0x10 the triangle count) are repointed and the level marked. The
 * GameCube's own arrays and lists stay in the pack, unused.
 *
 * Skinned geometry (header 0x4 nonzero, drawn by Gfx_Anim.c with one GX
 * position matrix per bone): the vertices of its lit and env-mapped groups,
 * the ones drawn with the matrix index in their vertex setup, start with
 * that u8 index (bone = index / 3). Its vertices are sorted by bone, as the
 * PC keeps them, and the level gets a table of eight per-bone vertex counts
 * at 0x10 (the PC's: four counts and four zero words).
 *
 * Left as they are: shadow volumes (groups flagged 0x4000, read by
 * Render_DrawShadows as they are). A level whose lists hold something other
 * than the three primitives, an index out of range, a bone past 8, or more
 * than 0x7FFF vertices would not convert, and would not be drawn (the PC's index lists mark strip
 * records with bit 15). docs/port-format/scripts/geom_check.py
 * (HWVX-X-FILE-EDITOR) applies these rules to a whole extracted tree: every
 * level of the GameCube release converts, so the GX path for pack geometry
 * is gone (Render_DrawGroupPrims). */
#include "common.h"
#include "Gfx.h"
#include "Port_Swap.h"

#define GEOMETRY_GC_HEADER 0x2003
#define MAX_LEVEL_VERTS 0x7FFF
#define MAX_BONES 8

/* HWVX_GEOMLOG=1: per blob, what was found and why levels stay GX. */
static struct {
    u32 geoms, skinned, shadow, levels, failed;
    const char *why;
} s_stat;

/* A level's converted data, before it is copied into the pack's block. */
typedef struct LevelOut {
    Geometry *lod;
    u32 numVerts;
    f32 *pos;               /* x,y,z,0 */
    f32 *nrm;               /* x,y,z,0, or NULL */
    u8 *clr;                /* RGBA, or NULL */
    f32 *uv;                /* s,t */
    u32 numGroups;
    u16 **tris;             /* per group, 3 per triangle */
    u32 *numTris;
    int skinned;
    u16 layout;             /* the mark PlaceLevel gives the level (0: PORT_LAYOUT_INDEXED) */
    u8 *bone;               /* skinned: each vertex's bone */
    u32 boneCounts[MAX_BONES];
    struct LevelOut *next;
} LevelOut;

typedef struct Arrays {
    const u8 *pos, *uv, *clr, *nrm;
    u32 numPos, numUV, numClr, numNrm;
} Arrays;

static u16 RdBE16(const u8 *p) {
    return (u16)(p[0] << 8 | p[1]);
}

static int InBlob(const u8 *base, u32 size, const void *p, u32 n) {
    return (const u8 *)p >= base && (const u8 *)p + n <= base + size;
}

static void FreeLevel(LevelOut *l) {
    u32 i;

    if (l == NULL) {
        return;
    }
    for (i = 0; l->tris != NULL && i < l->numGroups; i++) {
        free(l->tris[i]);
    }
    free(l->tris);
    free(l->numTris);
    free(l->pos);
    free(l->nrm);
    free(l->clr);
    free(l->uv);
    free(l->bone);
    free(l);
}

/* The level's packed arrays, from its vertex buffer header. */
static int ReadArrays(const u8 *base, u32 size, const u32 *vb, Arrays *a) {
    const u8 *data = (const u8 *)(vb + 4);
    u32 off[3] = { vb[0], vb[1], vb[2] }, end = vb[3], i;
    u32 bound[3];

    if (!InBlob(base, size, vb, 0x10) || !InBlob(base, size, data, end) || off[0] > end) {
        return 0;
    }
    /* each array runs to the next one that starts after it, or the end */
    for (i = 0; i < 3; i++) {
        u32 j;
        bound[i] = end;
        for (j = 0; j < 3; j++) {
            if (off[j] > off[i] && off[j] < bound[i] && (j == 0 || off[j] != 0)) {
                bound[i] = off[j];
            }
        }
    }
    a->pos = data;
    a->numPos = off[0] / 12;
    a->uv = data + off[0];
    a->numUV = (bound[0] - off[0]) / 8;
    a->clr = off[1] ? data + off[1] : NULL;
    a->numClr = off[1] ? (bound[1] - off[1]) / 4 : 0;
    a->nrm = off[2] ? data + off[2] : NULL;
    a->numNrm = off[2] ? (bound[2] - off[2]) / 12 : 0;
    return 1;
}

/* Open-addressed map from a vertex key to its new index. */
typedef struct VertMap {
    u64 *keys;
    u16 *vals;
    u32 cap;
} VertMap;

static u32 Lookup(VertMap *m, u64 key, u32 *count, int *isNew) {
    u32 h = (u32)((key * 0x9E3779B97F4A7C15ull) >> 40) & (m->cap - 1);

    while (m->keys[h] != 0) {
        if (m->keys[h] == key) {
            *isNew = 0;
            return m->vals[h];
        }
        h = (h + 1) & (m->cap - 1);
    }
    m->keys[h] = key;
    m->vals[h] = (u16)*count;
    *isNew = 1;
    return (*count)++;
}

/* Convert one level; NULL if it has to stay a display-list level. */
static int SortByBone(LevelOut *l);

static LevelOut *ConvertLevel(const u8 *base, u32 size, Geometry *lod, int skinned) {
    GeometryGroup *groups = GETP(lod->groups);
    const u32 *vb = (const u32 *)GETP(lod->lodDistances);
    Arrays a;
    VertMap map;
    LevelOut *out;
    u32 cap = 0, g, i, maxVerts = 0;
    int ok = 1;

    if (!InBlob(base, size, groups, lod->numGroups * sizeof(GeometryGroup)) || !ReadArrays(base, size, vb, &a)) {
        s_stat.why = "groups or vertex buffer outside the blob";
        return NULL;
    }
    for (g = 0; g < lod->numGroups; g++) {
        if (!InBlob(base, size, GETP(groups[g].displayList), groups[g].dlSize)) {
            s_stat.why = "a display list outside the blob";
            return NULL;
        }
        maxVerts += groups[g].dlSize / 6 + 1;
    }
    out = calloc(1, sizeof(LevelOut));
    if (out == NULL) {
        return NULL;
    }
    out->lod = lod;
    out->skinned = skinned;
    out->numGroups = lod->numGroups;
    out->tris = calloc(lod->numGroups, sizeof(u16 *));
    out->numTris = calloc(lod->numGroups, sizeof(u32));
    if (maxVerts > MAX_LEVEL_VERTS * 4) {
        maxVerts = MAX_LEVEL_VERTS * 4;
    }
    for (map.cap = 64; map.cap < maxVerts * 2; map.cap <<= 1) {
    }
    map.keys = calloc(map.cap, sizeof(u64));
    map.vals = calloc(map.cap, sizeof(u16));
    out->pos = malloc(MAX_LEVEL_VERTS * 16);
    out->uv = malloc(MAX_LEVEL_VERTS * 8);
    out->nrm = a.nrm != NULL ? calloc(MAX_LEVEL_VERTS, 16) : NULL;
    out->clr = a.clr != NULL ? malloc(MAX_LEVEL_VERTS * 4) : NULL;
    out->bone = skinned ? calloc(MAX_LEVEL_VERTS, 1) : NULL;
    if (out->tris == NULL || out->numTris == NULL || map.keys == NULL || map.vals == NULL || out->pos == NULL ||
        out->uv == NULL || (a.nrm != NULL && out->nrm == NULL) || (a.clr != NULL && out->clr == NULL) ||
        (skinned && out->bone == NULL)) {
        ok = 0;
    }
    for (g = 0; ok && g < lod->numGroups; g++) {
        const u8 *dl = GETP(groups[g].displayList), *end = dl + groups[g].dlSize;
        u16 flags = groups[g].flags;
        int lit = (flags & 0x16) && !(flags & 0x81);
        int mtx = skinned && lit;    /* drawn with the matrix index (GX_VA_PNMTXIDX) */
        u32 stride = mtx ? 7 : 6;
        u32 n = 0;

        (void)cap;
        out->tris[g] = malloc((groups[g].dlSize / 6 + 1) * 3 * sizeof(u16));
        if (out->tris[g] == NULL) {
            ok = 0;
            break;
        }
        while (ok && dl < end) {
            u8 cmd = *dl++;
            u32 count, prim, v;
            u16 idx[0x10000 / 4];

            if (cmd == 0) {
                continue;                   /* padding */
            }
            prim = cmd & 0xF8;
            if ((prim != 0x90 && prim != 0x98 && prim != 0xA0) || dl + 2 > end) {
                s_stat.why = "a display-list command other than triangles, strips, fans";
                ok = 0;
                break;
            }
            count = RdBE16(dl);
            dl += 2;
            if (dl + count * stride > end || count > sizeof(idx) / sizeof(idx[0])) {
                ok = 0;
                break;
            }
            for (v = 0; v < count; v++, dl += stride) {
                u32 bone = mtx ? dl[0] / 3 : 0;
                const u8 *ix = mtx ? dl + 1 : dl;
                u32 A = RdBE16(ix), B = RdBE16(ix + 2), C = RdBE16(ix + 4);
                u64 key = 1ull << 63 | (u64)lit << 56 | (u64)bone << 48 | (u64)A << 32 | (u64)B << 16 | C;
                int isNew;
                u32 k;

                if (bone >= MAX_BONES) {
                    s_stat.why = "a bone past 8";
                    ok = 0;
                    break;
                }

                if (A >= a.numPos || C >= a.numUV || (lit ? (a.nrm != NULL && B >= a.numNrm)
                                                          : (a.clr != NULL && B >= a.numClr))) {
                    s_stat.why = "an index out of its array";
                    ok = 0;
                    break;
                }
                k = Lookup(&map, key, &out->numVerts, &isNew);
                if (out->numVerts > MAX_LEVEL_VERTS) {
                    s_stat.why = "more than 0x7FFF vertices";
                    ok = 0;
                    break;
                }
                if (isNew) {
                    if (out->bone != NULL) {
                        out->bone[k] = (u8)bone;
                    }
                    memcpy(&out->pos[k * 4], a.pos + A * 12, 12);
                    out->pos[k * 4 + 3] = 0.0f;
                    memcpy(&out->uv[k * 2], a.uv + C * 8, 8);
                    if (out->nrm != NULL && lit) {
                        memcpy(&out->nrm[k * 4], a.nrm + B * 12, 12);
                    }
                    if (out->clr != NULL) {
                        if (lit) {
                            memset(&out->clr[k * 4], 0xFF, 4);
                        } else {
                            memcpy(&out->clr[k * 4], a.clr + B * 4, 4);
                        }
                    }
                }
                idx[v] = (u16)k;
            }
            if (!ok) {
                break;
            }
            /* to a triangle list, keeping each triangle's winding */
            for (v = 0; prim == 0x90 && v + 2 < count; v += 3) {
                out->tris[g][n++] = idx[v];
                out->tris[g][n++] = idx[v + 1];
                out->tris[g][n++] = idx[v + 2];
            }
            for (v = 2; prim == 0x98 && v < count; v++) {
                int odd = v & 1;
                out->tris[g][n++] = idx[v - 2 + odd];
                out->tris[g][n++] = idx[v - 1 - odd];
                out->tris[g][n++] = idx[v];
            }
            for (v = 2; prim == 0xA0 && v < count; v++) {
                out->tris[g][n++] = idx[0];
                out->tris[g][n++] = idx[v - 1];
                out->tris[g][n++] = idx[v];
            }
        }
        out->numTris[g] = n / 3;
    }
    free(map.keys);
    free(map.vals);
    if (ok && skinned && !SortByBone(out)) {
        s_stat.why = "out of memory sorting by bone";
        ok = 0;
    }
    if (!ok || out->numVerts == 0) {
        FreeLevel(out);
        return NULL;
    }
    return out;
}

/* Skinned: vertices sorted by bone (stable), the lists renumbered, the
 * per-bone counts noted, as the PC keeps a skinned level. */
static int SortByBone(LevelOut *l) {
    u32 first[MAX_BONES], i, g, n = l->numVerts;
    u16 *remap = malloc(n * sizeof(u16));
    f32 *pos = malloc(n * 16), *uv = malloc(n * 8), *nrm = l->nrm ? malloc(n * 16) : NULL;
    u8 *clr = l->clr ? malloc(n * 4) : NULL;

    if (remap == NULL || pos == NULL || uv == NULL || (l->nrm && nrm == NULL) || (l->clr && clr == NULL)) {
        free(remap);
        free(pos);
        free(uv);
        free(nrm);
        free(clr);
        return 0;
    }
    memset(l->boneCounts, 0, sizeof(l->boneCounts));
    for (i = 0; i < n; i++) {
        l->boneCounts[l->bone[i]]++;
    }
    for (i = 0, first[0] = 0; i + 1 < MAX_BONES; i++) {
        first[i + 1] = first[i] + l->boneCounts[i];
    }
    for (i = 0; i < n; i++) {
        u32 k = first[l->bone[i]]++;
        remap[i] = (u16)k;
        memcpy(&pos[k * 4], &l->pos[i * 4], 16);
        memcpy(&uv[k * 2], &l->uv[i * 2], 8);
        if (nrm != NULL) {
            memcpy(&nrm[k * 4], &l->nrm[i * 4], 16);
        }
        if (clr != NULL) {
            memcpy(&clr[k * 4], &l->clr[i * 4], 4);
        }
    }
    for (g = 0; g < l->numGroups; g++) {
        for (i = 0; i < l->numTris[g] * 3; i++) {
            l->tris[g][i] = remap[l->tris[g][i]];
        }
    }
    free(l->pos);
    free(l->uv);
    free(l->nrm);
    free(l->clr);
    l->pos = pos;
    l->uv = uv;
    l->nrm = nrm;
    l->clr = clr;
    free(remap);
    return 1;
}

static u32 Align16(u32 n) {
    return (n + 15) & ~15u;
}

/* The bytes a level takes in the block: vertex buffer header, arrays, lists. */
static u32 LevelSize(const LevelOut *l) {
    u32 size = 0x20 + Align16(l->numVerts * 16) + Align16(l->numVerts * 8), g;

    size += l->nrm != NULL ? Align16(l->numVerts * 16) : 0;
    size += l->clr != NULL ? Align16(l->numVerts * 4) : 0;
    size += l->skinned ? MAX_BONES * 4 : 0;
    for (g = 0; g < l->numGroups; g++) {
        size += Align16(l->numTris[g] * 6 + 2);
    }
    return size;
}

/* Copy a level into the block at p and repoint it; returns the next free byte. */
static u8 *PlaceLevel(LevelOut *l, u8 *p) {
    GeometryGroup *groups = GETP(l->lod->groups);
    u32 *vb = (u32 *)p;
    u8 *q = p + 0x20, *arr[4] = { NULL, NULL, NULL, NULL };
    u32 g;

    memset(p, 0, 0x20);
    arr[0] = q;
    memcpy(q, l->pos, l->numVerts * 16);
    q += Align16(l->numVerts * 16);
    if (l->clr != NULL) {
        arr[1] = q;
        memcpy(q, l->clr, l->numVerts * 4);
        q += Align16(l->numVerts * 4);
    }
    arr[2] = q;
    memcpy(q, l->uv, l->numVerts * 8);
    q += Align16(l->numVerts * 8);
    if (l->nrm != NULL) {
        arr[3] = q;
        memcpy(q, l->nrm, l->numVerts * 16);
        q += Align16(l->numVerts * 16);
    }
    for (g = 0; g < 4; g++) {
        SLOT_STORE(&vb[g], arr[g]);
    }
    for (g = 0; g < l->numGroups; g++) {
        u16 *tris = (u16 *)q;

        memcpy(tris, l->tris[g], l->numTris[g] * 6);
        tris[l->numTris[g] * 3] = 0;
        SETP(groups[g].displayList, (void *)tris);
        groups[g].dlSize = l->numTris[g];
        q += Align16(l->numTris[g] * 6 + 2);
    }
    if (l->skinned) {
        /* the per-bone counts at the level's 0x10, where the PC keeps its own */
        memcpy(q, l->boneCounts, MAX_BONES * 4);
        SLOT_STORE(&l->lod->bboxMin[0], q);
        q += MAX_BONES * 4;
    }
    SETP(l->lod->lodDistances, (f32 *)vb);
    l->lod->portLayout = l->layout ? l->layout : PORT_LAYOUT_INDEXED;
    return q;
}

/* A drawable GameCube geometry: its levels, if it is one. */
static void CollectGeometry(const u8 *base, u32 size, Geometry *geo, LevelOut **list) {
    Geometry *lods;
    u32 i;

    if (!InBlob(base, size, geo, sizeof(Geometry)) || geo->portLayout != GEOMETRY_GC_HEADER ||
        geo->numGroups == 0 || geo->numGroups > 8) {
        return;
    }
    s_stat.geoms++;
    s_stat.skinned += geo->skipDraw != 0;
    lods = (Geometry *)GETP(geo->groups);
    if (!InBlob(base, size, lods, geo->numGroups * 0x18)) {
        return;
    }
    for (i = 0; i < geo->numGroups; i++) {
        Geometry *lod = (Geometry *)((u8 *)lods + i * 0x18);
        GeometryGroup *groups = GETP(lod->groups);
        LevelOut *l;

        if (lod->portLayout != PORT_LAYOUT_GX || lod->numGroups == 0 || lod->numGroups > 0x400 ||
            !InBlob(base, size, groups, sizeof(GeometryGroup))) {
            continue;
        }
        if (groups[0].flags & 0x4000) {
            s_stat.shadow++;
            continue;
        }
        l = ConvertLevel(base, size, lod, geo->skipDraw != 0);
        if (l == NULL) {
            s_stat.failed++;
        } else {
            s_stat.levels++;
            lod->portLayout = PORT_LAYOUT_INDEXED;     /* reached again through another slot: skip */
            l->next = *list;
            *list = l;
        }
    }
}

/* Blocks kept by their owner: quadrant packs' geometry, PS2 packs' textures. */
static struct {
    void *owner;
    void *block;
} s_kept[1024];

void PortGeometry_Keep(void *owner, void *block) {
    u32 i;

    if (block == NULL) {
        return;
    }
    for (i = 0; i < sizeof(s_kept) / sizeof(s_kept[0]); i++) {
        if (s_kept[i].owner == NULL) {
            s_kept[i].owner = owner;
            s_kept[i].block = block;
            return;
        }
    }
    Port_Fatal("PortGeometry_Keep: more than %u kept blocks", (unsigned)i);
}

void PortGeometry_Release(void *owner) {
    u32 i;

    for (i = 0; i < sizeof(s_kept) / sizeof(s_kept[0]); i++) {
        if (s_kept[i].owner == owner) {
            Deallocate(s_kept[i].block);
            s_kept[i].owner = NULL;
            s_kept[i].block = NULL;
        }
    }
}

/* The GameCube geometry of a relocated blob (pack data, or a quadrant's GX
 * data), converted. Returns the block the
 * converted data lives in, or NULL (nothing converted). */
void *PortGeometry_ConvertGC(u8 *base, u32 size, const u32 *pointers, u32 numPointers, const u32 *relocs,
                             u32 numRelocs) {
    LevelOut *list = NULL, *l, *next;
    u32 i, total = 0;
    u8 *block, *p, *isSlot;

    memset(&s_stat, 0, sizeof(s_stat));
    /* Scan the blob for geometry headers: many geometries are reached through
     * offsets the game relocates itself (thing records, car parts, motion
     * parts, a car's shadow model), not through the pointer list. A header
     * is the magic 0x2003 with pointer slots at 0x8 (levels) and 0xC (LOD
     * distances). */
    isSlot = calloc(size / 4 + 1, 1);
    if (isSlot == NULL) {
        return NULL;
    }
    for (i = 0; i < numPointers; i++) {
        if (!(pointers[i] & 3) && pointers[i] + 4 <= size) {
            isSlot[pointers[i] / 4] = 1;
        }
    }
    for (i = 0; i + sizeof(Geometry) <= size; i += 4) {
        if (*(u16 *)(base + i) == GEOMETRY_GC_HEADER && isSlot[(i + 8) / 4] && isSlot[(i + 0xC) / 4]) {
            CollectGeometry(base, size, (Geometry *)(base + i), &list);
        }
    }
    free(isSlot);
    (void)relocs;
    (void)numRelocs;
    if (getenv("HWVX_GEOMLOG") != NULL && (s_stat.geoms || s_stat.skinned)) {
        Port_Log("geometry: %u GameCube geometries (%u skinned), %u levels converted, %u shadow, "
                 "%u not converted (not drawn)%s%s", s_stat.geoms, s_stat.skinned, s_stat.levels, s_stat.shadow, s_stat.failed,
                 s_stat.why ? ": last for " : "", s_stat.why ? s_stat.why : "");
    }
    if (list == NULL) {
        return NULL;
    }
    for (l = list; l != NULL; l = l->next) {
        total += LevelSize(l);
    }
    block = Allocate(total + 0x20, 0, "");
    p = block != NULL ? (u8 *)PTR((HND(block) + 0x1F) & ~0x1Fu) : NULL;
    for (l = list; l != NULL; l = next) {
        next = l->next;
        if (p != NULL) {
            p = PlaceLevel(l, p);
        } else {
            l->lod->portLayout = PORT_LAYOUT_GX;     /* out of memory: stays a display-list level */
        }
        FreeLevel(l);
    }
    return block;
}

/* ===========================================================================
 * The PS2 release's geometry (docs/ps2-version/README.md, HWVX-X-FILE-EDITOR;
 * scripts/ps2_geom.py is the reference these follow).
 *
 * Header 0x1005 (levels at header + 0x40). A level's u16 at 0 is its kind:
 * 1 drawn, 2 skinned, 3 shadow. A group's 0xC points to a 16-byte UVScroll
 * record followed by a DMA `ret` packet of VIF1 code for the VU1, in
 * batches: a GIF tag unpacked to VU address 0 (NLOOP, PRIM: 4 strip, 3
 * list), then STCYCL 3/1 and per vertex V4-32 positions (address 1; w: bit
 * 15 the GS ADC "don't draw" flag in strips, the signed matrix slot -1..1 in
 * skinned lists), V2-32 uvs (2), and V3-32 normals or V4-8 colours (3).
 * Skinned groups hold records {u8 bone[3], u8 0, ptr packet} instead, up to
 * a null pointer; a vertex's bone is bone[w + 1].
 *
 * Strips: a vertex with ADC starts a strip and draws nothing; any other
 * draws (i-2, i-1, i), the odd triangles of a run swapped. The PS2 draws
 * both faces, and about 1% of its strips run the other way round, so these
 * levels are drawn without back-face culling (PORT_LAYOUT_INDEXED_NOCULL)
 * rather than re-oriented (ps2_geom.py orient, not done here).
 *
 * Colours are GS-scaled (0x80 = 1.0; alpha a quarter): back to 0..255 as
 * 2x + (x >> 6) and 4a + (a >> 4). Group flags lack the GameCube's level
 * class bits: added back (0x800 normals, 0x1000 colours, 0x2000 both; alpha
 * groups lose 0x16, 0x8000 becomes 0x9000), which reproduces every
 * GameCube flag. Texture relocations store at a group's 0x18 as on the PC.
 *
 * Shadow levels: their positions pointer (0xC) is not a pointer on the PS2;
 * until it is found they are switched off (no positions, no edges).
 * ========================================================================= */

#define GEOMETRY_PS2_HEADER 0x1005
#define PS2_MAX_BATCH 256

typedef struct PS2Batch {
    u32 count, prim;
    const u8 *pos, *uv, *nrm, *col;   /* V4-32, V2-32, V3-32, V4-8 */
} PS2Batch;

/* A hash map from a converted vertex's bytes to its index. */
typedef struct PS2Map {
    u32 *hash;
    u16 *index;
    u32 cap;
} PS2Map;

static u32 HashBytes(const void *p, u32 n, u32 h) {
    const u8 *b = p;

    while (n--) {
        h = (h ^ *b++) * 16777619u;
    }
    return h;
}

/* The VIF batches of one DMA packet at `dma`; returns how many, or -1. */
static int ReadPS2Batches(const u8 *base, u32 size, const u8 *dma, PS2Batch *out, int max) {
    u32 tag, cycle = 0;
    const u8 *p, *end;
    int n = -1;

    if (!InBlob(base, size, dma, 16)) {
        return -1;
    }
    tag = *(const u32 *)dma;
    if (((tag >> 28) & 7) != 6) {
        s_stat.why = "a PS2 packet that is not a DMA ret";
        return -1;
    }
    end = dma + 16 + (tag & 0xFFFF) * 16;
    if (!InBlob(base, size, dma, (u32)(end - dma))) {
        return -1;
    }
    for (p = dma + 8; p + 4 <= end;) {
        u32 w = *(const u32 *)p, cmd = (w >> 24) & 0x7F, num = (w >> 16) & 0xFF, imm = w & 0xFFFF, len = 0;
        const u8 *q = p + 4;

        if ((cmd & 0x60) == 0x60) {
            u32 vn = (cmd >> 2) & 3, vl = cmd & 3, el = vl == 3 ? 2 : (vn + 1) * (4 >> vl);
            u32 count = num ? num : 256, addr = imm & 0x3FF;

            len = (count * el + 3) & ~3u;
            if (addr == 0) {
                u64 lo;

                if (cmd != 0x6C || count != 1 || cycle != 0x0404 || n + 1 >= max) {
                    s_stat.why = "a PS2 GIF tag unpack of an unknown shape";
                    return -1;
                }
                memcpy(&lo, q, 8);
                n++;
                memset(&out[n], 0, sizeof(out[n]));
                out[n].prim = (u32)((lo >> 47) & 7);
            } else if (n < 0 || cycle != 0x0103 || count > PS2_MAX_BATCH) {
                s_stat.why = "a PS2 vertex unpack outside a batch";
                return -1;
            } else if (addr == 1 && cmd == 0x6C) {
                out[n].pos = q;
                out[n].count = count;
            } else if (addr == 2 && cmd == 0x64) {
                out[n].uv = q;
            } else if (addr == 3 && cmd == 0x68) {
                out[n].nrm = q;
            } else if (addr == 3 && cmd == 0x6E) {
                out[n].col = q;
            } else {
                s_stat.why = "a PS2 unpack to an unknown VU address";
                return -1;
            }
        } else if (cmd == 0x01) {
            cycle = imm;
        } else if (cmd == 0x20) {
            len = 4;
        } else if (cmd == 0x30 || cmd == 0x31) {
            len = 16;
        } else if (cmd != 0x00 && cmd != 0x02 && cmd != 0x03 && cmd != 0x17) {
            s_stat.why = "a PS2 VIF command other than NOP, OFFSET, BASE, MSCNT";
            return -1;
        }
        p = q + len;
    }
    for (tag = 0; (int)tag <= n; tag++) {
        if (out[tag].pos == NULL || out[tag].uv == NULL || (out[tag].nrm == NULL) == (out[tag].col == NULL)) {
            s_stat.why = "a PS2 batch without positions, uvs and one of normals/colours";
            return -1;
        }
    }
    return n + 1;
}

/* The index of a converted vertex, added if new; ~0 when the level is full. */
static u32 PS2Vertex(LevelOut *l, PS2Map *m, const f32 *pos, const f32 *uv, const f32 *nrm, const u8 *col, u8 bone) {
    f32 p4[4] = { pos[0], pos[1], pos[2], 0.0f }, n4[4] = { 0, 0, 0, 0 };
    u8 c[4] = { 255, 255, 255, 255 };
    u32 h, k, i;

    if (nrm != NULL) {
        memcpy(n4, nrm, 12);
    }
    if (col != NULL) {
        for (i = 0; i < 3; i++) {
            u32 x = col[i] * 2u + (col[i] >> 6);
            c[i] = (u8)(x > 255 ? 255 : x);
        }
        k = col[3] * 4u + (col[3] >> 4);
        c[3] = (u8)(k > 255 ? 255 : k);
    }
    h = HashBytes(p4, 16, 2166136261u);
    h = HashBytes(uv, 8, h);
    h = HashBytes(n4, 16, h);
    h = HashBytes(c, 4, h);
    h = HashBytes(&bone, 1, h) | 1;
    for (k = h & (m->cap - 1); m->hash[k] != 0; k = (k + 1) & (m->cap - 1)) {
        i = m->index[k];
        if (m->hash[k] == h && !memcmp(&l->pos[i * 4], p4, 16) && !memcmp(&l->uv[i * 2], uv, 8) &&
            !memcmp(&l->nrm[i * 4], n4, 16) && !memcmp(&l->clr[i * 4], c, 4) && l->bone[i] == bone) {
            return i;
        }
    }
    if (l->numVerts >= MAX_LEVEL_VERTS) {
        return ~0u;
    }
    i = l->numVerts++;
    m->hash[k] = h;
    m->index[k] = (u16)i;
    memcpy(&l->pos[i * 4], p4, 16);
    memcpy(&l->uv[i * 2], uv, 8);
    memcpy(&l->nrm[i * 4], n4, 16);
    memcpy(&l->clr[i * 4], c, 4);
    l->bone[i] = bone;
    return i;
}

/* Append a batch's triangles to a group's list. */
static int PS2BatchTriangles(LevelOut *l, PS2Map *m, const PS2Batch *b, const u8 *bones, u32 g, u32 *cap,
                             int *hasN, int *hasC) {
    u32 idx[PS2_MAX_BATCH], i;
    s32 run = -1;

    *hasN |= b->nrm != NULL;
    *hasC |= b->col != NULL;
    for (i = 0; i < b->count; i++) {
        const f32 *pos = (const f32 *)(b->pos + i * 16);
        s32 w = *(const s32 *)(b->pos + i * 16 + 12);
        u8 bone = 0;

        if (bones != NULL) {
            if (w < -1 || w > 1 || bones[w + 1] == 0xFF || bones[w + 1] >= MAX_BONES) {
                s_stat.why = "a PS2 matrix slot without a bone";
                return 0;
            }
            bone = bones[w + 1];
        }
        idx[i] = PS2Vertex(l, m, pos, (const f32 *)(b->uv + i * 8), b->nrm ? (const f32 *)(b->nrm + i * 12) : NULL,
                           b->col ? b->col + i * 4 : NULL, bone);
        if (idx[i] == ~0u) {
            s_stat.why = "more than 0x7FFF vertices";
            return 0;
        }
    }
    for (i = 0; i < b->count; i++) {
        u32 t[3];

        if (b->prim == 4) {
            s32 w = *(const s32 *)(b->pos + i * 16 + 12);

            if (w & 0x8000) {
                run = -1;
                continue;
            }
            if (i < 2) {
                return 0;
            }
            run = run < 0 ? 0 : run;
            t[0] = idx[run % 2 ? i - 1 : i - 2];
            t[1] = idx[run % 2 ? i - 2 : i - 1];
            t[2] = idx[i];
            run++;
        } else if (b->prim == 3) {
            if (i % 3 != 2) {
                continue;
            }
            t[0] = idx[i - 2];
            t[1] = idx[i - 1];
            t[2] = idx[i];
        } else {
            s_stat.why = "a PS2 GIF primitive other than strips and lists";
            return 0;
        }
        if ((l->numTris[g] + 1) * 3 > *cap) {
            u16 *grown = realloc(l->tris[g], (*cap * 2 + 96) * sizeof(u16));
            if (grown == NULL) {
                return 0;
            }
            l->tris[g] = grown;
            *cap = *cap * 2 + 96;
        }
        l->tris[g][l->numTris[g] * 3 + 0] = (u16)t[0];
        l->tris[g][l->numTris[g] * 3 + 1] = (u16)t[1];
        l->tris[g][l->numTris[g] * 3 + 2] = (u16)t[2];
        l->numTris[g]++;
    }
    return 1;
}

static u16 PS2GroupFlags(u16 f, u16 levelClass) {
    if (f & 0x4000) {
        return f;
    }
    if (f & 1) {
        f &= (u16)~0x16;
    }
    if (f & 0x8000) {
        return (u16)((f & ~0x8000) | 0x9000);
    }
    return (u16)(f | levelClass);
}

static LevelOut *ConvertPS2Level(const u8 *base, u32 size, Geometry *lod, int skinned) {
    GeometryGroup *groups = GETP(lod->groups);
    PS2Batch *batches = malloc(sizeof(PS2Batch) * 64);
    LevelOut *out = calloc(1, sizeof(LevelOut));
    PS2Map map = { NULL, NULL, 1u << 16 };
    int hasN = 0, hasC = 0, ok = 1, nb, b;
    u32 g;

    if (out == NULL || batches == NULL || !InBlob(base, size, groups, lod->numGroups * sizeof(GeometryGroup))) {
        free(batches);
        free(out);
        return NULL;
    }
    out->lod = lod;
    out->skinned = skinned;
    out->layout = PORT_LAYOUT_INDEXED_NOCULL;
    out->numGroups = lod->numGroups;
    out->tris = calloc(lod->numGroups, sizeof(u16 *));
    out->numTris = calloc(lod->numGroups, sizeof(u32));
    out->pos = malloc(MAX_LEVEL_VERTS * 16);
    out->uv = malloc(MAX_LEVEL_VERTS * 8);
    out->nrm = malloc(MAX_LEVEL_VERTS * 16);
    out->clr = malloc(MAX_LEVEL_VERTS * 4);
    out->bone = calloc(MAX_LEVEL_VERTS, 1);
    map.hash = calloc(map.cap, sizeof(u32));
    map.index = calloc(map.cap, sizeof(u16));
    if (!out->tris || !out->numTris || !out->pos || !out->uv || !out->nrm || !out->clr || !out->bone || !map.hash ||
        !map.index) {
        ok = 0;
    }
    for (g = 0; ok && g < lod->numGroups; g++) {
        const u8 *pkt = (const u8 *)GETP(groups[g].uvScroll);
        u32 cap = 0;

        if (!InBlob(base, size, pkt, 0x18)) {
            s_stat.why = "a PS2 group packet outside the blob";
            ok = 0;
            break;
        }
        if (!skinned) {
            nb = ReadPS2Batches(base, size, pkt + 0x10, batches, 64);
            for (b = 0; ok && b < nb; b++) {
                ok = PS2BatchTriangles(out, &map, &batches[b], NULL, g, &cap, &hasN, &hasC);
            }
            ok = ok && nb >= 0;
            continue;
        }
        for (pkt += 0x10; ok && InBlob(base, size, pkt, 8) && *(const u32 *)(pkt + 4) != 0; pkt += 8) {
            const u8 *sub = SLOT_LOAD(const u8 *, pkt + 4);

            nb = ReadPS2Batches(base, size, sub, batches, 64);
            for (b = 0; ok && b < nb; b++) {
                ok = PS2BatchTriangles(out, &map, &batches[b], pkt, g, &cap, &hasN, &hasC);
            }
            ok = ok && nb >= 0;
        }
    }
    free(batches);
    free(map.hash);
    free(map.index);
    if (ok && out->numVerts > 0) {
        u16 cls = hasN && hasC ? 0x2000 : hasN ? 0x800 : 0x1000;

        for (g = 0; g < lod->numGroups; g++) {
            groups[g].flags = PS2GroupFlags(groups[g].flags, cls);
        }
        if (!hasN) {
            free(out->nrm);
            out->nrm = NULL;
        }
        if (!hasC) {
            free(out->clr);
            out->clr = NULL;
        }
        if (skinned && !SortByBone(out)) {
            ok = 0;
        }
    }
    if (!ok || out->numVerts == 0) {
        FreeLevel(out);
        return NULL;
    }
    return out;
}

/* A PS2 geometry's levels: drawn and skinned ones converted, shadow ones off.
 * isTexSlot notes each converted group's texture slot (0x18). */
static void CollectPS2Geometry(const u8 *base, u32 size, u32 off, LevelOut **list, u8 *isTexSlot) {
    Geometry *geo = (Geometry *)(base + off), *lods = (Geometry *)GETP(geo->groups);
    u32 i, g;

    if (geo->numGroups == 0 || geo->numGroups > 8 || (const u8 *)lods != base + off + 0x40 ||
        !InBlob(base, size, lods, geo->numGroups * 0x18)) {
        return;
    }
    s_stat.geoms++;
    for (i = 0; i < geo->numGroups; i++) {
        Geometry *lod = (Geometry *)((u8 *)lods + i * 0x18);
        u16 kind = lod->portLayout;
        GeometryGroup *groups = GETP(lod->groups);
        LevelOut *l;

        if (kind == 3) {
            /* shadow volume: no positions pointer on the PS2 (see above) */
            if (InBlob(base, size, groups, lod->numGroups * sizeof(GeometryGroup))) {
                for (g = 0; g < lod->numGroups; g++) {
                    groups[g].dlSize = 0;
                }
            }
            SETP(lod->lodDistances, NULL);
            lod->portLayout = 0;
            s_stat.shadow++;
            continue;
        }
        if ((kind != 1 && kind != 2) || lod->numGroups == 0 || lod->numGroups > 0x400) {
            continue;
        }
        s_stat.skinned += kind == 2;
        l = ConvertPS2Level(base, size, lod, kind == 2);
        if (l == NULL) {
            s_stat.failed++;
            lod->numGroups = 0;     /* nothing drawable: leave it out rather than misread it */
            continue;
        }
        s_stat.levels++;
        lod->portLayout = PORT_LAYOUT_INDEXED_NOCULL;
        for (g = 0; g < lod->numGroups; g++) {
            isTexSlot[((u8 *)&groups[g].textureOverride - base) / 4] = 1;
        }
        l->next = *list;
        *list = l;
    }
}

/* The PS2 geometry of a relocated blob (pack data or a quadrant's GX data),
 * converted; the blob's texture-id relocations at converted groups' 0x18
 * are flagged to store the id at 0x1A (PORT_TEXRELOC_U16). Returns the block
 * the converted data lives in, or NULL. */
void *PortGeometry_ConvertPS2(u8 *base, u32 size, const u32 *pointers, u32 numPointers, u32 *texIdRelocs,
                              u32 numTexIdRelocs) {
    LevelOut *list = NULL, *l, *next;
    u32 i, total = 0;
    u8 *block, *p, *isSlot;

    memset(&s_stat, 0, sizeof(s_stat));
    isSlot = calloc(size / 4 + 1, 2);
    if (isSlot == NULL) {
        return NULL;
    }
    for (i = 0; i < numPointers; i++) {
        if (!(pointers[i] & 3) && pointers[i] + 4 <= size) {
            isSlot[pointers[i] / 4] = 1;
        }
    }
    for (i = 0; i + 0x40 <= size; i += 4) {
        if (*(u16 *)(base + i) == GEOMETRY_PS2_HEADER && isSlot[(i + 8) / 4] && isSlot[(i + 0xC) / 4]) {
            CollectPS2Geometry(base, size, i, &list, isSlot + size / 4 + 1);
        }
    }
    for (i = 0; i < numTexIdRelocs; i++) {
        u32 slot = texIdRelocs[i * 2];
        if (!(slot & 3) && slot + 4 <= size && isSlot[size / 4 + 1 + slot / 4]) {
            texIdRelocs[i * 2] = slot | PORT_TEXRELOC_U16;
        }
    }
    free(isSlot);
    if (getenv("HWVX_GEOMLOG") != NULL && s_stat.geoms) {
        Port_Log("geometry: %u PS2 geometries (%u skinned levels), %u levels converted, %u shadow (off), "
                 "%u not converted (not drawn)%s%s", s_stat.geoms, s_stat.skinned, s_stat.levels, s_stat.shadow,
                 s_stat.failed, s_stat.why ? ": last for " : "", s_stat.why ? s_stat.why : "");
    }
    for (l = list; l != NULL; l = l->next) {
        total += LevelSize(l);
    }
    block = list != NULL ? Allocate(total + 0x20, 0, "") : NULL;
    p = block != NULL ? (u8 *)PTR((HND(block) + 0x1F) & ~0x1Fu) : NULL;
    for (l = list; l != NULL; l = next) {
        next = l->next;
        if (p != NULL) {
            p = PlaceLevel(l, p);
        } else {
            l->lod->numGroups = 0;      /* out of memory: not drawn */
        }
        FreeLevel(l);
    }
    return block;
}
