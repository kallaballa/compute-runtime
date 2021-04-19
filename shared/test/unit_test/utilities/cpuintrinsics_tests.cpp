/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/utilities/cpuintrinsics.h"

#include "gtest/gtest.h"

#include <atomic>
#include <cstdint>

namespace CpuIntrinsicsTests {
extern std::atomic<uintptr_t> lastClFlushedPtr;
extern std::atomic<uint32_t> pauseCounter;
} // namespace CpuIntrinsicsTests

TEST(CpuIntrinsicsTest, whenClFlushIsCalledThenExpectToPassPtrToSystemCall) {
    uintptr_t flushAddr = 0x1234;
    void const *ptr = reinterpret_cast<void const *>(flushAddr);
    NEO::CpuIntrinsics::clFlush(ptr);
    EXPECT_EQ(flushAddr, CpuIntrinsicsTests::lastClFlushedPtr);
}

TEST(CpuIntrinsicsTest, whenPauseCalledThenExpectToIncreaseCounter) {
    uint32_t oldCount = CpuIntrinsicsTests::pauseCounter.load();
    NEO::CpuIntrinsics::pause();
    EXPECT_EQ(oldCount + 1, CpuIntrinsicsTests::pauseCounter);
}
