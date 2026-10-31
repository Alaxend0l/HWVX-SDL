/* The nav graph (Nav.h): nearest-point searches and A* paths over the nav
 * points, used by the AI cars, the target arrow and respawning. */
#include "common.h"
#include "Nav.h"


extern NavHolder gs_NavHolder;
extern s8 gs_useMainRouteOnly;
extern NavPoint *gs_pOpenListHead;
extern NavPoint *gs_pClosedListHead;
extern s32 __float_nan[];
extern s32 __float_huge[];

/* MSL's inline sqrt (frsqrte + Newton steps) came here; libm's sqrt is used instead. */

/* Unlinks and returns the open point with the lowest f. */
static inline NavPoint *NavHolder_PopLowestOpen(void) {
    NavPoint *head = gs_pOpenListHead;
    NavPoint *prev = NULL;
    NavPoint *p;
    NavPoint *best;
    f32 bestF;

    if (head == NULL) {
        return NULL;
    }
    bestF = head->f;
    p = head;
    best = head;
    for (; GETP(p->listNext) != NULL; p = GETP(p->listNext)) {
        if (GETP(p->listNext)->f < bestF) {
            bestF = GETP(p->listNext)->f;
            prev = p;
            best = GETP(p->listNext);
        }
    }
    if (prev == NULL) {
        gs_pOpenListHead = GETP(head->listNext);
    } else {
        SETP(prev->listNext, GETP(best->listNext));
    }
    best->pathFlags &= ~NAV_PATH_OPEN;
    return best;
}

/* Clears the search state. A point is usable this search if enabled and its
 * route chance (NavPoint.chance) beats a roll of 1..100 (of 100 after
 * NavHolder_UseMainRouteOnly). */
static inline void NavHolder_ResetPathFlags(NavData *data) {
    u32 i;
    u32 chance = 1;

    if (data == NULL) {
        return;
    }
    gs_pOpenListHead = NULL;
    gs_pClosedListHead = NULL;
    if (gs_useMainRouteOnly) {
        chance = 100;
        gs_useMainRouteOnly = 0;
    }
    for (i = 0; i < data->numNodes; i++) {
        u32 r = RandomNumberGenerator_GetRandomInteger(chance, 100);
        NavPoint *node = &GETP(data->nodes)[i];
        if (node->enabled != 0 && node->chance >= r) {
            node->pathFlags |= NAV_PATH_USABLE;
        } else {
            node->pathFlags &= ~NAV_PATH_USABLE;
        }
        node->pathFlags &= ~(NAV_PATH_OPEN | NAV_PATH_CLOSED);
        SETP(node->parent, NULL);
    }
}

/* Takes the world's nav data and adds every point to the grid (type 5). */
void NavHolder_Init(NavData *data) {
    u32 i;

    SETP(gs_NavHolder.data, data);
    SETP(gs_NavHolder.objects, NULL);
    if (data->numNodes != 0) {
        SETP(gs_NavHolder.objects, (void **)Allocate(data->numNodes * 4, 0, ""));
    }
    for (i = 0; i < GETP(gs_NavHolder.data)->numNodes; i++) {
        NavPoint *point = &GETP(GETP(gs_NavHolder.data)->nodes)[i];
        SETP(GETP(gs_NavHolder.objects)[i], Grid_AddObject(5, i, point, point));
    }
}

void NavHolder_Release(void) {
    Deallocate(GETP(gs_NavHolder.objects));
    SETP(gs_NavHolder.data, NULL);
    SETP(gs_NavHolder.objects, NULL);
}


/* NavHolder_IsNavLinkNearer with its recursion step (FindIndexOfNearestNavLink) expanded once;
   FindIndexOfNearestNavLink inlines it. */
