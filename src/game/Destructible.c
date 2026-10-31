/* Destructibles: placed world objects that can be shot up and destroyed;
 * also the base of movers and physical objects (PhysicalObject.c). */
#include "common.h"
#include "CarA.h"
#include "AttachThing.h"
#include "Destructible.h"
#include "World.h"


extern f32 g_zeroVector[4];
extern u32 g_destructibleVTable[];


/* The bounding radius around `offset` of the box box[0] (min) .. box[1] (max). */
f32 GetBoundingBoxRadius(Vector4 *box, Vector4 *offset)
{
    Vector4 corners[8] __attribute__((aligned(16)));
    f32 radius;
    f32 mag;
    u32 i;

    radius = -99999.9f;
    corners[0].x = box[1].x;
    corners[0].y = box[1].y;
    corners[0].z = box[1].z;
    corners[1].x = box[1].x;
    corners[1].y = box[1].y;
    corners[1].z = box[0].z;
    corners[2].x = box[1].x;
    corners[2].y = box[0].y;
    corners[2].z = box[1].z;
    corners[3].x = box[1].x;
    corners[3].y = box[0].y;
    corners[3].z = box[0].z;
    corners[4].x = box[0].x;
    corners[4].y = box[1].y;
    corners[4].z = box[1].z;
    corners[5].x = box[0].x;
    corners[5].y = box[1].y;
    corners[5].z = box[0].z;
    corners[6].x = box[0].x;
    corners[6].y = box[0].y;
    corners[6].z = box[1].z;
    corners[7].x = box[0].x;
    corners[7].y = box[0].y;
    corners[7].z = box[0].z;
    for (i = 0; i < 8; i++)
    {
        corners[i].x += offset->x;
        corners[i].y += offset->y;
        corners[i].z += offset->z;
        mag = MagnitudeVector(&corners[i]);
        if (mag > radius)
        {
            radius = mag;
        }
    }
    return radius;
}

extern u8 g_moverVTable[];
extern u8 g_physicalObjectVTable[];
extern u8 g_droneVTable[];

/* The start of a geometry: its local bounds (v[0].w: bounding radius). */
typedef struct GeometryBounds {
    u8 pad0[0x10];
    OBox bounds;                /* 0x10 */
} GeometryBounds;

/* Set up a destructible from its record: collision shape and radius, the
 * kind of motion (static, mover, physical object, drone: each its own
 * vtable), health and slaves. Physical objects and movers come here too. */
