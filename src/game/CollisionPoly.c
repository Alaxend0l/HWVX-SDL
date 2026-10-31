#include "common.h"
#include "World.h"


/* Polys are TerrainPoly (Collision.h); hits are CollisionInfo. */

f32 RayHitsPoly(Vector4 *start, Vector4 *dir, TerrainPoly *p, f32 t) {
    Vector4 edge __attribute__((aligned(16)));
    Vector4 normal __attribute__((aligned(16)));
    u32 i;
    u32 prev;
    f32 denom;
    f32 d0;
    f32 d1;
    f32 base;
    f32 a;
    f32 b;

    denom = PSVECDotProduct(&p->plane, dir);
    if (denom >= 0.0f) {
        return t;
    }
    d0 = Distance_Plane(p, start);
    if (d0 < 0.0f) {
        return t;
    }
    d1 = denom * t + d0;
    if (d1 > 0.0f) {
        return t;
    }
    prev = p->numVerts - 1;
    for (i = 0; i < p->numVerts; i++) {
        PSVECSubtract(&GETP(p->verts)[i], &GETP(p->verts)[prev], &edge);
        PSVECCrossProduct(dir, &edge, &normal);
        base = PSVECDotProduct(&normal, start);
        a = PSVECDotProduct(&normal, &GETP(p->verts)[i]);
        b = PSVECDotProduct(&normal, &GETP(p->verts)[(u32)GETP(p->verts)[i].w]);
        if ((a - base) * (b - base) > 0.0f) {
            return t;
        }
        prev = i;
    }
    if (d0 && t && d0 != d1) {
        return t * (d0 / (d0 - d1));
    }
    return 0.0f;
}

