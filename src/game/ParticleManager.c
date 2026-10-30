#include "common.h"


extern u8 g_particleManager[];  /* typed per function below */
extern u8 g_numParticlesCreatedThisFrame[];  /* typed per function below */

void ParticleManager_Init(void) {
    typedef struct {
        u8 unk0[0x94];
        u8 flags;
        u8 unk95[0xB0 - 0x95];
    } ParticleSprite;
    typedef struct {
        u8 unk0[0xB4];
        u8 flags;
        u8 unkB5[0x100 - 0xB5];
    } ParticleGX;
    typedef struct {
        struct {
            ParticleGX particles[0x280];
            s32 unk28000;
            s32 unk28004;
            u8 unk28008[0x8];
        } groups[3];
        ParticleSprite sprites[0x200];
        s32 unk8E030;
        u8 unk8E034[0xC];
    } ParticleManager;
    #define g_particleManager (*(ParticleManager *)(void *)&g_particleManager)
    #define g_numParticlesCreatedThisFrame (*(s32 *)(void *)&g_numParticlesCreatedThisFrame)
    ParticleManager *pm = &g_particleManager;
    u32 j;
    u32 i;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 0x280; j++) {
            pm->groups[i].particles[j].flags |= 0xC;
        }
        pm->groups[i].unk28004 = 0;
        pm->groups[i].unk28000 = 0;
    }
    for (j = 0; j < 0x200; j++) {
        pm->sprites[j].flags |= 0xC;
    }
    pm->unk8E030 = 0;
    g_numParticlesCreatedThisFrame = 0;
#undef g_numParticlesCreatedThisFrame
#undef g_particleManager
}

s8 ParticleManager_CanCreatureParticle(void *creature) {
    #define g_numParticlesCreatedThisFrame (*(u32 *)(void *)&g_numParticlesCreatedThisFrame)
    u32 count;

    count = ++g_numParticlesCreatedThisFrame;
    if (count <= 11) {
        return 1;
    }
    if (count <= 21) {
        return ((u8 *)creature)[3] <= 8;
    }
    if (count <= 31) {
        return ((u8 *)creature)[3] <= 6;
    }
    if (count <= 41) {
        return ((u8 *)creature)[3] <= 4;
    }
    if (count <= 51) {
        return ((u8 *)creature)[3] <= 2;
    }
    if (count <= 101) {
        return ((u8 *)creature)[3] <= 1;
    }
    return 0;
#undef g_numParticlesCreatedThisFrame
}

void CreateParticle(Vector4 *pos, Vector4 *vel, Vector4 *accel, Vector4 *quat, u8 *info, f32 speed, f32 age) {
    switch (*info) {
    case 3:
        break;
    case 0:
        CreateParticleGX(pos, vel, accel, quat, info, speed, age);
        break;
    case 1:
        CreateParticleSprite(pos, vel, accel, quat, info, speed, age);
        break;
    case 2:
        LineManager_Add(info, pos, vel, accel);
        break;
    }
}