s32 Destructible_Init(Destructible *destructible, void *data) {
    typedef struct CollisionSphere {
        Vector4 pos;
        f32 radius;
        u8 pad14[0xC];
    } CollisionSphere;
    typedef struct CollisionSpheres {
        u32 count;
        PTR32(CollisionSphere) spheres;
    } CollisionSpheres;
    /* ThingManager_DARYL_CreateSlave(0xF, ...): a motion model */
    typedef struct SlaveParams {
        Vector4 pos;
        Vector4 vel;
        Vector4 quat;
        PTR32(void) geometry;
        PTR32(void) slaves;
    } SlaveParams;
    CollisionSpheres *spheres;
    OBox *box;
    DestructibleParams *params = data;
    Vector4 corner __attribute__((aligned(16)));
    Vector4 zero;
    SlaveParams slaveParams;
    CollisionOBox *obb;
    ThingRecord *record;
    DestructibleInfo *info;
    Thing *thing = &destructible->base;
    Thing *slave;
    f32 r;

    thing->flags = 0;
    Thing_Init(thing, (s32)HND(data));
    thing->quat = params->quat;
    thing->vel = params->vel;
    QuaternionToSubMatrix(&thing->quat, THING_MTX(thing));
    thing->pos = params->pos;
    SETP(destructible->record, GETP(params->record));
    SETP(destructible->orientedBox, NULL);
    SETP(destructible->hull, NULL);
    SETP(destructible->anim, NULL);
    switch (GETP(GETP(destructible->record)->info)->type) {
    case 3:
        /* a box taken from the geometry's bounds; from now on a plain box */
        box = GETP(GETP(GETP(destructible->record)->info)->box);
        *box = ((GeometryBounds *)GETP(GETP(destructible->record)->geometry))->bounds;
        box->v[2].x = GetBoundingBoxRadius(box->v, (Vector4 *)g_zeroVector);
        GETP(GETP(destructible->record)->info)->type = 0;
        break;
    }
    thing->flags |= 4;
    switch (GETP(GETP(destructible->record)->info)->type) {
    case 0: {
        u32 i;

        thing->collisionType = THING_COLLIDE_OBOX;
        thing->flags |= 0x80;
        SETP(destructible->orientedBox, OBoxHolder_Get());
        GETP(destructible->orientedBox)->cachedPoly = 0;
        obb = Destructible_GetOrientedCollision(destructible);
        obb->min.x = 99999.9f;
        obb->min.y = 99999.9f;
        obb->min.z = 99999.9f;
        obb->max.x = -99999.9f;
        obb->max.y = -99999.9f;
        obb->max.z = -99999.9f;
        for (i = 0; i < 8; i++) {
            corner = obb->corners[i];
            if (corner.x < obb->min.x) {
                obb->min.x = corner.x;
            }
            if (corner.y < obb->min.y) {
                obb->min.y = corner.y;
            }
            if (corner.z < obb->min.z) {
                obb->min.z = corner.z;
            }
            if (corner.x > obb->max.x) {
                obb->max.x = corner.x;
            }
            if (corner.y > obb->max.y) {
                obb->max.y = corner.y;
            }
            if (corner.z > obb->max.z) {
                obb->max.z = corner.z;
            }
        }
        box = GETP(GETP(GETP(destructible->record)->info)->box);
        box->v[2].x = GetBoundingBoxRadius(box->v, (Vector4 *)g_zeroVector);
        thing->radius = box->v[2].x;
        break;
    }
    case 1:
        thing->collisionType = 4;
        SETP(destructible->hull, OConvexHullHolder_Get(GETP(GETP(GETP(destructible->record)->info)->hull)));
        GETP(destructible->hull)->unk3CC = 0;
        thing->radius = 30.0f;
        break;
    case 4:
        thing->collisionType = THING_COLLIDE_NONE;
        thing->flags |= 0x20002;
        break;
    case 6:
        thing->collisionType = 5;
        SETP(destructible->hull, OConvexHullHolder_Get(GETP(GETP(GETP(destructible->record)->info)->hull)));
        GETP(destructible->hull)->unk3CC = 1;
        thing->radius = 30.0f;
        break;
    case 7: {
        u32 i;

        thing->collisionType = THING_COLLIDE_SPHERES;
        spheres = (CollisionSpheres *)PTR(Thing_GetLocalCollision((u8 *)destructible));
        thing->radius = 0.0f;
        for (i = 0; i < spheres->count; i++) {
            r = GETP(spheres->spheres)[i].radius + MagnitudeVector(&GETP(spheres->spheres)[i].pos);
            if (r > thing->radius) {
                thing->radius = r;
            }
        }
        break;
    }
    default:
        thing->flags &= ~4;
        thing->flags |= 0x60002;
        thing->collisionType = THING_COLLIDE_NONE;
        thing->radius = 0.0f;
        break;
    }
    thing->flags |= THING_FLAG_NO_TERRAIN_HITS;
    switch (params->moveType) {
    case DESTRUCTIBLE_STATIC:
        thing->flags |= THING_FLAG_AT_REST;
        SETP(destructible->anim, NULL);
        SETP(thing->vtable, g_destructibleVTable);
        thing->flags |= 0x40000000;
        thing->flags &= ~4;
        break;
    case DESTRUCTIBLE_MOVER:
        thing->flags &= ~THING_FLAG_AT_REST;
        thing->flags |= 0x4000000;
        SETP(destructible->anim, GETP(params->anim));
        destructible->trackPos = 0.0f;
        destructible->speed = params->speed;
        SETP(thing->vtable, g_moverVTable);
        break;
    case DESTRUCTIBLE_PHYSICAL:
        thing->flags |= THING_FLAG_NO_OBJECT_HITS;
        thing->flags |= 0x100000;
        thing->flags &= ~4;
        thing->flags &= ~THING_FLAG_AT_REST;
        thing->flags &= ~THING_FLAG_NO_TERRAIN_HITS;
        SETP(destructible->anim, NULL);
        SETP(thing->vtable, g_physicalObjectVTable);
        break;
    case DESTRUCTIBLE_DRONE:
        thing->flags |= THING_FLAG_NO_OBJECT_HITS;
        thing->flags &= ~4;
        thing->flags &= ~THING_FLAG_AT_REST;
        thing->flags &= ~THING_FLAG_NO_TERRAIN_HITS;
        SETP(destructible->anim, NULL);
        SETP(thing->vtable, g_droneVTable);
        break;
    }
    info = GETP(GETP(destructible->record)->info);
    if (info->deathTime < 0) {
        thing->flags |= 0x10000000;  /* no death timer */
    } else {
        destructible->deathTimer = info->deathTime;
    }
    if (info->takesDamage) {
        thing->health = info->maxHealth;
    } else {
        thing->health = 1;
    }
    destructible->drawDist = params->drawDist;
    if (0.0f == params->drawDist && GETP(params->record)->hasDrawDist) {
        destructible->drawDist = GETP(params->record)->drawDist;
    }
    if (!GETP(params->record)->collides) {
        thing->flags |= 0x200;
    }
    record = GETP(destructible->record);
    switch (record->noDraw) {
    case 0:
        Thing_InitAllSlavesNow(HND(destructible), record->slaves, 0xFF);
        if (((GeometryBounds *)GETP(GETP(destructible->record)->geometry))->bounds.v[0].w > thing->radius) {
            thing->radius = ((GeometryBounds *)GETP(GETP(destructible->record)->geometry))->bounds.v[0].w;
        }
        break;
    case 1:
        zero = *(Vector4 *)g_zeroVector;
        slaveParams.pos = zero;
        slaveParams.vel = zero;
        slaveParams.quat = zero;
        SETP(slaveParams.geometry, GETP(record->geometry));
        SETP(slaveParams.slaves, record->slaves);
        slave = ThingManager_DARYL_CreateSlave(0xF, &slaveParams, 0xFF);
        if (slave != NULL) {
            slave->slaveTag = 0;
            ThingManagerList_Append(&thing->slaves, slave);
        }
        break;
    }
    thing->damage = 0;
    if (!(thing->flags & 0x20002)) {
        Thing_AddToGrid(destructible, 2);
    }
    if (thing->flags & 2) {
        thing->flags &= ~4;
    }
    return 1;
}

