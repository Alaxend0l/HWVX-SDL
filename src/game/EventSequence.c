

/* Event sequences: the effect scripts behind explosions, sparks, pickup
 * flashes... A sequence is a Thing (THING_TYPE_EVENT_SEQ, made from
 * DebrisParams) that steps through its script, spawning particles,
 * projectiles, effects, debris, emitters, lights and sounds around itself,
 * with waits, repeats and random sections. */
#include "common.h"
#include "SoundSequence.h"
#include "ThingManager.h"
#include "Projectile.h"
#include "Debris.h"
#include "Destructible.h"
#include "Particle.h"
#include "CarA.h"

extern u8 g_CurrentViews;

/* A script entry: the kind, its object (a) and how to place it (b). */
typedef struct EventSeqEntry {
    u32 type;                   /* 0x00 EventSeq_Move */
    PTR32(void) a;              /* 0x04 the description spawned, or a value (repeat count, ms) */
    PTR32(struct EventSeqEmit) b; /* 0x08 */
} EventSeqEntry;

typedef struct EventSeqScript {
    u32 count;
    PTR32(EventSeqEntry) entries;
} EventSeqScript;

/* How many things an entry spawns and where: random count, speed and spin;
 * an offset (offsetType: 1 a box, 2 advanced) and a direction (dirType).
 * "Point" entries (lights, sounds, blasts...) read 0x18-0x1C differently. */
typedef struct EventSeqEmit {
    s32 countBase;              /* 0x00 */
    s32 countRange;             /* 0x04 */
    f32 speedBase;              /* 0x08 */
    f32 speedRange;             /* 0x0C */
    f32 spinBase;               /* 0x10 */
    f32 spinRange;              /* 0x14 */
    union {
        struct {
            f32 extraBase;      /* 0x18 per entry kind: gravity, life... */
            f32 extraRange;     /* 0x1C */
        };
        struct {
            u8 worldSpace;      /* 0x18 point entries: offset not rotated */
            u8 randomOffset;    /* 0x19 add a random offset from offsetRange */
            u8 pad1A[2];        /* 0x1A */
            PTR32(f32) offsetRange; /* 0x1C min/max x, y, z */
        };
    };
    u8 inheritVel;              /* 0x20 add the sequence's velocity... */
    u8 offsetType;              /* 0x21 */
    u8 dirType;                 /* 0x22 */
    u8 pad23;                   /* 0x23 */
    f32 inheritScale;           /* 0x24 ...times this */
    PTR32(void) offsetData;     /* 0x28 */
    PTR32(void) dirData;        /* 0x2C */
} EventSeqEmit;

_Static_assert(offsetof(EventSeqEmit, worldSpace) == 0x18, "EventSeqEmit.worldSpace");
_Static_assert(offsetof(EventSeqEmit, offsetRange) == 0x1C, "EventSeqEmit.offsetRange");
_Static_assert(offsetof(EventSeqEmit, inheritScale) == 0x24, "EventSeqEmit.inheritScale");
_Static_assert(offsetof(EventSeqEmit, dirData) == 0x2C, "EventSeqEmit.dirData");

typedef struct EventSeq {
    Thing base;                 /* 0x00 player: the owner credited with hits */
    u8 padAC[0x14];             /* 0xAC */
    Vector4 angVel;             /* 0xC0 */
    PTR32(EventSeqScript) script; /* 0xD0 */
    u32 step;                   /* 0xD4 entry */
    u32 loopStart;              /* 0xD8 */
    u32 loopCount;              /* 0xDC */
    u32 wait;                   /* 0xE0 ms */
    u32 randCount;              /* 0xE4 random sections left */
    u32 randStart;              /* 0xE8 */
    u32 randLength;             /* 0xEC */
    u8 randMode;                /* 0xF0 0: next entry, 1: jump into the section, 2: past it */
    s8 camDirty;                /* 0xF1 camDistSq needs recomputing */
    u8 padF2[2];                /* 0xF2 */
    f32 camDistSq;              /* 0xF4 */
    f32 speedScale;             /* 0xF8 moving sequences play faster */
    u8 padFC[4];                /* 0xFC */
} EventSeq;

_Static_assert(offsetof(EventSeq, angVel) == 0xC0, "EventSeq.angVel");
_Static_assert(offsetof(EventSeq, randMode) == 0xF0, "EventSeq.randMode");
_Static_assert(sizeof(EventSeq) == 0x100, "EventSeq size (ThingPools.eventSeqs)");




/* What a blast does to the things it reaches (BlastInfo.damage). */
typedef struct BlastDamage {
    u8 targets;                 /* 0x00 & 1: cars, 2: statics, 4: dynamics (also ray-collided things) */
    u8 occluded;                /* 0x01 the terrain shields */
    u8 pad02;                   /* 0x02 */
    u8 effect;                  /* 0x03 on cars: 2-6 stop a slave weapon (5 also repairs) */
    u8 pad04[8];                /* 0x04 */
    s32 damageType;             /* 0x0C */
    f32 force;                  /* 0x10 */
    u8 pad14[4];                /* 0x14 */
    u32 hitDebris;              /* 0x18 event sequence at each thing hit, 0: none */
} BlastDamage;

/* A "point" script entry's object for kind 5: a blast, flash and rumble. */
typedef struct BlastInfo {
    u8 blast;                   /* 0x00 Blast_Action */
    u8 flash;                   /* 0x01 FlashHolder_Add on things in range */
    u8 rumble[3];               /* 0x02 any: ForceFeedback_Play */
    u8 team;                    /* 0x05 0: spare the owner's things, 2: only them */
    u8 pad06[0x16];             /* 0x06 */
    PTR32(BlastDamage) damage;  /* 0x1C */
    u8 pad20[0x10];             /* 0x20 */
    PTR32(f32) radius;          /* 0x30 inner, outer: full effect inside inner */
} BlastInfo;

_Static_assert(offsetof(BlastDamage, hitDebris) == 0x18, "BlastDamage.hitDebris");
_Static_assert(offsetof(BlastInfo, damage) == 0x1C, "BlastInfo.damage");
_Static_assert(offsetof(BlastInfo, radius) == 0x30, "BlastInfo.radius");

