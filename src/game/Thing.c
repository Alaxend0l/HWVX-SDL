#include "common.h"



/* The transform every Thing starts with. */
typedef struct Thing {
    Vector4 unk0;      /* 0x00 */
    Matrix4 mtx;       /* 0x10 */
    Quaternion quat;   /* 0x50 */
    Vector4 unk60;     /* 0x60 */
    f32 unk70;         /* 0x70 */
    u32 flags;         /* 0x74 */
    u8 pad78[8];
    s16 unk80;         /* 0x80 */
    u8 unk82;          /* 0x82 */
    u8 unk83;          /* 0x83 */
    s32 unk84;         /* 0x84 */
    s32 unk88;         /* 0x88 */
    PTR32(void *) vtable;     /* 0x8C */
    u8 pad90[4];
    s32 unk94;         /* 0x94 */
} Thing;


static inline s8 Thing_ProcessItemPickup(u8 *thing, u8 *item) {
    void *rack;
    void *motion;

    switch (*SLOT_LOAD(u8 *, (item + 0xC0))) {
    case 0:
    case 1:
    case 2:
        break;
    case 3:
        rack = ThingManagerList_GetByType(thing + 0x88, 8);
        if (rack == NULL) {
            break;
        }
        WildCardRack_Add(rack, SLOT_LOAD(u8 *, (item + 0xC0)), *(s16 *)(item + 0xCE));
        Item_Release(item);
        motion = SLOT_LOAD(void *, (item + 0xD0));
        if (motion != NULL) {
            *(s32 *)((u8 *)motion + 0x18) = *(s32 *)((u8 *)motion + 0x14);
        }
        return 1;
    }
    return 0;
}

s32 Thing_Init(Thing *thing, s32 arg1) {
    extern Matrix4 g_identity;

    thing->unk0.z = 0.0f;
    thing->unk0.y = 0.0f;
    thing->unk0.x = 0.0f;
    thing->unk60.z = 0.0f;
    thing->unk60.y = 0.0f;
    thing->unk60.x = 0.0f;
    thing->unk70 = 0.0f;
    thing->quat.w = 1.0f;
    thing->quat.x = 0.0f;
    thing->quat.y = 0.0f;
    thing->quat.z = 0.0f;
    thing->flags = 0;
    thing->unk80 = 0;
    thing->mtx = g_identity;
    thing->unk94 = 0;
    thing->unk82 = 0xFF;
    thing->unk83 = 0xFF;
    thing->unk88 = 0;
    thing->unk84 = 0;
    return 1;
}

void Thing_Release(s32 thing) {
    M2C_UNK Grid_FreeObject(void *);
    void *player;

    M2C_FIELD(thing, u32 *, 0x74) |= 0x200;
    M2C_FIELD(thing, u32 *, 0x74) |= 2;
    M2C_FIELD(thing, u32 *, 0x74) |= 0x200000;
    if (SLOT_LOAD(void *, (u8 *)(thing) + 0x84) != NULL) {
        Grid_FreeObject(SLOT_LOAD(void *, (u8 *)(thing) + 0x84));
    }
    SLOT_STORE((u8 *)(thing) + 0x84, NULL);
    ThingManagerList_EmancipateSlaves(PTR(thing + 0x10), PTR(thing + 0x88));
    ThingManagerList_ReleaseAll(PTR(thing + 0x88));
    ThingManagerList_RemoveReleasedThings(PTR(thing + 0x88));
    player = SLOT_LOAD(void *, (u8 *)(thing) + 0x94);
    if (player != NULL && SLOT_LOAD(void *, (u8 *)(player) + 0x2C) == (void *)PTR(thing)) {
        Player_DetachThingDeath(player);
    }
}