f32 OOBHitsPoly(CollisionOBox *box, TerrainPoly *poly, Vector4 *vel, CollisionInfo *hit, f32 maxT) {
    f32 planeD;
    DividingAxis da;
    Vector4 edge __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    Vector4 p0 __attribute__((aligned(16)));
    Vector4 p1 __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 start __attribute__((aligned(16)));
    Vector4 end __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 cross __attribute__((aligned(16)));
    Vector4 corner __attribute__((aligned(16)));
    Vector4 cornerEnd __attribute__((aligned(16)));
    u8 hiIdx;
    s32 j;
    u8 type;
    u32 cornerIdx;
    u8 axisIdx;
    u8 edgeIdx;
    u8 edgeVert;
    s32 i;
    u8 loIdx;
    u8 vert;
    u8 minIdx;
    u8 maxIdx;
    u32 k;
    s32 prev;
    f32 a;
    f32 b;
    f32 d;
    f32 d2;

    planeD = -poly->plane.w;
    da.tEnter = 0.0f;
    da.tExit = maxT;
    type = 0;
    axisIdx = 0;
    vert = 99;
    edgeIdx = 99;
    for (i = 0; i < 3; i++) {
        minIdx = 0;
        maxIdx = 0;
        da.minA = da.maxA = PSVECDotProduct(GETP(box->axis[i]), &GETP(poly->verts)[0]);
        for (k = 1; k < poly->numVerts; k++) {
            d = PSVECDotProduct(GETP(box->axis[i]), &GETP(poly->verts)[k]);
            if (d < da.minA) {
                da.minA = d;
                minIdx = k;
            } else if (d > da.maxA) {
                da.maxA = d;
                maxIdx = k;
            }
        }
        da.dir = -PSVECDotProduct(GETP(box->axis[i]), vel);
        d = PSVECDotProduct(GETP(box->axis[i]), &box->center);
        da.maxB = d + box->extent[i];
        da.minB = d - box->extent[i];
        switch ((u8)Collision_ProcessDividingAxis(&da)) {
        case 0:
            return maxT;
        case 2:
            vert = minIdx;
            axisIdx = i + 3;
            type = 1;
            break;
        case 3:
            vert = maxIdx;
            axisIdx = i;
            type = 1;
            break;
        }
    }
    cornerIdx = 0;
    da.minB = da.maxB = PSVECDotProduct(&poly->plane, &box->corners[0]);
    for (k = 1; k < 8; k++) {
        d = PSVECDotProduct(&poly->plane, &box->corners[k]);
        if (d < da.minB) {
            da.minB = d;
            cornerIdx = (u8)k;
        } else if (d > da.maxB) {
            da.maxB = d;
        }
    }
    da.dir = -PSVECDotProduct(&poly->plane, vel);
    da.minA = planeD;
    da.maxA = planeD;
    switch ((u8)Collision_ProcessDividingAxis(&da)) {
    case 0:
        return maxT;
    case 2:
        type = 2;
        break;
    case 3:
        type = 2;
        break;
    }
    prev = poly->numVerts - 1;
    for (j = 0; j < (s32)poly->numVerts; j++) {
        PSVECSubtract(&GETP(poly->verts)[j], &GETP(poly->verts)[prev], &edge);
        for (i = 0; i < 3; i++) {
            u8 edgeMax = 0;
            u8 edgeMin = 0;
            s32 m;

            PSVECCrossProduct(GETP(box->axis[i]), &edge, &axis);
            da.minB = da.maxB = PSVECDotProduct(&axis, &box->corners[0]);
            for (m = 1; m < 8; m++) {
                d = PSVECDotProduct(&axis, &box->corners[m]);
                if (d < da.minB) {
                    da.minB = d;
                    edgeMin = m;
                } else if (d > da.maxB) {
                    edgeMax = m;
                    da.maxB = d;
                }
            }
            a = PSVECDotProduct(&axis, &GETP(poly->verts)[j]);
            b = PSVECDotProduct(&axis, &GETP(poly->verts)[(u32)GETP(poly->verts)[j].w]);
            if (a > b) {
                da.minA = b;
                hiIdx = j;
                da.maxA = a;
                loIdx = GETP(poly->verts)[j].w;
            } else {
                da.minA = a;
                loIdx = j;
                da.maxA = b;
                hiIdx = GETP(poly->verts)[j].w;
            }
            da.dir = -PSVECDotProduct(&axis, vel);
            switch ((u8)Collision_ProcessDividingAxis(&da)) {
            case 0:
                return maxT;
            case 2:
                edgeVert = loIdx;
                edgeIdx = j;
                axisIdx = i;
                cornerIdx = edgeMax;
                type = 3;
                break;
            case 3:
                edgeVert = hiIdx;
                edgeIdx = j;
                axisIdx = i;
                cornerIdx = edgeMin;
                type = 3;
                break;
            }
        }
        prev = j;
    }
    if (hit != NULL && da.tEnter < maxT) {
        hit->type = type;
        hit->material = GETP(((WorldCollision *)GETP(((World *)GETP(g_worldData.world))->collision))->materials)[poly->material];
        box->cachedPoly = (u32)HND(poly);
        switch (type) {
        case 1:
            hit->pos = GETP(poly->verts)[vert];
            hit->normal = *GETP(box->axis[axisIdx % 3]);
            if (axisIdx >= 3) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            CollisionTriCache_Set((u32)HND(box), (u32)HND(poly), &hit->pos, &hit->normal);
            break;
        case 2:
            hit->pos = box->corners[cornerIdx];
            hit->pos.x += vel->x * da.tEnter;
            hit->pos.y += vel->y * da.tEnter;
            hit->pos.z += vel->z * da.tEnter;
            hit->normal = poly->plane;
            CollisionTriCache_Set((u32)HND(box), (u32)HND(poly), &hit->pos, &hit->normal);
            break;
        case 3:
            p0 = GETP(poly->verts)[edgeIdx];
            p1 = GETP(poly->verts)[(edgeIdx + poly->numVerts - 1) % poly->numVerts];
            PSVECCrossProduct(GETP(box->axis[axisIdx]), vel, &cross);
            Create_Plane(&plane, &cross, &box->corners[cornerIdx]);
            start = GETP(poly->verts)[edgeVert];
            PSVECSubtract(&p1, &p0, &dir);
            PSVECAdd(&start, &dir, &end);
            a = Distance_Plane(&plane, &start);
            b = Distance_Plane(&plane, &end);
            corner = box->corners[cornerIdx];
            cornerEnd.x = corner.x + GETP(box->axis[axisIdx])->x;
            cornerEnd.y = corner.y + GETP(box->axis[axisIdx])->y;
            cornerEnd.z = corner.z + GETP(box->axis[axisIdx])->z;
            d = Distance_Plane(&poly->plane, &corner);
            d2 = Distance_Plane(&poly->plane, &cornerEnd);
            if (FloatsEqual(d, 0.0f, 0.1f) && FloatsEqual(d2, 0.0f, 0.1f)) {
                hit->pos = box->corners[cornerIdx];
                hit->pos.x += vel->x * da.tEnter;
                hit->pos.y += vel->y * da.tEnter;
                hit->pos.z += vel->z * da.tEnter;
                hit->normal = poly->plane;
                CollisionTriCache_Set((u32)HND(box), (u32)HND(poly), &hit->pos, &hit->normal);
            } else {
                if (FloatsEqual(a - b, 0.0f, 0.00001f)) {
                    hit->pos = GETP(poly->verts)[edgeVert];
                    hit->normal.x = -vel->x;
                    hit->normal.y = -vel->y;
                    hit->normal.z = -vel->z;
                    if (PSVECSquareMag(&hit->normal)) {
                        PSVECNormalize(&hit->normal, &hit->normal);
                    }
                } else {
                    d = a / (a - b);
                    hit->pos.x = dir.x * d + start.x;
                    hit->pos.y = dir.y * d + start.y;
                    hit->pos.z = dir.z * d + start.z;
                    PSVECCrossProduct(GETP(box->axis[axisIdx]), &dir, &hit->normal);
                    if (PSVECSquareMag(&hit->normal)) {
                        PSVECNormalize(&hit->normal, &hit->normal);
                    }
                    if (PSVECDotProduct(&hit->normal, vel) > 0.0f) {
                        hit->normal.x *= -1.0f;
                        hit->normal.y *= -1.0f;
                        hit->normal.z *= -1.0f;
                    }
                }
                CollisionTriCache_Set((u32)HND(box), (u32)HND(poly), &hit->pos, &hit->normal);
            }
            break;
        default:
            hit->type = 0xFF;
            if (!CollisionTriCache_Get((u32)HND(box), (u32)HND(poly), &hit->pos, &hit->normal)) {
                hit->pos = GETP(poly->verts)[0];
                hit->normal = poly->plane;
            }
            break;
        }
    }
    return da.tEnter;
}

