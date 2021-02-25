/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/command_stream_receiver.h"

#include "opencl/source/event/user_event.h"
#include "opencl/test/unit_test/command_queue/command_queue_fixture.h"
#include "opencl/test/unit_test/command_stream/command_stream_fixture.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/mocks/mock_buffer.h"
#include "test.h"

using namespace NEO;

class MigrateMemObjectsFixture
    : public ClDeviceFixture,
      public CommandQueueHwFixture {
  public:
    void SetUp() override {
        ClDeviceFixture::SetUp();
        CommandQueueHwFixture::SetUp(pClDevice, 0);
        ASSERT_NE(nullptr, pCmdQ);
    }

    void TearDown() override {
        CommandQueueHwFixture::TearDown();
        ClDeviceFixture::TearDown();
    }
};

typedef Test<MigrateMemObjectsFixture> MigrateMemObjectsTest;

TEST_F(MigrateMemObjectsTest, GivenNullEventWhenMigratingEventsThenSuccessIsReturned) {

    MockBuffer buffer;
    auto bufferMemObj = static_cast<cl_mem>(&buffer);
    auto pBufferMemObj = &bufferMemObj;

    auto retVal = pCmdQ->enqueueMigrateMemObjects(
        1,
        pBufferMemObj,
        CL_MIGRATE_MEM_OBJECT_HOST,
        0,
        nullptr,
        nullptr);

    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(MigrateMemObjectsTest, GivenValidEventListWhenMigratingEventsThenSuccessIsReturned) {

    MockBuffer buffer;
    auto bufferMemObj = static_cast<cl_mem>(&buffer);
    auto pBufferMemObj = &bufferMemObj;

    UserEvent uEvent;
    cl_event eventWaitList[] = {&uEvent};

    auto retVal = pCmdQ->enqueueMigrateMemObjects(
        1,
        pBufferMemObj,
        CL_MIGRATE_MEM_OBJECT_HOST,
        1,
        eventWaitList,
        nullptr);

    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(MigrateMemObjectsTest, GivenEventPointerWhenMigratingEventsThenEventIsReturned) {

    MockBuffer buffer;
    auto bufferMemObj = static_cast<cl_mem>(&buffer);
    auto pBufferMemObj = &bufferMemObj;

    cl_event event = nullptr;

    auto retVal = pCmdQ->enqueueMigrateMemObjects(
        1,
        pBufferMemObj,
        CL_MIGRATE_MEM_OBJECT_HOST,
        0,
        nullptr,
        &event);

    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_NE(nullptr, event);

    Event *eventObject = (Event *)event;
    delete eventObject;
}
