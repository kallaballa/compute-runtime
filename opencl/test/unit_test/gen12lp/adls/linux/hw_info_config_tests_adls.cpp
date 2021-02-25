/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/os_interface.h"

#include "opencl/test/unit_test/helpers/gtest_helpers.h"
#include "opencl/test/unit_test/os_interface/linux/drm_mock.h"
#include "opencl/test/unit_test/os_interface/linux/hw_info_config_linux_tests.h"

using namespace NEO;

struct HwInfoConfigTestLinuxAdls : HwInfoConfigTestLinux {
    void SetUp() override {
        HwInfoConfigTestLinux::SetUp();

        drm = new DrmMock(*executionEnvironment->rootDeviceEnvironments[0]);
        osInterface->get()->setDrm(drm);

        drm->StoredDeviceID = IGFX_ALDERLAKE_S;
        drm->setGtType(GTTYPE_GT1);
    }
};

ADLSTEST_F(HwInfoConfigTestLinuxAdls, WhenConfiguringHwInfoThenConfigIsCorrect) {
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(static_cast<unsigned short>(drm->StoredDeviceID), outHwInfo.platform.usDeviceID);
    EXPECT_EQ(static_cast<unsigned short>(drm->StoredDeviceRevID), outHwInfo.platform.usRevId);
    EXPECT_EQ(static_cast<uint32_t>(drm->StoredEUVal), outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ(static_cast<uint32_t>(drm->StoredSSVal), outHwInfo.gtSystemInfo.SubSliceCount);
    EXPECT_EQ(1u, outHwInfo.gtSystemInfo.SliceCount);

    EXPECT_EQ(GTTYPE_GT1, outHwInfo.platform.eGTType);
    EXPECT_TRUE(outHwInfo.featureTable.ftrGT1);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGT1_5);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGT2);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGT3);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGT4);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGTA);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGTC);
    EXPECT_FALSE(outHwInfo.featureTable.ftrGTX);
    EXPECT_FALSE(outHwInfo.featureTable.ftrTileY);
}

ADLSTEST_F(HwInfoConfigTestLinuxAdls, GivenIncorrectDataWhenConfiguringHwInfoThenErrorIsReturned) {
    auto hwInfoConfig = HwInfoConfig::get(productFamily);

    drm->StoredRetValForDeviceID = -1;
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);

    drm->StoredRetValForDeviceID = 0;
    drm->StoredRetValForDeviceRevID = -1;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);

    drm->StoredRetValForDeviceRevID = 0;
    drm->failRetTopology = true;
    drm->StoredRetValForEUVal = -1;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);

    drm->StoredRetValForEUVal = 0;
    drm->StoredRetValForSSVal = -1;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);
}

TEST(AdlsHwInfoTests, WhenSettingUpHwInfoThenConfigIsCorrect) {
    HardwareInfo hwInfo;
    auto executionEnvironment = std::make_unique<ExecutionEnvironment>();
    executionEnvironment->prepareRootDeviceEnvironments(1);
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(defaultHwInfo.get());
    DrmMock drm(*executionEnvironment->rootDeviceEnvironments[0]);
    GT_SYSTEM_INFO &gtSystemInfo = hwInfo.gtSystemInfo;
    DeviceDescriptor device = {0, &hwInfo, &ADLS_HW_CONFIG::setupHardwareInfo, GTTYPE_GT1};

    int ret = drm.setupHardwareInfo(&device, false);

    EXPECT_EQ(ret, 0);
    EXPECT_GT(gtSystemInfo.EUCount, 0u);
    EXPECT_GT(gtSystemInfo.ThreadCount, 0u);
    EXPECT_GT(gtSystemInfo.SliceCount, 0u);
    EXPECT_GT(gtSystemInfo.SubSliceCount, 0u);
    EXPECT_GT_VAL(gtSystemInfo.L3CacheSizeInKb, 0u);
    EXPECT_EQ(gtSystemInfo.CsrSizeInMb, 8u);
    EXPECT_FALSE(gtSystemInfo.IsDynamicallyPopulated);
    EXPECT_GT(gtSystemInfo.DualSubSliceCount, 0u);
    EXPECT_GT(gtSystemInfo.MaxDualSubSlicesSupported, 0u);
}
