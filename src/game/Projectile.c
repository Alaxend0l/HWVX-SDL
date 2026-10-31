/* Projectiles: what weapons fire. They fly with thrust and gravity, leave
 * trail effects, may home in (SmartProjectiles.c) or have a proximity
 * fuse, and on a hit explode, stick or slide (ProjectileInfo.collideMode). */
#include "common.h"
#include "CarA.h"
#include "Destructible.h"
#include "Particle.h"
#include "Projectile.h"


/* Spawn effect `id` (DebrisParams) at pos with the projectile's orientation
 * and velocity, credited to its player. */
static inline void Projectile_SpawnFx(Projectile *proj, u32 id, Vector4 *pos) {
    extern Vector4 g_zeroVector;
    DebrisParams p __attribute__((aligned(16)));

    p.debris = id;
    p.pos = *pos;
    p.quat = *(Vector4 *)&proj->base.quat;
    p.vel = proj->base.vel;
    p.owner = proj->base.player.h;
    p.unk48 = 0.0f;
    p.unk4C = 0.0f;
    p.angVel = g_zeroVector;
    ThingManager_DARYL_CreateThing(1, &p);
}

/* Set up from ProjectileParams: life, damage, key tracks, lens flare and
 * light, the spread of its direction and spin, grid/collision flags by
 * collideMode, the recoil impulse on the firing car, and the guidance. */
