/* The creature AI: one AI player (a ring of them, AICreature.next) that
 * drives one creature at a time. It spawns creatures near the cameras from
 * the world's creature tiles and creature volumes, walks them between the
 * volume's nodes or wanders, makes them notice passing cars, and removes
 * them once no camera is near. */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "AI.h"

SpawnVolume gs_aSpawnVolumes[256];
AICreature *gs_pCurrentAICreature;
u32 g_volumeTotal;
u32 gs_currentVolume;

#define AI_THING(ai) ((Creature *)GETP(AI_PLAYER(ai)->vehicle))

/* The squared distance from p to the nearest camera's look point (150 in
 * front of it), at most `limit`. */
static inline f32 AI_Creature_CameraDistSq(Vector4 *p, f32 limit) {
    Vector4 look __attribute__((aligned(16)));
    f32 minDist = limit;
    f32 dist;
    u32 i;

    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        PSVECScale(&CAMERA_MANAGER->cameras[i].dir, &look, 150.0f);
        PSVECAdd(&CAMERA_MANAGER->cameras[i].pos, &look, &look);
        dist = PSVECSquareDistance(&look, p);
        if (minDist > dist) {
            minDist = dist;
        }
    }
    return minDist;
}

/* Free the AI and its creature once no camera looks within 600 of it (or
 * the creature is gone); the spawn counts / tile are given back. */
s32 AI_Creature_Deactivate(AICreature *ai) {
    Creature *thing;
    SpawnVolume *spawner;
    TileCreature *tile;

    thing = AI_THING(ai);
    if (thing != NULL && !(thing->base.flags & THING_FLAG_FREE)) {
        if (360000.0f == AI_Creature_CameraDistSq(&thing->base.pos, 360000.0f)) {
            ai->base.state = AI_STATE_OFF;
            spawner = GETP(ai->volume);
            if (spawner != NULL) {
                spawner->alive--;
                GETP(ai->volume)->counts[ai->volumeNode]--;
            } else {
                thing = AI_THING(ai);
                tile = GETP(thing->base.trigger);
                if (tile != NULL && thing->deadTime == 0 && ai->leaving == 0) {
                    /* alive and unseen: the tile may spawn it again */
                    tile->mode = 1;
                    ((TileCreature *)GETP(AI_THING(ai)->base.trigger))->thing = 0;
                }
            }
            Creature_Release(AI_THING(ai));
            return 1;
        }
    } else {
        ai->base.state = AI_STATE_OFF;
        spawner = GETP(ai->volume);
        if (spawner != NULL) {
            spawner->alive--;
            GETP(ai->volume)->counts[ai->volumeNode]--;
        }
        return 1;
    }
    return 0;
}

/* Spawn a creature for the AI: at node `index` of a spawn volume (a random
 * creature of the volume), or, with index -1, from a TileCreature. */
