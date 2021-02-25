/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/test/common/helpers/debug_manager_state_restore.h"

#include "opencl/test/unit_test/fixtures/platform_fixture.h"
#include "opencl/test/unit_test/mocks/mock_command_queue.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

class ScenarioTest : public ::testing::Test,
                     public PlatformFixture {
    using PlatformFixture::SetUp;

  protected:
    void SetUp() override {
        DebugManager.flags.EnableTimestampPacket.set(false);
        PlatformFixture::SetUp();

        auto pDevice = pPlatform->getClDevice(0);
        ASSERT_NE(nullptr, pDevice);

        cl_device_id clDevice = pDevice;
        context = Context::create<MockContext>(nullptr, ClDeviceVector(&clDevice, 1), nullptr, nullptr, retVal);
        commandQueue = new MockCommandQueue(context, pDevice, 0);
        program = new MockProgram(context, false, toClDeviceVector(*pDevice));

        kernelInternals = new MockKernelWithInternals(*pDevice, context);
        kernel = kernelInternals->mockKernel;

        ASSERT_NE(nullptr, kernel);
    }
    void TearDown() override {
        delete kernelInternals;
        delete commandQueue;
        context->release();
        program->release();

        PlatformFixture::TearDown();
    }

    cl_int retVal;
    DebugManagerStateRestore dbgRestorer;
    MockCommandQueue *commandQueue = nullptr;
    MockContext *context = nullptr;
    MockKernelWithInternals *kernelInternals = nullptr;
    MockKernel *kernel = nullptr;
    MockProgram *program = nullptr;
};
