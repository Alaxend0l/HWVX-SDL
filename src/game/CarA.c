#include "CarA.h"
#include "AI.h"
#include "common.h"
#include "Interface.h"
#include "WildCard.h"


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
    XformVecOrientation(THING_MTX(&car->base), &delta, &world);
    car->base.pos.x -= world.x;
    car->base.pos.y -= world.y;
    car->base.pos.z -= world.z;
    car->bodyOffset.x = x;
    car->bodyOffset.y = y;
    car->bodyOffset.z = z;
    car->base.flags |= 0x100;
}

/* Angular velocity (CarA.localAxis) from angular momentum and the inverse
   body inertia tensor. */
static inline void CarA_UpdateAngularVelocity(CarA *car) {
    Vector4 local __attribute__((aligned(16)));
    Vector4 scaled __attribute__((aligned(16)));

    InverseXformVecOrientation(THING_MTX(&car->base), &car->angularMomentum, &local);
    scaled.x = car->inverseBodyInertia.v[0].x * local.x;
    scaled.y = car->inverseBodyInertia.v[1].y * local.y;
    scaled.z = car->inverseBodyInertia.v[2].z * local.z;
    XformVecOrientation(THING_MTX(&car->base), &scaled, &car->localAxis);
}

static inline Vector4 Vector4_OrZero(Vector4 *v) {
    extern Vector4 g_zeroVector;

    if (v != NULL) {
        return *v;
    }
    return g_zeroVector;
}

/* Slave set i of the car's description (CarASlaveSet). */
static inline CarASlaveSet *CarA_SlaveSet(CarA *car, u32 i) {
    return &((CarASlaveSet *)GETP(GETP(car->vehicleIface)->slaves))[i];
}

/* Releases the damage-state slaves (set 2) and re-triggers the ones whose
   health threshold the car has dropped below. */
static inline void CarA_UpdateDamageSlaves(CarA *car) {
    extern Vector4 g_zeroVector;
    MotionState *ms;
    u32 i;
    CarASlaveSet *set;
    f32 frac;

    if (GETP(car->vehicleIface)->numSlaves >= 2) {
        ms = &car->motionState;
        set = CarA_SlaveSet(car, 2);
        frac = 100.0f * (f32)car->base.health / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth;
        MotionState_ReleaseSlavesByType(ms, 2);
        for (i = 0; i < set->count; i++) {
            Vector4 color = {0.0f, 0.0f, 0.0f, 0.0f};

            /* the original tests slaves + i != NULL; spelled out, as compilers
             * may assume &a[i] is never NULL */
            if ((GETP(set->slaves) != NULL || i != 0) && frac < GETP(set->slaves)[i].threshold) {
                ThingManagerList_TriggerSlaves(&ms->slaves, &GETP(set->slaves)[i], &GETP(set->slaves)[i], &color, &g_zeroVector, 2);
            }
        }
    }
}

/* The car's pose: the blended MotionState key frames, wheels spun by their
   rotation, then the steering and the three additive animations on top. */
void CarA_Extract_Keyframes(CarA *car, AnimKeyFrame *frames) {
    u32 *numBones;
    u32 i;
    CarTrailBone *wheel;
    AnimKeyFrame keys[64];

    numBones = &GETP(GETP(car->motionState.model)->skeleton)->numBones;
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

/* Inverse inertia of a box around the collider (box corners or hull
   bounds, shifted by the body offset), scaled per axis by the collider. */
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

/* Angular velocity, as PhysicalObject callers ask for it. */
void CarA_GetAngularVelocity(CarA *car, Vector4 *vel) {
    *vel = car->localAxis;
}

/* The cubic through (x0, y0) and (x1, y1) with slopes m0 and m1 there:
   coef[0..3] = a, b, c, d of a*x^3 + b*x^2 + c*x + d. */
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

/* Vtable Init: sets the car up from its description (CarAParams.info):
   transform, collider bounds, model, wheels, torque curve, sounds and the
   slave things it starts with. */
s32 CarA_Init(void *thing, void *arg1) {
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    extern void *g_pSourcePlayer;
    struct SlaveParams {
        Vector4 pos;
        Vector4 quat;
        PTR32(CarAStartSlave) info;
    } params;
    Vector4 pos;
    u8 frames[0x800];
    u32 sample;
    f32 volume;
    CarACollider *coll;
    Vector4 *box;
    CarAHull *hull;
    CarAEngine *engine;
    CarASoundSet *snd;
    Thing *rack;
    Thing *slave;
    u32 *skel;
    u32 i;
    CarAParams *init = arg1;
    CarA *car = thing;

    Thing_Init(&car->base, (s32)HND(init));
    car->base.player.h = init->player.h;
    SETP(car->vehicleIface, GETP(init->info));
    car->base.pos = init->pos;
    QuaternionToSubMatrix(&init->quat, THING_MTX(&car->base));
    car->base.quat = init->quat;
    SETP(car->hull, NULL);
    coll = GETP(GETP(car->vehicleIface)->collider);
    switch (coll->type) {
    case 0:
        box = GETP(coll->box);
        car->colliderMin = box[0];
        car->base.radius = box[2].x;
        car->base.collisionType = THING_COLLIDE_OBOX;
        break;
    case 1:
        hull = GETP(coll->hull);
        car->colliderMin.x = 9999.9f;
        car->colliderMin.y = 9999.9f;
        car->colliderMin.z = 9999.9f;
        car->base.radius = 9999.9f;
        for (i = 0; i < hull->numVerts; i++) {
            if (car->colliderMin.x > GETP(hull->verts)[i].x) {
                car->colliderMin.x = GETP(hull->verts)[i].x;
            }
            if (car->colliderMin.y > GETP(hull->verts)[i].y) {
                car->colliderMin.y = GETP(hull->verts)[i].y;
            }
            if (car->colliderMin.z > GETP(hull->verts)[i].z) {
                car->colliderMin.z = GETP(hull->verts)[i].z;
            }
        }
        SETP(car->hull, OConvexHullHolder_Get(GETP(GETP(GETP(car->vehicleIface)->collider)->hull)));
        car->base.radius = 13.0f;
        car->base.collisionType = 4;
        break;
    }
    car->base.health = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
    car->gripScale = *GETP(GETP(car->vehicleIface)->gripScale);
    car->turboTime = -1;
    car->transmissionFlags = 0;
    car->motionState.model.h = GETP(GETP(car->vehicleIface)->modelRef)->data.h;
    car->motionState.layer.blend = 0.0f;
    MotionState_Init(&car->motionState, 0);
    car->tiltState = 0;
    engine = GETP(GETP(car->vehicleIface)->engine);
    car->bodyOffset = *(Vector4 *)engine->bodyOffset;
    GETP(GETP(car->vehicleIface)->engine)->unk0C = 1.0f;
    skel = &GETP(GETP(car->motionState.model)->skeleton)->numBones;
    CarA_Extract_Keyframes(car, frames);
    XformVec(THING_MTX(&car->base), &car->bodyOffset, &pos);
    TEST_AnimTraverseHeirarchyToMatricesWorld(&car->base.quat, &pos, frames, *skel, GETP(car->motionState.animMatrices), 1.0f);
    if (GETP(car->vehicleIface)->numSlaves >= 1) {
        g_pSourcePlayer = GETP(car->base.player);
        MotionState_InitAllSlavesNow(&car->motionState, &CarA_SlaveSet(car, 0)->count, 0);
        g_pSourcePlayer = NULL;
    }
    rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
    if (rack != NULL) {
        rack->player.h = car->base.player.h;
        for (i = 0; i < GETP(car->vehicleIface)->numStartItems; i++) {
            ItemRecord *item = GETP(GETP(GETP(car->vehicleIface)->startItems)[i]);

            if (item != NULL) {
                WildCardRack_Add(rack, (u8 *)item, GETP(item->weapon)->ammo);
            }
        }
    }
    for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
        car->wheelStates[i].rotation = 0.0f;
        car->wheelStates[i].skidTrail = 0;
        car->wheelStates[i].dustTimer = 0;
        car->wheelStates[i].sparkTimer = 0;
        car->wheelStates[i].skidColor.a = 0;
        car->wheelStates[i].lastCompression = 0.0f;
        car->wheelStates[i].compression = 0.0f;
        car->wheelStates[i].slipVel = g_zeroVector;
    }
    car->gradeOrReverse = 0.0f;
    car->unk4E0 = 0.0f;
    car->rpm = 0.0f;
    car->curveTableIdx = 1;
    engine = GETP(GETP(car->vehicleIface)->engine);
    if (GETP(car->vehicleIface)->driveMode == 2) {
        CarA_FitCubic(car->torquePolyCoeffs, 0.0f, GETP(car->vehicleIface)->unk1C, engine->torqueCurve[0], 0.0f, 1e-7f, 0.0f);
    } else {
        CarA_FitCubic(car->torquePolyCoeffs, 0.0f, GETP(car->vehicleIface)->unk1C, engine->torqueCurve[0],
                      engine->torqueCurve[1], engine->torqueCurve[2], engine->torqueCurve[3]);
    }
    sample = 0;
    volume = 0.0f;
    /* wind */
    snd = SLOT_LOAD(CarASoundSet *, &GETP(GETP(car->vehicleIface)->gears)[1].unk4);
    car->soundIds[0] = -1;
    if (snd != NULL) {
        car->soundIds[0] = SoundManager_CreateSource(1);
        sample = snd->sample;
        SoundSource_Set(car->soundIds[0], 8, &sample);
        SoundSource_Set(car->soundIds[0], 5, &car->base.pos);
        if (snd->type == 0) {
            SoundSource_Set(car->soundIds[0], 2, GETP(snd->range));
            SoundSource_Set(car->soundIds[0], 3, GETP(snd->range) + 1);
        }
        SoundSource_Play(car->soundIds[0]);
    }
    /* engine */
    car->soundIds[1] = -1;
    if (GETP(GETP(car->vehicleIface)->soundSets) != NULL && (snd = GETP(GETP(GETP(car->vehicleIface)->soundSets)[1])) != NULL) {
        car->soundIds[1] = SoundManager_CreateSource(1);
        sample = snd->sample;
        SoundSource_Set(car->soundIds[1], 8, &sample);
        SoundSource_Set(car->soundIds[1], 5, &car->base.pos);
        if (snd->type == 0) {
            SoundSource_Set(car->soundIds[1], 2, GETP(snd->range));
            SoundSource_Set(car->soundIds[1], 3, GETP(snd->range) + 1);
        }
        SoundSource_Play(car->soundIds[1]);
    }
    /* rolling, silent until the wheels touch a surface */
    snd = GETP(GETP(car->vehicleIface)->rollSound);
    car->soundIds[2] = -1;
    if (snd != NULL) {
        car->soundIds[2] = SoundManager_CreateSource(1);
        SoundSource_Set(car->soundIds[2], 5, &car->base.pos);
        SoundSource_Set(car->soundIds[2], 0, &volume);
        if (GETP(GETP(car->vehicleIface)->rollSound)->type == 0) {
            SoundSource_Set(car->soundIds[2], 2, GETP(snd->range));
            SoundSource_Set(car->soundIds[2], 3, GETP(snd->range) + 1);
        }
        SoundSource_Play(car->soundIds[2]);
    }
    car->turboCooldown = -1;
    car->torqueAccum = g_zeroVector;
    car->forceAccum = car->torqueAccum;
    car->damageAccum = 0;
    car->partVisMask = 0;
    car->unk460 = 0;
    car->unk45C = 0;
    car->stuckCounter = 0;
    car->lowSpeedFlag = 0;
    car->inverseBodyInertia = g_identity;
    car->groundedScale = 0.0f;
    car->pickupCount = 0;
    car->stuntPopup.h = 0;
    car->pickupId = 0;
    car->stuntScoreMessage.h = 0;
    car->stuntMessage.h = 0;
    car->stuntSpin.x = 0.0f;
    car->stuntSpin.y = 0.0f;
    car->stuntSpin.z = 0.0f;
    car->stuntAxis = 0;
    car->reverseTimer = 99999;
    for (i = 0; i < 8; i++) {
        car->slaveWeaponRpm[i] = 10001;
    }
    car->groundedTime = 0;
    car->forwardAccel = 0.0f;
    car->orientedBox.inUse = 1;     /* the box is the car's own, not from the pool */
    car->stuntTime = 0.0f;
    CarA_InitInverseBodyInertiaTensor(car);
    car->angularMomentum = g_zeroVector;
    car->localAxis = g_zeroVector;
    car->rollSurface.h = 0;
    car->navUpdateTimer = 0;
    car->navPointIndex = -1;
    car->base.flags |= 4;
    car->base.flags |= 0x80;
    car->path[0].h = 0;
    car->navTimer = 0;
    Thing_AddToGrid(car, 0);
    for (i = 0; i < GETP(car->vehicleIface)->numStartSlaves; i++) {
        params.quat = params.pos = g_zeroVector;
        SETP(params.info, &GETP(GETP(car->vehicleIface)->startSlaves)[i]);
        if (GETP(params.info) != NULL && GETP(params.info)->enabled != 0) {
            slave = ThingManager_DARYL_CreateSlave(0xD, &params, GETP(params.info)->tag);
            slave->player.h = car->base.player.h;
            ThingManagerList_Append(&car->motionState.slaves, slave);
        }
    }
    car->unk4FC = 1.0f;
    return 1;
}

