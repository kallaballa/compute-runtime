/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/sharings/gl/linux/lin_enable_gl.h"
#ifdef None
#undef None
#endif

#include "shared/source/debug_settings/debug_settings_manager.h"

#include "opencl/source/context/context.h"
#include "opencl/source/context/context.inl"
#include "opencl/source/sharings/gl/cl_gl_api_intel.h"
#include "opencl/source/sharings/gl/linux/gl_sharing_linux.h"
#include "opencl/source/sharings/sharing_factory.h"
#include "opencl/source/sharings/sharing_factory.inl"

#include <memory>

namespace NEO {

bool GlSharingContextBuilder::processProperties(cl_context_properties &propertyType, cl_context_properties &propertyValue) {
    if (contextData.get() == nullptr) {
        contextData = std::make_unique<GlCreateContextProperties>();
    }

    switch (propertyType) {
    case CL_GL_CONTEXT_KHR:
        contextData->GLHGLRCHandle = (GLContext)propertyValue;
        return true;
#if 0
        //GLX not supported
    case CL_GLX_DISPLAY_KHR:
        contextData->GLHDCType = (GLType)CL_GLX_DISPLAY_KHR;
        contextData->GLHDCHandle = (GLDisplay)propertyValue;
        return true;
#endif
    case CL_EGL_DISPLAY_KHR:
        contextData->GLHDCType = (GLType)CL_EGL_DISPLAY_KHR;
        contextData->GLHDCHandle = (GLDisplay)propertyValue;
        return true;
    }
    return false;
}

bool GlSharingContextBuilder::finalizeProperties(Context &context, int32_t &errcodeRet) {
    if (contextData.get() == nullptr)
        return true;

    if (contextData->GLHGLRCHandle) {
        context.registerSharing(new GLSharingFunctionsLinux(contextData->GLHDCType, contextData->GLHGLRCHandle,
                                                              nullptr, contextData->GLHDCHandle));
    }

    contextData.reset(nullptr);
    return true;
}

std::unique_ptr<SharingContextBuilder> GlSharingBuilderFactory::createContextBuilder() {
    return std::make_unique<GlSharingContextBuilder>();
};

void GlSharingBuilderFactory::fillGlobalDispatchTable() {
    icdGlobalDispatchTable.clCreateFromGLBuffer = clCreateFromGLBuffer;
    icdGlobalDispatchTable.clCreateFromGLTexture = clCreateFromGLTexture;
    icdGlobalDispatchTable.clCreateFromGLTexture2D = clCreateFromGLTexture2D;
    icdGlobalDispatchTable.clCreateFromGLTexture3D = clCreateFromGLTexture3D;
    icdGlobalDispatchTable.clCreateFromGLRenderbuffer = clCreateFromGLRenderbuffer;
    icdGlobalDispatchTable.clGetGLObjectInfo = clGetGLObjectInfo;
    icdGlobalDispatchTable.clGetGLTextureInfo = clGetGLTextureInfo;
    icdGlobalDispatchTable.clEnqueueAcquireGLObjects = clEnqueueAcquireGLObjects;
    icdGlobalDispatchTable.clEnqueueReleaseGLObjects = clEnqueueReleaseGLObjects;
    icdGlobalDispatchTable.clCreateEventFromGLsyncKHR = clCreateEventFromGLsyncKHR;
    icdGlobalDispatchTable.clGetGLContextInfoKHR = clGetGLContextInfoKHR;
}

std::string GlSharingBuilderFactory::getExtensions(DriverInfo *driverInfo) {
    if (DebugManager.flags.AddClGlSharing.get()) {
        return "cl_khr_gl_sharing "
               "cl_khr_gl_depth_images "
               "cl_khr_gl_event "
               "cl_khr_gl_msaa_sharing ";
    } else if (GLSharingFunctionsLinux::isGlSharingEnabled()) {
        return "cl_khr_gl_sharing "
               "cl_khr_gl_depth_images "
               "cl_khr_gl_event "
               "cl_khr_gl_msaa_sharing ";
    }
    return "";
}

void *GlSharingBuilderFactory::getExtensionFunctionAddress(const std::string &functionName) {
    if (DebugManager.flags.EnableFormatQuery.get() &&
        functionName == "clGetSupportedGLTextureFormatsINTEL") {
        return ((void *)(clGetSupportedGLTextureFormatsINTEL));
    }

    return nullptr;
}

static SharingFactory::RegisterSharing<GlSharingBuilderFactory, GLSharingFunctionsLinux> glSharing;
} // namespace NEO
