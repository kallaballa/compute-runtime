/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/test/common/helpers/default_hw_info.h"
#include "shared/test/unit_test/tests_configuration.h"

#include "opencl/source/platform/platform.h"
#include "opencl/test/unit_test/mocks/mock_aub_center.h"
#include "opencl/test/unit_test/mocks/mock_aub_manager.h"
#include "opencl/test/unit_test/mocks/mock_platform.h"

namespace NEO {
struct MockAubCenterFixture {

    MockAubCenterFixture() = default;
    MockAubCenterFixture(CommandStreamReceiverType commandStreamReceiverType) : commandStreamReceiverType(commandStreamReceiverType){};

    void SetUp() { // NOLINT(readability-identifier-naming)
        setMockAubCenter(*platform()->peekExecutionEnvironment()->rootDeviceEnvironments[0], commandStreamReceiverType);
    }
    void TearDown() { // NOLINT(readability-identifier-naming)
    }

    static void setMockAubCenter(RootDeviceEnvironment &rootDeviceEnvironment) {
        setMockAubCenter(rootDeviceEnvironment, CommandStreamReceiverType::CSR_AUB);
    }
    static void setMockAubCenter(RootDeviceEnvironment &rootDeviceEnvironment, CommandStreamReceiverType commandStreamReceiverType) {
        if (testMode != TestMode::AubTests && testMode != TestMode::AubTestsWithTbx) {
            auto mockAubCenter = std::make_unique<MockAubCenter>(defaultHwInfo.get(), false, "", commandStreamReceiverType);
            mockAubCenter->aubManager = std::make_unique<MockAubManager>();
            rootDeviceEnvironment.aubCenter.reset(mockAubCenter.release());
        }
    }

  protected:
    CommandStreamReceiverType commandStreamReceiverType = CommandStreamReceiverType::CSR_AUB;
};
} // namespace NEO
