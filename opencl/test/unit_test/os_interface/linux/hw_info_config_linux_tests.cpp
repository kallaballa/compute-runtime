/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/os_interface/linux/hw_info_config_linux_tests.h"

#include "shared/source/helpers/hw_helper.h"
#include "shared/source/os_interface/linux/os_interface.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/default_hw_info.h"

#include "opencl/extensions/public/cl_ext_private.h"

#include <cstring>

namespace NEO {

constexpr uint32_t hwConfigTestMidThreadBit = 1 << 8;
constexpr uint32_t hwConfigTestThreadGroupBit = 1 << 9;
constexpr uint32_t hwConfigTestMidBatchBit = 1 << 10;

template <>
int HwInfoConfigHw<IGFX_UNKNOWN>::configureHardwareCustom(HardwareInfo *hwInfo, OSInterface *osIface) {
    FeatureTable *featureTable = &hwInfo->featureTable;
    featureTable->ftrGpGpuMidThreadLevelPreempt = 0;
    featureTable->ftrGpGpuThreadGroupLevelPreempt = 0;
    featureTable->ftrGpGpuMidBatchPreempt = 0;

    if (hwInfo->platform.usDeviceID == 30) {
        GT_SYSTEM_INFO *gtSystemInfo = &hwInfo->gtSystemInfo;
        gtSystemInfo->EdramSizeInKb = 128 * 1000;
    }
    if (hwInfo->platform.usDeviceID & hwConfigTestMidThreadBit) {
        featureTable->ftrGpGpuMidThreadLevelPreempt = 1;
    }
    if (hwInfo->platform.usDeviceID & hwConfigTestThreadGroupBit) {
        featureTable->ftrGpGpuThreadGroupLevelPreempt = 1;
    }
    if (hwInfo->platform.usDeviceID & hwConfigTestMidBatchBit) {
        featureTable->ftrGpGpuMidBatchPreempt = 1;
    }
    return (hwInfo->platform.usDeviceID == 10) ? -1 : 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getHostMemCapabilities(const HardwareInfo * /*hwInfo*/) {
    return 0;
}

template <>
void HwInfoConfigHw<IGFX_UNKNOWN>::adjustPlatformForProductFamily(HardwareInfo *hwInfo) {
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getDeviceMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getSingleDeviceSharedMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getCrossDeviceSharedMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getSharedSystemMemCapabilities() {
    return 0;
}

template <>
bool HwInfoConfigHw<IGFX_UNKNOWN>::isEvenContextCountRequired() {
    return false;
}

template <>
void HwInfoConfigHw<IGFX_UNKNOWN>::adjustSamplerState(void *sampler){};

template <>
void HwInfoConfigHw<IGFX_UNKNOWN>::convertTimestampsFromOaToCsDomain(uint64_t &timestampData){};
} // namespace NEO

struct DummyHwConfig : HwInfoConfigHw<IGFX_UNKNOWN> {
};

using namespace NEO;

void mockCpuidex(int *cpuInfo, int functionId, int subfunctionId);

void HwInfoConfigTestLinux::SetUp() {
    HwInfoConfigTest::SetUp();
    executionEnvironment = std::make_unique<ExecutionEnvironment>();
    executionEnvironment->prepareRootDeviceEnvironments(1);
    executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(defaultHwInfo.get());

    osInterface = new OSInterface();
    drm = new DrmMock(*executionEnvironment->rootDeviceEnvironments[0]);
    osInterface->get()->setDrm(static_cast<Drm *>(drm));

    drm->StoredDeviceID = pInHwInfo.platform.usDeviceID;
    drm->StoredDeviceRevID = 0;
    drm->StoredEUVal = pInHwInfo.gtSystemInfo.EUCount;
    drm->StoredSSVal = pInHwInfo.gtSystemInfo.SubSliceCount;

    rt_cpuidex_func = CpuInfo::cpuidexFunc;
    CpuInfo::cpuidexFunc = mockCpuidex;
}

void HwInfoConfigTestLinux::TearDown() {
    CpuInfo::cpuidexFunc = rt_cpuidex_func;

    delete osInterface;

    HwInfoConfigTest::TearDown();
}

void mockCpuidex(int *cpuInfo, int functionId, int subfunctionId) {
    if (subfunctionId == 0) {
        cpuInfo[0] = 0x7F;
    }
    if (subfunctionId == 1) {
        cpuInfo[0] = 0x1F;
    }
    if (subfunctionId == 2) {
        cpuInfo[0] = 0;
    }
}

struct HwInfoConfigTestLinuxDummy : HwInfoConfigTestLinux {
    void SetUp() override {
        HwInfoConfigTestLinux::SetUp();

        drm->StoredDeviceID = 1;
        drm->setGtType(GTTYPE_GT0);
        testPlatform->eRenderCoreFamily = defaultHwInfo->platform.eRenderCoreFamily;
    }

    void TearDown() override {
        HwInfoConfigTestLinux::TearDown();
    }

    DummyHwConfig hwConfig;
};

TEST_F(HwInfoConfigTestLinuxDummy, GivenDummyConfigWhenConfiguringHwInfoThenSucceeds) {
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
}

GTTYPE GtTypes[] = {
    GTTYPE_GT1, GTTYPE_GT2, GTTYPE_GT1_5, GTTYPE_GT2_5, GTTYPE_GT3, GTTYPE_GT4, GTTYPE_GTA, GTTYPE_GTC, GTTYPE_GTX};

using HwInfoConfigCommonLinuxTest = ::testing::Test;

HWTEST2_F(HwInfoConfigCommonLinuxTest, givenDebugFlagSetWhenEnablingBlitterOperationsSupportThenIgnore, IsAtMostGen11) {
    DebugManagerStateRestore restore{};
    HardwareInfo hardwareInfo = *defaultHwInfo;

    auto hwInfoConfig = HwInfoConfig::get(hardwareInfo.platform.eProductFamily);

    DebugManager.flags.EnableBlitterOperationsSupport.set(1);
    hwInfoConfig->configureHardwareCustom(&hardwareInfo, nullptr);
    EXPECT_FALSE(hardwareInfo.capabilityTable.blitterOperationsSupported);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenDummyConfigGtTypesThenFtrIsSetCorrectly) {
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);

    EXPECT_EQ(GTTYPE_GT0, outHwInfo.platform.eGTType);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT1_5);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT2);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT2_5);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT3);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGT4);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTA);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTC);
    EXPECT_EQ(0u, outHwInfo.featureTable.ftrGTX);

    size_t arrSize = sizeof(GtTypes) / sizeof(GTTYPE);
    uint32_t FtrSum = 0;
    for (uint32_t i = 0; i < arrSize; i++) {
        drm->setGtType(GtTypes[i]);
        ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
        EXPECT_EQ(0, ret);
        EXPECT_EQ(GtTypes[i], outHwInfo.platform.eGTType);
        bool FtrPresent = (outHwInfo.featureTable.ftrGT1 ||
                           outHwInfo.featureTable.ftrGT1_5 ||
                           outHwInfo.featureTable.ftrGT2 ||
                           outHwInfo.featureTable.ftrGT2_5 ||
                           outHwInfo.featureTable.ftrGT3 ||
                           outHwInfo.featureTable.ftrGT4 ||
                           outHwInfo.featureTable.ftrGTA ||
                           outHwInfo.featureTable.ftrGTC ||
                           outHwInfo.featureTable.ftrGTX);
        EXPECT_TRUE(FtrPresent);
        FtrSum += (outHwInfo.featureTable.ftrGT1 +
                   outHwInfo.featureTable.ftrGT1_5 +
                   outHwInfo.featureTable.ftrGT2 +
                   outHwInfo.featureTable.ftrGT2_5 +
                   outHwInfo.featureTable.ftrGT3 +
                   outHwInfo.featureTable.ftrGT4 +
                   outHwInfo.featureTable.ftrGTA +
                   outHwInfo.featureTable.ftrGTC +
                   outHwInfo.featureTable.ftrGTX);
    }
    EXPECT_EQ(arrSize, FtrSum);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenDummyConfigThenEdramIsDetected) {
    drm->StoredDeviceID = 30;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1u, outHwInfo.featureTable.ftrEDram);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenEnabledPlatformCoherencyWhenConfiguringHwInfoThenIgnoreAndSetAsDisabled) {
    drm->StoredDeviceID = 21;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(outHwInfo.capabilityTable.ftrSupportsCoherency);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenDisabledPlatformCoherencyWhenConfiguringHwInfoThenSetValidCapability) {
    drm->StoredDeviceID = 20;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(outHwInfo.capabilityTable.ftrSupportsCoherency);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenUnknownGtTypeWhenConfiguringHwInfoThenFails) {
    drm->setGtType(GTTYPE_UNDEFINED);

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenUnknownDevIdWhenConfiguringHwInfoThenFails) {
    drm->StoredDeviceID = 0;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenFailGetDevIdWhenConfiguringHwInfoThenFails) {
    drm->StoredRetValForDeviceID = -2;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-2, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenFailGetDevRevIdWhenConfiguringHwInfoThenFails) {
    drm->StoredRetValForDeviceRevID = -3;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-3, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenFailGetEuCountWhenConfiguringHwInfoThenFails) {
    drm->StoredRetValForEUVal = -4;
    drm->failRetTopology = true;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-4, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenFailGetSsCountWhenConfiguringHwInfoThenFails) {
    drm->StoredRetValForSSVal = -5;
    drm->failRetTopology = true;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-5, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, whenFailGettingTopologyThenFallbackToEuCountIoctl) {
    drm->failRetTopology = true;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_NE(-1, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenInvalidTopologyDataWhenConfiguringThenReturnError) {
    auto storedSVal = drm->StoredSVal;
    auto storedSSVal = drm->StoredSSVal;
    auto storedEUVal = drm->StoredEUVal;

    {
        // 0 euCount
        drm->StoredSVal = storedSVal;
        drm->StoredSSVal = storedSSVal;
        drm->StoredEUVal = 0;

        int sliceCount, subSliceCount, euCount;
        EXPECT_FALSE(drm->queryTopology(outHwInfo, sliceCount, subSliceCount, euCount));
    }

    {
        // 0 subSliceCount
        drm->StoredSVal = storedSVal;
        drm->StoredSSVal = 0;
        drm->StoredEUVal = storedEUVal;

        int sliceCount, subSliceCount, euCount;
        EXPECT_FALSE(drm->queryTopology(outHwInfo, sliceCount, subSliceCount, euCount));
    }

    {
        // 0 sliceCount
        drm->StoredSVal = 0;
        drm->StoredSSVal = storedSSVal;
        drm->StoredEUVal = storedEUVal;

        int sliceCount, subSliceCount, euCount;
        EXPECT_FALSE(drm->queryTopology(outHwInfo, sliceCount, subSliceCount, euCount));
    }
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenFailingCustomConfigWhenConfiguringHwInfoThenFails) {
    drm->StoredDeviceID = 10;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenUnknownDeviceIdWhenConfiguringHwInfoThenFails) {
    drm->StoredDeviceID = 0;
    drm->setGtType(GTTYPE_GT1);

    auto hwConfig = DummyHwConfig{};
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(-1, ret);
}

TEST_F(HwInfoConfigTestLinuxDummy, whenConfigureHwInfoIsCalledThenAreNonPersistentContextsSupportedReturnsTrue) {
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_TRUE(drm->areNonPersistentContextsSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, whenConfigureHwInfoIsCalledAndPersitentContextIsUnsupportedThenAreNonPersistentContextsSupportedReturnsFalse) {
    drm->StoredPersistentContextsSupport = 0;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(drm->areNonPersistentContextsSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenPreemptionDrmEnabledMidThreadOnWhenConfiguringHwInfoThenPreemptionIsSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = hwConfigTestMidThreadBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::MidThread, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenPreemptionDrmEnabledThreadGroupOnWhenConfiguringHwInfoThenPreemptionIsSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = hwConfigTestThreadGroupBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::ThreadGroup, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, givenDebugFlagSetWhenConfiguringHwInfoThenPrintGetParamIoctlsOutput) {
    DebugManagerStateRestore restore;
    DebugManager.flags.PrintDebugMessages.set(true);

    testing::internal::CaptureStdout(); // start capturing
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);

    std::array<std::string, 6> expectedStrings = {{"DRM_IOCTL_I915_GETPARAM: param: I915_PARAM_CHIPSET_ID, output value: 1, retCode: 0",
                                                   "DRM_IOCTL_I915_GETPARAM: param: I915_PARAM_REVISION, output value: 0, retCode: 0",
                                                   "DRM_IOCTL_I915_GETPARAM: param: I915_PARAM_CHIPSET_ID, output value: 1, retCode: 0",
                                                   "DRM_IOCTL_I915_GETPARAM: param: I915_PARAM_HAS_SCHEDULER, output value: 7, retCode: 0"

    }};

    DebugManager.flags.PrintDebugMessages.set(false);
    std::string output = testing::internal::GetCapturedStdout(); // stop capturing
    for (const auto &expectedString : expectedStrings) {
        EXPECT_NE(std::string::npos, output.find(expectedString));
    }

    EXPECT_EQ(std::string::npos, output.find("UNKNOWN"));
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenPreemptionDrmEnabledMidBatchOnWhenConfiguringHwInfoThenPreemptionIsSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = hwConfigTestMidBatchBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::MidBatch, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, WhenConfiguringHwInfoThenPreemptionIsSupportedPreemptionDrmEnabledNoPreemptionWhenConfiguringHwInfoThenPreemptionIsNotSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = 1;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::Disabled, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenPreemptionDrmDisabledAllPreemptionWhenConfiguringHwInfoThenPreemptionIsNotSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::MidThread;
    drm->StoredPreemptionSupport = 0;
    drm->StoredDeviceID = hwConfigTestMidThreadBit | hwConfigTestThreadGroupBit | hwConfigTestMidBatchBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::Disabled, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_FALSE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenPreemptionDrmEnabledAllPreemptionDriverThreadGroupWhenConfiguringHwInfoThenPreemptionIsSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::ThreadGroup;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = hwConfigTestMidThreadBit | hwConfigTestThreadGroupBit | hwConfigTestMidBatchBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::ThreadGroup, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenPreemptionDrmEnabledAllPreemptionDriverMidBatchWhenConfiguringHwInfoThenPreemptionIsSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::MidBatch;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = hwConfigTestMidThreadBit | hwConfigTestThreadGroupBit | hwConfigTestMidBatchBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::MidBatch, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, GivenConfigPreemptionDrmEnabledAllPreemptionDriverDisabledWhenConfiguringHwInfoThenPreemptionIsSupported) {
    pInHwInfo.capabilityTable.defaultPreemptionMode = PreemptionMode::Disabled;
    drm->StoredPreemptionSupport =
        I915_SCHEDULER_CAP_ENABLED |
        I915_SCHEDULER_CAP_PRIORITY |
        I915_SCHEDULER_CAP_PREEMPTION;
    drm->StoredDeviceID = hwConfigTestMidThreadBit | hwConfigTestThreadGroupBit | hwConfigTestMidBatchBit;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(PreemptionMode::Disabled, outHwInfo.capabilityTable.defaultPreemptionMode);
    EXPECT_TRUE(drm->isPreemptionSupported());
}

TEST_F(HwInfoConfigTestLinuxDummy, givenPlatformEnabledFtrCompressionWhenInitializingThenFlagsAreSet) {
    pInHwInfo.capabilityTable.ftrRenderCompressedImages = true;
    pInHwInfo.capabilityTable.ftrRenderCompressedBuffers = true;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_TRUE(outHwInfo.capabilityTable.ftrRenderCompressedImages);
    EXPECT_TRUE(outHwInfo.capabilityTable.ftrRenderCompressedBuffers);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenPointerToHwInfoWhenConfigureHwInfoCalledThenRequiedSurfaceSizeIsSettedProperly) {
    EXPECT_EQ(MemoryConstants::pageSize, pInHwInfo.capabilityTable.requiredPreemptionSurfaceSize);
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(outHwInfo.gtSystemInfo.CsrSizeInMb * MemoryConstants::megaByte, outHwInfo.capabilityTable.requiredPreemptionSurfaceSize);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenInstrumentationForHardwareIsEnabledOrDisabledWhenConfiguringHwInfoThenOverrideItUsingHaveInstrumentation) {
    int ret;

    pInHwInfo.capabilityTable.instrumentationEnabled = false;
    ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    ASSERT_EQ(0, ret);
    EXPECT_FALSE(outHwInfo.capabilityTable.instrumentationEnabled);

    pInHwInfo.capabilityTable.instrumentationEnabled = true;
    ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    ASSERT_EQ(0, ret);
    EXPECT_TRUE(outHwInfo.capabilityTable.instrumentationEnabled);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenGttSizeReturnedWhenInitializingHwInfoThenSetSvmFtr) {
    drm->storedGTTSize = MemoryConstants::max64BitAppAddress;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(outHwInfo.capabilityTable.ftrSvm);

    drm->storedGTTSize = MemoryConstants::max64BitAppAddress + 1;
    ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_TRUE(outHwInfo.capabilityTable.ftrSvm);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenGttSizeReturnedWhenInitializingHwInfoThenSetGpuAddressSpace) {
    drm->storedGTTSize = maxNBitValue(40) + 1;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(drm->storedGTTSize - 1, outHwInfo.capabilityTable.gpuAddressSpace);
}

TEST_F(HwInfoConfigTestLinuxDummy, givenFailingGttSizeIoctlWhenInitializingHwInfoThenSetDefaultValues) {
    drm->StoredRetValForGetGttSize = -1;
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface);
    EXPECT_EQ(0, ret);

    EXPECT_TRUE(outHwInfo.capabilityTable.ftrSvm);
    EXPECT_NE(0u, outHwInfo.capabilityTable.gpuAddressSpace);
    EXPECT_EQ(pInHwInfo.capabilityTable.gpuAddressSpace, outHwInfo.capabilityTable.gpuAddressSpace);
}

TEST(HwInfoConfigLinuxTest, whenAdjustPlatformForProductFamilyCalledThenDoNothing) {
    HardwareInfo localHwInfo = *defaultHwInfo;

    auto hwInfoConfig = HwInfoConfig::get(localHwInfo.platform.eProductFamily);

    localHwInfo.platform.eDisplayCoreFamily = GFXCORE_FAMILY::IGFX_UNKNOWN_CORE;
    localHwInfo.platform.eRenderCoreFamily = GFXCORE_FAMILY::IGFX_UNKNOWN_CORE;

    hwInfoConfig->adjustPlatformForProductFamily(&localHwInfo);

    EXPECT_EQ(GFXCORE_FAMILY::IGFX_UNKNOWN_CORE, localHwInfo.platform.eRenderCoreFamily);
    EXPECT_EQ(GFXCORE_FAMILY::IGFX_UNKNOWN_CORE, localHwInfo.platform.eDisplayCoreFamily);
}
