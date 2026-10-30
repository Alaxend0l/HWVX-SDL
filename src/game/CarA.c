#include "CarA.h"
#include "common.h"


typedef struct AnimKeyFrame {
    Quaternion rot;
    f32 pos[4];
} AnimKeyFrame;


/* Stacks an additive animation's key frames onto dst. */
static inline void Anim_AddKeyFrames(AnimKeyFrame *dst, AnimKeyFrame *src, u32 count) {
    u32 i;
    Quaternion q;

    for (i = 0; i < count; i++) {
        if (src[i].rot.w != 1.0f || src[i].rot.x != 0.0f || src[i].rot.y != 0.0f || src[i].rot.z != 0.0f) {
            QuaternionMult((Vector4 *)&dst[i].rot, (Vector4 *)&src[i].rot, (Vector4 *)&q);
            dst[i].rot = q;
        }
        dst[i].pos[0] += src[i].pos[0];
        dst[i].pos[1] += src[i].pos[1];
        dst[i].pos[2] += src[i].pos[2];
    }
}

/* Moves the body offset (centre of mass) to (x, y, z), shifting the car's
   position so the body stays where it is in the world. */
static inline void CarA_SetBodyOffset(CarA *car, f32 x, f32 y, f32 z) {
    Vector4 world __attribute__((aligned(16)));
    Vector4 delta __attribute__((aligned(16)));

    delta.x = x - car->bodyOffset.x;
    delta.y = y - car->bodyOffset.y;
    delta.z = z - car->bodyOffset.z;
    XformVecOrientation(&car->worldMatrix, &delta, &world);
    car->worldMatrix.v[3].x -= world.x;
    car->worldMatrix.v[3].y -= world.y;
    car->worldMatrix.v[3].z -= world.z;
    car->bodyOffset.x = x;
    car->bodyOffset.y = y;
    car->bodyOffset.z = z;
    car->collDirtyFlags |= 0x100;
}

/* Angular velocity (CarA.localAxis) from angular momentum and the inverse
   body inertia tensor. */
static inline void CarA_UpdateAngularVelocity(CarA *car) {
    Vector4 local __attribute__((aligned(16)));
    Vector4 scaled __attribute__((aligned(16)));

    InverseXformVecOrientation(&car->worldMatrix, &car->angularMomentum, &local);
    scaled.x = car->inverseBodyInertia.v[0].x * local.x;
    scaled.y = car->inverseBodyInertia.v[1].y * local.y;
    scaled.z = car->inverseBodyInertia.v[2].z * local.z;
    XformVecOrientation(&car->worldMatrix, &scaled, &car->localAxis);
}

static inline Vector4 Vector4_OrZero(Vector4 *v) {
    extern Vector4 g_zeroVector;

    if (v != NULL) {
        return *v;
    }
    return g_zeroVector;
}

/* Releases the damage-state slaves and re-triggers the ones whose health
   threshold the car has dropped below. */
static inline void CarA_UpdateDamageSlaves(CarA *car) {
    typedef struct DamageSlave {
        u8 pad00[0x14];
        f32 threshold;
        u8 pad18[8];
    } DamageSlave;
    typedef struct DamageSlaves {
        u8 pad00[0x1C];
        u32 count;
        PTR32(DamageSlave) slaves;
    } DamageSlaves;
    extern Vector4 g_zeroVector;
    MotionState *ms;
    u32 i;
    DamageSlaves *slaves;
    f32 frac;

    if (GETP(car->vehicleIface)->numSlaves >= 2) {
        ms = &car->motionState;
        slaves = (DamageSlaves *)GETP(GETP(car->vehicleIface)->slaves);
        frac = 100.0f * (f32)car->speed / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth;
        MotionState_ReleaseSlavesByType(HND(ms), 2);
        for (i = 0; i < slaves->count; i++) {
            Vector4 color = {0.0f, 0.0f, 0.0f, 0.0f};

            if (&GETP(slaves->slaves)[i] != NULL && frac < GETP(slaves->slaves)[i].threshold) {
                ThingManagerList_TriggerSlaves(&ms->slaves, &GETP(slaves->slaves)[i], &GETP(slaves->slaves)[i], &color, &g_zeroVector, 2);
            }
        }
    }
}

void CarA_Extract_Keyframes(CarA *car, AnimKeyFrame *frames) {
    u32 *numBones;
    u32 i;
    CarTrailBone *wheel;
    AnimKeyFrame keys[64];

    numBones = GETP(GETP(car->motionState.model)->numBones);
    MotionState_ExtractBlendedKeyFrame(&car->motionState, frames);
    for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
        wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
        SinCosRad(&frames[wheel->bone].rot.x, &frames[wheel->bone].rot.w, 0.5f * car->wheelStates[i].rotation);
        frames[wheel->bone].rot.y = 0.0f;
        frames[wheel->bone].rot.z = 0.0f;
    }
    if (GETP(GETP(car->vehicleIface)->steerAnim) != NULL) {
        Gfx_AnimExtractKeyFrame(GETP(GETP(car->vehicleIface)->steerAnim),
                                10.0f * (car->unk378 / 0.7853982f) * GETP(GETP(car->vehicleIface)->engine)->animSpeed,
                                keys, *numBones);
        Anim_AddKeyFrames(frames, keys, *numBones);
    }
    if (GETP(GETP(GETP(car->vehicleIface)->modelRef)->anims[0]) != NULL) {
        Gfx_AnimExtractKeyFrame(GETP(GETP(GETP(car->vehicleIface)->modelRef)->anims[0]), -car->animTimes[0], keys, *numBones);
        Anim_AddKeyFrames(frames, keys, *numBones);
    }
    if (GETP(GETP(GETP(car->vehicleIface)->modelRef)->anims[1]) != NULL) {
        Gfx_AnimExtractKeyFrame(GETP(GETP(GETP(car->vehicleIface)->modelRef)->anims[1]), -car->animTimes[1], keys, *numBones);
        Anim_AddKeyFrames(frames, keys, *numBones);
    }
    if (GETP(GETP(GETP(car->vehicleIface)->modelRef)->anims[2]) != NULL) {
        Gfx_AnimExtractKeyFrame(GETP(GETP(GETP(car->vehicleIface)->modelRef)->anims[2]), -car->animTimes[2], keys, *numBones);
        Anim_AddKeyFrames(frames, keys, *numBones);
    }
}

void CarA_InitInverseBodyInertiaTensor(CarA *car) {
    extern Matrix4 g_identity;
    Vector4 ext[4] __attribute__((aligned(16)));
    Vector4 bounds[2] __attribute__((aligned(16)));
    CarACollider *coll;
    Vector4 *box;
    CarAHull *hull;
    u32 i;

    car->inverseBodyInertia = g_identity;
    coll = GETP(GETP(car->vehicleIface)->collider);
    switch (coll->type) {
    case 0:
        box = GETP(coll->box);
        PSVECAdd(&box[1], &car->bodyOffset, &ext[2]);
        PSVECAdd(&box[0], &car->bodyOffset, &ext[1]);
        break;
    case 1:
        hull = GETP(coll->hull);
        bounds[0].x = 9999.9f;
        bounds[0].y = 9999.9f;
        bounds[0].z = 9999.9f;
        bounds[1].x = -9999.9f;
        bounds[1].y = -9999.9f;
        bounds[1].z = -9999.9f;
        for (i = 0; i < hull->numVerts; i++) {
            if (bounds[0].x > GETP(hull->verts)[i].x) {
                bounds[0].x = GETP(hull->verts)[i].x;
            }
            if (bounds[0].y > GETP(hull->verts)[i].y) {
                bounds[0].y = GETP(hull->verts)[i].y;
            }
            if (bounds[0].z > GETP(hull->verts)[i].z) {
                bounds[0].z = GETP(hull->verts)[i].z;
            }
            if (bounds[1].x < GETP(hull->verts)[i].x) {
                bounds[1].x = GETP(hull->verts)[i].x;
            }
            if (bounds[1].y < GETP(hull->verts)[i].y) {
                bounds[1].y = GETP(hull->verts)[i].y;
            }
            if (bounds[1].z < GETP(hull->verts)[i].z) {
                bounds[1].z = GETP(hull->verts)[i].z;
            }
        }
        PSVECAdd(&bounds[1], &car->bodyOffset, &ext[2]);
        PSVECAdd(&bounds[0], &car->bodyOffset, &ext[1]);
        break;
    }
    car->inverseBodyInertia.v[0].x = 1.0f / (GETP(GETP(car->vehicleIface)->collider)->inertiaScale[0] * GETP(GETP(car->vehicleIface)->collider)->mass * (ext[2].y * ext[2].y + ext[2].z * ext[2].z));
    car->inverseBodyInertia.v[1].y = 1.0f / (GETP(GETP(car->vehicleIface)->collider)->inertiaScale[1] * GETP(GETP(car->vehicleIface)->collider)->mass * (ext[2].x * ext[2].x + ext[2].z * ext[2].z));
    car->inverseBodyInertia.v[2].z = 1.0f / (GETP(GETP(car->vehicleIface)->collider)->inertiaScale[2] * GETP(GETP(car->vehicleIface)->collider)->mass * (ext[2].x * ext[2].x + ext[2].y * ext[2].y));
}

void CarA_GetAngularVelocity(void *car, Vector4 *vel) {
    *vel = M2C_FIELD(car, Vector4 *, 0x410);
}

static inline void CarA_FitCubic(f32 *coef, f32 x0, f32 x1, f32 y0, f32 y1, f32 m0, f32 m1) {
    f32 dx = x0 - x1;
    f32 dy = y0 - y1;
    f32 a;
    f32 b;
    f32 c;

    a = ((m0 + m1) - 2.0f * dy / dx) / (dx * dx);
    coef[0] = a;
    b = (m0 - dy / dx) / dx - a * (x0 + x1);
    coef[1] = b;
    c = m0 - 3.0f * a * x0 * x0 - 2.0f * b * x0;
    coef[2] = c;
    coef[3] = y0 - a * x0 * x0 * x0 - b * x0 * x0 - c * x0;
}

