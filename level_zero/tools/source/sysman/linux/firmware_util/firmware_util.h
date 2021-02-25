/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "level_zero/core/source/device/device.h"

#ifdef IGSC_PRESENT
#include "igsc_lib.h"
#else
typedef struct igsc_device_info {
} igsc_device_info_t;
#endif
#include <string>
#include <vector>

namespace L0 {
class FirmwareUtil {
  public:
    static FirmwareUtil *create();
    virtual ze_result_t fwDeviceInit() = 0;
    virtual ze_result_t getFirstDevice(igsc_device_info *) = 0;
    virtual ze_result_t fwGetVersion(std::string &fwVersion) = 0;
    virtual ze_result_t opromGetVersion(std::string &fwVersion) = 0;
    virtual ze_result_t fwFlashGSC(void *pImage, uint32_t size) = 0;
    virtual ze_result_t fwFlashOprom(void *pImage, uint32_t size) = 0;
    virtual ~FirmwareUtil() = default;
};
} // namespace L0
