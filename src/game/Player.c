/* Players: the human and AI drivers of things (cars, creatures, lookers).
 * Each reads its input into the Player fields every frame (controlReader),
 * owns a HUD interface and, as an AI car driver, is respawned after its car
 * is destroyed. */
#include "common.h"
#include "CarA.h"
#include "World.h"
#include "Nav.h"


/* Human players read the pad (Player_DefaultControlReader). */
s8 Player_IsHuman(void *player) {
    s32 isHuman = 0;

    if (player != NULL && (void *)GETP(((Player *)player)->controlReader) == (void *)Player_DefaultControlReader) {
        isHuman = 1;
    }
    return isHuman;
}

void PlayerHuman_Init(Player *player) {
    Player_Init(player, 0);
    player->device = 0;
    PlayerHuman_SetDefaultControls(player, 0);
    player->flags = 0;
    SETP(player->stuntScoreText, player->stuntScoreBuf);
}

void Player_Init(Player *player, s32 arg1) {
    s32 i;

    Control_ClearControl(player);
    player->name[0] = 0;
    player->cameraIndex = 0;
    player->aiScripts.h = 0;
    player->interfaceIndex = -1;
    player->flags = 0;
    memset(&player->pad70, 0, 0x38);   /* the stats, 0x70-0xA7 */
    player->scoreDirty = 1;
    player->deadTime = 0;
    for (i = 0; i < 8; i++) {
        player->counters[i] = 30;
    }
    player->counters[7] = 0;
    player->flags = 0;
    player->deathNavPoint = -1;
    player->respawnNavPoint = -1;
}

void Player_SetName(Player *player, s32 port, const char *name) {
    strcpy(player->name, name);
    player->port = port;
    if (Player_IsHuman(player)) {
        player->device = Input_GetPlayerDevice(player->port);
    }
}

/* Recreate the player's HUD interface from its thing's interface info. */
void Player_RefreshInterface(void *arg0) {
    struct {
        u32 unk0;
        PTR32(u8) script;
        u32 x;
        u32 y;
        u8 unk10[0xC0];
    } info;
    Player *player = arg0;
    Thing *thing;

    if (GETP(player->vehicle) != NULL && (player->flags & 0x10)) {
        memset(&info, 0, sizeof(info));
        player->flags |= 0x200;
        thing = GETP(player->vehicle);
        THING_VFN(thing, THING_V_GET_INTERFACE_INFO, void (*)(Thing *, void *))(thing, &info);
        if (player->interfaceIndex < 0xFFFFFFFF) {
            InterfaceManager_ReleaseInterface(player->interfaceIndex);
            player->interfaceIndex = 0xFFFFFFFF;
        }
        if (GETP(info.script) != NULL) {
            player->interfaceIndex = InterfaceManager_Create(GETP(info.script), player, 0);
            InterfaceManager_SetOffset(player->interfaceIndex, info.x, info.y);
            player->scoreDirty = 1;
        }
        player->flags &= ~0x200;
    }
}

void Player_ReleaseInterface(Player *player) {
    u32 index = player->interfaceIndex;

    if (index != 0xFFFFFFFF) {
        InterfaceManager_ReleaseInterface(index);
    }
}

extern u8 g_carAVTable[];
extern u8 g_creatureVTable[];
/* Attach the player to a thing (or NULL): the thing's player, the pool type
 * and its info (AI scripts) are taken over. */
void Player_SetThing(Player *player, Thing *thing) {
    s32 isCreature;

    SETP(player->vehicle, thing);
    if (thing != NULL) {
        SETP(((Thing *)GETP(player->vehicle))->player, player);
        player->type = thing->type;
        if ((u8 *)GETP(thing->vtable) == g_carAVTable) {
            player->aiScripts.h = ((CarA *)thing)->vehicleIface.h;
            CarA_SetSlavePlayers(thing);
            if (!Player_IsHuman(player)) {
                AI_Car_Reset(player);
            }
        } else if ((u8 *)GETP(thing->vtable) == g_creatureVTable) {
            player->aiScripts.h = ((Creature *)thing)->info.h;
            Thing_SetSlavePlayers(thing);
        }
        Player_RefreshInterface(player);
    }
}

