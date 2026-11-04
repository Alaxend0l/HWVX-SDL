/* Specialised pixel shaders: one per distinct TEV / alpha compare / fog setup.
 *
 * shaders/gx.frag is the general ("uber") shader: per pixel it loops over the
 * active TEV stages, indexes the registers dynamically and branches on the
 * uniform block for every input, op and texture. A specialised shader has all
 * of that baked in as straight-line code, so it is much cheaper at high
 * resolutions. It keeps gx.frag's interface (inputs, samplers and FSBlock)
 * and its arithmetic, so the same uniforms drive both and both give the same
 * pixels; the TEV colour registers, alpha refs and fog range stay uniforms,
 * so they don't split the setups.
 *
 * GXTevKey is the part of GXFSUniforms a specialised shader bakes in,
 * normalised so that setups gx.frag treats the same share a key. */
#ifndef GX_TEVGEN_H
#define GX_TEVGEN_H

#include "gx_render.h"

typedef struct GXTevKey {
    u32 misc;          /* numStages, alpha compare funcs/op, fog type; see GX_TevKey */
    u32 pad;
    u64 stage[16];     /* unused stages are 0 */
} GXTevKey;

/* The key for a draw's FSBlock; 0 when the draw needs the general shader
 * (the HWVX_GXDEBUG magenta). */
int GX_TevKey(const GXFSUniforms *fs, GXTevKey *key);

u32 GX_TevKeyHash(const GXTevKey *key);

/* A line of an HWVX_TEVKEYS file (gx_draw.c): a whole FSBlock in hex.
 * ToHex writes 2 * sizeof(GXFSUniforms) digits and a NUL; FromHex returns 0
 * if the line is malformed. */
void GX_TevKeyToHex(const GXFSUniforms *fs, char *out);
int GX_TevKeyFromHex(const char *line, GXFSUniforms *fs);

/* The specialised fragment shader for key: decl (the declarations shared
 * with gx.frag) followed by a generated main(). The GLSL dialect is
 * gx.frag's, without a #version line. Returns a malloc'd string. */
char *GX_TevGenGLSL(const GXTevKey *key, const char *decl);

#endif
