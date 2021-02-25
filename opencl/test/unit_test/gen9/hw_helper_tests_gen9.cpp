/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/cmd_parse/gen_cmd_parse.h"

#include "opencl/test/unit_test/helpers/get_gpgpu_engines_tests.inl"
#include "opencl/test/unit_test/helpers/hw_helper_tests.h"

using HwHelperTestGen9 = HwHelperTest;

GEN9TEST_F(HwHelperTestGen9, WhenGettingMaxBarriersPerSliceThenCorrectSizeIsReturned) {
    auto &helper = HwHelper::get(renderCoreFamily);
    EXPECT_EQ(32u, helper.getMaxBarrierRegisterPerSlice());
}

GEN9TEST_F(HwHelperTestGen9, givenGen9WhenCallIsPackedSupportedThenReturnFalse) {
    auto &helper = HwHelper::get(renderCoreFamily);
    EXPECT_FALSE(helper.packedFormatsSupported());
}

GEN9TEST_F(HwHelperTestGen9, WhenSettingCapabilityCoherencyFlagThenFlagIsSet) {
    auto &helper = HwHelper::get(renderCoreFamily);

    bool coherency = false;
    helper.setCapabilityCoherencyFlag(&hardwareInfo, coherency);
    EXPECT_TRUE(coherency);
}

GEN9TEST_F(HwHelperTestGen9, WhenGettingPitchAlignmentForImageThenCorrectValueIsReturned) {
    auto &helper = HwHelper::get(renderCoreFamily);
    EXPECT_EQ(4u, helper.getPitchAlignmentForImage(&hardwareInfo));
}

GEN9TEST_F(HwHelperTestGen9, WhenAdjustingDefaultEngineTypeThenEngineTypeIsSet) {
    auto engineType = hardwareInfo.capabilityTable.defaultEngineType;
    auto &helper = HwHelper::get(renderCoreFamily);
    helper.adjustDefaultEngineType(&hardwareInfo);
    EXPECT_EQ(engineType, hardwareInfo.capabilityTable.defaultEngineType);
}

GEN9TEST_F(HwHelperTestGen9, givenGen9PlatformWhenSetupHardwareCapabilitiesIsCalledThenDefaultImplementationIsUsed) {
    auto &helper = HwHelper::get(renderCoreFamily);

    // Test default method implementation
    testDefaultImplementationOfSetupHardwareCapabilities(helper, hardwareInfo);
}

GEN9TEST_F(HwHelperTestGen9, givenDebuggingActiveWhenSipKernelTypeIsQueriedThenDbgCsrLocalTypeIsReturned) {
    auto &helper = HwHelper::get(renderCoreFamily);

    auto sipType = helper.getSipKernelType(true);
    EXPECT_EQ(SipKernelType::DbgCsrLocal, sipType);
}

GEN9TEST_F(HwHelperTestGen9, whenGetGpgpuEnginesThenReturnThreeRcsEngines) {
    whenGetGpgpuEnginesThenReturnTwoRcsEngines<FamilyType>(pDevice->getHardwareInfo());
    EXPECT_EQ(3u, pDevice->engines.size());
}

using MemorySynchronizatiopCommandsTestsGen9 = ::testing::Test;
GEN9TEST_F(MemorySynchronizatiopCommandsTestsGen9, WhenProgrammingCacheFlushThenExpectConstantCacheFieldSet) {
    using PIPE_CONTROL = typename FamilyType::PIPE_CONTROL;
    std::unique_ptr<uint8_t> buffer(new uint8_t[128]);

    LinearStream stream(buffer.get(), 128);
    MemorySynchronizationCommands<FamilyType>::addFullCacheFlush(stream);
    PIPE_CONTROL *pipeControl = genCmdCast<PIPE_CONTROL *>(buffer.get());
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_TRUE(pipeControl->getConstantCacheInvalidationEnable());
}
