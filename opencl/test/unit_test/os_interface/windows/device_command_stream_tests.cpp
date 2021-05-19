/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/command_stream/device_command_stream.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/command_stream/preemption.h"
#include "shared/source/direct_submission/dispatchers/render_dispatcher.h"
#include "shared/source/direct_submission/windows/wddm_direct_submission.h"
#include "shared/source/helpers/flush_stamp.h"
#include "shared/source/helpers/windows/gmm_callbacks.h"
#include "shared/source/memory_manager/internal_allocation_storage.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/windows/os_context_win.h"
#include "shared/source/os_interface/windows/os_interface.h"
#include "shared/source/os_interface/windows/wddm_memory_manager.h"
#include "shared/source/os_interface/windows/wddm_memory_operations_handler.h"
#include "shared/source/os_interface/windows/wddm_residency_controller.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/dispatch_flags_helper.h"
#include "shared/test/common/mocks/mock_device.h"
#include "shared/test/common/mocks/mock_graphics_allocation.h"
#include "shared/test/common/mocks/windows/mock_wddm_direct_submission.h"
#include "shared/test/unit_test/os_interface/windows/mock_gdi_interface.h"

#include "opencl/source/command_stream/aub_command_stream_receiver.h"
#include "opencl/source/command_stream/command_stream_receiver_with_aub_dump.h"
#include "opencl/source/mem_obj/buffer.h"
#include "opencl/source/os_interface/windows/wddm_device_command_stream.h"
#include "opencl/source/platform/platform.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/memory_management_fixture.h"
#include "opencl/test/unit_test/fixtures/mock_aub_center_fixture.h"
#include "opencl/test/unit_test/helpers/execution_environment_helper.h"
#include "opencl/test/unit_test/mocks/mock_buffer.h"
#include "opencl/test/unit_test/mocks/mock_builtins.h"
#include "opencl/test/unit_test/mocks/mock_gmm_page_table_mngr.h"
#include "opencl/test/unit_test/mocks/mock_io_functions.h"
#include "opencl/test/unit_test/mocks/mock_platform.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "opencl/test/unit_test/mocks/mock_submissions_aggregator.h"
#include "opencl/test/unit_test/mocks/mock_wddm_interface23.h"
#include "opencl/test/unit_test/os_interface/windows/mock_wddm_memory_manager.h"
#include "opencl/test/unit_test/os_interface/windows/wddm_fixture.h"
#include "test.h"

#include "hw_cmds.h"

using namespace NEO;
using namespace ::testing;

class WddmCommandStreamFixture {
  public:
    std::unique_ptr<MockDevice> device;
    DeviceCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME> *csr;
    MockWddmMemoryManager *memoryManager = nullptr;
    WddmMock *wddm = nullptr;

    DebugManagerStateRestore stateRestore;

    virtual void SetUp() {
        HardwareInfo *hwInfo = nullptr;
        DebugManager.flags.CsrDispatchMode.set(static_cast<uint32_t>(DispatchMode::ImmediateDispatch));
        auto executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);

        memoryManager = new MockWddmMemoryManager(*executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);
        wddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());
        device.reset(MockDevice::create<MockDevice>(executionEnvironment, 0u));
        ASSERT_NE(nullptr, device);

        csr = new WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME>(*executionEnvironment, 0, device->getDeviceBitfield());
        device->resetCommandStreamReceiver(csr);
        csr->getOsContext().ensureContextInitialized();
    }

    virtual void TearDown() {
    }
};

template <typename GfxFamily>
struct MockWddmCsr : public WddmCommandStreamReceiver<GfxFamily> {
    using CommandStreamReceiver::clearColorAllocation;
    using CommandStreamReceiver::commandStream;
    using CommandStreamReceiver::dispatchMode;
    using CommandStreamReceiver::getCS;
    using CommandStreamReceiver::globalFenceAllocation;
    using CommandStreamReceiver::useGpuIdleImplicitFlush;
    using CommandStreamReceiver::useNewResourceImplicitFlush;
    using CommandStreamReceiverHw<GfxFamily>::blitterDirectSubmission;
    using CommandStreamReceiverHw<GfxFamily>::directSubmission;
    using WddmCommandStreamReceiver<GfxFamily>::commandBufferHeader;
    using WddmCommandStreamReceiver<GfxFamily>::initDirectSubmission;
    using WddmCommandStreamReceiver<GfxFamily>::WddmCommandStreamReceiver;

    void overrideDispatchPolicy(DispatchMode overrideValue) {
        this->dispatchMode = overrideValue;
    }

    SubmissionAggregator *peekSubmissionAggregator() {
        return this->submissionAggregator.get();
    }

    void overrideSubmissionAggregator(SubmissionAggregator *newSubmissionsAggregator) {
        this->submissionAggregator.reset(newSubmissionsAggregator);
    }

    void overrideRecorededCommandBuffer(Device &device) {
        recordedCommandBuffer = std::unique_ptr<CommandBuffer>(new CommandBuffer(device));
    }

    bool initDirectSubmission(Device &device, OsContext &osContext) override {
        if (callParentInitDirectSubmission) {
            return WddmCommandStreamReceiver<GfxFamily>::initDirectSubmission(device, osContext);
        }
        bool ret = true;
        if (DebugManager.flags.EnableDirectSubmission.get() == 1) {
            if (!initBlitterDirectSubmission) {
                directSubmission = std::make_unique<
                    MockWddmDirectSubmission<GfxFamily, RenderDispatcher<GfxFamily>>>(device, osContext);
                ret = directSubmission->initialize(true);
                this->dispatchMode = DispatchMode::ImmediateDispatch;
            } else {
                blitterDirectSubmission = std::make_unique<
                    MockWddmDirectSubmission<GfxFamily, BlitterDispatcher<GfxFamily>>>(device, osContext);
                blitterDirectSubmission->initialize(true);
            }
        }
        return ret;
    }

    int flushCalledCount = 0;
    std::unique_ptr<CommandBuffer> recordedCommandBuffer = nullptr;

    bool callParentInitDirectSubmission = true;
    bool initBlitterDirectSubmission = false;
};

class WddmCommandStreamWithMockGdiFixture {
  public:
    MockWddmCsr<DEFAULT_TEST_FAMILY_NAME> *csr = nullptr;
    MemoryManager *memoryManager = nullptr;
    std::unique_ptr<MockDevice> device = nullptr;
    WddmMock *wddm = nullptr;
    MockGdi *gdi = nullptr;
    DebugManagerStateRestore stateRestore;
    GraphicsAllocation *preemptionAllocation = nullptr;

