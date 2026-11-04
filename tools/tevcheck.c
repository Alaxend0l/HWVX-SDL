/* tevcheck: do the specialised pixel shaders (src/port/gx_tevgen.c) give the
 * same pixels as the general one (shaders/gx.frag)?
 *
 * For each pixel-stage setup it draws a quad through both, with the same
 * random textures, registers, alpha refs and fog and varyings that sweep a
 * range, into a float target, and compares every pixel. Discarded pixels
 * keep a sentinel colour, so discarding differently is a mismatch; values
 * may differ by TOLERANCE, since with the setup known the compiler can fuse
 * and reorder the arithmetic differently (a few float ulps, far below the
 * 1/255 the EFB keeps). Setups come from an
 * HWVX_TEVKEYS file and/or are random (every input, op and register):
 *
 *   tools/tevcheck.sh [KEYS_FILE] [--random N] [--seed S] [--trials N] [--exact] [-v]
 *
 * (--trials: random register/ref/fog values per setup, default 4; -v: print
 * the setup and generated source of a mismatch.)
 *
 * Why a tolerance, and why some random setups still "mismatch": knowing the
 * setup, the compiler fuses and reorders differently, so values can differ
 * by a few ulps (scaled up by 4x stages), and when the alpha lands exactly on
 * a ref (8-bit texels and refs both being n/255) an ulp flips the alpha
 * test. Neither shader is more right: GX itself compares 8-bit integers.
 * --exact takes rounding out (registers, texels, colours and fog are
 * multiples of 1/4, colours constant over the quad); then every discard must
 * agree and any mismatch is a generator bug.
 *
 * Needs an OpenGL 3.3 context (a display). Exit status 1 on any mismatch. */
#include "gx_tevgen.h"
#include "gx_shaders.h"

#include <stdio.h>

#define W 96
#define H 96
#define TOLERANCE 1e-4f

void Port_Fatal(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}

void Port_Log(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static u32 s_rng = 1;
static int s_exact;

static u32 Rand(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* --exact: multiples of 1/4, so the combiners compute without rounding and
 * the two shaders must agree on every discard (see main). */
static f32 RandF(void) {
    return s_exact ? (f32)(Rand() % 5) * 0.25f : (f32)(Rand() & 0xFFFF) / 65535.0f;
}

/* Varyings for gx.frag's inputs, varied over the quad. Texture coordinates
 * run past [0, 1] and their q through 0, colours past [0, 1] too (the
 * combiner sees unclamped values in places). */
static const char s_vert[] =
    "#version 330 core\n"
    "out vec4 vClr0; out vec4 vClr1;\n"
    "out vec3 vTex0; out vec3 vTex1; out vec3 vTex2; out vec3 vTex3;\n"
    "out vec3 vTex4; out vec3 vTex5; out vec3 vTex6; out vec3 vTex7;\n"
    "out float vViewZ;\n"
    "uniform vec4 uSeed;\n"
    "uniform vec4 uC0, uC1;\n"
    "uniform int uExact;\n"
    "vec3 tc(float k, vec2 p) {\n"
    "    return vec3(p.x * (1.5 + k) + uSeed.x * k, p.y * (2.0 - 0.3 * k) - uSeed.y,\n"
    "                k == 3.0 ? p.x * p.y : 1.0 + 0.4 * p.y + 0.1 * k);\n"
    "}\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID & 1) != 0 ? 1.0 : -1.0, (gl_VertexID & 2) != 0 ? 1.0 : -1.0);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "    vClr0 = vec4(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y, uSeed.z, 0.5 + 0.6 * p.x * p.y);\n"
    "    vClr1 = vec4(uSeed.w, 0.5 + 0.5 * p.y, 0.5 - 0.5 * p.x, 0.5 - 0.5 * p.y);\n"
    "    vTex0 = tc(0.0, p); vTex1 = tc(1.0, p); vTex2 = tc(2.0, p); vTex3 = tc(3.0, p);\n"
    "    vTex4 = tc(4.0, p); vTex5 = tc(5.0, p); vTex6 = tc(6.0, p); vTex7 = tc(7.0, p);\n"
    "    vViewZ = 100.0 + 900.0 * (0.5 + 0.5 * p.x);\n"
    "    if (uExact != 0) { vClr0 = uC0; vClr1 = uC1; vViewZ = 500.0; }\n"
    "}\n";

static GLuint Compile(GLenum type, const char *a, const char *b) {
    const char *parts[2] = { a, b };
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "compile failed:\n%s\n--- source ---\n%s\n", log, b);
        exit(2);
    }
    return sh;
}