/* Release a destructible and its pooled collision shape. A static one also
 * clears its spawn entry (FileStreaming StaticEntry: thing 0x2C, active 0x30). */
void Destructible_Release(Destructible *destructible) {
    s8 paused;
    QuadInstance *entry;

    Trigger_DestructibleKilled(destructible);
    if (GETP(destructible->base.vtable) == g_destructibleVTable) {
        entry = GETP(destructible->base.trigger);
        if (entry != NULL) {
            SETP(destructible->base.trigger, NULL);
            entry->thing.h = 0;
            paused = Trigger_IsTACSystemPaused();
            if (paused == 0) {
                entry->mode = 0;
            }
        }
    }
    OBoxHolder_Release(GETP(destructible->orientedBox));
    SETP(destructible->orientedBox, NULL);
    OConvexHullHolder_Release(GETP(destructible->hull));
    SETP(destructible->hull, NULL);
    Thing_Release(destructible);
}

/* Per frame: turn a wildcard rack slave (type 8) by its player's stick, run
 * the death timer (spawning one of info->debris when it runs out), and
 * feed the health fraction to a radar blip slave (type 0x11). */
s32 Destructible_Move(Destructible *destructible) {
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    DebrisParams params __attribute__((aligned(16)));
    Quaternion rot __attribute__((aligned(16)));
    Quaternion quat __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    f32 angle;
    Thing *slave;
    RadarBlip *blip;
    u32 id;
    s8 spin;
    DestructibleInfo *info;
    Thing *thing = &destructible->base;

    if (thing->flags & 0x200000) {
        return 0;
    }
    if (GETP(thing->player) != NULL &&
        (slave = ThingManagerList_GetByType(&thing->slaves, 8)) != NULL &&
        GETP(slave->player) != NULL &&
        (spin = -((Player *)GETP(slave->player))->stickX) != 0) {
        axis.x = 0.0f;
        axis.y = 0.017453292f;
        axis.z = 0.0f;
        GenerateQuaternionXYZ(&axis, &quat);
        GenerateSpinAngleFromQuaternion(&quat, &axis, &angle);
        GenerateQuaternion(&axis, &rot, spin * (angle * g_fDeltaTimeSec));
        QuaternionMult(&slave->quat, &rot, &quat);
        slave->quat = quat;
        QuaternionToSubMatrix(&slave->quat, THING_MTX(slave));
    }
    if (!(thing->flags & 0x10000000)) {
        if (destructible->deathTimer < 0) {
            id = 0;
            info = GETP(GETP(destructible->record)->info);
            if (info != NULL && info->numDebris != 0) {
                id = GETP(info->debris)[RandomAdd_s32(0, info->numDebris)];
            }
            if (id != 0) {
                params.debris = id;
                params.pos = thing->pos;
                params.quat = *(Vector4 *)&thing->quat;
                params.vel = thing->vel;
                params.owner = 0;
                params.unk48 = 0.0f;
                params.unk4C = 0.0f;
                params.angVel = *(Vector4 *)g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &params);
            }
            Destructible_Release(destructible);
        }
        destructible->deathTimer -= g_iDeltaTimeMS;
    }
    Timer_Interrupt300();
    blip = ThingManagerList_GetByType(&thing->slaves, 0x11);
    if (blip != NULL) {
        if (GETP(GETP(destructible->record)->info)->maxHealth != 0) {
            blip->health = (f32)thing->health / (f32)GETP(GETP(destructible->record)->info)->maxHealth;
        } else {
            blip->health = 0.0f;
        }
        if (blip->health > 1.0f) {
            blip->health = 1.0f;
        }
    }
    return 1;
}