    virtual void SetUp() {
        HardwareInfo *hwInfo = nullptr;
        ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);
        wddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());
        gdi = new MockGdi();
        wddm->resetGdi(gdi);
        ASSERT_NE(wddm, nullptr);
        DebugManager.flags.CsrDispatchMode.set(static_cast<uint32_t>(DispatchMode::ImmediateDispatch));
        this->csr = new MockWddmCsr<DEFAULT_TEST_FAMILY_NAME>(*executionEnvironment, 0, 1);
        memoryManager = new WddmMemoryManager(*executionEnvironment);
        ASSERT_NE(nullptr, memoryManager);
        executionEnvironment->memoryManager.reset(memoryManager);
        device = std::unique_ptr<MockDevice>(Device::create<MockDevice>(executionEnvironment, 0u));
        device->resetCommandStreamReceiver(this->csr);
        ASSERT_NE(nullptr, device);
        this->csr->overrideRecorededCommandBuffer(*device);
    }

    virtual void TearDown() {
        wddm = nullptr;
    }
};

using WddmCommandStreamTest = ::Test<WddmCommandStreamFixture>;
using WddmCommandStreamMockGdiTest = ::Test<WddmCommandStreamWithMockGdiFixture>;
using WddmDefaultTest = ::Test<ClDeviceFixture>;
using DeviceCommandStreamTest = ::Test<MockAubCenterFixture>;

TEST_F(DeviceCommandStreamTest, WhenCreatingWddmCsrThenWddmPointerIsSetCorrectly) {
    ExecutionEnvironment *executionEnvironment = platform()->peekExecutionEnvironment();
    auto wddm = Wddm::createWddm(nullptr, *executionEnvironment->rootDeviceEnvironments[0].get());
    executionEnvironment->rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->setWddm(static_cast<WddmMock *>(wddm));
    executionEnvironment->initializeMemoryManager();
    std::unique_ptr<WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME>> csr(static_cast<WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME> *>(WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME>::create(false, *executionEnvironment, 0, 1)));
    EXPECT_NE(nullptr, csr);
    auto wddmFromCsr = csr->peekWddm();
    EXPECT_NE(nullptr, wddmFromCsr);
}

TEST_F(DeviceCommandStreamTest, WhenCreatingWddmCsrWithAubDumpThenAubCsrIsCreated) {
    ExecutionEnvironment *executionEnvironment = platform()->peekExecutionEnvironment();
    auto wddm = Wddm::createWddm(nullptr, *executionEnvironment->rootDeviceEnvironments[0].get());
    executionEnvironment->rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->setWddm(static_cast<WddmMock *>(wddm));
    executionEnvironment->initializeMemoryManager();
    std::unique_ptr<WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME>> csr(static_cast<WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME> *>(WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME>::create(true, *executionEnvironment, 0, 1)));
    EXPECT_NE(nullptr, csr);
    auto wddmFromCsr = csr->peekWddm();
    EXPECT_NE(nullptr, wddmFromCsr);
    auto aubCSR = static_cast<CommandStreamReceiverWithAUBDump<WddmCommandStreamReceiver<DEFAULT_TEST_FAMILY_NAME>> *>(csr.get())->aubCSR.get();
    EXPECT_NE(nullptr, aubCSR);
}

TEST_F(WddmCommandStreamTest, givenFlushStampWhenWaitCalledThenWaitForSpecifiedMonitoredFence) {
    uint64_t stampToWait = 123;
    wddm->waitFromCpuResult.called = 0u;
    csr->waitForFlushStamp(stampToWait);
    EXPECT_EQ(1u, wddm->waitFromCpuResult.called);
    EXPECT_TRUE(wddm->waitFromCpuResult.success);
    EXPECT_EQ(stampToWait, wddm->waitFromCpuResult.uint64ParamPassed);
}

TEST_F(WddmCommandStreamTest, WhenFlushingThenFlushIsSubmitted) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());

    EXPECT_EQ(1u, wddm->submitResult.called);
    EXPECT_TRUE(wddm->submitResult.success);
    EXPECT_EQ(csr->obtainCurrentFlushStamp(), static_cast<OsContextWin &>(csr->getOsContext()).getResidencyController().getMonitoredFence().lastSubmittedFence);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenPrintIndicesEnabledWhenFlushThenPrintIndices) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.PrintDeviceAndEngineIdOnSubmission.set(true);
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    ::testing::internal::CaptureStdout();
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    std::ostringstream expectedValue;
    expectedValue << "Submission to RootDevice Index: " << csr->getRootDeviceIndex() << ", Sub-Devices Mask: " << csr->getOsContext().getDeviceBitfield().to_ulong() << ", EngineId: " << csr->getOsContext().getEngineType() << "\n";
    EXPECT_STREQ(::testing::internal::GetCapturedStdout().c_str(), expectedValue.str().c_str());

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenGraphicsAllocationWithDifferentGpuAddressThenCpuAddressWhenSubmitIsCalledThenGpuAddressIsUsed) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});

    auto cpuAddress = commandBuffer->getUnderlyingBuffer();
    uint64_t mockGpuAddres = 1337;
    commandBuffer->setCpuPtrAndGpuAddress(cpuAddress, mockGpuAddres);

    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    EXPECT_EQ(mockGpuAddres, wddm->submitResult.commandBufferSubmitted);
    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, GivenOffsetWhenFlushingThenFlushIsSubmittedCorrectly) {
    auto offset = 128u;
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize, mockDeviceBitfield});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), offset, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    EXPECT_EQ(1u, wddm->submitResult.called);
    EXPECT_TRUE(wddm->submitResult.success);
    EXPECT_EQ(wddm->submitResult.commandBufferSubmitted, commandBuffer->getGpuAddress() + offset);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenWdmmWhenSubmitIsCalledThenCoherencyRequiredFlagIsSetToFalse) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;

    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);

    EXPECT_FALSE(pHeader->RequiresCoherency);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST(WddmPreemptionHeaderTests, givenWddmCommandStreamReceiverWhenPreemptionIsOffWhenWorkloadIsSubmittedThenHeaderDoesntHavePreemptionFieldSet) {
    HardwareInfo *hwInfo = nullptr;
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);
    hwInfo->capabilityTable.defaultPreemptionMode = PreemptionMode::Disabled;
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(hwInfo);
    auto wddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());
    auto csr = std::make_unique<MockWddmCsr<DEFAULT_TEST_FAMILY_NAME>>(*executionEnvironment, 0, 1);
    executionEnvironment->memoryManager.reset(new MemoryManagerCreate<WddmMemoryManager>(false, false, *executionEnvironment));
    csr->overrideDispatchPolicy(DispatchMode::ImmediateDispatch);
    OsContextWin osContext(*wddm, 0u, 1, HwHelper::get(hwInfo->platform.eRenderCoreFamily).getGpgpuEngineInstances(*hwInfo)[0],
                           PreemptionHelper::getDefaultPreemptionMode(*hwInfo),
                           false);
    csr->setupContext(osContext);

    auto commandBuffer = executionEnvironment->memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;
    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);

    EXPECT_FALSE(pHeader->NeedsMidBatchPreEmptionSupport);
    executionEnvironment->memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST(WddmPreemptionHeaderTests, givenWddmCommandStreamReceiverWhenPreemptionIsOnWhenWorkloadIsSubmittedThenHeaderDoesHavePreemptionFieldSet) {
    HardwareInfo *hwInfo = nullptr;
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);
    hwInfo->capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(hwInfo);
    auto wddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());
    auto csr = std::make_unique<MockWddmCsr<DEFAULT_TEST_FAMILY_NAME>>(*executionEnvironment, 0, 1);
    executionEnvironment->memoryManager.reset(new MemoryManagerCreate<WddmMemoryManager>(false, false, *executionEnvironment));
    csr->overrideDispatchPolicy(DispatchMode::ImmediateDispatch);
    OsContextWin osContext(*wddm, 0u, 1, HwHelper::get(hwInfo->platform.eRenderCoreFamily).getGpgpuEngineInstances(*hwInfo)[0],
                           PreemptionHelper::getDefaultPreemptionMode(*hwInfo),
                           false);
    csr->setupContext(osContext);

    auto commandBuffer = executionEnvironment->memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;
    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);

    EXPECT_TRUE(pHeader->NeedsMidBatchPreEmptionSupport);
    executionEnvironment->memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST(WddmPreemptionHeaderTests, givenDeviceSupportingPreemptionWhenCommandStreamReceiverIsCreatedThenHeaderContainsPreemptionFieldSet) {
    HardwareInfo *hwInfo = nullptr;
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);
    hwInfo->capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(hwInfo);
    auto commandStreamReceiver = std::make_unique<MockWddmCsr<DEFAULT_TEST_FAMILY_NAME>>(*executionEnvironment, 0, 1);
    auto commandHeader = commandStreamReceiver->commandBufferHeader;
    auto header = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);
    EXPECT_TRUE(header->NeedsMidBatchPreEmptionSupport);
}

