#include "common.h"


static inline void Mover_SetRotation(u8 *mover, Quaternion quat) {
    *(Quaternion *)(mover + 0xE0) = quat;
}

void Mover_GetTrackPos(void *mover, f32 *pos, f32 time) {
    f32 keyFrame[8];

    Gfx_AnimExtractKeyFrame(SLOT_LOAD(void *, (u8 *)(mover) + 0xD4), time, keyFrame, 1);
    pos[0] = keyFrame[4];
    pos[1] = keyFrame[5];
    pos[2] = keyFrame[6];
}

void PhysicalObject_ProcessForce(u8 *obj, Vector4 *force, Vector4 *dir)
{

    Vector4 pointVel;
    Vector4 cross;
    Vector4 r;
    Vector4 torque;
    Vector4 objVel;
    Vector4 scaled __attribute__((aligned(16)));

    objVel = *(Vector4 *)obj;
    PSVECScale(dir, &scaled, *(u32 *)(obj + 8));
    PSVECSubtract(force, (Vector4 *)(obj + 0x40), &r);
    GetComponentOfVector(dir, &objVel);
    PSVECCrossProduct((Vector4 *)(obj + 0x130), &r, &cross);
    PSVECAdd((Vector4 *)obj, &cross, &pointVel);
    PSVECCrossProduct(&r, &scaled, &torque);

    *(f32 *)(obj + 0xF0) += scaled.x;
    *(f32 *)(obj + 0xF4) += scaled.y;
    *(f32 *)(obj + 0xF8) += scaled.z;
    *(f32 *)(obj + 0x100) += torque.x;
    *(f32 *)(obj + 0x104) += torque.y;
    *(f32 *)(obj + 0x108) += torque.z;
}