s32 CarA_Init(void *arg0, void *arg1) {
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    extern void *g_pSourcePlayer;
    struct SlaveParams {
        Vector4 pos;
        Vector4 quat;
        PTR32(u8) info;
    } params;
    Vector4 pos;
    u8 frames[0x800];
    u32 sample;
    f32 volume;
    u8 *info;
    u8 *hull;
    u8 *snd;
    u8 *rack;
    u8 *slave;
    s32 *skel;
    u32 i;
    u8 *init = arg1;
    u8 *car = arg0;

    Thing_Init(car, (s32)HND(init));
    *(u32 *)(car + 0x94) = *(u32 *)(init + 0x3C);
    SLOT_STORE((car + 0x3BC), SLOT_LOAD(u8 *, (init + 0x30)));
    *(Vector4 *)(car + 0x40) = *(Vector4 *)init;
    QuaternionToSubMatrix(init + 0x10, car + 0x10);
    *(Vector4 *)(car + 0x50) = *(Vector4 *)(init + 0x10);
    SLOT_STORE((car + 0x370), NULL);
    info = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0));
    switch (info[0x10]) {
    case 0:
        hull = SLOT_LOAD(u8 *, (info + 0x44));
        *(Vector4 *)(car + 0x540) = *(Vector4 *)hull;
        *(f32 *)(car + 0x70) = *(f32 *)(hull + 0x20);
        *(s16 *)(car + 0x80) = 1;
        break;
    case 1:
        hull = SLOT_LOAD(u8 *, (info + 0x48));
        *(f32 *)(car + 0x540) = 9999.9f;
        *(f32 *)(car + 0x544) = 9999.9f;
        *(f32 *)(car + 0x548) = 9999.9f;
        *(f32 *)(car + 0x70) = 9999.9f;
        for (i = 0; i < *(u32 *)(hull + 0xC); i++) {
            if (*(f32 *)(car + 0x540) > (SLOT_LOAD(Vector4 *, (hull + 0x1C)))[i].x) {
                *(f32 *)(car + 0x540) = (SLOT_LOAD(Vector4 *, (hull + 0x1C)))[i].x;
            }
            if (*(f32 *)(car + 0x544) > (SLOT_LOAD(Vector4 *, (hull + 0x1C)))[i].y) {
                *(f32 *)(car + 0x544) = (SLOT_LOAD(Vector4 *, (hull + 0x1C)))[i].y;
            }
            if (*(f32 *)(car + 0x548) > (SLOT_LOAD(Vector4 *, (hull + 0x1C)))[i].z) {
                *(f32 *)(car + 0x548) = (SLOT_LOAD(Vector4 *, (hull + 0x1C)))[i].z;
            }
        }
        SLOT_STORE((car + 0x370), OConvexHullHolder_Get(SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)) + 0x48))));
        *(f32 *)(car + 0x70) = 13.0f;
        *(s16 *)(car + 0x80) = 4;
        break;
    }
    *(u32 *)(car + 0x78) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)) + 0x14);
    *(f32 *)(car + 0x468) = *SLOT_LOAD(f32 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x7C));
    *(s32 *)(car + 0x400) = -1;
    *(u32 *)(car + 0x48C) = 0;
    *(u32 *)(car + 0x330) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x78)) + 0x10);
    *(f32 *)(car + 0x354) = 0.0f;
    MotionState_Init(car + 0x330, 0);
    car[0x490] = 0;
    *(Vector4 *)(car + 0x520) = *SLOT_LOAD(Vector4 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x80));
    *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x80)) + 0xC) = 1.0f;
    skel = SLOT_LOAD(s32 *, (SLOT_LOAD(u8 *, (car + 0x330)) + 0x14));
    CarA_Extract_Keyframes((CarA *)car, frames);
    XformVec(car + 0x10, (Vector4 *)(car + 0x520), &pos);
    TEST_AnimTraverseHeirarchyToMatricesWorld((Vector4 *)(car + 0x50), &pos, frames, *skel, SLOT_LOAD(void *, (car + 0x340)), 1.0f);
    if (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xE0) >= 1) {
        g_pSourcePlayer = SLOT_LOAD(void *, (car + 0x94));
        MotionState_InitAllSlavesNow(HND(car + 0x330), SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xE4)) + 4, 0);
        g_pSourcePlayer = NULL;
    }
    rack = ThingManagerList_GetByType(car + 0x344, 8);
    if (rack != NULL) {
        *(u32 *)(rack + 0x94) = *(u32 *)(car + 0x94);
        for (i = 0; i < *(u32 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x108); i++) {
            u8 *item = (*(u8 ***)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x10C))[i];
            if (item != NULL) {
                WildCardRack_Add(rack, item, *(s32 *)(SLOT_LOAD(u8 *, (item + 0x30)) + 0x20));
            }
        }
    }
    for (i = 0; i < *(u32 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xD0); i++) {
        *(f32 *)(car + i * 0x30 + 0x1B0) = 0.0f;
        *(u32 *)(car + i * 0x30 + 0x1B8) = 0;
        *(u16 *)(car + i * 0x30 + 0x1B4) = 0;
        *(u16 *)(car + i * 0x30 + 0x1B6) = 0;
        car[i * 0x30 + 0x1C7] = 0;
        *(f32 *)(car + i * 0x30 + 0x1C0) = 0.0f;
        *(f32 *)(car + i * 0x30 + 0x1BC) = 0.0f;
        *(Vector4 *)(car + i * 0x30 + 0x1D0) = g_zeroVector;
    }
    *(f32 *)(car + 0x458) = 0.0f;
    *(f32 *)(car + 0x4E0) = 0.0f;
    *(f32 *)(car + 0x4E4) = 0.0f;
    car[0x4E8] = 1;
    if (*(u8 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x11) == 2) {
        CarA_FitCubic((f32 *)(car + 0x4C0), 0.0f, *(f32 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x1C),
                      *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x80)) + 0x24), 0.0f, 1e-7f, 0.0f);
    } else {
        u8 *curve = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x80));

        CarA_FitCubic((f32 *)(car + 0x4C0), 0.0f, *(f32 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x1C), *(f32 *)(curve + 0x24),
                      *(f32 *)(curve + 0x28), *(f32 *)(curve + 0x2C), *(f32 *)(curve + 0x30));
    }
    sample = 0;
    volume = 0.0f;
    snd = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xDC)) + 0xC));
    *(s32 *)(car + 0x4F0) = -1;
    if (snd != NULL) {
        *(s32 *)(car + 0x4F0) = SoundManager_CreateSource(1);
        sample = *(u32 *)(snd + 0x14);
        SoundSource_Set(*(s32 *)(car + 0x4F0), 8, &sample);
        SoundSource_Set(*(s32 *)(car + 0x4F0), 5, car + 0x40);
        if (snd[0] == 0) {
            SoundSource_Set(*(s32 *)(car + 0x4F0), 2, SLOT_LOAD(u8 *, (snd + 0x18)));
            SoundSource_Set(*(s32 *)(car + 0x4F0), 3, SLOT_LOAD(u8 *, (snd + 0x18)) + 4);
        }
        SoundSource_Play(*(s32 *)(car + 0x4F0));
    }
    *(s32 *)(car + 0x4F4) = -1;
    if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x11C)) != NULL && (snd = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x11C)) + 4))) != NULL) {
        *(s32 *)(car + 0x4F4) = SoundManager_CreateSource(1);
        sample = *(u32 *)(snd + 0x14);
        SoundSource_Set(*(s32 *)(car + 0x4F4), 8, &sample);
        SoundSource_Set(*(s32 *)(car + 0x4F4), 5, car + 0x40);
        if (snd[0] == 0) {
            SoundSource_Set(*(s32 *)(car + 0x4F4), 2, SLOT_LOAD(u8 *, (snd + 0x18)));
            SoundSource_Set(*(s32 *)(car + 0x4F4), 3, SLOT_LOAD(u8 *, (snd + 0x18)) + 4);
        }
        SoundSource_Play(*(s32 *)(car + 0x4F4));
    }
    snd = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x60));
    *(s32 *)(car + 0x4F8) = -1;
    if (snd != NULL) {
        *(s32 *)(car + 0x4F8) = SoundManager_CreateSource(1);
        SoundSource_Set(*(s32 *)(car + 0x4F8), 5, car + 0x40);
        SoundSource_Set(*(s32 *)(car + 0x4F8), 0, &volume);
        if (*SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x60)) == 0) {
            SoundSource_Set(*(s32 *)(car + 0x4F8), 2, SLOT_LOAD(u8 *, (snd + 0x18)));
            SoundSource_Set(*(s32 *)(car + 0x4F8), 3, SLOT_LOAD(u8 *, (snd + 0x18)) + 4);
        }
        SoundSource_Play(*(s32 *)(car + 0x4F8));
    }
    *(s32 *)(car + 0x550) = -1;
    *(Vector4 *)(car + 0x3A0) = g_zeroVector;
    *(Vector4 *)(car + 0x390) = *(Vector4 *)(car + 0x3A0);
    *(u32 *)(car + 0x500) = 0;
    *(u32 *)(car + 0x374) = 0;
    *(u32 *)(car + 0x460) = 0;
    *(u32 *)(car + 0x45C) = 0;
    *(u32 *)(car + 0x464) = 0;
    *(u32 *)(car + 0x404) = 0;
    *(Matrix4 *)(car + 0x3C0) = g_identity;
    *(f32 *)(car + 0x508) = 0.0f;
    *(u32 *)(car + 0x4A4) = 0;
    *(u32 *)(car + 0x494) = 0;
    *(u32 *)(car + 0x498) = 0;
    *(u32 *)(car + 0x49C) = 0;
    *(u32 *)(car + 0x4A0) = 0;
    *(f32 *)(car + 0x4B0) = 0.0f;
    *(f32 *)(car + 0x4B4) = 0.0f;
    *(f32 *)(car + 0x4B8) = 0.0f;
    car[0x491] = 0;
    *(s32 *)(car + 0x450) = 99999;
    *(s32 *)(car + 0x46C) = 10001;
    *(s32 *)(car + 0x470) = 10001;
    *(s32 *)(car + 0x474) = 10001;
    *(s32 *)(car + 0x478) = 10001;
    *(s32 *)(car + 0x47C) = 10001;
    *(s32 *)(car + 0x480) = 10001;
    *(s32 *)(car + 0x484) = 10001;
    *(s32 *)(car + 0x488) = 10001;
    *(u32 *)(car + 0x504) = 0;
    *(f32 *)(car + 0x50C) = 0.0f;
    car[0x15D] = 1;
    *(f32 *)(car + 0x554) = 0.0f;
    CarA_InitInverseBodyInertiaTensor(car);
    *(Vector4 *)(car + 0x380) = g_zeroVector;
    *(Vector4 *)(car + 0x410) = g_zeroVector;
    *(u32 *)(car + 0x454) = 0;
    *(u32 *)(car + 0x3B4) = 0;
    *(s32 *)(car + 0x3B0) = -1;
    *(u32 *)(car + 0x74) |= 4;
    *(u32 *)(car + 0x74) |= 0x80;
    *(u32 *)(car + 0x568) = 0;
    *(u32 *)(car + 0x55C) = 0;
    Thing_AddToGrid(car, 0);
    for (i = 0; i < *(u32 *)(SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x100); i++) {
        params.quat = params.pos = g_zeroVector;
        SETP(params.info, SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x104)) + i * 0x50);
        if (GETP(params.info) != NULL && *(u32 *)(GETP(params.info) + 0x3C) != 0) {
            slave = ThingManager_DARYL_CreateSlave(0xD, &params, *(u16 *)(GETP(params.info) + 0x32));
            *(u32 *)(slave + 0x94) = *(u32 *)(car + 0x94);
            ThingManagerList_Append(car + 0x344, slave);
        }
    }
    *(f32 *)(car + 0x4FC) = 1.0f;
    return 1;
}

void CarA_Release(u8 *car) {
    typedef struct CarNavPath {
        u8 pad[0x568];
        PTR32(u8) path[7];
    } CarNavPath;
    u16 i;

    if (GameSettings_GetCodeVariable(0x12D, 0) >= 3 && SLOT_LOAD(u8 *, (car + 0x94)) != NULL) {
        *(s16 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0xAC) = *(s32 *)(car + 0x3B0);
        for (i = 0; i < 6; i++) {
            if (GETP(((CarNavPath *)car)->path[i]) == NULL || GETP(((CarNavPath *)car)->path[i + 1]) == NULL) {
                break;
            }
            if (NavHolder_GetNavPoint(*(s32 *)(car + 0x3B0)) == GETP(((CarNavPath *)car)->path[i])) {
                *(s16 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0xAE) = *(s32 *)(SLOT_LOAD(u8 *, (u32 *)((car + 0x568)) + (i + 1)) + 0x28);
                break;
            }
        }
    }
    Trigger_VehicleKilled(car);
    OConvexHullHolder_Release(SLOT_LOAD(void *, (car + 0x370)));
    SLOT_STORE((car + 0x370), NULL);
    if (*(u32 *)(car + 0x4F0) != 0xFFFFFFFF) {
        SoundSource_Stop(*(s32 *)(car + 0x4F0));
    }
    if (*(u32 *)(car + 0x4F4) != 0xFFFFFFFF) {
        SoundSource_Stop(*(s32 *)(car + 0x4F4));
    }
    if (*(u32 *)(car + 0x4F8) != 0xFFFFFFFF) {
        SoundSource_Stop(*(s32 *)(car + 0x4F8));
    }
    *(s32 *)(car + 0x4F0) = -1;
    *(s32 *)(car + 0x4F4) = -1;
    *(s32 *)(car + 0x4F8) = -1;
    MotionState_Release(HND(car + 0x330));
    Thing_Release(HND(car));
}

void CarA_GetInverseLocalInertiaTensor(void *arg0, void *arg1) {
    M2C_FIELD(arg1, f32 *, 0) = (f32) M2C_FIELD(arg0, f32 *, 0x3C0);
    M2C_FIELD(arg1, f32 *, 4) = (f32) M2C_FIELD(arg0, f32 *, 0x3D4);
    M2C_FIELD(arg1, f32 *, 8) = (f32) M2C_FIELD(arg0, f32 *, 0x3E8);
}

f32 CarA_Transmission_GetTorqueAtWheels(u8 *car)
{
    u8 *trans;
    f32 scale;
    f32 rpm;
    f32 d;
    f32 c;
    f32 b;
    f32 a;

    a = *(f32 *)(car + 0x4C0);
    b = *(f32 *)(car + 0x4C4);
    c = *(f32 *)(car + 0x4C8);
    d = *(f32 *)(car + 0x4CC);
    rpm = *(f32 *)(car + 0x4E4);
    scale = 1.0f;
    if (*(s32 *)(car + 0x470) < 10000)
    {
        scale = 0.5f;
        rpm *= scale;
    }
    else if (*(u32 *)(car + 0x48C) & 0x1000)
    {
        trans = SLOT_LOAD(u8 *, (car + 0x3BC));
        rpm /= *(f32 *)(trans + 0x30);
        scale *= *(f32 *)(trans + 0x34);
    }
    if (rpm < 0.0f)
    {
        rpm *= -1.0f;
    }
    trans = SLOT_LOAD(u8 *, (car + 0x3BC));
    return scale * (rpm * (rpm * (a * rpm + b) + c) + d) /
           ((f32 *)(SLOT_LOAD(u8 *, (trans + 0xDC))))[*(u8 *)(car + 0x4E8) * 2];
}

void CarA_ExecuteCombo(CarA *car) {
    extern Vector4 g_zeroVector;
    extern char *g_stunt1localString;

    switch (GETP(car->player)->combo) {
    case 2:
        car->speed = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
        car->partVisMask = 0;
        break;
    case 3:
        GETP(car->player)->combo = 0;
        Thing_ProcessEndEvent(car, GETP(GETP(car->vehicleIface)->collider), NULL, &car->localAxis);
        CarA_Release((u8 *)car);
        return;
    case 4:
        car->transmissionFlags ^= 1;
        break;
    case 5:
        car->velocity = g_zeroVector;
        car->gradeOrReverse = 0.0f;
        car->rpm = 0.0f;
        car->curveTableIdx = 1;
        break;
    case 6:
        car->turboTime = 30000;
        break;
    }
    if (car->unk460 == 0) {
        switch (GETP(car->player)->combo) {
        case 9: {
            Vector4 a __attribute__((aligned(16))) = {0.0f, 57.0f, 0.0f, 0.0f};
            Vector4 b __attribute__((aligned(16))) = {5.8f, 0.0f, 0.0f, 0.0f};
            CarA_StuntImpulse(car, &a, &b);
            break;
        }
        case 10: {
            Vector4 a __attribute__((aligned(16))) = {0.0f, 57.0f, 0.0f, 0.0f};
            Vector4 b __attribute__((aligned(16))) = {-5.5f, 0.0f, 0.0f, 0.0f};
            CarA_StuntImpulse(car, &a, &b);
            break;
        }
        case 11: {
            Vector4 a __attribute__((aligned(16))) = {0.0f, 85.0f, 0.0f, 0.0f};
            Vector4 b __attribute__((aligned(16))) = {0.0f, 0.0f, 2.0f, 0.0f};
            CarA_StuntImpulse(car, &a, &b);
            break;
        }
        case 12: {
            Vector4 a __attribute__((aligned(16))) = {0.0f, 85.0f, 0.0f, 0.0f};
            Vector4 b __attribute__((aligned(16))) = {0.0f, 0.0f, -2.0f, 0.0f};
            CarA_StuntImpulse(car, &a, &b);
            break;
        }
        case 13: {
            Vector4 a __attribute__((aligned(16))) = {0.0f, 67.0f, 0.0f, 0.0f};
            Vector4 b __attribute__((aligned(16))) = {-5.056695f, 0.0f, -5.2875133f, 0.0f};
            CarA_StuntImpulse(car, &a, &b);
            break;
        }
        case 14: {
            Vector4 a __attribute__((aligned(16))) = {0.0f, 65.0f, 0.0f, 0.0f};
            Vector4 b __attribute__((aligned(16))) = {5.056695f, 0.0f, 5.2875133f, 0.0f};
            CarA_StuntImpulse(car, &a, &b);
            break;
        }
        case 15: {
            Vector4 b __attribute__((aligned(16))) = {0.0f, -7.0f, 0.0f, 0.0f};
            CarA_StuntImpulse(car, &g_zeroVector, &b);
            SETP(car->stuntPopup, &g_stunt1localString);
            break;
        }
        case 16: {
            Vector4 b __attribute__((aligned(16))) = {0.0f, 7.0f, 0.0f, 0.0f};
            CarA_StuntImpulse(car, &g_zeroVector, &b);
            SETP(car->stuntPopup, &g_stunt1localString);
            break;
        }
        }
    }
    GETP(car->player)->combo = 0;
}

