#include "common.h"
#include "CarA.h"


extern f32 g_frictionTable[9][10];

/* What a suspension ray hit is made of (GETP(RayHit.surface)). */
typedef struct SuspSurface {
    u8 type;                  /* 0x00 1: no skids, 2: own skid texture */
    u8 pad01;
    u16 skidTexture;          /* 0x02 */
    u8 pad04[0x10];
    f32 grip;                 /* 0x14 */
    u8 pad18[4];
    f32 dustRate;             /* 0x1C */
    f32 particleInterval;     /* 0x20 */
    u8 pad24[8];
    PTR32(GXColor) skidColor;       /* 0x2C */
    u32 dustThing;            /* 0x30 */
    u32 particle;             /* 0x34 */
    u8 pad38[4];
    PTR32(struct { u8 pad[0x14]; u32 sample; }) rollSound; /* 0x3C */
    u32 sparkThing;           /* 0x40 */
} SuspSurface;

typedef struct SuspRayHit {
    Vector4 pos;              /* 0x00 */
    Vector4 normal;           /* 0x10 */
    u8 pad20[8];
    PTR32(SuspSurface) surface;     /* 0x28 */
    u8 pad2C[4];
    Vector4 velocity;         /* 0x30 */
    u8 pad40[0x10];
} SuspRayHit;

/* ThingManager_DARYL_CreateThing(1, ...) parameters (same as CarA_HW.c's). */
typedef struct SuspThingParams {
    Vector4 pos;
    Vector4 animRefPos;
    Vector4 vel;
    Vector4 angVel;
    u32 id;
    PTR32(void) player;
    f32 unk48;
    f32 unk4C;
} SuspThingParams;

extern f32 g_fDeltaTimeSec;
extern s32 g_iDeltaTimeMS;
extern u32 g_iLastDeltaTimeMS;
extern CarA *g_pNoRayCollideThing;
extern Vector4 g_zeroVector __attribute__((aligned(16)));



f32 GetDampFactor(u8 type, u32 n) {
    f32 f = 1.0f;
    u32 i = n;

    if (type <= 8) {
        while (i >= 10) {
            f *= g_frictionTable[type][9];
            i -= 10;
        }
        return f * g_frictionTable[type][i];
    }
    while (type > 8) {
        f *= GetDampFactor(8, n);
        type -= 8;
    }
    return f * GetDampFactor(type, n);
}

f32 CarA_GetImpactMultiplier(void *car, void *other, f32 scale) {
    f32 impact;

    impact = scale * PSVECDotProduct((u8 *)car + 0x20, (u8 *)other + 0x10);
    if (impact < 0.0f) {
        return 1.0f;
    }
    return 1.0f - impact;
}

