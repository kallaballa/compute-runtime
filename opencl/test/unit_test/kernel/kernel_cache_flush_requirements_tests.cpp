/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"

#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"
#include "opencl/test/unit_test/mocks/mock_command_queue.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_program.h"

using namespace NEO;

TEST(KernelWithCacheFlushTests, givenDeviceWhichDoesntRequireCacheFlushWhenCheckIfKernelRequireFlushThenReturnedFalse) {
    auto device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    auto mockKernel = std::make_unique<MockKernelWithInternals>(*device);
    MockContext mockContext(device.get());
    MockCommandQueue queue;
    bool flushRequired = mockKernel->mockKernel->Kernel::requiresCacheFlushCommand(queue);
    EXPECT_FALSE(flushRequired);
}
