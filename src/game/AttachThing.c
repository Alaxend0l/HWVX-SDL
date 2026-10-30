#include "common.h"

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

typedef struct RadarBlipData {
    u8 unk0[2];
    u8 showBar;
    u8 showSprite;
    f32 size;
    f32 height;
    u8 unkC[0x18 - 0xC];
    u32 texture;
    u8 unk1C[0x20 - 0x1C];
    PTR32(GXColor) color;
} RadarBlipData;

typedef struct RadarBlip {
    u8 unk0[0x40];
    Vector4 pos;
    u8 unk50[0x94 - 0x50];
    PTR32(void) player;
    u8 unk98[0xB0 - 0x98];
    PTR32(RadarBlipData) data;
    s32 unkB4;
    f32 health;
} RadarBlip;

extern u32 g_attachVTable[];
extern u32 g_attachGeometryVTable[];

s32 AttachThing_Init(void *attach, void *data)
{
    #define g_attachGeometryVTable ((u8 *)(void *)&g_attachGeometryVTable)
    #define g_attachVTable ((u8 *)(void *)&g_attachVTable)
    typedef struct { u32 x, y, z, w; } Vec4;

    Thing_Init(attach, (s32)HND(data));
    M2C_FIELD(attach, u32 *, 0x94) = M2C_FIELD(data, u32 *, 0x24);
    *(Vec4 *)((u8 *)attach + 0x40) = *(Vec4 *)((u8 *)data + 0x10);
    SLOT_STORE((u8 *)(attach) + 0xB4, SLOT_LOAD(void *, (u8 *)(data) + 0));
    M2C_FIELD(attach, u8 *, 0x124) = 1;
    M2C_FIELD(attach, u8 *, 0xF4) = 1;
    M2C_FIELD(attach, u8 *, 0x15C) = 1;
    if (SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4)) + 0x2C))
    {
        M2C_FIELD(attach, f32 *, 0xC0) = RandomAdd(*(f32 *)((u8 *)SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4)) + 0x2C) + 0x10),
                                                   *(f32 *)((u8 *)SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4)) + 0x2C) + 0x14));
    }
    if ((M2C_FIELD(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4), u32 *, 0x1C) <= 1) &&
        (M2C_FIELD(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4), u32 *, 0x14) <= 1) &&
        (M2C_FIELD(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4), u32 *, 0xC) <= 1))
    {
        M2C_FIELD(attach, u32 *, 0x74) |= 0x4000;
    }
    if (M2C_FIELD(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4), u8 *, 2) == 1)
    {
        M2C_FIELD(attach, f32 *, 0xB0) = (f32)cos(0.01745329238474369f * M2C_FIELD(SLOT_LOAD(void *, (u8 *)(SLOT_LOAD(void *, (u8 *)(attach) + 0xB4)) + 0x28), f32 *, 0x10));
    }
    ExtractColorKey((u8 *)SLOT_LOAD(void *, (u8 *)(attach) + 0xB4) + 0x1C, (u8 *)attach + 0x130, 0.0f);
    ExtractScaleKey((u8 *)SLOT_LOAD(void *, (u8 *)(attach) + 0xB4) + 0x14, (u8 *)attach + 0xD0, 0.0f);
    ExtractScaleKey((u8 *)SLOT_LOAD(void *, (u8 *)(attach) + 0xB4) + 0xC, (u8 *)attach + 0x100, 0.0f);
    M2C_FIELD(attach, u32 *, 0x74) |= 0x400000;
    M2C_FIELD(attach, u32 *, 0x74) |= 2;
    M2C_FIELD(attach, u32 *, 0xB8) = 100;
    M2C_FIELD(attach, f32 *, 0xBC) = 1.0f;
    M2C_FIELD(attach, f32 *, 0) = 0.0f;
    M2C_FIELD(attach, f32 *, 4) = 0.0f;
    M2C_FIELD(attach, f32 *, 8) = 1.0f;
    if (M2C_FIELD(data, u32 *, 0x20) != 0)
    {
        SLOT_STORE((u8 *)(attach) + 0x8C, (void *)g_attachGeometryVTable);
    }
    else
    {
        SLOT_STORE((u8 *)(attach) + 0x8C, (void *)g_attachVTable);
    }
    return 1;
#undef g_attachGeometryVTable
#undef g_attachVTable
}

s32 AttachThing_Move(void *thing) {
    extern f32 g_fDeltaTimeMS;
    if (!(M2C_FIELD(thing, u32 *, 0x74) & 0x4000)) {
        ExtractColorKey(SLOT_LOAD(u8 *, (u8 *)(thing) + 0xB4) + 0x1C, (u8 *)thing + 0x130, g_fDeltaTimeMS);
        ExtractScaleKey(SLOT_LOAD(u8 *, (u8 *)(thing) + 0xB4) + 0x14, (u8 *)thing + 0xD0, g_fDeltaTimeMS);
        ExtractScaleKey(SLOT_LOAD(u8 *, (u8 *)(thing) + 0xB4) + 0xC, (u8 *)thing + 0x100, g_fDeltaTimeMS);
    }
    return 1;
}

