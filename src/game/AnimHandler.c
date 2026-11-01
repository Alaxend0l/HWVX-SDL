/* Animation: MotionState (a model's animation layers and the slave things
 * attached to its bones) and the key-frame / skeleton helpers. */
#include "common.h"
#include "CarA.h"
#include "ThingManager.h"
#include "WildCard.h"



/* Start the state's model from its first phase that has a range, and point
 * the layer at it. reset: blend in from 0 (else play it at full weight). */
void MotionState_Init(MotionState *ms, s8 reset) {
    MotionLayer *hist = &ms->layer;
    MotionAnim *anim;
    s32 i;
    u32 *skel;

    ms->loops = 0;
    ms->reverse = 0;
    ms->mode = 0;
    ms->mode = 0;
    anim = GETP(ms->model);
    if (anim == NULL) {
        return;
    }
    if (!anim->hasIntro) {
        ms->mode++;
        if (!anim->loopMode) {
            ms->mode++;
            if (anim->hasOutro) {
                ms->time = GETP(anim->phases[2])->start;
            }
        } else {
            ms->time = GETP(anim->phases[1])->start;
        }
    } else {
        ms->time = GETP(anim->phases[0])->start;
    }
    for (;;) {
        switch (ms->mode) {
        case 0:
            if (anim->hasIntro) {
                goto done;
            }
            ms->mode = 1;
        case 1:
            if (anim->loopMode) {
                goto done;
            }
            ms->mode = 2;
        case 2:
            if (anim->hasOutro) {
                goto done;
            }
            ms->mode = 0;
        }
    }
done:
    if (reset) {
        /* sic: one layer, so nothing to shift down */
        for (i = 0; i > 0; i--) {
            memcpy(&hist[i], &hist[i - 1], sizeof(MotionLayer));
        }
        hist[0].blend = 0.0f;
    } else {
        hist[0].blend = 1.0f;
    }
    SETP(hist[0].anim, anim);
    hist[0].phase = ms->mode;
    hist[0].loops = ms->loops;
    hist[0].reverse = ms->reverse;
    hist[0].time = ms->time;
    skel = &GETP(anim->skeleton)->numBones;
    if (ms->numMatrices < *skel) {
        if (ms->numMatrices) {
            MatrixCache_Free(ms->animMatrices.h);
        }
        ms->animMatrices.h = MatrixCache_Get(*skel, ms);
        ms->numMatrices = *skel;
    }
    MotionState_ReleaseSlavesByType(ms, 0xFF);
}

/* Release the state's slaves tagged `tag` straight back to their free list. */
void MotionState_ReleaseSlavesByType(MotionState *ms, u8 tag) {
    Thing *thing;
    Thing *next;

    thing = GETP(ms->slaves);
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->slaveTag == tag) {
            THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
            ThingManagerList_Remove(&ms->slaves, thing);
            thing->flags &= ~0x200000;
            thing->flags |= 0x1000;
            ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
        }
        thing = next;
    }
}

void MotionState_Release(MotionState *ms) {
    if (GETP(ms->animMatrices) != NULL) {
        ThingManagerList_EmancipateSlaves(GETP(ms->animMatrices), &ms->slaves);
        MatrixCache_Free(ms->animMatrices.h);
        SETP(ms->animMatrices, NULL);
        ms->numMatrices = 0;
    }
    ThingManagerList_ReleaseAll(&ms->slaves);
    ThingManagerList_RemoveReleasedThings(&ms->slaves);
}

/* The model's base pose with the layer's animation blended in. */
void MotionState_ExtractBlendedKeyFrame(MotionState *ms, void *keyFrame) {
    CreatureSkeleton *skel;

    skel = GETP(GETP(ms->model)->skeleton);
    MemCpy32(keyFrame, GETP(skel->nodes), skel->numBones << 5);
    ms->layer.blend = 1.0f;
    Gfx_AnimExtractKeyFrame(GETP(GETP(ms->layer.anim)->keys), ms->layer.time, keyFrame, skel->numBones);
}

