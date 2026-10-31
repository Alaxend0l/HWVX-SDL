/* GX rendering on OpenGL 3.3.
 *
 * The embedded frame buffer (EFB) is an off-screen framebuffer of 640x480
 * times the window scale; GXCopyDisp copies it to the window (letterboxed to
 * 4:3) and optionally clears it, like the copy to the external frame buffer.
 *
 * One "uber" shader implements the parts of the GX pipeline the game uses,
 * driven by uniforms: position/texture matrices, texture coordinate
 * generation, the two lighting channels, up to 16 TEV stages with the full
 * color/alpha combiner (no konst/swap tables, compare ops or indirect
 * texturing), alpha compare and fog. */
#include "gx_internal.h"

#define MAX_STAGES 16

static const char *s_vs =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec4 aClr0;\n"
    "layout(location=3) in vec4 aClr1;\n"
    "layout(location=4) in vec2 aTex[8];\n"
    "layout(location=12) in float aMtx;\n"
    "uniform vec4 uPosMtx[33];\n"
    "uniform vec4 uTexMtx[30];\n"
    "uniform mat4 uProj;\n"
    "uniform ivec4 uTexGen[8];\n"        /* func, src, mtx slot (-1 = identity), 0 */
    "uniform int uNumTexGens;\n"
    "uniform int uNumChans;\n"
    "uniform ivec4 uChan[4];\n"          /* enable, ambSrc, matSrc, lightMask */
    "uniform ivec4 uChanFn[4];\n"        /* diffFn, attnFn */
    "uniform vec4 uAmb[2];\n"
    "uniform vec4 uMat[2];\n"
    "uniform vec4 uLightPos[8];\n"
    "uniform vec4 uLightDir[8];\n"
    "uniform vec4 uLightColor[8];\n"
    "uniform vec4 uLightA[8];\n"
    "uniform vec4 uLightK[8];\n"
    "out vec4 vClr[2];\n"
    "out vec3 vTex[8];\n"
    "out float vViewZ;\n"
    "vec3 xf(vec4 r0, vec4 r1, vec4 r2, vec4 p) { return vec3(dot(r0,p), dot(r1,p), dot(r2,p)); }\n"
    "float lightChan(int c, vec3 vpos, vec3 n, int comp) {\n"
    "  return 0.0; }\n"
    "vec4 channel(int ci, int ai, vec3 vpos, vec3 n, vec4 vclr) {\n"
    "  vec4 res;\n"
    "  for (int k = 0; k < 2; k++) {\n"
    "    int c = k == 0 ? ci : ai;\n"
    "    vec4 mat = uChan[c].z != 0 ? vclr : uMat[ci];\n"
    "    vec4 amb = uChan[c].y != 0 ? vclr : uAmb[ci];\n"
    "    vec4 col = mat;\n"
    "    if (uChan[c].x != 0) {\n"
    "      vec4 lit = amb;\n"
    "      for (int l = 0; l < 8; l++) {\n"
    "        if ((uChan[c].w & (1 << l)) == 0) continue;\n"
    "        vec3 ld = uLightPos[l].xyz - vpos;\n"
    "        float dist = length(ld);\n"
    "        ld = dist > 0.0 ? ld / dist : vec3(0.0);\n"
    "        float diff = dot(n, ld);\n"
    "        if (uChanFn[c].x == 2) diff = max(diff, 0.0);\n"
    "        else if (uChanFn[c].x == 0) diff = 1.0;\n"
    "        float att = 1.0;\n"
    "        if (uChanFn[c].y == 1) {\n"
    "          float cs = max(dot(ld, -uLightDir[l].xyz), 0.0);\n"
    "          float aa = uLightA[l].x + uLightA[l].y * cs + uLightA[l].z * cs * cs;\n"
    "          float kk = uLightK[l].x + uLightK[l].y * dist + uLightK[l].z * dist * dist;\n"
    "          att = max(aa, 0.0) / max(kk, 1e-5);\n"
    "        }\n"
    "        lit += uLightColor[l] * diff * att;\n"
    "      }\n"
    "      col = mat * clamp(lit, 0.0, 1.0);\n"
    "    }\n"
    "    if (k == 0) res.rgb = col.rgb; else res.a = col.a;\n"
    "  }\n"
    "  return res;\n"
    "}\n"
    "void main() {\n"
    "  int m = int(aMtx) * 3;\n"
    "  vec4 p = vec4(aPos, 1.0);\n"
    "  vec3 vpos = xf(uPosMtx[m], uPosMtx[m+1], uPosMtx[m+2], p);\n"
    "  vec3 n = normalize(xf(uPosMtx[m], uPosMtx[m+1], uPosMtx[m+2], vec4(aNrm, 0.0)) + vec3(0.0, 0.0, 1e-6));\n"
    "  vec4 clip = uProj * vec4(vpos, 1.0);\n"
    "  clip.z = 2.0 * clip.z + clip.w;\n"   /* GX clip z is [-w, 0] */
    "  gl_Position = clip;\n"
    "  vViewZ = -vpos.z;\n"
    "  vClr[0] = uNumChans > 0 ? channel(0, 2, vpos, n, aClr0) : vec4(0.0);\n"
    "  vClr[1] = uNumChans > 1 ? channel(1, 3, vpos, n, aClr1) : vec4(0.0);\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    if (i >= uNumTexGens) { vTex[i] = vec3(0.0, 0.0, 1.0); continue; }\n"
    "    int src = uTexGen[i].y;\n"
    "    vec4 s = src == 0 ? p : src == 1 ? vec4(aNrm, 1.0) : (src >= 4 && src <= 11) ? vec4(aTex[src - 4], 1.0, 1.0)\n"
    /* TEXCOORDn: an earlier generated coordinate (bump/emboss chains); naming
     * one not generated yet (the shadow pass uses TEXCOORD0 for coordinate 0)
     * reads the raw vertex coordinate, as the hardware's source row does */
    "           : (src >= 12 && src <= 18) ? (src - 12 < i ? vec4(vTex[src - 12].xy, 1.0, 1.0) : vec4(aTex[src - 12], 1.0, 1.0))\n"
    "           : vec4(0.0, 0.0, 1.0, 1.0);\n"
    "    int t = uTexGen[i].z;\n"
    "    vec3 tc = t < 0 ? s.xyz : vec3(dot(uTexMtx[t*3], s), dot(uTexMtx[t*3+1], s), uTexGen[i].x == 0 ? dot(uTexMtx[t*3+2], s) : 1.0);\n"
    "    if (uTexGen[i].x == 1 && t < 0) tc.z = 1.0;\n"
    "    vTex[i] = tc;\n"
    "  }\n"
    "}\n";

