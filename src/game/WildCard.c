/* Wildcard racks: the slave thing on a car (or looker) holding the wildcard
 * weapons it picked up, one animated model per slot; it pops the selected
 * one up, aims it and fires it. */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "WildCard.h"

enum {
    RACK_FLAG_ONE_SLOT = 0x10,  /* WildCardRack.flags: only the current slot */
};

typedef void (*GetTargetPosFn)(void *thing, Vector4 *pos);

static inline WeaponInfo *WildCardSlot_Weapon(WildCardSlot *slot) {
    return GETP(GETP(slot->item)->weapon);
}

static inline MotionAnim *WildCardSlot_Model(WildCardSlot *slot) {
    return (MotionAnim *)GETP(slot->motion.model);
}

s32 WildCardRack_Init(WildCardRack *rack, Vector4 *pos) {
    u32 i;

    Thing_Init(&rack->base, 0);
    rack->base.pos = *pos;
    rack->cur = 0;
    rack->noFire = 0;
    for (i = 0; i < WILDCARD_SLOTS; i++) {
        rack->slots[i].ammo = 0;
        SETP(rack->slots[i].motion.model, NULL);
        rack->slots[i].unk42 = 0;
        rack->slots[i].angle = 0.0f;
        SETP(rack->slots[i].item, NULL);
    }
    SETP(rack->target, NULL);
    rack->unk47C = -1.0f;
    return 0;
}

void WildCardRack_Release(void *arg) {
    WildCardRack *rack = arg;
    u32 i;

    for (i = 0; i < WILDCARD_SLOTS; i++) {
        MotionState_Release(&rack->slots[i].motion);
    }
    Thing_Release(arg);
}

/* Fire the selected weapon when ready, switch weapons, pop the selected one
 * up and aim it by its aim mode, then animate it. */
