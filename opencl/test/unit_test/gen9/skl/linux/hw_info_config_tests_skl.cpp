/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/helpers/gtest_helpers.h"
#include "opencl/test/unit_test/os_interface/linux/hw_info_config_linux_tests.h"

using namespace NEO;

struct HwInfoConfigTestLinuxSkl : HwInfoConfigTestLinux {
    void SetUp() override {
        HwInfoConfigTestLinux::SetUp();
        drm->StoredDeviceID = 0x0902;
        drm->setGtType(GTTYPE_GT2);
    }
};

SKLTEST_F(HwInfoConfigTestLinuxSkl, configureHwInfo) {
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ((unsigned short)drm->StoredDeviceID, outHwInfo.platform.usDeviceID);
    EXPECT_EQ((unsigned short)drm->StoredDeviceRevID, outHwInfo.platform.usRevId);
    EXPECT_EQ((uint32_t)drm->StoredEUVal, outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ((uint32_t)drm->StoredSSVal, outHwInfo.gtSystemInfo.SubSliceCount);
    EXPECT_EQ(aub_stream::ENGINE_RCS, outHwInfo.capabilityTable.defaultEngineType);

    EXPECT_EQ(GTTYPE_GT2, outHwInfo.platform.eGTType);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1_5);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrGT2);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT3);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT4);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTA);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTC);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTX);

    //constant sysInfo/ftr flags
    EXPECT_EQ(1u, outHwInfo.gtSystemInfo.VEBoxInfo.Instances.Bits.VEBox0Enabled);
    EXPECT_EQ(1u, outHwInfo.gtSystemInfo.VDBoxInfo.Instances.Bits.VDBox0Enabled);
    EXPECT_TRUE(outHwInfo.gtSystemInfo.VEBoxInfo.IsValid);
    EXPECT_TRUE(outHwInfo.gtSystemInfo.VDBoxInfo.IsValid);

    drm->StoredDeviceID = 0x1902;
    drm->setGtType(GTTYPE_GT1);
    drm->StoredSSVal = 3;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ((unsigned short)drm->StoredDeviceID, outHwInfo.platform.usDeviceID);
    EXPECT_EQ((unsigned short)drm->StoredDeviceRevID, outHwInfo.platform.usRevId);
    EXPECT_EQ((uint32_t)drm->StoredEUVal, outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ((uint32_t)drm->StoredSSVal, outHwInfo.gtSystemInfo.SubSliceCount);
    EXPECT_EQ(1u, outHwInfo.gtSystemInfo.SliceCount);
    EXPECT_EQ(aub_stream::ENGINE_RCS, outHwInfo.capabilityTable.defaultEngineType);

    EXPECT_EQ(GTTYPE_GT1, outHwInfo.platform.eGTType);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrGT1);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1_5);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT2);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT3);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT4);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTA);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTC);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTX);

    drm->StoredDeviceID = 0x1917;
    drm->setGtType(GTTYPE_GT1_5);
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ((unsigned short)drm->StoredDeviceID, outHwInfo.platform.usDeviceID);
    EXPECT_EQ((unsigned short)drm->StoredDeviceRevID, outHwInfo.platform.usRevId);
    EXPECT_EQ((uint32_t)drm->StoredEUVal, outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ((uint32_t)drm->StoredSSVal, outHwInfo.gtSystemInfo.SubSliceCount);
    EXPECT_EQ(aub_stream::ENGINE_RCS, outHwInfo.capabilityTable.defaultEngineType);

    EXPECT_EQ(GTTYPE_GT1_5, outHwInfo.platform.eGTType);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrGT1_5);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT2);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT3);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT4);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTA);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTC);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTX);

    drm->StoredDeviceID = 0x0903;
    drm->setGtType(GTTYPE_GT3);
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ((unsigned short)drm->StoredDeviceID, outHwInfo.platform.usDeviceID);
    EXPECT_EQ((unsigned short)drm->StoredDeviceRevID, outHwInfo.platform.usRevId);
    EXPECT_EQ((uint32_t)drm->StoredEUVal, outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ((uint32_t)drm->StoredSSVal, outHwInfo.gtSystemInfo.SubSliceCount);
    EXPECT_EQ(aub_stream::ENGINE_RCS, outHwInfo.capabilityTable.defaultEngineType);

    EXPECT_EQ(GTTYPE_GT3, outHwInfo.platform.eGTType);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1_5);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT2);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrGT3);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT4);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTA);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTC);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTX);

    drm->StoredDeviceID = 0x0904;
    drm->setGtType(GTTYPE_GT4);
    drm->StoredSSVal = 6;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ((unsigned short)drm->StoredDeviceID, outHwInfo.platform.usDeviceID);
    EXPECT_EQ((unsigned short)drm->StoredDeviceRevID, outHwInfo.platform.usRevId);
    EXPECT_EQ((uint32_t)drm->StoredEUVal, outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ((uint32_t)drm->StoredSSVal, outHwInfo.gtSystemInfo.SubSliceCount);
    EXPECT_EQ(1u, outHwInfo.gtSystemInfo.SliceCount);
    EXPECT_EQ(aub_stream::ENGINE_RCS, outHwInfo.capabilityTable.defaultEngineType);

    EXPECT_EQ(GTTYPE_GT4, outHwInfo.platform.eGTType);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1_5);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT2);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT3);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrGT4);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTA);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTC);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTX);

    auto &outKmdNotifyProperties = outHwInfo.capabilityTable.kmdNotifyProperties;
    EXPECT_TRUE(outKmdNotifyProperties.enableKmdNotify);
    EXPECT_EQ(50000, outKmdNotifyProperties.delayKmdNotifyMicroseconds);
    EXPECT_TRUE(outKmdNotifyProperties.enableQuickKmdSleep);
    EXPECT_EQ(5000, outKmdNotifyProperties.delayQuickKmdSleepMicroseconds);
    EXPECT_TRUE(outKmdNotifyProperties.enableQuickKmdSleepForSporadicWaits);
    EXPECT_EQ(200000, outKmdNotifyProperties.delayQuickKmdSleepForSporadicWaitsMicroseconds);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, negativeUnknownDevId) {
    drm->StoredDeviceID = 0;
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, negativeFailedIoctlDevId) {
    drm->StoredRetValForDeviceID = -2;
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-2, ret);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, negativeFailedIoctlDevRevId) {
    drm->StoredRetValForDeviceRevID = -3;
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-3, ret);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, negativeFailedIoctlEuCount) {
    drm->StoredRetValForEUVal = -4;
    drm->failRetTopology = true;
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-4, ret);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, negativeFailedIoctlSsCount) {
    drm->StoredRetValForSSVal = -5;
    drm->failRetTopology = true;
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-5, ret);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, configureHwInfoWaFlags) {
    auto hwInfoConfig = HwInfoConfig::get(productFamily);

    drm->StoredDeviceRevID = 1;
    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);

    drm->StoredDeviceRevID = 0;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waCompressedResourceRequiresConstVA21);

    drm->StoredDeviceRevID = 5;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waCompressedResourceRequiresConstVA21);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waModifyVFEStateAfterGPGPUPreemption);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waDisablePerCtxtPreemptionGranularityControl);

    drm->StoredDeviceRevID = 6;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waCompressedResourceRequiresConstVA21);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waModifyVFEStateAfterGPGPUPreemption);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waDisablePerCtxtPreemptionGranularityControl);
    EXPECT_EQ(0u, outHwInfo.workaroundTable.waCSRUncachable);
}

