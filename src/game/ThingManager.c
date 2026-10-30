#include "common.h"


void ThingManagerList_Append(void **arg0, void *arg1) {
    SLOT_STORE((u8 *)(arg1) + 0xA4, SLOT_LOAD(void *, arg0));
    M2C_FIELD(arg1, s32 *, 0xA0) = 0;
    if (SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4) != NULL) {
        SLOT_STORE((u8 *)(SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4)) + 0xA0, arg1);
    }
    SLOT_STORE(arg0, arg1);
}

/* The fields every pooled Thing shares. */
typedef struct ThingHeader {
    u8 pad0[0x40];
    f32 unk40[4];               /* 0x40 bounding sphere centre */
    u8 pad50[0x20];
    f32 unk70;                  /* 0x70 bounding sphere radius */
    u32 flags;                  /* 0x74 */
    u8 pad78[0xA];
    u8 slaveId;                 /* 0x82 trigger id of a slave */
    u8 slavePlayer;             /* 0x83 */
    u8 pad84[4];
    u32 unk88;                  /* 0x88 */
    PTR32(void) vtable;               /* 0x8C */
    u8 pad90[0x10];
    PTR32(struct ThingHeader) prev;   /* 0xA0 */
    PTR32(struct ThingHeader) next;   /* 0xA4 */
    u32 type;                   /* 0xA8 index into the free lists */
} ThingHeader;

typedef struct ThingSlotProjectile { ThingHeader thing; u8 padAC[0x84]; s32 unk130; u8 pad134[0x2C]; } ThingSlotProjectile;
typedef struct ThingSlotD0 { ThingHeader thing; u8 pad[0xD0 - sizeof(ThingHeader)]; } ThingSlotD0;
typedef struct ThingSlotE0 { ThingHeader thing; u8 pad[0xE0 - sizeof(ThingHeader)]; } ThingSlotE0;
typedef struct ThingSlotF0 { ThingHeader thing; u8 pad[0xF0 - sizeof(ThingHeader)]; } ThingSlotF0;
typedef struct ThingSlot100 { ThingHeader thing; u8 pad[0x100 - sizeof(ThingHeader)]; } ThingSlot100;
typedef struct ThingSlot110 { ThingHeader thing; u8 pad[0x110 - sizeof(ThingHeader)]; } ThingSlot110;
typedef struct ThingSlot120 { ThingHeader thing; u8 pad[0x120 - sizeof(ThingHeader)]; } ThingSlot120;
typedef struct ThingSlot140 { ThingHeader thing; u8 pad[0x140 - sizeof(ThingHeader)]; } ThingSlot140;
typedef struct ThingSlot160 { ThingHeader thing; u8 pad[0x160 - sizeof(ThingHeader)]; } ThingSlot160;
typedef struct ThingSlot1D0 { ThingHeader thing; u8 pad[0x1D0 - sizeof(ThingHeader)]; } ThingSlot1D0;
typedef struct ThingSlot1F0 { ThingHeader thing; u8 pad[0x1F0 - sizeof(ThingHeader)]; } ThingSlot1F0;
typedef struct ThingSlot480 { ThingHeader thing; u8 pad[0x480 - sizeof(ThingHeader)]; } ThingSlot480;
typedef struct ThingSlot5A0 { ThingHeader thing; u8 pad[0x5A0 - sizeof(ThingHeader)]; } ThingSlot5A0;

/* g_thingManager_DARYL's thing pools, one per free list type. */
typedef struct ThingPools {
    ThingSlotProjectile projectiles[140]; /* 0x00000 type 0 */
    ThingSlot100 eventSeqs[192];        /* 0x0C080 type 1 */
    ThingSlotD0 emitters[160];          /* 0x18080 type 2 */
    ThingSlotF0 destructibles[500];     /* 0x20280 type 3 */
    ThingSlotF0 items[96];              /* 0x3D740 type 4 */
    ThingSlot160 attaches[192];         /* 0x43140 type 5 */
    ThingSlot100 debris[128];           /* 0x53940 type 6 */
    ThingSlot1D0 creature[1];           /* 0x5B940 type 7 */
    ThingSlot480 wildCardRacks[16];     /* 0x5BB10 type 8 */
    ThingSlot5A0 cars[8];               /* 0x60310 type 9 */
    ThingSlot140 physicalObjects[100];  /* 0x63010 type 10 */
    ThingSlotD0 looker[1];              /* 0x6AD10 type 11 */
    ThingSlot110 tracers[32];           /* 0x6ADE0 type 12 */
    ThingSlot120 pod[1];                /* 0x6CFE0 type 13 */
    ThingSlot1F0 drone[1];              /* 0x6D100 type 14 */
    ThingSlotF0 motions[56];            /* 0x6D2F0 type 15 */
    ThingSlotE0 soundSeqs[128];         /* 0x70770 type 16 */
    ThingSlotD0 radarBlips[64];         /* 0x77770 type 17 */
    PTR32(void) activeList;                   /* 0x7AB70 */
    PTR32(void) freeList[19];                 /* 0x7AB74 */
} ThingPools;

static inline void ThingManager_InitFreeThing(ThingHeader *thing, void *vtable, u32 type) {
    void **freeList;
    ThingHeader *head;

    SETP(thing->vtable, vtable);
    thing->unk88 = 0;
    thing->flags |= 0x1000;
    SETP(thing->prev, NULL);
    SETP(thing->next, NULL);
    thing->type = type;
    freeList = g_thingManager_DARYL.freeList + thing->type;
    SETP(thing->next, *freeList);
    SETP(thing->prev, NULL);
    head = GETP(thing->next);
    if (head != NULL) {
        SETP(head->prev, thing);
    }
    *freeList = thing;
}

static inline void ThingManager_InitPool(void *pool, u32 count, u32 size, void *vtable, u32 type) {
    u32 i;

    for (i = 0; i < count; i++) {
        ThingManager_InitFreeThing((ThingHeader *)((u8 *)pool + i * size), vtable, type);
    }
}

void ThingManagerList_Remove(void **arg0, void *arg1) {
    void *temp_r5;
    void *temp_r5_2;

    temp_r5 = SLOT_LOAD(void *, (u8 *)(arg1) + 0xA0);
    if (temp_r5 == NULL) {
        SLOT_STORE(arg0, SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4));
    } else {
        SLOT_STORE((u8 *)(temp_r5) + 0xA4, (void *) SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4));
    }
    temp_r5_2 = SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4);
    if (temp_r5_2 != NULL) {
        SLOT_STORE((u8 *)(temp_r5_2) + 0xA0, (void *) SLOT_LOAD(void *, (u8 *)(arg1) + 0xA0));
        SLOT_STORE((u8 *)(arg1) + 0xA4, NULL);
    }
    SLOT_STORE((u8 *)(arg1) + 0xA0, NULL);
}

void *ThingManagerList_GetByType(void *list, s32 type) {
    void *thing;

    for (thing = SLOT_LOAD(void *, list); thing != NULL; thing = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) {
        if (M2C_FIELD(thing, s32 *, 0xA8) == type) {
            return thing;
        }
    }
    return NULL;
}

s32 ThingManagerList_Get(void *list, void *target) {
    void *thing;

    for (thing = SLOT_LOAD(void *, list); thing != NULL; thing = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) {
        if (target == thing) {
            return 1;
        }
    }
    return 0;
}

void ThingManagerList_ReleaseSlavesBySlaveType(void **list, u8 type) {
    void *thing;
    void *next;
    void **freeList;
    void *head;

    thing = SLOT_LOAD(void *, list);
    while (thing != NULL) {
        next = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
        if (M2C_FIELD(thing, u8 *, 0x83) == type) {
            ((void (*)(void *))SLOT_LOAD(void *, (u8 *)PORT_ADDR(SLOT_LOAD(void *, (u8 *)(thing) + 0x8C)) + 4))(thing);
            ThingManagerList_Remove(list, thing);
            M2C_FIELD(thing, u32 *, 0x74) &= ~0x200000;
            M2C_FIELD(thing, u32 *, 0x74) |= 0x1000;
            freeList = &g_thingManager_DARYL.freeList[M2C_FIELD(thing, s32 *, 0xA8)];
            SLOT_STORE((u8 *)(thing) + 0xA4, *freeList);
            SLOT_STORE((u8 *)(thing) + 0xA0, NULL);
            head = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
            if (head != NULL) {
                SLOT_STORE((u8 *)(head) + 0xA0, thing);
            }
            *freeList = thing;
        }
        thing = next;
    }
}

