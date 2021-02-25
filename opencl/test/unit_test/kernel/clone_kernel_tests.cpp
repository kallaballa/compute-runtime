/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/memory_manager/unified_memory_manager.h"
#include "shared/test/unit_test/utilities/base_object_utils.h"

#include "opencl/source/accelerators/intel_accelerator.h"
#include "opencl/source/accelerators/intel_motion_estimation.h"
#include "opencl/source/helpers/sampler_helpers.h"
#include "opencl/source/kernel/kernel.h"
#include "opencl/source/mem_obj/pipe.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"
#include "opencl/test/unit_test/fixtures/image_fixture.h"
#include "opencl/test/unit_test/mocks/mock_buffer.h"
#include "opencl/test/unit_test/mocks/mock_device_queue.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_pipe.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "opencl/test/unit_test/mocks/mock_sampler.h"
#include "opencl/test/unit_test/test_macros/test_checks_ocl.h"
#include "test.h"

#include "CL/cl.h"
#include "gtest/gtest.h"

#include <memory>

using namespace NEO;

class CloneKernelFixture : public ContextFixture, public ClDeviceFixture {
    using ContextFixture::SetUp;

  public:
    CloneKernelFixture() {
    }

  protected:
    void SetUp() {
        ClDeviceFixture::SetUp();
        cl_device_id device = pClDevice;
        ContextFixture::SetUp(1, &device);

        // define kernel info
        pKernelInfo = std::make_unique<KernelInfo>();
        pKernelInfo->kernelDescriptor.kernelAttributes.simdSize = 1;

        // setup kernel arg offsets
        KernelArgPatchInfo kernelArgPatchInfo;

        pKernelInfo->heapInfo.SurfaceStateHeapSize = sizeof(surfaceStateHeap);
        pKernelInfo->heapInfo.pSsh = surfaceStateHeap;
        pKernelInfo->usesSsh = true;
        pKernelInfo->requiresSshForBuffers = true;

        pKernelInfo->kernelArgInfo.resize(1);
        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);

        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset = 0x20;
        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size = (uint32_t)sizeof(void *);

        pKernelInfo->kernelArgInfo[0].offsetHeap = 0x20;
        pKernelInfo->kernelArgInfo[0].offsetObjectId = 0x0;

        // image
        pKernelInfo->kernelArgInfo[0].offsetImgWidth = 0x4;
        pKernelInfo->kernelArgInfo[0].offsetImgHeight = 0x8;
        pKernelInfo->kernelArgInfo[0].offsetImgDepth = 0xc;

        // sampler
        pKernelInfo->kernelArgInfo[0].offsetSamplerSnapWa = 0x4;
        pKernelInfo->kernelArgInfo[0].offsetSamplerAddressingMode = 0x8;
        pKernelInfo->kernelArgInfo[0].offsetSamplerNormalizedCoords = 0x10;

        // accelerator
        pKernelInfo->kernelArgInfo[0].samplerArgumentType = iOpenCL::SAMPLER_OBJECT_VME;
        pKernelInfo->kernelArgInfo[0].offsetVmeMbBlockType = 0x4;
        pKernelInfo->kernelArgInfo[0].offsetVmeSubpixelMode = 0xc;
        pKernelInfo->kernelArgInfo[0].offsetVmeSadAdjustMode = 0x14;
        pKernelInfo->kernelArgInfo[0].offsetVmeSearchPathType = 0x1c;

        pProgram = new MockProgram(pContext, false, toClDeviceVector(*pClDevice));

        pSourceKernel = new MockKernel(pProgram, MockKernel::toKernelInfoContainer(*pKernelInfo, rootDeviceIndex));
        ASSERT_EQ(CL_SUCCESS, pSourceKernel->initialize());
        char pSourceCrossThreadData[64] = {};
        pSourceKernel->setCrossThreadData(pSourceCrossThreadData, sizeof(pSourceCrossThreadData));

