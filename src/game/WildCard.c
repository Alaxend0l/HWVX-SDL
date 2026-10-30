#include "common.h"



s32 WildCardRack_Init(void *thing, void *data) {
    typedef struct WildCardSlot {
        PTR32(void) card;
        u8 pad4[0x40 - 0x4];
        u16 unk40;
        u16 unk42;
        u8 pad44[0x48 - 0x44];
        f32 unk48;
        s32 unk4C;
    } WildCardSlot;
    typedef struct WildCardPos {
        s32 v[4];
    } WildCardPos;
    typedef struct WildCardRack {
        u8 pad0[0x40];
        WildCardPos pos;
        u8 pad50[0xB0 - 0x50];
        WildCardSlot slots[12];
        u8 pad470;
        u8 pad471;
        u8 unk472;
        u8 pad473[0x478 - 0x473];
        s32 unk478;
        f32 unk47C;
    } WildCardRack;
    WildCardRack *rack = thing;
    WildCardPos *init = data;
    u32 i;

    Thing_Init(rack, 0);
    rack->pos = *init;
    rack->pad470 = 0;
    rack->unk472 = 0;
    for (i = 0; i < 12; i++) {
        rack->slots[i].unk40 = 0;
        SETP(rack->slots[i].card, NULL);
        rack->slots[i].unk42 = 0;
        rack->slots[i].unk48 = 0.0f;
        rack->slots[i].unk4C = 0;
    }
    rack->unk478 = 0;
    rack->unk47C = -1.0f;
    return 0;
}

void WildCardRack_Release(s32 arg0) {
    s32 var_r31;
    u32 var_r30;

    var_r30 = 0U;
    var_r31 = arg0;
    do {
        MotionState_Release(var_r31 + 0xB0);
        var_r30 += 1;
        var_r31 += 0x50;
    } while (var_r30 < 0xCU);
    Thing_Release(arg0);
}

