#include "common.h"


s8 Player_IsHuman(void *player) {
    s32 isHuman = 0;

    if (player != NULL && SLOT_LOAD(void *, ((u8 *)player + 0x20)) == Player_DefaultControlReader) {
        isHuman = 1;
    }
    return isHuman;
}

void PlayerHuman_Init(void *player) {
    typedef struct PlayerHuman {
        u8 pad0[0x6C];
        u32 flags;
        u8 pad70[0xC8 - 0x70];
        s32 wheelChannel;
        u8 padCC[0x114 - 0xCC];
        void *bindings;
        u8 bindingData[4];
    } PlayerHuman;
    PlayerHuman *human = player;

    Player_Init(player, 0);
    human->wheelChannel = 0;
    PlayerHuman_SetDefaultControls(player, 0);
    human->flags = 0;
    human->bindings = human->bindingData;
}

void Player_Init(void *player, s32 arg1) {
    s32 i;

    Control_ClearControl(player);
    M2C_FIELD(player, u8 *, 0x40) = 0;
    M2C_FIELD(player, u8 *, 0x31) = 0;
    M2C_FIELD(player, s32 *, 0x3C) = 0;
    M2C_FIELD(player, s32 *, 0x68) = -1;
    M2C_FIELD(player, s32 *, 0x6C) = 0;
    memset((u8 *)player + 0x70, 0, 0x38);
    M2C_FIELD(player, u8 *, 0x71) = 1;
    M2C_FIELD(player, s32 *, 0x34) = 0;
    for (i = 0; i < 8; i++) {
        ((s16 *)((u8 *)player + 0x94))[i] = 30;
    }
    M2C_FIELD(player, s16 *, 0xA2) = 0;
    M2C_FIELD(player, s32 *, 0x6C) = 0;
    M2C_FIELD(player, s16 *, 0xAC) = -1;
    M2C_FIELD(player, s16 *, 0xAE) = -1;
}

void Player_SetName(void *player, s32 port, const char *name) {

    strcpy((char *)player + 0x40, name);
    M2C_FIELD(player, s32 *, 0x60) = port;
    if (Player_IsHuman(player)) {
        M2C_FIELD(player, s32 *, 0xC8) = Input_GetPlayerDevice(M2C_FIELD(player, s32 *, 0x60));
    }
}

void Player_RefreshInterface(void *arg0) {
    typedef struct Thing {
        u8 pad0[0x8C];
        void (**vtable)();
    } Thing;
    typedef struct Player {
        u8 pad0[0x2C];
        Thing *thing;
        u8 pad30[0x68 - 0x30];
        u32 interfaceIndex;
        u32 flags;
        u8 pad70;
        u8 nameDirty;
    } Player;
    struct {
        u32 unk0;
        u8 *script;
        u32 x;
        u32 y;
        u8 unk10[0xC0];
    } info;
    Player *player = arg0;
    Thing *thing;

    if (player->thing != NULL && (player->flags & 0x10)) {
        memset(&info, 0, sizeof(info));
        player->flags |= 0x200;
        thing = player->thing;
        ((void (*)(Thing *, void *))thing->vtable[8])(thing, &info);
        if (player->interfaceIndex < 0xFFFFFFFF) {
            InterfaceManager_ReleaseInterface(player->interfaceIndex);
            player->interfaceIndex = 0xFFFFFFFF;
        }
        if (info.script != NULL) {
            player->interfaceIndex = InterfaceManager_Create(info.script, player, 0);
            InterfaceManager_SetOffset(player->interfaceIndex, info.x, info.y);
            player->nameDirty = 1;
        }
        player->flags &= ~0x200;
    }
}

void Player_ReleaseInterface(void *player) {
    u32 index = M2C_FIELD(player, u32 *, 0x68);

    if (index != 0xFFFFFFFF) {
        InterfaceManager_ReleaseInterface(index);
    }
}

