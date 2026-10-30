#include "common.h"



void MotionState_Init(void *arg0, s8 reset) {
    typedef struct {
        u8 pad0[0x14];
        PTR32(u32) skel;
        u8 pad18[4];
        u8 loop[3];
        u8 pad1F;
        PTR32(f32) time[3];
    } Anim;
    typedef struct {
        PTR32(Anim) anim;
        u8 mode;
        u8 unk5;
        u8 unk6;
        u8 unk7;
        u8 pad8[4];
        f32 time;
    } MotionState;
    typedef struct {
        PTR32(Anim) anim;
        u8 mode;
        u8 unk5;
        u8 unk6;
        u8 pad7;
        f32 time;
        f32 blend;
        u8 pad10[0x10];
    } MotionHistory;
    typedef struct {
        MotionState cur;
        u32 matrices;
        u8 pad14[4];
        MotionHistory hist[1];
    } Motion;
    Motion *motion = arg0;
    s32 i;
    u32 *skel;

    motion->cur.unk5 = 0;
    motion->cur.unk6 = 0;
    motion->cur.mode = 0;
    motion->cur.mode = 0;
    if (GETP(motion->cur.anim) == NULL) {
        return;
    }
    if (!GETP(motion->cur.anim)->loop[0]) {
        motion->cur.mode++;
        if (!GETP(motion->cur.anim)->loop[1]) {
            motion->cur.mode++;
            if (GETP(motion->cur.anim)->loop[2]) {
                motion->cur.time = *GETP(GETP(motion->cur.anim)->time[2]);
            }
        } else {
            motion->cur.time = *GETP(GETP(motion->cur.anim)->time[1]);
        }
    } else {
        motion->cur.time = *GETP(GETP(motion->cur.anim)->time[0]);
    }
    for (;;) {
        switch (motion->cur.mode) {
        case 0:
            if (GETP(motion->cur.anim)->loop[0]) {
                goto done;
            }
            motion->cur.mode = 1;
        case 1:
            if (GETP(motion->cur.anim)->loop[1]) {
                goto done;
            }
            motion->cur.mode = 2;
        case 2:
            if (GETP(motion->cur.anim)->loop[2]) {
                goto done;
            }
            motion->cur.mode = 0;
        }
    }
done:
    if (reset) {
        for (i = 0; i > 0; i--) {
            memcpy(&motion->hist[i], &motion->hist[i - 1], sizeof(MotionHistory));
        }
        motion->hist[0].blend = 0.0f;
    } else {
        motion->hist[0].blend = 1.0f;
    }
    SETP(motion->hist[0].anim, GETP(motion->cur.anim));
    motion->hist[0].mode = motion->cur.mode;
    motion->hist[0].unk5 = motion->cur.unk5;
    motion->hist[0].unk6 = motion->cur.unk6;
    motion->hist[0].time = motion->cur.time;
    skel = GETP(GETP(motion->cur.anim)->skel);
    if (motion->cur.unk7 < *skel) {
        if (motion->cur.unk7) {
            MatrixCache_Free(motion->matrices);
        }
        motion->matrices = MatrixCache_Get(*skel, motion);
        motion->cur.unk7 = *skel;
    }
    MotionState_ReleaseSlavesByType((s32)HND(motion), 0xFF);
}

void MotionState_ReleaseSlavesByType(s32 state, u8 type) {
    typedef struct SlaveThing {
        u8 pad0[0x74];
        u32 flags;
        u8 pad78[0x83 - 0x78];
        u8 type;
        u8 pad84[0x8C - 0x84];
        void (**vtbl)(struct SlaveThing *);
        u8 pad90[0xA4 - 0x90];
        PTR32(struct SlaveThing) next;
        s32 pool;
    } SlaveThing;
    SlaveThing *thing;
    SlaveThing *next;

    thing = SLOT_LOAD(SlaveThing *, (state + 0x14));
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->type == type) {
            thing->vtbl[1](thing);
            ThingManagerList_Remove((void **)PTR((state + 0x14)), thing);
            thing->flags &= ~0x200000;
            thing->flags |= 0x1000;
            ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->pool], thing);
        }
        thing = next;
    }
}

