/* Smart projectiles: homing projectiles (ProjectileInfo.guidance 1, 2). A
 * Projectile with its own PreMove/Move/DynamicCollideImpact: it locks on
 * to the best target in its cone, steers towards a lead point, and hits
 * cars by proximity. */
#include "common.h"
#include "Projectile.h"


extern f32 g_fDeltaTimeSec;
extern s32 g_iDeltaTimeMS;
extern Vector4 g_zeroVector;


/* The target closest to the nose within the smart params' cone (fov),
 * beyond minRange and in line of sight; NULL if none. */
void SmartProjectileA_AquireTarget(struct Projectile *proj) {
    f32 t;
    Vector4 local __attribute__((aligned(16)));
    Thing *targets[32];
    Thing *source;
    u32 count;
    u32 i;
    u32 bestIdx;
    Vector4 *pos;
    f32 best;
    f32 dist;
    f32 angle;
    SmartProjParams *smart = GETP(GETP(proj->info)->smart);

    count = GetAllTargets(targets);
    source = PTR(Thing_GetSource(&proj->base));
    best = 180.0f;
    bestIdx = count;
    for (i = 0; i < count; i++) {
        dist = PSVECSquareDistance(&proj->base.pos, &targets[i]->pos);
        /* vtable GetOrientedCollision: the shape's centre comes first */
        pos = THING_VFN(targets[i], THING_V_GET_ORIENTED_COLLISION, Vector4 *(*)(Thing *))(targets[i]);
        t = 1.0f;
        if (targets[i] != &proj->base && targets[i] != source &&
            !(dist < smart->minRange * smart->minRange) &&
            !RayHitsTerrain(&proj->base.pos, pos, &t, NULL)) {
            InverseXformVec(THING_MTX(&proj->base), pos, &local);
            if (smart->invertZ) {
                local.z *= -1.0f;
            }
            if (!(local.z < 1.0f)) {
                angle = atan2(Sqrt(local.x * local.x + local.y * local.y), local.z);
                if (!(angle > 0.5f * (0.017453292f * smart->fov)) && angle < best) {
                    best = angle;
                    bestIdx = i;
                }
            }
        }
    }
    if (bestIdx < count) {
        SETP(proj->target, targets[bestIdx]);
    } else {
        SETP(proj->target, NULL);
    }
}

/* Turn the nose to the velocity, accelerate (up to maxSpeed), and after the
 * steering delay steer for the target's lead point: gradually beyond
 * dropRange, directly within it. A car target passed (behind the nose)
 * within proximityRadius is hit: impactFx stuck on it, its
 * DynamicCollideImpact called, and the projectile released. */
s32 SmartProjectileA_PreMove(struct Projectile *proj) {
  DebrisParams p;
  CollisionInfo h;
  Thing *cars[16];
  Vector4 cross;
  Vector4 axis;
  Quaternion quat;
  Quaternion result;
  Vector4 dir;
  Vector4 local;
  Vector4 aim __attribute__((aligned(16)));
  Thing *s;
  Thing *thing = &proj->base;
  Thing *target;
  ProjectileInfo *info = GETP(proj->info);
  SmartProjParams *smart = GETP(info->smart);
  u32 id, cnt;
  u32 i;
  s8 fnd;
  f32 speed, dot, ang, inv, ns, dist, lead;
  f32 tO, tI, mt, gain;
  speed = MagnitudeVector(&thing->vel);
  PSVECCrossProduct(&thing->vel, &thing->axis[2], &cross);
  if (!FloatsEqual(MagnitudeVector(&cross), 0.0f, 0.05f)) {
    inv = 1.0f / speed;
    dir.x = thing->vel.x * inv;
    dir.y = thing->vel.y * inv;
    dir.z = thing->vel.z * inv;
    dot = PSVECDotProduct(&dir, &thing->axis[2]);
    if (_fabs(dot) < 0.99f) {
      ang = (f32)acos((double)dot);
      PSVECCrossProduct(&thing->axis[2], &dir, &axis);
      GenerateQuaternion(&axis, &quat, ang);
      QuaternionMult(&quat, &thing->quat, &result);
      thing->quat = result;
      QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
    }
  }
  ns = info->thrust * g_fDeltaTimeSec + speed;
  if (ns > info->maxSpeed)
    ns = info->maxSpeed;
  proj->timer -= (u16)g_iDeltaTimeMS;
  if (proj->timer > 0) {
    /* as the original: still waiting, but the delay is then over */
    proj->timer = -1;
    return 0;
  }
  if (GETP(proj->target)) {
    target = GETP(proj->target);
    tO = smart->turnOut;
    tI = smart->turnIn;
    mt = smart->turnMax;
    dist = Sqrt(PSVECSquareDistance(&thing->pos, &target->pos));
    if (ns)
      lead = dist / ns;
    else
      lead = 0.0f;
    THING_VFN(target, THING_V_GET_TARGET_POS, void (*)(Thing *, Vector4 *))(target, &aim);
    if (lead > 2.0f)
      lead = 0.3f * (lead - 2.0f) + 2.0f;
    if (lead > 0.2f)
      lead = 0.2f;
    aim.x += target->vel.x * lead;
    aim.y += target->vel.y * lead;
    aim.z += target->vel.z * lead;
    InverseXformVec(THING_MTX(thing), &aim, &local);
    if (local.z < 0.0f) {
      cnt = ThingManager_GetActiveCars(cars, 16);
      fnd = 0;
      for (i = 0; i < cnt; i++)
        if (cars[i] == target) {
          fnd = 1;
          break;
        }
      if (fnd && dist < info->proximityRadius) {
        id = info->impactFx;
        if (id) {
          p.debris = id;
          p.quat = *(Vector4 *)&thing->quat;
          p.owner = thing->player.h;
          p.unk48 = 0.0f;
          p.unk4C = 0.0f;
          p.angVel = g_zeroVector;
          InverseXformVec(THING_MTX(target), &thing->pos, &p.pos);
          s = ThingManager_DARYL_CreateSlave(1, &p, 0xFF);
          if (s) {
            s->slaveTag = 0;
            ThingManagerList_Append(&target->slaves, s);
          }
        }
        h.thingA = HND(proj);
        h.thingB = HND(target);
        h.material = 0;
        h.pos = thing->pos;
        PSVECSubtract(&thing->pos, &target->pos, &h.normal);
        if (PSVECSquareMag(&h.normal))
          PSVECNormalize(&h.normal, &h.normal);
        /* (the decomp passed these as (0.0f, damage); same registers on GC) */
        THING_VFN(target, THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(Thing *, CollisionInfo *, s32, f32))(
            target, &h, info->damageBase, 0.0f);
        Projectile_Release(proj);
        thing->flags |= 2;
      } else
        SETP(proj->target, NULL);
    } else {
      if (dist > smart->dropRange) {
        proj->turnZ = 0.8f;
        local.x /= local.z;
        local.y /= local.z;
        if (proj->turnX * local.x < 0.0f)
          proj->turnX += g_fDeltaTimeSec * (tI * local.x);
        else
          proj->turnX += g_fDeltaTimeSec * (tO * local.x);
        if (proj->turnY * local.y > 0.0f)
          proj->turnY += g_fDeltaTimeSec * (-tI * local.y);
        else
          proj->turnY += g_fDeltaTimeSec * (-tO * local.y);
      } else if (local.z > 0.0f) {
        gain = smart->gain;
        inv = 1.0f / local.z;
        proj->turnX = inv * (g_fDeltaTimeSec * (gain * local.x));
        proj->turnY = inv * (g_fDeltaTimeSec * (-gain * local.y));
        proj->turnZ = 0.0f;
      }
      if (proj->turnX > mt)
        proj->turnX = mt;
      if (proj->turnX < -mt)
        proj->turnX = -mt;
      if (proj->turnY > mt)
        proj->turnY = mt;
      if (proj->turnY < -mt)
        proj->turnY = -mt;
    }
  } else {
    proj->turnZ = 0.0f;
    proj->turnY = 0.0f;
    proj->turnX = 0.0f;
  }
  thing->vel.x = thing->axis[2].x * ns;
  thing->vel.y = thing->axis[2].y * ns;
  thing->vel.z = thing->axis[2].z * ns;
  return 1;
}

