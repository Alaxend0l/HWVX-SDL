/* Pods (the Armageddon rescue drop-off points): an animated model whose
 * slave creatures are let out at the drop point when the pod opens, paying
 * out to the player who brought them. */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "AI.h"

/* A pod's description (PodParams.info). */
typedef struct PodInfo {
    Vector4 target;             /* 0x00 local: where released creatures head */
    Vector4 dropPoint;          /* 0x10 local: where they appear */
    Vector4 modelOrigin;        /* 0x20 local */
    u8 pad30[0xC];              /* 0x30 */
    u32 model;                  /* 0x3C Model handle */
} PodInfo;

typedef struct PodParams {
    Vector4 pos;                /* 0x00 */
    u8 pad10[0x10];             /* 0x10 */
    PTR32(PodInfo) info;        /* 0x20 */
} PodParams;

typedef struct Pod {
    Thing base;                 /* 0x00 player: whose creatures it collects */
    u8 padAC[4];                /* 0xAC */
    MotionState motion;         /* 0xB0 */
    u8 padE8[8];                /* 0xE8 */
    Vector4 dropPoint;          /* 0xF0 world */
    Vector4 target;             /* 0x100 world */
    PTR32(PodInfo) info;        /* 0x110 */
    u32 partMask;               /* 0x114 bit per model part: the "on" variant shows */
    u32 count;                  /* 0x118 creatures inside */
} Pod;

_Static_assert(offsetof(Pod, motion) == 0xB0, "Pod.motion");
_Static_assert(offsetof(Pod, dropPoint) == 0xF0, "Pod.dropPoint");
_Static_assert(offsetof(Pod, info) == 0x110, "Pod.info");

/* Thing.flags bits of a pod (kind-specific). */
enum {
    POD_FLAG_RELEASE = 0x100000, /* let the creatures out this frame */
};

s32 Pod_Init(Pod *pod, PodParams *init) {
    MotionState *motion;

    Thing_Init(&pod->base, 0);
    motion = &pod->motion;
    SETP(pod->info, GETP(init->info));
    pod->partMask = 0;
    pod->count = 0;
    pod->base.pos = init->pos;
    pod->motion.model.h = GETP(pod->info)->model;
    pod->motion.layer.blend = 0.0f;
    MotionState_Init(motion, 0);
    MotionState_BuildMatricesWorld(&pod->base.quat, &pod->base.pos, motion, 1.0f);
    return 1;
}

/* port: takes a pointer, as the vtable calls it; the decomp has s32 */
void Pod_Release(void *arg) {
    MotionState_Release(&((Pod *)arg)->motion);
    Thing_Release(arg);
}

/* Animate the pod; on a release, let each carried creature out at the drop
 * point heading for the target, and pay its reward to the player. */
