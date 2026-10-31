#include "common.h"
#include "Shader.h"

extern ShaderHolder *g_pShaderHolder;

void ShaderHolder_Create(void) {
    if (g_pShaderHolder != NULL) {
        Deallocate(g_pShaderHolder);
    }
    g_pShaderHolder = Allocate(sizeof(ShaderHolder), 0, "");
    memset(g_pShaderHolder, 0, sizeof(ShaderHolder));
    ShaderHolder_Init();
}

void ShaderHolder_Destroy(void) {
    if (g_pShaderHolder != NULL) {
        Deallocate(g_pShaderHolder);
        g_pShaderHolder = NULL;
    }
}

void ShaderHolder_Init(void) {

    g_pShaderHolder->numShaders = 0;
    g_pShaderHolder->numGX = 0;
}

s32 ShaderHolder_AddShader(ShaderDef *def) {
    u32 i;
    s32 slot = -1;
    u32 n;

    for (i = 0; i < g_pShaderHolder->numShaders; i++) {
        if (GETP(GETP(g_pShaderHolder->shaders[i])->def) == def) {
            return i;
        }
    }
    for (i = 0; i < SHADER_MAX; i++) {
        if (g_pShaderHolder->slots[i].refCount == 0) {
            slot = i;
            break;
        }
    }
    n = g_pShaderHolder->numShaders++;
    SETP(g_pShaderHolder->shaders[n], &g_pShaderHolder->slots[slot]);
    SETP(GETP(g_pShaderHolder->shaders[n])->def, def);
    GETP(g_pShaderHolder->shaders[n])->color.reset = 1;
    GETP(g_pShaderHolder->shaders[n])->texture.reset = 1;
    GETP(g_pShaderHolder->shaders[n])->point.reset = 1;
    GETP(g_pShaderHolder->shaders[n])->hasColorKey = def->colorKeys.count > 1;
    GETP(g_pShaderHolder->shaders[n])->hasTextureKey = GETP(def->textureKeys)->numKeys != 0;
    GETP(g_pShaderHolder->shaders[n])->hasPointKey = def->pointKeys.count > 1;
    GETP(g_pShaderHolder->shaders[n])->refCount = 1;
    return n;
}

void ShaderHolder_RemoveShader(void *id) {
    u32 i;

    if (g_pShaderHolder == NULL) {
        return;
    }
    for (i = 0; i < g_pShaderHolder->numShaders; i++) {
        if (GETP(GETP(g_pShaderHolder->shaders[i])->def) == id) {
            if (--GETP(g_pShaderHolder->shaders[i])->refCount == 0) {
                g_pShaderHolder->numShaders--;
                SETP(g_pShaderHolder->shaders[i], GETP(g_pShaderHolder->shaders[g_pShaderHolder->numShaders]));
            }
            return;
        }
    }
}

void ShaderHolder_AddGX(void *gx, u32 index) {
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
    extern f32 g_fDeltaTimeMS;
    u32 i;
    f32 dt;

    dt = g_fDeltaTimeMS;
    for (i = 0; i < g_pShaderHolder->numShaders; i++) {
        Shader *shader = GETP(g_pShaderHolder->shaders[i]);

        if (shader->hasColorKey != 0) {
            ExtractColorKey(&GETP(shader->def)->colorKeys, &shader->color, dt);
        }
        if (shader->hasPointKey != 0) {
            ExtractPointKey((u8 *)&GETP(shader->def)->pointKeys, (u8 *)&shader->point, dt);
        }
        if (shader->hasTextureKey != 0) {
            ExtractTextureKey(GETP(GETP(shader->def)->textureKeys), &shader->texture, dt);
        }
    }
    for (i = 0; i < g_pShaderHolder->numGX; i++) {
        Shader *shader = GETP(g_pShaderHolder->gx[i].shader);
        ShadedGeom *gx = GETP(g_pShaderHolder->gx[i].gx);
        ShaderParams *mtx = GETP(gx->params);

        if (GETP(shader->def)->isMovie != 0) {
            gx->texture = MovieManager_GetTextureIndex();
        }
        if (shader->texture.texture != 0) {
            gx->texture = shader->texture.texture;
        }
        if (mtx != NULL) {
            if (shader->hasColorKey != 0) {
                mtx->color = shader->color.color;
                mtx->mode = 2;
            }
            if (shader->hasPointKey != 0) {
                mtx->scrollX = shader->point.x;
                mtx->scrollY = shader->point.y;
            }
        }
    }
}

