/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/hw_helper.h"

#include "shared/source/debug_settings/debug_settings_manager.h"

#include <algorithm>

namespace NEO {
HwHelper *hwHelperFactory[IGFX_MAX_CORE] = {};

HwHelper &HwHelper::get(GFXCORE_FAMILY gfxCore) {
    return *hwHelperFactory[gfxCore];
}

bool HwHelper::renderCompressedBuffersSupported(const HardwareInfo &hwInfo) {
    if (DebugManager.flags.RenderCompressedBuffersEnabled.get() != -1) {
        return !!DebugManager.flags.RenderCompressedBuffersEnabled.get();
    }
    return hwInfo.capabilityTable.ftrRenderCompressedBuffers;
}

bool HwHelper::renderCompressedImagesSupported(const HardwareInfo &hwInfo) {
    if (DebugManager.flags.RenderCompressedImagesEnabled.get() != -1) {
        return !!DebugManager.flags.RenderCompressedImagesEnabled.get();
    }
    return hwInfo.capabilityTable.ftrRenderCompressedImages;
}

bool HwHelper::cacheFlushAfterWalkerSupported(const HardwareInfo &hwInfo) {
    int32_t dbgFlag = DebugManager.flags.EnableCacheFlushAfterWalker.get();
    if (dbgFlag == 1) {
        return true;
    } else if (dbgFlag == 0) {
        return false;
    }
    return hwInfo.capabilityTable.supportCacheFlushAfterWalker;
}

uint32_t HwHelper::getMaxThreadsForVfe(const HardwareInfo &hwInfo) {
    uint32_t threadsPerEU = (hwInfo.gtSystemInfo.ThreadCount / hwInfo.gtSystemInfo.EUCount) + hwInfo.capabilityTable.extraQuantityThreadsPerEU;
    auto maxHwThreadsCapable = hwInfo.gtSystemInfo.EUCount * threadsPerEU;
    auto maxHwThreadsReturned = maxHwThreadsCapable;
    if (DebugManager.flags.MaxHwThreadsPercent.get() != 0) {
        maxHwThreadsReturned = int(maxHwThreadsCapable * (DebugManager.flags.MaxHwThreadsPercent.get() / 100.0f));
    }
    if (DebugManager.flags.MinHwThreadsUnoccupied.get() != 0) {
        maxHwThreadsReturned = std::min(maxHwThreadsReturned, maxHwThreadsCapable - DebugManager.flags.MinHwThreadsUnoccupied.get());
    }
    return maxHwThreadsReturned;
}

uint32_t HwHelper::getMaxThreadsForWorkgroup(const HardwareInfo &hwInfo, uint32_t maxNumEUsPerSubSlice) const {
    uint32_t numThreadsPerEU = hwInfo.gtSystemInfo.ThreadCount / hwInfo.gtSystemInfo.EUCount;
    return maxNumEUsPerSubSlice * numThreadsPerEU;
}
} // namespace NEO