void MotionState_Draw(MotionState *ms) {
    MotionAnim *model = GETP(ms->model);
    u32 i;
    s32 *skel;

    skel = (s32 *)&GETP(model->skeleton)->numBones;
    for (i = 0; i < model->numGeoms; i++) {
        Gfx_DrawAnimGeometryAux(GETP(GETP(model->geoms)[i]), GETP(ms->animMatrices), *skel, 0);
    }
    for (i = 0; i < model->numRigid; i++) {
        MotionPart *e = &GETP(model->rigid)[i];
        Gfx_DrawGeometry_MatrixNotCached(HND(GETP(e->geom)), HND(&GETP(ms->animMatrices)[e->mtxIndex]));
    }
    MotionState_DrawSlaves(ms, 0);
}

/* Draw at the matrix handle mtx: matrices is scratch for the bone matrices,
 * unless the state's own are current (animMatrices, then drawn times mtx). */
void MotionState_Draw2(MotionState *ms, s32 mtx, u8 *matrices) {
    s32 *skel;
    u8 *cached;
    u32 i;
    MotionPart *e;
    Vector4 quat;
    Matrix4 out;
    u8 frames[0x800];

    skel = (s32 *)&GETP(GETP(ms->model)->skeleton)->numBones;
    if (GETP(ms->animMatrices) == NULL) {
        MotionState_ExtractBlendedKeyFrame(ms, frames);
        SubMatrixToQuaternion((void *)PTR(mtx), &quat);
        TEST_AnimTraverseHeirarchyToMatricesWorld(&quat, &((Matrix4 *)PTR(mtx))->v[3], frames, *skel, matrices, 1.0f);
        for (i = 0; i < GETP(ms->model)->numGeoms; i++) {
            Gfx_DrawAnimGeometryAux(GETP(GETP(GETP(ms->model)->geoms)[i]), matrices, *skel, 0);
        }
        for (i = 0; i < GETP(ms->model)->numRigid; i++) {
            e = &GETP(GETP(ms->model)->rigid)[i];
            Gfx_DrawGeometry(HND(GETP(e->geom)), HND(matrices + (e->mtxIndex << 6)));
        }
        MotionState_DrawSlaves(ms, 0);
    } else {
        cached = (u8 *)GETP(ms->animMatrices);
        for (i = 0; i < GETP(ms->model)->numGeoms; i++) {
            Gfx_DrawAnimGeometryAux(GETP(GETP(GETP(ms->model)->geoms)[i]), cached, *skel, PTR(mtx));
        }
        for (i = 0; i < GETP(ms->model)->numRigid; i++) {
            void *geom;
            e = &GETP(GETP(ms->model)->rigid)[i];
            geom = GETP(e->geom);
            MultiplyMatrix((Matrix4 *)(cached + (e->mtxIndex << 6)), (void *)PTR(mtx), &out);
            Gfx_DrawGeometry(HND(geom), HND(&out));
        }
        MotionState_DrawSlaves(ms, mtx);
    }
}

/* Fire every trigger of the list (tagging the slaves `player`), replacing
 * the slaves already tagged so. */
void MotionState_InitAllSlavesNow(MotionState *ms, SlaveList *list, u8 player) {
    extern Vector4 g_zeroVector;
    u32 i;

    MotionState_ReleaseSlavesByType(ms, player);
    for (i = 0; i < list->count; i++) {
        Vector4 dir = {0.0f, 0.0f, 0.0f, 1.0f};

        ThingManagerList_TriggerSlaves(&ms->slaves, &GETP(list->triggers)[i], &GETP(list->triggers)[i].pos, &dir, &g_zeroVector, player);
    }
}

/* Fire the triggers whose time falls in this frame, `time` ms in. */
void MotionState_InitSlavesTimed(MotionState *ms, SlaveList *list, u8 player, u32 time) {
    extern u32 g_iDeltaTimeMS;
    extern Vector4 g_zeroVector;
    u32 i;

    for (i = 0; i < list->count; i++) {
        Vector4 dir = {0.0f, 0.0f, 0.0f, 1.0f};

        if (GETP(list->triggers)[i].time >= time && GETP(list->triggers)[i].time < time + g_iDeltaTimeMS) {
            ThingManagerList_TriggerSlaves(&ms->slaves, &GETP(list->triggers)[i], &GETP(list->triggers)[i].pos, &dir, &g_zeroVector, player);
        }
    }
}

