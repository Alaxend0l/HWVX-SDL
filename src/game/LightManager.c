#include "common.h"
#include "CarA.h"

typedef struct ThingLightingData {
    u32 numSteps;           /* 0x00 frames to fade over, -1: never fade */
    u32 step;               /* 0x04 */
    PTR32(CarA) thing;            /* 0x08 */
    GXColor parallel;       /* 0x0C */
    GXColor parallelTarget; /* 0x10 */
    GXColor parallelDelta;  /* 0x14 */
    GXColor ambient;        /* 0x18 */
    GXColor ambientTarget;  /* 0x1C */
    GXColor ambientDelta;   /* 0x20 */
    s8 dirty;               /* 0x24 */
} ThingLightingData;

ThingLightingData g_ThingLightingData[0x400];
Light *g_directionalLights[5];
u32 g_numThingsToLight;
u32 g_numDirectionalLights;
s8 g_bLightVolumesExist;
GXColor g_ambientColor;

static inline s8 LightManager_AddThing(CarA *thing, u32 numSteps) {
    static u32 offsetCount;

    if (g_numThingsToLight >= 0x400) {
        THING_LIGHT_INDEX(thing) = -1;
        return 0;
    }
    if (THING_LIGHT_INDEX(thing) < g_numThingsToLight && GETP(g_ThingLightingData[THING_LIGHT_INDEX(thing)].thing) == thing) {
        g_ThingLightingData[THING_LIGHT_INDEX(thing)].dirty = 1;
        return 1;
    }
    SETP(g_ThingLightingData[g_numThingsToLight].thing, thing);
    g_ThingLightingData[g_numThingsToLight].numSteps = numSteps;
    g_ThingLightingData[g_numThingsToLight].step = offsetCount % numSteps;
    g_ThingLightingData[g_numThingsToLight].dirty = 1;
    THING_LIGHT_INDEX(thing) = g_numThingsToLight;
    g_numThingsToLight++;
    offsetCount += 5;
    if (offsetCount == 0xFFFFFFFF) {
        offsetCount = 0;
    }
    return 1;
}




void LightManager_Initialize(void) {
    u32 i;
    u32 r;
    u32 g;
    u32 b;
    Volume *volume;
    Light *light;

    r = 0;
    g = 0;
    b = 0;
    g_numDirectionalLights = 0;
    g_numThingsToLight = 0;
    g_bLightVolumesExist = 0;
    memset(g_ThingLightingData, 0, sizeof(g_ThingLightingData));
    for (i = 0; i < ((LightWorld *)GETP(g_worldData.world))->numVolumes; i++) {
        volume = (Volume *)(GETP(((LightWorld *)GETP(g_worldData.world))->volumes) + i * 0x30);
        if (volume->type == 6) {
            light = GETP(volume->light);
            g_bLightVolumesExist = 1;
            if (light->type == 1) {
                g_directionalLights[g_numDirectionalLights++] = light;
            } else if (light->type == 2) {
                r += light->r;
                g += light->g;
                b += light->b;
            } else if (light->type == 3) {
                light->index = VertexColorSeqManager_GetLightIndex(GETP(((LightWorld *)GETP(g_worldData.world))->lights) + light->index * 0x10);
            }
        }
    }
    if (r > 0xFF) {
        g_ambientColor.r = 0xFF;
    } else {
        g_ambientColor.r = r;
    }
    if (g > 0xFF) {
        g_ambientColor.g = 0xFF;
    } else {
        g_ambientColor.g = g;
    }
    if (b > 0xFF) {
        g_ambientColor.b = 0xFF;
    } else {
        g_ambientColor.b = b;
    }
}


