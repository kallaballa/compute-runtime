/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/sub_device.h"
#include "shared/source/os_interface/os_context.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/ult_hw_config.h"
#include "shared/test/common/helpers/variable_backup.h"
#include "shared/test/common/mocks/ult_device_factory.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/test/unit_test/mocks/mock_cl_device.h"
#include "opencl/test/unit_test/mocks/mock_memory_manager.h"
#include "opencl/test/unit_test/mocks/mock_platform.h"

using namespace NEO;

TEST(SubDevicesTest, givenDefaultConfigWhenCreateRootDeviceThenItDoesntContainSubDevices) {
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    EXPECT_EQ(0u, device->getNumSubDevices());
    EXPECT_EQ(1u, device->getNumAvailableDevices());
}

TEST(SubDevicesTest, givenCreateMultipleSubDevicesFlagSetWhenCreateRootDeviceThenItsSubdevicesHaveProperRootIdSet) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    EXPECT_EQ(2u, device->getNumSubDevices());
    EXPECT_EQ(0u, device->getRootDeviceIndex());

    EXPECT_EQ(0u, device->subdevices.at(0)->getRootDeviceIndex());
    EXPECT_EQ(0u, device->subdevices.at(0)->getSubDeviceIndex());

    EXPECT_EQ(0u, device->subdevices.at(1)->getRootDeviceIndex());
    EXPECT_EQ(1u, device->subdevices.at(1)->getSubDeviceIndex());
}

TEST(SubDevicesTest, givenCreateMultipleSubDevicesFlagSetWhenCreateRootDeviceThenItContainsSubDevices) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    EXPECT_EQ(2u, device->getNumSubDevices());

    EXPECT_EQ(2u, device->getNumAvailableDevices());
    EXPECT_EQ(1u, device->subdevices.at(0)->getNumAvailableDevices());
    EXPECT_EQ(1u, device->subdevices.at(1)->getNumAvailableDevices());
}

TEST(SubDevicesTest, givenDeviceWithSubDevicesWhenSubDeviceApiRefCountsAreChangedThenChangeIsPropagatedToRootDevice) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    initPlatform();
    auto nonDefaultPlatform = std::make_unique<MockPlatform>(*platform()->peekExecutionEnvironment());
    nonDefaultPlatform->initializeWithNewDevices();
    auto device = nonDefaultPlatform->getClDevice(0);
    auto defaultDevice = platform()->getClDevice(0);

    auto subDevice = device->getDeviceById(1);
    auto baseDeviceApiRefCount = device->getRefApiCount();
    auto baseDeviceInternalRefCount = device->getRefInternalCount();
    auto baseSubDeviceApiRefCount = subDevice->getRefApiCount();
    auto baseSubDeviceInternalRefCount = subDevice->getRefInternalCount();
    auto baseDefaultDeviceApiRefCount = defaultDevice->getRefApiCount();
    auto baseDefaultDeviceInternalRefCount = defaultDevice->getRefInternalCount();

    subDevice->retainApi();
    EXPECT_EQ(baseDeviceApiRefCount, device->getRefApiCount());
    EXPECT_EQ(baseDeviceInternalRefCount + 1, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceApiRefCount + 1, subDevice->getRefApiCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount + 1, subDevice->getRefInternalCount());
    EXPECT_EQ(baseDefaultDeviceApiRefCount, defaultDevice->getRefApiCount());
    EXPECT_EQ(baseDefaultDeviceInternalRefCount, defaultDevice->getRefInternalCount());

    subDevice->releaseApi();
    EXPECT_EQ(baseDeviceApiRefCount, device->getRefApiCount());
    EXPECT_EQ(baseDeviceInternalRefCount, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceApiRefCount, subDevice->getRefApiCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());
    EXPECT_EQ(baseDefaultDeviceApiRefCount, defaultDevice->getRefApiCount());
    EXPECT_EQ(baseDefaultDeviceInternalRefCount, defaultDevice->getRefInternalCount());
}