s32 Projectile_Init(struct Projectile *proj, struct ProjectileParams *params) {
    extern u8 g_projectileVTable[];
    extern u8 g_smartProjectileAVTable[];
    CollisionInfo hit __attribute__((aligned(16)));
    Thing *cars[32];
    Vector4 euler __attribute__((aligned(16)));
    Quaternion q __attribute__((aligned(16)));
    Quaternion rot __attribute__((aligned(16)));
    Vector4 tmp __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 rxn __attribute__((aligned(16)));
    Vector4 angVel __attribute__((aligned(16)));
    Vector4 invInertia __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Vector4 scaled __attribute__((aligned(16)));
    Vector4 sum __attribute__((aligned(16)));
    Vector4 targetPos __attribute__((aligned(16)));
    f32 rel[4];
    GXColor color;
    Thing *src;
    ProjectileInfo *info;
    f32 *spread;
    f32 vn;
    f32 invMassA;
    f32 invMassB;
    Thing *thing = &proj->base;

    src = NULL;
    Thing_Init(thing, 0);
    if (GETP(params->player) != NULL) {
        src = GETP(GETP(params->player)->vehicle);
    }
    thing->damage = RandomAdd_s32(GETP(params->info)->damageBase, GETP(params->info)->damageRange);
    SETP(proj->info, GETP(params->info));
    info = GETP(proj->info);
    proj->life = RandomAdd_s32(info->lifeBase, info->lifeRange);
    proj->keyRate = 1.0f;
    if ((u32)info->lifeBase != 0 && (u32)info->lifeRange != 0) {
        proj->keyRate = 1.0f / ((f32)proj->life / (f32)(u32)info->lifeBase);
    }
    proj->lensFlare = LensFlare_Add(GETP(info->lensFlare), proj, 0);
    thing->pos = params->pos;
    thing->quat = params->quat;
    thing->vel = params->vel;
    proj->gravity = params->gravity;
    thing->player.h = params->player.h;
    spread = GETP(info->spread);
    if (spread != NULL) {
        /* jitter the direction */
        euler.x = 0.017453292f * RandomDirVar(spread[0], spread[1]);
        euler.y = 0.017453292f * RandomDirVar(spread[2], spread[3]);
        euler.z = 0.017453292f * RandomDirVar(spread[4], spread[5]);
        GenerateQuaternionXYZ(&euler, &q);
        QuaternionMult(&q, &thing->quat, &rot);
        thing->quat = rot;
        /* and a random spin */
        euler.x = 0.017453292f * RandomAdd(spread[6], spread[7]);
        euler.y = 0.017453292f * RandomAdd(spread[8], spread[9]);
        euler.z = 0.017453292f * RandomAdd(spread[10], spread[11]);
        GenerateQuaternionXYZ(&euler, &q);
        GenerateSpinAngleFromQuaternion(&q, &proj->spinAxis, &proj->spinSpeed);
        proj->spinSpeed *= 10.0f;
    } else {
        proj->spinSpeed = 0.0f;
    }
    proj->proximityTimer = 0;
    proj->scale.reset = 1;
    proj->color.reset = 1;
    ExtractColorKey(&info->colorKeys, &proj->color, 0.0f);
    ExtractScaleKey(&info->scaleKeys, &proj->scale, 0.0f);
    /* tinted (0x4000) unless the colour is a single plain white key */
    thing->flags |= 0x4000;
    if (info->colorKeys.count == 0) {
        thing->flags &= ~0x4000;
    } else if (info->colorKeys.count == 1) {
        color = *(GXColor *)GETP(info->colorKeys.keys);
        if (color.r == 0xFF && color.b == 0xFF && color.g == 0xFF) {
            thing->flags &= ~0x4000;
        }
    }
    if (info->trailMode == 1) {
        proj->trailStep = RandomAdd(((u32 *)GETP(info->trailSpacing))[0], ((u32 *)GETP(info->trailSpacing))[1]);
    } else if (info->trailMode == 2) {
        proj->trailStep = RandomAdd(((f32 *)GETP(info->trailSpacing))[0], ((f32 *)GETP(info->trailSpacing))[1]);
    }
    proj->trailDist = 0.0f;
    QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
    thing->collisionType = THING_COLLIDE_RAY;
    proj->light = -1;
    if (GETP(info->light) != NULL) {
        proj->light = LightHolder_Add(GETP(info->light), &thing->pos, proj);
    }
    switch (info->collideMode) {
    case 5:
        thing->flags |= 2;
    case 3:
        thing->flags |= THING_FLAG_NO_OBJECT_HITS;
    case 4:
        thing->flags |= 4;
        break;
    case 0:
    case 1:
    case 6:
        thing->flags &= ~2;
        thing->flags |= 4;
        Thing_AddToGrid(proj, 1);
        break;
    case 2:
        break;
    }
    proj->turnX = 0.0f;
    proj->turnY = 0.0f;
    proj->turnZ = 0.0f;
    SETP(proj->target, NULL);
    SETP(thing->vtable, g_projectileVTable);
    if (src != NULL && info->recoil) {
        /* recoil: an impulse back along the firing direction on the car */
        vn = PSVECDotProduct(&thing->axis[2], &thing->vel);
        invMassB = 1.0f / THING_VFN(src, THING_V_GET_MASS, f32 (*)(Thing *))(src);
        invMassA = 1.0f / THING_VFN(thing, THING_V_GET_MASS, f32 (*)(Thing *))(thing);
        hit.normal = thing->axis[2];
        hit.pos = thing->pos;
        hit.material = 0;
        hit.thingA = HND(src);
        hit.thingB = 0;
        PSVECSubtract(&hit.pos, &src->pos, &r);
        PSVECCrossProduct(&r, &hit.normal, &rxn);
        THING_VFN(src, THING_V_GET_INV_INERTIA_TENSOR, void (*)(Thing *, Vector4 *))(src, &invInertia);
        InverseXformVecOrientation(THING_MTX(src), &rxn, &local);
        scaled.x = invInertia.x * local.x;
        scaled.y = invInertia.y * local.y;
        scaled.z = invInertia.z * local.z;
        XformVecOrientation(THING_MTX(src), &scaled, &angVel);
        PSVECCrossProduct(&angVel, &r, &tmp);
        vn *= 1.0f + THING_VFN(src, THING_V_GET_SURFACE_SCRIPT, SurfaceMaterial *(*)(Thing *))(src)->restitution;
        PSVECAdd(&tmp, &thing->axis[2], &sum);
        vn = info->recoil * vn / (invMassB + invMassA + PSVECDotProduct(&sum, &thing->axis[2]));
        THING_VFN(src, THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(Thing *, CollisionInfo *, s32, f32))(src, &hit, 0, -vn);
        if (GETP(src->player) != NULL) {
            ((Player *)GETP(src->player))->damageDealt += thing->damage;
        }
    }
    SETP(thing->vtable, g_projectileVTable);
    switch (info->guidance) {
    case 1:
    case 2:
        proj->timer = GETP(info->smart)->delay;
        SETP(thing->vtable, g_smartProjectileAVTable);
        SmartProjectileA_AquireTarget(proj);
        break;
    case 3: {
        /* aim (in pitch) at the nearest car ahead within 10 degrees */
        u32 best;
        Thing *source;
        u32 i;
        u32 count;
        Thing **car;
        f32 z;
        f32 bestDist;
        f32 dist;

        count = ThingManager_GetActiveCars(cars, 32);
        bestDist = 99999.9f;
        best = count;
        source = PTR(Thing_GetSource(thing));
        for (car = cars, i = 0; i < count; car++, i++) {
            dist = PSVECSquareDistance(&thing->pos, &(*car)->pos);
            if (*car != thing && *car != source && !(dist < 80.0f)) {
                InverseXformVec(THING_MTX(thing), &(*car)->pos, rel);
                z = rel[2];
                if (!(z < 1.0f)) {
                    if (!(ArcTan(Sqrt(rel[0] * rel[0] + rel[1] * rel[1]), z) > 0.17453292f) && dist < bestDist) {
                        bestDist = dist;
                        best = i;
                    }
                }
            }
        }
        if (best < count) {
            SETP(proj->target, cars[best]);
        } else {
            SETP(proj->target, NULL);
        }
        if (GETP(proj->target) != NULL) {
            THING_VFN(GETP(proj->target), THING_V_GET_TARGET_POS, void (*)(Thing *, Vector4 *))(GETP(proj->target), &targetPos);
            Projectile_YAim(proj, &targetPos);
        }
        break;
    }
    }
    proj->unk138 = -1;
    Thing_InitAllSlavesNow(HND(proj), info->slaves, 0xFF);
    return 1;
}


