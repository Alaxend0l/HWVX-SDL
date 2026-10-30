#include "common.h"


s8 OBoxHitsSegmentZeroTime(void *, void *, void *);

typedef struct OBoxCorners
{
    u8 pad[0x10];
    Vector4 corners[8];
} OBoxCorners;
typedef struct CollisionInfo
{
    Vector4 pos;
    Vector4 normal;
    s32 unk20;
    s32 unk24;
    u32 material;
    u8 pad2C[0x40 - 0x2C];
    u8 type;
} CollisionInfo;

f32 OBoxHitsSegment(void *, Vector4 *, void *, f32, void *);

typedef struct TerrainGrid {
    Vector4 origin;
    u8 unk10[0x20 - 0x10];
    u8 *cells;
    u8 unk24[0x28 - 0x24];
    f32 sizeX;
    f32 sizeY;
    f32 sizeZ;
    s32 numX;
    s32 numY;
    s32 numZ;
    s32 *cellIndex;
} TerrainGrid;

static inline u8 *TerrainGrid_GetCellXYZ(TerrainGrid *grid, s32 x, s32 y, s32 z) {
    s32 index;

    if (x < 0 || x >= grid->numX) {
        index = -1;
    } else if (y < 0 || y >= grid->numY) {
        index = -1;
    } else if (z < 0 || z >= grid->numZ) {
        index = -1;
    } else {
        index = z * grid->numX + grid->numZ * (y * grid->numX) + x;
    }
    return index == -1 ? grid->cells : grid->cells + grid->cellIndex[index] * 0x50;
}

s8 RayHitsTerrain(Vector4 *start, Vector4 *end, void *arg2, void *arg3)
{
    Vector4 dir __attribute__((aligned(16)));

    PSVECSubtract(end, start, &dir);
    return RayHitsTerrain_Aux3(start, &dir, arg2, arg3);
}




