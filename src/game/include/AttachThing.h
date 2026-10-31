/* Attach things (AttachThing.c): sprites, coronas and small spinning models
 * hung on other things as slaves (lights, glows); and radar blips, the
 * markers drawn over cars. Offsets are the GameCube layout; the asserts
 * keep them that way. */
#ifndef ATTACHTHING_H
#define ATTACHTHING_H

#include <stddef.h>
#include "KeyState.h"

/* AttachInfo.cone: only seen from within `angle` of `dir`. */
typedef struct AttachCone {
    Vector4 dir;                /* 0x00 in the master's space */
    f32 angle;                  /* 0x10 degrees */
} AttachCone;

/* AttachInfo.spin (attach geometry). */
typedef struct AttachSpin {
    Vector4 axis;               /* 0x00 */
    f32 speedBase;              /* 0x10 degrees/s */
    f32 speedRange;             /* 0x14 */
} AttachSpin;

/* An attach thing's description in the data pack. */
typedef struct AttachInfo {
    u8 type;                    /* 0x00 0: billboard sprite, 1: corona */
    u8 blend;                   /* 0x01 */
    u8 directional;             /* 0x02 1: faded by AttachInfo.cone */
    u8 pad03[5];                /* 0x03 */
    u32 visual;                 /* 0x08 texture id, or geometry handle (attach geometry) */
    KeyTrack scale2Keys;        /* 0x0C sprite: rotation, corona: y scale */
    KeyTrack scaleKeys;         /* 0x14 sprite: size, corona: x scale; geometry: scale */
    KeyTrack colorKeys;         /* 0x1C */
    PTR32(f32) range;           /* 0x24 corona: [near, far] fade distances */
    PTR32(AttachCone) cone;     /* 0x28 */
    PTR32(AttachSpin) spin;     /* 0x2C */
} AttachInfo;

/* ThingManager_DARYL_CreateSlave(5, ...) parameters. */
typedef struct AttachParams {
    PTR32(AttachInfo) info;     /* 0x00 */
    u8 pad04[0xC];              /* 0x04 */
    Vector4 pos;                /* 0x10 */
    s32 geometry;               /* 0x20 nonzero: attach geometry (g_attachGeometryVTable) */
    PTR32(Player) player;       /* 0x24 */
} AttachParams;

typedef struct AttachThing {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    f32 minDot;                 /* 0xB0 cos(cone angle) */
    PTR32(AttachInfo) info;     /* 0xB4 */
    s32 timer;                  /* 0xB8 corona: ms to the next occlusion ray */
    f32 fade;                   /* 0xBC */
    f32 spin;                   /* 0xC0 attach geometry: degrees/s */
    u8 padC4[0xC];              /* 0xC4 */
    KeyScaleState scale;        /* 0xD0 */
    KeyScaleState scale2;       /* 0x100 */
    KeyColorState color;        /* 0x130 */
} AttachThing;

/* What a radar blip looks like. */
typedef struct RadarBlipData {
    u8 pad00[2];                /* 0x00 */
    u8 showBar;                 /* 0x02 health bar */
    u8 showSprite;              /* 0x03 0: the blip thing isn't drawn at all (Thing flag 0x200) */
    f32 size;                   /* 0x04 */
    f32 height;                 /* 0x08 above the position, in pixels */
    u8 pad0C[8];                /* 0x0C */
    u32 radarTexture;           /* 0x14 its dot on the HUD radar (InterfaceManager_DrawRadarBlips) */
    u32 texture;                /* 0x18 */
    PTR32(GXColor) radarColor;  /* 0x1C */
    PTR32(GXColor) color;       /* 0x20 */
} RadarBlipData;

/* ThingManager_DARYL_CreateSlave(0x11, ...) parameters. */
typedef struct RadarBlipParams {
    PTR32(RadarBlipData) data;  /* 0x00 */
    u8 pad04[0xC];              /* 0x04 */
    Vector4 pos;                /* 0x10 */
} RadarBlipParams;

typedef struct RadarBlip {
    Thing base;                 /* 0x00 */
    u8 padAC[4];                /* 0xAC */
    PTR32(RadarBlipData) data;  /* 0xB0 */
    s32 unkB4;                  /* 0xB4 */
    f32 health;                 /* 0xB8 0..1, set by the master (Destructible_Move) */
    u8 padBC[4];                /* 0xBC */
    Vector4 lastPos;            /* 0xC0 */
} RadarBlip;

_Static_assert(offsetof(AttachInfo, scale2Keys) == 0x0C, "AttachInfo.scale2Keys");
_Static_assert(offsetof(AttachInfo, colorKeys) == 0x1C, "AttachInfo.colorKeys");
_Static_assert(offsetof(AttachInfo, spin) == 0x2C, "AttachInfo.spin");
_Static_assert(offsetof(AttachParams, geometry) == 0x20, "AttachParams.geometry");
_Static_assert(offsetof(AttachParams, player) == 0x24, "AttachParams.player");
_Static_assert(offsetof(AttachThing, info) == 0xB4, "AttachThing.info");
_Static_assert(offsetof(AttachThing, spin) == 0xC0, "AttachThing.spin");
_Static_assert(offsetof(AttachThing, scale) == 0xD0, "AttachThing.scale");
_Static_assert(offsetof(AttachThing, scale2) == 0x100, "AttachThing.scale2");
_Static_assert(offsetof(AttachThing, color) == 0x130, "AttachThing.color");
_Static_assert(sizeof(AttachThing) == 0x160, "AttachThing size");
_Static_assert(offsetof(RadarBlipData, radarTexture) == 0x14, "RadarBlipData.radarTexture");
_Static_assert(offsetof(RadarBlipData, texture) == 0x18, "RadarBlipData.texture");
_Static_assert(offsetof(RadarBlipData, radarColor) == 0x1C, "RadarBlipData.radarColor");
_Static_assert(offsetof(RadarBlipData, color) == 0x20, "RadarBlipData.color");
_Static_assert(offsetof(RadarBlip, data) == 0xB0, "RadarBlip.data");
_Static_assert(offsetof(RadarBlip, health) == 0xB8, "RadarBlip.health");
_Static_assert(offsetof(RadarBlip, lastPos) == 0xC0, "RadarBlip.lastPos");
_Static_assert(sizeof(RadarBlip) == 0xD0, "RadarBlip size");

#endif
