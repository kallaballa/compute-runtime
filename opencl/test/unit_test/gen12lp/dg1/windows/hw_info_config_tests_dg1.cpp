/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/windows/os_interface.h"

#include "opencl/test/unit_test/os_interface/windows/hw_info_config_win_tests.h"

using namespace NEO;

using HwInfoConfigTestWindowsDg1 = HwInfoConfigTestWindows;

DG1TEST_F(HwInfoConfigTestWindowsDg1, whenCheckIfEvenContextCountIsRequiredThenReturnTrue) {
    EXPECT_EQ(IGFX_DG1, productFamily);

    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    EXPECT_TRUE(hwInfoConfig->isEvenContextCountRequired());
}
