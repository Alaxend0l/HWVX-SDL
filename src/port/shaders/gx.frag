/* GX pixel stage: up to 16 TEV stages with the full color/alpha combiner (no
 * konst/swap tables, compare ops or indirect texturing), alpha compare and
 * fog. The uniform block is GXFSUniforms (gx_render.h); keep the two in
 * step. */
#include "common.glsl"

LOC(0) in vec4 vClr0;
LOC(1) in vec4 vClr1;
LOC(2) in vec3 vTex0;
LOC(3) in vec3 vTex1;
LOC(4) in vec3 vTex2;
LOC(5) in vec3 vTex3;
LOC(6) in vec3 vTex4;
LOC(7) in vec3 vTex5;
LOC(8) in vec3 vTex6;
LOC(9) in vec3 vTex7;
LOC(10) in float vViewZ;

layout(location = 0) out vec4 oColor;

SAMPLER(0) uTex0;
SAMPLER(1) uTex1;
SAMPLER(2) uTex2;
SAMPLER(3) uTex3;
SAMPLER(4) uTex4;
SAMPLER(5) uTex5;
SAMPLER(6) uTex6;
SAMPLER(7) uTex7;

UBO(3) FSBlock {
    ivec4 uOrder[16];   /* texcoord, texmap, channel */
    ivec4 uCIn[16];
    ivec4 uAIn[16];
    ivec4 uCOp[16];     /* op, bias, scale, clamp */
    ivec4 uAOp[16];
    ivec4 uOut[16];     /* color out reg, alpha out reg */
    vec4 uReg[4];
    ivec4 uAlphaComp;   /* comp0, ref0, op, comp1 */
    ivec4 uMisc;        /* numStages, alphaRef1, fog type, debug */
    vec4 uFogParam;     /* start, end */
    vec4 uFogColor;
};

vec4 tex(int map, vec2 uv) {
    if (map == 0) return texture(uTex0, uv); if (map == 1) return texture(uTex1, uv);
    if (map == 2) return texture(uTex2, uv); if (map == 3) return texture(uTex3, uv);
    if (map == 4) return texture(uTex4, uv); if (map == 5) return texture(uTex5, uv);
    if (map == 6) return texture(uTex6, uv); if (map == 7) return texture(uTex7, uv);
    return vec4(1.0);
}

vec3 cin(int s, vec4 r[4], vec4 t, vec4 ras) {
    if (s < 8) { vec4 v = r[s >> 1]; return (s & 1) != 0 ? vec3(v.a) : v.rgb; }
    if (s == 8) return t.rgb; if (s == 9) return vec3(t.a);
    if (s == 10) return ras.rgb; if (s == 11) return vec3(ras.a);
    if (s == 12) return vec3(1.0); if (s == 13) return vec3(0.5);
    return vec3(0.0);
}

float ain(int s, vec4 r[4], vec4 t, vec4 ras) {
    if (s < 4) return r[s].a; if (s == 4) return t.a; if (s == 5) return ras.a;
    return 0.0;
}

bool acomp(int f, float a, float ref) {
    if (f == 0) return false; if (f == 1) return a < ref; if (f == 2) return a == ref;
    if (f == 3) return a <= ref; if (f == 4) return a > ref; if (f == 5) return a != ref;
    if (f == 6) return a >= ref; return true;
}

void main() {
    vec3 vTex[8] = vec3[8](vTex0, vTex1, vTex2, vTex3, vTex4, vTex5, vTex6, vTex7);
    int numStages = uMisc.x;
    if (uMisc.w != 0) { oColor = vec4(1.0, 0.0, 1.0, 1.0); return; }
    vec4 r[4];
    for (int i = 0; i < 4; i++) r[i] = uReg[i];
    for (int s = 0; s < numStages; s++) {
        vec4 t = vec4(1.0);
        if (uOrder[s].y != 255) {
            vec3 tc = vTex[uOrder[s].x & 7];
            t = tex(uOrder[s].y & 7, tc.xy / (tc.z != 0.0 ? tc.z : 1.0));
        }
        vec4 ras = uOrder[s].z == 255 ? vec4(0.0) : (uOrder[s].z & 1) != 0 ? vClr1 : vClr0;
        vec3 a = cin(uCIn[s].x, r, t, ras), b = cin(uCIn[s].y, r, t, ras);
        vec3 c = cin(uCIn[s].z, r, t, ras), d = cin(uCIn[s].w, r, t, ras);
        vec3 cv = mix(a, b, c);
        cv = uCOp[s].x == 1 ? d - cv : d + cv;
        cv += uCOp[s].y == 1 ? vec3(0.5) : uCOp[s].y == 2 ? vec3(-0.5) : vec3(0.0);
        cv *= uCOp[s].z == 1 ? 2.0 : uCOp[s].z == 2 ? 4.0 : uCOp[s].z == 3 ? 0.5 : 1.0;
        if (uCOp[s].w != 0) cv = clamp(cv, 0.0, 1.0);
        float aa = ain(uAIn[s].x, r, t, ras), ab = ain(uAIn[s].y, r, t, ras);
        float ac = ain(uAIn[s].z, r, t, ras), ad = ain(uAIn[s].w, r, t, ras);
        float av = mix(aa, ab, ac);
        av = uAOp[s].x == 1 ? ad - av : ad + av;
        av += uAOp[s].y == 1 ? 0.5 : uAOp[s].y == 2 ? -0.5 : 0.0;
        av *= uAOp[s].z == 1 ? 2.0 : uAOp[s].z == 2 ? 4.0 : uAOp[s].z == 3 ? 0.5 : 1.0;
        if (uAOp[s].w != 0) av = clamp(av, 0.0, 1.0);
        r[uOut[s].x & 3].rgb = cv;
        r[uOut[s].y & 3].a = av;
    }
    vec4 o = clamp(r[numStages > 0 ? uOut[numStages - 1].x & 3 : 0], 0.0, 1.0);
    if (numStages > 0) o.a = clamp(r[uOut[numStages - 1].y & 3].a, 0.0, 1.0);
    bool p0 = acomp(uAlphaComp.x, o.a, float(uAlphaComp.y) / 255.0);
    bool p1 = acomp(uAlphaComp.w, o.a, float(uMisc.y) / 255.0);
    bool pass = uAlphaComp.z == 0 ? (p0 && p1) : uAlphaComp.z == 1 ? (p0 || p1) : uAlphaComp.z == 2 ? (p0 != p1) : (p0 == p1);
    if (!pass) discard;
    if (uMisc.z != 0) {
        float f = clamp((vViewZ - uFogParam.x) / max(uFogParam.y - uFogParam.x, 1e-5), 0.0, 1.0);
        int ft = uMisc.z & 7;
        if (ft == 4) f = 1.0 - exp2(-8.0 * f); else if (ft == 5) f = 1.0 - exp2(-8.0 * f * f);
        else if (ft == 6) f = exp2(-8.0 * (1.0 - f)); else if (ft == 7) f = exp2(-8.0 * (1.0 - f) * (1.0 - f));
        o.rgb = mix(o.rgb, uFogColor.rgb, f);
    }
    oColor = o;
}
