#include "common.h"


/* animation table: the per-creature list MotionStateInit/Death index */
typedef struct AnimEntry
{
    u32 anim;
    u8 unk4[0x60 - 0x4];
} AnimEntry;
typedef struct AnimTable
{
    u8 unk0[0x14];
    u32 count;
    AnimEntry *entries;
} AnimTable;

/* damage parts: pieces a creature can lose when gibbed */
typedef struct DamagePart
{
    u8 spawned;
    u8 useOffset;
    u16 unk2;
    u16 bone;
    u16 unk6;
    void *geom;
    Vector4 *offset;
    Vector4 *slavePos;
    u32 debris;
    void *slaveInfo;
} DamagePart;
typedef struct DamagePartList
{
    u32 count;
    DamagePart *parts;
} DamagePartList;

static inline u32 Creature_FindDamagePart(DamagePartList *list, u32 bone)
{
    u32 i;

    for (i = 0; i < list->count; i++)
    {
        if (bone == list->parts[i].bone)
        {
            return i;
        }
    }
    return -1;
}

static inline Vector4 Vector4_OrZero(Vector4 *v)
{
    extern Vector4 g_zeroVector;

    if (v != NULL)
    {
        return *v;
    }
    return g_zeroVector;
}

/* parameter 2 of Creature_Init: the spawn description it copies out of */
typedef struct CreatureInit
{
    Vector4 pos;
    f32 jitterBase;
    u8 *anims;
} CreatureInit;


void Creature_GetTargetPos(Creature *creature, Vector4 *pos) {
    pos->x = creature->pos.x;
    pos->y = 4.0f + creature->pos.y;
    pos->z = creature->pos.z;
    pos->w = 1.0f;
}

f32 Creature_GetMass(Creature *creature) {
    return creature->mass;
}

void Creature_MotionStateInitDeath(Creature *creature) {
    u8 *motion;
    s32 state;
    u32 index;
    u8 *entry;
    u8 *anims;

    motion = creature->motion;
    index = *(u32 *)((u8 *)creature + 0x1CC);
    state = 0xFFFF;
    if (creature->unk1B4 == 0) {
        anims = creature->anims;
        if (SLOT_LOAD(u32 *, (anims + 0x30)) != NULL) {
            entry = (u8 *)SLOT_LOAD(u32 *, (anims + 0x30)) + index * 0x60;
            if (index >= *(u32 *)(anims + 0x2C) || *(u32 *)entry == 0) {
                entry = (u8 *)SLOT_LOAD(u32 *, (anims + 0x18));
                state = 0;
                index = 0;
            }
            *(u32 *)((u8 *)creature + 0xCC) = *(u32 *)entry;
            MotionState_Init(motion, 0);
            *(s32 *)((u8 *)creature + 0xC4) = state;
            *(s32 *)((u8 *)creature + 0x114) = index;
            *(u32 *)((u8 *)creature + 0x118) = (u32)HND(entry);
            creature->flags |= 0x20;
            if (SLOT_LOAD(u8 *, ((u8 *)creature + 0x90)) != NULL) {
                *(s32 *)(SLOT_LOAD(u8 *, ((u8 *)creature + 0x90)) + 0x1C) = 0;
            }
            *(u32 *)((u8 *)creature + 0x1CC) = 0;
        }
    }
}


