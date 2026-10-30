#include "common.h"


typedef struct OBoxHolder {
    Vector4 v[3];                 /* 0x00 */
    u8 pad30[0x9D - 0x30];        /* 0x30 */
    s8 inUse;                     /* 0x9D */
    u8 pad9E[0xF0 - 0x9E];        /* 0x9E */
} OBoxHolder;

/* an oriented box as the segment tests see it: corners, half extents,
   axes and the box's projections onto its own axes */
typedef struct CollisionOBox {
    Vector4 center;               /* 0x00 */
    Vector4 corners[8];           /* 0x10 */
    f32 extent[3];                /* 0x90 */
    u8 pad9C[4];                  /* 0x9C */
    PTR32(Vector4) axis[3];             /* 0xA0 */
    u8 padAC[4];                  /* 0xAC */
    f32 centerProj[3];            /* 0xB0 */
    u8 padBC[4];                  /* 0xBC */
    f32 velProj[3];               /* 0xC0 */
    u32 unkCC;                    /* 0xCC */
} CollisionOBox;

typedef struct CollisionInfo {
    Vector4 pos;
    Vector4 normal;
    s32 unk20;
    s32 unk24;
    u32 material;
    u8 pad2C[0x40 - 0x2C];
    u8 type;
} CollisionInfo;

extern OBoxHolder g_aOBox[0x200];
extern OConvexHull g_aOConvexHull[8];
extern CollisionPair g_aCollisionPairCache[4];
extern s32 g_iDeltaTimeMS;

void OConvexHullHolder_Init(void)
{
    u32 i;

    for (i = 0; i < 8; i++)
    {
        SETP(g_aOConvexHull[i].hull, NULL);
    }
}

OConvexHull *OConvexHullHolder_Get(void *owner) {
    u32 i;

    for (i = 0; i < 8; i++) {
        if (GETP(g_aOConvexHull[i].hull) == NULL) {
            SETP(g_aOConvexHull[i].hull, owner);
            return &g_aOConvexHull[i];
        }
    }
    Dprintf("OUT OF CONVEX HULLS!!!!\n");
    return NULL;
}

void OConvexHullHolder_Release(OConvexHull *hull) {
    if (hull != NULL) {
        SETP(hull->hull, NULL);
    }
}

void OBoxHolder_Init(void)
{
    u32 i;

    Dprintf("OBOXHOLDER: %x\n", g_aOBox);
    for (i = 0; i < 0x200; i++)
    {
        g_aOBox[i].inUse = 0;
    }
}

OBoxHolder *OBoxHolder_Get(void) {
    u32 i;

    for (i = 0; i < 0x200; i++) {
        if (g_aOBox[i].inUse == 0) {
            g_aOBox[i].inUse = 1;
            return &g_aOBox[i];
        }
    }
    Dprintf("OUT OF OBOXES!!!!\n");
    return NULL;
}

void OBoxHolder_Release(OBoxHolder *obox) {
    if (obox != NULL) {
        obox->inUse = 0;
    }
}

f32 RayBoxTest(Vector4 *start, Vector4 *dir, Vector4 *box, f32 t, Vector4 *hit) {
    typedef struct BoxNode {
        Vector4 plane;
        u8 type;
        u8 pad;
        s16 next;
    } __attribute__((aligned(16))) BoxNode;
    Vector4 end __attribute__((aligned(16)));
    Vector4 endPt __attribute__((aligned(16)));
    BoxNode nodes[6] = {
        { { 1.0f, 0.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, 1.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, 0.0f, 1.0f, 0.0f }, 2, 0, 1 },
        { { -1.0f, 0.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, -1.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, 0.0f, -1.0f, 0.0f }, 0, 0, 0 },
    };
    Vector4 *plane;
    f32 d0;
    f32 frac;

    nodes[0].plane.w = -box[1].x;
    nodes[1].plane.w = -box[1].y;
    nodes[2].plane.w = -box[1].z;
    nodes[3].plane.w = box[0].x;
    nodes[4].plane.w = box[0].y;
    nodes[5].plane.w = box[0].z;
    endPt.x = end.x = t * dir->x + start->x;
    endPt.y = end.y = t * dir->y + start->y;
    endPt.z = end.z = t * dir->z + start->z;
    frac = t;
    plane = BSPN0_CollideWithSegment(nodes, 0, start, &end);
    if (plane != NULL) {
        d0 = Distance_Plane(plane, start);
        frac *= d0 / (d0 - Distance_Plane(plane, &endPt));
        if (hit != NULL) {
            hit[0].x = dir->x * frac + start->x;
            hit[0].y = dir->y * frac + start->y;
            hit[0].z = dir->z * frac + start->z;
            hit[1] = *plane;
        }
    }
    return frac;
}

