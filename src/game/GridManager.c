#include "common.h"


typedef struct GridNode {
    u32 data;
    u32 id;
    PTR32(struct GridNode) next;
    PTR32(struct GridNode) prev;
    PTR32(struct GridNode) objNext;
    PTR32(struct GridCell) cell;
} GridNode;

typedef struct GridObject {
    u32 data;
    u32 id;
    PTR32(GridNode) nodes;
    s32 x0;
    s32 x1;
    s32 z0;
    s32 z1;
    PTR32(struct GridObject) next;
} GridObject;

typedef struct GridCell {
    PTR32(GridNode) head;
    s32 count;
} GridCell;

typedef struct GridManager {
    GridCell cells[7][256];
    u32 numX;
    u32 numZ;
    u32 numCells;
    u8 pad380C[0x3810 - 0x380C];
    Vector4 min;
    Vector4 max;
    f32 cellSizeX;
    f32 cellSizeZ;
    GridNode nodes[4048];
    PTR32(GridNode) freeNodes;
    GridObject objects[1024];
    PTR32(GridObject) freeObjects;
    s32 numFreeObjects;
    u8 pad233C4[0x233D0 - 0x233C4];
} GridManager;

GridManager gGridManager;

static inline void Grid_GetCell(Vector4 *pos, s32 *cellX, s32 *cellZ) {
    f32 fx;
    f32 fz;

    fx = (pos->x - gGridManager.min.x) / gGridManager.cellSizeX;
    if (fx < 0.0f) {
        fx = 0.0f;
    } else if (fx >= (f32)gGridManager.numX) {
        fx = (f32)gGridManager.numX - 1.0f;
    }
    fz = (pos->z - gGridManager.min.z) / gGridManager.cellSizeZ;
    if (fz < 0.0f) {
        fz = 0.0f;
    } else if (fz >= (f32)gGridManager.numZ) {
        fz = (f32)gGridManager.numZ - 1.0f;
    }
    *cellX = fx;
    *cellZ = fz;
}

void Grid_Init(Vector4 *min, Vector4 *max, u32 numX, u32 numZ) {
    u16 i;
    u16 j;

    gGridManager.numX = numX;
    gGridManager.numZ = numZ;
    gGridManager.numCells = numX * numZ;
    gGridManager.min = *min;
    gGridManager.max = *max;
    gGridManager.cellSizeX = (max->x - min->x) / (f32)numX;
    gGridManager.cellSizeZ = (max->z - min->z) / (f32)numZ;
    for (i = 0; i < 7; i++) {
        for (j = 0; j < gGridManager.numCells; j++) {
            gGridManager.cells[i][j].count = 0;
            SETP(gGridManager.cells[i][j].head, NULL);
        }
    }
    SETP(gGridManager.freeNodes, NULL);
    for (i = 0; i < 4048; i++) {
        SETP(gGridManager.nodes[i].next, GETP(gGridManager.freeNodes));
        SETP(gGridManager.freeNodes, &gGridManager.nodes[i]);
    }
    SETP(gGridManager.freeObjects, NULL);
    gGridManager.numFreeObjects = 0;
    for (i = 0; i < 1024; i++) {
        SETP(gGridManager.objects[i].next, GETP(gGridManager.freeObjects));
        SETP(gGridManager.freeObjects, &gGridManager.objects[i]);
        gGridManager.numFreeObjects++;
    }
}

static inline void Grid_PushFreeNode(GridNode *node) {
    SETP(node->next, GETP(gGridManager.freeNodes));
    SETP(gGridManager.freeNodes, node);
}

static inline void Grid_RemoveNodes(GridObject *obj) {
    GridNode *node;
    GridNode *next;

    for (node = GETP(obj->nodes); node != NULL; node = next) {
        next = GETP(node->objNext);
        if (GETP(node->next) != NULL) {
            SETP(GETP(node->next)->prev, GETP(node->prev));
        }
        if (GETP(node->prev) != NULL) {
            SETP(GETP(node->prev)->next, GETP(node->next));
        } else {
            SETP(GETP(node->cell)->head, GETP(node->next));
        }
        GETP(node->cell)->count--;
        Grid_PushFreeNode(node);
    }
}

