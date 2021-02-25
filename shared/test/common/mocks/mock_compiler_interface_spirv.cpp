/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/mocks/mock_compiler_interface_spirv.h"

#include "shared/source/helpers/file_io.h"
#include "shared/test/common/helpers/test_files.h"

#include "opencl/test/unit_test/global_environment.h"
#include "opencl/test/unit_test/helpers/kernel_binary_helper.h"

namespace NEO {
TranslationOutput::ErrorCode MockCompilerInterfaceSpirv::compile(const NEO::Device &device, const TranslationInput &input, TranslationOutput &output) {
    std::string kernelName;
    retrieveBinaryKernelFilename(kernelName, KernelBinaryHelper::BUILT_INS + "_", ".gen");

    size_t size = 0;
    auto src = loadDataFromFile(
        kernelName.c_str(),
        size);
    output.deviceBinary.mem = std::move(src);
    output.deviceBinary.size = size;
    output.intermediateCodeType = IGC::CodeType::spirV;

    return TranslationOutput::ErrorCode::Success;
}
TranslationOutput::ErrorCode MockCompilerInterfaceSpirv::build(const NEO::Device &device, const TranslationInput &input, TranslationOutput &out) {
    return this->compile(device, input, out);
}
} // namespace NEO