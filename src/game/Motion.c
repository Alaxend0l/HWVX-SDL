#include "common.h"


s32 Motion_Init(u8 *motion, u8 *init) {
    u8 *state = motion + 0xB0;

    Thing_Init(motion, (s32)HND(init));
    M2C_FIELD(motion, Vector4 *, 0x40) = M2C_FIELD(init, Vector4 *, 0x10);
    M2C_FIELD(motion, Vector4 *, 0x50) = M2C_FIELD(init, Vector4 *, 0x20);
    QuaternionToSubMatrix(motion + 0x50, motion + 0x10);
    M2C_FIELD(state, s32 *, 0x0) = M2C_FIELD(init, s32 *, 0x30);
    M2C_FIELD(state, f32 *, 0x24) = 0.0f;
    MotionState_Init(state, 0);
    MotionState_BuildMatricesWorld(motion + 0x50, motion + 0x40, state, 1.0f);
    if (SLOT_LOAD(void *, (u8 *)(init) + 0x34) != NULL) {
        MotionState_InitAllSlavesNow(HND(state), SLOT_LOAD(void *, (u8 *)(init) + 0x34), 0xFF);
    }
    return 1;
}

/* port: takes a pointer, as the vtable calls it; the decomp has s32 */
void Motion_Release(void *arg0) {
    MotionState_Release((s32)HND((u8 *)arg0 + 0xB0));
    Thing_Release(arg0);
}

s32 Motion_Move(u8 *motion) {
    MotionState_BuildMatricesWorld(motion + 0x50, motion + 0x40, motion + 0xB0, 1.0f);
    MotionState_Heartbeat(motion + 0xB0, motion + 0x10, motion);
    return 1;
}

void Motion_Draw(u8 *motion) {
    extern u8 g_aMatStack[4096];
    if (M2C_FIELD(motion, u32 *, 0xC0) != 0) {
        MotionState_Draw(motion + 0xB0);
    } else {
        MotionState_Draw2(motion + 0xB0, HND(motion + 0x10), g_aMatStack);
    }
}

s32 Motion_Invalidate(void *arg0, u32 arg1, u32 arg2) {
    u32 temp_r0;

    temp_r0 = M2C_FIELD(arg0, u32 *, 0xB0);
    if ((temp_r0 >= arg1) && (temp_r0 < arg2)) {
        return 1;
    }
    return 0;
}
u8 g_aMatStack[4096];


/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_motionVTable[17];

__attribute__((constructor)) static void PortInit_g_motionVTable(void) {
    static void *const fns[] = {
        (void *)Motion_Init,
        (void *)Motion_Release,
        (void *)Thing_PreMove,
        (void *)Motion_Move,
        (void *)Motion_Draw,
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
        g_motionVTable[i] = HND(fns[i]);
    }
}