void CarA_ProcessForce(CarA *car, Vector4 *pos, Vector4 *dir, f32 scale)
{

    Vector4 pointVel;
    Vector4 cross;
    Vector4 r;
    Vector4 torque;
    Vector4 carVel;
    Vector4 scaled __attribute__((aligned(16)));

    carVel = car->velocity;
    PSVECScale(dir, &scaled, scale);
    PSVECSubtract(pos, &car->worldMatrix.v[3], &r);
    GetComponentOfVector(dir, &carVel);
    PSVECCrossProduct(&car->localAxis, &r, &cross);
    PSVECAdd(&car->velocity, &cross, &pointVel);
    PSVECCrossProduct(&r, &scaled, &torque);

    car->forceAccum.x += scaled.x;
    car->forceAccum.y += scaled.y;
    car->forceAccum.z += scaled.z;
    car->torqueAccum.x += torque.x;
    car->torqueAccum.y += torque.y;
    car->torqueAccum.z += torque.z;
}

/* Loads each wheel's suspension compression onto its bone (and the parent
   bone's) animation matrix, then adds gravity to the force accumulator. */
static void CarA_ApplyWheelLoads(void *thing) {
    CarA *car = thing;
    Vector4 *up;
    CarTrailBone *wheel;
    CarA *susp;
    u32 i;
    s32 bone;

    susp = car;
    for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
        wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
        if (wheel->parent != 0) {
            bone = wheel->bone - wheel->parent;
            up = &GETP(car->motionState.animMatrices)[bone].v[1];
            GETP(car->motionState.animMatrices)[bone].v[3].x -= susp->wheelStates[0].compression * up->x;
            GETP(car->motionState.animMatrices)[bone].v[3].y -= susp->wheelStates[0].compression * up->y;
            GETP(car->motionState.animMatrices)[bone].v[3].z -= susp->wheelStates[0].compression * up->z;
        } else {
            up = &car->worldMatrix.v[1];
        }
        GETP(car->motionState.animMatrices)[wheel->bone].v[3].x -= susp->wheelStates[0].compression * up->x;
        GETP(car->motionState.animMatrices)[wheel->bone].v[3].y -= susp->wheelStates[0].compression * up->y;
        GETP(car->motionState.animMatrices)[wheel->bone].v[3].z -= susp->wheelStates[0].compression * up->z;
        susp = (CarA *)((u8 *)susp + sizeof(CarAWheelState));
    }
    car->forceAccum.y += GETP(GETP(car->vehicleIface)->collider)->gravityScale * GETP(GETP(car->vehicleIface)->collider)->mass;
}

void CarA_ProcessImpulse(void *thing, Vector4 *normal, Vector4 *pos, Vector4 *pointVel, f32 restitution) {
    CarA *car = thing;
    Vector4 impulse __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 vel __attribute__((aligned(16)));
    Vector4 rxn __attribute__((aligned(16)));
    Vector4 ang __attribute__((aligned(16)));
    Vector4 angxr __attribute__((aligned(16)));
    Vector4 localRxn __attribute__((aligned(16)));
    Vector4 scaledRxn __attribute__((aligned(16)));
    Vector4 torque __attribute__((aligned(16)));
    Vector4 localAng __attribute__((aligned(16)));
    Vector4 scaledAng __attribute__((aligned(16)));
    f32 num;
    f32 j;
    f32 maxAng;
    f32 mag;

    vel = *pointVel;
    PSVECSubtract(pos, &car->worldMatrix.v[3], &r);
    num = -(restitution * GETP(GETP(GETP(car->vehicleIface)->collider)->material)->restitution + 1.0f) * PSVECDotProduct(&vel, normal);
    PSVECCrossProduct(&r, normal, &rxn);
    InverseXformVecOrientation(&car->worldMatrix, &rxn, &localRxn);
    scaledRxn.x = car->inverseBodyInertia.v[0].x * localRxn.x;
    scaledRxn.y = car->inverseBodyInertia.v[1].y * localRxn.y;
    scaledRxn.z = car->inverseBodyInertia.v[2].z * localRxn.z;
    XformVecOrientation(&car->worldMatrix, &scaledRxn, &ang);
    PSVECCrossProduct(&ang, &r, &angxr);
    j = num / (1.0f / GETP(GETP(car->vehicleIface)->collider)->mass + PSVECDotProduct(&angxr, normal));
    impulse.x = j * normal->x;
    impulse.y = j * normal->y;
    impulse.z = j * normal->z;
    car->velocity.x += impulse.x / GETP(GETP(car->vehicleIface)->collider)->mass;
    car->velocity.y += impulse.y / GETP(GETP(car->vehicleIface)->collider)->mass;
    car->velocity.z += impulse.z / GETP(GETP(car->vehicleIface)->collider)->mass;
    PSVECCrossProduct(&r, &impulse, &torque);
    car->angularMomentum.x += torque.x;
    car->angularMomentum.y += torque.y;
    car->angularMomentum.z += torque.z;
    if (car->numWheelsGrounded <= 1) {
        maxAng = GETP(GETP(car->vehicleIface)->engine)->maxAngularVel;
        InverseXformVecOrientation(&car->worldMatrix, &car->angularMomentum, &localAng);
        scaledAng.x = car->inverseBodyInertia.v[0].x * localAng.x;
        scaledAng.y = car->inverseBodyInertia.v[1].y * localAng.y;
        scaledAng.z = car->inverseBodyInertia.v[2].z * localAng.z;
        mag = PSVECSquareMag(&scaledAng);
        if (mag > maxAng * maxAng) {
            mag = maxAng / Sqrt(mag);
            scaledAng.x *= mag;
            scaledAng.y *= mag;
            scaledAng.z *= mag;
            localAng.x = scaledAng.x / car->inverseBodyInertia.v[0].x;
            localAng.y = scaledAng.y / car->inverseBodyInertia.v[1].y;
            localAng.z = scaledAng.z / car->inverseBodyInertia.v[2].z;
            XformVecOrientation(&car->worldMatrix, &localAng, &car->angularMomentum);
        }
    }
    CarA_UpdateAngularVelocity(car);
}

void CarA_ComputeForces(CarA *car) {
    typedef struct RayHit {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[8];
        PTR32(u8) surface;
        u8 pad2C[0x24];
    } RayHit;
    extern Vector4 g_zeroVector;
    RayHit hit __attribute__((aligned(16)));
    f32 worldPos[4];
    f32 down[4];
    u32 i;
    CarAForcePoints *forces;
    f32 dot;
    f32 scale;
    f32 dist;

    car->forceAccum = car->torqueAccum = g_zeroVector;
    CarA_ApplyWheelLoads(car);
    CarA_ComputeSuspensionForces(car);
    dot = PSVECDotProduct(&car->worldMatrix.v[2], &car->velocity);
    forces = GETP(GETP(car->vehicleIface)->forcePoints);
    if (dot < 0.0f) {
        return;
    }
    down[0] = -car->worldMatrix.v[1].x;
    down[1] = -car->worldMatrix.v[1].y;
    down[2] = -car->worldMatrix.v[1].z;
    scale = 1.0f - GETP(GETP(car->vehicleIface)->engine)->unk58 * (car->worldMatrix.v[1].y - 1.0f);
    for (i = 0; i < forces->count; i++) {
        if (GETP(forces->points)[i].ray) {
            dist = 6.0f;
            if (car->numWheelsGrounded < GETP(car->vehicleIface)->numWheels && !RayHitsTerrainAndObjects(&car->worldMatrix.v[3], down, &dist, &hit)) {
                continue;
            }
        }
        XformVec(&car->worldMatrix, &GETP(forces->points)[i].pos, (Vector4 *)worldPos);
        CarA_ProcessForce(car, (Vector4 *)worldPos, (Vector4 *)down, scale * (dot * GETP(forces->points)[i].weight));
    }
}