f32 RaySphereTest(Vector4 *start, Vector4 *dir, Vector4 *center, f32 radius, f32 t, u8 *hit) {
    Vector4 end __attribute__((aligned(16)));
    Vector4 seg __attribute__((aligned(16)));
    Vector4 toCenter __attribute__((aligned(16)));
    Vector4 endToCenter __attribute__((aligned(16)));
    Vector4 cross __attribute__((aligned(16)));
    Vector4 unit __attribute__((aligned(16)));
    Vector4 *pEnd = &end;
    f32 radiusSq = radius * radius;
    f32 crossSq;
    f32 segSq;
    f32 h;
    f32 len;
    f32 inv;
    f32 dist;

    PSVECSubtract(center, start, &toCenter);
    if (PSVECSquareMag(&toCenter) < radiusSq) {
        if (hit != NULL) {
            *(Vector4 *)hit = *start;
            PSVECSubtract((Vector4 *)hit, center, (Vector4 *)(hit + 0x10));
            if (PSVECSquareMag((Vector4 *)(hit + 0x10))) {
                PSVECNormalize((Vector4 *)(hit + 0x10), (Vector4 *)(hit + 0x10));
            }
        }
        return 0.0f;
    }
    pEnd->x = dir->x * t + start->x;
    pEnd->y = dir->y * t + start->y;
    pEnd->z = dir->z * t + start->z;
    PSVECSubtract(pEnd, start, &seg);
    if (PSVECDotProduct(&seg, &toCenter) < 0.0f) {
        return t;
    }
    PSVECSubtract(center, pEnd, &endToCenter);
    if (PSVECDotProduct(&seg, &endToCenter) > 0.0f && PSVECSquareMag(&endToCenter) > radiusSq) {
        return t;
    }
    PSVECCrossProduct(&seg, &toCenter, &cross);
    crossSq = cross.z * cross.z + (cross.x * cross.x + cross.y * cross.y);
    segSq = seg.z * seg.z + (seg.x * seg.x + seg.y * seg.y);
    if (crossSq > radiusSq * segSq) {
        return t;
    }
    if (PSVECDotProduct(&seg, &endToCenter) > 0.0f && PSVECSquareDistance(pEnd, center) > radiusSq) {
        return t;
    }
    h = Sqrt(radiusSq - crossSq / segSq);
    len = MagnitudeVector(&seg);
    inv = 1.0f / len;
    unit.x = seg.x * inv;
    unit.y = seg.y * inv;
    unit.z = seg.z * inv;
    dist = inv * (t * (PSVECDotProduct(&unit, &toCenter) - h));
    if (len < 1e-9f) {
        return t;
    }
    if (hit != NULL) {
        ((Vector4 *)hit)->x = dist * dir->x + start->x;
        ((Vector4 *)hit)->y = dist * dir->y + start->y;
        ((Vector4 *)hit)->z = dist * dir->z + start->z;
        PSVECSubtract((Vector4 *)hit, center, (Vector4 *)(hit + 0x10));
        if (PSVECSquareMag((Vector4 *)(hit + 0x10))) {
            PSVECNormalize((Vector4 *)(hit + 0x10), (Vector4 *)(hit + 0x10));
        }
    }
    return dist;
}

typedef struct BoxPlane {
    Vector4 plane;
    u8 type;
    u8 pad;
    u16 next;
    u8 pad14[0xC];
} BoxPlane;

static inline f32 RayBoxTestLocal(Vector4 *start, Vector4 *dir, Vector4 *box, f32 t, Vector4 *hit) {
    BoxPlane planes[6] = {
        { { 1.0f, 0.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, 1.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, 0.0f, 1.0f, 0.0f }, 2, 0, 1 },
        { { -1.0f, 0.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, -1.0f, 0.0f, 0.0f }, 2, 0, 1 },
        { { 0.0f, 0.0f, -1.0f, 0.0f }, 0, 0, 0 },
    };
    Vector4 seg __attribute__((aligned(16)));
    Vector4 end __attribute__((aligned(16)));
    Vector4 *res;
    f32 d0;
    f32 d1;

    planes[0].plane.w = -box[1].x;
    planes[1].plane.w = -box[1].y;
    planes[2].plane.w = -box[1].z;
    planes[3].plane.w = box[0].x;
    planes[4].plane.w = box[0].y;
    planes[5].plane.w = box[0].z;
    end.x = seg.x = t * dir->x + start->x;
    end.y = seg.y = t * dir->y + start->y;
    end.z = seg.z = t * dir->z + start->z;
    res = BSPN0_CollideWithSegment(planes, NULL, start, &seg);
    if (res != NULL) {
        d0 = Distance_Plane(res, start);
        d1 = Distance_Plane(res, &end);
        t *= d0 / (d0 - d1);
        hit[0].x = dir->x * t + start->x;
        hit[0].y = dir->y * t + start->y;
        hit[0].z = dir->z * t + start->z;
        hit[1] = *res;
    }
    return t;
}

