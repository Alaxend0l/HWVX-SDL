#include "common.h"

typedef struct Vector {
    f32 x;
    f32 y;
    f32 z;
} Vector;

extern s32 __float_nan[];
extern s32 __float_huge[];


static inline f32 SquareMagnitude(Vector *v) {
    return v->x * v->x + v->y * v->y + v->z * v->z;
}

Matrix4 g_identity;
Vector4 g_zeroVector;
Vector4 g_iBasis;
Vector4 g_jBasis;
Vector4 g_kBasis;

f32 MagnitudeVector(Vector *v) {
    f32 zero = 0.0f;

    if (v->x != zero || v->y != zero || v->z != zero) {
        return PSVECMag(v);
    }
    return zero;
}

void CopyVector(Vector4 *dst, Vector4 *src) {
    *dst = *src;
}

void CopyMatrix(Matrix4 *dst, Matrix4 *src)
{

    memcpy(dst, src, 0x40);
}

void Init_Math3D(void)
{
    g_identity.v[0].x = 1.0f;
    g_identity.v[0].y = 0.0f;
    g_identity.v[0].z = 0.0f;
    g_identity.v[0].w = 0.0f;
    g_identity.v[1].x = 0.0f;
    g_identity.v[1].y = 1.0f;
    g_identity.v[1].z = 0.0f;
    g_identity.v[1].w = 0.0f;
    g_identity.v[2].x = 0.0f;
    g_identity.v[2].y = 0.0f;
    g_identity.v[2].z = 1.0f;
    g_identity.v[2].w = 0.0f;
    g_identity.v[3].x = 0.0f;
    g_identity.v[3].y = 0.0f;
    g_identity.v[3].z = 0.0f;
    g_identity.v[3].w = 1.0f;
    g_zeroVector.x = 0.0f;
    g_zeroVector.y = 0.0f;
    g_zeroVector.z = 0.0f;
    g_zeroVector.w = 1.0f;
    g_iBasis.x = 1.0f;
    g_jBasis.x = 0.0f;
    g_kBasis.x = 0.0f;
    g_jBasis.y = 1.0f;
    g_kBasis.y = 0.0f;
    g_iBasis.y = 0.0f;
    g_kBasis.z = 1.0f;
    g_iBasis.z = 0.0f;
    g_jBasis.z = 0.0f;
    g_iBasis.w = 1.0f;
    g_kBasis.w = 1.0f;
    g_kBasis.w = 1.0f;
}

void SinCos(f32 *s, f32 *c, f32 angle)
{
    f32 r;

    while (angle < 0.0f) {
        angle += 360.0f;
    }
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    r = 0.017453292f * angle;
    *s = sin(r);
    *c = cos(r);
}

void SinCosRad(f32 *s, f32 *c, f32 rad)
{
    f32 angle = rad * 57.29578f;
    f32 r;

    while (angle < 0.0f) {
        angle += 360.0f;
    }
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    r = 0.017453292f * angle;
    *s = sin(r);
    *c = cos(r);
}

float ArcTan(float y, float x) {
    return (float)atan2(y, x);
}

f32 Sqrt(f64 x)
{
    f64 guess;
    f64 result;
    s32 i;

    if (x > 0.0)
    {
        guess = __frsqrte(x);
        for (i = 0; i < 4; i++)
        {
            guess = 0.5 * guess * (3.0 - x * (guess * guess));
        }
        result = x * guess;
    }
    else if (x == 0.0)
    {
        result = 0.0;
    }
    else if (x)
    {
        result = *(f32 *)__float_nan;
    }
    else
    {
        result = *(f32 *)__float_huge;
    }
    return result;
}

f32 _fabs(f32 x) {
    f64 result = __fabs((f64)x);
    return result;
}

