

#include "common.h"

extern u8 g_CurrentViews;



void Blast_Action(u8 *blast, Vector4 *pos, u8 *source) {
    extern Vector4 g_zeroVector;
    struct GridResult {
        u32 data;
        u32 id;
    };
    struct GridResult results[256];
    struct {
        Vector4 pos;
        Vector4 dir;
        PTR32(u8) thing;
        PTR32(u8) source;
        u32 unk28;
        u8 unk2C[0x50 - 0x2C];
    } __attribute__((aligned(16))) impact;
    struct { Vector4 pos; Vector4 quat; Vector4 row; Vector4 vel; u32 id; u32 unk74; f32 unk78; f32 unk7C; } __attribute__((aligned(16))) params;
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 t;
    u32 count;
    s32 damageType;
    u32 i;
    u8 *thing;
    u8 *data;
    s8 skip;
    u32 mask;
    f32 outer;
    f32 range;
    f32 inner;
    f32 innerSq;
    f32 outerSq;
    f32 force;
    f32 dist;
    f32 scale;

    mask = 0;
    min = *pos;
    max = min;
    force = *(f32 *)(SLOT_LOAD(u8 *, (blast + 0x1C)) + 0x10);
    damageType = *(s32 *)(SLOT_LOAD(u8 *, (blast + 0x1C)) + 0xC);
    outer = ((f32 *)SLOT_LOAD(u8 *, (blast + 0x30)))[1];
    inner = ((f32 *)SLOT_LOAD(u8 *, (blast + 0x30)))[0];
    min.x -= outer;
    min.y -= outer;
    min.z -= outer;
    max.x += outer;
    max.y += outer;
    max.z += outer;
    innerSq = inner * inner;
    outerSq = outer * outer;
    if (*SLOT_LOAD(u8 *, (blast + 0x1C)) & 1) {
        mask |= 4;
    }
    if (*SLOT_LOAD(u8 *, (blast + 0x1C)) & 2) {
        mask |= 1;
    }
    if (*SLOT_LOAD(u8 *, (blast + 0x1C)) & 4) {
        mask |= 2;
    }
    count = Grid_GetObjects(&min, &max, mask, results, 0x100);
    range = outer - inner;
    for (i = 0; i < count; i++) {
        thing = (u8 *)PTR(results[i].id);
        skip = 0;
        if (source != NULL && *(u32 *)(source + 0x94) != 0 && *(u32 *)(thing + 0x94) == *(u32 *)(source + 0x94)) {
            if (blast[5] == 0) {
                skip = 1;
            }
        } else if (blast[5] == 2) {
            skip = 1;
        }
        if (skip) {
            continue;
        }
        if (*(u32 *)(thing + 0x74) & 0x200000) {
            continue;
        }
        if ((*(u32 *)(thing + 0x74) & 8) && damageType == 0) {
            continue;
        }
        if (*(u32 *)(thing + 0x74) & 0x01000002) {
            continue;
        }
        if (*(s16 *)(thing + 0x80) == 0) {
            continue;
        }
        if (*(s16 *)(thing + 0x80) == 3 && !(*SLOT_LOAD(u8 *, (blast + 0x1C)) & 4)) {
            continue;
        }
        dist = PSVECSquareDistance(pos, (Vector4 *)(thing + 0x40));
        if (!(dist < outerSq)) {
            continue;
        }
        t = 1.0f;
        scale = t;
        dir.x = *(f32 *)(thing + 0x40) - pos->x;
        dir.y = *(f32 *)(thing + 0x44) - pos->y;
        dir.z = *(f32 *)(thing + 0x48) - pos->z;
        if (*(SLOT_LOAD(u8 *, (blast + 0x1C)) + 1) && RayHitsTerrain(pos, (Vector4 *)(thing + 0x40), &t, NULL)) {
            continue;
        }
        if (dist > innerSq) {
            scale = (outer - Sqrt(dist)) / range;
        }
        data = SLOT_LOAD(u8 *, (blast + 0x1C));
        if (data != NULL) {
            switch (data[3]) {
            case 2:
                if (SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable) {
                    Vector4 impulse __attribute__((aligned(16))) = {0.0f, -9.0f, 0.0f, 0.0f};
                    CarA_SlaveWeaponStop(thing, 0);
                    *(s32 *)(thing + 0x46C) = 9000;
                    CarA_StuntImpulse(thing, &g_zeroVector, &impulse);
                }
                break;
            case 3:
                if (SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable) {
                    CarA_SlaveWeaponStop(thing, 1);
                    *(s32 *)(thing + 0x470) = 5000;
                }
                break;
            case 4:
                if (SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable) {
                    CarA_SlaveWeaponStop(thing, 2);
                    *(s32 *)(thing + 0x474) = 0;
                }
                break;
            case 5:
                if (SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable) {
                    *(s32 *)(thing + 0x78) = *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (thing + 0x3BC)) + 0xA0)) + 0x14);
                    *(s32 *)(thing + 0x374) = 0;
                    MotionState_ReleaseSlavesByType((s32)HND((thing + 0x330)), 2);
                    CarA_SlaveWeaponStop(thing, 4);
                    *(s32 *)(thing + 0x47C) = 0;
                }
                break;
            case 6:
                if (SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable) {
                    CarA_SlaveWeaponStop(thing, 3);
                    *(s32 *)(thing + 0x478) = 0;
                }
                break;
            }
            if (*(u32 *)(SLOT_LOAD(u8 *, (blast + 0x1C)) + 0x18) != 0) {
                params.id = *(u32 *)(SLOT_LOAD(u8 *, (blast + 0x1C)) + 0x18);
                params.pos = *(Vector4 *)(thing + 0x40);
                params.row = *(Vector4 *)(thing + 0x0);
                params.quat = *(Vector4 *)(thing + 0x50);
                params.unk74 = 0;
                params.unk7C = params.unk78 = 0.0f;
                params.vel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &params);
            }
        }
        force *= scale;
        if ((((f32 (*)(u8 *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x14))))(thing) > 0.01f) {
            force /= (((f32 (*)(u8 *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x14))))(thing);
        } else {
            force *= 2000.0f;
        }
        if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f) {
            dir.y = 1.0f;
        } else if (PSVECSquareMag(&dir)) {
            PSVECNormalize(&dir, &dir);
        }
        if (force > 600.0f) {
            force = 600.0f;
        }
        impact.dir = dir;
        impact.pos = *pos;
        if (SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable) {
            impact.pos.x = pos->x - 4.0f * *(f32 *)(thing + 0x30);
            impact.pos.y = pos->y - 4.0f * *(f32 *)(thing + 0x34);
            impact.pos.z = pos->z - 4.0f * *(f32 *)(thing + 0x38);
            impact.pos.x = 2.0f * *(f32 *)(thing + 0x20) + pos->x;
            impact.pos.y = 2.0f * *(f32 *)(thing + 0x24) + pos->y;
            impact.pos.z = 2.0f * *(f32 *)(thing + 0x28) + pos->z;
        }
        SETP(impact.thing, thing);
        SETP(impact.source, source);
        impact.unk28 = 0;
        force *= (((f32 (*)(u8 *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x14))))(thing);
        (((void (*)(u8 *, void *, s32, f32))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x18))))(thing, &impact, damageType, force);
    }
}
s32 EventSeq_Init(u8 *seq, u8 *data)
{

    Thing_Init(seq, HND(data));
    *(Vector4 *)(seq + 0x0) = *(Vector4 *)(data + 0x20);
    *(Vector4 *)(seq + 0x50) = *(Vector4 *)(data + 0x10);
    *(s32 *)(seq + 0x94) = *(s32 *)(data + 0x44);
    *(Vector4 *)(seq + 0xC0) = *(Vector4 *)(data + 0x30);
    *(f32 *)(seq + 0xF8) = *(f32 *)(data + 0x48);
    QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
    *(Vector4 *)(seq + 0x40) = *(Vector4 *)(data + 0x0);
    *(s32 *)(seq + 0xD0) = *(s32 *)(data + 0x40);
    *(s32 *)(seq + 0xDC) = 0;
    *(s32 *)(seq + 0xE0) = 0;
    *(s32 *)(seq + 0xD4) = 0;
    *(u8 *)(seq + 0xF0) = 0;
    *(u16 *)(seq + 0x80) = 0;
    *(Vector4 *)(seq + 0x50) = *(Vector4 *)(data + 0x10);
    *(u32 *)(seq + 0x74) |= 0x80202;
    return 1;
}