s32 Player_InputHeartbeat(Player *player) {
    return ((s8 (*)(void *))GETP(player->controlReader))(player) != 0;
}

void Player_GetInterfaceInfo(void *arg0, void *arg1) {
    typedef struct InterfaceInfo {
        u8 pad0[0x74];
        char text[0xC];
        u8 state;
    } InterfaceInfo;
    Player *player = arg0;
    InterfaceInfo *info = arg1;
    Thing *thing;

    if (GETP(player->vehicle) != NULL) {
        info->state = 0;
        thing = GETP(player->vehicle);
        THING_VFN(thing, THING_V_GET_INTERFACE_INFO, void (*)(Thing *, InterfaceInfo *))(thing, info);
    } else {
        /* no thing: 1 the frame it was lost, then 2 */
        info->state = 2;
        if (player->thingLost != 0) {
            info->state = 1;
            player->thingLost = 0;
        }
    }
    if (player->scoreDirty != 0) {
        sprintf(info->text, "%d", player->stuntPoints);
        player->scoreDirty = 0;
    }
}

/* The player's thing died: detach it and remember for the HUD. */
void Player_DetachThingDeath(Player *player) {
    Thing *thing;

    thing = GETP(player->vehicle);
    SETP(player->vehicle, NULL);
    player->thingLost = 1;
    if (thing != NULL) {
        SETP(thing->player, NULL);
    }
}

/* Human players: feed the force-feedback forces at the car, then as any player. */
void PlayerHuman_Heartbeat(Player *player) {
    s32 c;
    u8 a;
    u8 b;
    Thing *car;

    car = GETP(player->vehicle);
    if (car != NULL && !(car->flags & THING_FLAG_DEAD)) {
        ForceFeedback_GetForce(&car->pos, &a, &b, &c);
        Input_SetAlternativeForceFeedback(player->device, a, b);
    }
    Player_Heartbeat(player);
}


/* Every player: cycle the camera on button 12; keep the car's distance along
 * the path to the target arrow (the HUD arrow for humans; route-only races
 * aim at the first way point); and respawn an AI driver's car 2 s after it
 * was destroyed: at its death nav point (race modes >= 3), at the tile
 * farthest from every car (mode 2) or at its own tile. */
