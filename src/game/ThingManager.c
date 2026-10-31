/* ThingManager: the thing pools and lists (ThingManager.h), slaves, the
 * per-frame move/collide loop, rendering, and object queries (rays, boxes). */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "ThingManager.h"

extern u8 g_aSharedSlaveHolder[0x200];
#define SHARED_SLAVE_HOLDERS ((SharedSlaveHolder *)g_aSharedSlaveHolder)
#define NUM_SHARED_SLAVE_HOLDERS 64

/* Pushes a thing onto the front of a list. */
void ThingManagerList_Append(ThingList *list, void *arg1) {
    Thing *thing = arg1;

    SETP(thing->next, GETP(*list));
    SETP(thing->prev, NULL);
    if (GETP(thing->next) != NULL) {
        SETP(GETP(thing->next)->prev, thing);
    }
    SETP(*list, thing);
}

/* Unlinks a thing from a list. */
void ThingManagerList_Remove(ThingList *list, void *arg1) {
    Thing *thing = arg1;
    Thing *prev;
    Thing *next;

    prev = GETP(thing->prev);
    if (prev == NULL) {
        SETP(*list, GETP(thing->next));
    } else {
        SETP(prev->next, GETP(thing->next));
    }
    next = GETP(thing->next);
    if (next != NULL) {
        SETP(next->prev, GETP(thing->prev));
        SETP(thing->next, NULL);
    }
    SETP(thing->prev, NULL);
}

/* Puts a released thing back on its type's free list. */
static inline void ThingManager_Free(Thing *thing) {
    thing->flags &= ~THING_FLAG_RELEASED;
    thing->flags |= THING_FLAG_FREE;
    ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
}

/* The first thing of a type (THING_TYPE_*) in a list, or NULL. */
void *ThingManagerList_GetByType(ThingList *list, s32 type) {
    Thing *thing;

    for (thing = GETP(*list); thing != NULL; thing = GETP(thing->next)) {
        if (thing->type == type) {
            return thing;
        }
    }
    return NULL;
}

s32 ThingManagerList_Get(ThingList *list, void *target) {
    Thing *thing;

    for (thing = GETP(*list); thing != NULL; thing = GETP(thing->next)) {
        if (target == thing) {
            return 1;
        }
    }
    return 0;
}

/* Releases and frees the slaves of a list with slaveTag `type`. */
void ThingManagerList_ReleaseSlavesBySlaveType(ThingList *list, u8 type) {
    Thing *thing;
    Thing *next;

    thing = GETP(*list);
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->slaveTag == type) {
            THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
            ThingManagerList_Remove(list, thing);
            ThingManager_Free(thing);
        }
        thing = next;
    }
}

/* Frees the released things of a list (and all their slaves). */
void ThingManagerList_RemoveReleasedThings(ThingList *list) {
    Thing *thing;
    Thing *next;

    for (thing = GETP(*list); thing != NULL; thing = next) {
        next = GETP(thing->next);
        if (thing->flags & THING_FLAG_RELEASED) {
            ThingManagerList_ReleaseAndRemoveAll(&thing->slaves);
            ThingManagerList_Remove(list, thing);
            ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
            thing->flags &= ~THING_FLAG_RELEASED;
            thing->flags |= THING_FLAG_FREE;
        }
    }
}

/* Releases every thing of a list and frees them with their slaves. */
void ThingManagerList_ReleaseAndRemoveAll(ThingList *list) {
    Thing *thing;
    Thing *next;

    ThingManagerList_ReleaseAll(list);
    for (thing = GETP(*list); thing != NULL; thing = next) {
        next = GETP(thing->next);
        if ((thing->flags & THING_FLAG_RELEASED) != 0) {
            ThingManagerList_ReleaseAndRemoveAll(&thing->slaves);
            ThingManagerList_Remove(list, thing);
            ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
            thing->flags &= ~THING_FLAG_RELEASED;
            thing->flags |= THING_FLAG_FREE;
        }
    }
}

/* Calls Release on every thing of a list (they stay in it, flagged). */
void ThingManagerList_ReleaseAll(ThingList *list) {
    Thing *thing;
    Thing *next;

    thing = GETP(*list);
    while (thing != NULL) {
        next = GETP(thing->next);
        THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
        thing = next;
    }
}

/* Detaches the slaves tagged 0xEF from a list into the active list as
 * independent things, at their world transform (`mtx` is their master's). */
void ThingManagerList_EmancipateSlaves(void *mtx, ThingList *list) {
    extern u8 g_itemVTable[];
    Matrix4 out;
    Thing *thing;
    Thing *next;

    thing = GETP(*list);
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->slaveTag == 0xEF) {
            if (GETP(thing->prev) == NULL) {
                SETP(*list, GETP(thing->next));
            } else {
                SETP(GETP(thing->prev)->next, GETP(thing->next));
            }
            if (GETP(thing->next) != NULL) {
                SETP(GETP(thing->next)->prev, GETP(thing->prev));
                SETP(thing->next, NULL);
            }
            SETP(thing->prev, NULL);
            MultiplyMatrix(THING_MTX(thing), mtx, &out);
            *THING_MTX(thing) = out;
            SETP(thing->next, GETP(g_thingManager_DARYL.activeList));
            SETP(thing->prev, NULL);
            if (GETP(thing->next) != NULL) {
                SETP(GETP(thing->next)->prev, thing);
            }
            SETP(g_thingManager_DARYL.activeList, thing);
            if (thing->collisionType != THING_COLLIDE_NONE && (u8 *)GETP(thing->vtable) == g_itemVTable) {
                Thing_AddToGrid(thing, 3);
                thing->flags &= ~THING_FLAG_AT_REST;
            }
        }
        thing = next;
    }
}

/* Releases all shared slaves and empties the holders. */
void SharedSlaveHolder_Release(void) {
    Thing *thing;
    Thing *next;
    SharedSlaveHolder *holder;
    u32 i;

    holder = SHARED_SLAVE_HOLDERS;
    i = 0;
    do {
        if (GETP(holder->key) != NULL) {
            for (thing = GETP(holder->slaves); thing != NULL; thing = next) {
                next = GETP(thing->next);
                THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
            }
            ThingManagerList_RemoveReleasedThings(&holder->slaves);
            SETP(holder->key, NULL);
        }
        i++;
        holder++;
    } while (i < NUM_SHARED_SLAVE_HOLDERS);
}

/* The holder of a SharedSlaves, created with its slaves on first use; 64 if
 * all are taken. */
s32 SharedSlaveHolder_Get(void *shared) {
    s32 i;

    for (i = 0; i < NUM_SHARED_SLAVE_HOLDERS; i++) {
        if (GETP(SHARED_SLAVE_HOLDERS[i].key) == shared) {
            return i;
        }
    }
    for (i = 0; i < NUM_SHARED_SLAVE_HOLDERS; i++) {
        if (GETP(SHARED_SLAVE_HOLDERS[i].key) == NULL) {
            SETP(SHARED_SLAVE_HOLDERS[i].key, shared);
            ThingManagerList_InitAllSlavesNow(&SHARED_SLAVE_HOLDERS[i].slaves, &((SharedSlaves *)shared)->list, 0xFF);
            return i;
        }
    }
    return 64;
}

/* Draws the shared slaves (type 10 triggers of `triggers`, a SlaveList) at
 * `thing`: each slave is moved into the thing's frame, drawn and put back. */
void Thing_DrawSharedSlaves(u8 *arg, u32 *triggers) {
    Thing *thing = (Thing *)arg;
    SlaveList *list = (SlaveList *)triggers;
    u32 i;
    Thing *slave;
    SharedSlaves *shared;
    Quaternion quat __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    for (i = 0; i < list->count; i++) {
        SlaveTrigger *trigger = &GETP(list->triggers)[i];
        if (trigger->type == 10) {
            shared = GETP(trigger->data);
            slave = GETP(SHARED_SLAVE_HOLDERS[GETP(shared->list.triggers)->holder].slaves);
            if (shared->active != 0) {
                for (; slave != NULL; slave = GETP(slave->next)) {
                    quat = slave->quat;
                    pos = slave->pos;
                    QuaternionMult(&thing->quat, &quat, &slave->quat);
                    XformVec(THING_MTX(thing), &pos, &slave->pos);
                    THING_VFN(slave, THING_V_DRAW, void (*)(Thing *))(slave);
                    slave->quat = quat;
                    slave->pos = pos;
                }
            }
        }
    }
}

/* Runs a slave trigger for `list` (a thing's slaves): creates the slave it
 * describes (a, b, c: position, orientation, velocity), tagged with
 * `player`; type 10 sets up shared slaves, 99 releases the matching slaves. */