s8 OBoxHitsTerrainZeroTimeEdgesOnly(void *obox, void *arg1) {
    TerrainGrid *grid;
    Vector4 bounds[3] __attribute__((aligned(16)));
    f32 ox;
    f32 oy;
    f32 oz;
    f32 sx;
    f32 sy;
    f32 sz;
    s32 x;
    s32 x1;
    s32 y;
    s32 y0;
    s32 y1;
    s32 z;
    s32 z0;
    s32 z1;
    u32 i;

    if (g_worldData.world == NULL || SLOT_LOAD(TerrainGrid *, g_worldData.world) == NULL) {
        return 0;
    }
    grid = SLOT_LOAD(TerrainGrid *, g_worldData.world);
    ox = grid->origin.x;
    oy = grid->origin.y;
    oz = grid->origin.z;
    sx = grid->sizeX;
    sy = grid->sizeY;
    sz = grid->sizeZ;
    bounds[0].x = 99999.0f;
    bounds[0].y = 99999.0f;
    bounds[0].z = 99999.0f;
    bounds[0].w = 99999.0f;
    bounds[1].x = -99999.0f;
    bounds[1].y = -99999.0f;
    bounds[1].z = -99999.0f;
    bounds[1].w = -99999.0f;
    for (i = 0; i < 8; i++) {
        MinMaxVec(&bounds[0], &bounds[1], &((OBoxCorners *)obox)->corners[i]);
    }
    World_NextSweep();
    x1 = (bounds[1].x - ox) / sx;
    y0 = (bounds[0].y - oy) / sy;
    y1 = (bounds[1].y - oy) / sy;
    z0 = (bounds[0].z - oz) / sz;
    z1 = (bounds[1].z - oz) / sz;
    for (x = (bounds[0].x - ox) / sx; x <= x1; x++) {
        for (y = y0; y <= y1; y++) {
            for (z = z0; z <= z1; z++) {
                if (OBoxHitsCellZeroTimeEdgesOnly(obox, TerrainGrid_GetCellXYZ(SLOT_LOAD(TerrainGrid *, g_worldData.world), x, y, z), arg1)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}



s8 OBoxHitsCellZeroTimeEdgesOnly(void *obox, void *cell, void *arg2)
{
    s8 OBoxHitsPolyZeroTime(void *, void *, void *);
    s8 hit = 0;
    u32 i;
    u8 *poly;

    *(f32 *)((char *)obox + 0xb0) = 0.0f;
    *(f32 *)((char *)obox + 0xb4) = 0.0f;
    *(f32 *)((char *)obox + 0xb8) = 0.0f;
    *(f32 *)((char *)obox + 0xc0) = 0.0f;
    *(f32 *)((char *)obox + 0xc4) = 0.0f;
    *(f32 *)((char *)obox + 0xc8) = 0.0f;
    if (*(u32 *)((char *)(SLOT_LOAD(u32 *, g_worldData.world)) + 0x58) == 1)
    {
        hit = OBoxHitsBSPZeroTimeEdgesOnly(obox, cell, arg2);
    }
    else
    {
        for (i = 0; i < *(u32 *)((char *)cell + 0x3c); i++)
        {
            poly = (u8 *)PTR((*(u32 *)((char *)(SLOT_LOAD(u32 *, g_worldData.world)) + 0x4c) +
                          (SLOT_LOAD(u32 *, ((char *)cell + 0x38)))[i] * 0x20));
            if (*(u32 *)(poly + 0x18) != g_worldData.unk4)
            {
                *(u32 *)(poly + 0x18) = g_worldData.unk4;
                hit = OBoxHitsPolyZeroTime(obox, poly, arg2);
                if (hit)
                {
                    return 1;
                }
            }
        }
    }
    return hit;
}

static inline s8 OBoxHitsCellZeroTime(void *obox, void *cell, void *arg2)
{
    s8 OBoxHitsPolyZeroTime(void *, void *, void *);
    s8 hit = 0;
    u32 i;
    u8 *poly;

    *(f32 *)((char *)obox + 0xb0) = 0.0f;
    *(f32 *)((char *)obox + 0xb4) = 0.0f;
    *(f32 *)((char *)obox + 0xb8) = 0.0f;
    *(f32 *)((char *)obox + 0xc0) = 0.0f;
    *(f32 *)((char *)obox + 0xc4) = 0.0f;
    *(f32 *)((char *)obox + 0xc8) = 0.0f;
    if (*(u32 *)((char *)(SLOT_LOAD(u32 *, g_worldData.world)) + 0x58) == 1)
    {
        hit = OBoxHitsBSPZeroTime(obox, cell, arg2);
    }
    else
    {
        for (i = 0; i < *(u32 *)((char *)cell + 0x3c); i++)
        {
            poly = (u8 *)PTR((*(u32 *)((char *)(SLOT_LOAD(u32 *, g_worldData.world)) + 0x4c) +
                          (SLOT_LOAD(u32 *, ((char *)cell + 0x38)))[i] * 0x20));
            if (*(u32 *)(poly + 0x18) != g_worldData.unk4)
            {
                *(u32 *)(poly + 0x18) = g_worldData.unk4;
                hit = OBoxHitsPolyZeroTime(obox, poly, arg2);
                if (hit)
                {
                    return 1;
                }
            }
        }
    }
    return hit;
}

s8 OBoxHitsCell(void *obox, Vector4 *vel, void *cell, f32 *time, void *arg4) {
    f32 OOBHitsPoly(void *obox, void *poly, Vector4 *vel, void *arg4, f32 t);
    extern Vector4 g_zeroVector;
    s8 hit = 0;
    u32 i;
    u8 *poly;
    f32 best;
    f32 t;

    if (vel == &g_zeroVector) {
        return OBoxHitsCellZeroTime(obox, cell, arg4);
    }
    *(f32 *)((u8 *)obox + 0xB0) = PSVECDotProduct(SLOT_LOAD(void *, ((u8 *)obox + 0xA0)), obox);
    *(f32 *)((u8 *)obox + 0xB4) = PSVECDotProduct(SLOT_LOAD(void *, ((u8 *)obox + 0xA4)), obox);
    *(f32 *)((u8 *)obox + 0xB8) = PSVECDotProduct(SLOT_LOAD(void *, ((u8 *)obox + 0xA8)), obox);
    *(f32 *)((u8 *)obox + 0xC0) = -PSVECDotProduct(SLOT_LOAD(void *, ((u8 *)obox + 0xA0)), vel);
    *(f32 *)((u8 *)obox + 0xC4) = -PSVECDotProduct(SLOT_LOAD(void *, ((u8 *)obox + 0xA4)), vel);
    *(f32 *)((u8 *)obox + 0xC8) = -PSVECDotProduct(SLOT_LOAD(void *, ((u8 *)obox + 0xA8)), vel);
    if (*(u32 *)((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x58) == 1) {
        hit = OBoxHitsBSP(obox, vel, cell, time, arg4);
    } else {
        best = *time;
        for (i = 0; i < *(u32 *)((u8 *)cell + 0x3C); i++) {
            poly = SLOT_LOAD(u8 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x4C)) + (SLOT_LOAD(u32 *, ((u8 *)cell + 0x38)))[i] * 0x20;
            if (*(u32 *)(poly + 0x18) != g_worldData.unk4) {
                *(u32 *)(poly + 0x18) = g_worldData.unk4;
                t = OOBHitsPoly(obox, poly, vel, arg4, best);
                if (t < best) {
                    best = t;
                    hit = 1;
                }
            }
        }
        if (hit) {
            *time = best;
        }
    }
    return hit;
}

s8 OBoxHitsBSP(void *obox, Vector4 *vel, void *bsp, f32 *time, void *arg4)
{
    Vector4 corner __attribute__((aligned(16)));
    Vector4 hitNormal __attribute__((aligned(16)));
    Vector4 normal __attribute__((aligned(16)));
    u8 order[8] = {2, 3, 6, 7, 0, 1, 4, 5};
    u8 *edge;
    f32 t;
    f32 hit;
    u32 i;
    s8 hitCorner;
    s8 hitEdge;
    u32 m;
    u32 material;

    t = *time;
    hitCorner = 0;
    hitEdge = 0;
    if (*(u32 *)((u8 *)bsp + 0x3c) == 0 && *(u32 *)((u8 *)bsp + 0x40) == 0)
    {
        return 0;
    }
    for (i = 0; i < *(u32 *)((u8 *)bsp + 0x40) && t > 0.0f; i++)
    {
        edge = (u8 *)(SLOT_LOAD(u8 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x54)) +
                      (SLOT_LOAD(u16 *, ((u8 *)bsp + 0x44)))[i] * 0x30);
        if (*(u32 *)(edge + 0x20) != g_worldData.unk4)
        {
            *(u32 *)(edge + 0x20) = g_worldData.unk4;
            hit = OBoxHitsSegment(obox, vel, edge, t, arg4);
            if (hit < t)
            {
                t = hit;
                hitEdge = 1;
                if (arg4 != NULL)
                {
                    ((CollisionInfo *)arg4)->material =
                        (SLOT_LOAD(u32 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x5c)))[*(u32 *)(edge + 0x24)];
                }
            }
        }
    }
    for (i = 0; i < 8 && t > 0.0f; i++)
    {
        m = RayHitsBSP(&((OBoxCorners *)obox)->corners[order[i]], vel, bsp, &t, &hitNormal);
        if (m != 0)
        {
            material = m;
            hitCorner = 1;
            corner = ((OBoxCorners *)obox)->corners[order[i]];
            normal = hitNormal;
        }
    }
    if (!hitEdge && !hitCorner)
    {
        return 0;
    }
    if (arg4 != NULL && hitCorner)
    {
        ((CollisionInfo *)arg4)->type = 0xfe;
        ((CollisionInfo *)arg4)->material = material;
        ((CollisionInfo *)arg4)->pos.x = vel->x * t + corner.x;
        ((CollisionInfo *)arg4)->pos.y = vel->y * t + corner.y;
        ((CollisionInfo *)arg4)->pos.z = vel->z * t + corner.z;
        ((CollisionInfo *)arg4)->unk20 = 0;
        ((CollisionInfo *)arg4)->unk24 = 0;
        ((CollisionInfo *)arg4)->normal = normal;
    }
    *time = t;
    return 1;
}

static inline s8 PointInsideBSP(void *bsp, void *point) {
    u32 node;

    node = 0;
    if (SLOT_LOAD(u8 *, ((u8 *)bsp + 0x30)) == NULL) {
        return 0;
    }
    while (1) {
        if (Distance_Plane(SLOT_LOAD(u8 *, ((u8 *)bsp + 0x30)) + node * 0x20, point) >= 0.0f) {
            node = *(u16 *)(SLOT_LOAD(u8 *, ((u8 *)bsp + 0x30)) + node * 0x20 + 0x14);
            if (node == 0xFFFF) {
                return 0;
            }
        } else {
            node = *(u16 *)(SLOT_LOAD(u8 *, ((u8 *)bsp + 0x30)) + node * 0x20 + 0x16);
            if (node == 0xFFFF) {
                return 1;
            }
        }
    }
}

s8 OBoxHitsBSPZeroTime(void *obox, void *bsp, void *arg2) {
    u8 *edge;
    u32 i;

    if (*(u32 *)((u8 *)bsp + 0x3C) == 0 && *(u32 *)((u8 *)bsp + 0x40) == 0) {
        return 0;
    }
    for (i = 0; i < *(u32 *)((u8 *)bsp + 0x40); i++) {
        edge = SLOT_LOAD(u8 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x54)) + (SLOT_LOAD(u16 *, ((u8 *)bsp + 0x44)))[i] * 0x30;
        if (*(u32 *)(edge + 0x20) != g_worldData.unk4) {
            *(u32 *)(edge + 0x20) = g_worldData.unk4;
            if (OBoxHitsSegmentZeroTime(obox, edge, arg2)) {
                return 1;
            }
        }
    }
    for (i = 0; i < 8; i++) {
        if (PointInsideBSP(bsp, &((OBoxCorners *)obox)->corners[i])) {
            return 1;
        }
    }
    return 0;
}

s8 OBoxHitsBSPZeroTimeEdgesOnly(void *obox, void *bsp, void *arg2)
{
    u8 *edge;
    u32 i;

    if (*(u32 *)((char *)bsp + 0x3c) == 0 && *(u32 *)((char *)bsp + 0x40) == 0)
    {
        return 0;
    }
    for (i = 0; i < *(u32 *)((char *)bsp + 0x40); i++)
    {
        edge = (u8 *)(SLOT_LOAD(u8 *, ((char *)SLOT_LOAD(void *, g_worldData.world) + 0x54)) +
                      (SLOT_LOAD(u16 *, ((char *)bsp + 0x44)))[i] * 0x30);
        if (*(u32 *)(edge + 0x20) != g_worldData.unk4)
        {
            *(u32 *)(edge + 0x20) = g_worldData.unk4;
            if (OBoxHitsSegmentZeroTime(obox, edge, arg2))
            {
                return 1;
            }
        }
    }
    return 0;
}

u32 RayHitsBSP2(Vector4 *start, Vector4 *end, Vector4 *bsp, void *node, s32 depth, void *plane, f32 *t, Vector4 *hit) {
    typedef struct {
        Vector4 plane;
        u32 material;
        u16 front;
        u16 back;
    } BSPNode2;
    BSPNode2 *n;
    f32 d0;
    f32 d1;
    f32 frac;
    u32 result;

    n = (BSPNode2 *)(SLOT_LOAD(u8 *, ((u8 *)bsp + 0x30)) + (u32)HND(node) * 0x20);
    d0 = Distance_Plane(n, start);
    d1 = Distance_Plane(n, end);
    if (d0 == 0.0f && d1 == 0.0f) {
        if (n->front == 0xFFFF) {
            return 0;
        }
        return RayHitsBSP2(start, end, bsp, (void *)PTR(n->front), depth + 1, plane, t, hit);
    }
    if (d0 > 0.0f && d1 < 0.0f) {
        Vector4 dir __attribute__((aligned(16)));
        Vector4 mid __attribute__((aligned(16)));
        PSVECSubtract(end, start, &dir);
        if (d0 != d1) {
            frac = d0 / (d0 - d1);
        } else {
            frac = 0.0f;
        }
        mid.x = dir.x * frac + start->x;
        mid.y = dir.y * frac + start->y;
        mid.z = dir.z * frac + start->z;
        if (n->front != 0xFFFF) {
            result = RayHitsBSP2(start, &mid, bsp, (void *)PTR(n->front), depth + 1, plane, t, hit);
            if (result) {
                *t *= frac;
                return result;
            }
        }
        if (n->back != 0xFFFF) {
            result = RayHitsBSP2(&mid, end, bsp, (void *)PTR(n->back), depth + 1, plane, t, hit);
            if (result) {
                *t = *t * (1.0f - frac) + frac;
                if (frac == *t) {
                    *hit = mid;
                    *t = frac;
                    *(Vector4 *)plane = n->plane;
                    return (SLOT_LOAD(u32 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x5C)))[n->material];
                }
                return result;
            }
            return 0;
        }
        *hit = mid;
        *t = frac;
        *(Vector4 *)plane = n->plane;
        return (SLOT_LOAD(u32 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x5C)))[n->material];
    }
    if (d0 < 0.0f && d1 > 0.0f) {
        Vector4 dir __attribute__((aligned(16)));
        Vector4 mid __attribute__((aligned(16)));
        PSVECSubtract(end, start, &dir);
        if (d0 != d1) {
            frac = d0 / (d0 - d1);
        } else {
            frac = 0.0f;
        }
        mid.x = dir.x * frac + start->x;
        mid.y = dir.y * frac + start->y;
        mid.z = dir.z * frac + start->z;
        if (n->back != 0xFFFF) {
            result = RayHitsBSP2(start, &mid, bsp, (void *)PTR(n->back), depth + 1, plane, t, hit);
            if (result) {
                *t *= frac;
                return result;
            }
        } else {
            *hit = mid;
            *t = frac;
            *(Vector4 *)plane = n->plane;
            return (SLOT_LOAD(u32 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x5C)))[n->material];
        }
        if (n->front != 0xFFFF) {
            result = RayHitsBSP2(&mid, end, bsp, (void *)PTR(n->front), depth + 1, plane, t, hit);
            if (result) {
                *t = *t * (1.0f - frac) + frac;
                if (frac == *t) {
                    *hit = mid;
                    *t = frac;
                    *(Vector4 *)plane = n->plane;
                    return (SLOT_LOAD(u32 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x5C)))[n->material];
                }
                return result;
            }
        }
        return 0;
    }
    if (d0 > 0.0f || d1 > 0.0f) {
        if (n->front != 0xFFFF) {
            return RayHitsBSP2(start, end, bsp, (void *)PTR(n->front), depth + 1, plane, t, hit);
        }
        return 0;
    }
    if (n->back != 0xFFFF) {
        return RayHitsBSP2(start, end, bsp, (void *)PTR(n->back), depth + 1, plane, t, hit);
    }
    *hit = *start;
    *t = 0.0f;
    *(Vector4 *)plane = n->plane;
    return (SLOT_LOAD(u32 *, ((u8 *)SLOT_LOAD(void *, g_worldData.world) + 0x5C)))[n->material];
}

