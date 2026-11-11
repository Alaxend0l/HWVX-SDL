/* The GameCube's geometry turned into the indexed layout the PC release
 * keeps, as a pack loads. Port-only file.
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
 * Left as they are (drawn through GX as before): skinned geometry (header
 * 0x4 nonzero, drawn by Gfx_Anim.c), shadow volumes (groups flagged 0x4000,
 * Render_DrawShadows), and any level whose lists hold something other than
 * the three primitives, an index out of range, or more than 0x7FFF
 * vertices (the PC's index lists mark strip records with bit 15).
 * HWVX_GX_GEOMETRY=1 leaves all GameCube geometry to GX, to compare. */
#include "common.h"
#include "Gfx.h"

#define GEOMETRY_GC_HEADER 0x2003
#define MAX_LEVEL_VERTS 0x7FFF

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
static LevelOut *ConvertLevel(const u8 *base, u32 size, Geometry *lod) {
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
    if (out->tris == NULL || out->numTris == NULL || map.keys == NULL || map.vals == NULL || out->pos == NULL ||
        out->uv == NULL || (a.nrm != NULL && out->nrm == NULL) || (a.clr != NULL && out->clr == NULL)) {
        ok = 0;
    }
    for (g = 0; ok && g < lod->numGroups; g++) {
        const u8 *dl = GETP(groups[g].displayList), *end = dl + groups[g].dlSize;
        u16 flags = groups[g].flags;
        int lit = (flags & 0x16) && !(flags & 0x81);
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
            if (dl + count * 6 > end || count > sizeof(idx) / sizeof(idx[0])) {
                ok = 0;
                break;
            }
            for (v = 0; v < count; v++, dl += 6) {
                u32 A = RdBE16(dl), B = RdBE16(dl + 2), C = RdBE16(dl + 4);
                u64 key = 1ull << 63 | (u64)lit << 48 | (u64)A << 32 | (u64)B << 16 | C;
                int isNew;
                u32 k;

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
    if (!ok || out->numVerts == 0) {
        FreeLevel(out);
        return NULL;
    }
    return out;
}

static u32 Align16(u32 n) {
    return (n + 15) & ~15u;
}

/* The bytes a level takes in the block: vertex buffer header, arrays, lists. */
static u32 LevelSize(const LevelOut *l) {
    u32 size = 0x20 + Align16(l->numVerts * 16) + Align16(l->numVerts * 8), g;

    size += l->nrm != NULL ? Align16(l->numVerts * 16) : 0;
    size += l->clr != NULL ? Align16(l->numVerts * 4) : 0;
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
    SETP(l->lod->lodDistances, (f32 *)vb);
    l->lod->portLayout = PORT_LAYOUT_INDEXED;
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
    if (geo->skipDraw != 0) {
        s_stat.skinned++;
        return;
    }
    s_stat.geoms++;
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
        l = ConvertLevel(base, size, lod);
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

/* Quadrant packs' blocks, by quad. */
static struct {
    void *owner;
    void *block;
} s_kept[512];

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
    Port_Fatal("PortGeometry_Keep: more than %u converted quadrant packs", (unsigned)i);
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
    static int gx = -1;
    LevelOut *list = NULL, *l, *next;
    u32 i, total = 0;
    u8 *block, *p, *isSlot;

    if (gx < 0) {
        gx = getenv("HWVX_GX_GEOMETRY") != NULL;
    }
    if (gx) {
        return NULL;
    }
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
        Port_Log("geometry: %u GameCube geometries (%u skinned, left to GX), %u levels converted, %u shadow, "
                 "%u left to GX%s%s", s_stat.geoms, s_stat.skinned, s_stat.levels, s_stat.shadow, s_stat.failed,
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