TEST(SubDevicesTest, givenDeviceWithSubDevicesWhenSubDeviceInternalRefCountsAreChangedThenChangeIsPropagatedToRootDevice) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));
    device->incRefInternal();
    auto subDevice = device->getDeviceById(0);

    auto baseDeviceInternalRefCount = device->getRefInternalCount();
    auto baseSubDeviceInternalRefCount = subDevice->getRefInternalCount();

    subDevice->incRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount + 1, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());

    device->incRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount + 2, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());

    subDevice->decRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount + 1, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());

    device->decRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());
}

TEST(SubDevicesTest, givenClDeviceWithSubDevicesWhenSubDeviceInternalRefCountsAreChangedThenChangeIsPropagatedToRootDevice) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));
    device->incRefInternal();
    auto &subDevice = device->subDevices[0];

    auto baseDeviceInternalRefCount = device->getRefInternalCount();
    auto baseSubDeviceInternalRefCount = subDevice->getRefInternalCount();

    subDevice->incRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount + 1, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());

    device->incRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount + 2, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());

    subDevice->decRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount + 1, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());

    device->decRefInternal();
    EXPECT_EQ(baseDeviceInternalRefCount, device->getRefInternalCount());
    EXPECT_EQ(baseSubDeviceInternalRefCount, subDevice->getRefInternalCount());
}

TEST(SubDevicesTest, givenDeviceWithSubDevicesWhenSubDeviceCreationFailThenWholeDeviceIsDestroyed) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(10);
    MockExecutionEnvironment executionEnvironment;
    executionEnvironment.prepareRootDeviceEnvironments(1);
    executionEnvironment.incRefInternal();
    executionEnvironment.memoryManager.reset(new FailMemoryManager(10, executionEnvironment));
    auto device = Device::create<RootDevice>(&executionEnvironment, 0u);
    EXPECT_EQ(nullptr, device);
}

TEST(SubDevicesTest, givenCreateMultipleRootDevicesFlagsEnabledWhenDevicesAreCreatedThenEachHasUniqueDeviceIndex) {

    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleRootDevices.set(2);

    VariableBackup<UltHwConfig> backup{&ultHwConfig};
    ultHwConfig.useMockedPrepareDeviceEnvironmentsFunc = false;
    initPlatform();
    EXPECT_EQ(0u, platform()->getClDevice(0)->getRootDeviceIndex());
    EXPECT_EQ(1u, platform()->getClDevice(1)->getRootDeviceIndex());
}

TEST(SubDevicesTest, givenRootDeviceWithSubDevicesWhenOsContextIsCreatedThenItsBitfieldBasesOnSubDevicesCount) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));
    EXPECT_EQ(2u, device->getNumSubDevices());

    uint32_t rootDeviceBitfield = 0b11;
    EXPECT_EQ(rootDeviceBitfield, static_cast<uint32_t>(device->getDefaultEngine().osContext->getDeviceBitfield().to_ulong()));
}

TEST(SubDevicesTest, givenSubDeviceWhenOsContextIsCreatedThenItsBitfieldBasesOnSubDeviceId) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    EXPECT_EQ(2u, device->getNumSubDevices());

    auto firstSubDevice = static_cast<SubDevice *>(device->subdevices.at(0));
    auto secondSubDevice = static_cast<SubDevice *>(device->subdevices.at(1));
    uint32_t firstSubDeviceMask = (1u << 0);
    uint32_t secondSubDeviceMask = (1u << 1);
    EXPECT_EQ(firstSubDeviceMask, static_cast<uint32_t>(firstSubDevice->getDefaultEngine().osContext->getDeviceBitfield().to_ulong()));
    EXPECT_EQ(secondSubDeviceMask, static_cast<uint32_t>(secondSubDevice->getDefaultEngine().osContext->getDeviceBitfield().to_ulong()));
}

TEST(SubDevicesTest, givenDeviceWithoutSubDevicesWhenGettingDeviceByIdZeroThenGetThisDevice) {
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    EXPECT_EQ(1u, device->getNumAvailableDevices());
    EXPECT_EQ(device.get(), device->getDeviceById(0u));
}

