/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/helpers/sampler_helpers.h"

#include "gtest/gtest.h"

TEST(SamplerHelpers, WhenGettingAddrModeEnumsThenCorrectValuesAreReturned) {
    EXPECT_EQ(CLK_ADDRESS_REPEAT, GetAddrModeEnum(CL_ADDRESS_REPEAT));
    EXPECT_EQ(CLK_ADDRESS_CLAMP_TO_EDGE, GetAddrModeEnum(CL_ADDRESS_CLAMP_TO_EDGE));
    EXPECT_EQ(CLK_ADDRESS_CLAMP, GetAddrModeEnum(CL_ADDRESS_CLAMP));
    EXPECT_EQ(CLK_ADDRESS_NONE, GetAddrModeEnum(CL_ADDRESS_NONE));
    EXPECT_EQ(CLK_ADDRESS_MIRRORED_REPEAT, GetAddrModeEnum(CL_ADDRESS_MIRRORED_REPEAT));
}

TEST(SamplerHelpers, WhenGettingNormCoordsEnumsThenCorrectValuesAreReturned) {
    EXPECT_EQ(CLK_NORMALIZED_COORDS_TRUE, GetNormCoordsEnum(true));
    EXPECT_EQ(CLK_NORMALIZED_COORDS_FALSE, GetNormCoordsEnum(false));
}
