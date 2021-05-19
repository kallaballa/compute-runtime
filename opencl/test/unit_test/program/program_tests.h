/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"

#include "gtest/gtest.h"

#include <vector>

class ProgramTests : public NEO::ClDeviceFixture,
                     public ::testing::Test,
                     public NEO::ContextFixture {

    using NEO::ContextFixture::SetUp;

  public:
    void SetUp() override;
    void TearDown() override;
};