void ThingManagerList_TriggerSlaves(ThingList *list, struct SlaveTrigger *trigger, Vector4 *a, Vector4 *b, Vector4 *c, u32 player) {
    extern Vector4 g_zeroVector;
    extern void *g_pSourcePlayer;
    struct { Vector4 a; PTR32(void) data; u8 flag; } p3 __attribute__((aligned(16)));
    struct { PTR32(void) data; u8 pad4[0xC]; Vector4 pos; s32 flag; PTR32(void) player; } p5 __attribute__((aligned(16)));
    struct { PTR32(void) data; u8 pad4[0xC]; Vector4 pos; s32 flag; PTR32(void) player; } p6 __attribute__((aligned(16)));
    struct { Vector4 a; Vector4 b; Vector4 c; PTR32(void) data; s32 time; PTR32(void) player; f32 f; } p7 __attribute__((aligned(16)));
    struct { Vector4 unk; Vector4 a; Vector4 b; PTR32(void) data; s32 flag; } p8 __attribute__((aligned(16)));
    struct { PTR32(void) data; u8 pad4[0xC]; Vector4 a; } p9 __attribute__((aligned(16)));
    DebrisParams p1 __attribute__((aligned(16)));
    EmitterParams p2 __attribute__((aligned(16)));
    Vector4 p4 __attribute__((aligned(16)));
    u8 id;
    u8 pl;
    SharedSlaves *shared;
    Thing *slave = NULL;
    void *data;
    Thing *thing;

    switch (trigger->type) {
    case 1:
        p1.debris = trigger->data.h;
        p1.vel = *c;
        p1.owner = HND(g_pSourcePlayer);
        p1.angVel = g_zeroVector;
        if (PTR(p1.debris) != NULL && *(u32 *)PTR(p1.debris) != 0) {
            p1.owner = HND(g_pSourcePlayer);
            p1.unk48 = 0.0f;
            p1.pos = *a;
            p1.quat = *b;
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_EVENT_SEQ, &p1, trigger->id);
        }
        break;
    case 2:
        if (GETP(trigger->data) != NULL) {
            SETP(p2.emitter, GETP(trigger->data));
            p2.drawDist = 0.0f;
            p2.vel = *c;
            p2.owner = HND(g_pSourcePlayer);
            p2.pos = *a;
            p2.pos.w = 1.0f;
            p2.quat = *(Quaternion *)b;
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_EMITTER, &p2, trigger->id);
        }
        break;
    case 3:
        data = GETP(trigger->data);
        if (data != NULL && data != NULL) {
            p3.a = *a;
            SETP(p3.data, data);
            p3.flag = 1;
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_SOUND_SEQ, &p3, trigger->id);
        }
        break;
    case 4:
        p4 = trigger->pos;
        p4.w = 1.0f;
        slave = ThingManager_DARYL_CreateSlave(THING_TYPE_WILDCARD_RACK, &p4, trigger->id);
        break;
    case 5:
        if (GETP(trigger->data) != NULL) {
            SETP(p5.data, GETP(trigger->data));
            p5.pos = trigger->pos;
            p5.flag = 1;
            SETP(p5.player, g_pSourcePlayer);
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_ATTACH, &p5, trigger->id);
        }
        break;
    case 6:
        /* split screen: skip the ones flagged in their data's first byte */
        if (ViewManager_GetViewFormat() > 1 && *(u8 *)GETP(trigger->data) != 0) {
            break;
        }
        if (GETP(trigger->data) != NULL) {
            SETP(p6.data, GETP(trigger->data));
            SETP(p6.player, g_pSourcePlayer);
            p6.pos = trigger->pos;
            p6.flag = 0;
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_ATTACH, &p6, trigger->id);
        }
        break;
    case 7:
        if (GETP(trigger->data) != NULL) {
            SETP(p7.data, GETP(trigger->data));
            p7.f = 0.0f;
            p7.time = 20;
            p7.c = *c;
            SETP(p7.player, g_pSourcePlayer);
            p7.a = *a;
            p7.a.w = 1.0f;
            p7.b = *b;
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_TRACER, &p7, trigger->id);
        }
        break;
    case 8:
        data = GETP(trigger->data);
        if (data != NULL) {
            p8.a = *a;
            p8.b = *b;
            SETP(p8.data, data);
            p8.flag = 0;
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_MOTION, &p8, trigger->id);
        }
        break;
    case 9:
        data = GETP(trigger->data);
        if (data != NULL) {
            p9.a = *a;
            SETP(p9.data, data);
            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_RADAR_BLIP, &p9, trigger->id);
        }
        break;
    case 10:
        shared = GETP(trigger->data);
        if (shared != NULL && shared->active == 0) {
            shared->active = 1;
            GETP(shared->list.triggers)->holder = SharedSlaveHolder_Get(shared);
        }
        break;
    case 99:
        id = trigger->id;
        pl = player;
        for (thing = GETP(*list); thing != NULL; thing = GETP(thing->next)) {
            if (thing->slaveBone == id && thing->slaveTag == pl) {
                THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
            }
        }
        break;
    }

    if (slave != NULL) {
        slave->slaveTag = player;
        SETP(slave->next, GETP(*list));
        SETP(slave->prev, NULL);
        if (GETP(slave->next) != NULL) {
            SETP(GETP(slave->next)->prev, slave);
        }
        SETP(*list, slave);
    }
}

/* Frees the slaves of `list` tagged `player`, then runs all the triggers of
 * `slaves` (a SlaveList) for it. */
void ThingManagerList_InitAllSlavesNow(ThingList *list, void *slaves, u8 player) {
    extern Vector4 g_zeroVector;
    SlaveList *sl = slaves;
    u32 i;
    Thing *next;
    Thing *thing;

    thing = GETP(*list);
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->slaveTag == player) {
            THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
            ThingManagerList_Remove(list, thing);
            ThingManager_Free(thing);
        }
        thing = next;
    }
    for (i = 0; i < sl->count; i++) {
        Quaternion dir = {0.0f, 0.0f, 0.0f, 1.0f};
        SlaveTrigger *trigger = &GETP(sl->triggers)[i];

        ThingManagerList_TriggerSlaves(list, trigger, &trigger->pos, (Vector4 *)&dir, &g_zeroVector, player);
    }
}

/* Puts every pool slot of a type on its free list. */
#define THINGPOOL_INIT(pool, count, vt, t)                                      \
    for (i = 0; i < (count); i++) {                                             \
        thing = &THING_POOLS->pool[i].thing;                                    \
        SETP(thing->vtable, (vt));                                              \
        SETP(thing->slaves, NULL);                                              \
        thing->flags |= THING_FLAG_FREE;                                        \
        SETP(thing->prev, NULL);                                                \
        SETP(thing->next, NULL);                                                \
        thing->type = (t);                                                      \
        ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing); \
    }

/* Empties the lists and puts every slot of every pool on the free lists. */
void ThingManager_DARYL_CreateMain(void) {
    extern u8 g_projectileVTable[];
    extern u8 g_eventSeqVTable[];
    extern u8 g_emitterVTable[];
    extern u8 g_destructibleVTable[];
    extern u8 g_itemVTable[];
    extern u8 g_attachVTable[];
    extern u8 g_debrisVTable[];
    extern u8 g_creatureVTable[];
    extern u8 g_wildCardRackVTable[];
    extern u8 g_physicalObjectVTable[];
    extern u8 g_lookerVTable[];
    extern u8 g_tracerVTable[];
    extern u8 g_podVTable[];
    extern u8 g_droneVTable[];
    extern u8 g_motionVTable[];
    extern u8 g_soundSeqVTable[];
    extern u8 g_radarBlipVTable[];
    Thing *thing;
    u32 i;

    for (i = 0; i < THING_NUM_TYPES; i++) {
        SETP(THING_POOLS->freeList[i], NULL);
    }
    SETP(THING_POOLS->activeList, NULL);
    for (i = 0; i < 140; i++) {
        SETP(THING_POOLS->projectiles[i].thing.vtable, g_projectileVTable);
        thing = &THING_POOLS->projectiles[i].thing;
        THING_POOLS->projectiles[i].unk130 = -1;
        SETP(thing->slaves, NULL);
        thing->flags |= THING_FLAG_FREE;
        SETP(thing->prev, NULL);
        SETP(thing->next, NULL);
        thing->type = THING_TYPE_PROJECTILE;
        ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
    }
    THINGPOOL_INIT(eventSeqs, 192, g_eventSeqVTable, THING_TYPE_EVENT_SEQ);
    THINGPOOL_INIT(emitters, 160, g_emitterVTable, THING_TYPE_EMITTER);
    THINGPOOL_INIT(destructibles, 500, g_destructibleVTable, THING_TYPE_DESTRUCTIBLE);
    THINGPOOL_INIT(items, 96, g_itemVTable, THING_TYPE_ITEM);
    THINGPOOL_INIT(attaches, 192, g_attachVTable, THING_TYPE_ATTACH);
    THINGPOOL_INIT(debris, 128, g_debrisVTable, THING_TYPE_DEBRIS);
    THINGPOOL_INIT(creature, 1, g_creatureVTable, THING_TYPE_CREATURE);
    THINGPOOL_INIT(wildCardRacks, 16, g_wildCardRackVTable, THING_TYPE_WILDCARD_RACK);
    THINGPOOL_INIT(cars, 8, g_carAVTable, THING_TYPE_CAR);
    THINGPOOL_INIT(physicalObjects, 100, g_physicalObjectVTable, THING_TYPE_PHYSICAL_OBJECT);
    THINGPOOL_INIT(looker, 1, g_lookerVTable, THING_TYPE_LOOKER);
    THINGPOOL_INIT(tracers, 32, g_tracerVTable, THING_TYPE_TRACER);
    THINGPOOL_INIT(pod, 1, g_podVTable, THING_TYPE_POD);
    THINGPOOL_INIT(drone, 1, g_droneVTable, THING_TYPE_DRONE);
    THINGPOOL_INIT(motions, 56, g_motionVTable, THING_TYPE_MOTION);
    THINGPOOL_INIT(soundSeqs, 128, g_soundSeqVTable, THING_TYPE_SOUND_SEQ);
    THINGPOOL_INIT(radarBlips, 64, g_radarBlipVTable, THING_TYPE_RADAR_BLIP);
}