static const char *s_fs =
    "#version 330 core\n"
    "in vec4 vClr[2];\n"
    "in vec3 vTex[8];\n"
    "in float vViewZ;\n"
    "out vec4 oColor;\n"
    "uniform sampler2D uTex0, uTex1, uTex2, uTex3, uTex4, uTex5, uTex6, uTex7;\n"
    "uniform int uNumStages;\n"
    "uniform ivec4 uOrder[16];\n"   /* texcoord, texmap, channel */
    "uniform ivec4 uCIn[16];\n"
    "uniform ivec4 uAIn[16];\n"
    "uniform ivec4 uCOp[16];\n"     /* op, bias, scale, clamp */
    "uniform ivec4 uAOp[16];\n"
    "uniform ivec4 uOut[16];\n"     /* color out reg, alpha out reg */
    "uniform vec4 uReg[4];\n"
    "uniform ivec4 uAlphaComp;\n"   /* comp0, ref0, op, comp1 */
    "uniform int uAlphaRef1;\n"
    "uniform ivec4 uFog;\n"         /* type */
    "uniform vec4 uFogParam;\n"     /* start, end */
    "uniform vec4 uFogColor;\n"
    "uniform int uDebug;\n"
    "vec4 tex(int map, vec2 uv) {\n"
    "  if (map == 0) return texture(uTex0, uv); if (map == 1) return texture(uTex1, uv);\n"
    "  if (map == 2) return texture(uTex2, uv); if (map == 3) return texture(uTex3, uv);\n"
    "  if (map == 4) return texture(uTex4, uv); if (map == 5) return texture(uTex5, uv);\n"
    "  if (map == 6) return texture(uTex6, uv); if (map == 7) return texture(uTex7, uv);\n"
    "  return vec4(1.0);\n"
    "}\n"
    "vec3 cin(int s, vec4 r[4], vec4 t, vec4 ras) {\n"
    "  if (s < 8) { vec4 v = r[s >> 1]; return (s & 1) != 0 ? vec3(v.a) : v.rgb; }\n"
    "  if (s == 8) return t.rgb; if (s == 9) return vec3(t.a);\n"
    "  if (s == 10) return ras.rgb; if (s == 11) return vec3(ras.a);\n"
    "  if (s == 12) return vec3(1.0); if (s == 13) return vec3(0.5);\n"
    "  return vec3(0.0);\n"
    "}\n"
    "float ain(int s, vec4 r[4], vec4 t, vec4 ras) {\n"
    "  if (s < 4) return r[s].a; if (s == 4) return t.a; if (s == 5) return ras.a;\n"
    "  return 0.0;\n"
    "}\n"
    "bool acomp(int f, float a, float ref) {\n"
    "  if (f == 0) return false; if (f == 1) return a < ref; if (f == 2) return a == ref;\n"
    "  if (f == 3) return a <= ref; if (f == 4) return a > ref; if (f == 5) return a != ref;\n"
    "  if (f == 6) return a >= ref; return true;\n"
    "}\n"
    "void main() {\n"
    "  if (uDebug != 0) { oColor = vec4(1.0, 0.0, 1.0, 1.0); return; }\n"
    "  vec4 r[4];\n"
    "  for (int i = 0; i < 4; i++) r[i] = uReg[i];\n"
    "  for (int s = 0; s < uNumStages; s++) {\n"
    "    vec4 t = vec4(1.0);\n"
    "    if (uOrder[s].y != 255) {\n"
    "      vec3 tc = vTex[uOrder[s].x & 7];\n"
    "      t = tex(uOrder[s].y & 7, tc.xy / (tc.z != 0.0 ? tc.z : 1.0));\n"
    "    }\n"
    "    vec4 ras = uOrder[s].z == 255 ? vec4(0.0) : vClr[uOrder[s].z & 1];\n"
    "    vec3 a = cin(uCIn[s].x, r, t, ras), b = cin(uCIn[s].y, r, t, ras);\n"
    "    vec3 c = cin(uCIn[s].z, r, t, ras), d = cin(uCIn[s].w, r, t, ras);\n"
    "    vec3 cv = mix(a, b, c);\n"
    "    cv = uCOp[s].x == 1 ? d - cv : d + cv;\n"
    "    cv += uCOp[s].y == 1 ? vec3(0.5) : uCOp[s].y == 2 ? vec3(-0.5) : vec3(0.0);\n"
    "    cv *= uCOp[s].z == 1 ? 2.0 : uCOp[s].z == 2 ? 4.0 : uCOp[s].z == 3 ? 0.5 : 1.0;\n"
    "    if (uCOp[s].w != 0) cv = clamp(cv, 0.0, 1.0);\n"
    "    float aa = ain(uAIn[s].x, r, t, ras), ab = ain(uAIn[s].y, r, t, ras);\n"
    "    float ac = ain(uAIn[s].z, r, t, ras), ad = ain(uAIn[s].w, r, t, ras);\n"
    "    float av = mix(aa, ab, ac);\n"
    "    av = uAOp[s].x == 1 ? ad - av : ad + av;\n"
    "    av += uAOp[s].y == 1 ? 0.5 : uAOp[s].y == 2 ? -0.5 : 0.0;\n"
    "    av *= uAOp[s].z == 1 ? 2.0 : uAOp[s].z == 2 ? 4.0 : uAOp[s].z == 3 ? 0.5 : 1.0;\n"
    "    if (uAOp[s].w != 0) av = clamp(av, 0.0, 1.0);\n"
    "    r[uOut[s].x & 3].rgb = cv;\n"
    "    r[uOut[s].y & 3].a = av;\n"
    "  }\n"
    "  vec4 o = clamp(r[uNumStages > 0 ? uOut[uNumStages - 1].x & 3 : 0], 0.0, 1.0);\n"
    "  if (uNumStages > 0) o.a = clamp(r[uOut[uNumStages - 1].y & 3].a, 0.0, 1.0);\n"
    "  bool p0 = acomp(uAlphaComp.x, o.a, float(uAlphaComp.y) / 255.0);\n"
    "  bool p1 = acomp(uAlphaComp.w, o.a, float(uAlphaRef1) / 255.0);\n"
    "  bool pass = uAlphaComp.z == 0 ? (p0 && p1) : uAlphaComp.z == 1 ? (p0 || p1) : uAlphaComp.z == 2 ? (p0 != p1) : (p0 == p1);\n"
    "  if (!pass) discard;\n"
    "  if (uFog.x != 0) {\n"
    "    float f = clamp((vViewZ - uFogParam.x) / max(uFogParam.y - uFogParam.x, 1e-5), 0.0, 1.0);\n"
    "    int ft = uFog.x & 7;\n"
    "    if (ft == 4) f = 1.0 - exp2(-8.0 * f); else if (ft == 5) f = 1.0 - exp2(-8.0 * f * f);\n"
    "    else if (ft == 6) f = exp2(-8.0 * (1.0 - f)); else if (ft == 7) f = exp2(-8.0 * (1.0 - f) * (1.0 - f));\n"
    "    o.rgb = mix(o.rgb, uFogColor.rgb, f);\n"
    "  }\n"
    "  oColor = o;\n"
    "}\n";

