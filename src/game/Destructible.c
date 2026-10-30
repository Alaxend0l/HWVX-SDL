#include "common.h"


extern f32 g_zeroVector[4];
extern u8 g_destructibleVTable[];

f32 MagnitudeVector(Vector4 *v);

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

void Destructible_Init()
{
}

void Destructible_Release(Destructible *destructible) {
    void OBoxHolder_Release(OBox *obox);
    s8 paused;
    u8 *trigger;

    Trigger_DestructibleKilled(destructible);
    if (SLOT_LOAD(u8 *, ((u8 *)destructible + 0x8C)) == g_destructibleVTable) {
        trigger = SLOT_LOAD(u8 *, ((u8 *)destructible + 0x90));
        if (trigger != NULL) {
            SLOT_STORE(((u8 *)destructible + 0x90), NULL);
            *(u32 *)(trigger + 0x2C) = 0;
            paused = Trigger_IsTACSystemPaused();
            if (paused == 0) {
                *(u32 *)(trigger + 0x30) = 0;
            }
        }
    }
    OBoxHolder_Release(GETP(destructible->orientedBox));
    SETP(destructible->orientedBox, NULL);
    OConvexHullHolder_Release(GETP(destructible->hull));
    SETP(destructible->hull, NULL);
    Thing_Release(HND(destructible));
}