extern u8 g_carAVTable[];
extern u8 g_creatureVTable[];
void Player_SetThing(void *player, void *thing) {
    s32 isCreature;

    SLOT_STORE(((u8 *)player + 0x2C), thing);
    if (thing != NULL) {
        SLOT_STORE((SLOT_LOAD(u8 *, ((u8 *)player + 0x2C)) + 0x94), player);
        *(u32 *)((u8 *)player + 0x38) = *(u32 *)((u8 *)thing + 0xA8);
        if (SLOT_LOAD(void *, ((u8 *)thing + 0x8C)) == g_carAVTable) {
            *(u32 *)((u8 *)player + 0x3C) = *(u32 *)((u8 *)thing + 0x3BC);
            CarA_SetSlavePlayers(thing);
            if (!Player_IsHuman(player)) {
                AI_Car_Reset(player);
            }
        } else if (SLOT_LOAD(void *, ((u8 *)thing + 0x8C)) == g_creatureVTable) {
            *(u32 *)((u8 *)player + 0x3C) = *(u32 *)((u8 *)thing + 0xB8);
            Thing_SetSlavePlayers(thing);
        }
        if (SLOT_LOAD(void *, ((u8 *)player + 0x2C)) == thing) {
            Player_RefreshInterface(player);
        }
    }
}

s32 Player_InputHeartbeat(void *arg0) {
    return M2C_FIELD(arg0, s8 (**)(), 0x20)() != 0;
}

void Player_GetInterfaceInfo(void *arg0, void *arg1) {
    typedef struct Thing {
        u8 pad0[0x8C];
        void (**vtable)();
    } Thing;
    typedef struct Player {
        u8 pad0[0x2C];
        Thing *thing;
        u8 attached;
        u8 pad31[0x71 - 0x31];
        u8 nameDirty;
        u8 pad72[0x7C - 0x72];
        s32 score;
    } Player;
    typedef struct InterfaceInfo {
        u8 pad0[0x74];
        char text[0xC];
        u8 state;
    } InterfaceInfo;
    Player *player = arg0;
    InterfaceInfo *info = arg1;
    Thing *thing;

    if (player->thing != NULL) {
        info->state = 0;
        thing = player->thing;
        ((void (*)(Thing *, InterfaceInfo *))thing->vtable[8])(thing, info);
    } else {
        info->state = 2;
        if (player->attached != 0) {
            info->state = 1;
            player->attached = 0;
        }
    }
    if (player->nameDirty != 0) {
        sprintf(info->text, "%d", player->score);
        player->nameDirty = 0;
    }
}

void Player_DetachThingDeath(void *arg0) {
    void *temp_r5;

    temp_r5 = SLOT_LOAD(void *, (u8 *)(arg0) + 0x2C);
    SLOT_STORE((u8 *)(arg0) + 0x2C, NULL);
    M2C_FIELD(arg0, s8 *, 0x30) = 1;
    if (temp_r5 != NULL) {
        M2C_FIELD(temp_r5, s32 *, 0x94) = 0;
    }
}

void PlayerHuman_Heartbeat(void *player) {
    s32 c;
    u8 a;
    u8 b;
    void *car;

    car = SLOT_LOAD(void *, (u8 *)(player) + 0x2C);
    if (car != NULL && !(M2C_FIELD(car, u32 *, 0x74) & 2)) {
        ForceFeedback_GetForce((u8 *)car + 0x40, &a, &b, &c);
        Input_SetAlternativeForceFeedback(M2C_FIELD(player, s32 *, 0xC8), a, b);
    }
    Player_Heartbeat(player);
}

void Player_Heartbeat()
{}

void PlayerHuman_SetDefaultControls(void *arg0, s32 index) {
    typedef struct { u8 unk0; u8 unk1; u8 keys[14]; } KeyBinding;
    typedef struct { u8 pad0[0xB8]; u8 keys[14]; u8 unkC6; u8 unkC7; } PlayerHuman;
    extern KeyBinding g_keyBinding[9];
    PlayerHuman *player = arg0;
    u16 i;

    memset(player->keys, 0, 0x10);
    for (i = 0; i < 14; i++) {
        player->keys[i] = 0xFF;
    }
    index++;
    for (i = 0; i < 14; i++) {
        player->keys[g_keyBinding[0].keys[i]] = g_keyBinding[index].keys[i];
    }
    player->unkC6 = g_keyBinding[index].unk0;
    player->unkC7 = g_keyBinding[index].unk1;
}

void PlayerHuman_SetControlBinding(void *arg0, void *arg1) {
    typedef struct { u8 unk0; u8 unk1; u8 keys[14]; } KeyBinding;
    typedef struct { u8 pad0[0xB8]; u8 keys[14]; u8 unkC6; u8 unkC7; } PlayerHuman;
    extern KeyBinding g_keyBinding[9];
    PlayerHuman *player = arg0;
    KeyBinding *binding = arg1;
    u16 i;

    memset(player->keys, 0, 0x10);
    for (i = 0; i < 14; i++) {
        player->keys[i] = 0xFF;
    }
    for (i = 0; i < 14; i++) {
        player->keys[g_keyBinding[0].keys[i]] = binding->keys[i];
    }
    player->unkC6 = binding->unk0;
    player->unkC7 = binding->unk1;
}

