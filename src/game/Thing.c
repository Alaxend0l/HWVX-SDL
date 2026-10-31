#include "common.h"
#include "CarA.h"



/* Pickup by a plain thing (type 11): only wildcards, into its rack slave. */
static inline s8 Thing_ProcessItemPickup(Thing *thing, ItemThing *item) {
    void *rack;
    u8 *spawner;

    switch (GETP(item->record)->type) {
    case 0:
    case 1:
    case 2:
        break;
    case ITEM_TYPE_WILDCARD:
        rack = ThingManagerList_GetByType(&thing->slaves, 8);
        if (rack == NULL) {
            break;
        }
        WildCardRack_Add(rack, GETP(item->record), item->ammo);
        Item_Release(item);
        /* restart the spawner's respawn timer */
        spawner = GETP(item->spawner);
        if (spawner != NULL) {
            *(s32 *)(spawner + 0x18) = *(s32 *)(spawner + 0x14);
        }
        return 1;
    }
    return 0;
}

s32 Thing_Init(Thing *thing, s32 arg1) {
    extern Matrix4 g_identity;

    thing->vel.z = 0.0f;
    thing->vel.y = 0.0f;
    thing->vel.x = 0.0f;
    thing->pushVel.z = 0.0f;
    thing->pushVel.y = 0.0f;
    thing->pushVel.x = 0.0f;
    thing->radius = 0.0f;
    thing->quat.w = 1.0f;
    thing->quat.x = 0.0f;
    thing->quat.y = 0.0f;
    thing->quat.z = 0.0f;
    thing->flags = 0;
    thing->collisionType = THING_COLLIDE_NONE;
    *THING_MTX(thing) = g_identity;
    SETP(thing->player, NULL);
    thing->slaveBone = 0xFF;
    thing->slaveTag = 0xFF;
    SETP(thing->slaves, NULL);
    SETP(thing->gridObject, NULL);
    return 1;
}

/* port: takes a pointer, as the vtable calls it; the decomp has s32 */
void Thing_Release(void *arg) {
    Thing *thing = arg;
    void *player;

    thing->flags |= 0x200;
    thing->flags |= 2;
    thing->flags |= 0x200000;
    if (GETP(thing->gridObject) != NULL) {
        Grid_FreeObject(GETP(thing->gridObject));
    }
    SETP(thing->gridObject, NULL);
    /* the slaves are released with their master, keeping its transform */
    ThingManagerList_EmancipateSlaves(THING_MTX(thing), (void **)&thing->slaves);
    ThingManagerList_ReleaseAll((void **)&thing->slaves);
    ThingManagerList_RemoveReleasedThings((void **)&thing->slaves);
    player = GETP(thing->player);
    if (player != NULL && GETP(((Player *)player)->vehicle) == thing) {
        Player_DetachThingDeath(player);
    }
}

/* The collision shape handle (box or hull) from the thing's physical
 * properties. Items have none: the original then reads through NULL, as
 * does this (callers only ask for things that collide). */
s32 Thing_GetLocalCollision(u8 *thing) {
    extern u8 g_destructibleVTable[];
    extern u8 g_moverVTable[];
    extern u8 g_physicalObjectVTable[];
    extern u8 g_itemVTable[];
    extern u8 g_droneVTable[];
    u8 *vtable = (u8 *)GETP(((Thing *)thing)->vtable);
    DestructibleInfo *physics;

    if (vtable == g_carAVTable) {
        physics = (DestructibleInfo *)GETP(GETP(((CarA *)thing)->vehicleIface)->collider);
    } else if (vtable == g_destructibleVTable || vtable == g_moverVTable || vtable == g_physicalObjectVTable) {
        physics = GETP(GETP(((Destructible *)thing)->record)->info);
    } else if (vtable == g_itemVTable) {
        physics = NULL;
    } else if (vtable == g_droneVTable) {
        physics = GETP(GETP(((Destructible *)thing)->record)->info);
    } else {
        physics = NULL;
    }
    switch (physics->type) {
    case 0:
        return physics->box.h;
    case 1:
        return physics->hull.h;
    case 7:
        return physics->box.h;
    default:
        return 0;
    }
}

s32 Thing_PreMove(void) {
    return 1;
}

s32 Thing_Move(void) {
    return 1;
}

void Thing_Draw(void)
{

}

/* Whether a's bounding sphere, moving at the relative velocity for t
 * seconds, touches b's. */
static inline s8 Thing_SweptSpheresTouch(Thing *a, Thing *b, f32 t) {
    Vector4 end;
    f32 dx = a->vel.x - b->vel.x;
    f32 dy = a->vel.y - b->vel.y;
    f32 dz = a->vel.z - b->vel.z;

    end.x = t * dx + a->pos.x;
    end.y = t * dy + a->pos.y;
    end.z = t * dz + a->pos.z;
    return SegmentVsSphere(&b->pos, &a->pos, &end, a->radius + b->radius);
}

typedef f32 (*ThingCollideWithRayFn)(void *thing, Vector4 *start, Vector4 *dir, void *hit, f32 maxDist);

/* Sweep a against b for up to *t seconds by their collision types; on a hit
 * *t becomes the time of impact and hit (if any) the contact. */