/* Projectile_MoveBase, then turn by the turn rates (about the side, up and
 * nose axes). */
s32 SmartProjectileA_Move(struct Projectile *proj) {
  Quaternion qy;
  Quaternion qz;
  Quaternion qx;
  Quaternion t1;
  Quaternion t2;
  Thing *thing = &proj->base;
  Projectile_MoveBase(proj);
  GenerateQuaternion(&thing->axis[1], &qx, proj->turnX * g_fDeltaTimeSec);
  GenerateQuaternion(&thing->axis[0], &qy, proj->turnY * g_fDeltaTimeSec);
  GenerateQuaternion(&thing->axis[2], &qz, proj->turnZ * g_fDeltaTimeSec);
  QuaternionMult(&thing->quat, &qx, &t1);
  QuaternionMult(&qy, &t1, &t2);
  QuaternionMult(&qz, &t2, &thing->quat);
  QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
  thing->flags |= 0x80;
  thing->flags |= 0x100;
  Thing_CollideWithForceBoxes(thing);
  return 1;
}

/* With a target, glancing off the terrain (a contact with only one thing,
 * hit less than 60 degrees head-on) turns it along the surface; anything
 * else is handled as for a plain projectile. */
void SmartProjectileA_DynamicCollideImpact(struct Projectile *proj, CollisionInfo *other, s32 damage, f32 force) {
    Quaternion q __attribute__((aligned(16)));
    Quaternion result __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    f32 dot;
    f32 angle;
    f32 speed;
    Thing *thing = &proj->base;

    proj->bounces++;
    if (proj->bounces > 8) {
        thing->flags |= 0x40;
        return;
    }
    if (other != NULL && proj->target.h != 0 &&
        (other->thingA == 0 || other->thingB == 0)) {
        dot = PSVECDotProduct(&other->normal, &thing->axis[2]);
        if (dot > -0.5f) {
            speed = MagnitudeVector(&thing->vel);
            angle = acos(dot);
            PSVECCrossProduct(&thing->axis[2], &other->normal, &axis);
            GenerateQuaternion(&axis, &q, angle - 1.5707964f);
            QuaternionMult(&q, &thing->quat, &result);
            thing->quat = result;
            QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
            thing->vel.x = speed * thing->axis[2].x;
            thing->vel.y = speed * thing->axis[2].y;
            thing->vel.z = speed * thing->axis[2].z;
            return;
        }
    }
    Projectile_DynamicCollideImpact(proj, other, damage, force);
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_smartProjectileAVTable[17];

__attribute__((constructor)) static void PortInit_g_smartProjectileAVTable(void) {
    static void *const fns[] = {
        (void *)Projectile_Init,
        (void *)Projectile_Release,
        (void *)SmartProjectileA_PreMove,
        (void *)SmartProjectileA_Move,
        (void *)Projectile_Draw,
        (void *)Projectile_GetMass,
        (void *)SmartProjectileA_DynamicCollideImpact,
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
        g_smartProjectileAVTable[i] = HND(fns[i]);
    }
}
