#pragma once

#include <stddef.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifndef GL_VERSION_1_1
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
#endif

#if defined(_WIN32)
typedef ptrdiff_t GLsizeiptr;
typedef char GLchar;
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRYP PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void (APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (APIENTRYP PXGLADBUFFERSUBDATAPROC)(GLenum target, ptrdiff_t offset, GLsizeiptr size, const void* data);
typedef void (APIENTRYP PXGLADBLENDFUNCSEPARATEPROC)(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void (APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void (APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint* renderbuffers);
typedef void (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint* textures);
typedef void (APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void (APIENTRYP PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void (APIENTRYP PFNGLDISABLEPROC)(GLenum cap);
typedef void (APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRYP PFNGLENABLEPROC)(GLenum cap);
typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void (APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (APIENTRYP PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint* renderbuffers);
typedef void (APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void (APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint* data);
typedef void (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (APIENTRYP PFNGLGETTEXIMAGEPROC)(GLenum target, GLint level, GLenum format, GLenum type, void* pixels);
typedef void (APIENTRYP PXGLADLINEWIDTHPROC)(GLfloat width);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PXGLADPOLYGONMODEPROC)(GLenum face, GLenum mode);
typedef void (APIENTRYP PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (APIENTRYP PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
typedef void (APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void (APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);

extern PFNGLACTIVETEXTUREPROC glad_glActiveTexture;
extern PFNGLATTACHSHADERPROC glad_glAttachShader;
extern PFNGLBINDBUFFERPROC glad_glBindBuffer;
extern PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
extern PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer;
extern PFNGLBINDTEXTUREPROC glad_glBindTexture;
extern PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
extern PFNGLBUFFERDATAPROC glad_glBufferData;
extern PXGLADBUFFERSUBDATAPROC glad_glBufferSubData;
extern PXGLADBLENDFUNCSEPARATEPROC glad_glBlendFuncSeparate;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus;
extern PFNGLCLEARPROC glad_glClear;
extern PFNGLCLEARCOLORPROC glad_glClearColor;
extern PFNGLCOMPILESHADERPROC glad_glCompileShader;
extern PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
extern PFNGLCREATESHADERPROC glad_glCreateShader;
extern PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers;
extern PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
extern PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers;
extern PFNGLDELETESHADERPROC glad_glDeleteShader;
extern PFNGLDELETETEXTURESPROC glad_glDeleteTextures;
extern PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;
extern PFNGLDEPTHFUNCPROC glad_glDepthFunc;
extern PFNGLDISABLEPROC glad_glDisable;
extern PFNGLDRAWARRAYSPROC glad_glDrawArrays;
extern PFNGLENABLEPROC glad_glEnable;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;
extern PFNGLGENBUFFERSPROC glad_glGenBuffers;
extern PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
extern PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers;
extern PFNGLGENTEXTURESPROC glad_glGenTextures;
extern PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
extern PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
extern PFNGLGETINTEGERVPROC glad_glGetIntegerv;
extern PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
extern PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC glad_glGetShaderiv;
extern PFNGLGETTEXIMAGEPROC glad_glGetTexImage;
extern PXGLADLINEWIDTHPROC glad_glLineWidth;
extern PFNGLLINKPROGRAMPROC glad_glLinkProgram;
extern PXGLADPOLYGONMODEPROC glad_glPolygonMode;
extern PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage;
extern PFNGLSHADERSOURCEPROC glad_glShaderSource;
extern PFNGLTEXIMAGE2DPROC glad_glTexImage2D;
extern PFNGLTEXPARAMETERIPROC glad_glTexParameteri;
extern PFNGLTEXSUBIMAGE2DPROC glad_glTexSubImage2D;
extern PFNGLUNIFORM1IPROC glad_glUniform1i;
extern PFNGLUNIFORM4FPROC glad_glUniform4f;
extern PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv;
extern PFNGLUSEPROGRAMPROC glad_glUseProgram;
extern PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
extern PFNGLVIEWPORTPROC glad_glViewport;

int gladLoaderLoadGL(void);
typedef void* (*GLADloadfunc)(const char* name);
int gladLoadGL(GLADloadfunc load);
void gladLoaderUnloadGL(void);

#define glActiveTexture glad_glActiveTexture
#define glAttachShader glad_glAttachShader
#define glBindBuffer glad_glBindBuffer
#define glBindFramebuffer glad_glBindFramebuffer
#define glBindRenderbuffer glad_glBindRenderbuffer
#define glBindTexture glad_glBindTexture
#define glBindVertexArray glad_glBindVertexArray
#define glBufferData glad_glBufferData
#define glBufferSubData glad_glBufferSubData
#define glBlendFuncSeparate glad_glBlendFuncSeparate
#define glCheckFramebufferStatus glad_glCheckFramebufferStatus
#define glClear glad_glClear
#define glClearColor glad_glClearColor
#define glCompileShader glad_glCompileShader
#define glCreateProgram glad_glCreateProgram
#define glCreateShader glad_glCreateShader
#define glDeleteBuffers glad_glDeleteBuffers
#define glDeleteFramebuffers glad_glDeleteFramebuffers
#define glDeleteProgram glad_glDeleteProgram
#define glDeleteRenderbuffers glad_glDeleteRenderbuffers
#define glDeleteShader glad_glDeleteShader
#define glDeleteTextures glad_glDeleteTextures
#define glDeleteVertexArrays glad_glDeleteVertexArrays
#define glDepthFunc glad_glDepthFunc
#define glDisable glad_glDisable
#define glDrawArrays glad_glDrawArrays
#define glEnable glad_glEnable
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glFramebufferRenderbuffer glad_glFramebufferRenderbuffer
#define glFramebufferTexture2D glad_glFramebufferTexture2D
#define glGenBuffers glad_glGenBuffers
#define glGenFramebuffers glad_glGenFramebuffers
#define glGenRenderbuffers glad_glGenRenderbuffers
#define glGenTextures glad_glGenTextures
#define glGenVertexArrays glad_glGenVertexArrays
#define glGetUniformLocation glad_glGetUniformLocation
#define glGetIntegerv glad_glGetIntegerv
#define glGetProgramInfoLog glad_glGetProgramInfoLog
#define glGetProgramiv glad_glGetProgramiv
#define glGetShaderInfoLog glad_glGetShaderInfoLog
#define glGetShaderiv glad_glGetShaderiv
#define glGetTexImage glad_glGetTexImage
#define glLineWidth glad_glLineWidth
#define glLinkProgram glad_glLinkProgram
#define glPolygonMode glad_glPolygonMode
#define glRenderbufferStorage glad_glRenderbufferStorage
#define glShaderSource glad_glShaderSource
#define glTexImage2D glad_glTexImage2D
#define glTexParameteri glad_glTexParameteri
#define glTexSubImage2D glad_glTexSubImage2D
#define glUniform1i glad_glUniform1i
#define glUniform4f glad_glUniform4f
#define glUniformMatrix4fv glad_glUniformMatrix4fv
#define glUseProgram glad_glUseProgram
#define glVertexAttribPointer glad_glVertexAttribPointer
#define glViewport glad_glViewport

#ifdef __cplusplus
}
#endif
