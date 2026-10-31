/* The car AI (AICar, AI.h): spawns AI cars from the world's car tiles, picks
 * a state of its script (a tree of AIStateInfo) with a target, paths to it
 * over the nav graph and drives there through the player controls; weapons
 * and obstacle scans run as async events. */
#include "common.h"
#include "CarA.h"
#include "AI.h"

#define CAR_THING(car) ((Thing *)(car))
#define THING_FLAG_FREE 0x1000          /* Thing.flags: in the pool, not spawned */

/* Past sizeof(CarA): the distance left to its goal along the nav path
 * (Player.c's navDist, the target arrow). */
#define CAR_NAV_DIST(car) (*(f32 *)((u8 *)(car) + 0x564))

static inline CarA *AI_Car_Vehicle(AICar *ai) {
    return GETP(AI_PLAYER(ai)->vehicle);
}

/* CarAInfo.driveMode of the AI's car (1 and 2 drive differently below). */
static inline u8 AI_Car_DriveMode(AICar *ai) {
    return GETP(AI_Car_Vehicle(ai)->vehicleIface)->driveMode;
}

/* path[i], NULL past the end. */
static inline NavPoint *AI_Car_PathPoint(AICar *ai, u32 i) {
    return i < 32 ? GETP(ai->path[i]) : NULL;
}

static inline AICarScript *AI_Car_Script(AICar *ai) {
    return GETP(ai->base.script);
}

/* The nearest nav point of a thing (vtable). */
static inline s32 AI_Car_NearestNavPoint(Thing *thing) {
    return THING_VFN(thing, THING_V_GET_NEAREST_NAV_POINT, s32 (*)(Thing *))(thing);
}

static inline s8 AI_Car_CanTarget(AICar *ai, AIStateInfo *def) {
    s8 ok = 1;

    if (ai == NULL) {
        ok = 0;
    } else if (def->subtype == AI_SUBTYPE_NO_TARGET) {
        ok = 0;
    }
    return ok;
}

/* AI_SUBTYPE_TARGET_LIMITS: whether `thing` passes the limits, by its
 * record (records[] ends at the first empty one). Written out twice in the
 * decomp (AI_Car_IsTargetValid, AI_Car_DetermineTarget). */
static inline s8 AI_Car_PassesLimits(AICar *ai, AITargetLimits *limits, void *thing) {
    AITargetRecord *rec;
    s8 valid = limits->mode != 1;
    s16 i;

    for (i = 0; i < 16 && GETP(ai->records[i].thing) != NULL; i++) {
        rec = &ai->records[i];
        if (GETP(rec->thing) == thing) {
            if (limits->compare == 1) {
                if ((limits->limit[2] > 0 && rec->stats[2] > limits->limit[2]) ||
                    (limits->limit[3] > 0 && rec->stats[3] > limits->limit[3]) ||
                    (limits->limit[0] > 0 && rec->stats[0] > limits->limit[0]) ||
                    (limits->limit[1] > 0 && rec->stats[1] > limits->limit[1])) {
                    valid = limits->mode == 1;
                }
            } else {
                if ((limits->limit[2] == 0 || rec->stats[2] > limits->limit[2]) &&
                    (limits->limit[3] == 0 || rec->stats[3] > limits->limit[3]) &&
                    (limits->limit[0] == 0 || rec->stats[0] > limits->limit[0]) &&
                    (limits->limit[1] == 0 || rec->stats[1] > limits->limit[1])) {
                    valid = limits->mode == 1;
                }
            }
            break;
        }
    }
    return valid;
}

/* Where to aim at an item: itself, or for a carried item the car of its
 * carrier, if that is a human (NULL otherwise). */
static inline Vector4 *AI_Car_ItemPos(ItemThing *item) {
    Thing *carrier;

    if (GETP(item->record)->type == ITEM_TYPE_CARRIED && GETP(item->base.player) != NULL &&
        GETP(((Player *)GETP(item->base.player))->vehicle) != NULL) {
        if (Player_IsHuman(GETP(item->base.player))) {
            carrier = GETP(((Player *)GETP(item->base.player))->vehicle);
            return &carrier->pos;
        }
        return NULL;
    }
    return &item->base.pos;
}

/* Spawns at most one AI car per call, cycling over the car tiles: a tile with
 * scripts, no car yet and a mode spawns when the nearest camera is between
 * its near and far distances. */
void AI_Car_AsyncProcess(void) {
    /* ThingManager_DARYL_CreateThing(9) parameters */
    typedef struct CarSpawnParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        PTR32(AIScriptSet) aiScripts;
        u8 pad34[0x3C - 0x34];
        PTR32(void) player;
    } CarSpawnParams;
    static u32 carTile;
    extern Vector4 g_zeroVector;
    CarSpawnParams params;
    Matrix4 mtx;
    u32 action[3];
    AIPlayerParams desc;
    TileCar *tile;
    CarA *car;
    AIScriptSet *scripts;
    u32 script;
    s32 difficulty;
    u32 i;
    f32 farSq;
    f32 nearSq;
    f32 minDist;
    void *rack;
    Player *player;

    tile = World_GetTileCar(carTile);
    if (tile != NULL) {
        if (GETP(tile->aiScripts) != NULL && GETP(tile->car) == NULL && tile->port > 4 && tile->dynamic != 0 && tile->mode != 0) {
            farSq = tile->farDist * tile->farDist;
            nearSq = tile->nearDist * tile->nearDist;
            minDist = farSq;
            difficulty = GameSettings_GetDifficulty();
            SETP(params.aiScripts, GETP(tile->aiScripts));
            scripts = GETP(params.aiScripts);
            script = 0;
            for (i = 0; i < AI_NUM_CAMERAS; i++) {
                f32 dist = PSVECSquareDistance(&AI_CAMERA(i)->pos, &tile->pos);
                if (dist < minDist) {
                    minDist = dist;
                }
            }
            if (minDist <= nearSq || minDist >= farSq) {
                carTile++;
                return;
            }
            /* the tile's script for the difficulty, else the set's; the set
             * is used only if its easy script is there (all difficulties) */
            if (difficulty == 0) {
                if (tile->scripts[0] != 0) {
                    script = tile->scripts[0];
                } else if (scripts->scripts[0] != 0) {
                    script = scripts->scripts[0];
                }
            } else if (difficulty == 1) {
                if (tile->scripts[1] != 0) {
                    script = tile->scripts[1];
                } else if (scripts->scripts[0] != 0) {
                    script = scripts->scripts[1];
                }
            } else if (difficulty == 2) {
                if (tile->scripts[2] != 0) {
                    script = tile->scripts[2];
                } else if (scripts->scripts[0] != 0) {
                    script = scripts->scripts[2];
                }
            } else if (difficulty == 3) {
                if (tile->scripts[3] != 0) {
                    script = tile->scripts[3];
                } else if (scripts->scripts[0] != 0) {
                    script = scripts->scripts[3];
                }
            }
            if (script == 0) {
                if (scripts->scripts[0] != 0) {
                    script = scripts->scripts[0];
                } else if (scripts->scripts[1] != 0) {
                    script = scripts->scripts[1];
                } else if (scripts->scripts[2] != 0) {
                    script = scripts->scripts[2];
                } else {
                    script = scripts->scripts[3];
                }
            }
            desc.script = script;
            desc.kind = AI_KIND_CAR;
            desc.dynamic = tile->dynamic;
            desc.nearSq = tile->nearDist * tile->nearDist;
            desc.farSq = tile->farDist * tile->farDist;
            player = PlayerManager_NewAIPlayer((u8 *)&desc);
            if (player == NULL) {
                carTile++;
                return;
            }
            SETP(player->tileCar, tile);
            params.pos = tile->pos;
            params.pos.y += 10.0f;
            SETP(params.player, player);
            params.vel = g_zeroVector;
            SetOrientation(&mtx, tile->rotY, tile->rotX, tile->rotZ);
            SubMatrixToQuaternion(&mtx, &params.quat);
            SETP(tile->car, ThingManager_DARYL_CreateThing(9, &params));
            if (GETP(tile->car) == NULL) {
                PlayerManager_RemovePlayer(player);
                carTile++;
                return;
            }
            car = GETP(tile->car);
            SETP(CAR_THING(car)->trigger, tile);
            Player_SetName(player, -1, "Dynamic AI");
            Player_SetThing(player, car);
            SETP(player->aiScripts, GETP(tile->aiScripts));
            SETP(player->tileCar, tile);
            if (GETP(tile->item) != NULL) {
                rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
                WildCardRack_JettisonAll(rack);
                WildCardRack_Add(rack, (u8 *)GETP(tile->item), GETP((GETP(tile->item))->weapon)->ammo);
            }
            action[0] = tile->mode;
            action[1] = 1;
            action[2] = (u32)HND(tile);
            ActionVehicleChangeMode(action);
        }
        carTile++;
    } else {
        carTile = 0;
    }
}

