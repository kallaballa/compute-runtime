/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/kernel/kernel.h"

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/device_binary_format/patchtokens_decoder.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/debug_helpers.h"
#include "shared/source/helpers/get_info.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/kernel_helpers.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/memory_manager/unified_memory_manager.h"

#include "opencl/source/accelerators/intel_accelerator.h"
#include "opencl/source/accelerators/intel_motion_estimation.h"
#include "opencl/source/built_ins/builtins_dispatch_builder.h"
#include "opencl/source/cl_device/cl_device.h"
#include "opencl/source/command_queue/command_queue.h"
#include "opencl/source/command_queue/gpgpu_walker.h"
#include "opencl/source/context/context.h"
#include "opencl/source/device_queue/device_queue.h"
#include "opencl/source/execution_model/device_enqueue.h"
#include "opencl/source/gtpin/gtpin_notify.h"
#include "opencl/source/helpers/cl_hw_helper.h"
#include "opencl/source/helpers/dispatch_info.h"
#include "opencl/source/helpers/get_info_status_mapper.h"
#include "opencl/source/helpers/per_thread_data.h"
#include "opencl/source/helpers/sampler_helpers.h"
#include "opencl/source/helpers/surface_formats.h"
#include "opencl/source/kernel/image_transformer.h"
#include "opencl/source/kernel/kernel.inl"
#include "opencl/source/kernel/kernel_info_cl.h"
#include "opencl/source/mem_obj/buffer.h"
#include "opencl/source/mem_obj/image.h"
#include "opencl/source/mem_obj/pipe.h"
#include "opencl/source/memory_manager/mem_obj_surface.h"
#include "opencl/source/platform/platform.h"
#include "opencl/source/program/block_kernel_manager.h"
#include "opencl/source/program/kernel_info.h"
#include "opencl/source/sampler/sampler.h"

#include "patch_list.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace iOpenCL;

namespace NEO {
class Surface;

uint32_t Kernel::dummyPatchLocation = 0xbaddf00d;

Kernel::Kernel(Program *programArg, const KernelInfoContainer &kernelInfosArg, bool schedulerKernel)
    : isParentKernel(kernelInfosArg[programArg->getDevices()[0]->getRootDeviceIndex()]->kernelDescriptor.kernelAttributes.flags.usesDeviceSideEnqueue),
      isSchedulerKernel(schedulerKernel),
      executionEnvironment(programArg->getExecutionEnvironment()),
      program(programArg),
      deviceVector(programArg->getDevices()),
      kernelInfos(kernelInfosArg),
      defaultRootDeviceIndex(programArg->getDevices()[0]->getRootDeviceIndex()) {
    kernelDeviceInfos.resize(program->getMaxRootDeviceIndex() + 1);
    program->retain();
    program->retainForKernel();
    imageTransformer.reset(new ImageTransformer);
    for (const auto &pClDevice : deviceVector) {
        auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
        kernelDeviceInfos[rootDeviceIndex].maxKernelWorkGroupSize = static_cast<uint32_t>(pClDevice->getSharedDeviceInfo().maxWorkGroupSize);
        kernelDeviceInfos[rootDeviceIndex].slmTotalSize = kernelInfosArg[rootDeviceIndex]->workloadInfo.slmStaticSize;
    }
}

Kernel::~Kernel() {
    for (auto &kernelDeviceInfo : kernelDeviceInfos) {
        delete[] kernelDeviceInfo.crossThreadData;
        kernelDeviceInfo.crossThreadData = nullptr;
        kernelDeviceInfo.crossThreadDataSize = 0;

        if (kernelDeviceInfo.privateSurface) {
            program->peekExecutionEnvironment().memoryManager->checkGpuUsageAndDestroyGraphicsAllocations(kernelDeviceInfo.privateSurface);
            kernelDeviceInfo.privateSurface = nullptr;
        }
    }

    if (kernelReflectionSurface) {
        program->peekExecutionEnvironment().memoryManager->freeGraphicsMemory(kernelReflectionSurface);
        kernelReflectionSurface = nullptr;
    }

    for (uint32_t i = 0; i < patchedArgumentsNum; i++) {
        if (SAMPLER_OBJ == getKernelArguments()[i].type) {
            auto sampler = castToObject<Sampler>(kernelArguments.at(i).object);
            if (sampler) {
                sampler->decRefInternal();
            }
        }
    }

    kernelArgHandlers.clear();
    program->releaseForKernel();
    program->release();
}

// Checks if patch offset is invalid (undefined)
inline bool isInvalidOffset(uint32_t offset) {
    return (offset == KernelArgInfo::undefinedOffset);
}

// Checks if patch offset is valid
inline bool isValidOffset(uint32_t offset) {
    return isInvalidOffset(offset) == false;
}

// If dstOffsetBytes is not an invalid offset, then patches dst at dstOffsetBytes
// with src casted to DstT type.
template <typename DstT, typename SrcT>
inline void patch(const SrcT &src, void *dst, uint32_t dstOffsetBytes) {
    if (isInvalidOffset(dstOffsetBytes)) {
        return;
    }

    DstT *patchLocation = reinterpret_cast<DstT *>(ptrOffset(dst, dstOffsetBytes));
    *patchLocation = static_cast<DstT>(src);
}

template <typename PatchTokenT>
void Kernel::patchWithImplicitSurface(void *ptrToPatchInCrossThreadData, GraphicsAllocation &allocation, const Device &device, const PatchTokenT &patch) {
    uint32_t crossThreadDataOffset = patch.DataParamOffset;
    uint32_t pointerSize = patch.DataParamSize;
    uint32_t sshOffset = patch.SurfaceStateHeapOffset;
    auto rootDeviceIndex = device.getRootDeviceIndex();
    void *crossThreadData = getCrossThreadData(rootDeviceIndex);
    void *ssh = getSurfaceStateHeap(rootDeviceIndex);
    if (crossThreadData != nullptr) {
        auto pp = ptrOffset(crossThreadData, crossThreadDataOffset);
        uintptr_t addressToPatch = reinterpret_cast<uintptr_t>(ptrToPatchInCrossThreadData);
        patchWithRequiredSize(pp, pointerSize, addressToPatch);
        if (DebugManager.flags.AddPatchInfoCommentsForAUBDump.get()) {
            PatchInfoData patchInfoData(addressToPatch, 0u, PatchInfoAllocationType::KernelArg, reinterpret_cast<uint64_t>(crossThreadData), crossThreadDataOffset, PatchInfoAllocationType::IndirectObjectHeap, pointerSize);
            this->patchInfoDataList.push_back(patchInfoData);
        }
    }

    if (ssh) {
        auto surfaceState = ptrOffset(ssh, sshOffset);
        void *addressToPatch = reinterpret_cast<void *>(allocation.getGpuAddressToPatch());
        size_t sizeToPatch = allocation.getUnderlyingBufferSize();
        Buffer::setSurfaceState(&device, surfaceState, false, false, sizeToPatch, addressToPatch, 0, &allocation, 0, 0);
    }
}

template void Kernel::patchWithImplicitSurface(void *ptrToPatchInCrossThreadData, GraphicsAllocation &allocation, const Device &device, const SPatchAllocateStatelessGlobalMemorySurfaceWithInitialization &patch);

template void Kernel::patchWithImplicitSurface(void *ptrToPatchInCrossThreadData, GraphicsAllocation &allocation, const Device &device, const SPatchAllocateStatelessPrivateSurface &patch);

template void Kernel::patchWithImplicitSurface(void *ptrToPatchInCrossThreadData, GraphicsAllocation &allocation, const Device &device, const SPatchAllocateStatelessConstantMemorySurfaceWithInitialization &patch);

cl_int Kernel::initialize() {
    std::bitset<64> isDeviceInitialized{};
    for (auto &pClDevice : getDevices()) {
        auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
        if (isDeviceInitialized.test(rootDeviceIndex)) {
            continue;
        }
        reconfigureKernel(rootDeviceIndex);
        auto &hwInfo = pClDevice->getHardwareInfo();
        auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);
        auto &kernelDeviceInfo = kernelDeviceInfos[rootDeviceIndex];
        auto &kernelInfo = *kernelInfos[rootDeviceIndex];
        auto &kernelDescriptor = kernelInfo.kernelDescriptor;
        auto maxSimdSize = kernelInfo.getMaxSimdSize();
        const auto &workloadInfo = kernelInfo.workloadInfo;
        const auto &heapInfo = kernelInfo.heapInfo;
        const auto &patchInfo = kernelInfo.patchInfo;

        if (maxSimdSize != 1 && maxSimdSize < hwHelper.getMinimalSIMDSize()) {
            return CL_INVALID_KERNEL;
        }

        kernelDeviceInfo.crossThreadDataSize = patchInfo.dataParameterStream
                                                   ? patchInfo.dataParameterStream->DataParameterStreamSize
                                                   : 0;

        // now allocate our own cross-thread data, if necessary
        if (kernelDeviceInfo.crossThreadDataSize) {
            kernelDeviceInfo.crossThreadData = new char[kernelDeviceInfo.crossThreadDataSize];

            if (kernelInfo.crossThreadData) {
                memcpy_s(kernelDeviceInfo.crossThreadData, kernelDeviceInfo.crossThreadDataSize,
                         kernelInfo.crossThreadData, kernelDeviceInfo.crossThreadDataSize);
            } else {
                memset(kernelDeviceInfo.crossThreadData, 0x00, kernelDeviceInfo.crossThreadDataSize);
            }

            auto crossThread = reinterpret_cast<uint32_t *>(kernelDeviceInfo.crossThreadData);
            kernelDeviceInfo.globalWorkOffsetX = workloadInfo.globalWorkOffsetOffsets[0] != WorkloadInfo::undefinedOffset
                                                     ? ptrOffset(crossThread, workloadInfo.globalWorkOffsetOffsets[0])
                                                     : kernelDeviceInfo.globalWorkOffsetX;
            kernelDeviceInfo.globalWorkOffsetY = workloadInfo.globalWorkOffsetOffsets[1] != WorkloadInfo::undefinedOffset
                                                     ? ptrOffset(crossThread, workloadInfo.globalWorkOffsetOffsets[1])
                                                     : kernelDeviceInfo.globalWorkOffsetY;
            kernelDeviceInfo.globalWorkOffsetZ = workloadInfo.globalWorkOffsetOffsets[2] != WorkloadInfo::undefinedOffset
                                                     ? ptrOffset(crossThread, workloadInfo.globalWorkOffsetOffsets[2])
                                                     : kernelDeviceInfo.globalWorkOffsetZ;

            kernelDeviceInfo.localWorkSizeX = workloadInfo.localWorkSizeOffsets[0] != WorkloadInfo::undefinedOffset
                                                  ? ptrOffset(crossThread, workloadInfo.localWorkSizeOffsets[0])
                                                  : kernelDeviceInfo.localWorkSizeX;
            kernelDeviceInfo.localWorkSizeY = workloadInfo.localWorkSizeOffsets[1] != WorkloadInfo::undefinedOffset
                                                  ? ptrOffset(crossThread, workloadInfo.localWorkSizeOffsets[1])
                                                  : kernelDeviceInfo.localWorkSizeY;
            kernelDeviceInfo.localWorkSizeZ = workloadInfo.localWorkSizeOffsets[2] != WorkloadInfo::undefinedOffset
                                                  ? ptrOffset(crossThread, workloadInfo.localWorkSizeOffsets[2])
                                                  : kernelDeviceInfo.localWorkSizeZ;

            kernelDeviceInfo.localWorkSizeX2 = workloadInfo.localWorkSizeOffsets2[0] != WorkloadInfo::undefinedOffset
                                                   ? ptrOffset(crossThread, workloadInfo.localWorkSizeOffsets2[0])
                                                   : kernelDeviceInfo.localWorkSizeX2;
            kernelDeviceInfo.localWorkSizeY2 = workloadInfo.localWorkSizeOffsets2[1] != WorkloadInfo::undefinedOffset
                                                   ? ptrOffset(crossThread, workloadInfo.localWorkSizeOffsets2[1])
                                                   : kernelDeviceInfo.localWorkSizeY2;
            kernelDeviceInfo.localWorkSizeZ2 = workloadInfo.localWorkSizeOffsets2[2] != WorkloadInfo::undefinedOffset
                                                   ? ptrOffset(crossThread, workloadInfo.localWorkSizeOffsets2[2])
                                                   : kernelDeviceInfo.localWorkSizeZ2;

            kernelDeviceInfo.globalWorkSizeX = workloadInfo.globalWorkSizeOffsets[0] != WorkloadInfo::undefinedOffset
                                                   ? ptrOffset(crossThread, workloadInfo.globalWorkSizeOffsets[0])
                                                   : kernelDeviceInfo.globalWorkSizeX;
            kernelDeviceInfo.globalWorkSizeY = workloadInfo.globalWorkSizeOffsets[1] != WorkloadInfo::undefinedOffset
                                                   ? ptrOffset(crossThread, workloadInfo.globalWorkSizeOffsets[1])
                                                   : kernelDeviceInfo.globalWorkSizeY;
            kernelDeviceInfo.globalWorkSizeZ = workloadInfo.globalWorkSizeOffsets[2] != WorkloadInfo::undefinedOffset
                                                   ? ptrOffset(crossThread, workloadInfo.globalWorkSizeOffsets[2])
                                                   : kernelDeviceInfo.globalWorkSizeZ;

            kernelDeviceInfo.enqueuedLocalWorkSizeX = workloadInfo.enqueuedLocalWorkSizeOffsets[0] != WorkloadInfo::undefinedOffset
                                                          ? ptrOffset(crossThread, workloadInfo.enqueuedLocalWorkSizeOffsets[0])
                                                          : kernelDeviceInfo.enqueuedLocalWorkSizeX;
            kernelDeviceInfo.enqueuedLocalWorkSizeY = workloadInfo.enqueuedLocalWorkSizeOffsets[1] != WorkloadInfo::undefinedOffset
                                                          ? ptrOffset(crossThread, workloadInfo.enqueuedLocalWorkSizeOffsets[1])
                                                          : kernelDeviceInfo.enqueuedLocalWorkSizeY;
            kernelDeviceInfo.enqueuedLocalWorkSizeZ = workloadInfo.enqueuedLocalWorkSizeOffsets[2] != WorkloadInfo::undefinedOffset
                                                          ? ptrOffset(crossThread, workloadInfo.enqueuedLocalWorkSizeOffsets[2])
                                                          : kernelDeviceInfo.enqueuedLocalWorkSizeZ;

            kernelDeviceInfo.numWorkGroupsX = workloadInfo.numWorkGroupsOffset[0] != WorkloadInfo::undefinedOffset
                                                  ? ptrOffset(crossThread, workloadInfo.numWorkGroupsOffset[0])
                                                  : kernelDeviceInfo.numWorkGroupsX;
            kernelDeviceInfo.numWorkGroupsY = workloadInfo.numWorkGroupsOffset[1] != WorkloadInfo::undefinedOffset
                                                  ? ptrOffset(crossThread, workloadInfo.numWorkGroupsOffset[1])
                                                  : kernelDeviceInfo.numWorkGroupsY;
            kernelDeviceInfo.numWorkGroupsZ = workloadInfo.numWorkGroupsOffset[2] != WorkloadInfo::undefinedOffset
                                                  ? ptrOffset(crossThread, workloadInfo.numWorkGroupsOffset[2])
                                                  : kernelDeviceInfo.numWorkGroupsZ;

            kernelDeviceInfo.maxWorkGroupSizeForCrossThreadData = workloadInfo.maxWorkGroupSizeOffset != WorkloadInfo::undefinedOffset
                                                                      ? ptrOffset(crossThread, workloadInfo.maxWorkGroupSizeOffset)
                                                                      : kernelDeviceInfo.maxWorkGroupSizeForCrossThreadData;
            kernelDeviceInfo.workDim = workloadInfo.workDimOffset != WorkloadInfo::undefinedOffset
                                           ? ptrOffset(crossThread, workloadInfo.workDimOffset)
                                           : kernelDeviceInfo.workDim;
            kernelDeviceInfo.dataParameterSimdSize = workloadInfo.simdSizeOffset != WorkloadInfo::undefinedOffset ? ptrOffset(crossThread, workloadInfo.simdSizeOffset) : kernelDeviceInfo.dataParameterSimdSize;
            kernelDeviceInfo.parentEventOffset = workloadInfo.parentEventOffset != WorkloadInfo::undefinedOffset
                                                     ? ptrOffset(crossThread, workloadInfo.parentEventOffset)
                                                     : kernelDeviceInfo.parentEventOffset;
            kernelDeviceInfo.preferredWkgMultipleOffset = workloadInfo.preferredWkgMultipleOffset != WorkloadInfo::undefinedOffset
                                                              ? ptrOffset(crossThread, workloadInfo.preferredWkgMultipleOffset)
                                                              : kernelDeviceInfo.preferredWkgMultipleOffset;

            *kernelDeviceInfo.maxWorkGroupSizeForCrossThreadData = kernelDeviceInfo.maxKernelWorkGroupSize;
            *kernelDeviceInfo.dataParameterSimdSize = maxSimdSize;
            *kernelDeviceInfo.preferredWkgMultipleOffset = maxSimdSize;
            *kernelDeviceInfo.parentEventOffset = WorkloadInfo::invalidParentEvent;
        }

        // allocate our own SSH, if necessary
        kernelDeviceInfo.sshLocalSize = heapInfo.SurfaceStateHeapSize;

        if (kernelDeviceInfo.sshLocalSize) {
            kernelDeviceInfo.pSshLocal = std::make_unique<char[]>(kernelDeviceInfo.sshLocalSize);

            // copy the ssh into our local copy
            memcpy_s(kernelDeviceInfo.pSshLocal.get(), kernelDeviceInfo.sshLocalSize,
                     heapInfo.pSsh, kernelDeviceInfo.sshLocalSize);
        }
        kernelDeviceInfo.numberOfBindingTableStates = (patchInfo.bindingTableState != nullptr) ? patchInfo.bindingTableState->Count : 0;
        kernelDeviceInfo.localBindingTableOffset = (patchInfo.bindingTableState != nullptr) ? patchInfo.bindingTableState->Offset : 0;

        // patch crossthread data and ssh with inline surfaces, if necessary
        auto perHwThreadPrivateMemorySize = PatchTokenBinary::getPerHwThreadPrivateSurfaceSize(patchInfo.pAllocateStatelessPrivateSurface, kernelInfo.getMaxSimdSize());

        if (perHwThreadPrivateMemorySize) {
            kernelDeviceInfo.privateSurfaceSize = KernelHelper::getPrivateSurfaceSize(perHwThreadPrivateMemorySize, pClDevice->getSharedDeviceInfo().computeUnitsUsedForScratch);

            DEBUG_BREAK_IF(kernelDeviceInfo.privateSurfaceSize == 0);
            if (kernelDeviceInfo.privateSurfaceSize > std::numeric_limits<uint32_t>::max()) {
                return CL_OUT_OF_RESOURCES;
            }
            kernelDeviceInfo.privateSurface = executionEnvironment.memoryManager->allocateGraphicsMemoryWithProperties(
                {rootDeviceIndex,
                 static_cast<size_t>(kernelDeviceInfo.privateSurfaceSize),
                 GraphicsAllocation::AllocationType::PRIVATE_SURFACE,
                 pClDevice->getDeviceBitfield()});
            if (kernelDeviceInfo.privateSurface == nullptr) {
                return CL_OUT_OF_RESOURCES;
            }
            const auto &patch = patchInfo.pAllocateStatelessPrivateSurface;
            patchWithImplicitSurface(reinterpret_cast<void *>(kernelDeviceInfo.privateSurface->getGpuAddressToPatch()), *kernelDeviceInfo.privateSurface, pClDevice->getDevice(), *patch);
        }
        if (patchInfo.pAllocateStatelessConstantMemorySurfaceWithInitialization) {
            DEBUG_BREAK_IF(program->getConstantSurface(rootDeviceIndex) == nullptr);
            uintptr_t constMemory = isBuiltIn ? (uintptr_t)program->getConstantSurface(rootDeviceIndex)->getUnderlyingBuffer() : (uintptr_t)program->getConstantSurface(rootDeviceIndex)->getGpuAddressToPatch();

            const auto &patch = patchInfo.pAllocateStatelessConstantMemorySurfaceWithInitialization;
            patchWithImplicitSurface(reinterpret_cast<void *>(constMemory), *program->getConstantSurface(rootDeviceIndex), pClDevice->getDevice(), *patch);
        }

        if (patchInfo.pAllocateStatelessGlobalMemorySurfaceWithInitialization) {
            DEBUG_BREAK_IF(program->getGlobalSurface(rootDeviceIndex) == nullptr);
            uintptr_t globalMemory = isBuiltIn ? (uintptr_t)program->getGlobalSurface(rootDeviceIndex)->getUnderlyingBuffer() : (uintptr_t)program->getGlobalSurface(rootDeviceIndex)->getGpuAddressToPatch();

            const auto &patch = patchInfo.pAllocateStatelessGlobalMemorySurfaceWithInitialization;
            patchWithImplicitSurface(reinterpret_cast<void *>(globalMemory), *program->getGlobalSurface(rootDeviceIndex), pClDevice->getDevice(), *patch);
        }

        if (isValidOffset(kernelDescriptor.payloadMappings.implicitArgs.deviceSideEnqueueEventPoolSurfaceAddress.bindful)) {
            auto surfaceState = ptrOffset(reinterpret_cast<uintptr_t *>(getSurfaceStateHeap(rootDeviceIndex)),
                                          kernelDescriptor.payloadMappings.implicitArgs.deviceSideEnqueueEventPoolSurfaceAddress.bindful);
            Buffer::setSurfaceState(&pClDevice->getDevice(), surfaceState, false, false, 0, nullptr, 0, nullptr, 0, 0);
        }

        if (patchInfo.pAllocateStatelessDefaultDeviceQueueSurface) {

            if (requiresSshForBuffers(rootDeviceIndex)) {
                auto surfaceState = ptrOffset(reinterpret_cast<uintptr_t *>(getSurfaceStateHeap(rootDeviceIndex)),
                                              patchInfo.pAllocateStatelessDefaultDeviceQueueSurface->SurfaceStateHeapOffset);
                Buffer::setSurfaceState(&pClDevice->getDevice(), surfaceState, false, false, 0, nullptr, 0, nullptr, 0, 0);
            }
        }

        setThreadArbitrationPolicy(hwHelper.getDefaultThreadArbitrationPolicy());
        if (false == kernelInfo.kernelDescriptor.kernelAttributes.flags.requiresSubgroupIndependentForwardProgress) {
            setThreadArbitrationPolicy(ThreadArbitrationPolicy::AgeBased);
        }
        patchBlocksSimdSize(rootDeviceIndex);

        auto &clHwHelper = ClHwHelper::get(hwInfo.platform.eRenderCoreFamily);
        auxTranslationRequired = HwHelper::renderCompressedBuffersSupported(hwInfo) && clHwHelper.requiresAuxResolves(kernelInfo);
        if (DebugManager.flags.ForceAuxTranslationEnabled.get() != -1) {
            auxTranslationRequired &= !!DebugManager.flags.ForceAuxTranslationEnabled.get();
        }
        if (auxTranslationRequired) {
            program->getContextPtr()->setResolvesRequiredInKernels(true);
        }

        if (isParentKernel) {
            program->allocateBlockPrivateSurfaces(*pClDevice);
        }
        if (program->isKernelDebugEnabled() && kernelInfo.patchInfo.pAllocateSystemThreadSurface) {
            debugEnabled = true;
        }
        auto numArgs = kernelInfo.kernelArgInfo.size();
        kernelDeviceInfo.slmSizes.resize(numArgs);
        isDeviceInitialized.set(rootDeviceIndex);
    }