s8 AI_Creature_Spawn(AICreature *ai, void *spawner, u32 index) {
    CreatureInit params __attribute__((aligned(16)));
    CreatureVolumeInfo *info;
    u32 *list;
    Creature *thing;
    TileCreature *tile = spawner;

    AI_Reset(ai);
    SETP(ai->nearestCar, NULL);
    ai->wanderTime = 0;
    ai->unkFC = 0;
    ai->leaving = 0;
    ai->base.state = AI_STATE_OFF;
    ai->base.heartbeatTimer = 0;
    SETP(ai->volume, NULL);
    ai->volumeNode = -1;
    SETP(ai->node, NULL);
    ai->speed = (f32)(rand() % 251 + 750) / 1000.0f;
    if (rand() % 4) {
        ai->run = 1;
    } else {
        ai->run = 0;
    }
    ai->base.script.h = 0;
    if (index != -1) {
        SETP(ai->volume, spawner);
        info = GETP(GETP(((SpawnVolume *)spawner)->volume)->info);
        SETP(ai->node, GETP(GETP(info->nodes)[index]));
        list = GETP(info->creatures);
        params.info.h = ((u32 *)PTR(list[1]))[rand() % list[0]];
        params.pos = GETP(ai->node)->pos;
        params.heading = 0.017453292f * (f32)(rand() % 360);
        SETP(AI_PLAYER(ai)->tileCar, NULL);
    } else {
        SETP(params.info, GETP(tile->info));
        params.pos = tile->pos;
        params.heading = tile->angle;
        SETP(ai->volume, NULL);
        AI_PLAYER(ai)->tileCar.h = HND(tile);   /* creature AIs: their TileCreature */
        switch (GameSettings_GetDifficulty()) {
        case 0:
            ai->base.script.h = tile->scripts[0];
            break;
        case 1:
            ai->base.script.h = tile->scripts[1];
            break;
        case 2:
            ai->base.script.h = tile->scripts[2];
            break;
        case 3:
            ai->base.script.h = tile->scripts[3];
            break;
        }
    }
    SETP(params.player, ai);
    params.pos.y += 0.1f;
    if (ai->base.script.h == 0 && GETP(params.info) != NULL) {
        switch (GameSettings_GetDifficulty()) {
        case 0:
            ai->base.script.h = GETP(params.info)->aiScripts[0];
            break;
        case 1:
            ai->base.script.h = GETP(params.info)->aiScripts[1];
            break;
        case 2:
            ai->base.script.h = GETP(params.info)->aiScripts[2];
            break;
        case 3:
            ai->base.script.h = GETP(params.info)->aiScripts[3];
            break;
        }
    }
    thing = ThingManager_DARYL_CreateThing(THING_TYPE_CREATURE, &params);
    if (thing != NULL) {
        ai->base.state = AI_STATE_ACTIVE;
        ai->base.heartbeatTimer = 1;
        ai->volumeNode = index;
        Player_SetThing(GETP(params.player), thing);
        AI_PLAYER(ai)->flags |= PLAYER_FLAG_AI_HEADING;
        thing->base.trigger.h = AI_PLAYER(ai)->tileCar.h;
        return 1;
    }
    return 0;
}

/* With the AIs all busy, nothing; else spawn into the next volume that
 * isn't full, at a node 400-600 from the nearest camera. */
void AI_Creature_PopulateVolumes(void) {
    u32 v;
    u32 idx;
    CreatureVolumeInfo *info;
    CreatureNode *node;
    AICreature *ai;
    f32 minDist;

    ai = gs_pCurrentAICreature;
    do {
        if (ai->base.state == AI_STATE_OFF) {
            break;
        }
        ai = GETP(ai->next);
    } while (ai != gs_pCurrentAICreature);
    if (ai->base.state != AI_STATE_OFF) {
        return;
    }
    v = gs_currentVolume;
    do {
        if (++v == g_volumeTotal) {
            v = 0;
        }
        info = GETP(GETP(gs_aSpawnVolumes[v].volume)->info);
        if (GETP(gs_aSpawnVolumes[v].volume)->active == 0 || info->maxAlive == gs_aSpawnVolumes[v].alive || info->numNodes == 0 || info->disabled != 0) {
            continue;
        }
        idx = gs_aSpawnVolumes[v].index;
        do {
            if (++idx == info->numNodes) {
                idx = 0;
            }
            node = GETP(GETP(info->nodes)[idx]);
            if (node->enabled == 0 || (f32)gs_aSpawnVolumes[v].counts[idx] >= 1.0f + (f32)(info->maxAlive / info->numNodes)) {
                continue;
            }
            minDist = AI_Creature_CameraDistSq(&node->pos, 360000.0f);
            if (minDist > 160000.0f && minDist < 360000.0f) {
                gs_currentVolume = v;
                gs_aSpawnVolumes[v].index = idx;
                if (AI_Creature_Spawn(ai, &gs_aSpawnVolumes[v], idx)) {
                    GETP(ai->volume)->alive++;
                    GETP(ai->volume)->counts[idx]++;
                }
                return;
            }
        } while (idx != gs_aSpawnVolumes[v].index);
    } while (v != gs_currentVolume);
}

static inline void AI_Creature_InitSpawnVolumes(void) {
    u32 i;

    gs_currentVolume = 0;
    MemClr32(gs_aSpawnVolumes, g_volumeTotal * sizeof(SpawnVolume));
    for (i = 0; i < g_volumeTotal; i++) {
        SETP(gs_aSpawnVolumes[i].volume, VolumeManager_GetVolumeTypeByIndex(5, i));
    }
}

/* Track up to 8 cars: a point ahead of each, and (one car per call) how
 * far creatures notice it, less the faster it goes. */