/* Vtable Release. With code variable 0x12D >= 3 the driver remembers where
   the car died (deathNavPoint) and where on its path to respawn. */
void CarA_Release(CarA *car) {
    u16 i;

    if (GameSettings_GetCodeVariable(0x12D, 0) >= 3 && GETP(car->base.player) != NULL) {
        CARA_PLAYER(car)->deathNavPoint = car->navPointIndex;
        for (i = 0; i < 6; i++) {
            if (GETP(car->path[i]) == NULL || GETP(car->path[i + 1]) == NULL) {
                break;
            }
            if (NavHolder_GetNavPoint(car->navPointIndex) == GETP(car->path[i])) {
                CARA_PLAYER(car)->respawnNavPoint = GETP(car->path[i + 1])->index;
                break;
            }
        }
    }
    Trigger_VehicleKilled(car);
    OConvexHullHolder_Release(GETP(car->hull));
    SETP(car->hull, NULL);
    if ((u32)car->soundIds[0] != 0xFFFFFFFF) {
        SoundSource_Stop(car->soundIds[0]);
    }
    if ((u32)car->soundIds[1] != 0xFFFFFFFF) {
        SoundSource_Stop(car->soundIds[1]);
    }
    if ((u32)car->soundIds[2] != 0xFFFFFFFF) {
        SoundSource_Stop(car->soundIds[2]);
    }
    car->soundIds[0] = -1;
    car->soundIds[1] = -1;
    car->soundIds[2] = -1;
    MotionState_Release(&car->motionState);
    Thing_Release(car);
}

/* Vtable GetInvInertiaTensor: the diagonal of the inverse body inertia. */
void CarA_GetInverseLocalInertiaTensor(CarA *car, Vector4 *out) {
    out->x = car->inverseBodyInertia.v[0].x;
    out->y = car->inverseBodyInertia.v[1].y;
    out->z = car->inverseBodyInertia.v[2].z;
}

/* Engine torque at the current rpm (the cubic torque curve), divided by the
   gear ratio. Halved while slave weapon 1 is active; 4WD turbo rescales. */
f32 CarA_Transmission_GetTorqueAtWheels(CarA *car)
{
    CarAInfo *trans;
    f32 scale;
    f32 rpm;
    f32 d;
    f32 c;
    f32 b;
    f32 a;

    a = car->torquePolyCoeffs[0];
    b = car->torquePolyCoeffs[1];
    c = car->torquePolyCoeffs[2];
    d = car->torquePolyCoeffs[3];
    rpm = car->rpm;
    scale = 1.0f;
    if (car->engineRpmState < 10000)
    {
        scale = 0.5f;
        rpm *= scale;
    }
    else if (car->transmissionFlags & 0x1000)
    {
        trans = GETP(car->vehicleIface);
        rpm /= trans->unk30;
        scale *= trans->unk34;
    }
    if (rpm < 0.0f)
    {
        rpm *= -1.0f;
    }
    trans = GETP(car->vehicleIface);
    return scale * (rpm * (rpm * (a * rpm + b) + c) + d) / GETP(trans->gears)[car->curveTableIdx].ratio;
}

/* Runs the driver's pending combo (Player.combo, cheats and stunt moves)
   and clears it: 2 repair, 3 self-destruct, 4 toggle transmission bit 1,
   5 stop, 6 full turbo, 9-16 stunt impulses (not while unk460 is set). */
void CarA_ExecuteCombo(CarA *car) {
    extern Vector4 g_zeroVector;
    extern u8 g_stunt1localString[];  /* port: a table of string slots */

    switch (CARA_PLAYER(car)->combo) {
    case 2:
        car->base.health = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
        car->partVisMask = 0;
        break;
    case 3:
        CARA_PLAYER(car)->combo = 0;
        Thing_ProcessEndEvent(car, GETP(GETP(car->vehicleIface)->collider), NULL, &car->localAxis);
        CarA_Release(car);
        return;
    case 4:
        car->transmissionFlags ^= 1;
        break;
    case 5:
        car->base.vel = g_zeroVector;
        car->gradeOrReverse = 0.0f;
        car->rpm = 0.0f;
        car->curveTableIdx = 1;
        break;
    case 6:
        car->turboTime = 30000;
        break;
    }
    if (car->unk460 == 0) {
        switch (CARA_PLAYER(car)->combo) {
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
    CARA_PLAYER(car)->combo = 0;
}

/* Adds force dir * scale at world point pos to the force and torque
   accumulators. */
void CarA_ProcessForce(CarA *car, Vector4 *pos, Vector4 *dir, f32 scale)
{

    Vector4 pointVel;
    Vector4 cross;
    Vector4 r;
    Vector4 torque;
    Vector4 carVel;
    Vector4 scaled __attribute__((aligned(16)));

    carVel = car->base.vel;
    PSVECScale(dir, &scaled, scale);
    PSVECSubtract(pos, &car->base.pos, &r);
    GetComponentOfVector(dir, &carVel);
    PSVECCrossProduct(&car->localAxis, &r, &cross);
    PSVECAdd(&car->base.vel, &cross, &pointVel);
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
            up = &car->base.axis[1];
        }
        GETP(car->motionState.animMatrices)[wheel->bone].v[3].x -= susp->wheelStates[0].compression * up->x;
        GETP(car->motionState.animMatrices)[wheel->bone].v[3].y -= susp->wheelStates[0].compression * up->y;
        GETP(car->motionState.animMatrices)[wheel->bone].v[3].z -= susp->wheelStates[0].compression * up->z;
        susp = (CarA *)((u8 *)susp + sizeof(CarAWheelState));
    }
    car->forceAccum.y += GETP(GETP(car->vehicleIface)->collider)->gravityScale * GETP(GETP(car->vehicleIface)->collider)->mass;
}