    provideInitializationHints();
    // resolve the new kernel info to account for kernel handlers
    // I think by this time we have decoded the binary and know the number of args etc.
    // double check this assumption
    bool usingBuffers = false;
    bool usingImages = false;
    auto &defaultKernelInfo = getDefaultKernelInfo();
    auto numArgs = defaultKernelInfo.kernelArgInfo.size();
    kernelArguments.resize(numArgs);
    kernelArgHandlers.resize(numArgs);
    kernelArgRequiresCacheFlush.resize(numArgs);

    for (uint32_t i = 0; i < numArgs; ++i) {
        storeKernelArg(i, NONE_OBJ, nullptr, nullptr, 0);

        // set the argument handler
        auto &argInfo = defaultKernelInfo.kernelArgInfo[i];
        if (argInfo.metadata.addressQualifier == KernelArgMetadata::AddrLocal) {
            kernelArgHandlers[i] = &Kernel::setArgLocal;
        } else if (argInfo.isAccelerator) {
            kernelArgHandlers[i] = &Kernel::setArgAccelerator;
        } else if (argInfo.metadata.typeQualifiers.pipeQual) {
            kernelArgHandlers[i] = &Kernel::setArgPipe;
            kernelArguments[i].type = PIPE_OBJ;
        } else if (argInfo.isImage) {
            kernelArgHandlers[i] = &Kernel::setArgImage;
            kernelArguments[i].type = IMAGE_OBJ;
            usingImages = true;
        } else if (argInfo.isSampler) {
            kernelArgHandlers[i] = &Kernel::setArgSampler;
            kernelArguments[i].type = SAMPLER_OBJ;
        } else if (argInfo.isBuffer) {
            kernelArgHandlers[i] = &Kernel::setArgBuffer;
            kernelArguments[i].type = BUFFER_OBJ;
            usingBuffers = true;
            allBufferArgsStateful &= static_cast<uint32_t>(argInfo.pureStatefulBufferAccess);
        } else if (argInfo.isDeviceQueue) {
            kernelArgHandlers[i] = &Kernel::setArgDevQueue;
            kernelArguments[i].type = DEVICE_QUEUE_OBJ;
        } else {
            kernelArgHandlers[i] = &Kernel::setArgImmediate;
        }
    }

    if (usingImages && !usingBuffers) {
        usingImagesOnly = true;
    }

    return CL_SUCCESS;
}

cl_int Kernel::cloneKernel(Kernel *pSourceKernel) {
    // copy cross thread data to store arguments set to source kernel with clSetKernelArg on immediate data (non-pointer types)
    for (auto rootDeviceIndex = 0u; rootDeviceIndex < pSourceKernel->kernelDeviceInfos.size(); rootDeviceIndex++) {
        memcpy_s(kernelDeviceInfos[rootDeviceIndex].crossThreadData, kernelDeviceInfos[rootDeviceIndex].crossThreadDataSize,
                 pSourceKernel->kernelDeviceInfos[rootDeviceIndex].crossThreadData, pSourceKernel->kernelDeviceInfos[rootDeviceIndex].crossThreadDataSize);
        DEBUG_BREAK_IF(pSourceKernel->kernelDeviceInfos[rootDeviceIndex].crossThreadDataSize != kernelDeviceInfos[rootDeviceIndex].crossThreadDataSize);
    }

    // copy arguments set to source kernel with clSetKernelArg or clSetKernelArgSVMPointer
    for (uint32_t i = 0; i < pSourceKernel->kernelArguments.size(); i++) {
        if (0 == pSourceKernel->getKernelArgInfo(i).size) {
            // skip copying arguments that haven't been set to source kernel
            continue;
        }
        switch (pSourceKernel->kernelArguments[i].type) {
        case NONE_OBJ:
            // all arguments with immediate data (non-pointer types) have been copied in cross thread data
            storeKernelArg(i, NONE_OBJ, nullptr, nullptr, pSourceKernel->getKernelArgInfo(i).size);
            patchedArgumentsNum++;
            kernelArguments[i].isPatched = true;
            break;
        case SVM_OBJ:
            setArgSvm(i, pSourceKernel->getKernelArgInfo(i).size, const_cast<void *>(pSourceKernel->getKernelArgInfo(i).value),
                      pSourceKernel->getKernelArgInfo(i).pSvmAlloc, pSourceKernel->getKernelArgInfo(i).svmFlags);
            break;
        case SVM_ALLOC_OBJ:
            setArgSvmAlloc(i, const_cast<void *>(pSourceKernel->getKernelArgInfo(i).value),
                           (GraphicsAllocation *)pSourceKernel->getKernelArgInfo(i).object);
            break;
        default:
            setArg(i, pSourceKernel->getKernelArgInfo(i).size, pSourceKernel->getKernelArgInfo(i).value);
            break;
        }
    }

    // copy additional information other than argument values set to source kernel with clSetKernelExecInfo
    for (auto gfxAlloc : pSourceKernel->kernelSvmGfxAllocations) {
        kernelSvmGfxAllocations.push_back(gfxAlloc);
    }

    this->isBuiltIn = pSourceKernel->isBuiltIn;

    return CL_SUCCESS;
}

cl_int Kernel::getInfo(cl_kernel_info paramName, size_t paramValueSize,
                       void *paramValue, size_t *paramValueSizeRet) const {
    cl_int retVal;
    const void *pSrc = nullptr;
    size_t srcSize = GetInfo::invalidSourceSize;
    cl_uint numArgs = 0;
    const _cl_program *prog;
    const _cl_context *ctxt;
    cl_uint refCount = 0;
    uint64_t nonCannonizedGpuAddress = 0llu;
    auto &defaultKernelInfo = getKernelInfo(defaultRootDeviceIndex);

    switch (paramName) {
    case CL_KERNEL_FUNCTION_NAME:
        pSrc = defaultKernelInfo.kernelDescriptor.kernelMetadata.kernelName.c_str();
        srcSize = defaultKernelInfo.kernelDescriptor.kernelMetadata.kernelName.length() + 1;
        break;

    case CL_KERNEL_NUM_ARGS:
        srcSize = sizeof(cl_uint);
        numArgs = static_cast<cl_uint>(defaultKernelInfo.kernelArgInfo.size());
        pSrc = &numArgs;
        break;

    case CL_KERNEL_CONTEXT:
        ctxt = &program->getContext();
        srcSize = sizeof(ctxt);
        pSrc = &ctxt;
        break;

    case CL_KERNEL_PROGRAM:
        prog = program;
        srcSize = sizeof(prog);
        pSrc = &prog;
        break;

    case CL_KERNEL_REFERENCE_COUNT:
        refCount = static_cast<cl_uint>(this->getReference());
        srcSize = sizeof(refCount);
        pSrc = &refCount;
        break;

    case CL_KERNEL_ATTRIBUTES:
        pSrc = defaultKernelInfo.attributes.c_str();
        srcSize = defaultKernelInfo.attributes.length() + 1;
        break;

    case CL_KERNEL_BINARY_PROGRAM_INTEL:
        pSrc = getKernelHeap(defaultRootDeviceIndex);
        srcSize = getKernelHeapSize(defaultRootDeviceIndex);
        break;
    case CL_KERNEL_BINARY_GPU_ADDRESS_INTEL:
        nonCannonizedGpuAddress = GmmHelper::decanonize(defaultKernelInfo.kernelAllocation->getGpuAddress());
        pSrc = &nonCannonizedGpuAddress;
        srcSize = sizeof(nonCannonizedGpuAddress);
        break;
    default:
        getAdditionalInfo(paramName, pSrc, srcSize);
        break;
    }

    auto getInfoStatus = GetInfo::getInfo(paramValue, paramValueSize, pSrc, srcSize);
    retVal = changeGetInfoStatusToCLResultType(getInfoStatus);
    GetInfo::setParamValueReturnSize(paramValueSizeRet, srcSize, getInfoStatus);

    return retVal;
}

cl_int Kernel::getArgInfo(cl_uint argIndx, cl_kernel_arg_info paramName, size_t paramValueSize,
                          void *paramValue, size_t *paramValueSizeRet) const {
    cl_int retVal;
    const void *pSrc = nullptr;
    size_t srcSize = GetInfo::invalidSourceSize;
    auto &defaultKernelInfo = getDefaultKernelInfo();
    auto numArgs = static_cast<cl_uint>(defaultKernelInfo.kernelArgInfo.size());
    const auto &argInfo = defaultKernelInfo.kernelArgInfo[argIndx];

    if (argIndx >= numArgs) {
        retVal = CL_INVALID_ARG_INDEX;
        return retVal;
    }

    cl_kernel_arg_address_qualifier addressQualifier;
    cl_kernel_arg_access_qualifier accessQualifier;
    cl_kernel_arg_type_qualifier typeQualifier;

    switch (paramName) {
    case CL_KERNEL_ARG_ADDRESS_QUALIFIER:
        addressQualifier = asClKernelArgAddressQualifier(argInfo.metadata.getAddressQualifier());
        srcSize = sizeof(addressQualifier);
        pSrc = &addressQualifier;
        break;

    case CL_KERNEL_ARG_ACCESS_QUALIFIER:
        accessQualifier = asClKernelArgAccessQualifier(argInfo.metadata.getAccessQualifier());
        srcSize = sizeof(accessQualifier);
        pSrc = &accessQualifier;
        break;

    case CL_KERNEL_ARG_TYPE_QUALIFIER:
        typeQualifier = asClKernelArgTypeQualifier(argInfo.metadata.typeQualifiers);
        srcSize = sizeof(typeQualifier);
        pSrc = &typeQualifier;
        break;

    case CL_KERNEL_ARG_TYPE_NAME:
        srcSize = argInfo.metadataExtended->type.length() + 1;
        pSrc = argInfo.metadataExtended->type.c_str();
        break;

    case CL_KERNEL_ARG_NAME:
        srcSize = argInfo.metadataExtended->argName.length() + 1;
        pSrc = argInfo.metadataExtended->argName.c_str();
        break;

    default:
        break;
    }

    auto getInfoStatus = GetInfo::getInfo(paramValue, paramValueSize, pSrc, srcSize);
    retVal = changeGetInfoStatusToCLResultType(getInfoStatus);
    GetInfo::setParamValueReturnSize(paramValueSizeRet, srcSize, getInfoStatus);

    return retVal;
}

cl_int Kernel::getWorkGroupInfo(ClDevice &device, cl_kernel_work_group_info paramName,
                                size_t paramValueSize, void *paramValue,
                                size_t *paramValueSizeRet) const {
    cl_int retVal = CL_INVALID_VALUE;
    const void *pSrc = nullptr;
    size_t srcSize = GetInfo::invalidSourceSize;
    struct size_t3 {
        size_t val[3];
    } requiredWorkGroupSize;
    cl_ulong localMemorySize;
    auto rootDeviceIndex = device.getRootDeviceIndex();
    auto &kernelInfo = *kernelInfos[rootDeviceIndex];
    const auto &patchInfo = kernelInfo.patchInfo;
    const auto &kernelDescriptor = kernelInfo.kernelDescriptor;
    size_t preferredWorkGroupSizeMultiple = 0;
    cl_ulong scratchSize;
    cl_ulong privateMemSize;
    size_t maxWorkgroupSize;
    const auto &hwInfo = device.getHardwareInfo();
    auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);
    auto &clHwHelper = ClHwHelper::get(hwInfo.platform.eRenderCoreFamily);
    GetInfoHelper info(paramValue, paramValueSize, paramValueSizeRet);

    switch (paramName) {
    case CL_KERNEL_WORK_GROUP_SIZE:
        maxWorkgroupSize = kernelDeviceInfos[rootDeviceIndex].maxKernelWorkGroupSize;
        if (DebugManager.flags.UseMaxSimdSizeToDeduceMaxWorkgroupSize.get()) {
            auto divisionSize = CommonConstants::maximalSimdSize / kernelInfo.getMaxSimdSize();
            maxWorkgroupSize /= divisionSize;
        }
        srcSize = sizeof(maxWorkgroupSize);
        pSrc = &maxWorkgroupSize;
        break;

    case CL_KERNEL_COMPILE_WORK_GROUP_SIZE:
        requiredWorkGroupSize.val[0] = kernelDescriptor.kernelAttributes.requiredWorkgroupSize[0];
        requiredWorkGroupSize.val[1] = kernelDescriptor.kernelAttributes.requiredWorkgroupSize[1];
        requiredWorkGroupSize.val[2] = kernelDescriptor.kernelAttributes.requiredWorkgroupSize[2];
        srcSize = sizeof(requiredWorkGroupSize);
        pSrc = &requiredWorkGroupSize;
        break;

    case CL_KERNEL_LOCAL_MEM_SIZE:
        localMemorySize = patchInfo.localsurface
                              ? patchInfo.localsurface->TotalInlineLocalMemorySize
                              : 0;
        srcSize = sizeof(localMemorySize);
        pSrc = &localMemorySize;
        break;

    case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
        preferredWorkGroupSizeMultiple = kernelInfo.getMaxSimdSize();
        if (hwHelper.isFusedEuDispatchEnabled(hwInfo)) {
            preferredWorkGroupSizeMultiple *= 2;
        }
        srcSize = sizeof(preferredWorkGroupSizeMultiple);
        pSrc = &preferredWorkGroupSizeMultiple;
        break;

    case CL_KERNEL_SPILL_MEM_SIZE_INTEL:
        scratchSize = kernelInfo.patchInfo.mediavfestate ? kernelInfo.patchInfo.mediavfestate->PerThreadScratchSpace : 0;
        srcSize = sizeof(scratchSize);
        pSrc = &scratchSize;
        break;
    case CL_KERNEL_PRIVATE_MEM_SIZE:
        privateMemSize = clHwHelper.getKernelPrivateMemSize(kernelInfo);
        srcSize = sizeof(privateMemSize);
        pSrc = &privateMemSize;
        break;
    default:
        getAdditionalWorkGroupInfo(paramName, pSrc, srcSize, rootDeviceIndex);
        break;
    }

    auto getInfoStatus = GetInfo::getInfo(paramValue, paramValueSize, pSrc, srcSize);
    retVal = changeGetInfoStatusToCLResultType(getInfoStatus);
    GetInfo::setParamValueReturnSize(paramValueSizeRet, srcSize, getInfoStatus);

    return retVal;
}

