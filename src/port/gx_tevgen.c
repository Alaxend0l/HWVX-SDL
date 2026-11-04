/* Specialised pixel shaders (see gx_tevgen.h). Every rule here mirrors one in
 * shaders/gx.frag; change the two together. */
#include "gx_tevgen.h"

/* ---- key ----------------------------------------------------------------- */

/* Per stage, normalised the way gx.frag reads the values:
 *  bits 0-2   texcoord (0 without a texture)
 *  bits 3-6   texmap 0-7, 8: none
 *  bits 7-8   raster channel 0/1, 2: none
 *  bits 9-24  color inputs a-d, 4 bits each (14 and up: zero, as 15)
 *  bits 25-36 alpha inputs a-d, 3 bits each (6 and up: zero, as 7)
 *  bits 37-42 color op (1 bit: subtract), bias (2), scale (2), clamp (1)
 *  bits 43-48 alpha op, bias, scale, clamp
 *  bits 49-50 color out register, 51-52 alpha out register */
static u64 StageKey(const GXFSUniforms *fs, int i) {
    u64 k = 0, v;
    int j, hasTex = fs->order[i][1] != 255;

    k |= (u64)(hasTex ? fs->order[i][0] & 7 : 0);
    k |= (u64)(hasTex ? fs->order[i][1] & 7 : 8) << 3;
    k |= (u64)(fs->order[i][2] == 255 ? 2 : fs->order[i][2] & 1) << 7;
    for (j = 0; j < 4; j++) {
        v = (u64)fs->cin[i][j];
        k |= (v < 14 ? v : 15) << (9 + 4 * j);
        v = (u64)fs->ain[i][j];
        k |= (v < 6 ? v : 7) << (25 + 3 * j);
    }
#define OPKEY(op) \
    ((u64)((op)[0] == 1) | (u64)((op)[1] == 1 || (op)[1] == 2 ? (op)[1] : 0) << 1 | \
     (u64)((op)[2] >= 1 && (op)[2] <= 3 ? (op)[2] : 0) << 3 | (u64)((op)[3] != 0) << 5)
    k |= OPKEY(fs->cop[i]) << 37;
    k |= OPKEY(fs->aop[i]) << 43;
#undef OPKEY
    k |= (u64)(fs->out[i][0] & 3) << 49;
    k |= (u64)(fs->out[i][1] & 3) << 51;
    return k;
}

/* misc: bits 0-4 numStages, 5-7 alpha compare 0, 8-10 alpha compare 1,
 * 11-12 the combining op, 13-15 fog (0 none, 1 linear, 4-7 the exp ones). */
int GX_TevKey(const GXFSUniforms *fs, GXTevKey *key) {
    int n = fs->misc[0], i, fog;

    if (fs->misc[3] != 0 || n < 0 || n > 16) {
        return 0;
    }
    SDL_zerop(key);
    fog = fs->misc[2] == 0 ? 0 : (fs->misc[2] & 7) >= 4 ? (fs->misc[2] & 7) : 1;
    /* acomp(): anything outside 0-6 passes, like 7 (GX_ALWAYS) */
#define COMP(f) ((u32)((f) >= 0 && (f) < 7 ? (f) : 7))
    key->misc = (u32)n | COMP(fs->alphaComp[0]) << 5 | COMP(fs->alphaComp[3]) << 8 |
                (u32)(fs->alphaComp[2] >= 0 && fs->alphaComp[2] <= 2 ? fs->alphaComp[2] : 3) << 11 |
                (u32)fog << 13;
#undef COMP
    for (i = 0; i < n; i++) {
        key->stage[i] = StageKey(fs, i);
    }
    return 1;
}

u32 GX_TevKeyHash(const GXTevKey *key) {
    u64 h = 0xcbf29ce484222325ull ^ key->misc;
    int i, n = key->misc & 31;

    for (i = 0; i < n; i++) {
        h = (h ^ key->stage[i]) * 0x100000001b3ull;
        h ^= h >> 29;
    }
    return (u32)(h ^ (h >> 32));
}

/* ---- HWVX_TEVKEYS lines ---------------------------------------------- */

void GX_TevKeyToHex(const GXFSUniforms *fs, char *out) {
    static const char hex[] = "0123456789abcdef";
    const u8 *b = (const u8 *)fs;
    size_t i;

    for (i = 0; i < sizeof(*fs); i++) {
        out[2 * i] = hex[b[i] >> 4];
        out[2 * i + 1] = hex[b[i] & 15];
    }
    out[2 * sizeof(*fs)] = 0;
}

