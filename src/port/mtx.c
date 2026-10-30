/* MTX/VEC library: portable C equivalents of the SDK's paired-single routines.
 * Vectors are taken as void* because the game passes both 12-byte Vec and its
 * own 16-byte Vector4; only x, y, z are read or written. */
#include "port_internal.h"

#include <math.h>

typedef struct V3 {
    f32 x, y, z;
} V3;

void PSMTXIdentity(Mtx m) {
    memset(m, 0, sizeof(Mtx));
    m[0][0] = m[1][1] = m[2][2] = 1.0f;
}

void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    Mtx t;
    int i;
    for (i = 0; i < 3; i++) {
        t[i][0] = a[i][0] * b[0][0] + a[i][1] * b[1][0] + a[i][2] * b[2][0];
        t[i][1] = a[i][0] * b[0][1] + a[i][1] * b[1][1] + a[i][2] * b[2][1];
        t[i][2] = a[i][0] * b[0][2] + a[i][1] * b[1][2] + a[i][2] * b[2][2];
        t[i][3] = a[i][0] * b[0][3] + a[i][1] * b[1][3] + a[i][2] * b[2][3] + a[i][3];
    }
    memcpy(ab, t, sizeof(Mtx));
}

void PSMTXScale(Mtx m, f32 x, f32 y, f32 z) {
    memset(m, 0, sizeof(Mtx));
    m[0][0] = x;
    m[1][1] = y;
    m[2][2] = z;
}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    f32 rl = 1.0f / (r - l);
    f32 tb = 1.0f / (t - b);
    f32 fn = 1.0f / (f - n);
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = 2.0f * rl;
    m[0][3] = -(r + l) * rl;
    m[1][1] = 2.0f * tb;
    m[1][3] = -(t + b) * tb;
    m[2][2] = -fn;
    m[2][3] = -f * fn;
    m[3][3] = 1.0f;
}

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
    f32 angle = fovY * 0.5f * (3.14159265f / 180.0f);
    f32 cot = 1.0f / tanf(angle);
    f32 fn = 1.0f / (f - n);
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = cot / aspect;
    m[1][1] = cot;
    m[2][2] = -n * fn;
    m[2][3] = -(f * n) * fn;
    m[3][2] = -1.0f;
}

void PSVECAdd(const void *a, const void *b, void *ab) {
    const V3 *va = a, *vb = b;
    V3 *o = ab;
    V3 t = {va->x + vb->x, va->y + vb->y, va->z + vb->z};
    *o = t;
}

void PSVECSubtract(const void *a, const void *b, void *a_b) {
    const V3 *va = a, *vb = b;
    V3 *o = a_b;
    V3 t = {va->x - vb->x, va->y - vb->y, va->z - vb->z};
    *o = t;
}

void PSVECScale(const void *src, void *dst, f32 scale) {
    const V3 *s = src;
    V3 *d = dst;
    V3 t = {s->x * scale, s->y * scale, s->z * scale};
    *d = t;
}

f32 PSVECSquareMag(const void *v) {
    const V3 *a = v;
    return a->x * a->x + a->y * a->y + a->z * a->z;
}

f32 PSVECMag(const void *v) {
    return sqrtf(PSVECSquareMag(v));
}

void PSVECNormalize(const void *src, void *unit) {
    f32 m = PSVECSquareMag(src);
    /* The paired-single version uses frsqrte without a zero check; return a
     * zero vector instead of NaNs for zero-length input. */
    PSVECScale(src, unit, m > 0.0f ? 1.0f / sqrtf(m) : 0.0f);
}

f32 PSVECDotProduct(const void *a, const void *b) {
    const V3 *va = a, *vb = b;
    return va->x * vb->x + va->y * vb->y + va->z * vb->z;
}

void PSVECCrossProduct(const void *a, const void *b, void *axb) {
    const V3 *va = a, *vb = b;
    V3 t = {va->y * vb->z - va->z * vb->y, va->z * vb->x - va->x * vb->z,
            va->x * vb->y - va->y * vb->x};
    *(V3 *)axb = t;
}

f32 PSVECSquareDistance(const void *a, const void *b) {
    V3 d;
    PSVECSubtract(a, b, &d);
    return PSVECSquareMag(&d);
}