s8 OBoxHitsPolyZeroTime(CollisionOBox *box, TerrainPoly *p, CollisionInfo *hit) {
    Vector4 edgeDir __attribute__((aligned(16)));
    Vector4 n __attribute__((aligned(16)));
    Vector4 pt __attribute__((aligned(16)));
    Vector4 c2 __attribute__((aligned(16)));
    Vector4 e __attribute__((aligned(16)));
    Vector4 c1 __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 tmp __attribute__((aligned(16)));
    f32 best;
    f32 planeD;
    f32 d;
    f32 t;
    s32 v;
    u8 type;
    u32 feature;
    u8 axis;
    u8 edge;
    s8 flip;
    s32 i;
    s32 k;
    s32 prev;

    best = -99999.9f;
    planeD = -p->plane.w;
    type = 0;
    axis = 0;
    edge = 0;
    for (i = 0; i < 3; i++) {
        u32 j;
        u8 minIdx = 0;
        u8 maxIdx = 0;
        f32 min;
        f32 max;
        f32 c;
        f32 lo;
        f32 hi;
        min = max = PSVECDotProduct(GETP(box->axis[i]), &GETP(p->verts)[0]);
        for (j = 1; j < p->numVerts; j++) {
            d = PSVECDotProduct(GETP(box->axis[i]), &GETP(p->verts)[j]);
            if (d < min) {
                min = d;
                minIdx = j;
            } else if (d > max) {
                max = d;
                maxIdx = j;
            }
        }
        c = PSVECDotProduct(GETP(box->axis[i]), box);
        lo = c - box->extent[i];
        hi = c + box->extent[i];
        if (max < lo || hi < min) {
            return 0;
        }
        if (min < hi && min - hi > best) {
            best = min - hi;
            feature = minIdx;
            axis = i;
            type = 1;
            flip = 0;
        }
        if (lo < max && lo - max > best) {
            best = lo - max;
            feature = maxIdx;
            axis = i;
            type = 1;
            flip = 1;
        }
    }
    {
        u32 j;
        u8 minIdx = 0;
        f32 min;
        f32 max;
        min = max = PSVECDotProduct(p, &box->corners[0]);
        for (j = 1; j < 8; j++) {
            d = PSVECDotProduct(p, &box->corners[j]);
            if (d < min) {
                min = d;
                minIdx = j;
            } else if (d > max) {
                max = d;
            }
        }
        if (planeD < min || max < planeD) {
            return 0;
        }
        if (planeD < max && planeD - max > best) {
            best = planeD - max;
            feature = minIdx;
            type = 2;
        }
        if (min < planeD && min - planeD > best) {
            best = min - planeD;
            feature = minIdx;
            type = 2;
        }
    }
    prev = p->numVerts - 1;
    for (v = 0; v < (s32)p->numVerts; prev = v++) {
        PSVECSubtract(&GETP(p->verts)[v], &GETP(p->verts)[prev], &edgeDir);
        for (k = 0; k < 3; k++) {
            s32 j;
            u8 maxIdx = 0;
            u8 minIdx = 0;
            f32 min;
            f32 max;
            f32 lo;
            f32 hi;
            f32 a;
            f32 mag;
            PSVECCrossProduct(GETP(box->axis[k]), &edgeDir, &n);
            mag = PSVECSquareMag(&n);
            if (mag < 0.0001f) {
                continue;
            }
            mag = 1.0f / Sqrt(mag);
            n.x *= mag;
            n.y *= mag;
            n.z *= mag;
            min = max = PSVECDotProduct(&n, &box->corners[0]);
            for (j = 1; j < 8; j++) {
                d = PSVECDotProduct(&n, &box->corners[j]);
                if (d < min) {
                    min = d;
                    minIdx = j;
                } else if (d > max) {
                    maxIdx = j;
                    max = d;
                }
            }
            a = PSVECDotProduct(&n, &GETP(p->verts)[v]);
            hi = PSVECDotProduct(&n, &GETP(p->verts)[(u32)GETP(p->verts)[v].w]);
            if (a > hi) {
                lo = hi;
                hi = a;
            } else {
                lo = a;
            }
            if (hi < min || max < lo) {
                return 0;
            }
            if (lo < max && lo - max > best) {
                best = lo - max;
                axis = k;
                edge = v;
                feature = maxIdx;
                type = 3;
            }
            if (min < hi && min - hi > best) {
                best = min - hi;
                axis = k;
                edge = v;
                feature = minIdx;
                type = 3;
            }
        }
    }
    if (hit != NULL && best < 0.0f) {
        switch (type) {
        case 1:
            hit->pos = GETP(p->verts)[feature];
            hit->normal = *GETP(box->axis[axis]);
            if (flip) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            break;
        case 2:
            hit->pos = box->corners[feature];
            hit->normal = p->plane;
            break;
        case 3:
            pt = box->corners[feature];
            PSVECSubtract(&GETP(p->verts)[edge], &GETP(p->verts)[(edge - 1 + p->numVerts) % p->numVerts], &e);
            PSVECCrossProduct(&e, GETP(box->axis[axis]), &c1);
            PSVECCrossProduct(&e, &c1, &c2);
            Create_Plane(&plane, &c2, &GETP(p->verts)[edge]);
            d = Distance_Plane(&plane, &pt);
            t = -d / PSVECDotProduct(&plane, GETP(box->axis[axis]));
            pt.x += GETP(box->axis[axis])->x * t;
            pt.y += GETP(box->axis[axis])->y * t;
            pt.z += GETP(box->axis[axis])->z * t;
            hit->pos = pt;
            hit->normal = c1;
            if (PSVECSquareMag(&hit->normal)) {
                PSVECNormalize(&hit->normal, &hit->normal);
            }
            PSVECSubtract(box, hit, &tmp);
            if (PSVECDotProduct(&tmp, &hit->normal) < 0.0f) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            break;
        }
    }
    return 1;
}

