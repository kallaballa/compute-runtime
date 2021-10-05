/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "opencl/source/sharings/gl/gl_sharing.h"
#include "opencl/source/sharings/gl/linux/include/gl_types.h"
#include "opencl/extensions/public/cl_gl_private_intel.h"

#include <GL/gl.h>
#include <EGL/eglext.h>

//from windows
#define BOOL unsigned char
typedef void* HDC;
typedef void* HGLRC;

namespace NEO {
//OpenGL API names
typedef GLboolean(*PFNOGLSetSharedOCLContextStateINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLboolean state, GLvoid *pContextInfo);
typedef GLboolean(*PFNOGLAcquireSharedBufferINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pBufferInfo);
typedef GLboolean(*PFNOGLAcquireSharedRenderBufferINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pResourceInfo);
//typedef GLboolean(*PFNOGLAcquireSharedTextureINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pResourceInfo);
typedef GLboolean(*PFNOGLReleaseSharedBufferINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pBufferInfo);
typedef GLboolean(*PFNOGLReleaseSharedRenderBufferINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pResourceInfo);
typedef GLboolean(*PFNOGLReleaseSharedTextureINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pResourceInfo);
typedef GLContext(*PFNOGLGetCurrentContext)();
typedef GLDisplay(*PFNOGLGetCurrentDisplay)();
typedef GLboolean(*PFNOGLMakeCurrent)(GLDisplay hdcHandle, void *draw, void *read, GLContext contextHandle);
typedef GLboolean(*PFNOGLRetainSyncINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pSyncInfo);
typedef GLboolean(*PFNOGLReleaseSyncINTEL)(GLDisplay hdcHandle, GLContext contextHandle, GLContext backupContextHandle, GLvoid *pSync);
typedef void(*PFNOGLGetSyncivINTEL)(GLvoid *pSync, GLenum pname, GLint *value);

typedef const GLubyte *(*PFNglGetString)(GLenum name);
typedef const GLubyte *(*PFNglGetStringi)(GLenum name, GLuint index);
typedef void(*PFNglGetIntegerv)(GLenum pname, GLint *params);
typedef void(*PFNglBindTexture)(GLenum target, GLuint texture);

//egl
typedef BOOL(*PFNeglMakeCurrent)(HDC, HGLRC);
typedef GLContext(*PFNeglCreateContext)(GLDisplay hdcHandle);
typedef int(*PFNeglShareLists)(GLContext contextHandle, GLContext backupContextHandle);
typedef BOOL(*PFNeglDeleteContext)(HGLRC hglrcHandle);

typedef bool (*PFNglArbSyncObjectSetup)(GLSharingFunctions &sharing, OSInterface &osInterface, CL_GL_SYNC_INFO &glSyncInfo);
typedef void (*PFNglArbSyncObjectCleanup)(OSInterface &osInterface, CL_GL_SYNC_INFO *glSyncInfo);
typedef void (*PFNglArbSyncObjectSignal)(OsContext &osContext, CL_GL_SYNC_INFO &glSyncInfo);
typedef void (*PFNglArbSyncObjectWaitServer)(OSInterface &osInterface, CL_GL_SYNC_INFO &glSyncInfo);

typedef EGLImage (*PFNeglCreateImage)(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list);

#ifdef STUB
typedef LUID(*PFNGLGETLUIDINTEL)(HGLRC hglrcHandle);
#endif
class GLSharingFunctionsLinux : public GLSharingFunctions {
  public:
    GLSharingFunctionsLinux() = default;
    GLSharingFunctionsLinux(GLType glhdcType, GLContext glhglrcHandle, GLContext glhglrcHandleBkpCtx, GLDisplay glhdcHandle);
    ~GLSharingFunctionsLinux() override;

    OS_HANDLE getGLDeviceHandle() const { return GLDeviceHandle; }
    OS_HANDLE getGLContextHandle() const { return GLContextHandle; }

    GLboolean initGLFunctions() override;
    bool isOpenGlSharingSupported() override;
    static bool isGlSharingEnabled();