void CreateParticleGX(Vector4 *pos, Vector4 *vel, Vector4 *accel, Vector4 *quat, u8 *info, f32 speed, f32 age) {
    extern Vector4 g_zeroVector;
    extern s32 g_iDeltaTimeMS;
    typedef struct {
        u8 r;
        u8 g;
        u8 b;
        u8 a;
    } Colour;
    typedef struct {
        Vector4 pos;
        Vector4 vel;
        Vector4 accel;
        Vector4 quat;
        Vector4 axis;
        u8 colour[0x2C];
        u8 colourActive;
        u8 unk7D[0x3];
        u8 scale[0x24];
        u8 scaleActive;
        u8 unkA5[0xB];
        f32 unkB0;
        u8 flags;
        u8 unkB5;
        u8 visible;
        u8 frame;
        f32 speed;
        PTR32(u8) info;
        PTR32(void) texture;
        f32 unkC4;
        f32 spin;
        s32 life;
        f32 rate;
        s32 frameTimer;
        f32 unkD8;
        s32 timer;
        Vector4 targetPos;
        Vector4 targetVel;
    } ParticleGX;
    typedef struct {
        ParticleGX particles[0x280];
        u32 next;
        u8 unk28004[0xC];
    } ParticleGroup;
    typedef struct {
        ParticleGroup groups[3];
        u8 unk78030[0x16010];
    } ParticleManager;
    #define g_particleManager (*(ParticleManager *)(void *)&g_particleManager)
    static s8 which;
    ParticleGroup *group;
    ParticleGX *particle;
    u8 *anim;
    s32 life;
    f32 lifeF;
    u32 time;
    u32 idx;
    Colour c;
    f32 rate;

    life = RandomAdd_s32(*(s32 *)(info + 0x8), *(s32 *)(info + 0xC));
    lifeF = life;
    anim = SLOT_LOAD(u8 *, (info + 0x4));
    time = age * lifeF;
    if (anim != NULL) {
        if (anim[1] & 3) {
            group = &g_particleManager.groups[0];
        } else if (which++ & 1) {
            group = &g_particleManager.groups[1];
        } else {
            group = &g_particleManager.groups[2];
        }
        idx = group->next++;
        if (group->next >= 0x280) {
            group->next = 0;
        }
        particle = &group->particles[idx];
        if (anim[3] != 0) {
            particle->frame = 0;
            particle->frameTimer = *(s32 *)(anim + 0x14);
        } else {
            particle->frame = RandomAdd_s32(0, *(s32 *)(anim + 0x20));
        }
        SETP(particle->texture, SLOT_LOAD(void *, (u32 *)SLOT_LOAD(void *, (anim + 0x24)) + (particle->frame)));
        if (GETP(particle->texture) != NULL) {
            particle->speed = speed;
            particle->pos = *pos;
            particle->vel = *vel;
            particle->accel = *accel;
            SETP(particle->info, info);
            rate = 1.0f;
            particle->unkC4 = rate;
            particle->life = life - time;
            particle->timer = g_iDeltaTimeMS;
            particle->targetVel = g_zeroVector;
            particle->targetPos = *pos;
            particle->rate = rate;
            if (*(s32 *)(info + 0xC) != 0 && *(s32 *)(info + 0x8) != 0) {
                particle->rate = rate / (lifeF / *(s32 *)(info + 0x8));
            }
            particle->scaleActive = 1;
            particle->colourActive = 1;
            ExtractColorKey(GETP(particle->info) + 0x14, particle->colour, time);
            particle->unkD8 = RandomAdd(*(f32 *)(anim + 0xC), *(f32 *)(anim + 0x10));
            ExtractScaleKey(anim + 0x28, particle->scale, time);
            particle->visible = 1;
            if (*(u32 *)(GETP(particle->info) + 0x14) == 0) {
                particle->visible = 0;
            } else if (*(u32 *)(GETP(particle->info) + 0x14) == 1) {
                c = *SLOT_LOAD(Colour *, (GETP(particle->info) + 0x18));
                if (c.r == 0xFF && c.b == 0xFF && c.g == 0xFF) {
                    particle->visible = 0;
                }
            }
            particle->flags = anim[1] != 0;
            if (anim[1] > 1) {
                particle->flags |= 2;
            }
            particle->flags &= ~0xC;
            if (anim[1] == 3) {
                particle->unkB0 = 0.0f;
            } else {
                particle->unkB0 = 0.75f;
            }
            if (anim[0] == 0) {
                particle->quat = *quat;
            } else if (anim[0] == 1) {
                RandomNumberGenerator_GetRandomQuaternion(&particle->quat);
            } else {
                particle->quat.x = quat->x = 0.0f;
                particle->quat.y = quat->y = 0.0f;
                particle->quat.z = quat->z = 0.0f;
                particle->quat.w = quat->w = 1.0f;
            }
            if (*(f32 *)(anim + 0x4) || *(f32 *)(anim + 0x8)) {
                switch (anim[2]) {
                case 0:
                    RandomNumberGenerator_GetRandomVector(&particle->axis);
                    break;
                case 1:
                    particle->axis.x = 1.0f;
                    particle->axis.y = 0.0f;
                    particle->axis.z = 0.0f;
                    break;
                case 2:
                    particle->axis.x = 0.0f;
                    particle->axis.y = 1.0f;
                    particle->axis.z = 0.0f;
                    break;
                case 3:
                    particle->axis.x = 0.0f;
                    particle->axis.y = 0.0f;
                    particle->axis.z = 1.0f;
                    break;
                }
                particle->spin = 0.1f * (0.017453292f * RandomAdd(*(f32 *)(anim + 0x4), *(f32 *)(anim + 0x8)));
            } else {
                particle->spin = 0.0f;
                particle->axis.x = 1.0f;
                particle->axis.y = 0.0f;
                particle->axis.z = 0.0f;
            }
        }
    }
#undef g_particleManager
}

