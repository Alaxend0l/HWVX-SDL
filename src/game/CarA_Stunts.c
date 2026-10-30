#include "common.h"

typedef struct DroneProps {
    u8 pad00[0x20];
    f32 mass;               /* 0x20 */
    f32 gravity;            /* 0x24 */
    u8 pad28[4];            /* 0x28 */
    f32 spinDamping;        /* 0x2C */
} DroneProps;

typedef struct DroneInfo {
    u8 pad00[0x10];
    PTR32(DroneProps) props;      /* 0x10 */
} DroneInfo;

typedef struct DroneAnim {
    u8 pad00[8];
    f32 start;              /* 0x08 */
    f32 end;                /* 0x0C */
} DroneAnim;

/* The flying drone Thing; its animation drives it in state 2. */
typedef struct Drone {
    Vector4 velocity;       /* 0x00 */
    Matrix4 mtx;            /* 0x10 v[3] = position */
    u8 pad50[0x24];         /* 0x50 */
    u32 flags;              /* 0x74 */
    u8 pad78[0x1C];         /* 0x78 */
    PTR32(s8) input;              /* 0x94 */
    u8 pad98[0x28];         /* 0x98 */
    PTR32(DroneInfo) info;        /* 0xC0 */
    u8 padC4[0x10];         /* 0xC4 */
    PTR32(DroneAnim) anim;        /* 0xD4 */
    u8 padD8[8];            /* 0xD8 */
    Quaternion rot;         /* 0xE0 */
    Vector4 force;          /* 0xF0 */
    Vector4 baseForce;      /* 0x100 */
    Vector4 angMomentum;    /* 0x110 */
    Vector4 invInertia;     /* 0x120 */
    Vector4 angVel;         /* 0x130 */
    f32 suspension[4];      /* 0x140 */
    u32 numContacts;        /* 0x150 */
    u8 pad154[0x2C];        /* 0x154 */
    f32 speed;              /* 0x180 */
    u8 pad184[0xC];         /* 0x184 */
    Vector4 wheels[4];      /* 0x190 */
    u8 pad1D0[0xC];         /* 0x1D0 */
    u8 state;               /* 0x1DC */
    u8 pad1DD[7];           /* 0x1DD */
    f32 animTime;           /* 0x1E4 */
} Drone;

extern Vector4 g_zeroVector;
extern f32 g_fDeltaTimeSec;

typedef struct DroneKeyFrame {
    Quaternion rot;
    Vector4 pos;
} DroneKeyFrame;

static inline void Mover_SetRotation(Drone *mover, Quaternion quat) {
    mover->rot = quat;
}


void Drone_GetCameraOrientationInfo(void *drone, void *camera, void *info) {
    M2C_FIELD(info, s32 *, 0x88) = 1;
    SLOT_STORE((u8 *)(info) + 0x84, (u8 *)drone + 0x154);
    M2C_FIELD(info, f32 *, 0x8C) = 1.0f;
    M2C_FIELD(info, f32 *, 0x30) = 0.0f;
    M2C_FIELD(info, f32 *, 0x34) = 1.0f;
    M2C_FIELD(info, f32 *, 0x38) = 0.0f;
}

