#include "CarA.h"
#include "common.h"
#include "World.h"

typedef struct CarAShiftSoundParams {
    Vector4 pos;
    Vector4 animRefPos;
    Vector4 vel;
    Vector4 angVel;
    u32 id;
    PTR32(void) player;
    f32 speedScale;
    f32 unusedParam;
} CarAShiftSoundParams;

extern f32 g_fDeltaTimeSec;
extern u32 g_iDeltaTimeMS;
extern void *g_pSourcePlayer;
extern Vector4 g_zeroVector;
extern char stunt_msg_buffer[];
extern u8 g_stuntLossString[];  /* port: a table of string slots */



s8 CarA_DoFreeStyleStunts(CarA *car) {
    s32 threshold;
    Player *pad;

    threshold = GETP(GETP(car->vehicleIface)->stunt)->startDelay;
    if (GETP(car->base.player) == NULL || !Player_IsHuman(GETP(car->base.player))) {
        return 0;
    }
    pad = GETP(car->base.player);
    if (pad->button10 == 0 && pad->button1A == 0 &&
        pad->stickX > -0x40 && pad->stickX < 0x40 && pad->stickY > -0x40 && pad->stickY < 0x40) {
        return 0;
    }
    if (car->airTime > threshold && car->lowSpeedFlag > threshold &&
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
    XformVecOrientation(THING_MTX(&car->base), &scaled, &ang);
    XformVecOrientation(THING_MTX(&car->base), linImpulse, &lin);
    car->base.vel.x += lin.x;
    car->base.vel.y += lin.y;
    car->base.vel.z += lin.z;
    car->angularMomentum.x += ang.x;
    car->angularMomentum.y += ang.y;
    car->angularMomentum.z += ang.z;
    car->transmissionFlags |= 2;
    info = GETP(car->vehicleIface);
    if (info->numSlaves > 4) {
        MotionState_InitAllSlavesNow(&car->motionState, &GETP(info->slaves)[4].list, 4);
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
    frac = (f32)(u32)(car->airTime - stunt->startDelay) / stunt->maxTime;
    if (!CarA_DoFreeStyleStunts(car)) {
        car->stuntAxis = 0;
        return 0;
    }
    if (CARA_PLAYER(car)->stickX > 100 || CARA_PLAYER(car)->stickX < -100) {
        rates[0] = 1.0f;
        if (car->stuntAxis != 1) {
            car->stuntAxisVec = car->base.axis[1];
            car->stuntAxis = 1;
        }
        if (CARA_PLAYER(car)->stickX > 0) {
            scale = -1.0f;
        }
    } else if (CARA_PLAYER(car)->stickY > 100 || CARA_PLAYER(car)->stickY < -100) {
        rates[1] = 1.0f;
        if (car->stuntAxis != 2) {
            car->stuntAxisVec = car->base.axis[0];
            car->stuntAxis = 2;
        }
        if (CARA_PLAYER(car)->stickY > 0) {
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
    player = GETP(car->base.player);
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
    accum.x += car->base.axis[2].x * roll;
    accum.y += car->base.axis[2].y * roll;
    accum.z += car->base.axis[2].z * roll;
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
        QuaternionMult((Vector4 *)&q, &car->base.quat, (Vector4 *)&newQ);
        InverseXformVecOrientation(THING_MTX(&car->base), &accum, &local);
        scaled.x = local.x / car->inverseBodyInertia.v[0].x;
        scaled.y = local.y / car->inverseBodyInertia.v[1].y;
        scaled.z = local.z / car->inverseBodyInertia.v[2].z;
        XformVecOrientation(THING_MTX(&car->base), &scaled, &car->angularMomentum);
        car->stuntSpin.x += 3.0f * t;
        *outQ = newQ;
        if (car->stuntTime > GETP(GETP(car->vehicleIface)->stunt)->minFailTime) {
            player = GETP(car->base.player);
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
            MotionState_InitAllSlavesNow(&car->motionState, &GETP(GETP(car->vehicleIface)->slaves)[10].list, 10);
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
                params.pos = car->base.pos;
                params.animRefPos = *(Vector4 *)&car->base.quat;
                params.vel.x = car->base.vel.x;
                params.vel.y = 0.0f;
                params.vel.z = car->base.vel.z;
                SETP(params.player, NULL);
                params.speedScale = 0.0f;
                params.unusedParam = 0.0f;
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
            MotionState_InitSlavesTimed(&car->motionState, &GETP(GETP(car->vehicleIface)->slaveWeapons)[i].list, i + 0xF0, *rpm);
            ThingManagerList_RemoveReleasedThings(&car->motionState.slaves);
            *rpm += g_iDeltaTimeMS;
        }
    }
}

void CarA_SlaveWeaponStop(CarA *car, s32 weapon) {

    car->slaveWeaponRpm[weapon] = 10001;
    MotionState_ReleaseSlavesByType(&car->motionState, weapon + 0xF0);
    ThingManagerList_RemoveReleasedThings(&car->motionState.slaves);
}

s32 CarA_PreMove_Control_HW(void *thing) {
    CarA *car = thing;
    u8 mode = 0;
    u8 throttling = 0;
    u8 reversing = 0;
    f32 decel = -GETP(GETP(car->vehicleIface)->accel)->brakeDecel;
    f32 coast;
    f32 steerRate;
    f32 steer;
    f32 torque;
    f32 accel;
    u32 id;
    CarAInfo *info;

    if (CARA_PLAYER(car)->button15) {
        car->transmissionFlags |= 0x8000;
    } else {
        car->transmissionFlags &= ~0x8000;
    }

    if (!CarA_DoFreeStyleStunts(car)) {
        if (CARA_PLAYER(car)->throttle) {
            if (CARA_PLAYER(car)->brake == 0 && car->gradeOrReverse > -2.0f) {
                if (((car->transmissionFlags & 0x1000) &&
                     car->rpm >= GETP(car->vehicleIface)->maxRpm * GETP(car->vehicleIface)->turboRpmScale) ||
                    (!(car->transmissionFlags & 0x1000) && car->rpm >= GETP(car->vehicleIface)->maxRpm)) {
                    car->gradeOrReverse += -0.1f * car->gradeOrReverse * g_fDeltaTimeSec;
                    car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
                    if (car->rpm < GETP(car->vehicleIface)->downshiftRpm && car->curveTableIdx > 1) {
                        car->curveTableIdx--;
                        car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
                    }
                    mode = 2;
                    decel = 255.0f * (0.2f * -GETP(GETP(car->vehicleIface)->accel)->engineBrake);
                } else {
                    throttling = 1;
                }
            } else {
                car->reverseTimer = 0;
                mode = 1;
                decel = -(f32)CARA_PLAYER(car)->throttle * GETP(GETP(car->vehicleIface)->accel)->reverseThrottleDecel;
            }
        } else if (CARA_PLAYER(car)->brake) {
            if (CARA_PLAYER(car)->throttle == 0 && car->gradeOrReverse < 1.0f) {
                car->reverseTimer += g_iDeltaTimeMS;
                if (car->reverseTimer > GETP(GETP(car->vehicleIface)->engine)->reverseDelay) {
                    reversing = 1;
                    car->transmissionFlags |= 4;
                } else {
                    mode = 1;
                }
            } else {
                car->reverseTimer = 0;
                mode = 1;
                decel = -(f32)CARA_PLAYER(car)->brake * GETP(GETP(car->vehicleIface)->accel)->brakeDecel;
            }
        } else {
            coast = 0.05f * car->gradeOrReverse;
            car->gradeOrReverse += -0.1f * car->gradeOrReverse * g_fDeltaTimeSec;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            if (car->rpm < GETP(car->vehicleIface)->downshiftRpm && car->curveTableIdx > 1) {
                car->curveTableIdx--;
                car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
                if (GETP(car->vehicleIface)->numShiftSounds != 0) {
                    id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
                    if (id != 0) {
                        CarAShiftSoundParams p __attribute__((aligned(16)));
                        p.id = id;
                        p.pos = car->base.pos;
                        p.animRefPos = *(Vector4 *)&car->base.quat;
                        p.vel = car->base.vel;
                        SETP(p.player, NULL);
                        p.speedScale = 0.0f;
                        p.unusedParam = 0.0f;
                        p.angVel = g_zeroVector;
                        ThingManager_DARYL_CreateThing(1, &p);
                    }
                }
            }
            mode = 2;
            decel = 255.0f * (0.5f * -GETP(GETP(car->vehicleIface)->accel)->engineBrake);
            if (car->gradeOrReverse < 20.0f) {
                if (coast < 0.1f) {
                    coast = 0.1f;
                }
                decel *= coast;
            }
        }
    }

    g_pSourcePlayer = GETP(car->base.player);
    if (reversing) {
        torque = 0.5f * CarA_Transmission_GetTorqueAtWheels(car);
        accel = -torque * (f32)CARA_PLAYER(car)->brake;
        car->rpm += accel * g_fDeltaTimeSec;
        if (car->rpm < -GETP(car->vehicleIface)->upshiftRpm) {
            car->rpm = -GETP(car->vehicleIface)->upshiftRpm;
        }
        if (car->rpm < GETP(car->vehicleIface)->downshiftRpm && car->curveTableIdx > 1) {
            car->curveTableIdx--;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            if (GETP(car->vehicleIface)->numShiftSounds != 0) {
                id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
                if (id != 0) {
                    CarAShiftSoundParams p __attribute__((aligned(16)));
                    p.id = id;
                    p.pos = car->base.pos;
                    p.animRefPos = *(Vector4 *)&car->base.quat;
                    p.vel = car->base.vel;
                    SETP(p.player, NULL);
                    p.speedScale = 0.0f;
                    p.unusedParam = 0.0f;
                    p.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
        }
        car->gradeOrReverse = car->rpm * GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
    } else if (throttling) {
        torque = 0.5f * CarA_Transmission_GetTorqueAtWheels(car);
        if (!(car->transmissionFlags & 0x1000)) {
            if (car->rpm >= GETP(car->vehicleIface)->maxRpm) {
                accel = 0.0f;
            } else {
                accel = torque * (f32)CARA_PLAYER(car)->throttle;
            }
        } else {
            accel = torque * (f32)CARA_PLAYER(car)->throttle;
        }
        if (CARA_PLAYER(car)->button15 && car->rpm < 1000.0f) {
            if (car->rpm < -1000.0f) {
                car->gripBlend = 1.0f;
            } else {
                car->gripBlend = 0.0f;
            }
            car->rpm = 6000.0f;
        } else {
            car->rpm += accel * g_fDeltaTimeSec;
            if (car->rpm < 0.0f) {
                car->rpm *= -1.0f;
            }
        }
        if (CARA_PLAYER(car)->button15 && car->gripBlend < 0.5f) {
            car->gripBlend = 0.2f;
        }
        if (car->rpm > GETP(car->vehicleIface)->upshiftRpm &&
            car->curveTableIdx < GETP(car->vehicleIface)->numGears - 1) {
            car->curveTableIdx++;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
        }
        car->gradeOrReverse = car->rpm * GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
        if (car->transmissionFlags & 0x1000) {
            if (car->gradeOrReverse > GETP(GETP(car->vehicleIface)->engine)->maxGrade * GETP(car->vehicleIface)->turboRpmScale) {
                car->gradeOrReverse = GETP(GETP(car->vehicleIface)->engine)->maxGrade * GETP(car->vehicleIface)->turboRpmScale;
            }
        } else {
            if (car->gradeOrReverse > GETP(GETP(car->vehicleIface)->engine)->maxGrade) {
                car->gradeOrReverse = GETP(GETP(car->vehicleIface)->engine)->maxGrade;
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
        if (car->rpm < GETP(car->vehicleIface)->downshiftRpm && car->curveTableIdx > 1) {
            car->curveTableIdx--;
            car->rpm = car->gradeOrReverse / GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio;
            if (GETP(car->vehicleIface)->numShiftSounds != 0) {
                id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
                if (id != 0) {
                    CarAShiftSoundParams p __attribute__((aligned(16)));
                    p.id = id;
                    p.pos = car->base.pos;
                    p.animRefPos = *(Vector4 *)&car->base.quat;
                    p.vel = car->base.vel;
                    SETP(p.player, NULL);
                    p.speedScale = 0.0f;
                    p.unusedParam = 0.0f;
                    p.angVel = g_zeroVector;
                    ThingManager_DARYL_CreateThing(1, &p);
                }
            }
        }
        if (car->rpm < -GETP(car->vehicleIface)->maxRpm) {
            car->rpm = -GETP(car->vehicleIface)->maxRpm;
        }
        if (!(car->transmissionFlags & 0x20) && mode == 1) {
            car->transmissionFlags |= 0x20;
            if (GETP(car->vehicleIface)->numSlaves >= 3) {
                MotionState_InitAllSlavesNow(&car->motionState, &GETP(GETP(car->vehicleIface)->slaves)[3].list, 3);
            }
        } else if (mode == 2 && (car->transmissionFlags & 0x20)) {
            car->transmissionFlags &= ~0x20;
            MotionState_ReleaseSlavesByType(&car->motionState, 3);
        }
    } else if (car->transmissionFlags & 0x20) {
        car->transmissionFlags &= ~0x20;
        MotionState_ReleaseSlavesByType(&car->motionState, 3);
    }

    info = GETP(car->vehicleIface);
    steerRate = GETP(info->engine)->steerRate;
    steer = 0.017453292f * (45.0f * -((f32)CARA_PLAYER(car)->stickX / 128.0f));
    if (CarA_DoFreeStyleStunts(car)) {
        steer = 0.0f;
    }
    if (car->engineRpmState < 10000) {
        steer *= 0.5f;
    }
    if (car->steerAngle < steer) {
        car->steerAngle += steerRate * g_fDeltaTimeSec;
        if (car->steerAngle > steer) {
            car->steerAngle = steer;
        }
    } else {
        car->steerAngle -= steerRate * g_fDeltaTimeSec;
        if (car->steerAngle < steer) {
            car->steerAngle = steer;
        }
    }
    g_pSourcePlayer = NULL;
    return 1;
}

/* ---- The PC release's third handling mode (CarAInfo.driveMode 2) -------------
 * Only the PC has these (hotwheels.exe 0x412DB0, 0x412FD0, 0x413540): the
 * GameCube and PS2 CarA_PreMove have an empty case 2, so a car of mode 2 has
 * no driver there. No car in any release's data is mode 2; the port lets the
 * player choose it (Options, "Handling": CarA.driveMode, set in CarA_Init).
 * The PC's car is +0x24..+0x34 from the GameCube's (vehicleIface 0x398, rpm
 * 0x4B0, flags 0x460, ...); the fields are the same. */

/* Stopped, with button 0 pressed: the PC's rescue loading, from the BrainCore
 * game the engine came from. Inside an enabled creature volume (type 5) whose
 * data's first byte is set, flag 0x200; then 0x80 (interface condition 3) until
 * the car moves off (CarA_Mode2UnloadZone). The PC also tags the creatures
 * riding in the car's pods (type 13 slaves) with the volume and marks the
 * pods; no car in the data carries pods, so that part is left out. 0x412DB0. */
static void CarA_Mode2LoadZone(CarA *car) {
    s32 cell = VolumeManager_GetVolumeCellIndex(&car->base.pos);
    s32 n = VolumeManager_GetNumVolumesOfTypeByCell(5, cell);
    s32 i;

    car->transmissionFlags &= ~0x200;
    for (i = 0; i < n; i++) {
        WorldVolume *v = PTR(VolumeManager_GetVolumeTypeByIndexByCell(5, i, cell));
        f32 distSq = 0.0f;

        if (v->enabled == 1 && IsPointWithinVolume(&car->base.pos, v, &distSq) && *(u8 *)GETP(v->data) != 0) {
            car->transmissionFlags |= 0x200;
            break;
        }
    }
    car->transmissionFlags |= 0x80;
}

/* Moving again: 0x80 off (and the pods' 0x10, see above). 0x412FD0. */
static void CarA_Mode2UnloadZone(CarA *car) {
    car->transmissionFlags &= ~0x80;
}

static void CarA_Mode2ShiftSound(CarA *car) {
    u32 id;

    if (GETP(car->vehicleIface)->numShiftSounds != 0) {
        id = GETP(GETP(car->vehicleIface)->shiftSounds)[RandomAdd_s32(0, GETP(car->vehicleIface)->numShiftSounds)];
        if (id != 0) {
            CarAShiftSoundParams p __attribute__((aligned(16)));
            p.id = id;
            p.pos = car->base.pos;
            p.animRefPos = *(Vector4 *)&car->base.quat;
            p.vel = car->base.vel;
            SETP(p.player, NULL);
            p.speedScale = 0.0f;
            p.unusedParam = 0.0f;
            p.angVel = g_zeroVector;
            ThingManager_DARYL_CreateThing(1, &p);
        }
    }
}

/* Off the gas (or at the rev limit) on the ground: engine braking of
 * engineBrake * 255 * brakeDecel, more uphill. */
static inline void CarA_Mode2Coast(CarA *car) {
    CarAInfo *info = GETP(car->vehicleIface);
    CarAAccel *accel = GETP(info->accel);
    f32 d = -(accel->engineBrake * 255.0f * accel->brakeDecel);

    if (car->gradeOrReverse > 0.0f) {
        if (car->base.axis[2].y > 0.0f) {
            d -= car->base.axis[2].y * 160.0f;
        }
        car->gradeOrReverse += d * g_fDeltaTimeSec;
        if (car->gradeOrReverse < 0.0f) {
            car->gradeOrReverse = 0.0f;
        }
    } else {
        if (car->base.axis[2].y < 0.0f) {
            d += car->base.axis[2].y * 160.0f;
        }
        car->gradeOrReverse -= d * g_fDeltaTimeSec;
        if (car->gradeOrReverse > 0.0f) {
            car->gradeOrReverse = 0.0f;
        }
    }
    car->rpm = car->gradeOrReverse / GETP(info->gears)[car->curveTableIdx].ratio;
    if (car->rpm < info->downshiftRpm && car->curveTableIdx > 1) {
        car->curveTableIdx--;
        car->rpm = car->gradeOrReverse / GETP(info->gears)[car->curveTableIdx].ratio;
        CarA_Mode2ShiftSound(car);
    }
}

/* The PC's tail of its accel at 0x40..0x4C, past the 0x3C bytes of CarAAccel
 * (the PC reads whatever follows it in the pack): 0, 0, 1, 0 after every car's
 * on the GameCube and PC alike. Taken as these values rather than read out of
 * bounds; with them the speed-scaled ride height and grip of
 * CarA_ComputeSuspensionForces never apply and steering may fade to nothing. */
const CarAMode2Tuning g_carAMode2Tuning = { 0.0f, 0.0f, 1.0f, 0.0f };

/* The driver of a mode 2 car (CarA_PreMove, which first fills turboTime to
 * 1000: unlimited turbo). Gas runs up the gears (an impulse forward on each
 * upshift), the brake slows to a stop without reversing, button 14 reverses,
 * button 15 is a handbrake (flag 0x8000: less grip, ComputeSuspensionForces),
 * and steering follows the stick at once, by less the faster the car goes:
 * (180 - forward speed) / 150 of 45 degrees, none from 180. 0x413540. */
/* port: mode 2 reverses on action 4 (Player.button14), which none of the
 * controller layouts binds, and the PC's keyboard has no key for: a human
 * could only stop. Where it is unbound, Brake / Reverse reverses once the
 * car is at a standstill (or already reversing), as in the normal handling.
 * From the human control reader. */
void CarA_PortMode2Reverse(Player *player) {
    CarA *car = GETP(player->vehicle);

    if (car != NULL && car->base.type == THING_TYPE_CAR && car->driveMode == 2 && player->keys[4] == 0xFF &&
        player->down[3] && car->gradeOrReverse < 1.0f) {
        player->down[4] = player->down[3];
    }
}

s32 CarA_PreMove_Control_Mode2(void *thing) {
    CarA *car = thing;
    Player *pl = CARA_PLAYER(car);
    CarAInfo *info;
    u8 brake = pl->brake;
    u8 gas = pl->throttle;
    u8 back = pl->button14;
    u8 coast = 0;
    f32 scale;
    f32 stick;
    f32 steer;

    if (PSVECSquareMag(&car->base.vel) < 900.0f) {
        if (pl->buttonsHeld & 1) {
            if (car->transmissionFlags & 0x80) {
                CarA_Mode2UnloadZone(car);
            } else {
                CarA_Mode2LoadZone(car);
            }
        }
    } else if (car->transmissionFlags & 0x80) {
        CarA_Mode2UnloadZone(car);
    }

    g_pSourcePlayer = pl;
    car->transmissionFlags &= ~0x8000;
    if (pl->button15) {
        car->transmissionFlags |= 0x8000;
    }

    if (back) {
        f32 accel = -(0.5f * CarA_Transmission_GetTorqueAtWheels(car) * (f32)pl->button14);

        if (car->rpm > 0.0f) {
            car->rpm *= -1.0f;
        }
        car->rpm += accel * g_fDeltaTimeSec;
        info = GETP(car->vehicleIface);
        if (car->rpm < -info->upshiftRpm) {
            car->rpm = -info->upshiftRpm;
        }
        if (car->rpm < info->downshiftRpm && car->curveTableIdx > 1) {
            car->curveTableIdx--;
            car->rpm = car->gradeOrReverse / GETP(info->gears)[car->curveTableIdx].ratio;
            CarA_Mode2ShiftSound(car);
            car->gradeOrReverse = GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio * car->rpm;
        }
    } else if (brake) {
        f32 b = (f32)brake;

        info = GETP(car->vehicleIface);
        if (car->gradeOrReverse > 0.0f) {
            car->gradeOrReverse -= g_fDeltaTimeSec * GETP(info->accel)->brakeDecel * b;
            if (car->gradeOrReverse < 0.0f) {
                car->gradeOrReverse = 0.0f;
            }
        } else {
            car->gradeOrReverse += g_fDeltaTimeSec * GETP(info->accel)->reverseThrottleDecel * b;
            if (car->gradeOrReverse > 0.0f) {
                car->gradeOrReverse = 0.0f;
            }
        }
        car->rpm = car->gradeOrReverse / GETP(info->gears)[car->curveTableIdx].ratio;
        if (car->rpm < info->downshiftRpm && car->curveTableIdx > 1) {
            car->curveTableIdx--;
            car->rpm = car->gradeOrReverse / GETP(info->gears)[car->curveTableIdx].ratio;
            CarA_Mode2ShiftSound(car);
        }
        /* brake lights; the PC's own check, before the common one below
         * (a brake value of exactly 2 turns them off) */
        if (!(car->transmissionFlags & 0x20)) {
            car->transmissionFlags |= 0x20;
            if (GETP(car->vehicleIface)->numSlaves >= 3) {
                MotionState_InitAllSlavesNow(&car->motionState, &GETP(GETP(car->vehicleIface)->slaves)[3].list, 3);
            }
        } else if (brake == 2) {
            car->transmissionFlags &= ~0x20;
            MotionState_ReleaseSlavesByType(&car->motionState, 3);
        }
    } else {
        if (gas) {
            f32 torque = CarA_Transmission_GetTorqueAtWheels(car);   /* the full torque, where mode 1 takes half */
            f32 accel;

            info = GETP(car->vehicleIface);
            if (((car->transmissionFlags & 0x1000) && car->rpm >= info->turboRpmScale * info->maxRpm) ||
                (!(car->transmissionFlags & 0x1000) && car->rpm >= info->maxRpm)) {
                accel = 0.0f;
                coast = 1;
            } else {
                accel = (f32)pl->throttle * torque;
            }
            car->rpm += accel * g_fDeltaTimeSec;
            if (car->rpm < 0.0f) {
                car->rpm *= -1.0f;
            }
            if (car->rpm > info->upshiftRpm && car->curveTableIdx < info->numGears - 1) {
                Vector4 pos __attribute__((aligned(16)));
                Vector4 dir __attribute__((aligned(16)));
                Vector4 vel __attribute__((aligned(16)));

                car->curveTableIdx++;
                car->rpm = car->gradeOrReverse / GETP(info->gears)[car->curveTableIdx].ratio;
                /* a kick forward from behind the car, as the turbo's */
                pos.x = car->base.pos.x - 8.0f * car->base.axis[2].x - 3.0f * car->base.axis[1].x;
                pos.y = car->base.pos.y - 8.0f * car->base.axis[2].y - 3.0f * car->base.axis[1].y;
                pos.z = car->base.pos.z - 8.0f * car->base.axis[2].z - 3.0f * car->base.axis[1].z;
                pos.w = 0.0f;
                dir.x = car->base.axis[2].x - 0.2f * car->base.axis[1].x;
                dir.y = car->base.axis[2].y - 0.2f * car->base.axis[1].y;
                dir.z = car->base.axis[2].z - 0.2f * car->base.axis[1].z;
                dir.w = 0.0f;
                vel.x = -10.0f * dir.x;
                vel.y = -10.0f * dir.y;
                vel.z = -10.0f * dir.z;
                vel.w = 0.0f;
                if (PSVECSquareMag(&dir)) {
                    PSVECNormalize(&dir, &dir);
                }
                CarA_ProcessImpulse(car, &dir, &pos, &vel, 1.0f);
            }
            car->gradeOrReverse = GETP(GETP(car->vehicleIface)->gears)[car->curveTableIdx].ratio * car->rpm;
        } else {
            coast = 1;
        }
        if (coast && car->numWheelsGrounded != 0) {
            CarA_Mode2Coast(car);
        }
    }

    if (!(car->transmissionFlags & 0x20) && brake) {
        car->transmissionFlags |= 0x20;
        if (GETP(car->vehicleIface)->numSlaves >= 3) {
            MotionState_InitAllSlavesNow(&car->motionState, &GETP(GETP(car->vehicleIface)->slaves)[3].list, 3);
        }
    } else if ((car->transmissionFlags & 0x20) && !brake) {
        car->transmissionFlags &= ~0x20;
        MotionState_ReleaseSlavesByType(&car->motionState, 3);
    }

    info = GETP(car->vehicleIface);
    if (car->rpm < -info->maxRpm) {
        car->rpm = -info->maxRpm;
    }
    scale = (180.0f - PSVECDotProduct(&car->base.axis[2], &car->base.vel)) * (1.0f / 150.0f);
    stick = (f32)pl->stickX * (1.0f / 128.0f);
    if (scale < g_carAMode2Tuning.minSteerScale) {
        scale = g_carAMode2Tuning.minSteerScale;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    steer = scale * (stick * -0.7853982f);
    if (car->transmissionFlags & 0x2000) {
        if (car->lowSpeedFlag < 100) {
            steer = 0.0f;
        } else {
            car->transmissionFlags &= ~0x2000;
        }
    }
    car->steerAngle = steer;
    if (car->gradeOrReverse > GETP(info->engine)->maxGrade) {
        car->gradeOrReverse = GETP(info->engine)->maxGrade;
    }
    g_pSourcePlayer = NULL;
    return 1;
}
