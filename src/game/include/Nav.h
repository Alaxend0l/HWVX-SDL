/* The navigation graph: nav points loaded with the world, linked into routes,
 * searched by NavHolder_FindPath (A*, NavHolder.c) and followed by AI cars
 * (AI_CarA.c) and the target arrow. Offsets are the GameCube layout. */
#ifndef NAV_H
#define NAV_H

#include <stddef.h>

/* NavPoint.pathFlags */
enum {
    NAV_PATH_USABLE = 1,        /* may be on a path this search (route chance) */
    NAV_PATH_OPEN = 2,          /* on the A* open list */
    NAV_PATH_CLOSED = 4,        /* on the A* closed list */
};

#define NAV_NO_LINK (-0x8000)   /* unused entry in NavPoint.prev / next */

/* One nav point (0x60 bytes, NavData.nodes[]). */
typedef struct NavPoint {
    Vector4 pos;                       /* 0x00 (w unused) */
    u32 numPrev;                       /* 0x10 */
    PTR32(f32) prevCosts;              /* 0x14 A* cost of each prev link */
    PTR32(s32) prev;                   /* 0x18 nav point indices; the links A* follows */
    u32 numNext;                       /* 0x1C */
    PTR32(s32) next;                   /* 0x20 nav point indices */
    f32 radius;                        /* 0x24 reached within this distance */
    s32 index;                         /* 0x28 own index */
    f32 speed;                         /* 0x2C AI cars' speed through it, 0: no limit */
    u8 pad30[5];                       /* 0x30 */
    u8 chance;                         /* 0x35 percent: used by a path search, picked as an AI target */
    u8 pad36;                          /* 0x36 */
    u8 pathFlags;                      /* 0x37 NAV_PATH_* (search state) */
    PTR32(struct NavPoint) parent;     /* 0x38 A* parent: once a path is found, the next
                                          point of it (the search runs goal to start) */
    PTR32(struct NavPoint) listNext;   /* 0x3C open/closed list link */
    f32 g;                             /* 0x40 A* cost from the start */
    f32 h;                             /* 0x44 A* estimate to the goal */
    f32 f;                             /* 0x48 g + h */
    s32 nextWayPoint;                  /* 0x4C index, when type == NAV_TYPE_WAYPOINT */
    u8 enabled;                        /* 0x50 */
    u8 type;                           /* 0x51 NAV_TYPE_* */
    u8 pad52[0xE];                     /* 0x52 */
} NavPoint;

/* NavPoint.type */
enum {
    NAV_TYPE_WAYPOINT = 1,             /* part of the way point chain (nextWayPoint) */
};

/* The world's nav data (NavHolder_Init). */
typedef struct NavData {
    PTR32(NavPoint) nodes;             /* 0x00 */
    u32 numNodes;                      /* 0x04 */
    s32 firstWayPoint;                 /* 0x08 index of the first way point */
} NavData;

/* gs_NavHolder */
typedef struct NavHolder {
    PTR32(NavData) data;               /* 0x00 */
    PTR32(PTR32(void)) objects;        /* 0x04 Grid_* handle per nav point */
} NavHolder;

_Static_assert(offsetof(NavPoint, numPrev) == 0x10, "NavPoint.numPrev");
_Static_assert(offsetof(NavPoint, next) == 0x20, "NavPoint.next");
_Static_assert(offsetof(NavPoint, radius) == 0x24, "NavPoint.radius");
_Static_assert(offsetof(NavPoint, index) == 0x28, "NavPoint.index");
_Static_assert(offsetof(NavPoint, speed) == 0x2C, "NavPoint.speed");
_Static_assert(offsetof(NavPoint, chance) == 0x35, "NavPoint.chance");
_Static_assert(offsetof(NavPoint, pathFlags) == 0x37, "NavPoint.pathFlags");
_Static_assert(offsetof(NavPoint, parent) == 0x38, "NavPoint.parent");
_Static_assert(offsetof(NavPoint, g) == 0x40, "NavPoint.g");
_Static_assert(offsetof(NavPoint, nextWayPoint) == 0x4C, "NavPoint.nextWayPoint");
_Static_assert(offsetof(NavPoint, enabled) == 0x50, "NavPoint.enabled");
_Static_assert(offsetof(NavPoint, type) == 0x51, "NavPoint.type");
_Static_assert(sizeof(NavPoint) == 0x60, "NavPoint size");
_Static_assert(offsetof(NavData, firstWayPoint) == 0x08, "NavData.firstWayPoint");

#endif