void ThingManagerList_RemoveReleasedThings(void **list) {
    ThingHeader *thing;
    ThingHeader *next;

    for (thing = SLOT_LOAD(void *, list); thing != NULL; thing = next) {
        next = GETP(thing->next);
        if (thing->flags & 0x200000) {
            ThingManagerList_ReleaseAndRemoveAll(&thing->unk88);
            ThingManagerList_Remove(list, thing);
            ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
            thing->flags &= ~0x200000;
            thing->flags |= 0x1000;
        }
    }
}

void ThingManagerList_ReleaseAndRemoveAll(void **list) {
    ThingHeader *thing;
    ThingHeader *next;

    ThingManagerList_ReleaseAll(list);
    for (thing = SLOT_LOAD(void *, list); thing != NULL; thing = next) {
        next = GETP(thing->next);
        if ((thing->flags & 0x200000) != 0) {
            void **children;
            ThingManagerList_ReleaseAndRemoveAll(children = (void **)&thing->unk88);
            ThingManagerList_Remove(list, thing);
            ThingManagerList_Append(g_thingManager_DARYL.freeList + thing->type, thing);
            thing->flags &= ~0x200000;
            thing->flags |= 0x1000;
        }
    }
}

void ThingManagerList_ReleaseAll(void **list) {
    void *thing;
    void *next;

    thing = SLOT_LOAD(void *, list);
    while (thing != NULL) {
        next = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
        ((void (*)(void *))SLOT_LOAD(void *, (u8 *)PORT_ADDR(SLOT_LOAD(void *, (u8 *)(thing) + 0x8C)) + 4))(thing);
        thing = next;
    }
}

void ThingManagerList_EmancipateSlaves(void *mtx, void **list) {
    typedef struct SlaveThing {
        u8 pad0[0x10];
        Matrix4 mtx;
        u8 pad50[0x24];
        u32 flags;
        u8 pad78[8];
        s16 gridCell;
        u8 slaveId;
        u8 slavePlayer;
        u8 pad84[8];
        PTR32(void) vtable;
        u8 pad90[0x10];
        PTR32(struct SlaveThing) prev;
        PTR32(struct SlaveThing) next;
    } SlaveThing;
    extern u8 g_itemVTable[];
    Matrix4 out;
    SlaveThing *thing;
    SlaveThing *next;

    thing = SLOT_LOAD(void *, list);
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->slavePlayer == 0xEF) {
            if (GETP(thing->prev) == NULL) {
                SLOT_STORE(list, GETP(thing->next));
            } else {
                SETP(GETP(thing->prev)->next, GETP(thing->next));
            }
            if (GETP(thing->next) != NULL) {
                SETP(GETP(thing->next)->prev, GETP(thing->prev));
                SETP(thing->next, NULL);
            }
            SETP(thing->prev, NULL);
            MultiplyMatrix(&thing->mtx, mtx, &out);
            thing->mtx = out;
            SETP(thing->next, GETP(g_thingManager_DARYL.activeList));
            SETP(thing->prev, NULL);
            if (GETP(thing->next) != NULL) {
                SETP(GETP(thing->next)->prev, thing);
            }
            SETP(g_thingManager_DARYL.activeList, thing);
            if (thing->gridCell != 0 && (u8 *)GETP(thing->vtable) == g_itemVTable) {
                Thing_AddToGrid(thing, 3);
                thing->flags &= ~8;
            }
        }
        thing = next;
    }
}

void SharedSlaveHolder_Release(void) {
    extern u8 g_aSharedSlaveHolder[0x200];
    void *thing;
    void *next;
    SharedSlaveHolder *holder;
    u32 i;

    holder = (SharedSlaveHolder *)g_aSharedSlaveHolder;
    i = 0;
    do {
        if (GETP(holder->key) != NULL) {
            for (thing = GETP(holder->slaves); thing != NULL; thing = next) {
                next = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
                ((void (*)(void *))SLOT_LOAD(void *, SLOT_LOAD(u32 *, (u8 *)PORT_ADDR(thing) + 0x8C) + (1)))(thing);
            }
            ThingManagerList_RemoveReleasedThings(PTR((s32)HND(&holder->slaves)));
            SETP(holder->key, NULL);
        }
        i++;
        holder++;
    } while (i < 64);
}

s32 SharedSlaveHolder_Get(void *shared) {
    extern u8 g_aSharedSlaveHolder[0x200];
    s32 i;

    for (i = 0; i < 64; i++) {
        if (GETP(((SharedSlaveHolder *)g_aSharedSlaveHolder)[i].key) == shared) {
            return i;
        }
    }
    for (i = 0; i < 64; i++) {
        if (GETP(((SharedSlaveHolder *)g_aSharedSlaveHolder)[i].key) == NULL) {
            SETP(((SharedSlaveHolder *)g_aSharedSlaveHolder)[i].key, shared);
            ThingManagerList_InitAllSlavesNow(&((SharedSlaveHolder *)g_aSharedSlaveHolder)[i].slaves, (u8 *)shared + 4, 0xFF);
            return i;
        }
    }
    return 64;
}

void Thing_DrawSharedSlaves(u8 *thing, u32 *triggers) {
    typedef struct { u8 pad[0x10]; u8 type; u8 pad2[7]; u8 *shared; u8 pad3[4]; } Trigger;
    typedef struct SlaveThing {
        u8 pad0[0x10];
        Matrix4 mtx;
        Quaternion quat;
        u8 pad60[0x2C];
        void (**vtbl)(struct SlaveThing *);
        u8 pad90[0x14];
        PTR32(struct SlaveThing) next;
    } SlaveThing;
    extern u8 g_aSharedSlaveHolder[0x200];
    u32 i;
    SlaveThing *slave;
    u8 *shared;
    Quaternion quat __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));

    for (i = 0; i < triggers[0]; i++) {
        Trigger *trigger = &((Trigger *)PTR(triggers[1]))[i];
        if (trigger->type == 10) {
            shared = trigger->shared;
            slave = SLOT_LOAD(SlaveThing *, (g_aSharedSlaveHolder + (*(SLOT_LOAD(u8 *, (shared + 8)) + 0x1D)) * 8 + 4));
            if (*(u32 *)shared != 0) {
                for (; slave != NULL; slave = GETP(slave->next)) {
                    quat = slave->quat;
                    pos = slave->mtx.v[3];
                    QuaternionMult((Quaternion *)(thing + 0x50), &quat, &slave->quat);
                    XformVec((Matrix4 *)(thing + 0x10), &pos, &slave->mtx.v[3]);
                    slave->vtbl[4](slave);
                    slave->quat = quat;
                    slave->mtx.v[3] = pos;
                }
            }
        }
    }
}

struct SlaveTrigger {
        Vector4 pos;
        u8 type;
        u8 pad11;
        u16 id;
        u8 pad14[4];
        PTR32(void) data;
    };
void ThingManagerList_TriggerSlaves(ThingHeader **list, struct SlaveTrigger *trigger, Vector4 *a, Vector4 *b, Vector4 *c, u32 player);

