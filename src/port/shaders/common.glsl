/* Shared by every shader. Compiled two ways (tools/gen_shaders.py):
 * - OpenGL 3.3: "#version 330 core" is prepended at run time (gx_gl.c);
 *   uniform blocks get their bindings from glUniformBlockBinding and the
 *   samplers from glUniform1i.
 * - SDL_GPU: glslang -V predefines VULKAN; the SPIR-V is translated to MSL
 *   (Metal) and HLSL -> DXIL (D3D12). Bindings follow SDL_CreateGPUShader:
 *   vertex uniform buffers in set 1, fragment samplers in set 2, fragment
 *   uniform buffers in set 3. */
#ifdef VULKAN
#define LOC(n) layout(location = n)
#define UBO(s) layout(std140, set = s, binding = 0) uniform
#define SAMPLER(b) layout(set = 2, binding = b) uniform sampler2D
#else
#define LOC(n)
#define UBO(s) layout(std140) uniform
#define SAMPLER(b) uniform sampler2D
#endif
