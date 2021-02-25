/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/device_info.h"

#include "opencl/source/helpers/dispatch_info.h"
#include "opencl/test/unit_test/fixtures/device_host_queue_fixture.h"
#include "opencl/test/unit_test/gen_common/matchers.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_program.h"

using namespace NEO;
using namespace DeviceHostQueue;

using DeviceQueueSimpleTest = ::testing::Test;

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueSimpleTest, WhenExecutionModelDispatchIsSetupThenNoAdditionalActionsOccur) {
    DeviceQueue devQueue;
    char buffer[20];

    memset(buffer, 1, 20);

    size_t size = 20;
    IndirectHeap ssh(buffer, size);
    IndirectHeap dsh(buffer, size);
    devQueue.setupExecutionModelDispatch(ssh, dsh, nullptr, 0, 0, 0x123, 0, false);

    EXPECT_EQ(0u, ssh.getUsed());

    for (uint32_t i = 0; i < 20; i++) {
        EXPECT_EQ(1, buffer[i]);
    }
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueSimpleTest, WhenResettingDeviceQueueThenIndirectHeapIsNotUsed) {
    DeviceQueue devQueue;
    devQueue.resetDeviceQueue();
    EXPECT_EQ(nullptr, devQueue.getIndirectHeap(IndirectHeap::DYNAMIC_STATE));
}

class DeviceQueueTest : public DeviceHostQueueFixture<DeviceQueue> {
  public:
    using BaseClass = DeviceHostQueueFixture<DeviceQueue>;
    void SetUp() override {
        BaseClass::SetUp();
        REQUIRE_DEVICE_ENQUEUE_OR_SKIP(pContext);
        device = pContext->getDevice(0);

        ASSERT_NE(device, nullptr);
    }

    void TearDown() override {
        BaseClass::TearDown();
    }

    void checkQueueBuffer(cl_uint expedtedSize) {
        auto alignedExpectedSize = alignUp(expedtedSize, MemoryConstants::pageSize);
        EXPECT_EQ(deviceQueue->getQueueSize(), expedtedSize);
        ASSERT_NE(deviceQueue->getQueueBuffer(), nullptr);
        EXPECT_EQ(deviceQueue->getQueueBuffer()->getUnderlyingBufferSize(), alignedExpectedSize);
    }

    DeviceQueue *deviceQueue;
    ClDevice *device;
};

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueTest, GivenDeviceQueueCapWhenCreatingAdditionalDeviceQueuesThenQueueIsNotCreated) {
    auto maxOnDeviceQueues = device->getDeviceInfo().maxOnDeviceQueues;
    const_cast<ClDeviceInfo *>(&device->getDeviceInfo())->maxOnDeviceQueues = 1;

    auto deviceQueue1 = createQueueObject();
    ASSERT_NE(deviceQueue1, nullptr);
    EXPECT_EQ(deviceQueue1->getReference(), 1);

    auto deviceQueue2 = createQueueObject();
    EXPECT_EQ(deviceQueue2, nullptr);

    delete deviceQueue1;

    const_cast<ClDeviceInfo *>(&device->getDeviceInfo())->maxOnDeviceQueues = maxOnDeviceQueues;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueTest, GivenDeviceQueueWhenEventPoolIsCreatedThenTimestampResolutionIsSet) {
    auto timestampResolution = static_cast<float>(device->getProfilingTimerResolution());

    auto deviceQueue = std::unique_ptr<DeviceQueue>(createQueueObject());
    ASSERT_NE(deviceQueue, nullptr);

    auto eventPoolBuffer = reinterpret_cast<IGIL_EventPool *>(deviceQueue->getEventPoolBuffer()->getUnderlyingBuffer());

    EXPECT_FLOAT_EQ(timestampResolution, eventPoolBuffer->m_TimestampResolution);
}

