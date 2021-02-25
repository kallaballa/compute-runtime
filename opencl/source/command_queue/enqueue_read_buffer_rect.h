/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/built_ins/built_ins.h"
#include "shared/source/command_stream/command_stream_receiver.h"

#include "opencl/source/command_queue/command_queue_hw.h"
#include "opencl/source/command_queue/enqueue_common.h"
#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/mem_obj/buffer.h"
#include "opencl/source/memory_manager/mem_obj_surface.h"

#include <new>

namespace NEO {

template <typename GfxFamily>
cl_int CommandQueueHw<GfxFamily>::enqueueReadBufferRect(
    Buffer *buffer,
    cl_bool blockingRead,
    const size_t *bufferOrigin,
    const size_t *hostOrigin,
    const size_t *region,
    size_t bufferRowPitch,
    size_t bufferSlicePitch,
    size_t hostRowPitch,
    size_t hostSlicePitch,
    void *ptr,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {

    auto rootDeviceIndex = getDevice().getRootDeviceIndex();

    buffer->getMigrateableMultiGraphicsAllocation().ensureMemoryOnDevice(*getDevice().getMemoryManager(), rootDeviceIndex);

    const cl_command_type cmdType = CL_COMMAND_READ_BUFFER_RECT;
    auto isMemTransferNeeded = true;
    if (buffer->isMemObjZeroCopy()) {
        size_t bufferOffset;
        size_t hostOffset;
        computeOffsetsValueForRectCommands(&bufferOffset, &hostOffset, bufferOrigin, hostOrigin, region, bufferRowPitch, bufferSlicePitch, hostRowPitch, hostSlicePitch);
        isMemTransferNeeded = buffer->checkIfMemoryTransferIsRequired(bufferOffset, hostOffset, ptr, cmdType);
    }
    if (!isMemTransferNeeded) {
        return enqueueMarkerForReadWriteOperation(buffer, ptr, cmdType, blockingRead,
                                                  numEventsInWaitList, eventWaitList, event);
    }

    auto eBuiltInOps = EBuiltInOps::CopyBufferRect;
    if (forceStateless(buffer->getSize())) {
        eBuiltInOps = EBuiltInOps::CopyBufferRectStateless;
    }

    size_t hostPtrSize = Buffer::calculateHostPtrSize(hostOrigin, region, hostRowPitch, hostSlicePitch);
    void *dstPtr = ptr;

    MemObjSurface bufferSurf(buffer);
    HostPtrSurface hostPtrSurf(dstPtr, hostPtrSize);
    Surface *surfaces[] = {&bufferSurf, &hostPtrSurf};
    auto blitAllowed = blitEnqueueAllowed(cmdType);

    if (region[0] != 0 &&
        region[1] != 0 &&
        region[2] != 0) {
        auto &csr = getCommandStreamReceiver(blitAllowed);
        bool status = csr.createAllocationForHostSurface(hostPtrSurf, true);
        if (!status) {
            return CL_OUT_OF_RESOURCES;
        }
        dstPtr = reinterpret_cast<void *>(hostPtrSurf.getAllocation()->getGpuAddress());
    }

    void *alignedDstPtr = alignDown(dstPtr, 4);
    size_t dstPtrOffset = ptrDiff(dstPtr, alignedDstPtr);

    BuiltinOpParams dc;
    dc.srcMemObj = buffer;
    dc.dstPtr = alignedDstPtr;
    dc.srcOffset = bufferOrigin;
    dc.dstOffset = hostOrigin;
    dc.transferAllocation = hostPtrSurf.getAllocation();
    dc.dstOffset.x += dstPtrOffset;
    dc.size = region;
    dc.srcRowPitch = bufferRowPitch;
    dc.srcSlicePitch = bufferSlicePitch;
    dc.dstRowPitch = hostRowPitch;
    dc.dstSlicePitch = hostSlicePitch;

    MultiDispatchInfo dispatchInfo(dc);
    dispatchBcsOrGpgpuEnqueue<CL_COMMAND_READ_BUFFER_RECT>(dispatchInfo, surfaces, eBuiltInOps, numEventsInWaitList, eventWaitList, event, blockingRead, blitAllowed);

    if (context->isProvidingPerformanceHints()) {
        context->providePerformanceHintForMemoryTransfer(CL_COMMAND_READ_BUFFER_RECT, true, static_cast<cl_mem>(buffer), ptr);
        if (!isL3Capable(ptr, hostPtrSize)) {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_BAD_INTEL, CL_ENQUEUE_READ_BUFFER_RECT_DOESNT_MEET_ALIGNMENT_RESTRICTIONS, ptr, hostPtrSize, MemoryConstants::pageSize, MemoryConstants::pageSize);
        }
    }

    return CL_SUCCESS;
}
} // namespace NEO