void Creature_MotionStateInit(void *arg0, s32 state, u8 reset, u8 keepTime)
{
    Creature *creature;
    AnimTable *anims;
    AnimEntry *entry;
    f32 time;
    u32 index;
    u8 *motion;

    creature = arg0;
    time = *(f32 *)(creature->motion + 0x20);
    motion = creature->motion;
    index = 0;
    if (reset && state == *(s32 *)((u8 *)creature + 0xC4) && *(u8 *)(creature->motion + 0x1C) != 2)
    {
        *(u8 *)(creature->motion + 0x1D) = 0;
        return;
    }
    if (creature->flags & 0x10)
    {
        return;
    }
    creature->flags &= ~0x20;
    switch (state)
    {
    case 0:
        index = 0;
        break;
    case 1:
        index = 1;
        break;
    case 2:
        index = 2;
        break;
    case 4:
        index = 5;
        break;
    case 5:
        index = 6;
        break;
    case 6:
        index = 7;
        break;
    case 7:
        index = 8;
        creature->flags |= 0x10;
        break;
    case 8:
        index = 9;
        creature->flags |= 0x10;
        break;
    case 9:
        index = 10;
        break;
    case 10:
        index = 0x10;
        creature->flags |= 0x10;
        break;
    case 11:
        index = 0x16;
        creature->flags |= 0x10;
        break;
    case 14:
        index = 0xE;
        creature->unkB4 = 0.0f;
        creature->flags |= 0x10;
        break;
    case 15:
        index = 0xF;
        creature->flags |= 0x10;
        break;
    }
    anims = (AnimTable *)creature->anims;
    entry = anims->entries + index;
    if (index >= anims->count || entry->anim == 0)
    {
        entry = anims->entries;
        state = 0;
        index = 0;
    }
    *(u32 *)creature->motion = entry->anim;
    if (!reset)
    {
        *(f32 *)(motion + 0x24) = 0.0f;
        *(s32 *)((u8 *)creature + 0x114) = 0xFFFF;
    }
    MotionState_Init(motion, reset);
    *(s32 *)((u8 *)creature + 0xC4) = state;
    *(s32 *)((u8 *)creature + 0x114) = index;
    *(u32 *)((u8 *)creature + 0x118) = (u32)HND(entry);
    if (keepTime)
    {
        *(f32 *)((u8 *)creature + 0xEC) = time;
    }
}


void Thing_Init(void *a, s32 b);
s32 Creature_Init(Creature *arg0, CreatureInit *arg1)
{
    CreatureInit *init;
    Creature *creature;

    init = arg1;
    creature = arg0;
    Thing_Init(creature, 0);
    creature->anims = init->anims;
    creature->pos = init->pos;
    creature->startPos = creature->pos;
    creature->mass = RandomAdd(0.002f, 0.001f);
    creature->unk1B8 = 0;
    creature->unk1C8 = 0;
    QuaternionToSubMatrix(&creature->quat, creature->mtx);
    creature->unk80 = 0;
    creature->unk70 = 11.0f;
    creature->unkB0 = init->jitterBase;
    creature->unkB4 = 0.0f;
    creature->unkC0 = 0.0f;
    creature->unk1B4 = 0;
    creature->unk78 = *(u32 *)(creature->anims + 4);
    Creature_MotionStateInit(creature, 0, 0, 0);
    creature->unkBF = 0;
    creature->unk130 = 0.03f;
    creature->unk1B0 = -1;
    creature->unk1C0 = 1.0f;
    Thing_AddToGrid(creature, 6);
    MotionState_BuildMatricesWorld(&creature->quat, &creature->pos, creature->motion, 1.0f);
    return 1;
}

void Creature_Release(Creature *creature) {
    MotionState_Release(HND(creature->motion));
    Thing_Release(HND(creature));
}

