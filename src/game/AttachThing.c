/* Attach things: billboard sprites, coronas and small spinning models hung
 * on other things as slaves (lights, glows); and radar blips, the markers
 * with a health bar drawn over cars. */
#include "common.h"
#include "AttachThing.h"
#include "Particle.h"

typedef struct GDLRectVtx {
    f32 x;
    f32 y;
    f32 z;
    u8 unkC[0x10 - 0xC];
    f32 s;
    f32 t;
    GXColor color;
    u8 unk1C[0x20 - 0x1C];
} GDLRectVtx;

typedef struct GDLRect {
    GDLRectVtx v[4];
    u16 tex;
    u8 blendMode;
    u8 window;
} GDLRect;

GDLRect *Gfx_Add2DRectangleRenderWindow(void);

extern u32 g_attachVTable[];
extern u32 g_attachGeometryVTable[];

/* Set up from AttachParams: key tracks, cone, spin, and the vtable (plain
 * or geometry). Keys of at most one entry each never change (flag 0x4000). */
s32 AttachThing_Init(struct AttachThing *attach, struct AttachParams *data)
{
    Thing *thing = &attach->base;
    AttachInfo *info;

    Thing_Init(thing, (s32)HND(data));
    thing->player.h = data->player.h;
    thing->pos = data->pos;
    SETP(attach->info, GETP(data->info));
    attach->scale2.reset = 1;
    attach->scale.reset = 1;
    attach->color.reset = 1;
    info = GETP(attach->info);
    if (GETP(info->spin)) {
        attach->spin = RandomAdd(GETP(info->spin)->speedBase, GETP(info->spin)->speedRange);
    }
    if (info->colorKeys.count <= 1 && info->scaleKeys.count <= 1 && info->scale2Keys.count <= 1) {
        thing->flags |= 0x4000;
    }
    if (info->directional == 1) {
        attach->minDot = (f32)cos(0.01745329238474369f * GETP(info->cone)->angle);
    }
    ExtractColorKey(&info->colorKeys, &attach->color, 0.0f);
    ExtractScaleKey(&info->scaleKeys, &attach->scale, 0.0f);
    ExtractScaleKey(&info->scale2Keys, &attach->scale2, 0.0f);
    thing->flags |= 0x400000;
    thing->flags |= 2;
    attach->timer = 100;
    attach->fade = 1.0f;
    thing->vel.x = 0.0f;
    thing->vel.y = 0.0f;
    thing->vel.z = 1.0f;
    if (data->geometry != 0)
    {
        SETP(thing->vtable, g_attachGeometryVTable);
    }
    else
    {
        SETP(thing->vtable, g_attachVTable);
    }
    return 1;
}

/* Play the key tracks (unless they never change). */
s32 AttachThing_Move(struct AttachThing *attach) {
    extern f32 g_fDeltaTimeMS;
    AttachInfo *info = GETP(attach->info);

    if (!(attach->base.flags & 0x4000)) {
        ExtractColorKey(&info->colorKeys, &attach->color, g_fDeltaTimeMS);
        ExtractScaleKey(&info->scaleKeys, &attach->scale, g_fDeltaTimeMS);
        ExtractScaleKey(&info->scale2Keys, &attach->scale2, g_fDeltaTimeMS);
    }
    return 1;
}

/* A corona: faded with distance (range) and the cone, hidden while a ray
 * from the camera is blocked (checked every 100 ms; flag 0x20). Single
 * view only: released in split screen. */
