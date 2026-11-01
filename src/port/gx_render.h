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
typedef struct GXSampler {
    u8 minLinear, magLinear, mip, wrapS, wrapT;
} GXSampler;

typedef struct GXDrawCall {
    int prim;                 /* GXR_* (quads arrive as triangles) */
    const GXVertex *v;
    u32 n;
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
    /* Textures: RGBA8, top row first. A texture is uploaded once, all its
     * levels before its first draw, and never changed afterwards. */
    void *(*texCreate)(int w, int h, int levels);
    void (*texUpload)(void *tex, int level, int w, int h, const u8 *rgba);
    void (*texDestroy)(void *tex);
    void (*draw)(const GXDrawCall *dc);
    void (*clear)(const f32 color[4], f32 depth);
    /* An RGBA image stretched over dst (x0, y0, x1, y1, EFB pixels). */
    void (*drawImage)(const u8 *rgba, int w, int h, const int dst[4]);
    /* EFB src rectangle (x0, y0, x1, y1) to the window, letterboxed to 4:3. */
    void (*copyDisp)(const int src[4]);
    /* The EFB, top row first, efbW * efbH * 4 bytes; 0 on failure. */
    int (*readEFB)(u8 *rgba);
    void (*present)(void);
} GXRenderer;

extern const GXRenderer g_gxGL;
extern const GXRenderer g_gxSDLGPU;
extern const GXRenderer *g_gxr;

/* The largest area of aspect aw:ah centred in a w x h window: x, y, width,
 * height (top-left origin). */
void GX_Letterbox(int w, int h, int aw, int ah, int out[4]);

/* gx_tex.c: the decoded texture for a texture object, and its sampler. */
void *GX_TexGet(const GXTexObj *obj, GXSampler *samp);

#endif