static inline void Grid_InsertNodes(GridObject *obj) {
    GridNode *node;
    GridNode *newNode;
    GridNode *prev;
    u32 x;
    u32 z;

    prev = NULL;
    for (x = obj->x0; x <= obj->x1; x++) {
        for (z = obj->z0; z <= obj->z1; z++) {
            node = GETP(gGridManager.freeNodes);
            newNode = NULL;
            if (node != NULL) {
                SETP(gGridManager.freeNodes, GETP(node->next));
                newNode = node;
                *(GridResult *)node = *(GridResult *)obj;
                SETP(node->objNext, prev);
                SETP(node->cell, &gGridManager.cells[node->data][x * gGridManager.numZ + z]);
                SETP(node->prev, NULL);
                SETP(node->next, GETP(gGridManager.cells[node->data][x * gGridManager.numZ + z].head));
                if (GETP(node->next) != NULL) {
                    SETP(GETP(node->next)->prev, node);
                }
                SETP(gGridManager.cells[node->data][x * gGridManager.numZ + z].head, node);
                gGridManager.cells[node->data][x * gGridManager.numZ + z].count++;
            }
            prev = newNode;
        }
    }
    SETP(obj->nodes, prev);
}

void Grid_Release(void) {

}

GridObject *Grid_AddObject(u32 data, u32 id, void *min, void *max) {
    u32 z;
    u32 x;
    GridObject *obj;
    GridObject *result = NULL;
    GridNode *prev;
    GridNode *newNode;
    GridNode *node;

    obj = GETP(gGridManager.freeObjects);
    if (obj != NULL) {
        SETP(gGridManager.freeObjects, GETP(obj->next));
        gGridManager.numFreeObjects--;
        Grid_GetCell(min, &obj->x0, &obj->z0);
        Grid_GetCell(max, &obj->x1, &obj->z1);
        obj->data = data;
        obj->id = id;
        prev = NULL;
        for (x = obj->x0; x <= obj->x1; x++) {
            for (z = obj->z0; z <= obj->z1; z++) {
                node = GETP(gGridManager.freeNodes);
                newNode = NULL;
                if (node != NULL) {
                    SETP(gGridManager.freeNodes, GETP(node->next));
                    newNode = node;
                    *(GridResult *)node = *(GridResult *)obj;
                    SETP(node->objNext, prev);
                    SETP(node->cell, &gGridManager.cells[node->data][x * gGridManager.numZ + z]);
                    SETP(node->prev, NULL);
                    SETP(node->next, GETP(gGridManager.cells[node->data][x * gGridManager.numZ + z].head));
                    if (GETP(node->next) != NULL) {
                        SETP(GETP(node->next)->prev, node);
                    }
                    SETP(gGridManager.cells[node->data][x * gGridManager.numZ + z].head, node);
                    gGridManager.cells[node->data][x * gGridManager.numZ + z].count++;
                }
                prev = newNode;
            }
        }
        SETP(obj->nodes, prev);
        SETP(obj->next, NULL);
        result = obj;
    }
    return result;
}

void Grid_MoveObject(GridObject *obj, Vector4 *min, Vector4 *max) {
    GridNode *next;
    GridNode *cur;
    u32 z;
    u32 x;
    GridNode *prev;
    GridNode *newNode;
    GridNode *node;
    u32 x0;
    u32 z0;
    u32 x1;
    u32 z1;

    if (obj != NULL) {
        Grid_GetCell(min, (s32 *)&x0, (s32 *)&z0);
        Grid_GetCell(max, (s32 *)&x1, (s32 *)&z1);
        if (x0 != obj->x0 || z0 != obj->z0 || x1 != obj->x1 || z1 != obj->z1) {
            for (cur = GETP(obj->nodes); cur != NULL; cur = next) {
                next = GETP(cur->objNext);
                if (GETP(cur->next) != NULL) {
                    SETP(GETP(cur->next)->prev, GETP(cur->prev));
                }
                if (GETP(cur->prev) != NULL) {
                    SETP(GETP(cur->prev)->next, GETP(cur->next));
                } else {
                    SETP(GETP(cur->cell)->head, GETP(cur->next));
                }
                GETP(cur->cell)->count--;
                Grid_PushFreeNode(cur);
            }
            obj->x0 = x0;
            obj->z0 = z0;
            obj->x1 = x1;
            obj->z1 = z1;
            prev = NULL;
            for (x = obj->x0; x <= obj->x1; x++) {
                for (z = obj->z0; z <= obj->z1; z++) {
                    node = GETP(gGridManager.freeNodes);
                    newNode = NULL;
                    if (node != NULL) {
                        SETP(gGridManager.freeNodes, GETP(node->next));
                        newNode = node;
                        *(GridResult *)node = *(GridResult *)obj;
                        SETP(node->objNext, prev);
                        SETP(node->cell, &gGridManager.cells[node->data][x * gGridManager.numZ + z]);
                        SETP(node->prev, NULL);
                        SETP(node->next, GETP(gGridManager.cells[node->data][x * gGridManager.numZ + z].head));
                        if (GETP(node->next) != NULL) {
                            SETP(GETP(node->next)->prev, node);
                        }
                        SETP(gGridManager.cells[node->data][x * gGridManager.numZ + z].head, node);
                        gGridManager.cells[node->data][x * gGridManager.numZ + z].count++;
                    }
                    prev = newNode;
                }
            }
            SETP(obj->nodes, prev);
            SETP(obj->next, NULL);
        }
    }
}

