/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "test.h"

#include "level_zero/core/source/driver/driver_handle_imp.h"
#include "level_zero/core/source/event/event.h"
#include "level_zero/core/test/unit_tests/fixtures/device_fixture.h"
#include "level_zero/core/test/unit_tests/mocks/mock_event.h"

namespace L0 {
namespace ult {
struct TimestampEvent : public Test<DeviceFixture> {
  public:
    void SetUp() override {
        DeviceFixture::SetUp();
        ze_event_pool_desc_t eventPoolDesc = {};
        eventPoolDesc.count = 1;
        eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP | ZE_EVENT_POOL_FLAG_HOST_VISIBLE;

        ze_event_desc_t eventDesc = {};
        eventDesc.index = 0;
        eventDesc.signal = 0;
        eventDesc.wait = 0;

        eventPool = std::unique_ptr<L0::EventPool>(L0::EventPool::create(driverHandle.get(), 0, nullptr, &eventPoolDesc));
        ASSERT_NE(nullptr, eventPool);
        event = std::unique_ptr<L0::Event>(L0::Event::create(eventPool.get(), &eventDesc, device));
        ASSERT_NE(nullptr, event);
    }

    void TearDown() override {
        DeviceFixture::TearDown();
    }

    std::unique_ptr<L0::EventPool> eventPool;
    std::unique_ptr<L0::Event> event;
};

GEN12LPTEST_F(TimestampEvent, givenEventTimestampsWhenQueryKernelTimestampThenCorrectDataAreSet) {
    TimestampPacketStorage::Packet data = {};
    data.contextStart = 1u;
    data.contextEnd = 2u;
    data.globalStart = 3u;
    data.globalEnd = 4u;

    event->hostAddress = &data;

    event->packetsInUse = 1;
    ze_kernel_timestamp_result_t result = {};

    event->queryKernelTimestamp(&result);
    EXPECT_EQ(data.globalStart, result.context.kernelStart);
    EXPECT_EQ(data.globalEnd, result.context.kernelEnd);
    EXPECT_EQ(data.globalStart, result.global.kernelStart);
    EXPECT_EQ(data.globalEnd, result.global.kernelEnd);
}

GEN12LPTEST_F(TimestampEvent, givenEventMoreThanOneTimestampsPacketWhenQueryKernelTimestampThenCorrectCalculationAreMade) {
    TimestampPacketStorage::Packet data[3] = {};
    data[0].contextStart = 3u;
    data[0].contextEnd = 4u;
    data[0].globalStart = 5u;
    data[0].globalEnd = 6u;
    data[1].contextStart = 2u;
    data[1].contextEnd = 6u;
    data[1].globalStart = 4u;
    data[1].globalEnd = 8u;
    data[2].contextStart = 4u;
    data[2].contextEnd = 5u;
    data[2].globalStart = 6u;
    data[2].globalEnd = 7u;

    event->hostAddress = &data;
    event->packetsInUse = 3;
    ze_kernel_timestamp_result_t result = {};

    event->queryKernelTimestamp(&result);
    EXPECT_EQ(data[1].globalStart, result.context.kernelStart);
    EXPECT_EQ(data[1].globalEnd, result.context.kernelEnd);
    EXPECT_EQ(data[1].globalStart, result.global.kernelStart);
    EXPECT_EQ(data[1].globalEnd, result.global.kernelEnd);
}
} // namespace ult
} // namespace L0