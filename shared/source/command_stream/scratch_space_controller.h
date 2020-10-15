/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/indirect_heap/indirect_heap.h"

#include <cstddef>
#include <cstdint>

namespace NEO {
class Device;
class ExecutionEnvironment;
class GraphicsAllocation;
class InternalAllocationStorage;
class MemoryManager;
struct HardwareInfo;
class OsContext;

namespace ScratchSpaceConstants {
constexpr size_t scratchSpaceOffsetFor64Bit = 4096u;
}

class ScratchSpaceController {
  public:
    ScratchSpaceController(uint32_t rootDeviceIndex, ExecutionEnvironment &environment, InternalAllocationStorage &allocationStorage);
    virtual ~ScratchSpaceController();

    MOCKABLE_VIRTUAL GraphicsAllocation *getScratchSpaceAllocation() {
        return scratchAllocation;
    }
    GraphicsAllocation *getPrivateScratchSpaceAllocation() {
        return privateScratchAllocation;
    }
    virtual void setRequiredScratchSpace(void *sshBaseAddress,
                                         uint32_t scratchSlot,
                                         uint32_t requiredPerThreadScratchSize,
                                         uint32_t requiredPerThreadPrivateScratchSize,
                                         uint32_t currentTaskCount,
                                         OsContext &osContext,
                                         bool &stateBaseAddressDirty,
                                         bool &vfeStateDirty) = 0;
    virtual uint64_t calculateNewGSH() = 0;
    virtual uint64_t getScratchPatchAddress() = 0;

    virtual void reserveHeap(IndirectHeap::Type heapType, IndirectHeap *&indirectHeap) = 0;
    virtual void programHeaps(HeapContainer &heapContainer,
                              uint32_t scratchSlot,
                              uint32_t requiredPerThreadScratchSize,
                              uint32_t requiredPerThreadPrivateScratchSize,
                              uint32_t currentTaskCount,
                              OsContext &osContext,
                              bool &stateBaseAddressDirty,
                              bool &vfeStateDirty) = 0;

  protected:
    MemoryManager *getMemoryManager() const;

    const uint32_t rootDeviceIndex;
    ExecutionEnvironment &executionEnvironment;
    GraphicsAllocation *scratchAllocation = nullptr;
    GraphicsAllocation *privateScratchAllocation = nullptr;
    InternalAllocationStorage &csrAllocationStorage;
    size_t scratchSizeBytes = 0;
    size_t privateScratchSizeBytes = 0;
    bool force32BitAllocation = false;
    uint32_t computeUnitsUsedForScratch = 0;
};
} // namespace NEO
