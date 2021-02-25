/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/preemption_mode.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/bit_helpers.h"
#include "shared/source/helpers/common_types.h"
#include "shared/source/helpers/engine_control.h"
#include "shared/source/helpers/heap_assigner.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/memory_manager/allocation_properties.h"
#include "shared/source/memory_manager/gfx_partition.h"
#include "shared/source/memory_manager/graphics_allocation.h"
#include "shared/source/memory_manager/host_ptr_defines.h"
#include "shared/source/memory_manager/local_memory_usage.h"
#include "shared/source/memory_manager/multi_graphics_allocation.h"
#include "shared/source/page_fault_manager/cpu_page_fault_manager.h"

#include "engine_node.h"

#include <bitset>
#include <cstdint>
#include <mutex>
#include <vector>

namespace NEO {
class DeferredDeleter;
class ExecutionEnvironment;
class Gmm;
class HostPtrManager;
class OsContext;

enum AllocationUsage {
    TEMPORARY_ALLOCATION,
    REUSABLE_ALLOCATION
};

struct AlignedMallocRestrictions {
    uintptr_t minAddress;
};

struct AddressRange {
    uint64_t address;
    size_t size;
};

constexpr size_t paddingBufferSize = 2 * MemoryConstants::megaByte;

namespace MemoryTransferHelper {
bool transferMemoryToAllocation(bool useBlitter, const Device &device, GraphicsAllocation *dstAllocation, size_t dstOffset, const void *srcMemory, size_t srcSize);
}

class MemoryManager {
  public:
    enum AllocationStatus {
        Success = 0,
        Error,
        InvalidHostPointer,
        RetryInNonDevicePool
    };

    MemoryManager(ExecutionEnvironment &executionEnvironment);
    bool isInitialized() const { return initialized; }

    virtual ~MemoryManager();
    MOCKABLE_VIRTUAL void *allocateSystemMemory(size_t size, size_t alignment);

    virtual void addAllocationToHostPtrManager(GraphicsAllocation *memory) = 0;
    virtual void removeAllocationFromHostPtrManager(GraphicsAllocation *memory) = 0;

    MOCKABLE_VIRTUAL GraphicsAllocation *allocateGraphicsMemoryWithProperties(const AllocationProperties &properties) {
        return allocateGraphicsMemoryInPreferredPool(properties, nullptr);
    }

    MOCKABLE_VIRTUAL GraphicsAllocation *allocateGraphicsMemoryWithProperties(const AllocationProperties &properties, const void *ptr) {
        return allocateGraphicsMemoryInPreferredPool(properties, ptr);
    }

    GraphicsAllocation *allocateInternalGraphicsMemoryWithHostCopy(uint32_t rootDeviceIndex, DeviceBitfield bitField, const void *ptr, size_t size);

    MOCKABLE_VIRTUAL GraphicsAllocation *allocateGraphicsMemoryInPreferredPool(const AllocationProperties &properties, const void *hostPtr);

    virtual bool verifyHandle(osHandle handle, uint32_t rootDeviceIndex, bool) { return true; }
    virtual GraphicsAllocation *createGraphicsAllocationFromSharedHandle(osHandle handle, const AllocationProperties &properties, bool requireSpecificBitness) = 0;
    virtual void closeSharedHandle(osHandle handle){};
    virtual GraphicsAllocation *createGraphicsAllocationFromNTHandle(void *handle, uint32_t rootDeviceIndex) = 0;

    virtual bool mapAuxGpuVA(GraphicsAllocation *graphicsAllocation);

    void *lockResource(GraphicsAllocation *graphicsAllocation);
    void unlockResource(GraphicsAllocation *graphicsAllocation);

    void cleanGraphicsMemoryCreatedFromHostPtr(GraphicsAllocation *);
    GraphicsAllocation *createGraphicsAllocationWithPadding(GraphicsAllocation *inputGraphicsAllocation, size_t sizeWithPadding);
    virtual GraphicsAllocation *createPaddedAllocation(GraphicsAllocation *inputGraphicsAllocation, size_t sizeWithPadding);

    MOCKABLE_VIRTUAL void *createMultiGraphicsAllocation(std::vector<uint32_t> &rootDeviceIndices, AllocationProperties &properties, MultiGraphicsAllocation &multiGraphicsAllocation);
    virtual GraphicsAllocation *createGraphicsAllocationFromExistingStorage(AllocationProperties &properties, void *ptr, MultiGraphicsAllocation &multiGraphicsAllocation);

    virtual AllocationStatus populateOsHandles(OsHandleStorage &handleStorage, uint32_t rootDeviceIndex) = 0;
    virtual void cleanOsHandles(OsHandleStorage &handleStorage, uint32_t rootDeviceIndex) = 0;

    void freeSystemMemory(void *ptr);

    virtual void freeGraphicsMemoryImpl(GraphicsAllocation *gfxAllocation) = 0;
    MOCKABLE_VIRTUAL void freeGraphicsMemory(GraphicsAllocation *gfxAllocation);
    virtual void handleFenceCompletion(GraphicsAllocation *allocation){};

