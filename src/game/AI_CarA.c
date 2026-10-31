#include "common.h"
#include "CarA.h"

static inline s8 AI_Car_CanTarget(u8 *ai, u8 *def) {
    s8 ok = 1;

    if (ai == NULL) {
        ok = 0;
    } else if (def[1] == 2) {
        ok = 0;
    }
    return ok;
}

void AI_Car_AsyncProcess(void) {
    typedef struct AIScripts {
        u8 pad0[0x90];
        u32 scripts[4];
    } AIScripts;
    typedef struct ItemParams {
        u8 pad0[0x20];
        s32 count;
    } ItemParams;
    typedef struct TileItem {
        u8 pad0[0x30];
        PTR32(ItemParams) params;
    } TileItem;
    typedef struct TileCar {
        Vector4 pos;
        f32 rotX;
        f32 rotY;
        f32 rotZ;
        u8 pad1C[0x24 - 0x1C];
        PTR32(AIScripts) aiScripts;
        PTR32(CarA) creature;
        u32 mode;
        u8 pad30[0x34 - 0x30];
        u8 unk34;
        u8 unk35;
        u8 pad36[0x38 - 0x36];
        f32 nearDist;
        f32 farDist;
        u32 scripts[4];
        u8 pad50[0x54 - 0x50];
        PTR32(TileItem) item;
    } TileCar;
    typedef struct CreatureParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        PTR32(AIScripts) aiScripts;
        u8 pad34[0x3C - 0x34];
        PTR32(void) player;
    } CreatureParams;
    typedef struct AIPlayerDesc {
        u32 script;
        f32 nearSq;
        f32 farSq;
        u8 unkC;
        u8 unkD;
    } AIPlayerDesc;
    typedef struct AIPlayer {
        u8 pad0[0x3C];
        PTR32(AIScripts) aiScripts;
        u8 pad40[0xA8 - 0x40];
        PTR32(TileCar) tileCar;
    } AIPlayer;
    static u32 carTile;
    extern u8 g_cameraManager[];
    extern Vector4 g_zeroVector;
    CreatureParams params;
    Matrix4 mtx;
    u32 action[3];
    AIPlayerDesc desc;
    TileCar *car;
    u32 script;
    s32 difficulty;
    u8 *cam;
    u32 i;
    f32 farSq;
    f32 nearSq;
    f32 minDist;
    void *rack;
    AIPlayer *player;

    car = World_GetTileCar(carTile);
    if (car != NULL) {
        if (GETP(car->aiScripts) != NULL && GETP(car->creature) == NULL && car->unk35 > 4 && car->unk34 != 0 && car->mode != 0) {
            farSq = car->farDist * car->farDist;
            nearSq = car->nearDist * car->nearDist;
            minDist = farSq;
            difficulty = GameSettings_GetDifficulty();
            SETP(params.aiScripts, GETP(car->aiScripts));
            script = 0;
            for (i = 0; i < g_cameraManager[0x640]; i++) {
                f32 dist = PSVECSquareDistance(g_cameraManager + i * 0x190 + 0x60, car);
                if (dist < minDist) {
                    minDist = dist;
                }
            }
            if (minDist <= nearSq || minDist >= farSq) {
                carTile++;
                return;
            }
            if (difficulty == 0) {
                if (car->scripts[0] != 0) {
                    script = car->scripts[0];
                } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                    script = GETP(params.aiScripts)->scripts[0];
                }
            } else if (difficulty == 1) {
                if (car->scripts[1] != 0) {
                    script = car->scripts[1];
                } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                    script = GETP(params.aiScripts)->scripts[1];
                }
            } else if (difficulty == 2) {
                if (car->scripts[2] != 0) {
                    script = car->scripts[2];
                } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                    script = GETP(params.aiScripts)->scripts[2];
                }
            } else if (difficulty == 3) {
                if (car->scripts[3] != 0) {
                    script = car->scripts[3];
                } else if (GETP(params.aiScripts)->scripts[0] != 0) {
                    script = GETP(params.aiScripts)->scripts[3];
                }
            }
            if (script == 0) {
                if (GETP(params.aiScripts)->scripts[0] != 0) {
                    script = GETP(params.aiScripts)->scripts[0];
                } else if (GETP(params.aiScripts)->scripts[1] != 0) {
                    script = GETP(params.aiScripts)->scripts[1];
                } else if (GETP(params.aiScripts)->scripts[2] != 0) {
                    script = GETP(params.aiScripts)->scripts[2];
                } else {
                    script = GETP(params.aiScripts)->scripts[3];
                }
            }
            desc.script = script;
            desc.unkD = 0;
            desc.unkC = car->unk34;
            desc.nearSq = car->nearDist * car->nearDist;
            desc.farSq = car->farDist * car->farDist;
            player = PlayerManager_NewAIPlayer(&desc);
            if (player == NULL) {
                carTile++;
                return;
            }
            SETP(player->tileCar, car);
            params.pos = car->pos;
            params.pos.y += 10.0f;
            SETP(params.player, player);
            params.vel = g_zeroVector;
            SetOrientation(&mtx, car->rotY, car->rotX, car->rotZ);
            SubMatrixToQuaternion(&mtx, &params.quat);
            if ((SETP(car->creature, ThingManager_DARYL_CreateThing(9, &params))) == NULL) {
                PlayerManager_RemovePlayer(player);
                carTile++;
                return;
            }
            SETP(GETP(car->creature)->base.trigger, car);
            Player_SetName(player, -1, "Dynamic AI");
            Player_SetThing(player, GETP(car->creature));
            SETP(player->aiScripts, GETP(car->aiScripts));
            SETP(player->tileCar, car);
            if (GETP(car->item) != NULL) {
                rack = ThingManagerList_GetByType(&GETP(car->creature)->motionState.slaves, 8);
                WildCardRack_JettisonAll(rack);
                WildCardRack_Add(rack, GETP(car->item), GETP(GETP(car->item)->params)->count);
            }
            action[0] = car->mode;
            action[1] = 1;
            action[2] = (u32)HND(car);
            ActionVehicleChangeMode(action);
        }
        carTile++;
    } else {
        carTile = 0;
    }
}

s32 AI_Car_CheckIfStuck(u8 *ai) {
    u8 *car = SLOT_LOAD(u8 *, (ai + 0x2C));
    s32 timer;
    u8 mode;
    f32 dot;

    if (*(s32 *)(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x464) > 600) {
        ai[0] = 0x7F;
        return 1;
    }
    timer = *(s32 *)(ai + 0x634);
    if (timer == 0) {
        if (*(s32 *)(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x404) < 500 && PSVECSquareDistance(ai + 0x620, SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40) < 0.5f) {
            (*(u32 *)(ai + 0x630))++;
            if (*(u32 *)(ai + 0x630) >= 10) {
                *(s32 *)(ai + 0x634) = 120;
                *(s16 *)(ai + 0x640) = AI_AngleToTarget(car, (Vector4 *)(ai + 0x400)) < 0.0f ? -127 : 127;
                *(Quaternion *)(ai + 0x610) = *(Quaternion *)(car + 0x30);
            }
        } else {
            *(u32 *)(ai + 0x630) = 0;
        }
        *(Quaternion *)(ai + 0x620) = *(Quaternion *)(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40);
        if (*(s32 *)(ai + 0x634) == 0) {
            return 0;
        }
    } else if (timer < 0) {
        *(s32 *)(ai + 0x634) = timer + 1;
        return 0;
    }
    ai[0] = *(s16 *)(ai + 0x640);
    (*(s32 *)(ai + 0x634))--;
    if (*(s32 *)(ai + 0x634) == 0) {
        *(s32 *)(ai + 0x634) = -120;
    }
    mode = (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)))[0x11];
    if (mode == 1) {
        ai[0x13] = 0xFF;
        dot = PSVECDotProduct(car + 0x30, ai + 0x610);
        if (dot > 1.0f) {
            dot = 1.0f;
        }
        if (dot < -1.0f) {
            dot = -1.0f;
        }
        if (30.0 <= 57.29578f * acos(dot)) {
            *(s32 *)(ai + 0x634) = -120;
        }
    } else if (mode == 2) {
        ai[0x14] = 0xFF;
        dot = PSVECDotProduct(car + 0x30, ai + 0x610);
        if (dot > 1.0f) {
            dot = 1.0f;
        }
        if (dot < -1.0f) {
            dot = -1.0f;
        }
        if (45.0 <= 57.29578f * acos(dot)) {
            *(s32 *)(ai + 0x634) = -180;
        }
    }
    return 1;
}

