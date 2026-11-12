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
/* Per 4-byte word of the blob, the node it is part of (geometry groups: their geometry), or -1; SDL_free it. */
s32 *Viewer_WordUsers(VPack *vp);

/* viewer_frames.c: the frames shared with the browser */
int ViewerFrames_On(void);
/* Shares frames of w x h (the EFB's size from the next frame on); 0 with a message. */
int ViewerFrames_Configure(int w, int h, char *err, size_t errSize);
void ViewerFrames_Stop(void);
void ViewerFrames_EndFrame(u32 frame);
void ViewerFrames_WriteInfo(JsonWriter *w);    /* the `frames` reply's keys */
void ViewerFrames_WriteStats(JsonWriter *w);   /* "frames": {...} */
void ViewerFrames_Shutdown(void);

/* A shared memory block for the browser (viewer_frames.c): POSIX shm_open
 * "/hwvx-<tag>" (mode 0600) or a Windows named mapping "Local\hwvx-<tag>". */
typedef struct ViewerShm {
    u8 *base;
    size_t size;
    char name[64];
    void *h;            /* Windows: the mapping's HANDLE */
} ViewerShm;
int ViewerShm_Create(ViewerShm *m, size_t size, const char *tag);
void ViewerShm_Destroy(ViewerShm *m);
u64 Viewer_RandomU64(void);

/* viewer_media.c: textures and sounds as the game decodes them (the media
 * commands), the audio played through the game's own mixer */
void ViewerMedia_Start(void);
/* 1 when cmd is a media command (its reply written, or *ok = 0 and err). */
int ViewerMedia_Dispatch(const char *cmd, const JsonNode *req, JsonWriter *w, int *ok, char *err, size_t errSize);
void ViewerMedia_WriteStatus(JsonWriter *w);   /* "audio": {...} */
/* A pack is about to be unloaded (its sounds go) / loaded (hasSong: it brings a song, which replaces the one held). */
void ViewerMedia_PackGoing(DataPack *pack);
void ViewerMedia_BeforeLoad(int hasSong);
void ViewerMedia_Shutdown(void);
/* The loaded pack (a viewer pack, or a share pack one of them refers to) named
 * "ARCHIVE/TYPE_ID" (base:// allowed), or NULL; *vp: the viewer's pack, if it is one. */
DataPack *Viewer_FindDataPack(const char *name, VPack **vp);
VPack *Viewer_ShownPack(void);     /* viewer.c: the pack shown, or NULL */

/* viewer.c, for viewer_world.c */
void Viewer_UnloadAll(void);                    /* every viewer pack */
void Viewer_ForgetShares(void);                 /* the share packs loaded first went (World_Unload) */
/* A pack the game loaded (the world's) as a viewer pack, shown: its graph and
 * outline; Viewer_DropPack forgets it without releasing its slot. */
VPack *Viewer_AdoptPack(const char *archive, int type, u32 id, DataPack *dp);
void Viewer_DropPack(VPack *vp);
struct CarAInfo;
void Viewer_DrawCar(struct CarAInfo *info, const Matrix4 *at); /* a car standing at `at` */
void Viewer_WorldLoaded(void);                  /* the camera to the world's start (or back to a pack) */

/* viewer_world.c: a whole world, streamed and drawn by the game's own code */
int ViewerWorld_Active(void);
int ViewerWorld_Dispatch(const char *cmd, const JsonNode *req, JsonWriter *w, int *ok, char *err, size_t errSize);
void ViewerWorld_Unload(void);
void ViewerWorld_Draw(void);                    /* the frame's own draws (tile cars) */
void ViewerWorld_Overlays(f32 fov);             /* after Gfx_EndScene */
void ViewerWorld_WriteStatus(JsonWriter *w);    /* "world": {...} or null */
u32 ViewerWorld_Pick(const f32 *origin, const f32 *dir, f32 *t, const char **kind);
int ViewerWorld_PlaceOf(u32 off, u32 size, f32 *pos, f32 *radius);
int ViewerWorld_Start(int port, f32 *pos, f32 *yaw);   /* yaw: the orbit's, radians, behind it */
int ViewerWorld_Bounds(f32 *min, f32 *max);
int ViewerWorld_NavPoint(u32 i, f32 *pos, f32 *yaw);
/* Another overlay layer (trigger volumes, ...): draw() adds its lines with
 * ViewerWorld_Line while the layer is on (`overlays` {name: true}). */
void ViewerWorld_AddOverlayHook(const char *name, void (*draw)(void));
void ViewerWorld_Line(const f32 *a, const f32 *b, u8 r, u8 g, u8 bl);
/* the structural path of the record at `off` of the world pack (World/0/tileCars/0), or "" */
void ViewerWorld_PathOf(u32 off, char *path, size_t size);

/* viewer_graph.c: a struct's fields at an offset of a pack, by its schema */
int Viewer_WriteStruct(JsonWriter *w, const char *key, VPack *vp, const char *schema, u32 off);

/* viewer_level.c: the level editor's commands (level, records, record, marks) */
int ViewerLevel_Dispatch(const char *cmd, const JsonNode *req, JsonWriter *w, int *ok, char *err, size_t errSize);
void ViewerLevel_Collect(void);     /* the level's volumes and the marks, into the overlay lines */
int ViewerLevel_HasLines(void);
void ViewerLevel_WorldGone(void);

#endif
