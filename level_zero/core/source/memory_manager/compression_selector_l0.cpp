/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/memory_manager/compression_selector.h"

namespace NEO {
bool CompressionSelector::preferRenderCompressedBuffer(const AllocationProperties &properties, const HardwareInfo &hwInfo) {
    bool preferredCompression = false;
    int32_t compressionEnabled = DebugManager.flags.EnableUsmCompression.get();
    if (compressionEnabled == 1) {
        if ((properties.allocationType == GraphicsAllocation::AllocationType::SVM_GPU) ||
            (properties.flags.isUSMDeviceAllocation)) {
            preferredCompression = true;
        }
    }
    return preferredCompression;
}
} // namespace NEO