#undef THINGPOOL_INIT

void ThingManager_DARYL_Create(void) {
    OBoxHolder_Init();
    OConvexHullHolder_Init();
    ThingManager_DARYL_CreateMain();
    CollisionPairCache_Clear();
    CollisionTriCache_Clear();
    memset(g_aSharedSlaveHolder, 0, sizeof(g_aSharedSlaveHolder));
}

void ThingManager_DARYL_Destroy(void) {
    Thing *thing;
    u32 i;

    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
        THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);
    }
    SETP(g_thingManager_DARYL.activeList, NULL);
    for (i = 0; i < THING_NUM_TYPES; i++) {
        SETP(g_thingManager_DARYL.freeList[i], NULL);
    }
}

void ThingManager_DARYL_Init(void) {
    ThingManager_DARYL_Release();
    MatrixCache_Init();
}

/* Releases every thing (shared slaves first) and resets the pools. */
void ThingManager_DARYL_Release(void) {

    SharedSlaveHolder_Release();
    ThingManagerList_ReleaseAndRemoveAll(&g_thingManager_DARYL.activeList);
    ThingManagerList_ReleaseAndRemoveAll(&g_thingManager_DARYL.activeList);
    ThingManager_DARYL_CreateMain();
}


/* The world is frozen: only the looker (camera) and the sound sequences
 * flagged 0x100000 run. */
void ThingManager_MoveLookerOnly(void) {
    extern u8 g_soundSeqVTable[];
    Thing *looker = &THING_POOLS->looker[0].thing;
    Thing *thing;

    THING_VFN(looker, THING_V_PRE_MOVE, void (*)(Thing *))(looker);
    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
        if ((u8 *)GETP(thing->vtable) == g_soundSeqVTable && (thing->flags & 0x100000)) {
            THING_VFN(thing, THING_V_MOVE, void (*)(Thing *))(thing);
        }
    }
    ThingManagerList_RemoveReleasedThings(&g_thingManager_DARYL.activeList);
}

void ThingManager_DARYL_Heartbeat(void) {
    extern u8 g_bFreezeWorld;
    if ((s8) g_bFreezeWorld != 0) {
        ThingManager_MoveLookerOnly();
        return;
    }
    ThingManager_MoveThings_DARYL_SUBTICK();
}

/* What the vtable's GetSurfaceScript returns: the thing's surface. */
typedef struct MovePhys {
    u8 pad0[0x14];
    f32 friction;               /* 0x14 */
    f32 elasticity;             /* 0x18 */
} MovePhys;

/* CarAEngine fields CarA.h doesn't name yet (CarAInfo.engine, 0x80). */
typedef struct CarAEngineImpact {
    u8 pad0[0x38];
    f32 maxImpactVel;           /* 0x38 */
    u8 pad3C[0x20];
    f32 damageScale;            /* 0x5C car-car bumper hits */
} CarAEngineImpact;

/* An AI player's script (Player.healthScale, 0xD0). */
typedef struct AIScriptImpact {
    u8 pad0[0x1C];
    f32 damageScale;            /* 0x1C */
} AIScriptImpact;

#define CAR_ENGINE_IMPACT(car) ((CarAEngineImpact *)GETP(GETP((car)->vehicleIface)->engine))
#define AI_DAMAGE_SCALE(player) (((AIScriptImpact *)GETP(((Player *)(player))->healthScale))->damageScale)

#define VFN_GET_MASS(t) THING_VFN(t, THING_V_GET_MASS, f32 (*)(Thing *))(t)
#define VFN_GET_PHYS(t) THING_VFN(t, THING_V_GET_SURFACE_SCRIPT, MovePhys *(*)(Thing *))(t)
#define VFN_GET_INV_INERTIA(t, out) THING_VFN(t, THING_V_GET_INV_INERTIA_TENSOR, void (*)(Thing *, Vector4 *))(t, out)

/* Pairs of things already pushed apart this frame (collision impulses are
 * not applied twice). */
extern CollisionInfo g_aCI[8];

static inline u32 CI_Find(Thing *a, Thing *b) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if ((P(Thing, g_aCI[i].thingA) == a && P(Thing, g_aCI[i].thingB) == b) || (P(Thing, g_aCI[i].thingB) == a && P(Thing, g_aCI[i].thingA) == b)) {
            return TRUE;
        }
    }
    return FALSE;
}

static inline void CI_Add(Thing *a, Thing *b) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (P(Thing, g_aCI[i].thingA) == NULL) {
            g_aCI[i].thingA = HND(a);
            g_aCI[i].thingB = HND(b);
            return;
        }
    }
    Dprintf("out of CI slots\n");
}

/* Moves the things for a frame. PreMove on each; then the dynamic things are
 * advanced in sub-steps: each step finds the earliest terrain or thing
 * contact within the time left, moves everything to it, resolves that
 * contact with an impulse (pushing apart overlapping pairs instead), and
 * repeats until the frame's time is used up. Finally Move on each thing,
 * slaves and shared slaves, and released things are freed. */
