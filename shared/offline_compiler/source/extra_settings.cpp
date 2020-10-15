/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/offline_compiler/source/offline_compiler.h"
#include "shared/source/os_interface/os_library.h"

#include "compiler_options.h"

namespace NEO {
void OfflineCompiler::resolveExtraSettings() {
    if (deviceName == "rkl") {
        CompilerOptions::concatenateAppend(internalOptions, CompilerOptions::forceEmuInt32DivRemSP);
    }
}
} // namespace NEO
