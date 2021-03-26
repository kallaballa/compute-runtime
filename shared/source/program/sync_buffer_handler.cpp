/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/program/sync_buffer_handler.h"

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/memory_manager/graphics_allocation.h"
#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {

SyncBufferHandler::~SyncBufferHandler() {
    memoryManager.checkGpuUsageAndDestroyGraphicsAllocations(graphicsAllocation);
};
SyncBufferHandler::SyncBufferHandler(Device &device)
    : device(device), memoryManager(*device.getMemoryManager()) {

    allocateNewBuffer();
}

void SyncBufferHandler::makeResident(CommandStreamReceiver &csr) {
    csr.makeResident(*graphicsAllocation);
}

void SyncBufferHandler::allocateNewBuffer() {
    AllocationProperties allocationProperties{device.getRootDeviceIndex(), true, bufferSize,
                                              GraphicsAllocation::AllocationType::LINEAR_STREAM,
                                              false, false, device.getDeviceBitfield()};
    graphicsAllocation = memoryManager.allocateGraphicsMemoryWithProperties(allocationProperties);
    UNRECOVERABLE_IF(graphicsAllocation == nullptr);

    auto cpuPointer = graphicsAllocation->getUnderlyingBuffer();
    std::memset(cpuPointer, 0, bufferSize);
}

} // namespace NEO
