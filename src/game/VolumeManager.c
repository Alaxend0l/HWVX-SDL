/* Volumes: spheres and BSP shapes of the world (World.h WorldVolume), found
 * through a grid of cells (VolumeHolder) listing the volumes of each type
 * touching the cell. Used for force boxes, reverb and fog zones, and by the
 * airbox triggers. */
#include "common.h"
#include "World.h"

/* VOLUME_FOG data */
typedef struct VolumeFog {
    u8 type;
    u8 pad1[3];
    f32 nearDistance;
    f32 farDistance;
    GXColor color;
} VolumeFog;

/* VOLUME_REVERB data */
typedef struct VolumeReverb {
    u32 type;                   /* 0: music, 1: sound effects, else both (at the world level) */
    u16 volume;
    u16 feedback;
} VolumeReverb;

static inline s8 VolumeManager_IsPointInVolume(Vector4 *point, WorldVolume *volume) {
    Vector4 diff;

    switch (volume->shape) {
    case 0:
        return BSPN0_CollideWithVector(GETP(volume->bsp), point);
    case 1:
        PSVECSubtract(point, &volume->center, &diff);
        if (PSVECSquareMag(&diff) < volume->radiusSq) {
            return 1;
        }
        return 0;
    }
    return 0;
}

/* The current world's volume grid, or NULL. port: a host pointer. */
extern u8 g_VolumeHolder[];
#define VOLUME_HOLDER (*(VolumeHolder **)(void *)g_VolumeHolder)
/* s8: apply the world reverb even if unchanged (after a world load). */
extern u8 g_bReset[];
#define VOLUME_RESET (*(s8 *)(void *)g_bReset)

void VolumeManager_Create(void) {
    VOLUME_HOLDER = NULL;
}

void VolumeManager_Release(void) {
    VOLUME_HOLDER = NULL;
    SetReverb(0, 0); /* 3rd/4th args were ignored on GC */
    SetReverb(1, 0); /* 3rd/4th args were ignored on GC */
}

void VolumeManager_Initialize(void) {
    VolumeManager_Release();
    VOLUME_HOLDER = GETP(WORLD()->volumeHolder);
    VOLUME_RESET = 1;
}

/* The grid cell of a point; 0 outside the grid or without one. */
static inline s32 VolumeManager_GetCell(VolumeHolder *holder, Vector4 *point) {
    s32 x;
    s32 y;
    s32 z;

    if (holder == NULL) {
        return 0;
    }
    x = (point->x - holder->origin[0]) / holder->cellSize[0];
    y = (point->y - holder->origin[1]) / holder->cellSize[1];
    z = (point->z - holder->origin[2]) / holder->cellSize[2];
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
    return VolumeManager_GetCell(VOLUME_HOLDER, point);
}

s32 VolumeManager_GetNumVolumesOfType(u32 type) {
    World *world = WORLD();
    s32 count = 0;
    u32 i;

    for (i = 0; i < world->numVolumes; i++) {
        if (type == GETP(world->volumes)[i].type) {
            count++;
        }
    }
    return count;
}

/* The index-th world volume of a type, or NULL. */
void *VolumeManager_GetVolumeTypeByIndex(u32 type, u32 index) {
    World *world = WORLD();
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
    if (VOLUME_HOLDER != NULL) {
        return GETP(VOLUME_HOLDER->cells)[cell].lists[type].count;
    }
    return 0;
}

/* The handle of the index-th volume of a type in a cell. */
s32 VolumeManager_GetVolumeTypeByIndexByCell(s32 type, s32 index, s32 cell) {
    VolumeCell *cells = GETP(VOLUME_HOLDER->cells);
    return GETP(cells[cell].lists[type].volumes)[index].h;
}

/* Whether a point is inside a volume; for a sphere also its squared distance
 * from the centre. */
s8 IsPointWithinVolume(Vector4 *point, void *arg, f32 *distSq) {
    WorldVolume *volume = arg;
    Vector4 diff __attribute__((aligned(16)));

    switch (volume->shape) {
    case 0:
        return BSPN0_CollideWithVector(GETP(volume->bsp), point);
    case 1:
        PSVECSubtract(point, &volume->center, &diff);
        *distSq = PSVECSquareMag(&diff);
        return *distSq < volume->radiusSq;
    }
    return 0;
}


