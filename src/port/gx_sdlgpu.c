/* SDL_GPU renderer: Vulkan, Metal or Direct3D 12 (--renderer vulkan|metal|
 * d3d12), with the shaders precompiled to SPIR-V, MSL and DXIL
 * (gx_shaders.h, tools/gen_shaders.py).
 *
 * SDL_GPU can't upload data inside a render pass, and the game uploads
 * textures between draws, so draws are recorded and run in batches (Flush):
 * one copy pass for the pending texture uploads and all recorded vertices,
 * then one render pass on the EFB. A batch ends when something needs the
 * EFB contents (display copy, texture copy, movie frame, dump), at a clear, and
 * at present.
 * Textures replaced in the cache are released only after the batch that may
 * still use them.
 *
 * SDL_GPU uses a top-left origin for viewports and textures on every driver,
 * so rectangles from gx_draw.c are used as they are.
 *
 * Pixel shaders: the setups in tools/tevkeys.txt have a specialised shader
 * (gx_tevgen.h) precompiled into gx_shaders.h; a draw with another setup
 * uses the general gx.frag. Which one is part of the pipeline key. */
#include "gx_render.h"
#include "gx_shaders.h"
#include "gx_tevgen.h"

#ifndef GX_NUM_TEV_VARIANTS   /* a gx_shaders.h from before the variants */
#define GX_NUM_TEV_VARIANTS 0
#endif

#define MAX_PIPELINES 1024
#define MAX_SAMPLERS 64
#define STAGING_ALIGN 512   /* D3D12 texture upload placement alignment */

typedef struct Upload {
    SDL_GPUTexture *tex;
    u32 level, w, h, offset;
} Upload;

static const SDL_GPUTextureFormat s_texFmt[GXR_TEX_COUNT] = {
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTUREFORMAT_INVALID,
    SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM,
};

/* A static mesh's buffer upload, queued for the next copy pass. */
typedef struct BufUpload {
    SDL_GPUBuffer *buf;
    u32 offset, size;
} BufUpload;

/* A static mesh's buffer (gx_mesh.c): indices, or vertices (GXMeshVertex). */
typedef struct GPUMesh {
    SDL_GPUBuffer *buf;
    u32 bytes;
    int skinned;
} GPUMesh;

/* Vertex layouts, part of the pipeline key: decoded vertices (GXVertex), or
 * a static mesh's with each vertex's matrix slot (skinned) or the draw's. */
enum { LAYOUT_GXVERTEX, LAYOUT_MESH_SKINNED, LAYOUT_MESH };

typedef struct Draw {
    SDL_GPUGraphicsPipeline *pipeline;
    u32 firstVertex, numVertices;   /* an indexed mesh: first index, index count */
    SDL_GPUBuffer *meshVtx, *meshIdx;   /* NULL: vertices in vbuf */
    u32 meshSlot;
    u32 vs, fs;   /* indices into the uniform snapshots */
    SDL_GPUTexture *tex[8];
    SDL_GPUSampler *samp[8];
    SDL_GPUViewport viewport;
    SDL_Rect scissor;
} Draw;

static struct {
    SDL_GPUDevice *dev;
    SDL_GPUTextureFormat swapFmt, depthFmt;
    SDL_GPUShader *vs, *fs, *presentVS, *presentFS;
    SDL_GPUGraphicsPipeline *present;
    SDL_GPUTexture *efb, *efbDepth, *black, *image;
    int imageW, imageH;
    SDL_GPUSampler *linearClamp;
    int efbW, efbH;
    u8 texFormats[GXR_TEX_COUNT];   /* GXR_TEX_*: the device samples it */

    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *swapTex;
    u32 swapW, swapH;

    /* the batch being recorded */
    GXVertex *verts;
    u32 nVerts, capVerts;
    Draw *draws;
    u32 nDraws, capDraws;
    GXVSUniforms *vsU;
    u32 nVS, capVS;
    GXFSUniforms *fsU;
    u32 nFS, capFS;
    u8 *staging;
    u32 nStaging, capStaging;
    Upload *uploads;
    u32 nUploads, capUploads;
    SDL_GPUTexture **released;
    u32 nReleased, capReleased;
    BufUpload *bufUploads;
    u32 nBufUploads, capBufUploads;
    SDL_GPUBuffer **releasedBufs;
    u32 nReleasedBufs, capReleasedBufs;
    /* Static meshes read the attributes they don't have from here, one
     * 16-byte record per matrix slot k: {f32 k, zeros}, bound per draw at
     * instance rate (the slot) - see CreatePipeline. */
    SDL_GPUBuffer *meshConst;
    int clearPending;
    SDL_FColor clearColor;
    f32 clearDepth;

    SDL_GPUBuffer *vbuf;
    u32 vbufSize;
    SDL_GPUTransferBuffer *tbuf;
    u32 tbufSize;

    /* the pixel shader for the last FSBlock recorded: 0 gx.frag, n variant
     * n - 1; variants' shaders are created on first use */
    u32 curFrag;
    int tevGen;
    int vsyncApplied;               /* Port_VSyncWanted() the present mode follows; -1 none yet */
#if GX_NUM_TEV_VARIANTS > 0
    SDL_GPUShader *tevFS[GX_NUM_TEV_VARIANTS];
#endif

    struct { u32 key; SDL_GPUGraphicsPipeline *p; } pipes[MAX_PIPELINES];
    u32 nPipes;
    struct { u32 key; SDL_GPUSampler *s; } samplers[MAX_SAMPLERS];
    u32 nSamplers;
} s;

/* The asynchronous readbacks (grabStart/grabFetch): a download buffer each,
 * and the fence of the submit that fills it. */
static struct {
    SDL_GPUTransferBuffer *tb;
    u32 size;
    SDL_GPUFence *fence;
    int w, h;
} s_grab[GXR_GRAB_SLOTS];

/* Grows *p (elements of size elem) to hold at least need. */
static void Grow(void **p, u32 *cap, u32 need, size_t elem) {
    if (need > *cap) {
        u32 n = *cap ? *cap : 64;
        while (n < need) {
            n *= 2;
        }
        *p = SDL_realloc(*p, n * elem);
        if (*p == NULL) {
            Port_Fatal("out of memory (renderer)");
        }
        *cap = n;
    }
}

