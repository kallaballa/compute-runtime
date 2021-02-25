/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/built_ins/built_ins.h"

#include "opencl/source/kernel/kernel.h"

#include <algorithm>

namespace NEO {

class SchedulerKernel : public Kernel {
  public:
    static constexpr const char *schedulerName = "SchedulerParallel20";
    friend Kernel;

    ~SchedulerKernel() override = default;

    size_t getLws() {
        return PARALLEL_SCHEDULER_HWTHREADS_IN_HW_GROUP20 * PARALLEL_SCHEDULER_COMPILATION_SIZE_20;
    }

    size_t getGws() {
        return gws;
    }

    void setGws(size_t newGws) {
        gws = newGws;
    }

    size_t getCurbeSize() {
        auto &defaultKernelInfo = getDefaultKernelInfo();
        size_t crossTrheadDataSize = defaultKernelInfo.patchInfo.dataParameterStream ? defaultKernelInfo.patchInfo.dataParameterStream->DataParameterStreamSize : 0;
        size_t dshSize = defaultKernelInfo.heapInfo.DynamicStateHeapSize;

        crossTrheadDataSize = alignUp(crossTrheadDataSize, 64);
        dshSize = alignUp(dshSize, 64);

        return alignUp(SCHEDULER_DYNAMIC_PAYLOAD_SIZE, 64) + crossTrheadDataSize + dshSize;
    }

    void setArgs(GraphicsAllocation *queue,
                 GraphicsAllocation *commandsStack,
                 GraphicsAllocation *eventsPool,
                 GraphicsAllocation *secondaryBatchBuffer,
                 GraphicsAllocation *dsh,
                 GraphicsAllocation *reflectionSurface,
                 GraphicsAllocation *queueStorageBuffer,
                 GraphicsAllocation *ssh,
                 GraphicsAllocation *debugQueue = nullptr);
    static BuiltinCode loadSchedulerKernel(Device *device);

  protected:
    SchedulerKernel(Program *programArg, const KernelInfoContainer &kernelInfosArg) : Kernel(programArg, kernelInfosArg, true) {
        computeGws();
    };

    void computeGws();

    size_t gws = 0u;
};

} // namespace NEO
