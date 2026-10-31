/* Particles: pooled geometry particles (three rings of ParticleGX, the first
 * for the ones that collide) and billboard sprites, created from a
 * ParticleInfo and run outside the Thing system. Geometry particles are
 * simulated in "macro steps" whose length grows with the distance to the
 * cameras, and glide in between. */
#include "common.h"
#include "Particle.h"

extern u8 g_particleManager[];
extern u8 g_numParticlesCreatedThisFrame[];

#define PARTICLES ((ParticleManager *)(void *)g_particleManager)
#define NUM_CREATED_THIS_FRAME (*(u32 *)(void *)g_numParticlesCreatedThisFrame)

void ParticleManager_Init(void) {
    ParticleManager *pm = PARTICLES;
    u32 j;
    u32 i;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < PARTICLE_GX_COUNT; j++) {
            pm->groups[i].particles[j].flags |= PARTICLE_FLAG_AT_REST | PARTICLE_FLAG_FREE;
        }
        pm->groups[i].unk28004 = 0;
        pm->groups[i].next = 0;
    }
    for (j = 0; j < PARTICLE_SPRITE_COUNT; j++) {
        pm->sprites[j].flags |= PARTICLE_FLAG_AT_REST | PARTICLE_FLAG_FREE;
    }
    pm->spriteNext = 0;
    NUM_CREATED_THIS_FRAME = 0;
}

/* Whether another particle may be made this frame: the more have been, the
 * fewer of the low-priority ones (high priority value) get through. */
s8 ParticleManager_CanCreatureParticle(struct ParticleInfo *info) {
    u32 count;

    count = ++NUM_CREATED_THIS_FRAME;
    if (count <= 11) {
        return 1;
    }
    if (count <= 21) {
        return info->priority <= 8;
    }
    if (count <= 31) {
        return info->priority <= 6;
    }
    if (count <= 41) {
        return info->priority <= 4;
    }
    if (count <= 51) {
        return info->priority <= 2;
    }
    if (count <= 101) {
        return info->priority <= 1;
    }
    return 0;
}

void CreateParticle(Vector4 *pos, Vector4 *vel, Vector4 *accel, Vector4 *quat, struct ParticleInfo *info, f32 speed, f32 age) {
    switch (info->type) {
    case PARTICLE_NONE:
        break;
    case PARTICLE_GX:
        CreateParticleGX(pos, vel, accel, quat, info, speed, age);
        break;
    case PARTICLE_SPRITE:
        CreateParticleSprite(pos, vel, accel, quat, info, speed, age);
        break;
    case PARTICLE_LINE:
        LineManager_Add(info, pos, vel, accel);
        break;
    }
}

/* Whether a particle needs colouring: not when the colour track has no keys
 * or a single white one. */
static inline u8 Particle_Tinted(ParticleInfo *info) {
    GXColor c;

    if (info->colorKeys.count == 0) {
        return 0;
    } else if (info->colorKeys.count == 1) {
        c = *(GXColor *)GETP(info->colorKeys.keys);
        if (c.r == 0xFF && c.b == 0xFF && c.g == 0xFF) {
            return 0;
        }
    }
    return 1;
}

/* Take the next slot of a ring (overwriting the oldest particle) and start
 * it `age` (0-1) through its life. */