    void checkGpuUsageAndDestroyGraphicsAllocations(GraphicsAllocation *gfxAllocation);

    virtual uint64_t getSystemSharedMemory(uint32_t rootDeviceIndex) = 0;
    virtual uint64_t getLocalMemorySize(uint32_t rootDeviceIndex, uint32_t deviceBitfield) = 0;

    uint64_t getMaxApplicationAddress() { return is64bit ? MemoryConstants::max64BitAppAddress : MemoryConstants::max32BitAppAddress; };
    MOCKABLE_VIRTUAL uint64_t getInternalHeapBaseAddress(uint32_t rootDeviceIndex, bool useLocalMemory) { return getGfxPartition(rootDeviceIndex)->getHeapBase(selectInternalHeap(useLocalMemory)); }
    uint64_t getExternalHeapBaseAddress(uint32_t rootDeviceIndex, bool useLocalMemory) { return getGfxPartition(rootDeviceIndex)->getHeapBase(selectExternalHeap(useLocalMemory)); }

    bool isLimitedRange(uint32_t rootDeviceIndex) { return getGfxPartition(rootDeviceIndex)->isLimitedRange(); }

    bool peek64kbPagesEnabled(uint32_t rootDeviceIndex) const;
    bool peekForce32BitAllocations() const { return force32bitAllocations; }
    void setForce32BitAllocations(bool newValue) { force32bitAllocations = newValue; }

    bool peekVirtualPaddingSupport() const { return virtualPaddingAvailable; }
    void setVirtualPaddingSupport(bool virtualPaddingSupport) { virtualPaddingAvailable = virtualPaddingSupport; }

    DeferredDeleter *getDeferredDeleter() const {
        return deferredDeleter.get();
    }

    PageFaultManager *getPageFaultManager() const {
        return pageFaultManager.get();
    }

    void waitForDeletions();
    MOCKABLE_VIRTUAL void waitForEnginesCompletion(GraphicsAllocation &graphicsAllocation);
    void cleanTemporaryAllocationListOnAllEngines(bool waitForCompletion);

    bool isAsyncDeleterEnabled() const;
    bool isLocalMemorySupported(uint32_t rootDeviceIndex) const;
    virtual bool isMemoryBudgetExhausted() const;

    virtual bool isKmdMigrationAvailable(uint32_t rootDeviceIndex) { return false; }

    virtual AlignedMallocRestrictions *getAlignedMallocRestrictions() {
        return nullptr;
    }

    MOCKABLE_VIRTUAL void *alignedMallocWrapper(size_t bytes, size_t alignment) {
        return ::alignedMalloc(bytes, alignment);
    }

    MOCKABLE_VIRTUAL void alignedFreeWrapper(void *ptr) {
        ::alignedFree(ptr);
    }

    MOCKABLE_VIRTUAL bool isHostPointerTrackingEnabled(uint32_t rootDeviceIndex);

    void setForceNonSvmForExternalHostPtr(bool mode) {
        forceNonSvmForExternalHostPtr = mode;
    }

    const ExecutionEnvironment &peekExecutionEnvironment() const { return executionEnvironment; }

    OsContext *createAndRegisterOsContext(CommandStreamReceiver *commandStreamReceiver, aub_stream::EngineType engineType,
                                          DeviceBitfield deviceBitfield, PreemptionMode preemptionMode,
                                          bool lowPriority, bool internalEngine, bool rootDevice);
    uint32_t getRegisteredEnginesCount() const { return static_cast<uint32_t>(registeredEngines.size()); }
    EngineControlContainer &getRegisteredEngines();
    EngineControl *getRegisteredEngineForCsr(CommandStreamReceiver *commandStreamReceiver);
    void unregisterEngineForCsr(CommandStreamReceiver *commandStreamReceiver);
    HostPtrManager *getHostPtrManager() const { return hostPtrManager.get(); }
    void setDefaultEngineIndex(uint32_t index) { defaultEngineIndex = index; }
    virtual bool copyMemoryToAllocation(GraphicsAllocation *graphicsAllocation, size_t destinationOffset, const void *memoryToCopy, size_t sizeToCopy);
    HeapIndex selectHeap(const GraphicsAllocation *allocation, bool hasPointer, bool isFullRangeSVM, bool useFrontWindow);
    static std::unique_ptr<MemoryManager> createMemoryManager(ExecutionEnvironment &executionEnvironment);
    virtual void *reserveCpuAddressRange(size_t size, uint32_t rootDeviceIndex) { return nullptr; };
    virtual void releaseReservedCpuAddressRange(void *reserved, size_t size, uint32_t rootDeviceIndex){};
    void *getReservedMemory(size_t size, size_t alignment);
    GfxPartition *getGfxPartition(uint32_t rootDeviceIndex) { return gfxPartitions.at(rootDeviceIndex).get(); }
    virtual AddressRange reserveGpuAddress(size_t size, uint32_t rootDeviceIndex) = 0;
    virtual void freeGpuAddress(AddressRange addressRange, uint32_t rootDeviceIndex) = 0;
    static HeapIndex selectInternalHeap(bool useLocalMemory) { return useLocalMemory ? HeapIndex::HEAP_INTERNAL_DEVICE_MEMORY : HeapIndex::HEAP_INTERNAL; }
    static HeapIndex selectExternalHeap(bool useLocalMemory) { return useLocalMemory ? HeapIndex::HEAP_EXTERNAL_DEVICE_MEMORY : HeapIndex::HEAP_EXTERNAL; }

