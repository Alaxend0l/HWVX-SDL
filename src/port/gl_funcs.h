/* OpenGL 3.3 core entry points, loaded at runtime through SDL_GL_GetProcAddress
 * so no GL loader library is needed. Add functions to GL_FUNCS as required. */
#ifndef GL_FUNCS_H
#define GL_FUNCS_H

#include <SDL3/SDL_opengl.h>

/* glext.h has no typedefs for OpenGL 1.0/1.1 entry points. */
typedef void (APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void (APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void (APIENTRYP PFNGLCLEARDEPTHPROC)(GLdouble depth);
typedef void (APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (APIENTRYP PFNGLSCISSORPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (APIENTRYP PFNGLENABLEPROC)(GLenum cap);
typedef void (APIENTRYP PFNGLDISABLEPROC)(GLenum cap);
typedef void (APIENTRYP PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void (APIENTRYP PFNGLLOGICOPPROC)(GLenum opcode);
typedef void (APIENTRYP PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void (APIENTRYP PFNGLDEPTHMASKPROC)(GLboolean flag);
typedef void (APIENTRYP PFNGLCOLORMASKPROC)(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
typedef void (APIENTRYP PFNGLCULLFACEPROC)(GLenum mode);
typedef void (APIENTRYP PFNGLFRONTFACEPROC)(GLenum mode);
typedef void (APIENTRYP PFNGLDEPTHRANGEPROC)(GLdouble n, GLdouble f);
typedef void (APIENTRYP PFNGLPOINTSIZEPROC)(GLfloat size);
typedef void (APIENTRYP PFNGLLINEWIDTHPROC)(GLfloat width);
typedef GLenum (APIENTRYP PFNGLGETERRORPROC)(void);
typedef const GLubyte *(APIENTRYP PFNGLGETSTRINGPROC)(GLenum name);
typedef void (APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint *data);
typedef void (APIENTRYP PFNGLPIXELSTOREIPROC)(GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                                             GLenum type, void *pixels);
typedef void (APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef void (APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint *textures);
typedef void (APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (APIENTRYP PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat,
                                             GLsizei w, GLsizei h, GLint border, GLenum format,
                                             GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint x, GLint y,
                                                GLsizei w, GLsizei h, GLenum format, GLenum type,
                                                const void *pixels);
typedef void (APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLTEXPARAMETERFPROC)(GLenum target, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNGLCOPYTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoff,
                                                    GLint yoff, GLint x, GLint y, GLsizei w,
                                                    GLsizei h);
typedef void (APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRYP PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type,
                                               const void *indices);

#define GL_FUNCS(X)                                                             \
    X(PFNGLCLEARPROC, glClear)                                                  \
    X(PFNGLCLEARCOLORPROC, glClearColor)                                        \
    X(PFNGLCLEARDEPTHPROC, glClearDepth)                                        \
    X(PFNGLVIEWPORTPROC, glViewport)                                            \
    X(PFNGLSCISSORPROC, glScissor)                                              \
    X(PFNGLENABLEPROC, glEnable)                                                \
    X(PFNGLDISABLEPROC, glDisable)                                              \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                                          \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                  \
    X(PFNGLLOGICOPPROC, glLogicOp)                                              \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                          \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                          \
    X(PFNGLCOLORMASKPROC, glColorMask)                                          \
    X(PFNGLCULLFACEPROC, glCullFace)                                            \
    X(PFNGLFRONTFACEPROC, glFrontFace)                                          \
    X(PFNGLDEPTHRANGEPROC, glDepthRange)                                        \
    X(PFNGLPOINTSIZEPROC, glPointSize)                                          \
    X(PFNGLLINEWIDTHPROC, glLineWidth)                                          \
    X(PFNGLGETERRORPROC, glGetError)                                            \
    X(PFNGLGETSTRINGPROC, glGetString)                                          \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                      \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                      \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                        \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                      \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                      \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                  \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                        \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                                  \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                  \
    X(PFNGLTEXPARAMETERFPROC, glTexParameterf)                                  \
    X(PFNGLCOPYTEXSUBIMAGE2DPROC, glCopyTexSubImage2D)                          \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                        \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                  \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                        \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                        \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                  \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                              \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                              \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)              \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)            \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                      \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer)                    \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                        \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                                    \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                    \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                    \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                  \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                      \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                            \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                    \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                  \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                    \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                        \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                      \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                    \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                          \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                        \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                  \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                        \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)                    \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)                      \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase)                                \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                          \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv)                                        \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                        \
    X(PFNGLUNIFORM4IVPROC, glUniform4iv)                                        \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                            \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                              \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                              \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                    \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                            \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                            \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                      \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)              \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)

#define GL_DECLARE(type, name) extern type p_##name;
GL_FUNCS(GL_DECLARE)
#undef GL_DECLARE

/* Call through the loaded pointers under the usual names. */
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glClearDepth p_glClearDepth
#define glViewport p_glViewport
#define glScissor p_glScissor
#define glEnable p_glEnable
#define glDisable p_glDisable
#define glBlendFunc p_glBlendFunc
#define glBlendEquation p_glBlendEquation
#define glLogicOp p_glLogicOp
#define glDepthFunc p_glDepthFunc
#define glDepthMask p_glDepthMask
#define glColorMask p_glColorMask
#define glCullFace p_glCullFace
#define glFrontFace p_glFrontFace
#define glDepthRange p_glDepthRange
#define glPointSize p_glPointSize
#define glLineWidth p_glLineWidth
#define glGetError p_glGetError
#define glGetString p_glGetString
#define glGetIntegerv p_glGetIntegerv
#define glPixelStorei p_glPixelStorei
#define glReadPixels p_glReadPixels
#define glGenTextures p_glGenTextures
#define glDeleteTextures p_glDeleteTextures
#define glBindTexture p_glBindTexture
#define glActiveTexture p_glActiveTexture
#define glTexImage2D p_glTexImage2D
#define glTexSubImage2D p_glTexSubImage2D
#define glTexParameteri p_glTexParameteri
#define glTexParameterf p_glTexParameterf
#define glCopyTexSubImage2D p_glCopyTexSubImage2D
#define glGenBuffers p_glGenBuffers
#define glDeleteBuffers p_glDeleteBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glBufferSubData p_glBufferSubData
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glVertexAttribIPointer p_glVertexAttribIPointer
#define glDrawArrays p_glDrawArrays
#define glDrawElements p_glDrawElements
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glDeleteShader p_glDeleteShader
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glUseProgram p_glUseProgram
#define glDeleteProgram p_glDeleteProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glGetUniformBlockIndex p_glGetUniformBlockIndex
#define glUniformBlockBinding p_glUniformBlockBinding
#define glBindBufferBase p_glBindBufferBase
#define glUniform1i p_glUniform1i
#define glUniform1iv p_glUniform1iv
#define glUniform4fv p_glUniform4fv
#define glUniform4iv p_glUniform4iv
#define glUniformMatrix4fv p_glUniformMatrix4fv
#define glGenFramebuffers p_glGenFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glGenRenderbuffers p_glGenRenderbuffers
#define glBindRenderbuffer p_glBindRenderbuffer
#define glRenderbufferStorage p_glRenderbufferStorage
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glBlitFramebuffer p_glBlitFramebuffer

void GL_Load(void);

#endif
