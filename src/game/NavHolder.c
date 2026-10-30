#include "common.h"


extern NavHolder gs_NavHolder_803C6CD8;
extern s8 gs_useMainRouteOnly_803C7000;
extern PathNode *gs_pOpenListHead_803C7008;
extern PathNode *gs_pClosedListHead_803C7004;
extern s32 __float_nan[];
extern s32 __float_huge[];

static inline f64 sqrt(f64 x) {
    f64 guess;
    s32 i;

    if (x > 0.0) {
        guess = __frsqrte(x);
        for (i = 0; i < 4; i++) {
            guess = 0.5 * guess * (3.0 - x * (guess * guess));
        }
        return x * guess;
    } else if (x == 0.0) {
        return 0.0;
    } else if (x) {
        return *(f32 *)__float_nan;
    }
    return *(f32 *)__float_huge;
}

static inline PathNode *NavHolder_PopLowestOpen(void) {
    PathNode *head = gs_pOpenListHead_803C7008;
    PathNode *prev = NULL;
    PathNode *p;
    PathNode *best;
    f32 bestF;

    if (head == NULL) {
        return NULL;
    }
    bestF = head->f;
    p = head;
    best = head;
    for (; GETP(p->next) != NULL; p = GETP(p->next)) {
        if (GETP(p->next)->f < bestF) {
            bestF = GETP(p->next)->f;
            prev = p;
            best = GETP(p->next);
        }
    }
    if (prev == NULL) {
        gs_pOpenListHead_803C7008 = GETP(head->next);
    } else {
        SETP(prev->next, GETP(best->next));
    }
    best->flags &= ~2;
    return best;
}

static inline void NavHolder_ResetPathFlags(NavData *data) {
    u32 i;
    u32 chance = 1;

    if (data == NULL) {
        return;
    }
    gs_pOpenListHead_803C7008 = NULL;
    gs_pClosedListHead_803C7004 = NULL;
    if (gs_useMainRouteOnly_803C7000) {
        chance = 100;
        gs_useMainRouteOnly_803C7000 = 0;
    }
    for (i = 0; i < data->numNodes; i++) {
        u32 r = RandomNumberGenerator_GetRandomInteger(chance, 100);
        u8 *node = GETP(data->nodes) + i * 0x60;
        if (node[0x50] != 0 && node[0x35] >= r) {
            node[0x37] |= 1;
        } else {
            node[0x37] &= ~1;
        }
        GETP(data->nodes)[i * 0x60 + 0x37] &= ~6;
        SLOT_STORE((GETP(data->nodes) + i * 0x60 + 0x38), NULL);
    }
}

void NavHolder_Init(NavData *data) {
    u32 i;

    SETP(gs_NavHolder_803C6CD8.data, data);
    SETP(gs_NavHolder_803C6CD8.objects, NULL);
    if (data->numNodes != 0) {
        SETP(gs_NavHolder_803C6CD8.objects, (void **)Allocate(data->numNodes * 4, 0, ""));
    }
    for (i = 0; i < GETP(gs_NavHolder_803C6CD8.data)->numNodes; i++) {
        GETP(gs_NavHolder_803C6CD8.objects)[i] = (void *)PTR(Grid_AddObject(5, i, GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + i * 0x60, GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + i * 0x60));
    }
}

void NavHolder_Release(void) {
    Deallocate(PTR(M2C_FIELD(&gs_NavHolder_803C6CD8, s32 *, 4)));
    M2C_FIELD(&gs_NavHolder_803C6CD8, s32 *, 0) = 0;
    M2C_FIELD(&gs_NavHolder_803C6CD8, s32 *, 4) = 0;
}


/* NavHolder_IsNavLinkNearer with its recursion step (FindIndexOfNearestNavLink) expanded once;
   FindIndexOfNearestNavLink inlines it. */
static inline s32 NavHolder_IsNavLinkNearer_Expanded(f32 *pos, NavLink *link, f32 *nearest, u8 depth) {
    extern s32 gs_aLinksDepth3[64];
    s32 result = -1;
    s32 i;
    s32 found;
    s32 best;
    u32 j;
    f32 dist;

    if (!link->enabled) {
        return -1;
    }
    for (i = 0; i < 64; i++) {
        if (link->index == gs_aLinksDepth3[i]) {
            return -1;
        }
        if (gs_aLinksDepth3[i] == -0x8000) {
            break;
        }
    }
    gs_aLinksDepth3[i] = link->index;
    gs_aLinksDepth3[i + 1] = -0x8000;
    dist = PSVECSquareDistance(pos, link->pos);
    if (*nearest > dist) {
        *nearest = dist;
        result = link->index;
    }
    if (depth < 3) {
        best = -1;
        for (j = 0; j < link->numPrev; j++) {
            if (GETP(link->prev)[j] != -0x8000) {
                found = NavHolder_IsNavLinkNearer(pos, (NavLink *)NavHolder_GetNavPoint(GETP(link->prev)[j]), nearest, depth + 1);
                if (found != -1) {
                    best = found;
                }
            }
        }
        for (j = 0; j < link->numNext; j++) {
            if (GETP(link->next)[j] != -0x8000) {
                found = NavHolder_IsNavLinkNearer(pos, (NavLink *)NavHolder_GetNavPoint(GETP(link->next)[j]), nearest, depth + 1);
                if (found != -1) {
                    best = found;
                }
            }
        }
        if (best != -1) {
            result = best;
        }
    }
    return result;
}

