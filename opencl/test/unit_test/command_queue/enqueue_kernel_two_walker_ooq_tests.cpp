/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/fixtures/hello_world_fixture.h"
#include "opencl/test/unit_test/fixtures/two_walker_fixture.h"
#include "test.h"

using namespace NEO;

struct OOQFixtureFactory : public HelloWorldFixtureFactory {
    typedef OOQueueFixture CommandQueueFixture;
};

typedef TwoWalkerTest<OOQFixtureFactory> OOQWithTwoWalkers;

HWTEST_F(OOQWithTwoWalkers, GivenTwoCommandQueuesWhenEnqueuingKernelThenTwoDifferentWalkersAreCreated) {
    enqueueTwoKernels<FamilyType>();

    EXPECT_NE(itorWalker1, itorWalker2);
}

HWTEST_F(OOQWithTwoWalkers, GivenTwoCommandQueuesWhenEnqueuingKernelThenOnePipelineSelectExists) {
    enqueueTwoKernels<FamilyType>();
    int numCommands = getNumberOfPipelineSelectsThatEnablePipelineSelect<FamilyType>();
    EXPECT_EQ(1, numCommands);
}

HWCMDTEST_F(IGFX_GEN8_CORE, OOQWithTwoWalkers, GivenTwoCommandQueuesWhenEnqueuingKernelThenThereIsOneVfeState) {
    enqueueTwoKernels<FamilyType>();

    auto numCommands = getCommandsList<typename FamilyType::MEDIA_VFE_STATE>().size();
    EXPECT_EQ(1u, numCommands);
}

HWTEST_F(OOQWithTwoWalkers, GivenTwoCommandQueuesWhenEnqueuingKernelThenOnePipeControlIsInsertedBetweenWalkers) {
    enqueueTwoKernels<FamilyType>();

    auto itorCmd = find<typename FamilyType::PIPE_CONTROL *>(itorWalker1, itorWalker2);
    // Workaround for DRM i915 coherency patch
    // EXPECT_EQ(itorWalker2, itorCmd);
    EXPECT_NE(itorWalker2, itorCmd);
}