static u32 s_statDraws, s_statVerts;

static struct {
    GLuint prog, vao, vbo;
    GLuint fbo, fboColor, fboDepth;
    int efbW, efbH;
    GLint u[64];
} s;

enum {
    U_POSMTX, U_TEXMTX, U_PROJ, U_TEXGEN, U_NUMTEXGENS, U_NUMCHANS, U_CHAN, U_CHANFN, U_AMB, U_MAT,
    U_LPOS, U_LDIR, U_LCOLOR, U_LA, U_LK, U_NUMSTAGES, U_ORDER, U_CIN, U_AIN, U_COP, U_AOP, U_OUT,
    U_REG, U_ALPHACOMP, U_ALPHAREF1, U_FOG, U_FOGPARAM, U_FOGCOLOR, U_TEX0, U_DEBUG
};

static const char *s_uniformNames[] = {
    "uPosMtx", "uTexMtx", "uProj", "uTexGen", "uNumTexGens", "uNumChans", "uChan", "uChanFn", "uAmb", "uMat",
    "uLightPos", "uLightDir", "uLightColor", "uLightA", "uLightK", "uNumStages", "uOrder", "uCIn", "uAIn",
    "uCOp", "uAOp", "uOut", "uReg", "uAlphaComp", "uAlphaRef1", "uFog", "uFogParam", "uFogColor", "uTex0",
    "uDebug",
};