void Creature_PartialGibb(Creature *creature, u32 count) {
    typedef struct DebrisParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        void *player;
        f32 unk48;
        f32 unk4C;
    } DebrisParams;
    typedef struct PartParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 axis;
        Vector4 vel;
        f32 gravity;
        void *geom;
        s32 life;
        u32 unk4C;
        s32 unk50;
        u8 pad54[0xC];
    } PartParams;
    typedef struct SlaveParams {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        void *info;
        u32 unk34;
        f32 unk38;
        u8 pad3C[4];
    } SlaveParams;
    typedef struct AnimNode {
        u8 pad0[0x1F];
        u8 flags;
    } AnimNode;
    typedef struct AnimNodeList {
        u32 count;
        u8 pad4[0xC];
        AnimNode *nodes;
    } AnimNodeList;
    extern Vector4 g_zeroVector;
    void XformVec(void *mtx, void *in, Vector4 *out);
    AnimNode nodes[64];
    PartParams partParams;
    DebrisParams debrisParams;
    SlaveParams slaveParams;
    Vector4 pos __attribute__((aligned(16)));
    Vector4 vel __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 quat __attribute__((aligned(16)));
    u32 i;
    u32 j;
    u32 blown;
    AnimNodeList *anim;
    DamagePartList *list;
    f32 scale;
    u8 *thing;
    u8 *slave;
    u32 debris;

    blown = 0;
    anim = SLOT_LOAD(AnimNodeList *, (SLOT_LOAD(u8 *, creature->motion) + 0x14));
    list = SLOT_LOAD(DamagePartList *, (creature->anims + 0x70));
    if (list == NULL) {
        return;
    }
    memcpy(nodes, anim->nodes, anim->count << 5);
    for (i = 0; i < anim->count; i++) {
        nodes[i].flags = 0;
        if (creature->unk1C8 & (1 << i)) {
            nodes[i].flags |= 1;
        }
        blown++;
    }
    do {
        i = RandomAdd_s32(2, anim->count - 2);
        blown += AnimNodes_BlowBranch(&nodes[i]);
    } while (blown < count);
    for (i = 0; i < anim->count; i++) {
        if (!(nodes[i].flags & 2)) {
            continue;
        }
        j = Creature_FindDamagePart(list, i);
        if (j == -1) {
            continue;
        }
        if (list->parts[j].useOffset) {
            XformVec(SLOT_LOAD(u8 *, (creature->motion + 0x10)) + (i << 6), list->parts[j].offset, &pos);
        } else {
            pos = *(Vector4 *)(SLOT_LOAD(u8 *, (creature->motion + 0x10)) + (i << 6) + 0x30);
        }
        if (list->parts[j].spawned == 0) {
            scale = RandomNumberGenerator_GetRandomFloat(3.0f, 20.0f);
            RandomNumberGenerator_GetRandomVector(&dir);
            vel = *(Vector4 *)creature;
            vel.x = dir.x * scale + vel.x;
            vel.y = dir.y * scale + vel.y;
            vel.z = dir.z * scale + vel.z;
            SubMatrixToQuaternion(SLOT_LOAD(u8 *, (creature->motion + 0x10)) + (i << 6), &quat);
            creature->unk1C8 |= 1 << i;
            debris = list->parts[j].debris;
            partParams.life = 3000;
            partParams.gravity = -50.0f;
            partParams.vel = vel;
            partParams.pos = pos;
            partParams.geom = list->parts[j].geom;
            partParams.quat = quat;
            partParams.unk50 = 1;
            if (debris != 0) {
                debrisParams.id = debris;
                debrisParams.pos = pos;
                debrisParams.quat = quat;
                debrisParams.vel = vel;
                debrisParams.player = NULL;
                debrisParams.unk48 = 0.0f;
                debrisParams.unk4C = 0.0f;
                debrisParams.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &debrisParams);
            }
            RandomNumberGenerator_GetRandomVector(&partParams.axis);
            partParams.axis.w = 0.017453292f * RandomAdd(100.0f, 1260.0f);
            thing = ThingManager_DARYL_CreateThing(6, &partParams);
            if (thing != NULL && list->parts[j].slaveInfo != NULL) {
                slaveParams.unk38 = 0.0f;
                slaveParams.quat.w = 1.0f;
                slaveParams.quat.x = 0.0f;
                slaveParams.quat.y = 0.0f;
                slaveParams.quat.z = 0.0f;
                slaveParams.pos = Vector4_OrZero(list->parts[j].slavePos);
                slaveParams.info = list->parts[j].slaveInfo;
                slaveParams.unk34 = 0;
                slaveParams.vel = g_zeroVector;
                slave = ThingManager_DARYL_CreateSlave(2, &slaveParams, 0xFF);
                if (slave != NULL) {
                    slave[0x83] = 0xFF;
                    ThingManagerList_Append(thing + 0x88, slave);
                }
            }
        }
    }
}
void Creature_InterpretControls(void *arg0) {
    struct CreatureControls {
        s8 x;
        s8 y;
        u8 unk2[0x14 - 0x2];
        u8 unk14;
        u8 unk15[0x1A - 0x15];
        u8 unk1A;
        u8 unk1B;
        u8 unk1C[0x32 - 0x1C];
        u8 unk32;
    };
    struct CreatureCtl {
        u8 unk0[0x78];
        s32 unk78;
        u8 unk7C[0x94 - 0x7C];
        struct CreatureControls *controls;
        u8 unk98[0xBF - 0x98];
        s8 unkBF;
        u8 unkC0[0xC4 - 0xC0];
        s32 state;
        f32 unkC8;
        u8 unkCC[0xE4 - 0xCC];
        u8 *motion;
        u8 unkE8[0xEC - 0xE8];
        f32 time;
    } *creature = arg0;
    s32 state;

    state = creature->state;
    if (creature->unk78 < 0) {
        return;
    }
    if (creature->unkBF) {
        if (!((state == 4 || state == 10) &&
              creature->time < *(f32 *)(SLOT_LOAD(u8 *, (creature->motion + 0x28)) + 4))) {
            if (creature->controls->unk1B) {
                Creature_MotionStateInit(creature, 7, 1, 0);
            } else if (creature->controls->unk1A) {
                Creature_MotionStateInit(creature, 9, 1, 0);
            } else if (creature->controls->y > 0x20) {
                if (creature->controls->unk14) {
                    Creature_MotionStateInit(creature, 2, 1, 1);
                } else {
                    Creature_MotionStateInit(creature, 1, 1, 1);
                }
            } else if (creature->controls->y < -0x20) {
                Creature_MotionStateInit(creature, 5, 1, 1);
            } else if (creature->controls->x > 0x20) {
                Creature_MotionStateInit(creature, 6, 1, 1);
            } else {
                Creature_MotionStateInit(creature, 0, 1, 1);
            }
        }
        switch (creature->controls->unk32) {
        case 17:
            Creature_MotionStateInit(creature, 14, 0, 0);
            break;
        case 18:
            Creature_MotionStateInit(creature, 15, 0, 0);
            break;
        case 3:
            Creature_PartialGibb(creature, 2, 0);
            break;
        }
        creature->controls->unk32 = 0;
        creature->unkC8 = 2.0f;
    } else {
        switch (state) {
        case 4:
        case 7:
            break;
        default:
            Creature_MotionStateInit(creature, 4, 1, 0);
            break;
        }
        creature->unkC8 = 5.0f;
    }
}