TEST(WddmPreemptionHeaderTests, givenDevicenotSupportingPreemptionWhenCommandStreamReceiverIsCreatedThenHeaderPreemptionFieldIsNotSet) {
    HardwareInfo *hwInfo = nullptr;
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);
    hwInfo->capabilityTable.defaultPreemptionMode = PreemptionMode::Disabled;
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(hwInfo);
    auto commandStreamReceiver = std::make_unique<MockWddmCsr<DEFAULT_TEST_FAMILY_NAME>>(*executionEnvironment, 0, 1);
    auto commandHeader = commandStreamReceiver->commandBufferHeader;
    auto header = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);
    EXPECT_FALSE(header->NeedsMidBatchPreEmptionSupport);
}

TEST_F(WddmCommandStreamTest, givenWdmmWhenSubmitIsCalledWhenEUCountWouldBeOddThenRequestEvenEuCount) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    wddm->getGtSysInfo()->EUCount = 9;
    wddm->getGtSysInfo()->SubSliceCount = 3;

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::LOW, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;

    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);

    EXPECT_EQ(0, pHeader->UmdRequestedSliceState);
    EXPECT_EQ(0, pHeader->UmdRequestedSubsliceCount);
    EXPECT_EQ((wddm->getGtSysInfo()->EUCount / wddm->getGtSysInfo()->SubSliceCount) & (~1u), pHeader->UmdRequestedEUCount);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenWdmmWhenSubmitIsCalledAndThrottleIsToLowThenSetHeaderFieldsProperly) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::LOW, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;

    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);

    EXPECT_EQ(0, pHeader->UmdRequestedSliceState);
    EXPECT_EQ(0, pHeader->UmdRequestedSubsliceCount);
    EXPECT_EQ((wddm->getGtSysInfo()->EUCount / wddm->getGtSysInfo()->SubSliceCount) & (~1u), pHeader->UmdRequestedEUCount);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenWdmmWhenSubmitIsCalledAndThrottleIsToMediumThenSetHeaderFieldsProperly) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;

    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);

    EXPECT_EQ(0, pHeader->UmdRequestedSliceState);
    EXPECT_EQ(0, pHeader->UmdRequestedSubsliceCount);
    EXPECT_EQ((wddm->getGtSysInfo()->EUCount / wddm->getGtSysInfo()->SubSliceCount) & (~1u), pHeader->UmdRequestedEUCount);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenWdmmWhenSubmitIsCalledAndThrottleIsToHighThenSetHeaderFieldsProperly) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::HIGH, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto commandHeader = wddm->submitResult.commandHeaderSubmitted;

    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandHeader);
    const uint32_t maxRequestedSubsliceCount = 7;
    EXPECT_EQ(0, pHeader->UmdRequestedSliceState);
    EXPECT_EQ((wddm->getGtSysInfo()->SubSliceCount <= maxRequestedSubsliceCount) ? wddm->getGtSysInfo()->SubSliceCount : 0, pHeader->UmdRequestedSubsliceCount);
    EXPECT_EQ((wddm->getGtSysInfo()->EUCount / wddm->getGtSysInfo()->SubSliceCount) & (~1u), pHeader->UmdRequestedEUCount);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafDisabledWhenFlushIsCalledWithAllocationsForResidencyThenNoneAllocationShouldBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    auto linearStreamAllocation = memoryManager->allocateGraphicsMemoryWithProperties({csr->getRootDeviceIndex(), MemoryConstants::pageSize, GraphicsAllocation::AllocationType::LINEAR_STREAM, device->getDeviceBitfield()});
    ASSERT_NE(nullptr, linearStreamAllocation);
    ResidencyContainer allocationsForResidency = {linearStreamAllocation};

    EXPECT_FALSE(wddm->isKmDafEnabled());
    csr->flush(batchBuffer, allocationsForResidency);

    EXPECT_EQ(0u, wddm->kmDafLockResult.called);
    EXPECT_EQ(0u, wddm->kmDafLockResult.lockedAllocations.size());

    memoryManager->freeGraphicsMemory(commandBuffer);
    memoryManager->freeGraphicsMemory(linearStreamAllocation);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafEnabledWhenFlushIsCalledWithoutAllocationsForResidencyThenNoneAllocationShouldBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    wddm->setKmDafEnabled(true);
    csr->flush(batchBuffer, csr->getResidencyAllocations());

    EXPECT_EQ(0u, wddm->kmDafLockResult.called);
    EXPECT_EQ(0u, wddm->kmDafLockResult.lockedAllocations.size());

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafEnabledWhenFlushIsCalledWithResidencyAllocationsInMemoryManagerThenLinearStreamAllocationsShouldBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    auto linearStreamAllocation = static_cast<WddmAllocation *>(memoryManager->allocateGraphicsMemoryWithProperties({csr->getRootDeviceIndex(), MemoryConstants::pageSize, GraphicsAllocation::AllocationType::LINEAR_STREAM, device->getDeviceBitfield()}));
    ASSERT_NE(nullptr, linearStreamAllocation);

    csr->makeResident(*linearStreamAllocation);
    EXPECT_EQ(1u, csr->getResidencyAllocations().size());
    EXPECT_EQ(linearStreamAllocation, csr->getResidencyAllocations()[0]);

    wddm->setKmDafEnabled(true);
    csr->flush(batchBuffer, csr->getResidencyAllocations());

    EXPECT_EQ(1u, wddm->kmDafLockResult.called);
    EXPECT_EQ(1u, wddm->kmDafLockResult.lockedAllocations.size());
    EXPECT_EQ(linearStreamAllocation->getDefaultHandle(), wddm->kmDafLockResult.lockedAllocations[0]);

    memoryManager->freeGraphicsMemory(commandBuffer);
    memoryManager->freeGraphicsMemory(linearStreamAllocation);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafEnabledWhenFlushIsCalledWithAllocationsForResidencyThenLinearStreamAllocationsShouldBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    auto linearStreamAllocation = static_cast<WddmAllocation *>(memoryManager->allocateGraphicsMemoryWithProperties({csr->getRootDeviceIndex(), MemoryConstants::pageSize, GraphicsAllocation::AllocationType::LINEAR_STREAM, device->getDeviceBitfield()}));
    ASSERT_NE(nullptr, linearStreamAllocation);
    ResidencyContainer allocationsForResidency = {linearStreamAllocation};

    wddm->setKmDafEnabled(true);
    csr->flush(batchBuffer, allocationsForResidency);

    EXPECT_EQ(1u, wddm->kmDafLockResult.called);
    EXPECT_EQ(1u, wddm->kmDafLockResult.lockedAllocations.size());
    EXPECT_EQ(linearStreamAllocation->getDefaultHandle(), wddm->kmDafLockResult.lockedAllocations[0]);

    memoryManager->freeGraphicsMemory(commandBuffer);
    memoryManager->freeGraphicsMemory(linearStreamAllocation);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafEnabledWhenFlushIsCalledWithAllocationsForResidencyThenFillPatternAllocationsShouldBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    auto fillPatternAllocation = static_cast<WddmAllocation *>(memoryManager->allocateGraphicsMemoryWithProperties({csr->getRootDeviceIndex(), MemoryConstants::pageSize, GraphicsAllocation::AllocationType::FILL_PATTERN, device->getDeviceBitfield()}));
    ASSERT_NE(nullptr, fillPatternAllocation);
    ResidencyContainer allocationsForResidency = {fillPatternAllocation};

    wddm->setKmDafEnabled(true);
    csr->flush(batchBuffer, allocationsForResidency);

    EXPECT_EQ(1u, wddm->kmDafLockResult.called);
    EXPECT_EQ(1u, wddm->kmDafLockResult.lockedAllocations.size());
    EXPECT_EQ(fillPatternAllocation->getDefaultHandle(), wddm->kmDafLockResult.lockedAllocations[0]);

    memoryManager->freeGraphicsMemory(commandBuffer);
    memoryManager->freeGraphicsMemory(fillPatternAllocation);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafEnabledWhenFlushIsCalledWithAllocationsForResidencyThenCommandBufferAllocationsShouldBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    auto commandBufferAllocation = static_cast<WddmAllocation *>(memoryManager->allocateGraphicsMemoryWithProperties({csr->getRootDeviceIndex(), MemoryConstants::pageSize, GraphicsAllocation::AllocationType::COMMAND_BUFFER, device->getDeviceBitfield()}));
    ASSERT_NE(nullptr, commandBufferAllocation);
    ResidencyContainer allocationsForResidency = {commandBufferAllocation};

    wddm->setKmDafEnabled(true);
    csr->flush(batchBuffer, allocationsForResidency);

    EXPECT_EQ(1u, wddm->kmDafLockResult.called);
    EXPECT_EQ(1u, wddm->kmDafLockResult.lockedAllocations.size());
    EXPECT_EQ(commandBufferAllocation->getDefaultHandle(), wddm->kmDafLockResult.lockedAllocations[0]);

    memoryManager->freeGraphicsMemory(commandBuffer);
    memoryManager->freeGraphicsMemory(commandBufferAllocation);
}

