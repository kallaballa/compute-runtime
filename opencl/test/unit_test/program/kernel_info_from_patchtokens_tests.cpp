/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device_binary_format/patchtokens_decoder.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/unit_test/device_binary_format/patchtokens_tests.h"

#include "opencl/source/program/kernel_info.h"
#include "opencl/source/program/kernel_info_from_patchtokens.h"

#include "gtest/gtest.h"

TEST(KernelInfoFromPatchTokens, GivenValidEmptyKernelFromPatchtokensThenReturnEmptyKernelInfo) {
    std::vector<uint8_t> storage;
    auto src = PatchTokensTestData::ValidEmptyKernel::create(storage);
    NEO::KernelInfo dst = {};
    NEO::populateKernelInfo(dst, src, 4);

    NEO::KernelInfo expectedKernelInfo = {};
    expectedKernelInfo.kernelDescriptor.kernelMetadata.kernelName = std::string(src.name.begin()).c_str();

    EXPECT_STREQ(expectedKernelInfo.kernelDescriptor.kernelMetadata.kernelName.c_str(), dst.kernelDescriptor.kernelMetadata.kernelName.c_str());
    EXPECT_EQ(src.header->KernelHeapSize, dst.heapInfo.KernelHeapSize);
    EXPECT_EQ(src.header->GeneralStateHeapSize, dst.heapInfo.GeneralStateHeapSize);
    EXPECT_EQ(src.header->DynamicStateHeapSize, dst.heapInfo.DynamicStateHeapSize);
    EXPECT_EQ(src.header->SurfaceStateHeapSize, dst.heapInfo.SurfaceStateHeapSize);
    EXPECT_EQ(src.header->KernelUnpaddedSize, dst.heapInfo.KernelUnpaddedSize);
}

TEST(KernelInfoFromPatchTokens, GivenValidKernelWithArgThenMetadataIsProperlyPopulated) {
    PatchTokensTestData::ValidProgramWithKernelAndArg src;
    NEO::KernelInfo dst = {};
    NEO::populateKernelInfo(dst, src.kernels[0], 4);
    ASSERT_EQ(1U, dst.kernelArgInfo.size());
    EXPECT_EQ(NEO::KernelArgMetadata::AccessReadWrite, dst.kernelArgInfo[0].metadata.accessQualifier);
    EXPECT_EQ(NEO::KernelArgMetadata::AddrGlobal, dst.kernelArgInfo[0].metadata.addressQualifier);
    NEO::KernelArgMetadata::TypeQualifiers typeQualifiers = {};
    typeQualifiers.constQual = true;
    EXPECT_EQ(typeQualifiers.packed, dst.kernelArgInfo[0].metadata.typeQualifiers.packed);
    EXPECT_EQ(0U, dst.kernelArgInfo[0].metadata.argByValSize);
    ASSERT_NE(nullptr, dst.kernelArgInfo[0].metadataExtended);
    EXPECT_STREQ("__global", dst.kernelArgInfo[0].metadataExtended->addressQualifier.c_str());
    EXPECT_STREQ("read_write", dst.kernelArgInfo[0].metadataExtended->accessQualifier.c_str());
    EXPECT_STREQ("custom_arg", dst.kernelArgInfo[0].metadataExtended->argName.c_str());
    EXPECT_STREQ("int*", dst.kernelArgInfo[0].metadataExtended->type.c_str());
    EXPECT_STREQ("const", dst.kernelArgInfo[0].metadataExtended->typeQualifiers.c_str());
}

