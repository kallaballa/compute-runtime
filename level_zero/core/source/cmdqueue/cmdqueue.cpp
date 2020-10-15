/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/command_stream/csr_definitions.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/memory_manager/memory_manager.h"

#include "level_zero/core/source/cmdlist/cmdlist_hw.h"
#include "level_zero/core/source/cmdqueue/cmdqueue_imp.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/device/device_imp.h"

#include "hw_helpers.h"
#include "igfxfmid.h"

namespace L0 {

CommandQueueAllocatorFn commandQueueFactory[IGFX_MAX_PRODUCT] = {};

ze_result_t CommandQueueImp::destroy() {
    delete this;
    return ZE_RESULT_SUCCESS;
}

void CommandQueueImp::initialize(bool copyOnly) {
    buffers.initialize(device, totalCmdBufferSize);
    NEO::GraphicsAllocation *bufferAllocation = buffers.getCurrentBufferAllocation();
    commandStream = new NEO::LinearStream(bufferAllocation->getUnderlyingBuffer(),
                                          defaultQueueCmdBufferSize);
    UNRECOVERABLE_IF(commandStream == nullptr);
    commandStream->replaceGraphicsAllocation(bufferAllocation);
    isCopyOnlyCommandQueue = copyOnly;
}

void CommandQueueImp::reserveLinearStreamSize(size_t size) {
    UNRECOVERABLE_IF(commandStream == nullptr);
    if (commandStream->getAvailableSpace() < size) {
        buffers.switchBuffers(csr);
        NEO::GraphicsAllocation *nextBufferAllocation = buffers.getCurrentBufferAllocation();
        commandStream->replaceBuffer(nextBufferAllocation->getUnderlyingBuffer(),
                                     defaultQueueCmdBufferSize);
        commandStream->replaceGraphicsAllocation(nextBufferAllocation);
    }
}

void CommandQueueImp::submitBatchBuffer(size_t offset, NEO::ResidencyContainer &residencyContainer, void *endingCmdPtr) {
    UNRECOVERABLE_IF(csr == nullptr);

    NEO::BatchBuffer batchBuffer(commandStream->getGraphicsAllocation(), offset, 0u, nullptr, false, false,
                                 NEO::QueueThrottle::HIGH, NEO::QueueSliceCount::defaultSliceCount,
                                 commandStream->getUsed(), commandStream, endingCmdPtr);

    csr->submitBatchBuffer(batchBuffer, residencyContainer);
    buffers.setCurrentFlushStamp(csr->obtainCurrentFlushStamp());
}

ze_result_t CommandQueueImp::synchronize(uint64_t timeout) {
    return synchronizeByPollingForTaskCount(timeout);
}

ze_result_t CommandQueueImp::synchronizeByPollingForTaskCount(uint64_t timeout) {
    UNRECOVERABLE_IF(csr == nullptr);

    auto taskCountToWait = getTaskCount();
    bool enableTimeout = true;
    int64_t timeoutMicroseconds = static_cast<int64_t>(timeout);
    if (timeout == std::numeric_limits<uint64_t>::max()) {
        enableTimeout = false;
        timeoutMicroseconds = NEO::TimeoutControls::maxTimeout;
    }

    csr->waitForCompletionWithTimeout(enableTimeout, timeoutMicroseconds, this->taskCount);

    if (*csr->getTagAddress() < taskCountToWait) {
        return ZE_RESULT_NOT_READY;
    }

    printFunctionsPrintfOutput();

    if (device->getL0Debugger() && NEO::DebugManager.flags.PrintDebugMessages.get()) {
        device->getL0Debugger()->printTrackedAddresses(csr->getOsContext().getContextId());
    }

    return ZE_RESULT_SUCCESS;
}

void CommandQueueImp::printFunctionsPrintfOutput() {
    size_t size = this->printfFunctionContainer.size();
    for (size_t i = 0; i < size; i++) {
        this->printfFunctionContainer[i]->printPrintfOutput();
    }
    this->printfFunctionContainer.clear();
}

CommandQueue *CommandQueue::create(uint32_t productFamily, Device *device, NEO::CommandStreamReceiver *csr,
                                   const ze_command_queue_desc_t *desc, bool isCopyOnly) {
    CommandQueueAllocatorFn allocator = nullptr;
    if (productFamily < IGFX_MAX_PRODUCT) {
        allocator = commandQueueFactory[productFamily];
    }

    CommandQueueImp *commandQueue = nullptr;
    if (allocator) {
        commandQueue = static_cast<CommandQueueImp *>((*allocator)(device, csr, desc));

        commandQueue->initialize(isCopyOnly);
    }
    return commandQueue;
}

ze_command_queue_mode_t CommandQueueImp::getSynchronousMode() {
    return desc.mode;
}

void CommandQueueImp::CommandBufferManager::initialize(Device *device, size_t sizeRequested) {
    size_t alignedSize = alignUp<size_t>(sizeRequested, MemoryConstants::pageSize64k);
    NEO::AllocationProperties properties{device->getRootDeviceIndex(), true, alignedSize,
                                         NEO::GraphicsAllocation::AllocationType::COMMAND_BUFFER,
                                         device->isMultiDeviceCapable(),
                                         false,
                                         CommonConstants::allDevicesBitfield};

    buffers[BUFFER_ALLOCATION::FIRST] = device->getNEODevice()->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);

    UNRECOVERABLE_IF(nullptr == buffers[BUFFER_ALLOCATION::FIRST]);
    memset(buffers[BUFFER_ALLOCATION::FIRST]->getUnderlyingBuffer(), 0, buffers[BUFFER_ALLOCATION::FIRST]->getUnderlyingBufferSize());

    buffers[BUFFER_ALLOCATION::SECOND] = device->getNEODevice()->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);

    UNRECOVERABLE_IF(nullptr == buffers[BUFFER_ALLOCATION::SECOND]);
    memset(buffers[BUFFER_ALLOCATION::SECOND]->getUnderlyingBuffer(), 0, buffers[BUFFER_ALLOCATION::SECOND]->getUnderlyingBufferSize());
    flushId[BUFFER_ALLOCATION::FIRST] = 0u;
    flushId[BUFFER_ALLOCATION::SECOND] = 0u;
}

void CommandQueueImp::CommandBufferManager::destroy(NEO::MemoryManager *memoryManager) {
    memoryManager->freeGraphicsMemory(buffers[BUFFER_ALLOCATION::FIRST]);
    memoryManager->freeGraphicsMemory(buffers[BUFFER_ALLOCATION::SECOND]);
}

void CommandQueueImp::CommandBufferManager::switchBuffers(NEO::CommandStreamReceiver *csr) {
    if (bufferUse == BUFFER_ALLOCATION::FIRST) {
        bufferUse = BUFFER_ALLOCATION::SECOND;
    } else {
        bufferUse = BUFFER_ALLOCATION::FIRST;
    }

    NEO::FlushStamp completionId = flushId[bufferUse];
    if (completionId != 0u) {
        UNRECOVERABLE_IF(csr == nullptr);
        csr->waitForFlushStamp(completionId);
    }
}

} // namespace L0
