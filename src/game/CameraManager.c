#include "common.h"
#include "CarA.h"
#include "World.h"
#include "ThingManager.h"


typedef struct CameraOrbitParams {
    u8 pad0[0x10];
    f32 fov;                /* 0x10 */
    f32 radius;             /* 0x14 */
    f32 height;             /* 0x18 */
    f32 speed;              /* 0x1C */
} CameraOrbitParams;

typedef struct CameraBoneParams {
    Vector4 offset;         /* 0x00 bone-local camera position */
    u8 pad10[4];
    u16 bone;               /* 0x14 */
} CameraBoneParams;

typedef struct CameraZoomParams {
    u8 pad0[0x10];
    f32 zoom;               /* 0x10 */
    u8 pad14[0xC];
} CameraZoomParams;




void *GetCameraManager(void) {
    return CAMERA_MANAGER;
}

s32 CameraManager_Create(void) {
    return 1;
}

/* The decomp has an m2c placeholder here; the original calls CameraManager_Init (asm). */
void CameraManager_Destroy(void) {
    CameraManager_Init();
}

u8 CameraManager_GetActiveCameras(void) {
    return CAMERA_MANAGER->count;
}

void *CameraManager_GetCameraByIndex(u32 index) {
    if (index >= CAMERA_MANAGER->count || index >= 4U) {
        return NULL;
    }
    return &CAMERA_MANAGER->cameras[index];
}

/* Camera spring state (g_springProxy): only ever cleared, by CameraManager_Init. */
typedef struct SpringProxy {
    u8 pad00[0x40];             /* 0x000 */
    Vector4 unk40[2];           /* 0x040 */
    Vector4 unk60[2];           /* 0x060 */
    u8 pad80[0x40];             /* 0x080 */
    Vector4 unkC0[2];           /* 0x0C0 */
    u8 padE0[0x60];             /* 0x0E0 */
    Vector4 unk140[2];          /* 0x140 */
    u8 pad160[0x60];            /* 0x160 */
    s32 unk1C0;                 /* 0x1C0 */
    s32 unk1C4;                 /* 0x1C4 */
    u8 pad1C8[8];               /* 0x1C8 */
} SpringProxy;
_Static_assert(sizeof(SpringProxy) == 0x1D0, "SpringProxy size");
extern SpringProxy g_springProxy;

void CameraManager_Init(void) {
    extern s8 g_cameraManagerPause;
    g_cameraManagerPause = 0;
    CAMERA_MANAGER->count = 0;
    SoundListener_SetTotalListeners(0);
    memset(g_springProxy.unk60, 0, sizeof(g_springProxy.unk60));
    memset(g_springProxy.unk40, 0, sizeof(g_springProxy.unk40));
    g_springProxy.unk1C0 = 0;
    memset(g_springProxy.unk140, 0, sizeof(g_springProxy.unk140));
    memset(g_springProxy.unkC0, 0, sizeof(g_springProxy.unkC0));
    g_springProxy.unk1C4 = 0;
}
extern u8 g_triggerAction_params_default[];  /* typed per function below */

/* A new camera (up to 4), facing +z, with the world's camera effect. */
void *CameraManager_GetNewCamera(void) {
    #define g_triggerAction_params_default (*(u32 *)(void *)&g_triggerAction_params_default)
    extern Vector4 g_zeroVector;
    Camera *cam;
    void *id;
    EmitterParams params;

    if (CAMERA_MANAGER->count < 4) {
        cam = &CAMERA_MANAGER->cameras[CAMERA_MANAGER->count++];
        SoundListener_SetTotalListeners(CAMERA_MANAGER->count);
        cam->pos.x = 0.0f;
        cam->pos.y = 0.0f;
        cam->pos.z = 0.0f;
        cam->dir.x = 0.0f;
        cam->dir.y = 0.0f;
        cam->dir.z = 1.0f;
        cam->up.x = 0.0f;
        cam->up.y = 1.0f;
        cam->up.z = 0.0f;
        cam->right.x = 1.0f;
        cam->right.y = 0.0f;
        cam->right.z = 0.0f;
        SETP(cam->mode, &g_triggerAction_params_default);
        cam->modeTime = 0.0f;
        cam->trim = 0.0f;
        cam->lookDir.x = 0.0f;
        cam->lookDir.y = 0.0f;
        cam->lookDir.z = 10.0f;
        cam->springPos.x = 0.0f;
        cam->springPos.y = 0.0f;
        cam->springPos.z = 1.0f;
        cam->springVel = g_zeroVector;
        cam->unkC0 = g_zeroVector;
        cam->unkD0 = g_zeroVector;
        cam->rollUpVel = g_zeroVector;
        cam->tiltUpVel = g_zeroVector;
        cam->rollUp.x = 0.0f;
        cam->rollUp.y = 1.0f;
        cam->rollUp.z = 0.0f;
        cam->tiltUp = cam->rollUp;
        if (GETP(g_worldData.world) != NULL) {
            id = GETP(GETP(((World *)GETP(g_worldData.world))->gameData)->cameraFx);
            if (id != NULL) {
                SETP(params.emitter, id);
                params.drawDist = 0.0f;
                params.quat = *(Quaternion *)&g_zeroVector;
                params.pos = g_zeroVector;
                params.owner = 0;
                params.vel = params.pos;
                SETP(cam->cameraFx, ThingManager_DARYL_CreateThing(THING_TYPE_EMITTER, &params));
            }
        }
        cam->unk100 = -1;
        cam->flipTimer = -1;
        return cam;
    }
    return NULL;
#undef g_triggerAction_params_default
}

void CameraManager_ResetAllCameras(void) {
    u32 i;

    for (i = 0; i < 4; i++) {
        Camera *cam = &CAMERA_MANAGER->cameras[i];
        cam->pos.x = 0.0f;
        cam->pos.y = 0.0f;
        cam->pos.z = 0.0f;
        cam->dir.x = 0.0f;
        cam->dir.y = 0.0f;
        cam->dir.z = 1.0f;
        cam->up.x = 0.0f;
        cam->up.y = 1.0f;
        cam->up.z = 0.0f;
        cam->right.x = 1.0f;
        cam->right.y = 0.0f;
        cam->right.z = 0.0f;
    }
}