u32 RayHitsBSP(Vector4 *start, Vector4 *dir, void *bbox, f32 *time, void *bsp) {
    f32 t;
    Vector4 end __attribute__((aligned(16)));
    Vector4 hit __attribute__((aligned(16)));
    u32 result;
    s8 inside;
    f32 eps;

    t = 1.0f;
    end.x = start->x + dir->x * *time;
    end.y = start->y + dir->y * *time;
    end.z = start->z + dir->z * *time;
    result = RayHitsBSP2(start, &end, bbox, NULL, 0, bsp, &t, &hit);
    if (result) {
        eps = 0.1f;
        if (hit.x < ((Vector4 *)bbox)[0].x - eps ||
            hit.x > ((Vector4 *)bbox)[1].x + eps ||
            hit.y < ((Vector4 *)bbox)[0].y - eps ||
            hit.y > ((Vector4 *)bbox)[1].y + eps ||
            hit.z < ((Vector4 *)bbox)[0].z - eps ||
            hit.z > ((Vector4 *)bbox)[1].z + eps) {
            inside = 0;
        } else {
            inside = 1;
        }
        if (inside) {
            *time = t * *time;
        } else {
            result = 0;
        }
    }
    return result;
}

s32 RayHitsCell(Vector4 *start, Vector4 *dir, u8 *cell, f32 *time, u8 *out) {
    extern Vector4 g_zeroVector;
    f32 RayHitsPoly(Vector4 *start, Vector4 *dir, Vector4 *poly, f32 t);
    s32 hitAny;
    u8 *poly;
    u8 *best;
    u32 i;
    f32 t;
    f32 minT;
    u32 result;
    Vector4 normal __attribute__((aligned(16)));

    hitAny = 0;
    if (*(u32 *)(SLOT_LOAD(u8 *, g_worldData.world) + 0x58) == 1) {
        if (*(u32 *)(cell + 0x30) != 0) {
            result = RayHitsBSP(start, dir, (Vector4 *)cell, time, &normal);
            if (result) {
                if (out != NULL) {
                    *(Vector4 *)(out + 0x10) = normal;
                    ((Vector4 *)out)->x = start->x + dir->x * *time;
                    ((Vector4 *)out)->y = start->y + dir->y * *time;
                    ((Vector4 *)out)->z = start->z + dir->z * *time;
                    *(u32 *)(out + 0x28) = result;
                    *(Vector4 *)(out + 0x30) = g_zeroVector;
                }
                hitAny = 1;
            }
        }
    } else {
        minT = *time;
        best = NULL;
        for (i = 0; i < *(u32 *)(cell + 0x3C); i++) {
            poly = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, g_worldData.world) + 0x4C)) + (SLOT_LOAD(u32 *, (cell + 0x38)))[i] * 0x20;
            if (*(u32 *)(poly + 0x18) != (u32)g_worldData.unk4) {
                *(u32 *)(poly + 0x18) = g_worldData.unk4;
                t = RayHitsPoly(start, dir, (Vector4 *)poly, minT);
                if (t < minT) {
                    minT = t;
                    best = poly;
                }
            }
        }
        if (minT != *time) {
            *time = minT;
            if (out != NULL) {
                *(Vector4 *)(out + 0x10) = *(Vector4 *)best;
                ((Vector4 *)out)->x = start->x + dir->x * *time;
                ((Vector4 *)out)->y = start->y + dir->y * *time;
                ((Vector4 *)out)->z = start->z + dir->z * *time;
                *(u32 *)(out + 0x28) = (SLOT_LOAD(u32 *, (SLOT_LOAD(u8 *, g_worldData.world) + 0x5C)))[*(u32 *)(best + 0x1C)];
                *(Vector4 *)(out + 0x30) = g_zeroVector;
            }
            hitAny = 1;
        }
    }
    return hitAny;
}