void EventSeq_Release(void *arg0)
{
    Thing_Release(arg0);
}

void EventSeq_Advanced_GetOffset(u8 *seq, void *arg1, Vector4 *out) {
    typedef struct {
        f32 base;
        f32 range;
        f32 scale;
        f32 speed;
        u8 axis;
        u8 damped;
        u8 spin;
    } EventSeqCircle;
    typedef struct {
        f32 base;
        f32 range;
        u8 pad8;
        u8 type;
        u8 padA[2];
        PTR32(void) data;
    } EventSeqAdvanced;
    f32 scale;

    switch (seq[0x21]) {
    case 1: {
        f32 *range = SLOT_LOAD(f32 *, (seq + 0x28));

        out->x = RandomAdd(range[0], range[1]);
        out->y = RandomAdd(range[2], range[3]);
        out->z = RandomAdd(range[4], range[5]);
        break;
    }
    case 2: {
        EventSeqAdvanced *adv = SLOT_LOAD(EventSeqAdvanced *, (seq + 0x28));

        scale = RandomAdd(adv->base, adv->range);
        switch (adv->type) {
        case 0: {
            Vector4 *dir = GETP(adv->data);

            RandomNumberGenerator_GetRandomVector(out);
            out->x *= scale * dir->x;
            out->y *= scale * dir->y;
            out->z *= scale * dir->z;
            break;
        }
        case 1: {
            Vector4 *dir = GETP(adv->data);
            Vector4 mul __attribute__((aligned(16))) = {1.0f, 1.0f, 1.0f, 0.0f};

            RandomNumberGenerator_GetRandomVector(out);
            switch (((u8 *)dir)[0x10]) {
            case 4:
                if (out->x < 0.0f) {
                    out->x *= -1.0f;
                }
                mul.x = dir->x;
                break;
            case 5:
                if (out->x > 0.0f) {
                    out->x *= -1.0f;
                }
                mul.x = dir->x;
                break;
            case 2:
                if (out->y < 0.0f) {
                    out->y *= -1.0f;
                }
                mul.y = dir->x;
                break;
            case 3:
                if (out->y > 0.0f) {
                    out->y *= -1.0f;
                }
                mul.y = dir->x;
                break;
            case 0:
                if (out->z < 0.0f) {
                    out->z *= -1.0f;
                }
                mul.z = dir->x;
                break;
            case 1:
                if (out->z > 0.0f) {
                    out->z *= -1.0f;
                }
                mul.z = dir->x;
                break;
            }
            out->x *= scale * mul.x;
            out->y *= scale * mul.y;
            out->z *= scale * mul.z;
            break;
        }
        case 2: {
            EventSeqCircle *circle = GETP(adv->data);
            f32 height = RandomAdd(circle->base, circle->range);
            f32 t = 1.0f;
            f32 angle;
            f32 s;
            f32 c;

            if (circle->range) {
                if (circle->damped && circle->scale) {
                    t = 1.0f - (height - circle->base) / (circle->range * circle->scale);
                    scale *= t;
                } else {
                    t = 1.0f - (height - circle->base) / circle->range;
                }
            }
            if (circle->spin) {
                if (circle->damped) {
                    angle = t * (360.0f * circle->speed * circle->scale);
                } else {
                    angle = t * (360.0f * circle->speed);
                }
            } else {
                angle = RandomAdd(0.0f, 360.0f);
            }
            SinCos(&s, &c, angle);
            switch (circle->axis) {
            case 0:
                out->x = height;
                out->y = scale * c;
                out->z = scale * s;
                break;
            case 1:
                out->x = scale * c;
                out->y = height;
                out->z = scale * s;
                break;
            case 2:
                out->x = scale * c;
                out->y = scale * s;
                out->z = height;
                break;
            }
            break;
        }
        }
        break;
    }
    default:
        out->w = out->x = out->y = out->z = 0.0f;
        break;
    }
}

