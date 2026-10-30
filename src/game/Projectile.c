#include "common.h"

void Projectile_Release(void * projectile)
{
    extern u8 g_projectileVTable[];

    LensFlare_Delete(((s32 *)projectile)[0x4c]);
    Thing_Release((s32)HND(projectile));
    LightHolder_Delete(((s32 *)projectile)[0x4d]);
    ((s32 *)projectile)[0x23] = (s32)HND(g_projectileVTable);
}

void Projectile_MoveBase(u8 *proj)
{
extern f32 g_fDeltaTimeSec;
extern f32 g_fDeltaTimeMS;
extern s32 g_iDeltaTimeMS;
extern Vector4 g_zeroVector;
extern u8 g_projectileVTable[];
f32 MagnitudeVector(void *v);
f32 dt;
u8 type;
u32 id;
Vector4 cur;
Vector4 step;
Vector4 move;
struct ThingParams
{
Vector4 a;
Vector4 b;
Vector4 c;
Vector4 zero;
u32 id;
s32 player;
f32 f0;
f32 f1;
} q __attribute__((aligned(16)));
struct ThingParams p __attribute__((aligned(16)));
dt = g_fDeltaTimeSec;
*(u16 *)(proj + 0x15C) = 0;
if (((s32 *)proj)[0x49] < 0) {
id = ((u32 *)SLOT_LOAD(void *, (u32 *)(proj) + (0x48)))[0x1E];
if (id != 0) {
q.id = id;
*(Vector4 *)&q.a = *(Vector4 *)(proj + 0x40);
q.b = *(Vector4 *)(proj + 0x50);
q.c = *(Vector4 *)(proj + 0x00);
q.player = ((s32 *)proj)[0x25];
q.f0 = 0.0f;
q.f1 = 0.0f;
q.zero = g_zeroVector;
ThingManager_DARYL_CreateThing(1, &q);
}
LensFlare_Delete(((s32 *)proj)[0x4C]);
Thing_Release((s32)HND(proj));
LightHolder_Delete(((s32 *)proj)[0x4D]);
SLOT_STORE((proj + 0x8C), g_projectileVTable);
}
((s32 *)proj)[0x49] -= g_iDeltaTimeMS;
type = *(u8 *)(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x04);
if (type != 0) {
if (type == 1) {
*(f32 *)(proj + 0x128) += 1000.0f * dt;
}
else if (type == 2) {
PSVECScale(proj, &move, dt);
*(f32 *)(proj + 0x128) += MagnitudeVector(&move);
}
if (*(f32 *)(proj + 0x128) >= 0.0f) {
cur = *(Vector4 *)(proj + 0x40);
PSVECScale(proj, &step, -*(f32 *)(proj + 0x12C));
if (*(f32 *)(proj + 0x12C)) {
while (*(f32 *)(proj + 0x128) > *(f32 *)(proj + 0x12C)) {
id = *(u32 *)(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x70);
if (id != 0) {
p.id = id;
p.a = cur;
p.b = *(Vector4 *)(proj + 0x50);
p.c = *(Vector4 *)(proj + 0x00);
p.player = ((s32 *)proj)[0x25];
p.f0 = 0.0f;
p.f1 = 0.0f;
p.zero = g_zeroVector;
ThingManager_DARYL_CreateThing(1, &p);
}
*(f32 *)(proj + 0x128) -= *(f32 *)(proj + 0x12C);
PSVECAdd(&cur, &step, &cur);
}
}
}
}
ExtractColorKey(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x94, proj + 0xF0, g_fDeltaTimeMS * *(f32 *)(proj + 0x140));
ExtractScaleKey(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x8C, proj + 0xC0, g_fDeltaTimeMS * *(f32 *)(proj + 0x140));
}