void ThingManager_MoveThings_DARYL_SUBTICK(void)
{
    extern f32 g_fDeltaTimeSec;
    extern u32 g_frameCounter;
    extern u8 g_carAVTable[];
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    static f32 offset = 0.2f;
    static f32 mult = 10.0f;
    Thing *moved[0x200];
    GridResult objs[0x200];
    CollisionInfo hit;
    CollisionInfo rayHit;
    DebrisParams fx;
    Matrix4 invInertia;
    Matrix4 rot;
    Matrix4 inertia;
    Matrix4 tmp;
    Vector4 push;
    Vector4 min;
    Vector4 max;
    f32 dt;
    f32 dist;
    Thing *thing;
    Thing *t;
    Thing *other;
    Thing *source;
    Thing *otherSource;
    s32 numMoved;
    s32 count;
    s32 i;
    s32 j;
    u32 k;
    u32 gridFlags;
    u32 stuck;
    u32 iters;
    f32 elapsed;
    f32 dtStep;
    f32 dtSave;
    f32 depth;

/* the contact's parties, re-read from `hit` as the original does */
#define HIT_A P(Thing, hit.thingA)
#define HIT_B P(Thing, hit.thingB)
#define HIT_SURFACE P(MovePhys, hit.material)

    stuck = 0;
    iters = 0;
    elapsed = 0.0f;
    numMoved = 0;
    MemClr32(g_aCI, sizeof(g_aCI));
    CollisionPairCache_Heartbeat();
    CollisionTriCache_Heartbeat();
    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
        if (!(thing->flags & 0x280008)) {
            THING_VFN(thing, THING_V_PRE_MOVE, void (*)(Thing *))(thing);
        }
        thing->flags &= ~0x1A040;
        if (thing->collisionType == THING_COLLIDE_NONE) {
            continue;
        }
        if (!(thing->flags & THING_FLAG_DYNAMIC) || (thing->flags & THING_FLAG_RELEASED)) {
            continue;
        }
        moved[numMoved++] = thing;
        if (GETP(thing->gridObject) == NULL || (thing->flags & THING_FLAG_AT_REST)) {
            continue;
        }
        Thing_DynamicGridMove(thing, g_fDeltaTimeSec);
    }

    do {
        hit.thingA = 0;
        hit.thingB = 0;
        dt = dtStep = g_fDeltaTimeSec - elapsed;
        hit.material = 0;
        for (i = 0; i < numMoved; i++) {
            t = moved[i];
            dtSave = dt;
            source = P(Thing, Thing_GetSource(t));
            t->flags |= THING_FLAG_IN_COLLISION_PASS;
            if (t->flags & 0x40) {
                continue;
            }
            if (t->flags & THING_FLAG_RELEASED) {
                continue;
            }
            hit.type = 0;
            if (dt && !(t->flags & 0x24000B) && Thing_HitsTerrain(t, &dt, &hit)) {
                if (hit.type == 0xFF) {
                    /* touching: push out along the normal */
                    if (PSVECDotProduct(&t->vel, &hit.normal) < 0.0f) {
                        f32 d;

                        depth = 0.0f;
                        d = PSVECDotProduct(&t->vel, &hit.normal);
                        if (d <= 15.5f) {
                            depth = -(d - 15.5f);
                        }
                        t->flags |= 0x12000;
                        PSVECScale(&hit.normal, &push, depth);
                        if (PSVECSquareMag(&t->pushVel) < 40000.0f) {
                            t->vel.x += push.x;
                            t->vel.y += push.y;
                            t->vel.z += push.z;
                            t->pushVel.x += push.x;
                            t->pushVel.y += push.y;
                            t->pushVel.z += push.z;
                        }
                    }
                    dt = dtSave;
                } else if (hit.type == 0xFE) {
                    /* embedded */
                    if (Distance_Plane(&hit.normal, &hit.pos) < -offset) {
                        dist = 1.5f;
                        rayHit.type = 0;
                        offset = 0.3f;
                        if (RayHitsTerrain(&t->pos, &hit.pos, &dist, &rayHit)) {
                            if (PSVECSquareMag(&t->pushVel) < 40000.0f) {
                                t->vel.x += rayHit.normal.x * mult;
                                t->vel.y += rayHit.normal.y * mult;
                                t->vel.z += rayHit.normal.z * mult;
                                t->pushVel.x += rayHit.normal.x * mult;
                                t->pushVel.y += rayHit.normal.y * mult;
                                t->pushVel.z += rayHit.normal.z * mult;
                            }
                            hit.type = 0xFF;
                        } else {
                            if (PSVECSquareMag(&t->pushVel) < 40000.0f) {
                                t->vel.x += hit.normal.x * mult;
                                t->vel.y += hit.normal.y * mult;
                                t->vel.z += hit.normal.z * mult;
                                t->pushVel.x += hit.normal.x * mult;
                                t->pushVel.y += hit.normal.y * mult;
                                t->pushVel.z += hit.normal.z * mult;
                            }
                            hit.type = 0xFF;
                        }
                        t->flags |= 0x10080;
                        dt = dtSave;
                    } else {
                        t->pos.x += hit.normal.x * offset;
                        t->pos.y += hit.normal.y * offset;
                        t->pos.z += hit.normal.z * offset;
                        t->flags |= THING_FLAG_MOVED;
                        hit.type = 0;
                    }
                    hit.thingA = HND(t);
                    hit.thingB = 0;
                } else {
                    hit.thingA = HND(t);
                    hit.thingB = 0;
                }
            }
            if (!(t->flags & 0x20002) && dt > 0.0f) {
                gridFlags = 4;
                if (t->flags & 0x4000009) {
                    gridFlags = 0;
                }
                Thing_GetDynamicMinMax(t, &min, &max, dtStep);
                count = Grid_GetObjects(&min, &max, gridFlags | 3, objs, 0x200);
                hit.type = 0;
                for (j = 0; j < count; j++) {
                    other = P(Thing, objs[j].id);
                    dtSave = dt;
                    otherSource = P(Thing, Thing_GetSource(other));
                    if (other->flags & THING_FLAG_IN_COLLISION_PASS) {
                        continue;
                    }
                    if (source != NULL && source == other) {
                        continue;
                    }
                    if (otherSource != NULL && otherSource == t) {
                        continue;
                    }
                    if (!(other->flags & 0x40000004)) {
                        continue;
                    }
                    if (other->flags & 0x200042) {
                        continue;
                    }
                    if ((t->flags & 0x4000009) && (other->flags & 0x4000009)) {
                        continue;
                    }
                    if ((other->flags & THING_FLAG_AT_REST) && (t->flags & THING_FLAG_AT_REST)) {
                        continue;
                    }
                    if (CI_Find(t, other)) {
                        continue;
                    }
                    if (!Thing_TestThingCollision(t, other, &dt, &hit)) {
                        continue;
                    }
                    if (hit.type == 0xFF && VFN_GET_PHYS(t)->elasticity > 0.0f &&
                        VFN_GET_PHYS(other)->elasticity > 0.0f && t->health > 0 && other->health > 0) {
                        /* touching: push both apart (all onto one side if
                         * the other doesn't move) */
                        f32 pushA = 0.0f;
                        f32 pushB = 0.0f;
                        f32 dA = PSVECDotProduct(&t->vel, &hit.normal);
                        f32 dB = PSVECDotProduct(&other->vel, &hit.normal);

                        if (dA - 5.5f <= 0.0f) {
                            pushA = -(dA - 5.5f);
                        }
                        if (dB + 5.5f >= 0.0f) {
                            pushB = dB + 5.5f;
                        }
                        t->flags |= THING_FLAG_PUSHED;
                        other->flags |= THING_FLAG_PUSHED;
                        if (t->flags & 0x4002008) {
                            pushB += pushA;
                            t->flags &= ~THING_FLAG_PUSHED;
                            pushA = 0.0f;
                        } else if (other->flags & 0x4002008) {
                            pushA += pushB;
                            other->flags &= ~THING_FLAG_PUSHED;
                            pushB = 0.0f;
                        }
                        if (PSVECSquareMag(&t->pushVel) < 40000.0f) {
                            t->vel.x += hit.normal.x * pushA;
                            t->vel.y += hit.normal.y * pushA;
                            t->vel.z += hit.normal.z * pushA;
                            t->pushVel.x += hit.normal.x * pushA;
                            t->pushVel.y += hit.normal.y * pushA;
                            t->pushVel.z += hit.normal.z * pushA;
                        }
                        if (PSVECSquareMag(&other->pushVel) < 40000.0f) {
                            other->vel.x -= hit.normal.x * pushB;
                            other->vel.y -= hit.normal.y * pushB;
                            other->vel.z -= hit.normal.z * pushB;
                            other->pushVel.x -= hit.normal.x * pushB;
                            other->pushVel.y -= hit.normal.y * pushB;
                            other->pushVel.z -= hit.normal.z * pushB;
                        }
                        CI_Add(t, other);
                        dt = dtSave;
                    } else {
                        hit.thingA = HND(t);
                        hit.thingB = HND(other);
                        hit.material = 0;
                    }
                }
            }
        }

        if (dt <= 0.0f) {
            /* no progress: after 10 such steps stop the things in contact */
            stuck++;
            dt = 0.0f;
            if (stuck > 10) {
                if (!(HIT_A->flags & 0x4000008)) {
                    HIT_A->vel.x = 0.0f;
                    HIT_A->vel.y = 0.0f;
                    HIT_A->vel.z = 0.0f;
                    HIT_A->flags |= 0x40;
                }
                if (HIT_B != NULL && !(HIT_B->flags & 0x4000008)) {
                    HIT_B->vel.x = 0.0f;
                    HIT_B->vel.y = 0.0f;
                    HIT_B->vel.z = 0.0f;
                    HIT_B->flags |= 0x40;
                }
            }
        } else {
            stuck = 0;
            for (i = 0; i < numMoved; i++) {
                t = moved[i];
                if (!(t->flags & THING_FLAG_AT_REST)) {
                    t->pos.x += t->vel.x * dt;
                    t->pos.y += t->vel.y * dt;
                    t->pos.z += t->vel.z * dt;
                    t->flags |= THING_FLAG_MOVED;
                }
            }
        }

        if (HIT_A != NULL) {
            /* the impulse along the normal between A and B (or the surface) */
            f32 vA = PSVECDotProduct(&hit.normal, &HIT_A->vel);
            f32 vB = 0.0f;
            f32 invMassA = 0.0f;
            f32 invMassB = 0.0f;
            f32 elastA = VFN_GET_PHYS(HIT_A)->elasticity;
            s32 damageB = 0;
            s32 damageA = HIT_A->damage;
            Vector4 crossA;
            Vector4 crossB = {0.0f, 0.0f, 0.0f, 0.0f};
            f32 elastB = 1.0f;
            Vector4 negVel;
            Vector4 rA;
            Vector4 rxnA;
            Vector4 wA;
            Vector4 diagA;
            Vector4 localA;
            Vector4 scaledA;
            Vector4 rB;
            Vector4 rxnB;
            Vector4 wB;
            Vector4 diagB;
            Vector4 sum;
            Vector4 armA;
            Vector4 armB __attribute__((aligned(16)));

            if (HIT_B == NULL && (u8 *)GETP(HIT_A->vtable) == g_carAVTable &&
                PSVECDotProduct(&HIT_A->axis[1], &hit.normal) < 0.5f) {
                /* a car hitting the terrain with its side or roof */
                elastA *= CarA_GetImpactMultiplier(HIT_A, &hit, 1.0f);
                if (HIT_A->axis[1].y < 0.0f) {
                    f32 blend = 0.6f * HIT_SURFACE->friction * VFN_GET_PHYS(HIT_A)->friction;

                    negVel.x = -HIT_A->vel.x;
                    negVel.y = -HIT_A->vel.y;
                    negVel.z = -HIT_A->vel.z;
                    if (blend > 1.0f) {
                        blend = 1.0f;
                    }
                    if (PSVECSquareMag(&negVel) != 0.0f) {
                        PSVECNormalize(&negVel, &negVel);
                    }
                    hit.normal.x = (1.0f - blend) * hit.normal.x + blend * negVel.x;
                    hit.normal.y = (1.0f - blend) * hit.normal.y + blend * negVel.y;
                    hit.normal.z = (1.0f - blend) * hit.normal.z + blend * negVel.z;
                    if (PSVECSquareMag(&hit.normal) != 0.0f) {
                        PSVECNormalize(&hit.normal, &hit.normal);
                    }
                }
            }
            if (!(HIT_A->flags & 0x4000008) || HIT_A->health == 0) {
                invMassA = 1.0f / VFN_GET_MASS(HIT_A);
            }
            if ((u8 *)GETP(HIT_A->vtable) == g_carAVTable &&
                (HIT_B == NULL || (HIT_B->health != 0 && (HIT_B->flags & THING_FLAG_AT_REST) &&
                                   VFN_GET_PHYS(HIT_B)->elasticity > 0.0f))) {
                /* cap a car's speed into the contact */
                CarA *car = (CarA *)HIT_A;
                f32 lim = CAR_ENGINE_IMPACT(car)->maxImpactVel * (0.7f * CarA_GetImpactMultiplier(car, &hit, 1.0f) + 0.3f);

                if (vA > lim) {
                    f32 d = lim - vA;

                    vA = lim;
                    HIT_A->vel.x += hit.normal.x * d;
                    HIT_A->vel.y += hit.normal.y * d;
                    HIT_A->vel.z += hit.normal.z * d;
                } else if (vA < -lim) {
                    f32 d = -lim - vA;

                    vA = -lim;
                    HIT_A->vel.x += hit.normal.x * d;
                    HIT_A->vel.y += hit.normal.y * d;
                    HIT_A->vel.z += hit.normal.z * d;
                }
            }
            PSVECSubtract(&hit.pos, &HIT_A->pos, &rA);
            PSVECCrossProduct(&rA, &hit.normal, &rxnA);
            VFN_GET_INV_INERTIA(HIT_A, &diagA);
            InverseXformVecOrientation(THING_MTX(HIT_A), &rxnA, &localA);
            scaledA.x = diagA.x * localA.x;
            scaledA.y = diagA.y * localA.y;
            scaledA.z = diagA.z * localA.z;
            XformVecOrientation(THING_MTX(HIT_A), &scaledA, &wA);
            PSVECCrossProduct(&wA, &rA, &crossA);
            if (HIT_B != NULL) {
                vB = PSVECDotProduct(&hit.normal, &HIT_B->vel);
                elastB = VFN_GET_PHYS(HIT_B)->elasticity;
                if (!(HIT_B->flags & 0x4000008) || HIT_B->health == 0) {
                    invMassB = 1.0f / VFN_GET_MASS(HIT_B);
                }
                if (HIT_B->type == THING_TYPE_CAR) {
                    elastB *= CarA_GetImpactMultiplier(HIT_B, &hit, -1.0f);
                    if (HIT_A->type == THING_TYPE_CAR) {
                        /* car-car impact effect, picked by the closing speed */
                        WorldGameData *fx_ = GETP(WORLD()->gameData);
                        f32 rel = vB - vA;
                        f32 lo = fx_->impactFxMinVel;
                        u32 n = fx_->numImpactFx;
                        f32 hi = fx_->impactFxMaxVel;

                        if (rel > lo && hi - lo != 0.0f && n != 0) {
                            u32 idx = (f32)n * ((rel - lo) / (hi - lo));
                            u32 id;

                            if (idx > n) {
                                idx = n;
                            }
                            id = GETP(fx_->impactFx)[(s32)(idx - 1)]; /* idx 0 reads ids[-1], as the 32-bit wrap does on GC */
                            if (id != 0) {
                                fx.debris = id;
                                fx.pos = hit.pos;
                                fx.vel = g_zeroVector;
                                fx.owner = 0;
                                fx.unk48 = 0.0f;
                                fx.unk4C = 0.0f;
                                fx.angVel = g_zeroVector;
                                fx.quat = g_zeroVector;
                                ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &fx);
                            }
                        }
                    }
                }
                if (HIT_B->type == THING_TYPE_CAR && (HIT_A->flags & THING_FLAG_AT_REST) && HIT_A->health != 0 && elastA > 0.0f) {
                    f32 lim = CAR_ENGINE_IMPACT((CarA *)HIT_B)->maxImpactVel;

                    if (vB > lim) {
                        f32 d = lim - vB;

                        vB = lim;
                        HIT_B->vel.x += hit.normal.x * d;
                        HIT_B->vel.y += hit.normal.y * d;
                        HIT_B->vel.z += hit.normal.z * d;
                    } else if (vB < -lim) {
                        f32 d = -lim - vB;

                        vB = -lim;
                        HIT_B->vel.x += hit.normal.x * d;
                        HIT_B->vel.y += hit.normal.y * d;
                        HIT_B->vel.z += hit.normal.z * d;
                    }
                }
                PSVECSubtract(&hit.pos, &HIT_B->pos, &rB);
                rot = *THING_MTX(HIT_B);
                inertia = g_identity;
                VFN_GET_INV_INERTIA(HIT_B, &diagB);
                TransposeSubMatrix(&rot);
                inertia.v[0].x = diagB.x;
                inertia.v[1].y = diagB.y;
                inertia.v[2].z = diagB.z;
                MultiplySubMatrix(&inertia, THING_MTX(HIT_B), &tmp);
                MultiplySubMatrix(&rot, &tmp, &invInertia);
                PSVECCrossProduct(&rB, &hit.normal, &rxnB);
                XformVecOrientation(&invInertia, &rxnB, &wB);
                PSVECCrossProduct(&wB, &rB, &crossB);
                damageB = HIT_B->damage;
            } else if (HIT_SURFACE != NULL) {
                elastB = HIT_SURFACE->elasticity;
            }
            vA = (vA - vB) * -(1.0f + _fabs(elastA * elastB));
            PSVECAdd(&crossA, &crossB, &sum);
            vA /= (invMassA + invMassB) * PSVECDotProduct(&hit.normal, &hit.normal) + PSVECDotProduct(&sum, &hit.normal);
            /* elastA/B now carry the impulse each side gets */
            if (elastB < 0.0f) {
                elastB = 0.0f;
            } else {
                elastB = vA;
            }
            if (elastA < 0.0f) {
                elastA = 0.0f;
            } else {
                elastA = -vA;
            }
            if (HIT_B != NULL && (u8 *)GETP(HIT_A->vtable) == g_carAVTable &&
                (u8 *)GETP(HIT_B->vtable) == g_carAVTable) {
                /* car-car: a hit at bumper height does damage by the impulse */
                CarA *car = (CarA *)HIT_A;
                Vector4 *box = GETP(GETP(GETP(car->vehicleIface)->collider)->box);

                armA.x = hit.pos.x - ((Thing *)car)->pos.x;
                armA.y = hit.pos.y - ((Thing *)car)->pos.y;
                armA.z = hit.pos.z - ((Thing *)car)->pos.z;
                if (FloatsEqual(PSVECDotProduct(&((Thing *)car)->axis[2], &armA) - car->bodyOffset.z, box[1].z, 0.3f)) {
                    damageA = -vA * CAR_ENGINE_IMPACT(car)->damageScale * PSVECDotProduct(&HIT_A->axis[2], &hit.normal);
                    if (GETP(HIT_A->player) != NULL && !Player_IsHuman(GETP(HIT_A->player))) {
                        damageA = (f32)damageA * AI_DAMAGE_SCALE(GETP(HIT_A->player));
                    }
                }
                car = (CarA *)HIT_B;
                box = GETP(GETP(GETP(car->vehicleIface)->collider)->box);
                armB.x = hit.pos.x - ((Thing *)car)->pos.x;
                armB.y = hit.pos.y - ((Thing *)car)->pos.y;
                armB.z = hit.pos.z - ((Thing *)car)->pos.z;
                if (FloatsEqual(PSVECDotProduct(&((Thing *)car)->axis[2], &armB) - car->bodyOffset.z, box[1].z, 0.3f)) {
                    damageB = vA * CAR_ENGINE_IMPACT(car)->damageScale * PSVECDotProduct(&HIT_B->axis[2], &hit.normal);
                    if (GETP(HIT_B->player) != NULL && !Player_IsHuman(GETP(HIT_B->player))) {
                        damageB = (f32)damageB * AI_DAMAGE_SCALE(GETP(HIT_B->player));
                    }
                }
            }
            THING_VFN(HIT_A, THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(Thing *, CollisionInfo *, s32, f32))(HIT_A, &hit, damageB, elastB);
            Thing_DynamicGridMove(HIT_A, dtStep);
            if (HIT_B != NULL) {
                THING_VFN(HIT_B, THING_V_DYNAMIC_COLLIDE_IMPACT, void (*)(Thing *, CollisionInfo *, s32, f32))(HIT_B, &hit, damageA, elastA);
                Thing_DynamicGridMove(HIT_B, dtStep);
            }
        }

        for (i = 0; i < numMoved; i++) {
            t = moved[i];
            THING_VFN(t, THING_V_DYNAMIC_ROTATION, void (*)(Thing *, f32))(t, dt);
            t->flags &= ~THING_FLAG_IN_COLLISION_PASS;
        }
        if (dt < 0.00001f && iters > 100) {
            elapsed = g_fDeltaTimeSec;
        }
        elapsed += dt;
        iters++;
    } while (elapsed < g_fDeltaTimeSec);

