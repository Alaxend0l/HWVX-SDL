#include "port_internal.h"

#define GL_DEFINE(type, name) type p_##name;
GL_FUNCS(GL_DEFINE)
#undef GL_DEFINE

void GL_Load(void) {
#define GLF_LOAD(type, name)                                                     \
    p_##name = (type)SDL_GL_GetProcAddress(#name);                              \
    if (p_##name == NULL) {                                                     \
        Port_Fatal("OpenGL function %s missing (OpenGL 3.3 required)", #name);  \
    }
    GL_FUNCS(GLF_LOAD)
#undef GLF_LOAD
    Port_Log("OpenGL %s, %s", (const char *)glGetString(GL_VERSION),
             (const char *)glGetString(GL_RENDERER));
}
