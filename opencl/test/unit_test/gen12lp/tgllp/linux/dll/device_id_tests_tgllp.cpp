/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/drm_neo.h"

#include "test.h"

#include <array>

using namespace NEO;

TEST(TglLpDeviceIdTest, GivenSupportedDeviceIdThenHardwareInfoIsCorrect) {
    std::array<DeviceDescriptor, 8> expectedDescriptors = {{
        {0xFF20, &TGLLP_1x6x16::hwInfo, &TGLLP_1x6x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A49, &TGLLP_1x6x16::hwInfo, &TGLLP_1x6x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A40, &TGLLP_1x6x16::hwInfo, &TGLLP_1x6x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A59, &TGLLP_1x6x16::hwInfo, &TGLLP_1x6x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A60, &TGLLP_1x2x16::hwInfo, &TGLLP_1x2x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A68, &TGLLP_1x2x16::hwInfo, &TGLLP_1x2x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A70, &TGLLP_1x2x16::hwInfo, &TGLLP_1x2x16::setupHardwareInfo, GTTYPE_GT2},
        {0x9A78, &TGLLP_1x2x16::hwInfo, &TGLLP_1x2x16::setupHardwareInfo, GTTYPE_GT2},
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