/* Fire the animation's slave triggers whose time the layer passed in its
 * last step (between prevTime and rawTime, either way). The count is the
 * layer model's, the triggers `events`'. */
void MotionState_BlendTriggerEvents(MotionState *ms, SlaveList *events, Vector4 *vel) {
    SlaveList *list = events;
    Vector4 v __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    f32 start;
    f32 end;
    u32 i;

    if (vel != NULL) {
        v = *vel;
    }
    start = ms->layer.prevTime;
    end = ms->layer.rawTime;
    for (i = 0; i < GETP(ms->layer.anim)->events.count; i++) {
        SlaveTrigger *trigger = &GETP(list->triggers)[i];
        f32 time = trigger->time;
        s8 hit = 0;

        if (ms->layer.reverse) {
            if (start > time && end <= time) {
                hit = 1;
            }
        } else {
            if (start <= time && end > time) {
                hit = 1;
            }
        }
        if (hit) {
            Vector4 dir __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 1.0f};
            ThingManagerList_TriggerSlaves(&ms->slaves, trigger, &trigger->pos, &dir, &v, 0xFF);
        }
    }
}

/* Advance a layer by fps * dt through its animation's phases: wrap or
 * ping-pong in the loop phase (up to maxLoops), and move on to the next
 * phase when one ends, as `mode` allows (> 0: into the loop, > 1: into the
 * outro). */
void Motion_BlendHeartbeat_FPS(MotionLayer *l, s32 mode, MotionAnim *anim, f32 fps) {
    extern f32 g_fDeltaTimeSec;

    f32 time;
    f32 prev;
    f32 raw;
    f32 start;
    f32 end;
    f32 over;
    f32 step;
    s8 reverse;
    u8 oldPhase;
    s8 type;
    u8 wrapped;

    l->done = 0;
    raw = 0.0f;
    type = 0;
    type |= 1;
    start = raw;
    end = raw;
    time = l->time;
    prev = time;
    reverse = l->reverse;
    over = -1.0f;
    wrapped = 0;
    switch (l->phase) {
    case 0:
        start = GETP(GETP(l->anim)->phases[0])->start;
        end = GETP(GETP(l->anim)->phases[0])->end;
        break;
    case 1:
        start = GETP(GETP(l->anim)->phases[1])->start;
        end = GETP(GETP(l->anim)->phases[1])->end;
        switch (GETP(l->anim)->loopMode) {
        case 0:
            break;
        case 1:
            if (GETP(l->anim)->maxLoops == 0 || l->loops < GETP(l->anim)->maxLoops) {
                type |= 2;
            }
            break;
        case 2:
            type |= 4;
            break;
        }
        break;
    case 2:
        start = GETP(GETP(l->anim)->phases[2])->start;
        end = GETP(GETP(l->anim)->phases[2])->end;
        break;
    }
    step = fps * g_fDeltaTimeSec;
    if (reverse) {
        switch (type) {
        case 2:
        case 3:
            break;
        case 4:
            raw = time += step;
            if (time > end) {
                over = time - end;
                reverse = 0;
                time = end;
                wrapped = 1;
            }
            break;
        case 5:
            raw = time -= step;
            if (time < start) {
                over = start - time;
                reverse = 0;
                time = start;
                wrapped = 1;
            }
            break;
        case 6:
            raw = time += step;
            if (time > end) {
                over = time - end;
                reverse = 0;
                wrapped = 1;
                time = 2.0f * end - time;
            }
            break;
        case 7:
            raw = time -= step;
            if (time < start) {
                over = start - time;
                reverse = 0;
                wrapped = 1;
                time = 2.0f * start - time;
            }
            break;
        }
    } else {
        switch (type) {
        case 0:
            raw = time -= step;
            if (time < start) {
                over = start - time;
                wrapped = 1;
                time = start;
            }
            break;
        case 1:
            raw = time += step;
            if (time > end) {
                over = time - end;
                wrapped = 1;
                time = end;
            }
            break;
        case 2:
            raw = time -= step;
            if (time < start) {
                over = start - time;
                wrapped = 1;
                time = end;
            }
            break;
        case 3:
            raw = time += step;
            if (time > end) {
                over = time - end;
                wrapped = 1;
                time = start;
            }
            break;
        case 4:
            raw = time -= step;
            if (time < start) {
                over = start - time;
                reverse = 1;
                time = 2.0f * start - time;
            }
            break;
        case 5:
            raw = time += step;
            if (time > end) {
                over = time - end;
                reverse = 1;
                time = 2.0f * end - time;
            }
            break;
        case 6:
            raw = time -= step;
            if (time < start) {
                over = start - time;
                reverse = 1;
                time = 2.0f * start - time;
            }
            break;
        case 7:
            raw = time += step;
            if (time > end) {
                over = time - end;
                reverse = 1;
                time = 2.0f * end - time;
            }
            break;
        }
    }
    l->prevTime = prev;
    l->rawTime = raw;
    oldPhase = l->phase;
    l->time = time;
    l->reverse = reverse;
    switch (l->phase) {
    case 0:
        if (wrapped) {
            if (GETP(l->anim)->loopMode & ((u8)mode != 0)) {
                l->phase = 1;
            } else if (GETP(l->anim)->hasOutro & ((u8)mode > 1)) {
                l->phase = 2;
            }
        }
        break;
    case 1:
        l->loops += wrapped;
        if (GETP(l->anim)->maxLoops != 0 && l->loops >= GETP(l->anim)->maxLoops) {
            l->loops = GETP(l->anim)->maxLoops;
            if (GETP(l->anim)->hasOutro & ((u8)mode > 1)) {
                l->phase = 2;
            } else {
                l->time = GETP(GETP(l->anim)->phases[1])->end;
                l->done = 1;
            }
        }
        break;
    case 2:
        if (wrapped) {
            l->done = 1;
        }
        break;
    }
    if (l->phase != oldPhase) {
        switch (l->phase) {
        case 0:
            l->time = GETP(GETP(l->anim)->phases[0])->start + over;
            break;
        case 1:
            l->time = GETP(GETP(l->anim)->phases[1])->start + over;
            break;
        case 2:
            l->time = GETP(GETP(l->anim)->phases[2])->start + over;
            break;
        }
    }
}

