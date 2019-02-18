/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/command_queue/command_queue.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/command_stream/tbx_command_stream_receiver.h"
#include "runtime/device/device.h"
#include "runtime/helpers/hw_helper.h"
#include "runtime/os_interface/os_context.h"
#include "unit_tests/aub_tests/command_stream/aub_command_stream_fixture.h"
#include "unit_tests/gen_common/gen_cmd_parse.h"
#include "unit_tests/helpers/memory_management.h"
#include "unit_tests/mocks/mock_device.h"
#include "unit_tests/tests_configuration.h"

#include "gtest/gtest.h"

namespace OCLRT {

void AUBCommandStreamFixture::SetUp(CommandQueue *pCmdQ) {
    ASSERT_NE(pCmdQ, nullptr);
    auto &device = reinterpret_cast<MockDevice &>(pCmdQ->getDevice());
    const auto &hwInfo = device.getHardwareInfo();
    auto &hwHelper = HwHelper::get(hwInfo.pPlatform->eRenderCoreFamily);

    const ::testing::TestInfo *const testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
    std::stringstream strfilename;
    auto engineType = pCmdQ->getCommandStreamReceiver().getOsContext().getEngineType();
    strfilename << testInfo->test_case_name() << "_" << testInfo->name() << "_" << hwHelper.getCsTraits(engineType.type).name;

    if (testMode == TestMode::AubTestsWithTbx) {
        pCommandStreamReceiver = TbxCommandStreamReceiver::create(hwInfo, strfilename.str(), true, *device.executionEnvironment);
    } else {
        pCommandStreamReceiver = AUBCommandStreamReceiver::create(hwInfo, strfilename.str(), true, *device.executionEnvironment);
    }
    ASSERT_NE(nullptr, pCommandStreamReceiver);

    device.resetCommandStreamReceiver(pCommandStreamReceiver);

    CommandStreamFixture::SetUp(pCmdQ);

    pTagMemory = pCommandStreamReceiver->getTagAddress();
    this->commandQueue = pCmdQ;
}

void AUBCommandStreamFixture::TearDown() {
    CommandStreamFixture::TearDown();
}
} // namespace OCLRT
