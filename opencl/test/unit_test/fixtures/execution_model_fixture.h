/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/test/common/helpers/debug_manager_state_restore.h"

#include "opencl/source/device_queue/device_queue.h"
#include "opencl/test/unit_test/command_queue/command_queue_fixture.h"
#include "opencl/test/unit_test/fixtures/execution_model_kernel_fixture.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/test_macros/test_checks_ocl.h"

class DeviceQueueFixture {
  public:
    void SetUp(Context *context, ClDevice *device) {
        REQUIRE_DEVICE_ENQUEUE_OR_SKIP(device);
        cl_int errcodeRet = 0;
        cl_queue_properties properties[3];

        properties[0] = CL_QUEUE_PROPERTIES;
        properties[1] = CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;
        properties[2] = 0;

        ASSERT_NE(nullptr, context);
        ASSERT_NE(nullptr, device);

        pDevQueue = DeviceQueue::create(context, device,
                                        properties[0],
                                        errcodeRet);

        ASSERT_NE(nullptr, pDevQueue);

        auto devQueue = context->getDefaultDeviceQueue();

        ASSERT_NE(nullptr, devQueue);
        EXPECT_EQ(pDevQueue, devQueue);
    }

    void TearDown() {
        delete pDevQueue;
    }

    DeviceQueue *pDevQueue = nullptr;
};

class ExecutionModelKernelTest : public ExecutionModelKernelFixture,
                                 public CommandQueueHwFixture,
                                 public DeviceQueueFixture,
                                 public ::testing::WithParamInterface<std::tuple<const char *, const char *>> {
  public:
    void SetUp() override {
        REQUIRE_DEVICE_ENQUEUE_OR_SKIP(defaultHwInfo);

        DebugManager.flags.EnableTimestampPacket.set(0);
        ExecutionModelKernelFixture::SetUp(std::get<0>(GetParam()), std::get<1>(GetParam()));
        CommandQueueHwFixture::SetUp(pClDevice, 0);
        DeviceQueueFixture::SetUp(context, pClDevice);
    }

    void TearDown() override {
        if (!IsSkipped()) {
            DeviceQueueFixture::TearDown();
            CommandQueueHwFixture::TearDown();
            ExecutionModelKernelFixture::TearDown();
        }
    }

    std::unique_ptr<KernelOperation> createBlockedCommandsData(CommandQueue &commandQueue) {
        auto commandStream = new LinearStream();

        auto &gpgpuCsr = commandQueue.getGpgpuCommandStreamReceiver();
        gpgpuCsr.ensureCommandBufferAllocation(*commandStream, 1, 1);

        return std::make_unique<KernelOperation>(commandStream, *gpgpuCsr.getInternalAllocationStorage());
    }

    DebugManagerStateRestore dbgRestore;
};

class ExecutionModelSchedulerTest : public ClDeviceFixture,
                                    public CommandQueueHwFixture,
                                    public DeviceQueueFixture {
  public:
    void SetUp() override {
        ClDeviceFixture::SetUp();
        CommandQueueHwFixture::SetUp(pClDevice, 0);
        DeviceQueueFixture::SetUp(context, pClDevice);

        parentKernel = MockParentKernel::create(*context);
        ASSERT_NE(nullptr, parentKernel);
    }

    void TearDown() override {
        parentKernel->release();

        DeviceQueueFixture::TearDown();
        CommandQueueHwFixture::TearDown();
        ClDeviceFixture::TearDown();
    }

    MockParentKernel *parentKernel = nullptr;
};

struct ParentKernelCommandQueueFixture : public CommandQueueHwFixture,
                                         testing::Test {

    void SetUp() override {
        device = new MockClDevice{MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr, rootDeviceIndex)};
        CommandQueueHwFixture::SetUp(device, 0);
    }
    void TearDown() override {
        CommandQueueHwFixture::TearDown();
        delete device;
    }

    std::unique_ptr<KernelOperation> createBlockedCommandsData(CommandQueue &commandQueue) {
        auto commandStream = new LinearStream();

        auto &gpgpuCsr = commandQueue.getGpgpuCommandStreamReceiver();
        gpgpuCsr.ensureCommandBufferAllocation(*commandStream, 1, 1);

        return std::make_unique<KernelOperation>(commandStream, *gpgpuCsr.getInternalAllocationStorage());
    }
    const uint32_t rootDeviceIndex = 0u;
};
