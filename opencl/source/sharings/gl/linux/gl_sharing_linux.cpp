/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/sharings/gl/linux/gl_sharing_linux.h"

#ifdef None
#undef None
#endif
#ifdef Success
#undef Success
#endif
#ifdef Bool
#undef Bool
#endif

#include "opencl/source/context/context.inl"
#include "opencl/source/helpers/linux/gl_helper.h"
#include "opencl/source/sharings/gl/gl_arb_sync_event.h"

namespace NEO {
GLSharingFunctionsLinux::GLSharingFunctionsLinux(GLType glhdcType, GLContext glhglrcHandle, GLContext glhglrcHandleBkpCtx, GLDisplay glhdcHandle)
    : GLHDCType(glhdcType), GLHGLRCHandle(glhglrcHandle), GLHGLRCHandleBkpCtx(glhglrcHandleBkpCtx), GLHDCHandle(glhdcHandle) {
    initGLFunctions();
    updateOpenGLContext();
#ifdef STUB
    createBackupContext();
#endif
}
GLSharingFunctionsLinux::~GLSharingFunctionsLinux() {
#ifdef STUB
    if (pfnEglDeleteContext) {
        pfnEglDeleteContext(GLHGLRCHandleBkpCtx);
    }
#endif
}

bool GLSharingFunctionsLinux::isGlSharingEnabled() {
    static bool oglLibAvailable = std::unique_ptr<OsLibrary>(OsLibrary::load(Os::openglDllName)).get() != nullptr;
    return oglLibAvailable;
}

void GLSharingFunctionsLinux::createBackupContext() {
    if (pfnEglCreateContext) {
        GLHGLRCHandleBkpCtx = pfnEglCreateContext(GLHDCHandle);
        pfnEglShareLists(GLHGLRCHandle, GLHGLRCHandleBkpCtx);
    }
}

GLboolean GLSharingFunctionsLinux::setSharedOCLContextState() {
    ContextInfo CtxInfo = {0};
    GLboolean retVal = GLSetSharedOCLContextState(GLHDCHandle, GLHGLRCHandle, CL_TRUE, &CtxInfo);
    if (retVal == GL_FALSE) {
        return GL_FALSE;
    }
    GLContextHandle = CtxInfo.ContextHandle;
    GLDeviceHandle = CtxInfo.DeviceHandle;

    return retVal;
}

bool GLSharingFunctionsLinux::isOpenGlExtensionSupported(const unsigned char *pExtensionString) {
    bool LoadedNull = (glGetStringi == nullptr) || (glGetIntegerv == nullptr);
    if (LoadedNull) {
        return false;
    }

    cl_int NumberOfExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &NumberOfExtensions);
    for (cl_int i = 0; i < NumberOfExtensions; i++) {
        std::basic_string<unsigned char> pString = glGetStringi(GL_EXTENSIONS, i);
        if (pString == pExtensionString) {
            return true;
        }
    }
    return false;
}

bool GLSharingFunctionsLinux::isOpenGlSharingSupported() {

    std::basic_string<unsigned char> Vendor = glGetString(GL_VENDOR);
    const unsigned char intelVendor[] = "Intel";

    if ((Vendor.empty()) || (Vendor != intelVendor)) {
        return false;
    }
    std::basic_string<unsigned char> Version = glGetString(GL_VERSION);
    if (Version.empty()) {
        return false;
    }

    bool IsOpenGLES = false;
    const unsigned char versionES[] = "OpenGL ES";
    if (Version.find(versionES) != std::string::npos) {
        IsOpenGLES = true;
    }

    if (IsOpenGLES == true) {
        const unsigned char versionES1[] = "OpenGL ES 1.";
        if (Version.find(versionES1) != std::string::npos) {
            const unsigned char supportGLOES[] = "GL_OES_framebuffer_object";
            if (isOpenGlExtensionSupported(supportGLOES) == false) {
                return false;
            }
        }
    } else {
        if (Version[0] < '3') {
            const unsigned char supportGLEXT[] = "GL_EXT_framebuffer_object";
            if (isOpenGlExtensionSupported(supportGLEXT) == false) {
                return false;
            }
        }
    }

    return true;
}