s32 Pod_Move(Pod *pod) {
    extern Matrix4 g_identity;
    Thing *thing = &pod->base;
    Creature *creature;
    Creature *next;
    Vector4 pos __attribute__((aligned(16)));

    XformVec(THING_MTX(thing), &GETP(pod->info)->dropPoint, &pod->dropPoint);
    XformVec(THING_MTX(thing), &GETP(pod->info)->target, &pod->target);
    XformVec(THING_MTX(thing), &GETP(pod->info)->modelOrigin, &pos);
    MotionState_BuildMatricesWorld(&thing->quat, &pos, &pod->motion, 1.0f);
    MotionState_Heartbeat(&pod->motion, &g_identity, pod);

    if ((thing->flags & 0x10) && !(thing->flags & 0x20) && pod->motion.layer.phase == 2) {
        /* at the end of the loop phase go back to its start */
        MotionAnim *anim = GETP(pod->motion.layer.anim);

        if (pod->motion.layer.time == GETP(anim->phases[2])->end) {
            pod->motion.layer.phase = 0;
            pod->motion.layer.time = GETP(anim->phases[0])->start;
        } else {
            pod->motion.layer.time = GETP(anim->phases[1])->end;
        }
    }

    if (thing->flags & POD_FLAG_RELEASE) {
        for (creature = (Creature *)GETP(thing->slaves); creature != NULL; creature = next) {
            next = (Creature *)GETP(creature->base.next);
            if ((u8 *)GETP(creature->base.vtable) == g_creatureVTable &&
                GETP(((AICreature *)GETP(creature->base.player))->volume) != NULL) {   /* only spawned ones */
                Thing *c = &creature->base;
                Player *owner = GETP(thing->player);
                CreatureInfo *info = GETP(creature->info);
                Vector4 dir __attribute__((aligned(16)));
                u8 type;

                creature->unk1C0 = 1.0f;
                ThingManagerList_Remove(&thing->slaves, creature);
                c->pos = pod->dropPoint;
                ThingManagerList_Append(&g_thingManager_DARYL.activeList, creature);
                Thing_AddToGrid(creature, 6);
                creature->stepTimer = -1;
                creature->stepPos = c->pos;
                creature->stepPos.y -= 1.0f;
                /* face the target, on the ground plane */
                PSVECSubtract(&pod->target, &thing->pos, &c->vel);
                c->vel.y = 0.0f;
                if (PSVECSquareMag(&c->vel)) {
                    PSVECNormalize(&c->vel, &c->vel);
                }
                dir = c->vel;
                creature->heading = atan2(dir.x, dir.z);
                SinCosRad(&c->quat.y, &c->quat.w, 0.5f * creature->heading);
                c->quat.x = 0.0f;
                c->quat.z = 0.0f;
                {
                    /* the matrix of that yaw quaternion */
                    f32 ww = c->quat.w * c->quat.w;
                    f32 yy = c->quat.y * c->quat.y;
                    f32 inv = 1.0f / (yy + ww);
                    f32 m = inv * (ww - yy);

                    inv *= 2.0f;
                    c->axis[0].x = m;
                    c->axis[1].y = 1.0f;
                    c->axis[2].z = m;
                    c->axis[0].y = 0.0f;
                    c->axis[0].z = inv * (c->quat.w * c->quat.y);
                    c->axis[1].x = 0.0f;
                    c->axis[1].z = 0.0f;
                    c->axis[2].x = inv * (c->quat.w * c->quat.y);
                    c->axis[2].y = 0.0f;
                    c->axis[2].w = 0.0f;
                    c->axis[1].w = 0.0f;
                    c->axis[0].w = 0.0f;
                    c->pos.w = 1.0f;
                }
                MotionState_BuildMatricesWorld(&c->quat, &c->pos, &creature->model, 1.0f);
                creature->turnRate = 0.0f;
                c->vel.x *= 15.0f;
                c->vel.y = 10.0f;
                c->vel.z *= 15.0f;
                c->flags &= ~0x10;
                Creature_MotionStateInit(creature, 0xF, 0, 0);
                c->flags |= THING_FLAG_NO_OBJECT_HITS;
                /* the reward */
                owner->stuntPoints += GETP(info->dropOff)[3];
                owner->scoreDirty = 1;
                type = info->dropType;
                if (type < 7) {
                    if (owner->counters[type] != 0) {
                        owner->counters[type]--;
                    }
                } else {
                    owner->counters[type]++;
                }
                owner->countersChanged = 1;
                ((Thing *)GETP(owner->vehicle))->health += GETP(info->dropOff)[0];
                Trigger_CreatureDroppedOff(GETP(owner->vehicle), creature);
                if (GETP(c->trigger) != NULL) {
                    ((TileCreature *)GETP(c->trigger))->mode = 0;
                }
                if (pod->count != 0) {
                    pod->count--;
                }
                thing->flags &= ~0x20;
            }
        }
        thing->flags &= ~POD_FLAG_RELEASE;
    }
    return 1;
}

/* Draw the model, then its parts: each in its on or off variant by partMask. */
void Pod_Draw(Pod *pod) {
    CarPartList *list;
    u32 i;
    CarPart *part;
    u32 bit;
    void *geom;
    Matrix4 mtx;

    if (GETP(pod->motion.animMatrices) != NULL) {
        MotionState_Draw(&pod->motion);
    } else {
        MotionState_Draw2(&pod->motion, HND(THING_MTX(&pod->base)), GETP(pod->motion.animMatrices));
    }
    list = GETP(((MotionAnim *)GETP(pod->motion.model))->parts);
    if (list != NULL) {
        for (i = 0; i < list->count; i++) {
            part = &GETP(list->parts)[i];
            geom = GETP(part->geom);
            bit = pod->partMask & (1 << i);
            if (bit && !(part->flags & 1)) {
                continue;
            }
            if (!bit && (part->flags & 1)) {
                continue;
            }
            mtx = GETP(pod->motion.animMatrices)[part->mtxIndex];
            if (GETP(list->parts)[i].xform) {
                XformVec(&mtx, GETP(GETP(list->parts)[i].offset), &mtx.v[3]);
            }
            Gfx_DrawGeometry(HND(geom), HND(&mtx));
        }
    }
}

/* Whether the pod's description lies in [start, end) (a pack being unloaded). */
s32 Pod_Invalidate(s32 arg, u32 start, u32 end) {
    u32 info = ((Pod *)PTR(arg))->info.h;

    if ((info >= start) && (info < end)) {
        return 1;
    }
    return 0;
}