void ThingManagerList_TriggerSlaves(ThingHeader **list, struct SlaveTrigger *trigger, Vector4 *a, Vector4 *b, Vector4 *c, u32 player) {
    typedef struct SharedSlaves {
        u32 active;
        u8 pad4[4];
        PTR32(u8) info;
    } SharedSlaves;
    extern Vector4 g_zeroVector;
    extern void *g_pSourcePlayer;
    struct { Vector4 a; Vector4 b; Vector4 c; Vector4 zero; void *data; void *player; f32 f; } p1 __attribute__((aligned(16)));
    struct { Vector4 a; Vector4 b; Vector4 c; void *data; void *player; f32 f; } p2 __attribute__((aligned(16)));
    struct { PTR32(void) data; u8 pad4[0xC]; Vector4 pos; s32 flag; void *player; } p5 __attribute__((aligned(16)));
    struct { PTR32(void) data; u8 pad4[0xC]; Vector4 pos; s32 flag; void *player; } p6 __attribute__((aligned(16)));
    struct { Vector4 a; Vector4 b; Vector4 c; void *data; s32 time; void *player; f32 f; } p7 __attribute__((aligned(16)));
    struct { Vector4 unk; Vector4 a; Vector4 b; void *data; s32 flag; } p8 __attribute__((aligned(16)));
    struct { Vector4 a; void *data; u8 flag; } p3 __attribute__((aligned(16)));
    struct { PTR32(void) data; u8 pad4[0xC]; Vector4 a; } p9 __attribute__((aligned(16)));
    Vector4 p4 __attribute__((aligned(16)));
    u8 id;
    u8 pl;
    SharedSlaves *shared;
    ThingHeader *slave = NULL;
    void *data;
    ThingHeader *thing;

    switch (trigger->type) {
    case 1:
        p1.data = GETP(trigger->data);
        p1.c = *c;
        p1.player = g_pSourcePlayer;
        p1.zero = g_zeroVector;
        if (p1.data != NULL && *(u32 *)p1.data != 0) {
            p1.player = g_pSourcePlayer;
            p1.f = 0.0f;
            p1.a = *a;
            p1.b = *b;
            slave = ThingManager_DARYL_CreateSlave(1, &p1, trigger->id);
        }
        break;
    case 2:
        if (GETP(trigger->data) != NULL) {
            p2.data = GETP(trigger->data);
            p2.f = 0.0f;
            p2.c = *c;
            p2.player = g_pSourcePlayer;
            p2.a = *a;
            p2.a.w = 1.0f;
            p2.b = *b;
            slave = ThingManager_DARYL_CreateSlave(2, &p2, trigger->id);
        }
        break;
    case 3:
        data = GETP(trigger->data);
        if (data != NULL && data != NULL) {
            p3.a = *a;
            p3.data = data;
            p3.flag = 1;
            slave = ThingManager_DARYL_CreateSlave(0x10, &p3, trigger->id);
        }
        break;
    case 4:
        p4 = trigger->pos;
        p4.w = 1.0f;
        slave = ThingManager_DARYL_CreateSlave(8, &p4, trigger->id);
        break;
    case 5:
        if (GETP(trigger->data) != NULL) {
            SETP(p5.data, GETP(trigger->data));
            p5.pos = trigger->pos;
            p5.flag = 1;
            p5.player = g_pSourcePlayer;
            slave = ThingManager_DARYL_CreateSlave(5, &p5, trigger->id);
        }
        break;
    case 6:
        if (ViewManager_GetViewFormat() > 1 && *(u8 *)GETP(trigger->data) != 0) {
            break;
        }
        if (GETP(trigger->data) != NULL) {
            SETP(p6.data, GETP(trigger->data));
            p6.player = g_pSourcePlayer;
            p6.pos = trigger->pos;
            p6.flag = 0;
            slave = ThingManager_DARYL_CreateSlave(5, &p6, trigger->id);
        }
        break;
    case 7:
        if (GETP(trigger->data) != NULL) {
            p7.data = GETP(trigger->data);
            p7.f = 0.0f;
            p7.time = 20;
            p7.c = *c;
            p7.player = g_pSourcePlayer;
            p7.a = *a;
            p7.a.w = 1.0f;
            p7.b = *b;
            slave = ThingManager_DARYL_CreateSlave(0xC, &p7, trigger->id);
        }
        break;
    case 8:
        data = GETP(trigger->data);
        if (data != NULL) {
            p8.a = *a;
            p8.b = *b;
            p8.data = data;
            p8.flag = 0;
            slave = ThingManager_DARYL_CreateSlave(0xF, &p8, trigger->id);
        }
        break;
    case 9:
        data = GETP(trigger->data);
        if (data != NULL) {
            p9.a = *a;
            SETP(p9.data, data);
            slave = ThingManager_DARYL_CreateSlave(0x11, &p9, trigger->id);
        }
        break;
    case 10:
        shared = GETP(trigger->data);
        if (shared != NULL && shared->active == 0) {
            shared->active = 1;
            GETP(shared->info)[0x1D] = SharedSlaveHolder_Get(shared);
        }
        break;
    case 99:
        id = trigger->id;
        pl = player;
        for (thing = SLOT_LOAD(void *, list); thing != NULL; thing = GETP(thing->next)) {
            if (thing->slaveId == id && thing->slavePlayer == pl) {
                ((void (**)(ThingHeader *))GETP(thing->vtable))[1](thing);
            }
        }
        break;
    }

    if (slave != NULL) {
        slave->slavePlayer = player;
        SETP(slave->next, SLOT_LOAD(void *, list));
        SETP(slave->prev, NULL);
        if (GETP(slave->next) != NULL) {
            SETP(GETP(slave->next)->prev, slave);
        }
        SLOT_STORE(list, slave);
    }
}

void ThingManagerList_InitAllSlavesNow(void *list, void *slaves, u8 player) {
    extern Vector4 g_zeroVector;
    typedef struct { f32 x, y, z, w; } Vec4;
    typedef struct {
        u8 unk0[0x20];
    } SlaveTrigger;
    typedef struct {
        u32 count;
        PTR32(SlaveTrigger) triggers;
    } SlaveList;
    SlaveList *sl = slaves;
    u32 i;
    void *next;
    void *thing;
    void **freeList;

    thing = SLOT_LOAD(void *, list);
    while (thing != NULL) {
        next = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
        if (M2C_FIELD(thing, u8 *, 0x83) == player) {
            ((void (*)(void *))SLOT_LOAD(void *, (u8 *)PORT_ADDR(SLOT_LOAD(void *, (u8 *)(thing) + 0x8C)) + 4))(thing);
            ThingManagerList_Remove(list, thing);
            M2C_FIELD(thing, u32 *, 0x74) &= ~0x200000;
            M2C_FIELD(thing, u32 *, 0x74) |= 0x1000;
            freeList = g_thingManager_DARYL.freeList + M2C_FIELD(thing, u32 *, 0xA8);
            ThingManagerList_Append(freeList, thing);
        }
        thing = next;
    }
    for (i = 0; i < sl->count; i++) {
        Vec4 dir = {0.0f, 0.0f, 0.0f, 1.0f};
        SlaveTrigger *trigger = &GETP(sl->triggers)[i];

        ThingManagerList_TriggerSlaves(list, (u8 *)trigger, (Vector4 *)trigger, (Vector4 *)&dir, &g_zeroVector, player);
    }
}

#define THINGPOOLS ((ThingPools *)&g_thingManager_DARYL)
#define THINGPOOL_INIT(pool, count, vt, t)                                          \
    for (i = 0; i < (count); i++) {                                                 \
        SETP(THINGPOOLS->pool[i].thing.vtable, (vt));                                    \
        thing = &THINGPOOLS->pool[i].thing;                                         \
        THINGPOOLS->pool[i].thing.unk88 = 0;                                        \
        THINGPOOLS->pool[i].thing.flags |= 0x1000;                                  \
        SETP(THINGPOOLS->pool[i].thing.prev, NULL);                                      \
        SETP(THINGPOOLS->pool[i].thing.next, NULL);                                      \
        THINGPOOLS->pool[i].thing.type = (t);                                       \
        ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing); \
    }

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
    ThingHeader *thing;
    u32 i;

    for (i = 0; i < 18; i++) {
        SETP(THINGPOOLS->freeList[i], NULL);
    }
    SETP(THINGPOOLS->activeList, NULL);
    for (i = 0; i < 140; i++) {
        SETP(THINGPOOLS->projectiles[i].thing.vtable, g_projectileVTable);
        thing = &THINGPOOLS->projectiles[i].thing;
        THINGPOOLS->projectiles[i].unk130 = -1;
        THINGPOOLS->projectiles[i].thing.unk88 = 0;
        THINGPOOLS->projectiles[i].thing.flags |= 0x1000;
        SETP(THINGPOOLS->projectiles[i].thing.prev, NULL);
        SETP(THINGPOOLS->projectiles[i].thing.next, NULL);
        THINGPOOLS->projectiles[i].thing.type = 0;
        ThingManagerList_Append(&g_thingManager_DARYL.freeList[thing->type], thing);
    }
    THINGPOOL_INIT(eventSeqs, 192, g_eventSeqVTable, 1);
    THINGPOOL_INIT(emitters, 160, g_emitterVTable, 2);
    THINGPOOL_INIT(destructibles, 500, g_destructibleVTable, 3);
    THINGPOOL_INIT(items, 96, g_itemVTable, 4);
    THINGPOOL_INIT(attaches, 192, g_attachVTable, 5);
    THINGPOOL_INIT(debris, 128, g_debrisVTable, 6);
    THINGPOOL_INIT(creature, 1, g_creatureVTable, 7);
    THINGPOOL_INIT(wildCardRacks, 16, g_wildCardRackVTable, 8);
    THINGPOOL_INIT(cars, 8, g_carAVTable, 9);
    THINGPOOL_INIT(physicalObjects, 100, g_physicalObjectVTable, 10);
    THINGPOOL_INIT(looker, 1, g_lookerVTable, 11);
    THINGPOOL_INIT(tracers, 32, g_tracerVTable, 12);
    THINGPOOL_INIT(pod, 1, g_podVTable, 13);
    THINGPOOL_INIT(drone, 1, g_droneVTable, 14);
    THINGPOOL_INIT(motions, 56, g_motionVTable, 15);
    THINGPOOL_INIT(soundSeqs, 128, g_soundSeqVTable, 16);
    THINGPOOL_INIT(radarBlips, 64, g_radarBlipVTable, 17);
}