void AccumulateVector(void *arg0, void *arg1, void *arg2, f32 farg0) {
    M2C_FIELD(arg2, f32 *, 0) = (f32) ((M2C_FIELD(arg0, f32 *, 0) * farg0) + M2C_FIELD(arg1, f32 *, 0));
    M2C_FIELD(arg2, f32 *, 4) = (f32) ((M2C_FIELD(arg0, f32 *, 4) * farg0) + M2C_FIELD(arg1, f32 *, 4));
    M2C_FIELD(arg2, f32 *, 8) = (f32) ((M2C_FIELD(arg0, f32 *, 8) * farg0) + M2C_FIELD(arg1, f32 *, 8));
}

void ProjectVectorTangential(Vector *v, Vector *n, Vector *out) {
    f32 scale;

    scale = PSVECDotProduct(v, n);
    scale = -scale;
    out->x = scale * n->x + v->x;
    out->y = scale * n->y + v->y;
    out->z = scale * n->z + v->z;
}

void DistanceXYZ(Vector *a, Vector *b, f32 *dist)
{
  f32 dx;
  f32 dy;
  f32 dz;
  ;
  dx = a->x - b->x;
  dz = a->z - b->z;
  *dist = Sqrt((((a->y - b->y) * (a->y - b->y)) + (dx * dx)) + (dz * dz));
}

s32 CompareDistances(Vector *a, Vector *b, f32 dist)
{
    f32 dx = a->x - b->x;
    f32 dy = a->y - b->y;
    f32 dz = a->z - b->z;
    f32 sq = dist * dist;
    f32 len = dx * dx + dy * dy + dz * dz;

    if (len > sq) {
        return 1;
    }
    if (len < sq) {
        return -1;
    }
    return 0;
}

void XformVec(Matrix4 *m, Vector *v, Vector *out)
{
    f32 tx = m->v[2].x * v->z + (m->v[0].x * v->x + m->v[1].x * v->y) + m->v[3].x;
    f32 ty = m->v[2].y * v->z + (m->v[0].y * v->x + m->v[1].y * v->y) + m->v[3].y;
    f32 tz = m->v[2].z * v->z + (m->v[0].z * v->x + m->v[1].z * v->y) + m->v[3].z;

    out->x = tx;
    out->y = ty;
    out->z = tz;
}

void XformVec4x4(Matrix4 *m, Vector *v, Vector *out)
{
    f32 tx = m->v[2].x * v->z + (m->v[0].x * v->x + m->v[1].x * v->y) + m->v[3].x;
    f32 ty = m->v[2].y * v->z + (m->v[0].y * v->x + m->v[1].y * v->y) + m->v[3].y;
    f32 tz = m->v[2].z * v->z + (m->v[0].z * v->x + m->v[1].z * v->y) + m->v[3].z;

    out->x = tx;
    out->y = ty;
    out->z = tz;
}

void InverseXformVec(Matrix4 *m, Vector *v, Vector *out)
{
    f32 dx = v->x - m->v[3].x;
    f32 dy = v->y - m->v[3].y;
    f32 dz = v->z - m->v[3].z;

    f32 tx = m->v[0].x * dx + m->v[0].y * dy + m->v[0].z * dz;
    f32 ty = m->v[1].x * dx + m->v[1].y * dy + m->v[1].z * dz;
    f32 tz = m->v[2].x * dx + m->v[2].y * dy + m->v[2].z * dz;

    out->x = tx;
    out->y = ty;
    out->z = tz;
}

void XformVecOrientation(Matrix4 *m, Vector *v, Vector *out)
{
    f32 tx = m->v[2].x * v->z + (m->v[0].x * v->x + m->v[1].x * v->y);
    f32 ty = m->v[2].y * v->z + (m->v[0].y * v->x + m->v[1].y * v->y);
    f32 tz = m->v[2].z * v->z + (m->v[0].z * v->x + m->v[1].z * v->y);

    out->x = tx;
    out->y = ty;
    out->z = tz;
}