void AttachThing_DrawCorona(struct AttachThing *attach) {
    extern s32 g_iDeltaTimeMS;
    Vector4 screen __attribute__((aligned(16)));
    Vector4 camera __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 dist;
    GXColor scaled;
    GXColor color;
    f32 farDist;
    f32 nearDist;
    f32 depth;
    f32 fade;
    Thing *thing = &attach->base;

    if (ViewManager_GetViewFormat() > 1) {
        Thing_Release(attach);
        return;
    }
    farDist = GETP(GETP(attach->info)->range)[1];
    nearDist = GETP(GETP(attach->info)->range)[0];
    Gfx_TransformVector(&thing->pos, &screen);
    depth = screen.w;
    if (!(depth < 0.0f) && !(depth > farDist)) {
        attach->timer -= g_iDeltaTimeMS;
        if (attach->timer < 0) {
            dist = 1.0f;
            Gfx_GetCameraPosition(&camera);
            PSVECSubtract(&thing->pos, &camera, &dir);
            attach->timer = 100;
            if (RayHitsTerrainAndObjects(&camera, &dir, &dist, NULL)) {
                thing->flags |= 0x20;
                return;
            }
            thing->flags &= ~0x20;
        }
        if (!(thing->flags & 0x20)) {
            if (depth > nearDist) {
                fade = attach->fade * (1.0f - (depth - nearDist) / (farDist - nearDist));
            } else {
                fade = attach->fade;
            }
            scaled.r = attach->color.color.r * fade;
            scaled.g = attach->color.color.g * fade;
            scaled.b = attach->color.color.b * fade;
            scaled.a = attach->color.color.a;
            color = scaled;
            /* port: GC passes ints and floats in separate registers, so the decomp's
             * (scaleX, scaleY, blend) order reaches (flag, a, b) as (blend, scaleX, scaleY) */
            Gfx_AddCorona(&screen, GETP(attach->info)->visual, &color, GETP(attach->info)->blend,
                          attach->scale.v.x, attach->scale2.v.x);
        }
    }
}

/* Billboard sprite or corona, unless hidden by the cone (flag 0x10). */
void AttachThing_Draw(struct AttachThing *attach) {
    FxBillboard *sprite;
    AttachInfo *info;

    if (!(attach->base.flags & 0x10)) {
        Gfx_SetWorldMatrix(0);
        info = GETP(attach->info);
        switch (info->type) {
        case 0:
            sprite = Gfx_AddBillBoardSprite((u16)info->visual);
            sprite->blend = GETP(attach->info)->blend;
            sprite->color = attach->color.color;
            sprite->rot = attach->scale2.v.x;
            sprite->size = attach->scale.v.x;
            sprite->x = attach->base.pos.x;
            sprite->y = attach->base.pos.y;
            sprite->z = attach->base.pos.z;
            return;
        case 1:
            AttachThing_DrawCorona(attach);
            break;
        }
    }
}

/* Attach geometry: the model at the thing's orientation, scale and position. */
void AttachGeometry_Draw(struct AttachThing *attach) {
    AttachInfo *info;
    void *geom;
    GXColor color;

    info = GETP(attach->info);
    geom = PTR(info->visual);
    if (info->colorKeys.count != 0) {
        color = attach->color.color;
        Gfx_SetBaseColor(&color);
        Gfx_EnableState(0x10);
    }
    attach->scale.v.w = 1.0f;
    Gfx_DrawGeometry_QuaternionScalePos(HND(geom), HND(&attach->base.quat), HND(&attach->scale.v), HND(&attach->base.pos));
    Gfx_DisableState(0x10);
}

/* Attach geometry: colour and scale keys, and the spin. */
s32 AttachGeometry_Move(struct AttachThing *attach) {
    extern f32 g_fDeltaTimeMS;
    extern f32 g_fDeltaTimeSec;
    Quaternion rot;
    Quaternion out;
    AttachInfo *info = GETP(attach->info);

    ExtractColorKey(&info->colorKeys, &attach->color, g_fDeltaTimeMS);
    ExtractScaleKey(&info->scaleKeys, &attach->scale, g_fDeltaTimeMS);
    GenerateQuaternion(&GETP(GETP(attach->info)->spin)->axis, &rot, 0.01745329238474369f * attach->spin * g_fDeltaTimeSec);
    QuaternionMult(&rot, &attach->base.quat, &out);
    attach->base.quat = out;
    return 1;
}

/* Follow the master (mtx); a directional one is hidden (flag 0x10) when
 * the camera is outside its cone, and faded towards the cone's edge. */
void AttachThing_SlaveTransform(struct AttachThing *attach, Matrix4 *mtx) {
    Vector4 pos __attribute__((aligned(16)));
    Vector4 camera __attribute__((aligned(16)));
    Vector4 toCamera __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 dot;
    f32 minDot;
    Thing *thing = &attach->base;

    pos = thing->pos;
    XformVec(mtx, &pos, &thing->pos);
    if (GETP(attach->info)->directional == 1) {
        thing->flags &= ~0x10;
        Gfx_GetCameraPosition(&camera);
        PSVECSubtract(&thing->pos, &camera, &toCamera);
        if (PSVECSquareMag(&toCamera)) {
            PSVECNormalize(&toCamera, &toCamera);
        }
        XformVecOrientation(mtx, &GETP(GETP(attach->info)->cone)->dir, &dir);
        dot = -PSVECDotProduct(&dir, &toCamera);
        minDot = attach->minDot;
        if (dot < minDot) {
            thing->flags |= 0x10;
        } else {
            attach->fade = (dot - minDot) / (1.0f - minDot);
        }
    }
}