/* Script entry kind 10: a sound. */
typedef struct EventSeqSound {
    u8 type;                    /* 0x00 0: positional with a range, 1: plain sample, else positional */
    u8 pad01[7];                /* 0x01 */
    f32 volume;                 /* 0x08 0-1 */
    f32 pitchMin;               /* 0x0C */
    f32 pitchMax;               /* 0x10 */
    u32 sample;                 /* 0x14 */
    PTR32(f32) range;           /* 0x18 near, far */
} EventSeqSound;

/* EventSeqEmit offset/direction data selecting one axis (0x10: 0/1 +-z,
 * 2/3 +-y, 4/5 +-x), with a scale. */
typedef struct EventSeqDir {
    Vector4 scale;              /* 0x00 */
    u8 axis;                    /* 0x10 */
} EventSeqDir;

/* Script entry kind 29: debris parts flying off from the sequence. */
typedef struct DebrisPartDesc {
    Vector4 dir;                /* 0x00 local launch direction and offset */
    f32 mass;                   /* 0x10 */
    f32 spinMin;                /* 0x14 degrees/s */
    f32 spinMax;                /* 0x18 */
    u32 geometry;               /* 0x1C 0: skipped */
    PTR32(void) slave;          /* 0x20 emitter description attached to it, or NULL */
    u8 pad24[0xC];              /* 0x24 */
} DebrisPartDesc;

typedef struct DebrisPartList {
    s32 lifeBase;               /* 0x00 ms */
    s32 lifeRange;              /* 0x04 */
    u32 count;                  /* 0x08 */
    PTR32(DebrisPartDesc) parts; /* 0x0C */
} DebrisPartList;

_Static_assert(sizeof(DebrisPartDesc) == 0x30, "DebrisPartDesc size");

typedef f32 (*ThingGetMassFn)(Thing *thing);
typedef void (*ThingImpactFn)(Thing *thing, CollisionInfo *info, s32 damageType, f32 force);

/* Push and damage the things around pos, falling off from the inner to the
 * outer radius; car hits may also stop one of its slave weapons. */
void Blast_Action(struct BlastInfo *blast, Vector4 *pos, Thing *source) {
    extern Vector4 g_zeroVector;
    struct GridResult {
        u32 data;
        u32 id;
    };
    struct GridResult results[256];
    CollisionInfo impact __attribute__((aligned(16)));
    DebrisParams params __attribute__((aligned(16)));
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    BlastDamage *damage = GETP(blast->damage);
    f32 t;
    u32 count;
    s32 damageType;
    u32 i;
    Thing *thing;
    BlastDamage *data;
    s8 skip;
    u32 mask;
    f32 outer;
    f32 range;
    f32 inner;
    f32 innerSq;
    f32 outerSq;
    f32 force;
    f32 dist;
    f32 scale;

    mask = 0;
    min = *pos;
    max = min;
    force = damage->force;
    damageType = damage->damageType;
    outer = GETP(blast->radius)[1];
    inner = GETP(blast->radius)[0];
    min.x -= outer;
    min.y -= outer;
    min.z -= outer;
    max.x += outer;
    max.y += outer;
    max.z += outer;
    innerSq = inner * inner;
    outerSq = outer * outer;
    if (damage->targets & 1) {
        mask |= 4;
    }
    if (damage->targets & 2) {
        mask |= 1;
    }
    if (damage->targets & 4) {
        mask |= 2;
    }
    count = Grid_GetObjects(&min, &max, mask, results, 0x100);
    range = outer - inner;
    for (i = 0; i < count; i++) {
        thing = (Thing *)PTR(results[i].id);
        skip = 0;
        if (source != NULL && source->player.h != 0 && thing->player.h == source->player.h) {
            if (blast->team == 0) {
                skip = 1;
            }
        } else if (blast->team == 2) {
            skip = 1;
        }
        if (skip) {
            continue;
        }
        if (thing->flags & THING_FLAG_RELEASED) {
            continue;
        }
        if ((thing->flags & THING_FLAG_AT_REST) && damageType == 0) {
            continue;
        }
        if (thing->flags & 0x01000002) {
            continue;
        }
        if (thing->collisionType == THING_COLLIDE_NONE) {
            continue;
        }
        if (thing->collisionType == THING_COLLIDE_RAY && !(damage->targets & 4)) {
            continue;
        }
        dist = PSVECSquareDistance(pos, &thing->pos);
        if (!(dist < outerSq)) {
            continue;
        }
        t = 1.0f;
        scale = t;
        dir.x = thing->pos.x - pos->x;
        dir.y = thing->pos.y - pos->y;
        dir.z = thing->pos.z - pos->z;
        if (damage->occluded && RayHitsTerrain(pos, &thing->pos, &t, NULL)) {
            continue;
        }
        if (dist > innerSq) {
            scale = (outer - Sqrt(dist)) / range;
        }
        data = GETP(blast->damage);
        if (data != NULL) {
            CarA *car = (CarA *)thing;

            switch (data->effect) {
            case 2:
                if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
                    Vector4 impulse __attribute__((aligned(16))) = {0.0f, -9.0f, 0.0f, 0.0f};
                    CarA_SlaveWeaponStop(thing, 0);
                    car->slaveWeaponRpm[0] = 9000;
                    CarA_StuntImpulse(thing, &g_zeroVector, &impulse);
                }
                break;
            case 3:
                if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
                    CarA_SlaveWeaponStop(thing, 1);
                    car->slaveWeaponRpm[1] = 5000;
                }
                break;
            case 4:
                if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
                    CarA_SlaveWeaponStop(thing, 2);
                    car->slaveWeaponRpm[2] = 0;
                }
                break;
            case 5:
                if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
                    /* repair */
                    thing->health = GETP(GETP(car->vehicleIface)->collider)->maxHealth;
                    car->partVisMask = 0;
                    MotionState_ReleaseSlavesByType(&car->motionState, 2);
                    CarA_SlaveWeaponStop(thing, 4);
                    car->slaveWeaponRpm[4] = 0;
                }
                break;
            case 6:
                if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
                    CarA_SlaveWeaponStop(thing, 3);
                    car->slaveWeaponRpm[3] = 0;
                }
                break;
            }
            if (damage->hitDebris != 0) {
                params.debris = damage->hitDebris;
                params.pos = thing->pos;
                params.vel = thing->vel;
                params.quat = *(Vector4 *)&thing->quat;
                params.owner = 0;
                params.unk4C = params.unk48 = 0.0f;
                params.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &params);
            }
        }
        force *= scale;
        if (THING_VFN(thing, THING_V_GET_MASS, ThingGetMassFn)(thing) > 0.01f) {
            force /= THING_VFN(thing, THING_V_GET_MASS, ThingGetMassFn)(thing);
        } else {
            force *= 2000.0f;
        }
        if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f) {
            dir.y = 1.0f;
        } else if (PSVECSquareMag(&dir)) {
            PSVECNormalize(&dir, &dir);
        }
        if (force > 600.0f) {
            force = 600.0f;
        }
        impact.normal = dir;
        impact.pos = *pos;
        if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
            /* sic: the first position is overwritten */
            impact.pos.x = pos->x - 4.0f * thing->axis[2].x;
            impact.pos.y = pos->y - 4.0f * thing->axis[2].y;
            impact.pos.z = pos->z - 4.0f * thing->axis[2].z;
            impact.pos.x = 2.0f * thing->axis[1].x + pos->x;
            impact.pos.y = 2.0f * thing->axis[1].y + pos->y;
            impact.pos.z = 2.0f * thing->axis[1].z + pos->z;
        }
        impact.thingA = HND(thing);
        impact.thingB = HND(source);
        impact.material = 0;
        force *= THING_VFN(thing, THING_V_GET_MASS, ThingGetMassFn)(thing);
        THING_VFN(thing, THING_V_DYNAMIC_COLLIDE_IMPACT, ThingImpactFn)(thing, &impact, damageType, force);
    }
}
s32 EventSeq_Init(struct EventSeq *seq, DebrisParams *data)
{
    Thing_Init(&seq->base, HND(data));
    seq->base.vel = data->vel;
    seq->base.quat = *(Quaternion *)&data->quat;
    seq->base.player.h = data->owner;
    seq->angVel = data->angVel;
    seq->speedScale = data->unk48;
    QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
    seq->base.pos = data->pos;
    seq->script.h = data->debris;
    seq->loopCount = 0;
    seq->wait = 0;
    seq->step = 0;
    seq->randMode = 0;
    seq->base.collisionType = THING_COLLIDE_NONE;
    seq->base.quat = *(Quaternion *)&data->quat;
    seq->base.flags |= 0x80202;
    return 1;
}

