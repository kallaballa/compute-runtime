/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/mocks/mock_device.h"

#include "opencl/test/unit_test/mocks/mock_cl_device.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"

#include "gtest/gtest.h"

using namespace NEO;

class PatchedKernelTest : public ::testing::Test {
  public:
    void SetUp() override {
        device = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(defaultHwInfo.get(), rootDeviceIndex));
        context.reset(new MockContext(device.get()));
        program.reset(Program::createBuiltInFromSource<MockProgram>("FillBufferBytes", context.get(), context->getDevices(), &retVal));
        EXPECT_EQ(CL_SUCCESS, retVal);
        program->build(program->getDevices(), nullptr, false);
        kernel.reset(Kernel::create(program.get(), program->getKernelInfosForKernel("FillBufferBytes"), &retVal));
        EXPECT_EQ(CL_SUCCESS, retVal);
    }
    void TearDown() override {
        context.reset();
    }

    const uint32_t rootDeviceIndex = 0u;
    std::unique_ptr<MockContext> context;
    std::unique_ptr<MockClDevice> device;
    std::unique_ptr<MockProgram> program;
    std::unique_ptr<Kernel> kernel;
    cl_int retVal = CL_SUCCESS;
};

TEST_F(PatchedKernelTest, givenKernelWithoutPatchedArgsWhenIsPatchedIsCalledThenReturnsFalse) {
    EXPECT_FALSE(kernel->isPatched());
}

TEST_F(PatchedKernelTest, givenKernelWithAllArgsSetWithBufferWhenIsPatchedIsCalledThenReturnsTrue) {
    auto buffer = clCreateBuffer(context.get(), CL_MEM_READ_ONLY, sizeof(int), nullptr, &retVal);
    EXPECT_EQ(CL_SUCCESS, retVal);
    auto argsNum = kernel->getKernelArgsNumber();
    for (uint32_t i = 0; i < argsNum; i++) {
        kernel->setArg(i, buffer);
    }
    EXPECT_TRUE(kernel->isPatched());
    clReleaseMemObject(buffer);
}

TEST_F(PatchedKernelTest, givenKernelWithoutAllArgsSetWhenIsPatchedIsCalledThenReturnsFalse) {
    auto buffer = clCreateBuffer(context.get(), CL_MEM_READ_ONLY, sizeof(int), nullptr, &retVal);
    EXPECT_EQ(CL_SUCCESS, retVal);
    auto argsNum = kernel->getKernelArgsNumber();
    for (uint32_t i = 0; i < argsNum; i++) {
        kernel->setArg(0, buffer);
    }
    EXPECT_FALSE(kernel->isPatched());
    clReleaseMemObject(buffer);
}

TEST_F(PatchedKernelTest, givenKernelWithAllArgsSetWithSvmAllocWhenIsPatchedIsCalledThenReturnsTrue) {
    auto argsNum = kernel->getKernelArgsNumber();
    for (uint32_t i = 0; i < argsNum; i++) {
        kernel->setArgSvmAlloc(0, nullptr, nullptr);
    }
    EXPECT_FALSE(kernel->isPatched());
    for (uint32_t i = 0; i < argsNum; i++) {
        kernel->setArgSvmAlloc(i, nullptr, nullptr);
    }
    EXPECT_TRUE(kernel->isPatched());
}

TEST_F(PatchedKernelTest, givenKernelWithAllArgsSetWithSvmWhenIsPatchedIsCalledThenReturnsTrue) {
    uint32_t size = sizeof(int);
    auto argsNum = kernel->getKernelArgsNumber();
    for (uint32_t i = 0; i < argsNum; i++) {
        kernel->setArgSvm(0, size, nullptr, nullptr, 0u);
    }
    EXPECT_FALSE(kernel->isPatched());
    for (uint32_t i = 0; i < argsNum; i++) {
        kernel->setArgSvm(i, size, nullptr, nullptr, 0u);
    }
    EXPECT_TRUE(kernel->isPatched());
}

TEST_F(PatchedKernelTest, givenKernelWithOneArgumentToPatchWhichIsNonzeroIndexedWhenThatArgumentIsSetThenKernelIsPatched) {
    uint32_t size = sizeof(int);
    MockKernelWithInternals mockKernel(*device.get(), context.get());
    EXPECT_EQ(0u, mockKernel.kernelInfo.argumentsToPatchNum);
    mockKernel.kernelInfo.storeKernelArgPatchInfo(1, 0, 0, 0, 0);
    EXPECT_EQ(1u, mockKernel.kernelInfo.argumentsToPatchNum);
    mockKernel.kernelInfo.storeKernelArgPatchInfo(1, 0, 0, 0, 0);
    EXPECT_EQ(1u, mockKernel.kernelInfo.argumentsToPatchNum);
    kernel.reset(mockKernel.mockKernel);
    kernel->initialize();
    EXPECT_FALSE(kernel->Kernel::isPatched());
    kernel->setArgSvm(1, size, nullptr, nullptr, 0u);
    EXPECT_TRUE(kernel->Kernel::isPatched());
    kernel.release();
}
