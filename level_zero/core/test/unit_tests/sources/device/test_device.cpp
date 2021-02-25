/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/root_device.h"
#include "shared/source/helpers/bindless_heaps_helper.h"
#include "shared/source/os_interface/hw_info_config.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/mocks/mock_device.h"
#include "shared/test/common/mocks/ult_device_factory.h"

#include "test.h"

#include "level_zero/core/source/cmdqueue/cmdqueue_imp.h"
#include "level_zero/core/source/driver/host_pointer_manager.h"
#include "level_zero/core/test/unit_tests/mocks/mock_driver_handle.h"

#include "gtest/gtest.h"

#include <memory>

using ::testing::Return;

namespace NEO {
namespace MockSipData {
extern SipKernelType calledType;
extern bool called;
} // namespace MockSipData
} // namespace NEO

namespace L0 {
namespace ult {

TEST(L0DeviceTest, GivenDualStorageSharedMemorySupportedWhenCreatingDeviceThenPageFaultCmdListImmediateWithInitializedCmdQIsCreated) {
    DebugManagerStateRestore restorer;
    NEO::DebugManager.flags.AllocateSharedAllocationsWithCpuAndGpuStorage.set(1);

    std::unique_ptr<DriverHandleImp> driverHandle(new DriverHandleImp);
    auto hwInfo = *NEO::defaultHwInfo;
    hwInfo.featureTable.ftrLocalMemory = true;
    auto neoDevice = std::unique_ptr<NEO::Device>(NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&hwInfo, 0));

    auto device = std::unique_ptr<L0::Device>(Device::create(driverHandle.get(), neoDevice.release(), 1, false));
    ASSERT_NE(nullptr, device);
    auto deviceImp = static_cast<DeviceImp *>(device.get());
    ASSERT_NE(nullptr, deviceImp->pageFaultCommandList);

    ASSERT_NE(nullptr, deviceImp->pageFaultCommandList->cmdQImmediate);
    EXPECT_NE(nullptr, static_cast<CommandQueueImp *>(deviceImp->pageFaultCommandList->cmdQImmediate)->getCsr());
    EXPECT_EQ(ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, static_cast<CommandQueueImp *>(deviceImp->pageFaultCommandList->cmdQImmediate)->getSynchronousMode());
}

TEST(L0DeviceTest, givenMidThreadPreemptionWhenCreatingDeviceThenSipKernelIsInitialized) {
    VariableBackup<bool> mockSipCalled(&NEO::MockSipData::called, false);
    VariableBackup<NEO::SipKernelType> mockSipCalledType(&NEO::MockSipData::calledType, NEO::SipKernelType::COUNT);

    std::unique_ptr<DriverHandleImp> driverHandle(new DriverHandleImp);
    auto hwInfo = *NEO::defaultHwInfo;
    hwInfo.capabilityTable.defaultPreemptionMode = NEO::PreemptionMode::MidThread;

    auto neoDevice = std::unique_ptr<NEO::Device>(NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&hwInfo, 0));

    EXPECT_EQ(NEO::SipKernelType::COUNT, NEO::MockSipData::calledType);
    EXPECT_FALSE(NEO::MockSipData::called);

    auto device = std::unique_ptr<L0::Device>(Device::create(driverHandle.get(), neoDevice.release(), 1, false));
    ASSERT_NE(nullptr, device);

    EXPECT_EQ(NEO::SipKernelType::Csr, NEO::MockSipData::calledType);
    EXPECT_TRUE(NEO::MockSipData::called);
}

TEST(L0DeviceTest, givenDisabledPreemptionWhenCreatingDeviceThenSipKernelIsNotInitialized) {
    VariableBackup<bool> mockSipCalled(&NEO::MockSipData::called, false);
    VariableBackup<NEO::SipKernelType> mockSipCalledType(&NEO::MockSipData::calledType, NEO::SipKernelType::COUNT);

    std::unique_ptr<DriverHandleImp> driverHandle(new DriverHandleImp);
    auto hwInfo = *NEO::defaultHwInfo;
    hwInfo.capabilityTable.defaultPreemptionMode = NEO::PreemptionMode::Disabled;

    auto neoDevice = std::unique_ptr<NEO::Device>(NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&hwInfo, 0));

    EXPECT_EQ(NEO::SipKernelType::COUNT, NEO::MockSipData::calledType);
    EXPECT_FALSE(NEO::MockSipData::called);

    auto device = std::unique_ptr<L0::Device>(Device::create(driverHandle.get(), neoDevice.release(), 1, false));
    ASSERT_NE(nullptr, device);

    EXPECT_EQ(NEO::SipKernelType::COUNT, NEO::MockSipData::calledType);
    EXPECT_FALSE(NEO::MockSipData::called);
}

struct DeviceTest : public ::testing::Test {
    void SetUp() override {
        DebugManager.flags.CreateMultipleRootDevices.set(numRootDevices);
        neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(NEO::defaultHwInfo.get(), rootDeviceIndex);
        NEO::DeviceVector devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
        device = driverHandle->devices[0];
    }