int GX_TevKeyFromHex(const char *line, GXFSUniforms *fs) {
    u8 *b = (u8 *)fs;
    size_t i;

    for (i = 0; i < sizeof(*fs); i++) {
        int hi = line[2 * i], lo = hi ? line[2 * i + 1] : 0;
#define NIB(c) ((c) >= '0' && (c) <= '9' ? (c) - '0' : (c) >= 'a' && (c) <= 'f' ? (c) - 'a' + 10 : -1)
        if (NIB(hi) < 0 || NIB(lo) < 0) {
            return 0;
        }
        b[i] = (u8)(NIB(hi) << 4 | NIB(lo));
#undef NIB
    }
    return 1;
}

/* ---- GLSL ---------------------------------------------------------------- */

typedef struct Buf {
    char *p;
    size_t len, cap;
} Buf;

static void Put(Buf *b, const char *fmt, ...) {
    va_list ap;
    int n;

    for (;;) {
        va_start(ap, fmt);
        n = SDL_vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0) {
            Port_Fatal("tevgen: format failed");
        }
        if (b->len + (size_t)n < b->cap) {
            b->len += (size_t)n;
            return;
        }
        b->cap = (b->cap + (size_t)n) * 2;
        b->p = SDL_realloc(b->p, b->cap);
        if (b->p == NULL) {
            Port_Fatal("tevgen: out of memory");
        }
    }
}

/* gx.frag's cin()/ain() */
static const char *ColorIn(int s, char *tmp) {
    static const char *const fixed[] = {
        "t.rgb", "vec3(t.a)", "ras.rgb", "vec3(ras.a)", "vec3(1.0)", "vec3(0.5)",
    };
    if (s < 8) {
        SDL_snprintf(tmp, 16, (s & 1) ? "vec3(r%d.a)" : "r%d.rgb", s >> 1);
        return tmp;
    }
    return s < 14 ? fixed[s - 8] : "vec3(0.0)";
}

static const char *AlphaIn(int s, char *tmp) {
    if (s < 4) {
        SDL_snprintf(tmp, 16, "r%d.a", s);
        return tmp;
    }
    return s == 4 ? "t.a" : s == 5 ? "ras.a" : "0.0";
}

static const char *const s_bias[4] = { "", " + 0.5", " - 0.5", "" };
static const char *const s_scale[4] = { "", " * 2.0", " * 4.0", " * 0.5" };

/* One stage's combiner: v = (d +/- mix(a, b, c) + bias) * scale, clamped.
 * Written in gx.frag's order of operations so the results match exactly. */
static void Combine(Buf *b, const char *v, const char *in[4], u32 op) {
    Put(b, "    %s = mix(%s, %s, %s);\n", v, in[0], in[1], in[2]);
    Put(b, "    %s = %s %c %s;\n", v, in[3], (op & 1) ? '-' : '+', v);
    if ((op >> 1) & 3) {
        Put(b, "    %s = %s%s;\n", v, v, s_bias[(op >> 1) & 3]);
    }
    if ((op >> 3) & 3) {
        Put(b, "    %s = %s%s;\n", v, v, s_scale[(op >> 3) & 3]);
    }
    if ((op >> 5) & 1) {
        Put(b, "    %s = clamp(%s, 0.0, 1.0);\n", v, v);
    }
}

/* gx.frag's acomp(): "true"/"false" when it doesn't depend on alpha */
static const char *AlphaTest(int f, int which) {
    static const char *const t[2][8] = {
        { "false", "o.a < ref0", "o.a == ref0", "o.a <= ref0", "o.a > ref0", "o.a != ref0", "o.a >= ref0", "true" },
        { "false", "o.a < ref1", "o.a == ref1", "o.a <= ref1", "o.a > ref1", "o.a != ref1", "o.a >= ref1", "true" },
    };
    return t[which][f & 7];
}

static void AlphaCompare(Buf *b, u32 misc) {
    int f0 = (misc >> 5) & 7, f1 = (misc >> 8) & 7, op = (misc >> 11) & 3;
    int c0 = f0 == 0 || f0 == 7, c1 = f1 == 0 || f1 == 7;
    static const char *const ops[4] = { "&&", "||", "!=", "==" };

    if (c0 && c1) {   /* constant: fold it here so no discard is left behind */
        int p0 = f0 == 7, p1 = f1 == 7;
        int pass = op == 0 ? (p0 && p1) : op == 1 ? (p0 || p1) : op == 2 ? (p0 != p1) : (p0 == p1);
        if (!pass) {
            Put(b, "    discard;\n");
        }
        return;
    }
    if (!c0) {
        Put(b, "    float ref0 = float(uAlphaComp.y) / 255.0;\n");
    }
    if (!c1) {
        Put(b, "    float ref1 = float(uMisc.y) / 255.0;\n");
    }
    Put(b, "    if (!((%s) %s (%s))) discard;\n", AlphaTest(f0, 0), ops[op], AlphaTest(f1, 1));
}

