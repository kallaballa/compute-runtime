/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "kernel_arg_buffer_fixture.h"

#include "shared/source/helpers/api_specific_config.h"

#include "opencl/source/kernel/kernel.h"
#include "opencl/source/mem_obj/buffer.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"
#include "opencl/test/unit_test/mocks/mock_buffer.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "test.h"

#include "CL/cl.h"
#include "gtest/gtest.h"

#include <memory>

using namespace NEO;

void KernelArgBufferFixture::SetUp() {
    ClDeviceFixture::SetUp();
    cl_device_id device = pClDevice;
    ContextFixture::SetUp(1, &device);

    // define kernel info
    pKernelInfo = std::make_unique<KernelInfo>();
    pKernelInfo->kernelDescriptor.kernelAttributes.simdSize = 1;

    // setup kernel arg offsets
    KernelArgPatchInfo kernelArgPatchInfo;

    pKernelInfo->heapInfo.pSsh = pSshLocal;
    pKernelInfo->heapInfo.SurfaceStateHeapSize = sizeof(pSshLocal);
    pKernelInfo->usesSsh = true;
    pKernelInfo->requiresSshForBuffers = true;

    pKernelInfo->kernelArgInfo.resize(1);
    pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);

    pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset = 0x30;
    pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size = (uint32_t)sizeof(void *);

    pKernelInfo->kernelDescriptor.kernelAttributes.bufferAddressingMode = ApiSpecificConfig::getBindlessConfiguration() ? KernelDescriptor::AddressingMode::BindlessAndStateless : KernelDescriptor::AddressingMode::BindfulAndStateless;

    pProgram = new MockProgram(pContext, false, toClDeviceVector(*pClDevice));

    pKernel = new MockKernel(pProgram, MockKernel::toKernelInfoContainer(*pKernelInfo, rootDeviceIndex));
    ASSERT_EQ(CL_SUCCESS, pKernel->initialize());
    pKernel->setCrossThreadData(pCrossThreadData, sizeof(pCrossThreadData));

    pKernel->setKernelArgHandler(0, &Kernel::setArgBuffer);
}

void KernelArgBufferFixture::TearDown() {
    delete pKernel;

    delete pProgram;
    ContextFixture::TearDown();
    ClDeviceFixture::TearDown();
}