s32 Creature_Move(Creature *creature)
{
    extern s32 g_iDeltaTimeMS;
    extern f32 g_fDeltaTimeMS;
    f32 Gfx_GetMinCameraDistSquared(void *pos);
    f32 dt;
    f32 dist;

    if (creature->unk1C0 < 1.0f)
    {
        return 1;
    }
    if (creature->unk1B0 <= 0)
    {
        dt = 500.0f;
        creature->pos = creature->startPos;
        dist = Sqrt(Gfx_GetMinCameraDistSquared(&creature->pos));
        if (dist > 900.0f)
        {
            creature->unk1B0 = RandomAdd_s32(500, 50);
        }
        else
        {
            dt = ((dist - 60.0f) / 840.0f) * dt + RandomAdd(0.0f, 50.0f);
            creature->unk1B0 = dt;
        }
        if (creature->unk1B0 < g_iDeltaTimeMS)
        {
            creature->unk1B0 = g_iDeltaTimeMS;
            dt = g_fDeltaTimeMS;
        }
        Creature_MacroStep(creature, 0.001f * dt);
    }
    dt = g_fDeltaTimeMS;
    if (creature->unk1B0 < g_iDeltaTimeMS)
    {
        dt = creature->unk1B0;
    }
    Creature_MicroStep(creature, 0.001f * dt);
    creature->unk1B0 -= g_iDeltaTimeMS;
    if ((s32)creature->unk78 < 0)
    {
        creature->unk1B4 += g_iDeltaTimeMS;
        if (*(u8 *)(creature->motion + 0x1C) == 2)
        {
            creature->flags &= ~0x20;
        }
        if (creature->unk1B4 > 20000)
        {
            MotionState_Release(HND(creature->motion));
            Thing_Release(HND(creature));
            return 0;
        }
    }
    return 1;
}