void AttachThing_DrawCorona(void *arg0) {
    extern s32 g_iDeltaTimeMS;
    typedef struct CoronaColor {
        u8 r;
        u8 g;
        u8 b;
        u8 a;
    } CoronaColor;
    struct AttachData {
        u8 type;
        u8 blend;
        u8 pad2[0x8 - 0x2];
        u32 texture;
        u8 padC[0x24 - 0xC];
        PTR32(f32) range;
    };
    struct AttachCorona {
        u8 pad00[0x40];
        Vector4 pos;
        u8 pad50[0x74 - 0x50];
        u32 flags;
        u8 pad78[0xB4 - 0x78];
        PTR32(struct AttachData) data;
        s32 timer;
        f32 fade;
        u8 padC0[0xD0 - 0xC0];
        f32 scaleX;
        u8 padD4[0x100 - 0xD4];
        f32 scaleY;
        u8 pad104[0x154 - 0x104];
        CoronaColor color;
    } *thing = arg0;
    Vector4 screen __attribute__((aligned(16)));
    Vector4 camera __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 dist;
    CoronaColor scaled;
    CoronaColor color;
    f32 farDist;
    f32 nearDist;
    f32 depth;
    f32 fade;

    if (ViewManager_GetViewFormat() > 1) {
        Thing_Release(thing);
        return;
    }
    farDist = GETP(GETP(thing->data)->range)[1];
    nearDist = GETP(GETP(thing->data)->range)[0];
    Gfx_TransformVector(&thing->pos, &screen);
    depth = screen.w;
    if (!(depth < 0.0f) && !(depth > farDist)) {
        thing->timer -= g_iDeltaTimeMS;
        if (thing->timer < 0) {
            dist = 1.0f;
            Gfx_GetCameraPosition(&camera);
            PSVECSubtract(&thing->pos, &camera, &dir);
            thing->timer = 100;
            if (RayHitsTerrainAndObjects(&camera, &dir, &dist, NULL)) {
                thing->flags |= 0x20;
                return;
            }
            thing->flags &= ~0x20;
        }
        if (!(thing->flags & 0x20)) {
            if (depth > nearDist) {
                fade = thing->fade * (1.0f - (depth - nearDist) / (farDist - nearDist));
            } else {
                fade = thing->fade;
            }
            scaled.r = thing->color.r * fade;
            scaled.g = thing->color.g * fade;
            scaled.b = thing->color.b * fade;
            scaled.a = thing->color.a;
            color = scaled;
            /* port: GC passes ints and floats in separate registers, so the decomp's
             * (scaleX, scaleY, blend) order reaches (flag, a, b) as (blend, scaleX, scaleY) */
            Gfx_AddCorona(&screen, GETP(thing->data)->texture, &color, GETP(thing->data)->blend, thing->scaleX, thing->scaleY);
        }
    }
}

void AttachThing_Draw(void *arg0) {
    void *Gfx_AddBillBoardSprite(u16);
    u8 temp_r0;
    void *temp_r3;
    void *temp_r3_2;

    if (!(M2C_FIELD(arg0, s32 *, 0x74) & 0x10)) {
        Gfx_SetWorldMatrix(0);
        temp_r3 = SLOT_LOAD(void *, (u8 *)(arg0) + 0xB4);
        temp_r0 = M2C_FIELD(temp_r3, u8 *, 0);
        switch ((s32) temp_r0) {                    /* irregular */
        case 0:
            temp_r3_2 = Gfx_AddBillBoardSprite((u16) M2C_FIELD(temp_r3, s32 *, 8));
            M2C_FIELD(temp_r3_2, u8 *, 0x2A) = (u8) M2C_FIELD(SLOT_LOAD(void *, (u8 *)(arg0) + 0xB4), u8 *, 1);
            M2C_FIELD(temp_r3_2, s32 *, 0x24) = (s32) M2C_FIELD(arg0, s32 *, 0x154);
            M2C_FIELD(temp_r3_2, f32 *, 0x1C) = (f32) M2C_FIELD(arg0, f32 *, 0x100);
            M2C_FIELD(temp_r3_2, f32 *, 0xC) = (f32) M2C_FIELD(arg0, f32 *, 0xD0);
            M2C_FIELD(temp_r3_2, f32 *, 0) = (f32) M2C_FIELD(arg0, f32 *, 0x40);
            M2C_FIELD(temp_r3_2, f32 *, 4) = (f32) M2C_FIELD(arg0, f32 *, 0x44);
            M2C_FIELD(temp_r3_2, f32 *, 8) = (f32) M2C_FIELD(arg0, f32 *, 0x48);
            return;
        case 1:
            AttachThing_DrawCorona(arg0);
            break;
        }
    }
}