s32 CarA_PreMove(void *thing) {
    typedef struct EffectVtbl {
        u8 pad0[0x18];
        FPTR32(void, (void *thing, void *params, s32 arg, f32 scale)) spawn;
    } EffectVtbl;
    typedef struct EffectThing {
        u8 pad0[0x8C];
        PTR32(EffectVtbl) vtbl;
    } EffectThing;
    typedef struct TurboFx {
        Vector4 pos;
        Vector4 dir;
        PTR32(void) owner;
        s32 unk24;
        s32 unk28;
        u8 pad2C[4];
        Vector4 vel;
        u8 unk40;
    } TurboFx;
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    extern void *g_pSourcePlayer;
    extern Vector4 g_zeroVector;
    CarA *car = thing;
    TurboFx fx __attribute__((aligned(16)));
    f32 engineVolume;
    f32 enginePitch;
    f32 windVolume;
    f32 windPitch;
    f32 maxRpm;
    f32 invMass;
    f32 speed;
    f32 spin;
    CarTrailBone *wheel;
    u32 i;

    car->prevVelocity = car->velocity;
    car->unk4FC += g_fDeltaTimeSec;
    if (car->unk4FC >= 1.0f) {
        car->unk4FC = 1.0f;
    }
    car->transmissionFlags &= ~0x20000;
    if (Timer_Interrupt300()) {
        car->transmissionFlags &= ~0x10000;
    }

    if (GETP(car->player) != NULL) {
        switch (GETP(car->vehicleIface)->driveMode) {
        case 0:
            break;
        case 1:
            CarA_PreMove_Control_HW(car);
            break;
        }

        if (car->turboCooldown >= 0) {
            if (!(car->transmissionFlags & 0x1000) && (GETP(car->player)->buttonsHeld & 4) && car->turboTime > 0) {
                car->transmissionFlags |= 0x1000;
                if (car->unk460 == 0) {
                    SETP(fx.owner, GETP(GETP(car->player)->vehicle));
                    fx.unk24 = 0;
                    fx.pos.x = car->worldMatrix.v[3].x - 8.0f * car->worldMatrix.v[2].x - 3.0f * car->worldMatrix.v[1].x;
                    fx.pos.y = car->worldMatrix.v[3].y - 8.0f * car->worldMatrix.v[2].y - 3.0f * car->worldMatrix.v[1].y;
                    fx.pos.z = car->worldMatrix.v[3].z - 8.0f * car->worldMatrix.v[2].z - 3.0f * car->worldMatrix.v[1].z;
                    fx.dir.x = car->worldMatrix.v[2].x - 0.2f * car->worldMatrix.v[1].x;
                    fx.dir.y = car->worldMatrix.v[2].y - 0.2f * car->worldMatrix.v[1].y;
                    fx.dir.z = car->worldMatrix.v[2].z - 0.2f * car->worldMatrix.v[1].z;
                    if (PSVECSquareMag(&fx.dir)) {
                        PSVECNormalize(&fx.dir, &fx.dir);
                    }
                    fx.unk28 = 0;
                    fx.vel = g_zeroVector;
                    fx.unk40 = 0;
                    GETP(GETP(((EffectThing *)GETP(GETP(car->player)->vehicle))->vtbl)->spawn)(GETP(GETP(car->player)->vehicle), &fx, 0, 280.0f);
                }
                if (GETP(car->vehicleIface)->numSlaves > 1) {
                    MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->slaves) + 0x10, 1);
                }
            }
            car->turboCooldown -= g_iDeltaTimeMS;
        } else if (GETP(car->player)->buttonsHeld & 4) {
            car->turboCooldown = 700;
        }

        if (car->transmissionFlags & 0x1000) {
            if (GETP(car->player)->throttle && !GETP(car->player)->brake) {
                car->turboTime -= g_iDeltaTimeMS;
                if (GameSettings_IsCheatEnabled(5) && GETP(car->player) != NULL && Player_IsHuman(GETP(car->player))) {
                    car->turboTime = 30000;
                }
                if (car->turboTime <= 0) {
                    car->transmissionFlags &= ~0x1000;
                    MotionState_ReleaseSlavesByType(HND(&car->motionState), 1);
                }
            } else {
                car->transmissionFlags &= ~0x1000;
                MotionState_ReleaseSlavesByType(HND(&car->motionState), 1);
            }
        }

        if (car->tiltState != 0) {
            if (car->tiltState != 3 && GETP(car->player)->stickX < 0x40 && GETP(car->player)->stickX > -0x40) {
                CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[1],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
                car->tiltState = 0;
            }
            if (car->worldMatrix.v[1].y > 0.35f) {
                CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[1],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
                car->tiltState = 0;
                car->stuckCounter = 0;
                car->unk45C = 0;
            }
        }
        if (car->stuckCounter > 600) {
            if (GETP(car->player)->stickX > 0x40) {
                if (car->tiltState != 1) {
                    CarA_SetBodyOffset(car, car->tiltX - 1.0f, 1.0f - car->tiltY, 0.0f);
                    car->tiltState = 1;
                }
            } else if (GETP(car->player)->stickX < -0x40) {
                if (car->tiltState != 2) {
                    CarA_SetBodyOffset(car, 1.0f - car->tiltX, 1.0f - car->tiltY, 0.0f);
                    car->tiltState = 2;
                }
            } else if (car->tiltState != 0) {
                CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[1],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
                car->tiltState = 0;
            }
        }
    } else {
        car->gradeOrReverse = 0.0f;
        car->rpm = 0.0f;
        car->curveTableIdx = 1;
    }

    if (car->unk4FC >= 1.0f) {
        if (car->transmissionFlags & 0x4000) {
            MotionState_ReleaseSlavesByType(HND(&car->motionState), 9);
            car->transmissionFlags &= ~0x4000;
        }
    } else if (!(car->transmissionFlags & 0x4000)) {
        if (GETP(car->vehicleIface)->numSlaves > 9) {
            MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->slaves) + 0x70, 9);
            car->transmissionFlags |= 0x4000;
        }
    }

    CarA_ComputeForces(car);
    car->velocity.x += g_fDeltaTimeSec * (car->forwardAccel * car->worldMatrix.v[0].x);
    car->velocity.y += g_fDeltaTimeSec * (car->forwardAccel * car->worldMatrix.v[0].y);
    car->velocity.z += g_fDeltaTimeSec * (car->forwardAccel * car->worldMatrix.v[0].z);
    g_pSourcePlayer = GETP(car->player);
    CarA_SlaveWeaponHeartbeat(car);
    g_pSourcePlayer = NULL;

    invMass = g_fDeltaTimeSec / GETP(GETP(car->vehicleIface)->collider)->mass;
    if (car->collDirtyFlags & 0x10000) {
        car->torqueAccum = g_zeroVector;
        car->forceAccum = car->torqueAccum;
    } else {
        car->velocity.x += invMass * car->forceAccum.x;
        car->velocity.y += invMass * car->forceAccum.y;
        car->velocity.z += invMass * car->forceAccum.z;
        car->angularMomentum.x += g_fDeltaTimeSec * car->torqueAccum.x;
        car->angularMomentum.y += g_fDeltaTimeSec * car->torqueAccum.y;
        car->angularMomentum.z += g_fDeltaTimeSec * car->torqueAccum.z;
        CarA_UpdateAngularVelocity(car);
        car->torqueAccum = g_zeroVector;
        car->forceAccum = car->torqueAccum;
    }

    Thing_CollideWithForceBoxes(car);
    car->navUpdateTimer -= g_iDeltaTimeMS;
    if (car->navUpdateTimer < 0) {
        car->navPointIndex = NavHolder_FindIndexOfNearestNavPoint(&car->worldMatrix.v[3], car->navPointIndex);
        car->navUpdateTimer = RandomAdd_s32(300, 100);
    }
    car->lowSpeedFlag += g_iDeltaTimeMS;
    if (car->unk45C > 0) {
        car->unk45C -= g_iDeltaTimeMS;
        car->stuckCounter += g_iDeltaTimeMS;
    }

    if ((u32)car->soundIds[1] != 0xFFFFFFFF) {
        if (_fabs(car->unk4E0) < (maxRpm = GETP(GETP(car->vehicleIface)->engine)->unk68)) {
            engineVolume = (maxRpm - _fabs(car->rpm)) / maxRpm;
            enginePitch = GETP(GETP(car->vehicleIface)->engine)->unk64 + _fabs(car->rpm) / GETP(car->vehicleIface)->unk1C;
            if (engineVolume < 0.0f) {
                engineVolume = 0.0f;
            }
            if (engineVolume > 1.0f) {
                engineVolume = 1.0f;
            }
            if (GETP(car->vehicleIface)->numSoundSets != 0) {
                enginePitch *= GETP(GETP(car->vehicleIface)->soundSets)[1]->pitchScale;
            }
            SoundSource_Set(car->soundIds[1], 5, &car->worldMatrix.v[3]);
            SoundSource_Set(car->soundIds[1], 6, &car->velocity);
            SoundSource_Set(car->soundIds[1], 1, &enginePitch);
            SoundSource_Set(car->soundIds[1], 0, &engineVolume);
        }
    }
    if ((u32)car->soundIds[0] != 0xFFFFFFFF) {
        windVolume = 1.0f - _fabs(car->gradeOrReverse) / 300.0f;
        windPitch = 3.0f * (car->rpm / GETP(car->vehicleIface)->unk1C);
        if (windPitch < 0.0f) {
            windPitch *= -1.0f;
        }
        windPitch += 0.5f;
        if (windPitch > 5.0f) {
            windPitch = 5.0f;
        }
        if (windVolume < 0.0f) {
            windVolume = 0.0f;
        }
        if (windVolume > 1.0f) {
            windVolume = 1.0f;
        }
        SoundSource_Set(car->soundIds[0], 0, &windVolume);
        SoundSource_Set(car->soundIds[0], 5, &car->worldMatrix.v[3]);
        SoundSource_Set(car->soundIds[0], 6, &car->velocity);
        SoundSource_Set(car->soundIds[0], 1, &windPitch);
    }

    speed = g_fDeltaTimeSec * PSVECDotProduct(&car->velocity, &car->worldMatrix.v[2]);
    spin = car->gradeOrReverse * g_fDeltaTimeSec;
    if (GETP(car->player) != NULL && GETP(car->player)->brake == 0) {
        for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
            wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
            car->wheelStates[i].rotation += (wheel->unk14 * spin + (1.0f - wheel->unk14) * speed) / wheel->offset;
        }
    } else {
        for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
            wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
            car->wheelStates[i].rotation += (wheel->unk14 * spin + (1.0f - wheel->unk14) * speed) / wheel->offset;
        }
    }
    if (GETP(car->player) != NULL) {
        CarA_ExecuteCombo(car);
    }
    Thing_PassiveTargeting(car);
    return 1;
}

s32 CarA_Move(void *thing) {
    typedef struct HealthSlave {
        u8 pad00[0xB8];
        f32 fraction;
    } HealthSlave;
    extern Matrix4 g_identity;
    extern void *g_pSourcePlayer;
    CarA *car = thing;
    HealthSlave *slave;
    u32 *numBones;
    u32 i;
    CarTrailBone *wheel;
    s32 bone;
    f32 dy;
    f32 dz;
    f32 t;
    f32 oz;
    f32 oy;
    f32 ox;
    f32 dx;
    f32 comp;
    f32 scale;
    GXColor color __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Vector4 trailPos __attribute__((aligned(16)));
    AnimKeyFrame frames[64] __attribute__((aligned(16)));

    Timer_Interrupt300();
    slave = ThingManagerList_GetByType(&car->motionState.slaves, 0x11);
    if (slave != NULL) {
        slave->fraction = (f32)car->speed / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth;
        if (GETP(car->player) != NULL && !Player_IsHuman(GETP(car->player))) {
            slave->fraction /= *GETP(GETP(car->player)->healthScale);
        }
        if (slave->fraction > 1.0f) {
            slave->fraction = 1.0f;
        }
    }
    scale = 1.0f / Sqrt(car->animRefPos.w * car->animRefPos.w +
                        (car->animRefPos.z * car->animRefPos.z +
                         (car->animRefPos.x * car->animRefPos.x + car->animRefPos.y * car->animRefPos.y)));
    car->animRefPos.x *= scale;
    car->animRefPos.y *= scale;
    car->animRefPos.z *= scale;
    car->animRefPos.w *= scale;
    if (GETP(car->player) != NULL && Player_IsHuman(GETP(car->player)) && GameSettings_IsCheatEnabled(5)) {
        car->turboTime = 30000;
    }
    if (Thing_CollideWithTeleporters(car)) {
        CarA_StuntFail(car);
    }
    Thing_CollideWithItems(car);
    numBones = GETP(GETP(car->motionState.model)->numBones);
    CarA_Extract_Keyframes(car, frames);
    XformVec(&car->worldMatrix, &car->bodyOffset, &pos);
    TEST_AnimTraverseHeirarchyToMatricesWorld(&car->animRefPos, &pos, frames, *numBones, GETP(car->motionState.animMatrices), 1.0f);
    for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
        wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
        if (wheel->parent != 0) {
            bone = wheel->bone - wheel->parent;
            comp = car->wheelStates[i].compression;
            dx = GETP(car->motionState.animMatrices)[bone].v[1].x;
            ox = comp * dx;
            dy = GETP(car->motionState.animMatrices)[bone].v[1].y;
            oy = comp * dy;
            dz = GETP(car->motionState.animMatrices)[bone].v[1].z;
            oz = comp * dz;
            GETP(car->motionState.animMatrices)[bone].v[3].x += ox;
            GETP(car->motionState.animMatrices)[bone].v[3].y += oy;
            GETP(car->motionState.animMatrices)[bone].v[3].z += oz;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].x += ox;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].y += oy;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].z += oz;
        } else {
            dx = car->worldMatrix.v[1].x;
            dy = car->worldMatrix.v[1].y;
            dz = car->worldMatrix.v[1].z;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].x += car->wheelStates[i].compression * dx;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].y += car->wheelStates[i].compression * car->worldMatrix.v[1].y;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].z += car->wheelStates[i].compression * car->worldMatrix.v[1].z;
        }
        if (car->wheelStates[i].skidColor.a != 0) {
            wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
            t = 0.1f - wheel->offset;
            trailPos.x = t * dx + GETP(car->motionState.animMatrices)[wheel->bone].v[3].x;
            trailPos.y = t * dy + GETP(car->motionState.animMatrices)[wheel->bone].v[3].y;
            trailPos.z = t * dz + GETP(car->motionState.animMatrices)[wheel->bone].v[3].z;
            color = car->wheelStates[i].skidColor;
            Trail_AddSegment(&car->wheelStates[i].skidTrail, &trailPos, &GETP(car->motionState.animMatrices)[wheel->bone - 1].v[2],
                             GETP(car->vehicleIface)->skidsEnabled, color, car->wheelStates[i].skidTexture, wheel->width0, wheel->width1);
        } else {
            car->wheelStates[i].skidTrail = -1;
        }
    }
    g_pSourcePlayer = GETP(car->player);
    MotionState_Heartbeat(&car->motionState, &g_identity, car);
    g_pSourcePlayer = NULL;
    return 1;
}

void CarA_SetSlavePlayers(u8 *car)
{
    u8 *slave;

    for (slave = SLOT_LOAD(u8 *, (car + 0x344)); slave != NULL; slave = SLOT_LOAD(u8 *, (slave + 0xA4)))
    {
        *(s32 *)(slave + 0x94) = *(s32 *)(car + 0x94);
    }
}