cl_int Kernel::getSubGroupInfo(ClDevice &clDevice, cl_kernel_sub_group_info paramName,
                               size_t inputValueSize, const void *inputValue,
                               size_t paramValueSize, void *paramValue,
                               size_t *paramValueSizeRet) const {
    size_t numDimensions = 0;
    size_t WGS = 1;
    auto rootDeviceIndex = clDevice.getRootDeviceIndex();
    const auto &kernelInfo = getKernelInfo(rootDeviceIndex);
    auto maxSimdSize = static_cast<size_t>(kernelInfo.getMaxSimdSize());
    auto maxRequiredWorkGroupSize = static_cast<size_t>(kernelInfo.getMaxRequiredWorkGroupSize(getMaxKernelWorkGroupSize(rootDeviceIndex)));
    auto largestCompiledSIMDSize = static_cast<size_t>(kernelInfo.getMaxSimdSize());

    GetInfoHelper info(paramValue, paramValueSize, paramValueSizeRet);

    if ((paramName == CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT) ||
        (paramName == CL_KERNEL_MAX_NUM_SUB_GROUPS) ||
        (paramName == CL_KERNEL_COMPILE_NUM_SUB_GROUPS)) {
        if (clDevice.areOcl21FeaturesEnabled() == false) {
            return CL_INVALID_OPERATION;
        }
    }

    if ((paramName == CL_KERNEL_MAX_SUB_GROUP_SIZE_FOR_NDRANGE_KHR) ||
        (paramName == CL_KERNEL_SUB_GROUP_COUNT_FOR_NDRANGE_KHR)) {
        if (!inputValue) {
            return CL_INVALID_VALUE;
        }
        if (inputValueSize % sizeof(size_t) != 0) {
            return CL_INVALID_VALUE;
        }
        numDimensions = inputValueSize / sizeof(size_t);
        if (numDimensions == 0 ||
            numDimensions > static_cast<size_t>(clDevice.getDeviceInfo().maxWorkItemDimensions)) {
            return CL_INVALID_VALUE;
        }
    }

    if (paramName == CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT) {
        if (!paramValue) {
            return CL_INVALID_VALUE;
        }
        if (paramValueSize % sizeof(size_t) != 0) {
            return CL_INVALID_VALUE;
        }
        numDimensions = paramValueSize / sizeof(size_t);
        if (numDimensions == 0 ||
            numDimensions > static_cast<size_t>(clDevice.getDeviceInfo().maxWorkItemDimensions)) {
            return CL_INVALID_VALUE;
        }
    }

    switch (paramName) {
    case CL_KERNEL_MAX_SUB_GROUP_SIZE_FOR_NDRANGE_KHR: {
        return changeGetInfoStatusToCLResultType(info.set<size_t>(maxSimdSize));
    }
    case CL_KERNEL_SUB_GROUP_COUNT_FOR_NDRANGE_KHR: {
        for (size_t i = 0; i < numDimensions; i++) {
            WGS *= ((size_t *)inputValue)[i];
        }
        return changeGetInfoStatusToCLResultType(
            info.set<size_t>((WGS / maxSimdSize) + std::min(static_cast<size_t>(1), WGS % maxSimdSize))); // add 1 if WGS % maxSimdSize != 0
    }
    case CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT: {
        auto subGroupsNum = *(size_t *)inputValue;
        auto workGroupSize = subGroupsNum * largestCompiledSIMDSize;
        // return workgroup size in first dimension, the rest shall be 1 in positive case
        if (workGroupSize > maxRequiredWorkGroupSize) {
            workGroupSize = 0;
        }
        // If no work group size can accommodate the requested number of subgroups, return 0 in each element of the returned array.
        switch (numDimensions) {
        case 1:
            return changeGetInfoStatusToCLResultType(info.set<size_t>(workGroupSize));
        case 2:
            struct size_t2 {
                size_t val[2];
            } workGroupSize2;
            workGroupSize2.val[0] = workGroupSize;
            workGroupSize2.val[1] = (workGroupSize > 0) ? 1 : 0;
            return changeGetInfoStatusToCLResultType(info.set<size_t2>(workGroupSize2));
        case 3:
        default:
            struct size_t3 {
                size_t val[3];
            } workGroupSize3;
            workGroupSize3.val[0] = workGroupSize;
            workGroupSize3.val[1] = (workGroupSize > 0) ? 1 : 0;
            workGroupSize3.val[2] = (workGroupSize > 0) ? 1 : 0;
            return changeGetInfoStatusToCLResultType(info.set<size_t3>(workGroupSize3));
        }
    }
    case CL_KERNEL_MAX_NUM_SUB_GROUPS: {
        // round-up maximum number of subgroups
        return changeGetInfoStatusToCLResultType(info.set<size_t>(Math::divideAndRoundUp(maxRequiredWorkGroupSize, largestCompiledSIMDSize)));
    }
    case CL_KERNEL_COMPILE_NUM_SUB_GROUPS: {
        return changeGetInfoStatusToCLResultType(info.set<size_t>(static_cast<size_t>(kernelInfo.kernelDescriptor.kernelMetadata.compiledSubGroupsNumber)));
    }
    case CL_KERNEL_COMPILE_SUB_GROUP_SIZE_INTEL: {
        return changeGetInfoStatusToCLResultType(info.set<size_t>(kernelInfo.kernelDescriptor.kernelMetadata.requiredSubGroupSize));
    }
    default:
        return CL_INVALID_VALUE;
    }
}

const void *Kernel::getKernelHeap(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).heapInfo.pKernelHeap;
}

size_t Kernel::getKernelHeapSize(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).heapInfo.KernelHeapSize;
}

void Kernel::substituteKernelHeap(const Device &device, void *newKernelHeap, size_t newKernelHeapSize) {
    auto rootDeviceIndex = device.getRootDeviceIndex();
    KernelInfo *pKernelInfo = const_cast<KernelInfo *>(&getKernelInfo(rootDeviceIndex));
    void **pKernelHeap = const_cast<void **>(&pKernelInfo->heapInfo.pKernelHeap);
    *pKernelHeap = newKernelHeap;
    auto &heapInfo = pKernelInfo->heapInfo;
    heapInfo.KernelHeapSize = static_cast<uint32_t>(newKernelHeapSize);
    pKernelInfo->isKernelHeapSubstituted = true;
    auto memoryManager = executionEnvironment.memoryManager.get();

    auto currentAllocationSize = pKernelInfo->kernelAllocation->getUnderlyingBufferSize();
    bool status = false;
    if (currentAllocationSize >= newKernelHeapSize) {
        status = memoryManager->copyMemoryToAllocation(pKernelInfo->kernelAllocation, 0, newKernelHeap, newKernelHeapSize);
    } else {
        memoryManager->checkGpuUsageAndDestroyGraphicsAllocations(pKernelInfo->kernelAllocation);
        pKernelInfo->kernelAllocation = nullptr;
        status = pKernelInfo->createKernelAllocation(device, isBuiltIn);
    }
    UNRECOVERABLE_IF(!status);
}

bool Kernel::isKernelHeapSubstituted(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).isKernelHeapSubstituted;
}

uint64_t Kernel::getKernelId(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).kernelId;
}

void Kernel::setKernelId(uint32_t rootDeviceIndex, uint64_t newKernelId) {
    KernelInfo *pKernelInfo = const_cast<KernelInfo *>(&getKernelInfo(rootDeviceIndex));
    pKernelInfo->kernelId = newKernelId;
}
uint32_t Kernel::getStartOffset() const {
    return this->startOffset;
}
void Kernel::setStartOffset(uint32_t offset) {
    this->startOffset = offset;
}

void *Kernel::getSurfaceStateHeap(uint32_t rootDeviceIndex) const {
    return kernelInfos[rootDeviceIndex]->usesSsh ? kernelDeviceInfos[rootDeviceIndex].pSshLocal.get() : nullptr;
}

size_t Kernel::getDynamicStateHeapSize(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).heapInfo.DynamicStateHeapSize;
}

const void *Kernel::getDynamicStateHeap(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).heapInfo.pDsh;
}

size_t Kernel::getSurfaceStateHeapSize(uint32_t rootDeviceIndex) const {
    return kernelInfos[rootDeviceIndex]->usesSsh
               ? kernelDeviceInfos[rootDeviceIndex].sshLocalSize
               : 0;
}

size_t Kernel::getNumberOfBindingTableStates(uint32_t rootDeviceIndex) const {
    return kernelDeviceInfos[rootDeviceIndex].numberOfBindingTableStates;
}

void Kernel::resizeSurfaceStateHeap(uint32_t rootDeviceIndex, void *pNewSsh, size_t newSshSize, size_t newBindingTableCount, size_t newBindingTableOffset) {
    kernelDeviceInfos[rootDeviceIndex].pSshLocal.reset(static_cast<char *>(pNewSsh));
    kernelDeviceInfos[rootDeviceIndex].sshLocalSize = static_cast<uint32_t>(newSshSize);
    kernelDeviceInfos[rootDeviceIndex].numberOfBindingTableStates = newBindingTableCount;
    kernelDeviceInfos[rootDeviceIndex].localBindingTableOffset = newBindingTableOffset;
}

cl_int Kernel::setArg(uint32_t argIndex, size_t argSize, const void *argVal) {
    cl_int retVal = CL_SUCCESS;
    bool updateExposedKernel = true;
    auto argWasUncacheable = false;
    auto &defaultKernelInfo = getDefaultKernelInfo();
    if (defaultKernelInfo.builtinDispatchBuilder != nullptr) {
        updateExposedKernel = defaultKernelInfo.builtinDispatchBuilder->setExplicitArg(argIndex, argSize, argVal, retVal);
    }
    if (updateExposedKernel) {
        if (argIndex >= kernelArgHandlers.size()) {
            return CL_INVALID_ARG_INDEX;
        }
        argWasUncacheable = kernelArguments[argIndex].isStatelessUncacheable;
        auto argHandler = kernelArgHandlers[argIndex];
        retVal = (this->*argHandler)(argIndex, argSize, argVal);
    }
    if (retVal == CL_SUCCESS) {
        if (!kernelArguments[argIndex].isPatched) {
            patchedArgumentsNum++;
            kernelArguments[argIndex].isPatched = true;
        }
        auto argIsUncacheable = kernelArguments[argIndex].isStatelessUncacheable;
        statelessUncacheableArgsCount += (argIsUncacheable ? 1 : 0) - (argWasUncacheable ? 1 : 0);
        resolveArgs();
    }
    return retVal;
}

cl_int Kernel::setArg(uint32_t argIndex, uint32_t argVal) {
    return setArg(argIndex, sizeof(argVal), &argVal);
}

cl_int Kernel::setArg(uint32_t argIndex, uint64_t argVal) {
    return setArg(argIndex, sizeof(argVal), &argVal);
}

cl_int Kernel::setArg(uint32_t argIndex, cl_mem argVal) {
    return setArg(argIndex, sizeof(argVal), &argVal);
}

cl_int Kernel::setArg(uint32_t argIndex, cl_mem argVal, uint32_t mipLevel) {
    return setArgImageWithMipLevel(argIndex, sizeof(argVal), &argVal, mipLevel);
}

void *Kernel::patchBufferOffset(const KernelArgInfo &argInfo, void *svmPtr, GraphicsAllocation *svmAlloc, uint32_t rootDeviceIndex) {
    if (isInvalidOffset(argInfo.offsetBufferOffset)) {
        return svmPtr;
    }
    void *ptrToPatch = svmPtr;
    if (svmAlloc != nullptr) {
        ptrToPatch = reinterpret_cast<void *>(svmAlloc->getGpuAddressToPatch());
    }

    constexpr uint32_t minimumAlignment = 4;
    ptrToPatch = alignDown(ptrToPatch, minimumAlignment);
    DEBUG_BREAK_IF(ptrDiff(svmPtr, ptrToPatch) != static_cast<uint32_t>(ptrDiff(svmPtr, ptrToPatch)));
    uint32_t offsetToPatch = static_cast<uint32_t>(ptrDiff(svmPtr, ptrToPatch));

    patch<uint32_t, uint32_t>(offsetToPatch, getCrossThreadData(rootDeviceIndex), argInfo.offsetBufferOffset);
    return ptrToPatch;
}

cl_int Kernel::setArgSvm(uint32_t argIndex, size_t svmAllocSize, void *svmPtr, GraphicsAllocation *svmAlloc, cl_mem_flags svmFlags) {
    auto rootDeviceIndex = getDevice().getRootDeviceIndex();
    auto &kernelInfo = getKernelInfo(rootDeviceIndex);
    void *ptrToPatch = patchBufferOffset(kernelInfo.kernelArgInfo[argIndex], svmPtr, svmAlloc, rootDeviceIndex);
    setArgImmediate(argIndex, sizeof(void *), &svmPtr);

    storeKernelArg(argIndex, SVM_OBJ, nullptr, svmPtr, sizeof(void *), svmAlloc, svmFlags);

    if (requiresSshForBuffers(rootDeviceIndex)) {
        const auto &kernelArgInfo = kernelInfo.kernelArgInfo[argIndex];
        auto surfaceState = ptrOffset(getSurfaceStateHeap(rootDeviceIndex), kernelArgInfo.offsetHeap);
        Buffer::setSurfaceState(&getDevice().getDevice(), surfaceState, false, false, svmAllocSize + ptrDiff(svmPtr, ptrToPatch), ptrToPatch, 0, svmAlloc, svmFlags, 0);
    }
    if (!kernelArguments[argIndex].isPatched) {
        patchedArgumentsNum++;
        kernelArguments[argIndex].isPatched = true;
    }
    addAllocationToCacheFlushVector(argIndex, svmAlloc);

    return CL_SUCCESS;
}

cl_int Kernel::setArgSvmAlloc(uint32_t argIndex, void *svmPtr, GraphicsAllocation *svmAlloc) {
    DBG_LOG_INPUTS("setArgBuffer svm_alloc", svmAlloc);

    auto rootDeviceIndex = getDevice().getRootDeviceIndex();
    auto &kernelInfo = getKernelInfo(rootDeviceIndex);
    const auto &kernelArgInfo = kernelInfo.kernelArgInfo[argIndex];

    storeKernelArg(argIndex, SVM_ALLOC_OBJ, svmAlloc, svmPtr, sizeof(uintptr_t));

    void *ptrToPatch = patchBufferOffset(kernelArgInfo, svmPtr, svmAlloc, rootDeviceIndex);

    auto patchLocation = ptrOffset(getCrossThreadData(rootDeviceIndex),
                                   kernelArgInfo.kernelArgPatchInfoVector[0].crossthreadOffset);

    auto patchSize = kernelArgInfo.kernelArgPatchInfoVector[0].size;

    patchWithRequiredSize(patchLocation, patchSize, reinterpret_cast<uintptr_t>(svmPtr));

    bool disableL3 = false;
    bool forceNonAuxMode = false;
    bool isAuxTranslationKernel = (AuxTranslationDirection::None != auxTranslationDirection);

    if (isAuxTranslationKernel) {
        if (((AuxTranslationDirection::AuxToNonAux == auxTranslationDirection) && argIndex == 1) ||
            ((AuxTranslationDirection::NonAuxToAux == auxTranslationDirection) && argIndex == 0)) {
            forceNonAuxMode = true;
        }
        disableL3 = (argIndex == 0);
    } else if (svmAlloc && svmAlloc->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED &&
               !kernelArgInfo.pureStatefulBufferAccess) {
        forceNonAuxMode = true;
    }

    if (requiresSshForBuffers(rootDeviceIndex)) {
        const auto &kernelArgInfo = kernelInfo.kernelArgInfo[argIndex];
        auto surfaceState = ptrOffset(getSurfaceStateHeap(rootDeviceIndex), kernelArgInfo.offsetHeap);
        size_t allocSize = 0;
        size_t offset = 0;
        if (svmAlloc != nullptr) {
            allocSize = svmAlloc->getUnderlyingBufferSize();
            offset = ptrDiff(ptrToPatch, svmAlloc->getGpuAddressToPatch());
            allocSize -= offset;
        }
        Buffer::setSurfaceState(&getDevice().getDevice(), surfaceState, forceNonAuxMode, disableL3, allocSize, ptrToPatch, offset, svmAlloc, 0, 0);
    }

    if (!kernelArguments[argIndex].isPatched) {
        patchedArgumentsNum++;
        kernelArguments[argIndex].isPatched = true;
    }

    addAllocationToCacheFlushVector(argIndex, svmAlloc);

    return CL_SUCCESS;
}

void Kernel::storeKernelArg(uint32_t argIndex, kernelArgType argType, void *argObject,
                            const void *argValue, size_t argSize,
                            GraphicsAllocation *argSvmAlloc, cl_mem_flags argSvmFlags) {
    kernelArguments[argIndex].type = argType;
    kernelArguments[argIndex].object = argObject;
    kernelArguments[argIndex].value = argValue;
    kernelArguments[argIndex].size = argSize;
    kernelArguments[argIndex].pSvmAlloc = argSvmAlloc;
    kernelArguments[argIndex].svmFlags = argSvmFlags;
}

const void *Kernel::getKernelArg(uint32_t argIndex) const {
    return kernelArguments[argIndex].object;
}

const Kernel::SimpleKernelArgInfo &Kernel::getKernelArgInfo(uint32_t argIndex) const {
    return kernelArguments[argIndex];
}

void Kernel::setSvmKernelExecInfo(GraphicsAllocation *argValue) {
    kernelSvmGfxAllocations.push_back(argValue);
    if (allocationForCacheFlush(argValue)) {
        svmAllocationsRequireCacheFlush = true;
    }
}

void Kernel::clearSvmKernelExecInfo() {
    kernelSvmGfxAllocations.clear();
    svmAllocationsRequireCacheFlush = false;
}

void Kernel::setUnifiedMemoryProperty(cl_kernel_exec_info infoType, bool infoValue) {
    if (infoType == CL_KERNEL_EXEC_INFO_INDIRECT_DEVICE_ACCESS_INTEL) {
        this->unifiedMemoryControls.indirectDeviceAllocationsAllowed = infoValue;
        return;
    }
    if (infoType == CL_KERNEL_EXEC_INFO_INDIRECT_HOST_ACCESS_INTEL) {
        this->unifiedMemoryControls.indirectHostAllocationsAllowed = infoValue;
        return;
    }
    if (infoType == CL_KERNEL_EXEC_INFO_INDIRECT_SHARED_ACCESS_INTEL) {
        this->unifiedMemoryControls.indirectSharedAllocationsAllowed = infoValue;
        return;
    }
}

void Kernel::setUnifiedMemoryExecInfo(GraphicsAllocation *unifiedMemoryAllocation) {
    kernelUnifiedMemoryGfxAllocations.push_back(unifiedMemoryAllocation);
}

void Kernel::clearUnifiedMemoryExecInfo() {
    kernelUnifiedMemoryGfxAllocations.clear();
}

cl_int Kernel::setKernelExecutionType(cl_execution_info_kernel_type_intel executionType) {
    switch (executionType) {
    case CL_KERNEL_EXEC_INFO_DEFAULT_TYPE_INTEL:
        this->executionType = KernelExecutionType::Default;
        break;
    case CL_KERNEL_EXEC_INFO_CONCURRENT_TYPE_INTEL:
        this->executionType = KernelExecutionType::Concurrent;
        break;
    default: {
        return CL_INVALID_VALUE;
    }
    }
    return CL_SUCCESS;
}