void Grid_FreeObject(GridObject *obj) {
    if (obj != NULL) {
        Grid_RemoveNodes(obj);
        SETP(obj->next, GETP(gGridManager.freeObjects));
        SETP(gGridManager.freeObjects, obj);
        gGridManager.numFreeObjects++;
    }
}

u32 Grid_GetObjects(Vector4 *min, Vector4 *max, u32 typeMask, GridResult *results, u32 maxResults) {
    u32 count;
    u32 x;
    u32 z;
    u32 type;
    GridNode *node;
    u32 i;
    s8 found;
    u32 x0;
    u32 z0;
    u32 x1;
    u32 z1;

    count = 0;
    Grid_GetCell(min, (s32 *)&x0, (s32 *)&z0);
    Grid_GetCell(max, (s32 *)&x1, (s32 *)&z1);
    for (x = x0; x <= x1; x++) {
        for (z = z0; z <= z1; z++) {
            for (type = 0; type < 7; type++) {
                if (typeMask & (1 << type)) {
                    node = GETP(gGridManager.cells[type][x * gGridManager.numZ + z].head);
                    while (node != NULL) {
                        found = 0;
                        for (i = 0; i < count && !found; i++) {
                            if ((results + i)->id == ((GridResult *)node)->id) {
                                found = 1;
                            }
                        }
                        if (!found) {
                            if (count < maxResults) {
                                results[count] = *(GridResult *)node;
                                count++;
                            } else {
                                Dprintf("%d\n", count);
                                break;
                            }
                        }
                        node = GETP(node->next);
                    }
                }
            }
        }
    }
    return count;
}

u32 Grid_GetNearestObjects(Vector4 *min, Vector4 *max, u32 typeMask, GridResult *results, u32 maxResults, s8 (*filter)(GridNode *, s32), s32 filterArg) {
    s8 first;
    u32 count;
    u32 x;
    u32 z;
    u32 type;
    GridResult *result;
    u32 i;
    s8 found;
    GridNode *node;
    u32 x0;
    u32 z0;
    u32 x1;
    u32 z1;

    first = 1;
    count = 0;
    Grid_GetCell(min, (s32 *)&x0, (s32 *)&z0);
    Grid_GetCell(max, (s32 *)&x1, (s32 *)&z1);
    while (count == 0) {
        for (x = x0; x <= x1; x++) {
            for (z = z0; z <= z1; z++) {
                if (first || x == x0 || x == x1 || z == z0 || z == z1) {
                    for (type = 0; type < 7; type++) {
                        if (typeMask & (1 << type)) {
                            node = GETP(gGridManager.cells[type][x * gGridManager.numZ + z].head);
                            while (node != NULL) {
                                result = results;
                                found = 0;
                                for (i = 0; i < count && !found; i++) {
                                    if (result->id == node->id) {
                                        found = 1;
                                    }
                                    result++;
                                }
                                if (!found) {
                                    if (count < maxResults) {
                                        if (filter == NULL || filter(node, filterArg) == 1) {
                                            results[count] = *(GridResult *)node;
                                            count++;
                                        }
                                    } else {
                                        node = NULL;
                                    }
                                }
                                if (node != NULL) {
                                    node = GETP(node->next);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (count == 0) {
            first = 0;
            if (x0 == 0 && z0 == 0 && x1 >= gGridManager.numX - 1 && z1 >= gGridManager.numZ - 1) {
                break;
            }
            if (x0 != 0) {
                x0--;
            }
            if (z0 != 0) {
                z0--;
            }
            if (x1 < gGridManager.numX - 1) {
                x1++;
            } else {
                x1 = gGridManager.numX - 1;
            }
            if (z1 < gGridManager.numZ - 1) {
                z1++;
            } else {
                z1 = gGridManager.numZ - 1;
            }
        }
    }
    return count;
}

