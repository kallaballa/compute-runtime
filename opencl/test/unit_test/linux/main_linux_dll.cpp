/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/linux/allocator_helper.h"
#include "shared/source/os_interface/linux/os_interface.h"
#include "shared/source/os_interface/linux/sys_calls.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/default_hw_info.inl"
#include "shared/test/common/helpers/ult_hw_config.inl"
#include "shared/test/common/helpers/variable_backup.h"

#include "opencl/test/unit_test/custom_event_listener.h"
#include "opencl/test/unit_test/linux/drm_wrap.h"
#include "opencl/test/unit_test/linux/mock_os_layer.h"
#include "opencl/test/unit_test/mocks/mock_execution_environment.h"
#include "opencl/test/unit_test/os_interface/linux/device_command_stream_fixture.h"
#include "test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <string>

namespace NEO {
void __attribute__((destructor)) platformsDestructor();
}
using namespace NEO;

class DrmTestsFixture {
  public:
    void SetUp() {
        executionEnvironment.prepareRootDeviceEnvironments(1);
        rootDeviceEnvironment = executionEnvironment.rootDeviceEnvironments[0].get();
    }

    void TearDown() {
    }
    ExecutionEnvironment executionEnvironment;
    RootDeviceEnvironment *rootDeviceEnvironment = nullptr;
};

typedef Test<DrmTestsFixture> DrmTests;

void initializeTestedDevice() {
    for (uint32_t i = 0; deviceDescriptorTable[i].eGtType != GTTYPE::GTTYPE_UNDEFINED; i++) {
        if (defaultHwInfo->platform.eProductFamily == deviceDescriptorTable[i].pHwInfo->platform.eProductFamily) {
            deviceId = deviceDescriptorTable[i].deviceId;
            break;
        }
    }
}

int openRetVal = 0;
std::string lastOpenedPath;
int testOpen(const char *fullPath, int, ...) {
    return openRetVal;
};

int openCounter = 1;
int openWithCounter(const char *fullPath, int, ...) {
    if (openCounter > 0) {
        if (fullPath) {
            lastOpenedPath = fullPath;
        }
        openCounter--;
        return 1023; // valid file descriptor for ULT
    }
    return -1;
};

TEST(DrmTest, GivenTwoOpenableDevicesWhenDiscoverDevicesThenCreateTwoHwDeviceIds) {
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = openWithCounter;
    openCounter = 2;
    ExecutionEnvironment executionEnvironment;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_EQ(2u, hwDeviceIds.size());
}

TEST(DrmTest, GivenSelectedNotExistingDeviceWhenGetDeviceFdThenFail) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.ForceDeviceId.set("1234");
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = testOpen;
    openRetVal = -1;
    ExecutionEnvironment executionEnvironment;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_TRUE(hwDeviceIds.empty());
}

TEST(DrmTest, GivenSelectedExistingDeviceWhenGetDeviceFdThenReturnFd) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.ForceDeviceId.set("1234");
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openRetVal = 1023; // fakeFd
    openFull = testOpen;
    ExecutionEnvironment executionEnvironment;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_EQ(1u, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
}