void InverseXformVecOrientation(Matrix4 *m, Vector *v, Vector *out)
{
    f32 tx = m->v[0].z * v->z + (m->v[0].x * v->x + m->v[0].y * v->y);
    f32 ty = m->v[1].z * v->z + (m->v[1].x * v->x + m->v[1].y * v->y);
    f32 tz = m->v[2].z * v->z + (m->v[2].x * v->x + m->v[2].y * v->y);

    out->x = tx;
    out->y = ty;
    out->z = tz;
}

void MultiplySubMatrix(f32 a[4][4], f32 b[4][4], f32 out[4][4]) {
    f32 tmp[4][4];

    tmp[0][0] = b[0][0] * a[0][0] + b[1][0] * a[0][1] + b[2][0] * a[0][2];
    tmp[0][1] = b[0][1] * a[0][0] + b[1][1] * a[0][1] + b[2][1] * a[0][2];
    tmp[0][2] = b[0][2] * a[0][0] + b[1][2] * a[0][1] + b[2][2] * a[0][2];
    tmp[0][3] = b[0][3] * a[0][0] + b[1][3] * a[0][1] + b[2][3] * a[0][2];
    tmp[1][0] = b[0][0] * a[1][0] + b[1][0] * a[1][1] + b[2][0] * a[1][2];
    tmp[1][1] = b[0][1] * a[1][0] + b[1][1] * a[1][1] + b[2][1] * a[1][2];
    tmp[1][2] = b[0][2] * a[1][0] + b[1][2] * a[1][1] + b[2][2] * a[1][2];
    tmp[1][3] = b[0][3] * a[1][0] + b[1][3] * a[1][1] + b[2][3] * a[1][2];
    tmp[2][0] = b[0][0] * a[2][0] + b[1][0] * a[2][1] + b[2][0] * a[2][2];
    tmp[2][1] = b[0][1] * a[2][0] + b[1][1] * a[2][1] + b[2][1] * a[2][2];
    tmp[2][2] = b[0][2] * a[2][0] + b[1][2] * a[2][1] + b[2][2] * a[2][2];
    tmp[2][3] = b[0][3] * a[2][0] + b[1][3] * a[2][1] + b[2][3] * a[2][2];
    memcpy(out, tmp, sizeof(tmp));
}

void MultiplyMatrix(Matrix4 *a, Matrix4 *b, Matrix4 *out)
{
    out->v[0].x = b->v[2].x * a->v[0].z + (b->v[0].x * a->v[0].x + b->v[1].x * a->v[0].y);
    out->v[0].y = b->v[2].y * a->v[0].z + (b->v[0].y * a->v[0].x + b->v[1].y * a->v[0].y);
    out->v[0].z = b->v[2].z * a->v[0].z + (b->v[0].z * a->v[0].x + b->v[1].z * a->v[0].y);
    out->v[0].w = 0.0f;
    out->v[1].x = b->v[2].x * a->v[1].z + (b->v[0].x * a->v[1].x + b->v[1].x * a->v[1].y);
    out->v[1].y = b->v[2].y * a->v[1].z + (b->v[0].y * a->v[1].x + b->v[1].y * a->v[1].y);
    out->v[1].z = b->v[2].z * a->v[1].z + (b->v[0].z * a->v[1].x + b->v[1].z * a->v[1].y);
    out->v[1].w = 0.0f;
    out->v[2].x = b->v[2].x * a->v[2].z + (b->v[0].x * a->v[2].x + b->v[1].x * a->v[2].y);
    out->v[2].y = b->v[2].y * a->v[2].z + (b->v[0].y * a->v[2].x + b->v[1].y * a->v[2].y);
    out->v[2].z = b->v[2].z * a->v[2].z + (b->v[0].z * a->v[2].x + b->v[1].z * a->v[2].y);
    out->v[2].w = 0.0f;
    out->v[3].x = b->v[3].x + (b->v[2].x * a->v[3].z + (b->v[0].x * a->v[3].x + b->v[1].x * a->v[3].y));
    out->v[3].y = b->v[3].y + (b->v[2].y * a->v[3].z + (b->v[0].y * a->v[3].x + b->v[1].y * a->v[3].y));
    out->v[3].z = b->v[3].z + (b->v[2].z * a->v[3].z + (b->v[0].z * a->v[3].x + b->v[1].z * a->v[3].y));
    out->v[3].w = 1.0f;
}