void CarA_ComputeSuspensionForces_Aux1(CarA *car) {
    extern Player *g_pSourcePlayer;
    extern char stunt_msg_buffer_second[];
    extern u8 g_stuntMessageLocalString[];  /* port: a table of string slots */
    typedef struct RackItem {
        u8 pad0[0x30];
        PTR32(struct { u8 pad0[0x28]; s32 maxCount; }) data;
    } RackItem;
    typedef struct RackSlot {
        u8 pad0[0x40];
        s16 count;
        u8 pad42[0xA];
        PTR32(RackItem) item;
    } RackSlot;
    typedef struct Rack {
        u8 pad0[0xB0];
        RackSlot slots[12];
    } Rack;
    SuspThingParams params;
    Vector4 torque __attribute__((aligned(16)));
    Vector4 arm __attribute__((aligned(16)));
    f32 dot;
    f32 force;
    f32 t;
    f32 range;
    CarAEngine *engine;
    CarAAccel *accel;
    CarAInfo *info;
    CarAStuntInfo *stunt;
    u32 points;
    u32 numMsgs;
    u32 numDebris;
    u32 idx;
    Rack *rack;
    s32 n;

    dot = PSVECDotProduct(&car->worldMatrix.v[2], &car->velocity);
    if (!(car->transmissionFlags & 2)) {
        force = -GETP(GETP(car->vehicleIface)->collider)->gravity;
        car->torqueAccum.x += force * car->localAxis.x;
        car->torqueAccum.y += force * car->localAxis.y;
        car->torqueAccum.z += force * car->localAxis.z;
    }
    if (car->numWheelsGrounded == 0) {
        car->unk460 += g_iDeltaTimeMS;
        car->groundedTime = 0;
        if (!(car->transmissionFlags & 2) && !CarA_DoFreeStyleStunts(car) && car->worldMatrix.v[1].y > 0.3f) {
            if (car->unk460 > (engine = GETP(GETP(car->vehicleIface)->engine))->selfRightDelay) {
                f32 len = engine->selfRightArm;

                arm.x = -car->worldMatrix.v[1].x * len;
                arm.y = (1.0f - car->worldMatrix.v[1].y) * len;
                arm.z = -car->worldMatrix.v[1].z * len;
                PSVECCrossProduct(&car->worldMatrix.v[1], &arm, &torque);
                car->torqueAccum.x += torque.x;
                car->torqueAccum.y += torque.y;
                car->torqueAccum.z += torque.z;
            }
            if (car->unk460 > (accel = GETP(GETP(car->vehicleIface)->accel))->airDownforceDelay) {
                force = accel->airDownforceRate * car->lowSpeedFlag + accel->airDownforceBase;
                if (force > accel->airDownforceMax) {
                    force = accel->airDownforceMax;
                }
                force = -force;
                car->torqueAccum.x += force * car->localAxis.x;
                car->torqueAccum.y += force * car->localAxis.y;
                car->torqueAccum.z += force * car->localAxis.z;
            }
        }
        if (!(car->transmissionFlags & 0x80000)) {
            info = GETP(car->vehicleIface);
            if (info->numSlaves > 8 && car->unk460 > GETP(info->engine)->selfRightDelay) {
                MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(info->slaves) + 0x64, 8);
                car->transmissionFlags |= 0x80000;
            }
        }
        car->groundedScale = 0.0f;
    } else {
        if (car->unk460 > 300) {
            MotionState_ReleaseSlavesByType(HND(&car->motionState), 4);
            car->transmissionFlags &= ~2;
        }
        car->unk460 = 0;
    }
    t = car->forwardAccel;
    force = 20.0f * (GETP(GETP(car->vehicleIface)->accel)->spinDecay * g_fDeltaTimeSec);
    if (t > 0.0f) {
        car->forwardAccel = t - force;
    } else if (t < 0.0f) {
        car->forwardAccel = t + force;
    }
    if (t * car->forwardAccel <= 0.0f) {
        car->forwardAccel = 0.0f;
    }
    car->transmissionFlags &= ~0x40;
    if (car->numWheelsGrounded == GETP(car->vehicleIface)->numWheels) {
        car->transmissionFlags &= ~0x80000;
        MotionState_ReleaseSlavesByType(HND(&car->motionState), 8);
        if ((car->transmissionFlags & 0x100) && _fabs(car->gradeOrReverse) < 2.0f &&
            (car->worldMatrix.v[1].y > 0.83f ||
             (car->worldMatrix.v[1].y > 0.6f && GETP(car->player) != NULL && (car->transmissionFlags & 0x20) &&
              GETP(car->player)->throttle == 0))) {
            car->velocity.x *= 0.9f;
            car->velocity.y *= 0.9f;
            car->velocity.z *= 0.9f;
            GetComponentOfVector(&car->groundNormal, &car->forceAccum);
        }
        if (car->transmissionFlags & 0x10) {
            car->gradeOrReverse = dot;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            while (car->rpm < GETP(car->vehicleIface)->unk24 && car->curveTableIdx > 1) {
                car->curveTableIdx--;
                car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            }
            car->transmissionFlags &= ~0x10;
        }
        if (GETP(car->player) != NULL) {
            g_pSourcePlayer = GETP(car->player);
            if ((GETP(car->player)->buttonsHeld & 1) && dot > 0.0f && car->forwardAccel < 0.1f) {
                car->forwardAccel = GETP(GETP(car->vehicleIface)->accel)->spinImpulse * dot;
                car->transmissionFlags |= 0x40;
                if (GETP(car->vehicleIface)->numSlaves > 5 && dot > 30.0f) {
                    MotionState_ReleaseSlavesByType(HND(&car->motionState), 5);
                    MotionState_ReleaseSlavesByType(HND(&car->motionState), 6);
                    MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->slaves) + 0x40, 5);
                }
            }
            if ((GETP(car->player)->buttonsHeld & 0x400) && dot > 0.0f && car->forwardAccel > -0.1f) {
                car->forwardAccel = -GETP(GETP(car->vehicleIface)->accel)->spinImpulse * dot;
                car->transmissionFlags |= 0x40;
                if (GETP(car->vehicleIface)->numSlaves > 5 && dot > 30.0f) {
                    MotionState_ReleaseSlavesByType(HND(&car->motionState), 5);
                    MotionState_ReleaseSlavesByType(HND(&car->motionState), 6);
                    MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->slaves) + 0x4C, 6);
                }
            }
            g_pSourcePlayer = NULL;
        }
        if (GETP(car->player) != NULL && Player_IsHuman(GETP(car->player))) {
            info = GETP(car->vehicleIface);
            if (car->stuntTime > GETP(info->stunt)->minFailTime) {
                points = car->stuntTime;
                if (info->numSlaves > 7) {
                    MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(info->slaves) + 0x58, 7);
                }
                MotionState_ReleaseSlavesByType(HND(&car->motionState), 8);
                stunt = GETP(GETP(car->vehicleIface)->stunt);
                range = stunt->maxFailTime - stunt->minFailTime;
                t = ((f32)points - stunt->minFailTime) / range;
                if (range > 0.0f && (f32)points > stunt->minFailTime) {
                    numMsgs = stunt->numMessages;
                    numDebris = stunt->numRewardThings;
                    if (numMsgs != 0) {
                        if (car->groundNormal.y < -0.5f) {
                            sprintf(stunt_msg_buffer_second, "CEILING PLANT!|%d x8", points);
                            SETP(car->stuntPopup, &g_stuntMessageLocalString);
                            points *= 8;
                            car->stuntTime *= 8.0f;
                        } else if (car->groundNormal.y < 0.25f) {
                            sprintf(stunt_msg_buffer_second, "Wall Plant|%d x3", points);
                            SETP(car->stuntPopup, &g_stuntMessageLocalString);
                            points *= 3;
                            car->stuntTime *= 3.0f;
                        } else {
                            idx = 0;
                            if (numMsgs > 1) {
                                idx = t * numMsgs + 0.5f;
                                if (idx >= numMsgs) {
                                    idx = numMsgs - 1;
                                }
                            }
                            if (GETP(GETP(stunt->messages)[idx]) != NULL) {
                                sprintf(stunt_msg_buffer_second, "%s%d\n", GETP(GETP(GETP(stunt->messages)[idx])[0]), points);
                                SETP(car->stuntPopup, &g_stuntMessageLocalString);
                            }
                        }
                    }
                    GETP(car->player)->stuntScored = 1;
                    GETP(car->player)->stuntPoints += points;
                    if (GETP(car->player)->stuntPoints > 99999) {
                        GETP(car->player)->stuntPoints = 99999;
                    }
                    if (numDebris != 0) {
                        idx = 0;
                        if (numDebris > 1) {
                            idx = t * numDebris + 0.5f;
                            if (idx >= numDebris) {
                                idx = numDebris - 1;
                            }
                        }
                        params.id = GETP(GETP(GETP(car->vehicleIface)->stunt)->rewardThings)[idx];
                        if (params.id != 0) {
                            params.pos = car->worldMatrix.v[3];
                            params.animRefPos = car->animRefPos;
                            params.vel.x = car->velocity.x;
                            params.vel.y = 0.0f;
                            params.vel.z = car->velocity.z;
                            SETP(params.player, NULL);
                            params.unk48 = 0.0f;
                            params.unk4C = 0.0f;
                            params.angVel = g_zeroVector;
                            ThingManager_DARYL_CreateThing(1, &params);
                        }
                    }
                }
                rack = ThingManagerList_GetByType(&car->motionState.slaves, 8);
                for (n = 0; n < 12; n++) {
                    RackSlot *slot = &rack->slots[n];

                    if (GETP(slot->item) != NULL) {
                        slot->count += (s16)points;
                        if (slot->count > (s16)GETP(GETP(slot->item)->data)->maxCount) {
                            slot->count = GETP(GETP(slot->item)->data)->maxCount;
                        }
                    }
                }
                car->turboTime += (s32)((f32)points * GETP(GETP(car->vehicleIface)->stunt)->turboPerPoint);
                if (car->turboTime > 30000) {
                    car->turboTime = 30000;
                }
                car->stuntTime = 0.0f;
            }
        }
        car->stuntSpin.x = 0.0f;
        car->stuntSpin.y = 0.0f;
        car->stuntSpin.z = 0.0f;
        car->groundedScale = 1.0f;
        car->groundedTime += g_iDeltaTimeMS;
        car->transmissionFlags &= ~0x40000;
    }
}