void AI_Car_DriveToTarget(u8 *ai, Vector4 *target, f32 speed) {
    Vector4 angVel __attribute__((aligned(16)));
    f32 dist;
    f32 angle;
    u8 *car;
    u8 *range;
    f32 distSq;
    f32 velSq;
    f32 vel;
    f32 deg;
    f32 t;
    f32 yaw;
    f32 r;
    f32 far;
    f32 near;
    f32 mid;
    f32 margin;
    f32 hi;
    f32 lo;
    s32 v;
    s16 s;
    s8 full;
    u8 brake;
    u8 reverse;
    u8 accel;

    car = SLOT_LOAD(u8 *, (ai + 0x2C));
    brake = 0;
    reverse = 0;
    accel = 0;
    distSq = PSVECSquareDistance(ai + 0x400, car + 0x40);
    velSq = PSVECSquareMag(car);
    vel = Sqrt(velSq);
    angle = AI_AngleToTarget(car, (Vector4 *)(ai + 0x400));
    deg = 57.29578f * angle;
    if (__fabs(deg) <= 5.0 || 0.0f == velSq) {
        brake = 0xFF;
        *(s16 *)(ai + 0x640) = (s8)(-6.0f * deg);
        ai[0] = *(s16 *)(ai + 0x640);
    } else if (velSq < 1600.0f && __fabs(deg) >= 100.0 && __fabs(deg) <= 125.0 && distSq < 900.0f) {
        accel = 0xFF;
        if ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)))[0x11] == 1) {
            *(s32 *)(ai + 0x634) = 80;
        } else {
            *(s32 *)(ai + 0x634) = 120;
        }
        *(s16 *)(ai + 0x640) = deg < 0.0f ? -127 : 127;
        *(Quaternion *)(ai + 0x610) = *(Quaternion *)(car + 0x30);
    } else if (velSq > 1600.0f && __fabs(deg) >= 45.0 && distSq < 5625.0f) {
        accel = 0xFF;
        reverse = 0xFF;
        *(s16 *)(ai + 0x640) = deg < 0.0f ? 127 : -127;
        ai[0] = *(s16 *)(ai + 0x640);
    } else {
        t = Sqrt(distSq / velSq);
        CarA_GetAngularVelocity(car, &angVel);
        yaw = PSVECDotProduct(car + 0x20, &angVel);
        if (yaw <= 0.01f) {
            yaw = 0.01f;
        }
        r = angle / yaw;
        if (r < 0.0f) {
            r = -r;
            r += yaw * (yaw >= 0.0f ? 3.0f : -3.0f);
        }
        r /= t;
        if (*(s16 *)(ai + 0x640) == 0 || ((*(s16 *)(ai + 0x640) << 16) ^ *(s32 *)&angle) < 0) {
            *(s16 *)(ai + 0x640) = -40.425358f * angle;
        }
        full = angle > 0.0f ? -127 : 127;
        if (r <= 1.0f) {
            brake = 0xFF;
            *(s16 *)(ai + 0x640) = r * (f32)(full - *(s16 *)(ai + 0x640)) + (f32)*(s16 *)(ai + 0x640);
        } else {
            s = r * (f32)*(s16 *)(ai + 0x640);
            if (_fabs(s) < _fabs(full)) {
                *(s16 *)(ai + 0x640) = s;
                brake = 0xFF;
            } else {
                *(s16 *)(ai + 0x640) = full;
                reverse = 0xFF;
                brake = 0xFF;
            }
        }
    }
    DistanceXYZ(car + 0x40, target, &dist);
    range = SLOT_LOAD(u8 *, SLOT_LOAD(void *, (ai + 0x3E4)));
    far = *(f32 *)(range + 0x28);
    near = *(f32 *)(range + 0x24);
    mid = (far - near) / 2.0f + near;
    if (dist < mid && accel != 0xFF) {
        v = 255 - (s32)(255.0f * ((dist - near) / (mid - near)));
        if (v > 255) {
            v = 255;
        }
        if (accel < v) {
            accel = v;
        }
        brake = 0;
    } else if (dist < far && brake) {
        v = 255.0f * ((dist - mid) / (far - mid));
        if (brake > v) {
            brake = v;
        }
    }
    if (*(f32 *)(ai + 0x63C) && (!speed || *(f32 *)(ai + 0x63C) < speed)) {
        speed = *(f32 *)(ai + 0x63C);
    }
    margin = 0.1f * speed;
    hi = speed + margin;
    lo = speed - margin;
    if (speed && vel > speed && accel != 0xFF) {
        v = 255.0f * ((vel - speed) / (hi - speed));
        if (v > 255) {
            v = 255;
        }
        if (accel < v) {
            accel = v;
        }
        brake = 0;
    } else if (speed && vel > lo && brake) {
        v = 255 - (s32)(255.0f * ((vel - lo) / (speed - lo)));
        if (brake > v) {
            brake = v;
        }
    }
    if (velSq < 100.0f || *(f32 *)(car + 0x458) < 0.0f) {
        brake = 0xFF;
        accel = 0;
    }
    if (*(s16 *)(ai + 0x640) > 127) {
        *(s16 *)(ai + 0x640) = 127;
    } else if (*(s16 *)(ai + 0x640) < -127) {
        *(s16 *)(ai + 0x640) = -127;
    }
    ai[0] = *(s16 *)(ai + 0x640);
    if (brake) {
        ai[0x12] = brake;
    } else if (accel) {
        ai[0x13] = accel;
    }
    if (reverse) {
        ai[0x15] = reverse;
    }
}
static inline void AI_Car_LeadTarget(u8 *ai) {
    Vector4 lead;
    u8 *myPos;
    u8 *thing;
    u8 *thingPos;
    u8 *car;
    f32 dist;
    f32 t;

    myPos = SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40;
    thing = SLOT_LOAD(u8 *, (ai + 0x3E8)) != NULL ? SLOT_LOAD(u8 *, (ai + 0x3E8)) : SLOT_LOAD(u8 *, (ai + 0x3EC));
    thingPos = thing + 0x40;
    if (PSVECSquareMag(thing) < 1.0f) {
        *(Vector4 *)(ai + 0x400) = *(Vector4 *)thingPos;
    } else {
        car = SLOT_LOAD(u8 *, (ai + 0x2C));
        dist = PSVECSquareDistance(thingPos, myPos);
        t = Sqrt(dist / PSVECSquareMag(car));
        PSVECScale(thing, &lead, t);
        PSVECAdd(&lead, thingPos, &lead);
        PSVECScale(thing, &lead, t * Sqrt(PSVECSquareDistance(&lead, myPos) / dist));
        PSVECAdd(&lead, thingPos, ai + 0x400);
        if ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)))[0x11] == 1) {
            PSVECSubtract(ai + 0x400, myPos, &lead);
            PSVECScale(thing + 0x10, &lead,
                       PSVECDotProduct(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x10, &lead) >= 0.0f ? -15.0f : 15.0f);
            PSVECAdd(ai + 0x400, &lead, ai + 0x400);
        }
    }
    if (PSVECSquareDistance(myPos, ai + 0x400) > 400.0f) {
        PSVECSubtract(ai + 0x400, myPos, ai + 0x400);
        PSVECScale(ai + 0x400, ai + 0x400, 0.5f);
        PSVECAdd(ai + 0x400, myPos, ai + 0x400);
    }
}

static inline void AI_Car_SteerAlongPath(u8 *ai, u32 i) {
    Vector4 a;
    Vector4 b;
    u8 *myPos;
    Vector4 *p0;
    Vector4 *p1;
    f32 ma;
    f32 mb;

    p0 = SLOT_LOAD(Vector4 *, (u32 *)((ai + 0x420)) + (i));
    myPos = SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40;
    if (i + 1 >= 32 || (p1 = SLOT_LOAD(Vector4 *, (u32 *)((ai + 0x420)) + (i + 1))) == NULL) {
        PSVECSubtract(myPos, p0, ai + 0x400);
        PSVECScale(ai + 0x400, ai + 0x400, 0.5f);
        PSVECAdd(ai + 0x400, p0, ai + 0x400);
    } else {
        PSVECSubtract(myPos, p0, &a);
        PSVECSubtract(p1, p0, &b);
        ma = MagnitudeVector(&a);
        mb = MagnitudeVector(&b);
        PSVECScale(&a, &a, mb / ma);
        PSVECScale(&b, &b, ma / mb);
        PSVECScale(&a, &a, -1.0f);
        PSVECAdd(&a, &b, ai + 0x400);
        PSVECScale(ai + 0x400, ai + 0x400, -(0.5f * ma / MagnitudeVector(ai + 0x400)));
        PSVECAdd(ai + 0x400, p0, ai + 0x400);
    }
}

static void AI_Car_SeekPoint(u8 *ai) {
    Vector4 target;
    f32 speed;
    u32 i;

    speed = 0.0f;
    if (SLOT_LOAD(u8 *, (ai + 0x3E8)) != NULL) {
        AI_Car_LeadTarget(ai);
    } else if ((i = *(u32 *)(ai + 0x4A4)) < 32 && SLOT_LOAD(Vector4 *, (u32 *)((ai + 0x420)) + (i)) != NULL) {
        AI_Car_SteerAlongPath(ai, i);
        speed = *(f32 *)((u8 *)SLOT_LOAD(Vector4 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))) + 0x2C);
    } else if (SLOT_LOAD(u8 *, (ai + 0x3EC)) != NULL) {
        AI_Car_LeadTarget(ai);
    } else {
        ai[0x13] = 0xFF;
        if ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)))[0x11] == 1) {
            ai[0x12] = 0xFF;
        }
        *(u32 *)(ai + 0x3F0) = 0;
        return;
    }
    if (SLOT_LOAD(u8 *, (ai + 0x3EC)) != NULL) {
        target = *(Vector4 *)(SLOT_LOAD(u8 *, (ai + 0x3EC)) + 0x40);
    } else if (*(u32 *)(ai + 0x4A4) < 32 && SLOT_LOAD(Vector4 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))) != NULL) {
        target = *SLOT_LOAD(Vector4 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4)));
    } else if (SLOT_LOAD(u8 *, (ai + 0x3E8)) != NULL) {
        target = *(Vector4 *)(SLOT_LOAD(u8 *, (ai + 0x3E8)) + 0x40);
    }
    AI_Car_DriveToTarget(ai, &target, speed);
}