typedef void (*SlaveTransformFn)(Thing *thing, Matrix4 *mtx);
typedef void (*ThingFn)(Thing *thing);

/* Move the slaves attached to the state's bones: each is put at its bone
 * (times `matrix`) with the master's velocity for its own Move, then
 * restored (slaves keep bone-relative transforms). */
void MotionState_MoveSlaves(void *state, Matrix4 *matrix, Vector4 *vel) {
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    MotionState *ms = state;
    Vector4 oldVel;
    Vector4 quat;
    Vector4 v = g_zeroVector;
    Matrix4 saved;
    Matrix4 out;
    Matrix4 *mtx = NULL;
    Thing *slave;
    Matrix4 *matrices;
    Thing *next;

    matrices = GETP(ms->animMatrices);
    if (vel != NULL) {
        v = *vel;
    }
    if (matrix != &g_identity) {
        mtx = matrix;
    }
    slave = GETP(ms->slaves);
    while (slave != NULL) {
        next = GETP(slave->next);
        if (!(slave->flags & 0x201000)) {
            if (!(slave->flags & 0x400000)) {
                CopyVector(&quat, &slave->quat);
                CopyMatrix(&saved, THING_MTX(slave));
                if (mtx != NULL) {
                    MultiplyMatrix(&matrices[slave->slaveBone], mtx, &out);
                    THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &out);
                } else {
                    THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &matrices[slave->slaveBone]);
                }
                oldVel = slave->vel;
                slave->vel = v;
                THING_VFN(slave, THING_V_MOVE, ThingFn)(slave);
                Thing_MoveSlaves(slave);
                CopyVector(&slave->quat, &quat);
                CopyMatrix(THING_MTX(slave), &saved);
                CopyVector(&slave->vel, &oldVel);
            } else {
                THING_VFN(slave, THING_V_MOVE, ThingFn)(slave);
            }
        }
        slave = next;
    }
    ThingManagerList_RemoveReleasedThings(&ms->slaves);
}

/* Draw the slaves at their bones (times the matrix handle arg1, unless it
 * is the identity), restoring their transforms afterwards. */