void CreateParticleSprite(Vector4 *pos, Vector4 *vel, Vector4 *accel, void *owner, u8 *info, f32 speed, f32 age) {
    typedef struct {
        u8 r;
        u8 g;
        u8 b;
        u8 a;
    } Colour;
    typedef struct {
        Vector4 pos;
        Vector4 vel;
        Vector4 accel;
        u8 colour[0x2C];
        u8 colourActive;
        u8 unk5D[0x3];
        u8 size[0x10];
        u8 sizeActive;
        u8 unk71[0xF];
        f32 rot;
        f32 rotSpeed;
        s32 life;
        f32 unk8C;
        PTR32(u8) info;
        u8 flags;
        u8 type;
        u8 visible;
        u8 frame;
        f32 speed;
        f32 rate;
        s32 frameTimer;
        u8 unkA4[0xC];
    } ParticleSprite;
    typedef struct {
        ParticleSprite sprites[0x200];
        u32 next;
    } SpriteList;
    typedef struct {
        u8 unk0[0x78030];
        SpriteList spriteList;
        u8 unk8E034[0xC];
    } ParticleManager;
    #define g_particleManager (*(ParticleManager *)(void *)&g_particleManager)
    SpriteList *list;
    ParticleSprite *sprite;
    u8 *anim;
    s32 life;
    f32 lifeF;
    u32 time;
    u32 idx;
    Colour c;
    f32 rate;

    life = RandomAdd_s32(*(s32 *)(info + 0x8), *(s32 *)(info + 0xC));
    lifeF = life;
    time = age * lifeF;
    anim = SLOT_LOAD(u8 *, (info + 0x4));
    if (anim != NULL) {
        list = &g_particleManager.spriteList;
        idx = list->next++;
        if (idx + 1 >= 0x200) {
            list->next = 0;
        }
        sprite = &list->sprites[idx];
        sprite->pos = *pos;
        sprite->vel = *vel;
        sprite->accel = *accel;
        SETP(sprite->info, info);
        sprite->flags &= ~0xC;
        if (anim[1] != 0) {
            sprite->frame = 0;
            sprite->frameTimer = *(s32 *)(anim + 0x1C);
        } else {
            sprite->frame = RandomAdd_s32(0, *(s32 *)(anim + 0x20));
        }
        sprite->life = life;
        sprite->speed = speed;
        rate = 1.0f;
        sprite->rate = rate;
        if (*(s32 *)(info + 0xC) != 0 && *(s32 *)(info + 0x8) != 0) {
            sprite->rate = rate / (lifeF / *(s32 *)(info + 0x8));
        }
        sprite->type = anim[0];
        sprite->sizeActive = 1;
        sprite->colourActive = 1;
        ExtractColorKey(GETP(sprite->info) + 0x14, sprite->colour, time);
        sprite->unk8C = RandomAdd(*(f32 *)(anim + 0x14), *(f32 *)(anim + 0x18));
        ExtractF32Key(anim + 0x28, sprite->size, time);
        sprite->rot = RandomDirVar(*(f32 *)(anim + 0x4), *(f32 *)(anim + 0x8));
        sprite->rotSpeed = RandomAdd(*(f32 *)(anim + 0xC), *(f32 *)(anim + 0x10));
        sprite->visible = 1;
        if (*(u32 *)(GETP(sprite->info) + 0x14) == 0) {
            sprite->visible = 0;
        } else if (*(u32 *)(GETP(sprite->info) + 0x14) == 1) {
            c = *SLOT_LOAD(Colour *, (GETP(sprite->info) + 0x18));
            if (c.r == 0xFF && c.b == 0xFF && c.g == 0xFF) {
                sprite->visible = 0;
            }
        }
    }
#undef g_particleManager
}