#undef HIT_A
#undef HIT_B
#undef HIT_SURFACE

    g_frameCounter++;
    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = GETP(t->next)) {
        if (GETP(t->gridObject) != NULL && !(t->flags & 0x200008)) {
            Thing_RotationalGridMove(t);
        }
    }
    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = GETP(t->next)) {
        if (!(t->flags & THING_FLAG_PUSHED)) {
            t->vel.x -= t->pushVel.x;
            t->vel.y -= t->pushVel.y;
            t->vel.z -= t->pushVel.z;
            t->pushVel.x = 0.0f;
            t->pushVel.y = 0.0f;
            t->pushVel.z = 0.0f;
        }
        if ((u8 *)GETP(t->vtable) == g_carAVTable) {
            t->vel.x -= g_fDeltaTimeSec * (((CarA *)t)->forwardAccel * t->axis[0].x);
            t->vel.y -= g_fDeltaTimeSec * (((CarA *)t)->forwardAccel * t->axis[0].y);
            t->vel.z -= g_fDeltaTimeSec * (((CarA *)t)->forwardAccel * t->axis[0].z);
        }
        if (!(t->flags & THING_FLAG_RELEASED)) {
            THING_VFN(t, THING_V_MOVE, void (*)(Thing *))(t);
            if (!(t->flags & THING_FLAG_AT_REST) && GETP(t->gridObject) != NULL) {
                Thing_StaticGridMove(t);
            }
            Thing_MoveSlaves(t);
        }
    }
    for (k = 0; k < NUM_SHARED_SLAVE_HOLDERS; k++) {
        if (GETP(SHARED_SLAVE_HOLDERS[k].key) != NULL) {
            Thing *slave = GETP(SHARED_SLAVE_HOLDERS[k].slaves);

            while (slave != NULL) {
                Thing *next = GETP(slave->next);

                THING_VFN(slave, THING_V_MOVE, void (*)(Thing *))(slave);
                if (Timer_Interrupt300()) {
                    f32 inv = 1.0f / Sqrt(slave->quat.x * slave->quat.x + slave->quat.y * slave->quat.y +
                                          slave->quat.z * slave->quat.z + slave->quat.w * slave->quat.w);

                    slave->quat.x *= inv;
                    slave->quat.y *= inv;
                    slave->quat.z *= inv;
                    slave->quat.w *= inv;
                }
                slave = next;
            }
        }
    }
    ThingManagerList_RemoveReleasedThings(&g_thingManager_DARYL.activeList);
}

