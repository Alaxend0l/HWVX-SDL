#include "common.h"


static void Normalize_Plane(Vector4 *plane) {
    f32 len;
    f32 inv;

    len = Sqrt(plane->z * plane->z + (plane->x * plane->x + plane->y * plane->y));
    if (len != 1.0f && len != 0.0f) {
        inv = 1.0f / len;
        plane->x *= inv;
        plane->y *= inv;
        plane->z *= inv;
        plane->w *= inv;
    }
}

void Create_Plane(Vector4 *plane, Vector4 *normal, Vector4 *point) {
    plane->x = normal->x;
    plane->y = normal->y;
    plane->z = normal->z;
    plane->w = -1.0f * (normal->z * point->z + (normal->x * point->x + normal->y * point->y));
    Normalize_Plane(plane);
}

void Create_Plane3(Vector4 *plane, Vector4 *normal, Vector4 *point) {
    plane->x = normal->x;
    plane->y = normal->y;
    plane->z = normal->z;
    plane->w = -1.0f * (normal->z * point->z + (normal->x * point->x + normal->y * point->y));
}

void Reverse_Plane(Vector4 *plane) {
    plane->x *= -1.0f;
    plane->y *= -1.0f;
    plane->z *= -1.0f;
    plane->w *= -1.0f;
}

f32 Distance_Plane(Vector4 *plane, Vector4 *point) {
    return plane->w + (plane->z * point->z + (plane->x * point->x + plane->y * point->y));
}
