/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/csr_definitions.h"
#include "shared/source/command_stream/preemption.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/helpers/address_patch.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/local_id_gen.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/source/helpers/string.h"
#include "shared/source/indirect_heap/indirect_heap.h"

#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/context/context.h"
#include "opencl/source/helpers/dispatch_info.h"
#include "opencl/source/kernel/kernel.h"
#include "opencl/source/program/block_kernel_manager.h"
#include "opencl/source/scheduler/scheduler_kernel.h"

#include <cstring>

namespace NEO {

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getSizeRequiredDSH(
    uint32_t rootDeviceIndex,
    const Kernel &kernel) {
    using INTERFACE_DESCRIPTOR_DATA = typename GfxFamily::INTERFACE_DESCRIPTOR_DATA;
    using SAMPLER_STATE = typename GfxFamily::SAMPLER_STATE;
    const auto &patchInfo = kernel.getKernelInfo(rootDeviceIndex).patchInfo;
    auto samplerCount = patchInfo.samplerStateArray
                            ? patchInfo.samplerStateArray->Count
                            : 0;
    auto totalSize = samplerCount
                         ? alignUp(samplerCount * sizeof(SAMPLER_STATE), INTERFACE_DESCRIPTOR_DATA::SAMPLERSTATEPOINTER_ALIGN_SIZE)
                         : 0;

    auto borderColorSize = patchInfo.samplerStateArray
                               ? patchInfo.samplerStateArray->Offset - patchInfo.samplerStateArray->BorderColorOffset
                               : 0;

    borderColorSize = alignUp(borderColorSize + EncodeStates<GfxFamily>::alignIndirectStatePointer - 1,
                              EncodeStates<GfxFamily>::alignIndirectStatePointer);

    totalSize += borderColorSize + additionalSizeRequiredDsh();

    DEBUG_BREAK_IF(!(totalSize >= kernel.getDynamicStateHeapSize(rootDeviceIndex) || kernel.getKernelInfo(rootDeviceIndex).isVmeWorkload));

    return alignUp(totalSize, EncodeStates<GfxFamily>::alignInterfaceDescriptorData);
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getSizeRequiredIOH(
    uint32_t rootDeviceIndex,
    const Kernel &kernel,
    size_t localWorkSize) {
    typedef typename GfxFamily::WALKER_TYPE WALKER_TYPE;

    auto threadPayload = kernel.getKernelInfo(rootDeviceIndex).patchInfo.threadPayload;
    DEBUG_BREAK_IF(nullptr == threadPayload);

    auto numChannels = PerThreadDataHelper::getNumLocalIdChannels(*threadPayload);
    uint32_t grfSize = sizeof(typename GfxFamily::GRF);
    return alignUp((kernel.getCrossThreadDataSize(rootDeviceIndex) +
                    getPerThreadDataSizeTotal(kernel.getKernelInfo(rootDeviceIndex).getMaxSimdSize(), grfSize, numChannels, localWorkSize)),
                   WALKER_TYPE::INDIRECTDATASTARTADDRESS_ALIGN_SIZE);
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getSizeRequiredSSH(
    const Kernel &kernel, uint32_t rootDeviceIndex) {
    typedef typename GfxFamily::BINDING_TABLE_STATE BINDING_TABLE_STATE;
    auto sizeSSH = kernel.getSurfaceStateHeapSize(rootDeviceIndex);
    sizeSSH += sizeSSH ? BINDING_TABLE_STATE::SURFACESTATEPOINTER_ALIGN_SIZE : 0;
    return sizeSSH;
}

template <typename SizeGetterT, typename... ArgsT>
size_t getSizeRequired(const MultiDispatchInfo &multiDispatchInfo, SizeGetterT &&getSize, ArgsT... args) {
    size_t totalSize = 0;
    auto it = multiDispatchInfo.begin();
    for (auto e = multiDispatchInfo.end(); it != e; ++it) {
        totalSize = alignUp(totalSize, MemoryConstants::cacheLineSize);
        totalSize += getSize(*it, std::forward<ArgsT>(args)...);
    }
    totalSize = alignUp(totalSize, MemoryConstants::pageSize);
    return totalSize;
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getTotalSizeRequiredDSH(
    const MultiDispatchInfo &multiDispatchInfo) {
    return getSizeRequired(multiDispatchInfo, [](const DispatchInfo &dispatchInfo) { return getSizeRequiredDSH(dispatchInfo.getClDevice().getRootDeviceIndex(), *dispatchInfo.getKernel()); });
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getTotalSizeRequiredIOH(
    const MultiDispatchInfo &multiDispatchInfo) {
    return getSizeRequired(multiDispatchInfo, [](const DispatchInfo &dispatchInfo) { return getSizeRequiredIOH(
                                                                                         dispatchInfo.getClDevice().getRootDeviceIndex(),
                                                                                         *dispatchInfo.getKernel(),
                                                                                         Math::computeTotalElementsCount(dispatchInfo.getLocalWorkgroupSize())); });
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getTotalSizeRequiredSSH(
    const MultiDispatchInfo &multiDispatchInfo) {
    return getSizeRequired(multiDispatchInfo, [](const DispatchInfo &dispatchInfo) { return getSizeRequiredSSH(*dispatchInfo.getKernel(), dispatchInfo.getClDevice().getRootDeviceIndex()); });
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::getSshSizeForExecutionModel(const Kernel &kernel, uint32_t rootDeviceIndex) {
    typedef typename GfxFamily::BINDING_TABLE_STATE BINDING_TABLE_STATE;

    size_t totalSize = 0;
    BlockKernelManager *blockManager = kernel.getProgram()->getBlockKernelManager();
    uint32_t blockCount = static_cast<uint32_t>(blockManager->getCount());
    uint32_t maxBindingTableCount = 0;

    totalSize = BINDING_TABLE_STATE::SURFACESTATEPOINTER_ALIGN_SIZE - 1;

    for (uint32_t i = 0; i < blockCount; i++) {
        const KernelInfo *pBlockInfo = blockManager->getBlockKernelInfo(i);
        totalSize += pBlockInfo->heapInfo.SurfaceStateHeapSize;
        totalSize = alignUp(totalSize, BINDING_TABLE_STATE::SURFACESTATEPOINTER_ALIGN_SIZE);

        maxBindingTableCount = std::max(maxBindingTableCount, pBlockInfo->patchInfo.bindingTableState->Count);
    }

    SchedulerKernel &scheduler = kernel.getContext().getSchedulerKernel();

    totalSize += getSizeRequiredSSH(scheduler, rootDeviceIndex);

    totalSize += maxBindingTableCount * sizeof(BINDING_TABLE_STATE) * DeviceQueue::interfaceDescriptorEntries;
    totalSize = alignUp(totalSize, BINDING_TABLE_STATE::SURFACESTATEPOINTER_ALIGN_SIZE);

    return totalSize;
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::sendInterfaceDescriptorData(
    const IndirectHeap &indirectHeap,
    uint64_t offsetInterfaceDescriptor,
    uint64_t kernelStartOffset,
    size_t sizeCrossThreadData,
    size_t sizePerThreadData,
    size_t bindingTablePointer,
    size_t offsetSamplerState,
    uint32_t numSamplers,
    uint32_t threadsPerThreadGroup,
    const Kernel &kernel,
    uint32_t bindingTablePrefetchSize,
    PreemptionMode preemptionMode,
    INTERFACE_DESCRIPTOR_DATA *inlineInterfaceDescriptor,
    const Device &device) {
    using SAMPLER_STATE = typename GfxFamily::SAMPLER_STATE;
    using SHARED_LOCAL_MEMORY_SIZE = typename INTERFACE_DESCRIPTOR_DATA::SHARED_LOCAL_MEMORY_SIZE;

    const auto &hardwareInfo = device.getHardwareInfo();
    auto rootDeviceIndex = device.getRootDeviceIndex();

    // Allocate some memory for the interface descriptor
    auto pInterfaceDescriptor = getInterfaceDescriptor(indirectHeap, offsetInterfaceDescriptor, inlineInterfaceDescriptor);
    auto interfaceDescriptor = GfxFamily::cmdInitInterfaceDescriptorData;

    // Program the kernel start pointer
    interfaceDescriptor.setKernelStartPointerHigh(kernelStartOffset >> 32);
    interfaceDescriptor.setKernelStartPointer((uint32_t)kernelStartOffset);

    // # of threads in thread group should be based on LWS.
    interfaceDescriptor.setNumberOfThreadsInGpgpuThreadGroup(threadsPerThreadGroup);

    interfaceDescriptor.setDenormMode(INTERFACE_DESCRIPTOR_DATA::DENORM_MODE_SETBYKERNEL);

    auto slmTotalSize = kernel.getSlmTotalSize(rootDeviceIndex);

    setGrfInfo(&interfaceDescriptor, kernel, sizeCrossThreadData, sizePerThreadData, rootDeviceIndex);
    EncodeDispatchKernel<GfxFamily>::appendAdditionalIDDFields(&interfaceDescriptor, hardwareInfo, threadsPerThreadGroup, slmTotalSize, SlmPolicy::SlmPolicyNone);

    interfaceDescriptor.setBindingTablePointer(static_cast<uint32_t>(bindingTablePointer));

    interfaceDescriptor.setSamplerStatePointer(static_cast<uint32_t>(offsetSamplerState));

    EncodeDispatchKernel<GfxFamily>::adjustBindingTablePrefetch(interfaceDescriptor, numSamplers, bindingTablePrefetchSize);

    auto programmableIDSLMSize =
        static_cast<SHARED_LOCAL_MEMORY_SIZE>(HwHelperHw<GfxFamily>::get().computeSlmValues(hardwareInfo, slmTotalSize));

    interfaceDescriptor.setSharedLocalMemorySize(programmableIDSLMSize);
    EncodeDispatchKernel<GfxFamily>::programBarrierEnable(interfaceDescriptor,
                                                          kernel.getKernelInfo(rootDeviceIndex).kernelDescriptor.kernelAttributes.barrierCount,
                                                          hardwareInfo);

    PreemptionHelper::programInterfaceDescriptorDataPreemption<GfxFamily>(&interfaceDescriptor, preemptionMode);
    EncodeDispatchKernel<GfxFamily>::adjustInterfaceDescriptorData(interfaceDescriptor, hardwareInfo);

    *pInterfaceDescriptor = interfaceDescriptor;
    return (size_t)offsetInterfaceDescriptor;
}

template <typename GfxFamily>
size_t HardwareCommandsHelper<GfxFamily>::sendIndirectState(
    LinearStream &commandStream,
    IndirectHeap &dsh,
    IndirectHeap &ioh,
    IndirectHeap &ssh,
    Kernel &kernel,
    uint64_t kernelStartOffset,
    uint32_t simd,
    const size_t localWorkSize[3],
    const uint64_t offsetInterfaceDescriptorTable,
    uint32_t &interfaceDescriptorIndex,
    PreemptionMode preemptionMode,
    WALKER_TYPE<GfxFamily> *walkerCmd,
    INTERFACE_DESCRIPTOR_DATA *inlineInterfaceDescriptor,
    bool localIdsGenerationByRuntime,
    const Device &device) {

    using SAMPLER_STATE = typename GfxFamily::SAMPLER_STATE;

    auto rootDeviceIndex = device.getRootDeviceIndex();

    DEBUG_BREAK_IF(simd != 1 && simd != 8 && simd != 16 && simd != 32);
    auto inlineDataProgrammingRequired = HardwareCommandsHelper<GfxFamily>::inlineDataProgrammingRequired(kernel, rootDeviceIndex);

    // Copy the kernel over to the ISH
    const auto &kernelInfo = kernel.getKernelInfo(rootDeviceIndex);
    const auto &patchInfo = kernelInfo.patchInfo;

    ssh.align(BINDING_TABLE_STATE::SURFACESTATEPOINTER_ALIGN_SIZE);
    kernel.patchBindlessSurfaceStateOffsets(device, ssh.getUsed());

    auto dstBindingTablePointer = EncodeSurfaceState<GfxFamily>::pushBindingTableAndSurfaceStates(ssh, (kernelInfo.patchInfo.bindingTableState != nullptr) ? kernelInfo.patchInfo.bindingTableState->Count : 0,
                                                                                                  kernel.getSurfaceStateHeap(rootDeviceIndex), kernel.getSurfaceStateHeapSize(rootDeviceIndex),
                                                                                                  kernel.getNumberOfBindingTableStates(rootDeviceIndex), kernel.getBindingTableOffset(rootDeviceIndex));

    // Copy our sampler state if it exists
    uint32_t samplerStateOffset = 0;
    uint32_t samplerCount = 0;
    if (patchInfo.samplerStateArray) {
        samplerCount = patchInfo.samplerStateArray->Count;
        samplerStateOffset = EncodeStates<GfxFamily>::copySamplerState(&dsh, patchInfo.samplerStateArray->Offset,
                                                                       samplerCount, patchInfo.samplerStateArray->BorderColorOffset,
                                                                       kernel.getDynamicStateHeap(rootDeviceIndex), device.getBindlessHeapsHelper());
    }

    auto threadPayload = kernelInfo.patchInfo.threadPayload;
    DEBUG_BREAK_IF(nullptr == threadPayload);

    auto localWorkItems = localWorkSize[0] * localWorkSize[1] * localWorkSize[2];
    auto threadsPerThreadGroup = static_cast<uint32_t>(getThreadsPerWG(simd, localWorkItems));
    auto numChannels = PerThreadDataHelper::getNumLocalIdChannels(*threadPayload);

    uint32_t sizeCrossThreadData = kernel.getCrossThreadDataSize(rootDeviceIndex);

    size_t offsetCrossThreadData = HardwareCommandsHelper<GfxFamily>::sendCrossThreadData(
        ioh, kernel, inlineDataProgrammingRequired,
        walkerCmd, sizeCrossThreadData, rootDeviceIndex);

    size_t sizePerThreadDataTotal = 0;
    size_t sizePerThreadData = 0;

    HardwareCommandsHelper<GfxFamily>::programPerThreadData(
        sizePerThreadData,
        localIdsGenerationByRuntime,
        ioh,
        simd,
        numChannels,
        localWorkSize,
        kernel,
        sizePerThreadDataTotal,
        localWorkItems,
        rootDeviceIndex);

    uint64_t offsetInterfaceDescriptor = offsetInterfaceDescriptorTable + interfaceDescriptorIndex * sizeof(INTERFACE_DESCRIPTOR_DATA);

    auto bindingTablePrefetchSize = std::min(31u, static_cast<uint32_t>(kernel.getNumberOfBindingTableStates(rootDeviceIndex)));
    if (resetBindingTablePrefetch(kernel)) {
        bindingTablePrefetchSize = 0;
    }

    HardwareCommandsHelper<GfxFamily>::sendInterfaceDescriptorData(
        dsh,
        offsetInterfaceDescriptor,
        kernelStartOffset,
        sizeCrossThreadData,
        sizePerThreadData,
        dstBindingTablePointer,
        samplerStateOffset,
        samplerCount,
        threadsPerThreadGroup,
        kernel,
        bindingTablePrefetchSize,
        preemptionMode,
        inlineInterfaceDescriptor,
        device);

    if (DebugManager.flags.AddPatchInfoCommentsForAUBDump.get()) {
        PatchInfoData patchInfoData(kernelStartOffset, 0, PatchInfoAllocationType::InstructionHeap, dsh.getGraphicsAllocation()->getGpuAddress(), offsetInterfaceDescriptor, PatchInfoAllocationType::DynamicStateHeap);
        kernel.getPatchInfoDataList().push_back(patchInfoData);
    }

    // Program media state flush to set interface descriptor offset
    sendMediaStateFlush(
        commandStream,
        interfaceDescriptorIndex);

    DEBUG_BREAK_IF(offsetCrossThreadData % 64 != 0);
    walkerCmd->setIndirectDataStartAddress(static_cast<uint32_t>(offsetCrossThreadData));
    setInterfaceDescriptorOffset(walkerCmd, interfaceDescriptorIndex);

    auto indirectDataLength = alignUp(static_cast<uint32_t>(sizeCrossThreadData + sizePerThreadDataTotal),
                                      WALKER_TYPE<GfxFamily>::INDIRECTDATASTARTADDRESS_ALIGN_SIZE);
    walkerCmd->setIndirectDataLength(indirectDataLength);

    return offsetCrossThreadData;
}

template <typename GfxFamily>
void HardwareCommandsHelper<GfxFamily>::updatePerThreadDataTotal(
    size_t &sizePerThreadData,
    uint32_t &simd,
    uint32_t &numChannels,
    size_t &sizePerThreadDataTotal,
    size_t &localWorkItems) {
    uint32_t grfSize = sizeof(typename GfxFamily::GRF);
    sizePerThreadData = getPerThreadSizeLocalIDs(simd, grfSize, numChannels);

    uint32_t localIdSizePerThread = PerThreadDataHelper::getLocalIdSizePerThread(simd, grfSize, numChannels);
    localIdSizePerThread = std::max(localIdSizePerThread, grfSize);

    sizePerThreadDataTotal = getThreadsPerWG(simd, localWorkItems) * localIdSizePerThread;
    DEBUG_BREAK_IF(sizePerThreadDataTotal == 0); // Hardware requires at least 1 GRF of perThreadData for each thread in thread group
}

template <typename GfxFamily>
bool HardwareCommandsHelper<GfxFamily>::inlineDataProgrammingRequired(const Kernel &kernel, uint32_t rootDeviceIndex) {
    auto checkKernelForInlineData = true;
    if (DebugManager.flags.EnablePassInlineData.get() != -1) {
        checkKernelForInlineData = !!DebugManager.flags.EnablePassInlineData.get();
    }
    if (checkKernelForInlineData) {
        return kernel.getKernelInfo(rootDeviceIndex).patchInfo.threadPayload->PassInlineData;
    }
    return false;
}

template <typename GfxFamily>
bool HardwareCommandsHelper<GfxFamily>::kernelUsesLocalIds(const Kernel &kernel, uint32_t rootDeviceIndex) {
    return (kernel.getKernelInfo(rootDeviceIndex).patchInfo.threadPayload->LocalIDXPresent ||
            kernel.getKernelInfo(rootDeviceIndex).patchInfo.threadPayload->LocalIDYPresent ||
            kernel.getKernelInfo(rootDeviceIndex).patchInfo.threadPayload->LocalIDZPresent);
}

} // namespace NEO