void Player_Heartbeat(void *arg0) {
    extern s32 g_iDeltaTimeMS;
    extern s32 g_TargetArrow[7];
    extern s8 g_bWaitingForRoundReset;
    extern Matrix4 g_identity;
    Player *player = arg0;
    NavPoint *b;
    TileCar *tile;
    CarA *thing;
    NavPoint *path;
    Vector4 *pos;
    NavPoint *a;
    s32 i;
    f32 left;
    f32 dist;
    Thing *cars[8];
    CarAParams params __attribute__((aligned(16)));
    Vector4 mtx[4] __attribute__((aligned(16)));

    thing = GETP(player->vehicle);
    if (player->buttonsHeld & 0x1000) {
        player->cameraIndex++;
    }
    if (GETP(player->vehicle) == NULL) {
        player->deadTime += g_iDeltaTimeMS;
    } else if (player->type == PLAYER_TYPE_CAR && (void *)PTR(g_TargetArrow[0]) != NULL) {
        thing->navTimer -= g_iDeltaTimeMS;
        if (thing->navTimer <= 0 && NavHolder_GetTotalNavPoints() != 0) {
            s32 dest;
            s32 cur;

            dest = InterfaceManager_GetTargetArrowNearestNavPoint();
            cur = THING_VFN(thing, THING_V_GET_NEAREST_NAV_POINT, s32 (*)(void *))(thing);
            if (dest != -1 && cur != -1) {
                NavHolder_UseMainRouteOnly();
                path = NavHolder_FindPath(cur, dest);
                NavHolder_FillPointerArrayPath(path, thing->path, 7);
                if (path != NULL) {
                    thing->pathLength = path->g;   /* the length of the path found */
                    thing->navTimer = RandomAdd_s32(900, 100);
                }
            }
        }
        left = thing->pathLength;
        pos = &thing->base.pos;
        for (i = 0; i < 6; i++) {
            a = GETP(thing->path[i]);
            b = GETP(thing->path[i + 1]);
            if (a == NULL || b == NULL) {
                break;
            }
            if (LinearPointTest(pos, a, b) < 0.0f) {
                break;
            }
            left -= Sqrt(PSVECSquareDistance(a, b));
        }
        if (a != NULL && b != NULL) {
            DistanceXYZ(pos, a, &dist);
            thing->navDist = dist + left;
            if (Player_IsHuman(player)) {
                while (dist < 300.0f) {
                    a = GETP(thing->path[i]);
                    /* port: when the loop above passed all six segments of a
                     * full path, i is 6 and the original reads path[7], past
                     * the array (a garbage pointer); end the path there */
                    b = i + 1 < 7 ? GETP(thing->path[i + 1]) : NULL;
                    if (a == NULL || b == NULL) {
                        if ((NavPoint *)PTR(g_TargetArrow[1]) != NULL) {
                            g_TargetArrow[2] = g_TargetArrow[1];
                            g_TargetArrow[6] = 0;
                            g_TargetArrow[1] = 0;
                        }
                        break;
                    }
                    dist += Sqrt(PSVECSquareDistance(a, b));
                    if (++i >= 6) {
                        break;
                    }
                }
                if ((NavPoint *)PTR(g_TargetArrow[1]) != b) {
                    g_TargetArrow[2] = g_TargetArrow[1];
                    g_TargetArrow[6] = 0;
                    g_TargetArrow[1] = (s32)HND(b);
                }
            }
        } else {
            DistanceXYZ(&thing->base.pos, (void *)PTR(g_TargetArrow[0]), &thing->navDist);
            if (Player_IsHuman(player)) {
                if ((NavPoint *)PTR(g_TargetArrow[1]) != NULL) {
                    g_TargetArrow[2] = g_TargetArrow[1];
                    g_TargetArrow[6] = 0;
                    g_TargetArrow[1] = 0;
                }
            }
        }
    } else if (GameSettings_GetCodeVariable(0x12D, 0) >= 3) {
        thing->navTimer -= g_iDeltaTimeMS;
        if (thing->navTimer <= 0 && NavHolder_GetTotalNavPoints() != 0) {
            s32 dest;
            s32 cur;

            cur = THING_VFN(thing, THING_V_GET_NEAREST_NAV_POINT, s32 (*)(void *))(thing);
            dest = NavHolder_GetFirstWayPoint();
            while (((NavPoint *)NavHolder_GetNavPoint(dest))->type == 1) {
                dest = ((NavPoint *)NavHolder_GetNavPoint(dest))->nextWayPoint;
            }
            if (dest != -1 && cur != -1) {
                NavHolder_UseMainRouteOnly();
                path = NavHolder_FindPath(cur, dest);
                NavHolder_FillPointerArrayPath(path, thing->path, 7);
                if (path != NULL) {
                    thing->pathLength = path->g;   /* the length of the path found */
                    thing->navTimer = RandomAdd_s32(900, 100);
                }
            }
        }
    }
    if (GETP(player->vehicle) == NULL && player->deadTime > 2000 && !(player->flags & 1) && !g_bWaitingForRoundReset &&
        player->buttonFire && GETP(player->aiScripts) != NULL && player->type == PLAYER_TYPE_CAR) {
        tile = GETP(player->tileCar);
        if (tile != NULL && !tile->respawn) {
            PlayerManager_RemovePlayer(player);
        } else if (tile != NULL) {
            SETP(params.player, player);
            SETP(params.info, GETP(tile->info));
            params.unk34 = 0.0f;
            if (GameSettings_GetCodeVariable(0x12D, 0) >= 3 && player->deathNavPoint != -1) {
                u8 attempt = 0;

                params.pos = ((NavPoint *)NavHolder_GetNavPoint(player->deathNavPoint))->pos;
                memcpy(mtx, &g_identity, sizeof(mtx));
                if (player->respawnNavPoint != -1) {
                    a = NavHolder_GetNavPoint(player->deathNavPoint);
                    PSVECSubtract(NavHolder_GetNavPoint(player->respawnNavPoint), a, &mtx[2]);
                    if (PSVECSquareMag(&mtx[2])) {
                        PSVECNormalize(&mtx[2], &mtx[2]);
                    }
                    mtx[1].x = 0.0f;
                    mtx[1].y = 1.0f;
                    mtx[1].z = 0.0f;
                    mtx[1].w = 0.0f;
                    PSVECCrossProduct(&mtx[1], &mtx[2], &mtx[0]);
                    PSVECCrossProduct(&mtx[2], &mtx[0], &mtx[1]);
                    if (PSVECSquareMag(&mtx[0])) {
                        PSVECNormalize(&mtx[0], &mtx[0]);
                    }
                    if (PSVECSquareMag(&mtx[1])) {
                        PSVECNormalize(&mtx[1], &mtx[1]);
                    }
                }
                player->respawnNavPoint = -1;
                player->deathNavPoint = -1;
                params.pos.y += 10.0f;
                SubMatrixToQuaternion(mtx, &params.quat);
                thing = ThingManager_DARYL_CreateThing(THING_TYPE_CAR, &params);
                do {
                    thing->base.pos = params.pos;
                    thing->base.flags |= THING_FLAG_MOVED;
                    if (OBoxHitsTerrainAndObjectsZeroTime(THING_VFN(thing, THING_V_GET_ORIENTED_COLLISION, void *(*)(void *))(thing), thing, 0)) {
                        switch (attempt++) {
                        case 0:
                            params.pos.z -= 10.0f;
                            break;
                        case 1:
                            params.pos.x += 10.0f;
                            break;
                        case 2:
                            params.pos.z += 10.0f;
                            break;
                        case 3:
                            params.pos.z += 10.0f;
                            break;
                        case 4:
                            params.pos.x -= 10.0f;
                            break;
                        case 5:
                            params.pos.x -= 10.0f;
                            break;
                        case 6:
                            params.pos.z -= 10.0f;
                            break;
                        case 7:
                            params.pos.z -= 10.0f;
                            break;
                        default:
                            attempt = 0;
                            params.pos.x += 10.0f;
                            params.pos.z += 10.0f;
                            thing->base.pos = params.pos;
                            thing->base.flags |= THING_FLAG_MOVED;
                            break;
                        }
                    } else {
                        attempt = 0;
                    }
                } while (attempt != 0);
            } else if (GameSettings_GetCodeVariable(0x12D, 0) == 2) {
                TileCar *car;
                TileCar *bestTile = NULL;
                u32 count;
                u8 j;
                u8 k;
                f32 nearest;
                f32 best;

                count = ThingManager_GetActiveCars(cars, 8);
                best = 0.0f;
                for (j = 0; j < World_GetTotalTiles_Car(); j++) {
                    car = World_GetTileCar(j);
                    if (car->mode == 1) {
                        nearest = 1e10f;
                        for (k = 0; k < count; k++) {
                            f32 d = PSVECSquareDistance(&cars[k]->pos, &car->pos);
                            if (nearest > d) {
                                nearest = d;
                            }
                        }
                        if (best < nearest) {
                            best = nearest;
                            bestTile = car;
                        }
                    }
                }
                params.pos = bestTile->pos;
                params.pos.y += 10.0f;
                SetOrientation(mtx, bestTile->rotY, bestTile->rotX, bestTile->rotZ);
                SubMatrixToQuaternion(mtx, &params.quat);
                thing = ThingManager_DARYL_CreateThing(THING_TYPE_CAR, &params);
            } else {
                params.pos = tile->pos;
                params.pos.y += 10.0f;
                SetOrientation(mtx, tile->rotY, tile->rotX, tile->rotZ);
                SubMatrixToQuaternion(mtx, &params.quat);
                thing = ThingManager_DARYL_CreateThing(THING_TYPE_CAR, &params);
            }
            player->deadTime = 0;
            SETP(tile->car, thing);
            Player_SetThing(player, thing);
            SETP(thing->base.trigger, tile);
            if (GETP(GETP(player->tileCar)->item) != NULL) {
                void *rack = ThingManagerList_GetByType(&GETP(GETP(player->tileCar)->car)->motionState.slaves, THING_TYPE_WILDCARD_RACK);

                WildCardRack_JettisonAll(rack);
                WildCardRack_Add(rack, GETP(GETP(player->tileCar)->item), GETP(GETP(GETP(player->tileCar)->item)->weapon)->ammo);
            }
        }
    }
}
/* A key layout: stick modes and the key for each action. g_keyBinding[0]
 * maps actions to Player.keys slots, [1 + i] are the built-in layouts. */