void MotionState_Release(s32 state) {
    if (M2C_FIELD(state, u32 *, 0x10) != 0) {
        ThingManagerList_EmancipateSlaves(PTR(M2C_FIELD(state, u32 *, 0x10)), PTR(state + 0x14));
        MatrixCache_Free(M2C_FIELD(state, u32 *, 0x10));
        M2C_FIELD(state, u32 *, 0x10) = 0;
        M2C_FIELD(state, u8 *, 0x7) = 0;
    }
    ThingManagerList_ReleaseAll(PTR(state + 0x14));
    ThingManagerList_RemoveReleasedThings(PTR(state + 0x14));
}

void MotionState_ExtractBlendedKeyFrame(u8 *state, void *keyFrame) {
    u8 *anim;

    anim = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(state) + 0x0)) + 0x14);
    MemCpy32(keyFrame, SLOT_LOAD(void *, (u8 *)(anim) + 0x10), M2C_FIELD(anim, u32 *, 0x0) << 5);
    M2C_FIELD(state, f32 *, 0x24) = 1.0f;
    Gfx_AnimExtractKeyFrame(SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(state) + 0x18)) + 0x10),
                            M2C_FIELD(state, f32 *, 0x20), keyFrame, M2C_FIELD(anim, u32 *, 0x0));
}

void MotionState_Draw(void *state) {
    void Gfx_DrawAnimGeometryAux(void *geom, void *mtx, s32 skel, s32 arg3);
    u32 i;
    s32 *skel;

    skel = SLOT_LOAD(s32 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(state) + 0x0)) + 0x14);
    for (i = 0; i < M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(state) + 0x0), u32 *, 0x0); i++) {
        Gfx_DrawAnimGeometryAux(M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(state) + 0x0), void ***, 0x4)[i],
                                SLOT_LOAD(void *, (u8 *)(state) + 0x10), *skel, 0);
    }
    for (i = 0; i < M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(state) + 0x0), u32 *, 0x8); i++) {
        u8 *e = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, (u8 *)(state) + 0x0)) + 0xC) + i * 8;
        Gfx_DrawGeometry_MatrixNotCached(HND(SLOT_LOAD(void *, (u8 *)(e) + 0x0)),
                                         HND(SLOT_LOAD(u8 *, (u8 *)(state) + 0x10) + (M2C_FIELD(e, u16 *, 0x4) << 6)));
    }
    MotionState_DrawSlaves(state, 0);
}

void MotionState_Draw2(void *state, s32 mtx, u8 *matrices) {
    typedef struct GeomEntry {
        PTR32(void) geom;
        u16 matrix;
        u16 pad6;
    } GeomEntry;
    typedef struct AnimData {
        u32 numAux;
        PTR32(void *) aux;
        u32 numGeom;
        PTR32(GeomEntry) geom;
        u8 pad10[4];
        PTR32(s32) skel;
    } AnimData;
    typedef struct DrawState {
        PTR32(AnimData) anim;
        u8 pad4[0xC];
        PTR32(u8) cached;
    } DrawState;
    void Gfx_DrawAnimGeometryAux(void *geom, void *mtx, s32 skel, s32 arg3);
    DrawState *ds = state;
    s32 *skel;
    u8 *cached;
    u32 i;
    GeomEntry *e;
    Vector4 quat;
    Matrix4 out;
    u8 frames[0x800];

    skel = GETP(GETP(ds->anim)->skel);
    if (GETP(ds->cached) == NULL) {
        MotionState_ExtractBlendedKeyFrame(state, frames);
        SubMatrixToQuaternion((void *)PTR(mtx), &quat);
        TEST_AnimTraverseHeirarchyToMatricesWorld(&quat, (void *)PTR((mtx + 0x30)), frames, *skel, matrices, 1.0f);
        for (i = 0; i < GETP(ds->anim)->numAux; i++) {
            Gfx_DrawAnimGeometryAux(GETP(GETP(ds->anim)->aux)[i], matrices, *skel, 0);
        }
        for (i = 0; i < GETP(ds->anim)->numGeom; i++) {
            e = &GETP(GETP(ds->anim)->geom)[i];
            Gfx_DrawGeometry(HND(GETP(e->geom)), HND(matrices + (e->matrix << 6)));
        }
        MotionState_DrawSlaves(state, 0);
    } else {
        cached = GETP(ds->cached);
        for (i = 0; i < GETP(ds->anim)->numAux; i++) {
            Gfx_DrawAnimGeometryAux(GETP(GETP(ds->anim)->aux)[i], cached, *skel, mtx);
        }
        for (i = 0; i < GETP(ds->anim)->numGeom; i++) {
            void *geom;
            e = &GETP(GETP(ds->anim)->geom)[i];
            geom = GETP(e->geom);
            MultiplyMatrix((Matrix4 *)(cached + (e->matrix << 6)), (void *)PTR(mtx), &out);
            Gfx_DrawGeometry(HND(geom), HND(&out));
        }
        MotionState_DrawSlaves(state, mtx);
    }
}