TEST_F(WddmCommandStreamTest, givenWddmWithKmDafEnabledWhenFlushIsCalledWithAllocationsForResidencyThenNonLinearStreamAllocationShouldNotBeKmDafLocked) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};

    auto nonLinearStreamAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, nonLinearStreamAllocation);
    ResidencyContainer allocationsForResidency = {nonLinearStreamAllocation};

    wddm->setKmDafEnabled(true);
    csr->flush(batchBuffer, allocationsForResidency);

    EXPECT_EQ(0u, wddm->kmDafLockResult.called);
    EXPECT_EQ(0u, wddm->kmDafLockResult.lockedAllocations.size());

    memoryManager->freeGraphicsMemory(commandBuffer);
    memoryManager->freeGraphicsMemory(nonLinearStreamAllocation);
}

TEST_F(WddmCommandStreamTest, WhenMakingResidentThenAllocationIsCorrectlySet) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    csr->makeResident(*commandBuffer);

    EXPECT_EQ(0u, wddm->makeResidentResult.called);
    EXPECT_EQ(1u, csr->getResidencyAllocations().size());
    EXPECT_EQ(commandBuffer, csr->getResidencyAllocations()[0]);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, WhenMakingNonResidentThenAllocationIsPlacedInEvictionAllocations) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    csr->makeResident(*cs.getGraphicsAllocation());

    csr->makeNonResident(*commandBuffer);

    EXPECT_EQ(1u, csr->getEvictionAllocations().size());

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, WhenProcessingEvictionThenAllAllocationsArePlacedOnTrimCandidateList) {
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    GraphicsAllocation *allocation2 = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, allocation);
    ASSERT_NE(nullptr, allocation2);

    csr->getEvictionAllocations().push_back(allocation);
    csr->getEvictionAllocations().push_back(allocation2);

    EXPECT_EQ(2u, csr->getEvictionAllocations().size());

    csr->processEviction();

    EXPECT_EQ(2u, static_cast<OsContextWin &>(csr->getOsContext()).getResidencyController().peekTrimCandidateList().size());

    memoryManager->freeGraphicsMemory(allocation);
    memoryManager->freeGraphicsMemory(allocation2);
}

