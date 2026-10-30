#include "common.h"


void ShaderHolder_Create(void) {
    extern ShaderHolder *g_pShaderHolder;
    if (g_pShaderHolder != NULL) {
        Deallocate(g_pShaderHolder);
    }
    g_pShaderHolder = Allocate(0x2008, 0, "");
    memset(g_pShaderHolder, 0, 0x2008);
    ShaderHolder_Init();
}

void ShaderHolder_Destroy(void) {
    extern void *g_pShaderHolder;
    if (g_pShaderHolder != NULL) {
        Deallocate(g_pShaderHolder);
        g_pShaderHolder = NULL;
    }
}

void ShaderHolder_Init(void) {
    extern ShaderHolder *g_pShaderHolder;

    g_pShaderHolder->numShaders = 0;
    g_pShaderHolder->numGX = 0;
}

s32 ShaderHolder_AddShader(ShaderDef *def) {
    extern ShaderHolder *g_pShaderHolder;
    u32 i;
    s32 slot = -1;
    u32 n;

    for (i = 0; i < g_pShaderHolder->numShaders; i++) {
        if (GETP(((Shader *)GETP(g_pShaderHolder->shaders[i]))->def) == def) {
            return i;
        }
    }
    for (i = 0; i < 0x40; i++) {
        if (g_pShaderHolder->slots[i].refCount == 0) {
            slot = i;
            break;
        }
    }
    n = g_pShaderHolder->numShaders++;
    SETP(g_pShaderHolder->shaders[n], &g_pShaderHolder->slots[slot]);
    SETP(((Shader *)GETP(g_pShaderHolder->shaders[n]))->def, def);
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->unk2C = 1;
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->unk38 = 1;
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->unk58 = 1;
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->hasColorKey = def->numColorKeys > 1;
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->hasTextureKey = *GETP(def->textureKeys) != 0;
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->hasPointKey = def->numPointKeys > 1;
    ((Shader *)GETP(g_pShaderHolder->shaders[n]))->refCount = 1;
    return n;
}

void ShaderHolder_RemoveShader(void *id) {
    extern ShaderHolder *g_pShaderHolder;
    u32 i;

    if (g_pShaderHolder == NULL) {
        return;
    }
    for (i = 0; i < g_pShaderHolder->numShaders; i++) {
        if (SLOT_LOAD(void *, (u8 *)(GETP(g_pShaderHolder->shaders[i])) + 0x5C) == id) {
            if (--M2C_FIELD(GETP(g_pShaderHolder->shaders[i]), s32 *, 0x64) == 0) {
                g_pShaderHolder->numShaders--;
                SETP(g_pShaderHolder->shaders[i], GETP(g_pShaderHolder->shaders[g_pShaderHolder->numShaders]));
            }
            return;
        }
    }
}

void ShaderHolder_AddGX(void *gx, u32 index) {
    extern ShaderHolder *g_pShaderHolder;
    u32 i;
    u32 n;

    if (gx == NULL || index >= g_pShaderHolder->numShaders) {
        return;
    }
    for (i = 0; i < g_pShaderHolder->numGX; i++) {
        if (GETP(g_pShaderHolder->gx[i].gx) == gx) {
            return;
        }
    }
    n = g_pShaderHolder->numGX++;
    SETP(g_pShaderHolder->gx[n].gx, gx);
    SETP(g_pShaderHolder->gx[n].shader, GETP(g_pShaderHolder->shaders[index]));
}

void ShaderHolder_RemoveGX(s32 gx) {
    extern ShaderHolder *g_pShaderHolder;
    u32 i;

    if (g_pShaderHolder == NULL) {
        return;
    }
    for (i = 0; i < g_pShaderHolder->numGX; i++) {
        if (GETP(g_pShaderHolder->gx[i].gx) == (void *)PTR(gx)) {
            g_pShaderHolder->numGX--;
            g_pShaderHolder->gx[i] = g_pShaderHolder->gx[g_pShaderHolder->numGX];
            return;
        }
    }
}

void ShaderHolder_Heartbeat(void) {
    extern ShaderHolder *g_pShaderHolder;
    extern f32 g_fDeltaTimeMS;
    u32 i;
    f32 dt;

    dt = g_fDeltaTimeMS;
    for (i = 0; i < g_pShaderHolder->numShaders; i++) {
        if (((u8 *)GETP(g_pShaderHolder->shaders[i]))[0x60] != 0) {
            ExtractColorKey(SLOT_LOAD(u8 *, ((u8 *)GETP(g_pShaderHolder->shaders[i]) + 0x5C)) + 4, GETP(g_pShaderHolder->shaders[i]), dt);
        }
        if (((u8 *)GETP(g_pShaderHolder->shaders[i]))[0x62] != 0) {
            ExtractPointKey(SLOT_LOAD(u8 *, ((u8 *)GETP(g_pShaderHolder->shaders[i]) + 0x5C)) + 0xC, (u8 *)GETP(g_pShaderHolder->shaders[i]) + 0x40, dt);
        }
        if (((u8 *)GETP(g_pShaderHolder->shaders[i]))[0x61] != 0) {
            ExtractTextureKey(SLOT_LOAD(TextureKeyAnim *, (SLOT_LOAD(u8 *, ((u8 *)GETP(g_pShaderHolder->shaders[i]) + 0x5C)) + 0x14)), (TextureKeyState *)((u8 *)GETP(g_pShaderHolder->shaders[i]) + 0x30), dt);
        }
    }
    for (i = 0; i < g_pShaderHolder->numGX; i++) {
        u8 *shader = GETP(g_pShaderHolder->gx[i].shader);
        u8 *gx = GETP(g_pShaderHolder->gx[i].gx);
        u8 *mtx = SLOT_LOAD(u8 *, (gx + 0xC));

        if (*SLOT_LOAD(u8 *, (shader + 0x5C)) != 0) {
            *(u16 *)(gx + 0x18) = MovieManager_GetTextureIndex();
        }
        if (*(u16 *)(shader + 0x30) != 0) {
            *(u16 *)(gx + 0x18) = *(u16 *)(shader + 0x30);
        }
        if (mtx != NULL) {
            if (shader[0x60] != 0) {
                *(u32 *)(mtx + 0x8) = *(u32 *)(shader + 0x24);
                *(u32 *)(mtx + 0xC) = 2;
            }
            if (shader[0x62] != 0) {
                *(f32 *)(mtx + 0x0) = *(f32 *)(shader + 0x40);
                *(f32 *)(mtx + 0x4) = *(f32 *)(shader + 0x44);
            }
        }
    }
}

