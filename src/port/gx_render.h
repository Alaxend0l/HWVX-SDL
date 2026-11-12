/* Renderer backends for the GX emulation.
 *
 * gx_draw.c turns the GX state into GXDrawCalls (shader uniforms already
 * packed) and runs the EFB/display-copy logic; a backend only executes them:
 *
 * - gx_gl.c: OpenGL 3.3 core (--renderer opengl);
 * - gx_sdlgpu.c: SDL_GPU, which runs on Vulkan, Metal or Direct3D 12
 *   (--renderer vulkan|metal|d3d12).
 *
 * The EFB has the window's size and shape. Coordinates handed to a backend
 * are EFB pixels with a top-left origin; gx_draw.c maps the game's 640x448
 * screen into them (see "screen mapping" there). */
#ifndef GX_RENDER_H
#define GX_RENDER_H

#include "gx_internal.h"

/* Uniform blocks, std140: VSBlock in shaders/gx.vert and FSBlock in
 * shaders/gx.frag. Every member is a 16-byte vec4/ivec4 (array), so the C
 * layout matches std140 as is. */
typedef struct GXVSUniforms {
    f32 posMtx[3 * (GX_POSMTX_IDENTITY + 1)][4];
    f32 texMtx[3 * GX_NUM_TEXMTX][4];
    f32 proj[4][4];           /* rows */
    s32 texGen[8][4];         /* func, src, mtx slot (-1 = identity), 0 */
    s32 chan[4][4];           /* enable, ambSrc, matSrc, lightMask */
    s32 chanFn[4][4];         /* diffFn, attnFn */
    f32 amb[2][4];
    f32 mat[2][4];
    f32 lightPos[8][4];
    f32 lightDir[8][4];
    f32 lightColor[8][4];
    f32 lightA[8][4];
    f32 lightK[8][4];
    s32 counts[4];            /* numTexGens, numChans */
} GXVSUniforms;

typedef struct GXFSUniforms {
    s32 order[16][4];         /* texcoord, texmap (255: none), channel (255: none) */
    s32 cin[16][4];
    s32 ain[16][4];
    s32 cop[16][4];           /* op, bias, scale, clamp */
    s32 aop[16][4];
    s32 out[16][4];           /* color out reg, alpha out reg */
    f32 reg[4][4];
    s32 alphaComp[4];         /* comp0, ref0, op, comp1 */
    s32 misc[4];              /* numStages, alphaRef1, fog type (0: none), debug */
    f32 fogParam[4];          /* start, end, near, far */
    f32 fogColor[4];
} GXFSUniforms;

_Static_assert(sizeof(GXVSUniforms) == 128 * 16, "GXVSUniforms must match VSBlock");
_Static_assert(sizeof(GXFSUniforms) == 104 * 16, "GXFSUniforms must match FSBlock");

enum {
    GXR_TRIANGLES, GXR_TRIANGLESTRIP, GXR_TRIANGLEFAN, GXR_LINES, GXR_LINESTRIP, GXR_POINTS
};

/* Blend factors (GXSetBlendMode's, with the source/destination color split
 * resolved). */
enum {
    GXR_BF_ZERO, GXR_BF_ONE, GXR_BF_SRC_COLOR, GXR_BF_INV_SRC_COLOR, GXR_BF_DST_COLOR,
    GXR_BF_INV_DST_COLOR, GXR_BF_SRC_ALPHA, GXR_BF_INV_SRC_ALPHA, GXR_BF_DST_ALPHA, GXR_BF_INV_DST_ALPHA
};

/* Filtering and wrapping for one texture map. wrap: 0 clamp, 1 repeat,
 * 2 mirror (GX_CLAMP/GX_REPEAT/GX_MIRROR). mip: 0 none, 1 nearest, 2 linear. */
/* Texture formats. RGBA8: 4 bytes a texel. The BC formats are rows of 4x4
 * blocks (a level smaller than 4 still one block), as S3TC has them: BC1 (DXT1,
 * 8 bytes a block; its 3-colour blocks' fourth colour transparent black),
 * BC1_RGB (the same drawn opaque, the fourth colour black: OpenGL's
 * COMPRESSED_RGB_S3TC_DXT1, which SDL_GPU lacks), BC3 (DXT5, 16 bytes). */
enum { GXR_TEX_RGBA8, GXR_TEX_BC1, GXR_TEX_BC1_RGB, GXR_TEX_BC3, GXR_TEX_COUNT };