TEST_F(WddmCommandStreamTest, WhenProcesssingEvictionThenEvictionAllocationsListIsCleared) {
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, allocation);

    csr->getEvictionAllocations().push_back(allocation);

    EXPECT_EQ(1u, csr->getEvictionAllocations().size());

    csr->processEviction();

    EXPECT_EQ(0u, csr->getEvictionAllocations().size());

    memoryManager->freeGraphicsMemory(allocation);
}

TEST_F(WddmCommandStreamTest, WhenMakingResidentAndNonResidentThenAllocationIsMovedCorrectly) {
    GraphicsAllocation *gfxAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    Buffer *buffer = new AlignedBuffer(gfxAllocation);

    csr->makeResident(*buffer->getGraphicsAllocation(csr->getRootDeviceIndex()));
    EXPECT_EQ(0u, wddm->makeResidentResult.called);
    EXPECT_EQ(1u, csr->getResidencyAllocations().size());
    EXPECT_EQ(gfxAllocation, csr->getResidencyAllocations()[0]);

    csr->makeNonResident(*buffer->getGraphicsAllocation(csr->getRootDeviceIndex()));
    EXPECT_EQ(gfxAllocation, csr->getEvictionAllocations()[0]);

    delete buffer;
    memoryManager->freeGraphicsMemory(gfxAllocation);
}

TEST_F(WddmCommandStreamTest, givenGraphicsAllocationWhenMakeResidentThenAllocationIsInResidencyContainer) {
    void *hostPtr = reinterpret_cast<void *>(wddm->virtualAllocAddress + 0x1234);
    auto size = 1234u;

    auto gfxAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), false, size}, hostPtr);

    ASSERT_NE(nullptr, gfxAllocation);

    csr->makeResidentHostPtrAllocation(gfxAllocation);

    EXPECT_EQ(1u, csr->getResidencyAllocations().size());
    EXPECT_EQ(hostPtr, gfxAllocation->getUnderlyingBuffer());

    memoryManager->freeGraphicsMemory(gfxAllocation);
}

TEST_F(WddmCommandStreamTest, givenHostPtrAllocationWhenMapFailsThenFragmentsAreClearedAndNullptrIsReturned) {
    this->wddm->callBaseMapGpuVa = false;
    this->wddm->mapGpuVaStatus = false;
    void *hostPtr = reinterpret_cast<void *>(wddm->virtualAllocAddress + 0x1234);
    auto size = 1234u;

    wddm->mapGpuVirtualAddressResult.called = 0u;
    wddm->destroyAllocationResult.called = 0u;

    auto gfxAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), false, size}, hostPtr);

    EXPECT_EQ(1u, wddm->mapGpuVirtualAddressResult.called);
    EXPECT_EQ(1u, wddm->destroyAllocationResult.called);
    EXPECT_EQ(nullptr, gfxAllocation);
}

TEST_F(WddmCommandStreamTest, givenAddressWithHighestBitSetWhenItIsMappedThenProperAddressIsPassed) {
    uintptr_t address = 0xffff0000;
    void *faultyAddress = reinterpret_cast<void *>(address);

    wddm->mapGpuVirtualAddressResult.called = 0u;

    auto gfxAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), false, MemoryConstants::pageSize}, faultyAddress);

    EXPECT_EQ(1u, wddm->mapGpuVirtualAddressResult.called);
    ASSERT_NE(nullptr, gfxAllocation);
    auto expectedAddress = castToUint64(faultyAddress);
    EXPECT_EQ(gfxAllocation->getGpuAddress(), expectedAddress);
    ASSERT_EQ(gfxAllocation->fragmentsStorage.fragmentCount, 1u);
    EXPECT_EQ(expectedAddress, static_cast<OsHandleWin *>(gfxAllocation->fragmentsStorage.fragmentStorageData[0].osHandleStorage)->gpuPtr);

    memoryManager->freeGraphicsMemory(gfxAllocation);
}

TEST_F(WddmCommandStreamTest, givenHostPtrWhenPtrBelowRestrictionThenCreateAllocationAndMakeResident) {
    void *hostPtr = reinterpret_cast<void *>(memoryManager->getAlignedMallocRestrictions()->minAddress - 0x1000);
    auto size = 0x2000u;

    auto gfxAllocation = static_cast<WddmAllocation *>(memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), false, size}, hostPtr));

    void *expectedReserve = reinterpret_cast<void *>(wddm->virtualAllocAddress);

    ASSERT_NE(nullptr, gfxAllocation);

    csr->makeResidentHostPtrAllocation(gfxAllocation);

    EXPECT_EQ(1u, csr->getResidencyAllocations().size());
    EXPECT_EQ(hostPtr, gfxAllocation->getUnderlyingBuffer());
    EXPECT_EQ(expectedReserve, gfxAllocation->getReservedAddressPtr());

    memoryManager->freeGraphicsMemory(gfxAllocation);
}