static inline s32 NavHolder_IsNavLinkNearer_Expanded(f32 *pos, NavPoint *link, f32 *nearest, u8 depth) {
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
        if (gs_aLinksDepth3[i] == NAV_NO_LINK) {
            break;
        }
    }
    gs_aLinksDepth3[i] = link->index;
    gs_aLinksDepth3[i + 1] = NAV_NO_LINK;
    dist = PSVECSquareDistance(pos, &link->pos);
    if (*nearest > dist) {
        *nearest = dist;
        result = link->index;
    }
    if (depth < 3) {
        best = -1;
        for (j = 0; j < link->numPrev; j++) {
            if (GETP(link->prev)[j] != NAV_NO_LINK) {
                found = NavHolder_IsNavLinkNearer(pos, NavHolder_GetNavPoint(GETP(link->prev)[j]), nearest, depth + 1);
                if (found != -1) {
                    best = found;
                }
            }
        }
        for (j = 0; j < link->numNext; j++) {
            if (GETP(link->next)[j] != NAV_NO_LINK) {
                found = NavHolder_IsNavLinkNearer(pos, NavHolder_GetNavPoint(GETP(link->next)[j]), nearest, depth + 1);
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

/* Index of the point nearest to pos among link and its neighbours up to
 * depth 3, each visited once (gs_aLinksDepth3, ended by NAV_NO_LINK); -1 if
 * none is nearer than *nearest, which it lowers. */
s32 NavHolder_IsNavLinkNearer(f32 *pos, NavPoint *link, f32 *nearest, u8 depth) {
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
        if (gs_aLinksDepth3[i] == NAV_NO_LINK) {
            break;
        }
    }
    gs_aLinksDepth3[i] = link->index;
    gs_aLinksDepth3[i + 1] = NAV_NO_LINK;
    dist = PSVECSquareDistance(pos, &link->pos);
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

/* NavHolder_IsNavLinkNearer over the prev and next links of link. */
s32 NavHolder_FindIndexOfNearestNavLink(f32 *pos, NavPoint *link, f32 *nearest, u8 depth) {
    s32 result = -1;
    s32 found;
    u32 i;

    for (i = 0; i < link->numPrev; i++) {
        if (GETP(link->prev)[i] != NAV_NO_LINK) {
            found = NavHolder_IsNavLinkNearer_Expanded(pos, NavHolder_GetNavPoint(GETP(link->prev)[i]), nearest, depth);
            if (found != -1) {
                result = found;
            }
        }
    }
    for (i = 0; i < link->numNext; i++) {
        if (GETP(link->next)[i] != NAV_NO_LINK) {
            found = NavHolder_IsNavLinkNearer_Expanded(pos, NavHolder_GetNavPoint(GETP(link->next)[i]), nearest, depth);
            if (found != -1) {
                result = found;
            }
        }
    }
    return result;
}

/* Index of the linked, enabled nav point nearest to pos: searched around
 * hint (an earlier result, -1: none) when that is within 500, else among the
 * grid's points within 50, else among all of them. -1: none. */
s32 NavHolder_FindIndexOfNearestNavPoint(f32 *pos, s32 hint) {
    extern s32 gs_aLinksDepth3[64];
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
        nearest = PSVECSquareDistance(pos, NavHolder_GetNavPoint(hint));
    }
    if (nearest < 250000.0f) {
        gs_aLinksDepth3[0] = hint;
        gs_aLinksDepth3[1] = NAV_NO_LINK;
        found = NavHolder_FindIndexOfNearestNavLink(pos, NavHolder_GetNavPoint(hint), &nearest, 1);
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
            point = NavHolder_GetNavPoint(result->id);
            if (point->enabled && (point->numPrev != 0 || point->numNext != 0)) {
                f32 dist = PSVECSquareDistance(pos, &point->pos);
                if (nearest > dist) {
                    nearest = dist;
                    found = point->index;
                }
            }
        }
        if (1e10f == nearest) {
            for (j = 0; j < NavHolder_GetTotalNavPoints(); j++) {
                p = NavHolder_GetNavPoint(j);
                if (p->enabled && (p->numPrev != 0 || p->numNext != 0)) {
                    f32 dist = PSVECSquareDistance(pos, &p->pos);
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

/* Whether `to` is `from` or one of its next links. */
s32 NavHolder_IsLinked(s32 from, s32 to) {
    NavPoint *point;
    u8 i;

    if (from < 0 || to < 0) {
        return 0;
    }
    if (from == to) {
        return 1;
    }
    point = &GETP(GETP(gs_NavHolder.data)->nodes)[from];
    for (i = 0; i < point->numNext; i++) {
        if (to == GETP(point->next)[i]) {
            return 1;
        }
    }
    return 0;
}

/* Writes the path starting at `point` (NavHolder_FindPath) into `array`,
 * NULL-ended if shorter than max.
 * port: `array` is a table of 4-byte pointer slots (Player navPath, AICar.path) */
void NavHolder_FillPointerArrayPath(void *point, void *array, u32 max) {
    NavPoint *p = point;
    u32 *out = array;
    u32 i = 0;
    while (p != NULL && i < max) {
        SLOT_STORE(out + i, p);
        i++;
        p = GETP(p->parent);
    }
    if (i < max) {
        out[i] = 0;
    }
}

/* The next search uses only the points of 100% chance. */
void NavHolder_UseMainRouteOnly(void) {
    gs_useMainRouteOnly = 1;
}

/* A* from start along the prev links until goal is reached; the parents
 * then lead from goal back to start. 0: no path. */
s8 NavHolder_CreatePath(NavPoint *goal, NavPoint *start) {
    NavPoint *cur;
    NavPoint *n;
    u8 i;
    NavPoint *end = goal;
    NavPoint *node = start;
    NavPoint *p;
    f32 g;

    node->g = 0.0f;
    node->h = sqrt(PSVECSquareDistance(&end->pos, &node->pos));
    node->f = node->g + node->h;
    SETP(node->listNext, gs_pOpenListHead);
    gs_pOpenListHead = node;
    node->pathFlags |= NAV_PATH_OPEN;
    while (1) {
        cur = NavHolder_PopLowestOpen();
        if (cur == end) {
            return 1;
        }
        if (cur == NULL) {
            return 0;
        }
        for (i = 0; i < cur->numPrev; i++) {
            n = NavHolder_GetNavPoint(GETP(cur->prev)[i]);
            if (n != end && !(s8)(n->pathFlags & NAV_PATH_USABLE)) {
                if (!(s8)(n->pathFlags & NAV_PATH_CLOSED)) {
                    SETP(n->listNext, gs_pClosedListHead);
                    gs_pClosedListHead = n;
                    n->pathFlags |= NAV_PATH_CLOSED;
                }
            } else {
                g = cur->g + GETP(cur->prevCosts)[i];
                if (!(n->pathFlags & (NAV_PATH_OPEN | NAV_PATH_CLOSED))) {
                    n->g = g;
                    n->h = sqrt(PSVECSquareDistance(&end->pos, &n->pos));
                    n->f = g + n->h;
                    SETP(n->parent, cur);
                    SETP(n->listNext, gs_pOpenListHead);
                    gs_pOpenListHead = n;
                    n->pathFlags |= NAV_PATH_OPEN;
                } else if (n->g > g) {
                    n->g = g;
                    n->f = g + n->h;
                    SETP(n->parent, cur);
                    if ((s8)(n->pathFlags & NAV_PATH_CLOSED)) {
                        /* back to the open list; the unlink below walks the
                         * parents, not listNext (as the original does) */
                        p = gs_pClosedListHead;
                        if (n == p) {
                            gs_pClosedListHead = GETP(p->listNext);
                        } else {
                            for (; GETP(p->parent) != NULL; p = GETP(p->parent)) {
                                if (GETP(p->parent) == n) {
                                    SETP(p->parent, GETP(n->parent));
                                    break;
                                }
                            }
                        }
                        n->pathFlags &= ~NAV_PATH_CLOSED;
                        SETP(n->listNext, gs_pOpenListHead);
                        gs_pOpenListHead = n;
                        n->pathFlags |= NAV_PATH_OPEN;
                    }
                }
            }
        }
        SETP(cur->listNext, gs_pClosedListHead);
        gs_pClosedListHead = cur;
        cur->pathFlags |= NAV_PATH_CLOSED;
    }
}

/* A path between two nav point indices: the NavPoint `from`, whose parents
 * lead to `to` (see NavHolder_FillPointerArrayPath), or NULL. */
void *NavHolder_FindPath(s32 from, s32 to) {
    NavPoint *nodes;

    if (from == -1 || to == -1) {
        return NULL;
    }
    NavHolder_ResetPathFlags(GETP(gs_NavHolder.data));
    nodes = GETP(GETP(gs_NavHolder.data)->nodes);
    if (NavHolder_CreatePath(&nodes[from], &nodes[to])) {
        return &nodes[from];
    }
    return NULL;
}

u32 NavHolder_GetTotalNavPoints(void) {
    return GETP(gs_NavHolder.data)->numNodes;
}

/* The NavPoint of an index, or NULL (negative indices aren't checked). */
void *NavHolder_GetNavPoint(s32 index) {
    NavPoint *point = NULL;
    if (GETP(gs_NavHolder.data) != NULL && index < (s32)GETP(gs_NavHolder.data)->numNodes) {
        point = &GETP(GETP(gs_NavHolder.data)->nodes)[index];
    }
    return point;
}

s32 NavHolder_GetFirstWayPoint(void) {
    return GETP(gs_NavHolder.data)->firstWayPoint;
}
