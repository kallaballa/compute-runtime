/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/preamble.h"
#include "shared/source/helpers/string.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/utilities/tag_allocator.h"

#include "opencl/source/command_queue/gpgpu_walker.h"
#include "opencl/source/device_queue/device_queue_hw.h"
#include "opencl/source/helpers/hardware_commands_helper.h"

#include "pipe_control_args.h"

namespace NEO {
template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::allocateSlbBuffer() {
    auto slbSize = getMinimumSlbSize() + getWaCommandsSize();
    slbSize *= 128; //num of enqueues
    slbSize += sizeof(MI_BATCH_BUFFER_START);
    slbSize = alignUp(slbSize, MemoryConstants::pageSize);
    slbSize += DeviceQueueHw<GfxFamily>::getExecutionModelCleanupSectionSize();
    slbSize += (4 * MemoryConstants::pageSize); // +4 pages spec restriction
    slbSize = alignUp(slbSize, MemoryConstants::pageSize);

    slbBuffer = device->getMemoryManager()->allocateGraphicsMemoryWithProperties({device->getRootDeviceIndex(), slbSize, GraphicsAllocation::AllocationType::DEVICE_QUEUE_BUFFER, device->getDeviceBitfield()});
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::resetDeviceQueue() {
    auto &caps = device->getDeviceInfo();
    auto igilEventPool = reinterpret_cast<IGIL_EventPool *>(eventPoolBuffer->getUnderlyingBuffer());

    memset(eventPoolBuffer->getUnderlyingBuffer(), 0x0, eventPoolBuffer->getUnderlyingBufferSize());
    igilEventPool->m_TimestampResolution = static_cast<float>(device->getProfilingTimerResolution());
    igilEventPool->m_size = caps.maxOnDeviceEvents;

    auto igilCmdQueue = reinterpret_cast<IGIL_CommandQueue *>(queueBuffer->getUnderlyingBuffer());
    igilQueue = igilCmdQueue;

    igilCmdQueue->m_controls.m_StackSize =
        static_cast<uint32_t>((stackBuffer->getUnderlyingBufferSize() / sizeof(cl_uint)) - 1);
    igilCmdQueue->m_controls.m_StackTop =
        static_cast<uint32_t>((stackBuffer->getUnderlyingBufferSize() / sizeof(cl_uint)) - 1);
    igilCmdQueue->m_controls.m_PreviousHead = IGIL_DEVICE_QUEUE_HEAD_INIT;
    igilCmdQueue->m_controls.m_IDTAfterFirstPhase = 1;
    igilCmdQueue->m_controls.m_CurrentIDToffset = 1;
    igilCmdQueue->m_controls.m_PreviousStorageTop = static_cast<uint32_t>(queueStorageBuffer->getUnderlyingBufferSize());
    igilCmdQueue->m_controls.m_PreviousStackTop =
        static_cast<uint32_t>((stackBuffer->getUnderlyingBufferSize() / sizeof(cl_uint)) - 1);
    igilCmdQueue->m_controls.m_DebugNextBlockID = 0xFFFFFFFF;
    igilCmdQueue->m_controls.m_QstorageSize = static_cast<uint32_t>(queueStorageBuffer->getUnderlyingBufferSize());
    igilCmdQueue->m_controls.m_QstorageTop = static_cast<uint32_t>(queueStorageBuffer->getUnderlyingBufferSize());
    igilCmdQueue->m_controls.m_IsProfilingEnabled = static_cast<uint32_t>(isProfilingEnabled());
    igilCmdQueue->m_controls.m_IsSimulation = static_cast<uint32_t>(device->isSimulation());

    igilCmdQueue->m_controls.m_LastScheduleEventNumber = 0;
    igilCmdQueue->m_controls.m_PreviousNumberOfQueues = 0;
    igilCmdQueue->m_controls.m_EnqueueMarkerScheduled = 0;
    igilCmdQueue->m_controls.m_SecondLevelBatchOffset = 0;
    igilCmdQueue->m_controls.m_TotalNumberOfQueues = 0;
    igilCmdQueue->m_controls.m_EventTimestampAddress = 0;
    igilCmdQueue->m_controls.m_ErrorCode = 0;
    igilCmdQueue->m_controls.m_CurrentScheduleEventNumber = 0;
    igilCmdQueue->m_controls.m_DummyAtomicOperationPlaceholder = 0x00;
    igilCmdQueue->m_controls.m_DebugNextBlockGWS = 0;

    // set first stack element in surface at value "1", it protects Scheduler in corner case when StackTop is empty after Child execution
    auto stack = static_cast<uint32_t *>(stackBuffer->getUnderlyingBuffer());
    stack += ((stackBuffer->getUnderlyingBufferSize() / sizeof(cl_uint)) - 1);
    *stack = 1;

    igilCmdQueue->m_head = IGIL_DEVICE_QUEUE_HEAD_INIT;
    igilCmdQueue->m_size = static_cast<uint32_t>(queueBuffer->getUnderlyingBufferSize() - sizeof(IGIL_CommandQueue));
    igilCmdQueue->m_magic = IGIL_MAGIC_NUMBER;

    igilCmdQueue->m_controls.m_SchedulerEarlyReturn = DebugManager.flags.SchedulerSimulationReturnInstance.get();
    igilCmdQueue->m_controls.m_SchedulerEarlyReturnCounter = 0;

    buildSlbDummyCommands();

    igilCmdQueue->m_controls.m_SLBENDoffsetInBytes = -1;

    igilCmdQueue->m_controls.m_CriticalSection = ExecutionModelCriticalSection::Free;

    resetDSH();
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::initPipeControl(PIPE_CONTROL *pc) {
    *pc = GfxFamily::cmdInitPipeControl;
    pc->setStateCacheInvalidationEnable(0x1);
    pc->setDcFlushEnable(true);
    pc->setPipeControlFlushEnable(true);
    pc->setTextureCacheInvalidationEnable(true);
    pc->setCommandStreamerStallEnable(true);
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::addExecutionModelCleanUpSection(Kernel *parentKernel, TagNode<HwTimeStamps> *hwTimeStamp, uint64_t tagAddress, uint32_t taskCount) {
    // CleanUp Section
    auto offset = slbCS.getUsed();
    auto alignmentSize = alignUp(offset, MemoryConstants::pageSize) - offset;
    slbCS.getSpace(alignmentSize);
    offset = slbCS.getUsed();

    igilQueue->m_controls.m_CleanupSectionAddress = ptrOffset(slbBuffer->getGpuAddress(), slbCS.getUsed());
    GpgpuWalkerHelper<GfxFamily>::applyWADisableLSQCROPERFforOCL(&slbCS, *parentKernel, true);

    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;

    if (hwTimeStamp != nullptr) {
        uint64_t timeStampAddress = hwTimeStamp->getGpuAddress() + offsetof(HwTimeStamps, ContextCompleteTS);
        igilQueue->m_controls.m_EventTimestampAddress = timeStampAddress;

        addProfilingEndCmds(timeStampAddress);

        //enable preemption
        addLriCmd(false);
    }

    uint64_t criticalSectionAddress = (uint64_t)&igilQueue->m_controls.m_CriticalSection;
    PipeControlArgs args;
    MemorySynchronizationCommands<GfxFamily>::addPipeControlAndProgramPostSyncOperation(
        slbCS,
        PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA,
        criticalSectionAddress,
        ExecutionModelCriticalSection::Free,
        device->getHardwareInfo(),
        args);

    MemorySynchronizationCommands<GfxFamily>::addPipeControlAndProgramPostSyncOperation(
        slbCS,
        PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA,
        tagAddress,
        taskCount,
        device->getHardwareInfo(),
        args);

    addMediaStateClearCmds();

    auto pBBE = slbCS.getSpaceForCmd<MI_BATCH_BUFFER_END>();
    *pBBE = GfxFamily::cmdInitBatchBufferEnd;

    igilQueue->m_controls.m_CleanupSectionSize = (uint32_t)(slbCS.getUsed() - offset);
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::resetDSH() {
    if (heaps[IndirectHeap::DYNAMIC_STATE]) {
        heaps[IndirectHeap::DYNAMIC_STATE]->replaceBuffer(heaps[IndirectHeap::DYNAMIC_STATE]->getCpuBase(), heaps[IndirectHeap::DYNAMIC_STATE]->getMaxAvailableSpace());
        heaps[IndirectHeap::DYNAMIC_STATE]->getSpace(colorCalcStateSize);
    }
}

template <typename GfxFamily>
IndirectHeap *DeviceQueueHw<GfxFamily>::getIndirectHeap(IndirectHeap::Type type) {
    UNRECOVERABLE_IF(type != IndirectHeap::DYNAMIC_STATE);

    if (!heaps[type]) {
        heaps[type] = new IndirectHeap(dshBuffer);
        // get space for colorCalc and 2 ID tables at the beginning
        heaps[type]->getSpace(colorCalcStateSize);
    }
    return heaps[type];
}

template <typename GfxFamily>
size_t DeviceQueueHw<GfxFamily>::setSchedulerCrossThreadData(SchedulerKernel &scheduler) {
    using INTERFACE_DESCRIPTOR_DATA = typename GfxFamily::INTERFACE_DESCRIPTOR_DATA;
    size_t offset = dshBuffer->getUnderlyingBufferSize() - scheduler.getCurbeSize() - 4096; // Page size padding

    auto igilCmdQueue = reinterpret_cast<IGIL_CommandQueue *>(queueBuffer->getUnderlyingBuffer());
    igilCmdQueue->m_controls.m_SchedulerDSHOffset = (uint32_t)offset;
    igilCmdQueue->m_controls.m_SchedulerConstantBufferSize = (uint32_t)scheduler.getCurbeSize();

    return offset;
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::dispatchScheduler(LinearStream &commandStream, SchedulerKernel &scheduler, PreemptionMode preemptionMode, IndirectHeap *ssh, IndirectHeap *dsh, bool isCcsUsed) {
    GpgpuWalkerHelper<GfxFamily>::dispatchScheduler(commandStream,
                                                    *this,
                                                    preemptionMode,
                                                    scheduler,
                                                    ssh,
                                                    dsh,
                                                    isCcsUsed);
    return;
}

template <typename GfxFamily>
size_t DeviceQueueHw<GfxFamily>::getCSPrefetchSize() {
    return 512;
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::addLriCmd(bool setArbCheck) {
    // CTXT_PREMP_DBG offset
    constexpr uint32_t registerAddress = 0x2248u;
    uint32_t value = 0u;
    if (setArbCheck) {
        // set only bit 8 (Preempt On MI_ARB_CHK Only)
        value = 0x00000100;
    }

    LriHelper<GfxFamily>::program(&slbCS,
                                  registerAddress,
                                  value,
                                  false);
}

template <typename GfxFamily>
size_t DeviceQueueHw<GfxFamily>::getExecutionModelCleanupSectionSize() {
    size_t totalSize = 0;
    totalSize += sizeof(PIPE_CONTROL) +
                 2 * sizeof(MI_LOAD_REGISTER_REG) +
                 sizeof(MI_LOAD_REGISTER_IMM) +
                 sizeof(PIPE_CONTROL) +
                 sizeof(MI_MATH) +
                 NUM_ALU_INST_FOR_READ_MODIFY_WRITE * sizeof(MI_MATH_ALU_INST_INLINE);

    totalSize += getProfilingEndCmdsSize();
    totalSize += getMediaStateClearCmdsSize();

    totalSize += 4 * sizeof(PIPE_CONTROL);
    totalSize += sizeof(MI_BATCH_BUFFER_END);
    return totalSize;
}

template <typename GfxFamily>
size_t DeviceQueueHw<GfxFamily>::getProfilingEndCmdsSize() {
    size_t size = 0;
    size += sizeof(PIPE_CONTROL) + sizeof(MI_STORE_REGISTER_MEM);
    size += sizeof(MI_LOAD_REGISTER_IMM);
    return size;
}

template <typename GfxFamily>
void DeviceQueueHw<GfxFamily>::addDcFlushToPipeControlWa(PIPE_CONTROL *pc) {}

template <typename GfxFamily>
uint64_t DeviceQueueHw<GfxFamily>::getBlockKernelStartPointer(const Device &device, const KernelInfo *blockInfo, bool isCcsUsed) {
    auto blockAllocation = blockInfo->getGraphicsAllocation();
    DEBUG_BREAK_IF(!blockAllocation);

    auto blockKernelStartPointer = blockAllocation ? blockAllocation->getGpuAddressToPatch() : 0llu;

    auto &hardwareInfo = device.getHardwareInfo();
    auto &hwHelper = HwHelper::get(hardwareInfo.platform.eRenderCoreFamily);

    if (blockAllocation && isCcsUsed && hwHelper.isOffsetToSkipSetFFIDGPWARequired(hardwareInfo)) {
        blockKernelStartPointer += blockInfo->patchInfo.threadPayload->OffsetToSkipSetFFIDGP;
    }
    return blockKernelStartPointer;
}

} // namespace NEO