static GLuint Link(GLuint vs, const char *fsSrc) {
    GLuint fs = Compile(GL_FRAGMENT_SHADER, "#version 330 core\n", fsSrc), p = glCreateProgram();
    GLint ok = 0;
    int i;

    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        Port_Fatal("link failed:\n%s", log);
    }
    glDeleteShader(fs);
    glUseProgram(p);
    glUniformBlockBinding(p, glGetUniformBlockIndex(p, "FSBlock"), 1);
    for (i = 0; i < 8; i++) {
        char name[8] = "uTex0";
        name[4] = (char)('0' + i);
        glUniform1i(glGetUniformLocation(p, name), i);
    }
    return p;
}

static void Render(GLuint prog, const f32 seed[4], const f32 c[8], f32 *out) {
    glUseProgram(prog);
    glUniform4fv(glGetUniformLocation(prog, "uSeed"), 1, seed);
    glUniform4fv(glGetUniformLocation(prog, "uC0"), 1, c);
    glUniform4fv(glGetUniformLocation(prog, "uC1"), 1, c + 4);
    glUniform1i(glGetUniformLocation(prog, "uExact"), s_exact);
    glClearColor(-7.0f, -7.0f, -7.0f, -7.0f);   /* discarded pixels keep this */
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out);
}

/* A random setup: every field in its full range, plus some out-of-range
 * values gx.frag handles (unknown inputs, ops and compare functions). */
static void RandomFS(GXFSUniforms *fs) {
    int i, j;

    SDL_zerop(fs);
    fs->misc[0] = (int)(Rand() % 17);
    for (i = 0; i < 16; i++) {
        fs->order[i][0] = (int)(Rand() % 8);
        fs->order[i][1] = Rand() % 5 == 0 ? 255 : (int)(Rand() % 8);
        fs->order[i][2] = Rand() % 5 == 0 ? 255 : (int)(Rand() % 2);
        for (j = 0; j < 4; j++) {
            fs->cin[i][j] = (int)(Rand() % 16);
            fs->ain[i][j] = (int)(Rand() % 8);
        }
        fs->cop[i][0] = (int)(Rand() % 2); fs->cop[i][1] = (int)(Rand() % 4);
        fs->cop[i][2] = (int)(Rand() % 4); fs->cop[i][3] = (int)(Rand() % 2);
        fs->aop[i][0] = (int)(Rand() % 2); fs->aop[i][1] = (int)(Rand() % 4);
        fs->aop[i][2] = (int)(Rand() % 4); fs->aop[i][3] = (int)(Rand() % 2);
        fs->out[i][0] = (int)(Rand() % 4);
        fs->out[i][1] = (int)(Rand() % 4);
    }
    fs->alphaComp[0] = (int)(Rand() % 9);
    fs->alphaComp[2] = (int)(Rand() % 5);
    fs->alphaComp[3] = (int)(Rand() % 9);
    fs->misc[2] = Rand() % 3 == 0 ? 0 : (int)(Rand() % 8);
}

/* What doesn't pick the shader: registers, alpha refs, fog range and colour. */
static void RandomValues(GXFSUniforms *fs) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            fs->reg[i][j] = s_exact ? (f32)((int)(Rand() % 13) - 4) * 0.25f : RandF() * 2.5f - 1.0f;
        }
    }
    fs->alphaComp[1] = (int)(Rand() % 256);
    fs->misc[1] = (int)(Rand() % 256);
    fs->fogParam[0] = s_exact ? 256.0f : 100.0f + RandF() * 300.0f;
    fs->fogParam[1] = s_exact ? 768.0f : fs->fogParam[0] + RandF() * 700.0f;
    for (j = 0; j < 4; j++) {
        fs->fogColor[j] = RandF();
    }
}

static GLuint s_vs, s_uber;
static char *s_decl;
static f32 s_a[W * H * 4], s_b[W * H * 4];
static int s_checked, s_failed, s_trials = 4, s_verbose;
static double s_values, s_same;
static f32 s_maxDiff;