static SDL_GPUCommandBuffer *Cmd(void) {
    if (s.cmd == NULL) {
        s.cmd = SDL_AcquireGPUCommandBuffer(s.dev);
        if (s.cmd == NULL) {
            Port_Fatal("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        }
    }
    return s.cmd;
}

/* ---- setup --------------------------------------------------------------- */

static int GPU_Open(void) {
    SDL_WindowFlags flags = 0;
    int debug = SDL_getenv("HWVX_GPUDEBUG") != NULL;

    if (SDL_strcmp(g_port.gpuDriver, "vulkan") == 0) {
        flags = SDL_WINDOW_VULKAN;
    } else if (SDL_strcmp(g_port.gpuDriver, "metal") == 0) {
        flags = SDL_WINDOW_METAL;
    }
    /* Any of these can fail where the API is missing or has no usable driver
     * (a Vulkan loader without a GPU driver fails the window already): then
     * report 0 for main_sdl.c to fall back to OpenGL. */
    if (!SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
                                      g_port.gpuDriver) ||
        !Port_TryCreateWindow(flags)) {
        Port_Log("The %s renderer is unavailable: %s", g_port.gpuDriver, SDL_GetError());
        return 0;
    }
    s.dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
                                debug, g_port.gpuDriver);
    if (s.dev == NULL || !SDL_ClaimWindowForGPUDevice(s.dev, g_port.window)) {
        Port_Log("The %s renderer is unavailable: %s", g_port.gpuDriver, SDL_GetError());
        if (s.dev != NULL) {
            SDL_DestroyGPUDevice(s.dev);
            s.dev = NULL;
        }
        SDL_DestroyWindow(g_port.window);
        g_port.window = NULL;
        return 0;
    }
    s.swapFmt = SDL_GetGPUSwapchainTextureFormat(s.dev, g_port.window);
    s.vsyncApplied = -1;
    Port_Log("SDL_GPU driver %s%s", SDL_GetGPUDeviceDriver(s.dev), debug ? " (debug)" : "");
    return 1;
}

/* The Graphics menu's VSync row (and HWVX_NO_VSYNC): SDL_GPU presents with
 * VSYNC (FIFO) unless told otherwise, which would cap the frame rate at the
 * display's even with VSync off and drop the frames in between. Off asks for
 * MAILBOX (no tearing), else IMMEDIATE. Set when it changes. */
