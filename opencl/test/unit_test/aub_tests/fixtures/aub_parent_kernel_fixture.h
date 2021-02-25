/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/test/unit_test/aub_tests/command_queue/command_enqueue_fixture.h"
#include "opencl/test/unit_test/fixtures/hello_world_kernel_fixture.h"
#include "opencl/test/unit_test/test_macros/test_checks_ocl.h"

namespace NEO {
static const char programFile[] = "simple_block_kernel";
static const char kernelName[] = "kernel_reflection";

class AUBParentKernelFixture : public CommandEnqueueAUBFixture,
                               public HelloWorldKernelFixture,
                               public testing::Test {
  public:
    using HelloWorldKernelFixture::SetUp;

    void SetUp() override {
        REQUIRE_OCL_21_OR_SKIP(defaultHwInfo);
        CommandEnqueueAUBFixture::SetUp();
        ASSERT_NE(nullptr, pClDevice);
        HelloWorldKernelFixture::SetUp(pClDevice, programFile, kernelName, "-cl-std=CL2.0");
    }
    void TearDown() override {
        if (IsSkipped()) {
            return;
        }
        HelloWorldKernelFixture::TearDown();
        CommandEnqueueAUBFixture::TearDown();
    }
};
} // namespace NEO
