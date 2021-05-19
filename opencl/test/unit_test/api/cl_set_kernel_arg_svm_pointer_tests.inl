/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/memory_manager/unified_memory_manager.h"

#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/test_macros/test_checks_ocl.h"
#include "test.h"

#include "cl_api_tests.h"

using namespace NEO;

class KernelArgSvmFixture : public ApiFixture<> {
  protected:
    void SetUp() override {
        ApiFixture::SetUp();
        REQUIRE_SVM_OR_SKIP(defaultHwInfo);

        pKernelInfo = std::make_unique<MockKernelInfo>();
        pKernelInfo->kernelDescriptor.kernelAttributes.simdSize = 1;

        pKernelInfo->heapInfo.SurfaceStateHeapSize = sizeof(pSshLocal);
        pKernelInfo->heapInfo.pSsh = pSshLocal;

        pKernelInfo->addArgBuffer(0, 0x30, sizeof(void *));
        pKernelInfo->setAddressQualifier(0, KernelArgMetadata::AddrGlobal);

        pMockMultiDeviceKernel = MultiDeviceKernel::create<MockKernel>(pProgram, MockKernel::toKernelInfoContainer(*pKernelInfo, testedRootDeviceIndex), nullptr);
        pMockKernel = static_cast<MockKernel *>(pMockMultiDeviceKernel->getKernel(testedRootDeviceIndex));
        ASSERT_NE(nullptr, pMockKernel);
        pMockKernel->setCrossThreadData(pCrossThreadData, sizeof(pCrossThreadData));
    }

    void TearDown() override {
        if (pMockMultiDeviceKernel) {
            delete pMockMultiDeviceKernel;
        }

        ApiFixture::TearDown();
    }

    cl_int retVal = CL_SUCCESS;
    MockKernel *pMockKernel = nullptr;
    MultiDeviceKernel *pMockMultiDeviceKernel = nullptr;
    std::unique_ptr<MockKernelInfo> pKernelInfo;
    char pSshLocal[64]{};
    char pCrossThreadData[64]{};
};

typedef Test<KernelArgSvmFixture> clSetKernelArgSVMPointerTests;