static void AI_Car_WeaponAsyncHeartbeat_80008210(u8 *ai) {
    typedef struct WeaponParams {
        u8 pad0[0x24];
        u32 minAmmo;
        u8 pad28[0x30 - 0x28];
        u32 reloadTime;
        u8 priority;
        u8 pad35[3];
        f32 minRange;
        f32 maxRange;
        f32 aimAngle;
        f32 farAngle;
        f32 nearAngle;
    } WeaponParams;
    typedef struct WeaponDef {
        u8 type;
        u8 pad1[0x20 - 0x1];
        PTR32(PTR32(char)) name;
        u8 pad24[0x30 - 0x24];
        PTR32(WeaponParams) params;
    } WeaponDef;
    typedef struct WeaponSlot {
        u8 pad0[0x40];
        s16 ammo;
        u8 pad42[0x4C - 0x42];
        PTR32(WeaponDef) def;
    } WeaponSlot;
    typedef struct WeaponHolder {
        u8 pad0[0xB0];
        WeaponSlot slots[12];
        u8 current;
    } WeaponHolder;
    extern u32 g_attackDelay;
    extern u8 g_itemVTable[];
    static const char g_jetBoostersName_800CC3E4[] = "Jet Boosters";  /* @832 in .rodata */
    u8 *cars[8];
    u8 *target;
    WeaponHolder *holder;
    WeaponParams *params;
    s16 i;
    s8 lowHealth;
    s16 best;
    s16 preferred;
    WeaponSlot *slot;
    WeaponParams *reload;
    WeaponDef *def;
    Vector4 *targetPos;
    f32 nearest;
    f32 distSq;
    f32 dist;
    f32 angle;
    f32 angleRange;
    u32 numCars;
    u8 n;
    u8 type;

    target = NULL;
    lowHealth = 0;
    if (SLOT_LOAD(u8 *, (ai + 0x2C)) == NULL) {
        return;
    }
    if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
        numCars = ThingManager_GetActiveCars(cars, 8);
        nearest = 1e10f;
        for (n = 0; n < numCars; n++) {
            if (cars[n] != SLOT_LOAD(u8 *, (ai + 0x2C)) && !(*(u32 *)(cars[n] + 0x74) & 0x1000) &&
                (GameSettings_GetCodeVariable(0x12D, 0) != 4 || Player_IsHuman(SLOT_LOAD(void *, (cars[n] + 0x94))))) {
                distSq = PSVECSquareDistance(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40, cars[n] + 0x40);
                if (nearest > distSq) {
                    nearest = distSq;
                    target = cars[n];
                }
            }
        }
    } else if (SLOT_LOAD(u8 *, (ai + 0x3E8)) != NULL) {
        target = SLOT_LOAD(u8 *, (ai + 0x3E8));
    } else if (SLOT_LOAD(u8 *, (ai + 0x3EC)) != NULL &&
               (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x3EC)) + 0x8C)) == g_carAVTable ||
                (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x3EC)) + 0x8C)) == g_itemVTable && *SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x3EC)) + 0xC0)) == 2))) {
        target = SLOT_LOAD(u8 *, (ai + 0x3EC));
    }
    if (target != NULL && SLOT_LOAD(u8 *, (ai + 0x2C)) != NULL) {
        targetPos = (Vector4 *)(target + 0x40);
        best = -1;
        preferred = -1;
        distSq = PSVECSquareDistance(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40, targetPos);
        dist = Sqrt(distSq);
        angle = 57.29578f * AI_AngleToTarget(SLOT_LOAD(u8 *, (ai + 0x2C)), targetPos);
        if ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)))[0x11] == 1 && g_attackDelay == 0 && distSq < 400.0f) {
            if (AI_CheckAngleInRange(angle, 90.0f, 12.5f)) {
                ai[0x10] = 0xFF;
                *(u32 *)(ai + 0xC) |= 1;
            } else if (AI_CheckAngleInRange(angle, 270.0f, 12.5f)) {
                ai[0x1A] = 0xFF;
                *(u32 *)(ai + 0xC) |= 0x400;
            }
        }
        if ((f32)*(s32 *)(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x78) <
            0.3f * ((f32)*(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)) + 0xA0)) + 0x14) *
                    *SLOT_LOAD(f32 *, (ai + 0xD0)))) {
            lowHealth = 1;
        }
        holder = ThingManagerList_GetByType(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x344, 8);
        for (i = 0; holder != NULL && i < 12; i++) {
            slot = &holder->slots[i];
            if (slot == NULL || GETP(slot->def) == NULL) {
                continue;
            }
            params = GETP(GETP(slot->def)->params);
            if (slot->ammo < params->minAmmo) {
                continue;
            }
            type = GETP(slot->def)->type;
            if (type == 1) {
                if (lowHealth) {
                    best = i;
                    preferred = -1;
                    break;
                }
                continue;
            }
            if (lowHealth && type == 6 && dist <= params->maxRange && dist >= params->minRange) {
                best = i;
                preferred = i;
            }
            if (preferred != -1) {
                continue;
            }
            if (best != -1 && params->priority != (SLOT_LOAD(u8 *, (ai + 0xD0)))[0x26] &&
                GETP(GETP(holder->slots[best].def)->params)->priority <= params->priority) {
                continue;
            }
            if ((params->maxRange || params->minRange) &&
                !(dist <= params->maxRange && dist >= params->minRange)) {
                continue;
            }
            angleRange = params->nearAngle + (params->farAngle - params->nearAngle) * (dist - params->minRange) /
                                                 (params->maxRange - params->minRange);
            if ((params->nearAngle || params->farAngle) &&
                !AI_CheckAngleInRange(angle, params->aimAngle, angleRange)) {
                continue;
            }
            best = i;
            if (params->priority == (SLOT_LOAD(u8 *, (ai + 0xD0)))[0x26]) {
                preferred = i;
            }
        }
        if (preferred != -1) {
            best = preferred;
        }
        if (holder->current == best) {
            def = GETP(holder->slots[best].def);
            if (g_attackDelay == 0 || RandomAdd_s32(0, 50) == 0 || def->type != 3 ||
                strcmp(GETP(GETP(def->name)[0]), g_jetBoostersName_800CC3E4) == 0) {
                ai[0x1B] = 0xFF;
                *(u32 *)(ai + 0xC) |= 0x800;
            }
        } else if (best != -1 && *(u32 *)(ai + 0x658) == 0) {
            ai[0x11] = 0xFF;
            *(u32 *)(ai + 0xC) |= 2;
            *(u32 *)(ai + 0x658) = 3000;
        }
        reload = NULL;
        if (best != -1) {
            reload = GETP(GETP(holder->slots[best].def)->params);
        } else if (GETP(holder->slots[holder->current].def) != NULL) {
            reload = GETP(GETP(holder->slots[holder->current].def)->params);
        }
        if (reload != NULL) {
            *(u32 *)(ai + 0x654) = (f32)reload->reloadTime *
                                   RandomAdd(*(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0x14), *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0x18));
        }
    }
    if (*(u32 *)(ai + 0x654) == 0) {
        *(u32 *)(ai + 0x654) =
            300.0f * RandomAdd(*(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0x14), *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0x18));
    }
}

static void AI_Car_ClearedNavPoint_800087C8(u8 *car, s8 arg1) {
    u8 *point;
    u8 *nav;
    u8 *thing;
    u8 *target;
    s32 holder;   /* port: nearest nav point index (vtable 0x2C); the decomp keeps it in a void * */
    s32 dest;
    f32 dist;

    if (!arg1) {
        if (*(u32 *)(car + 0x4A0) < NavHolder_GetTotalNavPoints()) {
            nav = NavHolder_GetNavPoint(*(u32 *)(car + 0x4A0));
            point = SLOT_LOAD(u8 *, (u32 *)((car + 0x420)) + (*(u32 *)(car + 0x4A4)));
            if (point == nav) {
                if (*(u8 *)(point + 0x51) == 1) {
                    *(s32 *)(car + 0x4A0) = *(s32 *)(point + 0x4C);
                } else {
                    *(s32 *)(car + 0x4A0) = -1;
                }
            }
        }
        (*(u32 *)(car + 0x4A4))++;
        if (SLOT_LOAD(u8 *, (car + 0x3E4)) != NULL && *(u32 *)(car + 0x4A4) >= 30) {
            thing = SLOT_LOAD(u8 *, (car + 0x2C));
            holder = (((s32 (*)(void *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x2C))))(thing);
            target = SLOT_LOAD(u8 *, (car + 0x3E4));
            if (*SLOT_LOAD(u8 *, target) == 10 || *SLOT_LOAD(u8 *, target) == 14) {
                dest = *(s32 *)(SLOT_LOAD(u8 *, (target + 0x10)) + 0x28);
            } else {
                thing = SLOT_LOAD(u8 *, (target + 0x10));
                dest = (((s32 (*)(void *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x2C))))(thing);
            }
            AI_Car_PrepareNavPath(car, holder, dest);
        }
    }
    if (*(u32 *)(car + 0x4A4) < 32) {
        point = SLOT_LOAD(u8 *, (u32 *)((car + 0x420)) + (*(u32 *)(car + 0x4A4)));
        if (point != NULL) {
            dist = *(f32 *)(SLOT_LOAD(u8 *, (car + 0xD0)) + 0xC);
            if (*(f32 *)(point + 0x2C) < dist) {
                dist = *(f32 *)(point + 0x2C);
            }
            if (dist > 150.0f || 0.0f == dist) {
                *(s32 *)(car + 0x4AC) = 3000;
            } else if (dist > 100.0f) {
                *(s32 *)(car + 0x4AC) = 4500;
            } else {
                *(s32 *)(car + 0x4AC) = 6000;
            }
        }
    }
}

s32 AI_Car_PrepareNavPath(u8 *car, s32 from, s32 dest) {
    s32 result = 1;
    u8 *point;

    memset(car + 0x420, 0, 0x80);
    *(u32 *)(car + 0x4A4) = 0;
    point = NavHolder_FindPath(from, dest);
    if (point != NULL) {
        NavHolder_FillPointerArrayPath(point, car + 0x420, 0x20);
        *(s32 *)(car + 0x4A8) = dest;
        AI_Car_ClearedNavPoint_800087C8(car, 1);
        while (*(u32 *)(car + 0x4A4) < 31) {
            u8 *a = SLOT_LOAD(u8 *, (u32 *)((car + 0x420)) + (*(u32 *)(car + 0x4A4)));
            u8 *b = SLOT_LOAD(u8 *, (u32 *)((car + 0x420)) + (*(u32 *)(car + 0x4A4) + 1));
            if (a == NULL || b == NULL) {
                break;
            }
            if (LinearPointTest(SLOT_LOAD(u8 *, (car + 0x2C)) + 0x40, a, b) < 0.0f) {
                break;
            }
            AI_Car_ClearedNavPoint_800087C8(car, 0);
        }
    } else {
        result = 0;
        *(s32 *)(car + 0x4A8) = -1;
    }
    return result;
}

