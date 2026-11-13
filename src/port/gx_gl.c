/* OpenGL 3.3 core renderer (--renderer opengl, the default). The EFB is a
 * framebuffer object; GL rows are bottom-up, so rectangles from gx_draw.c
 * (top-left origin) are flipped here. */
#include "gx_render.h"
#include "viewer.h"
#include "gx_shaders.h"
#include "gx_tevgen.h"

/* GL_KHR_parallel_shader_compile (and the ARB one, same values) */
#define GL_COMPLETION_STATUS 0x91B1
typedef void (APIENTRYP PFNMAXSHADERCOMPILERTHREADSPROC)(GLuint count);

/* Specialised pixel shaders (gx_tevgen.h), one program each, in an
 * open-addressed table keyed by GXTevKey. Until a draw's program has linked
 * it uses the general one (s.prog): with parallel shader compilation the
 * driver builds them on its own threads, so a new setup costs no stall. */
#define PROG_SLOTS 1024             /* power of two */
#define PROG_MAX (PROG_SLOTS * 3 / 4)

enum { PROG_EMPTY, PROG_PENDING, PROG_READY, PROG_FAILED };

typedef struct Prog {
    GXTevKey key;
    GLuint prog, fs;
    u8 state;
} Prog;

static struct {
    SDL_GLContext ctx;
    GLuint prog, vs, vao, vbo, ubo[2];
    GLuint gammaProg, gammaVao;     /* GL_DrawGamma: made when first needed */
    GLint gammaRect, gammaPower;
    GLuint poolVao, poolVbo;    /* the display list cache's vertex pool */
    GLuint fbo, fboColor, fboDepth;
    int efbW, efbH;
    GXVSUniforms lastVS;
    GXFSUniforms lastFS;
    int uboValid;
    /* specialised programs; on: 0 = HWVX_NO_TEVGEN, only the general one */
    int tevGen, parallel;
    char *fsDecl;                   /* gx.frag up to its functions */
    Prog *progs;
    u32 nProgs;
    GLuint curProg;                 /* for the FSBlock in lastFS */
    int curPending;
    /* What the pool buffer holds: only vertices [0, poolClean) still match
     * memory, because an aborted capture or a cache reset recycles the tail.
     * poolGen changes when the pool starts over. */
    u32 poolGen, poolCap, poolClean;
    /* Static meshes read the attributes they don't have from the current
     * vertex attribute values, which a draw from enabled arrays leaves
     * undefined: meshConst says they are set, meshSlot is location 12's. */
    int meshConst;
    f32 meshSlot;
    int s3tc;                       /* EXT_texture_compression_s3tc: the BC formats */
} s;

/* GL_Draw runs thousands of times a frame, mostly with the same state. What it
 * applies is remembered here and only re-applied when it differs. Anything that
 * touches GL state outside GL_Draw (clear, copy display, textures, present)
 * calls StateReset(), which makes the next draw apply everything. */
static struct {
    int full;                     /* 0: nothing here is known to be applied */
    GLuint fbo, prog, vao;
    f32 vp[6];                    /* viewport, near, far as GX set them */
    int scissorOn, sc[4];
    int depthTest, depthWrite, cull, blend, blendRevSub;
    GLenum zfunc, bsrc, bdst;
    GLboolean colorWrite, alphaWrite;
    struct { const void *tex; GXSampler samp; } unit[8];
} a;

static void StateReset(void) {
    a.full = 0;
    a.fbo = 0;
    a.prog = 0;
    a.vao = 0;
    a.scissorOn = 0;
    memset(a.unit, 0, sizeof(a.unit));
}

/* The Graphics menu's VSync row, and HWVX_NO_VSYNC=1 for measuring: the swap
 * interval says whether showing a frame waits for the display. It only hides
 * what the GPU costs when it is on, so the knob wins over the setting.
 * Set when it changes: the context is current in the opens and the presents. */
static void GL_ApplyVSync(void) {
    static int applied = -1;
    int want = Port_VSyncWanted();

    if (want != applied) {
        SDL_GL_SetSwapInterval(want);
        applied = want;
    }
}

static void TevGenFree(void);

static int GL_Open(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    /* no OpenGL 3.3 core (an old driver, some Wine setups): report 0 for
     * main_sdl.c to try another renderer */
    if (!Port_TryCreateWindow(SDL_WINDOW_OPENGL)) {
        Port_Log("The OpenGL renderer is unavailable: %s", SDL_GetError());
        return 0;
    }
    s.ctx = SDL_GL_CreateContext(g_port.window);
    if (s.ctx == NULL) {
        Port_Log("OpenGL 3.3 core context unavailable: %s", SDL_GetError());
        SDL_DestroyWindow(g_port.window);
        g_port.window = NULL;
        return 0;
    }
    SDL_GL_MakeCurrent(g_port.window, s.ctx);
    GL_ApplyVSync();
    GL_Load();
    return 1;
}

