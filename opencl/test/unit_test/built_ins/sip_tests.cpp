/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/built_ins/sip.h"
#include "shared/test/common/helpers/test_files.h"
#include "shared/test/common/mocks/mock_device.h"

#include "opencl/test/unit_test/global_environment.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

namespace SipKernelTests {

TEST(Sip, WhenGettingTypeThenCorrectTypeIsReturned) {
    SipKernel csr{SipKernelType::Csr, nullptr};
    EXPECT_EQ(SipKernelType::Csr, csr.getType());

    SipKernel dbgCsr{SipKernelType::DbgCsr, nullptr};
    EXPECT_EQ(SipKernelType::DbgCsr, dbgCsr.getType());

    SipKernel dbgCsrLocal{SipKernelType::DbgCsrLocal, nullptr};
    EXPECT_EQ(SipKernelType::DbgCsrLocal, dbgCsrLocal.getType());

    SipKernel undefined{SipKernelType::COUNT, nullptr};
    EXPECT_EQ(SipKernelType::COUNT, undefined.getType());
}

TEST(Sip, givenSipKernelClassWhenAskedForMaxDebugSurfaceSizeThenCorrectValueIsReturned) {
    EXPECT_EQ(0x1800000u, SipKernel::maxDbgSurfaceSize);
}

TEST(Sip, givenDebuggingInactiveWhenSipTypeIsQueriedThenCsrSipTypeIsReturned) {
    auto sipType = SipKernel::getSipKernelType(renderCoreFamily, false);
    EXPECT_EQ(SipKernelType::Csr, sipType);
}

TEST(DebugSip, givenDebuggingActiveWhenSipTypeIsQueriedThenDbgCsrSipTypeIsReturned) {
    auto sipType = SipKernel::getSipKernelType(renderCoreFamily, true);
    EXPECT_LE(SipKernelType::DbgCsr, sipType);
}

TEST(DebugSip, givenBuiltInsWhenDbgCsrSipIsRequestedThanCorrectSipKernelIsReturned) {
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    EXPECT_NE(nullptr, mockDevice);

    auto &builtins = *mockDevice->getBuiltIns();
    auto &sipKernel = builtins.getSipKernel(SipKernelType::DbgCsr, *mockDevice);

    EXPECT_NE(nullptr, &sipKernel);
    EXPECT_EQ(SipKernelType::DbgCsr, sipKernel.getType());
}

} // namespace SipKernelTests
