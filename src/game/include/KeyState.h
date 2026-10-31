/* Key-framed tracks in effect records, and the playback state the
 * Extract*Key functions (ExtractKeys.c) keep in the objects that use them
 * (projectiles, particles, attached sprites). Offsets are the GameCube
 * layout. */
#ifndef KEYSTATE_H
#define KEYSTATE_H

#include <stddef.h>

/* A track in a data record: `count` keys at `keys`. The key layout depends
 * on the kind (colour, scale, f32); a colour key starts with its GXColor. */
typedef struct KeyTrack {
    u32 count;                  /* 0x00 */
    PTR32(void) keys;           /* 0x04 */
} KeyTrack;

/* ExtractColorKey state. Set `reset` to restart the track. */
typedef struct KeyColorState {
    f32 slope[4];               /* 0x00 r, g, b, a per ms */
    f32 value[4];               /* 0x10 r, g, b, a */
    f32 time;                   /* 0x20 into the current key */
    GXColor color;              /* 0x24 current colour */
    u16 index;                  /* 0x28 */
    u16 current;                /* 0x2A */
    u8 reset;                   /* 0x2C */
    u8 pad2D[3];                /* 0x2D */
} KeyColorState;

/* ExtractScaleKey state: v is the current scale (w unused by the keys). */
typedef struct KeyScaleState {
    Vector4 v;                  /* 0x00 */
    f32 slope[3];               /* 0x10 */
    f32 time;                   /* 0x1C */
    u16 index;                  /* 0x20 */
    u16 current;                /* 0x22 */
    u8 reset;                   /* 0x24 */
    u8 pad25[0xB];              /* 0x25 */
} KeyScaleState;

/* ExtractF32Key state. */
typedef struct KeyF32State {
    f32 value;                  /* 0x00 */
    f32 slope;                  /* 0x04 */
    f32 time;                   /* 0x08 */
    u16 index;                  /* 0x0C */
    u16 current;                /* 0x0E */
    u8 reset;                   /* 0x10 */
    u8 pad11[0xF];              /* 0x11 */
} KeyF32State;

/* ExtractPointKey state: a 2D point (shader texture scroll). */
typedef struct KeyPointState {
    f32 x;                      /* 0x00 */
    f32 y;                      /* 0x04 */
    f32 slopeX;                 /* 0x08 per ms */
    f32 slopeY;                 /* 0x0C */
    f32 time;                   /* 0x10 into the current key */
    u16 index;                  /* 0x14 */
    u16 current;                /* 0x16 */
    u8 reset;                   /* 0x18 */
    u8 pad19[3];                /* 0x19 */
} KeyPointState;

_Static_assert(sizeof(KeyPointState) == 0x1C, "KeyPointState size");
_Static_assert(offsetof(KeyColorState, color) == 0x24, "KeyColorState.color");
_Static_assert(offsetof(KeyColorState, reset) == 0x2C, "KeyColorState.reset");
_Static_assert(sizeof(KeyColorState) == 0x30, "KeyColorState size");
_Static_assert(offsetof(KeyScaleState, reset) == 0x24, "KeyScaleState.reset");
_Static_assert(sizeof(KeyScaleState) == 0x30, "KeyScaleState size");
_Static_assert(offsetof(KeyF32State, reset) == 0x10, "KeyF32State.reset");
_Static_assert(sizeof(KeyF32State) == 0x20, "KeyF32State size");

#endif
