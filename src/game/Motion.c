#include "common.h"
#include "CarA.h"
#include "ThingManager.h"


/* Motion things (THING_TYPE_MOTION): an animated model at a place,
 * e.g. scenery a slave trigger starts. */
s32 Motion_Init(MotionThing *motion, MotionParams *init) {
    MotionState *state = &motion->state;

    Thing_Init(&motion->base, (s32)HND(init));
    motion->base.pos = init->pos;
    motion->base.quat = init->quat;
    QuaternionToSubMatrix(&motion->base.quat, THING_MTX(&motion->base));
    SETP(state->model, GETP(init->model));
    state->layer.blend = 0.0f;
    MotionState_Init(state, 0);
    MotionState_BuildMatricesWorld(&motion->base.quat, &motion->base.pos, state, 1.0f);
    if (GETP(init->slaves) != NULL) {
        MotionState_InitAllSlavesNow(state, GETP(init->slaves), 0xFF);
    }
    return 1;
}

/* port: takes a pointer, as the vtable calls it; the decomp has s32 */
void Motion_Release(MotionThing *motion) {
    MotionState_Release(&motion->state);
    Thing_Release(&motion->base);
}

s32 Motion_Move(MotionThing *motion) {
    MotionState_BuildMatricesWorld(&motion->base.quat, &motion->base.pos, &motion->state, 1.0f);
    MotionState_Heartbeat(&motion->state, THING_MTX(&motion->base), motion);
    return 1;
}

void Motion_Draw(MotionThing *motion) {
    extern u8 g_aMatStack[4096];
    if (GETP(motion->state.animMatrices) != NULL) {
        MotionState_Draw(&motion->state);
    } else {
        MotionState_Draw2(&motion->state, HND(THING_MTX(&motion->base)), g_aMatStack);
    }
}

/* The data pack in [start, end) is going away: is the model in it? */
s32 Motion_Invalidate(MotionThing *motion, u32 start, u32 end) {
    u32 model;

    model = motion->state.model.h;
    if ((model >= start) && (model < end)) {
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