f32 PhysicalProperties_CollideWithRay(u8 *props, void *mtx, Vector4 *start, Vector4 *dir, f32 t, u8 *hit) {
    typedef struct RayHit {
        Vector4 pos;
        Vector4 normal;
        u8 pad[0x30];
    } RayHit;
    f32 result = t;

    switch (props[0x10]) {
    case 0: {
        Vector4 *box = SLOT_LOAD(Vector4 *, (props + 0x44));
        Vector4 localDir __attribute__((aligned(16)));
        Vector4 localStart __attribute__((aligned(16)));
        RayHit localHit __attribute__((aligned(16)));

        InverseXformVec(mtx, start, &localStart);
        InverseXformVecOrientation(mtx, dir, &localDir);
        result = RayBoxTestLocal(&localStart, &localDir, box, t, &localHit.pos);
        if (hit != NULL && result < t) {
            XformVec(mtx, &localHit.pos, (Vector4 *)hit);
            XformVecOrientation(mtx, &localHit.normal, (Vector4 *)(hit + 0x10));
            *(s32 *)(hit + 0x28) = *(s32 *)(props + 0x30);
        }
        break;
    }
    case 1: {
        Vector4 localDir __attribute__((aligned(16)));
        Vector4 localStart __attribute__((aligned(16)));
        RayHit localHit __attribute__((aligned(16)));
        f32 d = t;

        InverseXformVec(mtx, start, &localStart);
        InverseXformVecOrientation(mtx, dir, &localDir);
        if (hit != NULL && d < t) {
            XformVec(mtx, &localHit.pos, (Vector4 *)hit);
            XformVecOrientation(mtx, &localHit.normal, (Vector4 *)(hit + 0x10));
            *(s32 *)(hit + 0x28) = *(s32 *)(props + 0x30);
        }
        break;
    }
    case 7: {
        u32 *list = SLOT_LOAD(u32 *, (props + 0x44));
        u32 i;
        Vector4 center __attribute__((aligned(16)));
        f32 d;

        for (i = 0; i < list[0]; i++) {
            Vector4 *sphere = (Vector4 *)PTR((list[1] + i * 0x20));
            XformVec(mtx, sphere, &center);
            d = RaySphereTest(start, dir, &center, sphere[1].x, result, hit);
            if (hit != NULL && d < result) {
                result = d;
                *(s32 *)(hit + 0x28) = *(s32 *)(props + 0x30);
            }
        }
        break;
    }
    }
    return result;
}

void CollisionPairCache_Clear(void) {
    u32 i;

    for (i = 0; i < 4; i++) {
        g_aCollisionPairCache[i].objectB = 0;
        g_aCollisionPairCache[i].objectA = 0;
        g_aCollisionPairCache[i].time = 99999;
    }
}

void CollisionPairCache_Heartbeat(void)
{
    u32 i;

    for (i = 0; i < 4; i++)
    {
        if (g_aCollisionPairCache[i].objectA != 0)
        {
            g_aCollisionPairCache[i].time += g_iDeltaTimeMS;
        }
        if ((u32) g_aCollisionPairCache[i].time > 1000)
        {
            g_aCollisionPairCache[i].objectB = 0;
            g_aCollisionPairCache[i].objectA = 0;
            g_aCollisionPairCache[i].time = 99999;
        }
    }
}

void OBoxCache_Set(u32 box0, u32 box1, Vector4 *pos, Vector4 *normal)
{
    u32 i;
    u32 maxTime;
    u32 slot;

    maxTime = 0;
    slot = 0;
    for (i = 0; i < 4; i++)
    {
        if (box0 == g_aCollisionPairCache[i].objectA && box1 == g_aCollisionPairCache[i].objectB)
        {
            g_aCollisionPairCache[i].time = 0;
            g_aCollisionPairCache[i].normal = *normal;
            g_aCollisionPairCache[i].pos = *pos;
            return;
        }
        if (box1 == g_aCollisionPairCache[i].objectA && box0 == g_aCollisionPairCache[i].objectB)
        {
            g_aCollisionPairCache[i].time = 0;
            g_aCollisionPairCache[i].normal.x = -normal->x;
            g_aCollisionPairCache[i].normal.y = -normal->y;
            g_aCollisionPairCache[i].normal.z = -normal->z;
            g_aCollisionPairCache[i].pos = *pos;
            return;
        }
    }

    for (i = 0; i < 4; i++)
    {
        if (g_aCollisionPairCache[i].objectA == 0)
        {
            slot = i;
            break;
        }
        if ((u32)g_aCollisionPairCache[i].time > maxTime)
        {
            maxTime = g_aCollisionPairCache[i].time;
            slot = i;
        }
    }

    g_aCollisionPairCache[slot].time = 0;
    g_aCollisionPairCache[slot].normal = *normal;
    g_aCollisionPairCache[slot].pos = *pos;
    g_aCollisionPairCache[slot].objectA = box0;
    g_aCollisionPairCache[slot].objectB = box1;
}

