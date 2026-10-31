/* SDL_GPU only: draws the EFB copy source over the letterboxed window area
 * as a 4-vertex triangle strip. uRect is the source rectangle in texture
 * coordinates (x0, y0, x1, y1), top-left origin. */
#include "common.glsl"

UBO(1) PresentBlock {
    vec4 uRect;
};

LOC(0) out vec2 vUV;

void main() {
    vec2 c = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    vUV = mix(uRect.xy, uRect.zw, c);
    gl_Position = vec4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
}