void CameraManager_Pause(s8 arg0) {
    extern s8 g_cameraManagerPause;
    g_cameraManagerPause = arg0;
}

void Camera_PrepareForRender(Camera *camera) {
    extern s8 g_cameraManagerPause;
    s32 window;
    Matrix4 mtx;

    window = Gfx_GetRenderWindowIndex();
    if (window != 0) {
        window--;
    }
    /* paused cameras hold still but are still applied (the vehicle select's
     * reset camera faces +z, where the turntable car is) */
    if (!g_cameraManagerPause) {
        Camera_UpdatePosition(camera);
    }
    Gfx_SetCameraMatrix(&camera->pos, &camera->dir, &camera->up, &camera->right);
    mtx.v[0].x = camera->right.x;
    mtx.v[1].x = camera->right.y;
    mtx.v[2].x = camera->right.z;
    mtx.v[0].y = camera->up.x;
    mtx.v[1].y = camera->up.y;
    mtx.v[2].y = camera->up.z;
    mtx.v[0].z = camera->dir.x;
    mtx.v[1].z = camera->dir.y;
    mtx.v[2].z = camera->dir.z;
    mtx.v[3].x = -(mtx.v[0].x * camera->pos.x + mtx.v[1].x * camera->pos.y + mtx.v[2].x * camera->pos.z);
    mtx.v[3].y = -(mtx.v[0].y * camera->pos.x + mtx.v[1].y * camera->pos.y + mtx.v[2].y * camera->pos.z);
    mtx.v[3].z = -(mtx.v[0].z * camera->pos.x + mtx.v[1].z * camera->pos.y + mtx.v[2].z * camera->pos.z);
    mtx.v[2].w = 0.0f;
    mtx.v[1].w = 0.0f;
    mtx.v[0].w = 0.0f;
    mtx.v[3].w = 1.0f;
    SoundListener_Set(window, 5, &camera->pos);
    SoundListener_Set(window, 6, &camera->vel);
    SoundListener_Set(window, 7, &mtx);
}


s8 RayHitsObjects_CameraSpecial(Vector4 *start, Vector4 *dir, f32 *t, void *info) {
    extern u8 g_destructibleVTable[];
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    Vector4 end __attribute__((aligned(16)));
    GridResult results[256];
    u32 count;
    u32 i;
    Destructible *other;
    OBox *box;
    f32 best;
    f32 hit;

    best = *t;
    max = *start;
    end.x = dir->x * best + start->x;
    end.y = dir->y * best + start->y;
    end.z = dir->z * best + start->z;
    min = end;
    if (start->x < min.x) {
        min.x = start->x;
    }
    if (start->y < min.y) {
        min.y = start->y;
    }
    if (start->z < min.z) {
        min.z = start->z;
    }
    if (end.x > max.x) {
        max.x = end.x;
    }
    if (end.y > max.y) {
        max.y = end.y;
    }
    if (end.z > max.z) {
        max.z = end.z;
    }
    count = Grid_GetObjects(&min, &max, 4, results, 0x100);
    for (i = 0; i < count; i++) {
        other = (Destructible *)PTR(results[i].id);
        if (other->base.flags & 2) {
            continue;
        }
        if ((u8 *)GETP(other->base.vtable) == g_destructibleVTable && GETP(GETP(other->record)->info)->type == 0) {
            box = GETP(GETP(GETP(other->record)->info)->box);
            if ((box->v[1].x - box->v[0].x) * (box->v[1].z - box->v[0].z) < 100.0f) {
                continue;
            }
        }
        if (SegmentVsSphere(&other->base.pos, start, &end, other->base.radius)) {
            hit = THING_VFN(other, THING_V_COLLIDE_WITH_RAY, f32 (*)(void *, Vector4 *, Vector4 *, void *, f32))(other, start, dir, info, best);
            if (hit < *t) {
                best = hit;
            }
        }
    }
    if (best < *t) {
        *t = best;
        return 1;
    }
    return 0;
}



static inline s8 RayHitsTerrainAndObjects_Camera(Vector4 *start, Vector4 *dir, f32 *t, void *info) {
    s8 hit;

    hit = RayHitsTerrain_Aux3(start, dir, t, info);
    if (RayHitsObjects_CameraSpecial(start, dir, t, info)) {
        return 1;
    }
    return hit;
}