/* Draws the visible things in view, with their slaves. */
void ThingManager_DARYL_Render(void) {
    extern void *g_pNoRayCollideThing;
    Thing *thing;

    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
        if (!(thing->flags & (THING_FLAG_RELEASED | THING_FLAG_HIDDEN)) && Gfx_FrustrumCullSphereTestAllWindows(&thing->pos, thing->radius)) {
            THING_VFN(thing, THING_V_DRAW, void (*)(Thing *))(thing);
            g_pNoRayCollideThing = thing;
            Thing_DrawSlaves(thing);
        }
    }
    g_pNoRayCollideThing = NULL;
}

/* Takes the first free thing of a type, or NULL. */
static inline Thing *ThingManager_Alloc(s32 type) {
    Thing *thing = GETP(g_thingManager_DARYL.freeList[type]);

    if (thing == NULL) {
        thing = NULL;
    } else {
        SETP(g_thingManager_DARYL.freeList[type], GETP(thing->next));
        if (GETP(g_thingManager_DARYL.freeList[type]) != NULL) {
            SETP(((Thing *)GETP(g_thingManager_DARYL.freeList[type]))->prev, NULL);
        }
        SETP(thing->next, NULL);
    }
    return thing;
}

/* Creates a thing of a type (THING_TYPE_*) in the active list, initialised
 * by its vtable Init from `params`; NULL if the pool is used up. */
void *ThingManager_DARYL_CreateThing(s32 type, void *params) {
    Thing *thing = ThingManager_Alloc(type);

    if (thing != NULL) {
        SETP(thing->next, GETP(g_thingManager_DARYL.activeList));
        SETP(thing->prev, NULL);
        if (GETP(thing->next) != NULL) {
            SETP(GETP(thing->next)->prev, thing);
        }
        SETP(g_thingManager_DARYL.activeList, thing);
        THING_VFN(thing, THING_V_INIT, void (*)(Thing *, void *))(thing, params);
        return thing;
    }
    return NULL;
}

/* As CreateThing, for a slave: not put in any list (the caller links it to
 * its master's), slaveBone set to `id`. */
void *ThingManager_DARYL_CreateSlave(s32 type, void *params, u8 id) {
    Thing *slave = ThingManager_Alloc(type);

    if (slave != NULL) {
        THING_VFN(slave, THING_V_INIT, void (*)(Thing *, void *))(slave, params);
        slave->slaveBone = id;
        slave->flags |= THING_FLAG_SLAVE;
        return slave;
    }
    return NULL;
}

/* Whether an oriented box overlaps an oriented-box thing near `thing`; the
 * pair goes in `info`. */