typedef struct KeyBinding {
    u8 stickMode;
    u8 substickMode;
    u8 keys[14];
} KeyBinding;

extern KeyBinding g_keyBinding[];

void PlayerHuman_SetDefaultControls(Player *player, s32 index) {
    u16 i;

    memset(player->keys, 0, 0x10);   /* keys and the two stick modes */
    for (i = 0; i < 14; i++) {
        player->keys[i] = 0xFF;
    }
    index++;
    for (i = 0; i < 14; i++) {
        player->keys[g_keyBinding[0].keys[i]] = g_keyBinding[index].keys[i];
    }
    player->stickMode = g_keyBinding[index].stickMode;
    player->substickMode = g_keyBinding[index].substickMode;
}

/* Use a controller layout from the MCP (ControlBinding.keys). */
void PlayerHuman_SetControlBinding(Player *player, KeyBinding *binding) {
    u16 i;

    memset(player->keys, 0, 0x10);
    for (i = 0; i < 14; i++) {
        player->keys[i] = 0xFF;
    }
    for (i = 0; i < 14; i++) {
        player->keys[g_keyBinding[0].keys[i]] = binding->keys[i];
    }
    player->stickMode = binding->stickMode;
    player->substickMode = binding->substickMode;
}



static inline u8 PlayerHuman_KeyDown(Player *player, s32 key) {
    return Input_KeyDown(player->device, player->keys[key]);
}

