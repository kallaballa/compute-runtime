/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/mocks/mock_device.h"
#include "shared/test/common/test_macros/test_checks_shared.h"

#include "opencl/source/program/printf_handler.h"
#include "opencl/test/unit_test/fixtures/multi_root_device_fixture.h"
#include "opencl/test/unit_test/mocks/mock_cl_device.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_mdi.h"
#include "opencl/test/unit_test/mocks/mock_program.h"

#include "gtest/gtest.h"

using namespace NEO;

TEST(PrintfHandlerTest, givenNotPreparedPrintfHandlerWhenGetSurfaceIsCalledThenResultIsNullptr) {
    MockClDevice *device = new MockClDevice{MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr)};
    MockContext context;

    auto pKernelInfo = std::make_unique<KernelInfo>();
    pKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = KernelDescriptor::Stateless;

    SPatchAllocateStatelessPrintfSurface printfSurface = {};
    printfSurface.DataParamOffset = 0;
    printfSurface.DataParamSize = 8;
    populateKernelDescriptor(pKernelInfo->kernelDescriptor, printfSurface);

    MockProgram *pProgram = new MockProgram(&context, false, toClDeviceVector(*device));
    MockKernel *pKernel = new MockKernel(pProgram, MockKernel::toKernelInfoContainer(*pKernelInfo, device->getRootDeviceIndex()));

    MockMultiDispatchInfo multiDispatchInfo(device, pKernel);
    PrintfHandler *printfHandler = PrintfHandler::create(multiDispatchInfo, *device);

    EXPECT_EQ(nullptr, printfHandler->getSurface());

    delete printfHandler;
    delete pKernel;

    delete pProgram;
    delete device;
}

TEST(PrintfHandlerTest, givenPreparedPrintfHandlerWhenGetSurfaceIsCalledThenResultIsNullptr) {
    MockClDevice *device = new MockClDevice{MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr)};
    MockContext context;

    auto pKernelInfo = std::make_unique<KernelInfo>();
    pKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = KernelDescriptor::Stateless;

    SPatchAllocateStatelessPrintfSurface printfSurface = {};
    printfSurface.DataParamOffset = 0;
    printfSurface.DataParamSize = 8;
    populateKernelDescriptor(pKernelInfo->kernelDescriptor, printfSurface);

    MockProgram *pProgram = new MockProgram(&context, false, toClDeviceVector(*device));

    uint64_t crossThread[10];
    MockKernel *pKernel = new MockKernel(pProgram,
                                         MockKernel::toKernelInfoContainer(*pKernelInfo, device->getRootDeviceIndex()));
    pKernel->setCrossThreadData(&crossThread, sizeof(uint64_t) * 8);

    MockMultiDispatchInfo multiDispatchInfo(device, pKernel);
    PrintfHandler *printfHandler = PrintfHandler::create(multiDispatchInfo, *device);
    printfHandler->prepareDispatch(multiDispatchInfo);
    EXPECT_NE(nullptr, printfHandler->getSurface());

    delete printfHandler;
    delete pKernel;

    delete pProgram;
    delete device;
}

TEST(PrintfHandlerTest, givenParentKernelWihoutPrintfAndBlockKernelWithPrintfWhenPrintfHandlerCreateCalledThenResaultIsAnObject) {

    auto device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    MockContext context(device.get());
    std::unique_ptr<MockParentKernel> parentKernelWithoutPrintf(MockParentKernel::create(context, false, false, false, false));

    MockMultiDispatchInfo multiDispatchInfo(device.get(), parentKernelWithoutPrintf.get());

    std::unique_ptr<PrintfHandler> printfHandler(PrintfHandler::create(multiDispatchInfo, *device));

    ASSERT_NE(nullptr, printfHandler.get());
}

