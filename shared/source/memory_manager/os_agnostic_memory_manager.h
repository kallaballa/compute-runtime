/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/basic_math.h"
#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {
constexpr size_t bigAllocation = 1 * MB;
constexpr uintptr_t dummyAddress = 0xFFFFF000u;
class MemoryAllocation : public GraphicsAllocation {
  public:
    const unsigned long long id;
    size_t sizeToFree = 0;
    const bool uncacheable;

    void setSharedHandle(osHandle handle) { sharingInfo.sharedHandle = handle; }

    MemoryAllocation(uint32_t rootDeviceIndex, AllocationType allocationType, void *cpuPtrIn, uint64_t gpuAddress, uint64_t baseAddress, size_t sizeIn,
                     MemoryPool::Type pool, size_t maxOsContextCount)
        : MemoryAllocation(rootDeviceIndex, 1, allocationType, cpuPtrIn, gpuAddress, baseAddress, sizeIn, pool, maxOsContextCount) {}

    MemoryAllocation(uint32_t rootDeviceIndex, size_t numGmms, AllocationType allocationType, void *cpuPtrIn, uint64_t gpuAddress, uint64_t baseAddress, size_t sizeIn,
                     MemoryPool::Type pool, size_t maxOsContextCount)
        : GraphicsAllocation(rootDeviceIndex, numGmms, allocationType, cpuPtrIn, gpuAddress, baseAddress, sizeIn, pool, maxOsContextCount),
          id(0), uncacheable(false) {}

    MemoryAllocation(uint32_t rootDeviceIndex, AllocationType allocationType, void *cpuPtrIn, size_t sizeIn, osHandle sharedHandleIn, MemoryPool::Type pool, size_t maxOsContextCount)
        : MemoryAllocation(rootDeviceIndex, 1, allocationType, cpuPtrIn, sizeIn, sharedHandleIn, pool, maxOsContextCount) {}

    MemoryAllocation(uint32_t rootDeviceIndex, size_t numGmms, AllocationType allocationType, void *cpuPtrIn, size_t sizeIn, osHandle sharedHandleIn, MemoryPool::Type pool, size_t maxOsContextCount)
        : GraphicsAllocation(rootDeviceIndex, numGmms, allocationType, cpuPtrIn, sizeIn, sharedHandleIn, pool, maxOsContextCount),
          id(0), uncacheable(false) {}

    MemoryAllocation(uint32_t rootDeviceIndex, AllocationType allocationType, void *driverAllocatedCpuPointer, void *pMem, uint64_t gpuAddress, size_t memSize,
                     uint64_t count, MemoryPool::Type pool, bool uncacheable, bool flushL3Required, size_t maxOsContextCount)
        : MemoryAllocation(rootDeviceIndex, 1, allocationType, driverAllocatedCpuPointer, pMem, gpuAddress, memSize,
                           count, pool, uncacheable, flushL3Required, maxOsContextCount) {}

    MemoryAllocation(uint32_t rootDeviceIndex, size_t numGmms, AllocationType allocationType, void *driverAllocatedCpuPointer, void *pMem, uint64_t gpuAddress, size_t memSize,
                     uint64_t count, MemoryPool::Type pool, bool uncacheable, bool flushL3Required, size_t maxOsContextCount)
        : GraphicsAllocation(rootDeviceIndex, numGmms, allocationType, pMem, gpuAddress, 0u, memSize, pool, maxOsContextCount),
          id(count), uncacheable(uncacheable) {

        this->driverAllocatedCpuPointer = driverAllocatedCpuPointer;
        overrideMemoryPool(pool);
        allocationInfo.flags.flushL3Required = flushL3Required;
    }

    void overrideMemoryPool(MemoryPool::Type pool);
};

class OsAgnosticMemoryManager : public MemoryManager {
  public:
    using MemoryManager::allocateGraphicsMemory;
    using MemoryManager::heapAssigner;

    OsAgnosticMemoryManager(ExecutionEnvironment &executionEnvironment) : OsAgnosticMemoryManager(false, executionEnvironment) {}
    OsAgnosticMemoryManager(bool aubUsage, ExecutionEnvironment &executionEnvironment);
    void initialize(bool aubUsage);
    ~OsAgnosticMemoryManager() override;
    GraphicsAllocation *createGraphicsAllocationFromSharedHandle(osHandle handle, const AllocationProperties &properties, bool requireSpecificBitness) override;
    GraphicsAllocation *createGraphicsAllocationFromNTHandle(void *handle, uint32_t rootDeviceIndex) override { return nullptr; }

    void addAllocationToHostPtrManager(GraphicsAllocation *gfxAllocation) override;
    void removeAllocationFromHostPtrManager(GraphicsAllocation *gfxAllocation) override;
    void freeGraphicsMemoryImpl(GraphicsAllocation *gfxAllocation) override;

    AllocationStatus populateOsHandles(OsHandleStorage &handleStorage, uint32_t rootDeviceIndex) override;
    void cleanOsHandles(OsHandleStorage &handleStorage, uint32_t rootDeviceIndex) override;

    uint64_t getSystemSharedMemory(uint32_t rootDeviceIndex) override;
    uint64_t getLocalMemorySize(uint32_t rootDeviceIndex, uint32_t deviceBitfield) override;

    void turnOnFakingBigAllocations();

    void *reserveCpuAddressRange(size_t size, uint32_t rootDeviceIndex) override;
    void releaseReservedCpuAddressRange(void *reserved, size_t size, uint32_t rootDeviceIndex) override;

    AddressRange reserveGpuAddress(size_t size, uint32_t rootDeviceIndex) override;
    void freeGpuAddress(AddressRange addressRange, uint32_t rootDeviceIndex) override;

  protected:
    GraphicsAllocation *createGraphicsAllocation(OsHandleStorage &handleStorage, const AllocationData &allocationData) override;
    GraphicsAllocation *allocateGraphicsMemoryForNonSvmHostPtr(const AllocationData &allocationData) override;
    GraphicsAllocation *allocateGraphicsMemoryWithAlignment(const AllocationData &allocationData) override;
    GraphicsAllocation *allocateUSMHostGraphicsMemory(const AllocationData &allocationData) override;
    GraphicsAllocation *allocateGraphicsMemory64kb(const AllocationData &allocationData) override;
    GraphicsAllocation *allocateShareableMemory(const AllocationData &allocationData) override;
    GraphicsAllocation *allocateGraphicsMemoryForImageImpl(const AllocationData &allocationData, std::unique_ptr<Gmm> gmm) override;
    GraphicsAllocation *allocateGraphicsMemoryWithGpuVa(const AllocationData &allocationData) override;

    void *lockResourceImpl(GraphicsAllocation &graphicsAllocation) override { return graphicsAllocation.getUnderlyingBuffer(); }
    void unlockResourceImpl(GraphicsAllocation &graphicsAllocation) override {}
    GraphicsAllocation *allocate32BitGraphicsMemoryImpl(const AllocationData &allocationData, bool useLocalMemory) override;
    GraphicsAllocation *allocateGraphicsMemoryInDevicePool(const AllocationData &allocationData, AllocationStatus &status) override;
    MemoryAllocation *createMemoryAllocation(GraphicsAllocation::AllocationType allocationType, void *driverAllocatedCpuPointer, void *pMem, uint64_t gpuAddress, size_t memSize,
                                             uint64_t count, MemoryPool::Type pool, uint32_t rootDeviceIndex, bool uncacheable, bool flushL3Required, bool requireSpecificBitness);

  private:
    unsigned long long counter = 0;
    bool fakeBigAllocations = false;
};

} // namespace NEO
