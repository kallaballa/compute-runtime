/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/sampler/sampler.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "test.h"

#include <memory>

using namespace NEO;

typedef Test<ClDeviceFixture> Gen9SamplerTest;

GEN9TEST_F(Gen9SamplerTest, WhenAppendingSamplerStateParamsThenStateIsNotChanged) {
    typedef typename FamilyType::SAMPLER_STATE SAMPLER_STATE;
    std::unique_ptr<MockContext> context(new MockContext());
    std::unique_ptr<SamplerHw<FamilyType>> sampler(new SamplerHw<FamilyType>(context.get(), CL_FALSE, CL_ADDRESS_NONE, CL_FILTER_NEAREST));
    auto stateWithoutAppendedParams = FamilyType::cmdInitSamplerState;
    auto stateWithAppendedParams = FamilyType::cmdInitSamplerState;
    EXPECT_TRUE(memcmp(&stateWithoutAppendedParams, &stateWithAppendedParams, sizeof(SAMPLER_STATE)) == 0);
    sampler->appendSamplerStateParams(&stateWithAppendedParams, *defaultHwInfo);
    EXPECT_TRUE(memcmp(&stateWithoutAppendedParams, &stateWithAppendedParams, sizeof(SAMPLER_STATE)) == 0);
}