void EventSeq_Advanced_GetInitData(u8 *emit, void *arg1, Vector4 *vel, Vector4 *pos, Vector4 *quat, Vector4 *angVel) {
    typedef struct EventSeqThing {
        Vector4 vel;
        Matrix4 mtx;
        Vector4 quat;
    } EventSeqThing;
    EventSeqThing *seq = arg1;
    Matrix4 mtx1;
    Matrix4 res;
    Matrix4 rot;
    Matrix4 mtx3;
    Matrix4 mtx4;
    Vector4 tmp __attribute__((aligned(16)));
    Vector4 v __attribute__((aligned(16)));
    Vector4 a __attribute__((aligned(16)));
    Vector4 offset __attribute__((aligned(16)));
    Vector4 dir3 __attribute__((aligned(16)));

    EventSeq_Advanced_GetOffset(emit, &seq->mtx, &offset);
    switch (emit[0x22]) {
    case 0:
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        if (quat != NULL) {
            *quat = seq->quat;
        }
        break;
    case 1: {
        f32 speed;
        f32 spin;

        speed = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        spin = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        RandomNumberGenerator_GetRandomSubMatrix(&mtx1);
        v.x = mtx1.v[2].x * speed;
        v.y = mtx1.v[2].y * speed;
        v.z = mtx1.v[2].z * speed;
        a.x = mtx1.v[2].x * spin;
        a.y = mtx1.v[2].y * spin;
        a.z = mtx1.v[2].z * spin;
        offset.x += mtx1.v[2].x;
        offset.y += mtx1.v[2].y;
        offset.z += mtx1.v[2].z;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx1, quat);
        }
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        break;
    }
    case 2: {
        f32 *range = SLOT_LOAD(f32 *, (emit + 0x2C));
        f32 ry;
        f32 rx;
        f32 rz;

        ry = RandomDirVar(range[2], range[3]);
        rx = -RandomDirVar(range[0], range[1]);
        rz = RandomDirVar(range[4], range[5]);
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        SetOrientation(&rot, 0.01745329238474369f * ry, 0.01745329238474369f * rx, 0.01745329238474369f * rz);
        rot.v[3].x = rot.v[3].y = rot.v[3].z = 0.0f;
        MultiplyMatrix(&rot, &seq->mtx, &res);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        if (quat != NULL) {
            SubMatrixToQuaternion(&res, quat);
        }
        XformVecOrientation(&res, &v, vel);
        XformVecOrientation(&res, &a, angVel);
        break;
    }
    case 3: {
        f32 speed;
        f32 spin;

        mtx3 = seq->mtx;
        speed = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        spin = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        dir3 = offset;
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        if (!dir3.x && !dir3.y && !dir3.z) {
            dir3.x = 0.0f;
            dir3.y = 0.0f;
            dir3.z = 1.0f;
        } else if (PSVECSquareMag(&dir3)) {
            PSVECNormalize(&dir3, &dir3);
        }
        v.x = dir3.x * speed;
        v.y = dir3.y * speed;
        v.z = dir3.z * speed;
        a.x = dir3.x * spin;
        a.y = dir3.y * spin;
        a.z = dir3.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx3, quat);
        }
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        break;
    }
    case 4: {
        f32 speed;
        f32 spin;
        u8 *info;

        speed = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        spin = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        info = SLOT_LOAD(u8 *, (emit + 0x2C));
        RandomNumberGenerator_GetRandomSubMatrix(&mtx4);
        switch (info[0x10]) {
        case 4:
            if (mtx4.v[2].x < 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 5:
            if (mtx4.v[2].x > 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 2:
            if (mtx4.v[2].y < 0.0f) {
                mtx4.v[0].x *= -1.0f;
                mtx4.v[0].y *= -1.0f;
                mtx4.v[0].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 3:
            if (mtx4.v[2].y > 0.0f) {
                mtx4.v[0].x *= -1.0f;
                mtx4.v[0].y *= -1.0f;
                mtx4.v[0].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 0:
            if (mtx4.v[2].z < 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 1:
            if (mtx4.v[2].z > 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        }
        v.x = mtx4.v[2].x * speed;
        v.y = mtx4.v[2].y * speed;
        v.z = mtx4.v[2].z * speed;
        a.x = mtx4.v[2].x * spin;
        a.y = mtx4.v[2].y * spin;
        a.z = mtx4.v[2].z * spin;
        offset.x += mtx4.v[2].x;
        offset.y += mtx4.v[2].y;
        offset.z += mtx4.v[2].z;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx4, quat);
        }
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        break;
    }
    }
    if (emit[0x20]) {
        vel->x += seq->vel.x * *(f32 *)(emit + 0x24);
        vel->y += seq->vel.y * *(f32 *)(emit + 0x24);
        vel->z += seq->vel.z * *(f32 *)(emit + 0x24);
    }
    pos->w = 1.0f;
}

void EventSeq_Advanced_GetInitData_NonDirectional(u8 *emit, void *arg1, Vector4 *vel, Vector4 *pos, Vector4 *quat, Vector4 *angVel) {
    typedef struct EventSeqThing {
        Vector4 vel;
        Matrix4 mtx;
        Vector4 quat;
    } EventSeqThing;
    EventSeqThing *seq = arg1;
    Matrix4 mtx1;
    Matrix4 res;
    Matrix4 rot;
    Matrix4 mtx3;
    Matrix4 mtx4;
    Vector4 tmp __attribute__((aligned(16)));
    Vector4 v __attribute__((aligned(16)));
    Vector4 a __attribute__((aligned(16)));
    Vector4 offset __attribute__((aligned(16)));
    Vector4 dir1 __attribute__((aligned(16)));
    Vector4 dir3 __attribute__((aligned(16)));
    Vector4 dir4 __attribute__((aligned(16)));

    EventSeq_Advanced_GetOffset(emit, &seq->mtx, &offset);
    switch (emit[0x22]) {
    case 0:
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        if (quat != NULL) {
            *quat = seq->quat;
        }
        break;
    case 1: {
        f32 speed;
        f32 spin;

        mtx1 = seq->mtx;
        speed = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        spin = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        RandomNumberGenerator_GetRandomVector(&dir1);
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        v.x = dir1.x * speed;
        v.y = dir1.y * speed;
        v.z = dir1.z * speed;
        a.x = dir1.x * spin;
        a.y = dir1.y * spin;
        a.z = dir1.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx1, quat);
        }
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        break;
    }
    case 2: {
        f32 *range = SLOT_LOAD(f32 *, (emit + 0x2C));
        f32 ry;
        f32 rx;
        f32 rz;

        ry = RandomDirVar(range[2], range[3]);
        rx = -RandomDirVar(range[0], range[1]);
        rz = RandomDirVar(range[4], range[5]);
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        SetOrientation(&rot, 0.01745329238474369f * ry, 0.01745329238474369f * rx, 0.01745329238474369f * rz);
        rot.v[3].x = rot.v[3].y = rot.v[3].z = 0.0f;
        MultiplyMatrix(&rot, &seq->mtx, &res);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        if (quat != NULL) {
            SubMatrixToQuaternion(&res, quat);
        }
        XformVecOrientation(&res, &v, vel);
        XformVecOrientation(&res, &a, angVel);
        break;
    }
    case 3: {
        f32 speed;
        f32 spin;

        mtx3 = seq->mtx;
        speed = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        spin = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        dir3 = offset;
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        if (!dir3.x && !dir3.y && !dir3.z) {
            dir3.x = 0.0f;
            dir3.y = 0.0f;
            dir3.z = 1.0f;
        } else if (PSVECSquareMag(&dir3)) {
            PSVECNormalize(&dir3, &dir3);
        }
        v.x = dir3.x * speed;
        v.y = dir3.y * speed;
        v.z = dir3.z * speed;
        a.x = dir3.x * spin;
        a.y = dir3.y * spin;
        a.z = dir3.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx3, quat);
        }
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        break;
    }
    case 4: {
        f32 speed;
        f32 spin;

        mtx4 = seq->mtx;
        speed = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC));
        spin = 0.001f * RandomAdd(*(f32 *)(emit + 0x10), *(f32 *)(emit + 0x14));
        RandomNumberGenerator_GetRandomVector(&dir4);
        switch ((SLOT_LOAD(u8 *, (emit + 0x2C)))[0x10]) {
        case 4:
            if (dir4.x < 0.0f) {
                dir4.x *= -1.0f;
            }
            break;
        case 5:
            if (dir4.x > 0.0f) {
                dir4.x *= -1.0f;
            }
            break;
        case 2:
            if (dir4.y < 0.0f) {
                dir4.y *= -1.0f;
            }
            break;
        case 3:
            if (dir4.y > 0.0f) {
                dir4.y *= -1.0f;
            }
            break;
        case 0:
            if (dir4.z < 0.0f) {
                dir4.z *= -1.0f;
            }
            break;
        case 1:
            if (dir4.z > 0.0f) {
                dir4.z *= -1.0f;
            }
            break;
        }
        XformVecOrientation(&seq->mtx, &offset, &tmp);
        PSVECAdd(&seq->mtx.v[3], &tmp, pos);
        v.x = dir4.x * speed;
        v.y = dir4.y * speed;
        v.z = dir4.z * speed;
        a.x = dir4.x * spin;
        a.y = dir4.y * spin;
        a.z = dir4.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx4, quat);
        }
        XformVecOrientation(&seq->mtx, &v, vel);
        XformVecOrientation(&seq->mtx, &a, angVel);
        break;
    }
    }
    if (emit[0x20]) {
        vel->x += seq->vel.x * *(f32 *)(emit + 0x24);
        vel->y += seq->vel.y * *(f32 *)(emit + 0x24);
        vel->z += seq->vel.z * *(f32 *)(emit + 0x24);
    }
    pos->w = 1.0f;
}
s32 EventSeq_Move(u8 *seq) {
    extern f32 g_fDeltaTimeMS;
    extern Vector4 g_zeroVector;
    extern Matrix4 g_identity;
    typedef struct { u32 type; PTR32(void) a; PTR32(void) b; } Entry;
    typedef struct { u32 count; PTR32(Entry) entries; } Script;
    typedef struct { Vector4 pos; Vector4 vel; f32 unk20; Vector4 quat; PTR32(u8) p; u32 id; } Params0;
    typedef struct { Vector4 pos; Vector4 quat; Vector4 vel; Vector4 angVel; PTR32(u8) p; u32 id; f32 unk48; f32 unk4C; } Params1;
    typedef struct { Vector4 pos; Vector4 quat; Vector4 vel; PTR32(u8) p; u32 id; f32 unk38; } Params2;
    typedef struct { Vector4 pos; Vector4 vel; Vector4 row; Vector4 quat; PTR32(u8) p; u32 unk44; f32 unk48; u32 unk4C; u32 unk50; u32 unk54; u32 unk58; u32 unk5C; } Params3;
    typedef struct { Vector4 quat; Vector4 pos; Vector4 vel; PTR32(u8) p; f32 unk34; } Params4;
    typedef struct { Vector4 pos; Vector4 quat; Vector4 axis; Vector4 vel; f32 unk40; u32 id; s32 unk48; f32 unk4C; u32 unk50; } Params6;
    typedef struct { Vector4 dir; f32 unk10; f32 unk14; f32 unk18; u32 id; PTR32(u8) slave; u8 pad24[0xC]; } Spawn;
    typedef struct { s32 base; s32 range; u32 count; PTR32(Spawn) spawns; } SpawnList;
    f32 scale;
    u32 dt;
    u32 sample;  /* sound effect id (pack sound relocation); SoundSource_Set copies 4 bytes */
    f32 pitch;
    Entry *entry;
    Script *script;

    scale = 1.0f;
    if (*(f32 *)(seq + 0xF8) != 0.0f) {
        scale = *(f32 *)(seq + 0xF8) * MagnitudeVector(seq) + 1.0f;
        dt = g_fDeltaTimeMS * scale;
    } else {
        dt = g_fDeltaTimeMS;
    }
    if (SLOT_LOAD(Script *, (seq + 0xD0)) == NULL) {
        return 0;
    }
    seq[0xF1] = 1;
    while (1) {
        if (*(u32 *)(seq + 0xE0) != 0) {
            if (dt > *(u32 *)(seq + 0xE0)) {
                dt -= *(u32 *)(seq + 0xE0);
                *(u32 *)(seq + 0xE0) = 0;
            } else {
                *(u32 *)(seq + 0xE0) -= dt;
                return 1;
            }
        }
        script = SLOT_LOAD(Script *, (seq + 0xD0));
        if (*(u32 *)(seq + 0xD4) >= script->count) {
            Thing_Release(seq);
            break;
        }
        entry = &GETP(script->entries)[*(u32 *)(seq + 0xD4)];
        switch (entry->type) {
        case 0:
            break;
        case 1: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            if (!ParticleManager_CanCreatureParticle(p)) break;
            {
                Vector4 ang;
                Vector4 vel = {0.0f, 0.0f, 0.0f, 0.0f};
                Vector4 pos;
                Vector4 quat = {0.0f, 0.0f, 0.0f, 1.0f};
                u8 *emit = GETP(GETP((SLOT_LOAD(Script *, (seq + 0xD0)))->entries)[*(u32 *)(seq + 0xD4)].b);
                u32 n = RandomAdd_s32(((s32 *)emit)[0], ((s32 *)emit)[1]);
                u32 i;

                if (!EventSeq_InCameraRange(seq, *(f32 *)(p + 0x10)) && GameSettings_GetNumOfPlayers() != 1) break;
                QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
                if (n == 0) n = 1;
                for (i = 0; i < n; i++) {
                    if (p[0] == 0) {
                        EventSeq_Advanced_GetInitData(emit, seq, &vel, &pos, &quat, &ang);
                    } else {
                        EventSeq_Advanced_GetInitData_NonDirectional(emit, seq, &vel, &pos, &quat, &ang);
                    }
                    vel.x *= 0.001f;
                    vel.y *= 0.001f;
                    vel.z *= 0.001f;
                    ang.x *= 0.001f;
                    ang.y *= 0.001f;
                    ang.z *= 0.001f;
                    ang.y += 9.6667e-7f * RandomAdd(*(f32 *)(emit + 0x18), *(f32 *)(emit + 0x1C));
                    CreateParticle(&pos, &vel, &ang, &quat, p, scale, 0.0f);
                }
            }
            break;
        }
        case 2: {
            u8 *p = GETP(entry->a);
            u8 *emit;
            u32 n, i;
            Params0 prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(((s32 *)emit)[0], ((s32 *)emit)[1]);
            SETP(prm.p, p);
            prm.id = *(u32 *)(seq + 0x94);
            QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, &prm.quat, &ang);
                prm.unk20 = RandomAdd(*(f32 *)(emit + 0x18), *(f32 *)(emit + 0x1C));
                ThingManager_DARYL_CreateThing(0, &prm);
            }
            break;
        }
        case 3: {
            u8 *p = GETP(entry->a);
            u8 *emit;
            u32 n, i;
            Params1 prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(((s32 *)emit)[0], ((s32 *)emit)[1]);
            QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
            prm.id = *(u32 *)(seq + 0x94);
            prm.unk48 = 0.0f;
            prm.unk4C = 0.0f;
            SETP(prm.p, p);
            prm.angVel = g_zeroVector;
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, &prm.quat, &ang);
                ThingManager_DARYL_CreateThing(1, &prm);
            }
            break;
        }
        case 4: {
            u8 *p = GETP(entry->a);
            u8 *emit;
            u32 n, i;
            Params2 prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(((s32 *)emit)[0], ((s32 *)emit)[1]);
            QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                SETP(prm.p, p);
                prm.unk38 = 0.0f;
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, &prm.quat, &ang);
                prm.id = *(u32 *)(seq + 0x94);
                ThingManager_DARYL_CreateThing(2, &prm);
            }
            break;
        }
        case 6: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            {
                Vector4 pos;
                Vector4 tmp;
                Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                u8 *emit = GETP(GETP((SLOT_LOAD(Script *, (seq + 0xD0)))->entries)[*(u32 *)(seq + 0xD4)].b);
                Matrix4 m;
                Params3 prm __attribute__((aligned(16)));

                if (emit[0x18] == 1) {
                    m = g_identity;
                    *(f32 *)(seq + 0x50) = 0.0f;
                    *(f32 *)(seq + 0x54) = 0.0f;
                    *(f32 *)(seq + 0x58) = 0.0f;
                    *(f32 *)(seq + 0x5C) = 1.0f;
                } else {
                    QuaternionToSubMatrix(seq + 0x50, &m);
                }
                pos = *(Vector4 *)(seq + 0x40);
                if (emit[0x19]) {
                    f32 *r = SLOT_LOAD(f32 *, (emit + 0x1C));
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                SETP(prm.p, p);
                prm.unk4C = 0;
                prm.unk50 = 0;
                prm.pos = pos;
                prm.quat = *(Vector4 *)(seq + 0x50);
                prm.vel = prm.row = g_zeroVector;
                prm.unk58 = 0;
                prm.unk5C = 0;
                if (p[6]) {
                    prm.unk48 = *(f32 *)(p + 8);
                } else {
                    prm.unk48 = 0.0f;
                }
                ThingManager_DARYL_CreateThing(3, &prm);
            }
            break;
        }
        case 7: {
            u8 *p = GETP(entry->a);
            u8 *emit;
            u32 n, i;
            Params3 prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(((s32 *)emit)[0], ((s32 *)emit)[1]);
            QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
            prm.unk4C = 0;
            prm.unk50 = 2;
            prm.row = *(Vector4 *)(seq + 0xC0);
            prm.unk5C = 0;
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                SETP(prm.p, p);
                prm.unk48 = 0.0f;
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, &prm.quat, &ang);
                ThingManager_DARYL_CreateThing(10, &prm);
            }
            break;
        }
        case 29: {
            SpawnList *p = GETP(entry->a);

            if (p == NULL) break;
            {
                u8 *emit = GETP(entry->b);
                u32 n = RandomAdd_s32(((s32 *)emit)[0], ((s32 *)emit)[1]);
                Vector4 base = {0.0f, 0.0f, 0.0f, 0.0f};
                Vector4 ang;
                Vector4 rnd;
                Params6 prm __attribute__((aligned(16)));
                Params2 sp __attribute__((aligned(16)));
                u32 i, j;

                QuaternionToSubMatrix(seq + 0x50, seq + 0x10);
                if (n == 0) n = 1;
                for (i = 0; i < n; i++) {
                    EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &base, &prm.quat, &ang);
                    for (j = 0; j < p->count; j++) {
                        f32 mag = PSVECSquareMag(&GETP(p->spawns)[j].dir);
                        f32 s;
                        u8 *thing;

                        if (mag <= 0.0001f) {
                            prm.vel.x = 0.0f;
                            prm.vel.y = mag = 1.0f;
                            prm.vel.z = 0.0f;
                        } else {
                            XformVecOrientation(seq + 0x10, &GETP(p->spawns)[j].dir, &prm.vel);
                        }
                        s = RandomAdd(*(f32 *)(emit + 0x8), *(f32 *)(emit + 0xC)) / mag;
                        prm.vel.x *= s;
                        prm.vel.y *= s;
                        prm.vel.z *= s;
                        RandomNumberGenerator_GetRandomVector(&rnd);
                        if ((prm.id = GETP(p->spawns)[j].id) == 0) continue;
                        prm.unk40 = RandomAdd(*(f32 *)(emit + 0x18), *(f32 *)(emit + 0x1C));
                        XformVecOrientation(seq + 0x10, &GETP(p->spawns)[j].dir, &prm.pos);
                        prm.quat = *(Vector4 *)(seq + 0x50);
                        prm.pos.x += base.x;
                        prm.pos.y += base.y;
                        prm.pos.z += base.z;
                        prm.vel.x += rnd.x * *(f32 *)(emit + 0x8);
                        prm.vel.y += rnd.y * *(f32 *)(emit + 0x8);
                        prm.vel.z += rnd.z * *(f32 *)(emit + 0x8);
                        if (emit[0x20]) {
                            prm.vel.x += *(f32 *)(seq + 0x0) * *(f32 *)(emit + 0x24);
                            prm.vel.y += *(f32 *)(seq + 0x4) * *(f32 *)(emit + 0x24);
                            prm.vel.z += *(f32 *)(seq + 0x8) * *(f32 *)(emit + 0x24);
                        }
                        prm.unk48 = RandomAdd_s32(p->base, p->range);
                        prm.unk4C = GETP(p->spawns)[j].unk10;
                        if (prm.unk48 < (s32)(0.5f * p->base)) {
                            prm.unk50 = 0;
                        } else {
                            prm.unk50 = 1;
                        }
                        RandomNumberGenerator_GetRandomVector(&prm.axis);
                        prm.axis.w = 0.017453292f * RandomAdd(GETP(p->spawns)[j].unk14, GETP(p->spawns)[j].unk18);
                        thing = ThingManager_DARYL_CreateThing(6, &prm);
                        if (thing != NULL && GETP(GETP(p->spawns)[j].slave) != NULL) {
                            u8 *slave;

                            sp.unk38 = 0.0f;
                            sp.quat.w = 1.0f;
                            sp.quat.x = 0.0f;
                            sp.quat.y = 0.0f;
                            sp.quat.z = 0.0f;
                            sp.pos = g_zeroVector;
                            SETP(sp.p, GETP(GETP(p->spawns)[j].slave));
                            sp.id = *(u32 *)(seq + 0x94);
                            sp.vel = g_zeroVector;
                            slave = ThingManager_DARYL_CreateSlave(2, &sp, 0xFF);
                            if (slave != NULL) {
                                slave[0x83] = 0xFF;
                                ThingManagerList_Append(thing + 0x88, slave);
                            }
                        }
                    }
                }
            }
            break;
        }
        case 25: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            {
                u8 *emit = GETP(entry->b);
                Vector4 pos = *(Vector4 *)(seq + 0x40);
                Params4 prm __attribute__((aligned(16)));

                if (emit[0x19]) {
                    f32 *r = SLOT_LOAD(f32 *, (emit + 0x1C));
                    pos.x += RandomAdd(r[0], r[1]);
                    pos.y += RandomAdd(r[2], r[3]);
                    pos.z += RandomAdd(r[4], r[5]);
                }
                SETP(prm.p, p);
                prm.pos = pos;
                prm.unk34 = 0.0f;
                prm.quat.w = 1.0f;
                prm.quat.x = 0.0f;
                prm.quat.y = 0.0f;
                prm.quat.z = 0.0f;
                prm.vel.x = 0.0f;
                prm.vel.y = 0.0f;
                prm.vel.z = 0.0f;
                ThingManager_DARYL_CreateThing(4, &prm);
            }
            break;
        }
        case 8: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            {
                Vector4 pos;
                Vector4 tmp;
                Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                u8 *emit = GETP(GETP((SLOT_LOAD(Script *, (seq + 0xD0)))->entries)[*(u32 *)(seq + 0xD4)].b);
                Matrix4 m;

                if (emit[0x18] == 1) {
                    m = g_identity;
                }
                QuaternionToSubMatrix(seq + 0x50, &m);
                pos = *(Vector4 *)(seq + 0x40);
                if (emit[0x19]) {
                    f32 *r = SLOT_LOAD(f32 *, (emit + 0x1C));
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                LightHolder_Add(p, &pos, NULL);
            }
            break;
        }
        case 10: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            if (p[0] == 1) {
                f32 pitch = RandomAdd(*(f32 *)(p + 0xC), *(f32 *)(p + 0x10));
                void *voice = SoundAudioSample_Play(*(u32 *)(p + 0x14));

                SoundAudioSample_Set(voice, 0, 32767.0f * *(f32 *)(p + 0x8));
                SoundAudioSample_Set(voice, 1, 65536.0f * pitch);
            } else {
                u8 *emit = GETP(entry->b);
                s32 src = SoundManager_CreateSource(0);

                sample = *(u32 *)(p + 0x14);
                pitch = RandomAdd(*(f32 *)(p + 0xC), *(f32 *)(p + 0x10));
                {
                    Vector4 tmp;
                    Vector4 pos;
                    Vector4 vel;
                    Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                    Matrix4 m;

                    vel = *(Vector4 *)(seq + 0x0);
                    if (emit[0x18] == 1) {
                        m = g_identity;
                        vel.x = vel.y = vel.z = 0.0f;
                    }
                    QuaternionToSubMatrix(seq + 0x50, &m);
                    pos = *(Vector4 *)(seq + 0x40);
                    if (emit[0x19]) {
                        f32 *r = SLOT_LOAD(f32 *, (emit + 0x1C));
                        off.x = RandomAdd(r[0], r[1]);
                        off.y = RandomAdd(r[2], r[3]);
                        off.z = RandomAdd(r[4], r[5]);
                    }
                    XformVecOrientation(&m, &off, &tmp);
                    PSVECAdd(&pos, &tmp, &pos);
                    SoundSource_Set(src, 8, &sample);
                    SoundSource_Set(src, 5, &pos);
                    SoundSource_Set(src, 6, &vel);
                    SoundSource_Set(src, 0, p + 0x8);
                    SoundSource_Set(src, 1, &pitch);
                    if (p[0] == 0) {
                        SoundSource_Set(src, 2, SLOT_LOAD(u8 *, (p + 0x18)));
                        SoundSource_Set(src, 3, SLOT_LOAD(u8 *, (p + 0x18)) + 4);
                    }
                    SoundSource_Play(src);
                }
            }
            break;
        }
        case 5: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            {
                u8 *emit = GETP(entry->b);
                Vector4 tmp;
                Vector4 pos;
                Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                Matrix4 m;

                if (emit[0x18] == 1) {
                    m = g_identity;
                }
                QuaternionToSubMatrix(seq + 0x50, &m);
                pos = *(Vector4 *)(seq + 0x40);
                if (emit[0x19]) {
                    f32 *r = SLOT_LOAD(f32 *, (emit + 0x1C));
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                if (p[0]) {
                    Blast_Action(p, &pos, seq);
                }
                if (!GameSettings_GetCodeVariable(0x198, 0) && (p[2] || p[3] || p[4])) {
                    ForceFeedback_Play(p, &pos);
                }
                if (p[1]) {
                    f32 *range = SLOT_LOAD(f32 *, (p + 0x30));
                    f32 lo = range[0];
                    f32 hi = range[1];
                    f32 loSq = lo * lo;
                    f32 hiSq = hi * hi;
                    f32 span = hi - lo;
                    s8 skip = 0;
                    u8 *t;

                    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = SLOT_LOAD(u8 *, (t + 0xA4))) {
                        if (*(u32 *)(t + 0x94) == *(u32 *)(seq + 0x94)) {
                            if (p[5] == 0) skip = 1;
                        } else {
                            if (p[5] == 2) skip = 1;
                        }
                        if (!skip && !(*(u32 *)(t + 0x74) & 0x200000) && *(s16 *)(t + 0x80) != 0) {
                            f32 d = PSVECSquareDistance((f32 *)&pos, (f32 *)(t + 0x40));

                            if (d < hiSq) {
                                f32 f = 1.0f;

                                if (d > loSq) {
                                    f = (hi - Sqrt(d)) / span;
                                }
                                FlashHolder_Add(p, &pos, HND(t), f);
                            }
                        }
                    }
                }
            }
            break;
        }
        case 26: {
            u8 *p = GETP(entry->a);
            u8 *player;

            if (p == NULL) break;
            player = SLOT_LOAD(u8 *, (seq + 0x94));
            if (player != NULL && !(*(u32 *)(player + 0x6C) & 0x40) && *(s8 *)(p + 4) == 0) break;
            {
                u8 *emit = GETP(entry->b);
                Vector4 tmp;
                Vector4 pos;
                Vector4 off __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
                Matrix4 m;

                if (emit[0x18] == 1) {
                    m = g_identity;
                }
                QuaternionToSubMatrix(seq + 0x50, &m);
                pos = *(Vector4 *)(seq + 0x40);
                if (emit[0x19]) {
                    f32 *r = SLOT_LOAD(f32 *, (emit + 0x1C));
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                SoundSequence_Play(p, &pos);
            }
            break;
        }
        case 11:
            *(u32 *)(seq + 0xD8) = *(u32 *)(seq + 0xD4);
            *(u32 *)(seq + 0xDC) = GETP((SLOT_LOAD(Script *, (seq + 0xD0)))->entries)[*(u32 *)(seq + 0xD4)].a.h  /* port: a value (repeat count), not a pointer */;
            break;
        case 12:
            if (*(u32 *)(seq + 0xDC) != 0) {
                *(u32 *)(seq + 0xDC) -= 1;
                *(u32 *)(seq + 0xD4) = *(u32 *)(seq + 0xD8);
            }
            break;
        case 15:
            *(u32 *)(seq + 0xE0) = entry->a.h;  /* port: a value, not a pointer */
            break;
        case 13: {
            u32 i;

            *(u32 *)(seq + 0xE8) = i = *(u32 *)(seq + 0xD4) + 1;
            *(u32 *)(seq + 0xE4) = GETP((SLOT_LOAD(Script *, (seq + 0xD0)))->entries)[*(u32 *)(seq + 0xD4)].a.h  /* port: a value (repeat count), not a pointer */;
            *(u32 *)(seq + 0xEC) = 0;
            for (; i < (SLOT_LOAD(Script *, (seq + 0xD0)))->count; i++) {
                if (GETP((SLOT_LOAD(Script *, (seq + 0xD0)))->entries)[i].type == 14) break;
                *(u32 *)(seq + 0xEC) += 1;
            }
            seq[0xF0] = 1;
            break;
        }
        case 14:
            if (*(u32 *)(seq + 0xE4) > 1) {
                *(u32 *)(seq + 0xE4) -= 1;
                seq[0xF0] = 1;
            }
            break;
        case 30:
            break;
        default:
            return 0;
        }
        switch (seq[0xF0]) {
        case 0:
            *(u32 *)(seq + 0xD4) += 1;
            break;
        case 1:
            *(u32 *)(seq + 0xD4) = RandomAdd_s32(*(s32 *)(seq + 0xE8), *(s32 *)(seq + 0xEC));
            seq[0xF0] = 2;
            break;
        case 2:
            *(u32 *)(seq + 0xD4) = *(u32 *)(seq + 0xE8) + *(u32 *)(seq + 0xEC);
            seq[0xF0] = 0;
            break;
        }
    }
    return 1;
}

