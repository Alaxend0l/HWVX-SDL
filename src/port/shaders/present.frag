/* SDL_GPU only. The EFB alpha is game scratch (shadow volumes), not opacity:
 * write 1 so compositors that honour window alpha (macOS) show the frame.
 * uGamma.x is the Graphics menu's Gamma row, the power each colour is raised to
 * (Port_GfxGammaPower). uFilter: with the EFB larger than the window (the
 * resolution scale above 1x, gx_draw.c), x is 1 and yz the EFB texels per
 * window pixel; each pixel is then the area-weighted average of the texels it
 * covers (a box filter), so the extra pixels anti-alias. x 0: one sample
 * through the sampler (linear, or nearest). gx_gl.c has the OpenGL version of
 * this. */
#include "common.glsl"

LOC(0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

SAMPLER(0) uEFB;

UBO(3) PresentGamma {
    vec4 uGamma;
    vec4 uFilter;
};

vec3 Box() {
    ivec2 size = textureSize(uEFB, 0);
    vec2 c = vUV * vec2(size), lo = c - 0.5 * uFilter.yz, hi = c + 0.5 * uFilter.yz;
    ivec2 i0 = ivec2(floor(lo)), i1 = ivec2(ceil(hi)) - 1;
    vec3 sum = vec3(0.0);
    float total = 0.0;

    for (int y = i0.y; y <= i1.y && y < i0.y + 8; y++) {
        float wy = min(hi.y, float(y + 1)) - max(lo.y, float(y));

        for (int x = i0.x; x <= i1.x && x < i0.x + 8; x++) {
            float w = wy * (min(hi.x, float(x + 1)) - max(lo.x, float(x)));

            sum += w * texelFetch(uEFB, clamp(ivec2(x, y), ivec2(0), size - 1), 0).rgb;
            total += w;
        }
    }
    return sum / max(total, 1e-6);
}

void main() {
    vec3 c = uFilter.x > 0.5 ? Box() : texture(uEFB, vUV).rgb;

    oColor = vec4(pow(c, uGamma.xxx), 1.0);
}