#undef THINGPOOL_INIT
#undef THINGPOOLS

extern u8 g_aSharedSlaveHolder[0x200];
void ThingManager_DARYL_Create(void) {
    OBoxHolder_Init();
    OConvexHullHolder_Init();
    ThingManager_DARYL_CreateMain();
    CollisionPairCache_Clear();
    CollisionTriCache_Clear();
    memset(g_aSharedSlaveHolder, 0, sizeof(g_aSharedSlaveHolder));
}

void ThingManager_DARYL_Destroy(void) {
    void *thing;
    u32 i;

    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) {
        ((void (*)(void *))SLOT_LOAD(void *, SLOT_LOAD(u32 *, (u8 *)PORT_ADDR(thing) + 0x8C) + (1)))(thing);
    }
    SETP(g_thingManager_DARYL.activeList, NULL);
    for (i = 0; i < 18; i++) {
        SETP(g_thingManager_DARYL.freeList[i], NULL);
    }
}

void ThingManager_DARYL_Init(void) {
    ThingManager_DARYL_Release();
    MatrixCache_Init();
}

/* The original calls these rather than inlining them. */


void ThingManager_DARYL_Release(void) {

    SharedSlaveHolder_Release();
    ThingManagerList_ReleaseAndRemoveAll(&g_thingManager_DARYL.activeList);
    ThingManagerList_ReleaseAndRemoveAll(&g_thingManager_DARYL.activeList);
    ThingManager_DARYL_CreateMain();
}


void ThingManager_MoveLookerOnly(void) {
    extern u8 g_soundSeqVTable[];
    void *thing;

    ((void (*)(void *))SLOT_LOAD(void *, SLOT_LOAD(u32 *, (u8 *)PORT_ADDR(&g_thingManager_DARYL) + 0x6AD9C) + (2)))(g_thingManager_DARYL.looker);
    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) {
        if (SLOT_LOAD(u8 *, (u8 *)(thing) + 0x8C) == g_soundSeqVTable && (M2C_FIELD(thing, u32 *, 0x74) & 0x100000)) {
            ((void (*)(void *))SLOT_LOAD(void *, SLOT_LOAD(u32 *, (u8 *)PORT_ADDR(thing) + 0x8C) + (3)))(thing);
        }
    }
    ThingManagerList_RemoveReleasedThings(PTR((s32)HND(&g_thingManager_DARYL.activeList)));
}

void ThingManager_DARYL_Heartbeat(void) {
    extern u8 g_bFreezeWorld;
    if ((s8) g_bFreezeWorld != 0) {
        ThingManager_MoveLookerOnly();
        return;
    }
    ThingManager_MoveThings_DARYL_SUBTICK();
}

typedef struct MovePhys {
    u8 pad0[0x14];
    f32 friction;               /* 0x14 */
    f32 elasticity;             /* 0x18 */
} MovePhys;

typedef struct MoveThing {
    Vector4 vel;                /* 0x00 */
    Matrix4 mtx;                /* 0x10 v[3] = position */
    Vector4 quat;               /* 0x50 */
    Vector4 pushVel;            /* 0x60 */
    f32 radius;                 /* 0x70 */
    u32 flags;                  /* 0x74 */
    s32 unk78;                  /* 0x78 */
    s32 damage;                 /* 0x7C */
    s16 unk80;                  /* 0x80 */
    u8 pad82[2];
    PTR32(void) gridData;             /* 0x84 */
    u8 pad88[4];
    PTR32(struct MoveThingVTable) vtable; /* 0x8C */
    u8 pad90[4];
    PTR32(void) player;               /* 0x94 */
    u8 pad98[0xC];
    PTR32(struct MoveThing) next;     /* 0xA4 */
    s32 type;                   /* 0xA8 */
} MoveThing;

typedef struct MoveHit {
    Vector4 pos;                /* 0x00 */
    Vector4 normal;             /* 0x10 */
    PTR32(MoveThing) thing;           /* 0x20 */
    PTR32(MoveThing) other;           /* 0x24 */
    PTR32(MovePhys) surface;          /* 0x28 */
    u8 pad2C[0x14];
    u8 type;                    /* 0x40 0xFF: pushed out, 0xFE: embedded */
    u8 pad41[0xF];
} MoveHit;

typedef struct MoveThingVTable {
    PTR32(void) fn0;
    PTR32(void) fn4;
    FPTR32(void, (MoveThing *)) move;                          /* 0x08 */
    FPTR32(void, (MoveThing *)) update;                        /* 0x0C */
    PTR32(void) fn10;
    FPTR32(f32, (MoveThing *)) getMass;                        /* 0x14 */
    FPTR32(void, (MoveThing *, MoveHit *, s32, f32)) collide;  /* 0x18 */
    PTR32(void) fn1C;
    PTR32(void) fn20;
    PTR32(void) fn24;
    FPTR32(MovePhys *, (MoveThing *)) getPhys;                  /* 0x28 */
    PTR32(void) fn2C;
    FPTR32(void, (MoveThing *, Vector4 *)) getInvInertia;      /* 0x30 */
    FPTR32(void, (MoveThing *, f32)) postMove;                 /* 0x34 */
} MoveThingVTable;

typedef struct MoveGridEntry {
    u32 unk0;
    PTR32(MoveThing) thing;
} MoveGridEntry;

typedef struct MoveCarTuning {
    u8 pad0[0x38];
    f32 maxImpactVel;           /* 0x38 */
    u8 pad3C[0x20];
    f32 damageScale;            /* 0x5C */
} MoveCarTuning;

typedef struct MoveCarBody {
    u8 pad0[0x18];
    f32 bumperHeight;           /* 0x18 */
} MoveCarBody;

typedef struct MoveCarIface {
    u8 pad0[0x80];
    PTR32(MoveCarTuning) tuning;      /* 0x80 */
    u8 pad84[0x1C];
    struct { u8 pad0[0x44]; MoveCarBody *body; } *model; /* 0xA0 */
} MoveCarIface;

typedef struct MoveCar {
    MoveThing thing;
    u8 padAC[0x3BC - 0xAC];
    PTR32(MoveCarIface) iface;        /* 0x3BC */
    u8 pad3C0[0x50C - 0x3C0];
    f32 drag;                   /* 0x50C */
    u8 pad510[0x18];
    f32 rideHeight;             /* 0x528 */
} MoveCar;

typedef struct MovePlayer {
    u8 pad0[0xD0];
    struct { u8 pad0[0x1C]; f32 damageScale; } *difficulty; /* 0xD0 */
} MovePlayer;

typedef struct MoveImpactFx {
    u8 pad0[0x68];
    f32 minVel;                 /* 0x68 */
    f32 maxVel;                 /* 0x6C */
    u8 pad70[0x80];
    u32 count;                  /* 0xF0 */
    PTR32(u32) ids;                   /* 0xF4 */
} MoveImpactFx;

extern MoveHit g_aCI[8];

static inline u32 CI_Find(MoveThing *a, MoveThing *b) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if ((GETP(g_aCI[i].thing) == a && GETP(g_aCI[i].other) == b) || (GETP(g_aCI[i].other) == a && GETP(g_aCI[i].thing) == b)) {
            return TRUE;
        }
    }
    return FALSE;
}

static inline void CI_Add(MoveThing *a, MoveThing *b) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (GETP(g_aCI[i].thing) == NULL) {
            SETP(g_aCI[i].thing, a);
            SETP(g_aCI[i].other, b);
            return;
        }
    }
    Dprintf("out of CI slots\n");
}

