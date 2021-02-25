/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/command_stream/preemption.h"
#include "shared/source/helpers/register_offsets.h"
#include "shared/source/helpers/timestamp_packet.h"
#include "shared/source/helpers/vec.h"
#include "shared/source/indirect_heap/indirect_heap.h"
#include "shared/source/utilities/tag_allocator.h"

#include "opencl/source/command_queue/command_queue.h"
#include "opencl/source/context/context.h"
#include "opencl/source/device_queue/device_queue_hw.h"
#include "opencl/source/event/hw_timestamps.h"
#include "opencl/source/event/perf_counter.h"
#include "opencl/source/helpers/dispatch_info.h"
#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/helpers/task_information.h"
#include "opencl/source/kernel/kernel.h"
#include "opencl/source/program/kernel_info.h"

namespace NEO {

struct RootDeviceEnvironment;

template <typename GfxFamily>
using WALKER_TYPE = typename GfxFamily::WALKER_TYPE;
template <typename GfxFamily>
using MI_STORE_REG_MEM = typename GfxFamily::MI_STORE_REGISTER_MEM_CMD;

void computeWorkgroupSize1D(
    uint32_t maxWorkGroupSize,
    size_t workGroupSize[3],
    const size_t workItems[3],
    size_t simdSize);

void computeWorkgroupSizeND(
    WorkSizeInfo wsInfo,
    size_t workGroupSize[3],
    const size_t workItems[3],
    const uint32_t workDim);

void computeWorkgroupSize2D(
    uint32_t maxWorkGroupSize,
    size_t workGroupSize[3],
    const size_t workItems[3],
    size_t simdSize);

void computeWorkgroupSizeSquared(
    uint32_t maxWorkGroupSize,
    size_t workGroupSize[3],
    const size_t workItems[3],
    size_t simdSize,
    const uint32_t workDim);

Vec3<size_t> computeWorkgroupSize(
    const DispatchInfo &dispatchInfo);

Vec3<size_t> generateWorkgroupSize(
    const DispatchInfo &dispatchInfo);

Vec3<size_t> computeWorkgroupsNumber(
    const Vec3<size_t> gws,
    const Vec3<size_t> lws);

Vec3<size_t> generateWorkgroupsNumber(
    const Vec3<size_t> gws,
    const Vec3<size_t> lws);

Vec3<size_t> generateWorkgroupsNumber(
    const DispatchInfo &dispatchInfo);

inline uint32_t calculateDispatchDim(Vec3<size_t> dispatchSize, Vec3<size_t> dispatchOffset) {
    return std::max(1U, std::max(dispatchSize.getSimplifiedDim(), dispatchOffset.getSimplifiedDim()));
}

Vec3<size_t> canonizeWorkgroup(
    Vec3<size_t> workgroup);

void provideLocalWorkGroupSizeHints(Context *context, DispatchInfo dispatchInfo);

void setSpecialWorkgroupSize(size_t workgroupSize[3]);

inline cl_uint computeDimensions(const size_t workItems[3]) {
    return (workItems[2] > 1) ? 3 : (workItems[1] > 1) ? 2 : 1;
}

template <typename GfxFamily>
class GpgpuWalkerHelper {
  public:
    static void applyWADisableLSQCROPERFforOCL(LinearStream *pCommandStream,
                                               const Kernel &kernel,
                                               bool disablePerfMode);

    static size_t getSizeForWADisableLSQCROPERFforOCL(const Kernel *pKernel);
    static size_t getSizeForWaDisableRccRhwoOptimization(const Kernel *pKernel, uint32_t rootDeviceIndex);

    static size_t setGpgpuWalkerThreadData(
        WALKER_TYPE<GfxFamily> *walkerCmd,
        const size_t globalOffsets[3],
        const size_t startWorkGroups[3],
        const size_t numWorkGroups[3],
        const size_t localWorkSizesIn[3],
        uint32_t simd,
        uint32_t workDim,
        bool localIdsGenerationByRuntime,
        bool inlineDataProgrammingRequired,
        const iOpenCL::SPatchThreadPayload &threadPayload,
        uint32_t requiredWorkgroupOrder);

    static void dispatchProfilingCommandsStart(
        TagNode<HwTimeStamps> &hwTimeStamps,
        LinearStream *commandStream,
        const HardwareInfo &hwInfo);

    static void dispatchProfilingCommandsEnd(
        TagNode<HwTimeStamps> &hwTimeStamps,
        LinearStream *commandStream,
        const HardwareInfo &hwInfo);

    static void dispatchPerfCountersCommandsStart(
        CommandQueue &commandQueue,
        TagNode<HwPerfCounter> &hwPerfCounter,
        LinearStream *commandStream);

