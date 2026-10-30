#include "common.h"


SpawnVolume gs_aSpawnVolumes[256];
u8 *gs_pCurrentAICreature;
u32 g_volumeTotal;
u32 gs_currentVolume;

s32 AI_Creature_Deactivate(u8 *creature) {
    extern u8 g_cameraManager[];
    u8 *thing;
    u8 *spawner;
    u8 *cam;
    u32 i;
    u8 *pos;
    f32 dist;
    f32 minDist;
    Vector4 p __attribute__((aligned(16)));

    thing = SLOT_LOAD(u8 *, (creature + 0x2C));
    if (thing != NULL && !(*(u32 *)(thing + 0x74) & 0x1000)) {
        minDist = 360000.0f;
        pos = thing + 0x40;
        cam = g_cameraManager;
        for (i = 0; i < g_cameraManager[0x640]; cam += 0x190, i++) {
            PSVECScale((Vector4 *)(cam + 0x80), &p, 150.0f);
            PSVECAdd((Vector4 *)(cam + 0x60), &p, &p);
            dist = PSVECSquareDistance(&p, pos);
            if (minDist > dist) {
                minDist = dist;
            }
        }
        if (360000.0f == minDist) {
            *(u8 *)(creature + 0xCD) = 0;
            spawner = SLOT_LOAD(u8 *, (creature + 0xD8));
            if (spawner != NULL) {
                (*(s32 *)(spawner + 0x4))--;
                ((s32 *)(SLOT_LOAD(u8 *, (creature + 0xD8)) + 0xC))[*(s32 *)(creature + 0xDC)]--;
            } else {
                thing = SLOT_LOAD(u8 *, (creature + 0x2C));
                spawner = SLOT_LOAD(u8 *, (thing + 0x90));
                if (spawner != NULL && *(s32 *)(thing + 0x1B4) == 0 && *(u32 *)(creature + 0xF8) == 0) {
                    *(s32 *)(spawner + 0x20) = 1;
                    *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x90)) + 0x1C) = 0;
                }
            }
            Creature_Release(PTR(*(s32 *)(creature + 0x2C)));
            return 1;
        }
    } else {
        *(u8 *)(creature + 0xCD) = 0;
        spawner = SLOT_LOAD(u8 *, (creature + 0xD8));
        if (spawner != NULL) {
            (*(s32 *)(spawner + 0x4))--;
            ((s32 *)(SLOT_LOAD(u8 *, (creature + 0xD8)) + 0xC))[*(s32 *)(creature + 0xDC)]--;
        }
        return 1;
    }
    return 0;
}