    DebugManagerStateRestore restorer;
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::Device *neoDevice = nullptr;
    L0::Device *device = nullptr;
    const uint32_t rootDeviceIndex = 1u;
    const uint32_t numRootDevices = 2u;
};

TEST_F(DeviceTest, givenEmptySVmAllocStorageWhenAllocateManagedMemoryFromHostPtrThenBufferHostAllocationIsCreated) {
    int data;
    auto allocation = device->allocateManagedMemoryFromHostPtr(&data, sizeof(data), nullptr);
    EXPECT_NE(nullptr, allocation);
    EXPECT_EQ(NEO::GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY, allocation->getAllocationType());
    EXPECT_EQ(rootDeviceIndex, allocation->getRootDeviceIndex());
    neoDevice->getMemoryManager()->freeGraphicsMemory(allocation);
}

TEST_F(DeviceTest, givenEmptySVmAllocStorageWhenAllocateMemoryFromHostPtrThenValidExternalHostPtrAllocationIsCreated) {
    DebugManager.flags.EnableHostPtrTracking.set(0);
    constexpr auto dataSize = 1024u;
    auto data = std::make_unique<int[]>(dataSize);

    constexpr auto allocationSize = sizeof(int) * dataSize;

    auto allocation = device->allocateMemoryFromHostPtr(data.get(), allocationSize);
    EXPECT_NE(nullptr, allocation);
    EXPECT_EQ(NEO::GraphicsAllocation::AllocationType::EXTERNAL_HOST_PTR, allocation->getAllocationType());
    EXPECT_EQ(rootDeviceIndex, allocation->getRootDeviceIndex());

    auto alignedPtr = alignDown(data.get(), MemoryConstants::pageSize);
    auto offsetInPage = ptrDiff(data.get(), alignedPtr);

    EXPECT_EQ(allocation->getAllocationOffset(), offsetInPage);
    EXPECT_EQ(allocation->getUnderlyingBufferSize(), allocationSize);
    EXPECT_EQ(allocation->isFlushL3Required(), true);

    neoDevice->getMemoryManager()->freeGraphicsMemory(allocation);
}

struct MemoryManagerHostPointer : public NEO::OsAgnosticMemoryManager {
    MemoryManagerHostPointer(NEO::ExecutionEnvironment &executionEnvironment) : OsAgnosticMemoryManager(const_cast<NEO::ExecutionEnvironment &>(executionEnvironment)) {}
    GraphicsAllocation *allocateGraphicsMemoryWithProperties(const AllocationProperties &properties,
                                                             const void *ptr) override {
        return nullptr;
    }
};

struct DeviceHostPointerTest : public ::testing::Test {
    void SetUp() override {
        executionEnvironment = new NEO::ExecutionEnvironment();
        executionEnvironment->prepareRootDeviceEnvironments(numRootDevices);
        for (uint32_t i = 0; i < numRootDevices; i++) {
            executionEnvironment->rootDeviceEnvironments[i]->setHwInfo(NEO::defaultHwInfo.get());
        }

        memoryManager = new MemoryManagerHostPointer(*executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);

        neoDevice = NEO::MockDevice::create<NEO::MockDevice>(executionEnvironment, rootDeviceIndex);
        std::vector<std::unique_ptr<NEO::Device>> devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));

        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));

        device = driverHandle->devices[0];
    }
    void TearDown() override {
    }

    NEO::ExecutionEnvironment *executionEnvironment = nullptr;
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::MockDevice *neoDevice = nullptr;
    L0::Device *device = nullptr;
    MemoryManagerHostPointer *memoryManager = nullptr;
    const uint32_t rootDeviceIndex = 1u;
    const uint32_t numRootDevices = 2u;
};

TEST_F(DeviceHostPointerTest, givenHostPointerNotAcceptedByKernelThenNewAllocationIsCreatedAndHostPointerCopied) {
    size_t size = 55;
    uint64_t *buffer = new uint64_t[size];
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = i + 10;
    }

    auto allocation = device->allocateMemoryFromHostPtr(buffer, size);
    EXPECT_NE(nullptr, allocation);
    EXPECT_EQ(NEO::GraphicsAllocation::AllocationType::INTERNAL_HOST_MEMORY, allocation->getAllocationType());
    EXPECT_EQ(rootDeviceIndex, allocation->getRootDeviceIndex());
    EXPECT_NE(allocation->getUnderlyingBuffer(), reinterpret_cast<void *>(buffer));
    EXPECT_EQ(allocation->getUnderlyingBufferSize(), size);
    EXPECT_EQ(0, memcmp(buffer, allocation->getUnderlyingBuffer(), size));

    neoDevice->getMemoryManager()->freeGraphicsMemory(allocation);
    delete[] buffer;
}

