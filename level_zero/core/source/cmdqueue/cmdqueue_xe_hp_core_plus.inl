/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/implicit_scaling.h"
#include "shared/source/command_stream/csr_definitions.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/state_base_address.h"

#include "level_zero/core/source/cmdqueue/cmdqueue_hw.h"

#include "igfxfmid.h"
#include "pipe_control_args.h"

namespace L0 {

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::programStateBaseAddress(uint64_t gsba, bool useLocalMemoryForIndirectHeap, NEO::LinearStream &commandStream) {
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    using STATE_BASE_ADDRESS = typename GfxFamily::STATE_BASE_ADDRESS;
    if (NEO::ApiSpecificConfig::getBindlessConfiguration()) {
        NEO::Device *neoDevice = device->getNEODevice();
        auto globalHeapsBase = neoDevice->getBindlessHeapsHelper()->getGlobalHeapsBase();
        NEO::PipeControlArgs args(true);
        NEO::MemorySynchronizationCommands<GfxFamily>::addPipeControl(commandStream, args);
        auto pSbaCmd = static_cast<STATE_BASE_ADDRESS *>(commandStream.getSpace(sizeof(STATE_BASE_ADDRESS)));
        STATE_BASE_ADDRESS sbaCmd;
        bool multiOsContextCapable = NEO::ImplicitScalingHelper::isImplicitScalingEnabled(neoDevice->getDeviceBitfield(), true);
        NEO::StateBaseAddressHelper<GfxFamily>::programStateBaseAddress(&sbaCmd,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        0,
                                                                        true,
                                                                        (device->getMOCS(true, false) >> 1),
                                                                        neoDevice->getMemoryManager()->getInternalHeapBaseAddress(neoDevice->getRootDeviceIndex(), useLocalMemoryForIndirectHeap),
                                                                        neoDevice->getMemoryManager()->getInternalHeapBaseAddress(neoDevice->getRootDeviceIndex(), neoDevice->getMemoryManager()->isLocalMemoryUsedForIsa(neoDevice->getRootDeviceIndex())),
                                                                        globalHeapsBase,
                                                                        true,
                                                                        true,
                                                                        neoDevice->getGmmHelper(),
                                                                        multiOsContextCapable,
                                                                        NEO::MemoryCompressionState::NotApplicable,
                                                                        false,
                                                                        1u);

        if (NEO::Debugger::isDebugEnabled(internalUsage) && device->getL0Debugger()) {

            NEO::Debugger::SbaAddresses sbaAddresses = {};
            sbaAddresses.BindlessSurfaceStateBaseAddress = sbaCmd.getBindlessSurfaceStateBaseAddress();
            sbaAddresses.DynamicStateBaseAddress = sbaCmd.getDynamicStateBaseAddress();
            sbaAddresses.GeneralStateBaseAddress = sbaCmd.getGeneralStateBaseAddress();
            sbaAddresses.IndirectObjectBaseAddress = sbaCmd.getIndirectObjectBaseAddress();
            sbaAddresses.InstructionBaseAddress = sbaCmd.getInstructionBaseAddress();
            sbaAddresses.SurfaceStateBaseAddress = sbaCmd.getSurfaceStateBaseAddress();

            device->getL0Debugger()->programSbaTrackingCommands(commandStream, sbaAddresses);
        }
        *pSbaCmd = sbaCmd;

        auto heap = neoDevice->getBindlessHeapsHelper()->getHeap(NEO::BindlessHeapsHelper::GLOBAL_SSH);
        auto cmd = GfxFamily::cmdInitStateBindingTablePoolAlloc;
        cmd.setBindingTablePoolBaseAddress(heap->getHeapGpuBase());
        cmd.setBindingTablePoolBufferSize(heap->getHeapSizeInPages());
        cmd.setSurfaceObjectControlStateIndexToMocsTables(neoDevice->getGmmHelper()->getMOCS(GMM_RESOURCE_USAGE_OCL_STATE_HEAP_BUFFER));

        auto buffer = commandStream.getSpace(sizeof(cmd));
        *(typename GfxFamily::_3DSTATE_BINDING_TABLE_POOL_ALLOC *)buffer = cmd;
    }
    csr->setGSBAStateDirty(false);
}

template <GFXCORE_FAMILY gfxCoreFamily>
size_t CommandQueueHw<gfxCoreFamily>::estimateStateBaseAddressCmdSize() {
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    using STATE_BASE_ADDRESS = typename GfxFamily::STATE_BASE_ADDRESS;
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;
    using _3DSTATE_BINDING_TABLE_POOL_ALLOC = typename GfxFamily::_3DSTATE_BINDING_TABLE_POOL_ALLOC;
    if (NEO::ApiSpecificConfig::getBindlessConfiguration()) {
        size_t size = sizeof(STATE_BASE_ADDRESS) + sizeof(PIPE_CONTROL) + sizeof(_3DSTATE_BINDING_TABLE_POOL_ALLOC);
        return size;
    } else {
        return 0;
    }
}

constexpr uint32_t maxPtssIndex = 15u;

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::handleScratchSpace(NEO::HeapContainer &sshHeaps,
                                                       NEO::ScratchSpaceController *scratchController,
                                                       bool &gsbaState, bool &frontEndState,
                                                       uint32_t perThreadScratchSpaceSize) {
    if (perThreadScratchSpaceSize > 0) {
        if (sshHeaps.size() > 0) {
            uint32_t offsetIndex = maxPtssIndex * csr->getOsContext().getEngineType() + 1u;
            scratchController->programHeaps(sshHeaps, offsetIndex, perThreadScratchSpaceSize, 0u, csr->peekTaskCount(),
                                            csr->getOsContext(), gsbaState, frontEndState);
        }
        if (NEO::ApiSpecificConfig::getBindlessConfiguration()) {
            scratchController->programBindlessSurfaceStateForScratch(device->getNEODevice()->getBindlessHeapsHelper(), perThreadScratchSpaceSize, 0u, csr->peekTaskCount(),
                                                                     csr->getOsContext(), gsbaState, frontEndState, csr);
        }
        auto scratchAllocation = scratchController->getScratchSpaceAllocation();
        csr->makeResident(*scratchAllocation);
    }
}

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::patchCommands(CommandList &commandList, uint64_t scratchAddress) {
    using GfxFamily = typename NEO::GfxFamilyMapper<gfxCoreFamily>::GfxFamily;
    using CFE_STATE = typename GfxFamily::CFE_STATE;

    uint32_t lowScratchAddress = uint32_t(0xFFFFFFFF & scratchAddress);

    auto &commandsToPatch = commandList.getCommandsToPatch();
    for (auto &commandToPatch : commandsToPatch) {
        switch (commandToPatch.type) {
        case CommandList::CommandToPatch::FrontEndState:
            reinterpret_cast<CFE_STATE *>(commandToPatch.pCommand)->setScratchSpaceBuffer(lowScratchAddress);
            *reinterpret_cast<CFE_STATE *>(commandToPatch.pDestination) = *reinterpret_cast<CFE_STATE *>(commandToPatch.pCommand);
            break;
        default:
            UNRECOVERABLE_IF(true);
        }
    }
}

} // namespace L0
