/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/os_library.h"

#include "GL/gl.h"

namespace Os {
extern const char *openglDllName;
}

namespace NEO {
class glFunctionHelper {
  public:
    glFunctionHelper(OsLibrary *glLibrary, const std::string &functionName) {
        glFunctionPtr = (*glLibrary)[functionName];
    }

    ConvertibleProcAddr operator[](const char *name) {
        return ConvertibleProcAddr{glFunctionPtr(name)};
    }

  protected:
    // clang-format off
    void* (*glFunctionPtr)(const char *Arg1) = 0;
    // clang-format on
};
}; // namespace NEO