TEST_F(DeviceTest, givenKernelPropertiesStructureWhenKernelPropertiesCalledThenAllPropertiesAreAssigned) {
    const auto &hardwareInfo = this->neoDevice->getHardwareInfo();

    ze_device_module_properties_t kernelProperties, kernelPropertiesBefore;
    memset(&kernelProperties, std::numeric_limits<int>::max(), sizeof(ze_device_module_properties_t));
    kernelPropertiesBefore = kernelProperties;
    device->getKernelProperties(&kernelProperties);

    EXPECT_NE(kernelPropertiesBefore.spirvVersionSupported, kernelProperties.spirvVersionSupported);
    EXPECT_NE(kernelPropertiesBefore.nativeKernelSupported.id, kernelProperties.nativeKernelSupported.id);

    EXPECT_TRUE(kernelPropertiesBefore.flags & ZE_DEVICE_MODULE_FLAG_FP16);
    if (hardwareInfo.capabilityTable.ftrSupportsInteger64BitAtomics) {
        EXPECT_TRUE(kernelPropertiesBefore.flags & ZE_DEVICE_MODULE_FLAG_INT64_ATOMICS);
    }

    EXPECT_NE(kernelPropertiesBefore.maxArgumentsSize, kernelProperties.maxArgumentsSize);
    EXPECT_NE(kernelPropertiesBefore.printfBufferSize, kernelProperties.printfBufferSize);
}