/* Bounces the car off a contact (point pos, normal) given the velocity of
   the point: the usual rigid-body impulse, restitution scaled by the
   collider material's. Airborne, the angular velocity is capped at
   engine->maxAngularVel. */
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
    PSVECSubtract(pos, &car->base.pos, &r);
    num = -(restitution * GETP(GETP(GETP(car->vehicleIface)->collider)->material)->restitution + 1.0f) * PSVECDotProduct(&vel, normal);
    PSVECCrossProduct(&r, normal, &rxn);
    InverseXformVecOrientation(THING_MTX(&car->base), &rxn, &localRxn);
    scaledRxn.x = car->inverseBodyInertia.v[0].x * localRxn.x;
    scaledRxn.y = car->inverseBodyInertia.v[1].y * localRxn.y;
    scaledRxn.z = car->inverseBodyInertia.v[2].z * localRxn.z;
    XformVecOrientation(THING_MTX(&car->base), &scaledRxn, &ang);
    PSVECCrossProduct(&ang, &r, &angxr);
    j = num / (1.0f / GETP(GETP(car->vehicleIface)->collider)->mass + PSVECDotProduct(&angxr, normal));
    impulse.x = j * normal->x;
    impulse.y = j * normal->y;
    impulse.z = j * normal->z;
    car->base.vel.x += impulse.x / GETP(GETP(car->vehicleIface)->collider)->mass;
    car->base.vel.y += impulse.y / GETP(GETP(car->vehicleIface)->collider)->mass;
    car->base.vel.z += impulse.z / GETP(GETP(car->vehicleIface)->collider)->mass;
    PSVECCrossProduct(&r, &impulse, &torque);
    car->angularMomentum.x += torque.x;
    car->angularMomentum.y += torque.y;
    car->angularMomentum.z += torque.z;
    if (car->numWheelsGrounded <= 1) {
        maxAng = GETP(GETP(car->vehicleIface)->engine)->maxAngularVel;
        InverseXformVecOrientation(THING_MTX(&car->base), &car->angularMomentum, &localAng);
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
            XformVecOrientation(THING_MTX(&car->base), &localAng, &car->angularMomentum);
        }
    }
    CarA_UpdateAngularVelocity(car);
}

/* Gathers this frame's forces: wheel loads and gravity, suspension, and the
   downforce points (driving forwards only). */
void CarA_ComputeForces(CarA *car) {
    extern Vector4 g_zeroVector;
    CollisionInfo hit __attribute__((aligned(16)));
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
    dot = PSVECDotProduct(&car->base.axis[2], &car->base.vel);
    forces = GETP(GETP(car->vehicleIface)->forcePoints);
    if (dot < 0.0f) {
        return;
    }
    down[0] = -car->base.axis[1].x;
    down[1] = -car->base.axis[1].y;
    down[2] = -car->base.axis[1].z;
    scale = 1.0f - GETP(GETP(car->vehicleIface)->engine)->unk58 * (car->base.axis[1].y - 1.0f);
    for (i = 0; i < forces->count; i++) {
        if (GETP(forces->points)[i].ray) {
            dist = 6.0f;
            if (car->numWheelsGrounded < GETP(car->vehicleIface)->numWheels && !RayHitsTerrainAndObjects(&car->base.pos, down, &dist, &hit)) {
                continue;
            }
        }
        XformVec(THING_MTX(&car->base), &GETP(forces->points)[i].pos, (Vector4 *)worldPos);
        CarA_ProcessForce(car, (Vector4 *)worldPos, (Vector4 *)down, scale * (dot * GETP(forces->points)[i].weight));
    }
}

/* Vtable PreMove: driver input (turbo, tilting a stuck car upright),
   forces, integration of velocity and angular momentum, nav point, engine
   and wind sounds, wheel spin, combos. */