static void GL_Close(void) {
    TevGenFree();
    SDL_GL_DestroyContext(s.ctx);
    s.ctx = NULL;
    s.gammaProg = 0;
}

static GLuint Compile(GLenum type, const char *src) {
    const char *parts[2] = { "#version 330 core\n", src };
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        Port_Fatal("GX shader compile failed:\n%s", log);
    }
    return sh;
}

/* Uniform block and sampler bindings: fixed in the shaders for SDL_GPU, set
 * per program here. Leaves prog bound. */
static void BindProgramSlots(GLuint prog) {
    int i;

    glUseProgram(prog);
    glUniformBlockBinding(prog, glGetUniformBlockIndex(prog, "VSBlock"), 0);
    glUniformBlockBinding(prog, glGetUniformBlockIndex(prog, "FSBlock"), 1);
    for (i = 0; i < 8; i++) {
        char name[8] = "uTex0";
        name[4] = (char)('0' + i);
        glUniform1i(glGetUniformLocation(prog, name), i);   /* -1 (unused): ignored */
    }
}

/* ---- specialised pixel shaders ------------------------------------------- */

/* gx.frag's declarations: everything before its first function. Generated
 * shaders reuse them, so the two can't drift apart. */
static char *FragDecl(void) {
    const char *end = SDL_strstr(gx_frag_glsl, "\nvec4 tex(int map");
    size_t n;
    char *d;

    if (end == NULL) {
        Port_Log("tevgen: gx.frag changed shape, using the general shader only");
        return NULL;
    }
    n = (size_t)(end - gx_frag_glsl) + 1;
    d = SDL_malloc(n + 1);
    if (d != NULL) {
        memcpy(d, gx_frag_glsl, n);
        d[n] = 0;
    }
    return d;
}

static void TevGenInit(void) {
    const char *e = SDL_getenv("HWVX_NO_TEVGEN");
    PFNMAXSHADERCOMPILERTHREADSPROC maxThreads = NULL;

    s.tevGen = !(e != NULL && *e == '1');
    if (!s.tevGen) {
        Port_Log("tevgen: off (HWVX_NO_TEVGEN), general pixel shader only");
        return;
    }
    s.fsDecl = FragDecl();
    s.progs = SDL_calloc(PROG_SLOTS, sizeof(Prog));
    if (s.fsDecl == NULL || s.progs == NULL) {
        s.tevGen = 0;
        return;
    }
    if (SDL_GL_ExtensionSupported("GL_KHR_parallel_shader_compile")) {
        maxThreads = (PFNMAXSHADERCOMPILERTHREADSPROC)SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsKHR");
    } else if (SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile")) {
        maxThreads = (PFNMAXSHADERCOMPILERTHREADSPROC)SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsARB");
    }
    if (maxThreads != NULL) {
        maxThreads(0xFFFFFFFFu);   /* as many as the driver likes */
        s.parallel = 1;
    }
    Port_Log("tevgen: specialised pixel shaders on, %s compilation",
             s.parallel ? "parallel" : "blocking");
}

/* Start building the program for key: compile and link are queued, and with
 * parallel compilation return at once. */
static void ProgStart(Prog *p) {
    char *src = GX_TevGenGLSL(&p->key, s.fsDecl);
    const char *parts[2] = { "#version 330 core\n", src };

    p->fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(p->fs, 2, parts, NULL);
    glCompileShader(p->fs);
    SDL_free(src);
    p->prog = glCreateProgram();
    glAttachShader(p->prog, s.vs);
    glAttachShader(p->prog, p->fs);
    glLinkProgram(p->prog);
    p->state = PROG_PENDING;
}

/* A pending program: done yet? Never waits with parallel compilation. */
static void ProgPoll(Prog *p) {
    GLint ok = 0;

    if (s.parallel) {
        glGetProgramiv(p->prog, GL_COMPLETION_STATUS, &ok);
        if (!ok) {
            return;
        }
    }
    glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        /* a generator bug: keep the general shader for this setup */
        char log[2048];
        GLint cok = 0;

        glGetShaderiv(p->fs, GL_COMPILE_STATUS, &cok);
        if (!cok) {
            glGetShaderInfoLog(p->fs, sizeof(log), NULL, log);
        } else {
            glGetProgramInfoLog(p->prog, sizeof(log), NULL, log);
        }
        Port_Log("tevgen: specialised shader failed, using the general one:\n%s", log);
        glDeleteProgram(p->prog);
        p->prog = 0;
        p->state = PROG_FAILED;
    } else {
        BindProgramSlots(p->prog);
        a.prog = p->prog;           /* BindProgramSlots bound it */
        p->state = PROG_READY;
    }
    glDeleteShader(p->fs);          /* stays alive while attached */
    p->fs = 0;
}