static void AI_Car_Cheat_80008A60(u8 *ai) {
    typedef struct WeaponParams {
        u8 pad0[0x28];
        u32 maxAmmo;
    } WeaponParams;
    typedef struct WeaponDef {
        u8 type;
        u8 pad1[0x30 - 0x1];
        PTR32(WeaponParams) params;
    } WeaponDef;
    typedef struct WeaponSlot {
        u8 pad0[0x40];
        s16 ammo;
        u8 pad42[0x4C - 0x42];
        PTR32(WeaponDef) def;
    } WeaponSlot;
    typedef struct WeaponHolder {
        u8 pad0[0xB0];
        WeaponSlot slots[12];
    } WeaponHolder;
    extern u8 g_cameraManager[];
    CarA *car;
    u8 *nearest;
    WeaponHolder *holder;
    u8 *cam;
    u8 **item;
    Vector4 *pos;
    u32 i;
    u32 j;
    f32 best;
    f32 dist;
    f32 cheat;

    cheat = 0.0f;
    car = SLOT_LOAD(CarA *, (ai + 0x2C));
    nearest = NULL;
    holder = ThingManagerList_GetByType(&car->motionState.slaves, 8);
    for (i = 0; i < 12; i++) {
        if (GETP(holder->slots[i].def) != NULL && GETP(holder->slots[i].def)->type == 3) {
            holder->slots[i].ammo = GETP(GETP(holder->slots[i].def)->params)->maxAmmo;
        }
    }
    best = 1e9f;
    pos = &car->base.pos;
    cam = g_cameraManager;
    for (j = 0; j < g_cameraManager[0x640]; cam += 0x190, j++) {
        nearest = SLOT_LOAD(u8 *, cam);
        if (nearest != NULL && SLOT_LOAD(u8 *, (nearest + 0x2C)) != NULL && *(u32 *)(nearest + 0x38) == 9) {
            dist = PSVECSquareDistance(pos, cam + 0x60);
            if (best > dist) {
                best = dist;
                nearest = SLOT_LOAD(u8 *, cam);
            }
        }
    }
    if (1e9f != best) {
        for (i = 0; i < 4; i++) {
            item = SLOT_LOAD(u8 **, (ai + 0x3D4 + i * 4));
            if (item == NULL || SLOT_LOAD(u8 *, item) == NULL) {
                break;
            }
            if (*SLOT_LOAD(u8 *, item) == 0xE) {
                cheat = (*(f32 *)((u8 *)car + 0x564) - *(f32 *)(SLOT_LOAD(u8 *, (nearest + 0x2C)) + 0x564)) / 750.0f;
                break;
            }
        }
    }
    if (cheat > 1.0f) {
        cheat = 1.0f;
    } else if (cheat < -1.0f) {
        cheat = -1.0f;
    }
    if (cheat > 0.0f) {
        *(f32 *)(ai + 0x63C) = *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0xC) * cheat + *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0xC);
        car->transmissionFlags |= 0x1000;
        car->turboTime = 10000;
    } else {
        if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
            *(f32 *)(ai + 0x63C) = *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0xC) - 0.2f * *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0xC) * -cheat;
        } else {
            *(f32 *)(ai + 0x63C) = *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0xC) - 0.333f * *(f32 *)(SLOT_LOAD(u8 *, (ai + 0xD0)) + 0xC) * -cheat;
        }
        car->transmissionFlags &= ~0x1000;
        car->turboTime = 0;
    }
}

void AI_Car_AsyncHeartbeat(u8 *ai) {
    extern u8 g_cameraManager[];
    u8 *car;
    s8 repath;
    s32 dest;
    s32 holder;   /* port: nearest nav point index (vtable 0x2C); the decomp keeps it in a void * */
    s32 paused;
    u8 *target;
    u8 *thing;
    s32 time;
    u32 j;
    u8 *tac;
    u8 i;
    f32 dist;
    f32 radius;
    f32 angle;

    car = SLOT_LOAD(u8 *, (ai + 0x2C));
    if (car != NULL && SLOT_LOAD(u8 *, (car + 0x8C)) != g_carAVTable) {
        return;
    }
    AI_AsyncHeartbeat(ai);
    if (*(s8 *)(ai + 0xCC) || SLOT_LOAD(u8 *, (ai + 0x2C)) == NULL) {
        ai[0x1B] = 0xFF;
        return;
    }
    if (ai[0xCD] != 3) {
        dist = *(f32 *)(ai + 0x660);
        j = 0;
        do {
            radius = PSVECSquareDistance(g_cameraManager + j * 0x190 + 0x60, SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40);
            if (radius < dist) {
                dist = radius;
            }
            j++;
        } while (j < g_cameraManager[0x640]);
        if (dist >= *(f32 *)(ai + 0x660)) {
            tac = SLOT_LOAD(u8 *, (ai + 0xA8));
            paused = Trigger_IsTACSystemPaused();
            Trigger_PauseTACSystem(1);
            CarA_Release(SLOT_LOAD(u8 *, (ai + 0x2C)));
            PlayerManager_RemovePlayer(ai);
            if (*(s8 *)(tac + 0x5C)) {
                *(u32 *)(tac + 0x28) = 0;
            }
            Trigger_PauseTACSystem(paused);
            return;
        }
    }
    if (SLOT_LOAD(u8 *, (ai + 0x3E4)) != NULL && (ai + 0x3D4) != NULL) {
        for (i = 0; i < 4; i++) {
            u8 *item = SLOT_LOAD(u8 *, (ai + 0x3D4 + i * 4));
            if (item == NULL) {
                break;
            }
            time = RandomAdd_s32(*(s32 *)(SLOT_LOAD(u8 *, item) + 0x40), *(s32 *)(SLOT_LOAD(u8 *, item) + 0x44));
            if (*(u32 *)(ai + 0xC8) > time) {
                *(u32 *)(ai + 0xC8) = time;
            }
        }
        thing = SLOT_LOAD(u8 *, (ai + 0x2C));
        repath = 0;
        holder = (((s32 (*)(void *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x2C))))(thing);
        target = SLOT_LOAD(u8 *, (ai + 0x3E4));
        if (*SLOT_LOAD(u8 *, target) == 10 || *SLOT_LOAD(u8 *, target) == 14) {
            dest = *(s32 *)(SLOT_LOAD(u8 *, (target + 0x10)) + 0x28);
            if (*(u32 *)(ai + 0x4A4) >= 32 || SLOT_LOAD(u8 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))) == NULL ||
                *(u32 *)(ai + 0x4AC) == 0) {
                repath = 1;
            }
        } else {
            thing = SLOT_LOAD(u8 *, (target + 0x10));
            if (!(*(u32 *)(thing + 0x74) & 0x1000)) {
                dest = (((s32 (*)(void *))SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (thing + 0x8C)) + 0x2C))))(thing);
                if ((s8)NavHolder_IsLinked(holder, dest)) {
                    *(u32 *)(ai + 0x4A4) = 32;
                    if (*(u32 *)(ai + 0x4A0) < NavHolder_GetTotalNavPoints()) {
                        dist = PSVECSquareDistance(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40, NavHolder_GetNavPoint(*(u32 *)(ai + 0x4A0)));
                        if (dist < *(f32 *)(NavHolder_GetNavPoint(*(u32 *)(ai + 0x4A0)) + 0x24) *
                                       *(f32 *)(NavHolder_GetNavPoint(*(u32 *)(ai + 0x4A0)) + 0x24)) {
                            if (*(u8 *)(NavHolder_GetNavPoint(*(u32 *)(ai + 0x4A0)) + 0x51) == 1) {
                                *(s32 *)(ai + 0x4A0) = *(s32 *)(NavHolder_GetNavPoint(*(u32 *)(ai + 0x4A0)) + 0x4C);
                            } else {
                                *(s32 *)(ai + 0x4A0) = -1;
                            }
                        }
                    }
                } else if (*(u32 *)(ai + 0x4A4) >= 32 || SLOT_LOAD(u8 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))) == NULL ||
                           *(u32 *)(ai + 0x4AC) == 0) {
                    repath = 1;
                } else if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x3E4)) + 0x10)) + 0x8C)) == g_carAVTable &&
                           dest != *(s32 *)(ai + 0x4A8)) {
                    repath = 1;
                }
            }
        }
        if (repath) {
            AI_Car_PrepareNavPath(ai, holder, dest);
        }
        if (*(u32 *)(ai + 0x3EC) != 0 || *(u32 *)(ai + 0x3F0) != 0) {
            if (ai[0xCD] == 1) {
                Vector4 out __attribute__((aligned(16)));
                Vector4 quat __attribute__((aligned(16))) = *(Vector4 *)(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x50);
                Vector4 axis __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 0.0f};

                if (*(u32 *)(ai + 0x4A4) < 32 && SLOT_LOAD(u8 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))) != NULL) {
                    angle = AI_AngleToTarget(SLOT_LOAD(u8 *, (ai + 0x2C)), (Vector4 *)SLOT_LOAD(u8 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))));
                } else if (SLOT_LOAD(u8 *, (ai + 0x3EC)) != NULL) {
                    angle = AI_AngleToTarget(SLOT_LOAD(u8 *, (ai + 0x2C)), (Vector4 *)(SLOT_LOAD(u8 *, (ai + 0x3EC)) + 0x40));
                } else if (SLOT_LOAD(u8 *, (ai + 0x3F0)) != NULL) {
                    angle = AI_AngleToTarget(SLOT_LOAD(u8 *, (ai + 0x2C)), SLOT_LOAD(Vector4 *, (ai + 0x3F0)));
                }
                GenerateQuaternion(&axis, &out, angle);
                QuaternionMult(&out, &quat, (Vector4 *)(SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x50));
                ai[0xCD] = 2;
            }
            if ((SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x3BC)))[0x11] == 1 && GameSettings_GetCodeVariable(0x198, 0) == 0) {
                AI_Car_Cheat_80008A60(ai);
            }
        }
    }
}

void AI_Car_AsyncScanDestructibles(u8 *car) {
    f32 dist;
    u8 *target;
    u32 index;

    if (SLOT_LOAD(u8 *, (u8 *)(car) + 0x2C) != NULL) {
        index = M2C_FIELD(car, u32 *, 0x4A4);
        target = NULL;
        if (index < 32 && SLOT_LOAD(u8 *, (u32 *)((car + 0x420)) + (index)) != NULL) {
            target = SLOT_LOAD(u8 *, (u32 *)((car + 0x420)) + (index));
        } else if (SLOT_LOAD(u8 *, (u8 *)(car) + 0x3EC) != NULL) {
            target = SLOT_LOAD(u8 *, (u8 *)(car) + 0x3EC) + 0x40;
        }
        SLOT_STORE((u8 *)(car) + 0x3E8, NULL);
        if (target != NULL) {
            dist = 1.0f;
            SLOT_STORE((u8 *)(car) + 0x3E8, PTR(RayHitsObjects_GetObject(SLOT_LOAD(u8 *, (u8 *)(car) + 0x2C) + 0x40, target, &dist, 1, 0)));
            M2C_FIELD(car, s32 *, 0x608) = RandomAdd_s32(400, 400);
        }
    }
}

