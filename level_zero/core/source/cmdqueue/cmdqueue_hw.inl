/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/built_ins/sip.h"
#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/command_stream_receiver_hw.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/command_stream/preemption.h"
#include "shared/source/command_stream/thread_arbitration_policy.h"
#include "shared/source/device/device.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/helpers/preamble.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/memory_manager/residency_container.h"
#include "shared/source/os_interface/os_context.h"
#include "shared/source/page_fault_manager/cpu_page_fault_manager.h"
#include "shared/source/unified_memory/unified_memory.h"

#include "level_zero/core/source/cmdlist/cmdlist.h"
#include "level_zero/core/source/cmdlist/cmdlist_hw.h"
#include "level_zero/core/source/cmdqueue/cmdqueue_hw.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/driver/driver_handle_imp.h"
#include "level_zero/core/source/fence/fence.h"
#include "level_zero/tools/source/metrics/metric.h"

#include "pipe_control_args.h"

#include <limits>
#include <thread>

namespace L0 {

template <GFXCORE_FAMILY gfxCoreFamily>
ze_result_t CommandQueueHw<gfxCoreFamily>::createFence(const ze_fence_desc_t *desc,
                                                       ze_fence_handle_t *phFence) {
    *phFence = Fence::create(this, desc);
    return ZE_RESULT_SUCCESS;
}

template <GFXCORE_FAMILY gfxCoreFamily>
ze_result_t CommandQueueHw<gfxCoreFamily>::destroy() {
    if (commandStream) {
        delete commandStream;
        commandStream = nullptr;
    }
    buffers.destroy(this->getDevice()->getNEODevice()->getMemoryManager());
    delete this;
    return ZE_RESULT_SUCCESS;
}

template <GFXCORE_FAMILY gfxCoreFamily>
ze_result_t CommandQueueHw<gfxCoreFamily>::executeCommandLists(
    uint32_t numCommandLists,
    ze_command_list_handle_t *phCommandLists,
    ze_fence_handle_t hFence,
    bool performMigration) {

    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    using MI_BATCH_BUFFER_START = typename GfxFamily::MI_BATCH_BUFFER_START;
    using MI_BATCH_BUFFER_END = typename GfxFamily::MI_BATCH_BUFFER_END;

    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;
    using POST_SYNC_OPERATION = typename PIPE_CONTROL::POST_SYNC_OPERATION;

    auto lockCSR = csr->obtainUniqueOwnership();

    for (auto i = 0u; i < numCommandLists; i++) {
        auto commandList = CommandList::fromHandle(phCommandLists[i]);
        if (isCopyOnlyCommandQueue != commandList->isCopyOnly()) {
            return ZE_RESULT_ERROR_INVALID_COMMAND_LIST_TYPE;
        }
    }

    size_t spaceForResidency = 0;
    size_t preemptionSize = 0u;
    size_t debuggerCmdsSize = 0;
    constexpr size_t residencyContainerSpaceForPreemption = 2;
    constexpr size_t residencyContainerSpaceForFence = 1;
    constexpr size_t residencyContainerSpaceForTagWrite = 1;

    NEO::Device *neoDevice = device->getNEODevice();
    NEO::PreemptionMode statePreemption = commandQueuePreemptionMode;
    auto devicePreemption = device->getDevicePreemptionMode();

    const bool initialPreemptionMode = commandQueuePreemptionMode == NEO::PreemptionMode::Initial;
    const bool stateSipRequired = (initialPreemptionMode && devicePreemption == NEO::PreemptionMode::MidThread) ||
                                  (neoDevice->getDebugger() && NEO::Debugger::isDebugEnabled(internalUsage));

    if (initialPreemptionMode) {
        preemptionSize += NEO::PreemptionHelper::getRequiredPreambleSize<GfxFamily>(*neoDevice);
    }

    if (stateSipRequired) {
        preemptionSize += NEO::PreemptionHelper::getRequiredStateSipCmdSize<GfxFamily>(*neoDevice);
    }

    preemptionSize += NEO::PreemptionHelper::getRequiredCmdStreamSize<GfxFamily>(devicePreemption, commandQueuePreemptionMode);
    statePreemption = devicePreemption;

    if (NEO::Debugger::isDebugEnabled(internalUsage) && !commandQueueDebugCmdsProgrammed) {
        debuggerCmdsSize += NEO::PreambleHelper<GfxFamily>::getKernelDebuggingCommandsSize(neoDevice->getSourceLevelDebugger() != nullptr);
    }

    if (devicePreemption == NEO::PreemptionMode::MidThread) {
        spaceForResidency += residencyContainerSpaceForPreemption;
    }

    bool directSubmissionEnabled = csr->isDirectSubmissionEnabled();

    L0::Fence *fence = nullptr;

    device->activateMetricGroups();

    size_t totalCmdBuffers = 0;
    uint32_t perThreadScratchSpaceSize = 0;
    for (auto i = 0u; i < numCommandLists; i++) {
        auto commandList = CommandList::fromHandle(phCommandLists[i]);

        bool indirectAllocationsAllowed = commandList->hasIndirectAllocationsAllowed();
        if (indirectAllocationsAllowed) {
            UnifiedMemoryControls unifiedMemoryControls = commandList->getUnifiedMemoryControls();

            auto svmAllocsManager = device->getDriverHandle()->getSvmAllocsManager();
            svmAllocsManager->addInternalAllocationsToResidencyContainer(neoDevice->getRootDeviceIndex(),
                                                                         commandList->commandContainer.getResidencyContainer(),
                                                                         unifiedMemoryControls.generateMask());
        }

        totalCmdBuffers += commandList->commandContainer.getCmdBufferAllocations().size();
        spaceForResidency += commandList->commandContainer.getResidencyContainer().size();
        auto commandListPreemption = commandList->getCommandListPreemptionMode();
        if (statePreemption != commandListPreemption) {
            preemptionSize += sizeof(PIPE_CONTROL);
            preemptionSize += NEO::PreemptionHelper::getRequiredCmdStreamSize<GfxFamily>(commandListPreemption, statePreemption);
            statePreemption = commandListPreemption;
        }

        if (perThreadScratchSpaceSize < commandList->getCommandListPerThreadScratchSize()) {
            perThreadScratchSpaceSize = commandList->getCommandListPerThreadScratchSize();
        }

        if (commandList->getCommandListPerThreadScratchSize() != 0) {
            if (commandList->commandContainer.getIndirectHeap(NEO::HeapType::SURFACE_STATE) != nullptr) {
                heapContainer.push_back(commandList->commandContainer.getIndirectHeap(NEO::HeapType::SURFACE_STATE)->getGraphicsAllocation());
            }
            for (auto element : commandList->commandContainer.sshAllocations) {
                heapContainer.push_back(element);
            }
        }
    }

    size_t linearStreamSizeEstimate = totalCmdBuffers * sizeof(MI_BATCH_BUFFER_START);
    linearStreamSizeEstimate += csr->getCmdsSizeForHardwareContext();

    if (directSubmissionEnabled) {
        linearStreamSizeEstimate += sizeof(MI_BATCH_BUFFER_START);
    } else {
        linearStreamSizeEstimate += sizeof(MI_BATCH_BUFFER_END);
    }

    if (hFence) {
        fence = Fence::fromHandle(hFence);
        spaceForResidency += residencyContainerSpaceForFence;
        linearStreamSizeEstimate += isCopyOnlyCommandQueue ? NEO::EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite() : NEO::MemorySynchronizationCommands<GfxFamily>::getSizeForPipeControlWithPostSyncOperation(device->getHwInfo());
    }

    spaceForResidency += residencyContainerSpaceForTagWrite;

    residencyContainer.reserve(spaceForResidency);

    auto scratchSpaceController = csr->getScratchSpaceController();
    bool gsbaStateDirty = false;
    bool frontEndStateDirty = false;
    handleScratchSpace(residencyContainer,
                       heapContainer,
                       scratchSpaceController,
                       gsbaStateDirty, frontEndStateDirty,
                       perThreadScratchSpaceSize);

    gsbaStateDirty |= csr->getGSBAStateDirty();
    frontEndStateDirty |= csr->getMediaVFEStateDirty();
    if (!isCopyOnlyCommandQueue) {

        if (!gpgpuEnabled) {
            linearStreamSizeEstimate += estimatePipelineSelect();
        }

        if (frontEndStateDirty) {
            linearStreamSizeEstimate += estimateFrontEndCmdSize();
        }

        if (gsbaStateDirty) {
            linearStreamSizeEstimate += estimateStateBaseAddressCmdSize();
        }

        linearStreamSizeEstimate += preemptionSize + debuggerCmdsSize;
    }

    linearStreamSizeEstimate += isCopyOnlyCommandQueue ? NEO::EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite() : NEO::MemorySynchronizationCommands<GfxFamily>::getSizeForPipeControlWithPostSyncOperation(device->getHwInfo());
    size_t alignedSize = alignUp<size_t>(linearStreamSizeEstimate, minCmdBufferPtrAlign);
    size_t padding = alignedSize - linearStreamSizeEstimate;
    reserveLinearStreamSize(alignedSize);
    NEO::LinearStream child(commandStream->getSpace(alignedSize), alignedSize);

    const auto globalFenceAllocation = csr->getGlobalFenceAllocation();
    if (globalFenceAllocation) {
        residencyContainer.push_back(globalFenceAllocation);
    }

    csr->programHardwareContext(child);

    if (NEO::Debugger::isDebugEnabled(internalUsage) && device->getL0Debugger()) {
        residencyContainer.push_back(device->getL0Debugger()->getSbaTrackingBuffer(csr->getOsContext().getContextId()));
    }

    if (!isCopyOnlyCommandQueue) {
        if (!gpgpuEnabled) {
            programPipelineSelect(child);
        }

        if (NEO::Debugger::isDebugEnabled(internalUsage) && !commandQueueDebugCmdsProgrammed && neoDevice->getSourceLevelDebugger()) {
            NEO::PreambleHelper<GfxFamily>::programKernelDebugging(&child);
            commandQueueDebugCmdsProgrammed = true;
        }

        if (frontEndStateDirty) {
            programFrontEnd(scratchSpaceController->getScratchPatchAddress(), scratchSpaceController->getPerThreadScratchSpaceSize(), child);
        }
        if (gsbaStateDirty) {
            auto indirectHeap = CommandList::fromHandle(phCommandLists[0])->commandContainer.getIndirectHeap(NEO::HeapType::INDIRECT_OBJECT);
            programStateBaseAddress(scratchSpaceController->calculateNewGSH(), indirectHeap->getGraphicsAllocation()->isAllocatedInLocalMemoryPool(), child);
        }

        if (initialPreemptionMode) {
            NEO::PreemptionHelper::programCsrBaseAddress<GfxFamily>(child, *neoDevice, csr->getPreemptionAllocation());
        }

        if (stateSipRequired) {
            NEO::PreemptionHelper::programStateSip<GfxFamily>(child, *neoDevice);
        }

        NEO::PreemptionHelper::programCmdStream<GfxFamily>(child,
                                                           devicePreemption,
                                                           commandQueuePreemptionMode,
                                                           csr->getPreemptionAllocation());
        commandQueuePreemptionMode = devicePreemption;
        statePreemption = commandQueuePreemptionMode;

        const bool sipKernelUsed = devicePreemption == NEO::PreemptionMode::MidThread ||
                                   (neoDevice->getDebugger() != nullptr && NEO::Debugger::isDebugEnabled(internalUsage));

        if (devicePreemption == NEO::PreemptionMode::MidThread) {
            residencyContainer.push_back(csr->getPreemptionAllocation());
        }

        if (sipKernelUsed) {
            auto sipIsa = NEO::SipKernel::getSipKernelAllocation(*neoDevice);
            residencyContainer.push_back(sipIsa);
        }

        if (NEO::Debugger::isDebugEnabled(internalUsage) && neoDevice->getDebugger()) {
            UNRECOVERABLE_IF(device->getDebugSurface() == nullptr);
            residencyContainer.push_back(device->getDebugSurface());
        }
    }

    NEO::PageFaultManager *pageFaultManager = nullptr;
    if (performMigration) {
        pageFaultManager = device->getDriverHandle()->getMemoryManager()->getPageFaultManager();
        if (pageFaultManager == nullptr) {
            performMigration = false;
        }
    }

    for (auto i = 0u; i < numCommandLists; ++i) {
        auto commandList = CommandList::fromHandle(phCommandLists[i]);
        auto cmdBufferAllocations = commandList->commandContainer.getCmdBufferAllocations();
        auto cmdBufferCount = cmdBufferAllocations.size();

        auto commandListPreemption = commandList->getCommandListPreemptionMode();
        if (statePreemption != commandListPreemption) {
            NEO::PipeControlArgs args;
            NEO::MemorySynchronizationCommands<GfxFamily>::addPipeControl(child, args);
            NEO::PreemptionHelper::programCmdStream<GfxFamily>(child,
                                                               commandListPreemption,
                                                               statePreemption,
                                                               csr->getPreemptionAllocation());
            statePreemption = commandListPreemption;
        }

        for (size_t iter = 0; iter < cmdBufferCount; iter++) {
            auto allocation = cmdBufferAllocations[iter];
            NEO::EncodeBatchBufferStartOrEnd<GfxFamily>::programBatchBufferStart(&child, allocation->getGpuAddress(), true);
        }

        printfFunctionContainer.insert(printfFunctionContainer.end(),
                                       commandList->getPrintfFunctionContainer().begin(),
                                       commandList->getPrintfFunctionContainer().end());

        for (auto alloc : commandList->commandContainer.getResidencyContainer()) {
            if (residencyContainer.end() ==
                std::find(residencyContainer.begin(), residencyContainer.end(), alloc)) {
                residencyContainer.push_back(alloc);

                if (performMigration) {
                    if (alloc &&
                        (alloc->getAllocationType() == NEO::GraphicsAllocation::AllocationType::SVM_GPU ||
                         alloc->getAllocationType() == NEO::GraphicsAllocation::AllocationType::SVM_CPU)) {
                        pageFaultManager->moveAllocationToGpuDomain(reinterpret_cast<void *>(alloc->getGpuAddress()));
                    }
                }
            }
        }
    }

    if (performMigration) {
        DriverHandleImp *driverHandleImp = static_cast<DriverHandleImp *>(device->getDriverHandle());
        std::lock_guard<std::mutex> lock(driverHandleImp->sharedMakeResidentAllocationsLock);
        for (auto alloc : driverHandleImp->sharedMakeResidentAllocations) {
            pageFaultManager->moveAllocationToGpuDomain(reinterpret_cast<void *>(alloc.second->getGpuAddress()));
        }
    }

    if (stateSipRequired) {
        NEO::PreemptionHelper::programStateSipEndWa<GfxFamily>(child, *neoDevice);
    }

    commandQueuePreemptionMode = statePreemption;

    if (hFence) {
        residencyContainer.push_back(&fence->getAllocation());
        if (isCopyOnlyCommandQueue) {
            NEO::EncodeMiFlushDW<GfxFamily>::programMiFlushDw(child, fence->getGpuAddress(), Fence::STATE_SIGNALED, false, true);
        } else {
            NEO::PipeControlArgs args(true);
            NEO::MemorySynchronizationCommands<GfxFamily>::addPipeControlAndProgramPostSyncOperation(
                child, POST_SYNC_OPERATION::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA,
                fence->getGpuAddress(),
                Fence::STATE_SIGNALED,
                device->getHwInfo(),
                args);
        }
    }

    dispatchTaskCountWrite(child, true);

    residencyContainer.push_back(csr->getTagAllocation());
    void *endingCmd = nullptr;
    if (directSubmissionEnabled) {
        endingCmd = child.getSpace(0);
        NEO::EncodeBatchBufferStartOrEnd<GfxFamily>::programBatchBufferStart(&child, 0ull, false);
    } else {
        MI_BATCH_BUFFER_END cmd = GfxFamily::cmdInitBatchBufferEnd;
        auto buffer = child.getSpaceForCmd<MI_BATCH_BUFFER_END>();
        *(MI_BATCH_BUFFER_END *)buffer = cmd;
    }

    if (padding) {
        void *paddingPtr = child.getSpace(padding);
        memset(paddingPtr, 0, padding);
    }

    submitBatchBuffer(ptrDiff(child.getCpuBase(), commandStream->getCpuBase()), residencyContainer, endingCmd);

    this->taskCount = csr->peekTaskCount();

    csr->makeSurfacePackNonResident(residencyContainer);

    if (getSynchronousMode() == ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS) {
        this->synchronize(std::numeric_limits<uint64_t>::max());
    }

    this->residencyContainer.clear();
    this->heapContainer.clear();

    return ZE_RESULT_SUCCESS;
}

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::programFrontEnd(uint64_t scratchAddress, uint32_t perThreadScratchSpaceSize, NEO::LinearStream &commandStream) {
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    UNRECOVERABLE_IF(csr == nullptr);
    NEO::PreambleHelper<GfxFamily>::programVFEState(&commandStream,
                                                    device->getHwInfo(),
                                                    perThreadScratchSpaceSize,
                                                    scratchAddress,
                                                    device->getMaxNumHwThreads(),
                                                    csr->getOsContext().getEngineType(),
                                                    NEO::AdditionalKernelExecInfo::NotApplicable,
                                                    NEO::KernelExecutionType::NotApplicable);
    csr->setMediaVFEStateDirty(false);
}

template <GFXCORE_FAMILY gfxCoreFamily>
size_t CommandQueueHw<gfxCoreFamily>::estimateFrontEndCmdSize() {
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    return NEO::PreambleHelper<GfxFamily>::getVFECommandsSize();
}

template <GFXCORE_FAMILY gfxCoreFamily>
size_t CommandQueueHw<gfxCoreFamily>::estimatePipelineSelect() {

    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    return NEO::PreambleHelper<GfxFamily>::getCmdSizeForPipelineSelect(device->getHwInfo());
}

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::programPipelineSelect(NEO::LinearStream &commandStream) {
    NEO::PipelineSelectArgs args = {0, 0};
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    NEO::PreambleHelper<GfxFamily>::programPipelineSelect(&commandStream, args, device->getHwInfo());
    gpgpuEnabled = true;
}

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::dispatchTaskCountWrite(NEO::LinearStream &commandStream, bool flushDataCache) {
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;
    using POST_SYNC_OPERATION = typename PIPE_CONTROL::POST_SYNC_OPERATION;

    UNRECOVERABLE_IF(csr == nullptr);

    auto taskCountToWrite = csr->peekTaskCount() + 1;
    auto gpuAddress = static_cast<uint64_t>(csr->getTagAllocation()->getGpuAddress());

    if (isCopyOnlyCommandQueue) {
        NEO::EncodeMiFlushDW<GfxFamily>::programMiFlushDw(commandStream, gpuAddress, taskCountToWrite, false, true);
    } else {
        NEO::PipeControlArgs args(true);
        NEO::MemorySynchronizationCommands<GfxFamily>::addPipeControlAndProgramPostSyncOperation(
            commandStream,
            POST_SYNC_OPERATION::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA,
            gpuAddress,
            taskCountToWrite,
            device->getHwInfo(),
            args);
    }
}
} // namespace L0
