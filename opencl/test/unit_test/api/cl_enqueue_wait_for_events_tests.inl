/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/array_count.h"

#include "opencl/source/command_queue/command_queue.h"
#include "opencl/source/context/context.h"
#include "opencl/source/event/event.h"
#include "opencl/source/event/user_event.h"
#include "opencl/test/unit_test/mocks/mock_event.h"

#include "cl_api_tests.h"

using namespace NEO;

using clEnqueueWaitForEventsTests = api_tests;

TEST_F(clEnqueueWaitForEventsTests, GivenInvalidCommandQueueWhenClEnqueueWaitForEventsIsCalledThenReturnError) {
    auto retVal = CL_SUCCESS;

    auto userEvent = clCreateUserEvent(
        pContext,
        &retVal);
    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clEnqueueWaitForEvents(
        nullptr,
        1,
        &userEvent);

    EXPECT_EQ(CL_INVALID_COMMAND_QUEUE, retVal);

    retVal = clReleaseEvent(userEvent);
    ASSERT_EQ(CL_SUCCESS, retVal);
}

TEST_F(clEnqueueWaitForEventsTests, GivenQueueIncapableWhenEnqueingWaitForEventsThenInvalidOperationReturned) {
    MockEvent<Event> events[] = {
        {pCommandQueue, CL_COMMAND_READ_BUFFER, 0, 0},
        {pCommandQueue, CL_COMMAND_READ_BUFFER, 0, 0},
        {pCommandQueue, CL_COMMAND_READ_BUFFER, 0, 0},
    };
    const cl_event waitList[] = {events, events + 1, events + 2};
    const cl_uint waitListSize = static_cast<cl_uint>(arrayCount(waitList));

    auto retVal = clEnqueueWaitForEvents(pCommandQueue, waitListSize, waitList);
    EXPECT_EQ(CL_SUCCESS, retVal);

    this->disableQueueCapabilities(CL_QUEUE_CAPABILITY_SINGLE_QUEUE_EVENT_WAIT_LIST_INTEL);
    retVal = clEnqueueWaitForEvents(pCommandQueue, waitListSize, waitList);
    EXPECT_EQ(CL_INVALID_OPERATION, retVal);
}

TEST_F(clEnqueueWaitForEventsTests, GivenProperParamsWhenClEnqueueWaitForEventsIsCalledAndEventStatusIsCompleteThenWaitAndReturnSuccess) {
    struct MyEvent : public UserEvent {
        MyEvent(Context *context)
            : UserEvent(context) {
        }
        bool wait(bool blocking, bool quickKmdSleep) override {
            wasWaitCalled = true;
            return true;
        };
        bool wasWaitCalled = false;
    };
    auto retVal = CL_SUCCESS;

    auto event = std::make_unique<MyEvent>(pContext);
    cl_event clEvent = static_cast<cl_event>(event.get());
    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clEnqueueWaitForEvents(
        pCommandQueue,
        1,
        &clEvent);
    EXPECT_EQ(true, event->wasWaitCalled);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(clEnqueueWaitForEventsTests, GivenProperParamsWhenClEnqueueWaitForEventsIsCalledAndEventStatusIsNotCompleteThenReturnError) {
    auto retVal = CL_SUCCESS;

    auto userEvent = clCreateUserEvent(
        pContext,
        &retVal);
    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clSetUserEventStatus(userEvent, -1);
    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clEnqueueWaitForEvents(
        pCommandQueue,
        1,
        &userEvent);

    EXPECT_EQ(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST, retVal);

    retVal = clReleaseEvent(userEvent);
    ASSERT_EQ(CL_SUCCESS, retVal);
}

TEST_F(clEnqueueWaitForEventsTests, GivenInvalidEventWhenClEnqueueWaitForEventsIsCalledThenReturnError) {
    auto retVal = CL_SUCCESS;

    auto validUserEvent = clCreateUserEvent(
        pContext,
        &retVal);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto ptr = std::make_unique<char[]>(sizeof(Event));
    cl_event invalidEvent = reinterpret_cast<cl_event>(ptr.get());
    cl_event events[]{validUserEvent, invalidEvent, validUserEvent};

    retVal = clEnqueueWaitForEvents(
        pCommandQueue,
        3,
        events);

    EXPECT_EQ(CL_INVALID_EVENT, retVal);

    retVal = clReleaseEvent(validUserEvent);
    ASSERT_EQ(CL_SUCCESS, retVal);
}
