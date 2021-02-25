/*
 * Copyright (C) 2017-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/os_interface/linux/drm_null_device.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"

#include "opencl/test/unit_test/linux/drm_wrap.h"
#include "opencl/test/unit_test/linux/mock_os_layer.h"
#include "test.h"

#include <memory>

using namespace NEO;

class DrmNullDeviceTestsFixture {
  public:
    void SetUp() {
        // Create nullDevice drm
        DebugManager.flags.EnableNullHardware.set(true);
        executionEnvironment.prepareRootDeviceEnvironments(1);

        drmNullDevice = DrmWrap::createDrm(*executionEnvironment.rootDeviceEnvironments[0]);
        ASSERT_NE(drmNullDevice, nullptr);
    }

    void TearDown() {
    }

    std::unique_ptr<Drm> drmNullDevice;
    ExecutionEnvironment executionEnvironment;

  protected:
    DebugManagerStateRestore dbgRestorer;
};

typedef Test<DrmNullDeviceTestsFixture> DrmNullDeviceTests;

TEST_F(DrmNullDeviceTests, GIVENdrmNullDeviceWHENcallGetDeviceIdTHENreturnProperDeviceId) {
    int deviceIdQueried = 0;
    int ret = drmNullDevice->getDeviceID(deviceIdQueried);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(deviceId, deviceIdQueried);
}

TEST_F(DrmNullDeviceTests, GIVENdrmNullDeviceWHENcallIoctlTHENalwaysSuccess) {
    EXPECT_EQ(drmNullDevice->ioctl(0, nullptr), 0);
}

TEST_F(DrmNullDeviceTests, GIVENdrmNullDeviceWHENregReadOtherThenTimestampReadTHENalwaysSuccess) {
    struct drm_i915_reg_read arg;

    arg.offset = 0;
    ASSERT_EQ(drmNullDevice->ioctl(DRM_IOCTL_I915_REG_READ, &arg), 0);
}

TEST_F(DrmNullDeviceTests, GIVENdrmNullDeviceWHENgetGpuTimestamp32bOr64bTHENerror) {
    struct drm_i915_reg_read arg;

    arg.offset = TIMESTAMP_LOW_REG;
    ASSERT_EQ(drmNullDevice->ioctl(DRM_IOCTL_I915_REG_READ, &arg), -1);

    arg.offset = TIMESTAMP_HIGH_REG;
    ASSERT_EQ(drmNullDevice->ioctl(DRM_IOCTL_I915_REG_READ, &arg), -1);
}

TEST_F(DrmNullDeviceTests, GIVENdrmNullDeviceWHENgetGpuTimestamp36bTHENproperValues) {
    struct drm_i915_reg_read arg;

    arg.offset = TIMESTAMP_LOW_REG | 1;
    ASSERT_EQ(drmNullDevice->ioctl(DRM_IOCTL_I915_REG_READ, &arg), 0);
    EXPECT_EQ(arg.val, 1000ULL);

    ASSERT_EQ(drmNullDevice->ioctl(DRM_IOCTL_I915_REG_READ, &arg), 0);
    EXPECT_EQ(arg.val, 2000ULL);

    ASSERT_EQ(drmNullDevice->ioctl(DRM_IOCTL_I915_REG_READ, &arg), 0);
    EXPECT_EQ(arg.val, 3000ULL);
}