void MotionState_DrawSlaves(void *state, s32 arg1) {
    extern Matrix4 g_identity;
    MotionState *ms = state;
    Matrix4 *matrix = (Matrix4 *)PTR(arg1);
    Thing *slave;
    Vector4 quat __attribute__((aligned(16)));
    Matrix4 mtx __attribute__((aligned(16)));
    Matrix4 out __attribute__((aligned(16)));

    slave = GETP(ms->slaves);
    if (matrix != NULL && matrix != &g_identity) {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if (slave->flags & 0x200200) {
                continue;
            }
            CopyVector(&quat, &slave->quat);
            CopyMatrix(&mtx, THING_MTX(slave));
            MultiplyMatrix(&GETP(ms->animMatrices)[slave->slaveBone], matrix, &out);
            THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &out);
            THING_VFN(slave, THING_V_DRAW, ThingFn)(slave);
            Thing_DrawSlaves(slave);
            CopyVector(&slave->quat, &quat);
            CopyMatrix(THING_MTX(slave), &mtx);
        }
    } else {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if (slave->flags & 0x200200) {
                continue;
            }
            CopyVector(&quat, &slave->quat);
            CopyMatrix(&mtx, THING_MTX(slave));
            THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &GETP(ms->animMatrices)[slave->slaveBone]);
            THING_VFN(slave, THING_V_DRAW, ThingFn)(slave);
            Thing_DrawSlaves(slave);
            CopyVector(&slave->quat, &quat);
            CopyMatrix(THING_MTX(slave), &mtx);
        }
    }
}

/* As MotionState_DrawSlaves, for the attached coronas (AttachThing) only. */
void MotionState_DrawSlavesAttachCoronaOnly(void *state, s32 arg1) {
    extern Matrix4 g_identity;
    extern u8 g_attachVTable[];
    MotionState *ms = state;
    Matrix4 *matrix = (Matrix4 *)PTR(arg1);
    Thing *slave;
    Vector4 quat __attribute__((aligned(16)));
    Matrix4 mtx __attribute__((aligned(16)));
    Matrix4 out __attribute__((aligned(16)));

    slave = GETP(ms->slaves);
    if (matrix != NULL && matrix != &g_identity) {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if ((slave->flags & 0x200200) || (void *)GETP(slave->vtable) != (void *)g_attachVTable) {
                continue;
            }
            CopyVector(&quat, &slave->quat);
            CopyMatrix(&mtx, THING_MTX(slave));
            MultiplyMatrix(&GETP(ms->animMatrices)[slave->slaveBone], matrix, &out);
            THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &out);
            THING_VFN(slave, THING_V_DRAW, ThingFn)(slave);
            Thing_DrawSlaves(slave);
            CopyVector(&slave->quat, &quat);
            CopyMatrix(THING_MTX(slave), &mtx);
        }
    } else {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if ((slave->flags & 0x200200) || (void *)GETP(slave->vtable) != (void *)g_attachVTable) {
                continue;
            }
            CopyVector(&quat, &slave->quat);
            CopyMatrix(&mtx, THING_MTX(slave));
            THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &GETP(ms->animMatrices)[slave->slaveBone]);
            THING_VFN(slave, THING_V_DRAW, ThingFn)(slave);
            Thing_DrawSlaves(slave);
            CopyVector(&slave->quat, &quat);
            CopyMatrix(THING_MTX(slave), &mtx);
        }
    }
}


void MotionState_BuildMatricesWorld(void *arg0, void *arg1, MotionState *ms, f32 time) {
    u8 frames[0x800];
    s32 *skel;

    if (GETP(ms->animMatrices) != NULL) {
        skel = (s32 *)&GETP(GETP(ms->model)->skeleton)->numBones;
        MotionState_ExtractBlendedKeyFrame(ms, frames);
        TEST_AnimTraverseHeirarchyToMatricesWorld(arg0, arg1, frames, *skel, GETP(ms->animMatrices), time);
    }
}

