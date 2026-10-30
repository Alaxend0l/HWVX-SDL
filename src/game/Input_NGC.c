#include "Input.h"

typedef struct RumbleStatus {
    s32 duration;
    s32 active;
} RumbleStatus;

typedef struct InputUnk108 {
    u16 button;
    u8 unk2[4];
    u8 unk6;
    u8 unk7;
    u8 unk8[2];
    s8 unkA;
    u8 unkB;
} InputUnk108;

/* TODO: these belong in .bss (original names, in order: rumbleStatus, g_mouse, g_padDown, g_padUp,
 * g_pKeyToDI, ngcPad, ngcPadLast, Wheels). Plain statics get laid out in first-use order, which breaks
 * Input_Init's offsets; the {0} initialisers keep declaration order but put them in .data. */
static RumbleStatus rumbleStatus[4] = {0};
static s32 s_rumbleTime[4] = {0};
static u8 s_unk30[0x40] = {0};
static u8 s_unk70[0x40] = {0};
static u16 s_keyMap[20] = {0};
static PADStatus s_unkD8[4] = {0};
static InputUnk108 s_unk108[4] = {0};
extern u16 g_pKeyToDI[20];
extern u8 g_padUpPos[4];
extern u8 g_padDownPos[4];
extern u32 g_StopInput;
extern void *wheels;
u8 IsOpened__8LGWheelsFl(void *wheels, s32 chan);
void PlaySpringForce__8LGWheelsFlScUcs(void *wheels, s32 chan, s8 offset, u8 saturation, s16 coefficient);
void PlayFrontalCollisionForce__8LGWheelsFlUc(void *wheels, s32 chan, u8 force);

static inline void Input_SetKeyDown(VirtualPad *pad, u16 key) {
    s32 k;

    for (k = 0; k < 20; k++) {
        if (s_keyMap[k] == key) {
            pad->keyDown |= 1 << k;
            pad->keyPress |= 1 << k;
        }
    }
}

static inline void Input_SetKeyUp(VirtualPad *pad, u16 key) {
    s32 k;

    for (k = 0; k < 20; k++) {
        if (s_keyMap[k] == key) {
            pad->keyDown &= ~(1 << k);
            pad->keyRelease |= 1 << k;
        }
    }
}

s32 Input_Create(void) {
    void *__nw__FUl(u32 size);
    void *__ct__8LGWheelsFv(void *self);
    void GenerateNonLinValues__8LGWheelsFlUc(void *self, s32 chan, u8 value);
    void GBAConnectivityInit__Fv(void);
    void *w;

    InitTables__Fv();
    Input_Init();
    SISetSamplingRate(1);
    w = __nw__FUl(0x1430);
    if (w != NULL) {
        w = __ct__8LGWheelsFv(w);
    }
    wheels = w;
    GenerateNonLinValues__8LGWheelsFlUc(wheels, 0, 0x50);
    GenerateNonLinValues__8LGWheelsFlUc(wheels, 1, 0x64);
    GenerateNonLinValues__8LGWheelsFlUc(wheels, 2, 0x64);
    GenerateNonLinValues__8LGWheelsFlUc(wheels, 3, 0x64);
    PADSetSamplingCallback(PadSamplingCallback__Fv);
    GBAConnectivityInit__Fv();
    return 1;
}


void Input_Init(void) {
    u32 i;

    memset(s_unk30, 0, 8);
    memset(s_unk70, 0, 8);
    for (i = 0; i < 4; i++) {
        memset(&g_VirtualPad[i], 0, sizeof(VirtualPad));
        g_VirtualPad[i].unk0[2] = 1;
        g_VirtualPad[i].forceFeedbackActive = 1;
        g_padUpPos[i] = 0;
        g_padDownPos[i] = 0;
        rumbleStatus[i].duration = -1;
        rumbleStatus[i].active = 0;
        s_unk108[i].unkA = 0;
    }
    s_rumbleTime[0] = 200;
    s_rumbleTime[1] = 200;
    g_StopInput = 0;
}


void Input_Destroy(void) {
    PADControlMotor(0, 0);
    PADControlMotor(1, 0);
    PADControlMotor(2, 0);
    PADControlMotor(3, 0);
}


