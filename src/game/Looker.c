/* The looker: a free-flying debug camera thing, steered with the pads; it can
 * attach to a host thing and look through that thing's camera. */
#include "common.h"
#include "World.h"
#include "CarA.h"

typedef struct Looker {
    Thing base;                 /* 0x00 */
    u8 padAC[8];                /* 0xAC */
    f32 yaw;                    /* 0xB4 degrees */
    f32 pitch;                  /* 0xB8 degrees, within +-80 */
    PTR32(Thing) host;          /* 0xBC thing whose camera it uses, or NULL */
    u8 padC0[4];                /* 0xC0 */
    s32 unkC4;                  /* 0xC4 */
    s32 unkC8;                  /* 0xC8 */
    f32 fov;                    /* 0xCC */
} Looker;

_Static_assert(offsetof(Looker, yaw) == 0xB4, "Looker.yaw");
_Static_assert(offsetof(Looker, fov) == 0xCC, "Looker.fov");
_Static_assert(sizeof(Looker) == 0xD0, "Looker size (ThingPools.looker)");

s32 Looker_Init(Looker *looker, u8 *init) {
    Vector4 params __attribute__((aligned(16)));
    Thing *slave;
    World *world;

    Thing_Init(&looker->base, 0);
    looker->base.flags |= THING_FLAG_DEAD;
    looker->yaw = 0.0f;
    looker->pitch = 0.0f;
    /* its wildcard rack */
    params.x = 0.0f;
    params.y = 2.0f;
    params.z = 1.0f;
    slave = ThingManager_DARYL_CreateSlave(THING_TYPE_WILDCARD_RACK, &params, 0xFF);
    if (slave != NULL) {
        slave->slaveTag = 0;
        ThingManagerList_Append(&looker->base.slaves, slave);
    }
    /* start at the world's first start point, if it has one */
    world = GETP(g_worldData.world);
    if (world->numTileCars != 0) {
        looker->base.pos = GETP(world->tileCars)[0].pos;
    }
    looker->base.collisionType = THING_COLLIDE_RAY;
    SETP(looker->host, NULL);
    looker->fov = 60.0f;
    looker->unkC4 = -1;
    looker->unkC8 = -1;
    return 1;
}

/* Fly by the player's input: left stick looks, second stick and the four
 * direction buttons move (button 0x1A slows down, 0x10 locks the buttons);
 * combo 7 toggles attaching to the thing it looks at. */
s32 Looker_PreMove(Looker *looker) {
    extern f32 g_fDeltaTimeSec;
    Thing *thing = &looker->base;
    Player *player = GETP(thing->player);
    f32 speed;
    f32 f;
    f32 t;
    f32 step;
    u32 i;
    s32 x;

    speed = 11000.0f * g_fDeltaTimeSec;
    if (player == NULL) {
        return 0;
    }
    if (!Player_IsHuman(player)) {
        return 0;
    }
    if (player->button1A) {
        speed *= 0.1f;
    }
    switch (player->combo) {
    case 7:
        if (GETP(looker->host) != NULL) {
            SETP(looker->host, NULL);
        } else {
            SETP(looker->host, FindHost(&thing->pos, &thing->axis[2]));
            if (GETP(looker->host) == thing) {
                SETP(looker->host, NULL);
            }
        }
        break;
    }
    player->combo = 0;
    thing->vel.x = 0.0f;
    thing->vel.y = 0.0f;
    thing->vel.z = 0.0f;
    x = player->stick2X;
    if (x > 32 || x < -32) {
        f = 120.0f * (f32)-x * g_fDeltaTimeSec;
        thing->vel.x += f * thing->axis[0].x;
        thing->vel.y += f * thing->axis[0].y;
        thing->vel.z += f * thing->axis[0].z;
    }
    if (player->stick2Y > 32 || player->stick2Y < -32) {
        f = 120.0f * (f32)player->stick2Y * g_fDeltaTimeSec;
        thing->vel.x += f * thing->axis[2].x;
        thing->vel.y += f * thing->axis[2].y;
        thing->vel.z += f * thing->axis[2].z;
    }
    if (player->stickX > 32 || player->stickX < -32) {
        looker->yaw -= 0.015f * (f32)player->stickX * g_fDeltaTimeSec;
    }
    if (player->stickY > 32 || player->stickY < -32) {
        looker->pitch -= 0.015f * (f32)player->stickY * g_fDeltaTimeSec;
        if (looker->pitch > 80.0f) {
            looker->pitch = 80.0f;
        }
        if (looker->pitch < -80.0f) {
            looker->pitch = -80.0f;
        }
    }
    SetOrientation(THING_MTX(thing), looker->yaw, looker->pitch, 0.0f);
    SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
    /* buttons: 0x14 forward, 0x12 back, 0x15 right, 0x13 left */
    if (!player->button10 && player->button14) {
        thing->vel.x += speed * thing->axis[2].x;
        thing->vel.y += speed * thing->axis[2].y;
        thing->vel.z += speed * thing->axis[2].z;
    }
    if (!player->button10 && player->throttle) {
        thing->vel.x -= speed * thing->axis[2].x;
        thing->vel.y -= speed * thing->axis[2].y;
        thing->vel.z -= speed * thing->axis[2].z;
    }
    if (!player->button10 && player->button15) {
        thing->vel.x += speed * thing->axis[0].x;
        thing->vel.y += speed * thing->axis[0].y;
        thing->vel.z += speed * thing->axis[0].z;
    }
    if (!player->button10 && player->brake) {
        thing->vel.x -= speed * thing->axis[0].x;
        thing->vel.y -= speed * thing->axis[0].y;
        thing->vel.z -= speed * thing->axis[0].z;
    }
    /* move in up to 8 steps (the loop always finishes in one) */
    t = 0.0f;
    i = 0;
    do {
        step = g_fDeltaTimeSec - t;
        thing->pos.x += thing->vel.x * step;
        thing->pos.y += thing->vel.y * step;
        thing->pos.z += thing->vel.z * step;
        t += step;
        if (i++ > 6) {
            t = g_fDeltaTimeSec;
        }
    } while (t < g_fDeltaTimeSec);
    Thing_CollideWithItems(thing);
    return 1;
}

void Looker_Draw(void)
{
}

/* Attached: take the host's transform and velocity and use its camera.
 * Otherwise look from the looker's own transform. */
void Looker_GetCameraOrientationInfo(Looker *looker, void *arg1, CameraInfo *info) {
    Thing *host = GETP(looker->host);

    if (host != NULL) {
        *THING_MTX(&looker->base) = *THING_MTX(host);
        looker->base.vel = host->vel;
        THING_VFN(host, THING_V_GET_CAMERA_ORIENTATION, void (*)(Thing *, void *, CameraInfo *))(host, arg1, info);
        return;
    }
    info->fov = looker->fov;
    info->unk80 = 0;
    SETP(info->subMode, NULL);
    info->mtx = *THING_MTX(&looker->base);
    info->target = looker->base.pos;
    info->from = looker->base.pos;
}


/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_lookerVTable[17];

__attribute__((constructor)) static void PortInit_g_lookerVTable(void) {
    static void *const fns[] = {
        (void *)Looker_Init,
        (void *)Thing_Release,
        (void *)Looker_PreMove,
        (void *)Thing_Move,
        (void *)Looker_Draw,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Looker_GetCameraOrientationInfo,
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
        g_lookerVTable[i] = HND(fns[i]);
    }
}