TEST_F(DeviceTest, givenDeviceCachePropertiesThenAllPropertiesAreAssigned) {
    ze_device_cache_properties_t deviceCacheProperties, deviceCachePropertiesBefore;

    deviceCacheProperties.cacheSize = std::numeric_limits<size_t>::max();

    deviceCachePropertiesBefore = deviceCacheProperties;

    uint32_t count = 0;
    ze_result_t res = device->getCacheProperties(&count, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(count, 1u);

    res = device->getCacheProperties(&count, &deviceCacheProperties);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    EXPECT_NE(deviceCacheProperties.cacheSize, deviceCachePropertiesBefore.cacheSize);
}

TEST_F(DeviceTest, givenDevicePropertiesStructureWhenDevicePropertiesCalledThenAllPropertiesAreAssigned) {
    ze_device_properties_t deviceProperties, devicePropertiesBefore;

    deviceProperties.type = ZE_DEVICE_TYPE_FPGA;
    memset(&deviceProperties.vendorId, std::numeric_limits<int>::max(), sizeof(deviceProperties.vendorId));
    memset(&deviceProperties.deviceId, std::numeric_limits<int>::max(), sizeof(deviceProperties.deviceId));
    memset(&deviceProperties.uuid, std::numeric_limits<int>::max(), sizeof(deviceProperties.uuid));
    memset(&deviceProperties.subdeviceId, std::numeric_limits<int>::max(), sizeof(deviceProperties.subdeviceId));
    memset(&deviceProperties.coreClockRate, std::numeric_limits<int>::max(), sizeof(deviceProperties.coreClockRate));
    memset(&deviceProperties.maxCommandQueuePriority, std::numeric_limits<int>::max(), sizeof(deviceProperties.maxCommandQueuePriority));
    memset(&deviceProperties.maxHardwareContexts, std::numeric_limits<int>::max(), sizeof(deviceProperties.maxHardwareContexts));
    memset(&deviceProperties.numThreadsPerEU, std::numeric_limits<int>::max(), sizeof(deviceProperties.numThreadsPerEU));
    memset(&deviceProperties.physicalEUSimdWidth, std::numeric_limits<int>::max(), sizeof(deviceProperties.physicalEUSimdWidth));
    memset(&deviceProperties.numEUsPerSubslice, std::numeric_limits<int>::max(), sizeof(deviceProperties.numEUsPerSubslice));
    memset(&deviceProperties.numSubslicesPerSlice, std::numeric_limits<int>::max(), sizeof(deviceProperties.numSubslicesPerSlice));
    memset(&deviceProperties.numSlices, std::numeric_limits<int>::max(), sizeof(deviceProperties.numSlices));
    memset(&deviceProperties.timerResolution, std::numeric_limits<int>::max(), sizeof(deviceProperties.timerResolution));
    memset(&deviceProperties.timestampValidBits, std::numeric_limits<uint32_t>::max(), sizeof(deviceProperties.timestampValidBits));
    memset(&deviceProperties.kernelTimestampValidBits, std::numeric_limits<uint32_t>::max(), sizeof(deviceProperties.kernelTimestampValidBits));
    memset(&deviceProperties.name, std::numeric_limits<int>::max(), sizeof(deviceProperties.name));
    deviceProperties.maxMemAllocSize = 0;

    devicePropertiesBefore = deviceProperties;
    device->getProperties(&deviceProperties);

    EXPECT_NE(deviceProperties.type, devicePropertiesBefore.type);
    EXPECT_NE(deviceProperties.vendorId, devicePropertiesBefore.vendorId);
    EXPECT_NE(deviceProperties.deviceId, devicePropertiesBefore.deviceId);
    EXPECT_NE(0, memcmp(&deviceProperties.uuid, &devicePropertiesBefore.uuid, sizeof(devicePropertiesBefore.uuid)));
    EXPECT_NE(deviceProperties.subdeviceId, devicePropertiesBefore.subdeviceId);
    EXPECT_NE(deviceProperties.coreClockRate, devicePropertiesBefore.coreClockRate);
    EXPECT_NE(deviceProperties.maxCommandQueuePriority, devicePropertiesBefore.maxCommandQueuePriority);
    EXPECT_NE(deviceProperties.maxHardwareContexts, devicePropertiesBefore.maxHardwareContexts);
    EXPECT_NE(deviceProperties.numThreadsPerEU, devicePropertiesBefore.numThreadsPerEU);
    EXPECT_NE(deviceProperties.physicalEUSimdWidth, devicePropertiesBefore.physicalEUSimdWidth);
    EXPECT_NE(deviceProperties.numEUsPerSubslice, devicePropertiesBefore.numEUsPerSubslice);
    EXPECT_NE(deviceProperties.numSubslicesPerSlice, devicePropertiesBefore.numSubslicesPerSlice);
    EXPECT_NE(deviceProperties.numSlices, devicePropertiesBefore.numSlices);
    EXPECT_NE(deviceProperties.timerResolution, devicePropertiesBefore.timerResolution);
    EXPECT_NE(deviceProperties.timestampValidBits, devicePropertiesBefore.timestampValidBits);
    EXPECT_NE(deviceProperties.kernelTimestampValidBits, devicePropertiesBefore.kernelTimestampValidBits);
    EXPECT_NE(0, memcmp(&deviceProperties.name, &devicePropertiesBefore.name, sizeof(devicePropertiesBefore.name)));
    EXPECT_NE(deviceProperties.maxMemAllocSize, devicePropertiesBefore.maxMemAllocSize);
}

TEST_F(DeviceTest, givenCallToDevicePropertiesThenMaximumMemoryToBeAllocatedIsCorrectlyReturned) {
    ze_device_properties_t deviceProperties;
    deviceProperties.maxMemAllocSize = 0;
    device->getProperties(&deviceProperties);
    EXPECT_EQ(deviceProperties.maxMemAllocSize, this->neoDevice->getDeviceInfo().maxMemAllocSize);
}

TEST_F(DeviceTest, whenGetDevicePropertiesCalledThenCorrectDevicePropertyEccFlagSet) {
    ze_device_properties_t deviceProps;

    device->getProperties(&deviceProps);
    auto expected = (this->neoDevice->getDeviceInfo().errorCorrectionSupport) ? ZE_DEVICE_PROPERTY_FLAG_ECC : static_cast<ze_device_property_flag_t>(0u);
    EXPECT_EQ(expected, deviceProps.flags & ZE_DEVICE_PROPERTY_FLAG_ECC);
}

TEST_F(DeviceTest, givenCommandQueuePropertiesCallThenCallSucceeds) {
    uint32_t count = 0;
    ze_result_t res = device->getCommandQueueGroupProperties(&count, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_GE(count, 1u);

    std::vector<ze_command_queue_group_properties_t> queueProperties(count);
    res = device->getCommandQueueGroupProperties(&count, queueProperties.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
}

TEST_F(DeviceTest, givenCallToDevicePropertiesThenTimestampValidBitsAreCorrectlyAssigned) {
    ze_device_properties_t deviceProps;

    device->getProperties(&deviceProps);
    EXPECT_EQ(36u, deviceProps.timestampValidBits);
    EXPECT_EQ(32u, deviceProps.kernelTimestampValidBits);
}

TEST_F(DeviceTest, whenGetExternalMemoryPropertiesIsCalledThenSuccessIsReturnedAndNoPropertiesAreReturned) {
    ze_device_external_memory_properties_t externalMemoryProperties;

    ze_result_t result = device->getExternalMemoryProperties(&externalMemoryProperties);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_FALSE(externalMemoryProperties.imageExportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD);
    EXPECT_FALSE(externalMemoryProperties.imageExportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF);
    EXPECT_FALSE(externalMemoryProperties.imageImportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD);
    EXPECT_FALSE(externalMemoryProperties.imageImportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF);
    EXPECT_FALSE(externalMemoryProperties.memoryAllocationExportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD);
    EXPECT_TRUE(externalMemoryProperties.memoryAllocationExportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF);
    EXPECT_FALSE(externalMemoryProperties.memoryAllocationImportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD);
    EXPECT_TRUE(externalMemoryProperties.memoryAllocationImportTypes & ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF);
}

using DeviceGetMemoryTests = DeviceTest;

TEST_F(DeviceGetMemoryTests, whenCallingGetMemoryPropertiesWithCountZeroThenOneIsReturned) {
    uint32_t count = 0;
    ze_result_t res = device->getMemoryProperties(&count, nullptr);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    EXPECT_EQ(1u, count);
}

TEST_F(DeviceGetMemoryTests, whenCallingGetMemoryPropertiesWithNullPtrThenInvalidArgumentIsReturned) {
    uint32_t count = 0;
    ze_result_t res = device->getMemoryProperties(&count, nullptr);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    EXPECT_EQ(1u, count);

    count++;
    res = device->getMemoryProperties(&count, nullptr);
    EXPECT_EQ(res, ZE_RESULT_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(1u, count);
}

TEST_F(DeviceGetMemoryTests, whenCallingGetMemoryPropertiesWithNonNullPtrThenPropertiesAreReturned) {
    uint32_t count = 0;
    ze_result_t res = device->getMemoryProperties(&count, nullptr);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    EXPECT_EQ(1u, count);

    count++;
    ze_device_memory_properties_t memProperties = {};
    res = device->getMemoryProperties(&count, &memProperties);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    EXPECT_EQ(1u, count);

    EXPECT_EQ(memProperties.maxClockRate, this->neoDevice->getDeviceInfo().maxClockFrequency);
    EXPECT_EQ(memProperties.maxBusWidth, this->neoDevice->getDeviceInfo().addressBits);
    EXPECT_EQ(memProperties.totalSize, this->neoDevice->getDeviceInfo().globalMemSize);
}

struct DeviceHasNoDoubleFp64Test : public ::testing::Test {
    void SetUp() override {
        DebugManager.flags.CreateMultipleRootDevices.set(numRootDevices);
        HardwareInfo nonFp64Device = *defaultHwInfo;
        nonFp64Device.capabilityTable.ftrSupportsFP64 = false;
        nonFp64Device.capabilityTable.ftrSupports64BitMath = false;
        neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&nonFp64Device, rootDeviceIndex);
        NEO::DeviceVector devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
        device = driverHandle->devices[0];
    }

    DebugManagerStateRestore restorer;
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::Device *neoDevice = nullptr;
    L0::Device *device = nullptr;
    const uint32_t rootDeviceIndex = 1u;
    const uint32_t numRootDevices = 2u;
};

TEST_F(DeviceHasNoDoubleFp64Test, givenDeviceThatDoesntHaveFp64WhenDbgFlagEnablesFp64ThenReportFp64Flags) {
    ze_device_module_properties_t kernelProperties;
    memset(&kernelProperties, std::numeric_limits<int>::max(), sizeof(ze_device_module_properties_t));

    device->getKernelProperties(&kernelProperties);
    EXPECT_FALSE(kernelProperties.flags & ZE_DEVICE_MODULE_FLAG_FP64);
    EXPECT_EQ(0u, kernelProperties.fp64flags);

    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.OverrideDefaultFP64Settings.set(1);

    device->getKernelProperties(&kernelProperties);
    EXPECT_TRUE(kernelProperties.flags & ZE_DEVICE_MODULE_FLAG_FP64);
    EXPECT_TRUE(kernelProperties.fp64flags & ZE_DEVICE_FP_FLAG_ROUND_TO_NEAREST);
    EXPECT_TRUE(kernelProperties.fp64flags & ZE_DEVICE_FP_FLAG_ROUND_TO_ZERO);
    EXPECT_TRUE(kernelProperties.fp64flags & ZE_DEVICE_FP_FLAG_ROUND_TO_INF);
    EXPECT_TRUE(kernelProperties.fp64flags & ZE_DEVICE_FP_FLAG_INF_NAN);
    EXPECT_TRUE(kernelProperties.fp64flags & ZE_DEVICE_FP_FLAG_DENORM);
    EXPECT_TRUE(kernelProperties.fp64flags & ZE_DEVICE_FP_FLAG_FMA);
}

struct DeviceHasNo64BitAtomicTest : public ::testing::Test {
    void SetUp() override {
        HardwareInfo nonFp64Device = *defaultHwInfo;
        nonFp64Device.capabilityTable.ftrSupportsInteger64BitAtomics = false;
        neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&nonFp64Device, rootDeviceIndex);
        NEO::DeviceVector devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
        device = driverHandle->devices[rootDeviceIndex];
    }

    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::Device *neoDevice = nullptr;
    L0::Device *device = nullptr;
    const uint32_t rootDeviceIndex = 0u;
};

TEST_F(DeviceHasNo64BitAtomicTest, givenDeviceWithNoSupportForInteger64BitAtomicsThenFlagsAreSetCorrectly) {
    ze_device_module_properties_t kernelProperties;
    memset(&kernelProperties, std::numeric_limits<int>::max(), sizeof(ze_device_module_properties_t));

    device->getKernelProperties(&kernelProperties);
    EXPECT_TRUE(kernelProperties.flags & ZE_DEVICE_MODULE_FLAG_FP16);
    EXPECT_FALSE(kernelProperties.flags & ZE_DEVICE_MODULE_FLAG_INT64_ATOMICS);
}

struct DeviceHas64BitAtomicTest : public ::testing::Test {
    void SetUp() override {
        HardwareInfo nonFp64Device = *defaultHwInfo;
        nonFp64Device.capabilityTable.ftrSupportsInteger64BitAtomics = true;
        neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&nonFp64Device, rootDeviceIndex);
        NEO::DeviceVector devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
        device = driverHandle->devices[rootDeviceIndex];
    }

    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::Device *neoDevice = nullptr;
    L0::Device *device = nullptr;
    const uint32_t rootDeviceIndex = 0u;
};

TEST_F(DeviceHas64BitAtomicTest, givenDeviceWithSupportForInteger64BitAtomicsThenFlagsAreSetCorrectly) {
    ze_device_module_properties_t kernelProperties;
    memset(&kernelProperties, std::numeric_limits<int>::max(), sizeof(ze_device_module_properties_t));

    device->getKernelProperties(&kernelProperties);
    EXPECT_TRUE(kernelProperties.flags & ZE_DEVICE_MODULE_FLAG_FP16);
    EXPECT_TRUE(kernelProperties.flags & ZE_DEVICE_MODULE_FLAG_INT64_ATOMICS);
}

struct MockMemoryManagerMultiDevice : public MemoryManagerMock {
    MockMemoryManagerMultiDevice(NEO::ExecutionEnvironment &executionEnvironment) : MemoryManagerMock(const_cast<NEO::ExecutionEnvironment &>(executionEnvironment)) {}
};

struct MultipleDevicesTest : public ::testing::Test {
    void SetUp() override {
        DebugManager.flags.CreateMultipleSubDevices.set(numSubDevices);
        VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);

        std::vector<std::unique_ptr<NEO::Device>> devices;
        NEO::ExecutionEnvironment *executionEnvironment = new NEO::ExecutionEnvironment();
        executionEnvironment->prepareRootDeviceEnvironments(numRootDevices);
        for (auto i = 0u; i < executionEnvironment->rootDeviceEnvironments.size(); i++) {
            executionEnvironment->rootDeviceEnvironments[i]->setHwInfo(NEO::defaultHwInfo.get());
        }

        memoryManager = new ::testing::NiceMock<MockMemoryManagerMultiDevice>(*executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);
        deviceFactory = std::make_unique<UltDeviceFactory>(numRootDevices, numSubDevices, *executionEnvironment);

        for (auto i = 0u; i < executionEnvironment->rootDeviceEnvironments.size(); i++) {
            devices.push_back(std::unique_ptr<NEO::Device>(deviceFactory->rootDevices[i]));
        }
        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
    }

    DebugManagerStateRestore restorer;
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    MockMemoryManagerMultiDevice *memoryManager = nullptr;
    std::unique_ptr<UltDeviceFactory> deviceFactory;

    const uint32_t numRootDevices = 2u;
    const uint32_t numSubDevices = 2u;
};