s32 Destructible_Move(Destructible *destructible) {
    typedef struct {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        u32 unk44;
        f32 unk48;
        f32 unk4C;
    } __attribute__((aligned(16))) DebrisParams;
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    void GenerateQuaternionXYZ(Vector4 *euler, Vector4 *quat);
    DebrisParams params;
    Vector4 rot __attribute__((aligned(16)));
    Vector4 quat __attribute__((aligned(16)));
    Vector4 axis __attribute__((aligned(16)));
    f32 angle;
    u8 *slave;
    u32 id;
    s8 spin;
    DestructibleInfo *info;

    if (destructible->flags & 0x200000) {
        return 0;
    }
    if (GETP(destructible->player) != NULL &&
        (slave = ThingManagerList_GetByType(&destructible->slaves, 8)) != NULL &&
        SLOT_LOAD(u8 *, (slave + 0x94)) != NULL &&
        (spin = -*SLOT_LOAD(u8 *, (slave + 0x94))) != 0) {
        axis.x = 0.0f;
        axis.y = 0.017453292f;
        axis.z = 0.0f;
        GenerateQuaternionXYZ(&axis, &quat);
        GenerateSpinAngleFromQuaternion(&quat, &axis, &angle);
        GenerateQuaternion(&axis, &rot, spin * (angle * g_fDeltaTimeSec));
        QuaternionMult((Vector4 *)(slave + 0x50), &rot, &quat);
        *(Vector4 *)(slave + 0x50) = quat;
        QuaternionToSubMatrix(slave + 0x50, slave + 0x10);
    }
    if (!(destructible->flags & 0x10000000)) {
        if (destructible->deathTimer < 0) {
            id = 0;
            info = GETP(GETP(destructible->record)->info);
            if (info != NULL && info->numDebris != 0) {
                id = GETP(info->debris)[RandomAdd_s32(0, info->numDebris)];
            }
            if (id != 0) {
                params.id = id;
                params.pos = destructible->offsetPos;
                params.quat = *(Vector4 *)destructible->pad50;
                params.vel = destructible->pos;
                params.unk44 = 0;
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
    slave = ThingManagerList_GetByType(&destructible->slaves, 0x11);
    if (slave != NULL) {
        if (GETP(GETP(destructible->record)->info)->maxHealth != 0) {
            *(f32 *)(slave + 0xB8) = (f32)destructible->health / (f32)GETP(GETP(destructible->record)->info)->maxHealth;
        } else {
            *(f32 *)(slave + 0xB8) = 0.0f;
        }
        if (*(f32 *)(slave + 0xB8) > 1.0f) {
            *(f32 *)(slave + 0xB8) = 1.0f;
        }
    }
    return 1;
}

void Destructible_Draw(Destructible *destructible) {
    f32 Gfx_GetMinCameraDistSquared(void *pos);
    f32 distSq;
    ThingRecord *record;

    if (destructible->drawDist) {
        distSq = Gfx_GetMinCameraDistSquared(&destructible->offsetPos);
        if ((u32)ViewManager_GetViewFormat() > 1) {
            distSq *= 4.0f;
        }
        if (distSq > destructible->drawDist * destructible->drawDist) {
            return;
        }
    }
    QuaternionToSubMatrix(destructible->pad50, destructible->matrix);
    record = GETP(destructible->record);
    if (record->noDraw == 0) {
        Gfx_DrawGeometry_MatrixNotCached(HND(GETP(record->geometry)), HND(destructible->matrix));
    }
    Thing_DrawSharedSlaves(destructible, GETP(destructible->record)->slaves);
}

f32 Destructible_GetMass(Destructible *destructible) {
    return GETP(GETP(destructible->record)->info)->mass;
}

void Destructible_DynamicCollideImpact(u8 *destructible, u8 *collision, s32 damage, f32 force) {
    typedef struct ABox {
        Vector4 v[3];
    } __attribute__((aligned(16))) ABox;
    typedef struct DestructibleInfo {
        f32 inertia[3];
        u8 pad0C[0x10 - 0xC];
        u8 type;
        u8 takesDamage;
        u8 pad12[0x18 - 0x12];
        s32 armour;
        u8 pad1C[0x20 - 0x1C];
        f32 mass;
        u8 pad24[0x44 - 0x24];
        PTR32(ABox) box;
    } DestructibleInfo;
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    void InverseXformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    DestructibleInfo *info;
    ABox box;
    Vector4 angVel __attribute__((aligned(16)));
    Vector4 torque __attribute__((aligned(16)));
    Vector4 impulse __attribute__((aligned(16)));
    Vector4 r __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Vector4 scaled __attribute__((aligned(16)));
    f32 scale;
    f32 invX;
    f32 invY;
    f32 invZ;
    f32 inertia;
    u8 *other;

    info = SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (destructible + 0xC0)) + 0x10));
    damage -= info->armour;
    if (*(u32 *)(destructible + 0x74) & 0x200000) {
        return;
    }
    if (info->takesDamage && damage > 0) {
        *(s32 *)(destructible + 0x78) -= damage;
    }
    if (*(s32 *)(destructible + 0x78) > 0) {
        return;
    }
    scale = force / (SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (destructible + 0xC0)) + 0x10)))->mass;
    ((Vector4 *)destructible)->x += ((Vector4 *)(collision + 0x10))->x * scale;
    ((Vector4 *)destructible)->y += ((Vector4 *)(collision + 0x10))->y * scale;
    ((Vector4 *)destructible)->z += ((Vector4 *)(collision + 0x10))->z * scale;
    PSVECSubtract((Vector4 *)collision, (Vector4 *)(destructible + 0x40), &r);
    impulse.x = force * ((Vector4 *)(collision + 0x10))->x;
    impulse.y = force * ((Vector4 *)(collision + 0x10))->y;
    impulse.z = force * ((Vector4 *)(collision + 0x10))->z;
    PSVECCrossProduct(&r, &impulse, &torque);
    info = SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (destructible + 0xC0)) + 0x10));
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
        inertia = 2.0f * info->mass * *(f32 *)(destructible + 0x70) * *(f32 *)(destructible + 0x70);
        invX = 1.0f / (info->inertia[0] * inertia);
        invY = 1.0f / (info->inertia[1] * inertia);
        invZ = 1.0f / (info->inertia[2] * inertia);
        break;
    }
    InverseXformVecOrientation(destructible + 0x10, &torque, &local);
    scaled.x = invX * local.x;
    scaled.y = invY * local.y;
    scaled.z = invZ * local.z;
    XformVecOrientation(destructible + 0x10, &scaled, &angVel);
    Thing_ProcessEndEvent(destructible, SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (destructible + 0xC0)) + 0x10)), collision, &angVel);
    if (SLOT_LOAD(u8 *, (destructible + 0x90)) != NULL && *(u32 *)(SLOT_LOAD(u8 *, (destructible + 0x90)) + 0x30) == 0x1A) {
        *(u32 *)(destructible + 0x74) |= 0x200;
        *(u32 *)(destructible + 0x74) |= 2;
        return;
    }
    other = HND(PTR(CollisionInfo_GetOther(collision, destructible)));
    if (other != NULL && SLOT_LOAD(void *, (other + 0x94)) != NULL && Player_IsHuman(SLOT_LOAD(void *, (other + 0x94)))) {
        *(u32 *)(destructible + 0x74) |= 0x20;
    }
    Destructible_Release((Destructible *)destructible);
    if (SLOT_LOAD(u8 *, (destructible + 0x90)) != NULL) {
        *(u32 *)(SLOT_LOAD(u8 *, (destructible + 0x90)) + 0x30) = 3;
    }
}

s32 Destructible_GetSurfaceScript(Destructible *destructible) {
    return GETP(GETP(destructible->record)->info)->surfaceScript;
}

u32 Destructible_Invalidate(Destructible *destructible, u32 start, u32 end) {
    if ((u32)HND(GETP(destructible->record)) >= start && (u32)HND(GETP(destructible->record)) < end) {
        return 1;
    }
    return 0;
}

