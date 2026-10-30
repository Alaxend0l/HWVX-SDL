#include "common.h"

typedef struct FogSettings {
    u8 type;
    u8 pad1[3];
    f32 nearDistance;
    f32 farDistance;
    GXColor color;
} FogSettings;

typedef struct WorldFog {
    u8 type;
    u8 pad1[0x14 - 0x1];
    f32 nearDistance;
    f32 farDistance;
    u8 pad1C[0xC0 - 0x1C];
    PTR32(GXColor) color;
} WorldFog;

typedef struct VolumeShape {
    u8 pad0[0x8];
    s32 type;
    u8 padC[0x10 - 0xC];
    Vector4 center;
    f32 radius;
    f32 radiusSq;
    PTR32(void) bsp;
    PTR32(FogSettings) fog;
} VolumeShape;

typedef struct VolumeList {
    u32 count;
    PTR32(PTR32(VolumeShape)) volumes;
} VolumeList;

typedef struct VolumeCell {
    VolumeList types[7];
} VolumeCell;

typedef struct VolumeVec3 {
    f32 x;
    f32 y;
    f32 z;
} VolumeVec3;

typedef struct VolumeHolder {
    VolumeVec3 origin;      /* 0x00 */
    u8 padC[4];
    PTR32(VolumeCell) cells;      /* 0x10 */
    PTR32(s32) cellIndex;         /* 0x14 */
    u8 pad18[8];
    VolumeVec3 cellSize;    /* 0x20 */
    s32 numX;               /* 0x2C */
    s32 numY;               /* 0x30 */
    s32 numZ;               /* 0x34 */
} VolumeHolder;


static inline s8 VolumeManager_IsPointInVolume(Vector4 *point, VolumeShape *shape) {
    Vector4 diff;

    switch (shape->type) {
    case 0:
        return BSPN0_CollideWithVector(GETP(shape->bsp), point);
    case 1:
        PSVECSubtract(point, &shape->center, &diff);
        if (PSVECSquareMag(&diff) < shape->radiusSq) {
            return 1;
        }
        return 0;
    }
    return 0;
}

extern u8 g_VolumeHolder[];  /* typed per function below */

void VolumeManager_Create(void) {
    #define g_VolumeHolder (*(u8 **)(void *)&g_VolumeHolder)
    g_VolumeHolder = NULL;
#undef g_VolumeHolder
}

void VolumeManager_Release(void) {
    #define g_VolumeHolder (*(u8 **)(void *)&g_VolumeHolder)
    g_VolumeHolder = NULL;
    SetReverb(0, 0); /* 3rd/4th args were ignored on GC */
    SetReverb(1, 0); /* 3rd/4th args were ignored on GC */
#undef g_VolumeHolder
}
extern u8 g_bReset[];  /* typed per function below */

void VolumeManager_Initialize(void) {
    typedef struct World {
        u8 pad0[0x4];
        s32 volumeHolder;
    } World;
    #define g_VolumeHolder (*(u8 **)(void *)&g_VolumeHolder)
    #define g_bReset (*(u8 *)(void *)&g_bReset)
    VolumeManager_Release();
    g_VolumeHolder = PTR(((World *)GETP(g_worldData.world))->volumeHolder);
    g_bReset = 1;
#undef g_bReset
#undef g_VolumeHolder
}

static inline s32 VolumeManager_GetCell(VolumeHolder *holder, Vector4 *point) {
    s32 x;
    s32 y;
    s32 z;

    if (holder == NULL) {
        return 0;
    }
    x = (point->x - holder->origin.x) / holder->cellSize.x;
    y = (point->y - holder->origin.y) / holder->cellSize.y;
    z = (point->z - holder->origin.z) / holder->cellSize.z;
    if (x < 0 || x >= holder->numX) {
        return 0;
    }
    if (y < 0 || y >= holder->numY) {
        return 0;
    }
    if (z < 0 || z >= holder->numZ) {
        return 0;
    }
    return GETP(holder->cellIndex)[z * holder->numX + holder->numZ * (y * holder->numX) + x];
}


s32 VolumeManager_GetVolumeCellIndex(Vector4 *point) {
    #define g_VolumeHolder (*(u8 **)(void *)&g_VolumeHolder)
    u8 *holder = g_VolumeHolder;
    s32 x;
    s32 y;
    s32 z;

    if (holder == NULL) {
        return 0;
    }
    x = (point->x - M2C_FIELD(holder, f32 *, 0x0)) / M2C_FIELD(holder, f32 *, 0x20);
    y = (point->y - M2C_FIELD(holder, f32 *, 0x4)) / M2C_FIELD(holder, f32 *, 0x24);
    z = (point->z - M2C_FIELD(holder, f32 *, 0x8)) / M2C_FIELD(holder, f32 *, 0x28);
    if (x < 0 || x >= M2C_FIELD(holder, s32 *, 0x2C)) {
        return 0;
    }
    if (y < 0 || y >= M2C_FIELD(holder, s32 *, 0x30)) {
        return 0;
    }
    if (z < 0 || z >= M2C_FIELD(holder, s32 *, 0x34)) {
        return 0;
    }
    return SLOT_LOAD(s32 *, (u8 *)(holder) + 0x14)[z * M2C_FIELD(holder, s32 *, 0x2C) + M2C_FIELD(holder, s32 *, 0x34) * (y * M2C_FIELD(holder, s32 *, 0x2C)) + x];
#undef g_VolumeHolder
}