TEST_F(MultipleDevicesTest, whenRetrievingNumberOfSubdevicesThenCorrectNumberIsReturned) {
    L0::Device *device0 = driverHandle->devices[0];

    uint32_t count = 0;
    auto result = device0->getSubDevices(&count, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(numSubDevices, count);

    std::vector<ze_device_handle_t> subDevices(count);
    count++;
    result = device0->getSubDevices(&count, subDevices.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(numSubDevices, count);
    for (auto subDevice : subDevices) {
        EXPECT_NE(nullptr, subDevice);
        EXPECT_TRUE(static_cast<DeviceImp *>(subDevice)->isSubdevice);
    }
}

TEST_F(MultipleDevicesTest, whenRetriecingSubDevicePropertiesThenCorrectFlagIsSet) {
    L0::Device *device0 = driverHandle->devices[0];

    uint32_t count = 0;
    auto result = device0->getSubDevices(&count, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(numSubDevices, count);

    std::vector<ze_device_handle_t> subDevices(count);
    count++;
    result = device0->getSubDevices(&count, subDevices.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(numSubDevices, count);

    ze_device_properties_t deviceProps;

    L0::Device *subdevice0 = static_cast<L0::Device *>(subDevices[0]);
    subdevice0->getProperties(&deviceProps);
    EXPECT_EQ(ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE, deviceProps.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE);
}

TEST_F(MultipleDevicesTest, givenTheSameDeviceThenCanAccessPeerReturnsTrue) {
    L0::Device *device0 = driverHandle->devices[0];

    ze_bool_t canAccess = false;
    ze_result_t res = device0->canAccessPeer(device0->toHandle(), &canAccess);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_TRUE(canAccess);
}

TEST_F(MultipleDevicesTest, givenTwoRootDevicesFromSameFamilyThenCanAccessPeerReturnsFalse) {
    L0::Device *device0 = driverHandle->devices[0];
    L0::Device *device1 = driverHandle->devices[1];

    GFXCORE_FAMILY device0Family = device0->getNEODevice()->getHardwareInfo().platform.eRenderCoreFamily;
    GFXCORE_FAMILY device1Family = device1->getNEODevice()->getHardwareInfo().platform.eRenderCoreFamily;
    EXPECT_EQ(device0Family, device1Family);

    ze_bool_t canAccess = true;
    ze_result_t res = device0->canAccessPeer(device1->toHandle(), &canAccess);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_FALSE(canAccess);
}

TEST_F(MultipleDevicesTest, givenTwoRootDevicesFromSameFamilyThenCanAccessPeerReturnsTrueIfEnableCrossDeviceAccessIsSetToOne) {
    DebugManager.flags.EnableCrossDeviceAccess.set(1);
    L0::Device *device0 = driverHandle->devices[0];
    L0::Device *device1 = driverHandle->devices[1];

    GFXCORE_FAMILY device0Family = device0->getNEODevice()->getHardwareInfo().platform.eRenderCoreFamily;
    GFXCORE_FAMILY device1Family = device1->getNEODevice()->getHardwareInfo().platform.eRenderCoreFamily;
    EXPECT_EQ(device0Family, device1Family);

    ze_bool_t canAccess = false;
    ze_result_t res = device0->canAccessPeer(device1->toHandle(), &canAccess);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_TRUE(canAccess);
}

TEST_F(MultipleDevicesTest, givenTwoSubDevicesFromTheSameRootDeviceThenCanAccessPeerReturnsTrue) {
    L0::Device *device0 = driverHandle->devices[0];
    L0::Device *device1 = driverHandle->devices[1];

    uint32_t subDeviceCount = 0;
    ze_result_t res = device0->getSubDevices(&subDeviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(numSubDevices, subDeviceCount);

    std::vector<ze_device_handle_t> subDevices0(subDeviceCount);
    res = device0->getSubDevices(&subDeviceCount, subDevices0.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    subDeviceCount = 0;
    res = device1->getSubDevices(&subDeviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(numSubDevices, subDeviceCount);

    std::vector<ze_device_handle_t> subDevices1(subDeviceCount);
    res = device1->getSubDevices(&subDeviceCount, subDevices1.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    ze_bool_t canAccess = false;
    L0::Device *subDevice0_0 = Device::fromHandle(subDevices0[0]);
    subDevice0_0->canAccessPeer(subDevices0[1], &canAccess);
    EXPECT_TRUE(canAccess);

    canAccess = false;
    L0::Device *subDevice1_0 = Device::fromHandle(subDevices1[0]);
    subDevice1_0->canAccessPeer(subDevices1[1], &canAccess);
    EXPECT_TRUE(canAccess);
}

TEST_F(MultipleDevicesTest, givenTwoSubDevicesFromTheSameRootDeviceThenCanAccessPeerReturnsFalseIfEnableCrossDeviceAccessIsSetToZero) {
    DebugManager.flags.EnableCrossDeviceAccess.set(0);
    L0::Device *device0 = driverHandle->devices[0];
    L0::Device *device1 = driverHandle->devices[1];

    uint32_t subDeviceCount = 0;
    ze_result_t res = device0->getSubDevices(&subDeviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(numSubDevices, subDeviceCount);

    std::vector<ze_device_handle_t> subDevices0(subDeviceCount);
    res = device0->getSubDevices(&subDeviceCount, subDevices0.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    subDeviceCount = 0;
    res = device1->getSubDevices(&subDeviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_EQ(numSubDevices, subDeviceCount);

    std::vector<ze_device_handle_t> subDevices1(subDeviceCount);
    res = device1->getSubDevices(&subDeviceCount, subDevices1.data());
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);

    ze_bool_t canAccess = true;
    L0::Device *subDevice0_0 = Device::fromHandle(subDevices0[0]);
    subDevice0_0->canAccessPeer(subDevices0[1], &canAccess);
    EXPECT_FALSE(canAccess);

    canAccess = true;
    L0::Device *subDevice1_0 = Device::fromHandle(subDevices1[0]);
    subDevice1_0->canAccessPeer(subDevices1[1], &canAccess);
    EXPECT_FALSE(canAccess);
}

struct MultipleDevicesDifferentLocalMemorySupportTest : public MultipleDevicesTest {
    void SetUp() override {
        MultipleDevicesTest::SetUp();
        memoryManager->localMemorySupported[0] = 1;

        deviceWithLocalMemory = driverHandle->devices[0];
        deviceWithoutLocalMemory = driverHandle->devices[1];
    }

    L0::Device *deviceWithLocalMemory = nullptr;
    L0::Device *deviceWithoutLocalMemory = nullptr;
};

TEST_F(MultipleDevicesDifferentLocalMemorySupportTest, givenTwoDevicesWithDifferentLocalMemorySupportThenCanAccessPeerReturnsFalse) {
    ze_bool_t canAccess = true;
    ze_result_t res = deviceWithLocalMemory->canAccessPeer(deviceWithoutLocalMemory->toHandle(), &canAccess);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_FALSE(canAccess);
}

struct MultipleDevicesDifferentFamilyAndLocalMemorySupportTest : public MultipleDevicesTest {
    void SetUp() override {
        if ((NEO::HwInfoConfig::get(IGFX_SKYLAKE) == nullptr) ||
            (NEO::HwInfoConfig::get(IGFX_KABYLAKE) == nullptr)) {
            GTEST_SKIP();
        }

        MultipleDevicesTest::SetUp();

        memoryManager->localMemorySupported[0] = 1;
        memoryManager->localMemorySupported[1] = 1;

        deviceSKL = driverHandle->devices[0];
        deviceKBL = driverHandle->devices[1];

        deviceSKL->getNEODevice()->getRootDeviceEnvironment().getMutableHardwareInfo()->platform.eProductFamily = IGFX_SKYLAKE;
        deviceKBL->getNEODevice()->getRootDeviceEnvironment().getMutableHardwareInfo()->platform.eProductFamily = IGFX_KABYLAKE;
    }

    L0::Device *deviceSKL = nullptr;
    L0::Device *deviceKBL = nullptr;
};

TEST_F(MultipleDevicesDifferentFamilyAndLocalMemorySupportTest, givenTwoDevicesFromDifferentFamiliesThenCanAccessPeerReturnsFalse) {
    PRODUCT_FAMILY deviceSKLFamily = deviceSKL->getNEODevice()->getHardwareInfo().platform.eProductFamily;
    PRODUCT_FAMILY deviceKBLFamily = deviceKBL->getNEODevice()->getHardwareInfo().platform.eProductFamily;
    EXPECT_NE(deviceSKLFamily, deviceKBLFamily);

    ze_bool_t canAccess = true;
    ze_result_t res = deviceSKL->canAccessPeer(deviceKBL->toHandle(), &canAccess);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_FALSE(canAccess);
}

struct MultipleDevicesSameFamilyAndLocalMemorySupportTest : public MultipleDevicesTest {
    void SetUp() override {
        MultipleDevicesTest::SetUp();

        memoryManager->localMemorySupported[0] = 1;
        memoryManager->localMemorySupported[1] = 1;

        device0 = driverHandle->devices[0];
        device1 = driverHandle->devices[1];
    }

    L0::Device *device0 = nullptr;
    L0::Device *device1 = nullptr;
};

TEST_F(MultipleDevicesSameFamilyAndLocalMemorySupportTest, givenTwoDevicesFromSameFamilyThenCanAccessPeerReturnsFalse) {
    PRODUCT_FAMILY device0Family = device0->getNEODevice()->getHardwareInfo().platform.eProductFamily;
    PRODUCT_FAMILY device1Family = device1->getNEODevice()->getHardwareInfo().platform.eProductFamily;
    EXPECT_EQ(device0Family, device1Family);

    ze_bool_t canAccess = true;
    ze_result_t res = device0->canAccessPeer(device1->toHandle(), &canAccess);
    EXPECT_EQ(ZE_RESULT_SUCCESS, res);
    EXPECT_FALSE(canAccess);
}

TEST_F(DeviceTest, givenNoActiveSourceLevelDebuggerWhenGetIsCalledThenNullptrIsReturned) {
    EXPECT_EQ(nullptr, device->getSourceLevelDebugger());
}

TEST_F(DeviceTest, givenNoL0DebuggerWhenGettingL0DebuggerThenNullptrReturned) {
    EXPECT_EQ(nullptr, device->getL0Debugger());
}

TEST_F(DeviceTest, givenValidDeviceWhenCallingReleaseResourcesThenResourcesReleased) {
    auto deviceImp = static_cast<DeviceImp *>(device);
    EXPECT_FALSE(deviceImp->resourcesReleased);
    EXPECT_FALSE(nullptr == deviceImp->neoDevice);
    deviceImp->releaseResources();
    EXPECT_TRUE(deviceImp->resourcesReleased);
    EXPECT_TRUE(nullptr == deviceImp->neoDevice);
    EXPECT_TRUE(nullptr == deviceImp->pageFaultCommandList);
    EXPECT_TRUE(nullptr == deviceImp->getDebugSurface());
    deviceImp->releaseResources();
    EXPECT_TRUE(deviceImp->resourcesReleased);
}

TEST(DevicePropertyFlagIsIntegratedTest, givenIntegratedDeviceThenCorrectDevicePropertyFlagSet) {
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::HardwareInfo hwInfo = *NEO::defaultHwInfo.get();
    hwInfo.capabilityTable.isIntegratedDevice = true;

    NEO::MockDevice *neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&hwInfo);
    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));
    auto device = driverHandle->devices[0];

    ze_device_properties_t deviceProps;

    device->getProperties(&deviceProps);
    EXPECT_EQ(ZE_DEVICE_PROPERTY_FLAG_INTEGRATED, deviceProps.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED);
}

TEST(DevicePropertyFlagDiscreteDeviceTest, givenDiscreteDeviceThenCorrectDevicePropertyFlagSet) {
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::HardwareInfo hwInfo = *NEO::defaultHwInfo.get();
    hwInfo.capabilityTable.isIntegratedDevice = false;

    NEO::MockDevice *neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(&hwInfo);
    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));
    auto device = driverHandle->devices[0];

    ze_device_properties_t deviceProps;

    device->getProperties(&deviceProps);
    EXPECT_EQ(0u, deviceProps.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED);
}

TEST(zeDevice, givenValidImagePropertiesStructGetImageProperties) {
    Mock<Device> device;
    ze_result_t result;
    ze_device_image_properties_t imageProperties;

    EXPECT_CALL(device, getDeviceImageProperties(&imageProperties))
        .Times(1)
        .WillRepeatedly(Return(ZE_RESULT_SUCCESS));

    result = zeDeviceGetImageProperties(device.toHandle(), &imageProperties);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

} // namespace ult
} // namespace L0