void LightManager_LightThing(CarA *thing) {
    Vector4 normal __attribute__((aligned(16)));
    GXColor ambient;
    GXColor parallel;
    GXColor whiteAmbient;
    GXColor whiteParallel;
    f32 dist;
    GXColor seq;
    GXColor seqFade;
    u32 numVolumes;
    s8 tracked;
    ThingLightingData *data;
    u32 i;
    s32 cell;
    Volume *volume;
    Light *light;
    f32 r;
    f32 g;
    f32 b;
    f32 f;
    s8 isNew;

    isNew = 0;
    tracked = 1;
    data = NULL;
    if (!g_bLightVolumesExist || GETP(g_worldData.world) == NULL || ((LightWorld *)GETP(g_worldData.world))->unk0 == 0) {
        whiteAmbient.r = 0xFF;
        whiteAmbient.g = 0xFF;
        whiteAmbient.b = 0xFF;
        whiteParallel.r = 0xFF;
        whiteParallel.g = 0xFF;
        whiteParallel.b = 0xFF;
        Gfx_SetAmbientColor(&whiteAmbient);
        Gfx_SetParallelLight(&whiteParallel, NULL);
        return;
    }
    *(u32 *)&ambient = 0;
    *(u32 *)&parallel = 0;
    if (THING_LIGHT_INDEX(thing) >= g_numThingsToLight || GETP(g_ThingLightingData[THING_LIGHT_INDEX(thing)].thing) != thing) {
        if ((u8 *)GETP(thing->base.vtable) == g_carAVTable) {
            tracked = LightManager_AddThing(thing, 10);
        } else {
            tracked = LightManager_AddThing(thing, 0xFFFFFFFF);
        }
        isNew = 1;
    }
    if (tracked) {
        data = &g_ThingLightingData[THING_LIGHT_INDEX(thing)];
    }
    if (isNew || data->dirty || data->step == data->numSteps) {
        cell = VolumeManager_GetVolumeCellIndex(&thing->base.pos);
        numVolumes = VolumeManager_GetNumVolumesOfTypeByCell(6, cell);
        r = g_ambientColor.r;
        g = g_ambientColor.g;
        b = g_ambientColor.b;
        data->dirty = 0;
        if (data->step == data->numSteps) {
            data->step = 0;
        }
        for (i = 0; i < numVolumes; i++) {
            volume = PTR(VolumeManager_GetVolumeTypeByIndexByCell(6, i, cell));
            if (IsPointWithinVolume(&thing->base.pos, volume, &dist)) {
                light = GETP(volume->light);
                if (volume->falloff == 0 || dist < volume->innerDist) {
                    if (light->type == 3) {
                        VertexColorSeqManager_GetLightColor(light->index, &seq);
                        r += seq.r;
                        g += seq.g;
                        b += seq.b;
                    } else if (light->subtract) {
                        r -= light->r;
                        g -= light->g;
                        b -= light->b;
                    } else {
                        r += light->r;
                        g += light->g;
                        b += light->b;
                    }
                } else {
                    f = 1.0f - (dist - volume->innerDist) / (volume->outerDist - volume->innerDist);
                    if (light->type == 3) {
                        VertexColorSeqManager_GetLightColor(light->index, &seqFade);
                        r += f * seqFade.r;
                        g += f * seqFade.g;
                        b += f * seqFade.b;
                    } else if (light->subtract) {
                        r -= f * light->r;
                        g -= f * light->g;
                        b -= f * light->b;
                    } else {
                        r += f * light->r;
                        g += f * light->g;
                        b += f * light->b;
                    }
                }
            }
        }
        if (tracked) {
            if (data->numSteps != 0xFFFFFFFF && data->numSteps != 1) {
                if (r < 0.0f) {
                    data->parallelTarget.r = 0;
                } else if (r > 255.0f) {
                    data->parallelTarget.r = 0xFF;
                } else {
                    data->parallelTarget.r = r;
                }
                if (g < 0.0f) {
                    data->parallelTarget.g = 0;
                } else if (g > 255.0f) {
                    data->parallelTarget.g = 0xFF;
                } else {
                    data->parallelTarget.g = g;
                }
                if (b < 0.0f) {
                    data->parallelTarget.b = 0;
                } else if (b > 255.0f) {
                    data->parallelTarget.b = 0xFF;
                } else {
                    data->parallelTarget.b = b;
                }
                if (data->parallel.r > data->parallelTarget.r) {
                    data->parallelDelta.r = (data->parallel.r - data->parallelTarget.r) / data->numSteps;
                } else {
                    data->parallelDelta.r = (data->parallelTarget.r - data->parallel.r) / data->numSteps;
                }
                if (data->parallel.g > data->parallelTarget.g) {
                    data->parallelDelta.g = (data->parallel.g - data->parallelTarget.g) / data->numSteps;
                } else {
                    data->parallelDelta.g = (data->parallelTarget.g - data->parallel.g) / data->numSteps;
                }
                if (data->parallel.b > data->parallelTarget.b) {
                    data->parallelDelta.b = (data->parallel.b - data->parallelTarget.b) / data->numSteps;
                } else {
                    data->parallelDelta.b = (data->parallelTarget.b - data->parallel.b) / data->numSteps;
                }
            } else {
                if (r < 0.0f) {
                    data->parallel.r = 0;
                } else if (r > 255.0f) {
                    data->parallel.r = 0xFF;
                } else {
                    data->parallel.r = r;
                }
                if (g < 0.0f) {
                    data->parallel.g = 0;
                } else if (g > 255.0f) {
                    data->parallel.g = 0xFF;
                } else {
                    data->parallel.g = g;
                }
                if (b < 0.0f) {
                    data->parallel.b = 0;
                } else if (b > 255.0f) {
                    data->parallel.b = 0xFF;
                } else {
                    data->parallel.b = b;
                }
            }
        } else {
            if (r < 0.0f) {
                parallel.r = 0;
            } else if (r > 255.0f) {
                parallel.r = 0xFF;
            } else {
                parallel.r = r;
            }
            if (g < 0.0f) {
                parallel.g = 0;
            } else if (g > 255.0f) {
                parallel.g = 0xFF;
            } else {
                parallel.g = g;
            }
            if (b < 0.0f) {
                parallel.b = 0;
            } else if (b > 255.0f) {
                parallel.b = 0xFF;
            } else {
                parallel.b = b;
            }
        }
        g = b = r = 100.0f;
        for (i = 0; i < g_numDirectionalLights; i++) {
            normal = thing->base.axis[2];
            f = PSVECDotProduct(g_directionalLights[i], &normal);
            if (f <= 0.0f) {
                continue;
            }
            r += f * g_directionalLights[i]->r;
            g += f * g_directionalLights[i]->g;
            b += f * g_directionalLights[i]->b;
        }
        if (tracked) {
            if (data->numSteps != 0xFFFFFFFF && data->numSteps != 1) {
                if (r < 0.0f) {
                    data->ambientTarget.r = 0;
                } else if (r > 255.0f) {
                    data->ambientTarget.r = 0xFF;
                } else {
                    data->ambientTarget.r = r;
                }
                if (g < 0.0f) {
                    data->ambientTarget.g = 0;
                } else if (g > 255.0f) {
                    data->ambientTarget.g = 0xFF;
                } else {
                    data->ambientTarget.g = g;
                }
                if (b < 0.0f) {
                    data->ambientTarget.b = 0;
                } else if (b > 255.0f) {
                    data->ambientTarget.b = 0xFF;
                } else {
                    data->ambientTarget.b = b;
                }
                if (data->ambient.r > data->ambientTarget.r) {
                    data->ambientDelta.r = (data->ambient.r - data->ambientTarget.r) / data->numSteps;
                } else {
                    data->ambientDelta.r = (data->ambientTarget.r - data->ambient.r) / data->numSteps;
                }
                if (data->ambient.g > data->ambientTarget.g) {
                    data->ambientDelta.g = (data->ambient.g - data->ambientTarget.g) / data->numSteps;
                } else {
                    data->ambientDelta.g = (data->ambientTarget.g - data->ambient.g) / data->numSteps;
                }
                if (data->ambient.b > data->ambientTarget.b) {
                    data->ambientDelta.b = (data->ambient.b - data->ambientTarget.b) / data->numSteps;
                } else {
                    data->ambientDelta.b = (data->ambientTarget.b - data->ambient.b) / data->numSteps;
                }
            } else {
                if (r < 0.0f) {
                    data->ambient.r = 0;
                } else if (r > 255.0f) {
                    data->ambient.r = 0xFF;
                } else {
                    data->ambient.r = r;
                }
                if (g < 0.0f) {
                    data->ambient.g = 0;
                } else if (g > 255.0f) {
                    data->ambient.g = 0xFF;
                } else {
                    data->ambient.g = g;
                }
                if (b < 0.0f) {
                    data->ambient.b = 0;
                } else if (b > 255.0f) {
                    data->ambient.b = 0xFF;
                } else {
                    data->ambient.b = b;
                }
            }
        } else {
            if (r < 0.0f) {
                ambient.r = 0;
            } else if (r > 255.0f) {
                ambient.r = 0xFF;
            } else {
                ambient.r = r;
            }
            if (g < 0.0f) {
                ambient.g = 0;
            } else if (g > 255.0f) {
                ambient.g = 0xFF;
            } else {
                ambient.g = g;
            }
            if (b < 0.0f) {
                ambient.b = 0;
            } else if (b > 255.0f) {
                ambient.b = 0xFF;
            } else {
                ambient.b = b;
            }
        }
    } else if (tracked) {
        data->step++;
    }
    if (tracked) {
        if (data->numSteps != 0xFFFFFFFF && data->numSteps != 1) {
            if (*(u32 *)&data->parallel != *(u32 *)&data->parallelTarget) {
                if (data->step == data->numSteps) {
                    data->parallel = data->parallelTarget;
                } else {
                    if (data->parallel.r < data->parallelTarget.r) {
                        data->parallel.r += data->parallelDelta.r;
                    } else {
                        data->parallel.r -= data->parallelDelta.r;
                    }
                    if (data->parallel.g < data->parallelTarget.g) {
                        data->parallel.g += data->parallelDelta.g;
                    } else {
                        data->parallel.g -= data->parallelDelta.g;
                    }
                    if (data->parallel.b < data->parallelTarget.b) {
                        data->parallel.b += data->parallelDelta.b;
                    } else {
                        data->parallel.b -= data->parallelDelta.b;
                    }
                }
            }
            if (*(u32 *)&data->ambient != *(u32 *)&data->ambientTarget) {
                if (data->step == data->numSteps) {
                    data->ambient = data->ambientTarget;
                } else {
                    if (data->ambient.r < data->ambientTarget.r) {
                        data->ambient.r += data->ambientDelta.r;
                    } else {
                        data->ambient.r -= data->ambientDelta.r;
                    }
                    if (data->ambient.g < data->ambientTarget.g) {
                        data->ambient.g += data->ambientDelta.g;
                    } else {
                        data->ambient.g -= data->ambientDelta.g;
                    }
                    if (data->ambient.b < data->ambientTarget.b) {
                        data->ambient.b += data->ambientDelta.b;
                    } else {
                        data->ambient.b -= data->ambientDelta.b;
                    }
                }
            }
        }
    }
    if (tracked) {
        Gfx_SetAmbientColor(&data->ambient);
        Gfx_SetParallelLight(&data->parallel, NULL);
        LightHolder_SetRenderState(&thing->base.pos);
        return;
    }
    Gfx_SetAmbientColor(&ambient);
    Gfx_SetParallelLight(&parallel, NULL);
    LightHolder_SetRenderState(&thing->base.pos);
}