static void Check(const GXFSUniforms *setup, const char *what) {
    GXTevKey key;
    GXFSUniforms fs = *setup;
    char *src;
    GLuint spec;
    int trial, failedTrials = 0;
    size_t worst = 0;

    fs.misc[3] = 0;
    if (!GX_TevKey(&fs, &key)) {
        return;                     /* the general shader's own */
    }
    src = GX_TevGenGLSL(&key, s_decl);
    spec = Link(s_vs, src);
    for (trial = 0; trial < s_trials; trial++) {
        f32 seed[4] = { RandF(), RandF(), RandF(), RandF() }, c[8];
        int k;

        for (k = 0; k < 8; k++) {
            c[k] = RandF();
        }
        size_t i, bad = 0, first = 0, exact = 0;

        RandomValues(&fs);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(fs), &fs, GL_STATIC_DRAW);
        Render(s_uber, seed, c, s_a);
        Render(spec, seed, c, s_b);
        for (i = 0; i < W * H * 4; i++) {
            f32 a = s_a[i], b = s_b[i], d = a > b ? a - b : b - a;
            int discA = a == -7.0f, discB = b == -7.0f;

            exact += memcmp(&a, &b, sizeof(f32)) == 0;
            if (discA != discB || !(d <= TOLERANCE)) {
                if (bad++ == 0) {
                    first = i;
                }
            } else if (d > s_maxDiff) {
                s_maxDiff = d;
            }
        }
        s_values += W * H * 4;
        s_same += exact;
        if (bad != 0) {
            if (failedTrials++ == 0 && s_verbose) {
                char hex[2 * sizeof(GXFSUniforms) + 1];

                GX_TevKeyToHex(&fs, hex);
                printf("  setup: %s\n--- generated ---\n%s\n", hex, src);
            }
            if (bad > worst) {
                worst = bad;
                printf("MISMATCH %s trial %d: %zu values differ; first at pixel %zu ch %zu: "
                       "general %.9g, specialised %.9g\n",
                       what, trial, bad, first / 4, first % 4, s_a[first], s_b[first]);
            }
        }
    }
    if (failedTrials != 0) {
        printf("  %s: %d of %d trials mismatched\n", what, failedTrials, s_trials);
        s_failed++;
    }
    s_checked++;
    glDeleteProgram(spec);
    SDL_free(src);
}

int main(int argc, char **argv) {
    const char *keysFile = NULL, *end;
    int nRandom = 0, i;
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint fbo, rt, ubo, vao, tex[8];

    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--random") == 0 && i + 1 < argc) {
            nRandom = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--trials") == 0 && i + 1 < argc) {
            s_trials = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--exact") == 0) {
            s_exact = 1;
        } else if (SDL_strcmp(argv[i], "-v") == 0) {
            s_verbose = 1;
        } else if (SDL_strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            s_rng = (u32)SDL_atoi(argv[++i]) | 1;
        } else {
            keysFile = argv[i];
        }
    }
    if (keysFile == NULL && nRandom == 0) {
        fprintf(stderr, "usage: tevcheck [KEYS_FILE] [--random N] [--seed S] [--trials N] [--exact] [-v]\n");
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Port_Fatal("SDL_Init: %s", SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    win = SDL_CreateWindow("tevcheck", W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (win == NULL || (ctx = SDL_GL_CreateContext(win)) == NULL) {
        Port_Fatal("no OpenGL 3.3 context: %s", SDL_GetError());
    }
    GL_Load();

    end = SDL_strstr(gx_frag_glsl, "\nvec4 tex(int map");
    if (end == NULL) {
        Port_Fatal("gx.frag has no \"vec4 tex(int map\" (gx_gl.c FragDecl)");
    }
    s_decl = SDL_strndup(gx_frag_glsl, (size_t)(end - gx_frag_glsl) + 1);
    s_vs = Compile(GL_VERTEX_SHADER, "", s_vert);
    s_uber = Link(s_vs, gx_frag_glsl);

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Port_Fatal("float framebuffer incomplete");
    }
    glViewport(0, 0, W, H);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);
    glGenTextures(8, tex);
    for (i = 0; i < 8; i++) {
        static u8 px[16 * 16 * 4];
        static f32 pxf[16 * 16 * 4];
        size_t j;

        for (j = 0; j < sizeof(px); j++) {
            px[j] = (u8)Rand();
            pxf[j] = RandF();
        }
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        if (s_exact) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 16, 16, 0, GL_RGBA, GL_FLOAT, pxf);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    if (keysFile != NULL) {
        char *text = SDL_LoadFile(keysFile, NULL), *line, *save = NULL;
        int n = 0;

        if (text == NULL) {
            Port_Fatal("can't read %s: %s", keysFile, SDL_GetError());
        }
        for (line = SDL_strtok_r(text, "\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\n", &save)) {
            GXFSUniforms fs;
            char what[64];

            n++;
            if (*line == '#') {
                continue;
            }
            if (!GX_TevKeyFromHex(line, &fs)) {
                fprintf(stderr, "%s:%d: malformed, skipped\n", keysFile, n);
                continue;
            }
            SDL_snprintf(what, sizeof(what), "%s:%d", keysFile, n);
            Check(&fs, what);
        }
        SDL_free(text);
    }
    for (i = 0; i < nRandom; i++) {
        GXFSUniforms fs;
        char what[32];

        RandomFS(&fs);
        SDL_snprintf(what, sizeof(what), "random #%d", i);
        Check(&fs, what);
    }
    printf("tevcheck: %d setups, %d mismatched; %.4f%% of values bit-identical, largest difference %.3g\n",
           s_checked, s_failed, s_values > 0 ? 100.0 * s_same / s_values : 100.0, (double)s_maxDiff);
    return s_failed != 0;
}