        pClonedKernel = new MockKernel(pProgram, MockKernel::toKernelInfoContainer(*pKernelInfo, rootDeviceIndex));
        ASSERT_EQ(CL_SUCCESS, pClonedKernel->initialize());
        char pClonedCrossThreadData[64] = {};
        pClonedKernel->setCrossThreadData(pClonedCrossThreadData, sizeof(pClonedCrossThreadData));
    }

    void TearDown() override {
        delete pSourceKernel;
        delete pClonedKernel;

        delete pProgram;
        ContextFixture::TearDown();
        ClDeviceFixture::TearDown();
    }

    cl_int retVal = CL_SUCCESS;
    MockProgram *pProgram = nullptr;
    MockKernel *pSourceKernel = nullptr;
    MockKernel *pClonedKernel = nullptr;
    std::unique_ptr<KernelInfo> pKernelInfo;
    SKernelBinaryHeaderCommon kernelHeader;
    char surfaceStateHeap[128];
};

typedef Test<CloneKernelFixture> CloneKernelTest;

TEST_F(CloneKernelTest, GivenUnsetArgWhenCloningKernelThenKernelInfoIsCorrect) {
    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::NONE_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(nullptr, pSourceKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(nullptr, pSourceKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(0u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_FALSE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);
}

TEST_F(CloneKernelTest, GivenArgLocalWhenCloningKernelThenKernelInfoIsCorrect) {
    const size_t slmSize = 0x800;

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgLocal);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgLocal);

    retVal = pSourceKernel->setArg(0, slmSize, nullptr);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::SLM_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    EXPECT_EQ(alignUp(slmSize, 1024), pClonedKernel->kernelDeviceInfos[rootDeviceIndex].slmTotalSize);
}

TEST_F(CloneKernelTest, GivenArgBufferWhenCloningKernelThenKernelInfoIsCorrect) {
    MockBuffer buffer;
    cl_mem memObj = &buffer;

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgBuffer);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgBuffer);

    retVal = pSourceKernel->setArg(0, sizeof(cl_mem), &memObj);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::BUFFER_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto pKernelArg = (cl_mem *)(pClonedKernel->getCrossThreadData(rootDeviceIndex) +
                                 pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(buffer.getCpuAddress(), *pKernelArg);
}

TEST_F(CloneKernelTest, GivenArgPipeWhenCloningKernelThenKernelInfoIsCorrect) {
    MockPipe pipe(pContext);
    cl_mem memObj = &pipe;

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgPipe);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgPipe);

    retVal = pSourceKernel->setArg(0, sizeof(cl_mem), &memObj);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::PIPE_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto pKernelArg = (cl_mem *)(pClonedKernel->getCrossThreadData(rootDeviceIndex) +
                                 pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(pipe.getCpuAddress(), *pKernelArg);
}

TEST_F(CloneKernelTest, GivenArgImageWhenCloningKernelThenKernelInfoIsCorrect) {
    auto image = std::unique_ptr<Image>(Image2dHelper<>::create(pContext));
    ASSERT_NE(nullptr, image);

    uint32_t objectId = pKernelInfo->kernelArgInfo[0].offsetHeap;
    size_t imageWidth = image->getImageDesc().image_width;
    size_t imageHeight = image->getImageDesc().image_height;
    size_t imageDepth = image->getImageDesc().image_depth;

    cl_mem memObj = image.get();

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgImage);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgImage);

    retVal = pSourceKernel->setArg(0, sizeof(cl_mem), &memObj);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::IMAGE_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto crossThreadData = reinterpret_cast<uint32_t *>(pClonedKernel->getCrossThreadData(rootDeviceIndex));
    EXPECT_EQ(objectId, *crossThreadData);

    const auto &argInfo = pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0];

    auto pImgWidth = ptrOffset(crossThreadData, argInfo.offsetImgWidth);
    EXPECT_EQ(imageWidth, *pImgWidth);

    auto pImgHeight = ptrOffset(crossThreadData, argInfo.offsetImgHeight);
    EXPECT_EQ(imageHeight, *pImgHeight);

    auto pImgDepth = ptrOffset(crossThreadData, argInfo.offsetImgDepth);
    EXPECT_EQ(imageDepth, *pImgDepth);
}