void GetTrimmedCameraPos(Camera *camera, Vector4 *from, Vector4 *dir, f32 *trim, f32 dist) {
    extern f32 g_fDeltaTimeSec;
    struct {
        Vector4 point;
        Vector4 normal;
        u8 unk20[0x30];
    } info __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Vector4 d __attribute__((aligned(16)));
    f32 t;
    f32 trimAmt;
    f32 travelled;
    f32 len;
    f32 dot;
    s8 hit;
    s8 trimmed;

    trimAmt = *trim;
    travelled = 0.0f;
    pos = *from;
    trimmed = 0;
    len = MagnitudeVector(dir);
    d.x = -dir->x;
    d.y = -dir->y;
    d.z = -dir->z;
    do {
        t = dist - travelled;
        hit = RayHitsTerrainAndObjects_Camera(&pos, &d, &t, &info);
        pos.x += d.x * t;
        pos.y += d.y * t;
        pos.z += d.z * t;
        if (hit) {
            dot = -PSVECDotProduct(&info.normal, &d);
            d.x += dot * info.normal.x;
            d.y += dot * info.normal.y;
            d.z += dot * info.normal.z;
            if (dot < 0.0f) {
                pos.x = from->x - dir->x * dist;
                pos.y = from->y - dir->y * dist;
                pos.z = from->z - dir->z * dist;
                break;
            }
            if (PSVECSquareMag(&d) < 0.1f) {
                d.y += 1.0f;
            }
            if (PSVECSquareMag(&d)) {
                PSVECNormalize(&d, &d);
            }
            trimmed = 1;
            d.x *= len;
            d.y *= len;
            d.z *= len;
            pos.x += info.normal.x * trimAmt;
            pos.y += info.normal.y * trimAmt;
            pos.z += info.normal.z * trimAmt;
        }
        travelled += t;
        if (t < 1e-7f) {
            break;
        }
    } while (travelled < dist);
    if (trimmed) {
        *trim += 3.0f * g_fDeltaTimeSec;
        if (*trim > 1.8f) {
            *trim = 1.8f;
        }
    } else {
        *trim = 0.0f;
    }
    camera->pos = pos;
}
void GetTrimmedCameraPos2(Vector4 *from, Vector4 *to, f32 *trim, s32 bone, Matrix4 *mtx) {
    extern f32 g_fDeltaTimeSec;
    struct {
        Vector4 point;
        Vector4 normal;
        u8 unk20[0x30];
    } info __attribute__((aligned(16)));
    Vector4 pos __attribute__((aligned(16)));
    Vector4 target __attribute__((aligned(16)));
    Vector4 d __attribute__((aligned(16)));
    f32 t;
    f32 trimAmt;
    f32 travelled;
    f32 dist;
    f32 len;
    f32 dot;
    s8 hit;
    s8 trimmed;

    trimAmt = *trim;
    travelled = 0.0f;
    pos = *from;
    target = *to;
    dist = Sqrt(PSVECSquareDistance(from, to));
    trimmed = 0;
    PSVECSubtract(&target, &pos, &d);
    do {
        t = 1.0f - travelled;
        hit = RayHitsTerrainAndObjects_Camera(&pos, &d, &t, &info);
        if (t == 0.0f && !trimmed) {
            hit = 0;
            t += 0.2f;
        }
        pos.x += d.x * t;
        pos.y += d.y * t;
        pos.z += d.z * t;
        if (hit) {
            dot = -PSVECDotProduct(&info.normal, &d);
            if (dot < 0.0f) {
                return;
            }
            d.x += dot * info.normal.x;
            d.y += dot * info.normal.y;
            d.z += dot * info.normal.z;
            len = MagnitudeVector(&d);
            if (len < 0.01f) {
                trimmed = 1;
                break;
            }
            len = dist / len;
            trimmed = 1;
            d.x *= len;
            d.y *= len;
            d.z *= len;
            pos.x += info.normal.x * trimAmt;
            pos.y += info.normal.y * trimAmt;
            pos.z += info.normal.z * trimAmt;
        }
        travelled += t;
        if (t < 1e-6f) {
            break;
        }
    } while (travelled < 1.0f);
    *to = pos;
    if (trimmed) {
        *trim += g_fDeltaTimeSec;
        if (*trim > 1.8f) {
            *trim = 1.8f;
        }
    } else {
        *trim -= g_fDeltaTimeSec;
        if (*trim <= 0.0f) {
            *trim = 0.0f;
        }
    }
}