static s8 AI_Car_IsTargetValid_800094EC(u8 *ai, u8 *target, f32 maxDist, f32 minDist, f32 center, f32 range) {
    extern u8 g_itemVTable[];
    typedef struct {
        s32 limit0;
        s32 limit4;
        s32 limit8;
        s32 limitC;
        u8 mode;
        u8 compare;
    } TargetLimits;
    typedef struct {
        u8 type;
        u8 subtype;
        u8 pad2[3];
        u8 unk5;
        u8 pad6[0x58 - 6];
        PTR32(TargetLimits) limits;
    } TargetDef;
    typedef struct {
        PTR32(u8) thing;
        s16 v4;
        s16 v6;
        s16 v8;
        s16 vA;
        u8 padC[8];
    } TargetEntry;
    typedef struct {
        u8 pad0[0x4B0];
        TargetEntry entries[16];
    } AIData;
    s16 i;
    s8 valid;
    TargetLimits *limits;
    u8 *pos;
    u8 *thing;
    u8 type;
    TargetDef *def;
    f32 dist;

    def = SLOT_LOAD(TargetDef *, target);
    type = def->type;
    if (type == 6 || (u8)(type - 7) <= 2 || type == 11 || type == 13) {
        thing = SLOT_LOAD(u8 *, (target + 0x10));
        if (thing != NULL && SLOT_LOAD(u8 *, (thing + 0x8C)) == g_itemVTable && !(*(u32 *)(thing + 0x74) & 0x1000)) {
            pos = NULL;
            if (*SLOT_LOAD(u8 *, (thing + 0xC0)) == 7 && SLOT_LOAD(u8 *, (thing + 0x94)) != NULL && *(u32 *)(SLOT_LOAD(u8 *, (thing + 0x94)) + 0x2C) != 0) {
                if (Player_IsHuman(SLOT_LOAD(u8 *, (thing + 0x94)))) {
                    pos = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (target + 0x10)) + 0x94)) + 0x2C)) + 0x40;
                }
            } else {
                pos = thing + 0x40;
            }
            if (pos != NULL) {
                dist = PSVECSquareDistance(pos, SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40);
                if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                    if (AI_CheckAngleInRange(57.29578f * AI_AngleToTarget(SLOT_LOAD(u8 *, (ai + 0x2C)), (Vector4 *)pos), center, range)) {
                        return 1;
                    }
                }
            }
        }
    } else if ((u8)(type - 1) <= 1 || type == 3) {
        valid = 1;
        if ((*(u32 *)(SLOT_LOAD(u8 *, (target + 0x10)) + 0x74) & 0x1000) || !AI_Car_CanTarget(ai, (u8 *)def)) {
            return 0;
        }
        if (def->subtype == 3) {
            limits = GETP(def->limits);
            valid = limits->mode != 1;
            for (i = 0; i < 16 && GETP(((AIData *)ai)->entries[i].thing) != NULL; i++) {
                if (GETP(((AIData *)ai)->entries[i].thing) == SLOT_LOAD(u8 *, (target + 0x10))) {
                    if (limits->compare == 1) {
                        if ((limits->limit8 > 0 && ((TargetEntry *)(ai + 0x4B0))[i].v8 > limits->limit8) ||
                            (limits->limitC > 0 && ((TargetEntry *)(ai + 0x4B0))[i].vA > limits->limitC) ||
                            (limits->limit0 > 0 && ((TargetEntry *)(ai + 0x4B0))[i].v4 > limits->limit0) ||
                            (limits->limit4 > 0 && ((TargetEntry *)(ai + 0x4B0))[i].v6 > limits->limit4)) {
                            valid = limits->mode == 1;
                        }
                    } else {
                        if ((limits->limit8 == 0 || ((TargetEntry *)(ai + 0x4B0))[i].v8 > limits->limit8) &&
                            (limits->limitC == 0 || ((TargetEntry *)(ai + 0x4B0))[i].vA > limits->limitC) &&
                            (limits->limit0 == 0 || ((TargetEntry *)(ai + 0x4B0))[i].v4 > limits->limit0) &&
                            (limits->limit4 == 0 || ((TargetEntry *)(ai + 0x4B0))[i].v6 > limits->limit4)) {
                            valid = limits->mode == 1;
                        }
                    }
                    break;
                }
            }
        }
        if (valid) {
            dist = PSVECSquareDistance(SLOT_LOAD(u8 *, (target + 0x10)) + 0x40, SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40);
            if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                if (AI_CheckAngleInRange(57.29578f * AI_AngleToTarget(SLOT_LOAD(u8 *, (ai + 0x2C)), (Vector4 *)(SLOT_LOAD(u8 *, (target + 0x10)) + 0x40)), center, range)) {
                    return 1;
                }
            }
        }
        return 0;
    } else if (type == 10) {
        u8 *nav = SLOT_LOAD(u8 *, (target + 0x10));

        if (nav != NULL && nav[0x50] != 0) {
            if (def->unk5 != 0 || PSVECSquareDistance(nav, SLOT_LOAD(u8 *, (ai + 0x2C)) + 0x40) > *(f32 *)(nav + 0x24) * *(f32 *)(nav + 0x24)) {
                return 1;
            }
        }
    } else if (type == 14) {
        if (SLOT_LOAD(u8 *, (target + 0x10)) != NULL) {
            if (*(u32 *)(ai + 0x4A0) < NavHolder_GetTotalNavPoints()) {
                SLOT_STORE((target + 0x10), NavHolder_GetNavPoint(*(u32 *)(ai + 0x4A0)));
                return 1;
            }
        }
    }
    return 0;
}

