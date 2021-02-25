/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/compiler_options_parser.h"

#include <cstdint>
#include <sstream>

namespace NEO {

const std::string clStdOptionName = "-cl-std=CL";

uint32_t getMajorVersion(const std::string &compileOptions) {
    auto clStdValuePosition = compileOptions.find(clStdOptionName);
    if (clStdValuePosition == std::string::npos) {
        return 0;
    }
    std::stringstream ss{compileOptions.c_str() + clStdValuePosition + clStdOptionName.size()};
    uint32_t majorVersion;
    ss >> majorVersion;
    return majorVersion;
}

bool requiresOpenClCFeatures(const std::string &compileOptions) {
    return (getMajorVersion(compileOptions) >= 3);
}

bool requiresAdditionalExtensions(const std::string &compileOptions) {
    return (getMajorVersion(compileOptions) == 2);
}

} // namespace NEO