void Camera_UpdatePosition_Default(Camera *cam, f32 *mode) {
    extern f32 g_fDeltaTimeSec;
    extern s32 g_iDeltaTimeMS;
    CameraInfo info __attribute__((aligned(16)));
    Matrix4 sub1;
    Matrix4 sub2;
    Matrix4 mtx;
    Vector4 lookPt __attribute__((aligned(16)));
    Vector4 target;
    Vector4 up2;
    Vector4 up1;
    Vector4 dir __attribute__((aligned(16)));
    CarA *car;
    void *volume;
    f32 scale;
    f32 k;
    f32 mag;
    f32 speed;
    f32 inv;
    f32 s;
    f32 strength;
    f32 damp;
    f32 kx;
    f32 ky;
    f32 t;
    f32 sx;
    f32 sy;
    f32 sz;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 step;
    u32 i;
    s32 cell;
    u32 count;
    s8 inVolume;

    car = GETP(GETP(cam->player)->vehicle);
    scale = 1.0f;
    THING_VFN(car, THING_V_GET_CAMERA_ORIENTATION, void (*)(CarA *, u8, struct CameraInfo *))(car, GETP(cam->player)->cameraIndex, &info);
    target = info.target;
    if (info.lookAhead != 0.0f) {
        car = GETP(GETP(cam->player)->vehicle);
        if (car->base.axis[2].y < -0.4f && car->base.axis[1].y < 0.6f) {
            cam->flipTimer = 700;
        }
        up1 = info.mtx.v[1];
    } else {
        up1.x = 0.0f;
        up1.y = 1.0f;
        up1.z = 0.0f;
    }
    if (cam->flipTimer > 0) {
        cam->flipTimer -= g_iDeltaTimeMS;
        scale = 0.0f;
    }
    car = GETP(GETP(cam->player)->vehicle);
    k = 350.0f * scale * info.lookAhead + 0.1f;
    lookPt.x = car->base.axis[2].x * k + car->base.vel.x;
    lookPt.y = car->base.axis[2].y * k + car->base.vel.y;
    lookPt.z = car->base.axis[2].z * k + car->base.vel.z;
    Gfx_SetProjectionMatrix(mode[0]);
    mag = PSVECSquareMag(&lookPt);
    if (mag > 80.0f) {
        inv = 1.0f / Sqrt(mag);
        dir = lookPt;
        dir.x *= inv;
        dir.y *= inv;
        dir.z *= inv;
        cam->lookDir = dir;
    }
    inVolume = 0;
    cell = VolumeManager_GetVolumeCellIndex(&((CarA *)GETP(GETP(cam->player)->vehicle))->base.pos);
    count = VolumeManager_GetNumVolumesOfTypeByCell(3, cell);
    car = GETP(GETP(cam->player)->vehicle);
    if ((u8 *)GETP(car->base.vtable) != g_carAVTable || car->unk460 == 0) {
        for (i = 0; i < count; i++) {
            f32 dist = 0.0f;
            volume = PTR(VolumeManager_GetVolumeTypeByIndexByCell(3, i, cell));
            if (IsPointWithinVolume(&((CarA *)GETP(GETP(cam->player)->vehicle))->base.pos, volume, &dist)) {
                inVolume = 1;
            }
        }
    }
    if (inVolume) {
        if (info.lookAhead != 0.0f) {
            up2 = info.mtx.v[1];
        }
    } else {
        up2.x = 0.0f;
        up2.y = 1.0f;
        up2.z = 0.0f;
    }

    {
        Vector4 cross __attribute__((aligned(16)));
        PSVECCrossProduct(&cam->rollUp, &up2, &cross);
        mag = MagnitudeVector(&cross);
        if (mag > 1e-7f) {
            Vector4 quat __attribute__((aligned(16)));
            Vector4 force __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
            Vector4 axis __attribute__((aligned(16)));
            Vector4 xf __attribute__((aligned(16)));
            inv = 1.0f / mag;
            cross.x *= inv;
            cross.y *= inv;
            cross.z *= inv;
            strength = 13.978342f;
            if (PSVECDotProduct(&cam->rollUp, &info.mtx.v[1]) < 0.0f || PSVECDotProduct(&cam->rollUp, &up2) < 0.0f || mag > 0.31415927f) {
                force.x = cross.x * strength;
                force.y = cross.y * strength;
                force.z = cross.z * strength;
            } else {
                force.x = 0.01f * (cross.x * strength);
                force.y = 0.01f * (cross.y * strength);
                force.z = 0.01f * (cross.z * strength);
            }
            force.x -= 5.5f * cam->rollUpVel.x;
            force.y -= 5.5f * cam->rollUpVel.y;
            force.z -= 5.5f * cam->rollUpVel.z;
            cam->rollUpVel.x += force.x * g_fDeltaTimeSec;
            cam->rollUpVel.y += force.y * g_fDeltaTimeSec;
            cam->rollUpVel.z += force.z * g_fDeltaTimeSec;
            speed = MagnitudeVector(&cam->rollUpVel);
            inv = 1.0f / speed;
            if (speed > 1e-6f) {
                if (speed > 3.0f) {
                    s = 3.0f * inv;
                    cam->rollUpVel.x *= s;
                    cam->rollUpVel.y *= s;
                    cam->rollUpVel.z *= s;
                }
                axis = cam->rollUpVel;
                axis.x = cam->rollUpVel.x * inv;
                axis.y = cam->rollUpVel.y * inv;
                axis.z = cam->rollUpVel.z * inv;
                if (mag < 0.2f) {
                    speed *= mag / 0.2f;
                }
                GenerateQuaternion(&axis, &quat, speed * g_fDeltaTimeSec);
                QuaternionToSubMatrix(&quat, &sub1);
                XformVecOrientation(&sub1, &cam->rollUp, &xf);
                cam->rollUp = xf;
            }
        }
    }

    {
        Vector4 cross __attribute__((aligned(16)));
        PSVECCrossProduct(&cam->tiltUp, &up1, &cross);
        mag = MagnitudeVector(&cross);
        if (mag > 1e-7f) {
            Vector4 quat __attribute__((aligned(16)));
            Vector4 force __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
            Vector4 axis __attribute__((aligned(16)));
            Vector4 xf __attribute__((aligned(16)));
            inv = 1.0f / mag;
            cross.x *= inv;
            cross.y *= inv;
            cross.z *= inv;
            strength = 13.978342f;
            if (PSVECDotProduct(&cam->tiltUp, &up1) < 0.0f || mag > 0.31415927f) {
                force.x = cross.x * strength;
                force.y = cross.y * strength;
                force.z = cross.z * strength;
            } else {
                force.x = 0.01f * (cross.x * strength);
                force.y = 0.01f * (cross.y * strength);
                force.z = 0.01f * (cross.z * strength);
            }
            force.x -= 4.5f * cam->tiltUpVel.x;
            force.y -= 4.5f * cam->tiltUpVel.y;
            force.z -= 4.5f * cam->tiltUpVel.z;
            cam->tiltUpVel.x += force.x * g_fDeltaTimeSec;
            cam->tiltUpVel.y += force.y * g_fDeltaTimeSec;
            cam->tiltUpVel.z += force.z * g_fDeltaTimeSec;
            speed = MagnitudeVector(&cam->tiltUpVel);
            inv = 1.0f / speed;
            if (speed > 1e-6f) {
                if (speed > 5.0f) {
                    s = 5.0f * inv;
                    cam->tiltUpVel.x *= s;
                    cam->tiltUpVel.y *= s;
                    cam->tiltUpVel.z *= s;
                }
                axis = cam->tiltUpVel;
                axis.x = cam->tiltUpVel.x * inv;
                axis.y = cam->tiltUpVel.y * inv;
                axis.z = cam->tiltUpVel.z * inv;
                if (mag < 0.2f) {
                    speed *= mag / 0.2f;
                }
                GenerateQuaternion(&axis, &quat, speed * g_fDeltaTimeSec);
                QuaternionToSubMatrix(&quat, &sub2);
                XformVecOrientation(&sub2, &cam->tiltUp, &xf);
                cam->tiltUp = xf;
            }
        }
    }

    kx = 2.0f * mode[3];
    damp = -mode[4];
    step = 0.017f;
    t = g_fDeltaTimeSec;
    ky = kx * mode[6];
    while (t > step) {
        sx = kx * (cam->lookDir.x - cam->springPos.x);
        sy = ky * (cam->lookDir.y - cam->springPos.y);
        sz = kx * (cam->lookDir.z - cam->springPos.z);
        dx = damp * cam->springVel.x;
        dy = damp * cam->springVel.y;
        dz = damp * cam->springVel.z;
        cam->springPos.x += cam->springVel.x * step;
        cam->springPos.y += cam->springVel.y * step;
        cam->springPos.z += cam->springVel.z * step;
        cam->springVel.x += step * (sx + dx);
        cam->springVel.y += step * (sy + dy);
        cam->springVel.z += step * (sz + dz);
        t -= step;
    }
    sx = kx * (cam->lookDir.x - cam->springPos.x);
    sy = ky * (cam->lookDir.y - cam->springPos.y);
    sz = kx * (cam->lookDir.z - cam->springPos.z);
    dx = damp * cam->springVel.x;
    dy = damp * cam->springVel.y;
    dz = damp * cam->springVel.z;
    cam->springPos.x += cam->springVel.x * t;
    cam->springPos.y += cam->springVel.y * t;
    cam->springPos.z += cam->springVel.z * t;
    cam->springVel.x += t * (sx + dx);
    cam->springVel.y += t * (sy + dy);
    cam->springVel.z += t * (sz + dz);

    cam->dir = cam->springPos;
    cam->dir.x -= cam->tiltUp.x * mode[5];
    cam->dir.y -= cam->tiltUp.y * mode[5];
    cam->dir.z -= cam->tiltUp.z * mode[5];
    if (PSVECSquareMag(&cam->dir)) {
        PSVECNormalize(&cam->dir, &cam->dir);
    }
    cam->pos.x = cam->tiltUp.x * mode[2] + (target.x - mode[1] * cam->dir.x);
    cam->pos.y = cam->tiltUp.y * mode[2] + (target.y - mode[1] * cam->dir.y);
    cam->pos.z = cam->tiltUp.z * mode[2] + (target.z - mode[1] * cam->dir.z);
    car = GETP(GETP(cam->player)->vehicle);
    if ((u8 *)GETP(car->base.vtable) == g_carAVTable) {
        Vector4 toTarget __attribute__((aligned(16)));
        Vector4 blend __attribute__((aligned(16)));
        s32 box;
        mtx = *THING_MTX(&car->base);
        box = GETP(GETP(car->vehicleIface)->collider)->box.h;  /* passed on, never read */
        XformVec(THING_MTX(&car->base), &car->bodyOffset, &mtx.v[3]);
        GetTrimmedCameraPos2(&info.from, &cam->pos, &cam->trim, box, &mtx);
        PSVECSubtract(&target, &cam->pos, &toTarget);
        if (PSVECSquareMag(&toTarget)) {
            PSVECNormalize(&toTarget, &toTarget);
        }
        blend.x = toTarget.x * cam->trim + cam->dir.x * (1.0f - cam->trim);
        blend.y = toTarget.y * cam->trim + cam->dir.y * (1.0f - cam->trim);
        blend.z = toTarget.z * cam->trim + cam->dir.z * (1.0f - cam->trim);
        cam->dir = blend;
        if (PSVECSquareMag(&cam->dir)) {
            PSVECNormalize(&cam->dir, &cam->dir);
        }
    }
    PSVECCrossProduct(&cam->rollUp, &cam->dir, &cam->right);
    PSVECCrossProduct(&cam->dir, &cam->right, &cam->up);
    if (PSVECSquareMag(&cam->right)) {
        PSVECNormalize(&cam->right, &cam->right);
    }
    if (PSVECSquareMag(&cam->up)) {
        PSVECNormalize(&cam->up, &cam->up);
    }
}