void Creature_MicroStep(void *arg0, f32 dt) {
    struct CreatureModel {
        u8 unk0[0x14];
        u32 *numBones;
    };
    struct Matrix4 {
        Vector4 v[4];
    };
    struct Creature {
        u8 unk0[0x40];
        Vector4 pos;
        u8 unk50[0x74 - 0x50];
        u32 flags;
        u8 unk78[0xCC - 0x78];
        struct CreatureModel *model;
        u8 unkD0[0xDC - 0xD0];
        struct Matrix4 *bones;
        u8 unkE0[0x1A0 - 0xE0];
        Vector4 vel;
    } *creature = arg0;
    Vector4 pos __attribute__((aligned(16)));
    u32 i;
    u32 *numBones;

    AccumulateVector(&creature->vel, &creature->pos, &pos, dt);
    creature->pos = pos;
    numBones = creature->model->numBones;
    for (i = 0; i < *numBones; i++) {
        creature->bones[i].v[3].x += creature->vel.x * dt;
        creature->bones[i].v[3].y += creature->vel.y * dt;
        creature->bones[i].v[3].z += creature->vel.z * dt;
    }
    creature->flags &= ~0x4000;
}

/* Creature_MacroStep's view of the creature: only the fields it touches */
struct CreatureAnimMS
{
    u8 unk0[0x18];
    f32 fps;
};
struct CreatureEntryMS
{
    u8 unk0[4];
    f32 forward;
    f32 side;
    f32 lift;
    f32 start;
    f32 end;
    u8 unk18;
    u8 unk19;
};
struct CreatureControlsMS
{
    s8 x;
    u8 unk1[3];
    f32 angle;
    f32 speed;
    u8 unkC[0x6C - 0xC];
    u32 flags;
};
struct CreatureModelMS
{
    u8 unk0[0x14];
    u32 *numBones;
};
struct CreatureMS
{
    Vector4 vel;
    Vector4 mtx[3];
    Vector4 pos;
    Vector4 quat;
    u8 unk60[0x74 - 0x60];
    u32 flags;
    s32 unk78;
    u8 unk7C[0x94 - 0x7C];
    struct CreatureControlsMS *controls;
    u8 unk98[0xB0 - 0x98];
    f32 angle;
    f32 angVel;
    u8 unkB8[0xBF - 0xB8];
    s8 onGround;
    u8 unkC0[0xC8 - 0xC0];
    f32 unkC8;
    struct CreatureModelMS *model;
    u8 unkD0[0xDC - 0xD0];
    u8 *bones;
    u8 unkE0[0xE4 - 0xE0];
    struct CreatureAnimMS *anim;
    u8 unkE8;
    u8 unkE9;
    u8 unkEA[0xEC - 0xEA];
    f32 time;
    f32 weight;
    f32 unkF4;
    f32 unkF8;
    u32 unkFC;
    u8 unk100[0x104 - 0x100];
    struct CreatureAnimMS *anim2;
    u8 unk108[0x110 - 0x108];
    f32 unk110;
    u8 unk114[0x118 - 0x114];
    struct CreatureEntryMS *entry;
    u8 unk11C[0x120 - 0x11C];
    Vector4 normal;
    f32 unk130;
    u8 unk134[0x190 - 0x134];
    Vector4 newPos;
    Vector4 newVel;
    s32 unk1B0;
    u8 unk1B4[0x1C0 - 0x1B4];
    f32 unk1C0;
    u8 unk1C4[0x1C8 - 0x1C4];
    u32 unk1C8;
};
extern f32 g_fDeltaTimeSec;
extern s32 g_iDeltaTimeMS;