void Kernel::getSuggestedLocalWorkSize(const cl_uint workDim, const size_t *globalWorkSize, const size_t *globalWorkOffset,
                                       size_t *localWorkSize, ClDevice &clDevice) {
    UNRECOVERABLE_IF((workDim == 0) || (workDim > 3));
    UNRECOVERABLE_IF(globalWorkSize == nullptr);
    Vec3<size_t> elws{0, 0, 0};
    Vec3<size_t> gws{
        globalWorkSize[0],
        (workDim > 1) ? globalWorkSize[1] : 0,
        (workDim > 2) ? globalWorkSize[2] : 0};
    Vec3<size_t> offset{0, 0, 0};
    if (globalWorkOffset) {
        offset.x = globalWorkOffset[0];
        if (workDim > 1) {
            offset.y = globalWorkOffset[1];
            if (workDim > 2) {
                offset.z = globalWorkOffset[2];
            }
        }
    }

    const DispatchInfo dispatchInfo{&clDevice, this, workDim, gws, elws, offset};
    auto suggestedLws = computeWorkgroupSize(dispatchInfo);

    localWorkSize[0] = suggestedLws.x;
    if (workDim > 1)
        localWorkSize[1] = suggestedLws.y;
    if (workDim > 2)
        localWorkSize[2] = suggestedLws.z;
}

uint32_t Kernel::getMaxWorkGroupCount(const cl_uint workDim, const size_t *localWorkSize, const CommandQueue *commandQueue) const {
    auto rootDeviceIndex = commandQueue->getDevice().getRootDeviceIndex();
    auto &hardwareInfo = getHardwareInfo(rootDeviceIndex);
    auto &hwHelper = HwHelper::get(hardwareInfo.platform.eRenderCoreFamily);

    if (!hwHelper.isCooperativeDispatchSupported(commandQueue->getGpgpuEngine().getEngineType(), hardwareInfo.platform.eProductFamily)) {
        return 0;
    }

    const auto &kernelDescriptor = getKernelInfo(rootDeviceIndex).kernelDescriptor;
    auto dssCount = hardwareInfo.gtSystemInfo.DualSubSliceCount;
    if (dssCount == 0) {
        dssCount = hardwareInfo.gtSystemInfo.SubSliceCount;
    }
    auto availableThreadCount = hwHelper.calculateAvailableThreadCount(
        hardwareInfo.platform.eProductFamily,
        kernelDescriptor.kernelAttributes.numGrfRequired,
        hardwareInfo.gtSystemInfo.EUCount, hardwareInfo.gtSystemInfo.ThreadCount / hardwareInfo.gtSystemInfo.EUCount);

    auto barrierCount = kernelDescriptor.kernelAttributes.barrierCount;
    return KernelHelper::getMaxWorkGroupCount(kernelInfos[rootDeviceIndex]->getMaxSimdSize(),
                                              availableThreadCount,
                                              dssCount,
                                              dssCount * KB * hardwareInfo.capabilityTable.slmSize,
                                              hwHelper.alignSlmSize(kernelDeviceInfos[rootDeviceIndex].slmTotalSize),
                                              static_cast<uint32_t>(hwHelper.getMaxBarrierRegisterPerSlice()),
                                              hwHelper.getBarriersCountFromHasBarriers(barrierCount),
                                              workDim,
                                              localWorkSize);
}

inline void Kernel::makeArgsResident(CommandStreamReceiver &commandStreamReceiver) {
    auto numArgs = kernelInfos[commandStreamReceiver.getRootDeviceIndex()]->kernelArgInfo.size();
    for (decltype(numArgs) argIndex = 0; argIndex < numArgs; argIndex++) {
        if (kernelArguments[argIndex].object) {
            if (kernelArguments[argIndex].type == SVM_ALLOC_OBJ) {
                auto pSVMAlloc = (GraphicsAllocation *)kernelArguments[argIndex].object;
                auto pageFaultManager = executionEnvironment.memoryManager->getPageFaultManager();
                if (pageFaultManager &&
                    this->isUnifiedMemorySyncRequired) {
                    pageFaultManager->moveAllocationToGpuDomain(reinterpret_cast<void *>(pSVMAlloc->getGpuAddress()));
                }
                commandStreamReceiver.makeResident(*pSVMAlloc);
            } else if (Kernel::isMemObj(kernelArguments[argIndex].type)) {
                auto clMem = const_cast<cl_mem>(static_cast<const _cl_mem *>(kernelArguments[argIndex].object));
                auto memObj = castToObjectOrAbort<MemObj>(clMem);
                auto image = castToObject<Image>(clMem);
                if (image && image->isImageFromImage()) {
                    commandStreamReceiver.setSamplerCacheFlushRequired(CommandStreamReceiver::SamplerCacheFlushState::samplerCacheFlushBefore);
                }
                memObj->getMigrateableMultiGraphicsAllocation().ensureMemoryOnDevice(*executionEnvironment.memoryManager, commandStreamReceiver.getRootDeviceIndex());
                commandStreamReceiver.makeResident(*memObj->getGraphicsAllocation(commandStreamReceiver.getRootDeviceIndex()));
                if (memObj->getMcsAllocation()) {
                    commandStreamReceiver.makeResident(*memObj->getMcsAllocation());
                }
            }
        }
    }
}

void Kernel::performKernelTunning(CommandStreamReceiver &commandStreamReceiver, const Vec3<size_t> &lws, const Vec3<size_t> &gws, const Vec3<size_t> &offsets, TimestampPacketContainer *timestampContainer) {
    auto performTunning = TunningType::DISABLED;

    if (DebugManager.flags.EnableKernelTunning.get() != -1) {
        performTunning = static_cast<TunningType>(DebugManager.flags.EnableKernelTunning.get());
    }

    if (performTunning == TunningType::SIMPLE) {
        this->singleSubdevicePreferedInCurrentEnqueue = !this->getKernelInfo(commandStreamReceiver.getRootDeviceIndex()).kernelDescriptor.kernelAttributes.flags.useGlobalAtomics;

    } else if (performTunning == TunningType::FULL) {
        KernelConfig config{gws, lws, offsets};

        auto submissionDataIt = this->kernelSubmissionMap.find(config);
        if (submissionDataIt == this->kernelSubmissionMap.end()) {
            KernelSubmissionData submissionData;
            submissionData.kernelStandardTimestamps = std::make_unique<TimestampPacketContainer>();
            submissionData.kernelSubdeviceTimestamps = std::make_unique<TimestampPacketContainer>();
            submissionData.status = TunningStatus::STANDARD_TUNNING_IN_PROGRESS;
            submissionData.kernelStandardTimestamps->assignAndIncrementNodesRefCounts(*timestampContainer);
            this->kernelSubmissionMap[config] = std::move(submissionData);
            this->singleSubdevicePreferedInCurrentEnqueue = false;
            return;
        }

        auto &submissionData = submissionDataIt->second;

        if (submissionData.status == TunningStatus::TUNNING_DONE) {
            this->singleSubdevicePreferedInCurrentEnqueue = submissionData.singleSubdevicePrefered;
        }

        if (submissionData.status == TunningStatus::SUBDEVICE_TUNNING_IN_PROGRESS) {
            if (this->hasTunningFinished(submissionData)) {
                submissionData.status = TunningStatus::TUNNING_DONE;
                submissionData.kernelStandardTimestamps.reset();
                submissionData.kernelSubdeviceTimestamps.reset();
                this->singleSubdevicePreferedInCurrentEnqueue = submissionData.singleSubdevicePrefered;
            } else {
                this->singleSubdevicePreferedInCurrentEnqueue = false;
            }
        }

        if (submissionData.status == TunningStatus::STANDARD_TUNNING_IN_PROGRESS) {
            submissionData.status = TunningStatus::SUBDEVICE_TUNNING_IN_PROGRESS;
            submissionData.kernelSubdeviceTimestamps->assignAndIncrementNodesRefCounts(*timestampContainer);
            this->singleSubdevicePreferedInCurrentEnqueue = true;
        }
    }
}

bool Kernel::hasTunningFinished(KernelSubmissionData &submissionData) {
    if (!this->hasRunFinished(submissionData.kernelStandardTimestamps.get()) ||
        !this->hasRunFinished(submissionData.kernelSubdeviceTimestamps.get())) {
        return false;
    }

    uint64_t globalStartTS = 0u;
    uint64_t globalEndTS = 0u;

    Event::getBoundaryTimestampValues(submissionData.kernelStandardTimestamps.get(), globalStartTS, globalEndTS);
    auto standardTSDiff = globalEndTS - globalStartTS;

    Event::getBoundaryTimestampValues(submissionData.kernelSubdeviceTimestamps.get(), globalStartTS, globalEndTS);
    auto subdeviceTSDiff = globalEndTS - globalStartTS;

    submissionData.singleSubdevicePrefered = standardTSDiff > subdeviceTSDiff;

    return true;
}

bool Kernel::hasRunFinished(TimestampPacketContainer *timestampContainer) {
    for (const auto &node : timestampContainer->peekNodes()) {
        if (!node->tagForCpuAccess->isCompleted()) {
            return false;
        }
    }
    return true;
}

bool Kernel::isSingleSubdevicePreferred() const {
    return this->singleSubdevicePreferedInCurrentEnqueue;
}

void Kernel::makeResident(CommandStreamReceiver &commandStreamReceiver) {
    auto rootDeviceIndex = commandStreamReceiver.getRootDeviceIndex();
    if (kernelDeviceInfos[rootDeviceIndex].privateSurface) {
        commandStreamReceiver.makeResident(*kernelDeviceInfos[rootDeviceIndex].privateSurface);
    }

    if (program->getConstantSurface(rootDeviceIndex)) {
        commandStreamReceiver.makeResident(*(program->getConstantSurface(rootDeviceIndex)));
    }

    if (program->getGlobalSurface(rootDeviceIndex)) {
        commandStreamReceiver.makeResident(*(program->getGlobalSurface(rootDeviceIndex)));
    }

    if (program->getExportedFunctionsSurface(rootDeviceIndex)) {
        commandStreamReceiver.makeResident(*(program->getExportedFunctionsSurface(rootDeviceIndex)));
    }

    for (auto gfxAlloc : kernelSvmGfxAllocations) {
        commandStreamReceiver.makeResident(*gfxAlloc);
    }

    auto pageFaultManager = program->peekExecutionEnvironment().memoryManager->getPageFaultManager();

    for (auto gfxAlloc : kernelUnifiedMemoryGfxAllocations) {
        commandStreamReceiver.makeResident(*gfxAlloc);
        if (pageFaultManager) {
            pageFaultManager->moveAllocationToGpuDomain(reinterpret_cast<void *>(gfxAlloc->getGpuAddress()));
        }
    }

    if (unifiedMemoryControls.indirectSharedAllocationsAllowed && pageFaultManager) {
        pageFaultManager->moveAllocationsWithinUMAllocsManagerToGpuDomain(this->getContext().getSVMAllocsManager());
    }
    makeArgsResident(commandStreamReceiver);

    auto kernelIsaAllocation = this->kernelInfos[rootDeviceIndex]->kernelAllocation;
    if (kernelIsaAllocation) {
        commandStreamReceiver.makeResident(*kernelIsaAllocation);
    }

    gtpinNotifyMakeResident(this, &commandStreamReceiver);

    if (unifiedMemoryControls.indirectDeviceAllocationsAllowed ||
        unifiedMemoryControls.indirectHostAllocationsAllowed ||
        unifiedMemoryControls.indirectSharedAllocationsAllowed) {
        this->getContext().getSVMAllocsManager()->makeInternalAllocationsResident(commandStreamReceiver, unifiedMemoryControls.generateMask());
    }
}

void Kernel::getResidency(std::vector<Surface *> &dst, uint32_t rootDeviceIndex) {
    if (kernelDeviceInfos[rootDeviceIndex].privateSurface) {
        GeneralSurface *surface = new GeneralSurface(kernelDeviceInfos[rootDeviceIndex].privateSurface);
        dst.push_back(surface);
    }

    if (program->getConstantSurface(rootDeviceIndex)) {
        GeneralSurface *surface = new GeneralSurface(program->getConstantSurface(rootDeviceIndex));
        dst.push_back(surface);
    }

    if (program->getGlobalSurface(rootDeviceIndex)) {
        GeneralSurface *surface = new GeneralSurface(program->getGlobalSurface(rootDeviceIndex));
        dst.push_back(surface);
    }

    if (program->getExportedFunctionsSurface(rootDeviceIndex)) {
        GeneralSurface *surface = new GeneralSurface(program->getExportedFunctionsSurface(rootDeviceIndex));
        dst.push_back(surface);
    }

    for (auto gfxAlloc : kernelSvmGfxAllocations) {
        GeneralSurface *surface = new GeneralSurface(gfxAlloc);
        dst.push_back(surface);
    }

    auto numArgs = kernelInfos[rootDeviceIndex]->kernelArgInfo.size();
    for (decltype(numArgs) argIndex = 0; argIndex < numArgs; argIndex++) {
        if (kernelArguments[argIndex].object) {
            if (kernelArguments[argIndex].type == SVM_ALLOC_OBJ) {
                auto pSVMAlloc = (GraphicsAllocation *)kernelArguments[argIndex].object;
                dst.push_back(new GeneralSurface(pSVMAlloc));
            } else if (Kernel::isMemObj(kernelArguments[argIndex].type)) {
                auto clMem = const_cast<cl_mem>(static_cast<const _cl_mem *>(kernelArguments[argIndex].object));
                auto memObj = castToObject<MemObj>(clMem);
                DEBUG_BREAK_IF(memObj == nullptr);
                dst.push_back(new MemObjSurface(memObj));
            }
        }
    }

    auto kernelIsaAllocation = this->kernelInfos[rootDeviceIndex]->kernelAllocation;
    if (kernelIsaAllocation) {
        GeneralSurface *surface = new GeneralSurface(kernelIsaAllocation);
        dst.push_back(surface);
    }

    gtpinNotifyUpdateResidencyList(this, &dst);
}

bool Kernel::requiresCoherency() {
    auto numArgs = getDefaultKernelInfo().kernelArgInfo.size();
    for (decltype(numArgs) argIndex = 0; argIndex < numArgs; argIndex++) {
        if (kernelArguments[argIndex].object) {
            if (kernelArguments[argIndex].type == SVM_ALLOC_OBJ) {
                auto pSVMAlloc = (GraphicsAllocation *)kernelArguments[argIndex].object;
                if (pSVMAlloc->isCoherent()) {
                    return true;
                }
            }

            if (Kernel::isMemObj(kernelArguments[argIndex].type)) {
                auto clMem = const_cast<cl_mem>(static_cast<const _cl_mem *>(kernelArguments[argIndex].object));
                auto memObj = castToObjectOrAbort<MemObj>(clMem);
                if (memObj->getMultiGraphicsAllocation().isCoherent()) {
                    return true;
                }
            }
        }
    }
    return false;
}

cl_int Kernel::setArgLocal(uint32_t argIndexIn,
                           size_t argSize,
                           const void *argVal) {
    std::bitset<64> isArgSet{};
    storeKernelArg(argIndexIn, SLM_OBJ, nullptr, argVal, argSize);

    for (auto &pClDevice : getDevices()) {
        auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
        if (isArgSet.test(rootDeviceIndex)) {
            continue;
        }
        auto crossThreadData = reinterpret_cast<uint32_t *>(getCrossThreadData(rootDeviceIndex));
        auto &kernelInfo = *kernelInfos[rootDeviceIndex];
        auto &kernelDeviceInfo = kernelDeviceInfos[rootDeviceIndex];

        uint32_t argIndex = argIndexIn;

        kernelDeviceInfo.slmSizes[argIndex] = argSize;

        // Extract our current slmOffset
        auto slmOffset = *ptrOffset(crossThreadData,
                                    kernelInfo.kernelArgInfo[argIndex].kernelArgPatchInfoVector[0].crossthreadOffset);

        // Add our size
        slmOffset += static_cast<uint32_t>(argSize);

        // Update all slm offsets after this argIndex
        ++argIndex;
        while (argIndex < kernelDeviceInfo.slmSizes.size()) {
            const auto &kernelArgInfo = kernelInfo.kernelArgInfo[argIndex];
            auto slmAlignment = kernelArgInfo.slmAlignment;

            // If an local argument, alignment should be non-zero
            if (slmAlignment) {
                // Align to specified alignment
                slmOffset = alignUp(slmOffset, slmAlignment);

                // Patch our new offset into cross thread data
                auto patchLocation = ptrOffset(crossThreadData,
                                               kernelArgInfo.kernelArgPatchInfoVector[0].crossthreadOffset);
                *patchLocation = slmOffset;
            }

            slmOffset += static_cast<uint32_t>(kernelDeviceInfo.slmSizes[argIndex]);
            ++argIndex;
        }

        kernelDeviceInfo.slmTotalSize = kernelInfo.workloadInfo.slmStaticSize + alignUp(slmOffset, KB);
        isArgSet.set(rootDeviceIndex);
    }

    return CL_SUCCESS;
}

