/* Item things: pickups lying in the world. They fall and bounce until they
 * come to rest, spin in place, and are collected by Thing_CollideWithItems. */
#include "common.h"
#include "World.h"
#include "CarA.h"


s32 Item_Init(ItemThing *item, ItemParams *data) {
    Vector4 euler __attribute__((aligned(16)));
    Vector4 quat __attribute__((aligned(16)));
    ItemRecord *record = GETP(data->record);
    WeaponInfo *weapon;

    Thing_Init(&item->base, (s32)HND(data));
    item->navPoint = -1;
    SETP(item->spawner, NULL);
    item->base.collisionType = 2;
    item->base.radius = record->radius;
    item->pickupRadiusSq = item->base.radius * item->base.radius;
    item->base.pos = data->pos;
    item->base.pos.w = 1.0f;
    item->base.vel = data->vel;
    item->base.quat = *(Quaternion *)&data->quat;
    item->unkEC = 0;
    QuaternionToSubMatrix(&item->base.quat, THING_MTX(&item->base));
    item->drawDist = data->drawDist;
    item->unkE0 = data->unk38;
    if (item->drawDist == 0.0f) {
        item->drawDist = 500.0f;
    }
    item->timerActive = 0;
    item->base.flags |= 0x1000000;
    SETP(item->record, record);
    weapon = GETP(GETP(item->record)->weapon);
    if (weapon != NULL) {
        item->ammo = weapon->ammo;
    }
    /* a random spin around y */
    euler.x = 0.0f;
    euler.y = 0.017453292f * RandomAdd(record->spinMin, record->spinMax);
    euler.z = 0.0f;
    GenerateQuaternionXYZ(&euler, &quat);
    GenerateSpinAngleFromQuaternion(&quat, &item->spinAxis, &item->spinSpeed);
    item->pickupDelay = 1000;
    Thing_InitAllSlavesNow(HND(item), GETP(item->record)->sharedSlaves, 0xFF);
    item->base.flags |= THING_FLAG_AT_REST;
    item->navPoint = NavHolder_FindIndexOfNearestNavPoint(&item->base.pos, -1);
    Thing_AddToGrid(item, 3);
    return 1;
}

void Item_Release(void *arg) {
    extern u32 g_itemVTable[];
    ItemThing *item = arg;
    QuadItem *slot;

    if (GETP(item->base.vtable) == g_itemVTable) {
        /* detach from its spawn entry */
        slot = GETP(item->base.trigger);
        if (slot != NULL && GETP(slot->thing) != NULL) {
            SETP(item->base.trigger, NULL);
            SETP(slot->thing, NULL);
        }
    }
    Thing_Release(item);
}

s32 Item_PreMove(void) {
    return 1;
}

s8 Item_Move(ItemThing *item) {
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    extern Vector4 g_zeroVector;
    extern u8 *g_pNoRayCollideThing;
    CollisionInfo hit __attribute__((aligned(16)));
    Quaternion q;
    Quaternion rot;
    f32 dt;
    u8 hitSomething;
    s8 hitTerrain;
    f32 d;
    Thing *thing = &item->base;
    Player *carrier = GETP(thing->player);
    Vector4 *vel = &thing->vel;

    if (!(thing->flags & THING_FLAG_AT_REST)) {
        /* fall and bounce off whatever is hit, until resting on the ground */
        hitSomething = 0;
        dt = g_fDeltaTimeSec;
        vel->y += -130.0f * dt;
        hitTerrain = VectorHitsTerrain(&thing->pos);
        if (hitTerrain) {
            *vel = g_zeroVector;
            thing->flags |= 1;
            thing->flags |= THING_FLAG_AT_REST;
            return hitTerrain;
        }
        hit.thingA = hit.thingB = 0;
        if (carrier != NULL) {
            g_pNoRayCollideThing = GETP(carrier->vehicle);
        }
        if (RayHitsTerrainAndObjects(&thing->pos, item, &dt, &hit)) {
            f32 push = 0.001f;
            hitSomething = 1;
            d = 2.0f * PSVECDotProduct(vel, &hit.normal);
            vel->x -= d * hit.normal.x;
            vel->y -= d * hit.normal.y;
            vel->z -= d * hit.normal.z;
            thing->pos.x += hit.normal.x * push;
            thing->pos.y += hit.normal.y * push;
            thing->pos.z += hit.normal.z * push;
        }
        g_pNoRayCollideThing = NULL;
        if (hitSomething) {
            vel->x *= 0.5f;
            vel->y *= 0.5f;
            vel->z *= 0.5f;
            if (hit.normal.y > 0.7f && vel->x * vel->x + vel->y * vel->y + vel->z * vel->z < 1.0f) {
                vel->x = vel->y = vel->z = 0.0f;
                thing->flags |= 1;
                thing->flags |= THING_FLAG_AT_REST;
            }
        }
        AccumulateVector(vel, &thing->pos, &thing->pos, dt);
    }
    if (item->pickupDelay > 0) {
        item->pickupDelay -= (u16)g_iDeltaTimeMS;
        if (item->pickupDelay < 0) {
            SETP(thing->player, NULL);
        }
    }
    if (item->timerActive != 0) {
        item->timer -= g_iDeltaTimeMS;
        if (item->timer < 0) {
            item->timerActive = 0;
        }
    }
    if (item->spinSpeed) {
        GenerateQuaternion(&item->spinAxis, &q, 10.0f * (item->spinSpeed * g_fDeltaTimeSec));
        QuaternionMult(&thing->quat, &q, &rot);
        thing->quat = rot;
    }
    carrier = GETP(thing->player);
    if (GETP(item->record)->type == ITEM_TYPE_CARRIED && carrier != NULL && GETP(carrier->vehicle) != NULL) {
        Thing *obj = GETP(carrier->vehicle);
        item->navPoint = THING_VFN(obj, THING_V_GET_NEAREST_NAV_POINT, s32 (*)(void *))(obj);
    }
    return 1;
}

void Item_Draw(ItemThing *item) {
    if (item->drawDist) {
        if (Gfx_GetMinCameraDistSquared(&item->base.pos) > item->drawDist * item->drawDist) {
            return;
        }
    }
    if (item->base.flags & THING_FLAG_AT_REST) {
        QuaternionToSubMatrix(&item->base.quat, THING_MTX(&item->base));
    }
    Thing_DrawSharedSlaves(item, GETP(item->record)->sharedSlaves);
}

s32 Item_GetNearestNavPoint(ItemThing *item) {
    return item->navPoint;
}

/* Whether the item's record lies in [start, end) (a data pack being unloaded). */
s32 Item_Invalidate(ItemThing *item, u32 start, u32 end) {
    u32 record = item->record.h;

    if ((record >= start) && (record < end)) {
        return 1;
    }
    return 0;
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_itemVTable[17];

__attribute__((constructor)) static void PortInit_g_itemVTable(void) {
    static void *const fns[] = {
        (void *)Item_Init,
        (void *)Item_Release,
        (void *)Item_PreMove,
        (void *)Item_Move,
        (void *)Item_Draw,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransform,
        (void *)Thing_GetSurfaceScript,
        (void *)Item_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Thing_DynamicRotation,
        (void *)Thing_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Thing_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_itemVTable[i] = HND(fns[i]);
    }
}