void ParticleGX_Move();
void ParticleManager_Heartbeat(void) {
    extern s8 g_bFreezeWorld;
    extern f32 g_fDeltaTimeMS;
    typedef struct {
        Vector4 pos;
        Vector4 vel;
        Vector4 accel;
        u8 unk30[0x94 - 0x30];
        u8 flags;
        u8 unk95[0xB0 - 0x95];
    } ParticleSprite;
    typedef struct {
        u8 unk0[0xB4];
        u8 flags;
        u8 unkB5[0x100 - 0xB5];
    } ParticleGX;
    typedef struct {
        struct {
            ParticleGX particles[0x280];
            u8 unk28000[0x10];
        } groups[3];
        ParticleSprite sprites[0x200];
        u8 unk8E030[0x10];
    } ParticleManager;
    #define g_particleManager (*(ParticleManager *)(void *)&g_particleManager)
    ParticleManager *pm;
    u32 j;
    u32 i;
    ParticleSprite *sprite;
    ParticleSprite *sprites;
    f32 dt;

    if (!g_bFreezeWorld) {
        pm = &g_particleManager;
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 0x280; j++) {
                if (!(pm->groups[i].particles[j].flags & 8)) {
                    ParticleGX_Move(&pm->groups[i].particles[j]);
                }
            }
        }
        dt = g_fDeltaTimeMS;
        sprites = pm->sprites;
        sprite = pm->sprites;
        for (j = 0; j < 0x200; j++) {
            if (!(pm->sprites[j].flags & 8)) {
                ParticleSprite_Move(&sprite[j], sprites);
            }
        }
        sprite = pm->sprites;
        for (j = 0; j < 0x200; j++) {
            if (!(pm->sprites[j].flags & 8)) {
                AccumulateVector(&sprite[j].accel, &sprite[j].vel, &sprite[j].vel, dt);
                AccumulateVector(&sprite[j].vel, &sprite[j].pos, &sprite[j].pos, dt);
            }
        }
    }
#undef g_particleManager
}

void ParticleGX_Move(struct ParticleGX_Move_t {
        Vector4 pos;
        u8 unk10[0xDC - 0x10];
        s32 timer;
        Vector4 targetPos;
    } *particle) {
    extern s32 g_iDeltaTimeMS;
    void ParticleGX_MacroStep(void *particle);
    f32 distSq;

    if (particle->timer <= 0) {
        particle->pos = particle->targetPos;
        distSq = Gfx_GetMinCameraDistSquared(particle);
        if ((u32)ViewManager_GetViewFormat() > 1) {
            distSq *= 4.0f;
        }
        if (distSq > 1000000.0f) {
            particle->timer = 700;
        } else {
            distSq = Sqrt(distSq) - 1.0f;
            distSq /= 999.0f;
            distSq *= 700.0f;
            particle->timer = (u32)distSq;
        }
        if (particle->timer < g_iDeltaTimeMS) {
            particle->timer = g_iDeltaTimeMS;
        }
        ParticleGX_MacroStep(particle);
    }
    ParticleGX_MicroStep(particle);
    particle->timer -= g_iDeltaTimeMS;
}

void ParticleGX_MicroStep(void *arg0) {
    extern f32 g_fDeltaTimeMS;
    struct ParticleGX {
        Vector4 pos;
        u8 unk10[0x30 - 0x10];
        Vector4 quat;
        Vector4 axis;
        u8 unk50[0xB8 - 0x50];
        f32 spinRate;
        u8 unkBC[0xC8 - 0xBC];
        f32 spin;
        u8 unkCC[0xF0 - 0xCC];
        Vector4 vel;
    } *particle = arg0;
    Vector4 pos __attribute__((aligned(16)));
    Vector4 rot __attribute__((aligned(16)));
    Vector4 out __attribute__((aligned(16)));

    AccumulateVector(&particle->vel, &particle->pos, &pos, g_fDeltaTimeMS);
    particle->pos = pos;
    if (particle->spin) {
        GenerateQuaternion(&particle->axis, &rot, 0.01f * (g_fDeltaTimeMS * (particle->spinRate * particle->spin)));
        QuaternionMult(&particle->quat, &rot, &out);
        particle->quat = out;
    }
}

