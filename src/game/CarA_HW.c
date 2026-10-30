#include "CarA.h"
#include "common.h"

typedef struct CarAShiftSoundParams {
    Vector4 pos;
    Vector4 animRefPos;
    Vector4 vel;
    Vector4 angVel;
    u32 id;
    PTR32(void) player;
    f32 unk48;
    f32 unk4C;
} CarAShiftSoundParams;

extern f32 g_fDeltaTimeSec;
extern u32 g_iDeltaTimeMS;
extern void *g_pSourcePlayer;
extern Vector4 g_zeroVector;
extern char stunt_msg_buffer[];
extern char *g_stuntLossString;



s8 CarA_DoFreeStyleStunts(CarA *car) {
    s32 threshold;
    Player *pad;

    threshold = GETP(GETP(car->vehicleIface)->stunt)->unk28;
    if (GETP(car->player) == NULL || !Player_IsHuman(GETP(car->player))) {
        return 0;
    }
    pad = GETP(car->player);
    if (pad->button10 == 0 && pad->button1A == 0 &&
        pad->stickX > -0x40 && pad->stickX < 0x40 && pad->stickY > -0x40 && pad->stickY < 0x40) {
        return 0;
    }
    if (car->unk460 > threshold && car->lowSpeedFlag > threshold &&
        !(car->transmissionFlags & 0x40002)) {
        return 1;
    }
    return 0;
}

void CarA_StuntImpulse(CarA *car, Vector4 *linImpulse, Vector4 *angImpulse) {
    Vector4 lin;
    Vector4 ang;
    Vector4 scaled __attribute__((aligned(16)));
    CarAInfo *info;

    scaled.x = angImpulse->x / car->inverseBodyInertia.v[0].x;
    scaled.y = angImpulse->y / car->inverseBodyInertia.v[1].y;
    scaled.z = angImpulse->z / car->inverseBodyInertia.v[2].z;
    XformVecOrientation(&car->worldMatrix, &scaled, &ang);
    XformVecOrientation(&car->worldMatrix, linImpulse, &lin);
    car->velocity.x += lin.x;
    car->velocity.y += lin.y;
    car->velocity.z += lin.z;
    car->angularMomentum.x += ang.x;
    car->angularMomentum.y += ang.y;
    car->angularMomentum.z += ang.z;
    car->transmissionFlags |= 2;
    info = GETP(car->vehicleIface);
    if (info->numSlaves > 4) {
        MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(info->slaves) + 0x34, 4);
    }
}