void ThingManager_MoveThings_DARYL_SUBTICK(void)
{
    extern f32 g_fDeltaTimeSec;
    extern u32 g_frameCounter;
    extern u8 g_carAVTable[];
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    static f32 offset = 0.2f;
    static f32 mult = 10.0f;
    MoveThing *moved[0x200];
    MoveGridEntry objs[0x200];
    MoveHit hit;
    MoveHit rayHit;
    FxParams fx;
    Matrix4 invInertia;
    Matrix4 rot;
    Matrix4 inertia;
    Matrix4 tmp;
    Vector4 push;
    Vector4 min;
    Vector4 max;
    f32 dt;
    f32 dist;
    MoveThing *thing;
    MoveThing *t;
    MoveThing *other;
    MoveThing *source;
    MoveThing *otherSource;
    SharedSlaveHolder *holder;
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

    stuck = 0;
    iters = 0;
    elapsed = 0.0f;
    numMoved = 0;
    MemClr32(g_aCI, sizeof(g_aCI));
    CollisionPairCache_Heartbeat();
    CollisionTriCache_Heartbeat();
    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
        if (!(thing->flags & 0x280008)) {
            GETP(GETP(thing->vtable)->move)(thing);
        }
        thing->flags &= ~0x1A040;
        if (thing->unk80 == 0) {
            continue;
        }
        if (!(thing->flags & 4) || (thing->flags & 0x200000)) {
            continue;
        }
        moved[numMoved++] = thing;
        if (GETP(thing->gridData) == NULL || (thing->flags & 8)) {
            continue;
        }
        Thing_DynamicGridMove(thing, g_fDeltaTimeSec);
    }

    do {
        SETP(hit.thing, NULL);
        SETP(hit.other, NULL);
        dt = dtStep = g_fDeltaTimeSec - elapsed;
        SETP(hit.surface, NULL);
        for (i = 0; i < numMoved; i++) {
            t = moved[i];
            dtSave = dt;
            source = PTR(Thing_GetSource(t));
            t->flags |= 0x8000;
            if (t->flags & 0x40) {
                continue;
            }
            if (t->flags & 0x200000) {
                continue;
            }
            hit.type = 0;
            if (dt && !(t->flags & 0x24000B) && Thing_HitsTerrain(t, &dt, &hit)) {
                if (hit.type == 0xFF) {
                    if (PSVECDotProduct(t, &hit.normal) < 0.0f) {
                        f32 d;

                        depth = 0.0f;
                        d = PSVECDotProduct(t, &hit.normal);
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
                    if (Distance_Plane(&hit.normal, &hit) < -offset) {
                        dist = 1.5f;
                        rayHit.type = 0;
                        offset = 0.3f;
                        if (RayHitsTerrain(&t->mtx.v[3], &hit, &dist, &rayHit)) {
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
                        t->mtx.v[3].x += hit.normal.x * offset;
                        t->mtx.v[3].y += hit.normal.y * offset;
                        t->mtx.v[3].z += hit.normal.z * offset;
                        t->flags |= 0x80;
                        hit.type = 0;
                    }
                    SETP(hit.thing, t);
                    SETP(hit.other, NULL);
                } else {
                    SETP(hit.thing, t);
                    SETP(hit.other, NULL);
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
                    other = GETP(objs[j].thing);
                    dtSave = dt;
                    otherSource = PTR(Thing_GetSource(other));
                    if (other->flags & 0x8000) {
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
                    if ((other->flags & 8) && (t->flags & 8)) {
                        continue;
                    }
                    if (CI_Find(t, other)) {
                        continue;
                    }
                    if (!Thing_TestThingCollision(t, other, &dt, &hit)) {
                        continue;
                    }
                    if (hit.type == 0xFF && GETP(GETP(t->vtable)->getPhys)(t)->elasticity > 0.0f &&
                        GETP(GETP(other->vtable)->getPhys)(other)->elasticity > 0.0f && t->unk78 > 0 && other->unk78 > 0) {
                        f32 pushA = 0.0f;
                        f32 pushB = 0.0f;
                        f32 dA = PSVECDotProduct(t, &hit.normal);
                        f32 dB = PSVECDotProduct(other, &hit.normal);

                        if (dA - 5.5f <= 0.0f) {
                            pushA = -(dA - 5.5f);
                        }
                        if (dB + 5.5f >= 0.0f) {
                            pushB = dB + 5.5f;
                        }
                        t->flags |= 0x10000;
                        other->flags |= 0x10000;
                        if (t->flags & 0x4002008) {
                            pushB += pushA;
                            t->flags &= ~0x10000;
                            pushA = 0.0f;
                        } else if (other->flags & 0x4002008) {
                            pushA += pushB;
                            other->flags &= ~0x10000;
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
                        SETP(hit.thing, t);
                        SETP(hit.other, other);
                        SETP(hit.surface, NULL);
                    }
                }
            }
        }

        if (dt <= 0.0f) {
            stuck++;
            dt = 0.0f;
            if (stuck > 10) {
                if (!(GETP(hit.thing)->flags & 0x4000008)) {
                    GETP(hit.thing)->vel.x = 0.0f;
                    GETP(hit.thing)->vel.y = 0.0f;
                    GETP(hit.thing)->vel.z = 0.0f;
                    GETP(hit.thing)->flags |= 0x40;
                }
                if (GETP(hit.other) != NULL && !(GETP(hit.other)->flags & 0x4000008)) {
                    GETP(hit.other)->vel.x = 0.0f;
                    GETP(hit.other)->vel.y = 0.0f;
                    GETP(hit.other)->vel.z = 0.0f;
                    GETP(hit.other)->flags |= 0x40;
                }
            }
        } else {
            stuck = 0;
            for (i = 0; i < numMoved; i++) {
                t = moved[i];
                if (!(t->flags & 8)) {
                    t->mtx.v[3].x += t->vel.x * dt;
                    t->mtx.v[3].y += t->vel.y * dt;
                    t->mtx.v[3].z += t->vel.z * dt;
                    t->flags |= 0x80;
                }
            }
        }

        if (GETP(hit.thing) != NULL) {
            f32 vA = PSVECDotProduct(&hit.normal, GETP(hit.thing));
            f32 vB = 0.0f;
            f32 invMassA = 0.0f;
            f32 invMassB = 0.0f;
            f32 elastA = GETP(GETP(GETP(hit.thing)->vtable)->getPhys)(GETP(hit.thing))->elasticity;
            s32 damageB = 0;
            s32 damageA = GETP(hit.thing)->damage;
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

            if (GETP(hit.other) == NULL && GETP(GETP(hit.thing)->vtable) == (MoveThingVTable *)g_carAVTable &&
                PSVECDotProduct(&GETP(hit.thing)->mtx.v[1], &hit.normal) < 0.5f) {
                elastA *= CarA_GetImpactMultiplier(GETP(hit.thing), &hit, 1.0f);
                if (GETP(hit.thing)->mtx.v[1].y < 0.0f) {
                    f32 blend = 0.6f * GETP(hit.surface)->friction * GETP(GETP(GETP(hit.thing)->vtable)->getPhys)(GETP(hit.thing))->friction;

                    negVel.x = -GETP(hit.thing)->vel.x;
                    negVel.y = -GETP(hit.thing)->vel.y;
                    negVel.z = -GETP(hit.thing)->vel.z;
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
            if (!(GETP(hit.thing)->flags & 0x4000008) || GETP(hit.thing)->unk78 == 0) {
                invMassA = 1.0f / GETP(GETP(GETP(hit.thing)->vtable)->getMass)(GETP(hit.thing));
            }
            if (GETP(GETP(hit.thing)->vtable) == (MoveThingVTable *)g_carAVTable &&
                (GETP(hit.other) == NULL || (GETP(hit.other)->unk78 != 0 && (GETP(hit.other)->flags & 8) &&
                                       GETP(GETP(GETP(hit.other)->vtable)->getPhys)(GETP(hit.other))->elasticity > 0.0f))) {
                MoveCar *car = (MoveCar *)GETP(hit.thing);
                f32 lim = GETP(GETP(car->iface)->tuning)->maxImpactVel * (0.7f * CarA_GetImpactMultiplier(car, &hit, 1.0f) + 0.3f);

                if (vA > lim) {
                    f32 d = lim - vA;

                    vA = lim;
                    GETP(hit.thing)->vel.x += hit.normal.x * d;
                    GETP(hit.thing)->vel.y += hit.normal.y * d;
                    GETP(hit.thing)->vel.z += hit.normal.z * d;
                } else if (vA < -lim) {
                    f32 d = -lim - vA;

                    vA = -lim;
                    GETP(hit.thing)->vel.x += hit.normal.x * d;
                    GETP(hit.thing)->vel.y += hit.normal.y * d;
                    GETP(hit.thing)->vel.z += hit.normal.z * d;
                }
            }
            PSVECSubtract(&hit.pos, &GETP(hit.thing)->mtx.v[3], &rA);
            PSVECCrossProduct(&rA, &hit.normal, &rxnA);
            GETP(GETP(GETP(hit.thing)->vtable)->getInvInertia)(GETP(hit.thing), &diagA);
            InverseXformVecOrientation(&GETP(hit.thing)->mtx, &rxnA, &localA);
            scaledA.x = diagA.x * localA.x;
            scaledA.y = diagA.y * localA.y;
            scaledA.z = diagA.z * localA.z;
            XformVecOrientation(&GETP(hit.thing)->mtx, &scaledA, &wA);
            PSVECCrossProduct(&wA, &rA, &crossA);
            if (GETP(hit.other) != NULL) {
                vB = PSVECDotProduct(&hit.normal, GETP(hit.other));
                elastB = GETP(GETP(GETP(hit.other)->vtable)->getPhys)(GETP(hit.other))->elasticity;
                if (!(GETP(hit.other)->flags & 0x4000008) || GETP(hit.other)->unk78 == 0) {
                    invMassB = 1.0f / GETP(GETP(GETP(hit.other)->vtable)->getMass)(GETP(hit.other));
                }
                if (GETP(hit.other)->type == 9) {
                    elastB *= CarA_GetImpactMultiplier(GETP(hit.other), &hit, -1.0f);
                    if (GETP(hit.thing)->type == 9) {
                        MoveImpactFx *fx_ = SLOT_LOAD(MoveImpactFx *, ((u8 *)GETP(g_worldData.world) + 0x9C));
                        f32 rel = vB - vA;
                        f32 lo = fx_->minVel;
                        u32 n = fx_->count;
                        f32 hi = fx_->maxVel;

                        if (rel > lo && hi - lo != 0.0f && n != 0) {
                            u32 idx = (f32)n * ((rel - lo) / (hi - lo));
                            u32 id;

                            if (idx > n) {
                                idx = n;
                            }
                            id = GETP(fx_->ids)[idx - 1];
                            if (id != 0) {
                                fx.id = id;
                                fx.pos = hit.pos;
                                fx.vel = g_zeroVector;
                                fx.unk44 = 0;
                                fx.unk48 = 0.0f;
                                fx.unk4C = 0.0f;
                                fx.angVel = g_zeroVector;
                                fx.quat = g_zeroVector;
                                ThingManager_DARYL_CreateThing(1, &fx);
                            }
                        }
                    }
                }
                if (GETP(hit.other)->type == 9 && (GETP(hit.thing)->flags & 8) && GETP(hit.thing)->unk78 != 0 && elastA > 0.0f) {
                    f32 lim = GETP(GETP(((MoveCar *)GETP(hit.other))->iface)->tuning)->maxImpactVel;

                    if (vB > lim) {
                        f32 d = lim - vB;

                        vB = lim;
                        GETP(hit.other)->vel.x += hit.normal.x * d;
                        GETP(hit.other)->vel.y += hit.normal.y * d;
                        GETP(hit.other)->vel.z += hit.normal.z * d;
                    } else if (vB < -lim) {
                        f32 d = -lim - vB;

                        vB = -lim;
                        GETP(hit.other)->vel.x += hit.normal.x * d;
                        GETP(hit.other)->vel.y += hit.normal.y * d;
                        GETP(hit.other)->vel.z += hit.normal.z * d;
                    }
                }
                PSVECSubtract(&hit.pos, &GETP(hit.other)->mtx.v[3], &rB);
                rot = GETP(hit.other)->mtx;
                inertia = g_identity;
                GETP(GETP(GETP(hit.other)->vtable)->getInvInertia)(GETP(hit.other), &diagB);
                TransposeSubMatrix(&rot);
                inertia.v[0].x = diagB.x;
                inertia.v[1].y = diagB.y;
                inertia.v[2].z = diagB.z;
                MultiplySubMatrix(&inertia, &GETP(hit.other)->mtx, &tmp);
                MultiplySubMatrix(&rot, &tmp, &invInertia);
                PSVECCrossProduct(&rB, &hit.normal, &rxnB);
                XformVecOrientation(&invInertia, &rxnB, &wB);
                PSVECCrossProduct(&wB, &rB, &crossB);
                damageB = GETP(hit.other)->damage;
            } else if (GETP(hit.surface) != NULL) {
                elastB = GETP(hit.surface)->elasticity;
            }
            vA = (vA - vB) * -(1.0f + _fabs(elastA * elastB));
            PSVECAdd(&crossA, &crossB, &sum);
            vA /= (invMassA + invMassB) * PSVECDotProduct(&hit.normal, &hit.normal) + PSVECDotProduct(&sum, &hit.normal);
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
            if (GETP(hit.other) != NULL && GETP(GETP(hit.thing)->vtable) == (MoveThingVTable *)g_carAVTable &&
                GETP(GETP(hit.other)->vtable) == (MoveThingVTable *)g_carAVTable) {
                MoveCar *car = (MoveCar *)GETP(hit.thing);
                MoveCarBody *body = GETP(car->iface)->model->body;

                armA.x = hit.pos.x - car->thing.mtx.v[3].x;
                armA.y = hit.pos.y - car->thing.mtx.v[3].y;
                armA.z = hit.pos.z - car->thing.mtx.v[3].z;
                if (FloatsEqual(PSVECDotProduct(&car->thing.mtx.v[2], &armA) - car->rideHeight, body->bumperHeight, 0.3f)) {
                    damageA = -vA * GETP(GETP(car->iface)->tuning)->damageScale * PSVECDotProduct(&GETP(hit.thing)->mtx.v[2], &hit.normal);
                    if (GETP(GETP(hit.thing)->player) != NULL && !Player_IsHuman(GETP(GETP(hit.thing)->player))) {
                        damageA = (f32)damageA * ((MovePlayer *)GETP(GETP(hit.thing)->player))->difficulty->damageScale;
                    }
                }
                car = (MoveCar *)GETP(hit.other);
                body = GETP(car->iface)->model->body;
                armB.x = hit.pos.x - car->thing.mtx.v[3].x;
                armB.y = hit.pos.y - car->thing.mtx.v[3].y;
                armB.z = hit.pos.z - car->thing.mtx.v[3].z;
                if (FloatsEqual(PSVECDotProduct(&car->thing.mtx.v[2], &armB) - car->rideHeight, body->bumperHeight, 0.3f)) {
                    damageB = vA * GETP(GETP(car->iface)->tuning)->damageScale * PSVECDotProduct(&GETP(hit.other)->mtx.v[2], &hit.normal);
                    if (GETP(GETP(hit.other)->player) != NULL && !Player_IsHuman(GETP(GETP(hit.other)->player))) {
                        damageB = (f32)damageB * ((MovePlayer *)GETP(GETP(hit.other)->player))->difficulty->damageScale;
                    }
                }
            }
            GETP(GETP(GETP(hit.thing)->vtable)->collide)(GETP(hit.thing), &hit, damageB, elastB);
            Thing_DynamicGridMove(GETP(hit.thing), dtStep);
            if (GETP(hit.other) != NULL) {
                GETP(GETP(GETP(hit.other)->vtable)->collide)(GETP(hit.other), &hit, damageA, elastA);
                Thing_DynamicGridMove(GETP(hit.other), dtStep);
            }
        }

        for (i = 0; i < numMoved; i++) {
            t = moved[i];
            GETP(GETP(t->vtable)->postMove)(t, dt);
            t->flags &= ~0x8000;
        }
        if (dt < 0.00001f && iters > 100) {
            elapsed = g_fDeltaTimeSec;
        }
        elapsed += dt;
        iters++;
    } while (elapsed < g_fDeltaTimeSec);

    g_frameCounter++;
    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = GETP(t->next)) {
        if (GETP(t->gridData) != NULL && !(t->flags & 0x200008)) {
            Thing_RotationalGridMove(t);
        }
    }
    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = GETP(t->next)) {
        if (!(t->flags & 0x10000)) {
            t->vel.x -= t->pushVel.x;
            t->vel.y -= t->pushVel.y;
            t->vel.z -= t->pushVel.z;
            t->pushVel.x = 0.0f;
            t->pushVel.y = 0.0f;
            t->pushVel.z = 0.0f;
        }
        if (GETP(t->vtable) == (MoveThingVTable *)g_carAVTable) {
            t->vel.x -= g_fDeltaTimeSec * (((MoveCar *)t)->drag * t->mtx.v[0].x);
            t->vel.y -= g_fDeltaTimeSec * (((MoveCar *)t)->drag * t->mtx.v[0].y);
            t->vel.z -= g_fDeltaTimeSec * (((MoveCar *)t)->drag * t->mtx.v[0].z);
        }
        if (!(t->flags & 0x200000)) {
            GETP(GETP(t->vtable)->update)(t);
            if (!(t->flags & 8) && GETP(t->gridData) != NULL) {
                Thing_StaticGridMove(t);
            }
            Thing_MoveSlaves(t);
        }
    }
    for (k = 0; k < 0x40; k++) {
        if (GETP(((SharedSlaveHolder *)g_aSharedSlaveHolder)[k].key) != NULL) {
            MoveThing *slave = GETP(((SharedSlaveHolder *)g_aSharedSlaveHolder)[k].slaves);

            while (slave != NULL) {
                MoveThing *next = GETP(slave->next);

                GETP(GETP(slave->vtable)->update)(slave);
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
    ThingManagerList_RemoveReleasedThings(PTR((s32)HND(&g_thingManager_DARYL.activeList)));
}

void ThingManager_DARYL_Render(void) {
    extern void *g_pNoRayCollideThing;
    ThingHeader *thing;

    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = GETP(thing->next)) {
        if (!(thing->flags & 0x200200) && Gfx_FrustrumCullSphereTestAllWindows(thing->unk40, thing->unk70)) {
            ((void (**)(void *))GETP(thing->vtable))[4](thing);
            g_pNoRayCollideThing = thing;
            Thing_DrawSlaves(thing);
        }
    }
    g_pNoRayCollideThing = NULL;
}

void *ThingManager_DARYL_CreateThing(s32 type, void *params) {
    void *thing = GETP(g_thingManager_DARYL.freeList[type]);

    if (thing == NULL) {
        thing = NULL;
    } else {
        SETP(g_thingManager_DARYL.freeList[type], SLOT_LOAD(void *, (u8 *)(thing) + 0xA4));
        if (GETP(g_thingManager_DARYL.freeList[type]) != NULL) {
            SLOT_STORE((u8 *)(GETP(g_thingManager_DARYL.freeList[type])) + 0xA0, NULL);
        }
        SLOT_STORE((u8 *)(thing) + 0xA4, NULL);
    }
    if (thing != NULL) {
        SLOT_STORE((u8 *)(thing) + 0xA4, GETP(g_thingManager_DARYL.activeList));
        SLOT_STORE((u8 *)(thing) + 0xA0, NULL);
        if (SLOT_LOAD(void *, (u8 *)(thing) + 0xA4) != NULL) {
            SLOT_STORE((u8 *)(SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) + 0xA0, thing);
        }
        SETP(g_thingManager_DARYL.activeList, thing);
        ((void (*)(void *, void *))SLOT_LOAD(void *, SLOT_LOAD(u32 *, (u8 *)PORT_ADDR(thing) + 0x8C) + (0)))(thing, params);
        return thing;
    }
    return NULL;
}

void *ThingManager_DARYL_CreateSlave(s32 type, void *params, u8 id) {
    void *slave = GETP(g_thingManager_DARYL.freeList[type]);

    if (slave == NULL) {
        slave = NULL;
    } else {
        SETP(g_thingManager_DARYL.freeList[type], SLOT_LOAD(void *, (u8 *)(slave) + 0xA4));
        if (GETP(g_thingManager_DARYL.freeList[type]) != NULL) {
            SLOT_STORE((u8 *)(GETP(g_thingManager_DARYL.freeList[type])) + 0xA0, NULL);
        }
        SLOT_STORE((u8 *)(slave) + 0xA4, NULL);
    }
    if (slave != NULL) {
        ((void (*)(void *, void *))SLOT_LOAD(void *, SLOT_LOAD(u32 *, (u8 *)PORT_ADDR(slave) + 0x8C) + (0)))(slave, params);
        M2C_FIELD(slave, u8 *, 0x82) = id;
        M2C_FIELD(slave, u32 *, 0x74) |= 0x20000000;
        return slave;
    }
    return NULL;
}

s32 OBox_CollideAllThingsZeroTime(void *obox, ThingHeader *thing, void *info) {
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    GridResult results[256];
    u32 count;
    u32 i;
    ThingHeader *other;
    f32 t;
    f32 dist;
    f32 r;
    void *otherBox;

    Thing_GetStaticRotationalMinMax(thing, &min, &max);
    count = Grid_GetObjects(&min, &max, 5, results, 0x100);
    for (i = 0; i < count; i++) {
        other = (ThingHeader *)PTR(results[i].id);
        if (!(other->flags & 2) && *(s16 *)((u8 *)other + 0x80) != 0 && other != thing) {
            dist = PSVECSquareDistance(thing->unk40, other->unk40);
            r = other->unk70 + thing->unk70;
            if (r * r > dist) {
                t = 1.0f;
                if (*(s16 *)((u8 *)other + 0x80) == 1) {
                    otherBox = ((void *(*)(void *))SLOT_LOAD(void *, (u32 *)GETP(other->vtable) + 0x10))(other);
                    if (OBoxHitsOBoxZeroTime(obox, otherBox, info)) {
                        if (info != NULL) {
                            SLOT_STORE(((u8 *)info + 0x20), thing);
                            SLOT_STORE(((u8 *)info + 0x24), other);
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

s8 RayHitsObjects_Aux(Vector4 *start, Vector4 *dir, f32 *t, void *info) {
    extern void *g_pNoRayCollideThing;
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 end __attribute__((aligned(16)));
    GridResult results[512];
    u32 count;
    u32 i;
    ThingHeader *other;
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
        other = (ThingHeader *)PTR(results[i].id);
        if (!(other->flags & 2) && other != g_pNoRayCollideThing &&
            SegmentVsSphere(other->unk40, start, &end, other->unk70)) {
            hit = (*(f32 (**)(void *, Vector4 *, Vector4 *, void *, f32))((u8 *)GETP(other->vtable) + 0x38))(other, start, dir, info, best);
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

    if (GETP(g_worldData.world) == NULL || SLOT_LOAD(void *, GETP(g_worldData.world)) == NULL) {
        return 0;
    }
    hitTerrain = RayHitsTerrain_Aux3(arg0, arg1, arg2, arg3);
    if (RayHitsObjects_Aux(arg0, arg1, arg2, arg3)) {
        return 1;
    }
    return hitTerrain;
}

s8 OBoxHitsTerrainAndObjectsZeroTime(void *obox, void *thing, void *info) {
    typedef struct { f32 x, y, z, w; } Vec4;
    extern Vector4 g_zeroVector;
    f32 time;
    Vec4 max;
    Vec4 min;
    GridResult results[96];
    u32 count;
    u32 i;
    u8 *other;
    f32 t;
    s32 hit;

    time = 1.0f;
    if (!(*(u32 *)((u8 *)thing + 0x74) & 0x40000)) {
        if (OBoxHitsTerrain(obox, &g_zeroVector, &time, info)) {
            return 1;
        }
    }
    if (*(u32 *)((u8 *)thing + 0x74) & 0x20000) {
        hit = 0;
    } else {
        Thing_GetStaticRotationalMinMax(thing, (Vector4 *)&min, (Vector4 *)&max);
        count = Grid_GetObjects(&min, &max, 5, results, 0x60);
        for (i = 0; i < count; i++) {
            other = (u8 *)PTR(results[i].id);
            if (!(*(u32 *)(other + 0x74) & 2) && *(s16 *)(other + 0x80) != 0 && other != thing &&
                (*(f32 *)(other + 0x70) + *(f32 *)((u8 *)thing + 0x70)) * (*(f32 *)(other + 0x70) + *(f32 *)((u8 *)thing + 0x70)) >
                    PSVECSquareDistance((u8 *)thing + 0x40, other + 0x40)) {
                if (*(s16 *)(other + 0x80) == 1) {
                    t = OBoxHitsOBox(obox, &g_zeroVector, (*(void *(**)(void *))(SLOT_LOAD(u8 *, (other + 0x8C)) + 0x40))(other), &g_zeroVector, 0, 1.0f);
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
    if (!(M2C_FIELD(thing, u32 *, 0x74) & 0x40000) && OBoxHitsTerrainZeroTimeEdgesOnly(obox, arg2)) {
        return 1;
    }
    return OBox_CollideAllThingsZeroTime(obox, thing, arg2);
}

s32 RayHitsObjects_GetObject(Vector4 *start, Vector4 *end, f32 *t, s8 statics, s8 dynamics) {
    GridResult results[256];
    u8 info[0x50] __attribute__((aligned(16)));
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
        if (other->flags & 2) {
            continue;
        }
        if ((statics == 1 && GETP(GETP(other->record)->info)->takesDamage == 0 && GETP(GETP(other->record)->info)->armour == 0) ||
            (dynamics == 1 && (GETP(GETP(other->record)->info)->takesDamage == 1 || GETP(GETP(other->record)->info)->armour != 0))) {
            if (SegmentVsSphere(&other->offsetPos, start, &hitEnd, *(f32 *)((u8 *)other + 0x70))) {
                hit = (*(f32 (**)(void *, Vector4 *, Vector4 *, void *, f32))((u8 *)GETP(other->vtable) + 0x38))(other, start, &dir, info, best);
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

s32 ThingManager_GetActiveCars(void **cars, s32 max) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 8; i++) {
        u8 *car = g_thingManager_DARYL.unk0 + 0x60310 + i * 0x5A0;
        if (!(M2C_FIELD(car, u32 *, 0x74) & 0x201000)) {
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
        u8 *item = g_thingManager_DARYL.unk0 + 0x3D740 + i * 0xF0;
        if (!(*(u32 *)(item + 0x74) & 0x201000)) {
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
        u8 *seq = g_thingManager_DARYL.unk0 + 0x70770 + i * 0xE0;
        if (!(M2C_FIELD(seq, u32 *, 0x74) & 0x201000)) {
            seqs[count++] = seq;
        }
    }
    return count;
}

s32 GetAllTargets(void **targets) {
    return ThingManager_GetActiveCars(targets, 8);
}

static inline s32 ThingManager_GetActiveUnk6D100(void **things) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 1; i++) {
        u8 *thing = g_thingManager_DARYL.unk0 + 0x6D100 + i * 0x5A0;
        if (!(*(u32 *)(thing + 0x74) & 0x201000)) {
            things[count++] = thing;
        }
    }
    return count;
}

static inline s32 ThingManager_GetActiveUnk5B940(void **things) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 1; i++) {
        u8 *thing = g_thingManager_DARYL.unk0 + 0x5B940 + i * 0x5A0;
        if (!(*(u32 *)(thing + 0x74) & 0x201000)) {
            things[count++] = thing;
        }
    }
    return count;
}

static inline s32 ThingManager_GetActiveUnk20280(void **things) {
    s32 count;
    s32 i;

    count = 0;
    for (i = 0; i < 500; i++) {
        u8 *thing = g_thingManager_DARYL.unk0 + 0x20280 + i * 0xF0;
        if (!(*(u32 *)(thing + 0x74) & 0x201000)) {
            things[count++] = thing;
        }
    }
    return count;
}

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
        PSVECSubtract((Vector4 *)((u8 *)cars[i] + 0x40), pos, &diff1);
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
    count = ThingManager_GetActiveUnk6D100(pods);
    for (j = 0; j < count; j++) {
        PSVECSubtract((Vector4 *)((u8 *)pods[j] + 0x40), pos, &diff2);
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
    count = ThingManager_GetActiveUnk5B940(bases);
    for (k = 0; k < count; k++) {
        PSVECSubtract((Vector4 *)((u8 *)bases[k] + 0x40), pos, &diff3);
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
    count = ThingManager_GetActiveUnk20280(items);
    for (m = 0; m < count; m++) {
        PSVECSubtract((Vector4 *)((u8 *)items[m] + 0x40), pos, &diff4);
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


void ThingManager_Invalidate(u32 start, u32 end) {
    typedef struct SoundSeqThing {
        u8 pad0[0xD0];
        s32 sources[4];
    } SoundSeqThing;
    u32 i;
    u8 *thing;
    SoundSeqThing *seq;
    u32 j;

    if (GETP(g_thingManager_DARYL.activeList) == NULL) {
        return;
    }
    for (i = 0; i < 140; i++) {
        thing = g_thingManager_DARYL.unk0 + i * 0x160;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Projectile_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 192; i++) {
        thing = g_thingManager_DARYL.unk0 + 0xC080 + i * 0x100;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)EventSeq_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 160; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x18080 + i * 0xD0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Emitter_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 500; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x20280 + i * 0xF0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Destructible_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 96; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x3D740 + i * 0xF0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Item_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 192; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x43140 + i * 0x160;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)AttachThing_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 1; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x5B940 + i * 0x5A0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Creature_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 16; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x5BB10 + i * 0x480;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)WildCardRack_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 8; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x60310 + i * 0x5A0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)CarA_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 100; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x63010 + i * 0x140;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Destructible_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 32; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x6ADE0 + i * 0x110;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Tracer_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 1; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x6CFE0 + i * 0x120;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Pod_Invalidate(HND(thing), start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 1; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x6D100 + i * 0x5A0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Destructible_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 56; i++) {
        thing = g_thingManager_DARYL.unk0 + 0x6D2F0 + i * 0xF0;
        if (!(*(u32 *)(thing + 0x74) & 0x1000) && (s8)Motion_Invalidate(thing, start, end)) {
            (*(void (**)(void *))(SLOT_LOAD(u8 *, (thing + 0x8C)) + 4))(thing);
        }
    }
    for (i = 0; i < 128; i++) {
        seq = (SoundSeqThing *)(g_thingManager_DARYL.unk0 + 0x70770 + i * 0xE0);
        if (!(*(u32 *)((u8 *)seq + 0x74) & 0x1000) && (s8)SoundSeq_Invalidate(seq, start, end)) {
            for (j = 0; j < 4; j++) {
                if (seq->sources[j] >= 0) {
                    SoundSource_Stop(seq->sources[j]);
                }
                seq->sources[j] = -1;
            }
            Thing_Release((s32)HND(seq));
        }
    }
    ThingManagerList_RemoveReleasedThings(&g_thingManager_DARYL.activeList);
}


s8 TriggerVehicleDamage(u32 *params) {
    u32 i;
    u32 count;
    u8 **things;
    s32 *saved;
    u32 type;
    s32 amount;
    s32 result;

    count = params[0];
    things = (u8 **)&params[1];
    result = 0;
    saved = (s32 *)&params[count + 3];
    type = params[count + 1];
    amount = params[count + 2];
    for (i = 0; i < count; i++) {
        u8 *vehicle = SLOT_LOAD(u8 *, (u8 *)(*things) + 0x28);
        if (type == 0x2A) {
            s32 damage = *(s32 *)(vehicle + 0x78);
            if (damage < *saved - amount) {
                result = 1;
            }
            *saved = damage;
        }
        if (type == 0x2B && *(s32 *)(vehicle + 0x78) < *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (vehicle + 0x3BC)) + 0xA0)) + 0x14) / amount) {
            result = 1;
        }
        things++;
        saved++;
    }
    return result;
}

s32 TriggerPlayerVehicleGear(u32 *params) {
    extern u8 g_cameraManager[];
    u8 *vehicle = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, g_cameraManager)) + 0x2C);

    if (vehicle != NULL && vehicle[0x4E8] == params[1]) {
        return 1;
    }
    return 0;
}

s32 TriggerPlayerVehicleInAir(u32 *params) {
    extern u8 g_cameraManager[];
    u8 *vehicle;
    u32 inAir;

    if (SLOT_LOAD(u8 *, g_cameraManager) != NULL) {
        vehicle = SLOT_LOAD(u8 *, (u8 *)(SLOT_LOAD(u8 *, g_cameraManager)) + 0x2C);
    } else {
        vehicle = NULL;
    }
    inAir = params[2];
    if (inAir == 0 && vehicle != NULL && *(s32 *)(vehicle + 0x460) >= (s32)params[1]) {
        params[2] = 1;
        return 1;
    }
    if (inAir != 0 && vehicle != NULL && *(s32 *)(vehicle + 0x460) == 0) {
        params[2] = 0;
        return 1;
    }
    return 0;
}

