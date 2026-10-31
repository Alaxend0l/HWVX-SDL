/* Creatures (Creature.h): animated walkers driven by their player's controls,
 * with physics in distance-dependent macro steps interpolated by micro steps. */
#include "common.h"
#include "AI.h"
#include "CarA.h"
#include "World.h"
#include "ThingManager.h"
#include "Debris.h"

/* The player whose controls the creature follows. */
#define CREATURE_CONTROLS(c) ((Player *)GETP((c)->base.player))

/* The index of the damage part on a bone, or -1. */
static inline u32 Creature_FindDamagePart(DamagePartList *list, u32 bone)
{
    u32 i;

    for (i = 0; i < list->count; i++)
    {
        if (bone == GETP(list->parts)[i].bone)
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

/* The skeleton of the model being played. */
static inline CreatureSkeleton *Creature_Skeleton(Creature *creature)
{
    return GETP(((MotionAnim *)GETP(creature->model))->skeleton);
}


void Creature_GetTargetPos(Creature *creature, Vector4 *pos) {
    pos->x = creature->base.pos.x;
    pos->y = 4.0f + creature->base.pos.y;
    pos->z = creature->base.pos.z;
    pos->w = 1.0f;
}

f32 Creature_GetMass(Creature *creature) {
    return creature->mass;
}

/* Starts the death animation deathAnim (the first animation if there is
 * none), once. */
void Creature_MotionStateInitDeath(Creature *creature) {
    void *motion;
    s32 state;
    u32 index;
    CreatureAnimEntry *entry;
    CreatureInfo *info;

    motion = &creature->motion;
    index = creature->deathAnim;
    state = 0xFFFF;
    if (creature->deadTime == 0) {
        info = GETP(creature->info);
        if (GETP(info->deathAnims) != NULL) {
            entry = GETP(info->deathAnims) + index;
            if (index >= info->numDeathAnims || entry->model == 0) {
                entry = GETP(info->anims);
                state = 0;
                index = 0;
            }
            creature->model.h = entry->model;
            MotionState_Init(motion, 0);
            creature->animState = state;
            creature->animIndex = index;
            SETP(creature->animEntry, entry);
            creature->base.flags |= CREATURE_FLAG_DYING;
            if (GETP(creature->base.trigger) != NULL) {
                SETP(((TileCreature *)GETP(creature->base.trigger))->creature, NULL);
            }
            creature->deathAnim = 0;
        }
    }
}


/* Plays the animation of a state (CREATURE_ANIM_*). reset: blend from the
 * current one, and do nothing if it is that state already and unfinished;
 * keepTime: keep the animation time. Busy animations aren't interrupted. */
void Creature_MotionStateInit(void *arg0, s32 state, u8 reset, u8 keepTime)
{
    Creature *creature;
    CreatureInfo *info;
    CreatureAnimEntry *entry;
    f32 time;
    u32 index;
    void *motion;

    creature = arg0;
    time = creature->animTime;
    motion = &creature->motion;
    index = 0;
    if (reset && state == creature->animState && creature->animStatus != 2)
    {
        creature->unkE9 = 0;
        return;
    }
    if (creature->base.flags & CREATURE_FLAG_BUSY)
    {
        return;
    }
    creature->base.flags &= ~CREATURE_FLAG_DYING;
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
        creature->base.flags |= CREATURE_FLAG_BUSY;
        break;
    case 8:
        index = 9;
        creature->base.flags |= CREATURE_FLAG_BUSY;
        break;
    case 9:
        index = 10;
        break;
    case 10:
        index = 0x10;
        creature->base.flags |= CREATURE_FLAG_BUSY;
        break;
    case 11:
        index = 0x16;
        creature->base.flags |= CREATURE_FLAG_BUSY;
        break;
    case 14:
        index = 0xE;
        creature->turnRate = 0.0f;
        creature->base.flags |= CREATURE_FLAG_BUSY;
        break;
    case 15:
        index = 0xF;
        creature->base.flags |= CREATURE_FLAG_BUSY;
        break;
    }
    info = GETP(creature->info);
    entry = GETP(info->anims) + index;
    if (index >= info->numAnims || entry->model == 0)
    {
        entry = GETP(info->anims);
        state = 0;
        index = 0;
    }
    creature->model.h = entry->model;
    if (!reset)
    {
        creature->animWeight = 0.0f;
        creature->animIndex = 0xFFFF;
    }
    MotionState_Init(motion, reset);
    creature->animState = state;
    creature->animIndex = index;
    SETP(creature->animEntry, entry);
    if (keepTime)
    {
        creature->animTime = time;
    }
}


s32 Creature_Init(Creature *creature, CreatureInit *init)
{
    Thing_Init(&creature->base, 0);
    SETP(creature->info, GETP(init->info));
    creature->base.pos = init->pos;
    creature->stepPos = creature->base.pos;
    creature->mass = RandomAdd(0.002f, 0.001f);
    creature->unk1B8 = 0;
    creature->gibbedBones = 0;
    QuaternionToSubMatrix(&creature->base.quat, creature->base.axis);
    creature->base.collisionType = THING_COLLIDE_NONE;
    creature->base.radius = 11.0f;
    creature->heading = init->heading;
    creature->turnRate = 0.0f;
    creature->unkC0 = 0.0f;
    creature->deadTime = 0;
    creature->base.health = GETP(creature->info)->health;
    Creature_MotionStateInit(creature, 0, 0, 0);
    creature->onGround = 0;
    creature->skin = 0.03f;
    creature->stepTimer = -1;
    creature->unk1C0 = 1.0f;
    Thing_AddToGrid(creature, 6);
    MotionState_BuildMatricesWorld(&creature->base.quat, &creature->base.pos, &creature->motion, 1.0f);
    return 1;
}

void Creature_Release(Creature *creature) {
    MotionState_Release(&creature->motion);
    Thing_Release(creature);
}

void Creature_PartialGibb(Creature *creature, u32 count, s32 unused) {
    extern Vector4 g_zeroVector;
    AnimNode nodes[64];
    DebrisPartParams partParams;
    DebrisParams debrisParams;
    EmitterParams slaveParams;
    Vector4 pos __attribute__((aligned(16)));
    Vector4 vel __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 quat __attribute__((aligned(16)));
    u32 i;
    u32 j;
    u32 blown;
    CreatureSkeleton *anim;
    DamagePartList *list;
    f32 scale;
    Thing *thing;
    Thing *slave;
    u32 debris;

    blown = 0;
    anim = Creature_Skeleton(creature);
    list = GETP(GETP(creature->info)->damageParts);
    if (list == NULL) {
        return;
    }
    memcpy(nodes, GETP(anim->nodes), anim->numBones << 5);
    for (i = 0; i < anim->numBones; i++) {
        nodes[i].flags = 0;
        if (creature->gibbedBones & (1 << i)) {
            nodes[i].flags |= 1;
        }
        blown++;
    }
    do {
        i = RandomAdd_s32(2, anim->numBones - 2);
        blown += AnimNodes_BlowBranch(&nodes[i]);
    } while (blown < count);
    for (i = 0; i < anim->numBones; i++) {
        if (!(nodes[i].flags & 2)) {
            continue;
        }
        j = Creature_FindDamagePart(list, i);
        if (j == -1) {
            continue;
        }
        if (GETP(list->parts)[j].useOffset) {
            XformVec((u8 *)&GETP(creature->bones)[i], GETP(GETP(list->parts)[j].offset), &pos);
        } else {
            pos = GETP(creature->bones)[i].v[3];
        }
        if (GETP(list->parts)[j].spawned == 0) {
            scale = RandomNumberGenerator_GetRandomFloat(3.0f, 20.0f);
            RandomNumberGenerator_GetRandomVector(&dir);
            vel = creature->base.vel;
            vel.x = dir.x * scale + vel.x;
            vel.y = dir.y * scale + vel.y;
            vel.z = dir.z * scale + vel.z;
            SubMatrixToQuaternion((u8 *)&GETP(creature->bones)[i], &quat);
            creature->gibbedBones |= 1 << i;
            debris = GETP(list->parts)[j].debris;
            partParams.life = 3000;
            partParams.gravity = -50.0f;
            partParams.vel = vel;
            partParams.pos = pos;
            SETP(partParams.geometry, GETP(GETP(list->parts)[j].geom));
            partParams.quat = *(Quaternion *)&quat;
            partParams.moves = 1;
            if (debris != 0) {
                debrisParams.debris = debris;
                debrisParams.pos = pos;
                debrisParams.quat = quat;
                debrisParams.vel = vel;
                debrisParams.owner = 0;
                debrisParams.unk48 = 0.0f;
                debrisParams.unk4C = 0.0f;
                debrisParams.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &debrisParams);
            }
            RandomNumberGenerator_GetRandomVector(&partParams.spinAxis);
            partParams.spinAxis.w = 0.017453292f * RandomAdd(100.0f, 1260.0f);
            thing = ThingManager_DARYL_CreateThing(THING_TYPE_DEBRIS, &partParams);
            if (thing != NULL && GETP(GETP(list->parts)[j].slaveInfo) != NULL) {
                slaveParams.drawDist = 0.0f;
                slaveParams.quat.w = 1.0f;
                slaveParams.quat.x = 0.0f;
                slaveParams.quat.y = 0.0f;
                slaveParams.quat.z = 0.0f;
                slaveParams.pos = Vector4_OrZero(GETP(GETP(list->parts)[j].slavePos));
                SETP(slaveParams.emitter, GETP(GETP(list->parts)[j].slaveInfo));
                slaveParams.owner = 0;
                slaveParams.vel = g_zeroVector;
                slave = ThingManager_DARYL_CreateSlave(THING_TYPE_EMITTER, &slaveParams, 0xFF);
                if (slave != NULL) {
                    slave->slaveTag = 0xFF;
                    ThingManagerList_Append(&thing->slaves, slave);
                }
            }
        }
    }
}
void Creature_InterpretControls(void *arg0) {
    Creature *creature = arg0;
    s32 state;

    state = creature->animState;
    if (creature->base.health < 0) {
        return;
    }
    if (creature->onGround) {
        if (!((state == 4 || state == 10) &&
              creature->animTime < GETP(GETP(creature->anim)->phases[2])->end)) {
            if (CREATURE_CONTROLS(creature)->buttonFire) {
                Creature_MotionStateInit(creature, 7, 1, 0);
            } else if (CREATURE_CONTROLS(creature)->button1A) {
                Creature_MotionStateInit(creature, 9, 1, 0);
            } else if (CREATURE_CONTROLS(creature)->stickY > 0x20) {
                if (CREATURE_CONTROLS(creature)->button14) {
                    Creature_MotionStateInit(creature, 2, 1, 1);
                } else {
                    Creature_MotionStateInit(creature, 1, 1, 1);
                }
            } else if (CREATURE_CONTROLS(creature)->stickY < -0x20) {
                Creature_MotionStateInit(creature, 5, 1, 1);
            } else if (CREATURE_CONTROLS(creature)->stickX > 0x20) {
                Creature_MotionStateInit(creature, 6, 1, 1);
            } else {
                Creature_MotionStateInit(creature, 0, 1, 1);
            }
        }
        switch (CREATURE_CONTROLS(creature)->combo) {
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
        CREATURE_CONTROLS(creature)->combo = 0;
        creature->blendRate = 2.0f;
    } else {
        switch (state) {
        case 4:
        case 7:
            break;
        default:
            Creature_MotionStateInit(creature, 4, 1, 0);
            break;
        }
        creature->blendRate = 5.0f;
    }
}

s32 Creature_Move(Creature *creature)
{
    extern s32 g_iDeltaTimeMS;
    extern f32 g_fDeltaTimeMS;
    f32 dt;
    f32 dist;

    if (creature->unk1C0 < 1.0f)
    {
        return 1;
    }
    if (creature->stepTimer <= 0)
    {
        dt = 500.0f;
        creature->base.pos = creature->stepPos;
        dist = Sqrt(Gfx_GetMinCameraDistSquared(&creature->base.pos));
        if (dist > 900.0f)
        {
            creature->stepTimer = RandomAdd_s32(500, 50);
        }
        else
        {
            dt = ((dist - 60.0f) / 840.0f) * dt + RandomAdd(0.0f, 50.0f);
            creature->stepTimer = dt;
        }
        if (creature->stepTimer < g_iDeltaTimeMS)
        {
            creature->stepTimer = g_iDeltaTimeMS;
            dt = g_fDeltaTimeMS;
        }
        Creature_MacroStep(creature, 0.001f * dt);
    }
    dt = g_fDeltaTimeMS;
    if (creature->stepTimer < g_iDeltaTimeMS)
    {
        dt = creature->stepTimer;
    }
    Creature_MicroStep(creature, 0.001f * dt);
    creature->stepTimer -= g_iDeltaTimeMS;
    if (creature->base.health < 0)
    {
        creature->deadTime += g_iDeltaTimeMS;
        if (creature->animStatus == 2)
        {
            creature->base.flags &= ~CREATURE_FLAG_DYING;
        }
        if (creature->deadTime > 20000)
        {
            MotionState_Release(&creature->motion);
            Thing_Release(creature);
            return 0;
        }
    }
    return 1;
}

void Creature_MicroStep(void *arg0, f32 dt) {
    Creature *creature = arg0;
    Vector4 pos __attribute__((aligned(16)));
    u32 i;
    u32 *numBones;

    AccumulateVector(&creature->stepVel, &creature->base.pos, &pos, dt);
    creature->base.pos = pos;
    numBones = &GETP(GETP(creature->model)->skeleton)->numBones;
    for (i = 0; i < *numBones; i++) {
        GETP(creature->bones)[i].v[3].x += creature->stepVel.x * dt;
        GETP(creature->bones)[i].v[3].y += creature->stepVel.y * dt;
        GETP(creature->bones)[i].v[3].z += creature->stepVel.z * dt;
    }
    creature->base.flags &= ~CREATURE_FLAG_DRAWN;
}

extern f32 g_fDeltaTimeSec;
extern s32 g_iDeltaTimeMS;


void Creature_MacroStep(void *arg0, f32 dt) {
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    Creature *creature = arg0;
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
    Player *controls;

    if (creature->unk1C0 < 1.0f) {
        return;
    }
    TimeHandler_SetTimeElapsedSec_f32(dt);
    Creature_InterpretControls(creature);
    if (GETP(creature->anim) != NULL && !GETP(creature->animEntry)->impulse && creature->animTime >= GETP(creature->animEntry)->start &&
        creature->animTime < GETP(creature->animEntry)->end) {
        speed = GETP(creature->anim)->fps;
        forward = GETP(creature->animEntry)->forward;
        side = GETP(creature->animEntry)->side;
        if (CREATURE_CONTROLS(creature) != NULL && (CREATURE_CONTROLS(creature)->flags & PLAYER_FLAG_AI_HEADING)) {
            speed *= CREATURE_CONTROLS(creature)->aiSpeed;
        }
        creature->base.vel.x = speed * (forward * creature->base.axis[0].x + side * creature->base.axis[2].x);
        creature->base.vel.z = speed * (forward * creature->base.axis[0].z + side * creature->base.axis[2].z);
    }
    creature->base.vel.y += -60.0f * g_fDeltaTimeSec;
    if (!(creature->base.flags & CREATURE_FLAG_NO_PHYSICS)) {
        travelled = 0.0f;
        count = 0;
        pos = creature->base.pos;
        creature->onGround = 0;
        do {
            s8 hitAny;
            t = g_fDeltaTimeSec - travelled;
            hitAny = RayHitsTerrainAndObjects(&pos, &creature->base.vel, &t, &hit);
            pos.x += creature->base.vel.x * t;
            pos.y += creature->base.vel.y * t;
            pos.z += creature->base.vel.z * t;
            if (hitAny) {
                dot = -PSVECDotProduct(&hit.normal, &creature->base.vel);
                creature->base.vel.x += dot * hit.normal.x;
                creature->base.vel.y += dot * hit.normal.y;
                creature->base.vel.z += dot * hit.normal.z;
                pos.x += hit.normal.x * creature->skin;
                pos.y += hit.normal.y * creature->skin;
                pos.z += hit.normal.z * creature->skin;
                if (hit.normal.y > 0.3f) {
                    creature->onGround = 1;
                    creature->groundNormal = hit.normal;
                }
            }
            travelled += t;
            if (count++ > 6) {
                travelled = g_fDeltaTimeSec;
            }
        } while (travelled < g_fDeltaTimeSec);
        creature->stepVel.x = 1000.0f * (pos.x - creature->base.pos.x) / (f32)creature->stepTimer;
        creature->stepVel.y = 1000.0f * (pos.y - creature->base.pos.y) / (f32)creature->stepTimer;
        creature->stepVel.z = 1000.0f * (pos.z - creature->base.pos.z) / (f32)creature->stepTimer;
        creature->stepPos = pos;
        if (!creature->onGround) {
            Vector4 start __attribute__((aligned(16))) = creature->base.pos;
            f32 len = 1.0f;
            Vector4 down __attribute__((aligned(16))) = {0.0f, -10.5f, 0.0f, 0.0f};

            start.y += 10.0f;
            creature->onGround = RayHitsTerrainAndObjects(&start, &down, &len, &hit);
            if (creature->onGround) {
                creature->groundNormal = hit.normal;
            }
        }
    }
    controls = CREATURE_CONTROLS(creature);
    if (!(controls->flags & PLAYER_FLAG_AI_HEADING)) {
        if (!(creature->base.flags & CREATURE_FLAG_BUSY) && creature->base.health > 0) {
            if (controls->stickX > 0x20) {
                creature->turnRate -= 0.3f * g_fDeltaTimeSec * (f32)(controls->stickX - 0x20);
            } else if (controls->stickX < -0x20) {
                creature->turnRate -= 0.3f * g_fDeltaTimeSec * (f32)(controls->stickX + 0x20);
            }
        }
        if (creature->turnRate > 5.0f) {
            creature->turnRate = 5.0f;
        } else if (creature->turnRate < -5.0f) {
            creature->turnRate = -5.0f;
        }
        if (creature->onGround) {
            creature->turnRate *= GetDampFactor(8, g_iDeltaTimeMS);
        }
        creature->heading += creature->turnRate * g_fDeltaTimeSec;
    } else if (!(creature->base.flags & CREATURE_FLAG_BUSY) && creature->base.health > 0) {
        diff = controls->aiHeading - creature->heading;
        step = 10.0f * g_fDeltaTimeSec;
        while (diff > 3.1415927f) {
            diff -= 6.2831855f;
        }
        while (diff < -3.1415927f) {
            diff += 6.2831855f;
        }
        if (FloatsEqual(diff, 0.0f, step)) {
            creature->heading = CREATURE_CONTROLS(creature)->aiHeading;
        } else if (diff > 0.0f) {
            creature->heading += step;
        } else {
            creature->heading -= step;
        }
    }
    SinCosRad(&creature->base.quat.y, &creature->base.quat.w, 0.5f * creature->heading);
    creature->base.quat.x = 0.0f;
    creature->base.quat.z = 0.0f;
    c2 = creature->base.quat.w * creature->base.quat.w;
    s2 = creature->base.quat.y * creature->base.quat.y;
    inv = 1.0f / (s2 + c2);
    m = inv * (c2 - s2);
    inv *= 2.0f;
    creature->base.axis[0].x = m;
    creature->base.axis[1].y = 1.0f;
    creature->base.axis[2].z = m;
    creature->base.axis[0].y = 0.0f;
    creature->base.axis[0].z = inv * (creature->base.quat.w * creature->base.quat.y);
    creature->base.axis[1].x = 0.0f;
    creature->base.axis[1].z = 0.0f;
    creature->base.axis[2].x = inv * (creature->base.quat.w * creature->base.quat.y);
    creature->base.axis[2].y = 0.0f;
    creature->base.axis[2].w = 0.0f;
    creature->base.axis[1].w = 0.0f;
    creature->base.axis[0].w = 0.0f;
    creature->base.pos.w = 1.0f;
    if (!(creature->unk1C0 < 1.0f)) {
        creature->animWeight += creature->blendRate * g_fDeltaTimeSec;
        if (creature->animWeight > 1.0f) {
            creature->animWeight = 1.0f;
        }
        w = creature->animWeight;
        a = 0.0f;
        b = a;
        w /= w + creature->prevAnimWeight;
        if (GETP(creature->anim) != NULL) {
            a = GETP(creature->anim)->fps;
        }
        if (GETP(creature->prevAnim) != NULL) {
            b = GETP(creature->prevAnim)->fps;
        }
        b += w * (a - b);
        if (CREATURE_CONTROLS(creature) != NULL && (CREATURE_CONTROLS(creature)->flags & PLAYER_FLAG_AI_HEADING)) {
            b *= CREATURE_CONTROLS(creature)->aiSpeed;
        }
        Motion_BlendHeartbeat_FPS(&creature->motion.layer, 2, NULL, b);
        if (creature->animLooped != 0) {
            creature->base.flags &= ~CREATURE_FLAG_BUSY;
        }
        creature->animWeight = 0.0f;
        Motion_HeartbeatB2(&creature->motion, creature);
    }
    if (GETP(creature->animEntry)->impulse) {
        if (creature->animPrevTime < GETP(creature->animEntry)->start && creature->animCurTime > GETP(creature->animEntry)->start) {
            forward = GETP(creature->animEntry)->forward;
            speed = GETP(creature->anim)->fps;
            side = GETP(creature->animEntry)->side;
            if (CREATURE_CONTROLS(creature) != NULL && (CREATURE_CONTROLS(creature)->flags & PLAYER_FLAG_AI_HEADING)) {
                speed *= CREATURE_CONTROLS(creature)->aiSpeed;
            }
            creature->base.vel.x += speed * (forward * creature->base.axis[0].x + side * creature->base.axis[2].x);
            creature->base.vel.y += GETP(creature->animEntry)->lift;
            creature->base.vel.z += speed * (forward * creature->base.axis[0].z + side * creature->base.axis[2].z);
        }
        if (creature->onGround) {
            if (creature->animCurTime > GETP(creature->animEntry)->end) {
                creature->base.vel.x = 0.0f;
                creature->base.vel.z = 0.0f;
            }
        } else {
            creature->unkE9 = 0;
        }
    }
    MotionState_BuildMatricesWorld(&creature->base.quat, &creature->base.pos, &creature->motion, 1.0f);
    if (creature->gibbedBones != 0) {
        u32 *numBones = &GETP(GETP(creature->model)->skeleton)->numBones;
        for (i = 0; i < *numBones; i++) {
            if (creature->gibbedBones & (1 << i)) {
                memset(&GETP(creature->bones)[i], 0, 0x30);  /* rotation only: the bone collapses to its point */
            }
        }
    }
    Thing_PassiveTargeting(creature);
    TimeHandler_SetTimeElapsedSec_f32(savedDt);
}


void Creature_Draw(Creature *creature) {
    if (creature->unk1C0 > 0.0f) {
        MotionState_Draw(&creature->motion);
    }
    creature->base.flags |= CREATURE_FLAG_DRAWN;
}

/* Vtable ProcessItemPickup: creatures only take wildcards, into their rack. */
s32 Creature_ProcessItemPickup(Creature *creature, ItemThing *item)
{
    WildCardRack *rack;
    QuadItem *spawner;

    switch (GETP(item->record)->type)
    {
    case 0:
    case 1:
    case 2:
        break;
    case ITEM_TYPE_WILDCARD:
        if ((rack = ThingManagerList_GetByType(&creature->slaves, THING_TYPE_WILDCARD_RACK)) == NULL)
        {
            break;
        }
        WildCardRack_Add(rack, GETP(item->record), item->ammo);
        Item_Release(item);
        /* restart the spawner's respawn timer */
        spawner = GETP(item->spawner);
        if (spawner != NULL)
        {
            spawner->timer = spawner->respawn;
        }
        return 1;
    case 4:
        break;
    }
    return 0;
}

u32 Creature_Invalidate(Creature *creature, u32 start, u32 end) {
    if ((u32)HND(GETP(creature->info)) >= start && (u32)HND(GETP(creature->info)) < end) {
        return 1;
    }
    return 0;
}
