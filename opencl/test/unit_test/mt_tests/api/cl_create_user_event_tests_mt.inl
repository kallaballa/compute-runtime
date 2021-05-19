/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/context/context.h"
#include "opencl/test/unit_test/api/cl_api_tests.h"

using namespace NEO;

typedef api_tests clCreateUserEventMtTests;

namespace ULT {

TEST_F(clCreateUserEventMtTests, GivenClCompleteEventWhenWaitingForEventThenWaitForEventsIsCompleted) {
    auto userEvent = clCreateUserEvent(
        pContext,
        &retVal);

    std::atomic<bool> ThreadStarted(false);
    std::atomic<bool> WaitForEventsCompleted(false);
    int counter = 0;
    int Deadline = 2000;
    std::thread t([&]() {
        ThreadStarted = true;
        clWaitForEvents(1, &userEvent);
        WaitForEventsCompleted = true;
    });

    //wait for the thread to start
    while (!ThreadStarted)
        ;
    //now wait a while.
    while (!WaitForEventsCompleted && counter++ < Deadline)
        ;

    ASSERT_EQ(WaitForEventsCompleted, false) << "WaitForEvents returned while user event is not signaled!";

    //set event to CL_COMPLETE
    retVal = clSetUserEventStatus(userEvent, CL_COMPLETE);
    t.join();

    ASSERT_EQ(WaitForEventsCompleted, true);

    retVal = clReleaseEvent(userEvent);
    EXPECT_EQ(CL_SUCCESS, retVal);
}
} // namespace ULT