TEST(KernelInfoFromPatchTokens, GivenValidKernelWithImageArgThenArgAccessQualifierIsPopulatedBasedOnArgInfo) {
    PatchTokensTestData::ValidProgramWithKernelAndArg src;
    iOpenCL::SPatchImageMemoryObjectKernelArgument imageArg = {};
    imageArg.Token = iOpenCL::PATCH_TOKEN_IMAGE_MEMORY_OBJECT_KERNEL_ARGUMENT;
    imageArg.Writeable = false;
    src.kernels[0].tokens.kernelArgs[0].objectArg = &imageArg;
    NEO::KernelInfo dst = {};
    NEO::populateKernelInfo(dst, src.kernels[0], 4);
    ASSERT_EQ(1U, dst.kernelArgInfo.size());
    EXPECT_EQ(NEO::KernelArgMetadata::AccessReadWrite, dst.kernelArgInfo[0].metadata.accessQualifier);
}

TEST(KernelInfoFromPatchTokens, GivenValidKernelWithImageArgWhenArgInfoIsMissingThenArgAccessQualifierIsPopulatedBasedOnImageArgWriteableFlag) {
    PatchTokensTestData::ValidProgramWithKernelAndArg src;
    iOpenCL::SPatchImageMemoryObjectKernelArgument imageArg = {};
    imageArg.Token = iOpenCL::PATCH_TOKEN_IMAGE_MEMORY_OBJECT_KERNEL_ARGUMENT;
    src.kernels[0].tokens.kernelArgs[0].objectArg = &imageArg;
    src.kernels[0].tokens.kernelArgs[0].argInfo = nullptr;
    {
        imageArg.Writeable = false;
        NEO::KernelInfo dst = {};
        NEO::populateKernelInfo(dst, src.kernels[0], 4);
        ASSERT_EQ(1U, dst.kernelArgInfo.size());
        EXPECT_EQ(NEO::KernelArgMetadata::AccessReadOnly, dst.kernelArgInfo[0].metadata.accessQualifier);
    }

    {
        imageArg.Writeable = true;
        NEO::KernelInfo dst = {};
        NEO::populateKernelInfo(dst, src.kernels[0], 4);
        ASSERT_EQ(1U, dst.kernelArgInfo.size());
        EXPECT_EQ(NEO::KernelArgMetadata::AccessReadWrite, dst.kernelArgInfo[0].metadata.accessQualifier);
    }
}

TEST(KernelInfoFromPatchTokens, GivenValidKernelWithNonDelimitedArgTypeThenUsesArgTypeAsIs) {
    PatchTokensTestData::ValidProgramWithKernelAndArg src;
    src.arg0TypeMutable[4] = '*';
    NEO::KernelInfo dst = {};
    NEO::populateKernelInfo(dst, src.kernels[0], 4);
    ASSERT_EQ(1U, dst.kernelArgInfo.size());
    EXPECT_STREQ("int**", dst.kernelArgInfo[0].metadataExtended->type.c_str());
}

TEST(KernelInfoFromPatchTokens, GivenDataParameterStreamWithEmptySizeThenTokenIsIgnored) {
    std::vector<uint8_t> storage;
    auto src = PatchTokensTestData::ValidEmptyKernel::create(storage);
    iOpenCL::SPatchDataParameterStream dataParameterStream = {};
    src.tokens.dataParameterStream = &dataParameterStream;
    dataParameterStream.DataParameterStreamSize = 0U;
    NEO::KernelInfo dst;
    NEO::populateKernelInfo(dst, src, 4);
    EXPECT_EQ(nullptr, dst.crossThreadData);
}

TEST(KernelInfoFromPatchTokens, GivenDataParameterStreamWithNonEmptySizeThenCrossthreadDataIsAllocated) {
    std::vector<uint8_t> storage;
    auto src = PatchTokensTestData::ValidEmptyKernel::create(storage);
    iOpenCL::SPatchDataParameterStream dataParameterStream = {};
    src.tokens.dataParameterStream = &dataParameterStream;
    dataParameterStream.DataParameterStreamSize = 256U;
    NEO::KernelInfo dst;
    NEO::populateKernelInfo(dst, src, 4);
    EXPECT_NE(nullptr, dst.crossThreadData);
}