TEST(DrmTest, GivenSelectedExistingDeviceWhenOpenDirSuccedsThenHwDeviceIdsHaveProperPciPaths) {
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    VariableBackup<decltype(failOnOpenDir)> backupOpenDir(&failOnOpenDir, false);
    VariableBackup<decltype(entryIndex)> backupEntryIndex(&entryIndex, 0u);
    openFull = openWithCounter;

    ExecutionEnvironment executionEnvironment;

    entryIndex = 0;
    openCounter = 1;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_EQ(1u, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
    EXPECT_STREQ("test1", hwDeviceIds[0]->getPciPath());

    entryIndex = 0;
    openCounter = 2;
    hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_EQ(2u, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
    EXPECT_STREQ("test1", hwDeviceIds[0]->getPciPath());
    EXPECT_NE(nullptr, hwDeviceIds[1].get());
    EXPECT_STREQ("test2", hwDeviceIds[1]->getPciPath());
}

TEST(DrmTest, GivenSelectedExistingDeviceWhenOpenDirFailsThenRetryOpeningRenderDevices) {
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    VariableBackup<decltype(failOnOpenDir)> backupOpenDir(&failOnOpenDir, true);
    VariableBackup<decltype(readLinkCalledTimes)> backupReadlink(&readLinkCalledTimes, 0);
    openFull = openWithCounter;
    openCounter = 1;

    ExecutionEnvironment executionEnvironment;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_STREQ("/dev/dri/renderD128", lastOpenedPath.c_str());
    EXPECT_EQ(1u, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
    EXPECT_STREQ("00:02.0", hwDeviceIds[0]->getPciPath());

    openCounter = 2;
    hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_STREQ("/dev/dri/renderD129", lastOpenedPath.c_str());
    EXPECT_EQ(2u, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
    EXPECT_STREQ("00:02.0", hwDeviceIds[0]->getPciPath());
    EXPECT_NE(nullptr, hwDeviceIds[1].get());
    EXPECT_STREQ("00:03.0", hwDeviceIds[1]->getPciPath());
}

TEST(DrmTest, GivenSelectedNonExistingDeviceWhenOpenDirFailsThenRetryOpeningRenderDevicesAndNoDevicesAreCreated) {
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    VariableBackup<decltype(failOnOpenDir)> backupOpenDir(&failOnOpenDir, true);
    openFull = openWithCounter;
    openCounter = 0;

    ExecutionEnvironment executionEnvironment;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_EQ(0u, hwDeviceIds.size());
}

TEST(DrmTest, GivenFailingOpenDirAndMultipleAvailableDevicesWhenCreateMultipleRootDevicesFlagIsSetThenTheFlagIsRespected) {
    DebugManagerStateRestore stateRestore;
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    VariableBackup<decltype(failOnOpenDir)> backupOpenDir(&failOnOpenDir, true);
    VariableBackup<decltype(readLinkCalledTimes)> backupReadlink(&readLinkCalledTimes, 0);
    openFull = openWithCounter;
    ExecutionEnvironment executionEnvironment;
    const uint32_t requestedNumRootDevices = 2u;
    DebugManager.flags.CreateMultipleRootDevices.set(requestedNumRootDevices);

    openCounter = 4;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_STREQ("/dev/dri/renderD129", lastOpenedPath.c_str());
    EXPECT_EQ(requestedNumRootDevices, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
    EXPECT_STREQ("00:02.0", hwDeviceIds[0]->getPciPath());
    EXPECT_NE(nullptr, hwDeviceIds[1].get());
    EXPECT_STREQ("00:03.0", hwDeviceIds[1]->getPciPath());
}

TEST(DrmTest, GivenMultipleAvailableDevicesWhenCreateMultipleRootDevicesFlagIsSetThenTheFlagIsRespected) {
    DebugManagerStateRestore stateRestore;
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = openWithCounter;
    ExecutionEnvironment executionEnvironment;
    const uint32_t requestedNumRootDevices = 2u;
    DebugManager.flags.CreateMultipleRootDevices.set(requestedNumRootDevices);

    openCounter = 4;
    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_STREQ("/dev/dri/by-path/pci-0000:test2-render", lastOpenedPath.c_str());
    EXPECT_EQ(requestedNumRootDevices, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
    EXPECT_STREQ("test1", hwDeviceIds[0]->getPciPath());
    EXPECT_NE(nullptr, hwDeviceIds[1].get());
    EXPECT_STREQ("test2", hwDeviceIds[1]->getPciPath());
}

TEST(DrmTest, GivenSelectedIncorectDeviceWhenGetDeviceFdThenFail) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.ForceDeviceId.set("1234");
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = testOpen;
    openRetVal = 1024;
    ExecutionEnvironment executionEnvironment;

    auto hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    EXPECT_TRUE(hwDeviceIds.empty());
}

TEST(DrmTest, givenUseVmBindFlagWhenOverrideBindSupportThenReturnProperValue) {
    DebugManagerStateRestore dbgRestorer;
    bool useVmBind = false;

    DebugManager.flags.UseVmBind.set(1);
    Drm::overrideBindSupport(useVmBind);
    EXPECT_TRUE(useVmBind);

    DebugManager.flags.UseVmBind.set(0);
    Drm::overrideBindSupport(useVmBind);
    EXPECT_FALSE(useVmBind);

    DebugManager.flags.UseVmBind.set(-1);
    Drm::overrideBindSupport(useVmBind);
    EXPECT_FALSE(useVmBind);
}

TEST_F(DrmTests, GivenErrorCodeWhenCreatingDrmThenDrmCreatedOnlyWithSpecificErrors) {
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);

    drm_i915_getparam_t getParam;
    int lDeviceId;

    VariableBackup<decltype(ioctlCnt)> backupIoctlCnt(&ioctlCnt);
    VariableBackup<int> backupIoctlSeq(&ioctlSeq[0]);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = EINTR;
    // check if device works, although there was EINTR error from KMD
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    auto ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(deviceId, lDeviceId);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = EAGAIN;
    // check if device works, although there was EAGAIN error from KMD
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(deviceId, lDeviceId);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = EBUSY;
    // check if device works, although there was EBUSY error from KMD
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(deviceId, lDeviceId);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = 0;
    // we failed with any other error code
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(deviceId, lDeviceId);
}

TEST_F(DrmTests, WhenCreatingTwiceThenDifferentDrmReturned) {
    auto drm1 = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm1, nullptr);
    auto drm2 = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm2, nullptr);
    EXPECT_NE(drm1, drm2);
}

TEST_F(DrmTests, WhenDriDeviceFoundThenDrmCreatedOnFallback) {
    VariableBackup<decltype(haveDri)> backupHaveDri(&haveDri);

    haveDri = 1;
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
}

TEST_F(DrmTests, GivenNoDeviceWhenCreatingDrmThenNullIsReturned) {
    VariableBackup<decltype(haveDri)> backupHaveDri(&haveDri);
    haveDri = -1;
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, GivenUnknownDeviceWhenCreatingDrmThenNullIsReturned) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.PrintDebugMessages.set(true);

    VariableBackup<decltype(deviceId)> backupDeviceId(&deviceId);

    deviceId = -1;

    ::testing::internal::CaptureStderr();
    ::testing::internal::CaptureStdout();
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
    std::string errStr = ::testing::internal::GetCapturedStderr();
    EXPECT_THAT(errStr, ::testing::HasSubstr(std::string("FATAL: Unknown device: deviceId: ffffffff, revisionId: 0000")));
    ::testing::internal::GetCapturedStdout();
}

TEST_F(DrmTests, GivenNoSoftPinWhenCreatingDrmThenNullIsReturned) {
    VariableBackup<decltype(haveSoftPin)> backupHaveSoftPin(&haveSoftPin);
    haveSoftPin = 0;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, WhenCantFindDeviceIdThenDrmIsNotCreated) {
    VariableBackup<decltype(failOnDeviceId)> backupFailOnDeviceId(&failOnDeviceId);
    failOnDeviceId = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, WhenCantQueryEuCountThenDrmIsNotCreated) {
    VariableBackup<decltype(failOnEuTotal)> backupfailOnEuTotal(&failOnEuTotal);
    failOnEuTotal = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, WhenCantQuerySubsliceCountThenDrmIsNotCreated) {
    VariableBackup<decltype(failOnSubsliceTotal)> backupfailOnSubsliceTotal(&failOnSubsliceTotal);
    failOnSubsliceTotal = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, WhenCantQueryRevisionIdThenDrmIsNotCreated) {
    VariableBackup<decltype(failOnRevisionId)> backupFailOnRevisionId(&failOnRevisionId);
    failOnRevisionId = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, WhenCantQuerySoftPinSupportThenDrmIsNotCreated) {
    VariableBackup<decltype(failOnSoftPin)> backupFailOnSoftPin(&failOnSoftPin);
    failOnSoftPin = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, GivenFailOnParamBoostWhenCreatingDrmThenDrmIsCreated) {
    VariableBackup<decltype(failOnParamBoost)> backupFailOnParamBoost(&failOnParamBoost);
    failOnParamBoost = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    //non-fatal error - issue warning only
    EXPECT_NE(drm, nullptr);
}

TEST_F(DrmTests, GivenFailOnContextCreateWhenCreatingDrmThenDrmIsCreated) {
    VariableBackup<decltype(failOnContextCreate)> backupFailOnContextCreate(&failOnContextCreate);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
    failOnContextCreate = -1;
    EXPECT_THROW(drm->createDrmContext(1, false), std::exception);
    EXPECT_FALSE(drm->isPreemptionSupported());
    failOnContextCreate = 0;
}

TEST_F(DrmTests, GivenFailOnSetPriorityWhenCreatingDrmThenDrmIsCreated) {
    VariableBackup<decltype(failOnSetPriority)> backupFailOnSetPriority(&failOnSetPriority);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
    failOnSetPriority = -1;
    auto drmContext = drm->createDrmContext(1, false);
    EXPECT_THROW(drm->setLowPriorityContextParam(drmContext), std::exception);
    EXPECT_FALSE(drm->isPreemptionSupported());
    failOnSetPriority = 0;
}

TEST_F(DrmTests, WhenCantQueryDrmVersionThenDrmIsNotCreated) {
    VariableBackup<decltype(failOnDrmVersion)> backupFailOnDrmVersion(&failOnDrmVersion);

    failOnDrmVersion = -1;
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
    failOnDrmVersion = 0;
}

TEST_F(DrmTests, GivenInvalidDrmVersionNameWhenCreatingDrmThenNullIsReturned) {
    VariableBackup<decltype(failOnDrmVersion)> backupFailOnDrmVersion(&failOnDrmVersion);

    strcpy(providedDrmVersion, "NA");
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
    failOnDrmVersion = 0;
    strcpy(providedDrmVersion, "i915");
}

TEST_F(DrmTests, whenDrmIsCreatedThenSetMemoryRegionsDoesntFailAndDrmObjectIsReturned) {
    DebugManagerStateRestore restore;
    DebugManager.flags.EnableLocalMemory.set(1);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
}

TEST(AllocatorHelper, givenExpectedSizeToReserveWhenGetSizeToReserveCalledThenExpectedValueReturned) {
    EXPECT_EQ((maxNBitValue(47) + 1) / 4, NEO::getSizeToReserve());
}

TEST(DrmMemoryManagerCreate, whenCallCreateMemoryManagerThenDrmMemoryManagerIsCreated) {
    MockExecutionEnvironment executionEnvironment(defaultHwInfo.get());
    auto drm = new DrmMockSuccess(fakeFd, *executionEnvironment.rootDeviceEnvironments[0]);

    executionEnvironment.rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    executionEnvironment.rootDeviceEnvironments[0]->osInterface->get()->setDrm(drm);
    auto drmMemoryManager = MemoryManager::createMemoryManager(executionEnvironment);
    EXPECT_NE(nullptr, drmMemoryManager.get());
    executionEnvironment.memoryManager = std::move(drmMemoryManager);
}

TEST(OsInterfaceTests, givenOsInterfaceWhenEnableLocalMemoryIsSpecifiedThenItIsSetToTrueOn64Bit) {
    EXPECT_TRUE(OSInterface::osEnableLocalMemory);
}

TEST_F(DrmTests, whenDrmIsCreatedWithMultipleSubDevicesThenCreateMultipleVirtualMemoryAddressSpaces) {
    DebugManagerStateRestore restore;
    DebugManager.flags.CreateMultipleSubDevices.set(2);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);

    if (drm->isPerContextVMRequired()) {
        GTEST_SKIP();
    }

    auto numSubDevices = HwHelper::getSubDevicesCount(rootDeviceEnvironment->getHardwareInfo());
    for (auto id = 0u; id < numSubDevices; id++) {
        EXPECT_EQ(id + 1, drm->getVirtualMemoryAddressSpace(id));
    }
}