TEST_F(WddmCommandStreamTest, givenTwoTemporaryAllocationsWhenCleanTemporaryAllocationListThenDestoryOnlyCompletedAllocations) {
    void *host_ptr = (void *)0x1212341;
    void *host_ptr2 = (void *)0x2212341;
    auto size = 17262u;

    GraphicsAllocation *graphicsAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), false, size}, host_ptr);
    GraphicsAllocation *graphicsAllocation2 = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), false, size}, host_ptr2);
    csr->getInternalAllocationStorage()->storeAllocation(std::unique_ptr<GraphicsAllocation>(graphicsAllocation), TEMPORARY_ALLOCATION);
    csr->getInternalAllocationStorage()->storeAllocation(std::unique_ptr<GraphicsAllocation>(graphicsAllocation2), TEMPORARY_ALLOCATION);

    graphicsAllocation->updateTaskCount(1, csr->getOsContext().getContextId());
    graphicsAllocation2->updateTaskCount(100, csr->getOsContext().getContextId());

    csr->waitForTaskCountAndCleanAllocationList(1, TEMPORARY_ALLOCATION);
    // graphicsAllocation2 still lives
    EXPECT_EQ(host_ptr2, graphicsAllocation2->getUnderlyingBuffer());

    auto hostPtrManager = memoryManager->getHostPtrManager();

    auto alignedPtr = alignDown(host_ptr, MemoryConstants::pageSize);
    auto alignedPtr2 = alignDown(host_ptr2, MemoryConstants::pageSize);

    auto fragment = hostPtrManager->getFragment({alignedPtr2, csr->getRootDeviceIndex()});
    ASSERT_NE(nullptr, fragment);

    EXPECT_EQ(alignedPtr2, fragment->fragmentCpuPointer);

    auto fragment2 = hostPtrManager->getFragment({alignedPtr, csr->getRootDeviceIndex()});
    EXPECT_EQ(nullptr, fragment2);
    // destroy remaining allocation
    csr->waitForTaskCountAndCleanAllocationList(100, TEMPORARY_ALLOCATION);
}

TEST_F(WddmCommandStreamMockGdiTest, WhenFlushingThenWddmMakeResidentIsCalledForResidencyAllocations) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    csr->makeResident(*commandBuffer);

    EXPECT_EQ(1u, csr->getResidencyAllocations().size());

    gdi->getMakeResidentArg().NumAllocations = 0;

    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0, nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(), &cs, nullptr, false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());

    EXPECT_NE(0u, gdi->getMakeResidentArg().NumAllocations);

    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamMockGdiTest, WhenMakingResidentThenResidencyAllocationsListIsCleared) {
    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);

    csr->makeResident(*commandBuffer);

    EXPECT_EQ(1u, csr->getResidencyAllocations().size());
    EXPECT_EQ(0u, csr->getEvictionAllocations().size());

    EXPECT_EQ(trimListUnusedPosition, static_cast<WddmAllocation *>(commandBuffer)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));

    csr->processResidency(csr->getResidencyAllocations(), 0u);

    csr->makeSurfacePackNonResident(csr->getResidencyAllocations());

    EXPECT_EQ(0u, csr->getResidencyAllocations().size());
    EXPECT_EQ(0u, csr->getEvictionAllocations().size());

    EXPECT_EQ(0u, static_cast<WddmAllocation *>(commandBuffer)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));

    memoryManager->freeGraphicsMemory(commandBuffer);
}

HWTEST_F(WddmCommandStreamMockGdiTest, givenRecordedCommandBufferWhenItIsSubmittedThenFlushTaskIsProperlyCalled) {
    //preemption allocation + sip allocation
    size_t csrSurfaceCount = 0;
    if (device->getPreemptionMode() == PreemptionMode::MidThread) {
        csrSurfaceCount = 2;
    }
    csrSurfaceCount += csr->globalFenceAllocation ? 1 : 0;

    csr->overrideDispatchPolicy(DispatchMode::BatchedDispatch);
    csr->useNewResourceImplicitFlush = false;
    csr->useGpuIdleImplicitFlush = false;

    auto mockedSubmissionsAggregator = new mockSubmissionsAggregator();
    csr->overrideSubmissionAggregator(mockedSubmissionsAggregator);

    auto commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    auto dshAlloc = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    auto iohAlloc = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    auto sshAlloc = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});

    auto tagAllocation = csr->getTagAllocation();

    LinearStream cs(commandBuffer);
    IndirectHeap dsh(dshAlloc);
    IndirectHeap ioh(iohAlloc);
    IndirectHeap ssh(sshAlloc);

    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();
    dispatchFlags.preemptionMode = PreemptionHelper::getDefaultPreemptionMode(device->getHardwareInfo());
    dispatchFlags.guardCommandBufferWithPipeControl = true;
    dispatchFlags.requiresCoherency = true;

    csr->flushTask(cs, 0u, dsh, ioh, ssh, 0u, dispatchFlags, *device);

    auto &cmdBuffers = mockedSubmissionsAggregator->peekCommandBuffers();
    auto storedCommandBuffer = cmdBuffers.peekHead();

    ResidencyContainer copyOfResidency = storedCommandBuffer->surfaces;
    copyOfResidency.push_back(storedCommandBuffer->batchBuffer.commandBufferAllocation);

    csr->flushBatchedSubmissions();

    csrSurfaceCount += csr->clearColorAllocation ? 1 : 0;
    EXPECT_TRUE(cmdBuffers.peekIsEmpty());

    EXPECT_EQ(1u, wddm->submitResult.called);
    auto csrCommandStream = csr->commandStream.getGraphicsAllocation();
    EXPECT_EQ(csrCommandStream->getGpuAddress(), wddm->submitResult.commandBufferSubmitted);
    EXPECT_TRUE(((COMMAND_BUFFER_HEADER *)wddm->submitResult.commandHeaderSubmitted)->RequiresCoherency);
    EXPECT_EQ(6u + csrSurfaceCount, wddm->makeResidentResult.handleCount);

    std::vector<D3DKMT_HANDLE> expectedHandles;
    expectedHandles.push_back(static_cast<WddmAllocation *>(tagAllocation)->getDefaultHandle());
    expectedHandles.push_back(static_cast<WddmAllocation *>(commandBuffer)->getDefaultHandle());
    expectedHandles.push_back(static_cast<WddmAllocation *>(dshAlloc)->getDefaultHandle());
    expectedHandles.push_back(static_cast<WddmAllocation *>(iohAlloc)->getDefaultHandle());
    expectedHandles.push_back(static_cast<WddmAllocation *>(sshAlloc)->getDefaultHandle());
    expectedHandles.push_back(static_cast<WddmAllocation *>(csrCommandStream)->getDefaultHandle());

    for (auto i = 0u; i < wddm->makeResidentResult.handleCount; i++) {
        auto handle = wddm->makeResidentResult.handlePack[i];
        auto found = false;
        for (auto &expectedHandle : expectedHandles) {
            if (expectedHandle == handle) {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    EXPECT_NE(trimListUnusedPosition, static_cast<WddmAllocation *>(tagAllocation)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));
    EXPECT_NE(trimListUnusedPosition, static_cast<WddmAllocation *>(commandBuffer)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));
    EXPECT_EQ(trimListUnusedPosition, static_cast<WddmAllocation *>(dshAlloc)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));
    EXPECT_EQ(trimListUnusedPosition, static_cast<WddmAllocation *>(iohAlloc)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));
    EXPECT_NE(trimListUnusedPosition, static_cast<WddmAllocation *>(sshAlloc)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));
    EXPECT_NE(trimListUnusedPosition, static_cast<WddmAllocation *>(csrCommandStream)->getTrimCandidateListPosition(csr->getOsContext().getContextId()));

    memoryManager->freeGraphicsMemory(dshAlloc);
    memoryManager->freeGraphicsMemory(iohAlloc);
    memoryManager->freeGraphicsMemory(sshAlloc);
    memoryManager->freeGraphicsMemory(commandBuffer);
}