s32 Player_NULL(void) {
    return 0;
}

void PlayerHuman_EvaluateCombo(u8 *player) {
    extern s8 g_bFreezeWorld;
    u32 *combo = (u32 *)(player + 0xD8);

    if (!(*(u32 *)(player + 0x6C) & 0x20)) {
        if (*(u32 *)(player + 0xCC) == 13 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x800) &&
            (combo[3] & 4) && (combo[4] & 4) && (combo[5] & 8) && (combo[6] & 8) && (combo[7] & 0x20) &&
            (combo[8] & 0x20) && (combo[9] & 0x800) && (combo[10] & 0x800) && (combo[11] & 0x800)) {
            *(u32 *)(player + 0x6C) |= 0x20;
        }
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 4) && (combo[2] & 4)) {
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 4) && (combo[2] & 8)) {
        player[0x32] = 7;
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 4) && (combo[2] & 0x10)) {
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x10)) {
        player[0x32] = 3;
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x20)) {
        TimeHandler_SetSlowMotion();
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 0x10) && (combo[2] & 0x10)) {
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 0x10) && (combo[2] & 0x20)) {
        CameraManager_ReturnAllToPlayer();
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 8) && (combo[2] & 8)) {
    } else if (*(u32 *)(player + 0xCC) == 6 && (combo[0] & 0x800) && (combo[1] & 0x800) && (combo[2] & 0x800) &&
               (combo[3] & 0x20) && (combo[4] & 0x20)) {
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 0x800) && (combo[1] & 0x20) && (combo[2] & 0x20)) {
        g_bFreezeWorld = !g_bFreezeWorld;
    } else if (*(u32 *)(player + 0xCC) == 4 && (combo[0] & 4) && (combo[1] & 8) && (combo[2] & 8)) {
        player[0x32] = 8;
    }
}

void Player_DefaultControlReader()
{
}

s32 Player_AIControlReader(s8 *ctrl) {
    extern u32 g_playbackStatus;
    s32 element;

    if (M2C_FIELD(ctrl, u32 *, 0x6C) & 1) {
        return 0;
    }
    if (g_playbackStatus == 2) {
        element = Playback_GetElement();
        ctrl[0] = element;
        ctrl[2] = (s8)(element & 0xFFFF0000) >> 16;
        element = Playback_GetElement();
        ctrl[1] = element;
        ctrl[3] = (s8)(element & 0xFFFF0000) >> 16;
    } else if (g_playbackStatus == 1) {
        Playback_PutElement(ctrl[0] + (ctrl[2] << 16));
        Playback_PutElement(ctrl[1] + (ctrl[3] << 16));
    }
    return 1;
}

s32 ActionVehicleChangeControl(u32 *action) {
    u8 i;
    u8 *vehicle;
    u8 *player;
    s32 isHuman;

    for (i = 0; i < action[2]; i++) {
        vehicle = SLOT_LOAD(u8 *, (action[i + 3] + 0x28));
        if (vehicle == NULL) {
            continue;
        }
        player = SLOT_LOAD(u8 *, (vehicle + 0x94));
        if (player == NULL) {
            continue;
        }
        if (action[0] == 0x22) {
            isHuman = 0;
            if (player != NULL && SLOT_LOAD(void *, (player + 0x20)) == Player_DefaultControlReader) {
                isHuman = 1;
            }
            if (isHuman) {
                Input_SetForceFeedbackActive(*(s32 *)(player + 0xC8), 0);
            }
            *(u32 *)(player + 0x6C) |= 1;
        } else {
            isHuman = 0;
            if (player != NULL && SLOT_LOAD(void *, (player + 0x20)) == Player_DefaultControlReader) {
                isHuman = 1;
            }
            if (isHuman) {
                Input_SetForceFeedbackActive(*(s32 *)(player + 0xC8), *GameSettings_GetControllerSettings(*(s32 *)(player + 0x60)));
            }
            *(u32 *)(player + 0x6C) &= ~1;
        }
    }
    return 1;
}