s32 Thing_GetLocalCollision(u8 *thing) {
    extern u8 g_destructibleVTable[];
    extern u8 g_moverVTable[];
    extern u8 g_physicalObjectVTable[];
    extern u8 g_itemVTable[];
    extern u8 g_droneVTable[];
    u8 *vtable = SLOT_LOAD(u8 *, (u8 *)(thing) + 0x8C);
    u8 *physics;

    if (vtable == g_carAVTable) {
        physics = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(thing) + 0x3BC)) + 0xA0);
    } else if (vtable == g_destructibleVTable || vtable == g_moverVTable || vtable == g_physicalObjectVTable) {
        physics = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(thing) + 0xC0)) + 0x10);
    } else if (vtable == g_itemVTable) {
        physics = NULL;
    } else if (vtable == g_droneVTable) {
        physics = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(thing) + 0xC0)) + 0x10);
    } else {
        physics = NULL;
    }
    switch (physics[0x10]) {
    case 0:
        return M2C_FIELD(physics, s32 *, 0x44);
    case 1:
        return M2C_FIELD(physics, s32 *, 0x48);
    case 7:
        return M2C_FIELD(physics, s32 *, 0x44);
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

s32 Thing_GetSource(void *arg0) {
    void *temp_r3;

    temp_r3 = SLOT_LOAD(void *, (u8 *)(arg0) + 0x94);
    if (temp_r3 != NULL) {
        return M2C_FIELD(temp_r3, s32 *, 0x2C);
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
    return ThingManagerList_Get(&thing->unk88, target);
}

s32 TriggerAirboxEntered(u32 *trigger) {
    f32 dist;
    u8 *volume;
    u8 *thing;
    u32 i;
    u32 numThings;
    u32 j;

    numThings = trigger[0];
    for (i = 0; i < numThings; i++) {
        thing = (u8 *)PTR(trigger[i + 1]);
        if (M2C_FIELD(thing, u32 *, 4) == 1) {
            for (j = 0; j < trigger[numThings + 1]; j++) {
                volume = SLOT_LOAD(u8 *, (u8 *)(trigger[numThings + 2 + j]) + 0x28);
                if (volume != NULL && IsPointWithinVolume((Vector4 *)(volume + 0x40), thing, &dist)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 TriggerCreatureVolumeEntered(u32 *trigger) {
    f32 dist;
    u8 *volume;
    u8 *thing;
    u32 i;
    u32 numThings;
    u32 j;

    numThings = trigger[0];
    for (i = 0; i < numThings; i++) {
        thing = (u8 *)PTR(trigger[i + 1]);
        if (M2C_FIELD(thing, u32 *, 4) == 1) {
            for (j = 0; j < trigger[numThings + 1]; j++) {
                volume = SLOT_LOAD(u8 *, (u8 *)(trigger[numThings + 2 + j]) + 0x1C);
                if (volume != NULL && IsPointWithinVolume((Vector4 *)(volume + 0x40), thing, &dist)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 TriggerItemVolumeEntered(u32 *trigger) {
    f32 dist;
    u8 *thing;
    u32 i;
    u32 numThings;
    u32 j;
    u8 *volume;

    numThings = trigger[0];
    for (i = 0; i < numThings; i++) {
        thing = (u8 *)PTR(trigger[i + 1]);
        for (j = 0; j < trigger[numThings + 1]; j++) {
            volume = SLOT_LOAD(u8 *, (u8 *)(trigger[numThings + 2 + j]) + 0x24);
            if (volume != NULL) {
                if (SLOT_LOAD(u8 *, (u8 *)(volume) + 0x94) != NULL && SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(volume) + 0x94)) + 0x2C) != NULL) {
                    volume = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(volume) + 0x94)) + 0x2C);
                }
                if (M2C_FIELD(thing, u32 *, 4) == 1 && IsPointWithinVolume((Vector4 *)(volume + 0x40), thing, &dist)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 TriggerMovableVolumeEntered(u32 *trigger) {
    f32 dist;
    u8 *thing;
    u32 i;
    u32 numThings;
    u32 j;
    u8 *volume;

    numThings = trigger[0];
    for (i = 0; i < numThings; i++) {
        thing = (u8 *)PTR(trigger[i + 1]);
        for (j = 0; j < trigger[numThings + 1]; j++) {
            volume = SLOT_LOAD(u8 *, (u8 *)(trigger[numThings + 2 + j]) + 0x10);
            if (volume != NULL && M2C_FIELD(thing, u32 *, 4) == 1 && IsPointWithinVolume((Vector4 *)(volume + 0x40), thing, &dist)) {
                return 1;
            }
        }
    }
    return 0;
}

s32 TriggerAirboxStopIn(u32 *trigger) {
    f32 dist;
    u8 *volume;
    u8 *thing;
    u32 i;
    u32 numThings;
    u32 j;

    numThings = trigger[0];
    for (i = 0; i < numThings; i++) {
        thing = (u8 *)PTR(trigger[i + 1]);
        for (j = 0; j < trigger[numThings + 1]; j++) {
            volume = SLOT_LOAD(u8 *, (u8 *)(trigger[numThings + 2 + j]) + 0x28);
            if (M2C_FIELD(thing, u32 *, 4) == 1 && PSVECSquareMag((Vector4 *)volume) < 50.0f && IsPointWithinVolume((Vector4 *)(volume + 0x40), thing, &dist)) {
                return 1;
            }
        }
    }
    return 0;
}

s32 TriggerAirboxExited(u32 *trigger) {
    f32 dist;
    u8 *volume;
    u8 *thing;
    u32 i;
    u32 numThings;
    u32 j;

    numThings = trigger[0];
    for (i = 0; i < numThings; i++) {
        thing = (u8 *)PTR(trigger[i + 1]);
        for (j = 0; j < trigger[numThings + 1]; j++) {
            volume = SLOT_LOAD(u8 *, (u8 *)(trigger[numThings + 2 + j]) + 0x28);
            if (M2C_FIELD(thing, u32 *, 4) == 1 && IsPointWithinVolume((Vector4 *)(volume + 0x40), thing, &dist)) {
                return 0;
            }
        }
    }
    return 1;
}

s32 ActionAirboxChangeMode(s32 *data) {
    u32 count = data[1];
    s32 mode = data[0];
    u32 i;

    for (i = 0; i < count; i++) {
        u8 *airbox = (u8 *)PTR(data[i + 2]);
        switch (mode) {
        case 0:
            *(u32 *)(airbox + 4) = 0;
            break;
        case 2:
            if (*(u32 *)(airbox + 4) == 1) {
                *(u32 *)(airbox + 4) = 0;
            } else {
                *(u32 *)(airbox + 4) = 1;
            }
            break;
        case 1:
        default:
            *(u32 *)(airbox + 4) = 1;
            break;
        }
    }
    return 1;
}

void Thing_CollideWithForceBoxes(u8 *thing) {
    typedef struct ForceBox {
        Vector4 force;
        s8 oneShot;
        s8 carOnly;
        u16 type;
    } ForceBox;
    extern u8 g_carAVTable[];
    u8 *IsPointWithinForceBox(Vector4 *point);
    f32 MagnitudeVector(void *v);
    Vector4 *vel = (Vector4 *)thing;
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

    wasInside = *(u32 *)(thing + 0x74) & 0x400;
    volume = IsPointWithinForceBox((Vector4 *)(thing + 0x40));
    *(u32 *)(thing + 0x74) &= ~0x400;
    if (volume == NULL) {
        return;
    }
    box = SLOT_LOAD(ForceBox *, (volume + 0x2C));
    if (box->carOnly && SLOT_LOAD(u8 *, (thing + 0x8C)) == g_carAVTable && *(s32 *)(thing + 0x460) != 0) {
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
        *(u32 *)(thing + 0x74) |= 0x400;
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

s32 TriggerP2PActivated(u32 *data) {
    u32 n = data[0];
    u32 i;
    u32 j;
    u32 k = n + 1;

    for (i = 0; i < n; i++) {
        for (j = 0; j < data[k]; j++) {
            if (M2C_FIELD((data + 1)[j + k], s32 *, 0x74) & 0x800) {
                return 1;
            }
        }
    }
    return 0;
}

s32 ActionVehicleTeleport(u32 *action) {
    extern Vector4 g_zeroVector;
    void InverseXformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    typedef struct {
        Matrix4 mtx;
        u8 pad40[0x44 - 0x40];
        u32 fx;
        u8 pad48[0x54 - 0x48];
        u32 flags;
    } TeleportData;
    typedef struct {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        s32 unk44;
        f32 unk48;
        f32 unk4C;
    } FxParams;
    typedef struct {
        Vector4 vel;
        Matrix4 mtx;
        Vector4 quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0x8C - 0x78];
        void (**vtbl)(void *);
        u8 pad90[0x94 - 0x90];
        PTR32(u8) player;
        u8 pad98[0x3B0 - 0x98];
        s32 unk3B0;
    } Car;
    TeleportData *data;
    u32 i;
    Car *car;
    Vector4 local __attribute__((aligned(16)));
    FxParams params __attribute__((aligned(16)));

    data = (TeleportData *)PTR(action[1]);
    for (i = 0; i < action[2]; i++) {
        car = SLOT_LOAD(Car *, (action[i + 3] + 0x28));
        if (data->fx != 0) {
            params.id = data->fx;
            params.pos = data->mtx.v[3];
            SubMatrixToQuaternion(data, &params.quat);
            params.vel.x = params.vel.y = params.vel.z = 0.0f;
            params.unk44 = 0;
            params.unk48 = 0.0f;
            params.unk4C = 0.0f;
            params.angVel = g_zeroVector;
            ThingManager_DARYL_CreateThing(1, &params);
        }
        if (data->flags & 2) {
            InverseXformVecOrientation(&car->mtx, &car->vel, &local);
            XformVecOrientation(data, &local, &car->vel);
        } else if (GETP(car->player) != NULL && SLOT_LOAD(Car *, (GETP(car->player) + 0x2C)) != NULL &&
                   SLOT_LOAD(Car *, (GETP(car->player) + 0x2C)) == car) {
            GETP(car->player)[0x32] = 5;
        }
        car->mtx = data->mtx;
        SubMatrixToQuaternion(&car->mtx, &car->quat);
        car->vtbl[3](car);
        car->flags |= 0x800;
        car->unk3B0 = -1;
        if (!Player_IsHuman(GETP(car->player))) {
            *(s32 *)(GETP(car->player) + 0x4A4) = 0x20;
            *(s32 *)(GETP(car->player) + 0x634) = -0x78;
        }
    }
    return 1;
}

f32 MagnitudeVector(void *v);
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

    ii->unk10 = *(s32 *)((u8 *)thing + 0x78);
    ii->speed = 0.681818f * MagnitudeVector(thing);
    ii->unk18 = 0;
    ii->unk4 = 0;
}

void Thing_SetSlavePlayers(void *thing) {
    void *slave;

    for (slave = SLOT_LOAD(void *, (u8 *)(thing) + 0x88); slave != NULL; slave = SLOT_LOAD(void *, (u8 *)(slave) + 0xA4)) {
        M2C_FIELD(slave, s32 *, 0x94) = M2C_FIELD(thing, s32 *, 0x94);
    }
}

void Thing_DrawSlaves(Thing *thing) {
    Thing *slave;
    Quaternion quat __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    for (slave = (Thing *)PTR(thing->unk88); slave != NULL; slave = SLOT_LOAD(Thing *, ((u8 *)slave + 0xA4))) {
        if (!(slave->flags & 0x200200)) {
            quat = slave->quat;
            pos = slave->mtx.v[3];
            QuaternionMult(&thing->quat, &quat, &slave->quat);
            XformVec(&thing->mtx, &pos, &slave->mtx.v[3]);
            (*(void (***)(Thing *))((u8 *)slave + 0x8C))[4](slave);
            slave->quat = quat;
            slave->mtx.v[3] = pos;
        }
    }
}

void Thing_MoveSlaves(Thing *thing) {
    Thing *slave;
    Thing *next;
    Quaternion quat __attribute__((aligned(16)));
    Vector4 trans __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    for (slave = (Thing *)PTR(thing->unk88); slave != NULL && !(thing->flags & 0x201000); slave = next) {
        next = SLOT_LOAD(Thing *, ((u8 *)slave + 0xA4));
        if (!(slave->flags & 0x201000)) {
            if (!(slave->flags & 0x400000)) {
                CopyVector(&quat, &slave->quat);
                CopyVector(&trans, &slave->mtx.v[3]);
                CopyVector(&pos, &slave->unk0);
                QuaternionMult(&thing->quat, &quat, &slave->quat);
                XformVec(&thing->mtx, &trans, &slave->mtx.v[3]);
                slave->unk0 = thing->unk0;
                (*(void (***)(Thing *))((u8 *)slave + 0x8C))[3](slave);
                CopyVector(&slave->quat, &quat);
                CopyVector(&slave->mtx.v[3], &trans);
                CopyVector(&slave->unk0, &pos);
            } else {
                (*(void (***)(Thing *))((u8 *)slave + 0x8C))[3](slave);
            }
        }
    }
    ThingManagerList_RemoveReleasedThings(&thing->unk88);
}

void Thing_InitAllSlavesNow(s32 arg0, void *data, s32 mask) {
    void ThingManagerList_InitAllSlavesNow(void *list, void *data, s32 mask);
    ThingManagerList_InitAllSlavesNow(PTR(arg0 + 0x88), data, mask);
}

void Thing_SlaveTransform(Thing *thing, Matrix4 *mtx) {
    Matrix4 local;

    local = thing->mtx;
    MultiplyMatrix(&local, mtx, &thing->mtx);
    SubMatrixToQuaternion(&thing->mtx, &thing->quat);
}

void Thing_SlaveTransformQuaternionOnly(Thing *thing, Matrix4 *mtx) {
    Quaternion rot __attribute__((aligned(16)));
    Quaternion quat __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    quat = thing->quat;
    pos = thing->mtx.v[3];
    XformVec(mtx, &pos, &thing->mtx.v[3]);
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

s32 Thing_AddToGrid(void *thing, s32 type) {
    void Thing_GetDynamicMinMax(void *thing, Vector4 *min, Vector4 *max, f32 expand);
    s32 Grid_AddObject(s32 type, void *thing, Vector4 *min, Vector4 *max);
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    Thing_GetDynamicMinMax(thing, &min, &max, 0.0f);
    *(s32 *)((u8 *)thing + 0x84) = Grid_AddObject(type, thing, &min, &max);
    return *(s32 *)((u8 *)thing + 0x84);
}

void Thing_GetDynamicMinMax(Thing *thing, Vector4 *min, Vector4 *max, f32 expand) {
    typedef struct Box {
        u8 pad[0x10];
        Vector4 corners[8];
    } Box;
    Box *box;
    u32 i;
    Vector4 p3 __attribute__((aligned(16)));
    Vector4 p2 __attribute__((aligned(16)));
    Vector4 p1 __attribute__((aligned(16)));

    min->x = min->y = min->z = min->w = 99999.9f;
    max->x = max->y = max->z = max->w = -99999.9f;
    switch (thing->unk80) {
    case 0:
        *max = thing->mtx.v[3];
        *min = *max;
        break;
    case 3:
        CopyVector(&p3, &thing->mtx.v[3]);
        CopyVector(min, &thing->mtx.v[3]);
        CopyVector(max, &thing->mtx.v[3]);
        p3.x += thing->unk0.x * expand;
        p3.y += thing->unk0.y * expand;
        p3.z += thing->unk0.z * expand;
        MinMaxVec(min, max, &p3);
        break;
    case 2:
    case 4:
    case 5:
    case 6:
        CopyVector(&p2, &thing->mtx.v[3]);
        CopyVector(min, &thing->mtx.v[3]);
        CopyVector(max, &thing->mtx.v[3]);
        p2.x += thing->unk0.x * expand;
        p2.y += thing->unk0.y * expand;
        p2.z += thing->unk0.z * expand;
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
        min->x -= thing->unk70;
        min->y -= thing->unk70;
        min->z -= thing->unk70;
        max->x += thing->unk70;
        max->y += thing->unk70;
        max->z += thing->unk70;
        break;
    case 1:
        if (thing->flags & 8) {
            thing = ((Thing *(*)(Thing *))GETP(thing->vtable)[0x10])(thing);
            CopyVector(min, (Vector4 *)((u8 *)thing + 0xD0));
            CopyVector(max, (Vector4 *)((u8 *)thing + 0xE0));
        } else {
            box = ((Box *(*)(Thing *))GETP(thing->vtable)[0x10])(thing);
            for (i = 0; i < 8; i++) {
                p1 = box->corners[i];
                MinMaxVec(min, max, &p1);
                p1.x += thing->unk0.x * expand;
                p1.y += thing->unk0.y * expand;
                p1.z += thing->unk0.z * expand;
                MinMaxVec(min, max, &p1);
            }
        }
        break;
    }
}

void Thing_GetStaticRotationalMinMax(void *thing, Vector4 *min, Vector4 *max) {
    *max = *(Vector4 *)((u8 *)thing + 0x40);
    *min = *max;
    min->x -= M2C_FIELD(thing, f32 *, 0x70);
    min->y -= M2C_FIELD(thing, f32 *, 0x70);
    min->z -= M2C_FIELD(thing, f32 *, 0x70);
    max->x += M2C_FIELD(thing, f32 *, 0x70);
    max->y += M2C_FIELD(thing, f32 *, 0x70);
    max->z += M2C_FIELD(thing, f32 *, 0x70);
}

void Thing_DynamicGridMove(void *thing, f32 expand) {
    void Grid_MoveObject(s32 obj, Vector4 *min, Vector4 *max);
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    if (*(u32 *)((u8 *)thing + 0x84) != 0) {
        Thing_GetDynamicMinMax(thing, &min, &max, expand);
        Grid_MoveObject(*(s32 *)((u8 *)thing + 0x84), &min, &max);
    }
}

void Thing_StaticGridMove(void *thing) {
    void Grid_MoveObject(s32 obj, Vector4 *min, Vector4 *max);
    void Thing_GetDynamicMinMax(void *thing, Vector4 *min, Vector4 *max, f32 expand);
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    if (*(u32 *)((u8 *)thing + 0x84) != 0) {
        Thing_GetDynamicMinMax(thing, &min, &max, 0.0f);
        Grid_MoveObject(*(s32 *)((u8 *)thing + 0x84), &min, &max);
    }
}

void Thing_RotationalGridMove(void *thing) {
    void Grid_MoveObject(s32 obj, Vector4 *min, Vector4 *max);
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));

    if (M2C_FIELD(thing, u32 *, 0x84) != 0) {
        Thing_GetStaticRotationalMinMax(thing, &min, &max);
        Grid_MoveObject(M2C_FIELD(thing, s32 *, 0x84), &min, &max);
    }
}

void Thing_CollideWithItems(u8 *thing) {
    extern Vector4 g_zeroVector;
    struct GridResult {
        u32 data;
        u32 id;
    };
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    struct { Vector4 pos; Vector4 quat; Vector4 row; Vector4 vel; u32 id; u32 unk74; f32 unk78; f32 unk7C; } params __attribute__((aligned(16)));
    struct GridResult results[64];
    s32 count;
    s32 i;
    u8 *item;
    s8 picked;

    max = *(Vector4 *)(thing + 0x40);
    min = max;
    min.x -= *(f32 *)(thing + 0x70);
    min.y -= *(f32 *)(thing + 0x70);
    min.z -= *(f32 *)(thing + 0x70);
    max.x += *(f32 *)(thing + 0x70);
    max.y += *(f32 *)(thing + 0x70);
    max.z += *(f32 *)(thing + 0x70);
    count = Grid_GetObjects(&min, &max, 8, results, 0x40);
    for (i = 0; i < count; i++) {
        item = (u8 *)PTR(results[i].id);
        if (*(s16 *)PTR((results[i].id + 0xCC)) > 0) {
            continue;
        }
        if (PSVECSquareDistance((f32 *)(thing + 0x40), (f32 *)PTR((results[i].id + 0x40))) < *(f32 *)(item + 0xE8)) {
            picked = 0;
            switch (*(s32 *)(thing + 0xA8)) {
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
            if (picked && *(u32 *)(SLOT_LOAD(u8 *, (item + 0xC0)) + 0x24) != 0) {
                params.id = *(u32 *)(SLOT_LOAD(u8 *, (item + 0xC0)) + 0x24);
                params.pos = *(Vector4 *)(thing + 0x40);
                params.row.z = 0.0f;
                params.row.y = 0.0f;
                params.row.x = 0.0f;
                params.quat = *(Vector4 *)(thing + 0x50);
                params.unk78 = 0.0f;
                params.unk7C = 0.0f;
                params.unk74 = 0;
                params.vel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &params);
            }
            return;
        }
    }
}

void Thing_ProcessEndEvent(u8 *thing, u8 *event, u8 *collision, Vector4 *vel) {
    extern Vector4 g_zeroVector;
    struct {
        Vector4 pos;
        Vector4 quat;
        Vector4 origin;
        Vector4 vel;
        PTR32(void) data;
        s32 owner;
        f32 a;
        f32 b;
    } params __attribute__((aligned(16)));
    void *data;
    u8 *other;

    data = NULL;
    if (event != NULL && *(u32 *)(event + 0x34) != 0) {
        data = SLOT_LOAD(void *, (u32 *)SLOT_LOAD(void *, (event + 0x38)) + (RandomAdd_s32(0, *(u32 *)(event + 0x34))));
    }
    if (data != NULL) {
        SETP(params.data, data);
        params.pos = *(Vector4 *)(thing + 0x40);
        params.quat = *(Vector4 *)(thing + 0x50);
        params.origin = *(Vector4 *)(thing + 0x0);
        params.owner = *(s32 *)(thing + 0x94);
        params.a = 0.0f;
        params.b = 0.0f;
        if (vel != NULL) {
            params.vel = *vel;
        } else {
            params.vel = g_zeroVector;
        }
        if (collision != NULL) {
            other = SLOT_LOAD(u8 *, (collision + 0x20));
            if (other != NULL && other != thing) {
                params.owner = *(s32 *)(other + 0x94);
            } else {
                other = SLOT_LOAD(u8 *, (collision + 0x24));
                if (other != NULL && other != thing) {
                    params.owner = *(s32 *)(other + 0x94);
                }
            }
        }
        ThingManager_DARYL_CreateThing(1, &params);
    }
}

void Thing_CollideWithRay(void)
{
}

void Thing_GetTargetPos(void *thing, Quaternion *pos) {
    *pos = *(Quaternion *)((u8 *)thing + 0x40);
}

s32 Thing_GetOrientedCollision(void) {
    return 0;
}

s32 Thing_HitsTerrain(void *thing, void *arg1, void *arg2) {
    s32 OBoxHitsTerrain(void *obox, void *thing, void *arg1, void *arg2);

    if (GETP(g_worldData.world) == NULL) {
        return 0;
    }
    switch (M2C_FIELD(thing, s16 *, 0x80)) {
    case 3:
        return RayHitsTerrain_Aux3((u8 *)thing + 0x40, thing, arg1, arg2);
    case 1:
        return OBoxHitsTerrain(M2C_FIELD(SLOT_LOAD(void *, (u8 *)(thing) + 0x8C), void *(**)(void *), 0x40)(thing), thing, arg1, arg2);
    }
    return 0;
}

void Thing_PassiveTargeting(void *thing)
{
}

u32 CollisionInfo_GetOther(void *arg0, u32 arg1) {
    u32 temp_r0;

    temp_r0 = M2C_FIELD(arg0, u32 *, 0x20);
    if (temp_r0 == arg1) {
        return M2C_FIELD(arg0, u32 *, 0x24);
    }
    return temp_r0;
}

