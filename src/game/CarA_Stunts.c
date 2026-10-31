/* The drone (g_droneVTable): a small remote-controlled vehicle, driven by
 * its player's stick on four suspension rays (PhysicalObject physics), or
 * flown along the NPP.ANI animation in state 2. */
#include "common.h"
#include "CarA.h"
#include "Destructible.h"

/* The drone's description (DroneInfo.props). */
typedef struct DroneProps {
    f32 inertia[3];         /* 0x00 per-axis factor of the inverse inertia */
    u8 pad0C[4];            /* 0x0C */
    u8 type;                /* 0x10 collider: 0 box */
    u8 pad11[3];            /* 0x11 */
    s32 health;             /* 0x14 */
    u8 pad18[8];            /* 0x18 */
    f32 mass;               /* 0x20 */
    f32 gravity;            /* 0x24 */
    u8 pad28[4];            /* 0x28 */
    f32 spinDamping;        /* 0x2C */
    u8 pad30[0x14];         /* 0x30 */
    PTR32(OBox) box;        /* 0x44 type 0: v[1] half sizes, v[2].x radius */
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

/* ThingManager_DARYL_CreateThing parameters for a drone. */
typedef struct DroneParams {
    Vector4 pos;            /* 0x00 */
    u8 pad10[0x20];         /* 0x10 */
    Quaternion quat;        /* 0x30 */
    PTR32(DroneInfo) info;  /* 0x40 */
} DroneParams;

typedef struct Drone {
    Thing base;             /* 0x00 player: whose stick steers it */
    u8 padAC[0x14];         /* 0xAC */
    PTR32(DroneInfo) info;  /* 0xC0 */
    u8 padC4[0x10];         /* 0xC4 */
    PTR32(DroneAnim) anim;  /* 0xD4 the flight path (state 2) */
    u8 padD8[8];            /* 0xD8 */
    Quaternion rot;         /* 0xE0 */
    Vector4 force;          /* 0xF0 */
    Vector4 baseForce;      /* 0x100 */
    Vector4 angMomentum;    /* 0x110 */
    Vector4 invInertia;     /* 0x120 diagonal of the inverse inertia */
    Vector4 angVel;         /* 0x130 */
    f32 suspension[4];      /* 0x140 last compression per wheel */
    u32 numContacts;        /* 0x150 wheels on the ground */
    /* 0x154: the drone's one chase camera, a CameraSubMode-like entry handed
       out by Drone_GetCameraOrientationInfo */
    u8 cameraType;          /* 0x154 */
    u8 pad155[3];           /* 0x155 */
    PTR32(f32) cameraParams; /* 0x158 -> cameraValues */
    s32 unk15C;             /* 0x15C */
    f32 cameraValues[5];    /* 0x160 */
    u8 pad174[8];           /* 0x174 */
    s32 unk17C;             /* 0x17C */
    f32 speed;              /* 0x180 along axis[2] */
    s32 unk184;             /* 0x184 */
    u8 pad188[8];           /* 0x188 */
    Vector4 wheels[4];      /* 0x190 suspension ray starts, body space */
    u8 pad1D0[0xC];         /* 0x1D0 */
    u8 state;               /* 0x1DC 0: airborne, 1: driving, 2: flying the animation */
    u8 pad1DD[7];           /* 0x1DD */
    f32 animTime;           /* 0x1E4 */
} Drone;

_Static_assert(offsetof(Drone, info) == 0xC0, "Drone.info");
_Static_assert(offsetof(Drone, anim) == 0xD4, "Drone.anim");
_Static_assert(offsetof(Drone, rot) == 0xE0, "Drone.rot");
_Static_assert(offsetof(Drone, angVel) == 0x130, "Drone.angVel");
_Static_assert(offsetof(Drone, numContacts) == 0x150, "Drone.numContacts");
_Static_assert(offsetof(Drone, cameraType) == 0x154, "Drone.cameraType");
_Static_assert(offsetof(Drone, cameraValues) == 0x160, "Drone.cameraValues");
_Static_assert(offsetof(Drone, speed) == 0x180, "Drone.speed");
_Static_assert(offsetof(Drone, wheels) == 0x190, "Drone.wheels");
_Static_assert(offsetof(Drone, state) == 0x1DC, "Drone.state");
_Static_assert(offsetof(Drone, animTime) == 0x1E4, "Drone.animTime");

extern Vector4 g_zeroVector;
extern f32 g_fDeltaTimeSec;

typedef struct DroneKeyFrame {
    Quaternion rot;
    Vector4 pos;
} DroneKeyFrame;

static inline void Mover_SetRotation(Drone *mover, Quaternion quat) {
    mover->rot = quat;
}

/* Vtable GetCameraOrientation: the drone's own camera, world up. */
void Drone_GetCameraOrientationInfo(Drone *drone, void *camera, void *out) {
    CameraInfo *info = out;

    info->cameraCount = 1;
    SETP(info->subMode, &drone->cameraType);
    info->lookAhead = 1.0f;
    info->mtx.v[1].x = 0.0f;
    info->mtx.v[1].y = 1.0f;
    info->mtx.v[1].z = 0.0f;
}

/* Vtable Init. */
s32 Drone_Init(Drone *drone, DroneParams *data) {
    OBox box;
    DroneProps *info;

    Thing_Init(&drone->base, (s32)HND(data));
    SETP(drone->anim, LoadDataChunkFile("NPP.ANI"));
    drone->animTime = GETP(drone->anim)->start;
    drone->base.player.h = 0;
    drone->base.pos = data->pos;
    QuaternionToSubMatrix(&data->quat, THING_MTX(&drone->base));
    drone->base.quat = data->quat;
    drone->unk15C = 0;
    drone->cameraType = 0;
    SETP(drone->cameraParams, drone->cameraValues);
    drone->cameraValues[1] = 20.0f;
    drone->cameraValues[4] = 20.0f;
    drone->cameraValues[0] = 75.0f;
    drone->cameraValues[3] = 40.0f;
    drone->cameraValues[2] = 5.0f;
    drone->info.h = data->info.h;
    info = GETP(GETP(drone->info)->props);
    switch (info->type) {
    case 0:
        drone->base.radius = GETP(info->box)->v[2].x;
        drone->base.collisionType = THING_COLLIDE_OBOX;
        break;
    case 1:
        break;
    }
    drone->base.health = GETP(GETP(drone->info)->props)->health;
    drone->speed = 0.0f;
    drone->baseForce = g_zeroVector;
    drone->force = drone->baseForce;
    drone->unk184 = 0;
    drone->unk17C = 0;
    info = GETP(GETP(drone->info)->props);
    switch (info->type) {
    case 0:
        box = *GETP(info->box);
        break;
    case 1:
        break;
    }
    drone->invInertia.x = 1.0f / (info->inertia[0] * info->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
    drone->invInertia.y = 1.0f / (GETP(GETP(drone->info)->props)->inertia[1] * GETP(GETP(drone->info)->props)->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
    drone->invInertia.z = 1.0f / (GETP(GETP(drone->info)->props)->inertia[2] * GETP(GETP(drone->info)->props)->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
    Thing_AddToGrid(drone, 0);
    drone->angMomentum = g_zeroVector;
    drone->angVel = g_zeroVector;
    drone->base.flags |= 4;
    drone->base.flags |= 0x80;
    drone->wheels[0].x = -3.39446f;
    drone->wheels[0].y = -0.48040998f;
    drone->wheels[0].z = -4.5236802f;
    drone->wheels[1].x = 3.39446f;
    drone->wheels[1].y = -0.48040998f;
    drone->wheels[1].z = -4.5236802f;
    drone->wheels[2].x = -3.39446f;
    drone->wheels[2].y = -0.48040998f;
    drone->wheels[2].z = 5.6166801f;
    drone->wheels[3].x = 3.39446f;
    drone->wheels[3].y = -0.48040998f;
    drone->wheels[3].z = 5.6166801f;
    drone->state = 0;
    return 1;
}


/* Four suspension rays: spring and damper forces, sideways friction while
   airborne-capable (state 0), then the drive: on all four wheels in state 1
   the player's stick turns and pushes it; settling on four wheels slowly
   enough switches to state 1, losing them all back to 0. */
void Drone_ComputeSuspensionForces(Drone *obj) {
    extern u32 g_iLastDeltaTimeMS;
    Drone *drone = obj;
    CollisionInfo hit __attribute__((aligned(16)));
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
    Player *input;

    drone->numContacts = 0;
    for (i = 0; i < 4; i++) {
        dir.x = -drone->base.axis[1].x;
        dir.y = -drone->base.axis[1].y;
        dir.z = -drone->base.axis[1].z;
        hit.vel = g_zeroVector;
        XformVec(THING_MTX(&drone->base), &drone->wheels[i], &start);
        start.x += 2.0f * drone->base.axis[1].x;
        start.y += 2.0f * drone->base.axis[1].y;
        start.z += 2.0f * drone->base.axis[1].z;
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
            PhysicalObject_ProcessForce(drone, &point, &drone->base.axis[1], (spring + damper) * PSVECDotProduct(&drone->base.axis[1], &hit.normal));
            if (drone->state == 0) {
                PSVECSubtract(&point, &drone->base.pos, &r);
                PSVECCrossProduct(&drone->angVel, &r, &spin);
                PSVECAdd(&drone->base.vel, &spin, &vel);
                slip.x = vel.x - drone->base.axis[2].x * drone->speed;
                slip.y = vel.y - drone->base.axis[2].y * drone->speed;
                slip.z = vel.z - drone->base.axis[2].z * drone->speed;
                ProjectVectorToNormal(&hit.normal, &slip);
                mag = MagnitudeVector(&slip);
                if (mag > 0.0f) {
                    PSVECDotProduct(&drone->base.axis[1], &hit.normal);
                    spring = 2.1f * spring;
                    PSVECCrossProduct(&drone->base.axis[2], &hit.normal, &side);
                    if (PSVECSquareMag(&side)) {
                        PSVECNormalize(&side, &side);
                    }
                    grip = PSVECDotProduct(&side, &drone->base.axis[1]);
                    grip = 1.1f * (1.0f - 1.8f * grip);
                    if (grip < 0.1f) {
                        grip = 0.1f;
                    } else if (grip > 1.0f) {
                        grip = 1.0f;
                    }
                    scale = grip * (((SurfaceMaterial *)PTR(hit.material))->friction * spring) / mag;
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
            ProjectVectorToNormal(&drone->base.axis[0], &drone->base.vel);
            drone->speed = PSVECDotProduct(&drone->base.vel, &drone->base.axis[2]);
            input = GETP(drone->base.player);
            if (input != NULL) {
                thrust = 12.1f * (f32)input->stickY;
                turn = 10.0f * (f32)-input->stickX;
                ProjectVectorToNormal(&drone->base.axis[1], &drone->angMomentum);
                drone->angMomentum.x += turn * drone->base.axis[1].x;
                drone->angMomentum.y += turn * drone->base.axis[1].y;
                drone->angMomentum.z += turn * drone->base.axis[1].z;
                if (_fabs(drone->speed) < 100.0f) {
                    drone->force.x += drone->base.axis[2].x * thrust;
                    drone->force.y += drone->base.axis[2].y * thrust;
                    drone->force.z += drone->base.axis[2].z * thrust;
                }
            }
        } else if (_fabs(PSVECDotProduct(&drone->base.axis[0], &drone->base.vel)) < 2.0f) {
            if (_fabs(PSVECDotProduct(&drone->base.axis[1], &drone->angVel)) < 5.0f) {
                drone->state = 1;
            }
        }
    } else if (drone->numContacts == 0) {
        drone->state = 0;
    }
}

/* Vtable PreMove: in state 2 follows the animation (velocity and angular
   velocity from the key frame), else gravity, suspension and integration. */
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
        drone->base.vel.x = (key.pos.x - drone->base.pos.x) / g_fDeltaTimeSec;
        drone->base.vel.y = (key.pos.y - drone->base.pos.y) / g_fDeltaTimeSec;
        drone->base.vel.z = (key.pos.z - drone->base.pos.z) / g_fDeltaTimeSec;
        quat = key.rot;
        inv = *THING_MTX(&drone->base);
        Mover_SetRotation(drone, quat);
        QuaternionToSubMatrix(&quat, &rot);
        TransposeSubMatrix(&inv);
        MultiplySubMatrix(&inv, &rot, &rel);
        MultiplySubMatrix(THING_MTX(&drone->base), &rel, &world);
        SubMatrixToQuaternion(&rel, (Vector4 *)&delta);
        GenerateSpinAngleFromQuaternion((Vector4 *)&delta, &drone->angVel, &angle);
        angle /= g_fDeltaTimeSec;
        drone->angVel.x *= angle;
        drone->angVel.y *= angle;
        drone->angVel.z *= angle;
        drone->base.flags &= ~0x10;
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

/* Vtable DynamicCollideImpact: a push along the contact normal (not while
   flying the animation); dies at zero health. */
void Drone_DynamicCollideImpact(Drone *drone, CollisionInfo *collision, f32 force)
{
    Vector4 normal;
    Vector4 pos;
    Vector4 impulse;
    Vector4 torque;
    Vector4 r;
    Vector4 scaled;
    Vector4 local __attribute__((aligned(16)));
    f32 scale;

    normal = collision->normal;
    pos = collision->pos;
    if (drone->state != 2)
    {
        scale = force / GETP(GETP(drone->info)->props)->mass;
        drone->base.vel.x += normal.x * scale;
        drone->base.vel.y += normal.y * scale;
        drone->base.vel.z += normal.z * scale;
        PSVECSubtract(&pos, &drone->base.pos, &r);
        impulse.x = force * normal.x;
        impulse.y = force * normal.y;
        impulse.z = force * normal.z;
        PSVECCrossProduct(&r, &impulse, &torque);
        drone->angMomentum.x += torque.x;
        drone->angMomentum.y += torque.y;
        drone->angMomentum.z += torque.z;
        InverseXformVecOrientation(THING_MTX(&drone->base), &drone->angMomentum, &local);
        scaled.x = drone->invInertia.x * local.x;
        scaled.y = drone->invInertia.y * local.y;
        scaled.z = drone->invInertia.z * local.z;
        XformVecOrientation(THING_MTX(&drone->base), &scaled, &drone->angVel);
        if (drone->base.health <= 0)
        {
            Thing_ProcessEndEvent(&drone->base, (u8 *)GETP(GETP(drone->info)->props), collision, &drone->angVel);
            Thing_Release(drone);
        }
        drone->speed = 0.0f;
        drone->unk17C = 0;
    }
}

/* Vtable DynamicRotation: in state 2 snaps to the animation's rotation once
   per key (flags & 0x10), else the PhysicalObject rotation. */
void Drone_DynamicRotation(Drone *drone, f32 dt) {
    Quaternion quat __attribute__((aligned(16)));
    Matrix4 mtx;

    if (drone->state == 2) {
        if (!(drone->base.flags & 0x10)) {
            drone->base.flags |= 0x10;
            quat = drone->rot;
            QuaternionToSubMatrix(&quat, &mtx);
            mtx.v[3].x = drone->base.pos.x;
            mtx.v[3].y = drone->base.pos.y;
            mtx.v[3].z = drone->base.pos.z;
            drone->base.quat = quat;
            *THING_MTX(&drone->base) = mtx;
        }
    } else {
        PhysicalObject_DynamicRotation((u8 *)drone, dt);
    }
}

