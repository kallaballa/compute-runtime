/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/drm_neo.h"

#include "test.h"

#include <array>

using namespace NEO;

TEST(EhlDeviceIdTest, GivenSpportedDeviceIdWhenCheckingHwSetupThenItIsCorrect) {
    std::array<DeviceDescriptor, 9> expectedDescriptors = {{
        {0x4500, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4541, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4551, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4571, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4555, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4E51, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4E61, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4E71, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
        {0x4E55, &EHL_HW_CONFIG::hwInfo, &EHL_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1},
    }};

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