s32 PhysicalObject_Init(u8 *obj, u8 *data) {
    typedef struct ABox {
        Vector4 v[3];
    } __attribute__((aligned(16))) ABox;
    typedef struct Mesh {
        u8 pad0[0xC];
        u32 count;
        u8 pad10[0x1C - 0x10];
        Vector4 *verts;
    } Mesh;
    typedef struct DestructibleInfo {
        f32 inertia[3];
        u8 pad0C[0x10 - 0xC];
        u8 type;
        u8 pad11[0x20 - 0x11];
        f32 mass;
        u8 pad24[0x44 - 0x24];
        ABox *box;
        Mesh *mesh;
    } DestructibleInfo;
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    void InverseXformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    u8 hit[0x50];
    ABox box;
    Vector4 scaled __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    f32 dist;
    DestructibleInfo *info;
    Mesh *mesh;
    u32 i;

    *(f32 *)(data + 4) += 0.2f;
    Destructible_Init(obj, data);
    *(u32 *)(obj + 0x74) &= ~8;
    *(u32 *)(obj + 0x74) &= ~0x200;
    if (SLOT_LOAD(u8 *, (data + 0x5C)) != NULL) {
        *(f32 *)(obj + 0xDC) = 1.0f + *(f32 *)(SLOT_LOAD(u8 *, (data + 0x5C)) + 0xC);
    }
    info = SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10));
    switch (info->type) {
    case 0:
        box = *info->box;
        *(f32 *)(obj + 0x120) = 1.0f / (info->inertia[0] * info->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
        *(f32 *)(obj + 0x124) = 1.0f / ((SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->inertia[1] * (SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
        *(f32 *)(obj + 0x128) = 1.0f / ((SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->inertia[2] * (SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
        break;
    case 1:
    case 6:
        mesh = info->mesh;
        box.v[0].x = 9999.9f;
        box.v[0].y = 9999.9f;
        box.v[0].z = 9999.9f;
        box.v[1].x = -9999.9f;
        box.v[1].y = -9999.9f;
        box.v[1].z = -9999.9f;
        for (i = 0; i < mesh->count; i++) {
            if (box.v[0].x > mesh->verts[i].x) {
                box.v[0].x = mesh->verts[i].x;
            }
            if (box.v[0].y > mesh->verts[i].y) {
                box.v[0].y = mesh->verts[i].y;
            }
            if (box.v[0].z > mesh->verts[i].z) {
                box.v[0].z = mesh->verts[i].z;
            }
            if (box.v[1].x < mesh->verts[i].x) {
                box.v[1].x = mesh->verts[i].x;
            }
            if (box.v[1].y < mesh->verts[i].y) {
                box.v[1].y = mesh->verts[i].y;
            }
            if (box.v[1].z < mesh->verts[i].z) {
                box.v[1].z = mesh->verts[i].z;
            }
        }
        *(f32 *)(obj + 0x120) = 1.0f / ((SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->inertia[0] * (SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->mass * (box.v[1].y * box.v[1].y + box.v[1].z * box.v[1].z));
        *(f32 *)(obj + 0x124) = 1.0f / ((SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->inertia[1] * (SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->mass * (box.v[1].x * box.v[1].x + box.v[1].z * box.v[1].z));
        *(f32 *)(obj + 0x128) = 1.0f / ((SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->inertia[2] * (SLOT_LOAD(DestructibleInfo *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))->mass * (box.v[1].x * box.v[1].x + box.v[1].y * box.v[1].y));
        break;
    }
    if (((Vector4 *)(data + 0x20))->x || ((Vector4 *)(data + 0x20))->y || ((Vector4 *)(data + 0x20))->z) {
        InverseXformVecOrientation(obj + 0x10, (Vector4 *)(data + 0x20), &local);
        scaled.x = local.x / *(f32 *)(obj + 0x120);
        scaled.y = local.y / *(f32 *)(obj + 0x124);
        scaled.z = local.z / *(f32 *)(obj + 0x128);
        XformVecOrientation(obj + 0x10, &scaled, (Vector4 *)(obj + 0x110));
    }
    if (!(*(u32 *)(obj + 0x74) & 0x40002)) {
        dist = 1.0f;
        if ((s8)Thing_HitsTerrain(obj, &dist, hit) && 0.0f == dist) {
            (*(void (**)(u8 *))(SLOT_LOAD(u8 *, (obj + 0x8C)) + 4))(obj);
        }
    }
    return 1;
}

void Mover_Init(void) {
    M2C_UNK fn_800957D4();
    fn_800957D4();
}

void PhysicalObject_IntegrateNonLateral(u8 *obj) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeSec;
    void XformVecOrientation(void *mtx, f32 *in, Vector4 *out);
    void InverseXformVecOrientation(void *mtx, Vector4 *in, f32 *out);
    f32 scaled[4];
    f32 local[4];
    f32 invMass;

    invMass = g_fDeltaTimeSec / *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x20);
    ((Vector4 *)obj)->x += invMass * ((Vector4 *)(obj + 0xF0))->x;
    ((Vector4 *)obj)->y += invMass * ((Vector4 *)(obj + 0xF0))->y;
    ((Vector4 *)obj)->z += invMass * ((Vector4 *)(obj + 0xF0))->z;
    ((Vector4 *)(obj + 0x110))->x += g_fDeltaTimeSec * ((Vector4 *)(obj + 0x100))->x;
    ((Vector4 *)(obj + 0x110))->y += g_fDeltaTimeSec * ((Vector4 *)(obj + 0x100))->y;
    ((Vector4 *)(obj + 0x110))->z += g_fDeltaTimeSec * ((Vector4 *)(obj + 0x100))->z;
    *(Vector4 *)(obj + 0x100) = g_zeroVector;
    *(Vector4 *)(obj + 0xF0) = *(Vector4 *)(obj + 0x100);
    InverseXformVecOrientation(obj + 0x10, (Vector4 *)(obj + 0x110), local);
    scaled[0] = ((Vector4 *)(obj + 0x120))->x * local[0];
    scaled[1] = ((Vector4 *)(obj + 0x120))->y * local[1];
    scaled[2] = ((Vector4 *)(obj + 0x120))->z * local[2];
    XformVecOrientation(obj + 0x10, scaled, (Vector4 *)(obj + 0x130));
}

s32 PhysicalObject_PreMove(u8 *obj) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeSec;
    void XformVecOrientation(void *mtx, f32 *in, f32 *out);
    void InverseXformVecOrientation(void *mtx, Vector4 *in, f32 *out);
    f32 angVel[4];
    f32 scaled[4];
    f32 local[4];
    f32 damping;
    f32 scaled2[4];
    f32 local2[4];
    f32 invMass;

    if (!(*(u32 *)(obj + 0x74) & 8)) {
        *(Vector4 *)(obj + 0x100) = g_zeroVector;
        *(Vector4 *)(obj + 0xF0) = *(Vector4 *)(obj + 0x100);
        ((Vector4 *)(obj + 0xF0))->y += *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x24) *
                                        *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x20);
        damping = *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x2C);
        InverseXformVecOrientation(obj + 0x10, (Vector4 *)(obj + 0x110), local);
        scaled[0] = ((Vector4 *)(obj + 0x120))->x * local[0];
        scaled[1] = ((Vector4 *)(obj + 0x120))->y * local[1];
        scaled[2] = ((Vector4 *)(obj + 0x120))->z * local[2];
        XformVecOrientation(obj + 0x10, scaled, angVel);
        ((Vector4 *)(obj + 0x100))->x += -damping * angVel[0];
        ((Vector4 *)(obj + 0x100))->y += -damping * angVel[1];
        ((Vector4 *)(obj + 0x100))->z += -damping * angVel[2];
        invMass = g_fDeltaTimeSec / *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x20);
        ((Vector4 *)obj)->x += invMass * ((Vector4 *)(obj + 0xF0))->x;
        ((Vector4 *)obj)->y += invMass * ((Vector4 *)(obj + 0xF0))->y;
        ((Vector4 *)obj)->z += invMass * ((Vector4 *)(obj + 0xF0))->z;
        ((Vector4 *)(obj + 0x110))->x += g_fDeltaTimeSec * ((Vector4 *)(obj + 0x100))->x;
        ((Vector4 *)(obj + 0x110))->y += g_fDeltaTimeSec * ((Vector4 *)(obj + 0x100))->y;
        ((Vector4 *)(obj + 0x110))->z += g_fDeltaTimeSec * ((Vector4 *)(obj + 0x100))->z;
        *(Vector4 *)(obj + 0x100) = g_zeroVector;
        *(Vector4 *)(obj + 0xF0) = *(Vector4 *)(obj + 0x100);
        InverseXformVecOrientation(obj + 0x10, (Vector4 *)(obj + 0x110), local2);
        scaled2[0] = ((Vector4 *)(obj + 0x120))->x * local2[0];
        scaled2[1] = ((Vector4 *)(obj + 0x120))->y * local2[1];
        scaled2[2] = ((Vector4 *)(obj + 0x120))->z * local2[2];
        XformVecOrientation(obj + 0x10, scaled2, (f32 *)(obj + 0x130));
    }
    return 1;
}

u8 PhysicalObject_Move(u8 *obj) {
    typedef struct {
        u8 pad0[0x14];
        f32 friction;
        f32 restitution;
    } Material;
    typedef struct {
        u8 pad0[0x4];
        void (*stop)(u8 *obj);
        u8 pad8[0x14 - 0x8];
        f32 (*getMass)(u8 *obj);
        void (*applyImpulse)(u8 *obj, void *hit, s32 arg2, f32 impulse);
        u8 pad1C[0x28 - 0x1C];
        Material *(*getMaterial)(u8 *obj);
        u8 pad2C[0x30 - 0x2C];
        void (*getInverseInertia)(u8 *obj, Vector4 *out);
        void (*integrate)(u8 *obj, f32 dt);
    } VTable;
    typedef struct {
        Vector4 pos;
        Vector4 normal;
        u8 *thing;
        s32 unk24;
        Material *material;
        u8 pad2C[0x40 - 0x2C];
        u8 type;
    } CollisionInfo;
    extern f32 g_fDeltaTimeSec;
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    void InverseXformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
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

    if (!(*(u32 *)(obj + 0x74) & 0xD)) {
        elapsed = 0.0f;
        iter = 0;
        if (Timer_Interrupt300()) {
            inv = 1.0f / Sqrt(((Vector4 *)(obj + 0x50))->x * ((Vector4 *)(obj + 0x50))->x +
                              ((Vector4 *)(obj + 0x50))->y * ((Vector4 *)(obj + 0x50))->y +
                              ((Vector4 *)(obj + 0x50))->z * ((Vector4 *)(obj + 0x50))->z +
                              ((Vector4 *)(obj + 0x50))->w * ((Vector4 *)(obj + 0x50))->w);
            ((Vector4 *)(obj + 0x50))->x *= inv;
            ((Vector4 *)(obj + 0x50))->y *= inv;
            ((Vector4 *)(obj + 0x50))->z *= inv;
            ((Vector4 *)(obj + 0x50))->w *= inv;
        }
        do {
            dt = g_fDeltaTimeSec - elapsed;
            hitTerrain = Thing_HitsTerrain(obj, &dt, &hit);
            ((Vector4 *)(obj + 0x40))->x += ((Vector4 *)obj)->x * dt;
            ((Vector4 *)(obj + 0x40))->y += ((Vector4 *)obj)->y * dt;
            ((Vector4 *)(obj + 0x40))->z += ((Vector4 *)obj)->z * dt;
            *(u32 *)(obj + 0x74) |= 0x100;
            (SLOT_LOAD(VTable *, (obj + 0x8C)))->integrate(obj, dt);
            hit.thing = obj;
            hit.unk24 = 0;
            if (0.0f == dt) {
                (SLOT_LOAD(VTable *, (obj + 0x8C)))->stop(obj);
                return 0;
            }
            if ((s8)hitTerrain) {
                invMass = 1.0f / (SLOT_LOAD(VTable *, (obj + 0x8C)))->getMass(obj);
                vn = PSVECDotProduct(&hit.normal, (Vector4 *)hit.thing);
                e1 = (SLOT_LOAD(VTable *, (hit.thing + 0x8C)))->getMaterial(hit.thing)->restitution;
                e2 = hit.material->restitution;
                friction = 0.3f * hit.material->friction * (SLOT_LOAD(VTable *, (hit.thing + 0x8C)))->getMaterial(hit.thing)->friction;
                negVel.x = -((Vector4 *)obj)->x;
                negVel.y = -((Vector4 *)obj)->y;
                negVel.z = -((Vector4 *)obj)->z;
                if (PSVECSquareMag(&negVel) > 0.0f) {
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
                PSVECSubtract(&hit.pos, (Vector4 *)(hit.thing + 0x40), &r);
                PSVECCrossProduct(&r, &hit.normal, &rxn);
                (SLOT_LOAD(VTable *, (hit.thing + 0x8C)))->getInverseInertia(hit.thing, &invInertia);
                InverseXformVecOrientation(hit.thing + 0x10, &rxn, &local);
                scaled.x = invInertia.x * local.x;
                scaled.y = invInertia.y * local.y;
                scaled.z = invInertia.z * local.z;
                XformVecOrientation(hit.thing + 0x10, &scaled, &angVel);
                PSVECCrossProduct(&angVel, &r, &tmp);
                friction = PSVECDotProduct(&tmp, &hit.normal);
                friction = invMass * PSVECDotProduct(&hit.normal, &hit.normal) + friction;
                ((Vector4 *)(obj + 0x40))->x += 0.03f * hit.normal.x;
                ((Vector4 *)(obj + 0x40))->y += 0.03f * hit.normal.y;
                ((Vector4 *)(obj + 0x40))->z += 0.03f * hit.normal.z;
                (SLOT_LOAD(VTable *, (obj + 0x8C)))->applyImpulse(obj, &hit, 0, vn * -(e1 * e2 + 1.0f) / friction);
            }
            if (*(u32 *)(obj + 0x74) & 0x200000) {
                elapsed = g_fDeltaTimeSec;
            }
            if (iter++ > 6) {
                elapsed = g_fDeltaTimeSec;
            }
            elapsed += dt;
        } while (elapsed < g_fDeltaTimeSec);
    }
    return Destructible_Move(obj);
}

s32 Mover_PreMove(u8 *mover) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeSec;
    typedef struct {
        Quaternion rot;
        Vector4 pos;
    } KeyFrame;
    Matrix4 inv;
    Matrix4 rel;
    Matrix4 rot;
    KeyFrame start __attribute__((aligned(16)));
    KeyFrame key __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Quaternion quat;
    Quaternion delta;
    f32 angle;

    if (!(*(u32 *)(mover + 0x74) & 0x4000)) {
        *(f32 *)(mover + 0xDC) += *(f32 *)(mover + 0xD8) * g_fDeltaTimeSec;
        if (*(f32 *)(mover + 0xDC) > *(f32 *)(SLOT_LOAD(u8 *, (mover + 0xD4)) + 0xC)) {
            *(f32 *)(mover + 0xDC) -= *(f32 *)(SLOT_LOAD(u8 *, (mover + 0xD4)) + 0xC) - *(f32 *)(SLOT_LOAD(u8 *, (mover + 0xD4)) + 0x8);
            Gfx_AnimExtractKeyFrame(SLOT_LOAD(void *, (mover + 0xD4)), *(f32 *)(SLOT_LOAD(u8 *, (mover + 0xD4)) + 0x8), &start, 1);
            ((Matrix4 *)(mover + 0x10))->v[3].x = start.pos.x;
            ((Matrix4 *)(mover + 0x10))->v[3].y = start.pos.y;
            ((Matrix4 *)(mover + 0x10))->v[3].z = start.pos.z;
        }
        Gfx_AnimExtractKeyFrame(SLOT_LOAD(void *, (mover + 0xD4)), *(f32 *)(mover + 0xDC), &key, 1);
        quat = key.rot;
        pos.x = key.pos.x;
        pos.y = key.pos.y;
        pos.z = key.pos.z;
        if (*(u32 *)(mover + 0x74) & 4) {
            ((Vector4 *)mover)->x = (pos.x - ((Matrix4 *)(mover + 0x10))->v[3].x) / g_fDeltaTimeSec;
            ((Vector4 *)mover)->y = (pos.y - ((Matrix4 *)(mover + 0x10))->v[3].y) / g_fDeltaTimeSec;
            ((Vector4 *)mover)->z = (pos.z - ((Matrix4 *)(mover + 0x10))->v[3].z) / g_fDeltaTimeSec;
            inv = *(Matrix4 *)(mover + 0x10);
            Mover_SetRotation(mover, quat);
            QuaternionToSubMatrix(&quat, &rot);
            TransposeSubMatrix(&inv);
            MultiplySubMatrix(&inv, &rot, &rel);
            SubMatrixToQuaternion(&rel, &delta);
            GenerateSpinAngleFromQuaternion(&delta, (Vector4 *)(mover + 0x130), &angle);
            angle /= g_fDeltaTimeSec;
            *(f32 *)(mover + 0x130) *= angle;
            *(f32 *)(mover + 0x134) *= angle;
            *(f32 *)(mover + 0x138) *= angle;
        } else {
            QuaternionToSubMatrix(&quat, mover + 0x10);
            ((Matrix4 *)(mover + 0x10))->v[3] = pos;
            *(Quaternion *)(mover + 0x50) = quat;
            Mover_SetRotation(mover, quat);
        }
        *(u32 *)(mover + 0x74) &= ~0x10;
    } else {
        *(Vector4 *)mover = g_zeroVector;
        Mover_SetRotation(mover, *(Quaternion *)(mover + 0x50));
        *(Vector4 *)(mover + 0x130) = g_zeroVector;
    }
    return 1;
}

void PhysicalObject_DynamicRotation(u8 *arg0, f32 dt) {
    typedef struct OBox {
        Vector4 center;
        Vector4 corners[8];
        u8 pad90[0xD];
        u8 valid;
        u8 pad9E[2];
        void *row2;
        void *row0;
        void *row1;
        u8 padAC[0xF0 - 0xAC];
    } OBox;
    typedef struct RayHit {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[8];
        u8 *surface;
        u8 pad2C[0x24];
    } RayHit;
    typedef struct PhysProps {
        u8 pad0[0x10];
        u8 type;
        u8 pad11[0x20 - 0x11];
        f32 mass;
        u8 pad24[0x30 - 0x24];
        u8 *material;
        u8 pad34[0x44 - 0x34];
        void *box;
    } PhysProps;
    typedef struct PhysInfo {
        u8 pad0[0x10];
        PhysProps *props;
    } PhysInfo;
    typedef struct PhysObj {
        Vector4 vel;
        Matrix4 mtx;
        Quaternion quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0xC0 - 0x78];
        PhysInfo *info;
        u8 padC4[0xD0 - 0xC4];
        OBox *obox;
        u8 padD4[0x110 - 0xD4];
        Vector4 angMomentum;
        Vector4 invInertia;
        Vector4 angVel;
    } PhysObj;
    f32 MagnitudeVector(Vector4 *v);
    void XformVecOrientation(void *mtx, void *in, void *out);
    void InverseXformVecOrientation(void *mtx, void *in, void *out);
    s8 OBoxHitsTerrainAndObjectsZeroTime(void *obox, void *thing, void *info);
    OBox obox;
    RayHit hit;
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
    f32 scaled3[4];
    f32 local3[4];
    f32 t;
    f32 mag;
    u32 i;
    PhysObj *obj = (PhysObj *)arg0;
    void *box;
    OBox *old;
    s8 hitTerrain;
    f32 e1;
    f32 num;
    f32 j;

    t = dt;
    if (obj->flags & 8) {
        return;
    }
    if (obj->flags & 0x200000) {
        return;
    }
    InverseXformVecOrientation(&obj->mtx, &obj->angMomentum, local);
    scaled[0] = obj->invInertia.x * local[0];
    scaled[1] = obj->invInertia.y * local[1];
    scaled[2] = obj->invInertia.z * local[2];
    XformVecOrientation(&obj->mtx, scaled, &spin);
    axis = spin;
    mag = MagnitudeVector(&axis);
    if (mag > 0.0001f) {
        axis.x /= mag;
        axis.y /= mag;
        axis.z /= mag;
    }
    GenerateQuaternion(&axis, &q, mag * t);
    QuaternionMult((Vector4 *)&q, (Vector4 *)&obj->quat, (Vector4 *)&newQ);
    QuaternionToSubMatrix(&newQ, &mtx);
    mtx.v[3].x = obj->mtx.v[3].x;
    mtx.v[3].y = obj->mtx.v[3].y;
    mtx.v[3].z = obj->mtx.v[3].z;
    if (obj->flags & 2) {
        obj->quat = newQ;
        obj->mtx = mtx;
        return;
    }
    switch (obj->info->props->type) {
    case 0:
        box = obj->info->props->box;
        old = Destructible_GetOrientedCollision(obj);
        obox.valid = 1;
        BuildOOB(box, &mtx, &obox);
        for (i = 0; i < 8; i++) {
            if (t > 0.00005f) {
                dir.x = (obox.corners[i].x - old->corners[i].x) / dt;
                dir.y = (obox.corners[i].y - old->corners[i].y) / dt;
                dir.z = (obox.corners[i].z - old->corners[i].z) / dt;
                hitTerrain = 0;
                obj->flags |= 2;
                if (!(obj->flags & 0x40000)) {
                    hitTerrain = RayHitsTerrain_Aux3(&old->corners[i], &dir, &t, &hit);
                }
                obj->flags &= ~2;
                if (hitTerrain) {
                    e1 = *(f32 *)(hit.surface + 0x18);
                    vel = dir;
                    PSVECSubtract(&hit.pos, &obj->mtx.v[3], &r);
                    num = -(e1 * *(f32 *)(obj->info->props->material + 0x18) + 1.0f) * PSVECDotProduct(&vel, &hit.normal);
                    PSVECCrossProduct(&r, &hit.normal, &rxn);
                    InverseXformVecOrientation(&obj->mtx, &rxn, bLocal);
                    bScaled[0] = obj->invInertia.x * bLocal[0];
                    bScaled[1] = obj->invInertia.y * bLocal[1];
                    bScaled[2] = obj->invInertia.z * bLocal[2];
                    XformVecOrientation(&obj->mtx, bScaled, &angVel);
                    PSVECCrossProduct(&angVel, &r, &tmp);
                    j = num / (1.0f / obj->info->props->mass + PSVECDotProduct(&tmp, &hit.normal));
                    impulse.x = j * hit.normal.x;
                    impulse.y = j * hit.normal.y;
                    impulse.z = j * hit.normal.z;
                    obj->vel.x += impulse.x / obj->info->props->mass;
                    obj->vel.y += impulse.y / obj->info->props->mass;
                    obj->vel.z += impulse.z / obj->info->props->mass;
                    obj->mtx.v[3].x += 0.03f * hit.normal.x;
                    obj->mtx.v[3].y += 0.03f * hit.normal.y;
                    obj->mtx.v[3].z += 0.03f * hit.normal.z;
                    PSVECCrossProduct(&r, &impulse, &torque);
                    obj->angMomentum.x += torque.x;
                    obj->angMomentum.y += torque.y;
                    obj->angMomentum.z += torque.z;
                    InverseXformVecOrientation(&obj->mtx, &obj->angMomentum, local3);
                    scaled3[0] = obj->invInertia.x * local3[0];
                    scaled3[1] = obj->invInertia.y * local3[1];
                    scaled3[2] = obj->invInertia.z * local3[2];
                    XformVecOrientation(&obj->mtx, scaled3, &obj->angVel);
                }
            }
        }
        if (!OBoxHitsTerrainAndObjectsZeroTime(&obox, obj, NULL)) {
            obj->quat = newQ;
            obj->mtx = mtx;
            *obj->obox = obox;
            obj->obox->row2 = &obj->mtx.v[2];
            obj->obox->row0 = &obj->mtx.v[0];
            obj->obox->row1 = &obj->mtx.v[1];
            obj->flags &= ~0x180;
        }
        break;
    case 1:
    case 6:
        break;
    }
}

void Mover_DynamicRotation(u8 *mover) {
    typedef struct Mover {
        u8 pad0[0x10];
        Matrix4 mtx;
        Quaternion quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0xE0 - 0x78];
        Quaternion rot;
    } Mover;
    Mover *m = (Mover *)mover;
    Quaternion quat __attribute__((aligned(16)));
    Matrix4 mtx;

    if (!(m->flags & 0x10)) {
        m->flags |= 0x10;
        quat = m->rot;
        QuaternionToSubMatrix(&quat, &mtx);
        mtx.v[3].x = m->mtx.v[3].x;
        mtx.v[3].y = m->mtx.v[3].y;
        mtx.v[3].z = m->mtx.v[3].z;
        m->quat = quat;
        m->mtx = mtx;
    }
}

void PhysicalObject_DynamicCollideImpact(u8 *obj, u8 *collision, f32 force) {
    extern Vector4 g_zeroVector;
    void ProjectVectorToNormal(void *normal, void *v);
    void XformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    void InverseXformVecOrientation(void *mtx, Vector4 *in, Vector4 *out);
    typedef struct {
        Vector4 center;
        Vector4 corners[8];
    } OBox;
    struct { Vector4 pos; Vector4 vel; Vector4 spin; Vector4 quat; void *info; u32 unk44; u32 unk48; u32 unk4C; u32 unk50; } params __attribute__((aligned(16)));
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
    u8 *other;
    u32 i;
    u8 *hull;
    f32 weight;
    f32 d;
    f32 w;
    f32 inv;
    f32 mag;

    normal = *(Vector4 *)(collision + 0x10);
    pos = *(Vector4 *)collision;
    scale = force / *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x20);
    if (*(u32 *)(obj + 0x74) & 0x200000) {
        return;
    }
    if ((*(u32 *)(obj + 0x74) & 0x100000) && normal.y > 0.9f && _fabs(scale) < 2.0f &&
        ((other = SLOT_LOAD(u8 *, (collision + 0x24))) == NULL || (*(u32 *)(other + 0x74) & 8))) {
        Vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};

        weight = 0.0f;
        center = zero;
        Create_Plane(&plane, &normal, (Vector4 *)collision);
        switch ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)))[0x10]) {
        case 0:
            for (i = 0; i < 8; i++) {
                d = Distance_Plane(&plane, &(SLOT_LOAD(OBox *, (obj + 0xD0)))->corners[i]);
                if (d < 0.15f) {
                    w = (0.15f - d) / 0.15f;
                    center.x += (SLOT_LOAD(OBox *, (obj + 0xD0)))->corners[i].x * w;
                    center.y += (SLOT_LOAD(OBox *, (obj + 0xD0)))->corners[i].y * w;
                    center.z += (SLOT_LOAD(OBox *, (obj + 0xD0)))->corners[i].z * w;
                    weight += w;
                }
            }
            break;
        case 1:
        case 6:
            hull = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (obj + 0xC0)) + 0x10)) + 0x48));
            for (i = 0; i < *(u32 *)(hull + 0xC); i++) {
                d = Distance_Plane(&plane, &(SLOT_LOAD(Vector4 *, (obj + 0xCC)))[i]);
                if (d < 0.15f) {
                    w = (0.15f - d) / 0.15f;
                    center.x += w * (SLOT_LOAD(Vector4 *, (obj + 0xCC)))[i].x;
                    center.y += w * (SLOT_LOAD(Vector4 *, (obj + 0xCC)))[i].y;
                    center.z += w * (SLOT_LOAD(Vector4 *, (obj + 0xCC)))[i].z;
                    weight += w;
                }
            }
            break;
        }
        if (weight >= 3.0f) {
            ProjectVectorToNormal(collision + 0x10, obj + 0x110);
            InverseXformVecOrientation(obj + 0x10, (Vector4 *)(obj + 0x110), &local1);
            scaled1.x = ((Vector4 *)(obj + 0x120))->x * local1.x;
            scaled1.y = ((Vector4 *)(obj + 0x120))->y * local1.y;
            scaled1.z = ((Vector4 *)(obj + 0x120))->z * local1.z;
            XformVecOrientation(obj + 0x10, &scaled1, (Vector4 *)(obj + 0x130));
            if (PSVECSquareMag(obj + 0x130) < 1.0f && PSVECSquareMag(obj) < 25.0f) {
                params.pos = *(Vector4 *)(obj + 0x40);
                params.unk50 = 0;
                params.unk4C = 0;
                params.vel = g_zeroVector;
                params.info = SLOT_LOAD(void *, (obj + 0xC0));
                params.quat = *(Vector4 *)(obj + 0x50);
                params.spin = *(Vector4 *)(obj + 0x130);
                ThingManager_DARYL_CreateThing(3, &params);
                Destructible_Release(obj);
            }
        }
    }
    ((Vector4 *)obj)->x += normal.x * scale;
    ((Vector4 *)obj)->y += normal.y * scale;
    ((Vector4 *)obj)->z += normal.z * scale;
    PSVECSubtract(&pos, (Vector4 *)(obj + 0x40), &r);
    PSVECScale(&normal, &impulse, force);
    PSVECCrossProduct(&r, &impulse, &torque);
    ((Vector4 *)(obj + 0x110))->x += torque.x;
    ((Vector4 *)(obj + 0x110))->y += torque.y;
    ((Vector4 *)(obj + 0x110))->z += torque.z;
    InverseXformVecOrientation(obj + 0x10, (Vector4 *)(obj + 0x110), &local2);
    angVel.x = ((Vector4 *)(obj + 0x120))->x * local2.x;
    angVel.y = ((Vector4 *)(obj + 0x120))->y * local2.y;
    angVel.z = ((Vector4 *)(obj + 0x120))->z * local2.z;
    mag = PSVECSquareMag(&angVel);
    if (mag > 10000.0f) {
        inv = 100.0f / Sqrt(mag);
        angVel.x *= inv;
        angVel.y *= inv;
        angVel.z *= inv;
        local2.x = angVel.x / ((Vector4 *)(obj + 0x120))->x;
        local2.y = angVel.y / ((Vector4 *)(obj + 0x120))->y;
        local2.z = angVel.z / ((Vector4 *)(obj + 0x120))->z;
        XformVecOrientation(obj + 0x10, &local2, (Vector4 *)(obj + 0x110));
    }
    InverseXformVecOrientation(obj + 0x10, (Vector4 *)(obj + 0x110), &local3);
    scaled3.x = ((Vector4 *)(obj + 0x120))->x * local3.x;
    scaled3.y = ((Vector4 *)(obj + 0x120))->y * local3.y;
    scaled3.z = ((Vector4 *)(obj + 0x120))->z * local3.z;
    XformVecOrientation(obj + 0x10, &scaled3, (Vector4 *)(obj + 0x130));
}