void EventSeq_Release(void *arg0)
{
    Thing_Release(arg0);
}

/* The spawn offset from the emit description (in the sequence's frame). */
void EventSeq_Advanced_GetOffset(struct EventSeqEmit *emit, void *arg1, Vector4 *out) {
    typedef struct {
        f32 base;
        f32 range;
        f32 scale;
        f32 speed;
        u8 axis;
        u8 damped;
        u8 spin;
    } EventSeqCircle;
    typedef struct {
        f32 base;
        f32 range;
        u8 pad8;
        u8 type;
        u8 padA[2];
        PTR32(void) data;
    } EventSeqAdvanced;
    f32 scale;

    switch (emit->offsetType) {
    case 1: {
        f32 *range = (f32 *)GETP(emit->offsetData);

        out->x = RandomAdd(range[0], range[1]);
        out->y = RandomAdd(range[2], range[3]);
        out->z = RandomAdd(range[4], range[5]);
        break;
    }
    case 2: {
        EventSeqAdvanced *adv = (EventSeqAdvanced *)GETP(emit->offsetData);

        scale = RandomAdd(adv->base, adv->range);
        switch (adv->type) {
        case 0: {
            Vector4 *dir = GETP(adv->data);

            RandomNumberGenerator_GetRandomVector(out);
            out->x *= scale * dir->x;
            out->y *= scale * dir->y;
            out->z *= scale * dir->z;
            break;
        }
        case 1: {
            Vector4 *dir = GETP(adv->data);
            Vector4 mul __attribute__((aligned(16))) = {1.0f, 1.0f, 1.0f, 0.0f};

            RandomNumberGenerator_GetRandomVector(out);
            switch (((EventSeqDir *)dir)->axis) {
            case 4:
                if (out->x < 0.0f) {
                    out->x *= -1.0f;
                }
                mul.x = dir->x;
                break;
            case 5:
                if (out->x > 0.0f) {
                    out->x *= -1.0f;
                }
                mul.x = dir->x;
                break;
            case 2:
                if (out->y < 0.0f) {
                    out->y *= -1.0f;
                }
                mul.y = dir->x;
                break;
            case 3:
                if (out->y > 0.0f) {
                    out->y *= -1.0f;
                }
                mul.y = dir->x;
                break;
            case 0:
                if (out->z < 0.0f) {
                    out->z *= -1.0f;
                }
                mul.z = dir->x;
                break;
            case 1:
                if (out->z > 0.0f) {
                    out->z *= -1.0f;
                }
                mul.z = dir->x;
                break;
            }
            out->x *= scale * mul.x;
            out->y *= scale * mul.y;
            out->z *= scale * mul.z;
            break;
        }
        case 2: {
            EventSeqCircle *circle = GETP(adv->data);
            f32 height = RandomAdd(circle->base, circle->range);
            f32 t = 1.0f;
            f32 angle;
            f32 s;
            f32 c;

            if (circle->range) {
                if (circle->damped && circle->scale) {
                    t = 1.0f - (height - circle->base) / (circle->range * circle->scale);
                    scale *= t;
                } else {
                    t = 1.0f - (height - circle->base) / circle->range;
                }
            }
            if (circle->spin) {
                if (circle->damped) {
                    angle = t * (360.0f * circle->speed * circle->scale);
                } else {
                    angle = t * (360.0f * circle->speed);
                }
            } else {
                angle = RandomAdd(0.0f, 360.0f);
            }
            SinCos(&s, &c, angle);
            switch (circle->axis) {
            case 0:
                out->x = height;
                out->y = scale * c;
                out->z = scale * s;
                break;
            case 1:
                out->x = scale * c;
                out->y = height;
                out->z = scale * s;
                break;
            case 2:
                out->x = scale * c;
                out->y = scale * s;
                out->z = height;
                break;
            }
            break;
        }
        }
        break;
    }
    default:
        out->w = out->x = out->y = out->z = 0.0f;
        break;
    }
}