TEST_F(DrmTests, givenDebuggingEnabledWhenDrmIsCreatedThenPerContextVMIsTrueGetVirtualMemoryAddressSpaceReturnsZeroAndVMsAreNotCreated) {
    DebugManagerStateRestore restore;
    DebugManager.flags.CreateMultipleSubDevices.set(2);
    DebugManager.flags.UseVmBind.set(1);

    rootDeviceEnvironment->executionEnvironment.setDebuggingEnabled();

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    ASSERT_NE(drm, nullptr);
    if (drm->isVmBindAvailable()) {
        EXPECT_TRUE(drm->isPerContextVMRequired());

        auto numSubDevices = HwHelper::getSubDevicesCount(rootDeviceEnvironment->getHardwareInfo());
        for (auto id = 0u; id < numSubDevices; id++) {
            EXPECT_EQ(0u, drm->getVirtualMemoryAddressSpace(id));
        }
        EXPECT_EQ(0u, static_cast<DrmWrap *>(drm.get())->virtualMemoryIds.size());
    }
}

TEST_F(DrmTests, givenEnabledDebuggingAndVmBindNotAvailableWhenDrmIsCreatedThenPerContextVMIsFalseVMsAreCreatedAndDebugMessageIsPrinted) {
    DebugManagerStateRestore restore;

    ::testing::internal::CaptureStderr();
    ::testing::internal::CaptureStdout();

    DebugManager.flags.CreateMultipleSubDevices.set(2);
    DebugManager.flags.UseVmBind.set(0);
    DebugManager.flags.PrintDebugMessages.set(true);

    rootDeviceEnvironment->executionEnvironment.setDebuggingEnabled();

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);

    if (drm->isPerContextVMRequired()) {
        ::testing::internal::GetCapturedStdout();
        ::testing::internal::GetCapturedStderr();
        GTEST_SKIP();
    }

    auto numSubDevices = HwHelper::getSubDevicesCount(rootDeviceEnvironment->getHardwareInfo());
    for (auto id = 0u; id < numSubDevices; id++) {
        EXPECT_NE(0u, drm->getVirtualMemoryAddressSpace(id));
    }
    EXPECT_NE(0u, static_cast<DrmWrap *>(drm.get())->virtualMemoryIds.size());

    DebugManager.flags.PrintDebugMessages.set(false);
    ::testing::internal::GetCapturedStdout();
    std::string errStr = ::testing::internal::GetCapturedStderr();

    EXPECT_THAT(errStr, ::testing::HasSubstr(std::string("WARNING: Debugging not supported\n")));
}