static void Fog(Buf *b, int ft) {
    if (ft == 0) {
        return;
    }
    Put(b, "    float f = clamp((vViewZ - uFogParam.x) / max(uFogParam.y - uFogParam.x, 1e-5), 0.0, 1.0);\n");
    switch (ft) {
    case 4: Put(b, "    f = 1.0 - exp2(-8.0 * f);\n"); break;
    case 5: Put(b, "    f = 1.0 - exp2(-8.0 * f * f);\n"); break;
    case 6: Put(b, "    f = exp2(-8.0 * (1.0 - f));\n"); break;
    case 7: Put(b, "    f = exp2(-8.0 * (1.0 - f) * (1.0 - f));\n"); break;
    default: break;
    }
    Put(b, "    o.rgb = mix(o.rgb, uFogColor.rgb, f);\n");
}

char *GX_TevGenGLSL(const GXTevKey *key, const char *decl) {
    Buf b = { NULL, 0, 0 };
    int n = key->misc & 31, i, j;
    int lastC = 0, lastA = 0;

    b.cap = SDL_strlen(decl) + 4096;
    b.p = SDL_malloc(b.cap);
    if (b.p == NULL) {
        Port_Fatal("tevgen: out of memory");
    }
    Put(&b, "%s\n/* generated by gx_tevgen.c */\nvoid main() {\n", decl);
    Put(&b, "    vec4 r0 = uReg[0], r1 = uReg[1], r2 = uReg[2], r3 = uReg[3];\n");
    Put(&b, "    vec4 t = vec4(1.0), ras = vec4(0.0);\n    vec3 cv;\n    float av;\n");
    for (i = 0; i < n; i++) {
        u64 k = key->stage[i];
        int tc = (int)(k & 7), map = (int)((k >> 3) & 15), chan = (int)((k >> 7) & 3);
        int cin[4], ain[4], useT = 0, useRas = 0;
        char ctmp[4][16], atmp[4][16];
        const char *cs[4], *as[4];

        for (j = 0; j < 4; j++) {
            cin[j] = (int)((k >> (9 + 4 * j)) & 15);
            ain[j] = (int)((k >> (25 + 3 * j)) & 7);
            useT |= cin[j] == 8 || cin[j] == 9 || ain[j] == 4;
            useRas |= cin[j] == 10 || cin[j] == 11 || ain[j] == 5;
            cs[j] = ColorIn(cin[j], ctmp[j]);
            as[j] = AlphaIn(ain[j], atmp[j]);
        }
        Put(&b, "    /* stage %d */\n", i);
        /* t and ras are only set when this stage reads them */
        if (useT) {
            if (map == 8) {
                Put(&b, "    t = vec4(1.0);\n");
            } else {
                Put(&b, "    t = texture(uTex%d, vTex%d.xy / (vTex%d.z != 0.0 ? vTex%d.z : 1.0));\n",
                    map, tc, tc, tc);
            }
        }
        if (useRas) {
            Put(&b, "    ras = %s;\n", chan == 2 ? "vec4(0.0)" : chan ? "vClr1" : "vClr0");
        }
        Combine(&b, "cv", cs, (u32)(k >> 37) & 63);
        Combine(&b, "av", as, (u32)(k >> 43) & 63);
        lastC = (int)((k >> 49) & 3);
        lastA = (int)((k >> 51) & 3);
        Put(&b, "    r%d.rgb = cv;\n    r%d.a = av;\n", lastC, lastA);
    }
    if (n > 0) {
        Put(&b, "    vec4 o = vec4(clamp(r%d.rgb, 0.0, 1.0), clamp(r%d.a, 0.0, 1.0));\n", lastC, lastA);
    } else {
        Put(&b, "    vec4 o = clamp(r0, 0.0, 1.0);\n");
    }
    AlphaCompare(&b, key->misc);
    Fog(&b, (int)((key->misc >> 13) & 7));
    Put(&b, "    oColor = o;\n}\n");
    return b.p;
}