s32 CarA_StuntsGetNewOrientation(CarA *car, Quaternion *outQ, f32 t) {
    Vector4 accum __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    Quaternion q;
    Quaternion newQ;
    f32 rates[3] = {0.0f, 0.0f, 0.0f};
    Vector4 axis __attribute__((aligned(16)));
    Vector4 scaled;
    Vector4 local;
    CarAStuntInfo *stunt;
    Player *player;
    f32 scale;
    f32 frac;
    f32 spin;
    f32 roll;
    f32 mag;

    stunt = GETP(GETP(car->vehicleIface)->stunt);
    scale = 1.0f;
    frac = (f32)(u32)(car->unk460 - stunt->unk28) / stunt->maxTime;
    if (!CarA_DoFreeStyleStunts(car)) {
        car->stuntAxis = 0;
        return 0;
    }
    if (GETP(car->player)->stickX > 100 || GETP(car->player)->stickX < -100) {
        rates[0] = 1.0f;
        if (car->stuntAxis != 1) {
            car->stuntAxisVec = car->worldMatrix.v[1];
            car->stuntAxis = 1;
        }
        if (GETP(car->player)->stickX > 0) {
            scale = -1.0f;
        }
    } else if (GETP(car->player)->stickY > 100 || GETP(car->player)->stickY < -100) {
        rates[1] = 1.0f;
        if (car->stuntAxis != 2) {
            car->stuntAxisVec = car->worldMatrix.v[0];
            car->stuntAxis = 2;
        }
        if (GETP(car->player)->stickY > 0) {
            scale = -1.0f;
        }
    } else {
        car->stuntAxis = 0;
    }
    if (frac > 1.0f) {
        frac = 1.0f;
    }
    spin = scale;
    switch (car->stuntAxis) {
    case 0:
        spin *= 0.0f;
        break;
    case 1:
        spin *= GETP(GETP(car->vehicleIface)->stunt)->pitchRate;
        break;
    case 2:
        spin *= -GETP(GETP(car->vehicleIface)->stunt)->rollRate;
        break;
    }
    accum.x += car->stuntAxisVec.x * spin;
    accum.y += car->stuntAxisVec.y * spin;
    accum.z += car->stuntAxisVec.z * spin;
    stunt = GETP(GETP(car->vehicleIface)->stunt);
    player = GETP(car->player);
    roll = 0.0f;
    scale = 0.5f * (stunt->pitchRate + stunt->spinRate);
    if (player->button10) {
        roll -= stunt->spinRate;
        rates[2] = 1.0f;
    }
    if (player->button1A) {
        roll += stunt->spinRate;
        rates[2] = 1.0f;
    }
    accum.x += car->worldMatrix.v[2].x * roll;
    accum.y += car->worldMatrix.v[2].y * roll;
    accum.z += car->worldMatrix.v[2].z * roll;
    if (!(car->transmissionFlags & 0x20000)) {
        stunt = GETP(GETP(car->vehicleIface)->stunt);
        car->stuntTime += g_fDeltaTimeSec * (stunt->rollRate * (rates[0] * stunt->rollScore)) +
                          g_fDeltaTimeSec * (stunt->pitchRate * (rates[1] * stunt->pitchScore)) +
                          g_fDeltaTimeSec * (stunt->spinRate * (rates[2] * stunt->spinScore));
        car->transmissionFlags |= 0x20000;
    }
    axis = accum;
    mag = MagnitudeVector(&axis);
    if (mag > scale) {
        mag = scale;
        accum.x *= scale / mag;
        accum.y *= scale / mag;
        accum.z *= scale / mag;
    }
    if (mag > 1.6f) {
        t = mag * t;
        GenerateQuaternion(&axis, &q, frac * t);
        QuaternionMult((Vector4 *)&q, &car->animRefPos, (Vector4 *)&newQ);
        InverseXformVecOrientation(&car->worldMatrix, &accum, &local);
        scaled.x = local.x / car->inverseBodyInertia.v[0].x;
        scaled.y = local.y / car->inverseBodyInertia.v[1].y;
        scaled.z = local.z / car->inverseBodyInertia.v[2].z;
        XformVecOrientation(&car->worldMatrix, &scaled, &car->angularMomentum);
        car->stuntSpin.x += 3.0f * t;
        *outQ = newQ;
        if (car->stuntTime > GETP(GETP(car->vehicleIface)->stunt)->minFailTime) {
            player = GETP(car->player);
            sprintf(GETP(player->stuntScoreText), "Points +%d", (u32)car->stuntTime);
            SETP(car->stuntScoreMessage, &player->stuntScoreText);
        }
        return 1;
    }
    return 0;
}


void CarA_StuntFail(CarA *car) {
    CarAStuntInfo *stunt;
    u32 count;
    u32 secs;
    u32 idx;
    f32 minTime;
    f32 maxTime;
    CarAShiftSoundParams params __attribute__((aligned(16)));

    stunt = GETP(GETP(car->vehicleIface)->stunt);
    minTime = stunt->minFailTime;
    if (car->stuntTime > minTime) {
        count = stunt->numFailThings;
        maxTime = stunt->maxFailTime;
        secs = car->stuntTime;
        if (GETP(stunt->failMessage) != NULL) {
            SETP(car->stuntMessage, GETP(stunt->failMessage));
        } else {
            sprintf(stunt_msg_buffer, "Stunt Failed");
            SETP(car->stuntMessage, &g_stuntLossString);
        }
        if (GETP(car->vehicleIface)->numSlaves > 10) {
            MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->slaves) + 0x7C, 10);
        }
        if (count != 0 && (f32)secs > minTime) {
            idx = 0;
            if (count > 1) {
                idx = (f32)count * (((f32)secs - minTime) / (maxTime - minTime)) + 0.5f;
                if (idx >= count) {
                    idx = count - 1;
                }
            }
            params.id = GETP(stunt->failThings)[idx];
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
    car->stuntTime = 0.0f;
    car->stuntSpin = g_zeroVector;
    car->transmissionFlags |= 0x40000;
}

void CarA_SlaveWeaponHeartbeat(CarA *car) {
    u32 i;
    s32 *rpm;

    for (i = 0; i < GETP(car->vehicleIface)->numSlaveWeapons; i++) {
        rpm = &car->slaveWeaponRpm[i];
        if (*rpm < 10000) {
            MotionState_InitSlavesTimed(&car->motionState, GETP(GETP(car->vehicleIface)->slaveWeapons) + i * 0xC + 4, i + 0xF0, *rpm);
            ThingManagerList_RemoveReleasedThings(&car->motionState.slaves);
            *rpm += g_iDeltaTimeMS;
        }
    }
}

void CarA_SlaveWeaponStop(CarA *car, s32 weapon) {

    car->slaveWeaponRpm[weapon] = 10001;
    MotionState_ReleaseSlavesByType(HND(&car->motionState), weapon + 0xF0);
    ThingManagerList_RemoveReleasedThings(&car->motionState.slaves);
}

