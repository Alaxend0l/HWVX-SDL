/* Physical objects: destructibles moved as rigid bodies (gravity, impulses,
 * bouncing off the terrain), and movers: destructibles that follow an
 * animation track. Both use the PhysicalObject layout (Destructible.h). */
#include "common.h"
#include "Destructible.h"


static inline void Mover_SetRotation(struct PhysicalObject *mover, Quaternion quat) {
    mover->d.moverRot = quat;
}

/* The position on the mover's track at `time`. */
void Mover_GetTrackPos(Destructible *mover, f32 *pos, f32 time) {
    MoverKey key;

    Gfx_AnimExtractKeyFrame(GETP(mover->anim), time, &key, 1);
    pos[0] = key.pos.x;
    pos[1] = key.pos.y;
    pos[2] = key.pos.z;
}

/* Add a force along `dir` (scaled), applied at `point`: to the force and
 * the torque accumulated this step. Also used by the drone (CarA_Stunts.c). */
void PhysicalObject_ProcessForce(struct PhysicalObject *obj, Vector4 *point, Vector4 *dir, f32 scale)
{

    Vector4 pointVel;
    Vector4 cross;
    Vector4 r;
    Vector4 torque;
    Vector4 objVel;
    Vector4 scaled __attribute__((aligned(16)));
    Thing *thing = &obj->d.base;

    objVel = thing->vel;
    PSVECScale(dir, &scaled, scale); /* f1: the 4th argument */
    PSVECSubtract(point, &thing->pos, &r);
    GetComponentOfVector(dir, &objVel);
    PSVECCrossProduct(&obj->angVel, &r, &cross);
    PSVECAdd(&thing->vel, &cross, &pointVel);
    PSVECCrossProduct(&r, &scaled, &torque);

    obj->force.x += scaled.x;
    obj->force.y += scaled.y;
    obj->force.z += scaled.z;
    obj->torque.x += torque.x;
    obj->torque.y += torque.y;
    obj->torque.z += torque.z;
}

/* angVel = the world angular velocity for the angular momentum
 * (R * invInertia * R^T * angMomentum). */
static inline void PhysicalObject_UpdateAngVel(struct PhysicalObject *obj, Vector4 *out) {
    f32 scaled[4];
    f32 local[4];

    InverseXformVecOrientation(THING_MTX(&obj->d.base), &obj->angMomentum, local);
    scaled[0] = obj->invInertia.x * local[0];
    scaled[1] = obj->invInertia.y * local[1];
    scaled[2] = obj->invInertia.z * local[2];
    XformVecOrientation(THING_MTX(&obj->d.base), scaled, out);
}

/* A destructible plus rigid body: the inverse inertia tensor from the
 * collision box (or the hull's bounds), the initial spin, and released at
 * once if it starts inside the terrain. */
