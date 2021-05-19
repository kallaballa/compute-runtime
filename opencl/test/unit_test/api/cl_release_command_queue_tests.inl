/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/unit_test_helper.h"

#include "opencl/source/context/context.h"
#include "opencl/test/unit_test/fixtures/device_host_queue_fixture.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"

#include "cl_api_tests.h"

#include <type_traits>

using namespace NEO;
namespace DeviceHostQueue {
typedef ::testing::Types<CommandQueue, DeviceQueue> QueueTypes;

template <typename T>
class clReleaseCommandQueueTypeTests : public DeviceHostQueueFixture<T> {};

TYPED_TEST_CASE(clReleaseCommandQueueTypeTests, QueueTypes);

TYPED_TEST(clReleaseCommandQueueTypeTests, GivenValidCmdQueueWhenReleasingCmdQueueThenSucessIsReturned) {
    if (std::is_same<TypeParam, DeviceQueue>::value && !this->pDevice->getHardwareInfo().capabilityTable.supportsDeviceEnqueue) {
        return;
    }

    using BaseType = typename TypeParam::BaseType;

    auto queue = this->createClQueue();
    ASSERT_EQ(CL_SUCCESS, this->retVal);
    auto qObject = castToObject<TypeParam>(static_cast<BaseType *>(queue));
    ASSERT_NE(qObject, nullptr);

    this->retVal = clReleaseCommandQueue(queue);
    EXPECT_EQ(CL_SUCCESS, this->retVal);
}

TEST(clReleaseCommandQueueTypeTests, GivenNullCmdQueueWhenReleasingCmdQueueThenClInvalidCommandQueueErrorIsReturned) {
    auto retVal = clReleaseCommandQueue(nullptr);
    EXPECT_EQ(CL_INVALID_COMMAND_QUEUE, retVal);
}
} // namespace DeviceHostQueue

namespace ULT {

typedef api_tests clReleaseCommandQueueTests;

TEST_F(clReleaseCommandQueueTests, givenBlockedEnqueueWithOutputEventStoredAsVirtualEventWhenReleasingCmdQueueThenInternalRefCountIsDecrementedAndQueueDeleted) {
    cl_command_queue cmdQ = nullptr;
    cl_queue_properties properties = 0;
    ClDevice *device = (ClDevice *)testedClDevice;
    MockKernelWithInternals kernelInternals(*device, pContext);

    cmdQ = clCreateCommandQueue(pContext, testedClDevice, properties, &retVal);

    ASSERT_NE(nullptr, cmdQ);
    ASSERT_EQ(CL_SUCCESS, retVal);

    size_t offset[] = {0, 0, 0};
    size_t gws[] = {1, 1, 1};

    cl_int retVal = CL_SUCCESS;
    cl_int success = CL_SUCCESS;
    cl_event event = clCreateUserEvent(pContext, &retVal);

    cl_event eventOut = nullptr;

    EXPECT_EQ(success, retVal);

    retVal = clEnqueueNDRangeKernel(cmdQ, kernelInternals.mockMultiDeviceKernel, 1, offset, gws, nullptr, 1, &event, &eventOut);

    EXPECT_EQ(success, retVal);
    EXPECT_NE(nullptr, eventOut);

    clSetUserEventStatus(event, CL_COMPLETE);

    clReleaseEvent(event);
    clReleaseEvent(eventOut);

    retVal = clReleaseCommandQueue(cmdQ);
    EXPECT_EQ(success, retVal);
}
} // namespace ULT
