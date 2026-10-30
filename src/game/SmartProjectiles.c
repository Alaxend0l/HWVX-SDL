#include "common.h"


extern f32 g_fDeltaTimeSec;
extern s32 g_iDeltaTimeMS;
extern Vector4 g_zeroVector;


typedef struct SmartProjParams {
  u8 unk0;
  u8 invertZ;
  u8 pad2[0x4 - 0x2];
  f32 turnMax;
  f32 turnOut;
  f32 turnIn;
  f32 dropRange;
  f32 gain;
  u8 pad18[0x1C - 0x18];
  f32 fov;
  f32 minRange;
} SmartProjParams;

typedef struct SmartProjInfo {
  u8 pad0[0x1C];
  s32 damage;
  u8 pad20[0x34 - 0x20];
  f32 f34;
  u8 pad38[0x40 - 0x38];
  f32 speedGain;
  f32 maxSpeed;
  u8 pad48[0x74 - 0x48];
  u32 slaveId;
  u8 pad78[0xA8 - 0x78];
  PTR32(SmartProjParams) params;
} SmartProjInfo;

typedef struct SmartTarget {
  Vector4 vec00;
  u8 mtx[0x20];
  u8 pad30[0x40 - 0x30];
  Vector4 pos;
  u8 pad50[0x88 - 0x50];
  u32 list;
  PTR32(PTR32(void)) vtable;
} SmartTarget;

typedef struct SmartProjVTable {
  u8 pad0[0x18];
  FPTR32(void, (SmartTarget *obj, void *hit, f32 impulse, s32 damage)) applyImpulse;
  u8 pad1C[0x3C - 0x1C];
  FPTR32(void, (SmartTarget *obj, Vector4 *out)) getPos;
} SmartProjVTable;

typedef struct CreateSlaveParams {
  Vector4 a;
  Quaternion b;
  Vector4 c;
  Vector4 zero;
  u32 id;
  s32 player;
  f32 f0;
  f32 f1;
} CreateSlaveParams;

typedef struct HitRecord {
  Vector4 pos;
  Vector4 normal;
  PTR32(u8) thing;
  PTR32(u8) other;
  s32 time;
  u8 pad2C[0x50 - 0x2C];
} HitRecord;

typedef struct SmartProjectile {
  Vector4 vel;
  u8 mtx[0x20];
  Vector4 dir;
  Vector4 pos;
  Quaternion quat;
  u8 pad60[0x74 - 0x60];
  u32 flags;
  u8 pad78[0x94 - 0x78];
  s32 player;
  u8 pad98[0x120 - 0x98];
  PTR32(SmartProjInfo) info;
  u8 pad124[0x14C - 0x124];
  PTR32(SmartTarget) target;
  f32 turnX;
  f32 turnY;
  f32 turnZ;
  u16 hits15C;
  s16 timer;
} SmartProjectile;