s32 NavHolder_IsNavLinkNearer(f32 *pos, NavLink *link, f32 *nearest, u8 depth) {
    extern s32 gs_aLinksDepth3[64];
    s32 result = -1;
    s32 i;
    s32 found;
    f32 dist;

    if (!link->enabled) {
        return -1;
    }
    for (i = 0; i < 64; i++) {
        if (link->index == gs_aLinksDepth3[i]) {
            return -1;
        }
        if (gs_aLinksDepth3[i] == -0x8000) {
            break;
        }
    }
    gs_aLinksDepth3[i] = link->index;
    gs_aLinksDepth3[i + 1] = -0x8000;
    dist = PSVECSquareDistance(pos, link->pos);
    if (*nearest > dist) {
        *nearest = dist;
        result = link->index;
    }
    if (depth < 3) {
        found = NavHolder_FindIndexOfNearestNavLink(pos, link, nearest, depth + 1);
        if (found != -1) {
            result = found;
        }
    }
    return result;
}

#pragma always_inline on

s32 NavHolder_FindIndexOfNearestNavLink(f32 *pos, NavLink *link, f32 *nearest, u8 depth) {
    s32 result = -1;
    s32 found;
    u32 i;

    for (i = 0; i < link->numPrev; i++) {
        if (GETP(link->prev)[i] != -0x8000) {
            found = NavHolder_IsNavLinkNearer_Expanded(pos, (NavLink *)NavHolder_GetNavPoint(GETP(link->prev)[i]), nearest, depth);
            if (found != -1) {
                result = found;
            }
        }
    }
    for (i = 0; i < link->numNext; i++) {
        if (GETP(link->next)[i] != -0x8000) {
            found = NavHolder_IsNavLinkNearer_Expanded(pos, (NavLink *)NavHolder_GetNavPoint(GETP(link->next)[i]), nearest, depth);
            if (found != -1) {
                result = found;
            }
        }
    }
    return result;
}

s32 NavHolder_FindIndexOfNearestNavPoint(f32 *pos, s32 hint) {
    extern s32 gs_aLinksDepth3[64];
    typedef struct NavPoint {
        f32 pos[3];
        u8 pad0C[0x10 - 0xC];
        u32 numPrev;
        u8 pad14[0x1C - 0x14];
        u32 numNext;
        u8 pad20[0x28 - 0x20];
        s32 index;
        u8 pad2C[0x50 - 0x2C];
        u8 enabled;
    } NavPoint;
    GridResult results[64];
    Vector4 min __attribute__((aligned(16)));
    Vector4 max __attribute__((aligned(16)));
    f32 nearest;
    NavPoint *p;
    u32 j;
    u32 count;
    GridResult *result;
    u32 i;
    NavPoint *point;
    s32 found;

    found = -1;
    nearest = 250000.0f;
    if (hint != -1) {
        nearest = PSVECSquareDistance(pos, (f32 *)NavHolder_GetNavPoint(hint));
    }
    if (nearest < 250000.0f) {
        gs_aLinksDepth3[0] = hint;
        gs_aLinksDepth3[1] = -0x8000;
        found = NavHolder_FindIndexOfNearestNavLink(pos, (NavLink *)NavHolder_GetNavPoint(hint), &nearest, 1);
        if (found == -1) {
            found = hint;
        }
    } else {
        min.x = min.y = min.z = -50.0f;
        max.x = max.y = max.z = 50.0f;
        PSVECAdd(pos, &min, &min);
        PSVECAdd(pos, &max, &max);
        count = Grid_GetNearestObjects(&min, &max, 0x20, results, 0x40, NULL, 0);
        if (count == 0) {
            return -1;
        }
        nearest = 1e10f;
        for (i = 0, result = results; i < count; result++, i++) {
            point = (NavPoint *)NavHolder_GetNavPoint(result->id);
            if (point->enabled && (point->numPrev != 0 || point->numNext != 0)) {
                f32 dist = PSVECSquareDistance(pos, point->pos);
                if (nearest > dist) {
                    nearest = dist;
                    found = point->index;
                }
            }
        }
        if (1e10f == nearest) {
            for (j = 0; j < NavHolder_GetTotalNavPoints(); j++) {
                p = (NavPoint *)NavHolder_GetNavPoint(j);
                if (p->enabled && (p->numPrev != 0 || p->numNext != 0)) {
                    f32 dist = PSVECSquareDistance(pos, p->pos);
                    if (nearest > dist) {
                        nearest = dist;
                        found = p->index;
                    }
                }
            }
        }
    }
    return found;
}