s32 VolumeManager_GetNumVolumesOfType(u32 type) {
    typedef struct Volume {
        u8 pad0[0xC];
        u32 type;
        u8 pad10[0x20];
    } Volume;
    typedef struct World {
        u8 pad0[0x68];
        u32 numVolumes;
        PTR32(Volume) volumes;
    } World;
    World *world = GETP(g_worldData.world);
    s32 count = 0;
    u32 i;

    for (i = 0; i < world->numVolumes; i++) {
        if (type == GETP(world->volumes)[i].type) {
            count++;
        }
    }
    return count;
}

void *VolumeManager_GetVolumeTypeByIndex(u32 type, u32 index) {
    typedef struct Volume {
        u8 pad0[0xC];
        u32 type;
        u8 pad10[0x20];
    } Volume;
    typedef struct World {
        u8 pad0[0x68];
        u32 numVolumes;
        PTR32(Volume) volumes;
    } World;
    World *world = GETP(g_worldData.world);
    u32 i;

    for (i = 0; i < world->numVolumes; i++) {
        if (GETP(world->volumes)[i].type == type) {
            if (index == 0) {
                return GETP(world->volumes) + i;
            }
            index--;
        }
    }
    return NULL;
}

s32 VolumeManager_GetNumVolumesOfTypeByCell(s32 type, s32 cell) {
    #define g_VolumeHolder (*(u8 **)(void *)&g_VolumeHolder)
    if (g_VolumeHolder != NULL) {
        return M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(g_VolumeHolder) + 0x10) + cell * 0x38 + type * 8, s32 *, 0x0);
    }
    return 0;
#undef g_VolumeHolder
}

s32 VolumeManager_GetVolumeTypeByIndexByCell(s32 type, s32 index, s32 cell) {
    typedef struct VolumeList {
        s32 count;
        PTR32(s32) volumes;
    } VolumeList;
    typedef struct VolumeCell {
        VolumeList types[7];
    } VolumeCell;
    #define g_VolumeHolder (*(u8 **)(void *)&g_VolumeHolder)
    VolumeCell *cells = SLOT_LOAD(VolumeCell *, (u8 *)(g_VolumeHolder) + 0x10);
    return GETP(cells[cell].types[type].volumes)[index];
#undef g_VolumeHolder
}

s8 IsPointWithinVolume(Vector4 *point, void *volume, f32 *distSq) {
    typedef struct Shape {
        u8 pad0[0x8];
        s32 type;
        u8 padC[0x10 - 0xC];
        Vector4 center;
        f32 radius;
        f32 radiusSq;
        PTR32(void) bsp;
    } Shape;
    Shape *shape = volume;
    Vector4 diff __attribute__((aligned(16)));

    switch (shape->type) {
    case 0:
        return BSPN0_CollideWithVector(GETP(shape->bsp), point);
    case 1:
        PSVECSubtract(point, &shape->center, &diff);
        *distSq = PSVECSquareMag(&diff);
        return *distSq < shape->radiusSq;
    }
    return 0;
}


VolumeShape *IsPointWithinForceBox(Vector4 *point) {
    #define g_VolumeHolder (*(VolumeHolder **)(void *)&g_VolumeHolder)
    VolumeShape *volume;
    u32 count;
    s32 i;
    s32 cell;

    cell = VolumeManager_GetCell(g_VolumeHolder, point);
    count = g_VolumeHolder != NULL ? GETP(g_VolumeHolder->cells)[cell].types[1].count : 0;
    i = 0;
    while (i < count) {
        volume = GETP(GETP(GETP(g_VolumeHolder->cells)[cell].types[1].volumes)[i]);
        if (VolumeManager_IsPointInVolume(point, volume)) {
            return volume;
        }
        i++;
    }
    return NULL;
#undef g_VolumeHolder
}