s8 AI_Creature_Spawn(u8 *creature, u8 *spawner, u32 index) {
    typedef struct CreatureParams {
        Vector4 pos;
        f32 angle;
        u8 *info;
        u8 *player;
    } CreatureParams;
    CreatureParams params __attribute__((aligned(16)));
    u8 *info;
    u8 *list;
    u8 *thing;

    AI_Reset(creature);
    *(s32 *)(creature + 0xE4) = 0;
    *(u8 *)(creature + 0xEC) = 0;
    *(s32 *)(creature + 0xFC) = 0;
    *(s32 *)(creature + 0xF8) = 0;
    *(u8 *)(creature + 0xCD) = 0;
    *(s32 *)(creature + 0xC8) = 0;
    *(s32 *)(creature + 0xD8) = 0;
    *(s32 *)(creature + 0xDC) = -1;
    *(s32 *)(creature + 0xE0) = 0;
    *(f32 *)(creature + 0xF0) = (f32)(rand() % 251 + 750) / 1000.0f;
    if (rand() % 4) {
        *(u8 *)(creature + 0xED) = 1;
    } else {
        *(u8 *)(creature + 0xED) = 0;
    }
    *(u32 *)(creature + 0xD0) = 0;
    if (index != -1) {
        SLOT_STORE((creature + 0xD8), spawner);
        info = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, spawner) + 0x2C));
        SLOT_STORE((creature + 0xE0), SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (info + 0x10)) + (index)));
        list = SLOT_LOAD(u8 *, (info + 0x8));
        params.info = SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (list + 4)) + (rand() % *(u32 *)list));
        params.pos = *SLOT_LOAD(Vector4 *, (creature + 0xE0));
        params.angle = 0.017453292f * (f32)(rand() % 360);
        SLOT_STORE((creature + 0xA8), NULL);
    } else {
        params.info = SLOT_LOAD(u8 *, (spawner + 0x18));
        params.pos = *(Vector4 *)spawner;
        params.angle = *(f32 *)(spawner + 0x14);
        SLOT_STORE((creature + 0xD8), NULL);
        SLOT_STORE((creature + 0xA8), spawner);
        switch (GameSettings_GetDifficulty()) {
        case 0:
            *(u32 *)(creature + 0xD0) = *(u32 *)(spawner + 0x24);
            break;
        case 1:
            *(u32 *)(creature + 0xD0) = *(u32 *)(spawner + 0x28);
            break;
        case 2:
            *(u32 *)(creature + 0xD0) = *(u32 *)(spawner + 0x2C);
            break;
        case 3:
            *(u32 *)(creature + 0xD0) = *(u32 *)(spawner + 0x30);
            break;
        }
    }
    params.player = creature;
    params.pos.y += 0.1f;
    if (*(u32 *)(creature + 0xD0) == 0 && params.info != NULL) {
        switch (GameSettings_GetDifficulty()) {
        case 0:
            *(u32 *)(creature + 0xD0) = *(u32 *)(params.info + 0x44);
            break;
        case 1:
            *(u32 *)(creature + 0xD0) = *(u32 *)(params.info + 0x48);
            break;
        case 2:
            *(u32 *)(creature + 0xD0) = *(u32 *)(params.info + 0x4C);
            break;
        case 3:
            *(u32 *)(creature + 0xD0) = *(u32 *)(params.info + 0x50);
            break;
        }
    }
    thing = ThingManager_DARYL_CreateThing(7, &params);
    if (thing != NULL) {
        *(u8 *)(creature + 0xCD) = 2;
        *(s32 *)(creature + 0xC8) = 1;
        *(u32 *)(creature + 0xDC) = index;
        Player_SetThing(params.player, thing);
        *(u32 *)(creature + 0x6C) |= 0x100;
        SLOT_STORE((thing + 0x90), SLOT_LOAD(u8 *, (creature + 0xA8)));
        return 1;
    }
    return 0;
}

void AI_Creature_PopulateVolumes(void) {
    extern u8 g_cameraManager[];
    u32 v;
    u32 idx;
    u8 *info;
    u8 *spawner;
    u8 *creature;
    u32 i;
    f32 dist;
    f32 minDist;
    Vector4 p __attribute__((aligned(16)));

    creature = gs_pCurrentAICreature;
    do {
        if (creature[0xCD] == 0) {
            break;
        }
        creature = SLOT_LOAD(u8 *, (creature + 0xD4));
    } while (creature != gs_pCurrentAICreature);
    if (creature[0xCD] != 0) {
        return;
    }
    v = gs_currentVolume;
    do {
        if (++v == g_volumeTotal) {
            v = 0;
        }
        info = SLOT_LOAD(u8 *, (gs_aSpawnVolumes[v].volume + 0x2C));
        if (*(u32 *)(gs_aSpawnVolumes[v].volume + 4) == 0 || *(u32 *)(info + 4) == gs_aSpawnVolumes[v].unk4 || *(u32 *)(info + 0xC) == 0 || info[0] != 0) {
            continue;
        }
        idx = gs_aSpawnVolumes[v].index;
        do {
            if (++idx == *(u32 *)(info + 0xC)) {
                idx = 0;
            }
            spawner = SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (info + 0x10)) + (idx));
            if (*(u32 *)(spawner + 0x14) == 0 || (f32)gs_aSpawnVolumes[v].counts[idx] >= 1.0f + (f32)(*(u32 *)(info + 4) / *(u32 *)(info + 0xC))) {
                continue;
            }
            minDist = 360000.0f;
            for (i = 0; i < g_cameraManager[0x640]; i++) {
                PSVECScale((Vector4 *)(g_cameraManager + i * 0x190 + 0x80), &p, 150.0f);
                PSVECAdd((Vector4 *)(g_cameraManager + i * 0x190 + 0x60), &p, &p);
                dist = PSVECSquareDistance(&p, spawner);
                if (minDist > dist) {
                    minDist = dist;
                }
            }
            if (minDist > 160000.0f && minDist < 360000.0f) {
                gs_currentVolume = v;
                gs_aSpawnVolumes[v].index = idx;
                if (AI_Creature_Spawn(creature, (u8 *)&gs_aSpawnVolumes[v], idx)) {
                    (*(s32 *)(SLOT_LOAD(u8 *, (creature + 0xD8)) + 4))++;
                    ((s32 *)(SLOT_LOAD(u8 *, (creature + 0xD8)) + 0xC))[idx]++;
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
        gs_aSpawnVolumes[i].volume = VolumeManager_GetVolumeTypeByIndex(5, i);
    }
}

static inline void AI_Creature_UpdateNearestCars(void) {
    extern NearestCar gs_aNearestCars[8];
    static u32 s_currentCar;
    u8 *cars[8];
    u32 count;
    u32 i;

    count = ThingManager_GetActiveCars(cars, 9);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            gs_aNearestCars[i].car = cars[i];
            PSVECScale(cars[i], &gs_aNearestCars[i].pos, 0.18f);
            PSVECAdd(cars[i] + 0x40, &gs_aNearestCars[i].pos, &gs_aNearestCars[i].pos);
        }
        if (i < 8) {
            gs_aNearestCars[i].car = NULL;
        }
        if (s_currentCar >= count) {
            s_currentCar = 0;
        }
        gs_aNearestCars[s_currentCar].speed = PSVECSquareMag(gs_aNearestCars[s_currentCar].car);
        if (gs_aNearestCars[s_currentCar].speed > 1.0f) {
            gs_aNearestCars[s_currentCar].range = 400.0f * (1.0f - gs_aNearestCars[s_currentCar].speed / 100000.0f);
        } else {
            gs_aNearestCars[s_currentCar].range = 400.0f;
        }
        s_currentCar++;
    }
}

