/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/extensions/public/cl_ext_private.h"

#include "CL/cl.h"

#include <cstdint>
namespace NEO {
class ClDevice;
struct ClDeviceInfoParam;

namespace ClDeviceHelper {
void getExtraDeviceInfo(const ClDevice &clDevice, cl_device_info paramName, ClDeviceInfoParam &param, const void *&src, size_t &size, size_t &retSize);
cl_device_feature_capabilities_intel getExtraCapabilities();
}; // namespace ClDeviceHelper
} // namespace NEO