    static uint32_t maxOsContextCount;
    virtual void commonCleanup(){};
    virtual bool isCpuCopyRequired(const void *ptr) { return false; }

    virtual void registerSysMemAlloc(GraphicsAllocation *allocation){};
    virtual void registerLocalMemAlloc(GraphicsAllocation *allocation, uint32_t rootDeviceIndex){};

  protected:
    bool getAllocationData(AllocationData &allocationData, const AllocationProperties &properties, const void *hostPtr, const StorageInfo &storageInfo);
    static void overrideAllocationData(AllocationData &allocationData, const AllocationProperties &properties);

    static bool isCopyRequired(ImageInfo &imgInfo, const void *hostPtr);

    bool useNonSvmHostPtrAlloc(GraphicsAllocation::AllocationType allocationType, uint32_t rootDeviceIndex);
    StorageInfo createStorageInfoFromProperties(const AllocationProperties &properties);

    virtual GraphicsAllocation *createGraphicsAllocation(OsHandleStorage &handleStorage, const AllocationData &allocationData) = 0;
    virtual GraphicsAllocation *allocateGraphicsMemoryForNonSvmHostPtr(const AllocationData &allocationData) = 0;
    GraphicsAllocation *allocateGraphicsMemory(const AllocationData &allocationData);
    virtual GraphicsAllocation *allocateGraphicsMemoryWithHostPtr(const AllocationData &allocationData);
    virtual GraphicsAllocation *allocateGraphicsMemoryWithAlignment(const AllocationData &allocationData) = 0;
    virtual GraphicsAllocation *allocateUSMHostGraphicsMemory(const AllocationData &allocationData) = 0;
    virtual GraphicsAllocation *allocateGraphicsMemory64kb(const AllocationData &allocationData) = 0;
    virtual GraphicsAllocation *allocate32BitGraphicsMemoryImpl(const AllocationData &allocationData, bool useLocalMemory) = 0;
    virtual GraphicsAllocation *allocateGraphicsMemoryInDevicePool(const AllocationData &allocationData, AllocationStatus &status) = 0;
    virtual GraphicsAllocation *allocateGraphicsMemoryWithGpuVa(const AllocationData &allocationData) = 0;

    GraphicsAllocation *allocateGraphicsMemoryForImageFromHostPtr(const AllocationData &allocationData);
    MOCKABLE_VIRTUAL GraphicsAllocation *allocateGraphicsMemoryForImage(const AllocationData &allocationData);
    virtual GraphicsAllocation *allocateGraphicsMemoryForImageImpl(const AllocationData &allocationData, std::unique_ptr<Gmm> gmm) = 0;
    virtual GraphicsAllocation *allocateShareableMemory(const AllocationData &allocationData) = 0;
    virtual void *lockResourceImpl(GraphicsAllocation &graphicsAllocation) = 0;
    virtual void unlockResourceImpl(GraphicsAllocation &graphicsAllocation) = 0;
    virtual void freeAssociatedResourceImpl(GraphicsAllocation &graphicsAllocation) { return unlockResourceImpl(graphicsAllocation); };
    virtual void registerAllocationInOs(GraphicsAllocation *allocation) {}

    bool initialized = false;
    bool forceNonSvmForExternalHostPtr = false;
    bool force32bitAllocations = false;
    bool virtualPaddingAvailable = false;
    std::unique_ptr<DeferredDeleter> deferredDeleter;
    bool asyncDeleterEnabled = false;
    std::vector<bool> enable64kbpages;
    std::vector<bool> localMemorySupported;
    bool supportsMultiStorageResources = true;
    ExecutionEnvironment &executionEnvironment;
    EngineControlContainer registeredEngines;
    std::unique_ptr<HostPtrManager> hostPtrManager;
    uint32_t latestContextId = std::numeric_limits<uint32_t>::max();
    uint32_t defaultEngineIndex = 0;
    std::unique_ptr<DeferredDeleter> multiContextResourceDestructor;
    std::vector<std::unique_ptr<GfxPartition>> gfxPartitions;
    std::vector<std::unique_ptr<LocalMemoryUsageBankSelector>> localMemoryUsageBankSelector;
    void *reservedMemory = nullptr;
    std::unique_ptr<PageFaultManager> pageFaultManager;
    OSMemory::ReservedCpuAddressRange reservedCpuAddressRange;
    HeapAssigner heapAssigner;
};

std::unique_ptr<DeferredDeleter> createDeferredDeleter();
} // namespace NEO