void Projectile_Release(struct Projectile *proj)
{
    extern u8 g_projectileVTable[];

    LensFlare_Delete(proj->lensFlare);
    Thing_Release(proj);
    LightHolder_Delete(proj->light);
    SETP(proj->base.vtable, g_projectileVTable);
}

/* Per frame (also the start of SmartProjectileA_Move): expire (spawning
 * expireFx), lay trail effects every trailStep of time or distance, and
 * play the key tracks. Note it carries on after releasing, as the
 * original does. */
void Projectile_MoveBase(struct Projectile *proj)
{
    extern f32 g_fDeltaTimeSec;
    extern f32 g_fDeltaTimeMS;
    extern s32 g_iDeltaTimeMS;
    f32 dt;
    u8 mode;
    u32 id;
    Vector4 cur;
    Vector4 step;
    Vector4 move;
    Thing *thing = &proj->base;
    ProjectileInfo *info;

    dt = g_fDeltaTimeSec;
    proj->bounces = 0;
    if (proj->life < 0) {
        id = GETP(proj->info)->expireFx;
        if (id != 0) {
            Projectile_SpawnFx(proj, id, &thing->pos);
        }
        Projectile_Release(proj);
    }
    proj->life -= g_iDeltaTimeMS;
    info = GETP(proj->info);
    mode = info->trailMode;
    if (mode != 0) {
        if (mode == 1) {
            proj->trailDist += 1000.0f * dt;
        }
        else if (mode == 2) {
            PSVECScale(&thing->vel, &move, dt);
            proj->trailDist += MagnitudeVector(&move);
        }
        if (proj->trailDist >= 0.0f) {
            /* back along the path from the current position */
            cur = thing->pos;
            PSVECScale(&thing->vel, &step, -proj->trailStep);
            if (proj->trailStep) {
                while (proj->trailDist > proj->trailStep) {
                    id = GETP(proj->info)->trailFx;
                    if (id != 0) {
                        Projectile_SpawnFx(proj, id, &cur);
                    }
                    proj->trailDist -= proj->trailStep;
                    PSVECAdd(&cur, &step, &cur);
                }
            }
        }
    }
    ExtractColorKey(&GETP(proj->info)->colorKeys, &proj->color, g_fDeltaTimeMS * proj->keyRate);
    ExtractScaleKey(&GETP(proj->info)->scaleKeys, &proj->scale, g_fDeltaTimeMS * proj->keyRate);
}