s32 WildCardRack_Move(void *thing) {
    extern f32 g_fDeltaTimeSec;
    void InverseXformVec(void *mtx, Vector4 *in, Vector4 *out);
    void WildcardState_Heartbeat(void *state, u8 *mtx, void *thing);
    typedef void (*GetTargetPosFn)(void *thing, Vector4 *pos);
    typedef struct WildcardData {
        u8 unk0[0x24];
        u32 numFrames;
        s32 unk28;
        u8 spinType;
        u8 spinLimit;
        u16 spinBone;
    } WildcardData;
    typedef struct WildcardThing {
        u8 unk0[0x30];
        PTR32(WildcardData) data;
    } WildcardThing;
    typedef struct WildcardAnim {
        u8 unk0[0x1E];
        u8 loop;
        u8 unk1F[0x24 - 0x1F];
        PTR32(f32) times;
    } WildcardAnim;
    typedef struct WildcardInfo {
        u8 unk0[0x18];
        f32 unk18;
    } WildcardInfo;
    typedef struct WildcardSlot {
        PTR32(WildcardInfo) info;
        u8 unk4[0x10 - 0x4];
        PTR32(Matrix4) matrices;
        u8 unk14[0x18 - 0x14];
        PTR32(WildcardAnim) anim;
        u8 mode;
        u8 state;
        u8 unk1E[0x20 - 0x1E];
        f32 time;
        u8 unk24[0x40 - 0x24];
        s16 count;
        u8 unk42[0x44 - 0x42];
        f32 scale;
        f32 angle;
        PTR32(WildcardThing) thing;
    } WildcardSlot;
    typedef struct WildcardRack {
        u8 unk0[0x10];
        Vector4 mtx[3];
        Vector4 pos;
        Vector4 quat;
        u8 unk60[0x74 - 0x60];
        u32 flags;
        u8 unk78[0x94 - 0x78];
        PTR32(u8) player;
        u8 unk98[0xB0 - 0x98];
        WildcardSlot slots[12];
        u8 cur;
        u8 unk471;
        u8 unk472;
        u8 unk473[0x478 - 0x473];
        PTR32(u8) target;
        f32 unk47C;
    } WildcardRack;
    void *targets[32];
    Matrix4 mtx;
    Vector4 targetPos __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Vector4 local2 __attribute__((aligned(16)));
    Vector4 pos2 __attribute__((aligned(16)));
    Vector4 q __attribute__((aligned(16)));
    Vector4 out __attribute__((aligned(16)));
    f32 t;
    WildcardSlot *slot;
    WildcardRack *rack = thing;
    u8 *car;

    if (GETP(rack->player) != NULL) {
        car = SLOT_LOAD(u8 *, (GETP(rack->player) + 0x2C));
        if (car != NULL && SLOT_LOAD(u8 *, (car + 0x8C)) == g_carAVTable && ThingManagerList_GetByType(car + 0x344, 4) != NULL) {
            return 1;
        }
    }
    if (GETP(rack->player) != NULL && !(rack->flags & 0x200000)) {
        if (GETP(rack->slots[rack->cur].thing) != NULL) {
            s8 ready = 0;

            if (GETP(rack->slots[rack->cur].info) != NULL && rack->slots[rack->cur].scale > 0.6f) {
                if (GETP(rack->slots[rack->cur].anim)->loop) {
                    ready = rack->slots[rack->cur].mode == 2;
                } else {
                    ready = rack->slots[rack->cur].state > 1;
                }
            }
            if (ready &&
                ((u32)rack->slots[rack->cur].count >= GETP(GETP(rack->slots[rack->cur].thing)->data)->numFrames || GameSettings_IsCheatEnabled(0)) &&
                !rack->unk472 && GETP(rack->player)[0x1B]) {
                rack->slots[rack->cur].state = 0;
                if (GameSettings_IsCheatEnabled(0) ||
                    rack->slots[rack->cur].count >= (s16)GETP(GETP(rack->slots[rack->cur].thing)->data)->numFrames) {
                    if (!GameSettings_IsCheatEnabled(0)) {
                        rack->slots[rack->cur].count -= (s16)GETP(GETP(rack->slots[rack->cur].thing)->data)->numFrames;
                    } else {
                        rack->slots[rack->cur].count = GETP(GETP(rack->slots[rack->cur].thing)->data)->unk28;
                    }
                    rack->slots[rack->cur].time = GETP(GETP(rack->slots[rack->cur].anim)->times)[1];
                    rack->slots[rack->cur].mode = 1;
                }
            }
        }
        if (*(u32 *)(GETP(rack->player) + 0xC) & 2) {
            u8 old = rack->cur;

            slot = &rack->slots[old];
            if (GETP(slot->info) == NULL || slot->mode == 2 || slot->time > *GETP(GETP(slot->anim)->times) + GETP(slot->info)->unk18) {
                while (++rack->cur <= 24 && GETP(rack->slots[rack->cur % 12].info) == NULL) {
                }
                rack->cur = rack->cur % 12;
            }
            if (old != rack->cur) {
                rack->slots[rack->cur].scale = 0.0f;
            }
        }
    }
    if (GETP(rack->slots[rack->cur].thing) != NULL) {
        u8 type;

        rack->slots[rack->cur].scale += 6.0f * g_fDeltaTimeSec;
        if (rack->slots[rack->cur].scale > 1.0f) {
            rack->slots[rack->cur].scale = 1.0f;
        }
        type = GETP(GETP(rack->slots[rack->cur].thing)->data)->spinType;
        if (type == 3) {
            if (GETP(rack->target) != NULL) {
                if (*(s32 *)(GETP(rack->target) + 0x78) < 0 || (*(u32 *)(GETP(rack->target) + 0x74) & 0x1000)) {
                    SETP(rack->target, NULL);
                    rack->unk47C = -1.0f;
                } else {
                    (*(GetTargetPosFn *)(SLOT_LOAD(u8 *, (GETP(rack->target) + 0x8C)) + 0x3C))(GETP(rack->target), &targetPos);
                    PSVECSubtract(&targetPos, &rack->pos, &rack->mtx[2]);
                    PSVECCrossProduct(&rack->mtx[1], &rack->mtx[2], &rack->mtx[0]);
                    PSVECCrossProduct(&rack->mtx[2], &rack->mtx[0], &rack->mtx[1]);
                    if (PSVECSquareMag(&rack->mtx[2])) {
                        PSVECNormalize(&rack->mtx[2], &rack->mtx[2]);
                    }
                    if (PSVECSquareMag(&rack->mtx[1])) {
                        PSVECNormalize(&rack->mtx[1], &rack->mtx[1]);
                    }
                    if (PSVECSquareMag(&rack->mtx[0])) {
                        PSVECNormalize(&rack->mtx[0], &rack->mtx[0]);
                    }
                    SubMatrixToQuaternion(&rack->mtx[0], &rack->quat);
                }
            }
        } else if (type == 2) {
            f32 step;
            f32 angle;

            slot = &rack->slots[rack->cur];
            step = 0.9f * g_fDeltaTimeSec;
            angle = 0.0f;
            if (Timer_Interrupt300()) {
                u32 i;
                u32 count;
                u32 best;
                f32 bestDist;
                f32 dist;

                count = GetAllTargets(targets);
                bestDist = 999999.875f;
                best = -1;
                if (GETP(rack->target) != NULL && rack->unk47C > 0.0f) {
                    targets[count++] = GETP(rack->target);
                }
                for (i = 0; i < count; i++) {
                    t = 1.0f;
                    if (targets[i] != rack && (GETP(rack->player) == NULL || targets[i] != SLOT_LOAD(void *, (GETP(rack->player) + 0x2C)))) {
                        (*(GetTargetPosFn *)(SLOT_LOAD(u8 *, ((u8 *)targets[i] + 0x8C)) + 0x3C))(targets[i], &pos);
                        InverseXformVec(&rack->mtx[0], &pos, &local);
                        dist = _fabs(local.z * (local.x * local.y));
                        if (!(local.z < 1.0f) && !(_fabs(local.x / local.z) > 1.0f) && !RayHitsTerrain(&rack->pos, &pos, &t, NULL) &&
                            dist < bestDist) {
                            bestDist = dist;
                            best = i;
                        }
                    }
                }
                if (best < count) {
                    SETP(rack->target, targets[best]);
                } else {
                    SETP(rack->target, NULL);
                }
            }
            if (GETP(rack->target) != NULL) {
                mtx = *(Matrix4 *)&rack->mtx[0];
                mtx.v[3] = GETP(rack->slots[rack->cur].matrices)[GETP(GETP(rack->slots[rack->cur].thing)->data)->spinBone].v[3];
                (*(GetTargetPosFn *)(SLOT_LOAD(u8 *, (GETP(rack->target) + 0x8C)) + 0x3C))(GETP(rack->target), &pos2);
                InverseXformVec(&mtx, &pos2, &local2);
                angle = -atan2(local2.y, local2.z);
            }
            if (FloatsEqual(angle, slot->angle, step)) {
                slot->angle = angle;
            } else if (slot->angle > angle) {
                slot->angle -= step;
            } else {
                slot->angle += step;
            }
            if (slot->angle > 0.017453292f * GETP(GETP(slot->thing)->data)->spinLimit) {
                slot->angle = 0.017453292f * GETP(GETP(slot->thing)->data)->spinLimit;
            } else if (slot->angle < 0.017453292f * -GETP(GETP(slot->thing)->data)->spinLimit) {
                slot->angle = 0.017453292f * -GETP(GETP(slot->thing)->data)->spinLimit;
            }
        } else if (type == 1) {
            if (GETP(rack->player) != NULL && *(s8 *)(GETP(rack->player) + 1) != 0) {
                WildcardSlot *s = &rack->slots[rack->cur];

                s->angle += 0.017453292f * (*(s8 *)(GETP(rack->player) + 1) * g_fDeltaTimeSec);
                if (s->angle > 0.017453292f * GETP(GETP(s->thing)->data)->spinLimit) {
                    s->angle = 0.017453292f * GETP(GETP(s->thing)->data)->spinLimit;
                } else if (s->angle < 0.017453292f * -GETP(GETP(s->thing)->data)->spinLimit) {
                    s->angle = 0.017453292f * -GETP(GETP(s->thing)->data)->spinLimit;
                }
            }
        }
        if (GETP(rack->slots[rack->cur].info) != NULL && rack->slots[rack->cur].scale < 1.0f) {
            Vector4 axis __attribute__((aligned(16))) = {1.0f, 0.0f, 0.0f, 0.0f};

            GenerateQuaternion(&axis, &q, 3.1415927f * (rack->slots[rack->cur].scale - 1.0f));
            QuaternionMult(&rack->quat, &q, &out);
            QuaternionToSubMatrix(&out, &rack->mtx[0]);
        }
        WildcardState_Heartbeat(&rack->slots[rack->cur], (u8 *)&rack->mtx[0], rack);
    }
    return 1;
}