void MotionState_InitAllSlavesNow(s32 state, void *slaves, u8 player) {
    typedef struct SlaveTrigger {
        u8 pad0[0x20];
    } SlaveTrigger;
    typedef struct SlaveList {
        u32 count;
        PTR32(SlaveTrigger) triggers;
    } SlaveList;
    extern Vector4 g_zeroVector;
    void ThingManagerList_TriggerSlaves(void **list, u8 *trigger, Vector4 *a, Vector4 *b, Vector4 *c, u8 player);
    SlaveList *list = slaves;
    u32 i;

    MotionState_ReleaseSlavesByType(state, player);
    for (i = 0; i < list->count; i++) {
        Vector4 dir = {0.0f, 0.0f, 0.0f, 1.0f};

        ThingManagerList_TriggerSlaves((void **)PTR((state + 0x14)), (u8 *)&GETP(list->triggers)[i], (Vector4 *)&GETP(list->triggers)[i], &dir, &g_zeroVector, player);
    }
}

void Motion_BlendHeartbeat_FPS(void *layer, s32 mode, void *anim, f32 fps) {
    extern f32 g_fDeltaTimeSec;
    typedef struct {
        f32 start;
        f32 end;
    } MotionRange;
    typedef struct {
        u8 unk0[0x1D];
        u8 loopMode;
        u8 pingPong;
        u8 maxLoops;
        PTR32(MotionRange) intro;
        PTR32(MotionRange) loop;
        PTR32(MotionRange) outro;
    } MotionAnim;
    typedef struct {
        PTR32(MotionAnim) anim;
        u8 phase;
        u8 loops;
        s8 reverse;
        f32 time;
        f32 weight;
        f32 prevTime;
        f32 rawTime;
        s32 done;
    } MotionLayer;
    MotionLayer *l = layer;
    f32 over;
    f32 step;
    f32 end;
    f32 start;
    f32 raw;
    f32 prev;
    f32 time;
    u8 wrapped;
    u8 oldPhase;
    s8 type;
    s8 reverse;

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
        start = GETP(GETP(l->anim)->intro)->start;
        end = GETP(GETP(l->anim)->intro)->end;
        break;
    case 1:
        start = GETP(GETP(l->anim)->loop)->start;
        end = GETP(GETP(l->anim)->loop)->end;
        switch (GETP(l->anim)->loopMode) {
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
        start = GETP(GETP(l->anim)->outro)->start;
        end = GETP(GETP(l->anim)->outro)->end;
        break;
    }
    step = fps * g_fDeltaTimeSec;
    if (reverse) {
        switch (type) {
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
        if (type == 0) {
            raw = time -= step;
            if (time < start) {
                over = start - time;
                wrapped = 1;
                time = start;
            }
        } else if (type == 1) {
            raw = time += step;
            if (time > end) {
                over = time - end;
                wrapped = 1;
                time = end;
            }
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
            } else if (GETP(l->anim)->pingPong & ((u8)mode > 1)) {
                l->phase = 2;
            }
        }
        break;
    case 1:
        l->loops += wrapped;
        if (GETP(l->anim)->maxLoops != 0 && l->loops >= GETP(l->anim)->maxLoops) {
            l->loops = GETP(l->anim)->maxLoops;
            if (GETP(l->anim)->pingPong & ((u8)mode > 1)) {
                l->phase = 2;
            } else {
                l->time = GETP(GETP(l->anim)->loop)->end;
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
            l->time = GETP(GETP(l->anim)->intro)->start + over;
            break;
        case 1:
            l->time = GETP(GETP(l->anim)->loop)->start + over;
            break;
        case 2:
            l->time = GETP(GETP(l->anim)->outro)->start + over;
            break;
        }
    }
}

void MotionState_DrawSlaves(void *state, s32 arg1) {
    extern Matrix4 g_identity;
    void Thing_DrawSlaves(void *thing);
    typedef struct SlaveThing {
        u8 pad0[0x10];
        Matrix4 mtx;
        Vector4 quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0x82 - 0x78];
        u8 bone;
        u8 pad83[0x8C - 0x83];
        void (**vtbl)();
        u8 pad90[0xA4 - 0x90];
        PTR32(struct SlaveThing) next;
    } SlaveThing;
    typedef struct {
        u8 pad0[0x10];
        PTR32(Matrix4) matrices;
        PTR32(SlaveThing) slaves;
    } State;
    Matrix4 *matrix = (Matrix4 *)PTR(arg1);
    SlaveThing *slave;
    Vector4 quat __attribute__((aligned(16)));
    Matrix4 mtx __attribute__((aligned(16)));
    Matrix4 out __attribute__((aligned(16)));

    slave = GETP(((State *)state)->slaves);
    if (matrix != NULL && matrix != &g_identity) {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if (slave->flags & 0x200200) {
                continue;
            }
            CopyVector(&quat, &slave->quat);
            CopyMatrix(&mtx, &slave->mtx);
            MultiplyMatrix(&GETP(((State *)state)->matrices)[slave->bone], matrix, &out);
            slave->vtbl[9](slave, &out);
            slave->vtbl[4](slave);
            Thing_DrawSlaves(slave);
            CopyVector(&slave->quat, &quat);
            CopyMatrix(&slave->mtx, &mtx);
        }
    } else {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if (slave->flags & 0x200200) {
                continue;
            }
            CopyVector(&quat, &slave->quat);
            CopyMatrix(&mtx, &slave->mtx);
            slave->vtbl[9](slave, &GETP(((State *)state)->matrices)[slave->bone]);
            slave->vtbl[4](slave);
            Thing_DrawSlaves(slave);
            CopyVector(&slave->quat, &quat);
            CopyMatrix(&slave->mtx, &mtx);
        }
    }
}

