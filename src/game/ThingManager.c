#include "common.h"


void ThingManagerList_Append(void **arg0, void *arg1) {
    SLOT_STORE((u8 *)(arg1) + 0xA4, (void *) *arg0);
    M2C_FIELD(arg1, s32 *, 0xA0) = 0;
    if (SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4) != NULL) {
        SLOT_STORE((u8 *)(SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4)) + 0xA0, arg1);
    }
    *arg0 = arg1;
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
    ThingSlot1D0 creature;              /* 0x5B940 type 7 */
    ThingSlot480 wildCardRacks[16];     /* 0x5BB10 type 8 */
    ThingSlot5A0 cars[8];               /* 0x60310 type 9 */
    ThingSlot140 physicalObjects[100];  /* 0x63010 type 10 */
    ThingSlotD0 looker;                 /* 0x6AD10 type 11 */
    ThingSlot110 tracers[32];           /* 0x6ADE0 type 12 */
    ThingSlot120 pod;                   /* 0x6CFE0 type 13 */
    ThingSlot1F0 drone;                 /* 0x6D100 type 14 */
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
        *arg0 = SLOT_LOAD(void *, (u8 *)(arg1) + 0xA4);
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

    thing = *list;
    while (thing != NULL) {
        next = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
        if (M2C_FIELD(thing, u8 *, 0x83) == type) {
            M2C_FIELD(SLOT_LOAD(void *, (u8 *)(thing) + 0x8C), void (**)(void *), 4)(thing);
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

    for (thing = *list; thing != NULL; thing = next) {
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
    for (thing = *list; thing != NULL; thing = next) {
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

    thing = *list;
    while (thing != NULL) {
        next = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4);
        M2C_FIELD(SLOT_LOAD(void *, (u8 *)(thing) + 0x8C), void (**)(void *), 4)(thing);
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

    thing = *list;
    while (thing != NULL) {
        next = GETP(thing->next);
        if (thing->slavePlayer == 0xEF) {
            if (GETP(thing->prev) == NULL) {
                *list = GETP(thing->next);
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

s32 SharedSlaveHolder_Get(void *shared) {
    extern u8 g_aSharedSlaveHolder[0x200];
    void ThingManagerList_InitAllSlavesNow(void *list, void *slaves, u8 player);
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

void ThingManagerList_TriggerSlaves(ThingHeader **list, struct SlaveTrigger {
        Vector4 pos;
        u8 type;
        u8 pad11;
        u16 id;
        u8 pad14[4];
        PTR32(void) data;
    } *trigger, Vector4 *a, Vector4 *b, Vector4 *c, u32 player) {
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
        for (thing = *list; thing != NULL; thing = GETP(thing->next)) {
            if (thing->slaveId == id && thing->slavePlayer == pl) {
                ((void (**)(ThingHeader *))GETP(thing->vtable))[1](thing);
            }
        }
        break;
    }

    if (slave != NULL) {
        slave->slavePlayer = player;
        SETP(slave->next, *list);
        SETP(slave->prev, NULL);
        if (GETP(slave->next) != NULL) {
            SETP(GETP(slave->next)->prev, slave);
        }
        *list = slave;
    }
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
    ThingPools *tm = (ThingPools *)&g_thingManager_DARYL;
    ThingHeader *thing;
    u32 i;

    for (i = 0; i < 18; i++) {
        SETP(tm->freeList[i], NULL);
    }
    SETP(tm->activeList, NULL);
    for (i = 0; i < 140; i++) {
        ThingSlotProjectile *p = &tm->projectiles[i];

        SETP(p->thing.vtable, g_projectileVTable);
        p->unk130 = -1;
        p->thing.unk88 = 0;
        p->thing.flags |= 0x1000;
        SETP(p->thing.prev, NULL);
        SETP(p->thing.next, NULL);
        p->thing.type = 0;
        ThingManagerList_Append(&g_thingManager_DARYL.freeList[p->thing.type], &p->thing);
    }
    for (i = 0; i < 192; i++) {
        thing = &tm->eventSeqs[i].thing;
        ThingManager_InitFreeThing(thing, g_eventSeqVTable, 1);
    }
    for (i = 0; i < 160; i++) {
        thing = &tm->emitters[i].thing;
        ThingManager_InitFreeThing(thing, g_emitterVTable, 2);
    }
    for (i = 0; i < 500; i++) {
        thing = &tm->destructibles[i].thing;
        ThingManager_InitFreeThing(thing, g_destructibleVTable, 3);
    }
    for (i = 0; i < 96; i++) {
        thing = &tm->items[i].thing;
        ThingManager_InitFreeThing(thing, g_itemVTable, 4);
    }
    for (i = 0; i < 192; i++) {
        thing = &tm->attaches[i].thing;
        ThingManager_InitFreeThing(thing, g_attachVTable, 5);
    }
    for (i = 0; i < 128; i++) {
        thing = &tm->debris[i].thing;
        ThingManager_InitFreeThing(thing, g_debrisVTable, 6);
    }
    thing = &tm->creature.thing;
    ThingManager_InitFreeThing(thing, g_creatureVTable, 7);
    for (i = 0; i < 16; i++) {
        thing = &tm->wildCardRacks[i].thing;
        ThingManager_InitFreeThing(thing, g_wildCardRackVTable, 8);
    }
    for (i = 0; i < 8; i++) {
        thing = &tm->cars[i].thing;
        ThingManager_InitFreeThing(thing, g_carAVTable, 9);
    }
    for (i = 0; i < 100; i++) {
        thing = &tm->physicalObjects[i].thing;
        ThingManager_InitFreeThing(thing, g_physicalObjectVTable, 10);
    }
    thing = &tm->looker.thing;
    ThingManager_InitFreeThing(thing, g_lookerVTable, 11);
    for (i = 0; i < 32; i++) {
        thing = &tm->tracers[i].thing;
        ThingManager_InitFreeThing(thing, g_tracerVTable, 12);
    }
    thing = &tm->pod.thing;
    ThingManager_InitFreeThing(thing, g_podVTable, 13);
    thing = &tm->drone.thing;
    ThingManager_InitFreeThing(thing, g_droneVTable, 14);
    for (i = 0; i < 56; i++) {
        thing = &tm->motions[i].thing;
        ThingManager_InitFreeThing(thing, g_motionVTable, 15);
    }
    for (i = 0; i < 128; i++) {
        thing = &tm->soundSeqs[i].thing;
        ThingManager_InitFreeThing(thing, g_soundSeqVTable, 16);
    }
    for (i = 0; i < 64; i++) {
        thing = &tm->radarBlips[i].thing;
        ThingManager_InitFreeThing(thing, g_radarBlipVTable, 17);
    }
}

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
        ((void (*)(void *))(M2C_FIELD(thing, void ***, 0x8C))[1])(thing);
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

void ThingManager_DARYL_Release(void) {

    SharedSlaveHolder_Release();
    ThingManagerList_ReleaseAndRemoveAll(&g_thingManager_DARYL.activeList);
    ThingManagerList_ReleaseAndRemoveAll(&g_thingManager_DARYL.activeList);
    ThingManager_DARYL_CreateMain();
}

void ThingManager_MoveLookerOnly(void) {
    extern u8 g_soundSeqVTable[];
    void *thing;

    ((void (*)(void *))(M2C_FIELD(&g_thingManager_DARYL, void ***, 0x6AD9C))[2])(g_thingManager_DARYL.looker);
    for (thing = GETP(g_thingManager_DARYL.activeList); thing != NULL; thing = SLOT_LOAD(void *, (u8 *)(thing) + 0xA4)) {
        if (SLOT_LOAD(u8 *, (u8 *)(thing) + 0x8C) == g_soundSeqVTable && (M2C_FIELD(thing, u32 *, 0x74) & 0x100000)) {
            ((void (*)(void *))(M2C_FIELD(thing, void ***, 0x8C))[3])(thing);
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

void ThingManager_MoveThings_DARYL_SUBTICK(void)
{
}

void ThingManager_DARYL_Render(void) {
    extern void *g_pNoRayCollideThing;
    s32 Gfx_FrustrumCullSphereTestAllWindows(void *, f32);
    void Thing_DrawSlaves(void *);
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
        ((void (*)(void *, void *))(M2C_FIELD(thing, void ***, 0x8C))[0])(thing, params);
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
        ((void (*)(void *, void *))(M2C_FIELD(slave, void ***, 0x8C))[0])(slave, params);
        M2C_FIELD(slave, u8 *, 0x82) = id;
        M2C_FIELD(slave, u32 *, 0x74) |= 0x20000000;
        return slave;
    }
    return NULL;
}

s32 OBox_CollideAllThingsZeroTime(void *obox, void *thing, void *info) {
    s8 OBoxHitsOBoxZeroTime(void *a, void *b, void *info);
    Vector4 min;
    Vector4 max;
    GridResult results[256];
    u32 count;
    u32 i;
    u8 *other;
    f32 t;
    f32 dist;
    f32 r;
    void *otherBox;

    Thing_GetStaticRotationalMinMax(thing, &min, &max);
    count = Grid_GetObjects(&min, &max, 5, results, 0x100);
    for (i = 0; i < count; i++) {
        other = (u8 *)PTR(results[i].id);
        if (!(*(u32 *)(other + 0x74) & 2) && *(s16 *)(other + 0x80) != 0 && other != thing) {
            dist = PSVECSquareDistance((f32 *)((u8 *)thing + 0x40), (f32 *)(other + 0x40));
            r = *(f32 *)(other + 0x70) + *(f32 *)((u8 *)thing + 0x70);
            if (r * r > dist) {
                t = 1.0f;
                if (*(s16 *)(other + 0x80) == 1) {
                    otherBox = (*(void *(**)(void *))(SLOT_LOAD(u8 *, (other + 0x8C)) + 0x40))(other);
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
    s8 SegmentVsSphere(void *center, Vector4 *start, Vector4 *end, f32 radius);
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

s8 OBoxHitsTerrainAndObjectsZeroTimeEdgesOnly(void *obox, void *thing, void *arg2) {
    if (!(M2C_FIELD(thing, u32 *, 0x74) & 0x40000) && OBoxHitsTerrainZeroTimeEdgesOnly(obox, arg2)) {
        return 1;
    }
    return OBox_CollideAllThingsZeroTime(obox, thing, arg2);
}

s32 RayHitsObjects_GetObject(Vector4 *start, Vector4 *end, f32 *t, s8 statics, s8 dynamics) {
    s8 SegmentVsSphere(void *center, Vector4 *start, Vector4 *end, f32 radius);
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

    count = ThingManager_GetActiveCars(cars);
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
    s32 Emitter_Invalidate(void *arg0, u32 arg1, u32 arg2);
    s32 SoundSeq_Invalidate(void *arg0, u32 arg1, u32 arg2);
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

