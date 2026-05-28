#include <glad/gl.h>

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
static HMODULE px_gl_module = NULL;
static void* px_get_proc(const char* name) {
    void* proc = (void*)wglGetProcAddress(name);
    uintptr_t value = (uintptr_t)proc;
    if (proc == NULL || value == 1 || value == 2 || value == 3 || value == (uintptr_t)-1) {
        if (px_gl_module == NULL) {
            px_gl_module = LoadLibraryA("opengl32.dll");
        }
        if (px_gl_module != NULL) {
            proc = (void*)GetProcAddress(px_gl_module, name);
        }
    }
    return proc;
}
#elif defined(__APPLE__)
#include <dlfcn.h>
static void* px_gl_module = NULL;
static void* px_get_proc(const char* name) {
    if (px_gl_module == NULL) {
        px_gl_module = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    }
    return px_gl_module == NULL ? NULL : dlsym(px_gl_module, name);
}
#else
#include <dlfcn.h>
typedef void* (*GLXGETPROCADDRESSPROC)(const unsigned char*);
static void* px_gl_module = NULL;
static GLXGETPROCADDRESSPROC px_glx_get_proc = NULL;
static void* px_get_proc(const char* name) {
    if (px_gl_module == NULL) {
        px_gl_module = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (px_gl_module == NULL) {
            px_gl_module = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (px_gl_module != NULL) {
            px_glx_get_proc = (GLXGETPROCADDRESSPROC)dlsym(px_gl_module, "glXGetProcAddressARB");
        }
    }
    void* proc = px_glx_get_proc == NULL ? NULL : px_glx_get_proc((const unsigned char*)name);
    if (proc == NULL && px_gl_module != NULL) {
        proc = dlsym(px_gl_module, name);
    }
    return proc;
}
#endif

PFNGLACTIVETEXTUREPROC glad_glActiveTexture = NULL;
PFNGLATTACHSHADERPROC glad_glAttachShader = NULL;
PFNGLBINDBUFFERPROC glad_glBindBuffer = NULL;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer = NULL;
PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer = NULL;
PFNGLBINDTEXTUREPROC glad_glBindTexture = NULL;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray = NULL;
PFNGLBUFFERDATAPROC glad_glBufferData = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus = NULL;
PFNGLCLEARPROC glad_glClear = NULL;
PFNGLCLEARCOLORPROC glad_glClearColor = NULL;
PFNGLCOMPILESHADERPROC glad_glCompileShader = NULL;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram = NULL;
PFNGLCREATESHADERPROC glad_glCreateShader = NULL;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers = NULL;
PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers = NULL;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram = NULL;
PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers = NULL;
PFNGLDELETESHADERPROC glad_glDeleteShader = NULL;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures = NULL;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays = NULL;
PFNGLDEPTHFUNCPROC glad_glDepthFunc = NULL;
PFNGLDISABLEPROC glad_glDisable = NULL;
PFNGLDRAWARRAYSPROC glad_glDrawArrays = NULL;
PFNGLENABLEPROC glad_glEnable = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = NULL;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D = NULL;
PFNGLGENBUFFERSPROC glad_glGenBuffers = NULL;
PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers = NULL;
PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers = NULL;
PFNGLGENTEXTURESPROC glad_glGenTextures = NULL;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays = NULL;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = NULL;
PFNGLGETINTEGERVPROC glad_glGetIntegerv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog = NULL;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv = NULL;
PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog = NULL;
PFNGLGETSHADERIVPROC glad_glGetShaderiv = NULL;
PFNGLGETTEXIMAGEPROC glad_glGetTexImage = NULL;
PFNGLLINKPROGRAMPROC glad_glLinkProgram = NULL;
PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage = NULL;
PFNGLSHADERSOURCEPROC glad_glShaderSource = NULL;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D = NULL;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri = NULL;
PFNGLTEXSUBIMAGE2DPROC glad_glTexSubImage2D = NULL;
PFNGLUNIFORM1IPROC glad_glUniform1i = NULL;
PFNGLUNIFORM4FPROC glad_glUniform4f = NULL;
PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv = NULL;
PFNGLUSEPROGRAMPROC glad_glUseProgram = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer = NULL;
PFNGLVIEWPORTPROC glad_glViewport = NULL;

#define LOAD_GL_WITH(load, name, type) do { glad_##name = (type)load(#name); ok = ok && glad_##name != NULL; } while (0)

int gladLoadGL(GLADloadfunc load) {
    if (load == NULL) {
        return 0;
    }
    int ok = 1;
    LOAD_GL_WITH(load, glActiveTexture, PFNGLACTIVETEXTUREPROC);
    LOAD_GL_WITH(load, glAttachShader, PFNGLATTACHSHADERPROC);
    LOAD_GL_WITH(load, glBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD_GL_WITH(load, glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC);
    LOAD_GL_WITH(load, glBindRenderbuffer, PFNGLBINDRENDERBUFFERPROC);
    LOAD_GL_WITH(load, glBindTexture, PFNGLBINDTEXTUREPROC);
    LOAD_GL_WITH(load, glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    LOAD_GL_WITH(load, glBufferData, PFNGLBUFFERDATAPROC);
    LOAD_GL_WITH(load, glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC);
    LOAD_GL_WITH(load, glClear, PFNGLCLEARPROC);
    LOAD_GL_WITH(load, glClearColor, PFNGLCLEARCOLORPROC);
    LOAD_GL_WITH(load, glCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD_GL_WITH(load, glCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD_GL_WITH(load, glCreateShader, PFNGLCREATESHADERPROC);
    LOAD_GL_WITH(load, glDeleteBuffers, PFNGLDELETEBUFFERSPROC);
    LOAD_GL_WITH(load, glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC);
    LOAD_GL_WITH(load, glDeleteProgram, PFNGLDELETEPROGRAMPROC);
    LOAD_GL_WITH(load, glDeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC);
    LOAD_GL_WITH(load, glDeleteShader, PFNGLDELETESHADERPROC);
    LOAD_GL_WITH(load, glDeleteTextures, PFNGLDELETETEXTURESPROC);
    LOAD_GL_WITH(load, glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC);
    LOAD_GL_WITH(load, glDepthFunc, PFNGLDEPTHFUNCPROC);
    LOAD_GL_WITH(load, glDisable, PFNGLDISABLEPROC);
    LOAD_GL_WITH(load, glDrawArrays, PFNGLDRAWARRAYSPROC);
    LOAD_GL_WITH(load, glEnable, PFNGLENABLEPROC);
    LOAD_GL_WITH(load, glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD_GL_WITH(load, glFramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC);
    LOAD_GL_WITH(load, glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC);
    LOAD_GL_WITH(load, glGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD_GL_WITH(load, glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC);
    LOAD_GL_WITH(load, glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC);
    LOAD_GL_WITH(load, glGenTextures, PFNGLGENTEXTURESPROC);
    LOAD_GL_WITH(load, glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    LOAD_GL_WITH(load, glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD_GL_WITH(load, glGetIntegerv, PFNGLGETINTEGERVPROC);
    LOAD_GL_WITH(load, glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
    LOAD_GL_WITH(load, glGetProgramiv, PFNGLGETPROGRAMIVPROC);
    LOAD_GL_WITH(load, glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    LOAD_GL_WITH(load, glGetShaderiv, PFNGLGETSHADERIVPROC);
    LOAD_GL_WITH(load, glGetTexImage, PFNGLGETTEXIMAGEPROC);
    LOAD_GL_WITH(load, glLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD_GL_WITH(load, glRenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC);
    LOAD_GL_WITH(load, glShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD_GL_WITH(load, glTexImage2D, PFNGLTEXIMAGE2DPROC);
    LOAD_GL_WITH(load, glTexParameteri, PFNGLTEXPARAMETERIPROC);
    LOAD_GL_WITH(load, glTexSubImage2D, PFNGLTEXSUBIMAGE2DPROC);
    LOAD_GL_WITH(load, glUniform1i, PFNGLUNIFORM1IPROC);
    LOAD_GL_WITH(load, glUniform4f, PFNGLUNIFORM4FPROC);
    LOAD_GL_WITH(load, glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
    LOAD_GL_WITH(load, glUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD_GL_WITH(load, glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD_GL_WITH(load, glViewport, PFNGLVIEWPORTPROC);
    return ok;
}

int gladLoaderLoadGL(void) {
    return gladLoadGL(px_get_proc);
}

void gladLoaderUnloadGL(void) {
#if defined(_WIN32)
    if (px_gl_module != NULL) {
        FreeLibrary(px_gl_module);
        px_gl_module = NULL;
    }
#elif defined(__APPLE__) || !defined(_WIN32)
    if (px_gl_module != NULL) {
        dlclose(px_gl_module);
        px_gl_module = NULL;
    }
#endif
}