TEST_F(DrmTests, givenDrmIsCreatedWhenCreateVirtualMemoryFailsThenReturnVirtualMemoryIdZeroAndPrintDebugMessage) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.PrintDebugMessages.set(true);

    VariableBackup<decltype(failOnVirtualMemoryCreate)> backupFailOnVirtualMemoryCreate(&failOnVirtualMemoryCreate);

    failOnVirtualMemoryCreate = -1;

    ::testing::internal::CaptureStderr();
    ::testing::internal::CaptureStdout();
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);

    EXPECT_EQ(0u, drm->getVirtualMemoryAddressSpace(0));
    EXPECT_EQ(0u, static_cast<DrmWrap *>(drm.get())->virtualMemoryIds.size());

    std::string errStr = ::testing::internal::GetCapturedStderr();
    if (!drm->isPerContextVMRequired()) {

        EXPECT_THAT(errStr, ::testing::HasSubstr(std::string("INFO: Device doesn't support GEM Virtual Memory")));
    }
    ::testing::internal::GetCapturedStdout();
}

TEST(SysCalls, WhenSysCallsPollCalledThenCallIsRedirectedToOs) {
    struct pollfd pollFd;
    pollFd.fd = 0;
    pollFd.events = 0;

    auto result = NEO::SysCalls::poll(&pollFd, 1, 0);
    EXPECT_EQ(0, result);
}

