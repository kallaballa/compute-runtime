/*
 * Copyright (C) 2021-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/memory_manager/internal_allocation_storage.h"
#include "shared/source/os_interface/os_time.h"

#include "level_zero/core/source/event/event.h"
#include "level_zero/core/source/hw_helpers/l0_hw_helper.h"

namespace L0 {
template <typename TagSizeT>
Event *Event::create(EventPool *eventPool, const ze_event_desc_t *desc, Device *device) {
    auto event = new EventImp<TagSizeT>(eventPool, desc->index, device);
    UNRECOVERABLE_IF(event == nullptr);

    if (eventPool->isEventPoolTimestampFlagSet()) {
        event->setEventTimestampFlag(true);
    }
    auto neoDevice = device->getNEODevice();
    event->kernelEventCompletionData = std::make_unique<KernelEventCompletionData<TagSizeT>[]>(EventPacketsCount::maxKernelSplit);

    auto alloc = eventPool->getAllocation().getGraphicsAllocation(neoDevice->getRootDeviceIndex());

    uint64_t baseHostAddr = reinterpret_cast<uint64_t>(alloc->getUnderlyingBuffer());
    event->eventPoolOffset = desc->index * eventPool->getEventSize();
    event->hostAddress = reinterpret_cast<void *>(baseHostAddr + event->eventPoolOffset);
    event->signalScope = desc->signal;
    event->waitScope = desc->wait;
    event->csr = neoDevice->getDefaultEngine().commandStreamReceiver;
    bool useContextEndOffset = L0HwHelper::get(neoDevice->getHardwareInfo().platform.eRenderCoreFamily).multiTileCapablePlatform();
    int32_t overrideUseContextEndOffset = NEO::DebugManager.flags.UseContextEndOffsetForEventCompletion.get();
    if (overrideUseContextEndOffset != -1) {
        useContextEndOffset = !!overrideUseContextEndOffset;
    }
    event->setUsingContextEndOffset(useContextEndOffset);

    EventPoolImp *eventPoolImp = static_cast<struct EventPoolImp *>(eventPool);
    // do not reset even if it has been imported, since event pool
    // might have been imported after events being already signaled
    if (eventPoolImp->isImportedIpcPool == false) {
        event->resetDeviceCompletionData();
    }

    return event;
}

template <typename TagSizeT>
uint64_t EventImp<TagSizeT>::getGpuAddress(Device *device) {
    auto alloc = eventPool->getAllocation().getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex());
    return (alloc->getGpuAddress() + this->eventPoolOffset);
}

template <typename TagSizeT>
NEO::GraphicsAllocation &EventImp<TagSizeT>::getAllocation(Device *device) {
    return *this->eventPool->getAllocation().getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex());
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::calculateProfilingData() {
    constexpr uint32_t skipL3EventPacketIndex = 2u;
    globalStartTS = kernelEventCompletionData[0].getGlobalStartValue(0);
    globalEndTS = kernelEventCompletionData[0].getGlobalEndValue(0);
    contextStartTS = kernelEventCompletionData[0].getContextStartValue(0);
    contextEndTS = kernelEventCompletionData[0].getContextEndValue(0);

    auto getEndTS = [](bool &isOverflowed, const std::pair<uint64_t, uint64_t> &currTs, const uint64_t &end) {
        auto &[currStartTs, currEndTs] = currTs;

        if (isOverflowed == false) {
            if (currEndTs < currStartTs) {
                isOverflowed = true;
                return currEndTs;
            } else {
                return std::max(end, currEndTs);
            }
        } else {
            // if already overflowed, then track the endTs of new overflowing ones
            if (currEndTs < currStartTs) {
                return std::max(end, currEndTs);
            }
        }
        return end;
    };

    bool isGlobalTsOverflowed = false;
    bool isContextTsOverflowed = false;

    for (uint32_t kernelId = 0; kernelId < kernelCount; kernelId++) {
        const auto &eventCompletion = kernelEventCompletionData[kernelId];
        for (auto packetId = 0u; packetId < eventCompletion.getPacketsUsed(); packetId++) {
            if (this->l3FlushAppliedOnKernel.test(kernelId) && ((packetId % skipL3EventPacketIndex) != 0)) {
                continue;
            }
            const std::pair<uint64_t, uint64_t> currentGlobal(eventCompletion.getGlobalStartValue(packetId),
                                                              eventCompletion.getGlobalEndValue(packetId));
            const std::pair<uint64_t, uint64_t> currentContext(eventCompletion.getContextStartValue(packetId),
                                                               eventCompletion.getContextEndValue(packetId));

            globalStartTS = std::min(globalStartTS, currentGlobal.first);
            contextStartTS = std::min(contextStartTS, currentContext.first);
            globalEndTS = getEndTS(isGlobalTsOverflowed, currentGlobal, globalEndTS);
            contextEndTS = getEndTS(isContextTsOverflowed, currentContext, contextEndTS);
        }
    }
    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
void EventImp<TagSizeT>::assignKernelEventCompletionData(void *address) {
    for (uint32_t i = 0; i < kernelCount; i++) {
        uint32_t packetsToCopy = 0;
        packetsToCopy = kernelEventCompletionData[i].getPacketsUsed();
        for (uint32_t packetId = 0; packetId < packetsToCopy; packetId++) {
            kernelEventCompletionData[i].assignDataToAllTimestamps(packetId, address);
            address = ptrOffset(address, singlePacketSize);
        }
    }
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::queryStatusEventPackets() {
    assignKernelEventCompletionData(hostAddress);
    uint32_t queryVal = Event::STATE_CLEARED;
    for (uint32_t i = 0; i < kernelCount; i++) {
        uint32_t packetsToCheck = kernelEventCompletionData[i].getPacketsUsed();
        for (uint32_t packetId = 0; packetId < packetsToCheck; packetId++) {
            void const *queryAddress = isUsingContextEndOffset()
                                           ? kernelEventCompletionData[i].getContextEndAddress(packetId)
                                           : kernelEventCompletionData[i].getContextStartAddress(packetId);
            bool ready = NEO::WaitUtils::waitFunctionWithPredicate<const TagSizeT>(
                static_cast<TagSizeT const *>(queryAddress),
                queryVal,
                std::not_equal_to<TagSizeT>());
            if (!ready) {
                return ZE_RESULT_NOT_READY;
            }
        }
    }
    isCompleted = true;
    this->csr->getInternalAllocationStorage()->cleanAllocationList(this->csr->peekTaskCount(), NEO::AllocationUsage::TEMPORARY_ALLOCATION);
    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::queryStatus() {
    if (metricStreamer != nullptr) {
        TagSizeT *hostAddr = static_cast<TagSizeT *>(hostAddress);
        if (usingContextEndOffset) {
            hostAddr = ptrOffset(hostAddr, this->getContextEndOffset());
        }
        *hostAddr = metricStreamer->getNotificationState();
    }
    this->csr->downloadAllocations();
    this->csr->downloadAllocation(*eventPool->getAllocation().getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex()));
    if (isCompleted == true) {
        return ZE_RESULT_SUCCESS;
    } else {
        return queryStatusEventPackets();
    }
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::hostEventSetValueTimestamps(TagSizeT eventVal) {

    auto baseAddr = castToUint64(hostAddress);
    auto eventTsSetFunc = [](auto tsAddr, TagSizeT value) {
        auto tsptr = reinterpret_cast<void *>(tsAddr);
        memcpy_s(tsptr, sizeof(TagSizeT), static_cast<void *>(&value), sizeof(TagSizeT));
    };

    TagSizeT timestampStart = eventVal;
    TagSizeT timestampEnd = eventVal;
    if (eventVal == Event::STATE_SIGNALED) {
        timestampStart = static_cast<TagSizeT>(this->gpuStartTimestamp);
        timestampEnd = static_cast<TagSizeT>(this->gpuEndTimestamp);
    }
    for (uint32_t i = 0; i < kernelCount; i++) {
        uint32_t packetsToSet = kernelEventCompletionData[i].getPacketsUsed();
        for (uint32_t j = 0; j < packetsToSet; j++) {
            eventTsSetFunc(baseAddr + contextStartOffset, timestampStart);
            eventTsSetFunc(baseAddr + globalStartOffset, timestampStart);
            eventTsSetFunc(baseAddr + contextEndOffset, timestampEnd);
            eventTsSetFunc(baseAddr + globalEndOffset, timestampEnd);
            baseAddr += singlePacketSize;
        }
    }

    const auto dataSize = 4u * EventPacketsCount::maxKernelSplit * NEO::TimestampPacketSizeControl::preferredPacketCount;
    TagSizeT tagValues[dataSize];

    for (uint32_t index = 0u; index < dataSize; index++) {
        tagValues[index] = eventVal;
    }

    assignKernelEventCompletionData(tagValues);

    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::hostEventSetValue(TagSizeT eventVal) {
    UNRECOVERABLE_IF(hostAddress == nullptr);

    if (isEventTimestampFlagSet()) {
        return hostEventSetValueTimestamps(eventVal);
    }

    auto packetHostAddr = hostAddress;
    if (usingContextEndOffset) {
        packetHostAddr = ptrOffset(packetHostAddr, contextEndOffset);
    }

    for (uint32_t i = 0; i < kernelCount; i++) {
        uint32_t packetsToSet = kernelEventCompletionData[i].getPacketsUsed();
        for (uint32_t j = 0; j < packetsToSet; j++) {
            memcpy_s(packetHostAddr, sizeof(TagSizeT), static_cast<void *>(&eventVal), sizeof(TagSizeT));
            packetHostAddr = ptrOffset(packetHostAddr, singlePacketSize);
        }
    }

    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::hostSignal() {
    auto status = hostEventSetValue(Event::STATE_SIGNALED);
    if (status == ZE_RESULT_SUCCESS) {
        isCompleted = true;
    }
    return status;
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::hostSynchronize(uint64_t timeout) {
    std::chrono::microseconds elapsedTimeSinceGpuHangCheck{0};
    std::chrono::high_resolution_clock::time_point waitStartTime, lastHangCheckTime, currentTime;
    uint64_t timeDiff = 0;

    ze_result_t ret = ZE_RESULT_NOT_READY;

    if (this->csr->getType() == NEO::CommandStreamReceiverType::CSR_AUB) {
        return ZE_RESULT_SUCCESS;
    }

    if (timeout == 0) {
        return queryStatus();
    }

    waitStartTime = std::chrono::high_resolution_clock::now();
    lastHangCheckTime = waitStartTime;
    while (true) {
        ret = queryStatus();
        if (ret == ZE_RESULT_SUCCESS) {
            return ret;
        }

        currentTime = std::chrono::high_resolution_clock::now();
        elapsedTimeSinceGpuHangCheck = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastHangCheckTime);

        if (elapsedTimeSinceGpuHangCheck.count() >= this->gpuHangCheckPeriod.count()) {
            lastHangCheckTime = currentTime;
            if (this->csr->isGpuHangDetected()) {
                return ZE_RESULT_ERROR_DEVICE_LOST;
            }
        }

        if (timeout == std::numeric_limits<uint64_t>::max()) {
            continue;
        }

        timeDiff = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - waitStartTime).count();

        if (timeDiff >= timeout) {
            break;
        }
    }

    return ret;
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::reset() {
    this->resetCompletion();
    this->resetDeviceCompletionData();
    this->l3FlushAppliedOnKernel.reset();
    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
void EventImp<TagSizeT>::resetDeviceCompletionData() {
    this->kernelCount = EventPacketsCount::maxKernelSplit;
    for (uint32_t i = 0; i < kernelCount; i++) {
        this->kernelEventCompletionData[i].setPacketsUsed(NEO::TimestampPacketSizeControl::preferredPacketCount);
    }
    this->hostEventSetValue(Event::STATE_INITIAL);
    this->resetPackets();
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::queryKernelTimestamp(ze_kernel_timestamp_result_t *dstptr) {

    ze_kernel_timestamp_result_t &result = *dstptr;

    if (queryStatus() != ZE_RESULT_SUCCESS) {
        return ZE_RESULT_NOT_READY;
    }

    assignKernelEventCompletionData(hostAddress);
    calculateProfilingData();

    auto eventTsSetFunc = [&](uint64_t &timestampFieldToCopy, uint64_t &timestampFieldForWriting) {
        memcpy_s(&(timestampFieldForWriting), sizeof(uint64_t), static_cast<void *>(&timestampFieldToCopy), sizeof(uint64_t));
    };

    if (!NEO::HwHelper::get(device->getHwInfo().platform.eRenderCoreFamily).useOnlyGlobalTimestamps()) {
        eventTsSetFunc(contextStartTS, result.context.kernelStart);
        eventTsSetFunc(globalStartTS, result.global.kernelStart);
        eventTsSetFunc(contextEndTS, result.context.kernelEnd);
        eventTsSetFunc(globalEndTS, result.global.kernelEnd);
    } else {
        eventTsSetFunc(globalStartTS, result.context.kernelStart);
        eventTsSetFunc(globalStartTS, result.global.kernelStart);
        eventTsSetFunc(globalEndTS, result.context.kernelEnd);
        eventTsSetFunc(globalEndTS, result.global.kernelEnd);
    }
    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
ze_result_t EventImp<TagSizeT>::queryTimestampsExp(Device *device, uint32_t *pCount, ze_kernel_timestamp_result_t *pTimestamps) {
    uint32_t timestampPacket = 0;
    uint64_t globalStartTs, globalEndTs, contextStartTs, contextEndTs;
    globalStartTs = globalEndTs = contextStartTs = contextEndTs = Event::STATE_INITIAL;
    auto deviceImp = static_cast<DeviceImp *>(device);
    bool isStaticPartitioning = true;

    if (NEO::DebugManager.flags.EnableStaticPartitioning.get() == 0) {
        isStaticPartitioning = false;
    }

    if (!isStaticPartitioning) {
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }

    uint32_t numPacketsUsed = 1u;
    if (!deviceImp->isSubdevice) {
        numPacketsUsed = this->getPacketsInUse();
    }

    if ((*pCount == 0) ||
        (*pCount > numPacketsUsed)) {
        *pCount = numPacketsUsed;
        return ZE_RESULT_SUCCESS;
    }

    for (auto i = 0u; i < *pCount; i++) {
        ze_kernel_timestamp_result_t &result = *(pTimestamps + i);

        auto queryTsEventAssignFunc = [&](uint64_t &timestampFieldForWriting, uint64_t &timestampFieldToCopy) {
            memcpy_s(&timestampFieldForWriting, sizeof(uint64_t), static_cast<void *>(&timestampFieldToCopy), sizeof(uint64_t));
        };

        auto packetId = i;
        if (deviceImp->isSubdevice) {
            packetId = static_cast<NEO::SubDevice *>(deviceImp->getNEODevice())->getSubDeviceIndex();
        }

        globalStartTs = kernelEventCompletionData[timestampPacket].getGlobalStartValue(packetId);
        contextStartTs = kernelEventCompletionData[timestampPacket].getContextStartValue(packetId);
        contextEndTs = kernelEventCompletionData[timestampPacket].getContextEndValue(packetId);
        globalEndTs = kernelEventCompletionData[timestampPacket].getGlobalEndValue(packetId);

        queryTsEventAssignFunc(result.global.kernelStart, globalStartTs);
        queryTsEventAssignFunc(result.context.kernelStart, contextStartTs);
        queryTsEventAssignFunc(result.global.kernelEnd, globalEndTs);
        queryTsEventAssignFunc(result.context.kernelEnd, contextEndTs);
    }

    return ZE_RESULT_SUCCESS;
}

template <typename TagSizeT>
void EventImp<TagSizeT>::resetPackets() {
    for (uint32_t i = 0; i < kernelCount; i++) {
        kernelEventCompletionData[i].setPacketsUsed(1);
    }
    kernelCount = 1;
    cpuStartTimestamp = 0;
    gpuStartTimestamp = 0;
    gpuEndTimestamp = 0;
    this->csr = this->device->getNEODevice()->getDefaultEngine().commandStreamReceiver;
}

template <typename TagSizeT>
uint32_t EventImp<TagSizeT>::getPacketsInUse() {
    uint32_t packetsInUse = 0;
    for (uint32_t i = 0; i < kernelCount; i++) {
        packetsInUse += kernelEventCompletionData[i].getPacketsUsed();
    }
    return packetsInUse;
}

template <typename TagSizeT>
uint32_t EventImp<TagSizeT>::getPacketsUsedInLastKernel() {
    return kernelEventCompletionData[getCurrKernelDataIndex()].getPacketsUsed();
}

template <typename TagSizeT>
void EventImp<TagSizeT>::setPacketsInUse(uint32_t value) {
    kernelEventCompletionData[getCurrKernelDataIndex()].setPacketsUsed(value);
}

template <typename TagSizeT>
uint64_t EventImp<TagSizeT>::getPacketAddress(Device *device) {
    uint64_t address = getGpuAddress(device);
    for (uint32_t i = 0; i < kernelCount - 1; i++) {
        address += kernelEventCompletionData[i].getPacketsUsed() *
                   singlePacketSize;
    }
    return address;
}

template <typename TagSizeT>
void EventImp<TagSizeT>::setGpuStartTimestamp() {
    if (isEventTimestampFlagSet()) {
        this->device->getGlobalTimestamps(&cpuStartTimestamp, &gpuStartTimestamp);
        cpuStartTimestamp = cpuStartTimestamp / this->device->getNEODevice()->getDeviceInfo().outProfilingTimerResolution;
    }
}

template <typename TagSizeT>
void EventImp<TagSizeT>::setGpuEndTimestamp() {
    if (isEventTimestampFlagSet()) {
        auto resolution = this->device->getNEODevice()->getDeviceInfo().outProfilingTimerResolution;
        auto cpuEndTimestamp = this->device->getNEODevice()->getOSTime()->getCpuRawTimestamp() / resolution;
        this->gpuEndTimestamp = gpuStartTimestamp + (cpuEndTimestamp - cpuStartTimestamp);
    }
}

} // namespace L0
