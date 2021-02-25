/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/debug_manager_state_restore.h"

#include "opencl/test/unit_test/mocks/mock_wddm.h"
#include "opencl/test/unit_test/os_interface/windows/wddm_fixture.h"

using namespace NEO;

struct OsContextWinTest : public WddmTestWithMockGdiDll {
    void SetUp() override {
        WddmTestWithMockGdiDll::SetUp();
        preemptionMode = PreemptionHelper::getDefaultPreemptionMode(*defaultHwInfo);
        engineType = HwHelper::get(defaultHwInfo->platform.eRenderCoreFamily).getGpgpuEngineInstances(*defaultHwInfo)[0].first;

        init();
    }

    PreemptionMode preemptionMode;
    aub_stream::EngineType engineType;
};

TEST_F(OsContextWinTest, givenWddm20WhenCreatingOsContextThenOsContextIsInitialized) {
    osContext = std::make_unique<OsContextWin>(*osInterface->get()->getWddm(), 0u, 1, engineType, preemptionMode, false, false, false);
    EXPECT_TRUE(osContext->isInitialized());
}

TEST_F(OsContextWinTest, givenWddm20WhenCreatingWddmContextFailThenOsContextIsNotInitialized) {
    wddm->device = INVALID_HANDLE;

    osContext = std::make_unique<OsContextWin>(*osInterface->get()->getWddm(), 0u, 1, engineType, preemptionMode, false, false, false);
    EXPECT_FALSE(osContext->isInitialized());
}

TEST_F(OsContextWinTest, givenWddm20WhenCreatingWddmMonitorFenceFailThenOsContextIsNotInitialized) {
    *getCreateSynchronizationObject2FailCallFcn() = true;

    osContext = std::make_unique<OsContextWin>(*osInterface->get()->getWddm(), 0u, 1, engineType, preemptionMode, false, false, false);
    EXPECT_FALSE(osContext->isInitialized());
}

TEST_F(OsContextWinTest, givenWddm20WhenRegisterTrimCallbackFailThenOsContextIsNotInitialized) {
    *getRegisterTrimNotificationFailCallFcn() = true;

    osContext = std::make_unique<OsContextWin>(*osInterface->get()->getWddm(), 0u, 1, engineType, preemptionMode, false, false, false);
    EXPECT_FALSE(osContext->isInitialized());
}

TEST_F(OsContextWinTest, givenWddm20WhenRegisterTrimCallbackIsDisabledThenOsContextIsInitialized) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.DoNotRegisterTrimCallback.set(true);
    *getRegisterTrimNotificationFailCallFcn() = true;

    osContext = std::make_unique<OsContextWin>(*osInterface->get()->getWddm(), 0u, 1, engineType, preemptionMode, false, false, false);
    EXPECT_TRUE(osContext->isInitialized());
}