GlArbSyncEvent *GLSharingFunctionsLinux::getGlArbSyncEvent(Event &baseEvent) {
    std::lock_guard<std::mutex> lock{glArbEventMutex};
    auto it = glArbEventMapping.find(&baseEvent);
    if (it != glArbEventMapping.end()) {
        return it->second;
    }
    return nullptr;
}

void GLSharingFunctionsLinux::removeGlArbSyncEventMapping(Event &baseEvent) {
    std::lock_guard<std::mutex> lock{glArbEventMutex};
    auto it = glArbEventMapping.find(&baseEvent);
    if (it == glArbEventMapping.end()) {
        DEBUG_BREAK_IF(it == glArbEventMapping.end());
        return;
    }
    glArbEventMapping.erase(it);
}

GLboolean GLSharingFunctionsLinux::initGLFunctions() {
    glLibrary.reset(OsLibrary::load(Os::openglDllName));

    if (glLibrary->isLoaded()) {
        glFunctionHelper eglLibrary(glLibrary.get(), "eglGetProcAddress");
        GLGetCurrentContext = (*glLibrary)["eglGetCurrentContext"];
        GLGetCurrentDisplay = (*glLibrary)["eglGetCurrentDisplay"];
//        glGetString = (*glLibrary)["glGetString"]; //libGL.so.1
//        glGetIntegerv = (*glLibrary)["glGetIntegerv"]; //libGL.so.1
        pfnEglCreateContext = (*glLibrary)["eglCreateContext"];
        pfnEglDeleteContext = (*glLibrary)["eglDestroyContext"];
//        pfnEglShareLists = (*glLibrary)["wglShareLists"];
        eglMakeCurrent = (*glLibrary)["eglMakeCurrent"];

#ifdef STUB
        GLSetSharedOCLContextState = eglLibrary["wglSetSharedOCLContextStateINTEL"];
        GLAcquireSharedBuffer = eglLibrary["wglAcquireSharedBufferINTEL"];
        GLReleaseSharedBuffer = eglLibrary["wglReleaseSharedBufferINTEL"];
        GLAcquireSharedRenderBuffer = eglLibrary["wglAcquireSharedRenderBufferINTEL"];
        GLReleaseSharedRenderBuffer = eglLibrary["wglReleaseSharedRenderBufferINTEL"];
        GLAcquireSharedTexture = eglLibrary["wglAcquireSharedTextureINTEL"];
        GLReleaseSharedTexture = eglLibrary["wglReleaseSharedTextureINTEL"];
        GLRetainSync = eglLibrary["wglRetainSyncINTEL"];
        GLReleaseSync = eglLibrary["wglReleaseSyncINTEL"];
#endif
//      GLGetSynciv = eglLibrary["glGetSyncivINTEL"]; //libGL.so.1
//        glGetStringi = eglLibrary["glGetStringi"]; //libGL.so.1
#ifdef STUB
        glGetLuid = eglLibrary["wglGetLuidINTEL"];
#endif
    }
    this->pfnGlArbSyncObjectCleanup = cleanupArbSyncObject;
    this->pfnGlArbSyncObjectSetup = setupArbSyncObject;
    this->pfnGlArbSyncObjectSignal = signalArbSyncObject;
    this->pfnGlArbSyncObjectWaitServer = serverWaitForArbSyncObject;

    return 1;
}

#ifdef STUB
LUID GLSharingFunctionsLinux::getAdapterLuid(GLContext glhglrcHandle) const {
    if (glGetLuid) {
        return glGetLuid(glhglrcHandle);
    }
    return {};
}
#endif

template GLSharingFunctionsLinux *Context::getSharing<GLSharingFunctionsLinux>();

} // namespace NEO
