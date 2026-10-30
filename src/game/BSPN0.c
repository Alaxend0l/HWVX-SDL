#include "common.h"

typedef struct BSPNode {
    Vector4 plane;
    u8 type;
    u8 pad;
    u16 next;
    u8 pad14[0xC];
} BSPNode;


s8 BSPN0_CollideWithVector(BSPNode *node, Vector4 *point) {
    if (Distance_Plane(node, point) > 0.0f) {
        if (node->type & 1) {
            return BSPN0_CollideWithVector(node + 1, point);
        }
        return 0;
    }
    if (node->type & 2) {
        return BSPN0_CollideWithVector(node + node->next, point);
    }
    return 1;
}

Vector4 *BSPN0_CollideWithSegment(BSPNode *node, Vector4 *hit, Vector4 *p0, Vector4 *p1) {
    Vector4 *res;
    Vector4 mid __attribute__((aligned(16)));
    f32 d0;
    f32 d1;
    f32 t;

    d0 = Distance_Plane(node, p0);
    d1 = Distance_Plane(node, p1);
    if (d0 >= 0.0f && d1 >= 0.0f) {
        if (node->type & 1) {
            return BSPN0_CollideWithSegment(node + 1, hit, p0, p1);
        }
        return NULL;
    }
    if (d0 < 0.0f && d1 < 0.0f) {
        if (node->type & 2) {
            return BSPN0_CollideWithSegment(node + node->next, hit, p0, p1);
        }
        if (hit != NULL) {
            *p1 = *p0;
        }
        return hit;
    }
    if (d0 < 0.0f && d1 >= 0.0f) {
        t = d0 / (d0 - d1);
        mid.x = t * (p1->x - p0->x) + p0->x;
        mid.y = t * (p1->y - p0->y) + p0->y;
        mid.z = t * (p1->z - p0->z) + p0->z;
        if (node->type & 2) {
            res = BSPN0_CollideWithSegment(node + node->next, hit, p0, &mid);
            if (res != NULL) {
                *p1 = mid;
                return res;
            }
        } else {
            if (hit != NULL) {
                *p1 = *p0;
            }
            return hit;
        }
        if (node->type & 1) {
            return BSPN0_CollideWithSegment(node + 1, hit, &mid, p1);
        }
        return NULL;
    }
    if (d0 >= 0.0f && d1 < 0.0f) {
        t = d0 / (d0 - d1);
        mid.x = t * (p1->x - p0->x) + p0->x;
        mid.y = t * (p1->y - p0->y) + p0->y;
        mid.z = t * (p1->z - p0->z) + p0->z;
        if (node->type & 1) {
            res = BSPN0_CollideWithSegment(node + 1, hit, p0, &mid);
            if (res != NULL) {
                *p1 = mid;
                return res;
            }
        }
        if (node->type & 2) {
            return BSPN0_CollideWithSegment(node + node->next, &node->plane, &mid, p1);
        }
        *p1 = mid;
        return &node->plane;
    }
    return NULL;
}