/* Look at eye from target (shaken along right by `shake`), with the field
 * of view scaled by the zoom and distance. */
void Camera_UpdatePosition_Zoom_Generic(Camera *cam, CameraZoomParams *mode, Vector4 *target, Vector4 *eye, f32 zoom, f32 shake) {
    extern f32 g_fDeltaTimeSec;
    Vector4 pos __attribute__((aligned(16)));

    pos = *target;
    cam->modeTime += 0.2f * g_fDeltaTimeSec;
    pos.x = shake * (cam->right.x * (f32)sin(cam->modeTime)) + target->x;
    pos.y = shake * (cam->right.y * (f32)sin(cam->modeTime)) + target->y;
    pos.z = shake * (cam->right.z * (f32)sin(cam->modeTime)) + target->z;
    {
        Vector4 up __attribute__((aligned(16))) = {0.0f, 1.0f, 0.0f, 0.0f};
        Vector4 dir __attribute__((aligned(16)));
        f32 mag;
        f32 inv;
        f32 fov;

        PSVECSubtract(eye, &pos, &dir);
        mag = MagnitudeVector(&dir);
        inv = 1.0f / mag;
        fov = mode->zoom * (zoom * (zoom * (f32)atan2(40.0, mag)));
        cam->dir.x = dir.x * inv;
        cam->dir.y = dir.y * inv;
        cam->dir.z = dir.z * inv;
        PSVECCrossProduct(&up, &cam->dir, &cam->right);
        PSVECCrossProduct(&cam->dir, &cam->right, &cam->up);
        inv = 1.0f / MagnitudeVector(&cam->right);
        cam->right.x *= inv;
        cam->right.y *= inv;
        cam->right.z *= inv;
        inv = 1.0f / MagnitudeVector(&cam->up);
        cam->up.x *= inv;
        cam->up.y *= inv;
        cam->up.z *= inv;
        if (fov > 150.0f) {
            fov = 150.0f;
        }
        if (fov < 6.0f) {
            fov = 6.0f;
        }
        Gfx_SetProjectionMatrix(fov);
    }
    cam->pos = pos;
    PSVECSubtract(&cam->pos, &pos, &cam->vel);   /* sic: always zero */
}