s32 OBox_CollideAllThingsZeroTime(void *obox, Thing *thing, void *info) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    GridResult results[256];
    u32 count;
    u32 i;
    Thing *other;
    f32 t;
    f32 dist;
    f32 r;
    void *otherBox;

    Thing_GetStaticRotationalMinMax(thing, &min, &max);
    count = Grid_GetObjects(&min, &max, 5, results, 0x100);
    for (i = 0; i < count; i++) {
        other = P(Thing, results[i].id);
        if (!(other->flags & THING_FLAG_DEAD) && other->collisionType != THING_COLLIDE_NONE && other != thing) {
            dist = PSVECSquareDistance(&thing->pos, &other->pos);
            r = other->radius + thing->radius;
            if (r * r > dist) {
                t = 1.0f;
                if (other->collisionType == THING_COLLIDE_OBOX) {
                    otherBox = THING_VFN(other, THING_V_GET_ORIENTED_COLLISION, void *(*)(Thing *))(other);
                    if (OBoxHitsOBoxZeroTime(obox, otherBox, info)) {
                        if (info != NULL) {
                            ((CollisionInfo *)info)->thingA = HND(thing);
                            ((CollisionInfo *)info)->thingB = HND(other);
                        }
                        t = 0.0f;
                    } else {
                        t = 1.0f;
                    }
                }
                if (t < 1.0f) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Nearest hit of a ray (start + t * dir, t up to *t) with the things along
 * it, except g_pNoRayCollideThing; shortens *t. */
s8 RayHitsObjects_Aux(Vector4 *start, Vector4 *dir, f32 *t, void *info) {
    extern void *g_pNoRayCollideThing;
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 end __attribute__((aligned(16)));
    GridResult results[512];
    u32 count;
    u32 i;
    Thing *other;
    f32 best;
    f32 hit;

    best = *t;
    max = *start;
    end.x = dir->x * best + start->x;
    end.y = dir->y * best + start->y;
    end.z = dir->z * best + start->z;
    min = end;
    if (start->x < min.x) {
        min.x = start->x;
    }
    if (start->y < min.y) {
        min.y = start->y;
    }
    if (start->z < min.z) {
        min.z = start->z;
    }
    if (end.x > max.x) {
        max.x = end.x;
    }
    if (end.y > max.y) {
        max.y = end.y;
    }
    if (end.z > max.z) {
        max.z = end.z;
    }
    count = Grid_GetObjects(&min, &max, 5, results, 0x200);
    for (i = 0; i < count; i++) {
        other = P(Thing, results[i].id);
        if (!(other->flags & THING_FLAG_DEAD) && other != g_pNoRayCollideThing &&
            SegmentVsSphere(&other->pos, start, &end, other->radius)) {
            hit = THING_VFN(other, THING_V_COLLIDE_WITH_RAY, f32 (*)(void *, Vector4 *, Vector4 *, void *, f32))(other, start, dir, info, best);
            if (hit < *t) {
                best = hit;
            }
        }
    }
    if (best < *t) {
        *t = best;
        return 1;
    }
    return 0;
}

s8 RayHitsTerrainAndObjects(void *arg0, void *arg1, void *arg2, void *arg3) {
    s8 hitTerrain;

    if (WORLD() == NULL || GETP(WORLD()->collision) == NULL) {
        return 0;
    }
    hitTerrain = RayHitsTerrain_Aux3(arg0, arg1, arg2, arg3);
    if (RayHitsObjects_Aux(arg0, arg1, arg2, arg3)) {
        return 1;
    }
    return hitTerrain;
}

/* Whether an oriented box (of `thing`) overlaps the terrain or another
 * oriented-box thing, each unless the thing's flags exclude it. */
s8 OBoxHitsTerrainAndObjectsZeroTime(void *obox, void *arg, void *info) {
    extern Vector4 g_zeroVector;
    Thing *thing = arg;
    f32 time;
    Vector4 max;
    Vector4 min;
    GridResult results[96];
    u32 count;
    u32 i;
    Thing *other;
    /* port: only OBox objects (type 1) set t; the original reuses f31 for the
     * others: the previous object's t (>= 1, no hit) or the caller's f31 if
     * none came before. Start at "no hit" so the latter is defined too. */
    f32 t = 1.0f;
    s32 hit;

    time = 1.0f;
    if (!(thing->flags & THING_FLAG_NO_TERRAIN_HITS)) {
        if (OBoxHitsTerrain(obox, &g_zeroVector, &time, info)) {
            return 1;
        }
    }
    if (thing->flags & THING_FLAG_NO_OBJECT_HITS) {
        hit = 0;
    } else {
        Thing_GetStaticRotationalMinMax(thing, &min, &max);
        count = Grid_GetObjects(&min, &max, 5, results, 0x60);
        for (i = 0; i < count; i++) {
            other = P(Thing, results[i].id);
            if (!(other->flags & THING_FLAG_DEAD) && other->collisionType != THING_COLLIDE_NONE && other != thing &&
                (other->radius + thing->radius) * (other->radius + thing->radius) >
                    PSVECSquareDistance(&thing->pos, &other->pos)) {
                if (other->collisionType == THING_COLLIDE_OBOX) {
                    t = OBoxHitsOBox(obox, &g_zeroVector, THING_VFN(other, THING_V_GET_ORIENTED_COLLISION, void *(*)(Thing *))(other), &g_zeroVector, 0, 1.0f);
                }
                if (t < 1.0f) {
                    hit = 1;
                    goto done;
                }
            }
        }
        hit = 0;
    }
done:
    return hit;
}

s8 OBoxHitsTerrainAndObjectsZeroTimeEdgesOnly(void *obox, void *thing, void *arg2) {
    if (!(((Thing *)thing)->flags & THING_FLAG_NO_TERRAIN_HITS) && OBoxHitsTerrainZeroTimeEdgesOnly(obox, arg2)) {
        return 1;
    }
    return OBox_CollideAllThingsZeroTime(obox, thing, arg2);
}

/* Nearest hit of the segment start-end (t up to *t) with destructibles,
 * the static ones and/or the damageable ones; shortens *t. Always 0. */
s32 RayHitsObjects_GetObject(Vector4 *start, Vector4 *end, f32 *t, s8 statics, s8 dynamics) {
    GridResult results[256];
    CollisionInfo info __attribute__((aligned(16)));
    Vector4 hitEnd __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    u32 count;
    u32 i;
    Destructible *other;
    f32 best;
    f32 hit;

    best = *t;
    dir.x = end->x - start->x;
    dir.y = end->y - start->y;
    dir.z = end->z - start->z;
    hitEnd.x = dir.x * best + start->x;
    hitEnd.y = dir.y * best + start->y;
    hitEnd.z = dir.z * best + start->z;
    min = *end;
    max = *start;
    if (start->x < min.x) {
        min.x = start->x;
    }
    if (start->y < min.y) {
        min.y = start->y;
    }
    if (start->z < min.z) {
        min.z = start->z;
    }
    if (end->x > max.x) {
        max.x = end->x;
    }
    if (end->y > max.y) {
        max.y = end->y;
    }
    if (end->z > max.z) {
        max.z = end->z;
    }
    count = Grid_GetObjects(&min, &max, 4, results, 0x100);
    for (i = 0; i < count; i++) {
        other = (Destructible *)PTR(results[i].id);
        if (other->base.flags & THING_FLAG_DEAD) {
            continue;
        }
        if ((statics == 1 && GETP(GETP(other->record)->info)->takesDamage == 0 && GETP(GETP(other->record)->info)->armour == 0) ||
            (dynamics == 1 && (GETP(GETP(other->record)->info)->takesDamage == 1 || GETP(GETP(other->record)->info)->armour != 0))) {
            if (SegmentVsSphere(&other->base.pos, start, &hitEnd, other->base.radius)) {
                hit = THING_VFN(other, THING_V_COLLIDE_WITH_RAY, f32 (*)(void *, Vector4 *, Vector4 *, void *, f32))(other, start, &dir, &info, best);
                if (hit < *t) {
                    best = hit;
                }
            }
        }
    }
    if (best < *t) {
        *t = best;
        return 0;
    }
    return 0;
}

/* The things of a pool that are in use. */
#define THING_IN_USE(thing) (!((thing)->flags & (THING_FLAG_RELEASED | THING_FLAG_FREE)))

s32 ThingManager_GetActiveCars(void **cars, s32 max) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 8; i++) {
        Thing *car = &THING_POOLS->cars[i].thing;
        if (THING_IN_USE(car)) {
            cars[count++] = car;
        }
    }
    return count;
}

s32 ThingManager_GetActiveItems(void **items, s32 max) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 96; i++) {
        Thing *item = &THING_POOLS->items[i].thing;
        if (THING_IN_USE(item)) {
            items[count++] = item;
        }
    }
    return count;
}

s32 ThingManager_GetActiveSoundSequences(void **seqs, s32 max) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 128; i++) {
        Thing *seq = &THING_POOLS->soundSeqs[i].thing;
        if (THING_IN_USE(seq)) {
            seqs[count++] = seq;
        }
    }
    return count;
}

s32 GetAllTargets(void **targets) {
    return ThingManager_GetActiveCars(targets, 8);
}

static inline s32 ThingManager_GetActiveDrones(void **things) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 1; i++) {
        Thing *thing = &THING_POOLS->drone[i].thing;
        if (THING_IN_USE(thing)) {
            things[count++] = thing;
        }
    }
    return count;
}

static inline s32 ThingManager_GetActiveCreatures(void **things) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 1; i++) {
        Thing *thing = &THING_POOLS->creature[i].thing;
        if (THING_IN_USE(thing)) {
            things[count++] = thing;
        }
    }
    return count;
}

static inline s32 ThingManager_GetActiveDestructibles(void **things) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 500; i++) {
        Thing *thing = &THING_POOLS->destructibles[i].thing;
        if (THING_IN_USE(thing)) {
            things[count++] = thing;
        }
    }
    return count;
}