TEST(PrintfHandlerTest, givenParentKernelAndBlockKernelWithoutPrintfWhenPrintfHandlerCreateCalledThenResaultIsNullptr) {

    auto device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    MockContext context(device.get());
    std::unique_ptr<MockParentKernel> blockKernelWithoutPrintf(MockParentKernel::create(context, false, false, false, false, false));

    MockMultiDispatchInfo multiDispatchInfo(device.get(), blockKernelWithoutPrintf.get());

    std::unique_ptr<PrintfHandler> printfHandler(PrintfHandler::create(multiDispatchInfo, *device));

    ASSERT_EQ(nullptr, printfHandler.get());
}
TEST(PrintfHandlerTest, givenParentKernelWithPrintfAndBlockKernelWithoutPrintfWhenPrintfHandlerCreateCalledThenResaultIsAnObject) {

    auto device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    MockContext context(device.get());
    std::unique_ptr<MockParentKernel> parentKernelWithPrintfBlockKernelWithoutPrintf(MockParentKernel::create(context, false, false, false, true, false));

    MockMultiDispatchInfo multiDispatchInfo(device.get(), parentKernelWithPrintfBlockKernelWithoutPrintf.get());

    std::unique_ptr<PrintfHandler> printfHandler(PrintfHandler::create(multiDispatchInfo, *device));

    ASSERT_NE(nullptr, printfHandler);
}

TEST(PrintfHandlerTest, givenMultiDispatchInfoWithMultipleKernelsWhenCreatingAndDispatchingPrintfHandlerThenPickMainKernel) {
    MockContext context;
    auto device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    auto program = std::make_unique<MockProgram>(&context, false, toClDeviceVector(*device));

    auto pMainKernelInfo = std::make_unique<KernelInfo>();
    pMainKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = KernelDescriptor::Stateless;

    auto pKernelInfo = std::make_unique<KernelInfo>();
    pKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = KernelDescriptor::Stateless;

    SPatchAllocateStatelessPrintfSurface printfSurface = {};
    printfSurface.DataParamOffset = 0;
    printfSurface.DataParamSize = 8;
    populateKernelDescriptor(pMainKernelInfo->kernelDescriptor, printfSurface);

    auto mainKernel = std::make_unique<MockKernel>(program.get(), MockKernel::toKernelInfoContainer(*pMainKernelInfo, device->getRootDeviceIndex()));
    auto kernel1 = std::make_unique<MockKernel>(program.get(), MockKernel::toKernelInfoContainer(*pKernelInfo, device->getRootDeviceIndex()));
    auto kernel2 = std::make_unique<MockKernel>(program.get(), MockKernel::toKernelInfoContainer(*pKernelInfo, device->getRootDeviceIndex()));

    uint64_t crossThread[8];
    mainKernel->setCrossThreadData(&crossThread, sizeof(uint64_t) * 8);

    DispatchInfo mainDispatchInfo(device.get(), mainKernel.get(), 1, {1, 1, 1}, {1, 1, 1}, {1, 1, 1});
    DispatchInfo dispatchInfo1(device.get(), kernel1.get(), 1, {1, 1, 1}, {1, 1, 1}, {1, 1, 1});
    DispatchInfo dispatchInfo2(device.get(), kernel2.get(), 1, {1, 1, 1}, {1, 1, 1}, {1, 1, 1});

    MultiDispatchInfo multiDispatchInfo(mainKernel.get());
    multiDispatchInfo.push(dispatchInfo1);
    multiDispatchInfo.push(mainDispatchInfo);
    multiDispatchInfo.push(dispatchInfo2);

    std::unique_ptr<PrintfHandler> printfHandler(PrintfHandler::create(multiDispatchInfo, *device));
    ASSERT_NE(nullptr, printfHandler.get());

    printfHandler->prepareDispatch(multiDispatchInfo);
    EXPECT_NE(nullptr, printfHandler->getSurface());
}

TEST(PrintfHandlerTest, GivenEmptyMultiDispatchInfoWhenCreatingPrintfHandlerThenPrintfHandlerIsNotCreated) {
    MockClDevice device{new MockDevice};
    MockKernelWithInternals mockKernelWithInternals{device};
    MockMultiDispatchInfo multiDispatchInfo{&device, mockKernelWithInternals.mockKernel};
    multiDispatchInfo.dispatchInfos.resize(0);
    EXPECT_EQ(nullptr, multiDispatchInfo.peekMainKernel());

    auto printfHandler = PrintfHandler::create(multiDispatchInfo, device);
    EXPECT_EQ(nullptr, printfHandler);
}

