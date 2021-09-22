/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/source/sharings/gl/linux/gl_sharing_linux.h"
#include "opencl/source/sharings/sharing_factory.h"

#include <memory>

namespace NEO {
class Context;

struct GlCreateContextProperties {
    GLType GLHDCType = 0;
    GLContext GLHGLRCHandle = 0;
    GLDisplay GLHDCHandle = 0;
};

class GlSharingContextBuilder : public SharingContextBuilder {
  protected:
    std::unique_ptr<GlCreateContextProperties> contextData;

  public:
    bool processProperties(cl_context_properties &propertyType, cl_context_properties &propertyValue) override;
    bool finalizeProperties(Context &context, int32_t &errcodeRet) override;
};

class GlSharingBuilderFactory : public SharingBuilderFactory {
  public:
    std::unique_ptr<SharingContextBuilder> createContextBuilder() override;
    std::string getExtensions(DriverInfo *driverInfo) override;
    void fillGlobalDispatchTable() override;
    void *getExtensionFunctionAddress(const std::string &functionName) override;
};
} // namespace NEO