s32 NavHolder_IsLinked(s32 from, s32 to) {
    u8 i;

    if (from < 0 || to < 0) {
        return 0;
    }
    if (from == to) {
        return 1;
    }
    for (i = 0; i < M2C_FIELD(GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + from * 0x60, u32 *, 0x1C); i++) {
        if (to == SLOT_LOAD(s32 *, (u8 *)(GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + from * 0x60) + 0x20)[i]) {
            return 1;
        }
    }
    return 0;
}

void NavHolder_FillPointerArrayPath(u8 *point, u8 **array, u32 max) {
    u8 **out = array;
    u32 i = 0;
    while (point != NULL && i < max) {
        *out++ = point;
        i++;
        point = SLOT_LOAD(u8 *, (point + 0x38));
    }
    if (i < max) {
        array[i] = NULL;
    }
}

void NavHolder_UseMainRouteOnly(void) {
    gs_useMainRouteOnly_803C7000 = 1;
}

s8 NavHolder_CreatePath(u8 *goal, u8 *start) {
    PathNode *cur;
    PathNode *n;
    u8 i;
    PathNode *end = (PathNode *)goal;
    PathNode *node = (PathNode *)start;
    PathNode *p;
    f32 g;

    node->g = 0.0f;
    node->h = sqrt(PSVECSquareDistance(end->pos, node->pos));
    node->f = node->g + node->h;
    SETP(node->next, gs_pOpenListHead_803C7008);
    gs_pOpenListHead_803C7008 = node;
    node->flags |= 2;
    while (1) {
        cur = NavHolder_PopLowestOpen();
        if (cur == end) {
            return 1;
        }
        if (cur == NULL) {
            return 0;
        }
        for (i = 0; i < cur->numLinks; i++) {
            n = (PathNode *)NavHolder_GetNavPoint(GETP(cur->links)[i]);
            if (n != end && !(s8)(n->flags & 1)) {
                if (!(s8)(n->flags & 4)) {
                    SETP(n->next, gs_pClosedListHead_803C7004);
                    gs_pClosedListHead_803C7004 = n;
                    n->flags |= 4;
                }
            } else {
                g = cur->g + GETP(cur->costs)[i];
                if (!(n->flags & 6)) {
                    n->g = g;
                    n->h = sqrt(PSVECSquareDistance(end->pos, n->pos));
                    n->f = g + n->h;
                    SETP(n->parent, cur);
                    SETP(n->next, gs_pOpenListHead_803C7008);
                    gs_pOpenListHead_803C7008 = n;
                    n->flags |= 2;
                } else if (n->g > g) {
                    n->g = g;
                    n->f = g + n->h;
                    SETP(n->parent, cur);
                    if ((s8)(n->flags & 4)) {
                        p = gs_pClosedListHead_803C7004;
                        if (n == p) {
                            gs_pClosedListHead_803C7004 = GETP(p->next);
                        } else {
                            for (; GETP(p->parent) != NULL; p = GETP(p->parent)) {
                                if (GETP(p->parent) == n) {
                                    SETP(p->parent, GETP(n->parent));
                                    break;
                                }
                            }
                        }
                        n->flags &= ~4;
                        SETP(n->next, gs_pOpenListHead_803C7008);
                        gs_pOpenListHead_803C7008 = n;
                        n->flags |= 2;
                    }
                }
            }
        }
        SETP(cur->next, gs_pClosedListHead_803C7004);
        gs_pClosedListHead_803C7004 = cur;
        cur->flags |= 4;
    }
}

u8 *NavHolder_FindPath(s32 from, s32 to) {
    if (from == -1 || to == -1) {
        return NULL;
    }
    NavHolder_ResetPathFlags(GETP(gs_NavHolder_803C6CD8.data));
    if (NavHolder_CreatePath(GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + from * 0x60, GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + to * 0x60)) {
        return GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + from * 0x60;
    }
    return NULL;
}

u32 NavHolder_GetTotalNavPoints(void) {
    return GETP(gs_NavHolder_803C6CD8.data)->numNodes;
}

u8 *NavHolder_GetNavPoint(s32 index) {
    u8 *point = NULL;
    if (GETP(gs_NavHolder_803C6CD8.data) != NULL && index < (s32)GETP(gs_NavHolder_803C6CD8.data)->numNodes) {
        point = GETP(GETP(gs_NavHolder_803C6CD8.data)->nodes) + index * 0x60;
    }
    return point;
}

void *NavHolder_GetFirstWayPoint(void) {
    return GETP(GETP(gs_NavHolder_803C6CD8.data)->firstWayPoint);
}