s8 Thing_TestThingCollision(Thing *a, Thing *b, f32 *t, CollisionInfo *hit) {
    typedef union CollisionBox {
        struct {
            Vector4 min;
            Vector4 max;
            Vector4 extra;
        };
        f64 d[6];
    } CollisionBox __attribute__((aligned(16)));
    typedef struct RayHit {
        Vector4 pos;
        Vector4 normal;
        u8 pad[0x30];
    } RayHit;
    f32 best;

    best = *t;
    switch (a->collisionType) {
    case THING_COLLIDE_NONE:
        break;
    case THING_COLLIDE_RAY:
        best = THING_VFN(b, THING_V_COLLIDE_WITH_RAY, ThingCollideWithRayFn)(b, &a->pos, &a->vel, hit, *t);
        break;
    case THING_COLLIDE_OBOX:
        switch (b->collisionType) {
        case THING_COLLIDE_NONE:
            break;
        case THING_COLLIDE_RAY:
            best = THING_VFN(a, THING_V_COLLIDE_WITH_RAY, ThingCollideWithRayFn)(a, &b->pos, &b->vel, hit, *t);
            break;
        case THING_COLLIDE_OBOX: {
            void *boxA;
            void *boxB;

            boxA = THING_VFN(a, THING_V_GET_ORIENTED_COLLISION, void *(*)(void *))(a);
            boxB = THING_VFN(b, THING_V_GET_ORIENTED_COLLISION, void *(*)(void *))(b);
            if (!Thing_SweptSpheresTouch(a, b, *t)) {
                return 0;
            }
            best = OBoxHitsOBox(boxA, a, boxB, b, hit, *t);
            break;
        }
        case THING_COLLIDE_SPHERES: {
            RayHit rayHit;
            RayHit contact;
            CollisionBox bounds;
            Vector4 relLocal;
            Vector4 rel;
            Vector4 world;
            Vector4 local;
            Vector4 p;
            Vector4 n;
            CollisionBox *box;
            u32 *list;
            u32 i;
            f32 d;
            f32 r;

            /* b's spheres, moving relative to a, against a's local box */
            box = (CollisionBox *)PTR(Thing_GetLocalCollision((u8 *)a));
            list = (u32 *)PTR(Thing_GetLocalCollision((u8 *)b));
            PSVECSubtract(&b->vel, &a->vel, &rel);
            InverseXformVecOrientation(THING_MTX(a), &rel, &relLocal);
            for (i = 0; i < list[0]; i++) {
                bounds = *box;
                r = ((Vector4 *)PTR((list[1] + i * 0x20)))[1].x;
                bounds.max.x += r;
                bounds.max.y += r;
                bounds.max.z += r;
                bounds.min.x -= r;
                bounds.min.y -= r;
                bounds.min.z -= r;
                XformVec(THING_MTX(b), (Vector4 *)PTR((list[1] + i * 0x20)), &world);
                InverseXformVec(THING_MTX(a), &world, &local);
                d = RayBoxTest(&local, &relLocal, &bounds.min, *t, &rayHit.pos);
                if (d < best && d > 0.0f) {
                    if (hit != NULL) {
                        p.x = d * relLocal.x + local.x;
                        p.y = d * relLocal.y + local.y;
                        p.z = d * relLocal.z + local.z;
                        contact.pos = p;
                        if (contact.pos.x < box->min.x) {
                            contact.pos.x = box->min.x;
                        }
                        if (contact.pos.y < box->min.y) {
                            contact.pos.y = box->min.y;
                        }
                        if (contact.pos.z < box->min.z) {
                            contact.pos.z = box->min.z;
                        }
                        if (contact.pos.w < box->min.w) {
                            contact.pos.w = box->min.w;
                        }
                        if (contact.pos.x > box->max.x) {
                            contact.pos.x = box->max.x;
                        }
                        if (contact.pos.y > box->max.y) {
                            contact.pos.y = box->max.y;
                        }
                        if (contact.pos.z > box->max.z) {
                            contact.pos.z = box->max.z;
                        }
                        if (contact.pos.w > box->max.w) {
                            contact.pos.w = box->max.w;
                        }
                        PSVECSubtract(&contact.pos, &p, &n);
                        contact.normal = n;
                        if (PSVECSquareMag(&contact.normal)) {
                            PSVECNormalize(&contact.normal, &contact.normal);
                        }
                        XformVec(THING_MTX(a), &contact.pos, &hit->pos);
                        XformVecOrientation(THING_MTX(a), &contact.normal, &hit->normal);
                    }
                    best = d;
                }
            }
            break;
        }
        }
        break;
    case THING_COLLIDE_SPHERES:
        switch (b->collisionType) {
        case THING_COLLIDE_RAY:
            /* sic: b's function, but with a as the thing */
            best = THING_VFN(b, THING_V_COLLIDE_WITH_RAY, ThingCollideWithRayFn)(a, &a->pos, &a->vel, hit, *t);
            break;
        case THING_COLLIDE_OBOX: {
            RayHit rayHit;
            RayHit contact;
            CollisionBox bounds;
            Vector4 relLocal;
            Vector4 rel;
            Vector4 world;
            Vector4 local;
            Vector4 p;
            Vector4 n;
            CollisionBox *box;
            u32 *list;
            u32 i;
            f32 d;
            f32 r;

            /* a's spheres, moving relative to b, against b's local box */
            box = (CollisionBox *)PTR(Thing_GetLocalCollision((u8 *)b));
            list = (u32 *)PTR(Thing_GetLocalCollision((u8 *)a));
            PSVECSubtract(&a->vel, &b->vel, &rel);
            InverseXformVecOrientation(THING_MTX(b), &rel, &relLocal);
            for (i = 0; i < list[0]; i++) {
                bounds = *box;
                r = ((Vector4 *)PTR((list[1] + i * 0x20)))[1].x;
                bounds.max.x += r;
                bounds.max.y += r;
                bounds.max.z += r;
                bounds.min.x -= r;
                bounds.min.y -= r;
                bounds.min.z -= r;
                XformVec(THING_MTX(a), (Vector4 *)PTR((list[1] + i * 0x20)), &world);
                InverseXformVec(THING_MTX(b), &world, &local);
                d = RayBoxTest(&local, &relLocal, &bounds.min, *t, &rayHit.pos);
                if (d < best) {
                    if (hit != NULL) {
                        p.x = d * relLocal.x + local.x;
                        p.y = d * relLocal.y + local.y;
                        p.z = d * relLocal.z + local.z;
                        contact.pos = p;
                        if (contact.pos.x < box->min.x) {
                            contact.pos.x = box->min.x;
                        }
                        if (contact.pos.y < box->min.y) {
                            contact.pos.y = box->min.y;
                        }
                        if (contact.pos.z < box->min.z) {
                            contact.pos.z = box->min.z;
                        }
                        if (contact.pos.w < box->min.w) {
                            contact.pos.w = box->min.w;
                        }
                        if (contact.pos.x > box->max.x) {
                            contact.pos.x = box->max.x;
                        }
                        if (contact.pos.y > box->max.y) {
                            contact.pos.y = box->max.y;
                        }
                        if (contact.pos.z > box->max.z) {
                            contact.pos.z = box->max.z;
                        }
                        if (contact.pos.w > box->max.w) {
                            contact.pos.w = box->max.w;
                        }
                        PSVECSubtract(&contact.pos, &p, &n);
                        contact.normal = n;
                        if (PSVECSquareMag(&contact.normal)) {
                            PSVECNormalize(&contact.normal, &contact.normal);
                        }
                        XformVec(THING_MTX(b), &contact.pos, &hit->pos);
                        XformVecOrientation(THING_MTX(b), &contact.normal, &hit->normal);
                    }
                    best = d;
                }
            }
            break;
        }
        case THING_COLLIDE_SPHERES:
            break;
        }
        break;
    }
    if (best < *t) {
        *t = best;
        return 1;
    }
    return 0;
}