static void GPU_ApplyVSync(void) {
    int want = Port_VSyncWanted();
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;

    if (want == s.vsyncApplied) {
        return;
    }
    if (!want) {
        if (SDL_WindowSupportsGPUPresentMode(s.dev, g_port.window, SDL_GPU_PRESENTMODE_MAILBOX)) {
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        } else if (SDL_WindowSupportsGPUPresentMode(s.dev, g_port.window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        }
    }
    if (!SDL_SetGPUSwapchainParameters(s.dev, g_port.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        Port_Log("SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
    } else {
        Port_Log("present mode: %s", mode == SDL_GPU_PRESENTMODE_VSYNC ? "vsync" :
                 mode == SDL_GPU_PRESENTMODE_MAILBOX ? "mailbox" : "immediate");
    }
    s.vsyncApplied = want;
}

static SDL_GPUShader *Shader(const u8 *spv, size_t spvSize, const char *msl, const u8 *dxil, size_t dxilSize,
                             SDL_GPUShaderStage stage, u32 samplers, u32 uniformBuffers) {
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(s.dev);
    SDL_GPUShaderCreateInfo ci;
    SDL_GPUShader *sh;

    SDL_zero(ci);
    ci.stage = stage;
    ci.num_samplers = samplers;
    ci.num_uniform_buffers = uniformBuffers;
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
        ci.code = spv;
        ci.code_size = spvSize;
        ci.entrypoint = "main";
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        ci.format = SDL_GPU_SHADERFORMAT_MSL;
        ci.code = (const u8 *)msl;
        ci.code_size = SDL_strlen(msl);
        ci.entrypoint = "main0";   /* spirv-cross renames main */
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        ci.format = SDL_GPU_SHADERFORMAT_DXIL;
        ci.code = dxil;
        ci.code_size = dxilSize;
        ci.entrypoint = "main";
    } else {
        Port_Fatal("The %s renderer needs a shader format this build doesn't have", g_port.gpuDriver);
    }
    sh = SDL_CreateGPUShader(s.dev, &ci);
    if (sh == NULL) {
        Port_Fatal("SDL_CreateGPUShader failed: %s", SDL_GetError());
    }
    return sh;
}

#define SHADER(n, stage, samplers, ubos) \
    Shader(n##_spv, sizeof(n##_spv), n##_msl, n##_dxil, sizeof(n##_dxil), stage, samplers, ubos)

static SDL_GPUTexture *CreateTexture(SDL_GPUTextureFormat fmt, SDL_GPUTextureUsageFlags usage, int w, int h,
                                     int levels) {
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTexture *t;

    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    ci.usage = usage;
    ci.width = (u32)w;
    ci.height = (u32)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = (u32)levels;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    t = SDL_CreateGPUTexture(s.dev, &ci);
    if (t == NULL) {
        Port_Log("SDL_CreateGPUTexture %dx%d failed: %s", w, h, SDL_GetError());
    }
    return t;
}

static void *GPU_TexCreate(int fmt, int w, int h, int levels);
static void GPU_TexUpload(void *tex, int fmt, int level, int w, int h, const u8 *data);
static SDL_GPUBuffer *StaticBuffer(SDL_GPUBufferUsageFlags usage, const void *data, u32 size);

static void CreatePresentPipeline(void) {
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;

    SDL_zero(ci);
    SDL_zero(ct);
    ct.format = s.swapFmt;
    ci.vertex_shader = s.presentVS;
    ci.fragment_shader = s.presentFS;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    s.present = SDL_CreateGPUGraphicsPipeline(s.dev, &ci);
    if (s.present == NULL) {
        Port_Fatal("SDL_CreateGPUGraphicsPipeline (present) failed: %s", SDL_GetError());
    }
}

static void CreateEFB(void) {
    s.efb = CreateTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                          SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, s.efbW, s.efbH, 1);
    s.efbDepth = CreateTexture(s.depthFmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, s.efbW, s.efbH, 1);
    if (s.efb == NULL || s.efbDepth == NULL) {
        Port_Fatal("Could not create the EFB: %s", SDL_GetError());
    }
}

static void GPU_Init(int efbW, int efbH) {
    static const u8 black[4] = { 0, 0, 0, 255 };
    SDL_GPUSamplerCreateInfo sci;
    int i;

    s.efbW = efbW;
    s.efbH = efbH;
    s.vs = SHADER(gx_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    s.fs = SHADER(gx_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 1);
    s.presentVS = SHADER(present_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    s.presentFS = SHADER(present_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    {
        const char *e = SDL_getenv("HWVX_NO_TEVGEN");

        s.tevGen = !(e != NULL && *e == '1');
        Port_Log("tevgen: %d precompiled specialised pixel shaders%s", GX_NUM_TEV_VARIANTS,
                 s.tevGen ? "" : ", off (HWVX_NO_TEVGEN)");
    }

    /* 24-bit depth like the EFB's; Apple GPUs only have 32-bit float */
    s.depthFmt = SDL_GPUTextureSupportsFormat(s.dev, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                              SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                     ? SDL_GPU_TEXTUREFORMAT_D24_UNORM : SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    CreateEFB();
    for (i = 0; i < GXR_TEX_COUNT; i++) {
        s.texFormats[i] = s_texFmt[i] != SDL_GPU_TEXTUREFORMAT_INVALID &&
                          SDL_GPUTextureSupportsFormat(s.dev, s_texFmt[i], SDL_GPU_TEXTURETYPE_2D,
                                                       SDL_GPU_TEXTUREUSAGE_SAMPLER);
    }
    /* unbound texture maps sample opaque black, as GL's texture 0 does */
    s.black = GPU_TexCreate(GXR_TEX_RGBA8, 1, 1, 1);
    GPU_TexUpload(s.black, GXR_TEX_RGBA8, 0, 1, 1, black);

    {
        f32 consts[GX_POSMTX_IDENTITY + 1][4];
        int k;

        SDL_zero(consts);
        for (k = 0; k <= GX_POSMTX_IDENTITY; k++) {
            consts[k][0] = (f32)k;
        }
        s.meshConst = StaticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, consts, sizeof(consts));
        if (s.meshConst == NULL) {
            Port_Fatal("SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        }
    }

    SDL_zero(sci);
    sci.min_filter = sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = sci.address_mode_v = sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s.linearClamp = SDL_CreateGPUSampler(s.dev, &sci);
    CreatePresentPipeline();

    s.clearPending = 1;
    s.clearColor.a = 1.0f;
    s.clearDepth = 1.0f;
}

static void Flush(void);

/* Recorded work may still use the old EFB: SDL releases it once that is done. */
static void GPU_Resize(int efbW, int efbH) {
    Flush();
    SDL_ReleaseGPUTexture(s.dev, s.efb);
    SDL_ReleaseGPUTexture(s.dev, s.efbDepth);
    s.efbW = efbW;
    s.efbH = efbH;
    CreateEFB();
    s.clearPending = 1;
}

static void GPU_Close(void) {
    u32 i;

    if (s.dev == NULL) {
        return;
    }
    if (s.cmd != NULL) {
        SDL_SubmitGPUCommandBuffer(s.cmd);
        s.cmd = NULL;
    }
    SDL_WaitForGPUIdle(s.dev);
    for (i = 0; i < s.nPipes; i++) SDL_ReleaseGPUGraphicsPipeline(s.dev, s.pipes[i].p);
    for (i = 0; i < s.nSamplers; i++) SDL_ReleaseGPUSampler(s.dev, s.samplers[i].s);
    for (i = 0; i < s.nReleased; i++) SDL_ReleaseGPUTexture(s.dev, s.released[i]);
    for (i = 0; i < s.nReleasedBufs; i++) SDL_ReleaseGPUBuffer(s.dev, s.releasedBufs[i]);
    if (s.meshConst) SDL_ReleaseGPUBuffer(s.dev, s.meshConst);
    if (s.present) SDL_ReleaseGPUGraphicsPipeline(s.dev, s.present);
    if (s.linearClamp) SDL_ReleaseGPUSampler(s.dev, s.linearClamp);
    if (s.vbuf) SDL_ReleaseGPUBuffer(s.dev, s.vbuf);
    if (s.tbuf) SDL_ReleaseGPUTransferBuffer(s.dev, s.tbuf);
    for (i = 0; i < GXR_GRAB_SLOTS; i++) {
        if (s_grab[i].fence) SDL_ReleaseGPUFence(s.dev, s_grab[i].fence);
        if (s_grab[i].tb) SDL_ReleaseGPUTransferBuffer(s.dev, s_grab[i].tb);
    }
    SDL_zeroa(s_grab);
    if (s.efb) SDL_ReleaseGPUTexture(s.dev, s.efb);
    if (s.efbDepth) SDL_ReleaseGPUTexture(s.dev, s.efbDepth);
    if (s.image) SDL_ReleaseGPUTexture(s.dev, s.image);
    if (s.vs) SDL_ReleaseGPUShader(s.dev, s.vs);
    if (s.fs) SDL_ReleaseGPUShader(s.dev, s.fs);
    if (s.presentVS) SDL_ReleaseGPUShader(s.dev, s.presentVS);
    if (s.presentFS) SDL_ReleaseGPUShader(s.dev, s.presentFS);
#if GX_NUM_TEV_VARIANTS > 0
    for (i = 0; i < GX_NUM_TEV_VARIANTS; i++) {
        if (s.tevFS[i]) SDL_ReleaseGPUShader(s.dev, s.tevFS[i]);
        s.tevFS[i] = NULL;
    }
#endif
    SDL_ReleaseWindowFromGPUDevice(s.dev, g_port.window);
    SDL_DestroyGPUDevice(s.dev);
    s.dev = NULL;
}

/* ---- textures ------------------------------------------------------------ */

static int GPU_TexFormat(int fmt) {
    return fmt >= 0 && fmt < GXR_TEX_COUNT && s.texFormats[fmt];
}

static void *GPU_TexCreate(int fmt, int w, int h, int levels) {
    return CreateTexture(s_texFmt[fmt], SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h, levels);
}

/* Queued for the next batch's copy pass. */
static void GPU_TexUpload(void *tex, int fmt, int level, int w, int h, const u8 *data) {
    u32 size = GXR_TexLevelBytes(fmt, w, h), offset = (s.nStaging + STAGING_ALIGN - 1) & ~(u32)(STAGING_ALIGN - 1);
    Upload *u;

    if (tex == NULL) {
        return;
    }
    Grow((void **)&s.staging, &s.capStaging, offset + size, 1);
    memcpy(s.staging + offset, data, size);
    s.nStaging = offset + size;
    Grow((void **)&s.uploads, &s.capUploads, s.nUploads + 1, sizeof(Upload));
    u = &s.uploads[s.nUploads++];
    u->tex = tex;
    u->level = (u32)level;
    u->w = (u32)w;
    u->h = (u32)h;
    u->offset = offset;
}

/* Recorded draws may still use it: released after the batch. */
static void GPU_TexDestroy(void *tex) {
    Grow((void **)&s.released, &s.capReleased, s.nReleased + 1, sizeof(SDL_GPUTexture *));
    s.released[s.nReleased++] = tex;
}

static SDL_GPUSampler *GetSampler(const GXSampler *sm) {
    static const SDL_GPUSamplerAddressMode wrap[3] = {
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
    };
    u32 key = (sm->minLinear != 0) | (sm->magLinear != 0) << 1 | (sm->mip % 3) << 2 | (sm->wrapS % 3) << 4 |
              (sm->wrapT % 3) << 6;
    SDL_GPUSamplerCreateInfo ci;
    u32 i;

    for (i = 0; i < s.nSamplers; i++) {
        if (s.samplers[i].key == key) {
            return s.samplers[i].s;
        }
    }
    if (s.nSamplers == MAX_SAMPLERS) {
        return s.linearClamp;
    }
    SDL_zero(ci);
    ci.min_filter = sm->minLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    ci.mag_filter = sm->magLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    ci.mipmap_mode = sm->mip == 2 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    ci.address_mode_u = wrap[sm->wrapS % 3];
    ci.address_mode_v = wrap[sm->wrapT % 3];
    ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.min_lod = 0.0f;
    ci.max_lod = sm->mip ? 1000.0f : 0.0f;   /* no mip filter: level 0 only */
    s.samplers[s.nSamplers].key = key;
    s.samplers[s.nSamplers].s = SDL_CreateGPUSampler(s.dev, &ci);
    if (s.samplers[s.nSamplers].s == NULL) {
        Port_Fatal("SDL_CreateGPUSampler failed: %s", SDL_GetError());
    }
    return s.samplers[s.nSamplers++].s;
}

/* ---- pipelines ----------------------------------------------------------- */

static SDL_GPUGraphicsPipeline *CreatePipeline(u32 key) {
    static const SDL_GPUBlendFactor bf[] = {
        SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_SRC_COLOR,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_GPU_BLENDFACTOR_DST_COLOR,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_GPU_BLENDFACTOR_DST_ALPHA,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
    };
    static const SDL_GPUCompareOp cmp[8] = {
        SDL_GPU_COMPAREOP_NEVER, SDL_GPU_COMPAREOP_LESS, SDL_GPU_COMPAREOP_EQUAL,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_NOT_EQUAL,
        SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_ALWAYS,
    };
    static const SDL_GPUCullMode cull[3] = {
        SDL_GPU_CULLMODE_NONE, SDL_GPU_CULLMODE_FRONT, SDL_GPU_CULLMODE_BACK,
    };
    SDL_GPUVertexAttribute attr[13];
    SDL_GPUVertexBufferDescription vb[2];
    SDL_GPUColorTargetDescription ct;
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUGraphicsPipeline *p;
    u32 prim = key & 7, blend = (key >> 3) & 1, src = (key >> 4) & 15, dst = (key >> 8) & 15;
    u32 revSub = (key >> 12) & 1, depthTest = (key >> 13) & 1, depthFunc = (key >> 14) & 7;
    u32 depthWrite = (key >> 17) & 1, colorWrite = (key >> 18) & 1, alphaWrite = (key >> 19) & 1;
    u32 cullMode = (key >> 20) & 3, layout = (key >> 22) & 3, frag = key >> 24;
    int i;

    SDL_zero(attr);
    SDL_zero(vb);
    SDL_zero(ct);
    SDL_zero(ci);
    attr[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attr[0].offset = offsetof(GXVertex, pos);
    attr[1].format = SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM; attr[1].offset = offsetof(GXVertex, nrm);
    attr[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attr[2].offset = offsetof(GXVertex, clr[0]);
    attr[3].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attr[3].offset = offsetof(GXVertex, clr[1]);
    for (i = 0; i < 8; i++) {
        attr[4 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attr[4 + i].offset = (u32)(offsetof(GXVertex, tex) + i * 2 * sizeof(f32));
    }
    attr[12].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT; attr[12].offset = offsetof(GXVertex, mtxIdx);
    for (i = 0; i < 13; i++) {
        attr[i].location = (u32)i;
        attr[i].buffer_slot = 0;
    }
    vb[0].slot = 0;
    vb[0].pitch = sizeof(GXVertex);
    vb[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    if (layout != LAYOUT_GXVERTEX) {
        /* A static mesh: GXMeshVertex in slot 0, the rest from the record
         * of s.meshConst bound in slot 1 (the second colour and texture
         * coordinates 1-7 zero, and unless skinned the matrix slot). */
        attr[0].offset = offsetof(GXMeshVertex, pos);
        attr[1].offset = offsetof(GXMeshVertex, nrm);
        attr[2].offset = offsetof(GXMeshVertex, clr);
        attr[4].offset = offsetof(GXMeshVertex, tex);
        attr[3].buffer_slot = 1;
        attr[3].offset = 4;
        for (i = 5; i < 12; i++) {
            attr[i].buffer_slot = 1;
            attr[i].offset = 8;
        }
        if (layout == LAYOUT_MESH_SKINNED) {
            attr[12].offset = offsetof(GXMeshVertex, mtxIdx);
        } else {
            attr[12].buffer_slot = 1;
            attr[12].offset = 0;
        }
        vb[0].pitch = sizeof(GXMeshVertex);
        vb[1].slot = 1;
        vb[1].pitch = 16;
        vb[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    }

    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ct.blend_state.enable_blend = blend != 0;
    ct.blend_state.src_color_blendfactor = ct.blend_state.src_alpha_blendfactor = bf[src % 10];
    ct.blend_state.dst_color_blendfactor = ct.blend_state.dst_alpha_blendfactor = bf[dst % 10];
    ct.blend_state.color_blend_op = ct.blend_state.alpha_blend_op =
        revSub ? SDL_GPU_BLENDOP_REVERSE_SUBTRACT : SDL_GPU_BLENDOP_ADD;
    ct.blend_state.enable_color_write_mask = true;
    ct.blend_state.color_write_mask = (colorWrite ? SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                                    SDL_GPU_COLORCOMPONENT_B : 0) |
                                      (alphaWrite ? SDL_GPU_COLORCOMPONENT_A : 0);

    ci.vertex_shader = s.vs;
    ci.fragment_shader = s.fs;
#if GX_NUM_TEV_VARIANTS > 0
    if (frag != 0) {
        const GXTevVariant *v = &gx_tev_variants[frag - 1];

        if (s.tevFS[frag - 1] == NULL) {
            s.tevFS[frag - 1] = Shader(v->spv, v->spvSize, v->msl, v->dxil, v->dxilSize,
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 1);
        }
        ci.fragment_shader = s.tevFS[frag - 1];
    }
#else
    (void)frag;
#endif
    ci.vertex_input_state.vertex_buffer_descriptions = vb;
    ci.vertex_input_state.num_vertex_buffers = layout != LAYOUT_GXVERTEX ? 2 : 1;
    ci.vertex_input_state.vertex_attributes = attr;
    ci.vertex_input_state.num_vertex_attributes = 13;
    ci.primitive_type = (SDL_GPUPrimitiveType)prim;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = cull[cullMode % 3];
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;   /* GX front faces are clockwise */
    /* as in GL, no depth writes without the depth test */
    ci.depth_stencil_state.enable_depth_test = depthTest != 0;
    ci.depth_stencil_state.enable_depth_write = depthTest && depthWrite;
    ci.depth_stencil_state.compare_op = cmp[depthFunc];
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    ci.target_info.depth_stencil_format = s.depthFmt;
    ci.target_info.has_depth_stencil_target = true;
    p = SDL_CreateGPUGraphicsPipeline(s.dev, &ci);
    if (p == NULL) {
        Port_Fatal("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
    }
    return p;
}

/* The pixel shader for an FSBlock (see curFrag). */
static u32 FragFor(const GXFSUniforms *fs) {
#if GX_NUM_TEV_VARIANTS > 0
    GXTevKey key;
    u32 i;

    if (s.tevGen && GX_TevKey(fs, &key)) {
        for (i = 0; i < GX_NUM_TEV_VARIANTS; i++) {
            if (gx_tev_variants[i].misc == key.misc &&
                memcmp(gx_tev_variants[i].stage, key.stage, sizeof(key.stage)) == 0) {
                return i + 1;
            }
        }
    }
#else
    (void)fs;
#endif
    return 0;
}

static SDL_GPUGraphicsPipeline *GetPipeline(u32 key) {
    static u32 last;
    u32 i;

    if (last < s.nPipes && s.pipes[last].key == key) {
        return s.pipes[last].p;
    }
    for (i = 0; i < s.nPipes; i++) {
        if (s.pipes[i].key == key) {
            last = i;
            return s.pipes[i].p;
        }
    }
    if (s.nPipes == MAX_PIPELINES) {
        Port_Fatal("more than %d pipeline states", MAX_PIPELINES);
    }
    s.pipes[s.nPipes].key = key;
    s.pipes[s.nPipes].p = CreatePipeline(key);
    last = s.nPipes;
    return s.pipes[s.nPipes++].p;
}

/* ---- batches ------------------------------------------------------------- */

static void EnsureBuffers(u32 transferSize, u32 vertexSize) {
    if (transferSize > s.tbufSize) {
        SDL_GPUTransferBufferCreateInfo ci;
        if (s.tbuf) SDL_ReleaseGPUTransferBuffer(s.dev, s.tbuf);
        SDL_zero(ci);
        ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ci.size = transferSize + transferSize / 2;
        s.tbuf = SDL_CreateGPUTransferBuffer(s.dev, &ci);
        if (s.tbuf == NULL) {
            Port_Fatal("SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        }
        s.tbufSize = ci.size;
    }
    if (vertexSize > s.vbufSize) {
        SDL_GPUBufferCreateInfo ci;
        if (s.vbuf) SDL_ReleaseGPUBuffer(s.dev, s.vbuf);
        SDL_zero(ci);
        ci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        ci.size = vertexSize + vertexSize / 2;
        s.vbuf = SDL_CreateGPUBuffer(s.dev, &ci);
        if (s.vbuf == NULL) {
            Port_Fatal("SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        }
        s.vbufSize = ci.size;
    }
}

static void CopyPass(SDL_GPUCommandBuffer *cmd) {
    u32 vertBytes = s.nVerts * (u32)sizeof(GXVertex);
    u32 vertOffset = (s.nStaging + 15) & ~15u, i;
    SDL_GPUCopyPass *cp;
    u8 *map;

    if (s.nUploads == 0 && vertBytes == 0 && s.nBufUploads == 0) {
        return;
    }
    EnsureBuffers(vertOffset + vertBytes, vertBytes);
    map = SDL_MapGPUTransferBuffer(s.dev, s.tbuf, true);
    if (map == NULL) {
        Port_Fatal("SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
    }
    memcpy(map, s.staging, s.nStaging);
    memcpy(map + vertOffset, s.verts, vertBytes);
    SDL_UnmapGPUTransferBuffer(s.dev, s.tbuf);

    cp = SDL_BeginGPUCopyPass(cmd);
    for (i = 0; i < s.nUploads; i++) {
        const Upload *u = &s.uploads[i];
        SDL_GPUTextureTransferInfo src;
        SDL_GPUTextureRegion dst;
        SDL_zero(src);
        SDL_zero(dst);
        src.transfer_buffer = s.tbuf;
        src.offset = u->offset;
        src.pixels_per_row = u->w;
        src.rows_per_layer = u->h;
        dst.texture = u->tex;
        dst.mip_level = u->level;
        dst.w = u->w;
        dst.h = u->h;
        dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
    }
    if (vertBytes != 0) {
        SDL_GPUTransferBufferLocation src = { s.tbuf, vertOffset };
        SDL_GPUBufferRegion dst = { s.vbuf, 0, vertBytes };
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
        g_gxUploadBytes += vertBytes;
    }
    for (i = 0; i < s.nBufUploads; i++) {
        const BufUpload *u = &s.bufUploads[i];
        SDL_GPUTransferBufferLocation src = { s.tbuf, u->offset };
        SDL_GPUBufferRegion dst = { u->buf, 0, u->size };
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        g_gxUploadBytes += u->size;
    }
    SDL_EndGPUCopyPass(cp);
}

static void RenderPass(SDL_GPUCommandBuffer *cmd) {
    SDL_GPUColorTargetInfo ct;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_GPURenderPass *pass;
    SDL_GPUGraphicsPipeline *pipe = NULL;
    SDL_GPUBuffer *boundVtx = NULL, *boundIdx = NULL;
    u32 lastVS = ~0u, lastFS = ~0u, boundSlot = ~0u, i;
    int j;

    SDL_zero(ct);
    SDL_zero(dt);
    ct.texture = s.efb;
    ct.load_op = s.clearPending ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = s.clearColor;
    dt.texture = s.efbDepth;
    dt.load_op = ct.load_op;
    dt.store_op = SDL_GPU_STOREOP_STORE;
    dt.clear_depth = s.clearDepth;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    for (i = 0; i < s.nDraws; i++) {
        const Draw *d = &s.draws[i];
        SDL_GPUTextureSamplerBinding tb[8];
        if (d->pipeline != pipe) {
            pipe = d->pipeline;
            SDL_BindGPUGraphicsPipeline(pass, pipe);
            lastVS = lastFS = ~0u;   /* push again after a pipeline change */
        }
        SDL_SetGPUViewport(pass, &d->viewport);
        SDL_SetGPUScissor(pass, &d->scissor);
        for (j = 0; j < 8; j++) {
            tb[j].texture = d->tex[j];
            tb[j].sampler = d->samp[j];
        }
        SDL_BindGPUFragmentSamplers(pass, 0, tb, 8);
        if (d->vs != lastVS) {
            lastVS = d->vs;
            SDL_PushGPUVertexUniformData(cmd, 0, &s.vsU[d->vs], sizeof(GXVSUniforms));
        }
        if (d->fs != lastFS) {
            lastFS = d->fs;
            SDL_PushGPUFragmentUniformData(cmd, 0, &s.fsU[d->fs], sizeof(GXFSUniforms));
        }
        if (d->meshVtx == NULL) {
            if (boundVtx != s.vbuf) {
                SDL_GPUBufferBinding vb = { s.vbuf, 0 };
                SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
                boundVtx = s.vbuf;
            }
            SDL_DrawGPUPrimitives(pass, d->numVertices, 1, d->firstVertex, 0);
            continue;
        }
        if (boundVtx != d->meshVtx || boundSlot != d->meshSlot) {
            SDL_GPUBufferBinding vb[2] = { { d->meshVtx, 0 }, { s.meshConst, d->meshSlot * 16 } };
            SDL_BindGPUVertexBuffers(pass, 0, vb, 2);
            boundVtx = d->meshVtx;
            boundSlot = d->meshSlot;
        }
        if (d->meshIdx == NULL) {
            SDL_DrawGPUPrimitives(pass, d->numVertices, 1, d->firstVertex, 0);
            continue;
        }
        if (boundIdx != d->meshIdx) {
            SDL_GPUBufferBinding ib = { d->meshIdx, 0 };
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            boundIdx = d->meshIdx;
        }
        SDL_DrawGPUIndexedPrimitives(pass, d->numVertices, 1, d->firstVertex, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
    s.clearPending = 0;
}

/* Runs the recorded batch: uploads, then the draws into the EFB. */
static void Flush(void) {
    u32 i;

    if (s.nDraws == 0 && !s.clearPending && s.nUploads == 0 && s.nBufUploads == 0) {
        return;
    }
    CopyPass(Cmd());
    if (s.nDraws != 0 || s.clearPending) {
        RenderPass(Cmd());
    }
    s.nDraws = s.nVerts = s.nVS = s.nFS = s.nStaging = s.nUploads = s.nBufUploads = 0;
    for (i = 0; i < s.nReleased; i++) {
        SDL_ReleaseGPUTexture(s.dev, s.released[i]);
    }
    s.nReleased = 0;
    for (i = 0; i < s.nReleasedBufs; i++) {
        SDL_ReleaseGPUBuffer(s.dev, s.releasedBufs[i]);
    }
    s.nReleasedBufs = 0;
}

/* ---- drawing ------------------------------------------------------------- */

static GXVertex *AddVertices(u32 n) {
    Grow((void **)&s.verts, &s.capVerts, s.nVerts + n, sizeof(GXVertex));
    s.nVerts += n;
    return s.verts + s.nVerts - n;
}

static void GPU_Draw(const GXDrawCall *dc) {
    static const u8 prims[] = {
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_PRIMITIVETYPE_LINELIST, SDL_GPU_PRIMITIVETYPE_LINESTRIP, SDL_GPU_PRIMITIVETYPE_POINTLIST,
    };
    const f32 *vp = dc->viewport;
    int i;
    u32 first = s.nVerts, n, key, layout = LAYOUT_GXVERTEX;
    Draw *d;

    /* GL culls every polygon with GL_FRONT_AND_BACK (lines and points stay) */
    if (dc->cull == 3 && dc->prim <= GXR_TRIANGLEFAN) {
        return;
    }

    if (dc->mesh != NULL) {
        const GPUMesh *mv = dc->mesh->vtx;

        first = dc->mesh->first;
        n = dc->mesh->count;
        layout = mv->skinned ? LAYOUT_MESH_SKINNED : LAYOUT_MESH;
    } else if (dc->prim == GXR_TRIANGLEFAN) {   /* no fans in SDL_GPU */
        GXVertex *t;
        u32 k;
        if (dc->n < 3) {
            return;
        }
        n = (dc->n - 2) * 3;
        t = AddVertices(n);
        for (k = 1; k + 1 < dc->n; k++) {
            *t++ = dc->v[0];
            *t++ = dc->v[k];
            *t++ = dc->v[k + 1];
        }
    } else {
        n = dc->n;
        memcpy(AddVertices(n), dc->v, n * sizeof(GXVertex));
    }

    Grow((void **)&s.draws, &s.capDraws, s.nDraws + 1, sizeof(Draw));
    d = &s.draws[s.nDraws++];
    d->firstVertex = first;
    d->numVertices = n;
    d->meshVtx = dc->mesh != NULL ? ((const GPUMesh *)dc->mesh->vtx)->buf : NULL;
    d->meshIdx = dc->mesh != NULL && dc->mesh->idx != NULL ? ((const GPUMesh *)dc->mesh->idx)->buf : NULL;
    d->meshSlot = layout == LAYOUT_MESH ? (u32)dc->mesh->slot : 0;
    if (s.nVS == 0 || memcmp(&s.vsU[s.nVS - 1], dc->vs, sizeof(GXVSUniforms)) != 0) {
        Grow((void **)&s.vsU, &s.capVS, s.nVS + 1, sizeof(GXVSUniforms));
        s.vsU[s.nVS++] = *dc->vs;
    }
    if (s.nFS == 0 || memcmp(&s.fsU[s.nFS - 1], dc->fs, sizeof(GXFSUniforms)) != 0) {
        Grow((void **)&s.fsU, &s.capFS, s.nFS + 1, sizeof(GXFSUniforms));
        s.fsU[s.nFS++] = *dc->fs;
        s.curFrag = FragFor(dc->fs);
    }
    if (s.curFrag != 0) {
        g_gxShaderStats.special++;
    } else {
        g_gxShaderStats.general++;
    }
    d->vs = s.nVS - 1;
    d->fs = s.nFS - 1;
    for (i = 0; i < 8; i++) {
        d->tex[i] = dc->tex[i] != NULL ? dc->tex[i] : s.black;
        d->samp[i] = dc->tex[i] != NULL ? GetSampler(&dc->samp[i]) : s.linearClamp;
    }
    d->viewport.x = vp[0];
    d->viewport.y = vp[1];
    d->viewport.w = vp[2];
    d->viewport.h = vp[3];
    d->viewport.min_depth = SDL_clamp(vp[4], 0.0f, 1.0f);
    d->viewport.max_depth = SDL_clamp(vp[5], 0.0f, 1.0f);
    d->scissor.x = dc->scissor[0];
    d->scissor.y = dc->scissor[1];
    d->scissor.w = dc->scissor[2];
    d->scissor.h = dc->scissor[3];
    key = prims[dc->prim] | (u32)(dc->blend != 0) << 3 | (u32)(dc->blendSrc & 15) << 4 |
          (u32)(dc->blendDst & 15) << 8 | (u32)(dc->blendRevSub != 0) << 12 | (u32)(dc->depthTest != 0) << 13 |
          (u32)(dc->depthFunc & 7) << 14 | (u32)(dc->depthWrite != 0) << 17 | (u32)(dc->colorWrite != 0) << 18 |
          (u32)(dc->alphaWrite != 0) << 19 | (u32)(dc->cull % 3) << 20 | layout << 22 | s.curFrag << 24;
    d->pipeline = GetPipeline(key);
}

/* ---- static meshes (gx_mesh.c) ------------------------------------------ */

/* A GPU buffer holding data (size rounded up to 4 bytes, zero-filled),
 * uploaded in the next batch's copy pass, before any draw recorded from now
 * on. */
static SDL_GPUBuffer *StaticBuffer(SDL_GPUBufferUsageFlags usage, const void *data, u32 dataSize) {
    u32 offset = (s.nStaging + 15) & ~15u, size = (dataSize + 3) & ~3u;
    SDL_GPUBufferCreateInfo ci;
    SDL_GPUBuffer *buf;
    BufUpload *u;

    SDL_zero(ci);
    ci.usage = usage;
    ci.size = size;
    buf = SDL_CreateGPUBuffer(s.dev, &ci);
    if (buf == NULL) {
        Port_Log("SDL_CreateGPUBuffer (%u bytes) failed: %s", size, SDL_GetError());
        return NULL;
    }
    Grow((void **)&s.staging, &s.capStaging, offset + size, 1);
    memcpy(s.staging + offset, data, dataSize);
    memset(s.staging + offset + dataSize, 0, size - dataSize);
    s.nStaging = offset + size;
    Grow((void **)&s.bufUploads, &s.capBufUploads, s.nBufUploads + 1, sizeof(BufUpload));
    u = &s.bufUploads[s.nBufUploads++];
    u->buf = buf;
    u->offset = offset;
    u->size = size;
    return buf;
}

static void *GPU_MeshIndices(const u16 *idx, u32 n) {
    GPUMesh *m = SDL_calloc(1, sizeof(GPUMesh));

    if (m == NULL) {
        return NULL;
    }
    m->bytes = n * (u32)sizeof(u16);
    m->buf = StaticBuffer(SDL_GPU_BUFFERUSAGE_INDEX, idx, m->bytes);
    if (m->buf == NULL) {
        SDL_free(m);
        return NULL;
    }
    return m;
}

static void *GPU_MeshVertices(const GXMeshVertex *v, u32 n, int skinned, void *indices) {
    GPUMesh *m = SDL_calloc(1, sizeof(GPUMesh));

    (void)indices;
    if (m == NULL) {
        return NULL;
    }
    m->bytes = n * (u32)sizeof(GXMeshVertex);
    m->skinned = skinned;
    m->buf = StaticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, v, m->bytes);
    if (m->buf == NULL) {
        SDL_free(m);
        return NULL;
    }
    return m;
}

/* Recorded draws may still use it: released after the batch. */
static void GPU_MeshDestroy(void *buf) {
    GPUMesh *m = buf;

    Grow((void **)&s.releasedBufs, &s.capReleasedBufs, s.nReleasedBufs + 1, sizeof(SDL_GPUBuffer *));
    s.releasedBufs[s.nReleasedBufs++] = m->buf;
    SDL_free(m);
}

static void GPU_Clear(const f32 c[4], f32 depth) {
    Flush();
    s.clearPending = 1;
    s.clearColor.r = c[0];
    s.clearColor.g = c[1];
    s.clearColor.b = c[2];
    s.clearColor.a = c[3];
    s.clearDepth = depth;
}

static void GPU_DrawImage(const u8 *rgba, int width, int height, const int dst[4]) {
    SDL_GPUBlitInfo bi;

    if (s.image == NULL || width != s.imageW || height != s.imageH) {
        if (s.image != NULL) {
            GPU_TexDestroy(s.image);
        }
        s.image = GPU_TexCreate(GXR_TEX_RGBA8, width, height, 1);
        s.imageW = width;
        s.imageH = height;
        if (s.image == NULL) {
            return;
        }
    }
    GPU_TexUpload(s.image, GXR_TEX_RGBA8, 0, width, height, rgba);
    Flush();
    SDL_zero(bi);
    bi.source.texture = s.image;
    bi.source.w = (u32)width;
    bi.source.h = (u32)height;
    bi.destination.texture = s.efb;
    bi.destination.x = (u32)dst[0];
    bi.destination.y = (u32)dst[1];
    bi.destination.w = (u32)(dst[2] - dst[0]);
    bi.destination.h = (u32)(dst[3] - dst[1]);
    bi.load_op = SDL_GPU_LOADOP_LOAD;
    bi.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(Cmd(), &bi);
}

static void GPU_CopyDisp(const int src[4]) {
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUColorTargetInfo ct;
    SDL_GPURenderPass *pass;
    SDL_GPUTextureSamplerBinding tb;
    SDL_GPUViewport vp;
    f32 rect[4], gamma[4] = { Port_GfxGammaPower(), 0.0f, 0.0f, 0.0f };
    int lb[4];

    Flush();
    cmd = Cmd();
    if (s.swapTex == NULL) {
        bool ok;

        GPU_ApplyVSync();
        if (Port_VSyncWanted()) {
            /* blocks for vsync: let the audio thread run meanwhile */
            Port_InterruptWindowBegin();
            ok = SDL_WaitAndAcquireGPUSwapchainTexture(cmd, g_port.window, &s.swapTex, &s.swapW, &s.swapH);
            Port_InterruptWindowEnd();
            if (!ok) {
                Port_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            }
        } else {
            /* VSync off: no wait for the display, so no window for the audio
             * thread either. */
            ok = SDL_AcquireGPUSwapchainTexture(cmd, g_port.window, &s.swapTex, &s.swapW, &s.swapH);
            if (!ok) {
                Port_Log("SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            }
        }
        if (s.swapTex == NULL) {   /* minimized */
            return;
        }
    }
    GX_Letterbox((int)s.swapW, (int)s.swapH, src[2] - src[0], src[3] - src[1], lb);
    SDL_zero(ct);
    ct.texture = s.swapTex;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color.a = 1.0f;
    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, s.present);
    vp.x = (f32)lb[0];
    vp.y = (f32)lb[1];
    vp.w = (f32)lb[2];
    vp.h = (f32)lb[3];
    vp.min_depth = 0.0f;
    vp.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &vp);
    rect[0] = (f32)src[0] / s.efbW;
    rect[1] = (f32)src[1] / s.efbH;
    rect[2] = (f32)src[2] / s.efbW;
    rect[3] = (f32)src[3] / s.efbH;
    SDL_PushGPUVertexUniformData(cmd, 0, rect, sizeof(rect));
    SDL_PushGPUFragmentUniformData(cmd, 0, gamma, sizeof(gamma));
    tb.texture = s.efb;
    tb.sampler = s.linearClamp;
    SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);
    SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

static void GPU_CopyEFB(void *tex, const int src[4]) {
    SDL_GPUTextureLocation from, to;
    SDL_GPUCopyPass *cp;

    Flush();
    SDL_zero(from);
    SDL_zero(to);
    from.texture = s.efb;
    from.x = (u32)src[0];
    from.y = (u32)src[1];
    to.texture = tex;
    cp = SDL_BeginGPUCopyPass(Cmd());
    SDL_CopyGPUTextureToTexture(cp, &from, &to, (u32)(src[2] - src[0]), (u32)(src[3] - src[1]), 1, false);
    SDL_EndGPUCopyPass(cp);
}

static int GPU_ReadEFB(u8 *rgba) {
    SDL_GPUTransferBufferCreateInfo ci;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUCopyPass *cp;
    SDL_GPUFence *fence;
    u32 size = (u32)s.efbW * (u32)s.efbH * 4;
    const u8 *map;
    int ok = 0;

    Flush();
    SDL_zero(ci);
    ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ci.size = size;
    tb = SDL_CreateGPUTransferBuffer(s.dev, &ci);
    if (tb == NULL) {
        return 0;
    }
    SDL_zero(src);
    SDL_zero(dst);
    src.texture = s.efb;
    src.w = (u32)s.efbW;
    src.h = (u32)s.efbH;
    src.d = 1;
    dst.transfer_buffer = tb;
    dst.pixels_per_row = (u32)s.efbW;
    dst.rows_per_layer = (u32)s.efbH;
    cp = SDL_BeginGPUCopyPass(Cmd());
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    /* submits the frame so far (and presents it, if a display copy already
     * ran this frame) */
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(s.cmd);
    s.cmd = NULL;
    s.swapTex = NULL;
    if (fence != NULL) {
        SDL_WaitForGPUFences(s.dev, true, &fence, 1);
        SDL_ReleaseGPUFence(s.dev, fence);
        map = SDL_MapGPUTransferBuffer(s.dev, tb, false);
        if (map != NULL) {
            memcpy(rgba, map, size);
            SDL_UnmapGPUTransferBuffer(s.dev, tb);
            ok = 1;
        }
    }
    SDL_ReleaseGPUTransferBuffer(s.dev, tb);
    return ok;
}

static int GPU_GrabStart(int slot) {
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUCopyPass *cp;
    u32 size = (u32)s.efbW * (u32)s.efbH * 4;

    if (slot < 0 || slot >= GXR_GRAB_SLOTS || s_grab[slot].fence != NULL) {
        return 0;
    }
    if (s_grab[slot].tb == NULL || s_grab[slot].size < size) {
        SDL_GPUTransferBufferCreateInfo ci;

        if (s_grab[slot].tb != NULL) {
            SDL_ReleaseGPUTransferBuffer(s.dev, s_grab[slot].tb);
        }
        SDL_zero(ci);
        ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ci.size = size;
        s_grab[slot].tb = SDL_CreateGPUTransferBuffer(s.dev, &ci);
        s_grab[slot].size = s_grab[slot].tb != NULL ? size : 0;
        if (s_grab[slot].tb == NULL) {
            return 0;
        }
    }
    Flush();
    SDL_zero(src);
    SDL_zero(dst);
    src.texture = s.efb;
    src.w = (u32)s.efbW;
    src.h = (u32)s.efbH;
    src.d = 1;
    dst.transfer_buffer = s_grab[slot].tb;
    dst.pixels_per_row = (u32)s.efbW;
    dst.rows_per_layer = (u32)s.efbH;
    cp = SDL_BeginGPUCopyPass(Cmd());
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    /* submitted now, as GPU_ReadEFB does, but not waited for */
    s_grab[slot].fence = SDL_SubmitGPUCommandBufferAndAcquireFence(s.cmd);
    s.cmd = NULL;
    s.swapTex = NULL;
    s_grab[slot].w = s.efbW;
    s_grab[slot].h = s.efbH;
    return s_grab[slot].fence != NULL;
}

static int GPU_GrabFetch(int slot, u8 *dst, int stride, int maxW, int maxH, int *w, int *h, int wait) {
    const u8 *map;
    int ok = -1, y;

    if (slot < 0 || slot >= GXR_GRAB_SLOTS || s_grab[slot].fence == NULL) {
        return -1;
    }
    if (wait) {
        SDL_WaitForGPUFences(s.dev, true, &s_grab[slot].fence, 1);
    } else if (!SDL_QueryGPUFence(s.dev, s_grab[slot].fence)) {
        return 0;
    }
    SDL_ReleaseGPUFence(s.dev, s_grab[slot].fence);
    s_grab[slot].fence = NULL;
    if (s_grab[slot].w <= maxW && s_grab[slot].h <= maxH &&
        (map = SDL_MapGPUTransferBuffer(s.dev, s_grab[slot].tb, false)) != NULL) {
        size_t row = (size_t)s_grab[slot].w * 4;

        if ((size_t)stride == row) {
            memcpy(dst, map, row * (size_t)s_grab[slot].h);
        } else {
            for (y = 0; y < s_grab[slot].h; y++) {
                memcpy(dst + (size_t)y * (size_t)stride, map + (size_t)y * row, row);
            }
        }
        SDL_UnmapGPUTransferBuffer(s.dev, s_grab[slot].tb);
        *w = s_grab[slot].w;
        *h = s_grab[slot].h;
        ok = 1;
    }
    return ok;
}

static void GPU_Present(void) {
    Flush();
    if (s.cmd != NULL) {
        if (!SDL_SubmitGPUCommandBuffer(s.cmd)) {
            Port_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        }
        s.cmd = NULL;
    }
    s.swapTex = NULL;
}

const GXRenderer g_gxSDLGPU = {
    "sdl_gpu", GPU_Open, GPU_Close, GPU_Init, GPU_Resize, GPU_TexFormat, GPU_TexCreate, GPU_TexUpload, GPU_TexDestroy,
    GPU_Draw, GPU_Clear, GPU_DrawImage, GPU_CopyDisp, GPU_CopyEFB, GPU_ReadEFB, GPU_Present,
    GPU_MeshIndices, GPU_MeshVertices, GPU_MeshDestroy, GPU_GrabStart, GPU_GrabFetch,
};