void MotionState_DrawSlavesAttachCoronaOnly(void *state, s32 arg1) {
    extern Matrix4 g_identity;
    extern u8 g_attachVTable[];
    void Thing_DrawSlaves(void *thing);
    typedef struct SlaveThing {
        u8 pad0[0x10];
        Matrix4 mtx;
        Vector4 quat;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0x82 - 0x78];
        u8 bone;
        u8 pad83[0x8C - 0x83];
        void (**vtbl)();
        u8 pad90[0xA4 - 0x90];
        PTR32(struct SlaveThing) next;
    } SlaveThing;
    typedef struct {
        u8 pad0[0x10];
        PTR32(Matrix4) matrices;
        PTR32(SlaveThing) slaves;
    } State;
    Matrix4 *matrix = (Matrix4 *)PTR(arg1);
    SlaveThing *slave;
    Vector4 quat __attribute__((aligned(16)));
    Matrix4 mtx __attribute__((aligned(16)));
    Matrix4 out __attribute__((aligned(16)));

    slave = GETP(((State *)state)->slaves);
    if (matrix != NULL && matrix != &g_identity) {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if (!(slave->flags & 0x200200) && slave->vtbl == (void *)g_attachVTable) {
                CopyVector(&quat, &slave->quat);
                CopyMatrix(&mtx, &slave->mtx);
                MultiplyMatrix(&GETP(((State *)state)->matrices)[slave->bone], matrix, &out);
                slave->vtbl[9](slave, &out);
                slave->vtbl[4](slave);
                Thing_DrawSlaves(slave);
                CopyVector(&slave->quat, &quat);
                CopyMatrix(&slave->mtx, &mtx);
            }
        }
    } else {
        for (; slave != NULL; slave = GETP(slave->next)) {
            if (!(slave->flags & 0x200200) && slave->vtbl == (void *)g_attachVTable) {
                CopyVector(&quat, &slave->quat);
                CopyMatrix(&mtx, &slave->mtx);
                slave->vtbl[9](slave, &GETP(((State *)state)->matrices)[slave->bone]);
                slave->vtbl[4](slave);
                Thing_DrawSlaves(slave);
                CopyVector(&slave->quat, &quat);
                CopyMatrix(&slave->mtx, &mtx);
            }
        }
    }
}