s32 CarA_PreMove(void *thing) {
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

    car->prevVelocity = car->base.vel;
    car->unk4FC += g_fDeltaTimeSec;
    if (car->unk4FC >= 1.0f) {
        car->unk4FC = 1.0f;
    }
    car->transmissionFlags &= ~0x20000;
    if (Timer_Interrupt300()) {
        car->transmissionFlags &= ~0x10000;
    }

    if (GETP(car->base.player) != NULL) {
        switch (GETP(car->vehicleIface)->driveMode) {
        case 0:
            break;
        case 1:
            CarA_PreMove_Control_HW(car);
            break;
        }

        if (car->turboCooldown >= 0) {
            if (!(car->transmissionFlags & 0x1000) && (CARA_PLAYER(car)->buttonsHeld & 4) && car->turboTime > 0) {
                car->transmissionFlags |= 0x1000;
                if (car->unk460 == 0) {
                    SETP(fx.owner, GETP(CARA_PLAYER(car)->vehicle));
                    fx.unk24 = 0;
                    fx.pos.x = car->base.pos.x - 8.0f * car->base.axis[2].x - 3.0f * car->base.axis[1].x;
                    fx.pos.y = car->base.pos.y - 8.0f * car->base.axis[2].y - 3.0f * car->base.axis[1].y;
                    fx.pos.z = car->base.pos.z - 8.0f * car->base.axis[2].z - 3.0f * car->base.axis[1].z;
                    fx.dir.x = car->base.axis[2].x - 0.2f * car->base.axis[1].x;
                    fx.dir.y = car->base.axis[2].y - 0.2f * car->base.axis[1].y;
                    fx.dir.z = car->base.axis[2].z - 0.2f * car->base.axis[1].z;
                    if (PSVECSquareMag(&fx.dir)) {
                        PSVECNormalize(&fx.dir, &fx.dir);
                    }
                    fx.unk28 = 0;
                    fx.vel = g_zeroVector;
                    fx.unk40 = 0;
                    /* turbo kick: an impact of 280 along fx.dir (fx is a contact) */
                    THING_VFN(GETP(CARA_PLAYER(car)->vehicle), THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(void *, void *, s32, f32))(GETP(CARA_PLAYER(car)->vehicle), &fx, 0, 280.0f);
                }
                if (GETP(car->vehicleIface)->numSlaves > 1) {
                    MotionState_InitAllSlavesNow(&car->motionState, &CarA_SlaveSet(car, 1)->count, 1);
                }
            }
            car->turboCooldown -= g_iDeltaTimeMS;
        } else if (CARA_PLAYER(car)->buttonsHeld & 4) {
            car->turboCooldown = 700;
        }

        if (car->transmissionFlags & 0x1000) {
            if (CARA_PLAYER(car)->throttle && !CARA_PLAYER(car)->brake) {
                car->turboTime -= g_iDeltaTimeMS;
                if (GameSettings_IsCheatEnabled(5) && GETP(car->base.player) != NULL && Player_IsHuman(GETP(car->base.player))) {
                    car->turboTime = 30000;
                }
                if (car->turboTime <= 0) {
                    car->transmissionFlags &= ~0x1000;
                    MotionState_ReleaseSlavesByType(&car->motionState, 1);
                }
            } else {
                car->transmissionFlags &= ~0x1000;
                MotionState_ReleaseSlavesByType(&car->motionState, 1);
            }
        }

        if (car->tiltState != 0) {
            if (car->tiltState != 3 && CARA_PLAYER(car)->stickX < 0x40 && CARA_PLAYER(car)->stickX > -0x40) {
                CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[1],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
                car->tiltState = 0;
            }
            if (car->base.axis[1].y > 0.35f) {
                CarA_SetBodyOffset(car, GETP(GETP(car->vehicleIface)->engine)->bodyOffset[0],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[1],
                                   GETP(GETP(car->vehicleIface)->engine)->bodyOffset[2]);
                car->tiltState = 0;
                car->stuckCounter = 0;
                car->unk45C = 0;
            }
        }
        if (car->stuckCounter > 600) {
            if (CARA_PLAYER(car)->stickX > 0x40) {
                if (car->tiltState != 1) {
                    CarA_SetBodyOffset(car, car->tiltX - 1.0f, 1.0f - car->tiltY, 0.0f);
                    car->tiltState = 1;
                }
            } else if (CARA_PLAYER(car)->stickX < -0x40) {
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
            MotionState_ReleaseSlavesByType(&car->motionState, 9);
            car->transmissionFlags &= ~0x4000;
        }
    } else if (!(car->transmissionFlags & 0x4000)) {
        if (GETP(car->vehicleIface)->numSlaves > 9) {
            MotionState_InitAllSlavesNow(&car->motionState, &CarA_SlaveSet(car, 9)->count, 9);
            car->transmissionFlags |= 0x4000;
        }
    }

    CarA_ComputeForces(car);
    car->base.vel.x += g_fDeltaTimeSec * (car->forwardAccel * car->base.axis[0].x);
    car->base.vel.y += g_fDeltaTimeSec * (car->forwardAccel * car->base.axis[0].y);
    car->base.vel.z += g_fDeltaTimeSec * (car->forwardAccel * car->base.axis[0].z);
    g_pSourcePlayer = GETP(car->base.player);
    CarA_SlaveWeaponHeartbeat(car);
    g_pSourcePlayer = NULL;

    invMass = g_fDeltaTimeSec / GETP(GETP(car->vehicleIface)->collider)->mass;
    if (car->base.flags & 0x10000) {
        car->torqueAccum = g_zeroVector;
        car->forceAccum = car->torqueAccum;
    } else {
        car->base.vel.x += invMass * car->forceAccum.x;
        car->base.vel.y += invMass * car->forceAccum.y;
        car->base.vel.z += invMass * car->forceAccum.z;
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
        car->navPointIndex = NavHolder_FindIndexOfNearestNavPoint(&car->base.pos, car->navPointIndex);
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
                enginePitch *= GETP(GETP(GETP(car->vehicleIface)->soundSets)[1])->pitchScale;
            }
            SoundSource_Set(car->soundIds[1], 5, &car->base.pos);
            SoundSource_Set(car->soundIds[1], 6, &car->base.vel);
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
        SoundSource_Set(car->soundIds[0], 5, &car->base.pos);
        SoundSource_Set(car->soundIds[0], 6, &car->base.vel);
        SoundSource_Set(car->soundIds[0], 1, &windPitch);
    }

    speed = g_fDeltaTimeSec * PSVECDotProduct(&car->base.vel, &car->base.axis[2]);
    spin = car->gradeOrReverse * g_fDeltaTimeSec;
    if (GETP(car->base.player) != NULL && CARA_PLAYER(car)->brake == 0) {
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
    if (GETP(car->base.player) != NULL) {
        CarA_ExecuteCombo(car);
    }
    Thing_PassiveTargeting(car);
    return 1;
}

/* Vtable Move: health bar slave, teleporters, item pickups, then the pose:
   bone matrices with the wheels pushed out by their compression, skid
   trails, and the slaves. */
s32 CarA_Move(void *thing) {
    typedef struct HealthSlave {    /* slave type 0x11: the health bar */
        Thing base;
        u8 padAC[0xC];
        f32 fraction;               /* 0xB8 */
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
        slave->fraction = (f32)car->base.health / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth;
        if (GETP(car->base.player) != NULL && !Player_IsHuman(GETP(car->base.player))) {
            slave->fraction /= GETP(CARA_PLAYER(car)->aiScript)->healthScale;
        }
        if (slave->fraction > 1.0f) {
            slave->fraction = 1.0f;
        }
    }
    scale = 1.0f / Sqrt(car->base.quat.w * car->base.quat.w +
                        (car->base.quat.z * car->base.quat.z +
                         (car->base.quat.x * car->base.quat.x + car->base.quat.y * car->base.quat.y)));
    car->base.quat.x *= scale;
    car->base.quat.y *= scale;
    car->base.quat.z *= scale;
    car->base.quat.w *= scale;
    if (GETP(car->base.player) != NULL && Player_IsHuman(GETP(car->base.player)) && GameSettings_IsCheatEnabled(5)) {
        car->turboTime = 30000;
    }
    if (Thing_CollideWithTeleporters(car)) {
        CarA_StuntFail(car);
    }
    Thing_CollideWithItems(car);
    numBones = &GETP(GETP(car->motionState.model)->skeleton)->numBones;
    CarA_Extract_Keyframes(car, frames);
    XformVec(THING_MTX(&car->base), &car->bodyOffset, &pos);
    TEST_AnimTraverseHeirarchyToMatricesWorld(&car->base.quat, &pos, frames, *numBones, GETP(car->motionState.animMatrices), 1.0f);
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
            dx = car->base.axis[1].x;
            dy = car->base.axis[1].y;
            dz = car->base.axis[1].z;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].x += car->wheelStates[i].compression * dx;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].y += car->wheelStates[i].compression * car->base.axis[1].y;
            GETP(car->motionState.animMatrices)[wheel->bone].v[3].z += car->wheelStates[i].compression * car->base.axis[1].z;
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
    g_pSourcePlayer = GETP(car->base.player);
    MotionState_Heartbeat(&car->motionState, &g_identity, car);
    g_pSourcePlayer = NULL;
    return 1;
}

/* Hands the car's driver on to all its slave things. */
void CarA_SetSlavePlayers(CarA *car)
{
    Thing *slave;

    for (slave = GETP(car->motionState.slaves); slave != NULL; slave = GETP(slave->next))
    {
        slave->player.h = car->base.player.h;
    }
}

/* Draws the car and its mirror image under it (flipped about the ground
   plane under the first wheel), into temporary bone matrices. */
