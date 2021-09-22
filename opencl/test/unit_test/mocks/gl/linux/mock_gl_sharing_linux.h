/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/extensions/public/cl_gl_private_intel.h"
#include "opencl/source/sharings/gl/linux/gl_sharing_linux.h"
#include "opencl/test/unit_test/sharings/gl/gl_dll_helper.h"

#include "config.h"

#include <cstring>

namespace NEO {
struct EGLBkpContextParams {
    int32_t configAttrs;
    int32_t contextAttrs[3];
    int32_t numConfigs;
};

struct GLXBkpContextParams {
    int FBConfigAttrs;
    int queryAttribute;
    int renderType;
};

struct GLMockReturnedValues {
    GLContext currentContext;
    GLDisplay currentDisplay;
    GLContext madeCurrentContext;
    bool forceMakeCurrentCallFail;
    int numberOfCallFails;
    int failsCounter;
    int syncivRetVal;
};

extern int GLSetSharedOCLContextStateCalled;
extern int EGLCreateContextCalled;
extern int EGLDeleteContextCalled;
extern int EGLChooseConfigCalled;
extern int GlxChooseFBConfigCalled;
extern int GlxQueryContextCalled;
extern int GlxCreateNewContextCalled;
extern int GlxDeleteContextCalled;
extern int GlxIsDirectCalled;
extern EGLBkpContextParams eglBkpContextParams;
extern GLXBkpContextParams glxBkpContextParams;

namespace glTextureTargets {
static const unsigned int supportedTargets[] = {
#ifdef STUB
    GL_TEXTURE_1D,
    GL_TEXTURE_1D_ARRAY,
    GL_TEXTURE_BUFFER,
#endif
    GL_TEXTURE_2D,
#ifdef STUB
    GL_TEXTURE_2D_ARRAY,
    GL_TEXTURE_RECTANGLE,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_X,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
    GL_TEXTURE_3D,
    GL_RENDERBUFFER_EXT,
    GL_TEXTURE_2D_MULTISAMPLE,
    GL_TEXTURE_2D_MULTISAMPLE_ARRAY,
#endif
};
}

class GlSharingFunctionsMock : public GLSharingFunctionsLinux {

    void initMembers();

  public:
    static GLboolean  mockGLSetSharedOCLContextState(GLDisplay, GLContext, GLboolean, GLvoid *pBufferInfo) {
        GLSetSharedOCLContextStateCalled++;
        return (GLboolean)1;
    };
    ~GlSharingFunctionsMock() override = default;

    using GLSharingFunctionsLinux::GLAcquireSharedBuffer;
    using GLSharingFunctionsLinux::GLAcquireSharedRenderBuffer;
    using GLSharingFunctionsLinux::GLAcquireSharedTexture;
    using GLSharingFunctionsLinux::GLGetCurrentContext;
    using GLSharingFunctionsLinux::GLGetCurrentDisplay;
    using GLSharingFunctionsLinux::glGetIntegerv;
    using GLSharingFunctionsLinux::glGetString;
    using GLSharingFunctionsLinux::glGetStringi;
    using GLSharingFunctionsLinux::GLGetSynciv;
    using GLSharingFunctionsLinux::GLReleaseSharedBuffer;
    using GLSharingFunctionsLinux::GLReleaseSharedRenderBuffer;
    using GLSharingFunctionsLinux::GLReleaseSharedTexture;
    using GLSharingFunctionsLinux::GLReleaseSync;
    using GLSharingFunctionsLinux::GLRetainSync;
    using GLSharingFunctionsLinux::GLSetSharedOCLContextState;
    using GLSharingFunctionsLinux::isOpenGlExtensionSupported;
    using GLSharingFunctionsLinux::pfnEglCreateContext;
    using GLSharingFunctionsLinux::pfnEglDeleteContext;
    using GLSharingFunctionsLinux::pfnEglShareLists;
    using GLSharingFunctionsLinux::setSharedOCLContextState;
    using GLSharingFunctionsLinux::eglMakeCurrent;

    using GLSharingFunctionsLinux::glArbEventMapping;
    using GLSharingFunctionsLinux::GLContextHandle;
    using GLSharingFunctionsLinux::GLDeviceHandle;

    using GLSharingFunctionsLinux::getSupportedFormats;
    using GLSharingFunctionsLinux::pfnGlArbSyncObjectCleanup;
    using GLSharingFunctionsLinux::pfnGlArbSyncObjectSetup;
    using GLSharingFunctionsLinux::pfnGlArbSyncObjectSignal;
    using GLSharingFunctionsLinux::pfnGlArbSyncObjectWaitServer;