void EventSeq_Advanced_GetInitData(struct EventSeqEmit *emit, void *arg1, Vector4 *vel, Vector4 *pos, Vector4 *quat, Vector4 *angVel) {
    EventSeq *seq = arg1;
    Matrix4 mtx1;
    Matrix4 res;
    Matrix4 rot;
    Matrix4 mtx3;
    Matrix4 mtx4;
    Vector4 tmp __attribute__((aligned(16)));
    Vector4 v __attribute__((aligned(16)));
    Vector4 a __attribute__((aligned(16)));
    Vector4 offset __attribute__((aligned(16)));
    Vector4 dir3 __attribute__((aligned(16)));

    EventSeq_Advanced_GetOffset(emit, THING_MTX(&seq->base), &offset);
    switch (emit->dirType) {
    case 0:
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(emit->speedBase, emit->speedRange);
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        if (quat != NULL) {
            *quat = *(Vector4 *)&seq->base.quat;
        }
        break;
    case 1: {
        f32 speed;
        f32 spin;

        speed = RandomAdd(emit->speedBase, emit->speedRange);
        spin = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        RandomNumberGenerator_GetRandomSubMatrix(&mtx1);
        v.x = mtx1.v[2].x * speed;
        v.y = mtx1.v[2].y * speed;
        v.z = mtx1.v[2].z * speed;
        a.x = mtx1.v[2].x * spin;
        a.y = mtx1.v[2].y * spin;
        a.z = mtx1.v[2].z * spin;
        offset.x += mtx1.v[2].x;
        offset.y += mtx1.v[2].y;
        offset.z += mtx1.v[2].z;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx1, quat);
        }
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        break;
    }
    case 2: {
        f32 *range = (f32 *)GETP(emit->dirData);
        f32 ry;
        f32 rx;
        f32 rz;

        ry = RandomDirVar(range[2], range[3]);
        rx = -RandomDirVar(range[0], range[1]);
        rz = RandomDirVar(range[4], range[5]);
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        SetOrientation(&rot, 0.01745329238474369f * ry, 0.01745329238474369f * rx, 0.01745329238474369f * rz);
        rot.v[3].x = rot.v[3].y = rot.v[3].z = 0.0f;
        MultiplyMatrix(&rot, THING_MTX(&seq->base), &res);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(emit->speedBase, emit->speedRange);
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        if (quat != NULL) {
            SubMatrixToQuaternion(&res, quat);
        }
        XformVecOrientation(&res, &v, vel);
        XformVecOrientation(&res, &a, angVel);
        break;
    }
    case 3: {
        f32 speed;
        f32 spin;

        mtx3 = *THING_MTX(&seq->base);
        speed = RandomAdd(emit->speedBase, emit->speedRange);
        spin = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        dir3 = offset;
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        if (!dir3.x && !dir3.y && !dir3.z) {
            dir3.x = 0.0f;
            dir3.y = 0.0f;
            dir3.z = 1.0f;
        } else if (PSVECSquareMag(&dir3)) {
            PSVECNormalize(&dir3, &dir3);
        }
        v.x = dir3.x * speed;
        v.y = dir3.y * speed;
        v.z = dir3.z * speed;
        a.x = dir3.x * spin;
        a.y = dir3.y * spin;
        a.z = dir3.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx3, quat);
        }
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        break;
    }
    case 4: {
        f32 speed;
        f32 spin;
        u8 *info;

        speed = RandomAdd(emit->speedBase, emit->speedRange);
        spin = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        info = (u8 *)GETP(emit->dirData);
        RandomNumberGenerator_GetRandomSubMatrix(&mtx4);
        switch (((EventSeqDir *)info)->axis) {
        case 4:
            if (mtx4.v[2].x < 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 5:
            if (mtx4.v[2].x > 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 2:
            if (mtx4.v[2].y < 0.0f) {
                mtx4.v[0].x *= -1.0f;
                mtx4.v[0].y *= -1.0f;
                mtx4.v[0].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 3:
            if (mtx4.v[2].y > 0.0f) {
                mtx4.v[0].x *= -1.0f;
                mtx4.v[0].y *= -1.0f;
                mtx4.v[0].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 0:
            if (mtx4.v[2].z < 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        case 1:
            if (mtx4.v[2].z > 0.0f) {
                mtx4.v[1].x *= -1.0f;
                mtx4.v[1].y *= -1.0f;
                mtx4.v[1].z *= -1.0f;
                mtx4.v[2].x *= -1.0f;
                mtx4.v[2].y *= -1.0f;
                mtx4.v[2].z *= -1.0f;
            }
            break;
        }
        v.x = mtx4.v[2].x * speed;
        v.y = mtx4.v[2].y * speed;
        v.z = mtx4.v[2].z * speed;
        a.x = mtx4.v[2].x * spin;
        a.y = mtx4.v[2].y * spin;
        a.z = mtx4.v[2].z * spin;
        offset.x += mtx4.v[2].x;
        offset.y += mtx4.v[2].y;
        offset.z += mtx4.v[2].z;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx4, quat);
        }
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        break;
    }
    }
    if (emit->inheritVel) {
        vel->x += seq->base.vel.x * emit->inheritScale;
        vel->y += seq->base.vel.y * emit->inheritScale;
        vel->z += seq->base.vel.z * emit->inheritScale;
    }
    pos->w = 1.0f;
}

void EventSeq_Advanced_GetInitData_NonDirectional(struct EventSeqEmit *emit, void *arg1, Vector4 *vel, Vector4 *pos, Vector4 *quat, Vector4 *angVel) {
    EventSeq *seq = arg1;
    Matrix4 mtx1;
    Matrix4 res;
    Matrix4 rot;
    Matrix4 mtx3;
    Matrix4 mtx4;
    Vector4 tmp __attribute__((aligned(16)));
    Vector4 v __attribute__((aligned(16)));
    Vector4 a __attribute__((aligned(16)));
    Vector4 offset __attribute__((aligned(16)));
    Vector4 dir1 __attribute__((aligned(16)));
    Vector4 dir3 __attribute__((aligned(16)));
    Vector4 dir4 __attribute__((aligned(16)));

    EventSeq_Advanced_GetOffset(emit, THING_MTX(&seq->base), &offset);
    switch (emit->dirType) {
    case 0:
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(emit->speedBase, emit->speedRange);
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        if (quat != NULL) {
            *quat = *(Vector4 *)&seq->base.quat;
        }
        break;
    case 1: {
        f32 speed;
        f32 spin;

        mtx1 = *THING_MTX(&seq->base);
        speed = RandomAdd(emit->speedBase, emit->speedRange);
        spin = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        RandomNumberGenerator_GetRandomVector(&dir1);
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        v.x = dir1.x * speed;
        v.y = dir1.y * speed;
        v.z = dir1.z * speed;
        a.x = dir1.x * spin;
        a.y = dir1.y * spin;
        a.z = dir1.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx1, quat);
        }
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        break;
    }
    case 2: {
        f32 *range = (f32 *)GETP(emit->dirData);
        f32 ry;
        f32 rx;
        f32 rz;

        ry = RandomDirVar(range[2], range[3]);
        rx = -RandomDirVar(range[0], range[1]);
        rz = RandomDirVar(range[4], range[5]);
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        SetOrientation(&rot, 0.01745329238474369f * ry, 0.01745329238474369f * rx, 0.01745329238474369f * rz);
        rot.v[3].x = rot.v[3].y = rot.v[3].z = 0.0f;
        MultiplyMatrix(&rot, THING_MTX(&seq->base), &res);
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = RandomAdd(emit->speedBase, emit->speedRange);
        a.x = 0.0f;
        a.y = 0.0f;
        a.z = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        if (quat != NULL) {
            SubMatrixToQuaternion(&res, quat);
        }
        XformVecOrientation(&res, &v, vel);
        XformVecOrientation(&res, &a, angVel);
        break;
    }
    case 3: {
        f32 speed;
        f32 spin;

        mtx3 = *THING_MTX(&seq->base);
        speed = RandomAdd(emit->speedBase, emit->speedRange);
        spin = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        dir3 = offset;
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        if (!dir3.x && !dir3.y && !dir3.z) {
            dir3.x = 0.0f;
            dir3.y = 0.0f;
            dir3.z = 1.0f;
        } else if (PSVECSquareMag(&dir3)) {
            PSVECNormalize(&dir3, &dir3);
        }
        v.x = dir3.x * speed;
        v.y = dir3.y * speed;
        v.z = dir3.z * speed;
        a.x = dir3.x * spin;
        a.y = dir3.y * spin;
        a.z = dir3.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx3, quat);
        }
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        break;
    }
    case 4: {
        f32 speed;
        f32 spin;

        mtx4 = *THING_MTX(&seq->base);
        speed = RandomAdd(emit->speedBase, emit->speedRange);
        spin = 0.001f * RandomAdd(emit->spinBase, emit->spinRange);
        RandomNumberGenerator_GetRandomVector(&dir4);
        switch (((EventSeqDir *)GETP(emit->dirData))->axis) {
        case 4:
            if (dir4.x < 0.0f) {
                dir4.x *= -1.0f;
            }
            break;
        case 5:
            if (dir4.x > 0.0f) {
                dir4.x *= -1.0f;
            }
            break;
        case 2:
            if (dir4.y < 0.0f) {
                dir4.y *= -1.0f;
            }
            break;
        case 3:
            if (dir4.y > 0.0f) {
                dir4.y *= -1.0f;
            }
            break;
        case 0:
            if (dir4.z < 0.0f) {
                dir4.z *= -1.0f;
            }
            break;
        case 1:
            if (dir4.z > 0.0f) {
                dir4.z *= -1.0f;
            }
            break;
        }
        XformVecOrientation(THING_MTX(&seq->base), &offset, &tmp);
        PSVECAdd(&seq->base.pos, &tmp, pos);
        v.x = dir4.x * speed;
        v.y = dir4.y * speed;
        v.z = dir4.z * speed;
        a.x = dir4.x * spin;
        a.y = dir4.y * spin;
        a.z = dir4.z * spin;
        if (quat != NULL) {
            SubMatrixToQuaternion(&mtx4, quat);
        }
        XformVecOrientation(THING_MTX(&seq->base), &v, vel);
        XformVecOrientation(THING_MTX(&seq->base), &a, angVel);
        break;
    }
    }
    if (emit->inheritVel) {
        vel->x += seq->base.vel.x * emit->inheritScale;
        vel->y += seq->base.vel.y * emit->inheritScale;
        vel->z += seq->base.vel.z * emit->inheritScale;
    }
    pos->w = 1.0f;
}
/* Run the script for this frame: entries until a wait or the end. */
s32 EventSeq_Move(struct EventSeq *seq) {
    extern f32 g_fDeltaTimeMS;
    extern Vector4 g_zeroVector;
    extern Matrix4 g_identity;
    f32 scale;
    u32 dt;
    u32 sample;  /* sound effect id (pack sound relocation); SoundSource_Set copies 4 bytes */
    f32 pitch;
    EventSeqEntry *entry;
    EventSeqScript *script;

    scale = 1.0f;
    if (seq->speedScale != 0.0f) {
        scale = seq->speedScale * MagnitudeVector(&seq->base.vel) + 1.0f;
        dt = g_fDeltaTimeMS * scale;
    } else {
        dt = g_fDeltaTimeMS;
    }
    if (GETP(seq->script) == NULL) {
        return 0;
    }
    seq->camDirty = 1;
    while (1) {
        if (seq->wait != 0) {
            if (dt > seq->wait) {
                dt -= seq->wait;
                seq->wait = 0;
            } else {
                seq->wait -= dt;
                return 1;
            }
        }
        script = GETP(seq->script);
        if (seq->step >= script->count) {
            Thing_Release(seq);
            break;
        }
        entry = &GETP(script->entries)[seq->step];
        switch (entry->type) {
        case 0:
            break;
        case 1: {
            ParticleInfo *p = GETP(entry->a);

            if (p == NULL) break;
            if (!ParticleManager_CanCreatureParticle(p)) break;
            {
                Vector4 ang;
                Vector4 vel = {0.0f, 0.0f, 0.0f, 0.0f};
                Vector4 pos;
                Vector4 quat = {0.0f, 0.0f, 0.0f, 1.0f};
                EventSeqEmit *emit = GETP(GETP(GETP(seq->script)->entries)[seq->step].b);
                u32 n = RandomAdd_s32(emit->countBase, emit->countRange);
                u32 i;

                if (!EventSeq_InCameraRange(seq, p->range) && GameSettings_GetNumOfPlayers() != 1) break;
                QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
                if (n == 0) n = 1;
                for (i = 0; i < n; i++) {
                    if (p->type == PARTICLE_GX) {
                        EventSeq_Advanced_GetInitData(emit, seq, &vel, &pos, &quat, &ang);
                    } else {
                        EventSeq_Advanced_GetInitData_NonDirectional(emit, seq, &vel, &pos, &quat, &ang);
                    }
                    vel.x *= 0.001f;
                    vel.y *= 0.001f;
                    vel.z *= 0.001f;
                    ang.x *= 0.001f;
                    ang.y *= 0.001f;
                    ang.z *= 0.001f;
                    ang.y += 9.6667e-7f * RandomAdd(emit->extraBase, emit->extraRange);
                    CreateParticle(&pos, &vel, &ang, &quat, p, scale, 0.0f);
                }
            }
            break;
        }
        case 2: {
            u8 *p = GETP(entry->a);
            EventSeqEmit *emit;
            u32 n, i;
            ProjectileParams prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(emit->countBase, emit->countRange);
            SETP(prm.info, p);
            prm.player.h = seq->base.player.h;
            QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, (Vector4 *)&prm.quat, &ang);
                prm.gravity = RandomAdd(emit->extraBase, emit->extraRange);
                ThingManager_DARYL_CreateThing(THING_TYPE_PROJECTILE, &prm);
            }
            break;
        }
        case 3: {
            u8 *p = GETP(entry->a);
            EventSeqEmit *emit;
            u32 n, i;
            DebrisParams prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(emit->countBase, emit->countRange);
            QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
            prm.owner = seq->base.player.h;
            prm.unk48 = 0.0f;
            prm.unk4C = 0.0f;
            prm.debris = HND(p);
            prm.angVel = g_zeroVector;
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, &prm.quat, &ang);
                ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &prm);
            }
            break;
        }
        case 4: {
            u8 *p = GETP(entry->a);
            EventSeqEmit *emit;
            u32 n, i;
            EmitterParams prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(emit->countBase, emit->countRange);
            QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                SETP(prm.emitter, p);
                prm.drawDist = 0.0f;
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, (Vector4 *)&prm.quat, &ang);
                prm.owner = seq->base.player.h;
                ThingManager_DARYL_CreateThing(THING_TYPE_EMITTER, &prm);
            }
            break;
        }
        case 6: {
            ThingRecord *p = GETP(entry->a);

            if (p == NULL) break;
            {
                Vector4 pos;
                Vector4 tmp;
                Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                EventSeqEmit *emit = GETP(GETP(GETP(seq->script)->entries)[seq->step].b);
                Matrix4 m;
                DestructibleParams prm __attribute__((aligned(16)));

                if (emit->worldSpace == 1) {
                    m = g_identity;
                    seq->base.quat.x = 0.0f;
                    seq->base.quat.y = 0.0f;
                    seq->base.quat.z = 0.0f;
                    seq->base.quat.w = 1.0f;
                } else {
                    QuaternionToSubMatrix(&seq->base.quat, &m);
                }
                pos = seq->base.pos;
                if (emit->randomOffset) {
                    f32 *r = GETP(emit->offsetRange);
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                SETP(prm.record, p);
                prm.unk4C = 0;
                prm.moveType = 0;
                prm.pos = pos;
                prm.quat = seq->base.quat;
                prm.vel = prm.angVel = g_zeroVector;
                prm.unk58 = 0;
                prm.anim.h = 0;
                if (p->hasDrawDist) {
                    prm.drawDist = p->drawDist;
                } else {
                    prm.drawDist = 0.0f;
                }
                ThingManager_DARYL_CreateThing(THING_TYPE_DESTRUCTIBLE, &prm);
            }
            break;
        }
        case 7: {
            u8 *p = GETP(entry->a);
            EventSeqEmit *emit;
            u32 n, i;
            DestructibleParams prm __attribute__((aligned(16)));
            Vector4 ang;

            if (p == NULL) break;
            emit = GETP(entry->b);
            n = RandomAdd_s32(emit->countBase, emit->countRange);
            QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
            prm.unk4C = 0;
            prm.moveType = 2;
            prm.angVel = seq->angVel;
            prm.anim.h = 0;
            if (n == 0) n = 1;
            for (i = 0; i < n; i++) {
                SETP(prm.record, p);
                prm.drawDist = 0.0f;
                EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &prm.pos, (Vector4 *)&prm.quat, &ang);
                ThingManager_DARYL_CreateThing(THING_TYPE_PHYSICAL_OBJECT, &prm);
            }
            break;
        }
        case 29: {
            DebrisPartList *p = GETP(entry->a);

            if (p == NULL) break;
            {
                EventSeqEmit *emit = GETP(entry->b);
                u32 n = RandomAdd_s32(emit->countBase, emit->countRange);
                Vector4 base = {0.0f, 0.0f, 0.0f, 0.0f};
                Vector4 ang;
                Vector4 rnd;
                DebrisPartParams prm __attribute__((aligned(16)));
                EmitterParams sp __attribute__((aligned(16)));
                u32 i, j;

                QuaternionToSubMatrix(&seq->base.quat, THING_MTX(&seq->base));
                if (n == 0) n = 1;
                for (i = 0; i < n; i++) {
                    EventSeq_Advanced_GetInitData(emit, seq, &prm.vel, &base, (Vector4 *)&prm.quat, &ang);
                    for (j = 0; j < p->count; j++) {
                        f32 mag = PSVECSquareMag(&GETP(p->parts)[j].dir);
                        f32 s;
                        Thing *thing;

                        if (mag <= 0.0001f) {
                            prm.vel.x = 0.0f;
                            prm.vel.y = mag = 1.0f;
                            prm.vel.z = 0.0f;
                        } else {
                            XformVecOrientation(THING_MTX(&seq->base), &GETP(p->parts)[j].dir, &prm.vel);
                        }
                        s = RandomAdd(emit->speedBase, emit->speedRange) / mag;
                        prm.vel.x *= s;
                        prm.vel.y *= s;
                        prm.vel.z *= s;
                        RandomNumberGenerator_GetRandomVector(&rnd);
                        if ((prm.geometry.h = GETP(p->parts)[j].geometry) == 0) continue;
                        prm.gravity = RandomAdd(emit->extraBase, emit->extraRange);
                        XformVecOrientation(THING_MTX(&seq->base), &GETP(p->parts)[j].dir, &prm.pos);
                        prm.quat = seq->base.quat;
                        prm.pos.x += base.x;
                        prm.pos.y += base.y;
                        prm.pos.z += base.z;
                        prm.vel.x += rnd.x * emit->speedBase;
                        prm.vel.y += rnd.y * emit->speedBase;
                        prm.vel.z += rnd.z * emit->speedBase;
                        if (emit->inheritVel) {
                            prm.vel.x += seq->base.vel.x * emit->inheritScale;
                            prm.vel.y += seq->base.vel.y * emit->inheritScale;
                            prm.vel.z += seq->base.vel.z * emit->inheritScale;
                        }
                        prm.life = RandomAdd_s32(p->lifeBase, p->lifeRange);
                        prm.mass = GETP(p->parts)[j].mass;
                        /* short-lived ones stay put */
                        if (prm.life < (s32)(0.5f * p->lifeBase)) {
                            prm.moves = 0;
                        } else {
                            prm.moves = 1;
                        }
                        RandomNumberGenerator_GetRandomVector(&prm.spinAxis);
                        prm.spinAxis.w = 0.017453292f * RandomAdd(GETP(p->parts)[j].spinMin, GETP(p->parts)[j].spinMax);
                        thing = ThingManager_DARYL_CreateThing(THING_TYPE_DEBRIS, &prm);
                        if (thing != NULL && GETP(GETP(p->parts)[j].slave) != NULL) {
                            Thing *slave;

                            sp.drawDist = 0.0f;
                            sp.quat.w = 1.0f;
                            sp.quat.x = 0.0f;
                            sp.quat.y = 0.0f;
                            sp.quat.z = 0.0f;
                            sp.pos = g_zeroVector;
                            SETP(sp.emitter, GETP(GETP(p->parts)[j].slave));
                            sp.owner = seq->base.player.h;
                            sp.vel = g_zeroVector;
                            slave = ThingManager_DARYL_CreateSlave(THING_TYPE_EMITTER, &sp, 0xFF);
                            if (slave != NULL) {
                                slave->slaveTag = 0xFF;
                                ThingManagerList_Append(&thing->slaves, slave);
                            }
                        }
                    }
                }
            }
            break;
        }
        case 25: {
            ItemRecord *p = GETP(entry->a);

            if (p == NULL) break;
            {
                EventSeqEmit *emit = GETP(entry->b);
                Vector4 pos = seq->base.pos;
                ItemParams prm __attribute__((aligned(16)));

                if (emit->randomOffset) {
                    f32 *r = GETP(emit->offsetRange);
                    pos.x += RandomAdd(r[0], r[1]);
                    pos.y += RandomAdd(r[2], r[3]);
                    pos.z += RandomAdd(r[4], r[5]);
                }
                SETP(prm.record, p);
                prm.pos = pos;
                prm.drawDist = 0.0f;
                prm.quat.w = 1.0f;
                prm.quat.x = 0.0f;
                prm.quat.y = 0.0f;
                prm.quat.z = 0.0f;
                prm.vel.x = 0.0f;
                prm.vel.y = 0.0f;
                prm.vel.z = 0.0f;
                ThingManager_DARYL_CreateThing(THING_TYPE_ITEM, &prm);
            }
            break;
        }
        case 8: {
            u8 *p = GETP(entry->a);

            if (p == NULL) break;
            {
                Vector4 pos;
                Vector4 tmp;
                Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                EventSeqEmit *emit = GETP(GETP(GETP(seq->script)->entries)[seq->step].b);
                Matrix4 m;

                if (emit->worldSpace == 1) {
                    m = g_identity;
                }
                QuaternionToSubMatrix(&seq->base.quat, &m);
                pos = seq->base.pos;
                if (emit->randomOffset) {
                    f32 *r = GETP(emit->offsetRange);
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                LightHolder_Add(p, &pos, NULL);
            }
            break;
        }
        case 10: {
            EventSeqSound *p = GETP(entry->a);

            if (p == NULL) break;
            if (p->type == 1) {
                f32 pitch = RandomAdd(p->pitchMin, p->pitchMax);
                void *voice = SoundAudioSample_Play(p->sample);

                SoundAudioSample_Set(voice, 0, 32767.0f * p->volume);
                SoundAudioSample_Set(voice, 1, 65536.0f * pitch);
            } else {
                EventSeqEmit *emit = GETP(entry->b);
                s32 src = SoundManager_CreateSource(0);

                sample = p->sample;
                pitch = RandomAdd(p->pitchMin, p->pitchMax);
                {
                    Vector4 tmp;
                    Vector4 pos;
                    Vector4 vel;
                    Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                    Matrix4 m;

                    vel = seq->base.vel;
                    if (emit->worldSpace == 1) {
                        m = g_identity;
                        vel.x = vel.y = vel.z = 0.0f;
                    }
                    QuaternionToSubMatrix(&seq->base.quat, &m);
                    pos = seq->base.pos;
                    if (emit->randomOffset) {
                        f32 *r = GETP(emit->offsetRange);
                        off.x = RandomAdd(r[0], r[1]);
                        off.y = RandomAdd(r[2], r[3]);
                        off.z = RandomAdd(r[4], r[5]);
                    }
                    XformVecOrientation(&m, &off, &tmp);
                    PSVECAdd(&pos, &tmp, &pos);
                    SoundSource_Set(src, 8, &sample);
                    SoundSource_Set(src, 5, &pos);
                    SoundSource_Set(src, 6, &vel);
                    SoundSource_Set(src, 0, &p->volume);
                    SoundSource_Set(src, 1, &pitch);
                    if (p->type == 0) {
                        SoundSource_Set(src, 2, &GETP(p->range)[0]);
                        SoundSource_Set(src, 3, &GETP(p->range)[1]);
                    }
                    SoundSource_Play(src);
                }
            }
            break;
        }
        case 5: {
            BlastInfo *p = GETP(entry->a);

            if (p == NULL) break;
            {
                EventSeqEmit *emit = GETP(entry->b);
                Vector4 tmp;
                Vector4 pos;
                Vector4 off = {0.0f, 0.0f, 0.0f, 0.0f};
                Matrix4 m;

                if (emit->worldSpace == 1) {
                    m = g_identity;
                }
                QuaternionToSubMatrix(&seq->base.quat, &m);
                pos = seq->base.pos;
                if (emit->randomOffset) {
                    f32 *r = GETP(emit->offsetRange);
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                if (p->blast) {
                    Blast_Action(p, &pos, &seq->base);
                }
                if (!GameSettings_GetCodeVariable(0x198, 0) && (p->rumble[0] || p->rumble[1] || p->rumble[2])) {
                    ForceFeedback_Play(p, &pos);
                }
                if (p->flash) {
                    f32 *range = GETP(p->radius);
                    f32 lo = range[0];
                    f32 hi = range[1];
                    f32 loSq = lo * lo;
                    f32 hiSq = hi * hi;
                    f32 span = hi - lo;
                    s8 skip = 0;   /* sic: never reset, once set every later thing is skipped */
                    Thing *t;

                    for (t = GETP(g_thingManager_DARYL.activeList); t != NULL; t = GETP(t->next)) {
                        if (t->player.h == seq->base.player.h) {
                            if (p->team == 0) skip = 1;
                        } else {
                            if (p->team == 2) skip = 1;
                        }
                        if (!skip && !(t->flags & THING_FLAG_RELEASED) && t->collisionType != THING_COLLIDE_NONE) {
                            f32 d = PSVECSquareDistance(&pos, &t->pos);

                            if (d < hiSq) {
                                f32 f = 1.0f;

                                if (d > loSq) {
                                    f = (hi - Sqrt(d)) / span;
                                }
                                FlashHolder_Add(p, &pos, HND(t), f);
                            }
                        }
                    }
                }
            }
            break;
        }
        case 26: {
            SoundSeqInfo *p = GETP(entry->a);   /* a sound sequence */
            Player *player;

            if (p == NULL) break;
            player = GETP(seq->base.player);
            if (player != NULL && !(player->flags & 0x40) && p->allPlayers == 0) break;
            {
                EventSeqEmit *emit = GETP(entry->b);
                Vector4 tmp;
                Vector4 pos;
                Vector4 off __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
                Matrix4 m;

                if (emit->worldSpace == 1) {
                    m = g_identity;
                }
                QuaternionToSubMatrix(&seq->base.quat, &m);
                pos = seq->base.pos;
                if (emit->randomOffset) {
                    f32 *r = GETP(emit->offsetRange);
                    off.x = RandomAdd(r[0], r[1]);
                    off.y = RandomAdd(r[2], r[3]);
                    off.z = RandomAdd(r[4], r[5]);
                }
                XformVecOrientation(&m, &off, &tmp);
                PSVECAdd(&pos, &tmp, &pos);
                SoundSequence_Play(p, &pos);
            }
            break;
        }
        case 11:
            seq->loopStart = seq->step;
            seq->loopCount = GETP(GETP(seq->script)->entries)[seq->step].a.h  /* port: a value (repeat count), not a pointer */;
            break;
        case 12:
            if (seq->loopCount != 0) {
                seq->loopCount -= 1;
                seq->step = seq->loopStart;
            }
            break;
        case 15:
            seq->wait = entry->a.h;  /* port: a value, not a pointer */
            break;
        case 13: {
            u32 i;

            seq->randStart = i = seq->step + 1;
            seq->randCount = GETP(GETP(seq->script)->entries)[seq->step].a.h  /* port: a value (repeat count), not a pointer */;
            seq->randLength = 0;
            for (; i < GETP(seq->script)->count; i++) {
                if (GETP(GETP(seq->script)->entries)[i].type == 14) break;
                seq->randLength += 1;
            }
            seq->randMode = 1;
            break;
        }
        case 14:
            if (seq->randCount > 1) {
                seq->randCount -= 1;
                seq->randMode = 1;
            }
            break;
        case 30:
            break;
        default:
            return 0;
        }
        switch (seq->randMode) {
        case 0:
            seq->step += 1;
            break;
        case 1:
            seq->step = RandomAdd_s32(seq->randStart, seq->randLength);
            seq->randMode = 2;
            break;
        case 2:
            seq->step = seq->randStart + seq->randLength;
            seq->randMode = 0;
            break;
        }
    }
    return 1;
}

u32 EventSeq_InCameraRange(struct EventSeq *seq, f32 range)
{
    Vector4 positions[5] __attribute__((aligned(16)));
    Vector4 *pos;
    f32 dist;
    u32 count;
    u32 i;

    if (range == 0.0f)
    {
        return 1;
    }
    if (seq->camDirty)
    {
        count = Gfx_GetCameraPositions(positions);
        seq->camDistSq = PSVECSquareDistance(&seq->base.pos, (f32 *)&positions[0]);
        if (count > 1)
        {
            for (i = 0, pos = positions; i < 5; i++, pos++)
            {
                if (g_CurrentViews & (1 << i))
                {
                    dist = PSVECSquareDistance(&seq->base.pos, (f32 *)pos);
                    if (dist < seq->camDistSq)
                    {
                        seq->camDistSq = dist;
                    }
                }
            }
        }
        seq->camDirty = 0;
    }
    return range * range > seq->camDistSq;
}

/* Whether the script lies in [start, end) (a data pack being unloaded). */
s32 EventSeq_Invalidate(struct EventSeq *seq, u32 start, u32 end)
{
    u32 script = seq->script.h;

    if (script >= start && script < end)
    {
        return 1;
    }
    return 0;
}