void Motion_HeartbeatB2(MotionState *ms, Vector4 *vel) {
    Matrix4 *matrices;
    Thing *slave;

    MotionState_BlendTriggerEvents(ms, &GETP(ms->model)->events, vel);
    matrices = GETP(ms->animMatrices);
    {
        Vector4 v __attribute__((aligned(16))) = {0};
        Vector4 quat __attribute__((aligned(16)));
        Vector4 oldVel __attribute__((aligned(16)));
        Matrix4 mtx __attribute__((aligned(16)));

        if (vel != NULL) {
            v = *vel;
        }
        for (slave = GETP(ms->slaves); slave != NULL; slave = GETP(slave->next)) {
            if (slave->flags & 0x201000) {
                continue;
            }
            if (!(slave->flags & 0x400000)) {
                CopyVector(&quat, &slave->quat);
                CopyMatrix(&mtx, THING_MTX(slave));
                THING_VFN(slave, THING_V_SLAVE_TRANSFORM, SlaveTransformFn)(slave, &matrices[slave->slaveBone]);
                oldVel = slave->vel;
                slave->vel = v;
                THING_VFN(slave, THING_V_MOVE, ThingFn)(slave);
                Thing_MoveSlaves(slave);
                CopyVector(&slave->quat, &quat);
                CopyMatrix(THING_MTX(slave), &mtx);
                CopyVector(&slave->vel, &oldVel);
            } else {
                THING_VFN(slave, THING_V_MOVE, ThingFn)(slave);
            }
        }
    }
    ThingManagerList_RemoveReleasedThings(&ms->slaves);
}

/* Blend the layer in over a second and advance it, then fire its events and
 * move the slaves (if a matrix is given). */
void MotionState_Heartbeat(MotionState *ms, void *arg1, void *thing) {
    extern f32 g_fDeltaTimeSec;
    MotionLayer *layers = &ms->layer;
    f32 rate = 0.0f;
    u32 i;

    layers[0].blend += g_fDeltaTimeSec;
    if (layers[0].blend > 1.0f) {
        layers[0].blend = 1.0f;
    }
    Motion_BlendHeartbeat_FPS(&layers[0], 2, GETP(layers[0].anim), GETP(layers[0].anim)->fps);
    /* sic: one layer, so older ones never fade out here */
    i = 1;
    while (i < 1) {
        rate -= 1.0f;
        layers[i].blend += rate * g_fDeltaTimeSec;
        if (layers[i].blend <= 0.0f) {
            layers[i].blend = 0.0f;
        } else {
            Motion_BlendHeartbeat_FPS(&layers[i], 2, GETP(layers[i].anim), GETP(layers[i].anim)->fps);
        }
        i++;
    }
    if (arg1 != NULL) {
        MotionState_BlendTriggerEvents(ms, &GETP(ms->model)->events, thing);
        MotionState_MoveSlaves(ms, arg1, thing);
    }
}

/* MotionState_Heartbeat for a wildcard slot: holds the loop while there is
 * too little ammo for a shot, and turns the aim bone by the slot's angle. */