s32 AI_Car_CheckLocalStats(u8 *ai, u8 *node) {
    typedef struct LocalStats {
        u8 health;
        u8 grip;
        u8 weapons;
        u8 pad3[3];
        u16 weaponCount;
        s32 healthValue;
        f32 gripValue;
    } LocalStats;
    typedef struct StatsNode {
        u8 pad0;
        u8 type;
        u8 pad2[0x58 - 2];
        PTR32(LocalStats) stats;
    } StatsNode;
    typedef struct WeaponParams {
        u8 pad0[0x24];
        u32 minAmmo;
    } WeaponParams;
    typedef struct WeaponDef {
        u8 type;
        u8 pad1[0x30 - 0x1];
        PTR32(WeaponParams) params;
    } WeaponDef;
    typedef struct WeaponSlot {
        u8 pad0[0x40];
        s16 ammo;
        u8 pad42[0x4C - 0x42];
        PTR32(WeaponDef) def;
    } WeaponSlot;
    typedef struct WeaponHolder {
        u8 pad0[0xB0];
        WeaponSlot slots[12];
    } WeaponHolder;
    LocalStats *stats = GETP(((StatsNode *)node)->stats);
    CarA *car = SLOT_LOAD(CarA *, (ai + 0x2C));
    s32 health;
    u16 count;
    u16 i;
    WeaponHolder *holder;

    if (((StatsNode *)node)->type != 1) {
        return 1;
    }
    if (stats->health != 0) {
        health = (f32)car->base.health / (100.0f * (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth);
        switch (stats->health) {
        case 1:
            if (health >= stats->healthValue) {
                return 0;
            }
            break;
        case 2:
            if (health <= stats->healthValue) {
                return 0;
            }
            break;
        }
    }
    switch (stats->grip) {
    case 0:
        break;
    case 1:
        if (car->gripScale >= stats->gripValue) {
            return 0;
        }
        break;
    case 2:
        if (car->gripScale <= stats->gripValue) {
            return 0;
        }
        break;
    }
    if (stats->weapons != 0) {
        count = 0;
        holder = ThingManagerList_GetByType(&car->motionState.slaves, 8);
        for (i = 0; holder != NULL && i < 12; i++) {
            if (GETP(holder->slots[i].def) != NULL && GETP(holder->slots[i].def)->type == 3 &&
                holder->slots[i].ammo > GETP(GETP(holder->slots[i].def)->params)->minAmmo) {
                count++;
            }
        }
        switch (stats->weapons) {
        case 1:
            if (count >= stats->weaponCount) {
                return 0;
            }
            break;
        case 2:
            if (count <= stats->weaponCount) {
                return 0;
            }
            break;
        }
    }
    return 1;
}

s8 AI_Car_DetermineTarget(u8 *ai, u8 *branch, u8 slot) {
    typedef struct {
        s32 limit0;
        s32 limit4;
        s32 limit8;
        s32 limitC;
        u8 mode;
        u8 compare;
    } TargetLimits;
    typedef struct {
        u8 type;
        u8 subtype;
        u8 sortMode;
        u8 pad3[2];
        u8 unk5;
        u8 pad6[0x14 - 6];
        f32 minDist;
        f32 maxDist;
        f32 center;
        f32 range;
        u8 pad24[0x34 - 0x24];
        u32 unk34;
        u8 pad38[0x58 - 0x38];
        PTR32(TargetLimits) limits;
    } TargetDef;
    typedef struct {
        PTR32(TargetDef) def;
        u8 pad4[8];
        u32 unkC;
        PTR32(u8) target;
    } AIBranch;
    typedef struct {
        PTR32(u8) thing;
        s16 v4;
        s16 v6;
        s16 v8;
        s16 vA;
        u8 padC[8];
    } TargetEntry;
    typedef struct {
        PTR32(u8) thing;
        f32 dist;
        f32 angle;
        u8 padC[4];
    } Candidate;
    typedef struct {
        u8 pad0[0x2C];
        PTR32(u8) car;
        u8 pad30[0xD0 - 0x30];
        PTR32(u8) unkD0;
        u8 padD4[0x3EC - 0xD4];
        PTR32(u8) targetThing;
        PTR32(u8) targetNav;
        u8 pad3F4[0x410 - 0x3F4];
        f32 targetDist;
        f32 minDist;
        f32 center;
        f32 range;
        u8 pad420[0x4A0 - 0x420];
        u32 wayPoint;
        u8 pad4A4[0x4B0 - 0x4A4];
        TargetEntry entries[16];
    } AIData;
    s8 valid;
    u8 *cars[8];
    u8 *items[0x60];
    Candidate cands[0x80];
    TargetDef *def;
    f32 maxDist;
    f32 minDist;
    f32 center;
    f32 range;
    f32 a;
    f32 b;
    f32 c;
    f32 d;
    f32 dist;
    f32 angle;
    Candidate *closest;
    u8 *thing;
    u8 *other;
    u8 *nav;
    u8 *path;
    TargetLimits *limits;
    u32 num;
    u32 n;
    u8 *pos;
    u8 i;
    s16 k;
    u16 j;
    u8 type;
    u32 index;
    u8 *car;
    u16 count;
    Candidate *best;

    def = GETP(((AIBranch *)branch)->def);
    car = GETP(((AIData *)ai)->car);
    valid = 0;
    maxDist = def->maxDist * def->maxDist;
    center = def->center;
    minDist = def->minDist * def->minDist;
    range = def->range;
    count = 0;
    best = NULL;
    if (slot) {
        if (((AIData *)ai)->targetDist || ((AIData *)ai)->minDist) {
            if (maxDist > ((AIData *)ai)->targetDist) {
                maxDist = ((AIData *)ai)->targetDist;
            }
            if (minDist < ((AIData *)ai)->minDist) {
                minDist = ((AIData *)ai)->minDist;
            }
        }
        if (!center && !range) {
            center = ((AIData *)ai)->center;
            range = ((AIData *)ai)->range;
        } else if (((AIData *)ai)->center || ((AIData *)ai)->range) {
            c = center + range;
            d = center - range;
            a = ((AIData *)ai)->center + ((AIData *)ai)->range;
            b = ((AIData *)ai)->center - ((AIData *)ai)->range;
            if (a >= 360.0f) {
                a -= 360.0f;
            }
            if (b < 0.0f) {
                b += 360.0f;
            }
            if (c >= 360.0f) {
                c -= 360.0f;
            }
            if (d < 0.0f) {
                d -= 360.0f;
            }
            if (a < c) {
                c = a;
            }
            if (b > d) {
                d = b;
            }
            if (d > c) {
                d -= 360.0f;
            }
            range = 0.5f * (c - d);
            center = d + range;
        }
    }

    if (branch == SLOT_LOAD(u8 *, (u32 *)((ai + 0x3D4)) + (slot)) &&
        AI_Car_IsTargetValid_800094EC(ai, branch, maxDist, minDist, center, range) &&
        (GETP(((AIBranch *)branch)->def)->unk5 == 0 || ((AIBranch *)branch)->unkC != 0)) {
        type = GETP(((AIBranch *)branch)->def)->type;
        if (type == 10 || type == 14) {
            SETP(((AIData *)ai)->targetThing, NULL);
            SETP(((AIData *)ai)->targetNav, GETP(((AIBranch *)branch)->target));
            ((AIData *)ai)->targetDist = PSVECSquareDistance(GETP(((AIData *)ai)->car) + 0x40, GETP(((AIData *)ai)->targetNav));
        } else {
            if ((type == 6 || type == 11) && *SLOT_LOAD(u8 *, (GETP(((AIBranch *)branch)->target) + 0xC0)) == 7 &&
                SLOT_LOAD(u8 *, (GETP(((AIBranch *)branch)->target) + 0x94)) != NULL &&
                SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(((AIBranch *)branch)->target) + 0x94)) + 0x2C)) != NULL) {
                SETP(((AIData *)ai)->targetThing, SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(((AIBranch *)branch)->target) + 0x94)) + 0x2C)));
            } else {
                SETP(((AIData *)ai)->targetThing, GETP(((AIBranch *)branch)->target));
            }
            SETP(((AIData *)ai)->targetNav, NULL);
            ((AIData *)ai)->targetDist = PSVECSquareDistance(GETP(((AIData *)ai)->car) + 0x40, GETP(((AIData *)ai)->targetThing) + 0x40);
        }
        ((AIData *)ai)->minDist = minDist;
        ((AIData *)ai)->center = center;
        ((AIData *)ai)->range = range;
        return 1;
    }

    type = GETP(((AIBranch *)branch)->def)->type;
    if (type == 1 || type == 3) {
        num = ThingManager_GetActiveCars(cars, 8);
        for (i = 0; i < num; i++) {
            if (*(u32 *)(cars[i] + 0x74) & 0x1000) {
                continue;
            }
            if (GETP(((AIBranch *)branch)->def)->type == 1 && !Player_IsHuman(SLOT_LOAD(u8 *, (cars[i] + 0x94)))) {
                continue;
            }
            thing = cars[i];
            other = GETP(((AIData *)ai)->car);
            if (thing == other) {
                continue;
            }
            def = GETP(((AIBranch *)branch)->def);
            if (!AI_Car_CanTarget(ai, (u8 *)def)) {
                continue;
            }
            valid = 1;
            if (def->subtype == 3) {
                limits = GETP(def->limits);
                valid = limits->mode != 1;
                for (k = 0; k < 16 && GETP(((AIData *)ai)->entries[k].thing) != NULL; k++) {
                    if (GETP(((AIData *)ai)->entries[k].thing) == cars[i]) {
                        if (limits->compare == 1) {
                            if ((limits->limit8 > 0 && ((AIData *)ai)->entries[k].v8 > limits->limit8) ||
                                (limits->limitC > 0 && ((AIData *)ai)->entries[k].vA > limits->limitC) ||
                                (limits->limit0 > 0 && ((AIData *)ai)->entries[k].v4 > limits->limit0) ||
                                (limits->limit4 > 0 && ((AIData *)ai)->entries[k].v6 > limits->limit4)) {
                                valid = limits->mode == 1;
                            }
                        } else {
                            if ((limits->limit8 == 0 || ((AIData *)ai)->entries[k].v8 > limits->limit8) &&
                                (limits->limitC == 0 || ((AIData *)ai)->entries[k].vA > limits->limitC) &&
                                (limits->limit0 == 0 || ((AIData *)ai)->entries[k].v4 > limits->limit0) &&
                                (limits->limit4 == 0 || ((AIData *)ai)->entries[k].v6 > limits->limit4)) {
                                valid = limits->mode == 1;
                            }
                        }
                        break;
                    }
                }
            }
            if (valid == 1) {
                dist = PSVECSquareDistance(thing + 0x40, other + 0x40);
                valid = 0;
                if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                    angle = 57.29578f * AI_AngleToTarget(car, (Vector4 *)(cars[i] + 0x40));
                    if (AI_CheckAngleInRange(angle, center, range) && count < 0x80) {
                        cands[count].angle = angle;
                        valid = 1;
                        cands[count].dist = dist;
                        SETP(cands[count].thing, cars[i]);
                        count++;
                    }
                }
            }
        }
    } else if (type == 2) {
    } else if ((u8)(type - 6) <= 3 || type == 11 || type == 13) {
        num = ThingManager_GetActiveItems(items, 0x60);
        for (n = 0; n < num; n++) {
            thing = items[n];
            if (thing[0xE0] != 1) {
                continue;
            }
            type = GETP(((AIBranch *)branch)->def)->type;
            if (type == 6 || (type == 7 && (*SLOT_LOAD(u8 *, (thing + 0xC0)) == 1 || *SLOT_LOAD(u8 *, (thing + 0xC0)) == 6)) ||
                (type == 8 && *SLOT_LOAD(u8 *, (thing + 0xC0)) == 2) || (type == 9 && *SLOT_LOAD(u8 *, (thing + 0xC0)) == 3) ||
                (type == 11 && *SLOT_LOAD(u8 *, (thing + 0xC0)) == 7) || (type == 13 && *SLOT_LOAD(u8 *, (thing + 0xC0)) == 6)) {
                pos = NULL;
                if (*SLOT_LOAD(u8 *, (thing + 0xC0)) == 7 && SLOT_LOAD(u8 *, (thing + 0x94)) != NULL && *(u32 *)(SLOT_LOAD(u8 *, (thing + 0x94)) + 0x2C) != 0) {
                    if (Player_IsHuman(SLOT_LOAD(u8 *, (thing + 0x94)))) {
                        pos = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (items[n] + 0x94)) + 0x2C)) + 0x40;
                    }
                } else {
                    pos = thing + 0x40;
                }
                if (pos != NULL) {
                    dist = PSVECSquareDistance(pos, GETP(((AIData *)ai)->car) + 0x40);
                    if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                        angle = 57.29578f * AI_AngleToTarget(GETP(((AIData *)ai)->car), (Vector4 *)pos);
                        if (AI_CheckAngleInRange(angle, center, range)) {
                            cands[count].angle = angle;
                            cands[count].dist = dist;
                            SETP(cands[count].thing, thing);
                            count++;
                        }
                    }
                }
            }
        }
    } else if (type == 10) {
        num = NavHolder_GetTotalNavPoints();
        for (n = 0; n < num; n++) {
            nav = NavHolder_GetNavPoint(n);
            if (nav != NULL && nav[0x50] != 0 && RandomAdd_s32(0, 100) < nav[0x35]) {
                dist = PSVECSquareDistance(nav, GETP(((AIData *)ai)->car) + 0x40);
                if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                    angle = 57.29578f * AI_AngleToTarget(car, (Vector4 *)nav);
                    if (AI_CheckAngleInRange(angle, center, range) && count < 0x80) {
                        cands[count].angle = angle;
                        cands[count].dist = dist;
                        SETP(cands[count].thing, nav);
                        count++;
                    }
                }
            }
        }
    } else if (type == 14) {
        if (((AIData *)ai)->wayPoint != 0xFFFFFFFF) {
            index = NavHolder_GetFirstWayPoint();
        } else {
            index = 0xFFFFFFFF;
        }
        while (index != 0xFFFFFFFF) {
            nav = NavHolder_GetNavPoint(index);
            if (nav == NULL || nav[0x50] == 0) {
                break;
            }
            dist = PSVECSquareDistance(nav, GETP(((AIData *)ai)->car) + 0x40);
            if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                angle = 57.29578f * AI_AngleToTarget(car, (Vector4 *)nav);
                if (AI_CheckAngleInRange(angle, center, range) && count < 0x80) {
                    cands[count].angle = angle;
                    cands[count].dist = dist;
                    SETP(cands[count].thing, nav);
                    count++;
                }
            }
            if (nav[0x51] != 1) {
                break;
            }
            index = *(u32 *)(nav + 0x4C);
        }
    }

    if (count != 0) {
        best = &cands[0];
        if (count > 1) {
            def = GETP(((AIBranch *)branch)->def);
            if (def->sortMode == 0) {
                if (def->type != 14) {
                    for (j = 1; j < count; j++) {
                        if (cands[j].dist < best->dist) {
                            best = &cands[j];
                        }
                    }
                }
            } else if (def->sortMode == 1) {
                if (def->type != 14) {
                    for (j = 1; j < count; j++) {
                        if (cands[j].dist > best->dist) {
                            best = &cands[j];
                        }
                    }
                } else {
                    best += count - 1;
                }
            } else if (def->sortMode == 2) {
                best += RandomAdd_s32(0, count - 1);
            } else if (def->sortMode == 3) {
                if (def->type == 7 || def->type == 8 || def->type == 9) {
                    closest = NULL;
                    for (j = 1; j < count; j++) {
                        u8 *info = SLOT_LOAD(u8 *, (GETP(cands[j].thing) + 0xC0));
                        u8 *owner;

                        switch (info[0]) {
                        case 3:
                            owner = SLOT_LOAD(u8 *, (info + 0x30));
                            if (owner != NULL) {
                                if (closest != NULL) {
                                    if (owner[0x34] == GETP(((AIData *)ai)->unkD0)[0x26] && cands[j].dist < closest->dist) {
                                        closest = &cands[j];
                                    }
                                } else if (owner[0x34] == GETP(((AIData *)ai)->unkD0)[0x26]) {
                                    closest = &cands[j];
                                } else if (owner[0x34] < (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(best->thing) + 0xC0)) + 0x30)))[0x34]) {
                                    best = &cands[j];
                                } else if (owner[0x34] == (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(best->thing) + 0xC0)) + 0x30)))[0x34] &&
                                           cands[j].dist < best->dist) {
                                    best = &cands[j];
                                }
                            }
                            break;
                        }
                    }
                    if (closest != NULL) {
                        best = closest;
                    }
                } else if (def->type == 14) {
                    path = NavHolder_FindPath(SLOT_LOAD(s32 (*)(u8 *), SLOT_LOAD(u8 *, (GETP(((AIData *)ai)->car) + 0x8C)) + 0x2C)(GETP(((AIData *)ai)->car)),
                                              *(s32 *)(GETP(cands[count - 1].thing) + 0x28));
                    best = NULL;
                    while (path != NULL && best == NULL) {
                        for (j = 0; j < count; j++) {
                            if (path == GETP(cands[j].thing)) {
                                best = &cands[j];
                                break;
                            }
                        }
                        path = SLOT_LOAD(u8 *, (path + 0x38));
                    }
                } else {
                    for (j = 1; j < count; j++) {
                        if (cands[j].dist < best->dist) {
                            best = &cands[j];
                        }
                    }
                }
            }
        }
        if (best == NULL) {
            return 0;
        }
        type = GETP(((AIBranch *)branch)->def)->type;
        if (type == 10 || type == 14) {
            SETP(((AIData *)ai)->targetNav, GETP(best->thing));
            SETP(((AIData *)ai)->targetThing, NULL);
            if (GETP(((AIBranch *)branch)->def)->type == 14 && ((AIData *)ai)->wayPoint == 0xFFFFFFFE) {
                ((AIData *)ai)->wayPoint = *(u32 *)(GETP(((AIData *)ai)->targetNav) + 0x28);
            }
        } else {
            if ((type == 6 || type == 11) && *SLOT_LOAD(u8 *, (GETP(best->thing) + 0xC0)) == 7 && SLOT_LOAD(u8 *, (GETP(best->thing) + 0x94)) != NULL &&
                SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(best->thing) + 0x94)) + 0x2C)) != NULL) {
                SETP(((AIData *)ai)->targetThing, SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (GETP(best->thing) + 0x94)) + 0x2C)));
            } else {
                SETP(((AIData *)ai)->targetThing, GETP(best->thing));
            }
            SETP(((AIData *)ai)->targetNav, NULL);
        }
        valid = 1;
    }
    if (valid) {
        ((AIData *)ai)->targetDist = best->dist;
        ((AIData *)ai)->minDist = minDist;
        ((AIData *)ai)->center = center;
        ((AIData *)ai)->range = range;
        ((AIBranch *)branch)->unkC = GETP(((AIBranch *)branch)->def)->unk34;
        SETP(((AIBranch *)branch)->target, GETP(best->thing));
    }
    return valid;
}
s8 AI_Car_EnterState(u8 *ai, u8 *branch, s32 slot) {
    typedef struct { PTR32(u8) info; u32 timer0; u32 timer1; u32 timer2; u8 pad10[4]; u8 count; } AIBranch;
    extern Vector4 g_zeroVector;
    struct { Vector4 a; Vector4 b; Vector4 c; Vector4 zero; u32 id; PTR32(u8) player; f32 f0; f32 f1; } p __attribute__((aligned(16)));
    u32 id;
    u8 *car;
    u8 i;
    s8 done;

    done = 0;
    if (branch == SLOT_LOAD(u8 *, (u32 *)((ai + 0x3D4)) + ((u8)slot))) {
        if (*(u32 *)(branch + 4) != 0) {
            AI_UpdateTimer((u32 *)PTR(((u32)HND(branch) + 4)));
        }
        if (*(u32 *)(branch + 4) == 0 && (*(u32 *)(GETP(((AIBranch *)branch)->info) + 0x48) != 0 || *(u32 *)(GETP(((AIBranch *)branch)->info) + 0x4C) != 0)) {
            done = 1;
        } else if (!(s8)AI_Car_CheckLocalStats(ai, GETP(((AIBranch *)branch)->info))) {
            done = 1;
        } else if (!AI_Car_DetermineTarget(ai, branch, slot)) {
            done = 1;
        }
        if (done) {
            branch = NULL;
        }
    } else {
        if (GETP(((AIBranch *)branch)->info)[6] != 0 && ((AIBranch *)branch)->count == 0) {
            return 0;
        }
        if (((AIBranch *)branch)->timer1 != 0) {
            return 0;
        }
        if (!(s8)AI_Car_CheckLocalStats(ai, GETP(((AIBranch *)branch)->info))) {
            return 0;
        }
        if (!AI_Car_DetermineTarget(ai, branch, slot)) {
            return 0;
        }
        if (((AIBranch *)branch)->count != 0) {
            ((AIBranch *)branch)->count--;
        }
        id = *(u32 *)(GETP(((AIBranch *)branch)->info) + 0x5C);
        if (id != 0) {
            p.id = id;
            car = SLOT_LOAD(u8 *, (ai + 0x2C));
            p.a = *(Vector4 *)(car + 0x40);
            p.b = *(Vector4 *)(car + 0x50);
            p.c = *(Vector4 *)(car + 0x0);
            SETP(p.player, ai);
            p.f0 = 0.0f;
            p.zero = g_zeroVector;
            ThingManager_DARYL_CreateThing(1, &p);
        }
        ((AIBranch *)branch)->timer0 = RandomAdd_s32(*(s32 *)(GETP(((AIBranch *)branch)->info) + 0x38), *(s32 *)(GETP(((AIBranch *)branch)->info) + 0x3C));
        done = 1;
    }
    if (done) {
        for (i = slot; i < 4; i++) {
            if (SLOT_LOAD(AIBranch *, (ai + 0x3D4 + i * 4)) == NULL) {
                break;
            }
            (SLOT_LOAD(AIBranch *, (ai + 0x3D4 + i * 4)))->timer1 = RandomAdd_s32(*(s32 *)(GETP((SLOT_LOAD(AIBranch *, (ai + 0x3D4 + i * 4)))->info) + 0x48), *(s32 *)(GETP((SLOT_LOAD(AIBranch *, (ai + 0x3D4 + i * 4)))->info) + 0x4C));
            SLOT_STORE((ai + 0x3D4 + i * 4), NULL);
        }
    }
    return branch != NULL;
}

