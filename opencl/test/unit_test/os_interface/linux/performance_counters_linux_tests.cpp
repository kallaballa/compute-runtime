/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/os_interface.h"

#include "opencl/source/os_interface/linux/performance_counters_linux.h"
#include "opencl/test/unit_test/os_interface/mock_performance_counters.h"

#include "gtest/gtest.h"

using namespace NEO;

struct PerformanceCountersLinuxTest : public PerformanceCountersFixture,
                                      public ::testing::Test {
  public:
    void SetUp() override {
        PerformanceCountersFixture::SetUp();
    }

    void TearDown() override {
        PerformanceCountersFixture::TearDown();
    }
};

TEST_F(PerformanceCountersLinuxTest, WhenCreatingPerformanceCountersThenDrmFileDescriptorIsPassed) {
    auto performanceCounters = PerformanceCounters::create(&device->getDevice());

    EXPECT_NE(nullptr, performanceCounters);
    EXPECT_NE(nullptr, performanceCounters.get());

    auto performanceCountersLinux = static_cast<PerformanceCountersLinux *>(performanceCounters.get());

    EXPECT_EQ(LinuxAdapterType::DrmFileDescriptor, performanceCountersLinux->adapter.Type);
    EXPECT_EQ(osInterface->get()->getDrm()->getFileDescriptor(), performanceCountersLinux->adapter.DrmFileDescriptor);
}