static inline void Camera_Orbit(Camera *cam, Vector4 *center, Vector4 *target, f32 radius, f32 height, f32 speed, f32 fov) {
    extern f32 g_fDeltaTimeSec;
    Vector4 eye;
    Vector4 dir;
    Vector4 up = {0.0f, 1.0f, 0.0f, 0.0f};
    f32 s;
    f32 c;

    cam->modeTime += speed * g_fDeltaTimeSec;
    s = sin(cam->modeTime);
    c = cos(cam->modeTime);
    eye.x = radius * s + center->x;
    eye.y = center->y + height;
    eye.z = radius * c + center->z;
    PSVECSubtract(target, &eye, &dir);
    GetTrimmedCameraPos((u8 *)cam, target, &dir, &cam->trim, 1.0f);
    PSVECSubtract(target, &cam->pos, &cam->dir);
    if (PSVECSquareMag(&cam->dir)) {
        PSVECNormalize(&cam->dir, &cam->dir);
    }
    PSVECCrossProduct(&up, &cam->dir, &cam->right);
    PSVECCrossProduct(&cam->dir, &cam->right, &cam->up);
    if (PSVECSquareMag(&cam->right)) {
        PSVECNormalize(&cam->right, &cam->right);
    }
    if (PSVECSquareMag(&cam->up)) {
        PSVECNormalize(&cam->up, &cam->up);
    }
    Gfx_SetProjectionMatrix(fov);
}

static inline void Camera_LookAt(Camera *cam, Vector4 *pos, Vector4 *target) {
    Vector4 up = {0.0f, 1.0f, 0.0f, 0.0f};

    cam->pos = *pos;
    PSVECSubtract(target, &cam->pos, &cam->dir);
    if (PSVECSquareMag(&cam->dir)) {
        PSVECNormalize(&cam->dir, &cam->dir);
    }
    PSVECCrossProduct(&up, &cam->dir, &cam->right);
    PSVECCrossProduct(&cam->dir, &cam->right, &cam->up);
    if (PSVECSquareMag(&cam->right)) {
        PSVECNormalize(&cam->right, &cam->right);
    }
    if (PSVECSquareMag(&cam->up)) {
        PSVECNormalize(&cam->up, &cam->up);
    }
}

static inline void Camera_ZoomOnVehicle(Camera *cam, u8 *params) {
    Vector4 target;
    Vector4 delta;
    f32 t;
    Vector4 *nav;

    THING_VFN(GETP(GETP(cam->player)->vehicle), THING_V_GET_TARGET_POS, void (*)(void *, Vector4 *))(GETP(GETP(cam->player)->vehicle), &target);
    PSVECSubtract(&target, &cam->pos, &delta);
    t = 1.0f;
    if (RayHitsTerrainAndObjects(&cam->pos, &delta, &t, NULL)) {
        nav = NavHolder_GetNavPoint(NavHolder_FindIndexOfNearestNavPoint(&target, -1));
        if (nav != NULL) {
            cam->pos = *nav;
        }
    }
    Camera_UpdatePosition_Zoom_Generic((u8 *)cam, params, &cam->pos, &target, ((CarA *)GETP(GETP(cam->player)->vehicle))->base.radius, 0.0f);
}

static inline void Camera_OrbitWithParams(Camera *cam, Vector4 *center, CameraOrbitParams *params) {
    extern f32 g_fDeltaTimeSec;
    Vector4 eye;
    Vector4 dir;
    Vector4 up = {0.0f, 1.0f, 0.0f, 0.0f};
    f32 s;
    f32 c;

    cam->modeTime += params->speed * g_fDeltaTimeSec;
    s = sin(cam->modeTime);
    c = cos(cam->modeTime);
    eye.x = params->radius * s + center->x;
    eye.y = center->y + params->height;
    eye.z = params->radius * c + center->z;
    PSVECSubtract(center, &eye, &dir);
    GetTrimmedCameraPos((u8 *)cam, center, &dir, &cam->trim, 1.0f);
    PSVECSubtract(center, &cam->pos, &cam->dir);
    if (PSVECSquareMag(&cam->dir)) {
        PSVECNormalize(&cam->dir, &cam->dir);
    }
    PSVECCrossProduct(&up, &cam->dir, &cam->right);
    PSVECCrossProduct(&cam->dir, &cam->right, &cam->up);
    if (PSVECSquareMag(&cam->right)) {
        PSVECNormalize(&cam->right, &cam->right);
    }
    if (PSVECSquareMag(&cam->up)) {
        PSVECNormalize(&cam->up, &cam->up);
    }
    Gfx_SetProjectionMatrix(params->fov);
}

static inline void Camera_OrbitVehicle(Camera *cam, CameraOrbitParams *params) {
    extern f32 g_fDeltaTimeSec;
    Vector4 targetPos;
    Vector4 *center;
    f32 s;
    f32 c;

    cam->modeTime += params->speed * g_fDeltaTimeSec;
    SinCosRad(&s, &c, cam->modeTime);
    if (GETP(GETP(cam->player)->vehicle) != NULL) {
        THING_VFN(GETP(GETP(cam->player)->vehicle), THING_V_GET_TARGET_POS, void (*)(void *, Vector4 *))(GETP(GETP(cam->player)->vehicle), &targetPos);
        center = &targetPos;
    } else {
        center = &cam->mtx.v[3];
    }
    Camera_OrbitWithParams(cam, center, params);
}
extern u8 g_pNoRayCollideThing[];  /* typed per function below */

/* A camera trigger's location: data[0] is the kind (5 the camera itself, 6
 * a point, 4 a mover's (TilePath) transform, 3 a vector, 2 a placed object
 * (QuadInstance) or its spawn point, 1 a car (TileCar), which the camera
 * then follows), then the operand and an offset. Returns the words used. */