static inline u32 GXR_TexLevelBytes(int fmt, int w, int h) {
    if (fmt == GXR_TEX_RGBA8) {
        return (u32)w * (u32)h * 4;
    }
    return (u32)((w + 3) / 4) * (u32)((h + 3) / 4) * (fmt == GXR_TEX_BC3 ? 16u : 8u);
}

typedef struct GXSampler {
    u8 minLinear, magLinear, mip, wrapS, wrapT;
} GXSampler;

/* A vertex of the static meshes (gx_mesh.c): the attributes pack geometry
 * has, in GXVertex's encodings. The attributes GXVertex has beyond these
 * (clr[1], tex[1..7]) read as zero, as they are in a decoded vertex. mtxIdx
 * is only read for a skinned mesh; the others take the draw's matrix slot.
 * 32 bytes. */
typedef struct GXMeshVertex {
    f32 pos[3];
    s8 nrm[4];           /* fixed scale 1/64 */
    u8 clr[4];
    f32 tex[2];
    f32 mtxIdx;          /* position matrix slot (skinned meshes) */
} GXMeshVertex;

_Static_assert(sizeof(GXMeshVertex) == 32, "GXMeshVertex size");

/* A draw from a static mesh: a triangle list of `count` vertices from
 * `first`, or with idx, of `count` u16 indices from index `first`. */
typedef struct GXMeshDraw {
    void *vtx;                /* meshVertices */
    void *idx;                /* meshIndices, or NULL: vertices in order */
    u32 first, count;
    f32 slot;                 /* matrix slot of every vertex (unskinned meshes) */
} GXMeshDraw;

typedef struct GXDrawCall {
    int prim;                 /* GXR_* (quads arrive as triangles) */
    const GXVertex *v;
    u32 n;
    /* Set instead of v/n for a draw from a static mesh (prim GXR_TRIANGLES). */
    const GXMeshDraw *mesh;
    /* Set when v points into a GXVtxBatch the renderer may already hold on the
     * GPU (the display list cache's vertex pool): a backend can then keep that
     * pool resident and draw from an offset instead of uploading every draw.
     * NULL when the vertices are a scratch buffer that changes each draw. */
    const GXVtxBatch *batch;
    const GXVSUniforms *vs;
    const GXFSUniforms *fs;
    void *tex[8];             /* backend textures (texCreate), NULL: none */
    GXSampler samp[8];
    f32 viewport[6];          /* left, top, width, height (EFB pixels), near, far */
    int scissor[4];           /* left, top, width, height (EFB pixels, inside the EFB) */
    u8 blend, blendSrc, blendDst, blendRevSub;   /* blendRevSub: dst - src, factors ONE */
    u8 depthTest, depthFunc, depthWrite;          /* depthFunc: GX_NEVER..GX_ALWAYS */
    u8 colorWrite, alphaWrite;
    u8 cull;                  /* 0 none, 1 front, 2 back, 3 all (GX_CULL_*) */
} GXDrawCall;

typedef struct GXRenderer {
    const char *name;
    /* main_sdl.c: create the window (Port_CreateWindow) and the
     * context/device; close destroys the context/device. 0: unavailable,
     * with nothing left open (main_sdl.c then falls back to OpenGL). */
    int (*open)(void);
    void (*close)(void);
    /* GXInit: create the EFB (efbW x efbH pixels) and pipeline objects. */
    void (*init)(int efbW, int efbH);
    /* Between frames: a new EFB size; its contents may be lost. */
    void (*resize)(int efbW, int efbH);
    /* Textures in a GXR_TEX_* format, top row first. A texture is uploaded
     * once, all its levels before its first draw, and never changed
     * afterwards. texFormat: 1 if the device samples fmt (RGBA8 always). */
    int (*texFormat)(int fmt);
    void *(*texCreate)(int fmt, int w, int h, int levels);
    void (*texUpload)(void *tex, int fmt, int level, int w, int h, const u8 *data);
    void (*texDestroy)(void *tex);
    void (*draw)(const GXDrawCall *dc);
    void (*clear)(const f32 color[4], f32 depth);
    /* An RGBA image stretched over dst (x0, y0, x1, y1, EFB pixels). */
    void (*drawImage)(const u8 *rgba, int w, int h, const int dst[4]);
    /* EFB src rectangle (x0, y0, x1, y1) to the window, letterboxed to 4:3. */
    void (*copyDisp)(const int src[4]);
    /* EFB src rectangle (x0, y0, x1, y1) to a texture of that size from
     * texCreate (one level, never uploaded), top row first. Draws recorded
     * before this see the old contents, later ones the new. */
    void (*copyEFB)(void *tex, const int src[4]);
    /* The EFB, top row first, efbW * efbH * 4 bytes; 0 on failure. */
    int (*readEFB)(u8 *rgba);
    void (*present)(void);
    /* Static meshes (gx_mesh.c), uploaded once and drawn until destroyed:
     * a u16 index buffer, and vertices drawn with it or (indices NULL) in
     * order. Skinned: each vertex's mtxIdx is its matrix slot, else the
     * draw's GXMeshDraw.slot. NULL on failure. Destroy takes either; a
     * mesh's vertices go before its indices. */
    void *(*meshIndices)(const u16 *idx, u32 n);
    void *(*meshVertices)(const GXMeshVertex *v, u32 n, int skinned, void *indices);
    void (*meshDestroy)(void *buf);
    /* Asynchronous EFB readback (the viewer's shared frames, viewer_frames.c):
     * grabStart copies the EFB as drawn so far into readback buffer `slot`
     * (0 .. GXR_GRAB_SLOTS - 1) and submits the work without waiting for the
     * GPU; 0 on failure. grabFetch, once the GPU is done with it, copies it
     * out (4 bytes a pixel, R G B and a byte to ignore, top row first, rows
     * `stride` bytes apart; nothing when it is over maxW x maxH), sets *w, *h
     * and frees the slot: 1. 0: not ready yet (wait: blocks until it is);
     * -1: nothing pending, too big, or failed (the slot is freed). */
    int (*grabStart)(int slot);
    int (*grabFetch)(int slot, u8 *dst, int stride, int maxW, int maxH, int *w, int *h, int wait);
} GXRenderer;