TEST(KernelInfoFromPatchTokens, GivenDataParameterStreamWhenTokensRequiringDeviceInfoPayloadConstantsArePresentThenCrossthreadDataIsProperlyPatched) {
    std::vector<uint8_t> storage;
    auto src = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchDataParameterStream dataParameterStream = {};
    src.tokens.dataParameterStream = &dataParameterStream;
    dataParameterStream.DataParameterStreamSize = 256U;

    NEO::DeviceInfoKernelPayloadConstants deviceInfoConstants;
    deviceInfoConstants.computeUnitsUsedForScratch = 128U;
    deviceInfoConstants.maxWorkGroupSize = 64U;
    std::unique_ptr<uint8_t> slm = std::make_unique<uint8_t>();
    deviceInfoConstants.slmWindow = slm.get();
    deviceInfoConstants.slmWindowSize = 512U;

    iOpenCL::SPatchAllocateStatelessPrivateSurface privateSurface = {};
    privateSurface.PerThreadPrivateMemorySize = 8U;
    privateSurface.IsSimtThread = 1;
    src.tokens.allocateStatelessPrivateSurface = &privateSurface;

    iOpenCL::SPatchDataParameterBuffer privateMemorySize = {};
    privateMemorySize.Offset = 8U;
    src.tokens.crossThreadPayloadArgs.privateMemoryStatelessSize = &privateMemorySize;

    iOpenCL::SPatchDataParameterBuffer localMemoryWindowStartVA = {};
    localMemoryWindowStartVA.Offset = 16U;
    src.tokens.crossThreadPayloadArgs.localMemoryStatelessWindowStartAddress = &localMemoryWindowStartVA;

    iOpenCL::SPatchDataParameterBuffer localMemoryWindowsSize = {};
    localMemoryWindowsSize.Offset = 24U;
    src.tokens.crossThreadPayloadArgs.localMemoryStatelessWindowSize = &localMemoryWindowsSize;

    iOpenCL::SPatchDataParameterBuffer maxWorkgroupSize = {};
    maxWorkgroupSize.Offset = 32U;
    src.tokens.crossThreadPayloadArgs.maxWorkGroupSize = &maxWorkgroupSize;

    NEO::KernelInfo dst;
    NEO::populateKernelInfo(dst, src, 4);
    ASSERT_NE(nullptr, dst.crossThreadData);
    dst.apply(deviceInfoConstants);
    uint32_t expectedPrivateMemorySize = privateSurface.PerThreadPrivateMemorySize * deviceInfoConstants.computeUnitsUsedForScratch * src.tokens.executionEnvironment->LargestCompiledSIMDSize;
    EXPECT_EQ(expectedPrivateMemorySize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + privateMemorySize.Offset));
    EXPECT_EQ(deviceInfoConstants.slmWindowSize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + localMemoryWindowsSize.Offset));
    EXPECT_EQ(deviceInfoConstants.maxWorkGroupSize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + maxWorkgroupSize.Offset));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(deviceInfoConstants.slmWindow), *reinterpret_cast<uintptr_t *>(dst.crossThreadData + localMemoryWindowStartVA.Offset));
}

