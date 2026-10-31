#include "common.h"

typedef struct LightHolderDef {
    u8 enabled;
    u8 pointLight;
    u8 unk2;
    u8 overlayMode;
    u32 lifeMin;
    u32 lifeRange;
    u8 radiusKeys[8];
    u8 colorKeys[8];
} LightHolderDef;

typedef struct LightHolderLight {
    Vector4 pos;
    u8 unk10[0x24];
    GXColor color;
    u8 unk38[4];
    u8 unk3C;
    u8 unk3D[3];
    f32 radius;
    u8 unk44[0xC];
    u8 unk50;
    u8 unk51[0xF];
    PTR32(LightHolderDef) def;
    PTR32(Thing) thing;
    u32 unk68;
    u32 life;
    u32 unk70;
    u32 unk74;
    f32 rate;
    u32 unk7C;
} LightHolderLight;

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
            g_pLight[i].unk50 = 1;
            g_pLight[i].unk3C = 1;
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
                if (dist <= light->radius) {
                    color = light->color;
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
                ExtractColorKey(GETP(light->def)->colorKeys, light->unk10, g_fDeltaTimeMS * light->rate);
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
            if (CompareDistances(camera, pos, light->radius) != 1) {
                color = light->color;
                Gfx_AddPointLight(&color, pos, light->radius);
            }
        }
    }
}