s32 ActionDestructibleChangeMode(u32 *action) {
    s32 mode;
    u32 count;
    u32 i;
    u8 *thing;
    Destructible *destructible;

    mode = action[0];
    count = action[1];
    for (i = 0; i < count; i++) {
        thing = (u8 *)PTR(action[i + 2]);
        destructible = SLOT_LOAD(Destructible *, (thing + 0x2C));
        if (destructible != NULL) {
            switch (mode) {
            case 3:
                Thing_ProcessEndEvent((u8 *)destructible, (u8 *)GETP(GETP(destructible->record)->info), NULL, NULL);
                Destructible_Release(destructible);
                break;
            case 0:
                ThingManagerList_ReleaseSlavesBySlaveType(&destructible->slaves, 0xFF);
                destructible->flags |= 0x200;
                destructible->flags |= 0x2;
                break;
            case 1:
                Thing_InitAllSlavesNow(HND(destructible), (u8 *)GETP(destructible->record) + 0x18, 0xFF);
                destructible->flags &= ~0x200;
                destructible->flags &= ~0x2;
                break;
            case 25:
                Destructible_Release(destructible);
                break;
            case 26:
                ThingManagerList_ReleaseSlavesBySlaveType(&destructible->slaves, 0xFF);
                Thing_ProcessEndEvent((u8 *)destructible, (u8 *)GETP(GETP(destructible->record)->info), NULL, NULL);
                destructible->flags |= 0x200;
                destructible->flags |= 0x2;
                break;
            }
        }
        *(s32 *)(thing + 0x30) = mode;
    }
    return 1;
}

s32 ActionMoveableChangeMode(u32 *action) {
    typedef struct {
        u8 pad0[0x10];
        PTR32(u8) mover;
    } Thing;
    typedef struct {
        u8 pad0[0x8];
        f32 start;
        f32 end;
    } Anim;
    u32 i;
    u8 *mover;

    for (i = 0; i < action[4]; i++) {
        mover = GETP(((Thing *)PTR(action[i + 5]))->mover);
        if (mover != NULL) {
            switch (action[0]) {
            case 6:
                *(f32 *)(mover + 0xD8) = action[2];
                break;
            case 12:
                *(u32 *)(mover + 0x74) |= 0x4000;
                break;
            case 11:
                *(u32 *)(mover + 0x74) &= ~0x4000;
                *(f32 *)(mover + 0xD8) = action[2];
                break;
            case 3:
                (*(void (**)(void *))(SLOT_LOAD(u8 *, (mover + 0x8C)) + 4))(mover);
                break;
            }
            switch (action[1]) {
            case 13:
                if (SLOT_LOAD(Anim *, (mover + 0xD4)) != NULL) {
                    *(f32 *)(mover + 0xDC) = (SLOT_LOAD(Anim *, (mover + 0xD4)))->start;
                }
                Mover_GetTrackPos(mover, (f32 *)(mover + 0x40), *(f32 *)(mover + 0xDC));
                break;
            case 14:
                if (SLOT_LOAD(Anim *, (mover + 0xD4)) != NULL) {
                    *(f32 *)(mover + 0xDC) = (SLOT_LOAD(Anim *, (mover + 0xD4)))->end;
                }
                Mover_GetTrackPos(mover, (f32 *)(mover + 0x40), *(f32 *)(mover + 0xDC));
                break;
            }
        }
    }
    return 1;
}

void Destructible_CollideWithRay(Destructible *destructible, Vector4 *start, Vector4 *end, Vector4 *hit, f32 maxDist) {
    f32 dist;

    dist = PhysicalProperties_CollideWithRay(GETP(GETP(destructible->record)->info), destructible->matrix, start, end, maxDist, hit);
    if (hit != NULL && dist < maxDist) {
        *(Vector4 *)((u8 *)hit + 0x30) = destructible->pos;
    }
}

void Destructible_GetTargetPos(Destructible *destructible, Vector4 *pos) {
    switch (GETP(GETP(destructible->record)->info)->type) {
    case 0:
    case 3:
        *pos = GETP(destructible->orientedBox)->v[0];
        break;
    case 1:
    case 6:
        *pos = GETP(destructible->hull)->point;
        break;
    default:
        *pos = destructible->offsetPos;
        break;
    }
}

void *Destructible_GetOrientedCollision(Destructible *destructible) {
    DestructibleInfo *info;

    info = GETP(GETP(destructible->record)->info);
    switch (info->type) {
    case 0:
    case 3:
        if (destructible->flags & 0x180) {
            BuildOOB(GETP(info->box), destructible->matrix, GETP(destructible->orientedBox));
            destructible->flags &= ~0x180;
        }
        return GETP(destructible->orientedBox);
    case 1:
    case 6:
        BuildOConvexHullOffset(GETP(destructible->hull), GETP(info->hull), destructible->matrix, g_zeroVector);
        return GETP(destructible->hull);
    default:
        return NULL;
    }
}
