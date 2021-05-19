/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/mocks/mock_wddm.h"

namespace NEO {
Wddm *Wddm::createWddm(std::unique_ptr<HwDeviceId> hwDeviceId, RootDeviceEnvironment &rootDeviceEnvironment) {
    return new WddmMock(rootDeviceEnvironment);
}
} // namespace NEO
