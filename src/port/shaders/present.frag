/* SDL_GPU only. The EFB alpha is game scratch (shadow volumes), not opacity:
 * write 1 so compositors that honour window alpha (macOS) show the frame. */
#include "common.glsl"

LOC(0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

SAMPLER(0) uEFB;

void main() {
    oColor = vec4(texture(uEFB, vUV).rgb, 1.0);
}