/* The program for a draw's FSBlock: its specialised one once linked, until
 * then (or without one) the general one. Sets s.curPending while waiting. */
static GLuint ProgramFor(const GXFSUniforms *fs) {
    GXTevKey key;
    u32 i;
    Prog *p;

    s.curPending = 0;
    if (!s.tevGen || !GX_TevKey(fs, &key)) {
        return s.prog;
    }
    for (i = GX_TevKeyHash(&key) & (PROG_SLOTS - 1);; i = (i + 1) & (PROG_SLOTS - 1)) {
        p = &s.progs[i];
        if (p->state == PROG_EMPTY) {
            if (s.nProgs >= PROG_MAX) {
                return s.prog;      /* table full: general shader from here on */
            }
            p->key = key;
            s.nProgs++;
            ProgStart(p);
            break;
        }
        if (memcmp(&p->key, &key, sizeof(key)) == 0) {
            break;
        }
    }
    if (p->state == PROG_PENDING) {
        ProgPoll(p);
    }
    if (p->state == PROG_READY) {
        return p->prog;
    }
    s.curPending = p->state == PROG_PENDING;
    return s.prog;
}

static void TevGenStats(void) {
    u32 i, pending = 0;

    if (s.progs == NULL) {
        return;
    }
    for (i = 0; i < PROG_SLOTS; i++) {
        pending += s.progs[i].state == PROG_PENDING;
    }
    g_gxShaderStats.programs = s.nProgs;
    g_gxShaderStats.pending = pending;
}

static void TevGenFree(void) {
    u32 i;

    if (s.progs != NULL) {
        for (i = 0; i < PROG_SLOTS; i++) {
            if (s.progs[i].prog != 0) glDeleteProgram(s.progs[i].prog);
            if (s.progs[i].fs != 0) glDeleteShader(s.progs[i].fs);
        }
    }
    SDL_free(s.progs);
    SDL_free(s.fsDecl);
    s.progs = NULL;
    s.fsDecl = NULL;
    s.nProgs = 0;
    s.curProg = 0;
}

static void CreateEFB(void) {
    if (s.fbo != 0) {
        glDeleteFramebuffers(1, &s.fbo);
        glDeleteTextures(1, &s.fboColor);
        glDeleteRenderbuffers(1, &s.fboDepth);
    }
    glGenFramebuffers(1, &s.fbo);
    glGenTextures(1, &s.fboColor);
    glBindTexture(GL_TEXTURE_2D, s.fboColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.efbW, s.efbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &s.fboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, s.fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, s.efbW, s.efbH);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.fboColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s.fboDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Port_Fatal("EFB framebuffer incomplete");
    }
    StateReset();
}

/* The vertex format, read from whichever array buffer is bound. Both the
 * scratch buffer and the pool use it, so a draw never re-specifies it. */
static void SetAttribs(void) {
#define ATTR(loc, n, field) \
    glEnableVertexAttribArray(loc); \
    glVertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void *)offsetof(GXVertex, field))
#define ATTRN(loc, n, type, field) \
    glEnableVertexAttribArray(loc); \
    glVertexAttribPointer(loc, n, type, GL_TRUE, sizeof(GXVertex), (void *)offsetof(GXVertex, field))
    int i;

    ATTR(0, 3, pos);
    ATTRN(1, 4, GL_BYTE, nrm);
    ATTRN(2, 4, GL_UNSIGNED_BYTE, clr[0]);
    ATTRN(3, 4, GL_UNSIGNED_BYTE, clr[1]);
    for (i = 0; i < 8; i++) {
        glEnableVertexAttribArray(4 + i);
        glVertexAttribPointer(4 + i, 2, GL_FLOAT, GL_FALSE, sizeof(GXVertex),
                              (void *)(offsetof(GXVertex, tex) + i * 2 * sizeof(f32)));
    }
    ATTR(12, 1, mtxIdx);
#undef ATTR
#undef ATTRN
}