namespace ULT {

TEST_F(clSetKernelArgSVMPointerTests, GivenNullKernelWhenSettingKernelArgThenInvalidKernelErrorIsReturned) {
    auto retVal = clSetKernelArgSVMPointer(
        nullptr, // cl_kernel kernel
        0,       // cl_uint arg_index
        nullptr  // const void *arg_value
    );
    EXPECT_EQ(CL_INVALID_KERNEL, retVal);
}

TEST_F(clSetKernelArgSVMPointerTests, GivenInvalidArgIndexWhenSettingKernelArgThenInvalidArgIndexErrorIsReturned) {
    auto retVal = clSetKernelArgSVMPointer(
        pMockMultiDeviceKernel, // cl_kernel kernel
        (cl_uint)-1,            // cl_uint arg_index
        nullptr                 // const void *arg_value
    );
    EXPECT_EQ(CL_INVALID_ARG_INDEX, retVal);
}

TEST_F(clSetKernelArgSVMPointerTests, GivenDeviceNotSupportingSvmWhenSettingKernelArgSVMPointerThenInvalidOperationErrorIsReturned) {
    auto hwInfo = executionEnvironment->rootDeviceEnvironments[ApiFixture::testedRootDeviceIndex]->getMutableHardwareInfo();
    hwInfo->capabilityTable.ftrSvm = false;

    std::unique_ptr<MultiDeviceKernel> pMultiDeviceKernel(
        MultiDeviceKernel::create<MockKernel>(pProgram, MockKernel::toKernelInfoContainer(*pKernelInfo, testedRootDeviceIndex), nullptr));

    auto retVal = clSetKernelArgSVMPointer(
        pMultiDeviceKernel.get(), // cl_kernel kernel
        (cl_uint)-1,              // cl_uint arg_index
        nullptr                   // const void *arg_value
    );
    EXPECT_EQ(CL_INVALID_OPERATION, retVal);
}

TEST_F(clSetKernelArgSVMPointerTests, GivenLocalAddressAndNullArgValueWhenSettingKernelArgThenInvalidArgValueErrorIsReturned) {
    pKernelInfo->setAddressQualifier(0, KernelArgMetadata::AddrLocal);

    auto retVal = clSetKernelArgSVMPointer(
        pMockMultiDeviceKernel, // cl_kernel kernel
        0,                      // cl_uint arg_index
        nullptr                 // const void *arg_value
    );
    EXPECT_EQ(CL_INVALID_ARG_VALUE, retVal);
}

TEST_F(clSetKernelArgSVMPointerTests, GivenInvalidArgValueWhenSettingKernelArgThenInvalidArgValueErrorIsReturned) {
    pDevice->deviceInfo.sharedSystemMemCapabilities = 0u;
    pDevice->sharedDeviceInfo.sharedSystemAllocationsSupport = false;
    void *ptrHost = malloc(256);
    EXPECT_NE(nullptr, ptrHost);

    auto retVal = clSetKernelArgSVMPointer(
        pMockMultiDeviceKernel, // cl_kernel kernel
        0,                      // cl_uint arg_index
        ptrHost                 // const void *arg_value
    );
    EXPECT_EQ(CL_INVALID_ARG_VALUE, retVal);

    free(ptrHost);
}

TEST_F(clSetKernelArgSVMPointerTests, GivenSvmAndNullArgValueWhenSettingKernelArgThenSuccessIsReturned) {
    const ClDeviceInfo &devInfo = pDevice->getDeviceInfo();
    if (devInfo.svmCapabilities != 0) {
        auto retVal = clSetKernelArgSVMPointer(
            pMockMultiDeviceKernel, // cl_kernel kernel
            0,                      // cl_uint arg_index
            nullptr                 // const void *arg_value
        );
        EXPECT_EQ(CL_SUCCESS, retVal);
    }
}

TEST_F(clSetKernelArgSVMPointerTests, GivenSvmAndValidArgValueWhenSettingKernelArgThenSuccessIsReturned) {
    const ClDeviceInfo &devInfo = pDevice->getDeviceInfo();
    if (devInfo.svmCapabilities != 0) {
        void *ptrSvm = clSVMAlloc(pContext, CL_MEM_READ_WRITE, 256, 4);
        EXPECT_NE(nullptr, ptrSvm);

        auto retVal = clSetKernelArgSVMPointer(
            pMockMultiDeviceKernel, // cl_kernel kernel
            0,                      // cl_uint arg_index
            ptrSvm                  // const void *arg_value
        );
        EXPECT_EQ(CL_SUCCESS, retVal);

        clSVMFree(pContext, ptrSvm);
    }
}

TEST_F(clSetKernelArgSVMPointerTests, GivenSvmAndConstantAddressWhenSettingKernelArgThenSuccessIsReturned) {
    const ClDeviceInfo &devInfo = pDevice->getDeviceInfo();
    if (devInfo.svmCapabilities != 0) {
        void *ptrSvm = clSVMAlloc(pContext, CL_MEM_READ_WRITE, 256, 4);
        EXPECT_NE(nullptr, ptrSvm);

        pKernelInfo->setAddressQualifier(0, KernelArgMetadata::AddrConstant);

        auto retVal = clSetKernelArgSVMPointer(
            pMockMultiDeviceKernel, // cl_kernel kernel
            0,                      // cl_uint arg_index
            ptrSvm                  // const void *arg_value
        );
        EXPECT_EQ(CL_SUCCESS, retVal);

        clSVMFree(pContext, ptrSvm);
    }
}

TEST_F(clSetKernelArgSVMPointerTests, GivenSvmAndPointerWithOffsetWhenSettingKernelArgThenSuccessIsReturned) {
    const ClDeviceInfo &devInfo = pDevice->getDeviceInfo();
    if (devInfo.svmCapabilities != 0) {
        void *ptrSvm = clSVMAlloc(pContext, CL_MEM_READ_WRITE, 256, 4);
        size_t offset = 256 / 2;
        EXPECT_NE(nullptr, ptrSvm);

        auto retVal = clSetKernelArgSVMPointer(
            pMockMultiDeviceKernel, // cl_kernel kernel
            0,                      // cl_uint arg_index
            (char *)ptrSvm + offset // const void *arg_value
        );
        EXPECT_EQ(CL_SUCCESS, retVal);

        clSVMFree(pContext, ptrSvm);
    }
}

TEST_F(clSetKernelArgSVMPointerTests, GivenSvmAndPointerWithInvalidOffsetWhenSettingKernelArgThenInvalidArgValueErrorIsReturned) {
    pDevice->deviceInfo.sharedSystemMemCapabilities = 0u;
    pDevice->sharedDeviceInfo.sharedSystemAllocationsSupport = false;
    const ClDeviceInfo &devInfo = pDevice->getDeviceInfo();
    if (devInfo.svmCapabilities != 0) {
        void *ptrSvm = clSVMAlloc(pContext, CL_MEM_READ_WRITE, 256, 4);
        auto svmData = pContext->getSVMAllocsManager()->getSVMAlloc(ptrSvm);
        ASSERT_NE(nullptr, svmData);
        auto svmAlloc = svmData->gpuAllocations.getGraphicsAllocation(pContext->getDevice(0)->getRootDeviceIndex());
        EXPECT_NE(nullptr, svmAlloc);

        size_t offset = svmAlloc->getUnderlyingBufferSize() + 1;

        auto retVal = clSetKernelArgSVMPointer(
            pMockMultiDeviceKernel, // cl_kernel kernel
            0,                      // cl_uint arg_index
            (char *)ptrSvm + offset // const void *arg_value
        );
        EXPECT_EQ(CL_INVALID_ARG_VALUE, retVal);

        clSVMFree(pContext, ptrSvm);
    }
}
} // namespace ULT
