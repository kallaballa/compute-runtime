/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/mocks/mock_kernel.h"

#include "opencl/source/kernel/kernel.inl"
#include "opencl/source/program/printf_handler.h"

namespace NEO {

MockKernel::BlockPatchValues MockKernel::ReflectionSurfaceHelperPublic::devQueue;
MockKernel::BlockPatchValues MockKernel::ReflectionSurfaceHelperPublic::defaultQueue;
MockKernel::BlockPatchValues MockKernel::ReflectionSurfaceHelperPublic::eventPool;
MockKernel::BlockPatchValues MockKernel::ReflectionSurfaceHelperPublic::printfBuffer;

const uint32_t MockDebugKernel::perThreadSystemThreadSurfaceSize = 0x100;

template <>
void Kernel::ReflectionSurfaceHelper::patchBlocksCurbe<true>(void *reflectionSurface, uint32_t blockID,
                                                             uint64_t defaultDeviceQueueCurbeOffset, uint32_t patchSizeDefaultQueue, uint64_t defaultDeviceQueueGpuAddress,
                                                             uint64_t eventPoolCurbeOffset, uint32_t patchSizeEventPool, uint64_t eventPoolGpuAddress,
                                                             uint64_t deviceQueueCurbeOffset, uint32_t patchSizeDeviceQueue, uint64_t deviceQueueGpuAddress,
                                                             uint64_t printfBufferOffset, uint32_t patchSizePrintfBuffer, uint64_t printfBufferGpuAddress,
                                                             uint64_t privateSurfaceOffset, uint32_t privateSurfaceSize, uint64_t privateSurfaceGpuAddress) {

    MockKernel::ReflectionSurfaceHelperPublic::patchBlocksCurbeMock(reflectionSurface, blockID,
                                                                    defaultDeviceQueueCurbeOffset, patchSizeDefaultQueue, defaultDeviceQueueGpuAddress,
                                                                    eventPoolCurbeOffset, patchSizeEventPool, eventPoolGpuAddress,
                                                                    deviceQueueCurbeOffset, patchSizeDeviceQueue, deviceQueueGpuAddress,
                                                                    printfBufferOffset, patchSizePrintfBuffer, printfBufferGpuAddress);
}

template void Kernel::patchReflectionSurface<true>(DeviceQueue *, PrintfHandler *);

const KernelInfoContainer MockKernel::toKernelInfoContainer(const KernelInfo &kernelInfo, uint32_t rootDeviceIndex) {
    KernelInfoContainer kernelInfos;
    kernelInfos.resize(rootDeviceIndex + 1);
    kernelInfos[rootDeviceIndex] = &kernelInfo;
    return kernelInfos;
}

bool MockKernel::isPatched() const {
    return isPatchedOverride;
}

bool MockKernel::canTransformImages() const {
    return canKernelTransformImages;
}

void MockKernel::makeResident(CommandStreamReceiver &commandStreamReceiver) {
    makeResidentCalls++;
    Kernel::makeResident(commandStreamReceiver);
}

void MockKernel::getResidency(std::vector<Surface *> &dst, uint32_t rootDeviceIndex) {
    getResidencyCalls++;
    Kernel::getResidency(dst, rootDeviceIndex);
}
bool MockKernel::requiresCacheFlushCommand(const CommandQueue &commandQueue) const {
    if (DebugManager.flags.EnableCacheFlushAfterWalker.get() != -1) {
        return !!DebugManager.flags.EnableCacheFlushAfterWalker.get();
    }

    return false;
}
} // namespace NEO