SKLTEST_F(HwInfoConfigTestLinuxSkl, configureHwInfoEdram) {
    auto hwInfoConfig = HwInfoConfig::get(productFamily);

    int ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ_VAL(0u, outHwInfo.gtSystemInfo.EdramSizeInKb);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrEDram);

    drm->StoredDeviceID = 0x1926;
    drm->setGtType(GTTYPE_GT3);
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ_VAL((64u * 1024u), outHwInfo.gtSystemInfo.EdramSizeInKb);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrEDram);

    drm->StoredDeviceID = 0x1927;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ_VAL((64u * 1024u), outHwInfo.gtSystemInfo.EdramSizeInKb);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrEDram);

    drm->StoredDeviceID = 0x192D;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ_VAL((64u * 1024u), outHwInfo.gtSystemInfo.EdramSizeInKb);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrEDram);

    drm->StoredDeviceID = 0x193B;
    drm->setGtType(GTTYPE_GT4);
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ_VAL((128u * 1024u), outHwInfo.gtSystemInfo.EdramSizeInKb);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrEDram);

    drm->StoredDeviceID = 0x193D;
    ret = hwInfoConfig->configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ_VAL((128u * 1024u), outHwInfo.gtSystemInfo.EdramSizeInKb);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrEDram);
}

template <typename T>
class SklHwInfoTests : public ::testing::Test {
};
typedef ::testing::Types<SKL_1x2x6, SKL_1x3x6, SKL_1x3x8, SKL_2x3x8, SKL_3x3x8> sklTestTypes;
TYPED_TEST_CASE(SklHwInfoTests, sklTestTypes);
TYPED_TEST(SklHwInfoTests, gtSetupIsCorrect) {
    HardwareInfo hwInfo;
    auto executionEnvironment = std::make_unique<ExecutionEnvironment>();
    executionEnvironment->prepareRootDeviceEnvironments(1);
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(defaultHwInfo.get());
    DrmMock drm(*executionEnvironment->rootDeviceEnvironments[0]);
    GT_SYSTEM_INFO &gtSystemInfo = hwInfo.gtSystemInfo;
    DeviceDescriptor device = {0, &hwInfo, &TypeParam::setupHardwareInfo, GTTYPE_GT1};

    int ret = drm.setupHardwareInfo(&device, false);

    EXPECT_EQ(ret, 0);
    EXPECT_GT(gtSystemInfo.EUCount, 0u);
    EXPECT_GT(gtSystemInfo.ThreadCount, 0u);
    EXPECT_GT(gtSystemInfo.SliceCount, 0u);
    EXPECT_GT(gtSystemInfo.SubSliceCount, 0u);
    EXPECT_GT_VAL(gtSystemInfo.L3CacheSizeInKb, 0u);
    EXPECT_EQ(gtSystemInfo.CsrSizeInMb, 8u);
    EXPECT_FALSE(gtSystemInfo.IsDynamicallyPopulated);
}

TYPED_TEST(SklHwInfoTests, givenGTSystemInfoTypeWhenConfigureHardwareCustomThenSliceCountDontChange) {
    HardwareInfo hwInfo;
    auto osInterface = std::unique_ptr<OSInterface>(new OSInterface());
    GT_SYSTEM_INFO &gtSystemInfo = hwInfo.gtSystemInfo;

    TypeParam::setupHardwareInfo(&hwInfo, false);
    auto sliceCount = gtSystemInfo.SliceCount;

    HwInfoConfig *hwConfig = HwInfoConfig::get(PRODUCT_FAMILY::IGFX_SKYLAKE);

    hwConfig->configureHardwareCustom(&hwInfo, osInterface.get());
    EXPECT_EQ(gtSystemInfo.SliceCount, sliceCount);
}