f32 Thing_GetMass(void) {
    extern f32 g_LR_Counter;
    return g_LR_Counter;
}

void Thing_DynamicCollideImpact(void)
{
}

void Thing_DynamicRotation(void)
{

}

/* The vehicle of the thing's player (handle), or 0. */
s32 Thing_GetSource(Thing *thing) {
    Player *player;

    player = GETP(thing->player);
    if (player != NULL) {
        return player->vehicle.h;
    }
    return 0;
}

s8 Thing_HasThing(Thing *thing, void *target) {
    u8 *vtable = (u8 *)GETP(thing->vtable);
    s8 found;

    if (vtable == g_carAVTable) {
        if ((found = ThingManagerList_Get((u8 *)thing + 0x344, target)) != 0) {
            return found;
        }
    } else if (vtable == g_creatureVTable) {
        if ((found = ThingManagerList_Get((u8 *)thing + 0xE0, target)) != 0) {
            return found;
        }
    }
    return ThingManagerList_Get(&thing->slaves, target);
}

void Thing_GetCameraOrientationInfo(Thing *thing, u32 index, void *out) {
    typedef struct CameraOrientationInfo {
        u8 pad00[0x8];
        f32 unk8;
        f32 unkC;
        f32 unk10;
        f32 unk14;
        u8 pad18[0x8];
        Matrix4 mtx;
        Vector4 pos;
        u8 pad70[0x10];
        s32 unk80;
        PTR32(u8) unk84;
        u32 unk88;
        f32 unk8C;
    } CameraOrientationInfo;
    CameraOrientationInfo *info = out;
    u32 *cams;

    cams = (u32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0x9C)) + 0x100);
    info->unk88 = cams[0];
    switch (ViewManager_GetViewFormat()) {
    case 2:
        cams = (u32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0x9C)) + 0x108);
        break;
    }
    if (cams[0] == 0) {
        info->unk80 = 0;
        SETP(info->unk84, NULL);
        info->mtx = *THING_MTX(thing);
    } else {
        SETP(info->unk84, (u8 *)PTR(cams[1]) + (index % cams[0]) * 0xC);
        info->unk8C = 1.0f;
        info->mtx.v[1].x = 0.0f;
        info->mtx.v[1].y = 1.0f;
        info->mtx.v[1].z = 0.0f;
        info->unk14 = 90.0f;
        info->pos = thing->pos;
        info->unk8 = 5.0f;
        info->unkC = 5.0f;
        info->unk10 = 90.0f;
        info->mtx = *THING_MTX(thing);
    }
}

/* ---- volume triggers -----------------------------------------------------
 * Trigger data: { numVolumes, volume handles..., numSubjects, subject
 * handles... }. A subject holds the things to test in fixed slots (car 0x28,
 * creature 0x1C, item 0x24, movable 0x10); a volume is enabled by
 * ActionAirboxChangeMode. */

typedef struct TriggerVolume {
    u8 pad0[4];
    u32 enabled;                /* 0x04 */
} TriggerVolume;

#define TRIGGER_VOLUME(trigger, i) ((TriggerVolume *)PTR((trigger)[(i) + 1]))
#define TRIGGER_SUBJECT_THING(trigger, numVolumes, j, slot) \
    SLOT_LOAD(Thing *, (u8 *)PTR((trigger)[(numVolumes) + 2 + (j)]) + (slot))