void CarA_ComputeSuspensionForces(void *thing) {
    SuspRayHit hit;
    SuspThingParams p1;
    SuspThingParams p2;
    Vector4 bodyPos;
    Vector4 normalSum __attribute__((aligned(16)));
    Vector4 dir;
    Vector4 up;
    Vector4 side;
    Vector4 contact;
    Vector4 fwd;
    Vector4 start;
    Vector4 slip;
    Vector4 pointVel;
    Vector4 rot;
    Vector4 r;
    Vector4 friction;
    Vector4 cross;
    Vector4 forcePos;
    Vector4 r2;
    s8 hitGround;
    f32 volume;
    f32 dist;
    GXColor color;
    u32 sample;
    f32 silence2;
    f32 silence;
    f32 base;
    CarA *car = thing;
    Player *player;
    CarTrailBone *wheel;
    u32 numSlipping;
    SuspSurface *rollSurface;
    CarAInfo *info;
    u32 i;
    u32 interval;
    s32 bone;
    s32 n;
    u32 tex;
    u8 maxAlpha;
    f32 rollScale;
    f32 driveSum;
    f32 maxForce;
    f32 sideSum;
    f32 gripLimit;
    f32 speed;
    f32 fwdSpeed;
    f32 sideSpeed;
    f32 compression;
    f32 springForce;
    f32 damping;
    f32 slipMag;
    f32 fric;
    f32 len;
    f32 scale;
    f32 steer;
    f32 drive;
    f32 lean;
    f32 d;
    f32 rate;
    f32 sparkRate;
    f32 r3scale;
    f32 one;
    f32 zero;
    f32 skid;
    f32 maxGrade;
    f32 force;
    f32 spin;
    f32 ny;

    rollScale = 0.0f;
    numSlipping = 0;
    driveSum = rollScale;
    PSVECDotProduct(&car->worldMatrix.v[2], &car->velocity);
    rollSurface = NULL;
    maxForce = 0.0f;
    base = GETP(GETP(car->vehicleIface)->accel)->unk14;
    sideSum = maxForce;
    volume = maxForce;
    normalSum = g_zeroVector;
    gripLimit = 0.083333f * GETP(GETP(car->vehicleIface)->wheels)->friction;
    car->numWheelsGrounded = 0;
    if (GETP(car->player) != NULL && Player_IsHuman(GETP(car->player))) {
        player = GETP(car->player);
        speed = MagnitudeVector(&car->velocity);
        compression = speed / 70.0f;
        compression = 1.0f <= compression ? 1.0f : compression;
        fric = 1.0f - speed / 70.0f;
        fric = 0.0f >= fric ? 0.0f : fric;
        fwdSpeed = _fabs(PSVECDotProduct(&car->velocity, &car->worldMatrix.v[2]));
        sideSpeed = _fabs(PSVECDotProduct(&car->velocity, &car->worldMatrix.v[0]));
        Wheel_PlaySpringForce(player->wheelChannel, 0, 40.0f * compression, 40.0f * compression);
        Wheel_PlayDamperForce(player->wheelChannel, 130.0f * fric, 0xFF);
        if (GETP(car->player)->throttle == 0 && !(car->transmissionFlags & 0x8020)) {
            if (sideSpeed < 2.0f || fwdSpeed > 1.7f * sideSpeed) {
                if (speed < 70.0f && car->gradeOrReverse < 70.0f) {
                    gripLimit += 0.0167f * speed * GETP(GETP(car->vehicleIface)->wheels)->friction;
                }
                if (gripLimit > 1.8f) {
                    gripLimit = 1.8f;
                }
            }
        }
    }
    if (_fabs(PSVECDotProduct(&car->localAxis, &car->worldMatrix.v[1])) < gripLimit) {
        car->transmissionFlags |= 0x100;
    } else {
        car->transmissionFlags &= ~0x100;
    }
    XformVec(&car->worldMatrix, &car->bodyOffset, &bodyPos);
    switch (GETP(car->vehicleIface)->driveMode) {
    case 0:
    case 1:
        rollScale = -GETP(GETP(car->vehicleIface)->accel)->unk4;
        break;
    case 2:
        break;
    }
    base = -(1.0f - base);
    for (i = 0; i < GETP(car->vehicleIface)->numWheels; i++) {
        wheel = &GETP(GETP(car->vehicleIface)->wheels)[i];
        bone = wheel->bone - wheel->parent;
        if (wheel->parent != 0) {
            up = GETP(car->motionState.animMatrices)[bone].v[1];
            fwd = GETP(car->motionState.animMatrices)[bone].v[2];
            side = GETP(car->motionState.animMatrices)[bone].v[0];
        } else {
            up = car->worldMatrix.v[1];
            fwd = car->worldMatrix.v[2];
            side = car->worldMatrix.v[0];
        }
        dir.x = -up.x;
        dir.y = -up.y;
        dir.z = -up.z;
        hit.velocity = g_zeroVector;
        start.x = GETP(car->motionState.animMatrices)[wheel->bone].v[3].x - wheel->offset * dir.x;
        start.y = GETP(car->motionState.animMatrices)[wheel->bone].v[3].y - wheel->offset * dir.y;
        start.z = GETP(car->motionState.animMatrices)[wheel->bone].v[3].z - wheel->offset * dir.z;
        g_pNoRayCollideThing = car;
        dist = 2.0f * wheel->offset + wheel->restLength;
        hitGround = RayHitsTerrainAndObjects(&start, &dir, &dist, &hit);
        g_pNoRayCollideThing = NULL;
        car->wheelStates[i].skidColor.a = 0;
        compression = 2.0f * wheel->offset - dist;
        if (hitGround) {
            len = wheel->restLength + compression;
            car->numWheelsGrounded++;
            if (wheel->driven) {
                scale = len + wheel->offset;
                springForce = wheel->springRate * len;
                one = 1.0f;
                damping = (compression - car->wheelStates[i].lastCompression) / ((f32)g_iLastDeltaTimeMS / 1000.0f) * (one * wheel->damping);
                normalSum.x += hit.normal.x;
                normalSum.y += hit.normal.y;
                normalSum.z += hit.normal.z;
                if (one != GETP(GETP(car->vehicleIface)->engine)->unk6C) {
                    scale = len;
                }
                if (springForce > maxForce && GETP(GETP(hit.surface)->rollSound) != NULL) {
                    rollSurface = (SuspSurface *)GETP(GETP(hit.surface)->rollSound);
                    maxForce = springForce;
                }
                contact.x = dir.x * scale + GETP(car->motionState.animMatrices)[wheel->bone].v[3].x;
                contact.y = dir.y * scale + GETP(car->motionState.animMatrices)[wheel->bone].v[3].y;
                contact.z = dir.z * scale + GETP(car->motionState.animMatrices)[wheel->bone].v[3].z;
                contact.w = 1.0f;
                if (wheel->maxDamping != 0.0f && springForce > 0.0f && damping > wheel->maxDamping) {
                    damping = wheel->maxDamping;
                }
                CarA_ProcessForce(car, &contact, &up, (springForce + damping) * PSVECDotProduct(&up, &hit.normal));
                PSVECSubtract(&contact, &car->worldMatrix.v[3], &r);
                PSVECCrossProduct(&car->localAxis, &r, &rot);
                PSVECAdd(&car->velocity, &rot, &pointVel);
                slip.x = pointVel.x - fwd.x * car->gradeOrReverse - hit.velocity.x;
                slip.y = pointVel.y - fwd.y * car->gradeOrReverse - hit.velocity.y;
                slip.z = pointVel.z - fwd.z * car->gradeOrReverse - hit.velocity.z;
                ProjectVectorToNormal(&hit.normal, &slip);
                car->wheelStates[i].slipVel = slip;
                slipMag = MagnitudeVector(&slip);
                if (slipMag > 0.0f) {
                    PSVECDotProduct(&up, &hit.normal);
                    fric = wheel->friction * springForce;
                    if (fric > wheel->maxFriction) {
                        fric = wheel->maxFriction;
                    }
                    PSVECCrossProduct(&fwd, &hit.normal, &cross);
                    if (PSVECSquareMag(&cross) != 0.0f) {
                        PSVECNormalize(&cross, &cross);
                    }
                    lean = 1.1f * (1.0f - 1.8f * PSVECDotProduct(&cross, &up));
                    if (lean < 0.1f) {
                        lean = 0.1f;
                    } else if (lean > 1.0f) {
                        lean = 1.0f;
                    }
                    if (car->slaveWeaponRpm[0] < 10000) {
                        lean = 0.0f;
                    }
                    if (car->transmissionFlags & 0x8000) {
                        fric *= one - wheel->handbrakeGrip * one;
                    }
                    fric = lean * (GETP(hit.surface)->grip * fric);
                    if ((car->transmissionFlags & 0x100) && fric * g_fDeltaTimeSec > slipMag) {
                        numSlipping++;
                        lean = 1.0f / g_fDeltaTimeSec;
                    } else {
                        lean = fric / slipMag;
                    }
                    steer = 0.0f;
                    friction.x = -slip.x * lean;
                    friction.y = -slip.y * lean;
                    friction.z = -slip.z * lean;
                    player = GETP(car->player);
                    if (player != NULL) {
                        steer = player->stickX;
                    }
                    if (player != NULL && player->brake && !player->throttle) {
                        drive = 0.0f;
                    } else {
                        drive = PSVECDotProduct(&fwd, &friction) * (1.0f - (f32)(wheel->unk14 * car->unk4FC));
                        steer = steer * (-wheel->unk14 * (1.0f - car->unk4FC));
                        friction.x = friction.x - fwd.x * drive;
                        friction.y = friction.y - fwd.y * drive;
                        friction.z = friction.z - fwd.z * drive;
                        friction.x = friction.x - side.x * steer;
                        friction.y = friction.y - side.y * steer;
                        friction.z = friction.z - side.z * steer;
                    }
                    driveSum += drive;
                    ny = hit.normal.y;
                    PSVECSubtract(&contact, &car->worldMatrix.v[3], &r2);
                    d = PSVECDotProduct(&up, &r2);
                    rate = base * d;
                    if (car->lowSpeedFlag < 500) {
                        rate = -1.0f * d * ny;
                    }
                    forcePos.x = up.x * rate + contact.x;
                    forcePos.y = up.y * rate + contact.y;
                    forcePos.z = up.z * rate + contact.z;
                    CarA_ProcessForce(car, &forcePos, &friction, 1.0f);
                    if (i == 2 || i == 3) {
                        sideSum += PSVECDotProduct(&side, &slip);
                    }
                }
                if (ViewManager_GetViewFormat() <= 1 && Player_IsHuman(GETP(car->player))) {
                    if (wheel->dusts && GETP(hit.surface)->sparkThing) {
                        if (wheel->unk14 && slipMag > 20.0f) {
                            sparkRate = 450.0f - 3.0f * (slipMag - 20.0f) * wheel->unk14;
                            if (sparkRate < 50.0f) {
                                sparkRate = 50.0f;
                            }
                            if (sparkRate > 1000.0f) {
                                sparkRate = 1000.0f;
                            }
                            r3scale = 0.01f * sparkRate;
                            one = 1.0f;
                            zero = 0.0f;
                            car->wheelStates[i].sparkTimer += (s16)g_iDeltaTimeMS;
                            while (car->wheelStates[i].sparkTimer > 50) {
                                p1.id = GETP(hit.surface)->sparkThing;
                                p1.pos = contact;
                                p1.animRefPos.w = one;
                                p1.animRefPos.x = zero;
                                p1.animRefPos.y = zero;
                                p1.animRefPos.z = zero;
                                p1.vel = car->velocity;
                                SETP(p1.player, NULL);
                                p1.angVel = g_zeroVector;
                                p1.unk48 = zero;
                                p1.unk4C = r3scale;
                                ThingManager_DARYL_CreateThing(1, &p1);
                                car->wheelStates[i].sparkTimer -= 50;
                            }
                        }
                    }
                    if (GETP(hit.surface)->dustThing && _fabs(car->gradeOrReverse) > 50.0f) {
                        interval = (u32)(GETP(hit.surface)->dustRate * (50.0f / _fabs(car->gradeOrReverse)) + slipMag) + 10;
                        if (interval > 200) {
                            interval = 200;
                        }
                        one = 1.0f;
                        zero = 0.0f;
                        car->wheelStates[i].sparkTimer += (s16)g_iDeltaTimeMS;
                        while (car->wheelStates[i].sparkTimer > (s32)interval) {
                            p2.id = GETP(hit.surface)->dustThing;
                            p2.pos = contact;
                            p2.animRefPos.w = one;
                            p2.animRefPos.x = zero;
                            p2.animRefPos.y = zero;
                            p2.animRefPos.z = zero;
                            p2.vel = car->velocity;
                            SETP(p2.player, NULL);
                            p2.angVel = g_zeroVector;
                            p2.unk48 = zero;
                            p2.unk4C = zero;
                            ThingManager_DARYL_CreateThing(1, &p2);
                            car->wheelStates[i].sparkTimer -= (s16)interval;
                        }
                    }
                    if (GETP(hit.surface)->particle && slipMag > 60.0f && springForce > 60.0f &&
                        Gfx_GetMinCameraDistSquared(&car->worldMatrix.v[3]) < 40000.0f) {
                        n = 62500.0f / springForce * GETP(hit.surface)->particleInterval;
                        car->wheelStates[i].dustTimer += (s16)g_iDeltaTimeMS;
                        while (car->wheelStates[i].dustTimer > (u16)n && ParticleManager_CanCreatureParticle(PTR(GETP(hit.surface)->particle))) {
                            f32 slide = 0.3f * car->gradeOrReverse * wheel->unk14;
                            f32 accel[4] = {0.0f, -0.0001f, 0.0f, 0.0f};
                            f32 rnd = wheel->unk14 * RandomAdd(5.0f, 30.0f);
                            f32 pcolor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                            Vector4 vel;

                            vel.x = 0.001f * (up.x * rnd + (1.02f * car->velocity.x - fwd.x * slide));
                            vel.y = 0.001f * (up.y * rnd + (1.02f * car->velocity.y - fwd.y * slide));
                            vel.z = 0.001f * (up.z * rnd + (1.02f * car->velocity.z - fwd.z * slide));
                            CreateParticle(&contact, &vel, accel, pcolor, PTR(GETP(hit.surface)->particle), 1.0f, 0.0f);
                            car->wheelStates[i].dustTimer -= n;
                        }
                    }
                }
                if (slipMag > 110.0f) {
                    info = GETP(car->vehicleIface);
                    if (info->skidsEnabled && wheel->skids && GETP(hit.surface)->type != 1) {
                        color = *GETP(info->skidColor);
                        skid = 0.025f * springForce;
                        tex = info->skidTexture;
                        maxAlpha = color.a;
                        if (skid < 0.03f) {
                            skid = 0.03f;
                        }
                        if (skid > 1.0f) {
                            skid = 1.0f;
                        }
                        if (GETP(hit.surface)->type == 2) {
                            tex = GETP(hit.surface)->skidTexture;
                            color = *GETP(GETP(hit.surface)->skidColor);
                        }
                        if (slipMag > 110.0f) {
                            volume += 0.5f * skid;
                        } else {
                            volume += 0.5f * skid * (slipMag / 110.0f);
                        }
                        color.a = 255.0f * skid;
                        if (color.a > maxAlpha) {
                            color.a = maxAlpha;
                        }
                        if (skid > 0.01f) {
                            car->wheelStates[i].skidColor = color;
                            car->wheelStates[i].skidTexture = tex;
                        }
                    }
                }
            }
        } else {
            car->wheelStates[i].slipVel = g_zeroVector;
        }
        car->wheelStates[i].lastCompression = compression;
        if (GETP(car->vehicleIface)->driveMode != 1 || compression < 0.3f) {
            car->wheelStates[i].compression = compression;
        }
    }
    player = GETP(car->player);
    if (player != NULL && !(player->buttonsHeld & 4)) {
        car->rpm = car->unk4FC * (g_fDeltaTimeSec * (driveSum * rollScale)) + car->rpm;
        car->gradeOrReverse = car->rpm * GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
        if (car->transmissionFlags & 0x1000) {
            maxGrade = GETP(GETP(car->vehicleIface)->engine)->unk44 * GETP(car->vehicleIface)->unk30;
            if (car->gradeOrReverse > maxGrade) {
                car->gradeOrReverse = maxGrade;
            }
        } else {
            maxGrade = GETP(GETP(car->vehicleIface)->engine)->unk44;
            if (car->gradeOrReverse > maxGrade) {
                car->gradeOrReverse = maxGrade;
            }
        }
    }
    if (GETP(car->player) != NULL && Player_IsHuman(GETP(car->player))) {
        player = GETP(car->player);
        if (sideSum > 0.0f) {
            force = 100.0f * (sideSum / 30.0f);
            force = 100.0f <= force ? 100.0f : force;
            Wheel_PlayConstantForce(player->wheelChannel, force, 0x10E);
        } else {
            force = 100.0f * (-sideSum / 30.0f);
            force = 100.0f <= force ? 100.0f : force;
            Wheel_PlayConstantForce(player->wheelChannel, force, 0x5A);
        }
        if (0.0f == sideSum) {
            if (!(car->transmissionFlags & 0x10000)) {
                Wheel_PlayCarAirborne(player->wheelChannel);
            }
        } else {
            Wheel_StopCarAirborne(player->wheelChannel);
        }
    }
    player = GETP(car->player);
    if (player != NULL && !player->throttle && !player->brake && !player->button14 &&
        _fabs(car->gradeOrReverse) < 1.0f && numSlipping == GETP(car->vehicleIface)->numWheels &&
        PSVECDotProduct(&car->localAxis, &car->worldMatrix.v[1]) < 0.9f) {
        ProjectVectorToNormal(&car->worldMatrix.v[0], &car->velocity);
        spin = 0.5f * PSVECDotProduct(&car->angularMomentum, &car->worldMatrix.v[1]);
        car->angularMomentum.x = car->angularMomentum.x - spin * car->worldMatrix.v[1].x;
        car->angularMomentum.y = car->angularMomentum.y - spin * car->worldMatrix.v[1].y;
        car->angularMomentum.z = car->angularMomentum.z - spin * car->worldMatrix.v[1].z;
    }
    if ((u32)car->soundIds[2] != 0xFFFFFFFF) {
        if (rollSurface != NULL) {
            sample = *(u32 *)&((SuspSurface *)rollSurface)->grip;
            if (volume > 1.0f) {
                volume = 1.0f;
            }
            if ((void *)rollSurface != GETP(car->rollSurface)) {
                silence2 = 0.0f;
                SoundSource_Set(car->soundIds[2], 0, &silence2);
                SoundSource_Set(car->soundIds[2], 8, &sample);
                SETP(car->rollSurface, rollSurface);
            } else {
                SoundSource_Set(car->soundIds[2], 0, &volume);
                SoundSource_Set(car->soundIds[2], 5, &car->worldMatrix.v[3]);
                SoundSource_Set(car->soundIds[2], 6, &car->velocity);
            }
        } else {
            silence = 0.0f;
            SoundSource_Set(car->soundIds[2], 0, &silence);
        }
    }
    if (car->numWheelsGrounded == GETP(car->vehicleIface)->numWheels) {
        car->groundNormal = normalSum;
        if (PSVECSquareMag(&car->groundNormal) != 0.0f) {
            PSVECNormalize(&car->groundNormal, &car->groundNormal);
        }
    }
    CarA_ComputeSuspensionForces_Aux1(car);
}
