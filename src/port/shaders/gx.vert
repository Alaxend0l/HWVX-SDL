/* GX vertex stage: position/normal transform, the two lighting channels and
 * texture coordinate generation. The uniform block is GXVSUniforms
 * (gx_render.h); keep the two in step. */
#include "common.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 aClr0;
layout(location = 3) in vec4 aClr1;
layout(location = 4) in vec2 aTex0;
layout(location = 5) in vec2 aTex1;
layout(location = 6) in vec2 aTex2;
layout(location = 7) in vec2 aTex3;
layout(location = 8) in vec2 aTex4;
layout(location = 9) in vec2 aTex5;
layout(location = 10) in vec2 aTex6;
layout(location = 11) in vec2 aTex7;
layout(location = 12) in float aMtx;

UBO(1) VSBlock {
    vec4 uPosMtx[33];
    vec4 uTexMtx[30];
    vec4 uProj[4];        /* rows */
    ivec4 uTexGen[8];     /* func, src, mtx slot (-1 = identity), 0 */
    ivec4 uChan[4];       /* enable, ambSrc, matSrc, lightMask */
    ivec4 uChanFn[4];     /* diffFn, attnFn */
    vec4 uAmb[2];
    vec4 uMat[2];
    vec4 uLightPos[8];
    vec4 uLightDir[8];
    vec4 uLightColor[8];
    vec4 uLightA[8];
    vec4 uLightK[8];
    ivec4 uCounts;        /* numTexGens, numChans */
};

LOC(0) out vec4 vClr0;
LOC(1) out vec4 vClr1;
LOC(2) out vec3 vTex0;
LOC(3) out vec3 vTex1;
LOC(4) out vec3 vTex2;
LOC(5) out vec3 vTex3;
LOC(6) out vec3 vTex4;
LOC(7) out vec3 vTex5;
LOC(8) out vec3 vTex6;
LOC(9) out vec3 vTex7;
LOC(10) out float vViewZ;

vec3 xf(vec4 r0, vec4 r1, vec4 r2, vec4 p) { return vec3(dot(r0, p), dot(r1, p), dot(r2, p)); }

vec4 channel(int ci, int ai, vec3 vpos, vec3 n, vec4 vclr) {
    vec4 res;
    for (int k = 0; k < 2; k++) {
        int c = k == 0 ? ci : ai;
        vec4 mat = uChan[c].z != 0 ? vclr : uMat[ci];
        vec4 amb = uChan[c].y != 0 ? vclr : uAmb[ci];
        vec4 col = mat;
        if (uChan[c].x != 0) {
            vec4 lit = amb;
            for (int l = 0; l < 8; l++) {
                if ((uChan[c].w & (1 << l)) == 0) continue;
                vec3 ld = uLightPos[l].xyz - vpos;
                float dist = length(ld);
                ld = dist > 0.0 ? ld / dist : vec3(0.0);
                float diff = dot(n, ld);
                if (uChanFn[c].x == 2) diff = max(diff, 0.0);
                else if (uChanFn[c].x == 0) diff = 1.0;
                float att = 1.0;
                if (uChanFn[c].y == 1) {
                    float cs = max(dot(ld, -uLightDir[l].xyz), 0.0);
                    float aa = uLightA[l].x + uLightA[l].y * cs + uLightA[l].z * cs * cs;
                    float kk = uLightK[l].x + uLightK[l].y * dist + uLightK[l].z * dist * dist;
                    att = max(aa, 0.0) / max(kk, 1e-5);
                }
                lit += uLightColor[l] * diff * att;
            }
            col = mat * clamp(lit, 0.0, 1.0);
        }
        if (k == 0) res.rgb = col.rgb; else res.a = col.a;
    }
    return res;
}

void main() {
    vec2 tx[8] = vec2[8](aTex0, aTex1, aTex2, aTex3, aTex4, aTex5, aTex6, aTex7);
    vec3 vt[8];
    int m = int(aMtx) * 3;
    vec4 p = vec4(aPos, 1.0);
    vec3 vpos = xf(uPosMtx[m], uPosMtx[m + 1], uPosMtx[m + 2], p);
    vec3 n = normalize(xf(uPosMtx[m], uPosMtx[m + 1], uPosMtx[m + 2], vec4(aNrm, 0.0)) + vec3(0.0, 0.0, 1e-6));
    vec4 v = vec4(vpos, 1.0);
    vec4 clip = vec4(dot(uProj[0], v), dot(uProj[1], v), dot(uProj[2], v), dot(uProj[3], v));
    /* GX clip z is [-w, 0] */
#ifdef VULKAN
    clip.z = clip.z + clip.w;          /* to [0, w] (Vulkan, Metal, D3D12) */
    gl_PointSize = 1.0;
#else
    clip.z = 2.0 * clip.z + clip.w;    /* to [-w, w] (OpenGL) */
#endif
    gl_Position = clip;
    vViewZ = -vpos.z;
    vClr0 = uCounts.y > 0 ? channel(0, 2, vpos, n, aClr0) : vec4(0.0);
    vClr1 = uCounts.y > 1 ? channel(1, 3, vpos, n, aClr1) : vec4(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= uCounts.x) { vt[i] = vec3(0.0, 0.0, 1.0); continue; }
        int src = uTexGen[i].y;
        /* TEXCOORDn (12-18): an earlier generated coordinate (bump/emboss
         * chains); naming one not generated yet (the shadow pass uses
         * TEXCOORD0 for coordinate 0) reads the raw vertex coordinate, as the
         * hardware's source row does */
        vec4 s = src == 0 ? p : src == 1 ? vec4(aNrm, 1.0) : (src >= 4 && src <= 11) ? vec4(tx[src - 4], 1.0, 1.0)
               : (src >= 12 && src <= 18) ? (src - 12 < i ? vec4(vt[src - 12].xy, 1.0, 1.0) : vec4(tx[src - 12], 1.0, 1.0))
               : vec4(0.0, 0.0, 1.0, 1.0);
        int t = uTexGen[i].z;
        vec3 tc = t < 0 ? s.xyz : vec3(dot(uTexMtx[t * 3], s), dot(uTexMtx[t * 3 + 1], s), uTexGen[i].x == 0 ? dot(uTexMtx[t * 3 + 2], s) : 1.0);
        if (uTexGen[i].x == 1 && t < 0) tc.z = 1.0;
        vt[i] = tc;
    }
    vTex0 = vt[0]; vTex1 = vt[1]; vTex2 = vt[2]; vTex3 = vt[3];
    vTex4 = vt[4]; vTex5 = vt[5]; vTex6 = vt[6]; vTex7 = vt[7];
}