void CarA_DrawReflection(void *thing) {
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
            u32 *skel = &GETP(((MotionAnim *)GETP(car->motionState.model))->skeleton)->numBones;
            CarA_Extract_Keyframes(car, frames);
            XformVec(THING_MTX(&car->base), &car->bodyOffset, &pos);
            TEST_AnimTraverseHeirarchyToMatricesWorld(&car->base.quat, &pos, frames, *skel, matrices, 1.0f);
        }
        Create_Plane3(&plane, &car->base.axis[1], &pos);
        h = -Distance_Plane(&plane, &matrices[GETP(GETP(car->vehicleIface)->wheels)->bone].v[3]);
        offset += 2.0f * h + 2.0f * GETP(GETP(car->vehicleIface)->wheels)->offset;
        {
            u32 i;
            for (i = 0; i < ((MotionAnim *)GETP(car->motionState.model))->numRigid; i++) {
                MotionPart *e = &GETP(((MotionAnim *)GETP(car->motionState.model))->rigid)[i];
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
        mtx = *THING_MTX(&car->base);
        {
            u32 i;
            u32 *skel = &GETP(((MotionAnim *)GETP(car->motionState.model))->skeleton)->numBones;
            CarA_Extract_Keyframes(car, frames2);
            Gfx_AnimTraverseHeirarchyToMatrices(frames2, *skel, matrices);
            mtx.v[1].x *= -1.0f;
            mtx.v[1].y *= -1.0f;
            mtx.v[1].z *= -1.0f;
            mtx.v[0].x *= -1.0f;
            mtx.v[0].y *= -1.0f;
            mtx.v[0].z *= -1.0f;
            XformVec(THING_MTX(&car->base), &car->bodyOffset, &mtx.v[3]);
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
            for (i = 0; i < ((MotionAnim *)GETP(car->motionState.model))->numRigid; i++) {
                MotionPart *e = &GETP(((MotionAnim *)GETP(car->motionState.model))->rigid)[i];
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

/* Draws the animated model (skinned geometry, rigid parts, and the breakable
   parts still on, see partVisMask) from the current bone matrices. */
void CarA_DrawScript(CarA *car) {
    MotionAnim *model;
    Matrix4 *matrices;
    u32 i;
    u32 *skel;
    u32 j;
    CarPartList *list;
    CarPart *part;
    u32 bit;
    void *geom;
    Matrix4 mtx;

    matrices = GETP(car->motionState.animMatrices);
    if (matrices == NULL) {
        return;
    }
    LightManager_LightThing(car);
    model = (MotionAnim *)GETP(car->motionState.model);
    skel = &GETP(model->skeleton)->numBones;
    for (i = 0; i < model->numGeoms; i++) {
        Gfx_DrawAnimGeometryAux(GETP(GETP(model->geoms)[i]), matrices, *skel, 0);
    }
    for (i = 0; i < model->numRigid; i++) {
        MotionPart *e = &GETP(model->rigid)[i];
        Gfx_DrawGeometry_MatrixNotCached(HND(GETP(e->geom)), HND(&matrices[e->mtxIndex]));
    }
    list = GETP(GETP(GETP(GETP(car->vehicleIface)->modelRef)->data)->parts);
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

/* Vtable Draw. Skipped for the driver's own viewport in an in-car camera;
   in the garage (state 5) a human car also gets its reflection geometry. */
void CarA_Draw(CarA *car)
{
    #define g_pNoRayCollideThing (*(void **)(void *)&g_pNoRayCollideThing)
    Matrix4 mtx __attribute__((aligned(16)));
    CarAEngine *engine;

    if (GETP(car->base.player) != NULL && (CARA_PLAYER(car)->flags & 0x10) &&
        !(CARA_PLAYER(car)->flags & 0x80) && ViewManager_GetViewFormat() <= 1) {
        engine = GETP(GETP(car->vehicleIface)->engine);
        if (GETP(engine->cameras.list)[(CARA_PLAYER(car)->cameraIndex % engine->cameras.count) * 0xC] == 4) {
            return;
        }
    }
    CarA_DrawScript(car);
    if (GETP(GETP(car->vehicleIface)->reflectionGeom) != NULL && StateManager_CurrState() == 5) {
        mtx = *THING_MTX(&car->base);
        XformVec(THING_MTX(&car->base), &car->bodyOffset, &mtx.v[3]);
        if (Player_IsHuman(GETP(car->base.player))) {
            Gfx_DrawGeometry(HND(GETP(GETP(car->vehicleIface)->reflectionGeom)), HND(&mtx));
        }
    }
    g_pNoRayCollideThing = car;
    MotionState_DrawSlaves(&car->motionState, 0);
    g_pNoRayCollideThing = NULL;
#undef g_pNoRayCollideThing
}

/* Vtable GetMass. */
f32 CarA_GetMass(CarA *car)
{
    return GETP(GETP(car->vehicleIface)->collider)->mass;
}

/* Vtable DynamicRotation: turns the car by its angular velocity (and the
   stunt input) over dt. The box's corners are swept by rays from the old
   pose; any hit, or the new box touching something, bounces the car instead
   of rotating it. Only the oriented-box collider rotates. */
void CarA_DynamicRotation(CarA *car, f32 dt) {
    #define g_pNoRayCollideThing (*(CarA **)(void *)&g_pNoRayCollideThing)
    CollisionOBox obox;
    CollisionInfo hit;
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
    CollisionOBox *old;
    void *box;
    s8 hitAny;

    t = dt;
    axis = car->localAxis;
    mag = MagnitudeVector(&axis);
    if (mag > 0.0001f) {
        axis.x /= mag;
        axis.y /= mag;
        axis.z /= mag;
    }
    GenerateQuaternion(&axis, &q, mag * t);
    QuaternionMult((Vector4 *)&q, (Vector4 *)&car->base.quat, (Vector4 *)&newQ);
    CarA_StuntsGetNewOrientation(car, &newQ, t);
    QuaternionToSubMatrix(&newQ, &mtx);
    mtx.v[3] = car->base.pos;
    switch (GETP(GETP(car->vehicleIface)->collider)->type) {
    case 0:
        box = GETP(GETP(GETP(car->vehicleIface)->collider)->box);
        old = CarA_GetOrientedCollision(car);
        hitAny = 0;
        BuildOOBOffset(box, &mtx, &car->bodyOffset, &obox);
        g_pNoRayCollideThing = car;
        for (i = 0; i < 8; i++) {
            if (t > 0.0f) {
                dir.x = (obox.corners[i].x - old->corners[i].x) / dt;
                dir.y = (obox.corners[i].y - old->corners[i].y) / dt;
                dir.z = (obox.corners[i].z - old->corners[i].z) / dt;
                if (RayHitsTerrainAndObjects(&old->corners[i], &dir, &t, &hit)) {
                    hitAny = 1;
                    CarA_ProcessImpulse(car, &hit.normal, &hit.pos, &dir, P(CarAMaterial, hit.material)->restitution);
                }
            }
        }
        g_pNoRayCollideThing = NULL;
        if (!hitAny) {
            if (OBoxHitsTerrainAndObjectsZeroTimeEdgesOnly(&obox, car, &hit)) {
                PSVECSubtract(&hit.pos, &car->base.pos, &r);
                PSVECCrossProduct(&car->localAxis, &r, &v);
                CarA_ProcessImpulse(car, &hit.normal, &hit.pos, &v, 0.5f);
            } else {
                car->base.quat = newQ;
                *THING_MTX(&car->base) = mtx;
                car->orientedBox = obox;
                /* the box's axes point into the car's own matrix rows */
                SETP(car->orientedBox.axis[0], &car->base.axis[2]);
                SETP(car->orientedBox.axis[1], &car->base.axis[0]);
                SETP(car->orientedBox.axis[2], &car->base.axis[1]);
                car->base.flags &= ~0x180;
            }
        }
        break;
    case 1:
        break;
    }
#undef g_pNoRayCollideThing
}

/* Breaks off the car parts within 5 units of a hit, once health is down to
   damagePercent and the hit is hard enough: each flies off as a thing of
   type 6 (with its debris effect and slave) and is hidden (partVisMask). */
void CarA_ProcessDamageSystem(CarA *car, Vector4 *hitPos, s32 level, f32 impulse) {
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
    Thing *thing;
    Thing *slave;
    f32 absImpulse;
    u32 debris;

    absImpulse = _fabs(impulse);
    if (hitPos == NULL) {
        return;
    }
    if (GETP(car->vehicleIface)->damageLevel > level && !(absImpulse > 70.0f)) {
        return;
    }
    if (!(100.0f * (f32)car->base.health / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth <= GETP(car->vehicleIface)->damagePercent)) {
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
                vel = car->base.vel;
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
                    debrisParams.debris = debris;
                    debrisParams.pos = pos;
                    debrisParams.quat = quat;
                    debrisParams.vel = vel;
                    debrisParams.owner = 0;
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
                        slave->slaveTag = 0xFF;
                        ThingManagerList_Append(&thing->slaves, slave);
                    }
                }
            }
            car->partVisMask |= bit;
        }
    }
}

/* Vtable GetTargetPos: where weapons aim, the centre of the collider. */
void CarA_GetTargetPos(CarA *car, Vector4 *pos)
{
    switch (GETP(GETP(car->vehicleIface)->collider)->type)
    {
    case 0:
        *pos = car->orientedBox.center;
        break;
    case 1:
        *pos = ((OConvexHull *)GETP(car->hull))->pos;
        break;
    }
}

/* Vtable GetInterfaceInfo: fills the HUD block (info, the interface's
   layout) with health %, weapon ammo, speed (smoothed), rpm, turbo, the HUD
   layout for the number of viewports, and the pending stunt/pickup
   messages, which are handed over once. */
void CarA_GetInterfaceInfo(CarA *car, InterfaceHudInfo *info) {
    WildCardRack *rack;
    WeaponInfo *weapon;
    u32 count;

    rack = ThingManagerList_GetByType(&car->motionState.slaves, THING_TYPE_WILDCARD_RACK);
    info->health = 100.0f * ((f32)car->base.health / (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth);
    if (info->health > 100.0f) {
        info->health = 100;
    }
    if (rack != NULL && GETP(rack->slots[rack->cur].item) != NULL) {
        /* ammo bar: percent of maxAmmo, and in shots */
        if (GameSettings_IsCheatEnabled(0)) {
            rack->slots[rack->cur].ammo = GETP(GETP(rack->slots[rack->cur].item)->weapon)->maxAmmo;
        }
        weapon = GETP(GETP(rack->slots[rack->cur].item)->weapon);
        if (weapon->ammoPerShot != 0) {
            info->ammo = 100.0f * ((f32)rack->slots[rack->cur].ammo / (f32)weapon->maxAmmo);
            info->ammoPips = info->ammo / weapon->ammoPerShot;
        } else {
            info->ammo = 0;
            info->ammoPips = 0;
        }
        if (weapon != NULL) {
            info->weaponName.h = weapon->hudName;
        }
    }
    info->speed = _fabs(0.681818f * car->gradeOrReverse);
    if (car->speedoRising && info->speed < car->speedoShown) {
        car->speedoCount++;
        if (car->speedoCount > 5) {
            car->speedoRising = 0;
            car->speedoCount = 0;
            car->speedoShown = info->speed;
        } else {
            info->speed = car->speedoShown;
        }
    } else if (car->speedoRising) {
        car->speedoCount = 0;
        car->speedoShown = info->speed;
    }
    if (!car->speedoRising && info->speed > car->speedoShown) {
        car->speedoCount++;
        if (car->speedoCount > 5) {
            car->speedoRising = 1;
            car->speedoCount = 0;
            car->speedoShown = info->speed;
        } else {
            info->speed = car->speedoShown;
        }
    } else if (!car->speedoRising) {
        car->speedoCount = 0;
        car->speedoShown = info->speed;
    }
    info->rpm = car->rpm;
    info->turbo = car->turboTime / 300;
    info->altitude = 0.2f * (100.0f + car->base.pos.y);
    if (GETP(car->base.player) != NULL && (CARA_PLAYER(car)->flags & 0x200)) {
        if (GameSettings_GetCodeVariable(0x198, 0)) {
            count = CameraManager_GetActiveCameras();
        } else {
            count = GameSettings_GetNumOfPlayers();
        }
        if (count > 1) {
            if (count == 2) {
                info->layout = GETP(GETP(car->vehicleIface)->hud)->layout2;
                if (CARA_PLAYER(car)->view == 1) {
                    info->x = 0;
                    info->y = 10;
                } else {
                    info->x = 0;
                    info->y = 230;
                }
            } else {
                info->layout = GETP(GETP(car->vehicleIface)->hud)->layout4;
                if (CARA_PLAYER(car)->view == 1) {
                    info->x = 10;
                    info->y = 10;
                } else if (CARA_PLAYER(car)->view == 2) {
                    info->x = 310;
                    info->y = 10;
                } else if (CARA_PLAYER(car)->view == 3) {
                    info->x = 10;
                    info->y = 230;
                } else {
                    info->x = 310;
                    info->y = 230;
                }
            }
        } else {
            info->layout = GETP(GETP(car->vehicleIface)->hud)->layout1;
            info->x = 0;
            info->y = 0;
        }
    }
    if (rack != NULL && GETP(rack->slots[rack->cur].motion.model) != NULL) {
        info->weapon[0] = 1;
        info->weaponIcon[0] = GETP(GETP(rack->slots[rack->cur].item)->weapon)->hudIcon;
    }
    if (car->stuntPopup.h != 0) {
        info->messages[1].h = car->stuntPopup.h;
        car->stuntPopup.h = 0;
    }
    if (car->pickupId != 0) {
        info->messages[0].h = car->pickupId;
        car->pickupId = 0;
    }
    if (car->stuntScoreMessage.h != 0) {
        info->messages[2].h = car->stuntScoreMessage.h;
        car->stuntScoreMessage.h = 0;
    }
    if (car->stuntMessage.h != 0) {
        info->messages[3].h = car->stuntMessage.h;
        car->stuntMessage.h = 0;
    }
}


/* Vtable DynamicCollideImpact: the car hit something (collision is the
   contact, impulse along its normal). Handles flipping and wall hits,
   damage and broken parts, dropping a carried item when two cars collide,
   force feedback, impact sounds, spin-out torque, and death (explosion,
   kill credit, Release). */
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
    extern u8 g_carAVTable[];
    extern u8 g_itemVTable[];
    extern u8 g_destructibleVTable[];
    extern u8 g_physicalObjectVTable[];
    extern Vector4 g_zeroVector;
    CollisionInfo *coll = collision;
    CarA *car = thing;
    DebrisParams sp;
    DebrisParams snd;
    DebrisParams ex;
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
    ItemThing *item;
    Thing *rack;
    DeathInfo *death;
    CarA *other;
    void *spawned;
    Player *player;
    u32 id;
    u32 exId;
    s32 idx;

    n = coll->normal;
    p = coll->pos;
    vel = car->base.vel;
    if (car->base.axis[1].y < 0.3f) {
        d = PSVECDotProduct(&car->base.axis[1], &n);
        d2 = PSVECDotProduct(&car->base.axis[1], &car->base.vel);
        if (impulse < 0.0f) {
            d *= -1.0f;
        }
        if (d < -0.5f && d2 > 0.0f) {
            if (P(CarA, coll->thingB) == NULL || GETP(P(CarA, coll->thingA)->base.player) != GETP(P(CarA, coll->thingB)->base.player)) {
                CarA_StuntFail(car);
            }
            car->unk45C = 100;
            car->angularMomentum.x *= 0.9f;
            car->angularMomentum.y *= 0.9f;
            car->angularMomentum.z *= 0.9f;
            CarA_UpdateAngularVelocity(car);
            car->base.vel.x *= 0.9f;
            car->base.vel.y *= 0.9f;
            car->base.vel.z *= 0.9f;
        }
        if (_fabs(PSVECDotProduct(&car->base.axis[0], &n)) > 0.7f) {
            car->unk45C = 100;
        }
        if (_fabs(PSVECDotProduct(&car->base.axis[2], &n)) > 0.7f) {
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
        MotionState_ReleaseSlavesByType(&car->motionState, 8);
    }
    if (P(CarA, coll->thingB) == NULL && PSVECDotProduct(&coll->normal, &car->base.axis[2]) < -0.85f) {
        Create_Plane(&plane, &car->base.axis[1], &car->base.pos);
        dist = Distance_Plane(&plane, &p);
        if (dist < 5.0f && dist > 0.0f) {
            d = PSVECDotProduct(&n, &car->base.axis[1]);
            n.x = n.x - d * car->base.axis[1].x;
            n.y = n.y - d * car->base.axis[1].y;
            n.z = n.z - d * car->base.axis[1].z;
            dist *= 0.5f;
            p.x = p.x - car->base.axis[1].x * dist;
            p.y = p.y - car->base.axis[1].y * dist;
            p.z = p.z - car->base.axis[1].z * dist;
        }
    }
    damage -= GETP(GETP(car->vehicleIface)->collider)->damageThreshold;
    if (damage > 0 && car->damageRpm >= 10000) {
        CarA_ProcessDamageSystem(car, coll, damage, impulse);
        car->damageAccum += damage;
        if (car->damageAccum > 30) {
            while (car->damageAccum > 30) {
                car->base.health -= 30;
                car->damageAccum -= 30;
            }
            if (GameSettings_IsCheatEnabled(6) && GETP(car->base.player) != NULL && Player_IsHuman(GETP(car->base.player))) {
                car->base.health = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
            }
            CarA_UpdateDamageSlaves(car);
        }
    }
    if (P(CarA, coll->thingB) != NULL && (u8 *)GETP(P(CarA, coll->thingA)->base.vtable) == g_carAVTable && (u8 *)GETP(P(CarA, coll->thingB)->base.vtable) == g_carAVTable &&
        _fabs(impulse) > GETP(GETP(car->vehicleIface)->engine)->itemDropImpact) {
        item = ThingManagerList_GetByType(&car->motionState.slaves, 4);
        if (item != NULL) {
            t = 1.0f;
            rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
            rack->flags &= 0xFFBFFDFF;
            XformVec(&GETP(car->motionState.animMatrices)[item->base.slaveBone], &item->base.pos, &itemPos);
            if (!RayHitsTerrain(&car->base.pos, &itemPos, &t, NULL)) {
                Trigger_ItemDropped(car, item);
                ThingManagerList_Remove(&car->motionState.slaves, item);
                MultiplyMatrix(THING_MTX(&item->base), THING_MTX(&car->base), &m);
                *THING_MTX(&item->base) = m;
                ThingManagerList_Append(&g_thingManager_DARYL.activeList, item);
                item->pickupDelay = 1000;
                item->base.flags &= 0xFFFBFFF5;
                item->base.vel = car->base.vel;
                item->base.vel.y += 30.0f;
                if (item->base.collisionType != THING_COLLIDE_NONE && (u8 *)GETP(item->base.vtable) == g_itemVTable) {
                    Thing_AddToGrid(item, 3);
                }
            }
        }
    }
    dv = impulse / GETP(GETP(car->vehicleIface)->collider)->mass;
    adv = _fabs(dv);
    car->base.vel.x += n.x * dv;
    car->base.vel.y += n.y * dv;
    car->base.vel.z += n.z * dv;
    if (GETP(car->base.player) != NULL && Player_IsHuman(GETP(car->base.player)) && adv > 0.0f) {
        mag = 0.06f * adv;
        player = GETP(car->base.player);
        dur = 0.3f;
        rem = mag;
        if (mag > 1.0f) {
            mag = 1.0f;
        }
        if (rem > 2.0f) {
            rem -= 1.0f;
            Input_SetForceFeedback(player->device, 0, 1.0f, 0.15f);
        }
        while (rem > 1.0f) {
            rem *= 0.5f;
            dur += 0.2f;
            if (dur > 0.8f) {
                dur = 0.8f;
                break;
            }
        }
        Input_SetForceFeedback(player->device, 1, mag, dur);
    }
    if (P(CollideSurface, coll->material) != NULL || P(CarA, coll->thingB) != NULL) {
        if (P(CollideSurface, coll->material) == NULL) {
            if (P(CarA, coll->thingA) == car) {
                coll->material = HND(THING_VFN(P(CarA, coll->thingB), THING_V_GET_SURFACE_SCRIPT, void *(*)(void *))(P(CarA, coll->thingB)));
            } else {
                coll->material = HND(THING_VFN(P(CarA, coll->thingA), THING_V_GET_SURFACE_SCRIPT, void *(*)(void *))(P(CarA, coll->thingA)));
            }
        }
        if (impulse > 0.0f) {
            bounce = 0.02f * -P(CollideSurface, coll->material)->bounce;
        } else {
            bounce = 0.02f * P(CollideSurface, coll->material)->bounce;
        }
        if (0.0f != impulse && PSVECDotProduct(&coll->normal, &car->base.axis[1]) < 0.5f) {
            bx = car->base.vel.x * bounce;
            by = car->base.vel.y * bounce;
            bz = car->base.vel.z * bounce;
            car->base.vel.x = car->base.vel.x + bx;
            car->base.vel.y = car->base.vel.y + by;
            car->base.vel.z = car->base.vel.z + bz;
            n.x += 0.1f * bx;
            n.y += 0.1f * by;
            n.z += 0.1f * bz;
        }
        if (P(CarA, coll->thingB) == NULL && Gfx_GetMinCameraDistSquared(&car->base.pos) < 10000.0f) {
            {
                Vector4 up = {0.0f, 1.0f, 0.0f, 0.0f};
                Vector4 side = {1.0f, 0.0f, 0.0f, 0.0f};

                refUp = up;
                axis = side;
            }
            id = 0;
            cosA = PSVECDotProduct(&coll->normal, &refUp);
            PSVECSubtract(&coll->pos, &car->base.pos, &rel);
            if (PSVECDotProduct(&car->base.axis[1], &rel) < 0.0f) {
                idx = (s32)((f32)P(CollideSurface, coll->material)->numSoundsA * ((dv - P(CollideSurface, coll->material)->minImpactA) / (P(CollideSurface, coll->material)->maxImpactA - P(CollideSurface, coll->material)->minImpactA)) - 1.0f);
                if (idx >= (s32)P(CollideSurface, coll->material)->numSoundsA) {
                    idx = P(CollideSurface, coll->material)->numSoundsA - 1;
                }
                if (idx >= 0 && P(CollideSurface, coll->material)->numSoundsA != 0) {
                    id = GETP(P(CollideSurface, coll->material)->soundsA)[idx];
                }
            } else {
                idx = (s32)((f32)P(CollideSurface, coll->material)->numSoundsB * ((dv - P(CollideSurface, coll->material)->minImpactB) / (P(CollideSurface, coll->material)->maxImpactB - P(CollideSurface, coll->material)->minImpactB)) - 1.0f);
                if (idx >= (s32)P(CollideSurface, coll->material)->numSoundsB) {
                    idx = P(CollideSurface, coll->material)->numSoundsB - 1;
                }
                if (idx >= 0 && P(CollideSurface, coll->material)->numSoundsB != 0) {
                    id = GETP(P(CollideSurface, coll->material)->soundsB)[idx];
                }
            }
            if (id != 0) {
                if (cosA < -0.98f) {
                    GenerateQuaternion(&axis, &sp.quat, 3.1415927f);
                } else if (cosA > 0.98f) {
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
                sp.debris = id;
                sp.pos = coll->pos;
                sp.vel = car->base.vel;
                sp.owner = car->base.player.h;
                sp.unk48 = 0.0f;
                sp.unk4C = 0.0f;
                sp.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &sp);
            }
        }
    }
    PSVECSubtract(&p, &car->base.pos, &r);
    imp.x = impulse * n.x;
    imp.y = impulse * n.y;
    imp.z = impulse * n.z;
    PSVECCrossProduct(&r, &imp, &torque);
    a = PSVECDotProduct(&r, &car->base.axis[0]);
    PSVECCrossProduct(&imp, &r, &c2);
    b = PSVECDotProduct(&c2, &car->base.axis[1]);
    c = PSVECDotProduct(&r, &car->base.axis[2]);
    if (_fabs(PSVECDotProduct(&coll->normal, &car->base.axis[1])) < 0.3f && c > 0.0f && car->numWheelsGrounded != 0 &&
        a * b < 0.0f && PSVECDotProduct(&vel, &car->base.axis[2]) > _fabs(PSVECDotProduct(&vel, &car->base.axis[0]))) {
        s = PSVECDotProduct(&car->base.axis[2], &car->base.vel);
        tw = PSVECDotProduct(&car->base.axis[1], &torque);
        if (P(CarA, coll->thingB) == NULL ||
            (((CollideSurface *)THING_VFN(P(CarA, coll->thingA), THING_V_GET_SURFACE_SCRIPT, void *(*)(void *))(P(CarA, coll->thingA)))->grip > 0.0f &&
             ((CollideSurface *)THING_VFN(P(CarA, coll->thingB), THING_V_GET_SURFACE_SCRIPT, void *(*)(void *))(P(CarA, coll->thingB)))->grip > 0.0f)) {
            if (GETP(car->base.player) != NULL && CARA_PLAYER(car)->throttle == 0) {
                car->transmissionFlags |= 0x10;
            } else {
                car->unk4FC = 0.0f;
            }
        }
        if (P(CarA, coll->thingB) != NULL && (!(P(CarA, coll->thingA)->base.flags & 8) || P(CarA, coll->thingA)->base.health == 0) &&
            (!(P(CarA, coll->thingB)->base.flags & 8) || P(CarA, coll->thingB)->base.health == 0)) {
            ProjectVectorToNormal(&car->base.axis[1], &torque);
        } else {
            sgn = 1.0f;
            torque.x = torque.x - tw * car->base.axis[1].x;
            torque.y = torque.y - tw * car->base.axis[1].y;
            torque.z = torque.z - tw * car->base.axis[1].z;
            if (tw < 0.0f) {
                sgn = -1.0f;
                tw *= sgn;
            }
            if (tw < 1200.0f) {
                tw = 1200.0f;
            }
            if (tw > 4500.0f) {
                tw = 4500.0f;
            }
            tw *= sgn;
            torque.x = torque.x - tw * car->base.axis[1].x;
            torque.y = torque.y - tw * car->base.axis[1].y;
            torque.z = torque.z - tw * car->base.axis[1].z;
        }
        if (P(CarA, coll->thingA) == car) {
            if (s < 0.0f) {
                ProjectVectorToNormal(&car->base.vel, &n);
            }
        } else if (s > 0.0f) {
            ProjectVectorToNormal(&car->base.vel, &n);
        }
    } else if (_fabs(impulse) > 10.0f && P(CarA, coll->thingB) == NULL && car->numWheelsGrounded != 0) {
        if (_fabs(PSVECDotProduct(&car->base.axis[1], &coll->normal)) < 0.3f) {
            if (_fabs(PSVECDotProduct(&car->base.axis[0], &coll->normal)) < 0.99f && c > 0.0f) {
                car->transmissionFlags |= 0x2000;
            }
        }
    }
    car->angularMomentum.x += torque.x;
    car->angularMomentum.y += torque.y;
    car->angularMomentum.z += torque.z;
    if (GETP(car->base.player) != NULL && Player_IsHuman(GETP(car->base.player)) && !(car->transmissionFlags & 0x10000)) {
        nn = coll->normal;
        fb = 200.0f * _fabs(impulse / 1000.0f);
        ProjectVectorToNormal(&car->base.axis[0], &nn);
        fb = fb * MagnitudeVector(&nn);
        if (fb > 20.0f) {
            player = GETP(car->base.player);
            Wheel_StopCarAirborne(player->device);
            Wheel_PlayFrontalCollisionForce(player->device, fb <= 255.0f ? fb : 255.0f);
            car->transmissionFlags |= 0x10000;
        }
    }
    if (car->numWheelsGrounded <= 1) {
        maxW = GETP(GETP(car->vehicleIface)->engine)->maxAngularVel;
        InverseXformVecOrientation(THING_MTX(&car->base), &car->angularMomentum, &local);
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
            XformVecOrientation(THING_MTX(&car->base), &local, &car->angularMomentum);
        }
    }
    CarA_UpdateAngularVelocity(car);
    if (car->base.health <= 0) {
        if (GETP(car->base.trigger) != NULL && (death = GETP(((DeathTile *)GETP(car->base.trigger))->death)) != NULL) {
            dp.pos = car->base.pos;
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
                ((Thing *)spawned)->flags |= 1;
                if (death->sound != 0) {
                    snd.debris = death->sound;
                    snd.pos = car->base.pos;
                    snd.quat = dp.quat;
                    snd.vel.z = 0.0f;
                    snd.vel.y = 0.0f;
                    snd.vel.x = 0.0f;
                    snd.owner = 0;
                    snd.unk48 = 0.0f;
                    snd.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &snd);
                }
            }
        }
        if (P(CarA, coll->thingA) == car) {
            other = P(CarA, coll->thingB);
        } else {
            other = P(CarA, coll->thingA);
        }
        if (GETP(car->base.player) != NULL) {
            if (other != NULL && GETP(other->base.player) != NULL && Player_IsHuman(GETP(other->base.player))) {
                CARA_PLAYER(car)->flags |= 0x40;
            } else {
                CARA_PLAYER(car)->flags &= ~0x40;
            }
        }
        if (!Player_IsHuman(GETP(car->base.player))) {
            exId = GETP(car->vehicleIface)->explosionThing;
            if (exId != 0) {
                ex.debris = exId;
                ex.pos = car->base.pos;
                ex.quat = *(Vector4 *)&car->base.quat;
                ex.vel = g_zeroVector;
                ex.owner = car->base.player.h;
                ex.unk48 = 0.0f;
                ex.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &ex);
            }
        }
        Thing_ProcessEndEvent(car, GETP(GETP(car->vehicleIface)->collider), coll, &car->localAxis);
        if (other != NULL && GETP(other->base.player) != NULL) {
            CARA_PLAYER(other)->kills++;
        }
        CarA_Release(car);
    }
    if (P(CarA, coll->thingB) == NULL || (u8 *)GETP(P(CarA, coll->thingA)->base.vtable) == g_destructibleVTable ||
        (u8 *)GETP(P(CarA, coll->thingA)->base.vtable) == g_physicalObjectVTable || (u8 *)GETP(P(CarA, coll->thingB)->base.vtable) == g_destructibleVTable ||
        (u8 *)GETP(P(CarA, coll->thingB)->base.vtable) == g_physicalObjectVTable) {
        car->transmissionFlags &= ~2;
        MotionState_ReleaseSlavesByType(&car->motionState, 4);
        ThingManagerList_RemoveReleasedThings(&car->motionState.slaves);
    }
    car->lowSpeedFlag = 0;
}

