/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/helpers/timestamp_packet.h"

#include "level_zero/core/source/cmdlist/cmdlist.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/driver/driver_handle.h"
#include <level_zero/ze_api.h>

struct _ze_event_handle_t {};

struct _ze_event_pool_handle_t {};

namespace L0 {
typedef uint64_t FlushStamp;
struct EventPool;
struct MetricStreamer;
using TimestampPacketStorage = NEO::TimestampPackets<uint32_t>;

struct Event : _ze_event_handle_t {
    virtual ~Event() = default;
    virtual ze_result_t destroy();
    virtual ze_result_t hostSignal() = 0;
    virtual ze_result_t hostSynchronize(uint64_t timeout) = 0;
    virtual ze_result_t queryStatus() = 0;
    virtual ze_result_t reset() = 0;
    virtual ze_result_t queryKernelTimestamp(ze_kernel_timestamp_result_t *dstptr) = 0;
    enum State : uint32_t {
        STATE_SIGNALED = 0u,
        STATE_CLEARED = static_cast<uint32_t>(-1),
        STATE_INITIAL = STATE_CLEARED
    };

    static Event *create(EventPool *eventPool, const ze_event_desc_t *desc, Device *device);

    static Event *fromHandle(ze_event_handle_t handle) { return static_cast<Event *>(handle); }

    inline ze_event_handle_t toHandle() { return this; }

    virtual NEO::GraphicsAllocation &getAllocation();

    void increasePacketsInUse() { packetsInUse++; }
    void resetPackets() { packetsInUse = 0; }
    uint32_t getPacketsInUse() { return packetsInUse; }
    uint64_t getTimestampPacketAddress();
    virtual uint64_t getGpuAddress();

    void *hostAddress = nullptr;
    uint32_t packetsInUse;
    uint64_t gpuAddress = 0u;

    ze_event_scope_flags_t signalScope = 0u;
    ze_event_scope_flags_t waitScope = 0u;
    bool isTimestampEvent = false;
    bool allocOnDevice = false;

    std::unique_ptr<TimestampPacketStorage> timestampsData = nullptr;
    uint64_t globalStartTS;
    uint64_t globalEndTS;
    uint64_t contextStartTS;
    uint64_t contextEndTS;

    // Metric streamer instance associated with the event.
    MetricStreamer *metricStreamer = nullptr;
    NEO::CommandStreamReceiver *csr = nullptr;

  protected:
    NEO::GraphicsAllocation *allocation = nullptr;
};

struct EventImp : public Event {
    EventImp(EventPool *eventPool, int index, Device *device)
        : device(device), eventPool(eventPool) {}

    ~EventImp() override {}

    ze_result_t hostSignal() override;

    ze_result_t hostSynchronize(uint64_t timeout) override;

    ze_result_t queryStatus() override;

    ze_result_t reset() override;

    ze_result_t queryKernelTimestamp(ze_kernel_timestamp_result_t *dstptr) override;

    Device *device;
    EventPool *eventPool;

  protected:
    ze_result_t calculateProfilingData();
    ze_result_t hostEventSetValue(uint32_t eventValue);
    ze_result_t hostEventSetValueTimestamps(uint32_t eventVal);
    void assignTimestampData(void *address);
    void makeAllocationResident();
};

struct EventPool : _ze_event_pool_handle_t {
    static EventPool *create(DriverHandle *driver, uint32_t numDevices, ze_device_handle_t *phDevices, const ze_event_pool_desc_t *desc);
    virtual ~EventPool() = default;
    virtual ze_result_t destroy() = 0;
    virtual ze_result_t getIpcHandle(ze_ipc_event_pool_handle_t *pIpcHandle) = 0;
    virtual ze_result_t closeIpcHandle() = 0;
    virtual ze_result_t createEvent(const ze_event_desc_t *desc, ze_event_handle_t *phEvent) = 0;
    virtual Device *getDevice() = 0;

    static EventPool *fromHandle(ze_event_pool_handle_t handle) {
        return static_cast<EventPool *>(handle);
    }

    inline ze_event_pool_handle_t toHandle() { return this; }

    virtual NEO::MultiGraphicsAllocation &getAllocation() { return *eventPoolAllocations; }

    virtual size_t getEventSize() = 0;

    bool isEventPoolUsedForTimestamp = false;
    bool allocOnDevice = false;

    CommandList *eventPoolCommandList = nullptr;

  protected:
    NEO::MultiGraphicsAllocation *eventPoolAllocations = nullptr;
};

struct EventPoolImp : public EventPool {
    EventPoolImp(DriverHandle *driver, uint32_t numDevices, ze_device_handle_t *phDevices, uint32_t numEvents, ze_event_pool_flags_t flags) : numEvents(numEvents) {
        if (flags & ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP) {
            isEventPoolUsedForTimestamp = true;
        }

        if (!(flags & ZE_EVENT_POOL_FLAG_HOST_VISIBLE)) {
            allocOnDevice = true;
        }
    }

    ze_result_t initialize(DriverHandle *driver,
                           uint32_t numDevices,
                           ze_device_handle_t *phDevices,
                           uint32_t numEvents);

    ~EventPoolImp();

    ze_result_t destroy() override;

    ze_result_t getIpcHandle(ze_ipc_event_pool_handle_t *pIpcHandle) override;

    ze_result_t closeIpcHandle() override;

    ze_result_t createEvent(const ze_event_desc_t *desc, ze_event_handle_t *phEvent) override;

    size_t getEventSize() override { return eventSize; }
    size_t getNumEvents() { return numEvents; }

    Device *getDevice() override { return devices[0]; }

    std::vector<Device *> devices;
    size_t numEvents;

  protected:
    const uint32_t eventSize = static_cast<uint32_t>(alignUp(NEO::TimestampPacketSizeControl::preferredPacketCount * sizeof(struct TimestampPacketStorage::Packet),
                                                             MemoryConstants::cacheLineSize));
    const uint32_t eventAlignment = MemoryConstants::cacheLineSize;
};

} // namespace L0