TEST(PrintfHandlerTest, GivenAllocationInLocalMemoryWhichRequiresBlitterWhenPreparingPrintfSurfaceDispatchThenBlitterIsUsed) {
    REQUIRE_BLITTER_OR_SKIP(defaultHwInfo.get());

    DebugManagerStateRestore restorer;

    auto hwInfo = *defaultHwInfo;
    hwInfo.capabilityTable.blitterOperationsSupported = true;

    uint32_t blitsCounter = 0;
    uint32_t expectedBlitsCount = 0;
    auto mockBlitMemoryToAllocation = [&blitsCounter](const Device &device, GraphicsAllocation *memory, size_t offset, const void *hostPtr,
                                                      Vec3<size_t> size) -> BlitOperationResult {
        blitsCounter++;
        return BlitOperationResult::Success;
    };
    VariableBackup<BlitHelperFunctions::BlitMemoryToAllocationFunc> blitMemoryToAllocationFuncBackup{
        &BlitHelperFunctions::blitMemoryToAllocation, mockBlitMemoryToAllocation};

    LocalMemoryAccessMode localMemoryAccessModes[] = {
        LocalMemoryAccessMode::Default,
        LocalMemoryAccessMode::CpuAccessAllowed,
        LocalMemoryAccessMode::CpuAccessDisallowed};

    for (auto localMemoryAccessMode : localMemoryAccessModes) {
        DebugManager.flags.ForceLocalMemoryAccessMode.set(static_cast<int32_t>(localMemoryAccessMode));
        for (auto isLocalMemorySupported : ::testing::Bool()) {
            DebugManager.flags.EnableLocalMemory.set(isLocalMemorySupported);
            auto pClDevice = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(&hwInfo));
            MockContext context{pClDevice.get()};

            auto pKernelInfo = std::make_unique<KernelInfo>();
            pKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = KernelDescriptor::Stateless;

            SPatchAllocateStatelessPrintfSurface printfSurface = {};
            printfSurface.DataParamOffset = 0;
            printfSurface.DataParamSize = 8;
            populateKernelDescriptor(pKernelInfo->kernelDescriptor, printfSurface);

            auto program = std::make_unique<MockProgram>(&context, false, toClDeviceVector(*pClDevice));
            uint64_t crossThread[10];
            auto kernel = std::make_unique<MockKernel>(program.get(), MockKernel::toKernelInfoContainer(*pKernelInfo, pClDevice->getRootDeviceIndex()));
            kernel->setCrossThreadData(&crossThread, sizeof(uint64_t) * 8);

            MockMultiDispatchInfo multiDispatchInfo(pClDevice.get(), kernel.get());
            std::unique_ptr<PrintfHandler> printfHandler(PrintfHandler::create(multiDispatchInfo, *pClDevice));
            printfHandler->prepareDispatch(multiDispatchInfo);

            if (printfHandler->getSurface()->isAllocatedInLocalMemoryPool() &&
                (localMemoryAccessMode == LocalMemoryAccessMode::CpuAccessDisallowed)) {
                expectedBlitsCount++;
            }
            EXPECT_EQ(expectedBlitsCount, blitsCounter);
        }
    }
}

using PrintfHandlerMultiRootDeviceTests = MultiRootDeviceFixture;

TEST_F(PrintfHandlerMultiRootDeviceTests, GivenPrintfSurfaceThenItHasCorrectRootDeviceIndex) {
    auto pKernelInfo = std::make_unique<KernelInfo>();
    pKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = KernelDescriptor::Stateless;

    SPatchAllocateStatelessPrintfSurface printfSurface = {};
    printfSurface.DataParamOffset = 0;
    printfSurface.DataParamSize = 8;
    populateKernelDescriptor(pKernelInfo->kernelDescriptor, printfSurface);

    auto program = std::make_unique<MockProgram>(context.get(), false, toClDeviceVector(*device1));

    uint64_t crossThread[10];
    auto kernel = std::make_unique<MockKernel>(program.get(), MockKernel::toKernelInfoContainer(*pKernelInfo, device1->getRootDeviceIndex()));
    kernel->setCrossThreadData(&crossThread, sizeof(uint64_t) * 8);

    MockMultiDispatchInfo multiDispatchInfo(device1, kernel.get());
    std::unique_ptr<PrintfHandler> printfHandler(PrintfHandler::create(multiDispatchInfo, *device1));
    printfHandler->prepareDispatch(multiDispatchInfo);
    auto surface = printfHandler->getSurface();

    ASSERT_NE(nullptr, surface);
    EXPECT_EQ(expectedRootDeviceIndex, surface->getRootDeviceIndex());
}