TEST(SubDevicesTest, givenDeviceWithSubDevicesWhenGettingDeviceByIdThenGetCorrectSubDevice) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));
    EXPECT_EQ(2u, device->getNumSubDevices());
    EXPECT_EQ(device->subdevices.at(0), device->getDeviceById(0));
    EXPECT_EQ(device->subdevices.at(1), device->getDeviceById(1));
    EXPECT_THROW(device->getDeviceById(2), std::exception);
}

TEST(SubDevicesTest, givenSubDevicesWhenGettingDeviceByIdZeroThenGetThisSubDevice) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));
    EXPECT_EQ(2u, device->getNumSubDevices());
    auto subDevice = device->subdevices.at(0);

    EXPECT_EQ(subDevice, subDevice->getDeviceById(0));
    EXPECT_THROW(subDevice->getDeviceById(1), std::exception);
}

TEST(RootDevicesTest, givenRootDeviceWithoutSubdevicesWhenCreateEnginesThenDeviceCreatesCorrectNumberOfEngines) {
    auto hwInfo = *defaultHwInfo;
    auto &gpgpuEngines = HwHelper::get(hwInfo.platform.eRenderCoreFamily).getGpgpuEngineInstances(hwInfo);

    auto executionEnvironment = new MockExecutionEnvironment;
    MockDevice device(executionEnvironment, 0);
    EXPECT_EQ(0u, device.engines.size());
    device.createEngines();
    EXPECT_EQ(gpgpuEngines.size(), device.engines.size());
}

TEST(RootDevicesTest, givenRootDeviceWithSubdevicesWhenCreateEnginesThenDeviceCreatesSpecialEngine) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);

    auto executionEnvironment = new MockExecutionEnvironment;
    MockDevice device(executionEnvironment, 0);
    device.numSubDevices = 2;
    EXPECT_EQ(0u, device.engines.size());
    device.createEngines();
    EXPECT_EQ(1u, device.engines.size());
}

TEST(SubDevicesTest, givenRootDeviceWithSubDevicesWhenGettingGlobalMemorySizeThenSubDevicesReturnReducedAmountOfGlobalMemAllocSize) {
    const uint32_t numSubDevices = 2u;
    UltDeviceFactory deviceFactory{1, numSubDevices};

    auto rootDevice = deviceFactory.rootDevices[0];

    auto totalGlobalMemorySize = rootDevice->getGlobalMemorySize(static_cast<uint32_t>(rootDevice->getDeviceBitfield().to_ulong()));
    auto expectedGlobalMemorySize = totalGlobalMemorySize / numSubDevices;

    for (const auto &subDevice : deviceFactory.subDevices) {
        auto mockSubDevice = static_cast<MockSubDevice *>(subDevice);
        auto subDeviceBitfield = static_cast<uint32_t>(mockSubDevice->getDeviceBitfield().to_ulong());
        EXPECT_EQ(expectedGlobalMemorySize, mockSubDevice->getGlobalMemorySize(subDeviceBitfield));
    }
}

TEST(SubDevicesTest, whenInitializeRootCsrThenDirectSubmissionIsNotInitialized) {
    auto device = std::make_unique<MockDevice>();
    device->initializeRootCommandStreamReceiver();

    auto csr = device->getEngine(1u).commandStreamReceiver;
    EXPECT_FALSE(csr->isDirectSubmissionEnabled());
}

TEST(SubDevicesTest, givenCreateMultipleSubDevicesFlagSetWhenBindlessHeapHelperCreatedThenSubDeviceReturnRootDeviceMember) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    VariableBackup<bool> mockDeviceFlagBackup(&MockDevice::createSingleDevice, false);
    auto device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get()));

    device->getExecutionEnvironment()->rootDeviceEnvironments[device->getRootDeviceIndex()]->createBindlessHeapsHelper(device->getMemoryManager(), device->getNumAvailableDevices() > 1, device->getRootDeviceIndex());
    EXPECT_EQ(device->getBindlessHeapsHelper(), device->subdevices.at(0)->getBindlessHeapsHelper());
}
