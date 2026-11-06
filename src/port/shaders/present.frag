/* SDL_GPU only. The EFB alpha is game scratch (shadow volumes), not opacity:
 * write 1 so compositors that honour window alpha (macOS) show the frame.
 * uGamma.x is the Graphics menu's Gamma row, the power each colour is raised to
 * (Port_GfxGammaPower). gx_gl.c has the OpenGL version of this. */
#include "common.glsl"

LOC(0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

SAMPLER(0) uEFB;

UBO(3) PresentGamma {
    vec4 uGamma;
};

void main() {
    oColor = vec4(pow(texture(uEFB, vUV).rgb, uGamma.xxx), 1.0);
}