/* Every proximityInterval ms: a car (other than the shooter's) within
 * proximityRadius sets it off: impactFx (stuck to the car if
 * proximityAttach), and the projectile is released. */
void Projectile_ProximityDetection(struct Projectile *proj)
{
    extern s32 g_iDeltaTimeMS;
    extern Vector4 g_zeroVector;
    DebrisParams p __attribute__((aligned(16)));
    Thing *cars[16];
    u32 count, i;
    void *source;
    f32 radius;
    ProjectileInfo *info;
    u32 id;
    Thing *slave;
    Thing *thing = &proj->base;

    radius = GETP(proj->info)->proximityRadius;
    source = PTR(Thing_GetSource(thing));
    proj->proximityTimer -= g_iDeltaTimeMS;
    if (proj->proximityTimer < 0)
    {
        count = ThingManager_GetActiveCars(cars, 16);
        for (i = 0; i < count; i++)
        {
            if (cars[i] != source)
            {
                if (PSVECSquareDistance(&thing->pos, &cars[i]->pos) < (radius + cars[i]->radius) * (radius + cars[i]->radius))
                {
                    info = GETP(proj->info);
                    id = info->impactFx;
                    if (id != 0)
                    {
                        p.unk48 = 0.0f;
                        p.unk4C = 0.0f;
                        p.owner = thing->player.h;
                        p.debris = id;
                        p.angVel = g_zeroVector;
                        if (info->proximityAttach != 0)
                        {
                            p.quat = *(Vector4 *)&thing->quat;
                            p.vel.x = 0.0f;
                            p.vel.y = 0.0f;
                            p.vel.z = 0.0f;
                            InverseXformVec(THING_MTX(cars[i]), &thing->pos, &p.pos);
                            slave = ThingManager_DARYL_CreateSlave(1, &p, 0xFF);
                            if (slave != NULL)
                            {
                                slave->slaveTag = 0;
                                ThingManagerList_Append(&cars[i]->slaves, slave);
                            }
                        }
                        else
                        {
                            p.vel = thing->vel;
                            p.pos = thing->pos;
                            p.quat.w = 1.0f;
                            p.quat.x = 0.0f;
                            p.quat.y = 0.0f;
                            p.quat.z = 0.0f;
                            ThingManager_DARYL_CreateThing(1, &p);
                        }
                    }
                    thing->vel.x = 0.0f;
                    thing->vel.y = 0.0f;
                    thing->vel.z = 0.0f;
                    Projectile_Release(proj);
                    thing->flags |= 2;
                    return;
                }
            }
        }
        proj->proximityTimer += GETP(proj->info)->proximityInterval;
    }
}