static GLuint Compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(sh, 1, &src, NULL);
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
    int scale = g_port.scale > 0 ? g_port.scale : 1;

    s.efbW = 640 * scale;
    s.efbH = 480 * scale;
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
}

void GX_DrawInit(void) {
    GLuint vs, fs;
    GLint ok = 0;
    int i;

    vs = Compile(GL_VERTEX_SHADER, s_vs);
    fs = Compile(GL_FRAGMENT_SHADER, s_fs);
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
    for (i = 0; i < (int)(sizeof(s_uniformNames) / sizeof(s_uniformNames[0])); i++) {
        s.u[i] = glGetUniformLocation(s.prog, s_uniformNames[i]);
    }
    for (i = 0; i < 8; i++) {
        char name[8] = "uTex0";
        name[4] = (char)('0' + i);
        glUniform1i(glGetUniformLocation(s.prog, name), i);
    }

    glGenVertexArrays(1, &s.vao);
    glBindVertexArray(s.vao);
    glGenBuffers(1, &s.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
#define ATTR(loc, n, field) \
    glEnableVertexAttribArray(loc); \
    glVertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void *)offsetof(GXVertex, field))
    ATTR(0, 3, pos);
    ATTR(1, 3, nrm);
    ATTR(2, 4, clr[0]);
    ATTR(3, 4, clr[1]);
    for (i = 0; i < 8; i++) {
        glEnableVertexAttribArray(4 + i);
        glVertexAttribPointer(4 + i, 2, GL_FLOAT, GL_FALSE, sizeof(GXVertex),
                              (void *)(offsetof(GXVertex, tex) + i * 2 * sizeof(f32)));
    }
    ATTR(12, 1, mtxIdx);