s32 CarA_PreMove_Control_HW(void *thing) {
    CarA *car = thing;
    u8 mode = 0;
    u8 throttling = 0;
    u8 reversing = 0;
    f32 decel = -GETP(GETP(car->vehicleIface)->accel)->unk18;
    f32 coast;
    f32 steerRate;
    f32 steer;
    f32 torque;
    f32 accel;
    u32 id;
    CarAInfo *info;

    if (GETP(car->player)->button15) {
        car->transmissionFlags |= 0x8000;
    } else {
        car->transmissionFlags &= ~0x8000;
    }

    if (!CarA_DoFreeStyleStunts(car)) {
        if (GETP(car->player)->throttle) {
            if (GETP(car->player)->brake == 0 && car->gradeOrReverse > -2.0f) {
                if (((car->transmissionFlags & 0x1000) &&
                     car->rpm >= GETP(car->vehicleIface)->unk1C * GETP(car->vehicleIface)->unk30) ||
                    (!(car->transmissionFlags & 0x1000) && car->rpm >= GETP(car->vehicleIface)->unk1C)) {
                    car->gradeOrReverse += -0.1f * car->gradeOrReverse * g_fDeltaTimeSec;
                    car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
                    if (car->rpm < GETP(car->vehicleIface)->unk24 && car->curveTableIdx > 1) {
                        car->curveTableIdx--;
                        car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
                    }
                    mode = 2;
                    decel = 255.0f * (0.2f * -GETP(GETP(car->vehicleIface)->accel)->unk1C);
                } else {
                    throttling = 1;
                }
            } else {
                car->reverseTimer = 0;
                mode = 1;
                decel = -(f32)GETP(car->player)->throttle * GETP(GETP(car->vehicleIface)->accel)->unk20;
            }
        } else if (GETP(car->player)->brake) {
            if (GETP(car->player)->throttle == 0 && car->gradeOrReverse < 1.0f) {
                car->reverseTimer += g_iDeltaTimeMS;
                if (car->reverseTimer > GETP(GETP(car->vehicleIface)->engine)->unk18) {
                    reversing = 1;
                    car->transmissionFlags |= 4;
                } else {
                    mode = 1;
                }
            } else {
                car->reverseTimer = 0;
                mode = 1;
                decel = -(f32)GETP(car->player)->brake * GETP(GETP(car->vehicleIface)->accel)->unk18;
            }
        } else {
            coast = 0.05f * car->gradeOrReverse;
            car->gradeOrReverse += -0.1f * car->gradeOrReverse * g_fDeltaTimeSec;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            if (car->rpm < GETP(car->vehicleIface)->unk24 && car->curveTableIdx > 1) {
                car->curveTableIdx--;
                car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
                if (GETP(car->vehicleIface)->numShiftSounds != 0) {
                    id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
                    if (id != 0) {
                        CarAShiftSoundParams p __attribute__((aligned(16)));
                        p.id = id;
                        p.pos = car->worldMatrix.v[3];
                        p.animRefPos = car->animRefPos;
                        p.vel = car->velocity;
                        SETP(p.player, NULL);
                        p.unk48 = 0.0f;
                        p.unk4C = 0.0f;
                        p.angVel = g_zeroVector;
                        ThingManager_DARYL_CreateThing(1, &p);
                    }
                }
            }
            mode = 2;
            decel = 255.0f * (0.5f * -GETP(GETP(car->vehicleIface)->accel)->unk1C);
            if (car->gradeOrReverse < 20.0f) {
                if (coast < 0.1f) {
                    coast = 0.1f;
                }
                decel *= coast;
            }
        }
    }

    g_pSourcePlayer = GETP(car->player);
    if (reversing) {
        torque = 0.5f * CarA_Transmission_GetTorqueAtWheels(car);
        accel = -torque * (f32)GETP(car->player)->brake;
        car->rpm += accel * g_fDeltaTimeSec;
        if (car->rpm < -GETP(car->vehicleIface)->unk20) {
            car->rpm = -GETP(car->vehicleIface)->unk20;
        }
        if (car->rpm < GETP(car->vehicleIface)->unk24 && car->curveTableIdx > 1) {
            car->curveTableIdx--;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            if (GETP(car->vehicleIface)->numShiftSounds != 0) {
                id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
                if (id != 0) {
                    CarAShiftSoundParams p __attribute__((aligned(16)));
                    p.id = id;
                    p.pos = car->worldMatrix.v[3];
                    p.animRefPos = car->animRefPos;
                    p.vel = car->velocity;
                    SETP(p.player, NULL);
                    p.unk48 = 0.0f;
                    p.unk4C = 0.0f;
                    p.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
        }
        car->gradeOrReverse = car->rpm * GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
    } else if (throttling) {
        torque = 0.5f * CarA_Transmission_GetTorqueAtWheels(car);
        if (!(car->transmissionFlags & 0x1000)) {
            if (car->rpm >= GETP(car->vehicleIface)->unk1C) {
                accel = 0.0f;
            } else {
                accel = torque * (f32)GETP(car->player)->throttle;
            }
        } else {
            accel = torque * (f32)GETP(car->player)->throttle;
        }
        if (GETP(car->player)->button15 && car->rpm < 1000.0f) {
            if (car->rpm < -1000.0f) {
                car->unk4FC = 1.0f;
            } else {
                car->unk4FC = 0.0f;
            }
            car->rpm = 6000.0f;
        } else {
            car->rpm += accel * g_fDeltaTimeSec;
            if (car->rpm < 0.0f) {
                car->rpm *= -1.0f;
            }
        }
        if (GETP(car->player)->button15 && car->unk4FC < 0.5f) {
            car->unk4FC = 0.2f;
        }
        if (car->rpm > GETP(car->vehicleIface)->unk20 &&
            car->curveTableIdx < GETP(car->vehicleIface)->numGears - 1) {
            car->curveTableIdx++;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
        }
        car->gradeOrReverse = car->rpm * GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
        if (car->transmissionFlags & 0x1000) {
            if (car->gradeOrReverse > GETP(GETP(car->vehicleIface)->engine)->unk44 * GETP(car->vehicleIface)->unk30) {
                car->gradeOrReverse = GETP(GETP(car->vehicleIface)->engine)->unk44 * GETP(car->vehicleIface)->unk30;
            }
        } else {
            if (car->gradeOrReverse > GETP(GETP(car->vehicleIface)->engine)->unk44) {
                car->gradeOrReverse = GETP(GETP(car->vehicleIface)->engine)->unk44;
            }
        }
    }

    if (mode) {
        if (car->gradeOrReverse > 0.0f) {
            car->gradeOrReverse += decel * g_fDeltaTimeSec;
            if (car->gradeOrReverse < 0.0f) {
                car->gradeOrReverse = 0.0f;
            }
        } else {
            car->gradeOrReverse -= decel * g_fDeltaTimeSec;
            if (car->gradeOrReverse > 0.0f) {
                car->gradeOrReverse = 0.0f;
            }
        }
        car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
        if (car->rpm < GETP(car->vehicleIface)->unk24 && car->curveTableIdx > 1) {
            car->curveTableIdx--;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            if (GETP(car->vehicleIface)->numShiftSounds != 0) {
                id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
                if (id != 0) {
                    CarAShiftSoundParams p __attribute__((aligned(16)));
                    p.id = id;
                    p.pos = car->worldMatrix.v[3];
                    p.animRefPos = car->animRefPos;
                    p.vel = car->velocity;
                    SETP(p.player, NULL);
                    p.unk48 = 0.0f;
                    p.unk4C = 0.0f;
                    p.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
        }
        if (car->rpm < -GETP(car->vehicleIface)->unk1C) {
            car->rpm = -GETP(car->vehicleIface)->unk1C;
        }
        if (!(car->transmissionFlags & 0x20) && mode == 1) {
            car->transmissionFlags |= 0x20;
            if (GETP(car->vehicleIface)->numSlaves >= 3) {
                MotionState_InitAllSlavesNow(HND(&car->motionState), GETP(GETP(car->vehicleIface)->slaves) + 0x28, 3);
            }
        } else if (mode == 2 && (car->transmissionFlags & 0x20)) {
            car->transmissionFlags &= ~0x20;
            MotionState_ReleaseSlavesByType(HND(&car->motionState), 3);
        }
    } else if (car->transmissionFlags & 0x20) {
        car->transmissionFlags &= ~0x20;
        MotionState_ReleaseSlavesByType(HND(&car->motionState), 3);
    }

    info = GETP(car->vehicleIface);
    steerRate = GETP(info->engine)->unk14;
    steer = 0.017453292f * (45.0f * -((f32)GETP(car->player)->stickX / 128.0f));
    if (CarA_DoFreeStyleStunts(car)) {
        steer = 0.0f;
    }
    if (car->engineRpmState < 10000) {
        steer *= 0.5f;
    }
    if (car->unk378 < steer) {
        car->unk378 += steerRate * g_fDeltaTimeSec;
        if (car->unk378 > steer) {
            car->unk378 = steer;
        }
    } else {
        car->unk378 -= steerRate * g_fDeltaTimeSec;
        if (car->unk378 < steer) {
            car->unk378 = steer;
        }
    }
    g_pSourcePlayer = NULL;
    return 1;
}