void ParticleGX_MacroStep(struct ParticleGX_MacroStep_t {
        Vector4 pos;
        Vector4 vel;
        Vector4 accel;
        Vector4 quat;
        Vector4 axis;
        u8 colour[0x30];
        u8 scale[0x30];
        f32 bounce;
        u8 flags;
        u8 unkB5;
        u8 visible;
        u8 frame;
        f32 speed;
        PTR32(u8) info;
        PTR32(void) texture;
        f32 unkC4;
        f32 spin;
        s32 life;
        f32 rate;
        s32 frameTimer;
        f32 unkD8;
        s32 timer;
        Vector4 targetPos;
        Vector4 targetVel;
    } *particle) {
    extern Vector4 g_zeroVector;
    typedef struct {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        s32 unk44;
        f32 unk48;
        f32 unk4C;
    } FxParams;
    typedef struct {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[0x30];
    } RayHit;
    FxParams params __attribute__((aligned(16)));
    RayHit hit __attribute__((aligned(16)));
    FxParams params2 __attribute__((aligned(16)));
    Vector4 newPos __attribute__((aligned(16)));
    f32 t;
    f32 dt;
    f32 d;
    f32 rest;
    u8 *anim;
    u32 fx;
    s32 idx;
    u32 sound;
    u8 bounced;

    anim = SLOT_LOAD(u8 *, (GETP(particle->info) + 0x4));
    idx = RandomAdd_s32(0, *(s32 *)(GETP(particle->info) + 0x1C));
    dt = particle->timer;
    if (particle->life < 0) {
        if (*(u32 *)(GETP(particle->info) + 0x1C) != 0) {
            fx = (SLOT_LOAD(u32 *, (GETP(particle->info) + 0x20)))[idx];
            if (fx != 0) {
                params.id = fx;
                params.pos = particle->pos;
                params.vel = particle->vel;
                params.quat = particle->quat;
                params.unk44 = 0;
                params.unk48 = 0.0f;
                params.unk4C = 0.0f;
                params.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &params);
            }
        }
        particle->flags |= 8;
        return;
    }
    particle->life -= particle->timer;
    if (anim[3] != 0) {
        if (particle->frameTimer < 0) {
            particle->frameTimer = *(s32 *)(anim + 0x14);
            particle->frame++;
            if (particle->frame >= *(u32 *)(anim + 0x20)) {
                particle->frame = 0;
            }
        } else {
            particle->frameTimer -= particle->timer;
        }
    }
    ExtractColorKey(GETP(particle->info) + 0x14, particle->colour, dt * particle->rate);
    ExtractScaleKey(anim + 0x28, particle->scale, dt * particle->rate);
    if (anim[1] & 4) {
        return;
    }
    AccumulateVector(&particle->accel, &particle->vel, &particle->vel, dt);
    AccumulateVector(&particle->vel, &particle->pos, &newPos, dt);
    if (particle->flags & 1) {
        t = 1.0f;
        bounced = 0;
        if (RayHitsTerrain(particle, &newPos, &t, &hit)) {
            AccumulateVector(&particle->vel, &particle->pos, &particle->pos, t * dt);
            if (anim[1] == 1) {
                if (*(u32 *)(GETP(particle->info) + 0x1C) != 0) {
                    fx = (SLOT_LOAD(u32 *, (GETP(particle->info) + 0x20)))[idx];
                    if (fx != 0) {
                        params2.id = fx;
                        params2.pos = particle->pos;
                        params2.vel = particle->vel;
                        params2.quat = particle->quat;
                        params2.unk44 = 0;
                        params2.unk48 = 0.0f;
                        params2.unk4C = 0.0f;
                        params2.angVel = g_zeroVector;
                        ThingManager_DARYL_CreateThing(1, &params2);
                    }
                }
                particle->flags |= 8;
                return;
            }
            if (anim[1] == 3) {
                bounced = 1;
            } else {
                bounced = 1;
                sound = *(u32 *)(anim + 0x18);
                rest = (1.0f - t) * dt;
                d = 2.0f * (particle->vel.x * hit.normal.x + particle->vel.y * hit.normal.y + particle->vel.z * hit.normal.z);
                particle->vel.x -= d * hit.normal.x;
                particle->vel.y -= d * hit.normal.y;
                particle->vel.z -= d * hit.normal.z;
                particle->vel.x *= particle->bounce;
                particle->vel.y *= particle->bounce;
                particle->vel.z *= particle->bounce;
                AccumulateVector(&particle->vel, &particle->pos, &newPos, rest);
                if (sound != 0 && d * d > 0.002) {
                    SoundSequence_Play(PTR(sound), particle);
                }
            }
        }
        if (bounced) {
            if (!(particle->flags & 2) ||
                particle->vel.x * particle->vel.x + particle->vel.y * particle->vel.y + particle->vel.z * particle->vel.z < 0.0001f ||
                anim[1] == 3) {
                particle->vel.x = particle->vel.y = particle->vel.z = particle->spin = 0.0f;
                particle->flags |= 4;
                if (anim[1] == 3) {
                    particle->accel.x = 0.0f;
                    particle->accel.y = 0.0f;
                    particle->accel.x = 0.0f;
                }
            } else if (particle->vel.x * particle->vel.x + particle->vel.z * particle->vel.z > 2e-05f) {
                if (particle->bounce < 0.7f) {
                    particle->spin *= 0.3f + particle->bounce;
                }
                RandomNumberGenerator_GetRandomVector(&particle->axis);
            } else {
                particle->spin = 0.0f;
            }
        }
    }
    particle->targetPos = newPos;
    particle->targetVel.x = (particle->targetPos.x - particle->pos.x) / particle->timer;
    particle->targetVel.y = (particle->targetPos.y - particle->pos.y) / particle->timer;
    particle->targetVel.z = (particle->targetPos.z - particle->pos.z) / particle->timer;
}