/* Vtable GetOrientedCollision: the collider in world space, the box rebuilt
   if the car moved (flags & 0x180), the hull every time. */
void *CarA_GetOrientedCollision(CarA *car)
{
    CarACollider *coll;

    coll = GETP(GETP(car->vehicleIface)->collider);
    switch (coll->type)
    {
    case 0:
        if (car->base.flags & 0x180)
        {
            BuildOOBOffset(GETP(coll->box), THING_MTX(&car->base), &car->bodyOffset, &car->orientedBox);
            car->base.flags &= ~0x180;
        }
        return &car->orientedBox;
    case 1:
        BuildOConvexHullOffset(GETP(car->hull), GETP(coll->hull), THING_MTX(&car->base), &car->bodyOffset);
        return GETP(car->hull);
    }
    return NULL;
}

/* port: returns the surface (a pointer, called through the vtable like
 * Thing_GetSurfaceScript); the decomp returned the 32-bit slot value */
void *CarA_GetSurfaceScript(CarA *car)
{
    return GETP(GETP(GETP(car->vehicleIface)->collider)->material);
}

/* Vtable GetCameraOrientation: chase camera index (of the car's cameras,
   split-screen ones when the view is split), up vector, eye and target. */
void CarA_GetCameraOrientationInfo(CarA *car, u32 index, void *out) {
    CameraInfo *info = out;
    CarACameras *cams;
    Vector4 offset __attribute__((aligned(16)));

    cams = &GETP(GETP(car->vehicleIface)->engine)->cameras;
    if (ViewManager_GetViewFormat() > 1) {
        cams = &GETP(GETP(car->vehicleIface)->engine)->splitCameras;
    }
    info->cameraCount = cams->count;
    if (cams->count == 0) {
        Thing_GetCameraOrientationInfo(car, index, info);
    } else {
        SETP(info->subMode, GETP(cams->list) + (index % cams->count) * 0xC);
        info->lookAhead = car->groundedScale;
        info->mtx.v[1] = car->groundNormal;
        if (info->mtx.v[1].x && info->mtx.v[1].y && info->mtx.v[1].z && PSVECSquareMag(&info->mtx.v[1])) {
            PSVECNormalize(&info->mtx.v[1], &info->mtx.v[1]);
        }
    }
    if (car->slaveWeaponRpm[0] < 10000) {
        info->lookAhead = 0.0f;
    }
    PSVECSubtract(&car->bodyOffset, (Vector4 *)GETP(GETP(car->vehicleIface)->engine), &offset);
    XformVec(THING_MTX(&car->base), &offset, &info->target);
    switch (GETP(GETP(car->vehicleIface)->collider)->type) {
    case 0:
        info->from = ((CollisionOBox *)CarA_GetOrientedCollision(car))->center;
        break;
    case 1:
        break;
    }
}

