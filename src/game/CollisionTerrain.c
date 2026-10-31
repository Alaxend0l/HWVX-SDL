#include "common.h"
#include "World.h"



/* The world's collision grid (World.collision). */
#define TERRAIN() ((WorldCollision *)GETP(((World *)GETP(g_worldData.world))->collision))

/* The cell at grid square (x, y, z); cells[0] outside the grid. */
static inline TerrainCell *TerrainGrid_GetCellXYZ(WorldCollision *grid, s32 x, s32 y, s32 z) {
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
    return index == -1 ? GETP(grid->cells) : &GETP(grid->cells)[GETP(grid->cellIndex)[index]];
}

s8 RayHitsTerrain(Vector4 *start, Vector4 *end, void *arg2, void *arg3)
{
    Vector4 dir __attribute__((aligned(16)));

    PSVECSubtract(end, start, &dir);
    return RayHitsTerrain_Aux3(start, &dir, arg2, arg3);
}

s8 OBoxHitsTerrain(CollisionOBox *obox, Vector4 *vel, f32 *t, void *info) {
    Vector4 bounds[2];
    f32 best;
    s32 numX;
    s32 numY;
    s32 numZ;
    s32 x0;
    s32 x1;
    s32 y0;
    s32 y1;
    s32 z0;
    s32 z1;
    s32 x;
    s32 y;
    s32 z;
    s32 n;
    s32 index;
    u32 i;
    WorldCollision *grid;
    s8 hit;
    f32 oy;
    f32 dtz;
    f32 ox;
    f32 dtx;
    f32 oz;
    f32 dty;
    f32 sx;
    f32 sy;
    f32 spanX;
    f32 sz;
    f32 spanZ;
    f32 tx;
    f32 tz;
    f32 spanY;
    f32 ty;

    grid = TERRAIN();
    if (GETP(g_worldData.world) == NULL || grid == NULL) {
        return 0;
    }
    numX = grid->numX;
    numY = grid->numY;
    numZ = grid->numZ;
    hit = 0;
    ox = grid->min.x;
    oy = grid->min.y;
    oz = grid->min.z;
    sx = grid->cellSize[0];
    sy = grid->cellSize[1];
    sz = grid->cellSize[2];
    bounds[0].x = 99999.0f;
    bounds[0].y = 99999.0f;
    bounds[0].z = 99999.0f;
    bounds[1].x = -99999.0f;
    bounds[1].y = -99999.0f;
    bounds[1].z = -99999.0f;
    for (i = 0; i < 8; i++) {
        MinMaxVec(&bounds[0], &bounds[1], &obox->corners[i]);
    }
    World_NextSweep();
    best = *t;
    x0 = (bounds[0].x - ox) / sx;
    x1 = (bounds[1].x - ox) / sx;
    y0 = (bounds[0].y - oy) / sy;
    y1 = (bounds[1].y - oy) / sy;
    z0 = (bounds[0].z - oz) / sz;
    z1 = (bounds[1].z - oz) / sz;
    if (x1 < 0) {
        return 0;
    }
    if ((u32)x1 > (u32)grid->numX) {
        x1 = grid->numX - 1;
    }
    if (x0 < 0) {
        x0 = 0;
    } else if (x0 > x1) {
        x0 = x1;
    }
    if (y1 < 0) {
        return 0;
    }
    if ((u32)y1 > (u32)grid->numY) {
        y1 = grid->numY - 1;
    }
    if (y0 < 0) {
        y0 = 0;
    } else if (y0 > y1) {
        y0 = y1;
    }
    if (z1 < 0) {
        return 0;
    }
    if ((u32)z1 > (u32)grid->numZ) {
        z1 = grid->numZ - 1;
    }
    if (z0 < 0) {
        z0 = 0;
    } else if (z0 > z1) {
        z0 = z1;
    }
    for (x = x0; x <= x1; x++) {
        for (y = y0; y <= y1; y++) {
            for (z = z0; z <= z1; z++) {
                grid = TERRAIN();
                if (x < 0 || x >= grid->numX) {
                    index = -1;
                } else if (y < 0 || y >= grid->numY) {
                    index = -1;
                } else if (z < 0 || z >= grid->numZ) {
                    index = -1;
                } else {
                    index = z * grid->numX + grid->numZ * (y * grid->numX) + x;
                }
                if (OBoxHitsCell(obox, vel, index == -1 ? GETP(grid->cells) : &GETP(grid->cells)[GETP(grid->cellIndex)[index]], &best, info)) {
                    hit = 1;
                }
            }
        }
    }
    if (hit) {
        *t = best;
        return 1;
    }
    tx = ty = tz = *t;
    if (vel->x >= 0.0f) {
        if (x1 < numX - 1 && 0.0f != vel->x) {
            tx = (ox + ((f32)(x1 + 1) * sx - bounds[1].x)) / vel->x;
        }
    } else if (x0 > 0) {
        tx = (ox + ((f32)x0 * sx - bounds[0].x)) / vel->x;
    }
    if (vel->y >= 0.0f) {
        if (y1 < numY - 1 && 0.0f != vel->y) {
            ty = (oy + ((f32)(y1 + 1) * sy - bounds[1].y)) / vel->y;
        }
    } else if (y0 > 0) {
        ty = (oy + ((f32)y0 * sy - bounds[0].y)) / vel->y;
    }
    if (vel->z >= 0.0f) {
        if (z1 < numZ - 1 && 0.0f != vel->z) {
            tz = (oz + ((f32)(z1 + 1) * sz - bounds[1].z)) / vel->z;
        }
    } else if (z0 > 0) {
        tz = (oz + ((f32)z0 * sz - bounds[0].z)) / vel->z;
    }
    spanX = (bounds[1].x - bounds[0].x) / sx;
    spanY = (bounds[1].y = bounds[0].y) / sy;  /* sic: = for -, as in the asm */
    spanZ = (bounds[1].z - bounds[0].z) / sz;
    if (0.0f != vel->x) {
        dtx = _fabs(sx / vel->x);
    } else {
        dtx = *t;
    }
    if (0.0f != vel->y) {
        dty = _fabs(sx / vel->y);  /* sic: sx, as in the asm */
    } else {
        dty = *t;
    }
    if (0.0f != vel->z) {
        dtz = _fabs(sz / vel->z);
    } else {
        dtz = *t;
    }
    while (tx < best || ty < best || tz < best) {
        if (tx < tz && tx < ty) {
            tx += dtx;
            if (vel->x > 0.0f) {
                n = ++x1;
                x0 = (f32)x1 - spanX;
                if (x1 >= numX) {
                    tx = best;
                }
            } else {
                n = --x0;
                x1 = (f32)x0 + spanX;
                if (x0 <= 0) {
                    tx = best;
                }
            }
            if (n >= 0 && n < numX) {
                for (y = y0; y <= y1; y++) {
                    for (z = z0; z <= z1; z++) {
                        grid = TERRAIN();
                        if (n < 0 || n >= grid->numX) {
                            index = -1;
                        } else if (y < 0 || y >= grid->numY) {
                            index = -1;
                        } else if (z < 0 || z >= grid->numZ) {
                            index = -1;
                        } else {
                            index = z * grid->numX + grid->numZ * (y * grid->numX) + n;
                        }
                        if (OBoxHitsCell(obox, vel, index == -1 ? GETP(grid->cells) : &GETP(grid->cells)[GETP(grid->cellIndex)[index]], &best, info)) {
                            hit = 1;
                        }
                    }
                }
                if (hit) {
                    *t = best;
                    return 1;
                }
            }
        } else if (tz < tx && tz < ty) {
            tz += dtz;
            if (vel->z > 0.0f) {
                n = ++z1;
                z0 = (f32)z1 - spanZ;
                if (z1 >= numZ) {
                    tz = best;
                }
            } else {
                n = --z0;
                z1 = (f32)z0 + spanZ;
                if (z0 <= 0) {
                    tz = best;
                }
            }
            if (n >= 0 && n < numZ) {
                for (x = x0; x <= x1; x++) {
                    for (y = y0; y <= y1; y++) {
                        grid = TERRAIN();
                        if (x < 0 || x >= grid->numX) {
                            index = -1;
                        } else if (y < 0 || y >= grid->numY) {
                            index = -1;
                        } else if (n < 0 || n >= grid->numZ) {
                            index = -1;
                        } else {
                            index = n * grid->numX + grid->numZ * (y * grid->numX) + x;
                        }
                        if (OBoxHitsCell(obox, vel, index == -1 ? GETP(grid->cells) : &GETP(grid->cells)[GETP(grid->cellIndex)[index]], &best, info)) {
                            hit = 1;
                        }
                    }
                }
                if (hit) {
                    *t = best;
                    return 1;
                }
            }
        } else {
            ty += dty;
            if (vel->y > 0.0f) {
                n = ++y1;
                y0 = (f32)y1 - spanY;
                if (y1 >= numY) {
                    ty = best;
                }
            } else {
                n = --y0;
                y1 = (f32)y0 + spanY;
                if (y0 <= 0) {
                    ty = best;
                }
            }
            if (n >= 0 && n < numY) {
                for (x = x0; x <= x1; x++) {
                    for (z = z0; z <= z1; z++) {
                        grid = TERRAIN();
                        if (x < 0 || x >= grid->numX) {
                            index = -1;
                        } else if (n < 0 || n >= grid->numY) {
                            index = -1;
                        } else if (z < 0 || z >= grid->numZ) {
                            index = -1;
                        } else {
                            index = z * grid->numX + grid->numZ * (n * grid->numX) + x;
                        }
                        if (OBoxHitsCell(obox, vel, index == -1 ? GETP(grid->cells) : &GETP(grid->cells)[GETP(grid->cellIndex)[index]], &best, info)) {
                            hit = 1;
                        }
                    }
                }
                if (hit) {
                    *t = best;
                    return 1;
                }
            }
        }
    }
    return 0;
}