/* Unsticks the car: once it hasn't moved for 10 frames it turns at full lock
 * for 120 frames (reversing, by drive mode), then holds off for 120 frames.
 * Returns 1 while it is doing that (the controls are set). */
s32 AI_Car_CheckIfStuck(AICar *ai) {
    CarA *car = AI_Car_Vehicle(ai);
    Player *pl = AI_PLAYER(ai);
    s32 timer;
    u8 mode;
    f32 dot;

    if (car->stuckCounter > 600) {
        pl->stickX = 0x7F;
        return 1;
    }
    timer = ai->stuckTimer;
    if (timer == 0) {
        if (car->lowSpeedFlag < 500 && PSVECSquareDistance(&ai->lastPos, &CAR_THING(car)->pos) < 0.5f) {
            ai->stillFrames++;
            if (ai->stillFrames >= 10) {
                ai->stuckTimer = 120;
                ai->steer = AI_AngleToTarget(car, &ai->steerPoint) < 0.0f ? -127 : 127;
                ai->stuckForward = CAR_THING(car)->axis[2];
            }
        } else {
            ai->stillFrames = 0;
        }
        ai->lastPos = CAR_THING(car)->pos;
        if (ai->stuckTimer == 0) {
            return 0;
        }
    } else if (timer < 0) {
        ai->stuckTimer = timer + 1;
        return 0;
    }
    pl->stickX = ai->steer;
    ai->stuckTimer--;
    if (ai->stuckTimer == 0) {
        ai->stuckTimer = -120;
    }
    mode = AI_Car_DriveMode(ai);
    if (mode == 1) {
        /* reverse until turned by 30 degrees */
        pl->brake = 0xFF;
        dot = PSVECDotProduct(&CAR_THING(car)->axis[2], &ai->stuckForward);
        if (dot > 1.0f) {
            dot = 1.0f;
        }
        if (dot < -1.0f) {
            dot = -1.0f;
        }
        if (30.0 <= 57.29578f * acos(dot)) {
            ai->stuckTimer = -120;
        }
    } else if (mode == 2) {
        pl->button14 = 0xFF;
        dot = PSVECDotProduct(&CAR_THING(car)->axis[2], &ai->stuckForward);
        if (dot > 1.0f) {
            dot = 1.0f;
        }
        if (dot < -1.0f) {
            dot = -1.0f;
        }
        if (45.0 <= 57.29578f * acos(dot)) {
            ai->stuckTimer = -180;
        }
    }
    return 1;
}

/* Sets the controls to drive towards steerPoint: steering from the angle to
 * it and the car's yaw rate, throttle and brake slowing down near `target`
 * (the current state's approach distances) and above `speed` (0: none;
 * speedLimit caps it). */
void AI_Car_DriveToTarget(AICar *ai, Vector4 *target, f32 speed) {
    Vector4 angVel __attribute__((aligned(16)));
    Player *pl = AI_PLAYER(ai);
    f32 dist;
    f32 angle;
    CarA *car;
    AIStateInfo *info;
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
    u8 throttle;    /* Player.throttle */
    u8 button15;    /* with full lock: a sliding turn */
    u8 brake;       /* Player.brake */

    car = AI_Car_Vehicle(ai);
    throttle = 0;
    button15 = 0;
    brake = 0;
    distSq = PSVECSquareDistance(&ai->steerPoint, &CAR_THING(car)->pos);
    velSq = PSVECSquareMag(&CAR_THING(car)->vel);
    vel = Sqrt(velSq);
    angle = AI_AngleToTarget(car, &ai->steerPoint);
    deg = 57.29578f * angle;
    if (__fabs(deg) <= 5.0 || 0.0f == velSq) {
        /* straight ahead */
        throttle = 0xFF;
        ai->steer = (s8)(-6.0f * deg);
        pl->stickX = ai->steer;
    } else if (velSq < 1600.0f && __fabs(deg) >= 100.0 && __fabs(deg) <= 125.0 && distSq < 900.0f) {
        /* slow, near and well off to the side: reverse round (as when stuck) */
        brake = 0xFF;
        if (AI_Car_DriveMode(ai) == 1) {
            ai->stuckTimer = 80;
        } else {
            ai->stuckTimer = 120;
        }
        ai->steer = deg < 0.0f ? -127 : 127;
        ai->stuckForward = CAR_THING(car)->axis[2];
    } else if (velSq > 1600.0f && __fabs(deg) >= 45.0 && distSq < 5625.0f) {
        brake = 0xFF;
        button15 = 0xFF;
        ai->steer = deg < 0.0f ? 127 : -127;
        pl->stickX = ai->steer;
    } else {
        /* steer so the yaw rate turns the car onto the point in the time
         * it takes to get there */
        t = Sqrt(distSq / velSq);
        CarA_GetAngularVelocity(car, &angVel);
        yaw = PSVECDotProduct(&CAR_THING(car)->axis[1], &angVel);
        if (yaw <= 0.01f) {
            yaw = 0.01f;
        }
        r = angle / yaw;
        if (r < 0.0f) {
            r = -r;
            r += yaw * (yaw >= 0.0f ? 3.0f : -3.0f);
        }
        r /= t;
        if (ai->steer == 0 || ((ai->steer << 16) ^ *(s32 *)&angle) < 0) {
            ai->steer = -40.425358f * angle;
        }
        full = angle > 0.0f ? -127 : 127;
        if (r <= 1.0f) {
            throttle = 0xFF;
            ai->steer = r * (f32)(full - ai->steer) + (f32)ai->steer;
        } else {
            s = r * (f32)ai->steer;
            if (_fabs(s) < _fabs(full)) {
                ai->steer = s;
                throttle = 0xFF;
            } else {
                ai->steer = full;
                button15 = 0xFF;
                throttle = 0xFF;
            }
        }
    }
    DistanceXYZ(&CAR_THING(car)->pos, target, &dist);
    info = GETP(GETP(ai->current)->info);
    far = info->approachFar;
    near = info->approachNear;
    mid = (far - near) / 2.0f + near;
    if (dist < mid && brake != 0xFF) {
        v = 255 - (s32)(255.0f * ((dist - near) / (mid - near)));
        if (v > 255) {
            v = 255;
        }
        if (brake < v) {
            brake = v;
        }
        throttle = 0;
    } else if (dist < far && throttle) {
        v = 255.0f * ((dist - mid) / (far - mid));
        if (throttle > v) {
            throttle = v;
        }
    }
    if (ai->speedLimit && (!speed || ai->speedLimit < speed)) {
        speed = ai->speedLimit;
    }
    margin = 0.1f * speed;
    hi = speed + margin;
    lo = speed - margin;
    if (speed && vel > speed && brake != 0xFF) {
        v = 255.0f * ((vel - speed) / (hi - speed));
        if (v > 255) {
            v = 255;
        }
        if (brake < v) {
            brake = v;
        }
        throttle = 0;
    } else if (speed && vel > lo && throttle) {
        v = 255 - (s32)(255.0f * ((vel - lo) / (speed - lo)));
        if (throttle > v) {
            throttle = v;
        }
    }
    if (velSq < 100.0f || car->gradeOrReverse < 0.0f) {
        throttle = 0xFF;
        brake = 0;
    }
    if (ai->steer > 127) {
        ai->steer = 127;
    } else if (ai->steer < -127) {
        ai->steer = -127;
    }
    pl->stickX = ai->steer;
    if (throttle) {
        pl->throttle = throttle;
    } else if (brake) {
        pl->brake = brake;
    }
    if (button15) {
        pl->button15 = button15;
    }
}

/* steerPoint for a moving target (the obstacle, else targetThing): where it
 * will be when the car gets there; drive mode 1 aims 15 to its side (to hit
 * it side-on). Halved towards the car beyond 20. */