void VolumeManager_SetReverbLevel(Vector4 *point) {
    typedef struct ReverbSettings {
        u32 type;
        u16 volume;
        u16 feedback;
    } ReverbSettings;
    typedef struct WorldReverb {
        u8 pad0[0x24];
        f32 level;
        f32 delay;
        f32 feedback;
    } WorldReverb;
    typedef struct World {
        u8 pad0[0x9C];
        PTR32(WorldReverb) reverb;
    } World;
    #define g_VolumeHolder (*(VolumeHolder **)(void *)&g_VolumeHolder)
    #define g_bReset (*(s8 *)(void *)&g_bReset)
    static u16 sfx_volume = 0xFFFF;
    static u16 sfx_feedback = 0xFFFF;
    static u16 music_volume = 0xFFFF;
    static u16 music_feedback = 0xFFFF;
    WorldReverb *settings;
    s32 level;
    s32 feedback;
    s32 delay;
    VolumeShape *volume;
    s32 count;
    u32 i;
    s32 cell;

    settings = GETP(((World *)GETP(g_worldData.world))->reverb);
    level = 32767.0f * settings->level;
    feedback = 32767.0f * settings->feedback;
    delay = settings->delay;
    cell = VolumeManager_GetCell(g_VolumeHolder, point);
    count = g_VolumeHolder != NULL ? GETP(g_VolumeHolder->cells)[cell].types[2].count : 0;
    for (i = 0; i < count; i++) {
        volume = GETP(GETP(GETP(g_VolumeHolder->cells)[cell].types[2].volumes)[i]);
        if (VolumeManager_IsPointInVolume(point, volume)) {
            ReverbSettings *reverb = (ReverbSettings *)GETP(volume->fog);

            if (reverb->type == 0) {
                if (reverb->volume != music_volume || reverb->feedback != music_feedback) {
                    SetReverb(0, reverb->volume); /* 3rd/4th args were ignored on GC */
                    music_volume = reverb->volume;
                    music_feedback = reverb->feedback;
                }
            } else if (reverb->type == 1) {
                if (reverb->volume != sfx_volume || reverb->feedback != sfx_feedback) {
                    SetReverb(1, reverb->volume); /* 3rd/4th args were ignored on GC */
                    sfx_volume = reverb->volume;
                    sfx_feedback = reverb->feedback;
                }
            } else {
                if (reverb->volume != music_volume || reverb->feedback != music_feedback) {
                    SetReverb(0, (u16)level); /* 3rd/4th args were ignored on GC */
                    music_volume = reverb->volume;
                    music_feedback = reverb->feedback;
                }
                if (reverb->volume != sfx_volume || reverb->feedback != sfx_feedback) {
                    SetReverb(1, (u16)level); /* 3rd/4th args were ignored on GC */
                    sfx_volume = reverb->volume;
                    sfx_feedback = reverb->feedback;
                }
            }
            return;
        }
    }
    if (g_bReset || (u16)level != music_volume || (u16)feedback != music_feedback) {
        SetReverb(0, (u16)level); /* 3rd/4th args were ignored on GC */
        music_volume = level;
        music_feedback = feedback;
    }
    if (g_bReset || (u16)level != sfx_volume || (u16)feedback != sfx_feedback) {
        SetReverb(1, (u16)level); /* 3rd/4th args were ignored on GC */
        sfx_volume = level;
        sfx_feedback = feedback;
    }
    g_bReset = 0;
#undef g_bReset
#undef g_VolumeHolder
}



void VolumeManager_SetFogLevel(Vector4 *point) {
    typedef struct World {
        u8 pad0[0x9C];
        PTR32(WorldFog) fog;
    } World;
    #define g_VolumeHolder (*(VolumeHolder **)(void *)&g_VolumeHolder)
    static u32 fogColor;
    static u8 fogType = 0xFF;
    static f32 fogNearDistance = -1.0f;
    static f32 fogFarDistance = -1.0f;
    WorldFog *settings;
    u32 count;
    VolumeShape *volume;
    s32 i;
    s32 cell;
    GXColor color;
    GXColor worldColor;

    settings = GETP(((World *)GETP(g_worldData.world))->fog);
    cell = VolumeManager_GetCell(g_VolumeHolder, point);
    count = g_VolumeHolder != NULL ? GETP(g_VolumeHolder->cells)[cell].types[4].count : 0;
    for (i = 0; i < count; i++) {
        volume = GETP(GETP(GETP(g_VolumeHolder->cells)[cell].types[4].volumes)[i]);
        if (VolumeManager_IsPointInVolume(point, volume)) {
            FogSettings *fog = GETP(volume->fog);

            if (*(u32 *)&fog->color != fogColor || fogType != fog->type || fogNearDistance != fog->nearDistance || fogFarDistance != fog->farDistance) {
                color = fog->color;
                Gfx_SetFog(&color, fog->type, fog->nearDistance, fog->farDistance);
                fogColor = *(u32 *)&fog->color;
                fogType = fog->type;
                fogNearDistance = fog->nearDistance;
                fogFarDistance = fog->farDistance;
            }
            return;
        }
    }
    if (*(u32 *)GETP(settings->color) != fogColor || fogType != settings->type || fogNearDistance != settings->nearDistance || fogFarDistance != settings->farDistance) {
        worldColor = *GETP(settings->color);
        Gfx_SetFog(&worldColor, settings->type, settings->nearDistance, settings->farDistance);
        fogColor = *(u32 *)GETP(settings->color);
        fogType = settings->type;
        fogNearDistance = settings->nearDistance;
        fogFarDistance = settings->farDistance;
    }
#undef g_VolumeHolder
}