void Projectile_ProximityDetection(u8 *proj)
{
    extern s32 g_iDeltaTimeMS;
    extern Vector4 g_zeroVector;
    extern u8 g_projectileVTable[];
    void InverseXformVec(void *mtx, Vector4 *in, Vector4 *out);
    struct { Vector4 a; Vector4 b; Vector4 c; Vector4 zero; u32 id; s32 player; f32 f0; f32 f1; } p __attribute__((aligned(16)));
    u8 *cars[16];
    u32 count, i;
    void *source;
    f32 radius;
    u8 *info;
    u32 id;
    u8 *slave;
    radius = ((f32 *)SLOT_LOAD(void *, (u32 *)(proj) + (0x48)))[0xD];
    source = PTR(Thing_GetSource(proj));
    ((s32 *)proj)[0x51] -= g_iDeltaTimeMS;
    if (((s32 *)proj)[0x51] < 0)
    {
        count = ThingManager_GetActiveCars(cars, 16);
        for (i = 0; i < count; i++)
        {
            if (cars[i] != source)
            {
                if (PSVECSquareDistance(proj + 0x40, cars[i] + 0x40) < (radius + ((f32 *)cars[i])[0x1C]) * (radius + ((f32 *)cars[i])[0x1C]))
                {
                    info = SLOT_LOAD(void *, (u32 *)(proj) + (0x48));
                    id = ((u32 *)info)[0x1D];
                    if (id != 0)
                    {
                        p.f0 = 0.0f;
                        p.f1 = 0.0f;
                        p.player = ((s32 *)proj)[0x25];
                        p.id = id;
                        p.zero = g_zeroVector;
                        if (((u8 *)info)[7] != 0)
                        {
                            p.b = *(Vector4 *)(proj + 0x50);
                            p.c.x = 0.0f;
                            p.c.y = 0.0f;
                            p.c.z = 0.0f;
                            InverseXformVec(cars[i] + 0x10, (Vector4 *)(proj + 0x40), &p.a);
                            slave = ThingManager_DARYL_CreateSlave(1, &p, 0xFF);
                            if (slave != NULL)
                            {
                                slave[0x83] = 0;
                                ThingManagerList_Append(cars[i] + 0x88, slave);
                            }
                        }
                        else
                        {
                            p.c = *(Vector4 *)(proj + 0x00);
                            p.a = *(Vector4 *)(proj + 0x40);
                            p.b.w = 1.0f;
                            p.b.x = 0.0f;
                            p.b.y = 0.0f;
                            p.b.z = 0.0f;
                            ThingManager_DARYL_CreateThing(1, &p);
                        }
                    }
                    ((Vector4 *)proj)->x = 0.0f;
                    ((Vector4 *)proj)->y = 0.0f;
                    ((Vector4 *)proj)->z = 0.0f;
                    LensFlare_Delete(((s32 *)proj)[0x4C]);
                    Thing_Release((s32)HND(proj));
                    LightHolder_Delete(((s32 *)proj)[0x4D]);
                    SLOT_STORE((proj + 0x8C), g_projectileVTable);
                    ((u32 *)proj)[0x1D] |= 2;
                    return;
                }
            }
        }
        ((s32 *)proj)[0x51] += ((s32 *)SLOT_LOAD(void *, (u32 *)(proj) + (0x48)))[0xE];
    }
}

s32 Projectile_PreMove(u8 *proj)
{
    extern f32 g_fDeltaTimeSec;
    Vector4 q;
    Vector4 result;
    void *temp;
    f32 step;
    f32 mag;
    f32 scale;

    GenerateQuaternion(proj + 0xB0, &q, ((f32 *)proj)[0x4F] * g_fDeltaTimeSec);
    QuaternionMult((Vector4 *)(proj + 0x50), &q, &result);
    *(Vector4 *)(proj + 0x50) = result;
    ((f32 *)proj)[1] += ((f32 *)proj)[0x52] * g_fDeltaTimeSec;
    temp = SLOT_LOAD(void *, (u32 *)(proj) + (0x48));
    if (((f32 *)temp)[0x10])
    {
        step = ((f32 *)temp)[0x10] * g_fDeltaTimeSec;
        ((f32 *)proj)[0] += ((f32 *)(proj + 0x30))[0] * step;
        ((f32 *)proj)[1] += ((f32 *)(proj + 0x30))[1] * step;
        ((f32 *)proj)[2] += ((f32 *)(proj + 0x30))[2] * step;
    }
    mag = PSVECSquareMag((Vector4 *)proj);
    temp = SLOT_LOAD(void *, (u32 *)(proj) + (0x48));
    if (mag > (((f32 *)temp)[0x11] * ((f32 *)temp)[0x11]))
    {
        scale = 1.0f / Sqrt(mag);
        ((f32 *)proj)[0] *= scale;
        ((f32 *)proj)[1] *= scale;
        ((f32 *)proj)[2] *= scale;
    }
    temp = SLOT_LOAD(void *, (u32 *)(proj) + (0x48));
    if (((f32 *)temp)[0xD])
    {
        Projectile_ProximityDetection(proj);
    }
    return 1;
}