s32 Drone_Init(u8 *drone, u8 *data) {
    typedef struct ABox {
        Vector4 v[3];
    } __attribute__((aligned(16))) ABox;
    typedef struct DroneInfoEx {
        f32 inertia[3];
        u8 pad0C[0x10 - 0xC];
        u8 type;
        u8 pad11[0x14 - 0x11];
        u32 unk14;
        u8 pad18[0x20 - 0x18];
        f32 mass;
        u8 pad24[0x44 - 0x24];
        PTR32(ABox) box;
    } DroneInfoEx;
    ABox box;
    DroneInfoEx *info;

    Thing_Init(drone, (s32)HND(data));
    SLOT_STORE((drone + 0xD4), LoadDataChunkFile("NPP.ANI"));
    *(f32 *)(drone + 0x1E4) = *(f32 *)(SLOT_LOAD(u8 *, (drone + 0xD4)) + 0x8);
    *(s32 *)(drone + 0x94) = 0;
    *(Vector4 *)(drone + 0x40) = *(Vector4 *)data;
    QuaternionToSubMatrix(data + 0x30, drone + 0x10);
    *(Vector4 *)(drone + 0x50) = *(Vector4 *)(data + 0x30);
    *(s32 *)(drone + 0x15C) = 0;
    drone[0x154] = 0;
    SLOT_STORE((drone + 0x158), drone + 0x160);
    *(f32 *)(drone + 0x164) = 20.0f;
    *(f32 *)(drone + 0x170) = 20.0f;
    *(f32 *)(drone + 0x160) = 75.0f;
    *(f32 *)(drone + 0x16C) = 40.0f;
    *(f32 *)(drone + 0x168) = 5.0f;
    SLOT_STORE((drone + 0xC0), SLOT_LOAD(void *, (data + 0x40)));
    info = SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10));
    switch (info->type) {
    case 0:
        *(f32 *)(drone + 0x70) = GETP(info->box)->v[2].x;
        *(u16 *)(drone + 0x80) = 1;
        break;
    case 1:
        break;
    }
    *(u32 *)(drone + 0x78) = (SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)))->unk14;
    *(f32 *)(drone + 0x180) = 0.0f;
    *(Vector4 *)(drone + 0x100) = g_zeroVector;
    *(Vector4 *)(drone + 0xF0) = *(Vector4 *)(drone + 0x100);
    *(s32 *)(drone + 0x184) = 0;
    *(s32 *)(drone + 0x17C) = 0;
    info = SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10));
    switch (info->type) {
    case 0:
        box = *GETP(info->box);
        break;
    case 1:
        break;
    }
    *(f32 *)(drone + 0x120) = 1.0f / (info->inertia[0] * info->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
    *(f32 *)(drone + 0x124) = 1.0f / ((SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)))->inertia[1] * (SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)))->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
    *(f32 *)(drone + 0x128) = 1.0f / ((SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)))->inertia[2] * (SLOT_LOAD(DroneInfoEx *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)))->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
    Thing_AddToGrid(drone, 0);
    *(Vector4 *)(drone + 0x110) = g_zeroVector;
    *(Vector4 *)(drone + 0x130) = g_zeroVector;
    *(u32 *)(drone + 0x74) |= 4;
    *(u32 *)(drone + 0x74) |= 0x80;
    *(f32 *)(drone + 0x190) = -3.39446f;
    *(f32 *)(drone + 0x194) = -0.48040998f;
    *(f32 *)(drone + 0x198) = -4.5236802f;
    *(f32 *)(drone + 0x1A0) = 3.39446f;
    *(f32 *)(drone + 0x1A4) = -0.48040998f;
    *(f32 *)(drone + 0x1A8) = -4.5236802f;
    *(f32 *)(drone + 0x1B0) = -3.39446f;
    *(f32 *)(drone + 0x1B4) = -0.48040998f;
    *(f32 *)(drone + 0x1B8) = 5.6166801f;
    *(f32 *)(drone + 0x1C0) = 3.39446f;
    *(f32 *)(drone + 0x1C4) = -0.48040998f;
    *(f32 *)(drone + 0x1C8) = 5.6166801f;
    drone[0x1DC] = 0;
    return 1;
}


