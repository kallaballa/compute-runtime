/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/cmdcontainer.h"

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/device/device.h"
#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/helpers/debug_helpers.h"
#include "shared/source/helpers/heap_helper.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/indirect_heap/indirect_heap.h"
#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {

CommandContainer::~CommandContainer() {
    if (!device) {
        DEBUG_BREAK_IF(device);
        return;
    }

    auto memoryManager = device->getMemoryManager();

    for (auto *alloc : cmdBufferAllocations) {
        memoryManager->freeGraphicsMemory(alloc);
    }

    for (auto allocationIndirectHeap : allocationIndirectHeaps) {
        if (heapHelper) {
            heapHelper->storeHeapAllocation(allocationIndirectHeap);
        }
    }
    for (auto deallocation : deallocationContainer) {
        if (((deallocation->getAllocationType() == GraphicsAllocation::AllocationType::INTERNAL_HEAP) || (deallocation->getAllocationType() == GraphicsAllocation::AllocationType::LINEAR_STREAM))) {
            getHeapHelper()->storeHeapAllocation(deallocation);
        }
    }
}

ErrorCode CommandContainer::initialize(Device *device) {
    this->device = device;

    size_t alignedSize = alignUp<size_t>(totalCmdBufferSize, MemoryConstants::pageSize64k);
    AllocationProperties properties{device->getRootDeviceIndex(),
                                    true /* allocateMemory*/,
                                    alignedSize,
                                    GraphicsAllocation::AllocationType::COMMAND_BUFFER,
                                    (device->getNumAvailableDevices() > 1u) /* multiOsContextCapable */,
                                    false,
                                    device->getDeviceBitfield()};

    auto cmdBufferAllocation = device->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);
    if (!cmdBufferAllocation) {
        return ErrorCode::OUT_OF_DEVICE_MEMORY;
    }

    cmdBufferAllocations.push_back(cmdBufferAllocation);

    commandStream = std::unique_ptr<LinearStream>(new LinearStream(cmdBufferAllocation->getUnderlyingBuffer(),
                                                                   defaultListCmdBufferSize));
    commandStream->replaceGraphicsAllocation(cmdBufferAllocation);

    addToResidencyContainer(cmdBufferAllocation);

    constexpr size_t heapSize = 65536u;
    heapHelper = std::unique_ptr<HeapHelper>(new HeapHelper(device->getMemoryManager(), device->getDefaultEngine().commandStreamReceiver->getInternalAllocationStorage(), device->getNumAvailableDevices() > 1u));

    for (uint32_t i = 0; i < IndirectHeap::Type::NUM_TYPES; i++) {
        if (NEO::ApiSpecificConfig::getBindlessConfiguration() && i != IndirectHeap::INDIRECT_OBJECT) {
            continue;
        }
        allocationIndirectHeaps[i] = heapHelper->getHeapAllocation(i,
                                                                   heapSize,
                                                                   alignedSize,
                                                                   device->getRootDeviceIndex());
        if (!allocationIndirectHeaps[i]) {
            return ErrorCode::OUT_OF_DEVICE_MEMORY;
        }
        residencyContainer.push_back(allocationIndirectHeaps[i]);

        bool requireInternalHeap = (IndirectHeap::INDIRECT_OBJECT == i);
        indirectHeaps[i] = std::make_unique<IndirectHeap>(allocationIndirectHeaps[i], requireInternalHeap);
        if (i == IndirectHeap::Type::SURFACE_STATE) {
            indirectHeaps[i]->getSpace(reservedSshSize);
        }
    }

    auto &hwHelper = HwHelper::get(getDevice()->getHardwareInfo().platform.eRenderCoreFamily);

    indirectObjectHeapBaseAddress = device->getMemoryManager()->getInternalHeapBaseAddress(device->getRootDeviceIndex(), allocationIndirectHeaps[IndirectHeap::Type::INDIRECT_OBJECT]->isAllocatedInLocalMemoryPool());

    instructionHeapBaseAddress = device->getMemoryManager()->getInternalHeapBaseAddress(device->getRootDeviceIndex(), !hwHelper.useSystemMemoryPlacementForISA(getDevice()->getHardwareInfo()));

    iddBlock = nullptr;
    nextIddInBlock = this->getNumIddPerBlock();

    return ErrorCode::SUCCESS;
}

