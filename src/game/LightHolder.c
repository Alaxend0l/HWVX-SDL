#include "common.h"

typedef struct LightHolderDef {
    u8 enabled;
    u8 pointLight;
    u8 unused2;
    u8 overlayMode;
    u32 lifeMin;
    u32 lifeRange;
    u8 radiusKeys[8];
    u8 colorKeys[8];
} LightHolderDef;

typedef struct LightHolderLight {
    Vector4 pos;                  /* 0x00 */
    KeyColorState color;          /* 0x10 colour track playback */
    KeyF32State radius;           /* 0x40 radius track playback */
    PTR32(LightHolderDef) def;    /* 0x60 */
    PTR32(Thing) thing;           /* 0x64 */
    u32 unused68;                 /* 0x68 */
    u32 life;                     /* 0x6C */
    u32 pad70;                    /* 0x70 */
    u32 unused74;                 /* 0x74 */
    f32 rate;                     /* 0x78 key rate multiplier */
    u32 unused7C;                 /* 0x7C */
} LightHolderLight;

_Static_assert(offsetof(LightHolderLight, color) == 0x10, "LightHolderLight.color");
_Static_assert(offsetof(LightHolderLight, radius) == 0x40, "LightHolderLight.radius");
_Static_assert(offsetof(LightHolderLight, def) == 0x60, "LightHolderLight.def");
_Static_assert(sizeof(LightHolderLight) == 0x80, "LightHolderLight size");

LightHolderLight g_pLight[16];
u16 g_lightTextureIndex;


void LightHolder_Init(void) {
    extern LightHolderLight g_pLight[16];
    u32 i;

    for (i = 0; i < 16; i++) {
        SETP(g_pLight[i].def, NULL);
    }
}

void LightHolder_SetTexture(u16 index) {
    extern u16 g_lightTextureIndex;
    g_lightTextureIndex = index;
    Gfx_SetTextureWrapMode(index, 1);
}

s32 LightHolder_Add(LightHolderDef *def, Vector4 *pos, void *thing) {
    extern LightHolderLight g_pLight[16];
    s32 i;

    for (i = 0; i < 16; i++) {
        if (GETP(g_pLight[i].def) == NULL) {
            SETP(g_pLight[i].def, def);
            g_pLight[i].pos = *pos;
            SETP(g_pLight[i].thing, thing);
            g_pLight[i].life = RandomAdd_s32(def->lifeMin, def->lifeRange);
            g_pLight[i].rate = 1.0f;
            if (def->lifeMin != 0 && def->lifeRange != 0) {
                g_pLight[i].rate = 1.0f / ((f32)g_pLight[i].life / (f32)def->lifeMin);
            }
            g_pLight[i].radius.reset = 1;
            g_pLight[i].color.reset = 1;
            return i;
        }
    }
    return -1;
}

void LightHolder_Delete(s32 index) {
    extern LightHolderLight g_pLight[16];
    if (index >= 0) {
        SETP(g_pLight[index].def, NULL);
    }
}

void LightHolder_Render(void) {
    extern LightHolderLight g_pLight[16];
    Vector4 camera __attribute__((aligned(16)));
    f32 dist;
    GXColor color;
    LightHolderLight *light;
    Vector4 *pos;
    LightHolderDef *def;
    u32 i;

    Gfx_GetCameraPosition(&camera);
    for (i = 0; i < 16; i++) {
        light = &g_pLight[i];
        def = GETP(light->def);
        if (def != NULL) {
            if (GETP(light->thing) != NULL) {
                pos = &GETP(light->thing)->pos;
            } else {
                pos = &light->pos;
            }
            if (def->enabled != 0) {
                DistanceXYZ(&camera, pos, &dist);
                if (dist <= light->radius.value) {
                    color = light->color.color;
                    Gfx_ScreenOverlay(&color, GETP(light->def)->overlayMode);
                }
            }
        }
    }
}

void LightHolder_Heartbeat(void) {
    extern LightHolderLight g_pLight[16];
    extern u32 g_iDeltaTimeMS;
    extern f32 g_fDeltaTimeMS;
    LightHolderLight *light;
    u32 i;
    u32 delta;

    delta = g_iDeltaTimeMS;
    for (i = 0; i < 16; i++) {
        light = &g_pLight[i];
        if (GETP(light->def) != NULL) {
            if (light->life == 0 || light->life > delta) {
                if (light->life != 0) {
                    light->life -= delta;
                }
                ExtractColorKey(GETP(light->def)->colorKeys, &light->color, g_fDeltaTimeMS * light->rate);
                ExtractF32Key(GETP(light->def)->radiusKeys, &light->radius, g_fDeltaTimeMS * light->rate);
            } else {
                LightHolder_Delete(i);
            }
        }
    }
}

void LightHolder_SetRenderState(void *camera) {
    extern LightHolderLight g_pLight[16];
    LightHolderLight *light;
    u32 i;
    Vector4 *pos;
    LightHolderDef *def;
    GXColor color;

    Gfx_RemovePointLights();
    for (i = 0; i < 16; i++) {
        light = &g_pLight[i];
        def = GETP(light->def);
        if (def != NULL && def->pointLight != 0) {
            if (GETP(light->thing) != NULL) {
                pos = &GETP(light->thing)->pos;
            } else {
                pos = &light->pos;
            }
            if (CompareDistances(camera, pos, light->radius.value) != 1) {
                color = light->color.color;
                Gfx_AddPointLight(&color, pos, light->radius.value);
            }
        }
    }
}