/* The first force box containing a point, or NULL. */
u8 *IsPointWithinForceBox(Vector4 *point) {
    WorldVolume *volume;
    u32 count;
    s32 i;
    s32 cell;

    cell = VolumeManager_GetCell(VOLUME_HOLDER, point);
    count = VOLUME_HOLDER != NULL ? GETP(VOLUME_HOLDER->cells)[cell].lists[VOLUME_FORCE_BOX].count : 0;
    i = 0;
    while (i < count) {
        volume = GETP(GETP(GETP(VOLUME_HOLDER->cells)[cell].lists[VOLUME_FORCE_BOX].volumes)[i]);
        if (VolumeManager_IsPointInVolume(point, volume)) {
            return (u8 *)volume;
        }
        i++;
    }
    return NULL;
}


/* Sets the music and sound effect reverb from the first reverb volume around
 * the point, else from the world's level; only calls SetReverb on changes. */
void VolumeManager_SetReverbLevel(Vector4 *point) {
    static u16 sfx_volume = 0xFFFF;
    static u16 sfx_feedback = 0xFFFF;
    static u16 music_volume = 0xFFFF;
    static u16 music_feedback = 0xFFFF;
    WorldGameData *settings;
    s32 level;
    s32 feedback;
    s32 delay;
    WorldVolume *volume;
    s32 count;
    u32 i;
    s32 cell;

    settings = GETP(WORLD()->gameData);
    level = 32767.0f * settings->reverbLevel;
    feedback = 32767.0f * settings->reverbFeedback;
    delay = settings->reverbDelay;
    cell = VolumeManager_GetCell(VOLUME_HOLDER, point);
    count = VOLUME_HOLDER != NULL ? GETP(VOLUME_HOLDER->cells)[cell].lists[VOLUME_REVERB].count : 0;
    for (i = 0; i < count; i++) {
        volume = GETP(GETP(GETP(VOLUME_HOLDER->cells)[cell].lists[VOLUME_REVERB].volumes)[i]);
        if (VolumeManager_IsPointInVolume(point, volume)) {
            VolumeReverb *reverb = GETP(volume->data);

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
    if (VOLUME_RESET || (u16)level != music_volume || (u16)feedback != music_feedback) {
        SetReverb(0, (u16)level); /* 3rd/4th args were ignored on GC */
        music_volume = level;
        music_feedback = feedback;
    }
    if (VOLUME_RESET || (u16)level != sfx_volume || (u16)feedback != sfx_feedback) {
        SetReverb(1, (u16)level); /* 3rd/4th args were ignored on GC */
        sfx_volume = level;
        sfx_feedback = feedback;
    }
    VOLUME_RESET = 0;
}



/* Sets the fog from the first fog volume around the point, else the
 * world's; only calls Gfx_SetFog on changes. */
void VolumeManager_SetFogLevel(Vector4 *point) {
    static u32 fogColor;
    static u8 fogType = 0xFF;
    static f32 fogNearDistance = -1.0f;
    static f32 fogFarDistance = -1.0f;
    WorldGameData *settings;
    u32 count;
    WorldVolume *volume;
    s32 i;
    s32 cell;
    GXColor color;
    GXColor worldColor;

    settings = GETP(WORLD()->gameData);
    cell = VolumeManager_GetCell(VOLUME_HOLDER, point);
    count = VOLUME_HOLDER != NULL ? GETP(VOLUME_HOLDER->cells)[cell].lists[VOLUME_FOG].count : 0;
    for (i = 0; i < count; i++) {
        volume = GETP(GETP(GETP(VOLUME_HOLDER->cells)[cell].lists[VOLUME_FOG].volumes)[i]);
        if (VolumeManager_IsPointInVolume(point, volume)) {
            VolumeFog *fog = GETP(volume->data);

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
    if (*(u32 *)GETP(settings->fogColor) != fogColor || fogType != settings->fogType || fogNearDistance != settings->fogNear || fogFarDistance != settings->fogFar) {
        worldColor = *GETP(settings->fogColor);
        Gfx_SetFog(&worldColor, settings->fogType, settings->fogNear, settings->fogFar);
        fogColor = *(u32 *)GETP(settings->fogColor);
        fogType = settings->fogType;
        fogNearDistance = settings->fogNear;
        fogFarDistance = settings->fogFar;
    }
}