void CommandContainer::addToResidencyContainer(GraphicsAllocation *alloc) {
    if (alloc == nullptr) {
        return;
    }

    this->residencyContainer.push_back(alloc);
}

void CommandContainer::removeDuplicatesFromResidencyContainer() {
    std::sort(this->residencyContainer.begin(), this->residencyContainer.end());
    this->residencyContainer.erase(std::unique(this->residencyContainer.begin(), this->residencyContainer.end()), this->residencyContainer.end());
}

void CommandContainer::reset() {
    setDirtyStateForAllHeaps(true);
    slmSize = std::numeric_limits<uint32_t>::max();
    getResidencyContainer().clear();
    getDeallocationContainer().clear();
    sshAllocations.clear();

    for (size_t i = 1; i < cmdBufferAllocations.size(); i++) {
        device->getMemoryManager()->freeGraphicsMemory(cmdBufferAllocations[i]);
    }
    cmdBufferAllocations.erase(cmdBufferAllocations.begin() + 1, cmdBufferAllocations.end());

    commandStream->replaceBuffer(cmdBufferAllocations[0]->getUnderlyingBuffer(),
                                 defaultListCmdBufferSize);
    addToResidencyContainer(commandStream->getGraphicsAllocation());

    for (auto &indirectHeap : indirectHeaps) {
        if (indirectHeap != nullptr) {
            indirectHeap->replaceBuffer(indirectHeap->getCpuBase(),
                                        indirectHeap->getMaxAvailableSpace());
            addToResidencyContainer(indirectHeap->getGraphicsAllocation());
        }
    }
    if (indirectHeaps[IndirectHeap::Type::SURFACE_STATE] != nullptr) {
        indirectHeaps[IndirectHeap::Type::SURFACE_STATE]->getSpace(reservedSshSize);
    }

    iddBlock = nullptr;
    nextIddInBlock = this->getNumIddPerBlock();
    lastSentNumGrfRequired = 0;
    lastPipelineSelectModeRequired = false;
}

void *CommandContainer::getHeapSpaceAllowGrow(HeapType heapType,
                                              size_t size) {
    auto indirectHeap = getIndirectHeap(heapType);

    if (indirectHeap->getAvailableSpace() < size) {
        size_t newSize = indirectHeap->getUsed() + indirectHeap->getAvailableSpace();
        newSize *= 2;
        newSize = std::max(newSize, indirectHeap->getAvailableSpace() + size);
        newSize = alignUp(newSize, MemoryConstants::pageSize);
        auto oldAlloc = getIndirectHeapAllocation(heapType);
        auto newAlloc = getHeapHelper()->getHeapAllocation(heapType, newSize, MemoryConstants::pageSize, device->getRootDeviceIndex());
        UNRECOVERABLE_IF(!oldAlloc);
        UNRECOVERABLE_IF(!newAlloc);
        auto oldBase = indirectHeap->getHeapGpuBase();
        indirectHeap->replaceGraphicsAllocation(newAlloc);
        indirectHeap->replaceBuffer(newAlloc->getUnderlyingBuffer(),
                                    newAlloc->getUnderlyingBufferSize());
        auto newBase = indirectHeap->getHeapGpuBase();
        getResidencyContainer().push_back(newAlloc);
        getDeallocationContainer().push_back(oldAlloc);
        setIndirectHeapAllocation(heapType, newAlloc);
        if (oldBase != newBase) {
            setHeapDirty(heapType);
        }
    }
    return indirectHeap->getSpace(size);
}

