/* tevgen_glsl: the specialised pixel shaders for SDL_GPU's precompiled set.
 * xdlrs shaders gen builds and runs it:
 *
 *   tevgen_glsl KEYS_FILE DECL_FILE OUT_DIR
 *
 * KEYS_FILE holds HWVX_TEVKEYS lines (tools/tevkeys.txt); DECL_FILE gx.frag's
 * declarations. For each distinct setup it writes OUT_DIR/tevN.frag (the
 * GLSL from src/port/gx_tevgen.c, the same the OpenGL backend compiles) and
 * a line "misc stage0 ... stage15" (hex) to OUT_DIR/keys. */
#include "gx_tevgen.h"

#include <stdio.h>

void Port_Fatal(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void Port_Log(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

int main(int argc, char **argv) {
    char *keys, *decl, *line, *save = NULL, path[1024];
    GXTevKey *seen = NULL;
    int nSeen = 0, lineNo = 0, i;
    FILE *keysOut;

    if (argc != 4) {
        fprintf(stderr, "usage: tevgen_glsl KEYS_FILE DECL_FILE OUT_DIR\n");
        return 2;
    }
    keys = SDL_LoadFile(argv[1], NULL);
    decl = SDL_LoadFile(argv[2], NULL);
    if (keys == NULL || decl == NULL) {
        Port_Fatal("tevgen_glsl: %s", SDL_GetError());
    }
    SDL_snprintf(path, sizeof(path), "%s/keys", argv[3]);
    keysOut = fopen(path, "w");
    if (keysOut == NULL) {
        Port_Fatal("tevgen_glsl: can't write %s", path);
    }
    for (line = SDL_strtok_r(keys, "\n", &save); line != NULL; line = SDL_strtok_r(NULL, "\n", &save)) {
        GXFSUniforms fs;
        GXTevKey key;
        char *src;
        FILE *f;

        lineNo++;
        if (*line == '#' || *line == 0) {
            continue;
        }
        if (!GX_TevKeyFromHex(line, &fs)) {
            Port_Fatal("%s:%d: not an HWVX_TEVKEYS line", argv[1], lineNo);
        }
        if (!GX_TevKey(&fs, &key)) {
            continue;
        }
        for (i = 0; i < nSeen; i++) {
            if (memcmp(&seen[i], &key, sizeof(key)) == 0) {
                break;
            }
        }
        if (i < nSeen) {
            continue;
        }
        seen = SDL_realloc(seen, (size_t)(nSeen + 1) * sizeof(*seen));
        seen[nSeen] = key;
        src = GX_TevGenGLSL(&key, decl);
        SDL_snprintf(path, sizeof(path), "%s/tev%d.frag", argv[3], nSeen);
        f = fopen(path, "w");
        if (f == NULL || fputs(src, f) < 0 || fclose(f) != 0) {
            Port_Fatal("tevgen_glsl: can't write %s", path);
        }
        SDL_free(src);
        fprintf(keysOut, "%08x", key.misc);
        for (i = 0; i < 16; i++) {
            fprintf(keysOut, " %016llx", (unsigned long long)key.stage[i]);
        }
        fputc('\n', keysOut);
        nSeen++;
    }
    fclose(keysOut);
    printf("%d\n", nSeen);
    return 0;
}