void AttachGeometry_Draw(void *thing) {
    u8 *data;
    void *geom;
    u32 color;

    data = SLOT_LOAD(u8 *, (u8 *)(thing) + 0xB4);
    geom = SLOT_LOAD(void *, (u8 *)(data) + 0x8);
    if (M2C_FIELD(data, u32 *, 0x1C) != 0) {
        color = M2C_FIELD(thing, u32 *, 0x154);
        Gfx_SetBaseColor(&color);
        Gfx_EnableState(0x10);
    }
    M2C_FIELD(thing, f32 *, 0xDC) = 1.0f;
    Gfx_DrawGeometry_QuaternionScalePos(HND(geom), HND((u8 *)thing + 0x50), HND((u8 *)thing + 0xD0), HND((u8 *)thing + 0x40));
    Gfx_DisableState(0x10);
}

s32 AttachGeometry_Move(void *arg0) {
    extern f32 g_fDeltaTimeMS;
    extern f32 g_fDeltaTimeSec;
    struct AttachGeomData {
        u8 pad0[0x14];
        u8 scale[0x1C - 0x14];
        u8 color[0x2C - 0x1C];
        PTR32(Vector4) axis;
    };
    struct AttachGeom {
        u8 pad0[0x50];
        Quaternion rot;
        u8 pad60[0xB4 - 0x60];
        PTR32(struct AttachGeomData) data;
        u8 padB8[0xC0 - 0xB8];
        f32 spin;
        u8 padC4[0xD0 - 0xC4];
        u8 scale[0x130 - 0xD0];
        u8 color[4];
    } *thing = arg0;
    Quaternion rot;
    Quaternion out;

    ExtractColorKey(GETP(thing->data)->color, thing->color, g_fDeltaTimeMS);
    ExtractScaleKey(GETP(thing->data)->scale, thing->scale, g_fDeltaTimeMS);
    GenerateQuaternion(GETP(GETP(thing->data)->axis), &rot, 0.01745329238474369f * thing->spin * g_fDeltaTimeSec);
    QuaternionMult(&rot, &thing->rot, &out);
    thing->rot = out;
    return 1;
}

void AttachThing_SlaveTransform(u8 *attach, Matrix4 *mtx) {
    typedef struct AttachThing {
        u8 pad0[0x10];
        Matrix4 mtx;
        u8 pad50[0x74 - 0x50];
        u32 flags;
        u8 pad78[0xB0 - 0x78];
        f32 minDot;
        PTR32(u8) data;
        u8 padB8[4];
        f32 fade;
    } AttachThing;
    AttachThing *thing = (AttachThing *)attach;
    Vector4 pos __attribute__((aligned(16)));
    Vector4 camera __attribute__((aligned(16)));
    Vector4 toCamera __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    f32 dot;
    f32 minDot;

    pos = thing->mtx.v[3];
    XformVec(mtx, &pos, &thing->mtx.v[3]);
    if (GETP(thing->data)[2] == 1) {
        thing->flags &= ~0x10;
        Gfx_GetCameraPosition(&camera);
        PSVECSubtract(&thing->mtx.v[3], &camera, &toCamera);
        if (PSVECSquareMag(&toCamera)) {
            PSVECNormalize(&toCamera, &toCamera);
        }
        XformVecOrientation(mtx, SLOT_LOAD(void *, (GETP(thing->data) + 0x28)), &dir);
        dot = -PSVECDotProduct(&dir, &toCamera);
        minDot = thing->minDot;
        if (dot < minDot) {
            thing->flags |= 0x10;
        } else {
            thing->fade = (dot - minDot) / (1.0f - minDot);
        }
    }
}

s32 AttachThing_Invalidate(void *arg0, u32 arg1, u32 arg2) {
    u32 temp_r0;

    temp_r0 = M2C_FIELD(arg0, u32 *, 0xB4);
    if ((temp_r0 >= arg1) && (temp_r0 < arg2)) {
        return 1;
    }
    return 0;
}

s32 RadarBlip_Init(void *blip, void *init) {
    typedef struct { u32 x, y, z, w; } Vec4;

    Thing_Init(blip, 0);
    *(Vec4 *)((u8 *)blip + 0x40) = *(Vec4 *)((u8 *)init + 0x10);
    SLOT_STORE((u8 *)(blip) + 0xB0, SLOT_LOAD(void *, (u8 *)(init) + 0));
    M2C_FIELD(blip, s32 *, 0xB4) = 0;
    M2C_FIELD(blip, f32 *, 0xB8) = 1.0f;
    if (M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(blip) + 0xB0), u8 *, 0x3) == 0) {
        M2C_FIELD(blip, u32 *, 0x74) |= 0x200;
    }
    return 1;
}

s32 RadarBlip_Move(void *blip)
{
    typedef struct { u32 x, y, z, w; } Vec4;

    *(Vec4 *)((u8 *)blip + 0xC0) = *(Vec4 *)((u8 *)blip + 0x40);
    return 1;
}

void RadarBlip_Draw(RadarBlip *blip) {
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

    single = 0;
    pos = blip->pos;
    if (g_CurrentViews == 1) {
        if (!((GETP(blip->player) == NULL || (!Player_IsHuman(GETP(blip->player)) && !GameSettings_GetCodeVariable(0x198, 0))) && StateManager_CurrState() == 5)) {
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