static inline s8 PlayerHuman_KeyPress(Player *player, s32 key) {
    return Input_KeyPress(player->device, player->keys[key]);
}

s32 Player_NULL(void) {
    return 0;
}

/* Debug/cheat combos: button 1 held while pressing a sequence. The first
 * one (12 presses) unlocks the rest (flags & 0x20). Matching starts at the
 * second recorded input. */
void PlayerHuman_EvaluateCombo(Player *player) {
    extern s8 g_bFreezeWorld;
    u32 *combo = &player->comboInputs[1];

    if (!(player->flags & 0x20)) {
        if (player->comboCount == 13 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x800) &&
            (combo[3] & 4) && (combo[4] & 4) && (combo[5] & 8) && (combo[6] & 8) && (combo[7] & 0x20) &&
            (combo[8] & 0x20) && (combo[9] & 0x800) && (combo[10] & 0x800) && (combo[11] & 0x800)) {
            player->flags |= 0x20;
        }
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 4) && (combo[2] & 4)) {
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 4) && (combo[2] & 8)) {
        player->combo = 7;
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 4) && (combo[2] & 0x10)) {
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x10)) {
        player->combo = 3;
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x20)) {
        TimeHandler_SetSlowMotion();
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 0x10) && (combo[2] & 0x10)) {
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 0x10) && (combo[2] & 0x20)) {
        CameraManager_ReturnAllToPlayer();
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 8) && (combo[2] & 8)) {
    } else if (player->comboCount == 6 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x800) &&
               (combo[3] & 0x20) && (combo[4] & 0x20)) {
    } else if (player->comboCount == 4 && (combo[0] & 0x800) && (combo[1] & 0x20) && (combo[2] & 0x20)) {
        g_bFreezeWorld = !g_bFreezeWorld;
    } else if (player->comboCount == 4 && (combo[0] & 4) && (combo[1] & 8) && (combo[2] & 8)) {
        player->combo = 8;
    }
}