void PhysicalObject_GetInverseLocalInertiaTensor(void *obj, Quaternion *out) {
    *out = *(Quaternion *)((u8 *)obj + 0x120);
}

f32 PhysicalObject_CollideWithRay(u8 *obj, Vector4 *start, Vector4 *end, Vector4 *hit, f32 maxDist) {
    Vector4 cross __attribute__((aligned(16)));
    Vector4 offset __attribute__((aligned(16)));
    Vector4 angVel __attribute__((aligned(16)));
    f32 dist;

    dist = PhysicalProperties_CollideWithRay(SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(void *, (u8 *)(obj) + 0xC0)) + 0x10), obj + 0x10, start, end, maxDist, hit);
    if (hit != NULL && dist < maxDist) {
        angVel = *(Vector4 *)(obj + 0x130);
        PSVECSubtract(hit, (Vector4 *)(obj + 0x40), &offset);
        PSVECCrossProduct(&angVel, &offset, &cross);
        PSVECAdd((Vector4 *)obj, &cross, (Vector4 *)((u8 *)hit + 0x30));
    }
    return dist;
}
void Thing_GetCameraOrientationInfo();
void Thing_SlaveTransform();

void *g_physicalObjectVTable[] = {
    PhysicalObject_Init,
    Destructible_Release,
    PhysicalObject_PreMove,
    PhysicalObject_Move,
    Destructible_Draw,
    Destructible_GetMass,
    PhysicalObject_DynamicCollideImpact,
    Thing_GetCameraOrientationInfo,
    Thing_GetInterfaceInfo,
    Thing_SlaveTransform,
    Destructible_GetSurfaceScript,
    Thing_GetNearestNavPoint,
    PhysicalObject_GetInverseLocalInertiaTensor,
    PhysicalObject_DynamicRotation,
    PhysicalObject_CollideWithRay,
    Thing_GetTargetPos,
    Destructible_GetOrientedCollision,
};

void *g_moverVTable[] = {
    Mover_Init,
    Destructible_Release,
    Mover_PreMove,
    Destructible_Move,
    Destructible_Draw,
    Destructible_GetMass,
    Destructible_DynamicCollideImpact,
    Thing_GetCameraOrientationInfo,
    Thing_GetInterfaceInfo,
    Thing_SlaveTransform,
    Destructible_GetSurfaceScript,
    Thing_GetNearestNavPoint,
    Thing_GetInverseLocalInertiaTensor,
    Mover_DynamicRotation,
    PhysicalObject_CollideWithRay,
    Thing_GetTargetPos,
    Destructible_GetOrientedCollision,
};
