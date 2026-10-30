#include "common.h"



typedef struct GeometryEntry {
    void *geometry;
    char name[0x40];
    s16 refCount;
    u8 unk46[2];
} GeometryEntry;

typedef struct GeometryHandler {
    GeometryEntry entries[0x800];
    s32 count;
    u32 max;
} GeometryHandler;

GeometryHandler *g_pGeometryHandler;


s32 Geometry_Create(void) {
    extern GeometryHandler *g_pGeometryHandler;
    u32 i;

    g_pGeometryHandler = Allocate(sizeof(GeometryHandler), 0, "");
    if (g_pGeometryHandler == NULL) {
        return 0;
    }
    g_pGeometryHandler->max = 0x800;
    g_pGeometryHandler->count = 0;
    for (i = 0; i < g_pGeometryHandler->max; i++) {
        g_pGeometryHandler->entries[i].geometry = NULL;
        g_pGeometryHandler->entries[i].refCount = 0;
    }
    return 1;
}

void Geometry_Destroy(void) {
    extern GeometryHandler *g_pGeometryHandler;
    Geometry_Init(1);
    Deallocate(g_pGeometryHandler);
    g_pGeometryHandler = NULL;
}

void Geometry_Init(s8 clear) {
    extern GeometryHandler *g_pGeometryHandler;
    u32 i;

    for (i = 0; i < g_pGeometryHandler->max; i++) {
        if (clear == 1) {
            g_pGeometryHandler->entries[i].refCount = 0;
        }
        Geometry_FreeGeometry(g_pGeometryHandler->entries[i].geometry);
    }
}

void Geometry_FreeGeometry(void *geometry) {
    extern GeometryHandler *g_pGeometryHandler;
    u32 i;

    if (geometry != NULL) {
        for (i = 0; i < g_pGeometryHandler->max; i++) {
            if (g_pGeometryHandler->entries[i].geometry == geometry) {
                g_pGeometryHandler->entries[i].refCount--;
                if (g_pGeometryHandler->entries[i].refCount < 1) {
                    Deallocate(geometry);
                    g_pGeometryHandler->entries[i].geometry = NULL;
                    g_pGeometryHandler->entries[i].name[0] = 0;
                    g_pGeometryHandler->count--;
                }
            }
        }
    }
}

void *Geometry_Get(char *name) {
    typedef struct {
        u8 pad0[0x16];
        u16 texIndex;
        u16 texHandle;
        u8 pad1a[6];
    } GeoMaterial;
    typedef struct {
        u16 unk0;
        u16 numMaterials;
        u32 unk4;
        GeoMaterial *materials;
        u32 unkC;
        u32 unk10;
        u8 *textures;
    } GeoMesh;
    typedef struct {
        u16 unk0;
        u16 numMeshes;
        u32 unk4;
        GeoMesh *meshes;
    } Geometry;
    extern GeometryHandler *g_pGeometryHandler;
    char path[256];
    char *ext;
    Geometry *geo;
    u32 i;
    u32 j;
    u32 slot = -1;
    GeoMesh *mesh;

    if (name == NULL) {
        return NULL;
    }
    strcpy(path, name);
    ext = strrchr(path, '.');
    if (ext != NULL && ext[1] == 'g') {
        *ext = 0;
    } else {
        ext = path + strlen(path);
    }
    for (j = 0; j < g_pGeometryHandler->max; j++) {
        if (g_pGeometryHandler->entries[j].geometry != NULL &&
            stricmp(g_pGeometryHandler->entries[j].name, path) == 0) {
            g_pGeometryHandler->entries[j].refCount++;
            return g_pGeometryHandler->entries[j].geometry;
        }
        if (g_pGeometryHandler->entries[j].geometry == NULL && slot == -1) {
            slot = j;
        }
    }
    if (slot == -1) {
        return NULL;
    }
    strcpy(g_pGeometryHandler->entries[slot].name, path);
    strcat(path, ".gn");
    geo = LoadDataChunkFile(path);
    if (geo == NULL) {
        return NULL;
    }
    g_pGeometryHandler->entries[slot].refCount++;
    g_pGeometryHandler->entries[slot].geometry = geo;
    g_pGeometryHandler->count++;
    for (i = 0; i < geo->numMeshes; i++) {
        mesh = &geo->meshes[i];
        for (j = 0; j < mesh->numMaterials; j++) {
            if (mesh->materials[j].texIndex != 0xFFFF) {
                mesh->materials[j].texHandle = Gfx_LoadTexture(mesh->textures + mesh->materials[j].texIndex);
            }
        }
    }
    return geo;
}