static void GL_Init(int efbW, int efbH) {
    GLuint fs;
    GLint ok = 0;

    s.efbW = efbW;
    s.efbH = efbH;
    /* the vertex shader stays: every specialised program links it too */
    s.vs = Compile(GL_VERTEX_SHADER, gx_vert_glsl);
    fs = Compile(GL_FRAGMENT_SHADER, gx_frag_glsl);
    s.prog = glCreateProgram();
    glAttachShader(s.prog, s.vs);
    glAttachShader(s.prog, fs);
    glLinkProgram(s.prog);
    glGetProgramiv(s.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(s.prog, sizeof(log), NULL, log);
        Port_Fatal("GX shader link failed:\n%s", log);
    }
    glDeleteShader(fs);
    BindProgramSlots(s.prog);
    TevGenInit();
    s.s3tc = SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc");
    glGenBuffers(2, s.ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, s.ubo[0]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GXVSUniforms), NULL, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, s.ubo[1]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GXFSUniforms), NULL, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s.ubo[0]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, s.ubo[1]);

    /* Two vertex array objects share the same attribute layout and differ only
     * in which vertex buffer they read: the scratch buffer that uncached draws
     * and the immediate FIFO fill, and the cache's vertex pool. */
    glGenVertexArrays(1, &s.vao);
    glBindVertexArray(s.vao);
    glGenBuffers(1, &s.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    SetAttribs();
    glGenVertexArrays(1, &s.poolVao);
    glBindVertexArray(s.poolVao);
    glGenBuffers(1, &s.poolVbo);
    glBindBuffer(GL_ARRAY_BUFFER, s.poolVbo);
    SetAttribs();
    glFrontFace(GL_CW);           /* GX front faces are clockwise */
    CreateEFB();
}

static void GL_Resize(int efbW, int efbH) {
    s.efbW = efbW;
    s.efbH = efbH;
    CreateEFB();
}

/* ---- textures ------------------------------------------------------------ */

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

static int GL_TexFormat(int fmt) {
    return fmt == GXR_TEX_RGBA8 || (s.s3tc && fmt < GXR_TEX_COUNT);
}

static void *GL_TexCreate(int fmt, int w, int h, int levels) {
    GLuint name = 0;

    (void)fmt;
    (void)w;
    (void)h;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    StateReset();
    return (void *)(uintptr_t)name;
}

static void GL_TexUpload(void *tex, int fmt, int level, int w, int h, const u8 *data) {
    static const GLenum bc[GXR_TEX_COUNT] = {
        0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
    };

    glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)tex);
    if (fmt == GXR_TEX_RGBA8) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    } else {
        glCompressedTexImage2D(GL_TEXTURE_2D, level, bc[fmt], w, h, 0, (GLsizei)GXR_TexLevelBytes(fmt, w, h), data);
    }
    StateReset();
}

static void GL_TexDestroy(void *tex) {
    GLuint name = (GLuint)(uintptr_t)tex;
    glDeleteTextures(1, &name);
    StateReset();
}

static GLint WrapMode(u8 w) {
    return w == 1 ? GL_REPEAT : w == 2 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
}

static void SetSampler(const GXSampler *sm) {
    static const GLint minf[2][3] = {
        { GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR },
        { GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR },
    };
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf[sm->minLinear != 0][sm->mip % 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sm->magLinear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, WrapMode(sm->wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, WrapMode(sm->wrapT));
}

/* ---- drawing ------------------------------------------------------------- */

static GLenum BlendFactor(u8 f) {
    static const GLenum t[] = {
        GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,
        GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
    };
    return f < sizeof(t) / sizeof(t[0]) ? t[f] : GL_ONE;
}

/* Returns whether the FSBlock changed. */
static int UploadUniforms(const GXDrawCall *dc) {
    int fsChanged = 0;

    if (!s.uboValid || memcmp(&s.lastVS, dc->vs, sizeof(s.lastVS)) != 0) {
        s.lastVS = *dc->vs;
        glBindBuffer(GL_UNIFORM_BUFFER, s.ubo[0]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(GXVSUniforms), dc->vs, GL_STREAM_DRAW);
    }
    if (!s.uboValid || memcmp(&s.lastFS, dc->fs, sizeof(s.lastFS)) != 0) {
        s.lastFS = *dc->fs;
        glBindBuffer(GL_UNIFORM_BUFFER, s.ubo[1]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(GXFSUniforms), dc->fs, GL_STREAM_DRAW);
        fsChanged = 1;
    }
    s.uboValid = 1;
    return fsChanged;
}

/* The draw's vertices are part of the cache's vertex pool, which stays on the
 * GPU while the cache holds them: append what has not been sent yet and draw
 * from an offset, with no buffer copy and no attribute changes. */
static void DrawBatched(const GXDrawCall *dc, GLenum mode) {
    const GXVtxBatch *b = dc->batch;

    if (s.poolGen != b->gen || s.poolCap < b->cap) {
        glBindBuffer(GL_ARRAY_BUFFER, s.poolVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(b->cap * sizeof(GXVertex)), NULL, GL_STREAM_DRAW);
        s.poolGen = b->gen;
        s.poolCap = b->cap;
        s.poolClean = 0;
    }
    if (b->used < s.poolClean) {
        s.poolClean = b->used;      /* an aborted capture recycled the tail */
    }
    if (b->used > s.poolClean) {
        glBindBuffer(GL_ARRAY_BUFFER, s.poolVbo);
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)(s.poolClean * sizeof(GXVertex)),
                        (GLsizeiptr)((b->used - s.poolClean) * sizeof(GXVertex)), b->base + s.poolClean);
        g_gxUploadBytes += (b->used - s.poolClean) * sizeof(GXVertex);
        s.poolClean = b->used;
    }
    if (a.vao != s.poolVao) {
        glBindVertexArray(s.poolVao);
        a.vao = s.poolVao;
    }
    glDrawArrays(mode, (GLint)(dc->v - b->base), (GLsizei)dc->n);
}