s32 Projectile_Move(u8 *proj)
{
extern f32 g_fDeltaTimeSec;
extern f32 g_fDeltaTimeMS;
extern s32 g_iDeltaTimeMS;
extern Vector4 g_zeroVector;
extern u8 g_projectileVTable[];
f32 MagnitudeVector(void *v);
f32 dt;
u8 type;
u32 id;
Vector4 cur;
Vector4 step;
Vector4 move;
struct ThingParams
{
Vector4 a;
Vector4 b;
Vector4 c;
Vector4 zero;
u32 id;
s32 player;
f32 f0;
f32 f1;
} q __attribute__((aligned(16)));
struct ThingParams p __attribute__((aligned(16)));
dt = g_fDeltaTimeSec;
*(u16 *)(proj + 0x15C) = 0;
if (((s32 *)proj)[0x49] < 0) {
id = ((u32 *)SLOT_LOAD(void *, (u32 *)(proj) + (0x48)))[0x1E];
if (id != 0) {
q.id = id;
*(Vector4 *)&q.a = *(Vector4 *)(proj + 0x40);
q.b = *(Vector4 *)(proj + 0x50);
q.c = *(Vector4 *)(proj + 0x00);
q.player = ((s32 *)proj)[0x25];
q.f0 = 0.0f;
q.f1 = 0.0f;
q.zero = g_zeroVector;
ThingManager_DARYL_CreateThing(1, &q);
}
LensFlare_Delete(((s32 *)proj)[0x4C]);
Thing_Release((s32)HND(proj));
LightHolder_Delete(((s32 *)proj)[0x4D]);
SLOT_STORE((proj + 0x8C), g_projectileVTable);
}
((s32 *)proj)[0x49] -= g_iDeltaTimeMS;
type = *(u8 *)(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x04);
if (type != 0) {
if (type == 1) {
*(f32 *)(proj + 0x128) += 1000.0f * dt;
}
else if (type == 2) {
PSVECScale(proj, &move, dt);
*(f32 *)(proj + 0x128) += MagnitudeVector(&move);
}
if (*(f32 *)(proj + 0x128) >= 0.0f) {
cur = *(Vector4 *)(proj + 0x40);
PSVECScale(proj, &step, -*(f32 *)(proj + 0x12C));
if (*(f32 *)(proj + 0x12C)) {
while (*(f32 *)(proj + 0x128) > *(f32 *)(proj + 0x12C)) {
id = *(u32 *)(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x70);
if (id != 0) {
p.id = id;
p.a = cur;
p.b = *(Vector4 *)(proj + 0x50);
p.c = *(Vector4 *)(proj + 0x00);
p.player = ((s32 *)proj)[0x25];
p.f0 = 0.0f;
p.f1 = 0.0f;
p.zero = g_zeroVector;
ThingManager_DARYL_CreateThing(1, &p);
}
*(f32 *)(proj + 0x128) -= *(f32 *)(proj + 0x12C);
PSVECAdd(&cur, &step, &cur);
}
}
}
}
ExtractColorKey(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x94, proj + 0xF0, g_fDeltaTimeMS * *(f32 *)(proj + 0x140));
ExtractScaleKey(SLOT_LOAD(u8 *, (proj + 0x120)) + 0x8C, proj + 0xC0, g_fDeltaTimeMS * *(f32 *)(proj + 0x140));
Thing_CollideWithForceBoxes(proj);
return 1;
}