/* Whether its info lies in [start, end) (a data pack being unloaded). */
s32 AttachThing_Invalidate(struct AttachThing *attach, u32 start, u32 end) {
    u32 info;

    info = attach->info.h;
    if ((info >= start) && (info < end)) {
        return 1;
    }
    return 0;
}

s32 RadarBlip_Init(struct RadarBlip *blip, struct RadarBlipParams *init) {
    Thing_Init(&blip->base, 0);
    blip->base.pos = init->pos;
    SETP(blip->data, GETP(init->data));
    blip->unkB4 = 0;
    blip->health = 1.0f;
    if (GETP(blip->data)->showSprite == 0) {
        blip->base.flags |= 0x200;
    }
    return 1;
}

s32 RadarBlip_Move(struct RadarBlip *blip)
{
    blip->lastPos = blip->base.pos;
    return 1;
}

/* The blip sprite and health bar over the master, in each view. In a
 * single view only for computer players (all with code variable 0x198),
 * and only in game state 5. */
void RadarBlip_Draw(struct RadarBlip *blip) {
    extern u8 g_CurrentViews;
    extern u32 g_pauseState;
    Vector4 pos;
    Vector4 screen __attribute__((aligned(16)));
    Vector4 spritePos __attribute__((aligned(16)));
    Vector4 barPos __attribute__((aligned(16)));
    GXColor color;
    GDLRect *rect;
    u32 single;
    u32 i;
    f32 width;
    f32 barWidth;
    void *player = GETP(blip->base.player);

    single = 0;
    pos = blip->base.pos;
    if (g_CurrentViews == 1) {
        if (!((player == NULL || (!Player_IsHuman(player) && !GameSettings_GetCodeVariable(0x198, 0))) && StateManager_CurrState() == 5)) {
            return;
        }
    }
    if (g_CurrentViews == 1) {
        single = 1;
        pos.y += 6.0f;
    } else {
        pos.y += 3.0f;
    }
    for (i = 0; i < (single ? 1 : 5); i++) {
        if (!(g_CurrentViews & (1 << i))) {
            continue;
        }
        Gfx_SetRenderWindow(i);
        Gfx_SetWorldMatrix(NULL);
        Gfx_TransformVector(&pos, &screen);
        if (screen.w > 10.0f) {
            color = *GETP(GETP(blip->data)->color);
            if (!isInWindow(&screen)) {
                continue;
            }
            if (screen.w < 250.0f) {
                color.a = (screen.w - 50.0f) / 200.0f * color.a;
            }
            spritePos = screen;
            spritePos.y -= GETP(blip->data)->height;
            if (GETP(blip->data)->showSprite && screen.w > 50.0f) {
                Gfx_Add2DSprite(&spritePos, GETP(blip->data)->texture, &color, 5, GETP(blip->data)->size, 3.1415927f);
            }
            barPos = screen;
            barPos.y -= 12.0f;
            barPos.x -= 12.0f;
            if (GETP(blip->data)->showBar && !g_pauseState) {
                barWidth = 24.0f;
                width = barWidth * blip->health;
                if (width > barWidth) {
                    width = barWidth;
                } else if (width < 1.0f) {
                    width = 0.0f;
                }
                {
                    GXColor green = {0x00, 0xFF, 0x00, 0x00};
                    green.a = color.a;
                    rect = Gfx_Add2DRectangleRenderWindow();
                    rect->v[0].x = barPos.x + width;
                    rect->v[0].y = barPos.y - 6.0f;
                    rect->v[0].z = barPos.z;
                    rect->v[1].x = barPos.x;
                    rect->v[1].y = barPos.y - 6.0f;
                    rect->v[1].z = barPos.z;
                    rect->v[3].x = barPos.x + width;
                    rect->v[3].y = barPos.y - 11.0f;
                    rect->v[3].z = barPos.z;
                    rect->v[2].x = barPos.x;
                    rect->v[2].y = barPos.y - 11.0f;
                    rect->v[2].z = barPos.z;
                    rect->v[0].t = 0.0f;
                    rect->v[0].s = 0.0f;
                    rect->v[3].t = 1.0f;
                    rect->v[1].s = 1.0f;
                    rect->v[2].t = 1.0f;
                    rect->v[2].s = 1.0f;
                    rect->v[1].t = 0.0f;
                    rect->v[3].s = 0.0f;
                    rect->v[0].color = rect->v[1].color = rect->v[2].color = rect->v[3].color = green;
                    rect->blendMode = 0;
                    rect->tex = 0xFFFF;
                }
                {
                    GXColor red = {0xFF, 0x00, 0x00, 0xFF};
                    rect = Gfx_Add2DRectangleRenderWindow();
                    rect->v[0].x = barPos.x + width;
                    rect->v[0].y = barPos.y - 6.0f;
                    rect->v[0].z = barPos.z;
                    rect->v[1].x = barPos.x + barWidth;
                    rect->v[1].y = barPos.y - 6.0f;
                    rect->v[1].z = barPos.z;
                    rect->v[3].x = barPos.x + width;
                    rect->v[3].y = barPos.y - 11.0f;
                    rect->v[3].z = barPos.z;
                    rect->v[2].x = barPos.x + barWidth;
                    rect->v[2].y = barPos.y - 11.0f;
                    rect->v[2].z = barPos.z;
                    rect->v[0].t = 0.0f;
                    rect->v[0].s = 0.0f;
                    rect->v[3].t = 1.0f;
                    rect->v[1].s = 1.0f;
                    rect->v[2].t = 1.0f;
                    rect->v[2].s = 1.0f;
                    rect->v[1].t = 0.0f;
                    rect->v[3].s = 0.0f;
                    rect->v[0].color = rect->v[1].color = rect->v[2].color = rect->v[3].color = red;
                    rect->blendMode = 0;
                    rect->tex = 0xFFFF;
                }
            }
        }
    }
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_attachVTable[17];

__attribute__((constructor)) static void PortInit_g_attachVTable(void) {
    static void *const fns[] = {
        (void *)AttachThing_Init,
        (void *)Thing_Release,
        (void *)Thing_PreMove,
        (void *)AttachThing_Move,
        (void *)AttachThing_Draw,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)AttachThing_SlaveTransform,
        (void *)Thing_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Thing_DynamicRotation,
        (void *)Thing_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Thing_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_attachVTable[i] = HND(fns[i]);
    }
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_attachGeometryVTable[17];

__attribute__((constructor)) static void PortInit_g_attachGeometryVTable(void) {
    static void *const fns[] = {
        (void *)AttachThing_Init,
        (void *)Thing_Release,
        (void *)Thing_PreMove,
        (void *)AttachGeometry_Move,
        (void *)AttachGeometry_Draw,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransformQuaternionOnly,
        (void *)Thing_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Thing_DynamicRotation,
        (void *)Thing_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Thing_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_attachGeometryVTable[i] = HND(fns[i]);
    }
}

/* port: vtables hold 32-bit handles, like every pointer slot the game reads
 * (the callers load entries with SLOT_LOAD); filled in at startup. */
u32 g_radarBlipVTable[17];

__attribute__((constructor)) static void PortInit_g_radarBlipVTable(void) {
    static void *const fns[] = {
        (void *)RadarBlip_Init,
        (void *)Thing_Release,
        (void *)Thing_PreMove,
        (void *)RadarBlip_Move,
        (void *)RadarBlip_Draw,
        (void *)Thing_GetMass,
        (void *)Thing_DynamicCollideImpact,
        (void *)Thing_GetCameraOrientationInfo,
        (void *)Thing_GetInterfaceInfo,
        (void *)Thing_SlaveTransformQuaternionOnly,
        (void *)Thing_GetSurfaceScript,
        (void *)Thing_GetNearestNavPoint,
        (void *)Thing_GetInverseLocalInertiaTensor,
        (void *)Thing_DynamicRotation,
        (void *)Thing_CollideWithRay,
        (void *)Thing_GetTargetPos,
        (void *)Thing_GetOrientedCollision,
    };
    u32 i;

    for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        g_radarBlipVTable[i] = HND(fns[i]);
    }
}