s32 Camera_TriggerActionGetLocation(Camera *cam, u32 *data, Vector4 *out) {
    #define g_pNoRayCollideThing (*(u8 **)(void *)&g_pNoRayCollideThing)
    #define g_triggerAction_params_default (*(s32 *)(void *)&g_triggerAction_params_default)
    Vector4 offset __attribute__((aligned(16)));
    Vector4 local __attribute__((aligned(16)));
    Thing *thing;
    s32 i = 0;

    switch (data[i++]) {
    case 5:
        *out = cam->pos;
        break;
    case 6:
        *out = *(Vector4 *)PTR(data[i++]);
        out->x += *(f32 *)&data[i++];
        out->y += *(f32 *)&data[i++];
        out->z += *(f32 *)&data[i++];
        break;
    case 4:
        thing = GETP(((TilePath *)PTR(data[i++]))->thing);
        offset.x = *(f32 *)&data[i++];
        offset.y = *(f32 *)&data[i++];
        offset.z = *(f32 *)&data[i++];
        XformVec(THING_MTX(thing), &offset, out);
        break;
    case 3:
        out->x = ((Vector4 *)PTR(data[i]))->x;
        out->y = ((Vector4 *)PTR(data[i]))->y;
        out->z = ((Vector4 *)PTR(data[i++]))->z;
        out->x += *(f32 *)&data[i++];
        out->y += *(f32 *)&data[i++];
        out->z += *(f32 *)&data[i++];
        break;
    case 2:
        thing = GETP(((QuadInstance *)PTR(data[i++]))->thing);
        if (thing == NULL) {
            *out = ((QuadInstance *)PTR(data[i - 1]))->pos;
        } else {
            *out = thing->pos;
        }
        out->x += *(f32 *)&data[i++];
        out->y += *(f32 *)&data[i++];
        out->z += *(f32 *)&data[i++];
        break;
    case 1:
        thing = (Thing *)GETP(((TileCar *)PTR(data[i++]))->car);
        local.x = *(f32 *)&data[i++];
        local.y = *(f32 *)&data[i++];
        local.z = *(f32 *)&data[i++];
        g_pNoRayCollideThing = (u8 *)thing;
        if (GETP(thing->player) != NULL) {
            ((Player *)GETP(thing->player))->flags |= 0x80;
        }
        if (thing->flags & 0x201000) {
            /* gone: back to the default mode */
            SETP(cam->mode, &g_triggerAction_params_default);
        }
        if (thing != NULL) {
            XformVec(THING_MTX(thing), &local, out);
        }
        break;
    }
    return i;
#undef g_pNoRayCollideThing
#undef g_triggerAction_params_default
}


void Camera_UpdatePosition(Camera *cam) {
    #define g_pNoRayCollideThing (*(CarA **)(void *)&g_pNoRayCollideThing)
    extern Matrix4 g_identity;
    extern Vector4 g_zeroVector;
    extern u8 g_lookerVTable[];
    #define g_triggerAction_params_default (*(u32 *)(void *)&g_triggerAction_params_default)
    Vector4 oldPos __attribute__((aligned(16)));
    CameraZoomParams zoom __attribute__((aligned(16)));
    CameraInfo info;
    s32 i;
    u32 *mode;

    oldPos = cam->pos;
    SETP(info.subMode, NULL);
    info.fov = 60.0f;
    info.mtx = g_identity;
    if (GETP(cam->player) != NULL) {
        GETP(cam->player)->flags &= ~0x80;
    }
    switch (*(mode = GETP(cam->mode))) {
    case 30:
        if (GETP(cam->player) != NULL) {
            if (GETP(GETP(cam->player)->vehicle) != NULL) {
                g_pNoRayCollideThing = GETP(GETP(cam->player)->vehicle);
                if (!(((CarA *)GETP(GETP(cam->player)->vehicle))->base.flags & 0x201000)) {
                    THING_VFN(GETP(GETP(cam->player)->vehicle), THING_V_GET_CAMERA_ORIENTATION, void (*)(void *, u8, struct CameraInfo *))(GETP(GETP(cam->player)->vehicle), GETP(cam->player)->cameraIndex, &info);
                    if (GETP(cam->player)->cameraIndex > info.cameraCount) {
                        GETP(cam->player)->cameraIndex -= (u8)info.cameraCount;
                    }
                    cam->mtx = *THING_MTX(&((CarA *)GETP(GETP(cam->player)->vehicle))->base);
                } else {
                    info.mtx = cam->mtx;
                }
            } else {
                Camera_Orbit(cam, &cam->mtx.v[3], &cam->mtx.v[3], 15.0f, 5.0f, 0.3f, 60.0f);
                return;
            }
        }
        if (GETP(info.subMode) == NULL) {
            g_pNoRayCollideThing = NULL;
            cam->right = info.mtx.v[0];
            cam->up = info.mtx.v[1];
            cam->dir = info.mtx.v[2];
            cam->pos = info.mtx.v[3];
            Gfx_SetProjectionMatrix(info.fov);
            if (GETP(cam->cameraFx) != NULL && GETP(cam->player) != NULL && GETP(GETP(cam->player)->vehicle) != NULL) {
                f32 scale = GETP(((World *)GETP(g_worldData.world))->gameData)->cameraFxLead;
                GETP(cam->cameraFx)->pos.x = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel.x * scale + cam->pos.x;
                GETP(cam->cameraFx)->pos.y = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel.y * scale + cam->pos.y;
                GETP(cam->cameraFx)->pos.z = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel.z * scale + cam->pos.z;
            }
            return;
        }
        switch (GETP(info.subMode)->type) {
        case 0:
            Camera_UpdatePosition_Default(cam, GETP(GETP(info.subMode)->params));
            cam->vel = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel;
            break;
        case 2:
            Camera_ZoomOnVehicle(cam, GETP(GETP(info.subMode)->params));
            PSVECSubtract(&cam->pos, &oldPos, &cam->vel);
            break;
        case 3:
            Camera_OrbitVehicle(cam, GETP(GETP(info.subMode)->params));
            cam->vel = g_zeroVector;
            break;
        case 4: {
            CameraBoneParams *params = GETP(GETP(info.subMode)->params);
            CarA *car = GETP(GETP(cam->player)->vehicle);

            if ((u8 *)GETP(car->base.vtable) == g_carAVTable) {
                cam->right = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 0];
                cam->up = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 1];
                cam->dir = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 2];
                cam->pos = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 3];
                XformVec(&GETP(car->motionState.animMatrices)[params->bone], &params->offset, &cam->pos);
            } else if ((u8 *)GETP(car->base.vtable) == g_lookerVTable) {
                car = GETP(car->unkBC);
                if (GETP(car->motionState.animMatrices) != NULL) {
                    cam->right = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 0];
                    cam->up = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 1];
                    cam->dir = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 2];
                    cam->pos = ((Vector4 *)GETP(car->motionState.animMatrices))[params->bone * 4 + 3];
                    XformVec(&GETP(car->motionState.animMatrices)[params->bone], &params->offset, &cam->pos);
                }
            }
            Gfx_SetProjectionMatrix(60.0f);
            cam->vel = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel;
            break;
        }
        }
        break;
    case 27: {
        Vector4 target __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
        Vector4 pos __attribute__((aligned(16))) = {1.0f, 1.0f, 1.0f, 0.0f};
        f32 fov;

        i = 1;
        fov = *(f32 *)&mode[i++];
        i += Camera_TriggerActionGetLocation((u8 *)cam, &mode[i], &pos);
        Camera_TriggerActionGetLocation((u8 *)cam, &mode[i], &target);
        Camera_LookAt(cam, &pos, &target);
        Gfx_SetProjectionMatrix(fov);
        break;
    }
    case 29: {
        Vector4 target __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
        Vector4 pos __attribute__((aligned(16))) = {1.0f, 1.0f, 1.0f, 0.0f};

        zoom.zoom = 0.014f;
        i = 1;
        i += Camera_TriggerActionGetLocation((u8 *)cam, &mode[i], &pos);
        Camera_TriggerActionGetLocation((u8 *)cam, &mode[i], &target);
        Camera_UpdatePosition_Zoom_Generic((u8 *)cam, (u8 *)&zoom, &pos, &target, 10.0f, 0.0f);
        break;
    }
    case 28: {
        Vector4 target __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
        Vector4 center __attribute__((aligned(16))) = {1.0f, 1.0f, 1.0f, 0.0f};
        f32 fov;
        f32 radius;
        f32 height;
        f32 speed;
        CarA *car;

        i = 1;
        fov = *(f32 *)&mode[i++];
        radius = *(f32 *)&mode[i++];
        height = *(f32 *)&mode[i++];
        speed = *(f32 *)&mode[i++];
        i += Camera_TriggerActionGetLocation((u8 *)cam, &mode[i], &center);
        Camera_TriggerActionGetLocation((u8 *)cam, &mode[i], &target);
        Camera_Orbit(cam, &center, &target, radius, height, speed, fov);
        car = GETP(GETP(cam->player)->vehicle);
        if (car != NULL && (u8 *)GETP(car->base.vtable) == g_carAVTable && (car->stuckCounter > 0 || car->unk460 <= 0) &&
            car->base.axis[1].y < 0.5f) {
            SETP(cam->mode, &g_triggerAction_params_default);
        }
        break;
    }
    }
    g_pNoRayCollideThing = NULL;
    if (GETP(cam->cameraFx) != NULL && GETP(cam->player) != NULL && GETP(GETP(cam->player)->vehicle) != NULL) {
        f32 scale = GETP(((World *)GETP(g_worldData.world))->gameData)->cameraFxLead;
        GETP(cam->cameraFx)->pos.x = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel.x * scale + cam->pos.x;
        GETP(cam->cameraFx)->pos.y = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel.y * scale + cam->pos.y;
        GETP(cam->cameraFx)->pos.z = ((CarA *)GETP(GETP(cam->player)->vehicle))->base.vel.z * scale + cam->pos.z;
    }