/* ---- static meshes (gx_mesh.c) ------------------------------------------ */

/* An index buffer, or a vertex buffer with the vertex array object that
 * reads it (GXMeshVertex) and its mesh's index buffer. */
typedef struct GLMesh {
    GLuint buf, vao;            /* vao 0: an index buffer */
    int skinned;
} GLMesh;

static void *GL_MeshIndices(const u16 *idx, u32 n) {
    GLMesh *m = SDL_calloc(1, sizeof(GLMesh));

    if (m == NULL) {
        return NULL;
    }
    /* through the array binding: the element binding is the bound VAO's */
    glGenBuffers(1, &m->buf);
    glBindBuffer(GL_ARRAY_BUFFER, m->buf);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(u16)), idx, GL_STATIC_DRAW);
    g_gxUploadBytes += n * sizeof(u16);
    return m;
}

/* GXMeshVertex's attributes; the shader's others come from the current
 * values (MeshConstants), and so does the matrix slot unless skinned. */
static void *GL_MeshVertices(const GXMeshVertex *v, u32 n, int skinned, void *indices) {
    GLMesh *m = SDL_calloc(1, sizeof(GLMesh));

    if (m == NULL) {
        return NULL;
    }
    m->skinned = skinned;
    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);
    a.vao = m->vao;
    glGenBuffers(1, &m->buf);
    glBindBuffer(GL_ARRAY_BUFFER, m->buf);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(GXMeshVertex)), v, GL_STATIC_DRAW);
    g_gxUploadBytes += n * sizeof(GXMeshVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GXMeshVertex), (void *)offsetof(GXMeshVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, sizeof(GXMeshVertex), (void *)offsetof(GXMeshVertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GXMeshVertex), (void *)offsetof(GXMeshVertex, clr));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(GXMeshVertex), (void *)offsetof(GXMeshVertex, tex));
    if (skinned) {
        glEnableVertexAttribArray(12);
        glVertexAttribPointer(12, 1, GL_FLOAT, GL_FALSE, sizeof(GXMeshVertex), (void *)offsetof(GXMeshVertex, mtxIdx));
    }
    if (indices != NULL) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GLMesh *)indices)->buf);
    }
    return m;
}

static void GL_MeshDestroy(void *buf) {
    GLMesh *m = buf;

    if (m->vao != 0) {
        if (a.vao == m->vao) {
            glBindVertexArray(s.vao);
            a.vao = s.vao;
        }
        glDeleteVertexArrays(1, &m->vao);
    }
    glDeleteBuffers(1, &m->buf);
    SDL_free(m);
}

/* The attributes a mesh doesn't have, as a decoded vertex has them: the
 * second colour and texture coordinates 1-7 zero. */
