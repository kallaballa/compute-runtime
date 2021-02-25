/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "test.h"

using namespace NEO;

typedef Test<ClDeviceFixture> Gen9OnlyTest;

GEN9TEST_F(Gen9OnlyTest, WhenGettingRenderCoreFamilyThenGen9CoreIsReturned) {
    EXPECT_EQ(IGFX_GEN9_CORE, pDevice->getRenderCoreFamily());
}