TEST(KernelInfoFromPatchTokens, givenIsSimtThreadNotSetWhenConfiguringThenDontUseSimdSizeForPrivateSizeCalculation) {
    std::vector<uint8_t> storage;
    auto src = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchDataParameterStream dataParameterStream = {};
    src.tokens.dataParameterStream = &dataParameterStream;
    dataParameterStream.DataParameterStreamSize = 256U;

    NEO::DeviceInfoKernelPayloadConstants deviceInfoConstants;
    deviceInfoConstants.computeUnitsUsedForScratch = 128U;
    deviceInfoConstants.maxWorkGroupSize = 64U;
    std::unique_ptr<uint8_t> slm = std::make_unique<uint8_t>();
    deviceInfoConstants.slmWindow = slm.get();
    deviceInfoConstants.slmWindowSize = 512U;

    iOpenCL::SPatchAllocateStatelessPrivateSurface privateSurface = {};
    privateSurface.PerThreadPrivateMemorySize = 8U;
    privateSurface.IsSimtThread = 0;
    src.tokens.allocateStatelessPrivateSurface = &privateSurface;

    iOpenCL::SPatchDataParameterBuffer privateMemorySize = {};
    privateMemorySize.Offset = 8U;
    src.tokens.crossThreadPayloadArgs.privateMemoryStatelessSize = &privateMemorySize;

    iOpenCL::SPatchDataParameterBuffer localMemoryWindowStartVA = {};
    localMemoryWindowStartVA.Offset = 16U;
    src.tokens.crossThreadPayloadArgs.localMemoryStatelessWindowStartAddress = &localMemoryWindowStartVA;

    iOpenCL::SPatchDataParameterBuffer localMemoryWindowsSize = {};
    localMemoryWindowsSize.Offset = 24U;
    src.tokens.crossThreadPayloadArgs.localMemoryStatelessWindowSize = &localMemoryWindowsSize;

    iOpenCL::SPatchDataParameterBuffer maxWorkgroupSize = {};
    maxWorkgroupSize.Offset = 32U;
    src.tokens.crossThreadPayloadArgs.maxWorkGroupSize = &maxWorkgroupSize;

    NEO::KernelInfo dst;
    NEO::populateKernelInfo(dst, src, 4);
    ASSERT_NE(nullptr, dst.crossThreadData);
    dst.apply(deviceInfoConstants);
    uint32_t expectedPrivateMemorySize = privateSurface.PerThreadPrivateMemorySize * deviceInfoConstants.computeUnitsUsedForScratch;
    EXPECT_EQ(expectedPrivateMemorySize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + privateMemorySize.Offset));
    EXPECT_EQ(deviceInfoConstants.slmWindowSize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + localMemoryWindowsSize.Offset));
    EXPECT_EQ(deviceInfoConstants.maxWorkGroupSize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + maxWorkgroupSize.Offset));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(deviceInfoConstants.slmWindow), *reinterpret_cast<uintptr_t *>(dst.crossThreadData + localMemoryWindowStartVA.Offset));
}

