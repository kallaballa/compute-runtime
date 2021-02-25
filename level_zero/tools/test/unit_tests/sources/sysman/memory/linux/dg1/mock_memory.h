/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/os_interface/linux/memory_info_impl.h"

#include "level_zero/core/test/unit_tests/mock.h"
#include "level_zero/core/test/unit_tests/mocks/mock_memory_manager.h"
#include "level_zero/tools/source/sysman/memory/linux/dg1/os_memory_imp.h"
#include "level_zero/tools/source/sysman/memory/memory_imp.h"

#include "sysman/linux/os_sysman_imp.h"

using namespace NEO;
constexpr uint64_t probedSizeRegionZero = 8 * GB;
constexpr uint64_t probedSizeRegionOne = 16 * GB;
constexpr uint64_t probedSizeRegionTwo = 4 * GB;
constexpr uint64_t probedSizeRegionThree = 16 * GB;
constexpr uint64_t unallocatedSizeRegionZero = 6 * GB;
constexpr uint64_t unallocatedSizeRegionOne = 12 * GB;
constexpr uint64_t unallocatedSizeRegionTwo = 25 * GB;
constexpr uint64_t unallocatedSizeRegionThree = 3 * GB;
namespace L0 {
namespace ult {

struct MockMemoryManagerSysman : public MemoryManagerMock {
    MockMemoryManagerSysman(NEO::ExecutionEnvironment &executionEnvironment) : MemoryManagerMock(const_cast<NEO::ExecutionEnvironment &>(executionEnvironment)) {}
};

class MemoryNeoDrm : public Drm {
  public:
    using Drm::memoryInfo;
    const int mockFd = 33;
    MemoryNeoDrm(RootDeviceEnvironment &rootDeviceEnvironment) : Drm(std::make_unique<HwDeviceId>(mockFd, ""), rootDeviceEnvironment) {}
};

template <>
struct Mock<MemoryNeoDrm> : public MemoryNeoDrm {
    Mock<MemoryNeoDrm>(RootDeviceEnvironment &rootDeviceEnvironment) : MemoryNeoDrm(rootDeviceEnvironment) {}
    bool queryMemoryInfoMockPositiveTest() {
        drm_i915_memory_region_info regionInfo[2] = {};
        regionInfo[0].region = {I915_MEMORY_CLASS_SYSTEM, 0};
        regionInfo[0].probed_size = probedSizeRegionZero;
        regionInfo[0].unallocated_size = unallocatedSizeRegionZero;
        regionInfo[1].region = {I915_MEMORY_CLASS_DEVICE, 0};
        regionInfo[1].probed_size = probedSizeRegionOne;
        regionInfo[1].unallocated_size = unallocatedSizeRegionOne;

        this->memoryInfo.reset(new MemoryInfoImpl(regionInfo, 2));
        return true;
    }

    bool queryMemoryInfoMockReturnFalse() {
        return false;
    }

    bool queryMemoryInfoMockReturnFakeTrue() {
        return true;
    }

    MOCK_METHOD(bool, queryMemoryInfo, (), (override));
};

} // namespace ult
} // namespace L0