void ParticleSprite_Move(u8 *sprite, void *sprites) {
    extern Vector4 g_zeroVector;
    extern f32 g_fDeltaTimeMS;
    extern f32 g_fDeltaTimeSec;
    typedef struct {
        Vector4 pos;
        Vector4 quat;
        Vector4 vel;
        Vector4 angVel;
        u32 id;
        s32 unk44;
        f32 unk48;
        f32 unk4C;
    } FxParams;
    FxParams params __attribute__((aligned(16)));
    f32 dt;
    u8 *anim;
    s32 idx;
    u32 fx;
    s32 step;

    dt = g_fDeltaTimeMS * *(f32 *)(sprite + 0x98);
    anim = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (sprite + 0x90)) + 0x4));
    idx = RandomAdd_s32(0, *(s32 *)(SLOT_LOAD(u8 *, (sprite + 0x90)) + 0x1C));
    if (*(s32 *)(sprite + 0x88) < 0) {
        if (*(u32 *)(SLOT_LOAD(u8 *, (sprite + 0x90)) + 0x1C) != 0) {
            fx = (SLOT_LOAD(u32 *, (SLOT_LOAD(u8 *, (sprite + 0x90)) + 0x20)))[idx];
            if (fx != 0) {
                params.id = fx;
                params.pos = *(Vector4 *)(sprite + 0x0);
                params.vel = *(Vector4 *)(sprite + 0x10);
                params.quat.x = 0.0f;
                params.quat.y = 0.0f;
                params.quat.z = 0.0f;
                params.quat.w = 1.0f;
                params.unk44 = 0;
                params.unk48 = 0.0f;
                params.unk4C = 0.0f;
                params.angVel = g_zeroVector;
                ThingManager_DARYL_CreateThing(1, &params);
            }
        }
        sprite[0x94] |= 8;
        return;
    }
    step = dt;
    *(s32 *)(sprite + 0x88) -= step;
    if (anim != NULL && anim[1] != 0) {
        if (*(s32 *)(sprite + 0xA0) < 0) {
            *(s32 *)(sprite + 0xA0) = *(s32 *)(anim + 0x1C);
            sprite[0x97]++;
            if (sprite[0x97] >= *(u32 *)(anim + 0x20)) {
                sprite[0x97] = 0;
            }
        } else {
            *(s32 *)(sprite + 0xA0) -= step;
        }
    }
    ExtractColorKey(SLOT_LOAD(u8 *, (sprite + 0x90)) + 0x14, sprite + 0x30, dt * *(f32 *)(sprite + 0x9C));
    ExtractF32Key(anim + 0x28, sprite + 0x60, dt * *(f32 *)(sprite + 0x9C));
    if (*(f32 *)(sprite + 0x84)) {
        *(f32 *)(sprite + 0x80) += *(f32 *)(sprite + 0x98) * (*(f32 *)(sprite + 0x84) * g_fDeltaTimeSec);
    }
}