static inline void AI_Car_LeadTarget(AICar *ai) {
    Vector4 lead;
    Vector4 *myPos;
    Thing *thing;
    Vector4 *thingPos;
    CarA *car;
    f32 dist;
    f32 t;

    myPos = &CAR_THING(AI_Car_Vehicle(ai))->pos;
    thing = GETP(ai->obstacle) != NULL ? GETP(ai->obstacle) : GETP(ai->targetThing);
    thingPos = &thing->pos;
    if (PSVECSquareMag(&thing->vel) < 1.0f) {
        ai->steerPoint = *thingPos;
    } else {
        car = AI_Car_Vehicle(ai);
        dist = PSVECSquareDistance(thingPos, myPos);
        t = Sqrt(dist / PSVECSquareMag(&CAR_THING(car)->vel));
        PSVECScale(&thing->vel, &lead, t);
        PSVECAdd(&lead, thingPos, &lead);
        PSVECScale(&thing->vel, &lead, t * Sqrt(PSVECSquareDistance(&lead, myPos) / dist));
        PSVECAdd(&lead, thingPos, &ai->steerPoint);
        if (AI_Car_DriveMode(ai) == 1) {
            PSVECSubtract(&ai->steerPoint, myPos, &lead);
            PSVECScale(&thing->axis[0], &lead,
                       PSVECDotProduct(&CAR_THING(AI_Car_Vehicle(ai))->axis[0], &lead) >= 0.0f ? -15.0f : 15.0f);
            PSVECAdd(&ai->steerPoint, &lead, &ai->steerPoint);
        }
    }
    if (PSVECSquareDistance(myPos, &ai->steerPoint) > 400.0f) {
        PSVECSubtract(&ai->steerPoint, myPos, &ai->steerPoint);
        PSVECScale(&ai->steerPoint, &ai->steerPoint, 0.5f);
        PSVECAdd(&ai->steerPoint, myPos, &ai->steerPoint);
    }
}

/* steerPoint for following the path at point i: cuts the corner towards the
 * point after it (or halfway to the point, if it is the last). */
static inline void AI_Car_SteerAlongPath(AICar *ai, u32 i) {
    Vector4 a;
    Vector4 b;
    Vector4 *myPos;
    Vector4 *p0;
    Vector4 *p1;
    f32 ma;
    f32 mb;

    p0 = &GETP(ai->path[i])->pos;
    myPos = &CAR_THING(AI_Car_Vehicle(ai))->pos;
    if (i + 1 >= 32 || GETP(ai->path[i + 1]) == NULL) {
        PSVECSubtract(myPos, p0, &ai->steerPoint);
        PSVECScale(&ai->steerPoint, &ai->steerPoint, 0.5f);
        PSVECAdd(&ai->steerPoint, p0, &ai->steerPoint);
    } else {
        p1 = &GETP(ai->path[i + 1])->pos;
        PSVECSubtract(myPos, p0, &a);
        PSVECSubtract(p1, p0, &b);
        ma = MagnitudeVector(&a);
        mb = MagnitudeVector(&b);
        PSVECScale(&a, &a, mb / ma);
        PSVECScale(&b, &b, ma / mb);
        PSVECScale(&a, &a, -1.0f);
        PSVECAdd(&a, &b, &ai->steerPoint);
        PSVECScale(&ai->steerPoint, &ai->steerPoint, -(0.5f * ma / MagnitudeVector(&ai->steerPoint)));
        PSVECAdd(&ai->steerPoint, p0, &ai->steerPoint);
    }
}

/* Drives to the obstacle, else along the path, else to targetThing; with
 * none of them it just brakes (and targetNav is dropped). */
static void AI_Car_SeekPoint(AICar *ai) {
    Player *pl = AI_PLAYER(ai);
    Vector4 target;
    f32 speed;
    u32 i;

    speed = 0.0f;
    if (GETP(ai->obstacle) != NULL) {
        AI_Car_LeadTarget(ai);
    } else if ((i = ai->pathIndex) < 32 && GETP(ai->path[i]) != NULL) {
        AI_Car_SteerAlongPath(ai, i);
        speed = GETP(ai->path[ai->pathIndex])->speed;
    } else if (GETP(ai->targetThing) != NULL) {
        AI_Car_LeadTarget(ai);
    } else {
        pl->brake = 0xFF;
        if (AI_Car_DriveMode(ai) == 1) {
            pl->throttle = 0xFF;
        }
        SETP(ai->targetNav, NULL);
        return;
    }
    if (GETP(ai->targetThing) != NULL) {
        target = GETP(ai->targetThing)->pos;
    } else if (AI_Car_PathPoint(ai, ai->pathIndex) != NULL) {
        target = GETP(ai->path[ai->pathIndex])->pos;
    } else if (GETP(ai->obstacle) != NULL) {
        target = GETP(ai->obstacle)->pos;
    }
    AI_Car_DriveToTarget(ai, &target, speed);
}

/* Async: picks the weapon for the target (any nearest car with code
 * variable 0x12D >= 3, else obstacle, targetThing if a car or wildcard
 * item): side attacks when close, fires the selected weapon when it is the
 * best for range/angle/priority (a heal weapon when low on health), else
 * switches; then waits a reload time. */