#undef ATTR
    CreateEFB();
}

/* ---- per-draw state ------------------------------------------------------ */

static void Color4(const GXColor *c, f32 *o) {
    o[0] = c->r / 255.0f;
    o[1] = c->g / 255.0f;
    o[2] = c->b / 255.0f;
    o[3] = c->a / 255.0f;
}

static GLenum BlendFactor(int f, int isSrc) {
    switch (f) {
    case 0: return GL_ZERO;
    case 1: return GL_ONE;
    case 2: return isSrc ? GL_DST_COLOR : GL_SRC_COLOR;
    case 3: return isSrc ? GL_ONE_MINUS_DST_COLOR : GL_ONE_MINUS_SRC_COLOR;
    case 4: return GL_SRC_ALPHA;
    case 5: return GL_ONE_MINUS_SRC_ALPHA;
    case 6: return GL_DST_ALPHA;
    default: return GL_ONE_MINUS_DST_ALPHA;
    }
}

static void ApplyState(void) {
    GXState *g = &g_gx;
    f32 buf[64 * 4];
    GLint ibuf[16 * 4];
    int i;

    /* viewport and scissor: GX origin is top-left, in 640x480 EFB pixels */
    {
        f32 sx = s.efbW / 640.0f, sy = s.efbH / 480.0f;
        glViewport((GLint)(g->viewport[0] * sx), (GLint)(s.efbH - (g->viewport[1] + g->viewport[3]) * sy),
                   (GLsizei)(g->viewport[2] * sx), (GLsizei)(g->viewport[3] * sy));
        glDepthRange(g->viewport[4], g->viewport[5]);
        glEnable(GL_SCISSOR_TEST);
        glScissor((GLint)(g->scissor[0] * sx), (GLint)(s.efbH - (g->scissor[1] + g->scissor[3]) * sy),
                  (GLsizei)(g->scissor[2] * sx), (GLsizei)(g->scissor[3] * sy));
    }

    glUniform4fv(s.u[U_POSMTX], 3 * (GX_POSMTX_IDENTITY + 1), &g->posMtx[0][0][0]);
    glUniform4fv(s.u[U_TEXMTX], 30, &g->texMtx[0][0][0]);
    glUniformMatrix4fv(s.u[U_PROJ], 1, GL_TRUE, &g->proj[0][0]);

    for (i = 0; i < 8; i++) {
        u32 m = g->texGen[i].mtx;
        ibuf[i * 4 + 0] = g->texGen[i].func;
        ibuf[i * 4 + 1] = g->texGen[i].src;
        ibuf[i * 4 + 2] = (m >= 30 && m < 60) ? (int)((m - 30) / 3) : -1;
        ibuf[i * 4 + 3] = 0;
    }
    glUniform4iv(s.u[U_TEXGEN], 8, ibuf);
    glUniform1i(s.u[U_NUMTEXGENS], g->numTexGens);

    glUniform1i(s.u[U_NUMCHANS], g->numChans);
    for (i = 0; i < 4; i++) {
        ibuf[i * 4 + 0] = g->chan[i].enable;
        ibuf[i * 4 + 1] = g->chan[i].ambSrc;
        ibuf[i * 4 + 2] = g->chan[i].matSrc;
        ibuf[i * 4 + 3] = (GLint)g->chan[i].lightMask;
    }
    glUniform4iv(s.u[U_CHAN], 4, ibuf);
    for (i = 0; i < 4; i++) {
        ibuf[i * 4 + 0] = g->chan[i].diffFn;
        ibuf[i * 4 + 1] = g->chan[i].attnFn;
        ibuf[i * 4 + 2] = ibuf[i * 4 + 3] = 0;
    }
    glUniform4iv(s.u[U_CHANFN], 4, ibuf);
    Color4(&g->ambColor[0], buf);
    Color4(&g->ambColor[1], buf + 4);
    glUniform4fv(s.u[U_AMB], 2, buf);
    Color4(&g->matColor[0], buf);
    Color4(&g->matColor[1], buf + 4);
    glUniform4fv(s.u[U_MAT], 2, buf);
    for (i = 0; i < 8; i++) memcpy(buf + i * 4, g->light[i].pos, 16);
    glUniform4fv(s.u[U_LPOS], 8, buf);
    for (i = 0; i < 8; i++) memcpy(buf + i * 4, g->light[i].dir, 16);
    glUniform4fv(s.u[U_LDIR], 8, buf);
    for (i = 0; i < 8; i++) memcpy(buf + i * 4, g->light[i].color, 16);
    glUniform4fv(s.u[U_LCOLOR], 8, buf);
    for (i = 0; i < 8; i++) memcpy(buf + i * 4, g->light[i].a, 16);
    glUniform4fv(s.u[U_LA], 8, buf);
    for (i = 0; i < 8; i++) memcpy(buf + i * 4, g->light[i].k, 16);
    glUniform4fv(s.u[U_LK], 8, buf);

    /* TEV */
    glUniform1i(s.u[U_NUMSTAGES], g->numTevStages);
    {
        GLint order[64], cin[64], ain[64], cop[64], aop[64], out[64];
        for (i = 0; i < MAX_STAGES; i++) {
            const GXTevStage *t = &g->tev[i];
            int c = t->channel;
            order[i * 4 + 0] = t->texCoord;
            order[i * 4 + 1] = t->texMap == 0xFF ? 255 : t->texMap;
            /* COLOR0/ALPHA0/COLOR0A0 -> 0, COLOR1/ALPHA1/COLOR1A1 -> 1 */
            order[i * 4 + 2] = c == 0xFF ? 255 : (c == 1 || c == 3 || c == 5) ? 1 : (c == 0 || c == 2 || c == 4) ? 0 : 255;
            order[i * 4 + 3] = 0;
            memcpy(&cin[i * 4], (GLint[4]){ t->colorIn[0], t->colorIn[1], t->colorIn[2], t->colorIn[3] }, 16);
            memcpy(&ain[i * 4], (GLint[4]){ t->alphaIn[0], t->alphaIn[1], t->alphaIn[2], t->alphaIn[3] }, 16);
            memcpy(&cop[i * 4], (GLint[4]){ t->colorOp, t->colorBias, t->colorScale, t->colorClamp }, 16);
            memcpy(&aop[i * 4], (GLint[4]){ t->alphaOp, t->alphaBias, t->alphaScale, t->alphaClamp }, 16);
            memcpy(&out[i * 4], (GLint[4]){ t->colorOut, t->alphaOut, 0, 0 }, 16);
        }
        glUniform4iv(s.u[U_ORDER], 16, order);
        glUniform4iv(s.u[U_CIN], 16, cin);
        glUniform4iv(s.u[U_AIN], 16, ain);
        glUniform4iv(s.u[U_COP], 16, cop);
        glUniform4iv(s.u[U_AOP], 16, aop);
        glUniform4iv(s.u[U_OUT], 16, out);
    }
    for (i = 0; i < 4; i++) Color4(&g->tevReg[i], buf + i * 4);
    glUniform4fv(s.u[U_REG], 4, buf);
    glUniform4iv(s.u[U_ALPHACOMP], 1, (GLint[4]){ g->alphaComp[0], g->alphaComp[1], g->alphaComp[2], g->alphaComp[3] });
    glUniform1i(s.u[U_ALPHAREF1], g->alphaComp[4]);
    glUniform4iv(s.u[U_FOG], 1, (GLint[4]){ g->fogType, 0, 0, 0 });
    glUniform4fv(s.u[U_FOGPARAM], 1, (f32[4]){ g->fogStart, g->fogEnd, g->fogNear, g->fogFar });
    Color4(&g->fogColor, buf);
    glUniform4fv(s.u[U_FOGCOLOR], 1, buf);

    /* textures */
    for (i = 0; i < 8; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, g->texLoaded[i] ? GX_TexGet(&g->texObj[i]) : 0);
    }
    glActiveTexture(GL_TEXTURE0);

    /* pixel engine */
    if (g->blendType == 1 || g->blendType == 3) {
        glEnable(GL_BLEND);
        glBlendFunc(BlendFactor(g->blendSrc, 1), BlendFactor(g->blendDst, 0));
        glBlendEquation(g->blendType == 3 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        if (g->blendType == 3) {
            glBlendFunc(GL_ONE, GL_ONE);
        }
    } else {
        glDisable(GL_BLEND);
    }
    if (g->zEnable) {
        static const GLenum f[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(f[g->zFunc & 7]);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(g->zUpdate ? GL_TRUE : GL_FALSE);
    glColorMask(g->colorUpdate, g->colorUpdate, g->colorUpdate, g->alphaUpdate);
    /* GX front faces are clockwise */
    glFrontFace(GL_CW);
    switch (g->cullMode) {
    case 0: glDisable(GL_CULL_FACE); break;
    case 1: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
    case 2: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
    default: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT_AND_BACK); break;
    }

    /* HWVX_GXDEBUG=1: everything magenta, no depth/blend/cull */
    {
        static int dbg = -1;
        if (dbg < 0) {
            const char *e = SDL_getenv("HWVX_GXDEBUG");
            dbg = e != NULL && *e == '1';
        }
        glUniform1i(s.u[U_DEBUG], dbg);
        if (dbg) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
    }
}

/* ---- primitives ---------------------------------------------------------- */

static GXVertex *s_tri;
static u32 s_triCap;

static GXVertex *Reserve(u32 n) {
    if (n > s_triCap) {
        s_triCap = n + 1024;
        s_tri = SDL_realloc(s_tri, s_triCap * sizeof(GXVertex));
    }
    return s_tri;
}

void GX_Draw(int prim, const GXVertex *v, u32 n) {
    GLenum mode = GL_TRIANGLES;
    const GXVertex *src = v;
    u32 count = n, i;

    if (n == 0) {
        return;
    }
    s_statDraws++;
    s_statVerts += n;
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glUseProgram(s.prog);
    glBindVertexArray(s.vao);
    ApplyState();
    switch (prim) {
    case GX_QUADS: {
        GXVertex *t = Reserve(n / 4 * 6);
        count = 0;
        for (i = 0; i + 3 < n; i += 4) {
            t[count++] = v[i]; t[count++] = v[i + 1]; t[count++] = v[i + 2];
            t[count++] = v[i]; t[count++] = v[i + 2]; t[count++] = v[i + 3];
        }
        src = t;
        break;
    }
    case GX_TRIANGLES: mode = GL_TRIANGLES; break;
    case GX_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; break;
    case GX_TRIANGLEFAN: mode = GL_TRIANGLE_FAN; break;
    case GX_LINES: mode = GL_LINES; break;
    case GX_LINESTRIP: mode = GL_LINE_STRIP; break;
    case GX_POINTS: mode = GL_POINTS; break;
    default: return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(GXVertex)), src, GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)count);
}

