/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/fixtures/preemption_fixture.h"

#include "shared/source/command_stream/preemption.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/test/common/cmd_parse/hw_parse.h"
#include "shared/test/common/mocks/mock_device.h"

#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

DevicePreemptionTests::DevicePreemptionTests() = default;
DevicePreemptionTests::~DevicePreemptionTests() = default;

void DevicePreemptionTests::SetUp() {
    if (dbgRestore == nullptr) {
        dbgRestore.reset(new DebugManagerStateRestore());
    }

    device = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    executionEnvironment.reset(new SPatchExecutionEnvironment);
    memset(executionEnvironment.get(), 0, sizeof(SPatchExecutionEnvironment));

    ASSERT_NE(nullptr, device);

    waTable = &device->getRootDeviceEnvironment().getMutableHardwareInfo()->workaroundTable;
}

void DevicePreemptionTests::TearDown() {
    dbgRestore.reset();
}