#undef g_pNoRayCollideThing
#undef g_triggerAction_params_default
}

void Camera_SetPlayer(Camera *camera, Player *player) {
    u32 i;
    Camera *cam;
    Player *p;

    SETP(camera->player, player);
    PlayerManager_ClearAllCameraFlags_PRIVATE();
    cam = CAMERA_MANAGER->cameras;
    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        p = GETP(cam->player);
        if (p != NULL) {
            p->flags |= 0x10;
        }
        cam++;
    }
    Player_RefreshInterface(player);
}

void CameraManager_ReturnAllToPlayer(void) {
    #define g_triggerAction_params_default (*(u32 *)(void *)&g_triggerAction_params_default)
    u32 i;

    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        SETP(CAMERA_MANAGER->cameras[i].mode, &g_triggerAction_params_default);
    }
#undef g_triggerAction_params_default
}

void CameraManager_SetCameraGlobalMode(u32 *mode) {
   
    u32 i;

    if (mode[0] >= 1 && mode[0] <= 4) {
        SETP(CAMERA_MANAGER->cameras[mode[0] - 1].mode, mode + 1);
        CAMERA_MANAGER->cameras[mode[0] - 1].modeTime = 0.0f;
        return;
    }
    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        SETP(CAMERA_MANAGER->cameras[i].mode, mode + (mode[0] == 0));
        CAMERA_MANAGER->cameras[i].modeTime = 0.0f;
    }
}

void *CameraManager_GetCameraFromPlayer(void *player) {
    u32 i;

    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        if (GETP(CAMERA_MANAGER->cameras[i].player) == player) {
            return &CAMERA_MANAGER->cameras[i];
        }
    }
    return NULL;
}

void CameraManager_ReleaseAllEmitters(void) {
    Camera *cam;
    u32 i;

    cam = CAMERA_MANAGER->cameras;
    for (i = 0; i < CAMERA_MANAGER->count; i++) {
        if (GETP(cam->cameraFx) != NULL) {
            THING_VFN(GETP(cam->cameraFx), THING_V_RELEASE, void (*)(void *))(GETP(cam->cameraFx));
            SETP(cam->cameraFx, NULL);
        }
        cam++;
    }
    ThingManagerList_RemoveReleasedThings(&g_thingManager_DARYL.activeList);
}