s32 PhysicalObject_Init(struct PhysicalObject *obj, struct DestructibleParams *data) {
    CollisionInfo hit __attribute__((aligned(16)));
    OBox box;
    Vector4 scaled __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    f32 dist;
    DestructibleInfo *info;
    ConvexHull *mesh;
    u32 i;
    Thing *thing = &obj->d.base;

    data->pos.y += 0.2f;
    Destructible_Init(&obj->d, data);
    thing->flags &= ~THING_FLAG_AT_REST;
    thing->flags &= ~0x200;
    if (GETP(data->anim) != NULL) {
        obj->d.trackPos = 1.0f + GETP(data->anim)->end;
    }
    info = GETP(GETP(obj->d.record)->info);
    switch (info->type) {
    case 0:
        box = *GETP(info->box);
        obj->invInertia.x = 1.0f / (info->inertia[0] * info->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
        obj->invInertia.y = 1.0f / (GETP(GETP(obj->d.record)->info)->inertia[1] * GETP(GETP(obj->d.record)->info)->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
        obj->invInertia.z = 1.0f / (GETP(GETP(obj->d.record)->info)->inertia[2] * GETP(GETP(obj->d.record)->info)->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
        break;
    case 1:
    case 6:
        mesh = GETP(info->hull);
        box.v[0].x = 9999.9f;
        box.v[0].y = 9999.9f;
        box.v[0].z = 9999.9f;
        box.v[1].x = -9999.9f;
        box.v[1].y = -9999.9f;
        box.v[1].z = -9999.9f;
        for (i = 0; i < mesh->numVerts; i++) {
            if (box.v[0].x > GETP(mesh->verts)[i].x) {
                box.v[0].x = GETP(mesh->verts)[i].x;
            }
            if (box.v[0].y > GETP(mesh->verts)[i].y) {
                box.v[0].y = GETP(mesh->verts)[i].y;
            }
            if (box.v[0].z > GETP(mesh->verts)[i].z) {
                box.v[0].z = GETP(mesh->verts)[i].z;
            }
            if (box.v[1].x < GETP(mesh->verts)[i].x) {
                box.v[1].x = GETP(mesh->verts)[i].x;
            }
            if (box.v[1].y < GETP(mesh->verts)[i].y) {
                box.v[1].y = GETP(mesh->verts)[i].y;
            }
            if (box.v[1].z < GETP(mesh->verts)[i].z) {
                box.v[1].z = GETP(mesh->verts)[i].z;
            }
        }
        obj->invInertia.x = 1.0f / (GETP(GETP(obj->d.record)->info)->inertia[0] * GETP(GETP(obj->d.record)->info)->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
        obj->invInertia.y = 1.0f / (GETP(GETP(obj->d.record)->info)->inertia[1] * GETP(GETP(obj->d.record)->info)->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
        obj->invInertia.z = 1.0f / (GETP(GETP(obj->d.record)->info)->inertia[2] * GETP(GETP(obj->d.record)->info)->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
        break;
    }
    if (data->angVel.x || data->angVel.y || data->angVel.z) {
        InverseXformVecOrientation(THING_MTX(thing), &data->angVel, &local);
        scaled.x = local.x / obj->invInertia.x;
        scaled.y = local.y / obj->invInertia.y;
        scaled.z = local.z / obj->invInertia.z;
        XformVecOrientation(THING_MTX(thing), &scaled, &obj->angMomentum);
    }
    if (!(thing->flags & (THING_FLAG_NO_TERRAIN_HITS | 2))) {
        dist = 1.0f;
        if ((s8)Thing_HitsTerrain(thing, &dist, &hit) && 0.0f == dist) {
            THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
        }
    }
    return 1;
}

/* The decomp has an m2c placeholder here; the original passes its arguments
 * on to PhysicalObject_Init and returns its result (asm). */
s32 Mover_Init(struct PhysicalObject *obj, struct DestructibleParams *data) {
    return PhysicalObject_Init(obj, data);
}

/* Apply the accumulated force and torque for this frame (no gravity or
 * damping, see PhysicalObject_PreMove), then clear them. */
void PhysicalObject_IntegrateNonLateral(struct PhysicalObject *obj) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeSec;
    f32 invMass;
    Thing *thing = &obj->d.base;

    invMass = g_fDeltaTimeSec / GETP(GETP(obj->d.record)->info)->mass;
    thing->vel.x += invMass * obj->force.x;
    thing->vel.y += invMass * obj->force.y;
    thing->vel.z += invMass * obj->force.z;
    obj->angMomentum.x += g_fDeltaTimeSec * obj->torque.x;
    obj->angMomentum.y += g_fDeltaTimeSec * obj->torque.y;
    obj->angMomentum.z += g_fDeltaTimeSec * obj->torque.z;
    obj->torque = g_zeroVector;
    obj->force = obj->torque;
    PhysicalObject_UpdateAngVel(obj, &obj->angVel);
}

/* Gravity and angular damping, integrated into the velocities. */
s32 PhysicalObject_PreMove(struct PhysicalObject *obj) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeSec;
    f32 angVel[4];
    f32 damping;
    f32 invMass;
    Thing *thing = &obj->d.base;
    DestructibleInfo *info;

    if (!(thing->flags & THING_FLAG_AT_REST)) {
        obj->torque = g_zeroVector;
        obj->force = obj->torque;
        info = GETP(GETP(obj->d.record)->info);
        obj->force.y += info->gravityScale * info->mass;
        damping = GETP(GETP(obj->d.record)->info)->angDamping;
        PhysicalObject_UpdateAngVel(obj, (Vector4 *)angVel);
        obj->torque.x += -damping * angVel[0];
        obj->torque.y += -damping * angVel[1];
        obj->torque.z += -damping * angVel[2];
        invMass = g_fDeltaTimeSec / GETP(GETP(obj->d.record)->info)->mass;
        thing->vel.x += invMass * obj->force.x;
        thing->vel.y += invMass * obj->force.y;
        thing->vel.z += invMass * obj->force.z;
        obj->angMomentum.x += g_fDeltaTimeSec * obj->torque.x;
        obj->angMomentum.y += g_fDeltaTimeSec * obj->torque.y;
        obj->angMomentum.z += g_fDeltaTimeSec * obj->torque.z;
        obj->torque = g_zeroVector;
        obj->force = obj->torque;
        PhysicalObject_UpdateAngVel(obj, &obj->angVel);
    }
    return 1;
}

/* Move through the frame in up to 8 sub-steps, stopping at each terrain
 * contact to bounce (impulse from restitution and friction of both
 * surfaces), rotating via the vtable's DynamicRotation. Released if stuck. */
u8 PhysicalObject_Move(struct PhysicalObject *obj) {
    extern f32 g_fDeltaTimeSec;
    CollisionInfo hit __attribute__((aligned(16)));
    Vector4 tmp __attribute__((aligned(16)));
    Vector4 negVel __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 rxn __attribute__((aligned(16)));
    Vector4 angVel __attribute__((aligned(16)));
    Vector4 invInertia __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Vector4 scaled __attribute__((aligned(16)));
    f32 dt;
    f32 elapsed;
    f32 invMass;
    f32 vn;
    f32 e1;
    f32 e2;
    f32 friction;
    f32 inv;
    u32 iter;
    s32 hitTerrain;
    Thing *thing = &obj->d.base;
    Thing *self;

    if (!(thing->flags & (1 | 4 | THING_FLAG_AT_REST))) {
        elapsed = 0.0f;
        iter = 0;
        if (Timer_Interrupt300()) {
            /* renormalise the orientation now and then */
            inv = 1.0f / Sqrt(thing->quat.x * thing->quat.x +
                              thing->quat.y * thing->quat.y +
                              thing->quat.z * thing->quat.z +
                              thing->quat.w * thing->quat.w);
            thing->quat.x *= inv;
            thing->quat.y *= inv;
            thing->quat.z *= inv;
            thing->quat.w *= inv;
        }
        do {
            dt = g_fDeltaTimeSec - elapsed;
            hitTerrain = Thing_HitsTerrain(thing, &dt, &hit);
            thing->pos.x += thing->vel.x * dt;
            thing->pos.y += thing->vel.y * dt;
            thing->pos.z += thing->vel.z * dt;
            thing->flags |= 0x100;
            THING_VFN(thing, THING_V_DYNAMIC_ROTATION, void (*)(Thing *, f32))(thing, dt);
            hit.thingA = HND(obj);
            hit.thingB = 0;
            if (0.0f == dt) {
                THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
                return 0;
            }
            if ((s8)hitTerrain) {
                self = PTR(hit.thingA);
                invMass = 1.0f / THING_VFN(thing, THING_V_GET_MASS, f32 (*)(Thing *))(thing);
                vn = PSVECDotProduct(&hit.normal, &self->vel);
                e1 = THING_VFN(self, THING_V_GET_SURFACE_SCRIPT, SurfaceMaterial *(*)(Thing *))(self)->restitution;
                e2 = ((SurfaceMaterial *)PTR(hit.material))->restitution;
                friction = 0.3f * ((SurfaceMaterial *)PTR(hit.material))->friction *
                           THING_VFN(self, THING_V_GET_SURFACE_SCRIPT, SurfaceMaterial *(*)(Thing *))(self)->friction;
                negVel.x = -thing->vel.x;
                negVel.y = -thing->vel.y;
                negVel.z = -thing->vel.z;
                if (PSVECSquareMag(&negVel) > 0.0f) {
                    /* friction tilts the impulse against the motion */
                    if (friction > 1.0f) {
                        friction = 1.0f;
                    }
                    if (PSVECSquareMag(&negVel)) {
                        PSVECNormalize(&negVel, &negVel);
                    }
                    hit.normal.x = (1.0f - friction) * hit.normal.x + friction * negVel.x;
                    hit.normal.y = (1.0f - friction) * hit.normal.y + friction * negVel.y;
                    hit.normal.z = (1.0f - friction) * hit.normal.z + friction * negVel.z;
                    if (PSVECSquareMag(&hit.normal)) {
                        PSVECNormalize(&hit.normal, &hit.normal);
                    }
                }
                PSVECSubtract(&hit.pos, &self->pos, &r);
                PSVECCrossProduct(&r, &hit.normal, &rxn);
                THING_VFN(self, THING_V_GET_INV_INERTIA_TENSOR, void (*)(Thing *, Vector4 *))(self, &invInertia);
                InverseXformVecOrientation(THING_MTX(self), &rxn, &local);
                scaled.x = invInertia.x * local.x;
                scaled.y = invInertia.y * local.y;
                scaled.z = invInertia.z * local.z;
                XformVecOrientation(THING_MTX(self), &scaled, &angVel);
                PSVECCrossProduct(&angVel, &r, &tmp);
                friction = PSVECDotProduct(&tmp, &hit.normal);
                friction = invMass * PSVECDotProduct(&hit.normal, &hit.normal) + friction;
                thing->pos.x += 0.03f * hit.normal.x;
                thing->pos.y += 0.03f * hit.normal.y;
                thing->pos.z += 0.03f * hit.normal.z;
                THING_VFN(thing, THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(Thing *, CollisionInfo *, s32, f32))(
                    thing, &hit, 0, vn * -(e1 * e2 + 1.0f) / friction);
            }
            if (thing->flags & 0x200000) {
                elapsed = g_fDeltaTimeSec;
            }
            if (iter++ > 6) {
                elapsed = g_fDeltaTimeSec;
            }
            elapsed += dt;
        } while (elapsed < g_fDeltaTimeSec);
    }
    return Destructible_Move(&obj->d);
}

/* Follow the track: advance trackPos (looping), then either set the
 * transform directly or, for colliding movers (flags 4), turn the step
 * into a velocity and angular velocity for the collision code. Stopped
 * movers (flags 0x4000) stand still. */
s32 Mover_PreMove(struct PhysicalObject *mover) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeSec;
    Matrix4 inv;
    Matrix4 rel;
    Matrix4 rot;
    MoverKey start __attribute__((aligned(16)));
    MoverKey key __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Quaternion quat;
    Quaternion delta;
    f32 angle;
    Destructible *d = &mover->d;
    Thing *thing = &mover->d.base;

    if (!(thing->flags & 0x4000)) {
        d->trackPos += d->speed * g_fDeltaTimeSec;
        if (d->trackPos > GETP(d->anim)->end) {
            d->trackPos -= GETP(d->anim)->end - GETP(d->anim)->start;
            Gfx_AnimExtractKeyFrame(GETP(d->anim), GETP(d->anim)->start, &start, 1);
            thing->pos.x = start.pos.x;
            thing->pos.y = start.pos.y;
            thing->pos.z = start.pos.z;
        }
        Gfx_AnimExtractKeyFrame(GETP(d->anim), d->trackPos, &key, 1);
        quat = key.rot;
        pos.x = key.pos.x;
        pos.y = key.pos.y;
        pos.z = key.pos.z;
        if (thing->flags & 4) {
            thing->vel.x = (pos.x - thing->pos.x) / g_fDeltaTimeSec;
            thing->vel.y = (pos.y - thing->pos.y) / g_fDeltaTimeSec;
            thing->vel.z = (pos.z - thing->pos.z) / g_fDeltaTimeSec;
            inv = *THING_MTX(thing);
            Mover_SetRotation(mover, quat);
            QuaternionToSubMatrix(&quat, &rot);
            TransposeSubMatrix(&inv);
            MultiplySubMatrix(&inv, &rot, &rel);
            SubMatrixToQuaternion(&rel, &delta);
            GenerateSpinAngleFromQuaternion(&delta, &mover->angVel, &angle);
            angle /= g_fDeltaTimeSec;
            mover->angVel.x *= angle;
            mover->angVel.y *= angle;
            mover->angVel.z *= angle;
        } else {
            QuaternionToSubMatrix(&quat, THING_MTX(thing));
            thing->pos = pos;
            thing->quat = quat;
            Mover_SetRotation(mover, quat);
        }
        thing->flags &= ~0x10;
    } else {
        thing->vel = g_zeroVector;
        Mover_SetRotation(mover, thing->quat);
        mover->angVel = g_zeroVector;
    }
    return 1;
}

/* Rotate by the angular velocity over dt. A box checks its corners' paths
 * against the terrain, bouncing off what they hit, and only takes the new
 * orientation if it is then clear of terrain and things. */
void PhysicalObject_DynamicRotation(struct PhysicalObject *obj, f32 dt) {
    CollisionOBox obox;
    CollisionInfo hit;
    Matrix4 mtx;
    Vector4 spin __attribute__((aligned(16)));
    Quaternion q __attribute__((aligned(16)));
    Quaternion newQ __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 scaled[4];
    f32 local[4];
    Vector4 torque;
    f32 bScaled[4];
    f32 bLocal[4];
    Vector4 tmp;
    Vector4 angVel;
    Vector4 rxn;
    Vector4 vel;
    Vector4 r;
    Vector4 impulse;
    f32 t;
    f32 mag;
    u32 i;
    Thing *thing = &obj->d.base;
    DestructibleInfo *info;
    OBox *box;
    CollisionOBox *old;
    s8 hitTerrain;
    f32 e1;
    f32 num;
    f32 j;

    t = dt;
    if (thing->flags & THING_FLAG_AT_REST) {
        return;
    }
    if (thing->flags & 0x200000) {
        return;
    }
    InverseXformVecOrientation(THING_MTX(thing), &obj->angMomentum, local);
    scaled[0] = obj->invInertia.x * local[0];
    scaled[1] = obj->invInertia.y * local[1];
    scaled[2] = obj->invInertia.z * local[2];
    XformVecOrientation(THING_MTX(thing), scaled, &spin);
    axis = spin;
    mag = MagnitudeVector(&axis);
    if (mag > 0.0001f) {
        axis.x /= mag;
        axis.y /= mag;
        axis.z /= mag;
    }
    GenerateQuaternion(&axis, &q, mag * t);
    QuaternionMult(&q, &thing->quat, &newQ);
    QuaternionToSubMatrix(&newQ, &mtx);
    mtx.v[3].x = thing->pos.x;
    mtx.v[3].y = thing->pos.y;
    mtx.v[3].z = thing->pos.z;
    if (thing->flags & 2) {
        thing->quat = newQ;
        *THING_MTX(thing) = mtx;
        return;
    }
    info = GETP(GETP(obj->d.record)->info);
    switch (info->type) {
    case 0:
        box = GETP(GETP(GETP(obj->d.record)->info)->box);
        old = Destructible_GetOrientedCollision(&obj->d);
        obox.inUse = 1;
        BuildOOB(box->v, &mtx, &obox);
        for (i = 0; i < 8; i++) {
            if (t > 0.00005f) {
                dir.x = (obox.corners[i].x - old->corners[i].x) / dt;
                dir.y = (obox.corners[i].y - old->corners[i].y) / dt;
                dir.z = (obox.corners[i].z - old->corners[i].z) / dt;
                hitTerrain = 0;
                thing->flags |= 2;
                if (!(thing->flags & THING_FLAG_NO_TERRAIN_HITS)) {
                    hitTerrain = RayHitsTerrain_Aux3(&old->corners[i], &dir, &t, &hit);
                }
                thing->flags &= ~2;
                if (hitTerrain) {
                    e1 = ((SurfaceMaterial *)PTR(hit.material))->restitution;
                    vel = dir;
                    PSVECSubtract(&hit.pos, &thing->pos, &r);
                    num = -(e1 * GETP(GETP(GETP(obj->d.record)->info)->material)->restitution + 1.0f) * PSVECDotProduct(&vel, &hit.normal);
                    PSVECCrossProduct(&r, &hit.normal, &rxn);
                    InverseXformVecOrientation(THING_MTX(thing), &rxn, bLocal);
                    bScaled[0] = obj->invInertia.x * bLocal[0];
                    bScaled[1] = obj->invInertia.y * bLocal[1];
                    bScaled[2] = obj->invInertia.z * bLocal[2];
                    XformVecOrientation(THING_MTX(thing), bScaled, &angVel);
                    PSVECCrossProduct(&angVel, &r, &tmp);
                    j = num / (1.0f / GETP(GETP(obj->d.record)->info)->mass + PSVECDotProduct(&tmp, &hit.normal));
                    impulse.x = j * hit.normal.x;
                    impulse.y = j * hit.normal.y;
                    impulse.z = j * hit.normal.z;
                    thing->vel.x += impulse.x / GETP(GETP(obj->d.record)->info)->mass;
                    thing->vel.y += impulse.y / GETP(GETP(obj->d.record)->info)->mass;
                    thing->vel.z += impulse.z / GETP(GETP(obj->d.record)->info)->mass;
                    thing->pos.x += 0.03f * hit.normal.x;
                    thing->pos.y += 0.03f * hit.normal.y;
                    thing->pos.z += 0.03f * hit.normal.z;
                    PSVECCrossProduct(&r, &impulse, &torque);
                    obj->angMomentum.x += torque.x;
                    obj->angMomentum.y += torque.y;
                    obj->angMomentum.z += torque.z;
                    PhysicalObject_UpdateAngVel(obj, &obj->angVel);
                }
            }
        }
        if (!OBoxHitsTerrainAndObjectsZeroTime(&obox, thing, NULL)) {
            thing->quat = newQ;
            *THING_MTX(thing) = mtx;
            *GETP(obj->d.orientedBox) = obox;
            /* the box's axes point into the thing's matrix (as BuildOOB orders them) */
            SETP(GETP(obj->d.orientedBox)->axis[0], &thing->axis[2]);
            SETP(GETP(obj->d.orientedBox)->axis[1], &thing->axis[0]);
            SETP(GETP(obj->d.orientedBox)->axis[2], &thing->axis[1]);
            thing->flags &= ~0x180;
        }
        break;
    case 1:
    case 6:
        break;
    }
}

/* Movers: take the orientation Mover_PreMove set, once per frame. */
void Mover_DynamicRotation(struct PhysicalObject *mover) {
    Quaternion quat __attribute__((aligned(16)));
    Matrix4 mtx;
    Thing *thing = &mover->d.base;

    if (!(thing->flags & 0x10)) {
        thing->flags |= 0x10;
        quat = mover->d.moverRot;
        QuaternionToSubMatrix(&quat, &mtx);
        mtx.v[3].x = thing->pos.x;
        mtx.v[3].y = thing->pos.y;
        mtx.v[3].z = thing->pos.z;
        thing->quat = quat;
        *THING_MTX(thing) = mtx;
    }
}

/* An impulse at the contact. Something pushable (flags 0x100000) that has
 * come to rest flat on the ground or a resting thing turns into a static
 * destructible; otherwise the impulse changes its velocity and spin. */
void PhysicalObject_DynamicCollideImpact(struct PhysicalObject *obj, CollisionInfo *collision, f32 force) {
    extern Vector4 g_zeroVector;
    DestructibleParams params __attribute__((aligned(16)));
    Vector4 normal __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Vector4 center __attribute__((aligned(16)));
    Vector4 plane __attribute__((aligned(16)));
    Vector4 impulse __attribute__((aligned(16)));
    Vector4 torque __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 local2 __attribute__((aligned(16)));
    Vector4 angVel __attribute__((aligned(16)));
    Vector4 scaled1 __attribute__((aligned(16)));
    Vector4 local1 __attribute__((aligned(16)));
    Vector4 scaled3 __attribute__((aligned(16)));
    Vector4 local3 __attribute__((aligned(16)));
    f32 scale;
    Thing *other;
    u32 i;
    ConvexHull *hull;
    f32 weight;
    f32 d;
    f32 w;
    f32 inv;
    f32 mag;
    Thing *thing = &obj->d.base;

    normal = collision->normal;
    pos = collision->pos;
    scale = force / GETP(GETP(obj->d.record)->info)->mass;
    if (thing->flags & 0x200000) {
        return;
    }
    if ((thing->flags & 0x100000) && normal.y > 0.9f && _fabs(scale) < 2.0f &&
        ((other = PTR(collision->thingB)) == NULL || (other->flags & THING_FLAG_AT_REST))) {
        Vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};

        /* how much of it lies on the contact plane */
        weight = 0.0f;
        center = zero;
        Create_Plane(&plane, &normal, &collision->pos);
        switch (GETP(GETP(obj->d.record)->info)->type) {
        case 0:
            for (i = 0; i < 8; i++) {
                d = Distance_Plane(&plane, &GETP(obj->d.orientedBox)->corners[i]);
                if (d < 0.15f) {
                    w = (0.15f - d) / 0.15f;
                    center.x += GETP(obj->d.orientedBox)->corners[i].x * w;
                    center.y += GETP(obj->d.orientedBox)->corners[i].y * w;
                    center.z += GETP(obj->d.orientedBox)->corners[i].z * w;
                    weight += w;
                }
            }
            break;
        case 1:
        case 6:
            hull = GETP(GETP(GETP(obj->d.record)->info)->hull);
            for (i = 0; i < hull->numVerts; i++) {
                d = Distance_Plane(&plane, &GETP(obj->d.hull)->verts[i]);
                if (d < 0.15f) {
                    w = (0.15f - d) / 0.15f;
                    center.x += w * GETP(obj->d.hull)->verts[i].x;
                    center.y += w * GETP(obj->d.hull)->verts[i].y;
                    center.z += w * GETP(obj->d.hull)->verts[i].z;
                    weight += w;
                }
            }
            break;
        }
        if (weight >= 3.0f) {
            ProjectVectorToNormal(&collision->normal, &obj->angMomentum);
            InverseXformVecOrientation(THING_MTX(thing), &obj->angMomentum, &local1);
            scaled1.x = obj->invInertia.x * local1.x;
            scaled1.y = obj->invInertia.y * local1.y;
            scaled1.z = obj->invInertia.z * local1.z;
            XformVecOrientation(THING_MTX(thing), &scaled1, &obj->angVel);
            if (PSVECSquareMag(&obj->angVel) < 1.0f && PSVECSquareMag(&thing->vel) < 25.0f) {
                /* settled: replace by a static destructible (the rest of
                 * params is left unset, as in the original) */
                params.pos = thing->pos;
                params.moveType = DESTRUCTIBLE_STATIC;
                params.unk4C = 0;
                params.vel = g_zeroVector;
                SETP(params.record, GETP(obj->d.record));
                params.quat = thing->quat;
                params.angVel = obj->angVel;
                ThingManager_DARYL_CreateThing(3, &params);
                Destructible_Release(&obj->d);
            }
        }
    }
    thing->vel.x += normal.x * scale;
    thing->vel.y += normal.y * scale;
    thing->vel.z += normal.z * scale;
    PSVECSubtract(&pos, &thing->pos, &r);
    PSVECScale(&normal, &impulse, force);
    PSVECCrossProduct(&r, &impulse, &torque);
    obj->angMomentum.x += torque.x;
    obj->angMomentum.y += torque.y;
    obj->angMomentum.z += torque.z;
    InverseXformVecOrientation(THING_MTX(thing), &obj->angMomentum, &local2);
    angVel.x = obj->invInertia.x * local2.x;
    angVel.y = obj->invInertia.y * local2.y;
    angVel.z = obj->invInertia.z * local2.z;
    mag = PSVECSquareMag(&angVel);
    if (mag > 10000.0f) {
        /* cap the spin at 100 rad/s */
        inv = 100.0f / Sqrt(mag);
        angVel.x *= inv;
        angVel.y *= inv;
        angVel.z *= inv;
        local2.x = angVel.x / obj->invInertia.x;
        local2.y = angVel.y / obj->invInertia.y;
        local2.z = angVel.z / obj->invInertia.z;
        XformVecOrientation(THING_MTX(thing), &local2, &obj->angMomentum);
    }
    InverseXformVecOrientation(THING_MTX(thing), &obj->angMomentum, &local3);
    scaled3.x = obj->invInertia.x * local3.x;
    scaled3.y = obj->invInertia.y * local3.y;
    scaled3.z = obj->invInertia.z * local3.z;
    XformVecOrientation(THING_MTX(thing), &scaled3, &obj->angVel);
}

void PhysicalObject_GetInverseLocalInertiaTensor(struct PhysicalObject *obj, Quaternion *out) {
    *out = *(Quaternion *)&obj->invInertia;
}

/* As Destructible_CollideWithRay, with the velocity at the hit point
 * including the spin. */
f32 PhysicalObject_CollideWithRay(struct PhysicalObject *obj, Vector4 *start, Vector4 *end, Vector4 *hit, f32 maxDist) {
    Vector4 cross __attribute__((aligned(16)));
    Vector4 offset __attribute__((aligned(16)));
    Vector4 angVel __attribute__((aligned(16)));
    f32 dist;
    Thing *thing = &obj->d.base;

    dist = PhysicalProperties_CollideWithRay(GETP(GETP(obj->d.record)->info), THING_MTX(thing), start, end, maxDist, hit);
    if (hit != NULL && dist < maxDist) {
        angVel = obj->angVel;
        PSVECSubtract(hit, &thing->pos, &offset);
        PSVECCrossProduct(&angVel, &offset, &cross);
        PSVECAdd(&thing->vel, &cross, &hit[3]);
    }
    return dist;
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_physicalObjectVTable[17];

__attribute__((constructor)) static void PortInit_g_physicalObjectVTable(void) {
    static void *const fns[] = {
        (void *)PhysicalObject_Init,
        (void *)Destructible_Release,
        (void *)PhysicalObject_PreMove,
        (void *)PhysicalObject_Move,
        (void *)Destructible_Draw,
        (void *)Destructible_GetMass,
        (void *)PhysicalObject_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransform,
        (void *)Destructible_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)PhysicalObject_GetInverseLocalInertiaTensor,
        (void *)PhysicalObject_DynamicRotation,
        (void *)PhysicalObject_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Destructible_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_physicalObjectVTable[i] = HND(fns[i]);
    }
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_moverVTable[17];

__attribute__((constructor)) static void PortInit_g_moverVTable(void) {
    static void *const fns[] = {
        (void *)Mover_Init,
        (void *)Destructible_Release,
        (void *)Mover_PreMove,
        (void *)Destructible_Move,
        (void *)Destructible_Draw,
        (void *)Destructible_GetMass,
        (void *)Destructible_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransform,
        (void *)Destructible_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Mover_DynamicRotation,
        (void *)PhysicalObject_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Destructible_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_moverVTable[i] = HND(fns[i]);
    }
}