void SetOrientation(Matrix4 *m, f32 x, f32 y, f32 z)
{
    f32 c;
    f32 s;
    f32 t;

    m->v[2].y = 0.0f;
    m->v[2].x = 0.0f;
    m->v[1].z = 0.0f;
    m->v[1].x = 0.0f;
    m->v[0].z = 0.0f;
    m->v[0].y = 0.0f;
    m->v[2].z = 1.0f;
    m->v[1].y = 1.0f;
    m->v[0].x = 1.0f;
    m->v[2].w = 0.0f;
    m->v[1].w = 0.0f;
    m->v[0].w = 0.0f;
    m->v[3].w = 1.0f;

    if (z != 0.0f) {
        c = cos(z);
        s = sin(z);
        t = m->v[0].x;
        m->v[0].x = c * t - s * m->v[0].y;
        m->v[0].y = c * m->v[0].y + s * t;
        t = m->v[1].x;
        m->v[1].x = c * t - s * m->v[1].y;
        m->v[1].y = c * m->v[1].y + s * t;
        t = m->v[2].x;
        m->v[2].x = c * t - s * m->v[2].y;
        m->v[2].y = c * m->v[2].y + s * t;
    }
    if (y != 0.0f) {
        c = cos(y);
        s = sin(y);
        t = m->v[0].y;
        m->v[0].y = c * t - s * m->v[0].z;
        m->v[0].z = c * m->v[0].z + s * t;
        t = m->v[1].y;
        m->v[1].y = c * t - s * m->v[1].z;
        m->v[1].z = c * m->v[1].z + s * t;
        t = m->v[2].y;
        m->v[2].y = c * t - s * m->v[2].z;
        m->v[2].z = c * m->v[2].z + s * t;
    }
    if (x != 0.0f) {
        c = cos(x);
        s = sin(x);
        t = m->v[0].x;
        m->v[0].x = c * t + s * m->v[0].z;
        m->v[0].z = -s * t + c * m->v[0].z;
        t = m->v[1].x;
        m->v[1].x = c * t + s * m->v[1].z;
        m->v[1].z = -s * t + c * m->v[1].z;
        t = m->v[2].x;
        m->v[2].x = c * t + s * m->v[2].z;
        m->v[2].z = -s * t + c * m->v[2].z;
    }
}

void TransposeSubMatrix(Matrix4 *m)
{
    f32 temp;

    temp = m->v[0].y;
    m->v[0].y = m->v[1].x;
    m->v[1].x = temp;

    temp = m->v[0].z;
    m->v[0].z = m->v[2].x;
    m->v[2].x = temp;

    temp = m->v[1].z;
    m->v[1].z = m->v[2].y;
    m->v[2].y = temp;
}

s32 FloatsEqual(f32 a, f32 b, f32 c)
{
    f32 diff = a - b;

    if (diff < 0.0f)
    {
        diff *= -1.0f;
    }
    return diff < c;
}

void GenerateQuaternion(Vector4 *axis, Quaternion *q, f32 angle)
{
    f32 s;

    if (angle) {
        angle *= 0.5f;
        q->w = cos(angle);
        s = sin(angle);
        q->x = axis->x * s;
        q->y = axis->y * s;
        q->z = axis->z * s;
    } else {
        q->w = 1.0f;
        q->x = q->y = q->z = 0.0f;
    }
}

void GenerateSpinAngleFromQuaternion(Quaternion *q, Vector4 *axis, f32 *angle)
{
    f32 s;

    *angle = acos(q->w);
    s = sin(*angle);
    if (0.0f == s) {
        axis->x = 1.0f;
        axis->y = 0.0f;
        axis->z = 0.0f;
    } else {
        axis->x = q->x / s;
        axis->y = q->y / s;
        axis->z = q->z / s;
        *angle *= 2.0f;
    }
}