static void MeshConstants(void) {
    int i;

    glVertexAttrib4f(3, 0.0f, 0.0f, 0.0f, 0.0f);
    for (i = 5; i < 12; i++) {
        glVertexAttrib4f((GLuint)i, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    s.meshConst = 1;
    s.meshSlot = -1.0f;
}

static void DrawMesh(const GXMeshDraw *d) {
    const GLMesh *v = d->vtx;

    if (a.vao != v->vao) {
        glBindVertexArray(v->vao);
        a.vao = v->vao;
    }
    if (!s.meshConst) {
        MeshConstants();
    }
    if (!v->skinned && s.meshSlot != d->slot) {
        glVertexAttrib1f(12, d->slot);
        s.meshSlot = d->slot;
    }
    if (d->idx == NULL) {
        glDrawArrays(GL_TRIANGLES, (GLint)d->first, (GLsizei)d->count);
    } else {
        glDrawElements(GL_TRIANGLES, (GLsizei)d->count, GL_UNSIGNED_SHORT,
                       (void *)(uintptr_t)(d->first * sizeof(u16)));
    }
    if (v->skinned) {
        s.meshSlot = -1.0f;     /* location 12 was an array */
    }
}

static void GL_Draw(const GXDrawCall *dc) {
    static const GLenum modes[] = {
        GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_LINES, GL_LINE_STRIP, GL_POINTS,
    };
    static const GLenum funcs[8] = {
        GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS,
    };
    const f32 *vp = dc->viewport;
    const int *sc = dc->scissor;
    int i, force = !a.full;

    if (force || a.fbo != s.fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
        a.fbo = s.fbo;
    }
    /* the program follows the FSBlock (and a pending one may have linked) */
    if (UploadUniforms(dc) || s.curPending || s.curProg == 0) {
        s.curProg = ProgramFor(dc->fs);
    }
    if (force || a.prog != s.curProg) {
        glUseProgram(s.curProg);
        a.prog = s.curProg;
    }
    if (s.curProg == s.prog) {
        g_gxShaderStats.general++;
    } else {
        g_gxShaderStats.special++;
    }

    /* GL rows are bottom-up */
    if (force || memcmp(a.vp, vp, sizeof(a.vp)) != 0) {
        glViewport((GLint)vp[0], (GLint)(s.efbH - vp[1] - vp[3]), (GLsizei)vp[2], (GLsizei)vp[3]);
        glDepthRange(vp[4], vp[5]);
        memcpy(a.vp, vp, sizeof(a.vp));
    }
    if (force || memcmp(a.sc, sc, sizeof(a.sc)) != 0) {
        glScissor(sc[0], s.efbH - sc[1] - sc[3], sc[2], sc[3]);
        memcpy(a.sc, sc, sizeof(a.sc));
    }
    if (force || !a.scissorOn) {
        glEnable(GL_SCISSOR_TEST);
        a.scissorOn = 1;
    }

    for (i = 0; i < 8; i++) {
        if (!force && a.unit[i].tex == dc->tex[i] &&
            memcmp(&a.unit[i].samp, &dc->samp[i], sizeof(GXSampler)) == 0) {
            continue;
        }
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)dc->tex[i]);
        if (dc->tex[i] != NULL) {
            SetSampler(&dc->samp[i]);
        }
        a.unit[i].tex = dc->tex[i];
        a.unit[i].samp = dc->samp[i];
    }

    if (force || a.blend != dc->blend) {
        if (dc->blend) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        a.blend = dc->blend;
    }
    if (dc->blend && (force || a.bsrc != dc->blendSrc || a.bdst != dc->blendDst ||
                      a.blendRevSub != dc->blendRevSub)) {
        glBlendFunc(BlendFactor(dc->blendSrc), BlendFactor(dc->blendDst));
        glBlendEquation(dc->blendRevSub ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        a.bsrc = dc->blendSrc;
        a.bdst = dc->blendDst;
        a.blendRevSub = dc->blendRevSub;
    }
    if (force || a.depthTest != dc->depthTest) {
        if (dc->depthTest) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        a.depthTest = dc->depthTest;
    }
    if (force || a.zfunc != funcs[dc->depthFunc & 7]) {
        glDepthFunc(funcs[dc->depthFunc & 7]);
        a.zfunc = funcs[dc->depthFunc & 7];
    }
    if (force || a.depthWrite != dc->depthWrite) {
        glDepthMask(dc->depthWrite ? GL_TRUE : GL_FALSE);
        a.depthWrite = dc->depthWrite;
    }
    if (force || a.colorWrite != dc->colorWrite || a.alphaWrite != dc->alphaWrite) {
        glColorMask(dc->colorWrite, dc->colorWrite, dc->colorWrite, dc->alphaWrite);
        a.colorWrite = dc->colorWrite;
        a.alphaWrite = dc->alphaWrite;
    }
    if (force || a.cull != dc->cull) {
        switch (dc->cull) {
        case 0: glDisable(GL_CULL_FACE); break;
        case 1: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
        case 2: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
        default: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT_AND_BACK); break;
        }
        a.cull = dc->cull;
    }

    if (dc->mesh != NULL) {
        DrawMesh(dc->mesh);
        return;
    }
    if (dc->batch != NULL) {
        DrawBatched(dc, modes[dc->prim]);
        s.meshConst = 0;
        return;
    }
    s.meshConst = 0;
    if (a.vao != s.vao) {
        glBindVertexArray(s.vao);
        a.vao = s.vao;
    }
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(dc->n * sizeof(GXVertex)), dc->v, GL_STREAM_DRAW);
    g_gxUploadBytes += dc->n * sizeof(GXVertex);
    glDrawArrays(modes[dc->prim], 0, (GLsizei)dc->n);
}