void CarA_DrawReflection(void *thing) {
    typedef struct { PTR32(void) geom; u16 mtxIndex; u8 pad6[2]; } ModelPart;
    typedef struct {
        u32 numGeoms;
        PTR32(void *) geoms;
        u32 numParts;
        PTR32(ModelPart) parts;
        u8 pad10[4];
        PTR32(s32) skel;
    } CarModel;
    Matrix4 matrices[64];
    u8 frames[0x800];
    u8 frames2[0x800];
    Matrix4 partMtx;
    Matrix4 mtx;
    Matrix4 tmp;
    Matrix4 partMtx2;
    Vector4 plane;
    Vector4 pos;
    CarA *car = thing;
    f32 offset = 0.0f;
    Matrix4 *oldMatrices = GETP(car->motionState.animMatrices);
    f32 h;

    if (GETP(car->motionState.model) != NULL) {
        LightManager_LightThing(car);
        SETP(car->motionState.animMatrices, matrices);
        {
            s32 *skel = GETP(((CarModel *)GETP(car->motionState.model))->skel);
            CarA_Extract_Keyframes(car, frames);
            XformVec(&car->worldMatrix, &car->bodyOffset, &pos);
            TEST_AnimTraverseHeirarchyToMatricesWorld(&car->animRefPos, &pos, frames, *skel, matrices, 1.0f);
        }
        Create_Plane3(&plane, &car->worldMatrix.v[1], &pos);
        h = -Distance_Plane(&plane, &matrices[GETP(GETP(car->vehicleIface)->wheels)->bone].v[3]);
        offset += 2.0f * h + 2.0f * GETP(GETP(car->vehicleIface)->wheels)->offset;
        {
            u32 i;
            for (i = 0; i < ((CarModel *)GETP(car->motionState.model))->numParts; i++) {
                ModelPart *e = &GETP(((CarModel *)GETP(car->motionState.model))->parts)[i];
                Gfx_DrawGeometry(HND(GETP(e->geom)), HND(&matrices[e->mtxIndex]));
            }
        }
        {
            u32 j;
            CarPartList *list = GETP(GETP(GETP(GETP(car->vehicleIface)->modelRef)->data)->parts);
            CarPart *part;
            u32 bit;
            void *geom;
            if (list != NULL) {
                for (j = 0; j < list->count; j++) {
                    part = &GETP(list->parts)[j];
                    geom = GETP(part->geom);
                    bit = car->partVisMask & (1 << j);
                    if (bit && !(part->flags & 1)) {
                        continue;
                    }
                    if (!bit && (part->flags & 1)) {
                        continue;
                    }
                    if (part->xform) {
                        partMtx = matrices[part->mtxIndex];
                        XformVec(&partMtx, GETP(GETP(list->parts)[j].offset), &partMtx.v[3]);
                        Gfx_DrawGeometry(HND(geom), HND(&partMtx));
                    } else {
                        Gfx_DrawGeometry(HND(geom), HND(&matrices[part->mtxIndex]));
                    }
                }
            }
        }
        MotionState_DrawSlaves(&car->motionState, 0);
        mtx = car->worldMatrix;
        {
            u32 i;
            s32 *skel = GETP(((CarModel *)GETP(car->motionState.model))->skel);
            CarA_Extract_Keyframes(car, frames2);
            Gfx_AnimTraverseHeirarchyToMatrices(frames2, *skel, matrices);
            mtx.v[1].x *= -1.0f;
            mtx.v[1].y *= -1.0f;
            mtx.v[1].z *= -1.0f;
            mtx.v[0].x *= -1.0f;
            mtx.v[0].y *= -1.0f;
            mtx.v[0].z *= -1.0f;
            XformVec(&car->worldMatrix, &car->bodyOffset, &mtx.v[3]);
            mtx.v[3].x += mtx.v[1].x * offset;
            mtx.v[3].y += mtx.v[1].y * offset;
            mtx.v[3].z += mtx.v[1].z * offset;
            for (i = 0; i < *skel; i++) {
                tmp = matrices[i];
                MultiplyMatrix(&tmp, &mtx, &matrices[i]);
            }
        }
        {
            u32 i;
            for (i = 0; i < ((CarModel *)GETP(car->motionState.model))->numParts; i++) {
                ModelPart *e = &GETP(((CarModel *)GETP(car->motionState.model))->parts)[i];
                Gfx_DrawGeometry(HND(GETP(e->geom)), HND(&matrices[e->mtxIndex]));
            }
        }
        {
            u32 j;
            CarPartList *list = GETP(GETP(GETP(GETP(car->vehicleIface)->modelRef)->data)->parts);
            CarPart *part;
            u32 bit;
            void *geom;
            if (list != NULL) {
                for (j = 0; j < list->count; j++) {
                    part = &GETP(list->parts)[j];
                    geom = GETP(part->geom);
                    bit = car->partVisMask & (1 << j);
                    if (bit && !(part->flags & 1)) {
                        continue;
                    }
                    if (!bit && (part->flags & 1)) {
                        continue;
                    }
                    if (part->xform) {
                        partMtx2 = matrices[part->mtxIndex];
                        XformVec(&partMtx2, GETP(GETP(list->parts)[j].offset), &partMtx2.v[3]);
                        Gfx_DrawGeometry(HND(geom), HND(&partMtx2));
                    } else {
                        Gfx_DrawGeometry(HND(geom), HND(&matrices[part->mtxIndex]));
                    }
                }
            }
        }
        MotionState_DrawSlavesAttachCoronaOnly(&car->motionState, 0);
        SETP(car->motionState.animMatrices, oldMatrices);
    }
}

void CarA_DrawScript(u8 *car) {
    typedef struct { PTR32(void) geom; u16 mtxIndex; u8 pad6[2]; } ModelPart;
    typedef struct {
        u32 numGeoms;
        PTR32(void *) geoms;
        u32 numParts;
        PTR32(ModelPart) parts;
        u8 pad10[4];
        PTR32(s32) skel;
    } Model;
    typedef struct CarPart {
        u8 flags;
        u8 xform;
        u8 pad2[2];
        u16 mtxIndex;
        u8 pad6[2];
        PTR32(void) geom;
        PTR32(Vector4) offset;
        u8 pad10[0xC];
    } CarPart;
    typedef struct CarPartList {
        u32 count;
        PTR32(CarPart) parts;
    } CarPartList;
    Matrix4 *matrices;
    u32 i;
    s32 *skel;
    u32 j;
    CarPartList *list;
    CarPart *part;
    u32 bit;
    void *geom;
    Matrix4 mtx;

    matrices = SLOT_LOAD(Matrix4 *, (car + 0x340));
    if (matrices == NULL) {
        return;
    }
    LightManager_LightThing(car);
    skel = GETP((SLOT_LOAD(Model *, (car + 0x330)))->skel);
    for (i = 0; i < (SLOT_LOAD(Model *, (car + 0x330)))->numGeoms; i++) {
        Gfx_DrawAnimGeometryAux(GETP((SLOT_LOAD(Model *, (car + 0x330)))->geoms)[i], matrices, *skel, 0);
    }
    for (i = 0; i < (SLOT_LOAD(Model *, (car + 0x330)))->numParts; i++) {
        ModelPart *e = &GETP((SLOT_LOAD(Model *, (car + 0x330)))->parts)[i];
        Gfx_DrawGeometry_MatrixNotCached(HND(GETP(e->geom)), HND(&matrices[e->mtxIndex]));
    }
    list = SLOT_LOAD(CarPartList *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x78)) + 0x10)) + 0x38));
    if (list != NULL) {
        for (j = 0; j < list->count; j++) {
            part = &GETP(list->parts)[j];
            geom = GETP(part->geom);
            bit = *(u32 *)(car + 0x374) & (1 << j);
            if (bit && !(part->flags & 1)) {
                continue;
            }
            if (!bit && (part->flags & 1)) {
                continue;
            }
            if (part->xform) {
                mtx = matrices[part->mtxIndex];
                XformVec(&mtx, GETP(GETP(list->parts)[j].offset), &mtx.v[3]);
                Gfx_DrawGeometry(HND(geom), HND(&mtx));
            } else {
                Gfx_DrawGeometry_MatrixNotCached(HND(geom), HND(&matrices[part->mtxIndex]));
            }
        }
    }
}
extern u8 g_pNoRayCollideThing[];  /* typed per function below */

void CarA_Draw(u8 *car)
{
    #define g_pNoRayCollideThing (*(void **)(void *)&g_pNoRayCollideThing)
    Matrix4 mtx __attribute__((aligned(16)));
    u8 *p2;

    if (SLOT_LOAD(u8 *, (car + 0x94)) != NULL && (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x6C) & 0x10) &&
        !(*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x6C) & 0x80) && ViewManager_GetViewFormat() <= 1) {
        p2 = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x80));
        if ((*((u8 **)(p2 + 0x84)))[((SLOT_LOAD(u8 *, (car + 0x94)))[0x31] % *(u32 *)(p2 + 0x80)) * 0xC] == 4) {
            return;
        }
    }
    CarA_DrawScript(car);
    if (SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x54)) != NULL && StateManager_CurrState() == 5) {
        mtx = *(Matrix4 *)(car + 0x10);
        XformVec(car + 0x10, (Vector4 *)(car + 0x520), &mtx.v[3]);
        if (Player_IsHuman(SLOT_LOAD(void *, (car + 0x94)))) {
            Gfx_DrawGeometry(HND(SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x54))), HND(&mtx));
        }
    }
    g_pNoRayCollideThing = car;
    MotionState_DrawSlaves(car + 0x330, 0);
    g_pNoRayCollideThing = NULL;
#undef g_pNoRayCollideThing
}

f32 CarA_GetMass(u8 *car)
{
    u8 *coll;

    coll = SLOT_LOAD(u8 *, (car + 0x3BC));
    coll = SLOT_LOAD(u8 *, (coll + 0xA0));
    return *(f32 *)(coll + 0x20);
}

void CarA_DynamicRotation(u8 *car, f32 dt) {
    typedef struct OBox {
        Vector4 center;
        Vector4 corners[8];
        u8 pad[0x60];
    } OBox;
    typedef struct RayHit {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[8];
        PTR32(u8) surface;
        u8 pad2C[0x24];
    } RayHit;
    #define g_pNoRayCollideThing (*(u8 **)(void *)&g_pNoRayCollideThing)
    OBox obox;
    RayHit hit;
    Matrix4 mtx;
    Quaternion q __attribute__((aligned(16)));
    Quaternion newQ __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 v __attribute__((aligned(16)));
    f32 t;
    f32 mag;
    u32 i;
    OBox *old;
    void *box;
    s8 hitAny;

    t = dt;
    axis = *(Vector4 *)(car + 0x410);
    mag = MagnitudeVector(&axis);
    if (mag > 0.0001f) {
        axis.x /= mag;
        axis.y /= mag;
        axis.z /= mag;
    }
    GenerateQuaternion(&axis, &q, mag * t);
    QuaternionMult((Vector4 *)&q, (Vector4 *)(car + 0x50), (Vector4 *)&newQ);
    CarA_StuntsGetNewOrientation(car, &newQ, t);
    QuaternionToSubMatrix(&newQ, &mtx);
    mtx.v[3] = *(Vector4 *)(car + 0x40);
    switch ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)))[0x10]) {
    case 0:
        box = SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)) + 0x44));
        old = CarA_GetOrientedCollision(car);
        hitAny = 0;
        BuildOOBOffset(box, &mtx, car + 0x520, &obox);
        g_pNoRayCollideThing = car;
        for (i = 0; i < 8; i++) {
            if (t > 0.0f) {
                dir.x = (obox.corners[i].x - old->corners[i].x) / dt;
                dir.y = (obox.corners[i].y - old->corners[i].y) / dt;
                dir.z = (obox.corners[i].z - old->corners[i].z) / dt;
                if (RayHitsTerrainAndObjects(&old->corners[i], &dir, &t, &hit)) {
                    hitAny = 1;
                    CarA_ProcessImpulse(car, &hit.normal, &hit.pos, &dir, *(f32 *)(GETP(hit.surface) + 0x18));
                }
            }
        }
        g_pNoRayCollideThing = NULL;
        if (!hitAny) {
            if (OBoxHitsTerrainAndObjectsZeroTimeEdgesOnly(&obox, car, &hit)) {
                PSVECSubtract(&hit.pos, car + 0x40, &r);
                PSVECCrossProduct(car + 0x410, &r, &v);
                CarA_ProcessImpulse(car, &hit.normal, &hit.pos, &v, 0.5f);
            } else {
                *(Quaternion *)(car + 0x50) = newQ;
                *(Matrix4 *)(car + 0x10) = mtx;
                *(OBox *)(car + 0xC0) = obox;
                SLOT_STORE((car + 0x160), car + 0x30);
                SLOT_STORE((car + 0x164), car + 0x10);
                SLOT_STORE((car + 0x168), car + 0x20);
                *(u32 *)(car + 0x74) &= ~0x180;
            }
        }
        break;
    case 1:
        break;
    }
#undef g_pNoRayCollideThing
}

void CarA_ProcessDamageSystem(CarA *car, Vector4 *hitPos, s32 level, f32 impulse) {
    typedef struct DebrisParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        PTR32(void) player;
        f32 unk48;
        f32 unk4C;
    } DebrisParams;
    typedef struct PartParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 axis;
        Vector4 vel;
        f32 gravity;
        PTR32(void) geom;
        s32 life;
        u32 unk4C;
        s32 unk50;
        u8 pad54[0xC];
    } PartParams;
    typedef struct SlaveParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        PTR32(void) info;
        u32 unk34;
        f32 unk38;
        u8 pad3C[4];
    } SlaveParams;
    extern Vector4 g_zeroVector;
    PartParams partParams;
    DebrisParams debrisParams;
    SlaveParams slaveParams;
    Vector4 pos __attribute__((aligned(16)));
    Vector4 vel __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 quat __attribute__((aligned(16)));
    u32 bit;
    u32 i;
    CarPartList *list;
    f32 scale;
    u8 *thing;
    u8 *slave;
    f32 absImpulse;
    u32 debris;

    absImpulse = _fabs(impulse);
    if (hitPos == NULL) {
        return;
    }
    if (GETP(car->vehicleIface)->damageLevel > level && !(absImpulse > 70.0f)) {
        return;
    }
    if (!(100.0f * (f32)car->speed / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth <= GETP(car->vehicleIface)->damagePercent)) {
        return;
    }
    list = GETP(GETP(GETP(GETP(car->vehicleIface)->modelRef)->data)->parts);
    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        if (car->partVisMask & (bit = 1 << i)) {
            continue;
        }
        if (GETP(list->parts)[i].xform) {
            XformVec(&GETP(car->motionState.animMatrices)[GETP(list->parts)[i].mtxIndex], GETP(GETP(list->parts)[i].offset), &pos);
        } else {
            pos = GETP(car->motionState.animMatrices)[GETP(list->parts)[i].mtxIndex].v[3];
        }
        if (PSVECSquareDistance(&pos, hitPos) < 25.0f) {
            if (GETP(list->parts)[i].flags == 0) {
                scale = RandomNumberGenerator_GetRandomFloat(40.0f, 80.0f);
                RandomNumberGenerator_GetRandomVector(&dir);
                vel = car->velocity;
                vel.x = dir.x * scale + vel.x;
                vel.y = dir.y * scale + vel.y;
                vel.z = dir.z * scale + vel.z;
                SubMatrixToQuaternion(&GETP(car->motionState.animMatrices)[GETP(list->parts)[i].mtxIndex], &quat);
                debris = GETP(list->parts)[i].debris;
                partParams.life = 3000;
                partParams.gravity = -50.0f;
                partParams.vel = vel;
                partParams.pos = pos;
                SETP(partParams.geom, GETP(GETP(list->parts)[i].geom));
                partParams.quat = quat;
                partParams.unk50 = 1;
                if (debris != 0) {
                    debrisParams.id = debris;
                    debrisParams.pos = pos;
                    debrisParams.quat = quat;
                    debrisParams.vel = vel;
                    SETP(debrisParams.player, NULL);
                    debrisParams.unk48 = 0.0f;
                    debrisParams.unk4C = 0.0f;
                    debrisParams.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &debrisParams);
                }
                RandomNumberGenerator_GetRandomVector(&partParams.axis);
                partParams.axis.w = 0.017453292f * RandomAdd(100.0f, 1260.0f);
                thing = ThingManager_DARYL_CreateThing(6, &partParams);
                if (thing != NULL && GETP(GETP(list->parts)[i].slaveInfo) != NULL) {
                    slaveParams.unk38 = 0.0f;
                    slaveParams.quat.w = 1.0f;
                    slaveParams.quat.x = 0.0f;
                    slaveParams.quat.y = 0.0f;
                    slaveParams.quat.z = 0.0f;
                    slaveParams.pos = Vector4_OrZero(GETP(GETP(list->parts)[i].slavePos));
                    SETP(slaveParams.info, GETP(GETP(list->parts)[i].slaveInfo));
                    slaveParams.unk34 = 0;
                    slaveParams.vel = g_zeroVector;
                    slave = ThingManager_DARYL_CreateSlave(2, &slaveParams, 0xFF);
                    if (slave != NULL) {
                        slave[0x83] = 0xFF;
                        ThingManagerList_Append(thing + 0x88, slave);
                    }
                }
            }
            car->partVisMask |= bit;
        }
    }
}