void MotionState_BuildMatricesWorld(void *arg0, void *arg1, void *motionState, f32 time) {
    u8 frames[0x800];
    void *anim;
    s32 *skel;

    if (M2C_FIELD(motionState, u32 *, 0x10) != 0) {
        anim = SLOT_LOAD(void *, (u8 *)(motionState) + 0);
        skel = SLOT_LOAD(s32 *, (u8 *)(anim) + 0x14);
        MotionState_ExtractBlendedKeyFrame(motionState, frames);
        TEST_AnimTraverseHeirarchyToMatricesWorld(arg0, arg1, frames, *skel, SLOT_LOAD(void *, (u8 *)(motionState) + 0x10), time);
    }
}

void Motion_HeartbeatB2(void *state, Vector4 *vel) {
    void MotionState_BlendTriggerEvents(void *state, void *events, Vector4 *vel);
    void Thing_MoveSlaves(void *thing);
    typedef struct SlaveThing {
        Vector4 vel;
        Matrix4 mtx;
        Vector4 pos;
        u8 pad60[0x74 - 0x60];
        u32 flags;
        u8 pad78[0x82 - 0x78];
        u8 bone;
        u8 pad83[0x8C - 0x83];
        void (**vtbl)();
        u8 pad90[0xA4 - 0x90];
        PTR32(struct SlaveThing) next;
    } SlaveThing;
    typedef struct {
        PTR32(u8) anim;
        u8 pad4[0x10 - 0x4];
        PTR32(Matrix4) matrices;
        PTR32(SlaveThing) slaves;
    } State;
    Matrix4 *matrices;
    SlaveThing *slave;

    MotionState_BlendTriggerEvents(state, GETP(((State *)state)->anim) + 0x2C, vel);
    matrices = GETP(((State *)state)->matrices);
    {
        Vector4 v __attribute__((aligned(16))) = {0};
        Vector4 pos __attribute__((aligned(16)));
        Vector4 oldVel __attribute__((aligned(16)));
        Matrix4 mtx __attribute__((aligned(16)));

        if (vel != NULL) {
            v = *vel;
        }
        for (slave = GETP(((State *)state)->slaves); slave != NULL; slave = GETP(slave->next)) {
            if (slave->flags & 0x201000) {
                continue;
            }
            if (!(slave->flags & 0x400000)) {
                CopyVector(&pos, &slave->pos);
                CopyMatrix(&mtx, &slave->mtx);
                slave->vtbl[9](slave, &matrices[slave->bone]);
                oldVel = slave->vel;
                slave->vel = v;
                slave->vtbl[3](slave);
                Thing_MoveSlaves(slave);
                CopyVector(&slave->pos, &pos);
                CopyMatrix(&slave->mtx, &mtx);
                CopyVector(&slave->vel, &oldVel);
            } else {
                slave->vtbl[3](slave);
            }
        }
    }
    ThingManagerList_RemoveReleasedThings(PTR((s32)HND(state) + 0x14));
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
        } else {
            pos = *--posSp;
            rotSp -= bone->pop + 1;
            rot = *rotSp++;
        }
        bone++;
        mtx += 4;
    }
}



s32 AnimNodes_BlowBranch(u8 *node) {
    s32 count = 1;
    u32 i;
    u8 *child = node + 0x20;

    if (node[0x1F] & 3) {
        return 0;
    }
    node[0x1F] |= 2;
    for (i = 0; i < node[0x1C]; i++) {
        count += AnimNodes_BlowBranch(child);
        child += child[0x1E] << 5;
    }
    return count;
}