void Creature_MacroStep(void *arg0, f32 dt) {
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    struct CreatureAnim {
        u8 unk0[0x18];
        f32 fps;
    };
    struct CreatureEntry {
        u8 unk0[4];
        f32 forward;
        f32 side;
        f32 lift;
        f32 start;
        f32 end;
        u8 unk18;
        u8 unk19;
    };
    struct CreatureControls {
        s8 x;
        u8 unk1[3];
        f32 angle;
        f32 speed;
        u8 unkC[0x6C - 0xC];
        u32 flags;
    };
    struct CreatureModel {
        u8 unk0[0x14];
        u32 *numBones;
    };
    struct Creature {
        Vector4 vel;
        Vector4 mtx[3];
        Vector4 pos;
        Vector4 quat;
        u8 unk60[0x74 - 0x60];
        u32 flags;
        s32 unk78;
        u8 unk7C[0x94 - 0x7C];
        struct CreatureControls *controls;
        u8 unk98[0xB0 - 0x98];
        f32 angle;
        f32 angVel;
        u8 unkB8[0xBF - 0xB8];
        s8 onGround;
        u8 unkC0[0xC8 - 0xC0];
        f32 unkC8;
        struct CreatureModel *model;
        u8 unkD0[0xDC - 0xD0];
        u8 *bones;
        u8 unkE0[0xE4 - 0xE0];
        struct CreatureAnim *anim;
        u8 unkE8;
        u8 unkE9;
        u8 unkEA[0xEC - 0xEA];
        f32 time;
        f32 weight;
        f32 unkF4;
        f32 unkF8;
        u32 unkFC;
        u8 unk100[0x104 - 0x100];
        struct CreatureAnim *anim2;
        u8 unk108[0x110 - 0x108];
        f32 unk110;
        u8 unk114[0x118 - 0x114];
        struct CreatureEntry *entry;
        u8 unk11C[0x120 - 0x11C];
        Vector4 normal;
        f32 unk130;
        u8 unk134[0x190 - 0x134];
        Vector4 newPos;
        Vector4 newVel;
        s32 unk1B0;
        u8 unk1B4[0x1C0 - 0x1B4];
        f32 unk1C0;
        u8 unk1C4[0x1C8 - 0x1C4];
        u32 unk1C8;
    } *creature = arg0;
    struct {
        Vector4 point;
        Vector4 normal;
        u8 unk20[0x30];
    } hit __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    f32 t;
    f32 savedDt = g_fDeltaTimeSec;
    f32 travelled;
    f32 speed;
    f32 forward;
    f32 side;
    f32 dot;
    f32 diff;
    f32 step;
    f32 a;
    f32 b;
    f32 w;
    f32 inv;
    f32 c2;
    f32 s2;
    f32 m;
    u32 count;
    u32 i;
    struct CreatureControls *controls;

    if (creature->unk1C0 < 1.0f) {
        return;
    }
    TimeHandler_SetTimeElapsedSec_f32(dt);
    Creature_InterpretControls(creature);
    if (creature->anim != NULL && !creature->entry->unk19 && creature->time >= creature->entry->start &&
        creature->time < creature->entry->end) {
        speed = creature->anim->fps;
        forward = creature->entry->forward;
        side = creature->entry->side;
        if (creature->controls != NULL && (creature->controls->flags & 0x100)) {
            speed *= creature->controls->speed;
        }
        creature->vel.x = speed * (forward * creature->mtx[0].x + side * creature->mtx[2].x);
        creature->vel.z = speed * (forward * creature->mtx[0].z + side * creature->mtx[2].z);
    }
    creature->vel.y += -60.0f * g_fDeltaTimeSec;
    if (!(creature->flags & 4)) {
        travelled = 0.0f;
        count = 0;
        pos = creature->pos;
        creature->onGround = 0;
        do {
            s8 hitAny;
            t = g_fDeltaTimeSec - travelled;
            hitAny = RayHitsTerrainAndObjects(&pos, creature, &t, &hit);
            pos.x += creature->vel.x * t;
            pos.y += creature->vel.y * t;
            pos.z += creature->vel.z * t;
            if (hitAny) {
                dot = -PSVECDotProduct(&hit.normal, creature);
                creature->vel.x += dot * hit.normal.x;
                creature->vel.y += dot * hit.normal.y;
                creature->vel.z += dot * hit.normal.z;
                pos.x += hit.normal.x * creature->unk130;
                pos.y += hit.normal.y * creature->unk130;
                pos.z += hit.normal.z * creature->unk130;
                if (hit.normal.y > 0.3f) {
                    creature->onGround = 1;
                    creature->normal = hit.normal;
                }
            }
            travelled += t;
            if (count++ > 6) {
                travelled = g_fDeltaTimeSec;
            }
        } while (travelled < g_fDeltaTimeSec);
        creature->newVel.x = 1000.0f * (pos.x - creature->pos.x) / (f32)creature->unk1B0;
        creature->newVel.y = 1000.0f * (pos.y - creature->pos.y) / (f32)creature->unk1B0;
        creature->newVel.z = 1000.0f * (pos.z - creature->pos.z) / (f32)creature->unk1B0;
        creature->newPos = pos;
        if (!creature->onGround) {
            Vector4 start __attribute__((aligned(16))) = creature->pos;
            f32 len = 1.0f;
            Vector4 down __attribute__((aligned(16))) = {0.0f, -10.5f, 0.0f, 0.0f};

            start.y += 10.0f;
            creature->onGround = RayHitsTerrainAndObjects(&start, &down, &len, &hit);
            if (creature->onGround) {
                creature->normal = hit.normal;
            }
        }
    }
    controls = creature->controls;
    if (!(controls->flags & 0x100)) {
        if (!(creature->flags & 0x10) && creature->unk78 > 0) {
            if (controls->x > 0x20) {
                creature->angVel -= 0.3f * g_fDeltaTimeSec * (f32)(controls->x - 0x20);
            } else if (controls->x < -0x20) {
                creature->angVel -= 0.3f * g_fDeltaTimeSec * (f32)(controls->x + 0x20);
            }
        }
        if (creature->angVel > 5.0f) {
            creature->angVel = 5.0f;
        } else if (creature->angVel < -5.0f) {
            creature->angVel = -5.0f;
        }
        if (creature->onGround) {
            creature->angVel *= GetDampFactor(8, g_iDeltaTimeMS);
        }
        creature->angle += creature->angVel * g_fDeltaTimeSec;
    } else if (!(creature->flags & 0x10) && creature->unk78 > 0) {
        diff = controls->angle - creature->angle;
        step = 10.0f * g_fDeltaTimeSec;
        while (diff > 3.1415927f) {
            diff -= 6.2831855f;
        }
        while (diff < -3.1415927f) {
            diff += 6.2831855f;
        }
        if (FloatsEqual(diff, 0.0f, step)) {
            creature->angle = creature->controls->angle;
        } else if (diff > 0.0f) {
            creature->angle += step;
        } else {
            creature->angle -= step;
        }
    }
    SinCosRad(&creature->quat.y, &creature->quat.w, 0.5f * creature->angle);
    creature->quat.x = 0.0f;
    creature->quat.z = 0.0f;
    c2 = creature->quat.w * creature->quat.w;
    s2 = creature->quat.y * creature->quat.y;
    inv = 1.0f / (s2 + c2);
    m = inv * (c2 - s2);
    inv *= 2.0f;
    creature->mtx[0].x = m;
    creature->mtx[1].y = 1.0f;
    creature->mtx[2].z = m;
    creature->mtx[0].y = 0.0f;
    creature->mtx[0].z = inv * (creature->quat.w * creature->quat.y);
    creature->mtx[1].x = 0.0f;
    creature->mtx[1].z = 0.0f;
    creature->mtx[2].x = inv * (creature->quat.w * creature->quat.y);
    creature->mtx[2].y = 0.0f;
    creature->mtx[2].w = 0.0f;
    creature->mtx[1].w = 0.0f;
    creature->mtx[0].w = 0.0f;
    creature->pos.w = 1.0f;
    if (!(creature->unk1C0 < 1.0f)) {
        creature->weight += creature->unkC8 * g_fDeltaTimeSec;
        if (creature->weight > 1.0f) {
            creature->weight = 1.0f;
        }
        w = creature->weight;
        a = 0.0f;
        b = a;
        w /= w + creature->unk110;
        if (creature->anim != NULL) {
            a = creature->anim->fps;
        }
        if (creature->anim2 != NULL) {
            b = creature->anim2->fps;
        }
        b += w * (a - b);
        if (creature->controls != NULL && (creature->controls->flags & 0x100)) {
            b *= creature->controls->speed;
        }
        Motion_BlendHeartbeat_FPS(&creature->anim, 2, b);
        if (creature->unkFC != 0) {
            creature->flags &= ~0x10;
        }
        creature->weight = 0.0f;
        Motion_HeartbeatB2(&creature->model, creature);
    }
    if (creature->entry->unk19) {
        if (creature->unkF4 < creature->entry->start && creature->unkF8 > creature->entry->start) {
            forward = creature->entry->forward;
            speed = creature->anim->fps;
            side = creature->entry->side;
            if (creature->controls != NULL && (creature->controls->flags & 0x100)) {
                speed *= creature->controls->speed;
            }
            creature->vel.x += speed * (forward * creature->mtx[0].x + side * creature->mtx[2].x);
            creature->vel.y += creature->entry->lift;
            creature->vel.z += speed * (forward * creature->mtx[0].z + side * creature->mtx[2].z);
        }
        if (creature->onGround) {
            if (creature->unkF8 > creature->entry->end) {
                creature->vel.x = 0.0f;
                creature->vel.z = 0.0f;
            }
        } else {
            creature->unkE9 = 0;
        }
    }
    MotionState_BuildMatricesWorld(&creature->quat, &creature->pos, &creature->model, 1.0f);
    if (creature->unk1C8 != 0) {
        u32 *numBones = creature->model->numBones;
        for (i = 0; i < *numBones; i++) {
            if (creature->unk1C8 & (1 << i)) {
                memset(creature->bones + i * 0x40, 0, 0x30);
            }
        }
    }
    Thing_PassiveTargeting(creature);
    TimeHandler_SetTimeElapsedSec_f32(savedDt);
}