/* Vtable GetNearestNavPoint, searched on first use. */
s32 CarA_GetNearestNavPoint(CarA *car) {
    s32 timer;

    if (car->navPointIndex == 0xFFFFFFFF) {
        car->navPointIndex = NavHolder_FindIndexOfNearestNavPoint(&car->base.pos, -1);
        timer = RandomAdd_s32(300, 100);
        car->navUpdateTimer = timer;
        car->navUpdateTimer = timer;
    }
    return car->navPointIndex;
}

/* Returns the hit distance (maxDist if none): the decomp has it void, but
 * the original leaves PhysicalProperties_CollideWithRay's result in f1, and
 * RayHitsObjects_Aux (vtable 0x38) uses it. */
f32 CarA_CollideWithRay(CarA *car, Vector4 *start, Vector4 *end, CollisionInfo *hit, f32 maxDist) {
    Matrix4 local;
    f32 dist;

    local = *THING_MTX(&car->base);
    XformVec(THING_MTX(&car->base), &car->bodyOffset, &local.v[3]);
    dist = PhysicalProperties_CollideWithRay(GETP(GETP(car->vehicleIface)->collider), &local, start, end, maxDist, hit);
    if (hit != NULL && dist < maxDist) {
        hit->vel = car->base.vel;
    }
    return dist;
}


