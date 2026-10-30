#include "Input.h"


VirtualPad g_VirtualPad[4];

s32 Input_GetPlayerDevice(s32 player) {
    s16 i;

    player--;
    if ((u32)player > 4) {
        for (i = 0; i < 4; i++) {
            if ((s8)g_VirtualPad[i].unk0[1] == 0) {
                g_VirtualPad[i].unk0[1] = 1;
                g_VirtualPad[i].unk0[2] = 1;
                return i;
            }
        }
        return -2;
    }
    g_VirtualPad[player].unk0[1] = 1;
    g_VirtualPad[player].unk0[2] = 1;
    return player;
}

s32 Input_GetAnalog(s32 chan, s16 *stickX, s16 *stickY, s16 *substickX, s16 *substickY) {
    u32 i;

    if (chan == -1) {
        for (i = 0; i < 4; i++) {
            if (stickX != NULL && _fabs(*stickX) > _fabs((s8)g_VirtualPad[i].keyValue[0x10])) {
                *stickX = (s8)g_VirtualPad[i].keyValue[0x10];
            }
            if (stickY != NULL && _fabs(*stickY) > _fabs((s8)g_VirtualPad[i].keyValue[0x11])) {
                *stickY = (s8)g_VirtualPad[i].keyValue[0x11];
            }
            if (substickX != NULL && _fabs(*substickX) > _fabs((s8)g_VirtualPad[i].keyValue[0x12])) {
                *substickX = (s8)g_VirtualPad[i].keyValue[0x12];
            }
            if (substickY != NULL && _fabs(*substickY) > _fabs((s8)g_VirtualPad[i].keyValue[0x13])) {
                *substickY = (s8)g_VirtualPad[i].keyValue[0x13];
            }
        }
    } else {
        if (stickX != NULL) {
            *stickX = (s8)g_VirtualPad[chan].keyValue[0x10];
        }
        if (stickY != NULL) {
            *stickY = (s8)g_VirtualPad[chan].keyValue[0x11];
        }
        if (substickX != NULL) {
            *substickX = (s8)g_VirtualPad[chan].keyValue[0x12];
        }
        if (substickY != NULL) {
            *substickY = (s8)g_VirtualPad[chan].keyValue[0x13];
        }
    }
    return 1;
}

s8 Input_KeyPress(s32 chan, u8 key) {
    s32 i;
    if (key > 0x14) {
        return 0;
    }
    if (chan == -1) {
        for (i = 0; i < 4; i++) {
            if ((g_VirtualPad[i].keyPress >> key) & 1) {
                return 1;
            }
        }
    } else {
        return (g_VirtualPad[chan].keyPress >> key) & 1;
    }
    return 0;
}

u8 Input_KeyDown(s32 chan, u8 key) {
    s32 i;
    if (key == 0xFF) {
        return 0;
    }
    if (chan == -1) {
        for (i = 0; i < 4; i++) {
            if ((g_VirtualPad[i].keyDown >> key) & 1) {
                return g_VirtualPad[chan].keyValue[key];
            }
        }
    } else if ((g_VirtualPad[chan].keyDown >> key) & 1) {
        return g_VirtualPad[chan].keyValue[key];
    }
    return 0;
}

void Input_SetForceFeedbackActive(s32 chan, u8 active) {
    g_VirtualPad[chan].forceFeedbackActive = active;
}