void QuaternionMult(Quaternion *a, Quaternion *b, Quaternion *out)
{
    out->w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
    out->x = a->y * b->z - a->z * b->y + a->w * b->x + a->x * b->w;
    out->y = a->z * b->x - a->x * b->z + a->w * b->y + a->y * b->w;
    out->z = a->x * b->y - a->y * b->x + a->w * b->z + a->z * b->w;
}

void GenerateQuaternionXYZ(Vector *angles, Quaternion *q) {
    Quaternion tmp = {0.0f, 0.0f, 0.0f, 1.0f};
    Vector4 axis;
    Quaternion r;

    axis.x = 0.0f;
    axis.y = 0.0f;
    axis.z = 1.0f;
    GenerateQuaternion(&axis, &r, angles->z);
    QuaternionMult(&r, &tmp, q);
    memcpy(&tmp, q, sizeof(Quaternion));
    axis.x = 1.0f;
    axis.y = 0.0f;
    axis.z = 0.0f;
    GenerateQuaternion(&axis, &r, angles->x);
    QuaternionMult(&r, &tmp, q);
    memcpy(&tmp, q, sizeof(Quaternion));
    axis.x = 0.0f;
    axis.y = 1.0f;
    axis.z = 0.0f;
    GenerateQuaternion(&axis, &r, angles->y);
    QuaternionMult(&r, &tmp, q);
}

void QuaternionToSubMatrix(Quaternion *q, Matrix4 *m)
{
    f32 s;
    f32 ww, yy, zz, xx;

    xx = q->x * q->x;
    yy = q->y * q->y;
    zz = q->z * q->z;
    ww = q->w * q->w;
    s = 1.0f / (xx + yy + zz + ww);
    m->v[0].x = s * (ww + xx - yy - zz);
    m->v[1].y = s * (ww - xx + yy - zz);
    m->v[2].z = s * (ww - xx - yy + zz);
    s *= 2.0f;
    m->v[0].y = s * (q->x * q->y + q->w * q->z);
    m->v[0].z = s * (q->x * q->z - q->w * q->y);
    m->v[1].x = s * (q->x * q->y - q->w * q->z);
    m->v[1].z = s * (q->y * q->z + q->w * q->x);
    m->v[2].x = s * (q->x * q->z + q->w * q->y);
    m->v[2].y = s * (q->y * q->z - q->w * q->x);
    m->v[2].w = 0.0f;
    m->v[1].w = 0.0f;
    m->v[0].w = 0.0f;
    m->v[3].w = 1.0f;
}

void SubMatrixToQuaternion(Matrix4 *m, Quaternion *q)
{
    f32 c;
    f32 s;

    c = 0.5f * (m->v[0].x + m->v[1].y + m->v[2].z - 1.0f);
    if (c <= -0.99999f) {
        q->w = 1e-13f;
        q->x = m->v[1].z - m->v[2].y;
        q->y = m->v[2].x - m->v[0].z;
        q->z = m->v[0].y - m->v[1].x;
        return;
    }
    q->w = Sqrt(0.5f * (1.0f + c));
    s = 1.0f / (4.0f * q->w);
    q->x = s * (m->v[1].z - m->v[2].y);
    q->y = s * (m->v[2].x - m->v[0].z);
    q->z = s * (m->v[0].y - m->v[1].x);
}

void GetComponentOfVector(void *arg0, void *arg1) {
    f32 temp_f1;

    temp_f1 = PSVECDotProduct(arg0, arg1);
    M2C_FIELD(arg1, f32 *, 0) = (f32) (temp_f1 * M2C_FIELD(arg0, f32 *, 0));
    M2C_FIELD(arg1, f32 *, 4) = (f32) (temp_f1 * M2C_FIELD(arg0, f32 *, 4));
    M2C_FIELD(arg1, f32 *, 8) = (f32) (temp_f1 * M2C_FIELD(arg0, f32 *, 8));
}