TEST(SysCalls, WhenSysCallsFstatCalledThenCallIsRedirectedToOs) {
    struct stat st = {};
    auto result = NEO::SysCalls::fstat(0, &st);
    EXPECT_EQ(0, result);
}

int main(int argc, char **argv) {
    bool useDefaultListener = false;

    ::testing::InitGoogleTest(&argc, argv);

    // parse remaining args assuming they're mine
    for (int i = 1; i < argc; ++i) {
        if (!strcmp("--disable_default_listener", argv[i])) {
            useDefaultListener = false;
        } else if (!strcmp("--enable_default_listener", argv[i])) {
            useDefaultListener = true;
        }
    }

    if (useDefaultListener == false) {
        auto &listeners = ::testing::UnitTest::GetInstance()->listeners();
        auto defaultListener = listeners.default_result_printer();
        auto customEventListener = new CCustomEventListener(defaultListener);

        listeners.Release(defaultListener);
        listeners.Append(customEventListener);
    }

    defaultHwInfo = std::make_unique<HardwareInfo>();
    *defaultHwInfo = DEFAULT_TEST_PLATFORM::hwInfo;

    initializeTestedDevice();

    auto retVal = RUN_ALL_TESTS();

    return retVal;
}

TEST_F(DrmTests, whenCreateDrmIsCalledThenProperHwInfoIsSetup) {
    auto oldHwInfo = rootDeviceEnvironment->getMutableHardwareInfo();
    *oldHwInfo = {};
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
    auto currentHwInfo = rootDeviceEnvironment->getHardwareInfo();
    EXPECT_NE(IGFX_UNKNOWN, currentHwInfo->platform.eProductFamily);
    EXPECT_NE(IGFX_UNKNOWN_CORE, currentHwInfo->platform.eRenderCoreFamily);
    EXPECT_LT(0u, currentHwInfo->gtSystemInfo.EUCount);
    EXPECT_LT(0u, currentHwInfo->gtSystemInfo.SubSliceCount);
}

TEST(PlatformsDestructor, whenGlobalPlatformsDestructorIsCalledThenGlobalPlatformsAreDestroyed) {
    EXPECT_NE(nullptr, platformsImpl);
    platformsDestructor();

    EXPECT_EQ(nullptr, platformsImpl);
    platformsImpl = new std::vector<std::unique_ptr<Platform>>;
}