extern Matrix4 g_identity;
void WildCardRack_Draw(u8 *rack) {
    typedef struct {
        PTR32(void) motion;
        u8 pad4[0x40];
        f32 scale;
        u8 pad48[0x8];
    } RackState;
    typedef struct {
        u8 pad0[0xB0];
        RackState states[1];
    } Rack;
    Matrix4 tmp;
    Matrix4 scale;
    Matrix4 trans;
    Matrix4 out;
    f32 s;

    if (GETP(((RackState *)(rack + 0xB0))[rack[0x470]].motion) != NULL) {
        QuaternionToSubMatrix(rack + 0x50, rack + 0x10);
        s = ((RackState *)(rack + 0xB0))[rack[0x470]].scale;
        if (s < 1.0f) {
            scale = g_identity;
            trans = g_identity;
            scale.v[2].z = s;
            scale.v[1].y = s;
            scale.v[0].x = s;
            tmp = g_identity;
            scale.v[3].x = *(f32 *)(rack + 0x40);
            scale.v[3].y = *(f32 *)(rack + 0x44);
            scale.v[3].z = *(f32 *)(rack + 0x48);
            trans.v[3].x = -*(f32 *)(rack + 0x40);
            trans.v[3].y = -*(f32 *)(rack + 0x44);
            trans.v[3].z = -*(f32 *)(rack + 0x48);
            MultiplyMatrix(&trans, &scale, &out);
            MotionState_Draw2(&((Rack *)rack)->states[rack[0x470]], HND(&out), NULL);
        } else {
            MotionState_Draw(&((Rack *)rack)->states[rack[0x470]]);
        }
    }
}