s8 AI_Car_DetermineStateBranch(u8 *car, void *arg, s8 skip) {
    typedef struct AIStateInfo {
        u8 unk0[6];
        u8 unk6;
    } AIStateInfo;
    typedef struct AIState {
        PTR32(AIStateInfo) info;
        u32 numChildren;
        PTR32(struct AIState) children;
    } AIState;
    typedef struct AIStateSlot {
        PTR32(AIStateInfo) info;
        s32 unk4;
        s32 unk8;
        s32 unkC;
        u8 unk10[4];
        u8 unk14;
    } AIStateSlot;
    typedef struct AICar {
        u8 pad0[0xD0];
        PTR32(u8) params;
        AIStateSlot slots[32];
        PTR32(AIStateSlot) active[4];
        PTR32(AIStateSlot) current;
    } AICar;
    static u8 iTotal;
    static u8 iValid;
    AICar *ai = (AICar *)car;
    AIState *state = arg;
    AIStateInfo *found = NULL;
    u8 i;

    if (state == (AIState *)(GETP(ai->params) + 0x28)) {
        iTotal = 0;
        iValid = 0;
    }
    if (GETP(state->info) != NULL) {
        if (!skip && AI_Car_EnterState(car, (u8 *)&ai->slots[iTotal], iValid)) {
            found = GETP(state->info);
            SETP(ai->active[iValid++], &ai->slots[iTotal]);
        }
        iTotal++;
    }
    skip = (!skip && (GETP(state->info) == NULL || found != NULL)) == 0;
    for (i = 0; i < state->numChildren; i++) {
        if (AI_Car_DetermineStateBranch(car, &GETP(state->children)[i], skip)) {
            return 1;
        }
    }
    if (skip) {
        return 0;
    }
    if (found != NULL) {
        SETP(ai->current, GETP(ai->active[iValid - 1]));
        SETP(ai->active[iValid], NULL);
        return 1;
    }
    return 0;
}

s32 AI_Car_DetermineState(u8 *car) {
    typedef struct { PTR32(u8) info; u32 timer0; u32 timer1; u32 timer2; u8 pad10[8]; } AIBranch;
    typedef struct { u8 pad0[0xD4]; AIBranch branches[32]; } AICarBranches;
    typedef s32 (*GetNavHolderFn)(void *thing);   /* nearest nav point index */
    u32 i;
    u32 prev;

    if (SLOT_LOAD(u8 *, (car + 0x3E4)) != NULL) {
        prev = *(u32 *)(SLOT_LOAD(u8 *, (car + 0x3E4)) + 0x10);
    } else {
        prev = 0;
    }
    for (i = 0; i < 32; i++) {
        if (GETP(((AICarBranches *)car)->branches[i].info) == NULL) {
            break;
        }
        AI_UpdateTimer(&((AICarBranches *)car)->branches[i].timer0);
        AI_UpdateTimer(&((AICarBranches *)car)->branches[i].timer1);
        if (GETP(((AICarBranches *)car)->branches[i].info)[5] != 0) {
            AI_UpdateTimer(&((AICarBranches *)car)->branches[i].timer2);
        }
    }
    if (AI_Car_DetermineStateBranch(car, SLOT_LOAD(u8 *, (car + 0xD0)) + 0x28, 0)) {
        if (*(u32 *)(SLOT_LOAD(u8 *, (car + 0x3E4)) + 0x10) != prev) {
            if (*(u32 *)(car + 0x3EC) != 0) {
                *(u32 *)(car + 0x4A4) = 0x20;
                *(u32 *)(car + 0xC8) = 1;
            } else if (SLOT_LOAD(u8 *, (car + 0x3F0)) != NULL) {
                AI_Car_PrepareNavPath(car,
                    SLOT_LOAD(GetNavHolderFn, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x2C)) + 0x8C)) + 0x2C))(SLOT_LOAD(u8 *, (car + 0x2C))),
                    *(s32 *)(SLOT_LOAD(u8 *, (car + 0x3F0)) + 0x28));
            }
        }
        return 1;
    }
    return 0;
}