/* Human players: read the pad through the key layout into the input fields
 * (or play back / record them), and feed the combo buffer. */
s32 Player_DefaultControlReader(void *arg0) {
    extern u32 g_playbackStatus;
    Player *player = arg0;
    s16 stickX;
    s16 substickX;
    s16 stickY;
    s16 substickY;
    u32 i;

    Control_ClearControl(player);
    if (player->flags & 1) {
        player->down[3] = 0xFF;
        player->down[2] = 0xFF;
        /* stopped: an upside-down car steers left */
        if (GETP(player->vehicle) != NULL && ((Thing *)GETP(player->vehicle))->axis[1].y < 0.0f) {
            player->stickX = -128;
        }
        return 1;
    }
    if (g_playbackStatus == 2) {
        u32 element = Playback_GetElement();
        player->stickX = element;
        player->stick2X = element >> 16;
        element = Playback_GetElement();
        player->stickY = element;
        player->stick2Y = element >> 16;
    } else {
        Control_ClearControl(player);
        for (i = 0; i < 14; i++) {
            player->down[i] = PlayerHuman_KeyDown(player, i);
            if (PlayerHuman_KeyPress(player, i)) {
                player->buttonsHeld |= 1 << i;
            }
        }
        if (player->down[1]) {
            if (player->buttonsHeld) {
                player->comboInputs[player->comboCount++] = player->buttonsHeld;
                if (player->comboCount >= 16) {
                    player->comboCount = 0;
                }
                PlayerHuman_EvaluateCombo(player);
            }
        } else {
            player->comboCount = 0;
        }
        if (!Wheel_IsConnected(player->device) && player->down[2]) {
            player->down[2] = 0xFF;
        }
        if (Input_GetAnalog(player->device, &stickX, &stickY, &substickX, &substickY)) {
            u8 mode = player->stickMode;
            switch (mode) {
            case 0:
                break;
            case 1:
                player->stickX = stickX;
                player->stickY = stickY;
                break;
            case 2:
                player->stickX = stickX;
                player->stickY = stickY;
                if (player->stickY > 0) {
                    player->down[2] = player->stickY << 1;
                } else {
                    player->down[3] = ~player->stickY << 1;
                }
                break;
            case 3:
                player->stickX = stickX;
                player->stickY = stickY;
                if (player->stickY > 0) {
                    player->down[2] = player->stickY << 1;
                } else {
                    player->down[3] = ~player->stickY << 1;
                }
                break;
            case 4:
                player->stickX = stickX;
                player->stickY = stickY;
                if (player->stickY < 0) {
                    player->down[3] = ~player->stickY << 1;
                }
                break;
            case 5:
                if (Timer_Interrupt300() && stickY > 0) {
                    player->down[12] = 0xFF;
                }
                break;
            case 6:
                if (Timer_Interrupt300() && stickX > 0) {
                    player->down[1] = 0xFF;
                }
                break;
            default:
                Dprintf("%d\n", mode);
                break;
            }
            switch (player->substickMode) {
            case 0:
                break;
            case 1:
                player->stickX = substickX;
                break;
            case 2:
                player->stickY = substickY;
                if (player->stickY >= 0) {
                    if (!player->down[2]) {
                        player->down[2] = player->stickY << 1;
                    }
                } else {
                    if (!player->down[3]) {
                        player->down[3] = ~player->stickY << 1;
                    }
                }
                break;
            case 3:
                player->stickX = substickX;
                player->stickY = substickY;
                if (player->stickY > 0) {
                    if (!player->down[2]) {
                        player->down[2] = player->stickY << 1;
                    }
                } else {
                    if (!player->down[3]) {
                        player->down[3] = ~player->stickY << 1;
                    }
                }
                break;
            case 4:
                player->stickX = substickX;
                if (substickY < 0 && !player->down[3]) {
                    player->down[3] = ~substickY << 1;
                }
                break;
            case 5:
                if (Timer_Interrupt300()) {
                    if (substickY > 40 || substickY < -40 || substickX > 40 || substickX < -40) {
                        player->down[12] = 0xFF;
                        player->buttonsHeld |= 0x1000;
                    }
                }
                break;
            case 6:
                if (Timer_Interrupt300()) {
                    if (substickY > 40 || substickY < -40 || substickX > 40 || substickX < -40) {
                        player->down[1] = 0xFF;
                        player->buttonsHeld |= 2;
                    }
                }
                break;
            }
        }
        if (Input_KeyDown(player->device, player->keys[6])) {
            player->stickY = 0x7F;
        }
        if (Input_KeyDown(player->device, player->keys[7])) {
            player->stickY = -0x7F;
        }
        if (Input_KeyDown(player->device, player->keys[9])) {
            player->stickX = -0x7F;
        }
        if (Input_KeyDown(player->device, player->keys[8])) {
            player->stickX = 0x7F;
        }
        if (g_playbackStatus == 1) {
            Playback_PutElement((u16)player->stickX + (player->stick2X << 16));
            Playback_PutElement((u16)player->stickY + (player->stick2Y << 16));
        }
    }
    return 1;
}

