/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/command_stream/tbx_command_stream_receiver_hw.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "test.h"

using namespace NEO;

using Gen11TbxCommandStreamReceiverTests = Test<ClDeviceFixture>;

GEN11TEST_F(Gen11TbxCommandStreamReceiverTests, whenAskedForPollForCompletionParametersThenReturnCorrectValues) {
    class MyMockTbxHw : public TbxCommandStreamReceiverHw<FamilyType> {
      public:
        MyMockTbxHw(ExecutionEnvironment &executionEnvironment)
            : TbxCommandStreamReceiverHw<FamilyType>(executionEnvironment, 0, 1) {}
        using TbxCommandStreamReceiverHw<FamilyType>::getpollNotEqualValueForPollForCompletion;
        using TbxCommandStreamReceiverHw<FamilyType>::getMaskAndValueForPollForCompletion;
    };

    MyMockTbxHw myMockTbxHw(*pDevice->executionEnvironment);
    EXPECT_EQ(0x80u, myMockTbxHw.getMaskAndValueForPollForCompletion());
    EXPECT_TRUE(myMockTbxHw.getpollNotEqualValueForPollForCompletion());
}