typedef struct AIResetStateInfo {
    u8 unk0[6];
    u8 unk6;
} AIResetStateInfo;
typedef struct AIResetState {
    PTR32(AIResetStateInfo) info;
    u32 numChildren;
    PTR32(struct AIResetState) children;
} AIResetState;
typedef struct AIResetStateSlot {
    PTR32(AIResetStateInfo) info;
    s32 unk4;
    s32 unk8;
    s32 unkC;
    u8 unk10[4];
    u8 unk14;
} AIResetStateSlot;
typedef struct AIResetParams {
    u8 pad0[0x28];
    AIResetState state;
} AIResetParams;
typedef struct AIResetCar {
    u8 pad0[0xD0];
    PTR32(AIResetParams) params;
    AIResetStateSlot slots[1];
} AIResetCar;

#pragma inline_max_size(600)
void AI_Car_ResetState(AIResetCar *car, AIResetState *state) {
    static u8 iTotal;
    u8 i;

    if (state == &GETP(car->params)->state) {
        iTotal = 0;
    }
    if (GETP(state->info) != NULL) {
        SETP(car->slots[iTotal].info, GETP(state->info));
        car->slots[iTotal].unk4 = 0;
        car->slots[iTotal].unk8 = 0;
        car->slots[iTotal].unkC = 0;
        car->slots[iTotal].unk14 = GETP(state->info)->unk6;
        iTotal++;
    }
    for (i = 0; i < state->numChildren; i++) {
        AI_Car_ResetState(car, &GETP(state->children)[i]);
    }
    if (state == &GETP(car->params)->state) {
        SETP(car->slots[iTotal].info, NULL);
    }
}


void AI_Car_Reset(u8 *car) {
    typedef struct AICarParams {
        f32 speedScale;
        f32 gripScale;
        u8 unk8[4];
        f32 unkC;
        u8 unk10[0x18];
        u8 state[1];
    } AICarParams;
    typedef struct AICarTarget {
        int value;
        u8 unk4[0x10];
    } AICarTarget;
    typedef struct AICarTargets {
        u8 unk0[0x4B0];
        AICarTarget targets[16];   /* 0x4B0 (asm); the decomp had them at 0 */
    } AICarTargets;
    register u8 *vehicle;
    u16 i;

    AI_Reset(car);
    if (SLOT_LOAD(AICarParams *, (car + 0xD0)) != NULL) {
        *(f32 *)(car + 0x63C) = (SLOT_LOAD(AICarParams *, (car + 0xD0)))->unkC;
        vehicle = SLOT_LOAD(u8 *, (car + 0x2C));
        if (vehicle != NULL) {
            *(s32 *)(vehicle + 0x78) = *(s32 *)(vehicle + 0x78) * (SLOT_LOAD(AICarParams *, (car + 0xD0)))->speedScale;
            *(f32 *)(SLOT_LOAD(u8 *, (car + 0x2C)) + 0x468) *= (SLOT_LOAD(AICarParams *, (car + 0xD0)))->gripScale;
        }
        AI_Car_ResetState(car, (SLOT_LOAD(AICarParams *, (car + 0xD0)))->state);
    }
    *(s32 *)(car + 0x3EC) = 0;
    *(s32 *)(car + 0x3F0) = 0;
    *(s32 *)(car + 0x5F0) = 0;
    *(s32 *)(car + 0x5F4) = 0;
    *(s32 *)(car + 0x4A8) = -1;
    *(s32 *)(car + 0x4A4) = 0;
    *(s32 *)(car + 0x4AC) = 0;
    *(s32 *)(car + 0x4A0) = -2;
    memset(car + 0x420, 0, 0x80);
    for (i = 0; i < 16; i++) {
        ((AICarTargets *)car)->targets[i].value = 0;
    }
    *(s32 *)(car + 0x608) = RandomAdd_s32(1000, 1000);
    *(s32 *)(car + 0x3E8) = 0;
    *(s32 *)(car + 0x3E4) = 0;
    *(s32 *)(car + 0x630) = 0;
    *(s32 *)(car + 0x634) = -120;
    *(s32 *)(car + 0x654) = 1;
    *(s32 *)(car + 0x658) = 3000;
}

void AI_Car_Init(u8 *car, u8 *params) {

    M2C_FIELD(car, s32 *, 0xD0) = M2C_FIELD(params, s32 *, 0x0);
    AI_Init(car, (s32)HND(params));  /* r3/r4 passed on unchanged (asm) */
    SLOT_STORE((u8 *)(car) + 0xB8, car);
    SLOT_STORE((u8 *)(car) + 0xBC, AI_Car_AsyncHeartbeat);
    M2C_FIELD(car, u8 *, 0xC4) = 0;
    SLOT_STORE((u8 *)(car) + 0x644, car);
    SLOT_STORE((u8 *)(car) + 0x648, AI_Car_WeaponAsyncHeartbeat_80008210);
    M2C_FIELD(car, u8 *, 0x650) = 0;
    SLOT_STORE((u8 *)(car) + 0x5F8, car);
    SLOT_STORE((u8 *)(car) + 0x5FC, AI_Car_AsyncScanDestructibles);
    M2C_FIELD(car, u8 *, 0x604) = 0;
    if (M2C_FIELD(params, s8 *, 0xC)) {
        M2C_FIELD(car, u8 *, 0xCD) = 1;
    } else {
        M2C_FIELD(car, u8 *, 0xCD) = 3;
    }
    M2C_FIELD(car, f32 *, 0x65C) = M2C_FIELD(params, f32 *, 0x4);
    M2C_FIELD(car, f32 *, 0x660) = M2C_FIELD(params, f32 *, 0x8);
    M2C_FIELD(car, s32 *, 0x4A0) = -2;
    memset(car + 0x3D4, 0, 0x60);
    M2C_FIELD(car, u8 *, 0xCC) = 1;
}

void AI_Car_Heartbeat(u8 *ai) {
    typedef struct AICarTarget {
        u32 timer;
        u8 unk4[8];
        f32 unkC;
        f32 unk10;
    } AICarTarget;
    typedef struct AICarTargets {
        u8 unk0[0x4B0];
        AICarTarget targets[16];
    } AICarTargets;
    extern u32 g_iDeltaTimeMS;
    s8 cleared;
    u8 *pos;
    u8 *point;
    u8 *car;
    u16 i;
    s16 j;
    s8 done;

    car = SLOT_LOAD(u8 *, (ai + 0x2C));
    if (car != NULL && SLOT_LOAD(u8 *, (car + 0x8C)) != g_carAVTable) {
        return;
    }
    Control_ClearControl(ai);
    if (!AI_Heartbeat(ai)) {
        return;
    }
    if (*(s8 *)(ai + 0xCC) && car != NULL) {
        AI_Car_Reset(ai);
    }
    if (*(s8 *)(ai + 0xCC)) {
        return;
    }
    if (*(u32 *)(ai + 0xD0) != 0) {
        AI_Car_DetermineState(ai);
        if (*(u32 *)(ai + 0x3E4) == 0 || (*(u32 *)(ai + 0x3EC) == 0 && *(u32 *)(ai + 0x3F0) == 0)) {
            ai[0x13] = 0xFF;
            if ((SLOT_LOAD(u8 *, (car + 0x3BC)))[0x11] == 1) {
                ai[0x12] = 0xFF;
            }
            return;
        }
        if (*(u32 *)(ai + 0x608) != 0 && AI_UpdateTimer((u32 *)(ai + 0x608)) == 0) {
            AI_AddEvent(ai + 0x5F8);
        }
        if (*(u32 *)(ai + 0x4AC) != 0) {
            AI_UpdateTimer((u32 *)(ai + 0x4AC));
        }
        if (*(u32 *)(ai + 0x4A4) < 32 && SLOT_LOAD(u8 *, (u32 *)((ai + 0x420)) + (*(u32 *)(ai + 0x4A4))) != NULL) {
            pos = SLOT_LOAD(u8 *, (ai + 0x2C));
            cleared = 1;
            while (cleared && *(u32 *)(ai + 0x4A4) < 32 && (point = SLOT_LOAD(u8 *, (ai + 0x420 + *(u32 *)(ai + 0x4A4) * 4))) != NULL) {
                cleared = 0;
                if (PSVECSquareDistance(point, pos + 0x40) < *(f32 *)(point + 0x24) * *(f32 *)(point + 0x24)) {
                    AI_Car_ClearedNavPoint_800087C8(ai, 0);
                    cleared = 1;
                }
            }
        }
        for (i = 0; i < 16 && ((AICarTargets *)ai)->targets[i].timer != 0; i++) {
            done = 0;
            while (!done) {
                done = 1;
                if (((AICarTargets *)ai)->targets[i].timer != 0) {
                    ((AICarTargets *)ai)->targets[i].unkC += g_iDeltaTimeMS;
                    ((AICarTargets *)ai)->targets[i].unk10 += g_iDeltaTimeMS;
                    if (((AICarTargets *)ai)->targets[i].unkC > 30000.0f && ((AICarTargets *)ai)->targets[i].unk10 > 30000.0f) {
                        for (j = i; j < 16 && ((AICarTargets *)ai)->targets[j].timer != 0; j++) {
                            if (j == 15) {
                                ((AICarTargets *)ai)->targets[j].timer = 0;
                            } else {
                                ((AICarTargets *)ai)->targets[j] = ((AICarTargets *)ai)->targets[j + 1];
                            }
                        }
                        done = 0;
                    }
                }
            }
        }
        *(u32 *)(ai + 0x5F0) = 0;
        *(u32 *)(ai + 0x5F4) = 0;
        if (*(u32 *)(ai + 0x5F0) != 0) {
            *(u32 *)(ai + 0x5F4) += g_iDeltaTimeMS;
            if (*(u32 *)(ai + 0x5F4) > 20000) {
                *(u32 *)(ai + 0x5F4) = 0;
                *(u32 *)(ai + 0x5F0) = 0;
            }
        }
        if (!AI_Car_CheckIfStuck(ai)) {
            AI_Car_SeekPoint(ai);
        }
        AI_UpdateTimer((u32 *)(ai + 0x658));
        if (*(u32 *)(ai + 0x654) != 0 && AI_UpdateTimer((u32 *)(ai + 0x654)) == 0) {
            AI_AddEvent(ai + 0x644);
        }
    }
}
