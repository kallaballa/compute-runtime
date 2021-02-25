/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/hw_info_config.h"

namespace NEO {

template <>
int HwInfoConfigHw<IGFX_DG1>::configureHardwareCustom(HardwareInfo *hwInfo, OSInterface *osIface) {
    GT_SYSTEM_INFO *gtSystemInfo = &hwInfo->gtSystemInfo;
    gtSystemInfo->SliceCount = 1;

    enableBlitterOperationsSupport(hwInfo);

    hwInfo->featureTable.ftrGpGpuMidThreadLevelPreempt = false;
    return 0;
}

template class HwInfoConfigHw<IGFX_DG1>;
} // namespace NEO