void ParticleManager_Render(void) {
    typedef struct {
        u8 unk0[0x94];
        u8 flags;
        u8 unk95[0xB0 - 0x95];
    } ParticleSprite;
    typedef struct {
        u8 unk0[0xB4];
        u8 flags;
        u8 unkB5[0x100 - 0xB5];
    } ParticleGX;
    typedef struct {
        struct {
            ParticleGX particles[0x280];
            u8 unk28000[0x10];
        } groups[3];
        ParticleSprite sprites[0x200];
        u8 unk8E030[0x10];
    } ParticleManager;
    #define g_particleManager (*(ParticleManager *)(void *)&g_particleManager)
    #define g_numParticlesCreatedThisFrame (*(s32 *)(void *)&g_numParticlesCreatedThisFrame)
    ParticleManager *pm = &g_particleManager;
    u32 j;
    u32 i;
    ParticleSprite *sprite;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 0x280; j++) {
            if (!(pm->groups[i].particles[j].flags & 8)) {
                ParticleGX_Render(&pm->groups[i].particles[j]);
            }
        }
    }
    for (j = 0, sprite = pm->sprites; j < 0x200; j++, sprite++) {
        if (!(pm->sprites[j].flags & 8)) {
            ParticleSprite_Render(sprite);
        }
    }
    g_numParticlesCreatedThisFrame = 0;
#undef g_numParticlesCreatedThisFrame
#undef g_particleManager
}

void ParticleGX_Render(u8 *particle) {
    u32 color;
    Vector4 scale __attribute__((aligned(16)));

    SLOT_STORE((particle + 0xC0), SLOT_LOAD(void *, (u32 *)SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (particle + 0xBC)) + 0x4)) + 0x24)) + (particle[0xB7])));
    if (particle[0xB6] != 0) {
        color = *(u32 *)(particle + 0x74);
        Gfx_SetBaseColor(&color);
        Gfx_EnableState(0x10);
    }
    *(f32 *)(particle + 0xC) = 1.0f;
    scale.x = *(f32 *)(particle + 0xD8) * (*(f32 *)(particle + 0x80) * *(f32 *)(particle + 0xC4));
    scale.y = *(f32 *)(particle + 0xD8) * (*(f32 *)(particle + 0x84) * *(f32 *)(particle + 0xC4));
    scale.z = *(f32 *)(particle + 0xD8) * (*(f32 *)(particle + 0x88) * *(f32 *)(particle + 0xC4));
    scale.w = 1.0f;
    Gfx_DrawGeometry_QuaternionScalePos(HND(SLOT_LOAD(void *, (particle + 0xC0))), HND(particle + 0x30), HND(&scale), HND(particle));
    if (particle[0xB6] != 0) {
        Gfx_DisableState(0x10);
    }
}

void ParticleSprite_Render(u8 *sprite) {
    u8 *Gfx_AddBillBoardSprite(u16);
    u8 *bb;

    bb = Gfx_AddBillBoardSprite((SLOT_LOAD(u32 *, (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (sprite + 0x90)) + 0x4)) + 0x24)))[sprite[0x97]]);
    bb[0x2A] = sprite[0x95];
    *(s32 *)(bb + 0x24) = *(s32 *)(sprite + 0x54);
    *(f32 *)(bb + 0x1C) = 360.0f - *(f32 *)(sprite + 0x80);
    *(f32 *)(bb + 0xC) = *(f32 *)(sprite + 0x60) * *(f32 *)(sprite + 0x8C);
    *(f32 *)(bb + 0x0) = *(f32 *)(sprite + 0x0);
    *(f32 *)(bb + 0x4) = *(f32 *)(sprite + 0x4);
    *(f32 *)(bb + 0x8) = *(f32 *)(sprite + 0x8);
}