cl_int Kernel::setArgBuffer(uint32_t argIndex,
                            size_t argSize,
                            const void *argVal) {

    if (argSize != sizeof(cl_mem *)) {
        return CL_INVALID_ARG_SIZE;
    }

    std::bitset<64> isArgSet{};

    auto clMem = reinterpret_cast<const cl_mem *>(argVal);

    if (clMem && *clMem) {
        auto clMemObj = *clMem;
        DBG_LOG_INPUTS("setArgBuffer cl_mem", clMemObj);

        storeKernelArg(argIndex, BUFFER_OBJ, clMemObj, argVal, argSize);

        auto buffer = castToObject<Buffer>(clMemObj);
        if (!buffer)
            return CL_INVALID_MEM_OBJECT;

        if (buffer->peekSharingHandler()) {
            usingSharedObjArgs = true;
        }
        for (auto &pClDevice : getDevices()) {
            auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
            if (isArgSet.test(rootDeviceIndex)) {
                continue;
            }
            const auto &kernelArgInfo = getKernelInfo(rootDeviceIndex).kernelArgInfo[argIndex];
            patchBufferOffset(kernelArgInfo, nullptr, nullptr, rootDeviceIndex);
            auto graphicsAllocation = buffer->getGraphicsAllocation(rootDeviceIndex);

            auto patchLocation = ptrOffset(getCrossThreadData(rootDeviceIndex),
                                           kernelArgInfo.kernelArgPatchInfoVector[0].crossthreadOffset);

            auto patchSize = kernelArgInfo.kernelArgPatchInfoVector[0].size;

            uint64_t addressToPatch = buffer->setArgStateless(patchLocation, patchSize, rootDeviceIndex, !this->isBuiltIn);

            if (DebugManager.flags.AddPatchInfoCommentsForAUBDump.get()) {
                PatchInfoData patchInfoData(addressToPatch - buffer->getOffset(), static_cast<uint64_t>(buffer->getOffset()),
                                            PatchInfoAllocationType::KernelArg, reinterpret_cast<uint64_t>(getCrossThreadData(rootDeviceIndex)),
                                            static_cast<uint64_t>(kernelArgInfo.kernelArgPatchInfoVector[0].crossthreadOffset),
                                            PatchInfoAllocationType::IndirectObjectHeap, patchSize);
                this->patchInfoDataList.push_back(patchInfoData);
            }

            bool disableL3 = false;
            bool forceNonAuxMode = false;
            bool isAuxTranslationKernel = (AuxTranslationDirection::None != auxTranslationDirection);

            if (isAuxTranslationKernel) {
                if (((AuxTranslationDirection::AuxToNonAux == auxTranslationDirection) && argIndex == 1) ||
                    ((AuxTranslationDirection::NonAuxToAux == auxTranslationDirection) && argIndex == 0)) {
                    forceNonAuxMode = true;
                }
                disableL3 = (argIndex == 0);
            } else if (graphicsAllocation->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED &&
                       !kernelArgInfo.pureStatefulBufferAccess) {
                forceNonAuxMode = true;
            }

            if (requiresSshForBuffers(rootDeviceIndex)) {
                auto surfaceState = ptrOffset(getSurfaceStateHeap(rootDeviceIndex), kernelArgInfo.offsetHeap);
                auto context = program->getContextPtr();
                size_t numDevicesInContext = context ? context->getNumDevices() : 1u;
                buffer->setArgStateful(surfaceState, forceNonAuxMode, disableL3, isAuxTranslationKernel, kernelArgInfo.isReadOnly, pClDevice->getDevice(),
                                       getDefaultKernelInfo().kernelDescriptor.kernelAttributes.flags.useGlobalAtomics, numDevicesInContext);
            }

            kernelArguments[argIndex].isStatelessUncacheable = kernelArgInfo.pureStatefulBufferAccess ? false : buffer->isMemObjUncacheable();

            auto allocationForCacheFlush = graphicsAllocation;

            //if we make object uncacheable for surface state and there are not stateless accessess , then ther is no need to flush caches
            if (buffer->isMemObjUncacheableForSurfaceState() && kernelArgInfo.pureStatefulBufferAccess) {
                allocationForCacheFlush = nullptr;
            }

            addAllocationToCacheFlushVector(argIndex, allocationForCacheFlush);
            isArgSet.set(rootDeviceIndex);
        }
        return CL_SUCCESS;
    } else {
        storeKernelArg(argIndex, BUFFER_OBJ, nullptr, argVal, argSize);
        for (auto &pClDevice : getDevices()) {
            auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
            if (isArgSet.test(rootDeviceIndex)) {
                continue;
            }
            const auto &kernelArgInfo = getKernelInfo(rootDeviceIndex).kernelArgInfo[argIndex];
            patchBufferOffset(kernelArgInfo, nullptr, nullptr, rootDeviceIndex);
            auto patchLocation = ptrOffset(getCrossThreadData(rootDeviceIndex),
                                           kernelArgInfo.kernelArgPatchInfoVector[0].crossthreadOffset);

            patchWithRequiredSize(patchLocation, kernelArgInfo.kernelArgPatchInfoVector[0].size, 0u);

            if (requiresSshForBuffers(rootDeviceIndex)) {
                auto surfaceState = ptrOffset(getSurfaceStateHeap(rootDeviceIndex), kernelArgInfo.offsetHeap);
                Buffer::setSurfaceState(&pClDevice->getDevice(), surfaceState, false, false, 0, nullptr, 0, nullptr, 0, 0);
            }
            isArgSet.set(rootDeviceIndex);
        }

        return CL_SUCCESS;
    }
}

cl_int Kernel::setArgPipe(uint32_t argIndex,
                          size_t argSize,
                          const void *argVal) {

    if (argSize != sizeof(cl_mem *)) {
        return CL_INVALID_ARG_SIZE;
    }

    auto clMem = reinterpret_cast<const cl_mem *>(argVal);

    if (clMem && *clMem) {
        auto clMemObj = *clMem;
        DBG_LOG_INPUTS("setArgPipe cl_mem", clMemObj);

        storeKernelArg(argIndex, PIPE_OBJ, clMemObj, argVal, argSize);

        auto memObj = castToObject<MemObj>(clMemObj);
        if (!memObj) {
            return CL_INVALID_MEM_OBJECT;
        }

        auto pipe = castToObject<Pipe>(clMemObj);
        if (!pipe) {
            return CL_INVALID_ARG_VALUE;
        }

        if (memObj->getContext() != &(this->getContext())) {
            return CL_INVALID_MEM_OBJECT;
        }
        auto rootDeviceIndex = getDevice().getRootDeviceIndex();
        const auto &kernelArgInfo = getKernelInfo(rootDeviceIndex).kernelArgInfo[argIndex];
        auto patchLocation = ptrOffset(getCrossThreadData(rootDeviceIndex),
                                       kernelArgInfo.kernelArgPatchInfoVector[0].crossthreadOffset);

        auto patchSize = kernelArgInfo.kernelArgPatchInfoVector[0].size;

        pipe->setPipeArg(patchLocation, patchSize, rootDeviceIndex);

        auto graphicsAllocation = pipe->getGraphicsAllocation(rootDeviceIndex);

        if (requiresSshForBuffers(rootDeviceIndex)) {
            auto surfaceState = ptrOffset(getSurfaceStateHeap(rootDeviceIndex), kernelArgInfo.offsetHeap);
            Buffer::setSurfaceState(&getDevice().getDevice(), surfaceState, false, false,
                                    pipe->getSize(), pipe->getCpuAddress(), 0,
                                    graphicsAllocation, 0, 0);
        }

        return CL_SUCCESS;
    } else {
        return CL_INVALID_MEM_OBJECT;
    }
}

cl_int Kernel::setArgImage(uint32_t argIndex,
                           size_t argSize,
                           const void *argVal) {
    return setArgImageWithMipLevel(argIndex, argSize, argVal, 0u);
}

cl_int Kernel::setArgImageWithMipLevel(uint32_t argIndex,
                                       size_t argSize,
                                       const void *argVal, uint32_t mipLevel) {
    auto retVal = CL_INVALID_ARG_VALUE;
    auto rootDeviceIndex = getDevice().getRootDeviceIndex();
    auto &kernelInfo = getKernelInfo(rootDeviceIndex);
    patchBufferOffset(kernelInfo.kernelArgInfo[argIndex], nullptr, nullptr, rootDeviceIndex);

    auto clMemObj = *(static_cast<const cl_mem *>(argVal));
    auto pImage = castToObject<Image>(clMemObj);

    if (pImage && argSize == sizeof(cl_mem *)) {
        if (pImage->peekSharingHandler()) {
            usingSharedObjArgs = true;
        }
        const auto &kernelArgInfo = kernelInfo.kernelArgInfo[argIndex];

        DBG_LOG_INPUTS("setArgImage cl_mem", clMemObj);

        storeKernelArg(argIndex, IMAGE_OBJ, clMemObj, argVal, argSize);

        auto surfaceState = ptrOffset(getSurfaceStateHeap(rootDeviceIndex), kernelArgInfo.offsetHeap);
        DEBUG_BREAK_IF(!kernelArgInfo.isImage);

        // Sets SS structure
        if (kernelArgInfo.isMediaImage) {
            DEBUG_BREAK_IF(!kernelInfo.isVmeWorkload);
            pImage->setMediaImageArg(surfaceState, rootDeviceIndex);
        } else {
            pImage->setImageArg(surfaceState, kernelArgInfo.isMediaBlockImage, mipLevel, rootDeviceIndex);
        }

        auto crossThreadData = reinterpret_cast<uint32_t *>(getCrossThreadData(rootDeviceIndex));
        auto &imageDesc = pImage->getImageDesc();
        auto &imageFormat = pImage->getImageFormat();
        auto graphicsAllocation = pImage->getGraphicsAllocation(rootDeviceIndex);

        if (imageDesc.image_type == CL_MEM_OBJECT_IMAGE3D) {
            imageTransformer->registerImage3d(argIndex);
        }

        patch<uint32_t, size_t>(imageDesc.image_width, crossThreadData, kernelArgInfo.offsetImgWidth);
        patch<uint32_t, size_t>(imageDesc.image_height, crossThreadData, kernelArgInfo.offsetImgHeight);
        patch<uint32_t, size_t>(imageDesc.image_depth, crossThreadData, kernelArgInfo.offsetImgDepth);
        patch<uint32_t, cl_uint>(imageDesc.num_samples, crossThreadData, kernelArgInfo.offsetNumSamples);
        patch<uint32_t, size_t>(imageDesc.image_array_size, crossThreadData, kernelArgInfo.offsetArraySize);
        patch<uint32_t, cl_channel_type>(imageFormat.image_channel_data_type, crossThreadData, kernelArgInfo.offsetChannelDataType);
        patch<uint32_t, cl_channel_order>(imageFormat.image_channel_order, crossThreadData, kernelArgInfo.offsetChannelOrder);
        patch<uint32_t, uint32_t>(kernelArgInfo.offsetHeap, crossThreadData, kernelArgInfo.offsetObjectId);
        patch<uint32_t, cl_uint>(imageDesc.num_mip_levels, crossThreadData, kernelArgInfo.offsetNumMipLevels);

        auto pixelSize = pImage->getSurfaceFormatInfo().surfaceFormat.ImageElementSizeInBytes;
        patch<uint64_t, uint64_t>(graphicsAllocation->getGpuAddress(), crossThreadData, kernelArgInfo.offsetFlatBaseOffset);
        patch<uint32_t, size_t>((imageDesc.image_width * pixelSize) - 1, crossThreadData, kernelArgInfo.offsetFlatWidth);
        patch<uint32_t, size_t>((imageDesc.image_height * pixelSize) - 1, crossThreadData, kernelArgInfo.offsetFlatHeight);
        patch<uint32_t, size_t>(imageDesc.image_row_pitch - 1, crossThreadData, kernelArgInfo.offsetFlatPitch);

        addAllocationToCacheFlushVector(argIndex, graphicsAllocation);
        retVal = CL_SUCCESS;
    }

    return retVal;
}

cl_int Kernel::setArgImmediate(uint32_t argIndex,
                               size_t argSize,
                               const void *argVal) {

    auto retVal = CL_INVALID_ARG_VALUE;

    if (argVal) {
        storeKernelArg(argIndex, NONE_OBJ, nullptr, nullptr, argSize);
        std::bitset<64> isArgSet{};
        for (auto &pClDevice : getDevices()) {
            auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
            if (isArgSet.test(rootDeviceIndex)) {
                continue;
            }
            const auto &kernelArgInfo = getKernelInfo(rootDeviceIndex).kernelArgInfo[argIndex];
            DEBUG_BREAK_IF(kernelArgInfo.kernelArgPatchInfoVector.size() <= 0);

            auto crossThreadData = getCrossThreadData(rootDeviceIndex);
            auto crossThreadDataEnd = ptrOffset(crossThreadData, getCrossThreadDataSize(rootDeviceIndex));

            for (const auto &kernelArgPatchInfo : kernelArgInfo.kernelArgPatchInfoVector) {
                DEBUG_BREAK_IF(kernelArgPatchInfo.size <= 0);
                auto pDst = ptrOffset(crossThreadData, kernelArgPatchInfo.crossthreadOffset);

                auto pSrc = ptrOffset(argVal, kernelArgPatchInfo.sourceOffset);

                DEBUG_BREAK_IF(!(ptrOffset(pDst, kernelArgPatchInfo.size) <= crossThreadDataEnd));
                UNUSED_VARIABLE(crossThreadDataEnd);

                if (kernelArgPatchInfo.sourceOffset < argSize) {
                    size_t maxBytesToCopy = argSize - kernelArgPatchInfo.sourceOffset;
                    size_t bytesToCopy = std::min(static_cast<size_t>(kernelArgPatchInfo.size), maxBytesToCopy);
                    memcpy_s(pDst, kernelArgPatchInfo.size, pSrc, bytesToCopy);
                }
            }
            isArgSet.set(rootDeviceIndex);
        }
        retVal = CL_SUCCESS;
    }

    return retVal;
}

cl_int Kernel::setArgSampler(uint32_t argIndex,
                             size_t argSize,
                             const void *argVal) {
    auto retVal = CL_INVALID_SAMPLER;

    if (!argVal) {
        return retVal;
    }

    auto clSamplerObj = *(static_cast<const cl_sampler *>(argVal));
    auto pSampler = castToObject<Sampler>(clSamplerObj);
    auto rootDeviceIndex = getDevice().getRootDeviceIndex();

    if (pSampler) {
        pSampler->incRefInternal();
    }

    if (kernelArguments.at(argIndex).object) {
        auto oldSampler = castToObject<Sampler>(kernelArguments.at(argIndex).object);
        UNRECOVERABLE_IF(!oldSampler);
        oldSampler->decRefInternal();
    }

    if (pSampler && argSize == sizeof(cl_sampler *)) {
        const auto &kernelArgInfo = getKernelInfo(rootDeviceIndex).kernelArgInfo[argIndex];

        storeKernelArg(argIndex, SAMPLER_OBJ, clSamplerObj, argVal, argSize);

        auto dsh = getDynamicStateHeap(rootDeviceIndex);
        auto samplerState = ptrOffset(dsh, kernelArgInfo.offsetHeap);

        pSampler->setArg(const_cast<void *>(samplerState), getProgram()->getDevices()[0]->getHardwareInfo());

        auto crossThreadData = reinterpret_cast<uint32_t *>(getCrossThreadData(rootDeviceIndex));
        patch<uint32_t, unsigned int>(pSampler->getSnapWaValue(), crossThreadData, kernelArgInfo.offsetSamplerSnapWa);
        patch<uint32_t, uint32_t>(GetAddrModeEnum(pSampler->addressingMode), crossThreadData, kernelArgInfo.offsetSamplerAddressingMode);
        patch<uint32_t, uint32_t>(GetNormCoordsEnum(pSampler->normalizedCoordinates), crossThreadData, kernelArgInfo.offsetSamplerNormalizedCoords);
        patch<uint32_t, uint32_t>(SAMPLER_OBJECT_ID_SHIFT + kernelArgInfo.offsetHeap, crossThreadData, kernelArgInfo.offsetObjectId);

        retVal = CL_SUCCESS;
    }

    return retVal;
}

cl_int Kernel::setArgAccelerator(uint32_t argIndex,
                                 size_t argSize,
                                 const void *argVal) {
    auto retVal = CL_INVALID_ARG_VALUE;

    if (argSize != sizeof(cl_accelerator_intel)) {
        return CL_INVALID_ARG_SIZE;
    }

    if (!argVal) {
        return retVal;
    }

    auto clAcceleratorObj = *(static_cast<const cl_accelerator_intel *>(argVal));
    DBG_LOG_INPUTS("setArgAccelerator cl_mem", clAcceleratorObj);

    const auto pAccelerator = castToObject<IntelAccelerator>(clAcceleratorObj);
    auto rootDeviceIndex = getDevice().getRootDeviceIndex();

    if (pAccelerator) {
        storeKernelArg(argIndex, ACCELERATOR_OBJ, clAcceleratorObj, argVal, argSize);

        const auto &kernelArgInfo = getKernelInfo(rootDeviceIndex).kernelArgInfo[argIndex];

        if (kernelArgInfo.samplerArgumentType == iOpenCL::SAMPLER_OBJECT_VME) {
            auto crossThreadData = getCrossThreadData(rootDeviceIndex);

            const auto pVmeAccelerator = castToObjectOrAbort<VmeAccelerator>(pAccelerator);
            auto pDesc = static_cast<const cl_motion_estimation_desc_intel *>(pVmeAccelerator->getDescriptor());
            DEBUG_BREAK_IF(!pDesc);

            auto pVmeMbBlockTypeDst = reinterpret_cast<cl_uint *>(ptrOffset(crossThreadData, kernelArgInfo.offsetVmeMbBlockType));
            *pVmeMbBlockTypeDst = pDesc->mb_block_type;

            auto pVmeSubpixelMode = reinterpret_cast<cl_uint *>(ptrOffset(crossThreadData, kernelArgInfo.offsetVmeSubpixelMode));
            *pVmeSubpixelMode = pDesc->subpixel_mode;

            auto pVmeSadAdjustMode = reinterpret_cast<cl_uint *>(ptrOffset(crossThreadData, kernelArgInfo.offsetVmeSadAdjustMode));
            *pVmeSadAdjustMode = pDesc->sad_adjust_mode;

            auto pVmeSearchPathType = reinterpret_cast<cl_uint *>(ptrOffset(crossThreadData, kernelArgInfo.offsetVmeSearchPathType));
            *pVmeSearchPathType = pDesc->search_path_type;

            retVal = CL_SUCCESS;
        } else if (kernelArgInfo.samplerArgumentType == iOpenCL::SAMPLER_OBJECT_VE) {
            retVal = CL_SUCCESS;
        }
    }

    return retVal;
}

cl_int Kernel::setArgDevQueue(uint32_t argIndex,
                              size_t argSize,
                              const void *argVal) {
    if (argVal == nullptr) {
        return CL_INVALID_ARG_VALUE;
    }

    if (argSize != sizeof(cl_command_queue)) {
        return CL_INVALID_ARG_SIZE;
    }

    auto clDeviceQueue = *(static_cast<const device_queue *>(argVal));
    auto pDeviceQueue = castToObject<DeviceQueue>(clDeviceQueue);

    if (pDeviceQueue == nullptr) {
        return CL_INVALID_DEVICE_QUEUE;
    }
    auto rootDeviceIndex = pDeviceQueue->getDevice().getRootDeviceIndex();

    storeKernelArg(argIndex, DEVICE_QUEUE_OBJ, clDeviceQueue, argVal, argSize);

    const auto &kernelArgPatchInfo = kernelInfos[rootDeviceIndex]->kernelArgInfo[argIndex].kernelArgPatchInfoVector[0];
    auto patchLocation = ptrOffset(reinterpret_cast<uint32_t *>(getCrossThreadData(rootDeviceIndex)),
                                   kernelArgPatchInfo.crossthreadOffset);

    patchWithRequiredSize(patchLocation, kernelArgPatchInfo.size,
                          static_cast<uintptr_t>(pDeviceQueue->getQueueBuffer()->getGpuAddressToPatch()));

    return CL_SUCCESS;
}

void Kernel::setKernelArgHandler(uint32_t argIndex, KernelArgHandler handler) {
    if (kernelArgHandlers.size() <= argIndex) {
        kernelArgHandlers.resize(argIndex + 1);
    }

    kernelArgHandlers[argIndex] = handler;
}

void Kernel::unsetArg(uint32_t argIndex) {
    if (kernelArguments[argIndex].isPatched) {
        patchedArgumentsNum--;
        kernelArguments[argIndex].isPatched = false;
        if (kernelArguments[argIndex].isStatelessUncacheable) {
            statelessUncacheableArgsCount--;
            kernelArguments[argIndex].isStatelessUncacheable = false;
        }
    }
}