TEST(KernelInfoFromPatchTokens, GivenDataParameterStreamWhenPrivateSurfaceIsNotAllocatedButPrivateSurfaceMemorySizePatchIsNeededThenPatchWithZero) {
    std::vector<uint8_t> storage;
    auto src = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchDataParameterStream dataParameterStream = {};
    src.tokens.dataParameterStream = &dataParameterStream;
    dataParameterStream.DataParameterStreamSize = 256U;

    NEO::DeviceInfoKernelPayloadConstants deviceInfoConstants;
    deviceInfoConstants.computeUnitsUsedForScratch = 128U;
    deviceInfoConstants.maxWorkGroupSize = 64U;
    std::unique_ptr<uint8_t> slm = std::make_unique<uint8_t>();
    deviceInfoConstants.slmWindow = slm.get();
    deviceInfoConstants.slmWindowSize = 512U;

    iOpenCL::SPatchDataParameterBuffer privateMemorySize = {};
    privateMemorySize.Offset = 8U;
    src.tokens.crossThreadPayloadArgs.privateMemoryStatelessSize = &privateMemorySize;

    NEO::KernelInfo dst;
    NEO::populateKernelInfo(dst, src, 4);
    ASSERT_NE(nullptr, dst.crossThreadData);
    dst.apply(deviceInfoConstants);
    uint32_t expectedPrivateMemorySize = 0U;
    EXPECT_EQ(expectedPrivateMemorySize, *reinterpret_cast<uint32_t *>(dst.crossThreadData + privateMemorySize.Offset));
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithGtpinInfoTokenThenKernelInfoIsProperlyPopulated) {
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);
    iOpenCL::SPatchItemHeader gtpinInfo = {};
    gtpinInfo.Token = iOpenCL::PATCH_TOKEN_GTPIN_INFO;
    gtpinInfo.Size = sizeof(iOpenCL::SPatchItemHeader);
    kernelTokens.tokens.gtpinInfo = &gtpinInfo;

    NEO::KernelInfo kernelInfo = {};
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    EXPECT_NE(nullptr, kernelInfo.igcInfoForGtpin);
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithGlobalObjectArgThenKernelInfoIsProperlyPopulated) {
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 1;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(2);
    kernelTokens.tokens.kernelArgs[1].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    EXPECT_TRUE(kernelInfo.usesSsh);
    EXPECT_EQ(1U, kernelInfo.argumentsToPatchNum);
    ASSERT_EQ(2U, kernelInfo.kernelArgInfo.size());
    EXPECT_TRUE(kernelInfo.kernelArgInfo[1].isBuffer);
    ASSERT_EQ(1U, kernelInfo.kernelArgInfo[1].kernelArgPatchInfoVector.size());
    EXPECT_EQ(0U, kernelInfo.kernelArgInfo[1].kernelArgPatchInfoVector[0].crossthreadOffset);
    EXPECT_EQ(0U, kernelInfo.kernelArgInfo[1].kernelArgPatchInfoVector[0].sourceOffset);
    EXPECT_EQ(0U, kernelInfo.kernelArgInfo[1].kernelArgPatchInfoVector[0].size);
    EXPECT_EQ(globalMemArg.Offset, kernelInfo.kernelArgInfo[1].offsetHeap);
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithGlobalObjectArgWhenAddressingModeIsBindlessThenBindlessOffsetIsSetProperly) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(1);
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 0;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(1);
    kernelTokens.tokens.kernelArgs[0].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    auto &argPointer = kernelInfo.kernelDescriptor.payloadMappings.explicitArgs[0].as<NEO::ArgDescPointer>(true);
    EXPECT_TRUE(NEO::isValidOffset(argPointer.bindless));
    EXPECT_FALSE(NEO::isValidOffset(argPointer.bindful));
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithGlobalObjectArgWhenAddressingModeIsBindfulThenBindlessOffsetIsSetProperly) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(0);
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 0;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(1);
    kernelTokens.tokens.kernelArgs[0].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    kernelInfo.kernelDescriptor.kernelAttributes.bufferAddressingMode = NEO::KernelDescriptor::BindfulAndStateless;
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    auto &argPointer = kernelInfo.kernelDescriptor.payloadMappings.explicitArgs[0].as<NEO::ArgDescPointer>(true);
    EXPECT_FALSE(NEO::isValidOffset(argPointer.bindless));
    EXPECT_TRUE(NEO::isValidOffset(argPointer.bindful));
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithImageObjectArgWhenAddressingModeIsBindlessThenBindlessOffsetIsSetProperly) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(1);
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchImageMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_IMAGE_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchImageMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 0;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(1);
    kernelTokens.tokens.kernelArgs[0].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    auto &argPointer = kernelInfo.kernelDescriptor.payloadMappings.explicitArgs[0].as<NEO::ArgDescImage>(true);
    EXPECT_TRUE(NEO::isValidOffset(argPointer.bindless));
    EXPECT_FALSE(NEO::isValidOffset(argPointer.bindful));
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithImageObjectArgWhenAddressingModeIsBindfulThenBindlessOffsetIsSetProperly) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(0);
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchImageMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_IMAGE_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchImageMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 0;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(1);
    kernelTokens.tokens.kernelArgs[0].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    kernelInfo.kernelDescriptor.kernelAttributes.bufferAddressingMode = NEO::KernelDescriptor::BindfulAndStateless;
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    auto &argPointer = kernelInfo.kernelDescriptor.payloadMappings.explicitArgs[0].as<NEO::ArgDescImage>(true);
    EXPECT_FALSE(NEO::isValidOffset(argPointer.bindless));
    EXPECT_TRUE(NEO::isValidOffset(argPointer.bindful));
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithStatelessObjectArgWhenAddressingModeIsBindlessThenBindlessOffsetIsSetProperly) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(1);
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    auto surfaceStateHeapOffset = 0x40;
    auto dataParamOffset = 0x32;

    iOpenCL::SPatchStatelessGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_STATELESS_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchStatelessGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 0;
    globalMemArg.SurfaceStateHeapOffset = surfaceStateHeapOffset;
    globalMemArg.DataParamOffset = dataParamOffset;

    kernelTokens.tokens.kernelArgs.resize(1);
    kernelTokens.tokens.kernelArgs[0].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    auto &argPointer = kernelInfo.kernelDescriptor.payloadMappings.explicitArgs[0].as<NEO::ArgDescPointer>(true);
    EXPECT_TRUE(NEO::isValidOffset(argPointer.bindless));
    EXPECT_TRUE(NEO::isValidOffset(argPointer.stateless));
    EXPECT_FALSE(NEO::isValidOffset(argPointer.bindful));

    EXPECT_EQ(argPointer.bindless, surfaceStateHeapOffset);
    EXPECT_EQ(argPointer.stateless, dataParamOffset);
}

