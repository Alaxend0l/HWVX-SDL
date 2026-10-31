#include "common.h"

Flash g_pFlash[16];

void FlashHolder_Init(void) {
    u32 i;

    for (i = 0; i < 16; i++) {
        SETP(g_pFlash[i].flash, NULL);
    }
}

s32 FlashHolder_Add(FlashDef *flash, Vector4 *pos, s32 arg2, f32 time) {
    s32 i;

    for (i = 0; i < 16; i++) {
        if (GETP(g_pFlash[i].flash) == NULL) {
            SETP(g_pFlash[i].flash, flash);
            g_pFlash[i].pos = *pos;
            g_pFlash[i].unk60 = arg2;
            g_pFlash[i].remaining = flash->duration;
            g_pFlash[i].colorState.reset = 1;
            g_pFlash[i].pointState.reset = 1;
            g_pFlash[i].time = time;
            return i;
        }
    }
    return -1;
}

static inline void FlashHolder_Remove(s32 index) {
    if (index >= 0) {
        SETP(g_pFlash[index].flash, NULL);
    }
}

void FlashHolder_Heartbeat(void) {
    extern u32 g_iDeltaTimeMS;
    extern f32 g_fDeltaTimeMS;
    Flash *flash;
    u32 i;
    u32 delta;

    delta = g_iDeltaTimeMS;
    for (i = 0; i < 16; i++) {
        flash = &g_pFlash[i];
        if (GETP(flash->flash) != NULL) {
            if (flash->remaining == 0 || flash->remaining > delta) {
                if (flash->remaining != 0) {
                    flash->remaining -= delta;
                }
                ExtractColorKey(&GETP(GETP(flash->flash)->colorKey)->keys, &flash->colorState, g_fDeltaTimeMS);
                ExtractPointKey((u8 *)&GETP(flash->flash)->pointKey, (u8 *)&flash->pointState, g_fDeltaTimeMS);
                if (GETP(GETP(flash->flash)->colorKey)->mode == 5) {
                    flash->colorState.color.a = flash->colorState.color.a * flash->time;
                } else {
                    flash->colorState.color.r = flash->colorState.color.r * flash->time;
                    flash->colorState.color.g = flash->colorState.color.g * flash->time;
                    flash->colorState.color.b = flash->colorState.color.b * flash->time;
                }
            } else {
                FlashHolder_Remove(i);
            }
        }
    }
}

void FlashHolder_Render(void) {
    Vector4 camera __attribute__((aligned(16)));
    GXColor color;
    Flash *flash;
    FlashDef *owner;
    u32 i;

    Gfx_GetCameraPosition(&camera);
    for (i = 0; i < 16; i++) {
        flash = &g_pFlash[i];
        owner = GETP(flash->flash);
        if (owner != NULL && owner->enabled != 0) {
            color = flash->colorState.color;
            Gfx_ScreenOverlay(&color, GETP(owner->colorKey)->mode);
        }
    }
}

