/* Debris: loose pieces (car parts, gibs) that fly, spin and bounce off the
 * terrain until they come to rest, and are released when their life is up. */
#include "common.h"
#include "Debris.h"


s32 Debris_Init(struct Debris *debris, struct DebrisPartParams *data)
{
    Thing *thing = &debris->base;

    Thing_Init(thing, 0);
    SETP(debris->geometry, GETP(data->geometry));
    debris->spinAxis = data->spinAxis;
    thing->quat = data->quat;
    thing->vel = data->vel;
    data->pos.w = 1.0f;
    thing->pos = data->pos;
    debris->life = data->life;
    debris->macroTime = -1;
    debris->macroPos = thing->pos;
    thing->collisionType = THING_COLLIDE_RAY;
    debris->gravity = data->gravity;
    debris->mass = data->mass;
    if (data->moves == 0) {
        thing->flags |= 2;
    }
    thing->flags |= 0x01000000;
    if (GETP(debris->geometry) != NULL) {
        /* the geometry's bounding radius */
        thing->radius = *(f32 *)((u8 *)GETP(debris->geometry) + 0x1C);
    }
    return 1;
}

/* Simulate `dt` seconds ahead: gravity, then a ray along the motion; a hit
 * reflects and halves the velocity and re-randomises the spin (stopping it
 * when slow). Sets macroPos and macroVel for Debris_Move to glide along. */
void Debris_MacroStep(struct Debris *debris, f32 dt) {
    extern f32 g_fDeltaTimeSec;
    Thing *thing = &debris->base;
    f32 savedDt = g_fDeltaTimeSec;
    CollisionInfo hit __attribute__((aligned(16)));
    Vector4 newPos __attribute__((aligned(16)));
    f32 t;
    u8 bounced;
    f32 d;
    f32 spin;

    if (thing->flags & 1) {
        return;
    }
    TimeHandler_SetTimeElapsedSec_f32(dt);
    thing->vel.y = debris->gravity * g_fDeltaTimeSec + thing->vel.y;
    newPos.w = 1.0f;
    if (!(thing->flags & 2)) {
        t = g_fDeltaTimeSec;
        bounced = 0;
        if (RayHitsTerrain_Aux3(&thing->pos, thing, &t, &hit)) {
            AccumulateVector(&thing->vel, &thing->pos, &newPos, t * g_fDeltaTimeSec);
            bounced = 1;
            d = 2.0f * PSVECDotProduct(&thing->vel, &hit.normal);
            thing->vel.x = thing->vel.x - d * hit.normal.x;
            thing->vel.y = thing->vel.y - d * hit.normal.y;
            thing->vel.z = thing->vel.z - d * hit.normal.z;
            thing->vel.x *= 0.5f;
            thing->vel.y *= 0.5f;
            thing->vel.z *= 0.5f;
            newPos.x = g_fDeltaTimeSec * (thing->vel.x * (1.0f - t)) + newPos.x;
            newPos.y = g_fDeltaTimeSec * (thing->vel.y * (1.0f - t)) + newPos.y;
            newPos.z = g_fDeltaTimeSec * (thing->vel.z * (1.0f - t)) + newPos.z;
        } else {
            newPos.x = thing->vel.x * g_fDeltaTimeSec + thing->pos.x;
            newPos.y = thing->vel.y * g_fDeltaTimeSec + thing->pos.y;
            newPos.z = thing->vel.z * g_fDeltaTimeSec + thing->pos.z;
        }
        if (bounced) {
            if (thing->vel.z * thing->vel.z + (thing->vel.x * thing->vel.x + thing->vel.y * thing->vel.y) < 1.0f) {
                thing->vel.x = thing->vel.y = thing->vel.z = debris->spinAxis.w = 0.0f;
                thing->flags |= 1;
            } else if (thing->vel.x * thing->vel.x + thing->vel.z * thing->vel.z > 2e-05f) {
                spin = debris->spinAxis.w;
                debris->spinAxis.w *= 0.8f;
                RandomNumberGenerator_GetRandomVector(&debris->spinAxis);
                debris->spinAxis.w = spin;
            } else {
                debris->spinAxis.w = 0.0f;
            }
        }
        debris->macroPos = newPos;
        debris->macroVel.x = (1000.0f * (newPos.x - thing->pos.x)) / debris->macroTime;
        debris->macroVel.y = (1000.0f * (newPos.y - thing->pos.y)) / debris->macroTime;
        debris->macroVel.z = (1000.0f * (newPos.z - thing->pos.z)) / debris->macroTime;
    }
    TimeHandler_SetTimeElapsedSec_f32(savedDt);
}

/* Glide towards macroPos, starting a new macro step (500 ms, less near the
 * camera) when the current one is over; spin; expire. */
s32 Debris_Move(struct Debris *debris) {
    extern s32 g_iDeltaTimeMS;
    extern f32 g_fDeltaTimeMS;
    extern f32 g_fDeltaTimeSec;
    Quaternion result;
    Quaternion rot;
    f32 dt;
    f32 dist;
    Thing *thing = &debris->base;

    if (debris->macroTime <= 0) {
        dt = 500.0f;
        thing->pos = debris->macroPos;
        dist = Sqrt(Gfx_GetMinCameraDistSquared(&thing->pos));
        if (dist > 900.0f) {
            debris->macroTime = RandomAdd_s32(500, 50);
        } else {
            dt = ((dist - 60.0f) / 840.0f) * dt + RandomAdd(0.0f, 50.0f);
            debris->macroTime = dt;
        }
        if (debris->macroTime < g_iDeltaTimeMS) {
            debris->macroTime = g_iDeltaTimeMS;
            dt = g_fDeltaTimeMS;
        }
        Debris_MacroStep(debris, 0.001f * dt);
    }
    dt = g_fDeltaTimeMS;
    if (debris->macroTime < g_iDeltaTimeMS) {
        dt = debris->macroTime;
    }
    dt = 0.001f * dt;
    thing->pos.x += debris->macroVel.x * dt;
    thing->pos.y += debris->macroVel.y * dt;
    thing->pos.z += debris->macroVel.z * dt;
    GenerateQuaternion(&debris->spinAxis, &rot, debris->spinAxis.w * g_fDeltaTimeSec);
    QuaternionMult(&thing->quat, &rot, &result);
    thing->quat = result;
    debris->macroTime -= g_iDeltaTimeMS;
    if (debris->life < 0) {
        Thing_Release(debris);
        return 0;
    }
    debris->life -= g_iDeltaTimeMS;
    QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
    return 1;
}

void Debris_Draw(struct Debris *debris) {
    if (GETP(debris->geometry) != NULL) {
        if (debris->base.flags & 0x20000000) {
            /* a slave: its matrix is cached */
            Gfx_DrawGeometry(HND(GETP(debris->geometry)), HND(THING_MTX(&debris->base)));
        }
        else {
            Gfx_DrawGeometry_MatrixNotCached(HND(GETP(debris->geometry)), HND(THING_MTX(&debris->base)));
        }
    }
}

f32 Debris_GetMass(struct Debris *debris) {
    return debris->mass;
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_debrisVTable[17];

__attribute__((constructor)) static void PortInit_g_debrisVTable(void) {
    static void *const fns[] = {
        (void *)Debris_Init,
        (void *)Thing_Release,
        (void *)Thing_PreMove,
        (void *)Debris_Move,
        (void *)Debris_Draw,
        (void *)Debris_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransform,
        (void *)Thing_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Thing_DynamicRotation,
        (void *)Thing_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Thing_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_debrisVTable[i] = HND(fns[i]);
    }
}