/* The first car, drone, creature or destructible within ~37 degrees of
 * `dir` from `pos` (and not within 1 unit), else the looker. */
void *FindHost(Vector4 *pos, Vector4 *dir) {
    void *items[500];
    void *cars[8];
    void *pods[8];
    Vector4 dir1 __attribute__((aligned(16)));
    Vector4 diff1 __attribute__((aligned(16)));
    Vector4 dir2 __attribute__((aligned(16)));
    Vector4 diff2 __attribute__((aligned(16)));
    Vector4 dir3 __attribute__((aligned(16)));
    Vector4 diff3 __attribute__((aligned(16)));
    Vector4 dir4 __attribute__((aligned(16)));
    Vector4 diff4 __attribute__((aligned(16)));
    void *bases[4];
    u32 count;
    u32 i;
    u32 j;
    u32 k;
    u32 m;

    count = ThingManager_GetActiveCars(cars, 8);
    for (i = 0; i < count; i++) {
        PSVECSubtract(&((Thing *)cars[i])->pos, pos, &diff1);
        dir1 = diff1;
        if (!(PSVECSquareMag(&diff1) < 1.0f)) {
            if (PSVECSquareMag(&dir1)) {
                PSVECNormalize(&dir1, &dir1);
            }
            if (PSVECDotProduct(dir, &dir1) > 0.8f) {
                return cars[i];
            }
        }
    }
    count = ThingManager_GetActiveDrones(pods);
    for (j = 0; j < count; j++) {
        PSVECSubtract(&((Thing *)pods[j])->pos, pos, &diff2);
        dir2 = diff2;
        if (!(PSVECSquareMag(&diff2) < 1.0f)) {
            if (PSVECSquareMag(&dir2)) {
                PSVECNormalize(&dir2, &dir2);
            }
            if (PSVECDotProduct(dir, &dir2) > 0.8f) {
                return pods[j];
            }
        }
    }
    count = ThingManager_GetActiveCreatures(bases);
    for (k = 0; k < count; k++) {
        PSVECSubtract(&((Thing *)bases[k])->pos, pos, &diff3);
        dir3 = diff3;
        if (!(PSVECSquareMag(&diff3) < 1.0f)) {
            if (PSVECSquareMag(&dir3)) {
                PSVECNormalize(&dir3, &dir3);
            }
            if (PSVECDotProduct(dir, &dir3) > 0.8f) {
                return bases[k];
            }
        }
    }
    count = ThingManager_GetActiveDestructibles(items);
    for (m = 0; m < count; m++) {
        PSVECSubtract(&((Thing *)items[m])->pos, pos, &diff4);
        dir4 = diff4;
        if (!(PSVECSquareMag(&diff4) < 1.0f)) {
            if (PSVECSquareMag(&dir4)) {
                PSVECNormalize(&dir4, &dir4);
            }
            if (PSVECDotProduct(dir, &dir4) > 0.8f) {
                return items[m];
            }
        }
    }
    return g_thingManager_DARYL.looker;
}


/* A data pack's memory [start, end) is going away: release every thing whose
 * data points into it (each kind's *_Invalidate decides). */
void ThingManager_Invalidate(u32 start, u32 end) {
    typedef struct SoundSeqThing {
        Thing base;
        u8 padAC[0xD0 - 0xAC];
        s32 sources[4];         /* 0xD0 SoundSource ids, -1: none */
    } SoundSeqThing;
    u32 i;
    Thing *thing;
    SoundSeqThing *seq;
    u32 j;

/* Releases the pool's things in use that `invalidate` says refer to the pack. */
#define INVALIDATE_POOL(pool, count, invalidate)                                         \
    for (i = 0; i < (count); i++) {                                                      \
        thing = &THING_POOLS->pool[i].thing;                                             \
        if (!(thing->flags & THING_FLAG_FREE) && (s8)invalidate) {                        \
            THING_VFN(thing, THING_V_RELEASE, void (*)(Thing *))(thing);                \
        }                                                                                \
    }

    if (GETP(g_thingManager_DARYL.activeList) == NULL) {
        return;
    }
    INVALIDATE_POOL(projectiles, 140, Projectile_Invalidate(thing, start, end));
    INVALIDATE_POOL(eventSeqs, 192, EventSeq_Invalidate(thing, start, end));
    INVALIDATE_POOL(emitters, 160, Emitter_Invalidate(thing, start, end));
    INVALIDATE_POOL(destructibles, 500, Destructible_Invalidate((Destructible *)thing, start, end));
    INVALIDATE_POOL(items, 96, Item_Invalidate((ItemThing *)thing, start, end));
    INVALIDATE_POOL(attaches, 192, AttachThing_Invalidate(thing, start, end));
    INVALIDATE_POOL(creature, 1, Creature_Invalidate((Creature *)thing, start, end));
    INVALIDATE_POOL(wildCardRacks, 16, WildCardRack_Invalidate((u8 *)thing, start, end));
    INVALIDATE_POOL(cars, 8, CarA_Invalidate(thing, start, end));
    INVALIDATE_POOL(physicalObjects, 100, Destructible_Invalidate((Destructible *)thing, start, end));
    INVALIDATE_POOL(tracers, 32, Tracer_Invalidate(thing, start, end));
    INVALIDATE_POOL(pod, 1, Pod_Invalidate(HND(thing), start, end));
    INVALIDATE_POOL(drone, 1, Destructible_Invalidate((Destructible *)thing, start, end));
    INVALIDATE_POOL(motions, 56, Motion_Invalidate(thing, start, end));
#undef INVALIDATE_POOL
    for (i = 0; i < 128; i++) {
        seq = (SoundSeqThing *)&THING_POOLS->soundSeqs[i].thing;
        if (!(seq->base.flags & THING_FLAG_FREE) && (s8)SoundSeq_Invalidate(seq, start, end)) {
            for (j = 0; j < 4; j++) {
                if (seq->sources[j] >= 0) {
                    SoundSource_Stop(seq->sources[j]);
                }
                seq->sources[j] = -1;
            }
            Thing_Release(seq);
        }
    }
    ThingManagerList_RemoveReleasedThings(&g_thingManager_DARYL.activeList);
}


/* params: { count, TileCar handles, type, amount, saved[count] }: type 0x2A
 * fires when a car lost more than `amount` health since the last check
 * (saved), 0x2B when its health is below max / amount. */
s8 TriggerVehicleDamage(u32 *params) {
    u32 i;
    u32 count;
    u32 *things; /* TileCar handles */
    s32 *saved;
    u32 type;
    s32 amount;
    s32 result;

    count = params[0];
    things = &params[1];
    result = 0;
    saved = (s32 *)&params[count + 3];
    type = params[count + 1];
    amount = params[count + 2];
    for (i = 0; i < count; i++) {
        CarA *vehicle = GETP(((TileCar *)PTR(*things))->car);
        if (type == 0x2A) {
            s32 damage = ((Thing *)vehicle)->health;
            if (damage < *saved - amount) {
                result = 1;
            }
            *saved = damage;
        }
        if (type == 0x2B && ((Thing *)vehicle)->health < GETP(GETP(vehicle->vehicleIface)->collider)->maxHealth / amount) {
            result = 1;
        }
        things++;
        saved++;
    }
    return result;
}

/* params: { unused, gear }: the camera's player car is in that gear. */
s32 TriggerPlayerVehicleGear(u32 *params) {
    extern u8 g_cameraManager[];
    CarA *vehicle = GETP(GETP(((Camera *)g_cameraManager)->player)->vehicle);

    if (vehicle != NULL && vehicle->curveTableIdx == params[1]) {
        return 1;
    }
    return 0;
}

/* params: { unused, time, inAir }: true when the camera's player car has
 * been airborne for `time` (then inAir = 1), and again when it lands
 * (inAir = 0; Trigger_Heartbeat then runs the else-actions). */
s32 TriggerPlayerVehicleInAir(u32 *params) {
    extern u8 g_cameraManager[];
    CarA *vehicle;
    u32 inAir;

    if (GETP(((Camera *)g_cameraManager)->player) != NULL) {
        vehicle = GETP(GETP(((Camera *)g_cameraManager)->player)->vehicle);
    } else {
        vehicle = NULL;
    }
    inAir = params[2];
    if (inAir == 0 && vehicle != NULL && vehicle->unk460 >= (s32)params[1]) {
        params[2] = 1;
        return 1;
    }
    if (inAir != 0 && vehicle != NULL && vehicle->unk460 == 0) {
        params[2] = 0;
        return 1;
    }
    return 0;
}