static inline void AI_Creature_UpdateNearestCars(void) {
    extern NearestCar gs_aNearestCars[8];
    static u32 s_currentCar;
    Thing *cars[8];
    u32 count;
    u32 i;

    count = ThingManager_GetActiveCars(cars, 9);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            SETP(gs_aNearestCars[i].car, cars[i]);
            PSVECScale(&cars[i]->vel, &gs_aNearestCars[i].pos, 0.18f);
            PSVECAdd(&cars[i]->pos, &gs_aNearestCars[i].pos, &gs_aNearestCars[i].pos);
        }
        if (i < 8) {
            SETP(gs_aNearestCars[i].car, NULL);
        }
        if (s_currentCar >= count) {
            s_currentCar = 0;
        }
        gs_aNearestCars[s_currentCar].speed = PSVECSquareMag(&GETP(gs_aNearestCars[s_currentCar].car)->vel);
        if (gs_aNearestCars[s_currentCar].speed > 1.0f) {
            gs_aNearestCars[s_currentCar].range = 400.0f * (1.0f - gs_aNearestCars[s_currentCar].speed / 100000.0f);
        } else {
            gs_aNearestCars[s_currentCar].range = 400.0f;
        }
        s_currentCar++;
    }
}

/* Notice the nearest car within the script's distance (a new one makes the
 * creature pick a new speed and react at once). */
static inline void AI_Creature_FindNearestCar(AICreature *ai) {
    extern NearestCar gs_aNearestCars[8];
    f32 best;
    f32 dist;
    u32 i;

    best = ((CreatureAIScript *)GETP(ai->base.script))->noticeDistSq;
    for (i = 0; i < 8; i++) {
        if (GETP(gs_aNearestCars[i].car) == NULL) {
            break;
        }
        dist = PSVECSquareDistance(&AI_THING(ai)->base.pos, &gs_aNearestCars[i].pos);
        if (best > dist) {
            if (GETP(ai->nearestCar) != &gs_aNearestCars[i]) {
                SETP(ai->nearestCar, &gs_aNearestCars[i]);
                ai->speed = (f32)(rand() % 501 + 750) / 1000.0f;
                ai->wanderTime = 0;
                ai->base.heartbeatTimer = 1;
            }
            ai->nearestDistSq = best;
            best = dist;
        }
    }
}

void AI_Creature_AsyncProcess(void) {
    extern NearestCar gs_aNearestCars[8];
    static u32 gs_creatureTile;
    AICreature *ai;
    TileCreature *tile;
    u32 j;
    u32 i;
    u32 n;
    AICreature *player;
    AIPlayerParams desc;
    Vector4 p;

    if (g_volumeTotal == -1) {
        /* first call in a world: the volumes and the AI ring */
        gs_pCurrentAICreature = NULL;
        g_volumeTotal = VolumeManager_GetNumVolumesOfType(5);
        if (g_volumeTotal != 0) {
            AI_Creature_InitSpawnVolumes();
        } else if (World_GetTotalTiles_Creature() == 0) {
            goto update;
        }
        MemClr32(gs_aNearestCars, sizeof(gs_aNearestCars));
        for (i = 0; i < 1; i++) {
            desc.script = 0;
            desc.kind = AI_KIND_CREATURE;
            player = PlayerManager_NewAIPlayer(&desc);
            if (gs_pCurrentAICreature == NULL) {
                gs_pCurrentAICreature = player;
                SETP(player->next, player);
            } else {
                SETP(player->next, GETP(gs_pCurrentAICreature->next));
                SETP(gs_pCurrentAICreature->next, player);
            }
            player->base.state = AI_STATE_OFF;
        }
    }
update:
    if (gs_pCurrentAICreature == NULL) {
        return;
    }
    AI_Creature_UpdateNearestCars();
    for (n = 0; n < 10; n++) {
        if (gs_pCurrentAICreature->base.state != AI_STATE_OFF && gs_pCurrentAICreature->leaving == 0 &&
            !(s8)AI_Creature_Deactivate(gs_pCurrentAICreature)) {
            AI_Creature_FindNearestCar(gs_pCurrentAICreature);
        }
        gs_pCurrentAICreature = GETP(gs_pCurrentAICreature->next);
    }
    if (World_GetTotalTiles_Creature() != 0) {
        /* a free AI spawns the next live creature tile that a camera nears */
        ai = gs_pCurrentAICreature;
        do {
            if (ai->base.state == AI_STATE_OFF) {
                break;
            }
            ai = GETP(ai->next);
        } while (ai != gs_pCurrentAICreature);
        if (ai->base.state == AI_STATE_OFF) {
            gs_creatureTile++;
            if (gs_creatureTile >= World_GetTotalTiles_Creature()) {
                gs_creatureTile = 0;
            }
            tile = World_GetTileCreature(gs_creatureTile);
            if (tile->mode != 0 && GETP(tile->creature) == NULL) {
                for (j = 0; j < CAMERA_MANAGER->count; j++) {
                    PSVECScale(&CAMERA_MANAGER->cameras[j].dir, &p, 150.0f);
                    PSVECAdd(&CAMERA_MANAGER->cameras[j].pos, &p, &p);
                    if (360000.0f > PSVECSquareDistance(&p, &tile->pos)) {
                        if (AI_Creature_Spawn(ai, tile, -1)) {
                            SETP(AI_THING(ai)->base.trigger, tile);
                            SETP(tile->creature, AI_THING(ai));
                            tile->mode = 0;
                        }
                        break;
                    }
                }
            }
        }
    }
    if (g_volumeTotal != 0) {
        AI_Creature_PopulateVolumes();
    }
}