    static void dispatchPerfCountersCommandsEnd(
        CommandQueue &commandQueue,
        TagNode<HwPerfCounter> &hwPerfCounter,
        LinearStream *commandStream);

    static void setupTimestampPacket(
        LinearStream *cmdStream,
        WALKER_TYPE<GfxFamily> *walkerCmd,
        TagNode<TimestampPacketStorage> *timestampPacketNode,
        const RootDeviceEnvironment &rootDeviceEnvironment);

    static void dispatchScheduler(
        LinearStream &commandStream,
        DeviceQueueHw<GfxFamily> &devQueueHw,
        PreemptionMode preemptionMode,
        SchedulerKernel &scheduler,
        IndirectHeap *ssh,
        IndirectHeap *dsh,
        bool isCcsUsed);

    static void adjustMiStoreRegMemMode(MI_STORE_REG_MEM<GfxFamily> *storeCmd);

  private:
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;

    static void addAluReadModifyWriteRegister(
        LinearStream *pCommandStream,
        uint32_t aluRegister,
        AluRegisters operation,
        uint32_t mask);
};

template <typename GfxFamily>
struct EnqueueOperation {
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;
    static size_t getTotalSizeRequiredCS(uint32_t eventType, const CsrDependencies &csrDeps, bool reserveProfilingCmdsSpace, bool reservePerfCounters, bool blitEnqueue, CommandQueue &commandQueue, const MultiDispatchInfo &multiDispatchInfo);
    static size_t getSizeRequiredCS(uint32_t cmdType, bool reserveProfilingCmdsSpace, bool reservePerfCounters, CommandQueue &commandQueue, const Kernel *pKernel);
    static size_t getSizeRequiredForTimestampPacketWrite();
    static size_t getSizeForCacheFlushAfterWalkerCommands(const Kernel &kernel, const CommandQueue &commandQueue);

  private:
    static size_t getSizeRequiredCSKernel(bool reserveProfilingCmdsSpace, bool reservePerfCounters, CommandQueue &commandQueue, const Kernel *pKernel);
    static size_t getSizeRequiredCSNonKernel(bool reserveProfilingCmdsSpace, bool reservePerfCounters, CommandQueue &commandQueue);
};

template <typename GfxFamily, uint32_t eventType>
LinearStream &getCommandStream(CommandQueue &commandQueue, const CsrDependencies &csrDeps, bool reserveProfilingCmdsSpace,
                               bool reservePerfCounterCmdsSpace, bool blitEnqueue, const MultiDispatchInfo &multiDispatchInfo,
                               Surface **surfaces, size_t numSurfaces) {
    size_t expectedSizeCS = EnqueueOperation<GfxFamily>::getTotalSizeRequiredCS(eventType, csrDeps, reserveProfilingCmdsSpace, reservePerfCounterCmdsSpace, blitEnqueue, commandQueue, multiDispatchInfo);
    return commandQueue.getCS(expectedSizeCS);
}

template <typename GfxFamily, IndirectHeap::Type heapType>
IndirectHeap &getIndirectHeap(CommandQueue &commandQueue, const MultiDispatchInfo &multiDispatchInfo) {
    size_t expectedSize = 0;
    IndirectHeap *ih = nullptr;

    // clang-format off
    switch (heapType) {
    case IndirectHeap::DYNAMIC_STATE:   expectedSize = HardwareCommandsHelper<GfxFamily>::getTotalSizeRequiredDSH(multiDispatchInfo); break;
    case IndirectHeap::INDIRECT_OBJECT: expectedSize = HardwareCommandsHelper<GfxFamily>::getTotalSizeRequiredIOH(multiDispatchInfo); break;
    case IndirectHeap::SURFACE_STATE:   expectedSize = HardwareCommandsHelper<GfxFamily>::getTotalSizeRequiredSSH(multiDispatchInfo); break;
    }
    // clang-format on

    if (Kernel *parentKernel = multiDispatchInfo.peekParentKernel()) {
        if (heapType == IndirectHeap::SURFACE_STATE) {
            expectedSize += HardwareCommandsHelper<GfxFamily>::getSshSizeForExecutionModel(*parentKernel, commandQueue.getDevice().getRootDeviceIndex());
        } else //if (heapType == IndirectHeap::DYNAMIC_STATE || heapType == IndirectHeap::INDIRECT_OBJECT)
        {
            DeviceQueueHw<GfxFamily> *pDevQueue = castToObject<DeviceQueueHw<GfxFamily>>(commandQueue.getContext().getDefaultDeviceQueue());
            DEBUG_BREAK_IF(pDevQueue == nullptr);
            ih = pDevQueue->getIndirectHeap(IndirectHeap::DYNAMIC_STATE);
        }
    }

    if (ih == nullptr)
        ih = &commandQueue.getIndirectHeap(heapType, expectedSize);

    return *ih;
}

} // namespace NEO