void WildCardRack_JettisonAll(void *rack) {
    u32 i;

    if (rack != NULL) {
        for (i = 0; i < 12; i++) {
            WildCardRack_Jettison(rack, i);
        }
    }
}

void WildCardRack_Jettison(void *thing, u8 slot) {
    typedef struct WildCardSlot {
        PTR32(void) card;
        u8 pad4[0x44 - 0x4];
        f32 unk44;
        u8 pad48[0x4C - 0x48];
        PTR32(void) unk4C;
    } WildCardSlot;
    typedef struct WildCardRack {
        u8 pad0[0xB0];
        WildCardSlot slots[12];
        u8 unk470;
    } WildCardRack;
    WildCardRack *rack = thing;
    u32 i;

    if (GETP(rack->slots[slot].card) != NULL) {
        SETP(rack->slots[slot].unk4C, NULL);
        SETP(rack->slots[slot].card, NULL);
        for (i = 0; i < 12; i++) {
            if (GETP(rack->slots[i].unk4C) != NULL && GETP(rack->slots[i].card) != NULL) {
                rack->unk470 = i;
                rack->slots[i].unk44 = 0.0f;
            }
        }
    }
}

void WildCardRack_Add(void *thing, u8 *item, s16 count) {
    typedef struct WildCard {
        u8 pad0[0x1E];
        u8 unk1E;
        u8 pad1F[0x24 - 0x1F];
        PTR32(f32) unk24;
        PTR32(f32) unk28;
    } WildCard;
    typedef struct WildCardData {
        PTR32(WildCard) card;
        u8 pad4[0x28 - 0x4];
        s32 max;
    } WildCardData;
    typedef struct WildCardItem {
        u8 pad0[0x30];
        PTR32(WildCardData) data;
    } WildCardItem;
    typedef struct WildCardSlot {
        PTR32(WildCard) card;
        u8 pad4[0x1C - 0x4];
        u8 unk1C;
        u8 unk1D;
        u8 pad1E[0x20 - 0x1E];
        f32 unk20;
        f32 unk24;
        u8 pad28[0x40 - 0x28];
        s16 count;
        u8 pad42[0x44 - 0x42];
        f32 unk44;
        f32 unk48;
        PTR32(WildCardItem) item;
    } WildCardSlot;
    typedef struct WildCardRack {
        u8 pad0[0x74];
        u32 flags;
        u8 pad78[0xB0 - 0x78];
        WildCardSlot slots[12];
        u8 unk470;
    } WildCardRack;
    WildCardRack *rack = thing;
    WildCardItem *it = (WildCardItem *)item;
    s8 found = 0;
    s8 slot = rack->unk470;
    u32 i;
    WildCardSlot *s;

    if (rack->flags & 0x10) {
        if (GETP(rack->slots[rack->unk470].item) != NULL) {
            if (GETP(GETP(rack->slots[rack->unk470].item)->data) == GETP(it->data)) {
                if (rack->slots[rack->unk470].count == 0) {
                    rack->slots[rack->unk470].unk1C = 1;
                    rack->slots[rack->unk470].unk20 = GETP(GETP(rack->slots[rack->unk470].card)->unk24)[1];
                }
                rack->slots[rack->unk470].count += (s16)count;
                if (rack->slots[rack->unk470].count > (s16)GETP(it->data)->max) {
                    rack->slots[rack->unk470].count = GETP(it->data)->max;
                }
                found = 1;
            } else {
                WildCardRack_Jettison(thing, rack->unk470);
            }
        }
    } else {
        for (i = 0; i < 12; i++) {
            if (GETP(rack->slots[i].item) != NULL && GETP(GETP(rack->slots[i].item)->data) == GETP(it->data)) {
                if (rack->slots[i].count == 0) {
                    rack->slots[i].unk1C = 1;
                    rack->slots[i].unk20 = GETP(GETP(rack->slots[i].card)->unk24)[1];
                }
                rack->slots[i].count += (s16)count;
                if (rack->slots[i].count > (s16)GETP(it->data)->max) {
                    rack->slots[i].count = GETP(it->data)->max;
                }
                found = 1;
                if (rack->unk470 != i) {
                    rack->unk470 = i;
                    rack->slots[i].unk44 = 0.0f;
                }
                break;
            }
            if (GETP(rack->slots[i].item) == NULL || GETP(rack->slots[i].card) == NULL) {
                slot = i;
            }
        }
    }
    if (!found) {
        SETP(rack->slots[slot].card, GETP(GETP(it->data)->card));
        s = &rack->slots[slot];
        rack->slots[slot].count = count;
        if (rack->slots[slot].count > (s16)GETP(it->data)->max) {
            rack->slots[slot].count = GETP(it->data)->max;
        }
        SETP(rack->slots[slot].item, it);
        s->unk24 = 0.0f;
        MotionState_Init(s, 0);
        if (GETP(s->card) != NULL) {
            if (GETP(s->card)->unk1E) {
                rack->slots[slot].unk20 = *GETP(GETP(s->card)->unk28);
                rack->slots[slot].unk1C = 2;
            } else {
                rack->slots[slot].unk20 = GETP(GETP(s->card)->unk24)[1];
            }
            rack->slots[slot].unk1D = 1;
            rack->slots[slot].unk44 = 0.0f;
            rack->slots[slot].unk48 = 0.0f;
            rack->unk470 = slot;
        }
    }
}