/* Picks up item thing by its type: 1-3, 6 wildcards into the rack, 7 a
   carried item onto the rack, 8 a pickup slave set, 9 counted, 10 humans
   only, 11 stunt points (humans) that also refill the wildcards. Returns
   whether it was taken. */
s8 CarA_ProcessItemPickup(CarA *car, void *thing) {
    /* ItemRecord fields ItemThing.h doesn't name yet */
    /* the FileStreaming QuadItem an item respawns from (ItemThing.spawner) */
    typedef struct PickupSpawner {
        u8 pad0[0x14];
        s32 respawnTime;
        s32 timer;
    } PickupSpawner;
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
        Thing base;
        u8 padAC[4];
        RackSlot slots[12];
        u8 current;
    } PickupRack;
    extern Vector4 g_zeroVector;
    extern char stunt_msg_buffer[];
    extern u8 g_stuntLossString[];  /* port: a table of string slots */
    Vector4 pos __attribute__((aligned(16)));
    u8 current;
    PickupRack *rack;
    s8 picked;
    PickupRack *wcRack;
    u32 points;
    ItemThing *item = thing;
    ItemRecord *def = GETP(item->record);
    PickupSpawner *spawner;
    u32 i;
    s16 ammo;

    picked = 0;
    Trigger_ItemPickedUp(car, item);
    car->pickupId = def->names.h;
    switch (def->type) {
    case 1:
    case 2:
    case 3:
    case 6:
        rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
        if (rack != NULL) {
            WildCardRack_Add(rack, (u8 *)def, item->ammo);
            Item_Release(item);
            if ((spawner = (PickupSpawner *)GETP(item->spawner)) != NULL) {
                spawner->timer = spawner->respawnTime;
            }
            picked = 1;
        }
        break;
    case 7:
        if (ThingManagerList_GetByType(&car->motionState.slaves, 4) == NULL) {
            rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
            pos.x = rack->base.pos.x;
            pos.y = GETP(GETP(car->vehicleIface)->engine)->itemHeight + (rack->base.pos.y + def->height);
            pos.z = rack->base.pos.z;
            pos.w = 1.0f;
            ThingManagerList_Remove(&g_thingManager_DARYL.activeList, item);
            ThingManagerList_Append(&car->motionState.slaves, item);
            item->base.slaveBone = rack->base.slaveBone;
            item->base.slaveTag = 0xEF;
            item->base.quat.w = 1.0f;
            item->base.quat.x = 0.0f;
            item->base.quat.y = 0.0f;
            item->base.quat.z = 0.0f;
            QuaternionToSubMatrix(&item->base.quat, THING_MTX(&item->base));
            item->base.pos = pos;
            item->base.pos.w = 1.0f;
            item->base.vel = g_zeroVector;
            Grid_FreeObject(GETP(item->base.gridObject));
            item->base.player.h = car->base.player.h;
            SETP(item->base.gridObject, NULL);
            picked = 1;
            item->base.trigger.h = item->spawner.h;
            rack->base.flags |= 0x400200;
            item->base.flags |= THING_FLAG_AT_REST;
        }
        break;
    case 8:
        if (GETP(car->vehicleIface)->numPickupSlaves > def->value) {
            MotionState_InitAllSlavesNow(&car->motionState, GETP(GETP(car->vehicleIface)->pickupSlaves) + def->value * 12 + 4,
                                         def->value);
        }
        Item_Release(item);
        if ((spawner = (PickupSpawner *)GETP(item->spawner)) != NULL) {
            spawner->timer = spawner->respawnTime;
        }
        picked = 1;
        break;
    case 9:
        car->pickupCount++;
        Item_Release(item);
        if ((spawner = (PickupSpawner *)GETP(item->spawner)) != NULL) {
            spawner->timer = spawner->respawnTime;
        }
        picked = 1;
        break;
    case 10:
        if (Player_IsHuman(GETP(car->base.player))) {
            Item_Release(item);
            if ((spawner = (PickupSpawner *)GETP(item->spawner)) != NULL) {
                spawner->timer = spawner->respawnTime;
            }
            picked = 1;
        }
        break;
    case 11:
        if (GETP(car->base.player) != NULL) {
            if (Player_IsHuman(GETP(car->base.player))) {
                wcRack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
                current = wcRack->current;
                points = def->value;
                CARA_PLAYER(car)->scoreDirty = 1;
                CARA_PLAYER(car)->stuntPoints += points;
                if (CARA_PLAYER(car)->stuntPoints > 99999) {
                    CARA_PLAYER(car)->stuntPoints = 99999;
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
            if ((spawner = (PickupSpawner *)GETP(item->spawner)) != NULL) {
                spawner->timer = spawner->respawnTime;
            }
            picked = 1;
        }
        break;
    }
    return picked;
}

/* Whether the car's description lies in [start, end) (handles), i.e. in
   data being unloaded. */
s32 CarA_Invalidate(CarA *car, u32 start, u32 end) {
    u32 info;

    info = car->vehicleIface.h;
    if ((info >= start) && (info < end)) {
        return 1;
    }
    return 0;
}