

#include "common.h"

extern u8 g_CurrentViews;

s32 Gfx_GetCameraPositions(Vector4 *positions);


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
        if ((*(f32 (**)(u8 *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x14))(thing) > 0.01f) {
            force /= (*(f32 (**)(u8 *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x14))(thing);
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
        force *= (*(f32 (**)(u8 *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x14))(thing);
        (*(void (**)(u8 *, void *, s32, f32))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x18))(thing, &impact, damageType, force);
    }
}
s32 EventSeq_Init(u8 *seq, u8 *data)
{

    Thing_Init(seq, data);
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
    Thing_Release(HND(arg0));
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
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
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
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
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
s32 EventSeq_Move(void *thing) {
    return 0;
}

s32 EventSeq_InCameraRange(u8 *seq, f32 range)
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
