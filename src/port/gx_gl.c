/* OpenGL 3.3 core renderer (--renderer opengl, the default). The EFB is a
 * framebuffer object; GL rows are bottom-up, so rectangles from gx_draw.c
 * (top-left origin) are flipped here. */
#include "gx_render.h"
#include "gx_shaders.h"

static struct {
    SDL_GLContext ctx;
    GLuint prog, vao, vbo, ubo[2];
    GLuint poolVao, poolVbo;    /* the display list cache's vertex pool */
    GLuint fbo, fboColor, fboDepth;
    int efbW, efbH;
    GXVSUniforms lastVS;
    GXFSUniforms lastFS;
    int uboValid;
    /* What the pool buffer holds: only vertices [0, poolClean) still match
     * memory, because an aborted capture or a cache reset recycles the tail.
     * poolGen changes when the pool starts over. */
    u32 poolGen, poolCap, poolClean;
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

static int GL_Open(void) {
    const char *nv;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    Port_CreateWindow(SDL_WINDOW_OPENGL);
    s.ctx = SDL_GL_CreateContext(g_port.window);
    if (s.ctx == NULL) {
        Port_Fatal("OpenGL 3.3 core context unavailable: %s", SDL_GetError());
    }
    SDL_GL_MakeCurrent(g_port.window, s.ctx);
    /* Vsync hides what the GPU costs, so HWVX_NO_VSYNC=1 frees the loop and the
     * frame rate in the HWVX_DUMP log becomes a measure of the work per frame. */
    nv = SDL_getenv("HWVX_NO_VSYNC");
    SDL_GL_SetSwapInterval(nv != NULL && *nv == '1' ? 0 : 1);
    GL_Load();
    return 1;
}

static void GL_Close(void) {
    SDL_GL_DestroyContext(s.ctx);
    s.ctx = NULL;
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
    GLuint vs, fs;
    GLint ok = 0;
    int i;

    s.efbW = efbW;
    s.efbH = efbH;
    vs = Compile(GL_VERTEX_SHADER, gx_vert_glsl);
    fs = Compile(GL_FRAGMENT_SHADER, gx_frag_glsl);
    s.prog = glCreateProgram();
    glAttachShader(s.prog, vs);
    glAttachShader(s.prog, fs);
    glLinkProgram(s.prog);
    glGetProgramiv(s.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(s.prog, sizeof(log), NULL, log);
        Port_Fatal("GX shader link failed:\n%s", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(s.prog);
    glUniformBlockBinding(s.prog, glGetUniformBlockIndex(s.prog, "VSBlock"), 0);
    glUniformBlockBinding(s.prog, glGetUniformBlockIndex(s.prog, "FSBlock"), 1);
    for (i = 0; i < 8; i++) {
        char name[8] = "uTex0";
        name[4] = (char)('0' + i);
        glUniform1i(glGetUniformLocation(s.prog, name), i);
    }
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

static void *GL_TexCreate(int w, int h, int levels) {
    GLuint name = 0;

    (void)w;
    (void)h;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    StateReset();
    return (void *)(uintptr_t)name;
}

static void GL_TexUpload(void *tex, int level, int w, int h, const u8 *rgba) {
    glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)tex);
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
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

static void UploadUniforms(const GXDrawCall *dc) {
    if (!s.uboValid || memcmp(&s.lastVS, dc->vs, sizeof(s.lastVS)) != 0) {
        s.lastVS = *dc->vs;
        glBindBuffer(GL_UNIFORM_BUFFER, s.ubo[0]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(GXVSUniforms), dc->vs, GL_STREAM_DRAW);
    }
    if (!s.uboValid || memcmp(&s.lastFS, dc->fs, sizeof(s.lastFS)) != 0) {
        s.lastFS = *dc->fs;
        glBindBuffer(GL_UNIFORM_BUFFER, s.ubo[1]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(GXFSUniforms), dc->fs, GL_STREAM_DRAW);
    }
    s.uboValid = 1;
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
        s.poolClean = b->used;
    }
    if (a.vao != s.poolVao) {
        glBindVertexArray(s.poolVao);
        a.vao = s.poolVao;
    }
    glDrawArrays(mode, (GLint)(dc->v - b->base), (GLsizei)dc->n);
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
    if (force || a.prog != s.prog) {
        glUseProgram(s.prog);
        a.prog = s.prog;
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

    UploadUniforms(dc);
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

    if (dc->batch != NULL) {
        DrawBatched(dc, modes[dc->prim]);
        return;
    }
    if (a.vao != s.vao) {
        glBindVertexArray(s.vao);
        a.vao = s.vao;
    }
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(dc->n * sizeof(GXVertex)), dc->v, GL_STREAM_DRAW);
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

static void GL_CopyDisp(const int src[4]) {
    int w, h, lb[4];

    SDL_GetWindowSizeInPixels(g_port.window, &w, &h);
    GX_Letterbox(w, h, src[2] - src[0], src[3] - src[1], lb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, s.fbo);
    glBlitFramebuffer(src[0], s.efbH - src[3], src[2], s.efbH - src[1], lb[0], lb[1], lb[0] + lb[2], lb[1] + lb[3],
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* The EFB alpha is game scratch (shadow volumes), not opacity, and the
     * blit copied it. Compositors that honour window alpha (macOS) would show
     * those frames as transparent/black; X11 ignores it. Make the window
     * opaque on every platform. */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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

static void GL_Present(void) {
    /* GXCopyDisp(clear) leaves the EFB bound (GL_Clear). Present from the
     * window's own framebuffer: Apple's GL only shows the swapped image when
     * it is bound; Mesa doesn't care. Draws rebind the EFB themselves. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* blocks for vsync: let the audio thread run meanwhile */
    Port_InterruptWindowBegin();
    SDL_GL_SwapWindow(g_port.window);
    Port_InterruptWindowEnd();
}

const GXRenderer g_gxGL = {
    "opengl", GL_Open, GL_Close, GL_Init, GL_Resize, GL_TexCreate, GL_TexUpload, GL_TexDestroy,
    GL_Draw, GL_Clear, GL_DrawImage, GL_CopyDisp, GL_ReadEFB, GL_Present,
};