/* AI players: their input is set by the AI; only play it back / record it. */
s32 Player_AIControlReader(Player *player) {
    extern u32 g_playbackStatus;
    s32 element;

    if (player->flags & 1) {
        return 0;
    }
    if (g_playbackStatus == 2) {
        element = Playback_GetElement();
        player->stickX = element;
        player->stick2X = (s8)(element & 0xFFFF0000) >> 16;
        element = Playback_GetElement();
        player->stickY = element;
        player->stick2Y = (s8)(element & 0xFFFF0000) >> 16;
    } else if (g_playbackStatus == 1) {
        Playback_PutElement(player->stickX + (player->stick2X << 16));
        Playback_PutElement(player->stickY + (player->stick2Y << 16));
    }
    return 1;
}

/* action: { mode (0x22: take control away), _, count, subjects... }: lock
 * or unlock the drivers of the subjects' cars (flags & 1), with force
 * feedback off / back to the player's setting. */
s32 ActionVehicleChangeControl(u32 *action) {
    u8 i;
    Thing *vehicle;
    Player *player;
    s32 isHuman;

    for (i = 0; i < action[2]; i++) {
        vehicle = SLOT_LOAD(Thing *, (action[i + 3] + 0x28));
        if (vehicle == NULL) {
            continue;
        }
        player = GETP(vehicle->player);
        if (player == NULL) {
            continue;
        }
        if (action[0] == 0x22) {
            isHuman = 0;
            if (player != NULL && (void *)GETP(player->controlReader) == (void *)Player_DefaultControlReader) {
                isHuman = 1;
            }
            if (isHuman) {
                Input_SetForceFeedbackActive(player->device, 0);
            }
            player->flags |= 1;
        } else {
            isHuman = 0;
            if (player != NULL && (void *)GETP(player->controlReader) == (void *)Player_DefaultControlReader) {
                isHuman = 1;
            }
            if (isHuman) {
                Input_SetForceFeedbackActive(player->device, GameSettings_GetControllerSettings(player->port)->data[0]);
            }
            player->flags &= ~1;
        }
    }
    return 1;
}