s32 WildCardRack_Move(WildCardRack *rack) {
    extern f32 g_fDeltaTimeSec;
    Thing *thing = &rack->base;
    Player *player = GETP(thing->player);
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
    WildCardSlot *slot;
    Thing *car;

    /* a car carrying the special item (type 4 slave) can't use wildcards */
    if (player != NULL) {
        car = GETP(player->vehicle);
        if (car != NULL && (u8 *)GETP(car->vtable) == g_carAVTable &&
            ThingManagerList_GetByType(&((CarA *)car)->motionState.slaves, 4) != NULL) {
            return 1;
        }
    }
    if (player != NULL && !(thing->flags & THING_FLAG_RELEASED)) {
        slot = &rack->slots[rack->cur];
        if (GETP(slot->item) != NULL) {
            s8 ready = 0;

            if (WildCardSlot_Model(slot) != NULL && slot->scale > 0.6f) {
                if (GETP(slot->motion.layer.anim)->hasOutro) {
                    ready = slot->motion.layer.phase == 2;
                } else {
                    ready = slot->motion.layer.loops > 1;
                }
            }
            if (ready &&
                ((u32)slot->ammo >= WildCardSlot_Weapon(slot)->ammoPerShot || GameSettings_IsCheatEnabled(0)) &&
                !rack->noFire && player->buttonFire) {
                slot->motion.layer.loops = 0;
                if (GameSettings_IsCheatEnabled(0) || slot->ammo >= (s16)WildCardSlot_Weapon(slot)->ammoPerShot) {
                    if (!GameSettings_IsCheatEnabled(0)) {
                        slot->ammo -= (s16)WildCardSlot_Weapon(slot)->ammoPerShot;
                    } else {
                        slot->ammo = WildCardSlot_Weapon(slot)->maxAmmo;
                    }
                    slot->motion.layer.time = GETP(GETP(slot->motion.layer.anim)->phases[1])->end;
                    slot->motion.layer.phase = 1;
                }
            }
        }
        if (player->buttonsHeld & 2) {
            /* next weapon, once the current one is idle */
            u8 old = rack->cur;

            slot = &rack->slots[old];
            if (WildCardSlot_Model(slot) == NULL || slot->motion.layer.phase == 2 ||
                slot->motion.layer.time > GETP(GETP(slot->motion.layer.anim)->phases[1])->start + WildCardSlot_Model(slot)->fps) {
                while (++rack->cur <= 24 && WildCardSlot_Model(&rack->slots[rack->cur % WILDCARD_SLOTS]) == NULL) {
                }
                rack->cur = rack->cur % WILDCARD_SLOTS;
            }
            if (old != rack->cur) {
                rack->slots[rack->cur].scale = 0.0f;
            }
        }
    }
    if (GETP(rack->slots[rack->cur].item) != NULL) {
        u8 type;

        rack->slots[rack->cur].scale += 6.0f * g_fDeltaTimeSec;
        if (rack->slots[rack->cur].scale > 1.0f) {
            rack->slots[rack->cur].scale = 1.0f;
        }
        type = WildCardSlot_Weapon(&rack->slots[rack->cur])->aimMode;
        if (type == 3) {
            /* face the target */
            Thing *target = GETP(rack->target);

            if (target != NULL) {
                if (target->health < 0 || (target->flags & THING_FLAG_FREE)) {
                    SETP(rack->target, NULL);
                    rack->unk47C = -1.0f;
                } else {
                    THING_VFN(target, THING_V_GET_TARGET_POS, GetTargetPosFn)(target, &targetPos);
                    PSVECSubtract(&targetPos, &thing->pos, &thing->axis[2]);
                    PSVECCrossProduct(&thing->axis[1], &thing->axis[2], &thing->axis[0]);
                    PSVECCrossProduct(&thing->axis[2], &thing->axis[0], &thing->axis[1]);
                    if (PSVECSquareMag(&thing->axis[2])) {
                        PSVECNormalize(&thing->axis[2], &thing->axis[2]);
                    }
                    if (PSVECSquareMag(&thing->axis[1])) {
                        PSVECNormalize(&thing->axis[1], &thing->axis[1]);
                    }
                    if (PSVECSquareMag(&thing->axis[0])) {
                        PSVECNormalize(&thing->axis[0], &thing->axis[0]);
                    }
                    SubMatrixToQuaternion(THING_MTX(thing), &thing->quat);
                }
            }
        } else if (type == 2) {
            /* auto-aim: every 300 ms pick the visible target nearest the
             * aim line, then turn the weapon towards it */
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
                    if (targets[i] != rack && (player == NULL || targets[i] != GETP(player->vehicle))) {
                        THING_VFN(targets[i], THING_V_GET_TARGET_POS, GetTargetPosFn)(targets[i], &pos);
                        InverseXformVec(THING_MTX(thing), &pos, &local);
                        dist = _fabs(local.z * (local.x * local.y));
                        if (!(local.z < 1.0f) && !(_fabs(local.x / local.z) > 1.0f) && !RayHitsTerrain(&thing->pos, &pos, &t, NULL) &&
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
                mtx = *THING_MTX(thing);
                mtx.v[3] = GETP(slot->motion.animMatrices)[WildCardSlot_Weapon(slot)->aimBone].v[3];
                THING_VFN(GETP(rack->target), THING_V_GET_TARGET_POS, GetTargetPosFn)(GETP(rack->target), &pos2);
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
            if (slot->angle > 0.017453292f * WildCardSlot_Weapon(slot)->aimLimit) {
                slot->angle = 0.017453292f * WildCardSlot_Weapon(slot)->aimLimit;
            } else if (slot->angle < 0.017453292f * -WildCardSlot_Weapon(slot)->aimLimit) {
                slot->angle = 0.017453292f * -WildCardSlot_Weapon(slot)->aimLimit;
            }
        } else if (type == 1) {
            /* aimed with the stick */
            if (player != NULL && player->stickY != 0) {
                WildCardSlot *s = &rack->slots[rack->cur];

                s->angle += 0.017453292f * (player->stickY * g_fDeltaTimeSec);
                if (s->angle > 0.017453292f * WildCardSlot_Weapon(s)->aimLimit) {
                    s->angle = 0.017453292f * WildCardSlot_Weapon(s)->aimLimit;
                } else if (s->angle < 0.017453292f * -WildCardSlot_Weapon(s)->aimLimit) {
                    s->angle = 0.017453292f * -WildCardSlot_Weapon(s)->aimLimit;
                }
            }
        }
        if (WildCardSlot_Model(&rack->slots[rack->cur]) != NULL && rack->slots[rack->cur].scale < 1.0f) {
            /* popping up: flipped over the x axis until fully out */
            Vector4 axis __attribute__((aligned(16))) = {1.0f, 0.0f, 0.0f, 0.0f};

            GenerateQuaternion(&axis, &q, 3.1415927f * (rack->slots[rack->cur].scale - 1.0f));
            QuaternionMult(&thing->quat, &q, &out);
            QuaternionToSubMatrix(&out, THING_MTX(thing));
        }
        WildcardState_Heartbeat(&rack->slots[rack->cur], (u8 *)THING_MTX(thing), rack);
    }
    return 1;
}

extern Matrix4 g_identity;
/* Draw the selected weapon, scaled about the rack while it pops up. */
void WildCardRack_Draw(WildCardRack *rack) {
    WildCardSlot *slot = &rack->slots[rack->cur];
    Matrix4 tmp;
    Matrix4 scale;
    Matrix4 trans;
    Matrix4 out;
    f32 s;

    if (GETP(slot->motion.model) != NULL) {
        QuaternionToSubMatrix(&rack->base.quat, THING_MTX(&rack->base));
        s = slot->scale;
        if (s < 1.0f) {
            scale = g_identity;
            trans = g_identity;
            scale.v[2].z = s;
            scale.v[1].y = s;
            scale.v[0].x = s;
            tmp = g_identity;
            scale.v[3].x = rack->base.pos.x;
            scale.v[3].y = rack->base.pos.y;
            scale.v[3].z = rack->base.pos.z;
            trans.v[3].x = -rack->base.pos.x;
            trans.v[3].y = -rack->base.pos.y;
            trans.v[3].z = -rack->base.pos.z;
            MultiplyMatrix(&trans, &scale, &out);
            MotionState_Draw2(&slot->motion, HND(&out), NULL);
        } else {
            MotionState_Draw(&slot->motion);
        }
    }
}

void WildCardRack_JettisonAll(void *rack) {
    u32 i;

    if (rack != NULL) {
        for (i = 0; i < WILDCARD_SLOTS; i++) {
            WildCardRack_Jettison(rack, i);
        }
    }
}

/* Empty a slot; the last remaining weapon becomes the current one. */
void WildCardRack_Jettison(WildCardRack *rack, u8 slot) {
    u32 i;

    if (GETP(rack->slots[slot].motion.model) != NULL) {
        SETP(rack->slots[slot].item, NULL);
        SETP(rack->slots[slot].motion.model, NULL);
        for (i = 0; i < WILDCARD_SLOTS; i++) {
            if (GETP(rack->slots[i].item) != NULL && GETP(rack->slots[i].motion.model) != NULL) {
                rack->cur = i;
                rack->slots[i].scale = 0.0f;
            }
        }
    }
}

/* Add `count` ammo of a wildcard item: to the slot already holding that
 * weapon, else to a free slot (one-slot racks replace their weapon). */
void WildCardRack_Add(WildCardRack *rack, ItemRecord *item, s16 count) {
    WeaponInfo *weapon = GETP(item->weapon);
    s8 found = 0;
    s8 slot = rack->cur;
    u32 i;
    WildCardSlot *s;

    if (rack->base.flags & RACK_FLAG_ONE_SLOT) {
        s = &rack->slots[rack->cur];
        if (GETP(s->item) != NULL) {
            if (GETP(GETP(s->item)->weapon) == weapon) {
                if (s->ammo == 0) {
                    s->motion.layer.phase = 1;
                    s->motion.layer.time = GETP(((MotionAnim *)GETP(s->motion.model))->phases[1])->end;
                }
                s->ammo += (s16)count;
                if (s->ammo > (s16)weapon->maxAmmo) {
                    s->ammo = weapon->maxAmmo;
                }
                found = 1;
            } else {
                WildCardRack_Jettison(rack, rack->cur);
            }
        }
    } else {
        for (i = 0; i < WILDCARD_SLOTS; i++) {
            s = &rack->slots[i];
            if (GETP(s->item) != NULL && GETP(GETP(s->item)->weapon) == weapon) {
                if (s->ammo == 0) {
                    s->motion.layer.phase = 1;
                    s->motion.layer.time = GETP(((MotionAnim *)GETP(s->motion.model))->phases[1])->end;
                }
                s->ammo += (s16)count;
                if (s->ammo > (s16)weapon->maxAmmo) {
                    s->ammo = weapon->maxAmmo;
                }
                found = 1;
                if (rack->cur != i) {
                    rack->cur = i;
                    s->scale = 0.0f;
                }
                break;
            }
            if (GETP(s->item) == NULL || GETP(s->motion.model) == NULL) {
                slot = i;
            }
        }
    }
    if (!found) {
        MotionAnim *model;

        s = &rack->slots[slot];
        SETP(s->motion.model, GETP(weapon->anim));
        s->ammo = count;
        if (s->ammo > (s16)weapon->maxAmmo) {
            s->ammo = weapon->maxAmmo;
        }
        SETP(s->item, item);
        s->motion.layer.blend = 0.0f;
        MotionState_Init(&s->motion, 0);
        model = (MotionAnim *)GETP(s->motion.model);
        if (model != NULL) {
            if (model->hasOutro) {
                s->motion.layer.time = GETP(model->phases[2])->start;
                s->motion.layer.phase = 2;
            } else {
                s->motion.layer.time = GETP(model->phases[1])->end;
            }
            s->motion.layer.loops = 1;
            s->scale = 0.0f;
            s->angle = 0.0f;
            rack->cur = slot;
        }
    }
}

/* trigger: { numItems, QuadItem handles..., numSubjects, subjects... }:
 * a subject's car holds one of the items, or a wildcard of the same weapon. */
s32 TriggerItemPickup(u32 *trigger) {
    u32 i;
    u32 j;
    u32 numItems;
    QuadItem *item;
    u32 found;
    Thing *thing;
    WildCardRack *rack;
    u32 k;

    numItems = trigger[0] + 1;
    for (i = 0; i < trigger[0]; i++) {
        item = (QuadItem *)PTR(trigger[i + 1]);
        for (j = 0; j < trigger[numItems]; j++) {
            found = 0;
            thing = (Thing *)GETP(((TileCar *)PTR(trigger[j + numItems + 1]))->car);
            if (thing != NULL) {
                if (Thing_HasThing(thing, GETP(item->thing))) {
                    return 1;
                }
                rack = ThingManagerList_GetByType(&((CarA *)thing)->motionState.slaves, THING_TYPE_WILDCARD_RACK);
                for (k = 0; rack != NULL && k < WILDCARD_SLOTS; k++) {
                    if (GETP(rack->slots[k].item) != NULL &&
                        GETP(item->record)->weapon.h == GETP(rack->slots[k].item)->weapon.h) {
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

/* Whether a slot's item lies in [start, end) (a data pack being unloaded). */
s32 WildCardRack_Invalidate(WildCardRack *rack, u32 start, u32 end) {
    u32 i;
    u32 addr;

    for (i = 0; i < WILDCARD_SLOTS; i++) {
        addr = rack->slots[i].item.h;
        if (addr >= start && addr < end) {
            return 1;
        }
    }
    return 0;
}


/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_wildCardRackVTable[17];

__attribute__((constructor)) static void PortInit_g_wildCardRackVTable(void) {
    static void *const fns[] = {
        (void *)WildCardRack_Init,
        (void *)WildCardRack_Release,
        (void *)Thing_PreMove,
        (void *)WildCardRack_Move,
        (void *)WildCardRack_Draw,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
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
        g_wildCardRackVTable[i] = HND(fns[i]);
    }
}