void ProjectVectorToNormal(Vector *n, Vector *v) {
    f32 scale;

    scale = -PSVECDotProduct(n, v);
    v->x = scale * n->x + v->x;
    v->y = scale * n->y + v->y;
    v->z = scale * n->z + v->z;
}

f32 LinearPointTest(Vector *point, Vector *start, Vector *end)
{
    Vector4 toPoint __attribute__((aligned(16)));
    Vector4 toEnd __attribute__((aligned(16)));
    f32 dot;

    PSVECSubtract(point, start, (Vector *)&toPoint);
    PSVECSubtract(end, start, (Vector *)&toEnd);
    dot = PSVECDotProduct((Vector *)&toPoint, (Vector *)&toEnd);
    return dot / (MagnitudeVector((Vector *)&toPoint) * MagnitudeVector((Vector *)&toEnd));
}

s8 SegmentVsSphere(Vector *start, Vector *end, Vector *center, f32 radius) {
    Vector toCenter __attribute__((aligned(16)));
    Vector dir __attribute__((aligned(16)));
    Vector negToCenter __attribute__((aligned(16)));
    Vector startToCenter __attribute__((aligned(16)));
    Vector cross __attribute__((aligned(16)));
    f32 radiusSq = radius * radius;

    PSVECSubtract(start, end, &dir);
    if (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z < radiusSq) {
        return 1;
    }
    if (PSVECSquareDistance(start, center) < radiusSq) {
        return 1;
    }
    PSVECSubtract(center, end, &toCenter);
    if (PSVECDotProduct(&toCenter, &dir) < 0.0f) {
        return 0;
    }
    negToCenter.x = -toCenter.x;
    negToCenter.y = -toCenter.y;
    negToCenter.z = -toCenter.z;
    PSVECSubtract(start, center, &startToCenter);
    if (PSVECDotProduct(&negToCenter, &startToCenter) < 0.0f) {
        return 0;
    }
    PSVECCrossProduct(&toCenter, &dir, &cross);
    return cross.x * cross.x + cross.y * cross.y + cross.z * cross.z <
           radiusSq * (toCenter.x * toCenter.x + toCenter.y * toCenter.y + toCenter.z * toCenter.z);
}

void MinMaxVec(Vector4 *min, Vector4 *max, Vector4 *v)
{
    if (min->x > v->x) {
        min->x = v->x;
    }
    if (min->y > v->y) {
        min->y = v->y;
    }
    if (min->z > v->z) {
        min->z = v->z;
    }
    if (min->w > v->w) {
        min->w = v->w;
    }
    if (max->x < v->x) {
        max->x = v->x;
    }
    if (max->y < v->y) {
        max->y = v->y;
    }
    if (max->z < v->z) {
        max->z = v->z;
    }
    if (max->w < v->w) {
        max->w = v->w;
    }
}

void Distance_Plane_4(Vector4 *planes, Vector4 *point, f32 *out)
{

    out[0] = Distance_Plane(&planes[0], point);
    out[1] = Distance_Plane(&planes[1], point);
    out[2] = Distance_Plane(&planes[2], point);
    out[3] = Distance_Plane(&planes[3], point);
}

void ScaleMatrix(Matrix4 *m, Vector4 *s)
{
    m->v[0].x *= s->x;
    m->v[0].y *= s->x;
    m->v[0].z *= s->x;
    m->v[0].w *= s->x;
    m->v[1].x *= s->y;
    m->v[1].y *= s->y;
    m->v[1].z *= s->y;
    m->v[1].w *= s->y;
    m->v[2].x *= s->z;
    m->v[2].y *= s->z;
    m->v[2].z *= s->z;
    m->v[2].w *= s->z;
    m->v[3].x *= s->w;
    m->v[3].y *= s->w;
    m->v[3].z *= s->w;
    m->v[3].w *= s->w;
}

