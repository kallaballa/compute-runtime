/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#define _GDI32_ //It causes that definitions of functions are not loaded from dll in file wingdi.h because they are in this file.

#include "opencl/test/unit_test/helpers/linux/mock_function.h"
#include "opencl/test/unit_test/mocks/gl/linux/mock_gl_sharing_linux.h"

#include "GL/gl.h"

#include <string.h>

extern "C" {
const char *glString = "Intel";
const char *glVersion = "4.0";
const char *arrayStringi[2]{"GL_OES_framebuffer_object", "GL_EXT_framebuffer_object"};
int GLAcquireSharedBufferCalled = 0;
int GLAcquireSharedRenderBufferCalled = 0;
int GLAcquireSharedTextureCalled = 0;
int GLDeleteContextCalled = 0;
int GLGetCurrentContextCalled = 0;
int GLGetCurrentDisplayCalled = 0;
int GLGetSyncivCalled = 0;
int GLMakeCurrentCalled = 0;
int GLReleaseSharedBufferCalled = 0;
int GLReleaseSharedRenderBufferCalled = 0;
int GLReleaseSharedTextureCalled = 0;
int GLReleaseSyncCalled = 0;
int GLRetainSyncCalled = 0;
int EGLCreateContextCalled = 0;
int EGLDeleteContextCalled = 0;
int EGLShareListsCalled = 0;
CL_GL_BUFFER_INFO bufferInfoInput = {0};
CL_GL_BUFFER_INFO bufferInfoOutput = {0};
CL_GL_RESOURCE_INFO textureInfoInput = {0};
CL_GL_RESOURCE_INFO textureInfoOutput = {0};
NEO::GLMockReturnedValues glMockReturnedValues = {0};
GLboolean GLSetSharedOCLContextStateReturnedValue = 1u;
bool glGetLuidFuncAvailable = true;
int glGetLuidCalled = 0;

void glGetTexLevelParameteriv( 	GLenum target,
  	GLint level,
  	GLenum pname,
  	GLint * params) {
    switch (pname) {
        case GL_TEXTURE_WIDTH:
            *params = 256;
            break;
        case GL_TEXTURE_HEIGHT:
            *params = 256;
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            *params = GL_RGBA;
            break;
    }
};

const unsigned char * glGetString(unsigned int name) {
    if (name == GL_VENDOR)
        return reinterpret_cast<const unsigned char *>(glString);
    if (name == GL_VERSION)
        return reinterpret_cast<const unsigned char *>(glVersion);
    return reinterpret_cast<const unsigned char *>("");
};
GLboolean  eglSetSharedOCLContextStateINTELMock(HDC HDCHandle, HGLRC ContextHandle, unsigned char State,
                                                      void *pContextInfo) {
    ((NEO::ContextInfo *)pContextInfo)->ContextHandle = 1;
    ((NEO::ContextInfo *)pContextInfo)->DeviceHandle = 2;
    return GLSetSharedOCLContextStateReturnedValue;
};
GLboolean  mockGLAcquireSharedBuffer(GLDisplay, GLContext, GLContext, GLvoid *pResourceInfo) {

    auto pBufferInfo = (CL_GL_BUFFER_INFO *)pResourceInfo;
    bufferInfoInput = *pBufferInfo;
    pBufferInfo->bufferSize = bufferInfoOutput.bufferSize;
    pBufferInfo->globalShareHandle = bufferInfoOutput.globalShareHandle;
    pBufferInfo->pGmmResInfo = bufferInfoOutput.pGmmResInfo;
    pBufferInfo->bufferOffset = bufferInfoOutput.bufferOffset;
    GLAcquireSharedBufferCalled++;
    return (GLboolean)1;
};
GLboolean  mockGLReleaseSharedBuffer(GLDisplay, GLContext, GLContext, GLvoid *pResourceInfo) {
    bufferInfoInput = *static_cast<CL_GL_BUFFER_INFO *>(pResourceInfo);
    GLReleaseSharedBufferCalled++;
    return (GLboolean)1;
};
GLboolean  mockGLAcquireSharedRenderBuffer(GLDisplay, GLContext, GLContext, GLvoid *pResourceInfo) {
    auto pTextureInfo = (CL_GL_RESOURCE_INFO *)pResourceInfo;
    textureInfoInput = *pTextureInfo;
    pTextureInfo->globalShareHandle = textureInfoOutput.globalShareHandle;
    pTextureInfo->pGmmResInfo = textureInfoOutput.pGmmResInfo;
    pTextureInfo->glInternalFormat = GL_RGBA8;
    GLAcquireSharedRenderBufferCalled++;
    return (GLboolean)1;
};
GLboolean  mockGLReleaseSharedRenderBuffer(GLDisplay, GLContext, GLContext, GLvoid *pResourceInfo) {
    textureInfoInput = *static_cast<CL_GL_RESOURCE_INFO *>(pResourceInfo);
    GLReleaseSharedRenderBufferCalled++;
    return (GLboolean)1;
};
EGLBoolean mockGLAcquireSharedTexture(EGLDisplay dpy, EGLImageKHR image, int *fds, EGLint *strides, EGLint *offsets) {
    GLAcquireSharedTextureCalled++;
    if (image == (void*)0xEEEE) {
        *fds = 10;
        textureInfoInput.name = 1;
        return (GLboolean)1;
    } else {
        return GL_FALSE;
    }
};
GLboolean  mockGlRetainSync(GLDisplay HDCHandle, GLContext ContextHandle, GLContext BackupContextHandle,
                                  GLvoid *pSyncInfo) {
    GLRetainSyncCalled++;
    GL_CL_SYNC_INFO *syncInfo = (GL_CL_SYNC_INFO *)(pSyncInfo);
    syncInfo->pSync = (void *)0x123;
    return GL_TRUE;
};
GLboolean  mockGlReleaseSync(GLDisplay HDCHandle, GLContext ContextHandle, GLContext BackupContextHandle,
                                   GLvoid *pSync) {
    GLReleaseSyncCalled++;
    return GL_TRUE;
};
void  mockGlGetSynciv(GLvoid *pSync, GLenum pname, GLint *value) {
    GLGetSyncivCalled++;
    *value = glMockReturnedValues.syncivRetVal;
};
const unsigned char * glGetStringiMock(unsigned int name, unsigned int index) { return reinterpret_cast<const unsigned char *>(arrayStringi[index]); };
GLDisplay  mockGLGetCurrentDisplay() {
    GLGetCurrentDisplayCalled++;
    return glMockReturnedValues.currentDisplay;
};

#ifdef STUB
LUID  eglGetLuidMock(GLContext glContext) {
    glGetLuidCalled++;
    LUID luid{};
    if (reinterpret_cast<GLContext>(1) == glContext) {
        luid.HighPart = 0x1d2e;
        luid.LowPart = 0x3f4a;
    } else if (reinterpret_cast<GLContext>(2) == glContext) {
        luid.HighPart = 0x5d2e;
        luid.LowPart = 0x3f4a;
    }
    return luid;
};
#endif
typedef void (*PROC)(void);

PROC eglGetProcAddress(const char * name) {
    if (strcmp(name, "eglSetSharedOCLContextStateINTEL") == 0) {
        return reinterpret_cast<PROC>(*eglSetSharedOCLContextStateINTELMock);
    }
    if (strcmp(name, "eglAcquireSharedBufferINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGLAcquireSharedBuffer);
    }
    if (strcmp(name, "eglReleaseSharedBufferINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGLReleaseSharedBuffer);
    }
    if (strcmp(name, "eglAcquireSharedRenderBufferINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGLAcquireSharedRenderBuffer);
    }
    if (strcmp(name, "eglReleaseSharedRenderBufferINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGLReleaseSharedRenderBuffer);
    }
    if (strcmp(name, "eglExportDMABUFImageMESA") == 0) {
        return reinterpret_cast<PROC>(*mockGLAcquireSharedTexture);
    }
    if (strcmp(name, "eglRetainSyncINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGlRetainSync);
    }
    if (strcmp(name, "eglReleaseSyncINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGlReleaseSync);
    }
    if (strcmp(name, "eglGetSyncivINTEL") == 0) {
        return reinterpret_cast<PROC>(*mockGlGetSynciv);
    }
    if (strcmp(name, "glGetStringi") == 0) {
        return reinterpret_cast<PROC>(*glGetStringiMock);
    }
#ifdef STUB
    if (strcmp(name, "eglGetLuidINTEL") == 0 && glGetLuidFuncAvailable) {
        return reinterpret_cast<PROC>(eglGetLuidMock);
    }
#endif
    return nullptr;
}
HGLRC  eglGetCurrentContext() {
    GLGetCurrentContextCalled++;
    return glMockReturnedValues.currentContext;
};
HDC  eglGetCurrentDisplay() { return mockGLGetCurrentDisplay(); };

EGLAPI EGLContext EGLAPIENTRY eglCreateContext (EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    EGLCreateContextCalled++;
    return (GLContext)0x101;
};
EGLBoolean  eglDestroyContext(EGLDisplay display, EGLContext context) {
    EGLDeleteContextCalled++;
    GLDeleteContextCalled++;
    return (GLboolean)1;
};
void  glGetIntegerv(GLenum pname, GLint *params) { return NEO::MockGLSharingFunctions::glGetIntegervTest(pname, params); };
BOOL  eglShareLists(HGLRC arg1, HGLRC arg2) {
    EGLShareListsCalled++;
    return 1;
};

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext arg2) {
    GLMakeCurrentCalled++;
    glMockReturnedValues.madeCurrentContext = arg2;
    if (glMockReturnedValues.forceMakeCurrentCallFail) {
        if (glMockReturnedValues.failsCounter < glMockReturnedValues.numberOfCallFails) {
            glMockReturnedValues.failsCounter++;
            return GL_FALSE;
        }
    }
    return (GLboolean)1;
};


EGLImage eglCreateImage(EGLDisplay display, EGLContext context, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list)
{
    return (void*)0xEEEE;
}

EGLBoolean eglDestroyImage(EGLDisplay  dpy, EGLImage  image) {
    if (image == (void*) 0xEEEE) return EGL_TRUE; else return EGL_FALSE; 
}

void * mockLoader(const char *name) {
    if (strcmp(name, "realFunction") == 0) {
        return (void*)realFunction;
    }
    return nullptr;
};
void resetParam(const char *name) {
    if (strcmp(name, "GLAcquireSharedBufferCalled") == 0) {
        GLAcquireSharedBufferCalled = 0;
    }
    if (strcmp(name, "GLAcquireSharedRenderBufferCalled") == 0) {
        GLAcquireSharedRenderBufferCalled = 0;
    }
    if (strcmp(name, "GLAcquireSharedTextureCalled") == 0) {
        GLAcquireSharedTextureCalled = 0;
    }
    if (strcmp(name, "GLDeleteContextCalled") == 0) {
        GLDeleteContextCalled = 0;
    }
    if (strcmp(name, "GLGetCurrentContextCalled") == 0) {
        GLGetCurrentContextCalled = 0;
    }
    if (strcmp(name, "GLGetCurrentDisplayCalled") == 0) {
        GLGetCurrentDisplayCalled = 0;
    }
    if (strcmp(name, "GLGetSyncivCalled") == 0) {
        GLGetSyncivCalled = 0;
    }
    if (strcmp(name, "GLMakeCurrentCalled") == 0) {
        GLMakeCurrentCalled = 0;
    }
    if (strcmp(name, "GLReleaseSharedBufferCalled") == 0) {
        GLReleaseSharedBufferCalled = 0;
    }
    if (strcmp(name, "GLReleaseSharedRenderBufferCalled") == 0) {
        GLReleaseSharedRenderBufferCalled = 0;
    }
    if (strcmp(name, "GLReleaseSharedTextureCalled") == 0) {
        GLReleaseSharedTextureCalled = 0;
    }
    if (strcmp(name, "GLReleaseSyncCalled") == 0) {
        GLReleaseSyncCalled = 0;
    }
    if (strcmp(name, "GLRetainSyncCalled") == 0) {
        GLRetainSyncCalled = 0;
    }
    if (strcmp(name, "EGLCreateContextCalled") == 0) {
        EGLCreateContextCalled = 0;
    }
    if (strcmp(name, "EGLDeleteContextCalled") == 0) {
        EGLDeleteContextCalled = 0;
    }
    if (strcmp(name, "EGLShareListsCalled") == 0) {
        EGLShareListsCalled = 0;
    }
    if (strcmp(name, "") == 0) {
        GLAcquireSharedBufferCalled = 0;
        GLAcquireSharedRenderBufferCalled = 0;
        GLAcquireSharedTextureCalled = 0;
        GLDeleteContextCalled = 0;
        GLGetCurrentContextCalled = 0;
        GLGetCurrentDisplayCalled = 0;
        GLGetSyncivCalled = 0;
        GLMakeCurrentCalled = 0;
        GLReleaseSharedBufferCalled = 0;
        GLReleaseSharedRenderBufferCalled = 0;
        GLReleaseSharedTextureCalled = 0;
        GLReleaseSyncCalled = 0;
        GLRetainSyncCalled = 0;
        EGLCreateContextCalled = 0;
        EGLDeleteContextCalled = 0;
        EGLShareListsCalled = 0;
        glGetLuidCalled = 0;
        glGetLuidFuncAvailable = true;
    }
    if (strcmp(name, "glGetLuidCalled") == 0) {
        glGetLuidCalled = 0;
    }
    if (strcmp(name, "glGetLuidFuncAvailable") == 0) {
        glGetLuidFuncAvailable = true;
    }
    if (strcmp(name, "glGetLuidFuncNotAvailable") == 0) {
        glGetLuidFuncAvailable = false;
    }
};
int getParam(const char *name) {
    if (strcmp(name, "GLAcquireSharedBufferCalled") == 0) {
        return GLAcquireSharedBufferCalled;
    }
    if (strcmp(name, "GLAcquireSharedRenderBufferCalled") == 0) {
        return GLAcquireSharedRenderBufferCalled;
    }
    if (strcmp(name, "GLAcquireSharedTextureCalled") == 0) {
        return GLAcquireSharedTextureCalled;
    }
    if (strcmp(name, "GLDeleteContextCalled") == 0) {
        return GLDeleteContextCalled;
    }
    if (strcmp(name, "GLGetCurrentContextCalled") == 0) {
        return GLGetCurrentContextCalled;
    }
    if (strcmp(name, "GLGetCurrentDisplayCalled") == 0) {
        return GLGetCurrentDisplayCalled;
    }
    if (strcmp(name, "GLGetSyncivCalled") == 0) {
        return GLGetSyncivCalled;
    }
    if (strcmp(name, "GLMakeCurrentCalled") == 0) {
        return GLMakeCurrentCalled;
    }
    if (strcmp(name, "GLReleaseSharedBufferCalled") == 0) {
        return GLReleaseSharedBufferCalled;
    }
    if (strcmp(name, "GLReleaseSharedRenderBufferCalled") == 0) {
        return GLReleaseSharedRenderBufferCalled;
    }
    if (strcmp(name, "GLReleaseSharedTextureCalled") == 0) {
        return GLReleaseSharedTextureCalled;
    }
    if (strcmp(name, "GLReleaseSyncCalled") == 0) {
        return GLReleaseSyncCalled;
    }
    if (strcmp(name, "GLRetainSyncCalled") == 0) {
        return GLRetainSyncCalled;
    }
    if (strcmp(name, "EGLCreateContextCalled") == 0) {
        return EGLCreateContextCalled;
    }
    if (strcmp(name, "EGLDeleteContextCalled") == 0) {
        return EGLDeleteContextCalled;
    }
    if (strcmp(name, "EGLShareListsCalled") == 0) {
        return EGLShareListsCalled;
    }
    if (strcmp(name, "glGetLuidCalled") == 0) {
        return glGetLuidCalled;
    }
    return 0;
};
CL_GL_BUFFER_INFO getBufferInfo() { return bufferInfoInput; };
CL_GL_RESOURCE_INFO getTextureInfo() { return textureInfoInput; };
void memParam() {
    memset(&bufferInfoInput, 0, sizeof(CL_GL_BUFFER_INFO));
    memset(&bufferInfoOutput, 0, sizeof(CL_GL_BUFFER_INFO));
    memset(&textureInfoInput, 0, sizeof(CL_GL_RESOURCE_INFO));
    memset(&textureInfoOutput, 0, sizeof(CL_GL_RESOURCE_INFO));
    memset(&glMockReturnedValues, 0, sizeof(NEO::GLMockReturnedValues));
};
void loadBuffer(CL_GL_BUFFER_INFO buff) { bufferInfoOutput = buff; };
void loadTexture(CL_GL_RESOURCE_INFO texture) { textureInfoOutput = texture; };
NEO::GLMockReturnedValues getGlMockReturnedValues() { return glMockReturnedValues; };
void setGlMockReturnedValues(NEO::GLMockReturnedValues value) { glMockReturnedValues = value; };
void setGetSyncivReturnValue(int val) { glMockReturnedValues.syncivRetVal = val; }
void glSetString(const char *name, unsigned int var) {
    if (var == GL_VENDOR) {
        glString = name;
    } else if (var == GL_VERSION) {
        glVersion = name;
    }
};
void glSetStringi(const char *name, unsigned int index) { arrayStringi[index] = name; };
void setGLSetSharedOCLContextStateReturnedValue(GLboolean value) { GLSetSharedOCLContextStateReturnedValue = static_cast<GLboolean>(value); };
GLboolean getGLSetSharedOCLContextStateReturnedValue() { return GLSetSharedOCLContextStateReturnedValue; };
}