s8 Thing_HasThing(u8 *thing, void *target);
s32 TriggerItemPickup(u32 *trigger) {
    typedef struct WildCardSlot {
        u8 pad0[0x4C];
        PTR32(u8) item;
    } WildCardSlot;
    typedef struct WildCardRack {
        u8 pad0[0xB0];
        WildCardSlot slots[12];
    } WildCardRack;
    u32 i;
    u32 j;
    u32 numItems;
    u8 *item;
    u32 found;
    u8 *thing;
    WildCardRack *rack;
    u32 k;

    numItems = trigger[0] + 1;
    for (i = 0; i < trigger[0]; i++) {
        item = (u8 *)PTR(trigger[i + 1]);
        for (j = 0; j < trigger[numItems]; j++) {
            found = 0;
            thing = SLOT_LOAD(u8 *, (trigger[j + numItems + 1] + 0x28));
            if (thing != NULL) {
                if (Thing_HasThing(thing, SLOT_LOAD(void *, (item + 0x24)))) {
                    return 1;
                }
                rack = ThingManagerList_GetByType(thing + 0x344, 8);
                for (k = 0; rack != NULL && k < 12; k++) {
                    if (GETP(rack->slots[k].item) != NULL &&
                        *(u32 *)(SLOT_LOAD(u8 *, (item + 0x1C)) + 0x30) == *(u32 *)(GETP(rack->slots[k].item) + 0x30)) {
                        found = 1;
                    }
                }
                if (found) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

s32 WildCardRack_Invalidate(u8 *rack, u32 start, u32 end) {
    u32 i;
    u32 addr;

    for (i = 0; i < 12; i++) {
        addr = *(u32 *)(rack + i * 0x50 + 0xFC);
        if (addr >= start && addr < end) {
            return 1;
        }
    }
    return 0;
}

void Thing_GetCameraOrientationInfo();
void Thing_SlaveTransform();

void *g_wildCardRackVTable[] = {
    WildCardRack_Init,
    WildCardRack_Release,
    Thing_PreMove,
    WildCardRack_Move,
    WildCardRack_Draw,
    Thing_GetMass,
    Thing_DynamicCollideImpact,
    Thing_GetCameraOrientationInfo,
    Thing_GetInterfaceInfo,
    Thing_SlaveTransform,
    Thing_GetSurfaceScript,
    Thing_GetNearestNavPoint,
    Thing_GetInverseLocalInertiaTensor,
    Thing_DynamicRotation,
    Thing_CollideWithRay,
    Thing_GetTargetPos,
    Thing_GetOrientedCollision,
};