void CarA_GetTargetPos(u8 *car, Vector4 *pos)
{
    u8 *coll;

    coll = SLOT_LOAD(u8 *, (car + 0x3BC));
    coll = SLOT_LOAD(u8 *, (coll + 0xA0));
    switch (*(u8 *)(coll + 0x10))
    {
    case 0:
        *pos = *(Vector4 *)(car + 0xC0);
        break;
    case 1:
        *pos = *(Vector4 *)(SLOT_LOAD(u8 *, (car + 0x370)) + 0x3D0);
        break;
    }
}

void CarA_GetInterfaceInfo(u8 *car, u8 *info) {
    typedef struct {
        u32 active;
        u8 unk4[0x3C];
        s16 ammo;
        u8 unk42[0xA];
        PTR32(u8) item;
    } RackSlot;
    typedef struct {
        u8 unk0[0xB0];
        RackSlot slots[12];
        u8 current;
    } WeaponRack;
    WeaponRack *rack;
    u32 count;

    rack = ThingManagerList_GetByType(car + 0x344, 8);
    *(u32 *)(info + 0x10) = 100.0f * ((f32)*(s32 *)(car + 0x78) / (f32)*(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)) + 0x14));
    if (*(u32 *)(info + 0x10) > 100.0f) {
        *(u32 *)(info + 0x10) = 100;
    }
    if (rack != NULL && GETP(rack->slots[rack->current].item) != NULL) {
        if (GameSettings_IsCheatEnabled(0)) {
            rack->slots[rack->current].ammo = *(u32 *)(SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) + 0x28);
        }
        if (*(u32 *)(SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) + 0x24) != 0) {
            *(u32 *)(info + 0x38) = 100.0f * ((f32)rack->slots[rack->current].ammo / (f32)*(u32 *)(SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) + 0x28));
            *(u32 *)(info + 0x3C) = *(u32 *)(info + 0x38) / *(u32 *)(SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) + 0x24);
        } else {
            *(u32 *)(info + 0x38) = 0;
            *(u32 *)(info + 0x3C) = 0;
        }
        if (SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) != NULL) {
            *(u32 *)(info + 0x68) = *(u32 *)(SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) + 0x10);
        }
    }
    *(u32 *)(info + 0x14) = _fabs(0.681818f * *(f32 *)(car + 0x458));
    if (*(s8 *)(car + 0x584) && *(u32 *)(info + 0x14) < *(u32 *)(car + 0x58C)) {
        (*(u32 *)(car + 0x588))++;
        if (*(u32 *)(car + 0x588) > 5) {
            *(s8 *)(car + 0x584) = 0;
            *(u32 *)(car + 0x588) = 0;
            *(u32 *)(car + 0x58C) = *(u32 *)(info + 0x14);
        } else {
            *(u32 *)(info + 0x14) = *(u32 *)(car + 0x58C);
        }
    } else if (*(s8 *)(car + 0x584)) {
        *(u32 *)(car + 0x588) = 0;
        *(u32 *)(car + 0x58C) = *(u32 *)(info + 0x14);
    }
    if (!*(s8 *)(car + 0x584) && *(u32 *)(info + 0x14) > *(u32 *)(car + 0x58C)) {
        (*(u32 *)(car + 0x588))++;
        if (*(u32 *)(car + 0x588) > 5) {
            *(s8 *)(car + 0x584) = 1;
            *(u32 *)(car + 0x588) = 0;
            *(u32 *)(car + 0x58C) = *(u32 *)(info + 0x14);
        } else {
            *(u32 *)(info + 0x14) = *(u32 *)(car + 0x58C);
        }
    } else if (!*(s8 *)(car + 0x584)) {
        *(u32 *)(car + 0x588) = 0;
        *(u32 *)(car + 0x58C) = *(u32 *)(info + 0x14);
    }
    *(u32 *)(info + 0x18) = *(f32 *)(car + 0x4E4);
    *(f32 *)(info + 0x20) = *(s32 *)(car + 0x400) / 300;
    *(f32 *)(info + 0x2C) = 0.2f * (100.0f + *(f32 *)(car + 0x44));
    if (SLOT_LOAD(u8 *, (car + 0x94)) != NULL && (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x6C) & 0x200)) {
        if (GameSettings_GetCodeVariable(0x198, 0)) {
            count = CameraManager_GetActiveCameras();
        } else {
            count = GameSettings_GetNumOfPlayers();
        }
        if (count > 1) {
            if (count == 2) {
                *(u32 *)(info + 0x4) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x84)) + 0x18);
                if (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x64) == 1) {
                    *(s32 *)(info + 0x8) = 0;
                    *(s32 *)(info + 0xC) = 10;
                } else {
                    *(s32 *)(info + 0x8) = 0;
                    *(s32 *)(info + 0xC) = 230;
                }
            } else {
                *(u32 *)(info + 0x4) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x84)) + 0x1C);
                if (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x64) == 1) {
                    *(s32 *)(info + 0x8) = 10;
                    *(s32 *)(info + 0xC) = 10;
                } else if (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x64) == 2) {
                    *(s32 *)(info + 0x8) = 310;
                    *(s32 *)(info + 0xC) = 10;
                } else if (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x94)) + 0x64) == 3) {
                    *(s32 *)(info + 0x8) = 10;
                    *(s32 *)(info + 0xC) = 230;
                } else {
                    *(s32 *)(info + 0x8) = 310;
                    *(s32 *)(info + 0xC) = 230;
                }
            }
        } else {
            *(u32 *)(info + 0x4) = *(u32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0x84)) + 0x10);
            *(s32 *)(info + 0x8) = 0;
            *(s32 *)(info + 0xC) = 0;
        }
    }
    if (rack != NULL && rack->slots[rack->current].active != 0) {
        info[0x34] = 1;
        *(s16 *)(info + 0x6C) = *(u32 *)(SLOT_LOAD(u8 *, (GETP(rack->slots[rack->current].item) + 0x30)) + 0x18);
    }
    if (*(u32 *)(car + 0x494) != 0) {
        *(u32 *)(info + 0x44) = *(u32 *)(car + 0x494);
        *(u32 *)(car + 0x494) = 0;
    }
    if (*(u32 *)(car + 0x498) != 0) {
        *(u32 *)(info + 0x40) = *(u32 *)(car + 0x498);
        *(u32 *)(car + 0x498) = 0;
    }
    if (*(u32 *)(car + 0x49C) != 0) {
        *(u32 *)(info + 0x48) = *(u32 *)(car + 0x49C);
        *(u32 *)(car + 0x49C) = 0;
    }
    if (*(u32 *)(car + 0x4A0) != 0) {
        *(u32 *)(info + 0x4C) = *(u32 *)(car + 0x4A0);
        *(u32 *)(car + 0x4A0) = 0;
    }
}