void CreateParticleGX(Vector4 *pos, Vector4 *vel, Vector4 *accel, Vector4 *quat, struct ParticleInfo *info, f32 speed, f32 age) {
    extern Vector4 g_zeroVector;
    extern s32 g_iDeltaTimeMS;
    static s8 which;
    ParticleGroup *group;
    ParticleGX *particle;
    ParticleGXAnim *anim;
    s32 life;
    f32 lifeF;
    u32 time;
    u32 idx;
    f32 rate;

    life = RandomAdd_s32(info->lifeBase, info->lifeRange);
    lifeF = life;
    anim = GETP(info->anim);
    time = age * lifeF;
    if (anim != NULL) {
        if (anim->collide & 3) {
            group = &PARTICLES->groups[0];
        } else if (which++ & 1) {
            group = &PARTICLES->groups[1];
        } else {
            group = &PARTICLES->groups[2];
        }
        idx = group->next++;
        if (group->next >= PARTICLE_GX_COUNT) {
            group->next = 0;
        }
        particle = &group->particles[idx];
        if (anim->animated != 0) {
            particle->frame = 0;
            particle->frameTimer = anim->frameTime;
        } else {
            particle->frame = RandomAdd_s32(0, anim->numFrames);
        }
        particle->geometry.h = GETP(anim->frames)[particle->frame];
        if (GETP(particle->geometry) != NULL) {
            particle->speed = speed;
            particle->pos = *pos;
            particle->vel = *vel;
            particle->accel = *accel;
            SETP(particle->info, info);
            rate = 1.0f;
            particle->scaleMul = rate;
            particle->life = life - time;
            particle->timer = g_iDeltaTimeMS;
            particle->macroVel = g_zeroVector;
            particle->macroPos = *pos;
            particle->rate = rate;
            if (info->lifeRange != 0 && info->lifeBase != 0) {
                particle->rate = rate / (lifeF / info->lifeBase);
            }
            particle->scale.reset = 1;
            particle->color.reset = 1;
            ExtractColorKey(&GETP(particle->info)->colorKeys, &particle->color, time);
            particle->size = RandomAdd(anim->sizeBase, anim->sizeRange);
            ExtractScaleKey(&anim->scaleKeys, &particle->scale, time);
            particle->tinted = Particle_Tinted(GETP(particle->info));
            particle->flags = anim->collide != 0;
            if (anim->collide > 1) {
                particle->flags |= PARTICLE_FLAG_BOUNCES;
            }
            particle->flags &= ~(PARTICLE_FLAG_AT_REST | PARTICLE_FLAG_FREE);
            if (anim->collide == 3) {
                particle->bounce = 0.0f;
            } else {
                particle->bounce = 0.75f;
            }
            if (anim->orient == 0) {
                particle->quat = *(Quaternion *)quat;
            } else if (anim->orient == 1) {
                RandomNumberGenerator_GetRandomQuaternion(&particle->quat);
            } else {
                particle->quat.x = quat->x = 0.0f;
                particle->quat.y = quat->y = 0.0f;
                particle->quat.z = quat->z = 0.0f;
                particle->quat.w = quat->w = 1.0f;
            }
            if (anim->spinBase || anim->spinRange) {
                switch (anim->spinAxis) {
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
                particle->spin = 0.1f * (0.017453292f * RandomAdd(anim->spinBase, anim->spinRange));
            } else {
                particle->spin = 0.0f;
                particle->axis.x = 1.0f;
                particle->axis.y = 0.0f;
                particle->axis.z = 0.0f;
            }
        }
    }
}

void CreateParticleSprite(Vector4 *pos, Vector4 *vel, Vector4 *accel, void *owner, struct ParticleInfo *info, f32 speed, f32 age) {
    ParticleManager *pm = PARTICLES;
    ParticleSprite *sprite;
    ParticleSpriteAnim *anim;
    s32 life;
    f32 lifeF;
    u32 time;
    u32 idx;
    f32 rate;

    life = RandomAdd_s32(info->lifeBase, info->lifeRange);
    lifeF = life;
    time = age * lifeF;
    anim = GETP(info->anim);
    if (anim != NULL) {
        idx = pm->spriteNext++;
        if (idx + 1 >= PARTICLE_SPRITE_COUNT) {
            pm->spriteNext = 0;
        }
        sprite = &pm->sprites[idx];
        sprite->pos = *pos;
        sprite->vel = *vel;
        sprite->accel = *accel;
        SETP(sprite->info, info);
        sprite->flags &= ~(PARTICLE_FLAG_AT_REST | PARTICLE_FLAG_FREE);
        if (anim->animated != 0) {
            sprite->frame = 0;
            sprite->frameTimer = anim->frameTime;
        } else {
            sprite->frame = RandomAdd_s32(0, anim->numFrames);
        }
        sprite->life = life;
        sprite->speed = speed;
        rate = 1.0f;
        sprite->rate = rate;
        if (info->lifeRange != 0 && info->lifeBase != 0) {
            sprite->rate = rate / (lifeF / info->lifeBase);
        }
        sprite->blend = anim->blend;
        sprite->size.reset = 1;
        sprite->color.reset = 1;
        ExtractColorKey(&GETP(sprite->info)->colorKeys, &sprite->color, time);
        sprite->sizeMul = RandomAdd(anim->sizeBase, anim->sizeRange);
        ExtractF32Key(&anim->sizeKeys, &sprite->size, time);
        sprite->rot = RandomDirVar(anim->rotBase, anim->rotRange);
        sprite->rotSpeed = RandomAdd(anim->rotSpeedBase, anim->rotSpeedRange);
        sprite->tinted = Particle_Tinted(GETP(sprite->info));
    }
}

void ParticleManager_Heartbeat(void) {
    extern s8 g_bFreezeWorld;
    extern f32 g_fDeltaTimeMS;
    ParticleManager *pm;
    u32 j;
    u32 i;
    ParticleSprite *sprite;
    ParticleSprite *sprites;
    f32 dt;

    if (!g_bFreezeWorld) {
        pm = PARTICLES;
        for (i = 0; i < 3; i++) {
            for (j = 0; j < PARTICLE_GX_COUNT; j++) {
                if (!(pm->groups[i].particles[j].flags & PARTICLE_FLAG_FREE)) {
                    ParticleGX_Move(&pm->groups[i].particles[j]);
                }
            }
        }
        dt = g_fDeltaTimeMS;
        sprites = pm->sprites;
        sprite = pm->sprites;
        for (j = 0; j < PARTICLE_SPRITE_COUNT; j++) {
            if (!(pm->sprites[j].flags & PARTICLE_FLAG_FREE)) {
                ParticleSprite_Move(&sprite[j], sprites);
            }
        }
        sprite = pm->sprites;
        for (j = 0; j < PARTICLE_SPRITE_COUNT; j++) {
            if (!(pm->sprites[j].flags & PARTICLE_FLAG_FREE)) {
                AccumulateVector(&sprite[j].accel, &sprite[j].vel, &sprite[j].vel, dt);
                AccumulateVector(&sprite[j].vel, &sprite[j].pos, &sprite[j].pos, dt);
            }
        }
    }
}

/* Start a macro step when the last one is over: arrive at its end, pick the
 * next length from the camera distance (up to 700 ms at 1000 units). */
void ParticleGX_Move(struct ParticleGX *particle) {
    extern s32 g_iDeltaTimeMS;
    f32 distSq;

    if (particle->timer <= 0) {
        particle->pos = particle->macroPos;
        distSq = Gfx_GetMinCameraDistSquared(&particle->pos);
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

/* Glide towards the macro step's end and spin. */
void ParticleGX_MicroStep(struct ParticleGX *particle) {
    extern f32 g_fDeltaTimeMS;
    Vector4 pos __attribute__((aligned(16)));
    Quaternion rot __attribute__((aligned(16)));
    Quaternion out __attribute__((aligned(16)));

    AccumulateVector(&particle->macroVel, &particle->pos, &pos, g_fDeltaTimeMS);
    particle->pos = pos;
    if (particle->spin) {
        GenerateQuaternion(&particle->axis, &rot, 0.01f * (g_fDeltaTimeMS * (particle->speed * particle->spin)));
        QuaternionMult(&particle->quat, &rot, &out);
        particle->quat = out;
    }
}

/* One of the info's death effects (DebrisParams.debris), at random. */
static inline void Particle_SpawnDeathFx(ParticleInfo *info, s32 idx, Vector4 *pos, Vector4 *vel, Quaternion *quat) {
    extern Vector4 g_zeroVector;
    DebrisParams params __attribute__((aligned(16)));
    u32 fx;

    if (info->numDeathFx != 0) {
        fx = GETP(info->deathFx)[idx];
        if (fx != 0) {
            params.debris = fx;
            params.pos = *pos;
            params.vel = *vel;
            params.quat = *(Vector4 *)quat;
            params.owner = 0;
            params.unk48 = 0.0f;
            params.unk4C = 0.0f;
            params.angVel = g_zeroVector;
            ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &params);
        }
    }
}

/* Simulate the particle over the macro step (timer ms): age it (dying with
 * a death effect), animate it, move it and collide with the terrain by its
 * anim's collide mode; then set the glide towards where it ends up. */
void ParticleGX_MacroStep(struct ParticleGX *particle) {
    typedef struct {
        Vector4 pos;
        Vector4 normal;
        u8 pad20[0x30];
    } RayHit;
    RayHit hit __attribute__((aligned(16)));
    Vector4 newPos __attribute__((aligned(16)));
    ParticleInfo *info = GETP(particle->info);
    f32 t;
    f32 dt;
    f32 d;
    f32 rest;
    ParticleGXAnim *anim;
    s32 idx;
    u32 sound;
    u8 bounced;

    anim = GETP(info->anim);
    idx = RandomAdd_s32(0, info->numDeathFx);
    dt = particle->timer;
    if (particle->life < 0) {
        Particle_SpawnDeathFx(info, idx, &particle->pos, &particle->vel, &particle->quat);
        particle->flags |= PARTICLE_FLAG_FREE;
        return;
    }
    particle->life -= particle->timer;
    if (anim->animated != 0) {
        if (particle->frameTimer < 0) {
            particle->frameTimer = anim->frameTime;
            particle->frame++;
            if (particle->frame >= anim->numFrames) {
                particle->frame = 0;
            }
        } else {
            particle->frameTimer -= particle->timer;
        }
    }
    ExtractColorKey(&info->colorKeys, &particle->color, dt * particle->rate);
    ExtractScaleKey(&anim->scaleKeys, &particle->scale, dt * particle->rate);
    if (anim->collide & 4) {
        return;
    }
    AccumulateVector(&particle->accel, &particle->vel, &particle->vel, dt);
    AccumulateVector(&particle->vel, &particle->pos, &newPos, dt);
    if (particle->flags & PARTICLE_FLAG_COLLIDES) {
        t = 1.0f;
        bounced = 0;
        if (RayHitsTerrain(&particle->pos, &newPos, &t, &hit)) {
            AccumulateVector(&particle->vel, &particle->pos, &particle->pos, t * dt);
            if (anim->collide == 1) {
                Particle_SpawnDeathFx(info, idx, &particle->pos, &particle->vel, &particle->quat);
                particle->flags |= PARTICLE_FLAG_FREE;
                return;
            }
            if (anim->collide == 3) {
                bounced = 1;
            } else {
                bounced = 1;
                sound = anim->bounceSound;
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
            if (!(particle->flags & PARTICLE_FLAG_BOUNCES) ||
                particle->vel.x * particle->vel.x + particle->vel.y * particle->vel.y + particle->vel.z * particle->vel.z < 0.0001f ||
                anim->collide == 3) {
                particle->vel.x = particle->vel.y = particle->vel.z = particle->spin = 0.0f;
                particle->flags |= PARTICLE_FLAG_AT_REST;
                if (anim->collide == 3) {
                    particle->accel.x = 0.0f;
                    particle->accel.y = 0.0f;
                    particle->accel.x = 0.0f;   /* sic: z is left as it is */
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
    particle->macroPos = newPos;
    particle->macroVel.x = (particle->macroPos.x - particle->pos.x) / particle->timer;
    particle->macroVel.y = (particle->macroPos.y - particle->pos.y) / particle->timer;
    particle->macroVel.z = (particle->macroPos.z - particle->pos.z) / particle->timer;
}

/* Age, animate and spin a sprite (it moves in ParticleManager_Heartbeat). */
void ParticleSprite_Move(struct ParticleSprite *sprite, void *sprites) {
    extern f32 g_fDeltaTimeMS;
    extern f32 g_fDeltaTimeSec;
    Quaternion identity = {0.0f, 0.0f, 0.0f, 1.0f};
    ParticleInfo *info = GETP(sprite->info);
    f32 dt;
    ParticleSpriteAnim *anim;
    s32 idx;
    s32 step;

    dt = g_fDeltaTimeMS * sprite->speed;
    anim = GETP(info->anim);
    idx = RandomAdd_s32(0, info->numDeathFx);
    if (sprite->life < 0) {
        Particle_SpawnDeathFx(info, idx, &sprite->pos, &sprite->vel, &identity);
        sprite->flags |= PARTICLE_FLAG_FREE;
        return;
    }
    step = dt;
    sprite->life -= step;
    if (anim != NULL && anim->animated != 0) {
        if (sprite->frameTimer < 0) {
            sprite->frameTimer = anim->frameTime;
            sprite->frame++;
            if (sprite->frame >= anim->numFrames) {
                sprite->frame = 0;
            }
        } else {
            sprite->frameTimer -= step;
        }
    }
    ExtractColorKey(&info->colorKeys, &sprite->color, dt * sprite->rate);
    ExtractF32Key(&anim->sizeKeys, &sprite->size, dt * sprite->rate);
    if (sprite->rotSpeed) {
        sprite->rot += sprite->speed * (sprite->rotSpeed * g_fDeltaTimeSec);
    }
}

void ParticleManager_Render(void) {
    ParticleManager *pm = PARTICLES;
    u32 j;
    u32 i;
    ParticleSprite *sprite;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < PARTICLE_GX_COUNT; j++) {
            if (!(pm->groups[i].particles[j].flags & PARTICLE_FLAG_FREE)) {
                ParticleGX_Render(&pm->groups[i].particles[j]);
            }
        }
    }
    for (j = 0, sprite = pm->sprites; j < PARTICLE_SPRITE_COUNT; j++, sprite++) {
        if (!(pm->sprites[j].flags & PARTICLE_FLAG_FREE)) {
            ParticleSprite_Render(sprite);
        }
    }
    NUM_CREATED_THIS_FRAME = 0;
}

void ParticleGX_Render(struct ParticleGX *particle) {
    u32 color;
    Vector4 scale __attribute__((aligned(16)));

    particle->geometry.h = GETP(((ParticleGXAnim *)GETP(GETP(particle->info)->anim))->frames)[particle->frame];
    if (particle->tinted != 0) {
        color = *(u32 *)&particle->color.color;
        Gfx_SetBaseColor(&color);
        Gfx_EnableState(0x10);
    }
    particle->pos.w = 1.0f;
    scale.x = particle->size * (particle->scale.v.x * particle->scaleMul);
    scale.y = particle->size * (particle->scale.v.y * particle->scaleMul);
    scale.z = particle->size * (particle->scale.v.z * particle->scaleMul);
    scale.w = 1.0f;
    Gfx_DrawGeometry_QuaternionScalePos(particle->geometry.h, HND(&particle->quat), HND(&scale), HND(&particle->pos));
    if (particle->tinted != 0) {
        Gfx_DisableState(0x10);
    }
}

void ParticleSprite_Render(struct ParticleSprite *sprite) {
    FxBillboard *Gfx_AddBillBoardSprite(u16);
    FxBillboard *bb;

    bb = Gfx_AddBillBoardSprite(GETP(((ParticleSpriteAnim *)GETP(GETP(sprite->info)->anim))->frames)[sprite->frame]);
    bb->blend = sprite->blend;
    bb->color = sprite->color.color;
    bb->rot = 360.0f - sprite->rot;
    bb->size = sprite->size.value * sprite->sizeMul;
    bb->x = sprite->pos.x;
    bb->y = sprite->pos.y;
    bb->z = sprite->pos.z;
}