void AI_Creature_Heartbeat(void) {
}

/* Head for the next node, unless at it (or it's disabled) or inside the
 * volume, where it wanders: mostly forward at a random heading, now and then
 * a short way backwards. */
static inline void AI_Creature_HeadFor(AICreature *ai, Vector4 *target) {
    Player *p = AI_PLAYER(ai);
    Thing *thing = &AI_THING(ai)->base;
    f32 angle;
    f32 diff;

    angle = ArcTan(target->x - thing->pos.x, target->z - thing->pos.z);
    p->aiHeading = angle;
    thing = &AI_THING(ai)->base;
    diff = ArcTan(thing->axis[2].x, thing->axis[2].z) - angle;   /* unused (as in the original) */
    while (diff > 3.1415927f) {
        diff -= 6.2831855f;
    }
    while (diff < -3.1415927f) {
        diff += 6.2831855f;
    }
    if (ai->speed >= 0.0f) {
        p->stickY = 127;
    } else {
        p->stickY = -127;
    }
}

void AI_Creature_VolumeState(AICreature *ai) {
    Player *p = AI_PLAYER(ai);
    Creature *thing;
    f32 dist;
    u8 r;

    thing = AI_THING(ai);
    if (IsPointWithinVolume(&thing->base.pos, GETP(GETP(ai->volume)->volume), &dist) || GETP(ai->node)->enabled == 0) {
        if (ai->wanderTime == 0) {
            r = rand() % 50;
            if (r == 0) {
                ai->speed = (f32)(rand() % 501 + 500) / -1000.0f;
                ai->heading = 0.017453292f * (f32)(rand() % 360 - 180);
                ai->run = 0;
            } else if (r < 3) {
                ai->speed = (f32)(rand() % 251 + 750) / 1000.0f;
                ai->run = 0;
            } else {
                ai->speed = (f32)(rand() % 251 + 750) / 1000.0f;
                ai->heading = 0.017453292f * (f32)(rand() % 360 - 180);
                if (rand() % 4) {
                    ai->run = 1;
                } else {
                    ai->run = 0;
                }
            }
            ai->wanderTime = rand() % 128 + 127;
        }
        p->aiHeading = ai->heading;
        p->stickY = 127.0f * ai->speed;
    } else {
        if (ai->speed <= 0.75f) {
            ai->speed = (f32)(rand() % 251 + 750) / 1000.0f;
        }
        ai->base.heartbeatTimer = 0xFA;
        AI_Creature_HeadFor(ai, &GETP(ai->node)->pos);
        ai->wanderTime = rand() % 128 + 127;
        ai->heading = p->aiHeading;
    }
    if (ai->run) {
        p->button14 = 0xFF;
    }
}