s32 TriggerAirboxEntered(u32 *trigger) {
    f32 dist;
    Thing *thing;
    TriggerVolume *volume;
    u32 i;
    u32 numVolumes;
    u32 j;

    numVolumes = trigger[0];
    for (i = 0; i < numVolumes; i++) {
        volume = TRIGGER_VOLUME(trigger, i);
        if (volume->enabled == 1) {
            for (j = 0; j < trigger[numVolumes + 1]; j++) {
                thing = TRIGGER_SUBJECT_THING(trigger, numVolumes, j, 0x28);
                if (thing != NULL && IsPointWithinVolume(&thing->pos, volume, &dist)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 TriggerCreatureVolumeEntered(u32 *trigger) {
    f32 dist;
    Thing *thing;
    TriggerVolume *volume;
    u32 i;
    u32 numVolumes;
    u32 j;

    numVolumes = trigger[0];
    for (i = 0; i < numVolumes; i++) {
        volume = TRIGGER_VOLUME(trigger, i);
        if (volume->enabled == 1) {
            for (j = 0; j < trigger[numVolumes + 1]; j++) {
                thing = TRIGGER_SUBJECT_THING(trigger, numVolumes, j, 0x1C);
                if (thing != NULL && IsPointWithinVolume(&thing->pos, volume, &dist)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 TriggerItemVolumeEntered(u32 *trigger) {
    f32 dist;
    TriggerVolume *volume;
    u32 i;
    u32 numVolumes;
    u32 j;
    Thing *thing;
    Player *player;

    numVolumes = trigger[0];
    for (i = 0; i < numVolumes; i++) {
        volume = TRIGGER_VOLUME(trigger, i);
        for (j = 0; j < trigger[numVolumes + 1]; j++) {
            thing = TRIGGER_SUBJECT_THING(trigger, numVolumes, j, 0x24);
            if (thing != NULL) {
                /* a carried item: test where its carrier is */
                player = GETP(thing->player);
                if (player != NULL && GETP(player->vehicle) != NULL) {
                    thing = GETP(player->vehicle);
                }
                if (volume->enabled == 1 && IsPointWithinVolume(&thing->pos, volume, &dist)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 TriggerMovableVolumeEntered(u32 *trigger) {
    f32 dist;
    TriggerVolume *volume;
    u32 i;
    u32 numVolumes;
    u32 j;
    Thing *thing;

    numVolumes = trigger[0];
    for (i = 0; i < numVolumes; i++) {
        volume = TRIGGER_VOLUME(trigger, i);
        for (j = 0; j < trigger[numVolumes + 1]; j++) {
            thing = TRIGGER_SUBJECT_THING(trigger, numVolumes, j, 0x10);
            if (thing != NULL && volume->enabled == 1 && IsPointWithinVolume(&thing->pos, volume, &dist)) {
                return 1;
            }
        }
    }
    return 0;
}

/* A car came to rest (speed^2 < 50) inside an enabled volume. */
s32 TriggerAirboxStopIn(u32 *trigger) {
    f32 dist;
    Thing *thing;
    TriggerVolume *volume;
    u32 i;
    u32 numVolumes;
    u32 j;

    numVolumes = trigger[0];
    for (i = 0; i < numVolumes; i++) {
        volume = TRIGGER_VOLUME(trigger, i);
        for (j = 0; j < trigger[numVolumes + 1]; j++) {
            thing = TRIGGER_SUBJECT_THING(trigger, numVolumes, j, 0x28);
            if (volume->enabled == 1 && PSVECSquareMag(&thing->vel) < 50.0f && IsPointWithinVolume(&thing->pos, volume, &dist)) {
                return 1;
            }
        }
    }
    return 0;
}

s32 TriggerAirboxExited(u32 *trigger) {
    f32 dist;
    Thing *thing;
    TriggerVolume *volume;
    u32 i;
    u32 numVolumes;
    u32 j;

    numVolumes = trigger[0];
    for (i = 0; i < numVolumes; i++) {
        volume = TRIGGER_VOLUME(trigger, i);
        for (j = 0; j < trigger[numVolumes + 1]; j++) {
            thing = TRIGGER_SUBJECT_THING(trigger, numVolumes, j, 0x28);
            if (volume->enabled == 1 && IsPointWithinVolume(&thing->pos, volume, &dist)) {
                return 0;
            }
        }
    }
    return 1;
}

/* data: { mode (0 off, 1 on, 2 toggle), count, volume handles... } */
s32 ActionAirboxChangeMode(s32 *data) {
    u32 count = data[1];
    s32 mode = data[0];
    u32 i;

    for (i = 0; i < count; i++) {
        TriggerVolume *airbox = (TriggerVolume *)PTR(data[i + 2]);
        switch (mode) {
        case 0:
            airbox->enabled = 0;
            break;
        case 2:
            if (airbox->enabled == 1) {
                airbox->enabled = 0;
            } else {
                airbox->enabled = 1;
            }
            break;
        case 1:
        default:
            airbox->enabled = 1;
            break;
        }
    }
    return 1;
}

void Thing_CollideWithForceBoxes(Thing *thing) {
    typedef struct ForceBox {
        Vector4 force;
        s8 oneShot;
        s8 carOnly;
        u16 type;
    } ForceBox;
    extern u8 g_carAVTable[];
    u8 *IsPointWithinForceBox(Vector4 *point);
    Vector4 *vel = &thing->vel;
    ForceBox *box;
    u8 *volume;
    u32 wasInside;
    f32 mag;
    f32 dot;
    Vector4 dir __attribute__((aligned(16)));
    Vector4 velDir __attribute__((aligned(16)));
    Vector4 forceDir __attribute__((aligned(16)));
    Vector4 dir2 __attribute__((aligned(16)));
    Vector4 velDir2 __attribute__((aligned(16)));
    Vector4 forceDir2 __attribute__((aligned(16)));

    wasInside = thing->flags & THING_FLAG_IN_FORCE_BOX;
    volume = IsPointWithinForceBox(&thing->pos);
    thing->flags &= ~THING_FLAG_IN_FORCE_BOX;
    if (volume == NULL) {
        return;
    }
    box = SLOT_LOAD(ForceBox *, (volume + 0x2C));
    if (box->carOnly && (u8 *)GETP(thing->vtable) == g_carAVTable && ((CarA *)thing)->unk460 != 0) {
        return;
    }
    if (box->oneShot) {
        if (!wasInside) {
            switch (box->type) {
            case 0:
                dir = *vel;
                mag = MagnitudeVector(box);
                if (PSVECSquareMag(&dir)) {
                    PSVECNormalize(&dir, &dir);
                }
                vel->x += dir.x * mag;
                vel->y += dir.y * mag;
                vel->z += dir.z * mag;
                break;
            case 1:
                vel->x += box->force.x;
                vel->y += box->force.y;
                vel->z += box->force.z;
                break;
            case 2:
                velDir = *vel;
                forceDir = box->force;
                if (PSVECSquareMag(&velDir)) {
                    PSVECNormalize(&velDir, &velDir);
                }
                if (PSVECSquareMag(&forceDir)) {
                    PSVECNormalize(&forceDir, &forceDir);
                }
                dot = PSVECDotProduct(&velDir, &forceDir);
                if (dot > 0.0f) {
                    vel->x += box->force.x * dot;
                    vel->y += box->force.y * dot;
                    vel->z += box->force.z * dot;
                }
                break;
            }
        }
        thing->flags |= THING_FLAG_IN_FORCE_BOX;
        return;
    }
    switch (box->type) {
    case 0:
        dir2 = *vel;
        vel->x += dir2.x;
        vel->y += dir2.y;
        vel->z += dir2.z;
        break;
    case 1:
        vel->x += box->force.x;
        vel->y += box->force.y;
        vel->z += box->force.z;
        break;
    case 2:
        velDir2 = *vel;
        forceDir2 = box->force;
        if (PSVECSquareMag(&velDir2)) {
            PSVECNormalize(&velDir2, &velDir2);
        }
        if (PSVECSquareMag(&forceDir2)) {
            PSVECNormalize(&forceDir2, &forceDir2);
        }
        dot = PSVECDotProduct(&velDir2, &forceDir2);
        if (dot > 0.0f) {
            vel->x += box->force.x * dot;
            vel->y += box->force.y * dot;
            vel->z += box->force.z * dot;
        }
        break;
    }
}

/* ---- teleporters --------------------------------------------------------- */

/* A world teleporter (world + 0x34, world + 0x30 of them); also the data of
 * ActionVehicleTeleport. */
typedef struct Teleporter {
    Matrix4 mtx;                /* 0x00 v[3] is the position */
    u8 pad40[4];                /* 0x40 */
    u32 debris;                 /* 0x44 effect spawned on use, 0: none */
    u8 pad48[4];                /* 0x48 */
    f32 radius;                 /* 0x4C */
    u32 numDest;                /* 0x50 */
    u32 flags;                  /* 0x54 TELEPORTER_* */
    PTR32(u32) dest;            /* 0x58 teleporter indices, one picked at random */
    s32 damage;                 /* 0x5C */
    u8 pad60[0x10];             /* 0x60 */
} Teleporter;

enum {
    TELEPORTER_KEEP_ROTATION = 1,  /* as destination: keep the relative orientation */
    TELEPORTER_KEEP_SPEED = 2,     /* as destination: keep the velocity */
    TELEPORTER_KILL_PLANE = 4,     /* as source: triggers below its height instead of a sphere */
    TELEPORTER_KEEP_OFFSET = 8,    /* as destination: keep the position relative to it */
};

static inline u8 Teleporter_Contains(Teleporter *tp, Thing *thing) {
    if (tp->flags & TELEPORTER_KILL_PLANE) {
        s32 hit = 0;

        if (thing->pos.y < tp->mtx.v[3].y || MagnitudeVector(&thing->pos) > 10000.0f) {
            hit = 1;
        }
        return hit;
    }
    return tp->radius * tp->radius > PSVECSquareDistance(&tp->mtx.v[3], &thing->pos);
}

static inline Teleporter *World_GetTeleporter(u32 index) {
    return SLOT_LOAD(Teleporter *, ((u8 *)GETP(g_worldData.world) + 0x34)) + index;
}

static void Teleporter_SpawnDebris(Teleporter *tp) {
    extern Vector4 g_zeroVector;
    DebrisParams params;

    params.debris = tp->debris;
    params.pos = tp->mtx.v[3];
    SubMatrixToQuaternion(tp, &params.quat);
    params.vel.x = params.vel.y = params.vel.z = 0.0f;
    params.owner = 0;
    params.unk48 = 0.0f;
    params.unk4C = 0.0f;
    params.angVel = g_zeroVector;
    ThingManager_DARYL_CreateThing(1, &params);
}

/* After a teleport an AI car must find its path again. */
static void Teleporter_ResetCarNav(Thing *car) {
    ((CarA *)car)->navPointIndex = -1;
    if (!Player_IsHuman(GETP(car->player))) {
        *(s32 *)((u8 *)GETP(car->player) + 0x4A4) = 0x20;   /* AI: path index, none */
        *(s32 *)((u8 *)GETP(car->player) + 0x634) = -0x78;
    }
}

s8 Thing_CollideWithTeleporters(u8 *arg) {
    Thing *thing = (Thing *)arg;
    Matrix4 inv;
    Matrix4 rel;
    Vector4 vel;
    Vector4 rotated;
    Vector4 neg;
    u32 i;
    Teleporter *tp;
    Teleporter *dest;
    u32 wasInside;

    wasInside = (thing->flags & THING_FLAG_IN_TELEPORTER) != 0;
    thing->flags &= ~THING_FLAG_IN_TELEPORTER;
    if (GETP(g_worldData.world) == NULL) {
        return 0;
    }
    for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x30); i++) {
        tp = World_GetTeleporter(i);
        if (tp->numDest == 0 || !Teleporter_Contains(tp, thing)) {
            continue;
        }
        if (!wasInside) {
            /* rel = the thing's transform relative to the source */
            inv = tp->mtx;
            rel = *THING_MTX(thing);
            vel = thing->vel;
            neg.x = -tp->mtx.v[3].x;
            neg.y = -tp->mtx.v[3].y;
            neg.z = -tp->mtx.v[3].z;
            dest = World_GetTeleporter(GETP(tp->dest)[RandomNumberGenerator_GetRandomInteger(0, tp->numDest)]);
            TransposeSubMatrix(&inv);
            XformVecOrientation(&inv, &neg, &rotated);
            inv.v[3].x = rotated.x;
            inv.v[3].y = rotated.y;
            inv.v[3].z = rotated.z;
            MultiplyMatrix(THING_MTX(thing), &inv, &rel);
            XformVecOrientation(&inv, &thing->vel, &vel);
            if (tp->damage != 0) {
                thing->health -= tp->damage;
                if (thing->health < 1) {
                    thing->health = 1;
                }
            }
            if (dest->flags & TELEPORTER_KEEP_ROTATION) {
                MultiplyMatrix(&rel, dest, THING_MTX(thing));
                SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
                XformVecOrientation(dest, &vel, &thing->vel);
                if (!(dest->flags & TELEPORTER_KEEP_OFFSET)) {
                    thing->pos.x = dest->mtx.v[3].x;
                    thing->pos.y = dest->mtx.v[3].y;
                    thing->pos.z = dest->mtx.v[3].z;
                }
            } else {
                *THING_MTX(thing) = dest->mtx;
                SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
                if (dest->flags & TELEPORTER_KEEP_OFFSET) {
                    XformVec(dest, &rel.v[3], &thing->pos);
                }
            }
            if (!(dest->flags & TELEPORTER_KEEP_SPEED)) {
                thing->vel.x = 0.0f;
                thing->vel.y = 0.0f;
                thing->vel.z = 0.0f;
            }
            if (dest->debris != 0) {
                Teleporter_SpawnDebris(dest);
            }
            if (tp->debris != 0) {
                Teleporter_SpawnDebris(tp);
            }
            if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
                Teleporter_ResetCarNav(thing);
            }
        }
        thing->flags |= THING_FLAG_IN_TELEPORTER;
        return 1;
    }
    return 0;
}

/* data: { n, groups of { count, thing handles... } }: any of them inside a
 * teleporter this frame. */
s32 TriggerP2PActivated(u32 *data) {
    u32 n = data[0];
    u32 i;
    u32 j;
    u32 k = n + 1;

    for (i = 0; i < n; i++) {
        for (j = 0; j < data[k]; j++) {
            if (((Thing *)PTR((data + 1)[j + k]))->flags & THING_FLAG_IN_TELEPORTER) {
                return 1;
            }
        }
    }
    return 0;
}

/* action: { _, teleporter data, count, subject handles... } moves each
 * subject's car onto the teleporter. */
s32 ActionVehicleTeleport(u32 *action) {
    Teleporter *data;
    u32 i;
    Thing *car;
    Player *player;
    Vector4 local __attribute__((aligned(16)));

    data = (Teleporter *)PTR(action[1]);
    for (i = 0; i < action[2]; i++) {
        car = SLOT_LOAD(Thing *, (action[i + 3] + 0x28));
        player = GETP(car->player);
        if (data->debris != 0) {
            Teleporter_SpawnDebris(data);
        }
        if (data->flags & TELEPORTER_KEEP_SPEED) {
            InverseXformVecOrientation(THING_MTX(car), &car->vel, &local);
            XformVecOrientation(data, &local, &car->vel);
        } else if (player != NULL && GETP(player->vehicle) != NULL && GETP(player->vehicle) == car) {
            player->combo = 5;
        }
        *THING_MTX(car) = data->mtx;
        SubMatrixToQuaternion(THING_MTX(car), &car->quat);
        THING_VFN(car, THING_V_MOVE, void (*)(void *))(car);
        car->flags |= THING_FLAG_IN_TELEPORTER;
        Teleporter_ResetCarNav(car);
    }
    return 1;
}

void Thing_GetInterfaceInfo(void *thing, void *info) {
    typedef struct InterfaceInfo {
        s32 unk0;
        s32 unk4;
        s32 unk8[2];
        s32 unk10;
        u32 speed;
        s32 unk18;
    } InterfaceInfo;
    InterfaceInfo *ii = info;

    ii->unk10 = ((Thing *)thing)->health;
    ii->speed = 0.681818f * MagnitudeVector(thing);
    ii->unk18 = 0;
    ii->unk4 = 0;
}

void Thing_SetSlavePlayers(Thing *thing) {
    Thing *slave;

    for (slave = GETP(thing->slaves); slave != NULL; slave = GETP(slave->next)) {
        slave->player = thing->player;
    }
}

void Thing_DrawSlaves(Thing *thing) {
    Thing *slave;
    Quaternion quat __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    for (slave = GETP(thing->slaves); slave != NULL; slave = GETP(slave->next)) {
        if (!(slave->flags & 0x200200)) {
            quat = slave->quat;
            pos = slave->pos;
            QuaternionMult(&thing->quat, &quat, &slave->quat);
            XformVec(THING_MTX(thing), &pos, &slave->pos);
            THING_VFN(slave, THING_V_DRAW, void (*)(Thing *))(slave);
            slave->quat = quat;
            slave->pos = pos;
        }
    }
}

void Thing_MoveSlaves(Thing *thing) {
    Thing *slave;
    Thing *next;
    Quaternion quat __attribute__((aligned(16)));
    Vector4 trans __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    for (slave = GETP(thing->slaves); slave != NULL && !(thing->flags & 0x201000); slave = next) {
        next = GETP(slave->next);
        if (!(slave->flags & 0x201000)) {
            if (!(slave->flags & 0x400000)) {
                CopyVector(&quat, &slave->quat);
                CopyVector(&trans, &slave->pos);
                CopyVector(&pos, &slave->vel);
                QuaternionMult(&thing->quat, &quat, &slave->quat);
                XformVec(THING_MTX(thing), &trans, &slave->pos);
                slave->vel = thing->vel;
                THING_VFN(slave, THING_V_MOVE, void (*)(Thing *))(slave);
                CopyVector(&slave->quat, &quat);
                CopyVector(&slave->pos, &trans);
                CopyVector(&slave->vel, &pos);
            } else {
                THING_VFN(slave, THING_V_MOVE, void (*)(Thing *))(slave);
            }
        }
    }
    ThingManagerList_RemoveReleasedThings((void **)&thing->slaves);
}

void Thing_InitAllSlavesNow(s32 arg0, void *data, s32 mask) {
    ThingManagerList_InitAllSlavesNow(&((Thing *)PTR(arg0))->slaves, data, mask);
}

void Thing_SlaveTransform(Thing *thing, Matrix4 *mtx) {
    Matrix4 local;

    local = *THING_MTX(thing);
    MultiplyMatrix(&local, mtx, THING_MTX(thing));
    SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
}

void Thing_SlaveTransformQuaternionOnly(Thing *thing, Matrix4 *mtx) {
    Quaternion rot __attribute__((aligned(16)));
    Quaternion quat __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    quat = thing->quat;
    pos = thing->pos;
    XformVec(mtx, &pos, &thing->pos);
    SubMatrixToQuaternion(mtx, &rot);
    QuaternionMult(&rot, &quat, &thing->quat);
}

void *Thing_GetSurfaceScript(void) {
    extern u8 g_defaultSurfaceScript[];
    return g_defaultSurfaceScript;
}

void Thing_GetInverseLocalInertiaTensor(void *thing, f32 *tensor) {
    tensor[0] = 0.0f;
    tensor[1] = 0.0f;
    tensor[2] = 0.0f;
}

s32 Thing_GetNearestNavPoint(void) {
    return -1;
}
void Thing_GetDynamicMinMax(Thing *thing, Vector4 *min, Vector4 *max, f32 expand);

s32 Thing_AddToGrid(void *thing, s32 type) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    Thing_GetDynamicMinMax(thing, &min, &max, 0.0f);
    /* port: the grid keeps the thing as a 32-bit id; the object pointer goes in a slot */
    SETP(((Thing *)thing)->gridObject, Grid_AddObject(type, HND(thing), &min, &max));
    return ((Thing *)thing)->gridObject.h;
}

/* The world-space bounds of `thing` over the next `expand` seconds of
 * movement, for the grid. */
void Thing_GetDynamicMinMax(Thing *thing, Vector4 *min, Vector4 *max, f32 expand) {
    CollisionOBox *box;
    u32 i;
    Vector4 p3 __attribute__((aligned(16)));
    Vector4 p2 __attribute__((aligned(16)));
    Vector4 p1 __attribute__((aligned(16)));

    min->x = min->y = min->z = min->w = 99999.9f;
    max->x = max->y = max->z = max->w = -99999.9f;
    switch (thing->collisionType) {
    case 0:
        *max = thing->pos;
        *min = *max;
        break;
    case 3:
        CopyVector(&p3, &thing->pos);
        CopyVector(min, &thing->pos);
        CopyVector(max, &thing->pos);
        p3.x += thing->vel.x * expand;
        p3.y += thing->vel.y * expand;
        p3.z += thing->vel.z * expand;
        MinMaxVec(min, max, &p3);
        break;
    case 2:
    case 4:
    case 5:
    case 6:
        CopyVector(&p2, &thing->pos);
        CopyVector(min, &thing->pos);
        CopyVector(max, &thing->pos);
        p2.x += thing->vel.x * expand;
        p2.y += thing->vel.y * expand;
        p2.z += thing->vel.z * expand;
        if (p2.x < min->x) {
            min->x = p2.x;
        }
        if (p2.y < min->y) {
            min->y = p2.y;
        }
        if (p2.z < min->z) {
            min->z = p2.z;
        }
        if (p2.x > max->x) {
            max->x = p2.x;
        }
        if (p2.y > max->y) {
            max->y = p2.y;
        }
        if (p2.z > max->z) {
            max->z = p2.z;
        }
        min->x -= thing->radius;
        min->y -= thing->radius;
        min->z -= thing->radius;
        max->x += thing->radius;
        max->y += thing->radius;
        max->z += thing->radius;
        break;
    case 1:
        box = THING_VFN(thing, THING_V_GET_ORIENTED_COLLISION, CollisionOBox *(*)(Thing *))(thing);
        if (thing->flags & 8) {
            /* the box already holds its bounds */
            CopyVector(min, &box->min);
            CopyVector(max, &box->max);
        } else {
            for (i = 0; i < 8; i++) {
                p1 = box->corners[i];
                MinMaxVec(min, max, &p1);
                p1.x += thing->vel.x * expand;
                p1.y += thing->vel.y * expand;
                p1.z += thing->vel.z * expand;
                MinMaxVec(min, max, &p1);
            }
        }
        break;
    }
}

/* The bounds of the bounding sphere: covers any rotation. */
void Thing_GetStaticRotationalMinMax(Thing *thing, Vector4 *min, Vector4 *max) {
    *max = thing->pos;
    *min = *max;
    min->x -= thing->radius;
    min->y -= thing->radius;
    min->z -= thing->radius;
    max->x += thing->radius;
    max->y += thing->radius;
    max->z += thing->radius;
}

void Thing_DynamicGridMove(Thing *thing, f32 expand) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    if (thing->gridObject.h != 0) {
        Thing_GetDynamicMinMax(thing, &min, &max, expand);
        Grid_MoveObject(GETP(thing->gridObject), &min, &max);
    }
}

void Thing_StaticGridMove(Thing *thing) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    if (thing->gridObject.h != 0) {
        Thing_GetDynamicMinMax(thing, &min, &max, 0.0f);
        Grid_MoveObject(GETP(thing->gridObject), &min, &max);
    }
}

void Thing_RotationalGridMove(Thing *thing) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    if (thing->gridObject.h != 0) {
        Thing_GetStaticRotationalMinMax(thing, &min, &max);
        Grid_MoveObject(GETP(thing->gridObject), &min, &max);
    }
}

/* Pick up the first item within reach (by the collector's kind), spawning
 * the item's pickup effect. */
void Thing_CollideWithItems(Thing *thing) {
    extern Vector4 g_zeroVector;
    struct GridResult {
        u32 data;
        u32 id;
    };
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    DebrisParams params __attribute__((aligned(16)));
    struct GridResult results[64];
    s32 count;
    s32 i;
    ItemThing *item;
    s8 picked;

    max = thing->pos;
    min = max;
    min.x -= thing->radius;
    min.y -= thing->radius;
    min.z -= thing->radius;
    max.x += thing->radius;
    max.y += thing->radius;
    max.z += thing->radius;
    count = Grid_GetObjects(&min, &max, 8, results, 0x40);
    for (i = 0; i < count; i++) {
        item = (ItemThing *)PTR(results[i].id);
        if (item->pickupDelay > 0) {
            continue;
        }
        if (PSVECSquareDistance(&thing->pos, &item->base.pos) < item->pickupRadiusSq) {
            picked = 0;
            switch (thing->type) {
            case 7:
                picked = Creature_ProcessItemPickup(thing, item);
                break;
            case 9:
                picked = CarA_ProcessItemPickup(thing, item);
                break;
            case 11:
                picked = Thing_ProcessItemPickup(thing, item);
                break;
            }
            if (picked && GETP(item->record)->pickupDebris != 0) {
                params.debris = GETP(item->record)->pickupDebris;
                params.pos = thing->pos;
                params.vel.z = 0.0f;
                params.vel.y = 0.0f;
                params.vel.x = 0.0f;
                params.quat = *(Vector4 *)&thing->quat;
                params.unk48 = 0.0f;
                params.unk4C = 0.0f;
                params.owner = 0;
                params.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &params);
            }
            return;
        }
    }
}

/* A thing's end (destroyed, expired): spawn one of the event's debris
 * effects at random, credited to whoever caused it. `event` is the thing's
 * physical properties (DestructibleInfo: 0x34 count, 0x38 debris list). */
void Thing_ProcessEndEvent(Thing *thing, u8 *event, CollisionInfo *collision, Vector4 *angVel) {
    extern Vector4 g_zeroVector;
    DebrisParams params __attribute__((aligned(16)));
    void *data;
    Thing *other;

    data = NULL;
    if (event != NULL && *(u32 *)(event + 0x34) != 0) {
        data = SLOT_LOAD(void *, (u32 *)SLOT_LOAD(void *, (event + 0x38)) + (RandomAdd_s32(0, *(u32 *)(event + 0x34))));
    }
    if (data != NULL) {
        params.debris = HND(data);
        params.pos = thing->pos;
        params.quat = *(Vector4 *)&thing->quat;
        params.vel = thing->vel;
        params.owner = thing->player.h;
        params.unk48 = 0.0f;
        params.unk4C = 0.0f;
        if (angVel != NULL) {
            params.angVel = *angVel;
        } else {
            params.angVel = g_zeroVector;
        }
        if (collision != NULL) {
            other = (Thing *)PTR(collision->thingA);
            if (other != NULL && other != thing) {
                params.owner = other->player.h;
            } else {
                other = (Thing *)PTR(collision->thingB);
                if (other != NULL && other != thing) {
                    params.owner = other->player.h;
                }
            }
        }
        ThingManager_DARYL_CreateThing(1, &params);
    }
}

/* No collision: the original is an empty function, which leaves its maxDist
 * argument in f1 as the result ("no hit") for RayHitsObjects_Aux. */
f32 Thing_CollideWithRay(void *thing, Vector4 *start, Vector4 *dir, void *hit, f32 maxDist) {
    (void)thing;
    (void)start;
    (void)dir;
    (void)hit;
    return maxDist;
}

void Thing_GetTargetPos(Thing *thing, Quaternion *pos) {
    *pos = *(Quaternion *)&thing->pos;
}

/* No collision box (vtable 0x40): callers read a pointer. */
void *Thing_GetOrientedCollision(void *thing) {
    (void)thing;
    return NULL;
}

/* Sweep the thing against the terrain along its velocity (arg1: time, arg2:
 * CollisionInfo out). */
s32 Thing_HitsTerrain(Thing *thing, void *arg1, void *arg2) {
    if (GETP(g_worldData.world) == NULL) {
        return 0;
    }
    switch (thing->collisionType) {
    case THING_COLLIDE_RAY:
        return RayHitsTerrain_Aux3(&thing->pos, &thing->vel, arg1, arg2);
    case THING_COLLIDE_OBOX:
        return OBoxHitsTerrain(THING_VFN(thing, THING_V_GET_ORIENTED_COLLISION, void *(*)(void *))(thing), &thing->vel, arg1, arg2);
    }
    return 0;
}

void Thing_PassiveTargeting(void *thing)
{
}

/* The party of the contact that isn't `self` (Thing handles). */
u32 CollisionInfo_GetOther(CollisionInfo *info, u32 self) {
    u32 a;

    a = info->thingA;
    if (a == self) {
        return info->thingB;
    }
    return a;
}