typedef DeviceQueueTest DeviceQueueBuffer;

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueBuffer, GivenNoPropertyWhenCreatingQueueThenPreferredSizeIsSet) {
    auto &deviceInfo = device->getDeviceInfo();
    deviceQueue = createQueueObject(); // only minimal properties
    ASSERT_NE(deviceQueue, nullptr);
    checkQueueBuffer(deviceInfo.queueOnDevicePreferredSize);
    deviceQueue->release();
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueBuffer, GivenInvalidPropertyWhenCreatingQueueThenPreferredSizeIsSet) {
    cl_queue_properties properties[5] = {CL_QUEUE_PROPERTIES, deviceQueueProperties::minimumProperties[1],
                                         CL_QUEUE_SIZE, 0, 0};
    auto &deviceInfo = device->getDeviceInfo();

    deviceQueue = createQueueObject(properties); // zero size
    ASSERT_NE(deviceQueue, nullptr);

    checkQueueBuffer(deviceInfo.queueOnDevicePreferredSize);
    delete deviceQueue;

    properties[3] = static_cast<cl_queue_properties>(deviceInfo.queueOnDeviceMaxSize + 1);
    deviceQueue = createQueueObject(properties); // greater than max
    EXPECT_EQ(deviceQueue, nullptr);
    delete deviceQueue;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueBuffer, GivenValidSizeWhenCreatingQueueThenProvidedSizeIsSet) {
    auto &deviceInfo = device->getDeviceInfo();
    cl_uint validSize = deviceInfo.queueOnDevicePreferredSize - 1;
    cl_queue_properties properties[5] = {CL_QUEUE_PROPERTIES, deviceQueueProperties::minimumProperties[1],
                                         CL_QUEUE_SIZE, static_cast<cl_queue_properties>(validSize),
                                         0};

    EXPECT_NE(validSize, alignUp(validSize, MemoryConstants::pageSize)); // create aligned
    deviceQueue = createQueueObject(properties);
    ASSERT_NE(deviceQueue, nullptr);

    checkQueueBuffer(validSize);
    delete deviceQueue;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueBuffer, WhenDeviceQueueIsCreatedThenItIsCorrectlyInitialized) {
    auto &deviceInfo = device->getDeviceInfo();

    deviceQueue = createQueueObject();
    ASSERT_NE(deviceQueue, nullptr);

    IGIL_CommandQueue expectedIgilCmdQueue = getExpectedInitIgilCmdQueue(deviceQueue);
    EXPECT_EQ(static_cast<uint32_t>(deviceQueue->isProfilingEnabled()), expectedIgilCmdQueue.m_controls.m_IsProfilingEnabled);

    IGIL_EventPool expectedIgilEventPool = {0, 0, 0};
    expectedIgilEventPool.m_head = 0;
    expectedIgilEventPool.m_size = deviceInfo.maxOnDeviceEvents;
    expectedIgilEventPool.m_TimestampResolution = static_cast<float>(device->getProfilingTimerResolution());

    // initialized header
    EXPECT_EQ(0, memcmp(deviceQueue->getQueueBuffer()->getUnderlyingBuffer(),
                        &expectedIgilCmdQueue, sizeof(IGIL_CommandQueue)));

    EXPECT_EQ(0, memcmp(deviceQueue->getEventPoolBuffer()->getUnderlyingBuffer(),
                        &expectedIgilEventPool, sizeof(IGIL_EventPool)));

    delete deviceQueue;
}

typedef DeviceQueueTest DeviceQueueStackBuffer;

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueStackBuffer, WhenDeviceQueueIsCreatedThenAllocatedResourcesAreZeroed) {
    deviceQueue = createQueueObject();
    ASSERT_NE(deviceQueue, nullptr);

    EXPECT_THAT(deviceQueue->getQueueStorageBuffer()->getUnderlyingBuffer(), MemoryZeroed(deviceQueue->getQueueStorageBuffer()->getUnderlyingBufferSize()));
    EXPECT_THAT(deviceQueue->getStackBuffer()->getUnderlyingBuffer(), MemoryZeroed(deviceQueue->getStackBuffer()->getUnderlyingBufferSize()));
    delete deviceQueue;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueStackBuffer, WhenDeviceQueueIsCreatedThenStackBufferIsAllocated) {
    deviceQueue = createQueueObject();
    ASSERT_NE(deviceQueue, nullptr);

    auto maxEnqueue = deviceQueue->getQueueSize() / sizeof(IGIL_CommandHeader);
    //stack can hold at most 3 full loads of commands
    auto expectedStackSize = maxEnqueue * sizeof(uint32_t) * 3;
    expectedStackSize = alignUp(expectedStackSize, MemoryConstants::pageSize);

    ASSERT_NE(deviceQueue->getStackBuffer(), nullptr);
    EXPECT_EQ(deviceQueue->getStackBuffer()->getUnderlyingBufferSize(), expectedStackSize);
    delete deviceQueue;
}

typedef DeviceQueueTest DeviceQueueStorageBuffer;

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueStorageBuffer, WhenDeviceQueueIsCreatedThenStorageBufferIsAllocated) {
    deviceQueue = createQueueObject();
    ASSERT_NE(deviceQueue, nullptr);

    auto expectedStorageSize = deviceQueue->getQueueBuffer()->getUnderlyingBufferSize() * 2;
    expectedStorageSize = alignUp(expectedStorageSize, MemoryConstants::pageSize);

    ASSERT_NE(deviceQueue->getQueueStorageBuffer(), nullptr);
    EXPECT_EQ(deviceQueue->getQueueStorageBuffer()->getUnderlyingBufferSize(), expectedStorageSize);
    delete deviceQueue;
}

typedef DeviceQueueTest DefaultDeviceQueue;