s8 OBoxHitsTerrainZeroTimeEdgesOnly(CollisionOBox *obox, void *arg1) {
    WorldCollision *grid;
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

    if (GETP(g_worldData.world) == NULL || TERRAIN() == NULL) {
        return 0;
    }
    grid = TERRAIN();
    ox = grid->min.x;
    oy = grid->min.y;
    oz = grid->min.z;
    sx = grid->cellSize[0];
    sy = grid->cellSize[1];
    sz = grid->cellSize[2];
    bounds[0].x = 99999.0f;
    bounds[0].y = 99999.0f;
    bounds[0].z = 99999.0f;
    bounds[0].w = 99999.0f;
    bounds[1].x = -99999.0f;
    bounds[1].y = -99999.0f;
    bounds[1].z = -99999.0f;
    bounds[1].w = -99999.0f;
    for (i = 0; i < 8; i++) {
        MinMaxVec(&bounds[0], &bounds[1], &obox->corners[i]);
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
                if (OBoxHitsCellZeroTimeEdgesOnly(obox, TerrainGrid_GetCellXYZ(TERRAIN(), x, y, z), arg1)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}



s8 OBoxHitsCellZeroTimeEdgesOnly(CollisionOBox *obox, TerrainCell *cell, void *arg2)
{
    s8 hit = 0;
    u32 i;
    TerrainPoly *poly;

    obox->centerProj[0] = 0.0f;
    obox->centerProj[1] = 0.0f;
    obox->centerProj[2] = 0.0f;
    obox->velProj[0] = 0.0f;
    obox->velProj[1] = 0.0f;
    obox->velProj[2] = 0.0f;
    if (TERRAIN()->mode == 1)
    {
        hit = OBoxHitsBSPZeroTimeEdgesOnly(obox, cell, arg2);
    }
    else
    {
        for (i = 0; i < cell->numPolys; i++)
        {
            poly = &GETP(TERRAIN()->polys)[GETP(cell->polys)[i]];
            if (poly->sweep != g_worldData.sweep)
            {
                poly->sweep = g_worldData.sweep;
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

static inline s8 OBoxHitsCellZeroTime(CollisionOBox *obox, TerrainCell *cell, void *arg2)
{
    s8 hit = 0;
    u32 i;
    TerrainPoly *poly;

    obox->centerProj[0] = 0.0f;
    obox->centerProj[1] = 0.0f;
    obox->centerProj[2] = 0.0f;
    obox->velProj[0] = 0.0f;
    obox->velProj[1] = 0.0f;
    obox->velProj[2] = 0.0f;
    if (TERRAIN()->mode == 1)
    {
        hit = OBoxHitsBSPZeroTime(obox, cell, arg2);
    }
    else
    {
        for (i = 0; i < cell->numPolys; i++)
        {
            poly = &GETP(TERRAIN()->polys)[GETP(cell->polys)[i]];
            if (poly->sweep != g_worldData.sweep)
            {
                poly->sweep = g_worldData.sweep;
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

s8 OBoxHitsCell(CollisionOBox *obox, Vector4 *vel, TerrainCell *cell, f32 *time, void *arg4) {
    extern Vector4 g_zeroVector;
    s8 hit = 0;
    u32 i;
    TerrainPoly *poly;
    f32 best;
    f32 t;

    if (vel == &g_zeroVector) {
        return OBoxHitsCellZeroTime(obox, cell, arg4);
    }
    obox->centerProj[0] = PSVECDotProduct(GETP(obox->axis[0]), obox);
    obox->centerProj[1] = PSVECDotProduct(GETP(obox->axis[1]), obox);
    obox->centerProj[2] = PSVECDotProduct(GETP(obox->axis[2]), obox);
    obox->velProj[0] = -PSVECDotProduct(GETP(obox->axis[0]), vel);
    obox->velProj[1] = -PSVECDotProduct(GETP(obox->axis[1]), vel);
    obox->velProj[2] = -PSVECDotProduct(GETP(obox->axis[2]), vel);
    if (TERRAIN()->mode == 1) {
        hit = OBoxHitsBSP(obox, vel, cell, time, arg4);
    } else {
        best = *time;
        for (i = 0; i < cell->numPolys; i++) {
            poly = &GETP(TERRAIN()->polys)[GETP(cell->polys)[i]];
            if (poly->sweep != g_worldData.sweep) {
                poly->sweep = g_worldData.sweep;
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

s8 OBoxHitsBSP(CollisionOBox *obox, Vector4 *vel, TerrainCell *bsp, f32 *time, void *arg4)
{
    Vector4 corner __attribute__((aligned(16)));
    Vector4 hitNormal __attribute__((aligned(16)));
    Vector4 normal __attribute__((aligned(16)));
    u8 order[8] = {2, 3, 6, 7, 0, 1, 4, 5};
    TerrainEdge *edge;
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
    if (bsp->numPolys == 0 && bsp->numEdges == 0)
    {
        return 0;
    }
    for (i = 0; i < bsp->numEdges && t > 0.0f; i++)
    {
        edge = &GETP(TERRAIN()->edges)[GETP(bsp->edges)[i]];
        if (edge->sweep != g_worldData.sweep)
        {
            edge->sweep = g_worldData.sweep;
            hit = OBoxHitsSegment(obox, vel, edge, t, arg4);
            if (hit < t)
            {
                t = hit;
                hitEdge = 1;
                if (arg4 != NULL)
                {
                    ((CollisionInfo *)arg4)->material = GETP(TERRAIN()->materials)[edge->material];
                }
            }
        }
    }
    for (i = 0; i < 8 && t > 0.0f; i++)
    {
        m = RayHitsBSP(&obox->corners[order[i]], vel, bsp, &t, &hitNormal);
        if (m != 0)
        {
            material = m;
            hitCorner = 1;
            corner = obox->corners[order[i]];
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
        ((CollisionInfo *)arg4)->thingA = 0;
        ((CollisionInfo *)arg4)->thingB = 0;
        ((CollisionInfo *)arg4)->normal = normal;
    }
    *time = t;
    return 1;
}

static inline s8 PointInsideBSP(TerrainCell *bsp, void *point) {
    u32 node;

    node = 0;
    if (GETP(bsp->nodes) == NULL) {
        return 0;
    }
    while (1) {
        if (Distance_Plane(&GETP(bsp->nodes)[node].plane, point) >= 0.0f) {
            node = GETP(bsp->nodes)[node].front;
            if (node == BSP_LEAF) {
                return 0;
            }
        } else {
            node = GETP(bsp->nodes)[node].back;
            if (node == BSP_LEAF) {
                return 1;
            }
        }
    }
}

s8 OBoxHitsBSPZeroTime(CollisionOBox *obox, TerrainCell *bsp, void *arg2) {
    TerrainEdge *edge;
    u32 i;

    if (bsp->numPolys == 0 && bsp->numEdges == 0) {
        return 0;
    }
    for (i = 0; i < bsp->numEdges; i++) {
        edge = &GETP(TERRAIN()->edges)[GETP(bsp->edges)[i]];
        if (edge->sweep != g_worldData.sweep) {
            edge->sweep = g_worldData.sweep;
            if (OBoxHitsSegmentZeroTime(obox, edge, arg2)) {
                return 1;
            }
        }
    }
    for (i = 0; i < 8; i++) {
        if (PointInsideBSP(bsp, &obox->corners[i])) {
            return 1;
        }
    }
    return 0;
}

s8 OBoxHitsBSPZeroTimeEdgesOnly(CollisionOBox *obox, TerrainCell *bsp, void *arg2)
{
    TerrainEdge *edge;
    u32 i;

    if (bsp->numPolys == 0 && bsp->numEdges == 0)
    {
        return 0;
    }
    for (i = 0; i < bsp->numEdges; i++)
    {
        edge = &GETP(TERRAIN()->edges)[GETP(bsp->edges)[i]];
        if (edge->sweep != g_worldData.sweep)
        {
            edge->sweep = g_worldData.sweep;
            if (OBoxHitsSegmentZeroTime(obox, edge, arg2))
            {
                return 1;
            }
        }
    }
    return 0;
}

u32 RayHitsBSP2(Vector4 *start, Vector4 *end, TerrainCell *bsp, u32 node, s32 depth, void *plane, f32 *t, Vector4 *hit) {
    TerrainBSPNode *n;
    f32 d0;
    f32 d1;
    f32 frac;
    u32 result;

    n = &GETP(bsp->nodes)[node];
    d0 = Distance_Plane(n, start);
    d1 = Distance_Plane(n, end);
    if (d0 == 0.0f && d1 == 0.0f) {
        if (n->front == BSP_LEAF) {
            return 0;
        }
        return RayHitsBSP2(start, end, bsp, n->front, depth + 1, plane, t, hit);
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
        if (n->front != BSP_LEAF) {
            result = RayHitsBSP2(start, &mid, bsp, n->front, depth + 1, plane, t, hit);
            if (result) {
                *t *= frac;
                return result;
            }
        }
        if (n->back != BSP_LEAF) {
            result = RayHitsBSP2(&mid, end, bsp, n->back, depth + 1, plane, t, hit);
            if (result) {
                *t = *t * (1.0f - frac) + frac;
                if (frac == *t) {
                    *hit = mid;
                    *t = frac;
                    *(Vector4 *)plane = n->plane;
                    return GETP(TERRAIN()->materials)[n->material];
                }
                return result;
            }
            return 0;
        }
        *hit = mid;
        *t = frac;
        *(Vector4 *)plane = n->plane;
        return GETP(TERRAIN()->materials)[n->material];
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
        if (n->back != BSP_LEAF) {
            result = RayHitsBSP2(start, &mid, bsp, n->back, depth + 1, plane, t, hit);
            if (result) {
                *t *= frac;
                return result;
            }
        } else {
            *hit = mid;
            *t = frac;
            *(Vector4 *)plane = n->plane;
            return GETP(TERRAIN()->materials)[n->material];
        }
        if (n->front != BSP_LEAF) {
            result = RayHitsBSP2(&mid, end, bsp, n->front, depth + 1, plane, t, hit);
            if (result) {
                *t = *t * (1.0f - frac) + frac;
                if (frac == *t) {
                    *hit = mid;
                    *t = frac;
                    *(Vector4 *)plane = n->plane;
                    return GETP(TERRAIN()->materials)[n->material];
                }
                return result;
            }
        }
        return 0;
    }
    if (d0 > 0.0f || d1 > 0.0f) {
        if (n->front != BSP_LEAF) {
            return RayHitsBSP2(start, end, bsp, n->front, depth + 1, plane, t, hit);
        }
        return 0;
    }
    if (n->back != BSP_LEAF) {
        return RayHitsBSP2(start, end, bsp, n->back, depth + 1, plane, t, hit);
    }
    *hit = *start;
    *t = 0.0f;
    *(Vector4 *)plane = n->plane;
    return GETP(TERRAIN()->materials)[n->material];
}

/* A ray against a cell's BSP; hits outside the cell's bounds are dropped.
 * Returns the surface hit, 0: none. */
u32 RayHitsBSP(Vector4 *start, Vector4 *dir, TerrainCell *cell, f32 *time, Vector4 *plane) {
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
    result = RayHitsBSP2(start, &end, cell, 0, 0, plane, &t, &hit);
    if (result) {
        eps = 0.1f;
        if (hit.x < cell->min.x - eps ||
            hit.x > cell->max.x + eps ||
            hit.y < cell->min.y - eps ||
            hit.y > cell->max.y + eps ||
            hit.z < cell->min.z - eps ||
            hit.z > cell->max.z + eps) {
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

s32 RayHitsCell(Vector4 *start, Vector4 *dir, TerrainCell *cell, f32 *time, CollisionInfo *out) {
    extern Vector4 g_zeroVector;
    s32 hitAny;
    TerrainPoly *poly;
    TerrainPoly *best;
    u32 i;
    f32 t;
    f32 minT;
    u32 result;
    Vector4 normal __attribute__((aligned(16)));

    hitAny = 0;
    if (TERRAIN()->mode == 1) {
        if (GETP(cell->nodes) != NULL) {
            result = RayHitsBSP(start, dir, cell, time, &normal);
            if (result) {
                if (out != NULL) {
                    out->normal = normal;
                    out->pos.x = start->x + dir->x * *time;
                    out->pos.y = start->y + dir->y * *time;
                    out->pos.z = start->z + dir->z * *time;
                    out->material = result;
                    out->unk30 = g_zeroVector;
                }
                hitAny = 1;
            }
        }
    } else {
        minT = *time;
        best = NULL;
        for (i = 0; i < cell->numPolys; i++) {
            poly = &GETP(TERRAIN()->polys)[GETP(cell->polys)[i]];
            if (poly->sweep != (u32)g_worldData.sweep) {
                poly->sweep = g_worldData.sweep;
                t = RayHitsPoly(start, dir, poly, minT);
                if (t < minT) {
                    minT = t;
                    best = poly;
                }
            }
        }
        if (minT != *time) {
            *time = minT;
            if (out != NULL) {
                out->normal = best->plane;
                out->pos.x = start->x + dir->x * *time;
                out->pos.y = start->y + dir->y * *time;
                out->pos.z = start->z + dir->z * *time;
                out->material = GETP(TERRAIN()->materials)[best->material];
                out->unk30 = g_zeroVector;
            }
            hitAny = 1;
        }
    }
    return hitAny;
}

s8 VectorHitsTerrain(Vector4 *point) {
    WorldCollision *grid;
    s32 x;
    s32 y;
    s32 z;
    s32 index;
    u32 node;
    TerrainCell *cell;

    if (GETP(g_worldData.world) == NULL || TERRAIN() == NULL) {
        return 0;
    }
    grid = TERRAIN();
    x = (point->x - grid->min.x) / grid->cellSize[0];
    y = (point->y - grid->min.y) / grid->cellSize[1];
    z = (point->z - grid->min.z) / grid->cellSize[2];
    if (x < 0 || x >= grid->numX) {
        index = -1;
    } else if (y < 0 || y >= grid->numY) {
        index = -1;
    } else if (z < 0 || z >= grid->numZ) {
        index = -1;
    } else {
        index = z * grid->numX + grid->numZ * (y * grid->numX) + x;
    }
    cell = index == -1 ? GETP(grid->cells) : &GETP(grid->cells)[GETP(grid->cellIndex)[index]];
    node = 0;
    if (GETP(cell->nodes) == NULL) {
        return 0;
    }
    while (1) {
        if (Distance_Plane(&GETP(cell->nodes)[node].plane, point) >= 0.0f) {
            node = GETP(cell->nodes)[node].front;
            if (node == BSP_LEAF) {
                return 0;
            }
        } else {
            node = GETP(cell->nodes)[node].back;
            if (node == BSP_LEAF) {
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
    WorldCollision *grid;
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

    if (GETP(g_worldData.world) == NULL || TERRAIN() == NULL) {
        return 0;
    }
    grid = TERRAIN();
    numX = grid->numX;
    numY = grid->numY;
    numZ = grid->numZ;
    ox = grid->min.x;
    oy = grid->min.y;
    oz = grid->min.z;
    sx = grid->cellSize[0];
    sy = grid->cellSize[1];
    sz = grid->cellSize[2];
    World_NextSweep();
    maxT = *t;
    x = (start->x - ox) / sx;
    y = (start->y - oy) / sy;
    z = (start->z - oz) / sz;
    if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(TERRAIN(), x, y, z), t, info)) {
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
                if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(TERRAIN(), n, y, z), t, info)) {
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
                if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(TERRAIN(), x, y, n), t, info)) {
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
                if ((s8)RayHitsCell(start, dir, TerrainGrid_GetCellXYZ(TERRAIN(), x, n, z), t, info)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Builds an oriented box from a local box (bounds: min, max) and transform.
 * The axes are the transform's z, x and y rows, which stay referenced. */
void BuildOOB(Vector4 *bounds, Matrix4 *mtx, CollisionOBox *oob) {
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
    f32 *ext = oob->extent;
    Vector4 *axes[3];  /* port: stored below as handles (CollisionOBox.axis) */

    ext[0] = 0.5f * (bounds[1].z - bounds[0].z);
    ext[1] = 0.5f * (bounds[1].x - bounds[0].x);
    ext[2] = 0.5f * (bounds[1].y - bounds[0].y);
    axes[0] = &mtx->v[2];
    axes[1] = &mtx->v[0];
    axes[2] = &mtx->v[1];
    SETP(oob->axis[0], axes[0]);
    SETP(oob->axis[1], axes[1]);
    SETP(oob->axis[2], axes[2]);
    PSVECAdd(&bounds[0], &bounds[1], &center);
    PSVECScale(&center, &center, 0.5f);
    PSVECScale(&mtx->v[2], &oob->center, center.z);
    AccumulateVector(&mtx->v[0], &oob->center, &oob->center, center.x);
    AccumulateVector(&mtx->v[1], &oob->center, &oob->center, center.y);
    PSVECAdd(&mtx->v[3], &oob->center, &oob->center);
    PSVECScale(axes[0], &z, ext[0]);
    PSVECAdd(&oob->center, &z, &f);
    PSVECSubtract(&oob->center, &z, &b);
    PSVECScale(axes[2], &y, ext[2]);
    PSVECAdd(&f, &y, &fr);
    PSVECSubtract(&f, &y, &fl);
    PSVECAdd(&b, &y, &br);
    PSVECSubtract(&b, &y, &bl);
    PSVECScale(axes[1], &x, ext[1]);
    PSVECAdd(&fr, &x, &oob->corners[0]);
    PSVECSubtract(&fr, &x, &oob->corners[1]);
    PSVECSubtract(&fl, &x, &oob->corners[2]);
    PSVECAdd(&fl, &x, &oob->corners[3]);
    PSVECAdd(&br, &x, &oob->corners[4]);
    PSVECSubtract(&br, &x, &oob->corners[5]);
    PSVECSubtract(&bl, &x, &oob->corners[6]);
    PSVECAdd(&bl, &x, &oob->corners[7]);
    oob->cachedPoly = 0;
}

/* BuildOOB with the box's origin moved to rot in mtx's space. The axes are
 * then pointed back at mtx, as the local copy goes away. */
void BuildOOBOffset(Vector4 *bounds, Matrix4 *mtx, Vector4 *rot, CollisionOBox *out)
{
    Matrix4 local __attribute__((aligned(16)));

    local = *mtx;
    XformVec(mtx, rot, &local.v[3]);
    BuildOOB(bounds, &local, out);
    SETP(out->axis[0], &mtx->v[2]);
    SETP(out->axis[1], &mtx->v[0]);
    SETP(out->axis[2], &mtx->v[1]);
}

void BuildOConvexHullOffset(void *out, void *hull, void *pos, void *rot) {
    typedef struct HullFace {
        Vector4 normal;
        u8 unk10[4];
        PTR32(u32) verts;
        u8 unk18[8];
    } HullFace;
    typedef struct Hull {
        u8 unk0[8];
        u32 numFaces;
        u32 numVerts;
        u8 unk10[8];
        PTR32(HullFace) faces;
        PTR32(Vector4) verts;
    } Hull;
    typedef struct OConvexHull {
        Vector4 verts[32];
        Vector4 planes[16];
        PTR32(Hull) hull;
        u8 unk304[0x3C4 - 0x304];
        s32 unk3C4;
        s32 unk3C8;
        u8 unk3CC[4];
        Vector4 pos;
    } OConvexHull;
    struct Local {
        double d[8];
    };
    Vector4 normal;
    struct Local local __attribute__((aligned(16)));
    u32 i;

    local = *(struct Local *)pos;
    XformVec(pos, rot, &local.d[6]);
    for (i = 0; i < ((Hull *)hull)->numVerts; i++) {
        XformVec(&local, &GETP(((Hull *)hull)->verts)[i], &((OConvexHull *)out)->verts[i]);
    }
    {
        OConvexHull *o = (OConvexHull *)out;
        for (i = 0; i < ((Hull *)hull)->numFaces; i++) {
            XformVecOrientation(&local, &GETP(((Hull *)hull)->faces)[i].normal, &normal);
            Create_Plane3(&o->planes[i], &normal,
                          &((OConvexHull *)out)->verts[*GETP(GETP(((Hull *)hull)->faces)[i].verts)]);
        }
    }
    SETP(((OConvexHull *)out)->hull, hull);
    ((OConvexHull *)out)->unk3C8 = 0;
    ((OConvexHull *)out)->unk3C4 = 0;
    ((OConvexHull *)out)->pos = *(Vector4 *)&local.d[6];
}