#define GXR_GRAB_SLOTS 2

/* HWVX_STATS: draws with a specialised pixel shader (gx_tevgen.h) and with
 * the general one since the last report, and how many specialised programs
 * exist / are still compiling. Backends without them leave it alone. */
typedef struct GXShaderStats {
    u32 special, general, programs, pending;
} GXShaderStats;

extern GXShaderStats g_gxShaderStats;

/* Vertex and index bytes the renderer has sent to the GPU (HWVX_STATS). */
extern u64 g_gxUploadBytes;

extern const GXRenderer g_gxGL;
extern const GXRenderer g_gxSDLGPU;
extern const GXRenderer *g_gxr;

/* The largest area of aspect aw:ah centred in a w x h window: x, y, width,
 * height (top-left origin). */
void GX_Letterbox(int w, int h, int aw, int ah, int out[4]);

/* gx_tex.c: the decoded texture for a texture object, and its sampler. */
void *GX_TexGet(const GXTexObj *obj, GXSampler *samp);
/* gx_tex.c, HWVX_STATS: texture cache misses so far (GPU textures created:
 * decoded to RGBA8 or kept as stored blocks; shared with a texture of the
 * same content) and the time they took (performance counter ticks); the GPU
 * textures resident, their bytes, and the bytes they'd take as RGBA8 with one
 * texture per texture object (as before step 2b). */
typedef struct GXTexStats {
    u32 created, decoded, kept, shared;
    u32 inexact;    /* of decoded: CMPR that BC1 would draw differently */
    u64 ticks;
    u32 textures;
    u64 bytes, rgbaBytes;
} GXTexStats;
void GX_TexStats(GXTexStats *st);
/* The game freed [p, p+size): the textures whose images were there go. */
void GX_TexInvalidate(const void *p, u32 size);
/* gx_tex.c, for the data browser (viewer_media.c): a texture object as the
 * renderer draws it. Info: 0 for no texture (no image, or past 1024). */
typedef struct GXPortTexInfo {
    int fmt, w, h, levels;          /* GX_TF_* or GX_TF_PORT | PORT_TEX_PC_* */
    int wrapS, wrapT;               /* GX_CLAMP / REPEAT / MIRROR */
    int minLinear, magLinear, mip;  /* the sampler's (GXSampler) */
    int squared;                    /* the PC's colours squared (Port_PCTexturesLookGC) */
    int mod;                        /* a mod's colour recipe applies (gx_texmod.h) */
    u32 bytes;                      /* every level's stored bytes */
} GXPortTexInfo;
int GX_PortTexObjInfo(const GXTexObj *obj, GXPortTexInfo *info);
/* Level `level` of it into rgba (the level's w x h RGBA8), decoded as the
 * texture cache does for an RGBA8 upload: the same decoder, a mod's recipe,
 * the squared colours. 0 if there is no such level. */
int GX_PortDecodeTexObj(const GXTexObj *obj, int level, u8 *rgba);
/* GXCopyTex: texture objects on dest's memory now sample this EFB rectangle. */
void GX_TexCopyEFB(void *dest, const int src[4]);

#endif
