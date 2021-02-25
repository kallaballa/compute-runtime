/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/built_ins/built_ins.h"
#include "shared/source/command_stream/command_stream_receiver.h"

#include "opencl/source/command_queue/command_queue_hw.h"
#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/helpers/mipmap.h"
#include "opencl/source/mem_obj/buffer.h"
#include "opencl/source/mem_obj/image.h"
#include "opencl/source/memory_manager/mem_obj_surface.h"

#include <new>

namespace NEO {

template <typename GfxFamily>
cl_int CommandQueueHw<GfxFamily>::enqueueCopyBufferToImage(
    Buffer *srcBuffer,
    Image *dstImage,
    size_t srcOffset,
    const size_t *dstOrigin,
    const size_t *region,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {

    auto rootDeviceIndex = getDevice().getRootDeviceIndex();

    srcBuffer->getMigrateableMultiGraphicsAllocation().ensureMemoryOnDevice(*getDevice().getMemoryManager(), rootDeviceIndex);
    dstImage->getMigrateableMultiGraphicsAllocation().ensureMemoryOnDevice(*getDevice().getMemoryManager(), rootDeviceIndex);

    auto eBuiltInOpsType = EBuiltInOps::CopyBufferToImage3d;
    if (forceStateless(srcBuffer->getSize())) {
        eBuiltInOpsType = EBuiltInOps::CopyBufferToImage3dStateless;
    }

    auto &builder = BuiltInDispatchBuilderOp::getBuiltinDispatchInfoBuilder(eBuiltInOpsType,
                                                                            this->getClDevice());
    BuiltInOwnershipWrapper builtInLock(builder);

    MemObjSurface srcBufferSurf(srcBuffer);
    MemObjSurface dstImgSurf(dstImage);
    Surface *surfaces[] = {&srcBufferSurf, &dstImgSurf};

    BuiltinOpParams dc;
    dc.srcMemObj = srcBuffer;
    dc.dstMemObj = dstImage;
    dc.srcOffset = {srcOffset, 0, 0};
    dc.dstOffset = dstOrigin;
    dc.size = region;
    if (isMipMapped(dstImage->getImageDesc())) {
        dc.dstMipLevel = findMipLevel(dstImage->getImageDesc().image_type, dstOrigin);
    }

    MultiDispatchInfo dispatchInfo(dc);
    builder.buildDispatchInfos(dispatchInfo);

    enqueueHandler<CL_COMMAND_COPY_BUFFER_TO_IMAGE>(
        surfaces,
        false,
        dispatchInfo,
        numEventsInWaitList,
        eventWaitList,
        event);

    return CL_SUCCESS;
}
} // namespace NEO