s8 VectorHitsTerrain(Vector4 *point) {
    TerrainGrid *grid;
    s32 x;
    s32 y;
    s32 z;
    s32 index;
    u32 node;
    u8 *cell;

    if (g_worldData.world == NULL || SLOT_LOAD(TerrainGrid *, g_worldData.world) == NULL) {
        return 0;
    }
    grid = SLOT_LOAD(TerrainGrid *, g_worldData.world);
    x = (point->x - grid->origin.x) / grid->sizeX;
    y = (point->y - grid->origin.y) / grid->sizeY;
    z = (point->z - grid->origin.z) / grid->sizeZ;
    if (x < 0 || x >= grid->numX) {
        index = -1;
    } else if (y < 0 || y >= grid->numY) {
        index = -1;
    } else if (z < 0 || z >= grid->numZ) {
        index = -1;
    } else {
        index = z * grid->numX + grid->numZ * (y * grid->numX) + x;
    }
    cell = index == -1 ? grid->cells : grid->cells + grid->cellIndex[index] * 0x50;
    node = 0;
    if (SLOT_LOAD(u8 *, (cell + 0x30)) == NULL) {
        return 0;
    }
    while (1) {
        if (Distance_Plane(SLOT_LOAD(u8 *, (cell + 0x30)) + node * 0x20, point) >= 0.0f) {
            node = *(u16 *)(SLOT_LOAD(u8 *, (cell + 0x30)) + node * 0x20 + 0x14);
            if (node == 0xFFFF) {
                return 0;
            }
        } else {
            node = *(u16 *)(SLOT_LOAD(u8 *, (cell + 0x30)) + node * 0x20 + 0x16);
            if (node == 0xFFFF) {
                return 1;
            }
        }
    }
}