/* ---- frame --------------------------------------------------------------- */

static void ClearEFB(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(g_gx.clearColor.r / 255.0f, g_gx.clearColor.g / 255.0f, g_gx.clearColor.b / 255.0f,
                 g_gx.clearColor.a / 255.0f);
    glClearDepth((g_gx.clearZ & 0xFFFFFF) / (f64)0xFFFFFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GX_DrawBeginFrame(void) {
    static int first = 1;
    if (first) {
        first = 0;
        ClearEFB();
    }
}

/* Movie frames (thp.c): an RGBA image, top row first, stretched over the
 * whole EFB. Texture bindings and the scissor are reapplied by the next draw. */
void GX_PortDrawImage(const u8 *rgba, int width, int height) {
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
    glBlitFramebuffer(0, 0, width, height, 0, s.efbH, s.efbW, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    GX_DrawCopyDisp(0);   /* movies never call GXCopyDisp: copy to the window here */
}

/* Debugging: HWVX_DUMP=dir saves every 60th copied frame as dir/frameNNNNN.bmp. */
static void DumpEFB(void) {
    static const char *dir;
    static int checked, count;
    SDL_Surface *surf;
    char path[512];
    int y;

    if (!checked) {
        checked = 1;
        dir = SDL_getenv("HWVX_DUMP");
    }
    if (dir == NULL || (count++ % 60) != 0) {
        return;
    }
    Port_Log("frame %d: %u draws, %u vertices since last dump, GL error 0x%X", count - 1, s_statDraws, s_statVerts,
             glGetError());
    s_statDraws = s_statVerts = 0;
    /* XBGR: the EFB alpha is game scratch (shadow volumes), not opacity */
    surf = SDL_CreateSurface(s.efbW, s.efbH, SDL_PIXELFORMAT_XBGR8888);
    if (surf == NULL) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glReadPixels(0, 0, s.efbW, s.efbH, GL_RGBA, GL_UNSIGNED_BYTE, surf->pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (y = 0; y < s.efbH / 2; y++) { /* GL rows are bottom-up */
        u8 tmp[640 * 4 * 8];
        u8 *a = (u8 *)surf->pixels + y * surf->pitch, *b = (u8 *)surf->pixels + (s.efbH - 1 - y) * surf->pitch;
        size_t n = (size_t)s.efbW * 4 < sizeof(tmp) ? (size_t)s.efbW * 4 : sizeof(tmp);
        memcpy(tmp, a, n); memcpy(a, b, n); memcpy(b, tmp, n);
    }
    SDL_snprintf(path, sizeof(path), "%s/frame%05d.bmp", dir, count - 1);
    SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
}

/* EFB -> window, letterboxed to 4:3. */
void GX_DrawCopyDisp(int clear) {
    int w, h, dw, dh;

    SDL_GetWindowSizeInPixels(g_port.window, &w, &h);
    dw = w;
    dh = w * 3 / 4;
    if (dh > h) {
        dh = h;
        dw = h * 4 / 3;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, s.fbo);
    glBlitFramebuffer(0, 0, s.efbW, s.efbH, (w - dw) / 2, (h - dh) / 2, (w - dw) / 2 + dw, (h - dh) / 2 + dh,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    DumpEFB();
    if (clear) {
        ClearEFB();
    }
}

void GX_DrawPresent(void) {
    SDL_GL_SwapWindow(g_port.window);
}