static void AI_Car_WeaponAsyncHeartbeat_80008210(AICar *ai) {
    extern u32 g_attackDelay;
    extern u8 g_itemVTable[];
    static const char g_jetBoostersName_800CC3E4[] = "Jet Boosters";  /* @832 in .rodata */
    Player *pl = AI_PLAYER(ai);
    AICarScript *script = AI_Car_Script(ai);
    Thing *cars[8];
    Thing *target;
    Thing *thing;
    CarA *car;
    AIWeaponRack *holder;
    WeaponInfo *params;
    s16 i;
    s8 lowHealth;
    s16 best;
    s16 preferred;
    AIWeaponSlot *slot;
    WeaponInfo *reload;
    ItemRecord *def;
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
    car = AI_Car_Vehicle(ai);
    if (car == NULL) {
        return;
    }
    if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
        numCars = ThingManager_GetActiveCars((void **)cars, 8);
        nearest = 1e10f;
        for (n = 0; n < numCars; n++) {
            if (cars[n] != CAR_THING(car) && !(cars[n]->flags & THING_FLAG_FREE) &&
                (GameSettings_GetCodeVariable(0x12D, 0) != 4 || Player_IsHuman(GETP(cars[n]->player)))) {
                distSq = PSVECSquareDistance(&CAR_THING(car)->pos, &cars[n]->pos);
                if (nearest > distSq) {
                    nearest = distSq;
                    target = cars[n];
                }
            }
        }
    } else if (GETP(ai->obstacle) != NULL) {
        target = GETP(ai->obstacle);
    } else if ((thing = GETP(ai->targetThing)) != NULL &&
               (GETP(thing->vtable) == (u32 *)g_carAVTable ||
                (GETP(thing->vtable) == (u32 *)g_itemVTable && GETP(((ItemThing *)thing)->record)->type == 2))) {
        target = thing;
    }
    car = AI_Car_Vehicle(ai);
    if (target != NULL && car != NULL) {
        targetPos = &target->pos;
        best = -1;
        preferred = -1;
        distSq = PSVECSquareDistance(&CAR_THING(car)->pos, targetPos);
        dist = Sqrt(distSq);
        angle = 57.29578f * AI_AngleToTarget(car, targetPos);
        if (AI_Car_DriveMode(ai) == 1 && g_attackDelay == 0 && distSq < 400.0f) {
            if (AI_CheckAngleInRange(angle, 90.0f, 12.5f)) {
                pl->button10 = 0xFF;
                pl->buttonsHeld |= 1;
            } else if (AI_CheckAngleInRange(angle, 270.0f, 12.5f)) {
                pl->button1A = 0xFF;
                pl->buttonsHeld |= 0x400;
            }
        }
        if ((f32)CAR_THING(car)->health <
            0.3f * ((f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth * script->healthScale)) {
            lowHealth = 1;
        }
        holder = ThingManagerList_GetByType(&car->motionState.slaves, 8);
        for (i = 0; holder != NULL && i < 12; i++) {
            slot = &holder->slots[i];
            if (slot == NULL || GETP(slot->record) == NULL) {
                continue;
            }
            params = GETP((GETP(slot->record))->weapon);
            if (slot->ammo < params->ammoPerShot) {
                continue;
            }
            type = GETP(slot->record)->type;
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
            if (best != -1 && params->priority != script->preferredWeapon &&
                GETP((GETP(holder->slots[best].record))->weapon)->priority <= params->priority) {
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
            if (params->priority == script->preferredWeapon) {
                preferred = i;
            }
        }
        if (preferred != -1) {
            best = preferred;
        }
        if (holder->current == best) {
            def = GETP(holder->slots[best].record);
            if (g_attackDelay == 0 || RandomAdd_s32(0, 50) == 0 || def->type != 3 ||
                strcmp(GETP(GETP((def)->names)[0]), g_jetBoostersName_800CC3E4) == 0) {
                ((Player *)(pl))->buttonFire = 0xFF;
                pl->buttonsHeld |= 0x800;
            }
        } else if (best != -1 && ai->weaponSwitchTimer == 0) {
            ((Player *)(pl))->buttonSwitch = 0xFF;
            pl->buttonsHeld |= 2;
            ai->weaponSwitchTimer = 3000;
        }
        reload = NULL;
        if (best != -1) {
            reload = GETP((GETP(holder->slots[best].record))->weapon);
        } else if (GETP(holder->slots[holder->current].record) != NULL) {
            reload = GETP((GETP(holder->slots[holder->current].record))->weapon);
        }
        if (reload != NULL) {
            ai->weaponTimer = (f32)reload->reloadTime * RandomAdd(script->reloadScale[0], script->reloadScale[1]);
        }
    }
    if (ai->weaponTimer == 0) {
        ai->weaponTimer = 300.0f * RandomAdd(script->reloadScale[0], script->reloadScale[1]);
    }
}

/* The car reached path[pathIndex] (initial: just set the path timer): moves
 * on along the way point chain if it was the next way point, and to the next
 * point; repaths when 30 points in. Then sets pathTimer by the speed ahead. */
static void AI_Car_ClearedNavPoint_800087C8(AICar *ai, s8 initial) {
    NavPoint *point;
    NavPoint *nav;
    Thing *thing;
    AIStateSlot *current;
    s32 holder;   /* port: nearest nav point index (vtable 0x2C); the decomp keeps it in a void * */
    s32 dest;
    f32 dist;

    if (!initial) {
        if ((u32)ai->wayPoint < NavHolder_GetTotalNavPoints()) {
            nav = NavHolder_GetNavPoint(ai->wayPoint);
            point = GETP(ai->path[ai->pathIndex]);
            if (point == nav) {
                if (point->type == NAV_TYPE_WAYPOINT) {
                    ai->wayPoint = point->nextWayPoint;
                } else {
                    ai->wayPoint = -1;
                }
            }
        }
        ai->pathIndex++;
        if (GETP(ai->current) != NULL && ai->pathIndex >= 30) {
            holder = AI_Car_NearestNavPoint(CAR_THING(AI_Car_Vehicle(ai)));
            current = GETP(ai->current);
            if (GETP(current->info)->type == AI_TARGET_NAV_POINT || GETP(current->info)->type == AI_TARGET_WAY_POINT) {
                dest = ((NavPoint *)GETP(current->target))->index;
            } else {
                thing = GETP(current->target);
                dest = AI_Car_NearestNavPoint(thing);
            }
            AI_Car_PrepareNavPath(ai, holder, dest);
        }
    }
    if (ai->pathIndex < 32) {
        point = GETP(ai->path[ai->pathIndex]);
        if (point != NULL) {
            dist = AI_Car_Script(ai)->speed;
            if (point->speed < dist) {
                dist = point->speed;
            }
            if (dist > 150.0f || 0.0f == dist) {
                ai->pathTimer = 3000;
            } else if (dist > 100.0f) {
                ai->pathTimer = 4500;
            } else {
                ai->pathTimer = 6000;
            }
        }
    }
}

/* Fills path[] with a path between two nav point indices and skips the
 * points the car is already past. 0: no path (pathDest = -1). */
s32 AI_Car_PrepareNavPath(AICar *ai, s32 from, s32 dest) {
    s32 result = 1;
    NavPoint *point;

    memset(ai->path, 0, sizeof(ai->path));
    ai->pathIndex = 0;
    point = NavHolder_FindPath(from, dest);
    if (point != NULL) {
        NavHolder_FillPointerArrayPath(point, ai->path, 32);
        ai->pathDest = dest;
        AI_Car_ClearedNavPoint_800087C8(ai, 1);
        while (ai->pathIndex < 31) {
            NavPoint *a = GETP(ai->path[ai->pathIndex]);
            NavPoint *b = GETP(ai->path[ai->pathIndex + 1]);
            if (a == NULL || b == NULL) {
                break;
            }
            if (LinearPointTest(&CAR_THING(AI_Car_Vehicle(ai))->pos, a, b) < 0.0f) {
                break;
            }
            AI_Car_ClearedNavPoint_800087C8(ai, 0);
        }
    } else {
        result = 0;
        ai->pathDest = -1;
    }
    return result;
}

/* Catch-up for drive mode 1 in way point races: refills the type 3 weapons,
 * then by the distance left to the goal compared with a car driver's (cheat
 * in -1..1, > 0: behind) raises the speed limit and gives turbo, or lowers it. */
static void AI_Car_Cheat_80008A60(AICar *ai) {
    AICarScript *script = AI_Car_Script(ai);
    CarA *car;
    Player *nearest;
    AIWeaponRack *holder;
    AIStateSlot *slot;
    Camera *cam;
    Vector4 *pos;
    u32 i;
    u32 j;
    f32 best;
    f32 dist;
    f32 cheat;

    cheat = 0.0f;
    car = AI_Car_Vehicle(ai);
    nearest = NULL;
    holder = ThingManagerList_GetByType(&car->motionState.slaves, 8);
    for (i = 0; i < 12; i++) {
        if (GETP(holder->slots[i].record) != NULL && GETP(holder->slots[i].record)->type == 3) {
            holder->slots[i].ammo = GETP((GETP(holder->slots[i].record))->weapon)->maxAmmo;
        }
    }
    best = 1e9f;
    pos = &CAR_THING(car)->pos;
    cam = AI_CAMERA(0);
    for (j = 0; j < AI_NUM_CAMERAS; cam++, j++) {
        nearest = GETP(cam->player);
        if (nearest != NULL && GETP(nearest->vehicle) != NULL && ((Player *)(nearest))->type == 9) {
            dist = PSVECSquareDistance(pos, &cam->pos);
            if (best > dist) {
                best = dist;
                nearest = GETP(cam->player);
            }
        }
    }
    /* quirk of the original: `nearest` ends up as the last camera's player */
    if (1e9f != best) {
        for (i = 0; i < 4; i++) {
            slot = GETP(ai->active[i]);
            if (slot == NULL || GETP(slot->info) == NULL) {
                break;
            }
            if (GETP(slot->info)->type == AI_TARGET_WAY_POINT) {
                cheat = (CAR_NAV_DIST(car) - CAR_NAV_DIST(GETP(nearest->vehicle))) / 750.0f;
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
        ai->speedLimit = script->speed * cheat + script->speed;
        car->transmissionFlags |= 0x1000;
        car->turboTime = 10000;
    } else {
        if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
            ai->speedLimit = script->speed - 0.2f * script->speed * -cheat;
        } else {
            ai->speedLimit = script->speed - 0.333f * script->speed * -cheat;
        }
        car->transmissionFlags &= ~0x1000;
        car->turboTime = 0;
    }
}

/* Async, every 500 ms or a state's heartbeat time: removes a dynamic car out
 * of every camera's far distance; repaths to the current target when needed;
 * turns a just-spawned car to face where it goes; runs the cheat. */
void AI_Car_AsyncHeartbeat(AICar *ai) {
    CarA *car;
    s8 repath;
    s32 dest;
    s32 holder;   /* port: nearest nav point index (vtable 0x2C); the decomp keeps it in a void * */
    s32 paused;
    AIStateSlot *target;
    AIStateSlot *slot;
    NavPoint *nav;
    Thing *thing;
    s32 time;
    u32 j;
    TileCar *tac;
    u8 i;
    f32 dist;
    f32 radius;
    f32 angle;

    car = AI_Car_Vehicle(ai);
    if (car != NULL && GETP(CAR_THING(car)->vtable) != (u32 *)g_carAVTable) {
        return;
    }
    AI_AsyncHeartbeat(&ai->base);
    if (ai->base.reset || AI_Car_Vehicle(ai) == NULL) {
        ((Player *)(ai))->buttonFire = 0xFF;
        return;
    }
    if (ai->base.state != AI_STATE_STATIC) {
        dist = ai->farSq;
        j = 0;
        do {
            radius = PSVECSquareDistance(&AI_CAMERA(j)->pos, &CAR_THING(AI_Car_Vehicle(ai))->pos);
            if (radius < dist) {
                dist = radius;
            }
            j++;
        } while (j < AI_NUM_CAMERAS);
        if (dist >= ai->farSq) {
            tac = GETP(AI_PLAYER(ai)->tileCar);
            paused = Trigger_IsTACSystemPaused();
            Trigger_PauseTACSystem(1);
            CarA_Release((void *)AI_Car_Vehicle(ai));
            PlayerManager_RemovePlayer(ai);
            if (tac->respawn) {
                SETP(tac->car, NULL);
            }
            Trigger_PauseTACSystem(paused);
            return;
        }
    }
    /* decomp: also tested &ai->active != NULL (always true) */
    if (GETP(ai->current) != NULL) {
        for (i = 0; i < 4; i++) {
            slot = GETP(ai->active[i]);
            if (slot == NULL) {
                break;
            }
            time = RandomAdd_s32(GETP(slot->info)->heartbeat[0], GETP(slot->info)->heartbeat[1]);
            if (ai->base.heartbeatTimer > time) {
                ai->base.heartbeatTimer = time;
            }
        }
        thing = CAR_THING(AI_Car_Vehicle(ai));
        repath = 0;
        holder = AI_Car_NearestNavPoint(thing);
        target = GETP(ai->current);
        if (GETP(target->info)->type == AI_TARGET_NAV_POINT || GETP(target->info)->type == AI_TARGET_WAY_POINT) {
            dest = ((NavPoint *)GETP(target->target))->index;
            if (AI_Car_PathPoint(ai, ai->pathIndex) == NULL || ai->pathTimer == 0) {
                repath = 1;
            }
        } else {
            thing = GETP(target->target);
            if (!(thing->flags & THING_FLAG_FREE)) {
                dest = AI_Car_NearestNavPoint(thing);
                if ((s8)NavHolder_IsLinked(holder, dest)) {
                    /* next to it: drive straight there */
                    ai->pathIndex = 32;
                    if ((u32)ai->wayPoint < NavHolder_GetTotalNavPoints()) {
                        dist = PSVECSquareDistance(&CAR_THING(AI_Car_Vehicle(ai))->pos, NavHolder_GetNavPoint(ai->wayPoint));
                        nav = NavHolder_GetNavPoint(ai->wayPoint);
                        if (dist < nav->radius * nav->radius) {
                            if (nav->type == NAV_TYPE_WAYPOINT) {
                                ai->wayPoint = nav->nextWayPoint;
                            } else {
                                ai->wayPoint = -1;
                            }
                        }
                    }
                } else if (AI_Car_PathPoint(ai, ai->pathIndex) == NULL || ai->pathTimer == 0) {
                    repath = 1;
                } else if (GETP(((Thing *)GETP(GETP(ai->current)->target))->vtable) == (u32 *)g_carAVTable &&
                           dest != ai->pathDest) {
                    repath = 1;
                }
            }
        }
        if (repath) {
            AI_Car_PrepareNavPath(ai, holder, dest);
        }
        if (GETP(ai->targetThing) != NULL || GETP(ai->targetNav) != NULL) {
            if (ai->base.state == AI_STATE_SPAWNED) {
                Vector4 out __attribute__((aligned(16)));
                Vector4 quat __attribute__((aligned(16))) = *(Vector4 *)&CAR_THING(AI_Car_Vehicle(ai))->quat;
                Vector4 axis __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 0.0f};

                if (AI_Car_PathPoint(ai, ai->pathIndex) != NULL) {
                    angle = AI_AngleToTarget(AI_Car_Vehicle(ai), &GETP(ai->path[ai->pathIndex])->pos);
                } else if (GETP(ai->targetThing) != NULL) {
                    angle = AI_AngleToTarget(AI_Car_Vehicle(ai), &GETP(ai->targetThing)->pos);
                } else if (GETP(ai->targetNav) != NULL) {
                    angle = AI_AngleToTarget(AI_Car_Vehicle(ai), &GETP(ai->targetNav)->pos);
                }
                GenerateQuaternion(&axis, &out, angle);
                QuaternionMult(&out, &quat, (Vector4 *)&CAR_THING(AI_Car_Vehicle(ai))->quat);
                ai->base.state = AI_STATE_ACTIVE;
            }
            if (AI_Car_DriveMode(ai) == 1 && GameSettings_GetCodeVariable(0x198, 0) == 0) {
                AI_Car_Cheat_80008A60(ai);
            }
        }
    }
}

/* Async, every 400..800 ms: the destructible (if any) between the car and
 * the next path point or targetThing becomes the obstacle to drive at. */
void AI_Car_AsyncScanDestructibles(AICar *ai) {
    f32 dist;
    Vector4 *target;
    u32 index;

    if (AI_Car_Vehicle(ai) != NULL) {
        index = ai->pathIndex;
        target = NULL;
        if (index < 32 && GETP(ai->path[index]) != NULL) {
            target = &GETP(ai->path[index])->pos;
        } else if (GETP(ai->targetThing) != NULL) {
            target = &GETP(ai->targetThing)->pos;
        }
        SETP(ai->obstacle, NULL);
        if (target != NULL) {
            dist = 1.0f;
            SETP(ai->obstacle, PTR(RayHitsObjects_GetObject(&CAR_THING(AI_Car_Vehicle(ai))->pos, target, &dist, 1, 0)));
            ai->scanTimer = RandomAdd_s32(400, 400);
        }
    }
}

/* Whether the active state's target (slot->target) is still good: in
 * range and angle, alive, passing the limits. Way point states move their
 * target on to the next way point. */
static s8 AI_Car_IsTargetValid_800094EC(AICar *ai, AIStateSlot *slot, f32 maxDist, f32 minDist, f32 center, f32 range) {
    extern u8 g_itemVTable[];
    s8 valid;
    Vector4 *pos;
    Thing *thing;
    NavPoint *nav;
    u8 type;
    AIStateInfo *def;
    f32 dist;

    def = GETP(slot->info);
    type = def->type;
    if (type == AI_TARGET_ITEM || (u8)(type - AI_TARGET_ITEM_1_6) <= 2 || type == AI_TARGET_CARRIED_ITEM ||
        type == AI_TARGET_ITEM_6) {
        thing = GETP(slot->target);
        if (thing != NULL && GETP(thing->vtable) == (u32 *)g_itemVTable && !(thing->flags & THING_FLAG_FREE)) {
            pos = AI_Car_ItemPos((ItemThing *)thing);
            if (pos != NULL) {
                dist = PSVECSquareDistance(pos, &CAR_THING(AI_Car_Vehicle(ai))->pos);
                if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                    if (AI_CheckAngleInRange(57.29578f * AI_AngleToTarget(AI_Car_Vehicle(ai), pos), center, range)) {
                        return 1;
                    }
                }
            }
        }
    } else if ((u8)(type - AI_TARGET_HUMAN_CAR) <= 1 || type == AI_TARGET_CAR) {
        thing = GETP(slot->target);
        valid = 1;
        if ((thing->flags & THING_FLAG_FREE) || !AI_Car_CanTarget(ai, def)) {
            return 0;
        }
        if (def->subtype == AI_SUBTYPE_TARGET_LIMITS) {
            valid = AI_Car_PassesLimits(ai, GETP(def->limits), thing);
        }
        if (valid) {
            dist = PSVECSquareDistance(&thing->pos, &CAR_THING(AI_Car_Vehicle(ai))->pos);
            if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                if (AI_CheckAngleInRange(57.29578f * AI_AngleToTarget(AI_Car_Vehicle(ai), &thing->pos), center, range)) {
                    return 1;
                }
            }
        }
        return 0;
    } else if (type == AI_TARGET_NAV_POINT) {
        nav = GETP(slot->target);

        if (nav != NULL && nav->enabled != 0) {
            if (def->unk5 != 0 || PSVECSquareDistance(&nav->pos, &CAR_THING(AI_Car_Vehicle(ai))->pos) > nav->radius * nav->radius) {
                return 1;
            }
        }
    } else if (type == AI_TARGET_WAY_POINT) {
        if (GETP(slot->target) != NULL) {
            if ((u32)ai->wayPoint < NavHolder_GetTotalNavPoints()) {
                SETP(slot->target, NavHolder_GetNavPoint(ai->wayPoint));
                return 1;
            }
        }
    }
    return 0;
}

