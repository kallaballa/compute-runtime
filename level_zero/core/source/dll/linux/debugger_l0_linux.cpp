/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/device/device.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/os_interface.h"

#include "level_zero/core/source/debugger/debugger_l0.h"

namespace L0 {
std::unique_ptr<NEO::Debugger> DebuggerL0::create(NEO::Device *device) {
    auto success = initDebuggingInOs(device->getRootDeviceEnvironment().osInterface.get());
    if (success) {
        auto debugger = debuggerL0Factory[device->getHardwareInfo().platform.eRenderCoreFamily](device);
        return std::unique_ptr<DebuggerL0>(debugger);
    }
    return std::unique_ptr<DebuggerL0>(nullptr);
}

} // namespace L0