void Drone_ComputeSuspensionForces(Drone *obj) {
    typedef struct {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[8];
        PTR32(u8) material;
        u8 pad2C[4];
        Vector4 unk30;
        u8 pad40[0x10];
    } WheelHit;
    extern u32 g_iLastDeltaTimeMS;
    Drone *drone = obj;
    WheelHit hit __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 point __attribute__((aligned(16)));
    Vector4 start __attribute__((aligned(16)));
    Vector4 slip __attribute__((aligned(16)));
    Vector4 vel __attribute__((aligned(16)));
    Vector4 spin __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 friction __attribute__((aligned(16)));
    Vector4 side __attribute__((aligned(16)));
    f32 t;
    u32 i;
    s8 hitTerrain;
    f32 compression;
    f32 len;
    f32 spring;
    f32 damper;
    f32 mag;
    f32 grip;
    f32 scale;
    f32 gravity;
    f32 turn;
    f32 thrust;
    s8 *input;

    drone->numContacts = 0;
    for (i = 0; i < 4; i++) {
        dir.x = -drone->mtx.v[1].x;
        dir.y = -drone->mtx.v[1].y;
        dir.z = -drone->mtx.v[1].z;
        hit.unk30 = g_zeroVector;
        XformVec(&drone->mtx, &drone->wheels[i], &start);
        start.x += 2.0f * drone->mtx.v[1].x;
        start.y += 2.0f * drone->mtx.v[1].y;
        start.z += 2.0f * drone->mtx.v[1].z;
        t = 5.0f;
        hitTerrain = RayHitsTerrain_Aux3(&start, &dir, &t, &hit);
        compression = 4.0f - t;
        if (hitTerrain) {
            drone->numContacts++;
            len = 1.0f + compression;
            spring = 700.0f * len;
            damper = 100.0f * ((compression - drone->suspension[i]) / ((f32)g_iLastDeltaTimeMS / 1000.0f));
            point.x = dir.x * len + start.x;
            point.y = dir.y * len + start.y;
            point.z = dir.z * len + start.z;
            PhysicalObject_ProcessForce(drone, &point, &drone->mtx.v[1], (spring + damper) * PSVECDotProduct(&drone->mtx.v[1], &hit.normal));
            if (drone->state == 0) {
                PSVECSubtract(&point, &drone->mtx.v[3], &r);
                PSVECCrossProduct(&drone->angVel, &r, &spin);
                PSVECAdd(&drone->velocity, &spin, &vel);
                slip.x = vel.x - drone->mtx.v[2].x * drone->speed;
                slip.y = vel.y - drone->mtx.v[2].y * drone->speed;
                slip.z = vel.z - drone->mtx.v[2].z * drone->speed;
                ProjectVectorToNormal(&hit.normal, &slip);
                mag = MagnitudeVector(&slip);
                if (mag > 0.0f) {
                    PSVECDotProduct(&drone->mtx.v[1], &hit.normal);
                    spring = 2.1f * spring;
                    PSVECCrossProduct(&drone->mtx.v[2], &hit.normal, &side);
                    if (PSVECSquareMag(&side)) {
                        PSVECNormalize(&side, &side);
                    }
                    grip = PSVECDotProduct(&side, &drone->mtx.v[1]);
                    grip = 1.1f * (1.0f - 1.8f * grip);
                    if (grip < 0.1f) {
                        grip = 0.1f;
                    } else if (grip > 1.0f) {
                        grip = 1.0f;
                    }
                    scale = grip * (*(f32 *)(GETP(hit.material) + 0x14) * spring) / mag;
                    friction.x = -slip.x * scale;
                    friction.y = -slip.y * scale;
                    friction.z = -slip.z * scale;
                    PhysicalObject_ProcessForce(drone, &point, &friction, 1.0f);
                }
            }
        }
        drone->suspension[i] = compression;
    }
    gravity = -GETP(GETP(drone->info)->props)->spinDamping;
    drone->baseForce.x += gravity * drone->angVel.x;
    drone->baseForce.y += gravity * drone->angVel.y;
    drone->baseForce.z += gravity * drone->angVel.z;
    if (drone->numContacts == 4) {
        if (drone->state == 1) {
            ProjectVectorToNormal(&drone->mtx, &drone->velocity);
            drone->speed = PSVECDotProduct(&drone->velocity, &drone->mtx.v[2]);
            input = GETP(drone->input);
            if (input != NULL) {
                thrust = 12.1f * (f32)input[1];
                turn = 10.0f * (f32)-input[0];
                ProjectVectorToNormal(&drone->mtx.v[1], &drone->angMomentum);
                drone->angMomentum.x += turn * drone->mtx.v[1].x;
                drone->angMomentum.y += turn * drone->mtx.v[1].y;
                drone->angMomentum.z += turn * drone->mtx.v[1].z;
                if (_fabs(drone->speed) < 100.0f) {
                    drone->force.x += drone->mtx.v[2].x * thrust;
                    drone->force.y += drone->mtx.v[2].y * thrust;
                    drone->force.z += drone->mtx.v[2].z * thrust;
                }
            }
        } else if (_fabs(PSVECDotProduct(&drone->mtx, &drone->velocity)) < 2.0f) {
            if (_fabs(PSVECDotProduct(&drone->mtx.v[1], &drone->angVel)) < 5.0f) {
                drone->state = 1;
            }
        }
    } else if (drone->numContacts == 0) {
        drone->state = 0;
    }
}