    GlSharingFunctionsMock(GLType GLHDCType, GLContext GLHGLRCHandle, GLContext GLHGLRCHandleBkpCtx, GLDisplay GLHDCHandle)
        : GLSharingFunctionsLinux(GLHDCType, GLHGLRCHandle, GLHGLRCHandleBkpCtx, GLHDCHandle) {
        initMembers();
        updateOpenGLContext();
        createBackupContext();
    }
    GlSharingFunctionsMock();

    void setHandles(GLType GLHDCType, GLContext GLHGLRCHandle, GLContext GLHGLRCHandleBkpCtx, GLDisplay GLHDCHandle) {
        this->GLHDCType = GLHDCType;
        this->GLHGLRCHandle = GLHGLRCHandle;
        this->GLHGLRCHandleBkpCtx = GLHGLRCHandleBkpCtx;
        this->GLHDCHandle = GLHDCHandle;
    }

    void setGLAcquireSharedBufferMock(PFNOGLAcquireSharedBufferINTEL mock) { GLAcquireSharedBuffer = mock; }

    void setGLAcquireSharedTextureMock(PFNEGLEXPORTDMABUFIMAGEMESAPROC mock) { GLAcquireSharedTexture = mock; }
};

class MockGlSharing {
  public:
    MockGlSharing() {}
    MockGlSharing(GLType GLHDCType, GLContext GLHGLRCHandle, GLContext GLHGLRCHandleBkpCtx, GLDisplay GLHDCHandle);
    void uploadDataToBufferInfo() {
        dllParam->loadBuffer(m_bufferInfoOutput);
    }
    void uploadDataToBufferInfo(unsigned int sharedHandle, int bufferOffset, GMM_RESOURCE_INFO *gmmResInfo) {
        m_bufferInfoOutput.globalShareHandle = sharedHandle;
        m_bufferInfoOutput.bufferOffset = bufferOffset;
        m_bufferInfoOutput.pGmmResInfo = gmmResInfo;
        dllParam->loadBuffer(m_bufferInfoOutput);
    }
    void uploadDataToTextureInfo() {
        dllParam->loadTexture(m_textureInfoOutput);
    }
    void uploadDataToTextureInfo(unsigned int sharedHandle) {
        m_textureInfoOutput.globalShareHandle = sharedHandle;
        dllParam->loadTexture(m_textureInfoOutput);
    }
    void uploadTextureBufferOffsetToTextureInfo(int texBufOffset) {
        m_textureInfoOutput.textureBufferOffset = texBufOffset;
        dllParam->loadTexture(m_textureInfoOutput);
    }
    void overrideGetCurrentValues(GLContext ctx, GLDisplay display, bool forceMakeCurrentFail = false, int numberOfFails = 0) {
        glMockReturnedValues.currentContext = ctx;
        glMockReturnedValues.currentDisplay = display;
        glMockReturnedValues.forceMakeCurrentCallFail = forceMakeCurrentFail;
        glMockReturnedValues.numberOfCallFails = numberOfFails;
        glMockReturnedValues.failsCounter = 0;
        dllParam->setGlMockReturnedValues(glMockReturnedValues);
    }
    void setGetSyncivReturnValue(int val) {
        glMockReturnedValues.syncivRetVal = val;
        dllParam->setGlMockReturnedValues(glMockReturnedValues);
    }

    std::unique_ptr<GlSharingFunctionsMock> sharingFunctions = std::make_unique<GlSharingFunctionsMock>();
    std::unique_ptr<GlDllHelper> dllParam = std::make_unique<GlDllHelper>();
    CL_GL_RESOURCE_INFO m_clGlResourceInfo = {0};
    GL_CL_RESOURCE_INFO m_glClResourceInfo = {0};
    CL_GL_BUFFER_INFO m_bufferInfoOutput = {0};
    CL_GL_RESOURCE_INFO m_textureInfoOutput = {0};
    GLMockReturnedValues glMockReturnedValues = {0};
};

class MockGLSharingFunctions : public GLSharingFunctionsLinux {
  public:
    using GLSharingFunctionsLinux::isOpenGlExtensionSupported;
    using GLSharingFunctionsLinux::setSharedOCLContextState;

    static bool SharingEnabled;

    static void  glGetIntegervTest(GLenum pname, GLint *data) {
        if (pname == GL_NUM_EXTENSIONS)
            *data = 2;
    };
    using GLSharingFunctionsLinux::glGetIntegerv;
    using GLSharingFunctionsLinux::glGetString;
    std::unique_ptr<GlDllHelper> dllParam = std::make_unique<GlDllHelper>();
    MockGLSharingFunctions() {
        GLSharingFunctionsLinux::initGLFunctions();
        MockGLSharingFunctions::SharingEnabled = 1;
    }
};
} // namespace NEO