IndirectHeap *CommandContainer::getHeapWithRequiredSizeAndAlignment(HeapType heapType, size_t sizeRequired, size_t alignment) {
    auto indirectHeap = getIndirectHeap(heapType);
    auto sizeRequested = sizeRequired;

    auto heapBuffer = indirectHeap->getSpace(0);
    if (alignment && (heapBuffer != alignUp(heapBuffer, alignment))) {
        sizeRequested += alignment;
    }

    if (indirectHeap->getAvailableSpace() < sizeRequested) {
        size_t newSize = indirectHeap->getUsed() + indirectHeap->getAvailableSpace();
        newSize = alignUp(newSize, MemoryConstants::pageSize);
        auto oldAlloc = getIndirectHeapAllocation(heapType);
        auto newAlloc = getHeapHelper()->getHeapAllocation(heapType, newSize, MemoryConstants::pageSize, device->getRootDeviceIndex());
        UNRECOVERABLE_IF(!oldAlloc);
        UNRECOVERABLE_IF(!newAlloc);
        auto oldBase = indirectHeap->getHeapGpuBase();
        indirectHeap->replaceGraphicsAllocation(newAlloc);
        indirectHeap->replaceBuffer(newAlloc->getUnderlyingBuffer(),
                                    newAlloc->getUnderlyingBufferSize());
        auto newBase = indirectHeap->getHeapGpuBase();
        getResidencyContainer().push_back(newAlloc);
        getDeallocationContainer().push_back(oldAlloc);
        setIndirectHeapAllocation(heapType, newAlloc);
        if (oldBase != newBase) {
            setHeapDirty(heapType);
        }
        if (heapType == HeapType::SURFACE_STATE) {
            indirectHeap->getSpace(reservedSshSize);
            sshAllocations.push_back(oldAlloc);
        }
    }

    if (alignment) {
        indirectHeap->align(alignment);
    }

    return indirectHeap;
}

void CommandContainer::allocateNextCommandBuffer() {
    size_t alignedSize = alignUp<size_t>(totalCmdBufferSize, MemoryConstants::pageSize64k);
    AllocationProperties properties{0u,
                                    true /* allocateMemory*/,
                                    alignedSize,
                                    GraphicsAllocation::AllocationType::COMMAND_BUFFER,
                                    (device->getNumAvailableDevices() > 1u) /* multiOsContextCapable */,
                                    false,
                                    device->getDeviceBitfield()};

    auto cmdBufferAllocation = device->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);
    UNRECOVERABLE_IF(!cmdBufferAllocation);

    cmdBufferAllocations.push_back(cmdBufferAllocation);

    commandStream->replaceBuffer(cmdBufferAllocation->getUnderlyingBuffer(), defaultListCmdBufferSize);
    commandStream->replaceGraphicsAllocation(cmdBufferAllocation);

    addToResidencyContainer(cmdBufferAllocation);
}
void CommandContainer::prepareBindfulSsh() {
    if (ApiSpecificConfig::getBindlessConfiguration()) {
        if (allocationIndirectHeaps[IndirectHeap::SURFACE_STATE] == nullptr) {
            size_t alignedSize = alignUp<size_t>(totalCmdBufferSize, MemoryConstants::pageSize64k);
            constexpr size_t heapSize = 65536u;
            allocationIndirectHeaps[IndirectHeap::SURFACE_STATE] = heapHelper->getHeapAllocation(IndirectHeap::SURFACE_STATE,
                                                                                                 heapSize,
                                                                                                 alignedSize,
                                                                                                 device->getRootDeviceIndex());
            UNRECOVERABLE_IF(!allocationIndirectHeaps[IndirectHeap::SURFACE_STATE]);
            residencyContainer.push_back(allocationIndirectHeaps[IndirectHeap::SURFACE_STATE]);

            indirectHeaps[IndirectHeap::SURFACE_STATE] = std::make_unique<IndirectHeap>(allocationIndirectHeaps[IndirectHeap::SURFACE_STATE], false);
            indirectHeaps[IndirectHeap::SURFACE_STATE]->getSpace(reservedSshSize);
        }
        setHeapDirty(IndirectHeap::SURFACE_STATE);
    }
}
} // namespace NEO
