/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/drm_neo.h"

#include "opencl/source/dll/linux/devices/device_ids.h"
#include "test.h"

#include <array>

using namespace NEO;

TEST(RklDeviceIdTest, supportedDeviceId) {
    std::array<DeviceDescriptor, 6> expectedDescriptors = {{{DEVICE_ID_4C80, &RKL_HW_CONFIG::hwInfo, &RKL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
                                                            {DEVICE_ID_4C8A, &RKL_HW_CONFIG::hwInfo, &RKL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
                                                            {DEVICE_ID_4C8B, &RKL_HW_CONFIG::hwInfo, &RKL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
                                                            {DEVICE_ID_4C8C, &RKL_HW_CONFIG::hwInfo, &RKL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT0_5},
                                                            {DEVICE_ID_4C90, &RKL_HW_CONFIG::hwInfo, &RKL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
                                                            {DEVICE_ID_4C9A, &RKL_HW_CONFIG::hwInfo, &RKL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1}}};

    auto compareStructs = [](const DeviceDescriptor *first, const DeviceDescriptor *second) {
        return first->deviceId == second->deviceId && first->pHwInfo == second->pHwInfo &&
               first->setupHardwareInfo == second->setupHardwareInfo && first->eGtType == second->eGtType;
    };

    size_t startIndex = 0;
    while (!compareStructs(&expectedDescriptors[0], &deviceDescriptorTable[startIndex]) &&
           deviceDescriptorTable[startIndex].deviceId != 0) {
        startIndex++;
    };
    EXPECT_NE(0u, deviceDescriptorTable[startIndex].deviceId);

    for (auto &expected : expectedDescriptors) {
        EXPECT_TRUE(compareStructs(&expected, &deviceDescriptorTable[startIndex]));
        startIndex++;
    }
}