TEST_F(CloneKernelTest, GivenArgAcceleratorWhenCloningKernelThenKernelInfoIsCorrect) {
    cl_motion_estimation_desc_intel desc = {
        CL_ME_MB_TYPE_4x4_INTEL,
        CL_ME_SUBPIXEL_MODE_QPEL_INTEL,
        CL_ME_SAD_ADJUST_MODE_HAAR_INTEL,
        CL_ME_SEARCH_PATH_RADIUS_16_12_INTEL};

    cl_accelerator_intel accelerator = VmeAccelerator::create(
        pContext,
        CL_ACCELERATOR_TYPE_MOTION_ESTIMATION_INTEL, sizeof(desc), &desc,
        retVal);
    ASSERT_EQ(CL_SUCCESS, retVal);
    ASSERT_NE(nullptr, accelerator);

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgAccelerator);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgAccelerator);

    retVal = pSourceKernel->setArg(0, sizeof(cl_accelerator_intel), &accelerator);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::ACCELERATOR_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto crossThreadData = reinterpret_cast<uint32_t *>(pClonedKernel->getCrossThreadData(rootDeviceIndex));

    const auto &argInfo = pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0];

    uint32_t *pMbBlockType = ptrOffset(crossThreadData, argInfo.offsetVmeMbBlockType);
    EXPECT_EQ(desc.mb_block_type, *pMbBlockType);

    uint32_t *pSubpixelMode = ptrOffset(crossThreadData, argInfo.offsetVmeSubpixelMode);
    EXPECT_EQ(desc.subpixel_mode, *pSubpixelMode);

    uint32_t *pSadAdjustMode = ptrOffset(crossThreadData, argInfo.offsetVmeSadAdjustMode);
    EXPECT_EQ(desc.sad_adjust_mode, *pSadAdjustMode);

    uint32_t *pSearchPathType = ptrOffset(crossThreadData, argInfo.offsetVmeSearchPathType);
    EXPECT_EQ(desc.search_path_type, *pSearchPathType);

    retVal = clReleaseAcceleratorINTEL(accelerator);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(CloneKernelTest, GivenArgSamplerWhenCloningKernelThenKernelInfoIsCorrect) {
    auto sampler = clUniquePtr<Sampler>(new MockSampler(pContext,
                                                        true,
                                                        (cl_addressing_mode)CL_ADDRESS_MIRRORED_REPEAT,
                                                        (cl_filter_mode)CL_FILTER_NEAREST));

    uint32_t objectId = SAMPLER_OBJECT_ID_SHIFT + pKernelInfo->kernelArgInfo[0].offsetHeap;

    cl_sampler samplerObj = sampler.get();

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgSampler);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgSampler);

    retVal = pSourceKernel->setArg(0, sizeof(cl_sampler), &samplerObj);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::SAMPLER_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto crossThreadData = reinterpret_cast<uint32_t *>(pClonedKernel->getCrossThreadData(rootDeviceIndex));
    EXPECT_EQ(objectId, *crossThreadData);

    const auto &argInfo = pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0];

    auto pSnapWa = ptrOffset(crossThreadData, argInfo.offsetSamplerSnapWa);
    EXPECT_EQ(sampler->getSnapWaValue(), *pSnapWa);

    auto pAddressingMode = ptrOffset(crossThreadData, argInfo.offsetSamplerAddressingMode);
    EXPECT_EQ(GetAddrModeEnum(sampler->addressingMode), *pAddressingMode);

    auto pNormalizedCoords = ptrOffset(crossThreadData, argInfo.offsetSamplerNormalizedCoords);
    EXPECT_EQ(GetNormCoordsEnum(sampler->normalizedCoordinates), *pNormalizedCoords);

    EXPECT_EQ(3, sampler->getRefInternalCount());
}