using WddmSimpleTest = ::testing::Test;

HWTEST_F(WddmSimpleTest, givenDefaultWddmCsrWhenItIsCreatedThenBatchingIsTurnedOn) {
    DebugManager.flags.CsrDispatchMode.set(0);
    ExecutionEnvironment *executionEnvironment = platform()->peekExecutionEnvironment();
    std::unique_ptr<MockDevice> device(Device::create<MockDevice>(executionEnvironment, 0u));
    auto wddm = Wddm::createWddm(nullptr, *executionEnvironment->rootDeviceEnvironments[0].get());
    executionEnvironment->rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->setWddm(wddm);
    executionEnvironment->rootDeviceEnvironments[0]->memoryOperationsInterface = std::make_unique<WddmMemoryOperationsHandler>(wddm);
    std::unique_ptr<MockWddmCsr<FamilyType>> mockCsr(new MockWddmCsr<FamilyType>(*executionEnvironment, 0, 1));
    EXPECT_EQ(DispatchMode::BatchedDispatch, mockCsr->dispatchMode);
}

HWTEST_F(WddmDefaultTest, givenFtrWddmHwQueuesFlagWhenCreatingCsrThenPickWddmVersionBasingOnFtrFlag) {
    auto wddm = Wddm::createWddm(nullptr, *pDevice->executionEnvironment->rootDeviceEnvironments[0].get());
    pDevice->executionEnvironment->rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    pDevice->executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->setWddm(wddm);
    pDevice->executionEnvironment->rootDeviceEnvironments[0]->memoryOperationsInterface = std::make_unique<WddmMemoryOperationsHandler>(wddm);
    WddmCommandStreamReceiver<FamilyType> wddmCsr(*pDevice->executionEnvironment, 0, 1);

    auto wddmFromCsr = wddmCsr.peekWddm();
    EXPECT_NE(nullptr, wddmFromCsr);
}

struct WddmCsrCompressionTests : ::testing::Test {
    void setCompressionEnabled(bool enableForBuffer, bool enableForImages) {
        RuntimeCapabilityTable capabilityTable = defaultHwInfo->capabilityTable;
        capabilityTable.ftrRenderCompressedBuffers = enableForBuffer;
        capabilityTable.ftrRenderCompressedImages = enableForImages;
        hwInfo->capabilityTable = capabilityTable;
    }

    HardwareInfo *hwInfo = nullptr;
    WddmMock *myMockWddm;
};

struct WddmCsrCompressionParameterizedTest : WddmCsrCompressionTests, ::testing::WithParamInterface<bool /*compressionEnabled*/> {
    void SetUp() override {
        compressionEnabled = GetParam();
    }

    bool compressionEnabled;
};

HWTEST_P(WddmCsrCompressionParameterizedTest, givenEnabledCompressionWhenInitializedThenCreatePagetableMngr) {
    uint32_t index = 1u;
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 2);
    std::unique_ptr<MockDevice> device(Device::create<MockDevice>(executionEnvironment, 1u));
    setCompressionEnabled(compressionEnabled, !compressionEnabled);
    myMockWddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());
    EXPECT_EQ(nullptr, executionEnvironment->rootDeviceEnvironments[index]->pageTableManager.get());

    MockWddmCsr<FamilyType> mockWddmCsr(*executionEnvironment, index, 1);
    mockWddmCsr.createPageTableManager();
    ASSERT_NE(nullptr, executionEnvironment->rootDeviceEnvironments[index]->pageTableManager.get());

    auto mockMngr = reinterpret_cast<MockGmmPageTableMngr *>(executionEnvironment->rootDeviceEnvironments[index]->pageTableManager.get());
    EXPECT_EQ(1u, mockMngr->setCsrHanleCalled);
    EXPECT_EQ(&mockWddmCsr, mockMngr->passedCsrHandle);

    GMM_TRANSLATIONTABLE_CALLBACKS expectedTTCallbacks = {};
    unsigned int expectedFlags = TT_TYPE::AUXTT;

    expectedTTCallbacks.pfWriteL3Adr = TTCallbacks<FamilyType>::writeL3Address;

    EXPECT_TRUE(memcmp(&expectedTTCallbacks, &mockMngr->translationTableCb, sizeof(GMM_TRANSLATIONTABLE_CALLBACKS)) == 0);
    EXPECT_TRUE(memcmp(&expectedFlags, &mockMngr->translationTableFlags, sizeof(unsigned int)) == 0);
}

HWTEST_F(WddmCsrCompressionTests, givenDisabledCompressionWhenInitializedThenDontCreatePagetableMngr) {
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 2);
    std::unique_ptr<MockDevice> device(Device::create<MockDevice>(executionEnvironment, 1u));
    setCompressionEnabled(false, false);
    myMockWddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());
    MockWddmCsr<FamilyType> mockWddmCsr(*executionEnvironment, 1, device->getDeviceBitfield());
    EXPECT_EQ(nullptr, executionEnvironment->rootDeviceEnvironments[1]->pageTableManager.get());
}

INSTANTIATE_TEST_CASE_P(
    WddmCsrCompressionParameterizedTestCreate,
    WddmCsrCompressionParameterizedTest,
    ::testing::Bool());

HWTEST_F(WddmCsrCompressionTests, givenDisabledCompressionWhenFlushingThenDontInitTranslationTable) {
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 2);
    setCompressionEnabled(false, false);
    myMockWddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->getWddm());

    executionEnvironment->memoryManager.reset(new WddmMemoryManager(*executionEnvironment));

    std::unique_ptr<MockDevice> device(Device::create<MockDevice>(executionEnvironment, 1u));

    auto mockWddmCsr = new MockWddmCsr<FamilyType>(*executionEnvironment, 1, device->getDeviceBitfield());
    mockWddmCsr->overrideDispatchPolicy(DispatchMode::BatchedDispatch);
    device->resetCommandStreamReceiver(mockWddmCsr);

    auto memoryManager = executionEnvironment->memoryManager.get();

    EXPECT_EQ(nullptr, executionEnvironment->rootDeviceEnvironments[1]->pageTableManager.get());

    auto graphicsAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{mockWddmCsr->getRootDeviceIndex(), MemoryConstants::pageSize});
    IndirectHeap cs(graphicsAllocation);

    EXPECT_EQ(nullptr, executionEnvironment->rootDeviceEnvironments[1]->pageTableManager.get());

    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();

    mockWddmCsr->flushTask(cs, 0u, cs, cs, cs, 0u, dispatchFlags, *device);

    EXPECT_EQ(nullptr, executionEnvironment->rootDeviceEnvironments[1]->pageTableManager.get());

    mockWddmCsr->flushBatchedSubmissions();
    memoryManager->freeGraphicsMemory(graphicsAllocation);
}