void Projectile_Draw(u8 *proj)
{
    void *Gfx_AddBillBoardSprite(u16 arg0);
    void *sprite;
    Matrix4 mtx __attribute__((aligned(16)));
    Matrix4 scale __attribute__((aligned(16)));
    u32 color;

    if (((u8 *)SLOT_LOAD(void *, (u32 *)(proj) + (0x48)))[0] == 1)
    {
        QuaternionToSubMatrix(proj + 0x50, proj + 0x10);
        memset(&scale, 0, sizeof(Matrix4));
        scale.v[0].x = ((f32 *)proj)[0x30];
        scale.v[1].y = ((f32 *)proj)[0x31];
        scale.v[2].z = ((f32 *)proj)[0x32];
        scale.v[3].w = 1.0f;
        MultiplyMatrix(&scale, proj + 0x10, &mtx);
        switch (((u8 *)SLOT_LOAD(void *, (u32 *)(proj) + (0x48)))[1])
        {
        case 0:
            if (((u32 *)proj)[0x1d] & 0x4000)
            {
                color = ((u32 *)proj)[0x45];
                Gfx_SetBaseColor(&color);
                Gfx_EnableState(0x10);
            }
            Gfx_DrawGeometry(HND(SLOT_LOAD(void *, (u32 *)(SLOT_LOAD(void *, (u32 *)(proj) + (0x48))) + (4))), HND(&mtx));
            if (((u32 *)proj)[0x1d] & 0x4000)
            {
                Gfx_DisableState(0x10);
            }
            break;
        case 1:
            sprite = Gfx_AddBillBoardSprite(0);
            *(u8 *)((u8 *)sprite + 0x2a) = *(u8 *)SLOT_LOAD(void *, (u32 *)(SLOT_LOAD(void *, (u32 *)(proj) + (0x48))) + (0x18));
            *(u32 *)((u8 *)sprite + 0x24) = ((u32 *)proj)[0x45];
            *(f32 *)((u8 *)sprite + 0xc) = ((f32 *)proj)[0x30];
            *(f32 *)((u8 *)sprite + 0x0) = ((f32 *)proj)[0x10];
            *(f32 *)((u8 *)sprite + 0x4) = ((f32 *)proj)[0x11];
            *(f32 *)((u8 *)sprite + 0x8) = ((f32 *)proj)[0x12];
            *(f32 *)((u8 *)sprite + 0x1c) = 360.0f;
            break;
        }
    }
}

void Projectile_YAim(u8 *proj, Vector4 *target)
{
    void InverseXformVec(void *mtx, Vector4 *in, Vector4 *out);
    f32 MagnitudeVector(void *v);
    Vector4 local __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16))) = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector4 q __attribute__((aligned(16)));
    Vector4 result __attribute__((aligned(16)));
    f32 angle;
    f32 speed;

    InverseXformVec(proj + 0x10, target, &local);
    angle = ArcTan(local.y, local.z);
    if (FloatsEqual(angle, 0.0f, 0.2617993950843811f)) {
        speed = MagnitudeVector(proj);
        GenerateQuaternion(&axis, &q, -angle);
        QuaternionMult((Vector4 *)(proj + 0x50), &q, &result);
        *(Vector4 *)(proj + 0x50) = result;
        QuaternionToSubMatrix(proj + 0x50, proj + 0x10);
        ((Vector4 *)proj)->x = ((Vector4 *)(proj + 0x30))->x * speed;
        ((Vector4 *)proj)->y = ((Vector4 *)(proj + 0x30))->y * speed;
        ((Vector4 *)proj)->z = ((Vector4 *)(proj + 0x30))->z * speed;
    }
}

void Projectile_DynamicCollideImpact(void * proj, void * dir, s32 damage, f32 force)
{
    void Projectile_DynamicCollisionCorrection(void *);
    f32 scale;
    void * temp;

    ((s32 *)proj)[0x1e] -= damage;
    if (dir != NULL)
    {
        temp = SLOT_LOAD(void *, (u32 *)(proj) + (0x48));
        scale = force / ((f32 *)temp)[0xb];
        ((f32 *)proj)[0] = ((f32 *)dir)[4] * scale + ((f32 *)proj)[0];
        ((f32 *)proj)[1] = ((f32 *)dir)[5] * scale + ((f32 *)proj)[1];
        ((f32 *)proj)[2] = ((f32 *)dir)[6] * scale + ((f32 *)proj)[2];
        Projectile_DynamicCollisionCorrection(proj);
    }
}

f32 Projectile_GetMass(void * arg0)
{
    void * temp;

    temp = SLOT_LOAD(void *, (u32 *)(arg0) + (0x48));
    return ((f32 *)temp)[0xb];
}

s32 Projectile_Invalidate(void * arg0, u32 arg1, u32 arg2)
{
    u32 temp;

    temp = ((u32 *)arg0)[0x48];
    if ((temp >= arg1) && (temp < arg2))
    {
        return 1;
    }
    return 0;
}

