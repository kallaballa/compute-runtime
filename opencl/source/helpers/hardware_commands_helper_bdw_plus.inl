/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/hw_helper.h"

#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/kernel/kernel.h"

#include "pipe_control_args.h"

namespace NEO {

template <typename GfxFamily>
typename HardwareCommandsHelper<GfxFamily>::INTERFACE_DESCRIPTOR_DATA *HardwareCommandsHelper<GfxFamily>::getInterfaceDescriptor(
    const IndirectHeap &indirectHeap,
    uint64_t offsetInterfaceDescriptor,
    HardwareCommandsHelper<GfxFamily>::INTERFACE_DESCRIPTOR_DATA *inlineInterfaceDescriptor) {
    return static_cast<INTERFACE_DESCRIPTOR_DATA *>(ptrOffset(indirectHeap.getCpuBase(), (size_t)offsetInterfaceDescriptor));
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::setGrfInfo(INTERFACE_DESCRIPTOR_DATA *pInterfaceDescriptor, const Kernel &kernel,
                                                   const size_t &sizeCrossThreadData, const size_t &sizePerThreadData,
                                                   uint32_t rootDeviceIndex) {
    auto grfSize = sizeof(typename GfxFamily::GRF);
    DEBUG_BREAK_IF((sizeCrossThreadData % grfSize) != 0);
    auto numGrfCrossThreadData = static_cast<uint32_t>(sizeCrossThreadData / grfSize);
    DEBUG_BREAK_IF(numGrfCrossThreadData == 0);
    pInterfaceDescriptor->setCrossThreadConstantDataReadLength(numGrfCrossThreadData);

    DEBUG_BREAK_IF((sizePerThreadData % grfSize) != 0);
    auto numGrfPerThreadData = static_cast<uint32_t>(sizePerThreadData / grfSize);

    // at least 1 GRF of perThreadData for each thread in a thread group when sizeCrossThreadData != 0
    numGrfPerThreadData = std::max(numGrfPerThreadData, 1u);
    pInterfaceDescriptor->setConstantIndirectUrbEntryReadLength(numGrfPerThreadData);
}

template <typename GfxFamily>
uint32_t HardwareCommandsHelper<GfxFamily>::additionalSizeRequiredDsh() {
    return sizeof(INTERFACE_DESCRIPTOR_DATA);
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getSizeRequiredCS(const Kernel *kernel) {
    size_t size = 2 * sizeof(typename GfxFamily::MEDIA_STATE_FLUSH) +
                  sizeof(typename GfxFamily::MEDIA_INTERFACE_DESCRIPTOR_LOAD);
    return size;
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getSizeRequiredForCacheFlush(const CommandQueue &commandQueue, const Kernel *kernel, uint64_t postSyncAddress) {
    return kernel->requiresCacheFlushCommand(commandQueue) ? sizeof(typename GfxFamily::PIPE_CONTROL) : 0;
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::sendMediaStateFlush(
    LinearStream &commandStream,
    size_t offsetInterfaceDescriptorData) {

    using MEDIA_STATE_FLUSH = typename GfxFamily::MEDIA_STATE_FLUSH;
    auto pCmd = commandStream.getSpaceForCmd<MEDIA_STATE_FLUSH>();
    MEDIA_STATE_FLUSH cmd = GfxFamily::cmdInitMediaStateFlush;

    cmd.setInterfaceDescriptorOffset(static_cast<uint32_t>(offsetInterfaceDescriptorData));
    *pCmd = cmd;
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::sendMediaInterfaceDescriptorLoad(
    LinearStream &commandStream,
    size_t offsetInterfaceDescriptorData,
    size_t sizeInterfaceDescriptorData) {
    {
        using MEDIA_STATE_FLUSH = typename GfxFamily::MEDIA_STATE_FLUSH;
        auto pCmd = commandStream.getSpaceForCmd<MEDIA_STATE_FLUSH>();
        *pCmd = GfxFamily::cmdInitMediaStateFlush;
    }

    {
        using MEDIA_INTERFACE_DESCRIPTOR_LOAD = typename GfxFamily::MEDIA_INTERFACE_DESCRIPTOR_LOAD;
        auto pCmd = commandStream.getSpaceForCmd<MEDIA_INTERFACE_DESCRIPTOR_LOAD>();
        MEDIA_INTERFACE_DESCRIPTOR_LOAD cmd = GfxFamily::cmdInitMediaInterfaceDescriptorLoad;
        cmd.setInterfaceDescriptorDataStartAddress(static_cast<uint32_t>(offsetInterfaceDescriptorData));
        cmd.setInterfaceDescriptorTotalLength(static_cast<uint32_t>(sizeInterfaceDescriptorData));
        *pCmd = cmd;
    }
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::programPerThreadData(
    size_t &sizePerThreadData,
    const bool &localIdsGenerationByRuntime,
    LinearStream &ioh,
    uint32_t &simd,
    uint32_t &numChannels,
    const size_t localWorkSize[3],
    Kernel &kernel,
    size_t &sizePerThreadDataTotal,
    size_t &localWorkItems,
    uint32_t rootDeviceIndex) {

    uint32_t grfSize = sizeof(typename GfxFamily::GRF);

    sendPerThreadData(
        ioh,
        simd,
        grfSize,
        numChannels,
        std::array<uint16_t, 3>{{static_cast<uint16_t>(localWorkSize[0]), static_cast<uint16_t>(localWorkSize[1]), static_cast<uint16_t>(localWorkSize[2])}},
        std::array<uint8_t, 3>{{kernel.getKernelInfo(rootDeviceIndex).kernelDescriptor.kernelAttributes.workgroupDimensionsOrder[0],
                                kernel.getKernelInfo(rootDeviceIndex).kernelDescriptor.kernelAttributes.workgroupDimensionsOrder[1],
                                kernel.getKernelInfo(rootDeviceIndex).kernelDescriptor.kernelAttributes.workgroupDimensionsOrder[2]}},
        kernel.usesOnlyImages());

    updatePerThreadDataTotal(sizePerThreadData, simd, numChannels, sizePerThreadDataTotal, localWorkItems);
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::sendCrossThreadData(
    IndirectHeap &indirectHeap,
    Kernel &kernel,
    bool inlineDataProgrammingRequired,
    WALKER_TYPE<GfxFamily> *walkerCmd,
    uint32_t &sizeCrossThreadData,
    uint32_t rootDeviceIndex) {
    indirectHeap.align(WALKER_TYPE<GfxFamily>::INDIRECTDATASTARTADDRESS_ALIGN_SIZE);

    auto offsetCrossThreadData = indirectHeap.getUsed();
    char *pDest = static_cast<char *>(indirectHeap.getSpace(sizeCrossThreadData));
    memcpy_s(pDest, sizeCrossThreadData, kernel.getCrossThreadData(rootDeviceIndex), sizeCrossThreadData);

    if (DebugManager.flags.AddPatchInfoCommentsForAUBDump.get()) {
        FlatBatchBufferHelper::fixCrossThreadDataInfo(kernel.getPatchInfoDataList(), offsetCrossThreadData, indirectHeap.getGraphicsAllocation()->getGpuAddress());
    }

    return offsetCrossThreadData + static_cast<size_t>(indirectHeap.getHeapGpuStartOffset());
}

template <typename GfxFamily>
bool HardwareCommandsHelper<GfxFamily>::resetBindingTablePrefetch(Kernel &kernel) {
    return kernel.isSchedulerKernel || !EncodeSurfaceState<GfxFamily>::doBindingTablePrefetch();
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::setInterfaceDescriptorOffset(
    WALKER_TYPE<GfxFamily> *walkerCmd,
    uint32_t &interfaceDescriptorIndex) {

    walkerCmd->setInterfaceDescriptorOffset(interfaceDescriptorIndex++);
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::programCacheFlushAfterWalkerCommand(LinearStream *commandStream, const CommandQueue &commandQueue, const Kernel *kernel, uint64_t postSyncAddress) {
    PipeControlArgs args(true);
    MemorySynchronizationCommands<GfxFamily>::addPipeControl(*commandStream, args);
}

} // namespace NEO