void SmartProjectileA_AquireTarget(SmartProjectile *proj) {
    f32 t;
    Vector4 local __attribute__((aligned(16)));
    SmartTarget *targets[32];
    SmartTarget *source;
    u32 count;
    u32 i;
    u32 bestIdx;
    Vector4 *pos;
    f32 best;
    f32 dist;
    f32 angle;

    count = GetAllTargets(targets);
    source = PTR(Thing_GetSource(proj));
    best = 180.0f;
    bestIdx = count;
    for (i = 0; i < count; i++) {
        dist = PSVECSquareDistance(&proj->pos, &targets[i]->pos);
        pos = ((Vector4 *(*)(SmartTarget *))GETP(GETP(targets[i]->vtable)[0x40 / 4]))(targets[i]);
        t = 1.0f;
        if (targets[i] != (SmartTarget *)proj && targets[i] != source &&
            !(dist < GETP(GETP(proj->info)->params)->minRange * GETP(GETP(proj->info)->params)->minRange) &&
            !RayHitsTerrain(&proj->pos, pos, &t, NULL)) {
            InverseXformVec(proj->mtx, pos, &local);
            if (GETP(GETP(proj->info)->params)->invertZ) {
                local.z *= -1.0f;
            }
            if (!(local.z < 1.0f)) {
                angle = atan2(Sqrt(local.x * local.x + local.y * local.y), local.z);
                if (!(angle > 0.5f * (0.017453292f * GETP(GETP(proj->info)->params)->fov)) && angle < best) {
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

s32 SmartProjectileA_PreMove(SmartProjectile *proj) {
  CreateSlaveParams p;
  HitRecord h;
  u8 *cars[16];
  Vector4 cross;
  Vector4 axis;
  Quaternion quat;
  Quaternion result;
  Vector4 dir;
  Vector4 local;
  Vector4 aim __attribute__((aligned(16)));
  u8 *s;
  u32 id, cnt;
  u32 i;
  s8 fnd;
  f32 speed, dot, ang, inv, ns, dist, lead;
  f32 tO, tI, mt, gain;
  speed = MagnitudeVector(&proj->vel);
  PSVECCrossProduct(&proj->vel, &proj->dir, &cross);
  if (!FloatsEqual(MagnitudeVector(&cross), 0.0f, 0.05f)) {
    inv = 1.0f / speed;
    dir.x = proj->vel.x * inv;
    dir.y = proj->vel.y * inv;
    dir.z = proj->vel.z * inv;
    dot = PSVECDotProduct(&dir, &proj->dir);
    if (_fabs(dot) < 0.99f) {
      ang = (f32)acos((double)dot);
      PSVECCrossProduct(&proj->dir, &dir, &axis);
      GenerateQuaternion(&axis, &quat, ang);
      QuaternionMult(&quat, &proj->quat, &result);
      proj->quat = result;
      QuaternionToSubMatrix(&proj->quat, &proj->mtx[0]);
    }
  }
  ns = GETP(proj->info)->speedGain * g_fDeltaTimeSec + speed;
  if (ns > GETP(proj->info)->maxSpeed)
    ns = GETP(proj->info)->maxSpeed;
  proj->timer -= (u16)g_iDeltaTimeMS;
  if (proj->timer > 0) {
    proj->timer = -1;
    return 0;
  }
  if (GETP(proj->target)) {
    tO = GETP(GETP(proj->info)->params)->turnOut;
    tI = GETP(GETP(proj->info)->params)->turnIn;
    mt = GETP(GETP(proj->info)->params)->turnMax;
    dist = Sqrt(PSVECSquareDistance(&proj->pos, &GETP(proj->target)->pos));
    if (ns)
      lead = dist / ns;
    else
      lead = 0.0f;
    GETP(((SmartProjVTable *)GETP(GETP(proj->target)->vtable))->getPos)(GETP(proj->target), &aim);
    if (lead > 2.0f)
      lead = 0.3f * (lead - 2.0f) + 2.0f;
    if (lead > 0.2f)
      lead = 0.2f;
    aim.x += GETP(proj->target)->vec00.x * lead;
    aim.y += GETP(proj->target)->vec00.y * lead;
    aim.z += GETP(proj->target)->vec00.z * lead;
    InverseXformVec(&proj->mtx[0], &aim, &local);
    if (local.z < 0.0f) {
      cnt = ThingManager_GetActiveCars(cars, 16);
      fnd = 0;
      for (i = 0; i < cnt; i++)
        if (cars[i] == (u8 *)GETP(proj->target)) {
          fnd = 1;
          break;
        }
      if (fnd && dist < GETP(proj->info)->f34) {
        id = GETP(proj->info)->slaveId;
        if (id) {
          p.id = id;
          p.b = proj->quat;
          p.player = proj->player;
          p.f0 = 0.0f;
          p.f1 = 0.0f;
          p.zero = g_zeroVector;
          InverseXformVec(&GETP(proj->target)->mtx[0], &proj->pos, &p.a);
          s = ThingManager_DARYL_CreateSlave(1, &p, 0xFF);
          if (s) {
            s[0x83] = 0;
            ThingManagerList_Append(&GETP(proj->target)->list, s);
          }
        }
        SETP(h.thing, (u8 *)proj);
        SETP(h.other, (u8 *)GETP(proj->target));
        h.time = 0;
        h.pos = proj->pos;
        PSVECSubtract(&proj->pos, &GETP(proj->target)->pos, &h.normal);
        if (PSVECSquareMag(&h.normal))
          PSVECNormalize(&h.normal, &h.normal);
        GETP(((SmartProjVTable *)GETP(GETP(proj->target)->vtable))->applyImpulse)(
            GETP(proj->target), &h, 0.0f, GETP(proj->info)->damage);
        Projectile_Release((u8 *)proj);
        proj->flags |= 2;
      } else
        SETP(proj->target, NULL);
    } else {
      if (dist > GETP(GETP(proj->info)->params)->dropRange) {
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
        gain = GETP(GETP(proj->info)->params)->gain;
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
  proj->vel.x = proj->dir.x * ns;
  proj->vel.y = proj->dir.y * ns;
  proj->vel.z = proj->dir.z * ns;
  return 1;
}

s32 SmartProjectileA_Move(SmartProjectile *proj) {
  Quaternion qy;
  Quaternion qz;
  Quaternion qx;
  Quaternion t1;
  Quaternion t2;
  Projectile_MoveBase(proj);
  GenerateQuaternion((Vector4 *)&proj->mtx[0x10], &qx, proj->turnX * g_fDeltaTimeSec);
  GenerateQuaternion((Vector4 *)&proj->mtx[0], &qy, proj->turnY * g_fDeltaTimeSec);
  GenerateQuaternion(&proj->dir, &qz, proj->turnZ * g_fDeltaTimeSec);
  QuaternionMult(&proj->quat, &qx, &t1);
  QuaternionMult(&qy, &t1, &t2);
  QuaternionMult(&qz, &t2, &proj->quat);
  QuaternionToSubMatrix(&proj->quat, &proj->mtx[0]);
  proj->flags |= 0x80;
  proj->flags |= 0x100;
  Thing_CollideWithForceBoxes(proj);
  return 1;
}

void SmartProjectileA_DynamicCollideImpact(u8 *proj, u8 *other, void *arg2, f32 arg3) {
    Vector4 q __attribute__((aligned(16)));
    Vector4 result __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    f32 dot;
    f32 angle;
    f32 speed;

    (*(u16 *)(proj + 0x15C))++;
    if (*(u16 *)(proj + 0x15C) > 8) {
        *(u32 *)(proj + 0x74) |= 0x40;
        return;
    }
    if (other != NULL && *(u32 *)(proj + 0x14C) != 0 &&
        (*(u32 *)(other + 0x20) == 0 || *(u32 *)(other + 0x24) == 0)) {
        dot = PSVECDotProduct(other + 0x10, proj + 0x30);
        if (dot > -0.5f) {
            speed = MagnitudeVector(proj);
            angle = acos(dot);
            PSVECCrossProduct((Vector4 *)(proj + 0x30), (Vector4 *)(other + 0x10), &axis);
            GenerateQuaternion(&axis, &q, angle - 1.5707964f);
            QuaternionMult(&q, (Vector4 *)(proj + 0x50), &result);
            *(Vector4 *)(proj + 0x50) = result;
            QuaternionToSubMatrix(proj + 0x50, proj + 0x10);
            ((Vector4 *)proj)->x = speed * ((Vector4 *)(proj + 0x30))->x;
            ((Vector4 *)proj)->y = speed * ((Vector4 *)(proj + 0x30))->y;
            ((Vector4 *)proj)->z = speed * ((Vector4 *)(proj + 0x30))->z;
            return;
        }
    }
    Projectile_DynamicCollideImpact(proj, other, HND(arg2), arg3);
}

void *g_smartProjectileAVTable[] = {
    Projectile_Init,
    Projectile_Release,
    SmartProjectileA_PreMove,
    SmartProjectileA_Move,
    Projectile_Draw,
    Projectile_GetMass,
    SmartProjectileA_DynamicCollideImpact,
    Thing_GetCameraOrientationInfo,
    Thing_GetInterfaceInfo,
    Thing_SlaveTransform,
    Thing_GetSurfaceScript,
    Thing_GetNearestNavPoint,
    Thing_GetInverseLocalInertiaTensor,
    Thing_DynamicRotation,
    Thing_CollideWithRay,
    Thing_GetTargetPos,
    Thing_GetOrientedCollision,
};