void WildcardState_Heartbeat(WildCardSlot *slot, u8 *mtx, void *thing) {
    extern f32 g_fDeltaTimeSec;
    MotionState *ms = &slot->motion;
    MotionLayer *layers = &ms->layer;
    WeaponInfo *weapon;
    f32 rate = 0.0f;
    u32 i;
    s32 *skel;
    f32 angle;
    Quaternion quat;
    Quaternion rot;
    Quaternion out;
    u8 frames[0x800];

    layers[0].blend += g_fDeltaTimeSec;
    if (layers[0].blend > 1.0f) {
        layers[0].blend = 1.0f;
    }
    Motion_BlendHeartbeat_FPS(&layers[0], 2, GETP(layers[0].anim), GETP(layers[0].anim)->fps);
    i = 1;
    while (i < 1) {
        rate -= 1.0f;
        layers[i].blend += rate * g_fDeltaTimeSec;
        if (layers[i].blend <= 0.0f) {
            layers[i].blend = 0.0f;
        } else {
            Motion_BlendHeartbeat_FPS(&layers[i], 2, GETP(layers[i].anim), GETP(layers[i].anim)->fps);
        }
        i++;
    }
    weapon = GETP(GETP(slot->item)->weapon);
    if (layers[0].phase == 2 && slot->ammo < weapon->ammoPerShot) {
        layers[0].time = GETP(GETP(layers[0].anim)->phases[2])->end;
    }
    if (GETP(ms->animMatrices) != NULL) {
        skel = (s32 *)&GETP(GETP(layers[0].anim)->skeleton)->numBones;
        MotionState_ExtractBlendedKeyFrame(ms, frames);
        if (GETP(GETP(slot->item)->weapon)->aimMode == 1 || GETP(GETP(slot->item)->weapon)->aimMode == 2) {
            angle = 0.5f * slot->angle * slot->scale;
            rot.w = cos(angle);
            rot.x = sin(angle);
            rot.y = 0.0f;
            rot.z = 0.0f;
            QuaternionMult((Vector4 *)&rot, (Vector4 *)(frames + GETP(GETP(slot->item)->weapon)->aimBone * 32), (Vector4 *)&out);
            *(Quaternion *)(frames + GETP(GETP(slot->item)->weapon)->aimBone * 32) = out;
        }
        SubMatrixToQuaternion(mtx, (Vector4 *)&quat);
        TEST_AnimTraverseHeirarchyToMatricesWorld((Vector4 *)&quat, &((Matrix4 *)mtx)->v[3], frames, *skel, GETP(ms->animMatrices), 1.0f);
    }
    MotionState_BlendTriggerEvents(ms, &GETP(ms->model)->events, thing);
    MotionState_MoveSlaves(ms, NULL, thing);
}

void TEST_AnimTraverseHeirarchyToMatricesWorld(Vector4 *quat, void *pos0, void *frames, s32 skel, void *matrices, f32 time) {
    typedef struct AnimBone {
        Quaternion rot;
        f32 pos[3];
        u8 numChildren;
        u8 pop;
        u8 next;
        u8 pad[1];
    } AnimBone;
    Vector4 posStack[64];
    Quaternion rotStack[64];
    Matrix4 m;
    Quaternion rot;
    Quaternion q;
    Vector4 pos;
    Vector4 *mtx;
    u32 i;
    Vector4 *posSp;
    Quaternion *rotSp;
    AnimBone *bone;
    u32 n;
    Vector4 *p;

    p = pos0;
    pos.w = 1.0f;
    bone = frames;
    mtx = matrices;
    posSp = posStack;
    rotSp = rotStack;
    rot = *(Quaternion *)quat;
    QuaternionToSubMatrix(quat, &m);
    m.v[3] = *p;
    XformVec(&m, (Vector4 *)((AnimBone *)frames)->pos, &pos);
    for (i = 0; i < skel; i++) {
        QuaternionMult((Vector4 *)&rot, (Vector4 *)&bone->rot, (Vector4 *)&q);
        rot = q;
        QuaternionToSubMatrix(&q, mtx);
        mtx[3] = pos;
        mtx[3].w = 1.0f;
        if (bone->numChildren != 0) {
            frames = &bone[1];
            n = bone->numChildren - 1;
            XformVec(mtx, (Vector4 *)((AnimBone *)frames)->pos, &pos);
            if (n != 0) {
                u32 j;

                posSp += n;
                p = posSp;
                frames = &((AnimBone *)frames)[((AnimBone *)frames)->next];
                *rotSp++ = q;
                for (j = 0; j < n; j++) {
                    XformVec(mtx, (Vector4 *)((AnimBone *)frames)->pos, --p);
                    frames = &((AnimBone *)frames)[((AnimBone *)frames)->next];
                }
            }
        } else if (i + 1 < skel) {
            /* port: the original also pops after the last bone (a leaf with
             * no sibling left), reading below posStack/rotStack; the values
             * are never used, so that pop is skipped */
            pos = *--posSp;
            rotSp -= bone->pop + 1;
            rot = *rotSp++;
        }
        bone++;
        mtx += 4;
    }
}



/* Mark a bone and its subtree as just blown off; returns how many. */
s32 AnimNodes_BlowBranch(AnimNode *node) {
    s32 count = 1;
    u32 i;
    AnimNode *child = node + 1;

    if (node->flags & 3) {
        return 0;
    }
    node->flags |= 2;
    for (i = 0; i < node->numChildren; i++) {
        count += AnimNodes_BlowBranch(child);
        child += child->next;
    }
    return count;
}