/* Spin, gravity, thrust along the nose (capped at maxSpeed), proximity fuse. */
s32 Projectile_PreMove(struct Projectile *proj)
{
    extern f32 g_fDeltaTimeSec;
    Quaternion q;
    Quaternion result;
    ProjectileInfo *info;
    f32 step;
    f32 mag;
    f32 scale;
    Thing *thing = &proj->base;

    GenerateQuaternion(&proj->spinAxis, &q, proj->spinSpeed * g_fDeltaTimeSec);
    QuaternionMult(&thing->quat, &q, &result);
    thing->quat = result;
    thing->vel.y += proj->gravity * g_fDeltaTimeSec;
    info = GETP(proj->info);
    if (info->thrust)
    {
        step = info->thrust * g_fDeltaTimeSec;
        thing->vel.x += thing->axis[2].x * step;
        thing->vel.y += thing->axis[2].y * step;
        thing->vel.z += thing->axis[2].z * step;
    }
    mag = PSVECSquareMag(&thing->vel);
    info = GETP(proj->info);
    if (mag > info->maxSpeed * info->maxSpeed)
    {
        /* note: scales the velocity to length 1, not maxSpeed (the decomp's
         * 1.0f: a constant in the original too) */
        scale = 1.0f / Sqrt(mag);
        thing->vel.x *= scale;
        thing->vel.y *= scale;
        thing->vel.z *= scale;
    }
    info = GETP(proj->info);
    if (info->proximityRadius)
    {
        Projectile_ProximityDetection(proj);
    }
    return 1;
}

/* The original inlines Projectile_MoveBase here. */
s32 Projectile_Move(struct Projectile *proj)
{
    Projectile_MoveBase(proj);
    Thing_CollideWithForceBoxes(&proj->base);
    return 1;
}

/* drawMode 0: the geometry scaled by the scale key (tinted by the colour
 * key if flag 0x4000); 1: a billboard sprite. */
void Projectile_Draw(struct Projectile *proj)
{
    FxBillboard *sprite;
    Matrix4 mtx __attribute__((aligned(16)));
    Matrix4 scale __attribute__((aligned(16)));
    GXColor color;
    Thing *thing = &proj->base;

    if (GETP(proj->info)->draw == 1)
    {
        QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
        memset(&scale, 0, sizeof(Matrix4));
        scale.v[0].x = proj->scale.v.x;
        scale.v[1].y = proj->scale.v.y;
        scale.v[2].z = proj->scale.v.z;
        scale.v[3].w = 1.0f;
        MultiplyMatrix(&scale, THING_MTX(thing), &mtx);
        switch (GETP(proj->info)->drawMode)
        {
        case 0:
            if (thing->flags & 0x4000)
            {
                color = proj->color.color;
                Gfx_SetBaseColor(&color);
                Gfx_EnableState(0x10);
            }
            Gfx_DrawGeometry(HND(GETP(GETP(proj->info)->geometry)), HND(&mtx));
            if (thing->flags & 0x4000)
            {
                Gfx_DisableState(0x10);
            }
            break;
        case 1:
            sprite = Gfx_AddBillBoardSprite(0);
            sprite->blend = *GETP(GETP(proj->info)->sprite);
            sprite->color = proj->color.color;
            sprite->size = proj->scale.v.x;
            sprite->x = thing->pos.x;
            sprite->y = thing->pos.y;
            sprite->z = thing->pos.z;
            sprite->rot = 360.0f;
            break;
        }
    }
}

/* Pitch the nose (and the velocity) towards `target` if it is within 15
 * degrees above or below. */
void Projectile_YAim(struct Projectile *proj, Vector4 *target)
{
    Vector4 local __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16))) = {1.0f, 0.0f, 0.0f, 0.0f};
    Quaternion q __attribute__((aligned(16)));
    Quaternion result __attribute__((aligned(16)));
    f32 angle;
    f32 speed;
    Thing *thing = &proj->base;

    InverseXformVec(THING_MTX(thing), target, &local);
    angle = ArcTan(local.y, local.z);
    if (FloatsEqual(angle, 0.0f, 0.2617993950843811f)) {
        speed = MagnitudeVector(&thing->vel);
        GenerateQuaternion(&axis, &q, -angle);
        QuaternionMult(&thing->quat, &q, &result);
        thing->quat = result;
        QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
        thing->vel.x = thing->axis[2].x * speed;
        thing->vel.y = thing->axis[2].y * speed;
        thing->vel.z = thing->axis[2].z * speed;
    }
}


/* The orientation of an impact effect: the projectile's turned so its nose
 * follows `normal` (impactOrient 1) or reflects off it (2). */