HWCMDTEST_F(IGFX_GEN8_CORE, DefaultDeviceQueue, GivenSingleDeviceQueueIsSupportedWhenSecondDeviceQueueIsCreatedThenReuseDeviceQueue) {
    cl_queue_properties properties[] = {CL_QUEUE_PROPERTIES, CL_QUEUE_ON_DEVICE_DEFAULT, 0, 0, 0};

    auto maxOnDeviceQueues = device->getDeviceInfo().maxOnDeviceQueues;
    const_cast<ClDeviceInfo *>(&device->getDeviceInfo())->maxOnDeviceQueues = 1;

    auto deviceQueue1 = createQueueObject(properties);
    ASSERT_NE(deviceQueue1, nullptr);

    EXPECT_EQ(pContext->getDefaultDeviceQueue(), deviceQueue1);
    EXPECT_EQ(deviceQueue1->getReference(), 1);

    auto deviceQueue2 = createQueueObject(properties);
    ASSERT_NE(deviceQueue2, nullptr);

    EXPECT_EQ(deviceQueue2, deviceQueue1);

    EXPECT_EQ(pContext->getDefaultDeviceQueue(), deviceQueue1);
    EXPECT_EQ(deviceQueue1->getReference(), 2);

    deviceQueue1->release();
    deviceQueue2->release();

    const_cast<ClDeviceInfo *>(&device->getDeviceInfo())->maxOnDeviceQueues = maxOnDeviceQueues;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DefaultDeviceQueue, GivenMultipleDeviceQueuesIsSupportedWhenSecondDeviceQueueIsCreatedThenReuseDeviceQueue) {
    cl_queue_properties properties[] = {CL_QUEUE_PROPERTIES, CL_QUEUE_ON_DEVICE_DEFAULT, 0, 0, 0};

    auto maxOnDeviceQueues = device->getDeviceInfo().maxOnDeviceQueues;
    const_cast<ClDeviceInfo *>(&device->getDeviceInfo())->maxOnDeviceQueues = 2;

    auto deviceQueue1 = createQueueObject(properties);
    ASSERT_NE(deviceQueue1, nullptr);

    EXPECT_EQ(pContext->getDefaultDeviceQueue(), deviceQueue1);
    EXPECT_EQ(deviceQueue1->getReference(), 1);

    auto deviceQueue2 = createQueueObject(properties);
    ASSERT_NE(deviceQueue2, nullptr);

    EXPECT_EQ(deviceQueue2, deviceQueue1);

    EXPECT_EQ(pContext->getDefaultDeviceQueue(), deviceQueue1);
    EXPECT_EQ(deviceQueue1->getReference(), 2);

    deviceQueue1->release();
    deviceQueue2->release();

    const_cast<ClDeviceInfo *>(&device->getDeviceInfo())->maxOnDeviceQueues = maxOnDeviceQueues;
}

typedef DeviceQueueTest DeviceQueueEventPool;

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueEventPool, WhenDeviceQueueIsCreatedThenEventPoolBufferIsAllocated) {
    auto &deviceInfo = device->getDeviceInfo();

    // number of events + event pool representation
    auto expectedSize = static_cast<uint32_t>(deviceInfo.maxOnDeviceEvents * sizeof(IGIL_DeviceEvent) +
                                              sizeof(IGIL_EventPool));
    expectedSize = alignUp(expectedSize, MemoryConstants::pageSize);

    auto deviceQueue = createQueueObject();
    ASSERT_NE(deviceQueue, nullptr);

    ASSERT_NE(deviceQueue->getEventPoolBuffer(), nullptr);
    EXPECT_EQ(deviceQueue->getEventPoolBuffer()->getUnderlyingBufferSize(), expectedSize);

    delete deviceQueue;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueTest, WhenDeviceQueueIsCreatedThenDshBufferIsAllocated) {
    deviceQueue = createQueueObject();
    ASSERT_NE(deviceQueue, nullptr);

    ASSERT_NE(deviceQueue->getDshBuffer(), nullptr);
    auto dshBufferSize = deviceQueue->getDshBuffer()->getUnderlyingBufferSize();

    EXPECT_LE(761856u, dshBufferSize);
    delete deviceQueue;
}

HWCMDTEST_F(IGFX_GEN8_CORE, DeviceQueueTest, WhenDispatchingSchedulerThenNoAssertsOccur) {
    DeviceQueue devQueue;
    MockProgram program(toClDeviceVector(*device));
    MockCommandQueue cmdQ(nullptr, nullptr, 0);
    KernelInfo info;
    KernelInfoContainer kernelInfos;
    auto rootDeviceIndex = device->getRootDeviceIndex();
    kernelInfos.resize(rootDeviceIndex + 1);
    kernelInfos[rootDeviceIndex] = &info;
    MockSchedulerKernel *kernel = new MockSchedulerKernel(&program, kernelInfos);
    LinearStream cmdStream;

    devQueue.dispatchScheduler(cmdStream, *kernel, device->getPreemptionMode(), nullptr, nullptr, false);
    delete kernel;
}