TEST(KernelInfoFromPatchTokens, GivenKernelWithStatelessObjectArgWhenAddressingModeIsBindfulThenBindlessOffsetIsSetProperly) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UseBindlessMode.set(0);
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchStatelessGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_STATELESS_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchStatelessGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 0;
    globalMemArg.SurfaceStateHeapOffset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(1);
    kernelTokens.tokens.kernelArgs[0].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    kernelInfo.kernelDescriptor.kernelAttributes.bufferAddressingMode = NEO::KernelDescriptor::BindfulAndStateless;
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    auto &argPointer = kernelInfo.kernelDescriptor.payloadMappings.explicitArgs[0].as<NEO::ArgDescPointer>(true);
    EXPECT_FALSE(NEO::isValidOffset(argPointer.bindless));
    EXPECT_TRUE(NEO::isValidOffset(argPointer.bindful));
}

TEST(KernelInfoFromPatchTokens, GivenDefaultModeThenKernelDescriptorIsNotBeingPopulated) {
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 1;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(2);
    kernelTokens.tokens.kernelArgs[1].objectArg = &globalMemArg;
    NEO::useKernelDescriptor = false;
    NEO::KernelInfo kernelInfo = {};
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    EXPECT_TRUE(kernelInfo.kernelDescriptor.payloadMappings.explicitArgs.empty());
    NEO::useKernelDescriptor = true;
}

TEST(KernelInfoFromPatchTokens, WhenUseKernelDescriptorIsEnabledThenKernelDescriptorIsBeingPopulated) {
    std::vector<uint8_t> storage;
    NEO::PatchTokenBinary::KernelFromPatchtokens kernelTokens = PatchTokensTestData::ValidEmptyKernel::create(storage);

    iOpenCL::SPatchGlobalMemoryObjectKernelArgument globalMemArg = {};
    globalMemArg.Token = iOpenCL::PATCH_TOKEN_GLOBAL_MEMORY_OBJECT_KERNEL_ARGUMENT;
    globalMemArg.Size = sizeof(iOpenCL::SPatchGlobalMemoryObjectKernelArgument);
    globalMemArg.ArgumentNumber = 1;
    globalMemArg.Offset = 0x40;

    kernelTokens.tokens.kernelArgs.resize(2);
    kernelTokens.tokens.kernelArgs[1].objectArg = &globalMemArg;
    NEO::KernelInfo kernelInfo = {};
    NEO::useKernelDescriptor = true;
    NEO::populateKernelInfo(kernelInfo, kernelTokens, sizeof(void *));
    EXPECT_FALSE(kernelInfo.kernelDescriptor.payloadMappings.explicitArgs.empty());
}
