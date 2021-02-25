/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/memory_manager/os_agnostic_memory_manager.h"
#include "shared/test/common/mocks/mock_device.h"
#include "shared/test/common/mocks/ult_device_factory.h"

#include "opencl/source/helpers/memory_properties_helpers.h"
#include "opencl/source/mem_obj/mem_obj_helper.h"
#include "opencl/test/unit_test/mocks/mock_execution_environment.h"
#include "opencl/test/unit_test/mocks/mock_memory_manager.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

TEST(MemoryManagerTest, givenSetUseSytemMemoryWhenGraphicsAllocationInDevicePoolIsAllocatedThenNullptrIsReturned) {
    MockExecutionEnvironment executionEnvironment(defaultHwInfo.get());
    MockMemoryManager memoryManager(false, false, executionEnvironment);
    MemoryManager::AllocationStatus status = MemoryManager::AllocationStatus::Success;
    AllocationData allocData;
    allocData.size = MemoryConstants::pageSize;
    allocData.flags.useSystemMemory = true;
    allocData.flags.allocateMemory = true;

    auto allocation = memoryManager.allocateGraphicsMemoryInDevicePool(allocData, status);
    EXPECT_EQ(nullptr, allocation);
    EXPECT_EQ(MemoryManager::AllocationStatus::RetryInNonDevicePool, status);
}

TEST(MemoryManagerTest, givenAllowed32BitAndFroce32BitWhenGraphicsAllocationInDevicePoolIsAllocatedThenNullptrIsReturned) {
    MockExecutionEnvironment executionEnvironment(defaultHwInfo.get());
    MockMemoryManager memoryManager(false, false, executionEnvironment);
    memoryManager.setForce32BitAllocations(true);

    MemoryManager::AllocationStatus status = MemoryManager::AllocationStatus::Success;
    AllocationData allocData;
    allocData.size = MemoryConstants::pageSize;
    allocData.flags.allow32Bit = true;
    allocData.flags.allocateMemory = true;

    auto allocation = memoryManager.allocateGraphicsMemoryInDevicePool(allocData, status);
    EXPECT_EQ(nullptr, allocation);
    EXPECT_EQ(MemoryManager::AllocationStatus::RetryInNonDevicePool, status);
}

TEST(AllocationFlagsTest, givenAllocateMemoryFlagWhenGetAllocationFlagsIsCalledThenAllocateFlagIsCorrectlySet) {
    HardwareInfo hwInfo(*defaultHwInfo);
    UltDeviceFactory deviceFactory{1, 0};
    auto pDevice = deviceFactory.rootDevices[0];
    MemoryProperties memoryProperties = MemoryPropertiesHelper::createMemoryProperties(0, 0, 0, pDevice);
    auto allocationProperties = MemoryPropertiesHelper::getAllocationProperties(0, memoryProperties, true, 0, GraphicsAllocation::AllocationType::BUFFER, false, hwInfo, {});
    EXPECT_TRUE(allocationProperties.flags.allocateMemory);

    auto allocationProperties2 = MemoryPropertiesHelper::getAllocationProperties(0, memoryProperties, false, 0, GraphicsAllocation::AllocationType::BUFFER, false, hwInfo, {});
    EXPECT_FALSE(allocationProperties2.flags.allocateMemory);
}

TEST(UncacheableFlagsTest, givenUncachedResourceFlagWhenGetAllocationFlagsIsCalledThenUncacheableFlagIsCorrectlySet) {
    cl_mem_flags_intel flagsIntel = CL_MEM_LOCALLY_UNCACHED_RESOURCE;
    UltDeviceFactory deviceFactory{1, 0};
    auto pDevice = deviceFactory.rootDevices[0];
    MemoryProperties memoryProperties = MemoryPropertiesHelper::createMemoryProperties(0, flagsIntel, 0, pDevice);
    auto allocationFlags = MemoryPropertiesHelper::getAllocationProperties(
        0, memoryProperties, false, 0, GraphicsAllocation::AllocationType::BUFFER, false, pDevice->getHardwareInfo(), {});
    EXPECT_TRUE(allocationFlags.flags.uncacheable);

    flagsIntel = 0;
    memoryProperties = MemoryPropertiesHelper::createMemoryProperties(0, flagsIntel, 0, pDevice);
    auto allocationFlags2 = MemoryPropertiesHelper::getAllocationProperties(
        0, memoryProperties, false, 0, GraphicsAllocation::AllocationType::BUFFER, false, pDevice->getHardwareInfo(), {});
    EXPECT_FALSE(allocationFlags2.flags.uncacheable);
}

TEST(AllocationFlagsTest, givenReadOnlyResourceFlagWhenGetAllocationFlagsIsCalledThenFlushL3FlagsAreCorrectlySet) {
    cl_mem_flags flags = CL_MEM_READ_ONLY;
    UltDeviceFactory deviceFactory{1, 0};
    auto pDevice = deviceFactory.rootDevices[0];
    MemoryProperties memoryProperties = MemoryPropertiesHelper::createMemoryProperties(flags, 0, 0, pDevice);

    auto allocationFlags =
        MemoryPropertiesHelper::getAllocationProperties(
            0, memoryProperties, true, 0, GraphicsAllocation::AllocationType::BUFFER, false, pDevice->getHardwareInfo(), {});
    EXPECT_FALSE(allocationFlags.flags.flushL3RequiredForRead);
    EXPECT_FALSE(allocationFlags.flags.flushL3RequiredForWrite);

    memoryProperties = MemoryPropertiesHelper::createMemoryProperties(0, 0, 0, pDevice);
    auto allocationFlags2 = MemoryPropertiesHelper::getAllocationProperties(
        0, memoryProperties, true, 0, GraphicsAllocation::AllocationType::BUFFER, false, pDevice->getHardwareInfo(), {});
    EXPECT_TRUE(allocationFlags2.flags.flushL3RequiredForRead);
    EXPECT_TRUE(allocationFlags2.flags.flushL3RequiredForWrite);
}

TEST(StorageInfoTest, whenStorageInfoIsCreatedWithDefaultConstructorThenReturnsOneHandle) {
    StorageInfo storageInfo;
    EXPECT_EQ(1u, storageInfo.getNumBanks());
}