void Drone_ComputeSuspensionForces(Drone *drone);
s32 Drone_PreMove(Drone *drone) {
    Matrix4 inv;
    Matrix4 rel;
    Matrix4 world;
    Matrix4 rot;
    DroneKeyFrame key;
    Quaternion delta;
    Quaternion quat;
    f32 angle;

    if (drone->state == 2) {
        Gfx_AnimExtractKeyFrame(GETP(drone->anim), drone->animTime, &key, 1);
        drone->velocity.x = (key.pos.x - drone->mtx.v[3].x) / g_fDeltaTimeSec;
        drone->velocity.y = (key.pos.y - drone->mtx.v[3].y) / g_fDeltaTimeSec;
        drone->velocity.z = (key.pos.z - drone->mtx.v[3].z) / g_fDeltaTimeSec;
        quat = key.rot;
        inv = drone->mtx;
        Mover_SetRotation(drone, quat);
        QuaternionToSubMatrix(&quat, &rot);
        TransposeSubMatrix(&inv);
        MultiplySubMatrix(&inv, &rot, &rel);
        MultiplySubMatrix(&drone->mtx, &rel, &world);
        SubMatrixToQuaternion(&rel, (Vector4 *)&delta);
        GenerateSpinAngleFromQuaternion((Vector4 *)&delta, &drone->angVel, &angle);
        angle /= g_fDeltaTimeSec;
        drone->angVel.x *= angle;
        drone->angVel.y *= angle;
        drone->angVel.z *= angle;
        drone->flags &= ~0x10;
        drone->animTime += g_fDeltaTimeSec;
        if (drone->animTime > GETP(drone->anim)->end) {
            drone->animTime = GETP(drone->anim)->start;
        }
    } else {
        drone->baseForce = g_zeroVector;
        drone->force = drone->baseForce;
        drone->force.y += GETP(GETP(drone->info)->props)->gravity * GETP(GETP(drone->info)->props)->mass;
        Drone_ComputeSuspensionForces(drone);
        PhysicalObject_IntegrateNonLateral(drone);
    }
    return 1;
}

void Drone_DynamicCollideImpact(u8 *drone, u8 *collision, f32 force)
{
    Vector4 normal;
    Vector4 pos;
    Vector4 impulse;
    Vector4 torque;
    Vector4 r;
    Vector4 scaled;
    Vector4 local __attribute__((aligned(16)));
    f32 scale;

    normal = *(Vector4 *)(collision + 0x10);
    pos = *(Vector4 *)collision;
    if (drone[0x1DC] != 2)
    {
        scale = force / *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)) + 0x20);
        ((Vector4 *)drone)->x += normal.x * scale;
        ((Vector4 *)drone)->y += normal.y * scale;
        ((Vector4 *)drone)->z += normal.z * scale;
        PSVECSubtract(&pos, (Vector4 *)(drone + 0x40), &r);
        impulse.x = force * normal.x;
        impulse.y = force * normal.y;
        impulse.z = force * normal.z;
        PSVECCrossProduct(&r, &impulse, &torque);
        ((Vector4 *)(drone + 0x110))->x += torque.x;
        ((Vector4 *)(drone + 0x110))->y += torque.y;
        ((Vector4 *)(drone + 0x110))->z += torque.z;
        InverseXformVecOrientation(drone + 0x10, (Vector4 *)(drone + 0x110), &local);
        scaled.x = ((Vector4 *)(drone + 0x120))->x * local.x;
        scaled.y = ((Vector4 *)(drone + 0x120))->y * local.y;
        scaled.z = ((Vector4 *)(drone + 0x120))->z * local.z;
        XformVecOrientation(drone + 0x10, &scaled, (Vector4 *)(drone + 0x130));
        if (*(s32 *)(drone + 0x78) <= 0)
        {
            Thing_ProcessEndEvent(drone, SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (drone + 0xC0)) + 0x10)), collision, (Vector4 *)(drone + 0x130));
            Thing_Release(HND(drone));
        }
        *(f32 *)(drone + 0x180) = 0.0f;
        *(s32 *)(drone + 0x17C) = 0;
    }
}

void Drone_DynamicRotation(u8 *drone, f32 dt) {
    typedef struct Drone {
        u8 pad0[0x10];
        Matrix4 mtx;
        Quaternion quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0xE0 - 0x78];
        Quaternion rot;
        u8 padF0[0x1DC - 0xF0];
        u8 mode;
    } Drone;
    Drone *d = (Drone *)drone;
    Quaternion quat __attribute__((aligned(16)));
    Matrix4 mtx;

    if (d->mode == 2) {
        if (!(d->flags & 0x10)) {
            d->flags |= 0x10;
            quat = d->rot;
            QuaternionToSubMatrix(&quat, &mtx);
            mtx.v[3].x = d->mtx.v[3].x;
            mtx.v[3].y = d->mtx.v[3].y;
            mtx.v[3].z = d->mtx.v[3].z;
            d->quat = quat;
            d->mtx = mtx;
        }
    } else {
        PhysicalObject_DynamicRotation(drone, dt);
    }
}