TEST_F(WddmCommandStreamMockGdiTest, givenDirectSubmissionEnabledOnRcsWhenFlushingCommandBufferThenExpectDirectSubmissionUsed) {
    using Dispatcher = RenderDispatcher<DEFAULT_TEST_FAMILY_NAME>;
    using MockSubmission =
        MockWddmDirectSubmission<DEFAULT_TEST_FAMILY_NAME, Dispatcher>;

    DebugManager.flags.EnableDirectSubmission.set(1);

    auto hwInfo = device->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->capabilityTable.directSubmissionEngines.data[aub_stream::ENGINE_RCS].engineSupported = true;

    std::unique_ptr<OsContext> osContext;
    osContext.reset(OsContext::create(device->getExecutionEnvironment()->rootDeviceEnvironments[0]->osInterface.get(),
                                      0, device->getDeviceBitfield(), EngineTypeUsage{aub_stream::ENGINE_RCS, EngineUsage::Regular}, PreemptionMode::ThreadGroup,
                                      false));
    osContext->setDefaultContext(true);
    csr->callParentInitDirectSubmission = false;
    bool ret = csr->initDirectSubmission(*device.get(), *osContext.get());
    EXPECT_TRUE(ret);
    EXPECT_TRUE(csr->isDirectSubmissionEnabled());
    EXPECT_FALSE(csr->isBlitterDirectSubmissionEnabled());

    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0,
                            nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(),
                            &cs, commandBuffer->getUnderlyingBuffer(), false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto directSubmission = reinterpret_cast<MockSubmission *>(csr->directSubmission.get());
    EXPECT_TRUE(directSubmission->ringStart);
    size_t actualDispatchSize = directSubmission->ringCommandStream.getUsed();
    size_t expectedSize = directSubmission->getSizeSemaphoreSection() +
                          Dispatcher::getSizePreemption() +
                          directSubmission->getSizeDispatch();
    EXPECT_EQ(expectedSize, actualDispatchSize);
    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamMockGdiTest, givenDirectSubmissionEnabledOnBcsWhenFlushingCommandBufferThenExpectDirectSubmissionUsed) {
    using Dispatcher = BlitterDispatcher<DEFAULT_TEST_FAMILY_NAME>;
    using MockSubmission =
        MockWddmDirectSubmission<DEFAULT_TEST_FAMILY_NAME, Dispatcher>;

    DebugManager.flags.EnableDirectSubmission.set(1);

    auto hwInfo = device->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->capabilityTable.directSubmissionEngines.data[aub_stream::ENGINE_BCS].engineSupported = true;

    std::unique_ptr<OsContext> osContext;
    osContext.reset(OsContext::create(device->getExecutionEnvironment()->rootDeviceEnvironments[0]->osInterface.get(),
                                      0, device->getDeviceBitfield(), EngineTypeUsage{aub_stream::ENGINE_RCS, EngineUsage::Regular}, PreemptionMode::ThreadGroup,
                                      false));
    csr->callParentInitDirectSubmission = false;
    csr->initBlitterDirectSubmission = true;
    bool ret = csr->initDirectSubmission(*device.get(), *osContext.get());
    EXPECT_TRUE(ret);
    EXPECT_FALSE(csr->isDirectSubmissionEnabled());
    EXPECT_TRUE(csr->isBlitterDirectSubmissionEnabled());

    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0,
                            nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(),
                            &cs, commandBuffer->getUnderlyingBuffer(), false};
    csr->flush(batchBuffer, csr->getResidencyAllocations());
    auto directSubmission = reinterpret_cast<MockSubmission *>(csr->blitterDirectSubmission.get());
    EXPECT_TRUE(directSubmission->ringStart);
    size_t actualDispatchSize = directSubmission->ringCommandStream.getUsed();
    size_t expectedSize = directSubmission->getSizeSemaphoreSection() +
                          Dispatcher::getSizePreemption() +
                          directSubmission->getSizeDispatch();
    EXPECT_EQ(expectedSize, actualDispatchSize);
    memoryManager->freeGraphicsMemory(commandBuffer);
}

TEST_F(WddmCommandStreamTest, givenResidencyLoggingAvailableWhenFlushingCommandBufferThenNotifiesResidencyLogger) {
    if (!NEO::wddmResidencyLoggingAvailable) {
        GTEST_SKIP();
    }

    GraphicsAllocation *commandBuffer = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{csr->getRootDeviceIndex(), MemoryConstants::pageSize});
    ASSERT_NE(nullptr, commandBuffer);
    LinearStream cs(commandBuffer);
    BatchBuffer batchBuffer{cs.getGraphicsAllocation(), 0, 0,
                            nullptr, false, false, QueueThrottle::MEDIUM, QueueSliceCount::defaultSliceCount, cs.getUsed(),
                            &cs, nullptr, false};
    DebugManagerStateRestore restorer;
    DebugManager.flags.WddmResidencyLogger.set(1);

    NEO::IoFunctions::mockFopenCalled = 0u;
    NEO::IoFunctions::mockVfptrinfCalled = 0u;
    NEO::IoFunctions::mockFcloseCalled = 0u;

    wddm->createPagingFenceLogger();

    EXPECT_EQ(1u, NEO::IoFunctions::mockFopenCalled);
    EXPECT_EQ(1u, NEO::IoFunctions::mockVfptrinfCalled);
    EXPECT_EQ(0u, NEO::IoFunctions::mockFcloseCalled);

    csr->flush(batchBuffer, csr->getResidencyAllocations());

    EXPECT_EQ(1u, NEO::IoFunctions::mockFopenCalled);
    EXPECT_EQ(3u, NEO::IoFunctions::mockVfptrinfCalled);
    EXPECT_EQ(0u, NEO::IoFunctions::mockFcloseCalled);

    memoryManager->freeGraphicsMemory(commandBuffer);
}