void Input_Heartbeat(void) {
    void GBAConnectivityLoop__Fv(void);
    void pollControllers__Fv(void);
    extern s32 g_iDeltaTimeMS;
    s32 i;
    s32 j;
    u16 key;

    if (!g_StopInput) {
        GBAConnectivityLoop__Fv();
        pollControllers__Fv();
    }
    for (i = 0; i < 4; i++) {
        g_VirtualPad[i].keyPress = 0;
        for (j = 0; j < g_padDownPos[i]; j++) {
            key = ((u16 (*)[8])s_unk30)[i][j];
            Input_SetKeyDown(&g_VirtualPad[i], key);
            ((u16 (*)[8])s_unk30)[i][j] = key;
        }
        for (j = 0; j < g_padUpPos[i]; j++) {
            key = ((u16 (*)[8])s_unk70)[i][j];
            Input_SetKeyUp(&g_VirtualPad[i], key);
            ((u16 (*)[8])s_unk70)[i][j] = key;
        }
        g_padUpPos[i] = 0;
        g_padDownPos[i] = 0;
        if (rumbleStatus[i].active) {
            if (rumbleStatus[i].duration < 0) {
                rumbleStatus[i].active = 0;
                PADControlMotor(i, 0);
            } else {
                rumbleStatus[i].duration -= g_iDeltaTimeMS;
            }
        }
    }
}


void Input_SetForceFeedback(s32 chan, f32 strength, f32 duration) {
    if (g_VirtualPad[chan].forceFeedbackActive) {
        rumbleStatus[chan].duration = 32.0f * duration * strength;
        rumbleStatus[chan].active = 1;
        PADControlMotor(chan, 1);
    }
}

void Input_SetAlternativeForceFeedback(void) {

}

void Input_ForceFeedbackPause(void) {

}

s32 Input_GetContollersPluggedIn(void) {
    return 0xF;
}

void InitTables__Fv(void) {
    memset(g_pKeyToDI, 0, sizeof(g_pKeyToDI));
    g_pKeyToDI[0] = 0x1000;
    g_pKeyToDI[1] = 0;
    g_pKeyToDI[2] = 1;
    g_pKeyToDI[3] = 2;
    g_pKeyToDI[4] = 8;
    g_pKeyToDI[5] = 4;
    g_pKeyToDI[6] = 0x200;
    g_pKeyToDI[7] = 0x400;
    g_pKeyToDI[8] = 0x800;
    g_pKeyToDI[9] = 0x100;
    g_pKeyToDI[10] = 0;
    g_pKeyToDI[11] = 0x40;
    g_pKeyToDI[12] = 0x10;
    g_pKeyToDI[13] = 0x20;
    g_pKeyToDI[14] = 0;
    g_pKeyToDI[15] = 0;
    g_pKeyToDI[16] = 0;
    g_pKeyToDI[17] = 0;
    g_pKeyToDI[18] = 0;
    g_pKeyToDI[19] = 0;
}

void Wheel_PlaySpringForce(s32 chan, s8 offset, u8 saturation, s16 coefficient) {
    if (IsOpened__8LGWheelsFl(wheels, chan)) {
        PlaySpringForce__8LGWheelsFlScUcs(wheels, chan, offset, saturation, coefficient);
    }
}

void Wheel_PlayDamperForce(s32 chan, s32 coefficient, s32 offset) {
    if (IsOpened__8LGWheelsFl(wheels, chan)) {
        PlayDamperForce__8LGWheelsFlUcs(wheels, chan, coefficient, offset);
    }
}

void Wheel_PlayConstantForce(s32 chan, s32 coefficient, s32 offset) {
    if (IsOpened__8LGWheelsFl(wheels, chan)) {
        PlayConstantForce__8LGWheelsFlsUs(wheels, chan, coefficient, offset);
    }
}

void Wheel_PlayCarAirborne(s32 chan) {
    if (IsOpened__8LGWheelsFl(wheels, chan)) {
        PlayCarAirborne__8LGWheelsFl(wheels, chan);
    }
}

void Wheel_StopCarAirborne(s32 chan) {
    if (IsOpened__8LGWheelsFl(wheels, chan)) {
        StopCarAirborne__8LGWheelsFl(wheels, chan);
    }
}

void Wheel_PlayFrontalCollisionForce(s32 chan, u8 force) {
    if (IsOpened__8LGWheelsFl(wheels, chan)) {
        PlayFrontalCollisionForce__8LGWheelsFlUc(wheels, chan, force);
    }
}

s8 Wheel_IsConnected(s32 arg0) {
    return IsConnected__8LGWheelsFl(wheels, arg0);
}

void Wheel_PlayAllAirborne(void) {

}

void PadSamplingCallback__Fv(void) {
    PADStatus pads[4];
    s32 i;

    PADRead(pads);
    PADClamp(pads);
    for (i = 0; i < 4; i++) {
        if (pads[i].err != -3) {
            memcpy(&ngcPad[i], &pads[i], sizeof(PADStatus));
        }
    }
}