static void GL_Clear(const f32 c[4], f32 depth) {
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClearDepth(depth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    StateReset();
}

/* The copy uses its own texture and framebuffers; StateReset() makes the next
 * draw re-apply what it had cached. */
static void GL_DrawImage(const u8 *rgba, int width, int height, const int dst[4]) {
    static GLuint tex, readFbo;
    static int texW, texH;

    if (tex == 0) {
        glGenTextures(1, &tex);
        glGenFramebuffers(1, &readFbo);
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != texW || height != texH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        texW = width;
        texH = height;
        glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, readFbo);
    glBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, s.fbo);
    /* image rows are top-down, GL's bottom-up: flip while copying */
    glBlitFramebuffer(0, 0, width, height, dst[0], s.efbH - dst[1], dst[2], s.efbH - dst[3],
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    StateReset();
}

/* The display copy when the Graphics menu's Gamma row is off its middle: the blit
 * below can't change colours, so draw the EFB rectangle instead. shaders/present.*
 * are SDL_GPU's version of this. Made the first time it is needed. */
static const char s_gammaVS[] =
    "uniform vec4 uRect;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    vUV = mix(uRect.xy, uRect.zw, c);\n"
    "    gl_Position = vec4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);\n"
    "}\n";
static const char s_gammaFS[] =
    "uniform sampler2D uEFB;\n"
    "uniform vec4 uGamma;\n"
    "in vec2 vUV;\n"
    "out vec4 oColor;\n"
    "void main() {\n"
    "    oColor = vec4(pow(texture(uEFB, vUV).rgb, uGamma.xxx), 1.0);\n"
    "}\n";

static void GL_DrawGamma(const int src[4], const int lb[4], f32 power) {
    f32 rect[4], gamma[4] = { power, 0.0f, 0.0f, 0.0f };

    if (s.gammaProg == 0) {
        GLuint vs = Compile(GL_VERTEX_SHADER, s_gammaVS), fs = Compile(GL_FRAGMENT_SHADER, s_gammaFS);
        GLint ok = 0;

        s.gammaProg = glCreateProgram();
        glAttachShader(s.gammaProg, vs);
        glAttachShader(s.gammaProg, fs);
        glLinkProgram(s.gammaProg);
        glGetProgramiv(s.gammaProg, GL_LINK_STATUS, &ok);
        if (!ok) {
            Port_Fatal("Gamma shader link failed");
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
        s.gammaRect = glGetUniformLocation(s.gammaProg, "uRect");
        s.gammaPower = glGetUniformLocation(s.gammaProg, "uGamma");
        glGenVertexArrays(1, &s.gammaVao);
    }
    /* texture rows are bottom-up, src is top-down */
    rect[0] = (f32)src[0] / s.efbW;
    rect[1] = 1.0f - (f32)src[1] / s.efbH;
    rect[2] = (f32)src[2] / s.efbW;
    rect[3] = 1.0f - (f32)src[3] / s.efbH;
    glUseProgram(s.gammaProg);
    glUniform4fv(s.gammaRect, 1, rect);
    glUniform4fv(s.gammaPower, 1, gamma);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.fboColor);
    glBindVertexArray(s.gammaVao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glViewport(lb[0], lb[1], lb[2], lb[3]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    StateReset();
}

static void GL_CopyDisp(const int src[4]) {
    int w, h, lb[4];
    f32 power = Port_GfxGammaPower();

    SDL_GetWindowSizeInPixels(g_port.window, &w, &h);
    GX_Letterbox(w, h, src[2] - src[0], src[3] - src[1], lb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (power != 1.0f) {
        GL_DrawGamma(src, lb, power);
    } else {
        glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, s.fbo);
        glBlitFramebuffer(src[0], s.efbH - src[3], src[2], s.efbH - src[1], lb[0], lb[1], lb[0] + lb[2], lb[1] + lb[3],
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    /* The EFB alpha is game scratch (shadow volumes), not opacity, and the
     * blit copied it. Compositors that honour window alpha (macOS) would show
     * those frames as transparent/black; X11 ignores it. Make the window
     * opaque on every platform. */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

/* The storage is respecified on each copy since texCreate leaves it to the
 * first upload, and a texture only gets one copy per frame. */
static void GL_CopyEFB(void *tex, const int src[4]) {
    static GLuint fbo;
    int w = src[2] - src[0], h = src[3] - src[1];

    if (fbo == 0) {
        glGenFramebuffers(1, &fbo);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)(uintptr_t)tex, 0);
    glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, s.fbo);
    /* GL rows are bottom-up, texture rows top-down: flip while copying */
    glBlitFramebuffer(src[0], s.efbH - src[3], src[2], s.efbH - src[1], 0, h, w, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    StateReset();
}

static int GL_ReadEFB(u8 *rgba) {
    size_t row = (size_t)s.efbW * 4;
    u8 *tmp = SDL_malloc(row);
    GLenum err = glGetError();
    int y;

    if (err != GL_NO_ERROR) {
        Port_Log("GL error 0x%X", err);
    }
    if (tmp == NULL) {
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glReadPixels(0, 0, s.efbW, s.efbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (y = 0; y < s.efbH / 2; y++) {   /* GL rows are bottom-up */
        u8 *a = rgba + y * row, *b = rgba + (s.efbH - 1 - y) * row;
        memcpy(tmp, a, row); memcpy(a, b, row); memcpy(b, tmp, row);
    }
    SDL_free(tmp);
    return 1;
}

/* The asynchronous readbacks (grabStart/grabFetch): a pixel buffer each and
 * the fence after the read into it. */
static struct {
    GLuint pbo;
    GLsync fence;
    int w, h;
} s_grab[GXR_GRAB_SLOTS];

static int GL_GrabStart(int slot) {
    if (slot < 0 || slot >= GXR_GRAB_SLOTS || s_grab[slot].fence != NULL) {
        return 0;
    }
    if (s_grab[slot].pbo == 0) {
        glGenBuffers(1, &s_grab[slot].pbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s_grab[slot].pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)s.efbW * s.efbH * 4, NULL, GL_STREAM_READ);
    glReadPixels(0, 0, s.efbW, s.efbH, GL_RGBA, GL_UNSIGNED_BYTE, (void *)0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    s_grab[slot].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    s_grab[slot].w = s.efbW;
    s_grab[slot].h = s.efbH;
    return s_grab[slot].fence != NULL;
}

static int GL_GrabFetch(int slot, u8 *dst, int stride, int maxW, int maxH, int *w, int *h, int wait) {
    GLenum r;
    const u8 *map;
    int ok = -1, y;

    if (slot < 0 || slot >= GXR_GRAB_SLOTS || s_grab[slot].fence == NULL) {
        return -1;
    }
    r = glClientWaitSync(s_grab[slot].fence, GL_SYNC_FLUSH_COMMANDS_BIT, wait ? 1000000000ull : 0);
    for (y = 1; wait && r == GL_TIMEOUT_EXPIRED && y < 5; y++) {
        r = glClientWaitSync(s_grab[slot].fence, 0, 1000000000ull);   /* waiting: up to 5 s in all */
    }
    if (r == GL_TIMEOUT_EXPIRED && !wait) {
        return 0;
    }
    glDeleteSync(s_grab[slot].fence);   /* (waited and still not done: given up, the slot freed) */
    s_grab[slot].fence = NULL;
    if (r == GL_WAIT_FAILED || r == GL_TIMEOUT_EXPIRED || s_grab[slot].w > maxW || s_grab[slot].h > maxH) {
        return -1;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s_grab[slot].pbo);
    map = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)s_grab[slot].w * s_grab[slot].h * 4, GL_MAP_READ_BIT);
    if (map != NULL) {
        size_t row = (size_t)s_grab[slot].w * 4;

        for (y = 0; y < s_grab[slot].h; y++) {   /* GL rows are bottom-up */
            memcpy(dst + (size_t)y * (size_t)stride, map + (size_t)(s_grab[slot].h - 1 - y) * row, row);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        *w = s_grab[slot].w;
        *h = s_grab[slot].h;
        ok = 1;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return ok;
}

static void GL_Present(void) {
    TevGenStats();
    /* GXCopyDisp(clear) leaves the EFB bound (GL_Clear). Present from the
     * window's own framebuffer: Apple's GL only shows the swapped image when
     * it is bound; Mesa doesn't care. Draws rebind the EFB themselves. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GL_ApplyVSync();
    if (Port_ViewerHeadless()) {
        glFlush();      /* the viewer's hidden window: nothing to show */
        return;
    }
    /* blocks for vsync: let the audio thread run meanwhile */
    Port_InterruptWindowBegin();
    SDL_GL_SwapWindow(g_port.window);
    Port_InterruptWindowEnd();
}

const GXRenderer g_gxGL = {
    "opengl", GL_Open, GL_Close, GL_Init, GL_Resize, GL_TexFormat, GL_TexCreate, GL_TexUpload, GL_TexDestroy,
    GL_Draw, GL_Clear, GL_DrawImage, GL_CopyDisp, GL_CopyEFB, GL_ReadEFB, GL_Present,
    GL_MeshIndices, GL_MeshVertices, GL_MeshDestroy, GL_GrabStart, GL_GrabFetch,
};