void Kernel::createReflectionSurface() {
    auto pClDevice = program->getDevices()[0];
    auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
    if (this->isParentKernel && kernelReflectionSurface == nullptr) {
        auto &hwInfo = pClDevice->getHardwareInfo();
        auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);
        BlockKernelManager *blockManager = program->getBlockKernelManager();
        uint32_t blockCount = static_cast<uint32_t>(blockManager->getCount());

        ObjectCounts objectCount;
        getParentObjectCounts(objectCount);
        uint32_t parentImageCount = objectCount.imageCount;
        uint32_t parentSamplerCount = objectCount.samplerCount;
        size_t maxConstantBufferSize = 0;

        std::vector<IGIL_KernelCurbeParams> *curbeParamsForBlocks = new std::vector<IGIL_KernelCurbeParams>[blockCount];

        uint64_t *tokenMask = new uint64_t[blockCount];
        uint32_t *sshTokenOffsetsFromKernelData = new uint32_t[blockCount];

        size_t kernelReflectionSize = alignUp(sizeof(IGIL_KernelDataHeader) + blockCount * sizeof(IGIL_KernelAddressData), sizeof(void *));
        uint32_t kernelDataOffset = static_cast<uint32_t>(kernelReflectionSize);
        uint32_t parentSSHAlignedSize = alignUp(this->kernelInfos[rootDeviceIndex]->heapInfo.SurfaceStateHeapSize, hwHelper.getBindingTableStateAlignement());
        uint32_t btOffset = parentSSHAlignedSize;

        for (uint32_t i = 0; i < blockCount; i++) {
            const KernelInfo *pBlockInfo = blockManager->getBlockKernelInfo(i);
            size_t samplerStateAndBorderColorSize = 0;

            uint32_t firstSSHTokenIndex = 0;

            ReflectionSurfaceHelper::getCurbeParams(curbeParamsForBlocks[i], tokenMask[i], firstSSHTokenIndex, *pBlockInfo, hwInfo);

            maxConstantBufferSize = std::max(maxConstantBufferSize, static_cast<size_t>(pBlockInfo->patchInfo.dataParameterStream->DataParameterStreamSize));

            samplerStateAndBorderColorSize = pBlockInfo->getSamplerStateArraySize(hwInfo);
            samplerStateAndBorderColorSize = alignUp(samplerStateAndBorderColorSize, Sampler::samplerStateArrayAlignment);
            samplerStateAndBorderColorSize += pBlockInfo->getBorderColorStateSize();
            samplerStateAndBorderColorSize = alignUp(samplerStateAndBorderColorSize, sizeof(void *));

            sshTokenOffsetsFromKernelData[i] = offsetof(IGIL_KernelData, m_data) + sizeof(IGIL_KernelCurbeParams) * firstSSHTokenIndex;

            kernelReflectionSize += alignUp(sizeof(IGIL_KernelData) + sizeof(IGIL_KernelCurbeParams) * curbeParamsForBlocks[i].size(), sizeof(void *));
            kernelReflectionSize += parentSamplerCount * sizeof(IGIL_SamplerParams) + samplerStateAndBorderColorSize;
        }

        maxConstantBufferSize = alignUp(maxConstantBufferSize, sizeof(void *));
        kernelReflectionSize += blockCount * alignUp(maxConstantBufferSize, sizeof(void *));
        kernelReflectionSize += parentImageCount * sizeof(IGIL_ImageParamters);
        kernelReflectionSize += parentSamplerCount * sizeof(IGIL_ParentSamplerParams);
        kernelReflectionSurface = executionEnvironment.memoryManager->allocateGraphicsMemoryWithProperties(
            {pClDevice->getRootDeviceIndex(), kernelReflectionSize,
             GraphicsAllocation::AllocationType::DEVICE_QUEUE_BUFFER,
             pClDevice->getDeviceBitfield()});

        for (uint32_t i = 0; i < blockCount; i++) {
            const KernelInfo *pBlockInfo = blockManager->getBlockKernelInfo(i);
            uint32_t newKernelDataOffset = ReflectionSurfaceHelper::setKernelData(kernelReflectionSurface->getUnderlyingBuffer(),
                                                                                  kernelDataOffset,
                                                                                  curbeParamsForBlocks[i],
                                                                                  tokenMask[i],
                                                                                  maxConstantBufferSize,
                                                                                  parentSamplerCount,
                                                                                  *pBlockInfo,
                                                                                  hwInfo);

            uint32_t offset = static_cast<uint32_t>(offsetof(IGIL_KernelDataHeader, m_data) + sizeof(IGIL_KernelAddressData) * i);

            uint32_t samplerHeapOffset = static_cast<uint32_t>(alignUp(kernelDataOffset + sizeof(IGIL_KernelData) + curbeParamsForBlocks[i].size() * sizeof(IGIL_KernelCurbeParams), sizeof(void *)));
            uint32_t samplerHeapSize = static_cast<uint32_t>(alignUp(pBlockInfo->getSamplerStateArraySize(hwInfo), Sampler::samplerStateArrayAlignment) + pBlockInfo->getBorderColorStateSize());
            uint32_t constantBufferOffset = alignUp(samplerHeapOffset + samplerHeapSize, sizeof(void *));

            uint32_t samplerParamsOffset = 0;
            if (parentSamplerCount) {
                samplerParamsOffset = newKernelDataOffset - sizeof(IGIL_SamplerParams) * parentSamplerCount;
                IGIL_SamplerParams *pSamplerParams = (IGIL_SamplerParams *)ptrOffset(kernelReflectionSurface->getUnderlyingBuffer(), samplerParamsOffset);
                uint32_t sampler = 0;
                for (uint32_t argID = 0; argID < pBlockInfo->kernelArgInfo.size(); argID++) {
                    if (pBlockInfo->kernelArgInfo[argID].isSampler) {

                        pSamplerParams[sampler].m_ArgID = argID;
                        pSamplerParams[sampler].m_SamplerStateOffset = pBlockInfo->kernelArgInfo[argID].offsetHeap;
                        sampler++;
                    }
                }
            }

            ReflectionSurfaceHelper::setKernelAddressData(kernelReflectionSurface->getUnderlyingBuffer(),
                                                          offset,
                                                          kernelDataOffset,
                                                          samplerHeapOffset,
                                                          constantBufferOffset,
                                                          samplerParamsOffset,
                                                          sshTokenOffsetsFromKernelData[i] + kernelDataOffset,
                                                          btOffset,
                                                          *pBlockInfo,
                                                          hwInfo);

            if (samplerHeapSize > 0) {
                void *pDst = ptrOffset(kernelReflectionSurface->getUnderlyingBuffer(), samplerHeapOffset);
                const void *pSrc = ptrOffset(pBlockInfo->heapInfo.pDsh, pBlockInfo->getBorderColorOffset());
                memcpy_s(pDst, samplerHeapSize, pSrc, samplerHeapSize);
            }

            void *pDst = ptrOffset(kernelReflectionSurface->getUnderlyingBuffer(), constantBufferOffset);
            const char *pSrc = pBlockInfo->crossThreadData;
            memcpy_s(pDst, pBlockInfo->getConstantBufferSize(), pSrc, pBlockInfo->getConstantBufferSize());

            btOffset += pBlockInfo->patchInfo.bindingTableState->Offset;
            kernelDataOffset = newKernelDataOffset;
        }

        uint32_t samplerOffset = 0;
        if (parentSamplerCount) {
            samplerOffset = kernelDataOffset + parentImageCount * sizeof(IGIL_ImageParamters);
        }
        ReflectionSurfaceHelper::setKernelDataHeader(kernelReflectionSurface->getUnderlyingBuffer(), blockCount, parentImageCount, parentSamplerCount, kernelDataOffset, samplerOffset);
        delete[] curbeParamsForBlocks;
        delete[] tokenMask;
        delete[] sshTokenOffsetsFromKernelData;

        // Patch constant values once after reflection surface creation
        patchBlocksCurbeWithConstantValues();
    }

    if (DebugManager.flags.ForceDispatchScheduler.get()) {
        if (this->isSchedulerKernel && kernelReflectionSurface == nullptr) {
            kernelReflectionSurface = executionEnvironment.memoryManager->allocateGraphicsMemoryWithProperties(
                {pClDevice->getRootDeviceIndex(), MemoryConstants::pageSize,
                 GraphicsAllocation::AllocationType::DEVICE_QUEUE_BUFFER,
                 pClDevice->getDeviceBitfield()});
        }
    }
}

void Kernel::getParentObjectCounts(ObjectCounts &objectCount) {
    objectCount.imageCount = 0;
    objectCount.samplerCount = 0;
    DEBUG_BREAK_IF(!isParentKernel);

    for (const auto &arg : this->kernelArguments) {
        if (arg.type == SAMPLER_OBJ) {
            objectCount.samplerCount++;
        } else if (arg.type == IMAGE_OBJ) {
            objectCount.imageCount++;
        }
    }
}

bool Kernel::hasPrintfOutput(uint32_t rootDeviceIndex) const {
    return getKernelInfo(rootDeviceIndex).kernelDescriptor.kernelAttributes.flags.usesPrintf;
}

size_t Kernel::getInstructionHeapSizeForExecutionModel() const {
    BlockKernelManager *blockManager = program->getBlockKernelManager();
    uint32_t blockCount = static_cast<uint32_t>(blockManager->getCount());

    size_t totalSize = 0;
    if (isParentKernel) {
        totalSize = kernelBinaryAlignement - 1; // for initial alignment
        for (uint32_t i = 0; i < blockCount; i++) {
            const KernelInfo *pBlockInfo = blockManager->getBlockKernelInfo(i);
            totalSize += pBlockInfo->heapInfo.KernelHeapSize;
            totalSize = alignUp(totalSize, kernelBinaryAlignement);
        }
    }
    return totalSize;
}

void Kernel::patchBlocksCurbeWithConstantValues() {
    auto rootDeviceIndex = program->getDevices()[0]->getRootDeviceIndex();
    BlockKernelManager *blockManager = program->getBlockKernelManager();
    uint32_t blockCount = static_cast<uint32_t>(blockManager->getCount());

    uint64_t globalMemoryGpuAddress = program->getGlobalSurface(rootDeviceIndex) != nullptr ? program->getGlobalSurface(rootDeviceIndex)->getGpuAddressToPatch() : 0;
    uint64_t constantMemoryGpuAddress = program->getConstantSurface(rootDeviceIndex) != nullptr ? program->getConstantSurface(rootDeviceIndex)->getGpuAddressToPatch() : 0;

    for (uint32_t blockID = 0; blockID < blockCount; blockID++) {
        const KernelInfo *pBlockInfo = blockManager->getBlockKernelInfo(blockID);

        uint64_t globalMemoryCurbeOffset = ReflectionSurfaceHelper::undefinedOffset;
        uint32_t globalMemoryPatchSize = 0;
        uint64_t constantMemoryCurbeOffset = ReflectionSurfaceHelper::undefinedOffset;
        uint32_t constantMemoryPatchSize = 0;

        if (pBlockInfo->patchInfo.pAllocateStatelessGlobalMemorySurfaceWithInitialization) {
            globalMemoryCurbeOffset = pBlockInfo->patchInfo.pAllocateStatelessGlobalMemorySurfaceWithInitialization->DataParamOffset;
            globalMemoryPatchSize = pBlockInfo->patchInfo.pAllocateStatelessGlobalMemorySurfaceWithInitialization->DataParamSize;
        }

        if (pBlockInfo->patchInfo.pAllocateStatelessConstantMemorySurfaceWithInitialization) {
            constantMemoryCurbeOffset = pBlockInfo->patchInfo.pAllocateStatelessConstantMemorySurfaceWithInitialization->DataParamOffset;
            constantMemoryPatchSize = pBlockInfo->patchInfo.pAllocateStatelessConstantMemorySurfaceWithInitialization->DataParamSize;
        }

        ReflectionSurfaceHelper::patchBlocksCurbeWithConstantValues(kernelReflectionSurface->getUnderlyingBuffer(), blockID,
                                                                    globalMemoryCurbeOffset, globalMemoryPatchSize, globalMemoryGpuAddress,
                                                                    constantMemoryCurbeOffset, constantMemoryPatchSize, constantMemoryGpuAddress,
                                                                    ReflectionSurfaceHelper::undefinedOffset, 0, 0);
    }
}

void Kernel::ReflectionSurfaceHelper::getCurbeParams(std::vector<IGIL_KernelCurbeParams> &curbeParamsOut, uint64_t &tokenMaskOut, uint32_t &firstSSHTokenIndex, const KernelInfo &kernelInfo, const HardwareInfo &hwInfo) {

    size_t numArgs = kernelInfo.kernelArgInfo.size();
    size_t patchTokenCount = +kernelInfo.kernelNonArgInfo.size();
    uint64_t tokenMask = 0;
    tokenMaskOut = 0;
    firstSSHTokenIndex = 0;
    curbeParamsOut.reserve(patchTokenCount * 5);

    uint32_t bindingTableIndex = 253;

    for (uint32_t argNumber = 0; argNumber < numArgs; argNumber++) {
        IGIL_KernelCurbeParams curbeParam;
        bindingTableIndex = 253;
        auto sizeOfkernelArgForSSH = kernelInfo.gpuPointerSize;

        if (kernelInfo.kernelArgInfo[argNumber].isBuffer) {
            curbeParam.m_patchOffset = kernelInfo.kernelArgInfo[argNumber].kernelArgPatchInfoVector[0].crossthreadOffset;
            curbeParam.m_parameterSize = kernelInfo.gpuPointerSize;
            curbeParam.m_parameterType = COMPILER_DATA_PARAMETER_GLOBAL_SURFACE;
            curbeParam.m_sourceOffset = argNumber;

            curbeParamsOut.push_back(curbeParam);
            tokenMask |= shiftLeftBy(63);
        } else if (kernelInfo.kernelArgInfo[argNumber].isImage) {
            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetImgWidth)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_IMAGE_WIDTH + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetImgWidth, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetImgHeight)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_IMAGE_HEIGHT + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetImgHeight, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetImgDepth)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_IMAGE_DEPTH + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetImgDepth, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetChannelDataType)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_IMAGE_CHANNEL_DATA_TYPE + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetChannelDataType, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetChannelOrder)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_IMAGE_CHANNEL_ORDER + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetChannelOrder, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetArraySize)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_IMAGE_ARRAY_SIZE + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetArraySize, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetObjectId)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_OBJECT_ID + 50, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetObjectId, argNumber});
            }
            tokenMask |= shiftLeftBy(50);

            if (kernelInfo.patchInfo.bindingTableState) {
                auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);
                const void *ssh = static_cast<const char *>(kernelInfo.heapInfo.pSsh) + kernelInfo.patchInfo.bindingTableState->Offset;

                for (uint32_t i = 0; i < kernelInfo.patchInfo.bindingTableState->Count; i++) {

                    uint32_t pointer = hwHelper.getBindingTableStateSurfaceStatePointer(ssh, i);
                    if (pointer == kernelInfo.kernelArgInfo[argNumber].offsetHeap) {
                        bindingTableIndex = i;
                        break;
                    }
                }
                DEBUG_BREAK_IF(!((bindingTableIndex != 253) || (kernelInfo.patchInfo.bindingTableState->Count == 0)));
            }

        } else if (kernelInfo.kernelArgInfo[argNumber].isSampler) {
            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetSamplerSnapWa)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_SAMPLER_COORDINATE_SNAP_WA_REQUIRED + 100, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetSamplerSnapWa, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetSamplerAddressingMode)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_SAMPLER_ADDRESS_MODE + 100, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetSamplerAddressingMode, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetSamplerNormalizedCoords)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_SAMPLER_NORMALIZED_COORDS + 100, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetSamplerNormalizedCoords, argNumber});
            }

            if (isValidOffset(kernelInfo.kernelArgInfo[argNumber].offsetObjectId)) {
                curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_OBJECT_ID + 100, sizeof(uint32_t), kernelInfo.kernelArgInfo[argNumber].offsetObjectId, argNumber});
            }
            tokenMask |= shiftLeftBy(51);
        } else {
            bindingTableIndex = 0;
            sizeOfkernelArgForSSH = 0;
        }

        curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{1024, sizeOfkernelArgForSSH, bindingTableIndex, argNumber});

        if (kernelInfo.kernelArgInfo[argNumber].slmAlignment != 0) {
            DEBUG_BREAK_IF(kernelInfo.kernelArgInfo[argNumber].kernelArgPatchInfoVector.size() != 1);
            uint32_t offset = kernelInfo.kernelArgInfo[argNumber].kernelArgPatchInfoVector[0].crossthreadOffset;
            uint32_t srcOffset = kernelInfo.kernelArgInfo[argNumber].slmAlignment;
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_SUM_OF_LOCAL_MEMORY_OBJECT_ARGUMENT_SIZES, 0, offset, srcOffset});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_SUM_OF_LOCAL_MEMORY_OBJECT_ARGUMENT_SIZES);
        }
    }

    for (auto param : kernelInfo.patchInfo.dataParameterBuffersKernelArgs) {
        curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_KERNEL_ARGUMENT, param->DataSize, param->Offset, param->ArgumentNumber});
        tokenMask |= shiftLeftBy(DATA_PARAMETER_KERNEL_ARGUMENT);
    }

    for (uint32_t i = 0; i < 3; i++) {
        const uint32_t sizeOfParam = 4;
        if (kernelInfo.workloadInfo.enqueuedLocalWorkSizeOffsets[i] != WorkloadInfo::undefinedOffset) {
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_ENQUEUED_LOCAL_WORK_SIZE, sizeOfParam, kernelInfo.workloadInfo.enqueuedLocalWorkSizeOffsets[i], i * sizeOfParam});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_ENQUEUED_LOCAL_WORK_SIZE);
        }
        if (kernelInfo.workloadInfo.globalWorkOffsetOffsets[i] != WorkloadInfo::undefinedOffset) {
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_GLOBAL_WORK_OFFSET, sizeOfParam, kernelInfo.workloadInfo.globalWorkOffsetOffsets[i], i * sizeOfParam});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_GLOBAL_WORK_OFFSET);
        }
        if (kernelInfo.workloadInfo.globalWorkSizeOffsets[i] != WorkloadInfo::undefinedOffset) {
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_GLOBAL_WORK_SIZE, sizeOfParam, kernelInfo.workloadInfo.globalWorkSizeOffsets[i], i * sizeOfParam});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_GLOBAL_WORK_SIZE);
        }
        if (kernelInfo.workloadInfo.localWorkSizeOffsets[i] != WorkloadInfo::undefinedOffset) {
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_LOCAL_WORK_SIZE, sizeOfParam, kernelInfo.workloadInfo.localWorkSizeOffsets[i], i * sizeOfParam});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_LOCAL_WORK_SIZE);
        }
        if (kernelInfo.workloadInfo.localWorkSizeOffsets2[i] != WorkloadInfo::undefinedOffset) {
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_LOCAL_WORK_SIZE, sizeOfParam, kernelInfo.workloadInfo.localWorkSizeOffsets2[i], i * sizeOfParam});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_LOCAL_WORK_SIZE);
        }
        if (kernelInfo.workloadInfo.numWorkGroupsOffset[i] != WorkloadInfo::undefinedOffset) {
            curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_NUM_WORK_GROUPS, sizeOfParam, kernelInfo.workloadInfo.numWorkGroupsOffset[i], i * sizeOfParam});
            tokenMask |= shiftLeftBy(DATA_PARAMETER_NUM_WORK_GROUPS);
        }
    }

    if (kernelInfo.workloadInfo.parentEventOffset != WorkloadInfo::undefinedOffset) {
        curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_PARENT_EVENT, sizeof(uint32_t), kernelInfo.workloadInfo.parentEventOffset, 0});
        tokenMask |= shiftLeftBy(DATA_PARAMETER_PARENT_EVENT);
    }
    if (kernelInfo.workloadInfo.workDimOffset != WorkloadInfo::undefinedOffset) {
        curbeParamsOut.emplace_back(IGIL_KernelCurbeParams{DATA_PARAMETER_WORK_DIMENSIONS, sizeof(uint32_t), kernelInfo.workloadInfo.workDimOffset, 0});
        tokenMask |= shiftLeftBy(DATA_PARAMETER_WORK_DIMENSIONS);
    }

    std::sort(curbeParamsOut.begin(), curbeParamsOut.end(), compareFunction);
    tokenMaskOut = tokenMask;
    firstSSHTokenIndex = static_cast<uint32_t>(curbeParamsOut.size() - numArgs);
}