void CarA_DynamicCollideImpact(void *thing, void *collision, s32 damage, f32 impulse) {
    typedef struct CollideSurface {
        u8 pad00[4];
        f32 minImpactA;
        f32 maxImpactA;
        f32 minImpactB;
        f32 maxImpactB;
        f32 bounce;
        f32 grip;
        u8 pad1C[0x44 - 0x1C];
        u32 numSoundsA;
        PTR32(u32) soundsA;
        u32 numSoundsB;
        PTR32(u32) soundsB;
    } CollideSurface;
    typedef struct CollideInfo {
        Vector4 pos;
        Vector4 normal;
        PTR32(CarA) thingA;
        PTR32(CarA) thingB;
        PTR32(CollideSurface) surface;
    } CollideInfo;
    typedef struct DropItem {
        Vector4 velocity;
        Matrix4 mtx;
        u8 pad50[0x24];
        u32 flags;
        u8 pad78[8];
        s16 gridCell;
        u8 bone;
        u8 pad83[9];
        PTR32(ThingVTable) vtable;
        u8 pad90[0x3C];
        s16 timer;
    } DropItem;
    typedef struct DeathInfo {
        u8 pad00[0x10];
        f32 height;
        u8 pad14[0x14];
        u32 sound;
    } DeathInfo;
    typedef struct DeathTile {
        u8 pad00[0x50];
        PTR32(DeathInfo) death;
    } DeathTile;
    typedef struct DeathParams {
        Vector4 quat;
        Vector4 pos;
        Vector4 vel;
        PTR32(DeathInfo) death;
        f32 unk34;
        u8 unk38;
    } DeathParams;
    typedef struct SpawnParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        PTR32(void) player;
        f32 unk48;
        f32 unk4C;
    } SpawnParams;
    extern u8 g_carAVTable[];
    extern u8 g_itemVTable[];
    extern u8 g_destructibleVTable[];
    extern u8 g_physicalObjectVTable[];
    extern Vector4 g_zeroVector;
    CollideInfo *coll = collision;
    CarA *car = thing;
    SpawnParams sp;
    SpawnParams snd;
    SpawnParams ex;
    Matrix4 m;
    DeathParams dp;
    Vector4 n __attribute__((aligned(16)));
    Vector4 p __attribute__((aligned(16)));
    Vector4 vel __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 itemPos __attribute__((aligned(16)));
    Vector4 rel __attribute__((aligned(16)));
    Vector4 refUp __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    Vector4 imp __attribute__((aligned(16)));
    Vector4 torque __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 c2 __attribute__((aligned(16)));
    Vector4 nn __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Vector4 w __attribute__((aligned(16)));
    f32 t;
    f32 d;
    f32 d2;
    f32 dist;
    f32 dv;
    f32 adv;
    f32 dur;
    f32 mag;
    f32 rem;
    f32 bounce;
    f32 bx;
    f32 by;
    f32 bz;
    f32 cosA;
    f32 a;
    f32 b;
    f32 c;
    f32 s;
    f32 tw;
    f32 sgn;
    f32 maxW;
    f32 mag2;
    f32 scale;
    f32 fb;
    DropItem *item;
    DropItem *rack;
    DeathInfo *death;
    CarA *other;
    void *spawned;
    Player *player;
    u32 id;
    u32 exId;
    s32 idx;

    n = coll->normal;
    p = coll->pos;
    vel = car->velocity;
    if (car->worldMatrix.v[1].y < 0.3f) {
        d = PSVECDotProduct(&car->worldMatrix.v[1], &n);
        d2 = PSVECDotProduct(&car->worldMatrix.v[1], &car->velocity);
        if (impulse < 0.0f) {
            d *= -1.0f;
        }
        if (d < -0.5f && d2 > 0.0f) {
            if (GETP(coll->thingB) == NULL || GETP(GETP(coll->thingA)->player) != GETP(GETP(coll->thingB)->player)) {
                CarA_StuntFail(car);
            }
            car->unk45C = 100;
            car->angularMomentum.x *= 0.5f;
            car->angularMomentum.y *= 0.5f;
            car->angularMomentum.z *= 0.5f;
            CarA_UpdateAngularVelocity(car);
            car->velocity.x *= 0.5f;
            car->velocity.y *= 0.5f;
            car->velocity.z *= 0.5f;
        }
        if (_fabs(PSVECDotProduct(&car->worldMatrix.v[0], &n)) > 0.7f) {
            car->unk45C = 100;
        }
        if (_fabs(PSVECDotProduct(&car->worldMatrix.v[2], &n)) > 0.7f) {
            if (car->tiltState == 0) {
                CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0], 1.0f - car->tiltY,
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
                car->tiltState = 3;
            }
        } else if (car->tiltState == 3) {
            CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0], GETP(GETP(car->vehicleIface)->engine)->bodyOffset[1],
                               GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
            car->tiltState = 0;
        }
        if (coll->normal.y > 0.7f && car->groundNormal.y < 0.5f) {
            car->groundNormal.x = 0.0f;
            car->groundNormal.y = 1.0f;
            car->groundNormal.z = 0.0f;
        }
        car->transmissionFlags &= ~0x80000;
        MotionState_ReleaseSlavesByType(HND(&car->motionState), 8);
    }
    if (GETP(coll->thingB) == NULL && PSVECDotProduct(&coll->normal, &car->worldMatrix.v[2]) < -0.7f) {
        Create_Plane(&plane, &car->worldMatrix.v[1], &car->worldMatrix.v[3]);
        dist = Distance_Plane(&plane, &p);
        if (dist < 5.0f && dist > 0.0f) {
            d = PSVECDotProduct(&n, &car->worldMatrix.v[1]);
            n.x = n.x - d * car->worldMatrix.v[1].x;
            n.y = n.y - d * car->worldMatrix.v[1].y;
            n.z = n.z - d * car->worldMatrix.v[1].z;
            dist *= 0.5f;
            p.x = p.x - car->worldMatrix.v[1].x * dist;
            p.y = p.y - car->worldMatrix.v[1].y * dist;
            p.z = p.z - car->worldMatrix.v[1].z * dist;
        }
    }
    damage -= GETP(GETP(car->vehicleIface)->collider)->damageThreshold;
    if (damage > 0 && car->damageRpm >= 10000) {
        CarA_ProcessDamageSystem(car, coll, damage, impulse);
        car->damageAccum += damage;
        if (car->damageAccum > 30) {
            while (car->damageAccum > 30) {
                car->speed -= 30;
                car->damageAccum -= 30;
            }
            if (GameSettings_IsCheatEnabled(6) && GETP(car->player) != NULL && Player_IsHuman(GETP(car->player))) {
                car->speed = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
            }
            CarA_UpdateDamageSlaves(car);
        }
    }
    if (GETP(coll->thingB) != NULL && (u8 *)GETP(GETP(coll->thingA)->vtable) == g_carAVTable && (u8 *)GETP(GETP(coll->thingB)->vtable) == g_carAVTable &&
        _fabs(impulse) > GETP(GETP(car->vehicleIface)->engine)->itemDropImpact) {
        item = ThingManagerList_GetByType(&car->motionState.slaves, 4);
        if (item != NULL) {
            t = 1.0f;
            rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
            rack->flags &= 0xFFBFFDFF;
            XformVec(&GETP(car->motionState.animMatrices)[item->bone], &item->mtx.v[3], &itemPos);
            if (!RayHitsTerrain(&car->worldMatrix.v[3], &itemPos, &t, NULL)) {
                Trigger_ItemDropped(car, item);
                ThingManagerList_Remove(&car->motionState.slaves, item);
                MultiplyMatrix(&item->mtx, &car->worldMatrix, &m);
                item->mtx = m;
                ThingManagerList_Append(&g_thingManager_DARYL.activeList, item);
                item->timer = 1000;
                item->flags &= 0xFFFBFFF5;
                item->velocity = car->velocity;
                item->velocity.y += 2.0f;
                if (item->gridCell != 0 && (u8 *)GETP(item->vtable) == g_itemVTable) {
                    Thing_AddToGrid(item, 3);
                }
            }
        }
    }
    dv = impulse / GETP(GETP(car->vehicleIface)->collider)->mass;
    adv = _fabs(dv);
    car->velocity.x += n.x * dv;
    car->velocity.y += n.y * dv;
    car->velocity.z += n.z * dv;
    if (GETP(car->player) != NULL && Player_IsHuman(GETP(car->player)) && adv > 0.0f) {
        mag = 0.01f * adv;
        player = GETP(car->player);
        dur = 0.3f;
        rem = mag;
        if (mag > 1.0f) {
            mag = 1.0f;
        }
        if (rem > 2.0f) {
            rem -= 1.0f;
            Input_SetForceFeedback(player->wheelChannel, 0, 1.0f, 0.1f);
        }
        while (rem > 1.0f) {
            rem *= 0.5f;
            dur += 0.2f;
            if (dur > 1.5f) {
                dur = 1.5f;
                break;
            }
        }
        Input_SetForceFeedback(player->wheelChannel, 1, mag, dur);
    }
    if (GETP(coll->surface) != NULL || GETP(coll->thingB) != NULL) {
        if (GETP(coll->surface) == NULL) {
            if (GETP(coll->thingA) == car) {
                SETP(coll->surface, GETP(GETP(GETP(coll->thingB)->vtable)->getSurface)(GETP(coll->thingB)));
            } else {
                SETP(coll->surface, GETP(GETP(GETP(coll->thingA)->vtable)->getSurface)(GETP(coll->thingA)));
            }
        }
        if (impulse > 0.0f) {
            bounce = 0.5f * -GETP(coll->surface)->bounce;
        } else {
            bounce = 0.5f * GETP(coll->surface)->bounce;
        }
        if (0.0f != impulse && PSVECDotProduct(&coll->normal, &car->worldMatrix.v[1]) < 0.5f) {
            bx = car->velocity.x * bounce;
            by = car->velocity.y * bounce;
            bz = car->velocity.z * bounce;
            car->velocity.x = car->velocity.x + bx;
            car->velocity.y = car->velocity.y + by;
            car->velocity.z = car->velocity.z + bz;
            n.x += 0.1f * bx;
            n.y += 0.1f * by;
            n.z += 0.1f * bz;
        }
        if (GETP(coll->thingB) == NULL && Gfx_GetMinCameraDistSquared(&car->worldMatrix.v[3]) < 250000.0f) {
            {
                Vector4 up = {0.0f, 1.0f, 0.0f, 0.0f};
                Vector4 side = {1.0f, 0.0f, 0.0f, 0.0f};

                refUp = up;
                axis = side;
            }
            id = 0;
            cosA = PSVECDotProduct(&coll->normal, &refUp);
            PSVECSubtract(&coll->pos, &car->worldMatrix.v[3], &rel);
            if (PSVECDotProduct(&car->worldMatrix.v[1], &rel) < 0.0f) {
                idx = (s32)((f32)GETP(coll->surface)->numSoundsA * ((dv - GETP(coll->surface)->minImpactA) / (GETP(coll->surface)->maxImpactA - GETP(coll->surface)->minImpactA)) - 1.0f);
                if (idx >= (s32)GETP(coll->surface)->numSoundsA) {
                    idx = GETP(coll->surface)->numSoundsA - 1;
                }
                if (idx >= 0 && GETP(coll->surface)->numSoundsA != 0) {
                    id = GETP(GETP(coll->surface)->soundsA)[idx];
                }
            } else {
                idx = (s32)((f32)GETP(coll->surface)->numSoundsB * ((dv - GETP(coll->surface)->minImpactB) / (GETP(coll->surface)->maxImpactB - GETP(coll->surface)->minImpactB)) - 1.0f);
                if (idx >= (s32)GETP(coll->surface)->numSoundsB) {
                    idx = GETP(coll->surface)->numSoundsB - 1;
                }
                if (idx >= 0 && GETP(coll->surface)->numSoundsB != 0) {
                    id = GETP(GETP(coll->surface)->soundsB)[idx];
                }
            }
            if (id != 0) {
                if (cosA < -0.99f) {
                    GenerateQuaternion(&axis, &sp.quat, 3.1415927f);
                } else if (cosA > 0.99f) {
                    sp.quat.x = sp.quat.y = sp.quat.z = 0.0f;
                    sp.quat.w = 1.0f;
                } else {
                    a = acos(cosA);
                    PSVECCrossProduct(&refUp, &coll->normal, &axis);
                    if (PSVECSquareMag(&axis)) {
                        PSVECNormalize(&axis, &axis);
                    }
                    GenerateQuaternion(&axis, &sp.quat, a);
                }
                sp.id = id;
                sp.pos = coll->pos;
                sp.vel = car->velocity;
                SETP(sp.player, GETP(car->player));
                sp.unk48 = 0.0f;
                sp.unk4C = 0.0f;
                sp.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &sp);
            }
        }
    }
    PSVECSubtract(&p, &car->worldMatrix.v[3], &r);
    imp.x = impulse * n.x;
    imp.y = impulse * n.y;
    imp.z = impulse * n.z;
    PSVECCrossProduct(&r, &imp, &torque);
    a = PSVECDotProduct(&r, &car->worldMatrix.v[0]);
    PSVECCrossProduct(&imp, &r, &c2);
    b = PSVECDotProduct(&c2, &car->worldMatrix.v[1]);
    c = PSVECDotProduct(&r, &car->worldMatrix.v[2]);
    if (_fabs(PSVECDotProduct(&coll->normal, &car->worldMatrix.v[1])) < 0.3f && c > 0.0f && car->numWheelsGrounded != 0 &&
        a * b < 0.0f && PSVECDotProduct(&vel, &car->worldMatrix.v[2]) > _fabs(PSVECDotProduct(&vel, &car->worldMatrix.v[0]))) {
        s = PSVECDotProduct(&car->worldMatrix.v[2], &car->velocity);
        tw = PSVECDotProduct(&car->worldMatrix.v[1], &torque);
        if (GETP(coll->thingB) == NULL ||
            (((CollideSurface *)GETP(GETP(GETP(coll->thingA)->vtable)->getSurface)(GETP(coll->thingA)))->grip > 0.0f &&
             ((CollideSurface *)GETP(GETP(GETP(coll->thingB)->vtable)->getSurface)(GETP(coll->thingB)))->grip > 0.0f)) {
            if (GETP(car->player) != NULL && GETP(car->player)->throttle == 0) {
                car->transmissionFlags |= 0x10;
            } else {
                car->unk4FC = 0.0f;
            }
        }
        if (GETP(coll->thingB) != NULL && (!(GETP(coll->thingA)->collDirtyFlags & 8) || GETP(coll->thingA)->speed == 0) &&
            (!(GETP(coll->thingB)->collDirtyFlags & 8) || GETP(coll->thingB)->speed == 0)) {
            ProjectVectorToNormal(&car->worldMatrix.v[1], &torque);
        } else {
            sgn = 1.0f;
            torque.x = torque.x - tw * car->worldMatrix.v[1].x;
            torque.y = torque.y - tw * car->worldMatrix.v[1].y;
            torque.z = torque.z - tw * car->worldMatrix.v[1].z;
            if (tw < 0.0f) {
                sgn = -1.0f;
                tw *= sgn;
            }
            if (tw < 50.0f) {
                tw = 50.0f;
            }
            if (tw > 500.0f) {
                tw = 500.0f;
            }
            tw *= sgn;
            torque.x = torque.x - tw * car->worldMatrix.v[1].x;
            torque.y = torque.y - tw * car->worldMatrix.v[1].y;
            torque.z = torque.z - tw * car->worldMatrix.v[1].z;
        }
        if (GETP(coll->thingA) == car) {
            if (s < 0.0f) {
                ProjectVectorToNormal(&car->velocity, &n);
            }
        } else if (s > 0.0f) {
            ProjectVectorToNormal(&car->velocity, &n);
        }
    } else if (_fabs(impulse) > 10.0f && GETP(coll->thingB) == NULL && car->numWheelsGrounded != 0) {
        if (_fabs(PSVECDotProduct(&car->worldMatrix.v[1], &coll->normal)) < 0.3f) {
            if (_fabs(PSVECDotProduct(&car->worldMatrix.v[0], &coll->normal)) < 0.3f && c > 0.0f) {
                car->transmissionFlags |= 0x2000;
            }
        }
    }
    car->angularMomentum.x += torque.x;
    car->angularMomentum.y += torque.y;
    car->angularMomentum.z += torque.z;
    if (GETP(car->player) != NULL && Player_IsHuman(GETP(car->player)) && !(car->transmissionFlags & 0x10000)) {
        nn = coll->normal;
        fb = 255.0f * _fabs(impulse / 1000.0f);
        ProjectVectorToNormal(&car->worldMatrix.v[0], &nn);
        fb = fb * MagnitudeVector(&nn);
        if (fb > 20.0f) {
            player = GETP(car->player);
            Wheel_StopCarAirborne(player->wheelChannel);
            Wheel_PlayFrontalCollisionForce(player->wheelChannel, fb <= 255.0f ? fb : 255.0f);
            car->transmissionFlags |= 0x10000;
        }
    }
    if (car->numWheelsGrounded <= 1) {
        maxW = GETP(GETP(car->vehicleIface)->engine)->maxAngularVel;
        InverseXformVecOrientation(&car->worldMatrix, &car->angularMomentum, &local);
        w.x = car->inverseBodyInertia.v[0].x * local.x;
        w.y = car->inverseBodyInertia.v[1].y * local.y;
        w.z = car->inverseBodyInertia.v[2].z * local.z;
        mag2 = PSVECSquareMag(&w);
        if (mag2 > maxW * maxW) {
            scale = maxW / Sqrt(mag2);
            w.x *= scale;
            w.y *= scale;
            w.z *= scale;
            local.x = w.x / car->inverseBodyInertia.v[0].x;
            local.y = w.y / car->inverseBodyInertia.v[1].y;
            local.z = w.z / car->inverseBodyInertia.v[2].z;
            XformVecOrientation(&car->worldMatrix, &local, &car->angularMomentum);
        }
    }
    CarA_UpdateAngularVelocity(car);
    if (car->speed <= 0) {
        if (GETP(car->tileData) != NULL && (death = GETP(((DeathTile *)GETP(car->tileData))->death)) != NULL) {
            dp.pos = car->worldMatrix.v[3];
            dp.pos.y += death->height;
            dp.vel.x = 0.0f;
            dp.vel.y = 0.0f;
            dp.vel.z = 0.0f;
            SETP(dp.death, death);
            dp.quat.w = 1.0f;
            dp.quat.x = 0.0f;
            dp.quat.y = 0.0f;
            dp.quat.z = 0.0f;
            dp.unk34 = 0.0f;
            dp.unk38 = 0;
            spawned = ThingManager_DARYL_CreateThing(4, &dp);
            if (spawned != NULL) {
                ((CarA *)spawned)->collDirtyFlags |= 1;
                if (death->sound != 0) {
                    snd.id = death->sound;
                    snd.pos = car->worldMatrix.v[3];
                    snd.quat = dp.quat;
                    snd.vel.z = 0.0f;
                    snd.vel.y = 0.0f;
                    snd.vel.x = 0.0f;
                    SETP(snd.player, NULL);
                    snd.unk48 = 0.0f;
                    snd.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &snd);
                }
            }
        }
        if (GETP(coll->thingA) == car) {
            other = GETP(coll->thingB);
        } else {
            other = GETP(coll->thingA);
        }
        if (GETP(car->player) != NULL) {
            if (other != NULL && GETP(other->player) != NULL && Player_IsHuman(GETP(other->player))) {
                GETP(car->player)->flags |= 0x40;
            } else {
                GETP(car->player)->flags &= ~0x40;
            }
        }
        if (!Player_IsHuman(GETP(car->player))) {
            exId = GETP(car->vehicleIface)->explosionThing;
            if (exId != 0) {
                ex.id = exId;
                ex.pos = car->worldMatrix.v[3];
                ex.quat = car->animRefPos;
                ex.vel = g_zeroVector;
                SETP(ex.player, GETP(car->player));
                ex.unk48 = 0.0f;
                ex.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &ex);
            }
        }
        Thing_ProcessEndEvent(car, GETP(GETP(car->vehicleIface)->collider), coll, &car->localAxis);
        if (other != NULL && GETP(other->player) != NULL) {
            GETP(other->player)->kills++;
        }
        CarA_Release((u8 *)car);
    }
    if (GETP(coll->thingB) == NULL || (u8 *)GETP(GETP(coll->thingA)->vtable) == g_destructibleVTable ||
        (u8 *)GETP(GETP(coll->thingA)->vtable) == g_physicalObjectVTable || (u8 *)GETP(GETP(coll->thingB)->vtable) == g_destructibleVTable ||
        (u8 *)GETP(GETP(coll->thingB)->vtable) == g_physicalObjectVTable) {
        car->transmissionFlags &= ~2;
        MotionState_ReleaseSlavesByType(HND(&car->motionState), 4);
        ThingManagerList_RemoveReleasedThings(&car->motionState.slaves);
    }
    car->lowSpeedFlag = 0;
}