    // Arb sync event
    template <typename EventType = GlArbSyncEvent>
    auto getOrCreateGlArbSyncEvent(Event &baseEvent) -> decltype(EventType::create(baseEvent));
    GlArbSyncEvent *getGlArbSyncEvent(Event &baseEvent);
    void removeGlArbSyncEventMapping(Event &baseEvent);

    // Gl functions
    GLboolean acquireSharedBufferINTEL(GLvoid *pBufferInfo) {
        return GLAcquireSharedBuffer(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pBufferInfo);
    }
    GLboolean releaseSharedBufferINTEL(GLvoid *pBufferInfo) {
        return GLReleaseSharedBuffer(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pBufferInfo);
    }
    GLboolean acquireSharedRenderBuffer(GLvoid *pResourceInfo) {
        return GLAcquireSharedRenderBuffer(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pResourceInfo);
    }
    GLboolean releaseSharedRenderBuffer(GLvoid *pResourceInfo) {
        return GLReleaseSharedRenderBuffer(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pResourceInfo);
    }
    EGLBoolean acquireSharedTexture(CL_GL_RESOURCE_INFO *pResourceInfo) {
//       typedef EGLBoolean (EGLAPIENTRYP PFNEGLEXPORTDMABUFIMAGEMESAPROC) (EGLDisplay dpy, EGLImageKHR image, int *fds, EGLint *strides, EGLint *offsets);
        int fds;
        int stride, offset;
        int miplevel = 0;

        EGLAttrib attrib_list[] = {EGL_GL_TEXTURE_LEVEL, miplevel, EGL_NONE};
        EGLImage image = eglCreateImage(GLHDCHandle, GLHGLRCHandle, EGL_GL_TEXTURE_2D, (EGLClientBuffer) pResourceInfo->name, &attrib_list[0]);

        EGLBoolean ret = GLAcquireSharedTexture(GLHDCHandle, image, &fds, &stride, &offset);
        printf("fds=%d stride=%d offset=%d\n", fds, stride, offset);
        pResourceInfo->globalShareHandle = fds;

        return ret;
    }
    GLboolean releaseSharedTexture(GLvoid *pResourceInfo) {
#ifdef STUB
        return GLReleaseSharedTexture(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pResourceInfo);
#else
        return 1;
#endif
    }
    GLboolean retainSync(GLvoid *pSyncInfo) {
        return GLRetainSync(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pSyncInfo);
    }
    GLboolean releaseSync(GLvoid *pSync) {
        return GLReleaseSync(GLHDCHandle, GLHGLRCHandle, GLHGLRCHandleBkpCtx, pSync);
    }
    void getSynciv(GLvoid *pSync, GLenum pname, GLint *value) {
        return GLGetSynciv(pSync, pname, value);
    }
    GLContext getCurrentContext() {
        return GLGetCurrentContext();
    }
    GLDisplay getCurrentDisplay() {
        return GLGetCurrentDisplay();
    }
    GLboolean makeCurrent(GLContext contextHandle, GLDisplay displayHandle = 0) {
        if (displayHandle == 0) {
            displayHandle = GLHDCHandle;
        }
        return this->eglMakeCurrent(displayHandle, contextHandle);
    }
    GLContext getBackupContextHandle() {
        return GLHGLRCHandleBkpCtx;
    }
    GLContext getContextHandle() {
        return GLHGLRCHandle;
    }
    bool glArbSyncObjectSetup(OSInterface &osInterface, CL_GL_SYNC_INFO &glSyncInfo) {
        return pfnGlArbSyncObjectSetup(*this, osInterface, glSyncInfo);
    }
    void glArbSyncObjectCleanup(OSInterface &osInterface, CL_GL_SYNC_INFO *glSyncInfo) {
        pfnGlArbSyncObjectCleanup(osInterface, glSyncInfo);
    }
    void glArbSyncObjectSignal(OsContext &osContext, CL_GL_SYNC_INFO &glSyncInfo) {
        pfnGlArbSyncObjectSignal(osContext, glSyncInfo);
    }
    void glArbSyncObjectWaitServer(OSInterface &osInterface, CL_GL_SYNC_INFO &glSyncInfo) {
        pfnGlArbSyncObjectWaitServer(osInterface, glSyncInfo);
    }
#ifdef STUB
    LUID getAdapterLuid(GLContext glhglrcHandle) const;
#endif
    // Buffer reuse
    std::mutex mutex;
    std::vector<std::pair<unsigned int, GraphicsAllocation *>> graphicsAllocationsForGlBufferReuse;