static inline void Projectile_TurnToNormal(Projectile *proj, Vector4 *normal, f32 dot, f32 angle, Quaternion *out) {
    Quaternion q __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));

    PSVECCrossProduct(&proj->base.axis[2], normal, &axis);
    if (dot > 0.99f) {
        axis = proj->base.axis[0];
    } else if (PSVECSquareMag(&axis)) {
        PSVECNormalize(&axis, &axis);
    }
    GenerateQuaternion(&axis, &q, angle);
    QuaternionMult(&q, &proj->base.quat, out);
}

/* Turned about y by 180 degrees (for a nose straight into the surface). */
static inline void Projectile_TurnAround(Projectile *proj, Quaternion *out) {
    Vector4 flip __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 0.0f};
    Quaternion q __attribute__((aligned(16)));

    GenerateQuaternion(&flip, &q, 3.1415927f);
    QuaternionMult(&q, &proj->base.quat, out);
}

/* What a hit does, by collideMode: stick to the thing hit (6), explode
 * with impactFx and a bullet hole on terrain (0, 3), or get pushed off
 * the surface (1, 4). More than 8 corrections in a frame: flag 0x40. */
void Projectile_DynamicCollisionCorrection(struct Projectile *proj, CollisionInfo *hit) {
    extern Vector4 g_zeroVector;
    DebrisParams p;
    Matrix4 mtx;
    Matrix4 other;
    Matrix4 hole;
    Vector4 normal __attribute__((aligned(16)));
    Thing *target;
    SurfaceMaterial *surface;
    Thing *slave;
    ProjectileInfo *info;
    u32 id;
    f32 dot;
    f32 angle;
    f32 size;
    Thing *thing = &proj->base;

    Thing_GetSource(thing);
    if (!(thing->flags & 0x40) && !(thing->flags & 0x200000) && hit != NULL) {
        proj->bounces += 1;
        if (proj->bounces > 8) {
            thing->flags |= 0x40;
            return;
        }
        /* note: thingB either way, as the original does */
        if (PTR(hit->thingA) == thing) {
            target = PTR(hit->thingB);
            normal = hit->normal;
        } else {
            target = PTR(hit->thingB);
            normal.x = -hit->normal.x;
            normal.y = -hit->normal.y;
            normal.z = -hit->normal.z;
        }
        info = GETP(proj->info);
        switch (info->collideMode) {
        case 6:
            if (target != NULL) {
                /* become a slave of the target, in its local space */
                mtx = *THING_MTX(thing);
                other = *THING_MTX(target);
                TransposeSubMatrix(&other);
                MultiplySubMatrix(&other, &mtx, THING_MTX(thing));
                SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
                InverseXformVec(THING_MTX(target), &mtx.v[3], &thing->pos);
                if (GETP(thing->gridObject) != NULL) {
                    Grid_FreeObject(GETP(thing->gridObject));
                }
                SETP(thing->gridObject, NULL);
                thing->slaveTag = 0xEF;
                ThingManagerList_Remove(&g_thingManager_DARYL.activeList, proj);
                ThingManagerList_Append(&target->slaves, proj);
                thing->vel = g_zeroVector;
            } else {
                thing->flags |= 0x80000;
                thing->vel = g_zeroVector;
            }
            break;
        case 0:
        case 3:
            id = info->impactFx;
            if (id != 0) {
                dot = PSVECDotProduct(&normal, &thing->axis[2]);
                switch (info->impactOrient) {
                case 0:
                    p.quat = *(Vector4 *)&thing->quat;
                    break;
                case 1:
                    if (dot < -0.97f) {
                        Projectile_TurnAround(proj, (Quaternion *)&p.quat);
                    } else {
                        angle = acos(dot);
                        Projectile_TurnToNormal(proj, &normal, dot, angle, (Quaternion *)&p.quat);
                    }
                    break;
                case 2:
                    if (dot < -0.97f) {
                        Projectile_TurnAround(proj, (Quaternion *)&p.quat);
                    } else {
                        if (dot > 0.97f) {
                            angle = -1.5707964f;
                        } else {
                            angle = (f32)acos(dot) - 1.5707964f;
                        }
                        Projectile_TurnToNormal(proj, &normal, dot, 2.0f * angle, (Quaternion *)&p.quat);
                    }
                    break;
                }
                p.debris = id;
                p.vel = g_zeroVector;
                p.owner = thing->player.h;
                p.unk48 = 0.0f;
                p.unk4C = 0.0f;
                p.angVel = g_zeroVector;
                if (target != NULL) {
                    InverseXformVec(THING_MTX(target), &thing->pos, &p.pos);
                    slave = ThingManager_DARYL_CreateSlave(1, &p, 0xFF);
                    if (slave != NULL) {
                        slave->slaveTag = 0xEF;
                        ThingManagerList_Append(&target->slaves, slave);
                    }
                } else {
                    p.pos = thing->pos;
                    p.pos.x = 0.5f * normal.x + p.pos.x;
                    p.pos.y = 0.5f * normal.y + p.pos.y;
                    p.pos.z = 0.5f * normal.z + p.pos.z;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
            thing->vel.x = 0.0f;
            thing->vel.y = 0.0f;
            thing->vel.z = 0.0f;
            if (PTR(hit->thingB) == NULL && (surface = PTR(hit->material)) != NULL && GETP(proj->info)->holeTexture != 0) {
                /* a bullet hole on the terrain; surface byte 1: 0 takes holes */
                switch (((u8 *)surface)[1]) {
                case 0: {
                    Quaternion rot __attribute__((aligned(16)));

                    dot = PSVECDotProduct(&normal, &thing->axis[2]);
                    if (dot < -0.985f) {
                        Projectile_TurnAround(proj, &rot);
                    } else {
                        angle = acos(dot);
                        Projectile_TurnToNormal(proj, &normal, dot, angle, &rot);
                    }
                    QuaternionToSubMatrix(&rot, &hole);
                    hole.v[3].x = 0.5f * hit->normal.x + hit->pos.x;
                    hole.v[3].y = 0.5f * hit->normal.y + hit->pos.y;
                    hole.v[3].z = 0.5f * hit->normal.z + hit->pos.z;
                    info = GETP(proj->info);
                    size = RandomAdd(info->holeSizeBase, info->holeSizeRange);
                    angle = RandomAdd_s32(info->holeLifeBase, info->holeLifeRange);
                    BulletHole_AddSegment(&hole, info->holeTexture, *GETP(info->holeColor), angle, size);
                    break;
                }
                case 1:
                case 2:
                    break;
                }
            }
            Projectile_Release(proj);
            thing->flags |= 2;
            break;
        case 1:
        case 4:
            thing->pos.x = 0.02f * normal.x + thing->pos.x;
            thing->pos.y = 0.02f * normal.y + thing->pos.y;
            thing->pos.z = 0.02f * normal.z + thing->pos.z;
        case 2:
        case 5:
            break;
        }
    }
}

/* Vtable DynamicCollideImpact: take the damage, get pushed by the impulse,
 * then react to the hit. */
void Projectile_DynamicCollideImpact(struct Projectile *proj, CollisionInfo *hit, s32 damage, f32 force)
{
    f32 scale;
    Thing *thing = &proj->base;

    thing->health -= damage;
    if (hit != NULL)
    {
        scale = force / GETP(proj->info)->mass;
        thing->vel.x = hit->normal.x * scale + thing->vel.x;
        thing->vel.y = hit->normal.y * scale + thing->vel.y;
        thing->vel.z = hit->normal.z * scale + thing->vel.z;
        Projectile_DynamicCollisionCorrection(proj, hit);
    }
}

f32 Projectile_GetMass(struct Projectile *proj)
{
    return GETP(proj->info)->mass;
}

/* Whether its info lies in [start, end) (a data pack being unloaded). */
s32 Projectile_Invalidate(struct Projectile *proj, u32 start, u32 end)
{
    u32 info;

    info = proj->info.h;
    if ((info >= start) && (info < end))
    {
        return 1;
    }
    return 0;
}