void *CarA_GetOrientedCollision(u8 *car)
{
    u8 *coll;

    coll = SLOT_LOAD(u8 *, (car + 0x3BC));
    coll = SLOT_LOAD(u8 *, (coll + 0xA0));
    switch (*(u8 *)(coll + 0x10))
    {
    case 0:
        if (*(u32 *)(car + 0x74) & 0x180)
        {
            BuildOOBOffset(SLOT_LOAD(u8 *, (coll + 0x44)), car + 0x10, car + 0x520, car + 0xC0);
            *(u32 *)(car + 0x74) &= ~0x180;
        }
        return car + 0xC0;
    case 1:
        BuildOConvexHullOffset(SLOT_LOAD(u8 *, (car + 0x370)), SLOT_LOAD(u8 *, (coll + 0x48)), car + 0x10, car + 0x520);
        return SLOT_LOAD(u8 *, (car + 0x370));
    }
    return NULL;
}

s32 CarA_GetSurfaceScript(u8 *car)
{
    u8 *coll;

    coll = SLOT_LOAD(u8 *, (car + 0x3BC));
    coll = SLOT_LOAD(u8 *, (coll + 0xA0));
    return *(s32 *)(coll + 0x30);
}

void CarA_GetCameraOrientationInfo(CarA *car, u32 index, void *out) {
    typedef struct CameraOrientationInfo {
        u8 pad00[0x30];
        Vector4 up;
        u8 pad40[0x60 - 0x40];
        Vector4 pos;
        Vector4 target;
        u8 pad80[0x84 - 0x80];
        PTR32(u8) camera;
        u32 numCameras;
        f32 unk8C;
    } CameraOrientationInfo;
    typedef struct CarCameras {
        u32 count;
        PTR32(u8) list;
    } CarCameras;
    CameraOrientationInfo *info = out;
    CarCameras *cams;
    Vector4 offset __attribute__((aligned(16)));

    cams = (CarCameras *)((u8 *)GETP(GETP(car->vehicleIface)->engine) + 0x80);
    if (ViewManager_GetViewFormat() > 1) {
        cams = (CarCameras *)((u8 *)GETP(GETP(car->vehicleIface)->engine) + 0x88);
    }
    info->numCameras = cams->count;
    if (cams->count == 0) {
        Thing_GetCameraOrientationInfo(car, index, info);
    } else {
        SETP(info->camera, GETP(cams->list) + (index % cams->count) * 0xC);
        info->unk8C = car->groundedScale;
        info->up = car->groundNormal;
        if (info->up.x && info->up.y && info->up.z && PSVECSquareMag(&info->up)) {
            PSVECNormalize(&info->up, &info->up);
        }
    }
    if (car->slaveWeaponRpm[0] < 10000) {
        info->unk8C = 0.0f;
    }
    PSVECSubtract(&car->bodyOffset, (Vector4 *)GETP(GETP(car->vehicleIface)->engine), &offset);
    XformVec(&car->worldMatrix, &offset, &info->pos);
    switch (GETP(GETP(car->vehicleIface)->collider)->type) {
    case 0:
        info->target = *(Vector4 *)CarA_GetOrientedCollision((u8 *)car);
        break;
    case 1:
        break;
    }
}

s32 CarA_GetNearestNavPoint(CarA *car) {
    s32 timer;

    if (car->navPointIndex == 0xFFFFFFFF) {
        car->navPointIndex = NavHolder_FindIndexOfNearestNavPoint(&car->worldMatrix.v[3], -1);
        timer = RandomAdd_s32(300, 100);
        car->navUpdateTimer = timer;
        car->navUpdateTimer = timer;
    }
    return car->navPointIndex;
}

void CarA_CollideWithRay(CarA *car, void *start, void *end, struct RayHit { u8 pad0[0x30]; Vector4 normal; } *hit, f32 maxDist) {
    Matrix4 local;
    f32 dist;

    local = car->worldMatrix;
    XformVec(&car->worldMatrix, &car->bodyOffset, &local.v[3]);
    dist = PhysicalProperties_CollideWithRay(GETP(GETP(car->vehicleIface)->collider), &local, start, end, maxDist, hit);
    if (hit != NULL && dist < maxDist) {
        hit->normal = car->velocity;
    }
}


s8 CarA_ProcessItemPickup(CarA *car, void *thing) {
    typedef struct PickupDef {
        u8 type;
        u8 pad1;
        u16 value;
        u8 pad4[0x10 - 0x4];
        f32 height;
        u8 pad14[0x20 - 0x14];
        u32 id;
    } PickupDef;
    typedef struct PickupSpawner {
        u8 pad0[0x14];
        s32 respawnTime;
        s32 timer;
    } PickupSpawner;
    typedef struct PickupItem {
        Vector4 velocity;
        Matrix4 mtx;
        Vector4 quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0x82 - 0x78];
        u8 bone;
        u8 unk83;
        PTR32(void) gridObj;
        u8 pad88[0x90 - 0x88];
        PTR32(void) tileData;
        PTR32(Player) player;
        u8 pad98[0xC0 - 0x98];
        PTR32(PickupDef) def;
        u8 padC4[0xCE - 0xC4];
        s16 count;
        PTR32(PickupSpawner) spawner;
    } PickupItem;
    typedef struct SlotAnim {
        u8 pad0[0x1E];
        u8 loop;
        u8 pad1F[0x24 - 0x1F];
        PTR32(f32) times;
        PTR32(f32) loopTimes;
    } SlotAnim;
    typedef struct SlotData {
        u8 pad0[0x28];
        s32 max;
    } SlotData;
    typedef struct SlotThing {
        u8 pad0[0x30];
        PTR32(SlotData) data;
    } SlotThing;
    typedef struct RackSlot {
        PTR32(SlotAnim) anim;
        u8 pad4[0x1C - 0x4];
        u8 mode;
        u8 state;
        u8 pad1E[0x20 - 0x1E];
        f32 time;
        u8 pad24[0x40 - 0x24];
        s16 count;
        u8 pad42[0x44 - 0x42];
        f32 scale;
        u8 pad48[0x4C - 0x48];
        PTR32(SlotThing) thing;
    } RackSlot;
    typedef struct PickupRack {
        u8 pad0[0x40];
        Vector4 pos;
        u8 pad50[0x74 - 0x50];
        u32 flags;
        u8 pad78[0x82 - 0x78];
        u8 bone;
        u8 pad83[0xB0 - 0x83];
        RackSlot slots[12];
        u8 current;
    } PickupRack;
    extern Vector4 g_zeroVector;
    extern char stunt_msg_buffer[];
    extern char *g_stuntLossString;
    Vector4 pos __attribute__((aligned(16)));
    u8 current;
    PickupRack *rack;
    s8 picked;
    PickupRack *wcRack;
    u32 points;
    PickupItem *item = thing;
    u32 i;
    s16 ammo;

    picked = 0;
    Trigger_ItemPickedUp(car, item);
    car->pickupId = GETP(item->def)->id;
    switch (GETP(item->def)->type) {
    case 1:
    case 2:
    case 3:
    case 6:
        rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
        if (rack != NULL) {
            WildCardRack_Add(rack, GETP(item->def), item->count);
            Item_Release(item);
            if (GETP(item->spawner) != NULL) {
                GETP(item->spawner)->timer = GETP(item->spawner)->respawnTime;
            }
            picked = 1;
        }
        break;
    case 7:
        if (ThingManagerList_GetByType(&car->motionState.slaves, 4) == NULL) {
            rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
            pos.x = rack->pos.x;
            pos.y = GETP(GETP(car->vehicleIface)->engine)->itemHeight + (rack->pos.y + GETP(item->def)->height);
            pos.z = rack->pos.z;
            pos.w = 1.0f;
            ThingManagerList_Remove(&g_thingManager_DARYL.activeList, item);
            ThingManagerList_Append(&car->motionState.slaves, item);
            item->bone = rack->bone;
            item->unk83 = 0xEF;
            item->quat.w = 1.0f;
            item->quat.x = 0.0f;
            item->quat.y = 0.0f;
            item->quat.z = 0.0f;
            QuaternionToSubMatrix(&item->quat, &item->mtx);
            item->mtx.v[3] = pos;
            item->mtx.v[3].w = 1.0f;
            item->velocity = g_zeroVector;
            Grid_FreeObject(GETP(item->gridObj));
            SETP(item->player, GETP(car->player));
            SETP(item->gridObj, NULL);
            picked = 1;
            SETP(item->tileData, GETP(item->spawner));
            rack->flags |= 0x400200;
            item->flags |= 8;
        }
        break;
    case 8:
        if (GETP(car->vehicleIface)->numPickupSlaves > GETP(item->def)->value) {
            MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->pickupSlaves) + GETP(item->def)->value * 12 + 4,
                                         GETP(item->def)->value);
        }
        Item_Release(item);
        if (GETP(item->spawner) != NULL) {
            GETP(item->spawner)->timer = GETP(item->spawner)->respawnTime;
        }
        picked = 1;
        break;
    case 9:
        car->pickupCount++;
        Item_Release(item);
        if (GETP(item->spawner) != NULL) {
            GETP(item->spawner)->timer = GETP(item->spawner)->respawnTime;
        }
        picked = 1;
        break;
    case 10:
        if (Player_IsHuman(GETP(car->player))) {
            Item_Release(item);
            if (GETP(item->spawner) != NULL) {
                GETP(item->spawner)->timer = GETP(item->spawner)->respawnTime;
            }
            picked = 1;
        }
        break;
    case 11:
        if (GETP(car->player) != NULL) {
            if (Player_IsHuman(GETP(car->player))) {
                wcRack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
                current = wcRack->current;
                points = GETP(item->def)->value;
                GETP(car->player)->stuntScored = 1;
                GETP(car->player)->stuntPoints += points;
                if (GETP(car->player)->stuntPoints > 99999) {
                    GETP(car->player)->stuntPoints = 99999;
                }
                sprintf(stunt_msg_buffer, "Points +%d", points);
                SETP(car->stuntPopup, &g_stuntLossString);
                car->turboTime += (s32)(points * GETP(GETP(car->vehicleIface)->stunt)->turboPerPoint);
                if (car->turboTime > 30000) {
                    car->turboTime = 30000;
                }
                ammo = points;
                for (i = 0; i < 12; i++) {
                    RackSlot *slot = &wcRack->slots[i];

                    if (GETP(slot->thing) != NULL) {
                        if (slot->count < (s16)GETP(GETP(slot->thing)->data)->max) {
                            if (GETP(slot->anim)->loop) {
                                slot->time = GETP(GETP(slot->anim)->loopTimes)[0];
                                slot->mode = 2;
                            } else {
                                slot->time = GETP(GETP(slot->anim)->times)[1];
                            }
                            slot->state = 1;
                        }
                        slot->count += ammo;
                        if (slot->count > (s16)GETP(GETP(slot->thing)->data)->max) {
                            slot->count = GETP(GETP(slot->thing)->data)->max;
                        }
                    }
                }
                wcRack->current = current;
                wcRack->slots[current].scale = 1.0f;
            }
            Item_Release(item);
            if (GETP(item->spawner) != NULL) {
                GETP(item->spawner)->timer = GETP(item->spawner)->respawnTime;
            }
            picked = 1;
        }
        break;
    }
    return picked;
}

s32 CarA_Invalidate(void *arg0, u32 arg1, u32 arg2) {
    u32 temp_r0;

    temp_r0 = M2C_FIELD(arg0, u32 *, 0x3BC);
    if ((temp_r0 >= arg1) && (temp_r0 < arg2)) {
        return 1;
    }
    return 0;
}