void Creature_Draw(Creature *creature) {
    if (creature->unk1C0 > 0.0f) {
        MotionState_Draw(creature->motion);
    }
    creature->flags |= 0x4000;
}

s32 Creature_ProcessItemPickup(u8 *creature, void *item)
{
    void *rack;
    void *motion;

    switch (*SLOT_LOAD(u8 *, (u8 *)(item) + 0xC0))
    {
    case 0:
    case 1:
    case 2:
        break;
    case 3:
        if ((rack = ThingManagerList_GetByType(creature + 0xE0, 8)) == NULL)
        {
            break;
        }
        WildCardRack_Add(rack, SLOT_LOAD(u8 *, (u8 *)(item) + 0xC0), M2C_FIELD(item, s16 *, 0xCE));
        Item_Release(item);
        motion = SLOT_LOAD(void *, (u8 *)(item) + 0xD0);
        if (motion != NULL)
        {
            M2C_FIELD(motion, s32 *, 0x18) = M2C_FIELD(motion, s32 *, 0x14);
        }
        return 1;
    case 4:
        break;
    }
    return 0;
}

u32 Creature_Invalidate(Creature *creature, u32 start, u32 end) {
    if ((u32)HND(creature->anims) >= start && (u32)HND(creature->anims) < end) {
        return 1;
    }
    return 0;
}