/* Draw the geometry (unless beyond the draw distance) and shared slaves. */
void Destructible_Draw(Destructible *destructible) {
    f32 distSq;
    ThingRecord *record;

    if (destructible->drawDist) {
        distSq = Gfx_GetMinCameraDistSquared(&destructible->base.pos);
        if ((u32)ViewManager_GetViewFormat() > 1) {
            distSq *= 4.0f;
        }
        if (distSq > destructible->drawDist * destructible->drawDist) {
            return;
        }
    }
    QuaternionToSubMatrix(&destructible->base.quat, THING_MTX(&destructible->base));
    record = GETP(destructible->record);
    if (record->noDraw == 0) {
        Gfx_DrawGeometry_MatrixNotCached(HND(GETP(record->geometry)), HND(THING_MTX(&destructible->base)));
    }
    Thing_DrawSharedSlaves(destructible, GETP(destructible->record)->slaves);
}

f32 Destructible_GetMass(Destructible *destructible) {
    return GETP(GETP(destructible->record)->info)->mass;
}

/* A hit: take the damage (less armour). Once dead, the push of the impact
 * becomes the spin of the end event's debris and it is released. */
void Destructible_DynamicCollideImpact(Destructible *destructible, CollisionInfo *collision, s32 damage, f32 force) {
    DestructibleInfo *info;
    OBox box;
    Vector4 angVel __attribute__((aligned(16)));
    Vector4 torque __attribute__((aligned(16)));
    Vector4 impulse __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Vector4 scaled __attribute__((aligned(16)));
    f32 scale;
    /* port: the original leaves these unset for hull types (1, 6), so the
     * impulse there was scaled by whatever the caller left in f29-f31. Use
     * 0 (no spin) rather than garbage/NaN. */
    f32 invX = 0.0f;
    f32 invY = 0.0f;
    f32 invZ = 0.0f;
    f32 inertia;
    Thing *other;
    Thing *thing = &destructible->base;
    QuadInstance *entry;

    info = GETP(GETP(destructible->record)->info);
    damage -= info->armour;
    if (thing->flags & 0x200000) {
        return;
    }
    if (info->takesDamage && damage > 0) {
        thing->health -= damage;
    }
    if (thing->health > 0) {
        return;
    }
    scale = force / GETP(GETP(destructible->record)->info)->mass;
    thing->vel.x += collision->normal.x * scale;
    thing->vel.y += collision->normal.y * scale;
    thing->vel.z += collision->normal.z * scale;
    PSVECSubtract(&collision->pos, &thing->pos, &r);
    impulse.x = force * collision->normal.x;
    impulse.y = force * collision->normal.y;
    impulse.z = force * collision->normal.z;
    PSVECCrossProduct(&r, &impulse, &torque);
    info = GETP(GETP(destructible->record)->info);
    switch (info->type) {
    case 0:
        box = *GETP(info->box);
        invX = 1.0f / (info->inertia[0] * info->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
        invY = 1.0f / (info->inertia[1] * info->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
        invZ = 1.0f / (info->inertia[2] * info->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
        break;
    case 1:
        break;
    case 7:
        inertia = 2.0f * info->mass * thing->radius * thing->radius;
        invX = 1.0f / (info->inertia[0] * inertia);
        invY = 1.0f / (info->inertia[1] * inertia);
        invZ = 1.0f / (info->inertia[2] * inertia);
        break;
    }
    InverseXformVecOrientation(THING_MTX(thing), &torque, &local);
    scaled.x = invX * local.x;
    scaled.y = invY * local.y;
    scaled.z = invZ * local.z;
    XformVecOrientation(THING_MTX(thing), &scaled, &angVel);
    Thing_ProcessEndEvent(thing, (CarACollider *)GETP(GETP(destructible->record)->info), collision, &angVel);
    /* its quadrant entry in mode 26: kept, hidden */
    entry = GETP(thing->trigger);
    if (entry != NULL && entry->mode == 0x1A) {
        thing->flags |= 0x200;
        thing->flags |= 2;
        return;
    }
    other = PTR(CollisionInfo_GetOther(collision, HND(destructible)));
    if (other != NULL && GETP(other->player) != NULL && Player_IsHuman(GETP(other->player))) {
        thing->flags |= 0x20;  /* destroyed by a human player */
    }
    Destructible_Release(destructible);
    entry = GETP(thing->trigger);
    if (entry != NULL) {
        entry->mode = 3;
    }
}

/* port: returns the material pointer (vtable GetSurfaceScript); the field is a slot */
void *Destructible_GetSurfaceScript(Destructible *destructible) {
    return GETP(GETP(GETP(destructible->record)->info)->material);
}

/* Whether the record lies in [start, end) (a data pack being unloaded). */
u32 Destructible_Invalidate(Destructible *destructible, u32 start, u32 end) {
    if ((u32)HND(GETP(destructible->record)) >= start && (u32)HND(GETP(destructible->record)) < end) {
        return 1;
    }
    return 0;
}

/* Trigger action on static objects: action = { mode, count, entries[count] }
 * (FileStreaming StaticEntry: thing 0x2C, active 0x30 gets the mode). */
s32 ActionDestructibleChangeMode(u32 *action) {
    s32 mode;
    u32 count;
    u32 i;
    QuadInstance *entry;
    Destructible *destructible;

    mode = action[0];
    count = action[1];
    for (i = 0; i < count; i++) {
        entry = (QuadInstance *)PTR(action[i + 2]);
        destructible = (Destructible *)GETP(entry->thing);
        if (destructible != NULL) {
            switch (mode) {
            case 3:
                /* destroy */
                Thing_ProcessEndEvent(&destructible->base, (CarACollider *)GETP(GETP(destructible->record)->info), NULL, NULL);
                Destructible_Release(destructible);
                break;
            case 0:
                /* hide */
                ThingManagerList_ReleaseSlavesBySlaveType(&destructible->base.slaves, 0xFF);
                destructible->base.flags |= 0x200;
                destructible->base.flags |= 0x2;
                break;
            case 1:
                /* show */
                Thing_InitAllSlavesNow(HND(destructible), GETP(destructible->record)->slaves, 0xFF);
                destructible->base.flags &= ~0x200;
                destructible->base.flags &= ~0x2;
                break;
            case 25:
                Destructible_Release(destructible);
                break;
            case 26:
                /* destroyed, but kept hidden */
                ThingManagerList_ReleaseSlavesBySlaveType(&destructible->base.slaves, 0xFF);
                Thing_ProcessEndEvent(&destructible->base, (CarACollider *)GETP(GETP(destructible->record)->info), NULL, NULL);
                destructible->base.flags |= 0x200;
                destructible->base.flags |= 0x2;
                break;
            }
        }
        entry->mode = mode;
    }
    return 1;
}

/* Trigger action on movers: action = { speed mode, position mode, speed,
 * ?, count, entries[count] } (an entry holds its mover at 0x10). */
s32 ActionMoveableChangeMode(u32 *action) {
    typedef struct {
        u8 pad0[0x10];
        PTR32(Destructible) mover;
    } MoverEntry;
    u32 i;
    Destructible *mover;

    for (i = 0; i < action[4]; i++) {
        mover = GETP(((MoverEntry *)PTR(action[i + 5]))->mover);
        if (mover != NULL) {
            switch (action[0]) {
            case 6:
                mover->speed = action[2];
                break;
            case 12:
                mover->base.flags |= 0x4000;  /* stopped (Mover_PreMove) */
                break;
            case 11:
                mover->base.flags &= ~0x4000;
                mover->speed = action[2];
                break;
            case 3:
                THING_VFN(mover, THING_V_RELEASE, void (*)(void *))(mover);
                break;
            }
            switch (action[1]) {
            case 13:
                /* to the start of the track */
                if (GETP(mover->anim) != NULL) {
                    mover->trackPos = GETP(mover->anim)->start;
                }
                Mover_GetTrackPos(mover, (f32 *)&mover->base.pos, mover->trackPos);
                break;
            case 14:
                /* to the end */
                if (GETP(mover->anim) != NULL) {
                    mover->trackPos = GETP(mover->anim)->end;
                }
                Mover_GetTrackPos(mover, (f32 *)&mover->base.pos, mover->trackPos);
                break;
            }
        }
    }
    return 1;
}

/* Returns the hit distance (maxDist if none), as the original does in f1.
 * hit: position, normal..., with the velocity at the hit point at [3]. */
f32 Destructible_CollideWithRay(Destructible *destructible, Vector4 *start, Vector4 *end, Vector4 *hit, f32 maxDist) {
    f32 dist;

    dist = PhysicalProperties_CollideWithRay(GETP(GETP(destructible->record)->info), THING_MTX(&destructible->base), start, end, maxDist, hit);
    if (hit != NULL && dist < maxDist) {
        hit[3] = destructible->base.vel;
    }
    return dist;
}

/* Aim point: the centre of the collision shape. */
void Destructible_GetTargetPos(Destructible *destructible, Vector4 *pos) {
    switch (GETP(GETP(destructible->record)->info)->type) {
    case 0:
    case 3:
        *pos = GETP(destructible->orientedBox)->center;
        break;
    case 1:
    case 6:
        *pos = GETP(destructible->hull)->pos;
        break;
    default:
        *pos = destructible->base.pos;
        break;
    }
}

/* The world-space collision shape, a CollisionOBox (rebuilt when the thing
 * moved: flags 0x180) or an OConvexHull. */
void *Destructible_GetOrientedCollision(Destructible *destructible) {
    DestructibleInfo *info;

    info = GETP(GETP(destructible->record)->info);
    switch (info->type) {
    case 0:
    case 3:
        if (destructible->base.flags & 0x180) {
            BuildOOB(GETP(info->box), THING_MTX(&destructible->base), GETP(destructible->orientedBox));
            destructible->base.flags &= ~0x180;
        }
        return GETP(destructible->orientedBox);
    case 1:
    case 6:
        BuildOConvexHullOffset(GETP(destructible->hull), GETP(info->hull), THING_MTX(&destructible->base), g_zeroVector);
        return GETP(destructible->hull);
    default:
        return NULL;
    }
}


/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_destructibleVTable[17];

__attribute__((constructor)) static void PortInit_g_destructibleVTable(void) {
    static void *const fns[] = {
        (void *)Destructible_Init,
        (void *)Destructible_Release,
        (void *)Thing_PreMove,
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
        (void *)Thing_DynamicRotation,
        (void *)Destructible_CollideWithRay,
        (void *)Destructible_GetTargetPos,
        (void *)Destructible_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_destructibleVTable[i] = HND(fns[i]);
    }
}