s8 RayHitsTerrain_Aux3(Vector4 *start, Vector4 *dir, f32 *t, void *info) {
    s32 numX;
    s32 numY;
    s32 numZ;
    s32 x;
    s32 y;
    s32 z;
    s32 n;
    TerrainGrid *grid;
    f32 oy;
    f32 dtz;
    f32 ox;
    f32 dtx;
    f32 oz;
    f32 sx;
    f32 sy;
    f32 sz;
    f32 tx;
    f32 tz;
    f32 maxT;
    f32 ty;
    f32 dty;

    if (g_worldData.world == NULL || SLOT_LOAD(TerrainGrid *, g_worldData.world) == NULL) {
        return 0;
    }
    grid = SLOT_LOAD(TerrainGrid *, g_worldData.world);
    numX = grid->numX;
    numY = grid->numY;
    numZ = grid->numZ;
    ox = grid->origin.x;
    oy = grid->origin.y;
    oz = grid->origin.z;
    sx = grid->sizeX;
    sy = grid->sizeY;
    sz = grid->sizeZ;
    World_NextSweep();
    maxT = *t;
    x = (start->x - ox) / sx;
    y = (start->y - oy) / sy;
    z = (start->z - oz) / sz;
    if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(SLOT_LOAD(TerrainGrid *, g_worldData.world), x, y, z), t, info)) {
        return 1;
    }
    tx = ty = tz = dtx = *t;
    if (dir->x >= 0.0f) {
        if (x < numX - 1 && 0.0f != dir->x) {
            tx = (ox + ((f32)(x + 1) * sx - start->x)) / dir->x;
        }
    } else if (x > 0) {
        tx = (ox + ((f32)x * sx - start->x)) / dir->x;
    }
    if (dir->y >= 0.0f) {
        if (y < numY - 1 && 0.0f != dir->y) {
            ty = (oy + ((f32)(y + 1) * sy - start->y)) / dir->y;
        }
    } else if (y > 0) {
        ty = (oy + ((f32)y * sy - start->y)) / dir->y;
    }
    if (dir->z >= 0.0f) {
        if (z < numZ - 1 && 0.0f != dir->z) {
            tz = (oz + ((f32)(z + 1) * sz - start->z)) / dir->z;
        }
    } else if (z > 0) {
        tz = (oz + ((f32)z * sz - start->z)) / dir->z;
    }
    if (0.0f != dir->x) {
        dtx = _fabs(sx / dir->x);
    }
    if (0.0f != dir->y) {
        dty = _fabs(sy / dir->y);
    } else {
        dty = *t;
    }
    if (0.0f != dir->z) {
        dtz = _fabs(sz / dir->z);
    } else {
        dtz = *t;
    }
    while (tx < maxT || ty < maxT || tz < maxT) {
        if (tx <= tz && tx <= ty) {
            tx += dtx;
            if (dir->x >= 0.0f) {
                n = ++x;
                x = (f32)x;
                if (x >= numX) {
                    tx = maxT;
                }
            } else {
                n = --x;
                x = (f32)x;
                if (x <= 0) {
                    tx = maxT;
                }
            }
            if (n >= 0 && n < numX) {
                if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(SLOT_LOAD(TerrainGrid *, g_worldData.world), n, y, z), t, info)) {
                    return 1;
                }
            }
        } else if (tz <= ty && tz <= tx) {
            tz += dtz;
            if (dir->z >= 0.0f) {
                n = ++z;
                z = (f32)z;
                if (z >= numZ) {
                    tz = maxT;
                }
            } else {
                n = --z;
                z = (f32)z;
                if (z <= 0) {
                    tz = maxT;
                }
            }
            if (n >= 0 && n < numZ) {
                if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(SLOT_LOAD(TerrainGrid *, g_worldData.world), x, y, n), t, info)) {
                    return 1;
                }
            }
        } else {
            ty += dty;
            if (dir->y >= 0.0f) {
                n = ++y;
                y = (f32)y;
                if (y >= numY) {
                    ty = maxT;
                }
            } else {
                n = --y;
                y = (f32)y;
                if (y <= 0) {
                    ty = maxT;
                }
            }
            if (n >= 0 && n < numY) {
                if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(SLOT_LOAD(TerrainGrid *, g_worldData.world), x, n, z), t, info)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

void BuildOOB(Vector4 *bounds, Matrix4 *mtx, u8 *oob) {
    Vector4 z __attribute__((aligned(16)));
    Vector4 f __attribute__((aligned(16)));
    Vector4 b __attribute__((aligned(16)));
    Vector4 y __attribute__((aligned(16)));
    Vector4 x __attribute__((aligned(16)));
    Vector4 fr __attribute__((aligned(16)));
    Vector4 fl __attribute__((aligned(16)));
    Vector4 br __attribute__((aligned(16)));
    Vector4 bl __attribute__((aligned(16)));
    Vector4 center __attribute__((aligned(16)));
    Vector4 *corners = (Vector4 *)oob;
    f32 *ext = (f32 *)(oob + 0x90);
    Vector4 **axes = (Vector4 **)(oob + 0xA0);

    ext[0] = 0.5f * (bounds[1].z - bounds[0].z);
    ext[1] = 0.5f * (bounds[1].x - bounds[0].x);
    ext[2] = 0.5f * (bounds[1].y - bounds[0].y);
    axes[0] = &mtx->v[2];
    axes[1] = &mtx->v[0];
    axes[2] = &mtx->v[1];
    PSVECAdd(&bounds[0], &bounds[1], &center);
    PSVECScale(&center, &center, 0.5f);
    PSVECScale(&mtx->v[2], &corners[0], center.z);
    AccumulateVector(&mtx->v[0], &corners[0], &corners[0], center.x);
    AccumulateVector(&mtx->v[1], &corners[0], &corners[0], center.y);
    PSVECAdd(&mtx->v[3], &corners[0], &corners[0]);
    PSVECScale(axes[0], &z, ext[0]);
    PSVECAdd(&corners[0], &z, &f);
    PSVECSubtract(&corners[0], &z, &b);
    PSVECScale(axes[2], &y, ext[2]);
    PSVECAdd(&f, &y, &fr);
    PSVECSubtract(&f, &y, &fl);
    PSVECAdd(&b, &y, &br);
    PSVECSubtract(&b, &y, &bl);
    PSVECScale(axes[1], &x, ext[1]);
    PSVECAdd(&fr, &x, &corners[1]);
    PSVECSubtract(&fr, &x, &corners[2]);
    PSVECSubtract(&fl, &x, &corners[3]);
    PSVECAdd(&fl, &x, &corners[4]);
    PSVECAdd(&br, &x, &corners[5]);
    PSVECSubtract(&br, &x, &corners[6]);
    PSVECSubtract(&bl, &x, &corners[7]);
    PSVECAdd(&bl, &x, &corners[8]);
    *(u32 *)(oob + 0xCC) = 0;
}

void BuildOOBOffset(void *oob, void *pos, void *rot, void *out)
{
    void XformVec(void *, void *, void *);
    struct Local {
        double d[8];
    };
    struct Local local __attribute__((aligned(16)));

    local = *(struct Local *)pos;
    XformVec(pos, rot, &local.d[6]);
    BuildOOB(oob, &local, out);
    SLOT_STORE(((char *)out + 0xa0), (char *)pos + 0x20);
    SLOT_STORE(((char *)out + 0xa4), (char *)pos);
    SLOT_STORE(((char *)out + 0xa8), (char *)pos + 0x10);
}

void BuildOConvexHullOffset(void *out, void *hull, void *pos, void *rot) {
    typedef struct HullFace {
        Vector4 normal;
        u8 unk10[4];
        u32 *verts;
        u8 unk18[8];
    } HullFace;
    typedef struct Hull {
        u8 unk0[8];
        u32 numFaces;
        u32 numVerts;
        u8 unk10[8];
        HullFace *faces;
        Vector4 *verts;
    } Hull;
    typedef struct OConvexHull {
        Vector4 verts[32];
        Vector4 planes[16];
        Hull *hull;
        u8 unk304[0x3C4 - 0x304];
        s32 unk3C4;
        s32 unk3C8;
        u8 unk3CC[4];
        Vector4 pos;
    } OConvexHull;
    struct Local {
        double d[8];
    };
    void XformVec(void *, void *, void *);
    void XformVecOrientation(void *, Vector4 *, Vector4 *);
    Vector4 normal;
    struct Local local __attribute__((aligned(16)));
    u32 i;

    local = *(struct Local *)pos;
    XformVec(pos, rot, &local.d[6]);
    for (i = 0; i < ((Hull *)hull)->numVerts; i++) {
        XformVec(&local, &((Hull *)hull)->verts[i], &((OConvexHull *)out)->verts[i]);
    }
    {
        OConvexHull *o = (OConvexHull *)out;
        for (i = 0; i < ((Hull *)hull)->numFaces; i++) {
            XformVecOrientation(&local, &((Hull *)hull)->faces[i].normal, &normal);
            Create_Plane3(&o->planes[i], &normal,
                          &((OConvexHull *)out)->verts[*((Hull *)hull)->faces[i].verts]);
        }
    }
    ((OConvexHull *)out)->hull = hull;
    ((OConvexHull *)out)->unk3C8 = 0;
    ((OConvexHull *)out)->unk3C4 = 0;
    ((OConvexHull *)out)->pos = *(Vector4 *)&local.d[6];
}
