#include "common.h"

typedef struct VirtualPad {
    u8 unk0[3];
    s8 forceFeedbackActive;
    u32 keyDown;
    u32 keyPress;
    u32 keyRelease;
    u8 keyValue[0x20];
} VirtualPad;

extern VirtualPad g_VirtualPad[4];

typedef struct PADStatus {
    u16 button;
    s8 stickX;
    s8 stickY;
    s8 substickX;
    s8 substickY;
    u8 triggerLeft;
    u8 triggerRight;
    u8 analogA;
    u8 analogB;
    s8 err;
} PADStatus;
extern PADStatus ngcPad[4];