void AI_Creature_AsyncHeartbeat(AICreature *ai) {
    Player *p = AI_PLAYER(ai);
    NearestCar *seen;
    Creature *thing;
    CreatureNode *node;
    CreatureNode *next;
    CreatureVolumeInfo *volInfo;

    if (ai->base.state == AI_STATE_OFF || ai->base.reset || GETP(p->vehicle) == NULL) {
        ai->base.heartbeatTimer = 0;
        return;
    }
    AI_AsyncHeartbeat(ai);
    Control_ClearControl(ai);
    /* at the node (or it's disabled): on to a random linked one */
    node = GETP(ai->node);
    if (node != NULL && (node->enabled == 0 || PSVECSquareDistance(&node->pos, &AI_THING(ai)->base.pos) < 25.0f)) {
        node = GETP(ai->node);
        if (node->numLinks != 0) {
            next = GETP(GETP(node->links)[rand() % node->numLinks]);
            if (next->enabled != 0) {
                SETP(ai->node, next);
            }
        }
    }
    if (ai->leaving != 0) {
        /* walk to a node and vanish there */
        if (ai->unkFC != 0 || 1.0f != AI_THING(ai)->unk1C0) {
            ai->base.heartbeatTimer = 1000;
        } else {
            volInfo = (CreatureVolumeInfo *)GETP(ai->volume);   /* sic: a SpawnVolume, read as its info */
            if (GETP(ai->node) == NULL) {
                if (volInfo->numNodes == 0) {
                    goto end;
                }
                SETP(ai->node, GETP(GETP(volInfo->nodes)[rand() % volInfo->numNodes]));
            }
            if (PSVECSquareDistance(&GETP(ai->node)->pos, &AI_THING(ai)->base.pos) < 25.0f) {
                Creature_Release(AI_THING(ai));
                ai->base.state = AI_STATE_OFF;
            } else {
                ai->base.heartbeatTimer = 0xFA;
                AI_Creature_HeadFor(ai, &GETP(ai->node)->pos);
                p->button14 = 0xFF;
            }
        }
    } else {
        thing = AI_THING(ai);
        if (thing->unk1B8 & 1) {
            /* running about */
            if (ai->wanderTime == 0) {
                ai->speed = (f32)(rand() % 201 + 800) / 1000.0f;
                ai->heading = 0.017453292f * (f32)(rand() % 360 - 180);
                ai->run = 1;
                ai->wanderTime = rand() % 64 + 64;
            }
            p->aiHeading = ai->heading;
            if (ai->speed >= 0.0f) {
                p->stickY = 127;
            } else {
                p->stickY = -127;
            }
            if (ai->run) {
                p->button14 = 0xFF;
            }
        } else {
            seen = GETP(ai->nearestCar);
            if (seen == NULL || seen->car.h == 0) {
                if (ai->volumeNode == -1) {
                    /* from a tile: go back to it */
                    if (PSVECSquareDistance(&thing->base.pos, &((TileCreature *)GETP(thing->base.trigger))->pos) > 25.0f) {
                        AI_Creature_HeadFor(ai, &((TileCreature *)GETP(AI_THING(ai)->base.trigger))->pos);
                        p->button14 = 0xFF;
                        ai->base.heartbeatTimer = 0xFA;
                    }
                } else {
                    AI_Creature_VolumeState(ai);
                }
            }
        }
    }
end:
    if (ai->speed >= 0.0f) {
        p->aiSpeed = ai->speed;
    } else {
        p->aiSpeed = -ai->speed;
    }
}

void AI_Creature_Init(AICreature *ai, AIPlayerParams *params) {
    AI_Init(ai, (s32)HND(params));  /* r3/r4 passed on unchanged (asm) */
    ai->base.script.h = params->script;
    SETP(ai->base.heartbeat.data, ai);
    SETP(ai->base.heartbeat.func, AI_Creature_AsyncHeartbeat);
    ai->base.heartbeat.queued = 0;
    AI_Reset(ai);
    SETP(ai->nearestCar, NULL);
    ai->wanderTime = 0;
    ai->unkFC = 0;
    ai->leaving = 0;
    ai->base.state = AI_STATE_OFF;
    ai->base.heartbeatTimer = 0;
    SETP(ai->volume, NULL);
    ai->volumeNode = -1;
    SETP(ai->node, NULL);
    ai->speed = (f32)(rand() % 251 + 750) / 1000.0f;
    if (rand() % 4) {
        ai->run = 1;
    } else {
        ai->run = 0;
    }
    ai->base.state = AI_STATE_STATIC;
}