HWCMDTEST_F(IGFX_GEN8_CORE, CloneKernelTest, GivenArgDeviceQueueWhenCloningKernelThenKernelInfoIsCorrect) {
    REQUIRE_DEVICE_ENQUEUE_OR_SKIP(pClDevice);

    cl_queue_properties queueProps[5] = {
        CL_QUEUE_PROPERTIES,
        CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
        0, 0, 0};

    MockDeviceQueueHw<FamilyType> mockDevQueue(pContext, pClDevice, queueProps[0]);
    auto clDeviceQueue = static_cast<cl_command_queue>(&mockDevQueue);

    pSourceKernel->setKernelArgHandler(0, &Kernel::setArgDevQueue);
    pClonedKernel->setKernelArgHandler(0, &Kernel::setArgDevQueue);

    retVal = pSourceKernel->setArg(0, sizeof(cl_command_queue), &clDeviceQueue);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::DEVICE_QUEUE_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto pKernelArg = (uintptr_t *)(pClonedKernel->getCrossThreadData(rootDeviceIndex) +
                                    pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(static_cast<uintptr_t>(mockDevQueue.getQueueBuffer()->getGpuAddressToPatch()), *pKernelArg);
}

TEST_F(CloneKernelTest, GivenArgSvmWhenCloningKernelThenKernelInfoIsCorrect) {
    char *svmPtr = new char[256];

    retVal = pSourceKernel->setArgSvm(0, 256, svmPtr, nullptr, 0u);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::SVM_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto pKernelArg = (void **)(pClonedKernel->getCrossThreadData(rootDeviceIndex) +
                                pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(svmPtr, *pKernelArg);

    delete[] svmPtr;
}

TEST_F(CloneKernelTest, GivenArgSvmAllocWhenCloningKernelThenKernelInfoIsCorrect) {
    char *svmPtr = new char[256];
    MockGraphicsAllocation svmAlloc(svmPtr, 256);

    retVal = pSourceKernel->setArgSvmAlloc(0, svmPtr, &svmAlloc);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::SVM_ALLOC_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto pKernelArg = (void **)(pClonedKernel->getCrossThreadData(rootDeviceIndex) +
                                pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(svmPtr, *pKernelArg);

    delete[] svmPtr;
}

TEST_F(CloneKernelTest, GivenArgImmediateWhenCloningKernelThenKernelInfoIsCorrect) {
    using TypeParam = unsigned long;
    auto value = (TypeParam)0xAA55AA55UL;

    retVal = pSourceKernel->setArg(0, sizeof(TypeParam), &value);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(1u, pSourceKernel->getKernelArguments().size());
    EXPECT_EQ(Kernel::NONE_OBJ, pSourceKernel->getKernelArgInfo(0).type);
    EXPECT_NE(0u, pSourceKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(1u, pSourceKernel->getPatchedArgumentsNum());
    EXPECT_TRUE(pSourceKernel->getKernelArgInfo(0).isPatched);

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->getKernelArguments().size(), pClonedKernel->getKernelArguments().size());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).type, pClonedKernel->getKernelArgInfo(0).type);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).object, pClonedKernel->getKernelArgInfo(0).object);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).value, pClonedKernel->getKernelArgInfo(0).value);
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).size, pClonedKernel->getKernelArgInfo(0).size);
    EXPECT_EQ(pSourceKernel->getPatchedArgumentsNum(), pClonedKernel->getPatchedArgumentsNum());
    EXPECT_EQ(pSourceKernel->getKernelArgInfo(0).isPatched, pClonedKernel->getKernelArgInfo(0).isPatched);

    auto pKernelArg = (TypeParam *)(pClonedKernel->getCrossThreadData(rootDeviceIndex) +
                                    pClonedKernel->getKernelInfo(rootDeviceIndex).kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(value, *pKernelArg);
}

TEST_F(CloneKernelTest, GivenExecInfoWhenCloningKernelThenSvmAllocationIsCorrect) {
    REQUIRE_SVM_OR_SKIP(pDevice);
    void *ptrSVM = pContext->getSVMAllocsManager()->createSVMAlloc(256, {}, pContext->getRootDeviceIndices(), pContext->getDeviceBitfields());
    ASSERT_NE(nullptr, ptrSVM);

    auto svmData = pContext->getSVMAllocsManager()->getSVMAlloc(ptrSVM);
    ASSERT_NE(nullptr, svmData);
    GraphicsAllocation *pSvmAlloc = svmData->gpuAllocations.getGraphicsAllocation(pDevice->getRootDeviceIndex());
    ASSERT_NE(nullptr, pSvmAlloc);

    pSourceKernel->setSvmKernelExecInfo(pSvmAlloc);

    EXPECT_EQ(1u, pSourceKernel->kernelSvmGfxAllocations.size());

    retVal = pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(pSourceKernel->kernelSvmGfxAllocations.size(), pClonedKernel->kernelSvmGfxAllocations.size());
    EXPECT_EQ(pSourceKernel->kernelSvmGfxAllocations.at(0), pClonedKernel->kernelSvmGfxAllocations.at(0));

    pContext->getSVMAllocsManager()->freeSVMAlloc(ptrSVM);
}

TEST_F(CloneKernelTest, givenBuiltinSourceKernelWhenCloningThenSetBuiltinFlagToClonedKernel) {
    pSourceKernel->isBuiltIn = true;
    pClonedKernel->cloneKernel(pSourceKernel);
    EXPECT_TRUE(pClonedKernel->isBuiltIn);
}
