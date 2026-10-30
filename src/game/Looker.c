#include "common.h"


s32 Looker_Init(u8 *looker, u8 *init) {
    typedef struct { f32 x, y, z, w; } Vec4;
    Vector4 params __attribute__((aligned(16)));
    u8 *slave;
    void *world;

    Thing_Init(looker, 0);
    M2C_FIELD(looker, u32 *, 0x74) |= 2;
    M2C_FIELD(looker, f32 *, 0xB4) = 0.0f;
    M2C_FIELD(looker, f32 *, 0xB8) = 0.0f;
    params.x = 0.0f;
    params.y = 2.0f;
    params.z = 1.0f;
    slave = ThingManager_DARYL_CreateSlave(8, &params, 0xFF);
    if (slave != NULL) {
        slave[0x83] = 0;
        ThingManagerList_Append((void **)(looker + 0x88), slave);
    }
    world = GETP(g_worldData.world);
    if (M2C_FIELD(world, u32 *, 0x8) != 0) {
        M2C_FIELD(looker, Vec4 *, 0x40) = *SLOT_LOAD(Vec4 *, (u8 *)(world) + 0xC);
    }
    M2C_FIELD(looker, s16 *, 0x80) = 3;
    M2C_FIELD(looker, s32 *, 0xBC) = 0;
    M2C_FIELD(looker, f32 *, 0xCC) = 60.0f;
    M2C_FIELD(looker, s32 *, 0xC4) = -1;
    M2C_FIELD(looker, s32 *, 0xC8) = -1;
    return 1;
}

s32 Looker_PreMove(u8 *thing) {
    typedef struct {
        s8 lookX;
        s8 lookY;
        s8 moveX;
        s8 moveY;
        u8 unk4[0xC];
        u8 locked;
        u8 unk11;
        u8 back;
        u8 left;
        u8 forward;
        u8 right;
        u8 unk16[4];
        u8 fast;
        u8 unk1B[0x17];
        u8 pressed;
    } LookerInput;
    typedef struct Looker {
        Vector4 vel;
        Vector4 right;
        Vector4 up;
        Vector4 forward;
        Vector4 pos;
        Vector4 quat;
        u8 unk60[0x34];
        PTR32(LookerInput) player;
        u8 unk98[0x1C];
        f32 yaw;
        f32 pitch;
        PTR32(struct Looker) host;
    } Looker;
    extern f32 g_fDeltaTimeSec;
    Looker *looker = (Looker *)thing;
    f32 speed;
    f32 f;
    f32 t;
    f32 step;
    u32 i;
    s32 x;

    speed = 11000.0f * g_fDeltaTimeSec;
    if (GETP(looker->player) == NULL) {
        return 0;
    }
    if (!Player_IsHuman(GETP(looker->player))) {
        return 0;
    }
    if (GETP(looker->player)->fast) {
        speed *= 0.1f;
    }
    switch (GETP(looker->player)->pressed) {
    case 7:
        if (GETP(looker->host) != NULL) {
            SETP(looker->host, NULL);
        } else {
            SETP(looker->host, FindHost(&looker->pos, &looker->forward));
            if (GETP(looker->host) == looker) {
                SETP(looker->host, NULL);
            }
        }
        break;
    }
    GETP(looker->player)->pressed = 0;
    looker->vel.x = 0.0f;
    looker->vel.y = 0.0f;
    looker->vel.z = 0.0f;
    x = GETP(looker->player)->moveX;
    if (x > 32 || x < -32) {
        f = 120.0f * (f32)-x * g_fDeltaTimeSec;
        looker->vel.x += f * looker->right.x;
        looker->vel.y += f * looker->right.y;
        looker->vel.z += f * looker->right.z;
    }
    if (GETP(looker->player)->moveY > 32 || GETP(looker->player)->moveY < -32) {
        f = 120.0f * (f32)GETP(looker->player)->moveY * g_fDeltaTimeSec;
        looker->vel.x += f * looker->forward.x;
        looker->vel.y += f * looker->forward.y;
        looker->vel.z += f * looker->forward.z;
    }
    if (GETP(looker->player)->lookX > 32 || GETP(looker->player)->lookX < -32) {
        looker->yaw -= 0.015f * (f32)GETP(looker->player)->lookX * g_fDeltaTimeSec;
    }
    if (GETP(looker->player)->lookY > 32 || GETP(looker->player)->lookY < -32) {
        looker->pitch -= 0.015f * (f32)GETP(looker->player)->lookY * g_fDeltaTimeSec;
        if (looker->pitch > 80.0f) {
            looker->pitch = 80.0f;
        }
        if (looker->pitch < -80.0f) {
            looker->pitch = -80.0f;
        }
    }
    SetOrientation(&looker->right, looker->yaw, looker->pitch, 0.0f);
    SubMatrixToQuaternion(&looker->right, &looker->quat);
    if (!GETP(looker->player)->locked && GETP(looker->player)->forward) {
        looker->vel.x += speed * looker->forward.x;
        looker->vel.y += speed * looker->forward.y;
        looker->vel.z += speed * looker->forward.z;
    }
    if (!GETP(looker->player)->locked && GETP(looker->player)->back) {
        looker->vel.x -= speed * looker->forward.x;
        looker->vel.y -= speed * looker->forward.y;
        looker->vel.z -= speed * looker->forward.z;
    }
    if (!GETP(looker->player)->locked && GETP(looker->player)->right) {
        looker->vel.x += speed * looker->right.x;
        looker->vel.y += speed * looker->right.y;
        looker->vel.z += speed * looker->right.z;
    }
    if (!GETP(looker->player)->locked && GETP(looker->player)->left) {
        looker->vel.x -= speed * looker->right.x;
        looker->vel.y -= speed * looker->right.y;
        looker->vel.z -= speed * looker->right.z;
    }
    t = 0.0f;
    i = 0;
    do {
        step = g_fDeltaTimeSec - t;
        looker->pos.x += looker->vel.x * step;
        looker->pos.y += looker->vel.y * step;
        looker->pos.z += looker->vel.z * step;
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

void Looker_GetCameraOrientationInfo(u8 *looker, void *arg1, u8 *info) {
    typedef struct LookerCamera {
        Vector4 pos;
        Matrix4 orient;
        u8 unk50[0x3C];
        struct { u8 unk0[0x1C]; void (*getOrientationInfo)(struct LookerCamera *, void *, u8 *); } *vtable;
    } LookerCamera;
    typedef struct {
        Vector4 pos;
        Matrix4 orient;
        u8 unk50[0x6C];
        PTR32(LookerCamera) camera;
        u8 unkC0[0xC];
        f32 fov;
    } Looker;
    typedef struct {
        u8 unk0[0x14];
        f32 fov;
        u8 unk18[0x8];
        Matrix4 orient;
        Vector4 eye;
        Vector4 target;
        s32 unk80;
        s32 unk84;
    } OrientationInfo;
    Looker *l = (Looker *)looker;
    OrientationInfo *o = (OrientationInfo *)info;
    LookerCamera *cam = GETP(l->camera);

    if (cam != NULL) {
        l->orient = cam->orient;
        l->pos = GETP(l->camera)->pos;
        GETP(l->camera)->vtable->getOrientationInfo(GETP(l->camera), arg1, info);
        return;
    }
    o->fov = l->fov;
    o->unk80 = 0;
    o->unk84 = 0;
    o->orient = l->orient;
    o->eye = l->orient.v[3];
    o->target = l->orient.v[3];
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