u32 EventSeq_InCameraRange(u8 *seq, f32 range)
{
    Vector4 positions[5] __attribute__((aligned(16)));
    Vector4 *pos;
    f32 dist;
    u32 count;
    u32 i;

    if (range == 0.0f)
    {
        return 1;
    }
    if (*(s8 *)(seq + 0xF1))
    {
        count = Gfx_GetCameraPositions(positions);
        *(f32 *)(seq + 0xF4) = PSVECSquareDistance((f32 *)(seq + 0x40), (f32 *)&positions[0]);
        if (count > 1)
        {
            for (i = 0, pos = positions; i < 5; i++, pos++)
            {
                if (g_CurrentViews & (1 << i))
                {
                    dist = PSVECSquareDistance((f32 *)(seq + 0x40), (f32 *)pos);
                    if (dist < *(f32 *)(seq + 0xF4))
                    {
                        *(f32 *)(seq + 0xF4) = dist;
                    }
                }
            }
        }
        *(s8 *)(seq + 0xF1) = 0;
    }
    return range * range > *(f32 *)(seq + 0xF4);
}

s32 EventSeq_Invalidate(void *arg0, u32 arg1, u32 arg2)
{
    u32 field;

    field = M2C_FIELD(arg0, u32 *, 0xD0);
    if (field >= arg1 && field < arg2)
    {
        return 1;
    }
    return 0;
}
