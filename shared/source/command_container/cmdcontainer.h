/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/csr_definitions.h"
#include "shared/source/helpers/heap_helper.h"
#include "shared/source/helpers/non_copyable_or_moveable.h"
#include "shared/source/indirect_heap/indirect_heap.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace NEO {
class Device;
class GraphicsAllocation;
class LinearStream;

using ResidencyContainer = std::vector<GraphicsAllocation *>;
using CmdBufferContainer = std::vector<GraphicsAllocation *>;
using HeapType = IndirectHeap::Type;

enum class ErrorCode {
    SUCCESS = 0,
    OUT_OF_DEVICE_MEMORY = 1
};

class CommandContainer : public NonCopyableOrMovableClass {
  public:
    static constexpr size_t defaultListCmdBufferSize = MemoryConstants::kiloByte * 256;
    static constexpr size_t totalCmdBufferSize =
        defaultListCmdBufferSize +
        MemoryConstants::cacheLineSize +
        CSRequirements::csOverfetchSize;

    CommandContainer() {
        for (auto &indirectHeap : indirectHeaps) {
            indirectHeap = nullptr;
        }

        for (auto &allocationIndirectHeap : allocationIndirectHeaps) {
            allocationIndirectHeap = nullptr;
        }
    }

    CommandContainer(uint32_t maxNumAggregatedIdds) : CommandContainer() {
        numIddsPerBlock = maxNumAggregatedIdds;
    }

    CmdBufferContainer &getCmdBufferAllocations() { return cmdBufferAllocations; }

    ResidencyContainer &getResidencyContainer() { return residencyContainer; }

    std::vector<GraphicsAllocation *> &getDeallocationContainer() { return deallocationContainer; }

    void addToResidencyContainer(GraphicsAllocation *alloc);
    void removeDuplicatesFromResidencyContainer();

    LinearStream *getCommandStream() { return commandStream.get(); }

    IndirectHeap *getIndirectHeap(HeapType heapType) { return indirectHeaps[heapType].get(); }

    HeapHelper *getHeapHelper() { return heapHelper.get(); }

    GraphicsAllocation *getIndirectHeapAllocation(HeapType heapType) { return allocationIndirectHeaps[heapType]; }

    void setIndirectHeapAllocation(HeapType heapType, GraphicsAllocation *allocation) { allocationIndirectHeaps[heapType] = allocation; }

    uint64_t getInstructionHeapBaseAddress() const { return instructionHeapBaseAddress; }

    uint64_t getIndirectObjectHeapBaseAddress() const { return indirectObjectHeapBaseAddress; }

    void *getHeapSpaceAllowGrow(HeapType heapType, size_t size);

    ErrorCode initialize(Device *device);

    void prepareBindfulSsh();

    virtual ~CommandContainer();

    uint32_t slmSize = std::numeric_limits<uint32_t>::max();
    uint32_t nextIddInBlock = 0;
    uint32_t lastSentNumGrfRequired = 0;
    bool lastPipelineSelectModeRequired = false;

    Device *getDevice() const { return device; }

    IndirectHeap *getHeapWithRequiredSizeAndAlignment(HeapType heapType, size_t sizeRequired, size_t alignment);
    void allocateNextCommandBuffer();

    void reset();

    bool isHeapDirty(HeapType heapType) const { return (dirtyHeaps & (1u << heapType)); }
    bool isAnyHeapDirty() const { return dirtyHeaps != 0; }
    void setHeapDirty(HeapType heapType) { dirtyHeaps |= (1u << heapType); }
    void setDirtyStateForAllHeaps(bool dirty) { dirtyHeaps = dirty ? std::numeric_limits<uint32_t>::max() : 0; }
    void setIddBlock(void *iddBlock) { this->iddBlock = iddBlock; }
    void *getIddBlock() { return iddBlock; }
    uint32_t getNumIddPerBlock() const { return numIddsPerBlock; }
    void setReservedSshSize(size_t reserveSize) {
        reservedSshSize = reserveSize;
    }
    HeapContainer sshAllocations;

  protected:
    void *iddBlock = nullptr;
    Device *device = nullptr;
    std::unique_ptr<HeapHelper> heapHelper;

    CmdBufferContainer cmdBufferAllocations;
    GraphicsAllocation *allocationIndirectHeaps[HeapType::NUM_TYPES] = {};
    uint64_t instructionHeapBaseAddress = 0u;
    uint64_t indirectObjectHeapBaseAddress = 0u;
    uint32_t dirtyHeaps = std::numeric_limits<uint32_t>::max();
    uint32_t numIddsPerBlock = 64;
    size_t reservedSshSize = 0;

    std::unique_ptr<LinearStream> commandStream;
    std::unique_ptr<IndirectHeap> indirectHeaps[HeapType::NUM_TYPES];
    ResidencyContainer residencyContainer;
    std::vector<GraphicsAllocation *> deallocationContainer;
};

} // namespace NEO
