/* Viewer mode, shared between viewer.c (the server and the preview) and
 * viewer_graph.c (schemas, a pack's nodes, field values). */
#ifndef VIEWER_INTERNAL_H
#define VIEWER_INTERNAL_H

#include <SDL3/SDL.h>
#include "common.h"
#include "Gfx.h"
#include "Port_Swap.h"
#include "jsonc.h"
#include "viewer.h"

/* What a 4-byte slot of a pack's blob holds, from its relocation tables. */
enum {
    VS_NONE,
    VS_POINTER,     /* the pointer list: a handle into this pack */
    VS_REF,         /* a local reference: the same (geometry links) */
    VS_EXPORT,      /* a share pack's export: a handle into that pack */
    VS_TEXTURE,     /* a texture id (u16 at slot + 2) */
    VS_SOUND        /* a sound id */
};

enum { VN_TYPED, VN_RAW, VN_GEOMETRY, VN_TEXTURE, VN_SHADER, VN_TRIGGER };

#define VIEWER_NO_OFFSET 0xFFFFFFFFu

typedef struct VNode {
    char *id;                   /* below the pack: "CarAInfo/0/engine", "geometry/14" */
    u8 kind;                    /* VN_* */
    const JsonNode *schema;     /* VN_TYPED: its struct's schema */
    const char *type;           /* the struct's name where known, or NULL */
    u32 off, size;              /* in the blob (VIEWER_NO_OFFSET: a texture) */
    s32 parent;                 /* the node its id continues, or -1 */
    u32 index;                  /* geometry/texture/shader/trigger N */
} VNode;

typedef struct VPack {
    int used;
    char archive[96];           /* as the game asks for it: "HW_Cars", "worlds_/sp_/..." */
    char archiveName[64];       /* its last component, the id's: "HW_Cars" */
    char name[48];              /* "car_5" */
    int type;
    u32 id;
    s32 slot;                   /* in g_StreamingDataPacks[type] */
    DataPack *pack;
    u32 size;                   /* the blob's */
    u8 *slotKind;               /* per 4 bytes: VS_* */
    s32 *owner;                 /* per 4 bytes: the node whose extent it is in, or -1 */
    u32 *refs;                  /* {slot offset, id} pairs */
    u32 numRefs;
    VNode *nodes;
    u32 numNodes, capNodes;
} VPack;

#define VIEWER_MAX_PACKS 16
extern VPack g_viewerPacks[VIEWER_MAX_PACKS];

extern const char *const g_viewerTypeNames[11];

/* viewer_graph.c */
int Viewer_LoadSchemas(const char *dir);        /* how many were read */
const JsonNode *Viewer_Schema(const char *name);
void Viewer_WriteSchemaNames(JsonWriter *w, const char *key);
/* The pointer list and reference relocations Port_ViewerNotePack kept for
 * `pack`, moved into vp; then its nodes. */
void Viewer_BuildGraph(VPack *vp);
void Viewer_FreeGraph(VPack *vp);
void Viewer_ForgetPack(DataPack *pack);
VNode *Viewer_FindNode(VPack *vp, const char *id);
void Viewer_WriteOutline(JsonWriter *w, VPack *vp);
void Viewer_WriteNode(JsonWriter *w, VPack *vp, VNode *nd);
/* 1, or 0 with a message. The field's new value is written as "value". force: an object
 * link may take a node of another type, or a raw @hex offset. */
int Viewer_SetField(JsonWriter *w, VPack *vp, VNode *nd, const char *field, const JsonNode *value, int force,
                    char *err, size_t errSize);
void Viewer_WriteNull(JsonWriter *w, const char *key);

/* viewer_frames.c: the frames shared with the browser */
int ViewerFrames_On(void);
/* Shares frames of w x h (the EFB's size from the next frame on); 0 with a message. */
int ViewerFrames_Configure(int w, int h, char *err, size_t errSize);
void ViewerFrames_Stop(void);
void ViewerFrames_EndFrame(u32 frame);
void ViewerFrames_WriteInfo(JsonWriter *w);    /* the `frames` reply's keys */
void ViewerFrames_WriteStats(JsonWriter *w);   /* "frames": {...} */
void ViewerFrames_Shutdown(void);

#endif