static inline s8 OBoxCache_Get(u32 box0, u32 box1, Vector4 *pos, Vector4 *normal) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (box0 == g_aCollisionPairCache[i].objectA && box1 == g_aCollisionPairCache[i].objectB) {
            *pos = g_aCollisionPairCache[i].pos;
            *normal = g_aCollisionPairCache[i].normal;
            g_aCollisionPairCache[i].time = 0;
            return 1;
        }
        if (box1 == g_aCollisionPairCache[i].objectA && box0 == g_aCollisionPairCache[i].objectB) {
            *pos = g_aCollisionPairCache[i].pos;
            normal->x = -g_aCollisionPairCache[i].normal.x;
            normal->y = -g_aCollisionPairCache[i].normal.y;
            normal->z = -g_aCollisionPairCache[i].normal.z;
            g_aCollisionPairCache[i].time = 0;
            return 1;
        }
    }
    return 0;
}

f32 OBoxHitsOBox(void *boxA, Vector4 *velA, void *boxB, Vector4 *velB, void *hitInfo, f32 maxT) {
    typedef struct OBB {
        Vector4 center;
        Vector4 corners[8];
        f32 extent[3];
        u8 pad9C[4];
        PTR32(Vector4) axis[3];
    } OBB;
    typedef struct OBBHit {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[0x20];
        u8 type;
    } OBBHit;
    DividingAxis da;
    Vector4 rel;
    Vector4 n;
    Vector4 pA;
    Vector4 pB;
    Vector4 plane;
    Vector4 q;
    Vector4 diff;
    u8 maxIdx2;
    u8 minIdx2;
    s32 i;
    s32 k;
    u8 minIdx;
    u8 maxIdx;
    s32 j;
    s32 ia;
    u8 axisIdx;
    u8 corner;
    u8 type;
    f32 d;
    f32 s;
    f32 d1;
    f32 denom;
    f32 mag;
    u8 featB;
    u8 axisB;
    OBBHit *hit = hitInfo;
    OBB *b = boxB;
    OBB *a = boxA;

    featB = 0;
    da.tEnter = 0.0f;
    type = 0;
    corner = 0;
    da.tExit = maxT;
    axisIdx = 0;
    axisB = 0;
    PSVECSubtract(velB, velA, &rel);
    MagnitudeVector(&rel);
    if (hit != NULL) {
        hit->type = 0;
    }
    for (i = 0; i < 3; i++) {
        minIdx = 0;
        maxIdx = 0;
        da.minA = da.maxA = PSVECDotProduct(GETP(a->axis[i]), &b->corners[0]);
        for (k = 1; k < 8; k++) {
            d = PSVECDotProduct(GETP(a->axis[i]), &b->corners[k]);
            if (d < da.minA) {
                da.minA = d;
                minIdx = k;
            } else if (d > da.maxA) {
                maxIdx = k;
                da.maxA = d;
            }
        }
        da.dir = PSVECDotProduct(GETP(a->axis[i]), &rel);
        d = PSVECDotProduct(GETP(a->axis[i]), &a->center);
        da.maxB = d + a->extent[i];
        da.minB = d - a->extent[i];
        switch ((u8)Collision_ProcessDividingAxis(&da)) {
        case 0:
            return maxT;
        case 2:
            corner = minIdx;
            axisIdx = i + 3;
            type = 1;
            break;
        case 3:
            corner = maxIdx;
            axisIdx = i;
            type = 1;
            break;
        }
    }
    for (i = 0; i < 3; i++) {
        minIdx = 0;
        maxIdx = 0;
        da.minB = da.maxB = PSVECDotProduct(GETP(b->axis[i]), &a->corners[0]);
        for (k = 1; k < 8; k++) {
            d = PSVECDotProduct(GETP(b->axis[i]), &a->corners[k]);
            if (d < da.minB) {
                da.minB = d;
                minIdx = k;
            } else if (d > da.maxB) {
                maxIdx = k;
                da.maxB = d;
            }
        }
        da.dir = PSVECDotProduct(GETP(b->axis[i]), &rel);
        d = PSVECDotProduct(GETP(b->axis[i]), &b->center);
        da.maxA = d + b->extent[i];
        da.minA = d - b->extent[i];
        switch ((u8)Collision_ProcessDividingAxis(&da)) {
        case 0:
            return maxT;
        case 2:
            corner = maxIdx;
            axisIdx = i + 3;
            type = 2;
            break;
        case 3:
            corner = minIdx;
            axisIdx = i;
            type = 2;
            break;
        }
    }
    for (ia = 0; ia < 3; ia++) {
        for (j = 0; j < 3; j++) {
            minIdx = 0;
            maxIdx = 0;
            minIdx2 = 0;
            maxIdx2 = 0;
            PSVECCrossProduct(GETP(a->axis[ia]), GETP(b->axis[j]), &n);
            if (PSVECDotProduct(&n, &n) < 0.0001) {
                continue;
            }
            da.minB = da.maxB = PSVECDotProduct(&a->corners[0], &n);
            da.minA = da.maxA = PSVECDotProduct(&b->corners[0], &n);
            for (k = 1; k < 8; k++) {
                d = PSVECDotProduct(&a->corners[k], &n);
                if (d < da.minB) {
                    da.minB = d;
                    minIdx = k;
                } else if (d > da.maxB) {
                    maxIdx = k;
                    da.maxB = d;
                }
                d = PSVECDotProduct(&b->corners[k], &n);
                if (d < da.minA) {
                    da.minA = d;
                    minIdx2 = k;
                } else if (d > da.maxA) {
                    maxIdx2 = k;
                    da.maxA = d;
                }
            }
            da.dir = PSVECDotProduct(&n, &rel);
            switch ((u8)Collision_ProcessDividingAxis(&da)) {
            case 0:
                return maxT;
            case 2:
                featB = minIdx2;
                corner = maxIdx;
                axisIdx = ia;
                axisB = j;
                type = 3;
                break;
            case 3:
                featB = maxIdx2;
                corner = minIdx;
                axisIdx = ia + 3;
                axisB = j;
                type = 3;
                break;
            }
        }
    }
    if (hit != NULL && da.tEnter < maxT) {
        hit->type = type;
        switch (type) {
        case 1:
            hit->pos = b->corners[corner];
            hit->pos.x += velB->x * da.tEnter;
            hit->pos.y += velB->y * da.tEnter;
            hit->pos.z += velB->z * da.tEnter;
            hit->normal = *GETP(a->axis[axisIdx % 3]);
            if (axisIdx >= 3) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            OBoxCache_Set((u32)HND(a), (u32)HND(b), &hit->pos, &hit->normal);
            break;
        case 2:
            hit->pos = a->corners[corner];
            hit->pos.x += velA->x * da.tEnter;
            hit->pos.y += velA->y * da.tEnter;
            hit->pos.z += velA->z * da.tEnter;
            hit->normal = *GETP(b->axis[axisIdx % 3]);
            if (axisIdx >= 3) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            OBoxCache_Set((u32)HND(a), (u32)HND(b), &hit->pos, &hit->normal);
            break;
        case 3:
            pA = a->corners[corner];
            pB = b->corners[featB];
            pA.x += velA->x * da.tEnter;
            pA.y += velA->y * da.tEnter;
            pA.z += velA->z * da.tEnter;
            pB.x += velB->x * da.tEnter;
            pB.y += velB->y * da.tEnter;
            pB.z += velB->z * da.tEnter;
            PSVECCrossProduct(GETP(b->axis[axisB]), GETP(a->axis[axisIdx % 3]), &hit->normal);
            mag = PSVECSquareMag(&hit->normal);
            if (!FloatsEqual(mag, 1.0f, 0.0001f)) {
                s = 1.0f / Sqrt(mag);
                hit->normal.x *= s;
                hit->normal.y *= s;
                hit->normal.z *= s;
            }
            if (axisIdx >= 3) {
                axisIdx -= 3;
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            q.x = 100.0f * GETP(a->axis[axisIdx])->x + pA.x;
            q.y = 100.0f * GETP(a->axis[axisIdx])->y + pA.y;
            q.z = 100.0f * GETP(a->axis[axisIdx])->z + pA.z;
            Create_Plane(&plane, GETP(b->axis[(axisB + 1) % 3]), &pB);
            d1 = Distance_Plane(&plane, &pA);
            denom = d1 - Distance_Plane(&plane, &q);
            if (_fabs(denom) > 0.0f) {
                s = d1 / denom;
                hit->pos.x = s * (q.x - pA.x) + pA.x;
                hit->pos.y = s * (q.y - pA.y) + pA.y;
                hit->pos.z = s * (q.z - pA.z) + pA.z;
            } else {
                hit->pos = pA;
            }
            OBoxCache_Set((u32)HND(a), (u32)HND(b), &hit->pos, &hit->normal);
            break;
        default:
            if (!OBoxCache_Get((u32)HND(a), (u32)HND(b), &hit->pos, &hit->normal)) {
                diff.x = a->center.x - b->center.x;
                diff.y = a->center.y - b->center.y;
                diff.z = a->center.z - b->center.z;
                if (PSVECSquareMag(&diff)) {
                    PSVECNormalize(&diff, &diff);
                }
                hit->normal = diff;
                hit->pos.x = 0.5f * (b->center.x + a->center.x);
                hit->pos.y = 0.5f * (b->center.y + a->center.y);
                hit->pos.z = 0.5f * (b->center.z + a->center.z);
                OBoxCache_Set((u32)HND(a), (u32)HND(b), &hit->pos, &hit->normal);
            }
            hit->type = 0xFF;
            break;
        }
    }
    return da.tEnter;
}

s8 OBoxHitsOBoxZeroTime(CollisionOBox *a, CollisionOBox *b, CollisionInfo *hit) {
    Vector4 n __attribute__((aligned(16)));
    Vector4 pt __attribute__((aligned(16)));
    Vector4 edgePt __attribute__((aligned(16)));
    Vector4 c2 __attribute__((aligned(16)));
    Vector4 c1 __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 tmp __attribute__((aligned(16)));
    u8 type;
    u8 feature;
    u8 axisA;
    u8 feature2;
    f32 best;
    f32 d;
    f32 t;
    f32 dot;
    f32 minA;
    f32 maxA;
    f32 maxB;
    f32 minB;
    s32 i;
    u8 axisB;
    s8 flip;

    best = -99999.9f;
    type = 0;
    feature = 0;
    flip = 0;
    feature2 = 0;
    axisA = 0;
    axisB = 0;
    for (i = 0; i < 3; i++) {
        s32 k;
        u8 minIdx = 0;
        u8 maxIdx = 0;
        f32 c;
        f32 lo;
        f32 hi;
        minB = maxB = PSVECDotProduct(GETP(a->axis[i]), &b->corners[0]);
        for (k = 1; k < 8; k++) {
            d = PSVECDotProduct(GETP(a->axis[i]), &b->corners[k]);
            if (d < minB) {
                minB = d;
                minIdx = k;
            } else if (d > maxB) {
                maxIdx = k;
                maxB = d;
            }
        }
        c = PSVECDotProduct(GETP(a->axis[i]), a);
        lo = c - a->extent[i];
        hi = a->extent[i] + c;
        if (maxB < lo || hi < minB) {
            return 0;
        }
        if (minB < hi && minB - hi > best) {
            best = minB - hi;
            flip = 1;
            feature = minIdx;
            axisA = i;
            type = 1;
        }
        if (lo < maxB && lo - maxB > best) {
            best = lo - maxB;
            flip = 0;
            feature = maxIdx;
            axisA = i;
            type = 1;
        }
    }
    for (i = 0; i < 3; i++) {
        s32 k;
        u8 minIdx = 0;
        u8 maxIdx = 0;
        f32 c;
        f32 lo;
        f32 hi;
        minA = maxA = PSVECDotProduct(GETP(b->axis[i]), &a->corners[0]);
        for (k = 1; k < 8; k++) {
            d = PSVECDotProduct(GETP(b->axis[i]), &a->corners[k]);
            if (d < minA) {
                minA = d;
                minIdx = k;
            } else if (d > maxA) {
                maxIdx = k;
                maxA = d;
            }
        }
        c = PSVECDotProduct(GETP(b->axis[i]), b);
        hi = b->extent[i] + c;
        lo = c - b->extent[i];
        if (hi < minA || maxA < lo) {
            return 0;
        }
        if (lo < maxA && lo - maxA > best) {
            best = lo - maxA;
            flip = 1;
            feature = maxIdx;
            axisA = i;
            type = 2;
        }
        if (minA < hi && minA - hi > best) {
            best = minA - hi;
            flip = 0;
            feature = minIdx;
            axisA = i;
            type = 2;
        }
    }
    {
        s32 ia;
        s32 ib;
        for (ia = 0; ia < 3; ia++) {
            for (ib = 0; ib < 3; ib++) {
                s32 k;
                u8 minAIdx = 0;
                u8 maxAIdx = 0;
                u8 minBIdx = 0;
                u8 maxBIdx = 0;
                PSVECCrossProduct(GETP(a->axis[ia]), GETP(b->axis[ib]), &n);
                maxA = minA = PSVECDotProduct(&a->corners[0], &n);
                maxB = minB = PSVECDotProduct(&b->corners[0], &n);
                for (k = 1; k < 8; k++) {
                    d = PSVECDotProduct(&a->corners[k], &n);
                    if (d < minA) {
                        minA = d;
                        minAIdx = k;
                    } else if (d > maxA) {
                        maxAIdx = k;
                        maxA = d;
                    }
                    d = PSVECDotProduct(&b->corners[k], &n);
                    if (d < minB) {
                        minB = d;
                        minBIdx = k;
                    } else if (d > maxB) {
                        maxBIdx = k;
                        maxB = d;
                    }
                }
                if (maxB < minA || maxA < minB) {
                    return 0;
                }
                if (minB < maxA && minB - maxA > best) {
                    best = minB - maxA;
                    feature = maxAIdx;
                    feature2 = minBIdx;
                    axisA = ia;
                    axisB = ib;
                    type = 3;
                }
                if (minA < maxB && minA - maxB > best) {
                    best = minA - maxB;
                    feature = minAIdx;
                    feature2 = maxBIdx;
                    axisA = ia;
                    axisB = ib;
                    type = 3;
                }
            }
        }
    }
    if (hit != NULL && best < 0.0f) {
        switch (type) {
        case 1:
            hit->pos = b->corners[feature];
            hit->normal = *GETP(a->axis[axisA]);
            if (flip) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            break;
        case 2:
            hit->pos = a->corners[feature];
            hit->normal = *GETP(b->axis[axisA]);
            if (flip) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            break;
        case 3:
            pt = a->corners[feature];
            edgePt = b->corners[feature2];
            PSVECCrossProduct(GETP(b->axis[axisB]), GETP(a->axis[axisA]), &c1);
            PSVECCrossProduct(GETP(b->axis[axisB]), &c1, &c2);
            Create_Plane(&plane, &c2, &edgePt);
            d = Distance_Plane(&plane, &pt);
            dot = PSVECDotProduct(&plane, GETP(a->axis[axisA]));
            if (dot) {
                t = -d / dot;
            } else {
                t = 0.0f;
            }
            pt.x += GETP(a->axis[axisA])->x * t;
            pt.y += GETP(a->axis[axisA])->y * t;
            pt.z += GETP(a->axis[axisA])->z * t;
            hit->pos = pt;
            hit->normal = c1;
            if (PSVECSquareMag(&hit->normal)) {
                PSVECNormalize(&hit->normal, &hit->normal);
            }
            PSVECSubtract(a, hit, &tmp);
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
s8 OBoxHitsSegmentZeroTime(CollisionOBox *box, Vector4 *seg, CollisionInfo *hit);

f32 OBoxHitsSegment(CollisionOBox *box, Vector4 *vel, Vector4 *seg, f32 maxT, CollisionInfo *hit) {
    DividingAxis da;
    Vector4 dir __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 cross __attribute__((aligned(16)));
    Vector4 diff __attribute__((aligned(16)));
    s32 i;
    u8 type;
    u32 k;
    u32 j;
    u8 loIdx;
    u8 hiIdx;
    u8 maxIdx;
    u8 minIdx;
    f32 a;
    f32 b;
    f32 d;
    u32 corner;
    u8 axisIdx;
    u8 vert;

    da.tEnter = 0.0f;
    da.tExit = maxT;
    type = 0;
    axisIdx = 0;
    vert = 99;
    for (i = 0; i < 3; i++) {
        a = PSVECDotProduct(GETP(box->axis[i]), &seg[0]);
        b = PSVECDotProduct(GETP(box->axis[i]), &seg[1]);
        if (a > b) {
            da.minA = b;
            hiIdx = 1;
            loIdx = 0;
            da.maxA = a;
        } else {
            da.minA = a;
            hiIdx = 0;
            loIdx = 1;
            da.maxA = b;
        }
        da.dir = box->velProj[i];
        da.maxB = box->centerProj[i] + box->extent[i];
        da.minB = box->centerProj[i] - box->extent[i];
        switch ((u8)Collision_ProcessDividingAxis(&da)) {
        case 0:
            return maxT;
        case 2:
            vert = hiIdx;
            axisIdx = i + 3;
            type = 1;
            break;
        case 3:
            vert = loIdx;
            axisIdx = i;
            type = 1;
            break;
        }
    }
    PSVECSubtract(&seg[0], &seg[1], &dir);
    for (j = 0; j < 3; j++) {
        maxIdx = 0;
        minIdx = 0;
        PSVECCrossProduct(GETP(box->axis[j]), &dir, &axis);
        da.minB = da.maxB = PSVECDotProduct(&axis, &box->corners[0]);
        for (k = 1; k < 8; k++) {
            d = PSVECDotProduct(&axis, &box->corners[k]);
            if (d < da.minB) {
                da.minB = d;
                minIdx = k;
            } else if (d > da.maxB) {
                maxIdx = k;
                da.maxB = d;
            }
        }
        da.minA = da.maxA = PSVECDotProduct(&axis, &seg[0]);
        da.dir = -PSVECDotProduct(&axis, vel);
        switch ((u8)Collision_ProcessDividingAxis(&da)) {
        case 0:
            return maxT;
        case 2:
            axisIdx = j;
            corner = maxIdx;
            type = 3;
            break;
        case 3:
            axisIdx = j;
            corner = minIdx;
            type = 3;
            break;
        }
    }
    if (hit != NULL && da.tEnter < maxT) {
        hit->type = type;
        hit->material = 0;
        box->unkCC = 0;
        switch (type) {
        case 1:
            hit->pos = seg[vert];
            hit->normal = *GETP(box->axis[axisIdx % 3]);
            if (axisIdx >= 3) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            CollisionTriCache_Set((u32)HND(box), (u32)HND(seg), &hit->pos, &hit->normal);
            break;
        case 3:
            PSVECCrossProduct(GETP(box->axis[axisIdx]), vel, &cross);
            Create_Plane(&plane, &cross, &box->corners[corner]);
            a = Distance_Plane(&plane, &seg[0]);
            b = Distance_Plane(&plane, &seg[1]);
            if (FloatsEqual(a - b, 0.0f, 0.00001f)) {
                hit->pos = seg[0];
                hit->normal.x = -vel->x;
                hit->normal.y = -vel->y;
                hit->normal.z = -vel->z;
                if (PSVECSquareMag(&hit->normal)) {
                    PSVECNormalize(&hit->normal, &hit->normal);
                }
            } else {
                d = a / (a - b);
                PSVECSubtract(&seg[1], &seg[0], &diff);
                hit->pos.x = diff.x * d + seg[0].x;
                hit->pos.y = diff.y * d + seg[0].y;
                hit->pos.z = diff.z * d + seg[0].z;
                PSVECCrossProduct(GETP(box->axis[axisIdx]), &diff, &hit->normal);
                if (PSVECSquareMag(&hit->normal)) {
                    PSVECNormalize(&hit->normal, &hit->normal);
                }
                if (PSVECDotProduct(&hit->normal, vel) > 0.0f) {
                    hit->normal.x *= -1.0f;
                    hit->normal.y *= -1.0f;
                    hit->normal.z *= -1.0f;
                }
            }
            CollisionTriCache_Set((u32)HND(box), (u32)HND(seg), &hit->pos, &hit->normal);
            break;
        default:
            da.tEnter = 0.0f;
            hit->type = 0xFF;
            OBoxHitsSegmentZeroTime(box, seg, hit);
            break;
        }
    }
    return da.tEnter;
}

s8 OBoxHitsSegmentZeroTime(CollisionOBox *box, Vector4 *seg, CollisionInfo *hit) {
    u32 k;
    s32 i;
    u8 type;
    u8 axisIdx;
    u8 corner;
    u8 idx;
    u32 j;
    u8 maxIdx;
    u8 minIdx;
    f32 boxMin;
    f32 lo;
    f32 hi;
    f32 min;
    f32 max;
    f32 c;
    f32 d;
    f32 t;
    f32 best = -99999.9f;
    Vector4 dir __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    Vector4 point __attribute__((aligned(16)));
    Vector4 perp __attribute__((aligned(16)));
    Vector4 normal __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 segDir __attribute__((aligned(16)));
    Vector4 diff __attribute__((aligned(16)));

    type = 0;
    axisIdx = 0;
    for (i = 0; i < 3; i++) {
        hi = PSVECDotProduct(GETP(box->axis[i]), &seg[0]);
        d = PSVECDotProduct(GETP(box->axis[i]), &seg[1]);
        if (hi > d) {
            lo = d;
            idx = 1;
        } else {
            lo = hi;
            idx = 0;
            hi = d;
        }
        c = PSVECDotProduct(GETP(box->axis[i]), &box->center);
        boxMin = c - box->extent[i];
        c = c + box->extent[i];
        if (hi < boxMin || c < lo) {
            return 0;
        }
        if (lo < c) {
            d = lo - c;
            if (d > best) {
                best = d;
                if (hit != NULL) {
                    hit->pos = seg[idx];
                    hit->normal = *GETP(box->axis[i]);
                    Reverse_Plane(&hit->normal);
                }
            }
        }
        if (boxMin < hi) {
            d = boxMin - hi;
            if (d > best) {
                best = d;
                if (hit != NULL) {
                    hit->pos = seg[idx];
                    hit->normal = *GETP(box->axis[i]);
                }
            }
        }
    }
    PSVECSubtract(&seg[0], &seg[1], &dir);
    for (j = 0; j < 3; j++) {
        maxIdx = 0;
        minIdx = 0;
        PSVECCrossProduct(GETP(box->axis[j]), &dir, &axis);
        if (PSVECSquareMag(&axis)) {
            PSVECNormalize(&axis, &axis);
        }
        max = PSVECDotProduct(&axis, &box->corners[0]);
        min = max;
        for (k = 1; k < 8; k++) {
            d = PSVECDotProduct(&axis, &box->corners[k]);
            if (d < min) {
                min = d;
                minIdx = k;
            } else if (d > max) {
                maxIdx = k;
                max = d;
            }
        }
        c = PSVECDotProduct(&axis, &seg[0]);
        if (c < min || max < c) {
            return 0;
        }
        if (c < max) {
            d = c - max;
            if (d > best) {
                best = d;
                corner = maxIdx;
                axisIdx = j;
                type = 3;
            }
        }
        if (min < c) {
            d = min - c;
            if (d > best) {
                best = d;
                corner = minIdx;
                axisIdx = j;
                type = 3;
            }
        }
    }
    if (hit != NULL && best < 0.0f) {
        switch (type) {
        case 3:
            point = box->corners[corner];
            PSVECSubtract(&seg[1], &seg[0], &segDir);
            PSVECCrossProduct(&segDir, GETP(box->axis[axisIdx]), &normal);
            PSVECCrossProduct(&segDir, &normal, &perp);
            Create_Plane(&plane, &perp, &seg[0]);
            d = Distance_Plane(&plane, &point);
            t = -d / PSVECDotProduct(&plane, GETP(box->axis[axisIdx]));
            point.x += GETP(box->axis[axisIdx])->x * t;
            point.y += GETP(box->axis[axisIdx])->y * t;
            point.z += GETP(box->axis[axisIdx])->z * t;
            hit->pos = point;
            hit->normal = normal;
            if (PSVECSquareMag(&hit->normal)) {
                PSVECNormalize(&hit->normal, &hit->normal);
            }
            PSVECSubtract(&box->center, &hit->pos, &diff);
            if (PSVECDotProduct(&diff, &hit->normal) < 0.0f) {
                hit->normal.x *= -1.0f;
                hit->normal.y *= -1.0f;
                hit->normal.z *= -1.0f;
            }
            break;
        }
    }
    return 1;
}