static inline void AI_Creature_FindNearestCar(u8 *creature) {
    extern NearestCar gs_aNearestCars[8];
    f32 best;
    f32 dist;
    u32 i;

    best = *(f32 *)(SLOT_LOAD(u8 *, (creature + 0xD0)) + 0x18);
    for (i = 0; i < 8; i++) {
        if (gs_aNearestCars[i].car == NULL) {
            break;
        }
        dist = PSVECSquareDistance(SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x40, &gs_aNearestCars[i].pos);
        if (best > dist) {
            if (SLOT_LOAD(NearestCar *, (creature + 0xE4)) != &gs_aNearestCars[i]) {
                SLOT_STORE((creature + 0xE4), &gs_aNearestCars[i]);
                *(f32 *)(creature + 0xF0) = (f32)(rand() % 501 + 750) / 1000.0f;
                creature[0xEC] = 0;
                *(s32 *)(creature + 0xC8) = 1;
            }
            *(f32 *)(creature + 0xE8) = best;
            best = dist;
        }
    }
}

void AI_Creature_AsyncProcess(void) {
    extern NearestCar gs_aNearestCars[8];
    extern u8 g_cameraManager[];
    static u32 gs_creatureTile;
    u8 *creature;
    u8 *tile;
    u32 j;
    u32 i;
    u32 n;
    u8 *cam;
    u8 *player;
    AIPlayerDesc desc;
    Vec p;

    if (g_volumeTotal == -1) {
        gs_pCurrentAICreature = NULL;
        g_volumeTotal = VolumeManager_GetNumVolumesOfType(5);
        if (g_volumeTotal != 0) {
            AI_Creature_InitSpawnVolumes();
        } else if (World_GetTotalTiles_Creature() == 0) {
            goto update;
        }
        MemClr32(gs_aNearestCars, sizeof(gs_aNearestCars));
        for (i = 0; i < 1; i++) {
            desc.unk0 = 0;
            desc.unkD = 1;
            player = PlayerManager_NewAIPlayer((u8 *)&desc);
            if (gs_pCurrentAICreature == NULL) {
                gs_pCurrentAICreature = player;
                SLOT_STORE((player + 0xD4), player);
            } else {
                SLOT_STORE((player + 0xD4), SLOT_LOAD(u8 *, (gs_pCurrentAICreature + 0xD4)));
                SLOT_STORE((gs_pCurrentAICreature + 0xD4), player);
            }
            player[0xCD] = 0;
        }
    }
update:
    if (gs_pCurrentAICreature == NULL) {
        return;
    }
    AI_Creature_UpdateNearestCars();
    for (n = 0; n < 10; n++) {
        if (gs_pCurrentAICreature[0xCD] != 0 && *(u32 *)(gs_pCurrentAICreature + 0xF8) == 0 &&
            !(s8)AI_Creature_Deactivate(gs_pCurrentAICreature)) {
            AI_Creature_FindNearestCar(gs_pCurrentAICreature);
        }
        gs_pCurrentAICreature = SLOT_LOAD(u8 *, (gs_pCurrentAICreature + 0xD4));
    }
    if (World_GetTotalTiles_Creature() != 0) {
        creature = gs_pCurrentAICreature;
        do {
            if (creature[0xCD] == 0) {
                break;
            }
            creature = SLOT_LOAD(u8 *, (creature + 0xD4));
        } while (creature != gs_pCurrentAICreature);
        if (creature[0xCD] == 0) {
            gs_creatureTile++;
            if (gs_creatureTile >= World_GetTotalTiles_Creature()) {
                gs_creatureTile = 0;
            }
            tile = World_GetTileCreature(gs_creatureTile);
            if (*(u32 *)(tile + 0x20) != 0 && SLOT_LOAD(u8 *, (tile + 0x1C)) == NULL) {
                cam = g_cameraManager;
                for (j = 0; j < g_cameraManager[0x640]; cam += 0x190, j++) {
                    PSVECScale(cam + 0x80, &p, 150.0f);
                    PSVECAdd(cam + 0x60, &p, &p);
                    if (360000.0f > PSVECSquareDistance(&p, tile)) {
                        if (AI_Creature_Spawn(creature, tile, -1)) {
                            SLOT_STORE((SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x90), tile);
                            SLOT_STORE((tile + 0x1C), SLOT_LOAD(u8 *, (creature + 0x2C)));
                            *(u32 *)(tile + 0x20) = 0;
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

void AI_Creature_VolumeState(u8 *creature) {
    u8 *thing;
    u8 *target;
    f32 dist;
    f32 angle;
    f32 diff;
    u8 r;

    thing = SLOT_LOAD(u8 *, (creature + 0x2C));
    if (IsPointWithinVolume((Vector4 *)(thing + 0x40), SLOT_LOAD(u8 *, SLOT_LOAD(void *, (creature + 0xD8))), &dist) || *(u32 *)(SLOT_LOAD(u8 *, (creature + 0xE0)) + 0x14) == 0) {
        if (*(u8 *)(creature + 0xEC) == 0) {
            r = rand() % 50;
            if (r == 0) {
                *(f32 *)(creature + 0xF0) = (f32)(rand() % 501 + 500) / -1000.0f;
                *(f32 *)(creature + 0xF4) = 0.017453292f * (f32)(rand() % 360 - 180);
                *(u8 *)(creature + 0xED) = 0;
            } else if (r < 3) {
                *(f32 *)(creature + 0xF0) = (f32)(rand() % 251 + 750) / 1000.0f;
                *(u8 *)(creature + 0xED) = 0;
            } else {
                *(f32 *)(creature + 0xF0) = (f32)(rand() % 251 + 750) / 1000.0f;
                *(f32 *)(creature + 0xF4) = 0.017453292f * (f32)(rand() % 360 - 180);
                if (rand() % 4) {
                    *(u8 *)(creature + 0xED) = 1;
                } else {
                    *(u8 *)(creature + 0xED) = 0;
                }
            }
            *(u8 *)(creature + 0xEC) = rand() % 128 + 127;
        }
        *(f32 *)(creature + 0x4) = *(f32 *)(creature + 0xF4);
        *(s8 *)(creature + 0x1) = 127.0f * *(f32 *)(creature + 0xF0);
    } else {
        if (*(f32 *)(creature + 0xF0) <= 0.75f) {
            *(f32 *)(creature + 0xF0) = (f32)(rand() % 251 + 750) / 1000.0f;
        }
        *(s32 *)(creature + 0xC8) = 0xFA;
        target = SLOT_LOAD(u8 *, (creature + 0xE0));
        thing = SLOT_LOAD(u8 *, (creature + 0x2C));
        angle = ArcTan(*(f32 *)(target + 0x0) - *(f32 *)(thing + 0x40), *(f32 *)(target + 0x8) - *(f32 *)(thing + 0x48));
        *(f32 *)(creature + 0x4) = angle;
        thing = SLOT_LOAD(u8 *, (creature + 0x2C));
        diff = ArcTan(*(f32 *)(thing + 0x30), *(f32 *)(thing + 0x38)) - angle;
        while (diff > 3.1415927f) {
            diff -= 6.2831855f;
        }
        while (diff < -3.1415927f) {
            diff += 6.2831855f;
        }
        if (*(f32 *)(creature + 0xF0) >= 0.0f) {
            *(s8 *)(creature + 0x1) = 127;
        } else {
            *(s8 *)(creature + 0x1) = -127;
        }
        *(u8 *)(creature + 0xEC) = rand() % 128 + 127;
        *(f32 *)(creature + 0xF4) = *(f32 *)(creature + 0x4);
    }
    if (*(s8 *)(creature + 0xED)) {
        *(u8 *)(creature + 0x14) = 0xFF;
    }
}

void AI_Creature_AsyncHeartbeat(u8 *creature) {
    u8 *list;
    u8 *thing;
    u8 *node;
    u8 *next;
    u8 *spawner;
    f32 angle;
    f32 diff;

    if (*(u8 *)(creature + 0xCD) == 0 || *(s8 *)(creature + 0xCC) || SLOT_LOAD(u8 *, (creature + 0x2C)) == NULL) {
        *(s32 *)(creature + 0xC8) = 0;
        return;
    }
    AI_AsyncHeartbeat(creature);
    Control_ClearControl(creature);
    node = SLOT_LOAD(u8 *, (creature + 0xE0));
    if (node != NULL && (*(u32 *)(node + 0x14) == 0 || PSVECSquareDistance(node, SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x40) < 25.0f)) {
        node = SLOT_LOAD(u8 *, (creature + 0xE0));
        if (*(u32 *)(node + 0x18) != 0) {
            next = SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (node + 0x1C)) + (rand() % *(u32 *)(node + 0x18)));
            if (*(u32 *)(next + 0x14) != 0) {
                SLOT_STORE((creature + 0xE0), next);
            }
        }
    }
    if (*(u32 *)(creature + 0xF8) != 0) {
        if (*(s32 *)(creature + 0xFC) != 0 || 1.0f != *(f32 *)(SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x1C0)) {
            *(s32 *)(creature + 0xC8) = 1000;
        } else {
            spawner = SLOT_LOAD(u8 *, (creature + 0xD8));
            if (SLOT_LOAD(u8 *, (creature + 0xE0)) == NULL) {
                if (*(u32 *)(spawner + 0xC) == 0) {
                    goto end;
                }
                SLOT_STORE((creature + 0xE0), SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (spawner + 0x10)) + (rand() % *(u32 *)(spawner + 0xC))));
            }
            if (PSVECSquareDistance(SLOT_LOAD(u8 *, (creature + 0xE0)), SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x40) < 25.0f) {
                Creature_Release(PTR(*(s32 *)(creature + 0x2C)));
                *(u8 *)(creature + 0xCD) = 0;
            } else {
                *(s32 *)(creature + 0xC8) = 0xFA;
                node = SLOT_LOAD(u8 *, (creature + 0xE0));
                angle = ArcTan(*(f32 *)(node + 0x0) - *(f32 *)(SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x40),
                               *(f32 *)(node + 0x8) - *(f32 *)(SLOT_LOAD(u8 *, (creature + 0x2C)) + 0x48));
                *(f32 *)(creature + 0x4) = angle;
                thing = SLOT_LOAD(u8 *, (creature + 0x2C));
                diff = ArcTan(*(f32 *)(thing + 0x30), *(f32 *)(thing + 0x38)) - angle;
                while (diff > 3.1415927f) {
                    diff -= 6.2831855f;
                }
                while (diff < -3.1415927f) {
                    diff += 6.2831855f;
                }
                if (*(f32 *)(creature + 0xF0) >= 0.0f) {
                    *(s8 *)(creature + 0x1) = 127;
                } else {
                    *(s8 *)(creature + 0x1) = -127;
                }
                *(u8 *)(creature + 0x14) = 0xFF;
            }
        }
    } else {
        thing = SLOT_LOAD(u8 *, (creature + 0x2C));
        if (*(u32 *)(thing + 0x1B8) & 1) {
            if (*(u8 *)(creature + 0xEC) == 0) {
                *(f32 *)(creature + 0xF0) = (f32)(rand() % 201 + 800) / 1000.0f;
                *(f32 *)(creature + 0xF4) = 0.017453292f * (f32)(rand() % 360 - 180);
                *(u8 *)(creature + 0xED) = 1;
                *(u8 *)(creature + 0xEC) = rand() % 64 + 64;
            }
            *(f32 *)(creature + 0x4) = *(f32 *)(creature + 0xF4);
            if (*(f32 *)(creature + 0xF0) >= 0.0f) {
                *(s8 *)(creature + 0x1) = 127;
            } else {
                *(s8 *)(creature + 0x1) = -127;
            }
            if (*(s8 *)(creature + 0xED)) {
                *(u8 *)(creature + 0x14) = 0xFF;
            }
        } else {
            list = SLOT_LOAD(u8 *, (creature + 0xE4));
            if (list == NULL || *(u32 *)list == 0) {
                if (*(u32 *)(creature + 0xDC) == -1) {
                    if (PSVECSquareDistance(thing + 0x40, SLOT_LOAD(u8 *, (thing + 0x90))) > 25.0f) {
                        thing = SLOT_LOAD(u8 *, (creature + 0x2C));
                        node = SLOT_LOAD(u8 *, (thing + 0x90));
                        angle = ArcTan(*(f32 *)(node + 0x0) - *(f32 *)(thing + 0x40), *(f32 *)(node + 0x8) - *(f32 *)(thing + 0x48));
                        *(f32 *)(creature + 0x4) = angle;
                        thing = SLOT_LOAD(u8 *, (creature + 0x2C));
                        diff = ArcTan(*(f32 *)(thing + 0x30), *(f32 *)(thing + 0x38)) - angle;
                        while (diff > 3.1415927f) {
                            diff -= 6.2831855f;
                        }
                        while (diff < -3.1415927f) {
                            diff += 6.2831855f;
                        }
                        if (*(f32 *)(creature + 0xF0) >= 0.0f) {
                            *(s8 *)(creature + 0x1) = 127;
                        } else {
                            *(s8 *)(creature + 0x1) = -127;
                        }
                        *(u8 *)(creature + 0x14) = 0xFF;
                        *(s32 *)(creature + 0xC8) = 0xFA;
                    }
                } else {
                    AI_Creature_VolumeState(creature);
                }
            }
        }
    }
end:
    if (*(f32 *)(creature + 0xF0) >= 0.0f) {
        *(f32 *)(creature + 0x8) = *(f32 *)(creature + 0xF0);
    } else {
        *(f32 *)(creature + 0x8) = -*(f32 *)(creature + 0xF0);
    }
}

void AI_Creature_Init(u8 *creature, u8 *params) {

    AI_Init();
    *(s32 *)(creature + 0xD0) = *(s32 *)(params + 0x0);
    SLOT_STORE((creature + 0xB8), creature);
    SLOT_STORE((creature + 0xBC), AI_Creature_AsyncHeartbeat);
    *(u8 *)(creature + 0xC4) = 0;
    AI_Reset(creature);
    *(s32 *)(creature + 0xE4) = 0;
    *(u8 *)(creature + 0xEC) = 0;
    *(s32 *)(creature + 0xFC) = 0;
    *(s32 *)(creature + 0xF8) = 0;
    *(u8 *)(creature + 0xCD) = 0;
    *(s32 *)(creature + 0xC8) = 0;
    *(s32 *)(creature + 0xD8) = 0;
    *(s32 *)(creature + 0xDC) = -1;
    *(s32 *)(creature + 0xE0) = 0;
    *(f32 *)(creature + 0xF0) = (f32)(rand() % 251 + 750) / 1000.0f;
    if (rand() % 4) {
        *(u8 *)(creature + 0xED) = 1;
    } else {
        *(u8 *)(creature + 0xED) = 0;
    }
    *(u8 *)(creature + 0xCD) = 3;
}