/* AI_SUBTYPE_LOCAL_STATS: whether the own car's health, grip and loaded
 * weapons meet the state's conditions (other subtypes: 1). */
s32 AI_Car_CheckLocalStats(AICar *ai, AIStateInfo *info) {
    AILocalStats *stats = GETP(info->stats);
    CarA *car = AI_Car_Vehicle(ai);
    s32 health;
    u16 count;
    u16 i;
    AIWeaponRack *holder;

    if (info->subtype != AI_SUBTYPE_LOCAL_STATS) {
        return 1;
    }
    if (stats->health != 0) {
        health = (f32)CAR_THING(car)->health / (100.0f * (f32)GETP(GETP(car->vehicleIface)->collider)->maxHealth);
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
            if (GETP(holder->slots[i].record) != NULL && GETP(holder->slots[i].record)->type == 3 &&
                holder->slots[i].ammo > GETP((GETP(holder->slots[i].record))->weapon)->ammoPerShot) {
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

/* Finds the target for a state entered at depth `level` of the active
 * branch (whose distance/angle limits narrow those of the levels above):
 * keeps the current one if still valid, else picks among the candidates of
 * its type by sortMode. Sets targetThing / targetNav and the limits. */
s8 AI_Car_DetermineTarget(AICar *ai, AIStateSlot *branch, u8 level) {
    typedef struct {
        PTR32(void) thing;
        f32 dist;
        f32 angle;
        u8 padC[4];
    } Candidate;
    s8 valid;
    Thing *cars[8];
    ItemThing *items[0x60];
    Candidate cands[0x80];
    AIStateInfo *def;
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
    Thing *thing;
    Thing *other;
    NavPoint *nav;
    NavPoint *path;
    u32 num;
    u32 n;
    Vector4 *pos;
    u8 i;
    u16 j;
    u8 type;
    u32 index;
    CarA *car;
    u16 count;
    Candidate *best;
#define AI_Car_CandidateWeapon(cand) GETP(GETP(((ItemThing *)GETP((cand)->thing))->record)->weapon)

    def = GETP(branch->info);
    car = AI_Car_Vehicle(ai);
    valid = 0;
    maxDist = def->maxDist * def->maxDist;
    center = def->center;
    minDist = def->minDist * def->minDist;
    range = def->range;
    count = 0;
    best = NULL;
    if (level) {
        if (ai->targetDistSq || ai->minDistSq) {
            if (maxDist > ai->targetDistSq) {
                maxDist = ai->targetDistSq;
            }
            if (minDist < ai->minDistSq) {
                minDist = ai->minDistSq;
            }
        }
        if (!center && !range) {
            center = ai->center;
            range = ai->range;
        } else if (ai->center || ai->range) {
            /* intersect the two angle ranges */
            c = center + range;
            d = center - range;
            a = ai->center + ai->range;
            b = ai->center - ai->range;
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

    if (branch == GETP(ai->active[level]) &&
        AI_Car_IsTargetValid_800094EC(ai, branch, maxDist, minDist, center, range) &&
        (GETP(branch->info)->unk5 == 0 || branch->retargetTimer != 0)) {
        /* keeps its target */
        type = GETP(branch->info)->type;
        if (type == AI_TARGET_NAV_POINT || type == AI_TARGET_WAY_POINT) {
            SETP(ai->targetThing, NULL);
            SETP(ai->targetNav, GETP(branch->target));
            ai->targetDistSq = PSVECSquareDistance(&CAR_THING(AI_Car_Vehicle(ai))->pos, &GETP(ai->targetNav)->pos);
        } else {
            thing = GETP(branch->target);
            if ((type == AI_TARGET_ITEM || type == AI_TARGET_CARRIED_ITEM) &&
                GETP(((ItemThing *)thing)->record)->type == ITEM_TYPE_CARRIED &&
                GETP(thing->player) != NULL && GETP(((Player *)GETP(thing->player))->vehicle) != NULL) {
                SETP(ai->targetThing, GETP(((Player *)GETP(thing->player))->vehicle));
            } else {
                SETP(ai->targetThing, thing);
            }
            SETP(ai->targetNav, NULL);
            ai->targetDistSq = PSVECSquareDistance(&CAR_THING(AI_Car_Vehicle(ai))->pos, &GETP(ai->targetThing)->pos);
        }
        ai->minDistSq = minDist;
        ai->center = center;
        ai->range = range;
        return 1;
    }

    type = GETP(branch->info)->type;
    if (type == AI_TARGET_HUMAN_CAR || type == AI_TARGET_CAR) {
        num = ThingManager_GetActiveCars((void **)cars, 8);
        for (i = 0; i < num; i++) {
            if (cars[i]->flags & THING_FLAG_FREE) {
                continue;
            }
            if (GETP(branch->info)->type == AI_TARGET_HUMAN_CAR && !Player_IsHuman(GETP(cars[i]->player))) {
                continue;
            }
            thing = cars[i];
            other = CAR_THING(AI_Car_Vehicle(ai));
            if (thing == other) {
                continue;
            }
            def = GETP(branch->info);
            if (!AI_Car_CanTarget(ai, def)) {
                continue;
            }
            valid = 1;
            if (def->subtype == AI_SUBTYPE_TARGET_LIMITS) {
                valid = AI_Car_PassesLimits(ai, GETP(def->limits), cars[i]);
            }
            if (valid == 1) {
                dist = PSVECSquareDistance(&thing->pos, &other->pos);
                valid = 0;
                if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                    angle = 57.29578f * AI_AngleToTarget(car, &cars[i]->pos);
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
    } else if (type == AI_TARGET_2) {
    } else if ((u8)(type - AI_TARGET_ITEM) <= 3 || type == AI_TARGET_CARRIED_ITEM || type == AI_TARGET_ITEM_6) {
        num = ThingManager_GetActiveItems((void **)items, 0x60);
        for (n = 0; n < num; n++) {
            ItemThing *item = items[n];
            u8 itemType;

            if (item->unkE0 != 1) {
                continue;
            }
            type = GETP(branch->info)->type;
            itemType = GETP(item->record)->type;
            if (type == AI_TARGET_ITEM || (type == AI_TARGET_ITEM_1_6 && (itemType == 1 || itemType == 6)) ||
                (type == AI_TARGET_ITEM_2 && itemType == 2) || (type == AI_TARGET_WILDCARD && itemType == ITEM_TYPE_WILDCARD) ||
                (type == AI_TARGET_CARRIED_ITEM && itemType == ITEM_TYPE_CARRIED) || (type == AI_TARGET_ITEM_6 && itemType == 6)) {
                pos = AI_Car_ItemPos(item);
                if (pos != NULL) {
                    dist = PSVECSquareDistance(pos, &CAR_THING(AI_Car_Vehicle(ai))->pos);
                    if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                        angle = 57.29578f * AI_AngleToTarget(AI_Car_Vehicle(ai), pos);
                        if (AI_CheckAngleInRange(angle, center, range)) {
                            /* no count < 0x80 check here (as the original) */
                            cands[count].angle = angle;
                            cands[count].dist = dist;
                            SETP(cands[count].thing, item);
                            count++;
                        }
                    }
                }
            }
        }
    } else if (type == AI_TARGET_NAV_POINT) {
        num = NavHolder_GetTotalNavPoints();
        for (n = 0; n < num; n++) {
            nav = NavHolder_GetNavPoint(n);
            if (nav != NULL && nav->enabled != 0 && RandomAdd_s32(0, 100) < nav->chance) {
                dist = PSVECSquareDistance(&nav->pos, &CAR_THING(AI_Car_Vehicle(ai))->pos);
                if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                    angle = 57.29578f * AI_AngleToTarget(car, &nav->pos);
                    if (AI_CheckAngleInRange(angle, center, range) && count < 0x80) {
                        cands[count].angle = angle;
                        cands[count].dist = dist;
                        SETP(cands[count].thing, nav);
                        count++;
                    }
                }
            }
        }
    } else if (type == AI_TARGET_WAY_POINT) {
        /* the way point chain, from the first */
        if (ai->wayPoint != -1) {
            index = NavHolder_GetFirstWayPoint();
        } else {
            index = 0xFFFFFFFF;
        }
        while (index != 0xFFFFFFFF) {
            nav = NavHolder_GetNavPoint(index);
            if (nav == NULL || nav->enabled == 0) {
                break;
            }
            dist = PSVECSquareDistance(&nav->pos, &CAR_THING(AI_Car_Vehicle(ai))->pos);
            if ((!minDist && !maxDist) || (dist <= maxDist && dist >= minDist)) {
                angle = 57.29578f * AI_AngleToTarget(car, &nav->pos);
                if (AI_CheckAngleInRange(angle, center, range) && count < 0x80) {
                    cands[count].angle = angle;
                    cands[count].dist = dist;
                    SETP(cands[count].thing, nav);
                    count++;
                }
            }
            if (nav->type != NAV_TYPE_WAYPOINT) {
                break;
            }
            index = nav->nextWayPoint;
        }
    }

    if (count != 0) {
        best = &cands[0];
        if (count > 1) {
            def = GETP(branch->info);
            if (def->sortMode == AI_SORT_NEAREST) {
                if (def->type != AI_TARGET_WAY_POINT) {
                    for (j = 1; j < count; j++) {
                        if (cands[j].dist < best->dist) {
                            best = &cands[j];
                        }
                    }
                }
            } else if (def->sortMode == AI_SORT_FARTHEST) {
                if (def->type != AI_TARGET_WAY_POINT) {
                    for (j = 1; j < count; j++) {
                        if (cands[j].dist > best->dist) {
                            best = &cands[j];
                        }
                    }
                } else {
                    best += count - 1;
                }
            } else if (def->sortMode == AI_SORT_RANDOM) {
                best += RandomAdd_s32(0, count - 1);
            } else if (def->sortMode == AI_SORT_SPECIAL) {
                if (def->type == AI_TARGET_ITEM_1_6 || def->type == AI_TARGET_ITEM_2 || def->type == AI_TARGET_WILDCARD) {
                    /* wildcards: the nearest of the preferred weapon, else the
                     * lowest priority (nearest of those) */
                    closest = NULL;
                    for (j = 1; j < count; j++) {
                        ItemRecord *info = GETP(((ItemThing *)GETP(cands[j].thing))->record);
                        WeaponInfo *owner;

                        switch (info->type) {
                        case ITEM_TYPE_WILDCARD:
                            owner = GETP((info)->weapon);
                            if (owner != NULL) {
                                if (closest != NULL) {
                                    if (owner->priority == AI_Car_Script(ai)->preferredWeapon && cands[j].dist < closest->dist) {
                                        closest = &cands[j];
                                    }
                                } else if (owner->priority == AI_Car_Script(ai)->preferredWeapon) {
                                    closest = &cands[j];
                                } else if (owner->priority < AI_Car_CandidateWeapon(best)->priority) {
                                    best = &cands[j];
                                } else if (owner->priority == AI_Car_CandidateWeapon(best)->priority && cands[j].dist < best->dist) {
                                    best = &cands[j];
                                }
                            }
                            break;
                        }
                    }
                    if (closest != NULL) {
                        best = closest;
                    }
                } else if (def->type == AI_TARGET_WAY_POINT) {
                    /* the first candidate on the path to the last one */
                    path = NavHolder_FindPath(AI_Car_NearestNavPoint(CAR_THING(AI_Car_Vehicle(ai))),
                                              ((NavPoint *)GETP(cands[count - 1].thing))->index);
                    best = NULL;
                    while (path != NULL && best == NULL) {
                        for (j = 0; j < count; j++) {
                            if (path == GETP(cands[j].thing)) {
                                best = &cands[j];
                                break;
                            }
                        }
                        path = GETP(path->parent);
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
        type = GETP(branch->info)->type;
        if (type == AI_TARGET_NAV_POINT || type == AI_TARGET_WAY_POINT) {
            SETP(ai->targetNav, GETP(best->thing));
            SETP(ai->targetThing, NULL);
            if (GETP(branch->info)->type == AI_TARGET_WAY_POINT && ai->wayPoint == -2) {
                ai->wayPoint = GETP(ai->targetNav)->index;
            }
        } else {
            thing = GETP(best->thing);
            if ((type == AI_TARGET_ITEM || type == AI_TARGET_CARRIED_ITEM) &&
                GETP(((ItemThing *)thing)->record)->type == ITEM_TYPE_CARRIED && GETP(thing->player) != NULL &&
                GETP(((Player *)GETP(thing->player))->vehicle) != NULL) {
                SETP(ai->targetThing, GETP(((Player *)GETP(thing->player))->vehicle));
            } else {
                SETP(ai->targetThing, thing);
            }
            SETP(ai->targetNav, NULL);
        }
        valid = 1;
    }
    if (valid) {
        ai->targetDistSq = best->dist;
        ai->minDistSq = minDist;
        ai->center = center;
        ai->range = range;
        branch->retargetTimer = GETP(branch->info)->retargetTime;
        SETP(branch->target, GETP(best->thing));
    }
    return valid;
#undef AI_Car_CandidateWeapon
}

/* Tries a state at depth `level` of the active branch. If it is the one
 * active there it stays while its duration runs, its stats hold and it has a
 * target; otherwise it is entered if it has uses left, isn't cooling down,
 * its stats hold and it finds a target (spawning its debris effect). On
 * leaving, it and the states below it start cooling down. */
s8 AI_Car_EnterState(AICar *ai, AIStateSlot *branch, s32 level) {
    extern Vector4 g_zeroVector;
    DebrisParams p __attribute__((aligned(16)));
    AIStateInfo *info = GETP(branch->info);
    AIStateSlot *slot;
    Thing *car;
    u8 i;
    s8 done;

    done = 0;
    if (branch == GETP(ai->active[(u8)level])) {
        if (branch->durationTimer != 0) {
            AI_UpdateTimer(&branch->durationTimer);
        }
        if (branch->durationTimer == 0 && (info->cooldown[0] != 0 || info->cooldown[1] != 0)) {
            done = 1;
        } else if (!(s8)AI_Car_CheckLocalStats(ai, info)) {
            done = 1;
        } else if (!AI_Car_DetermineTarget(ai, branch, level)) {
            done = 1;
        }
        if (done) {
            branch = NULL;
        }
    } else {
        if (info->uses != 0 && branch->usesLeft == 0) {
            return 0;
        }
        if (branch->cooldownTimer != 0) {
            return 0;
        }
        if (!(s8)AI_Car_CheckLocalStats(ai, info)) {
            return 0;
        }
        if (!AI_Car_DetermineTarget(ai, branch, level)) {
            return 0;
        }
        if (branch->usesLeft != 0) {
            branch->usesLeft--;
        }
        if (info->debris != 0) {
            p.debris = info->debris;
            car = CAR_THING(AI_Car_Vehicle(ai));
            p.pos = car->pos;
            p.quat = *(Vector4 *)&car->quat;
            p.vel = car->vel;
            p.owner = (s32)HND(ai);
            p.unk48 = 0.0f;   /* unk4C is left unset, as in the original */
            p.angVel = g_zeroVector;
            ThingManager_DARYL_CreateThing(1, &p);
        }
        branch->durationTimer = RandomAdd_s32(info->duration[0], info->duration[1]);
        done = 1;
    }
    if (done) {
        for (i = level; i < 4; i++) {
            slot = GETP(ai->active[i]);
            if (slot == NULL) {
                break;
            }
            slot->cooldownTimer = RandomAdd_s32(GETP(slot->info)->cooldown[0], GETP(slot->info)->cooldown[1]);
            SETP(ai->active[i], NULL);
        }
    }
    return branch != NULL;
}

/* Walks the state tree depth first, entering the first state that can be
 * at each depth; the deepest one entered becomes current. Recursive; the
 * slot and depth counters are static. */
s8 AI_Car_DetermineStateBranch(AICar *ai, AIStateNode *state, s8 skip) {
    static u8 iTotal;
    static u8 iValid;
    AIStateInfo *found = NULL;
    u8 i;

    if (state == &AI_Car_Script(ai)->root) {
        iTotal = 0;
        iValid = 0;
    }
    if (GETP(state->info) != NULL) {
        if (!skip && AI_Car_EnterState(ai, &ai->slots[iTotal], iValid)) {
            found = GETP(state->info);
            SETP(ai->active[iValid++], &ai->slots[iTotal]);
        }
        iTotal++;
    }
    skip = (!skip && (GETP(state->info) == NULL || found != NULL)) == 0;
    for (i = 0; i < state->numChildren; i++) {
        if (AI_Car_DetermineStateBranch(ai, &GETP(state->children)[i], skip)) {
            return 1;
        }
    }
    if (skip) {
        return 0;
    }
    if (found != NULL) {
        SETP(ai->current, GETP(ai->active[iValid - 1]));
        SETP(ai->active[iValid], NULL);   /* active[4] is `current` */
        return 1;
    }
    return 0;
}

/* Counts the state timers down and picks the state; a new target starts a
 * new path (or a heartbeat soon, for a thing). 0: no state. */
s32 AI_Car_DetermineState(AICar *ai) {
    u32 i;
    void *prev;

    if (GETP(ai->current) != NULL) {
        prev = GETP(GETP(ai->current)->target);
    } else {
        prev = NULL;
    }
    for (i = 0; i < 32; i++) {
        if (GETP(ai->slots[i].info) == NULL) {
            break;
        }
        AI_UpdateTimer(&ai->slots[i].durationTimer);
        AI_UpdateTimer(&ai->slots[i].cooldownTimer);
        if (GETP(ai->slots[i].info)->unk5 != 0) {
            AI_UpdateTimer(&ai->slots[i].retargetTimer);
        }
    }
    if (AI_Car_DetermineStateBranch(ai, &AI_Car_Script(ai)->root, 0)) {
        if (GETP(GETP(ai->current)->target) != prev) {
            if (GETP(ai->targetThing) != NULL) {
                ai->pathIndex = 32;
                ai->base.heartbeatTimer = 1;
            } else if (GETP(ai->targetNav) != NULL) {
                AI_Car_PrepareNavPath(ai, AI_Car_NearestNavPoint(CAR_THING(AI_Car_Vehicle(ai))),
                                      GETP(ai->targetNav)->index);
            }
        }
        return 1;
    }
    return 0;
}

/* Gives every state of the tree its slot, in depth-first order, and ends
 * the slots with a NULL info. */
void AI_Car_ResetState(AICar *ai, AIStateNode *state) {
    static u8 iTotal;
    u8 i;

    if (state == &AI_Car_Script(ai)->root) {
        iTotal = 0;
    }
    if (GETP(state->info) != NULL) {
        SETP(ai->slots[iTotal].info, GETP(state->info));
        ai->slots[iTotal].durationTimer = 0;
        ai->slots[iTotal].cooldownTimer = 0;
        ai->slots[iTotal].retargetTimer = 0;
        ai->slots[iTotal].usesLeft = GETP(state->info)->uses;
        iTotal++;
    }
    for (i = 0; i < state->numChildren; i++) {
        AI_Car_ResetState(ai, &GETP(state->children)[i]);
    }
    if (state == &AI_Car_Script(ai)->root) {
        SETP(ai->slots[iTotal].info, NULL);
    }
}

/* (Re)starts the AI: scales the car's health and grip by the script, resets
 * the states, path, targets and timers. Called with the player; note
 * ActionSwapAIScript passes the address of the car's Thing.player field
 * instead (as the original). */
void AI_Car_Reset(void *player) {
    AICar *ai = player;
    AICarScript *script = AI_Car_Script(ai);
    CarA *vehicle;
    u16 i;

    AI_Reset(&ai->base);
    if (script != NULL) {
        ai->speedLimit = script->speed;
        vehicle = AI_Car_Vehicle(ai);
        if (vehicle != NULL) {
            CAR_THING(vehicle)->health = CAR_THING(vehicle)->health * script->healthScale;
            AI_Car_Vehicle(ai)->gripScale *= script->gripScale;
        }
        AI_Car_ResetState(ai, &script->root);
    }
    SETP(ai->targetThing, NULL);
    SETP(ai->targetNav, NULL);
    ai->unk5F0 = 0;
    ai->unk5F4 = 0;
    ai->pathDest = -1;
    ai->pathIndex = 0;
    ai->pathTimer = 0;
    ai->wayPoint = -2;
    memset(ai->path, 0, sizeof(ai->path));
    for (i = 0; i < 16; i++) {
        SETP(ai->records[i].thing, NULL);
    }
    ai->scanTimer = RandomAdd_s32(1000, 1000);
    SETP(ai->obstacle, NULL);
    SETP(ai->current, NULL);
    ai->stillFrames = 0;
    ai->stuckTimer = -120;
    ai->weaponTimer = 1;
    ai->weaponSwitchTimer = 3000;
}

/* PlayerManager_NewAIPlayer init for AI_KIND_CAR; the AI is reset on its
 * first heartbeat (once it has a car). */
void AI_Car_Init(AICar *ai, AIPlayerParams *params) {

    ai->base.script.h = params->script;
    AI_Init(ai, (s32)HND(params));  /* r3/r4 passed on unchanged (asm) */
    SETP(ai->base.heartbeat.data, ai);
    SETP(ai->base.heartbeat.func, AI_Car_AsyncHeartbeat);
    ai->base.heartbeat.queued = 0;
    SETP(ai->weaponEvent.data, ai);
    SETP(ai->weaponEvent.func, AI_Car_WeaponAsyncHeartbeat_80008210);
    ai->weaponEvent.queued = 0;
    SETP(ai->scanEvent.data, ai);
    SETP(ai->scanEvent.func, AI_Car_AsyncScanDestructibles);
    ai->scanEvent.queued = 0;
    if (params->dynamic) {
        ai->base.state = AI_STATE_SPAWNED;
    } else {
        ai->base.state = AI_STATE_STATIC;
    }
    ai->nearSq = params->nearSq;
    ai->farSq = params->farSq;
    ai->wayPoint = -2;
    memset(ai->active, 0, 0x60);   /* active[] up to path[3] */
    ai->base.reset = 1;
}

/* The player's control reader: clears the controls and sets them from the
 * state, path and target each frame; queues the async events. */
void AI_Car_Heartbeat(AICar *ai) {
    extern u32 g_iDeltaTimeMS;
    Player *pl = AI_PLAYER(ai);
    s8 cleared;
    Vector4 *pos;
    NavPoint *point;
    CarA *car;
    u16 i;
    s16 j;
    s8 done;

    car = AI_Car_Vehicle(ai);
    if (car != NULL && GETP(CAR_THING(car)->vtable) != (u32 *)g_carAVTable) {
        return;
    }
    Control_ClearControl(ai);
    if (!AI_Heartbeat(&ai->base)) {
        return;
    }
    if (ai->base.reset && car != NULL) {
        AI_Car_Reset(ai);
    }
    if (ai->base.reset) {
        return;
    }
    if (GETP(ai->base.script) != NULL) {
        AI_Car_DetermineState(ai);
        if (GETP(ai->current) == NULL || (GETP(ai->targetThing) == NULL && GETP(ai->targetNav) == NULL)) {
            pl->brake = 0xFF;
            if (GETP(car->vehicleIface)->driveMode == 1) {
                pl->throttle = 0xFF;
            }
            return;
        }
        if (ai->scanTimer != 0 && AI_UpdateTimer(&ai->scanTimer) == 0) {
            AI_AddEvent(&ai->scanEvent);
        }
        if (ai->pathTimer != 0) {
            AI_UpdateTimer(&ai->pathTimer);
        }
        if (AI_Car_PathPoint(ai, ai->pathIndex) != NULL) {
            pos = &CAR_THING(AI_Car_Vehicle(ai))->pos;
            cleared = 1;
            while (cleared && (point = AI_Car_PathPoint(ai, ai->pathIndex)) != NULL) {
                cleared = 0;
                if (PSVECSquareDistance(&point->pos, pos) < point->radius * point->radius) {
                    AI_Car_ClearedNavPoint_800087C8(ai, 0);
                    cleared = 1;
                }
            }
        }
        /* drop records older than 30 s */
        for (i = 0; i < 16 && GETP(ai->records[i].thing) != NULL; i++) {
            done = 0;
            while (!done) {
                done = 1;
                if (GETP(ai->records[i].thing) != NULL) {
                    ai->records[i].age[0] += g_iDeltaTimeMS;
                    ai->records[i].age[1] += g_iDeltaTimeMS;
                    if (ai->records[i].age[0] > 30000.0f && ai->records[i].age[1] > 30000.0f) {
                        for (j = i; j < 16 && GETP(ai->records[j].thing) != NULL; j++) {
                            if (j == 15) {
                                SETP(ai->records[j].thing, NULL);
                            } else {
                                ai->records[j] = ai->records[j + 1];
                            }
                        }
                        done = 0;
                    }
                }
            }
        }
        /* a dead timer: zeroed right before it would count */
        ai->unk5F0 = 0;
        ai->unk5F4 = 0;
        if (ai->unk5F0 != 0) {
            ai->unk5F4 += g_iDeltaTimeMS;
            if (ai->unk5F4 > 20000) {
                ai->unk5F4 = 0;
                ai->unk5F0 = 0;
            }
        }
        if (!AI_Car_CheckIfStuck(ai)) {
            AI_Car_SeekPoint(ai);
        }
        AI_UpdateTimer(&ai->weaponSwitchTimer);
        if (ai->weaponTimer != 0 && AI_UpdateTimer(&ai->weaponTimer) == 0) {
            AI_AddEvent(&ai->weaponEvent);
        }
    }
}