  protected:
    void updateOpenGLContext() {
        if (GLSetSharedOCLContextState) {
            setSharedOCLContextState();
        }
    }
    GLboolean setSharedOCLContextState();
    void createBackupContext();
    bool isOpenGlExtensionSupported(const unsigned char *pExtentionString);

    // Handles
    GLType GLHDCType = 0;
    GLContext GLHGLRCHandle = 0;
    GLContext GLHGLRCHandleBkpCtx = 0;
    GLDisplay GLHDCHandle = 0;
    OS_HANDLE GLDeviceHandle = 0;
    OS_HANDLE GLContextHandle = 0;

    // GL functions
    std::unique_ptr<OsLibrary> glLibrary;
    PFNOGLSetSharedOCLContextStateINTEL GLSetSharedOCLContextState = nullptr;
    PFNOGLAcquireSharedBufferINTEL GLAcquireSharedBuffer = nullptr;
    PFNOGLReleaseSharedBufferINTEL GLReleaseSharedBuffer = nullptr;
    PFNOGLAcquireSharedRenderBufferINTEL GLAcquireSharedRenderBuffer = nullptr;
    PFNOGLReleaseSharedRenderBufferINTEL GLReleaseSharedRenderBuffer = nullptr;
    PFNEGLEXPORTDMABUFIMAGEMESAPROC GLAcquireSharedTexture = nullptr;
    PFNOGLReleaseSharedTextureINTEL GLReleaseSharedTexture = nullptr;
    PFNOGLGetCurrentContext GLGetCurrentContext = nullptr;
    PFNOGLGetCurrentDisplay GLGetCurrentDisplay = nullptr;
    PFNglGetString glGetString = nullptr;
    PFNglGetStringi glGetStringi = nullptr;
    PFNglGetIntegerv glGetIntegerv = nullptr;
    PFNeglCreateContext pfnEglCreateContext = nullptr;
    PFNeglMakeCurrent eglMakeCurrent = nullptr;
    PFNeglShareLists pfnEglShareLists = nullptr;
    PFNeglDeleteContext pfnEglDeleteContext = nullptr;
    PFNOGLRetainSyncINTEL GLRetainSync = nullptr;
    PFNOGLReleaseSyncINTEL GLReleaseSync = nullptr;
    PFNOGLGetSyncivINTEL GLGetSynciv = nullptr;
    PFNglArbSyncObjectSetup pfnGlArbSyncObjectSetup = nullptr;
    PFNglArbSyncObjectCleanup pfnGlArbSyncObjectCleanup = nullptr;
    PFNglArbSyncObjectSignal pfnGlArbSyncObjectSignal = nullptr;
    PFNglArbSyncObjectWaitServer pfnGlArbSyncObjectWaitServer = nullptr;
    PFNeglCreateImage eglCreateImage = nullptr;
#ifdef STUB
    PFNGLGETLUIDINTEL glGetLuid = nullptr;
#endif
    // support for GL_ARB_cl_event
    std::mutex glArbEventMutex;
    std::unordered_map<Event *, GlArbSyncEvent *> glArbEventMapping;
};

template <typename EventType>
inline auto GLSharingFunctionsLinux::getOrCreateGlArbSyncEvent(Event &baseEvent) -> decltype(EventType::create(baseEvent)) {
    std::lock_guard<std::mutex> lock{glArbEventMutex};
    auto it = glArbEventMapping.find(&baseEvent);
    if (it != glArbEventMapping.end()) {
        return it->second;
    }

    auto arbEvent = EventType::create(baseEvent);
    if (nullptr == arbEvent) {
        return arbEvent;
    }
    glArbEventMapping[&baseEvent] = arbEvent;
    return arbEvent;
}

} // namespace NEO