uint32_t Kernel::ReflectionSurfaceHelper::setKernelData(void *reflectionSurface, uint32_t offset,
                                                        std::vector<IGIL_KernelCurbeParams> &curbeParamsIn, uint64_t tokenMaskIn,
                                                        size_t maxConstantBufferSize, size_t samplerCount, const KernelInfo &kernelInfo, const HardwareInfo &hwInfo) {
    uint32_t offsetToEnd = 0;
    IGIL_KernelData *kernelData = reinterpret_cast<IGIL_KernelData *>(ptrOffset(reflectionSurface, offset));
    size_t samplerHeapSize = alignUp(kernelInfo.getSamplerStateArraySize(hwInfo), Sampler::samplerStateArrayAlignment) + kernelInfo.getBorderColorStateSize();

    kernelData->m_numberOfCurbeParams = static_cast<uint32_t>(curbeParamsIn.size()); // number of paramters to patch
    kernelData->m_numberOfCurbeTokens = static_cast<uint32_t>(curbeParamsIn.size() - kernelInfo.kernelArgInfo.size());
    kernelData->m_numberOfSamplerStates = static_cast<uint32_t>(kernelInfo.getSamplerStateArrayCount());
    kernelData->m_SizeOfSamplerHeap = static_cast<uint32_t>(samplerHeapSize);
    kernelData->m_SamplerBorderColorStateOffsetOnDSH = kernelInfo.patchInfo.samplerStateArray ? kernelInfo.patchInfo.samplerStateArray->BorderColorOffset : 0;
    kernelData->m_SamplerStateArrayOffsetOnDSH = kernelInfo.patchInfo.samplerStateArray ? kernelInfo.patchInfo.samplerStateArray->Offset : (uint32_t)-1;
    kernelData->m_sizeOfConstantBuffer = kernelInfo.getConstantBufferSize();
    kernelData->m_PatchTokensMask = tokenMaskIn;
    kernelData->m_ScratchSpacePatchValue = 0;
    kernelData->m_SIMDSize = kernelInfo.getMaxSimdSize();
    kernelData->m_HasBarriers = kernelInfo.kernelDescriptor.kernelAttributes.barrierCount;
    kernelData->m_RequiredWkgSizes[0] = kernelInfo.kernelDescriptor.kernelAttributes.requiredWorkgroupSize[0];
    kernelData->m_RequiredWkgSizes[1] = kernelInfo.kernelDescriptor.kernelAttributes.requiredWorkgroupSize[1];
    kernelData->m_RequiredWkgSizes[2] = kernelInfo.kernelDescriptor.kernelAttributes.requiredWorkgroupSize[2];
    kernelData->m_InilineSLMSize = kernelInfo.workloadInfo.slmStaticSize;

    bool localIdRequired = false;
    if (kernelInfo.patchInfo.threadPayload) {
        if (kernelInfo.patchInfo.threadPayload->LocalIDFlattenedPresent ||
            kernelInfo.patchInfo.threadPayload->LocalIDXPresent ||
            kernelInfo.patchInfo.threadPayload->LocalIDYPresent ||
            kernelInfo.patchInfo.threadPayload->LocalIDZPresent) {
            localIdRequired = true;
        }
        kernelData->m_PayloadSize = PerThreadDataHelper::getThreadPayloadSize(*kernelInfo.patchInfo.threadPayload, kernelData->m_SIMDSize, hwInfo.capabilityTable.grfSize);
    }

    kernelData->m_NeedLocalIDS = localIdRequired ? 1 : 0;
    kernelData->m_DisablePreemption = 0u;

    bool concurrentExecAllowed = true;

    if (kernelInfo.patchInfo.pAllocateStatelessPrivateSurface) {
        if (kernelInfo.patchInfo.pAllocateStatelessPrivateSurface->PerThreadPrivateMemorySize > 0) {
            concurrentExecAllowed = false;
        }
    }
    kernelData->m_CanRunConcurently = concurrentExecAllowed ? 1 : 0;

    if (DebugManager.flags.DisableConcurrentBlockExecution.get()) {
        kernelData->m_CanRunConcurently = false;
    }

    IGIL_KernelCurbeParams *kernelCurbeParams = kernelData->m_data;

    for (uint32_t i = 0; i < curbeParamsIn.size(); i++) {
        kernelCurbeParams[i] = curbeParamsIn[i];
    }

    offsetToEnd = static_cast<uint32_t>(offset +
                                        alignUp(sizeof(IGIL_KernelData) + sizeof(IGIL_KernelCurbeParams) * curbeParamsIn.size(), sizeof(void *)) +
                                        alignUp(samplerHeapSize, sizeof(void *)) +
                                        alignUp(maxConstantBufferSize, sizeof(void *)) +
                                        sizeof(IGIL_SamplerParams) * samplerCount);

    return offsetToEnd;
}

void Kernel::ReflectionSurfaceHelper::setKernelAddressDataBtOffset(void *reflectionSurface, uint32_t blockID, uint32_t btOffset) {

    uint32_t offset = static_cast<uint32_t>(offsetof(IGIL_KernelDataHeader, m_data) + sizeof(IGIL_KernelAddressData) * blockID);
    IGIL_KernelAddressData *kernelAddressData = reinterpret_cast<IGIL_KernelAddressData *>(ptrOffset(reflectionSurface, offset));

    kernelAddressData->m_BTSoffset = btOffset;
}

void Kernel::ReflectionSurfaceHelper::setKernelAddressData(void *reflectionSurface, uint32_t offset, uint32_t kernelDataOffset, uint32_t samplerHeapOffset,
                                                           uint32_t constantBufferOffset, uint32_t samplerParamsOffset,
                                                           uint32_t sshTokensOffset, uint32_t btOffset, const KernelInfo &kernelInfo, const HardwareInfo &hwInfo) {
    IGIL_KernelAddressData *kernelAddressData = reinterpret_cast<IGIL_KernelAddressData *>(ptrOffset(reflectionSurface, offset));

    auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    kernelAddressData->m_KernelDataOffset = kernelDataOffset;
    kernelAddressData->m_SamplerHeapOffset = samplerHeapOffset;
    kernelAddressData->m_SamplerParamsOffset = samplerParamsOffset;
    kernelAddressData->m_ConstantBufferOffset = constantBufferOffset;
    kernelAddressData->m_SSHTokensOffset = sshTokensOffset;
    kernelAddressData->m_BTSoffset = btOffset;
    kernelAddressData->m_BTSize = static_cast<uint32_t>(kernelInfo.patchInfo.bindingTableState ? kernelInfo.patchInfo.bindingTableState->Count * hwHelper.getBindingTableStateSize() : 0);
}

template <>
void Kernel::ReflectionSurfaceHelper::patchBlocksCurbe<false>(void *reflectionSurface, uint32_t blockID,
                                                              uint64_t defaultDeviceQueueCurbeOffset, uint32_t patchSizeDefaultQueue, uint64_t defaultDeviceQueueGpuAddress,
                                                              uint64_t eventPoolCurbeOffset, uint32_t patchSizeEventPool, uint64_t eventPoolGpuAddress,
                                                              uint64_t deviceQueueCurbeOffset, uint32_t patchSizeDeviceQueue, uint64_t deviceQueueGpuAddress,
                                                              uint64_t printfBufferOffset, uint32_t patchSizePrintfBuffer, uint64_t printfBufferGpuAddress,
                                                              uint64_t privateSurfaceOffset, uint32_t privateSurfaceSize, uint64_t privateSurfaceGpuAddress) {

    IGIL_KernelDataHeader *pKernelHeader = reinterpret_cast<IGIL_KernelDataHeader *>(reflectionSurface);

    // Reflection surface must be initialized prior to patching blocks curbe on KRS
    DEBUG_BREAK_IF(blockID >= pKernelHeader->m_numberOfKernels);

    IGIL_KernelAddressData *addressData = pKernelHeader->m_data;
    // const buffer offsets must be set
    DEBUG_BREAK_IF(addressData[blockID].m_ConstantBufferOffset == 0);

    void *pCurbe = ptrOffset(reflectionSurface, addressData[blockID].m_ConstantBufferOffset);

    if (defaultDeviceQueueCurbeOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)defaultDeviceQueueCurbeOffset);
        patchWithRequiredSize(patchedPointer, patchSizeDefaultQueue, (uintptr_t)defaultDeviceQueueGpuAddress);
    }
    if (eventPoolCurbeOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)eventPoolCurbeOffset);
        patchWithRequiredSize(patchedPointer, patchSizeEventPool, (uintptr_t)eventPoolGpuAddress);
    }
    if (deviceQueueCurbeOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)deviceQueueCurbeOffset);
        patchWithRequiredSize(patchedPointer, patchSizeDeviceQueue, (uintptr_t)deviceQueueGpuAddress);
    }
    if (printfBufferOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)printfBufferOffset);
        patchWithRequiredSize(patchedPointer, patchSizePrintfBuffer, (uintptr_t)printfBufferGpuAddress);
    }

    if (privateSurfaceOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)privateSurfaceOffset);
        patchWithRequiredSize(patchedPointer, privateSurfaceSize, (uintptr_t)privateSurfaceGpuAddress);
    }
}

void Kernel::ReflectionSurfaceHelper::patchBlocksCurbeWithConstantValues(void *reflectionSurface, uint32_t blockID,
                                                                         uint64_t globalMemoryCurbeOffset, uint32_t globalMemoryPatchSize, uint64_t globalMemoryGpuAddress,
                                                                         uint64_t constantMemoryCurbeOffset, uint32_t constantMemoryPatchSize, uint64_t constantMemoryGpuAddress,
                                                                         uint64_t privateMemoryCurbeOffset, uint32_t privateMemoryPatchSize, uint64_t privateMemoryGpuAddress) {

    IGIL_KernelDataHeader *pKernelHeader = reinterpret_cast<IGIL_KernelDataHeader *>(reflectionSurface);

    // Reflection surface must be initialized prior to patching blocks curbe on KRS
    DEBUG_BREAK_IF(blockID >= pKernelHeader->m_numberOfKernels);

    IGIL_KernelAddressData *addressData = pKernelHeader->m_data;
    // const buffer offsets must be set
    DEBUG_BREAK_IF(addressData[blockID].m_ConstantBufferOffset == 0);

    void *pCurbe = ptrOffset(reflectionSurface, addressData[blockID].m_ConstantBufferOffset);

    if (globalMemoryCurbeOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)globalMemoryCurbeOffset);
        patchWithRequiredSize(patchedPointer, globalMemoryPatchSize, (uintptr_t)globalMemoryGpuAddress);
    }
    if (constantMemoryCurbeOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)constantMemoryCurbeOffset);
        patchWithRequiredSize(patchedPointer, constantMemoryPatchSize, (uintptr_t)constantMemoryGpuAddress);
    }
    if (privateMemoryCurbeOffset != undefinedOffset) {
        auto *patchedPointer = ptrOffset(pCurbe, (size_t)privateMemoryCurbeOffset);
        patchWithRequiredSize(patchedPointer, privateMemoryPatchSize, (uintptr_t)privateMemoryGpuAddress);
    }
}

void Kernel::ReflectionSurfaceHelper::setParentImageParams(void *reflectionSurface, std::vector<Kernel::SimpleKernelArgInfo> &parentArguments, const KernelInfo &parentKernelInfo) {
    IGIL_KernelDataHeader *pKernelHeader = reinterpret_cast<IGIL_KernelDataHeader *>(reflectionSurface);
    IGIL_ImageParamters *pImageParameters = reinterpret_cast<IGIL_ImageParamters *>(ptrOffset(pKernelHeader, (size_t)pKernelHeader->m_ParentImageDataOffset));

    uint32_t numArgs = (uint32_t)parentArguments.size();
    for (uint32_t i = 0; i < numArgs; i++) {
        if (parentArguments[i].type == Kernel::kernelArgType::IMAGE_OBJ) {
            const Image *image = castToObject<Image>((cl_mem)parentArguments[i].object);
            if (image) {
                pImageParameters->m_ArraySize = (uint32_t)image->getImageDesc().image_array_size;
                pImageParameters->m_Depth = (uint32_t)image->getImageDesc().image_depth;
                pImageParameters->m_Height = (uint32_t)image->getImageDesc().image_height;
                pImageParameters->m_Width = (uint32_t)image->getImageDesc().image_width;
                pImageParameters->m_NumMipLevels = (uint32_t)image->getImageDesc().num_mip_levels;
                pImageParameters->m_NumSamples = (uint32_t)image->getImageDesc().num_samples;

                pImageParameters->m_ChannelDataType = (uint32_t)image->getImageFormat().image_channel_data_type;
                pImageParameters->m_ChannelOrder = (uint32_t)image->getImageFormat().image_channel_data_type;
                pImageParameters->m_ObjectID = (uint32_t)parentKernelInfo.kernelArgInfo[i].offsetHeap;
                pImageParameters++;
            }
        }
    }
}

void Kernel::ReflectionSurfaceHelper::setParentSamplerParams(void *reflectionSurface, std::vector<Kernel::SimpleKernelArgInfo> &parentArguments, const KernelInfo &parentKernelInfo) {
    IGIL_KernelDataHeader *pKernelHeader = reinterpret_cast<IGIL_KernelDataHeader *>(reflectionSurface);
    IGIL_ParentSamplerParams *pParentSamplerParams = reinterpret_cast<IGIL_ParentSamplerParams *>(ptrOffset(pKernelHeader, (size_t)pKernelHeader->m_ParentSamplerParamsOffset));

    uint32_t numArgs = (uint32_t)parentArguments.size();
    for (uint32_t i = 0; i < numArgs; i++) {
        if (parentArguments[i].type == Kernel::kernelArgType::SAMPLER_OBJ) {
            const Sampler *sampler = castToObject<Sampler>((cl_sampler)parentArguments[i].object);
            if (sampler) {
                pParentSamplerParams->CoordinateSnapRequired = (uint32_t)sampler->getSnapWaValue();
                pParentSamplerParams->m_AddressingMode = (uint32_t)sampler->addressingMode;
                pParentSamplerParams->NormalizedCoords = (uint32_t)sampler->normalizedCoordinates;

                pParentSamplerParams->m_ObjectID = OCLRT_ARG_OFFSET_TO_SAMPLER_OBJECT_ID((uint32_t)parentKernelInfo.kernelArgInfo[i].offsetHeap);
                pParentSamplerParams++;
            }
        }
    }
}

void Kernel::resetSharedObjectsPatchAddresses() {
    for (size_t i = 0; i < getKernelArgsNumber(); i++) {
        auto clMem = (cl_mem)kernelArguments[i].object;
        auto memObj = castToObject<MemObj>(clMem);
        if (memObj && memObj->peekSharingHandler()) {
            setArg((uint32_t)i, sizeof(cl_mem), &clMem);
        }
    }
}

void Kernel::provideInitializationHints() {

    Context *context = program->getContextPtr();
    if (context == nullptr || !context->isProvidingPerformanceHints())
        return;

    for (auto &pClDevice : getDevices()) {
        auto rootDeviceIndex = pClDevice->getRootDeviceIndex();
        if (kernelDeviceInfos[rootDeviceIndex].privateSurfaceSize) {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_BAD_INTEL, PRIVATE_MEMORY_USAGE_TOO_HIGH,
                                            kernelInfos[rootDeviceIndex]->kernelDescriptor.kernelMetadata.kernelName.c_str(),
                                            kernelDeviceInfos[rootDeviceIndex].privateSurfaceSize);
        }
        const auto &patchInfo = kernelInfos[rootDeviceIndex]->patchInfo;
        if (patchInfo.mediavfestate) {
            auto scratchSize = patchInfo.mediavfestate->PerThreadScratchSpace;
            scratchSize *= pClDevice->getSharedDeviceInfo().computeUnitsUsedForScratch * getKernelInfo(rootDeviceIndex).getMaxSimdSize();
            if (scratchSize > 0) {
                context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_BAD_INTEL, REGISTER_PRESSURE_TOO_HIGH,
                                                kernelInfos[rootDeviceIndex]->kernelDescriptor.kernelMetadata.kernelName.c_str(), scratchSize);
            }
        }
    }
}

void Kernel::patchDefaultDeviceQueue(DeviceQueue *devQueue) {

    auto rootDeviceIndex = devQueue->getDevice().getRootDeviceIndex();
    const auto &patchInfo = kernelInfos[rootDeviceIndex]->patchInfo;
    if (patchInfo.pAllocateStatelessDefaultDeviceQueueSurface) {
        if (kernelDeviceInfos[rootDeviceIndex].crossThreadData) {
            auto patchLocation = ptrOffset(reinterpret_cast<uint32_t *>(getCrossThreadData(rootDeviceIndex)),
                                           patchInfo.pAllocateStatelessDefaultDeviceQueueSurface->DataParamOffset);

            patchWithRequiredSize(patchLocation, patchInfo.pAllocateStatelessDefaultDeviceQueueSurface->DataParamSize,
                                  static_cast<uintptr_t>(devQueue->getQueueBuffer()->getGpuAddressToPatch()));
        }
        if (requiresSshForBuffers(rootDeviceIndex)) {
            auto surfaceState = ptrOffset(reinterpret_cast<uintptr_t *>(getSurfaceStateHeap(rootDeviceIndex)),
                                          patchInfo.pAllocateStatelessDefaultDeviceQueueSurface->SurfaceStateHeapOffset);
            Buffer::setSurfaceState(&devQueue->getDevice(), surfaceState, false, false, devQueue->getQueueBuffer()->getUnderlyingBufferSize(),
                                    (void *)devQueue->getQueueBuffer()->getGpuAddress(), 0, devQueue->getQueueBuffer(), 0, 0);
        }
    }
}

void Kernel::patchEventPool(DeviceQueue *devQueue) {
    auto rootDeviceIndex = devQueue->getDevice().getRootDeviceIndex();
    const auto &eventPoolSurfaceAddress = kernelInfos[rootDeviceIndex]->kernelDescriptor.payloadMappings.implicitArgs.deviceSideEnqueueEventPoolSurfaceAddress;

    if (isValidOffset(eventPoolSurfaceAddress.stateless) && kernelDeviceInfos[rootDeviceIndex].crossThreadData) {
        auto patchLocation = ptrOffset(reinterpret_cast<uint32_t *>(getCrossThreadData(rootDeviceIndex)), eventPoolSurfaceAddress.stateless);
        patchWithRequiredSize(patchLocation, eventPoolSurfaceAddress.pointerSize,
                              static_cast<uintptr_t>(devQueue->getEventPoolBuffer()->getGpuAddressToPatch()));
    }

    if (isValidOffset(eventPoolSurfaceAddress.bindful)) {
        auto surfaceState = ptrOffset(reinterpret_cast<uintptr_t *>(getSurfaceStateHeap(rootDeviceIndex)), eventPoolSurfaceAddress.bindful);
        auto eventPoolBuffer = devQueue->getEventPoolBuffer();
        Buffer::setSurfaceState(&devQueue->getDevice(), surfaceState, false, false, eventPoolBuffer->getUnderlyingBufferSize(),
                                (void *)eventPoolBuffer->getGpuAddress(), 0, eventPoolBuffer, 0, 0);
    }
}

void Kernel::patchBlocksSimdSize(uint32_t rootDeviceIndex) {
    BlockKernelManager *blockManager = program->getBlockKernelManager();

    for (auto &idOffset : kernelInfos[rootDeviceIndex]->childrenKernelsIdOffset) {

        DEBUG_BREAK_IF(!(idOffset.first < static_cast<uint32_t>(blockManager->getCount())));

        const KernelInfo *blockInfo = blockManager->getBlockKernelInfo(idOffset.first);
        uint32_t *simdSize = reinterpret_cast<uint32_t *>(&kernelDeviceInfos[rootDeviceIndex].crossThreadData[idOffset.second]);
        *simdSize = blockInfo->getMaxSimdSize();
    }
}

bool Kernel::usesSyncBuffer(uint32_t rootDeviceIndex) {
    return (getKernelInfo(rootDeviceIndex).patchInfo.pAllocateSyncBuffer != nullptr);
}

void Kernel::patchSyncBuffer(Device &device, GraphicsAllocation *gfxAllocation, size_t bufferOffset) {
    auto rootDeviceIndex = device.getRootDeviceIndex();
    auto &patchInfo = kernelInfos[rootDeviceIndex]->patchInfo;
    auto bufferPatchAddress = ptrOffset(getCrossThreadData(rootDeviceIndex), patchInfo.pAllocateSyncBuffer->DataParamOffset);
    patchWithRequiredSize(bufferPatchAddress, patchInfo.pAllocateSyncBuffer->DataParamSize,
                          ptrOffset(gfxAllocation->getGpuAddressToPatch(), bufferOffset));

    if (requiresSshForBuffers(rootDeviceIndex)) {
        auto surfaceState = ptrOffset(reinterpret_cast<uintptr_t *>(getSurfaceStateHeap(rootDeviceIndex)),
                                      patchInfo.pAllocateSyncBuffer->SurfaceStateHeapOffset);
        auto addressToPatch = gfxAllocation->getUnderlyingBuffer();
        auto sizeToPatch = gfxAllocation->getUnderlyingBufferSize();
        Buffer::setSurfaceState(&device, surfaceState, false, false, sizeToPatch, addressToPatch, 0, gfxAllocation, 0, 0);
    }
}

template void Kernel::patchReflectionSurface<false>(DeviceQueue *, PrintfHandler *);

bool Kernel::isPatched() const {
    return patchedArgumentsNum == getDefaultKernelInfo().argumentsToPatchNum;
}
cl_int Kernel::checkCorrectImageAccessQualifier(cl_uint argIndex,
                                                size_t argSize,
                                                const void *argValue) const {
    auto &defaultKernelInfo = getDefaultKernelInfo();
    if (defaultKernelInfo.kernelArgInfo[argIndex].isImage) {
        cl_mem mem = *(static_cast<const cl_mem *>(argValue));
        MemObj *pMemObj = nullptr;
        WithCastToInternal(mem, &pMemObj);
        if (pMemObj) {
            auto accessQualifier = defaultKernelInfo.kernelArgInfo[argIndex].metadata.accessQualifier;
            cl_mem_flags flags = pMemObj->getFlags();
            if ((accessQualifier == KernelArgMetadata::AccessReadOnly && ((flags | CL_MEM_WRITE_ONLY) == flags)) ||
                (accessQualifier == KernelArgMetadata::AccessWriteOnly && ((flags | CL_MEM_READ_ONLY) == flags))) {
                return CL_INVALID_ARG_VALUE;
            }
        } else {
            return CL_INVALID_ARG_VALUE;
        }
    }
    return CL_SUCCESS;
}

void Kernel::resolveArgs() {
    if (!Kernel::isPatched() || !imageTransformer->hasRegisteredImages3d() || !canTransformImages())
        return;
    bool canTransformImageTo2dArray = true;
    for (uint32_t i = 0; i < patchedArgumentsNum; i++) {
        if (getDefaultKernelInfo().kernelArgInfo.at(i).isSampler) {
            auto sampler = castToObject<Sampler>(kernelArguments.at(i).object);
            if (sampler->isTransformable()) {
                canTransformImageTo2dArray = true;
            } else {
                canTransformImageTo2dArray = false;
                break;
            }
        }
    }
    for (auto rootDeviceIndex = 0u; rootDeviceIndex < kernelInfos.size(); rootDeviceIndex++) {
        auto pKernelInfo = kernelInfos[rootDeviceIndex];
        if (!pKernelInfo) {
            continue;
        }
        if (canTransformImageTo2dArray) {
            imageTransformer->transformImagesTo2dArray(*pKernelInfo, kernelArguments, getSurfaceStateHeap(rootDeviceIndex));
        } else if (imageTransformer->didTransform()) {
            imageTransformer->transformImagesTo3d(*pKernelInfo, kernelArguments, getSurfaceStateHeap(rootDeviceIndex));
        }
    }
}

bool Kernel::canTransformImages() const {
    auto renderCoreFamily = program->getDevices()[0]->getHardwareInfo().platform.eRenderCoreFamily;
    return renderCoreFamily >= IGFX_GEN9_CORE && renderCoreFamily <= IGFX_GEN11LP_CORE;
}

void Kernel::fillWithKernelObjsForAuxTranslation(KernelObjsForAuxTranslation &kernelObjsForAuxTranslation, uint32_t rootDeviceIndex) {
    kernelObjsForAuxTranslation.reserve(getKernelArgsNumber());
    auto &kernelInfo = getKernelInfo(rootDeviceIndex);
    for (uint32_t i = 0; i < getKernelArgsNumber(); i++) {
        if (BUFFER_OBJ == kernelArguments.at(i).type && !kernelInfo.kernelArgInfo.at(i).pureStatefulBufferAccess) {
            auto buffer = castToObject<Buffer>(getKernelArg(i));
            if (buffer && buffer->getMultiGraphicsAllocation().getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED) {
                kernelObjsForAuxTranslation.insert({KernelObjForAuxTranslation::Type::MEM_OBJ, buffer});
                auto &context = this->program->getContext();
                if (context.isProvidingPerformanceHints()) {
                    context.providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_BAD_INTEL, KERNEL_ARGUMENT_AUX_TRANSLATION,
                                                   kernelInfo.kernelDescriptor.kernelMetadata.kernelName.c_str(), i, kernelInfo.kernelArgInfo.at(i).metadataExtended->argName.c_str());
                }
            }
        }
        if (SVM_ALLOC_OBJ == getKernelArguments().at(i).type && !kernelInfo.kernelArgInfo.at(i).pureStatefulBufferAccess) {
            auto svmAlloc = reinterpret_cast<GraphicsAllocation *>(const_cast<void *>(getKernelArg(i)));
            if (svmAlloc && svmAlloc->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED) {
                kernelObjsForAuxTranslation.insert({KernelObjForAuxTranslation::Type::GFX_ALLOC, svmAlloc});
                auto &context = this->program->getContext();
                if (context.isProvidingPerformanceHints()) {
                    context.providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_BAD_INTEL, KERNEL_ARGUMENT_AUX_TRANSLATION,
                                                   kernelInfo.kernelDescriptor.kernelMetadata.kernelName.c_str(), i, kernelInfo.kernelArgInfo.at(i).metadataExtended->argName.c_str());
                }
            }
        }
    }
}

bool Kernel::hasDirectStatelessAccessToHostMemory() const {
    for (uint32_t i = 0; i < getKernelArgsNumber(); i++) {
        if (BUFFER_OBJ == kernelArguments.at(i).type && !getDefaultKernelInfo().kernelArgInfo.at(i).pureStatefulBufferAccess) {
            auto buffer = castToObject<Buffer>(getKernelArg(i));
            if (buffer && buffer->getMultiGraphicsAllocation().getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY) {
                return true;
            }
        }
        if (SVM_ALLOC_OBJ == kernelArguments.at(i).type && !getDefaultKernelInfo().kernelArgInfo.at(i).pureStatefulBufferAccess) {
            auto svmAlloc = reinterpret_cast<const GraphicsAllocation *>(getKernelArg(i));
            if (svmAlloc && svmAlloc->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY) {
                return true;
            }
        }
    }
    return false;
}

bool Kernel::hasIndirectStatelessAccessToHostMemory() const {
    if (!getDefaultKernelInfo().hasIndirectStatelessAccess) {
        return false;
    }

    for (auto gfxAllocation : kernelUnifiedMemoryGfxAllocations) {
        if (gfxAllocation->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY) {
            return true;
        }
    }

    if (unifiedMemoryControls.indirectHostAllocationsAllowed) {
        return getContext().getSVMAllocsManager()->hasHostAllocations();
    }

    return false;
}

void Kernel::getAllocationsForCacheFlush(CacheFlushAllocationsVec &out, uint32_t rootDeviceIndex) const {
    if (false == HwHelper::cacheFlushAfterWalkerSupported(getHardwareInfo(rootDeviceIndex))) {
        return;
    }
    for (GraphicsAllocation *alloc : this->kernelArgRequiresCacheFlush) {
        if (nullptr == alloc) {
            continue;
        }

        out.push_back(alloc);
    }

    auto global = getProgram()->getGlobalSurface(rootDeviceIndex);
    if (global != nullptr) {
        out.push_back(global);
    }

    if (svmAllocationsRequireCacheFlush) {
        for (GraphicsAllocation *alloc : kernelSvmGfxAllocations) {
            if (allocationForCacheFlush(alloc)) {
                out.push_back(alloc);
            }
        }
    }
}

bool Kernel::allocationForCacheFlush(GraphicsAllocation *argAllocation) const {
    return argAllocation->isFlushL3Required();
}

void Kernel::addAllocationToCacheFlushVector(uint32_t argIndex, GraphicsAllocation *argAllocation) {
    if (argAllocation == nullptr) {
        kernelArgRequiresCacheFlush[argIndex] = nullptr;
    } else {
        if (allocationForCacheFlush(argAllocation)) {
            kernelArgRequiresCacheFlush[argIndex] = argAllocation;
        } else {
            kernelArgRequiresCacheFlush[argIndex] = nullptr;
        }
    }
}

void Kernel::setReflectionSurfaceBlockBtOffset(uint32_t blockID, uint32_t offset) {
    DEBUG_BREAK_IF(blockID >= program->getBlockKernelManager()->getCount());
    ReflectionSurfaceHelper::setKernelAddressDataBtOffset(getKernelReflectionSurface()->getUnderlyingBuffer(), blockID, offset);
}

bool Kernel::checkIfIsParentKernelAndBlocksUsesPrintf() {
    return isParentKernel && getProgram()->getBlockKernelManager()->getIfBlockUsesPrintf();
}

uint64_t Kernel::getKernelStartOffset(
    const bool localIdsGenerationByRuntime,
    const bool kernelUsesLocalIds,
    const bool isCssUsed,
    uint32_t rootDeviceIndex) const {

    uint64_t kernelStartOffset = 0;

    if (kernelInfos[rootDeviceIndex]->getGraphicsAllocation()) {
        kernelStartOffset = kernelInfos[rootDeviceIndex]->getGraphicsAllocation()->getGpuAddressToPatch();
        if (localIdsGenerationByRuntime == false && kernelUsesLocalIds == true) {
            kernelStartOffset += kernelInfos[rootDeviceIndex]->patchInfo.threadPayload->OffsetToSkipPerThreadDataLoad;
        }
    }

    kernelStartOffset += getStartOffset();

    auto &hardwareInfo = getHardwareInfo(rootDeviceIndex);
    auto &hwHelper = HwHelper::get(hardwareInfo.platform.eRenderCoreFamily);

    if (isCssUsed && hwHelper.isOffsetToSkipSetFFIDGPWARequired(hardwareInfo)) {
        kernelStartOffset += kernelInfos[rootDeviceIndex]->patchInfo.threadPayload->OffsetToSkipSetFFIDGP;
    }

    return kernelStartOffset;
}

void Kernel::patchBindlessSurfaceStateOffsets(const Device &device, const size_t sshOffset) {
    auto rootDeviceIndex = device.getRootDeviceIndex();
    auto &kernelInfo = *kernelInfos[rootDeviceIndex];
    const bool bindlessUsed = kernelInfo.kernelDescriptor.kernelAttributes.bufferAddressingMode == KernelDescriptor::AddressingMode::BindlessAndStateless;

    if (bindlessUsed) {
        auto &hardwareInfo = device.getHardwareInfo();
        auto &hwHelper = HwHelper::get(hardwareInfo.platform.eRenderCoreFamily);

        for (size_t i = 0; i < kernelInfo.kernelArgInfo.size(); i++) {
            if ((kernelInfo.kernelArgInfo[i].isBuffer) ||
                (kernelInfo.kernelArgInfo[i].isImage)) {

                auto patchLocation = ptrOffset(getCrossThreadData(device.getRootDeviceIndex()),
                                               kernelInfo.kernelArgInfo[i].kernelArgPatchInfoVector[0].crossthreadOffset);

                auto bindlessOffset = static_cast<uint32_t>(sshOffset) + kernelInfo.kernelArgInfo[i].offsetHeap;
                auto patchValue = hwHelper.getBindlessSurfaceExtendedMessageDescriptorValue(bindlessOffset);
                patchWithRequiredSize(patchLocation, sizeof(patchValue), patchValue);
            }
        }
    }
}

void Kernel::setAdditionalKernelExecInfo(uint32_t additionalKernelExecInfo) {
    this->additionalKernelExecInfo = additionalKernelExecInfo;
}

uint32_t Kernel::getAdditionalKernelExecInfo() const {
    return this->additionalKernelExecInfo;
}

bool Kernel::requiresWaDisableRccRhwoOptimization(uint32_t rootDeviceIndex) const {
    auto &hardwareInfo = getHardwareInfo(rootDeviceIndex);
    auto &hwHelper = HwHelper::get(hardwareInfo.platform.eRenderCoreFamily);

    if (hwHelper.isWaDisableRccRhwoOptimizationRequired() && isUsingSharedObjArgs()) {
        for (auto &arg : getKernelArguments()) {
            auto clMemObj = static_cast<cl_mem>(arg.object);
            auto memObj = castToObject<MemObj>(clMemObj);
            if (memObj && memObj->peekSharingHandler()) {
                auto allocation = memObj->getGraphicsAllocation(rootDeviceIndex);
                for (uint32_t handleId = 0u; handleId < allocation->getNumGmms(); handleId++) {
                    if (allocation->getGmm(handleId)->gmmResourceInfo->getResourceFlags()->Info.MediaCompressed) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

const HardwareInfo &Kernel::getHardwareInfo(uint32_t rootDeviceIndex) const {
    return *executionEnvironment.rootDeviceEnvironments[rootDeviceIndex]->getHardwareInfo();
}

const KernelInfo &Kernel::getDefaultKernelInfo() const {
    return *kernelInfos[defaultRootDeviceIndex];
}
void Kernel::setGlobalWorkOffsetValues(uint32_t rootDeviceIndex, uint32_t globalWorkOffsetX, uint32_t globalWorkOffsetY, uint32_t globalWorkOffsetZ) {
    *kernelDeviceInfos[rootDeviceIndex].globalWorkOffsetX = globalWorkOffsetX;
    *kernelDeviceInfos[rootDeviceIndex].globalWorkOffsetY = globalWorkOffsetY;
    *kernelDeviceInfos[rootDeviceIndex].globalWorkOffsetZ = globalWorkOffsetZ;
}

void Kernel::setGlobalWorkSizeValues(uint32_t rootDeviceIndex, uint32_t globalWorkSizeX, uint32_t globalWorkSizeY, uint32_t globalWorkSizeZ) {
    *kernelDeviceInfos[rootDeviceIndex].globalWorkSizeX = globalWorkSizeX;
    *kernelDeviceInfos[rootDeviceIndex].globalWorkSizeY = globalWorkSizeY;
    *kernelDeviceInfos[rootDeviceIndex].globalWorkSizeZ = globalWorkSizeZ;
}

void Kernel::setLocalWorkSizeValues(uint32_t rootDeviceIndex, uint32_t localWorkSizeX, uint32_t localWorkSizeY, uint32_t localWorkSizeZ) {
    *kernelDeviceInfos[rootDeviceIndex].localWorkSizeX = localWorkSizeX;
    *kernelDeviceInfos[rootDeviceIndex].localWorkSizeY = localWorkSizeY;
    *kernelDeviceInfos[rootDeviceIndex].localWorkSizeZ = localWorkSizeZ;
}

void Kernel::setLocalWorkSize2Values(uint32_t rootDeviceIndex, uint32_t localWorkSizeX, uint32_t localWorkSizeY, uint32_t localWorkSizeZ) {
    *kernelDeviceInfos[rootDeviceIndex].localWorkSizeX2 = localWorkSizeX;
    *kernelDeviceInfos[rootDeviceIndex].localWorkSizeY2 = localWorkSizeY;
    *kernelDeviceInfos[rootDeviceIndex].localWorkSizeZ2 = localWorkSizeZ;
}

void Kernel::setEnqueuedLocalWorkSizeValues(uint32_t rootDeviceIndex, uint32_t localWorkSizeX, uint32_t localWorkSizeY, uint32_t localWorkSizeZ) {
    *kernelDeviceInfos[rootDeviceIndex].enqueuedLocalWorkSizeX = localWorkSizeX;
    *kernelDeviceInfos[rootDeviceIndex].enqueuedLocalWorkSizeY = localWorkSizeY;
    *kernelDeviceInfos[rootDeviceIndex].enqueuedLocalWorkSizeZ = localWorkSizeZ;
}

bool Kernel::isLocalWorkSize2Patched(uint32_t rootDeviceIndex) {
    return kernelDeviceInfos[rootDeviceIndex].localWorkSizeX2 != &dummyPatchLocation;
}

void Kernel::setNumWorkGroupsValues(uint32_t rootDeviceIndex, uint32_t numWorkGroupsX, uint32_t numWorkGroupsY, uint32_t numWorkGroupsZ) {
    *kernelDeviceInfos[rootDeviceIndex].numWorkGroupsX = numWorkGroupsX;
    *kernelDeviceInfos[rootDeviceIndex].numWorkGroupsY = numWorkGroupsY;
    *kernelDeviceInfos[rootDeviceIndex].numWorkGroupsZ = numWorkGroupsZ;
}

void Kernel::setWorkDim(uint32_t rootDeviceIndex, uint32_t workDim) {
    *kernelDeviceInfos[rootDeviceIndex].workDim = workDim;
}

uint32_t Kernel::getMaxKernelWorkGroupSize(uint32_t rootDeviceIndex) const {
    return kernelDeviceInfos[rootDeviceIndex].maxKernelWorkGroupSize;
}
uint32_t Kernel::getSlmTotalSize(uint32_t rootDeviceIndex) const {
    return kernelDeviceInfos[rootDeviceIndex].slmTotalSize;
}
} // namespace NEO
